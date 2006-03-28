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

#include "qdbustype_p.h"

/*!
    \namespace QDBusUtil
    The QDBusUtil namespace contains a few functions that are of general use when dealing with D-Bus
    strings.
*/
namespace QDBusUtil
{
    /*!
        \fn QDBusUtil::isValidInterfaceName(const QString &ifaceName)
        Returns true if this is \a ifaceName is a valid interface name.

        Valid interface names must:
        \list
          \o not be empty
          \o not exceed 255 characters in length
          \o be composed of dot-separated string components that contain only ASCII letters, digits
             and the underscore ("_") character
          \o contain at least two such components
        \endlist
    */
    bool isValidInterfaceName(const QString& ifaceName)
    {
        if (ifaceName.isEmpty() || ifaceName.length() > DBUS_MAXIMUM_NAME_LENGTH)
            return false;

        QStringList parts = ifaceName.split(QLatin1Char('.'));
        if (parts.count() < 2)
            return false;           // at least two parts

        foreach (QString part, parts)
            if (!isValidMemberName(part))
                return false;

        return true;
    }

    /*!
        \fn QDBusUtil::isValidUniqueConnectionName(const QString &connName)
        Returns true if \a connName is a valid unique connection name.

        Unique connection names start with a colon (":") and are followed by a list of dot-separated
        components composed of ASCII letters, digits, the hypen or the underscore ("_") character.
    */
    bool isValidUniqueConnectionName(const QString &connName)
    {
        if (connName.isEmpty() || connName.length() > DBUS_MAXIMUM_NAME_LENGTH ||
            !connName.startsWith(QLatin1Char(':')))
            return false;

        QStringList parts = connName.mid(1).split(QLatin1Char('.'));
        if (parts.count() < 1)
            return false;

        QRegExp regex(QLatin1String("[a-zA-Z0-9_-]+"));
        foreach (QString part, parts)
            if (!regex.exactMatch(part))
                return false;

        return true;
    }

    /*!
        \fn QDBusUtil::isValidBusName(const QString &busName)
        Returns true if \a busName is a valid bus name.

        A valid bus name is either a valid unique connection name or follows the rules:
        \list
          \o is not empty
          \o does not exceed 255 characters in length
          \o be composed of dot-separated string components that contain only ASCII letters, digits,
             hyphens or underscores ("_"), but don't start with a digit
          \o contains at least two such elements
        \endlist

        \sa isValidUniqueConnectionName()
    */
    bool isValidBusName(const QString &busName)
    {
        if (busName.isEmpty() || busName.length() > DBUS_MAXIMUM_NAME_LENGTH)
            return false;

        if (busName.startsWith(QLatin1Char(':')))
            return isValidUniqueConnectionName(busName);

        QStringList parts = busName.split(QLatin1Char('.'));
        if (parts.count() < 1)
            return false;

        QRegExp regex(QLatin1String("[a-zA-Z_-][a-zA-Z0-9_-]*"));
        foreach (QString part, parts)
            if (!regex.exactMatch(part))
                return false;

        return true;
    }

    /*!
        \fn QDBusUtil::isValidMemberName(const QString &memberName)
        Returns true if \a memberName is a valid member name. A valid member name does not exceed
        255 characters in length, is not empty, is composed only of ASCII letters, digits and
        underscores, but does not start with a digit.
    */
    bool isValidMemberName(const QString &memberName)
    {
        if (memberName.isEmpty() || memberName.length() > DBUS_MAXIMUM_NAME_LENGTH)
            return false;

        QRegExp regex(QLatin1String("[a-zA-Z_][a-zA-Z0-9_]+"));
        return regex.exactMatch(memberName);
    }

    /*!
        \fn QDBusUtil::isValidErrorName(const QString &errorName)
        Returns true if \a errorName is a valid error name. Valid error names are valid interface
        names and vice-versa, so this function is actually an alias for isValidInterfaceName.
    */
    bool isValidErrorName(const QString &errorName)
    {
        return isValidInterfaceName(errorName);
    }

    /*!
        \fn QDBusUtil::isValidObjectPath(const QString &path)
        Returns true if \a path is valid object path.

        Valid object paths follow the rules:
        \list
          \o start with the slash character ("/")
          \o do not end in a slash, unless the path is just the initial slash
          \o do not contain any two slashes in sequence
          \o contain slash-separated parts, each of which is composed of ASCII letters, digits and
             underscores ("_")
        \endlist
    */
    bool isValidObjectPath(const QString &path)
    {
        if (path == QLatin1String("/"))
            return true;

        if (!path.startsWith(QLatin1Char('/')) || path.indexOf(QLatin1String("//")) != -1 ||
            path.endsWith(QLatin1Char('/')))
            return false;

        QStringList parts = path.split(QLatin1Char('/'));
        Q_ASSERT(parts.count() >= 1);
        parts.removeFirst();    // it starts with /, so we get an empty first part

        QRegExp regex(QLatin1String("[a-zA-Z0-9_]+"));
        foreach (QString part, parts)
            if (!regex.exactMatch(part))
                return false;

        return true;
    }

    /*!
        \fn QDBusUtil::isValidSignature(const QString &signature)
        Returns true if \a signature is a valid D-Bus type signature for one or more types.
        This function returns true if it can all of \a signature into valid, individual types and no
        characters remain in \a signature.

        \sa isValidSingleSignature()
    */
    bool isValidSignature(const QString &signature)
    {
        return dbus_signature_validate(signature.toUtf8(), 0);
    }

    /*!
        \fn QDBusUtil::isValidSingleSignature(const QString &signature)
        Returns true if \a signature is a valid D-Bus type signature for exactly one full type. This
        function tries to convert the type signature into a D-Bus type and, if it succeeds and no
        characters remain in the signature, it returns true.
    */
    bool isValidSingleSignature(const QString &signature)
    {
        return dbus_signature_validate_single(signature.toUtf8(), 0);
    }

    /*!
        \fn QDBusUtil::signatureToType(const QString &signature)
        Returns the Qt meta type id for the given D-Bus signature for exactly one full type, given
        by \a signature.

        \sa isValidSingleSignature(), typeToSignature(), QVariant::type(), QVariant::userType()
    */
    QVariant::Type signatureToType(const QString &signature)
    {
        return QVariant::Type( QDBusType::qvariantType(signature.toLatin1().constData()) );
    }

    /*!
        \fn QDBusUtil::typeToSignature(QVariant::Type type)
        Returns the D-Bus signature equivalent to the supplied meta type id \a type.

        \sa isValidSingleSignature(), signatureToType(), QVariant::type(), QVariant::userType()
    */
    const char *typeToSignature(QVariant::Type type)
    {
        return QDBusType::dbusSignature( type );
    }

} // namespace QDBusUtil
