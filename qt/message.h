/* -*- mode: C++; c-file-style: "gnu" -*- */
/* message.h: Qt wrapper for DBusMessage
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

#include <qvariant.h>
#include <qstring.h>
#include <dbus.h>

namespace DBus {

  class Message
  {
  public:
    class iterator {
    public:
      iterator();
      iterator( const iterator & );
      iterator( DBusMessage* msg );
      ~iterator();

      iterator& operator=( const iterator& );
      const QVariant& operator*() const;
      QVariant& operator*();
      iterator& operator++();
      iterator operator++(int);
      bool operator==( const iterator& it );
      bool operator!=( const iterator& it );

      QVariant var() const;
    private:
      void fillVar();
      struct IteratorData;
      IteratorData *d;
    };

    Message( const QString& service, const QString& name );
    Message( const QString& name,
             const Message& replayingTo );
    Message( const Message& other );

    virtual ~Message();

    bool    setSender( const QString& sender );
    void    setError( bool error );

    QString name() const;
    QString service() const;
    QString sender() const;
    bool    isError() const;

    virtual void append( const QVariant& var );

    operator DBusMessage*() const;

    iterator begin() const;
    iterator end() const;

    QVariant at( int i );

  protected:
    DBusMessage* message() const;

  private:
    struct MessagePrivate;
    MessagePrivate *d;
  };

}
