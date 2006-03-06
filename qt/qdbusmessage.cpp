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

#include "qdbuserror.h"
#include "qdbusmarshall_p.h"
#include "qdbusmessage_p.h"

QDBusMessagePrivate::QDBusMessagePrivate(QDBusMessage *qq)
    : connection(QString()), msg(0), reply(0), q(qq), type(DBUS_MESSAGE_TYPE_INVALID),
      timeout(-1), ref(1), repliedTo(false)
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
    \enum QDBusMessage::MessageType
    The possible message types:

    \value MethodCallMessage    a message representing an outgoing or incoming method call
    \value SignalMessage        a message representing an outgoing or incoming signal emission
    \value ReplyMessage         a message representing the return values of a method call
    \value ErrorMessage         a message representing an error condition in response to a method call
    \value InvalidMessage       an invalid message: this is never set on messages received from D-Bus
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
    \returns            a QDBusMessage object that can be sent with QDBusConnection::send,
                        QDBusConnection::sendWithReply, or QDBusConnection::sendWithReplyAsync
*/
QDBusMessage QDBusMessage::methodCall(const QString &service, const QString &path,
                                      const QString &interface, const QString &method)
{
    QDBusMessage message;
    message.d->type = DBUS_MESSAGE_TYPE_METHOD_CALL;
    message.d->service = service;
    message.d->path = path;
    message.d->interface = interface;
    message.d->name = method;

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
    message.d->connection = other.d->connection;
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
    message.d->connection = other.d->connection;
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
    message.d->connection = other.d->connection;
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

static inline const char *data(const QByteArray &arr)
{
    return arr.isEmpty() ? 0 : arr.constData();
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
        msg = dbus_message_new_method_call(data(d->service.toUtf8()), data(d->path.toUtf8()),
                                           data(d->interface.toUtf8()), data(d->name.toUtf8()));
        break;
    case DBUS_MESSAGE_TYPE_SIGNAL:
        msg = dbus_message_new_signal(data(d->path.toUtf8()), data(d->interface.toUtf8()),
                                      data(d->name.toUtf8()));
        break;
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
        msg = dbus_message_new_method_return(d->reply);
        break;
    case DBUS_MESSAGE_TYPE_ERROR:
        msg = dbus_message_new_error(d->reply, data(d->name.toUtf8()), data(d->message.toUtf8()));
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
QDBusMessage QDBusMessage::fromDBusMessage(DBusMessage *dmsg, const QDBusConnection &connection)
{
    QDBusMessage message;
    if (!dmsg)
        return message;

    message.d->connection = connection;
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
    \fn QDBusMessage::member() const
    Returns the name of the method being called.
*/

/*!
    \fn QDBusMessage::method() const
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
    \fn QDBusMessage::sender() const
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
    Sets the signature for the output arguments of this method call. This function has no meaning
    in other types of messages or when dealing with received method calls.
*/
void QDBusMessage::setSignature(const QString &signature)
{
    d->signature = signature;
}

/*!
    Returns the connection this message was received on or an unconnected QDBusConnection object if
    this isn't a message that has been received.
*/
QDBusConnection QDBusMessage::connection() const
{
    return d->connection;
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

// Document QDBusReply here
/*!
    \class QDBusReply
    \brief The reply for a method call to a remote object.

    A QDBusReply object is a subset of the QDBusMessage object that represents a method call's
    reply. It contains only the first output argument or the error code and is used by
    QDBusInterface-derived classes to allow returning the error code as the function's return
    argument.

    It can be used in the following manner:
    \code
        QDBusReply<QString> reply = interface.call("RemoteMethod");
        if (reply.isSuccess())
            // use the returned value
            useValue(reply.value());
        else
            // call failed. Show an error condition.
            showError(reply.error());
    \endcode

    If the remote method call cannot fail, you can skip the error checking:
    \code
        QString reply = interface.call("RemoteMethod");
    \endcode

    However, if it does fail under those conditions, the value returned by QDBusReply::value() is
    undefined. It may be undistinguishable from a valid return value.

    QDBusReply objects are used for remote calls that have no output arguments or return values
    (i.e., they have a "void" return type). In this case, you can only test if the reply succeeded
    or not, by calling isError() and isSuccess(), and inspecting the error condition by calling
    error(). You cannot call value().

    \sa QDBusMessage, QDBusInterface, \ref StandardInterfaces
*/

/*!
    \fn QDBusReply::QDBusReply(const QDBusMessage &reply)
    Automatically construct a QDBusReply object from the reply message \p reply, extracting the
    first return value from it if it is a success reply.
*/

/*!
    \fn QDBusReply::QDBusReply(const QDBusError &error)
    Construct an error reply from the D-Bus error.
*/

/*!
    \fn QDBusReply::isError() const
    Returns true if this reply is an error reply. You can extract the error contents using the
    error() function.
*/

/*!
    \fn QDBusReply::isSuccess() const
    Returns true if this reply is a normal error reply (not an error). You can extract the returned
    value with value()
*/

/*!
    \fn QDBusReply::error()
    Returns the error code that was returned from the remote function call. If the remote call did
    not return an error (i.e., if it succeeded), then the QDBusError object that is returned will
    not be a valid error code (QDBusError::isValid() will return false).
*/

/*!
    \fn QDBusReply::value()
    Returns the remote function's calls return value. If the remote call returned with an error,
    the return value of this function is undefined and may be undistinguishable from a valid return
    value.

    This function is not available if the remote call returns "void".
*/

/*!
    \fn QDBusReply::operator Type()
    Returns the same as value().
    
    This function is not available if the remote call returns "void".
*/

/*!
    \fn QDBusReply::fromVariant(const QDBusReply<QDBusVariant> &variantReply)>
    Converts the QDBusReply<QDBusVariant> object to this type by converting the variant contained in
    \p variantReply to the template's type and copying the error condition.

    If the QDBusVariant in variantReply is not convertible to this type, it will assume an undefined
    value.
*/

// document QDBusVariant here too
/*!
    \class QDBusVariant
    \brief Represents the D-Bus type VARIANT.

    This class represents a D-Bus argument of type VARIANT, which is composed of a type description
    and its value.
*/

/*!
    \var QDBusVariant::type
    Contains the VARIANT's type. It will contain an invalid type if this QDBusVariant argument was
    constructed, as opposed to being received over the D-Bus connection.
*/

/*!
    \var QDBusVariant::value
    Contain's the VARIANT's value.
*/

/*!
    \fn QDBusVariant::QDBusVariant()
    Constructs an empty variant. An empty variant cannot be sent over D-Bus without being
    initialized first.
*/

/*!
    \fn QDBusVariant::QDBusVariant(const QVariant &variant)
    Constructs a D-Bus Variant from the QVariant value \p variant. The D-Bus type, if not set, will
    be guessed from the QVariant value when actually sending the argument over D-Bus by calling
    QDBusType::guessFromVariant. You should explicitly set the type if are unsure the automatic
    guessing will produce the correct type.
*/

/*!
    \fn QDBusVariant::QDBusVariant(const QVariant &variant, const QDBusType &forcetype)
    Constructs a D-Bus Variant from the QVariant of value \p variant and sets the type to \p
    forcetype. The actual transformation of the QVariant to the proper D-Bus type will happen only
    when sending this argument over D-Bus.
*/

/*!
    \fn QDBusVariant::operator const QVariant &() const
    Returns the value #value.
*/

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

