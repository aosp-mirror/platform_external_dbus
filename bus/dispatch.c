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

typedef struct
{
  DBusMessage    *message;
  BusTransaction *transaction;
  DBusError      *error;
} SendMessageData;

static dbus_bool_t
send_one_message (DBusConnection *connection, void *data)
{
  SendMessageData *d = data;
  
  if (!bus_connection_is_active (connection))
    return TRUE;

  if (!bus_transaction_send_message (d->transaction,
                                     connection,
                                     d->message))
    {
      BUS_SET_OOM (d->error);
      return FALSE;
    }

  return TRUE;
}

dbus_bool_t
bus_dispatch_broadcast_message (BusTransaction *transaction,
                                DBusMessage    *message,
                                DBusError      *error)
{
  DBusError tmp_error;
  SendMessageData d;
  
  _dbus_assert (dbus_message_get_sender (message) != NULL);

  dbus_error_init (&tmp_error);
  d.message = message;
  d.transaction = transaction;
  d.error = &tmp_error;
  bus_connection_foreach (send_one_message, &d);

  if (dbus_error_is_set (&tmp_error))
    {
      dbus_move_error (&tmp_error, error);
      return FALSE;
    }
  else
    return TRUE;
}

static dbus_bool_t
send_service_nonexistent_error (BusTransaction *transaction,
                                DBusConnection *connection,
                                const char     *service_name,
                                DBusMessage    *in_reply_to,
                                DBusError      *error)
{
  DBusMessage *error_reply;
  DBusString error_message;
  const char *error_str;
	  
  /* Trying to send a message to a non-existant service,
   * bounce back an error message.
   */
	  
  if (!_dbus_string_init (&error_message, _DBUS_INT_MAX))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!_dbus_string_append (&error_message, "Service \"") ||
      !_dbus_string_append (&error_message, service_name) ||
      !_dbus_string_append (&error_message, "does not exist"))
    {
      _dbus_string_free (&error_message);
      BUS_SET_OOM (error);
      return FALSE;
    }
              
  _dbus_string_get_const_data (&error_message, &error_str);
  error_reply = dbus_message_new_error_reply (in_reply_to,
                                              DBUS_ERROR_SERVICE_DOES_NOT_EXIST,
                                              error_str);

  _dbus_string_free (&error_message);
              
  if (error_reply == NULL)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
              
  if (!bus_transaction_send_message (transaction, connection, error_reply))
    {
      dbus_message_unref (error_reply);
      BUS_SET_OOM (error);
      return FALSE;
    }
              
  dbus_message_unref (error_reply);

  return TRUE;
}

static DBusHandlerResult
bus_dispatch_message_handler (DBusMessageHandler *handler,
			      DBusConnection     *connection,
			      DBusMessage        *message,
			      void               *user_data)
{
  const char *sender, *service_name, *message_name;
  DBusError error;
  BusTransaction *transaction;

  transaction = NULL;
  dbus_error_init (&error);
  
  /* If we can't even allocate an OOM error, we just go to sleep
   * until we can.
   */
  while (!bus_connection_preallocate_oom_error (connection))
    bus_wait_for_memory ();
  
  /* Ref connection in case we disconnect it at some point in here */
  dbus_connection_ref (connection);

  service_name = dbus_message_get_service (message);
  message_name = dbus_message_get_name (message);

  _dbus_assert (message_name != NULL); /* DBusMessageLoader is supposed to check this */

  /* If service_name is NULL, this is a message to the bus daemon, not intended
   * to actually go "on the bus"; e.g. a peer-to-peer ping. Handle these
   * immediately, especially disconnection messages.
   */
  if (service_name == NULL)
    {
      if (strcmp (message_name, DBUS_MESSAGE_LOCAL_DISCONNECT) == 0)
        bus_connection_disconnected (connection);

      /* DBusConnection also handles some of these automatically, we leave
       * it to do so.
       */
      goto out;
    }

  _dbus_assert (service_name != NULL); /* this message is intended for bus routing */
  
  /* Create our transaction */
  transaction = bus_transaction_new ();
  if (transaction == NULL)
    {
      BUS_SET_OOM (&error);
      goto out;
    }
  
  /* Assign a sender to the message */
  if (bus_connection_is_active (connection))
    {
      sender = bus_connection_get_name (connection);
      _dbus_assert (sender != NULL);
      
      if (!dbus_message_set_sender (message, sender))
        {
          BUS_SET_OOM (&error);
          goto out;
        }
    }

  if (strcmp (service_name, DBUS_SERVICE_DBUS) == 0) /* to bus driver */
    {
      if (!bus_driver_handle_message (connection, transaction, message, &error))
        goto out;
    }
  else if (!bus_connection_is_active (connection)) /* clients must talk to bus driver first */
    {
      _dbus_verbose ("Received message from non-registered client. Disconnecting.\n");
      dbus_connection_disconnect (connection);
    }
  /* FIXME what if we un-special-case this service and just have a flag
   * on services that all service owners will get messages to it, not just
   * the primary owner.
   */
  else if (strcmp (service_name, DBUS_SERVICE_BROADCAST) == 0) /* spam! */
    {
      if (!bus_dispatch_broadcast_message (transaction, message, &error))
        goto out;
    }
  else  /* route to named service */
    {
      DBusString service_string;
      BusService *service;

      _dbus_string_init_const (&service_string, service_name);
      service = bus_service_lookup (&service_string);

      if (service == NULL)
        {
          if (!send_service_nonexistent_error (transaction, connection,
                                               service_name,
                                               message, &error))
            goto out;
        }
      else
        {
          _dbus_assert (bus_service_get_primary_owner (service) != NULL);
      
          /* Dispatch the message */
          if (!bus_transaction_send_message (transaction,
                                             bus_service_get_primary_owner (service),
                                             message))
            {
              BUS_SET_OOM (&error);
              goto out;
            }
        }
    }
  
 out:
  if (dbus_error_is_set (&error))
    {
      if (!dbus_connection_get_is_connected (connection))
        {
          /* If we disconnected it, we won't bother to send it any error
           * messages.
           */
        }
      else if (dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY))
        {
          bus_connection_send_oom_error (connection, message);

          /* cancel transaction due to OOM */
          if (transaction != NULL)
            {
              bus_transaction_cancel_and_free (transaction);
              transaction = NULL;
            }
        }
      else
        {
          /* Try to send the real error, if no mem to do that, send
           * the OOM error
           */
          _dbus_assert (transaction != NULL);
          
          if (!bus_transaction_send_error_reply (transaction, connection,
                                                 &error, message))
            {
              bus_connection_send_oom_error (connection, message);

              /* cancel transaction due to OOM */
              if (transaction != NULL)
                {
                  bus_transaction_cancel_and_free (transaction);
                  transaction = NULL;
                }
            }
        }
      
      dbus_error_free (&error);
    }

  if (transaction != NULL)
    {
      bus_transaction_execute_and_free (transaction);
    }

  dbus_connection_unref (connection);
  
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

