#include <glib.h>
#include "dbus-glib.h"
#include <stdio.h>
#include <string.h>

#include "test-thread.h"

typedef struct {
  guint32 counters[N_TEST_THREADS];
} ThreadTestData;

static ThreadTestData *
thread_test_data_new (void)
{
  ThreadTestData *data;

  data = g_new0 (ThreadTestData, 1);
  
  return data;
}

static void
thread_test_data_free (ThreadTestData *data)
{
  g_free (data);
}

static DBusMessageHandler *disconnect_handler;
static DBusMessageHandler *filter_handler;
static int handler_slot;

static DBusHandlerResult
handle_test_message (DBusMessageHandler *handler,
		     DBusConnection     *connection,
		     DBusMessage        *message,
		     void               *user_data)
{
  ThreadTestData *data = user_data;
  DBusMessageIter *iter;
  gint32 threadnr;
  guint32 counter;
  char *str, *expected_str;
  GString *counter_str;
  int i;
  
  iter = dbus_message_get_args_iter (message);
  g_assert (iter != NULL);
  
  if (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_INT32)
    {
      g_print ("First arg not right type\n");
      goto out;
    }
  threadnr = dbus_message_iter_get_int32 (iter);
  if (threadnr < 0 || threadnr >= N_TEST_THREADS)
    {
      g_print ("Invalid thread nr\n");
      goto out;
    }

  if (! dbus_message_iter_next (iter))
    {
      g_print ("Couldn't get second arg\n");
      goto out;
    }

  if (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_UINT32)
    {
      g_print ("Second arg not right type\n");
      goto out;
    }
  
  counter = dbus_message_iter_get_uint32 (iter);

  if (counter != data->counters[threadnr])
    {
      g_print ("Thread %d, counter %d, expected %d\n", threadnr, counter, data->counters[threadnr]);
      goto out;
    }
  data->counters[threadnr]++;
  
  if (! dbus_message_iter_next (iter))
    {
      g_print ("Couldn't get third arg\n");
      goto out;
    }

  if (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_STRING)
    {
      g_print ("Third arg not right type\n");
      goto out;
    }

  str = dbus_message_iter_get_string (iter);

  if (str == NULL)
    {
      g_print ("No third arg\n");
      goto out;
    }

  expected_str = g_strdup_printf ("Thread %d-%d\n", threadnr, counter);
  if (strcmp (expected_str, str) != 0)
    {
      g_print ("Wrong string '%s', expected '%s'\n", str, expected_str);
      goto out;
    }
  g_free (str);
  g_free (expected_str);

  if (dbus_message_iter_next (iter))
    {
      g_print ("Extra args on end of message\n");
      goto out;
    }
  
  dbus_connection_flush (connection);

  counter_str = g_string_new ("");
  for (i = 0; i < N_TEST_THREADS; i++)
    {
      g_string_append_printf (counter_str, "%d ", data->counters[i]);
    }
  g_print ("%s\r", counter_str->str);
  g_string_free (counter_str, TRUE);
  
 out:
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static DBusHandlerResult
handle_filter (DBusMessageHandler *handler,
	       DBusConnection     *connection,
	       DBusMessage        *message,
	       void               *user_data)
{
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static DBusHandlerResult
handle_disconnect (DBusMessageHandler *handler,
                   DBusConnection     *connection,
                   DBusMessage        *message,
                   void               *user_data)
{
  g_print ("connection disconnected\n");
  dbus_connection_unref (connection);

  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}


static void
new_connection_callback (DBusServer     *server,
                         DBusConnection *new_connection,
                         void           *user_data)
{
  const char *test_messages[] = { "org.freedesktop.ThreadTest" };
  const char *disconnect_messages[] = { "org.freedesktop.Local.Disconnect" };
  DBusMessageHandler *test_message_handler;
  ThreadTestData * data;

  g_print ("new_connection_callback\n");
  
  dbus_connection_ref (new_connection);
  dbus_connection_setup_with_g_main (new_connection);

  data = thread_test_data_new ();
  
  test_message_handler =
    dbus_message_handler_new (handle_test_message,
			      data, (DBusFreeFunction)thread_test_data_free);
  
  if (!dbus_connection_register_handler (new_connection,
                                         test_message_handler,
                                         test_messages, 1))
    goto nomem;

  if (!dbus_connection_set_data (new_connection,
				 handler_slot,
				 test_message_handler,
				 (DBusFreeFunction)dbus_message_handler_unref))
    goto nomem;
  
  if (!dbus_connection_register_handler (new_connection,
                                         disconnect_handler,
                                         disconnect_messages, 1))
    goto nomem;
  
  if (!dbus_connection_add_filter (new_connection,
				   filter_handler))
    goto nomem;

  return;
  
 nomem:
  g_error ("no memory to setup new connection");
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  DBusServer *server;
  DBusError error;

  g_thread_init (NULL);
  dbus_gthread_init ();
  
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

  handler_slot = dbus_connection_allocate_data_slot ();
  
  filter_handler =
    dbus_message_handler_new (handle_filter, NULL, NULL);
  if (filter_handler == NULL)
    g_error ("no memory for handler");
  
  disconnect_handler =
    dbus_message_handler_new (handle_disconnect, NULL, NULL);
  if (disconnect_handler == NULL)
    g_error ("no memory for handler");
  
  dbus_server_set_new_connection_function (server,
                                           new_connection_callback,
                                           NULL, NULL);

  dbus_server_setup_with_g_main (server);
  
  loop = g_main_loop_new (NULL, FALSE);
  g_main_run (loop);  

  return 0;
}
