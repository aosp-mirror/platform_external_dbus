#include <dbus/dbus.h>
#include <stdio.h>
#include <sys/time.h>

#define DBUS_COMPILATION /* cheat and use DBusList */
#include <dbus/dbus-list.h>
#include <bus/connection.h>

#undef DBUS_COMPILATION

typedef struct
{
  long time;
  DBusTimeout *timeout;

} LoopTimeout;

static DBusList *timeouts;

static long
get_time (void)
{
  struct timeval r;
  long time;

  /* Can't use dbus-sysdeps here since that isn't
   * available outside of libdbus.
   */
  gettimeofday (&r, NULL);

  time = r.tv_sec * 1000;
  time += r.tv_usec / 1000;

  return time;
}

static void
add_timeout (DBusTimeout *timeout,
	     void        *data)
{
  LoopTimeout *lt;

  lt = dbus_new (LoopTimeout, 1);
  lt->time = get_time () + dbus_timeout_get_interval (timeout);
  lt->timeout = timeout;

  _dbus_list_append (&timeouts, lt);
  printf ("add timeout!\n");
}

static void
remove_timeout (DBusTimeout *timeout,
		void        *data)
{
  printf ("remove timeout!\n");

}

static DBusHandlerResult
message_handler (DBusMessageHandler *handler,
		 DBusConnection     *connection,
		 DBusMessage        *message,
		 void               *user_data)
{
  printf ("client got a message!: %s\n",
	  dbus_message_get_name (message));
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static void
new_connection_callback (DBusServer     *server,
                         DBusConnection *new_connection,
                         void           *data)
{
  if (!bus_connection_setup (new_connection))
    return;

  dbus_connection_set_timeout_functions (new_connection,
					 add_timeout, remove_timeout,
					 NULL, NULL);

  dbus_connection_ref (new_connection);
}



static dbus_bool_t running_loop;


static void
loop_quit (void)
{
  running_loop = FALSE;
}

static void
loop_run (void)
{
  long start_time = get_time ();
  running_loop = TRUE;

  /* Horribly inefficient main loop */
  while (running_loop)
    {
      DBusList *link, *list_copy;
      long time;

      time = get_time ();

      _dbus_list_copy (&timeouts, &list_copy);

      link = _dbus_list_get_first_link (&list_copy);
      while (link != NULL)
	{
	  LoopTimeout *lt = link->data;
	  if (lt->time <= time)
	    {
	      dbus_timeout_handle (lt->timeout);
	      _dbus_list_remove (&timeouts, lt);
	    }
	  link = _dbus_list_get_next_link (&list_copy, link);
	}

      if (get_time () - start_time > 1000)
	loop_quit ();
    }
}





int
main (int    argc,
      char **argv)
{
  DBusServer *server;
  DBusConnection *connection;
  DBusResultCode result;
  DBusMessage *message;
  DBusMessageHandler *handler;

  server = dbus_server_listen ("debug:name=test-server", &result);
  dbus_server_set_new_connection_function (server,
                                           new_connection_callback,
                                           NULL, NULL);
  dbus_server_set_timeout_functions (server,
				     add_timeout, remove_timeout,
				     NULL, NULL);
  if (server == NULL)
    {
      fprintf (stderr, "Failed to start server: %s\n",
	       dbus_result_to_string (result));
      return 1;
    }

  connection = dbus_connection_open ("debug:name=test-server", &result);
  dbus_connection_set_timeout_functions (connection,
					 add_timeout, remove_timeout,
					 NULL, NULL);
  if (connection == NULL)
    {
      fprintf (stderr, "Failed to connect to server: %s\n",
	       dbus_result_to_string (result));
      return 1;
    }

  message = dbus_message_new (DBUS_SERVICE_DBUS,
			      DBUS_MESSAGE_HELLO);
  dbus_message_append_args (message,
			    DBUS_TYPE_STRING, "test",
			    0);

  handler = dbus_message_handler_new (message_handler, NULL, NULL);
  dbus_connection_add_filter (connection, handler);

  dbus_connection_send_message (connection, message, NULL, NULL);
  dbus_message_unref (message);

  bus_connection_init ();

  loop_run ();

  dbus_connection_disconnect (connection);

  return 0;
}
