// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* connection.cpp: Qt wrapper for DBusConnection
 *
 * Copyright (C) 2003  Zack Rusin <zack@kde.org>
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
  int timeout;
};

Connection::Connection( const QString& host, QObject *parent )
  : QObject( parent )
{
  d = new Private;

  if ( !host.isEmpty() )
    init( host );
}

void Connection::init( const QString& host )
{
  dbus_error_init( &d->error );
  d->timeout = -1;
  d->connection = dbus_connection_open( host.ascii(), &d->error );
  d->integrator = new Integrator( d->connection, this );
  connect( d->integrator, SIGNAL(readReady()),
           SLOT(dispatchRead()) );
  //dbus_connection_allocate_data_slot( &d->connectionSlot );
  //dbus_connection_set_data( d->connection, d->connectionSlot, 0, 0 );
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

Connection::Connection( DBusConnection *connection, QObject *parent  )
  : QObject( parent )
{
  d = new Private;
  dbus_error_init( &d->error );
  d->timeout = -1;
  d->connection = connection;
  d->integrator = new Integrator( d->connection, this );
  connect( d->integrator, SIGNAL(readReady()),
           SLOT(dispatchRead()) );
}

void Connection::send( const Message& )
{
}

void Connection::sendWithReply( const Message& )
{
}

Message Connection::sendWithReplyAndBlock( const Message &m )
{
  DBusMessage *reply;
  reply = dbus_connection_send_with_reply_and_block( d->connection, m.message(), d->timeout, &d->error );
  return Message( reply );
}

void* Connection::virtual_hook( int, void*  )
{
}

/////////////////////////////////////////////////////////

#include "connection.moc"
