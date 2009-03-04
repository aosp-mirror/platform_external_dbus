/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-misc.c  A few assorted public functions that don't fit elsewhere
 *
 * Copyright (C) 2006 Red Hat, Inc.
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

#include <config.h>
#include "dbus-misc.h"
#include "dbus-internals.h"
#include "dbus-string.h"

/**
 * @defgroup DBusMisc Miscellaneous
 * @ingroup  DBus
 * @brief Miscellaneous API that doesn't cleanly fit anywhere else
 *
 * @{
 */

/**
 * Obtains the machine UUID of the machine this process is running on.
 *
 * The returned string must be freed with dbus_free().
 * 
 * This UUID is guaranteed to remain the same until the next reboot
 * (unless the sysadmin foolishly changes it and screws themselves).
 * It will usually remain the same across reboots also, but hardware
 * configuration changes or rebuilding the machine could break that.
 *
 * The idea is that two processes with the same machine ID should be
 * able to use shared memory, UNIX domain sockets, process IDs, and other
 * features of the OS that require both processes to be running
 * on the same OS kernel instance.
 *
 * The machine ID can also be used to create unique per-machine
 * instances. For example, you could use it in bus names or
 * X selection names.
 *
 * The machine ID is preferred over the machine hostname, because
 * the hostname is frequently set to "localhost.localdomain" and
 * may also change at runtime.
 *
 * You can get the machine ID of a remote application by invoking the
 * method GetMachineId from interface org.freedesktop.DBus.Peer.
 *
 * If the remote application has the same machine ID as the one
 * returned by this function, then the remote application is on the
 * same machine as your application.
 *
 * The UUID is not a UUID in the sense of RFC4122; the details
 * are explained in the D-Bus specification.
 *
 * @returns a 32-byte-long hex-encoded UUID string, or #NULL if insufficient memory
 */
char*
dbus_get_local_machine_id (void)
{
  DBusString uuid;
  char *s;

  s = NULL;
  _dbus_string_init (&uuid);
  if (!_dbus_get_local_machine_uuid_encoded (&uuid) ||
      !_dbus_string_steal_data (&uuid, &s))
    {
      _dbus_string_free (&uuid);
      return FALSE;
    }
  else
    {
      _dbus_string_free (&uuid);
      return s;
    }

}

/** @} */ /* End of public API */

#ifdef DBUS_BUILD_TESTS

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#include "dbus-test.h"
#include <stdlib.h>


dbus_bool_t
_dbus_misc_test (void)
{
  
  return TRUE;
}

#endif /* !DOXYGEN_SHOULD_SKIP_THIS */

#endif
