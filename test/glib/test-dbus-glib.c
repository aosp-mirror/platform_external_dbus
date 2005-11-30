/* -*- mode: C; c-file-style: "gnu" -*- */
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test-service-glib-bindings.h"
#include <glib/dbus-gidl.h>
#include <glib/dbus-gparser.h>
#include <glib.h>
#include <glib-object.h>
#include "my-object-marshal.h"

static GMainLoop *loop = NULL;
static const char *await_terminating_service = NULL;
static int n_times_foo_received = 0;
static int n_times_frobnicate_received = 0;
static int n_times_frobnicate_received_2 = 0;
static int n_times_sig0_received = 0;
static int n_times_sig1_received = 0;
static int n_times_sig2_received = 0;
static guint exit_timeout = 0;
static gboolean proxy_destroyed = FALSE;
static gboolean proxy_destroy_and_nameowner = FALSE;
static gboolean proxy_destroy_and_nameowner_complete = FALSE;

static void lose (const char *fmt, ...) G_GNUC_NORETURN G_GNUC_PRINTF (1, 2);
static void lose_gerror (const char *prefix, GError *error) G_GNUC_NORETURN;

static void
unset_and_free_gvalue (gpointer val)
{
  g_value_unset (val);
  g_free (val);
}

static gboolean
timed_exit (gpointer loop)
{
  g_print ("timed exit!\n");
  g_main_loop_quit (loop);
  return TRUE;
}

static void
proxy_destroyed_cb (DBusGProxy *proxy, gpointer user_data)
{
  proxy_destroyed = TRUE;
  if (proxy_destroy_and_nameowner && !proxy_destroy_and_nameowner_complete && await_terminating_service == NULL)
    {
      g_source_remove (exit_timeout);
      g_main_loop_quit (loop);
      proxy_destroy_and_nameowner_complete = TRUE;
    } 
}

static void
name_owner_changed (DBusGProxy *proxy,
		    const char *name,
		    const char *prev_owner,
		    const char *new_owner,
		    gpointer   user_data)
{
  g_print ("(signal NameOwnerChanged) name owner changed for %s from %s to %s\n",
	   name, prev_owner, new_owner);
  if (await_terminating_service &&
      !strcmp (name, await_terminating_service)
      && !strcmp ("", new_owner))
    {
      g_print ("Caught expected ownership loss for %s\n", name); 
      await_terminating_service = NULL;
      if (proxy_destroy_and_nameowner && !proxy_destroy_and_nameowner_complete && proxy_destroyed)
	{
	  g_source_remove (exit_timeout);
	  g_main_loop_quit (loop);
	  proxy_destroy_and_nameowner_complete = TRUE;
	} 
      else if (!proxy_destroy_and_nameowner)
	{
	  g_source_remove (exit_timeout);
	  g_main_loop_quit (loop);
	}
    }
}

static void
foo_signal_handler (DBusGProxy  *proxy,
                    double       d,
                    void        *user_data)
{
  n_times_foo_received += 1;

  g_print ("Got Foo signal\n");

  g_main_loop_quit (loop);
  g_source_remove (exit_timeout);
}

static void
frobnicate_signal_handler (DBusGProxy  *proxy,
			   int          val,
			   void        *user_data)
{
  n_times_frobnicate_received += 1;

  g_assert (val == 42);
  g_print ("Got Frobnicate signal\n");

  g_main_loop_quit (loop);
  g_source_remove (exit_timeout);
}

static void
frobnicate_signal_handler_2 (DBusGProxy  *proxy,
			     int          val,
			     void        *user_data)
{
  n_times_frobnicate_received_2 += 1;

  g_assert (val == 42);
  g_print ("Got Frobnicate signal (again)\n");
}

static void
sig0_signal_handler (DBusGProxy  *proxy,
		     const char  *str0,
		     int          val,
		     const char  *str1,
		     void        *user_data)
{
  n_times_sig0_received += 1;

  g_assert (!strcmp (str0, "foo"));

  g_assert (val == 22);

  g_assert (!strcmp (str1, "moo"));

  g_print ("Got Sig0 signal\n");

  g_main_loop_quit (loop);
  g_source_remove (exit_timeout);
}

static void
sig1_signal_handler (DBusGProxy  *proxy,
		     const char  *str0,
		     GValue      *value,
		     void        *user_data)
{
  n_times_sig1_received += 1;

  g_assert (!strcmp (str0, "baz"));

  g_assert (G_VALUE_HOLDS_STRING (value));

  g_assert (!strcmp (g_value_get_string (value), "bar"));

  g_print ("Got Sig1 signal\n");

  g_main_loop_quit (loop);
  g_source_remove (exit_timeout);
}

static void
sig2_signal_handler (DBusGProxy  *proxy,
		     GHashTable  *table,
		     void        *user_data)
{
  n_times_sig2_received += 1;

  g_assert (g_hash_table_size (table) == 2);

  g_assert (g_hash_table_lookup (table, "baz") != NULL);
  g_assert (!strcmp (g_hash_table_lookup (table, "baz"), "cow"));
  g_assert (g_hash_table_lookup (table, "bar") != NULL);
  g_assert (!strcmp (g_hash_table_lookup (table, "bar"), "foo"));

  g_print ("Got Sig2 signal\n");

  g_main_loop_quit (loop);
  g_source_remove (exit_timeout);
}

static DBusGProxyCall *echo_call;
static guint n_times_echo_cb_entered;
static void
echo_received_cb (DBusGProxy *proxy,
		  DBusGProxyCall *call,
		  gpointer data)
{
  GError *error;
  char *echo_data;

  g_assert (call == echo_call);
  g_assert (data == NULL);

  error = NULL;
  echo_data = NULL;
  n_times_echo_cb_entered++;

  if (!dbus_g_proxy_end_call (proxy, call, &error,
			      G_TYPE_STRING,
			      &echo_data,
			      G_TYPE_INVALID))
    lose_gerror ("Failed to complete async Echo", error);
  g_assert (echo_data != NULL);
  g_print ("Async echo gave \"%s\"\n", echo_data); 
  g_free (echo_data);
  g_main_loop_quit (loop);
  g_source_remove (exit_timeout);
}

static void
increment_received_cb (DBusGProxy *proxy,
		       DBusGProxyCall *call,
		       gpointer data)
{
  GError *error;
  guint val;

  g_assert (!strcmp (data, "moo"));

  error = NULL;
  if (!dbus_g_proxy_end_call (proxy, call, &error,
			      G_TYPE_UINT, &val,
			      G_TYPE_INVALID))
    lose_gerror ("Failed to complete (async) Increment call", error);

  if (val != 43)
    lose ("Increment call returned %d, should be 43", val);
  
  g_print ("Async increment gave \"%d\"\n", val); 
  g_main_loop_quit (loop);
  g_source_remove (exit_timeout);
}

static void
increment_async_cb (DBusGProxy *proxy, guint val, GError *error, gpointer data)
{
  if (error)
    lose_gerror ("Failed to complete (wrapped async) Increment call", error);

  if (data != NULL)
    lose ("(wrapped async) Increment call gave unexpected data");
  if (val != 43)
    lose ("(wrapped async) Increment call returned %d, should be 43", val);

  g_print ("(wrapped async) increment gave \"%d\"\n", val); 
  g_main_loop_quit (loop);
  g_source_remove (exit_timeout);
}


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
  if (error->domain == DBUS_GERROR && error->code == DBUS_GERROR_REMOTE_EXCEPTION)
    lose ("%s (%s): %s", prefix, dbus_g_error_get_name (error),
	  error->message);
  else
    lose ("%s: %s", prefix, error->message);
}

static void
run_mainloop (void)
{
  GMainContext *ctx;

  ctx = g_main_loop_get_context (loop);

  while (g_main_context_pending (ctx))
    g_main_context_iteration (ctx, FALSE);
}

int
main (int argc, char **argv)
{
  DBusGConnection *connection;
  GError *error;
  DBusGProxy *driver;
  DBusGProxy *proxy;
  DBusGProxy *proxy2;
  char **name_list;
  guint name_list_len;
  guint i;
  DBusGProxyCall *call;
  guint32 result;
  char *v_STRING_2;
  guint32 v_UINT32_2;
  double v_DOUBLE_2;
    
  g_type_init ();

  g_log_set_always_fatal (G_LOG_LEVEL_WARNING);
  
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

  dbus_g_proxy_add_signal (driver,
			   "NameOwnerChanged",
			   G_TYPE_STRING,
			   G_TYPE_STRING,
			   G_TYPE_STRING,
			   G_TYPE_INVALID);
  
  dbus_g_proxy_connect_signal (driver,
			       "NameOwnerChanged", 
			       G_CALLBACK (name_owner_changed),
			       NULL,
			       NULL);
  /* Call ListNames method */
  
  error = NULL;
  if (!dbus_g_proxy_call (driver, "ListNames", &error,
			  G_TYPE_INVALID,
			  G_TYPE_STRV, &name_list,
			  G_TYPE_INVALID))
    lose_gerror ("Failed to complete ListNames call", error);

  g_print ("Names on the message bus:\n");
  i = 0;
  name_list_len = g_strv_length (name_list);
  while (i < name_list_len)
    {
      g_assert (name_list[i] != NULL);
      g_print ("  %s\n", name_list[i]);
      ++i;
    }
  g_assert (name_list[i] == NULL);

  g_strfreev (name_list);

  g_print ("calling ThisMethodDoesNotExist\n");
  /* Test handling of unknown method */
  if (dbus_g_proxy_call (driver, "ThisMethodDoesNotExist", &error,
			 G_TYPE_STRING,
			 "blah blah blah blah blah",
			 G_TYPE_INT,
			 10,
			 G_TYPE_INVALID, G_TYPE_INVALID) != FALSE)
    lose ("Calling nonexistent method succeeded!");

  g_print ("Got EXPECTED error from calling unknown method: %s\n", error->message);
  g_clear_error (&error);

  run_mainloop ();
  
  /* Activate a service */
  g_print ("Activating echo service\n");
  if (!dbus_g_proxy_call (driver, "StartServiceByName", &error,
			  G_TYPE_STRING,
			  "org.freedesktop.DBus.TestSuiteEchoService",
			  G_TYPE_UINT, 0,
			  G_TYPE_INVALID,
			  G_TYPE_UINT, &result,
			  G_TYPE_INVALID))
    lose_gerror ("Failed to complete Activate call", error);

  g_print ("Starting echo service result = 0x%x\n", result);

  /* Activate a service again */
  g_print ("Activating echo service again\n");
  if (!dbus_g_proxy_call (driver, "StartServiceByName", &error,
			  G_TYPE_STRING,
			  "org.freedesktop.DBus.TestSuiteEchoService",
			  G_TYPE_UINT,
			  0,
			  G_TYPE_INVALID,
			  G_TYPE_UINT, &result,
			  G_TYPE_INVALID))
    lose_gerror ("Failed to complete Activate call", error);

  g_print ("Duplicate start of echo service = 0x%x\n", result);

  /* Talk to the new service */
  
  g_print ("Creating proxy for echo service\n");
  proxy = dbus_g_proxy_new_for_name_owner (connection,
                                           "org.freedesktop.DBus.TestSuiteEchoService",
                                           "/org/freedesktop/TestSuite",
                                           "org.freedesktop.TestSuite",
                                           &error);
  
  if (proxy == NULL)
    lose_gerror ("Failed to create proxy for name owner", error);

  run_mainloop ();

  g_print ("Calling Echo\n");
  if (!dbus_g_proxy_call (proxy, "Echo", &error,
			  G_TYPE_STRING, "my string hello",
			  G_TYPE_INVALID,
			  G_TYPE_STRING, &v_STRING_2,
			  G_TYPE_INVALID))
    lose_gerror ("Failed to complete Echo call", error);

  g_print ("String echoed = \"%s\"\n", v_STRING_2);
  g_free (v_STRING_2);

  g_print ("Calling Echo (async)\n");
  echo_call = dbus_g_proxy_begin_call (proxy, "Echo",
				       echo_received_cb, NULL, NULL,
				       G_TYPE_STRING, "my string hello",
				       G_TYPE_INVALID);
  dbus_g_connection_flush (connection);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);

  /* Test oneway call and signal handling */

  g_print ("Testing Foo emission\n");
  dbus_g_proxy_add_signal (proxy, "Foo", G_TYPE_DOUBLE, G_TYPE_INVALID);
  
  dbus_g_proxy_connect_signal (proxy, "Foo",
                               G_CALLBACK (foo_signal_handler),
                               NULL, NULL);
  
  dbus_g_proxy_call_no_reply (proxy, "EmitFoo",
                              G_TYPE_INVALID);
  
  dbus_g_connection_flush (connection);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);

  if (n_times_foo_received != 1)
    lose ("Foo signal received %d times, should have been 1", n_times_foo_received);
  
  /* Activate test servie */ 
  g_print ("Activating TestSuiteGLibService\n");
  error = NULL;
  if (!dbus_g_proxy_call (driver, "StartServiceByName", &error,
			  G_TYPE_STRING,
			  "org.freedesktop.DBus.TestSuiteGLibService",
			  G_TYPE_UINT,
			  0,
			  G_TYPE_INVALID,
			  G_TYPE_UINT, &result,
			  G_TYPE_INVALID)) {
    lose_gerror ("Failed to complete Activate call", error);
  }

  g_print ("TestSuiteGLibService activated\n");

  if (getenv ("DBUS_GLIB_TEST_SLEEP_AFTER_ACTIVATION"))
    g_usleep (8 * G_USEC_PER_SEC);

  g_object_unref (G_OBJECT (proxy));

  run_mainloop ();

  proxy = dbus_g_proxy_new_for_name_owner (connection,
                                           "org.freedesktop.DBus.TestSuiteGLibService",
                                           "/org/freedesktop/DBus/Tests/MyTestObject",
                                           "org.freedesktop.DBus.Tests.MyObject",
                                           &error);
  
  if (proxy == NULL)
    lose_gerror ("Failed to create proxy for name owner", error);

  g_print ("Calling DoNothing\n");
  if (!dbus_g_proxy_call (proxy, "DoNothing", &error,
			  G_TYPE_INVALID, G_TYPE_INVALID))
    lose_gerror ("Failed to complete DoNothing call", error);

  g_print ("Calling Increment\n");
  error = NULL;
  if (!dbus_g_proxy_call (proxy, "Increment", &error,
			  G_TYPE_UINT, 42,
			  G_TYPE_INVALID,
			  G_TYPE_UINT, &v_UINT32_2,
			  G_TYPE_INVALID))
    lose_gerror ("Failed to complete Increment call", error);
  if (v_UINT32_2 != 43)
    lose ("Increment call returned %d, should be 43", v_UINT32_2);

  v_UINT32_2 = 0;
  g_print ("Calling Increment (async)\n");
  call = dbus_g_proxy_begin_call (proxy, "Increment",
				  increment_received_cb, g_strdup ("moo"), g_free,
				  G_TYPE_UINT, 42,
				  G_TYPE_INVALID);
  dbus_g_connection_flush (connection);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);

  g_print ("Calling IncrementRetval\n");
  error = NULL;
  v_UINT32_2 = 0;
  if (!dbus_g_proxy_call (proxy, "IncrementRetval", &error,
			  G_TYPE_UINT, 42,
			  G_TYPE_INVALID,
			  G_TYPE_UINT, &v_UINT32_2,
			  G_TYPE_INVALID))
    lose_gerror ("Failed to complete Increment call", error);
  if (v_UINT32_2 != 43)
    lose ("IncrementRetval call returned %d, should be 43", v_UINT32_2);

  g_print ("Calling IncrementRetvalError\n");
  error = NULL;
  v_UINT32_2 = 0;
  if (!dbus_g_proxy_call (proxy, "IncrementRetvalError", &error,
			  G_TYPE_UINT, 5,
			  G_TYPE_INVALID,
			  G_TYPE_UINT, &v_UINT32_2,
			  G_TYPE_INVALID))
    lose_gerror ("Failed to complete Increment call", error);
  if (v_UINT32_2 != 6)
    lose ("IncrementRetval call returned %d, should be 6", v_UINT32_2);

  g_print ("Calling ThrowError\n");
  if (dbus_g_proxy_call (proxy, "ThrowError", &error,
			 G_TYPE_INVALID, G_TYPE_INVALID) != FALSE)
    lose ("ThrowError call unexpectedly succeeded!");

  if (!dbus_g_error_has_name (error, "org.freedesktop.DBus.Tests.MyObject.Foo"))
    lose ("ThrowError call returned unexpected error \"%s\": %s", dbus_g_error_get_name (error),
	  error->message);

  g_print ("ThrowError failed (as expected) returned error: %s\n", error->message);
  g_clear_error (&error);

  g_print ("Calling IncrementRetvalError (for error)\n");
  error = NULL;
  v_UINT32_2 = 0;
  if (dbus_g_proxy_call (proxy, "IncrementRetvalError", &error,
			 G_TYPE_UINT, 20,
			 G_TYPE_INVALID,
			 G_TYPE_UINT, &v_UINT32_2,
			 G_TYPE_INVALID) != FALSE)
    lose ("IncrementRetvalError call unexpectedly succeeded!");
  if (!dbus_g_error_has_name (error, "org.freedesktop.DBus.Tests.MyObject.Foo"))
    lose ("IncrementRetvalError call returned unexpected error \"%s\": %s", dbus_g_error_get_name (error), error->message);
  g_clear_error (&error);

  error = NULL;
  g_print ("Calling Uppercase\n");
  if (!dbus_g_proxy_call (proxy, "Uppercase", &error,
			  G_TYPE_STRING, "foobar",
			  G_TYPE_INVALID,
			  G_TYPE_STRING, &v_STRING_2,
			  G_TYPE_INVALID))
    lose_gerror ("Failed to complete Uppercase call", error);
  if (strcmp ("FOOBAR", v_STRING_2) != 0)
    lose ("Uppercase call returned unexpected string %s", v_STRING_2);
  g_free (v_STRING_2);

  run_mainloop ();

  g_print ("Calling ManyArgs\n");
  if (!dbus_g_proxy_call (proxy, "ManyArgs", &error,
			  G_TYPE_UINT, 26,
			  G_TYPE_STRING, "bazwhee",
			  G_TYPE_DOUBLE, G_PI,
			  G_TYPE_INVALID,
			  G_TYPE_DOUBLE, &v_DOUBLE_2,
			  G_TYPE_STRING, &v_STRING_2,
			  G_TYPE_INVALID))
    lose_gerror ("Failed to complete ManyArgs call", error);
  if (v_DOUBLE_2 < 55 || v_DOUBLE_2 > 56)
    lose ("ManyArgs call returned unexpected double value %f", v_DOUBLE_2);
  if (strcmp ("BAZWHEE", v_STRING_2) != 0)
    lose ("ManyArgs call returned unexpected string %s", v_STRING_2);
  g_free (v_STRING_2);

  g_print ("Calling (wrapped) do_nothing\n");
  if (!org_freedesktop_DBus_Tests_MyObject_do_nothing (proxy, &error))
    lose_gerror ("Failed to complete (wrapped) DoNothing call", error);

  g_print ("Calling (wrapped) increment\n");
  if (!org_freedesktop_DBus_Tests_MyObject_increment (proxy, 42, &v_UINT32_2, &error))
    lose_gerror ("Failed to complete (wrapped) Increment call", error);

  if (v_UINT32_2 != 43)
    lose ("(wrapped) increment call returned %d, should be 43", v_UINT32_2);

  g_print ("Calling (wrapped async) increment\n");
  if (!org_freedesktop_DBus_Tests_MyObject_increment_async (proxy, 42, increment_async_cb, NULL))
    lose_gerror ("Failed to complete (wrapped) Increment call", error);
  dbus_g_connection_flush (connection);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);

  v_UINT32_2 = 0;
  if (!org_freedesktop_DBus_Tests_MyObject_async_increment (proxy, 42, &v_UINT32_2, &error))
    lose_gerror ("Failed to complete (wrapped) AsyncIncrement call", error);

  if (v_UINT32_2 != 43)
    lose ("(wrapped) async increment call returned %d, should be 43", v_UINT32_2);

  g_print ("Calling (wrapped) throw_error\n");
  if (org_freedesktop_DBus_Tests_MyObject_throw_error (proxy, &error) != FALSE)
    lose ("(wrapped) ThrowError call unexpectedly succeeded!");

  g_print ("(wrapped) ThrowError failed (as expected) returned error: %s\n", error->message);
  g_clear_error (&error);

  if (org_freedesktop_DBus_Tests_MyObject_async_throw_error (proxy, &error) != FALSE)
    lose ("(wrapped) AsyncThrowError call unexpectedly succeeded!");

  g_print ("(wrapped) AsyncThrowError failed (as expected) returned error: %s\n", error->message);
  g_clear_error (&error);

  g_print ("Calling (wrapped) uppercase\n");
  if (!org_freedesktop_DBus_Tests_MyObject_uppercase (proxy, "foobar", &v_STRING_2, &error)) 
    lose_gerror ("Failed to complete (wrapped) Uppercase call", error);
  if (strcmp ("FOOBAR", v_STRING_2) != 0)
    lose ("(wrapped) Uppercase call returned unexpected string %s", v_STRING_2);
  g_free (v_STRING_2);

  g_print ("Calling (wrapped) many_args\n");
  if (!org_freedesktop_DBus_Tests_MyObject_many_args (proxy, 26, "bazwhee", G_PI,
						      &v_DOUBLE_2, &v_STRING_2, &error))
    lose_gerror ("Failed to complete (wrapped) ManyArgs call", error);

  if (v_DOUBLE_2 < 55 || v_DOUBLE_2 > 56)
    
    lose ("(wrapped) ManyArgs call returned unexpected double value %f", v_DOUBLE_2);

  if (strcmp ("BAZWHEE", v_STRING_2) != 0)
    lose ("(wrapped) ManyArgs call returned unexpected string %s", v_STRING_2);
  g_free (v_STRING_2);

  {
    guint32 arg0;
    char *arg1;
    gint32 arg2;
    guint32 arg3;
    guint32 arg4;
    char *arg5;
    
    g_print ("Calling (wrapped) many_return\n");
    if (!org_freedesktop_DBus_Tests_MyObject_many_return (proxy, &arg0, &arg1, &arg2, &arg3, &arg4, &arg5, &error))
      lose_gerror ("Failed to complete (wrapped) ManyReturn call", error);

    if (arg0 != 42)
      lose ("(wrapped) ManyReturn call returned unexpected guint32 value %u", arg0);

    if (strcmp ("42", arg1) != 0)
      lose ("(wrapped) ManyReturn call returned unexpected string %s", arg1);
    g_free (arg1);

    if (arg2 != -67)
      lose ("(wrapped) ManyReturn call returned unexpected gint32 value %u", arg2);

    if (arg3 != 2)
      lose ("(wrapped) ManyReturn call returned unexpected guint32 value %u", arg3);

    if (arg4 != 26)
      lose ("(wrapped) ManyReturn call returned unexpected guint32 value %u", arg4);

    if (strcmp ("hello world", arg5))
      lose ("(wrapped) ManyReturn call returned unexpected string %s", arg5);
    g_free (arg5);
  }

  run_mainloop ();

  {
    GValue value = {0, };

    g_value_init (&value, G_TYPE_STRING);
    g_value_set_string (&value, "foo");

    g_print ("Calling (wrapped) stringify, with string\n");
    if (!org_freedesktop_DBus_Tests_MyObject_stringify (proxy,
							&value,
							&v_STRING_2,
							&error))
      lose_gerror ("Failed to complete (wrapped) stringify call", error);
    if (strcmp ("foo", v_STRING_2) != 0)
      lose ("(wrapped) stringify call returned unexpected string %s", v_STRING_2);
    g_free (v_STRING_2);

    g_value_unset (&value);
    g_value_init (&value, G_TYPE_INT);
    g_value_set_int (&value, 42);

    g_print ("Calling (wrapped) stringify, with int\n");
    if (!org_freedesktop_DBus_Tests_MyObject_stringify (proxy,
							&value,
							&v_STRING_2,
							&error))
      lose_gerror ("Failed to complete (wrapped) stringify call 2", error);
    if (strcmp ("42", v_STRING_2) != 0)
      lose ("(wrapped) stringify call 2 returned unexpected string %s", v_STRING_2);
    g_value_unset (&value);
    g_free (v_STRING_2);

    g_value_init (&value, G_TYPE_INT);
    g_value_set_int (&value, 88);
    g_print ("Calling (wrapped) stringify, with another int\n");
    if (!org_freedesktop_DBus_Tests_MyObject_stringify (proxy,
							&value,
							NULL,
							&error))
      lose_gerror ("Failed to complete (wrapped) stringify call 3", error);
    g_value_unset (&value);

    g_print ("Calling (wrapped) unstringify, for string\n");
    if (!org_freedesktop_DBus_Tests_MyObject_unstringify (proxy,
							  "foo",
							  &value,
							  &error))
      lose_gerror ("Failed to complete (wrapped) unstringify call", error);
    if (!G_VALUE_HOLDS_STRING (&value))
      lose ("(wrapped) unstringify call returned unexpected value type %d", (int) G_VALUE_TYPE (&value));
    if (strcmp (g_value_get_string (&value), "foo"))
      lose ("(wrapped) unstringify call returned unexpected string %s",
	    g_value_get_string (&value));
	
    g_value_unset (&value);

    g_print ("Calling (wrapped) unstringify, for int\n");
    if (!org_freedesktop_DBus_Tests_MyObject_unstringify (proxy,
							  "10",
							  &value,
							  &error))
      lose_gerror ("Failed to complete (wrapped) unstringify call", error);
    if (!G_VALUE_HOLDS_INT (&value))
      lose ("(wrapped) unstringify call returned unexpected value type %d", (int) G_VALUE_TYPE (&value));
    if (g_value_get_int (&value) != 10)
      lose ("(wrapped) unstringify call returned unexpected integer %d",
	    g_value_get_int (&value));

    g_value_unset (&value);
  }

  run_mainloop ();

  {
    GArray *array;
    guint32 val;
    guint32 arraylen;

    array = g_array_new (FALSE, TRUE, sizeof (guint32));
    val = 42;
    g_array_append_val (array, val);
    val = 69;
    g_array_append_val (array, val);
    val = 88;
    g_array_append_val (array, val);
    val = 26;
    g_array_append_val (array, val);
    val = 2;
    g_array_append_val (array, val);

    arraylen = 0;
    g_print ("Calling (wrapped) recursive1\n");
    if (!org_freedesktop_DBus_Tests_MyObject_recursive1 (proxy, array,
							 &arraylen, &error))
      lose_gerror ("Failed to complete (wrapped) recursive1 call", error);
    if (arraylen != 5)
      lose ("(wrapped) recursive1 call returned invalid length %u", arraylen);
  }

  {
    GArray *array = NULL;
    guint32 *arrayvals;
    
    g_print ("Calling (wrapped) recursive2\n");
    if (!org_freedesktop_DBus_Tests_MyObject_recursive2 (proxy, 2, &array, &error))
      lose_gerror ("Failed to complete (wrapped) Recursive2 call", error);

    if (array == NULL)
      lose ("(wrapped) Recursive2 call returned NULL");
    if (array->len != 5)
      lose ("(wrapped) Recursive2 call returned unexpected array length %u", array->len);

    arrayvals = (guint32*) array->data;
    if (arrayvals[0] != 42)
      lose ("(wrapped) Recursive2 call returned unexpected value %d in position 0", arrayvals[0]);
    if (arrayvals[1] != 26)
      lose ("(wrapped) Recursive2 call returned unexpected value %d in position 1", arrayvals[1]);
    if (arrayvals[4] != 2)
      lose ("(wrapped) Recursive2 call returned unexpected value %d in position 4", arrayvals[4]);

    g_array_free (array, TRUE);
  }

  run_mainloop ();

  {
    char **strs;
    char **strs_ret;

    strs = g_new0 (char *, 4);
    strs[0] = "hello";
    strs[1] = "HellO";
    strs[2] = "HELLO";
    strs[3] = NULL;

    strs_ret = NULL;
    g_print ("Calling (wrapped) many_uppercase\n");
    if (!org_freedesktop_DBus_Tests_MyObject_many_uppercase (proxy, strs, &strs_ret, &error)) 
      lose_gerror ("Failed to complete (wrapped) ManyUppercase call", error);
    g_assert (strs_ret != NULL);
    if (strcmp ("HELLO", strs_ret[0]) != 0)
      lose ("(wrapped) ManyUppercase call returned unexpected string %s", strs_ret[0]);
    if (strcmp ("HELLO", strs_ret[1]) != 0)
      lose ("(wrapped) ManyUppercase call returned unexpected string %s", strs_ret[1]);
    if (strcmp ("HELLO", strs_ret[2]) != 0)
      lose ("(wrapped) ManyUppercase call returned unexpected string %s", strs_ret[2]);

    g_free (strs);
    g_strfreev (strs_ret);
  }

  {
    GHashTable *table;
    guint len;

    table = g_hash_table_new (g_str_hash, g_str_equal);
    g_hash_table_insert (table, "moooo", "b");
    g_hash_table_insert (table, "xxx", "cow!");

    len = 0;
    g_print ("Calling (wrapped) str_hash_len\n");
    if (!org_freedesktop_DBus_Tests_MyObject_str_hash_len (proxy, table, &len, &error))
      lose_gerror ("(wrapped) StrHashLen call failed", error);
    if (len != 13) 
      lose ("(wrapped) StrHashLen returned unexpected length %u", len);
    g_hash_table_destroy (table);
  }

  {
    GHashTable *table;
    const char *val;

    g_print ("Calling (wrapped) get_hash\n");
    if (!org_freedesktop_DBus_Tests_MyObject_get_hash (proxy, &table, &error))
      lose_gerror ("(wrapped) GetHash call failed", error);
    val = g_hash_table_lookup (table, "foo");
    if (val == NULL || strcmp ("bar", val))
      lose ("(wrapped) StrHashLen returned invalid value %s for key \"foo\"",
	    val ? val : "(null)");
    val = g_hash_table_lookup (table, "baz");
    if (val == NULL || strcmp ("whee", val))
      lose ("(wrapped) StrHashLen returned invalid value %s for key \"whee\"",
	    val ? val : "(null)");
    val = g_hash_table_lookup (table, "cow");
    if (val == NULL || strcmp ("crack", val))
      lose ("(wrapped) StrHashLen returned invalid value %s for key \"cow\"",
	    val ? val : "(null)");
    if (g_hash_table_size (table) != 3)
      lose ("(wrapped) StrHashLen returned unexpected hash size %u",
	    g_hash_table_size (table));

    g_hash_table_destroy (table);
  }

  run_mainloop ();

  {
    GValueArray *vals;
    GValueArray *vals_ret;
    GValue *val;

    vals = g_value_array_new (3);

    g_value_array_append (vals, NULL);
    g_value_init (g_value_array_get_nth (vals, vals->n_values - 1), G_TYPE_STRING);
    g_value_set_string (g_value_array_get_nth (vals, 0), "foo");

    g_value_array_append (vals, NULL);
    g_value_init (g_value_array_get_nth (vals, vals->n_values - 1), G_TYPE_UINT);
    g_value_set_uint (g_value_array_get_nth (vals, vals->n_values - 1), 42);

    g_value_array_append (vals, NULL);
    g_value_init (g_value_array_get_nth (vals, vals->n_values - 1), G_TYPE_VALUE);
    val = g_new0 (GValue, 1);
    g_value_init (val, G_TYPE_UCHAR);
    g_value_set_uchar (val, '!');
    g_value_set_boxed (g_value_array_get_nth (vals, vals->n_values - 1), val);

    vals_ret = NULL;
    g_print ("Calling SendCar\n");
    if (!dbus_g_proxy_call (proxy, "SendCar", &error,
			    G_TYPE_VALUE_ARRAY, vals,
			    G_TYPE_INVALID,
			    G_TYPE_VALUE_ARRAY, &vals_ret,
			    G_TYPE_INVALID))
      lose_gerror ("Failed to complete SendCar call", error);

    g_assert (vals_ret != NULL);
    g_assert (vals_ret->n_values == 2);

    g_assert (G_VALUE_HOLDS_UINT (g_value_array_get_nth (vals_ret, 0)));
    g_assert (g_value_get_uint (g_value_array_get_nth (vals_ret, 0)) == 43);
    
    g_assert (G_VALUE_TYPE (g_value_array_get_nth (vals_ret, 1)) == DBUS_TYPE_G_OBJECT_PATH);
    g_assert (!strcmp ("/org/freedesktop/DBus/Tests/MyTestObject2",
		       g_value_get_boxed (g_value_array_get_nth (vals_ret, 1))));

    g_value_array_free (vals);
    g_value_array_free (vals_ret);
  }

  {
    GValue *val;
    GHashTable *table;
    GHashTable *ret_table;

    table = g_hash_table_new_full (g_str_hash, g_str_equal,
				   g_free, unset_and_free_gvalue);
    
    val = g_new0 (GValue, 1);
    g_value_init (val, G_TYPE_UINT);
    g_value_set_uint (val, 42);
    g_hash_table_insert (table, g_strdup ("foo"), val);

    val = g_new0 (GValue, 1);
    g_value_init (val, G_TYPE_STRING);
    g_value_set_string (val, "hello");
    g_hash_table_insert (table, g_strdup ("bar"), val);

    ret_table = NULL;
    g_print ("Calling ManyStringify\n");
    if (!dbus_g_proxy_call (proxy, "ManyStringify", &error,
			    dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), table,
			    G_TYPE_INVALID,
			    dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &ret_table,
			    G_TYPE_INVALID))
      lose_gerror ("Failed to complete ManyStringify call", error);

    g_assert (ret_table != NULL);
    g_assert (g_hash_table_size (ret_table) == 2);

    val = g_hash_table_lookup (ret_table, "foo");
    g_assert (val != NULL);
    g_assert (G_VALUE_HOLDS_STRING (val));
    g_assert (!strcmp ("42", g_value_get_string (val)));

    val = g_hash_table_lookup (ret_table, "bar");
    g_assert (val != NULL);
    g_assert (G_VALUE_HOLDS_STRING (val));
    g_assert (!strcmp ("hello", g_value_get_string (val)));

    g_hash_table_destroy (table);
    g_hash_table_destroy (ret_table);
  }

  {
    GPtrArray *in_array;
    GPtrArray *out_array;
    char **strs;
    GArray *uints;

    in_array = g_ptr_array_new ();

    strs = g_new0 (char *, 3);
    strs[0] = "foo";
    strs[1] = "bar";
    strs[2] = NULL;
    g_ptr_array_add (in_array, strs);

    strs = g_new0 (char *, 4);
    strs[0] = "baz";
    strs[1] = "whee";
    strs[2] = "moo";
    strs[3] = NULL;
    g_ptr_array_add (in_array, strs);

    out_array = NULL;
    g_print ("Calling RecArrays\n");
    if (!dbus_g_proxy_call (proxy, "RecArrays", &error,
			    dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRV), in_array,
			    G_TYPE_INVALID,
			    dbus_g_type_get_collection ("GPtrArray",
							dbus_g_type_get_collection ("GPtrArray",
										    G_TYPE_UINT)), &out_array, 
			    G_TYPE_INVALID))
      lose_gerror ("Failed to complete RecArrays call", error);
    g_free (g_ptr_array_index (in_array, 0));
    g_free (g_ptr_array_index (in_array, 1));

    g_assert (out_array);
    g_assert (out_array->len == 2);
    uints = g_ptr_array_index (out_array, 0);
    g_assert (uints);
    g_assert (uints->len == 3);
    g_assert (g_array_index (uints, guint, 0) == 10);
    g_assert (g_array_index (uints, guint, 1) == 42);
    g_assert (g_array_index (uints, guint, 2) == 27);
    g_array_free (uints, TRUE);
    uints = g_ptr_array_index (out_array, 1);
    g_assert (uints);
    g_assert (uints->len == 1);
    g_assert (g_array_index (uints, guint, 0) == 30);
    g_array_free (uints, TRUE);
    g_ptr_array_free (out_array, TRUE);
  }

  {
    guint val;
    char *ret_path;
    DBusGProxy *ret_proxy;

    g_print ("Calling (wrapped) objpath\n");
    if (!dbus_g_proxy_call (proxy, "Objpath", &error,
			    DBUS_TYPE_G_PROXY, proxy, G_TYPE_INVALID,
			    DBUS_TYPE_G_PROXY, &ret_proxy, G_TYPE_INVALID))
      lose_gerror ("Failed to complete Objpath call", error);
    if (strcmp ("/org/freedesktop/DBus/Tests/MyTestObject2",
		dbus_g_proxy_get_path (ret_proxy)) != 0)
      lose ("(wrapped) objpath call returned unexpected proxy %s",
	    dbus_g_proxy_get_path (ret_proxy));

    g_print ("Doing get/increment val tests\n");
    val = 1;
    if (!org_freedesktop_DBus_Tests_MyObject_get_val (ret_proxy, &val, &error))
      lose_gerror ("Failed to complete (wrapped) GetVal call", error);
    if (val != 0)
      lose ("(wrapped) GetVal returned invalid value %d", val);

    if (!org_freedesktop_DBus_Tests_MyObject_increment_val (ret_proxy, &error))
      lose_gerror ("Failed to complete (wrapped) IncrementVal call", error);

    if (!org_freedesktop_DBus_Tests_MyObject_increment_val (ret_proxy, &error))
      lose_gerror ("Failed to complete (wrapped) IncrementVal call", error);

    if (!org_freedesktop_DBus_Tests_MyObject_increment_val (ret_proxy, &error))
      lose_gerror ("Failed to complete (wrapped) IncrementVal call", error);

    if (!org_freedesktop_DBus_Tests_MyObject_get_val (ret_proxy, &val, &error))
      lose_gerror ("Failed to complete (wrapped) GetVal call", error);
    if (val != 3)
      lose ("(wrapped) GetVal returned invalid value %d", val);

    if (!org_freedesktop_DBus_Tests_MyObject_get_val (proxy, &val, &error))
      lose_gerror ("Failed to complete (wrapped) GetVal call", error);
    if (val != 0)
      lose ("(wrapped) GetVal returned invalid value %d", val);

    if (!org_freedesktop_DBus_Tests_MyObject_increment_val (proxy, &error))
      lose_gerror ("Failed to complete (wrapped) IncrementVal call", error);

    if (!org_freedesktop_DBus_Tests_MyObject_get_val (proxy, &val, &error))
      lose_gerror ("Failed to complete (wrapped) GetVal call", error);
    if (val != 1)
      lose ("(wrapped) GetVal returned invalid value %d", val);

    if (!org_freedesktop_DBus_Tests_MyObject_get_val (ret_proxy, &val, &error))
      lose_gerror ("Failed to complete (wrapped) GetVal call", error);
    if (val != 3)
      lose ("(wrapped) GetVal returned invalid value %d", val);

    g_object_unref (G_OBJECT (ret_proxy));

    g_print ("Calling objpath again\n");
    ret_proxy = NULL;

    if (!dbus_g_proxy_call (proxy, "Objpath", &error,
			    DBUS_TYPE_G_OBJECT_PATH,
			    dbus_g_proxy_get_path (proxy),
			    G_TYPE_INVALID,
			    DBUS_TYPE_G_OBJECT_PATH,
			    &ret_path,
			    G_TYPE_INVALID))
      lose_gerror ("Failed to complete Objpath call 2", error);
    if (strcmp ("/org/freedesktop/DBus/Tests/MyTestObject2", ret_path) != 0)
      lose ("Objpath call 2 returned unexpected path %s",
	    ret_path);

    ret_proxy = dbus_g_proxy_new_for_name_owner (connection,
						 "org.freedesktop.DBus.TestSuiteGLibService",
						 ret_path,
						 "org.freedesktop.DBus.Tests.FooObject",
						 &error);
    g_free (ret_path);
    
    val = 0;
    if (!org_freedesktop_DBus_Tests_FooObject_get_value (ret_proxy, &val, &error))
      lose_gerror ("Failed to complete (wrapped) GetValue call", error);
    if (val != 3)
      lose ("(wrapped) GetValue returned invalid value %d", val);
  }

  run_mainloop ();

  {
    GPtrArray *objs;
    guint i;

    g_print ("Calling GetObjs\n");

    if (!dbus_g_proxy_call (proxy, "GetObjs", &error, G_TYPE_INVALID,
			    dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH),
			    &objs,
			    G_TYPE_INVALID))
      lose_gerror ("Failed to complete GetObjs call", error);
    if (objs->len != 2)
      lose ("GetObjs call returned unexpected number of objects %d, expected 2",
	    objs->len);

    if (strcmp ("/org/freedesktop/DBus/Tests/MyTestObject",
		g_ptr_array_index (objs, 0)) != 0)
      lose ("GetObjs call returned unexpected path \"%s\" in position 0; expected /org/freedesktop/DBus/Tests/MyTestObject", (char*) g_ptr_array_index (objs, 0));

    if (strcmp ("/org/freedesktop/DBus/Tests/MyTestObject2",
		g_ptr_array_index (objs, 1)) != 0)
      lose ("GetObjs call returned unexpected path \"%s\" in position 1; expected /org/freedesktop/DBus/Tests/MyTestObject2", (char*) g_ptr_array_index (objs, 1));

    for (i = 0; i < objs->len; i++)
      g_free (g_ptr_array_index (objs, i));
    g_ptr_array_free (objs, TRUE);
  }
  
  {
    GValue *variant;
    GArray *array;
    gint i;

    g_print ("Calling ProcessVariantOfArrayOfInts123\n");

    array = g_array_sized_new (FALSE, FALSE, sizeof(gint), 3);
    i = 1;
    g_array_append_val (array, i);
    i++;
    g_array_append_val (array, i);
    i++;
    g_array_append_val (array, i);

    variant = g_new0 (GValue, 1);
    g_value_init (variant, dbus_g_type_get_collection ("GArray", G_TYPE_INT));
    g_value_set_boxed_take_ownership (variant, array);

    if (!dbus_g_proxy_call (proxy, "ProcessVariantOfArrayOfInts123", &error,
                            G_TYPE_VALUE, variant,
                            G_TYPE_INVALID,
			    G_TYPE_INVALID))
      lose_gerror ("Failed to send a vairant of array of ints 1, 2 and 3!", error);

    g_value_unset (variant);
  }
  /* Signal handling tests */
  
  g_print ("Testing signal handling\n");
  dbus_g_proxy_add_signal (proxy, "Frobnicate", G_TYPE_INT, G_TYPE_INVALID);
  
  dbus_g_proxy_connect_signal (proxy, "Frobnicate",
                               G_CALLBACK (frobnicate_signal_handler),
                               NULL, NULL);
  
  g_print ("Calling EmitFrobnicate\n");
  if (!dbus_g_proxy_call (proxy, "EmitFrobnicate", &error,
			  G_TYPE_INVALID, G_TYPE_INVALID))
    lose_gerror ("Failed to complete EmitFrobnicate call", error);

  
  dbus_g_connection_flush (connection);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);

  if (n_times_frobnicate_received != 1)
    lose ("Frobnicate signal received %d times, should have been 1", n_times_frobnicate_received);

  g_print ("Calling EmitFrobnicate again\n");
  if (!dbus_g_proxy_call (proxy, "EmitFrobnicate", &error,
			  G_TYPE_INVALID, G_TYPE_INVALID))
    lose_gerror ("Failed to complete EmitFrobnicate call", error);
  
  dbus_g_connection_flush (connection);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);

  if (n_times_frobnicate_received != 2)
    lose ("Frobnicate signal received %d times, should have been 2", n_times_frobnicate_received);

  g_object_unref (G_OBJECT (proxy));

  run_mainloop ();

  g_print ("Creating proxy for FooObject interface\n");
  proxy = dbus_g_proxy_new_for_name_owner (connection,
                                           "org.freedesktop.DBus.TestSuiteGLibService",
                                           "/org/freedesktop/DBus/Tests/MyTestObject",
                                           "org.freedesktop.DBus.Tests.FooObject",
                                           &error);
  
  if (proxy == NULL)
    lose_gerror ("Failed to create proxy for name owner", error);

  dbus_g_object_register_marshaller (my_object_marshal_VOID__STRING_INT_STRING, 
				     G_TYPE_NONE, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INVALID);

  dbus_g_object_register_marshaller (my_object_marshal_VOID__STRING_BOXED, 
				     G_TYPE_NONE, G_TYPE_STRING, G_TYPE_VALUE, G_TYPE_INVALID);

  dbus_g_proxy_add_signal (proxy, "Sig0", G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INVALID);
  dbus_g_proxy_add_signal (proxy, "Sig1", G_TYPE_STRING, G_TYPE_VALUE, G_TYPE_INVALID);
  dbus_g_proxy_add_signal (proxy, "Sig2", DBUS_TYPE_G_STRING_STRING_HASHTABLE, G_TYPE_INVALID);
  
  dbus_g_proxy_connect_signal (proxy, "Sig0",
                               G_CALLBACK (sig0_signal_handler),
                               NULL, NULL);
  dbus_g_proxy_connect_signal (proxy, "Sig1",
                               G_CALLBACK (sig1_signal_handler),
                               NULL, NULL);
  dbus_g_proxy_connect_signal (proxy, "Sig2",
                               G_CALLBACK (sig2_signal_handler),
                               NULL, NULL);

  g_print ("Calling FooObject EmitSignals\n");
  dbus_g_proxy_call_no_reply (proxy, "EmitSignals", G_TYPE_INVALID);

  dbus_g_connection_flush (connection);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);

  if (n_times_sig0_received != 1)
    lose ("Sig0 signal received %d times, should have been 1", n_times_sig0_received);
  if (n_times_sig1_received != 1)
    lose ("Sig1 signal received %d times, should have been 1", n_times_sig1_received);

  g_print ("Calling FooObject EmitSignals and EmitSignal2\n");
  dbus_g_proxy_call_no_reply (proxy, "EmitSignal2", G_TYPE_INVALID);
  dbus_g_connection_flush (connection);

  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);

  if (n_times_sig2_received != 1)
    lose ("Sig2 signal received %d times, should have been 1", n_times_sig2_received);

  g_print ("Calling FooObject EmitSignals two more times\n");
  dbus_g_proxy_call_no_reply (proxy, "EmitSignals", G_TYPE_INVALID);
  dbus_g_proxy_call_no_reply (proxy, "EmitSignals", G_TYPE_INVALID);

  dbus_g_connection_flush (connection);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);

  if (n_times_sig0_received != 3)
    lose ("Sig0 signal received %d times, should have been 3", n_times_sig0_received);
  if (n_times_sig1_received != 3)
    lose ("Sig1 signal received %d times, should have been 3", n_times_sig1_received);

  /* Terminate again */
  g_print ("Terminating service\n");
  await_terminating_service = "org.freedesktop.DBus.TestSuiteGLibService";
  dbus_g_proxy_call_no_reply (proxy, "Terminate", G_TYPE_INVALID);

  proxy_destroyed = FALSE;
  proxy_destroy_and_nameowner = TRUE;
  proxy_destroy_and_nameowner_complete = FALSE;

  g_signal_connect (G_OBJECT (proxy),
		    "destroy",
		    G_CALLBACK (proxy_destroyed_cb),
		    NULL);

  dbus_g_connection_flush (connection);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);

  if (await_terminating_service != NULL)
    lose ("Didn't see name loss for \"org.freedesktop.DBus.TestSuiteGLibService\"");
  if (!proxy_destroyed)
    lose ("Didn't get proxy_destroyed");
  g_print ("Proxy destroyed successfully\n");

  /* Don't need to unref, proxy was destroyed */

  run_mainloop ();

  /* Create a new proxy for the name; should not be associated */
  proxy = dbus_g_proxy_new_for_name (connection,
				     "org.freedesktop.DBus.TestSuiteGLibService",
				     "/org/freedesktop/DBus/Tests/MyTestObject",
				     "org.freedesktop.DBus.Tests.MyObject");
  g_assert (proxy != NULL);

  proxy_destroyed = FALSE;
  proxy_destroy_and_nameowner = FALSE;
  proxy_destroy_and_nameowner_complete = FALSE;

  g_signal_connect (G_OBJECT (proxy),
		    "destroy",
		    G_CALLBACK (proxy_destroyed_cb),
		    NULL);
  
  if (!dbus_g_proxy_call (driver, "GetNameOwner", &error,
			  G_TYPE_STRING,
			  "org.freedesktop.DBus.TestSuiteGLibService",
			  G_TYPE_INVALID,
			  G_TYPE_STRING,
			  &v_STRING_2,
			  G_TYPE_INVALID)) {
    if (error->domain == DBUS_GERROR && error->code == DBUS_GERROR_NAME_HAS_NO_OWNER)
      g_print ("Got expected error \"org.freedesktop.DBus.Error.NameHasNoOwner\"\n");
    else
      lose_gerror ("Unexpected error from GetNameOwner", error);
  } else
    lose ("GetNameOwner unexpectedly succeeded!");
  g_clear_error (&error);

  /* This will have the side-effect of activating the service, thus
   * causing a NameOwnerChanged, which should let our name proxy
   * get signals
   */
  g_print ("Calling Uppercase for name proxy\n");
  if (!dbus_g_proxy_call (proxy, "Uppercase", &error,
			  G_TYPE_STRING, "bazwhee",
			  G_TYPE_INVALID,
			  G_TYPE_STRING, &v_STRING_2,
			  G_TYPE_INVALID))
    lose_gerror ("Failed to complete Uppercase call", error);
  g_free (v_STRING_2);

  if (getenv ("DBUS_GLIB_TEST_SLEEP_AFTER_ACTIVATION1"))
    g_usleep (8 * G_USEC_PER_SEC);

  dbus_g_proxy_add_signal (proxy, "Frobnicate", G_TYPE_INT, G_TYPE_INVALID);
  
  dbus_g_proxy_connect_signal (proxy, "Frobnicate",
                               G_CALLBACK (frobnicate_signal_handler),
                               NULL, NULL);
  
  g_print ("Calling EmitFrobnicate\n");
  if (!dbus_g_proxy_call (proxy, "EmitFrobnicate", &error,
			  G_TYPE_INVALID, G_TYPE_INVALID))
    lose_gerror ("Failed to complete EmitFrobnicate call", error);

  n_times_frobnicate_received = 0;

  dbus_g_connection_flush (connection);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);

  if (n_times_frobnicate_received != 1)
    lose ("Frobnicate signal received %d times, should have been 1", n_times_frobnicate_received);

  /* Now terminate the service, then start it again (implicitly) and wait for signals */
  g_print ("Terminating service (2)\n");
  await_terminating_service = "org.freedesktop.DBus.TestSuiteGLibService";
  dbus_g_proxy_call_no_reply (proxy, "Terminate", G_TYPE_INVALID);
  dbus_g_connection_flush (connection);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);
  if (await_terminating_service != NULL)
    lose ("Didn't see name loss for \"org.freedesktop.DBus.TestSuiteGLibService\"");

  if (proxy_destroyed)
    lose ("Unexpectedly got proxy_destroyed!");

  n_times_frobnicate_received = 0;

  g_print ("Calling EmitFrobnicate (2)\n");
  if (!dbus_g_proxy_call (proxy, "EmitFrobnicate", &error,
			  G_TYPE_INVALID, G_TYPE_INVALID))
    lose_gerror ("Failed to complete EmitFrobnicate call", error);

  if (getenv ("DBUS_GLIB_TEST_SLEEP_AFTER_ACTIVATION2"))
    g_usleep (8 * G_USEC_PER_SEC);

  dbus_g_connection_flush (connection);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);

  if (n_times_frobnicate_received != 1)
    lose ("Frobnicate signal received %d times, should have been 1", n_times_frobnicate_received);

  if (proxy_destroyed)
    lose ("Unexpectedly got proxy_destroyed!");
  
  /* Create another proxy for the name; should be associated immediately */
  proxy2 = dbus_g_proxy_new_for_name (connection,
				     "org.freedesktop.DBus.TestSuiteGLibService",
				     "/org/freedesktop/DBus/Tests/MyTestObject",
				     "org.freedesktop.DBus.Tests.MyObject");
  g_assert (proxy2 != NULL);

  dbus_g_proxy_add_signal (proxy2, "Frobnicate", G_TYPE_INT, G_TYPE_INVALID);
  
  dbus_g_proxy_connect_signal (proxy2, "Frobnicate",
                               G_CALLBACK (frobnicate_signal_handler_2),
                               NULL, NULL);

  g_print ("Calling EmitFrobnicate (3)\n");
  if (!dbus_g_proxy_call (proxy, "EmitFrobnicate", &error,
			  G_TYPE_INVALID, G_TYPE_INVALID))
    lose_gerror ("Failed to complete EmitFrobnicate call", error);

  dbus_g_connection_flush (connection);
  exit_timeout = g_timeout_add (5000, timed_exit, loop);
  g_main_loop_run (loop);

  if (n_times_frobnicate_received != 2)
    lose ("Frobnicate signal received %d times for 1st proxy, should have been 2", n_times_frobnicate_received);
  if (n_times_frobnicate_received_2 != 1)
    lose ("Frobnicate signal received %d times for 2nd proxy, should have been 1", n_times_frobnicate_received_2);

  g_object_unref (G_OBJECT (proxy));
  g_object_unref (G_OBJECT (proxy2));

  run_mainloop ();

  /* Test introspection */
  proxy = dbus_g_proxy_new_for_name_owner (connection,
                                           "org.freedesktop.DBus.TestSuiteGLibService",
                                           "/org/freedesktop/DBus/Tests/MyTestObject",
                                           "org.freedesktop.DBus.Introspectable",
                                           &error);
  if (proxy == NULL)
    lose_gerror ("Failed to create proxy for name owner", error);

  g_print ("Testing introspect\n");
  if (!dbus_g_proxy_call (proxy, "Introspect", &error,
			  G_TYPE_INVALID,
			  G_TYPE_STRING, &v_STRING_2,
			  G_TYPE_INVALID))
    lose_gerror ("Failed to complete Introspect call", error);

  /* Could just do strcmp(), but that seems more fragile */
  {
    NodeInfo *node;
    GSList *elt;
    gboolean found_introspectable;
    gboolean found_properties;
    gboolean found_myobject;
    gboolean found_fooobject;

    node = description_load_from_string (v_STRING_2, strlen (v_STRING_2), &error);
    if (!node)
      lose_gerror ("Failed to parse introspection data: %s", error);

    found_introspectable = FALSE;
    found_properties = FALSE;
    found_myobject = FALSE;
    found_fooobject = FALSE;
    for (elt = node_info_get_interfaces (node); elt ; elt = elt->next)
      {
	InterfaceInfo *iface = elt->data;

	if (!found_introspectable && strcmp (interface_info_get_name (iface), "org.freedesktop.DBus.Introspectable") == 0)
	  found_introspectable = TRUE;
	else if (!found_properties && strcmp (interface_info_get_name (iface), "org.freedesktop.DBus.Properties") == 0)
	  found_properties = TRUE;
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
	else if (!found_fooobject && strcmp (interface_info_get_name (iface), "org.freedesktop.DBus.Tests.FooObject") == 0)
	  found_fooobject = TRUE;
	else
	  lose ("Unexpected or duplicate interface %s", interface_info_get_name (iface));
      }

    if (!(found_introspectable && found_myobject && found_properties))
      lose ("Missing interface"); 
  }
  g_free (v_STRING_2);
  
  g_object_unref (G_OBJECT (driver));

  g_print ("Successfully completed %s\n", argv[0]);
  
  return 0;
}
