/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-server.c DBusServer object
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
#include "dbus-server.h"
#include "dbus-server-unix.h"

/**
 * @defgroup DBusServer DBusServer
 * @ingroup  DBus
 * @brief Server that listens for new connections.
 *
 * Types and functions related to DBusServer.
 * A DBusServer represents a server that other applications
 * can connect to. Each connection from another application
 * is represented by a DBusConnection.
 */

/**
 * @defgroup DBusServerInternals DBusServer implementation details
 * @ingroup  DBusInternals
 * @brief Implementation details of DBusServer
 *
 * @{
 */

/**
 * Initializes the members of the DBusServer base class.
 * Chained up to by subclass constructors.
 *
 * @param server the server.
 * @param vtable the vtable for the subclass.
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_server_init_base (DBusServer             *server,
                        const DBusServerVTable *vtable)
{
  server->vtable = vtable;
  server->refcount = 1;

  server->watches = _dbus_watch_list_new ();
  if (server->watches == NULL)
    return FALSE;

  return TRUE;
}

/**
 * Finalizes the members of the DBusServer base class.
 * Chained up to by subclass finalizers.
 *
 * @param server the server.
 */
void
_dbus_server_finalize_base (DBusServer *server)
{
  dbus_server_set_new_connection_function (server, NULL, NULL, NULL);

  if (!server->disconnected)
    dbus_server_disconnect (server);

  _dbus_watch_list_free (server->watches);
}

/**
 * Adds a watch for this server, chaining out to application-provided
 * watch handlers.
 *
 * @param server the server.
 * @param watch the watch to add.
 */
dbus_bool_t
_dbus_server_add_watch (DBusServer *server,
                        DBusWatch  *watch)
{
  return _dbus_watch_list_add_watch (server->watches, watch);
}

/**
 * Removes a watch previously added with _dbus_server_remove_watch().
 *
 * @param server the server.
 * @param watch the watch to remove.
 */
void
_dbus_server_remove_watch  (DBusServer *server,
                            DBusWatch  *watch)
{
  _dbus_watch_list_remove_watch (server->watches, watch);
}


/** @} */

/**
 * @addtogroup DBusServer
 *
 * @{
 */


/**
 * @typedef DBusServer
 *
 * An opaque object representing a server that listens for
 * connections from other applications. Each time a connection
 * is made, a new DBusConnection is created and made available
 * via an application-provided DBusNewConnectionFunction.
 * The DBusNewConnectionFunction is provided with
 * dbus_server_set_new_connection_function().
 * 
 */

/**
 * Listens for new connections on the given address.
 * Returns #NULL if listening fails for any reason.
 * Otherwise returns a new #DBusServer.
 * dbus_server_set_new_connection_function() and
 * dbus_server_set_watch_functions() should be called
 * immediately to render the server fully functional.
 *
 * @param address the address of this server.
 * @param result location to store rationale for failure.
 * @returns a new DBusServer, or #NULL on failure.
 * 
 */
DBusServer*
dbus_server_listen (const char     *address,
                    DBusResultCode *result)
{
  DBusServer *server;

  /* For now just pretend the address is a unix domain socket path */
  server = _dbus_server_new_for_domain_socket (address, result);
  
  return server;
}

/**
 * Increments the reference count of a DBusServer.
 *
 * @param server the server.
 */
void
dbus_server_ref (DBusServer *server)
{
  server->refcount += 1;
}

/**
 * Decrements the reference count of a DBusServer.  Finalizes the
 * server if the reference count reaches zero. The server connection
 * will be closed as with dbus_server_disconnect() when the server is
 * finalized.
 *
 * @param server the server.
 */
void
dbus_server_unref (DBusServer *server)
{
  _dbus_assert (server != NULL);
  _dbus_assert (server->refcount > 0);

  server->refcount -= 1;
  if (server->refcount == 0)
    {
      _dbus_assert (server->vtable->finalize != NULL);
      
      (* server->vtable->finalize) (server);
    }
}

/**
 * Releases the server's address and stops listening for
 * new clients. If called more than once, only the first
 * call has an effect. Does not modify the server's
 * reference count.
 * 
 * @param server the server.
 */
void
dbus_server_disconnect (DBusServer *server)
{
  _dbus_assert (server->vtable->disconnect != NULL);

  if (server->disconnected)
    return;
  
  (* server->vtable->disconnect) (server);
  server->disconnected = TRUE;
}

/**
 * Returns #TRUE if the server is still listening for new connections.
 *
 * @param server the server.
 */
dbus_bool_t
dbus_server_get_is_connected (DBusServer *server)
{
  return !server->disconnected;
}

/**
 * Sets a function to be used for handling new connections.  The given
 * function is passed each new connection as the connection is
 * created. If the new connection function increments the connection's
 * reference count, the connection will stay alive. Otherwise, the
 * connection will be unreferenced and closed.
 *
 * @param server the server.
 * @param function a function to handle new connections.
 * @param data data to pass to the new connection handler.
 * @param free_data_function function to free the data.
 */
void
dbus_server_set_new_connection_function (DBusServer                *server,
                                         DBusNewConnectionFunction  function,
                                         void                      *data,
                                         DBusFreeFunction           free_data_function)
{
  if (server->new_connection_free_data_function != NULL)
    (* server->new_connection_free_data_function) (server->new_connection_data);
  
  server->new_connection_function = function;
  server->new_connection_data = data;
  server->new_connection_free_data_function = free_data_function;
}

/**
 * Sets the watch functions for the connection. These functions are
 * responsible for making the application's main loop aware of file
 * descriptors that need to be monitored for events.
 *
 * This function behaves exactly like dbus_connection_set_watch_functions();
 * see the documentation for that routine.
 *
 * @param server the server.
 * @param add_function function to begin monitoring a new descriptor.
 * @param remove_function function to stop monitoring a descriptor.
 * @param data data to pass to add_function and remove_function.
 * @param free_data_function function to be called to free the data.
 */
void
dbus_server_set_watch_functions (DBusServer              *server,
                                 DBusAddWatchFunction     add_function,
                                 DBusRemoveWatchFunction  remove_function,
                                 void                    *data,
                                 DBusFreeFunction         free_data_function)
{
  _dbus_watch_list_set_functions (server->watches,
                                  add_function,
                                  remove_function,
                                  data,
                                  free_data_function);
}

/**
 * Called to notify the server when a previously-added watch
 * is ready for reading or writing, or has an exception such
 * as a hangup.
 *
 * @param server the server.
 * @param watch the watch.
 * @param condition the current condition of the file descriptors being watched.
 */
void
dbus_server_handle_watch (DBusServer              *server,
                          DBusWatch               *watch,
                          unsigned int             condition)
{
  _dbus_assert (server->vtable->handle_watch != NULL);

  _dbus_watch_sanitize_condition (watch, &condition);
  
  (* server->vtable->handle_watch) (server, watch, condition);
}

/** @} */

