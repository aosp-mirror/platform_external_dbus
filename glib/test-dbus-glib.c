/* -*- mode: C; c-file-style: "gnu" -*- */
#include "dbus-glib.h"
#include <stdio.h>

int
main (int argc, char **argv)
{
  DBusConnection *connection;
  DBusResultCode result;
  DBusMessage *message, *reply;
  
  GMainLoop *loop;
  
  if (argc < 2)
    {
      fprintf (stderr, "Give the server address as an argument\n");
      return 1;
    }

  loop = g_main_loop_new (NULL, FALSE);

  connection = dbus_connection_open (argv[1], &result);
  if (connection == NULL)
    {
      fprintf (stderr, "Failed to open connection to %s: %s\n", argv[1],
	       dbus_result_to_string (result));
      return 1;
    }

  dbus_connection_hookup_with_g_main (connection);

  message = dbus_message_new ("org.freedesktop.DBus", "org.freedesktop.DBus.Hello");

  reply = dbus_connection_send_message_with_reply_and_block (connection, message, -1, &result);
  g_print ("reply name: %s\n", dbus_message_get_name (reply));
  
  g_main_loop_run (loop);
  
  return 0;
}
