/* -*- mode: C; c-file-style: "gnu" -*- */
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GMainLoop *loop = NULL;
static int n_times_foo_received = 0;

static gboolean
timed_exit (gpointer loop)
{
  g_main_loop_quit (loop);
  return TRUE;
}

static void
foo_signal_handler (DBusGProxy  *proxy,
                    void        *user_data)
{
#if 0
  double d;
  
  /* FIXME - need to fix up dbus_gproxy_signal_connect() to be able to
   * get signal args
   */
  
  DBusError derror;
  
  if (!dbus_message_is_signal (signal,
                               "org.freedesktop.TestSuite",
                               "Foo"))
    {
      g_printerr ("Signal handler received the wrong message\n");
      exit (1);
    }

  dbus_error_init (&derror);
  if (!dbus_message_get_args (signal, &derror, DBUS_TYPE_DOUBLE,
                              &d, DBUS_TYPE_INVALID))
    {
      g_printerr ("failed to get signal args: %s\n", derror.message);
      dbus_error_free (&derror);
      exit (1);
    }
#endif

  n_times_foo_received += 1;

  g_main_loop_quit (loop);
}

int
main (int argc, char **argv)
{
  DBusGConnection *connection;
  GError *error;
  DBusGProxy *driver;
  DBusGProxy *proxy;
  DBusGPendingCall *call;
  char **service_list;
  int service_list_len;
  int i;
  guint32 result;
  char *str;
  
  g_type_init ();
  
  loop = g_main_loop_new (NULL, FALSE);

  error = NULL;
  connection = dbus_g_bus_get (DBUS_BUS_SESSION,
                               &error);
  if (connection == NULL)
    {
      g_printerr ("Failed to open connection to bus: %s\n",
                  error->message);
      g_error_free (error);
      exit (1);
    }

  /* Create a proxy object for the "bus driver" */
  
  driver = dbus_g_proxy_new_for_service (connection,
                                         DBUS_SERVICE_ORG_FREEDESKTOP_DBUS,
                                         DBUS_PATH_ORG_FREEDESKTOP_DBUS,
                                         DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS);

  /* Call ListServices method */
  
  call = dbus_g_proxy_begin_call (driver, "ListServices", DBUS_TYPE_INVALID);

  error = NULL;
  if (!dbus_g_proxy_end_call (driver, call, &error,
                              DBUS_TYPE_ARRAY, DBUS_TYPE_STRING,
                              &service_list, &service_list_len,
                              DBUS_TYPE_INVALID))
    {
      g_printerr ("Failed to complete ListServices call: %s\n",
                  error->message);
      g_error_free (error);
      exit (1);
    }

  g_print ("Services on the message bus:\n");
  i = 0;
  while (i < service_list_len)
    {
      g_assert (service_list[i] != NULL);
      g_print ("  %s\n", service_list[i]);
      ++i;
    }
  g_assert (service_list[i] == NULL);

  g_strfreev (service_list);

  /* Test handling of unknown method */
  call = dbus_g_proxy_begin_call (driver, "ThisMethodDoesNotExist",
                                 DBUS_TYPE_STRING,
                                 "blah blah blah blah blah",
                                 DBUS_TYPE_INT32,
                                 10,
                                 DBUS_TYPE_INVALID);

  error = NULL;
  if (dbus_g_proxy_end_call (driver, call, &error,
                            DBUS_TYPE_INVALID))
    {
      g_printerr ("Calling nonexistent method succeeded!\n");
      exit (1);
    }

  g_print ("Got EXPECTED error from calling unknown method: %s\n",
           error->message);
  g_error_free (error);
  
  /* Activate a service */
  call = dbus_g_proxy_begin_call (driver, "ActivateService",
                                 DBUS_TYPE_STRING,
                                 "org.freedesktop.DBus.TestSuiteEchoService",
                                 DBUS_TYPE_UINT32,
                                 0,
                                 DBUS_TYPE_INVALID);

  error = NULL;
  if (!dbus_g_proxy_end_call (driver, call, &error,
                             DBUS_TYPE_UINT32, &result,
                             DBUS_TYPE_INVALID))
    {
      g_printerr ("Failed to complete Activate call: %s\n",
                  error->message);
      g_error_free (error);
      exit (1);
    }

  g_print ("Activation of echo service = 0x%x\n", result);

  /* Activate a service again */
  call = dbus_g_proxy_begin_call (driver, "ActivateService",
                                 DBUS_TYPE_STRING,
                                 "org.freedesktop.DBus.TestSuiteEchoService",
                                 DBUS_TYPE_UINT32,
                                 0,
                                 DBUS_TYPE_INVALID);

  error = NULL;
  if (!dbus_g_proxy_end_call (driver, call, &error,
                             DBUS_TYPE_UINT32, &result,
                             DBUS_TYPE_INVALID))
    {
      g_printerr ("Failed to complete Activate call: %s\n",
                  error->message);
      g_error_free (error);
      exit (1);
    }

  g_print ("Duplicate activation of echo service = 0x%x\n", result);

  /* Talk to the new service */
  
  proxy = dbus_g_proxy_new_for_service_owner (connection,
                                              "org.freedesktop.DBus.TestSuiteEchoService",
                                              "/org/freedesktop/TestSuite",
                                              "org.freedesktop.TestSuite",
                                              &error);
  
  if (proxy == NULL)
    {
      g_printerr ("Failed to create proxy for service owner: %s\n",
                  error->message);
      g_error_free (error);
      exit (1);      
    }

  call = dbus_g_proxy_begin_call (proxy, "Echo",
                                 DBUS_TYPE_STRING,
                                 "my string hello",
                                 DBUS_TYPE_INVALID);

  error = NULL;
  if (!dbus_g_proxy_end_call (proxy, call, &error,
                             DBUS_TYPE_STRING, &str,
                             DBUS_TYPE_INVALID))
    {
      g_printerr ("Failed to complete Echo call: %s\n",
                  error->message);
      g_error_free (error);
      exit (1);
    }

  g_print ("String echoed = \"%s\"\n", str);
  g_free (str);

  /* Test oneway call and signal handling */

  dbus_g_proxy_connect_signal (proxy, "Foo",
                               G_CALLBACK (foo_signal_handler),
                               NULL, NULL);
  
  dbus_g_proxy_call_no_reply (proxy, "EmitFoo",
                              DBUS_TYPE_INVALID);
  
  dbus_g_connection_flush (connection);
  
  g_timeout_add (5000, timed_exit, loop);

  g_main_loop_run (loop);

  if (n_times_foo_received != 1)
    {
      g_printerr ("Foo signal received %d times, should have been 1\n",
                  n_times_foo_received);
      exit (1);
    }
  
  g_object_unref (G_OBJECT (driver));
  g_object_unref (G_OBJECT (proxy));
  
  g_print ("Successfully completed %s\n", argv[0]);
  
  return 0;
}
