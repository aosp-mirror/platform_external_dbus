/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-transport.h DBusTransport object (internal to D-BUS implementation)
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
#ifndef DBUS_TRANSPORT_H
#define DBUS_TRANSPORT_H

#include <dbus/dbus-internals.h>
#include <dbus/dbus-connection.h>

DBUS_BEGIN_DECLS;

typedef struct DBusTransport DBusTransport;

DBusTransport*     _dbus_transport_open                       (const char                *address,
                                                               DBusError                 *error);
void               _dbus_transport_ref                        (DBusTransport             *transport);
void               _dbus_transport_unref                      (DBusTransport             *transport);
void               _dbus_transport_disconnect                 (DBusTransport             *transport);
dbus_bool_t        _dbus_transport_get_is_connected           (DBusTransport             *transport);
dbus_bool_t        _dbus_transport_get_is_authenticated       (DBusTransport             *transport);
const char*        _dbus_transport_get_address                (DBusTransport             *transport);
dbus_bool_t        _dbus_transport_handle_watch               (DBusTransport             *transport,
                                                               DBusWatch                 *watch,
                                                               unsigned int               condition);
dbus_bool_t        _dbus_transport_set_connection             (DBusTransport             *transport,
                                                               DBusConnection            *connection);
void               _dbus_transport_messages_pending           (DBusTransport             *transport,
                                                               int                        queue_length);
void               _dbus_transport_do_iteration               (DBusTransport             *transport,
                                                               unsigned int               flags,
                                                               int                        timeout_milliseconds);
DBusDispatchStatus _dbus_transport_get_dispatch_status        (DBusTransport             *transport);
dbus_bool_t        _dbus_transport_queue_messages             (DBusTransport             *transport);
void               _dbus_transport_set_max_message_size       (DBusTransport             *transport,
                                                               long                       size);
long               _dbus_transport_get_max_message_size       (DBusTransport             *transport);
void               _dbus_transport_set_max_live_messages_size (DBusTransport             *transport,
                                                               long                       size);
long               _dbus_transport_get_max_live_messages_size (DBusTransport             *transport);
dbus_bool_t        _dbus_transport_get_unix_user              (DBusTransport             *transport,
                                                               unsigned long             *uid);
void               _dbus_transport_set_unix_user_function     (DBusTransport             *transport,
                                                               DBusAllowUnixUserFunction  function,
                                                               void                      *data,
                                                               DBusFreeFunction           free_data_function,
                                                               void                     **old_data,
                                                               DBusFreeFunction          *old_free_data_function);




DBUS_END_DECLS;

#endif /* DBUS_TRANSPORT_H */
