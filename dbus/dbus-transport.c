/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-transport.c DBusTransport object (internal to D-BUS implementation)
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

#include "dbus-transport-protected.h"
#include "dbus-transport-unix.h"
#include "dbus-connection-internal.h"

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

/**
 * Initializes the base class members of DBusTransport.
 * Chained up to by subclasses in their constructor.
 *
 * @param transport the transport being created.
 * @param vtable the subclass vtable.
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_transport_init_base (DBusTransport             *transport,
                           const DBusTransportVTable *vtable)
{
  DBusMessageLoader *loader;

  loader = _dbus_message_loader_new ();
  if (loader == NULL)
    return FALSE;
  
  transport->refcount = 1;
  transport->vtable = vtable;
  transport->loader = loader;
  
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
}

/**
 * Opens a new transport for the given address.
 *
 * @todo right now the address is just a Unix domain socket path.
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
  
  /* FIXME parse the address - whatever format
   * we decide addresses are in - and find the
   * appropriate transport.
   */

  /* Pretend it's just a unix domain socket name for now */
  transport = _dbus_transport_new_for_domain_socket (address, result);
  
  return transport;
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
  
  (* transport->vtable->disconnect) (transport);

  transport->disconnected = TRUE;
}

/**
 * Returns #TRUE if the transport has not been disconnected.
 * Disconnection can result from _dbus_transport_disconnect()
 * or because the server drops its end of the connection.
 *
 * @param transport the transport.
 */
dbus_bool_t
_dbus_transport_get_is_connected (DBusTransport *transport)
{
  return !transport->disconnected;
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
    {
      _dbus_connection_transport_error (transport->connection,
                                        DBUS_RESULT_DISCONNECTED);
      return;
    }

  _dbus_watch_sanitize_condition (watch, &condition);
  
  (* transport->vtable->handle_watch) (transport, watch, condition);
}

/**
 * Sets the connection using this transport. Allows the transport
 * to add watches to the connection, queue incoming messages,
 * and pull outgoing messages.
 *
 * @param transport the transport.
 * @param connection the connection.
 */
void
_dbus_transport_set_connection (DBusTransport  *transport,
                                DBusConnection *connection)
{
  _dbus_assert (transport->vtable->connection_set != NULL);
  _dbus_assert (transport->connection == NULL);
  
  transport->connection = connection;

  (* transport->vtable->connection_set) (transport);
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
    {
      _dbus_connection_transport_error (transport->connection,
                                        DBUS_RESULT_DISCONNECTED);
      return;
    }
  
  (* transport->vtable->messages_pending) (transport,
                                           queue_length);
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

  if ((flags & (DBUS_ITERATION_DO_WRITING |
                DBUS_ITERATION_DO_READING)) == 0)
    return; /* Nothing to do */

  if (transport->disconnected)
    {
      _dbus_connection_transport_error (transport->connection,
                                        DBUS_RESULT_DISCONNECTED);
      return;
    }
  
  (* transport->vtable->do_iteration) (transport, flags,
                                       timeout_milliseconds);
}

/** @} */
