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
#include <dbus/dbus-list.h>
#include "bus.h"

typedef dbus_bool_t (* BusConnectionForeachFunction) (DBusConnection *connection, 
                                                      void           *data);


BusConnections* bus_connections_new               (BusContext                   *context);
void            bus_connections_ref               (BusConnections               *connections);
void            bus_connections_unref             (BusConnections               *connections);
dbus_bool_t     bus_connections_setup_connection  (BusConnections               *connections,
                                                   DBusConnection               *connection);
void            bus_connections_foreach           (BusConnections               *connections,
                                                   BusConnectionForeachFunction  function,
                                                   void                         *data);
void            bus_connections_foreach_active    (BusConnections               *connections,
                                                   BusConnectionForeachFunction  function,
                                                   void                         *data);
BusContext*     bus_connections_get_context       (BusConnections               *connections);
BusContext*     bus_connection_get_context        (DBusConnection               *connection);
BusConnections* bus_connection_get_connections    (DBusConnection               *connection);
BusRegistry*    bus_connection_get_registry       (DBusConnection               *connection);
BusActivation*  bus_connection_get_activation     (DBusConnection               *connection);
dbus_bool_t     bus_connections_check_limits      (BusConnections               *connections,
                                                   DBusConnection               *requesting_completion,
                                                   DBusError                    *error);
void            bus_connections_expire_incomplete (BusConnections               *connections);



dbus_bool_t bus_connection_is_active (DBusConnection *connection);
const char *bus_connection_get_name  (DBusConnection *connection);

dbus_bool_t bus_connection_preallocate_oom_error (DBusConnection *connection);
void        bus_connection_send_oom_error        (DBusConnection *connection,
                                                  DBusMessage    *in_reply_to);


/* called by services.c */
dbus_bool_t bus_connection_add_owned_service      (DBusConnection *connection,
                                                   BusService     *service);
void        bus_connection_remove_owned_service   (DBusConnection *connection,
                                                   BusService     *service);
void        bus_connection_add_owned_service_link (DBusConnection *connection,
                                                   DBusList       *link);
int         bus_connection_get_n_services_owned   (DBusConnection *connection);

/* called by driver.c */
dbus_bool_t bus_connection_complete (DBusConnection               *connection,
				     const DBusString             *name,
                                     DBusError                    *error);

/* called by dispatch.c when the connection is dropped */
void        bus_connection_disconnected (DBusConnection *connection);

dbus_bool_t      bus_connection_is_in_group (DBusConnection       *connection,
                                             unsigned long         gid);
dbus_bool_t      bus_connection_get_groups  (DBusConnection       *connection,
                                             unsigned long       **groups,
                                             int                  *n_groups,
                                             DBusError            *error);
BusClientPolicy* bus_connection_get_policy  (DBusConnection       *connection);

/* transaction API so we can send or not send a block of messages as a whole */

typedef void (* BusTransactionCancelFunction) (void *data);

BusTransaction* bus_transaction_new              (BusContext                   *context);
BusContext*     bus_transaction_get_context      (BusTransaction               *transaction);
BusConnections* bus_transaction_get_connections  (BusTransaction               *transaction);
dbus_bool_t     bus_transaction_send             (BusTransaction               *transaction,
                                                  DBusConnection               *connection,
                                                  DBusMessage                  *message);
dbus_bool_t     bus_transaction_send_from_driver (BusTransaction               *transaction,
                                                  DBusConnection               *connection,
                                                  DBusMessage                  *message);
dbus_bool_t     bus_transaction_send_error_reply (BusTransaction               *transaction,
                                                  DBusConnection               *connection,
                                                  const DBusError              *error,
                                                  DBusMessage                  *in_reply_to);
void            bus_transaction_cancel_and_free  (BusTransaction               *transaction);
void            bus_transaction_execute_and_free (BusTransaction               *transaction);
dbus_bool_t     bus_transaction_add_cancel_hook  (BusTransaction               *transaction,
                                                  BusTransactionCancelFunction  cancel_function,
                                                  void                         *data,
                                                  DBusFreeFunction              free_data_function);

#endif /* BUS_CONNECTION_H */
