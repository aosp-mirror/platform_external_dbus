/* -*- mode: C; c-file-style: "gnu" -*- */
/* dispatch.c  Message dispatcher
 *
 * Copyright (C) 2003  CodeFactory AB
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

#include "dispatch.h"
#include "connection.h"
#include "driver.h"
#include "services.h"
#include "utils.h"
#include "bus.h"
#include "test.h"
#include "loop.h"
#include <dbus/dbus-internals.h>
#include <string.h>

static int message_handler_slot = -1;
static int message_handler_slot_refcount;

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
  BusConnections *connections;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  _dbus_assert (dbus_message_get_sender (message) != NULL);

  connections = bus_transaction_get_connections (transaction);
  
  dbus_error_init (&tmp_error);
  d.message = message;
  d.transaction = transaction;
  d.error = &tmp_error;
  
  bus_connections_foreach (connections, send_one_message, &d);

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

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
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
      !_dbus_string_append (&error_message, "\" does not exist"))
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

static void
bus_dispatch (DBusConnection *connection,
              DBusMessage    *message)
{
  const char *sender, *service_name, *message_name;
  DBusError error;
  BusTransaction *transaction;
  BusContext *context;
  
  transaction = NULL;
  dbus_error_init (&error);
  
  context = bus_connection_get_context (connection);
  _dbus_assert (context != NULL);
  
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

  _dbus_verbose ("DISPATCH: %s to %s\n",
                 message_name, service_name ? service_name : "peer");
  
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
  transaction = bus_transaction_new (context);
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

      /* We need to refetch the service name here, because
       * dbus_message_set_sender can cause the header to be
       * reallocated, and thus the service_name pointer will become
       * invalid.
       */
      service_name = dbus_message_get_service (message);
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
      BusRegistry *registry;

      registry = bus_connection_get_registry (connection);
      
      _dbus_string_init_const (&service_string, service_name);
      service = bus_registry_lookup (registry, &service_string);

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
}

static DBusHandlerResult
bus_dispatch_message_handler (DBusMessageHandler *handler,
			      DBusConnection     *connection,
			      DBusMessage        *message,
			      void               *user_data)
{
  bus_dispatch (connection, message);
  
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static dbus_bool_t
message_handler_slot_ref (void)
{
  if (message_handler_slot < 0)
    {
      message_handler_slot = dbus_connection_allocate_data_slot ();
      
      if (message_handler_slot < 0)
        return FALSE;

      _dbus_assert (message_handler_slot_refcount == 0);
    }  

  message_handler_slot_refcount += 1;

  return TRUE;
}

static void
message_handler_slot_unref (void)
{
  _dbus_assert (message_handler_slot_refcount > 0);

  message_handler_slot_refcount -= 1;
  
  if (message_handler_slot_refcount == 0)
    {
      dbus_connection_free_data_slot (message_handler_slot);
      message_handler_slot = -1;
    }
}

static void
free_message_handler (void *data)
{
  DBusMessageHandler *handler = data;
  
  _dbus_assert (message_handler_slot >= 0);
  _dbus_assert (message_handler_slot_refcount > 0);
  
  dbus_message_handler_unref (handler);
  message_handler_slot_unref ();
}

dbus_bool_t
bus_dispatch_add_connection (DBusConnection *connection)
{
  DBusMessageHandler *handler;

  if (!message_handler_slot_ref ())
    return FALSE;
  
  handler = dbus_message_handler_new (bus_dispatch_message_handler, NULL, NULL);  
  if (handler == NULL)
    {
      message_handler_slot_unref ();
      return FALSE;
    }    
  
  if (!dbus_connection_add_filter (connection, handler))
    {
      dbus_message_handler_unref (handler);
      message_handler_slot_unref ();
      
      return FALSE;
    }

  _dbus_assert (message_handler_slot >= 0);
  _dbus_assert (message_handler_slot_refcount > 0);
  
  if (!dbus_connection_set_data (connection,
				 message_handler_slot,
				 handler,
                                 free_message_handler))
    {
      dbus_message_handler_unref (handler);
      message_handler_slot_unref ();

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

#ifdef DBUS_BUILD_TESTS

typedef dbus_bool_t (* Check1Func) (BusContext     *context);
typedef dbus_bool_t (* Check2Func) (BusContext     *context,
                                    DBusConnection *connection);

static dbus_bool_t check_no_leftovers (BusContext *context);

typedef struct
{
  const char *expected_service_name;
  dbus_bool_t failed;
} CheckServiceDeletedData;

static dbus_bool_t
check_service_deleted_foreach (DBusConnection *connection,
                               void           *data)
{
  CheckServiceDeletedData *d = data;
  DBusMessage *message;
  DBusError error;
  char *service_name;

  dbus_error_init (&error);
  d->failed = TRUE;
  service_name = NULL;
  
  message = dbus_connection_pop_message (connection);
  if (message == NULL)
    {
      _dbus_warn ("Did not receive a message on %p, expecting %s\n",
                  connection, DBUS_MESSAGE_SERVICE_DELETED);
      goto out;
    }
  else if (!dbus_message_name_is (message, DBUS_MESSAGE_SERVICE_DELETED))
    {
      _dbus_warn ("Received message %s on %p, expecting %s\n",
                  dbus_message_get_name (message),
                  connection, DBUS_MESSAGE_SERVICE_DELETED);
      goto out;
    }
  else
    {
      if (!dbus_message_get_args (message, &error,
                                  DBUS_TYPE_STRING, &service_name,
                                  DBUS_TYPE_INVALID))
        {
          if (dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY))
            {
              _dbus_verbose ("no memory to get service name arg\n");
            }
          else
            {
              _dbus_assert (dbus_error_is_set (&error));
              _dbus_warn ("Did not get the expected single string argument\n");
              goto out;
            }
        }
      else if (strcmp (service_name, d->expected_service_name) != 0)
        {
          _dbus_warn ("expected deletion of service %s, got deletion of %s\n",
                      d->expected_service_name,
                      service_name);
          goto out;
        }
    }

  d->failed = FALSE;
  
 out:
  dbus_free (service_name);
  dbus_error_free (&error);
  
  if (message)
    dbus_message_unref (message);

  return !d->failed;
}

static void
kill_client_connection (BusContext     *context,
                        DBusConnection *connection)
{
  char *base_service;
  const char *s;
  CheckServiceDeletedData csdd;

  _dbus_verbose ("killing connection %p\n", connection);
  
  s = dbus_bus_get_base_service (connection);
  _dbus_assert (s != NULL);

  while ((base_service = _dbus_strdup (s)) == NULL)
    bus_wait_for_memory ();

  dbus_connection_ref (connection);
  
  /* kick in the disconnect handler that unrefs the connection */
  dbus_connection_disconnect (connection);

  bus_test_flush_bus (context);

  _dbus_assert (bus_test_client_listed (connection));
  
  /* Run disconnect handler in test.c */
  if (bus_connection_dispatch_one_message (connection))
    _dbus_assert_not_reached ("something received on connection being killed other than the disconnect");
  
  _dbus_assert (!dbus_connection_get_is_connected (connection));
  dbus_connection_unref (connection);
  connection = NULL;
  _dbus_assert (!bus_test_client_listed (connection));
  
  csdd.expected_service_name = base_service;
  csdd.failed = FALSE;

  bus_test_clients_foreach (check_service_deleted_foreach,
                            &csdd);

  dbus_free (base_service);
  
  if (csdd.failed)
    _dbus_assert_not_reached ("didn't get the expected ServiceDeleted messages");
  
  if (!check_no_leftovers (context))
    _dbus_assert_not_reached ("stuff left in message queues after disconnecting a client");
}

static void
kill_client_connection_unchecked (DBusConnection *connection)
{
  /* This kills the connection without expecting it to affect
   * the rest of the bus.
   */  
  _dbus_verbose ("Unchecked kill of connection %p\n", connection);

  dbus_connection_ref (connection);
  dbus_connection_disconnect (connection);
  /* dispatching disconnect handler will unref once */
  if (bus_connection_dispatch_one_message (connection))
    _dbus_assert_not_reached ("message other than disconnect dispatched after failure to register");
  dbus_connection_unref (connection);
  _dbus_assert (!bus_test_client_listed (connection));
}

typedef struct
{
  dbus_bool_t failed;
} CheckNoMessagesData;

static dbus_bool_t
check_no_messages_foreach (DBusConnection *connection,
                           void           *data)
{
  CheckNoMessagesData *d = data;
  DBusMessage *message;

  message = dbus_connection_pop_message (connection);
  if (message != NULL)
    {
      _dbus_warn ("Received message %s on %p, expecting no messages\n",
                  dbus_message_get_name (message), connection);
      d->failed = TRUE;
    }

  if (message)
    dbus_message_unref (message);
  return !d->failed;
}

typedef struct
{
  DBusConnection *skip_connection;
  const char *expected_service_name;
  dbus_bool_t failed;
} CheckServiceCreatedData;

static dbus_bool_t
check_service_created_foreach (DBusConnection *connection,
                               void           *data)
{
  CheckServiceCreatedData *d = data;
  DBusMessage *message;
  DBusError error;
  char *service_name;

  if (connection == d->skip_connection)
    return TRUE;

  dbus_error_init (&error);
  d->failed = TRUE;
  service_name = NULL;
  
  message = dbus_connection_pop_message (connection);
  if (message == NULL)
    {
      _dbus_warn ("Did not receive a message on %p, expecting %s\n",
                  connection, DBUS_MESSAGE_SERVICE_CREATED);
      goto out;
    }
  else if (!dbus_message_name_is (message, DBUS_MESSAGE_SERVICE_CREATED))
    {
      _dbus_warn ("Received message %s on %p, expecting %s\n",
                  dbus_message_get_name (message),
                  connection, DBUS_MESSAGE_SERVICE_CREATED);
      goto out;
    }
  else
    {
      if (!dbus_message_get_args (message, &error,
                                  DBUS_TYPE_STRING, &service_name,
                                  DBUS_TYPE_INVALID))
        {
          if (dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY))
            {
              _dbus_verbose ("no memory to get service name arg\n");
            }
          else
            {
              _dbus_assert (dbus_error_is_set (&error));
              _dbus_warn ("Did not get the expected single string argument\n");
              goto out;
            }
        }
      else if (strcmp (service_name, d->expected_service_name) != 0)
        {
          _dbus_warn ("expected creation of service %s, got creation of %s\n",
                      d->expected_service_name,
                      service_name);
          goto out;
        }
    }

  d->failed = FALSE;
  
 out:
  dbus_free (service_name);
  dbus_error_free (&error);
  
  if (message)
    dbus_message_unref (message);

  return !d->failed;
}

static dbus_bool_t
check_no_leftovers (BusContext *context)
{
  CheckNoMessagesData nmd;

  nmd.failed = FALSE;
  bus_test_clients_foreach (check_no_messages_foreach,
                            &nmd);
  
  if (nmd.failed)
    return FALSE;
  else
    return TRUE;
}

/* returns TRUE if the correct thing happens,
 * but the correct thing may include OOM errors.
 */
static dbus_bool_t
check_hello_message (BusContext     *context,
                     DBusConnection *connection)
{
  DBusMessage *message;
  dbus_int32_t serial;
  dbus_bool_t retval;
  DBusError error;
  char *name;
  char *acquired;
  
  dbus_error_init (&error);
  name = NULL;
  acquired = NULL;
  
  message = dbus_message_new (DBUS_SERVICE_DBUS,
			      DBUS_MESSAGE_HELLO);

  if (message == NULL)
    return TRUE;

  if (!dbus_connection_send (connection, message, &serial))
    {
      dbus_message_unref (message);
      return TRUE;
    }

  dbus_message_unref (message);
  message = NULL;
  
  bus_test_flush_bus (context);

  if (!dbus_connection_get_is_connected (connection))
    {
      _dbus_verbose ("connection was disconnected\n");
      return TRUE;
    }
  
  retval = FALSE;
  
  message = dbus_connection_pop_message (connection);
  if (message == NULL)
    {
      _dbus_warn ("Did not receive a reply to %s %d on %p\n",
                  DBUS_MESSAGE_HELLO, serial, connection);
      goto out;
    }

  _dbus_verbose ("Received %s on %p\n",
                 dbus_message_get_name (message), connection);

  if (dbus_message_get_is_error (message))
    {
      if (dbus_message_name_is (message,
                                DBUS_ERROR_NO_MEMORY))
        {
          ; /* good, this is a valid response */
        }
      else
        {
          _dbus_warn ("Did not expect error %s\n",
                      dbus_message_get_name (message));
          goto out;
        }
    }
  else
    {
      CheckServiceCreatedData scd;
      
      if (dbus_message_name_is (message,
                                DBUS_MESSAGE_HELLO))
        {
          ; /* good, expected */
        }
      else
        {
          _dbus_warn ("Did not expect reply %s\n",
                      dbus_message_get_name (message));
          goto out;
        }

    retry_get_hello_name:
      if (!dbus_message_get_args (message, &error,
                                  DBUS_TYPE_STRING, &name,
                                  DBUS_TYPE_INVALID))
        {
          if (dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY))
            {
              _dbus_verbose ("no memory to get service name arg from hello\n");
              dbus_error_free (&error);
              bus_wait_for_memory ();
              goto retry_get_hello_name;
            }
          else
            {
              _dbus_assert (dbus_error_is_set (&error));
              _dbus_warn ("Did not get the expected single string argument to hello\n");
              goto out;
            }
        }

      _dbus_verbose ("Got hello name: %s\n", name);

      while (!dbus_bus_set_base_service (connection, name))
        bus_wait_for_memory ();
      
      scd.skip_connection = NULL;
      scd.failed = FALSE;
      scd.expected_service_name = name;
      bus_test_clients_foreach (check_service_created_foreach,
                                &scd);
      
      if (scd.failed)
        goto out;
      
      /* Client should also have gotten ServiceAcquired */
      dbus_message_unref (message);
      message = dbus_connection_pop_message (connection);
      if (message == NULL)
        {
          _dbus_warn ("Expecting %s, got nothing\n",
                      DBUS_MESSAGE_SERVICE_ACQUIRED);
          goto out;
        }
      
    retry_get_acquired_name:
      if (!dbus_message_get_args (message, &error,
                                  DBUS_TYPE_STRING, &acquired,
                                  DBUS_TYPE_INVALID))
        {
          if (dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY))
            {
              _dbus_verbose ("no memory to get service name arg from acquired\n");
              dbus_error_free (&error);
              bus_wait_for_memory ();
              goto retry_get_acquired_name;
            }
          else
            {
              _dbus_assert (dbus_error_is_set (&error));
              _dbus_warn ("Did not get the expected single string argument to ServiceAcquired\n");
              goto out;
            }
        }

      _dbus_verbose ("Got acquired name: %s\n", acquired);

      if (strcmp (acquired, name) != 0)
        {
          _dbus_warn ("Acquired name is %s but expected %s\n",
                      acquired, name);
          goto out;
        }
    }

  if (!check_no_leftovers (context))
    goto out;
  
  retval = TRUE;
  
 out:
  dbus_error_free (&error);
  
  dbus_free (name);
  dbus_free (acquired);
  
  if (message)
    dbus_message_unref (message);
  
  return retval;
}

/* returns TRUE if the correct thing happens,
 * but the correct thing may include OOM errors.
 */
static dbus_bool_t
check_hello_connection (BusContext *context)
{
  DBusConnection *connection;
  DBusError error;

  dbus_error_init (&error);

  connection = dbus_connection_open ("debug-pipe:name=test-server", &error);
  if (connection == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (&error);
      dbus_error_free (&error);
      return TRUE;
    }

  if (!bus_setup_debug_client (connection))
    {
      dbus_connection_disconnect (connection);
      dbus_connection_unref (connection);
      return TRUE;
    }

  if (!check_hello_message (context, connection))
    return FALSE;

  if (dbus_bus_get_base_service (connection) == NULL)
    {
      /* We didn't successfully register, so we can't
       * do the usual kill_client_connection() checks
       */
      kill_client_connection_unchecked (connection);
    }
  else
    {
      kill_client_connection (context, connection);
    }

  return TRUE;
}

static void
check1_try_iterations (BusContext *context,
                       const char *description,
                       Check1Func  func)
{
  int approx_mallocs;

  /* Run once to see about how many mallocs are involved */
  
  _dbus_set_fail_alloc_counter (_DBUS_INT_MAX);
  
  if (! (*func) (context))
    _dbus_assert_not_reached ("test failed");

  approx_mallocs = _DBUS_INT_MAX - _dbus_get_fail_alloc_counter ();

  _dbus_verbose ("=================\n%s: about %d mallocs total\n=================\n",
                 description, approx_mallocs);
  
  approx_mallocs += 10; /* fudge factor */
  
  /* Now run failing each malloc */
  
  while (approx_mallocs >= 0)
    {
      _dbus_set_fail_alloc_counter (approx_mallocs);

      _dbus_verbose ("\n===\n %s: (will fail malloc %d)\n===\n",
                     description, approx_mallocs);

      if (! (*func) (context))
        _dbus_assert_not_reached ("test failed");

      if (!check_no_leftovers (context))
        _dbus_assert_not_reached ("Messages were left over, should be covered by test suite");
      
      approx_mallocs -= 1;
    }

  _dbus_set_fail_alloc_counter (_DBUS_INT_MAX);

  _dbus_verbose ("=================\n%s: all iterations passed\n=================\n",
                 description);
}

dbus_bool_t
bus_dispatch_test (const DBusString *test_data_dir)
{
  BusContext *context;
  DBusError error;
  const char *activation_dirs[] = { NULL, NULL };
  DBusConnection *foo;
  DBusConnection *bar;
  DBusConnection *baz;

  dbus_error_init (&error);
  context = bus_context_new ("debug-pipe:name=test-server",
                             activation_dirs,
                             &error);
  if (context == NULL)
    _dbus_assert_not_reached ("could not alloc context");
  
  foo = dbus_connection_open ("debug-pipe:name=test-server", &error);
  if (foo == NULL)
    _dbus_assert_not_reached ("could not alloc connection");

  if (!bus_setup_debug_client (foo))
    _dbus_assert_not_reached ("could not set up connection");

  if (!check_hello_message (context, foo))
    _dbus_assert_not_reached ("hello message failed");
  
  bar = dbus_connection_open ("debug-pipe:name=test-server", &error);
  if (bar == NULL)
    _dbus_assert_not_reached ("could not alloc connection");

  if (!bus_setup_debug_client (bar))
    _dbus_assert_not_reached ("could not set up connection");

  if (!check_hello_message (context, bar))
    _dbus_assert_not_reached ("hello message failed");
  
  baz = dbus_connection_open ("debug-pipe:name=test-server", &error);
  if (baz == NULL)
    _dbus_assert_not_reached ("could not alloc connection");

  if (!bus_setup_debug_client (baz))
    _dbus_assert_not_reached ("could not set up connection");

  if (!check_hello_message (context, baz))
    _dbus_assert_not_reached ("hello message failed");

  check1_try_iterations (context, "create_and_hello",
                         check_hello_connection);
  
  _dbus_verbose ("Disconnecting foo, bar, and baz\n");

  kill_client_connection_unchecked (foo);
  kill_client_connection_unchecked (bar);
  kill_client_connection_unchecked (baz);

  bus_context_unref (context);
  
  return TRUE;
}
#endif /* DBUS_BUILD_TESTS */
