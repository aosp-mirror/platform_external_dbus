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

  GMainContext *context; /**< the GMainContext to use, NULL for default */

  void *connection_or_server; /**< DBusConnection or DBusServer */
};

static dbus_int32_t connection_slot = -1;
static dbus_int32_t server_slot = -1;

static gboolean gsource_connection_prepare  (GSource     *source,
                                             gint        *timeout);
static gboolean gsource_connection_check    (GSource     *source);
static gboolean gsource_connection_dispatch (GSource     *source,
                                             GSourceFunc  callback,
                                             gpointer     user_data);
static gboolean gsource_server_prepare      (GSource     *source,
                                             gint        *timeout);
static gboolean gsource_server_check        (GSource     *source);
static gboolean gsource_server_dispatch     (GSource     *source,
                                             GSourceFunc  callback,
                                             gpointer     user_data);

static GSourceFuncs dbus_connection_funcs = {
  gsource_connection_prepare,
  gsource_connection_check,
  gsource_connection_dispatch,
  NULL
};

static GSourceFuncs dbus_server_funcs = {
  gsource_server_prepare,
  gsource_server_check,
  gsource_server_dispatch,
  NULL
};

static gboolean
gsource_connection_prepare (GSource *source,
                            gint    *timeout)
{
  DBusConnection *connection = ((DBusGSource *)source)->connection_or_server;
  
  *timeout = -1;

  return (dbus_connection_get_dispatch_status (connection) == DBUS_DISPATCH_DATA_REMAINS);  
}

static gboolean
gsource_server_prepare (GSource *source,
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
gsource_connection_check (GSource *source)
{
  return dbus_gsource_check (source);
}

static gboolean
gsource_server_check (GSource *source)
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
      dbus_watch_handle(). */
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

           dbus_watch_handle (watch, condition);
	 }

       list = list->next;
     }

   g_list_free (copy);   

   return TRUE;
}

static gboolean
gsource_connection_dispatch (GSource     *source,
                             GSourceFunc  callback,
                             gpointer     user_data)
{
  DBusGSource *dbus_source = (DBusGSource *)source;
  DBusConnection *connection = dbus_source->connection_or_server;

  dbus_connection_ref (connection);

  dbus_gsource_dispatch (source, callback, user_data,
                         FALSE);
  
  /* Dispatch messages */
  while (dbus_connection_dispatch (connection) == DBUS_DISPATCH_DATA_REMAINS)
    ;

   dbus_connection_unref (connection);
   
   return TRUE;
}

static gboolean
gsource_server_dispatch (GSource     *source,
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

  if (!dbus_watch_get_enabled (watch))
    return TRUE;
  
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
  if (poll_fd == NULL)
    return; /* probably a not-enabled watch that was added */
  
  dbus_source->poll_fds = g_list_remove (dbus_source->poll_fds, poll_fd);
  g_hash_table_remove (dbus_source->watches, poll_fd);
  g_source_remove_poll ((GSource *)dbus_source, poll_fd);

  dbus_watch_set_data (watch, NULL, NULL); /* needed due to watch_toggled
                                            * breaking add/remove symmetry
                                            */
  
  g_free (poll_fd);
}

static void
watch_toggled (DBusWatch *watch,
               void      *data)
{
  /* Because we just exit on OOM, enable/disable is
   * no different from add/remove
   */
  if (dbus_watch_get_enabled (watch))
    add_watch (watch, data);
  else
    remove_watch (watch, data);
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
  DBusGSource *dbus_source = data;
  GSource *source;

  if (!dbus_timeout_get_enabled (timeout))
    return TRUE;
  
  source = g_timeout_source_new (dbus_timeout_get_interval (timeout));
  g_source_set_callback (source, timeout_handler, timeout, NULL);
  g_source_attach (source, dbus_source->context);
  
  dbus_timeout_set_data (timeout, GUINT_TO_POINTER (g_source_get_id (source)),
      			 NULL);

  return TRUE;
}

static void
remove_timeout (DBusTimeout *timeout,
		void        *data)
{
  guint timeout_tag;
  
  timeout_tag = GPOINTER_TO_UINT (dbus_timeout_get_data (timeout));

  if (timeout_tag != 0) /* if 0, probably timeout was disabled */
    g_source_remove (timeout_tag);
}

static void
timeout_toggled (DBusTimeout *timeout,
                 void        *data)
{
  /* Because we just exit on OOM, enable/disable is
   * no different from add/remove
   */
  if (dbus_timeout_get_enabled (timeout))
    add_timeout (timeout, data);
  else
    remove_timeout (timeout, data);
}


static void
free_source (GSource *source)
{
  g_source_destroy (source);
}

static void
wakeup_main (void *data)
{
  DBusGSource *dbus_source = data;

  g_main_context_wakeup (dbus_source->context);
}


/** @} */ /* End of GLib bindings internals */

/** @addtogroup DBusGLib
 * @{
 */

static GSource*
create_source (void         *connection_or_server,
               GSourceFuncs *funcs,
	       GMainContext *context)
{
  GSource *source;
  DBusGSource *dbus_source;

  source = g_source_new (funcs, sizeof (DBusGSource));
  
  dbus_source = (DBusGSource *)source;  
  dbus_source->watches = g_hash_table_new (NULL, NULL);
  dbus_source->connection_or_server = connection_or_server;
  dbus_source->context = context;

  return source;
}

/**
 * Sets the watch and timeout functions of a #DBusConnection
 * to integrate the connection with the GLib main loop.
 * Pass in #NULL for the #GMainContext unless you're
 * doing something specialized.
 *
 * @param connection the connection
 * @param context the #GMainContext or #NULL for default context
 */
void
dbus_connection_setup_with_g_main (DBusConnection *connection,
				   GMainContext   *context)
{
  GSource *source;

  source = create_source (connection, &dbus_connection_funcs, context);

  if (!dbus_connection_set_watch_functions (connection,
                                            add_watch,
                                            remove_watch,
                                            watch_toggled,
                                            source, NULL))
    goto nomem;

  if (!dbus_connection_set_timeout_functions (connection,
                                              add_timeout,
                                              remove_timeout,
                                              timeout_toggled,
                                              NULL, NULL))
    goto nomem;
    
  dbus_connection_set_wakeup_main_function (connection,
					    wakeup_main,
					    source, NULL);
      
  g_source_attach (source, context);

  /* FIXME we never free the slot, so its refcount just keeps growing,
   * which is kind of broken.
   */
  dbus_connection_allocate_data_slot (&connection_slot);
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
 * In most cases the context argument should be #NULL.
 *
 * @param server the server
 * @param context the #GMainContext or #NULL for default
 */
void
dbus_server_setup_with_g_main (DBusServer   *server,
                               GMainContext *context)
{
  GSource *source;

  source = create_source (server, &dbus_server_funcs, context);

  dbus_server_set_watch_functions (server,
                                   add_watch,
                                   remove_watch,
                                   watch_toggled,
                                   source, NULL);

  dbus_server_set_timeout_functions (server,
                                     add_timeout,
                                     remove_timeout,
                                     timeout_toggled,
                                     NULL, NULL);
  
  g_source_attach (source, context);

  /* FIXME we never free the slot, so its refcount just keeps growing,
   * which is kind of broken.
   */
  dbus_server_allocate_data_slot (&server_slot);
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
