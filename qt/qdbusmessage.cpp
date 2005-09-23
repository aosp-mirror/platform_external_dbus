/* qdbusmessage.cpp
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

#include "qdbusmessage.h"

#include <QtCore/qdebug.h>
#include <QtCore/qstringlist.h>

#include <dbus/dbus.h>

#include "qdbusmarshall.h"
#include "qdbusmessage_p.h"

QDBusMessagePrivate::QDBusMessagePrivate(QDBusMessage *qq)
    : msg(0), reply(0), q(qq), type(DBUS_MESSAGE_TYPE_INVALID), timeout(-1), ref(1)
{
}

QDBusMessagePrivate::~QDBusMessagePrivate()
{
    if (msg)
        dbus_message_unref(msg);
    if (reply)
        dbus_message_unref(reply);
}

///////////////


QDBusMessage QDBusMessage::signal(const QString &path, const QString &interface,
                                  const QString &name)
{
    QDBusMessage message;
    message.d->type = DBUS_MESSAGE_TYPE_SIGNAL;
    message.d->path = path;
    message.d->interface = interface;
    message.d->name = name;

    return message;
}

QDBusMessage QDBusMessage::methodCall(const QString &service, const QString &path,
                                      const QString &interface, const QString &method)
{
    QDBusMessage message;
    message.d->type = DBUS_MESSAGE_TYPE_METHOD_CALL;
    message.d->service = service;
    message.d->path = path;
    message.d->interface = interface;
    message.d->method = method;

    return message;
}

QDBusMessage QDBusMessage::methodReply(const QDBusMessage &other)
{
    Q_ASSERT(other.d->msg);

    QDBusMessage message;
    message.d->type = DBUS_MESSAGE_TYPE_METHOD_RETURN;
    message.d->reply = dbus_message_ref(other.d->msg);

    return message;
}

QDBusMessage::QDBusMessage()
{
    d = new QDBusMessagePrivate(this);
}

QDBusMessage::QDBusMessage(const QDBusMessage &other)
    : QList<QVariant>(other)
{
    d = other.d;
    d->ref.ref();
}

QDBusMessage::~QDBusMessage()
{
    if (!d->ref.deref())
        delete d;
}

QDBusMessage &QDBusMessage::operator=(const QDBusMessage &other)
{
    QList<QVariant>::operator=(other);
    qAtomicAssign(d, other.d);
    return *this;
}

DBusMessage *QDBusMessage::toDBusMessage() const
{
    DBusMessage *msg = 0;
    switch (d->type) {
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
        msg = dbus_message_new_method_call(d->service.toUtf8().constData(),
                d->path.toUtf8().constData(), d->interface.toUtf8().constData(),
                d->method.toUtf8().constData());
        break;
    case DBUS_MESSAGE_TYPE_SIGNAL:
        msg = dbus_message_new_signal(d->path.toUtf8().constData(),
                d->interface.toUtf8().constData(), d->name.toUtf8().constData());
        break;
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
        msg = dbus_message_new_method_return(d->reply);
        break;
    }
    if (!msg)
        return 0;

    QDBusMarshall::listToMessage(*this, msg);
    return msg;
}

QDBusMessage QDBusMessage::fromDBusMessage(DBusMessage *dmsg)
{
    QDBusMessage message;
    if (!dmsg)
        return message;

    message.d->type = dbus_message_get_type(dmsg);
    message.d->path = QString::fromUtf8(dbus_message_get_path(dmsg));
    message.d->interface = QString::fromUtf8(dbus_message_get_interface(dmsg));
    message.d->name = QString::fromUtf8(dbus_message_get_member(dmsg));
    message.d->sender = QString::fromUtf8(dbus_message_get_sender(dmsg));
    message.d->msg = dbus_message_ref(dmsg);

    QDBusMarshall::messageToList(message, dmsg);

    return message;
}

QString QDBusMessage::path() const
{
    return d->path;
}

QString QDBusMessage::interface() const
{
    return d->interface;
}

QString QDBusMessage::name() const
{
    return d->name;
}

QString QDBusMessage::sender() const
{
    return d->sender;
}

int QDBusMessage::timeout() const
{
    return d->timeout;
}

void QDBusMessage::setTimeout(int ms)
{
    d->timeout = ms;
}

/*!
    Returns the unique serial number assigned to this message
    or 0 if the message was not sent yet.
 */
int QDBusMessage::serialNumber() const
{
    if (!d->msg)
        return 0;
    return dbus_message_get_serial(d->msg);
}

/*!
    Returns the unique serial number assigned to the message
    that triggered this reply message.

    If this message is not a reply to another message, 0
    is returned.

 */
int QDBusMessage::replySerialNumber() const
{
    if (!d->msg)
        return 0;
    return dbus_message_get_reply_serial(d->msg);
}

QDBusMessage::MessageType QDBusMessage::type() const
{
    switch (d->type) {
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
        return MethodCallMessage;
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
        return ReplyMessage;
    case DBUS_MESSAGE_TYPE_ERROR:
        return ErrorMessage;
    case DBUS_MESSAGE_TYPE_SIGNAL:
        return SignalMessage;
    default:
        return InvalidMessage;
    }
}

#ifndef QT_NO_DEBUG
QDebug operator<<(QDebug dbg, const QDBusMessage &msg)
{
    dbg.nospace() << "QDBusMessage(" << msg.path() << ", " << msg.interface() << ", "
                  << msg.name() << ", " << msg.sender() << ", "
                  << static_cast<QList<QVariant> >(msg) << ")";
    return dbg.space();
}
#endif

