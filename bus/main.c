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
#include "bus.h"
#include "loop.h"
#include <dbus/dbus-internals.h>

int
main (int argc, char **argv)
{
  BusContext *context;
  DBusError error;
  const char *paths[] = { NULL, NULL };
  
  if (argc < 3)
    {
      /* FIXME obviously just for testing */
      _dbus_warn ("Give the server address as an argument and activation directory as args\n");
      return 1;
    }

  paths[0] = argv[2];
  
  dbus_error_init (&error);
  context = bus_context_new (argv[1], paths, &error);
  if (context == NULL)
    {
      _dbus_warn ("Failed to start message bus: %s\n",
                  error.message);
      dbus_error_free (&error);
      return 1;
    }

  _dbus_verbose ("We are on D-Bus...\n");
  bus_loop_run ();
  
  bus_context_shutdown (context);
  bus_context_unref (context);

  return 0;
}
