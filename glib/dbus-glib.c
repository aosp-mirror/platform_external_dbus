/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-glib.c General GLib binding stuff
 *
 * Copyright (C) 2004 Red Hat, Inc.
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
#include <string.h>

#include <libintl.h>
#define _(x) dgettext (GETTEXT_PACKAGE, x)
#define N_(x) x

/**
 * @addtogroup DBusGLib
 * @{
 */

/**
 * Blocks until outgoing calls and signal emissions have been sent.
 * 
 * @param connection the connection to flush
 */
void
dbus_g_connection_flush (DBusGConnection *connection)
{
  dbus_connection_flush (DBUS_CONNECTION_FROM_G_CONNECTION (connection));
}

/**
 * Increment refcount on a #DBusGConnection
 * 
 * @param gconnection the connection to ref
 * @returns the connection that was ref'd
 */
DBusGConnection*
dbus_g_connection_ref (DBusGConnection *gconnection)
{
  DBusConnection *c;

  c = DBUS_CONNECTION_FROM_G_CONNECTION (gconnection);
  dbus_connection_ref (c);
  return gconnection;
}


/**
 * Decrement refcount on a #DBusGConnection
 * 
 * @param gconnection the connection to unref
 */
void
dbus_g_connection_unref (DBusGConnection *gconnection)
{
  DBusConnection *c;

  c = DBUS_CONNECTION_FROM_G_CONNECTION (gconnection);
  dbus_connection_unref (c);
}


/**
 * Increment refcount on a #DBusGMessage
 * 
 * @param gmessage the message to ref
 * @returns the message that was ref'd
 */
DBusGMessage*
dbus_g_message_ref (DBusGMessage *gmessage)
{
  DBusMessage *c;

  c = DBUS_MESSAGE_FROM_G_MESSAGE (gmessage);
  dbus_message_ref (c);
  return gmessage;
}

/**
 * Decrement refcount on a #DBusGMessage
 * 
 * @param gmessage the message to unref
 */
void
dbus_g_message_unref (DBusGMessage *gmessage)
{
  DBusMessage *c;

  c = DBUS_MESSAGE_FROM_G_MESSAGE (gmessage);
  dbus_message_unref (c);
}

/**
 * Increments refcount on a pending call.
 *
 * @param call the call
 * @returns the same call
 */
DBusGPendingCall*
dbus_g_pending_call_ref (DBusGPendingCall  *call)
{
  dbus_pending_call_ref (DBUS_PENDING_CALL_FROM_G_PENDING_CALL (call));
  return call;
}

/**
 * Decrements refcount on a pending call.
 *
 * @param call the call
 */
void
dbus_g_pending_call_unref (DBusGPendingCall  *call)
{
  dbus_pending_call_unref (DBUS_PENDING_CALL_FROM_G_PENDING_CALL (call));
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

#include "dbus-glib-error-switch.h"

/**
 * Set a GError return location from a D-BUS error name and message.
 * This function should only be used in the implementation of service
 * methods.
 *
 * @param gerror location to store a GError, or #NULL
 * @param name the D-BUS error name
 * @param msg the D-BUS error detailed message
 */
void
dbus_g_error_set (GError    **gerror,
                  const char *name,
		  const char *msg)
{
  int code;
  g_return_if_fail (name != NULL);
  g_return_if_fail (msg != NULL);

  code = dbus_error_to_gerror_code (name);
  if (code == DBUS_GERROR_REMOTE_EXCEPTION)
    g_set_error (gerror, DBUS_GERROR,
		 code,
		 "%s%c%s",
		 msg,
		 '\0',
		 name);
  else
    g_set_error (gerror, DBUS_GERROR,
		 code,
		 "%s",
		 msg);
}

/**
 * Determine whether D-BUS error name for a remote exception matches
 * the given name.  This function is intended to be invoked on a
 * GError returned from an invocation of a remote method, e.g. via
 * dbus_g_proxy_end_call.  It will silently return FALSE for errors
 * which are not remote D-BUS exceptions (i.e. with a domain other
 * than DBUS_GERROR or a code other than
 * DBUS_GERROR_REMOTE_EXCEPTION).
 *
 * @param gerror the GError given from the remote method
 * @param name the D-BUS error name
 * @param msg the D-BUS error detailed message
 * @returns TRUE iff the remote error has the given name
 */
gboolean
dbus_g_error_has_name (GError *error, const char *name)
{
  g_return_val_if_fail (error != NULL, FALSE);

  if (error->domain != DBUS_GERROR
      || error->code != DBUS_GERROR_REMOTE_EXCEPTION)
    return FALSE;

  return !strcmp (dbus_g_error_get_name (error), name);
}

/**
 * Return the D-BUS name for a remote exception.
 * This function may only be invoked on a GError returned from an
 * invocation of a remote method, e.g. via dbus_g_proxy_end_call.
 * Moreover, you must ensure that the error's domain is DBUS_GERROR,
 * and the code is DBUS_GERROR_REMOTE_EXCEPTION.
 *
 * @param gerror the GError given from the remote method
 * @param name the D-BUS error name
 * @param msg the D-BUS error detailed message
 * @returns the D-BUS error name
 */
const char *
dbus_g_error_get_name (GError *error)
{
  g_return_val_if_fail (error != NULL, NULL);
  g_return_val_if_fail (error->domain == DBUS_GERROR, NULL);
  g_return_val_if_fail (error->code == DBUS_GERROR_REMOTE_EXCEPTION, NULL);

  return error->message + strlen (error->message) + 1;
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

/**
 * Get the GLib type ID for a DBusPendingCall boxed type.
 *
 * @returns GLib type
 */
GType
dbus_pending_call_get_g_type (void)
{
  static GType our_type = 0;
  
  if (our_type == 0)
    our_type = g_boxed_type_register_static ("DBusPendingCall",
                                             (GBoxedCopyFunc) dbus_pending_call_ref,
                                             (GBoxedFreeFunc) dbus_pending_call_unref);

  return our_type;
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
 * Get the GLib type ID for a DBusGPendingCall boxed type.
 *
 * @returns GLib type
 */
GType
dbus_g_pending_call_get_g_type (void)
{
  static GType our_type = 0;
  
  if (our_type == 0)
    our_type = g_boxed_type_register_static ("DBusGPendingCall",
                                             (GBoxedCopyFunc) dbus_g_pending_call_ref,
                                             (GBoxedFreeFunc) dbus_g_pending_call_unref);

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

typedef struct
{
  DBusGPendingCallNotify func;
  void *data;
  GDestroyNotify free_data_func;
} GPendingNotifyClosure;

static void
d_pending_call_notify (DBusPendingCall *dcall,
                       void            *data)
{
  GPendingNotifyClosure *closure = data;
  DBusGPendingCall *gcall = DBUS_G_PENDING_CALL_FROM_PENDING_CALL (dcall);

  (* closure->func) (gcall, closure->data);
}

static void
d_pending_call_free (void *data)
{
  GPendingNotifyClosure *closure = data;
  
  if (closure->free_data_func)
    (* closure->free_data_func) (closure->data);

  g_free (closure);
}
  
/**
 * Sets up a notification to be invoked when the pending call
 * is ready to be ended without blocking.
 * You can call dbus_g_proxy_end_call() at any time,
 * but it will block if no reply or error has been received yet.
 * This function lets you handle the reply asynchronously.
 *
 * @param call the pending call
 * @param callback the callback
 * @param callback_data data for the callback
 * @param free_data_func free the callback data with this
 */
void
dbus_g_pending_call_set_notify (DBusGPendingCall      *call,
                                DBusGPendingCallNotify callback,
                                void                  *callback_data,
                                GDestroyNotify         free_data_func)
{
  GPendingNotifyClosure *closure;
  DBusPendingCall *dcall;

  g_return_if_fail (callback != NULL);
  
  closure = g_new (GPendingNotifyClosure, 1);

  closure->func = callback;
  closure->data = callback_data;
  closure->free_data_func = free_data_func;

  dcall = DBUS_PENDING_CALL_FROM_G_PENDING_CALL (call);
  dbus_pending_call_set_notify (dcall, d_pending_call_notify,
                                closure,
                                d_pending_call_free);
}

/**
 * Cancels a pending call. Does not affect the reference count
 * of the call, but it means you will never be notified of call
 * completion, and can't call dbus_g_proxy_end_call().
 *
 * @param call the call
 */
void
dbus_g_pending_call_cancel (DBusGPendingCall *call)
{
  DBusPendingCall *dcall;
  
  dcall = DBUS_PENDING_CALL_FROM_G_PENDING_CALL (call);

  dbus_pending_call_cancel (dcall);
}

/** @} */ /* end of public API */


#ifdef DBUS_BUILD_TESTS

/**
 * @ingroup DBusGLibInternals
 * Unit test for general glib stuff
 * @returns #TRUE on success.
 */
gboolean
_dbus_glib_test (const char *test_data_dir)
{
  DBusError err;
  GError *gerror = NULL;

  dbus_error_init (&err);
  dbus_set_error_const (&err, DBUS_ERROR_NO_MEMORY, "Out of memory!");

  dbus_g_error_set (&gerror, err.name, err.message);
  g_assert (gerror != NULL);
  g_assert (gerror->domain == DBUS_GERROR);
  g_assert (gerror->code == DBUS_GERROR_NO_MEMORY);
  g_assert (!strcmp (gerror->message, "Out of memory!"));
  
  dbus_error_init (&err);
  g_clear_error (&gerror);

  dbus_g_error_set (&gerror, "com.example.Foo.BlahFailed", "blah failed");
  g_assert (gerror != NULL);
  g_assert (gerror->domain == DBUS_GERROR);
  g_assert (gerror->code == DBUS_GERROR_REMOTE_EXCEPTION);
  g_assert (dbus_g_error_has_name (gerror, "com.example.Foo.BlahFailed"));
  g_assert (!strcmp (gerror->message, "blah failed"));

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
