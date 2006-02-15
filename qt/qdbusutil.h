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

#ifndef QDBUSUTIL_H
#define QDBUSUTIL_H

#include <QtCore/qstring.h>

#include "qdbusmacros.h"

namespace QDBusUtil
{
    bool isValidInterfaceName(const QString &ifaceName) QDBUS_EXPORT;

    bool isValidUniqueConnectionName(const QString &busName) QDBUS_EXPORT;

    bool isValidBusName(const QString &busName) QDBUS_EXPORT;

    bool isValidMemberName(const QString &memberName) QDBUS_EXPORT;

    bool isValidErrorName(const QString &errorName) QDBUS_EXPORT;

    bool isValidObjectPath(const QString &path) QDBUS_EXPORT;

    bool isValidSignature(const QString &signature) QDBUS_EXPORT;

    bool isValidSingleSignature(const QString &signature) QDBUS_EXPORT;
}

#endif
