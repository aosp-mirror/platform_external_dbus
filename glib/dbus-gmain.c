/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gmain.c GLib main loop integration
 *
 * Copyright (C) 2002, 2003 CodeFactory AB
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
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include "dbus-gtest.h"
#include "dbus-gutils.h"

#include <libintl.h>
#define _(x) dgettext (GETTEXT_PACKAGE, x)
#define N_(x) x

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

/**
 * A GSource subclass for a DBusConnection.
 */
struct DBusGSource
{
  GSource source; /**< the parent GSource */

  GList *watch_fds;      /**< descriptors we're watching */

  GMainContext *context; /**< the GMainContext to use, NULL for default */

  void *connection_or_server; /**< DBusConnection or DBusServer */
};

/**
 * Auxillary struct for pairing up a #DBusWatch and associated
 * #GPollFD
 */
typedef struct
{
  int refcount;     /**< reference count */

  GPollFD poll_fd;  /**< the #GPollFD to use with g_source_add_poll() */
  DBusWatch *watch; /**< the corresponding DBusWatch*/
  
  unsigned int removed : 1; /**< true if this #WatchFD has been removed */
} WatchFD;

static WatchFD *
watch_fd_new (void)
{
  WatchFD *watch_fd;

  watch_fd = g_new0 (WatchFD, 1);
  watch_fd->refcount = 1;

  return watch_fd;
}

static WatchFD * 
watch_fd_ref (WatchFD *watch_fd)
{
  watch_fd->refcount += 1;

  return watch_fd;
}

static void
watch_fd_unref (WatchFD *watch_fd)
{
  watch_fd->refcount -= 1;

  if (watch_fd->refcount == 0)
    {
      g_assert (watch_fd->removed);
  
      g_free (watch_fd);
    }
}

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

  list = dbus_source->watch_fds;

  while (list)
    {
      WatchFD *watch_fd = list->data;

      if (watch_fd->poll_fd.revents != 0)
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

   /* Make a copy of the list and ref all WatchFDs */
   copy = g_list_copy (dbus_source->watch_fds);
   g_list_foreach (copy, (GFunc)watch_fd_ref, NULL);
   
   list = copy;
   while (list)
     {
       WatchFD *watch_fd = list->data;

       if (!watch_fd->removed && watch_fd->poll_fd.revents != 0)
	 {
	   guint condition = 0;
	   
	   if (watch_fd->poll_fd.revents & G_IO_IN)
	     condition |= DBUS_WATCH_READABLE;
	   if (watch_fd->poll_fd.revents & G_IO_OUT)
	     condition |= DBUS_WATCH_WRITABLE;
	   if (watch_fd->poll_fd.revents & G_IO_ERR)
	     condition |= DBUS_WATCH_ERROR;
	   if (watch_fd->poll_fd.revents & G_IO_HUP)
	     condition |= DBUS_WATCH_HANGUP;

           dbus_watch_handle (watch_fd->watch, condition);
	 }

       list = list->next;
     }

   g_list_foreach (copy, (GFunc)watch_fd_unref, NULL);   
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
  WatchFD *watch_fd;
  DBusGSource *dbus_source;
  guint flags;

  if (!dbus_watch_get_enabled (watch))
    return TRUE;
  
  dbus_source = data;

  watch_fd = watch_fd_new ();
  watch_fd->poll_fd.fd = dbus_watch_get_fd (watch);
  watch_fd->poll_fd.events = 0;
  flags = dbus_watch_get_flags (watch);
  dbus_watch_set_data (watch, watch_fd, (DBusFreeFunction)watch_fd_unref);

  if (flags & DBUS_WATCH_READABLE)
    watch_fd->poll_fd.events |= G_IO_IN;
  if (flags & DBUS_WATCH_WRITABLE)
    watch_fd->poll_fd.events |= G_IO_OUT;
  watch_fd->poll_fd.events |= G_IO_ERR | G_IO_HUP;

  watch_fd->watch = watch;
  
  g_source_add_poll ((GSource *)dbus_source, &watch_fd->poll_fd);

  dbus_source->watch_fds = g_list_prepend (dbus_source->watch_fds, watch_fd);

  return TRUE;
}

static void
remove_watch (DBusWatch *watch,
	      gpointer   data)
{
  DBusGSource *dbus_source = data;
  WatchFD *watch_fd;
  
  watch_fd = dbus_watch_get_data (watch);
  if (watch_fd == NULL)
    return; /* probably a not-enabled watch that was added */

  watch_fd->removed = TRUE;
  watch_fd->watch = NULL;
  
  dbus_source->watch_fds = g_list_remove (dbus_source->watch_fds, watch_fd);

  g_source_remove_poll ((GSource *)dbus_source, &watch_fd->poll_fd);

  dbus_watch_set_data (watch, NULL, NULL); /* needed due to watch_toggled
                                            * breaking add/remove symmetry
                                            */
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
 * If called twice for the same context, does nothing the second
 * time. If called once with context A and once with context B,
 * context B replaces context A as the context monitoring the
 * connection.
 *
 * @param connection the connection
 * @param context the #GMainContext or #NULL for default context
 */
void
dbus_connection_setup_with_g_main (DBusConnection *connection,
				   GMainContext   *context)
{
  GSource *source;
  
  /* FIXME we never free the slot, so its refcount just keeps growing,
   * which is kind of broken.
   */
  dbus_connection_allocate_data_slot (&connection_slot);
  if (connection_slot < 0)
    goto nomem;

  /* So we can test for equality below */
  if (context == NULL)
    context = g_main_context_default ();
  
  source = dbus_connection_get_data (connection, connection_slot);
  if (source != NULL)
    {
      if (source->context == context)
        return; /* nothing to do */

      /* Remove the previous source and move to a new context */
      dbus_connection_set_data (connection, connection_slot, NULL, NULL);
      source = NULL;
    }
  
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
                                              source, NULL))
    goto nomem;
    
  dbus_connection_set_wakeup_main_function (connection,
					    wakeup_main,
					    source, NULL);
      
  g_source_attach (source, context);

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
 * If called twice for the same context, does nothing the second
 * time. If called once with context A and once with context B,
 * context B replaces context A as the context monitoring the
 * connection.
 *
 * @param server the server
 * @param context the #GMainContext or #NULL for default
 */
void
dbus_server_setup_with_g_main (DBusServer   *server,
                               GMainContext *context)
{
  GSource *source;

  dbus_server_allocate_data_slot (&server_slot);
  if (server_slot < 0)
    goto nomem;

  /* So we can test for equality below */
  if (context == NULL)
    context = g_main_context_default ();
  
  source = dbus_server_get_data (server, server_slot);
  if (source != NULL)
    {
      if (source->context == context)
        return; /* nothing to do */

      /* Remove the previous source and move to a new context */
      dbus_server_set_data (server, server_slot, NULL, NULL);
      source = NULL;
    }
  
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

  if (!dbus_server_set_data (server, server_slot, source,
                             (DBusFreeFunction)free_source))
    goto nomem;

  return;

 nomem:
  g_error ("Not enough memory to set up DBusServer for use with GLib");
}

/**
 * Returns a connection to the given bus. The connection is a global variable
 * shared with other callers of this function.
 * 
 * (Internally, calls dbus_bus_get() then calls
 * dbus_connection_setup_with_g_main() on the result.)
 *
 * @param type bus type
 * @param error address where an error can be returned.
 * @returns a DBusConnection
 */
DBusGConnection*
dbus_g_bus_get (DBusBusType     type,
                GError        **error)
{
  DBusConnection *connection;
  DBusError derror;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  
  dbus_error_init (&derror);

  connection = dbus_bus_get (type, &derror);
  if (connection == NULL)
    {
      dbus_set_g_error (error, &derror);
      dbus_error_free (&derror);
      return NULL;
    }

  /* does nothing if it's already been done */
  dbus_connection_setup_with_g_main (connection, NULL);

  return DBUS_G_CONNECTION_FROM_CONNECTION (connection);
}

/**
 * The implementation of DBUS_GERROR error domain. See documentation
 * for GError in GLib reference manual.
 *
 * @returns the error domain quark for use with GError
 */
GQuark
dbus_g_error_quark (void)
{
  static GQuark quark = 0;
  if (quark == 0)
    quark = g_quark_from_static_string ("g-exec-error-quark");
  return quark;
}


/**
 * Set a GError return location from a DBusError.
 *
 * @todo expand the DBUS_GERROR enum and take advantage of it here
 * 
 * @param gerror location to store a GError, or #NULL
 * @param derror the DBusError
 */
void
dbus_set_g_error (GError   **gerror,
                  DBusError *derror)
{
  g_return_if_fail (derror != NULL);
  g_return_if_fail (dbus_error_is_set (derror));
  
  g_set_error (gerror, DBUS_GERROR,
               DBUS_GERROR_FAILED,
               _("D-BUS error %s: %s"),
               derror->name, derror->message);  
}

/**
 * Get the GLib type ID for a DBusConnection boxed type.
 *
 * @returns GLib type
 */
GType
dbus_connection_get_g_type (void)
{
  static GType our_type = 0;
  
  if (our_type == 0)
    our_type = g_boxed_type_register_static ("DBusConnection",
                                             (GBoxedCopyFunc) dbus_connection_ref,
                                             (GBoxedFreeFunc) dbus_connection_unref);

  return our_type;
}

/**
 * Get the GLib type ID for a DBusMessage boxed type.
 *
 * @returns GLib type
 */
GType
dbus_message_get_g_type (void)
{
  static GType our_type = 0;
  
  if (our_type == 0)
    our_type = g_boxed_type_register_static ("DBusMessage",
                                             (GBoxedCopyFunc) dbus_message_ref,
                                             (GBoxedFreeFunc) dbus_message_unref);

  return our_type;
}

static DBusGConnection*
dbus_g_connection_ref (DBusGConnection *gconnection)
{
  DBusConnection *c;

  c = DBUS_CONNECTION_FROM_G_CONNECTION (gconnection);
  dbus_connection_ref (c);
  return gconnection;
}

static void
dbus_g_connection_unref (DBusGConnection *gconnection)
{
  DBusConnection *c;

  c = DBUS_CONNECTION_FROM_G_CONNECTION (gconnection);
  dbus_connection_unref (c);
}


static DBusGMessage*
dbus_g_message_ref (DBusGMessage *gmessage)
{
  DBusMessage *c;

  c = DBUS_MESSAGE_FROM_G_MESSAGE (gmessage);
  dbus_message_ref (c);
  return gmessage;
}

static void
dbus_g_message_unref (DBusGMessage *gmessage)
{
  DBusMessage *c;

  c = DBUS_MESSAGE_FROM_G_MESSAGE (gmessage);
  dbus_message_unref (c);
}

/**
 * Get the GLib type ID for a DBusGConnection boxed type.
 *
 * @returns GLib type
 */
GType
dbus_g_connection_get_g_type (void)
{
  static GType our_type = 0;
  
  if (our_type == 0)
    our_type = g_boxed_type_register_static ("DBusGConnection",
                                             (GBoxedCopyFunc) dbus_g_connection_ref,
                                             (GBoxedFreeFunc) dbus_g_connection_unref);

  return our_type;
}

/**
 * Get the GLib type ID for a DBusGMessage boxed type.
 *
 * @returns GLib type
 */
GType
dbus_g_message_get_g_type (void)
{
  static GType our_type = 0;
  
  if (our_type == 0)
    our_type = g_boxed_type_register_static ("DBusGMessage",
                                             (GBoxedCopyFunc) dbus_g_message_ref,
                                             (GBoxedFreeFunc) dbus_g_message_unref);

  return our_type;
}

/**
 * Get the DBusConnection corresponding to this DBusGConnection.
 * The return value does not have its refcount incremented.
 *
 * @returns DBusConnection 
 */
DBusConnection*
dbus_g_connection_get_connection (DBusGConnection *gconnection)
{
  return DBUS_CONNECTION_FROM_G_CONNECTION (gconnection);
}

/**
 * Get the DBusMessage corresponding to this DBusGMessage.
 * The return value does not have its refcount incremented.
 *
 * @returns DBusMessage 
 */
DBusMessage*
dbus_g_message_get_message (DBusGMessage *gmessage)
{
  return DBUS_MESSAGE_FROM_G_MESSAGE (gmessage);
}

/** @} */ /* end of public API */

#ifdef DBUS_BUILD_TESTS

/**
 * @ingroup DBusGLibInternals
 * Unit test for GLib main loop integration
 * @returns #TRUE on success.
 */
gboolean
_dbus_gmain_test (const char *test_data_dir)
{
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
