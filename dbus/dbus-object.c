/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-object.c  Objects
 *
 * Copyright (C) 2003  Red Hat Inc.
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
#include "dbus-internals.h"
#include "dbus-object.h"

/**
 * @defgroup DBusCallbackObjectInternals DBusCallbackObject implementation details
 * @ingroup DBusInternals
 * @brief DBusCallbackObject private implementation details.
 *
 * The guts of DBusCallbackObject and its methods.
 *
 * @{
 */

_DBUS_DEFINE_GLOBAL_LOCK (callback_object);

/**
 * @brief Internals of DBusCallbackObject
 *
 * Object that can send and receive messages.
 */
struct DBusCallbackObject
{
  DBusAtomic                refcount;             /**< reference count */
  DBusObjectMessageFunction function;             /**< callback function */
  void                     *user_data;            /**< user data for function */
  DBusFreeFunction          free_user_data;       /**< free the user data */
};

static void
callback_object_registered (DBusObjectInfo *info)
{
  DBusCallbackObject *callback = info->object_impl;

  dbus_callback_object_ref (callback);
}

static void
callback_object_unregistered (DBusObjectInfo *info)
{
  DBusCallbackObject *callback = info->object_impl;

  dbus_callback_object_unref (callback);
}

static void
callback_object_message (DBusObjectInfo *info,
                         DBusMessage    *message)
{
  DBusCallbackObject *callback = info->object_impl;
  
  if (callback->function)
    (* callback->function) (info, message);
}

/** @} */

/**
 * @defgroup DBusObject DBusObjectInfo, DBusObjectVTable, DBusCallbackObject
 * @ingroup  DBus
 * @brief support for object instances
 *
 * Behind each DBusConnection are object instances. An object instance
 * may be a GObject (using GLib), a QObject (using Qt), a built-in
 * object type called DBusCallbackObject, or any other representation
 * of an object; it's even permissible to have an object that's simply
 * an integer value or a pointer to a struct.
 *
 * Objects are registered with one or more DBusConnection. Registered
 * objects receive an object ID, represented by the DBusObjectID type.
 * Object IDs can be passed over a DBusConnection and used by the
 * remote application to refer to objects. Remote applications can
 * also refer to objects by dynamically locating objects that support
 * a particular interface.
 *
 * To define an object, you simply provide three callbacks: one to be
 * called when the object is registered with a new connection, one
 * to be called when the object is unregistered, and one to be called
 * when the object receives a message from the peer on the other end
 * of the DBusConnection. The three callbacks are specified in a
 * DBusObjectVTable struct.
 *
 * The DBusObjectInfo struct is used to pass the object pointer
 * (object_impl), connection, and object ID to each of the callbacks
 * in the virtual table. This struct should be treated as read-only.
 *
 * DBusCallbackObject is provided for convenience as a way to
 * implement an object quickly by writing only one callback function,
 * the callback that processes messages. To use DBusCallbackObject,
 * simply create one, then call dbus_connection_register_object()
 * passing in the provided DBusObjectVTable
 * dbus_callback_object_vtable. This is the simplest possible object;
 * it simply contains a function to be called whenever a message is
 * received.
 *
 * The DBusCallbackObject will be strong-referenced by the
 * DBusConnection, so may be unreferenced once it's registered, and
 * will go away either on unregistration or when the connection is
 * freed.
 *
 * One DBusCallbackObject may be registered with any number of
 * DBusConnection.
 * 
 * @{
 */

/**
 * @typedef DBusCallbackObject
 *
 * Opaque data type representing a callback object.
 */

static const DBusObjectVTable callback_object_vtable = {
  callback_object_registered,
  callback_object_unregistered,
  callback_object_message,
  NULL, NULL, NULL
};

/**
 * Virtual table for a DBusCallbackObject, used to register the
 * callback object with dbus_connection_register_object().
 */
const DBusObjectVTable* dbus_callback_object_vtable = &callback_object_vtable;

/**
 * Creates a new callback object. The callback function
 * may be #NULL for a no-op callback or a callback to
 * be assigned a function later.
 *
 * Use dbus_connection_register_object() along with
 * dbus_callback_object_vtable to register the callback object with
 * one or more connections.  Each connection will add a reference to
 * the callback object, so once it's registered it may be unreferenced
 * with dbus_callback_object_unref().
 *
 * @param function function to call to handle a message
 * @param user_data data to pass to the function
 * @param free_user_data function to call to free the user data
 * @returns a new DBusCallbackObject or #NULL if no memory.
 */
DBusCallbackObject*
dbus_callback_object_new (DBusObjectMessageFunction   function,
                          void                       *user_data,
                          DBusFreeFunction            free_user_data)
{
  DBusCallbackObject *callback;

  callback = dbus_new0 (DBusCallbackObject, 1);
  if (callback == NULL)
    return NULL;

  callback->refcount.value = 1;
  callback->function = function;
  callback->user_data = user_data;
  callback->free_user_data = free_user_data;

  return callback;
}

/**
 * Increments the reference count on a callback object.
 *
 * @param callback the callback
 */
void
dbus_callback_object_ref (DBusCallbackObject *callback)
{
  _dbus_return_if_fail (callback != NULL);

  _dbus_atomic_inc (&callback->refcount);
}


/**
 * Decrements the reference count on a callback object,
 * freeing the callback if the count reaches 0.
 *
 * @param callback the callback
 */
void
dbus_callback_object_unref (DBusCallbackObject *callback)
{
  dbus_bool_t last_unref;

  _dbus_return_if_fail (callback != NULL);

  last_unref = (_dbus_atomic_dec (&callback->refcount) == 1);

  if (last_unref)
    {
      if (callback->free_user_data)
        (* callback->free_user_data) (callback->user_data);

      dbus_free (callback);
    }
}

/**
 * Gets the user data for the callback.
 *
 * @param callback the callback
 * @returns the user data
 */
void*
dbus_callback_object_get_data (DBusCallbackObject *callback)
{
  void* user_data;

  _dbus_return_val_if_fail (callback != NULL, NULL);

  _DBUS_LOCK (callback_object);
  user_data = callback->user_data;
  _DBUS_UNLOCK (callback_object);
  return user_data;
}


/**
 * Sets the user data for the callback. Frees any previously-existing
 * user data with the previous free_user_data function.
 *
 * @param callback the callback
 * @param user_data the user data
 * @param free_user_data free function for the data
 */
void
dbus_callback_object_set_data (DBusCallbackObject         *callback,
                               void                       *user_data,
                               DBusFreeFunction            free_user_data)
{
  DBusFreeFunction old_free_func;
  void *old_user_data;

  _dbus_return_if_fail (callback != NULL);

  _DBUS_LOCK (callback_object);
  old_free_func = callback->free_user_data;
  old_user_data = callback->user_data;

  callback->user_data = user_data;
  callback->free_user_data = free_user_data;
  _DBUS_UNLOCK (callback_object);

  if (old_free_func)
    (* old_free_func) (old_user_data);
}

/**
 * Sets the function to be used to handle messages to the
 * callback object.
 *
 * @todo the thread locking on DBusCallbackObject is hosed; in this
 * function in particular it's a joke since we don't take the same
 * lock when _calling_ the callback function.
 *
 * @param callback the callback
 * @param function the function
 */
void
dbus_callback_object_set_function (DBusCallbackObject         *callback,
                                   DBusObjectMessageFunction   function)
{
  _dbus_return_if_fail (callback != NULL);

  _DBUS_LOCK (callback_object);
  callback->function = function;
  _DBUS_UNLOCK (callback_object);
}

/** @} */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>

static void
test_message_function (DBusObjectInfo     *info,
                       DBusMessage        *message)
{
  /* nothing */
}

static void
free_test_data (void *data)
{
  /* does nothing */
}

/**
 * @ingroup DBusCallbackObjectInternals
 * Unit test for DBusCallbackObject.
 *
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_object_test (void)
{
  DBusCallbackObject *callback;

#define TEST_DATA ((void*) 0xcafebabe)
  
  callback = dbus_callback_object_new (test_message_function,
                                       TEST_DATA,
                                       free_test_data);

  _dbus_assert (callback != NULL);
  _dbus_assert (callback->function == test_message_function);

  if (dbus_callback_object_get_data (callback) != TEST_DATA)
    _dbus_assert_not_reached ("got wrong data");

  dbus_callback_object_set_data (callback, NULL, NULL);
  if (dbus_callback_object_get_data (callback) != NULL)
    _dbus_assert_not_reached ("got wrong data after set");

  dbus_callback_object_set_function (callback, NULL);
  _dbus_assert (callback->function == NULL);

  dbus_callback_object_ref (callback);
  dbus_callback_object_unref (callback);
  dbus_callback_object_unref (callback);

  return TRUE;
}
#endif /* DBUS_BUILD_TESTS */
