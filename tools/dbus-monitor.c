/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-monitor.c  Utility program to monitor messages on the bus
 *
 * Copyright (C) 2003 Philip Blundell <philb@gnu.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include "dbus-print-message.h"

static DBusHandlerResult
filter_func (DBusConnection     *connection,
             DBusMessage        *message,
             void               *user_data)
{
  print_message (message, FALSE);
  
  if (dbus_message_is_signal (message,
                              DBUS_INTERFACE_LOCAL,
                              "Disconnected"))
    exit (0);
  
  /* Conceptually we want this to be
   * DBUS_HANDLER_RESULT_NOT_YET_HANDLED, but this raises
   * some problems.  See bug 1719.
   */
  return DBUS_HANDLER_RESULT_HANDLED;
}

static void
usage (char *name, int ecode)
{
  fprintf (stderr, "Usage: %s [--system | --session] [watch expressions]\n", name);
  exit (ecode);
}

int
main (int argc, char *argv[])
{
  DBusConnection *connection;
  DBusError error;
  DBusBusType type = DBUS_BUS_SESSION;
  GMainLoop *loop;
  int i = 0, j = 0, numFilters = 0;
  char **filters = NULL;
  for (i = 1; i < argc; i++)
    {
      char *arg = argv[i];

      if (!strcmp (arg, "--system"))
	type = DBUS_BUS_SYSTEM;
      else if (!strcmp (arg, "--session"))
	type = DBUS_BUS_SESSION;
      else if (!strcmp (arg, "--help"))
	usage (argv[0], 0);
      else if (!strcmp (arg, "--"))
	continue;
      else if (arg[0] == '-')
	usage (argv[0], 1);
      else {
	numFilters++;
       filters = (char **)realloc(filters, numFilters * sizeof(char *));
	filters[j] = (char *)malloc((strlen(arg) + 1) * sizeof(char *));
	snprintf(filters[j], strlen(arg) + 1, "%s", arg);
	j++;
      }
    }

  loop = g_main_loop_new (NULL, FALSE);

  dbus_error_init (&error);
  connection = dbus_bus_get (type, &error);
  if (connection == NULL)
    {
      fprintf (stderr, "Failed to open connection to %s message bus: %s\n",
	       (type == DBUS_BUS_SYSTEM) ? "system" : "session",
               error.message);
      dbus_error_free (&error);
      exit (1);
    }

  dbus_connection_setup_with_g_main (connection, NULL);

  if (numFilters)
    {
      for (i = 0; i < j; i++)
        {
          dbus_bus_add_match (connection, filters[i], &error);
          if (dbus_error_is_set (&error))
            {
              fprintf (stderr, "Failed to setup match \"%s\": %s\n",
                       filters[i], error.message);
              dbus_error_free (&error);
              exit (1);
            }
	  free(filters[i]);
        }
    }
  else
    {
      dbus_bus_add_match (connection,
		          "type='signal'",
		          &error);
      if (dbus_error_is_set (&error))
        goto lose;
      dbus_bus_add_match (connection,
		          "type='method_call'",
		          &error);
      if (dbus_error_is_set (&error))
        goto lose;
      dbus_bus_add_match (connection,
		          "type='method_return'",
		          &error);
      if (dbus_error_is_set (&error))
        goto lose;
      dbus_bus_add_match (connection,
		          "type='error'",
		          &error);
      if (dbus_error_is_set (&error))
        goto lose;
    }

  if (!dbus_connection_add_filter (connection, filter_func, NULL, NULL)) {
    fprintf (stderr, "Couldn't add filter!\n");
    exit (1);
  }

  g_main_loop_run (loop);

  exit (0);
 lose:
  fprintf (stderr, "Error: %s\n", error.message);
  exit (1);
}
