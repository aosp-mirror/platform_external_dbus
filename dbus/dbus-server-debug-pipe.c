/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-server-debug-pipe.c In-proc debug server implementation 
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
#include "dbus-server-debug-pipe.h"
#include "dbus-transport-unix.h"
#include "dbus-connection-internal.h"
#include "dbus-hash.h"

#ifdef DBUS_BUILD_TESTS

/**
 * @defgroup DBusServerDebugPipe DBusServerDebugPipe
 * @ingroup  DBusInternals
 * @brief In-process pipe debug server used in unit tests.
 *
 * Types and functions related to DBusServerDebugPipe.
 * This is used for unit testing.
 *
 * @{
 */

/**
 * Opaque object representing a debug server implementation.
 */
typedef struct DBusServerDebugPipe DBusServerDebugPipe;

/**
 * Implementation details of DBusServerDebugPipe. All members
 * are private.
 */
struct DBusServerDebugPipe
{
  DBusServer base;  /**< Parent class members. */

  char *name; /**< Server name. */

  dbus_bool_t disconnected; /**< TRUE if disconnect has been called */
};

/* FIXME not threadsafe (right now the test suite doesn't use threads anyhow ) */
static DBusHashTable *server_pipe_hash;
static int server_pipe_hash_refcount = 0;

static dbus_bool_t
pipe_hash_ref (void)
{
  if (!server_pipe_hash)
    {
      _dbus_assert (server_pipe_hash_refcount == 0);
      
      server_pipe_hash = _dbus_hash_table_new (DBUS_HASH_STRING, NULL, NULL);

      if (!server_pipe_hash)
        return FALSE;
    }

  server_pipe_hash_refcount = 1;

  return TRUE;
}

static void
pipe_hash_unref (void)
{
  _dbus_assert (server_pipe_hash != NULL);
  _dbus_assert (server_pipe_hash_refcount > 0);

  server_pipe_hash_refcount -= 1;
  if (server_pipe_hash_refcount == 0)
    {
      _dbus_hash_table_unref (server_pipe_hash);
      server_pipe_hash = NULL;
    }
}

static void
debug_finalize (DBusServer *server)
{
  DBusServerDebugPipe *debug_server = (DBusServerDebugPipe*) server;

  pipe_hash_unref ();
  
  _dbus_server_finalize_base (server);

  dbus_free (debug_server->name);
  dbus_free (server);
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
  ((DBusServerDebugPipe*)server)->disconnected = TRUE;
}

static DBusServerVTable debug_vtable = {
  debug_finalize,
  debug_handle_watch,
  debug_disconnect
};

/**
 * Creates a new debug server using an in-process pipe
 *
 * @param server_name the name of the server.
 * @param error address where an error can be returned.
 * @returns a new server, or #NULL on failure.
 */
DBusServer*
_dbus_server_debug_pipe_new (const char     *server_name,
                             DBusError      *error)
{
  DBusServerDebugPipe *debug_server;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  if (!pipe_hash_ref ())
    return NULL;
  
  if (_dbus_hash_table_lookup_string (server_pipe_hash, server_name) != NULL)
    {
      dbus_set_error (error, DBUS_ERROR_ADDRESS_IN_USE, NULL);
      pipe_hash_unref ();
      return NULL;
    }
  
  debug_server = dbus_new0 (DBusServerDebugPipe, 1);

  if (debug_server == NULL)
    {
      pipe_hash_unref ();
      return NULL;
    }

  debug_server->name = _dbus_strdup (server_name);
  if (debug_server->name == NULL)
    {
      dbus_free (debug_server->name);
      dbus_free (debug_server);

      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);

      pipe_hash_unref ();
      return NULL;
    }
  
  if (!_dbus_server_init_base (&debug_server->base,
			       &debug_vtable))
    {
      dbus_free (debug_server->name);      
      dbus_free (debug_server);

      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);

      pipe_hash_unref ();
      return NULL;
    }

  if (!_dbus_hash_table_insert_string (server_pipe_hash,
				       debug_server->name,
				       debug_server))
    {
      _dbus_server_finalize_base (&debug_server->base);
      dbus_free (debug_server->name);      
      dbus_free (debug_server);

      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);

      pipe_hash_unref ();
      return NULL;
    }
  
  return (DBusServer *)debug_server;
}

/**
 * Creates the client-side transport for
 * a debug-pipe connection connected to the
 * given debug-pipe server name.
 * 
 * @param server_name name of server to connect to
 * @param result return location for result
 * @returns #NULL on no memory or transport
 */
DBusTransport*
_dbus_transport_debug_pipe_new (const char     *server_name,
                                DBusError      *error)
{
  DBusTransport *client_transport;
  DBusTransport *server_transport;
  DBusConnection *connection;
  int client_fd, server_fd;
  DBusServer *server;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  server = _dbus_hash_table_lookup_string (server_pipe_hash,
                                           server_name);
  if (server == NULL ||
      ((DBusServerDebugPipe*)server)->disconnected)
    {
      dbus_set_error (error, DBUS_ERROR_BAD_ADDRESS, NULL);
      return NULL;
    }
  
  if (!_dbus_full_duplex_pipe (&client_fd, &server_fd,
                               NULL))
    {
      _dbus_verbose ("failed to create full duplex pipe\n");
      dbus_set_error (error, DBUS_ERROR_FAILED, "Could not create full-duplex pipe");
      return NULL;
    }

  _dbus_fd_set_close_on_exec (client_fd);
  _dbus_fd_set_close_on_exec (server_fd);
  
  client_transport = _dbus_transport_new_for_fd (client_fd,
                                                 FALSE);
  if (client_transport == NULL)
    {
      _dbus_close (client_fd, NULL);
      _dbus_close (server_fd, NULL);
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }

  client_fd = -1;
  
  server_transport = _dbus_transport_new_for_fd (server_fd,
                                                 TRUE);
  if (server_transport == NULL)
    {
      _dbus_transport_unref (client_transport);
      _dbus_close (server_fd, NULL);
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }

  server_fd = -1;

  connection = _dbus_connection_new_for_transport (server_transport);
  _dbus_transport_unref (server_transport);
  server_transport = NULL;
  
  if (connection == NULL)
    {
      _dbus_transport_unref (client_transport);
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }

  /* See if someone wants to handle this new connection,
   * self-referencing for paranoia
   */
  if (server->new_connection_function)
    {
      dbus_server_ref (server);
      (* server->new_connection_function) (server, connection,
                                           server->new_connection_data);
      dbus_server_unref (server);
    }
  
  /* If no one grabbed a reference, the connection will die,
   * and the client transport will get an immediate disconnect
   */
  dbus_connection_unref (connection);

  return client_transport;
}


/** @} */

#endif /* DBUS_BUILD_TESTS */

