// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* server.h: Qt wrapper for DBusServer
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
#include "server.h"
#include "connection.h"

#include "integrator.h"
using DBusQt::Internal::Integrator;

namespace DBusQt
{

struct Server::Private {
  Private() : integrator( 0 ), server( 0 )
    {}

  Integrator *integrator;
  DBusServer *server;
  DBusError error;
};

Server::Server( const QString& addr, QObject *parent )
  : QObject( parent )
{
  d = new Private;

  if ( !addr.isEmpty() ) {
    init( addr );
  }
}

Server::~Server()
{
  delete d;
}

bool Server::isConnected() const
{
  return dbus_server_get_is_connected( d->server );
}

void Server::disconnect()
{
  dbus_server_disconnect( d->server );
}

QString Server::address() const
{
  //FIXME: leak?
  return dbus_server_get_address( d->server );
}

void Server::listen( const QString& addr )
{
  if ( !d->server ) {
    init( addr );
  }
}

void Server::init( const QString& addr )
{
  d->server = dbus_server_listen( addr.ascii(),  &d->error );
  d->integrator = new Integrator( d->server, this );
  connect( d->integrator, SIGNAL(newConnection(Connection*)),
           SIGNAL(newConnection(Connection*)) );
}

}


#include "server.moc"
