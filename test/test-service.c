#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include "watch.h"

static void
die (const char *message)
{
  fprintf (stderr, "%s", message);
  exit (1);
}

static DBusHandlerResult
echo_handler (DBusMessageHandler *handler,
              DBusConnection     *connection,
              DBusMessage        *message,
              void               *user_data)
{
  DBusError error;
  DBusMessage *reply;
  char *s;
  
  dbus_error_init (&error);
  
  if (!dbus_message_get_args (message,
                              &error,
                              DBUS_TYPE_STRING, &s,
                              DBUS_TYPE_INVALID))
    {
      reply = dbus_message_new_error_reply (message,
                                            error.name,
                                            error.message);

      if (reply == NULL)
        die ("No memory\n");

      if (!dbus_connection_send (connection, reply, NULL))
        die ("No memory\n");

      dbus_message_unref (reply);

      return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }

  reply = dbus_message_new_reply (message);
  if (reply == NULL)
    die ("No memory\n");

  if (!dbus_message_append_string (reply, s))
    die ("No memory");

  if (!dbus_connection_send (connection, reply, NULL))
    die ("No memory\n");
  
  dbus_free (s);
  
  dbus_message_unref (reply);
    
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

int
main (int    argc,
      char **argv)
{
  DBusConnection *connection;
  DBusError error;
  DBusMessageHandler *handler;
  const char *to_handle[] = { "org.freedesktop.DBus.TestSuiteEcho" };
  int result;
  
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

  handler = dbus_message_handler_new (echo_handler, NULL, NULL);
  if (handler == NULL)
    die ("No memory");
  
  if (!dbus_connection_register_handler (connection, handler, to_handle, 1))
    die ("No memory");

  result = dbus_bus_acquire_service (connection, "org.freedesktop.DBus.TestSuiteEchoService",
                                     0, &error);
  if (dbus_error_is_set (&error))
    {
      fprintf (stderr, "Failed to acquire service: %s\n",
               error.message);
      dbus_error_free (&error);
      return 1;
    }
  
  do_mainloop ();

  dbus_connection_unref (connection);

  dbus_message_handler_unref (handler);
  
  dbus_shutdown ();
  
  return 0;
}
