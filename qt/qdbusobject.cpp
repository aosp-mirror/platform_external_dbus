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

#include "qdbusmessage.h"
#include "qdbusconnection.h"
#include "qdbusobject.h"
#include "qdbusinterface.h"
#include "qdbusstandardinterfaces.h"
#include "qdbuserror.h"

#include "qdbusxmlparser_p.h"
#include "qdbusobject_p.h"
#include "qdbusutil.h"

/*!
    \class QDBusObject
    \brief Base object for referencing remote D-Bus Objects

    This class provides the basic functionality for referencing remote objects. It does not,
    however, allow you to place calls to the remote object: you have to use the QDBusInterface class
    for that.
*/

/*!
    \internal
*/
QDBusObject::QDBusObject(QDBusObjectPrivate* p, const QDBusConnection& conn)
    :d(p), m_conn(conn)
{
}
    
/*!
    Creates a QDBusObject that references the same object that the QDBusInterface class does.
*/
QDBusObject::QDBusObject(const QDBusInterface& iface)
    : m_conn(iface.connection())
{
    *this = m_conn.findObject(iface.service(), iface.path());
}

/*!
    Copy constructor: creates a copy of the \p other object.
*/
QDBusObject::QDBusObject(const QDBusObject& other)
    : d(other.d), m_conn(other.m_conn)
{
}

/*!
    Destroys this object and frees any resource it held.
*/
QDBusObject::~QDBusObject()
{
}

/*!
    Assignment operator: copy the contents of the \p other QDBusObject.
*/
QDBusObject& QDBusObject::operator=(const QDBusObject& other)
{
#if 0    
    if (other.d)
        other.d->ref.ref();
    
    QDBusObjectPrivate* old = qAtomicSetPtr(&d, other.d);
    if (old && !old->ref.deref())
        m_conn.d->disposeOf(d);
#endif
    d = other.d;

    return *this;
}

/*!
    Returns the connection this object is bound to.
*/
QDBusConnection QDBusObject::connection() const
{
    return m_conn;
}

/*!
    Returns the service this object is associated to.
    \sa connection
*/
QString QDBusObject::service() const
{
    return d ? d->data->service : QString();
}

/*!
    Returns the path on the remote service this object is on.
    \sa connection, service
*/
QString QDBusObject::path() const
{
    return d ? d->data->path : QString();
}

/*!
    Places an Introspect call to the remote object and return the XML data that describes its
    contents. This is the raw XML data of the structures introspectionData() returns.

    \bug We should not cache here. The remote object can change.
*/
QString QDBusObject::introspect() const
{
    if (!d)
        // not connected
        return QString();

    if (d->data->introspection.isNull()) {
        // Try to introspect
        QDBusIntrospectableInterface iface(*this);
        QDBusReply<QString> reply = iface.introspect();

        if (reply.isSuccess()) {
            // this will change the contents of d->data
            QDBusXmlParser::parse(d, reply);
        }
    }
    return d->data->introspection;
}

/*!
    Places an Introspect call to the remote object and return the parsed structures representing the
    object's interfaces and child objects. The raw XML data corresponding to this function's
    structures can be obtained using introspect().
*/
QSharedDataPointer<QDBusIntrospection::Object> QDBusObject::introspectionData() const
{
    QSharedDataPointer<QDBusIntrospection::Object> retval;
    if (d)
        retval = const_cast<QDBusIntrospection::Object*>(d->data);
    return retval;
}

/*!
    Returns a list of all the interfaces in this object. This is the same value as the found in the
    \ref QDBusIntrospection::Object::interfaces "interfaces" member of the value returned by
    introspectionData().
*/
QStringList QDBusObject::interfaces() const
{
    introspect();
    return d ? d->data->interfaces : QStringList();
}

/*!
    Returns a map of all the children object in this object along with pre-created QDBusObjects for
    referencing them.

    \todo Write this function!
*/
QMap<QString, QDBusObject> QDBusObject::children() const
{
    QMap<QString, QDBusObject> retval;
#if 0    
    if (!d)
        return retval;

    QString prefix = d->path;
    if (!prefix.endsWith('/'))
        prefix.append('/');
    foreach (QString sub, d->childObjects)
        retval.insert(sub, QDBusObject( m_conn.d->findObject(d->path, prefix + sub), m_conn ));

    return retval;
#endif
    qFatal("fixme!");
    return retval;
}

/*!
    Returns true if we're referencing a valid object service and path. This does not mean the object
    actually exists in the remote application or that the remote application exists.
*/
bool QDBusObject::isValid() const
{
    return d && m_conn.isConnected() && QDBusUtil::isValidBusName(d->data->service) &&
        QDBusUtil::isValidObjectPath(d->data->path);
}

#if 0                           // we don't have a way of determining if an object exists or not
/*!
    Returns true if the object being referenced exists.
*/
bool QDBusObject::exists() const
{
    if (!isValid())
        return false;

    // call a non-existant interface/method
    QDBusMessage msg = QDBusMessage::methodCall(d->service, d->path,
                                                "org.freedesktop.DBus.NonExistant", "NonExistant");
    QDBusMessage reply = m_conn.sendWithReply(msg);
    // ignore the reply

    QDBusError err = m_conn.lastError();
    if (!err.isValid()) {
        qWarning("D-Bus call to %s:%s on a supposedly non-existant interface worked!",
                 qPrintable(d->service), qPrintable(d->path));
        return true;
    }

    if (err.name == DBUS_ERROR_SERVICE_UNKNOWN ||
        err.name == DBUS_ERROR_BAD_ADDRESS)
    return !m_conn.lastError().isValid();
}
#endif

/*!
    \fn QDBusObject::operator Interface()
    Cast this object to an interface, if possible.
*/

/*!
    \fn QDBusObject::operator const Interface()
    Cast this object to an interface, if possible.
*/

/*!
    \fn qdbus_cast
    \relates QDBusObject

    Casts a QDBusObject to the QDBusInterface-derived class of type Interface.
*/
