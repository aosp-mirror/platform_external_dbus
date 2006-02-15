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

QDBusError::QDBusError(const DBusError *error)
{
    if (!error || !dbus_error_is_set(error))
        return;

    nm = QString::fromUtf8(error->name);
    msg = QString::fromUtf8(error->message);
}

QDBusError::QDBusError(const QDBusMessage &qdmsg)
{
    if (qdmsg.type() != QDBusMessage::ErrorMessage)
        return;

    nm = qdmsg.name();
    if (qdmsg.count())
        msg = qdmsg[0].toString();
}

#ifndef QT_NO_DEBUG
QDebug operator<<(QDebug dbg, const QDBusError &msg)
{
    dbg.nospace() << "QDBusError(" << msg.name() << ", " << msg.message() << ")";
    return dbg.space();
}
#endif


