/* -*- mode: C; c-file-style: "gnu" -*- */
/* connection.c  Client connections
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
#include "connection.h"
#include "loop.h"
#include "services.h"
#include <dbus/dbus-list.h>

static int connection_data_slot;
static DBusList *connections = NULL;

typedef struct
{
  DBusList *services_owned;

} BusConnectionData;

#define BUS_CONNECTION_DATA(connection) (dbus_connection_get_data ((connection), connection_data_slot))

static void
connection_disconnect_handler (DBusConnection *connection,
                               void           *data)
{
  BusConnectionData *d;
  BusService *service;

  _dbus_warn ("Disconnected\n");

  d = BUS_CONNECTION_DATA (connection);
  _dbus_assert (d != NULL);  

  /* Drop any service ownership */
  while ((service = _dbus_list_get_last (&d->services_owned)))
    bus_service_remove_owner (service, connection);

  /* no more watching */
  dbus_connection_set_watch_functions (connection,
                                       NULL, NULL,
                                       connection,
                                       NULL);
  
  dbus_connection_set_data (connection,
                            connection_data_slot,
                            NULL, NULL);
  
  _dbus_list_remove (&connections, connection);    
  dbus_connection_unref (connection);
}

static void
connection_watch_callback (DBusWatch     *watch,
                           unsigned int   condition,
                           void          *data)
{
  DBusConnection *connection = data;

  dbus_connection_handle_watch (connection, watch, condition);
}

static void
add_connection_watch (DBusWatch      *watch,
                      DBusConnection *connection)
{
  bus_loop_add_watch (watch, connection_watch_callback, connection,
                      NULL);
}

static void
remove_connection_watch (DBusWatch      *watch,
                         DBusConnection *connection)
{
  bus_loop_remove_watch (watch, connection_watch_callback, connection);
}

static void
free_connection_data (void *data)
{
  BusConnectionData *d = data;

  /* services_owned should be NULL since we should be disconnected */
  _dbus_assert (d->services_owned == NULL);
  
  dbus_free (d);
}

dbus_bool_t
bus_connection_init (void)
{
  connection_data_slot = dbus_connection_allocate_data_slot ();

  if (connection_data_slot < 0)
    return FALSE;

  return TRUE;
}

dbus_bool_t
bus_connection_setup (DBusConnection *connection)
{
  BusConnectionData *d;

  d = dbus_new0 (BusConnectionData, 1);
  
  if (d == NULL)
    return FALSE;
  
  if (!dbus_connection_set_data (connection,
                                 connection_data_slot,
                                 d, free_connection_data))
    {
      dbus_free (d);
      return FALSE;
    }
  
  if (!_dbus_list_append (&connections, connection))
    {
      /* this will free our data when connection gets finalized */
      dbus_connection_disconnect (connection);
      return FALSE;
    }

  dbus_connection_ref (connection);
  
  dbus_connection_set_watch_functions (connection,
                                       (DBusAddWatchFunction) add_connection_watch,
                                       (DBusRemoveWatchFunction) remove_connection_watch,
                                       connection,
                                       NULL);
  
  dbus_connection_set_disconnect_function (connection,
                                           connection_disconnect_handler,
                                           NULL, NULL);

  return TRUE;
}

dbus_bool_t
bus_connection_add_owned_service (DBusConnection *connection,
                                  BusService     *service)
{
  BusConnectionData *d;

  d = BUS_CONNECTION_DATA (connection);
  _dbus_assert (d != NULL);

  if (!_dbus_list_append (&d->services_owned,
                          service))
    return FALSE;

  return TRUE;
}

void
bus_connection_remove_owned_service (DBusConnection *connection,
                                     BusService     *service)
{
  BusConnectionData *d;

  d = BUS_CONNECTION_DATA (connection);
  _dbus_assert (d != NULL);

  _dbus_list_remove_last (&d->services_owned, service);
}
