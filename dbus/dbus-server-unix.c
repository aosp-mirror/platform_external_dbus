/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-server-unix.c Server implementation for Unix network protocols.
 *
 * Copyright (C) 2002, 2003, 2004  Red Hat Inc.
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

#include "dbus-internals.h"
#include "dbus-server-unix.h"
#include "dbus-transport-unix.h"
#include "dbus-connection-internal.h"
#include "dbus-string.h"
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
  DBusServer base;   /**< Parent class members. */
  int fd;            /**< File descriptor or -1 if disconnected. */
  DBusWatch *watch;  /**< File descriptor watch. */
  char *socket_name; /**< Name of domain socket, to unlink if appropriate */
};

static void
unix_finalize (DBusServer *server)
{
  DBusServerUnix *unix_server = (DBusServerUnix*) server;
  
  _dbus_server_finalize_base (server);

  if (unix_server->watch)
    {
      _dbus_watch_unref (unix_server->watch);
      unix_server->watch = NULL;
    }
  
  dbus_free (unix_server->socket_name);
  dbus_free (server);
}

/**
 * @todo unreffing the connection at the end may cause
 * us to drop the last ref to the connection before
 * disconnecting it. That is invalid.
 *
 * @todo doesn't this leak a server refcount if
 * new_connection_function is NULL?
 */
/* Return value is just for memory, not other failures. */
static dbus_bool_t
handle_new_client_fd_and_unlock (DBusServer *server,
                                 int         client_fd)
{
  DBusConnection *connection;
  DBusTransport *transport;
  DBusNewConnectionFunction new_connection_function;
  void *new_connection_data;
  
  _dbus_verbose ("Creating new client connection with fd %d\n", client_fd);

  HAVE_LOCK_CHECK (server);
  
  if (!_dbus_set_fd_nonblocking (client_fd, NULL))
    {
      SERVER_UNLOCK (server);
      return TRUE;
    }
  
  transport = _dbus_transport_new_for_fd (client_fd, &server->guid_hex, NULL);
  if (transport == NULL)
    {
      close (client_fd);
      SERVER_UNLOCK (server);
      return FALSE;
    }

  if (!_dbus_transport_set_auth_mechanisms (transport,
                                            (const char **) server->auth_mechanisms))
    {
      _dbus_transport_unref (transport);
      SERVER_UNLOCK (server);
      return FALSE;
    }
  
  /* note that client_fd is now owned by the transport, and will be
   * closed on transport disconnection/finalization
   */
  
  connection = _dbus_connection_new_for_transport (transport);
  _dbus_transport_unref (transport);
  transport = NULL; /* now under the connection lock */
  
  if (connection == NULL)
    {
      SERVER_UNLOCK (server);
      return FALSE;
    }
  
  /* See if someone wants to handle this new connection, self-referencing
   * for paranoia.
   */
  new_connection_function = server->new_connection_function;
  new_connection_data = server->new_connection_data;

  _dbus_server_ref_unlocked (server);
  SERVER_UNLOCK (server);
  
  if (new_connection_function)
    {
      (* new_connection_function) (server, connection,
                                   new_connection_data);
      dbus_server_unref (server);
    }
  
  /* If no one grabbed a reference, the connection will die. */
  dbus_connection_unref (connection);

  return TRUE;
}

static dbus_bool_t
unix_handle_watch (DBusWatch    *watch,
                   unsigned int  flags,
                   void         *data)
{
  DBusServer *server = data;
  DBusServerUnix *unix_server = data;

  SERVER_LOCK (server);
  
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

          SERVER_UNLOCK (server);
        }
      else
        {
	  _dbus_fd_set_close_on_exec (client_fd);	  

          if (!handle_new_client_fd_and_unlock (server, client_fd))
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

  HAVE_LOCK_CHECK (server);
  
  if (unix_server->watch)
    {
      _dbus_server_remove_watch (server,
                                 unix_server->watch);
      _dbus_watch_unref (unix_server->watch);
      unix_server->watch = NULL;
    }
  
  close (unix_server->fd);
  unix_server->fd = -1;

  if (unix_server->socket_name != NULL)
    {
      DBusString tmp;
      _dbus_string_init_const (&tmp, unix_server->socket_name);
      _dbus_delete_file (&tmp, NULL);
    }

  HAVE_LOCK_CHECK (server);
}

static DBusServerVTable unix_vtable = {
  unix_finalize,
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
  DBusServer *server;
  DBusWatch *watch;
  
  unix_server = dbus_new0 (DBusServerUnix, 1);
  if (unix_server == NULL)
    return NULL;
  
  watch = _dbus_watch_new (fd,
                           DBUS_WATCH_READABLE,
                           TRUE,
                           unix_handle_watch, unix_server,
                           NULL);
  if (watch == NULL)
    {
      dbus_free (unix_server);
      return NULL;
    }
  
  if (!_dbus_server_init_base (&unix_server->base,
                               &unix_vtable, address))
    {
      _dbus_watch_unref (watch);
      dbus_free (unix_server);
      return NULL;
    }

  server = (DBusServer*) unix_server;

  SERVER_LOCK (server);
  
  if (!_dbus_server_add_watch (&unix_server->base,
                               watch))
    {
      SERVER_UNLOCK (server);
      _dbus_server_finalize_base (&unix_server->base);
      _dbus_watch_unref (watch);
      dbus_free (unix_server);
      return NULL;
    }
  
  unix_server->fd = fd;
  unix_server->watch = watch;

  SERVER_UNLOCK (server);
  
  return (DBusServer*) unix_server;
}

/**
 * Creates a new server listening on the given Unix domain socket.
 *
 * @param path the path for the domain socket.
 * @param abstract #TRUE to use abstract socket namespace
 * @param error location to store reason for failure.
 * @returns the new server, or #NULL on failure.
 */
DBusServer*
_dbus_server_new_for_domain_socket (const char     *path,
                                    dbus_bool_t     abstract,
                                    DBusError      *error)
{
  DBusServer *server;
  DBusServerUnix *unix_server;
  int listen_fd;
  DBusString address;
  char *path_copy;
  DBusString path_str;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (!_dbus_string_init (&address))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }

  _dbus_string_init_const (&path_str, path);
  if ((abstract &&
       !_dbus_string_append (&address, "unix:abstract=")) ||
      (!abstract &&
       !_dbus_string_append (&address, "unix:path=")) ||
      !_dbus_address_append_escaped (&address, &path_str))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      goto failed_0;
    }

  path_copy = _dbus_strdup (path);
  if (path_copy == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      goto failed_0;
    }
  
  listen_fd = _dbus_listen_unix_socket (path, abstract, error);
  _dbus_fd_set_close_on_exec (listen_fd);
  
  if (listen_fd < 0)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto failed_1;
    }
  
  server = _dbus_server_new_for_fd (listen_fd, &address);
  if (server == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      goto failed_2;
    }

  unix_server = (DBusServerUnix*) server;
  unix_server->socket_name = path_copy;
  
  _dbus_string_free (&address);
  
  return server;

 failed_2:
  _dbus_close (listen_fd, NULL);
 failed_1:
  dbus_free (path_copy);
 failed_0:
  _dbus_string_free (&address);

  return NULL;
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
  DBusString host_str;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (!_dbus_string_init (&address))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }

  if (host == NULL)
    host = "localhost";

  _dbus_string_init_const (&host_str, host);
  if (!_dbus_string_append (&address, "tcp:host=") ||
      !_dbus_address_append_escaped (&address, &host_str) ||
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

