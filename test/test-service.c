
#include "test-utils.h"

static DBusLoop *loop;
static dbus_bool_t already_quit = FALSE;

static void
quit (void)
{
  if (!already_quit)
    {
      _dbus_loop_quit (loop);
      already_quit = TRUE;
    }
}

static void
die (const char *message)
{
  fprintf (stderr, "*** %s", message);
  exit (1);
}

static DBusHandlerResult
handle_echo (DBusConnection     *connection,
             DBusMessage        *message)
{
  DBusError error;
  DBusMessage *reply;
  DBusMessageIter iter;
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

  dbus_message_append_iter_init (message, &iter);
  
  if (!dbus_message_iter_append_string (&iter, s))
    die ("No memory");

  if (!dbus_connection_send (connection, reply, NULL))
    die ("No memory\n");
  
  dbus_free (s);
  
  dbus_message_unref (reply);
    
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static DBusHandlerResult
filter_func (DBusMessageHandler *handler,
             DBusConnection     *connection,
             DBusMessage        *message,
             void               *user_data)
{  
  if (dbus_message_has_name (message, "org.freedesktop.DBus.TestSuiteEcho"))
    return handle_echo (connection, message);
  else if (dbus_message_has_name (message, "org.freedesktop.DBus.TestSuiteExit") ||
           dbus_message_has_name (message, DBUS_MESSAGE_LOCAL_DISCONNECT))
    {
      dbus_connection_disconnect (connection);
      quit ();
      return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }
  else
    {
      return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }
}

int
main (int    argc,
      char **argv)
{
  DBusConnection *connection;
  DBusError error;
  DBusMessageHandler *handler;
  const char *to_handle[] = {
    "org.freedesktop.DBus.TestSuiteEcho",
    "org.freedesktop.DBus.TestSuiteExit",
    DBUS_MESSAGE_LOCAL_DISCONNECT,
  };
  int result;
  
  dbus_error_init (&error);
  connection = dbus_bus_get (DBUS_BUS_ACTIVATION, &error);
  if (connection == NULL)
    {
      _dbus_verbose ("*** Failed to open connection to activating message bus: %s\n",
                     error.message);
      dbus_error_free (&error);
      return 1;
    }

  loop = _dbus_loop_new ();
  if (loop == NULL)
    die ("No memory\n");
  
  if (!test_connection_setup (loop, connection))
    die ("No memory\n");

  handler = dbus_message_handler_new (filter_func, NULL, NULL);
  if (handler == NULL)
    die ("No memory");
  
  if (!dbus_connection_register_handler (connection, handler, to_handle,
                                         _DBUS_N_ELEMENTS (to_handle)))
    die ("No memory");

  result = dbus_bus_acquire_service (connection, "org.freedesktop.DBus.TestSuiteEchoService",
                                     0, &error);
  if (dbus_error_is_set (&error))
    {
      _dbus_verbose ("*** Failed to acquire service: %s\n",
                     error.message);
      dbus_error_free (&error);
      exit (1);
    }

  _dbus_verbose ("*** Test service entering main loop\n");
  _dbus_loop_run (loop);

  test_connection_shutdown (loop, connection);
  
  dbus_connection_unref (connection);
  
  dbus_message_handler_unref (handler);

  _dbus_loop_unref (loop);
  loop = NULL;
  
  dbus_shutdown ();

  _dbus_verbose ("*** Test service exiting\n");
  
  return 0;
}
