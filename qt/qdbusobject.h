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
class QDBUS_EXPORT QDBusObject
{
    friend class QDBusConnection;
public:
    // public constructors
    QDBusObject(const QDBusObject& other);
    QDBusObject(const QDBusInterface& iface);

    // public destructors
    ~QDBusObject();

public:
    // public functions
    QDBusObject& operator=(const QDBusObject&);

    QDBusConnection connection() const;
    QString service() const;
    QString path() const;

    QString introspect() const;
    QSharedDataPointer<QDBusIntrospection::Object> introspectionData() const;

    QStringList interfaces() const;
    QMap<QString, QDBusObject> children() const;

    //bool exists() const;
    bool isValid() const;

#ifndef QT_NO_MEMBER_TEMPLATES
    template<typename Interface>
        inline operator Interface()
    { return qdbus_cast<Interface>(*this); }

    template<typename Interface>
        inline operator const Interface() const
    { return qdbus_cast<Interface>(*this); }
#endif
    
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
