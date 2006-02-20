/* qdbusmessage.h QDBusMessage object
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

#ifndef QDBUSMESSAGE_H
#define QDBUSMESSAGE_H

#include "qdbusmacros.h"
#include <QtCore/qlist.h>
#include <QtCore/qvariant.h>

#include <limits.h>

class QDBusMessagePrivate;
class QDBusError;
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
    static QDBusMessage methodCall(const QString &destination, const QString &path,
                                   const QString &interface, const QString &method,
                                   const QString &signature = QString());
    static QDBusMessage methodReply(const QDBusMessage &other);
    static QDBusMessage error(const QDBusMessage &other, const QString &name,
                              const QString &message = QString());
    static QDBusMessage error(const QDBusMessage &other, const QDBusError &error);

    QString path() const;
    QString interface() const;
    QString name() const;
    inline QString member() const { return name(); }
    inline QString method() const { return name(); }
    QString service() const;
    inline QString sender() const { return service(); }
    MessageType type() const;

    int timeout() const;
    void setTimeout(int ms);

    bool noReply() const;
    void setNoReply(bool enable);

    QString signature() const;

//protected:
    DBusMessage *toDBusMessage() const;
    static QDBusMessage fromDBusMessage(DBusMessage *dmsg);
    static QDBusMessage fromError(const QDBusError& error);
    int serialNumber() const;
    int replySerialNumber() const;
    bool wasRepliedTo() const;

private:
    QDBusMessagePrivate *d;
};

#ifndef QT_NO_DEBUG
QDebug operator<<(QDebug, const QDBusMessage &);
#endif

#endif

