/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-test.c  Program to run all tests
 *
 * Copyright (C) 2002  Red Hat Inc.
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

#include <stdlib.h>
#include <dbus/dbus.h>
#include "debug-thread.h"

#define DBUS_COMPILATION
#include <dbus/dbus-internals.h>
#undef DBUS_COMPILATION


static DBusMutex * tmutex_new    (void);
static void        tmutex_free   (DBusMutex *mutex);
static dbus_bool_t tmutex_lock   (DBusMutex *mutex);
static dbus_bool_t tmutex_unlock (DBusMutex *mutex);

static DBusCondVar*tcondvar_new          (void);
static void        tcondvar_free         (DBusCondVar *cond);
static void        tcondvar_wait         (DBusCondVar *cond,
					  DBusMutex   *mutex);
static dbus_bool_t tcondvar_wait_timeout (DBusCondVar *cond,
					  DBusMutex   *mutex,
					  int          timeout_msec);
static void        tcondvar_wake_one     (DBusCondVar *cond);
static void        tcondvar_wake_all     (DBusCondVar *cond);

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
  tmutex_new,
  tmutex_free,
  tmutex_lock,
  tmutex_unlock,
  tcondvar_new,
  tcondvar_free,
  tcondvar_wait,
  tcondvar_wait_timeout,
  tcondvar_wake_one,
  tcondvar_wake_all
};

static DBusMutex *
tmutex_new (void)
{
  int *tmutex;

  tmutex = malloc (sizeof (int*));
  *tmutex = 0;

  return (DBusMutex *)tmutex;
}

static void
tmutex_free (DBusMutex *mutex)
{
  free (mutex);
}

static dbus_bool_t
tmutex_lock (DBusMutex *mutex)
{
  int *tmutex = (int *)mutex;

  _dbus_assert (*tmutex == 0);
  
  *tmutex = 1;

  return TRUE;
}

static dbus_bool_t
tmutex_unlock (DBusMutex *mutex)
{
  int *tmutex = (int *)mutex;

  _dbus_assert (*tmutex == 1);
  
  *tmutex = 0;

  return TRUE;
}

static DBusCondVar*
tcondvar_new (void)
{
  return (DBusCondVar*)0xcafebabe;
}

static void
tcondvar_free (DBusCondVar *cond)
{
}

static void
tcondvar_wait (DBusCondVar *cond,
	       DBusMutex   *mutex)
{
  int *tmutex = (int *)mutex;

  _dbus_assert (*tmutex == 1);
}

static dbus_bool_t
tcondvar_wait_timeout (DBusCondVar *cond,
		       DBusMutex   *mutex,
		       int         timeout_msec)
{
  int *tmutex = (int *)mutex;

  _dbus_assert (*tmutex == 1);

  return TRUE;
}


static void
tcondvar_wake_one (DBusCondVar *cond)
{
}

static void
tcondvar_wake_all (DBusCondVar *cond)
{
}

void
debug_threads_init (void)
{
  dbus_threads_init (&functions);
}
  
