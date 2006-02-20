/* qdbusintegrator.cpp QDBusConnection private implementation
 *
 * Copyright (C) 2005 Harald Fernengel <harry@kdevelop.org>
 * Copyright (C) 2006 Trolltech AS. All rights reserved.
 *    Author: Thiago Macieira <thiago.macieira@trolltech.com>
 *
 * Licensed under the Academic Free License version 2.1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <qcoreapplication.h>
#include <qcoreevent.h>
#include <qdebug.h>
#include <qmetaobject.h>
#include <qsocketnotifier.h>
#include <qcoreevent.h>
#include <qtimer.h>

#include "qdbusvariant.h"
#include "qdbusconnection_p.h"
#include "qdbusinterface_p.h"
#include "qdbusobject_p.h"
#include "qdbusmessage.h"
#include "qdbusabstractadaptor.h"

int QDBusConnectionPrivate::messageMetaType = 0;

struct QDBusPendingCall
{
    QPointer<QObject> receiver;
    QList<int> metaTypes;
    int methodIdx;
    DBusPendingCall *pending;
    const QDBusConnectionPrivate *connection;
};

class CallDeliveryEvent: public QEvent
{
public:
    CallDeliveryEvent()
        : QEvent(QEvent::User), object(0), flags(0), slotIdx(-1)
        { }

    const QDBusConnectionPrivate *conn;
    QPointer<QObject> object;
    QDBusMessage message;
    QList<int> metaTypes;
    
    int flags;
    int slotIdx;
};

static dbus_bool_t qDBusAddTimeout(DBusTimeout *timeout, void *data)
{
    Q_ASSERT(timeout);
    Q_ASSERT(data);

  //  qDebug("addTimeout %d", dbus_timeout_get_interval(timeout));

    QDBusConnectionPrivate *d = static_cast<QDBusConnectionPrivate *>(data);

    if (!dbus_timeout_get_enabled(timeout))
        return true;

    if (!QCoreApplication::instance()) {
        d->pendingTimeouts.append(timeout);
        return true;
    }
    int timerId = d->startTimer(dbus_timeout_get_interval(timeout));
    if (!timerId)
        return false;

    d->timeouts[timerId] = timeout;
    return true;
}

static void qDBusRemoveTimeout(DBusTimeout *timeout, void *data)
{
    Q_ASSERT(timeout);
    Q_ASSERT(data);

  //  qDebug("removeTimeout");

    QDBusConnectionPrivate *d = static_cast<QDBusConnectionPrivate *>(data);
    d->pendingTimeouts.removeAll(timeout);

    QDBusConnectionPrivate::TimeoutHash::iterator it = d->timeouts.begin();
    while (it != d->timeouts.end()) {
        if (it.value() == timeout) {
            d->killTimer(it.key());
            it = d->timeouts.erase(it);
        } else {
            ++it;
        }
    }
}

static void qDBusToggleTimeout(DBusTimeout *timeout, void *data)
{
    Q_ASSERT(timeout);
    Q_ASSERT(data);

    qDebug("ToggleTimeout");

    qDBusRemoveTimeout(timeout, data);
    qDBusAddTimeout(timeout, data);
}

static dbus_bool_t qDBusAddWatch(DBusWatch *watch, void *data)
{
    Q_ASSERT(watch);
    Q_ASSERT(data);

    QDBusConnectionPrivate *d = static_cast<QDBusConnectionPrivate *>(data);

    int flags = dbus_watch_get_flags(watch);
    int fd = dbus_watch_get_fd(watch);

    QDBusConnectionPrivate::Watcher watcher;
    if (flags & DBUS_WATCH_READABLE) {
        qDebug("addReadWatch %d", fd);
        watcher.watch = watch;
        if (QCoreApplication::instance()) {
            watcher.read = new QSocketNotifier(fd, QSocketNotifier::Read, d);
            watcher.read->setEnabled(dbus_watch_get_enabled(watch));
            d->connect(watcher.read, SIGNAL(activated(int)), SLOT(socketRead(int)));
        }
    }
    if (flags & DBUS_WATCH_WRITABLE) {
        qDebug("addWriteWatch %d", fd);
        watcher.watch = watch;
        if (QCoreApplication::instance()) {
            watcher.write = new QSocketNotifier(fd, QSocketNotifier::Write, d);
            watcher.write->setEnabled(dbus_watch_get_enabled(watch));
            d->connect(watcher.write, SIGNAL(activated(int)), SLOT(socketWrite(int)));
        }
    }
    d->watchers.insertMulti(fd, watcher);

    return true;
}

static void qDBusRemoveWatch(DBusWatch *watch, void *data)
{
    Q_ASSERT(watch);
    Q_ASSERT(data);

    qDebug("remove watch");

    QDBusConnectionPrivate *d = static_cast<QDBusConnectionPrivate *>(data);
    int fd = dbus_watch_get_fd(watch);

    QDBusConnectionPrivate::WatcherHash::iterator i = d->watchers.find(fd);
    while (i != d->watchers.end() && i.key() == fd) {
        if (i.value().watch == watch) {
            delete i.value().read;
            delete i.value().write;
            d->watchers.erase(i);
            return;
        }
        ++i;
    }
}

static void qDBusToggleWatch(DBusWatch *watch, void *data)
{
    Q_ASSERT(watch);
    Q_ASSERT(data);

    QDBusConnectionPrivate *d = static_cast<QDBusConnectionPrivate *>(data);
    int fd = dbus_watch_get_fd(watch);

    QDBusConnectionPrivate::WatcherHash::iterator i = d->watchers.find(fd);
    while (i != d->watchers.end() && i.key() == fd) {
        if (i.value().watch == watch) {
            bool enabled = dbus_watch_get_enabled(watch);
            int flags = dbus_watch_get_flags(watch);

            qDebug("toggle watch %d to %d (write: %d, read: %d)", dbus_watch_get_fd(watch), enabled, flags & DBUS_WATCH_WRITABLE, flags & DBUS_WATCH_READABLE);

            if (flags & DBUS_WATCH_READABLE && i.value().read)
                i.value().read->setEnabled(enabled);
            if (flags & DBUS_WATCH_WRITABLE && i.value().write)
                i.value().write->setEnabled(enabled);
            return;
        }
        ++i;
    }
}

static void qDBusNewConnection(DBusServer *server, DBusConnection *c, void *data)
{
    Q_ASSERT(data); Q_ASSERT(server); Q_ASSERT(c);

    qDebug("SERVER: GOT A NEW CONNECTION"); // TODO
}

static DBusHandlerResult qDBusSignalFilter(DBusConnection *connection,
                                           DBusMessage *message, void *data)
{
    Q_ASSERT(data);
    Q_UNUSED(connection);

    QDBusConnectionPrivate *d = static_cast<QDBusConnectionPrivate *>(data);
    if (d->mode == QDBusConnectionPrivate::InvalidMode)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    int msgType = dbus_message_get_type(message);
    bool handled = false;

    QDBusMessage amsg = QDBusMessage::fromDBusMessage(message);
    qDebug() << "got message:" << amsg;

    if (msgType == DBUS_MESSAGE_TYPE_SIGNAL) {
        handled = d->handleSignal(amsg);
    } else if (msgType == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        handled = d->handleObjectCall(amsg);
    }

    return handled ? DBUS_HANDLER_RESULT_HANDLED :
            DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static bool checkAsyncTag(const char *tag)
{
    if (!tag || !*tag)
        return false;

    const char *p = strstr(tag, "async");
    if (p != NULL &&
        (p == tag || *(p-1) == ' ') &&
        (p[6] == '\0' || p[6] == ' '))
        return true;

    p = strstr(tag, "Q_ASYNC");
    if (p != NULL &&
        (p == tag || *(p-1) == ' ') &&
        (p[8] == '\0' || p[8] == ' '))
        return true;

    return false;
}

static QList<QByteArray> splitParameters(const char *p)
{
    QList<QByteArray> retval;
    ++p;
    const char *e = p;
    while (*e != ')') {
        while (*e != ')' && *e != ',')
            ++e;

        // found the end of this parameter
        retval += QByteArray(p, e - p);

        if (*e != ')')
            p = ++e;
    }
    return retval;
}

static bool typesMatch(int metaId, QVariant::Type variantType)
{
    if (metaId == (int)variantType)
        return true;

    if (variantType == QVariant::Int && metaId == QMetaType::Short)
        return true;

    if (variantType == QVariant::UInt && (metaId == QMetaType::UShort ||
                                          metaId == QMetaType::UChar))
        return true;

    return false;               // no match
}

static int returnTypeId(const char *name)
{
    if (!name || !*name)
        return QMetaType::Void;
    
    // force normalizedSignature to work for us
    QVarLengthArray<char, 32> buf(strlen(name) + 3);
    buf.append("_(", 2);
    buf.append(name, strlen(name));
    buf.append(')');
    
    QByteArray normalized = QMetaObject::normalizedSignature( buf.data() );
    normalized.truncate(normalized.length() - 1);
    return QMetaType::type(normalized.constData() + 2);
}

static int typeId(const char *type)
{
    int id = static_cast<int>( QVariant::nameToType(type) );
    if (id == QVariant::UserType)
        id = QMetaType::type(type);

    switch (id) {
    case QVariant::Bool:
    case QVariant::Int:
    case QVariant::UInt:
    case QVariant::Char:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::UChar:
    case QVariant::LongLong:
    case QVariant::ULongLong:
    case QVariant::Double:
    case QVariant::String:
    case QVariant::Date:
    case QVariant::Time:
    case QVariant::DateTime:
    case QVariant::Map:
    case QVariant::StringList:
    case QVariant::ByteArray:
    case QVariant::List:
        return id;

    default:
        if (id == qMetaTypeId<QDBusVariant>() || id == QDBusConnectionPrivate::messageMetaType)
            return id;
        
        return 0;               // invalid
    }
}
    

// calculates the metatypes for the method
// the slot must have the parameters in the following form:
//  - zero or more value or const-ref parameters of any kind
//  - zero or one const ref of QDBusMessage
//  - zero or more non-const ref parameters
// No parameter may be a template.
// this function returns -1 if the parameters don't match the above form
// this function returns the number of *input* parameters, including the QDBusMessage one if any
// this function does not check the return type, so metaTypes[0] is always 0 and always present
// metaTypes.count() >= retval + 1 in all cases
//
// sig must be the normalised signature for the method
static int parametersForMethod(const QByteArray &sig, QList<int>& metaTypes)
{
    if (sig.indexOf('<') != -1) {
        qWarning("Could not parse the method '%s'", sig.constData());
        // there's no type with templates that we can handle
        return -1;
    }

    int paren = sig.indexOf('(');
    QList<QByteArray> parameterTypes = splitParameters(sig.data() + paren);
    metaTypes.clear();

    metaTypes.append(0);        // return type
    int inputCount = 0;
    bool seenMessage = false;
    foreach (QByteArray type, parameterTypes) {
        if (type.endsWith('*')) {
            qWarning("Could not parse the method '%s'", sig.constData());
            // pointer?
            return -1;
        }

        if (type.endsWith('&')) {
            type.truncate(type.length() - 1);
            int id = typeId(type);
            if (id == 0) {
                qWarning("Could not parse the method '%s'", sig.constData());
                // invalid type in method parameter list
                return -1;
            }
            
            metaTypes.append( id );

            if (metaTypes.last() == 0) {
                qWarning("Could not parse the method '%s'", sig.constData());
                // void?
                return -1;
            }

            continue;
        }

        if (seenMessage) {      // && !type.endsWith('&')
            qWarning("Could not parse the method '%s'", sig.constData());
            // non-output parameters after message or after output params
            return -1;          // not allowed
        }

        int id = typeId(type);
        if (id == 0) {
            qWarning("Could not parse the method '%s'", sig.constData());
            // invalid type in method parameter list
            return -1;
        }
        metaTypes.append(id);    
        ++inputCount;

        if (id == QDBusConnectionPrivate::messageMetaType)
            seenMessage = true;
    }

    return inputCount;
}

static int findSlot(const QMetaObject *mo, const QByteArray &name, int flags,
                    const QDBusTypeList &types, QList<int>& metaTypes, int &msgPos)
{
    // find the first slot
    const QMetaObject *super = mo;
    while (qstrcmp(super->className(), "QObject") != 0 &&
           qstrcmp(super->className(), "QDBusAbstractAdaptor") != 0)
        super = super->superClass();
    
    int attributeMask = (flags & QDBusConnection::ExportNonScriptableSlots) ?
                        0 : QMetaMethod::Scriptable;

    for (int idx = super->methodCount() ; idx <= mo->methodCount(); ++idx) {
        QMetaMethod mm = mo->method(idx);

        // check access:
        if (mm.access() != QMetaMethod::Public)
            continue;

        // check type:
        // unnecessary, since slots are never public:
        //if (mm.methodType() != QMetaMethod::Slot)
        //    continue;

        // check name:
        QByteArray sig = QMetaObject::normalizedSignature(mm.signature());
        int paren = sig.indexOf('(');
        if (paren != name.length() || !sig.startsWith( name ))
            continue;

        int returnType = returnTypeId(mm.typeName());
        bool isAsync = checkAsyncTag(mm.tag());

        // consistency check:
        if (isAsync && returnType != QMetaType::Void)
            continue;

        int inputCount = parametersForMethod(sig, metaTypes);
        if (inputCount == -1)
            continue;           // problem parsing

        metaTypes[0] = returnType;
        msgPos = 0;
        if (inputCount > 0 &&
            metaTypes.at(inputCount) == QDBusConnectionPrivate::messageMetaType) {
            // no input parameters is allowed as long as the message meta type is there
            msgPos = inputCount;
            --inputCount;
        }

        if (inputCount) {
            // try to match the parameters
            if (inputCount < types.count())
                continue;       // not enough parameters

            bool matches = true;
            int i;
            for (i = 0; i < types.count(); ++i)
                if ( !typesMatch(metaTypes.at(i + 1), types.at(i).qvariantType()) ) {
                    matches = false;
                    break;
                }

            if (!matches)
                continue;           // we didn't match them all

            // consistency check:
            if (isAsync && metaTypes.count() > i + 1)
                continue;
        }

        if (!msgPos && (mm.attributes() & attributeMask) != attributeMask)
            continue;           // not exported

        // if we got here, this slot matched
        return idx;
    }

    // no slot matched
    return -1;
}       

bool QDBusConnectionPrivate::activateSignal(const QDBusConnectionPrivate::SignalHook& hook,
                                            const QDBusMessage &msg)
{
    // This is called by QDBusConnectionPrivate::handleSignal to deliver a signal
    // that was received from D-Bus
    //
    // Signals are delivered to slots if the parameters match
    // Slots can have less parameters than there are on the message
    // Slots can optionally have one final parameter that is a QDBusMessage
    // Slots receive read-only copies of the message (i.e., pass by value or by const-ref)
    return activateReply(hook.obj, hook.midx, hook.params, msg);
}

bool QDBusConnectionPrivate::activateReply(QObject *object, int idx, const QList<int> &metaTypes,
                                           const QDBusMessage &msg)
{
    // This is called by qDBusResultReceived and is used to deliver the return value
    // of a remote function call.
    //
    // There is only one connection and it is specified by idx
    // The slot must have the same parameter types that the message does
    // The slot may have less parameters than the message
    // The slot may optionally have one final parameter that is QDBusMessage
    // The slot receives read-only copies of the message (i.e., pass by value or by const-ref)
    Q_ASSERT(object);

    int n = metaTypes.count() - 1;
    if (metaTypes[n] == QDBusConnectionPrivate::messageMetaType)
        --n;

    // check that types match
    for (int i = 0; i < n; ++i)
        if (!typesMatch(metaTypes.at(i + 1), msg.at(i).type()))
            return false;       // no match

    // we can deliver
    // prepare for the call
    CallDeliveryEvent *data = new CallDeliveryEvent;
    data->conn = this;
    data->object = object;
    data->flags = 0;
    data->message = msg;
    data->metaTypes = metaTypes;
    data->slotIdx = idx;

    QCoreApplication::postEvent( this, data );
    
    return true;
}

bool QDBusConnectionPrivate::activateCall(QObject* object, int flags,
                                          const QDBusMessage &msg)
{
    // This is called by QDBusConnectionPrivate::handleObjectCall to place a call
    // to a slot on the object.
    //
    // The call is delivered to the first slot that matches the following conditions:
    //  - has the same name as the message's target name
    //  - ALL of the message's types are found in slot's parameter list
    //  - optionally has one more parameter of type QDBusMessage
    // If none match, then the slot of the same name as the message target and with
    // the first type of QDBusMessage is delivered.
    //
    // Because the marshalling of D-Bus data into QVariant loses the information on
    // the original types, the message signature is used to determine the original type.
    // Aside from that, the "int" and "unsigned" types will be tried as well.
    //
    // The D-Bus specification requires that all MethodCall messages be replied to, unless the
    // caller specifically waived this requirement. This means that we inspect if the user slot
    // generated a reply and, if it didn't, we will. Obviously, if the user slot doesn't take a
    // QDBusMessage parameter, it cannot generate a reply.
    //
    // When a return message is generated, the slot's return type, if any, will be placed
    // in the message's first position. If there are non-const reference parameters to the
    // slot, they must appear at the end and will be placed in the subsequent message
    // positions.
    
    Q_ASSERT(object);

    QList<int> metaTypes;
    int idx;
    int msgPos;
    
    {
        const QMetaObject *mo = object->metaObject();
        QDBusTypeList typeList(msg.signature().toUtf8());

        // find a slot that matches according to the rules above
        idx = ::findSlot(mo, msg.name().toUtf8(), flags, typeList, metaTypes, msgPos);
        if (idx == -1)
            // no match
            return false;
    }

    // found the slot to be called
    // prepare for the call:
    CallDeliveryEvent *call = new CallDeliveryEvent;
    call->conn = this;

    // parameters:
    call->object = object;
    call->flags = flags;
    call->message = msg;

    // save our state:
    call->metaTypes = metaTypes;
    call->slotIdx = idx;

    QCoreApplication::postEvent( this, call );

    // ready
    return true;
}

void QDBusConnectionPrivate::deliverCall(const CallDeliveryEvent& data) const
{
    // resume state:
    const QList<int>& metaTypes = data.metaTypes;
    const QDBusMessage& msg = data.message;

    QVarLengthArray<void *, 10> params;
    params.reserve(metaTypes.count());

#if __BYTE_ORDER != __LITTLE_ENDIAN
    union integer
    {
        short s;
        unsigned short us;
        unsigned char uc;
    }
    QVarLengthArray<integer, 4> auxParameters;
#endif
    // let's create the parameter list

    // first one is the return type -- add it below
    params.append(0);

    // add the input parameters
    int i;
    for (i = 0; i < msg.count(); ++i) {
        int id = metaTypes[i + 1];
        if (id == QDBusConnectionPrivate::messageMetaType)
            break;

#if __BYTE_ORDER == __LITTLE_ENDIAN
        params.append(const_cast<void *>( msg.at(i).constData() ));
#else
        if (id == msg.at(i).type())
            params.append(const_cast<void *>( msg.at(i).constData() ));
        else {
            // need some help
            integer aux;
            const QVariant &var = msg.at(i);
            if (id == QMetaType::Short)
                aux.s = var.toInt();
            else if (id == QMetaType::UShort)
                aux.us = var.toUInt();
            else
                aux.uc = var.toUInt();
            auxParameters.append(aux);
            params.append( &auxParameters[auxParameters.count()] );
        }
#endif
    }

    bool takesMessage = false;
    if (metaTypes.count() > i + 1 && metaTypes[i + 1] == QDBusConnectionPrivate::messageMetaType) {
        params.append(const_cast<void*>(static_cast<const void*>(&msg)));
        takesMessage = true;
        ++i;
    }

    // output arguments
    QVariantList outputArgs;
    void *null = 0;
    if (metaTypes[0] != QMetaType::Void) {
        QVariant arg(metaTypes[0], null);
        params.append( arg.data() );
        outputArgs.append( arg );
    }
    for ( ; i < metaTypes.count(); ++i) {
        QVariant arg(metaTypes[i], null);
        params.append( arg.data() );
        outputArgs.append( arg );
    }

    // make call:
    bool fail;
    if (data.object.isNull())
        fail = true;
    else
        fail = data.object->qt_metacall(QMetaObject::InvokeMetaMethod,
                                        data.slotIdx, params.data()) >= 0;

    // do we create a reply? Only if the caller is waiting for a reply and one hasn't been sent
    // yet.
    if (!msg.noReply() && !msg.wasRepliedTo()) {
        if (!fail) {
            // normal reply
            QDBusMessage reply = QDBusMessage::methodReply(msg);
            reply += outputArgs;
                
            qDebug() << "Automatically sending reply:" << reply;
            send(reply);
        }
        else {
            // generate internal error
            QDBusMessage reply = QDBusMessage::error(msg, "com.trolltech.QtDBus.InternalError",
                                                     "Failed to deliver message");
            qDebug("Internal error: Failed to deliver message");
            send(reply);
        }
    }

    return;
}

void QDBusConnectionPrivate::customEvent(QEvent *event)
{
    // nothing else should be sending custom events at us
    CallDeliveryEvent* call = static_cast<CallDeliveryEvent *>(event);

    // self check:
    Q_ASSERT(call->conn == this);

    deliverCall(*call);
}

QDBusConnectionPrivate::QDBusConnectionPrivate(QObject *parent)
    : QObject(parent), ref(1), mode(InvalidMode), connection(0), server(0)
{
    extern bool qDBusInitThreads();
    static const int msgType = registerMessageMetaType();
    static const bool threads = qDBusInitThreads();

    Q_UNUSED(msgType);
    Q_UNUSED(threads);

    dbus_error_init(&error);
}

QDBusConnectionPrivate::~QDBusConnectionPrivate()
{
    Q_ASSERT(knownObjects.isEmpty());
    
    if (dbus_error_is_set(&error))
        dbus_error_free(&error);

    closeConnection();

    KnownInterfacesHash::iterator it = knownInterfaces.begin();
    while (it != knownInterfaces.end()) {
        const QSharedDataPointer<QDBusIntrospection::Interface>& item = *it;
        
        const_cast<QDBusIntrospection::Interface*>(item.constData())->ref.deref();

        it = knownInterfaces.erase(it);
    }
}

void QDBusConnectionPrivate::closeConnection()
{
    ConnectionMode oldMode = mode;
    mode = InvalidMode; // prevent reentrancy
    if (oldMode == ServerMode) {
        if (server) {
            dbus_server_disconnect(server);
            dbus_server_unref(server);
            server = 0;
        }
    } else if (oldMode == ClientMode) {
        if (connection) {
            dbus_connection_close(connection);
            // send the "close" message
            while (dbus_connection_dispatch(connection) == DBUS_DISPATCH_DATA_REMAINS)
                ;
            dbus_connection_unref(connection);
            connection = 0;
        }
    }
}

bool QDBusConnectionPrivate::handleError()
{
    lastError = QDBusError(&error);
    if (dbus_error_is_set(&error))
        dbus_error_free(&error);
    return lastError.isValid();
}

void QDBusConnectionPrivate::bindToApplication()
{
    // Yay, now that we have an application we are in business
    // Re-add all watchers
    WatcherHash oldWatchers = watchers;
    watchers.clear();
    QHashIterator<int, QDBusConnectionPrivate::Watcher> it(oldWatchers);
    while (it.hasNext()) {
        it.next();
        if (!it.value().read && !it.value().write) {
            qDBusAddWatch(it.value().watch, this);
        }
    }

    // Re-add all timeouts
    while (!pendingTimeouts.isEmpty())
       qDBusAddTimeout(pendingTimeouts.takeFirst(), this);
}

void QDBusConnectionPrivate::socketRead(int fd)
{
    QHashIterator<int, QDBusConnectionPrivate::Watcher> it(watchers);
    while (it.hasNext()) {
        it.next();
        if (it.key() == fd && it.value().read && it.value().read->isEnabled()) {
            if (!dbus_watch_handle(it.value().watch, DBUS_WATCH_READABLE))
                qDebug("OUT OF MEM");
        }
    }
    if (mode == ClientMode)
        while (dbus_connection_dispatch(connection) == DBUS_DISPATCH_DATA_REMAINS);
}

void QDBusConnectionPrivate::socketWrite(int fd)
{
    QHashIterator<int, QDBusConnectionPrivate::Watcher> it(watchers);
    while (it.hasNext()) {
        it.next();
        if (it.key() == fd && it.value().write && it.value().write->isEnabled()) {
            if (!dbus_watch_handle(it.value().watch, DBUS_WATCH_WRITABLE))
                qDebug("OUT OF MEM");
        }
    }
}

void QDBusConnectionPrivate::objectDestroyed(QObject *obj)
{
    ObjectHookHash::iterator it = objectHooks.begin();
    while (it != objectHooks.end()) {
        ObjectDataHash::iterator dit = it->begin();
        while (dit != it->end()) {
            if (static_cast<QObject *>(dit.value().obj) == obj)
                dit = it->erase(dit);
            else
                ++dit;
        }

        if (it->isEmpty())
            it = objectHooks.erase(it);
        else
            ++it;
    }

    SignalHookHash::iterator sit = signalHooks.begin();
    while (sit != signalHooks.end()) {
        if (static_cast<QObject *>(sit.value().obj) == obj)
            sit = signalHooks.erase(sit);
        else
            ++sit;
    }
    obj->disconnect(this);
}

bool QDBusConnectionPrivate::activateAdaptor(QObject* object, int flags,
                                             const QDBusMessage &msg)
{
    // This is called by QDBusConnectionPrivate::handleObjectCall to place a call to a slot
    // on the object.
    //
    // The call is routed through the adaptor sub-objects

    Q_ASSERT(object);
    flags |= QDBusConnection::ExportNonScriptableSlots;

    const QObjectList& children = object->children();
    QObjectList::const_iterator child = children.begin(),
                               end = children.end();
    for ( ; child != end; ++child) {
        // check if this is an adaptor
        if (!qobject_cast<QDBusAbstractAdaptor *>(*child))
            continue;           // not adaptor

        const QMetaObject *mo = (*child)->metaObject();
        int ciend = mo->classInfoCount();
        for (int i = 0; i < ciend; ++i) {
            QMetaClassInfo mci = mo->classInfo(i);
            if (strcmp(mci.name(), "DBus Interface") == 0 && *mci.value()) {
                // one interface.
                // is this it?
                if (msg.interface().isEmpty() || msg.interface() == mci.value())
                    return activateCall(*child, flags, msg);
            }
        }
    }
    return false;
}

int QDBusConnectionPrivate::registerMessageMetaType()
{
    int tp = messageMetaType = qRegisterMetaType<QDBusMessage>("QDBusMessage");
    return tp;
}

int QDBusConnectionPrivate::findSlot(QObject* obj, const char *slotName, QList<int>& params)
{
    Q_ASSERT(slotName);
    QByteArray normalizedName = QMetaObject::normalizedSignature(slotName);
    int midx = obj->metaObject()->indexOfMethod(normalizedName);
    if (midx == -1) {
        qWarning("No such slot '%s' while connecting D-Bus", slotName);
        return -1;
    }

    int inputCount = parametersForMethod(normalizedName, params);
    if ( inputCount == -1 || inputCount + 1 != params.count() )
        return -1;              // failed to parse or invalid arguments or output arguments
    
    return midx;
}

bool QDBusConnectionPrivate::activateObject(const QDBusConnectionPrivate::ObjectData &hook,
                                            const QDBusMessage &msg)
{
    if (!hook.obj)
        return false;           // object is gone

    if (hook.flags & QDBusConnection::ExportAdaptors)
        return activateAdaptor(hook.obj, hook.flags, msg);
    else
        return activateCall(hook.obj, hook.flags, msg);
}

bool QDBusConnectionPrivate::handleObjectCall(const QDBusMessage &msg)
{
    ObjectHookHash::ConstIterator it = objectHooks.find(msg.path());
    if (it == objectHooks.constEnd())
        return false;
    
    bool ok = false;
    const ObjectDataHash& hook = it.value();
    ObjectDataHash::ConstIterator hit;
    if (msg.interface().isEmpty()) {
        // we must go through all the objects and interfaces
        
        for (hit = hook.begin(); hit != hook.end(); ++hit) {
            ok = activateObject(hit.value(), msg);
            if (ok)
                break;          // processed
        }
    } else {
        // find the interface:
        hit = hook.find(msg.interface());
        if (hit != hook.end())
            ok = activateObject(hit.value(), msg);

        if (!ok) {
            // try adaptors (or any interface)
            hit = hook.find(QString());
            if (hit != hook.end())
                ok = activateObject(hit.value(), msg);
        }
    }

    qDebug(ok ? "Call scheduled" : "Call failed");
    return ok;
}

bool QDBusConnectionPrivate::handleSignal(const QString &path, const QDBusMessage &msg)
{
    SignalHookHash::const_iterator it = signalHooks.find(path);
    qDebug("looking for: %s", path.toLocal8Bit().constData());
    qDebug() << signalHooks.keys();
    for ( ; it != signalHooks.constEnd() && it.key() == path; ++ it) {
        const SignalHook &hook = it.value();
        if ( hook.obj.isNull() )
            continue;
        if ( !hook.name.isEmpty() && hook.name != msg.name() )
            continue;
        if ( !hook.interface.isEmpty() && hook.interface != msg.interface() )
            continue;
        if ( !hook.signature.isEmpty() && hook.signature != msg.signature() )
            continue;

        activateSignal(hook, msg);
    }
    return true;
}

bool QDBusConnectionPrivate::handleSignal(const QDBusMessage& msg)
{
    // yes, it is a single "|" below...
    return handleSignal(QString(), msg) | handleSignal(msg.sender() + msg.path(), msg);
}

static dbus_int32_t server_slot = -1;

void QDBusConnectionPrivate::setServer(DBusServer *s)
{
    if (!server) {
        handleError();
        return;
    }

    server = s;
    mode = ServerMode;

    dbus_server_allocate_data_slot(&server_slot);
    if (server_slot < 0)
        return;

    dbus_server_set_watch_functions(server, qDBusAddWatch, qDBusRemoveWatch,
                                    qDBusToggleWatch, this, 0); // ### check return type?
    dbus_server_set_timeout_functions(server, qDBusAddTimeout, qDBusRemoveTimeout,
                                      qDBusToggleTimeout, this, 0);
    dbus_server_set_new_connection_function(server, qDBusNewConnection, this, 0);

    dbus_server_set_data(server, server_slot, this, 0);
}

void QDBusConnectionPrivate::setConnection(DBusConnection *dbc)
{
    if (!dbc) {
        handleError();
        return;
    }

    connection = dbc;
    mode = ClientMode;

    dbus_connection_set_exit_on_disconnect(connection, false);
    dbus_connection_set_watch_functions(connection, qDBusAddWatch, qDBusRemoveWatch,
                                        qDBusToggleWatch, this, 0);
    dbus_connection_set_timeout_functions(connection, qDBusAddTimeout, qDBusRemoveTimeout,
                                          qDBusToggleTimeout, this, 0);
//    dbus_bus_add_match(connection, "type='signal',interface='com.trolltech.dbus.Signal'", &error);
//    dbus_bus_add_match(connection, "type='signal'", &error);

    dbus_bus_add_match(connection, "type='signal'", &error);
    if (handleError()) {
        closeConnection();
        return;
    }

    const char *service = dbus_bus_get_unique_name(connection);
    if (service) {
        QVarLengthArray<char, 56> filter;
        filter.append("destination='", 13);
        filter.append(service, qstrlen(service));
        filter.append("\'\0", 2);

        dbus_bus_add_match(connection, filter.constData(), &error);
        if (handleError()) {
            closeConnection();
            return;
        }
    } else {
        qWarning("QDBusConnectionPrivate::SetConnection: Unable to get base service");
    }

    dbus_connection_add_filter(connection, qDBusSignalFilter, this, 0);

    qDebug("base service: %s", service);
}

static void qDBusResultReceived(DBusPendingCall *pending, void *user_data)
{
    QDBusPendingCall *call = reinterpret_cast<QDBusPendingCall *>(user_data);
    QDBusConnectionPrivate *connection = const_cast<QDBusConnectionPrivate *>(call->connection);
    Q_ASSERT(call->pending == pending);

    if (!call->receiver.isNull() && call->methodIdx != -1) {
        DBusMessage *reply = dbus_pending_call_steal_reply(pending);
        connection->activateReply(call->receiver, call->methodIdx, call->metaTypes,
                                  QDBusMessage::fromDBusMessage(reply));
    }
    dbus_pending_call_unref(pending);
    delete call;
}

bool QDBusConnectionPrivate::send(const QDBusMessage& message) const
{
    DBusMessage *msg = message.toDBusMessage();
    if (!msg)
        return false;

    dbus_message_set_no_reply(msg, true); // the reply would not be delivered to anything

    qDebug() << "sending message:" << message;
    bool isOk = dbus_connection_send(connection, msg, 0);
    dbus_message_unref(msg);
    return isOk;
}

int QDBusConnectionPrivate::sendWithReplyAsync(const QDBusMessage &message, QObject *receiver,
                                               const char *method) const
{
    DBusMessage *msg = message.toDBusMessage();
    if (!msg)
        return 0;

    int slotIdx = -1;
    QList<int> metaTypes;
    if (receiver && method && *method)
        slotIdx = findSlot(receiver, method + 1, metaTypes);

    qDebug() << "sending message:" << message;
    DBusPendingCall *pending = 0;
    if (dbus_connection_send_with_reply(connection, msg, &pending, message.timeout())) {
        if (slotIdx != -1) {
            QDBusPendingCall *pcall = new QDBusPendingCall;
            pcall->receiver = receiver;
            pcall->metaTypes = metaTypes;
            pcall->methodIdx = slotIdx;
            pcall->connection = this;
            pcall->pending = dbus_pending_call_ref(pending);
            dbus_pending_call_set_notify(pending, qDBusResultReceived, pcall, 0);
        }
        dbus_pending_call_unref(pending);
        return dbus_message_get_serial(msg);
    }

    return 0;
}

QSharedDataPointer<QDBusIntrospection::Interface>
QDBusConnectionPrivate::findInterface(const QString& name)
{
    QMutexLocker locker(&mutex);
    QSharedDataPointer<QDBusIntrospection::Interface> data = knownInterfaces.value(name);
    if (!data) {
        data = new QDBusIntrospection::Interface;
        data->name = name;
        data->ref.ref();          // don't delete

        knownInterfaces.insert(name, data);
    }
    return data;
}

QDBusIntrospection::Object*
QDBusConnectionPrivate::findObject(const QString& service, const QString& path)
{
    QMutexLocker locker(&mutex);
    QDBusIntrospection::Object* data = knownObjects.value(service + path);
    if (!data) {
        data = new QDBusIntrospection::Object;
        data->service = service;
        data->path = path;

        knownObjects.insert(service + path, data);
    }
    
    return data;
}

void QDBusConnectionPrivate::disposeOfLocked(QDBusIntrospection::Object* p)
{
    if (p && !p->ref.deref()) { // ref--
        // no one else is using it
        // get rid of the reference
        QString objName = p->service + p->path;
        
#ifndef QT_NO_DEBUG
        // debug code
        Q_ASSERT(p == knownObjects.take(objName));
#else
        // non-debug
        knownObjects.remove(objName);
#endif

        // remove sub-objects too
        if (!objName.endsWith('/'))
            objName.append('/');
        foreach (QString subObjName, p->childObjects)
            disposeOfLocked(knownObjects.value(objName + subObjName));
        
        delete p;
    }
}

void QDBusConnectionPrivate::disposeOf(QDBusObjectPrivate* p)
{
    // We're called from QDBusConnectionPrivate's destructor
    // that means the object it represents is going out of scope

    QMutexLocker locker(&mutex);
    disposeOfLocked( const_cast<QDBusIntrospection::Object*>(p->data) );
}

void QDBusReplyWaiter::reply(const QDBusMessage &msg)
{
    replyMsg = msg;
    QTimer::singleShot(0, this, SLOT(quit()));
}
