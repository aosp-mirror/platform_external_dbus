/* qdbusintegrator.cpp QDBusConnection private implementation
 *
 * Copyright (C) 2005 Harald Fernengel <harry@kdevelop.org>
 * Copyright (C) 2006 Trolltech AS. All rights reserved.
 *    Author: Thiago Macieira <thiago.macieira@trolltech.com>
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
 * along with this program; if not, write to the Free Software Foundation
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <QtCore/qmutex.h>
#include <QtCore/qwaitcondition.h>

#include <dbus/dbus.h>

struct DBusMutex: public QMutex
{
    inline DBusMutex()
        : QMutex( QMutex::NonRecursive )
    { }

    static DBusMutex* mutex_new()
    {
        return new DBusMutex;
    }
    
    static void mutex_free(DBusMutex *mutex)
    {
        delete mutex;
    }

    static dbus_bool_t mutex_lock(DBusMutex *mutex)
    {
        mutex->lock();
        return true;
    }

    static dbus_bool_t mutex_unlock(DBusMutex *mutex)
    {
        mutex->unlock();
        return true;
    }
};

struct DBusCondVar: public QWaitCondition
{
    inline DBusCondVar()
    { }

    static DBusCondVar* condvar_new()
    {
        return new DBusCondVar;
    }

    static void condvar_free(DBusCondVar *cond)
    {
        delete cond;
    }

    static void condvar_wait(DBusCondVar *cond, DBusMutex *mutex)
    {
        cond->wait(mutex);
    }

    static dbus_bool_t condvar_wait_timeout(DBusCondVar *cond, DBusMutex *mutex, int msec)
    {
        return cond->wait(mutex, msec);
    }

    static void condvar_wake_one(DBusCondVar *cond)
    {
        cond->wakeOne();
    }

    static void condvar_wake_all(DBusCondVar *cond)
    {
        cond->wakeAll();
    }
};

bool qDBusInitThreads()
{
    static DBusThreadFunctions fcn = {
        DBUS_THREAD_FUNCTIONS_ALL_MASK,
        DBusMutex::mutex_new,
        DBusMutex::mutex_free,
        DBusMutex::mutex_lock,
        DBusMutex::mutex_unlock,
        DBusCondVar::condvar_new,
        DBusCondVar::condvar_free,
        DBusCondVar::condvar_wait,
        DBusCondVar::condvar_wait_timeout,
        DBusCondVar::condvar_wake_one,
        DBusCondVar::condvar_wake_all,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    dbus_threads_init(&fcn);
    return true;
}

        
