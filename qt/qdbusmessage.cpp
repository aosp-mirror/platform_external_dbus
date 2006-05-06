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
#include "qdbustypehelper_p.h"

QDBusMessagePrivate::QDBusMessagePrivate()
    : connection(QString()), msg(0), reply(0), type(DBUS_MESSAGE_TYPE_INVALID),
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
    \list
      \o Method calls
      \o Method return values
      \o Signal emissions
      \o Error codes
    \endlist

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

    The signal will be constructed to represent a signal coming from the path \a path, interface \a
    interface and signal name \a name.

    The QDBusMessage object that is returned can be sent with QDBusConnection::send().
*/
QDBusMessage QDBusMessage::signal(const QString &path, const QString &interface,
                                  const QString &name)
{
    QDBusMessage message;
    message.d_ptr->type = DBUS_MESSAGE_TYPE_SIGNAL;
    message.d_ptr->path = path;
    message.d_ptr->interface = interface;
    message.d_ptr->name = name;

    return message;
}

/*!
    Constructs a new DBus message representing a method call. A method call always informs
    its destination address (\a service, \a path, \a interface and \a method).

    The DBus bus allows calling a method on a given remote object without specifying the
    destination interface, if the method name is unique. However, if two interfaces on the
    remote object export the same method name, the result is undefined (one of the two may be
    called or an error may be returned).

    When using DBus in a peer-to-peer context (i.e., not on a bus), the \a service parameter is
    optional.

    The QDBusObject and QDBusInterface classes provide a simpler abstraction to synchronous
    method calling.

    This function returns a QDBusMessage object that can be sent with QDBusConnection::send(),
    QDBusConnection::sendWithReply(), or QDBusConnection::sendWithReplyAsync().
*/
QDBusMessage QDBusMessage::methodCall(const QString &service, const QString &path,
                                      const QString &interface, const QString &method)
{
    QDBusMessage message;
    message.d_ptr->type = DBUS_MESSAGE_TYPE_METHOD_CALL;
    message.d_ptr->service = service;
    message.d_ptr->path = path;
    message.d_ptr->interface = interface;
    message.d_ptr->name = method;

    return message;
}

/*!
    Constructs a new DBus message representing the return values from a called method. The \a other
    variable represents the method call that the reply will be for.
    
    This function returns a QDBusMessage object that can be sent with QDBusConnection::send().
*/
QDBusMessage QDBusMessage::methodReply(const QDBusMessage &other)
{
    Q_ASSERT(other.d_ptr->msg);

    QDBusMessage message;
    message.d_ptr->connection = other.d_ptr->connection;
    message.d_ptr->type = DBUS_MESSAGE_TYPE_METHOD_RETURN;
    message.d_ptr->reply = dbus_message_ref(other.d_ptr->msg);
    other.d_ptr->repliedTo = true;

    return message;
}

/*!
    Constructs a DBus message representing an error condition described by the \a name
    parameter. The \a msg parameter is optional and may contain a human-readable description of the
    error. The \a other variable represents the method call that this error relates to.

    This function returns a QDBusMessage object that can be sent with QDBusMessage::send().
*/
QDBusMessage QDBusMessage::error(const QDBusMessage &other, const QString &name,
                                 const QString &msg)
{
    Q_ASSERT(other.d_ptr->msg);

    QDBusMessage message;
    message.d_ptr->connection = other.d_ptr->connection;
    message.d_ptr->type = DBUS_MESSAGE_TYPE_ERROR;
    message.d_ptr->name = name;
    message.d_ptr->message = msg;
    message.d_ptr->reply = dbus_message_ref(other.d_ptr->msg);
    other.d_ptr->repliedTo = true;

    return message;
}

/*!
    \overload
    Constructs a DBus message representing an error, where \a other is the method call that
    generated this error and \a error is the error code.
*/
QDBusMessage QDBusMessage::error(const QDBusMessage &other, const QDBusError &error)
{
    Q_ASSERT(other.d_ptr->msg);

    QDBusMessage message;
    message.d_ptr->connection = other.d_ptr->connection;
    message.d_ptr->type = DBUS_MESSAGE_TYPE_ERROR;
    message.d_ptr->name = error.name();
    message.d_ptr->message = error.message();
    message.d_ptr->reply = dbus_message_ref(other.d_ptr->msg);
    other.d_ptr->repliedTo = true;

    return message;
}

/*!
    Constructs an empty, invalid QDBusMessage object.

    \sa methodCall(), methodReply(), signal(), error()
*/
QDBusMessage::QDBusMessage()
{
    d_ptr = new QDBusMessagePrivate;
}

/*!
    Constructs a copy of the object given by \a other.
*/
QDBusMessage::QDBusMessage(const QDBusMessage &other)
    : QList<QVariant>(other)
{
    d_ptr = other.d_ptr;
    d_ptr->ref.ref();
}

/*!
    Disposes of the object and frees any resources that were being held.
*/
QDBusMessage::~QDBusMessage()
{
    if (!d_ptr->ref.deref())
        delete d_ptr;
}

/*!
    Copies the contents of the object given by \a other.
*/
QDBusMessage &QDBusMessage::operator=(const QDBusMessage &other)
{
    QList<QVariant>::operator=(other);
    qAtomicAssign(d_ptr, other.d_ptr);
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
    
    switch (d_ptr->type) {
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
        msg = dbus_message_new_method_call(data(d_ptr->service.toUtf8()), data(d_ptr->path.toUtf8()),
                                           data(d_ptr->interface.toUtf8()), data(d_ptr->name.toUtf8()));
        break;
    case DBUS_MESSAGE_TYPE_SIGNAL:
        msg = dbus_message_new_signal(data(d_ptr->path.toUtf8()), data(d_ptr->interface.toUtf8()),
                                      data(d_ptr->name.toUtf8()));
        break;
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
        msg = dbus_message_new_method_return(d_ptr->reply);
        break;
    case DBUS_MESSAGE_TYPE_ERROR:
        msg = dbus_message_new_error(d_ptr->reply, data(d_ptr->name.toUtf8()), data(d_ptr->message.toUtf8()));
        break;
    }
    if (!msg)
        return 0;

    QDBusMarshall::listToMessage(*this, msg, d_ptr->signature);
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

    message.d_ptr->connection = connection;
    message.d_ptr->type = dbus_message_get_type(dmsg);
    message.d_ptr->path = QString::fromUtf8(dbus_message_get_path(dmsg));
    message.d_ptr->interface = QString::fromUtf8(dbus_message_get_interface(dmsg));
    message.d_ptr->name = message.d_ptr->type == DBUS_MESSAGE_TYPE_ERROR ?
                      QString::fromUtf8(dbus_message_get_error_name(dmsg)) :
                      QString::fromUtf8(dbus_message_get_member(dmsg));
    message.d_ptr->service = QString::fromUtf8(dbus_message_get_sender(dmsg));
    message.d_ptr->signature = QString::fromUtf8(dbus_message_get_signature(dmsg));
    message.d_ptr->msg = dbus_message_ref(dmsg);

    QDBusMarshall::messageToList(message, dmsg);
    return message;
}

/*!
    Creates a QDBusMessage that represents the same error as the QDBusError object.
*/
QDBusMessage QDBusMessage::fromError(const QDBusError &error)
{
    QDBusMessage message;
    message.d_ptr->type = DBUS_MESSAGE_TYPE_ERROR;
    message.d_ptr->name = error.name();
    message << error.message();
    return message;
}

/*!
    Returns the path of the object that this message is being sent to (in the case of a
    method call) or being received from (for a signal).
*/
QString QDBusMessage::path() const
{
    return d_ptr->path;
}

/*!
    Returns the interface of the method being called (in the case of a method call) or of
    the signal being received from.
*/
QString QDBusMessage::interface() const
{
    return d_ptr->interface;
}

/*!
    Returns the name of the signal that was emitted or the name of the error that was
    received.
    \sa member()
*/
QString QDBusMessage::name() const
{
    return d_ptr->name;
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
    return d_ptr->service;
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
    return d_ptr->timeout;
}

/*!
    Sets the timeout for this message to be processed, given by \a ms, in milliseconds.
*/
void QDBusMessage::setTimeout(int ms)
{
    qAtomicDetach(d_ptr);
    d_ptr->timeout = ms;
}

/*!
    Returns the flag that indicates if this message should see a reply or not. This is only
    meaningful for MethodCall messages: any other kind of message cannot have replies and this
    function will always return false for them.
*/
bool QDBusMessage::noReply() const
{
    if (!d_ptr->msg)
        return false;
    return dbus_message_get_no_reply(d_ptr->msg);
}

/*!
    Returns the unique serial number assigned to this message
    or 0 if the message was not sent yet.
 */
int QDBusMessage::serialNumber() const
{
    if (!d_ptr->msg)
        return 0;
    return dbus_message_get_serial(d_ptr->msg);
}

/*!
    Returns the unique serial number assigned to the message
    that triggered this reply message.

    If this message is not a reply to another message, 0
    is returned.

 */
int QDBusMessage::replySerialNumber() const
{
    if (!d_ptr->msg)
        return 0;
    return dbus_message_get_reply_serial(d_ptr->msg);
}

/*!
    Returns true if this is a MethodCall message and a reply for it has been generated using
    QDBusMessage::methodReply or QDBusMessage::error.
*/
bool QDBusMessage::wasRepliedTo() const
{
    return d_ptr->repliedTo;
}

/*!
    Returns the signature of the signal that was received or for the output arguments
    of a method call.
*/
QString QDBusMessage::signature() const
{
    return d_ptr->signature;
}

/*!
    Sets the signature for the output arguments of this method call to be the value of \a
    signature. This function has no meaning in other types of messages or when dealing with received
    method calls.

    A message's signature indicate the type of the parameters to
    be marshalled over the bus. If there are more arguments than entries in the signature, the
    tailing arguments will be silently dropped and not sent. If there are less arguments,
    default values will be inserted (default values are those created by QVariant::convert
    when a variant of type QVariant::Invalid is converted to the type).

*/
void QDBusMessage::setSignature(const QString &signature)
{
    qAtomicDetach(d_ptr);
    d_ptr->signature = signature;
}

/*!
    Returns the connection this message was received on or an unconnected QDBusConnection object if
    this isn't a message that has been received.
*/
QDBusConnection QDBusMessage::connection() const
{
    return d_ptr->connection;
}

/*!
    Returns the message type.
*/
QDBusMessage::MessageType QDBusMessage::type() const
{
    switch (d_ptr->type) {
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
        QDBusReply<QString> reply = interface->call("RemoteMethod");
        if (reply.isSuccess())
            // use the returned value
            useValue(reply.value());
        else
            // call failed. Show an error condition.
            showError(reply.error());
    \endcode

    If the remote method call cannot fail, you can skip the error checking:
    \code
        QString reply = interface->call("RemoteMethod");
    \endcode

    However, if it does fail under those conditions, the value returned by QDBusReply::value() is
    undefined. It may be undistinguishable from a valid return value.

    QDBusReply objects are used for remote calls that have no output arguments or return values
    (i.e., they have a "void" return type). In this case, you can only test if the reply succeeded
    or not, by calling isError() and isSuccess(), and inspecting the error condition by calling
    error(). You cannot call value().

    \sa QDBusMessage, QDBusInterface
*/

/*!
    \fn QDBusReply::QDBusReply(const QDBusMessage &reply)
    Automatically construct a QDBusReply object from the reply message \a reply, extracting the
    first return value from it if it is a success reply.
*/

/*!
    \fn QDBusReply::QDBusReply(const QDBusError &error)
    Constructs an error reply from the D-Bus error code given by \a error.
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

    This function is not available if the remote call returns \c void.
*/

/*!
    \fn QDBusReply::operator Type()
    Returns the same as value().
    
    This function is not available if the remote call returns \c void.
*/

/*!
    \internal
    \fn QDBusReply::fromVariant(const QDBusReply<QVariant> &variantReply)
    Converts the QDBusReply<QVariant> object to this type by converting the variant contained in
    \a variantReply to the template's type and copying the error condition.

    If the QVariant in variantReply is not convertible to this type, it will assume an undefined
    value.
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

static void debugVariantList(QDebug dbg, const QVariantList &list);
static void debugVariantMap(QDebug dbg, const QVariantMap &map);

static void debugVariant(QDebug dbg, const QVariant &v)
{
    dbg.nospace() << v.typeName() << "(";
    switch (v.userType())
    {
    case QVariant::Bool:
        dbg.nospace() << v.toBool();
        break;
    case QMetaType::UChar:
        dbg.nospace() << qvariant_cast<uchar>(v);
        break;
    case QMetaType::Short:
        dbg.nospace() << qvariant_cast<short>(v);
        break;
    case QMetaType::UShort:
        dbg.nospace() << qvariant_cast<ushort>(v);
        break;
    case QVariant::Int:
        dbg.nospace() << v.toInt();
        break;
    case QVariant::UInt:
        dbg.nospace() << v.toUInt();
        break;
    case QVariant::LongLong:
        dbg.nospace() << v.toLongLong();
        break;
    case QVariant::ULongLong:
        dbg.nospace() << v.toULongLong();
        break;
    case QVariant::Double:
        dbg.nospace() << v.toDouble();
        break;
    case QVariant::String:
        dbg.nospace() << v.toString();
        break;
    case QVariant::ByteArray:
        dbg.nospace() << v.toByteArray();
        break;
    case QVariant::StringList:
        dbg.nospace() << v.toStringList();
        break;
    case QVariant::List:
        debugVariantList(dbg, v.toList());
        break;
    case QVariant::Map:
        debugVariantMap(dbg, v.toMap());
        break;

    default: {
        int id = v.userType();
        if (id == QDBusTypeHelper<QVariant>::id())
            debugVariant(dbg, QDBusTypeHelper<QVariant>::fromVariant(v));
        else if (id == QDBusTypeHelper<bool>::listId())
            dbg.nospace() << QDBusTypeHelper<QList<bool> >::fromVariant(v);
        else if (id == QDBusTypeHelper<short>::listId())
            dbg.nospace() << QDBusTypeHelper<QList<short> >::fromVariant(v);
        else if (id == QDBusTypeHelper<ushort>::listId())
            dbg.nospace() << QDBusTypeHelper<QList<ushort> >::fromVariant(v);
        else if (id == QDBusTypeHelper<int>::listId())
            dbg.nospace() << QDBusTypeHelper<QList<int> >::fromVariant(v);
        else if (id == QDBusTypeHelper<uint>::listId())
            dbg.nospace() << QDBusTypeHelper<QList<uint> >::fromVariant(v);
        else if (id == QDBusTypeHelper<qlonglong>::listId())
            dbg.nospace() << QDBusTypeHelper<QList<qlonglong> >::fromVariant(v);
        else if (id == QDBusTypeHelper<qulonglong>::listId())
            dbg.nospace() << QDBusTypeHelper<QList<qulonglong> >::fromVariant(v);
        else if (id == QDBusTypeHelper<double>::listId())
            dbg.nospace() << QDBusTypeHelper<QList<double> >::fromVariant(v);
        else
            dbg.nospace() << "unknown";
    }
    }
    dbg.nospace() << ")";
}    

static void debugVariantList(QDebug dbg, const QVariantList list)
{
    bool first = true;
    foreach (const QVariant &v, list) {
        if (!first)
            dbg.nospace() << ", ";
        debugVariant(dbg, v);
        first = false;
    }
}

static void debugVariantMap(QDebug dbg, const QVariantMap &map)
{
    QVariantMap::ConstIterator it = map.constBegin();
    QVariantMap::ConstIterator end = map.constEnd();
    for ( ; it != end; ++it) {
        dbg << "(" << it.key() << ", ";
        debugVariant(dbg, it.value());
        dbg << ") ";
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
                  << ", contents=(";
    debugVariantList(dbg, msg);
    dbg.nospace() << " ) )";
    return dbg.space();
}
#endif

