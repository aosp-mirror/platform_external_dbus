// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* connection.cpp: Qt wrapper for DBusConnection
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
#include "connection.h"

#include <qsocketnotifier.h>
#include <qintdict.h>

using namespace DBusQt;

struct QtWatch {
  QtWatch(): readSocket( 0 ), writeSocket( 0 ) { }

  DBusWatch *watch;
  QSocketNotifier *readSocket;
  QSocketNotifier *writeSocket;
};

struct Connection::Private
{
  DBusConnection *connection;
  int connectionSlot;
  DBusError error;
  QIntDict<QtWatch> watches;
};

Connection::Connection( const QString& host )
{
  d = new Private;
  dbus_error_init( &d->error );

  if ( !host.isEmpty() )
    init( host );
}

void Connection::init( const QString& host )
{
  d->connection = dbus_connection_open( host.ascii(), &d->error );
  dbus_connection_allocate_data_slot( &d->connectionSlot );
  dbus_connection_set_data( d->connection, d->connectionSlot, 0, 0 );
}

bool Connection::isConnected() const
{
}

bool Connection::isAuthenticated() const
{
}

void Connection::open( const QString& host )
{
  if ( host.isEmpty() ) return;

  init( host );
}

void Connection::close()
{
  dbus_connection_disconnect( d->connection );
}

void Connection::flush()
{
  dbus_connection_flush( d->connection );
}

void Connection::slotRead( int fd )
{
  Q_UNUSED( fd );
  while ( dbus_connection_dispatch( d->connection ) == DBUS_DISPATCH_DATA_REMAINS )
    ;
}

void Connection::slotWrite( int fd )
{
  Q_UNUSED( fd );
}

void Connection::addWatch( DBusWatch *watch )
{
  if ( !dbus_watch_get_enabled( watch ) )
    return;

  QtWatch *qtwatch = new QtWatch;
  qtwatch->watch = watch;

  int flags = dbus_watch_get_flags( watch );
  int fd = dbus_watch_get_fd( watch );

  if ( flags & DBUS_WATCH_READABLE ) {
    qtwatch->readSocket = new QSocketNotifier( fd, QSocketNotifier::Read, this );
    QObject::connect( qtwatch->readSocket, SIGNAL(activated(int)), SLOT(slotRead(int)) );
  }

  if (flags & DBUS_WATCH_WRITABLE) {
    qtwatch->writeSocket = new QSocketNotifier( fd, QSocketNotifier::Write, this );
    QObject::connect( qtwatch->writeSocket, SIGNAL(activated(int)), SLOT(slotWrite(int)) );
  }

  d->watches.insert( fd, qtwatch );

}

void Connection::removeWatch( DBusWatch *watch )
{
  int key = dbus_watch_get_fd( watch );

  QtWatch *qtwatch = d->watches.take( key );

  if ( qtwatch ) {
    delete qtwatch->readSocket;  qtwatch->readSocket = 0;
    delete qtwatch->writeSocket; qtwatch->writeSocket = 0;
    delete qtwatch;
  }
}


/////////////////////////////////////////////////////////
