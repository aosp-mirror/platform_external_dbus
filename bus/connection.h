/* -*- mode: C; c-file-style: "gnu" -*- */
/* connection.h  Client connections
 *
 * Copyright (C) 2003  Red Hat, Inc.
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

#ifndef BUS_CONNECTION_H
#define BUS_CONNECTION_H

#include <dbus/dbus.h>
#include "services.h"

dbus_bool_t bus_connection_init (void);

dbus_bool_t bus_connection_setup (DBusConnection *connection);


/* called by services.c */
dbus_bool_t bus_connection_add_owned_service    (DBusConnection *connection,
                                                 BusService     *service);
void        bus_connection_remove_owned_service (DBusConnection *connection,
                                                 BusService     *service);

#endif /* BUS_CONNECTION_H */
