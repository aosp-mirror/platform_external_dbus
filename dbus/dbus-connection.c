/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-connection.c DBusConnection object
 *
 * Copyright (C) 2002, 2003  Red Hat Inc.
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

#include "dbus-connection.h"
#include "dbus-list.h"
#include "dbus-timeout.h"
#include "dbus-transport.h"
#include "dbus-watch.h"
#include "dbus-connection-internal.h"
#include "dbus-list.h"
#include "dbus-hash.h"
#include "dbus-message-internal.h"
#include "dbus-message-handler.h"
#include "dbus-threads.h"
#include "dbus-protocol.h"
#include "dbus-dataslot.h"

/**
 * @defgroup DBusConnection DBusConnection
 * @ingroup  DBus
 * @brief Connection to another application
 *
 * A DBusConnection represents a connection to another
 * application. Messages can be sent and received via this connection.
 *
 * The connection maintains a queue of incoming messages and a queue
 * of outgoing messages. dbus_connection_pop_message() and friends
 * can be used to read incoming messages from the queue.
 * Outgoing messages are automatically discarded as they are
 * written to the network.
 *
 * In brief a DBusConnection is a message queue associated with some
 * message transport mechanism such as a socket.
 * 
 */

/**
 * @defgroup DBusConnectionInternals DBusConnection implementation details
 * @ingroup  DBusInternals
 * @brief Implementation details of DBusConnection
 *
 * @{
 */

/** default timeout value when waiting for a message reply */
#define DEFAULT_TIMEOUT_VALUE (15 * 1000)

static dbus_bool_t _dbus_modify_sigpipe = TRUE;

/**
 * Implementation details of DBusConnection. All fields are private.
 */
struct DBusConnection
{
  int refcount; /**< Reference count. */

  DBusMutex *mutex; /**< Lock on the entire DBusConnection */

  dbus_bool_t dispatch_acquired; /**< Protects dispatch_message */
  DBusCondVar *dispatch_cond;    /**< Protects dispatch_message */

  dbus_bool_t io_path_acquired;  /**< Protects transport io path */
  DBusCondVar *io_path_cond;     /**< Protects transport io path */
  
  DBusList *outgoing_messages; /**< Queue of messages we need to send, send the end of the list first. */
  DBusList *incoming_messages; /**< Queue of messages we have received, end of the list received most recently. */

  DBusMessage *message_borrowed; /**< True if the first incoming message has been borrowed */
  DBusCondVar *message_returned_cond; /**< Used with dbus_connection_borrow_message() */
  
  int n_outgoing;              /**< Length of outgoing queue. */
  int n_incoming;              /**< Length of incoming queue. */
  
  DBusTransport *transport;    /**< Object that sends/receives messages over network. */
  DBusWatchList *watches;      /**< Stores active watches. */
  DBusTimeoutList *timeouts;   /**< Stores active timeouts. */
  
  DBusHashTable *handler_table; /**< Table of registered DBusMessageHandler */
  DBusList *filter_list;        /**< List of filters. */

  DBusDataSlotList slot_list;   /**< Data stored by allocated integer ID */

  DBusHashTable *pending_replies;  /**< Hash of message serials and their message handlers. */  
  DBusCounter *connection_counter; /**< Counter that we decrement when finalized */
  
  int client_serial;            /**< Client serial. Increments each time a message is sent  */
  DBusList *disconnect_message_link; /**< Preallocated list node for queueing the disconnection message */

  DBusWakeupMainFunction wakeup_main_function; /**< Function to wake up the mainloop  */
  void *wakeup_main_data; /**< Application data for wakeup_main_function */
  DBusFreeFunction free_wakeup_main_data; /**< free wakeup_main_data */
};

typedef struct
{
  DBusConnection *connection;
  DBusMessageHandler *handler;
  DBusTimeout *timeout;
  int serial;

  DBusList *timeout_link; /* Preallocated timeout response */
  
  dbus_bool_t timeout_added;
  dbus_bool_t connection_added;
} ReplyHandlerData;

static void reply_handler_data_free (ReplyHandlerData *data);

static void _dbus_connection_remove_timeout_locked (DBusConnection *connection,
						    DBusTimeout    *timeout);

/**
 * Acquires the connection lock.
 *
 * @param connection the connection.
 */
void
_dbus_connection_lock (DBusConnection *connection)
{
  dbus_mutex_lock (connection->mutex);
}

/**
 * Releases the connection lock.
 *
 * @param connection the connection.
 */
void
_dbus_connection_unlock (DBusConnection *connection)
{
  dbus_mutex_unlock (connection->mutex);
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

/**
 * Adds a message to the incoming message queue, returning #FALSE
 * if there's insufficient memory to queue the message.
 *
 * @param connection the connection.
 * @param message the message to queue.
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_connection_queue_received_message (DBusConnection *connection,
                                         DBusMessage    *message)
{
  ReplyHandlerData *reply_handler_data;
  dbus_int32_t reply_serial;
  
  _dbus_assert (_dbus_transport_get_is_authenticated (connection->transport));
  
  if (!_dbus_list_append (&connection->incoming_messages,
                          message))
    return FALSE;

  /* If this is a reply we're waiting on, remove timeout for it */
  reply_serial = _dbus_message_get_reply_serial (message);
  if (reply_serial != -1)
    {
      reply_handler_data = _dbus_hash_table_lookup_int (connection->pending_replies,
							reply_serial);
      if (reply_handler_data != NULL)
	{
	  if (reply_handler_data->timeout_added)
	    _dbus_connection_remove_timeout_locked (connection,
						    reply_handler_data->timeout);
	  reply_handler_data->timeout_added = FALSE;
	}
    }
  
  dbus_message_ref (message);
  connection->n_incoming += 1;

  _dbus_connection_wakeup_mainloop (connection);
  
  _dbus_verbose ("Incoming message %p added to queue, %d incoming\n",
                 message, connection->n_incoming);
  
  return TRUE;
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
  _dbus_list_append_link (&connection->incoming_messages, link);

  connection->n_incoming += 1;

  _dbus_connection_wakeup_mainloop (connection);
  
  _dbus_verbose ("Incoming synthesized message %p added to queue, %d incoming\n",
                 link->data, connection->n_incoming);
}


/**
 * Checks whether there are messages in the outgoing message queue.
 *
 * @param connection the connection.
 * @returns #TRUE if the outgoing queue is non-empty.
 */
dbus_bool_t
_dbus_connection_have_messages_to_send (DBusConnection *connection)
{
  return connection->outgoing_messages != NULL;
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
  return _dbus_list_get_last (&connection->outgoing_messages);
}

/**
 * Notifies the connection that a message has been sent, so the
 * message can be removed from the outgoing queue.
 *
 * @param connection the connection.
 * @param message the message that was sent.
 */
void
_dbus_connection_message_sent (DBusConnection *connection,
                               DBusMessage    *message)
{
  _dbus_assert (_dbus_transport_get_is_authenticated (connection->transport));
  _dbus_assert (message == _dbus_list_get_last (&connection->outgoing_messages));
  
  _dbus_list_pop_last (&connection->outgoing_messages);
  dbus_message_unref (message);
  
  connection->n_outgoing -= 1;

  _dbus_verbose ("Message %p removed from outgoing queue, %d left to send\n",
                 message, connection->n_outgoing);
  
  if (connection->n_outgoing == 0)
    _dbus_transport_messages_pending (connection->transport,
                                      connection->n_outgoing);  
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
  if (connection->watches) /* null during finalize */
    return _dbus_watch_list_add_watch (connection->watches,
                                       watch);
  else
    return FALSE;
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
  if (connection->watches) /* null during finalize */
    _dbus_watch_list_remove_watch (connection->watches,
                                   watch);
}

/**
 * Adds a timeout using the connection's DBusAddTimeoutFunction if
 * available. Otherwise records the timeout to be added when said
 * function is available. Also re-adds the timeout if the
 * DBusAddTimeoutFunction changes. May fail due to lack of memory.
 *
 * @param connection the connection.
 * @param timeout the timeout to add.
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_connection_add_timeout (DBusConnection *connection,
			      DBusTimeout    *timeout)
{
 if (connection->timeouts) /* null during finalize */
    return _dbus_timeout_list_add_timeout (connection->timeouts,
					   timeout);
  else
    return FALSE;  
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
  if (connection->timeouts) /* null during finalize */
    _dbus_timeout_list_remove_timeout (connection->timeouts,
				       timeout);
}

static void
_dbus_connection_remove_timeout_locked (DBusConnection *connection,
					DBusTimeout    *timeout)
{
  dbus_mutex_lock (connection->mutex);
  _dbus_connection_remove_timeout (connection, timeout);
  dbus_mutex_unlock (connection->mutex);
}


/**
 * Tells the connection that the transport has been disconnected.
 * Results in posting a disconnect message on the incoming message
 * queue.  Only has an effect the first time it's called.
 *
 * @param connection the connection
 */
void
_dbus_connection_notify_disconnected (DBusConnection *connection)
{
  if (connection->disconnect_message_link)
    {
      /* We haven't sent the disconnect message already */
      _dbus_connection_queue_synthesized_message_link (connection,
						       connection->disconnect_message_link);
      connection->disconnect_message_link = NULL;
    }
}


/**
 * Acquire the transporter I/O path. This must be done before
 * doing any I/O in the transporter. May sleep and drop the
 * connection mutex while waiting for the I/O path.
 *
 * @param connection the connection.
 * @param timeout_milliseconds maximum blocking time, or -1 for no limit.
 * @returns TRUE if the I/O path was acquired.
 */
static dbus_bool_t
_dbus_connection_acquire_io_path (DBusConnection *connection,
				  int timeout_milliseconds)
{
  dbus_bool_t res = TRUE;

  if (connection->io_path_acquired)
    {
      if (timeout_milliseconds != -1) 
	res = dbus_condvar_wait_timeout (connection->io_path_cond,
					 connection->mutex,
					 timeout_milliseconds);
      else
	dbus_condvar_wait (connection->io_path_cond, connection->mutex);
    }
  
  if (res)
    {
      _dbus_assert (!connection->io_path_acquired);

      connection->io_path_acquired = TRUE;
    }
  
  return res;
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
  _dbus_assert (connection->io_path_acquired);

  connection->io_path_acquired = FALSE;
  dbus_condvar_wake_one (connection->io_path_cond);
}


/**
 * Queues incoming messages and sends outgoing messages for this
 * connection, optionally blocking in the process. Each call to
 * _dbus_connection_do_iteration() will call select() or poll() one
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
 * @param connection the connection.
 * @param flags iteration flags.
 * @param timeout_milliseconds maximum blocking time, or -1 for no limit.
 */
void
_dbus_connection_do_iteration (DBusConnection *connection,
                               unsigned int    flags,
                               int             timeout_milliseconds)
{
  if (connection->n_outgoing == 0)
    flags &= ~DBUS_ITERATION_DO_WRITING;

  if (_dbus_connection_acquire_io_path (connection,
					(flags & DBUS_ITERATION_BLOCK)?timeout_milliseconds:0))
    {
      _dbus_transport_do_iteration (connection->transport,
				    flags, timeout_milliseconds);
      _dbus_connection_release_io_path (connection);
    }
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
  DBusHashTable *handler_table, *pending_replies;
  DBusMutex *mutex;
  DBusCondVar *message_returned_cond;
  DBusCondVar *dispatch_cond;
  DBusCondVar *io_path_cond;
  DBusList *disconnect_link;
  DBusMessage *disconnect_message;

  watch_list = NULL;
  connection = NULL;
  handler_table = NULL;
  pending_replies = NULL;
  timeout_list = NULL;
  mutex = NULL;
  message_returned_cond = NULL;
  dispatch_cond = NULL;
  io_path_cond = NULL;
  disconnect_link = NULL;
  disconnect_message = NULL;
  
  watch_list = _dbus_watch_list_new ();
  if (watch_list == NULL)
    goto error;

  timeout_list = _dbus_timeout_list_new ();
  if (timeout_list == NULL)
    goto error;
  
  handler_table =
    _dbus_hash_table_new (DBUS_HASH_STRING,
                          dbus_free, NULL);
  if (handler_table == NULL)
    goto error;

  pending_replies =
    _dbus_hash_table_new (DBUS_HASH_INT,
			  NULL, (DBusFreeFunction)reply_handler_data_free);
  if (pending_replies == NULL)
    goto error;
  
  connection = dbus_new0 (DBusConnection, 1);
  if (connection == NULL)
    goto error;

  mutex = dbus_mutex_new ();
  if (mutex == NULL)
    goto error;
  
  message_returned_cond = dbus_condvar_new ();
  if (message_returned_cond == NULL)
    goto error;
  
  dispatch_cond = dbus_condvar_new ();
  if (dispatch_cond == NULL)
    goto error;
  
  io_path_cond = dbus_condvar_new ();
  if (io_path_cond == NULL)
    goto error;

  disconnect_message = dbus_message_new (NULL, DBUS_MESSAGE_LOCAL_DISCONNECT);
  if (disconnect_message == NULL)
    goto error;

  disconnect_link = _dbus_list_alloc_link (disconnect_message);
  if (disconnect_link == NULL)
    goto error;

  if (_dbus_modify_sigpipe)
    _dbus_disable_sigpipe ();
  
  connection->refcount = 1;
  connection->mutex = mutex;
  connection->dispatch_cond = dispatch_cond;
  connection->io_path_cond = io_path_cond;
  connection->message_returned_cond = message_returned_cond;
  connection->transport = transport;
  connection->watches = watch_list;
  connection->timeouts = timeout_list;
  connection->handler_table = handler_table;
  connection->pending_replies = pending_replies;
  connection->filter_list = NULL;

  _dbus_data_slot_list_init (&connection->slot_list);

  connection->client_serial = 1;

  connection->disconnect_message_link = disconnect_link;
  
  _dbus_transport_ref (transport);
  _dbus_transport_set_connection (transport, connection);
  
  return connection;
  
 error:
  if (disconnect_message != NULL)
    dbus_message_unref (disconnect_message);
  
  if (disconnect_link != NULL)
    _dbus_list_free_link (disconnect_link);
  
  if (io_path_cond != NULL)
    dbus_condvar_free (io_path_cond);
  
  if (dispatch_cond != NULL)
    dbus_condvar_free (dispatch_cond);
  
  if (message_returned_cond != NULL)
    dbus_condvar_free (message_returned_cond);
  
  if (mutex != NULL)
    dbus_mutex_free (mutex);

  if (connection != NULL)
    dbus_free (connection);

  if (handler_table)
    _dbus_hash_table_unref (handler_table);

  if (pending_replies)
    _dbus_hash_table_unref (pending_replies);
  
  if (watch_list)
    _dbus_watch_list_free (watch_list);

  if (timeout_list)
    _dbus_timeout_list_free (timeout_list);
  
  return NULL;
}

static dbus_int32_t
_dbus_connection_get_next_client_serial (DBusConnection *connection)
{
  int serial;

  serial = connection->client_serial++;

  if (connection->client_serial < 0)
    connection->client_serial = 1;
  
  return serial;
}

/**
 * Used to notify a connection when a DBusMessageHandler is
 * destroyed, so the connection can drop any reference
 * to the handler. This is a private function, but still
 * takes the connection lock. Don't call it with the lock held.
 *
 * @todo needs to check in pending_replies too.
 * 
 * @param connection the connection
 * @param handler the handler
 */
void
_dbus_connection_handler_destroyed_locked (DBusConnection     *connection,
					   DBusMessageHandler *handler)
{
  DBusHashIter iter;
  DBusList *link;

  dbus_mutex_lock (connection->mutex);
  
  _dbus_hash_iter_init (connection->handler_table, &iter);
  while (_dbus_hash_iter_next (&iter))
    {
      DBusMessageHandler *h = _dbus_hash_iter_get_value (&iter);

      if (h == handler)
        _dbus_hash_iter_remove_entry (&iter);
    }

  link = _dbus_list_get_first_link (&connection->filter_list);
  while (link != NULL)
    {
      DBusMessageHandler *h = link->data;
      DBusList *next = _dbus_list_get_next_link (&connection->filter_list, link);

      if (h == handler)
        _dbus_list_remove_link (&connection->filter_list,
                                link);
      
      link = next;
    }
  dbus_mutex_unlock (connection->mutex);
}

/**
 * Adds the counter used to count the number of open connections.
 * Increments the counter by one, and saves it to be decremented
 * again when this connection is finalized.
 *
 * @param connection a #DBusConnection
 * @param counter counter that tracks number of connections
 */
void
_dbus_connection_set_connection_counter (DBusConnection *connection,
                                         DBusCounter    *counter)
{
  _dbus_assert (connection->connection_counter == NULL);
  
  connection->connection_counter = counter;
  _dbus_counter_ref (connection->connection_counter);
  _dbus_counter_adjust (connection->connection_counter, 1);
}

/** @} */

/**
 * @addtogroup DBusConnection
 *
 * @{
 */

/**
 * Opens a new connection to a remote address.
 *
 * @todo specify what the address parameter is. Right now
 * it's just the name of a UNIX domain socket. It should be
 * something more complex that encodes which transport to use.
 *
 * If the open fails, the function returns #NULL, and provides
 * a reason for the failure in the result parameter. Pass
 * #NULL for the result parameter if you aren't interested
 * in the reason for failure.
 * 
 * @param address the address.
 * @param result address where a result code can be returned.
 * @returns new connection, or #NULL on failure.
 */
DBusConnection*
dbus_connection_open (const char     *address,
                      DBusResultCode *result)
{
  DBusConnection *connection;
  DBusTransport *transport;
  
  transport = _dbus_transport_open (address, result);
  if (transport == NULL)
    return NULL;
  
  connection = _dbus_connection_new_for_transport (transport);

  _dbus_transport_unref (transport);
  
  if (connection == NULL)
    {
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return NULL;
    }
  
  return connection;
}

/**
 * Increments the reference count of a DBusConnection.
 *
 * @param connection the connection.
 */
void
dbus_connection_ref (DBusConnection *connection)
{
  dbus_mutex_lock (connection->mutex);
  _dbus_assert (connection->refcount > 0);

  connection->refcount += 1;
  dbus_mutex_unlock (connection->mutex);
}

/**
 * Increments the reference count of a DBusConnection.
 * Requires that the caller already holds the connection lock.
 *
 * @param connection the connection.
 */
void
_dbus_connection_ref_unlocked (DBusConnection *connection)
{
  _dbus_assert (connection->refcount > 0);
  connection->refcount += 1;
}


/* This is run without the mutex held, but after the last reference
 * to the connection has been dropped we should have no thread-related
 * problems
 */
static void
_dbus_connection_last_unref (DBusConnection *connection)
{
  DBusHashIter iter;
  DBusList *link;

  /* You have to disconnect the connection before unref:ing it. Otherwise
   * you won't get the disconnected message.
   */
  _dbus_assert (!_dbus_transport_get_is_connected (connection->transport));
  
  if (connection->connection_counter != NULL)
    {
      /* subtract ourselves from the counter */
      _dbus_counter_adjust (connection->connection_counter, - 1);
      _dbus_counter_unref (connection->connection_counter);
      connection->connection_counter = NULL;
    }
  
  _dbus_watch_list_free (connection->watches);
  connection->watches = NULL;
  
  _dbus_timeout_list_free (connection->timeouts);
  connection->timeouts = NULL;

  /* calls out to application code... */
  _dbus_data_slot_list_free (&connection->slot_list);
  
  _dbus_hash_iter_init (connection->handler_table, &iter);
  while (_dbus_hash_iter_next (&iter))
    {
      DBusMessageHandler *h = _dbus_hash_iter_get_value (&iter);
      
      _dbus_message_handler_remove_connection (h, connection);
    }
  
  link = _dbus_list_get_first_link (&connection->filter_list);
  while (link != NULL)
    {
      DBusMessageHandler *h = link->data;
      DBusList *next = _dbus_list_get_next_link (&connection->filter_list, link);
      
      _dbus_message_handler_remove_connection (h, connection);
      
      link = next;
    }

  _dbus_hash_table_unref (connection->handler_table);
  connection->handler_table = NULL;

  _dbus_hash_table_unref (connection->pending_replies);
  connection->pending_replies = NULL;
  
  _dbus_list_clear (&connection->filter_list);
  
  _dbus_list_foreach (&connection->outgoing_messages,
		      (DBusForeachFunction) dbus_message_unref,
		      NULL);
  _dbus_list_clear (&connection->outgoing_messages);
  
  _dbus_list_foreach (&connection->incoming_messages,
		      (DBusForeachFunction) dbus_message_unref,
		      NULL);
  _dbus_list_clear (&connection->incoming_messages);
  
  _dbus_transport_unref (connection->transport);

  if (connection->disconnect_message_link)
    {
      DBusMessage *message = connection->disconnect_message_link->data;
      dbus_message_unref (message);
      _dbus_list_free_link (connection->disconnect_message_link);
    }
  
  dbus_condvar_free (connection->dispatch_cond);
  dbus_condvar_free (connection->io_path_cond);
  dbus_condvar_free (connection->message_returned_cond);
  
  dbus_mutex_free (connection->mutex);
  
  dbus_free (connection);
}

/**
 * Decrements the reference count of a DBusConnection, and finalizes
 * it if the count reaches zero.  It is a bug to drop the last reference
 * to a connection that has not been disconnected.
 *
 * @param connection the connection.
 */
void
dbus_connection_unref (DBusConnection *connection)
{
  dbus_bool_t last_unref;
  
  dbus_mutex_lock (connection->mutex);
  
  _dbus_assert (connection != NULL);
  _dbus_assert (connection->refcount > 0);

  connection->refcount -= 1;
  last_unref = (connection->refcount == 0);

  dbus_mutex_unlock (connection->mutex);

  if (last_unref)
    _dbus_connection_last_unref (connection);
}

/**
 * Closes the connection, so no further data can be sent or received.
 * Any further attempts to send data will result in errors.  This
 * function does not affect the connection's reference count.  It's
 * safe to disconnect a connection more than once; all calls after the
 * first do nothing. It's impossible to "reconnect" a connection, a
 * new connection must be created.
 *
 * @param connection the connection.
 */
void
dbus_connection_disconnect (DBusConnection *connection)
{
  dbus_mutex_lock (connection->mutex);
  _dbus_transport_disconnect (connection->transport);
  dbus_mutex_unlock (connection->mutex);
}

/**
 * Gets whether the connection is currently connected.  All
 * connections are connected when they are opened.  A connection may
 * become disconnected when the remote application closes its end, or
 * exits; a connection may also be disconnected with
 * dbus_connection_disconnect().
 *
 * @param connection the connection.
 * @returns #TRUE if the connection is still alive.
 */
dbus_bool_t
dbus_connection_get_is_connected (DBusConnection *connection)
{
  dbus_bool_t res;
  
  dbus_mutex_lock (connection->mutex);
  res = _dbus_transport_get_is_connected (connection->transport);
  dbus_mutex_unlock (connection->mutex);
  
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
  
  dbus_mutex_lock (connection->mutex);
  res = _dbus_transport_get_is_authenticated (connection->transport);
  dbus_mutex_unlock (connection->mutex);
  
  return res;
}

/**
 * Adds a message to the outgoing message queue. Does not block to
 * write the message to the network; that happens asynchronously. to
 * force the message to be written, call dbus_connection_flush().
 *
 * If the function fails, it returns #FALSE and returns the
 * reason for failure via the result parameter.
 * The result parameter can be #NULL if you aren't interested
 * in the reason for the failure.
 * 
 * @param connection the connection.
 * @param message the message to write.
 * @param client_serial return location for client serial.
 * @param result address where result code can be placed.
 * @returns #TRUE on success.
 */
dbus_bool_t
dbus_connection_send_message (DBusConnection *connection,
                              DBusMessage    *message,
			      dbus_int32_t   *client_serial,
                              DBusResultCode *result)

{
  dbus_int32_t serial;

  dbus_mutex_lock (connection->mutex);

  if (!_dbus_list_prepend (&connection->outgoing_messages,
                           message))
    {
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      dbus_mutex_unlock (connection->mutex);
      return FALSE;
    }

  dbus_message_ref (message);
  connection->n_outgoing += 1;

  _dbus_verbose ("Message %p added to outgoing queue, %d pending to send\n",
                 message, connection->n_outgoing);

  if (_dbus_message_get_client_serial (message) == -1)
    {
      serial = _dbus_connection_get_next_client_serial (connection);
      _dbus_message_set_client_serial (message, serial);
    }
  
  if (client_serial)
    *client_serial = _dbus_message_get_client_serial (message);
  
  _dbus_message_lock (message);

  if (connection->n_outgoing == 1)
    _dbus_transport_messages_pending (connection->transport,
				      connection->n_outgoing);
  
  _dbus_connection_wakeup_mainloop (connection);

  dbus_mutex_unlock (connection->mutex);
  
  return TRUE;
}

static void
reply_handler_timeout (void *data)
{
  DBusConnection *connection;
  ReplyHandlerData *reply_handler_data = data;

  connection = reply_handler_data->connection;
  
  dbus_mutex_lock (connection->mutex);
  if (reply_handler_data->timeout_link)
    {
      _dbus_connection_queue_synthesized_message_link (connection,
						       reply_handler_data->timeout_link);
      reply_handler_data->timeout_link = NULL;
    }

  _dbus_connection_remove_timeout (connection,
				   reply_handler_data->timeout);
  reply_handler_data->timeout_added = FALSE;
  
  dbus_mutex_unlock (connection->mutex);
}

static void
reply_handler_data_free (ReplyHandlerData *data)
{
  if (!data)
    return;

  if (data->timeout_added)
    _dbus_connection_remove_timeout_locked (data->connection,
					    data->timeout);

  if (data->connection_added)
    _dbus_message_handler_remove_connection (data->handler,
					     data->connection);

  if (data->timeout_link)
    {
      dbus_message_unref ((DBusMessage *)data->timeout_link->data);
      _dbus_list_free_link (data->timeout_link);
    }
  
  dbus_message_handler_unref (data->handler);
  
  dbus_free (data);
}

/**
 * Queues a message to send, as with dbus_connection_send_message(),
 * but also sets up a DBusMessageHandler to receive a reply to the
 * message. If no reply is received in the given timeout_milliseconds,
 * expires the pending reply and sends the DBusMessageHandler a
 * synthetic error reply (generated in-process, not by the remote
 * application) indicating that a timeout occurred.
 *
 * Reply handlers see their replies after message filters see them,
 * but before message handlers added with
 * dbus_connection_register_handler() see them, regardless of the
 * reply message's name. Reply handlers are only handed a single
 * message as a reply, after one reply has been seen the handler is
 * removed. If a filter filters out the reply before the handler sees
 * it, the reply is immediately timed out and a timeout error reply is
 * generated. If a filter removes the timeout error reply then the
 * reply handler will never be called. Filters should not do this.
 * 
 * If #NULL is passed for the reply_handler, the timeout reply will
 * still be generated and placed into the message queue, but no
 * specific message handler will receive the reply.
 *
 * If -1 is passed for the timeout, a sane default timeout is used. -1
 * is typically the best value for the timeout for this reason, unless
 * you want a very short or very long timeout.  There is no way to
 * avoid a timeout entirely, other than passing INT_MAX for the
 * timeout to postpone it indefinitely.
 *
 * @todo I think we should rename this function family
 * dbus_connection_send(), send_with_reply(), etc. (i.e.
 * drop the "message" part), the names are too long.
 * 
 * @param connection the connection
 * @param message the message to send
 * @param reply_handler message handler expecting the reply, or #NULL
 * @param timeout_milliseconds timeout in milliseconds or -1 for default
 * @param result return location for result code
 * @returns #TRUE if the message is successfully queued, #FALSE if no memory.
 *
 */
dbus_bool_t
dbus_connection_send_message_with_reply (DBusConnection     *connection,
                                         DBusMessage        *message,
                                         DBusMessageHandler *reply_handler,
                                         int                 timeout_milliseconds,
                                         DBusResultCode     *result)
{
  DBusTimeout *timeout;
  ReplyHandlerData *data;
  DBusMessage *reply;
  DBusList *reply_link;
  dbus_int32_t serial = -1;
  
  if (timeout_milliseconds == -1)
    timeout_milliseconds = DEFAULT_TIMEOUT_VALUE;

  data = dbus_new0 (ReplyHandlerData, 1);

  if (!data)
    {
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return FALSE;
    }
  
  timeout = _dbus_timeout_new (timeout_milliseconds, reply_handler_timeout,
			       data, NULL);

  if (!timeout)
    {
      reply_handler_data_free (data);
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return FALSE;
    }

  dbus_mutex_lock (connection->mutex);
  
  /* Add timeout */
  if (!_dbus_connection_add_timeout (connection, timeout))
    {
      reply_handler_data_free (data);
      _dbus_timeout_unref (timeout);
      dbus_mutex_unlock (connection->mutex);
      
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return FALSE;
    }

  /* The connection now owns the reference to the timeout. */
  _dbus_timeout_unref (timeout);
  
  data->timeout_added = TRUE;
  data->timeout = timeout;
  data->connection = connection;
  
  if (!_dbus_message_handler_add_connection (reply_handler, connection))
    {
      dbus_mutex_unlock (connection->mutex);
      reply_handler_data_free (data);
      
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return FALSE;
    }
  data->connection_added = TRUE;
  
  /* Assign a serial to the message */
  if (_dbus_message_get_client_serial (message) == -1)
    {
      serial = _dbus_connection_get_next_client_serial (connection);
      _dbus_message_set_client_serial (message, serial);
    }

  data->handler = reply_handler;
  data->serial = serial;

  dbus_message_handler_ref (reply_handler);

  reply = dbus_message_new_error_reply (message, DBUS_ERROR_NO_REPLY,
					"No reply within specified time");
  if (!reply)
    {
      dbus_mutex_unlock (connection->mutex);
      reply_handler_data_free (data);
      
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return FALSE;
    }

  reply_link = _dbus_list_alloc_link (reply);
  if (!reply)
    {
      dbus_mutex_unlock (connection->mutex);
      dbus_message_unref (reply);
      reply_handler_data_free (data);
      
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return FALSE;
    }

  data->timeout_link = reply_link;
  
  /* Insert the serial in the pending replies hash. */
  if (!_dbus_hash_table_insert_int (connection->pending_replies, serial, data))
    {
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      dbus_mutex_unlock (connection->mutex);
      reply_handler_data_free (data);
      
      return FALSE;
    }

  dbus_mutex_unlock (connection->mutex);
  
  if (!dbus_connection_send_message (connection, message, NULL, result))
    {
      /* This will free the handler data too */
      _dbus_hash_table_remove_int (connection->pending_replies, serial);
      return FALSE;
    }

  dbus_set_result (result, DBUS_RESULT_SUCCESS);  
  return TRUE;
}

/**
 * Sends a message and blocks a certain time period while waiting for a reply.
 * This function does not dispatch any message handlers until the main loop
 * has been reached. This function is used to do non-reentrant "method calls."
 * If a reply is received, it is returned, and removed from the incoming
 * message queue. If it is not received, #NULL is returned and the
 * result is set to #DBUS_RESULT_NO_REPLY. If something else goes
 * wrong, result is set to whatever is appropriate, such as
 * #DBUS_RESULT_NO_MEMORY.
 *
 * @todo could use performance improvements (it keeps scanning
 * the whole message queue for example) and has thread issues,
 * see comments in source
 *
 * @param connection the connection
 * @param message the message to send
 * @param timeout_milliseconds timeout in milliseconds or -1 for default
 * @param result return location for result code
 * @returns the message that is the reply or #NULL with an error code if the
 * function fails.
 */
DBusMessage *
dbus_connection_send_message_with_reply_and_block (DBusConnection     *connection,
						   DBusMessage        *message,
						   int                 timeout_milliseconds,
						   DBusResultCode     *result)
{
  dbus_int32_t client_serial;
  DBusList *link;
  long start_tv_sec, start_tv_usec;
  long end_tv_sec, end_tv_usec;
  long tv_sec, tv_usec;

  if (timeout_milliseconds == -1)
    timeout_milliseconds = DEFAULT_TIMEOUT_VALUE;

  /* it would probably seem logical to pass in _DBUS_INT_MAX
   * for infinite timeout, but then math below would get
   * all overflow-prone, so smack that down.
   */
  if (timeout_milliseconds > _DBUS_ONE_HOUR_IN_MILLISECONDS * 6)
    timeout_milliseconds = _DBUS_ONE_HOUR_IN_MILLISECONDS * 6;
  
  if (!dbus_connection_send_message (connection, message, &client_serial, result))
    return NULL;

  message = NULL;
  
  /* Flush message queue */
  dbus_connection_flush (connection);

  dbus_mutex_lock (connection->mutex);

  _dbus_get_current_time (&start_tv_sec, &start_tv_usec);
  end_tv_sec = start_tv_sec + timeout_milliseconds / 1000;
  end_tv_usec = start_tv_usec + (timeout_milliseconds % 1000) * 1000;
  end_tv_sec += end_tv_usec / _DBUS_USEC_PER_SECOND;
  end_tv_usec = end_tv_usec % _DBUS_USEC_PER_SECOND;

  _dbus_verbose ("will block %d milliseconds from %ld sec %ld usec to %ld sec %ld usec\n",
                 timeout_milliseconds,
                 start_tv_sec, start_tv_usec,
                 end_tv_sec, end_tv_usec);
  
  /* Now we wait... */
  /* THREAD TODO: This is busted. What if a dispatch_message or pop_message
   * gets the message before we do?
   */
 block_again:  
  
  _dbus_connection_do_iteration (connection,
				 DBUS_ITERATION_DO_READING |
				 DBUS_ITERATION_BLOCK,
				 timeout_milliseconds);

  /* Check if we've gotten a reply */
  link = _dbus_list_get_first_link (&connection->incoming_messages);

  while (link != NULL)
    {
      DBusMessage *reply = link->data;

      if (_dbus_message_get_reply_serial (reply) == client_serial)
	{
	  _dbus_list_remove_link (&connection->incoming_messages, link);
	  dbus_message_ref (reply);

	  if (result)
	    *result = DBUS_RESULT_SUCCESS;
	  
	  dbus_mutex_unlock (connection->mutex);
	  return reply;
	}
      link = _dbus_list_get_next_link (&connection->incoming_messages, link);
    }

  _dbus_get_current_time (&tv_sec, &tv_usec);
  
  if (tv_sec < start_tv_sec)
    ; /* clock set backward, bail out */
  else if (connection->disconnect_message_link == NULL)
    ; /* we're disconnected, bail out */
  else if (tv_sec < end_tv_sec ||
           (tv_sec == end_tv_sec && tv_usec < end_tv_usec))
    {
      timeout_milliseconds = (end_tv_sec - tv_sec) * 1000 +
        (end_tv_usec - tv_usec) / 1000;
      _dbus_verbose ("%d milliseconds remain\n", timeout_milliseconds);
      _dbus_assert (timeout_milliseconds > 0);

      goto block_again; /* not expired yet */
    }

  if (dbus_connection_get_is_connected (connection))
    dbus_set_result (result, DBUS_RESULT_NO_REPLY);
  else
    dbus_set_result (result, DBUS_RESULT_DISCONNECTED);

  dbus_mutex_unlock (connection->mutex);

  return NULL;
}

/**
 * Blocks until the outgoing message queue is empty.
 *
 * @param connection the connection.
 */
void
dbus_connection_flush (DBusConnection *connection)
{
  /* We have to specify DBUS_ITERATION_DO_READING here
   * because otherwise we could have two apps deadlock
   * if they are both doing a flush(), and the kernel
   * buffers fill up.
   */
  
  dbus_mutex_lock (connection->mutex);
  while (connection->n_outgoing > 0)
    _dbus_connection_do_iteration (connection,
                                   DBUS_ITERATION_DO_READING |
                                   DBUS_ITERATION_DO_WRITING |
                                   DBUS_ITERATION_BLOCK,
                                   -1);
  dbus_mutex_unlock (connection->mutex);
}

/**
 * Gets the number of messages in the incoming message queue.
 *
 * @param connection the connection.
 * @returns the number of messages in the queue.
 */
int
dbus_connection_get_n_messages (DBusConnection *connection)
{
  int res;

  dbus_mutex_lock (connection->mutex);
  res = connection->n_incoming;
  dbus_mutex_unlock (connection->mutex);
  return res;
}


/* Call with mutex held. Will drop it while waiting and re-acquire
 * before returning
 */
static void
_dbus_connection_wait_for_borrowed (DBusConnection *connection)
{
  _dbus_assert (connection->message_borrowed != NULL);

  while (connection->message_borrowed != NULL)
    dbus_condvar_wait (connection->message_returned_cond, connection->mutex);
}

/**
 * Returns the first-received message from the incoming message queue,
 * leaving it in the queue. If the queue is empty, returns #NULL.
 * 
 * The caller does not own a reference to the returned message, and must
 * either return it using dbus_connection_return_message or keep it after
 * calling dbus_connection_steal_borrowed_message. No one can get at the
 * message while its borrowed, so return it as quickly as possible and
 * don't keep a reference to it after returning it. If you need to keep
 * the message, make a copy of it.
 *
 * @param connection the connection.
 * @returns next message in the incoming queue.
 */
DBusMessage*
dbus_connection_borrow_message  (DBusConnection *connection)
{
  DBusMessage *message;

  dbus_mutex_lock (connection->mutex);

  if (connection->message_borrowed != NULL)
    _dbus_connection_wait_for_borrowed (connection);
  
  message = _dbus_list_get_first (&connection->incoming_messages);

  if (message) 
    connection->message_borrowed = message;
  
  dbus_mutex_unlock (connection->mutex);
  return message;
}

/**
 * @todo docs
 */
void
dbus_connection_return_message (DBusConnection *connection,
				DBusMessage    *message)
{
  dbus_mutex_lock (connection->mutex);
  
  _dbus_assert (message == connection->message_borrowed);
  
  connection->message_borrowed = NULL;
  dbus_condvar_wake_all (connection->message_returned_cond);
  
  dbus_mutex_unlock (connection->mutex);
}

/**
 * @todo docs
 */
void
dbus_connection_steal_borrowed_message (DBusConnection *connection,
					DBusMessage    *message)
{
  DBusMessage *pop_message;
  
  dbus_mutex_lock (connection->mutex);
 
  _dbus_assert (message == connection->message_borrowed);

  pop_message = _dbus_list_pop_first (&connection->incoming_messages);
  _dbus_assert (message == pop_message);
  
  connection->n_incoming -= 1;
 
  _dbus_verbose ("Incoming message %p stolen from queue, %d incoming\n",
		 message, connection->n_incoming);
 
  connection->message_borrowed = NULL;
  dbus_condvar_wake_all (connection->message_returned_cond);
  
  dbus_mutex_unlock (connection->mutex);
}


/* See dbus_connection_pop_message, but requires the caller to own
 * the lock before calling. May drop the lock while running.
 */
static DBusMessage*
_dbus_connection_pop_message_unlocked (DBusConnection *connection)
{
  if (connection->message_borrowed != NULL)
    _dbus_connection_wait_for_borrowed (connection);
  
  if (connection->n_incoming > 0)
    {
      DBusMessage *message;

      message = _dbus_list_pop_first (&connection->incoming_messages);
      connection->n_incoming -= 1;

      _dbus_verbose ("Incoming message %p removed from queue, %d incoming\n",
                     message, connection->n_incoming);

      return message;
    }
  else
    return NULL;
}


/**
 * Returns the first-received message from the incoming message queue,
 * removing it from the queue. The caller owns a reference to the
 * returned message. If the queue is empty, returns #NULL.
 *
 * @param connection the connection.
 * @returns next message in the incoming queue.
 */
DBusMessage*
dbus_connection_pop_message (DBusConnection *connection)
{
  DBusMessage *message;
  dbus_mutex_lock (connection->mutex);

  message = _dbus_connection_pop_message_unlocked (connection);
  
  dbus_mutex_unlock (connection->mutex);
  
  return message;
}

/**
 * Acquire the dispatcher. This must be done before dispatching
 * messages in order to guarantee the right order of
 * message delivery. May sleep and drop the connection mutex
 * while waiting for the dispatcher.
 *
 * @param connection the connection.
 */
static void
_dbus_connection_acquire_dispatch (DBusConnection *connection)
{
  if (connection->dispatch_acquired)
    dbus_condvar_wait (connection->dispatch_cond, connection->mutex);
  _dbus_assert (!connection->dispatch_acquired);

  connection->dispatch_acquired = TRUE;
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
  _dbus_assert (connection->dispatch_acquired);

  connection->dispatch_acquired = FALSE;
  dbus_condvar_wake_one (connection->dispatch_cond);
}

static void
_dbus_connection_failed_pop (DBusConnection *connection,
			     DBusList *message_link)
{
  _dbus_list_prepend_link (&connection->incoming_messages,
			   message_link);
  connection->n_incoming += 1;
}

/**
 * Pops the first-received message from the current incoming message
 * queue, runs any handlers for it, then unrefs the message.
 *
 * @param connection the connection
 * @returns #TRUE if the queue is not empty after dispatch
 */
dbus_bool_t
dbus_connection_dispatch_message (DBusConnection *connection)
{
  DBusMessageHandler *handler;
  DBusMessage *message;
  DBusList *link, *filter_list_copy, *message_link;
  DBusHandlerResult result;
  ReplyHandlerData *reply_handler_data;
  const char *name;
  dbus_int32_t reply_serial;

  /* Preallocate link so we can put the message back on failure */
  message_link = _dbus_list_alloc_link (NULL);
  if (message_link == NULL)
    return FALSE;
  
  dbus_mutex_lock (connection->mutex);

  /* We need to ref the connection since the callback could potentially
   * drop the last ref to it */
  _dbus_connection_ref_unlocked (connection);

  _dbus_connection_acquire_dispatch (connection);
  
  /* This call may drop the lock during the execution (if waiting for
   * borrowed messages to be returned) but the order of message
   * dispatch if several threads call dispatch_message is still
   * protected by the lock, since only one will get the lock, and that
   * one will finish the message dispatching
   */
  message = _dbus_connection_pop_message_unlocked (connection);
  if (message == NULL)
    {
      _dbus_connection_release_dispatch (connection);
      dbus_mutex_unlock (connection->mutex);
      dbus_connection_unref (connection);
      return FALSE;
    }
  
  message_link->data = message;
  
  result = DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  reply_serial = _dbus_message_get_reply_serial (message);
  reply_handler_data = _dbus_hash_table_lookup_int (connection->pending_replies,
						    reply_serial);
  
  if (!_dbus_list_copy (&connection->filter_list, &filter_list_copy))
    {
      _dbus_connection_release_dispatch (connection);
      dbus_mutex_unlock (connection->mutex);
      _dbus_connection_failed_pop (connection, message_link);
      dbus_connection_unref (connection);
      return FALSE;
    }
  
  _dbus_list_foreach (&filter_list_copy,
		      (DBusForeachFunction)dbus_message_handler_ref,
		      NULL);

  /* We're still protected from dispatch_message reentrancy here
   * since we acquired the dispatcher
   */
  dbus_mutex_unlock (connection->mutex);
  
  link = _dbus_list_get_first_link (&filter_list_copy);
  while (link != NULL)
    {
      DBusMessageHandler *handler = link->data;
      DBusList *next = _dbus_list_get_next_link (&filter_list_copy, link);

      result = _dbus_message_handler_handle_message (handler, connection,
                                                     message);

      if (result == DBUS_HANDLER_RESULT_REMOVE_MESSAGE)
	break;

      link = next;
    }

  _dbus_list_foreach (&filter_list_copy,
		      (DBusForeachFunction)dbus_message_handler_unref,
		      NULL);
  _dbus_list_clear (&filter_list_copy);
  
  dbus_mutex_lock (connection->mutex);

  /* Did a reply we were waiting on get filtered? */
  if (reply_handler_data && result == DBUS_HANDLER_RESULT_REMOVE_MESSAGE)
    {
      /* Queue the timeout immediately! */
      if (reply_handler_data->timeout_link)
	{
	  _dbus_connection_queue_synthesized_message_link (connection,
							   reply_handler_data->timeout_link);
	  reply_handler_data->timeout_link = NULL;
	}
      else
	{
	  /* We already queued the timeout? Then it was filtered! */
	  _dbus_warn ("The timeout error with reply serial %d was filtered, so the reply handler will never be called.\n", reply_serial);
	}
    }
  
  if (result == DBUS_HANDLER_RESULT_REMOVE_MESSAGE)
    goto out;

  if (reply_handler_data)
    {
      dbus_mutex_unlock (connection->mutex);
      result = _dbus_message_handler_handle_message (reply_handler_data->handler,
						     connection, message);
      reply_handler_data_free (reply_handler_data);
      dbus_mutex_lock (connection->mutex);
      goto out;
    }
  
  name = dbus_message_get_name (message);
  if (name != NULL)
    {
      handler = _dbus_hash_table_lookup_string (connection->handler_table,
                                                name);
      if (handler != NULL)
        {
	  /* We're still protected from dispatch_message reentrancy here
	   * since we acquired the dispatcher */
	  dbus_mutex_unlock (connection->mutex);
          result = _dbus_message_handler_handle_message (handler, connection,
                                                         message);
	  dbus_mutex_lock (connection->mutex);
          if (result == DBUS_HANDLER_RESULT_REMOVE_MESSAGE)
            goto out;
        }
    }

 out:
  _dbus_connection_release_dispatch (connection);
  dbus_mutex_unlock (connection->mutex);
  _dbus_list_free_link (message_link);
  dbus_connection_unref (connection);
  dbus_message_unref (message);
  
  return connection->n_incoming > 0;
}

/**
 * Sets the watch functions for the connection. These functions are
 * responsible for making the application's main loop aware of file
 * descriptors that need to be monitored for events, using select() or
 * poll(). When using Qt, typically the DBusAddWatchFunction would
 * create a QSocketNotifier. When using GLib, the DBusAddWatchFunction
 * could call g_io_add_watch(), or could be used as part of a more
 * elaborate GSource.
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
 * occurs, dbus_connection_handle_watch() should be called to
 * notify the connection of the file descriptor's condition.
 *
 * dbus_connection_handle_watch() cannot be called during the
 * DBusAddWatchFunction, as the connection will not be ready to handle
 * that watch yet.
 * 
 * It is not allowed to reference a DBusWatch after it has been passed
 * to remove_function.
 * 
 * @param connection the connection.
 * @param add_function function to begin monitoring a new descriptor.
 * @param remove_function function to stop monitoring a descriptor.
 * @param data data to pass to add_function and remove_function.
 * @param free_data_function function to be called to free the data.
 */
void
dbus_connection_set_watch_functions (DBusConnection              *connection,
                                     DBusAddWatchFunction         add_function,
                                     DBusRemoveWatchFunction      remove_function,
                                     void                        *data,
                                     DBusFreeFunction             free_data_function)
{
  dbus_mutex_lock (connection->mutex);
  /* ref connection for slightly better reentrancy */
  _dbus_connection_ref_unlocked (connection);
  
  _dbus_watch_list_set_functions (connection->watches,
                                  add_function, remove_function,
                                  data, free_data_function);
  
  dbus_mutex_unlock (connection->mutex);
  /* drop our paranoid refcount */
  dbus_connection_unref (connection);
}

/**
 * Sets the timeout functions for the connection. These functions are
 * responsible for making the application's main loop aware of timeouts.
 * When using Qt, typically the DBusAddTimeoutFunction would create a
 * QTimer. When using GLib, the DBusAddTimeoutFunction would call
 * g_timeout_add.
 *
 * The DBusTimeout can be queried for the timer interval using
 * dbus_timeout_get_interval.
 *
 * Once a timeout occurs, dbus_timeout_handle should be called to invoke
 * the timeout's callback.
 *
 * @param connection the connection.
 * @param add_function function to add a timeout.
 * @param remove_function function to remove a timeout.
 * @param data data to pass to add_function and remove_function.
 * @param free_data_function function to be called to free the data.
 */
void
dbus_connection_set_timeout_functions   (DBusConnection            *connection,
					 DBusAddTimeoutFunction     add_function,
					 DBusRemoveTimeoutFunction  remove_function,
					 void                      *data,
					 DBusFreeFunction           free_data_function)
{
  dbus_mutex_lock (connection->mutex);
  /* ref connection for slightly better reentrancy */
  _dbus_connection_ref_unlocked (connection);
  
  _dbus_timeout_list_set_functions (connection->timeouts,
				    add_function, remove_function,
				    data, free_data_function);
  
  dbus_mutex_unlock (connection->mutex);
  /* drop our paranoid refcount */
  dbus_connection_unref (connection);  
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
  
  dbus_mutex_lock (connection->mutex);
  old_data = connection->wakeup_main_data;
  old_free_data = connection->free_wakeup_main_data;

  connection->wakeup_main_function = wakeup_main_function;
  connection->wakeup_main_data = data;
  connection->free_wakeup_main_data = free_data_function;
  
  dbus_mutex_unlock (connection->mutex);

  /* Callback outside the lock */
  if (old_free_data)
    (*old_free_data) (old_data);
}

/**
 * Called to notify the connection when a previously-added watch
 * is ready for reading or writing, or has an exception such
 * as a hangup.
 *
 * @param connection the connection.
 * @param watch the watch.
 * @param condition the current condition of the file descriptors being watched.
 */
void
dbus_connection_handle_watch (DBusConnection              *connection,
                              DBusWatch                   *watch,
                              unsigned int                 condition)
{
  dbus_mutex_lock (connection->mutex);
  _dbus_connection_acquire_io_path (connection, -1);
  _dbus_transport_handle_watch (connection->transport,
				watch, condition);
  _dbus_connection_release_io_path (connection);
  dbus_mutex_unlock (connection->mutex);
}

/**
 * Adds a message filter. Filters are handlers that are run on
 * all incoming messages, prior to the normal handlers
 * registered with dbus_connection_register_handler().
 * Filters are run in the order that they were added.
 * The same handler can be added as a filter more than once, in
 * which case it will be run more than once.
 * Filters added during a filter callback won't be run on the
 * message being processed.
 *
 * @param connection the connection
 * @param handler the handler
 * @returns #TRUE on success, #FALSE if not enough memory.
 */
dbus_bool_t
dbus_connection_add_filter (DBusConnection      *connection,
                            DBusMessageHandler  *handler)
{
  dbus_mutex_lock (connection->mutex);
  if (!_dbus_message_handler_add_connection (handler, connection))
    {
      dbus_mutex_unlock (connection->mutex);
      return FALSE;
    }

  if (!_dbus_list_append (&connection->filter_list,
                          handler))
    {
      _dbus_message_handler_remove_connection (handler, connection);
      dbus_mutex_unlock (connection->mutex);
      return FALSE;
    }

  dbus_mutex_unlock (connection->mutex);
  return TRUE;
}

/**
 * Removes a previously-added message filter. It is a programming
 * error to call this function for a handler that has not
 * been added as a filter. If the given handler was added
 * more than once, only one instance of it will be removed
 * (the most recently-added instance).
 *
 * @param connection the connection
 * @param handler the handler to remove
 *
 */
void
dbus_connection_remove_filter (DBusConnection      *connection,
                               DBusMessageHandler  *handler)
{
  dbus_mutex_lock (connection->mutex);
  if (!_dbus_list_remove_last (&connection->filter_list, handler))
    {
      _dbus_warn ("Tried to remove a DBusConnection filter that had not been added\n");
      dbus_mutex_unlock (connection->mutex);
      return;
    }

  _dbus_message_handler_remove_connection (handler, connection);

  dbus_mutex_unlock (connection->mutex);
}

/**
 * Registers a handler for a list of message names. A single handler
 * can be registered for any number of message names, but each message
 * name can only have one handler at a time. It's not allowed to call
 * this function with the name of a message that already has a
 * handler. If the function returns #FALSE, the handlers were not
 * registered due to lack of memory.
 *
 * @todo the messages_to_handle arg may be more convenient if it's a
 * single string instead of an array. Though right now MessageHandler
 * is sort of designed to say be associated with an entire object with
 * multiple methods, that's why for example the connection only
 * weakrefs it.  So maybe the "manual" API should be different.
 * 
 * @param connection the connection
 * @param handler the handler
 * @param messages_to_handle the messages to handle
 * @param n_messages the number of message names in messages_to_handle
 * @returns #TRUE on success, #FALSE if no memory or another handler already exists
 * 
 **/
dbus_bool_t
dbus_connection_register_handler (DBusConnection     *connection,
                                  DBusMessageHandler *handler,
                                  const char        **messages_to_handle,
                                  int                 n_messages)
{
  int i;

  dbus_mutex_lock (connection->mutex);
  i = 0;
  while (i < n_messages)
    {
      DBusHashIter iter;
      char *key;

      key = _dbus_strdup (messages_to_handle[i]);
      if (key == NULL)
        goto failed;
      
      if (!_dbus_hash_iter_lookup (connection->handler_table,
                                   key, TRUE,
                                   &iter))
        {
          dbus_free (key);
          goto failed;
        }

      if (_dbus_hash_iter_get_value (&iter) != NULL)
        {
          _dbus_warn ("Bug in application: attempted to register a second handler for %s\n",
                      messages_to_handle[i]);
          dbus_free (key); /* won't have replaced the old key with the new one */
          goto failed;
        }

      if (!_dbus_message_handler_add_connection (handler, connection))
        {
          _dbus_hash_iter_remove_entry (&iter);
          /* key has freed on nuking the entry */
          goto failed;
        }
      
      _dbus_hash_iter_set_value (&iter, handler);

      ++i;
    }
  
  dbus_mutex_unlock (connection->mutex);
  return TRUE;
  
 failed:
  /* unregister everything registered so far,
   * so we don't fail partially
   */
  dbus_connection_unregister_handler (connection,
                                      handler,
                                      messages_to_handle,
                                      i);

  dbus_mutex_unlock (connection->mutex);
  return FALSE;
}

/**
 * Unregisters a handler for a list of message names. The handlers
 * must have been previously registered.
 *
 * @param connection the connection
 * @param handler the handler
 * @param messages_to_handle the messages to handle
 * @param n_messages the number of message names in messages_to_handle
 * 
 **/
void
dbus_connection_unregister_handler (DBusConnection     *connection,
                                    DBusMessageHandler *handler,
                                    const char        **messages_to_handle,
                                    int                 n_messages)
{
  int i;

  dbus_mutex_lock (connection->mutex);
  i = 0;
  while (i < n_messages)
    {
      DBusHashIter iter;

      if (!_dbus_hash_iter_lookup (connection->handler_table,
                                   (char*) messages_to_handle[i], FALSE,
                                   &iter))
        {
          _dbus_warn ("Bug in application: attempted to unregister handler for %s which was not registered\n",
                      messages_to_handle[i]);
        }
      else if (_dbus_hash_iter_get_value (&iter) != handler)
        {
          _dbus_warn ("Bug in application: attempted to unregister handler for %s which was registered by a different handler\n",
                      messages_to_handle[i]);
        }
      else
        {
          _dbus_hash_iter_remove_entry (&iter);
          _dbus_message_handler_remove_connection (handler, connection);
        }

      ++i;
    }

  dbus_mutex_unlock (connection->mutex);
}

static DBusDataSlotAllocator slot_allocator;

/**
 * Initialize the mutex used for #DBusConnection data
 * slot reservations.
 *
 * @returns the mutex
 */
DBusMutex *
_dbus_connection_slots_init_lock (void)
{
  if (!_dbus_data_slot_allocator_init (&slot_allocator))
    return NULL;
  else
    return slot_allocator.lock;
}

/**
 * Allocates an integer ID to be used for storing application-specific
 * data on any DBusConnection. The allocated ID may then be used
 * with dbus_connection_set_data() and dbus_connection_get_data().
 * If allocation fails, -1 is returned. Again, the allocated
 * slot is global, i.e. all DBusConnection objects will
 * have a slot with the given integer ID reserved.
 *
 * @returns -1 on failure, otherwise the data slot ID
 */
int
dbus_connection_allocate_data_slot (void)
{
  return _dbus_data_slot_allocator_alloc (&slot_allocator);
}

/**
 * Deallocates a global ID for connection data slots.
 * dbus_connection_get_data() and dbus_connection_set_data()
 * may no longer be used with this slot.
 * Existing data stored on existing DBusConnection objects
 * will be freed when the connection is finalized,
 * but may not be retrieved (and may only be replaced
 * if someone else reallocates the slot).
 *
 * @param slot the slot to deallocate
 */
void
dbus_connection_free_data_slot (int slot)
{
  _dbus_data_slot_allocator_free (&slot_allocator, slot);
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
                          int               slot,
                          void             *data,
                          DBusFreeFunction  free_data_func)
{
  DBusFreeFunction old_free_func;
  void *old_data;
  dbus_bool_t retval;
  
  dbus_mutex_lock (connection->mutex);

  retval = _dbus_data_slot_list_set (&slot_allocator,
                                     &connection->slot_list,
                                     slot, data, free_data_func,
                                     &old_free_func, &old_data);
  
  dbus_mutex_unlock (connection->mutex);

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
                          int               slot)
{
  void *res;
  
  dbus_mutex_lock (connection->mutex);

  res = _dbus_data_slot_list_get (&slot_allocator,
                                  &connection->slot_list,
                                  slot);
  
  dbus_mutex_unlock (connection->mutex);

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
  _dbus_modify_sigpipe = will_modify_sigpipe;
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
  dbus_mutex_lock (connection->mutex);
  _dbus_transport_set_max_message_size (connection->transport,
                                        size);
  dbus_mutex_unlock (connection->mutex);
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
  dbus_mutex_lock (connection->mutex);
  res = _dbus_transport_get_max_message_size (connection->transport);
  dbus_mutex_unlock (connection->mutex);
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
 * @param connection the connection
 * @param size the maximum size in bytes of all outstanding messages
 */
void
dbus_connection_set_max_live_messages_size (DBusConnection *connection,
                                            long            size)
{
  dbus_mutex_lock (connection->mutex);
  _dbus_transport_set_max_live_messages_size (connection->transport,
                                              size);
  dbus_mutex_unlock (connection->mutex);
}

/**
 * Gets the value set by dbus_connection_set_max_live_messages_size().
 *
 * @param connection the connection
 * @returns the max size of all live messages
 */
long
dbus_connection_get_max_live_messages_size (DBusConnection *connection)
{
  long res;
  dbus_mutex_lock (connection->mutex);
  res = _dbus_transport_get_max_live_messages_size (connection->transport);
  dbus_mutex_unlock (connection->mutex);
  return res;
}

/** @} */
