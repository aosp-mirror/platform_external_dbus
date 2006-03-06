/* -*- C++ -*-
 *
 * Copyright (C) 2005 Thiago Macieira <thiago@kde.org>
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "qdbusstandardinterfaces.h"

/*!
    \page StandardInterfaces Standard D-Bus Interfaces

    The standard, well-known interfaces provided by D-Bus are as follows:
    \value org.freedesktop.DBus.Peer            Peer detection
    \value org.freedesktop.DBus.Introspectable  Introspection of remote object's contents
    \value org.freedesktop.DBus.Properties      Access to remote object's properties

    The QtDBus implementation provides easy access to those three interfaces with the
    QDBusPeerInterface, QDBusIntrospectableInterface and QDBusPropertiesInterface classes. As a
    convenience form, they can also be accessed by the classes org::freedesktop::DBus::Peer,
    org::freedesktop::DBus::Introspectable and org::freedesktop::DBus::Properties.

    Those three classes also illustrate code-generation by the \ref dbusidl2cpp tool: the methods
    defined in those three interfaces are provided as member functions in the QtDBus classes, which
    are capable of type-checking the parameters at compile-time, in order to guarantee that they
    conform to the types expected by the remote objects.
*/

/*!
    \typedef org::freedesktop::DBus::Peer
*/

/*!
    \typedef org::freedesktop::DBus::Introspectable
*/

/*!
    \typedef org::freedesktop::DBus::Properties
*/

/*!
    \class QDBusPeerInterface
    \brief Provides access to the \a org.freedesktop.DBus.Peer interface.

    This interface has only one method: ping(). Calling this method will generate a success reply if
    the target service exists or a failure if it doesn't. The target object path is irrelevant in
    this case.
*/

/*!
    \fn QDBusPeerInterface::staticInterfaceName
    Returns the interface name: "org.freedesktop.DBus.Peer"
*/

/*!
    \fn QDBusPeerInterface::staticIntrospectionData
    Returns the XML fragment corresponding to this interface's definition.
*/

/*!
    \fn QDBusPeerInterface::QDBusPeerInterface(const QDBusObject &)
    Creates a QDBusPeerInterface object accessing interface the \a org.freedesktop.DBus.Peer
    interface on object \p obj.
*/

/*!
    \fn QDBusPeerInterface::introspectionData() const
    Returns the XML fragment corresponding to this interface's definition.
*/

/*!
    \fn QDBusPeerInterface::ping
    Emits an \a org.freedesktop.DBus.Peer.Ping call to the remote object.
*/

/*!
    Destroys this object.
*/
QDBusPeerInterface::~QDBusPeerInterface()
{
}

/*!
    \class QDBusIntrospectableInterface
     \brief  Provides access to the \a org.freedesktop.DBus.Introspectable interface.

    The \a Introspectable interface is used to obtain information about the remote object's
    internals. Its one method, \a introspect(), returns a XML document describing the interfaces and
    child objects of a remote object in the D-Bus bus.

    The QtDBus implementation automatically introspects remote objects in order to construct the
    introspection structures found in QDBusIntrospection and QDBusInterface.

    \sa QDBusInterface, QDBusIntrospection, QDBusObject::interfaces, QDBusObject::childObjects,
        QDBusObject::introspect
*/

/*!
    \fn QDBusIntrospectableInterface::staticInterfaceName
    Returns the interface name: "org.freedesktop.DBus.Introspection"
*/

/*!
    \fn QDBusIntrospectableInterface::staticIntrospectionData
    Returns the XML fragment corresponding to this interface's definition.
*/

/*!
    \fn QDBusIntrospectableInterface::QDBusIntrospectableInterface(const QDBusObject &)
    Creates a QDBusIntrospectableInterface object accessing interface the \a
    org.freedesktop.DBus.Introspectable interface on object \p obj.
*/

/*!
    \fn QDBusIntrospectableInterface::introspectionData() const
    Returns the XML fragment corresponding to this interface's definition.
*/

/*!
    \fn QDBusIntrospectableInterface::introspect
    Places an \a org.freedesktop.DBus.Introspectable.Introspect call to the remote object and
    return the XML result.
*/

/*!
    Destroys the object.
*/
QDBusIntrospectableInterface::~QDBusIntrospectableInterface()
{
}

/*!
    \class QDBusPropertiesInterface
    \brief Provides access to the \a org.freedesktop.DBus.Properties interface

    D-Bus interfaces can export properties, much like the ones used in QObject. In order to access
    those properties, two methods are defined in the \a org.freedesktop.DBus.Properties interface:
    get() and set(), which are similar in functionality to QObject::property and
    QObject::setProperty.
*/

/*!
    \fn QDBusPropertiesInterface::staticInterfaceName
    Returns the interface name: "org.freedesktop.DBus.Properties"
*/

/*!
    \fn QDBusPropertiesInterface::staticIntrospectionData
    Returns the XML fragment corresponding to this interface's definition.
*/

/*!
    \fn QDBusPropertiesInterface::QDBusPropertiesInterface(const QDBusObject &)
    Creates a QDBusPropertiesInterface object accessing interface the \a
    org.freedesktop.DBus.Properties interface on object \p obj.
*/

/*!
    \fn QDBusPropertiesInterface::introspectionData() const
    Returns the XML fragment corresponding to this interface's definition.
*/

/*!
    \fn QDBusPropertiesInterface::set(const QString &interfaceName, const QString &propertyName,
                                      const QDBusVariant &value)
    Sets the property named \a propertyName in interface \a interfaceName in the remote object this
    QDBusPropertiesInterface object points to to the value specified by \a value. This function is
    analogous to QObject::setProperty.

    If the type of the \a value parameter is not what the remote interface declared, the result is
    undefined. See QDBusInterface::properties for information on remote properties.

    \sa QDBusInterface::setProperty
*/

/*!
    \fn QDBusPropertiesInterface::get(const QString &interfaceName, const QString &propertyName)
    Retrieves the value of property named \a propertyName in interface \a interfaceName in the
    remote object this QDBusPropertiesInterface object points to. This function is analogous to
    QObject::property.

    \sa QDBusInterface::property
*/

/*!
    Destroys the object.
*/
QDBusPropertiesInterface::~QDBusPropertiesInterface()
{
}

#if 0
/*!
    \class QDBusBusInterface
    \internal
    \brief Provides access to the \a org.freedesktop.DBus interface found in the D-Bus server
    daemon.

    The org.freedesktop.DBus interface is found only in the D-Bus daemon server. It is used to
    communicate with it and to request information about the bus itself and other applications in
    it.

    Normally, you don't need to use this interface in your application. Instead, use the methods in
    QDBusConnection.

    \sa QDBusConnection
*/

QDBusBusInterface::~QDBusBusInterface()
{
}

const char* QDBusBusInterface::staticIntrospectionData()
{
    // FIXME!
    // This should be auto-generated!

    return
        "<interface name=\"org.freedesktop.DBus\">"
        "<method name=\"RequestName\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"in\" type=\"u\"/>"
        "<arg direction=\"out\" type=\"u\"/>"
        "</method>"
        "<method name=\"ReleaseName\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"u\"/>"
        "</method>"
        "<method name=\"StartServiceByName\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"in\" type=\"u\"/>"
        "<arg direction=\"out\" type=\"u\"/>"
        "</method>"
        "<method name=\"Hello\">"
        "<arg direction=\"out\" type=\"s\"/>"
        "</method>"
        "<method name=\"NameHasOwner\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"b\"/>"
        "</method>"
        "<method name=\"ListNames\">"
        "<arg direction=\"out\" type=\"as\"/>"
        "</method>"
        "<method name=\"AddMatch\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "</method>"
        "<method name=\"RemoveMatch\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "</method>"
        "<method name=\"GetNameOwner\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"s\"/>"
        "</method>"
        "<method name=\"ListQueuedOwners\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"as\"/>"
        "</method>"
        "<method name=\"GetConnectionUnixUser\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"u\"/>"
        "</method>"
        "<method name=\"GetConnectionUnixProcessID\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"u\"/>"
        "</method>"
        "<method name=\"GetConnectionSELinuxSecurityContext\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"ay\"/>"
        "</method>"
        "<method name=\"ReloadConfig\">"
        "</method>"
        "<signal name=\"NameOwnerChanged\">"
        "<arg type=\"s\"/>"
        "<arg type=\"s\"/>"
        "<arg type=\"s\"/>"
        "</signal>"
        "<signal name=\"NameLost\">"
        "<arg type=\"s\"/>"
        "</signal>"
        "<signal name=\"NameAcquired\">"
        "<arg type=\"s\"/>"
        "</signal>"
        "</interface>";
}
#endif
