/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gthread.c  GThread integration
 *
 * Copyright (C) 2002  CodeFactory AB
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

#include <glib.h>
#include <dbus/dbus.h>
#include "dbus-gthread.h"

static DBusMutex * dbus_gmutex_new    (void);
static void        dbus_gmutex_free   (DBusMutex *mutex);
static dbus_bool_t dbus_gmutex_lock   (DBusMutex *mutex);
static dbus_bool_t dbus_gmutex_unlock (DBusMutex *mutex);

static const DBusThreadFunctions functions =
{
  DBUS_THREAD_FUNCTIONS_NEW_MASK |
  DBUS_THREAD_FUNCTIONS_FREE_MASK |
  DBUS_THREAD_FUNCTIONS_LOCK_MASK |
  DBUS_THREAD_FUNCTIONS_UNLOCK_MASK,
  dbus_gmutex_new,
  dbus_gmutex_free,
  dbus_gmutex_lock,
  dbus_gmutex_unlock
};

static DBusMutex *
dbus_gmutex_new (void)
{
  GMutex *mutex;

  mutex = g_mutex_new ();

  return (DBusMutex *)mutex;
}

static void
dbus_gmutex_free (DBusMutex *mutex)
{
  g_mutex_free ((GMutex *)mutex);
}

static dbus_bool_t
dbus_gmutex_lock (DBusMutex *mutex)
{
  g_mutex_lock ((GMutex *)mutex);

  return TRUE;
}

static dbus_bool_t
dbus_gmutex_unlock (DBusMutex *mutex)
{
  g_mutex_unlock ((GMutex *)mutex);

  return TRUE;
}

void
dbus_gthread_init (void)
{
  if (!g_thread_supported ())
    g_error ("g_thread_init() must be called before dbus_threads_init()");
    
  dbus_threads_init (&functions);
}
