/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-keyring.c Store secret cookies in your homedir
 *
 * Copyright (C) 2003  Red Hat Inc.
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

#include "dbus-keyring.h"
#include <dbus/dbus-string.h>
#include <dbus/dbus-list.h>
#include <dbus/dbus-sysdeps.h>

/**
 * @defgroup DBusKeyring keyring class
 * @ingroup  DBusInternals
 * @brief DBusKeyring data structure
 *
 * Types and functions related to DBusKeyring. DBusKeyring is intended
 * to manage cookies used to authenticate clients to servers.  This is
 * essentially the "verify that client can read the user's homedir"
 * authentication mechanism.  Both client and server must have access
 * to the homedir.
 *
 * The secret keys are not kept in locked memory, and are written to a
 * file in the user's homedir. However they are transient (only used
 * by a single server instance for a fixed period of time, then
 * discarded). Also, the keys are not sent over the wire.
 */

/**
 * @defgroup DBusKeyringInternals DBusKeyring implementation details
 * @ingroup  DBusInternals
 * @brief DBusKeyring implementation details
 *
 * The guts of DBusKeyring.
 *
 * @{
 */

typedef struct
{
  dbus_uint32_t id; /**< identifier used to refer to the key */

  unsigned long creation_time; /**< when the key was generated,
                                *   as unix timestamp
                                */

  DBusString context; /**< Name of kind of server using this
                       *   key, for example "desktop_session_bus"
                       */
  
  DBusString secret; /**< the actual key */

} DBusKey;

/**
 * @brief Internals of DBusKeyring.
 * 
 * DBusKeyring internals. DBusKeyring is an opaque object, it must be
 * used via accessor functions.
 */
struct DBusKeyring
{
  DBusString filename;
  DBusString lock_filename;
  
  
};

/** @} */ /* end of internals */

/**
 * @addtogroup DBusKeyring
 *
 * @{
 */


/** @} */ /* end of public API */
