/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gmain.c GLib main loop integration
 *
 * Copyright (C) 2002  CodeFactory AB
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

#include "dbus-glib.h"
#include <glib.h>

typedef struct _DBusGSource DBusGSource;

struct _DBusGSource
{
  GSource source;

  DBusConnection *connection;

  GList *poll_fds;
  GHashTable *watches;
};

static gboolean gdbus_connection_prepare  (GSource     *source,
					   gint        *timeout);
static gboolean gdbus_connection_check    (GSource     *source);
static gboolean gdbus_connection_dispatch (GSource     *source,
					   GSourceFunc  callback,
					   gpointer     user_data);

static GSourceFuncs dbus_funcs = {
  gdbus_connection_prepare,
  gdbus_connection_check,
  gdbus_connection_dispatch,
  NULL
};

static gboolean
gdbus_connection_prepare (GSource     *source,
			  gint        *timeout)
{
  DBusConnection *connection = ((DBusGSource *)source)->connection;
  
  *timeout = -1;

  return (dbus_connection_peek_message (connection) != NULL);
}

static gboolean
gdbus_connection_check (GSource *source)
{
  DBusGSource *dbus_source = (DBusGSource *)source;
  GList *list;

  list = dbus_source->poll_fds;

  while (list)
    {
      GPollFD *poll_fd = list->data;

      if (poll_fd->revents != 0)
	return TRUE;

      list = list->next;
    }

  return FALSE;
}

static gboolean
gdbus_connection_dispatch (GSource     *source,
			   GSourceFunc  callback,
			   gpointer     user_data)
{
   DBusGSource *dbus_source = (DBusGSource *)source;
   DBusMessageHandler handler = (DBusMessageHandler)callback;
   DBusMessage *message;
   
   GList *list;

   list = dbus_source->poll_fds;

   while (list)
     {
       GPollFD *poll_fd = list->data;

       g_print ("poll_fd is: %p\n", poll_fd);
       if (poll_fd->revents != 0)
	 {
	   DBusWatch *watch = g_hash_table_lookup (dbus_source->watches, poll_fd);
	   guint condition = 0;

	   if (poll_fd->revents & G_IO_IN)
	     condition |= DBUS_WATCH_READABLE;
	   if (poll_fd->revents & G_IO_OUT)
	     condition |= DBUS_WATCH_WRITABLE;
	   if (poll_fd->revents & G_IO_ERR)
	     condition |= DBUS_WATCH_ERROR;
	   if (poll_fd->revents & G_IO_HUP)
	     condition |= DBUS_WATCH_HANGUP;
	   
	   dbus_connection_handle_watch (dbus_source->connection, watch, condition);
	 }
       
       list = list->next;
     }

   while ((message = dbus_connection_pop_message (dbus_source->connection)))
     {
       handler (dbus_source->connection, message, user_data);

       dbus_message_unref (message);
     }
   
   return TRUE;
}

static void
gdbus_add_connection_watch (DBusWatch      *watch,
			    DBusGSource    *source)
{
  GPollFD *poll_fd;
  guint flags;
  
  poll_fd = g_new (GPollFD, 1);
  poll_fd->fd = dbus_watch_get_fd (watch);

  poll_fd->events = 0;
  flags = dbus_watch_get_flags (watch);
  dbus_watch_set_data (watch, poll_fd, NULL);
  
  if (flags & DBUS_WATCH_READABLE)
    poll_fd->events |= G_IO_IN;

  if (flags & DBUS_WATCH_WRITABLE)
    poll_fd->events |= G_IO_OUT;

  g_source_add_poll ((GSource *)source, poll_fd);
  
  g_print ("Add connection watch: %p!\n", watch);

  source->poll_fds = g_list_prepend (source->poll_fds, poll_fd);
  g_hash_table_insert (source->watches, poll_fd, watch);
}

static void
gdbus_remove_connection_watch (DBusWatch      *watch,
			       DBusGSource    *source)
{
  GPollFD *poll_fd;

  poll_fd = dbus_watch_get_data (watch);

  source->poll_fds = g_list_remove (source->poll_fds, poll_fd);
  g_hash_table_remove (source->watches, poll_fd);
  g_source_remove_poll ((GSource *)source, poll_fd);
  
  g_free (poll_fd);
}

GSource *
dbus_connection_gsource_new (DBusConnection *connection)
{
  GSource *source = g_source_new (&dbus_funcs, sizeof (DBusGSource));
  DBusGSource *dbus_source = (DBusGSource *)source;

  dbus_source->watches = g_hash_table_new (NULL, NULL);
  dbus_source->connection = connection;
  dbus_connection_ref (dbus_source->connection);
  
  dbus_connection_set_watch_functions (connection,
                                       (DBusAddWatchFunction) gdbus_add_connection_watch,
                                       (DBusRemoveWatchFunction) gdbus_remove_connection_watch,
                                       dbus_source,
                                       NULL);


  return source;
}
