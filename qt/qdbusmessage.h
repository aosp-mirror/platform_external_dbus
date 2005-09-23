/* qdbusmessage.h QDBusMessage object
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

#ifndef QDBUSMESSAGE_H
#define QDBUSMESSAGE_H

#include "dbus/qdbus.h"

#include <QtCore/qlist.h>
#include <QtCore/qvariant.h>

#include <limits.h>

class QDBusMessagePrivate;
struct DBusMessage;

class QDBUS_EXPORT QDBusMessage: public QList<QVariant>
{
    friend class QDBusConnection;
public:
    enum { DefaultTimeout = -1, NoTimeout = INT_MAX};
    enum MessageType { InvalidMessage, MethodCallMessage, ReplyMessage,
                       ErrorMessage, SignalMessage };

    QDBusMessage();
    QDBusMessage(const QDBusMessage &other);
    ~QDBusMessage();

    QDBusMessage &operator=(const QDBusMessage &other);

    static QDBusMessage signal(const QString &path, const QString &interface,
                               const QString &name);
    static QDBusMessage methodCall(const QString &service, const QString &path,
                                   const QString &interface, const QString &method);
    static QDBusMessage methodReply(const QDBusMessage &other);

    QString path() const;
    QString interface() const;
    QString name() const; //rename to member?
    QString sender() const; //rename to service?
    MessageType type() const;

    int timeout() const;
    void setTimeout(int ms);


//protected:
    DBusMessage *toDBusMessage() const;
    static QDBusMessage fromDBusMessage(DBusMessage *dmsg);
    int serialNumber() const;
    int replySerialNumber() const;

private:
    QDBusMessagePrivate *d;
};

#ifndef QT_NO_DEBUG
QDebug operator<<(QDebug, const QDBusMessage &);
#endif

#endif

