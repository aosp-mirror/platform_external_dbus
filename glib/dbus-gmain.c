/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gmain.c GLib main loop integration
 *
 * Copyright (C) 2002, 2003 CodeFactory AB
 * Copyright (C) 2005 Red Hat, Inc.
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
 * @brief API for using D-BUS with GLib
 *
 * libdbus proper is a low-level API, these GLib bindings wrap libdbus
 * with a much higher-level approach. The higher level approach is
 * possible because GLib defines a main loop, an object/type system,
 * and an out-of-memory handling policy (it exits the program).
 * See http://www.gtk.org for GLib information.
 *
 * To manipulate remote objects, use #DBusGProxy.
 */

/**
 * @defgroup DBusGLibInternals GLib bindings implementation details
 * @ingroup  DBusInternals
 * @brief Implementation details of GLib bindings
 *
 * @{
 */

typedef struct
{
  GMainContext *context;      /**< the main context */
  GSList *ios;                /**< all IOHandler */
  GSList *timeouts;           /**< all TimeoutHandler */
  DBusConnection *connection; /**< NULL if this is really for a server not a connection */
} ConnectionSetup;


typedef struct
{
  ConnectionSetup *cs;
  GSource *source;
  DBusWatch *watch;
} IOHandler;

typedef struct
{
  ConnectionSetup *cs;
  GSource *source;
  DBusTimeout *timeout;
} TimeoutHandler;

static dbus_int32_t connection_slot = -1;
static dbus_int32_t server_slot = -1;

static ConnectionSetup*
connection_setup_new (GMainContext *context)
{
  ConnectionSetup *cs;

  cs = g_new0 (ConnectionSetup, 1);

  g_assert (context != NULL);
  
  cs->context = context;
  g_main_context_ref (cs->context);
  
  return cs;
}

static void
io_handler_source_destroyed (gpointer data)
{
  IOHandler *handler;

  handler = data;

  handler->cs->ios = g_slist_remove (handler->cs->ios, handler);

  if (handler->watch)
    dbus_watch_set_data (handler->watch, NULL, NULL);
  
  g_free (handler);
}

static void
io_handler_destroy_source (void *data)
{
  IOHandler *handler;

  handler = data;

  if (handler->source)
    {
      GSource *source = handler->source;
      handler->source = NULL;
      g_source_destroy (source);
    }
}

static void
io_handler_watch_freed (void *data)
{
  IOHandler *handler;

  handler = data;

  handler->watch = NULL;

  io_handler_destroy_source (handler);
}

static gboolean
io_handler_dispatch (GIOChannel   *source,
                     GIOCondition  condition,
                     gpointer      data)
{
  IOHandler *handler;
  guint dbus_condition = 0;
  DBusConnection *connection;

  handler = data;

  connection = handler->cs->connection;
  
  if (connection)
    dbus_connection_ref (connection);
  
  if (condition & G_IO_IN)
    dbus_condition |= DBUS_WATCH_READABLE;
  if (condition & G_IO_OUT)
    dbus_condition |= DBUS_WATCH_WRITABLE;
  if (condition & G_IO_ERR)
    dbus_condition |= DBUS_WATCH_ERROR;
  if (condition & G_IO_HUP)
    dbus_condition |= DBUS_WATCH_HANGUP;

  /* Note that we don't touch the handler after this, because
   * dbus may have disabled the watch and thus killed the
   * handler.
   */
  dbus_watch_handle (handler->watch, dbus_condition);
  handler = NULL;

  if (connection)
    {
      /* Dispatch messages */
      while (dbus_connection_dispatch (connection) == DBUS_DISPATCH_DATA_REMAINS)
        ;
      
      dbus_connection_unref (connection);
    }
  
  return TRUE;
}

static void
connection_setup_add_watch (ConnectionSetup *cs,
                            DBusWatch       *watch)
{
  guint flags;
  GIOCondition condition;
  GIOChannel *channel;
  IOHandler *handler;
  
  if (!dbus_watch_get_enabled (watch))
    return;
  
  g_assert (dbus_watch_get_data (watch) == NULL);
  
  flags = dbus_watch_get_flags (watch);

  condition = G_IO_ERR | G_IO_HUP;
  if (flags & DBUS_WATCH_READABLE)
    condition |= G_IO_IN;
  if (flags & DBUS_WATCH_WRITABLE)
    condition |= G_IO_OUT;

  handler = g_new0 (IOHandler, 1);
  handler->cs = cs;
  handler->watch = watch;
  
  channel = g_io_channel_unix_new (dbus_watch_get_fd (watch));
  
  handler->source = g_io_create_watch (channel, condition);
  g_source_set_callback (handler->source, (GSourceFunc) io_handler_dispatch, handler,
                         io_handler_source_destroyed);
  g_source_attach (handler->source, cs->context);

  cs->ios = g_slist_prepend (cs->ios, handler);
  
  dbus_watch_set_data (watch, handler, io_handler_watch_freed);
}

static void
connection_setup_remove_watch (ConnectionSetup *cs,
                               DBusWatch       *watch)
{
  IOHandler *handler;

  handler = dbus_watch_get_data (watch);

  if (handler == NULL)
    return;
  
  io_handler_destroy_source (handler);
}

static void
timeout_handler_source_destroyed (gpointer data)
{
  TimeoutHandler *handler;

  handler = data;

  handler->cs->timeouts = g_slist_remove (handler->cs->timeouts, handler);
  
  if (handler->timeout)
    dbus_timeout_set_data (handler->timeout, NULL, NULL);
  
  g_free (handler);
}

static void
timeout_handler_destroy_source (void *data)
{
  TimeoutHandler *handler;

  handler = data;

  if (handler->source)
    {
      GSource *source = handler->source;
      handler->source = NULL;
      g_source_destroy (source);
    }
}

static void
timeout_handler_timeout_freed (void *data)
{
  TimeoutHandler *handler;

  handler = data;

  handler->timeout = NULL;

  timeout_handler_destroy_source (handler);
}

static gboolean
timeout_handler_dispatch (gpointer      data)
{
  TimeoutHandler *handler;

  handler = data;

  dbus_timeout_handle (handler->timeout);
  
  return TRUE;
}

static void
connection_setup_add_timeout (ConnectionSetup *cs,
                              DBusTimeout     *timeout)
{
  TimeoutHandler *handler;
  
  if (!dbus_timeout_get_enabled (timeout))
    return;
  
  g_assert (dbus_timeout_get_data (timeout) == NULL);

  handler = g_new0 (TimeoutHandler, 1);
  handler->cs = cs;
  handler->timeout = timeout;

  handler->source = g_timeout_source_new (dbus_timeout_get_interval (timeout));
  g_source_set_callback (handler->source, timeout_handler_dispatch, handler,
                         timeout_handler_source_destroyed);
  g_source_attach (handler->source, handler->cs->context);

  cs->timeouts = g_slist_prepend (cs->timeouts, handler);

  dbus_timeout_set_data (timeout, handler, timeout_handler_timeout_freed);
}

static void
connection_setup_remove_timeout (ConnectionSetup *cs,
                                 DBusTimeout       *timeout)
{
  TimeoutHandler *handler;
  
  handler = dbus_timeout_get_data (timeout);

  if (handler == NULL)
    return;
  
  timeout_handler_destroy_source (handler);
}

static void
connection_setup_free (ConnectionSetup *cs)
{
  while (cs->ios)
    io_handler_destroy_source (cs->ios->data);

  while (cs->timeouts)
    timeout_handler_destroy_source (cs->timeouts->data);
  
  g_main_context_unref (cs->context);
  g_free (cs);
}

static dbus_bool_t
add_watch (DBusWatch *watch,
	   gpointer   data)
{
  ConnectionSetup *cs;

  cs = data;

  connection_setup_add_watch (cs, watch);
  
  return TRUE;
}

static void
remove_watch (DBusWatch *watch,
	      gpointer   data)
{
  ConnectionSetup *cs;

  cs = data;

  connection_setup_remove_watch (cs, watch);
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

static dbus_bool_t
add_timeout (DBusTimeout *timeout,
	     void        *data)
{
  ConnectionSetup *cs;

  cs = data;
  
  if (!dbus_timeout_get_enabled (timeout))
    return TRUE;

  connection_setup_add_timeout (cs, timeout);

  return TRUE;
}

static void
remove_timeout (DBusTimeout *timeout,
		void        *data)
{
  ConnectionSetup *cs;

  cs = data;

  connection_setup_remove_timeout (cs, timeout);
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
wakeup_main (void *data)
{
  ConnectionSetup *cs = data;

  g_main_context_wakeup (cs->context);
}


/* Move to a new context */
static ConnectionSetup*
connection_setup_new_from_old (GMainContext    *context,
                               ConnectionSetup *old)
{
  GSList *tmp;
  ConnectionSetup *cs;

  g_assert (old->context != context);
  
  cs = connection_setup_new (context);
  
  tmp = old->ios;
  while (tmp != NULL)
    {
      IOHandler *handler = tmp->data;

      connection_setup_add_watch (cs, handler->watch);
      
      tmp = tmp->next;
    }

  tmp = old->timeouts;
  while (tmp != NULL)
    {
      TimeoutHandler *handler = tmp->data;

      connection_setup_add_timeout (cs, handler->timeout);
      
      tmp = tmp->next;
    }

  return cs;
}

/** @} */ /* End of GLib bindings internals */

/** @addtogroup DBusGLib
 * @{
 */

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
  ConnectionSetup *old_setup;
  ConnectionSetup *cs;
  
  /* FIXME we never free the slot, so its refcount just keeps growing,
   * which is kind of broken.
   */
  dbus_connection_allocate_data_slot (&connection_slot);
  if (connection_slot < 0)
    goto nomem;

  if (context == NULL)
    context = g_main_context_default ();

  cs = NULL;
  
  old_setup = dbus_connection_get_data (connection, connection_slot);
  if (old_setup != NULL)
    {
      if (old_setup->context == context)
        return; /* nothing to do */

      cs = connection_setup_new_from_old (context, old_setup);
      
      /* Nuke the old setup */
      dbus_connection_set_data (connection, connection_slot, NULL, NULL);
      old_setup = NULL;
    }

  if (cs == NULL)
    cs = connection_setup_new (context);

  if (!dbus_connection_set_data (connection, connection_slot, cs,
                                 (DBusFreeFunction)connection_setup_free))
    goto nomem;

  cs->connection = connection;
  
  if (!dbus_connection_set_watch_functions (connection,
                                            add_watch,
                                            remove_watch,
                                            watch_toggled,
                                            cs, NULL))
    goto nomem;

  if (!dbus_connection_set_timeout_functions (connection,
                                              add_timeout,
                                              remove_timeout,
                                              timeout_toggled,
                                              cs, NULL))
    goto nomem;
    
  dbus_connection_set_wakeup_main_function (connection,
					    wakeup_main,
					    cs, NULL);
      
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
  ConnectionSetup *old_setup;
  ConnectionSetup *cs;
  
  /* FIXME we never free the slot, so its refcount just keeps growing,
   * which is kind of broken.
   */
  dbus_server_allocate_data_slot (&server_slot);
  if (server_slot < 0)
    goto nomem;

  if (context == NULL)
    context = g_main_context_default ();

  cs = NULL;
  
  old_setup = dbus_server_get_data (server, server_slot);
  if (old_setup != NULL)
    {
      if (old_setup->context == context)
        return; /* nothing to do */

      cs = connection_setup_new_from_old (context, old_setup);
      
      /* Nuke the old setup */
      dbus_server_set_data (server, server_slot, NULL, NULL);
      old_setup = NULL;
    }

  if (cs == NULL)
    cs = connection_setup_new (context);

  if (!dbus_server_set_data (server, server_slot, cs,
                             (DBusFreeFunction)connection_setup_free))
    goto nomem;
  
  if (!dbus_server_set_watch_functions (server,
                                        add_watch,
                                        remove_watch,
                                        watch_toggled,
                                        cs, NULL))
    goto nomem;

  if (!dbus_server_set_timeout_functions (server,
                                          add_timeout,
                                          remove_timeout,
                                          timeout_toggled,
                                          cs, NULL))
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
