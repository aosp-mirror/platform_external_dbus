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

#include <config.h>

#include <dbus/dbus.h>
#include <dbus/dbus-string.h>

#include "loop.h"

typedef struct BusActivation  BusActivation;
typedef struct BusConnections BusConnections;
typedef struct BusContext     BusContext;
typedef struct BusPolicy      BusPolicy;
typedef struct BusPolicyRule  BusPolicyRule;
typedef struct BusRegistry    BusRegistry;
typedef struct BusService     BusService;
typedef struct BusTransaction BusTransaction;

BusContext*     bus_context_new                      (const DBusString *config_file,
                                                      int               print_addr_fd,
                                                      DBusError        *error);
void            bus_context_shutdown                 (BusContext       *context);
void            bus_context_ref                      (BusContext       *context);
void            bus_context_unref                    (BusContext       *context);
const char*     bus_context_get_type                 (BusContext       *context);
const char*     bus_context_get_address              (BusContext       *context);
BusRegistry*    bus_context_get_registry             (BusContext       *context);
BusConnections* bus_context_get_connections          (BusContext       *context);
BusActivation*  bus_context_get_activation           (BusContext       *context);
BusLoop*        bus_context_get_loop                 (BusContext       *context);
dbus_bool_t     bus_context_allow_user               (BusContext       *context,
                                                      unsigned long     uid);
BusPolicy*      bus_context_create_connection_policy (BusContext       *context,
                                                      DBusConnection   *connection);
int             bus_context_get_activation_timeout   (BusContext       *context);

#endif /* BUS_BUS_H */
