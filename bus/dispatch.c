/* -*- mode: C; c-file-style: "gnu" -*- */
/* dispatch.c  Message dispatcher
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

#include "dispatch.h"
#include "connection.h"
#include "driver.h"
#include "utils.h"
#include <dbus/dbus-internals.h>
#include <string.h>

static int message_handler_slot;

static void
send_one_message (DBusConnection *connection, void *data)
{
  BUS_HANDLE_OOM (dbus_connection_send_message (connection, data, NULL, NULL));
}

void
bus_dispatch_broadcast_message (DBusMessage *message)
{
  _dbus_assert (dbus_message_get_sender (message) != NULL);
  
  bus_connection_foreach (send_one_message, message);
  
}

static DBusHandlerResult
bus_dispatch_message_handler (DBusMessageHandler *handler,
			      DBusConnection     *connection,
			      DBusMessage        *message,
			      void               *user_data)
{
  const char *sender, *service_name;
  
  /* Assign a sender to the message */
  sender = bus_connection_get_name (connection);
  BUS_HANDLE_OOM (dbus_message_set_sender (message, sender));

  service_name = dbus_message_get_service (message);
  
  /* See if the message is to the driver */
  if (strcmp (service_name, DBUS_SERVICE_DBUS) == 0)
    {
      bus_driver_handle_message (connection, message);
    }
  else if (sender == NULL)
    {
      _dbus_verbose ("Received message from non-registered client. Disconnecting.\n");
      dbus_connection_disconnect (connection);
    }
  else if (strcmp (service_name, DBUS_SERVICE_BROADCAST) == 0)
    {
      bus_dispatch_broadcast_message (message);
    }
  else  
    {
      DBusString service_string;
      BusService *service;

      _dbus_string_init_const (&service_string, service_name);
      service = bus_service_lookup (&service_string, FALSE);

      _dbus_assert (bus_service_get_primary_owner (service) != NULL);
      
      /* Dispatch the message */
      BUS_HANDLE_OOM (dbus_connection_send_message (bus_service_get_primary_owner (service),
						    message, NULL, NULL));
    }

  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

dbus_bool_t
bus_dispatch_add_connection (DBusConnection *connection)
{
  DBusMessageHandler *handler;
  
  message_handler_slot = dbus_connection_allocate_data_slot ();

  if (message_handler_slot < 0)
    return FALSE;

  handler = dbus_message_handler_new (bus_dispatch_message_handler, NULL, NULL);  

  if (!dbus_connection_add_filter (connection, handler))
    {
      dbus_message_handler_unref (handler);

      return FALSE;
    }

  if (!dbus_connection_set_data (connection,
				 message_handler_slot,
				 handler,
				 (DBusFreeFunction)dbus_message_handler_unref))
    {
      dbus_connection_remove_filter (connection, handler);
      dbus_message_handler_unref (handler);

      return FALSE;
    }

  return TRUE;
}

void
bus_dispatch_remove_connection (DBusConnection *connection)
{
  /* Here we tell the bus driver that we want to get off. */
  bus_driver_remove_connection (connection);

  dbus_connection_set_data (connection,
			    message_handler_slot,
			    NULL, NULL);
}

