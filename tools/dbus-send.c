/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-send.c  Utility program to send messages from the command line
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

#include <dbus/dbus.h>

static void
usage (char *name)
{
  fprintf (stderr, "Usage: %s [--session] [--dest=SERVICE] <message type> [contents ...]\n", name);
  exit (1);
}

int
main (int argc, char *argv[])
{
  DBusConnection *connection;
  DBusError error;
  DBusMessage *message;
  DBusMessageIter iter;
  int i;
  DBusBusType type = DBUS_BUS_SYSTEM;
  char *dest = DBUS_SERVICE_BROADCAST;
  char *name = NULL;

  if (argc < 2)
    usage (argv[0]);

  for (i = 1; i < argc && name == NULL; i++)
    {
      char *arg = argv[i];

      if (!strcmp (arg, "--session"))
	type = DBUS_BUS_SESSION;
      else if (strstr (arg, "--dest=") == arg)
	dest = strchr (arg, '=') + 1;
      else if (arg[0] == '-')
	usage (argv[0]);
      else
	name = arg;
    }

  if (name == NULL)
    usage (argv[0]);

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

  message = dbus_message_new (name, dest);
  if (message == NULL)
    {
      fprintf (stderr, "Couldn't allocate D-BUS message\n");
      exit (1);
    }

  dbus_message_append_iter_init (message, &iter);

  while (i < argc)
    {
      char *arg;
      char *c;
      int type;
      dbus_uint32_t uint32;
      dbus_int32_t int32;
      double d;
      unsigned char byte;

      type = DBUS_TYPE_INVALID;
      arg = argv[i++];
      c = strchr (arg, ':');

      if (c == NULL)
	{
	  fprintf (stderr, "%s: Data item \"%s\" is badly formed\n", argv[0], arg);
	  exit (1);
	}

      *(c++) = 0;

      if (arg[0] == 0 || !strcmp (arg, "string"))
	type = DBUS_TYPE_STRING;
      else if (!strcmp (arg, "int32"))
	type = DBUS_TYPE_INT32;
      else if (!strcmp (arg, "uint32"))
	type = DBUS_TYPE_UINT32;
      else if (!strcmp (arg, "double"))
	type = DBUS_TYPE_DOUBLE;
      else if (!strcmp (arg, "byte"))
	type = DBUS_TYPE_BYTE;
      else if (!strcmp (arg, "boolean"))
	type = DBUS_TYPE_BOOLEAN;
      else
	{
	  fprintf (stderr, "%s: Unknown type \"%s\"\n", argv[0], arg);
	  exit (1);
	}

      switch (type)
	{
	case DBUS_TYPE_BYTE:
	  byte = strtoul (c, NULL, 0);
	  dbus_message_iter_append_byte (&iter, byte);
	  break;

	case DBUS_TYPE_DOUBLE:
	  d = strtod (c, NULL);
	  dbus_message_iter_append_double (&iter, d);
	  break;

	case DBUS_TYPE_INT32:
	  int32 = strtol (c, NULL, 0);
	  dbus_message_iter_append_int32 (&iter, int32);
	  break;

	case DBUS_TYPE_UINT32:
	  uint32 = strtoul (c, NULL, 0);
	  dbus_message_iter_append_uint32 (&iter, uint32);
	  break;

	case DBUS_TYPE_STRING:
	  dbus_message_iter_append_string (&iter, c);
	  break;

	default:
	  fprintf (stderr, "%s: Unsupported data type\n", argv[0]);
	  exit (1);
	}
    }

  dbus_connection_send (connection, message, NULL);

  dbus_connection_flush (connection);

  dbus_message_unref (message);

  dbus_connection_disconnect (connection);

  exit (0);
}
