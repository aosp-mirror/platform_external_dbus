/* qdbusconnection.cpp
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

#include <qdebug.h>
#include <qcoreapplication.h>

#include "qdbusconnection.h"
#include "qdbuserror.h"
#include "qdbusmessage.h"
#include "qdbusconnection_p.h"
#include "qdbusinterface_p.h"
#include "qdbusobject_p.h"
#include "qdbusutil.h"

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
    return d->send(message);
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

    if (!QCoreApplication::instance()) {
        DBusMessage *msg = message.toDBusMessage();
        if (!msg)
            return QDBusMessage::fromDBusMessage(0);

        DBusMessage *reply = dbus_connection_send_with_reply_and_block(d->connection, msg,
                                                                       -1, &d->error);
        d->handleError();
        dbus_message_unref(msg);

        if (lastError().isValid())
            return QDBusMessage::fromError(lastError());

        return QDBusMessage::fromDBusMessage(reply);
    } else {
        QDBusReplyWaiter waiter;
        if (d->sendWithReplyAsync(message, &waiter, SLOT(reply(const QDBusMessage&))) > 0) {
            // enter the event loop and wait for a reply
            waiter.exec(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents);
        
            d->lastError = waiter.replyMsg; // set or clear error
            return waiter.replyMsg;
        }

        return QDBusMessage::fromDBusMessage(0);
    }
}

bool QDBusConnection::connect(const QString &service, const QString &path, const QString& interface,
                              const QString &name, QObject *receiver, const char *slot)
{
    return connect(service, path, interface, name, QString(), receiver, slot);
}

bool QDBusConnection::connect(const QString &service, const QString &path, const QString& interface,
                              const QString &name, const QString &signature,
                              QObject *receiver, const char *slot)
{
    if (!receiver || !slot || !d || !d->connection)
        return false;

    QString source = getNameOwner(service);
    if (source.isEmpty())
        return false;
    source += path;

    // check the slot
    QDBusConnectionPrivate::SignalHook hook;
    if ((hook.midx = QDBusConnectionPrivate::findSlot(receiver, slot + 1, hook.params)) == -1)
        return false;
    
    hook.interface = interface;
    hook.name = name;
    hook.signature = signature;
    hook.obj = QPointer<QObject>(receiver);

    d->signalHooks.insertMulti(source, hook);
    d->connect(receiver, SIGNAL(destroyed(QObject*)), SLOT(objectDestroyed(QObject*)));

    return true;
}

bool QDBusConnection::registerObject(const QString &path, QObject *object, RegisterOptions options)
{
    return registerObject(path, QString(), object, options);
}

bool QDBusConnection::registerObject(const QString &path, const QString &interface,
                                     QObject *object, RegisterOptions options)
{
    if (!d || !d->connection || !object || !options || !QDBusUtil::isValidObjectPath(path))
        return false;

    QString iface = interface;
    if (options & ExportForAnyInterface)
        iface.clear();

    QDBusConnectionPrivate::ObjectDataHash& hook = d->objectHooks[path];

    // if we're replacing and matching any interface, then we're replacing every interface
    // this catches ExportAdaptors | Reexport too
    if (( options & ( ExportForAnyInterface | Reexport )) == ( ExportForAnyInterface | Reexport ))
        hook.clear();

    // we're not matching any interface, but if we're not replacing, make sure it doesn't exist yet
    else if (( options & Reexport ) == 0 && hook.find(iface) != hook.end())
        return false;

    QDBusConnectionPrivate::ObjectData& data = hook[iface];

    data.flags = options;
    data.obj = object;

    d->connect(object, SIGNAL(destroyed(QObject*)), SLOT(objectDestroyed(QObject*)));
    qDebug("REGISTERED FOR %s", path.toLocal8Bit().constData());

    return true; // todo - check for slots etc.
}

void QDBusConnection::unregisterObject(const QString &path)
{
    if (!d || !d->connection)
        return;

    d->objectHooks.remove(path);
}

QDBusInterface QDBusConnection::findInterface(const QString& service, const QString& path,
                                              const QString& interface)
{
    // create one
    QDBusInterfacePrivate *priv = new QDBusInterfacePrivate;
    priv->conn = *this;

    if (!QDBusUtil::isValidObjectPath(path) || !QDBusUtil::isValidInterfaceName(interface))
        return QDBusInterface(priv);

    // check if it's there first
    QString owner = getNameOwner(service);
    if (owner.isEmpty())
        return QDBusInterface(priv);

    // getNameOwner returns empty if d is 0
    Q_ASSERT(d);
    priv->service = owner;
    priv->path = path;
    priv->data = d->findInterface(interface).constData();

    return QDBusInterface(priv); // will increment priv's refcount
}

QDBusObject QDBusConnection::findObject(const QString& service, const QString& path)
{
    QDBusObjectPrivate* priv = 0;
    if (d && QDBusUtil::isValidObjectPath(path)) {
        QString owner = getNameOwner(service);
        
        if (!owner.isEmpty())
            priv = new QDBusObjectPrivate(d, owner, path);
    }
    return QDBusObject(priv, *this);
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
    static const int DBusModes[] = { DBUS_NAME_FLAG_ALLOW_REPLACEMENT, 0,
        DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_ALLOW_REPLACEMENT};

    int retval = dbus_bus_request_name(d->connection, name.toUtf8(), DBusModes[mode], &d->error);
    d->handleError();
    return retval == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ||
        retval == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER;
}

bool QDBusConnection::releaseName(const QString &name)
{
    int retval = dbus_bus_release_name(d->connection, name.toUtf8(), &d->error);
    d->handleError();
    if (lastError().isValid())
        return false;
    return retval == DBUS_RELEASE_NAME_REPLY_RELEASED;
}

QString QDBusConnection::getNameOwner(const QString& name)
{
    if (QDBusUtil::isValidUniqueConnectionName(name))
        return name;
    if (!d || !QDBusUtil::isValidBusName(name))
        return QString();
    
    QDBusMessage msg = QDBusMessage::methodCall(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
                                                DBUS_INTERFACE_DBUS, "GetNameOwner");
    msg << name;
    QDBusMessage reply = sendWithReply(msg);
    if (!lastError().isValid() && reply.type() == QDBusMessage::ReplyMessage)
        return reply.first().toString();
    return QString();
}

#include "qdbusconnection_p.moc"
