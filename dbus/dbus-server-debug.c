/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-server-debug.h In-proc debug server implementation 
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
#include "dbus-server-debug.h"
#include "dbus-transport-debug.h"
#include "dbus-connection-internal.h"
#include "dbus-hash.h"

#ifdef DBUS_BUILD_TESTS

/**
 * @defgroup DBusServerDebug DBusServerDebug
 * @ingroup  DBusInternals
 * @brief In-process debug server used in unit tests.
 *
 * Types and functions related to DBusServerDebug.
 * This is used for unit testing.
 *
 * @{
 */

/**
 * Default timeout interval when reading or writing.
 */
#define DEFAULT_INTERVAL 1

/**
 * Opaque object representing a debug server implementation.
 */
typedef struct DBusServerDebug DBusServerDebug;

/**
 * Implementation details of DBusServerDebug. All members
 * are private.
 */
struct DBusServerDebug
{
  DBusServer base;  /**< Parent class members. */

  char *name; /**< Server name. */
};

/* Not thread safe, but OK since we don't use
 * threads in the bus
 */
static DBusHashTable *server_hash;

static void
debug_finalize (DBusServer *server)
{
}

static dbus_bool_t
debug_handle_watch (DBusServer  *server,
		    DBusWatch   *watch,
		    unsigned int flags)
{
  return TRUE;
}

static void
debug_disconnect (DBusServer *server)
{
}

static DBusServerVTable debug_vtable = {
  debug_finalize,
  debug_handle_watch,
  debug_disconnect
};

/**
 * Looks up a server by its name.
 *
 * @param server_name the server name.
 * @returns the server, or #NULL if none exists.
 */
DBusServer*
_dbus_server_debug_lookup (const char *server_name)
{
  if (!server_hash)
    return NULL;

  return _dbus_hash_table_lookup_string (server_hash, server_name);
}

/**
 * Creates a new debug server.
 *
 * @param server_name the name of the server.
 * @param error address where an error can be returned.
 * @returns a new server, or #NULL on failure.
 */
DBusServer*
_dbus_server_debug_new (const char     *server_name,
                        DBusError      *error)
{
  DBusServerDebug *debug_server;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  if (!server_hash)
    {
      server_hash = _dbus_hash_table_new (DBUS_HASH_STRING, NULL, NULL);

      if (!server_hash)
	{
	  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
	  return NULL;
	}
    }

  if (_dbus_hash_table_lookup_string (server_hash, server_name) != NULL)
    {
      dbus_set_error (error, DBUS_ERROR_ADDRESS_IN_USE,
                      NULL);
      return NULL;
    }
  
  debug_server = dbus_new0 (DBusServerDebug, 1);

  if (debug_server == NULL)
    return NULL;

  debug_server->name = _dbus_strdup (server_name);
  if (debug_server->name == NULL)
    {
      dbus_free (debug_server->name);
      dbus_free (debug_server);

      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
    }
  
  if (!_dbus_server_init_base (&debug_server->base,
			       &debug_vtable))
    {
      dbus_free (debug_server->name);      
      dbus_free (debug_server);

      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);

      return NULL;
    }

  if (!_dbus_hash_table_insert_string (server_hash,
				       debug_server->name,
				       debug_server))
    {
      _dbus_server_finalize_base (&debug_server->base);
      dbus_free (debug_server->name);      
      dbus_free (debug_server);

      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);

      return NULL;
    }
  
  return (DBusServer *)debug_server;
}

typedef struct
{
  DBusServer *server;
  DBusTransport *transport;
  DBusTimeout *timeout;
} ServerAndTransport;

static dbus_bool_t
handle_new_client (void *data)
{
  ServerAndTransport *st = data;
  DBusTransport *transport;
  DBusConnection *connection;

  _dbus_verbose ("  new debug client transport %p connecting to server\n",
                 st->transport);
  
  transport = _dbus_transport_debug_server_new (st->transport);
  if (transport == NULL)
    return FALSE;

  connection = _dbus_connection_new_for_transport (transport);
  _dbus_transport_unref (transport);

  if (connection == NULL)
    return FALSE;

  /* See if someone wants to handle this new connection,
   * self-referencing for paranoia
   */
  if (st->server->new_connection_function)
    {
      dbus_server_ref (st->server);
      
      (* st->server->new_connection_function) (st->server, connection,
					       st->server->new_connection_data);
      dbus_server_unref (st->server);
    }

  _dbus_server_remove_timeout (st->server, st->timeout);
  
  /* If no one grabbed a reference, the connection will die. */
  dbus_connection_unref (connection);

  /* killing timeout frees both "st" and "timeout" */
  _dbus_timeout_unref (st->timeout);

  return TRUE;
}

/**
 * Tells the server to accept a transport so the transport
 * can send messages to it.
 *
 * @param server the server
 * @param transport the transport
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_server_debug_accept_transport (DBusServer     *server,
				     DBusTransport  *transport)
{
  ServerAndTransport *st = NULL;

  st = dbus_new (ServerAndTransport, 1);
  if (st == NULL)
    return FALSE;

  st->transport = transport;
  st->server = server;
  
  st->timeout = _dbus_timeout_new (DEFAULT_INTERVAL, handle_new_client, st,
                                   dbus_free);

  if (st->timeout == NULL)
    goto failed;

  if (!_dbus_server_add_timeout (server, st->timeout))
    goto failed;
  
  return TRUE;

 failed:
  if (st->timeout)
    _dbus_timeout_unref (st->timeout);
  dbus_free (st);
  return FALSE;
}

/** @} */

#endif /* DBUS_BUILD_TESTS */

