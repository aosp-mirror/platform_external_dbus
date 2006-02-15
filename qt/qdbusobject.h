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

#ifndef QDBUSOBJECT_H
#define QDBUSOBJECT_H

#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>
#include <QtCore/qlist.h>
#include <QtCore/qshareddata.h>

#include "qdbusconnection.h"
#include "qdbusintrospection.h"

class QDBusInterface;
class QDBusObject;

template<class Interface>
inline Interface qdbus_cast(QDBusObject& obj, Interface * = 0);

template<class Interface>
inline const Interface qdbus_cast(const QDBusObject& obj, Interface * = 0);

class QDBusObjectPrivate;
/**
 * QDBusObject
 * Base object for DBUS objects imported and exported.
 */
class QDBUS_EXPORT QDBusObject
{
    friend class QDBusConnection;
public:
    // public constructors
    /**
     * Construct a QDBusObject referencing the remote object given.
     */
    QDBusObject(const QDBusConnection& conn, const QString& service, const QString& path);    

    /**
     * Copy constructor.
     */
    QDBusObject(const QDBusObject& other);

    /**
     * Construct from an interface.
     */
    QDBusObject(const QDBusInterface& iface);

    // public destructors
    /**
     * Destructor.
     */
    ~QDBusObject();

public:
    // public functions

    /**
     * Assignment operator
     */
    QDBusObject& operator=(const QDBusObject&);

    /**
     * Returns the connection this object is bound to.
     */
    QDBusConnection connection() const;

    /**
     * Returns the service this object is associated to.
     */
    QString service() const;

    /**
     * Returns the path on the service this object is on.
     */
    QString path() const;

    /**
     * Returns the introspection XML data of this object node.
     */
    QString introspect() const;

    /**
     * Returns the introspection data for this object node.
     */
    QSharedDataPointer<QDBusIntrospection::Object> introspectionData() const;

    /**
     * Returns all the interfaces in this object.
     */
    QStringList interfaces() const;

    /**
     * Returns all the children object in this object.
     */
    QMap<QString, QDBusObject> children() const;

    /**
     * Returns true if the object being referenced exists.
     */
    //bool exists() const;

    /**
     * Returns true if we're referencing a valid object.
     */
    bool isValid() const;

    /**
     * Cast this object to an interface, if possible.
     */
    template<typename Interface>
        inline operator Interface()
    { return qdbus_cast<Interface>(*this); }

    /**
     * Cast this object to an interface, if possible.
     */
    template<typename Interface>
        inline operator const Interface() const
    { return qdbus_cast<Interface>(*this); }
    
private:
    QDBusObject(QDBusObjectPrivate*, const QDBusConnection& conn);
    QSharedDataPointer<QDBusObjectPrivate> d;
    QDBusConnection m_conn;
};

template<class Interface>
inline Interface qdbus_cast(QDBusObject& obj, Interface *)
{
    return Interface(obj);
}

template<class Interface>
inline const Interface qdbus_cast(const QDBusObject& obj, Interface *)
{
    return Interface(obj);
}

#endif // QDBUSOBJECT_H
