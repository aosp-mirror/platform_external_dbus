#include <dbus/dbus.h>
#include <stdio.h>
#include "watch.h"

int
main (int    argc,
      char **argv)
{
  DBusConnection *connection;
  DBusResultCode result;
  DBusMessage *message;
  
  if (argc < 2)
    {
      fprintf (stderr, "Give the server address as an argument\n");
      return 1;
    }
  
  connection = dbus_connection_open (argv[1], &result);
  if (connection == NULL)
    {
      fprintf (stderr, "Failed to open connection to %s: %s\n",
               argv[1], dbus_result_to_string (result));
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
