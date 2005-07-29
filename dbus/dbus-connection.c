/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-connection.c DBusConnection object
 *
 * Copyright (C) 2002, 2003, 2004, 2005  Red Hat Inc.
 *
 * Licensed under the Academic Free License version 2.1
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

#include <config.h>
#include "dbus-shared.h"
#include "dbus-connection.h"
#include "dbus-list.h"
#include "dbus-timeout.h"
#include "dbus-transport.h"
#include "dbus-watch.h"
#include "dbus-connection-internal.h"
#include "dbus-list.h"
#include "dbus-hash.h"
#include "dbus-message-internal.h"
#include "dbus-threads.h"
#include "dbus-protocol.h"
#include "dbus-dataslot.h"
#include "dbus-string.h"
#include "dbus-pending-call.h"
#include "dbus-object-tree.h"
#include "dbus-threads-internal.h"

#ifdef DBUS_DISABLE_CHECKS
#define TOOK_LOCK_CHECK(connection)
#define RELEASING_LOCK_CHECK(connection)
#define HAVE_LOCK_CHECK(connection)
#else
#define TOOK_LOCK_CHECK(connection) do {                \
    _dbus_assert (!(connection)->have_connection_lock); \
    (connection)->have_connection_lock = TRUE;          \
  } while (0)
#define RELEASING_LOCK_CHECK(connection) do {            \
    _dbus_assert ((connection)->have_connection_lock);   \
    (connection)->have_connection_lock = FALSE;          \
  } while (0)
#define HAVE_LOCK_CHECK(connection)        _dbus_assert ((connection)->have_connection_lock)
/* A "DO_NOT_HAVE_LOCK_CHECK" is impossible since we need the lock to check the flag */
#endif

#define TRACE_LOCKS 1

#define CONNECTION_LOCK(connection)   do {                                      \
    if (TRACE_LOCKS) { _dbus_verbose ("  LOCK: %s\n", _DBUS_FUNCTION_NAME); }   \
    _dbus_mutex_lock ((connection)->mutex);                                      \
    TOOK_LOCK_CHECK (connection);                                               \
  } while (0)

#define CONNECTION_UNLOCK(connection) do {                                              \
    if (TRACE_LOCKS) { _dbus_verbose ("  UNLOCK: %s\n", _DBUS_FUNCTION_NAME);  }        \
    RELEASING_LOCK_CHECK (connection);                                                  \
    _dbus_mutex_unlock ((connection)->mutex);                                            \
  } while (0)

#define DISPATCH_STATUS_NAME(s)                                            \
                     ((s) == DBUS_DISPATCH_COMPLETE ? "complete" :         \
                      (s) == DBUS_DISPATCH_DATA_REMAINS ? "data remains" : \
                      (s) == DBUS_DISPATCH_NEED_MEMORY ? "need memory" :   \
                      "???")

/**
 * @defgroup DBusConnection DBusConnection
 * @ingroup  DBus
 * @brief Connection to another application
 *
 * A DBusConnection represents a connection to another
 * application. Messages can be sent and received via this connection.
 * The other application may be a message bus; for convenience, the
 * function dbus_bus_get() is provided to automatically open a
 * connection to the well-known message buses.
 * 
 * In brief a DBusConnection is a message queue associated with some
 * message transport mechanism such as a socket.  The connection
 * maintains a queue of incoming messages and a queue of outgoing
 * messages.
 *
 * Incoming messages are normally processed by calling
 * dbus_connection_dispatch(). dbus_connection_dispatch() runs any
 * handlers registered for the topmost message in the message queue,
 * then discards the message, then returns.
 * 
 * dbus_connection_get_dispatch_status() indicates whether
 * messages are currently in the queue that need dispatching.
 * dbus_connection_set_dispatch_status_function() allows
 * you to set a function to be used to monitor the dispatch status.
 *
 * If you're using GLib or Qt add-on libraries for D-BUS, there are
 * special convenience APIs in those libraries that hide
 * all the details of dispatch and watch/timeout monitoring.
 * For example, dbus_connection_setup_with_g_main().
 *
 * If you aren't using these add-on libraries, you have to manually
 * call dbus_connection_set_dispatch_status_function(),
 * dbus_connection_set_watch_functions(),
 * dbus_connection_set_timeout_functions() providing appropriate
 * functions to integrate the connection with your application's main
 * loop.
 *
 * When you use dbus_connection_send() or one of its variants to send
 * a message, the message is added to the outgoing queue.  It's
 * actually written to the network later; either in
 * dbus_watch_handle() invoked by your main loop, or in
 * dbus_connection_flush() which blocks until it can write out the
 * entire outgoing queue. The GLib/Qt add-on libraries again
 * handle the details here for you by setting up watch functions.
 *
 * When a connection is disconnected, you are guaranteed to get a
 * signal "Disconnected" from the interface
 * #DBUS_INTERFACE_LOCAL, path
 * #DBUS_PATH_LOCAL.
 *
 * You may not drop the last reference to a #DBusConnection
 * until that connection has been disconnected.
 *
 * You may dispatch the unprocessed incoming message queue even if the
 * connection is disconnected. However, "Disconnected" will always be
 * the last message in the queue (obviously no messages are received
 * after disconnection).
 *
 * #DBusConnection has thread locks and drops them when invoking user
 * callbacks, so in general is transparently threadsafe. However,
 * #DBusMessage does NOT have thread locks; you must not send the same
 * message to multiple #DBusConnection that will be used from
 * different threads.
 */

/**
 * @defgroup DBusConnectionInternals DBusConnection implementation details
 * @ingroup  DBusInternals
 * @brief Implementation details of DBusConnection
 *
 * @{
 */

/**
 * Internal struct representing a message filter function 
 */
typedef struct DBusMessageFilter DBusMessageFilter;

/**
 * Internal struct representing a message filter function 
 */
struct DBusMessageFilter
{
  DBusAtomic refcount; /**< Reference count */
  DBusHandleMessageFunction function; /**< Function to call to filter */
  void *user_data; /**< User data for the function */
  DBusFreeFunction free_user_data_function; /**< Function to free the user data */
};


/**
 * Internals of DBusPreallocatedSend
 */
struct DBusPreallocatedSend
{
  DBusConnection *connection; /**< Connection we'd send the message to */
  DBusList *queue_link;       /**< Preallocated link in the queue */
  DBusList *counter_link;     /**< Preallocated link in the resource counter */
};

static dbus_bool_t _dbus_modify_sigpipe = TRUE;

/**
 * Implementation details of DBusConnection. All fields are private.
 */
struct DBusConnection
{
  DBusAtomic refcount; /**< Reference count. */

  DBusMutex *mutex; /**< Lock on the entire DBusConnection */

  DBusMutex *dispatch_mutex;     /**< Protects dispatch_acquired */
  DBusCondVar *dispatch_cond;    /**< Notify when dispatch_acquired is available */
  DBusMutex *io_path_mutex;      /**< Protects io_path_acquired */
  DBusCondVar *io_path_cond;     /**< Notify when io_path_acquired is available */
  
  DBusList *outgoing_messages; /**< Queue of messages we need to send, send the end of the list first. */
  DBusList *incoming_messages; /**< Queue of messages we have received, end of the list received most recently. */

  DBusMessage *message_borrowed; /**< Filled in if the first incoming message has been borrowed;
                                  *   dispatch_acquired will be set by the borrower
                                  */
  
  int n_outgoing;              /**< Length of outgoing queue. */
  int n_incoming;              /**< Length of incoming queue. */

  DBusCounter *outgoing_counter; /**< Counts size of outgoing messages. */
  
  DBusTransport *transport;    /**< Object that sends/receives messages over network. */
  DBusWatchList *watches;      /**< Stores active watches. */
  DBusTimeoutList *timeouts;   /**< Stores active timeouts. */
  
  DBusList *filter_list;        /**< List of filters. */

  DBusDataSlotList slot_list;   /**< Data stored by allocated integer ID */

  DBusHashTable *pending_replies;  /**< Hash of message serials to #DBusPendingCall. */  
  
  dbus_uint32_t client_serial;       /**< Client serial. Increments each time a message is sent  */
  DBusList *disconnect_message_link; /**< Preallocated list node for queueing the disconnection message */

  DBusWakeupMainFunction wakeup_main_function; /**< Function to wake up the mainloop  */
  void *wakeup_main_data; /**< Application data for wakeup_main_function */
  DBusFreeFunction free_wakeup_main_data; /**< free wakeup_main_data */

  DBusDispatchStatusFunction dispatch_status_function; /**< Function on dispatch status changes  */
  void *dispatch_status_data; /**< Application data for dispatch_status_function */
  DBusFreeFunction free_dispatch_status_data; /**< free dispatch_status_data */

  DBusDispatchStatus last_dispatch_status; /**< The last dispatch status we reported to the application. */

  DBusList *link_cache; /**< A cache of linked list links to prevent contention
                         *   for the global linked list mempool lock
                         */
  DBusObjectTree *objects; /**< Object path handlers registered with this connection */

  char *server_guid; /**< GUID of server if we are in shared_connections, #NULL if server GUID is unknown or connection is private */

  unsigned int shareable : 1; /**< #TRUE if connection can go in shared_connections once we know the GUID */
  
  unsigned int dispatch_acquired : 1; /**< Someone has dispatch path (can drain incoming queue) */
  unsigned int io_path_acquired : 1;  /**< Someone has transport io path (can use the transport to read/write messages) */
  
  unsigned int exit_on_disconnect : 1; /**< If #TRUE, exit after handling disconnect signal */
  
#ifndef DBUS_DISABLE_CHECKS
  unsigned int have_connection_lock : 1; /**< Used to check locking */
#endif
  
#ifndef DBUS_DISABLE_CHECKS
  int generation; /**< _dbus_current_generation that should correspond to this connection */
#endif 
};

static DBusDispatchStatus _dbus_connection_get_dispatch_status_unlocked      (DBusConnection     *connection);
static void               _dbus_connection_update_dispatch_status_and_unlock (DBusConnection     *connection,
                                                                              DBusDispatchStatus  new_status);
static void               _dbus_connection_last_unref                        (DBusConnection     *connection);
static void               _dbus_connection_acquire_dispatch                  (DBusConnection     *connection);
static void               _dbus_connection_release_dispatch                  (DBusConnection     *connection);

static DBusMessageFilter *
_dbus_message_filter_ref (DBusMessageFilter *filter)
{
  _dbus_assert (filter->refcount.value > 0);
  _dbus_atomic_inc (&filter->refcount);

  return filter;
}

static void
_dbus_message_filter_unref (DBusMessageFilter *filter)
{
  _dbus_assert (filter->refcount.value > 0);

  if (_dbus_atomic_dec (&filter->refcount) == 1)
    {
      if (filter->free_user_data_function)
        (* filter->free_user_data_function) (filter->user_data);
      
      dbus_free (filter);
    }
}

/**
 * Acquires the connection lock.
 *
 * @param connection the connection.
 */
void
_dbus_connection_lock (DBusConnection *connection)
{
  CONNECTION_LOCK (connection);
}

/**
 * Releases the connection lock.
 *
 * @param connection the connection.
 */
void
_dbus_connection_unlock (DBusConnection *connection)
{
  CONNECTION_UNLOCK (connection);
}

/**
 * Wakes up the main loop if it is sleeping
 * Needed if we're e.g. queueing outgoing messages
 * on a thread while the mainloop sleeps.
 *
 * @param connection the connection.
 */
static void
_dbus_connection_wakeup_mainloop (DBusConnection *connection)
{
  if (connection->wakeup_main_function)
    (*connection->wakeup_main_function) (connection->wakeup_main_data);
}

#ifdef DBUS_BUILD_TESTS
/* For now this function isn't used */
/**
 * Adds a message to the incoming message queue, returning #FALSE
 * if there's insufficient memory to queue the message.
 * Does not take over refcount of the message.
 *
 * @param connection the connection.
 * @param message the message to queue.
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_connection_queue_received_message (DBusConnection *connection,
                                         DBusMessage    *message)
{
  DBusList *link;

  link = _dbus_list_alloc_link (message);
  if (link == NULL)
    return FALSE;

  dbus_message_ref (message);
  _dbus_connection_queue_received_message_link (connection, link);

  return TRUE;
}
#endif

/**
 * Adds a message-containing list link to the incoming message queue,
 * taking ownership of the link and the message's current refcount.
 * Cannot fail due to lack of memory.
 *
 * @param connection the connection.
 * @param link the message link to queue.
 */
void
_dbus_connection_queue_received_message_link (DBusConnection  *connection,
                                              DBusList        *link)
{
  DBusPendingCall *pending;
  dbus_int32_t reply_serial;
  DBusMessage *message;
  
  _dbus_assert (_dbus_transport_get_is_authenticated (connection->transport));
  
  _dbus_list_append_link (&connection->incoming_messages,
                          link);
  message = link->data;

  /* If this is a reply we're waiting on, remove timeout for it */
  reply_serial = dbus_message_get_reply_serial (message);
  if (reply_serial != -1)
    {
      pending = _dbus_hash_table_lookup_int (connection->pending_replies,
                                             reply_serial);
      if (pending != NULL)
	{
	  if (pending->timeout_added)
	    _dbus_connection_remove_timeout (connection,
                                             pending->timeout);

	  pending->timeout_added = FALSE;
	}
    }
  
  connection->n_incoming += 1;

  _dbus_connection_wakeup_mainloop (connection);
  
  _dbus_verbose ("Message %p (%d %s %s %s '%s' reply to %u) added to incoming queue %p, %d incoming\n",
                 message,
                 dbus_message_get_type (message),
		 dbus_message_get_path (message),
                 dbus_message_get_interface (message) ?
                 dbus_message_get_interface (message) :
                 "no interface",
                 dbus_message_get_member (message) ?
                 dbus_message_get_member (message) :
                 "no member",
                 dbus_message_get_signature (message),
                 dbus_message_get_reply_serial (message),
                 connection,
                 connection->n_incoming);
}

/**
 * Adds a link + message to the incoming message queue.
 * Can't fail. Takes ownership of both link and message.
 *
 * @param connection the connection.
 * @param link the list node and message to queue.
 *
 * @todo This needs to wake up the mainloop if it is in
 * a poll/select and this is a multithreaded app.
 */
static void
_dbus_connection_queue_synthesized_message_link (DBusConnection *connection,
						 DBusList *link)
{
  HAVE_LOCK_CHECK (connection);
  
  _dbus_list_append_link (&connection->incoming_messages, link);

  connection->n_incoming += 1;

  _dbus_connection_wakeup_mainloop (connection);
  
  _dbus_verbose ("Synthesized message %p added to incoming queue %p, %d incoming\n",
                 link->data, connection, connection->n_incoming);
}


/**
 * Checks whether there are messages in the outgoing message queue.
 * Called with connection lock held.
 *
 * @param connection the connection.
 * @returns #TRUE if the outgoing queue is non-empty.
 */
dbus_bool_t
_dbus_connection_has_messages_to_send_unlocked (DBusConnection *connection)
{
  HAVE_LOCK_CHECK (connection);
  return connection->outgoing_messages != NULL;
}

/**
 * Checks whether there are messages in the outgoing message queue.
 *
 * @param connection the connection.
 * @returns #TRUE if the outgoing queue is non-empty.
 */
dbus_bool_t
dbus_connection_has_messages_to_send (DBusConnection *connection)
{
  dbus_bool_t v;
  
  _dbus_return_val_if_fail (connection != NULL, FALSE);

  CONNECTION_LOCK (connection);
  v = _dbus_connection_has_messages_to_send_unlocked (connection);
  CONNECTION_UNLOCK (connection);

  return v;
}

/**
 * Gets the next outgoing message. The message remains in the
 * queue, and the caller does not own a reference to it.
 *
 * @param connection the connection.
 * @returns the message to be sent.
 */ 
DBusMessage*
_dbus_connection_get_message_to_send (DBusConnection *connection)
{
  HAVE_LOCK_CHECK (connection);
  
  return _dbus_list_get_last (&connection->outgoing_messages);
}

/**
 * Notifies the connection that a message has been sent, so the
 * message can be removed from the outgoing queue.
 * Called with the connection lock held.
 *
 * @param connection the connection.
 * @param message the message that was sent.
 */
void
_dbus_connection_message_sent (DBusConnection *connection,
                               DBusMessage    *message)
{
  DBusList *link;

  HAVE_LOCK_CHECK (connection);
  
  /* This can be called before we even complete authentication, since
   * it's called on disconnect to clean up the outgoing queue.
   * It's also called as we successfully send each message.
   */
  
  link = _dbus_list_get_last_link (&connection->outgoing_messages);
  _dbus_assert (link != NULL);
  _dbus_assert (link->data == message);

  /* Save this link in the link cache */
  _dbus_list_unlink (&connection->outgoing_messages,
                     link);
  _dbus_list_prepend_link (&connection->link_cache, link);
  
  connection->n_outgoing -= 1;

  _dbus_verbose ("Message %p (%d %s %s %s '%s') removed from outgoing queue %p, %d left to send\n",
                 message,
                 dbus_message_get_type (message),
		 dbus_message_get_path (message),
                 dbus_message_get_interface (message) ?
                 dbus_message_get_interface (message) :
                 "no interface",
                 dbus_message_get_member (message) ?
                 dbus_message_get_member (message) :
                 "no member",
                 dbus_message_get_signature (message),
                 connection, connection->n_outgoing);

  /* Save this link in the link cache also */
  _dbus_message_remove_size_counter (message, connection->outgoing_counter,
                                     &link);
  _dbus_list_prepend_link (&connection->link_cache, link);
  
  dbus_message_unref (message);
}

typedef dbus_bool_t (* DBusWatchAddFunction)     (DBusWatchList *list,
                                                  DBusWatch     *watch);
typedef void        (* DBusWatchRemoveFunction)  (DBusWatchList *list,
                                                  DBusWatch     *watch);
typedef void        (* DBusWatchToggleFunction)  (DBusWatchList *list,
                                                  DBusWatch     *watch,
                                                  dbus_bool_t    enabled);

static dbus_bool_t
protected_change_watch (DBusConnection         *connection,
                        DBusWatch              *watch,
                        DBusWatchAddFunction    add_function,
                        DBusWatchRemoveFunction remove_function,
                        DBusWatchToggleFunction toggle_function,
                        dbus_bool_t             enabled)
{
  DBusWatchList *watches;
  dbus_bool_t retval;
  
  HAVE_LOCK_CHECK (connection);

  /* This isn't really safe or reasonable; a better pattern is the "do everything, then
   * drop lock and call out" one; but it has to be propagated up through all callers
   */
  
  watches = connection->watches;
  if (watches)
    {
      connection->watches = NULL;
      _dbus_connection_ref_unlocked (connection);
      CONNECTION_UNLOCK (connection);

      if (add_function)
        retval = (* add_function) (watches, watch);
      else if (remove_function)
        {
          retval = TRUE;
          (* remove_function) (watches, watch);
        }
      else
        {
          retval = TRUE;
          (* toggle_function) (watches, watch, enabled);
        }
      
      CONNECTION_LOCK (connection);
      connection->watches = watches;
      _dbus_connection_unref_unlocked (connection);

      return retval;
    }
  else
    return FALSE;
}
     

/**
 * Adds a watch using the connection's DBusAddWatchFunction if
 * available. Otherwise records the watch to be added when said
 * function is available. Also re-adds the watch if the
 * DBusAddWatchFunction changes. May fail due to lack of memory.
 *
 * @param connection the connection.
 * @param watch the watch to add.
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_connection_add_watch (DBusConnection *connection,
                            DBusWatch      *watch)
{
  return protected_change_watch (connection, watch,
                                 _dbus_watch_list_add_watch,
                                 NULL, NULL, FALSE);
}

/**
 * Removes a watch using the connection's DBusRemoveWatchFunction
 * if available. It's an error to call this function on a watch
 * that was not previously added.
 *
 * @param connection the connection.
 * @param watch the watch to remove.
 */
void
_dbus_connection_remove_watch (DBusConnection *connection,
                               DBusWatch      *watch)
{
  protected_change_watch (connection, watch,
                          NULL,
                          _dbus_watch_list_remove_watch,
                          NULL, FALSE);
}

/**
 * Toggles a watch and notifies app via connection's
 * DBusWatchToggledFunction if available. It's an error to call this
 * function on a watch that was not previously added.
 * Connection lock should be held when calling this.
 *
 * @param connection the connection.
 * @param watch the watch to toggle.
 * @param enabled whether to enable or disable
 */
void
_dbus_connection_toggle_watch (DBusConnection *connection,
                               DBusWatch      *watch,
                               dbus_bool_t     enabled)
{
  _dbus_assert (watch != NULL);

  protected_change_watch (connection, watch,
                          NULL, NULL,
                          _dbus_watch_list_toggle_watch,
                          enabled);
}

typedef dbus_bool_t (* DBusTimeoutAddFunction)    (DBusTimeoutList *list,
                                                   DBusTimeout     *timeout);
typedef void        (* DBusTimeoutRemoveFunction) (DBusTimeoutList *list,
                                                   DBusTimeout     *timeout);
typedef void        (* DBusTimeoutToggleFunction) (DBusTimeoutList *list,
                                                   DBusTimeout     *timeout,
                                                   dbus_bool_t      enabled);

static dbus_bool_t
protected_change_timeout (DBusConnection           *connection,
                          DBusTimeout              *timeout,
                          DBusTimeoutAddFunction    add_function,
                          DBusTimeoutRemoveFunction remove_function,
                          DBusTimeoutToggleFunction toggle_function,
                          dbus_bool_t               enabled)
{
  DBusTimeoutList *timeouts;
  dbus_bool_t retval;
  
  HAVE_LOCK_CHECK (connection);

  /* This isn't really safe or reasonable; a better pattern is the "do everything, then
   * drop lock and call out" one; but it has to be propagated up through all callers
   */
  
  timeouts = connection->timeouts;
  if (timeouts)
    {
      connection->timeouts = NULL;
      _dbus_connection_ref_unlocked (connection);
      CONNECTION_UNLOCK (connection);

      if (add_function)
        retval = (* add_function) (timeouts, timeout);
      else if (remove_function)
        {
          retval = TRUE;
          (* remove_function) (timeouts, timeout);
        }
      else
        {
          retval = TRUE;
          (* toggle_function) (timeouts, timeout, enabled);
        }
      
      CONNECTION_LOCK (connection);
      connection->timeouts = timeouts;
      _dbus_connection_unref_unlocked (connection);

      return retval;
    }
  else
    return FALSE;
}

/**
 * Adds a timeout using the connection's DBusAddTimeoutFunction if
 * available. Otherwise records the timeout to be added when said
 * function is available. Also re-adds the timeout if the
 * DBusAddTimeoutFunction changes. May fail due to lack of memory.
 * The timeout will fire repeatedly until removed.
 *
 * @param connection the connection.
 * @param timeout the timeout to add.
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_connection_add_timeout (DBusConnection *connection,
			      DBusTimeout    *timeout)
{
  return protected_change_timeout (connection, timeout,
                                   _dbus_timeout_list_add_timeout,
                                   NULL, NULL, FALSE);
}

/**
 * Removes a timeout using the connection's DBusRemoveTimeoutFunction
 * if available. It's an error to call this function on a timeout
 * that was not previously added.
 *
 * @param connection the connection.
 * @param timeout the timeout to remove.
 */
void
_dbus_connection_remove_timeout (DBusConnection *connection,
				 DBusTimeout    *timeout)
{
  protected_change_timeout (connection, timeout,
                            NULL,
                            _dbus_timeout_list_remove_timeout,
                            NULL, FALSE);
}

/**
 * Toggles a timeout and notifies app via connection's
 * DBusTimeoutToggledFunction if available. It's an error to call this
 * function on a timeout that was not previously added.
 *
 * @param connection the connection.
 * @param timeout the timeout to toggle.
 * @param enabled whether to enable or disable
 */
void
_dbus_connection_toggle_timeout (DBusConnection   *connection,
                                 DBusTimeout      *timeout,
                                 dbus_bool_t       enabled)
{
  protected_change_timeout (connection, timeout,
                            NULL, NULL,
                            _dbus_timeout_list_toggle_timeout,
                            enabled);
}

static dbus_bool_t
_dbus_connection_attach_pending_call_unlocked (DBusConnection  *connection,
                                               DBusPendingCall *pending)
{
  HAVE_LOCK_CHECK (connection);
  
  _dbus_assert (pending->reply_serial != 0);

  if (!_dbus_connection_add_timeout (connection, pending->timeout))
    return FALSE;
  
  if (!_dbus_hash_table_insert_int (connection->pending_replies,
                                    pending->reply_serial,
                                    pending))
    {
      _dbus_connection_remove_timeout (connection, pending->timeout);

      HAVE_LOCK_CHECK (connection);
      return FALSE;
    }
  
  pending->timeout_added = TRUE;
  pending->connection = connection;

  dbus_pending_call_ref (pending);

  HAVE_LOCK_CHECK (connection);
  
  return TRUE;
}

static void
free_pending_call_on_hash_removal (void *data)
{
  DBusPendingCall *pending;
  
  if (data == NULL)
    return;

  pending = data;

  if (pending->connection)
    {
      if (pending->timeout_added)
        {
          _dbus_connection_remove_timeout (pending->connection,
                                           pending->timeout);
          pending->timeout_added = FALSE;
        }

      pending->connection = NULL;
      
      dbus_pending_call_unref (pending);
    }
}

static void
_dbus_connection_detach_pending_call_unlocked (DBusConnection  *connection,
                                               DBusPendingCall *pending)
{
  /* Can't have a destroy notifier on the pending call if we're going to do this */

  dbus_pending_call_ref (pending);
  _dbus_hash_table_remove_int (connection->pending_replies,
                               pending->reply_serial);
  _dbus_assert (pending->connection == NULL);
  dbus_pending_call_unref (pending);
}

static void
_dbus_connection_detach_pending_call_and_unlock (DBusConnection  *connection,
                                                 DBusPendingCall *pending)
{
  /* The idea here is to avoid finalizing the pending call
   * with the lock held, since there's a destroy notifier
   * in pending call that goes out to application code.
   */
  dbus_pending_call_ref (pending);
  _dbus_hash_table_remove_int (connection->pending_replies,
                               pending->reply_serial);
  _dbus_assert (pending->connection == NULL);
  CONNECTION_UNLOCK (connection);
  dbus_pending_call_unref (pending);
}

/**
 * Removes a pending call from the connection, such that
 * the pending reply will be ignored. May drop the last
 * reference to the pending call.
 *
 * @param connection the connection
 * @param pending the pending call
 */
void
_dbus_connection_remove_pending_call (DBusConnection  *connection,
                                      DBusPendingCall *pending)
{
  CONNECTION_LOCK (connection);
  _dbus_connection_detach_pending_call_and_unlock (connection, pending);
}

/**
 * Completes a pending call with the given message,
 * or if the message is #NULL, by timing out the pending call.
 * 
 * @param pending the pending call
 * @param message the message to complete the call with, or #NULL
 *  to time out the call
 */
void
_dbus_pending_call_complete_and_unlock (DBusPendingCall *pending,
                                        DBusMessage     *message)
{
  if (message == NULL)
    {
      message = pending->timeout_link->data;
      _dbus_list_clear (&pending->timeout_link);
    }
  else
    dbus_message_ref (message);

  _dbus_verbose ("  handing message %p (%s) to pending call serial %u\n",
                 message,
                 dbus_message_get_type (message) == DBUS_MESSAGE_TYPE_METHOD_RETURN ?
                 "method return" :
                 dbus_message_get_type (message) == DBUS_MESSAGE_TYPE_ERROR ?
                 "error" : "other type",
                 pending->reply_serial);
  
  _dbus_assert (pending->reply == NULL);
  _dbus_assert (pending->reply_serial == dbus_message_get_reply_serial (message));
  pending->reply = message;
  
  dbus_pending_call_ref (pending); /* in case there's no app with a ref held */
  _dbus_connection_detach_pending_call_and_unlock (pending->connection, pending);
  
  /* Must be called unlocked since it invokes app callback */
  _dbus_pending_call_notify (pending);
  dbus_pending_call_unref (pending);
}

/**
 * Acquire the transporter I/O path. This must be done before
 * doing any I/O in the transporter. May sleep and drop the
 * IO path mutex while waiting for the I/O path.
 *
 * @param connection the connection.
 * @param timeout_milliseconds maximum blocking time, or -1 for no limit.
 * @returns TRUE if the I/O path was acquired.
 */
static dbus_bool_t
_dbus_connection_acquire_io_path (DBusConnection *connection,
				  int timeout_milliseconds)
{
  dbus_bool_t we_acquired;
  
  HAVE_LOCK_CHECK (connection);

  /* We don't want the connection to vanish */
  _dbus_connection_ref_unlocked (connection);

  /* We will only touch io_path_acquired which is protected by our mutex */
  CONNECTION_UNLOCK (connection);
  
  _dbus_verbose ("%s locking io_path_mutex\n", _DBUS_FUNCTION_NAME);
  _dbus_mutex_lock (connection->io_path_mutex);

  _dbus_verbose ("%s start connection->io_path_acquired = %d timeout = %d\n",
                 _DBUS_FUNCTION_NAME, connection->io_path_acquired, timeout_milliseconds);

  we_acquired = FALSE;
  
  if (connection->io_path_acquired)
    {
      if (timeout_milliseconds != -1)
        {
          _dbus_verbose ("%s waiting %d for IO path to be acquirable\n",
                         _DBUS_FUNCTION_NAME, timeout_milliseconds);
          _dbus_condvar_wait_timeout (connection->io_path_cond,
                                      connection->io_path_mutex,
                                      timeout_milliseconds);
        }
      else
        {
          while (connection->io_path_acquired)
            {
              _dbus_verbose ("%s waiting for IO path to be acquirable\n", _DBUS_FUNCTION_NAME);
              _dbus_condvar_wait (connection->io_path_cond, connection->io_path_mutex);
            }
        }
    }
  
  if (!connection->io_path_acquired)
    {
      we_acquired = TRUE;
      connection->io_path_acquired = TRUE;
    }
  
  _dbus_verbose ("%s end connection->io_path_acquired = %d we_acquired = %d\n",
                 _DBUS_FUNCTION_NAME, connection->io_path_acquired, we_acquired);

  _dbus_verbose ("%s unlocking io_path_mutex\n", _DBUS_FUNCTION_NAME);
  _dbus_mutex_unlock (connection->io_path_mutex);

  CONNECTION_LOCK (connection);
  
  HAVE_LOCK_CHECK (connection);

  _dbus_connection_unref_unlocked (connection);
  
  return we_acquired;
}

/**
 * Release the I/O path when you're done with it. Only call
 * after you've acquired the I/O. Wakes up at most one thread
 * currently waiting to acquire the I/O path.
 *
 * @param connection the connection.
 */
static void
_dbus_connection_release_io_path (DBusConnection *connection)
{
  HAVE_LOCK_CHECK (connection);
  
  _dbus_verbose ("%s locking io_path_mutex\n", _DBUS_FUNCTION_NAME);
  _dbus_mutex_lock (connection->io_path_mutex);
  
  _dbus_assert (connection->io_path_acquired);

  _dbus_verbose ("%s start connection->io_path_acquired = %d\n",
                 _DBUS_FUNCTION_NAME, connection->io_path_acquired);
  
  connection->io_path_acquired = FALSE;
  _dbus_condvar_wake_one (connection->io_path_cond);

  _dbus_verbose ("%s unlocking io_path_mutex\n", _DBUS_FUNCTION_NAME);
  _dbus_mutex_unlock (connection->io_path_mutex);
}

/**
 * Queues incoming messages and sends outgoing messages for this
 * connection, optionally blocking in the process. Each call to
 * _dbus_connection_do_iteration_unlocked() will call select() or poll() one
 * time and then read or write data if possible.
 *
 * The purpose of this function is to be able to flush outgoing
 * messages or queue up incoming messages without returning
 * control to the application and causing reentrancy weirdness.
 *
 * The flags parameter allows you to specify whether to
 * read incoming messages, write outgoing messages, or both,
 * and whether to block if no immediate action is possible.
 *
 * The timeout_milliseconds parameter does nothing unless the
 * iteration is blocking.
 *
 * If there are no outgoing messages and DBUS_ITERATION_DO_READING
 * wasn't specified, then it's impossible to block, even if
 * you specify DBUS_ITERATION_BLOCK; in that case the function
 * returns immediately.
 *
 * Called with connection lock held.
 * 
 * @param connection the connection.
 * @param flags iteration flags.
 * @param timeout_milliseconds maximum blocking time, or -1 for no limit.
 */
void
_dbus_connection_do_iteration_unlocked (DBusConnection *connection,
                                        unsigned int    flags,
                                        int             timeout_milliseconds)
{
  _dbus_verbose ("%s start\n", _DBUS_FUNCTION_NAME);
  
  HAVE_LOCK_CHECK (connection);
  
  if (connection->n_outgoing == 0)
    flags &= ~DBUS_ITERATION_DO_WRITING;

  if (_dbus_connection_acquire_io_path (connection,
					(flags & DBUS_ITERATION_BLOCK) ? timeout_milliseconds : 0))
    {
      HAVE_LOCK_CHECK (connection);
      
      _dbus_transport_do_iteration (connection->transport,
				    flags, timeout_milliseconds);
      _dbus_connection_release_io_path (connection);
    }

  HAVE_LOCK_CHECK (connection);

  _dbus_verbose ("%s end\n", _DBUS_FUNCTION_NAME);
}

/**
 * Creates a new connection for the given transport.  A transport
 * represents a message stream that uses some concrete mechanism, such
 * as UNIX domain sockets. May return #NULL if insufficient
 * memory exists to create the connection.
 *
 * @param transport the transport.
 * @returns the new connection, or #NULL on failure.
 */
DBusConnection*
_dbus_connection_new_for_transport (DBusTransport *transport)
{
  DBusConnection *connection;
  DBusWatchList *watch_list;
  DBusTimeoutList *timeout_list;
  DBusHashTable *pending_replies;
  DBusMutex *mutex;
  DBusMutex *io_path_mutex;
  DBusMutex *dispatch_mutex;
  DBusCondVar *dispatch_cond;
  DBusCondVar *io_path_cond;
  DBusList *disconnect_link;
  DBusMessage *disconnect_message;
  DBusCounter *outgoing_counter;
  DBusObjectTree *objects;
  
  watch_list = NULL;
  connection = NULL;
  pending_replies = NULL;
  timeout_list = NULL;
  mutex = NULL;
  io_path_mutex = NULL;
  dispatch_mutex = NULL;
  dispatch_cond = NULL;
  io_path_cond = NULL;
  disconnect_link = NULL;
  disconnect_message = NULL;
  outgoing_counter = NULL;
  objects = NULL;
  
  watch_list = _dbus_watch_list_new ();
  if (watch_list == NULL)
    goto error;

  timeout_list = _dbus_timeout_list_new ();
  if (timeout_list == NULL)
    goto error;  

  pending_replies =
    _dbus_hash_table_new (DBUS_HASH_INT,
			  NULL,
                          (DBusFreeFunction)free_pending_call_on_hash_removal);
  if (pending_replies == NULL)
    goto error;
  
  connection = dbus_new0 (DBusConnection, 1);
  if (connection == NULL)
    goto error;

  mutex = _dbus_mutex_new ();
  if (mutex == NULL)
    goto error;

  io_path_mutex = _dbus_mutex_new ();
  if (io_path_mutex == NULL)
    goto error;

  dispatch_mutex = _dbus_mutex_new ();
  if (dispatch_mutex == NULL)
    goto error;
  
  dispatch_cond = _dbus_condvar_new ();
  if (dispatch_cond == NULL)
    goto error;
  
  io_path_cond = _dbus_condvar_new ();
  if (io_path_cond == NULL)
    goto error;

  disconnect_message = dbus_message_new_signal (DBUS_PATH_LOCAL,
                                                DBUS_INTERFACE_LOCAL,
                                                "Disconnected");
  
  if (disconnect_message == NULL)
    goto error;

  disconnect_link = _dbus_list_alloc_link (disconnect_message);
  if (disconnect_link == NULL)
    goto error;

  outgoing_counter = _dbus_counter_new ();
  if (outgoing_counter == NULL)
    goto error;

  objects = _dbus_object_tree_new (connection);
  if (objects == NULL)
    goto error;
  
  if (_dbus_modify_sigpipe)
    _dbus_disable_sigpipe ();
  
  connection->refcount.value = 1;
  connection->mutex = mutex;
  connection->dispatch_cond = dispatch_cond;
  connection->dispatch_mutex = dispatch_mutex;
  connection->io_path_cond = io_path_cond;
  connection->io_path_mutex = io_path_mutex;
  connection->transport = transport;
  connection->watches = watch_list;
  connection->timeouts = timeout_list;
  connection->pending_replies = pending_replies;
  connection->outgoing_counter = outgoing_counter;
  connection->filter_list = NULL;
  connection->last_dispatch_status = DBUS_DISPATCH_COMPLETE; /* so we're notified first time there's data */
  connection->objects = objects;
  connection->exit_on_disconnect = FALSE;
  connection->shareable = FALSE;
#ifndef DBUS_DISABLE_CHECKS
  connection->generation = _dbus_current_generation;
#endif
  
  _dbus_data_slot_list_init (&connection->slot_list);

  connection->client_serial = 1;

  connection->disconnect_message_link = disconnect_link;

  CONNECTION_LOCK (connection);
  
  if (!_dbus_transport_set_connection (transport, connection))
    goto error;

  _dbus_transport_ref (transport);

  CONNECTION_UNLOCK (connection);
  
  return connection;
  
 error:
  if (disconnect_message != NULL)
    dbus_message_unref (disconnect_message);
  
  if (disconnect_link != NULL)
    _dbus_list_free_link (disconnect_link);
  
  if (io_path_cond != NULL)
    _dbus_condvar_free (io_path_cond);
  
  if (dispatch_cond != NULL)
    _dbus_condvar_free (dispatch_cond);
  
  if (mutex != NULL)
    _dbus_mutex_free (mutex);

  if (io_path_mutex != NULL)
    _dbus_mutex_free (io_path_mutex);

  if (dispatch_mutex != NULL)
    _dbus_mutex_free (dispatch_mutex);
  
  if (connection != NULL)
    dbus_free (connection);

  if (pending_replies)
    _dbus_hash_table_unref (pending_replies);
  
  if (watch_list)
    _dbus_watch_list_free (watch_list);

  if (timeout_list)
    _dbus_timeout_list_free (timeout_list);

  if (outgoing_counter)
    _dbus_counter_unref (outgoing_counter);

  if (objects)
    _dbus_object_tree_unref (objects);
  
  return NULL;
}

/**
 * Increments the reference count of a DBusConnection.
 * Requires that the caller already holds the connection lock.
 *
 * @param connection the connection.
 * @returns the connection.
 */
DBusConnection *
_dbus_connection_ref_unlocked (DBusConnection *connection)
{  
  _dbus_assert (connection != NULL);
  _dbus_assert (connection->generation == _dbus_current_generation);

  HAVE_LOCK_CHECK (connection);
  
#ifdef DBUS_HAVE_ATOMIC_INT
  _dbus_atomic_inc (&connection->refcount);
#else
  _dbus_assert (connection->refcount.value > 0);
  connection->refcount.value += 1;
#endif

  return connection;
}

/**
 * Decrements the reference count of a DBusConnection.
 * Requires that the caller already holds the connection lock.
 *
 * @param connection the connection.
 */
void
_dbus_connection_unref_unlocked (DBusConnection *connection)
{
  dbus_bool_t last_unref;

  HAVE_LOCK_CHECK (connection);
  
  _dbus_assert (connection != NULL);

  /* The connection lock is better than the global
   * lock in the atomic increment fallback
   */
  
#ifdef DBUS_HAVE_ATOMIC_INT
  last_unref = (_dbus_atomic_dec (&connection->refcount) == 1);
#else
  _dbus_assert (connection->refcount.value > 0);

  connection->refcount.value -= 1;
  last_unref = (connection->refcount.value == 0);  
#if 0
  printf ("unref_unlocked() connection %p count = %d\n", connection, connection->refcount.value);
#endif
#endif
  
  if (last_unref)
    _dbus_connection_last_unref (connection);
}

static dbus_uint32_t
_dbus_connection_get_next_client_serial (DBusConnection *connection)
{
  int serial;

  serial = connection->client_serial++;

  if (connection->client_serial < 0)
    connection->client_serial = 1;
  
  return serial;
}

/**
 * A callback for use with dbus_watch_new() to create a DBusWatch.
 * 
 * @todo This is basically a hack - we could delete _dbus_transport_handle_watch()
 * and the virtual handle_watch in DBusTransport if we got rid of it.
 * The reason this is some work is threading, see the _dbus_connection_handle_watch()
 * implementation.
 *
 * @param watch the watch.
 * @param condition the current condition of the file descriptors being watched.
 * @param data must be a pointer to a #DBusConnection
 * @returns #FALSE if the IO condition may not have been fully handled due to lack of memory
 */
dbus_bool_t
_dbus_connection_handle_watch (DBusWatch                   *watch,
                               unsigned int                 condition,
                               void                        *data)
{
  DBusConnection *connection;
  dbus_bool_t retval;
  DBusDispatchStatus status;

  connection = data;

  _dbus_verbose ("%s start\n", _DBUS_FUNCTION_NAME);
  
  CONNECTION_LOCK (connection);
  _dbus_connection_acquire_io_path (connection, -1);
  HAVE_LOCK_CHECK (connection);
  retval = _dbus_transport_handle_watch (connection->transport,
                                         watch, condition);

  _dbus_connection_release_io_path (connection);

  HAVE_LOCK_CHECK (connection);

  _dbus_verbose ("%s middle\n", _DBUS_FUNCTION_NAME);
  
  status = _dbus_connection_get_dispatch_status_unlocked (connection);

  /* this calls out to user code */
  _dbus_connection_update_dispatch_status_and_unlock (connection, status);

  _dbus_verbose ("%s end\n", _DBUS_FUNCTION_NAME);
  
  return retval;
}

_DBUS_DEFINE_GLOBAL_LOCK (shared_connections);
static DBusHashTable *shared_connections = NULL;

static void
shared_connections_shutdown (void *data)
{
  _DBUS_LOCK (shared_connections);

  _dbus_assert (_dbus_hash_table_get_n_entries (shared_connections) == 0);
  _dbus_hash_table_unref (shared_connections);
  shared_connections = NULL;
  
  _DBUS_UNLOCK (shared_connections);
}

static dbus_bool_t
connection_lookup_shared (DBusAddressEntry  *entry,
                          DBusConnection   **result)
{
  _dbus_verbose ("checking for existing connection\n");
  
  *result = NULL;
  
  _DBUS_LOCK (shared_connections);

  if (shared_connections == NULL)
    {
      _dbus_verbose ("creating shared_connections hash table\n");
      
      shared_connections = _dbus_hash_table_new (DBUS_HASH_STRING,
                                                 dbus_free,
                                                 NULL);
      if (shared_connections == NULL)
        {
          _DBUS_UNLOCK (shared_connections);
          return FALSE;
        }

      if (!_dbus_register_shutdown_func (shared_connections_shutdown, NULL))
        {
          _dbus_hash_table_unref (shared_connections);
          shared_connections = NULL;
          _DBUS_UNLOCK (shared_connections);
          return FALSE;
        }

      _dbus_verbose ("  successfully created shared_connections\n");
      
      _DBUS_UNLOCK (shared_connections);
      return TRUE; /* no point looking up in the hash we just made */
    }
  else
    {
      const char *guid;

      guid = dbus_address_entry_get_value (entry, "guid");
      
      if (guid != NULL)
        {
          *result = _dbus_hash_table_lookup_string (shared_connections,
                                                    guid);

          if (*result)
            {
              /* The DBusConnection can't have been disconnected
               * between the lookup and this code, because the
               * disconnection will take the shared_connections lock to
               * remove the connection. It can't have been finalized
               * since you have to disconnect prior to finalize.
               *
               * Thus it's safe to ref the connection.
               */
              dbus_connection_ref (*result);

              _dbus_verbose ("looked up existing connection to server guid %s\n",
                             guid);
            }
        }
      
      _DBUS_UNLOCK (shared_connections);
      return TRUE;
    }
}

static dbus_bool_t
connection_record_shared_unlocked (DBusConnection *connection,
                                   const char     *guid)
{
  char *guid_key;
  char *guid_in_connection;

  /* A separate copy of the key is required in the hash table, because
   * we don't have a lock on the connection when we are doing a hash
   * lookup.
   */
  
  _dbus_assert (connection->server_guid == NULL);
  _dbus_assert (connection->shareable);
  
  guid_key = _dbus_strdup (guid);
  if (guid_key == NULL)
    return FALSE;

  guid_in_connection = _dbus_strdup (guid);
  if (guid_in_connection == NULL)
    {
      dbus_free (guid_key);
      return FALSE;
    }
  
  _DBUS_LOCK (shared_connections);
  _dbus_assert (shared_connections != NULL);
  
  if (!_dbus_hash_table_insert_string (shared_connections,
                                       guid_key, connection))
    {
      dbus_free (guid_key);
      dbus_free (guid_in_connection);
      _DBUS_UNLOCK (shared_connections);
      return FALSE;
    }

  connection->server_guid = guid_in_connection;

  _dbus_verbose ("stored connection to %s to be shared\n",
                 connection->server_guid);
  
  _DBUS_UNLOCK (shared_connections);

  _dbus_assert (connection->server_guid != NULL);
  
  return TRUE;
}

static void
connection_forget_shared_unlocked (DBusConnection *connection)
{
  HAVE_LOCK_CHECK (connection);
  
  if (connection->server_guid == NULL)
    return;

  _dbus_verbose ("dropping connection to %s out of the shared table\n",
                 connection->server_guid);
  
  _DBUS_LOCK (shared_connections);

  if (!_dbus_hash_table_remove_string (shared_connections,
                                       connection->server_guid))
    _dbus_assert_not_reached ("connection was not in the shared table");
  
  dbus_free (connection->server_guid);
  connection->server_guid = NULL;

  _DBUS_UNLOCK (shared_connections);
}

static DBusConnection*
connection_try_from_address_entry (DBusAddressEntry *entry,
                                   DBusError        *error)
{
  DBusTransport *transport;
  DBusConnection *connection;

  transport = _dbus_transport_open (entry, error);

  if (transport == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      return NULL;
    }

  connection = _dbus_connection_new_for_transport (transport);

  _dbus_transport_unref (transport);
  
  if (connection == NULL)
    {
      _DBUS_SET_OOM (error);
      return NULL;
    }

#ifndef DBUS_DISABLE_CHECKS
  _dbus_assert (!connection->have_connection_lock);
#endif
  return connection;
}

/*
 * If the shared parameter is true, then any existing connection will
 * be used (and if a new connection is created, it will be available
 * for use by others). If the shared parameter is false, a new
 * connection will always be created, and the new connection will
 * never be returned to other callers.
 *
 * @param address the address
 * @param shared whether the connection is shared or private
 * @param error error return
 * @returns the connection or #NULL on error
 */
static DBusConnection*
_dbus_connection_open_internal (const char     *address,
                                dbus_bool_t     shared,
                                DBusError      *error)
{
  DBusConnection *connection;
  DBusAddressEntry **entries;
  DBusError tmp_error;
  DBusError first_error;
  int len, i;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  _dbus_verbose ("opening %s connection to: %s\n",
                 shared ? "shared" : "private", address);
  
  if (!dbus_parse_address (address, &entries, &len, error))
    return NULL;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  connection = NULL;

  dbus_error_init (&tmp_error);
  dbus_error_init (&first_error);
  for (i = 0; i < len; i++)
    {
      if (shared)
        {
          if (!connection_lookup_shared (entries[i], &connection))
            _DBUS_SET_OOM (&tmp_error);
        }

      if (connection == NULL)
        {
          connection = connection_try_from_address_entry (entries[i],
                                                          &tmp_error);
          
          if (connection != NULL && shared)
            {
              const char *guid;

              connection->shareable = TRUE;
              
              guid = dbus_address_entry_get_value (entries[i], "guid");

              /* we don't have a connection lock but we know nobody
               * else has a handle to the connection
               */
              
              if (guid &&
                  !connection_record_shared_unlocked (connection, guid))
                {
                  _DBUS_SET_OOM (&tmp_error);
                  dbus_connection_close (connection);
                  dbus_connection_unref (connection);
                  connection = NULL;
                }

              /* but as of now the connection is possibly shared
               * since another thread could have pulled it from the table
               */
            }
        }
      
      if (connection)
	break;

      _DBUS_ASSERT_ERROR_IS_SET (&tmp_error);
      
      if (i == 0)
        dbus_move_error (&tmp_error, &first_error);
      else
        dbus_error_free (&tmp_error);
    }

  /* NOTE we don't have a lock on a possibly-shared connection object */
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  _DBUS_ASSERT_ERROR_IS_CLEAR (&tmp_error);
  
  if (connection == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (&first_error);
      dbus_move_error (&first_error, error);
    }
  else
    {
      dbus_error_free (&first_error);
    }
  
  dbus_address_entries_free (entries);
  return connection;
}

/** @} */

/**
 * @addtogroup DBusConnection
 *
 * @{
 */

/**
 * Gets a connection to a remote address. If a connection to the given
 * address already exists, returns the existing connection with its
 * reference count incremented.  Otherwise, returns a new connection
 * and saves the new connection for possible re-use if a future call
 * to dbus_connection_open() asks to connect to the same server.
 *
 * Use dbus_connection_open_private() to get a dedicated connection
 * not shared with other callers of dbus_connection_open().
 *
 * If the open fails, the function returns #NULL, and provides a
 * reason for the failure in the error parameter. Pass #NULL for the
 * error parameter if you aren't interested in the reason for
 * failure.
 * 
 * @param address the address.
 * @param error address where an error can be returned.
 * @returns new connection, or #NULL on failure.
 */
DBusConnection*
dbus_connection_open (const char     *address,
                      DBusError      *error)
{
  DBusConnection *connection;

  _dbus_return_val_if_fail (address != NULL, NULL);
  _dbus_return_val_if_error_is_set (error, NULL);

  connection = _dbus_connection_open_internal (address,
                                               TRUE,
                                               error);

  return connection;
}

/**
 * Opens a new, dedicated connection to a remote address. Unlike
 * dbus_connection_open(), always creates a new connection.
 * This connection will not be saved or recycled by libdbus.
 *
 * If the open fails, the function returns #NULL, and provides a
 * reason for the failure in the error parameter. Pass #NULL for the
 * error parameter if you aren't interested in the reason for
 * failure.
 * 
 * @param address the address.
 * @param error address where an error can be returned.
 * @returns new connection, or #NULL on failure.
 */
DBusConnection*
dbus_connection_open_private (const char     *address,
                              DBusError      *error)
{
  DBusConnection *connection;

  _dbus_return_val_if_fail (address != NULL, NULL);
  _dbus_return_val_if_error_is_set (error, NULL);

  connection = _dbus_connection_open_internal (address,
                                               FALSE,
                                               error);

  return connection;
}

/**
 * Increments the reference count of a DBusConnection.
 *
 * @param connection the connection.
 * @returns the connection.
 */
DBusConnection *
dbus_connection_ref (DBusConnection *connection)
{
  _dbus_return_val_if_fail (connection != NULL, NULL);
  _dbus_return_val_if_fail (connection->generation == _dbus_current_generation, NULL);
  
  /* The connection lock is better than the global
   * lock in the atomic increment fallback
   */
  
#ifdef DBUS_HAVE_ATOMIC_INT
  _dbus_atomic_inc (&connection->refcount);
#else
  CONNECTION_LOCK (connection);
  _dbus_assert (connection->refcount.value > 0);

  connection->refcount.value += 1;
  CONNECTION_UNLOCK (connection);
#endif

  return connection;
}

static void
free_outgoing_message (void *element,
                       void *data)
{
  DBusMessage *message = element;
  DBusConnection *connection = data;

  _dbus_message_remove_size_counter (message,
                                     connection->outgoing_counter,
                                     NULL);
  dbus_message_unref (message);
}

/* This is run without the mutex held, but after the last reference
 * to the connection has been dropped we should have no thread-related
 * problems
 */
static void
_dbus_connection_last_unref (DBusConnection *connection)
{
  DBusList *link;

  _dbus_verbose ("Finalizing connection %p\n", connection);
  
  _dbus_assert (connection->refcount.value == 0);
  
  /* You have to disconnect the connection before unref:ing it. Otherwise
   * you won't get the disconnected message.
   */
  _dbus_assert (!_dbus_transport_get_is_connected (connection->transport));
  _dbus_assert (connection->server_guid == NULL);
  
  /* ---- We're going to call various application callbacks here, hope it doesn't break anything... */
  _dbus_object_tree_free_all_unlocked (connection->objects);
  
  dbus_connection_set_dispatch_status_function (connection, NULL, NULL, NULL);
  dbus_connection_set_wakeup_main_function (connection, NULL, NULL, NULL);
  dbus_connection_set_unix_user_function (connection, NULL, NULL, NULL);
  
  _dbus_watch_list_free (connection->watches);
  connection->watches = NULL;
  
  _dbus_timeout_list_free (connection->timeouts);
  connection->timeouts = NULL;

  _dbus_data_slot_list_free (&connection->slot_list);
  
  link = _dbus_list_get_first_link (&connection->filter_list);
  while (link != NULL)
    {
      DBusMessageFilter *filter = link->data;
      DBusList *next = _dbus_list_get_next_link (&connection->filter_list, link);

      filter->function = NULL;
      _dbus_message_filter_unref (filter); /* calls app callback */
      link->data = NULL;
      
      link = next;
    }
  _dbus_list_clear (&connection->filter_list);
  
  /* ---- Done with stuff that invokes application callbacks */

  _dbus_object_tree_unref (connection->objects);  

  _dbus_hash_table_unref (connection->pending_replies);
  connection->pending_replies = NULL;
  
  _dbus_list_clear (&connection->filter_list);
  
  _dbus_list_foreach (&connection->outgoing_messages,
                      free_outgoing_message,
		      connection);
  _dbus_list_clear (&connection->outgoing_messages);
  
  _dbus_list_foreach (&connection->incoming_messages,
		      (DBusForeachFunction) dbus_message_unref,
		      NULL);
  _dbus_list_clear (&connection->incoming_messages);

  _dbus_counter_unref (connection->outgoing_counter);

  _dbus_transport_unref (connection->transport);

  if (connection->disconnect_message_link)
    {
      DBusMessage *message = connection->disconnect_message_link->data;
      dbus_message_unref (message);
      _dbus_list_free_link (connection->disconnect_message_link);
    }

  _dbus_list_clear (&connection->link_cache);
  
  _dbus_condvar_free (connection->dispatch_cond);
  _dbus_condvar_free (connection->io_path_cond);

  _dbus_mutex_free (connection->io_path_mutex);
  _dbus_mutex_free (connection->dispatch_mutex);

  _dbus_mutex_free (connection->mutex);
  
  dbus_free (connection);
}

/**
 * Decrements the reference count of a DBusConnection, and finalizes
 * it if the count reaches zero.  It is a bug to drop the last reference
 * to a connection that has not been disconnected.
 *
 * @todo in practice it can be quite tricky to never unref a connection
 * that's still connected; maybe there's some way we could avoid
 * the requirement.
 *
 * @param connection the connection.
 */
void
dbus_connection_unref (DBusConnection *connection)
{
  dbus_bool_t last_unref;

  _dbus_return_if_fail (connection != NULL);
  _dbus_return_if_fail (connection->generation == _dbus_current_generation);
  
  /* The connection lock is better than the global
   * lock in the atomic increment fallback
   */
  
#ifdef DBUS_HAVE_ATOMIC_INT
  last_unref = (_dbus_atomic_dec (&connection->refcount) == 1);
#else
  CONNECTION_LOCK (connection);
  
  _dbus_assert (connection->refcount.value > 0);

  connection->refcount.value -= 1;
  last_unref = (connection->refcount.value == 0);

#if 0
  printf ("unref() connection %p count = %d\n", connection, connection->refcount.value);
#endif
  
  CONNECTION_UNLOCK (connection);
#endif
  
  if (last_unref)
    _dbus_connection_last_unref (connection);
}

/**
 * Closes the connection, so no further data can be sent or received.
 * Any further attempts to send data will result in errors.  This
 * function does not affect the connection's reference count.  It's
 * safe to disconnect a connection more than once; all calls after the
 * first do nothing. It's impossible to "reopen" a connection, a
 * new connection must be created. This function may result in a call
 * to the DBusDispatchStatusFunction set with
 * dbus_connection_set_dispatch_status_function(), as the disconnect
 * message it generates needs to be dispatched.
 *
 * @param connection the connection.
 */
void
dbus_connection_close (DBusConnection *connection)
{
  DBusDispatchStatus status;
  
  _dbus_return_if_fail (connection != NULL);
  _dbus_return_if_fail (connection->generation == _dbus_current_generation);

  _dbus_verbose ("Disconnecting %p\n", connection);
  
  CONNECTION_LOCK (connection);
  
  _dbus_transport_disconnect (connection->transport);

  _dbus_verbose ("%s middle\n", _DBUS_FUNCTION_NAME);
  status = _dbus_connection_get_dispatch_status_unlocked (connection);

  /* this calls out to user code */
  _dbus_connection_update_dispatch_status_and_unlock (connection, status);
}

/** Alias for dbus_connection_close(). This method is DEPRECATED and will be
 *  removed for 1.0. Change your code to use dbus_connection_close() instead.
 *
 * @param connection the connection.
 * @deprecated
 */
void
dbus_connection_disconnect (DBusConnection *connection)
{
  dbus_connection_close (connection);
}

static dbus_bool_t
_dbus_connection_get_is_connected_unlocked (DBusConnection *connection)
{
  HAVE_LOCK_CHECK (connection);
  return _dbus_transport_get_is_connected (connection->transport);
}

/**
 * Gets whether the connection is currently connected.  All
 * connections are connected when they are opened.  A connection may
 * become disconnected when the remote application closes its end, or
 * exits; a connection may also be disconnected with
 * dbus_connection_close().
 *
 * @param connection the connection.
 * @returns #TRUE if the connection is still alive.
 */
dbus_bool_t
dbus_connection_get_is_connected (DBusConnection *connection)
{
  dbus_bool_t res;

  _dbus_return_val_if_fail (connection != NULL, FALSE);
  
  CONNECTION_LOCK (connection);
  res = _dbus_connection_get_is_connected_unlocked (connection);
  CONNECTION_UNLOCK (connection);
  
  return res;
}

/**
 * Gets whether the connection was authenticated. (Note that
 * if the connection was authenticated then disconnected,
 * this function still returns #TRUE)
 *
 * @param connection the connection
 * @returns #TRUE if the connection was ever authenticated
 */
dbus_bool_t
dbus_connection_get_is_authenticated (DBusConnection *connection)
{
  dbus_bool_t res;

  _dbus_return_val_if_fail (connection != NULL, FALSE);
  
  CONNECTION_LOCK (connection);
  res = _dbus_transport_get_is_authenticated (connection->transport);
  CONNECTION_UNLOCK (connection);
  
  return res;
}

/**
 * Set whether _exit() should be called when the connection receives a
 * disconnect signal. The call to _exit() comes after any handlers for
 * the disconnect signal run; handlers can cancel the exit by calling
 * this function.
 *
 * By default, exit_on_disconnect is #FALSE; but for message bus
 * connections returned from dbus_bus_get() it will be toggled on
 * by default.
 *
 * @param connection the connection
 * @param exit_on_disconnect #TRUE if _exit() should be called after a disconnect signal
 */
void
dbus_connection_set_exit_on_disconnect (DBusConnection *connection,
                                        dbus_bool_t     exit_on_disconnect)
{
  _dbus_return_if_fail (connection != NULL);

  CONNECTION_LOCK (connection);
  connection->exit_on_disconnect = exit_on_disconnect != FALSE;
  CONNECTION_UNLOCK (connection);
}

static DBusPreallocatedSend*
_dbus_connection_preallocate_send_unlocked (DBusConnection *connection)
{
  DBusPreallocatedSend *preallocated;

  HAVE_LOCK_CHECK (connection);
  
  _dbus_assert (connection != NULL);
  
  preallocated = dbus_new (DBusPreallocatedSend, 1);
  if (preallocated == NULL)
    return NULL;

  if (connection->link_cache != NULL)
    {
      preallocated->queue_link =
        _dbus_list_pop_first_link (&connection->link_cache);
      preallocated->queue_link->data = NULL;
    }
  else
    {
      preallocated->queue_link = _dbus_list_alloc_link (NULL);
      if (preallocated->queue_link == NULL)
        goto failed_0;
    }
  
  if (connection->link_cache != NULL)
    {
      preallocated->counter_link =
        _dbus_list_pop_first_link (&connection->link_cache);
      preallocated->counter_link->data = connection->outgoing_counter;
    }
  else
    {
      preallocated->counter_link = _dbus_list_alloc_link (connection->outgoing_counter);
      if (preallocated->counter_link == NULL)
        goto failed_1;
    }

  _dbus_counter_ref (preallocated->counter_link->data);

  preallocated->connection = connection;
  
  return preallocated;
  
 failed_1:
  _dbus_list_free_link (preallocated->queue_link);
 failed_0:
  dbus_free (preallocated);
  
  return NULL;
}

/**
 * Preallocates resources needed to send a message, allowing the message 
 * to be sent without the possibility of memory allocation failure.
 * Allows apps to create a future guarantee that they can send
 * a message regardless of memory shortages.
 *
 * @param connection the connection we're preallocating for.
 * @returns the preallocated resources, or #NULL
 */
DBusPreallocatedSend*
dbus_connection_preallocate_send (DBusConnection *connection)
{
  DBusPreallocatedSend *preallocated;

  _dbus_return_val_if_fail (connection != NULL, NULL);

  CONNECTION_LOCK (connection);
  
  preallocated =
    _dbus_connection_preallocate_send_unlocked (connection);

  CONNECTION_UNLOCK (connection);

  return preallocated;
}

/**
 * Frees preallocated message-sending resources from
 * dbus_connection_preallocate_send(). Should only
 * be called if the preallocated resources are not used
 * to send a message.
 *
 * @param connection the connection
 * @param preallocated the resources
 */
void
dbus_connection_free_preallocated_send (DBusConnection       *connection,
                                        DBusPreallocatedSend *preallocated)
{
  _dbus_return_if_fail (connection != NULL);
  _dbus_return_if_fail (preallocated != NULL);  
  _dbus_return_if_fail (connection == preallocated->connection);

  _dbus_list_free_link (preallocated->queue_link);
  _dbus_counter_unref (preallocated->counter_link->data);
  _dbus_list_free_link (preallocated->counter_link);
  dbus_free (preallocated);
}

/* Called with lock held, does not update dispatch status */
static void
_dbus_connection_send_preallocated_unlocked_no_update (DBusConnection       *connection,
                                                       DBusPreallocatedSend *preallocated,
                                                       DBusMessage          *message,
                                                       dbus_uint32_t        *client_serial)
{
  dbus_uint32_t serial;
  const char *sig;

  preallocated->queue_link->data = message;
  _dbus_list_prepend_link (&connection->outgoing_messages,
                           preallocated->queue_link);

  _dbus_message_add_size_counter_link (message,
                                       preallocated->counter_link);

  dbus_free (preallocated);
  preallocated = NULL;
  
  dbus_message_ref (message);
  
  connection->n_outgoing += 1;

  sig = dbus_message_get_signature (message);
  
  _dbus_verbose ("Message %p (%d %s %s %s '%s') for %s added to outgoing queue %p, %d pending to send\n",
                 message,
                 dbus_message_get_type (message),
		 dbus_message_get_path (message),
                 dbus_message_get_interface (message) ?
                 dbus_message_get_interface (message) :
                 "no interface",
                 dbus_message_get_member (message) ?
                 dbus_message_get_member (message) :
                 "no member",
                 sig,
                 dbus_message_get_destination (message) ?
                 dbus_message_get_destination (message) :
                 "null",
                 connection,
                 connection->n_outgoing);

  if (dbus_message_get_serial (message) == 0)
    {
      serial = _dbus_connection_get_next_client_serial (connection);
      _dbus_message_set_serial (message, serial);
      if (client_serial)
        *client_serial = serial;
    }
  else
    {
      if (client_serial)
        *client_serial = dbus_message_get_serial (message);
    }

  _dbus_verbose ("Message %p serial is %u\n",
                 message, dbus_message_get_serial (message));
  
  _dbus_message_lock (message);

  /* Now we need to run an iteration to hopefully just write the messages
   * out immediately, and otherwise get them queued up
   */
  _dbus_connection_do_iteration_unlocked (connection,
                                          DBUS_ITERATION_DO_WRITING,
                                          -1);

  /* If stuff is still queued up, be sure we wake up the main loop */
  if (connection->n_outgoing > 0)
    _dbus_connection_wakeup_mainloop (connection);
}

static void
_dbus_connection_send_preallocated_and_unlock (DBusConnection       *connection,
					       DBusPreallocatedSend *preallocated,
					       DBusMessage          *message,
					       dbus_uint32_t        *client_serial)
{
  DBusDispatchStatus status;

  HAVE_LOCK_CHECK (connection);
  
  _dbus_connection_send_preallocated_unlocked_no_update (connection,
                                                         preallocated,
                                                         message, client_serial);

  _dbus_verbose ("%s middle\n", _DBUS_FUNCTION_NAME);
  status = _dbus_connection_get_dispatch_status_unlocked (connection);

  /* this calls out to user code */
  _dbus_connection_update_dispatch_status_and_unlock (connection, status);
}

/**
 * Sends a message using preallocated resources. This function cannot fail.
 * It works identically to dbus_connection_send() in other respects.
 * Preallocated resources comes from dbus_connection_preallocate_send().
 * This function "consumes" the preallocated resources, they need not
 * be freed separately.
 *
 * @param connection the connection
 * @param preallocated the preallocated resources
 * @param message the message to send
 * @param client_serial return location for client serial assigned to the message
 */
void
dbus_connection_send_preallocated (DBusConnection       *connection,
                                   DBusPreallocatedSend *preallocated,
                                   DBusMessage          *message,
                                   dbus_uint32_t        *client_serial)
{
  _dbus_return_if_fail (connection != NULL);
  _dbus_return_if_fail (preallocated != NULL);
  _dbus_return_if_fail (message != NULL);
  _dbus_return_if_fail (preallocated->connection == connection);
  _dbus_return_if_fail (dbus_message_get_type (message) != DBUS_MESSAGE_TYPE_METHOD_CALL ||
                        (dbus_message_get_interface (message) != NULL &&
                         dbus_message_get_member (message) != NULL));
  _dbus_return_if_fail (dbus_message_get_type (message) != DBUS_MESSAGE_TYPE_SIGNAL ||
                        (dbus_message_get_interface (message) != NULL &&
                         dbus_message_get_member (message) != NULL));
  
  CONNECTION_LOCK (connection);
  _dbus_connection_send_preallocated_and_unlock (connection,
						 preallocated,
						 message, client_serial);
}

static dbus_bool_t
_dbus_connection_send_unlocked_no_update (DBusConnection *connection,
                                          DBusMessage    *message,
                                          dbus_uint32_t  *client_serial)
{
  DBusPreallocatedSend *preallocated;

  _dbus_assert (connection != NULL);
  _dbus_assert (message != NULL);
  
  preallocated = _dbus_connection_preallocate_send_unlocked (connection);
  if (preallocated == NULL)
    return FALSE;

  _dbus_connection_send_preallocated_unlocked_no_update (connection,
                                                         preallocated,
                                                         message,
                                                         client_serial);
  return TRUE;
}

dbus_bool_t
_dbus_connection_send_and_unlock (DBusConnection *connection,
				  DBusMessage    *message,
				  dbus_uint32_t  *client_serial)
{
  DBusPreallocatedSend *preallocated;

  _dbus_assert (connection != NULL);
  _dbus_assert (message != NULL);
  
  preallocated = _dbus_connection_preallocate_send_unlocked (connection);
  if (preallocated == NULL)
    {
      CONNECTION_UNLOCK (connection);
      return FALSE;
    }

  _dbus_connection_send_preallocated_and_unlock (connection,
						 preallocated,
						 message,
						 client_serial);
  return TRUE;
}

/**
 * Adds a message to the outgoing message queue. Does not block to
 * write the message to the network; that happens asynchronously. To
 * force the message to be written, call dbus_connection_flush().
 * Because this only queues the message, the only reason it can
 * fail is lack of memory. Even if the connection is disconnected,
 * no error will be returned.
 *
 * If the function fails due to lack of memory, it returns #FALSE.
 * The function will never fail for other reasons; even if the
 * connection is disconnected, you can queue an outgoing message,
 * though obviously it won't be sent.
 * 
 * @param connection the connection.
 * @param message the message to write.
 * @param client_serial return location for client serial.
 * @returns #TRUE on success.
 */
dbus_bool_t
dbus_connection_send (DBusConnection *connection,
                      DBusMessage    *message,
                      dbus_uint32_t  *client_serial)
{
  _dbus_return_val_if_fail (connection != NULL, FALSE);
  _dbus_return_val_if_fail (message != NULL, FALSE);

  CONNECTION_LOCK (connection);

  return _dbus_connection_send_and_unlock (connection,
					   message,
					   client_serial);
}

static dbus_bool_t
reply_handler_timeout (void *data)
{
  DBusConnection *connection;
  DBusDispatchStatus status;
  DBusPendingCall *pending = data;

  connection = pending->connection;
  
  CONNECTION_LOCK (connection);
  if (pending->timeout_link)
    {
      _dbus_connection_queue_synthesized_message_link (connection,
						       pending->timeout_link);
      pending->timeout_link = NULL;
    }

  _dbus_connection_remove_timeout (connection,
				   pending->timeout);
  pending->timeout_added = FALSE;

  _dbus_verbose ("%s middle\n", _DBUS_FUNCTION_NAME);
  status = _dbus_connection_get_dispatch_status_unlocked (connection);

  /* Unlocks, and calls out to user code */
  _dbus_connection_update_dispatch_status_and_unlock (connection, status);
  
  return TRUE;
}

/**
 * Queues a message to send, as with dbus_connection_send_message(),
 * but also returns a #DBusPendingCall used to receive a reply to the
 * message. If no reply is received in the given timeout_milliseconds,
 * this function expires the pending reply and generates a synthetic
 * error reply (generated in-process, not by the remote application)
 * indicating that a timeout occurred.
 *
 * A #DBusPendingCall will see a reply message after any filters, but
 * before any object instances or other handlers. A #DBusPendingCall
 * will always see exactly one reply message, unless it's cancelled
 * with dbus_pending_call_cancel().
 * 
 * If a filter filters out the reply before the handler sees it, the
 * reply is immediately timed out and a timeout error reply is
 * generated. If a filter removes the timeout error reply then the
 * #DBusPendingCall will get confused. Filtering the timeout error
 * is thus considered a bug and will print a warning.
 * 
 * If #NULL is passed for the pending_return, the #DBusPendingCall
 * will still be generated internally, and used to track
 * the message reply timeout. This means a timeout error will
 * occur if no reply arrives, unlike with dbus_connection_send().
 *
 * If -1 is passed for the timeout, a sane default timeout is used. -1
 * is typically the best value for the timeout for this reason, unless
 * you want a very short or very long timeout.  There is no way to
 * avoid a timeout entirely, other than passing INT_MAX for the
 * timeout to postpone it indefinitely.
 * 
 * @param connection the connection
 * @param message the message to send
 * @param pending_return return location for a #DBusPendingCall object, or #NULL
 * @param timeout_milliseconds timeout in milliseconds or -1 for default
 * @returns #TRUE if the message is successfully queued, #FALSE if no memory.
 *
 */
dbus_bool_t
dbus_connection_send_with_reply (DBusConnection     *connection,
                                 DBusMessage        *message,
                                 DBusPendingCall   **pending_return,
                                 int                 timeout_milliseconds)
{
  DBusPendingCall *pending;
  DBusMessage *reply;
  DBusList *reply_link;
  dbus_int32_t serial = -1;
  DBusDispatchStatus status;

  _dbus_return_val_if_fail (connection != NULL, FALSE);
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (timeout_milliseconds >= 0 || timeout_milliseconds == -1, FALSE);

  if (pending_return)
    *pending_return = NULL;
  
  pending = _dbus_pending_call_new (connection,
                                    timeout_milliseconds,
                                    reply_handler_timeout);

  if (pending == NULL)
    return FALSE;

  CONNECTION_LOCK (connection);
  
  /* Assign a serial to the message */
  if (dbus_message_get_serial (message) == 0)
    {
      serial = _dbus_connection_get_next_client_serial (connection);
      _dbus_message_set_serial (message, serial);
    }

  pending->reply_serial = serial;

  reply = dbus_message_new_error (message, DBUS_ERROR_NO_REPLY,
                                  "No reply within specified time");
  if (reply == NULL)
    goto error;

  reply_link = _dbus_list_alloc_link (reply);
  if (reply_link == NULL)
    {
      CONNECTION_UNLOCK (connection);
      dbus_message_unref (reply);
      goto error_unlocked;
    }

  pending->timeout_link = reply_link;

  /* Insert the serial in the pending replies hash;
   * hash takes a refcount on DBusPendingCall.
   * Also, add the timeout.
   */
  if (!_dbus_connection_attach_pending_call_unlocked (connection,
						      pending))
    goto error;
 
  if (!_dbus_connection_send_unlocked_no_update (connection, message, NULL))
    {
      _dbus_connection_detach_pending_call_and_unlock (connection,
						       pending);
      goto error_unlocked;
    }

  if (pending_return)
    *pending_return = pending;
  else
    {
      _dbus_connection_detach_pending_call_unlocked (connection, pending);
      dbus_pending_call_unref (pending);
    }

  _dbus_verbose ("%s middle\n", _DBUS_FUNCTION_NAME);
  status = _dbus_connection_get_dispatch_status_unlocked (connection);

  /* this calls out to user code */
  _dbus_connection_update_dispatch_status_and_unlock (connection, status);

  return TRUE;

 error:
  CONNECTION_UNLOCK (connection);
 error_unlocked:
  dbus_pending_call_unref (pending);
  return FALSE;
}

/* This is slightly strange since we can pop a message here without
 * the dispatch lock.
 */
static DBusMessage*
check_for_reply_unlocked (DBusConnection *connection,
                          dbus_uint32_t   client_serial)
{
  DBusList *link;

  HAVE_LOCK_CHECK (connection);
  
  link = _dbus_list_get_first_link (&connection->incoming_messages);

  while (link != NULL)
    {
      DBusMessage *reply = link->data;

      if (dbus_message_get_reply_serial (reply) == client_serial)
	{
	  _dbus_list_remove_link (&connection->incoming_messages, link);
	  connection->n_incoming  -= 1;
	  return reply;
	}
      link = _dbus_list_get_next_link (&connection->incoming_messages, link);
    }

  return NULL;
}

/**
 * When a function that blocks has been called with a timeout, and we
 * run out of memory, the time to wait for memory is based on the
 * timeout. If the caller was willing to block a long time we wait a
 * relatively long time for memory, if they were only willing to block
 * briefly then we retry for memory at a rapid rate.
 *
 * @timeout_milliseconds the timeout requested for blocking
 */
static void
_dbus_memory_pause_based_on_timeout (int timeout_milliseconds)
{
  if (timeout_milliseconds == -1)
    _dbus_sleep_milliseconds (1000);
  else if (timeout_milliseconds < 100)
    ; /* just busy loop */
  else if (timeout_milliseconds <= 1000)
    _dbus_sleep_milliseconds (timeout_milliseconds / 3);
  else
    _dbus_sleep_milliseconds (1000);
}

/**
 * Blocks until a pending call times out or gets a reply.
 *
 * Does not re-enter the main loop or run filter/path-registered
 * callbacks. The reply to the message will not be seen by
 * filter callbacks.
 *
 * Returns immediately if pending call already got a reply.
 * 
 * @todo could use performance improvements (it keeps scanning
 * the whole message queue for example)
 *
 * @param pending the pending call we block for a reply on
 */
void
_dbus_connection_block_pending_call (DBusPendingCall *pending)
{
  long start_tv_sec, start_tv_usec;
  long end_tv_sec, end_tv_usec;
  long tv_sec, tv_usec;
  DBusDispatchStatus status;
  DBusConnection *connection;
  dbus_uint32_t client_serial;
  int timeout_milliseconds;

  _dbus_assert (pending != NULL);

  if (dbus_pending_call_get_completed (pending))
    return;

  if (pending->connection == NULL)
    return; /* call already detached */

  dbus_pending_call_ref (pending); /* necessary because the call could be canceled */
  
  connection = pending->connection;
  client_serial = pending->reply_serial;

  /* note that timeout_milliseconds is limited to a smallish value
   * in _dbus_pending_call_new() so overflows aren't possible
   * below
   */
  timeout_milliseconds = dbus_timeout_get_interval (pending->timeout);

  /* Flush message queue */
  dbus_connection_flush (connection);

  CONNECTION_LOCK (connection);

  _dbus_get_current_time (&start_tv_sec, &start_tv_usec);
  end_tv_sec = start_tv_sec + timeout_milliseconds / 1000;
  end_tv_usec = start_tv_usec + (timeout_milliseconds % 1000) * 1000;
  end_tv_sec += end_tv_usec / _DBUS_USEC_PER_SECOND;
  end_tv_usec = end_tv_usec % _DBUS_USEC_PER_SECOND;

  _dbus_verbose ("dbus_connection_send_with_reply_and_block(): will block %d milliseconds for reply serial %u from %ld sec %ld usec to %ld sec %ld usec\n",
                 timeout_milliseconds,
                 client_serial,
                 start_tv_sec, start_tv_usec,
                 end_tv_sec, end_tv_usec);

  /* Now we wait... */
  /* always block at least once as we know we don't have the reply yet */
  _dbus_connection_do_iteration_unlocked (connection,
                                          DBUS_ITERATION_DO_READING |
                                          DBUS_ITERATION_BLOCK,
                                          timeout_milliseconds);

 recheck_status:

  _dbus_verbose ("%s top of recheck\n", _DBUS_FUNCTION_NAME);
  
  HAVE_LOCK_CHECK (connection);
  
  /* queue messages and get status */

  status = _dbus_connection_get_dispatch_status_unlocked (connection);

  /* the get_completed() is in case a dispatch() while we were blocking
   * got the reply instead of us.
   */
  if (dbus_pending_call_get_completed (pending))
    {
      _dbus_verbose ("Pending call completed by dispatch in %s\n", _DBUS_FUNCTION_NAME);
      _dbus_connection_update_dispatch_status_and_unlock (connection, status);
      dbus_pending_call_unref (pending);
      return;
    }
  
  if (status == DBUS_DISPATCH_DATA_REMAINS)
    {
      DBusMessage *reply;
      
      reply = check_for_reply_unlocked (connection, client_serial);
      if (reply != NULL)
        {
          _dbus_verbose ("%s checked for reply\n", _DBUS_FUNCTION_NAME);

          _dbus_verbose ("dbus_connection_send_with_reply_and_block(): got reply\n");
          
          _dbus_pending_call_complete_and_unlock (pending, reply);
          dbus_message_unref (reply);

          CONNECTION_LOCK (connection);
          status = _dbus_connection_get_dispatch_status_unlocked (connection);
          _dbus_connection_update_dispatch_status_and_unlock (connection, status);
          dbus_pending_call_unref (pending);
          
          return;
        }
    }
  
  _dbus_get_current_time (&tv_sec, &tv_usec);
  
  if (!_dbus_connection_get_is_connected_unlocked (connection))
    {
      /* FIXME send a "DBUS_ERROR_DISCONNECTED" instead, just to help
       * programmers understand what went wrong since the timeout is
       * confusing
       */
      
      _dbus_pending_call_complete_and_unlock (pending, NULL);
      dbus_pending_call_unref (pending);
      return;
    }
  else if (tv_sec < start_tv_sec)
    _dbus_verbose ("dbus_connection_send_with_reply_and_block(): clock set backward\n");
  else if (connection->disconnect_message_link == NULL)
    _dbus_verbose ("dbus_connection_send_with_reply_and_block(): disconnected\n");
  else if (tv_sec < end_tv_sec ||
           (tv_sec == end_tv_sec && tv_usec < end_tv_usec))
    {
      timeout_milliseconds = (end_tv_sec - tv_sec) * 1000 +
        (end_tv_usec - tv_usec) / 1000;
      _dbus_verbose ("dbus_connection_send_with_reply_and_block(): %d milliseconds remain\n", timeout_milliseconds);
      _dbus_assert (timeout_milliseconds >= 0);
      
      if (status == DBUS_DISPATCH_NEED_MEMORY)
        {
          /* Try sleeping a bit, as we aren't sure we need to block for reading,
           * we may already have a reply in the buffer and just can't process
           * it.
           */
          _dbus_verbose ("dbus_connection_send_with_reply_and_block() waiting for more memory\n");

          _dbus_memory_pause_based_on_timeout (timeout_milliseconds);
        }
      else
        {          
          /* block again, we don't have the reply buffered yet. */
          _dbus_connection_do_iteration_unlocked (connection,
                                                  DBUS_ITERATION_DO_READING |
                                                  DBUS_ITERATION_BLOCK,
                                                  timeout_milliseconds);
        }

      goto recheck_status;
    }

  _dbus_verbose ("dbus_connection_send_with_reply_and_block(): Waited %ld milliseconds and got no reply\n",
                 (tv_sec - start_tv_sec) * 1000 + (tv_usec - start_tv_usec) / 1000);

  _dbus_assert (!dbus_pending_call_get_completed (pending));
  
  /* unlock and call user code */
  _dbus_pending_call_complete_and_unlock (pending, NULL);

  /* update user code on dispatch status */
  CONNECTION_LOCK (connection);
  status = _dbus_connection_get_dispatch_status_unlocked (connection);
  _dbus_connection_update_dispatch_status_and_unlock (connection, status);
  dbus_pending_call_unref (pending);
}

/**
 * Sends a message and blocks a certain time period while waiting for
 * a reply.  This function does not reenter the main loop,
 * i.e. messages other than the reply are queued up but not
 * processed. This function is used to do non-reentrant "method
 * calls."
 * 
 * If a normal reply is received, it is returned, and removed from the
 * incoming message queue. If it is not received, #NULL is returned
 * and the error is set to #DBUS_ERROR_NO_REPLY.  If an error reply is
 * received, it is converted to a #DBusError and returned as an error,
 * then the reply message is deleted. If something else goes wrong,
 * result is set to whatever is appropriate, such as
 * #DBUS_ERROR_NO_MEMORY or #DBUS_ERROR_DISCONNECTED.
 *
 * @param connection the connection
 * @param message the message to send
 * @param timeout_milliseconds timeout in milliseconds or -1 for default
 * @param error return location for error message
 * @returns the message that is the reply or #NULL with an error code if the
 * function fails.
 */
DBusMessage*
dbus_connection_send_with_reply_and_block (DBusConnection     *connection,
                                           DBusMessage        *message,
                                           int                 timeout_milliseconds,
                                           DBusError          *error)
{
  DBusMessage *reply;
  DBusPendingCall *pending;
  
  _dbus_return_val_if_fail (connection != NULL, NULL);
  _dbus_return_val_if_fail (message != NULL, NULL);
  _dbus_return_val_if_fail (timeout_milliseconds >= 0 || timeout_milliseconds == -1, FALSE);  
  _dbus_return_val_if_error_is_set (error, NULL);
  
  if (!dbus_connection_send_with_reply (connection, message,
                                        &pending, timeout_milliseconds))
    {
      _DBUS_SET_OOM (error);
      return NULL;
    }

  _dbus_assert (pending != NULL);
  
  dbus_pending_call_block (pending);

  reply = dbus_pending_call_steal_reply (pending);
  dbus_pending_call_unref (pending);

  /* call_complete_and_unlock() called from pending_call_block() should
   * always fill this in.
   */
  _dbus_assert (reply != NULL);
  
   if (dbus_set_error_from_message (error, reply))
    {
      dbus_message_unref (reply);
      return NULL;
    }
  else
    return reply;
}

/**
 * Blocks until the outgoing message queue is empty.
 *
 * @param connection the connection.
 */
void
dbus_connection_flush (DBusConnection *connection)
{
  /* We have to specify DBUS_ITERATION_DO_READING here because
   * otherwise we could have two apps deadlock if they are both doing
   * a flush(), and the kernel buffers fill up. This could change the
   * dispatch status.
   */
  DBusDispatchStatus status;

  _dbus_return_if_fail (connection != NULL);
  
  CONNECTION_LOCK (connection);
  while (connection->n_outgoing > 0 &&
         _dbus_connection_get_is_connected_unlocked (connection))
    {
      _dbus_verbose ("doing iteration in %s\n", _DBUS_FUNCTION_NAME);
      HAVE_LOCK_CHECK (connection);
      _dbus_connection_do_iteration_unlocked (connection,
                                              DBUS_ITERATION_DO_READING |
                                              DBUS_ITERATION_DO_WRITING |
                                              DBUS_ITERATION_BLOCK,
                                              -1);
    }

  HAVE_LOCK_CHECK (connection);
  _dbus_verbose ("%s middle\n", _DBUS_FUNCTION_NAME);
  status = _dbus_connection_get_dispatch_status_unlocked (connection);

  HAVE_LOCK_CHECK (connection);
  /* Unlocks and calls out to user code */
  _dbus_connection_update_dispatch_status_and_unlock (connection, status);

  _dbus_verbose ("%s end\n", _DBUS_FUNCTION_NAME);
}

/**
 * This function is intended for use with applications that don't want
 * to write a main loop and deal with #DBusWatch and #DBusTimeout. An
 * example usage would be:
 * 
 * @code
 *   while (dbus_connection_read_write_dispatch (connection, -1))
 *     ; // empty loop body
 * @endcode
 * 
 * In this usage you would normally have set up a filter function to look
 * at each message as it is dispatched. The loop terminates when the last
 * message from the connection (the disconnected signal) is processed.
 * 
 * If there are messages to dispatch, this function will
 * dbus_connection_dispatch() once, and return. If there are no
 * messages to dispatch, this function will block until it can read or
 * write, then read or write, then return.
 *
 * The way to think of this function is that it either makes some sort
 * of progress, or it blocks.
 *
 * The return value indicates whether the disconnect message has been
 * processed, NOT whether the connection is connected. This is
 * important because even after disconnecting, you want to process any
 * messages you received prior to the disconnect.
 *
 * @param connection the connection
 * @param timeout_milliseconds max time to block or -1 for infinite
 * @returns #TRUE if the disconnect message has not been processed
 */
dbus_bool_t
dbus_connection_read_write_dispatch (DBusConnection *connection,
                                     int             timeout_milliseconds)
{
  DBusDispatchStatus dstatus;
  dbus_bool_t dispatched_disconnected;
  
  _dbus_return_val_if_fail (connection != NULL, FALSE);
  _dbus_return_val_if_fail (timeout_milliseconds >= 0 || timeout_milliseconds == -1, FALSE);
  dstatus = dbus_connection_get_dispatch_status (connection);

  if (dstatus == DBUS_DISPATCH_DATA_REMAINS)
    {
      _dbus_verbose ("doing dispatch in %s\n", _DBUS_FUNCTION_NAME);
      dbus_connection_dispatch (connection);
      CONNECTION_LOCK (connection);
    }
  else if (dstatus == DBUS_DISPATCH_NEED_MEMORY)
    {
      _dbus_verbose ("pausing for memory in %s\n", _DBUS_FUNCTION_NAME);
      _dbus_memory_pause_based_on_timeout (timeout_milliseconds);
      CONNECTION_LOCK (connection);
    }
  else
    {
      CONNECTION_LOCK (connection);
      if (_dbus_connection_get_is_connected_unlocked (connection))
        {
          _dbus_verbose ("doing iteration in %s\n", _DBUS_FUNCTION_NAME);
          _dbus_connection_do_iteration_unlocked (connection,
                                                  DBUS_ITERATION_DO_READING |
                                                  DBUS_ITERATION_DO_WRITING |
                                                  DBUS_ITERATION_BLOCK,
                                                  timeout_milliseconds);
        }
    }
  
  HAVE_LOCK_CHECK (connection);
  dispatched_disconnected = connection->n_incoming == 0 &&
    connection->disconnect_message_link == NULL;
  CONNECTION_UNLOCK (connection);
  return !dispatched_disconnected; /* TRUE if we have not processed disconnected */
}

/**
 * Returns the first-received message from the incoming message queue,
 * leaving it in the queue. If the queue is empty, returns #NULL.
 * 
 * The caller does not own a reference to the returned message, and
 * must either return it using dbus_connection_return_message() or
 * keep it after calling dbus_connection_steal_borrowed_message(). No
 * one can get at the message while its borrowed, so return it as
 * quickly as possible and don't keep a reference to it after
 * returning it. If you need to keep the message, make a copy of it.
 *
 * dbus_connection_dispatch() will block if called while a borrowed
 * message is outstanding; only one piece of code can be playing with
 * the incoming queue at a time. This function will block if called
 * during a dbus_connection_dispatch().
 *
 * @param connection the connection.
 * @returns next message in the incoming queue.
 */
DBusMessage*
dbus_connection_borrow_message (DBusConnection *connection)
{
  DBusDispatchStatus status;
  DBusMessage *message;

  _dbus_return_val_if_fail (connection != NULL, NULL);

  _dbus_verbose ("%s start\n", _DBUS_FUNCTION_NAME);
  
  /* this is called for the side effect that it queues
   * up any messages from the transport
   */
  status = dbus_connection_get_dispatch_status (connection);
  if (status != DBUS_DISPATCH_DATA_REMAINS)
    return NULL;
  
  CONNECTION_LOCK (connection);

  _dbus_connection_acquire_dispatch (connection);

  /* While a message is outstanding, the dispatch lock is held */
  _dbus_assert (connection->message_borrowed == NULL);

  connection->message_borrowed = _dbus_list_get_first (&connection->incoming_messages);
  
  message = connection->message_borrowed;

  /* Note that we KEEP the dispatch lock until the message is returned */
  if (message == NULL)
    _dbus_connection_release_dispatch (connection);

  CONNECTION_UNLOCK (connection);
  
  return message;
}

/**
 * Used to return a message after peeking at it using
 * dbus_connection_borrow_message(). Only called if
 * message from dbus_connection_borrow_message() was non-#NULL.
 *
 * @param connection the connection
 * @param message the message from dbus_connection_borrow_message()
 */
void
dbus_connection_return_message (DBusConnection *connection,
				DBusMessage    *message)
{
  _dbus_return_if_fail (connection != NULL);
  _dbus_return_if_fail (message != NULL);
  _dbus_return_if_fail (message == connection->message_borrowed);
  _dbus_return_if_fail (connection->dispatch_acquired);
  
  CONNECTION_LOCK (connection);
  
  _dbus_assert (message == connection->message_borrowed);
  
  connection->message_borrowed = NULL;

  _dbus_connection_release_dispatch (connection);
  
  CONNECTION_UNLOCK (connection);
}

/**
 * Used to keep a message after peeking at it using
 * dbus_connection_borrow_message(). Before using this function, see
 * the caveats/warnings in the documentation for
 * dbus_connection_pop_message().
 *
 * @param connection the connection
 * @param message the message from dbus_connection_borrow_message()
 */
void
dbus_connection_steal_borrowed_message (DBusConnection *connection,
					DBusMessage    *message)
{
  DBusMessage *pop_message;

  _dbus_return_if_fail (connection != NULL);
  _dbus_return_if_fail (message != NULL);
  _dbus_return_if_fail (message == connection->message_borrowed);
  _dbus_return_if_fail (connection->dispatch_acquired);
  
  CONNECTION_LOCK (connection);
 
  _dbus_assert (message == connection->message_borrowed);

  pop_message = _dbus_list_pop_first (&connection->incoming_messages);
  _dbus_assert (message == pop_message);
  
  connection->n_incoming -= 1;
 
  _dbus_verbose ("Incoming message %p stolen from queue, %d incoming\n",
		 message, connection->n_incoming);
 
  connection->message_borrowed = NULL;

  _dbus_connection_release_dispatch (connection);
  
  CONNECTION_UNLOCK (connection);
}

/* See dbus_connection_pop_message, but requires the caller to own
 * the lock before calling. May drop the lock while running.
 */
static DBusList*
_dbus_connection_pop_message_link_unlocked (DBusConnection *connection)
{
  HAVE_LOCK_CHECK (connection);
  
  _dbus_assert (connection->message_borrowed == NULL);
  
  if (connection->n_incoming > 0)
    {
      DBusList *link;

      link = _dbus_list_pop_first_link (&connection->incoming_messages);
      connection->n_incoming -= 1;

      _dbus_verbose ("Message %p (%d %s %s %s '%s') removed from incoming queue %p, %d incoming\n",
                     link->data,
                     dbus_message_get_type (link->data),
		     dbus_message_get_path (link->data), 
                     dbus_message_get_interface (link->data) ?
                     dbus_message_get_interface (link->data) :
                     "no interface",
                     dbus_message_get_member (link->data) ?
                     dbus_message_get_member (link->data) :
                     "no member",
                     dbus_message_get_signature (link->data),
                     connection, connection->n_incoming);

      return link;
    }
  else
    return NULL;
}

/* See dbus_connection_pop_message, but requires the caller to own
 * the lock before calling. May drop the lock while running.
 */
static DBusMessage*
_dbus_connection_pop_message_unlocked (DBusConnection *connection)
{
  DBusList *link;

  HAVE_LOCK_CHECK (connection);
  
  link = _dbus_connection_pop_message_link_unlocked (connection);

  if (link != NULL)
    {
      DBusMessage *message;
      
      message = link->data;
      
      _dbus_list_free_link (link);
      
      return message;
    }
  else
    return NULL;
}

static void
_dbus_connection_putback_message_link_unlocked (DBusConnection *connection,
                                                DBusList       *message_link)
{
  HAVE_LOCK_CHECK (connection);
  
  _dbus_assert (message_link != NULL);
  /* You can't borrow a message while a link is outstanding */
  _dbus_assert (connection->message_borrowed == NULL);
  /* We had to have the dispatch lock across the pop/putback */
  _dbus_assert (connection->dispatch_acquired);

  _dbus_list_prepend_link (&connection->incoming_messages,
                           message_link);
  connection->n_incoming += 1;

  _dbus_verbose ("Message %p (%d %s %s '%s') put back into queue %p, %d incoming\n",
                 message_link->data,
                 dbus_message_get_type (message_link->data),
                 dbus_message_get_interface (message_link->data) ?
                 dbus_message_get_interface (message_link->data) :
                 "no interface",
                 dbus_message_get_member (message_link->data) ?
                 dbus_message_get_member (message_link->data) :
                 "no member",
                 dbus_message_get_signature (message_link->data),
                 connection, connection->n_incoming);
}

/**
 * Returns the first-received message from the incoming message queue,
 * removing it from the queue. The caller owns a reference to the
 * returned message. If the queue is empty, returns #NULL.
 *
 * This function bypasses any message handlers that are registered,
 * and so using it is usually wrong. Instead, let the main loop invoke
 * dbus_connection_dispatch(). Popping messages manually is only
 * useful in very simple programs that don't share a #DBusConnection
 * with any libraries or other modules.
 *
 * There is a lock that covers all ways of accessing the incoming message
 * queue, so dbus_connection_dispatch(), dbus_connection_pop_message(),
 * dbus_connection_borrow_message(), etc. will all block while one of the others
 * in the group is running.
 * 
 * @param connection the connection.
 * @returns next message in the incoming queue.
 */
DBusMessage*
dbus_connection_pop_message (DBusConnection *connection)
{
  DBusMessage *message;
  DBusDispatchStatus status;

  _dbus_verbose ("%s start\n", _DBUS_FUNCTION_NAME);
  
  /* this is called for the side effect that it queues
   * up any messages from the transport
   */
  status = dbus_connection_get_dispatch_status (connection);
  if (status != DBUS_DISPATCH_DATA_REMAINS)
    return NULL;
  
  CONNECTION_LOCK (connection);
  _dbus_connection_acquire_dispatch (connection);
  HAVE_LOCK_CHECK (connection);
  
  message = _dbus_connection_pop_message_unlocked (connection);

  _dbus_verbose ("Returning popped message %p\n", message);    

  _dbus_connection_release_dispatch (connection);
  CONNECTION_UNLOCK (connection);
  
  return message;
}

/**
 * Acquire the dispatcher. This is a separate lock so the main
 * connection lock can be dropped to call out to application dispatch
 * handlers.
 *
 * @param connection the connection.
 */
static void
_dbus_connection_acquire_dispatch (DBusConnection *connection)
{
  HAVE_LOCK_CHECK (connection);

  _dbus_connection_ref_unlocked (connection);
  CONNECTION_UNLOCK (connection);
  
  _dbus_verbose ("%s locking dispatch_mutex\n", _DBUS_FUNCTION_NAME);
  _dbus_mutex_lock (connection->dispatch_mutex);

  while (connection->dispatch_acquired)
    {
      _dbus_verbose ("%s waiting for dispatch to be acquirable\n", _DBUS_FUNCTION_NAME);
      _dbus_condvar_wait (connection->dispatch_cond, connection->dispatch_mutex);
    }
  
  _dbus_assert (!connection->dispatch_acquired);

  connection->dispatch_acquired = TRUE;

  _dbus_verbose ("%s unlocking dispatch_mutex\n", _DBUS_FUNCTION_NAME);
  _dbus_mutex_unlock (connection->dispatch_mutex);
  
  CONNECTION_LOCK (connection);
  _dbus_connection_unref_unlocked (connection);
}

/**
 * Release the dispatcher when you're done with it. Only call
 * after you've acquired the dispatcher. Wakes up at most one
 * thread currently waiting to acquire the dispatcher.
 *
 * @param connection the connection.
 */
static void
_dbus_connection_release_dispatch (DBusConnection *connection)
{
  HAVE_LOCK_CHECK (connection);
  
  _dbus_verbose ("%s locking dispatch_mutex\n", _DBUS_FUNCTION_NAME);
  _dbus_mutex_lock (connection->dispatch_mutex);
  
  _dbus_assert (connection->dispatch_acquired);

  connection->dispatch_acquired = FALSE;
  _dbus_condvar_wake_one (connection->dispatch_cond);

  _dbus_verbose ("%s unlocking dispatch_mutex\n", _DBUS_FUNCTION_NAME);
  _dbus_mutex_unlock (connection->dispatch_mutex);
}

static void
_dbus_connection_failed_pop (DBusConnection *connection,
			     DBusList       *message_link)
{
  _dbus_list_prepend_link (&connection->incoming_messages,
			   message_link);
  connection->n_incoming += 1;
}

static DBusDispatchStatus
_dbus_connection_get_dispatch_status_unlocked (DBusConnection *connection)
{
  HAVE_LOCK_CHECK (connection);
  
  if (connection->n_incoming > 0)
    return DBUS_DISPATCH_DATA_REMAINS;
  else if (!_dbus_transport_queue_messages (connection->transport))
    return DBUS_DISPATCH_NEED_MEMORY;
  else
    {
      DBusDispatchStatus status;
      dbus_bool_t is_connected;
      
      status = _dbus_transport_get_dispatch_status (connection->transport);
      is_connected = _dbus_transport_get_is_connected (connection->transport);

      _dbus_verbose ("dispatch status = %s is_connected = %d\n",
                     DISPATCH_STATUS_NAME (status), is_connected);
      
      if (!is_connected)
        {
          if (status == DBUS_DISPATCH_COMPLETE &&
              connection->disconnect_message_link)
            {
              _dbus_verbose ("Sending disconnect message from %s\n",
                             _DBUS_FUNCTION_NAME);

              connection_forget_shared_unlocked (connection);
              
              /* We haven't sent the disconnect message already,
               * and all real messages have been queued up.
               */
              _dbus_connection_queue_synthesized_message_link (connection,
                                                               connection->disconnect_message_link);
              connection->disconnect_message_link = NULL;
            }

          /* Dump the outgoing queue, we aren't going to be able to
           * send it now, and we'd like accessors like
           * dbus_connection_get_outgoing_size() to be accurate.
           */
          if (connection->n_outgoing > 0)
            {
              DBusList *link;
              
              _dbus_verbose ("Dropping %d outgoing messages since we're disconnected\n",
                             connection->n_outgoing);
              
              while ((link = _dbus_list_get_last_link (&connection->outgoing_messages)))
                {
                  _dbus_connection_message_sent (connection, link->data);
                }
            }
        }
      
      if (status != DBUS_DISPATCH_COMPLETE)
        return status;
      else if (connection->n_incoming > 0)
        return DBUS_DISPATCH_DATA_REMAINS;
      else
        return DBUS_DISPATCH_COMPLETE;
    }
}

static void
_dbus_connection_update_dispatch_status_and_unlock (DBusConnection    *connection,
                                                    DBusDispatchStatus new_status)
{
  dbus_bool_t changed;
  DBusDispatchStatusFunction function;
  void *data;

  HAVE_LOCK_CHECK (connection);

  _dbus_connection_ref_unlocked (connection);

  changed = new_status != connection->last_dispatch_status;

  connection->last_dispatch_status = new_status;

  function = connection->dispatch_status_function;
  data = connection->dispatch_status_data;

  /* We drop the lock */
  CONNECTION_UNLOCK (connection);
  
  if (changed && function)
    {
      _dbus_verbose ("Notifying of change to dispatch status of %p now %d (%s)\n",
                     connection, new_status,
                     DISPATCH_STATUS_NAME (new_status));
      (* function) (connection, new_status, data);      
    }
  
  dbus_connection_unref (connection);
}

/**
 * Gets the current state (what we would currently return
 * from dbus_connection_dispatch()) but doesn't actually
 * dispatch any messages.
 * 
 * @param connection the connection.
 * @returns current dispatch status
 */
DBusDispatchStatus
dbus_connection_get_dispatch_status (DBusConnection *connection)
{
  DBusDispatchStatus status;

  _dbus_return_val_if_fail (connection != NULL, DBUS_DISPATCH_COMPLETE);

  _dbus_verbose ("%s start\n", _DBUS_FUNCTION_NAME);
  
  CONNECTION_LOCK (connection);

  status = _dbus_connection_get_dispatch_status_unlocked (connection);
  
  CONNECTION_UNLOCK (connection);

  return status;
}

/**
* Filter funtion for handling the Peer standard interface
**/
static DBusHandlerResult
_dbus_connection_peer_filter (DBusConnection *connection,
                              DBusMessage    *message)
{
  if (dbus_message_is_method_call (message,
                                   DBUS_INTERFACE_PEER,
                                   "Ping"))
    {
      DBusMessage *ret;
      dbus_bool_t sent;
      
      ret = dbus_message_new_method_return (message);
      if (ret == NULL)
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
      
      sent = dbus_connection_send (connection, ret, NULL);
      dbus_message_unref (ret);

      if (!sent)
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
      
      return DBUS_HANDLER_RESULT_HANDLED;
    }
                                   
  
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
* Processes all builtin filter functions
*
* If the spec specifies a standard interface
* they should be processed from this method
**/
static DBusHandlerResult
_dbus_connection_run_builtin_filters (DBusConnection *connection,
                                      DBusMessage    *message)
{
  /* We just run one filter for now but have the option to run more
     if the spec calls for it in the future */

  return _dbus_connection_peer_filter (connection, message);
}

/**
 * Processes data buffered while handling watches, queueing zero or
 * more incoming messages. Then pops the first-received message from
 * the current incoming message queue, runs any handlers for it, and
 * unrefs the message. Returns a status indicating whether messages/data
 * remain, more memory is needed, or all data has been processed.
 * 
 * Even if the dispatch status is #DBUS_DISPATCH_DATA_REMAINS,
 * does not necessarily dispatch a message, as the data may
 * be part of authentication or the like.
 *
 * @todo some FIXME in here about handling DBUS_HANDLER_RESULT_NEED_MEMORY
 *
 * @todo FIXME what if we call out to application code to handle a
 * message, holding the dispatch lock, and the application code runs
 * the main loop and dispatches again? Probably deadlocks at the
 * moment. Maybe we want a dispatch status of DBUS_DISPATCH_IN_PROGRESS,
 * and then the GSource etc. could handle the situation? Right now
 * our GSource is NO_RECURSE
 * 
 * @param connection the connection
 * @returns dispatch status
 */
DBusDispatchStatus
dbus_connection_dispatch (DBusConnection *connection)
{
  DBusMessage *message;
  DBusList *link, *filter_list_copy, *message_link;
  DBusHandlerResult result;
  DBusPendingCall *pending;
  dbus_int32_t reply_serial;
  DBusDispatchStatus status;

  _dbus_return_val_if_fail (connection != NULL, DBUS_DISPATCH_COMPLETE);

  _dbus_verbose ("%s\n", _DBUS_FUNCTION_NAME);
  
  CONNECTION_LOCK (connection);
  status = _dbus_connection_get_dispatch_status_unlocked (connection);
  if (status != DBUS_DISPATCH_DATA_REMAINS)
    {
      /* unlocks and calls out to user code */
      _dbus_connection_update_dispatch_status_and_unlock (connection, status);
      return status;
    }
  
  /* We need to ref the connection since the callback could potentially
   * drop the last ref to it
   */
  _dbus_connection_ref_unlocked (connection);

  _dbus_connection_acquire_dispatch (connection);
  HAVE_LOCK_CHECK (connection);

  message_link = _dbus_connection_pop_message_link_unlocked (connection);
  if (message_link == NULL)
    {
      /* another thread dispatched our stuff */

      _dbus_verbose ("another thread dispatched message (during acquire_dispatch above)\n");
      
      _dbus_connection_release_dispatch (connection);

      status = _dbus_connection_get_dispatch_status_unlocked (connection);

      _dbus_connection_update_dispatch_status_and_unlock (connection, status);
      
      dbus_connection_unref (connection);
      
      return status;
    }

  message = message_link->data;

  _dbus_verbose (" dispatching message %p (%d %s %s '%s')\n",
                 message,
                 dbus_message_get_type (message),
                 dbus_message_get_interface (message) ?
                 dbus_message_get_interface (message) :
                 "no interface",
                 dbus_message_get_member (message) ?
                 dbus_message_get_member (message) :
                 "no member",
                 dbus_message_get_signature (message));

  result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  
  /* Pending call handling must be first, because if you do
   * dbus_connection_send_with_reply_and_block() or
   * dbus_pending_call_block() then no handlers/filters will be run on
   * the reply. We want consistent semantics in the case where we
   * dbus_connection_dispatch() the reply.
   */
  
  reply_serial = dbus_message_get_reply_serial (message);
  pending = _dbus_hash_table_lookup_int (connection->pending_replies,
                                         reply_serial);
  if (pending)
    {
      _dbus_verbose ("Dispatching a pending reply\n");
      _dbus_pending_call_complete_and_unlock (pending, message);
      pending = NULL; /* it's probably unref'd */
      
      CONNECTION_LOCK (connection);
      _dbus_verbose ("pending call completed in dispatch\n");
      result = DBUS_HANDLER_RESULT_HANDLED;
      goto out;
    }
 
  result = _dbus_connection_run_builtin_filters (connection, message);
  if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED)
    goto out;
 
  if (!_dbus_list_copy (&connection->filter_list, &filter_list_copy))
    {
      _dbus_connection_release_dispatch (connection);
      HAVE_LOCK_CHECK (connection);
      
      _dbus_connection_failed_pop (connection, message_link);

      /* unlocks and calls user code */
      _dbus_connection_update_dispatch_status_and_unlock (connection,
                                                          DBUS_DISPATCH_NEED_MEMORY);

      if (pending)
        dbus_pending_call_unref (pending);
      dbus_connection_unref (connection);
      
      return DBUS_DISPATCH_NEED_MEMORY;
    }
  
  _dbus_list_foreach (&filter_list_copy,
		      (DBusForeachFunction)_dbus_message_filter_ref,
		      NULL);

  /* We're still protected from dispatch() reentrancy here
   * since we acquired the dispatcher
   */
  CONNECTION_UNLOCK (connection);
  
  link = _dbus_list_get_first_link (&filter_list_copy);
  while (link != NULL)
    {
      DBusMessageFilter *filter = link->data;
      DBusList *next = _dbus_list_get_next_link (&filter_list_copy, link);

      _dbus_verbose ("  running filter on message %p\n", message);
      result = (* filter->function) (connection, message, filter->user_data);

      if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED)
	break;

      link = next;
    }

  _dbus_list_foreach (&filter_list_copy,
		      (DBusForeachFunction)_dbus_message_filter_unref,
		      NULL);
  _dbus_list_clear (&filter_list_copy);
  
  CONNECTION_LOCK (connection);

  if (result == DBUS_HANDLER_RESULT_NEED_MEMORY)
    {
      _dbus_verbose ("No memory in %s\n", _DBUS_FUNCTION_NAME);
      goto out;
    }
  else if (result == DBUS_HANDLER_RESULT_HANDLED)
    {
      _dbus_verbose ("filter handled message in dispatch\n");
      goto out;
    }

  /* We're still protected from dispatch() reentrancy here
   * since we acquired the dispatcher
   */
  _dbus_verbose ("  running object path dispatch on message %p (%d %s %s '%s')\n",
                 message,
                 dbus_message_get_type (message),
                 dbus_message_get_interface (message) ?
                 dbus_message_get_interface (message) :
                 "no interface",
                 dbus_message_get_member (message) ?
                 dbus_message_get_member (message) :
                 "no member",
                 dbus_message_get_signature (message));

  HAVE_LOCK_CHECK (connection);
  result = _dbus_object_tree_dispatch_and_unlock (connection->objects,
                                                  message);
  
  CONNECTION_LOCK (connection);

  if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED)
    {
      _dbus_verbose ("object tree handled message in dispatch\n");
      goto out;
    }

  if (dbus_message_get_type (message) == DBUS_MESSAGE_TYPE_METHOD_CALL)
    {
      DBusMessage *reply;
      DBusString str;
      DBusPreallocatedSend *preallocated;

      _dbus_verbose ("  sending error %s\n",
                     DBUS_ERROR_UNKNOWN_METHOD);
      
      if (!_dbus_string_init (&str))
        {
          result = DBUS_HANDLER_RESULT_NEED_MEMORY;
          _dbus_verbose ("no memory for error string in dispatch\n");
          goto out;
        }
              
      if (!_dbus_string_append_printf (&str,
                                       "Method \"%s\" with signature \"%s\" on interface \"%s\" doesn't exist\n",
                                       dbus_message_get_member (message),
                                       dbus_message_get_signature (message),
                                       dbus_message_get_interface (message)))
        {
          _dbus_string_free (&str);
          result = DBUS_HANDLER_RESULT_NEED_MEMORY;
          _dbus_verbose ("no memory for error string in dispatch\n");
          goto out;
        }
      
      reply = dbus_message_new_error (message,
                                      DBUS_ERROR_UNKNOWN_METHOD,
                                      _dbus_string_get_const_data (&str));
      _dbus_string_free (&str);

      if (reply == NULL)
        {
          result = DBUS_HANDLER_RESULT_NEED_MEMORY;
          _dbus_verbose ("no memory for error reply in dispatch\n");
          goto out;
        }
      
      preallocated = _dbus_connection_preallocate_send_unlocked (connection);

      if (preallocated == NULL)
        {
          dbus_message_unref (reply);
          result = DBUS_HANDLER_RESULT_NEED_MEMORY;
          _dbus_verbose ("no memory for error send in dispatch\n");
          goto out;
        }

      _dbus_connection_send_preallocated_unlocked_no_update (connection, preallocated,
                                                             reply, NULL);

      dbus_message_unref (reply);
      
      result = DBUS_HANDLER_RESULT_HANDLED;
    }
  
  _dbus_verbose ("  done dispatching %p (%d %s %s '%s') on connection %p\n", message,
                 dbus_message_get_type (message),
                 dbus_message_get_interface (message) ?
                 dbus_message_get_interface (message) :
                 "no interface",
                 dbus_message_get_member (message) ?
                 dbus_message_get_member (message) :
                 "no member",
                 dbus_message_get_signature (message),
                 connection);
  
 out:
  if (result == DBUS_HANDLER_RESULT_NEED_MEMORY)
    {
      _dbus_verbose ("out of memory in %s\n", _DBUS_FUNCTION_NAME);
      
      /* Put message back, and we'll start over.
       * Yes this means handlers must be idempotent if they
       * don't return HANDLED; c'est la vie.
       */
      _dbus_connection_putback_message_link_unlocked (connection,
                                                      message_link);
    }
  else
    {
      _dbus_verbose (" ... done dispatching in %s\n", _DBUS_FUNCTION_NAME);
      
      if (connection->exit_on_disconnect &&
          dbus_message_is_signal (message,
                                  DBUS_INTERFACE_LOCAL,
                                  "Disconnected"))
        {
          _dbus_verbose ("Exiting on Disconnected signal\n");
          CONNECTION_UNLOCK (connection);
          _dbus_exit (1);
          _dbus_assert_not_reached ("Call to exit() returned");
        }
      
      _dbus_list_free_link (message_link);
      dbus_message_unref (message); /* don't want the message to count in max message limits
                                     * in computing dispatch status below
                                     */
    }
  
  _dbus_connection_release_dispatch (connection);
  HAVE_LOCK_CHECK (connection);

  _dbus_verbose ("%s before final status update\n", _DBUS_FUNCTION_NAME);
  status = _dbus_connection_get_dispatch_status_unlocked (connection);

  /* unlocks and calls user code */
  _dbus_connection_update_dispatch_status_and_unlock (connection, status);
  
  dbus_connection_unref (connection);
  
  return status;
}

/**
 * Sets the watch functions for the connection. These functions are
 * responsible for making the application's main loop aware of file
 * descriptors that need to be monitored for events, using select() or
 * poll(). When using Qt, typically the DBusAddWatchFunction would
 * create a QSocketNotifier. When using GLib, the DBusAddWatchFunction
 * could call g_io_add_watch(), or could be used as part of a more
 * elaborate GSource. Note that when a watch is added, it may
 * not be enabled.
 *
 * The DBusWatchToggledFunction notifies the application that the
 * watch has been enabled or disabled. Call dbus_watch_get_enabled()
 * to check this. A disabled watch should have no effect, and enabled
 * watch should be added to the main loop. This feature is used
 * instead of simply adding/removing the watch because
 * enabling/disabling can be done without memory allocation.  The
 * toggled function may be NULL if a main loop re-queries
 * dbus_watch_get_enabled() every time anyway.
 * 
 * The DBusWatch can be queried for the file descriptor to watch using
 * dbus_watch_get_fd(), and for the events to watch for using
 * dbus_watch_get_flags(). The flags returned by
 * dbus_watch_get_flags() will only contain DBUS_WATCH_READABLE and
 * DBUS_WATCH_WRITABLE, never DBUS_WATCH_HANGUP or DBUS_WATCH_ERROR;
 * all watches implicitly include a watch for hangups, errors, and
 * other exceptional conditions.
 *
 * Once a file descriptor becomes readable or writable, or an exception
 * occurs, dbus_watch_handle() should be called to
 * notify the connection of the file descriptor's condition.
 *
 * dbus_watch_handle() cannot be called during the
 * DBusAddWatchFunction, as the connection will not be ready to handle
 * that watch yet.
 * 
 * It is not allowed to reference a DBusWatch after it has been passed
 * to remove_function.
 *
 * If #FALSE is returned due to lack of memory, the failure may be due
 * to a #FALSE return from the new add_function. If so, the
 * add_function may have been called successfully one or more times,
 * but the remove_function will also have been called to remove any
 * successful adds. i.e. if #FALSE is returned the net result
 * should be that dbus_connection_set_watch_functions() has no effect,
 * but the add_function and remove_function may have been called.
 *
 * @todo We need to drop the lock when we call the
 * add/remove/toggled functions which can be a side effect
 * of setting the watch functions.
 * 
 * @param connection the connection.
 * @param add_function function to begin monitoring a new descriptor.
 * @param remove_function function to stop monitoring a descriptor.
 * @param toggled_function function to notify of enable/disable
 * @param data data to pass to add_function and remove_function.
 * @param free_data_function function to be called to free the data.
 * @returns #FALSE on failure (no memory)
 */
dbus_bool_t
dbus_connection_set_watch_functions (DBusConnection              *connection,
                                     DBusAddWatchFunction         add_function,
                                     DBusRemoveWatchFunction      remove_function,
                                     DBusWatchToggledFunction     toggled_function,
                                     void                        *data,
                                     DBusFreeFunction             free_data_function)
{
  dbus_bool_t retval;
  DBusWatchList *watches;

  _dbus_return_val_if_fail (connection != NULL, FALSE);
  
  CONNECTION_LOCK (connection);

#ifndef DBUS_DISABLE_CHECKS
  if (connection->watches == NULL)
    {
      _dbus_warn ("Re-entrant call to %s is not allowed\n",
                  _DBUS_FUNCTION_NAME);
      return FALSE;
    }
#endif
  
  /* ref connection for slightly better reentrancy */
  _dbus_connection_ref_unlocked (connection);

  /* This can call back into user code, and we need to drop the
   * connection lock when it does. This is kind of a lame
   * way to do it.
   */
  watches = connection->watches;
  connection->watches = NULL;
  CONNECTION_UNLOCK (connection);

  retval = _dbus_watch_list_set_functions (watches,
                                           add_function, remove_function,
                                           toggled_function,
                                           data, free_data_function);
  CONNECTION_LOCK (connection);
  connection->watches = watches;
  
  CONNECTION_UNLOCK (connection);
  /* drop our paranoid refcount */
  dbus_connection_unref (connection);
  
  return retval;
}

/**
 * Sets the timeout functions for the connection. These functions are
 * responsible for making the application's main loop aware of timeouts.
 * When using Qt, typically the DBusAddTimeoutFunction would create a
 * QTimer. When using GLib, the DBusAddTimeoutFunction would call
 * g_timeout_add.
 * 
 * The DBusTimeoutToggledFunction notifies the application that the
 * timeout has been enabled or disabled. Call
 * dbus_timeout_get_enabled() to check this. A disabled timeout should
 * have no effect, and enabled timeout should be added to the main
 * loop. This feature is used instead of simply adding/removing the
 * timeout because enabling/disabling can be done without memory
 * allocation. With Qt, QTimer::start() and QTimer::stop() can be used
 * to enable and disable. The toggled function may be NULL if a main
 * loop re-queries dbus_timeout_get_enabled() every time anyway.
 * Whenever a timeout is toggled, its interval may change.
 *
 * The DBusTimeout can be queried for the timer interval using
 * dbus_timeout_get_interval(). dbus_timeout_handle() should be called
 * repeatedly, each time the interval elapses, starting after it has
 * elapsed once. The timeout stops firing when it is removed with the
 * given remove_function.  The timer interval may change whenever the
 * timeout is added, removed, or toggled.
 *
 * @param connection the connection.
 * @param add_function function to add a timeout.
 * @param remove_function function to remove a timeout.
 * @param toggled_function function to notify of enable/disable
 * @param data data to pass to add_function and remove_function.
 * @param free_data_function function to be called to free the data.
 * @returns #FALSE on failure (no memory)
 */
dbus_bool_t
dbus_connection_set_timeout_functions   (DBusConnection            *connection,
					 DBusAddTimeoutFunction     add_function,
					 DBusRemoveTimeoutFunction  remove_function,
                                         DBusTimeoutToggledFunction toggled_function,
					 void                      *data,
					 DBusFreeFunction           free_data_function)
{
  dbus_bool_t retval;
  DBusTimeoutList *timeouts;

  _dbus_return_val_if_fail (connection != NULL, FALSE);
  
  CONNECTION_LOCK (connection);

#ifndef DBUS_DISABLE_CHECKS
  if (connection->timeouts == NULL)
    {
      _dbus_warn ("Re-entrant call to %s is not allowed\n",
                  _DBUS_FUNCTION_NAME);
      return FALSE;
    }
#endif
  
  /* ref connection for slightly better reentrancy */
  _dbus_connection_ref_unlocked (connection);

  timeouts = connection->timeouts;
  connection->timeouts = NULL;
  CONNECTION_UNLOCK (connection);
  
  retval = _dbus_timeout_list_set_functions (timeouts,
                                             add_function, remove_function,
                                             toggled_function,
                                             data, free_data_function);
  CONNECTION_LOCK (connection);
  connection->timeouts = timeouts;
  
  CONNECTION_UNLOCK (connection);
  /* drop our paranoid refcount */
  dbus_connection_unref (connection);

  return retval;
}

/**
 * Sets the mainloop wakeup function for the connection. Thi function is
 * responsible for waking up the main loop (if its sleeping) when some some
 * change has happened to the connection that the mainloop needs to reconsiders
 * (e.g. a message has been queued for writing).
 * When using Qt, this typically results in a call to QEventLoop::wakeUp().
 * When using GLib, it would call g_main_context_wakeup().
 *
 *
 * @param connection the connection.
 * @param wakeup_main_function function to wake up the mainloop
 * @param data data to pass wakeup_main_function
 * @param free_data_function function to be called to free the data.
 */
void
dbus_connection_set_wakeup_main_function (DBusConnection            *connection,
					  DBusWakeupMainFunction     wakeup_main_function,
					  void                      *data,
					  DBusFreeFunction           free_data_function)
{
  void *old_data;
  DBusFreeFunction old_free_data;

  _dbus_return_if_fail (connection != NULL);
  
  CONNECTION_LOCK (connection);
  old_data = connection->wakeup_main_data;
  old_free_data = connection->free_wakeup_main_data;

  connection->wakeup_main_function = wakeup_main_function;
  connection->wakeup_main_data = data;
  connection->free_wakeup_main_data = free_data_function;
  
  CONNECTION_UNLOCK (connection);

  /* Callback outside the lock */
  if (old_free_data)
    (*old_free_data) (old_data);
}

/**
 * Set a function to be invoked when the dispatch status changes.
 * If the dispatch status is #DBUS_DISPATCH_DATA_REMAINS, then
 * dbus_connection_dispatch() needs to be called to process incoming
 * messages. However, dbus_connection_dispatch() MUST NOT BE CALLED
 * from inside the DBusDispatchStatusFunction. Indeed, almost
 * any reentrancy in this function is a bad idea. Instead,
 * the DBusDispatchStatusFunction should simply save an indication
 * that messages should be dispatched later, when the main loop
 * is re-entered.
 *
 * @param connection the connection
 * @param function function to call on dispatch status changes
 * @param data data for function
 * @param free_data_function free the function data
 */
void
dbus_connection_set_dispatch_status_function (DBusConnection             *connection,
                                              DBusDispatchStatusFunction  function,
                                              void                       *data,
                                              DBusFreeFunction            free_data_function)
{
  void *old_data;
  DBusFreeFunction old_free_data;

  _dbus_return_if_fail (connection != NULL);
  
  CONNECTION_LOCK (connection);
  old_data = connection->dispatch_status_data;
  old_free_data = connection->free_dispatch_status_data;

  connection->dispatch_status_function = function;
  connection->dispatch_status_data = data;
  connection->free_dispatch_status_data = free_data_function;
  
  CONNECTION_UNLOCK (connection);

  /* Callback outside the lock */
  if (old_free_data)
    (*old_free_data) (old_data);
}

/**
 * Get the UNIX file descriptor of the connection, if any.  This can
 * be used for SELinux access control checks with getpeercon() for
 * example. DO NOT read or write to the file descriptor, or try to
 * select() on it; use DBusWatch for main loop integration. Not all
 * connections will have a file descriptor. So for adding descriptors
 * to the main loop, use dbus_watch_get_fd() and so forth.
 *
 * @param connection the connection
 * @param fd return location for the file descriptor.
 * @returns #TRUE if fd is successfully obtained.
 */
dbus_bool_t
dbus_connection_get_unix_fd (DBusConnection *connection,
                             int            *fd)
{
  dbus_bool_t retval;

  _dbus_return_val_if_fail (connection != NULL, FALSE);
  _dbus_return_val_if_fail (connection->transport != NULL, FALSE);
  
  CONNECTION_LOCK (connection);
  
  retval = _dbus_transport_get_unix_fd (connection->transport,
                                        fd);

  CONNECTION_UNLOCK (connection);

  return retval;
}

/**
 * Gets the UNIX user ID of the connection if any.
 * Returns #TRUE if the uid is filled in.
 * Always returns #FALSE on non-UNIX platforms.
 * Always returns #FALSE prior to authenticating the
 * connection.
 *
 * @param connection the connection
 * @param uid return location for the user ID
 * @returns #TRUE if uid is filled in with a valid user ID
 */
dbus_bool_t
dbus_connection_get_unix_user (DBusConnection *connection,
                               unsigned long  *uid)
{
  dbus_bool_t result;

  _dbus_return_val_if_fail (connection != NULL, FALSE);
  _dbus_return_val_if_fail (uid != NULL, FALSE);
  
  CONNECTION_LOCK (connection);

  if (!_dbus_transport_get_is_authenticated (connection->transport))
    result = FALSE;
  else
    result = _dbus_transport_get_unix_user (connection->transport,
                                            uid);
  CONNECTION_UNLOCK (connection);

  return result;
}

/**
 * Gets the process ID of the connection if any.
 * Returns #TRUE if the uid is filled in.
 * Always returns #FALSE prior to authenticating the
 * connection.
 *
 * @param connection the connection
 * @param pid return location for the process ID
 * @returns #TRUE if uid is filled in with a valid process ID
 */
dbus_bool_t
dbus_connection_get_unix_process_id (DBusConnection *connection,
				     unsigned long  *pid)
{
  dbus_bool_t result;

  _dbus_return_val_if_fail (connection != NULL, FALSE);
  _dbus_return_val_if_fail (pid != NULL, FALSE);
  
  CONNECTION_LOCK (connection);

  if (!_dbus_transport_get_is_authenticated (connection->transport))
    result = FALSE;
  else
    result = _dbus_transport_get_unix_process_id (connection->transport,
						  pid);
  CONNECTION_UNLOCK (connection);

  return result;
}

/**
 * Sets a predicate function used to determine whether a given user ID
 * is allowed to connect. When an incoming connection has
 * authenticated with a particular user ID, this function is called;
 * if it returns #TRUE, the connection is allowed to proceed,
 * otherwise the connection is disconnected.
 *
 * If the function is set to #NULL (as it is by default), then
 * only the same UID as the server process will be allowed to
 * connect.
 *
 * @param connection the connection
 * @param function the predicate
 * @param data data to pass to the predicate
 * @param free_data_function function to free the data
 */
void
dbus_connection_set_unix_user_function (DBusConnection             *connection,
                                        DBusAllowUnixUserFunction   function,
                                        void                       *data,
                                        DBusFreeFunction            free_data_function)
{
  void *old_data = NULL;
  DBusFreeFunction old_free_function = NULL;

  _dbus_return_if_fail (connection != NULL);
  
  CONNECTION_LOCK (connection);
  _dbus_transport_set_unix_user_function (connection->transport,
                                          function, data, free_data_function,
                                          &old_data, &old_free_function);
  CONNECTION_UNLOCK (connection);

  if (old_free_function != NULL)
    (* old_free_function) (old_data);    
}

/**
 * Adds a message filter. Filters are handlers that are run on all
 * incoming messages, prior to the objects registered with
 * dbus_connection_register_object_path().  Filters are run in the
 * order that they were added.  The same handler can be added as a
 * filter more than once, in which case it will be run more than once.
 * Filters added during a filter callback won't be run on the message
 * being processed.
 *
 * @todo we don't run filters on messages while blocking without
 * entering the main loop, since filters are run as part of
 * dbus_connection_dispatch(). This is probably a feature, as filters
 * could create arbitrary reentrancy. But kind of sucks if you're
 * trying to filter METHOD_RETURN for some reason.
 *
 * @param connection the connection
 * @param function function to handle messages
 * @param user_data user data to pass to the function
 * @param free_data_function function to use for freeing user data
 * @returns #TRUE on success, #FALSE if not enough memory.
 */
dbus_bool_t
dbus_connection_add_filter (DBusConnection            *connection,
                            DBusHandleMessageFunction  function,
                            void                      *user_data,
                            DBusFreeFunction           free_data_function)
{
  DBusMessageFilter *filter;
  
  _dbus_return_val_if_fail (connection != NULL, FALSE);
  _dbus_return_val_if_fail (function != NULL, FALSE);

  filter = dbus_new0 (DBusMessageFilter, 1);
  if (filter == NULL)
    return FALSE;

  filter->refcount.value = 1;
  
  CONNECTION_LOCK (connection);

  if (!_dbus_list_append (&connection->filter_list,
                          filter))
    {
      _dbus_message_filter_unref (filter);
      CONNECTION_UNLOCK (connection);
      return FALSE;
    }

  /* Fill in filter after all memory allocated,
   * so we don't run the free_user_data_function
   * if the add_filter() fails
   */
  
  filter->function = function;
  filter->user_data = user_data;
  filter->free_user_data_function = free_data_function;
        
  CONNECTION_UNLOCK (connection);
  return TRUE;
}

/**
 * Removes a previously-added message filter. It is a programming
 * error to call this function for a handler that has not been added
 * as a filter. If the given handler was added more than once, only
 * one instance of it will be removed (the most recently-added
 * instance).
 *
 * @param connection the connection
 * @param function the handler to remove
 * @param user_data user data for the handler to remove
 *
 */
void
dbus_connection_remove_filter (DBusConnection            *connection,
                               DBusHandleMessageFunction  function,
                               void                      *user_data)
{
  DBusList *link;
  DBusMessageFilter *filter;
  
  _dbus_return_if_fail (connection != NULL);
  _dbus_return_if_fail (function != NULL);
  
  CONNECTION_LOCK (connection);

  filter = NULL;
  
  link = _dbus_list_get_last_link (&connection->filter_list);
  while (link != NULL)
    {
      filter = link->data;

      if (filter->function == function &&
          filter->user_data == user_data)
        {
          _dbus_list_remove_link (&connection->filter_list, link);
          filter->function = NULL;
          
          break;
        }
        
      link = _dbus_list_get_prev_link (&connection->filter_list, link);
    }
  
  CONNECTION_UNLOCK (connection);

#ifndef DBUS_DISABLE_CHECKS
  if (filter == NULL)
    {
      _dbus_warn ("Attempt to remove filter function %p user data %p, but no such filter has been added\n",
                  function, user_data);
      return;
    }
#endif
  
  /* Call application code */
  if (filter->free_user_data_function)
    (* filter->free_user_data_function) (filter->user_data);

  filter->free_user_data_function = NULL;
  filter->user_data = NULL;
  
  _dbus_message_filter_unref (filter);
}

/**
 * Registers a handler for a given path in the object hierarchy.
 * The given vtable handles messages sent to exactly the given path.
 *
 *
 * @param connection the connection
 * @param path a '/' delimited string of path elements
 * @param vtable the virtual table
 * @param user_data data to pass to functions in the vtable
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_connection_register_object_path (DBusConnection              *connection,
                                      const char                  *path,
                                      const DBusObjectPathVTable  *vtable,
                                      void                        *user_data)
{
  char **decomposed_path;
  dbus_bool_t retval;
  
  _dbus_return_val_if_fail (connection != NULL, FALSE);
  _dbus_return_val_if_fail (path != NULL, FALSE);
  _dbus_return_val_if_fail (path[0] == '/', FALSE);
  _dbus_return_val_if_fail (vtable != NULL, FALSE);

  if (!_dbus_decompose_path (path, strlen (path), &decomposed_path, NULL))
    return FALSE;

  CONNECTION_LOCK (connection);

  retval = _dbus_object_tree_register (connection->objects,
                                       FALSE,
                                       (const char **) decomposed_path, vtable,
                                       user_data);

  CONNECTION_UNLOCK (connection);

  dbus_free_string_array (decomposed_path);

  return retval;
}

/**
 * Registers a fallback handler for a given subsection of the object
 * hierarchy.  The given vtable handles messages at or below the given
 * path. You can use this to establish a default message handling
 * policy for a whole "subdirectory."
 *
 * @param connection the connection
 * @param path a '/' delimited string of path elements
 * @param vtable the virtual table
 * @param user_data data to pass to functions in the vtable
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_connection_register_fallback (DBusConnection              *connection,
                                   const char                  *path,
                                   const DBusObjectPathVTable  *vtable,
                                   void                        *user_data)
{
  char **decomposed_path;
  dbus_bool_t retval;
  
  _dbus_return_val_if_fail (connection != NULL, FALSE);
  _dbus_return_val_if_fail (path != NULL, FALSE);
  _dbus_return_val_if_fail (path[0] == '/', FALSE);
  _dbus_return_val_if_fail (vtable != NULL, FALSE);

  if (!_dbus_decompose_path (path, strlen (path), &decomposed_path, NULL))
    return FALSE;

  CONNECTION_LOCK (connection);

  retval = _dbus_object_tree_register (connection->objects,
                                       TRUE,
				       (const char **) decomposed_path, vtable,
                                       user_data);

  CONNECTION_UNLOCK (connection);

  dbus_free_string_array (decomposed_path);

  return retval;
}

/**
 * Unregisters the handler registered with exactly the given path.
 * It's a bug to call this function for a path that isn't registered.
 * Can unregister both fallback paths and object paths.
 *
 * @param connection the connection
 * @param path a '/' delimited string of path elements
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_connection_unregister_object_path (DBusConnection              *connection,
                                        const char                  *path)
{
  char **decomposed_path;

  _dbus_return_val_if_fail (connection != NULL, FALSE);
  _dbus_return_val_if_fail (path != NULL, FALSE);
  _dbus_return_val_if_fail (path[0] == '/', FALSE);

  if (!_dbus_decompose_path (path, strlen (path), &decomposed_path, NULL))
      return FALSE;

  CONNECTION_LOCK (connection);

  _dbus_object_tree_unregister_and_unlock (connection->objects, (const char **) decomposed_path);

  dbus_free_string_array (decomposed_path);

  return TRUE;
}

/**
 * Gets the user data passed to dbus_connection_register_object_path()
 * or dbus_connection_register_fallback(). If nothing was registered
 * at this path, the data is filled in with #NULL.
 *
 * @param connection the connection
 * @param path the path you registered with
 * @param data_p location to store the user data, or #NULL
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_connection_get_object_path_data (DBusConnection *connection,
                                      const char     *path,
                                      void          **data_p)
{
  char **decomposed_path;

  _dbus_return_val_if_fail (connection != NULL, FALSE);
  _dbus_return_val_if_fail (path != NULL, FALSE);
  _dbus_return_val_if_fail (data_p != NULL, FALSE);

  *data_p = NULL;
  
  if (!_dbus_decompose_path (path, strlen (path), &decomposed_path, NULL))
    return FALSE;
  
  CONNECTION_LOCK (connection);

  *data_p = _dbus_object_tree_get_user_data_unlocked (connection->objects, (const char**) decomposed_path);

  CONNECTION_UNLOCK (connection);

  dbus_free_string_array (decomposed_path);

  return TRUE;
}

/**
 * Lists the registered fallback handlers and object path handlers at
 * the given parent_path. The returned array should be freed with
 * dbus_free_string_array().
 *
 * @param connection the connection
 * @param parent_path the path to list the child handlers of
 * @param child_entries returns #NULL-terminated array of children
 * @returns #FALSE if no memory to allocate the child entries
 */
dbus_bool_t
dbus_connection_list_registered (DBusConnection              *connection,
                                 const char                  *parent_path,
                                 char                      ***child_entries)
{
  char **decomposed_path;
  dbus_bool_t retval;
  _dbus_return_val_if_fail (connection != NULL, FALSE);
  _dbus_return_val_if_fail (parent_path != NULL, FALSE);
  _dbus_return_val_if_fail (parent_path[0] == '/', FALSE);
  _dbus_return_val_if_fail (child_entries != NULL, FALSE);

  if (!_dbus_decompose_path (parent_path, strlen (parent_path), &decomposed_path, NULL))
    return FALSE;

  CONNECTION_LOCK (connection);

  retval = _dbus_object_tree_list_registered_and_unlock (connection->objects,
							 (const char **) decomposed_path,
							 child_entries);
  dbus_free_string_array (decomposed_path);

  return retval;
}

static DBusDataSlotAllocator slot_allocator;
_DBUS_DEFINE_GLOBAL_LOCK (connection_slots);

/**
 * Allocates an integer ID to be used for storing application-specific
 * data on any DBusConnection. The allocated ID may then be used
 * with dbus_connection_set_data() and dbus_connection_get_data().
 * The passed-in slot must be initialized to -1, and is filled in
 * with the slot ID. If the passed-in slot is not -1, it's assumed
 * to be already allocated, and its refcount is incremented.
 * 
 * The allocated slot is global, i.e. all DBusConnection objects will
 * have a slot with the given integer ID reserved.
 *
 * @param slot_p address of a global variable storing the slot
 * @returns #FALSE on failure (no memory)
 */
dbus_bool_t
dbus_connection_allocate_data_slot (dbus_int32_t *slot_p)
{
  return _dbus_data_slot_allocator_alloc (&slot_allocator,
                                          _DBUS_LOCK_NAME (connection_slots),
                                          slot_p);
}

/**
 * Deallocates a global ID for connection data slots.
 * dbus_connection_get_data() and dbus_connection_set_data() may no
 * longer be used with this slot.  Existing data stored on existing
 * DBusConnection objects will be freed when the connection is
 * finalized, but may not be retrieved (and may only be replaced if
 * someone else reallocates the slot).  When the refcount on the
 * passed-in slot reaches 0, it is set to -1.
 *
 * @param slot_p address storing the slot to deallocate
 */
void
dbus_connection_free_data_slot (dbus_int32_t *slot_p)
{
  _dbus_return_if_fail (*slot_p >= 0);
  
  _dbus_data_slot_allocator_free (&slot_allocator, slot_p);
}

/**
 * Stores a pointer on a DBusConnection, along
 * with an optional function to be used for freeing
 * the data when the data is set again, or when
 * the connection is finalized. The slot number
 * must have been allocated with dbus_connection_allocate_data_slot().
 *
 * @param connection the connection
 * @param slot the slot number
 * @param data the data to store
 * @param free_data_func finalizer function for the data
 * @returns #TRUE if there was enough memory to store the data
 */
dbus_bool_t
dbus_connection_set_data (DBusConnection   *connection,
                          dbus_int32_t      slot,
                          void             *data,
                          DBusFreeFunction  free_data_func)
{
  DBusFreeFunction old_free_func;
  void *old_data;
  dbus_bool_t retval;

  _dbus_return_val_if_fail (connection != NULL, FALSE);
  _dbus_return_val_if_fail (slot >= 0, FALSE);
  
  CONNECTION_LOCK (connection);

  retval = _dbus_data_slot_list_set (&slot_allocator,
                                     &connection->slot_list,
                                     slot, data, free_data_func,
                                     &old_free_func, &old_data);
  
  CONNECTION_UNLOCK (connection);

  if (retval)
    {
      /* Do the actual free outside the connection lock */
      if (old_free_func)
        (* old_free_func) (old_data);
    }

  return retval;
}

/**
 * Retrieves data previously set with dbus_connection_set_data().
 * The slot must still be allocated (must not have been freed).
 *
 * @param connection the connection
 * @param slot the slot to get data from
 * @returns the data, or #NULL if not found
 */
void*
dbus_connection_get_data (DBusConnection   *connection,
                          dbus_int32_t      slot)
{
  void *res;

  _dbus_return_val_if_fail (connection != NULL, NULL);
  
  CONNECTION_LOCK (connection);

  res = _dbus_data_slot_list_get (&slot_allocator,
                                  &connection->slot_list,
                                  slot);
  
  CONNECTION_UNLOCK (connection);

  return res;
}

/**
 * This function sets a global flag for whether dbus_connection_new()
 * will set SIGPIPE behavior to SIG_IGN.
 *
 * @param will_modify_sigpipe #TRUE to allow sigpipe to be set to SIG_IGN
 */
void
dbus_connection_set_change_sigpipe (dbus_bool_t will_modify_sigpipe)
{  
  _dbus_modify_sigpipe = will_modify_sigpipe != FALSE;
}

/**
 * Specifies the maximum size message this connection is allowed to
 * receive. Larger messages will result in disconnecting the
 * connection.
 * 
 * @param connection a #DBusConnection
 * @param size maximum message size the connection can receive, in bytes
 */
void
dbus_connection_set_max_message_size (DBusConnection *connection,
                                      long            size)
{
  _dbus_return_if_fail (connection != NULL);
  
  CONNECTION_LOCK (connection);
  _dbus_transport_set_max_message_size (connection->transport,
                                        size);
  CONNECTION_UNLOCK (connection);
}

/**
 * Gets the value set by dbus_connection_set_max_message_size().
 *
 * @param connection the connection
 * @returns the max size of a single message
 */
long
dbus_connection_get_max_message_size (DBusConnection *connection)
{
  long res;

  _dbus_return_val_if_fail (connection != NULL, 0);
  
  CONNECTION_LOCK (connection);
  res = _dbus_transport_get_max_message_size (connection->transport);
  CONNECTION_UNLOCK (connection);
  return res;
}

/**
 * Sets the maximum total number of bytes that can be used for all messages
 * received on this connection. Messages count toward the maximum until
 * they are finalized. When the maximum is reached, the connection will
 * not read more data until some messages are finalized.
 *
 * The semantics of the maximum are: if outstanding messages are
 * already above the maximum, additional messages will not be read.
 * The semantics are not: if the next message would cause us to exceed
 * the maximum, we don't read it. The reason is that we don't know the
 * size of a message until after we read it.
 *
 * Thus, the max live messages size can actually be exceeded
 * by up to the maximum size of a single message.
 * 
 * Also, if we read say 1024 bytes off the wire in a single read(),
 * and that contains a half-dozen small messages, we may exceed the
 * size max by that amount. But this should be inconsequential.
 *
 * This does imply that we can't call read() with a buffer larger
 * than we're willing to exceed this limit by.
 *
 * @param connection the connection
 * @param size the maximum size in bytes of all outstanding messages
 */
void
dbus_connection_set_max_received_size (DBusConnection *connection,
                                       long            size)
{
  _dbus_return_if_fail (connection != NULL);
  
  CONNECTION_LOCK (connection);
  _dbus_transport_set_max_received_size (connection->transport,
                                         size);
  CONNECTION_UNLOCK (connection);
}

/**
 * Gets the value set by dbus_connection_set_max_received_size().
 *
 * @param connection the connection
 * @returns the max size of all live messages
 */
long
dbus_connection_get_max_received_size (DBusConnection *connection)
{
  long res;

  _dbus_return_val_if_fail (connection != NULL, 0);
  
  CONNECTION_LOCK (connection);
  res = _dbus_transport_get_max_received_size (connection->transport);
  CONNECTION_UNLOCK (connection);
  return res;
}

/**
 * Gets the approximate size in bytes of all messages in the outgoing
 * message queue. The size is approximate in that you shouldn't use
 * it to decide how many bytes to read off the network or anything
 * of that nature, as optimizations may choose to tell small white lies
 * to avoid performance overhead.
 *
 * @param connection the connection
 * @returns the number of bytes that have been queued up but not sent
 */
long
dbus_connection_get_outgoing_size (DBusConnection *connection)
{
  long res;

  _dbus_return_val_if_fail (connection != NULL, 0);
  
  CONNECTION_LOCK (connection);
  res = _dbus_counter_get_value (connection->outgoing_counter);
  CONNECTION_UNLOCK (connection);
  return res;
}

/** @} */
