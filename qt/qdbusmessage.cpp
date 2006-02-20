/* qdbusmessage.cpp
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

#include "qdbusmessage.h"

#include <qdebug.h>
#include <qstringlist.h>

#include <dbus/dbus.h>

#include "qdbusmarshall.h"
#include "qdbuserror.h"
#include "qdbusmessage_p.h"

QDBusMessagePrivate::QDBusMessagePrivate(QDBusMessage *qq)
    : msg(0), reply(0), q(qq), type(DBUS_MESSAGE_TYPE_INVALID), timeout(-1), ref(1),
      repliedTo(false)
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
/*!
    \class QDBusMessage
    \brief Represents one message sent or received over the DBus bus.

    This object can represent any of four different types of messages possible on the bus
    (see MessageType)
     - Method calls
     - Method return values
     - Signal emissions
     - Error codes

    Objects of this type are created with the four static functions signal, methodCall,
    methodReply and error.
*/

/*!
    Constructs a new DBus message representing a signal emission. A DBus signal is emitted
    from one application and is received by all applications that are listening for that signal
    from that interface.

    \param path         the path of the object that is emitting the signal
    \param interface    the interface that is emitting the signal
    \param name         the name of the signal (a.k.a. method name)
    \returns            a QDBusMessage object that can be sent with with QDBusConnection::send
*/
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

/*!
    Constructs a new DBus message representing a method call. A method call always informs
    its destination address (service, path, interface and method).

    The DBus bus allows calling a method on a given remote object without specifying the
    destination interface, if the method name is unique. However, if two interfaces on the
    remote object export the same method name, the result is undefined (one of the two may be
    called or an error may be returned).

    When using DBus in a peer-to-peer context (i.e., not on a bus), the service parameter is
    optional.

    Optionally, a signature parameter can be passed, indicating the type of the parameters to
    be marshalled over the bus. If there are more arguments thanentries in the signature, the
    tailing arguments will be silently dropped and not sent. If there are less arguments,
    default values will be inserted (default values are those created by QVariant::convert
    when a variant of type QVariant::Invalid is converted to the type).

    The QDBusObject and QDBusInterface classes provide a simpler abstraction to synchronous
    method calling.

    \param service      the remote service to be called (can be a well-known name, a bus
                        address or null)
    \param path         the path of the object on the remote service to be called
    \param interface    the remote interface that is wanted (can be null)
    \param method       the remote method to be called (a.k.a., name)
    \param sig          the DBus signature (set to null to discard processing and guess the
                        method signature from the arguments; empty means no arguments)
    \returns            a QDBusMessage object that can be sent with QDBusConnection::send,
                        QDBusConnection::sendWithReply, or QDBusConnection::sendWithReplyAsync
*/
QDBusMessage QDBusMessage::methodCall(const QString &service, const QString &path,
                                      const QString &interface, const QString &method,
                                      const QString &sig)
{
    QDBusMessage message;
    message.d->type = DBUS_MESSAGE_TYPE_METHOD_CALL;
    message.d->service = service;
    message.d->path = path;
    message.d->interface = interface;
    message.d->name = method;
    message.d->signature = sig;

    return message;
}

/*!
    Constructs a new DBus message representing the return values from a called method.

    \param other        the method call DBus message that this is a reply to
    \returns            a QDBusMessage object that can be sent with QDBusConnection::send
*/
QDBusMessage QDBusMessage::methodReply(const QDBusMessage &other)
{
    Q_ASSERT(other.d->msg);

    QDBusMessage message;
    message.d->type = DBUS_MESSAGE_TYPE_METHOD_RETURN;
    message.d->reply = dbus_message_ref(other.d->msg);
    other.d->repliedTo = true;

    return message;
}

/*!
    Constructs a DBus message representing an error condition.

    \param other        the QDBusMessage object that generated this error
    \param name         the DBus error name (error names must follow the same convention that
                        interface names do)
    \param msg          the error message
    \return             a QDBusMessage object that can be sent with QDBusMessage::send
*/
QDBusMessage QDBusMessage::error(const QDBusMessage &other, const QString &name,
                                 const QString &msg)
{
    Q_ASSERT(other.d->msg);

    QDBusMessage message;
    message.d->type = DBUS_MESSAGE_TYPE_ERROR;
    message.d->name = name;
    message.d->message = msg;
    message.d->reply = dbus_message_ref(other.d->msg);
    other.d->repliedTo = true;

    return message;
}

/*!
    \overload
    Constructs a DBus message representing an error condition.

    \param other        the QDBusMessage object that generated this error
    \param error        the QDBusError object representing this error
    \return             a QDBusMessage object that can be sent with QDBusMessage::send
*/
QDBusMessage QDBusMessage::error(const QDBusMessage &other, const QDBusError &error)
{
    Q_ASSERT(other.d->msg);

    QDBusMessage message;
    message.d->type = DBUS_MESSAGE_TYPE_ERROR;
    message.d->name = error.name();
    message.d->message = error.message();
    message.d->reply = dbus_message_ref(other.d->msg);
    other.d->repliedTo = true;

    return message;
}

/*!
    Constructs an empty, invalid QDBusMessage object.

    \sa methodCall, methodReply, signal, error
*/
QDBusMessage::QDBusMessage()
{
    d = new QDBusMessagePrivate(this);
}

/*!
    Constructs a copy of the other object.
*/
QDBusMessage::QDBusMessage(const QDBusMessage &other)
    : QList<QVariant>(other)
{
    d = other.d;
    d->ref.ref();
}

/*!
    Disposes of the object and frees any resources that were being held.
*/
QDBusMessage::~QDBusMessage()
{
    if (!d->ref.deref())
        delete d;
}

/*!
    Copies the contents of the other object.
*/
QDBusMessage &QDBusMessage::operator=(const QDBusMessage &other)
{
    QList<QVariant>::operator=(other);
    qAtomicAssign(d, other.d);
    return *this;
}

/*!
    \internal
    Constructs a DBusMessage object from this object. The returned value must be de-referenced
    with dbus_message_unref.
*/
DBusMessage *QDBusMessage::toDBusMessage() const
{
    DBusMessage *msg = 0;
    
    switch (d->type) {
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
        msg = dbus_message_new_method_call(d->service.toUtf8().constData(),
                d->path.toUtf8().constData(), d->interface.toUtf8().constData(),
                d->name.toUtf8().constData());
        break;
    case DBUS_MESSAGE_TYPE_SIGNAL:
        msg = dbus_message_new_signal(d->path.toUtf8().constData(),
                d->interface.toUtf8().constData(), d->name.toUtf8().constData());
        break;
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
        msg = dbus_message_new_method_return(d->reply);
        break;
    case DBUS_MESSAGE_TYPE_ERROR:
        msg = dbus_message_new_error(d->reply, d->name.toUtf8().constData(),
                                     d->message.toUtf8().constData());
        break;
    }
    if (!msg)
        return 0;

    QDBusMarshall::listToMessage(*this, msg, d->signature);
    return msg;
}

/*!
    \internal
    Constructs a QDBusMessage by parsing the given DBusMessage object.
*/
QDBusMessage QDBusMessage::fromDBusMessage(DBusMessage *dmsg)
{
    QDBusMessage message;
    if (!dmsg)
        return message;

    message.d->type = dbus_message_get_type(dmsg);
    message.d->path = QString::fromUtf8(dbus_message_get_path(dmsg));
    message.d->interface = QString::fromUtf8(dbus_message_get_interface(dmsg));
    message.d->name = message.d->type == DBUS_MESSAGE_TYPE_ERROR ?
                      QString::fromUtf8(dbus_message_get_error_name(dmsg)) :
                      QString::fromUtf8(dbus_message_get_member(dmsg));
    message.d->service = QString::fromUtf8(dbus_message_get_sender(dmsg));
    message.d->signature = QString::fromUtf8(dbus_message_get_signature(dmsg));
    message.d->msg = dbus_message_ref(dmsg);

    QDBusMarshall::messageToList(message, dmsg);
    return message;
}

/*!
    Creates a QDBusMessage that represents the same error as the QDBusError object.
*/
QDBusMessage QDBusMessage::fromError(const QDBusError &error)
{
    QDBusMessage message;
    message.d->type = DBUS_MESSAGE_TYPE_ERROR;
    message.d->name = error.name();
    message << error.message();
    return message;
}

/*!
    Returns the path of the object that this message is being sent to (in the case of a
    method call) or being received from (for a signal).
*/
QString QDBusMessage::path() const
{
    return d->path;
}

/*!
    Returns the interface of the method being called (in the case of a method call) or of
    the signal being received from.
*/
QString QDBusMessage::interface() const
{
    return d->interface;
}

/*!
    Returns the name of the signal that was emitted or the name of the error that was
    received.
    \sa member
*/
QString QDBusMessage::name() const
{
    return d->name;
}

/*!
    \fn QDBusMessage::member
    Returns the name of the method being called.
*/

/*!
    \fn QDBusMessage::method
    \overload
    Returns the name of the method being called.
*/

/*!
    Returns the name of the service or the bus address of the remote method call.
*/
QString QDBusMessage::service() const
{
    return d->service;
}

/*!
    \fn QDBusMessage::sender
    Returns the unique name of the remote sender.
*/

/*!
    Returns the timeout (in milliseconds) for this message to be processed.
*/
int QDBusMessage::timeout() const
{
    return d->timeout;
}

/*!
    Sets the timeout for this message to be processed.

    \param ms           the time, in milliseconds
*/
void QDBusMessage::setTimeout(int ms)
{
    d->timeout = ms;
}

/*!
    Returns the flag that indicates if this message should see a reply or not. This is only
    meaningful for MethodCall messages: any other kind of message cannot have replies and this
    function will always return false for them.
*/
bool QDBusMessage::noReply() const
{
    if (!d->msg)
        return false;
    return dbus_message_get_no_reply(d->msg);
}

/*!
    Sets the flag that indicates whether we're expecting a reply from the callee. This flag only
    makes sense for MethodCall messages.

    \param enable       whether to enable the flag (i.e., we are not expecting a reply)
*/
void QDBusMessage::setNoReply(bool enable)
{
    if (d->msg)
        dbus_message_set_no_reply(d->msg, enable);
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

/*!
    Returns true if this is a MethodCall message and a reply for it has been generated using
    QDBusMessage::methodReply or QDBusMessage::error.
*/
bool QDBusMessage::wasRepliedTo() const
{
    return d->repliedTo;
}

/*!
    Returns the signature of the signal that was received or for the output arguments
    of a method call.
*/
QString QDBusMessage::signature() const
{
    return d->signature;
}

/*!
    Returns the message type.
*/
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
QDebug operator<<(QDebug dbg, QDBusMessage::MessageType t)
{
    switch (t)
    {
    case QDBusMessage::MethodCallMessage:
        return dbg << "MethodCall";        
    case QDBusMessage::ReplyMessage:
        return dbg << "MethodReturn";
    case QDBusMessage::SignalMessage:
        return dbg << "Signal";
    case QDBusMessage::ErrorMessage:
        return dbg << "Error";
    default:
        return dbg << "Invalid";
    }
}

QDebug operator<<(QDebug dbg, const QDBusMessage &msg)
{
    dbg.nospace() << "QDBusMessage(type=" << msg.type()
                  << ", service=" << msg.service()
                  << ", path=" << msg.path()
                  << ", interface=" << msg.interface()
                  << ", name=" << msg.name()
                  << ", signature=" << msg.signature()
                  << ", contents=" << static_cast<QList<QVariant> >(msg) << ")";
    return dbg.space();
}
#endif

