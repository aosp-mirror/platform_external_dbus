#include "dbus-glib.h"
#include <stdio.h>

GMainLoop *loop;

static void
message_handler (DBusConnection *connection,
		 DBusMessage *message, gpointer user_data)
{
  static int count = 0;
  DBusMessage *reply;

  reply = dbus_message_new ();
  dbus_connection_send_message (connection,
				reply,
				NULL);
  dbus_message_unref (reply);
  count += 1;
  
  if (count > 100)
    {
      printf ("Saw %d messages, exiting\n", count);
      g_main_loop_quit (loop);
    }
}

int
main (int argc, char **argv)
{
  GSource *source;
  
  DBusConnection *connection;
  DBusResultCode result;
  DBusMessage *message;
  
  loop = g_main_loop_new (NULL, FALSE);

  connection = dbus_connection_open (argv[1], &result);
  if (connection == NULL)
    {
      fprintf (stderr, "Failed to open connection to %s: %s\n", argv[1],
	       dbus_result_to_string (result));
      return 1;
    }

  source = dbus_connection_gsource_new (connection);
  g_source_attach (source, NULL);
  g_source_set_callback (source, (GSourceFunc)message_handler, NULL, NULL);
  
  message = dbus_message_new ();
  dbus_connection_send_message (connection,
                                message,
                                NULL);
  dbus_message_unref (message);
  
  
  g_main_loop_run (loop);
  
  return 0;
}
