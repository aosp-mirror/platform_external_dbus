/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gthread.c  GThread integration
 *
 * Copyright (C) 2002  CodeFactory AB
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

/* #define G_DEBUG_LOCKS 1 */

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

/** @addtogroup DBusGLibInternals
 * @{
 */

static DBusMutex * dbus_gmutex_new        (void);
static void        dbus_gmutex_free       (DBusMutex   *mutex);
static dbus_bool_t dbus_gmutex_lock       (DBusMutex   *mutex);
static dbus_bool_t dbus_gmutex_unlock     (DBusMutex   *mutex);


static DBusCondVar* dbus_gcondvar_new          (void);
static void         dbus_gcondvar_free         (DBusCondVar *cond);
static void         dbus_gcondvar_wait         (DBusCondVar *cond,
						DBusMutex   *mutex);
static dbus_bool_t  dbus_gcondvar_wait_timeout (DBusCondVar *cond,
						DBusMutex   *mutex,
						int          timeout_msec);
static void         dbus_gcondvar_wake_one     (DBusCondVar *cond);
static void         dbus_gcondvar_wake_all     (DBusCondVar *cond);


static const DBusThreadFunctions functions =
{
  DBUS_THREAD_FUNCTIONS_MUTEX_NEW_MASK |
  DBUS_THREAD_FUNCTIONS_MUTEX_FREE_MASK |
  DBUS_THREAD_FUNCTIONS_MUTEX_LOCK_MASK |
  DBUS_THREAD_FUNCTIONS_MUTEX_UNLOCK_MASK |
  DBUS_THREAD_FUNCTIONS_CONDVAR_NEW_MASK |
  DBUS_THREAD_FUNCTIONS_CONDVAR_FREE_MASK |
  DBUS_THREAD_FUNCTIONS_CONDVAR_WAIT_MASK |
  DBUS_THREAD_FUNCTIONS_CONDVAR_WAIT_TIMEOUT_MASK |
  DBUS_THREAD_FUNCTIONS_CONDVAR_WAKE_ONE_MASK|
  DBUS_THREAD_FUNCTIONS_CONDVAR_WAKE_ALL_MASK,
  dbus_gmutex_new,
  dbus_gmutex_free,
  dbus_gmutex_lock,
  dbus_gmutex_unlock,
  dbus_gcondvar_new,
  dbus_gcondvar_free,
  dbus_gcondvar_wait,
  dbus_gcondvar_wait_timeout,
  dbus_gcondvar_wake_one,
  dbus_gcondvar_wake_all
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

static DBusCondVar*
dbus_gcondvar_new (void)
{
  return (DBusCondVar*)g_cond_new ();
}

static void
dbus_gcondvar_free (DBusCondVar *cond)
{
  g_cond_free ((GCond *)cond);
}

static void
dbus_gcondvar_wait (DBusCondVar *cond,
		    DBusMutex   *mutex)
{
  g_cond_wait ((GCond *)cond, (GMutex *)mutex);
}

static dbus_bool_t
dbus_gcondvar_wait_timeout (DBusCondVar *cond,
			    DBusMutex   *mutex,
			    int         timeout_msec)
{
  GTimeVal now;
  
  g_get_current_time (&now);

  now.tv_sec += timeout_msec / 1000;
  now.tv_usec += (timeout_msec % 1000) * 1000;
  if (now.tv_usec > G_USEC_PER_SEC)
    {
      now.tv_sec += 1;
      now.tv_usec -= G_USEC_PER_SEC;
    }
  
  return g_cond_timed_wait ((GCond *)cond, (GMutex *)mutex, &now);
}

static void
dbus_gcondvar_wake_one (DBusCondVar *cond)
{
  g_cond_signal ((GCond *)cond);
}

static void
dbus_gcondvar_wake_all (DBusCondVar *cond)
{
  g_cond_broadcast ((GCond *)cond);
}

/** @} End of internals */

/** @addtogroup DBusGLib
 * @{
 */
/**
 * Initializes the D-BUS thread system to use
 * GLib threads. This function may only be called
 * once and must be called prior to calling any
 * other function in the D-BUS API.
 */
void
dbus_g_thread_init (void)
{
  if (!g_thread_supported ())
    g_error ("g_thread_init() must be called before dbus_threads_init()");
    
  dbus_threads_init (&functions);
}

/** @} end of public API */
