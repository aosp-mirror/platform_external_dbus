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

#include "activation.h"
#include "connection.h"
#include "driver.h"
#include "dispatch.h"
#include "services.h"
#include "utils.h"
#include <dbus/dbus-string.h>
#include <dbus/dbus-internals.h>
#include <string.h>

static dbus_bool_t bus_driver_send_welcome_message (DBusConnection *connection,
                                                    DBusMessage    *hello_message,
                                                    BusTransaction *transaction,
                                                    DBusError      *error);

dbus_bool_t
bus_driver_send_service_deleted (const char     *service_name,
                                 BusTransaction *transaction,
                                 DBusError      *error)
{
  DBusMessage *message;
  dbus_bool_t retval;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  _dbus_verbose ("sending service deleted: %s\n", service_name);

  message = dbus_message_new (DBUS_SERVICE_BROADCAST,
                              DBUS_MESSAGE_SERVICE_DELETED);
  if (message == NULL)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_set_sender (message, DBUS_SERVICE_DBUS) ||
      !dbus_message_append_args (message,
                                 DBUS_TYPE_STRING, service_name,
                                 0))
    {
      dbus_message_unref (message);
      BUS_SET_OOM (error);
      return FALSE;
    }

  retval = bus_dispatch_broadcast_message (transaction, message, error);
  dbus_message_unref (message);

  return retval;
}

dbus_bool_t
bus_driver_send_service_created (const char     *service_name,
                                 BusTransaction *transaction,
                                 DBusError      *error)
{
  DBusMessage *message;
  dbus_bool_t retval;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  message = dbus_message_new (DBUS_SERVICE_BROADCAST,
                              DBUS_MESSAGE_SERVICE_CREATED);
  if (message == NULL)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_set_sender (message, DBUS_SERVICE_DBUS))
    {
      dbus_message_unref (message);
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_append_args (message,
                                 DBUS_TYPE_STRING, service_name,
                                 0))
    {
      dbus_message_unref (message);
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  retval = bus_dispatch_broadcast_message (transaction, message, error);
  dbus_message_unref (message);

  return retval;
}

dbus_bool_t
bus_driver_send_service_lost (DBusConnection *connection,
			      const char     *service_name,
                              BusTransaction *transaction,
                              DBusError      *error)
{
  DBusMessage *message;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  message = dbus_message_new (bus_connection_get_name (connection),
                              DBUS_MESSAGE_SERVICE_LOST);
  if (message == NULL)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_set_sender (message, DBUS_SERVICE_DBUS))
    {
      dbus_message_unref (message);
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_append_args (message,
                                 DBUS_TYPE_STRING, service_name,
                                 0))
    {
      dbus_message_unref (message);
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!bus_transaction_send_message (transaction, connection, message))
    {
      dbus_message_unref (message);
      BUS_SET_OOM (error);
      return FALSE;
    }
  else
    {
      dbus_message_unref (message);
      return TRUE;
    }
}

dbus_bool_t
bus_driver_send_service_acquired (DBusConnection *connection,
                                  const char     *service_name,
                                  BusTransaction *transaction,
                                  DBusError      *error)
{
  DBusMessage *message;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  message = dbus_message_new (bus_connection_get_name (connection),
                              DBUS_MESSAGE_SERVICE_ACQUIRED);
  if (message == NULL)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_set_sender (message, DBUS_SERVICE_DBUS))
    {
      dbus_message_unref (message);
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_append_args (message,
                                 DBUS_TYPE_STRING, service_name,
                                 0))
    {
      dbus_message_unref (message);
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!bus_transaction_send_message (transaction, connection, message))
    {
      dbus_message_unref (message);
      BUS_SET_OOM (error);
      return FALSE;
    }
  else
    {
      dbus_message_unref (message);
      return TRUE;
    }
}

static dbus_bool_t
create_unique_client_name (BusRegistry *registry,
                           DBusString  *str)
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
      if (bus_registry_lookup (registry, str) == NULL)
	break;

      /* drop the number again, try the next one. */
      _dbus_string_set_length (str, len);
    }

  return TRUE;
}

static dbus_bool_t
bus_driver_handle_hello (DBusConnection *connection,
                         BusTransaction *transaction,
                         DBusMessage    *message,
                         DBusError      *error)
{
  DBusString unique_name;
  BusService *service;
  dbus_bool_t retval;
  BusRegistry *registry;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  if (!_dbus_string_init (&unique_name))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  retval = FALSE;

  registry = bus_connection_get_registry (connection);
  
  if (!create_unique_client_name (registry, &unique_name))
    {
      BUS_SET_OOM (error);
      goto out_0;
    }

  if (!bus_connection_set_name (connection, &unique_name))
    {
      BUS_SET_OOM (error);
      goto out_0;
    }
  
  if (!dbus_message_set_sender (message,
                                bus_connection_get_name (connection)))
    {
      BUS_SET_OOM (error);
      goto out_0;
    }
  
  if (!bus_driver_send_welcome_message (connection, message, transaction, error))
    goto out_0;

  /* Create the service */
  service = bus_registry_ensure (registry,
                                 &unique_name, connection, transaction, error);
  if (service == NULL)
    goto out_0;
  
  bus_service_set_prohibit_replacement (service, TRUE);

  retval = TRUE;
  
 out_0:
  _dbus_string_free (&unique_name);
  return retval;
}

static dbus_bool_t
bus_driver_send_welcome_message (DBusConnection *connection,
                                 DBusMessage    *hello_message,
                                 BusTransaction *transaction,
                                 DBusError      *error)
{
  DBusMessage *welcome;
  const char *name;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  name = bus_connection_get_name (connection);
  _dbus_assert (name != NULL);
  
  welcome = dbus_message_new_reply (hello_message);
  if (welcome == NULL)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_set_sender (welcome, DBUS_SERVICE_DBUS))
    {
      dbus_message_unref (welcome);
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_append_args (welcome,
                                 DBUS_TYPE_STRING, name,
                                 NULL))
    {
      dbus_message_unref (welcome);
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!bus_transaction_send_message (transaction, connection, welcome))
    {
      dbus_message_unref (welcome);
      BUS_SET_OOM (error);
      return FALSE;
    }
  else
    {
      dbus_message_unref (welcome);
      return TRUE;
    }
}

static dbus_bool_t
bus_driver_handle_list_services (DBusConnection *connection,
                                 BusTransaction *transaction,
                                 DBusMessage    *message,
                                 DBusError      *error)
{
  DBusMessage *reply;
  int len;
  char **services;
  BusRegistry *registry;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  registry = bus_connection_get_registry (connection);
  
  reply = dbus_message_new_reply (message);
  if (reply == NULL)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!bus_registry_list_services (registry, &services, &len))
    {
      dbus_message_unref (reply);
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_append_args (reply,
                                 DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, services, len,
                                 0))
    {
      dbus_free_string_array (services);
      dbus_message_unref (reply);
      BUS_SET_OOM (error);
      return FALSE;
    }

  dbus_free_string_array (services);
  
  if (!bus_transaction_send_message (transaction, connection, reply))
    {
      dbus_message_unref (reply);
      BUS_SET_OOM (error);
      return FALSE;
    }
  else
    {
      dbus_message_unref (reply);
      return TRUE;
    }
}

static dbus_bool_t
bus_driver_handle_acquire_service (DBusConnection *connection,
                                   BusTransaction *transaction,
                                   DBusMessage    *message,
                                   DBusError      *error)
{
  DBusMessage *reply;
  DBusString service_name;
  BusService *service;  
  char *name;
  int service_reply;
  int flags;
  dbus_bool_t retval;
  DBusConnection *old_owner;
  DBusConnection *current_owner;
  BusRegistry *registry;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  registry = bus_connection_get_registry (connection);
  
  if (!dbus_message_get_args (message, error,
                              DBUS_TYPE_STRING, &name,
                              DBUS_TYPE_UINT32, &flags,
                              0))
    return FALSE;
  
  _dbus_verbose ("Trying to own service %s with flags 0x%x\n", name, flags);
  
  retval = FALSE;
  reply = NULL;
  
  if (*name == ':')
    {
      /* Not allowed; only base services can start with ':' */
      dbus_set_error (error, DBUS_ERROR_ACCESS_DENIED,
                      "Cannot acquire a service starting with ':' such as \"%s\"",
                      name);
      
      goto out;
    }

  _dbus_string_init_const (&service_name, name);
  
  service = bus_registry_lookup (registry, &service_name);

  if (service != NULL)
    old_owner = bus_service_get_primary_owner (service);
  else
    old_owner = NULL;  
  
  reply = dbus_message_new_reply (message);
  if (reply == NULL)
    {
      BUS_SET_OOM (error);
      goto out;
    }

  if (!dbus_message_set_sender (reply, DBUS_SERVICE_DBUS))
    {
      BUS_SET_OOM (error);
      goto out;
    }
      
  if (service == NULL)
    {
      service = bus_registry_ensure (registry,
                                     &service_name, connection, transaction, error);
      if (service == NULL)
        goto out;
    }

  current_owner = bus_service_get_primary_owner (service);

  if (old_owner == NULL)
    {
      _dbus_assert (current_owner == connection);

      bus_service_set_prohibit_replacement (service,
					    (flags & DBUS_SERVICE_FLAG_PROHIBIT_REPLACEMENT));      
			
      service_reply = DBUS_SERVICE_REPLY_PRIMARY_OWNER;      
    }
  else if (old_owner == connection)
    service_reply = DBUS_SERVICE_REPLY_ALREADY_OWNER;
  else if (!((flags & DBUS_SERVICE_FLAG_REPLACE_EXISTING)))
    service_reply = DBUS_SERVICE_REPLY_SERVICE_EXISTS;
  else if (bus_service_get_prohibit_replacement (service))
    {
      /* Queue the connection */
      if (!bus_service_add_owner (service, connection,
                                  transaction, error))
        goto out;
      
      service_reply = DBUS_SERVICE_REPLY_IN_QUEUE;
    }
  else
    {
      /* Replace the current owner */

      /* We enqueue the new owner and remove the first one because
       * that will cause ServiceAcquired and ServiceLost messages to
       * be sent.
       */
      
      /* FIXME this is broken, if the remove_owner fails
       * we don't undo the add_owner
       * (easiest fix is probably to move all this to
       * services.c and have a single routine for it)
       */
      
      if (!bus_service_add_owner (service, connection,
                                  transaction, error))
        goto out;
      
      if (!bus_service_remove_owner (service, old_owner,
                                     transaction, error))
        goto out;
      
      _dbus_assert (connection == bus_service_get_primary_owner (service));
      service_reply = DBUS_SERVICE_REPLY_PRIMARY_OWNER;
    }

  if (!dbus_message_append_args (reply, DBUS_TYPE_UINT32, service_reply, 0))
    {
      BUS_SET_OOM (error);
      goto out;
    }

  if (!bus_transaction_send_message (transaction, connection, reply))
    {
      BUS_SET_OOM (error);
      goto out;
    }

  retval = TRUE;
  
 out:
  dbus_free (name);
  if (reply)
    dbus_message_unref (reply);
  return retval;
} 

static dbus_bool_t
bus_driver_handle_service_exists (DBusConnection *connection,
                                  BusTransaction *transaction,
                                  DBusMessage    *message,
                                  DBusError      *error)
{
  DBusMessage *reply;
  DBusString service_name;
  BusService *service;
  char *name;
  dbus_bool_t retval;
  BusRegistry *registry;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  registry = bus_connection_get_registry (connection);
  
  if (!dbus_message_get_args (message, error,
                              DBUS_TYPE_STRING, &name,
                              0))
    return FALSE;

  retval = FALSE;
  
  _dbus_string_init_const (&service_name, name);
  service = bus_registry_lookup (registry, &service_name);
 
  reply = dbus_message_new_reply (message);
  if (reply == NULL)
    {
      BUS_SET_OOM (error);
      goto out;
    }
  
  if (!dbus_message_set_sender (reply, DBUS_SERVICE_DBUS))
    {
      BUS_SET_OOM (error);
      goto out;
    }

  if (!dbus_message_append_args (reply,
                                 DBUS_TYPE_UINT32, service != NULL,
                                 0))
    {
      BUS_SET_OOM (error);
      goto out;
    }

  if (!bus_transaction_send_message (transaction, connection, reply))
    {
      BUS_SET_OOM (error);
      goto out;
    }

  retval = TRUE;
  
 out:
  if (reply)
    dbus_message_unref (reply);
  dbus_free (name);

  return retval;
}

static dbus_bool_t
bus_driver_handle_activate_service (DBusConnection *connection,
                                    BusTransaction *transaction,
                                    DBusMessage    *message,
                                    DBusError      *error)
{
  dbus_uint32_t flags;
  char *name;
  dbus_bool_t retval;
  BusActivation *activation;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  activation = bus_connection_get_activation (connection);
  
  if (!dbus_message_get_args (message, error,
                              DBUS_TYPE_STRING, &name,
                              DBUS_TYPE_UINT32, &flags,
                              0))
    return FALSE;

  retval = FALSE;

  if (!bus_activation_activate_service (activation, connection, transaction,
                                        message, name, error))
    goto out;

  retval = TRUE;
  
 out:
  dbus_free (name);
  return retval;
}

/* For speed it might be useful to sort this in order of
 * frequency of use (but doesn't matter with only a few items
 * anyhow)
 */
struct
{
  const char *name;
  dbus_bool_t (* handler) (DBusConnection *connection,
                           BusTransaction *transaction,
                           DBusMessage    *message,
                           DBusError      *error);
} message_handlers[] = {
  { DBUS_MESSAGE_ACQUIRE_SERVICE, bus_driver_handle_acquire_service },
  { DBUS_MESSAGE_ACTIVATE_SERVICE, bus_driver_handle_activate_service },
  { DBUS_MESSAGE_HELLO, bus_driver_handle_hello },
  { DBUS_MESSAGE_SERVICE_EXISTS, bus_driver_handle_service_exists },
  { DBUS_MESSAGE_LIST_SERVICES, bus_driver_handle_list_services }
};

dbus_bool_t
bus_driver_handle_message (DBusConnection *connection,
                           BusTransaction *transaction,
			   DBusMessage    *message,
                           DBusError      *error)
{
  const char *name, *sender;
  int i;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  _dbus_verbose ("Driver got a message: %s\n",
		 dbus_message_get_name (message));
  
  name = dbus_message_get_name (message);
  sender = dbus_message_get_sender (message);

  if (sender == NULL && (strcmp (name, DBUS_MESSAGE_HELLO) != 0))
    {
      dbus_set_error (error, DBUS_ERROR_ACCESS_DENIED,
                      "Client tried to send a message other than %s without being registered",
                      DBUS_MESSAGE_HELLO);

      dbus_connection_disconnect (connection);
      return FALSE;
    }

  i = 0;
  while (i < _DBUS_N_ELEMENTS (message_handlers))
    {
      if (strcmp (message_handlers[i].name, name) == 0)
        {
          if ((* message_handlers[i].handler) (connection, transaction, message, error))
            return TRUE;
          else
            return FALSE;
        }
      
      ++i;
    }

  dbus_set_error (error, DBUS_ERROR_UNKNOWN_MESSAGE,
                  "%s does not understand message %s",
                  DBUS_SERVICE_DBUS, name);
  
  return FALSE;
}

void
bus_driver_remove_connection (DBusConnection *connection)
{
  /* FIXME Does nothing for now, should unregister the connection
   * with the bus driver.
   */
}
