/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-bus.h  Convenience functions for communicating with the bus.
 *
 * Copyright (C) 2003  CodeFactory AB
 *
 * Licensed under the Academic Free License version 1.2
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
#if !defined (DBUS_INSIDE_DBUS_H) && !defined (DBUS_COMPILATION)
#error "Only <dbus/dbus.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef DBUS_BUS_H
#define DBUS_BUS_H

#include <dbus/dbus-connection.h>

DBUS_BEGIN_DECLS;

typedef enum
{
  DBUS_BUS_SESSION,    /**< The login session bus */
  DBUS_BUS_SYSTEM,     /**< The systemwide bus */
  DBUS_BUS_ACTIVATION  /**< The bus that activated us, if any */
} DBusBusType;

DBusConnection *dbus_bus_get              (DBusBusType     type,
					   DBusError      *error);
dbus_bool_t     dbus_bus_register         (DBusConnection *connection,
					   DBusError      *error);
dbus_bool_t     dbus_bus_set_base_service (DBusConnection *connection,
					   const char     *base_service);
const char*     dbus_bus_get_base_service (DBusConnection *connection);
int             dbus_bus_acquire_service  (DBusConnection *connection,
					   const char     *service_name,
					   unsigned int    flags,
					   DBusError      *error);
dbus_bool_t     dbus_bus_service_exists   (DBusConnection *connection,
					   const char     *service_name,
					   DBusError      *error);

DBUS_END_DECLS;

#endif /* DBUS_BUS_H */
