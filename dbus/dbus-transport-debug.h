/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-transport-debug.h Debug in-proc subclass of DBusTransport
 *
 * Copyright (C) 2003  CodeFactory AB
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
#ifndef DBUS_TRANSPORT_DEBUG_H
#define DBUS_TRANSPORT_DEBUG_H

#include <dbus/dbus-transport.h>

DBUS_BEGIN_DECLS;

DBusTransport* _dbus_transport_debug_server_new (DBusTransport  *client);
DBusTransport* _dbus_transport_debug_client_new (const char     *server_name,
                                                 DBusError      *error);

DBUS_END_DECLS;

#endif /* DBUS_TRANSPORT_DEBUG_H */
