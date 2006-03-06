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

class QDBusConnectionManager
{
public:
    QDBusConnectionManager() {}
    ~QDBusConnectionManager();
    void bindToApplication();
    QDBusConnectionPrivate *connection(const QString &name) const;
    void removeConnection(const QString &name);
    void setConnection(const QString &name, QDBusConnectionPrivate *c);

private:
    mutable QMutex mutex;
    QHash<QString, QDBusConnectionPrivate *> connectionHash;
};

Q_GLOBAL_STATIC(QDBusConnectionManager, manager);

QDBusConnectionPrivate *QDBusConnectionManager::connection(const QString &name) const
{
    QMutexLocker locker(&mutex);
    return connectionHash.value(name, 0);
}

void QDBusConnectionManager::removeConnection(const QString &name)
{
    QMutexLocker locker(&mutex);

    QDBusConnectionPrivate *d = 0;
    d = connectionHash.take(name);
    if (d && !d->ref.deref())
        delete d;
}

QDBusConnectionManager::~QDBusConnectionManager()
{
    for (QHash<QString, QDBusConnectionPrivate *>::const_iterator it = connectionHash.constBegin();
         it != connectionHash.constEnd(); ++it) {
             delete it.value();
    }
    connectionHash.clear();
}

void QDBusConnectionManager::bindToApplication()
{
    QMutexLocker locker(&mutex);
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
    connectionHash[name] = c;
    c->name = name;
}

/*!
    \fn QDBusConnection QDBus::sessionBus()

    Returns a QDBusConnection object opened with the session bus. The object reference returned
    by this function is valid until the QCoreApplication's destructor is run, when the
    connection will be closed and the object, deleted.
*/
/*!
    \fn QDBusConnection QDBus::systemBus()

    Returns a QDBusConnection object opened with the system bus. The object reference returned
    by this function is valid until the QCoreApplication's destructor is run, when the
    connection will be closed and the object, deleted.
*/

/*!
    \class QDBusConnection
    \brief A connection to the D-Bus bus daemon.

    This class is the initial point in a D-Bus session. Using it, you can get access to remote
    objects, interfaces; connect remote signals to your object's slots; register objects, etc.

    D-Bus connections are created using the QDBusConnection::addConnection function, which opens a
    connection to the server daemon and does the initial handshaking, associating that connection
    with a name. Further attempts to connect using the same name will return the same
    connection.

    The connection is then torn down using the QDBusConnection::closeConnection function.

    As a convenience for the two most common connection types, the QDBus::sessionBus and
    QDBus::systemBus functions return open connections to the session server daemon and the system
    server daemon, respectively. Those connections are opened when first used and are closed when
    the QCoreApplication destructor is run.

    D-Bus also supports peer-to-peer connections, without the need for a bus server daemon. Using
    this facility, two applications can talk to each other and exchange messages. This can be
    achieved by passing an address to QDBusConnection::addConnection(const QString &, const QString
    &) function, which was opened by another D-Bus application using QDBusServer.
*/

/*!
    \enum QDBusConnection::BusType
    Specifies the type of the bus connection. The valid bus types are:

    \value SessionBus           the session bus, associated with the running desktop session
    \value SystemBus            the system bus, used to communicate with system-wide processes
    \value ActivationBus        the activation bus, whose purpose I have no idea...

    On the Session Bus, one can find other applications by the same user that are sharing the same
    desktop session (hence the name). On the System Bus, however, processes shared for the whole
    system are usually found.

    \todo Find out what the ActivationBus is for
*/

/*!
    \enum QDBusConnection::NameRequestMode
    Specifies the flags for when requesting a name in the bus.

    \bug Change the enum into flags and update with the new flags from the spec.
*/

/*!
    \enum QDBusConnection::RegisterOption
    Specifies the options for registering objects with the connection. The possible values are:

    \value ExportAdaptors                       export the contents of adaptors found in this object

    \value ExportSlots                          export this object's scriptable slots
    \value ExportSignals                        export this object's scriptable signals
    \value ExportProperties                     export this object's scriptable properties
    \value ExportContents                       shorthand form for ExportSlots | ExportSignals |
                                                ExportProperties

    \value ExportNonScriptableSlots             export all of this object's slots, including
                                                non-scriptable ones
    \value ExportNonScriptableSignals           export all of this object's signals, including
                                                non-scriptable ones
    \value ExportNonScriptableProperties        export all of this object's properties, including
                                                non-scriptable ones
    \value ExportNonScriptableContents          export all of this object's slots, signals and
                                                properties, including non-scriptable ones

    \value ExportChildObjects                   export this object's child objects

    \note It is currently not possible to export signals from objects. If you pass the flag
    ExportSignals or ExportNonScriptableSignals, the registerObject() function will print a warning.

    \sa QDBusConnection::registerObject, QDBusAbstractAdaptor, \ref UsingAdaptors
*/

/*!
    \enum QDBusConnection::UnregisterMode
    The mode for unregistering an object path:

    \value UnregisterNode       unregister this node only: do not unregister child objects
    \value UnregisterTree       unregister this node and all its sub-tree

    Note, however, if this object was registered with the ExportChildObjects option, UnregisterNode
    will unregister the child objects too.
*/

/*!
    Creates a QDBusConnection object attached to the connection with name \p name.

    This does not open the connection. You have to call QDBusConnection::addConnection to open it.
*/
QDBusConnection::QDBusConnection(const QString &name)
{
    d = manager()->connection(name);
    if (d)
        d->ref.ref();
}

/*!
    Creates a copy of the \p other connection.
*/
QDBusConnection::QDBusConnection(const QDBusConnection &other)
{
    d = other.d;
    if (d)
        d->ref.ref();
}

/*!
    Disposes of this object. This does not close the connection: you have to call
    QDBusConnection::closeConnection to do that.
*/
QDBusConnection::~QDBusConnection()
{
    if (d && !d->ref.deref())
        delete d;
}

/*!
    Creates a copy of the connection \p other in this object. The connection this object referenced
    before the copy is not spontaneously disconnected. See QDBusConnection::closeConnection for more
    information.
*/
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

/*!
    Opens a connection of type \p type to one of the known busses and associate with it the
    connection name \p name. Returns a QDBusConnection object associated with that connection.
*/
QDBusConnection QDBusConnection::addConnection(BusType type, const QString &name)
{
//    Q_ASSERT_X(QCoreApplication::instance(), "QDBusConnection::addConnection",
//               "Cannot create connection without a Q[Core]Application instance");

    QDBusConnectionPrivate *d = manager()->connection(name);
    if (d || name.isEmpty())
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

/*!
    Opens a peer-to-peer connection on address \p address and associate with it the
    connection name \p name. Returns a QDBusConnection object associated with that connection.
*/
QDBusConnection QDBusConnection::addConnection(const QString &address,
                    const QString &name)
{
//    Q_ASSERT_X(QCoreApplication::instance(), "QDBusConnection::addConnection",
//               "Cannot create connection without a Q[Core]Application instance");

    QDBusConnectionPrivate *d = manager()->connection(name);
    if (d || name.isEmpty())
        return QDBusConnection(name);

    d = new QDBusConnectionPrivate;
    // setConnection does the error handling for us
    d->setConnection(dbus_connection_open(address.toUtf8().constData(), &d->error));

    manager()->setConnection(name, d);

    return QDBusConnection(name);
}

/*!
    Closes the connection of name \p name.

    Note that if there are still QDBusConnection objects associated with the same connection, the
    connection will not be closed until all references are dropped. However, no further references
    can be created using the QDBusConnection::QDBusConnection constructor.
*/
void QDBusConnection::closeConnection(const QString &name)
{
    manager()->removeConnection(name);
}

void QDBusConnectionPrivate::timerEvent(QTimerEvent *e)
{
    DBusTimeout *timeout = timeouts.value(e->timerId(), 0);
    dbus_timeout_handle(timeout);
}

/*!
    Sends the message over this connection, without waiting for a reply. This is suitable for errors,
    signals, and return values as well as calls whose return values are not necessary.

    \returns true if the message was queued successfully, false otherwise
*/
bool QDBusConnection::send(const QDBusMessage &message) const
{
    if (!d || !d->connection)
        return false;
    return d->send(message);
}

/*!
    Sends the message over this connection and returns immediately after queueing it. When the reply
    is received, the slot \p method is called in the object \p receiver. This function is suitable
    for method calls only.

    This function guarantees that the slot will be called exactly once with the reply, as long as
    the parameter types match. If they don't, the reply cannot be delivered.

    \returns true if the message was queued successfully, false otherwise.
*/
int QDBusConnection::sendWithReplyAsync(const QDBusMessage &message, QObject *receiver,
        const char *method) const
{
    if (!d || !d->connection)
        return 0;

    return d->sendWithReplyAsync(message, receiver, method);
}

/*!
    Sends the message over this connection and blocks, waiting for a reply. This function is
    suitable for method calls only. It returns the reply message as its return value, which will be
    either of type QDBusMessage::ReplyMessage or QDBusMessage::ErrorMessage.

    See the QDBusInterface::call function for a more friendly way of placing calls.

    \warning This function reenters the Qt event loop in order to wait for the reply, excluding user
             input. During the wait, it may deliver signals and other method calls to your
             application. Therefore, it must be prepared to handle a reentrancy whenever a call is
             placed with sendWithReply().
*/
QDBusMessage QDBusConnection::sendWithReply(const QDBusMessage &message) const
{
    if (!d || !d->connection)
        return QDBusMessage::fromDBusMessage(0, *this);

    if (!QCoreApplication::instance()) {
        DBusMessage *msg = message.toDBusMessage();
        if (!msg)
            return QDBusMessage::fromDBusMessage(0, *this);

        DBusMessage *reply = dbus_connection_send_with_reply_and_block(d->connection, msg,
                                                                       -1, &d->error);
        d->handleError();
        dbus_message_unref(msg);

        if (lastError().isValid())
            return QDBusMessage::fromError(lastError());

        return QDBusMessage::fromDBusMessage(reply, *this);
    } else {
        QDBusReplyWaiter waiter;
        if (d->sendWithReplyAsync(message, &waiter, SLOT(reply(const QDBusMessage&))) > 0) {
            // enter the event loop and wait for a reply
            waiter.exec(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents);

            d->lastError = waiter.replyMsg; // set or clear error
            return waiter.replyMsg;
        }

        return QDBusMessage::fromDBusMessage(0, *this);
    }
}

/*!
    Connects the signal to the slot \p slot in object \p receiver.

    \param service      the service that will emit the signal, or QString() to wait for the signal
                        coming from any remote application
    \param path         the path that will emit the signal, or QString() to wait for the signal
                        coming from any object path (usually associated with an empty \p service)
    \param interface    the name of the interface to for this signal
    \param name         the name of the signal
    \param receiver     the object to connect to
    \param slot         the slot that will be invoked when the signal is emitted
    \returns            true if the connection was successful

    \note The signal will only be delivered to the slot if the parameters match. This verification
          can be done only when the signal is received, not at connection time.

    \bug does not allow an empty service
*/
bool QDBusConnection::connect(const QString &service, const QString &path, const QString& interface,
                              const QString &name, QObject *receiver, const char *slot)
{
    return connect(service, path, interface, name, QString(), receiver, slot);
}

/*!
    \overload
    Connects the signal to the slot \p slot in object \p receiver. Unlike the other
    QDBusConnection::connect overload, this function allows one to specify the parameter signature
    to be connected. The function will then verify that this signature can be delivered to the slot
    specified by \p slot and return false otherwise.

    \bug does not validate signature vs slot yet
*/
bool QDBusConnection::connect(const QString &service, const QString &path, const QString& interface,
                              const QString &name, const QString &signature,
                              QObject *receiver, const char *slot)
{
    if (!receiver || !slot || !d || !d->connection || !QDBusUtil::isValidInterfaceName(interface))
        return false;

    QString source;
    if (!service.isEmpty()) {
        source = getNameOwner(service);
        if (source.isEmpty())
            return false;
    }
    source += path;

    // check the slot
    QDBusConnectionPrivate::SignalHook hook;
    if ((hook.midx = QDBusConnectionPrivate::findSlot(receiver, slot + 1, hook.params)) == -1)
        return false;

    hook.interface = interface;
    hook.name = name;
    hook.signature = signature;
    hook.obj = receiver;

    // avoid duplicating:
    QDBusConnectionPrivate::SignalHookHash::ConstIterator it = d->signalHooks.find(source);
    for ( ; it != d->signalHooks.end() && it.key() == source; ++it) {
        const QDBusConnectionPrivate::SignalHook &entry = it.value();
        if (entry.interface == hook.interface &&
            entry.name == hook.name &&
            entry.signature == hook.signature &&
            entry.obj == hook.obj &&
            entry.midx == hook.midx) {
            // no need to compare the parameters if it's the same slot
            return true;        // already there
        }
    }


    d->connectSignal(source, hook);
    return true;
}

/*!
    Registers the object \p object at path \p path and returns true if the registration was
    successful.

    This function does not replace existing objects: if there is already an object registered at
    path \p path, this function will return false. Use unregisterObject() to unregister it first.

    You cannot register an object as a child object of an object that was registered with
    QDBusConnection::ExportChildObjects.
*/
bool QDBusConnection::registerObject(const QString &path, QObject *object, RegisterOptions options)
{
    if (!d || !d->connection || !object || !options || !QDBusUtil::isValidObjectPath(path))
        return false;

    if (options & ExportSignals) {
        qWarning("Cannot export signals from objects. Use an adaptor for that purpose.");
        return false;
    }

    QStringList pathComponents = path.split(QLatin1Char('/'));
    if (pathComponents.last().isEmpty())
        pathComponents.removeLast();
    QWriteLocker locker(&d->lock);

    // lower-bound search for where this object should enter in the tree
    QDBusConnectionPrivate::ObjectTreeNode *node = &d->rootNode;
    int i = 1;
    while (node) {
        if (pathComponents.count() == i) {
            // this node exists
            // consider it free if there's no object here and the user is not trying to
            // replace the object sub-tree
            if ((options & ExportChildObjects && !node->children.isEmpty()) || node->obj)
                return false;

            // we can add the object here
            node->obj = object;
            node->flags = options;

            d->registerObject(node);
            qDebug("REGISTERED FOR %s", path.toLocal8Bit().constData());
            return true;
        }

        // find the position where we'd insert the node
        QVector<QDBusConnectionPrivate::ObjectTreeNode::Data>::Iterator it =
            qLowerBound(node->children.begin(), node->children.end(), pathComponents.at(i));
        if (it != node->children.constEnd() && it->name == pathComponents.at(i)) {
            // match: this node exists
            node = it->node;

            // are we allowed to go deeper?
            if (node->flags & ExportChildObjects) {
                // we're not
                qDebug("Cannot register object at %s because %s exports its own child objects",
                       qPrintable(path), qPrintable(pathComponents.at(i)));
                return false;
            }
        } else {
            // add entry
            QDBusConnectionPrivate::ObjectTreeNode::Data entry;
            entry.name = pathComponents.at(i);
            entry.node = new QDBusConnectionPrivate::ObjectTreeNode;
            node->children.insert(it, entry);

            node = entry.node;
        }

        // iterate
        ++i;
    }

    Q_ASSERT_X(false, "QDBusConnection::registerObject", "The impossible happened");
    return false;
}

/*!
    Unregisters an object that was registered with the registerObject() function and, if \p mode is
    QDBusConnection::UnregisterTree, all of its sub-objects too.

    Note that you cannot unregister objects that were not registered with registerObject().
*/
void QDBusConnection::unregisterObject(const QString &path, UnregisterMode mode)
{
    if (!d || !d->connection || !QDBusUtil::isValidObjectPath(path))
        return;

    QStringList pathComponents = path.split(QLatin1Char('/'));
    QWriteLocker locker(&d->lock);
    QDBusConnectionPrivate::ObjectTreeNode *node = &d->rootNode;
    int i = 1;

    // find the object
    while (node) {
        if (pathComponents.count() == i) {
            // found it
            node->obj = 0;
            node->flags = 0;

            if (mode == UnregisterTree) {
                // clear the sub-tree as well
                node->clear();  // can't disconnect the objects because we really don't know if they can
                                // be found somewhere else in the path too
            }

            return;
        }

        QVector<QDBusConnectionPrivate::ObjectTreeNode::Data>::ConstIterator it =
            qLowerBound(node->children.constBegin(), node->children.constEnd(), pathComponents.at(i));
        if (it == node->children.constEnd() || it->name != pathComponents.at(i))
            break;              // node not found

        node = it->node;
        ++i;
    }
}

/*!
    Returns a QDBusInterface associated with the interface \p interface on object at path \p path on
    service \p service.
*/
QDBusInterface QDBusConnection::findInterface(const QString& service, const QString& path,
                                              const QString& interface)
{
    // create one
    QDBusInterfacePrivate *priv = new QDBusInterfacePrivate(*this);

    if (!(interface.isEmpty() || QDBusUtil::isValidInterfaceName(interface)) ||
        !QDBusUtil::isValidObjectPath(path))
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

/*!
    \fn QDBusConnection::findInterface(const QString &service, const QString &path)
    Returns an interface of type \p Interface associated with the object on path \p path at service
    \p service.

    \p Interface must be a class derived from QDBusInterface.
*/

/*!
    Returns a QDBusObject associated with the object on path \p path at service \p service.
*/
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


/*!
    Returns true if this QDBusConnection object is connected.

    \note If it isn't connected, calling QDBusConnection::addConnection on the same connection name
    will not make be connected. You need to call the QDBusConnection constructor again.
*/
bool QDBusConnection::isConnected( ) const
{
    return d && d->connection && dbus_connection_get_is_connected(d->connection);
}

/*!
    Returns the last error that happened in this connection.

    This function is provided for low-level code. If you're using QDBusInterface::call, error codes are
    reported by its return value.

    \sa QDBusInterface, QDBusMessage
*/
QDBusError QDBusConnection::lastError() const
{
    return d ? d->lastError : QDBusError();
}

/*!
    Returns the unique connection name for this connection, if this QDBusConnection object is
    connected, or an empty QString otherwise.

    A Unique Connection Name is a string in the form ":x.xxx" (where x are decimal digits) that is
    assigned by the D-Bus server daemon upon connection. It uniquely identifies this client in the
    bus.

    This function returns an empty QString for peer-to-peer connections.
*/
QString QDBusConnection::baseService() const
{
    return d && d->connection ?
            QString::fromUtf8(dbus_bus_get_unique_name(d->connection))
            : QString();
}

/*!
    Sends a request to the D-Bus server daemon to request the service name \p name. The flags \p
    mode indicate how to proceed if the name is already taken or when another D-Bus client requests
    the same name.

    Service names are used to publish well-known services on the D-Bus bus, by associating a
    friendly name to this connection. Other D-Bus clients will then be able to contact this
    connection and the objects registered on it by using this name instead of the unique connection
    name (see baseService()). This also allows one application to always have the same name, while
    its unique connection name changes.

    This function has no meaning in peer-to-peer connections.

    This function returns true if the name is assigned to this connection now (including the case
    when it was already assigned).

    \todo probably move to the QObject representing the bus
    \todo update the NameRequestMode flags
*/
bool QDBusConnection::requestName(const QString &name, NameRequestMode mode)
{
    static const int DBusModes[] = { DBUS_NAME_FLAG_ALLOW_REPLACEMENT, 0,
        DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_ALLOW_REPLACEMENT};

    int retval = dbus_bus_request_name(d->connection, name.toUtf8(), DBusModes[mode], &d->error);
    d->handleError();
    return retval == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ||
        retval == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER;
}

/*!
    Releases a name that had been requested using requestName(). This function returns true if the
    name has been released, false otherwise.

    This function has no meaning in peer-to-peer connections.

    You cannot cause a name owned by another application to be released using releaseName(). Use
    requestName() instead to assign it to your application.

    \todo probably move to the QObject representing the bus
*/
bool QDBusConnection::releaseName(const QString &name)
{
    int retval = dbus_bus_release_name(d->connection, name.toUtf8(), &d->error);
    d->handleError();
    if (lastError().isValid())
        return false;
    return retval == DBUS_RELEASE_NAME_REPLY_RELEASED;
}

/*!
    Returns the unique connection name of the client that currently has the \p name
    requested. Returns an empty QString in case there is no such name on the bus or if \p name is
    not a well-formed bus name.

    \todo probably move to the QObject representing the bus
*/
QString QDBusConnection::getNameOwner(const QString& name)
{
    if (QDBusUtil::isValidUniqueConnectionName(name))
        return name;
    if (!d || !QDBusUtil::isValidBusName(name))
        return QString();

    QDBusMessage msg = QDBusMessage::methodCall(QLatin1String(DBUS_SERVICE_DBUS),
            QLatin1String(DBUS_PATH_DBUS), QLatin1String(DBUS_INTERFACE_DBUS),
            QLatin1String("GetNameOwner"));
    msg << name;
    QDBusMessage reply = sendWithReply(msg);
    if (!lastError().isValid() && reply.type() == QDBusMessage::ReplyMessage)
        return reply.first().toString();
    return QString();
}

/*!
    \internal
*/
template<int type>
struct DefaultBus
{
    DefaultBus()
    {
        QDBusConnection con = QDBusConnection::addConnection(QDBusConnection::BusType(type),
               QLatin1String(busName));
        bus = new QDBusConnection(con);
        qAddPostRoutine(clear);
    }

    ~DefaultBus()
    {
        delete bus;
    }

    static void clear()
    {
        delete bus;
        bus = 0;
        QDBusConnection::closeConnection(QLatin1String(busName));
    }

    static QDBusConnection *bus;
    static const char busName[];
};

Q_GLOBAL_STATIC(DefaultBus<QDBusConnection::SessionBus>, sessionBusPtr);
Q_GLOBAL_STATIC(DefaultBus<QDBusConnection::SystemBus>, systemBusPtr);

template<>
QT_STATIC_CONST_IMPL char DefaultBus<QDBusConnection::SessionBus>::busName[] = "qt_default_session_bus";
template<>
QT_STATIC_CONST_IMPL char DefaultBus<QDBusConnection::SystemBus>::busName[] = "qt_default_system_bus";

template<> QDBusConnection *DefaultBus<QDBusConnection::SessionBus>::bus = 0;
template<> QDBusConnection *DefaultBus<QDBusConnection::SystemBus>::bus = 0;

namespace QDBus {
    QDBusConnection &sessionBus()
    {
        return *sessionBusPtr()->bus;
    }

    QDBusConnection &systemBus()
    {
        return *systemBusPtr()->bus;
    }
}

