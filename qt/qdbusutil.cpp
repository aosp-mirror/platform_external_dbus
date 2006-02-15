/* -*- C++ -*-
 *
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "qdbusutil.h"

#include <dbus/dbus.h>

#include <QtCore/qstringlist.h>
#include <QtCore/qregexp.h>

namespace QDBusUtil
{

    bool isValidInterfaceName(const QString& ifaceName)
    {
        if (ifaceName.isEmpty() || ifaceName.length() > DBUS_MAXIMUM_NAME_LENGTH)
            return false;

        QStringList parts = ifaceName.split('.');
        if (parts.count() < 2)
            return false;           // at least two parts

        foreach (QString part, parts)
            if (!isValidMemberName(part))
                return false;

        return true;
    }

    bool isValidUniqueConnectionName(const QString &connName)
    {
        if (connName.isEmpty() || connName.length() > DBUS_MAXIMUM_NAME_LENGTH ||
            !connName.startsWith(':'))
            return false;

        QStringList parts = connName.mid(1).split('.');
        if (parts.count() < 1)
            return false;

        QRegExp regex("[a-zA-Z0-9_-]+");
        foreach (QString part, parts)
            if (!regex.exactMatch(part))
                return false;

        return true;
    }
    
    bool isValidBusName(const QString &busName)
    {
        if (busName.isEmpty() || busName.length() > DBUS_MAXIMUM_NAME_LENGTH)
            return false;

        if (busName.startsWith(':'))
            return isValidUniqueConnectionName(busName);

        QStringList parts = busName.split('.');
        if (parts.count() < 1)
            return false;

        QRegExp regex("[a-zA-Z_-][a-zA-Z0-9_-]*");
        foreach (QString part, parts)
            if (!regex.exactMatch(part))
                return false;

        return true;
    }

    bool isValidMemberName(const QString &memberName)
    {
        if (memberName.isEmpty() || memberName.length() > DBUS_MAXIMUM_NAME_LENGTH)
            return false;

        QRegExp regex("[a-zA-Z0-9_]+");
        return regex.exactMatch(memberName);
    }

    bool isValidErrorName(const QString &errorName)
    {
        return isValidInterfaceName(errorName);
    }

    bool isValidObjectPath(const QString &path)
    {
        if (path == QLatin1String("/"))
            return true;

        if (!path.startsWith('/') || path.indexOf(QLatin1String("//")) != -1 ||
            path.endsWith('/'))
            return false;

        QStringList parts = path.split('/');
        Q_ASSERT(parts.count() >= 1);
        parts.removeFirst();    // it starts with /, so we get an empty first part
 
        QRegExp regex("[a-zA-Z0-9_]+");
        foreach (QString part, parts)
            if (!regex.exactMatch(part))
                return false;

        return true;
    }

    bool isValidSignature(const QString &signature)
    {
        return dbus_signature_validate(signature.toUtf8(), 0);
    }

    bool isValidSingleSignature(const QString &signature)
    {
        return dbus_signature_validate_single(signature.toUtf8(), 0);
    }
    
} // namespace QDBusUtil
