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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage (void)
{
  fprintf (stderr, "dbus-daemon-1 [--session] [--system] [--config-file=FILE] [--version]\n");
  exit (1);
}

static void
version (void)
{
  printf ("D-BUS Message Bus Daemon %s\n"
          "Copyright (C) 2002, 2003 Red Hat, Inc., CodeFactory AB, and others\n"
          "This is free software; see the source for copying conditions.\n"
          "There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n",
          VERSION);
  exit (0);
}

static void
check_two_config_files (const DBusString *config_file,
                        const char       *extra_arg)
{
  if (_dbus_string_get_length (config_file) > 0)
    {
      fprintf (stderr, "--%s specified but configuration file %s already requested\n",
               extra_arg, _dbus_string_get_const_data (config_file));
      exit (1);
    }
}

int
main (int argc, char **argv)
{
  BusContext *context;
  DBusError error;
  DBusString config_file;
  const char *prev_arg;
  int i;

  if (!_dbus_string_init (&config_file))
    return 1;
  
  prev_arg = NULL;
  i = 1;
  while (i < argc)
    {
      const char *arg = argv[i];
      
      if (strcmp (arg, "--help") == 0 ||
          strcmp (arg, "-h") == 0 ||
          strcmp (arg, "-?") == 0)
        usage ();
      else if (strcmp (arg, "--version") == 0)
        version ();
      else if (strcmp (arg, "--system") == 0)
        {
          check_two_config_files (&config_file, "system");

          if (!_dbus_string_append (&config_file, DBUS_SYSTEM_CONFIG_FILE))
            exit (1);
        }
      else if (strcmp (arg, "--session") == 0)
        {
          check_two_config_files (&config_file, "session");

          if (!_dbus_string_append (&config_file, DBUS_SESSION_CONFIG_FILE))
            exit (1);
        }
      else if (strstr (arg, "--config-file=") == arg)
        {
          const char *file;

          check_two_config_files (&config_file, "config-file");
          
          file = strchr (arg, '=');
          ++file;

          if (!_dbus_string_append (&config_file, file))
            exit (1);
        }
      else if (prev_arg &&
               strcmp (prev_arg, "--config-file") == 0)
        {
          check_two_config_files (&config_file, "config-file");
          
          if (!_dbus_string_append (&config_file, arg))
            exit (1);
        }
      else if (strcmp (arg, "--config-file") == 0)
        ; /* wait for next arg */
      else
        usage ();
      
      prev_arg = arg;
      
      ++i;
    }

  if (_dbus_string_get_length (&config_file) == 0)
    {
      fprintf (stderr, "No configuration file specified.\n");
      usage ();
    }
  
  dbus_error_init (&error);
  context = bus_context_new (&config_file, &error);
  _dbus_string_free (&config_file);
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
