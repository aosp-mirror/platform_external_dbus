/* -*- mode: C; c-file-style: "gnu" -*- */
/* test-profile.c Program that does basic message-response for timing
 *
 * Copyright (C) 2003  Red Hat Inc.
 *
 * Licensed under the Academic Free License version 2.1
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
#include <dbus/dbus-glib-lowlevel.h>
#include <stdlib.h>

#define N_CLIENT_THREADS 1
#define N_ITERATIONS 4000
#define PAYLOAD_SIZE 30
#define ECHO_PATH "/org/freedesktop/EchoTest"
#define ECHO_INTERFACE "org.freedesktop.EchoTest"
#define ECHO_METHOD "EchoProfile"

static const char *address;
static unsigned char *payload;

typedef struct
{
  int iterations;
  GMainLoop *loop;
} ClientData;

typedef struct
{
  int handled;
  GMainLoop *loop;
  int n_clients;
} ServerData;

static void
send_echo_method_call (DBusConnection *connection)
{
  DBusMessage *message;

  message = dbus_message_new_method_call (NULL, ECHO_PATH,
                                          ECHO_INTERFACE, ECHO_METHOD);
  dbus_message_append_args (message,
                            DBUS_TYPE_STRING, "Hello World!",
                            DBUS_TYPE_INT32, 123456,
#if 0
                            DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
                            payload, PAYLOAD_SIZE,
#endif
                            DBUS_TYPE_INVALID);
  
  dbus_connection_send (connection, message, NULL);
  dbus_message_unref (message);
  dbus_connection_flush (connection);
}

static void
send_echo_method_return (DBusConnection *connection,
                         DBusMessage    *call_message)
{
  DBusMessage *message;

  message = dbus_message_new_method_return (call_message);
  
  dbus_connection_send (connection, message, NULL);
  dbus_message_unref (message);
  dbus_connection_flush (connection);
}

static DBusHandlerResult
client_filter (DBusConnection     *connection,
	       DBusMessage        *message,
	       void               *user_data)
{
  ClientData *cd = user_data;
  
  if (dbus_message_is_signal (message,
                              DBUS_INTERFACE_ORG_FREEDESKTOP_LOCAL,
                              "Disconnected"))
    {
      g_printerr ("Client thread disconnected\n");
      exit (1);
    }
  else if (dbus_message_get_type (message) == DBUS_MESSAGE_TYPE_METHOD_RETURN)
    {
      cd->iterations += 1;
      if (cd->iterations >= N_ITERATIONS)
        {
          g_print ("Completed %d iterations\n", N_ITERATIONS);
          g_main_loop_quit (cd->loop);
        }
      send_echo_method_call (connection);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void*
thread_func (void *data)
{
  DBusError error;
  GMainContext *context;
  DBusConnection *connection;
  ClientData cd;
  
  g_printerr ("Starting client thread\n");
  
  dbus_error_init (&error);
  connection = dbus_connection_open (address, &error);
  if (connection == NULL)
    {
      g_printerr ("could not open connection: %s\n", error.message);
      dbus_error_free (&error);
      exit (1);
    }

  context = g_main_context_new ();

  cd.iterations = 1;
  cd.loop = g_main_loop_new (context, FALSE);
  
  if (!dbus_connection_add_filter (connection,
				   client_filter, &cd, NULL))
    g_error ("no memory");
  
  
  dbus_connection_setup_with_g_main (connection, context);

  g_printerr ("Client thread sending message to prime pingpong\n");
  send_echo_method_call (connection);
  g_printerr ("Client thread sent message\n");

  g_printerr ("Client thread entering main loop\n");
  g_main_loop_run (cd.loop);
  g_printerr ("Client thread exiting main loop\n");

  dbus_connection_disconnect (connection);
  
  g_main_loop_unref (cd.loop);
  g_main_context_unref (context);
  
  return NULL;
}

static DBusHandlerResult
server_filter (DBusConnection     *connection,
	       DBusMessage        *message,
	       void               *user_data)
{
  ServerData *sd = user_data;
  
  if (dbus_message_is_signal (message,
                              DBUS_INTERFACE_ORG_FREEDESKTOP_LOCAL,
                              "Disconnected"))
    {
      g_printerr ("Client disconnected from server\n");
      sd->n_clients -= 1;
      if (sd->n_clients == 0)
        g_main_loop_quit (sd->loop);
    }
  else if (dbus_message_is_method_call (message,
                                        ECHO_INTERFACE,
                                        ECHO_METHOD))
    {
      sd->handled += 1;
      send_echo_method_return (connection, message);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
new_connection_callback (DBusServer     *server,
                         DBusConnection *new_connection,
                         void           *user_data)
{
  ServerData *sd = user_data;
  
  dbus_connection_ref (new_connection);
  dbus_connection_setup_with_g_main (new_connection, NULL);  
  
  if (!dbus_connection_add_filter (new_connection,
                                   server_filter, sd, NULL))
    g_error ("no memory");

  sd->n_clients += 1;

  /* FIXME we leak the handler */  
}

int
main (int argc, char *argv[])
{
  DBusError error;
  DBusServer *server;
  GTimer *timer;
  int i;
  double secs;
  ServerData sd;

  g_thread_init (NULL);
  dbus_g_thread_init ();

  dbus_error_init (&error);
  server = dbus_server_listen ("unix:tmpdir="DBUS_TEST_SOCKET_DIR,
                               &error);
  if (server == NULL)
    {
      g_printerr ("Could not start server: %s\n",
                  error.message);
      return 1;
    }

#ifndef DBUS_DISABLE_ASSERT
  g_printerr ("You should probably turn off assertions before you profile\n");
#endif
  
  address = dbus_server_get_address (server);
  payload = g_malloc (PAYLOAD_SIZE);
  
  dbus_server_set_new_connection_function (server,
                                           new_connection_callback,
                                           &sd, NULL);

  sd.handled = 0;
  sd.n_clients = 0;
  sd.loop = g_main_loop_new (NULL, FALSE);

  dbus_server_setup_with_g_main (server, NULL);
  
  for (i = 0; i < N_CLIENT_THREADS; i++)
    {
      g_thread_create (thread_func, NULL, FALSE, NULL);
    }

  timer = g_timer_new ();
  
  g_printerr ("Server thread entering main loop\n");
  g_main_loop_run (sd.loop);
  g_printerr ("Server thread exiting main loop\n");

  secs = g_timer_elapsed (timer, NULL);
  g_timer_destroy (timer);

  g_printerr ("%g seconds, %d round trips, %g seconds per pingpong\n",
              secs, sd.handled, secs/sd.handled);
#ifndef DBUS_DISABLE_ASSERT
  g_printerr ("You should probably --disable-asserts before you profile as they have noticeable overhead\n");
#endif

  g_printerr ("The following g_warning is because we try to call g_source_remove_poll() after g_source_destroy() in dbus-gmain.c, I think we need to add a source free func that clears out the watch/timeout funcs\n");
  dbus_server_unref (server);
  
  g_main_loop_unref (sd.loop);
  
  return 0;
}

  
