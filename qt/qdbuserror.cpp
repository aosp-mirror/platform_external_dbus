/* qdbuserror.h QDBusError object
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

#include "qdbuserror.h"

#include <qdebug.h>

#include <dbus/dbus.h>
#include "qdbusmessage.h"

/*!
    \class QDBusError
    \brief Represents an error received from the D-Bus bus or from remote applications found in the bus.

    When dealing with the D-Bus bus service or with remote applications over D-Bus, a number of
    error conditions can happen. This error conditions are sometimes signalled by a returned error
    value or by a QDBusError.

    C++ and Java exceptions are a valid analogy for D-Bus errors: instead of returning normally with
    a return value, remote applications and the bus may decide to throw an error condition. However,
    the QtDBus implementation does not use the C++ exception-throwing mechanism, so you will receive
    QDBusErrors in the return reply (see QDBusReply::error()).

    QDBusError objects are used to inspect the error name and message as received from the bus and
    remote applications. You should not create such objects yourself to signal error conditions when
    called from D-Bus: instead, use QDBusMessage::error and QDBusConnection::send.

    \sa QDBusConnection::send, QDBusMessage, QDBusReply
*/

/*!
    \internal
    Constructs a QDBusError from a DBusError structure.
*/
QDBusError::QDBusError(const DBusError *error)
{
    if (!error || !dbus_error_is_set(error))
        return;

    nm = QString::fromUtf8(error->name);
    msg = QString::fromUtf8(error->message);
}

/*!
    \internal
    Constructs a QDBusError from a QDBusMessage.
*/
QDBusError::QDBusError(const QDBusMessage &qdmsg)
{
    if (qdmsg.type() != QDBusMessage::ErrorMessage)
        return;

    nm = qdmsg.name();
    if (qdmsg.count())
        msg = qdmsg[0].toString();
}

/*!
    \fn QDBusError::QDBusError(const QString &name, const QString &message)
    \internal

    Constructs an error by passing the name and message.
*/

/*!
    \fn QDBusError::name() const
    Returns this error's name. Error names are similar to D-Bus Interface names, like
    "org.freedesktop.DBus.InvalidArgs".
*/

/*!
    \fn QDBusError::message() const
    Returns the message that the callee associated with this error. Error messages are
    implementation defined and usually contain a human-readable error code, though this does not
    mean it is suitable for your end-users.
*/

/*!
    \fn QDBusError::isValid() const
    Returns true if this is a valid error condition (i.e., if there was an error), false otherwise.
*/

#ifndef QT_NO_DEBUG
QDebug operator<<(QDebug dbg, const QDBusError &msg)
{
    dbg.nospace() << "QDBusError(" << msg.name() << ", " << msg.message() << ")";
    return dbg.space();
}
#endif


