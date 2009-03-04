/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-server-socket.c Server implementation for sockets
 *
 * Copyright (C) 2002, 2003, 2004, 2006  Red Hat Inc.
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
#include "dbus-server-socket.h"
#include "dbus-transport-socket.h"
#include "dbus-connection-internal.h"
#include "dbus-string.h"

/**
 * @defgroup DBusServerSocket DBusServer implementations for SOCKET
 * @ingroup  DBusInternals
 * @brief Implementation details of DBusServer on SOCKET
 *
 * @{
 */
/**
 * 
 * Opaque object representing a Socket server implementation.
 */
typedef struct DBusServerSocket DBusServerSocket;

/**
 * Implementation details of DBusServerSocket. All members
 * are private.
 */
struct DBusServerSocket
{
  DBusServer base;   /**< Parent class members. */
  int fd;            /**< File descriptor or -1 if disconnected. */
  DBusWatch *watch;  /**< File descriptor watch. */
  char *socket_name; /**< Name of domain socket, to unlink if appropriate */
};

static void
socket_finalize (DBusServer *server)
{
  DBusServerSocket *socket_server = (DBusServerSocket*) server;
  
  _dbus_server_finalize_base (server);

  if (socket_server->watch)
    {
      _dbus_watch_unref (socket_server->watch);
      socket_server->watch = NULL;
    }
  
  dbus_free (socket_server->socket_name);
  dbus_free (server);
}

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
  
  transport = _dbus_transport_new_for_socket (client_fd, &server->guid_hex, NULL);
  if (transport == NULL)
    {
      _dbus_close_socket (client_fd, NULL);
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
    }
  dbus_server_unref (server);
  
  /* If no one grabbed a reference, the connection will die. */
  _dbus_connection_close_if_only_one_ref (connection);
  dbus_connection_unref (connection);

  return TRUE;
}

static dbus_bool_t
socket_handle_watch (DBusWatch    *watch,
                   unsigned int  flags,
                   void         *data)
{
  DBusServer *server = data;
  DBusServerSocket *socket_server = data;

  SERVER_LOCK (server);
  
  _dbus_assert (watch == socket_server->watch);

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
socket_disconnect (DBusServer *server)
{
  DBusServerSocket *socket_server = (DBusServerSocket*) server;

  HAVE_LOCK_CHECK (server);
  
  if (socket_server->watch)
    {
      _dbus_server_remove_watch (server,
                                 socket_server->watch);
      _dbus_watch_unref (socket_server->watch);
      socket_server->watch = NULL;
    }
  
  _dbus_close_socket (socket_server->fd, NULL);
  socket_server->fd = -1;

  if (socket_server->socket_name != NULL)
    {
      DBusString tmp;
      _dbus_string_init_const (&tmp, socket_server->socket_name);
      _dbus_delete_file (&tmp, NULL);
    }

  HAVE_LOCK_CHECK (server);
}

static const DBusServerVTable socket_vtable = {
  socket_finalize,
  socket_disconnect
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
_dbus_server_new_for_socket (int               fd,
                             const DBusString *address)
{
  DBusServerSocket *socket_server;
  DBusServer *server;
  DBusWatch *watch;
  
  socket_server = dbus_new0 (DBusServerSocket, 1);
  if (socket_server == NULL)
    return NULL;
  
  watch = _dbus_watch_new (fd,
                           DBUS_WATCH_READABLE,
                           TRUE,
                           socket_handle_watch, socket_server,
                           NULL);
  if (watch == NULL)
    {
      dbus_free (socket_server);
      return NULL;
    }
  
  if (!_dbus_server_init_base (&socket_server->base,
                               &socket_vtable, address))
    {
      _dbus_watch_unref (watch);
      dbus_free (socket_server);
      return NULL;
    }

  server = (DBusServer*) socket_server;

  SERVER_LOCK (server);
  
  if (!_dbus_server_add_watch (&socket_server->base,
                               watch))
    {
      SERVER_UNLOCK (server);
      _dbus_server_finalize_base (&socket_server->base);
      _dbus_watch_unref (watch);
      dbus_free (socket_server);
      return NULL;
    }
  
  socket_server->fd = fd;
  socket_server->watch = watch;

  SERVER_UNLOCK (server);
  
  return (DBusServer*) socket_server;
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
  
  server = _dbus_server_new_for_socket (listen_fd, &address);
  if (server == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      _dbus_close_socket (listen_fd, NULL);
      _dbus_string_free (&address);
      return NULL;
    }

  _dbus_string_free (&address);
  
  return server;


}

/**
 * Tries to interpret the address entry for various socket-related
 * addresses (well, currently only tcp).
 * 
 * Sets error if the result is not OK.
 * 
 * @param entry an address entry
 * @param server_p a new DBusServer, or #NULL on failure.
 * @param error location to store rationale for failure on bad address
 * @returns the outcome
 * 
 */
DBusServerListenResult
_dbus_server_listen_socket (DBusAddressEntry *entry,
                            DBusServer      **server_p,
                            DBusError        *error)
{
  const char *method;

  *server_p = NULL;
  
  method = dbus_address_entry_get_method (entry);
  
  if (strcmp (method, "tcp") == 0)
    {
      const char *host = dbus_address_entry_get_value (entry, "host");
      const char *port = dbus_address_entry_get_value (entry, "port");
      DBusString  str;
      long lport;
      dbus_bool_t sresult;
          
      if (port == NULL)
        {
          _dbus_set_bad_address(error, "tcp", "port", NULL);
          return DBUS_SERVER_LISTEN_BAD_ADDRESS;
        }

      _dbus_string_init_const (&str, port);
      sresult = _dbus_string_parse_int (&str, 0, &lport, NULL);
      _dbus_string_free (&str);
          
      if (sresult == FALSE || lport <= 0 || lport > 65535)
        {
          _dbus_set_bad_address(error, NULL, NULL, 
                                "Port is not an integer between 0 and 65535");
          return DBUS_SERVER_LISTEN_BAD_ADDRESS;
        }
          
      *server_p = _dbus_server_new_for_tcp_socket (host, lport, error);

      if (*server_p)
        {
          _DBUS_ASSERT_ERROR_IS_CLEAR(error);
          return DBUS_SERVER_LISTEN_OK;
        }
      else
        {
          _DBUS_ASSERT_ERROR_IS_SET(error);
          return DBUS_SERVER_LISTEN_DID_NOT_CONNECT;
        }
    }
  else
    {
      _DBUS_ASSERT_ERROR_IS_CLEAR(error);
      return DBUS_SERVER_LISTEN_NOT_HANDLED;
    }
}

/**
 * This is a bad hack since it's really unix domain socket
 * specific. Also, the function weirdly adopts ownership
 * of the passed-in string.
 * 
 * @param server a socket server
 * @param filename socket filename to report/delete
 * 
 */
void
_dbus_server_socket_own_filename (DBusServer *server,
                                  char       *filename)
{
  DBusServerSocket *socket_server = (DBusServerSocket*) server;

  socket_server->socket_name = filename;
}


/** @} */

