/* -*- mode: C; c-file-style: "gnu" -*- */
/* bus.h  message bus context object
 *
 * Copyright (C) 2003 Red Hat, Inc.
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

#ifndef BUS_BUS_H
#define BUS_BUS_H

#include <dbus/dbus.h>
#include <dbus/dbus-string.h>

typedef struct BusActivation  BusActivation;
typedef struct BusConnections BusConnections;
typedef struct BusContext     BusContext;
typedef struct BusRegistry    BusRegistry;
typedef struct BusService     BusService;
typedef struct BusTransaction BusTransaction;

BusContext*     bus_context_new             (const char  *address,
                                             const char **service_dirs,
                                             DBusError   *error);
void            bus_context_shutdown        (BusContext  *context);
void            bus_context_ref             (BusContext  *context);
void            bus_context_unref           (BusContext  *context);
BusRegistry*    bus_context_get_registry    (BusContext  *context);
BusConnections* bus_context_get_connections (BusContext  *context);
BusActivation*  bus_context_get_activation  (BusContext  *context);


#endif /* BUS_BUS_H */
