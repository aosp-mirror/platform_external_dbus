/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gmain.c GLib main loop integration
 *
 * Copyright (C) 2002, 2003  CodeFactory AB
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

typedef struct
{
  DBusWatch *watch;
  DBusConnection *connection;

  guint tag;
} WatchCallback;

static gboolean
watch_callback (GIOChannel *source,
		GIOCondition condition,
		gpointer data)
{
  WatchCallback *cb = data;
  unsigned int flags = 0;

  if (condition & G_IO_IN)
    flags |= DBUS_WATCH_READABLE;
  if (condition & G_IO_OUT)
    flags |= DBUS_WATCH_WRITABLE;
  if (condition & G_IO_ERR)
    flags |= DBUS_WATCH_ERROR;
  if (condition & G_IO_HUP)
    flags |= DBUS_WATCH_HANGUP;

  dbus_connection_handle_watch (cb->connection,
				cb->watch,
				flags);

  /* Dispatch messages */
  while (dbus_connection_dispatch_message (cb->connection));
  
  return TRUE;
}

static void
free_callback_data (WatchCallback *cb)
{
  dbus_connection_unref (cb->connection);
  g_free (cb);
}

static void
add_watch (DBusWatch *watch,
	   gpointer   data)
{
  GIOChannel *channel;
  DBusConnection *connection = data;
  GIOCondition condition = 0;
  WatchCallback *cb;
  guint tag;
  gint flags;

  flags = dbus_watch_get_flags (watch);
  condition = 0;
  
  if (flags & DBUS_WATCH_READABLE)
    condition |= G_IO_IN;
  if (flags & DBUS_WATCH_WRITABLE)
    condition |= G_IO_OUT;
  if (flags & DBUS_WATCH_ERROR)
    condition |= G_IO_ERR;
  if (flags & DBUS_WATCH_HANGUP)
    condition |= G_IO_HUP;
  
  channel = g_io_channel_unix_new (dbus_watch_get_fd (watch));
  g_io_channel_set_encoding (channel, NULL, NULL);
  g_io_channel_set_buffered (channel, FALSE);

  cb = g_new0 (WatchCallback, 1);
  cb->watch = watch;
  cb->connection = connection;
  dbus_connection_ref (connection);
  
  dbus_watch_set_data (watch, cb, (DBusFreeFunction)free_callback_data);
  
  tag = g_io_add_watch (channel, condition, watch_callback, cb);
  cb->tag = tag;
}

static void
remove_watch (DBusWatch *watch,
	      gpointer   data)
{
  WatchCallback *cb;

  cb = dbus_watch_get_data (watch);

  g_source_remove (cb->tag);
  
  dbus_watch_set_data (watch, NULL, NULL);
}

static void
add_timeout (DBusTimeout *timeout,
	     void        *data)
{
}

static void
remove_timeout (DBusTimeout *timeout,
		void        *data)
{
}

void
dbus_connection_hookup_with_g_main (DBusConnection *connection)
{

  dbus_connection_set_watch_functions (connection,
				       add_watch,
				       remove_watch,
				       connection, NULL);
  dbus_connection_set_timeout_functions (connection,
					 add_timeout,
					 remove_timeout,
					 NULL, NULL);
					 
}
