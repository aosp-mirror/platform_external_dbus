/* -*- mode: C; c-file-style: "gnu" -*- */
/* main.c  main() for message bus
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
#include "loop.h"
#include "activation.h"
#include "connection.h"
#include "driver.h"
#include <dbus/dbus-list.h>

static void
server_watch_callback (DBusWatch     *watch,
                       unsigned int   condition,
                       void          *data)
{
  DBusServer *server = data;

  dbus_server_handle_watch (server, watch, condition);
}

static void
add_server_watch (DBusWatch      *watch,
                  DBusServer     *server)
{
  bus_loop_add_watch (watch, server_watch_callback, server,
                      NULL);
}

static void
remove_server_watch (DBusWatch      *watch,
                     DBusServer     *server)
{
  bus_loop_remove_watch (watch, server_watch_callback, server);
}

static void
setup_server (DBusServer *server)
{
  dbus_server_set_watch_functions (server,
                                   (DBusAddWatchFunction) add_server_watch,
                                   (DBusRemoveWatchFunction) remove_server_watch,
                                   server,
                                   NULL);
}

static void
new_connection_callback (DBusServer     *server,
                         DBusConnection *new_connection,
                         void           *data)
{
  if (!bus_connection_setup (new_connection))
    ; /* we won't have ref'd the connection so it will die */
}

int
main (int argc, char **argv)
{
  DBusServer *server;
  DBusResultCode result;

  if (argc < 2)
    {
      _dbus_warn ("Give the server address as an argument\n");
      return 1;
    }

  
  server = dbus_server_listen (argv[1], &result);
  if (server == NULL)
    {
      _dbus_warn ("Failed to start server on %s: %s\n",
                  argv[1], dbus_result_to_string (result));
      return 1;
    }

  if (argc < 3)
    {
      _dbus_warn ("No service location given, not activating activation\n");
    }
  else
    {
      const char *paths[] = { argv[2], NULL };
      DBusError error;

      dbus_error_init (&error);
      if (!bus_activation_init (argv[1], paths,
                                &error))
        {
          _dbus_warn ("Could not initialize service activation: %s\n",
                      error.message);
          dbus_error_free (&error);
          return 1;
        }
    }
  
  setup_server (server);

  bus_connection_init ();
  
  dbus_server_set_new_connection_function (server,
                                           new_connection_callback,
                                           NULL, NULL);

  _dbus_verbose ("We are on D-Bus...\n");
  bus_loop_run ();
  
  dbus_server_disconnect (server);
  dbus_server_unref (server);

  return 0;
}
