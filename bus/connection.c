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
#include "dispatch.h"
#include "policy.h"
#include "services.h"
#include "utils.h"
#include <dbus/dbus-list.h>

static void bus_connection_remove_transactions (DBusConnection *connection);

struct BusConnections
{
  int refcount;
  DBusList *list; /**< List of all the connections */
  BusContext *context;
};

static int connection_data_slot = -1;
static int connection_data_slot_refcount = 0;

typedef struct
{
  BusConnections *connections;
  DBusConnection *connection;
  DBusList *services_owned;
  char *name;
  DBusList *transaction_messages; /**< Stuff we need to send as part of a transaction */
  DBusMessage *oom_message;
  DBusPreallocatedSend *oom_preallocated;
  BusClientPolicy *policy;
} BusConnectionData;

#define BUS_CONNECTION_DATA(connection) (dbus_connection_get_data ((connection), connection_data_slot))

static dbus_bool_t
connection_data_slot_ref (void)
{
  if (connection_data_slot < 0)
    {
      connection_data_slot = dbus_connection_allocate_data_slot ();
      
      if (connection_data_slot < 0)
        return FALSE;

      _dbus_assert (connection_data_slot_refcount == 0);
    }  

  connection_data_slot_refcount += 1;

  return TRUE;

}

static void
connection_data_slot_unref (void)
{
  _dbus_assert (connection_data_slot_refcount > 0);

  connection_data_slot_refcount -= 1;
  
  if (connection_data_slot_refcount == 0)
    {
      dbus_connection_free_data_slot (connection_data_slot);
      connection_data_slot = -1;
    }
}

static DBusLoop*
connection_get_loop (DBusConnection *connection)
{
  BusConnectionData *d;

  d = BUS_CONNECTION_DATA (connection);

  return bus_context_get_loop (d->connections->context);
}

void
bus_connection_disconnected (DBusConnection *connection)
{
  BusConnectionData *d;
  BusService *service;

  d = BUS_CONNECTION_DATA (connection);
  _dbus_assert (d != NULL);

  _dbus_verbose ("%s disconnected, dropping all service ownership and releasing\n",
                 d->name ? d->name : "(inactive)");
  
  /* Drop any service ownership. FIXME Unfortunately, this requires
   * memory allocation and there doesn't seem to be a good way to
   * handle it other than sleeping; we can't "fail" the operation of
   * disconnecting a client, and preallocating a broadcast "service is
   * now gone" message for every client-service pair seems kind of
   * involved. Probably we need to do that though, and also
   * extend BusTransaction to be able to revert generic
   * stuff, not just sending a message (so we can e.g. revert
   * removal of service owners).
   */
  while ((service = _dbus_list_get_last (&d->services_owned)))
    {
      BusTransaction *transaction;
      DBusError error;

    retry:
      
      dbus_error_init (&error);
        
      transaction = NULL;
      while (transaction == NULL)
        {
          transaction = bus_transaction_new (d->connections->context);
          _dbus_wait_for_memory ();
        }
        
      if (!bus_service_remove_owner (service, connection,
                                     transaction, &error))
        {
          _DBUS_ASSERT_ERROR_IS_SET (&error);
          
          if (dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY))
            {
              dbus_error_free (&error);
              bus_transaction_cancel_and_free (transaction);
              _dbus_wait_for_memory ();
              goto retry;
            }
          else
            {
              _dbus_verbose ("Failed to remove service owner: %s %s\n",
                             error.name, error.message);
              _dbus_assert_not_reached ("Removing service owner failed for non-memory-related reason");
            }
        }
        
      bus_transaction_execute_and_free (transaction);
    }

  bus_dispatch_remove_connection (connection);
  
  /* no more watching */
  if (!dbus_connection_set_watch_functions (connection,
                                            NULL, NULL, NULL,
                                            connection,
                                            NULL))
    _dbus_assert_not_reached ("setting watch functions to NULL failed");

  if (!dbus_connection_set_timeout_functions (connection,
                                              NULL, NULL, NULL,
                                              connection,
                                              NULL))
    _dbus_assert_not_reached ("setting timeout functions to NULL failed");
  
  dbus_connection_set_unix_user_function (connection,
                                          NULL, NULL, NULL);

  dbus_connection_set_dispatch_status_function (connection,
                                                NULL, NULL, NULL);
  
  bus_connection_remove_transactions (connection);

  _dbus_list_remove (&d->connections->list, connection);

  /* frees "d" as side effect */
  dbus_connection_set_data (connection,
                            connection_data_slot,
                            NULL, NULL);

  dbus_connection_unref (connection);
}

static dbus_bool_t
connection_watch_callback (DBusWatch     *watch,
                           unsigned int   condition,
                           void          *data)
{
 /* FIXME this can be done in dbus-mainloop.c
   * if the code in activation.c for the babysitter
   * watch handler is fixed.
   */
  
#if 0
  _dbus_verbose ("Calling handle_watch\n");
#endif
  return dbus_watch_handle (watch, condition);
}

static dbus_bool_t
add_connection_watch (DBusWatch      *watch,
                      void           *data)
{
  DBusConnection *connection = data;

  return _dbus_loop_add_watch (connection_get_loop (connection),
                               watch, connection_watch_callback, connection,
                               NULL);
}

static void
remove_connection_watch (DBusWatch      *watch,
                         void           *data)
{
  DBusConnection *connection = data;
  
  _dbus_loop_remove_watch (connection_get_loop (connection),
                           watch, connection_watch_callback, connection);
}

static void
connection_timeout_callback (DBusTimeout   *timeout,
                             void          *data)
{
  /* DBusConnection *connection = data; */

  /* can return FALSE on OOM but we just let it fire again later */
  dbus_timeout_handle (timeout);
}

static dbus_bool_t
add_connection_timeout (DBusTimeout    *timeout,
                        void           *data)
{
  DBusConnection *connection = data;
  
  return _dbus_loop_add_timeout (connection_get_loop (connection),
                                 timeout, connection_timeout_callback, connection, NULL);
}

static void
remove_connection_timeout (DBusTimeout    *timeout,
                           void           *data)
{
  DBusConnection *connection = data;
  
  _dbus_loop_remove_timeout (connection_get_loop (connection),
                             timeout, connection_timeout_callback, connection);
}

static void
dispatch_status_function (DBusConnection    *connection,
                          DBusDispatchStatus new_status,
                          void              *data)
{
  DBusLoop *loop = data;
  
  if (new_status != DBUS_DISPATCH_COMPLETE)
    {
      while (!_dbus_loop_queue_dispatch (loop, connection))
        _dbus_wait_for_memory ();
    }
}

static dbus_bool_t
allow_user_function (DBusConnection *connection,
                     unsigned long   uid,
                     void           *data)
{
  BusConnectionData *d;
    
  d = BUS_CONNECTION_DATA (connection);

  _dbus_assert (d != NULL);
  
  return bus_context_allow_user (d->connections->context, uid);
}

static void
free_connection_data (void *data)
{
  BusConnectionData *d = data;

  /* services_owned should be NULL since we should be disconnected */
  _dbus_assert (d->services_owned == NULL);
  /* similarly */
  _dbus_assert (d->transaction_messages == NULL);

  if (d->oom_preallocated)
    dbus_connection_free_preallocated_send (d->connection, d->oom_preallocated);

  if (d->oom_message)
    dbus_message_unref (d->oom_message);

  if (d->policy)
    bus_client_policy_unref (d->policy);
  
  dbus_free (d->name);
  
  dbus_free (d);
}

BusConnections*
bus_connections_new (BusContext *context)
{
  BusConnections *connections;

  if (!connection_data_slot_ref ())
    return NULL;

  connections = dbus_new0 (BusConnections, 1);
  if (connections == NULL)
    {
      connection_data_slot_unref ();
      return NULL;
    }
  
  connections->refcount = 1;
  connections->context = context;
  
  return connections;
}

void
bus_connections_ref (BusConnections *connections)
{
  _dbus_assert (connections->refcount > 0);
  connections->refcount += 1;
}

void
bus_connections_unref (BusConnections *connections)
{
  _dbus_assert (connections->refcount > 0);
  connections->refcount -= 1;
  if (connections->refcount == 0)
    {
      while (connections->list != NULL)
        {
          DBusConnection *connection;

          connection = connections->list->data;

          dbus_connection_ref (connection);
          dbus_connection_disconnect (connection);
          bus_connection_disconnected (connection);
          dbus_connection_unref (connection);
        }
      
      _dbus_list_clear (&connections->list);
      
      dbus_free (connections);

      connection_data_slot_unref ();
    }
}

dbus_bool_t
bus_connections_setup_connection (BusConnections *connections,
                                  DBusConnection *connection)
{
  BusConnectionData *d;
  dbus_bool_t retval;
  
  d = dbus_new0 (BusConnectionData, 1);
  
  if (d == NULL)
    return FALSE;

  d->connections = connections;
  d->connection = connection;

  _dbus_assert (connection_data_slot >= 0);
  
  if (!dbus_connection_set_data (connection,
                                 connection_data_slot,
                                 d, free_connection_data))
    {
      dbus_free (d);
      return FALSE;
    }

  retval = FALSE;
  
  if (!dbus_connection_set_watch_functions (connection,
                                            add_connection_watch,
                                            remove_connection_watch,
                                            NULL,
                                            connection,
                                            NULL))
    goto out;
  
  if (!dbus_connection_set_timeout_functions (connection,
                                              add_connection_timeout,
                                              remove_connection_timeout,
                                              NULL,
                                              connection, NULL))
    goto out;


  dbus_connection_set_unix_user_function (connection,
                                          allow_user_function,
                                          NULL, NULL);

  dbus_connection_set_dispatch_status_function (connection,
                                                dispatch_status_function,
                                                bus_context_get_loop (connections->context),
                                                NULL);
  
  /* Setup the connection with the dispatcher */
  if (!bus_dispatch_add_connection (connection))
    goto out;
  
  if (!_dbus_list_append (&connections->list, connection))
    {
      bus_dispatch_remove_connection (connection);
      goto out;
    }

  if (dbus_connection_get_dispatch_status (connection) != DBUS_DISPATCH_COMPLETE)
    {
      if (!_dbus_loop_queue_dispatch (bus_context_get_loop (connections->context), connection))
        {
          bus_dispatch_remove_connection (connection);
          goto out;
        }
    }
  
  dbus_connection_ref (connection);
  retval = TRUE;

 out:
  if (!retval)
    {        
      if (!dbus_connection_set_watch_functions (connection,
                                                NULL, NULL, NULL,
                                                connection,
                                                NULL))
        _dbus_assert_not_reached ("setting watch functions to NULL failed");
      
      if (!dbus_connection_set_timeout_functions (connection,
                                                  NULL, NULL, NULL,
                                                  connection,
                                                  NULL))
        _dbus_assert_not_reached ("setting timeout functions to NULL failed");

      dbus_connection_set_unix_user_function (connection,
                                              NULL, NULL, NULL);

      dbus_connection_set_dispatch_status_function (connection,
                                                    NULL, NULL, NULL);
      
      if (!dbus_connection_set_data (connection,
                                     connection_data_slot,
                                     NULL, NULL))
        _dbus_assert_not_reached ("failed to set connection data to null");
    }
  
  return retval;
}

dbus_bool_t
bus_connection_get_groups  (DBusConnection   *connection,
                            unsigned long   **groups,
                            int              *n_groups)
{
  BusConnectionData *d;
  unsigned long uid;
  DBusUserDatabase *user_database;
  
  d = BUS_CONNECTION_DATA (connection);

  _dbus_assert (d != NULL);

  user_database = bus_context_get_user_database (d->connections->context);
  
  *groups = NULL;
  *n_groups = 0;

  if (dbus_connection_get_unix_user (connection, &uid))
    {
      if (!_dbus_user_database_get_groups (user_database,
                                           uid, groups, n_groups,
                                           NULL))
        {
          _dbus_verbose ("Did not get any groups for UID %lu\n",
                         uid);
          return FALSE;
        }
      else
        {
          _dbus_verbose ("Got %d groups for UID %lu\n",
                         *n_groups, uid);
          return TRUE;
        }
    }
  else
    return TRUE; /* successfully got 0 groups */
}

dbus_bool_t
bus_connection_is_in_group (DBusConnection *connection,
                            unsigned long   gid)
{
  int i;
  unsigned long *group_ids;
  int n_group_ids;

  if (!bus_connection_get_groups (connection, &group_ids, &n_group_ids))
    return FALSE;

  i = 0;
  while (i < n_group_ids)
    {
      if (group_ids[i] == gid)
        {
          dbus_free (group_ids);
          return TRUE;
        }
      ++i;
    }

  dbus_free (group_ids);
  return FALSE;
}

BusClientPolicy*
bus_connection_get_policy (DBusConnection *connection)
{
  BusConnectionData *d;
    
  d = BUS_CONNECTION_DATA (connection);

  _dbus_assert (d != NULL);

  if (!dbus_connection_get_is_authenticated (connection))
    {
      _dbus_verbose ("Tried to get policy for unauthenticated connection!\n");
      return NULL;
    }
  
  /* We do lazy creation of the policy because
   * it can only be done post-authentication.
   */
  if (d->policy == NULL)
    {
      d->policy =
        bus_context_create_client_policy (d->connections->context,
                                          connection);

      /* we may have a NULL policy on OOM or error getting list of
       * groups for a user. In the latter case we don't handle it so
       * well currently, just keep pretending we're out of memory,
       * which is kind of bizarre.
       */
    }

  return d->policy;
}

/**
 * Calls function on each connection; if the function returns
 * #FALSE, stops iterating.
 *
 * @param connections the connections object
 * @param function the function
 * @param data data to pass to it as a second arg
 */
void
bus_connections_foreach (BusConnections               *connections,
                         BusConnectionForeachFunction  function,
			void                          *data)
{
  DBusList *link;
  
  link = _dbus_list_get_first_link (&connections->list);
  while (link != NULL)
    {
      DBusConnection *connection = link->data;
      DBusList *next = _dbus_list_get_next_link (&connections->list, link);

      if (!(* function) (connection, data))
        break;
      
      link = next;
    }
}

BusContext*
bus_connections_get_context (BusConnections *connections)
{
  return connections->context;
}

BusContext*
bus_connection_get_context (DBusConnection *connection)
{
  BusConnectionData *d;

  d = BUS_CONNECTION_DATA (connection);

  _dbus_assert (d != NULL);

  return d->connections->context;
}

BusConnections*
bus_connection_get_connections (DBusConnection *connection)
{
  BusConnectionData *d;
    
  d = BUS_CONNECTION_DATA (connection);

  _dbus_assert (d != NULL);

  return d->connections;
}

BusRegistry*
bus_connection_get_registry (DBusConnection *connection)
{
  BusConnectionData *d;

  d = BUS_CONNECTION_DATA (connection);

  _dbus_assert (d != NULL);

  return bus_context_get_registry (d->connections->context);
}

BusActivation*
bus_connection_get_activation (DBusConnection *connection)
{
  BusConnectionData *d;

  d = BUS_CONNECTION_DATA (connection);

  _dbus_assert (d != NULL);

  return bus_context_get_activation (d->connections->context);
}

/**
 * Checks whether the connection is registered with the message bus.
 *
 * @param connection the connection
 * @returns #TRUE if we're an active message bus participant
 */
dbus_bool_t
bus_connection_is_active (DBusConnection *connection)
{
  BusConnectionData *d;

  d = BUS_CONNECTION_DATA (connection);
  
  return d != NULL && d->name != NULL;
}

dbus_bool_t
bus_connection_preallocate_oom_error (DBusConnection *connection)
{
  DBusMessage *message;
  DBusPreallocatedSend *preallocated;
  BusConnectionData *d;

  d = BUS_CONNECTION_DATA (connection);  

  _dbus_assert (d != NULL);

  if (d->oom_preallocated != NULL)
    return TRUE;
  
  preallocated = dbus_connection_preallocate_send (connection);
  if (preallocated == NULL)
    return FALSE;

  /* d->name may be NULL, but that should be OK */
  message = dbus_message_new (d->name,
                              DBUS_ERROR_NO_MEMORY);
  if (message == NULL)
    {
      dbus_connection_free_preallocated_send (connection, preallocated);
      return FALSE;
    }

  dbus_message_set_is_error (message, TRUE);

  if (!dbus_message_set_sender (message,
                                DBUS_SERVICE_DBUS))
    {
      dbus_connection_free_preallocated_send (connection, preallocated);
      dbus_message_unref (message);
      return FALSE;
    }
  
  /* set reply serial to placeholder value just so space is already allocated
   * for it.
   */
  if (!dbus_message_set_reply_serial (message, 14))
    {
      dbus_connection_free_preallocated_send (connection, preallocated);
      dbus_message_unref (message);
      return FALSE;
    }

  d->oom_message = message;
  d->oom_preallocated = preallocated;
  
  return TRUE;
}

void
bus_connection_send_oom_error (DBusConnection *connection,
                               DBusMessage    *in_reply_to)
{
  BusConnectionData *d;

  d = BUS_CONNECTION_DATA (connection);  

  _dbus_assert (d != NULL);  
  _dbus_assert (d->oom_message != NULL);

  /* should always succeed since we set it to a placeholder earlier */
  if (!dbus_message_set_reply_serial (d->oom_message,
                                      dbus_message_get_serial (in_reply_to)))
    _dbus_assert_not_reached ("Failed to set reply serial for preallocated oom message");

  _dbus_assert (dbus_message_get_sender (d->oom_message) != NULL);
  
  dbus_connection_send_preallocated (connection, d->oom_preallocated,
                                     d->oom_message, NULL);

  dbus_message_unref (d->oom_message);
  d->oom_message = NULL;
  d->oom_preallocated = NULL;
}

void
bus_connection_add_owned_service_link (DBusConnection *connection,
                                       DBusList       *link)
{
  BusConnectionData *d;

  d = BUS_CONNECTION_DATA (connection);
  _dbus_assert (d != NULL);

  _dbus_list_append_link (&d->services_owned, link);
}

dbus_bool_t
bus_connection_add_owned_service (DBusConnection *connection,
                                  BusService     *service)
{
  DBusList *link;

  link = _dbus_list_alloc_link (service);

  if (link == NULL)
    return FALSE;

  bus_connection_add_owned_service_link (connection, link);

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

dbus_bool_t
bus_connection_set_name (DBusConnection   *connection,
			 const DBusString *name)
{
  BusConnectionData *d;
  
  d = BUS_CONNECTION_DATA (connection);
  _dbus_assert (d != NULL);
  _dbus_assert (d->name == NULL);

  if (!_dbus_string_copy_data (name, &d->name))
    return FALSE;

  _dbus_assert (d->name != NULL);
  
  _dbus_verbose ("Name %s assigned to %p\n", d->name, connection);
  
  return TRUE;
}

const char *
bus_connection_get_name (DBusConnection *connection)
{
  BusConnectionData *d;
  
  d = BUS_CONNECTION_DATA (connection);
  _dbus_assert (d != NULL);
  
  return d->name;
}

/*
 * Transactions
 *
 * Note that this is fairly fragile; in particular, don't try to use
 * one transaction across any main loop iterations.
 */

typedef struct
{
  BusTransaction *transaction;
  DBusMessage    *message;
  DBusPreallocatedSend *preallocated;
} MessageToSend;

typedef struct
{
  BusTransactionCancelFunction cancel_function;
  DBusFreeFunction free_data_function;
  void *data;
} CancelHook;

struct BusTransaction
{
  DBusList *connections;
  BusContext *context;
  DBusList *cancel_hooks;
};

static void
message_to_send_free (DBusConnection *connection,
                      MessageToSend  *to_send)
{
  if (to_send->message)
    dbus_message_unref (to_send->message);

  if (to_send->preallocated)
    dbus_connection_free_preallocated_send (connection, to_send->preallocated);

  dbus_free (to_send);
}

static void
cancel_hook_cancel (void *element,
                    void *data)
{
  CancelHook *ch = element;

  _dbus_verbose ("Running transaction cancel hook\n");
  
  if (ch->cancel_function)
    (* ch->cancel_function) (ch->data);  
}

static void
cancel_hook_free (void *element,
                  void *data)
{
  CancelHook *ch = element;

  if (ch->free_data_function)
    (* ch->free_data_function) (ch->data);

  dbus_free (ch);
}

static void
free_cancel_hooks (BusTransaction *transaction)
{
  _dbus_list_foreach (&transaction->cancel_hooks,
                      cancel_hook_free, NULL);
  
  _dbus_list_clear (&transaction->cancel_hooks);
}

BusTransaction*
bus_transaction_new (BusContext *context)
{
  BusTransaction *transaction;

  transaction = dbus_new0 (BusTransaction, 1);
  if (transaction == NULL)
    return NULL;

  transaction->context = context;
  
  return transaction;
}

BusContext*
bus_transaction_get_context (BusTransaction  *transaction)
{
  return transaction->context;
}

BusConnections*
bus_transaction_get_connections (BusTransaction  *transaction)
{
  return bus_context_get_connections (transaction->context);
}

dbus_bool_t
bus_transaction_send_from_driver (BusTransaction *transaction,
                                  DBusConnection *connection,
                                  DBusMessage    *message)
{
  /* We have to set the sender to the driver, and have
   * to check security policy since it was not done in
   * dispatch.c
   */
  _dbus_verbose ("Sending %s from driver\n",
                 dbus_message_get_name (message));
  
  if (!dbus_message_set_sender (message, DBUS_SERVICE_DBUS))
    return FALSE;

  /* If security policy doesn't allow the message, we silently
   * eat it; the driver doesn't care about getting a reply.
   */
  if (!bus_context_check_security_policy (bus_transaction_get_context (transaction),
                                          NULL, connection, message, NULL))
    return TRUE;

  return bus_transaction_send (transaction, connection, message);
}

dbus_bool_t
bus_transaction_send (BusTransaction *transaction,
                      DBusConnection *connection,
                      DBusMessage    *message)
{
  MessageToSend *to_send;
  BusConnectionData *d;
  DBusList *link;

  _dbus_verbose ("  trying to add %s %s to transaction%s\n",
                 dbus_message_get_is_error (message) ? "error" :
                 dbus_message_get_reply_serial (message) != 0 ? "reply" :
                 "message",
                 dbus_message_get_name (message),
                 dbus_connection_get_is_connected (connection) ?
                 "" : " (disconnected)");

  _dbus_assert (dbus_message_get_sender (message) != NULL);
  
  if (!dbus_connection_get_is_connected (connection))
    return TRUE; /* silently ignore disconnected connections */
  
  d = BUS_CONNECTION_DATA (connection);
  _dbus_assert (d != NULL);
  
  to_send = dbus_new (MessageToSend, 1);
  if (to_send == NULL)
    {
      return FALSE;
    }

  to_send->preallocated = dbus_connection_preallocate_send (connection);
  if (to_send->preallocated == NULL)
    {
      dbus_free (to_send);
      return FALSE;
    }  
  
  dbus_message_ref (message);
  to_send->message = message;
  to_send->transaction = transaction;

  _dbus_verbose ("about to prepend message\n");
  
  if (!_dbus_list_prepend (&d->transaction_messages, to_send))
    {
      message_to_send_free (connection, to_send);
      return FALSE;
    }

  _dbus_verbose ("prepended message\n");
  
  /* See if we already had this connection in the list
   * for this transaction. If we have a pending message,
   * then we should already be in transaction->connections
   */
  link = _dbus_list_get_first_link (&d->transaction_messages);
  _dbus_assert (link->data == to_send);
  link = _dbus_list_get_next_link (&d->transaction_messages, link);
  while (link != NULL)
    {
      MessageToSend *m = link->data;
      DBusList *next = _dbus_list_get_next_link (&d->transaction_messages, link);
      
      if (m->transaction == transaction)
        break;
        
      link = next;
    }

  if (link == NULL)
    {
      if (!_dbus_list_prepend (&transaction->connections, connection))
        {
          _dbus_list_remove (&d->transaction_messages, to_send);
          message_to_send_free (connection, to_send);
          return FALSE;
        }
    }

  return TRUE;
}

static void
connection_cancel_transaction (DBusConnection *connection,
                               BusTransaction *transaction)
{
  DBusList *link;
  BusConnectionData *d;
  
  d = BUS_CONNECTION_DATA (connection);
  _dbus_assert (d != NULL);
  
  link = _dbus_list_get_first_link (&d->transaction_messages);
  while (link != NULL)
    {
      MessageToSend *m = link->data;
      DBusList *next = _dbus_list_get_next_link (&d->transaction_messages, link);
      
      if (m->transaction == transaction)
        {
          _dbus_list_remove_link (&d->transaction_messages,
                                  link);
          
          message_to_send_free (connection, m);
        }
        
      link = next;
    }
}

void
bus_transaction_cancel_and_free (BusTransaction *transaction)
{
  DBusConnection *connection;

  _dbus_verbose ("TRANSACTION: cancelled\n");
  
  while ((connection = _dbus_list_pop_first (&transaction->connections)))
    connection_cancel_transaction (connection, transaction);

  _dbus_assert (transaction->connections == NULL);

  _dbus_list_foreach (&transaction->cancel_hooks,
                      cancel_hook_cancel, NULL);

  free_cancel_hooks (transaction);
  
  dbus_free (transaction);
}

static void
connection_execute_transaction (DBusConnection *connection,
                                BusTransaction *transaction)
{
  DBusList *link;
  BusConnectionData *d;
  
  d = BUS_CONNECTION_DATA (connection);
  _dbus_assert (d != NULL);

  /* Send the queue in order (FIFO) */
  link = _dbus_list_get_last_link (&d->transaction_messages);
  while (link != NULL)
    {
      MessageToSend *m = link->data;
      DBusList *prev = _dbus_list_get_prev_link (&d->transaction_messages, link);
      
      if (m->transaction == transaction)
        {
          _dbus_list_remove_link (&d->transaction_messages,
                                  link);

          _dbus_assert (dbus_message_get_sender (m->message) != NULL);
          
          dbus_connection_send_preallocated (connection,
                                             m->preallocated,
                                             m->message,
                                             NULL);

          m->preallocated = NULL; /* so we don't double-free it */
          
          message_to_send_free (connection, m);
        }
        
      link = prev;
    }
}

void
bus_transaction_execute_and_free (BusTransaction *transaction)
{
  /* For each connection in transaction->connections
   * send the messages
   */
  DBusConnection *connection;

  _dbus_verbose ("TRANSACTION: executing\n");
  
  while ((connection = _dbus_list_pop_first (&transaction->connections)))
    connection_execute_transaction (connection, transaction);

  _dbus_assert (transaction->connections == NULL);

  free_cancel_hooks (transaction);
  
  dbus_free (transaction);
}

static void
bus_connection_remove_transactions (DBusConnection *connection)
{
  MessageToSend *to_send;
  BusConnectionData *d;
  
  d = BUS_CONNECTION_DATA (connection);
  _dbus_assert (d != NULL);
  
  while ((to_send = _dbus_list_get_first (&d->transaction_messages)))
    {
      /* only has an effect for the first MessageToSend listing this transaction */
      _dbus_list_remove (&to_send->transaction->connections,
                         connection);

      _dbus_list_remove (&d->transaction_messages, to_send);
      message_to_send_free (connection, to_send);
    }
}

/**
 * Converts the DBusError to a message reply
 */
dbus_bool_t
bus_transaction_send_error_reply (BusTransaction  *transaction,
                                  DBusConnection  *connection,
                                  const DBusError *error,
                                  DBusMessage     *in_reply_to)
{
  DBusMessage *reply;
  
  _dbus_assert (error != NULL);
  _DBUS_ASSERT_ERROR_IS_SET (error);
  
  reply = dbus_message_new_error_reply (in_reply_to,
                                        error->name,
                                        error->message);
  if (reply == NULL)
    return FALSE;

  if (!bus_transaction_send_from_driver (transaction, connection, reply))
    {
      dbus_message_unref (reply);
      return FALSE;
    }

  dbus_message_unref (reply);
  
  return TRUE;
}

dbus_bool_t
bus_transaction_add_cancel_hook (BusTransaction               *transaction,
                                 BusTransactionCancelFunction  cancel_function,
                                 void                         *data,
                                 DBusFreeFunction              free_data_function)
{
  CancelHook *ch;

  ch = dbus_new (CancelHook, 1);
  if (ch == NULL)
    return FALSE;
  
  ch->cancel_function = cancel_function;
  ch->data = data;
  ch->free_data_function = free_data_function;

  /* It's important that the hooks get run in reverse order that they
   * were added
   */
  if (!_dbus_list_prepend (&transaction->cancel_hooks, ch))
    {
      dbus_free (ch);
      return FALSE;
    }

  return TRUE;
}
