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

using namespace DBusQt;

#include "integrator.h"
using Internal::Integrator;

struct Connection::Private
{
  DBusConnection *connection;
  int connectionSlot;
  DBusError error;
  Integrator *integrator;
};

Connection::Connection( const QString& host )
{
  d = new Private;
  d->integrator = new Integrator( this );
  connect( d->integrator, SIGNAL(readReady()),
           SLOT(dispatchRead()) );

  initDbus();

  if ( !host.isEmpty() )
    init( host );
}

void Connection::initDbus()
{
}

void Connection::init( const QString& host )
{
  dbus_error_init( &d->error );
  d->connection = dbus_connection_open( host.ascii(), &d->error );
  //dbus_connection_allocate_data_slot( &d->connectionSlot );
  //dbus_connection_set_data( d->connection, d->connectionSlot, 0, 0 );
  initDbus();
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

void Connection::dispatchRead()
{
  while ( dbus_connection_dispatch( d->connection ) == DBUS_DISPATCH_DATA_REMAINS )
    ;
}

DBusConnection* Connection::connection() const
{
  return d->connection;
}

/////////////////////////////////////////////////////////

#include "connection.moc"
