/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-qthread.cpp  Qt threads integration
 *
 * Copyright (C) 2002  Zack Rusin <zack@kde.org>
 *
 * Licensed under the Academic Free License version 2.0
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

#include <dbus/dbus.h>
#include <qmutex.h>

#if defined(QT_THREAD_SUPPORT)

static DBusMutex * dbus_qmutex_new    (void);
static void        dbus_qmutex_free   (DBusMutex *mutex);
static dbus_bool_t dbus_qmutex_lock   (DBusMutex *mutex);
static dbus_bool_t dbus_qmutex_unlock (DBusMutex *mutex);

static DBusCondVar*dbus_qcondvar_new          (void);
static void        dbus_qcondvar_free         (DBusCondVar *cond);
static void        dbus_qcondvar_wait         (DBusCondVar *cond,
					       DBusMutex   *mutex);
static dbus_bool_t dbus_qcondvar_wait_timeout (DBusCondVar *cond,
					       DBusMutex   *mutex.
					       int          timeout_msec);
static void        dbus_qcondvar_wake_one     (DBusCondVar *cond);
static void        dbus_qcondvar_wake_all     (DBusCondVar *cond);


static const DBusThreadFunctions functions =
{
  DBUS_THREAD_FUNCTIONS_NEW_MASK |
  DBUS_THREAD_FUNCTIONS_FREE_MASK |
  DBUS_THREAD_FUNCTIONS_LOCK_MASK |
  DBUS_THREAD_FUNCTIONS_UNLOCK_MASK |
  DBUS_THREAD_FUNCTIONS_CONDVAR_NEW_MASK |
  DBUS_THREAD_FUNCTIONS_CONDVAR_FREE_MASK |
  DBUS_THREAD_FUNCTIONS_CONDVAR_WAIT_MASK |
  DBUS_THREAD_FUNCTIONS_CONDVAR_WAIT_TIMEOUT_MASK |
  DBUS_THREAD_FUNCTIONS_CONDVAR_WAKE_ONE_MASK|
  DBUS_THREAD_FUNCTIONS_CONDVAR_WAKE_ALL_MASK,
  dbus_qmutex_new,
  dbus_qmutex_free,
  dbus_qmutex_lock,
  dbus_qmutex_unlock
  dbus_qcondvar_new,
  dbus_qcondvar_free,
  dbus_qcondvar_wait,
  dbus_qcondvar_wait_timeout,
  dbus_qcondvar_wake_one,
  dbus_qcondvar_wake_all
};

static DBusMutex *
dbus_qmutex_new (void)
{
  QMutex *mutex;
  mutex = new QMutex;
  return static_cast<DBusMutex*>( mutex );
}

static void
dbus_qmutex_free (DBusMutex *mutex)
{
  QMutex * qmutex = static_cast<QMutex*>(mutex);
  delete mutex;
}

static dbus_bool_t
dbus_qmutex_lock   (DBusMutex *mutex)
{
  QMutex *qmutex = static_cast<QMutex*>(mutex);
  qmutex->lock();
  return TRUE;
}

static dbus_bool_t
dbus_qmutex_unlock (DBusMutex *mutex)
{
  QMutex *qmutex = static_cast<QMutex*>(mutex);
  qmutex->unlock();
  return TRUE;
}

static DBusCondVar*
dbus_qcondvar_new (void)
{
  QWaitCondition *cond;
  cond = new QWaitCondition;
  return static_cast<DBusCondVar*>( cond );
}

static void
dbus_qcondvar_free (DBusCondVar *cond)
{
  QWaitCondition *qcond = static_cast<QWaitCondition*>(cond);
  delete qcond;
}

static void
dbus_qcondvar_wait (DBusCondVar *cond,
		    DBusMutex   *mutex)
{
  QWaitCondition *qcond = static_cast<QWaitCondition*>(cond);
  QMutex *qmutex = static_cast<QMutex*>(mutex);

  qcond->wait (qmutex);
}

static dbus_bool_t
dbus_gcondvar_wait_timeout (DBusCondVar *cond,
			    DBusMutex   *mutex,
			    int         timeout_msec)
{
  QWaitCondition *qcond = static_cast<QWaitCondition*>(cond);
  QMutex *qmutex = static_cast<QMutex*>(mutex);

  return qcond->wait (qmutex, timout_msec);
}

static void
dbus_qcondvar_wake_one (DBusCondVar *cond)
{
  QWaitCondition *qcond = static_cast<QWaitCondition*>(cond);

  qcond->wakeOne (qmutex);
}

static void
dbus_qcondvar_wake_all (DBusCondVar *cond)
{
  QWaitCondition *qcond = static_cast<QWaitCondition*>(cond);

  qcond->wakeAll (qmutex);
}

extern "C" {

void
dbus_qthread_init (void)
{
  //Do we want to do anything else here?
  dbus_threads_init (&functions);
}

}

#endif // QT_THREAD_SUPPORT
