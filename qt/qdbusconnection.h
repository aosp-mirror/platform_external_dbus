/* qdbusconnection.h QDBusConnection object
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

#ifndef QDBUSCONNECTION_H
#define QDBUSCONNECTION_H

#include "qdbusmacros.h"
#include <QtCore/qstring.h>

class QDBusConnectionPrivate;
class QDBusXmlParser;
class QDBusObject;
class QDBusInterface;
class QDBusError;
class QDBusMessage;
class QByteArray;
class QObject;

class QDBUS_EXPORT QDBusConnection
{
public:
    enum BusType { SessionBus, SystemBus, ActivationBus };

    QDBusConnection(const QString &name = QLatin1String(default_connection_name));
    QDBusConnection(const QDBusConnection &other);
    ~QDBusConnection();

    QDBusConnection &operator=(const QDBusConnection &other);

    bool isConnected() const;
    QDBusError lastError() const;

    enum NameRequestMode { NoReplace = 0, ProhibitReplace = 1, ReplaceExisting = 2 };
    bool requestName(const QString &name, NameRequestMode mode = NoReplace);
    bool releaseName(const QString& name);
    QString getNameOwner(const QString& name);


    QString baseService() const;

    bool send(const QDBusMessage &message) const;
    QDBusMessage sendWithReply(const QDBusMessage &message) const;
    int sendWithReplyAsync(const QDBusMessage &message, QObject *receiver,
                           const char *slot) const;

    bool connect(const QString &service, const QString &path, const QString &interface,
                 const QString &name, QObject *receiver, const char *slot);
    bool connect(const QString &service, const QString &path, const QString &interface,
                 const QString &name, const QString& signature,
                 QObject *receiver, const char *slot);

    enum RegisterOption {
        ExportForAnyInterface = 0x01,
        ExportAdaptors = 0x03,
        
        ExportOwnSlots = 0x10,
        ExportOwnSignals = 0x20,
        ExportOwnProperties = 0x40,
        ExportOwnContents = 0xf0,
        
        ExportNonScriptableSlots = 0x100,
        ExportNonScriptableSignals = 0x200,
        ExportNonScriptableProperties = 0x400,
        ExportNonScriptables = 0xf00,
        
        ExportChildObjects = 0x1000,        

        Reexport = 0x100000,
    };
    Q_DECLARE_FLAGS(RegisterOptions, RegisterOption);
    
    bool registerObject(const QString &path, const QString &interface, QObject *object,
                        RegisterOptions options = ExportOwnContents);
    bool registerObject(const QString &path, QObject *object,
                        RegisterOptions options = ExportAdaptors);
    void unregisterObject(const QString &path);

    QDBusObject findObject(const QString& service, const QString& path);
    QDBusInterface findInterface(const QString& service, const QString& path, const QString& interface);
    

    static QDBusConnection addConnection(BusType type,
                               const QString &name = QLatin1String(default_connection_name));
    static QDBusConnection addConnection(const QString &address,
                               const QString &name = QLatin1String(default_connection_name));
    static void closeConnection(const QString &name = QLatin1String(default_connection_name));

    QT_STATIC_CONST char *default_connection_name;

private:
    friend class QDBusObject;
    QDBusConnectionPrivate *d;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(QDBusConnection::RegisterOptions)
#endif
