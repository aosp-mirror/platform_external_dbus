/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-transport-debug.c In-proc debug subclass of DBusTransport
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

#include "dbus-internals.h"
#include "dbus-connection-internal.h"
#include "dbus-transport-protected.h"
#include "dbus-transport-debug.h"
#include "dbus-server-debug.h"
#include "dbus-list.h"

#ifdef DBUS_BUILD_TESTS

/**
 * @defgroup DBusTransportDebug DBusTransportDebug
 * @ingroup  DBusInternals
 * @brief In-process debug transport used in unit tests.
 *
 * Types and functions related to DBusTransportDebug.
 * This is used for unit testing.
 *
 * @{
 */

/**
 * Default timeout interval when reading or writing.
 */
#define DEFAULT_INTERVAL 10

/**
 * Opaque object representing a debug transport.
 *
 */
typedef struct DBusTransportDebug DBusTransportDebug;

/**
 * Implementation details of DBusTransportDebug. All members are private.
 */
struct DBusTransportDebug
{
  DBusTransport base;                   /**< Parent instance */

  DBusTimeout *write_timeout;           /**< Timeout for reading. */
  DBusTimeout *read_timeout;            /**< Timeout for writing. */
  
  DBusTransport *other_end;             /**< The transport that this transport is connected to. */
};

static void
debug_finalize (DBusTransport *transport)
{
  _dbus_transport_finalize_base (transport);  

  dbus_free (transport);
}

static void
do_reading (DBusTransport *transport)
{
  DBusTransportDebug *debug_transport = (DBusTransportDebug*) transport;
  
  if (transport->disconnected)
    return;

  /* Now dispatch the messages */
  if (dbus_connection_dispatch_message (transport->connection))
    {
      debug_transport->read_timeout =
	_dbus_timeout_new (DEFAULT_INTERVAL, (DBusTimeoutHandler)do_reading,
			   transport, NULL);
      if (!_dbus_connection_add_timeout (transport->connection,
					 debug_transport->read_timeout))
	{
	  _dbus_timeout_unref (debug_transport->read_timeout);
	  debug_transport->read_timeout = NULL;
	}
    }
}

static void
check_read_timeout (DBusTransport *transport)
{
  DBusTransportDebug *debug_transport = (DBusTransportDebug*) transport;
  dbus_bool_t need_read_timeout;

  if (transport->connection == NULL)
    return;

  _dbus_transport_ref (transport);
  
  need_read_timeout = dbus_connection_get_n_messages (transport->connection) > 0;
  
  if (transport->disconnected)
    need_read_timeout = FALSE;
  
  if (need_read_timeout &&
      debug_transport->read_timeout == NULL)
    {
      debug_transport->read_timeout =
	_dbus_timeout_new (DEFAULT_INTERVAL, (DBusTimeoutHandler)do_reading,
			   transport, NULL);

      if (debug_transport->read_timeout == NULL)
	goto out;

      if (!_dbus_connection_add_timeout (transport->connection,
					 debug_transport->read_timeout))
	{
	  _dbus_timeout_unref (debug_transport->read_timeout);
	  debug_transport->read_timeout = NULL;

	  goto out;
	}
    }
  else if (!need_read_timeout &&
	   debug_transport->read_timeout != NULL)
    {
      _dbus_connection_remove_timeout (transport->connection,
				       debug_transport->read_timeout);
      _dbus_timeout_unref (debug_transport->read_timeout);
      debug_transport->read_timeout = NULL;
    }

 out:
  _dbus_transport_unref (transport);      
}

static void
do_writing (DBusTransport *transport)
{
  if (transport->disconnected)
    return;

  while (!transport->disconnected &&
	 _dbus_connection_have_messages_to_send (transport->connection))
    {
      DBusMessage *message, *copy;
      
      message = _dbus_connection_get_message_to_send (transport->connection);
      _dbus_message_lock (message);
      
      copy = dbus_message_new_from_message (message);
      
      _dbus_connection_message_sent (transport->connection,
				     message);
      
      _dbus_connection_queue_received_message (((DBusTransportDebug *)transport)->other_end->connection,
                                               copy);
      dbus_message_unref (copy);
    }

  check_read_timeout (((DBusTransportDebug *)transport)->other_end);
}

static void
check_write_timeout (DBusTransport *transport)
{
  DBusTransportDebug *debug_transport = (DBusTransportDebug *)transport;
  dbus_bool_t need_write_timeout;
  
  if (transport->connection == NULL)
    return;

  _dbus_transport_ref (transport);

  need_write_timeout = transport->messages_need_sending;
  
  if (transport->disconnected)
    need_write_timeout = FALSE;

  if (need_write_timeout &&
      debug_transport->write_timeout == NULL)
    {
      debug_transport->write_timeout =
	_dbus_timeout_new (DEFAULT_INTERVAL, (DBusTimeoutHandler)do_writing,
			   transport, NULL);

      if (debug_transport->write_timeout == NULL)
	goto out;

      if (!_dbus_connection_add_timeout (transport->connection,
					 debug_transport->write_timeout))
	{
	  _dbus_timeout_unref (debug_transport->write_timeout);
	  debug_transport->write_timeout = NULL;
	}
    }
  else if (!need_write_timeout &&
	   debug_transport->write_timeout != NULL)
    {
      _dbus_connection_remove_timeout (transport->connection,
				       debug_transport->write_timeout);
      _dbus_timeout_unref (debug_transport->write_timeout);
      debug_transport->write_timeout = NULL;
    }

 out:
  _dbus_transport_unref (transport);
}

static void
debug_handle_watch (DBusTransport *transport,
		    DBusWatch     *watch,
		    unsigned int   flags)
{
}

static void
debug_disconnect (DBusTransport *transport)
{
}

static void
debug_connection_set (DBusTransport *transport)
{
}

static void
debug_messages_pending (DBusTransport *transport,
			int            messages_pending)
{
  check_write_timeout (transport);
}

static void
debug_do_iteration (DBusTransport *transport,
		    unsigned int   flags,
		    int            timeout_milliseconds)
{
}

static void
debug_live_messages_changed (DBusTransport *transport)
{
}

static DBusTransportVTable debug_vtable = {
  debug_finalize,
  debug_handle_watch,
  debug_disconnect,
  debug_connection_set,
  debug_messages_pending,
  debug_do_iteration,
  debug_live_messages_changed
};

/**
 * Creates a new debug server transport.
 *
 * @param client the client transport that the server transport
 * should use.
 * @returns a new debug transport
 */
DBusTransport*
_dbus_transport_debug_server_new (DBusTransport *client)
{
  DBusTransportDebug *debug_transport;

  debug_transport = dbus_new0 (DBusTransportDebug, 1);
  
  if (debug_transport == NULL)
    return NULL;

  if (!_dbus_transport_init_base (&debug_transport->base,
				  &debug_vtable,
				  TRUE))
    {
      dbus_free (debug_transport);
      return NULL;
    }

  debug_transport->base.authenticated = TRUE;

  /* Connect the two transports */
  debug_transport->other_end = client;
  ((DBusTransportDebug *)client)->other_end = (DBusTransport *)debug_transport;
  
  return (DBusTransport *)debug_transport;
}

/**
 * Creates a new debug client transport.
 *
 * @param server_name name of the server transport that
 * the client should try to connect to.
 * @param result address where a result code can be returned.
 * @returns a new transport, or #NULL on failure. 
 */
DBusTransport*
_dbus_transport_debug_client_new (const char     *server_name,
				  DBusResultCode *result)
{
  DBusServer *debug_server;
  DBusTransportDebug *debug_transport;
  
  debug_server = _dbus_server_debug_lookup (server_name);

  if (!debug_server)
    {
      dbus_set_result (result, DBUS_RESULT_NO_SERVER);
      return NULL;
    }

  debug_transport = dbus_new0 (DBusTransportDebug, 1);
  if (debug_transport == NULL)
    {
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return NULL;
    }

  if (!_dbus_transport_init_base (&debug_transport->base,
				  &debug_vtable,
				  FALSE))
    {
      dbus_free (debug_transport);
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return NULL;
    }

  if (!_dbus_server_debug_accept_transport (debug_server, (DBusTransport *)debug_transport))
    {
      _dbus_transport_finalize_base (&debug_transport->base);

      dbus_free (debug_transport);      
      dbus_set_result (result, DBUS_RESULT_IO_ERROR);
      return NULL;
      
    }

  /* FIXME: Prolly wrong to do this. */
  debug_transport->base.authenticated = TRUE;
  
  return (DBusTransport *)debug_transport;
}

/** @} */

#endif /* DBUS_BUILD_TESTS */
