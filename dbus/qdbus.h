/* qdbus.h precompiled header
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

#ifndef QDBUS_H
#define QDBUS_H

#include <QtCore/qglobal.h>

#ifndef DBUS_COMPILATION
# include <dbus/qdbusabstractadaptor.h>
# include <dbus/qdbusconnection.h>
# include <dbus/qdbuserror.h>
# include <dbus/qdbusinterface.h>
# include <dbus/qdbusintrospection.h>
# include <dbus/qdbusmessage.h>
# include <dbus/qdbusobject.h>
# include <dbus/qdbusreply.h>
# include <dbus/qdbusserver.h>
# include <dbus/qdbustype.h>
# include <dbus/qdbusutil.h>
#else
# include <qt/qdbusabstractadaptor.h>
# include <qt/qdbusconnection.h>
# include <qt/qdbuserror.h>
# include <qt/qdbusinterface.h>
# include <qt/qdbusintrospection.h>
# include <qt/qdbusmessage.h>
# include <qt/qdbusobject.h>
# include <qt/qdbusreply.h>
# include <qt/qdbusserver.h>
# include <qt/qdbustype.h>
# include <qt/qdbusutil.h>
#endif

#endif
