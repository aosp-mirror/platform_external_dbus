/* -*- mode: C; c-file-style: "gnu" -*- */
/* test-profile.c Program that does basic message-response for timing
 *
 * Copyright (C) 2003  Red Hat Inc.
 *
 * Licensed under the Academic Free License version 1.2
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <config.h>
#include <glib.h>
#include "dbus-glib.h"
#include <stdlib.h>

#define N_CLIENT_THREADS 1
#define N_ITERATIONS 2000
#define ECHO_MESSAGE "org.freedesktop.DBus.Test.EchoProfile"
static const char *address;

static void
send_echo_message (DBusConnection *connection)
{
  DBusMessage *message;

  message = dbus_message_new (ECHO_MESSAGE, NULL);
  dbus_message_append_args (message,
                            DBUS_TYPE_STRING, "Hello World!",
                            DBUS_TYPE_INT32, 123456,
                            DBUS_TYPE_INVALID);
  dbus_connection_send (connection, message, NULL);
  dbus_message_unref (message);
  dbus_connection_flush (connection);
}

static DBusHandlerResult
client_filter (DBusMessageHandler *handler,
	       DBusConnection     *connection,
	       DBusMessage        *message,
	       void               *user_data)
{
  int *iterations = user_data;
  
  if (dbus_message_has_name (message, DBUS_MESSAGE_LOCAL_DISCONNECT))
    {
      g_printerr ("Client thread disconnected\n");
      exit (1);
    }
  else if (dbus_message_has_name (message,
                                  ECHO_MESSAGE))
    {
      *iterations += 1;
      send_echo_message (connection);
      if (*iterations > N_ITERATIONS)
        {
          g_print ("Completed %d iterations\n", N_ITERATIONS);
          exit (0);
        }
    }
  
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static void*
thread_func (void *data)
{
  DBusError error;
  GMainContext *context;
  GMainLoop *loop;
  DBusMessageHandler *handler;
  DBusConnection *connection;
  int iterations;
  
  g_printerr ("Starting client thread\n");
  
  dbus_error_init (&error);
  connection = dbus_connection_open (address, &error);
  if (connection == NULL)
    {
      g_printerr ("could not open connection: %s\n", error.message);
      dbus_error_free (&error);
      exit (1);
    }

  iterations = 0;
  
  handler = dbus_message_handler_new (client_filter,
                                      &iterations, NULL);
  
  if (!dbus_connection_add_filter (connection,
				   handler))
    g_error ("no memory");

  /* FIXME we leak the handler */
  
  context = g_main_context_new ();
  loop = g_main_loop_new (context, FALSE);
  
  dbus_connection_setup_with_g_main (connection, context);

  g_printerr ("Client thread sending message to prime pingpong\n");
  send_echo_message (connection);
  g_printerr ("Client thread sent message\n");

  g_printerr ("Client thread entering main loop\n");
  g_main_loop_run (loop);
  g_printerr ("Client thread exiting main loop\n");
  
  g_main_loop_unref (loop);
  g_main_context_unref (context);

  return NULL;
}

static DBusHandlerResult
server_filter (DBusMessageHandler *handler,
	       DBusConnection     *connection,
	       DBusMessage        *message,
	       void               *user_data)
{
  if (dbus_message_has_name (message, DBUS_MESSAGE_LOCAL_DISCONNECT))
    {
      g_printerr ("Server thread disconnected\n");
      exit (1);
    }
  else if (dbus_message_has_name (message,
                                  ECHO_MESSAGE))
    {
      send_echo_message (connection);
    }
  
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

static void
new_connection_callback (DBusServer     *server,
                         DBusConnection *new_connection,
                         void           *user_data)
{
  DBusMessageHandler *handler;
  
  dbus_connection_ref (new_connection);
  dbus_connection_setup_with_g_main (new_connection, NULL);  

  handler = dbus_message_handler_new (server_filter,
                                      NULL, NULL);
  
  if (!dbus_connection_add_filter (new_connection,
				   handler))
    g_error ("no memory");
  

  /* FIXME we leak the handler */  
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  DBusError error;
  DBusServer *server;
  int i;
  
  g_thread_init (NULL);
  dbus_gthread_init ();

  dbus_error_init (&error);
  server = dbus_server_listen ("unix:tmpdir="DBUS_TEST_SOCKET_DIR,
                               &error);
  if (server == NULL)
    {
      g_printerr ("Could not start server: %s\n",
                  error.message);
      return 1;
    }

  address = dbus_server_get_address (server);
  
  dbus_server_set_new_connection_function (server,
                                           new_connection_callback,
                                           NULL, NULL);
  
  loop = g_main_loop_new (NULL, FALSE);

  dbus_server_setup_with_g_main (server, NULL);
  
  for (i = 0; i < N_CLIENT_THREADS; i++)
    {
      g_thread_create (thread_func, NULL, FALSE, NULL);
    }

  g_printerr ("Server thread entering main loop\n");
  g_main_loop_run (loop);
  g_printerr ("Server thread exiting main loop\n");

  dbus_server_unref (server);
  
  g_main_loop_unref (loop);
  
  return 0;
}
  
