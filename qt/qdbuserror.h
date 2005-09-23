/* qdbuserror.h QDBusError object
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

#ifndef QDBUSERROR_H
#define QDBUSERROR_H

#include "dbus/qdbus.h"
#include <QtCore/qstring.h>

struct DBusError;

class QDBUS_EXPORT QDBusError
{
public:
    QDBusError(const DBusError *error = 0);

    inline QString name() const { return nm; }
    inline QString message() const { return msg; }
    inline bool isValid() const { return !nm.isNull() && !msg.isNull(); }

private:
    QString nm, msg;
};

#ifndef QT_NO_DEBUG
QDebug operator<<(QDebug, const QDBusError &);
#endif

#endif
