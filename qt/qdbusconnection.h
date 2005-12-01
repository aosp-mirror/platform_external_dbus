/* qdbusconnection.h QDBusConnection object
 *
 * Copyright (C) 2005 Harald Fernengel <harry@kdevelop.org>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef QDBUSCONNECTION_H
#define QDBUSCONNECTION_H

#include "dbus/qdbus.h"
#include <QtCore/qstring.h>

class QDBusConnectionPrivate;
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

    enum NameRequestMode { NoReplace = 0, AllowReplace = 1, ReplaceExisting = 2 };
    bool requestName(const QString &name, NameRequestMode mode = NoReplace);

    QString baseService() const;

    bool send(const QDBusMessage &message) const;
    QDBusMessage sendWithReply(const QDBusMessage &message) const;
    int sendWithReplyAsync(const QDBusMessage &message, QObject *receiver,
                           const char *slot) const;

    bool connect(const QString &path, const QString &interface,
                 const QString &name, QObject *receiver, const char *slot);

    bool registerObject(const QString &path, const QString &interface,
                        QObject *object);
    void unregisterObject(const QString &path);

    static QDBusConnection addConnection(BusType type,
                               const QString &name = QLatin1String(default_connection_name));
    static QDBusConnection addConnection(const QString &address,
                               const QString &name = QLatin1String(default_connection_name));
    static void closeConnection(const QString &name = QLatin1String(default_connection_name));

    QT_STATIC_CONST char *default_connection_name;

private:
    QDBusConnectionPrivate *d;
};

#endif
