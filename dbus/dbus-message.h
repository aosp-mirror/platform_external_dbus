/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message.h  DBusMessage object
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
#if !defined (DBUS_INSIDE_DBUS_H) && !defined (DBUS_COMPILATION)
#error "Only <dbus/dbus.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef DBUS_MESSAGE_H
#define DBUS_MESSAGE_H

#include <dbus/dbus-macros.h>

DBUS_BEGIN_DECLS

/**
 * @defgroup Message
 * @ingroup  DBus
 * @brief Message handling.
 *
 * This functions deal with message structure
 * within DBus.
 *
 * @{
 */

typedef struct DBusMessage DBusMessage;

/**
 * Constructs a new DBus message.
 * @return New DBusMessage
 */
DBusMessage* dbus_message_new   (void);

/**
 * Increments a reference count on a message.
 *
 * @arg message It's the message whose reference count we're incrementing
 * @see dbus_message_unref
 */
void         dbus_message_ref   (DBusMessage *message);

/**
 * Decrements a reference count on a message.
 *
 * @arg message It's the message whose reference count we're decrementing
 */
void         dbus_message_unref (DBusMessage *message);


/** @} */

DBUS_END_DECLS

#endif /* DBUS_MESSAGE_H */
