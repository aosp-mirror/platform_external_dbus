#include "watch.h"
#include <stdio.h>

#define DBUS_COMPILATION /* cheat and use DBusList */
#include <dbus/dbus-list.h>
#undef DBUS_COMPILATION

/* Cheesy main loop used in test programs.  Any real app would use the
 * GLib or Qt or other non-sucky main loops.
 */ 

#undef	MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

static DBusList *watches = NULL;
static dbus_bool_t exited = FALSE;
static DBusList *connections = NULL;

typedef enum
{
  WATCH_CONNECTION,
  WATCH_SERVER
} WatchType;

typedef struct
{
  WatchType type;
  void *data;
} WatchData;

static void
add_connection_watch (DBusWatch      *watch,
                      DBusConnection *connection)
{
  WatchData *wd;

  wd = dbus_new0 (WatchData, 1);
  wd->type = WATCH_CONNECTION;
  wd->data = connection;
  
  _dbus_list_append (&watches, watch);
  dbus_watch_set_data (watch, wd, dbus_free);
}

static void
remove_connection_watch (DBusWatch      *watch,
                         DBusConnection *connection)
{
  _dbus_list_remove (&watches, watch);
  dbus_watch_set_data (watch, NULL, NULL);
}

static void
add_server_watch (DBusWatch      *watch,
                  DBusServer     *server)
{
  WatchData *wd;
  
  wd = dbus_new0 (WatchData, 1);
  wd->type = WATCH_SERVER;
  wd->data = server;
  
  _dbus_list_append (&watches, watch);

  dbus_watch_set_data (watch, wd, dbus_free);
}

static void
remove_server_watch (DBusWatch      *watch,
                     DBusServer     *server)
{
  _dbus_list_remove (&watches, watch);
  dbus_watch_set_data (watch, NULL, NULL);
}

static int count = 0;

static void
check_messages (void)
{
  DBusList *link;
  
  link = _dbus_list_get_first_link (&connections);
  while (link != NULL)
    {
      DBusList *next = _dbus_list_get_next_link (&connections, link);
      DBusConnection *connection = link->data;
      DBusMessage *message;
      
      while ((message = dbus_connection_pop_message (connection)))
        {
          DBusMessage *reply;

          printf ("Received message %d, sending reply\n", count);
          
          reply = dbus_message_new ();
          dbus_connection_send_message (connection,
                                        reply,
                                        NULL);
          dbus_message_unref (reply);

          dbus_message_unref (message);

          count += 1;
          if (count > 100)
            {
              printf ("Saw %d messages, exiting\n", count);
              quit_mainloop ();
            }
        }
      
      link = next;
    }
}

void
do_mainloop (void)
{
  /* Of course with any real app you'd use GMainLoop or
   * QSocketNotifier and not have to see all this crap.
   */
  
  while (!exited && watches != NULL)
    {
      fd_set read_set;
      fd_set write_set;
      fd_set err_set;
      int max_fd;
      DBusList *link;

      check_messages ();
      
      FD_ZERO (&read_set);
      FD_ZERO (&write_set);
      FD_ZERO (&err_set);

      max_fd = -1;

      link = _dbus_list_get_first_link (&watches);
      while (link != NULL)
        {
          DBusList *next = _dbus_list_get_next_link (&watches, link);
          int fd;
          DBusWatch *watch;
          unsigned int flags;
          
          watch = link->data;
          
          fd = dbus_watch_get_fd (watch);
          flags = dbus_watch_get_flags (watch);
          
          max_fd = MAX (max_fd, fd);
          
          if (flags & DBUS_WATCH_READABLE)
            FD_SET (fd, &read_set);

          if (flags & DBUS_WATCH_WRITABLE)
            FD_SET (fd, &write_set);

          FD_SET (fd, &err_set);
          
          link = next;
        }

      select (max_fd + 1, &read_set, &write_set, &err_set, NULL);

      link = _dbus_list_get_first_link (&watches);
      while (link != NULL)
        {
          DBusList *next = _dbus_list_get_next_link (&watches, link);
          int fd;
          DBusWatch *watch;
          unsigned int flags;
          unsigned int condition;
          
          watch = link->data;
          
          fd = dbus_watch_get_fd (watch);
          flags = dbus_watch_get_flags (watch);

          condition = 0;
          
          if ((flags & DBUS_WATCH_READABLE) &&
              FD_ISSET (fd, &read_set))
            condition |= DBUS_WATCH_READABLE;

          if ((flags & DBUS_WATCH_WRITABLE) &&
              FD_ISSET (fd, &write_set))
            condition |= DBUS_WATCH_WRITABLE;

          if (FD_ISSET (fd, &err_set))
            condition |= DBUS_WATCH_ERROR;

          if (condition != 0)
            {
              WatchData *wd;

              wd = dbus_watch_get_data (watch);

              if (wd->type == WATCH_CONNECTION)
                {
                  DBusConnection *connection = wd->data;

                  dbus_connection_handle_watch (connection,
                                                watch,
                                                condition);
                }
              else if (wd->type == WATCH_SERVER)
                {
                  DBusServer *server = wd->data;
                  
                  dbus_server_handle_watch (server,
                                            watch,
                                            condition);
                }
            }
          
          link = next;
        }
    }
}

void
quit_mainloop (void)
{
  exited = TRUE;
}

static void
error_handler (DBusConnection *connection,
               DBusResultCode  error_code,
               void           *data)
{
  fprintf (stderr,
           "Error on connection: %s\n",
           dbus_result_to_string (error_code));

  _dbus_list_remove (&connections, connection);
  dbus_connection_unref (connection);
  quit_mainloop ();
}

void
setup_connection (DBusConnection *connection)
{
  dbus_connection_set_watch_functions (connection,
                                       (DBusAddWatchFunction) add_connection_watch,
                                       (DBusRemoveWatchFunction) remove_connection_watch,
                                       connection,
                                       NULL);

  dbus_connection_set_error_function (connection,
                                      error_handler,
                                      NULL, NULL);

  dbus_connection_ref (connection);
  _dbus_list_append (&connections, connection);
}

void
setup_server (DBusServer *server)
{
  dbus_server_set_watch_functions (server,
                                   (DBusAddWatchFunction) add_server_watch,
                                   (DBusRemoveWatchFunction) remove_server_watch,
                                   server,
                                   NULL);
}
