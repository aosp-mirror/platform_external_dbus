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

#include <libintl.h>
#define _(x) dgettext (GETTEXT_PACKAGE, x)
#define N_(x) x

/**
 * @ingroup DBusGLib
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
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
