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

  dbus_error_init (&error);
  connection = dbus_bus_get (DBUS_BUS_ACTIVATION, &error);
  if (connection == NULL)
    {
      fprintf (stderr, "Failed to open connection to activating message bus: %s\n",
               error.message);
      dbus_error_free (&error);
      return 1;
    }

  setup_connection (connection);
  
  do_mainloop ();

  dbus_connection_unref (connection);
  
  return 0;
}
