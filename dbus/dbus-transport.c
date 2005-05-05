/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-transport.c DBusTransport object (internal to D-BUS implementation)
 *
 * Copyright (C) 2002, 2003  Red Hat Inc.
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

#include "dbus-transport-protected.h"
#include "dbus-transport-unix.h"
#include "dbus-connection-internal.h"
#include "dbus-watch.h"
#include "dbus-auth.h"
#include "dbus-address.h"
#ifdef DBUS_BUILD_TESTS
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
 * Initializes the base class members of DBusTransport.  Chained up to
 * by subclasses in their constructor.  The server GUID is the
 * globally unique ID for the server creating this connection
 * and will be #NULL for the client side of a connection. The GUID
 * is in hex format.
 *
 * @param transport the transport being created.
 * @param vtable the subclass vtable.
 * @param server_guid non-#NULL if this transport is on the server side of a connection
 * @param address the address of the transport
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_transport_init_base (DBusTransport             *transport,
                           const DBusTransportVTable *vtable,
                           const DBusString          *server_guid,
                           const DBusString          *address)
{
  DBusMessageLoader *loader;
  DBusAuth *auth;
  DBusCounter *counter;
  char *address_copy;
  
  loader = _dbus_message_loader_new ();
  if (loader == NULL)
    return FALSE;
  
  if (server_guid)
    auth = _dbus_auth_server_new (server_guid);
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
  
  if (server_guid)
    {
      _dbus_assert (address == NULL);
      address_copy = NULL;
    }
  else
    {
      _dbus_assert (address != NULL);

      if (!_dbus_string_copy_data (address, &address_copy))
        {
          _dbus_counter_unref (counter);
          _dbus_auth_unref (auth);
          _dbus_message_loader_unref (loader);
          return FALSE;
        }
    }
  
  transport->refcount = 1;
  transport->vtable = vtable;
  transport->loader = loader;
  transport->auth = auth;
  transport->live_messages_size = counter;
  transport->authenticated = FALSE;
  transport->disconnected = FALSE;
  transport->is_server = (server_guid != NULL);
  transport->send_credentials_pending = !transport->is_server;
  transport->receive_credentials_pending = transport->is_server;
  transport->address = address_copy;
  
  transport->unix_user_function = NULL;
  transport->unix_user_data = NULL;
  transport->free_unix_user_data = NULL;

  transport->expected_guid = NULL;
  
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

  if (transport->address)
    _dbus_verbose ("Initialized transport on address %s\n", transport->address);
  
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

  if (transport->free_unix_user_data != NULL)
    (* transport->free_unix_user_data) (transport->unix_user_data);
  
  _dbus_message_loader_unref (transport->loader);
  _dbus_auth_unref (transport->auth);
  _dbus_counter_set_notify (transport->live_messages_size,
                            0, NULL, NULL);
  _dbus_counter_unref (transport->live_messages_size);
  dbus_free (transport->address);
  dbus_free (transport->expected_guid);
}

/**
 * Try to open a new transport for the given address entry.  (This
 * opens a client-side-of-the-connection transport.)
 * 
 * @param entry the address entry
 * @param error location to store reason for failure.
 * @returns new transport of #NULL on failure.
 */
DBusTransport*
_dbus_transport_open (DBusAddressEntry *entry,
                      DBusError        *error)
{
  DBusTransport *transport;
  const char *address_problem_type;
  const char *address_problem_field;
  const char *address_problem_other;
  const char *method;
  const char *expected_guid_orig;
  char *expected_guid;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  transport = NULL;
  address_problem_type = NULL;
  address_problem_field = NULL;
  address_problem_other = NULL;
  expected_guid_orig = dbus_address_entry_get_value (entry, "guid");
  expected_guid = _dbus_strdup (expected_guid_orig);

  if (expected_guid_orig != NULL && expected_guid == NULL)
    {
      _DBUS_SET_OOM (error);
      return NULL;
    }
  
  method = dbus_address_entry_get_method (entry);
  _dbus_assert (method != NULL);
      
  if (strcmp (method, "unix") == 0)
    {
      const char *path = dbus_address_entry_get_value (entry, "path");
      const char *tmpdir = dbus_address_entry_get_value (entry, "tmpdir");
      const char *abstract = dbus_address_entry_get_value (entry, "abstract");
          
      if (tmpdir != NULL)
        {
          address_problem_other = "cannot use the \"tmpdir\" option for an address to connect to, only in an address to listen on";
          goto bad_address;
        }
          
      if (path == NULL && abstract == NULL)
        {
          address_problem_type = "unix";
          address_problem_field = "path or abstract";  
          goto bad_address;
        }

      if (path != NULL && abstract != NULL)
        {
          address_problem_other = "can't specify both \"path\" and \"abstract\" options in an address";
          goto bad_address;
        }

      if (path)
        transport = _dbus_transport_new_for_domain_socket (path, FALSE,
                                                           error);
      else
        transport = _dbus_transport_new_for_domain_socket (abstract, TRUE,
                                                           error);
    }
  else if (strcmp (method, "tcp") == 0)
    {
      const char *host = dbus_address_entry_get_value (entry, "host");
      const char *port = dbus_address_entry_get_value (entry, "port");
      DBusString  str;
      long lport;
      dbus_bool_t sresult;
          
      if (port == NULL)
        {
          address_problem_type = "tcp";
          address_problem_field = "port";
          goto bad_address;
        }

      _dbus_string_init_const (&str, port);
      sresult = _dbus_string_parse_int (&str, 0, &lport, NULL);
      _dbus_string_free (&str);
          
      if (sresult == FALSE || lport <= 0 || lport > 65535)
        {
          address_problem_other = "Port is not an integer between 0 and 65535";
          goto bad_address;
        }
          
      transport = _dbus_transport_new_for_tcp_socket (host, lport, error);
    }
#ifdef DBUS_BUILD_TESTS
  else if (strcmp (method, "debug-pipe") == 0)
    {
      const char *name = dbus_address_entry_get_value (entry, "name");

      if (name == NULL)
        {
          address_problem_type = "debug-pipe";
          address_problem_field = "name";
          goto bad_address;
        }
          
      transport = _dbus_transport_debug_pipe_new (name, error);
    }
#endif
  else
    {
      address_problem_other = "Unknown address type (examples of valid types are \"unix\" and \"tcp\")";
      goto bad_address;
    }

  if (transport == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      dbus_free (expected_guid);
    }
  else
    {
      transport->expected_guid = expected_guid;
    }
  
  return transport;
  
 bad_address:
  dbus_free (expected_guid);
  
  if (address_problem_type != NULL)
    dbus_set_error (error, DBUS_ERROR_BAD_ADDRESS,
                    "Address of type %s was missing argument %s",
                    address_problem_type, address_problem_field);
  else
    dbus_set_error (error, DBUS_ERROR_BAD_ADDRESS,
                    "Could not parse address: %s",
                    address_problem_other);

  return NULL;
}

/**
 * Increments the reference count for the transport.
 *
 * @param transport the transport.
 * @returns the transport.
 */
DBusTransport *
_dbus_transport_ref (DBusTransport *transport)
{
  _dbus_assert (transport->refcount > 0);
  
  transport->refcount += 1;

  return transport;
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
      _dbus_verbose ("%s: finalizing\n", _DBUS_FUNCTION_NAME);
      
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
  _dbus_verbose ("%s start\n", _DBUS_FUNCTION_NAME);
  
  _dbus_assert (transport->vtable->disconnect != NULL);
  
  if (transport->disconnected)
    return;

  (* transport->vtable->disconnect) (transport);
  
  transport->disconnected = TRUE;

  _dbus_verbose ("%s end\n", _DBUS_FUNCTION_NAME);
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
 * @todo we drop connection->mutex when calling the unix_user_function,
 * which may not be safe really.
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
      dbus_bool_t maybe_authenticated;
      
      if (transport->disconnected)
        return FALSE;

      /* paranoia ref since we call user callbacks sometimes */
      _dbus_connection_ref_unlocked (transport->connection);
      
      maybe_authenticated =
        (!(transport->send_credentials_pending ||
           transport->receive_credentials_pending));

      if (maybe_authenticated)
        {
          switch (_dbus_auth_do_work (transport->auth))
            {
            case DBUS_AUTH_STATE_AUTHENTICATED:
              /* leave as maybe_authenticated */
              break;
            default:
              maybe_authenticated = FALSE;
            }
        }

      if (maybe_authenticated && !transport->is_server)
        {
          const char *server_guid;

          server_guid = _dbus_auth_get_guid_from_server (transport->auth);
          _dbus_assert (server_guid != NULL);

          if (transport->expected_guid &&
              strcmp (transport->expected_guid, server_guid) != 0)
            {
              _dbus_verbose ("Client expected GUID '%s' and we got '%s' from the server\n",
                             transport->expected_guid, server_guid);
              _dbus_transport_disconnect (transport);
              _dbus_connection_unref_unlocked (transport->connection);
              return FALSE;
            }

          if (transport->expected_guid == NULL)
            {
              transport->expected_guid = _dbus_strdup (server_guid);

              if (transport->expected_guid == NULL)
                {
                  _dbus_verbose ("No memory to complete auth in %s\n", _DBUS_FUNCTION_NAME);
                  return FALSE;
                }
            }
        }
      
      /* If we've authenticated as some identity, check that the auth
       * identity is the same as our own identity.  In the future, we
       * may have API allowing applications to specify how this is
       * done, for example they may allow connection as any identity,
       * but then impose restrictions on certain identities.
       * Or they may give certain identities extra privileges.
       */
      
      if (maybe_authenticated && transport->is_server)
        {
          DBusCredentials auth_identity;

          _dbus_auth_get_identity (transport->auth, &auth_identity);

          if (transport->unix_user_function != NULL)
            {
              dbus_bool_t allow;
              DBusConnection *connection;
              DBusAllowUnixUserFunction unix_user_function;
              void *unix_user_data;
              
              /* Dropping the lock here probably isn't that safe. */

              connection = transport->connection;
              unix_user_function = transport->unix_user_function;
              unix_user_data = transport->unix_user_data;

              _dbus_verbose ("unlock %s\n", _DBUS_FUNCTION_NAME);
              _dbus_connection_unlock (connection);
              
              allow = (* unix_user_function) (connection,
                                              auth_identity.uid,
                                              unix_user_data);

              _dbus_verbose ("lock %s post unix user function\n", _DBUS_FUNCTION_NAME);
              _dbus_connection_lock (connection);

              if (allow)
                {
                  _dbus_verbose ("Client UID "DBUS_UID_FORMAT" authorized\n", auth_identity.uid);
                }
              else
                {
                  _dbus_verbose ("Client UID "DBUS_UID_FORMAT
                                 " was rejected, disconnecting\n",
                                 auth_identity.uid);
                  _dbus_transport_disconnect (transport);
                  _dbus_connection_unref_unlocked (connection);
                  return FALSE;
                }
            }
          else
            {
              DBusCredentials our_identity;
              
              _dbus_credentials_from_current_process (&our_identity);
              
              if (!_dbus_credentials_match (&our_identity,
                                            &auth_identity))
                {
                  _dbus_verbose ("Client authorized as UID "DBUS_UID_FORMAT
                                 " but our UID is "DBUS_UID_FORMAT", disconnecting\n",
                                 auth_identity.uid, our_identity.uid);
                  _dbus_transport_disconnect (transport);
                  _dbus_connection_unref_unlocked (transport->connection);
                  return FALSE;
                }
              else
                {
                  _dbus_verbose ("Client authorized as UID "DBUS_UID_FORMAT
                                 " matching our UID "DBUS_UID_FORMAT"\n",
                                 auth_identity.uid, our_identity.uid);
                }
            }
        }
      
      transport->authenticated = maybe_authenticated;

      _dbus_connection_unref_unlocked (transport->connection);
      return maybe_authenticated;
    }
}

/**
 * Gets the address of a transport. It will be
 * #NULL for a server-side transport.
 *
 * @param transport the transport
 * @returns transport's address
 */
const char*
_dbus_transport_get_address (DBusTransport *transport)
{
  return transport->address;
}

/**
 * Handles a watch by reading data, writing data, or disconnecting
 * the transport, as appropriate for the given condition.
 *
 * @param transport the transport.
 * @param watch the watch.
 * @param condition the current state of the watched file descriptor.
 * @returns #FALSE if not enough memory to fully handle the watch
 */
dbus_bool_t
_dbus_transport_handle_watch (DBusTransport           *transport,
                              DBusWatch               *watch,
                              unsigned int             condition)
{
  dbus_bool_t retval;
  
  _dbus_assert (transport->vtable->handle_watch != NULL);

  if (transport->disconnected)
    return TRUE;

  if (dbus_watch_get_fd (watch) < 0)
    {
      _dbus_warn ("Tried to handle an invalidated watch; this watch should have been removed\n");
      return TRUE;
    }
  
  _dbus_watch_sanitize_condition (watch, &condition);

  _dbus_transport_ref (transport);
  _dbus_watch_ref (watch);
  retval = (* transport->vtable->handle_watch) (transport, watch, condition);
  _dbus_watch_unref (watch);
  _dbus_transport_unref (transport);

  return retval;
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
 * Get the UNIX file descriptor, if any.
 *
 * @param transport the transport
 * @param fd_p pointer to fill in with the descriptor
 * @returns #TRUE if a descriptor was available
 */
dbus_bool_t
_dbus_transport_get_unix_fd (DBusTransport *transport,
                             int           *fd_p)
{
  dbus_bool_t retval;
  
  if (transport->vtable->get_unix_fd == NULL)
    return FALSE;

  if (transport->disconnected)
    return FALSE;

  _dbus_transport_ref (transport);

  retval = (* transport->vtable->get_unix_fd) (transport,
                                               fd_p);
  
  _dbus_transport_unref (transport);

  return retval;
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

  _dbus_verbose ("%s end\n", _DBUS_FUNCTION_NAME);
}

static dbus_bool_t
recover_unused_bytes (DBusTransport *transport)
{
  if (_dbus_auth_needs_decoding (transport->auth))
    {
      DBusString plaintext;
      const DBusString *encoded;
      DBusString *buffer;
      int orig_len;
      
      if (!_dbus_string_init (&plaintext))
        goto nomem;
      
      _dbus_auth_get_unused_bytes (transport->auth,
                                   &encoded);

      if (!_dbus_auth_decode_data (transport->auth,
                                   encoded, &plaintext))
        {
          _dbus_string_free (&plaintext);
          goto nomem;
        }
      
      _dbus_message_loader_get_buffer (transport->loader,
                                       &buffer);
      
      orig_len = _dbus_string_get_length (buffer);
      
      if (!_dbus_string_move (&plaintext, 0, buffer,
                              orig_len))
        {
          _dbus_string_free (&plaintext);
          goto nomem;
        }
      
      _dbus_verbose (" %d unused bytes sent to message loader\n", 
                     _dbus_string_get_length (buffer) -
                     orig_len);
      
      _dbus_message_loader_return_buffer (transport->loader,
                                          buffer,
                                          _dbus_string_get_length (buffer) -
                                          orig_len);

      _dbus_auth_delete_unused_bytes (transport->auth);
      
      _dbus_string_free (&plaintext);
    }
  else
    {
      const DBusString *bytes;
      DBusString *buffer;
      int orig_len;
      dbus_bool_t succeeded;

      _dbus_message_loader_get_buffer (transport->loader,
                                       &buffer);
                
      orig_len = _dbus_string_get_length (buffer);
                
      _dbus_auth_get_unused_bytes (transport->auth,
                                   &bytes);

      succeeded = TRUE;
      if (!_dbus_string_copy (bytes, 0, buffer, _dbus_string_get_length (buffer)))
        succeeded = FALSE;
      
      _dbus_verbose (" %d unused bytes sent to message loader\n", 
                     _dbus_string_get_length (buffer) -
                     orig_len);
      
      _dbus_message_loader_return_buffer (transport->loader,
                                          buffer,
                                          _dbus_string_get_length (buffer) -
                                          orig_len);

      if (succeeded)
        _dbus_auth_delete_unused_bytes (transport->auth);
      else
        goto nomem;
    }

  return TRUE;

 nomem:
  _dbus_verbose ("Not enough memory to transfer unused bytes from auth conversation\n");
  return FALSE;
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

  if (!_dbus_transport_get_is_authenticated (transport))
    {
      if (_dbus_auth_do_work (transport->auth) ==
          DBUS_AUTH_STATE_WAITING_FOR_MEMORY)
        return DBUS_DISPATCH_NEED_MEMORY;
      else if (!_dbus_transport_get_is_authenticated (transport))
        return DBUS_DISPATCH_COMPLETE;
    }

  if (!transport->unused_bytes_recovered &&
      !recover_unused_bytes (transport))
    return DBUS_DISPATCH_NEED_MEMORY;

  transport->unused_bytes_recovered = TRUE;
  
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

#if 0
  _dbus_verbose ("_dbus_transport_queue_messages()\n");
#endif
  
  /* Queue any messages */
  while ((status = _dbus_transport_get_dispatch_status (transport)) == DBUS_DISPATCH_DATA_REMAINS)
    {
      DBusMessage *message;
      DBusList *link;

      link = _dbus_message_loader_pop_message_link (transport->loader);
      _dbus_assert (link != NULL);
      
      message = link->data;
      
      _dbus_verbose ("queueing received message %p\n", message);

      if (!_dbus_message_add_size_counter (message, transport->live_messages_size))
        {
          _dbus_message_loader_putback_message_link (transport->loader,
                                                     link);
          status = DBUS_DISPATCH_NEED_MEMORY;
          break;
        }
      else
        {
          /* pass ownership of link and message ref to connection */
          _dbus_connection_queue_received_message_link (transport->connection,
                                                        link);
        }
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
 * See dbus_connection_set_max_received_size().
 *
 * @param transport the transport
 * @param size the max size of all incoming messages
 */
void
_dbus_transport_set_max_received_size (DBusTransport  *transport,
                                       long            size)
{
  transport->max_live_messages_size = size;
  _dbus_counter_set_notify (transport->live_messages_size,
                            transport->max_live_messages_size,
                            live_messages_size_notify,
                            transport);
}


/**
 * See dbus_connection_get_max_received_size().
 *
 * @param transport the transport
 * @returns max bytes for all live messages
 */
long
_dbus_transport_get_max_received_size (DBusTransport  *transport)
{
  return transport->max_live_messages_size;
}

/**
 * See dbus_connection_get_unix_user().
 *
 * @param transport the transport
 * @param uid return location for the user ID
 * @returns #TRUE if uid is filled in with a valid user ID
 */
dbus_bool_t
_dbus_transport_get_unix_user (DBusTransport *transport,
                               unsigned long *uid)
{
  DBusCredentials auth_identity;

  *uid = _DBUS_INT32_MAX; /* better than some root or system user in
                           * case of bugs in the caller. Caller should
                           * never use this value on purpose, however.
                           */
  
  if (!transport->authenticated)
    return FALSE;
  
  _dbus_auth_get_identity (transport->auth, &auth_identity);

  if (auth_identity.uid != DBUS_UID_UNSET)
    {
      *uid = auth_identity.uid;
      return TRUE;
    }
  else
    return FALSE;
}

/**
 * See dbus_connection_get_unix_process_id().
 *
 * @param transport the transport
 * @param pid return location for the process ID
 * @returns #TRUE if uid is filled in with a valid process ID
 */
dbus_bool_t
_dbus_transport_get_unix_process_id (DBusTransport *transport,
				     unsigned long *pid)
{
  DBusCredentials auth_identity;

  *pid = DBUS_PID_UNSET; /* Caller should never use this value on purpose,
			  * but we set it to a safe number, INT_MAX,
			  * just to root out possible bugs in bad callers.
			  */
  
  if (!transport->authenticated)
    return FALSE;
  
  _dbus_auth_get_identity (transport->auth, &auth_identity);

  if (auth_identity.pid != DBUS_PID_UNSET)
    {
      *pid = auth_identity.pid;
      return TRUE;
    }
  else
    return FALSE;
}

/**
 * See dbus_connection_set_unix_user_function().
 *
 * @param transport the transport
 * @param function the predicate
 * @param data data to pass to the predicate
 * @param free_data_function function to free the data
 * @param old_data the old user data to be freed
 * @param old_free_data_function old free data function to free it with
 */
void
_dbus_transport_set_unix_user_function (DBusTransport             *transport,
                                        DBusAllowUnixUserFunction  function,
                                        void                      *data,
                                        DBusFreeFunction           free_data_function,
                                        void                     **old_data,
                                        DBusFreeFunction          *old_free_data_function)
{  
  *old_data = transport->unix_user_data;
  *old_free_data_function = transport->free_unix_user_data;

  transport->unix_user_function = function;
  transport->unix_user_data = data;
  transport->free_unix_user_data = free_data_function;
}

/**
 * Sets the SASL authentication mechanisms supported by this transport.
 *
 * @param transport the transport
 * @param mechanisms the #NULL-terminated array of mechanisms
 *
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_transport_set_auth_mechanisms (DBusTransport  *transport,
                                     const char    **mechanisms)
{
  return _dbus_auth_set_mechanisms (transport->auth, mechanisms);
}


/** @} */
