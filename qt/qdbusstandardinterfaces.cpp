/* -*- C++ -*-
 *
 * Copyright (C) 2005 Thiago Macieira <thiago@kde.org>
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

#include "qdbusstandardinterfaces.h"

QDBusPeerInterface::~QDBusPeerInterface()
{
}

QDBusIntrospectableInterface::~QDBusIntrospectableInterface()
{
}

QDBusPropertiesInterface::~QDBusPropertiesInterface()
{
}

QDBusBusInterface::~QDBusBusInterface()
{
}

const char* QDBusBusInterface::staticIntrospectionData()
{
    // FIXME!
    // This should be auto-generated!

    return
        "<interface name=\"org.freedesktop.DBus\">"
        "<method name=\"RequestName\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"in\" type=\"u\"/>"
        "<arg direction=\"out\" type=\"u\"/>"
        "</method>"
        "<method name=\"ReleaseName\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"u\"/>"
        "</method>"
        "<method name=\"StartServiceByName\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"in\" type=\"u\"/>"
        "<arg direction=\"out\" type=\"u\"/>"
        "</method>"
        "<method name=\"Hello\">"
        "<arg direction=\"out\" type=\"s\"/>"
        "</method>"
        "<method name=\"NameHasOwner\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"b\"/>"
        "</method>"
        "<method name=\"ListNames\">"
        "<arg direction=\"out\" type=\"as\"/>"
        "</method>"
        "<method name=\"AddMatch\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "</method>"
        "<method name=\"RemoveMatch\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "</method>"
        "<method name=\"GetNameOwner\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"s\"/>"
        "</method>"
        "<method name=\"ListQueuedOwners\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"as\"/>"
        "</method>"
        "<method name=\"GetConnectionUnixUser\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"u\"/>"
        "</method>"
        "<method name=\"GetConnectionUnixProcessID\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"u\"/>"
        "</method>"
        "<method name=\"GetConnectionSELinuxSecurityContext\">"
        "<arg direction=\"in\" type=\"s\"/>"
        "<arg direction=\"out\" type=\"ay\"/>"
        "</method>"
        "<method name=\"ReloadConfig\">"
        "</method>"
        "<signal name=\"NameOwnerChanged\">"
        "<arg type=\"s\"/>"
        "<arg type=\"s\"/>"
        "<arg type=\"s\"/>"
        "</signal>"
        "<signal name=\"NameLost\">"
        "<arg type=\"s\"/>"
        "</signal>"
        "<signal name=\"NameAcquired\">"
        "<arg type=\"s\"/>"
        "</signal>"
        "</interface>";
}
