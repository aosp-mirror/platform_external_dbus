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
#include "dbus-threads.h"

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

/** Opaque typedef for DBusDataSlot */
typedef struct DBusDataSlot DBusDataSlot;
/** DBusDataSlot is used to store application data on the connection */
struct DBusDataSlot
{
  void *data;                      /**< The application data */
  DBusFreeFunction free_data_func; /**< Free the application data */
};

/**
 * Implementation details of DBusConnection. All fields are private.
 */
struct DBusConnection
{
  int refcount; /**< Reference count. */

  DBusList *outgoing_messages; /**< Queue of messages we need to send, send the end of the list first. */
  DBusList *incoming_messages; /**< Queue of messages we have received, end of the list received most recently. */

  int n_outgoing;              /**< Length of outgoing queue. */
  int n_incoming;              /**< Length of incoming queue. */
  
  DBusTransport *transport;    /**< Object that sends/receives messages over network. */
  DBusWatchList *watches;      /**< Stores active watches. */
  DBusTimeoutList *timeouts;   /**< Stores active timeouts. */
  
  DBusDisconnectFunction disconnect_function;      /**< Callback on disconnect. */
  void *disconnect_data;                           /**< Data for disconnect callback. */
  DBusFreeFunction disconnect_free_data_function;  /**< Free function for disconnect callback data. */
  DBusHashTable *handler_table; /**< Table of registered DBusMessageHandler */
  DBusList *filter_list;        /**< List of filters. */
  int filters_serial;           /**< Increments when the list of filters is changed. */
  int handlers_serial;          /**< Increments when the handler table is changed. */
  DBusDataSlot *data_slots;        /**< Data slots */
  int           n_slots; /**< Slots allocated so far. */

  DBusCounter *connection_counter; /**< Counter that we decrement when finalized */
  
  int client_serial;            /**< Client serial. Increments each time a message is sent  */
  unsigned int disconnect_notified : 1; /**< Already called disconnect_function */
};

static void _dbus_connection_free_data_slots (DBusConnection *connection);

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
  _dbus_assert (_dbus_transport_get_is_authenticated (connection->transport));
  
  if (!_dbus_list_append (&connection->incoming_messages,
                          message))
    return FALSE;
  
  dbus_message_ref (message);
  connection->n_incoming += 1;

  _dbus_verbose ("Incoming message %p added to queue, %d incoming\n",
                 message, connection->n_incoming);
  
  return TRUE;
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
 * Tells the connection that the transport has been disconnected.
 * Results in calling the application disconnect callback.
 * Only has an effect the first time it's called.
 *
 * @param connection the connection
 */
void
_dbus_connection_notify_disconnected (DBusConnection *connection)
{
  if (connection->disconnect_function != NULL &&
      !connection->disconnect_notified)
    {
      connection->disconnect_notified = TRUE;
      dbus_connection_ref (connection);
      (* connection->disconnect_function) (connection,
                                           connection->disconnect_data);
      dbus_connection_unref (connection);
    }
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
  
  _dbus_transport_do_iteration (connection->transport,
                                flags, timeout_milliseconds);
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
  DBusHashTable *handler_table;
  
  watch_list = NULL;
  connection = NULL;
  handler_table = NULL;
  
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
  
  connection = dbus_new0 (DBusConnection, 1);
  if (connection == NULL)
    goto error;
  
  connection->refcount = 1;
  connection->transport = transport;
  connection->watches = watch_list;
  connection->timeouts = timeout_list;
  connection->handler_table = handler_table;
  connection->filter_list = NULL;

  connection->data_slots = NULL;
  connection->n_slots = 0;
  connection->client_serial = 1;
  connection->disconnect_notified = FALSE;
  
  _dbus_transport_ref (transport);
  _dbus_transport_set_connection (transport, connection);
  
  return connection;
  
 error:

  if (connection != NULL)
    dbus_free (connection);

  if (handler_table)
    _dbus_hash_table_unref (handler_table);
  
  if (watch_list)
    _dbus_watch_list_free (watch_list);
  
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
 * to the handler.
 *
 * @param connection the connection
 * @param handler the handler
 */
void
_dbus_connection_handler_destroyed (DBusConnection     *connection,
                                    DBusMessageHandler *handler)
{
  DBusHashIter iter;
  DBusList *link;

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
  connection->refcount += 1;
}

/**
 * Decrements the reference count of a DBusConnection, and finalizes
 * it if the count reaches zero.  If a connection is still connected
 * when it's finalized, it will be disconnected (that is, associated
 * file handles will be closed).
 *
 * @param connection the connection.
 */
void
dbus_connection_unref (DBusConnection *connection)
{
  _dbus_assert (connection != NULL);
  _dbus_assert (connection->refcount > 0);
  
  connection->refcount -= 1;
  if (connection->refcount == 0)
    {
      DBusHashIter iter;
      DBusList *link;

      dbus_connection_disconnect (connection);
      
      /* free disconnect data as a side effect */
      dbus_connection_set_disconnect_function (connection,
                                               NULL, NULL, NULL);

      if (connection->connection_counter != NULL)
        {
          /* subtract ourselves from the counter */
          _dbus_counter_adjust (connection->connection_counter, - 1);
          _dbus_counter_unref (connection->connection_counter);
          connection->connection_counter = NULL;
        }
      
      _dbus_watch_list_free (connection->watches);
      connection->watches = NULL;

      _dbus_connection_free_data_slots (connection);
      
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
      
      dbus_free (connection);
    }
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
  _dbus_transport_disconnect (connection->transport);
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
  return _dbus_transport_get_is_connected (connection->transport);
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
  return _dbus_transport_get_is_authenticated (connection->transport);
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
  
  if (!_dbus_list_prepend (&connection->outgoing_messages,
                           message))
    {
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return FALSE;
    }

  dbus_message_ref (message);
  connection->n_outgoing += 1;

  _dbus_verbose ("Message %p added to outgoing queue, %d pending to send\n",
                 message, connection->n_outgoing);

  /* Unlock the message, resetting its header. */
  _dbus_message_unlock (message);
  
  serial = _dbus_connection_get_next_client_serial (connection);
  _dbus_message_set_client_serial (message, serial);

  if (client_serial)
    *client_serial = serial;
  
  _dbus_message_lock (message);
  
  if (connection->n_outgoing == 1)
    _dbus_transport_messages_pending (connection->transport,
                                      connection->n_outgoing);

  return TRUE;
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
 * message as a reply, after a reply has been seen the handler is
 * removed. If a filter filters out the reply before the handler sees
 * it, the handler is not removed but the timeout will immediately
 * fire again. If a filter was dumb and kept removing the timeout
 * reply then we'd get in an infinite loop.
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
 * @param connection the connection
 * @param message the message to send
 * @param reply_handler message handler expecting the reply, or #NULL
 * @param timeout_milliseconds timeout in milliseconds or -1 for default
 * @param result return location for result code
 * @returns #TRUE if the message is successfully queued, #FALSE if no memory.
 *
 * @todo this function isn't implemented because we need message serials
 * and other slightly more rich DBusMessage implementation in order to
 * implement it. The basic idea will be to keep a hash of serials we're
 * expecting a reply to, and also to add a way to tell GLib or Qt to
 * install a timeout. Then install a timeout which is the shortest
 * timeout of any pending reply.
 *
 */
dbus_bool_t
dbus_connection_send_message_with_reply (DBusConnection     *connection,
                                         DBusMessage        *message,
                                         DBusMessageHandler *reply_handler,
                                         int                 timeout_milliseconds,
                                         DBusResultCode     *result)
{
  /* FIXME */
  return dbus_connection_send_message (connection, message, NULL, result);
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
 * @todo I believe if we get EINTR or otherwise interrupt the
 * do_iteration call in here, we won't block the required length of
 * time. I think there probably has to be a loop: "while (!timeout_elapsed)
 * { check_for_reply_in_queue(); iterate_with_remaining_timeout(); }"
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

  if (timeout_milliseconds == -1)
    timeout_milliseconds = DEFAULT_TIMEOUT_VALUE;
  
  if (!dbus_connection_send_message (connection, message, &client_serial, result))
    return NULL;

  /* Flush message queue */
  dbus_connection_flush (connection);
  
  /* Now we wait... */
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
	  _dbus_list_remove (&connection->incoming_messages, link);
	  dbus_message_ref (message);

	  if (result)
	    *result = DBUS_RESULT_SUCCESS;
	  
	  return reply;
	}
      link = _dbus_list_get_next_link (&connection->incoming_messages, link);
    }

  if (result)
    *result = DBUS_RESULT_NO_REPLY;
  
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
  while (connection->n_outgoing > 0)
    _dbus_connection_do_iteration (connection,
                                   DBUS_ITERATION_DO_WRITING |
                                   DBUS_ITERATION_BLOCK,
                                   -1);
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
  return connection->n_incoming;
}

/**
 * Returns the first-received message from the incoming message queue,
 * leaving it in the queue. The caller does not own a reference to the
 * returned message. If the queue is empty, returns #NULL.
 *
 * @param connection the connection.
 * @returns next message in the incoming queue.
 */
DBusMessage*
dbus_connection_peek_message  (DBusConnection *connection)
{
  return _dbus_list_get_first (&connection->incoming_messages);
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
 * Pops the first-received message from the current incoming message
 * queue, runs any handlers for it, then unrefs the message.
 *
 * @param connection the connection
 * @returns #TRUE if the queue is not empty after dispatch
 *
 * @todo this function is not properly robust against reentrancy,
 * that is, if handlers are added/removed while dispatching
 * a message, things will get messed up.
 */
dbus_bool_t
dbus_connection_dispatch_message (DBusConnection *connection)
{
  DBusMessage *message;
  int filter_serial;
  int handler_serial;
  DBusList *link;
  DBusHandlerResult result;
  const char *name;
  
  dbus_connection_ref (connection);
  
  message = dbus_connection_pop_message (connection);
  if (message == NULL)
    {
      dbus_connection_unref (connection);
      return FALSE;
    }

  filter_serial = connection->filters_serial;
  handler_serial = connection->handlers_serial;

  result = DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
  
  link = _dbus_list_get_first_link (&connection->filter_list);
  while (link != NULL)
    {
      DBusMessageHandler *handler = link->data;
      DBusList *next = _dbus_list_get_next_link (&connection->filter_list, link);
      
      result = _dbus_message_handler_handle_message (handler, connection,
                                                     message);

      if (result == DBUS_HANDLER_RESULT_REMOVE_MESSAGE)
        goto out;

      if (filter_serial != connection->filters_serial)
        {
          _dbus_warn ("Message filters added or removed while dispatching filters - not currently supported!\n");
          goto out;
        }
      
      link = next;
    }

  name = dbus_message_get_name (message);
  if (name != NULL)
    {
      DBusMessageHandler *handler;
      
      handler = _dbus_hash_table_lookup_string (connection->handler_table,
                                                name);
      if (handler != NULL)
        {

          result = _dbus_message_handler_handle_message (handler, connection,
                                                         message);
      
          if (result == DBUS_HANDLER_RESULT_REMOVE_MESSAGE)
            goto out;
          
          if (handler_serial != connection->handlers_serial)
            {
              _dbus_warn ("Message handlers added or removed while dispatching handlers - not currently supported!\n");
              goto out;
            }
        }
    }

 out:
  dbus_connection_unref (connection);
  dbus_message_unref (message);
  
  return connection->n_incoming > 0;
}

/**
 * Sets the disconnect handler function for the connection.
 * Will be called exactly once, when the connection is
 * disconnected.
 * 
 * @param connection the connection.
 * @param disconnect_function the disconnect handler.
 * @param data data to pass to the disconnect handler.
 * @param free_data_function function to be called to free the data.
 */
void
dbus_connection_set_disconnect_function  (DBusConnection              *connection,
                                          DBusDisconnectFunction       disconnect_function,
                                          void                        *data,
                                          DBusFreeFunction             free_data_function)
{
  if (connection->disconnect_free_data_function != NULL)
    (* connection->disconnect_free_data_function) (connection->disconnect_data);

  connection->disconnect_function = disconnect_function;
  connection->disconnect_data = data;
  connection->disconnect_free_data_function = free_data_function;
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
  /* ref connection for slightly better reentrancy */
  dbus_connection_ref (connection);
  
  _dbus_watch_list_set_functions (connection->watches,
                                  add_function, remove_function,
                                  data, free_data_function);
  
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
 * Once a timeout occurs, dbus_timeout_handle should be call to invoke
 * the timeout's callback.
 */
void
dbus_connection_set_timeout_functions   (DBusConnection            *connection,
					 DBusAddTimeoutFunction     add_function,
					 DBusRemoveTimeoutFunction  remove_function,
					 void                      *data,
					 DBusFreeFunction           free_data_function)
{
  /* ref connection for slightly better reentrancy */
  dbus_connection_ref (connection);
  
  _dbus_timeout_list_set_functions (connection->timeouts,
				    add_function, remove_function,
				    data, free_data_function);
  
  /* drop our paranoid refcount */
  dbus_connection_unref (connection);  
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
  _dbus_transport_handle_watch (connection->transport,
                                watch, condition);
}

/**
 * Adds a message filter. Filters are handlers that are run on
 * all incoming messages, prior to the normal handlers
 * registered with dbus_connection_register_handler().
 * Filters are run in the order that they were added.
 * The same handler can be added as a filter more than once, in
 * which case it will be run more than once.
 *
 * @param connection the connection
 * @param handler the handler
 * @returns #TRUE on success, #FALSE if not enough memory.
 */
dbus_bool_t
dbus_connection_add_filter (DBusConnection      *connection,
                            DBusMessageHandler  *handler)
{
  if (!_dbus_message_handler_add_connection (handler, connection))
    return FALSE;

  if (!_dbus_list_append (&connection->filter_list,
                          handler))
    {
      _dbus_message_handler_remove_connection (handler, connection);
      return FALSE;
    }

  connection->filters_serial += 1;
  
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
  if (!_dbus_list_remove_last (&connection->filter_list, handler))
    {
      _dbus_warn ("Tried to remove a DBusConnection filter that had not been added\n");
      return;
    }

  _dbus_message_handler_remove_connection (handler, connection);

  connection->filters_serial += 1;
}

/**
 * Registers a handler for a list of message names. A single handler
 * can be registered for any number of message names, but each message
 * name can only have one handler at a time. It's not allowed to call
 * this function with the name of a message that already has a
 * handler. If the function returns #FALSE, the handlers were not
 * registered due to lack of memory.
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

      connection->handlers_serial += 1;
      
      ++i;
    }
  
  return TRUE;
  
 failed:
  /* unregister everything registered so far,
   * so we don't fail partially
   */
  dbus_connection_unregister_handler (connection,
                                      handler,
                                      messages_to_handle,
                                      i);

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

  connection->handlers_serial += 1;
}

static int *allocated_slots = NULL;
static int  n_allocated_slots = 0;
static int  n_used_slots = 0;
static DBusStaticMutex allocated_slots_lock = DBUS_STATIC_MUTEX_INIT;

/**
 * Allocates an integer ID to be used for storing application-specific
 * data on any DBusConnection. The allocated ID may then be used
 * with dbus_connection_set_data() and dbus_connection_get_data().
 * If allocation fails, -1 is returned.
 *
 * @returns -1 on failure, otherwise the data slot ID
 */
int
dbus_connection_allocate_data_slot (void)
{
  int slot;
  
  if (!dbus_static_mutex_lock (&allocated_slots_lock))
    return -1;

  if (n_used_slots < n_allocated_slots)
    {
      slot = 0;
      while (slot < n_allocated_slots)
        {
          if (allocated_slots[slot] < 0)
            {
              allocated_slots[slot] = slot;
              n_used_slots += 1;
              break;
            }
          ++slot;
        }

      _dbus_assert (slot < n_allocated_slots);
    }
  else
    {
      int *tmp;
      
      slot = -1;
      tmp = dbus_realloc (allocated_slots,
                          sizeof (int) * (n_allocated_slots + 1));
      if (tmp == NULL)
        goto out;

      allocated_slots = tmp;
      slot = n_allocated_slots;
      n_allocated_slots += 1;
      n_used_slots += 1;
      allocated_slots[slot] = slot;
    }

  _dbus_assert (slot >= 0);
  _dbus_assert (slot < n_allocated_slots);
  
 out:
  dbus_static_mutex_unlock (&allocated_slots_lock);
  return slot;
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
  dbus_static_mutex_lock (&allocated_slots_lock);

  _dbus_assert (slot < n_allocated_slots);
  _dbus_assert (allocated_slots[slot] == slot);
  
  allocated_slots[slot] = -1;
  n_used_slots -= 1;

  if (n_used_slots == 0)
    {
      dbus_free (allocated_slots);
      allocated_slots = NULL;
      n_allocated_slots = 0;
    }
  
  dbus_static_mutex_unlock (&allocated_slots_lock);
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
  _dbus_assert (slot < n_allocated_slots);
  _dbus_assert (allocated_slots[slot] == slot);
  
  if (slot >= connection->n_slots)
    {
      DBusDataSlot *tmp;
      int i;
      
      tmp = dbus_realloc (connection->data_slots,
                          sizeof (DBusDataSlot) * (slot + 1));
      if (tmp == NULL)
        return FALSE;
      
      connection->data_slots = tmp;
      i = connection->n_slots;
      connection->n_slots = slot + 1;
      while (i < connection->n_slots)
        {
          connection->data_slots[i].data = NULL;
          connection->data_slots[i].free_data_func = NULL;
          ++i;
        }
    }

  _dbus_assert (slot < connection->n_slots);
  
  if (connection->data_slots[slot].free_data_func)
    (* connection->data_slots[slot].free_data_func) (connection->data_slots[slot].data);

  connection->data_slots[slot].data = data;
  connection->data_slots[slot].free_data_func = free_data_func;

  return TRUE;
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
  _dbus_assert (slot < n_allocated_slots);
  _dbus_assert (allocated_slots[slot] == slot);

  if (slot >= connection->n_slots)
    return NULL;

  return connection->data_slots[slot].data;
}

static void
_dbus_connection_free_data_slots (DBusConnection *connection)
{
  int i;

  i = 0;
  while (i < connection->n_slots)
    {
      if (connection->data_slots[i].free_data_func)
        (* connection->data_slots[i].free_data_func) (connection->data_slots[i].data);
      connection->data_slots[i].data = NULL;
      connection->data_slots[i].free_data_func = NULL;
      ++i;
    }

  dbus_free (connection->data_slots);
  connection->data_slots = NULL;
  connection->n_slots = 0;
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
  _dbus_transport_set_max_message_size (connection->transport,
                                        size);
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
  return _dbus_transport_get_max_message_size (connection->transport);
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
  _dbus_transport_set_max_live_messages_size (connection->transport,
                                              size);
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
  return _dbus_transport_get_max_live_messages_size (connection->transport);
}

/** @} */
