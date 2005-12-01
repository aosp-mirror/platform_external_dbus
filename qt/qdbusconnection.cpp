/* qdbusconnection.cpp
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

#include <QtCore/qdebug.h>
#include <QtCore/qcoreapplication.h>

#include "qdbusconnection.h"
#include "qdbusconnection_p.h"

QT_STATIC_CONST_IMPL char *QDBusConnection::default_connection_name = "qt_dbus_default_connection";

class QDBusConnectionManager
{
public:
    QDBusConnectionManager(): default_connection(0) {}
    ~QDBusConnectionManager();
    void bindToApplication();
    QDBusConnectionPrivate *connection(const QString &name) const;
    void removeConnection(const QString &name);
    void setConnection(const QString &name, QDBusConnectionPrivate *c);

private:
    QDBusConnectionPrivate *default_connection;
    QHash<QString, QDBusConnectionPrivate *> connectionHash;
};

Q_GLOBAL_STATIC(QDBusConnectionManager, manager);

QDBusConnectionPrivate *QDBusConnectionManager::connection(const QString &name) const
{
    return name == QLatin1String(QDBusConnection::default_connection_name) ?
            default_connection : connectionHash.value(name, 0);
}

void QDBusConnectionManager::removeConnection(const QString &name)
{
    QDBusConnectionPrivate *d = 0;
    if (name == QLatin1String(QDBusConnection::default_connection_name)) {
        d = default_connection;
        default_connection = 0;
    } else {
        d = connectionHash.take(name);
    }
    if (!d->ref.deref())
        delete d;
}

QDBusConnectionManager::~QDBusConnectionManager()
{
    if (default_connection) {
        delete default_connection;
        default_connection = 0;
    }
    for (QHash<QString, QDBusConnectionPrivate *>::const_iterator it = connectionHash.constBegin();
         it != connectionHash.constEnd(); ++it) {
             delete it.value();
    }
    connectionHash.clear();
}

void QDBusConnectionManager::bindToApplication()
{
    if (default_connection) {
        default_connection->bindToApplication();
    }
    for (QHash<QString, QDBusConnectionPrivate *>::const_iterator it = connectionHash.constBegin();
         it != connectionHash.constEnd(); ++it) {
             (*it)->bindToApplication();
    }
}

void qDBusBindToApplication()
{
    manager()->bindToApplication();
}

void QDBusConnectionManager::setConnection(const QString &name, QDBusConnectionPrivate *c)
{
    if (name == QLatin1String(QDBusConnection::default_connection_name))
        default_connection = c;
    else
        connectionHash[name] = c;
}


QDBusConnection::QDBusConnection(const QString &name)
{
    d = manager()->connection(name);
    if (d)
        d->ref.ref();
}

QDBusConnection::QDBusConnection(const QDBusConnection &other)
{
    d = other.d;
    if (d)
        d->ref.ref();
}

QDBusConnection::~QDBusConnection()
{
    if (d && !d->ref.deref())
        delete d;
}

QDBusConnection &QDBusConnection::operator=(const QDBusConnection &other)
{
    if (other.d)
        other.d->ref.ref();
    QDBusConnectionPrivate *old = static_cast<QDBusConnectionPrivate *>(
            q_atomic_set_ptr(&d, other.d));
    if (old && !old->ref.deref())
        delete old;

    return *this;
}

QDBusConnection QDBusConnection::addConnection(BusType type, const QString &name)
{
//    Q_ASSERT_X(QCoreApplication::instance(), "QDBusConnection::addConnection",
//               "Cannot create connection without a Q[Core]Application instance");

    QDBusConnectionPrivate *d = manager()->connection(name);
    if (d)
        return QDBusConnection(name);

    d = new QDBusConnectionPrivate;
    DBusConnection *c = 0;
    switch (type) {
        case SystemBus:
            c = dbus_bus_get(DBUS_BUS_SYSTEM, &d->error);
            break;
        case SessionBus:
            c = dbus_bus_get(DBUS_BUS_SESSION, &d->error);
            break;
        case ActivationBus:
            c = dbus_bus_get(DBUS_BUS_STARTER, &d->error);
            break;
    }
    d->setConnection(c); //setConnection does the error handling for us

    manager()->setConnection(name, d);

    return QDBusConnection(name);
}

QDBusConnection QDBusConnection::addConnection(const QString &address,
                    const QString &name)
{
//    Q_ASSERT_X(QCoreApplication::instance(), "QDBusConnection::addConnection",
//               "Cannot create connection without a Q[Core]Application instance");

    QDBusConnectionPrivate *d = manager()->connection(name);
    if (d)
        return QDBusConnection(name);

    d = new QDBusConnectionPrivate;
    // setConnection does the error handling for us
    d->setConnection(dbus_connection_open(address.toUtf8().constData(), &d->error));

    manager()->setConnection(name, d);

    return QDBusConnection(name);
}

void QDBusConnection::closeConnection(const QString &name)
{
    manager()->removeConnection(name);
}

void QDBusConnectionPrivate::timerEvent(QTimerEvent *e)
{
    DBusTimeout *timeout = timeouts.value(e->timerId(), 0);
    dbus_timeout_handle(timeout);
}

bool QDBusConnection::send(const QDBusMessage &message) const
{
    if (!d || !d->connection)
        return false;

    DBusMessage *msg = message.toDBusMessage();
    if (!msg)
        return false;

    bool isOk = dbus_connection_send(d->connection, msg, 0);
    dbus_message_unref(msg);
    return isOk;
}

int QDBusConnection::sendWithReplyAsync(const QDBusMessage &message, QObject *receiver,
        const char *method) const
{
    if (!d || !d->connection)
        return 0;

    return d->sendWithReplyAsync(message, receiver, method);
}

QDBusMessage QDBusConnection::sendWithReply(const QDBusMessage &message) const
{
    if (!d || !d->connection)
        return QDBusMessage::fromDBusMessage(0);

    DBusMessage *msg = message.toDBusMessage();
    if (!msg)
        return QDBusMessage::fromDBusMessage(0);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(d->connection, msg,
                                                -1, &d->error);
    d->handleError();
    dbus_message_unref(msg);

    return QDBusMessage::fromDBusMessage(reply);
}

bool QDBusConnection::connect(const QString &path, const QString &interface,
                              const QString &name, QObject *receiver, const char *slot)
{
    if (!receiver || !slot || !d || !d->connection)
        return false;

    QDBusConnectionPrivate::SignalHook hook;

    hook.interface = interface;
    hook.name = name;
    hook.obj = QPointer<QObject>(receiver);
    if (!hook.setSlot(slot + 1))
        return false;

    d->signalHooks.insertMulti(path, hook);
    d->connect(receiver, SIGNAL(destroyed(QObject*)), SLOT(objectDestroyed(QObject*)));

    return true;
}

bool QDBusConnection::registerObject(const QString &path, const QString &interface,
                                     QObject *object)
{
    if (!d || !d->connection || !object || path.isEmpty() || interface.isEmpty())
        return false;

    QDBusConnectionPrivate::ObjectHook hook;
    hook.interface = interface;
    hook.obj = object;

    QDBusConnectionPrivate::ObjectHookHash::iterator it = d->objectHooks.find(path);
    while (it != d->objectHooks.end() && it.key() == path) {
        if (it.value().interface == interface) {
            d->objectHooks.erase(it);
            break;
        }
        ++it;
    }

    d->objectHooks.insert(path, hook);

    d->connect(object, SIGNAL(destroyed(QObject*)), SLOT(objectDestroyed(QObject*)));
    qDebug("REGISTERED FOR %s", path.toLocal8Bit().constData());

    return true; // todo - check for slots etc.
}

void QDBusConnection::unregisterObject(const QString &path)
{
    if (!d || !d->connection)
        return;

    // TODO - check interfaces
    d->objectHooks.remove(path);
}

bool QDBusConnection::isConnected( ) const
{
    return d && d->connection && dbus_connection_get_is_connected(d->connection);
}

QDBusError QDBusConnection::lastError() const
{
    return d ? d->lastError : QDBusError();
}

QString QDBusConnection::baseService() const
{
    return d && d->connection ?
            QString::fromUtf8(dbus_bus_get_unique_name(d->connection))
            : QString();
}

bool QDBusConnection::requestName(const QString &name, NameRequestMode mode)
{
    //FIXME: DBUS_NAME_FLAGS_* are bit fields not enumeration
    static const int DBusModes[] = { 0, DBUS_NAME_FLAG_ALLOW_REPLACEMENT,
        DBUS_NAME_FLAG_REPLACE_EXISTING };
    Q_ASSERT(mode == 0 || mode == AllowReplace ||
             mode == ReplaceExisting );

    DBusError error;
    dbus_error_init (&error);
    dbus_bus_request_name(d->connection, name.toUtf8(), DBusModes[mode], &error);
    if (dbus_error_is_set (&error)) {
        qDebug("Error %s\n", error.message);
        dbus_error_free (&error);
        return false;
    }
    return true;
}

#include "qdbusconnection.moc"
