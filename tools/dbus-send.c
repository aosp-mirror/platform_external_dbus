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

#include "dbus-print-message.h"

static void
usage (char *name, int ecode)
{
  fprintf (stderr, "Usage: %s [--help] [--system | --session] [--dest=SERVICE] [--type=TYPE] [--print-reply] [--reply-timeout=MSEC] <destination object path> <message name> [contents ...]\n", name);
  exit (ecode);
}

int
main (int argc, char *argv[])
{
  DBusConnection *connection;
  DBusError error;
  DBusMessage *message;
  int print_reply;
  int reply_timeout;
  DBusMessageIter iter;
  int i;
  DBusBusType type = DBUS_BUS_SESSION;
  const char *dest = NULL;
  const char *name = NULL;
  const char *path = NULL;
  int message_type = DBUS_MESSAGE_TYPE_SIGNAL;
  const char *type_str = NULL;
  
  if (argc < 3)
    usage (argv[0], 1);

  print_reply = FALSE;
  reply_timeout = -1;
  
  for (i = 1; i < argc && name == NULL; i++)
    {
      char *arg = argv[i];

      if (strcmp (arg, "--system") == 0)
	type = DBUS_BUS_SYSTEM;
      else if (strcmp (arg, "--session") == 0)
	type = DBUS_BUS_SESSION;
      else if (strcmp (arg, "--print-reply") == 0)
	{
	  print_reply = TRUE;
	  message_type = DBUS_MESSAGE_TYPE_METHOD_CALL;
	}
      else if (strstr (arg, "--reply-timeout=") == arg)
	{
	  reply_timeout = strtol (strchr (arg, '=') + 1,
				  NULL, 10);
	}
      else if (strstr (arg, "--dest=") == arg)
	dest = strchr (arg, '=') + 1;
      else if (strstr (arg, "--type=") == arg)
	type_str = strchr (arg, '=') + 1;
      else if (!strcmp(arg, "--help"))
	usage (argv[0], 0);
      else if (arg[0] == '-')
	usage (argv[0], 1);
      else if (path == NULL)
        path = arg;
      else if (name == NULL)
        name = arg;
      else
        usage (argv[0], 1);
    }

  if (name == NULL)
    usage (argv[0], 1);

  if (type_str != NULL)
    {
      message_type = dbus_message_type_from_string (type_str);
      if (!(message_type == DBUS_MESSAGE_TYPE_METHOD_CALL ||
            message_type == DBUS_MESSAGE_TYPE_SIGNAL))
        {
          fprintf (stderr, "Message type \"%s\" is not supported\n",
                   type_str);
          exit (1);
        }
    }
  
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

  if (message_type == DBUS_MESSAGE_TYPE_METHOD_CALL)
    {
      char *last_dot;

      last_dot = strrchr (name, '.');
      if (last_dot == NULL)
        {
          fprintf (stderr, "Must use org.mydomain.Interface.Method notation, no dot in \"%s\"\n",
                   name);
          exit (1);
        }
      *last_dot = '\0';
      
      message = dbus_message_new_method_call (NULL,
                                              path,
                                              name,
                                              last_dot + 1);
    }
  else if (message_type == DBUS_MESSAGE_TYPE_SIGNAL)
    {
      char *last_dot;

      last_dot = strrchr (name, '.');
      if (last_dot == NULL)
        {
          fprintf (stderr, "Must use org.mydomain.Interface.Signal notation, no dot in \"%s\"\n",
                   name);
          exit (1);
        }
      *last_dot = '\0';
      
      message = dbus_message_new_signal (path, name, last_dot + 1);
    }
  else
    {
      fprintf (stderr, "Internal error, unknown message type\n");
      exit (1);
    }

  if (message == NULL)
    {
      fprintf (stderr, "Couldn't allocate D-BUS message\n");
      exit (1);
    }

  if (dest && !dbus_message_set_destination (message, dest))
    {
      fprintf (stderr, "Not enough memory\n");
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

      /* FIXME - we are ignoring OOM returns on all these functions */
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

	case DBUS_TYPE_BOOLEAN:
          if (strcmp(c, "true") == 0)
            dbus_message_iter_append_boolean (&iter, TRUE);
	  else if (strcmp(c, "false") == 0)
            dbus_message_iter_append_boolean (&iter, FALSE);
	  else
	    {
	      fprintf (stderr, "%s: Expected \"true\" or \"false\" instead of \"%s\"\n", argv[0], c);
	      exit (1);
	    }
	  break;

	default:
	  fprintf (stderr, "%s: Unsupported data type\n", argv[0]);
	  exit (1);
	}
    }

  if (print_reply)
    {
      DBusMessage *reply;

      dbus_error_init (&error);
      reply = dbus_connection_send_with_reply_and_block (connection,
                                                         message, reply_timeout,
                                                         &error);
      if (dbus_error_is_set (&error))
        {
          fprintf (stderr, "Error: %s\n",
                   error.message);
          exit (1);
        }

      if (reply)
        {
          print_message (reply);
          dbus_message_unref (reply);
        }
    }
  else
    {
      dbus_connection_send (connection, message, NULL);
      dbus_connection_flush (connection);
    }

  dbus_message_unref (message);

  dbus_connection_disconnect (connection);

  exit (0);
}
