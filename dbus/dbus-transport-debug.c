/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-transport-debug.c In-proc debug subclass of DBusTransport
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
#define DEFAULT_INTERVAL 1

/**
 * Hack due to lack of OOM handling in a couple places.
 * Need to alloc timeout permanently and enabled/disable so
 * that check_timeout won't fail in messages_pending
 */
#define WAIT_FOR_MEMORY() _dbus_sleep_milliseconds (250)

static dbus_bool_t check_timeout (DBusTransport *transport);

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

  DBusTimeout *timeout;                 /**< Timeout for moving messages. */
  
  DBusTransport *other_end;             /**< The transport that this transport is connected to. */

  unsigned int timeout_added : 1;       /**< Whether timeout has been added */
};

/* move messages in both directions */
static dbus_bool_t
move_messages (DBusTransport *transport)
{
  DBusTransportDebug *debug_transport = (DBusTransportDebug*) transport;
  
  if (transport->disconnected)
    return TRUE;

  while (!transport->disconnected &&
	 _dbus_connection_have_messages_to_send (transport->connection))
    {
      DBusMessage *message, *copy;
      
      message = _dbus_connection_get_message_to_send (transport->connection);
      _dbus_assert (message != NULL);
      
      copy = dbus_message_copy (message);
      if (copy == NULL)
        return FALSE;
        
      _dbus_message_lock (message);
      
      _dbus_connection_message_sent (transport->connection,
				     message);

      _dbus_verbose ("   -->transporting message %s from %s %p to %s %p\n",
                     dbus_message_get_name (copy),
                     transport->is_server ? "server" : "client",
                     transport->connection,
                     debug_transport->other_end->is_server ? "server" : "client",
                     debug_transport->other_end->connection);
      
      _dbus_connection_queue_received_message (debug_transport->other_end->connection,
                                               copy);
      dbus_message_unref (copy);
    }

  if (debug_transport->other_end &&
      !debug_transport->other_end->disconnected &&
      _dbus_connection_have_messages_to_send (debug_transport->other_end->connection))
    {
      if (!move_messages (debug_transport->other_end))
        return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
timeout_handler (void *data)
{
  DBusTransport *transport = data;
  
  if (!move_messages (transport))
    return FALSE;

  if (!check_timeout (transport))
    return FALSE;

  return TRUE;
}

static dbus_bool_t
check_timeout (DBusTransport *transport)
{
  DBusTransportDebug *debug_transport = (DBusTransportDebug*) transport;
  
  if (transport->connection &&
      transport->authenticated &&
      (transport->messages_need_sending ||
       (debug_transport->other_end &&
        debug_transport->other_end->messages_need_sending)))
    {
      if (!debug_transport->timeout_added)
        {
          /* FIXME this can be fixed now, by enabling/disabling
           * the timeout instead of adding it here
           */
          if (!_dbus_connection_add_timeout (transport->connection,
                                             debug_transport->timeout))
            return FALSE;
          debug_transport->timeout_added = TRUE;
        }
    }
  else
    {
      if (debug_transport->timeout_added)
        {
          _dbus_connection_remove_timeout (transport->connection,
                                           debug_transport->timeout);
          debug_transport->timeout_added = FALSE;
        }
    }

  return TRUE;
}

static void
debug_finalize (DBusTransport *transport)
{
  DBusTransportDebug *debug_transport = (DBusTransportDebug*) transport;
  
  if (debug_transport->timeout_added)
    _dbus_connection_remove_timeout (transport->connection,
                                     debug_transport->timeout);
  
  if (debug_transport->other_end)
    {
      _dbus_transport_disconnect (debug_transport->other_end);
      debug_transport->other_end = NULL;
    }
  
  _dbus_transport_finalize_base (transport);  

  _dbus_timeout_unref (debug_transport->timeout);
  
  dbus_free (transport);
}

static dbus_bool_t
debug_handle_watch (DBusTransport *transport,
		    DBusWatch     *watch,
		    unsigned int   flags)
{
  return TRUE;
}

static void
debug_disconnect (DBusTransport *transport)
{
}

static dbus_bool_t
debug_connection_set (DBusTransport *transport)
{
  if (!check_timeout (transport))
    return FALSE;
  return TRUE;
}

static void
debug_messages_pending (DBusTransport *transport,
			int            messages_pending)
{
  while (!check_timeout (transport))
    WAIT_FOR_MEMORY ();
}

static void
debug_do_iteration (DBusTransport *transport,
		    unsigned int   flags,
		    int            timeout_milliseconds)
{
  move_messages (transport);
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

static dbus_bool_t
create_timeout_object (DBusTransportDebug *debug_transport)
{
  debug_transport->timeout = _dbus_timeout_new (DEFAULT_INTERVAL,
                                                timeout_handler,
                                                debug_transport, NULL);
  
  return debug_transport->timeout != NULL;
}

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

  if (!create_timeout_object (debug_transport))
    {
      _dbus_transport_finalize_base (&debug_transport->base);      
      dbus_free (debug_transport);
      return NULL;
    }
  
  debug_transport->base.authenticated = TRUE;

  /* Connect the two transports */
  debug_transport->other_end = client;
  ((DBusTransportDebug *)client)->other_end = (DBusTransport *)debug_transport;

  _dbus_verbose ("  new debug server transport %p created, other end %p\n",
                 debug_transport, debug_transport->other_end);
  
  return (DBusTransport *)debug_transport;
}

/**
 * Creates a new debug client transport.
 *
 * @param server_name name of the server transport that
 * the client should try to connect to.
 * @param error address where an error can be returned.
 * @returns a new transport, or #NULL on failure. 
 */
DBusTransport*
_dbus_transport_debug_client_new (const char     *server_name,
				  DBusError      *error)
{
  DBusServer *debug_server;
  DBusTransportDebug *debug_transport;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  debug_server = _dbus_server_debug_lookup (server_name);

  if (!debug_server)
    {
      dbus_set_error (error, DBUS_ERROR_NO_SERVER, NULL);
      return NULL;
    }

  debug_transport = dbus_new0 (DBusTransportDebug, 1);
  if (debug_transport == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }

  if (!_dbus_transport_init_base (&debug_transport->base,
				  &debug_vtable,
				  FALSE))
    {
      dbus_free (debug_transport);
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }
  
  if (!create_timeout_object (debug_transport))
    {
      _dbus_transport_finalize_base (&debug_transport->base);
      dbus_free (debug_transport);
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }
  
  if (!_dbus_server_debug_accept_transport (debug_server,
                                            (DBusTransport *)debug_transport))
    {
      _dbus_timeout_unref (debug_transport->timeout);
      _dbus_transport_finalize_base (&debug_transport->base);
      dbus_free (debug_transport);
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }

  /* FIXME: Prolly wrong to do this. */
  debug_transport->base.authenticated = TRUE;

  _dbus_verbose ("  new debug client transport %p created, other end %p\n",
                 debug_transport, debug_transport->other_end);
  
  return (DBusTransport *)debug_transport;
}

/** @} */

#endif /* DBUS_BUILD_TESTS */
