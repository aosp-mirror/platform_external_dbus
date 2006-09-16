/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-transport-unix.c UNIX socket subclasses of DBusTransport
 *
 * Copyright (C) 2002, 2003, 2004  Red Hat Inc.
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
#include "dbus-connection-internal.h"
#include "dbus-transport-unix.h"
#include "dbus-transport-protected.h"
#include "dbus-watch.h"
#include "dbus-sysdeps-unix.h"

/**
 * @defgroup DBusTransportUnix DBusTransport implementations for UNIX
 * @ingroup  DBusInternals
 * @brief Implementation details of DBusTransport on UNIX
 *
 * @{
 */

/**
 * Creates a new transport for the given Unix domain socket
 * path. This creates a client-side of a transport.
 *
 * @todo once we add a way to escape paths in a dbus
 * address, this function needs to do escaping.
 *
 * @param path the path to the domain socket.
 * @param abstract #TRUE to use abstract socket namespace
 * @param error address where an error can be returned.
 * @returns a new transport, or #NULL on failure.
 */
DBusTransport*
_dbus_transport_new_for_domain_socket (const char     *path,
                                       dbus_bool_t     abstract,
                                       DBusError      *error)
{
  int fd;
  DBusTransport *transport;
  DBusString address;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (!_dbus_string_init (&address))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }

  fd = -1;

  if ((abstract &&
       !_dbus_string_append (&address, "unix:abstract=")) ||
      (!abstract &&
       !_dbus_string_append (&address, "unix:path=")) ||
      !_dbus_string_append (&address, path))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      goto failed_0;
    }
  
  fd = _dbus_connect_unix_socket (path, abstract, error);
  if (fd < 0)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto failed_0;
    }

  _dbus_fd_set_close_on_exec (fd);
  
  _dbus_verbose ("Successfully connected to unix socket %s\n",
                 path);

  transport = _dbus_transport_new_for_socket (fd, NULL, &address);
  if (transport == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      goto failed_1;
    }
  
  _dbus_string_free (&address);
  
  return transport;

 failed_1:
  _dbus_close_socket (fd, NULL);
 failed_0:
  _dbus_string_free (&address);
  return NULL;
}

/** @} */
