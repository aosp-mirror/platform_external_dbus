/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-sysdeps-pthread.c Implements threads using pthreads (internal to libdbus)
 * 
 * Copyright (C) 2002, 2003, 2006  Red Hat, Inc.
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

#include "dbus-internals.h"
#include "dbus-sysdeps.h"
#include "dbus-threads.h"

#include <sys/time.h>
#include <pthread.h>
#include <string.h>

typedef struct {
  pthread_mutex_t lock;
} DBusMutexPThread;

typedef struct {
  pthread_cond_t cond;
} DBusCondVarPThread;

#define DBUS_MUTEX(m)         ((DBusMutex*) m)
#define DBUS_MUTEX_PTHREAD(m) ((DBusMutexPThread*) m)

#define DBUS_COND_VAR(c)         ((DBusCondVar*) c)
#define DBUS_COND_VAR_PTHREAD(c) ((DBusCondVarPThread*) c)


#define PTHREAD_CHECK(func_name, result_or_call) do {                                  \
    int tmp = (result_or_call);                                                        \
    if (tmp != 0) {                                                                    \
      _dbus_warn_check_failed ("pthread function %s failed with %d %s in %s\n",        \
                               func_name, tmp, strerror(tmp), _DBUS_FUNCTION_NAME);    \
    }                                                                                  \
} while (0)
            
static DBusMutex*
_dbus_pthread_mutex_new (void)
{
  DBusMutexPThread *pmutex;
  int result;
  
  pmutex = dbus_new (DBusMutexPThread, 1);
  if (pmutex == NULL)
    return NULL;

  result = pthread_mutex_init (&pmutex->lock, NULL);

  if (result == ENOMEM || result == EAGAIN)
    {
      dbus_free (pmutex);
      return NULL;
    }
  else
    {
      PTHREAD_CHECK ("pthread_mutex_init", result);
    }

  return DBUS_MUTEX (pmutex);
}

static void
_dbus_pthread_mutex_free (DBusMutex *mutex)
{
  DBusMutexPThread *pmutex = DBUS_MUTEX_PTHREAD (mutex);
  
  PTHREAD_CHECK ("pthread_mutex_destroy", pthread_mutex_destroy (&pmutex->lock));

  dbus_free (pmutex);
}

static dbus_bool_t
_dbus_pthread_mutex_lock (DBusMutex *mutex)
{
  DBusMutexPThread *pmutex = DBUS_MUTEX_PTHREAD (mutex);

  PTHREAD_CHECK ("pthread_mutex_lock", pthread_mutex_lock (&pmutex->lock));

  return TRUE;
}

static dbus_bool_t
_dbus_pthread_mutex_unlock (DBusMutex *mutex)
{
  DBusMutexPThread *pmutex = DBUS_MUTEX_PTHREAD (mutex);

  PTHREAD_CHECK ("pthread_mutex_unlock", pthread_mutex_unlock (&pmutex->lock));

  return TRUE;
}

static DBusCondVar *
_dbus_pthread_condvar_new (void)
{
  DBusCondVarPThread *pcond;
  int result;
  
  pcond = dbus_new (DBusCondVarPThread, 1);
  if (pcond == NULL)
    return NULL;

  result = pthread_cond_init (&pcond->cond, NULL);

  if (result == EAGAIN || result == ENOMEM)
    {
      dbus_free (pcond);
      return NULL;
    }
  else
    {
      PTHREAD_CHECK ("pthread_cond_init", result);
    }
  
  return DBUS_COND_VAR (pcond);
}

static void
_dbus_pthread_condvar_free (DBusCondVar *cond)
{  
  DBusCondVarPThread *pcond = DBUS_COND_VAR_PTHREAD (cond);
  
  PTHREAD_CHECK ("pthread_cond_destroy", pthread_cond_destroy (&pcond->cond));

  dbus_free (pcond);
}

static void
_dbus_pthread_condvar_wait (DBusCondVar *cond,
                            DBusMutex   *mutex)
{
  DBusMutexPThread *pmutex = DBUS_MUTEX_PTHREAD (mutex);
  DBusCondVarPThread *pcond = DBUS_COND_VAR_PTHREAD (cond);
  
  PTHREAD_CHECK ("pthread_cond_wait", pthread_cond_wait (&pcond->cond, &pmutex->lock));
}

static dbus_bool_t
_dbus_pthread_condvar_wait_timeout (DBusCondVar               *cond,
                                    DBusMutex                 *mutex,
                                    int                        timeout_milliseconds)
{
  DBusMutexPThread *pmutex = DBUS_MUTEX_PTHREAD (mutex);
  DBusCondVarPThread *pcond = DBUS_COND_VAR_PTHREAD (cond);
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

  result = pthread_cond_timedwait (&pcond->cond, &pmutex->lock, &end_time);

  if (result != ETIMEDOUT)
    {
      PTHREAD_CHECK ("pthread_cond_timedwait", result);
    }
  
  /* return true if we did not time out */
  return result != ETIMEDOUT;
}

static void
_dbus_pthread_condvar_wake_one (DBusCondVar *cond)
{
  DBusCondVarPThread *pcond = DBUS_COND_VAR_PTHREAD (cond);

  PTHREAD_CHECK ("pthread_cond_signal", pthread_cond_signal (&pcond->cond));
}

static void
_dbus_pthread_condvar_wake_all (DBusCondVar *cond)
{
  DBusCondVarPThread *pcond = DBUS_COND_VAR_PTHREAD (cond);
  
  PTHREAD_CHECK ("pthread_cond_broadcast", pthread_cond_broadcast (&pcond->cond));
}

static const DBusThreadFunctions pthread_functions =
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
  _dbus_pthread_mutex_new,
  _dbus_pthread_mutex_free,
  _dbus_pthread_mutex_lock,
  _dbus_pthread_mutex_unlock,
  _dbus_pthread_condvar_new,
  _dbus_pthread_condvar_free,
  _dbus_pthread_condvar_wait,
  _dbus_pthread_condvar_wait_timeout,
  _dbus_pthread_condvar_wake_one,
  _dbus_pthread_condvar_wake_all
};

dbus_bool_t
_dbus_threads_init_platform_specific (void)
{
  return dbus_threads_init (&pthread_functions);
}
