
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
  fprintf (stderr, "*** test-service: %s", message);
  exit (1);
}

static DBusHandlerResult
handle_echo (DBusConnection     *connection,
             DBusMessage        *message)
{
  DBusError error;
  DBusMessage *reply;
  char *s;

  _dbus_verbose ("sending reply to Echo method\n");
  
  dbus_error_init (&error);
  
  if (!dbus_message_get_args (message,
                              &error,
                              DBUS_TYPE_STRING, &s,
                              DBUS_TYPE_INVALID))
    {
      reply = dbus_message_new_error (message,
                                      error.name,
                                      error.message);

      if (reply == NULL)
        die ("No memory\n");

      if (!dbus_connection_send (connection, reply, NULL))
        die ("No memory\n");

      dbus_message_unref (reply);

      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

  reply = dbus_message_new_method_return (message);
  if (reply == NULL)
    die ("No memory\n");
  
  if (!dbus_message_append_args (reply,
                                 DBUS_TYPE_STRING, s,
                                 DBUS_TYPE_INVALID))
    die ("No memory");
  
  if (!dbus_connection_send (connection, reply, NULL))
    die ("No memory\n");

  fprintf (stderr, "Echo service echoed string: \"%s\"\n", s);
  
  dbus_free (s);
  
  dbus_message_unref (reply);
    
  return DBUS_HANDLER_RESULT_HANDLED;
}

static void
path_unregistered_func (DBusConnection  *connection,
                        void            *user_data)
{
  /* connection was finalized */
}

static DBusHandlerResult
path_message_func (DBusConnection  *connection,
                   DBusMessage     *message,
                   void            *user_data)
{
  if (dbus_message_is_method_call (message,
                                   "org.freedesktop.TestSuite",
                                   "Echo"))
    return handle_echo (connection, message);
  else if (dbus_message_is_method_call (message,
                                        "org.freedesktop.TestSuite",
                                        "Exit"))
    {
      dbus_connection_disconnect (connection);
      quit ();
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  else if (dbus_message_is_method_call (message,
                                        "org.freedesktop.TestSuite",
                                        "EmitFoo"))
    {
      /* Emit the Foo signal */
      DBusMessage *signal;

      _dbus_verbose ("emitting signal Foo\n");
      
      signal = dbus_message_new_signal ("/org/freedesktop/TestSuite",
                                        "org.freedesktop.TestSuite",
                                        "Foo");
      if (signal == NULL)
        die ("No memory\n");
      
      if (!dbus_message_append_args (signal,
                                     DBUS_TYPE_DOUBLE, 42.6,
                                     DBUS_TYPE_INVALID))
        die ("No memory");
  
      if (!dbus_connection_send (connection, signal, NULL))
        die ("No memory\n");
      
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  else
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusObjectPathVTable
echo_vtable = {
  path_unregistered_func,
  path_message_func,
  NULL,
};


static const char* echo_path = "/org/freedesktop/TestSuite" ;

static DBusHandlerResult
filter_func (DBusConnection     *connection,
             DBusMessage        *message,
             void               *user_data)
{
  if (dbus_message_is_signal (message,
                              DBUS_INTERFACE_ORG_FREEDESKTOP_LOCAL,
                              "Disconnected"))
    {
      dbus_connection_disconnect (connection);
      quit ();
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  else
    {
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
}

int
main (int    argc,
      char **argv)
{
  DBusConnection *connection;
  DBusError error;
  int result;
  
  dbus_error_init (&error);
  connection = dbus_bus_get (DBUS_BUS_ACTIVATION, &error);
  if (connection == NULL)
    {
      fprintf (stderr, "*** Failed to open connection to activating message bus: %s\n",
               error.message);
      dbus_error_free (&error);
      return 1;
    }

  loop = _dbus_loop_new ();
  if (loop == NULL)
    die ("No memory\n");
  
  if (!test_connection_setup (loop, connection))
    die ("No memory\n");

  if (!dbus_connection_add_filter (connection,
                                   filter_func, NULL, NULL))
    die ("No memory");

  if (!dbus_connection_register_object_path (connection,
                                             echo_path,
                                             &echo_vtable,
                                             NULL))
    die ("No memory");

  result = dbus_bus_acquire_service (connection, "org.freedesktop.DBus.TestSuiteEchoService",
                                     0, &error);
  if (dbus_error_is_set (&error))
    {
      fprintf (stderr, "Error %s\n", error.message);
      _dbus_verbose ("*** Failed to acquire service: %s\n",
                     error.message);
      dbus_error_free (&error);
      exit (1);
    }

  _dbus_verbose ("*** Test service entering main loop\n");
  _dbus_loop_run (loop);

  test_connection_shutdown (loop, connection);

  dbus_connection_remove_filter (connection, filter_func, NULL);
  
  dbus_connection_unref (connection);

  _dbus_loop_unref (loop);
  loop = NULL;
  
  dbus_shutdown ();

  _dbus_verbose ("*** Test service exiting\n");
  
  return 0;
}
