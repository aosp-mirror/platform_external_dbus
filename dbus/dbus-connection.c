/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-connection.c DBusConnection object
 *
 * Copyright (C) 2002  Red Hat Inc.
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
#include "dbus-transport.h"
#include "dbus-watch.h"
#include "dbus-connection-internal.h"
#include "dbus-list.h"
#include "dbus-hash.h"
#include "dbus-message-internal.h"

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
  
  DBusConnectionErrorFunction error_function; /**< Callback for errors. */
  void *error_data;                           /**< Data for error callback. */
  DBusFreeFunction error_free_data_function;  /**< Free function for error callback data. */
  DBusHashTable *handler_table; /**< Table of registered DBusMessageHandler */
  DBusList *filter_list;        /**< List of filters. */
  int filters_serial;           /**< Increments when the list of filters is changed. */
  int handlers_serial;          /**< Increments when the handler table is changed. */
};

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
  if (!_dbus_list_append (&connection->incoming_messages,
                          message))
    return FALSE;

  dbus_message_ref (message);
  connection->n_incoming += 1;

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
 * Gets the next outgoing message. The message remanins in the
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
  _dbus_assert (message == _dbus_list_get_last (&connection->outgoing_messages));
  _dbus_list_pop_last (&connection->outgoing_messages);
  dbus_message_unref (message);
  
  connection->n_outgoing -= 1;
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
  return _dbus_watch_list_add_watch (connection->watches,
                                     watch);
  
  return TRUE;
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
  _dbus_watch_list_remove_watch (connection->watches,
                                 watch);
}

static void
handle_error (DBusConnection *connection,
              DBusResultCode  result)
{
  if (result != DBUS_RESULT_SUCCESS &&
      connection->error_function != NULL)
    {
      dbus_connection_ref (connection);
      (* connection->error_function) (connection, result,
                                      connection->error_data);
      dbus_connection_unref (connection);
    }
}

static void
set_result_handled (DBusConnection *connection,
                    DBusResultCode *result_address,
                    DBusResultCode  result)
{
  dbus_set_result (result_address, result);
  handle_error (connection, result);
}

/**
 * Reports a transport error to the connection. Typically
 * results in an application error callback being invoked.
 *
 * @param connection the connection.
 * @param result_code the error code.
 */
void
_dbus_connection_transport_error (DBusConnection *connection,
                                  DBusResultCode  result_code)
{
  handle_error (connection, result_code);
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
  DBusHashTable *handler_table;
  
  watch_list = NULL;
  connection = NULL;
  handler_table = NULL;
  
  watch_list = _dbus_watch_list_new ();
  if (watch_list == NULL)
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
  connection->handler_table = handler_table;
  connection->filter_list = NULL;
  
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

      /* free error data as a side effect */
      dbus_connection_set_error_function (connection,
                                          NULL, NULL, NULL);

      _dbus_watch_list_free (connection->watches);

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
 * @param result address where result code can be placed.
 * @returns #TRUE on success.
 */
dbus_bool_t
dbus_connection_send_message (DBusConnection *connection,
                              DBusMessage    *message,
                              DBusResultCode *result)
{  
  if (!_dbus_list_prepend (&connection->outgoing_messages,
                           message))
    {
      set_result_handled (connection, result, DBUS_RESULT_NO_MEMORY);
      return FALSE;
    }

  dbus_message_ref (message);
  connection->n_outgoing += 1;

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
 * @todo implement non-reentrant "block for reply" variant.  i.e. send
 * a message, block until we get a reply, then pull reply out of
 * message queue and return it, *without dispatching any handlers for
 * any other messages* - used for non-reentrant "method calls" We can
 * block properly for this using _dbus_connection_do_iteration().
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
  return dbus_connection_send_message (connection, message, result);
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
      connection->n_incoming -= 1;
      return _dbus_list_pop_first (&connection->incoming_messages);
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
    return FALSE;

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
 * Sets the error handler function for the connection.
 * 
 * @param connection the connection.
 * @param error_function the error handler.
 * @param data data to pass to the error handler.
 * @param free_data_function function to be called to free the data.
 */
void
dbus_connection_set_error_function  (DBusConnection              *connection,
                                     DBusConnectionErrorFunction  error_function,
                                     void                        *data,
                                     DBusFreeFunction             free_data_function)
{
  if (connection->error_free_data_function != NULL)
    (* connection->error_free_data_function) (connection->error_data);

  connection->error_function = error_function;
  connection->error_data = data;
  connection->error_free_data_function = free_data_function;
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

/** @} */
