/* -*- mode: C; c-file-style: "gnu" -*- */
#include "dbus-glib.h"
#include <stdio.h>

int
main (int argc, char **argv)
{
  DBusConnection *connection;
  DBusMessage *message, *reply;  
  GMainLoop *loop;
  DBusError error;
  
  if (argc < 2)
    {
      g_printerr ("Give the server address as an argument\n");
      return 1;
    }

  loop = g_main_loop_new (NULL, FALSE);

  dbus_error_init (&error);
  connection = dbus_connection_open (argv[1], &error);
  if (connection == NULL)
    {
      g_printerr ("Failed to open connection to %s: %s\n", argv[1],
                  error.message);
      dbus_error_free (&error);
      return 1;
    }

  dbus_connection_setup_with_g_main (connection, NULL);

  message = dbus_message_new (DBUS_MESSAGE_HELLO,
                              DBUS_SERVICE_DBUS);

  dbus_error_init (&error);
  reply = dbus_connection_send_with_reply_and_block (connection, message, -1, &error);
  if (reply == NULL)
    {
      g_printerr ("Error on hello message: %s\n", error.message);
      dbus_error_free (&error);
      return 1;
    }
  
  g_print ("reply name: %s\n", dbus_message_get_name (reply));
  
  g_main_loop_run (loop);
  
  return 0;
}
