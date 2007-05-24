/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-server-win.c Server implementation for WIN network protocols.
 *
 * Copyright (C) 2002, 2003, 2004  Red Hat Inc.
 * Copyright (C) 2007 Ralf Habacker <ralf.habacker@freenet.de>
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

#include "dbus-internals.h"
#include "dbus-server-win.h"
#include "dbus-server-socket.h"
#include "dbus-connection-internal.h"
#include "dbus-sysdeps-win.h"

/**
 * @defgroup DBusServerWin DBusServer implementations for Windows
 * @ingroup  DBusInternals
 * @brief Implementation details of DBusServer on Windows 
 *
 * @{
 */

/**
 * Tries to interpret the address entry in a platform-specific
 * way, creating a platform-specific server type if appropriate.
 * Sets error if the result is not OK.
 * 
 * @param entry an address entry
 * @param server_p location to store a new DBusServer, or #NULL on failure.
 * @param error location to store rationale for failure on bad address
 * @returns the outcome
 * 
 */
DBusServerListenResult
_dbus_server_listen_platform_specific (DBusAddressEntry *entry,
                                       DBusServer      **server_p,
                                       DBusError        *error)
{
  /* don't handle any method yet, return NULL with the error unset, 
   ** for a sample implementation see dbus-server-unix.c 
   */
  *server_p  = NULL;
  _DBUS_ASSERT_ERROR_IS_CLEAR(error);
  return DBUS_SERVER_LISTEN_NOT_HANDLED;
}

/** @} */

