// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* integrator.h: integrates D-BUS into Qt event loop
 *
 * Copyright (C) 2003  Zack Rusin <zack@kde.org>
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
#ifndef DBUS_QT_INTEGRATOR_H
#define DBUS_QT_INTEGRATOR_H

#include <qobject.h>

#include <qintdict.h>
#include <qptrlist.h>

#include "dbus/dbus.h"

namespace DBusQt
{
  class Connection;

  namespace Internal
  {
    struct QtWatch;
    struct DBusQtTimeout;
    class Integrator : public QObject
    {
      Q_OBJECT
    public:
      Integrator( Connection* parent );

    signals:
      void readReady();

    protected slots:
      void slotRead( int );
      void slotWrite( int );

    protected:
      void addWatch( DBusWatch* );
      void removeWatch( DBusWatch* );

      void addTimeout( DBusTimeout* );
      void removeTimeout( DBusTimeout* );
    private:
      QIntDict<QtWatch> m_watches;
      QPtrList<DBusQtTimeout> m_timeouts;
      Connection* m_parent;

    private:
      friend dbus_bool_t dbusAddWatch( DBusWatch*, void* );
      friend void dbusRemoveWatch( DBusWatch*, void* );
      friend void dbusToggleWatch( DBusWatch*, void* );

      friend dbus_bool_t dbusAddTimeout( DBusTimeout*, void* );
      friend void dbusRemoveTimeout( DBusTimeout*, void* );
      friend void dbusToggleTimeout( DBusTimeout*, void* );
    };

    //////////////////////////////////////////////////////////////
    //Friends
    dbus_bool_t dbusAddWatch( DBusWatch *watch, void *data )
    {
      Integrator *con = static_cast<Integrator*>( data );
      con->addWatch( watch );
      return true;
    }
    void dbusRemoveWatch( DBusWatch *watch, void *data )
    {
      Integrator *con = static_cast<Integrator*>( data );
      con->removeWatch( watch );
    }

    void dbusToggleWatch( DBusWatch*, void* )
    {
      //I don't know how to handle this one right now
//#warning "FIXME: implement"
    }

    dbus_bool_t dbusAddTimeout( DBusTimeout *timeout, void *data )
    {
      Integrator *con = static_cast<Integrator*>( data );
      con->addTimeout( timeout );
      return true;
    }

    void dbusRemoveTimeout( DBusTimeout *timeout, void *data )
    {
      Integrator *con = static_cast<Integrator*>( data );
    }

    void dbusToggleTimeout( DBusTimeout *timeout, void *data )
    {
      Integrator *con = static_cast<Integrator*>( data );
    }
    /////////////////////////////////////////////////////////////
  }
}

#endif
