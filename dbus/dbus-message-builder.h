/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message-builder.h Build messages from text files for testing (internal to D-BUS implementation)
 * 
 * Copyright (C) 2003 Red Hat, Inc.
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

#ifndef DBUS_MESSAGE_BUILDER_H
#define DBUS_MESSAGE_BUILDER_H

#include <config.h>

#include <dbus/dbus-memory.h>
#include <dbus/dbus-types.h>
#include <dbus/dbus-string.h>

DBUS_BEGIN_DECLS

dbus_bool_t _dbus_message_data_load (DBusString       *dest,
                                     const DBusString *filename);

DBUS_END_DECLS

#endif /* DBUS_MESSAGE_BUILDER_H */
