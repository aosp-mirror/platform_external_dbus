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

#include <kdebug.h>

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
  d->iter = dbus_message_get_fields_iter( msg );
  d->end = false;
}

/**
 * Copy constructor for the iterator.
 * @param itr iterator
 */
Message::iterator::iterator( const iterator& itr )
{
  d = new IteratorData;
  dbus_message_iter_ref( itr.d->iter );
  d->iter = itr.d->iter;
  d->var  = itr.d->var;
  d->end  = itr.d->end;
}

/**
 * Destructor.
 */
Message::iterator::~iterator()
{
  dbus_message_iter_unref( d->iter );
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
  //in case we'll ever go fot exception safety
  dbus_message_iter_ref( itr.d->iter );
  IteratorData *tmp = new IteratorData;
  tmp->iter = itr.d->iter;
  tmp->var  = itr.d->var;
  tmp->end  = itr.d->end;
  dbus_message_iter_unref( d->iter );
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
  switch ( dbus_message_iter_get_field_type( d->iter ) ) {
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
  case DBUS_TYPE_BYTE_ARRAY:
    {
      QByteArray a;
      int len;
      char *ar;
      ar = reinterpret_cast<char*>( dbus_message_iter_get_byte_array( d->iter, &len ) );
      a.setRawData( ar, len );
      QDataStream stream( a, IO_ReadOnly );
      stream >> d->var;
      a.resetRawData( ar, len );
    }
    break;
  case DBUS_TYPE_STRING_ARRAY:
#warning "String array not implemented"
    //d->var = QVariant( dbus_message_iter_get_string_array );
    break;
  default:
    kdWarning()<<k_funcinfo<<" Serious problem!! "<<endl;
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
 * Constructs a new Message with the given service and name.
 * @param service service service that the message should be sent to
 * @param name name of the message
 */
Message::Message( const QString& service, const QString& name )
{
  d = new MessagePrivate;
  d->msg = dbus_message_new( service.latin1(), name.latin1() );
}

/**
 * Constructs a message that is a reply to some other
 * message.
 * @param name the name of the message
 * @param replayingTo original_message the message which the created
 * message is a reply to.
 */
Message::Message( const QString& name, const Message& replayingTo )
{
  d = new MessagePrivate;
  d->msg = dbus_message_new_reply( name.latin1(), replayingTo );
}

/**
 * Creates a message just like @p other
 * @param other the copied message
 */
Message::Message( const Message& other )
{
  d = new MessagePrivate;
  d->msg = dbus_message_new_from_message( other );
}

/**
 * Destructs message.
 */
Message::~Message()
{
  dbus_message_unref( d->msg );
  delete d; d=0;
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
 * Sets a flag indicating that the message is an error reply
 * message, i.e. an "exception" rather than a normal response.
 * @param error true if this is an error message.
 */
void
Message::setError( bool error )
{
  return dbus_message_set_is_error( d->msg, error );
}

/**
 * Returns name of this message.
 * @return name
 */
QString
Message::name() const
{
  return dbus_message_get_name( d->msg );
}

/**
 * Returns service associated with this message.
 * @return service
 */
QString
Message::service() const
{
  return dbus_message_get_service( d->msg );
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

/**
 * Checks whether this message is an error indicating message.
 * @return true if this is an error message
 */
bool
Message::isError() const
{
  return dbus_message_get_is_error( d->msg );
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
