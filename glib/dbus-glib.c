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
#include "dbus-gobject.h"
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
 * Determine whether D-BUS error name for a remote exception matches
 * the given name.  This function is intended to be invoked on a
 * GError returned from an invocation of a remote method, e.g. via
 * dbus_g_proxy_end_call.  It will silently return FALSE for errors
 * which are not remote D-BUS exceptions (i.e. with a domain other
 * than DBUS_GERROR or a code other than
 * DBUS_GERROR_REMOTE_EXCEPTION).
 *
 * @param error the GError given from the remote method
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
 * @param error the GError given from the remote method
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

  dbus_set_g_error (&gerror, &err);
  g_assert (gerror != NULL);
  g_assert (gerror->domain == DBUS_GERROR);
  g_assert (gerror->code == DBUS_GERROR_NO_MEMORY);
  g_assert (!strcmp (gerror->message, "Out of memory!"));
  
  dbus_error_init (&err);
  g_clear_error (&gerror);

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
