/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-server.c DBusServer object
 *
 * Copyright (C) 2002, 2003 Red Hat Inc.
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
#ifdef DBUS_BUILD_TESTS
#include "dbus-server-debug.h"
#include "dbus-server-debug-pipe.h"
#endif
#include "dbus-address.h"

/**
 * @defgroup DBusServer DBusServer
 * @ingroup  DBus
 * @brief Server that listens for new connections.
 *
 * Types and functions related to DBusServer.
 * A DBusServer represents a server that other applications
 * can connect to. Each connection from another application
 * is represented by a DBusConnection.
 *
 * @todo Thread safety hasn't been looked at for #DBusServer
 * @todo Need notification to apps of disconnection, may matter for some transports
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

  server->timeouts = _dbus_timeout_list_new ();
  if (server->timeouts == NULL)
    {
      _dbus_watch_list_free (server->watches);
      server->watches = NULL;
      return FALSE;
    }
  
  server->connection_counter = _dbus_counter_new ();
  if (server->connection_counter == NULL)
    {
      _dbus_watch_list_free (server->watches);
      server->watches = NULL;
      _dbus_timeout_list_free (server->timeouts);
      server->timeouts = NULL;
      
      return FALSE;
    }

  server->max_connections = 256; /* same as an X server, seems like a nice default */

  _dbus_data_slot_list_init (&server->slot_list);
  
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
  /* calls out to application code... */
  _dbus_data_slot_list_free (&server->slot_list);

  dbus_server_set_new_connection_function (server, NULL, NULL, NULL);

  if (!server->disconnected)
    dbus_server_disconnect (server);

  _dbus_watch_list_free (server->watches);
  _dbus_timeout_list_free (server->timeouts);
  _dbus_counter_unref (server->connection_counter);
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

/**
 * Toggles a watch and notifies app via server's
 * DBusWatchToggledFunction if available. It's an error to call this
 * function on a watch that was not previously added.
 *
 * @param server the server.
 * @param watch the watch to toggle.
 * @param enabled whether to enable or disable
 */
void
_dbus_server_toggle_watch (DBusServer  *server,
                           DBusWatch   *watch,
                           dbus_bool_t  enabled)
{
  if (server->watches) /* null during finalize */
    _dbus_watch_list_toggle_watch (server->watches,
                                   watch, enabled);
}

/**
 * Adds a timeout for this server, chaining out to
 * application-provided timeout handlers. The timeout should be
 * repeatedly handled with dbus_timeout_handle() at its given interval
 * until it is removed.
 *
 * @param server the server.
 * @param timeout the timeout to add.
 */
dbus_bool_t
_dbus_server_add_timeout (DBusServer  *server,
			  DBusTimeout *timeout)
{
  return _dbus_timeout_list_add_timeout (server->timeouts, timeout);
}

/**
 * Removes a timeout previously added with _dbus_server_add_timeout().
 *
 * @param server the server.
 * @param timeout the timeout to remove.
 */
void
_dbus_server_remove_timeout (DBusServer  *server,
			     DBusTimeout *timeout)
{
  _dbus_timeout_list_remove_timeout (server->timeouts, timeout);  
}

/**
 * Toggles a timeout and notifies app via server's
 * DBusTimeoutToggledFunction if available. It's an error to call this
 * function on a timeout that was not previously added.
 *
 * @param server the server.
 * @param timeout the timeout to toggle.
 * @param enabled whether to enable or disable
 */
void
_dbus_server_toggle_timeout (DBusServer  *server,
                             DBusTimeout *timeout,
                             dbus_bool_t  enabled)
{
  if (server->timeouts) /* null during finalize */
    _dbus_timeout_list_toggle_timeout (server->timeouts,
                                       timeout, enabled);
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
 * @todo error messages on bad address could really be better.
 * DBusResultCode is a bit limiting here.
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
  DBusAddressEntry **entries;
  int len, i;
  
  if (!dbus_parse_address (address, &entries, &len, result))
    return NULL;

  server = NULL;
  
  for (i = 0; i < len; i++)
    {
      const char *method = dbus_address_entry_get_method (entries[i]);

      if (strcmp (method, "unix") == 0)
	{
	  const char *path = dbus_address_entry_get_value (entries[i], "path");

	  if (path == NULL)
	    goto bad_address;

	  server = _dbus_server_new_for_domain_socket (path, result);

	  if (server)
	    break;
	}
      else if (strcmp (method, "tcp") == 0)
	{
	  const char *host = dbus_address_entry_get_value (entries[i], "host");
          const char *port = dbus_address_entry_get_value (entries[i], "port");
          DBusString  str;
          long lport;
          dbus_bool_t sresult;
          
	  if (port == NULL)
	    goto bad_address;

          _dbus_string_init_const (&str, port);
          sresult = _dbus_string_parse_int (&str, 0, &lport, NULL);
          _dbus_string_free (&str);
          
          if (sresult == FALSE || lport <= 0 || lport > 65535)
            goto bad_address;
          
	  server = _dbus_server_new_for_tcp_socket (host, lport, result);

	  if (server)
	    break;
	}
#ifdef DBUS_BUILD_TESTS
      else if (strcmp (method, "debug") == 0)
	{
	  const char *name = dbus_address_entry_get_value (entries[i], "name");

	  if (name == NULL)
	    goto bad_address;

	  server = _dbus_server_debug_new (name, result);

	  if (server)
	    break;
	}
      else if (strcmp (method, "debug-pipe") == 0)
	{
	  const char *name = dbus_address_entry_get_value (entries[i], "name");

	  if (name == NULL)
	    goto bad_address;

	  server = _dbus_server_debug_pipe_new (name, result);

	  if (server)
	    break;
	}
#endif
      else
        goto bad_address;
    }
  
  dbus_address_entries_free (entries);
  return server;

 bad_address:
  dbus_address_entries_free (entries);
  dbus_set_result (result, DBUS_RESULT_BAD_ADDRESS);

  return NULL;
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
 * @param toggled_function function to notify when the watch is enabled/disabled
 * @param data data to pass to add_function and remove_function.
 * @param free_data_function function to be called to free the data.
 * @returns #FALSE on failure (no memory)
 */
dbus_bool_t
dbus_server_set_watch_functions (DBusServer              *server,
                                 DBusAddWatchFunction     add_function,
                                 DBusRemoveWatchFunction  remove_function,
                                 DBusWatchToggledFunction toggled_function,
                                 void                    *data,
                                 DBusFreeFunction         free_data_function)
{
  return _dbus_watch_list_set_functions (server->watches,
                                         add_function,
                                         remove_function,
                                         toggled_function,
                                         data,
                                         free_data_function);
}

/**
 * Sets the timeout functions for the connection. These functions are
 * responsible for making the application's main loop aware of timeouts.
 *
 * This function behaves exactly like dbus_connection_set_timeout_functions();
 * see the documentation for that routine.
 *
 * @param server the server.
 * @param add_function function to add a timeout.
 * @param remove_function function to remove a timeout.
 * @param toggled_function function to notify when the timeout is enabled/disabled
 * @param data data to pass to add_function and remove_function.
 * @param free_data_function function to be called to free the data.
 * @returns #FALSE on failure (no memory)
 */
dbus_bool_t
dbus_server_set_timeout_functions (DBusServer                *server,
				   DBusAddTimeoutFunction     add_function,
				   DBusRemoveTimeoutFunction  remove_function,
                                   DBusTimeoutToggledFunction toggled_function,
				   void                      *data,
				   DBusFreeFunction           free_data_function)
{
  return _dbus_timeout_list_set_functions (server->timeouts,
                                           add_function, remove_function,
                                           toggled_function,
                                           data, free_data_function); 
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

/**
 * Sets the maximum number of connections that can be open at one
 * time for this server. If the maximum is reached, and another
 * client tries to connect, then the oldest unauthenticated client
 * will be dropped. If no unauthenticated client exists, then
 * the new connection will be refused.
 *
 * If the maximum is set to a number lower than the current
 * number of connections, no current connections are
 * disconnected.
 *
 * @todo honoring max_connections has not been implemented
 * yet. The only real work involved is keeping a list
 * of live connections on the DBusServer so the oldest
 * unauthenticated client can be located when required.
 * 
 * @todo for a systemwide daemon, we need a max number of connections
 * per user, since any user can authenticate a bunch of connections
 * and create a DOS.
 *
 * @todo a single process might listen on multiple mechanisms
 * (multiple DBusServer) and might want the max connections
 * value to span all those servers. Should consider
 * changing the API accordingly, though I'm inclined to
 * punt this to the app that wants to do it instead of
 * putting it in the library.
 * 
 * @param server the server
 * @param max_connections maximum number of connections allowed
 */
void
dbus_server_set_max_connections (DBusServer *server,
                                 int         max_connections)
{
  server->max_connections = max_connections;
}

/**
 * Gets the maximum number of connections that can be active
 * at a time for this server.
 *
 * @param server the server
 * @returns maximum number of connections at once
 */
int
dbus_server_get_max_connections (DBusServer *server)
{
  return server->max_connections;
}

/**
 * Gets the number of #DBusConnection to this server that
 * have not yet been finalized. i.e. all #DBusConnection that
 * were passed to #DBusNewConnectionFunction and have not yet been
 * finalized will count in this total.
 *
 * @param server the server
 * @returns the number of connections
 */
int
dbus_server_get_n_connections (DBusServer *server)
{
  return _dbus_counter_get_value (server->connection_counter);
}


static DBusDataSlotAllocator slot_allocator;

/**
 * Initialize the mutex used for #DBusConnection data
 * slot reservations.
 *
 * @returns the mutex
 */
DBusMutex *
_dbus_server_slots_init_lock (void)
{
  if (!_dbus_data_slot_allocator_init (&slot_allocator))
    return NULL;
  else
    return slot_allocator.lock;
}

/**
 * Allocates an integer ID to be used for storing application-specific
 * data on any DBusServer. The allocated ID may then be used
 * with dbus_server_set_data() and dbus_server_get_data().
 * If allocation fails, -1 is returned. Again, the allocated
 * slot is global, i.e. all DBusServer objects will
 * have a slot with the given integer ID reserved.
 *
 * @returns -1 on failure, otherwise the data slot ID
 */
int
dbus_server_allocate_data_slot (void)
{
  return _dbus_data_slot_allocator_alloc (&slot_allocator);
}

/**
 * Deallocates a global ID for server data slots.
 * dbus_server_get_data() and dbus_server_set_data()
 * may no longer be used with this slot.
 * Existing data stored on existing DBusServer objects
 * will be freed when the server is finalized,
 * but may not be retrieved (and may only be replaced
 * if someone else reallocates the slot).
 *
 * @param slot the slot to deallocate
 */
void
dbus_server_free_data_slot (int slot)
{
  _dbus_data_slot_allocator_free (&slot_allocator, slot);
}

/**
 * Stores a pointer on a DBusServer, along
 * with an optional function to be used for freeing
 * the data when the data is set again, or when
 * the server is finalized. The slot number
 * must have been allocated with dbus_server_allocate_data_slot().
 *
 * @param server the server
 * @param slot the slot number
 * @param data the data to store
 * @param free_data_func finalizer function for the data
 * @returns #TRUE if there was enough memory to store the data
 */
dbus_bool_t
dbus_server_set_data (DBusServer   *server,
                      int               slot,
                      void             *data,
                      DBusFreeFunction  free_data_func)
{
  DBusFreeFunction old_free_func;
  void *old_data;
  dbus_bool_t retval;

#if 0
  dbus_mutex_lock (server->mutex);
#endif
  
  retval = _dbus_data_slot_list_set (&slot_allocator,
                                     &server->slot_list,
                                     slot, data, free_data_func,
                                     &old_free_func, &old_data);

#if 0
  dbus_mutex_unlock (server->mutex);
#endif
  
  if (retval)
    {
      /* Do the actual free outside the server lock */
      if (old_free_func)
        (* old_free_func) (old_data);
    }

  return retval;
}

/**
 * Retrieves data previously set with dbus_server_set_data().
 * The slot must still be allocated (must not have been freed).
 *
 * @param server the server
 * @param slot the slot to get data from
 * @returns the data, or #NULL if not found
 */
void*
dbus_server_get_data (DBusServer   *server,
                      int               slot)
{
  void *res;
  
#if 0
  dbus_mutex_lock (server->mutex);
#endif
  
  res = _dbus_data_slot_list_get (&slot_allocator,
                                  &server->slot_list,
                                  slot);

#if 0
  dbus_mutex_unlock (server->mutex);
#endif
  
  return res;
}

/** @} */

