/* -*- mode: C++; c-file-style: "gnu" -*- */
/* message.cpp: Qt wrapper for DBusMessage
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
#include "message.h"


namespace DBus {

struct Message::iterator::IteratorData {
  DBusMessageIter *iter;
  QVariant         var;
  bool             end;
};

/**
 * Iterator.
 */
Message::iterator::iterator()
{
  d = new IteratorData;
  d->iter = 0; d->end = true;
}

/**
 * Constructs iterator for the message.
 * @param msg message whose fields we want to iterate
 */
Message::iterator::iterator( DBusMessage* msg)
{
  d = new IteratorData;
  dbus_message_iter_init( msg, d->iter );
  d->end = false;
}

/**
 * Copy constructor for the iterator.
 * @param itr iterator
 */
Message::iterator::iterator( const iterator& itr )
{
  d = new IteratorData;
  d->iter = itr.d->iter;
  d->var  = itr.d->var;
  d->end  = itr.d->end;
}

/**
 * Destructor.
 */
Message::iterator::~iterator()
{
  delete d; d=0;
}

/**
 * Creates an iterator equal to the @p itr iterator
 * @param itr other iterator
 * @return
 */
Message::iterator&
Message::iterator::operator=( const iterator& itr )
{
  IteratorData *tmp = new IteratorData;
  tmp->iter = itr.d->iter;
  tmp->var  = itr.d->var;
  tmp->end  = itr.d->end;
  delete d; d=tmp;
  return *this;
}

/**
 * Returns the constant QVariant held by the iterator.
 * @return the constant reference to QVariant held by this iterator
 */
const QVariant&
Message::iterator::operator*() const
{
  return d->var;
}

/**
 * Returns the QVariant held by the iterator.
 * @return reference to QVariant held by this iterator
 */
QVariant&
Message::iterator::operator*()
{
  return d->var;
}

/**
 * Moves to the next field and return a reference to itself after
 * incrementing.
 * @return reference to self after incrementing
 */
Message::iterator&
Message::iterator::operator++()
{
  if ( d->end )
    return *this;

  if (  dbus_message_iter_next( d->iter ) ) {
    fillVar();
  } else {
    d->end = true;
    d->var = QVariant();
  }
  return *this;
}

/**
 * Moves to the next field and returns self before incrementing.
 * @return self before incrementing
 */
Message::iterator
Message::iterator::operator++(int)
{
  iterator itr( *this );
  operator++();
  return itr;
}

/**
 * Compares this iterator to @p it iterator.
 * @param it the iterator to which we're comparing this one to
 * @return true if they're equal, false otherwise
 */
bool
Message::iterator::operator==( const iterator& it )
{
  if ( d->end == it.d->end ) {
    if ( d->end == true ) {
      return true;
    } else {
      return d->var == it.d->var;
    }
  } else
    return false;
}

/**
 * Compares two iterators.
 * @param it The other iterator.
 * @return true if two iterators are not equal, false
 *         otherwise
 */
bool
Message::iterator::operator!=( const iterator& it )
{
  return !operator==( it );
}

/**
 * Fills QVariant based on what current DBusMessageIter helds.
 */
void
Message::iterator::fillVar()
{
  switch ( dbus_message_iter_get_arg_type( d->iter ) ) {
  case DBUS_TYPE_INT32:
    d->var = QVariant( dbus_message_iter_get_int32( d->iter ) );
    break;
  case DBUS_TYPE_UINT32:
    d->var = QVariant( dbus_message_iter_get_uint32( d->iter ) );
    break;
  case DBUS_TYPE_DOUBLE:
    d->var = QVariant( dbus_message_iter_get_double( d->iter ) );
    break;
  case DBUS_TYPE_STRING:
    d->var = QVariant( QString(dbus_message_iter_get_string( d->iter )) );
    break;
  default:
    qDebug( "not implemented" );
    d->var = QVariant();
    break;
  }
}

/**
 * Returns a QVariant help by this iterator.
 * @return QVariant held by this iterator
 */
QVariant
Message::iterator::var() const
{
  return d->var;
}

struct Message::MessagePrivate {
  DBusMessage *msg;
};

/**
 *
 */
Message::Message( int messageType )
{
  d = new MessagePrivate;
  d->msg = dbus_message_new( messageType );
}

/**
 * Constructs a new Message with the given service and name.
 * @param service service service that the message should be sent to
 * @param name name of the message
 */
Message::Message( const QString& service, const QString& path,
                  const QString& interface, const QString& method )
{
  d = new MessagePrivate;
  d->msg = dbus_message_new_method_call( service.latin1(), path.latin1(),
                                         interface.latin1(), method.latin1() );
}

/**
 * Constructs a message that is a reply to some other
 * message.
 * @param name the name of the message
 * @param replayingTo original_message the message which the created
 * message is a reply to.
 */
Message::Message( const Message& replayingTo )
{
  d = new MessagePrivate;
  d->msg = dbus_message_new_method_return( replayingTo.d->msg );
}

Message:: Message( const QString& path, const QString& interface,
                   const QString& name )
{
  d = new MessagePrivate;
  d->msg = dbus_message_new_signal( path.ascii(), interface.ascii(),
                                    name.ascii() );
}

Message::Message( const Message& replayingTo, const QString& errorName,
                  const QString& errorMessage )
{
  d = new MessagePrivate;
  d->msg = dbus_message_new_error( replayingTo.d->msg, errorName.utf8(),
                                   errorMessage.utf8() );
}

Message Message::operator=( const Message& other )
{
  //FIXME: ref the other.d->msg instead of copying it?
}
/**
 * Destructs message.
 */
Message::~Message()
{
  dbus_message_unref( d->msg );
  delete d; d=0;
}

int Message::type() const
{
  return dbus_message_get_type( d->msg );
}

void Message::setPath( const QString& path )
{
  dbus_message_set_path( d->msg, path.ascii() );
}

QString Message::path() const
{
  return dbus_message_get_path( d->msg );
}

void Message::setInterface( const QString& iface )
{
  dbus_message_set_interface( d->msg, iface.ascii() );
}

QString Message::interface() const
{
  return dbus_message_get_interface( d->msg );
}

void Message::setMember( const QString& member )
{
  dbus_message_set_member( d->msg, member.ascii() );
}

QString Message::member() const
{
  return dbus_message_get_member( d->msg );
}

void Message::setErrorName( const QString& err )
{
  dbus_message_set_error_name( d->msg, err );
}

QString Message::errorName() const
{
  return dbus_message_get_error_name( d->msg );
}

void Message::setDestination( const QString& dest )
{
  dbus_message_set_destination( d->msg, dest );
}

QString Message::destination() const
{
  return dbus_message_get_destination( d->msg );
}

/**
 * Sets the message sender.
 * @param sender the sender
 * @return false if unsuccessful
 */
bool
Message::setSender( const QString& sender )
{
  return dbus_message_set_sender( d->msg, sender.latin1() );
}

/**
 * Returns sender of this message.
 * @return sender
 */
QString
Message::sender() const
{
  return dbus_message_get_sender( d->msg );
}

QString Message::signature() const
{
  return dbus_message_get_signature( d->msg );
}

/**
 * Message can be casted to DBusMessage* to make it easier to
 * use it with raw DBus.
 * @return underlying DBusMessage*
 */
Message::operator DBusMessage*() const
{
  return d->msg;
}

/**
 * Appends data to this message. It can be anything QVariant accepts.
 * @param var Data to append
 */
void
Message::append( const QVariant& var )
{
  switch ( var.type() ) {
  case QVariant::Int:
    dbus_message_append_int32( d->msg, var.toInt() );
    break;
  case QVariant::UInt:
    dbus_message_append_uint32( d->msg, var.toUInt() );
    break;
  case QVariant::String: //what about QVariant::CString ?
    dbus_message_append_string( d->msg, var.toString() );
    break;
  case QVariant::Double:
    dbus_message_append_double( d->msg, var.toDouble() );
    break;
  case QVariant::Invalid:
    break;
  default: // handles QVariant::ByteArray
    QByteArray a;
    QDataStream stream( a, IO_WriteOnly );
    stream<<var;
    dbus_message_append_byte_array( d->msg, a.data(), a.size() );
  }
}


/**
 * Returns the starting iterator for the fields of this
 * message.
 * @return starting iterator
 */
Message::iterator
Message::begin() const
{
  return iterator( d->msg );
}

/**
 * Returns the ending iterator for the fields of this
 * message.
 * @return ending iterator
 */
Message::iterator
Message::end() const
{
  return iterator();
}

/**
 * Returns the field at position @p i
 * @param i position of the wanted field
 * @return QVariant at position @p i or an empty QVariant
 */
QVariant
Message::at( int i )
{
  iterator itr( d->msg );

  while ( i-- ) {
    if ( itr == end() )
      return QVariant();//nothing there
    ++itr;
  }
  return *itr;
}

/**
 * The underlying DBusMessage of this class.
 * @return DBusMessage pointer.
 */
DBusMessage*
Message::message() const
{
  return d->msg;
}

}
