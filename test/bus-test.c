#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>

#define DBUS_COMPILATION /* cheat and use DBusList */
#include <dbus/dbus-list.h>
#include <bus/connection.h>

#undef DBUS_COMPILATION

#include "debug-thread.h"
#include "bus-test-loop.h"


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

  bus_test_loop_hookup_with_connection (new_connection);

  dbus_connection_ref (new_connection);
}


static void
die (const char *failure)
{
  fprintf (stderr, "Unit test failed: %s\n", failure);
  exit (1);
}

/* Here are the tests */
static dbus_bool_t test_hello_succeeding = TRUE;
static char *client1_name, *client2_name;
static int client1_stage = 0, client2_stage = 0;

#define TEST_HELLO_HANDLE_FAIL(x) do { if (!(x)) { printf ("failed at line %d\n", __LINE__); test_hello_succeeding = FALSE; goto out;  } } while (0)

				   
static DBusHandlerResult
test_hello_client1_handler (DBusMessageHandler *handler,
			    DBusConnection     *connection,
			    DBusMessage        *message,
			    void               *user_data)
{
  char *tmp = NULL;

  if (!test_hello_succeeding)
    goto out;
  
  if (dbus_message_name_is (message, DBUS_MESSAGE_HELLO))
    {
      TEST_HELLO_HANDLE_FAIL (client1_stage == 0);

      TEST_HELLO_HANDLE_FAIL ((dbus_message_get_args (message,
						      DBUS_TYPE_STRING, &client1_name,
						      0) == DBUS_RESULT_SUCCESS));

      client1_stage += 1;
    }
  else if (dbus_message_name_is (message, DBUS_MESSAGE_SERVICE_CREATED))
    {
      TEST_HELLO_HANDLE_FAIL (client1_stage == 1 || client1_stage == 3);

      TEST_HELLO_HANDLE_FAIL ((dbus_message_get_args (message,
						      DBUS_TYPE_STRING, &tmp,
						      0) == DBUS_RESULT_SUCCESS));
      if (client1_stage == 1)
	TEST_HELLO_HANDLE_FAIL (strcmp (client1_name, tmp) == 0);
      else
	TEST_HELLO_HANDLE_FAIL (strcmp (client2_name, tmp) == 0);

      client1_stage += 1;

      if (client1_stage == 4)
	bus_test_loop_quit ();
    }
  else if (dbus_message_name_is (message, DBUS_MESSAGE_SERVICE_ACQUIRED))
    {
      TEST_HELLO_HANDLE_FAIL (client1_stage == 2);

      TEST_HELLO_HANDLE_FAIL ((dbus_message_get_args (message,
						      DBUS_TYPE_STRING, &tmp,
						      0) == DBUS_RESULT_SUCCESS));
      TEST_HELLO_HANDLE_FAIL (strcmp (client1_name, tmp) == 0);

      client1_stage += 1;
    }
  else
    {
      printf ("client1 received unexpected message %s in stage %d\n",
	      dbus_message_get_name (message), client1_stage);
      
      test_hello_succeeding = FALSE;
      goto out;
    }

 out:
  if (tmp)
    dbus_free (tmp);

  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static DBusHandlerResult
test_hello_client2_handler (DBusMessageHandler *handler,
			    DBusConnection     *connection,
			    DBusMessage        *message,
			    void               *user_data)
{
  char *tmp = NULL;
  
  if (!test_hello_succeeding)
    goto out;

  if (dbus_message_name_is (message, DBUS_MESSAGE_HELLO))
    {
      TEST_HELLO_HANDLE_FAIL (client2_stage == 0);

      TEST_HELLO_HANDLE_FAIL ((dbus_message_get_args (message,
						      DBUS_TYPE_STRING, &client2_name,
						      0) == DBUS_RESULT_SUCCESS));

      client2_stage += 1;
    }
  else if (dbus_message_name_is (message, DBUS_MESSAGE_SERVICE_CREATED))
    {
      TEST_HELLO_HANDLE_FAIL (client2_stage == 1);

      TEST_HELLO_HANDLE_FAIL ((dbus_message_get_args (message,
						      DBUS_TYPE_STRING, &tmp,
						      0) == DBUS_RESULT_SUCCESS));
      TEST_HELLO_HANDLE_FAIL (strcmp (client2_name, tmp) == 0);
      
      client2_stage += 1;
    }
  else if (dbus_message_name_is (message, DBUS_MESSAGE_SERVICE_ACQUIRED))
    {
      TEST_HELLO_HANDLE_FAIL (client2_stage == 2);

      TEST_HELLO_HANDLE_FAIL ((dbus_message_get_args (message,
						      DBUS_TYPE_STRING, &tmp,
						      0) == DBUS_RESULT_SUCCESS));
      TEST_HELLO_HANDLE_FAIL (strcmp (client2_name, tmp) == 0);

      client2_stage += 1;
    }
  else
    {
      test_hello_succeeding = FALSE;
      goto out;
    }
    
 out:
  if (tmp)
    dbus_free (tmp);

  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static dbus_bool_t
test_hello_replies (void)
{
  DBusConnection *connection;
  DBusResultCode result;
  DBusMessage *message;
  DBusMessageHandler *handler;
  
  /* First start client 1 */
  connection = dbus_connection_open ("debug:name=test-server", &result);
  bus_test_loop_hookup_with_connection (connection);
  message = dbus_message_new (DBUS_SERVICE_DBUS,
			      DBUS_MESSAGE_HELLO);
  handler = dbus_message_handler_new (test_hello_client1_handler, NULL, NULL);
  dbus_connection_add_filter (connection, handler);
  dbus_connection_send_message (connection, message, NULL, NULL);
  dbus_message_unref (message);

  /* Then start client 2 */
  connection = dbus_connection_open ("debug:name=test-server", &result);
  bus_test_loop_hookup_with_connection (connection);
  message = dbus_message_new (DBUS_SERVICE_DBUS,
			      DBUS_MESSAGE_HELLO);
  handler = dbus_message_handler_new (test_hello_client2_handler, NULL, NULL);
  dbus_connection_add_filter (connection, handler);
  dbus_connection_send_message (connection, message, NULL, NULL);
  dbus_message_unref (message);

  bus_test_loop_run ();
  
  return test_hello_succeeding;
}

int
main (int    argc,
      char **argv)
{
  DBusServer *server;
  DBusResultCode result;
    
  debug_threads_init ();

  bus_connection_init ();
  
  server = dbus_server_listen ("debug:name=test-server", &result);
  dbus_server_set_new_connection_function (server,
                                           new_connection_callback,
                                           NULL, NULL);
  bus_test_loop_hookup_with_server (server);
  if (server == NULL)
    {
      fprintf (stderr, "Failed to start server: %s\n",
	       dbus_result_to_string (result));
      return 1;
    }

  if (!test_hello_replies ())
    die ("hello with replies");

  printf ("all tests succeeded\n");
  
  return 0;
}
