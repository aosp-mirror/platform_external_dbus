/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-sysdeps-unix.h UNIX-specific wrappers around system/libc features (internal to D-Bus implementation)
 * 
 * Copyright (C) 2002, 2003, 2006  Red Hat, Inc.
 * Copyright (C) 2003 CodeFactory AB
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

#ifndef DBUS_SYSDEPS_UNIX_H
#define DBUS_SYSDEPS_UNIX_H

#include <config.h>
#include <dbus/dbus-sysdeps.h>

#ifdef DBUS_WIN
#error "Don't include this on Windows"
#endif

DBUS_BEGIN_DECLS

/**
 * @defgroup DBusSysdepsUnix UNIX-specific internal API
 * @ingroup DBusInternals
 * @brief Internal system-dependent API available on UNIX only
 * @{
 */

dbus_bool_t 
_dbus_close     (int               fd,
                 DBusError        *error);
int 
_dbus_read      (int               fd,
                 DBusString       *buffer,
                 int               count);
int 
_dbus_write     (int               fd,
                 const DBusString *buffer,
                 int               start,
                 int               len);
int 
_dbus_write_two (int               fd,
                 const DBusString *buffer1,
                 int               start1,
                 int               len1,
                 const DBusString *buffer2,
                 int               start2,
                 int               len2);

dbus_bool_t _dbus_open_unix_socket (int              *fd,
                                    DBusError        *error);
int _dbus_connect_unix_socket (const char     *path,
                               dbus_bool_t     abstract,
                               DBusError      *error);
int _dbus_listen_unix_socket  (const char     *path,
                               dbus_bool_t     abstract,
                               DBusError      *error);

/** @} */

DBUS_END_DECLS

#endif /* DBUS_SYSDEPS_UNIX_H */
