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
  NULL, NULL, NULL, NULL, NULL,

  NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL
};
static int thread_init_generation = 0;

/** This is used for the no-op default mutex pointer, just to be distinct from #NULL */
#ifdef DBUS_BUILD_TESTS
#define _DBUS_DUMMY_MUTEX_NEW ((DBusMutex*)_dbus_strdup ("FakeMutex"))
#else
#define _DBUS_DUMMY_MUTEX_NEW ((DBusMutex*)0xABCDEF)
#endif

/** This is used for the no-op default mutex pointer, just to be distinct from #NULL */
#ifdef DBUS_BUILD_TESTS
#define _DBUS_DUMMY_CONDVAR_NEW ((DBusCondVar*)_dbus_strdup ("FakeCondvar"))
#else
#define _DBUS_DUMMY_CONDVAR_NEW ((DBusCondVar*)0xABCDEF2)
#endif

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
    return _DBUS_DUMMY_MUTEX_NEW;
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
#ifdef DBUS_BUILD_TESTS
  /* Free the fake mutex */
  else
    dbus_free (mutex);
#endif
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
 * Creates a new condition variable using the function supplied
 * to dbus_threads_init(), or creates a no-op condition variable
 * if threads are not initialized. May return #NULL even if
 * threads are initialized, indicating out-of-memory.
 *
 * @returns new mutex or #NULL
 */
DBusCondVar *
dbus_condvar_new (void)
{
  if (thread_functions.condvar_new)
    return (* thread_functions.condvar_new) ();
  else
    return _DBUS_DUMMY_CONDVAR_NEW;
}

/**
 * Frees a conditional variable created with dbus_condvar_new(); does
 * nothing if passed a #NULL pointer.
 */
void
dbus_condvar_free (DBusCondVar *cond)
{
  if (cond && thread_functions.condvar_free)
    (* thread_functions.condvar_free) (cond);
#ifdef DBUS_BUILD_TESTS
  else
    /* Free the fake condvar */
    dbus_free (cond);
#endif
}

/**
 * Atomically unlocks the mutex and waits for the conditions
 * variable to be signalled. Locks the mutex again before
 * returning.
 * Does nothing if passed a #NULL pointer.
 */
void
dbus_condvar_wait (DBusCondVar *cond,
		   DBusMutex   *mutex)
{
  if (cond && mutex && thread_functions.condvar_wait)
    (* thread_functions.condvar_wait) (cond, mutex);
}

/**
 * Atomically unlocks the mutex and waits for the conditions
 * variable to be signalled, or for a timeout. Locks the
 * mutex again before returning.
 * Does nothing if passed a #NULL pointer.
 *
 * @param cond the condition variable
 * @param mutex the mutex
 * @param timeout_milliseconds the maximum time to wait
 * @returns TRUE if the condition was reached, or FALSE if the
 * timeout was reached.
 */
dbus_bool_t
dbus_condvar_wait_timeout (DBusCondVar               *cond,
			   DBusMutex                 *mutex,
			   int                        timeout_milliseconds)
{
  if (cond && mutex && thread_functions.condvar_wait)
    return (* thread_functions.condvar_wait_timeout) (cond, mutex, timeout_milliseconds);
  else
    return TRUE;
}

/**
 * If there are threads waiting on the condition variable, wake
 * up exactly one. 
 * Does nothing if passed a #NULL pointer.
 */
void
dbus_condvar_wake_one (DBusCondVar *cond)
{
  if (cond && thread_functions.condvar_wake_one)
    (* thread_functions.condvar_wake_one) (cond);
}

/**
 * If there are threads waiting on the condition variable, wake
 * up all of them. 
 * Does nothing if passed a #NULL pointer.
 */
void
dbus_condvar_wake_all (DBusCondVar *cond)
{
  if (cond && thread_functions.condvar_wake_all)
    (* thread_functions.condvar_wake_all) (cond);
}

static void
shutdown_global_locks (void *data)
{
  DBusMutex ***locks = data;
  int i;

  i = 0;
  while (i < _DBUS_N_GLOBAL_LOCKS)
    {
      dbus_mutex_free (*(locks[i]));
      *(locks[i]) = NULL;
      ++i;
    }
  
  dbus_free (locks);
}

static dbus_bool_t
init_global_locks (void)
{
  int i;
  DBusMutex ***dynamic_global_locks;
  
  DBusMutex **global_locks[] = {
#define LOCK_ADDR(name) (& _dbus_lock_##name)
    LOCK_ADDR (list),
    LOCK_ADDR (connection_slots),
    LOCK_ADDR (server_slots),
    LOCK_ADDR (atomic),
    LOCK_ADDR (message_handler),
    LOCK_ADDR (user_info),
    LOCK_ADDR (bus)
#undef LOCK_ADDR
  };

  _dbus_assert (_DBUS_N_ELEMENTS (global_locks) ==
                _DBUS_N_GLOBAL_LOCKS);

  i = 0;
  
  dynamic_global_locks = dbus_new (DBusMutex**, _DBUS_N_GLOBAL_LOCKS);
  if (dynamic_global_locks == NULL)
    goto failed;
  
  while (i < _DBUS_N_ELEMENTS (global_locks))
    {
      *global_locks[i] = dbus_mutex_new ();
      
      if (*global_locks[i] == NULL)
        goto failed;

      dynamic_global_locks[i] = global_locks[i];

      ++i;
    }
  
  if (!_dbus_register_shutdown_func (shutdown_global_locks,
                                     dynamic_global_locks))
    goto failed;
  
  return TRUE;

 failed:
  dbus_free (dynamic_global_locks);
                                     
  for (i = i - 1; i >= 0; i--)
    {
      dbus_mutex_free (*global_locks[i]);
      *global_locks[i] = NULL;
    }
  return FALSE;
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
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_MUTEX_NEW_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_MUTEX_FREE_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_MUTEX_LOCK_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_MUTEX_UNLOCK_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_CONDVAR_NEW_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_CONDVAR_FREE_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_CONDVAR_WAIT_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_CONDVAR_WAIT_TIMEOUT_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_CONDVAR_WAKE_ONE_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_CONDVAR_WAKE_ALL_MASK);
  _dbus_assert (functions->mutex_new != NULL);
  _dbus_assert (functions->mutex_free != NULL);
  _dbus_assert (functions->mutex_lock != NULL);
  _dbus_assert (functions->mutex_unlock != NULL);
  _dbus_assert (functions->condvar_new != NULL);
  _dbus_assert (functions->condvar_free != NULL);
  _dbus_assert (functions->condvar_wait != NULL);
  _dbus_assert (functions->condvar_wait_timeout != NULL);
  _dbus_assert (functions->condvar_wake_one != NULL);
  _dbus_assert (functions->condvar_wake_all != NULL);

  /* Check that all bits in the mask actually are valid mask bits.
   * ensures people won't write code that breaks when we add
   * new bits.
   */
  _dbus_assert ((functions->mask & ~DBUS_THREAD_FUNCTIONS_ALL_MASK) == 0);

  if (thread_init_generation != _dbus_current_generation)
    thread_functions.mask = 0; /* allow re-init in new generation */
  
  if (thread_functions.mask != 0)
    {
      _dbus_warn ("dbus_threads_init() may only be called one time\n");
      return FALSE;
    }
  
  thread_functions.mutex_new = functions->mutex_new;
  thread_functions.mutex_free = functions->mutex_free;
  thread_functions.mutex_lock = functions->mutex_lock;
  thread_functions.mutex_unlock = functions->mutex_unlock;
  
  thread_functions.condvar_new = functions->condvar_new;
  thread_functions.condvar_free = functions->condvar_free;
  thread_functions.condvar_wait = functions->condvar_wait;
  thread_functions.condvar_wait_timeout = functions->condvar_wait_timeout;
  thread_functions.condvar_wake_one = functions->condvar_wake_one;
  thread_functions.condvar_wake_all = functions->condvar_wake_all;
  
  thread_functions.mask = functions->mask;

  if (!init_global_locks ())
    return FALSE;

  thread_init_generation = _dbus_current_generation;
  
  return TRUE;
}

/** @} */
