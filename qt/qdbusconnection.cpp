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
#include <qstringlist.h>

#include "qdbusbus.h"
#include "qdbusconnection.h"
#include "qdbuserror.h"
#include "qdbusmessage.h"
#include "qdbusconnection_p.h"
#include "qdbusinterface_p.h"
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

QDBUS_EXPORT void qDBusBindToApplication();
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
    \relates QDBusConnection

    Returns a QDBusConnection object opened with the session bus. The object reference returned
    by this function is valid until the QCoreApplication's destructor is run, when the
    connection will be closed and the object, deleted.
*/
/*!
    \fn QDBusConnection QDBus::systemBus()
    \relates QDBusConnection

    Returns a QDBusConnection object opened with the system bus. The object reference returned
    by this function is valid until the QCoreApplication's destructor is run, when the
    connection will be closed and the object, deleted.
*/

/*!
    \class QDBusConnection
    \brief A connection to the D-Bus bus daemon.

    This class is the initial point in a D-Bus session. Using it, you can get access to remote
    objects, interfaces; connect remote signals to your object's slots; register objects, etc.

    D-Bus connections are created using the QDBusConnection::addConnection() function, which opens a
    connection to the server daemon and does the initial handshaking, associating that connection
    with a name. Further attempts to connect using the same name will return the same
    connection.

    The connection is then torn down using the QDBusConnection::closeConnection() function.

    As a convenience for the two most common connection types, the QDBus::sessionBus() and
    QDBus::systemBus() functions return open connections to the session server daemon and the system
    server daemon, respectively. Those connections are opened when first used and are closed when
    the QCoreApplication destructor is run.

    D-Bus also supports peer-to-peer connections, without the need for a bus server daemon. Using
    this facility, two applications can talk to each other and exchange messages. This can be
    achieved by passing an address to QDBusConnection::addConnection()
    function, which was opened by another D-Bus application using QDBusServer.
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
*/

/*!
    \enum QDBusConnection::WaitMode
    Specifies the call waiting mode.

    \value UseEventLoop         use the Qt Event Loop to wait for the reply
    \value NoUseEventLoop       don't use the event loop

    The \c UseEventLoop option allows for the application to continue to update its UI while the
    call is performed, but it also opens up the possibility for reentrancy: socket notifiers may
    fire, signals may be delivered and other D-Bus calls may be processed. The \c NoUseEventLoop
    does not use the event loop, thus being safe from those problems, but it may block the
    application for a noticeable period of time, in case the remote application fails to respond.

    Also note that calls that go back to the local application can only be placed in \c UseEventLoop
    mode.
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

    \value ExportAllSlots                       export all of this object's slots, including
                                                non-scriptable ones
    \value ExportAllSignals                     export all of this object's signals, including
                                                non-scriptable ones
    \value ExportAllProperties                  export all of this object's properties, including
                                                non-scriptable ones
    \value ExportAllContents                    export all of this object's slots, signals and
                                                properties, including non-scriptable ones

    \value ExportChildObjects                   export this object's child objects

    \warning It is currently not possible to export signals from objects. If you pass the flag
    ExportSignals or ExportAllSignals, the registerObject() function will print a warning.

    \sa registerObject(), QDBusAbstractAdaptor, {usingadaptors.html}{Using adaptors}
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
    Creates a QDBusConnection object attached to the connection with name \a name.

    This does not open the connection. You have to call QDBusConnection::addConnection to open it.
*/
QDBusConnection::QDBusConnection(const QString &name)
{
    d = manager()->connection(name);
    if (d)
        d->ref.ref();
}

/*!
    Creates a copy of the \a other connection.
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
    Creates a copy of the connection \a other in this object. The connection this object referenced
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
    Opens a connection of type \a type to one of the known busses and associate with it the
    connection name \a name. Returns a QDBusConnection object associated with that connection.
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

    QDBusConnection retval(name);

    // create the bus service
    QDBusAbstractInterfacePrivate *p;
    p = retval.findInterface_helper(QLatin1String(DBUS_SERVICE_DBUS),
                                    QLatin1String(DBUS_PATH_DBUS),
                                    QLatin1String(DBUS_INTERFACE_DBUS));
    if (p) {
        d->busService = new QDBusBusService(p);
        d->busService->setParent(d); // auto-deletion
        d->ref.deref();              // busService has a increased the refcounting to us
    }

    return retval;
}

/*!
    Opens a peer-to-peer connection on address \a address and associate with it the
    connection name \a name. Returns a QDBusConnection object associated with that connection.
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

    QDBusConnection retval(name);

    // create the bus service
    QDBusAbstractInterfacePrivate *p;
    p = retval.findInterface_helper(QLatin1String(DBUS_SERVICE_DBUS),
                                    QLatin1String(DBUS_PATH_DBUS),
                                    QLatin1String(DBUS_INTERFACE_DBUS));
    if (p) {
        d->busService = new QDBusBusService(p);
        d->busService->setParent(d); // auto-deletion
        d->ref.deref();              // busService has a increased the refcounting to us
    }

    return retval;
}

/*!
    Closes the connection of name \a name.

    Note that if there are still QDBusConnection objects associated with the same connection, the
    connection will not be closed until all references are dropped. However, no further references
    can be created using the QDBusConnection::QDBusConnection constructor.
*/
void QDBusConnection::closeConnection(const QString &name)
{
    manager()->removeConnection(name);
}

/*!
    Sends the \a message over this connection, without waiting for a reply. This is suitable for
    errors, signals, and return values as well as calls whose return values are not necessary.

    Returns true if the message was queued successfully, false otherwise.
*/
bool QDBusConnection::send(const QDBusMessage &message) const
{
    if (!d || !d->connection)
        return false;
    return d->send(message) != 0;
}

/*!
    Sends the \a message over this connection and returns immediately after queueing it. When the
    reply is received, the slot \a method is called in the object \a receiver. This function is
    suitable for method calls only.

    This function guarantees that the slot will be called exactly once with the reply, as long as
    the parameter types match. If they don't, the reply cannot be delivered.

    Returns the identification of the message that was sent or 0 if nothing was sent.
*/
int QDBusConnection::sendWithReplyAsync(const QDBusMessage &message, QObject *receiver,
        const char *method) const
{
    if (!d || !d->connection)
        return 0;

    return d->sendWithReplyAsync(message, receiver, method);
}

/*!
    Sends the \a message over this connection and blocks, waiting for a reply. This function is
    suitable for method calls only. It returns the reply message as its return value, which will be
    either of type QDBusMessage::ReplyMessage or QDBusMessage::ErrorMessage.

    See the QDBusInterface::call function for a more friendly way of placing calls.

    \warning If \a mode is \c UseEventLoop, this function will reenter the Qt event loop in order to
             wait for the reply. During the wait, it may deliver signals and other method calls to
             your application. Therefore, it must be prepared to handle a reentrancy whenever a call
             is placed with sendWithReply.
*/
QDBusMessage QDBusConnection::sendWithReply(const QDBusMessage &message, WaitMode mode) const
{
    if (!d || !d->connection)
        return QDBusMessage();
    return d->sendWithReply(message, mode);
}

/*!
    Connects the signal specified by the \a service, \a path, \a interface and \a name parameters to
    the slot \a slot in object \a receiver. The arguments \a service and \a path can be empty,
    denoting a connection to any signal of the \a interface - \a name pair, from any remote
    application.

    Returns true if the connection was successful.

    \warning The signal will only be delivered to the slot if the parameters match. This verification
             can be done only when the signal is received, not at connection time.
*/
bool QDBusConnection::connect(const QString &service, const QString &path, const QString& interface,
                              const QString &name, QObject *receiver, const char *slot)
{
    return connect(service, path, interface, name, QString(), receiver, slot);
}

/*!
    \overload
    Connects the signal to the slot \a slot in object \a receiver. Unlike the other
    QDBusConnection::connect overload, this function allows one to specify the parameter signature
    to be connected using the \a signature variable. The function will then verify that this
    signature can be delivered to the slot specified by \a slot and return false otherwise.
*/
bool QDBusConnection::connect(const QString &service, const QString &path, const QString& interface,
                              const QString &name, const QString &signature,
                              QObject *receiver, const char *slot)
{
    if (!receiver || !slot || !d || !d->connection || !QDBusUtil::isValidInterfaceName(interface))
        return false;

    QString source;
    if (!service.isEmpty()) {
        source = d->getNameOwner(service);
        if (source.isEmpty())
            return false;
    }

    // check the slot
    QDBusConnectionPrivate::SignalHook hook;
    QString key;
    hook.signature = signature;
    if (!d->prepareHook(hook, key, source, path, interface, name, receiver, slot, 0, false))
        return false;           // don't connect

    // avoid duplicating:
    QWriteLocker locker(&d->lock);
    QDBusConnectionPrivate::SignalHookHash::ConstIterator it = d->signalHooks.find(key);
    for ( ; it != d->signalHooks.end() && it.key() == key; ++it) {
        const QDBusConnectionPrivate::SignalHook &entry = it.value();
        if (entry.sender == hook.sender &&
            entry.path == hook.path &&
            entry.signature == hook.signature &&
            entry.obj == hook.obj &&
            entry.midx == hook.midx) {
            // no need to compare the parameters if it's the same slot
            return true;        // already there
        }
    }


    d->connectSignal(key, hook);
    return true;
}

/*!
    Registers the object \a object at path \a path and returns true if the registration was
    successful. The \a options parameter specifies how much of the object \a object will be exposed
    through D-Bus.

    This function does not replace existing objects: if there is already an object registered at
    path \a path, this function will return false. Use unregisterObject() to unregister it first.

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
    Unregisters an object that was registered with the registerObject() at the object path given by
    \a path and, if \a mode is QDBusConnection::UnregisterTree, all of its sub-objects too.

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
    Returns a dynamic QDBusInterface associated with the interface \a interface on object at path \a
    path on service \a service.

    This function creates a new object. It is your resposibility to ensure it is properly deleted
    (you can use all normal QObject deletion mechanisms, including the QObject::deleteLater() slot
    and QObject::setParent()).

    If the searching for this interface on the remote object failed, this function returns 0.
*/
QDBusInterface *QDBusConnection::findInterface(const QString& service, const QString& path,
                                               const QString& interface)
{
    if (!d)
        return 0;
    
    QDBusInterfacePrivate *p = d->findInterface(service, path, interface);
    QDBusInterface *retval = new QDBusInterface(p);
    retval->setParent(d);
    return retval;
}

/*!
    \fn QDBusConnection::findInterface(const QString &service, const QString &path)
    Returns an interface of type \c Interface associated with the object on path \a path at service
    \a service.

    \c Interface must be a class generated by \l {dbusidl2cpp.html}.

    This function creates a new object. It is your resposibility to ensure it is properly deleted
    (you can use all normal QObject deletion mechanisms, including the QObject::deleteLater() slot
    and QObject::setParent()).
*/

/*!
    Returns a QDBusBusService object that represents the D-Bus bus service on this connection.

    This function returns 0 for peer-to-peer connections.
*/
QDBusBusService *QDBusConnection::busService() const
{
    if (!d)
        return 0;
    return d->busService;
};

QDBusAbstractInterfacePrivate *
QDBusConnection::findInterface_helper(const QString &service, const QString &path,
                                      const QString &interface)
{
    if (!d)
        return 0;
    if (!interface.isEmpty() && !QDBusUtil::isValidInterfaceName(interface))
        return 0;
    
    QString owner;
    if (!service.isEmpty()) {
        if (!QDBusUtil::isValidObjectPath(path))
            return 0;

        // check if it's there first -- FIXME: add binding mode
        owner = d->getNameOwner(service);
        if (owner.isEmpty())
            return 0;
    } else if (!path.isEmpty())
        return 0;
    
    return new QDBusAbstractInterfacePrivate(*this, d, owner, path, interface);
}

/*!
    Returns true if this QDBusConnection object is connected.

    If it isn't connected, calling QDBusConnection::addConnection on the same connection name
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

Q_GLOBAL_STATIC(QMutex, defaultBussesMutex);
static const char sessionBusName[] = "qt_default_session_bus";
static const char systemBusName[] = "qt_default_system_bus";
static QDBusConnection *sessionBus = 0;
static QDBusConnection *systemBus = 0;

static void closeConnections()
{
    QMutexLocker locker(defaultBussesMutex());
    delete sessionBus;
    delete systemBus;
    QDBusConnection::closeConnection(QLatin1String(sessionBusName));
    QDBusConnection::closeConnection(QLatin1String(systemBusName));
    sessionBus = systemBus = 0;
}

static QDBusConnection *openConnection(QDBusConnection::BusType type)
{
    QMutexLocker locker(defaultBussesMutex());
    qAddPostRoutine(closeConnections);
    
    if (type == QDBusConnection::SystemBus) {
        if (systemBus)
            // maybe it got created before we locked the mutex
            return systemBus;
        systemBus = new QDBusConnection(QDBusConnection::addConnection(QDBusConnection::SystemBus,
                           QLatin1String(systemBusName)));
        return systemBus;
    } else {
        if (sessionBus)
            // maybe it got created before we locked the mutex
            return sessionBus;
        sessionBus = new QDBusConnection(QDBusConnection::addConnection(QDBusConnection::SessionBus,
                           QLatin1String(sessionBusName)));
        return sessionBus;
    }
}

namespace QDBus {
    QDBusConnection &sessionBus()
    {
        if (::sessionBus) return *::sessionBus;
        return *openConnection(QDBusConnection::SessionBus);
    }

    QDBusConnection &systemBus()
    {
        if (::systemBus) return *::systemBus;
        return *openConnection(QDBusConnection::SystemBus);
    }
}

