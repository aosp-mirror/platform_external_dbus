/* -*- mode: C; c-file-style: "gnu" -*- */
/* services.h  Service management
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

#ifndef BUS_SERVICES_H
#define BUS_SERVICES_H

#include <dbus/dbus.h>
#include <dbus/dbus-string.h>

/* Each service can have multiple owners; one owner is the "real
 * owner" and the others are queued up. For example, if I have
 * multiple text editors open, one might own the TextEditor service;
 * if I close that one, the next in line will become the owner of it.
 */

typedef struct BusService BusService;

typedef void (* BusServiceForeachFunction) (BusService       *service,
                                            void             *data);

BusService*     bus_service_lookup            (const DBusString          *service_name,
                                               dbus_bool_t                create_if_not_found);
dbus_bool_t     bus_service_add_owner         (BusService                *service,
                                               DBusConnection            *owner);
void            bus_service_remove_owner      (BusService                *service,
                                               DBusConnection            *owner);
DBusConnection* bus_service_get_primary_owner (BusService                *service);
const char*     bus_service_get_name          (BusService                *service);

void            bus_service_foreach           (BusServiceForeachFunction  function,
                                               void                      *data);

#endif /* BUS_SERVICES_H */
