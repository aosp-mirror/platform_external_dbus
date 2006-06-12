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
#include <qobject.h>
#include <qsocketnotifier.h>
#include <qstringlist.h>
#include <qtimer.h>

#include "qdbusconnection_p.h"
#include "qdbusinterface_p.h"
#include "qdbusmessage.h"
#include "qdbusabstractadaptor.h"
#include "qdbusabstractadaptor_p.h"
#include "qdbustypehelper_p.h"
#include "qdbusutil.h"
#include "qdbustype_p.h"

#ifndef USE_OUTSIDE_DISPATCH
# define USE_OUTSIDE_DISPATCH    0
#endif

int QDBusConnectionPrivate::messageMetaType = 0;

typedef void (*QDBusSpyHook)(const QDBusMessage&);
typedef QVarLengthArray<QDBusSpyHook, 4> QDBusSpyHookList;
Q_GLOBAL_STATIC(QDBusSpyHookList, qDBusSpyHookList)

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

    //qDebug("ToggleTimeout");

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
        //qDebug("addReadWatch %d", fd);
        watcher.watch = watch;
        if (QCoreApplication::instance()) {
            watcher.read = new QSocketNotifier(fd, QSocketNotifier::Read, d);
            watcher.read->setEnabled(dbus_watch_get_enabled(watch));
            d->connect(watcher.read, SIGNAL(activated(int)), SLOT(socketRead(int)));
        }
    }
    if (flags & DBUS_WATCH_WRITABLE) {
        //qDebug("addWriteWatch %d", fd);
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

    //qDebug("remove watch");

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

            //qDebug("toggle watch %d to %d (write: %d, read: %d)", dbus_watch_get_fd(watch), enabled, flags & DBUS_WATCH_WRITABLE, flags & DBUS_WATCH_READABLE);

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
    Q_UNUSED(data); Q_UNUSED(server); Q_UNUSED(c);

    qDebug("SERVER: GOT A NEW CONNECTION"); // TODO
}

extern QDBUS_EXPORT void qDBusAddSpyHook(QDBusSpyHook);
void qDBusAddSpyHook(QDBusSpyHook hook)
{
    qDBusSpyHookList()->append(hook);
}

#if USE_OUTSIDE_DISPATCH
# define HANDLED     DBUS_HANDLER_RESULT_HANDLED_OUTSIDE_DISPATCH
static DBusHandlerResult qDBusSignalFilterOutside(DBusConnection *connection,
                                                  DBusMessage *message, void *data)
{
    Q_ASSERT(data);
    Q_UNUSED(connection);
    Q_UNUSED(message);

    QDBusConnectionPrivate *d = static_cast<QDBusConnectionPrivate *>(data);
    if (d->mode == QDBusConnectionPrivate::InvalidMode)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED; // internal error, actually

    CallDeliveryEvent *e = d->postedCallDeliveryEvent();

    d->deliverCall(*e);
    delete e;

    return DBUS_HANDLER_RESULT_HANDLED;
}
#else
# define HANDLED     DBUS_HANDLER_RESULT_HANDLED
#endif

extern "C" {
static DBusHandlerResult
qDBusSignalFilter(DBusConnection *connection, DBusMessage *message, void *data)
{
    return QDBusConnectionPrivate::messageFilter(connection, message, data);
}
}

DBusHandlerResult QDBusConnectionPrivate::messageFilter(DBusConnection *connection,
                                                        DBusMessage *message, void *data)
{
    Q_ASSERT(data);
    Q_UNUSED(connection);

    QDBusConnectionPrivate *d = static_cast<QDBusConnectionPrivate *>(data);
    if (d->mode == QDBusConnectionPrivate::InvalidMode)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    QDBusMessage amsg = QDBusMessage::fromDBusMessage(message, QDBusConnection(d->name));
    qDebug() << "got message:" << amsg;

    const QDBusSpyHookList *list = qDBusSpyHookList();
    for (int i = 0; i < list->size(); ++i) {
        qDebug() << "calling the message spy hook";
        (*(*list)[i])(amsg);
    }

    bool handled = false;
    int msgType = dbus_message_get_type(message);
    if (msgType == DBUS_MESSAGE_TYPE_SIGNAL) {
        handled = d->handleSignal(amsg);
    } else if (msgType == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        handled = d->handleObjectCall(amsg);
    }

    return handled ? HANDLED :
        DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void huntAndDestroy(QObject *needle, QDBusConnectionPrivate::ObjectTreeNode *haystack)
{
    foreach (const QDBusConnectionPrivate::ObjectTreeNode::Data &entry, haystack->children)
        huntAndDestroy(needle, entry.node);

    if (needle == haystack->obj) {
        haystack->obj = 0;
        haystack->flags = 0;
    }
}

static void huntAndEmit(DBusConnection *connection, DBusMessage *msg,
                        QObject *needle, QDBusConnectionPrivate::ObjectTreeNode *haystack,
                        const QString &path = QString())
{
    foreach (const QDBusConnectionPrivate::ObjectTreeNode::Data &entry, haystack->children)
        huntAndEmit(connection, msg, needle, entry.node, path + QLatin1String("/") + entry.name);

    if (needle == haystack->obj && haystack->flags & QDBusConnection::ExportAdaptors) {
        QByteArray p = path.toLatin1();
        if (p.isEmpty())
            p = "/";
        //qDebug() << p;
        DBusMessage *msg2 = dbus_message_copy(msg);
        dbus_message_set_path(msg2, p);
        dbus_connection_send(connection, msg2, 0);
        dbus_message_unref(msg2);
    }
}

static bool typesMatch(int metaId, int variantType)
{
    if (metaId == int(variantType))
        return true;

    if (variantType == QVariant::Int && metaId == QMetaType::Short)
        return true;

    if (variantType == QVariant::UInt && (metaId == QMetaType::UShort ||
                                          metaId == QMetaType::UChar))
        return true;

    if (variantType == QVariant::List) {
        if (metaId == QDBusTypeHelper<bool>::listId() ||
            metaId == QDBusTypeHelper<short>::listId() ||
            metaId == QDBusTypeHelper<ushort>::listId() || 
            metaId == QDBusTypeHelper<int>::listId() ||
            metaId == QDBusTypeHelper<uint>::listId() ||
            metaId == QDBusTypeHelper<qlonglong>::listId() ||
            metaId == QDBusTypeHelper<qulonglong>::listId() ||
            metaId == QDBusTypeHelper<double>::listId())
            return true;
    }

    return false;               // no match
}

static int findSlot(const QMetaObject *mo, const QByteArray &name, int flags,
                    const QDBusTypeList &types, QList<int>& metaTypes)
{
    // find the first slot
    const QMetaObject *super = mo;
    while (super != &QObject::staticMetaObject &&
           super != &QDBusAbstractAdaptor::staticMetaObject)
        super = super->superClass();

    int attributeMask = (flags & QDBusConnection::ExportAllSlots) ?
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

        int returnType = qDBusNameToTypeId(mm.typeName());
        bool isAsync = qDBusCheckAsyncTag(mm.tag());

        // consistency check:
        if (isAsync && returnType != QMetaType::Void)
            continue;

        int inputCount = qDBusParametersForMethod(mm, metaTypes);
        if (inputCount == -1)
            continue;           // problem parsing

        metaTypes[0] = returnType;
        bool hasMessage = false;
        if (inputCount > 0 &&
            metaTypes.at(inputCount) == QDBusConnectionPrivate::messageMetaType) {
            // "no input parameters" is allowed as long as the message meta type is there
            hasMessage = true;
            --inputCount;
        }

        // try to match the parameters
        if (inputCount != types.count())
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
        if (isAsync && metaTypes.count() > i + 1 + (hasMessage ? 1 : 0))
            continue;

        if (hasMessage && (mm.attributes() & attributeMask) != attributeMask)
            continue;           // not exported

        // if we got here, this slot matched
        return idx;
    }

    // no slot matched
    return -1;
}

static CallDeliveryEvent* prepareReply(QObject *object, int idx, const QList<int> &metaTypes,
                                       const QDBusMessage &msg)
{
    Q_ASSERT(object);

    int n = metaTypes.count() - 1;
    if (metaTypes[n] == QDBusConnectionPrivate::messageMetaType)
        --n;

    // check that types match
    for (int i = 0; i < n; ++i)
        if (!typesMatch(metaTypes.at(i + 1), msg.at(i).type()))
            return 0;           // no match

    // we can deliver
    // prepare for the call
    CallDeliveryEvent *data = new CallDeliveryEvent;
    data->object = object;
    data->flags = 0;
    data->message = msg;
    data->metaTypes = metaTypes;
    data->slotIdx = idx;

    return data;
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
    CallDeliveryEvent *call = prepareReply(hook.obj, hook.midx, hook.params, msg);
    if (call) {
        postCallDeliveryEvent(call);
        return true;
    }
    return false;
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

    if (!object)
        return false;

    QList<int> metaTypes;
    int idx;

    {
        const QMetaObject *mo = object->metaObject();
        QDBusTypeList typeList(msg.signature().toUtf8());
        QByteArray memberName = msg.name().toUtf8();

        // find a slot that matches according to the rules above
        idx = ::findSlot(mo, memberName, flags, typeList, metaTypes);
        if (idx == -1) {
            // try with no parameters, but with a QDBusMessage
            idx = ::findSlot(mo, memberName, flags, QDBusTypeList(), metaTypes);
            if (metaTypes.count() != 2 || metaTypes.at(1) != messageMetaType)
                return false;
        }
    }

    // found the slot to be called
    // prepare for the call:
    CallDeliveryEvent *call = new CallDeliveryEvent;

    // parameters:
    call->object = object;
    call->flags = flags;
    call->message = msg;

    // save our state:
    call->metaTypes = metaTypes;
    call->slotIdx = idx;

    postCallDeliveryEvent(call);

    // ready
    return true;
}

void QDBusConnectionPrivate::postCallDeliveryEvent(CallDeliveryEvent *data)
{
    Q_ASSERT(data);
    data->conn = this;    
#if USE_OUTSIDE_DISPATCH
    callDeliveryMutex.lock();
    callDeliveryState = data;
#else
    QCoreApplication::postEvent( this, data );
#endif
}

CallDeliveryEvent *QDBusConnectionPrivate::postedCallDeliveryEvent()
{
    CallDeliveryEvent *e = callDeliveryState;
    Q_ASSERT(e && e->conn == this);

    // release it:
    callDeliveryState = 0;
    callDeliveryMutex.unlock();

    return e;
}

void QDBusConnectionPrivate::deliverCall(const CallDeliveryEvent& data) const
{
    // resume state:
    const QList<int>& metaTypes = data.metaTypes;
    const QDBusMessage& msg = data.message;

    QVarLengthArray<void *, 10> params;
    params.reserve(metaTypes.count());

    QVariantList auxParameters;
    // let's create the parameter list

    // first one is the return type -- add it below
    params.append(0);

    // add the input parameters
    int i;
    for (i = 1; i <= msg.count(); ++i) {
        int id = metaTypes[i];
        if (id == QDBusConnectionPrivate::messageMetaType)
            break;

        if (id == int(msg.at(i - 1).userType()))
            // no conversion needed
            params.append(const_cast<void *>( msg.at(i - 1).constData() ));
        else {
            // convert to what the function expects
            auxParameters.append(QVariant());
            
            const QVariant &in = msg.at(i - 1);
            QVariant &out = auxParameters[auxParameters.count() - 1];

            bool isOk = true;
            if (id == QVariant::List) {
                int mid = in.userType();
                // the only conversion possible here is from a specialised QList<T> to QVariantList
                if (mid == QDBusTypeHelper<bool>::listId())
                    out = qVariantFromValue(QDBusTypeHelper<bool>::toVariantList(in));
                else if (mid == QDBusTypeHelper<short>::listId())
                    out = qVariantFromValue(QDBusTypeHelper<short>::toVariantList(in));
                else if (mid == QDBusTypeHelper<ushort>::listId())
                    out = qVariantFromValue(QDBusTypeHelper<ushort>::toVariantList(in));
                else if (mid == QDBusTypeHelper<int>::listId())
                    out = qVariantFromValue(QDBusTypeHelper<int>::toVariantList(in));
                else if (mid == QDBusTypeHelper<uint>::listId())
                    out = qVariantFromValue(QDBusTypeHelper<uint>::toVariantList(in));
                else if (mid == QDBusTypeHelper<qlonglong>::listId())
                    out = qVariantFromValue(QDBusTypeHelper<qlonglong>::toVariantList(in));
                else if (mid == QDBusTypeHelper<qulonglong>::listId())
                    out = qVariantFromValue(QDBusTypeHelper<qulonglong>::toVariantList(in));
                else if (mid == QDBusTypeHelper<double>::listId())
                    out = qVariantFromValue(QDBusTypeHelper<double>::toVariantList(in));
                else
                    isOk = false;
            } else if (in.type() == QVariant::UInt) {
                if (id == QMetaType::UChar) {
                    uchar uc = in.toUInt();
                    out = qVariantFromValue(uc);
                } else if (id == QMetaType::UShort) {
                    ushort us = in.toUInt();
                    out = qVariantFromValue(us);
                } else {
                    isOk = false;
                }
            } else if (in.type() == QVariant::Int) {
                if (id == QMetaType::Short) {
                    short s = in.toInt();
                    out = qVariantFromValue(s);
                } else {
                    isOk = false;
                }
            } else {
                isOk = false;
            }

            if (!isOk)
                qFatal("Internal error: got invalid meta type %d when trying to convert to meta type %d",
                       in.userType(), id);

            params.append( const_cast<void *>(out.constData()) );
        }
    }

    bool takesMessage = false;
    if (metaTypes.count() > i && metaTypes[i] == QDBusConnectionPrivate::messageMetaType) {
        params.append(const_cast<void*>(static_cast<const void*>(&msg)));
        takesMessage = true;
        ++i;
    }

    // output arguments
    QVariantList outputArgs;
    void *null = 0;
    if (metaTypes[0] != QMetaType::Void) {
        QVariant arg(metaTypes[0], null);
        outputArgs.append( arg );
        params[0] = const_cast<void*>(outputArgs.at( outputArgs.count() - 1 ).constData());
    }
    for ( ; i < metaTypes.count(); ++i) {
        QVariant arg(metaTypes[i], null);
        outputArgs.append( arg );
        params.append( const_cast<void*>(outputArgs.at( outputArgs.count() - 1 ).constData()) );
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
            QDBusMessage reply = QDBusMessage::error(msg, QDBusError(QDBusError::InternalError,
                    QLatin1String("Failed to deliver message")));
            qWarning("Internal error: Failed to deliver message");
            send(reply);
        }
    }

    return;
}

void QDBusConnectionPrivate::customEvent(QEvent *e)
{
    // nothing else should be sending custom events at us
    CallDeliveryEvent* call = static_cast<CallDeliveryEvent *>(e);

    // self check:
    Q_ASSERT(call->conn == this);

    deliverCall(*call);
}

QDBusConnectionPrivate::QDBusConnectionPrivate(QObject *p)
    : QObject(p), ref(1), mode(InvalidMode), connection(0), server(0), busService(0)
{
    extern bool qDBusInitThreads();
    static const int msgType = registerMessageMetaType();
    static const bool threads = qDBusInitThreads();
    static const bool metatypes = QDBusMetaTypeId::innerInitialize();

    Q_UNUSED(msgType);
    Q_UNUSED(threads);
    Q_UNUSED(metatypes);

    dbus_error_init(&error);

    rootNode.flags = 0;
}

QDBusConnectionPrivate::~QDBusConnectionPrivate()
{
    if (dbus_error_is_set(&error))
        dbus_error_free(&error);

    closeConnection();
    rootNode.clear();        // free resources
    qDeleteAll(cachedMetaObjects);
}

void QDBusConnectionPrivate::closeConnection()
{
    QWriteLocker locker(&lock);
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
    Q_ASSERT_X(QCoreApplication::instance(), "QDBusConnection",
               "qDBusBindToApplication called without an application");
    moveToThread(QCoreApplication::instance()->thread());
    
    // Re-add all watchers
    WatcherHash oldWatchers = watchers;
    watchers.clear();
    QHashIterator<int, QDBusConnectionPrivate::Watcher> it(oldWatchers);
    while (it.hasNext()) {
        it.next();
        if (!it.value().read && !it.value().write) {
            qDBusAddWatch(it.value().watch, this);
        } else {
            watchers.insertMulti(it.key(), it.value());
        }
    }

    // Re-add all timeouts
    while (!pendingTimeouts.isEmpty())
       qDBusAddTimeout(pendingTimeouts.takeFirst(), this);
}

void QDBusConnectionPrivate::timerEvent(QTimerEvent *e)
{
    DBusTimeout *timeout = timeouts.value(e->timerId(), 0);
    dbus_timeout_handle(timeout);
}

void QDBusConnectionPrivate::doDispatch()
{
    if (mode == ClientMode)
        while (dbus_connection_dispatch(connection) == DBUS_DISPATCH_DATA_REMAINS);
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

    doDispatch();
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
    QWriteLocker locker(&lock);
    huntAndDestroy(obj, &rootNode);

    SignalHookHash::iterator sit = signalHooks.begin();
    while (sit != signalHooks.end()) {
        if (static_cast<QObject *>(sit.value().obj) == obj)
            sit = signalHooks.erase(sit);
        else
            ++sit;
    }

    obj->disconnect(this);
}

void QDBusConnectionPrivate::relaySignal(QObject *obj, const char *interface, const char *memberName,
                                         const QVariantList &args)
{
    QReadLocker locker(&lock);
    QDBusMessage message = QDBusMessage::signal(QLatin1String("/"), QLatin1String(interface),
                                                QLatin1String(memberName));
    message += args;
    DBusMessage *msg = message.toDBusMessage();
    if (!msg) {
        qWarning("Could not emit signal %s.%s", interface, memberName);
        return;
    }

    //qDebug() << "Emitting signal" << message;
    //qDebug() << "for paths:";
    dbus_message_set_no_reply(msg, true); // the reply would not be delivered to anything
    huntAndEmit(connection, msg, obj, &rootNode);
    dbus_message_unref(msg);
}

int QDBusConnectionPrivate::registerMessageMetaType()
{
    int tp = messageMetaType = qRegisterMetaType<QDBusMessage>("QDBusMessage");
    return tp;
}

int QDBusConnectionPrivate::findSlot(QObject* obj, const QByteArray &normalizedName,
                                     QList<int> &params)
{
    int midx = obj->metaObject()->indexOfMethod(normalizedName);
    if (midx == -1) {
        qWarning("No such slot '%s' while connecting D-Bus", normalizedName.constData());
        return -1;
    }

    int inputCount = qDBusParametersForMethod(obj->metaObject()->method(midx), params);
    if ( inputCount == -1 || inputCount + 1 != params.count() )
        return -1;              // failed to parse or invalid arguments or output arguments

    return midx;
}

bool QDBusConnectionPrivate::prepareHook(QDBusConnectionPrivate::SignalHook &hook, QString &key,
                                         const QString &service, const QString &path,
                                         const QString &interface, const QString &name,
                                         QObject *receiver, const char *signal, int minMIdx,
                                         bool buildSignature)
{
    QByteArray normalizedName = QMetaObject::normalizedSignature(signal + 1);
    hook.midx = findSlot(receiver, normalizedName, hook.params);
    if (hook.midx < minMIdx)
        return false;

    hook.sender = service;
    hook.path = path;
    hook.obj = receiver;

    // build the D-Bus signal name and signature
    QString mname = name;
    if (mname.isEmpty()) {
        normalizedName.truncate(normalizedName.indexOf('('));        
        mname = QString::fromUtf8(normalizedName);
    }
    key = mname;
    key.reserve(interface.length() + 1 + mname.length());
    key += QLatin1Char(':');
    key += interface;

    if (buildSignature) {
        hook.signature.clear();
        for (int i = 1; i < hook.params.count(); ++i)
            if (hook.params.at(i) != messageMetaType)
                hook.signature += QLatin1String( QDBusType::dbusSignature( QVariant::Type(hook.params.at(i)) ) );
    }
    
    return true;                // connect to this signal
}

bool QDBusConnectionPrivate::activateInternalFilters(const ObjectTreeNode *node, const QDBusMessage &msg)
{
    // object may be null

    if (msg.interface().isEmpty() || msg.interface() == QLatin1String(DBUS_INTERFACE_INTROSPECTABLE)) {
        if (msg.method() == QLatin1String("Introspect") && msg.signature().isEmpty())
            qDBusIntrospectObject(node, msg);
        if (msg.interface() == QLatin1String(DBUS_INTERFACE_INTROSPECTABLE))
            return true;
    }

    if (node->obj && (msg.interface().isEmpty() ||
                      msg.interface() == QLatin1String(DBUS_INTERFACE_PROPERTIES))) {
        if (msg.method() == QLatin1String("Get") && msg.signature() == QLatin1String("ss"))
            qDBusPropertyGet(node, msg);
        else if (msg.method() == QLatin1String("Set") && msg.signature() == QLatin1String("ssv"))
            qDBusPropertySet(node, msg);

        if (msg.interface() == QLatin1String(DBUS_INTERFACE_PROPERTIES))
            return true;
    }

    return false;
}

bool QDBusConnectionPrivate::activateObject(const ObjectTreeNode *node, const QDBusMessage &msg)
{
    // This is called by QDBusConnectionPrivate::handleObjectCall to place a call to a slot
    // on the object.
    //
    // The call is routed through the adaptor sub-objects if we have any

    // object may be null

    QDBusAdaptorConnector *connector;
    if (node->flags & QDBusConnection::ExportAdaptors &&
        (connector = qDBusFindAdaptorConnector(node->obj))) {
        int newflags = node->flags | QDBusConnection::ExportAllSlots;

        if (msg.interface().isEmpty()) {
            // place the call in all interfaces
            // let the first one that handles it to work
            foreach (const QDBusAdaptorConnector::AdaptorData &entry, connector->adaptors)
                if (activateCall(entry.adaptor, newflags, msg))
                    return true;
        } else {
            // check if we have an interface matching the name that was asked:
            QDBusAdaptorConnector::AdaptorMap::ConstIterator it;
            it = qLowerBound(connector->adaptors.constBegin(), connector->adaptors.constEnd(),
                             msg.interface());
            if (it != connector->adaptors.constEnd() && it->interface == msg.interface())
                if (activateCall(it->adaptor, newflags, msg))
                return true;
        }
    }

    // no adaptors matched
    // try our standard filters
    if (activateInternalFilters(node, msg))
        return true;

    // try the object itself:
    if (node->flags & QDBusConnection::ExportSlots && activateCall(node->obj, node->flags, msg))
        return true;
#if 0
    // nothing matched
    qDebug("Call failed: no match for %s%s%s at %s",
           qPrintable(msg.interface()), msg.interface().isEmpty() ? "" : ".",
           qPrintable(msg.name()),
           qPrintable(msg.path()));
#endif
    return false;
}

template<typename Func>
static bool applyForObject(QDBusConnectionPrivate::ObjectTreeNode *root, const QString &fullpath,
                           Func& functor)
{
    // walk the object tree
    QStringList path = fullpath.split(QLatin1Char('/'));
    if (path.last().isEmpty())
        path.removeLast();      // happens if path is "/"
    int i = 1;
    QDBusConnectionPrivate::ObjectTreeNode *node = root;

    // try our own tree first
    while (node && !(node->flags & QDBusConnection::ExportChildObjects) ) {
        if (i == path.count()) {
            // found our object
            functor(node);
            return true;
        }

        QVector<QDBusConnectionPrivate::ObjectTreeNode::Data>::ConstIterator it =
            qLowerBound(node->children.constBegin(), node->children.constEnd(), path.at(i));
        if (it != node->children.constEnd() && it->name == path.at(i))
            // match
            node = it->node;
        else
            node = 0;

        ++i;
    }

    // any object in the tree can tell us to switch to its own object tree:
    if (node && node->flags & QDBusConnection::ExportChildObjects) {
        QObject *obj = node->obj;

        while (obj) {
            if (i == path.count()) {
                // we're at the correct level
                QDBusConnectionPrivate::ObjectTreeNode fakenode(*node);
                fakenode.obj = obj;
                functor(&fakenode);
                return true;
            }

            const QObjectList children = obj->children();

            // find a child with the proper name
            QObject *next = 0;
            foreach (QObject *child, children)
                if (child->objectName() == path.at(i)) {
                    next = child;
                    break;
                }

            if (!next)
                break;

            ++i;
            obj = next;
        }
    }

    // object not found
    return false;
}

struct qdbus_activateObject
{
    QDBusConnectionPrivate *self;
    const QDBusMessage &msg;
    bool returnVal;
    inline qdbus_activateObject(QDBusConnectionPrivate *s, const QDBusMessage &m)
        : self(s), msg(m)
    { }

    inline void operator()(QDBusConnectionPrivate::ObjectTreeNode *node)
    { returnVal = self->activateObject(node, msg); }
};

bool QDBusConnectionPrivate::handleObjectCall(const QDBusMessage &msg)
{
    QReadLocker locker(&lock);

    qdbus_activateObject apply(this, msg);
    if (applyForObject(&rootNode, msg.path(), apply))
        return apply.returnVal;

    qDebug("Call failed: no object found at %s", qPrintable(msg.path()));
    return false;
}

bool QDBusConnectionPrivate::handleSignal(const QString &key, const QDBusMessage& msg)
{
    bool result = false;
    SignalHookHash::const_iterator it = signalHooks.find(key);
    //qDebug("looking for: %s", path.toLocal8Bit().constData());
    //qDebug() << signalHooks.keys();
    for ( ; it != signalHooks.constEnd() && it.key() == key; ++it) {
        const SignalHook &hook = it.value();
        if ( !hook.sender.isEmpty() && hook.sender != msg.sender() )
            continue;
        if ( !hook.path.isEmpty() && hook.path != msg.path() )
            continue;
        if ( !hook.signature.isEmpty() && hook.signature != msg.signature() )
            continue;
        if ( hook.signature.isEmpty() && !hook.signature.isNull() && !msg.signature().isEmpty())
            continue;

        // yes, |=
        result |= activateSignal(hook, msg);
    }
    return result;
}

bool QDBusConnectionPrivate::handleSignal(const QDBusMessage& msg)
{
    QString key = msg.member();
    key.reserve(key.length() + 1 + msg.interface().length());
    key += QLatin1Char(':');
    key += msg.interface();

    QReadLocker locker(&lock);
    bool result = handleSignal(key, msg);    // one try

    key.truncate(msg.member().length() + 1); // keep the ':'
    result |= handleSignal(key, msg);        // second try
    return result;
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

#if USE_OUTSIDE_DISPATCH
    dbus_connection_add_filter_outside(connection, qDBusSignalFilter, qDBusSignalFilterOutside, this, 0);
#else
    dbus_connection_add_filter(connection, qDBusSignalFilter, this, 0);
#endif

    //qDebug("base service: %s", service);

    // schedule a dispatch:
    QMetaObject::invokeMethod(this, "doDispatch", Qt::QueuedConnection);
}

extern "C"{
static void qDBusResultReceived(DBusPendingCall *pending, void *user_data)
{
    QDBusConnectionPrivate::messageResultReceived(pending, user_data);
}
}

void QDBusConnectionPrivate::messageResultReceived(DBusPendingCall *pending, void *user_data)
{
    QDBusPendingCall *call = reinterpret_cast<QDBusPendingCall *>(user_data);
    QDBusConnectionPrivate *connection = const_cast<QDBusConnectionPrivate *>(call->connection);
    Q_ASSERT(call->pending == pending);

    if (!call->receiver.isNull() && call->methodIdx != -1) {
        DBusMessage *reply = dbus_pending_call_steal_reply(pending);

        // Deliver the return values of a remote function call.
        //
        // There is only one connection and it is specified by idx
        // The slot must have the same parameter types that the message does
        // The slot may have less parameters than the message
        // The slot may optionally have one final parameter that is QDBusMessage
        // The slot receives read-only copies of the message (i.e., pass by value or by const-ref)

        QDBusMessage msg = QDBusMessage::fromDBusMessage(reply, QDBusConnection(connection->name));
        qDebug() << "got message: " << msg;
        CallDeliveryEvent *e = prepareReply(call->receiver, call->methodIdx, call->metaTypes, msg);
        if (e)
            connection->postCallDeliveryEvent(e);
        else
            qDebug() << "Deliver failed!";
    }
    dbus_pending_call_unref(pending);
    delete call;
}

int QDBusConnectionPrivate::send(const QDBusMessage& message) const
{
    DBusMessage *msg = message.toDBusMessage();
    if (!msg)
        return 0;

    dbus_message_set_no_reply(msg, true); // the reply would not be delivered to anything

    qDebug() << "sending message:" << message;
    bool isOk = dbus_connection_send(connection, msg, 0);
    int serial = 0;
    if (isOk)
        serial = dbus_message_get_serial(msg);

    dbus_message_unref(msg);
    return serial;
}

QDBusMessage QDBusConnectionPrivate::sendWithReply(const QDBusMessage &message,
                                                   int sendMode)
{
    if (!QCoreApplication::instance() || sendMode == QDBusConnection::NoUseEventLoop) {
        DBusMessage *msg = message.toDBusMessage();
        if (!msg)
            return QDBusMessage();

        qDebug() << "sending message:" << message;
        DBusMessage *reply = dbus_connection_send_with_reply_and_block(connection, msg,
                                                                       -1, &error);
        handleError();
        dbus_message_unref(msg);

        if (lastError.isValid())
            return QDBusMessage::fromError(lastError);

        QDBusMessage amsg = QDBusMessage::fromDBusMessage(reply, QDBusConnection(name));
        qDebug() << "got message:" << amsg;

        if (dbus_connection_get_dispatch_status(connection) == DBUS_DISPATCH_DATA_REMAINS)
            QMetaObject::invokeMethod(this, "doDispatch", Qt::QueuedConnection);
        return amsg;
    } else {                    // use the event loop
        QDBusReplyWaiter waiter;
        if (sendWithReplyAsync(message, &waiter, SLOT(reply(const QDBusMessage&))) > 0) {
            // enter the event loop and wait for a reply
            waiter.exec(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents);

            lastError = waiter.replyMsg; // set or clear error
            return waiter.replyMsg;
        }

        return QDBusMessage();
    }
}    

int QDBusConnectionPrivate::sendWithReplyAsync(const QDBusMessage &message, QObject *receiver,
                                               const char *method)
{
    if (!receiver || !method || !*method)
        // would not be able to deliver a reply
        return send(message);

    int slotIdx = -1;
    QList<int> metaTypes;
    QByteArray normalizedName = QMetaObject::normalizedSignature(method + 1);
    slotIdx = findSlot(receiver, normalizedName, metaTypes);
    if (slotIdx == -1)
        // would not be able to deliver a reply
        return send(message);

    DBusMessage *msg = message.toDBusMessage();
    if (!msg)
        return 0;

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

void QDBusConnectionPrivate::connectSignal(const QString &key, const SignalHook &hook)
{
    signalHooks.insertMulti(key, hook);
    connect(hook.obj, SIGNAL(destroyed(QObject*)), SLOT(objectDestroyed(QObject*)));
}

void QDBusConnectionPrivate::registerObject(const ObjectTreeNode *node)
{
    connect(node->obj, SIGNAL(destroyed(QObject*)), SLOT(objectDestroyed(QObject*)));

    if (node->flags & QDBusConnection::ExportAdaptors) {
        QDBusAdaptorConnector *connector = qDBusCreateAdaptorConnector(node->obj);

        // disconnect and reconnect to avoid duplicates
        connector->disconnect(SIGNAL(relaySignal(QObject*,const char*,const char*,QVariantList)),
                              this, SLOT(relaySignal(QObject*,const char*,const char*,QVariantList)));
        connect(connector, SIGNAL(relaySignal(QObject*,const char*,const char*,QVariantList)),
                SLOT(relaySignal(QObject*,const char*,const char*,QVariantList)));
    }
}

void QDBusConnectionPrivate::connectRelay(const QString &service, const QString &path,
                                          const QString &interface,
                                          QDBusAbstractInterface *receiver,
                                          const char *signal)
{
    // this function is called by QDBusAbstractInterface when one of its signals is connected
    // we set up a relay from D-Bus into it
    SignalHook hook;
    QString key;
    if (!prepareHook(hook, key, service, path, interface, QString(), receiver, signal,
                     QDBusAbstractInterface::staticMetaObject.methodCount(), true))
        return;                 // don't connect

    // add it to our list:
    QWriteLocker locker(&lock);
    SignalHookHash::ConstIterator it = signalHooks.find(key);
    SignalHookHash::ConstIterator end = signalHooks.constEnd();
    for ( ; it != end && it.key() == key; ++it) {
        const SignalHook &entry = it.value();
        if (entry.sender == hook.sender &&
            entry.path == hook.path &&
            entry.signature == hook.signature &&
            entry.obj == hook.obj &&
            entry.midx == hook.midx)
            return;             // already there, no need to re-add
    }

    connectSignal(key, hook);
}

void QDBusConnectionPrivate::disconnectRelay(const QString &service, const QString &path,
                                             const QString &interface,
                                             QDBusAbstractInterface *receiver,
                                             const char *signal)
{
    // this function is called by QDBusAbstractInterface when one of its signals is disconnected
    // we remove relay from D-Bus into it
    SignalHook hook;
    QString key;
    if (!prepareHook(hook, key, service, path, interface, QString(), receiver, signal,
                     QDBusAbstractInterface::staticMetaObject.methodCount(), true))
        return;                 // don't connect

    // remove it from our list:
    QWriteLocker locker(&lock);
    SignalHookHash::Iterator it = signalHooks.find(key);
    SignalHookHash::Iterator end = signalHooks.end();
    for ( ; it != end && it.key() == key; ++it) {
        const SignalHook &entry = it.value();
        if (entry.sender == hook.sender &&
            entry.path == hook.path &&
            entry.signature == hook.signature &&
            entry.obj == hook.obj &&
            entry.midx == hook.midx) {
            // found it
            signalHooks.erase(it);
            return;
        }
    }

    qWarning("QDBusConnectionPrivate::disconnectRelay called for a signal that was not found");
}

QString QDBusConnectionPrivate::getNameOwner(const QString& serviceName)
{
    if (QDBusUtil::isValidUniqueConnectionName(serviceName))
        return serviceName;
    if (!connection || !QDBusUtil::isValidBusName(serviceName))
        return QString();

    QDBusMessage msg = QDBusMessage::methodCall(QLatin1String(DBUS_SERVICE_DBUS),
            QLatin1String(DBUS_PATH_DBUS), QLatin1String(DBUS_INTERFACE_DBUS),
            QLatin1String("GetNameOwner"));
    msg << serviceName;
    QDBusMessage reply = sendWithReply(msg, QDBusConnection::NoUseEventLoop);
    if (!lastError.isValid() && reply.type() == QDBusMessage::ReplyMessage)
        return reply.first().toString();
    return QString();
}

QDBusInterfacePrivate *
QDBusConnectionPrivate::findInterface(const QString &service,
                                      const QString &path,
                                      const QString &interface)
{
    // check if it's there first -- FIXME: add binding mode
    QDBusMetaObject *mo = 0;
    QString owner = getNameOwner(service);
    if (connection && !owner.isEmpty() && QDBusUtil::isValidObjectPath(path) &&
        (interface.isEmpty() || QDBusUtil::isValidInterfaceName(interface)))
        // always call here with the unique connection name
        mo = findMetaObject(owner, path, interface);

    QDBusInterfacePrivate *p = new QDBusInterfacePrivate(QDBusConnection(name), this, owner, path, interface, mo);

    if (!mo) {
        // invalid object
        p->isValid = false;
        p->lastError = lastError;
        if (!lastError.isValid()) {
            // try to determine why we couldn't get the data
            if (!connection)
                p->lastError = QDBusError(QDBusError::Disconnected,
                                          QLatin1String("Not connected to D-Bus server"));
            else if (owner.isEmpty())
                p->lastError = QDBusError(QDBusError::ServiceUnknown,
                                          QString(QLatin1String("Service %1 is unknown")).arg(service));
#if 0                           // caught by Q_ASSERT in QDBusConnection::findInterface
            else if (!QDBusUtil::isValidObjectPath(path))
                p->lastError = QDBusError(QDBusError::InvalidArgs,
                                          QString(QLatin1String("Object path %1 is invalid")).arg(path));
            else if (!interface.isEmpty() && !QDBusUtil::isValidInterfaceName(interface))
                p->lastError = QDBusError(QDBusError::InvalidArgs,
                                          QString(QLatin1String("Interface %1 is invalid")).arg(interface));
#endif
            else
                p->lastError = QDBusError(QDBusError::Other, QLatin1String("Unknown error"));
        }
    }

    return p;
}

struct qdbus_Introspect
{
    QString xml;
    inline void operator()(QDBusConnectionPrivate::ObjectTreeNode *node)
    { xml = qDBusIntrospectObject(node); }
};

QDBusMetaObject *
QDBusConnectionPrivate::findMetaObject(const QString &service, const QString &path,
                                       const QString &interface)
{
    // service must be a unique connection name
    if (!interface.isEmpty()) {
        QReadLocker locker(&lock);
        QDBusMetaObject *mo = cachedMetaObjects.value(interface, 0);
        if (mo)
            return mo;
    }
    if (service == QString::fromUtf8(dbus_bus_get_unique_name(connection))) {
        // it's one of our own
        QWriteLocker locker(&lock);
        QDBusMetaObject *mo = 0;
        if (!interface.isEmpty())
            mo = cachedMetaObjects.value(interface, 0);
        if (mo)
            // maybe it got created when we switched from read to write lock
            return mo;

        qdbus_Introspect apply;
        if (!applyForObject(&rootNode, path, apply)) {
            lastError = QDBusError(QDBusError::InvalidArgs,
                                   QString(QLatin1String("No object at %1")).arg(path));
            return 0;           // no object at path
        }

        // release the lock and return
        return QDBusMetaObject::createMetaObject(interface, apply.xml, cachedMetaObjects, lastError);
    }

    // not local: introspect the target object:
    QDBusMessage msg = QDBusMessage::methodCall(service, path,
                                                QLatin1String(DBUS_INTERFACE_INTROSPECTABLE),
                                                QLatin1String("Introspect"));


    QDBusMessage reply = sendWithReply(msg, QDBusConnection::NoUseEventLoop);

    // it doesn't exist yet, we have to create it
    QWriteLocker locker(&lock);
    QDBusMetaObject *mo = 0;
    if (!interface.isEmpty())
        mo = cachedMetaObjects.value(interface, 0);
    if (mo)
        // maybe it got created when we switched from read to write lock
        return mo;

    QString xml;
    if (reply.type() == QDBusMessage::ReplyMessage)
        // fetch the XML description
        xml = reply.first().toString();
    else {
        lastError = reply;
        if (reply.type() != QDBusMessage::ErrorMessage || lastError != QDBusError::UnknownMethod)
            return 0;           // error
    }

    // release the lock and return
    return QDBusMetaObject::createMetaObject(interface, xml, cachedMetaObjects, lastError);
}

void QDBusReplyWaiter::reply(const QDBusMessage &msg)
{
    replyMsg = msg;
    QTimer::singleShot(0, this, SLOT(quit()));
}

#include "qdbusconnection_p.moc"
