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
#include <dbus/dbus-internals.h>
#include <dbus/dbus-list.h>

/* The "debug client" watch/timeout handlers don't dispatch messages,
 * as we manually pull them in order to verify them. This is why they
 * are different from the real handlers in connection.c
 */
static DBusList *clients = NULL;
static DBusLoop *client_loop = NULL;

static dbus_bool_t
client_watch_callback (DBusWatch     *watch,
                       unsigned int   condition,
                       void          *data)
{
  /* FIXME this can be done in dbus-mainloop.c
   * if the code in activation.c for the babysitter
   * watch handler is fixed.
   */
 
  return dbus_watch_handle (watch, condition);
}

static dbus_bool_t
add_client_watch (DBusWatch      *watch,
                  void           *data)
{
  DBusConnection *connection = data;
  
  return _dbus_loop_add_watch (client_loop,
                               watch, client_watch_callback, connection,
                               NULL);
}

static void
remove_client_watch (DBusWatch      *watch,
                     void           *data)
{
  DBusConnection *connection = data;
  
  _dbus_loop_remove_watch (client_loop,
                           watch, client_watch_callback, connection);
}

static void
client_timeout_callback (DBusTimeout   *timeout,
                         void          *data)
{
  DBusConnection *connection = data;

  dbus_connection_ref (connection);

  /* can return FALSE on OOM but we just let it fire again later */
  dbus_timeout_handle (timeout);

  dbus_connection_unref (connection);
}

static dbus_bool_t
add_client_timeout (DBusTimeout    *timeout,
                    void           *data)
{
  DBusConnection *connection = data;
  
  return _dbus_loop_add_timeout (client_loop, timeout, client_timeout_callback, connection, NULL);
}

static void
remove_client_timeout (DBusTimeout    *timeout,
                       void           *data)
{
  DBusConnection *connection = data;
  
  _dbus_loop_remove_timeout (client_loop, timeout, client_timeout_callback, connection);
}

static DBusHandlerResult
client_disconnect_handler (DBusMessageHandler *handler,
                           DBusConnection     *connection,
                           DBusMessage        *message,
                           void               *user_data)
{
  if (!dbus_message_has_name (message,
                              DBUS_MESSAGE_LOCAL_DISCONNECT))
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    
  _dbus_verbose ("Removing client %p in disconnect handler\n",
                 connection);
  
  _dbus_list_remove (&clients, connection);

  dbus_connection_unref (connection);
  
  if (clients == NULL)
    {
      _dbus_loop_unref (client_loop);
      client_loop = NULL;
    }
  
  return DBUS_HANDLER_RESULT_HANDLED;
}

static dbus_int32_t handler_slot = -1;

static void
free_handler (void *data)
{
  DBusMessageHandler *handler = data;

  dbus_message_handler_unref (handler);
  dbus_connection_free_data_slot (&handler_slot);
}

dbus_bool_t
bus_setup_debug_client (DBusConnection *connection)
{
  DBusMessageHandler *disconnect_handler;
  dbus_bool_t retval;
  
  disconnect_handler = dbus_message_handler_new (client_disconnect_handler,
                                                 NULL, NULL);

  if (disconnect_handler == NULL)
    return FALSE;

  if (!dbus_connection_add_filter (connection,
                                   disconnect_handler))
    {
      dbus_message_handler_unref (disconnect_handler);
      return FALSE;
    }

  retval = FALSE;

  if (client_loop == NULL)
    {
      client_loop = _dbus_loop_new ();
      if (client_loop == NULL)
        goto out;
    }
  
  if (!dbus_connection_set_watch_functions (connection,
                                            add_client_watch,
                                            remove_client_watch,
                                            NULL,
                                            connection,
                                            NULL))
    goto out;
      
  if (!dbus_connection_set_timeout_functions (connection,
                                              add_client_timeout,
                                              remove_client_timeout,
                                              NULL,
                                              connection, NULL))
    goto out;

  if (!_dbus_list_append (&clients, connection))
    goto out;

  if (!dbus_connection_allocate_data_slot (&handler_slot))
    goto out;

  /* Set up handler to be destroyed */  
  if (!dbus_connection_set_data (connection, handler_slot,
                                 disconnect_handler,
                                 free_handler))
    {
      dbus_connection_free_data_slot (&handler_slot);
      goto out;
    }
  
  retval = TRUE;
  
 out:
  if (!retval)
    {
      dbus_message_handler_unref (disconnect_handler); /* unregisters it */
      
      dbus_connection_set_watch_functions (connection,
                                           NULL, NULL, NULL, NULL, NULL);
      dbus_connection_set_timeout_functions (connection,
                                             NULL, NULL, NULL, NULL, NULL);

      _dbus_list_remove_last (&clients, connection);

      if (clients == NULL)
        {
          _dbus_loop_unref (client_loop);
          client_loop = NULL;
        }
    }
      
  return retval;
}

void
bus_test_clients_foreach (BusConnectionForeachFunction  function,
                          void                         *data)
{
  DBusList *link;
  
  link = _dbus_list_get_first_link (&clients);
  while (link != NULL)
    {
      DBusConnection *connection = link->data;
      DBusList *next = _dbus_list_get_next_link (&clients, link);

      if (!(* function) (connection, data))
        break;
      
      link = next;
    }
}

dbus_bool_t
bus_test_client_listed (DBusConnection *connection)
{
  DBusList *link;
  
  link = _dbus_list_get_first_link (&clients);
  while (link != NULL)
    {
      DBusConnection *c = link->data;
      DBusList *next = _dbus_list_get_next_link (&clients, link);

      if (c == connection)
        return TRUE;
      
      link = next;
    }

  return FALSE;
}

void
bus_test_run_clients_loop (dbus_bool_t block_once)
{
  if (client_loop == NULL)
    return;

  /* dispatch before we block so pending dispatches
   * won't make our block return early
   */
  _dbus_loop_dispatch (client_loop);
  
  /* Do one blocking wait, since we're expecting data */
  if (block_once)
    {
      _dbus_verbose ("---> blocking on \"client side\"\n");
      _dbus_loop_iterate (client_loop, TRUE);
    }

  /* Then mop everything up */
  while (_dbus_loop_iterate (client_loop, FALSE))
    ;
}

void
bus_test_run_bus_loop (BusContext *context,
                       dbus_bool_t block_once)
{
  /* dispatch before we block so pending dispatches
   * won't make our block return early
   */
  _dbus_loop_dispatch (bus_context_get_loop (context));
  
  /* Do one blocking wait, since we're expecting data */
  if (block_once)
    {
      _dbus_verbose ("---> blocking on \"server side\"\n");
      _dbus_loop_iterate (bus_context_get_loop (context), TRUE);
    }

  /* Then mop everything up */
  while (_dbus_loop_iterate (bus_context_get_loop (context), FALSE))
    ;
}

void
bus_test_run_everything (BusContext *context)
{
  while (_dbus_loop_iterate (bus_context_get_loop (context), FALSE) ||
         (client_loop == NULL || _dbus_loop_iterate (client_loop, FALSE)))
    ;
}

BusContext*
bus_context_new_test (const DBusString *test_data_dir,
                      const char       *filename)
{
  DBusError error;
  DBusString config_file;
  DBusString relative;
  BusContext *context;
  
  if (!_dbus_string_init (&config_file))
    {
      _dbus_warn ("No memory\n");
      return NULL;
    }

  if (!_dbus_string_copy (test_data_dir, 0,
                          &config_file, 0))
    {
      _dbus_warn ("No memory\n");
      _dbus_string_free (&config_file);
      return NULL;
    }

  _dbus_string_init_const (&relative, filename);

  if (!_dbus_concat_dir_and_file (&config_file, &relative))
    {
      _dbus_warn ("No memory\n");
      _dbus_string_free (&config_file);
      return NULL;
    }
  
  dbus_error_init (&error);
  context = bus_context_new (&config_file, FALSE, -1, -1, &error);
  if (context == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (&error);
      
      _dbus_warn ("Failed to create debug bus context from configuration file %s: %s\n",
                  filename, error.message);

      dbus_error_free (&error);
      
      _dbus_string_free (&config_file);
      
      return NULL;
    }

  _dbus_string_free (&config_file);
  
  return context;
}

#endif
