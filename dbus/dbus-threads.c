/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-threads.h  D-BUS threads handling
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
#include "dbus-threads.h"
#include "dbus-internals.h"

static DBusThreadFunctions thread_functions =
{
  0,
  NULL, NULL, NULL, NULL,

  NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL
};

static DBusMutex *static_mutex_init_lock = NULL;

/** This is used for the no-op default mutex pointer, just to be distinct from #NULL */
#define _DBUS_DUMMY_MUTEX ((void*)0xABCDEF)

/**
 * @defgroup DBusThreads Thread functions
 * @ingroup  DBus
 * @brief dbus_threads_init(), dbus_mutex_lock(), etc.
 *
 * Functions and macros related to threads and thread locks.
 *
 * @{
 */

/**
 * Creates a new mutex using the function supplied to dbus_threads_init(),
 * or creates a no-op mutex if threads are not initialized.
 * May return #NULL even if threads are initialized, indicating
 * out-of-memory.
 *
 * @returns new mutex or #NULL
 */
DBusMutex*
dbus_mutex_new (void)
{
  if (thread_functions.mutex_new)
    return (* thread_functions.mutex_new) ();
  else
    return _DBUS_DUMMY_MUTEX;
}

/**
 * Frees a mutex created with dbus_mutex_new(); does
 * nothing if passed a #NULL pointer.
 */
void
dbus_mutex_free (DBusMutex *mutex)
{
  if (mutex && thread_functions.mutex_free)
    (* thread_functions.mutex_free) (mutex);
}

/**
 * Locks a mutex. Does nothing if passed a #NULL pointer.
 * Locks are not recursive.
 *
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_mutex_lock (DBusMutex *mutex)
{
  if (mutex && thread_functions.mutex_lock)
    return (* thread_functions.mutex_lock) (mutex);
  else
    return TRUE;
}

/**
 * Unlocks a mutex. Does nothing if passed a #NULL pointer.
 *
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_mutex_unlock (DBusMutex *mutex)
{
  if (mutex && thread_functions.mutex_unlock)
    return (* thread_functions.mutex_unlock) (mutex);
  else
    return TRUE;
}

/**
 * Initializes threads. If this function is not called,
 * the D-BUS library will not lock any data structures.
 * If it is called, D-BUS will do locking, at some cost
 * in efficiency. Note that this function must be called
 * BEFORE using any other D-BUS functions.
 *
 * @todo right now this function can only be called once,
 * maybe we should instead silently ignore multiple calls.
 *
 * @param functions functions for using threads
 * @returns #TRUE on success, #FALSE if no memory
 */
dbus_bool_t
dbus_threads_init (const DBusThreadFunctions *functions)
{
  _dbus_assert (functions != NULL);

  /* these base functions are required. Future additions to
   * DBusThreadFunctions may be optional.
   */
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_NEW_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_FREE_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_LOCK_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_UNLOCK_MASK);
  _dbus_assert (functions->mutex_new != NULL);
  _dbus_assert (functions->mutex_free != NULL);
  _dbus_assert (functions->mutex_lock != NULL);
  _dbus_assert (functions->mutex_unlock != NULL);

  /* Check that all bits in the mask actually are valid mask bits.
   * ensures people won't write code that breaks when we add
   * new bits.
   */
  _dbus_assert ((functions->mask & ~DBUS_THREAD_FUNCTIONS_ALL_MASK) == 0);
  
  if (thread_functions.mask != 0)
    {
      _dbus_warn ("dbus_threads_init() may only be called one time\n");
      return FALSE;
    }
  
  thread_functions.mutex_new = functions->mutex_new;
  thread_functions.mutex_free = functions->mutex_free;
  thread_functions.mutex_lock = functions->mutex_lock;
  thread_functions.mutex_unlock = functions->mutex_unlock;
  
  thread_functions.mask = functions->mask;

  static_mutex_init_lock = dbus_mutex_new ();

  if (static_mutex_init_lock == NULL)
    {
      thread_functions.mask = 0;
      return FALSE;
    }

  return TRUE;
}

/** Accesses the field of DBusStaticMutex that
 * stores the DBusMutex used to implement.
 */
#define _DBUS_STATIC_MUTEX_IMPL(mutex) ((mutex)->pad1)

/**
 * Lock a static mutex
 *
 * @param mutex the mutex to lock
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_static_mutex_lock (DBusStaticMutex *mutex)
{
  if (_DBUS_STATIC_MUTEX_IMPL (mutex))
    return dbus_mutex_lock (_DBUS_STATIC_MUTEX_IMPL (mutex));

  if (!dbus_mutex_lock (static_mutex_init_lock))
    return FALSE;

  if (_DBUS_STATIC_MUTEX_IMPL (mutex) == NULL)
    _DBUS_STATIC_MUTEX_IMPL (mutex) = dbus_mutex_new ();
  
  dbus_mutex_unlock (static_mutex_init_lock);

  if (_DBUS_STATIC_MUTEX_IMPL (mutex))
    return dbus_mutex_lock (_DBUS_STATIC_MUTEX_IMPL (mutex));
  else
    return FALSE;
}

/**
 * Unlock a static mutex
 * @param mutex the mutex to lock
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_static_mutex_unlock (DBusStaticMutex *mutex)
{
  _dbus_assert (_DBUS_STATIC_MUTEX_IMPL (mutex) != NULL);
  
  return dbus_mutex_unlock (_DBUS_STATIC_MUTEX_IMPL (mutex));
}

/** @} */
