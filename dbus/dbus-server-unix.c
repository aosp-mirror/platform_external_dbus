/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-server-unix.c Server implementation for Unix network protocols.
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

#include "dbus-internals.h"
#include "dbus-server-unix.h"
#include "dbus-transport-unix.h"
#include "dbus-connection-internal.h"
#include <sys/types.h>
#include <unistd.h>

/**
 * @defgroup DBusServerUnix DBusServer implementations for UNIX
 * @ingroup  DBusInternals
 * @brief Implementation details of DBusServer on UNIX
 *
 * @{
 */
/**
 * 
 * Opaque object representing a Unix server implementation.
 */
typedef struct DBusServerUnix DBusServerUnix;

/**
 * Implementation details of DBusServerUnix. All members
 * are private.
 */
struct DBusServerUnix
{
  DBusServer base;  /**< Parent class members. */
  int fd;           /**< File descriptor or -1 if disconnected. */
  DBusWatch *watch; /**< File descriptor watch. */
};

static void
unix_finalize (DBusServer *server)
{
  DBusServerUnix *unix_server = (DBusServerUnix*) server;
  
  _dbus_server_finalize_base (server);

  if (unix_server->watch)
    _dbus_watch_unref (unix_server->watch);
  
  dbus_free (server);
}

/**
 * @todo unreffing the connection at the end may cause
 * us to drop the last ref to the connection before
 * disconnecting it. That is invalid.
 */
/* Return value is just for memory, not other failures. */
static dbus_bool_t
handle_new_client_fd (DBusServer *server,
                      int         client_fd)
{
  DBusConnection *connection;
  DBusTransport *transport;
  
  _dbus_verbose ("Creating new client connection with fd %d\n", client_fd);
          
  if (!_dbus_set_fd_nonblocking (client_fd, NULL))
    return TRUE;
  
  transport = _dbus_transport_new_for_fd (client_fd, TRUE, NULL);
  if (transport == NULL)
    {
      close (client_fd);
      return FALSE;
    }

  if (!_dbus_transport_set_auth_mechanisms (transport,
                                            (const char **) server->auth_mechanisms))
    {
      _dbus_transport_unref (transport);
      return FALSE;
    }
  
  /* note that client_fd is now owned by the transport, and will be
   * closed on transport disconnection/finalization
   */
  
  connection = _dbus_connection_new_for_transport (transport);
  _dbus_transport_unref (transport);
  
  if (connection == NULL)
    return FALSE;

  _dbus_connection_set_connection_counter (connection,
                                           server->connection_counter);
  
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
  
  /* If no one grabbed a reference, the connection will die. */
  dbus_connection_unref (connection);

  return TRUE;
}

static dbus_bool_t
unix_handle_watch (DBusServer  *server,
                   DBusWatch   *watch,
                   unsigned int flags)
{
  DBusServerUnix *unix_server = (DBusServerUnix*) server;

  _dbus_assert (watch == unix_server->watch);

  _dbus_verbose ("Handling client connection, flags 0x%x\n", flags);
  
  if (flags & DBUS_WATCH_READABLE)
    {
      int client_fd;
      int listen_fd;
      
      listen_fd = dbus_watch_get_fd (watch);

      client_fd = _dbus_accept (listen_fd);
      
      if (client_fd < 0)
        {
          /* EINTR handled for us */
          
          if (errno == EAGAIN || errno == EWOULDBLOCK)
            _dbus_verbose ("No client available to accept after all\n");
          else
            _dbus_verbose ("Failed to accept a client connection: %s\n",
                           _dbus_strerror (errno));
        }
      else
        {
	  _dbus_fd_set_close_on_exec (client_fd);	  

          if (!handle_new_client_fd (server, client_fd))
            _dbus_verbose ("Rejected client connection due to lack of memory\n");
        }
    }

  if (flags & DBUS_WATCH_ERROR)
    _dbus_verbose ("Error on server listening socket\n");

  if (flags & DBUS_WATCH_HANGUP)
    _dbus_verbose ("Hangup on server listening socket\n");

  return TRUE;
}
  
static void
unix_disconnect (DBusServer *server)
{
  DBusServerUnix *unix_server = (DBusServerUnix*) server;

  if (unix_server->watch)
    {
      _dbus_server_remove_watch (server,
                                 unix_server->watch);
      _dbus_watch_unref (unix_server->watch);
      unix_server->watch = NULL;
    }
  
  close (unix_server->fd);
  unix_server->fd = -1;
}

static DBusServerVTable unix_vtable = {
  unix_finalize,
  unix_handle_watch,
  unix_disconnect
};

/**
 * Creates a new server listening on the given file descriptor.  The
 * file descriptor should be nonblocking (use
 * _dbus_set_fd_nonblocking() to make it so). The file descriptor
 * should be listening for connections, that is, listen() should have
 * been successfully invoked on it. The server will use accept() to
 * accept new client connections.
 *
 * @param fd the file descriptor.
 * @param address the server's address
 * @returns the new server, or #NULL if no memory.
 * 
 */
DBusServer*
_dbus_server_new_for_fd (int               fd,
                         const DBusString *address)
{
  DBusServerUnix *unix_server;
  DBusWatch *watch;

  watch = _dbus_watch_new (fd,
                           DBUS_WATCH_READABLE,
                           TRUE);
  if (watch == NULL)
    return NULL;
  
  unix_server = dbus_new0 (DBusServerUnix, 1);
  if (unix_server == NULL)
    {
      _dbus_watch_unref (watch);
      return NULL;
    }
  
  if (!_dbus_server_init_base (&unix_server->base,
                               &unix_vtable, address))
    {
      _dbus_watch_unref (watch);
      dbus_free (unix_server);
      return NULL;
    }

  if (!_dbus_server_add_watch (&unix_server->base,
                               watch))
    {
      _dbus_server_finalize_base (&unix_server->base);
      _dbus_watch_unref (watch);
      dbus_free (unix_server);
      return NULL;
    }
  
  unix_server->fd = fd;
  unix_server->watch = watch;

  return (DBusServer*) unix_server;
}

/**
 * Creates a new server listening on the given Unix domain socket.
 *
 * @param path the path for the domain socket.
 * @param error location to store reason for failure.
 * @returns the new server, or #NULL on failure.
 */
DBusServer*
_dbus_server_new_for_domain_socket (const char     *path,
                                    DBusError      *error)
{
  DBusServer *server;
  int listen_fd;
  DBusString address;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (!_dbus_string_init (&address))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }

  if (!_dbus_string_append (&address, "unix:path=") ||
      !_dbus_string_append (&address, path))
    {
      _dbus_string_free (&address);
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }
  
  listen_fd = _dbus_listen_unix_socket (path, error);
  _dbus_fd_set_close_on_exec (listen_fd);
  
  if (listen_fd < 0)
    {
      _dbus_string_free (&address);
      return NULL;
    }
  
  server = _dbus_server_new_for_fd (listen_fd, &address);
  if (server == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      close (listen_fd);
      _dbus_string_free (&address);
      return NULL;
    }

  _dbus_string_free (&address);
  
  return server;
}

/**
 * Creates a new server listening on the given hostname and port.
 * If the hostname is NULL, listens on localhost.
 *
 * @param host the hostname to listen on.
 * @param port the port to listen on.
 * @param error location to store reason for failure.
 * @returns the new server, or #NULL on failure.
 */
DBusServer*
_dbus_server_new_for_tcp_socket (const char     *host,
                                 dbus_uint32_t   port,
                                 DBusError      *error)
{
  DBusServer *server;
  int listen_fd;
  DBusString address;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (!_dbus_string_init (&address))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }

  if (!_dbus_string_append (&address, "tcp:host=") ||
      !_dbus_string_append (&address, host) ||
      !_dbus_string_append (&address, ",port=") ||
      !_dbus_string_append_int (&address, port))
    {
      _dbus_string_free (&address);
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }
  
  listen_fd = _dbus_listen_tcp_socket (host, port, error);
  _dbus_fd_set_close_on_exec (listen_fd);
  
  if (listen_fd < 0)
    {
      _dbus_string_free (&address);
      return NULL;
    }
  
  server = _dbus_server_new_for_fd (listen_fd, &address);
  if (server == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      close (listen_fd);
      _dbus_string_free (&address);
      return NULL;
    }

  _dbus_string_free (&address);
  
  return server;


}

/** @} */

