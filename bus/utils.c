/* -*- mode: C; c-file-style: "gnu" -*- */
/* utils.c  General utility functions
 *
 * Copyright (C) 2003  CodeFactory AB
 * Copyright (C) 2003  Red Hat, Inc.
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

#include <config.h>
#include "utils.h"
#include <dbus/dbus-sysdeps.h>

const char bus_no_memory_message[] = "Memory allocation failure in message bus";

int
bus_get_oom_wait (void)
{
#ifdef DBUS_BUILD_TESTS
  /* make tests go fast */
  return 0;
#else
  return 500;
#endif
}

void
bus_wait_for_memory (void)
{
  _dbus_sleep_milliseconds (bus_get_oom_wait ());
}

void
bus_connection_dispatch_all_messages (DBusConnection *connection)
{
  while (bus_connection_dispatch_one_message (connection))
    ;
}

dbus_bool_t
bus_connection_dispatch_one_message  (DBusConnection *connection)
{
  DBusDispatchStatus status;

  while ((status = dbus_connection_dispatch (connection)) == DBUS_DISPATCH_NEED_MEMORY)
    bus_wait_for_memory ();
  
  return status == DBUS_DISPATCH_DATA_REMAINS;
}
