/* -*- mode: C; c-file-style: "gnu" -*- */
/* gather-introspect.c  Dump introspection data from service to stdout
 *
 * Copyright (C) 2005  Red Hat, Inc.
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

#include <dbus/dbus.h>

static void
usage (char *name, int ecode)
{
  fprintf (stderr, "Usage: %s <service> <destination object path>\n", name);
  exit (ecode);
}

int
main (int argc, char *argv[])
{
  DBusConnection *connection;
  DBusError error;
  DBusMessage *message;
  DBusMessage *reply;
  const char *service;
  const char *path;
  const char *introspect_data;
  
  if (argc != 3)
    usage (argv[0], 1);

  service = argv[1];
  path = argv[2];

  dbus_error_init (&error);
  connection = dbus_bus_get (DBUS_BUS_SESSION, &error);
  if (connection == NULL)
    {
      fprintf (stderr, "Failed to open connection to session bus: %s\n",
               error.message);
      dbus_error_free (&error);
      exit (1);
    }

  message = dbus_message_new_method_call (NULL,
					  path,
					  DBUS_INTERFACE_INTROSPECTABLE,
					  "Introspect");
  if (message == NULL)
    {
      fprintf (stderr, "Couldn't allocate D-BUS message\n");
      exit (1);
    }

  if (!dbus_message_set_destination (message, service))
    {
      fprintf (stderr, "Not enough memory\n");
      exit (1);
    }
  
  reply = dbus_connection_send_with_reply_and_block (connection,
						     message,
						     -1,
						     &error);
  dbus_message_unref (message);
  if (dbus_error_is_set (&error))
    {
      fprintf (stderr, "Error: %s\n", error.message);
      exit (1);
    }

  if (!dbus_message_get_args (reply, &error,
			      DBUS_TYPE_STRING,
			      &introspect_data,
			      DBUS_TYPE_INVALID))
    {
      fprintf (stderr, "Error: %s\n", error.message);
      exit (1);
    }
  printf ("%s", introspect_data);

  dbus_message_unref (reply);

  dbus_connection_disconnect (connection);

  exit (0);
}
