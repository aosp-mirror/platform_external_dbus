/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-marshal-recursive.h  Marshalling routines for recursive types
 *
 * Copyright (C) 2004 Red Hat, Inc.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef DBUS_MARSHAL_RECURSIVE_H
#define DBUS_MARSHAL_RECURSIVE_H

#include <config.h>
#include <dbus/dbus-marshal-basic.h>

#ifndef PACKAGE
#error "config.h not included here"
#endif

/* Notes on my plan to implement this:
 * - also have DBusTypeWriter (heh)
 * - TypeReader has accessors for:
 *    . basic type
 *    . array of basic type (efficiency hack)
 *    . another type reader
 * - a dict will appear to be a list of string, whatever, string, whatever
 * - a variant will contain another TypeReader
 * - a struct will be a list of whatever, whatever, whatever
 *
 * So the basic API usage is to go next, next, next; if the
 * item is a basic type or basic array then read the item;
 * if it's another type reader then process it; if it's
 * a container type (struct, array, variant, dict) then
 * recurse.
 * 
 */

struct DBusTypeReader
{
  int byte_order;
  const DBusString *type_str;
  int type_pos;
  const DBusString *value_str;
  int value_pos;
};

typedef struct DBusTypeReader DBusTypeReader;

struct DBusTypeWriter
{
  int byte_order;
  DBusString *type_str;
  int type_pos;
  DBusString *value_str;
  int value_pos;
};

typedef struct DBusTypeWriter DBusTypeWriter;

void        _dbus_type_reader_init             (DBusTypeReader    *reader,
                                                int                byte_order,
                                                const DBusString  *type_str,
                                                int                type_pos,
                                                const DBusString  *value_str,
                                                int                value_pos);
int         _dbus_type_reader_get_value_end    (DBusTypeReader    *reader);
int         _dbus_type_reader_get_type_end     (DBusTypeReader    *reader);
int         _dbus_type_reader_get_current_type (DBusTypeReader    *reader);
int         _dbus_type_reader_get_array_type   (DBusTypeReader    *reader);
void        _dbus_type_reader_read_basic       (DBusTypeReader    *reader,
                                                void              *value);
dbus_bool_t _dbus_type_reader_read_array       (DBusTypeReader    *reader,
                                                int                type,
                                                void             **array,
                                                int               *array_len);
void        _dbus_type_reader_recurse          (DBusTypeReader    *reader);
void        _dbus_type_reader_unrecurse        (DBusTypeReader    *reader);
dbus_bool_t _dbus_type_reader_next             (DBusTypeReader    *reader);


void        _dbus_type_writer_init        (DBusTypeWriter *writer,
                                           int             byte_order,
                                           DBusString     *type_str,
                                           int             type_pos,
                                           DBusString     *value_str,
                                           int             value_pos);
dbus_bool_t _dbus_type_writer_write_basic (DBusTypeWriter *writer,
                                           int             type,
                                           const void     *value);
dbus_bool_t _dbus_type_writer_write_array (DBusTypeWriter *writer,
                                           int             type,
                                           const void     *array,
                                           int             array_len);
dbus_bool_t _dbus_type_writer_recurse     (DBusTypeWriter *writer,
                                           int             container_type);
dbus_bool_t _dbus_type_writer_unrecurse   (DBusTypeWriter *writer);

#endif /* DBUS_MARSHAL_RECURSIVE_H */
