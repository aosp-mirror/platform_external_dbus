/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-transport.c DBusTransport object (internal to D-BUS implementation)
 *
 * Copyright (C) 2002, 2003  Red Hat Inc.
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

#include "dbus-transport-protected.h"
#include "dbus-transport-unix.h"
#include "dbus-connection-internal.h"
#include "dbus-watch.h"
#include "dbus-auth.h"
#include "dbus-address.h"
#ifdef DBUS_BUILD_TESTS
#include "dbus-transport-debug.h"
#include "dbus-server-debug-pipe.h"
#endif

/**
 * @defgroup DBusTransport DBusTransport object
 * @ingroup  DBusInternals
 * @brief "Backend" for a DBusConnection.
 *
 * Types and functions related to DBusTransport.  A transport is an
 * abstraction that can send and receive data via various kinds of
 * network connections or other IPC mechanisms.
 * 
 * @{
 */

/**
 * @typedef DBusTransport
 *
 * Opaque object representing a way message stream.
 * DBusTransport abstracts various kinds of actual
 * transport mechanism, such as different network protocols,
 * or encryption schemes.
 */

static void
live_messages_size_notify (DBusCounter *counter,
                           void        *user_data)
{
  DBusTransport *transport = user_data;

  _dbus_transport_ref (transport);

#if 0
  _dbus_verbose ("Counter value is now %d\n",
                 (int) _dbus_counter_get_value (counter));
#endif
  
  /* disable or re-enable the read watch for the transport if
   * required.
   */
  if (* transport->vtable->live_messages_changed)
    (* transport->vtable->live_messages_changed) (transport);

  _dbus_transport_unref (transport);
}

/**
 * Initializes the base class members of DBusTransport.
 * Chained up to by subclasses in their constructor.
 *
 * @param transport the transport being created.
 * @param vtable the subclass vtable.
 * @param server #TRUE if this transport is on the server side of a connection
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_transport_init_base (DBusTransport             *transport,
                           const DBusTransportVTable *vtable,
                           dbus_bool_t                server)
{
  DBusMessageLoader *loader;
  DBusAuth *auth;
  DBusCounter *counter;
  
  loader = _dbus_message_loader_new ();
  if (loader == NULL)
    return FALSE;
  
  if (server)
    auth = _dbus_auth_server_new ();
  else
    auth = _dbus_auth_client_new ();
  if (auth == NULL)
    {
      _dbus_message_loader_unref (loader);
      return FALSE;
    }

  counter = _dbus_counter_new ();
  if (counter == NULL)
    {
      _dbus_auth_unref (auth);
      _dbus_message_loader_unref (loader);
      return FALSE;
    }
  
  transport->refcount = 1;
  transport->vtable = vtable;
  transport->loader = loader;
  transport->auth = auth;
  transport->live_messages_size = counter;
  transport->authenticated = FALSE;
  transport->messages_need_sending = FALSE;
  transport->disconnected = FALSE;
  transport->send_credentials_pending = !server;
  transport->receive_credentials_pending = server;
  transport->is_server = server;

  /* Try to default to something that won't totally hose the system,
   * but doesn't impose too much of a limitation.
   */
  transport->max_live_messages_size = _DBUS_ONE_MEGABYTE * 63;
  
  transport->credentials.pid = -1;
  transport->credentials.uid = -1;
  transport->credentials.gid = -1;

  _dbus_counter_set_notify (transport->live_messages_size,
                            transport->max_live_messages_size,
                            live_messages_size_notify,
                            transport);
  
  return TRUE;
}

/**
 * Finalizes base class members of DBusTransport.
 * Chained up to from subclass finalizers.
 *
 * @param transport the transport.
 */
void
_dbus_transport_finalize_base (DBusTransport *transport)
{
  if (!transport->disconnected)
    _dbus_transport_disconnect (transport);
  
  _dbus_message_loader_unref (transport->loader);
  _dbus_auth_unref (transport->auth);
  _dbus_counter_set_notify (transport->live_messages_size,
                            0, NULL, NULL);
  _dbus_counter_unref (transport->live_messages_size);
}

/**
 * Opens a new transport for the given address.  (This opens a
 * client-side-of-the-connection transport.)
 *
 * @todo error messages on bad address could really be better.
 * DBusResultCode is a bit limiting here.
 * 
 * @param address the address.
 * @param result location to store reason for failure.
 * @returns new transport of #NULL on failure.
 */
DBusTransport*
_dbus_transport_open (const char     *address,
                      DBusResultCode *result)
{
  DBusTransport *transport;
  DBusAddressEntry **entries;
  int len, i;
  
  if (!dbus_parse_address (address, &entries, &len, result))
    return NULL;

  transport = NULL;
  
  for (i = 0; i < len; i++)
    {
      const char *method = dbus_address_entry_get_method (entries[i]);

      if (strcmp (method, "unix") == 0)
	{
	  const char *path = dbus_address_entry_get_value (entries[i], "path");

	  if (path == NULL)
	    goto bad_address;

	  transport = _dbus_transport_new_for_domain_socket (path, FALSE, result);
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
          
	  transport = _dbus_transport_new_for_tcp_socket (host, lport, FALSE, result);
	}
#ifdef DBUS_BUILD_TESTS
      else if (strcmp (method, "debug") == 0)
	{
	  const char *name = dbus_address_entry_get_value (entries[i], "name");

	  if (name == NULL)
	    goto bad_address;

	  transport = _dbus_transport_debug_client_new (name, result);
	}
      else if (strcmp (method, "debug-pipe") == 0)
	{
	  const char *name = dbus_address_entry_get_value (entries[i], "name");

	  if (name == NULL)
	    goto bad_address;

	  transport = _dbus_transport_debug_pipe_new (name, result);
	}
#endif
      else
	goto bad_address;

      if (transport)
	break;	  
    }
  
  dbus_address_entries_free (entries);
  return transport;

 bad_address:
  dbus_address_entries_free (entries);
  dbus_set_result (result, DBUS_RESULT_BAD_ADDRESS);

  return NULL;
}

/**
 * Increments the reference count for the transport.
 *
 * @param transport the transport.
 */
void
_dbus_transport_ref (DBusTransport *transport)
{
  transport->refcount += 1;
}

/**
 * Decrements the reference count for the transport.
 * Disconnects and finalizes the transport if
 * the reference count reaches zero.
 *
 * @param transport the transport.
 */
void
_dbus_transport_unref (DBusTransport *transport)
{
  _dbus_assert (transport != NULL);
  _dbus_assert (transport->refcount > 0);

  transport->refcount -= 1;
  if (transport->refcount == 0)
    {
      _dbus_assert (transport->vtable->finalize != NULL);
      
      (* transport->vtable->finalize) (transport);
    }
}

/**
 * Closes our end of the connection to a remote application. Further
 * attempts to use this transport will fail. Only the first call to
 * _dbus_transport_disconnect() will have an effect.
 *
 * @param transport the transport.
 * 
 */
void
_dbus_transport_disconnect (DBusTransport *transport)
{
  _dbus_assert (transport->vtable->disconnect != NULL);

  if (transport->disconnected)
    return;

  _dbus_transport_ref (transport);
  (* transport->vtable->disconnect) (transport);
  
  transport->disconnected = TRUE;

  _dbus_connection_notify_disconnected (transport->connection);
  
  _dbus_transport_unref (transport);
}

/**
 * Returns #TRUE if the transport has not been disconnected.
 * Disconnection can result from _dbus_transport_disconnect()
 * or because the server drops its end of the connection.
 *
 * @param transport the transport.
 * @returns whether we're connected
 */
dbus_bool_t
_dbus_transport_get_is_connected (DBusTransport *transport)
{
  return !transport->disconnected;
}

/**
 * Returns #TRUE if we have been authenticated.  Will return #TRUE
 * even if the transport is disconnected.
 *
 * @param transport the transport
 * @returns whether we're authenticated
 */
dbus_bool_t
_dbus_transport_get_is_authenticated (DBusTransport *transport)
{  
  if (transport->authenticated)
    return TRUE;
  else
    {
      if (transport->disconnected)
        return FALSE;
      
      transport->authenticated =
        (!(transport->send_credentials_pending ||
           transport->receive_credentials_pending)) &&
        _dbus_auth_do_work (transport->auth) == DBUS_AUTH_STATE_AUTHENTICATED;

      /* If we've authenticated as some identity, check that the auth
       * identity is the same as our own identity.  In the future, we
       * may have API allowing applications to specify how this is
       * done, for example they may allow connection as any identity,
       * but then impose restrictions on certain identities.
       * Or they may give certain identities extra privileges.
       */
      
      if (transport->authenticated && transport->is_server)
        {
          DBusCredentials auth_identity;
          DBusCredentials our_identity;

          _dbus_credentials_from_current_process (&our_identity);
          _dbus_auth_get_identity (transport->auth, &auth_identity);
          
          if (!_dbus_credentials_match (&our_identity,
                                        &auth_identity))
            {
              _dbus_verbose ("Client authorized as UID %d but our UID is %d, disconnecting\n",
                             auth_identity.uid, our_identity.uid);
              _dbus_transport_disconnect (transport);
              return FALSE;
            }
          else
            {
              _dbus_verbose ("Client authorized as UID %d matching our UID %d\n",
                             auth_identity.uid, our_identity.uid);
            }
        }
      
      return transport->authenticated;
    }
}

/**
 * Handles a watch by reading data, writing data, or disconnecting
 * the transport, as appropriate for the given condition.
 *
 * @param transport the transport.
 * @param watch the watch.
 * @param condition the current state of the watched file descriptor.
 */
void
_dbus_transport_handle_watch (DBusTransport           *transport,
                              DBusWatch               *watch,
                              unsigned int             condition)
{
  _dbus_assert (transport->vtable->handle_watch != NULL);

  if (transport->disconnected)
    return;

  if (dbus_watch_get_fd (watch) < 0)
    {
      _dbus_warn ("Tried to handle an invalidated watch; this watch should have been removed\n");
      return;
    }
  
  _dbus_watch_sanitize_condition (watch, &condition);

  _dbus_transport_ref (transport);
  _dbus_watch_ref (watch);
  (* transport->vtable->handle_watch) (transport, watch, condition);
  _dbus_watch_unref (watch);
  _dbus_transport_unref (transport);
}

/**
 * Sets the connection using this transport. Allows the transport
 * to add watches to the connection, queue incoming messages,
 * and pull outgoing messages.
 *
 * @param transport the transport.
 * @param connection the connection.
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
_dbus_transport_set_connection (DBusTransport  *transport,
                                DBusConnection *connection)
{
  _dbus_assert (transport->vtable->connection_set != NULL);
  _dbus_assert (transport->connection == NULL);
  
  transport->connection = connection;

  _dbus_transport_ref (transport);
  if (!(* transport->vtable->connection_set) (transport))
    transport->connection = NULL;
  _dbus_transport_unref (transport);

  return transport->connection != NULL;
}

/**
 * Notifies the transport when the outgoing message queue goes from
 * empty to non-empty or vice versa. Typically causes the transport to
 * add or remove its DBUS_WATCH_WRITABLE watch.
 *
 * @param transport the transport.
 * @param queue_length the length of the outgoing message queue.
 *
 */
void
_dbus_transport_messages_pending (DBusTransport  *transport,
                                  int             queue_length)
{
  _dbus_assert (transport->vtable->messages_pending != NULL);

  if (transport->disconnected)
    return;

  transport->messages_need_sending = queue_length > 0;

  _dbus_transport_ref (transport);
  (* transport->vtable->messages_pending) (transport,
                                           queue_length);
  _dbus_transport_unref (transport);
}

/**
 * Performs a single poll()/select() on the transport's file
 * descriptors and then reads/writes data as appropriate,
 * queueing incoming messages and sending outgoing messages.
 * This is the backend for _dbus_connection_do_iteration().
 * See _dbus_connection_do_iteration() for full details.
 *
 * @param transport the transport.
 * @param flags indicates whether to read or write, and whether to block.
 * @param timeout_milliseconds if blocking, timeout or -1 for no timeout.
 */
void
_dbus_transport_do_iteration (DBusTransport  *transport,
                              unsigned int    flags,
                              int             timeout_milliseconds)
{
  _dbus_assert (transport->vtable->do_iteration != NULL);

  _dbus_verbose ("Transport iteration flags 0x%x timeout %d connected = %d\n",
                 flags, timeout_milliseconds, !transport->disconnected);
  
  if ((flags & (DBUS_ITERATION_DO_WRITING |
                DBUS_ITERATION_DO_READING)) == 0)
    return; /* Nothing to do */

  if (transport->disconnected)
    return;

  _dbus_transport_ref (transport);
  (* transport->vtable->do_iteration) (transport, flags,
                                       timeout_milliseconds);
  _dbus_transport_unref (transport);
}

/**
 * Reports our current dispatch status (whether there's buffered
 * data to be queued as messages, or not, or we need memory).
 *
 * @param transport the transport
 * @returns current status
 */
DBusDispatchStatus
_dbus_transport_get_dispatch_status (DBusTransport *transport)
{
  if (_dbus_counter_get_value (transport->live_messages_size) >= transport->max_live_messages_size)
    return DBUS_DISPATCH_COMPLETE; /* complete for now */

  if (!_dbus_message_loader_queue_messages (transport->loader))
    return DBUS_DISPATCH_NEED_MEMORY;

  if (_dbus_message_loader_peek_message (transport->loader) != NULL)
    return DBUS_DISPATCH_DATA_REMAINS;
  else
    return DBUS_DISPATCH_COMPLETE;
}

/**
 * Processes data we've read while handling a watch, potentially
 * converting some of it to messages and queueing those messages on
 * the connection.
 *
 * @param transport the transport
 * @returns #TRUE if we had enough memory to queue all messages
 */
dbus_bool_t
_dbus_transport_queue_messages (DBusTransport *transport)
{
  DBusDispatchStatus status;
  
  /* Queue any messages */
  while ((status = _dbus_transport_get_dispatch_status (transport)) == DBUS_DISPATCH_DATA_REMAINS)
    {
      DBusMessage *message;
      DBusList *link;

      link = _dbus_message_loader_pop_message_link (transport->loader);
      _dbus_assert (link != NULL);
      
      message = link->data;
      
      _dbus_verbose ("queueing received message %p\n", message);

      _dbus_message_add_size_counter (message, transport->live_messages_size);

      /* pass ownership of link and message ref to connection */
      _dbus_connection_queue_received_message_link (transport->connection,
                                                    link);
    }

  if (_dbus_message_loader_get_is_corrupted (transport->loader))
    {
      _dbus_verbose ("Corrupted message stream, disconnecting\n");
      _dbus_transport_disconnect (transport);
    }

  return status != DBUS_DISPATCH_NEED_MEMORY;
}

/**
 * See dbus_connection_set_max_message_size().
 *
 * @param transport the transport
 * @param size the max size of a single message
 */
void
_dbus_transport_set_max_message_size (DBusTransport  *transport,
                                      long            size)
{
  _dbus_message_loader_set_max_message_size (transport->loader, size);
}

/**
 * See dbus_connection_get_max_message_size().
 *
 * @param transport the transport
 * @returns max message size
 */
long
_dbus_transport_get_max_message_size (DBusTransport  *transport)
{
  return _dbus_message_loader_get_max_message_size (transport->loader);
}

/**
 * See dbus_connection_set_max_live_messages_size().
 *
 * @param transport the transport
 * @param size the max size of all incoming messages
 */
void
_dbus_transport_set_max_live_messages_size (DBusTransport  *transport,
                                            long            size)
{
  transport->max_live_messages_size = size;
  _dbus_counter_set_notify (transport->live_messages_size,
                            transport->max_live_messages_size,
                            live_messages_size_notify,
                            transport);
}


/**
 * See dbus_connection_get_max_live_messages_size().
 *
 * @param transport the transport
 * @returns max bytes for all live messages
 */
long
_dbus_transport_get_max_live_messages_size (DBusTransport  *transport)
{
  return transport->max_live_messages_size;
}

/** @} */
