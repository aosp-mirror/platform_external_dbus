#include <dbus/dbus.h>
#include <stdio.h>
#include "watch.h"

static void
new_connection_callback (DBusServer     *server,
                         DBusConnection *new_connection,
                         void           *data)
{
  printf ("Got new connection\n");

  dbus_connection_set_max_live_messages_size (new_connection,
                                              10);
  
  setup_connection (new_connection);
}

int
main (int    argc,
      char **argv)
{
  DBusServer *server;
  DBusError error;

  if (argc < 2)
    {
      fprintf (stderr, "Give the server address as an argument\n");
      return 1;
    }

  dbus_error_init (&error);
  server = dbus_server_listen (argv[1], &error);
  if (server == NULL)
    {
      fprintf (stderr, "Failed to start server on %s: %s\n",
               argv[1], error.message);
      dbus_error_free (&error);
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
