/* -*- mode: C; c-file-style: "gnu" -*- */
/* bus.c  message bus context object
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

#include "bus.h"
#include "loop.h"
#include "activation.h"
#include "connection.h"
#include "services.h"
#include "utils.h"
#include <dbus/dbus-internals.h>

struct BusContext
{
  int refcount;
  char *address;  
  DBusServer *server;
  BusConnections *connections;
  BusActivation *activation;
  BusRegistry *registry;
};

static dbus_bool_t
server_watch_callback (DBusWatch     *watch,
                       unsigned int   condition,
                       void          *data)
{
  BusContext *context = data;

  return dbus_server_handle_watch (context->server, watch, condition);
}

static dbus_bool_t
add_server_watch (DBusWatch  *watch,
                  BusContext *context)
{
  return bus_loop_add_watch (watch, server_watch_callback, context,
                             NULL);
}

static void
remove_server_watch (DBusWatch  *watch,
                     BusContext *context)
{
  bus_loop_remove_watch (watch, server_watch_callback, context);
}


static void
server_timeout_callback (DBusTimeout   *timeout,
                         void          *data)
{
  /* can return FALSE on OOM but we just let it fire again later */
  dbus_timeout_handle (timeout);
}

static dbus_bool_t
add_server_timeout (DBusTimeout *timeout,
                    BusContext  *context)
{
  return bus_loop_add_timeout (timeout, server_timeout_callback, context, NULL);
}

static void
remove_server_timeout (DBusTimeout *timeout,
                       BusContext  *context)
{
  bus_loop_remove_timeout (timeout, server_timeout_callback, context);
}

static void
new_connection_callback (DBusServer     *server,
                         DBusConnection *new_connection,
                         void           *data)
{
  BusContext *context = data;
  
  if (!bus_connections_setup_connection (context->connections, new_connection))
    {
      _dbus_verbose ("No memory to setup new connection\n");

      /* if we don't do this, it will get unref'd without
       * being disconnected... kind of strange really
       * that we have to do this, people won't get it right
       * in general.
       */
      dbus_connection_disconnect (new_connection);
    }
  
  /* on OOM, we won't have ref'd the connection so it will die. */
}

BusContext*
bus_context_new (const char  *address,
                 const char **service_dirs,
                 DBusError   *error)
{
  BusContext *context;
  DBusResultCode result;
  
  context = dbus_new0 (BusContext, 1);
  if (context == NULL)
    {
      BUS_SET_OOM (error);
      return NULL;
    }
  
  context->refcount = 1;

  context->address = _dbus_strdup (address);
  if (context->address == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }
  
  context->server = dbus_server_listen (address, &result);
  if (context->server == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Failed to start server on %s: %s\n",
                      address, dbus_result_to_string (result));
      goto failed;
    }

  context->activation = bus_activation_new (context, address, service_dirs,
                                            error);
  if (context->activation == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto failed;
    }

  context->connections = bus_connections_new (context);
  if (context->connections == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  context->registry = bus_registry_new (context);
  if (context->registry == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }
  
  dbus_server_set_new_connection_function (context->server,
                                           new_connection_callback,
                                           context, NULL);
  
  if (!dbus_server_set_watch_functions (context->server,
                                        (DBusAddWatchFunction) add_server_watch,
                                        (DBusRemoveWatchFunction) remove_server_watch,
                                        NULL,
                                        context,
                                        NULL))
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  if (!dbus_server_set_timeout_functions (context->server,
                                          (DBusAddTimeoutFunction) add_server_timeout,
                                          (DBusRemoveTimeoutFunction) remove_server_timeout,
                                          NULL,
                                          context, NULL))
    {
      BUS_SET_OOM (error);
      goto failed;
    }
  
  return context;
  
 failed:
  bus_context_unref (context);
  return NULL;
}

void
bus_context_shutdown (BusContext  *context)
{
  if (context->server == NULL ||
      !dbus_server_get_is_connected (context->server))
    return;
  
  if (!dbus_server_set_watch_functions (context->server,
                                        NULL, NULL, NULL,
                                        context,
                                        NULL))
    _dbus_assert_not_reached ("setting watch functions to NULL failed");
  
  if (!dbus_server_set_timeout_functions (context->server,
                                          NULL, NULL, NULL,
                                          context,
                                          NULL))
    _dbus_assert_not_reached ("setting timeout functions to NULL failed");
  
  dbus_server_disconnect (context->server);
}

void
bus_context_ref (BusContext *context)
{
  _dbus_assert (context->refcount > 0);
  context->refcount += 1;
}

void
bus_context_unref (BusContext *context)
{
  _dbus_assert (context->refcount > 0);
  context->refcount -= 1;

  if (context->refcount == 0)
    {
      _dbus_verbose ("Finalizing bus context %p\n", context);
      
      bus_context_shutdown (context);

      if (context->connections)
        {
          bus_connections_unref (context->connections);
          context->connections = NULL;
        }
      
      if (context->registry)
        {
          bus_registry_unref (context->registry);
          context->registry = NULL;
        }
      
      if (context->activation)
        {
          bus_activation_unref (context->activation);
          context->activation = NULL;
        }
      
      if (context->server)
        {
          dbus_server_unref (context->server);
          context->server = NULL;
        }
      
      dbus_free (context->address);
      dbus_free (context);
    }
}

BusRegistry*
bus_context_get_registry (BusContext  *context)
{
  return context->registry;
}

BusConnections*
bus_context_get_connections (BusContext  *context)
{
  return context->connections;
}

BusActivation*
bus_context_get_activation (BusContext  *context)
{
  return context->activation;
}
