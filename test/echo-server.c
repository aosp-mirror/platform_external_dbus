#include <dbus/dbus.h>
#include <stdio.h>
#include "watch.h"

static void
new_connection_callback (DBusServer     *server,
                         DBusConnection *new_connection,
                         void           *data)
{
  printf ("Got new connection\n");

  setup_connection (new_connection);
}

int
main (int    argc,
      char **argv)
{
  DBusServer *server;
  DBusResultCode result;

  if (argc < 2)
    {
      fprintf (stderr, "Give the server address as an argument\n");
      return 1;
    }

  server = dbus_server_listen (argv[1], &result);
  if (server == NULL)
    {
      fprintf (stderr, "Failed to start server on %s: %s\n",
               argv[1], dbus_result_to_string (result));
      return 1;
    }

  setup_server (server);

  dbus_server_set_new_connection_function (server,
                                           new_connection_callback,
                                           NULL, NULL);
  
  do_mainloop ();

  dbus_server_disconnect (server);
  dbus_server_unref (server);
  
  return 0;
}
