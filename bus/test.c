/* -*- mode: C; c-file-style: "gnu" -*- */
/* test.c  unit test routines
 *
 * Copyright (C) 2003 Red Hat, Inc.
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

#ifdef DBUS_BUILD_TESTS
#include "test.h"
#include "loop.h"

/* The "debug client" watch/timeout handlers don't dispatch messages,
 * as we manually pull them in order to verify them. This is why they
 * are different from the real handlers in connection.c
 */

static void
connection_watch_callback (DBusWatch     *watch,
                           unsigned int   condition,
                           void          *data)
{
  DBusConnection *connection = data;

  dbus_connection_ref (connection);
  
  dbus_connection_handle_watch (connection, watch, condition);

  dbus_connection_unref (connection);
}

static dbus_bool_t
add_connection_watch (DBusWatch      *watch,
                      DBusConnection *connection)
{
  return bus_loop_add_watch (watch, connection_watch_callback, connection,
                             NULL);
}

static void
remove_connection_watch (DBusWatch      *watch,
                         DBusConnection *connection)
{
  bus_loop_remove_watch (watch, connection_watch_callback, connection);
}

static void
connection_timeout_callback (DBusTimeout   *timeout,
                             void          *data)
{
  DBusConnection *connection = data;

  dbus_connection_ref (connection);
  
  dbus_timeout_handle (timeout);

  dbus_connection_unref (connection);
}

static dbus_bool_t
add_connection_timeout (DBusTimeout    *timeout,
                        DBusConnection *connection)
{
  return bus_loop_add_timeout (timeout, connection_timeout_callback, connection, NULL);
}

static void
remove_connection_timeout (DBusTimeout    *timeout,
                           DBusConnection *connection)
{
  bus_loop_remove_timeout (timeout, connection_timeout_callback, connection);
}


dbus_bool_t
bus_setup_debug_client (DBusConnection *connection)
{
  
  if (!dbus_connection_set_watch_functions (connection,
                                            (DBusAddWatchFunction) add_connection_watch,
                                            (DBusRemoveWatchFunction) remove_connection_watch,
                                            connection,
                                            NULL))
    {
      dbus_connection_disconnect (connection);
      return FALSE;
    }
  
  if (!dbus_connection_set_timeout_functions (connection,
                                              (DBusAddTimeoutFunction) add_connection_timeout,
                                              (DBusRemoveTimeoutFunction) remove_connection_timeout,
                                              connection, NULL))
    {
      dbus_connection_disconnect (connection);
      return FALSE;
    }

  return TRUE;
}
#endif
