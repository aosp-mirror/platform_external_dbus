#include <dbus/dbus.h>
#include <stdio.h>
#include "watch.h"

int
main (int    argc,
      char **argv)
{
  DBusConnection *connection;
  DBusError error;
  DBusMessage *message;
  
  if (argc < 2)
    {
      fprintf (stderr, "Give the server address as an argument\n");
      return 1;
    }

  dbus_error_init (&error);
  connection = dbus_connection_open (argv[1], &error);
  if (connection == NULL)
    {
      fprintf (stderr, "Failed to open connection to %s: %s\n",
               argv[1], error.message);
      dbus_error_free (&error);
      return 1;
    }

  setup_connection (connection);

  /* Send a message to get things going */
  message = dbus_message_new ("org.freedesktop.DBus.Test", "org.freedesktop.DBus.Test");
  if (!dbus_connection_send (connection,
                             message,
                             NULL))
    fprintf (stderr, "No memory to send reply\n");
  dbus_message_unref (message);
  
  do_mainloop ();

  dbus_connection_unref (connection);
  
  return 0;
}
