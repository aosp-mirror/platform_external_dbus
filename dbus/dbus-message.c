/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message.c  DBusMessage object
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

#include "dbus-message.h"

/**
 * @defgroup DBusMessage
 * @ingroup  DBus
 * @brief DBusMessage object
 *
 * Types and functions related to the DBusMessage object.
 *
 * @{
 */

struct DBusMessage
{
  int refcount;

};

/**
 * Constructs a new message.
 * @return a new DBusMessage, free with dbus_message_unref()
 * @see dbus_message_unref()
 */
DBusMessage*
dbus_message_new (void)
{
  
  return NULL;
}


/**
 * Increments the reference count of a DBusMessage.
 *
 * @arg message The message
 * @see dbus_message_unref
 */
void
dbus_message_ref (DBusMessage *message)
{
  
}

/**
 * Decrements the reference count of a DBusMessage.
 *
 * @arg message The message
 * @see dbus_message_ref
 */
void
dbus_message_unref (DBusMessage *message)
{


}

/** @} */
