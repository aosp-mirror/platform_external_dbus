/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gcall.c convenience routines for calling methods, etc.
 *
 * Copyright (C) 2003  Red Hat, Inc.
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
#include "dbus-gproxy.h"

/**
 * @addtogroup DBusGLibInternals
 *
 * @{
 */

struct DBusGProxy
{
  int refcount;
  DBusConnection *connection;
  char *service;
  char *interface;
  DBusObjectID object_id;
};

static DBusGProxy*
_dbus_gproxy_new (DBusConnection *connection)
{
  DBusGProxy *proxy;

  proxy = g_new0 (DBusGProxy, 1);

  proxy->refcount = 1;
  proxy->connection = connection;
  dbus_connection_ref (connection);

  return proxy;
}

/** @} End of DBusGLibInternals */

/** @addtogroup DBusGLib
 * @{
 */

/**
 * Creates a new proxy for a remote interface. Method calls and signal
 * connections over this proxy will go to the service owner; the
 * service owner is expected to support the given interface name. THE
 * SERVICE OWNER MAY CHANGE OVER TIME, for example between two
 * different method calls. If you need a fixed owner, you need to
 * request the current owner and bind a proxy to that rather than to
 * the generic service name; see dbus_gproxy_new_for_service_owner().
 *
 * A service-associated proxy only makes sense with a message bus,
 * not for app-to-app direct dbus connections.
 *
 * @param connection the connection to the remote bus or app
 * @param service_name name of the service on the message bus
 * @param interface_name name of the interface to call methods on
 * @returns new proxy object
 */
DBusGProxy*
dbus_gproxy_new_for_service (DBusConnection *connection,
                             const char     *service_name,
                             const char     *interface_name)
{
  DBusGProxy *proxy;

  g_return_val_if_fail (connection != NULL, NULL);
  g_return_val_if_fail (service_name != NULL, NULL);
  g_return_val_if_fail (interface_name != NULL, NULL);
  
  proxy = _dbus_gproxy_new (connection);

  proxy->service = g_strdup (service_name);
  proxy->interface = g_strdup (interface_name);

  return proxy;
}

/**
 * Increment reference count on proxy object.
 *
 * @param proxy the proxy
 */
void
dbus_gproxy_ref (DBusGProxy *proxy)
{
  g_return_if_fail (proxy != NULL);

  proxy->refcount += 1;
}

/**
 * Decrement reference count on proxy object.
 *
 * @param proxy the proxy
 */
void
dbus_gproxy_unref (DBusGProxy *proxy)
{
  g_return_if_fail (proxy != NULL);

  proxy->refcount -= 1;
  if (proxy->refcount == 0)
    {
      dbus_connection_unref (proxy->connection);
      g_free (proxy->interface);
      g_free (proxy->service);
      g_free (proxy);
    }
}

/**
 * Invokes a method on a remote interface. This function does not
 * block; instead it returns an opaque #DBusGPendingCall object that
 * tracks the pending call.  The method call will not be sent over the
 * wire until the application returns to the main loop, or blocks in
 * dbus_connection_flush() to write out pending data.  The call will
 * be completed after a timeout, or when a reply is received.
 *
 * @param proxy a proxy for a remote interface
 * @param method the name of the method to invoke
 * @param first_arg_type type of the first argument
 *
 * @returns opaque pending call object
 * 
 */
DBusGPendingCall*
dbus_gproxy_begin_call (DBusGProxy *proxy,
                        const char *method,
                        int         first_arg_type,
                        ...)
{
  
  
}

/** @} End of DBusGLib public */

#ifdef DBUS_BUILD_TESTS

/**
 * @ingroup DBusGLibInternals
 * Unit test for GLib proxy functions
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_gproxy_test (void)
{

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
