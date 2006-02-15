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

#ifndef QDBUS_STANDARD_INTERFACES_H
#define QDBUS_STANDARD_INTERFACES_H

#include "qdbusinterface.h"
#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>
#include <dbus/dbus.h>

class QDBusConnection;

class QDBUS_EXPORT QDBusPeerInterface: public QDBusInterface
{
public:
    static inline const char* staticInterfaceName()
    { return DBUS_INTERFACE_PEER; }

    static inline const char* staticIntrospectionData()
    {
        return
            "<interface name=\"org.freedesktop.DBus.Peer\">"
            "<method name=\"Ping\" />"
            "</interface>";
    }

public:
    explicit QDBusPeerInterface(const QDBusObject& obj)
        : QDBusInterface(obj, staticInterfaceName())
    { }

    QDBusPeerInterface(QDBusConnection& conn, const QString& service, const QString& path)
        : QDBusInterface(conn, service, path, staticInterfaceName())
    { }

    ~QDBusPeerInterface();

    inline virtual QString introspectionData() const
    { return staticIntrospectionData(); }

    inline void ping()
    { call(QLatin1String("Ping")); }
};

class QDBUS_EXPORT QDBusIntrospectableInterface: public QDBusInterface
{
public:
    static inline const char* staticInterfaceName()
    { return DBUS_INTERFACE_INTROSPECTABLE; }

    static inline const char* staticIntrospectionData()
    {
        return
            "<interface name=\"org.freedesktop.DBus.Introspectable\">"
            "<method name=\"Introspect\">"
            "<arg name=\"xml_data\" type=\"s\" direction=\"out\" />"
            "</method>"
            "</interface>";
    }
public:
    explicit QDBusIntrospectableInterface(const QDBusObject& obj)
        : QDBusInterface(obj, staticInterfaceName())
    { }

    QDBusIntrospectableInterface(QDBusConnection& conn, const QString& service, const QString& path)
        : QDBusInterface(conn, service, path, staticInterfaceName())
    { }

    ~QDBusIntrospectableInterface();

    inline virtual QString introspectionData() const
    { return staticIntrospectionData(); }
    
    inline QString introspect()
    { return call(QLatin1String("Introspect")).at(0).toString(); }
};

class QDBUS_EXPORT QDBusPropertiesInterface: public QDBusInterface
{
public:
    static inline const char* staticInterfaceName()
    { return DBUS_INTERFACE_PROPERTIES; }

    static inline const char* staticIntrospectionData()
    {
        return
            "<interface name=\"org.freedesktop.DBus.Properties\">"
            "<method name=\"Get\">"
            "<arg name=\"interface_name\" type=\"s\" direction=\"in\"/>"
            "<arg name=\"property_name\" type=\"s\" direction=\"in\"/>"
            "<arg name=\"value\" type=\"v\" direction=\"out\"/>"
            "</method>"
            "<method name=\"Set\">"
            "<arg name=\"interface_name\" type=\"s\" direction=\"in\"/>"
            "<arg name=\"property_name\" type=\"s\" direction=\"in\"/>"
            "<arg name=\"value\" type=\"v\" direction=\"in\"/>"
            "</method>";
            }
public:
    explicit QDBusPropertiesInterface(const QDBusObject& obj)
        : QDBusInterface(obj, staticInterfaceName())
    { }

    QDBusPropertiesInterface(QDBusConnection& conn, const QString& service, const QString& path)
        : QDBusInterface(conn, service, path, staticInterfaceName())
    { }

    ~QDBusPropertiesInterface();
    
    inline virtual QString introspectionData() const
    { return staticIntrospectionData(); }

    inline void set(const QString& interfaceName, const QString& propertyName, QVariant value)
    { call(QLatin1String("Set.ssv"), interfaceName, propertyName, value); }

    inline QVariant get(const QString& interfaceName, const QString& propertyName)
    { return call(QLatin1String("Get.ss"), interfaceName, propertyName).at(0); }
};

class QDBUS_EXPORT QDBusBusInterface: public QDBusInterface
{
public:
    static inline const char* staticInterfaceName()
    { return DBUS_INTERFACE_DBUS; }

    static const char* staticIntrospectionData();

public:
    explicit QDBusBusInterface(const QDBusObject& obj)
        : QDBusInterface(obj, staticInterfaceName())
    { }

    QDBusBusInterface(QDBusConnection& conn, const QString& service, const QString& path)
        : QDBusInterface(conn, service, path, staticInterfaceName())
    { }

    ~QDBusBusInterface();

    inline virtual QString introspectionData() const
    { return staticIntrospectionData(); }

    inline unsigned requestName(const QString& name, unsigned flags)
    { return call(QLatin1String("RequestName.su"), name, flags).at(0).toUInt(); }

    inline unsigned releaseName(const QString& name)
    { return call(QLatin1String("ReleaseName.s"), name).at(0).toUInt(); }

    inline unsigned startServiceByName(const QString& name, unsigned flags)
    { return call(QLatin1String("StartServiceByName.su"), name, flags).at(0).toUInt(); }

    inline QString Hello()
    { return call(QLatin1String("Hello")).at(0).toString(); }

    inline bool nameHasOwner(const QString& name)
    { return call(QLatin1String("NameHasOwner.s"), name).at(0).toBool(); }

    inline QStringList listNames()
    { return call(QLatin1String("ListNames")).at(0).toStringList(); }

    inline void addMatch(const QString& rule)
    { call(QLatin1String("AddMatch"), rule); }

    inline void removeMatch(const QString& rule)
    { call(QLatin1String("RemoveMatch"), rule); }

    inline QString getNameOwner(const QString& name)
    { return call(QLatin1String("GetNameOwner.s"), name).at(0).toString(); }

    inline QStringList listQueuedOwners(const QString& name)
    { return call(QLatin1String("ListQueuedOwners.s"), name).at(0).toStringList(); }

    inline quint32 getConnectionUnixUser(const QString& connectionName)
    { return call(QLatin1String("GetConnectionUnixUser.s"), connectionName).at(0).toUInt(); }

    inline quint32 getConnectionUnixProcessID(const QString& connectionName)
    { return call(QLatin1String("GetConnectionUnixProcessID.s"), connectionName).at(0).toUInt(); }

    inline QByteArray getConnectionSELinuxSecurityContext(const QString& connectionName)
    { return call(QLatin1String("GetConnectionSELinuxSecurityContext.s"), connectionName).at(0).toByteArray(); }

    inline void reloadConfig()
    { call(QLatin1String("ReloadConfig")); }
};
    

namespace org {
    namespace freedesktop {
        namespace DBus {
            typedef ::QDBusPeerInterface Peer;
            typedef ::QDBusIntrospectableInterface Introspectable;
            typedef ::QDBusPropertiesInterface Properties;
        }
    }
}

#endif
