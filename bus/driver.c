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
#include "dispatch.h"
#include "services.h"
#include <dbus/dbus-string.h>
#include <dbus/dbus-internals.h>
#include <string.h>

static void bus_driver_send_welcome_message (DBusConnection *connection,
					     DBusMessage    *hello_message);

void
bus_driver_send_service_deleted (const char *service_name)
{
  DBusMessage *message;

  _dbus_verbose ("sending service deleted: %s\n", service_name);
  
  _DBUS_HANDLE_OOM (message = dbus_message_new (DBUS_SERVICE_BROADCAST,
						DBUS_MESSAGE_SERVICE_DELETED));
  
  _DBUS_HANDLE_OOM (dbus_message_set_sender (message, DBUS_SERVICE_DBUS));

  _DBUS_HANDLE_OOM (dbus_message_append_fields (message,
						DBUS_TYPE_STRING, service_name,
						0));
  bus_dispatch_broadcast_message (message);
  dbus_message_unref (message);  
}

static void
bus_driver_send_service_created (const char *service_name)
{
  DBusMessage *message;

  _DBUS_HANDLE_OOM (message = dbus_message_new (DBUS_SERVICE_BROADCAST,
						DBUS_MESSAGE_SERVICE_CREATED));
  
  _DBUS_HANDLE_OOM (dbus_message_set_sender (message, DBUS_SERVICE_DBUS));

  _DBUS_HANDLE_OOM (dbus_message_append_fields (message,
						DBUS_TYPE_STRING, service_name,
						0));
  bus_dispatch_broadcast_message (message);
  dbus_message_unref (message);
}

void
bus_driver_send_service_lost (DBusConnection *connection,
			      const char *service_name)
{
  DBusMessage *message;

  _DBUS_HANDLE_OOM (message = dbus_message_new (DBUS_SERVICE_BROADCAST,
						DBUS_MESSAGE_SERVICE_LOST));
  
  _DBUS_HANDLE_OOM (dbus_message_set_sender (message, DBUS_SERVICE_DBUS));
  _DBUS_HANDLE_OOM (dbus_message_append_fields (message,
						DBUS_TYPE_STRING, service_name,
						0));
  _DBUS_HANDLE_OOM (dbus_connection_send_message (connection, message, NULL, NULL));
  
  dbus_message_unref (message);
}

void
bus_driver_send_service_acquired (DBusConnection *connection,
				  const char *service_name)
{
  DBusMessage *message;

  _DBUS_HANDLE_OOM (message = dbus_message_new (DBUS_SERVICE_BROADCAST,
						DBUS_MESSAGE_SERVICE_ACQUIRED));
  
  _DBUS_HANDLE_OOM (dbus_message_set_sender (message, DBUS_SERVICE_DBUS));
  _DBUS_HANDLE_OOM (dbus_message_append_fields (message,
						DBUS_TYPE_STRING, service_name,
						0));
  _DBUS_HANDLE_OOM (dbus_connection_send_message (connection, message, NULL, NULL));
  
  dbus_message_unref (message);
}

static dbus_bool_t
create_unique_client_name (DBusString *str)
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

static void
bus_driver_handle_hello (DBusConnection *connection,
			 DBusMessage    *message)
{
  DBusString unique_name;
  BusService *service;
  
  _DBUS_HANDLE_OOM (_dbus_string_init (&unique_name, _DBUS_INT_MAX));
  _DBUS_HANDLE_OOM (create_unique_client_name (&unique_name));
  
  /* Create the service */
  _DBUS_HANDLE_OOM (service = bus_service_lookup (&unique_name, TRUE));
  bus_service_set_prohibit_replacement (service, TRUE);
  
  /* Add the connection as the owner */
  _DBUS_HANDLE_OOM (bus_service_add_owner (service, connection));
  _DBUS_HANDLE_OOM (bus_connection_set_name (connection, &unique_name));

  _DBUS_HANDLE_OOM (dbus_message_set_sender (message,
					     bus_connection_get_name (connection)));
  
  _dbus_string_free (&unique_name);

  _DBUS_HANDLE_OOM (bus_driver_send_welcome_message (connection, message));

  /* Broadcast a service created message */
  bus_driver_send_service_created (bus_service_get_name (service));
}

static void
bus_driver_send_welcome_message (DBusConnection *connection,
				 DBusMessage    *hello_message)
{
  DBusMessage *welcome;
  const char *name;
  
  name = bus_connection_get_name (connection);
  _dbus_assert (name != NULL);
  
  _DBUS_HANDLE_OOM (welcome = dbus_message_new_reply (DBUS_MESSAGE_HELLO,
						      hello_message));
  
  _DBUS_HANDLE_OOM (dbus_message_set_sender (welcome, DBUS_SERVICE_DBUS));
  
  _DBUS_HANDLE_OOM (dbus_message_append_fields (welcome,
						DBUS_TYPE_STRING, name,
						NULL));
  
  _DBUS_HANDLE_OOM (dbus_connection_send_message (connection, welcome, NULL, NULL));

  dbus_message_unref (welcome);
}

static void
bus_driver_handle_list_services (DBusConnection *connection,
				 DBusMessage    *message)
{
  DBusMessage *reply;
  int len, i;
  char **services;

  _DBUS_HANDLE_OOM (reply = dbus_message_new_reply (DBUS_MESSAGE_LIST_SERVICES, message));

  _DBUS_HANDLE_OOM (services = bus_services_list (&len));

  _DBUS_HANDLE_OOM (dbus_message_append_fields (reply,
						DBUS_TYPE_STRING_ARRAY, services, len,
						0));

  _DBUS_HANDLE_OOM (dbus_connection_send_message (connection, reply, NULL, NULL));

  dbus_message_unref (reply);

  if (services != NULL)
    {
      for (i = 0; i < len; i++)
        dbus_free (services[i]);
      dbus_free (services);
    }
}

static void
bus_driver_handle_own_service (DBusConnection *connection,
			       DBusMessage    *message)
{
  DBusMessage *reply;
  DBusResultCode result;
  DBusString service_name;
  BusService *service;  
  char *name;
  int service_reply;
  int flags;
  
  _DBUS_HANDLE_OOM ((result = dbus_message_get_fields (message,
						       DBUS_TYPE_STRING, &name,
						       DBUS_TYPE_UINT32, &flags,
						       0)) != DBUS_RESULT_NO_MEMORY);
  
  if (result != DBUS_RESULT_SUCCESS)
    {
      dbus_free (name);
      dbus_connection_disconnect (connection);
      return;
    }

  _dbus_verbose ("Trying to own service %s with flags %d\n", name, flags);

  _dbus_string_init_const (&service_name, name);
  service = bus_service_lookup (&service_name, TRUE);

  _DBUS_HANDLE_OOM ((reply = dbus_message_new_reply (DBUS_MESSAGE_ACQUIRE_SERVICE, message)));
  
  /*
   * Check if the service already has an owner
   */
  if (bus_service_get_primary_owner (service) != NULL)
    {
      if (bus_service_has_owner (service, connection))
	service_reply = DBUS_SERVICE_REPLY_ALREADY_OWNER;
      else if (!(flags & DBUS_SERVICE_FLAG_REPLACE_EXISTING))
	service_reply = DBUS_SERVICE_REPLY_SERVICE_EXISTS;
      else
	{
	  if (bus_service_get_prohibit_replacement (service))
	    {
	      
	      /* Queue the connection */
	      _DBUS_HANDLE_OOM (bus_service_add_owner (service, connection));
	      
	      service_reply = DBUS_SERVICE_REPLY_IN_QUEUE;
	    }
	  else
	    {
	      DBusConnection *owner;
	      
	      /* We can replace the primary owner */
	      owner = bus_service_get_primary_owner (service);

	      /* We enqueue the new owner and remove the first one because
	       * that will cause ServiceAcquired and ServiceLost messages to
	       * be sent.
	       */
	      _DBUS_HANDLE_OOM (bus_service_add_owner (service, connection));
	      bus_service_remove_owner (service, owner);
	      _dbus_assert (connection == bus_service_get_primary_owner (service));
	      service_reply = DBUS_SERVICE_REPLY_PRIMARY_OWNER;
	    }
	}
    }
  else
    {
      bus_service_set_prohibit_replacement (service,
					    (flags & DBUS_SERVICE_FLAG_PROHIBIT_REPLACEMENT));
      
      /* Broadcast service created message */
      bus_driver_send_service_created (bus_service_get_name (service));
      
      _DBUS_HANDLE_OOM (bus_service_add_owner (service, connection));
			
      service_reply = DBUS_SERVICE_REPLY_PRIMARY_OWNER;
    }

  _DBUS_HANDLE_OOM (dbus_message_append_fields (reply, DBUS_TYPE_UINT32, service_reply, 0));

  /* Send service reply */
  _DBUS_HANDLE_OOM (dbus_connection_send_message (connection, reply, NULL, NULL));
  dbus_free (name);
  dbus_message_unref (reply);
}

static void
bus_driver_handle_service_exists (DBusConnection *connection,
				  DBusMessage    *message)
{
  DBusMessage *reply;
  DBusResultCode result;
  DBusString service_name;
  BusService *service;
  char *name;
  
  _DBUS_HANDLE_OOM ((result = dbus_message_get_fields (message,
						       DBUS_TYPE_STRING, &name,
						       0)) != DBUS_RESULT_NO_MEMORY);
 if (result != DBUS_RESULT_SUCCESS)
    {
      dbus_free (name);
      dbus_connection_disconnect (connection);
      return;
    }

 _dbus_string_init_const (&service_name, name);
 service = bus_service_lookup (&service_name, FALSE);
 
 _DBUS_HANDLE_OOM ((reply = dbus_message_new_reply (DBUS_MESSAGE_ACQUIRE_SERVICE, message)));
 _DBUS_HANDLE_OOM (dbus_message_set_sender (reply, DBUS_SERVICE_DBUS));

 _DBUS_HANDLE_OOM (dbus_message_append_fields (reply,
					       DBUS_TYPE_UINT32, (service != NULL ? 1 : 0),
					       0));
 _DBUS_HANDLE_OOM (dbus_connection_send_message (connection, reply, NULL, NULL));
 dbus_message_unref (reply);
 dbus_free (name);
}

void
bus_driver_handle_message (DBusConnection *connection,
			   DBusMessage    *message)
{
  const char *name, *sender;

  _dbus_verbose ("Driver got a message: %s\n",
		 dbus_message_get_name (message));
  
  name = dbus_message_get_name (message);
  sender = dbus_message_get_sender (message);

  if (sender == NULL && (strcmp (name, DBUS_MESSAGE_HELLO) != 0))
    {
      _dbus_verbose ("Trying to send a message without being registered. Disconnecting.\n");
      dbus_connection_disconnect (connection);
      return;
    }

  /* Now check names. */
  if (strcmp (name, DBUS_MESSAGE_HELLO) == 0)
    bus_driver_handle_hello (connection, message);
  else if (strcmp (name, DBUS_MESSAGE_LIST_SERVICES) == 0)
    bus_driver_handle_list_services (connection, message);
  else if (strcmp (name, DBUS_MESSAGE_ACQUIRE_SERVICE) == 0)
    bus_driver_handle_own_service (connection, message);
  else if (strcmp (name, DBUS_MESSAGE_SERVICE_EXISTS) == 0)
    bus_driver_handle_service_exists (connection, message);
  
}

void
bus_driver_remove_connection (DBusConnection *connection)
{
  /* Does nothing for now */
}
