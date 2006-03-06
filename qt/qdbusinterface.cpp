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

#include "qdbusinterface.h"
#include "qdbusobject.h"
#include "qdbusstandardinterfaces.h"

#include "qdbusinterface_p.h"

/*!
    \internal
*/
struct EmptyInterfaceInitializer
{
    QDBusIntrospection::Interface *data;
    EmptyInterfaceInitializer()
    {
        data = new QDBusIntrospection::Interface;
        data->ref = 1;
        data->introspection = QLatin1String("");
    }

    ~EmptyInterfaceInitializer()
    {
        Q_ASSERT(data->ref == 1);
        delete data;
        data = 0;
    }
};

Q_GLOBAL_STATIC(EmptyInterfaceInitializer, emptyDataInit);

const QDBusIntrospection::Interface*
QDBusInterfacePrivate::emptyData()
{
    return emptyDataInit()->data;
}

/*!
    \class QDBusInterface
    \brief Base class for all D-Bus interfaces in the QtDBus binding, allowing access to remote interfaces.

    QDBusInterface is a generic accessor class that is used to place calls to remote objects,
    connect to signals exported by remote objects and get/set the value of remote properties. This
    class is useful for dynamic access to remote objects: that is, when you do not have a generated
    code that represents the remote interface.

    Generated-code classes also derive from QDBusInterface, all methods described here are also
    valid for generated-code classes. In addition to those described here, generated-code classes
    provide member functions for the remote methods, which allow for compile-time checking of the
    correct parameters and return values, as well as property type-matching and signal
    parameter-matching.

    Calls are usually placed by using the call() function, which constructs the message, sends it
    over the bus, waits for the reply and decodes the reply. Signals are connected to by using the
    connect() family of functions, whose behavior is similar to QObject::connect(). Finally,
    properties are accessed using the property() and setProperty() functions, whose behaviour is
    also similar to QObject::property() and QObject::setProperty().

    \sa \ref StandardInterfaces, \ref dbusidl2cpp
*/

/*!
    \enum QDBusInterface::CallMode
    \todo turn this into flags and add UseEventLoop/NoUseEventLoop

    Specifies how a call should be placed. The valid options are:
    \value NoWaitForReply       place the call but don't wait for the reply (the reply's contents
                                will be discarded)
    \value WaitForReply         place the call and wait for the method to finish before returning
                                (the reply's contents will be returned)
    \value NoUseEventLoop       don't use an event loop to wait for a reply, but instead block on
                                network operations while waiting. This option means the
                                user-interface may not be updated for the duration of the call.
    \value UseEventLoop         use the Qt event loop to wait for a reply. This option means the
                                user-interface will update, but it also means other events may
                                happen, like signal delivery and other D-Bus method calls.

    When using UseEventLoop, applications must be prepared for reentrancy in any function.
*/

QDBusInterface::QDBusInterface(QDBusInterfacePrivate* p)
    : d(p)
{
    d->ref.ref();
}

/*!
    Constructs a QDBusInterface object by associating it with the interface \p name in the remote
    object \p obj.
*/
QDBusInterface::QDBusInterface(const QDBusObject& obj, const QString& name)
    : d(0)
{
    *this = obj.connection().findInterface(obj.service(), obj.path(), name);
}

/*!
    Constructs a copy QDBusInterface object.
*/
QDBusInterface::QDBusInterface(const QDBusInterface &other)
    : d(0)
{
    *this = other;
}

/*!
    Releases this object's resources.
*/
QDBusInterface::~QDBusInterface()
{
    if (!d->ref.deref())
        delete d;
}

/*!
    Constructs a copy QDBusInterface object.
*/
QDBusInterface& QDBusInterface::operator=(const QDBusInterface& other)
{
    other.d->ref.ref();
    QDBusInterfacePrivate* old = qAtomicSetPtr(&d, other.d);
    if (old && !old->ref.deref())
        delete old;

    return *this;
}

/*!
    \fn QDBusInterface::object()
    Returns the object associated with this interface.
*/

/*!
    \fn QDBusInterface::object() const
    \overload
    Returns the object associated with this interface.
*/

/*!
    \fn "QDBusInterface::operator QDBusObject"
    \overload
    Returns the object associated with this interface.
*/

/*!
    \fn "QDBusInterface::operator const QDBusObject"
    \overload
    Returns the object associated with this interface.
*/

/*!
    Returns the connection this interface is assocated with.
*/
QDBusConnection QDBusInterface::connection() const
{
    return d->conn;
}

/*!
    Returns the name of the service this interface is associated with.
*/
QString QDBusInterface::service() const
{
    return d->service;
}

/*!
    Returns the object path that this interface is associated with.
*/
QString QDBusInterface::path() const
{
    return d->path;
}

/*!
    Returns the name of this interface.
*/
QString QDBusInterface::interface() const
{
    return d->data->name;
}

/*!
    Returns the XML document fragment that describes the introspection of this interface. This is
    the raw XML form of the structures returned by interfaceData().
 */
QString QDBusInterface::introspectionData() const
{
    d->introspect();
    return d->data->introspection;
}

/*!
    Returns the interface data for this interface. This is the parsed form of the XML introspection
    data, as returned by introspectionData().
 */
const QDBusIntrospection::Interface& QDBusInterface::interfaceData() const
{
    d->introspect();
    return *d->data;
}

/*!
    Returns the annotations present in this interface, if any.
    This information can also be found in the data returned by interfaceData().
*/
const QDBusIntrospection::Annotations& QDBusInterface::annotationData() const
{
    d->introspect();
    return d->data->annotations;
}

/*!
    Returns a map of all the methods found in this interface.
    This information can also be found in the data returned by interfaceData().
*/
const QDBusIntrospection::Methods& QDBusInterface::methodData() const
{
    d->introspect();
    return d->data->methods;
}

/*!
    Returns a map of all the signals found in this interface.
    This information can also be found in the data returned by interfaceData().
*/
const QDBusIntrospection::Signals& QDBusInterface::signalData() const
{
    d->introspect();
    return d->data->signals_;
}

/*!
    Returns a map of all the properties found in this interface.
    This information can also be found in the data returned by interfaceData().
*/
const QDBusIntrospection::Properties& QDBusInterface::propertyData() const
{
    d->introspect();
    return d->data->properties;
}

/*!
    Places a call to the remote method specified by \p method on this interface, using \p a_args as
    arguments.

    Normally, you should place calls using call().
*/
QDBusMessage QDBusInterface::callWithArgs(const QDBusIntrospection::Method& method,
                                          const QList<QVariant>& a_args,
                                          CallMode mode)
{
    QString signature = QLatin1String("");      // empty, not null
    QVariantList args = a_args;

    if (!method.inputArgs.isEmpty())
    {
        // go over the list of parameters for the method
        QDBusIntrospection::Arguments::const_iterator it = method.inputArgs.begin(),
                                                     end = method.inputArgs.end();
        int arg;
        for (arg = 0; it != end; ++it, ++arg)
        {
            // find the marshalled name for this type
            QString typeSig = QLatin1String(it->type.dbusSignature());
            signature += typeSig;
        }
    }
    else
        args.clear();

    if (method.annotations.value(QLatin1String(ANNOTATION_NO_WAIT)) == QLatin1String("true"))
        mode = NoWaitForReply;

    return callWithArgs(method.name, signature, args, mode);
}

/*!
    \overload
    Places a call to the remote method specified by \p method on this interface, using \p args as
    arguments.

    Normally, you should place calls using call().
*/
QDBusMessage QDBusInterface::callWithArgs(const QString& method, const QList<QVariant>& args,
                                          CallMode mode)
{
    QString m = method, sig;
    // split out the signature from the method
    int pos = method.indexOf(QLatin1Char('.'));
    if (pos != -1) {
        m.truncate(pos);
        sig = method.mid(pos + 1);
    }
    return callWithArgs(m, sig, args, mode);
}

/*!
    \overload
    Places a call to the remote method specified by \p method on this interface, using \p args as
    arguments. The \p signature parameter specifies how the arguments should be marshalled over the
    connection. (It also serves to distinguish between overloading of remote methods by name)

    Normally, you should place calls using call().
*/
QDBusMessage QDBusInterface::callWithArgs(const QString& method, const QString& signature,
                                          const QList<QVariant>& args, CallMode mode)
{
    QDBusMessage msg = QDBusMessage::methodCall(service(), path(), interface(), method);
    msg.setSignature(signature);
    msg.QList<QVariant>::operator=(args);

    QDBusMessage reply;
    if (mode == WaitForReply)
        reply = d->conn.sendWithReply(msg);
    else
        d->conn.send(msg);

    d->lastError = reply;       // will clear if reply isn't an error

    // ensure that there is at least one element
    if (reply.isEmpty())
        reply << QVariant();

    return reply;
}

/*!
    Connects the D-Bus signal specified by \p sig to the given slot \p slot in the object \p obj.

    This function is similar to QObject::connect.
*/
bool QDBusInterface::connect(const QDBusIntrospection::Signal& sig, QObject* obj, const char *slot)
{
    QString signature = QLatin1String("");      // empty, not null

    if (!sig.outputArgs.isEmpty())
    {
        // go over the list of parameters for the method
        QDBusIntrospection::Arguments::const_iterator it = sig.outputArgs.begin(),
                                                     end = sig.outputArgs.end();
        int arg;
        for (arg = 0; it != end; ++it, ++arg)
        {
            // find the marshalled name for this type
            QString typeSig = QLatin1String(it->type.dbusSignature());
            signature += typeSig;
        }
    }

    return connect(sig.name, signature, obj, slot);
}

/*!
    \overload
    Connects the D-Bus signal specified by \p signalName to the given slot \p slot in the object \p
    obj.

    This function is similar to QObject::connect.
*/
bool QDBusInterface::connect(const QString& signalName, QObject* obj, const char *slot)
{
    QString s = signalName, sig;
    // split out the signature from the name
    int pos = signalName.indexOf(QLatin1Char('.'));
    if (pos != -1) {
        s.truncate(pos);
        sig = QLatin1String("") + signalName.mid(pos + 1);
    }
    return connect(s, sig, obj, slot);
}

/*!
    \overload
    Connects the D-Bus signal specified by \p signalName to the given slot \p slot in the object \p
    obj. The \p signature parameter allows one to connect to the signal only if it is emitted with
    the parameters matching the given type signature.

    This function is similar to QObject::connect.
*/
bool QDBusInterface::connect(const QString& signalName, const QString& signature,
                             QObject* obj, const char *slot)
{
    return d->conn.connect(service(), path(), interface(), signalName, signature, obj, slot);
}

/*!
    Retrieves the value of the property \p prop in the remote object. This function returns an error
    if you try to read the value of a write-only property.
*/
QDBusReply<QDBusVariant> QDBusInterface::property(const QDBusIntrospection::Property& prop)
{
    // sanity checking
    if (prop.access == QDBusIntrospection::Property::Write)
        // write-only prop
        return QDBusError(QLatin1String(DBUS_ERROR_ACCESS_DENIED),
                     QString::fromLatin1("Property %1 in interface %2 in object %3 is write-only")
                     .arg(prop.name, interface(), path()));

    QDBusPropertiesInterface pi(object());
    return pi.get(interface(), prop.name);
}

/*!
    \overload
    Retrieves the value of the property \p propname in the remote object. This function returns an
    error if you try to read the value of a write-only property.
*/
QDBusReply<QDBusVariant> QDBusInterface::property(const QString& propName)
{
    // can't do sanity checking
    QDBusPropertiesInterface pi(object());
    return pi.get(interface(), propName);
}

/*!
    Sets the value of the property \p prop to \p newValue in the remote object. This function
    automatically changes the type of \p newValue to the property's type, but the call will fail if
    the types don't match.

    This function returns an error if the property is read-only.
*/
QDBusReply<void> QDBusInterface::setProperty(const QDBusIntrospection::Property& prop,
                                             const QDBusVariant &newValue)
{
    // sanity checking
    if (prop.access == QDBusIntrospection::Property::Read)
        // read-only prop
        return QDBusError(QLatin1String(DBUS_ERROR_ACCESS_DENIED),
                     QString::fromLatin1("Property %1 in interface %2 in object %3 is read-only")
                     .arg(prop.name, interface(), path()));

    // set the property type
    QDBusVariant value = newValue;
    value.type = prop.type;

    QDBusPropertiesInterface pi(object());
    return pi.set(interface(), prop.name, value);
}

/*!
    \overload
    Sets the value of the property \p propName to \p newValue in the remote object. This function
    will not change \p newValue's type to match the property, so it is your responsibility to make
    sure it is of the correct type.

    This function returns an error if the property is read-only.
*/
QDBusReply<void> QDBusInterface::setProperty(const QString& propName, const QDBusVariant &newValue)
{
    // can't do sanity checking
    QDBusPropertiesInterface pi(object());
    return pi.set(interface(), propName, newValue);
}

/*!
    \fn QDBusMessage QDBusInterface::call(const QDBusIntrospection::Method &method, ...)

    Calls the method \p method on this interface and passes the parameters to this function to the
    method.

    The parameters to \a call are passed on to the remote function via D-Bus as input
    arguments. Output arguments are returned in the QDBusMessage reply.

    \warning This function reenters the Qt event loop in order to wait for the reply, excluding user
             input. During the wait, it may deliver signals and other method calls to your
             application. Therefore, it must be prepared to handle a reentrancy whenever a call is
             placed with call().
*/

/*!
    \overload
    \fn QDBusMessage QDBusInterface::call(const QString &method, ...)

    Calls the method \p method on this interface and passes the parameters to this function to the
    method.

    The parameters to \a call are passed on to the remote function via D-Bus as input
    arguments. Output arguments are returned in the QDBusMessage reply.

    \warning This function reenters the Qt event loop in order to wait for the reply, excluding user
             input. During the wait, it may deliver signals and other method calls to your
             application. Therefore, it must be prepared to handle a reentrancy whenever a call is
             placed with call().
*/
