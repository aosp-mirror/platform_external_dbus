/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-print-message.h  Utility function to print out a message
 *
 * Copyright (C) 2003 Philip Blundell <philb@gnu.org>
 * Copyright (C) 2003 Red Hat, Inc.
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
#include "dbus-print-message.h"

static const char*
type_to_name (int message_type)
{
  switch (message_type)
    {
    case DBUS_MESSAGE_TYPE_SIGNAL:
      return "signal";
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
      return "method call";
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
      return "method return";
    case DBUS_MESSAGE_TYPE_ERROR:
      return "error";
    default:
      return "(unknown message type)";
    }
}

void
print_message (DBusMessage *message)
{
  DBusMessageIter iter;
  const char *sender;
  int message_type;

  message_type = dbus_message_get_type (message);
  sender = dbus_message_get_sender (message); 

  switch (message_type)
    {
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
    case DBUS_MESSAGE_TYPE_SIGNAL:
      printf ("%s interface=%s; member=%s; sender=%s\n",
              type_to_name (message_type),
              dbus_message_get_interface (message),
              dbus_message_get_member (message),
              sender ? sender : "(no sender)");
      break;
      
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
      printf ("%s; sender=%s\n",
              type_to_name (message_type),
              sender ? sender : "(no sender)");
      break;

    case DBUS_MESSAGE_TYPE_ERROR:
      printf ("%s name=%s; sender=%s\n",
              type_to_name (message_type),
              dbus_message_get_error_name (message),
              sender ? sender : "(no sender)");
      break;

    default:
      printf ("Message of unknown type %d received\n",
              message_type);
      break;
    }
      
  dbus_message_iter_init (message, &iter);

  do
    {
      int type = dbus_message_iter_get_arg_type (&iter);
      char *str;
      dbus_uint32_t uint32;
      dbus_int32_t int32;
      double d;
      unsigned char byte;
      dbus_bool_t boolean;

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

	case DBUS_TYPE_BOOLEAN:
	  boolean = dbus_message_iter_get_boolean (&iter);
	  printf ("boolean:%s\n", boolean ? "true" : "false");
	  break;

	default:
	  printf ("(unknown arg type %d)\n", type);
	  break;
	}
    } while (dbus_message_iter_next (&iter));
}

