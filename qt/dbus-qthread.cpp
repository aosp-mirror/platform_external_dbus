/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-qthread.cpp  Qt threads integration
 *
 * Copyright (C) 2002  Zack Rusin <zack@kde.org>
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

#include <dbus/dbus.h>
#include <qmutex.h>

#if defined(QT_THREAD_SUPPORT)

static DBusMutex * dbus_qmutex_new    (void);
static void        dbus_qmutex_free   (DBusMutex *mutex);
static dbus_bool_t dbus_qmutex_lock   (DBusMutex *mutex);
static dbus_bool_t dbus_qmutex_unlock (DBusMutex *mutex);


static const DBusThreadFunctions functions =
{
  DBUS_THREAD_FUNCTIONS_NEW_MASK |
  DBUS_THREAD_FUNCTIONS_FREE_MASK |
  DBUS_THREAD_FUNCTIONS_LOCK_MASK |
  DBUS_THREAD_FUNCTIONS_UNLOCK_MASK,
  dbus_qmutex_new,
  dbus_qmutex_free,
  dbus_qmutex_lock,
  dbus_qmutex_unlock
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

extern "C" {

void
dbus_qthread_init (void)
{
  //Do we want to do anything else here?
  dbus_threads_init (&functions);
}

}

#endif // QT_THREAD_SUPPORT
