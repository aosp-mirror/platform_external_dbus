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
#include "dbus-glib.h"

/**
 * @addtogroup DBusGLibInternals
 *
 * @{
 */

/**
 * DBusGProxyManager's primary task is to route signals to the proxies
 * those signals are emitted on. In order to do this it also has to
 * track the owners of the services proxies are bound to.
 */
typedef struct
{
  GStaticMutex lock; /**< Thread lock */
  int refcount;      /**< Reference count */
  DBusConnection *connection; /**< Connection we're associated with. */
  

} DBusGProxyManager;

/** Lock the DBusGProxyManager */
#define LOCK_MANAGER(mgr)   (g_static_mutex_lock (&(mgr)->lock))
/** Unlock the DBusGProxyManager */
#define UNLOCK_MANAGER(mgr) (g_static_mutex_unlock (&(mgr)->lock))

static int gproxy_manager_slot = -1;

/* Lock controlling get/set manager as data on each connection */
static GStaticMutex connection_gproxy_lock = G_STATIC_MUTEX_INIT;

static void dbus_gproxy_manager_ref (DBusGProxyManager *manager);

static DBusGProxyManager*
dbus_gproxy_manager_get (DBusConnection *connection)
{
  DBusGProxyManager *manager;

  dbus_connection_allocate_data_slot (&gproxy_manager_slot);
  if (gproxy_manager_slot < 0)
    g_error ("out of memory");
  
  g_static_mutex_lock (&connection_gproxy_lock);
  
  manager = dbus_connection_get_data (connection, gproxy_manager_slot);
  if (manager != NULL)
    {
      dbus_connection_free_data_slot (&gproxy_manager_slot);
      dbus_gproxy_manager_ref (manager);
      g_static_mutex_unlock (&connection_gproxy_lock);
      return manager;
    }
  
  manager = g_new0 (DBusGProxyManager, 1);

  manager->refcount = 1;
  manager->connection = connection;
  
  g_static_mutex_init (&manager->lock);

  /* Proxy managers keep the connection alive, which means that
   * DBusGProxy indirectly does. To free a connection you have to free
   * all the proxies referring to it.
   */
  dbus_connection_ref (manager->connection);

  dbus_connection_set_data (connection, gproxy_manager_slot,
                            manager, NULL);

  g_static_mutex_unlock (&connection_gproxy_lock);
  
  return manager;
}

static void
dbus_gproxy_manager_ref (DBusGProxyManager *manager)
{
  g_assert (manager != NULL);
  g_assert (manager->refcount > 0);

  LOCK_MANAGER (manager);
  
  manager->refcount += 1;

  UNLOCK_MANAGER (manager);
}

static void
dbus_gproxy_manager_unref (DBusGProxyManager *manager)
{
  g_assert (manager != NULL);
  g_assert (manager->refcount > 0);

  LOCK_MANAGER (manager);
  manager->refcount -= 1;
  if (manager->refcount == 0)
    {
      UNLOCK_MANAGER (manager);
      
      g_static_mutex_free (&manager->lock);

      g_static_mutex_lock (&connection_gproxy_lock);
      
      dbus_connection_set_data (manager->connection,
                                gproxy_manager_slot,
                                NULL, NULL);

      g_static_mutex_unlock (&connection_gproxy_lock);
      
      dbus_connection_unref (manager->connection);
      g_free (manager);

      dbus_connection_free_data_slot (&gproxy_manager_slot);
    }
  else
    {
      UNLOCK_MANAGER (manager);
    }
}

/**
 * Internals of DBusGProxy
 */
struct DBusGProxy
{
  GStaticMutex lock; /**< Thread lock */
  int refcount;      /**< Reference count */
  DBusGProxyManager *manager; /**< Proxy manager */
  char *service;             /**< Service messages go to or NULL */
  char *path;                /**< Path messages go to or NULL */
  char *interface;           /**< Interface messages go to or NULL */
};

/** Lock the DBusGProxy */
#define LOCK_PROXY(proxy)   (g_static_mutex_lock (&(proxy)->lock))
/** Unlock the DBusGProxy */
#define UNLOCK_PROXY(proxy) (g_static_mutex_unlock (&(proxy)->lock))

static DBusGProxy*
_dbus_gproxy_new (DBusConnection *connection)
{
  DBusGProxy *proxy;

  proxy = g_new0 (DBusGProxy, 1);
  
  proxy->refcount = 1;
  proxy->manager = dbus_gproxy_manager_get (connection);

  g_static_mutex_init (&proxy->lock);
  
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
 * @param path_name name of the object inside the service to call methods on
 * @param interface_name name of the interface to call methods on
 * @returns new proxy object
 */
DBusGProxy*
dbus_gproxy_new_for_service (DBusConnection *connection,
                             const char     *service_name,
                             const char     *path_name,
                             const char     *interface_name)
{
  DBusGProxy *proxy;

  g_return_val_if_fail (connection != NULL, NULL);
  g_return_val_if_fail (service_name != NULL, NULL);
  g_return_val_if_fail (path_name != NULL, NULL);
  g_return_val_if_fail (interface_name != NULL, NULL);
  
  proxy = _dbus_gproxy_new (connection);

  proxy->service = g_strdup (service_name);
  proxy->path = g_strdup (path_name);
  proxy->interface = g_strdup (interface_name);

  return proxy;
}

/**
 * Increment reference count on proxy object.
 *
 * @todo use GAtomic to avoid locking
 * 
 * @param proxy the proxy
 */
void
dbus_gproxy_ref (DBusGProxy *proxy)
{
  g_return_if_fail (proxy != NULL);

  LOCK_PROXY (proxy);
  
  proxy->refcount += 1;

  UNLOCK_PROXY (proxy);
}

/**
 * Decrement reference count on proxy object.
 * 
 * @todo use GAtomic to avoid locking
 *
 * @param proxy the proxy
 */
void
dbus_gproxy_unref (DBusGProxy *proxy)
{
  g_return_if_fail (proxy != NULL);

  LOCK_PROXY (proxy);
  
  proxy->refcount -= 1;  
  
  if (proxy->refcount == 0)
    {
      UNLOCK_PROXY (proxy);
      
      dbus_gproxy_manager_unref (proxy->manager);
      g_free (proxy->service);
      g_free (proxy->path);
      g_free (proxy->interface);
      g_static_mutex_free (&proxy->lock);
      g_free (proxy);
    }
  else
    {
      UNLOCK_PROXY (proxy);
    }
}

/**
 * Invokes a method on a remote interface. This function does not
 * block; instead it returns an opaque #DBusPendingCall object that
 * tracks the pending call.  The method call will not be sent over the
 * wire until the application returns to the main loop, or blocks in
 * dbus_connection_flush() to write out pending data.  The call will
 * be completed after a timeout, or when a reply is received.
 * To collect the results of the call (which may be an error,
 * or a reply), use dbus_gproxy_end_call().
 *
 * @todo this particular function shouldn't die on out of memory,
 * since you should be able to do a call with large arguments.
 * 
 * @param proxy a proxy for a remote interface
 * @param method the name of the method to invoke
 * @param first_arg_type type of the first argument
 *
 * @returns opaque pending call object
 *  */
DBusPendingCall*
dbus_gproxy_begin_call (DBusGProxy *proxy,
                        const char *method,
                        int         first_arg_type,
                        ...)
{
  DBusPendingCall *pending;
  DBusMessage *message;
  va_list args;
  
  g_return_val_if_fail (proxy != NULL, NULL);
  LOCK_PROXY (proxy);

  message = dbus_message_new_method_call (proxy->service,
                                          proxy->path,
                                          proxy->interface,
                                          method);
  if (message == NULL)
    goto oom;

  va_start (args, first_arg_type);
  if (!dbus_message_append_args_valist (message, first_arg_type,
                                        args))
    goto oom;
  va_end (args);

  if (!dbus_connection_send_with_reply (proxy->manager->connection,
                                        message,
                                        &pending,
                                        -1))
    goto oom;
  
  UNLOCK_PROXY (proxy);

  return pending;

 oom:
  /* FIXME we should create a pending call that's
   * immediately completed with an error status without
   * ever going on the wire.
   */
  
  g_error ("Out of memory");
  return NULL;
}

/**
 * Collects the results of a method call. The method call was normally
 * initiated with dbus_gproxy_end_call(). This function will block if
 * the results haven't yet been received; use
 * dbus_pending_call_set_notify() to be notified asynchronously that a
 * pending call has been completed. Use
 * dbus_pending_call_get_completed() to check whether a call has been
 * completed. If it's completed, it will not block.
 *
 * If the call results in an error, the error is set as normal for
 * GError and the function returns #FALSE.
 *
 * Otherwise, the "out" parameters and return value of the
 * method are stored in the provided varargs list.
 * The list should be terminated with DBUS_TYPE_INVALID.
 *
 * This function doesn't affect the reference count of the
 * #DBusPendingCall, the caller of dbus_gproxy_begin_call() still owns
 * a reference.
 *
 * @param proxy a proxy for a remote interface
 * @param pending the pending call from dbus_gproxy_begin_call()
 * @param error return location for an error
 * @param first_arg_type type of first "out" argument
 * @returns #FALSE if an error is set */
gboolean
dbus_gproxy_end_call (DBusGProxy          *proxy,
                      DBusPendingCall     *pending,
                      GError             **error,
                      int                  first_arg_type,
                      ...)
{
  DBusMessage *message;
  va_list args;
  DBusError derror;
  
  g_return_val_if_fail (proxy != NULL, FALSE);
  g_return_val_if_fail (pending != NULL, FALSE);
  
  LOCK_PROXY (proxy);

  dbus_pending_call_block (pending);
  message = dbus_pending_call_get_reply (pending);

  g_assert (message != NULL);

  dbus_error_init (&derror);
  va_start (args, first_arg_type);
  if (!dbus_message_get_args_valist (message, &derror, first_arg_type, args))
    {
      va_end (args);
      goto error;
    }
  va_end (args);

  UNLOCK_PROXY (proxy);

  return TRUE;

 error:
  dbus_set_g_error (error, &derror);
  dbus_error_free (&derror);
  return FALSE;
}

/**
 * Sends a message to the interface we're proxying for.  Does not
 * block or wait for a reply. The message is only actually written out
 * when you return to the main loop or block in
 * dbus_connection_flush().
 *
 * The message is modified to be addressed to the target interface.
 * That is, a destination service field or whatever is needed will be
 * added to the message. The basic point of this function is to add
 * the necessary header fields, otherwise it's equivalent to
 * dbus_connection_send().
 *
 * This function adds a reference to the message, so the caller
 * still owns its original reference.
 * 
 * @param proxy a proxy for a remote interface
 * @param message the message to address and send
 * @param client_serial return location for message's serial, or #NULL */
void
dbus_gproxy_send (DBusGProxy          *proxy,
                  DBusMessage         *message,
                  dbus_uint32_t       *client_serial)
{
  g_return_if_fail (proxy != NULL);
  LOCK_PROXY (proxy);
  
  if (proxy->service)
    {
      if (!dbus_message_set_destination (message, proxy->service))
        g_error ("Out of memory");
    }
  if (proxy->path)
    {
      if (!dbus_message_set_path (message, proxy->path))
        g_error ("Out of memory");
    }
  if (proxy->interface)
    {
      if (!dbus_message_set_interface (message, proxy->interface))
        g_error ("Out of memory");
    }
  
  if (!dbus_connection_send (proxy->manager->connection, message, client_serial))
    g_error ("Out of memory\n");

  UNLOCK_PROXY (proxy);
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
