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
#include <dbus/dbus.h>
 /* Don't copy this, for programs outside the dbus tree it's dbus/dbus-glib.h */
#include <glib/dbus-glib.h>

static DBusHandlerResult
handler_func (DBusMessageHandler *handler,
 	      DBusConnection     *connection,
	      DBusMessage        *message,
	      void               *user_data)
{
  DBusMessageIter iter;

  printf ("message name=%s; sender=%s\n", dbus_message_get_name (message),
          dbus_message_get_sender (message));

  dbus_message_iter_init (message, &iter);

  do
    {
      int type = dbus_message_iter_get_arg_type (&iter);
      char *str;
      dbus_uint32_t uint32;
      dbus_int32_t int32;
      double d;
      unsigned char byte;

      if (type == DBUS_TYPE_INVALID)
	break;

      switch (type)
	{
	case DBUS_TYPE_STRING:
	  str = dbus_message_iter_get_string (&iter);
	  printf ("string:%s\n", str);
	  break;

	case DBUS_TYPE_INT32:
	  int32 = dbus_message_iter_get_int32 (&iter);
	  printf ("int32:%d\n", int32);
	  break;

	case DBUS_TYPE_UINT32:
	  uint32 = dbus_message_iter_get_uint32 (&iter);
	  printf ("int32:%u\n", uint32);
	  break;

	case DBUS_TYPE_DOUBLE:
	  d = dbus_message_iter_get_double (&iter);
	  printf ("double:%f\n", d);
	  break;

	case DBUS_TYPE_BYTE:
	  byte = dbus_message_iter_get_byte (&iter);
	  printf ("byte:%d\n", byte);
	  break;

	default:
	  printf ("(unknown arg type %d)\n", type);
	  break;
	}
    } while (dbus_message_iter_next (&iter));

  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

int
main (int argc, char *argv[])
{
  DBusConnection *connection;
  DBusError error;
  DBusBusType type = DBUS_BUS_SYSTEM;
  DBusMessageHandler *handler;
  GMainLoop *loop;

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

  handler = dbus_message_handler_new (handler_func, NULL, NULL);
  dbus_connection_add_filter (connection, handler);

  g_main_loop_run (loop);

  exit (0);
}
