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

/**
 * @defgroup DBusGLib GLib bindings
 * @ingroup  DBus
 * @brief API for using D-BUS with GLib
 *
 * Convenience functions are provided for using D-BUS
 * with the GLib library (see http://www.gtk.org for GLib
 * information).
 * 
 */

/**
 * @defgroup DBusGLibInternals GLib bindings implementation details
 * @ingroup  DBusInternals
 * @brief Implementation details of GLib bindings
 *
 * @{
 */

/** @typedef DBusGSource
 * A GSource representing a #DBusConnection or #DBusServer
 */
typedef struct DBusGSource DBusGSource;

struct DBusGSource
{
  GSource source; /**< the parent GSource */

  GList *poll_fds;      /**< descriptors we're watching */
  GHashTable *watches;  /**< hash of DBusWatch objects */

  void *connection_or_server; /**< DBusConnection or DBusServer */
};

static GStaticMutex connection_slot_lock = G_STATIC_MUTEX_INIT;
static int connection_slot = -1;
static GStaticMutex server_slot_lock = G_STATIC_MUTEX_INIT;
static int server_slot = -1;

static gboolean dbus_connection_prepare  (GSource     *source,
                                          gint        *timeout);
static gboolean dbus_connection_check    (GSource     *source);
static gboolean dbus_connection_dispatch (GSource     *source,
                                          GSourceFunc  callback,
                                          gpointer     user_data);
static gboolean dbus_server_prepare      (GSource     *source,
                                          gint        *timeout);
static gboolean dbus_server_check        (GSource     *source);
static gboolean dbus_server_dispatch     (GSource     *source,
                                          GSourceFunc  callback,
                                          gpointer     user_data);

static GSourceFuncs dbus_connection_funcs = {
  dbus_connection_prepare,
  dbus_connection_check,
  dbus_connection_dispatch,
  NULL
};

static GSourceFuncs dbus_server_funcs = {
  dbus_server_prepare,
  dbus_server_check,
  dbus_server_dispatch,
  NULL
};

static gboolean
dbus_connection_prepare (GSource *source,
			 gint    *timeout)
{
  DBusConnection *connection = ((DBusGSource *)source)->connection_or_server;
  
  *timeout = -1;

  return (dbus_connection_get_n_messages (connection) > 0);  
}

static gboolean
dbus_server_prepare (GSource *source,
                     gint    *timeout)
{
  *timeout = -1;

  return FALSE;
}

static gboolean
dbus_gsource_check (GSource *source)
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
dbus_connection_check (GSource *source)
{
  return dbus_gsource_check (source);
}

static gboolean
dbus_server_check (GSource *source)
{
  return dbus_gsource_check (source);
}

static gboolean
dbus_gsource_dispatch (GSource     *source,
                       GSourceFunc  callback,
                       gpointer     user_data,
                       dbus_bool_t  is_server)
{
   DBusGSource *dbus_source = (DBusGSource *)source;
   GList *copy, *list;

   /* We need to traverse a copy of the list, since it can change in
      dbus_connect_handle_watch. */
   copy = g_list_copy (dbus_source->poll_fds);

   list = copy;
   while (list)
     {
       GPollFD *poll_fd = list->data;

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

           if (is_server)
             dbus_server_handle_watch (dbus_source->connection_or_server,
                                       watch, condition);
           else
             dbus_connection_handle_watch (dbus_source->connection_or_server,
                                           watch, condition);
	 }

       list = list->next;
     }

   g_list_free (copy);   

   return TRUE;
}

static gboolean
dbus_connection_dispatch (GSource     *source,
			  GSourceFunc  callback,
			  gpointer     user_data)
{
  DBusGSource *dbus_source = (DBusGSource *)source;
  DBusConnection *connection = dbus_source->connection_or_server;

  dbus_connection_ref (connection);

  dbus_gsource_dispatch (source, callback, user_data,
                         FALSE);
  
  /* Dispatch messages */
  while (dbus_connection_dispatch_message (connection))
    ;

   dbus_connection_unref (connection);
   
   return TRUE;
}

static gboolean
dbus_server_dispatch (GSource     *source,
                      GSourceFunc  callback,
                      gpointer     user_data)
{
  DBusGSource *dbus_source = (DBusGSource *)source;
  DBusServer *server = dbus_source->connection_or_server;

  dbus_server_ref (server);

  dbus_gsource_dispatch (source, callback, user_data,
                         TRUE);

  dbus_server_unref (server);
   
  return TRUE;
}
     
static dbus_bool_t
add_watch (DBusWatch *watch,
	   gpointer   data)
{
  GPollFD *poll_fd;
  DBusGSource *dbus_source;
  guint flags;
  
  dbus_source = data;
  
  poll_fd = g_new (GPollFD, 1);
  poll_fd->fd = dbus_watch_get_fd (watch);
  poll_fd->events = 0;
  flags = dbus_watch_get_flags (watch);
  dbus_watch_set_data (watch, poll_fd, NULL);

  if (flags & DBUS_WATCH_READABLE)
    poll_fd->events |= G_IO_IN;
  if (flags & DBUS_WATCH_WRITABLE)
    poll_fd->events |= G_IO_OUT;
  poll_fd->events |= G_IO_ERR | G_IO_HUP;

  g_source_add_poll ((GSource *)dbus_source, poll_fd);

  dbus_source->poll_fds = g_list_prepend (dbus_source->poll_fds, poll_fd);
  g_hash_table_insert (dbus_source->watches, poll_fd, watch);

  return TRUE;
}

static void
remove_watch (DBusWatch *watch,
	      gpointer   data)
{
  DBusGSource *dbus_source = data;
  GPollFD *poll_fd;
  
  poll_fd = dbus_watch_get_data (watch);
  
  dbus_source->poll_fds = g_list_remove (dbus_source->poll_fds, poll_fd);
  g_hash_table_remove (dbus_source->watches, poll_fd);
  g_source_remove_poll ((GSource *)dbus_source, poll_fd);
  
  g_free (poll_fd);
}

static gboolean
timeout_handler (gpointer data)
{
  DBusTimeout *timeout = data;

  dbus_timeout_handle (timeout);

  return TRUE;
}

static dbus_bool_t
add_timeout (DBusTimeout *timeout,
	     void        *data)
{
  guint timeout_tag;

  timeout_tag = g_timeout_add (dbus_timeout_get_interval (timeout),
			       timeout_handler, timeout);
  
  dbus_timeout_set_data (timeout, GUINT_TO_POINTER (timeout_tag), NULL);

  return TRUE;
}

static void
remove_timeout (DBusTimeout *timeout,
		void        *data)
{
  guint timeout_tag;
  
  timeout_tag = GPOINTER_TO_UINT (dbus_timeout_get_data (timeout));
  
  g_source_remove (timeout_tag);
}

static void
free_source (GSource *source)
{
  g_source_destroy (source);
}

static void
wakeup_main (void *data)
{
  g_main_context_wakeup (NULL);
}


/** @} */ /* End of GLib bindings internals */

/** @addtogroup DBusGLib
 * @{
 */

static GSource*
create_source (void         *connection_or_server,
               GSourceFuncs *funcs)
{
  GSource *source;
  DBusGSource *dbus_source;

  source = g_source_new (funcs, sizeof (DBusGSource));
  
  dbus_source = (DBusGSource *)source;  
  dbus_source->watches = g_hash_table_new (NULL, NULL);
  dbus_source->connection_or_server = connection_or_server;

  return source;
}

/**
 * Sets the watch and timeout functions of a #DBusConnection
 * to integrate the connection with the GLib main loop.
 *
 * @param connection the connection
 */
void
dbus_connection_setup_with_g_main (DBusConnection *connection)
{
  GSource *source;

  source = create_source (connection, &dbus_connection_funcs);

  if (!dbus_connection_set_watch_functions (connection,
                                            add_watch,
                                            remove_watch,
                                            source, NULL))
    goto nomem;

  if (!dbus_connection_set_timeout_functions (connection,
                                              add_timeout,
                                              remove_timeout,
                                              NULL, NULL))
    goto nomem;
    
  dbus_connection_set_wakeup_main_function (connection,
					    wakeup_main,
					    NULL, NULL);
      
  g_source_attach (source, NULL);

  g_static_mutex_lock (&connection_slot_lock);
  if (connection_slot == -1 )
    connection_slot = dbus_connection_allocate_data_slot ();
  g_static_mutex_unlock (&connection_slot_lock);

  if (connection_slot < 0)
    goto nomem;

  if (!dbus_connection_set_data (connection, connection_slot, source,
                                 (DBusFreeFunction)free_source))
    goto nomem;

  return;

 nomem:
  g_error ("Not enough memory to set up DBusConnection for use with GLib");
}

/**
 * Sets the watch and timeout functions of a #DBusServer
 * to integrate the server with the GLib main loop.
 *
 * @param server the server
 */
void
dbus_server_setup_with_g_main (DBusServer *server)
{
  GSource *source;

  source = create_source (server, &dbus_server_funcs);

  dbus_server_set_watch_functions (server,
                                   add_watch,
                                   remove_watch,
                                   source, NULL);

  dbus_server_set_timeout_functions (server,
                                     add_timeout,
                                     remove_timeout,
                                     NULL, NULL);
  
  g_source_attach (source, NULL);

  g_static_mutex_lock (&server_slot_lock);
  if (server_slot == -1 )
    server_slot = dbus_server_allocate_data_slot ();
  g_static_mutex_unlock (&server_slot_lock);

  if (server_slot < 0)
    goto nomem;

  if (!dbus_server_set_data (server, server_slot, source,
                             (DBusFreeFunction)free_source))
    goto nomem;

  return;

 nomem:
  g_error ("Not enough memory to set up DBusServer for use with GLib");
}

/** @} */ /* end of public API */
