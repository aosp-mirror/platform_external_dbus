/* -*- mode: C; c-file-style: "gnu" -*- */
/* test-profile.c Program that does basic message-response for timing
 *
 * Copyright (C) 2003, 2004  Red Hat Inc.
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
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#ifndef HAVE_SOCKLEN_T
#define socklen_t int
#endif

#define _DBUS_ZERO(object) (memset (&(object), '\0', sizeof ((object))))
#define _DBUS_MAX_SUN_PATH_LENGTH 99

/* Note that if you set threads > 1 you get a bogus profile since the
 * clients start blocking on the server, so the client write() will go
 * higher in the profile the larger the number of threads.
 */
#define N_CLIENT_THREADS 1
#define N_ITERATIONS 1500000
#define N_PROGRESS_UPDATES 20
#define PAYLOAD_SIZE 30
#define ECHO_PATH "/org/freedesktop/EchoTest"
#define ECHO_INTERFACE "org.freedesktop.EchoTest"
#define ECHO_METHOD "EchoProfile"

static const char *with_bus_address;
static const char *plain_sockets_address;
static unsigned char *payload;
static int echo_call_size;
static int echo_return_size;

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

typedef struct
{
  const char *name;
  void* (* init_server)        (ServerData *sd);
  void  (* stop_server)        (ServerData *sd,
                                void       *server);
  void* (* client_thread_func) (void *data);

  /* this is so different runs show up in the profiler with
   * different backtrace
   */
  void  (* main_loop_run_func) (GMainLoop *loop);
} ProfileRunVTable;

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
          g_printerr ("\nCompleted %d iterations\n", N_ITERATIONS);
          g_main_loop_quit (cd->loop);
        }
      else if (cd->iterations % (N_ITERATIONS/N_PROGRESS_UPDATES) == 0)
        {
          g_printerr ("%d%% ", (int) (cd->iterations/(double)N_ITERATIONS * 100.0));
        }
      
      send_echo_method_call (connection);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void*
with_bus_thread_func (void *data)
{
  DBusError error;
  GMainContext *context;
  DBusConnection *connection;
  ClientData cd;
  
  g_printerr ("Starting client thread %p\n", g_thread_self());  
  
  dbus_error_init (&error);
  connection = dbus_connection_open (with_bus_address, &error);
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
  g_printerr ("Client thread %p exiting main loop\n",
              g_thread_self());

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

static void*
with_bus_init_server (ServerData *sd)
{
  DBusServer *server;
  DBusError error;

#ifndef DBUS_DISABLE_ASSERT
  g_printerr ("You should probably --disable-asserts before you profile as they have noticeable overhead\n");
#endif

  dbus_error_init (&error);
  server = dbus_server_listen ("unix:tmpdir="DBUS_TEST_SOCKET_DIR,
                               &error);
  if (server == NULL)
    {
      g_printerr ("Could not start server: %s\n",
                  error.message);
      exit (1);
    }

  with_bus_address = dbus_server_get_address (server);
  
  dbus_server_set_new_connection_function (server,
                                           new_connection_callback,
                                           sd, NULL);

  dbus_server_setup_with_g_main (server, NULL);
  
  return server;
}

static void
with_bus_stop_server (ServerData *sd,
                      void       *server)
{
  g_printerr ("The following g_warning is because we try to call g_source_remove_poll() after g_source_destroy() in dbus-gmain.c, I think we need to add a source free func that clears out the watch/timeout funcs\n");
  
  dbus_server_unref (server);
}

static void
with_bus_main_loop_run (GMainLoop *loop)
{
  g_main_loop_run (loop);
}

static const ProfileRunVTable with_bus_vtable = {
  "with bus",
  with_bus_init_server,
  with_bus_stop_server,
  with_bus_thread_func,
  with_bus_main_loop_run
};

typedef struct
{
  int listen_fd;
  ServerData *sd;
  unsigned int source_id;
} PlainSocketServer;

static void
read_and_drop_on_floor (int fd,
                        int count)
{
  int bytes_read;
  int val;
  char buf[512];

  bytes_read = 0;

  while (bytes_read < count)
    {
    again:
      
      val = read (fd, buf, MIN (count - bytes_read, (int) sizeof(buf)));
      
      if (val < 0)
        {
          if (errno == EINTR)
            goto again;
          else
            {
              g_printerr ("read() failed thread %p: %s\n",
                          g_thread_self(), strerror (errno));
              exit (1);
            }
        }
      else
        {
          bytes_read += val;
        }
    }

#if 0
  g_printerr ("%p read %d bytes from fd %d\n",
           g_thread_self(), bytes_read, fd);
#endif
}

static void
write_junk (int fd,
            int count)
{
  int bytes_written;
  int val;
  char buf[512];

  bytes_written = 0;
  
  while (bytes_written < count)
    {
    again:
      
      val = write (fd, buf, MIN (count - bytes_written, (int) sizeof(buf)));
      
      if (val < 0)
        {
          if (errno == EINTR)
            goto again;
          else
            {
              g_printerr ("write() failed thread %p: %s\n",
                          g_thread_self(), strerror (errno));
              exit (1);
            }
        }
      else
        {
          bytes_written += val;
        }
    }

#if 0
  g_printerr ("%p wrote %d bytes to fd %d\n",
           g_thread_self(), bytes_written, fd);
#endif
}

static gboolean
plain_sockets_talk_to_client_watch (GIOChannel   *source,
                                    GIOCondition  condition,
                                    gpointer      data)
{
  PlainSocketServer *server = data;
  int client_fd = g_io_channel_unix_get_fd (source);
  
  if (condition & G_IO_HUP)
    {
      g_printerr ("Client disconnected from server\n");
      server->sd->n_clients -= 1;
      if (server->sd->n_clients == 0)
        g_main_loop_quit (server->sd->loop);

      return FALSE; /* remove watch */
    }
  else if (condition & G_IO_IN)
    {
      server->sd->handled += 1;

      read_and_drop_on_floor (client_fd, echo_call_size);
      write_junk (client_fd, echo_return_size);
    }
  else
    {
      g_printerr ("Unexpected IO condition in server thread\n");
      exit (1);
    }

  return TRUE;
}

static gboolean
plain_sockets_new_client_watch (GIOChannel   *source,
                                GIOCondition  condition,
                                gpointer      data)
{
  int client_fd;
  struct sockaddr addr;
  socklen_t addrlen;
  GIOChannel *channel;
  PlainSocketServer *server = data;

  if (!(condition & G_IO_IN))
    {
      g_printerr ("Unexpected IO condition on server socket\n");
      exit (1);
    }
  
  addrlen = sizeof (addr);
  
 retry:
  client_fd = accept (server->listen_fd, &addr, &addrlen);
  
  if (client_fd < 0)
    {
      if (errno == EINTR)
        goto retry;
      else
        {
          g_printerr ("Failed to accept() connection from client: %s\n",
                      strerror (errno));
          exit (1);
        }
    }
  
  channel = g_io_channel_unix_new (client_fd);
  g_io_add_watch (channel,
                  G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL | G_IO_PRI,
                  plain_sockets_talk_to_client_watch,
                  server);
  g_io_channel_unref (channel);

  server->sd->n_clients += 1;
  
  return TRUE;
}

static void*
plain_sockets_init_server (ServerData *sd)
{
  PlainSocketServer *server;
  struct sockaddr_un addr;
  static char path[] = "/tmp/dbus-test-profile-XXXXXX";
  char *p;
  GIOChannel *channel;

  server = g_new0 (PlainSocketServer, 1);
  server->sd = sd;
  
  p = path;
  while (*p)
    {
      if (*p == 'X')
        *p = 'a' + (int) (26.0*rand()/(RAND_MAX+1.0));
      ++p;
    }

  g_printerr ("Socket is %s\n", path);
  
  server->listen_fd = socket (PF_UNIX, SOCK_STREAM, 0);
  
  if (server->listen_fd < 0)
    {
      g_printerr ("Failed to create socket: %s",
                  strerror (errno));
      exit (1);
    }

  _DBUS_ZERO (addr);
  addr.sun_family = AF_UNIX;
  
#ifdef HAVE_ABSTRACT_SOCKETS
  /* remember that abstract names aren't nul-terminated so we rely
   * on sun_path being filled in with zeroes above.
   */
  addr.sun_path[0] = '\0'; /* this is what says "use abstract" */
  strncpy (&addr.sun_path[1], path, _DBUS_MAX_SUN_PATH_LENGTH - 2);
  /* _dbus_verbose_bytes (addr.sun_path, sizeof (addr.sun_path)); */
#else /* HAVE_ABSTRACT_SOCKETS */
  {
    struct stat sb;
    
    if (stat (path, &sb) == 0 &&
        S_ISSOCK (sb.st_mode))
      unlink (path);
  }

  strncpy (addr.sun_path, path, _DBUS_MAX_SUN_PATH_LENGTH - 1);
#endif /* ! HAVE_ABSTRACT_SOCKETS */
  
  if (bind (server->listen_fd, (struct sockaddr*) &addr, sizeof (addr)) < 0)
    {
      g_printerr ("Failed to bind socket \"%s\": %s",
                  path, strerror (errno));
      exit (1);
    }

  if (listen (server->listen_fd, 30 /* backlog */) < 0)
    {
      g_printerr ("Failed to listen on socket \"%s\": %s",
                  path, strerror (errno));
      exit (1);
    }

  plain_sockets_address = path;

  channel = g_io_channel_unix_new (server->listen_fd);
  server->source_id =
    g_io_add_watch (channel,
                    G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL | G_IO_PRI,
                    plain_sockets_new_client_watch,
                    server);
  g_io_channel_unref (channel);
  
  return server;
}

static void
plain_sockets_stop_server (ServerData *sd,
                           void       *server_v)
{
  PlainSocketServer *server = server_v;

  g_source_remove (server->source_id);
  
  close (server->listen_fd);
  g_free (server);
  
  {
    struct stat sb;
    
    if (stat (plain_sockets_address, &sb) == 0 &&
        S_ISSOCK (sb.st_mode))
      unlink (plain_sockets_address);
  }
}

static gboolean
plain_sockets_client_side_watch (GIOChannel   *source,
                                 GIOCondition  condition,
                                 gpointer      data)
{
  ClientData *cd = data;
  int fd = g_io_channel_unix_get_fd (source);

  if (condition & G_IO_IN)
    {
      read_and_drop_on_floor (fd, echo_return_size);
    }
  else if (condition & G_IO_OUT)
    {
      cd->iterations += 1;
      if (cd->iterations >= N_ITERATIONS)
        {
          g_printerr ("\nCompleted %d iterations\n", N_ITERATIONS);
          g_main_loop_quit (cd->loop);
        }
      else if (cd->iterations % (N_ITERATIONS/N_PROGRESS_UPDATES) == 0)
        {
          g_printerr ("%d%% ", (int) (cd->iterations/(double)N_ITERATIONS * 100.0));
        }
      
      write_junk (fd, echo_call_size);
    }
  else
    {
      g_printerr ("Unexpected IO condition in client thread\n");
      exit (1);
    }

  return TRUE;
}

static void*
plain_sockets_thread_func (void *data)
{
  GMainContext *context;
  ClientData cd;
  int fd;
  struct sockaddr_un addr;
  GIOChannel *channel;
  GSource *gsource;
  
  g_printerr ("Starting client thread %p\n",
              g_thread_self());
  
  fd = socket (PF_UNIX, SOCK_STREAM, 0);
  
  if (fd < 0)
    {
      g_printerr ("Failed to create socket: %s",
                  strerror (errno)); 
      exit (1);
    }

  _DBUS_ZERO (addr);
  addr.sun_family = AF_UNIX;

#ifdef HAVE_ABSTRACT_SOCKETS
  /* remember that abstract names aren't nul-terminated so we rely
   * on sun_path being filled in with zeroes above.
   */
  addr.sun_path[0] = '\0'; /* this is what says "use abstract" */
  strncpy (&addr.sun_path[1], plain_sockets_address, _DBUS_MAX_SUN_PATH_LENGTH - 2);
  /* _dbus_verbose_bytes (addr.sun_path, sizeof (addr.sun_path)); */
#else /* HAVE_ABSTRACT_SOCKETS */
  strncpy (addr.sun_path, plain_sockets_address, _DBUS_MAX_SUN_PATH_LENGTH - 1);
#endif /* ! HAVE_ABSTRACT_SOCKETS */
  
  if (connect (fd, (struct sockaddr*) &addr, sizeof (addr)) < 0)
    {      
      g_printerr ("Failed to connect to socket %s: %s",
                  plain_sockets_address, strerror (errno));
      exit (1);
    }

  context = g_main_context_new ();

  cd.iterations = 1;
  cd.loop = g_main_loop_new (context, FALSE);

  channel = g_io_channel_unix_new (fd);
  
  gsource = g_io_create_watch (channel,
                               G_IO_IN | G_IO_OUT |
                               G_IO_ERR | G_IO_HUP | G_IO_NVAL | G_IO_PRI);

  g_source_set_callback (gsource,
                         (GSourceFunc)plain_sockets_client_side_watch,
                         &cd, NULL);

  g_source_attach (gsource, context);

  g_io_channel_unref (channel);

  g_printerr ("Client thread writing to prime pingpong\n");
  write_junk (fd, echo_call_size);
  g_printerr ("Client thread done writing primer\n");

  g_printerr ("Client thread entering main loop\n");
  g_main_loop_run (cd.loop);
  g_printerr ("Client thread %p exiting main loop\n",
              g_thread_self());

  g_source_destroy (gsource);
  
  close (fd);
  
  g_main_loop_unref (cd.loop);
  g_main_context_unref (context);

  return NULL;
}

static void
plain_sockets_main_loop_run (GMainLoop *loop)
{
  g_main_loop_run (loop);
}

static const ProfileRunVTable plain_sockets_vtable = {
  "plain sockets",
  plain_sockets_init_server,
  plain_sockets_stop_server,
  plain_sockets_thread_func,
  plain_sockets_main_loop_run
};

static double
do_profile_run (const ProfileRunVTable *vtable)
{
  GTimer *timer;
  int i;
  double secs;
  ServerData sd;
  void *server;

  sd.handled = 0;
  sd.n_clients = 0;
  sd.loop = g_main_loop_new (NULL, FALSE);

  server = (* vtable->init_server) (&sd);
  
  for (i = 0; i < N_CLIENT_THREADS; i++)
    {
      g_thread_create (vtable->client_thread_func, NULL, FALSE, NULL);
    }

  timer = g_timer_new ();
  
  g_printerr ("Server thread %p entering main loop\n",
              g_thread_self());
  (* vtable->main_loop_run_func) (sd.loop);
  g_printerr ("Server thread %p exiting main loop\n",
              g_thread_self());

  secs = g_timer_elapsed (timer, NULL);
  g_timer_destroy (timer);

  g_printerr ("%s: %g seconds, %d round trips, %f seconds per pingpong\n",
              vtable->name, secs, sd.handled, secs/sd.handled);

  (* vtable->stop_server) (&sd, server);
  
  g_main_loop_unref (sd.loop);

  return secs;
}

int
main (int argc, char *argv[])
{
  g_thread_init (NULL);
  dbus_g_thread_init ();
  
  payload = g_malloc (PAYLOAD_SIZE);

  /* The actual size of the DBusMessage on the wire, as of Nov 23 2004,
   * without the payload
   */
  echo_call_size = 140;
  echo_return_size = 32;

  if (argc > 1 && strcmp (argv[1], "plain_sockets") == 0)
    do_profile_run (&plain_sockets_vtable);
  if (argc > 1 && strcmp (argv[1], "both") == 0)
    {
      double e1, e2;
      
      e1 = do_profile_run (&plain_sockets_vtable);
      e2 = do_profile_run (&with_bus_vtable);

      g_printerr ("libdbus version is %g times slower than plain sockets\n",
                  e2/e1);
    }
  else
    do_profile_run (&with_bus_vtable);
  
  return 0;
}
