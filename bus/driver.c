/* -*- mode: C; c-file-style: "gnu" -*- */
/* driver.c  Bus client (driver)
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

#include "connection.h"
#include "driver.h"
#include "services.h"
#include <dbus/dbus-message-internal.h>
#include <dbus/dbus-internals.h>
#include <dbus/dbus-string.h>
#include <string.h>

#define BUS_DRIVER_SERVICE_NAME "org.freedesktop.DBus"
#define BUS_DRIVER_HELLO_NAME "org.freedesktop.DBus.Hello"
#define BUS_DRIVER_WELCOME_NAME "org.freedesktop.DBus.Welcome"
#define BUS_DRIVER_LIST_SERVICES_NAME "org.freedesktop.DBus.ListServices"
#define BUS_DRIVER_SERVICES_NAME "org.freedesktop.DBus.Services"

#define BUS_DRIVER_SERVICE_CREATED_NAME "org.freedesktop.DBus.ServiceCreated"
#define BUS_DRIVER_SERVICE_DELETED_NAME "org.freedesktop.DBus.ServiceDeleted"

static dbus_bool_t  bus_driver_send_welcome_message (DBusConnection *connection,
						     DBusMessage    *hello_message);

static void
send_one_message (DBusConnection *connection, void *data)
{
  dbus_connection_send_message (connection, data, NULL, NULL);
}

static void
bus_driver_broadcast_message (DBusMessage *message)
{
  bus_connection_foreach (send_one_message, message);
}

static dbus_bool_t
bus_driver_send_service_created (DBusConnection *connection, const char *name)
{
  DBusMessage *message;

  message = dbus_message_new (NULL, BUS_DRIVER_SERVICE_CREATED_NAME);

  if (!message)
    return FALSE;

  if (!dbus_message_append_fields (message,
				   DBUS_TYPE_STRING, name,
				   0))
    {
      dbus_message_unref (message);
      return FALSE;
    }
  
  dbus_message_set_sender (message, BUS_DRIVER_SERVICE_NAME);
  bus_driver_broadcast_message (message);
  dbus_message_unref (message);
  
  return TRUE;
}

static dbus_bool_t
create_unique_client_name (const char *name,
                           DBusString *str)
{
  /* We never want to use the same unique client name twice, because
   * we want to guarantee that if you send a message to a given unique
   * name, you always get the same application. So we use two numbers
   * for INT_MAX * INT_MAX combinations, should be pretty safe against
   * wraparound.
   */
  static int next_major_number = 0;
  static int next_minor_number = 0;
  int len;

  if (!_dbus_string_append (str, name))
    return FALSE;
  
  len = _dbus_string_get_length (str);
  
  while (TRUE)
    {
      /* start out with 1-0, go to 1-1, 1-2, 1-3,
       * up to 1-MAXINT, then 2-0, 2-1, etc.
       */
      if (next_minor_number <= 0)
        {
          next_major_number += 1;
          next_minor_number = 0;
          if (next_major_number <= 0)
            _dbus_assert_not_reached ("INT_MAX * INT_MAX clients were added");
        }

      _dbus_assert (next_major_number > 0);
      _dbus_assert (next_minor_number >= 0);

      /* appname:MAJOR-MINOR */
      
      if (!_dbus_string_append (str, ":"))
        return FALSE;
      
      if (!_dbus_string_append_int (str, next_major_number))
        return FALSE;

      if (!_dbus_string_append (str, "-"))
        return FALSE;
      
      if (!_dbus_string_append_int (str, next_minor_number))
        return FALSE;

      next_minor_number += 1;
      
      /* Check if a client with the name exists */
      if (bus_service_lookup (str, FALSE) == NULL)
	break;

      /* drop the number again, try the next one. */
      _dbus_string_set_length (str, len);
    }

  return TRUE;
}

static dbus_bool_t
bus_driver_handle_hello (DBusConnection *connection,
			 DBusMessage    *message)
{
  DBusResultCode result;
  char *name;
  DBusString unique_name;
  BusService *service;
  dbus_bool_t retval;
  
  result = dbus_message_get_fields (message,
				    DBUS_TYPE_STRING, &name,
				    0);

  /* FIXME: Handle this in a better way */
  if (result != DBUS_RESULT_SUCCESS)
    return FALSE;

  if (!_dbus_string_init (&unique_name, _DBUS_INT_MAX))
    return FALSE;
  
  if (!create_unique_client_name (name, &unique_name))
    {
      _dbus_string_free (&unique_name);
      return FALSE;
    }

  /* Create the service */
  service = bus_service_lookup (&unique_name, TRUE);
  if (!service)
    {
      _dbus_string_free (&unique_name);
      return FALSE;
    }

  /* FIXME: Error checks from this point */
  
  /* Add the connection as the owner */
  bus_service_add_owner (service, connection);
  bus_connection_set_name (connection, &unique_name);

  /* We need to assign the sender to the message here */
  dbus_message_set_sender (message,
			   bus_connection_get_name (connection));
  
  _dbus_string_free (&unique_name);

  retval = bus_driver_send_welcome_message (connection, message);

  if (!retval)
    return FALSE;
  
  /* Broadcast a ServiceCreated message */
  retval = bus_driver_send_service_created (connection, bus_connection_get_name (connection));
  
  return retval;
}

static dbus_bool_t
bus_driver_send_welcome_message (DBusConnection *connection,
				 DBusMessage    *hello_message)
{
  DBusMessage *welcome;
  const char *name;
  dbus_bool_t retval;
  
  
  name = bus_connection_get_name (connection);
  _dbus_assert (name != NULL);
  
  welcome = dbus_message_new_reply (BUS_DRIVER_WELCOME_NAME,
				    hello_message);
  if (welcome == NULL)
    return FALSE;

  /* FIXME: Return value */
  dbus_message_set_sender (welcome, BUS_DRIVER_SERVICE_NAME);
  
  if (!dbus_message_append_fields (welcome,
				   DBUS_TYPE_STRING, name,
				   NULL))
    {
      dbus_message_unref (welcome);
      return FALSE;
    }

  retval = dbus_connection_send_message (connection, welcome, NULL, NULL);
  dbus_message_unref (welcome);

  return retval;
}

static void
bus_driver_handle_list_services (DBusConnection *connection,
				 DBusMessage    *message)
{
  DBusMessage *reply;
  int len, i;
  char **services;

  reply = dbus_message_new_reply (BUS_DRIVER_SERVICES_NAME, message);
  
  if (reply == NULL)
    return;

  services = bus_services_list (&len);

  if (!services)
    return;
  
  if (!dbus_message_append_fields (reply,
				   DBUS_TYPE_STRING_ARRAY, services, len,
				   0))
    goto error;

  if (!dbus_connection_send_message (connection, reply, NULL, NULL))
    goto error;
  
 error:
  dbus_message_unref (reply);
  for (i = 0; i < len; i++)
    dbus_free (services[i]);
  dbus_free (services);
}

/* This is where all the magic occurs */
static DBusHandlerResult
bus_driver_message_handler (DBusMessageHandler *handler,
			    DBusConnection     *connection,
			    DBusMessage        *message,
			    void               *user_data)
{
  const char *service, *name;

  service = dbus_message_get_service (message);
  name = dbus_message_get_name (message);

  dbus_message_set_sender (message,
			   bus_connection_get_name (connection));
  
  if (strcmp (service, BUS_DRIVER_SERVICE_NAME) == 0)
    {
      if (strcmp (name, BUS_DRIVER_HELLO_NAME) == 0)
	bus_driver_handle_hello (connection, message);
      else if (strcmp (name, BUS_DRIVER_LIST_SERVICES_NAME) == 0)
	bus_driver_handle_list_services (connection, message);
    }
  else
    {
      /* FIXME: Dispatch the message :-) */
    }

  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

dbus_bool_t
bus_driver_add_connection (DBusConnection *connection)
{
  DBusMessageHandler *handler;

  handler = dbus_message_handler_new (bus_driver_message_handler, NULL, NULL);

  if (!dbus_connection_add_filter (connection, handler))
    {
      dbus_message_handler_unref (handler);

      return FALSE;
    }

  /* FIXME we are leaking the DBusMessageHandler */
  
  _dbus_verbose ("D-Bus driver on board...\n");
  
  return TRUE;
}

void
bus_driver_remove_connection (DBusConnection *connection)
{
  BusService *service;
  DBusString service_name;
  const char *name;

  name = bus_connection_get_name (connection);

  if (name == NULL)
    return;
  
  _dbus_string_init_const (&service_name, name);
  
  service = bus_service_lookup (&service_name, FALSE);

  if (service)
    bus_service_free (service);
}
