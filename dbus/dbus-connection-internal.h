/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-connection-internal.h DBusConnection internal interfaces
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
#ifndef DBUS_CONNECTION_INTERNAL_H
#define DBUS_CONNECTION_INTERNAL_H

#include <dbus/dbus-internals.h>
#include <dbus/dbus-connection.h>
#include <dbus/dbus-message.h>
#include <dbus/dbus-transport.h>

DBUS_BEGIN_DECLS;

typedef enum
{
  DBUS_ITERATION_DO_WRITING = 1 << 0, /**< Write messages out. */
  DBUS_ITERATION_DO_READING = 1 << 1, /**< Read messages in. */
  DBUS_ITERATION_BLOCK      = 1 << 2  /**< Block if nothing to do. */
} DBusIterationFlags;

dbus_bool_t     _dbus_connection_queue_received_message (DBusConnection *connection,
                                                         DBusMessage    *message);
dbus_bool_t     _dbus_connection_have_messages_to_send  (DBusConnection *connection);

DBusMessage*    _dbus_connection_get_message_to_send    (DBusConnection *connection);
void            _dbus_connection_message_sent           (DBusConnection *connection,
                                                         DBusMessage    *message);

dbus_bool_t     _dbus_connection_add_watch              (DBusConnection *connection,
                                                         DBusWatch      *watch);
void            _dbus_connection_remove_watch           (DBusConnection *connection,
                                                         DBusWatch      *watch);
DBusConnection* _dbus_connection_new_for_transport      (DBusTransport  *transport);


void            _dbus_connection_do_iteration           (DBusConnection *connection,
                                                         unsigned int    flags,
                                                         int             timeout_milliseconds);

void            _dbus_connection_transport_error        (DBusConnection *connection,
                                                         DBusResultCode  result_code);

DBUS_END_DECLS;

#endif /* DBUS_CONNECTION_INTERNAL_H */
