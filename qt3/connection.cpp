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
  Private( Connection *qq );
  void setConnection( DBusConnection *c );
  DBusConnection *connection;
  int connectionSlot;
  DBusError error;
  Integrator *integrator;
  int timeout;
  Connection *q;
};

Connection::Private::Private( Connection *qq )
  : connection( 0 ), connectionSlot( 0 ), integrator( 0 ),
    timeout( -1 ), q( qq )
{
  dbus_error_init( &error );
}

void Connection::Private::setConnection( DBusConnection *c )
{
  if (!c) {
    qDebug( "error: %s, %s", error.name, error.message );
    dbus_error_free( &error );
    return;
  }
  connection = c;
  integrator = new Integrator( c, q );
  connect( integrator, SIGNAL(readReady()), q, SLOT(dispatchRead()) );
}

Connection::Connection( QObject *parent )
  : QObject( parent )
{
  d = new Private( this );
}

Connection::Connection( const QString& host, QObject *parent )
  : QObject( parent )
{
  d = new Private( this );

  if ( !host.isEmpty() )
    init( host );
}

Connection::Connection( DBusBusType type, QObject* parent )
  : QObject( parent )
{
  d = new Private( this );
  d->setConnection( dbus_bus_get(type, &d->error) );
}

void Connection::init( const QString& host )
{
  d->setConnection( dbus_connection_open( host.ascii(), &d->error) );
  //dbus_connection_allocate_data_slot( &d->connectionSlot );
  //dbus_connection_set_data( d->connection, d->connectionSlot, 0, 0 );
}

bool Connection::isConnected() const
{
  return dbus_connection_get_is_connected( d->connection );
}

bool Connection::isAuthenticated() const
{
  return dbus_connection_get_is_authenticated( d->connection );
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
  d = new Private(this);
  d->setConnection(connection);
}

void Connection::send( const Message &m )
{
    dbus_connection_send(d->connection, m.message(), 0);
}

void Connection::sendWithReply( const Message& )
{
}

Message Connection::sendWithReplyAndBlock( const Message &m )
{
  DBusMessage *reply;
  reply = dbus_connection_send_with_reply_and_block( d->connection, m.message(), d->timeout, &d->error );
  if (dbus_error_is_set(&d->error)) {
      qDebug("error: %s, %s", d->error.name, d->error.message);
      dbus_error_free(&d->error);
  }
  return Message( reply );
}

void* Connection::virtual_hook( int, void*  )
{
}

void Connection::dbus_connection_setup_with_qt_main (DBusConnection *connection)
{
  d->setConnection( connection );
}



/////////////////////////////////////////////////////////

#include "connection.moc"
