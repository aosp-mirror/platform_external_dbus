/* -*- mode: C; c-file-style: "gnu" -*- */
/* test.c  unit test routines
 *
 * Copyright (C) 2003 Red Hat, Inc.
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

#ifdef DBUS_BUILD_TESTS
#include "test.h"
#include "loop.h"
#include <dbus/dbus-internals.h>

/* The "debug client" watch/timeout handlers don't dispatch messages,
 * as we manually pull them in order to verify them. This is why they
 * are different from the real handlers in connection.c
 */

static void
client_watch_callback (DBusWatch     *watch,
                           unsigned int   condition,
                           void          *data)
{
  DBusConnection *connection = data;

  dbus_connection_ref (connection);
  
  dbus_connection_handle_watch (connection, watch, condition);

  dbus_connection_unref (connection);
}

static dbus_bool_t
add_client_watch (DBusWatch      *watch,
                      DBusConnection *connection)
{
  return bus_loop_add_watch (watch, client_watch_callback, connection,
                             NULL);
}

static void
remove_client_watch (DBusWatch      *watch,
                         DBusConnection *connection)
{
  bus_loop_remove_watch (watch, client_watch_callback, connection);
}

static void
client_timeout_callback (DBusTimeout   *timeout,
                             void          *data)
{
  DBusConnection *connection = data;

  dbus_connection_ref (connection);
  
  dbus_timeout_handle (timeout);

  dbus_connection_unref (connection);
}

static dbus_bool_t
add_client_timeout (DBusTimeout    *timeout,
                        DBusConnection *connection)
{
  return bus_loop_add_timeout (timeout, client_timeout_callback, connection, NULL);
}

static void
remove_client_timeout (DBusTimeout    *timeout,
                           DBusConnection *connection)
{
  bus_loop_remove_timeout (timeout, client_timeout_callback, connection);
}

static DBusHandlerResult
client_disconnect_handler (DBusMessageHandler *handler,
                           DBusConnection     *connection,
                           DBusMessage        *message,
                           void               *user_data)
{
  dbus_connection_unref (connection);
  
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

dbus_bool_t
bus_setup_debug_client (DBusConnection *connection)
{
  DBusMessageHandler *disconnect_handler;
  const char *to_handle[] = { DBUS_MESSAGE_LOCAL_DISCONNECT };
  dbus_bool_t retval;
  
  disconnect_handler = dbus_message_handler_new (client_disconnect_handler,
                                                 NULL, NULL);

  if (disconnect_handler == NULL)
    return FALSE;

  if (!dbus_connection_register_handler (connection,
                                         disconnect_handler,
                                         to_handle,
                                         _DBUS_N_ELEMENTS (to_handle)))
    {
      dbus_message_handler_unref (disconnect_handler);
      return FALSE;
    }

  retval = FALSE;
  
  if (!dbus_connection_set_watch_functions (connection,
                                            (DBusAddWatchFunction) add_client_watch,
                                            (DBusRemoveWatchFunction) remove_client_watch,
                                            connection,
                                            NULL))
    goto out;
      
  if (!dbus_connection_set_timeout_functions (connection,
                                              (DBusAddTimeoutFunction) add_client_timeout,
                                              (DBusRemoveTimeoutFunction) remove_client_timeout,
                                              connection, NULL))
    goto out;

  retval = TRUE;
  
 out:
  if (!retval)
    {
      dbus_connection_unregister_handler (connection,
                                          disconnect_handler,
                                          to_handle,
                                          _DBUS_N_ELEMENTS (to_handle));
      
      dbus_connection_set_watch_functions (connection,
                                           NULL, NULL, NULL, NULL);
      dbus_connection_set_timeout_functions (connection,
                                             NULL, NULL, NULL, NULL);
    }
  
  dbus_message_handler_unref (disconnect_handler);
  
  return retval;
}
#endif
