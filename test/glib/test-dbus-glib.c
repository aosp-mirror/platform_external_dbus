/* -*- mode: C; c-file-style: "gnu" -*- */
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test-service-glib-bindings.h"
#include <glib/dbus-gidl.h>
#include <glib/dbus-gparser.h>

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
                    double       d,
                    void        *user_data)
{
  n_times_foo_received += 1;

  g_main_loop_quit (loop);
}

static void lose (const char *fmt, ...) G_GNUC_NORETURN G_GNUC_PRINTF (1, 2);
static void lose_gerror (const char *prefix, GError *error) G_GNUC_NORETURN;

static void
lose (const char *str, ...)
{
  va_list args;

  va_start (args, str);

  vfprintf (stderr, str, args);
  fputc ('\n', stderr);

  va_end (args);

  exit (1);
}

static void
lose_gerror (const char *prefix, GError *error) 
{
  lose ("%s: %s", prefix, error->message);
}

int
main (int argc, char **argv)
{
  DBusGConnection *connection;
  GError *error;
  DBusGProxy *driver;
  DBusGProxy *proxy;
  DBusGPendingCall *call;
  char **name_list;
  int name_list_len;
  int i;
  guint32 result;
  const char *v_STRING;
  char *v_STRING_2;
  guint32 v_UINT32;
  guint32 v_UINT32_2;
  double v_DOUBLE;
  double v_DOUBLE_2;
    
  g_type_init ();
  
  loop = g_main_loop_new (NULL, FALSE);

  error = NULL;
  connection = dbus_g_bus_get (DBUS_BUS_SESSION,
                               &error);
  if (connection == NULL)
    lose_gerror ("Failed to open connection to bus", error);

  /* should always get the same one */
  g_assert (connection == dbus_g_bus_get (DBUS_BUS_SESSION, NULL));
  g_assert (connection == dbus_g_bus_get (DBUS_BUS_SESSION, NULL));
  g_assert (connection == dbus_g_bus_get (DBUS_BUS_SESSION, NULL));
  
  /* Create a proxy object for the "bus driver" */
  
  driver = dbus_g_proxy_new_for_name (connection,
                                      DBUS_SERVICE_DBUS,
                                      DBUS_PATH_DBUS,
                                      DBUS_INTERFACE_DBUS);

  /* Call ListNames method */
  
  call = dbus_g_proxy_begin_call (driver, "ListNames", DBUS_TYPE_INVALID);

  error = NULL;
  if (!dbus_g_proxy_end_call (driver, call, &error,
                              DBUS_TYPE_ARRAY, DBUS_TYPE_STRING,
                              &name_list, &name_list_len,
                              DBUS_TYPE_INVALID))
    lose_gerror ("Failed to complete ListNames call", error);

  g_print ("Names on the message bus:\n");
  i = 0;
  while (i < name_list_len)
    {
      g_assert (name_list[i] != NULL);
      g_print ("  %s\n", name_list[i]);
      ++i;
    }
  g_assert (name_list[i] == NULL);

  g_strfreev (name_list);

  /* Test handling of unknown method */
  v_STRING = "blah blah blah blah blah";
  v_UINT32 = 10;
  call = dbus_g_proxy_begin_call (driver, "ThisMethodDoesNotExist",
                                  DBUS_TYPE_STRING,
                                  &v_STRING,
                                  DBUS_TYPE_INT32,
                                  &v_UINT32,
                                  DBUS_TYPE_INVALID);

  error = NULL;
  if (dbus_g_proxy_end_call (driver, call, &error,
			     DBUS_TYPE_INVALID))
    lose ("Calling nonexistent method succeeded!");

  g_print ("Got EXPECTED error from calling unknown method: %s\n", error->message);
  g_error_free (error);
  
  /* Activate a service */
  v_STRING = "org.freedesktop.DBus.TestSuiteEchoService";
  v_UINT32 = 0;
  call = dbus_g_proxy_begin_call (driver, "StartServiceByName",
                                  DBUS_TYPE_STRING,
                                  &v_STRING,
                                  DBUS_TYPE_UINT32,
                                  &v_UINT32,
                                  DBUS_TYPE_INVALID);

  error = NULL;
  if (!dbus_g_proxy_end_call (driver, call, &error,
                              DBUS_TYPE_UINT32, &result,
                              DBUS_TYPE_INVALID))
    lose_gerror ("Failed to complete Activate call", error);

  g_print ("Starting echo service result = 0x%x\n", result);

  /* Activate a service again */
  v_STRING = "org.freedesktop.DBus.TestSuiteEchoService";
  v_UINT32 = 0;
  call = dbus_g_proxy_begin_call (driver, "StartServiceByName",
                                  DBUS_TYPE_STRING,
                                  &v_STRING,
                                  DBUS_TYPE_UINT32,
                                  &v_UINT32,
                                  DBUS_TYPE_INVALID);

  error = NULL;
  if (!dbus_g_proxy_end_call (driver, call, &error,
                             DBUS_TYPE_UINT32, &result,
                             DBUS_TYPE_INVALID))
    lose_gerror ("Failed to complete Activate call", error);

  g_print ("Duplicate start of echo service = 0x%x\n", result);

  /* Talk to the new service */
  
  proxy = dbus_g_proxy_new_for_name_owner (connection,
                                           "org.freedesktop.DBus.TestSuiteEchoService",
                                           "/org/freedesktop/TestSuite",
                                           "org.freedesktop.TestSuite",
                                           &error);
  
  if (proxy == NULL)
    lose_gerror ("Failed to create proxy for name owner", error);

  v_STRING = "my string hello";
  call = dbus_g_proxy_begin_call (proxy, "Echo",
                                  DBUS_TYPE_STRING,
                                  &v_STRING,
                                  DBUS_TYPE_INVALID);

  error = NULL;
  if (!dbus_g_proxy_end_call (proxy, call, &error,
                              DBUS_TYPE_STRING, &v_STRING,
                              DBUS_TYPE_INVALID))
    lose_gerror ("Failed to complete Echo call", error);

  g_print ("String echoed = \"%s\"\n", v_STRING);

  /* Test oneway call and signal handling */

  dbus_g_proxy_add_signal (proxy, "Foo", DBUS_TYPE_DOUBLE_AS_STRING);
  
  dbus_g_proxy_connect_signal (proxy, "Foo",
                               G_CALLBACK (foo_signal_handler),
                               NULL, NULL);
  
  dbus_g_proxy_call_no_reply (proxy, "EmitFoo",
                              DBUS_TYPE_INVALID);
  
  dbus_g_connection_flush (connection);
  
  g_timeout_add (5000, timed_exit, loop);

  g_main_loop_run (loop);

  if (n_times_foo_received != 1)
    lose ("Foo signal received %d times, should have been 1", n_times_foo_received);
  
  /* Activate test servie */ 
  g_print ("Activating TestSuiteGLibService\n");
  v_STRING = "org.freedesktop.DBus.TestSuiteGLibService";
  v_UINT32 = 0;
  call = dbus_g_proxy_begin_call (driver, "StartServiceByName",
                                  DBUS_TYPE_STRING,
                                  &v_STRING,
                                  DBUS_TYPE_UINT32,
                                  &v_UINT32,
                                  DBUS_TYPE_INVALID);

  error = NULL;
  if (!dbus_g_proxy_end_call (driver, call, &error,
                             DBUS_TYPE_UINT32, &result,
                             DBUS_TYPE_INVALID))
    lose_gerror ("Failed to complete Activate call", error);

  g_object_unref (G_OBJECT (proxy));

  proxy = dbus_g_proxy_new_for_name_owner (connection,
                                           "org.freedesktop.DBus.TestSuiteGLibService",
                                           "/org/freedesktop/DBus/Tests/MyTestObject",
                                           "org.freedesktop.DBus.Tests.MyObject",
                                           &error);
  
  if (proxy == NULL)
    lose_gerror ("Failed to create proxy for name owner", error);

  call = dbus_g_proxy_begin_call (proxy, "DoNothing",
                                  DBUS_TYPE_INVALID);
  error = NULL;
  if (!dbus_g_proxy_end_call (proxy, call, &error, DBUS_TYPE_INVALID))
    lose_gerror ("Failed to complete DoNothing call", error);

  v_UINT32 = 42;
  call = dbus_g_proxy_begin_call (proxy, "Increment",
				  DBUS_TYPE_UINT32, &v_UINT32,
                                  DBUS_TYPE_INVALID);
  error = NULL;
  if (!dbus_g_proxy_end_call (proxy, call, &error,
			      DBUS_TYPE_UINT32, &v_UINT32_2,
			      DBUS_TYPE_INVALID))
    lose_gerror ("Failed to complete Increment call", error);

  if (v_UINT32_2 != v_UINT32 + 1)
    lose ("Increment call returned %d, should be 43", v_UINT32_2);

  call = dbus_g_proxy_begin_call (proxy, "ThrowError", DBUS_TYPE_INVALID);
  error = NULL;
  if (dbus_g_proxy_end_call (proxy, call, &error, DBUS_TYPE_INVALID) != FALSE)
    lose ("ThrowError call unexpectedly succeeded!");

  g_print ("ThrowError failed (as expected) returned error: %s\n", error->message);
  g_error_free (error);

  v_STRING = "foobar";
  call = dbus_g_proxy_begin_call (proxy, "Uppercase",
				  DBUS_TYPE_STRING, &v_STRING,
				  DBUS_TYPE_INVALID);
  error = NULL;
  if (!dbus_g_proxy_end_call (proxy, call, &error,
			      DBUS_TYPE_STRING, &v_STRING_2,
			      DBUS_TYPE_INVALID))
    lose_gerror ("Failed to complete Uppercase call", error);
  if (strcmp ("FOOBAR", v_STRING_2) != 0)
    lose ("Uppercase call returned unexpected string %s", v_STRING_2);

  v_STRING = "bazwhee";
  v_UINT32 = 26;
  v_DOUBLE = G_PI;
  call = dbus_g_proxy_begin_call (proxy, "ManyArgs",
				  DBUS_TYPE_UINT32, &v_UINT32,
				  DBUS_TYPE_STRING, &v_STRING,
				  DBUS_TYPE_DOUBLE, &v_DOUBLE,
				  DBUS_TYPE_INVALID);
  error = NULL;
  if (!dbus_g_proxy_end_call (proxy, call, &error,
			      DBUS_TYPE_DOUBLE, &v_DOUBLE_2,
			      DBUS_TYPE_STRING, &v_STRING_2,
			      DBUS_TYPE_INVALID))
    lose_gerror ("Failed to complete ManyArgs call", error);
  if (v_DOUBLE_2 < 55 || v_DOUBLE_2 > 56)
    lose ("ManyArgs call returned unexpected double value %f", v_DOUBLE_2);
  if (strcmp ("BAZWHEE", v_STRING_2) != 0)
    lose ("ManyArgs call returned unexpected string %s", v_STRING_2);

  if (!org_freedesktop_DBus_Tests_MyObject_do_nothing (proxy, &error))
    lose_gerror ("Failed to complete (wrapped) DoNothing call", error);

  if (!org_freedesktop_DBus_Tests_MyObject_increment (proxy, 42, &v_UINT32_2, &error))
    lose_gerror ("Failed to complete (wrapped) Increment call", error);

  if (v_UINT32_2 != 43)
    lose ("(wrapped) increment call returned %d, should be 43", v_UINT32_2);

  if (org_freedesktop_DBus_Tests_MyObject_throw_error (proxy, &error) != FALSE)
    lose ("(wrapped) ThrowError call unexpectedly succeeded!");

  g_print ("(wrapped) ThrowError failed (as expected) returned error: %s\n", error->message);
  g_error_free (error);

  if (!org_freedesktop_DBus_Tests_MyObject_uppercase (proxy, "foobar", &v_STRING_2, &error)) 
    lose_gerror ("Failed to complete (wrapped) Uppercase call", error);
  if (strcmp ("FOOBAR", v_STRING_2) != 0)
    lose ("(wrapped) Uppercase call returned unexpected string %s", v_STRING_2);

  if (!org_freedesktop_DBus_Tests_MyObject_many_args (proxy, 26, "bazwhee", G_PI,
						      &v_DOUBLE_2, &v_STRING_2, &error))
    lose_gerror ("Failed to complete (wrapped) ManyArgs call", error);

  if (v_DOUBLE_2 < 55 || v_DOUBLE_2 > 56)
    
    lose ("(wrapped) ManyArgs call returned unexpected double value %f", v_DOUBLE_2);

  if (strcmp ("BAZWHEE", v_STRING_2) != 0)
    lose ("(wrapped) ManyArgs call returned unexpected string %s", v_STRING_2);

  g_object_unref (G_OBJECT (proxy));

  proxy = dbus_g_proxy_new_for_name_owner (connection,
                                           "org.freedesktop.DBus.TestSuiteGLibService",
                                           "/org/freedesktop/DBus/Tests/MyTestObject",
                                           "org.freedesktop.DBus.Introspectable",
                                           &error);
  
  if (proxy == NULL)
    lose_gerror ("Failed to create proxy for name owner", error);

  call = dbus_g_proxy_begin_call (proxy, "Introspect",
				  DBUS_TYPE_INVALID);
  error = NULL;
  if (!dbus_g_proxy_end_call (proxy, call, &error,
			      DBUS_TYPE_STRING, &v_STRING,
			      DBUS_TYPE_INVALID))
    lose_gerror ("Failed to complete Introspect call", error);

  /* Could just do strcmp(), but that seems more fragile */
  {
    NodeInfo *node;
    GSList *elt;
    gboolean found_introspectable;
    gboolean found_properties;
    gboolean found_myobject;
    gboolean found_gtk_myobject;

    node = description_load_from_string (v_STRING, strlen (v_STRING), &error);
    if (!node)
      lose_gerror ("Failed to parse introspection data: %s", error);

    found_introspectable = FALSE;
    found_properties = FALSE;
    found_gtk_myobject = FALSE;
    found_myobject = FALSE;
    for (elt = node_info_get_interfaces (node); elt ; elt = elt->next)
      {
	InterfaceInfo *iface = elt->data;

	if (!found_introspectable && strcmp (interface_info_get_name (iface), "org.freedesktop.DBus.Introspectable") == 0)
	  found_introspectable = TRUE;
	else if (!found_properties && strcmp (interface_info_get_name (iface), "org.freedesktop.DBus.Properties") == 0)
	  found_properties = TRUE;
	else if (!found_gtk_myobject && strcmp (interface_info_get_name (iface), "org.gtk.objects.MyObject") == 0)
	  found_gtk_myobject = TRUE;
	else if (!found_myobject && strcmp (interface_info_get_name (iface), "org.freedesktop.DBus.Tests.MyObject") == 0)
	  {
	    GSList *elt;
	    gboolean found_manyargs;
	    
	    found_myobject = TRUE;
	    
	    found_manyargs = FALSE;
	    for (elt = interface_info_get_methods (iface); elt; elt = elt->next)
	      {
		MethodInfo *method;

		method = elt->data;
		if (strcmp (method_info_get_name (method), "ManyArgs") == 0)
		  {
		    found_manyargs = TRUE;
		    break;
		  }
	      }
	    if (!found_manyargs)
	      lose ("Missing method org.freedesktop.DBus.Tests.MyObject.ManyArgs");
	  }
	else
	  lose ("Unexpected or duplicate interface %s", interface_info_get_name (iface));
      }

    if (!(found_introspectable && found_gtk_myobject && found_myobject && found_properties))
      lose ("Missing interface"); 
  }
  
  g_object_unref (G_OBJECT (driver));

  g_print ("Successfully completed %s\n", argv[0]);
  
  return 0;
}
