/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-threads.h  D-Bus threads handling
 *
 * Copyright (C) 2002, 2003 Red Hat Inc.
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
#include "dbus-threads.h"
#include "dbus-internals.h"
#include "dbus-threads-internal.h"
#include "dbus-list.h"

#if defined(__WIN32) || defined(__CYGWIN__)
#define USE_WIN32_THREADS
#endif

#ifdef USE_WIN32_THREADS
#include <windows.h>
#else
#include <sys/time.h>
#include <pthread.h>
#endif

static DBusThreadFunctions thread_functions =
{
  0,
  NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL,
  
  NULL, NULL, NULL, NULL
};

#ifdef USE_WIN32_THREADS
struct DBusCondVar {
  DBusList *list;
  CRITICAL_SECTION lock;
};

static DWORD dbus_cond_event_tls = TLS_OUT_OF_INDEXES;
#endif

static int thread_init_generation = 0;
 
static DBusList *uninitialized_mutex_list = NULL;
static DBusList *uninitialized_condvar_list = NULL;

/** This is used for the no-op default mutex pointer, just to be distinct from #NULL */
#define _DBUS_DUMMY_MUTEX ((DBusMutex*)0xABCDEF)

/** This is used for the no-op default mutex pointer, just to be distinct from #NULL */
#define _DBUS_DUMMY_CONDVAR ((DBusCondVar*)0xABCDEF2)

/**
 * @defgroup DBusThreadsInternals Thread functions
 * @ingroup  DBusInternals
 * @brief _dbus_mutex_lock(), etc.
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
_dbus_mutex_new (void)
{
  if (thread_functions.recursive_mutex_new)
    return (* thread_functions.recursive_mutex_new) ();
  else if (thread_functions.mutex_new)
    return (* thread_functions.mutex_new) ();
  else
    return _DBUS_DUMMY_MUTEX;
}

/**
 * This does the same thing as _dbus_mutex_new.  It however
 * gives another level of indirection by allocating a pointer
 * to point to the mutex location.  This allows the threading
 * module to swap out dummy mutexes for real a real mutex so libraries
 * can initialize threads even after the D-Bus API has been used.
 *
 * @param location_p the location of the new mutex, can return #NULL on OOM
 */
void
_dbus_mutex_new_at_location (DBusMutex **location_p)
{
  _dbus_assert (location_p != NULL);

  *location_p = _dbus_mutex_new();

  if (thread_init_generation != _dbus_current_generation && *location_p)
    {
      if (!_dbus_list_append (&uninitialized_mutex_list, location_p))
        {
	  _dbus_mutex_free (*location_p);
	  *location_p = NULL;
	}
    }
}

/**
 * Frees a mutex created with dbus_mutex_new(); does
 * nothing if passed a #NULL pointer.
 */
void
_dbus_mutex_free (DBusMutex *mutex)
{
  if (mutex)
    {
      if (mutex && thread_functions.recursive_mutex_free)
        (* thread_functions.recursive_mutex_free) (mutex);
      else if (mutex && thread_functions.mutex_free)
        (* thread_functions.mutex_free) (mutex);
    }
}

/**
 * Frees a mutex and removes it from the 
 * uninitialized_mutex_list;
 * does nothing if passed a #NULL pointer.
 */
void
_dbus_mutex_free_at_location (DBusMutex **location_p)
{
  if (location_p)
    {
      if (thread_init_generation != _dbus_current_generation)
        _dbus_list_remove (&uninitialized_mutex_list, location_p);

      _dbus_mutex_free (*location_p);
    }
}

/**
 * Locks a mutex. Does nothing if passed a #NULL pointer.
 * Locks may be recursive if threading implementation initialized
 * recursive locks.
 */
void
_dbus_mutex_lock (DBusMutex *mutex)
{
  if (mutex) 
    {
      if (thread_functions.recursive_mutex_lock)
        (* thread_functions.recursive_mutex_lock) (mutex);
      else if (thread_functions.mutex_lock)
        (* thread_functions.mutex_lock) (mutex);
    }
}

/**
 * Unlocks a mutex. Does nothing if passed a #NULL pointer.
 *
 * @returns #TRUE on success
 */
void
_dbus_mutex_unlock (DBusMutex *mutex)
{
  if (mutex)
    {
      if (thread_functions.recursive_mutex_unlock)
        (* thread_functions.recursive_mutex_unlock) (mutex);
      else if (thread_functions.mutex_unlock)
        (* thread_functions.mutex_unlock) (mutex);
    }
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
_dbus_condvar_new (void)
{
  if (thread_functions.condvar_new)
    return (* thread_functions.condvar_new) ();
  else
    return _DBUS_DUMMY_CONDVAR;
}


/**
 * This does the same thing as _dbus_condvar_new.  It however
 * gives another level of indirection by allocating a pointer
 * to point to the condvar location.  This allows the threading
 * module to swap out dummy condvars for real a real condvar so libraries
 * can initialize threads even after the D-Bus API has been used.
 *
 * @returns the location of a new condvar or #NULL on OOM
 */

void 
_dbus_condvar_new_at_location (DBusCondVar **location_p)
{
  *location_p = _dbus_condvar_new();

  if (thread_init_generation != _dbus_current_generation && *location_p)
    {
      if (!_dbus_list_append (&uninitialized_condvar_list, location_p))
        {
          _dbus_condvar_free (*location_p);
          *location_p = NULL;
        }
    }
}


/**
 * Frees a conditional variable created with dbus_condvar_new(); does
 * nothing if passed a #NULL pointer.
 */
void
_dbus_condvar_free (DBusCondVar *cond)
{
  if (cond && thread_functions.condvar_free)
    (* thread_functions.condvar_free) (cond);
}

/**
 * Frees a conditional variable and removes it from the 
 * uninitialized_condvar_list; 
 * does nothing if passed a #NULL pointer.
 */
void
_dbus_condvar_free_at_location (DBusCondVar **location_p)
{
  if (location_p)
    {
      if (thread_init_generation != _dbus_current_generation)
        _dbus_list_remove (&uninitialized_condvar_list, location_p);

      _dbus_condvar_free (*location_p);
    }
}

/**
 * Atomically unlocks the mutex and waits for the conditions
 * variable to be signalled. Locks the mutex again before
 * returning.
 * Does nothing if passed a #NULL pointer.
 */
void
_dbus_condvar_wait (DBusCondVar *cond,
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
_dbus_condvar_wait_timeout (DBusCondVar               *cond,
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
_dbus_condvar_wake_one (DBusCondVar *cond)
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
_dbus_condvar_wake_all (DBusCondVar *cond)
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
      _dbus_mutex_free (*(locks[i]));
      *(locks[i]) = NULL;
      ++i;
    }
  
  dbus_free (locks);
}

static void
shutdown_uninitialized_locks (void *data)
{
  _dbus_list_clear (&uninitialized_mutex_list);
  _dbus_list_clear (&uninitialized_condvar_list);
}

static dbus_bool_t
init_uninitialized_locks (void)
{
  DBusList *link;

  _dbus_assert (thread_init_generation == 0);

  link = uninitialized_mutex_list;
  while (link != NULL)
    {
      DBusMutex **mp;

      mp = (DBusMutex **)link->data;
      _dbus_assert (*mp == _DBUS_DUMMY_MUTEX);

      *mp = _dbus_mutex_new ();
      if (*mp == NULL)
        goto fail_mutex;

      link = _dbus_list_get_next_link (&uninitialized_mutex_list, link);
    }

  link = uninitialized_condvar_list;
  while (link != NULL)
    {
      DBusCondVar **cp;

      cp = (DBusCondVar **)link->data;
      _dbus_assert (*cp == _DBUS_DUMMY_CONDVAR);

      *cp = _dbus_condvar_new ();
      if (*cp == NULL)
        goto fail_condvar;

      link = _dbus_list_get_next_link (&uninitialized_condvar_list, link);
    }

  _dbus_list_clear (&uninitialized_mutex_list);
  _dbus_list_clear (&uninitialized_condvar_list);

  if (!_dbus_register_shutdown_func (shutdown_uninitialized_locks,
                                     NULL))
    goto fail_condvar;

  return TRUE;

 fail_condvar:
  link = uninitialized_condvar_list;
  while (link != NULL)
    {
      DBusCondVar **cp;

      cp = (DBusCondVar **)link->data;

      if (*cp != _DBUS_DUMMY_CONDVAR)
        _dbus_condvar_free (*cp);
      else
        break;

      *cp = _DBUS_DUMMY_CONDVAR;

      link = _dbus_list_get_next_link (&uninitialized_condvar_list, link);
    }

 fail_mutex:
  link = uninitialized_mutex_list;
  while (link != NULL)
    {
      DBusMutex **mp;

      mp = (DBusMutex **)link->data;

      if (*mp != _DBUS_DUMMY_MUTEX)
        _dbus_mutex_free (*mp);
      else
        break;

      *mp = _DBUS_DUMMY_MUTEX;

      link = _dbus_list_get_next_link (&uninitialized_mutex_list, link);
    }

  return FALSE;
}

static dbus_bool_t
init_locks (void)
{
  int i;
  DBusMutex ***dynamic_global_locks;
  
  DBusMutex **global_locks[] = {
#define LOCK_ADDR(name) (& _dbus_lock_##name)
    LOCK_ADDR (win_fds),
    LOCK_ADDR (sid_atom_cache),
    LOCK_ADDR (list),
    LOCK_ADDR (connection_slots),
    LOCK_ADDR (pending_call_slots),
    LOCK_ADDR (server_slots),
    LOCK_ADDR (message_slots),
    LOCK_ADDR (atomic),
    LOCK_ADDR (bus),
    LOCK_ADDR (shutdown_funcs),
    LOCK_ADDR (system_users),
    LOCK_ADDR (message_cache),
    LOCK_ADDR (shared_connections),
    LOCK_ADDR (machine_uuid)
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
      *global_locks[i] = _dbus_mutex_new ();
      
      if (*global_locks[i] == NULL)
        goto failed;

      dynamic_global_locks[i] = global_locks[i];

      ++i;
    }
  
  if (!_dbus_register_shutdown_func (shutdown_global_locks,
                                     dynamic_global_locks))
    goto failed;

  if (!init_uninitialized_locks ())
    goto failed;
  
  return TRUE;

 failed:
  dbus_free (dynamic_global_locks);
                                     
  for (i = i - 1; i >= 0; i--)
    {
      _dbus_mutex_free (*global_locks[i]);
      *global_locks[i] = NULL;
    }
  return FALSE;
}

/** @} */ /* end of internals */

/**
 * @defgroup DBusThreads Thread functions
 * @ingroup  DBus
 * @brief dbus_threads_init() and dbus_threads_init_default()
 *
 * Functions and macros related to threads and thread locks.
 *
 * If threads are initialized, the D-Bus library has locks on all
 * global data structures.  In addition, each #DBusConnection has a
 * lock, so only one thread at a time can touch the connection.  (See
 * @ref DBusConnection for more on connection locking.)
 *
 * Most other objects, however, do not have locks - they can only be
 * used from a single thread at a time, unless you lock them yourself.
 * For example, a #DBusMessage can't be modified from two threads
 * at once.
 * 
 * @{
 */

/**
 * 
 * Initializes threads. If this function is not called,
 * the D-Bus library will not lock any data structures.
 * If it is called, D-Bus will do locking, at some cost
 * in efficiency. Note that this function must be called
 * BEFORE the second thread is started.
 *
 * Use dbus_threads_init_default() if you don't need a
 * particular thread implementation.
 *
 * This function may be called more than once.  The first
 * one wins.
 *
 * @param functions functions for using threads
 * @returns #TRUE on success, #FALSE if no memory
 */
dbus_bool_t
dbus_threads_init (const DBusThreadFunctions *functions)
{
  dbus_bool_t mutex_set;
  dbus_bool_t recursive_mutex_set;

  _dbus_assert (functions != NULL);

  /* these base functions are required. Future additions to
   * DBusThreadFunctions may be optional.
   */
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_CONDVAR_NEW_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_CONDVAR_FREE_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_CONDVAR_WAIT_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_CONDVAR_WAIT_TIMEOUT_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_CONDVAR_WAKE_ONE_MASK);
  _dbus_assert (functions->mask & DBUS_THREAD_FUNCTIONS_CONDVAR_WAKE_ALL_MASK);
  _dbus_assert (functions->condvar_new != NULL);
  _dbus_assert (functions->condvar_free != NULL);
  _dbus_assert (functions->condvar_wait != NULL);
  _dbus_assert (functions->condvar_wait_timeout != NULL);
  _dbus_assert (functions->condvar_wake_one != NULL);
  _dbus_assert (functions->condvar_wake_all != NULL);

  /* Either the mutex function set or recursive mutex set needs 
   * to be available but not both
   */
  mutex_set = (functions->mask & DBUS_THREAD_FUNCTIONS_MUTEX_NEW_MASK) &&  
              (functions->mask & DBUS_THREAD_FUNCTIONS_MUTEX_FREE_MASK) && 
              (functions->mask & DBUS_THREAD_FUNCTIONS_MUTEX_LOCK_MASK) &&
              (functions->mask & DBUS_THREAD_FUNCTIONS_MUTEX_UNLOCK_MASK) &&
               functions->mutex_new &&
               functions->mutex_free &&
               functions->mutex_lock &&
               functions->mutex_unlock;

  recursive_mutex_set = 
              (functions->mask & DBUS_THREAD_FUNCTIONS_RECURSIVE_MUTEX_NEW_MASK) && 
              (functions->mask & DBUS_THREAD_FUNCTIONS_RECURSIVE_MUTEX_FREE_MASK) && 
              (functions->mask & DBUS_THREAD_FUNCTIONS_RECURSIVE_MUTEX_LOCK_MASK) && 
              (functions->mask & DBUS_THREAD_FUNCTIONS_RECURSIVE_MUTEX_UNLOCK_MASK) &&
                functions->recursive_mutex_new &&
                functions->recursive_mutex_free &&
                functions->recursive_mutex_lock &&
                functions->recursive_mutex_unlock;

  if (!(mutex_set || recursive_mutex_set))
    _dbus_assert_not_reached ("Either the nonrecusrive or recursive mutex " 
                              "functions sets should be passed into "
                              "dbus_threads_init. Neither sets were passed.");

  if (mutex_set && recursive_mutex_set)
    _dbus_assert_not_reached ("Either the nonrecusrive or recursive mutex " 
                              "functions sets should be passed into "
                              "dbus_threads_init. Both sets were passed. "
                              "You most likely just want to set the recursive "
                              "mutex functions to avoid deadlocks in D-Bus.");
                          
  /* Check that all bits in the mask actually are valid mask bits.
   * ensures people won't write code that breaks when we add
   * new bits.
   */
  _dbus_assert ((functions->mask & ~DBUS_THREAD_FUNCTIONS_ALL_MASK) == 0);

  if (thread_init_generation != _dbus_current_generation)
    thread_functions.mask = 0; /* allow re-init in new generation */
 
  /* Silently allow multiple init
   * First init wins and D-Bus will always use its threading system 
   */ 
  if (thread_functions.mask != 0)
    return TRUE;
  
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
 
  if (functions->mask & DBUS_THREAD_FUNCTIONS_RECURSIVE_MUTEX_NEW_MASK)
    thread_functions.recursive_mutex_new = functions->recursive_mutex_new;
  
  if (functions->mask & DBUS_THREAD_FUNCTIONS_RECURSIVE_MUTEX_FREE_MASK)
    thread_functions.recursive_mutex_free = functions->recursive_mutex_free;
  
  if (functions->mask & DBUS_THREAD_FUNCTIONS_RECURSIVE_MUTEX_LOCK_MASK)
    thread_functions.recursive_mutex_lock = functions->recursive_mutex_lock;

  if (functions->mask & DBUS_THREAD_FUNCTIONS_RECURSIVE_MUTEX_UNLOCK_MASK)
    thread_functions.recursive_mutex_unlock = functions->recursive_mutex_unlock;

  thread_functions.mask = functions->mask;

  if (!init_locks ())
    return FALSE;

  thread_init_generation = _dbus_current_generation;
  
  return TRUE;
}



/* Default thread implemenation */

static DBusMutex*   _dbus_internal_mutex_new            (void);
static void         _dbus_internal_mutex_free           (DBusMutex   *mutex);
static dbus_bool_t  _dbus_internal_mutex_lock           (DBusMutex   *mutex);
static dbus_bool_t  _dbus_internal_mutex_unlock         (DBusMutex   *mutex);
static DBusCondVar *_dbus_internal_condvar_new          (void);
static void         _dbus_internal_condvar_free         (DBusCondVar *cond);
static void         _dbus_internal_condvar_wait         (DBusCondVar *cond,
							 DBusMutex   *mutex);
static dbus_bool_t  _dbus_internal_condvar_wait_timeout (DBusCondVar *cond,
							 DBusMutex   *mutex,
							 int          timeout_milliseconds);
static void         _dbus_internal_condvar_wake_one     (DBusCondVar *cond);
static void         _dbus_internal_condvar_wake_all     (DBusCondVar *cond);

#ifdef USE_WIN32_THREADS

BOOL WINAPI DllMain (HINSTANCE hinstDLL,
		     DWORD     fdwReason,
		     LPVOID    lpvReserved);

/* We need this to free the TLS events on thread exit */
BOOL WINAPI
DllMain (HINSTANCE hinstDLL,
	 DWORD     fdwReason,
	 LPVOID    lpvReserved)
{
  HANDLE event;
  switch (fdwReason) 
    { 
    case DLL_THREAD_DETACH:
      if (dbus_cond_event_tls != TLS_OUT_OF_INDEXES)
	{
	  event = TlsGetValue(dbus_cond_event_tls);
	  CloseHandle (event);
	  TlsSetValue(dbus_cond_event_tls, NULL);
	}
      break;
    case DLL_PROCESS_DETACH: 
      if (dbus_cond_event_tls != TLS_OUT_OF_INDEXES)
	{
	  event = TlsGetValue(dbus_cond_event_tls);
	  CloseHandle (event);
	  TlsSetValue(dbus_cond_event_tls, NULL);

	  TlsFree(dbus_cond_event_tls); 
	}
      break;
    default: 
      break; 
    }
  return TRUE;
}

static DBusMutex*
_dbus_internal_mutex_new (void)
{
  HANDLE handle;
  handle = CreateMutex (NULL, FALSE, NULL);
  return (DBusMutex *) handle;
}

static void
_dbus_internal_mutex_free (DBusMutex *mutex)
{
  CloseHandle ((HANDLE *) mutex);
}

static dbus_bool_t
_dbus_internal_mutex_lock (DBusMutex *mutex)
{
  return WaitForSingleObject ((HANDLE *) mutex, INFINITE) != WAIT_FAILED;
}

static dbus_bool_t
_dbus_internal_mutex_unlock (DBusMutex *mutex)
{
  return ReleaseMutex ((HANDLE *) mutex) != 0;
}

static DBusCondVar *
_dbus_internal_condvar_new (void)
{
  DBusCondVar *cond;
    
  cond = dbus_new (DBusCondVar, 1);
  if (cond == NULL)
    return NULL;
  
  cond->list = NULL;
  
  InitializeCriticalSection (&cond->lock);
  return (DBusCondVar *) cond;
}

static void
_dbus_internal_condvar_free (DBusCondVar *cond)
{
  DeleteCriticalSection (&cond->lock);
  _dbus_list_clear (&cond->list);
  dbus_free (cond);
}

static dbus_bool_t
_dbus_condvar_wait_win32 (DBusCondVar *cond,
			  DBusMutex *mutex,
			  int milliseconds)
{
  DWORD retval;
  dbus_bool_t ret;
  HANDLE event = TlsGetValue (dbus_cond_event_tls);

  if (!event)
    {
      event = CreateEvent (0, FALSE, FALSE, NULL);
      if (event == 0)
	return FALSE;
      TlsSetValue (dbus_cond_event_tls, event);
    }

  EnterCriticalSection (&cond->lock);

  /* The event must not be signaled. Check this */
  _dbus_assert (WaitForSingleObject (event, 0) == WAIT_TIMEOUT);

  ret = _dbus_list_append (&cond->list, event);
  
  LeaveCriticalSection (&cond->lock);
  
  if (!ret)
    return FALSE; /* Prepend failed */

  _dbus_mutex_unlock (mutex);
  retval = WaitForSingleObject (event, milliseconds);
  _dbus_mutex_lock (mutex);
  
  if (retval == WAIT_TIMEOUT)
    {
      EnterCriticalSection (&cond->lock);
      _dbus_list_remove (&cond->list, event);

      /* In the meantime we could have been signaled, so we must again
       * wait for the signal, this time with no timeout, to reset
       * it. retval is set again to honour the late arrival of the
       * signal */
      retval = WaitForSingleObject (event, 0);

      LeaveCriticalSection (&cond->lock);
    }

#ifndef DBUS_DISABLE_ASSERT
  EnterCriticalSection (&cond->lock);

  /* Now event must not be inside the array, check this */
  _dbus_assert (_dbus_list_remove (cond->list, event) == FALSE);

  LeaveCriticalSection (&cond->lock);
#endif /* !G_DISABLE_ASSERT */

  return retval != WAIT_TIMEOUT;
}

static void
_dbus_internal_condvar_wait (DBusCondVar *cond,
                    DBusMutex   *mutex)
{
  _dbus_condvar_wait_win32 (cond, mutex, INFINITE);
}

static dbus_bool_t
_dbus_internal_condvar_wait_timeout (DBusCondVar               *cond,
				     DBusMutex                 *mutex,
				     int                        timeout_milliseconds)
{
  return _dbus_condvar_wait_win32 (cond, mutex, timeout_milliseconds);
}

static void
_dbus_internal_condvar_wake_one (DBusCondVar *cond)
{
  EnterCriticalSection (&cond->lock);
  
  if (cond->list != NULL)
    SetEvent (_dbus_list_pop_first (&cond->list));
    
  LeaveCriticalSection (&cond->lock);
}

static void
_dbus_internal_condvar_wake_all (DBusCondVar *cond)
{
  EnterCriticalSection (&cond->lock);

  while (cond->list != NULL)
    SetEvent (_dbus_list_pop_first (&cond->list));
  
  LeaveCriticalSection (&cond->lock);
}


#else /* Posix threads */

static DBusMutex*
_dbus_internal_mutex_new (void)
{
  pthread_mutex_t *retval;
  
  retval = dbus_new (pthread_mutex_t, 1);
  if (retval == NULL)
    return NULL;
  
  if (pthread_mutex_init (retval, NULL))
    {
      dbus_free (retval);
      return NULL;
    }
  return (DBusMutex *) retval;
}

static void
_dbus_internal_mutex_free (DBusMutex *mutex)
{
  pthread_mutex_destroy ((pthread_mutex_t *) mutex);
  dbus_free (mutex);
}

static dbus_bool_t
_dbus_internal_mutex_lock (DBusMutex *mutex)
{
  return pthread_mutex_lock ((pthread_mutex_t *) mutex) == 0;
}

static dbus_bool_t
_dbus_internal_mutex_unlock (DBusMutex *mutex)
{
  return pthread_mutex_unlock ((pthread_mutex_t *) mutex) == 0;
}

static DBusCondVar *
_dbus_internal_condvar_new (void)
{
  pthread_cond_t *retval;
  
  retval = dbus_new (pthread_cond_t, 1);
  if (retval == NULL)
    return NULL;
  
  if (pthread_cond_init (retval, NULL))
    {
      dbus_free (retval);
      return NULL;
    }
  return (DBusCondVar *) retval;
}

static void
_dbus_internal_condvar_free (DBusCondVar *cond)
{
  pthread_cond_destroy ((pthread_cond_t *) cond);
  dbus_free (cond);
}

static void
_dbus_internal_condvar_wait (DBusCondVar *cond,
                    DBusMutex   *mutex)
{
  pthread_cond_wait ((pthread_cond_t *)cond,
		     (pthread_mutex_t *) mutex);
}

static dbus_bool_t
_dbus_internal_condvar_wait_timeout (DBusCondVar               *cond,
				     DBusMutex                 *mutex,
				     int                        timeout_milliseconds)
{
  struct timeval time_now;
  struct timespec end_time;
  int result;
  
  gettimeofday (&time_now, NULL);
  
  end_time.tv_sec = time_now.tv_sec + timeout_milliseconds / 1000;
  end_time.tv_nsec = (time_now.tv_usec + (timeout_milliseconds % 1000) * 1000) * 1000;
  if (end_time.tv_nsec > 1000*1000*1000)
    {
      end_time.tv_sec += 1;
      end_time.tv_nsec -= 1000*1000*1000;
    }
  
  result = pthread_cond_timedwait ((pthread_cond_t *) cond,
				   (pthread_mutex_t *) mutex,
				   &end_time);
  return result == ETIMEDOUT;
}

static void
_dbus_internal_condvar_wake_one (DBusCondVar *cond)
{
  pthread_cond_signal ((pthread_cond_t *)cond);
}

static void
_dbus_internal_condvar_wake_all (DBusCondVar *cond)
{
  pthread_cond_broadcast ((pthread_cond_t *)cond);
}

#endif

static const DBusThreadFunctions internal_functions =
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
  _dbus_internal_mutex_new,
  _dbus_internal_mutex_free,
  _dbus_internal_mutex_lock,
  _dbus_internal_mutex_unlock,
  _dbus_internal_condvar_new,
  _dbus_internal_condvar_free,
  _dbus_internal_condvar_wait,
  _dbus_internal_condvar_wait_timeout,
  _dbus_internal_condvar_wake_one,
  _dbus_internal_condvar_wake_all
};

/**
 *
 * Calls dbus_threads_init() with a default set of
 * #DBusThreadFunctions appropriate for the platform.
 *
 * @returns #TRUE on success, #FALSE if not enough memory
 */
dbus_bool_t
dbus_threads_init_default (void)
{
#ifdef USE_WIN32_THREADS
  /* We reuse this over several generations, because we can't
   * free the events once they are in use
   */
  if (dbus_cond_event_tls == TLS_OUT_OF_INDEXES)
    {
      dbus_cond_event_tls = TlsAlloc ();
      if (dbus_cond_event_tls == TLS_OUT_OF_INDEXES)
	return FALSE;
    }
#endif
  
  return dbus_threads_init (&internal_functions);
}


/** @} */

#ifdef DBUS_BUILD_TESTS
/** Fake mutex used for debugging */
typedef struct DBusFakeMutex DBusFakeMutex;
/** Fake mutex used for debugging */
struct DBusFakeMutex
{
  dbus_bool_t locked; /**< Mutex is "locked" */
};	

static DBusMutex *  dbus_fake_mutex_new            (void);
static void         dbus_fake_mutex_free           (DBusMutex   *mutex);
static dbus_bool_t  dbus_fake_mutex_lock           (DBusMutex   *mutex);
static dbus_bool_t  dbus_fake_mutex_unlock         (DBusMutex   *mutex);
static DBusCondVar* dbus_fake_condvar_new          (void);
static void         dbus_fake_condvar_free         (DBusCondVar *cond);
static void         dbus_fake_condvar_wait         (DBusCondVar *cond,
                                                    DBusMutex   *mutex);
static dbus_bool_t  dbus_fake_condvar_wait_timeout (DBusCondVar *cond,
                                                    DBusMutex   *mutex,
                                                    int          timeout_msec);
static void         dbus_fake_condvar_wake_one     (DBusCondVar *cond);
static void         dbus_fake_condvar_wake_all     (DBusCondVar *cond);


static const DBusThreadFunctions fake_functions =
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
  dbus_fake_mutex_new,
  dbus_fake_mutex_free,
  dbus_fake_mutex_lock,
  dbus_fake_mutex_unlock,
  dbus_fake_condvar_new,
  dbus_fake_condvar_free,
  dbus_fake_condvar_wait,
  dbus_fake_condvar_wait_timeout,
  dbus_fake_condvar_wake_one,
  dbus_fake_condvar_wake_all
};

static DBusMutex *
dbus_fake_mutex_new (void)
{
  DBusFakeMutex *mutex;

  mutex = dbus_new0 (DBusFakeMutex, 1);

  return (DBusMutex *)mutex;
}

static void
dbus_fake_mutex_free (DBusMutex *mutex)
{
  DBusFakeMutex *fake = (DBusFakeMutex*) mutex;

  _dbus_assert (!fake->locked);
  
  dbus_free (fake);
}

static dbus_bool_t
dbus_fake_mutex_lock (DBusMutex *mutex)
{
  DBusFakeMutex *fake = (DBusFakeMutex*) mutex;

  _dbus_assert (!fake->locked);

  fake->locked = TRUE;
  
  return TRUE;
}

static dbus_bool_t
dbus_fake_mutex_unlock (DBusMutex *mutex)
{
  DBusFakeMutex *fake = (DBusFakeMutex*) mutex;

  _dbus_assert (fake->locked);

  fake->locked = FALSE;
  
  return TRUE;
}

static DBusCondVar*
dbus_fake_condvar_new (void)
{
  return (DBusCondVar*) _dbus_strdup ("FakeCondvar");
}

static void
dbus_fake_condvar_free (DBusCondVar *cond)
{
  dbus_free (cond);
}

static void
dbus_fake_condvar_wait (DBusCondVar *cond,
                        DBusMutex   *mutex)
{
  
}

static dbus_bool_t
dbus_fake_condvar_wait_timeout (DBusCondVar *cond,
                                DBusMutex   *mutex,
                                int         timeout_msec)
{
  return TRUE;
}

static void
dbus_fake_condvar_wake_one (DBusCondVar *cond)
{

}

static void
dbus_fake_condvar_wake_all (DBusCondVar *cond)
{

}

dbus_bool_t
_dbus_threads_init_debug (void)
{
  return dbus_threads_init (&fake_functions);
}

#endif /* DBUS_BUILD_TESTS */
