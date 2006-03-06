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
    enum NameRequestMode { NoReplace = 0, ProhibitReplace = 1, ReplaceExisting = 2 };
    enum RegisterOption {
        ExportAdaptors = 0x01,

        ExportSlots = 0x10,
        ExportSignals = 0x20,
        ExportProperties = 0x40,
        ExportContents = 0xf0,

        ExportNonScriptableSlots = 0x110,
        ExportNonScriptableSignals = 0x220,
        ExportNonScriptableProperties = 0x440,
        ExportNonScriptableContents = 0xff0,

        ExportChildObjects = 0x1000
    };
    enum UnregisterMode {
        UnregisterNode,
        UnregisterTree
    };

    Q_DECLARE_FLAGS(RegisterOptions, RegisterOption);

    QDBusConnection(const QString &name);
    QDBusConnection(const QDBusConnection &other);
    ~QDBusConnection();

    QDBusConnection &operator=(const QDBusConnection &other);

    bool isConnected() const;
    QString baseService() const;
    QDBusError lastError() const;

    bool send(const QDBusMessage &message) const;
    QDBusMessage sendWithReply(const QDBusMessage &message) const;
    int sendWithReplyAsync(const QDBusMessage &message, QObject *receiver,
                           const char *slot) const;

    bool connect(const QString &service, const QString &path, const QString &interface,
                 const QString &name, QObject *receiver, const char *slot);
    bool connect(const QString &service, const QString &path, const QString &interface,
                 const QString &name, const QString& signature,
                 QObject *receiver, const char *slot);

    bool registerObject(const QString &path, QObject *object,
                        RegisterOptions options = ExportAdaptors);
    void unregisterObject(const QString &path, UnregisterMode = UnregisterNode);

    QDBusObject findObject(const QString& service, const QString& path);
    QDBusInterface findInterface(const QString& service, const QString& path, const QString& interface);

#ifndef QT_NO_MEMBER_TEMPLATES
    template<class Interface>
    inline Interface findInterface(const QString &service, const QString &path)
    { return Interface(findObject(service, path)); }
#endif

    bool requestName(const QString &name, NameRequestMode mode = NoReplace);
    bool releaseName(const QString& name);
    QString getNameOwner(const QString& name);

    static QDBusConnection addConnection(BusType type,
                                         const QString &name);
    static QDBusConnection addConnection(const QString &address,
                                         const QString &name);
    static void closeConnection(const QString &name);

private:
    friend class QDBusObject;
    QDBusConnectionPrivate *d;
};

namespace QDBus {
    QDBusConnection &sessionBus();
    QDBusConnection &systemBus();
}

template<class Interface>
inline Interface qDBusConnectionFindInterface(QDBusConnection &connection, const QString &service,
                                              const QString &path)
{
    return Interface(connection.findObject(service, path));
}

Q_DECLARE_OPERATORS_FOR_FLAGS(QDBusConnection::RegisterOptions)
#endif
