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

typedef struct DBusTypeReader      DBusTypeReader;
typedef struct DBusTypeWriter      DBusTypeWriter;
typedef struct DBusTypeReaderClass DBusTypeReaderClass;

struct DBusTypeReader
{
  int byte_order;
  const DBusString *type_str;
  int type_pos;
  const DBusString *value_str;
  int value_pos;

  const DBusTypeReaderClass *klass;
  union
  {
    struct {
      int start_pos;
      dbus_uint32_t len;
      int element_type;
    } array;

    struct {
      int len_pos;

    } dict;

    struct {
      dbus_uint32_t finished : 1;
    } strct;
  } u;
};

struct DBusTypeWriter
{
  int byte_order;
  DBusString *type_str;
  int type_pos;
  DBusString *value_str;
  int value_pos;

  dbus_uint32_t inside_array : 1;

  int container_type;
  union
  {
    struct {
      int start_pos; /* first element */
      int len_pos;
      int element_type_pos; /* position in type_str of array element type */
      int element_type_len;
    } array;

    struct {
      int len_pos;

    } dict;
  } u;
};

void        _dbus_type_reader_init                (DBusTypeReader    *reader,
                                                   int                byte_order,
                                                   const DBusString  *type_str,
                                                   int                type_pos,
                                                   const DBusString  *value_str,
                                                   int                value_pos);
int         _dbus_type_reader_get_current_type    (DBusTypeReader    *reader);
dbus_bool_t _dbus_type_reader_array_is_empty      (DBusTypeReader    *reader);
void        _dbus_type_reader_read_basic          (DBusTypeReader    *reader,
                                                   void              *value);
dbus_bool_t _dbus_type_reader_read_array_of_basic (DBusTypeReader    *reader,
                                                   int                type,
                                                   void             **array,
                                                   int               *array_len);
void        _dbus_type_reader_recurse             (DBusTypeReader    *reader,
                                                   DBusTypeReader    *subreader);
dbus_bool_t _dbus_type_reader_next                (DBusTypeReader    *reader);

void        _dbus_type_writer_init          (DBusTypeWriter *writer,
                                             int             byte_order,
                                             DBusString     *type_str,
                                             int             type_pos,
                                             DBusString     *value_str,
                                             int             value_pos);
dbus_bool_t _dbus_type_writer_write_basic   (DBusTypeWriter *writer,
                                             int             type,
                                             const void     *value);
dbus_bool_t _dbus_type_writer_write_array   (DBusTypeWriter *writer,
                                             int             type,
                                             const void     *array,
                                             int             array_len);
dbus_bool_t _dbus_type_writer_recurse       (DBusTypeWriter *writer,
                                             int             container_type,
                                             DBusTypeWriter *sub);
dbus_bool_t _dbus_type_writer_recurse_array (DBusTypeWriter *writer,
                                             const char     *element_type,
                                             DBusTypeWriter *sub);
dbus_bool_t _dbus_type_writer_unrecurse     (DBusTypeWriter *writer,
                                             DBusTypeWriter *sub);


#endif /* DBUS_MARSHAL_RECURSIVE_H */
