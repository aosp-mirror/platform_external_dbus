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
#include "qdbusreply.h"
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
            "  <interface name=\"org.freedesktop.DBus.Peer\">\n"
            "    <method name=\"Ping\" />\n"
            "  </interface>\n";
    }

public:
    explicit QDBusPeerInterface(const QDBusObject& obj)
        : QDBusInterface(obj, QLatin1String(staticInterfaceName()))
    { }

    ~QDBusPeerInterface();

    inline virtual QString introspectionData() const
    { return QString::fromLatin1(staticIntrospectionData()); }

    inline QDBusReply<void> ping()
    { return call(QLatin1String("Ping")); }
};

class QDBUS_EXPORT QDBusIntrospectableInterface: public QDBusInterface
{
public:
    static inline const char* staticInterfaceName()
    { return DBUS_INTERFACE_INTROSPECTABLE; }

    static inline const char* staticIntrospectionData()
    {
        return
            "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
            "    <method name=\"Introspect\">\n"
            "      <arg name=\"xml_data\" type=\"s\" direction=\"out\"/>\n"
            "    </method>\n"
            "  </interface>\n";
    }
public:
    explicit QDBusIntrospectableInterface(const QDBusObject& obj)
        : QDBusInterface(obj, QLatin1String(staticInterfaceName()))
    { }

    ~QDBusIntrospectableInterface();

    inline virtual QString introspectionData() const
    { return QLatin1String(staticIntrospectionData()); }

    inline QDBusReply<QString> introspect()
    { return call(QLatin1String("Introspect")); }
};

class QDBUS_EXPORT QDBusPropertiesInterface: public QDBusInterface
{
public:
    static inline const char* staticInterfaceName()
    { return DBUS_INTERFACE_PROPERTIES; }

    static inline const char* staticIntrospectionData()
    {
        return
            "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
            "    <method name=\"Get\">\n"
            "      <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"
            "      <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"
            "      <arg name=\"value\" type=\"v\" direction=\"out\"/>\n"
            "    </method>\n"
            "    <method name=\"Set\">\n"
            "      <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"
            "      <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"
            "      <arg name=\"value\" type=\"v\" direction=\"in\"/>\n"
            "    </method>\n"
            "  </interface>\n";
            }
public:
    explicit QDBusPropertiesInterface(const QDBusObject& obj)
        : QDBusInterface(obj, QLatin1String(staticInterfaceName()))
    { }

    ~QDBusPropertiesInterface();

    inline virtual QString introspectionData() const
    { return QString::fromLatin1(staticIntrospectionData()); }

    inline QDBusReply<void> set(const QString& interfaceName, const QString& propertyName,
                                const QDBusVariant &value)
    { return call(QLatin1String("Set.ssv"), interfaceName, propertyName, value); }

    inline QDBusReply<QDBusVariant> get(const QString& interfaceName, const QString& propertyName)
    { return call(QLatin1String("Get.ss"), interfaceName, propertyName); }
};

#if 0
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

    ~QDBusBusInterface();

    inline virtual QString introspectionData() const
    { return staticIntrospectionData(); }

    inline QDBusReply<unsigned> requestName(const QString& name, unsigned flags)
    { return call(QLatin1String("RequestName.su"), name, flags); }

    inline QDBusReply<unsigned> releaseName(const QString& name)
    { return call(QLatin1String("ReleaseName.s"), name); }

    inline QDBusReply<unsigned> startServiceByName(const QString& name, unsigned flags)
    { return call(QLatin1String("StartServiceByName.su"), name, flags); }

    inline QDBusReply<QString> Hello()
    { return call(QLatin1String("Hello")); }

    inline QDBusReply<bool> nameHasOwner(const QString& name)
    { return call(QLatin1String("NameHasOwner.s"), name); }

    inline QDBusReply<QStringList> listNames()
    { return call(QLatin1String("ListNames")); }

    inline QDBusReply<void> addMatch(const QString& rule)
    { return call(QLatin1String("AddMatch"), rule); }

    inline QDBusReply<void> removeMatch(const QString& rule)
    { return call(QLatin1String("RemoveMatch"), rule); }

    inline QDBusReply<QString> getNameOwner(const QString& name)
    { return call(QLatin1String("GetNameOwner.s"), name); }

    inline QDBusReply<QStringList> listQueuedOwners(const QString& name)
    { return call(QLatin1String("ListQueuedOwners.s"), name); }

    inline QDBusReply<quint32> getConnectionUnixUser(const QString& connectionName)
    { return call(QLatin1String("GetConnectionUnixUser.s"), connectionName); }

    inline QDBusReply<quint32> getConnectionUnixProcessID(const QString& connectionName)
    { return call(QLatin1String("GetConnectionUnixProcessID.s"), connectionName); }

    inline QDBusReply<QByteArray> getConnectionSELinuxSecurityContext(const QString& connectionName)
    { return call(QLatin1String("GetConnectionSELinuxSecurityContext.s"), connectionName); }

    inline QDBusReply<void> reloadConfig()
    { return call(QLatin1String("ReloadConfig")); }
};
#endif    

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
