/* qdbusintegrator.cpp QDBusConnection private implementation
 *
 * Copyright (C) 2005 Harald Fernengel <harry@kdevelop.org>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <QtCore/qcoreapplication.h>
#include <QtCore/qcoreevent.h>
#include <QtCore/qdebug.h>
#include <QtCore/qmetaobject.h>
#include <QtCore/qsocketnotifier.h>

#include "qdbusconnection_p.h"
#include "qdbusmessage.h"

int QDBusConnectionPrivate::messageMetaType = 0;

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
            d->connect(watcher.read, SIGNAL(activated(int)), SLOT(socketRead(int)));
        }
    }
    if (flags & DBUS_WATCH_WRITABLE) {
        qDebug("addWriteWatch %d", fd);
        watcher.watch = watch;
        if (QCoreApplication::instance()) {
            watcher.write = new QSocketNotifier(fd, QSocketNotifier::Write, d);
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
    qDebug() << "got message: " << dbus_message_get_type(message) << amsg;

    if (msgType == DBUS_MESSAGE_TYPE_SIGNAL) {
        handled = d->handleSignal(message);
    } else if (msgType == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        handled = d->handleObjectCall(message);
    }

    return handled ? DBUS_HANDLER_RESULT_HANDLED :
            DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static bool qInvokeDBusSlot(const QDBusConnectionPrivate::SignalHook& hook, const QDBusMessage &msg)
{
    int count = msg.count();
    if (!(count == hook.params.count()
        || (count + 1 == hook.params.count()
        && hook.params[count] == QDBusConnectionPrivate::messageMetaType)))
        return false;

    QVarLengthArray<void *, 16> params;
    params.append(0); // return value
    for (int i = 0; i < msg.count(); ++i) {
        const QVariant &v = msg.at(i);
        if (int(v.type()) != hook.params[i]) {
            return false;
        }
        params.append(const_cast<void *>(v.constData()));
    }
    if (count + 1 == hook.params.count())
        params.append(const_cast<QDBusMessage *>(&msg));
    return hook.obj->qt_metacall(QMetaObject::InvokeMetaMethod, hook.midx, params.data()) < 0;
}

static bool qInvokeDBusSlot(QObject *object, int idx, const QDBusMessage &msg)
{
    Q_ASSERT(object);

    const QMetaMethod method = object->metaObject()->method(idx);
    if (!method.signature())
        return false;

    QVarLengthArray<void *> params;
    params.append(0); // ### return type

    QList<QByteArray> parameterTypes = method.parameterTypes();

    // check parameters, the slot should have <= parameters than the message
    // also allow the QDBusMessage itself as last parameter slot
    if ((parameterTypes.count() > msg.count())
       || (parameterTypes.count() + 1 != msg.count())
          && parameterTypes.last() != "QDBusMessage") {
        qWarning("Cannot deliver asynchronous reply to object named '%s' because of parameter "
                 "mismatch. Please check your sendWithReplyAsync() statements.",
                object->objectName().toLocal8Bit().constData());
        return false;
    }

    int i;
    for (i = 0; i < parameterTypes.count(); ++i) {
        const QByteArray param = parameterTypes.at(i);
        if (param == msg.at(i).typeName()) {
            params.append(const_cast<void *>(msg.at(i).constData()));
        } else if (i == parameterTypes.count() - 1 && param == "QDBusMessage") {
            params.append(const_cast<void *>(static_cast<const void *>(&msg)));
        } else {
            qWarning("Parameter mismatch while delivering message, expected '%s', got '%s'",
                     msg.at(i).typeName(), param.constData());
            return false;
        }
    }
    return object->qt_metacall(QMetaObject::InvokeMetaMethod, idx, params.data()) < 0;
}

static bool qInvokeDBusSlot(QObject *object, QDBusMessage *msg)
{
    Q_ASSERT(object);
    Q_ASSERT(msg);

    const QMetaObject *mo = object->metaObject();
    QVarLengthArray<void *> params;
    params.append(0); // ### return type

    /* Try to find a slot with all args and the QDBusMessage */
    QByteArray slotName = msg->name().toUtf8(); // QVarLengthArray?
    slotName.append("(");
    for (int i = 0; i < msg->count(); ++i) {
        slotName.append(msg->at(i).typeName()).append(",");
        params.append(const_cast<void *>(msg->at(i).constData()));
    }
    slotName.append("QDBusMessage)");

    int idx = mo->indexOfSlot(slotName.constData());
    if (idx >= 0) {
        params.append(msg);
        return object->qt_metacall(QMetaObject::InvokeMetaMethod, idx, params.data()) < 0;
    }

    /* Try to find only args, without the QDBusMessage */
    slotName.chop(13);
    slotName[slotName.count() - 1] = ')';

    idx = mo->indexOfSlot(slotName.constData());
    if (idx >= 0 && (mo->method(idx).attributes() & QMetaMethod::Scriptable))
        return object->qt_metacall(QMetaObject::InvokeMetaMethod, idx, params.data()) < 0;

    /* Try to find a slot with only QDBusMessage */
    slotName = msg->name().toUtf8();
    slotName.append("(QDBusMessage)");

    idx = mo->indexOfSlot(slotName.constData());
    if (idx >= 0)
        return QMetaObject::invokeMethod(object, msg->name().toUtf8().constData(),
                                         Q_ARG(QDBusMessage, *msg));

    return false;
}

int QDBusConnectionPrivate::registerMessageMetaType()
{
    int tp = messageMetaType = qRegisterMetaType<QDBusMessage>("QDBusMessage");
    return tp;
}

bool QDBusConnectionPrivate::SignalHook::setSlot(const char *slotName)
{
    Q_ASSERT(static_cast<QObject *>(obj)); Q_ASSERT(slotName);

    QByteArray normalizedName = QMetaObject::normalizedSignature(slotName);
    const QMetaObject *mo = obj->metaObject();
    midx = mo->indexOfMethod(normalizedName.constData());
    if (midx < 0)
        return false;

    const QList<QByteArray> ptypes = mo->method(midx).parameterTypes();
    for (int i = 0; i < ptypes.count(); ++i) {
        int t = QVariant::nameToType(ptypes.at(i).constData());
        if (t == QVariant::UserType)
            t = QMetaType::type(ptypes.at(i).constData());
        if (t == QVariant::Invalid)
            return false;
        params.append(t);
    }

    return true;
}

QDBusConnectionPrivate::QDBusConnectionPrivate(QObject *parent)
    : QObject(parent), ref(1), mode(InvalidMode), connection(0), server(0)
{
    static const int msgType = registerMessageMetaType();
    Q_UNUSED(msgType);

    dbus_error_init(&error);
}

QDBusConnectionPrivate::~QDBusConnectionPrivate()
{
    if (dbus_error_is_set(&error))
        dbus_error_free(&error);

    closeConnection();
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
        // ### break out of loop?
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
        if (static_cast<QObject *>(it.value().obj) == obj)
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

bool QDBusConnectionPrivate::handleObjectCall(DBusMessage *message) const
{
    QDBusMessage msg = QDBusMessage::fromDBusMessage(message);

    ObjectHook hook;
    ObjectHookHash::ConstIterator it = objectHooks.find(msg.path());
    while (it != objectHooks.constEnd() && it.key() == msg.path()) {
        if (it.value().interface == msg.interface()) {
            hook = it.value();
            break;
        } else if (it.value().interface.isEmpty()) {
            hook = it.value();
        }
        ++it;
    }

    if (!hook.obj) {
        qDebug("NO OBJECT for %s", msg.path().toLocal8Bit().constData());
        return false;
    }

    if (!qInvokeDBusSlot(hook.obj, &msg)) {
        qDebug("NO SUCH SLOT: %s(QDBusMessage)", msg.name().toLocal8Bit().constData());
        return false;
    }

    return true;
}

bool QDBusConnectionPrivate::handleSignal(const QString &path, const QDBusMessage &msg) const
{
    SignalHookHash::const_iterator it = signalHooks.find(path);
    qDebug("looking for: %s", path.toLocal8Bit().constData());
    qDebug() << signalHooks.keys();
    while (it != signalHooks.constEnd() && it.key() == path) {
        const SignalHook &hook = it.value();
        if ((hook.name.isEmpty() || hook.name == msg.name())
             && (hook.interface.isEmpty() || hook.interface == msg.interface()))
            qInvokeDBusSlot(hook, msg);
        ++it;
    }
    return true;
}

bool QDBusConnectionPrivate::handleSignal(DBusMessage *message) const
{
    QDBusMessage msg = QDBusMessage::fromDBusMessage(message);

    // yes, it is a single "|" below...
    return handleSignal(QString(), msg) | handleSignal(msg.path(), msg);
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

struct QDBusPendingCall
{
    QPointer<QObject> receiver;
    int methodIdx;
    DBusPendingCall *pending;
};

static void qDBusResultReceived(DBusPendingCall *pending, void *user_data)
{
    QDBusPendingCall *call = reinterpret_cast<QDBusPendingCall *>(user_data);
    Q_ASSERT(call->pending == pending);

    if (!call->receiver.isNull() && call->methodIdx != -1) {
        DBusMessage *reply = dbus_pending_call_steal_reply(pending);
        qInvokeDBusSlot(call->receiver, call->methodIdx, QDBusMessage::fromDBusMessage(reply));
    }
    dbus_pending_call_unref(pending);
    delete call;
}

int QDBusConnectionPrivate::sendWithReplyAsync(const QDBusMessage &message, QObject *receiver,
        const char *method) const
{
    DBusMessage *msg = message.toDBusMessage();
    if (!msg)
        return 0;

    int slotIdx = -1;
    if (receiver && method && *method) {
        QByteArray normalized = QMetaObject::normalizedSignature(method + 1);
        slotIdx = receiver->metaObject()->indexOfMethod(normalized.constData());
        if (slotIdx == -1)
            qWarning("QDBusConnection::sendWithReplyAsync: no such method: '%s'",
                     normalized.constData());
    }

    DBusPendingCall *pending = 0;
    if (dbus_connection_send_with_reply(connection, msg, &pending, message.timeout())) {
        if (slotIdx != -1) {
            QDBusPendingCall *pcall = new QDBusPendingCall;
            pcall->receiver = receiver;
            pcall->methodIdx = slotIdx;
            pcall->pending = dbus_pending_call_ref(pending);
            dbus_pending_call_set_notify(pending, qDBusResultReceived, pcall, 0);
        }
        return dbus_message_get_serial(msg);
    }

    return 0;
}
