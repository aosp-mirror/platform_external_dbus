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
#include <dbus/dbus-protocol.h>
#include <dbus/dbus-list.h>
#include <dbus/dbus-marshal-basic.h> /* this can vanish when we merge */

#ifndef PACKAGE
#error "config.h not included here"
#endif

/* Features we need to port dbus-message:
 *  - memoize a position of a reader for small/fast access later
 *  - delete an array element and re-align the remainder of the array
 *    (not necessary yet to re-align remainder of entire string,
 *     though that's probably just as hard/easy)
 *    (really this one is to set a complex-type array element to
 *    a new value, but for dbus-message.c delete-and-reappend would
 *    be good enough)
 *  - set string, int, etc. values at a memoized position
 *    (implement generic set of any value? changes only
 *     value_str not type_str)
 *  - implement has_next()
 *  - the all-in-one-block array accessors
 *  - validation
 *
 * - remember to try a HAVE_INT64=0 build at the end
 */

typedef struct DBusTypeMark        DBusTypeMark;
typedef struct DBusTypeReader      DBusTypeReader;
typedef struct DBusTypeWriter      DBusTypeWriter;
typedef struct DBusTypeReaderClass DBusTypeReaderClass;
typedef struct DBusArrayLenFixup   DBusArrayLenFixup;

/* The mark is a way to compress a TypeReader; it isn't all that
 * successful though. The idea was to use this for caching header
 * fields in dbus-message.c. However now I'm thinking why not cache
 * the actual values (e.g. char*) and if the field needs to be set or
 * deleted, just linear search for it. Those operations are uncommon,
 * and getting the values should be fast and not involve all this type
 * reader nonsense.
 */
struct DBusTypeMark
{
  dbus_uint32_t type_pos_in_value_str : 1;
  dbus_uint32_t container_type : 3;
  dbus_uint32_t array_len_offset : 3; /* bytes back from start_pos that len ends */
  dbus_uint32_t type_pos : DBUS_MAXIMUM_MESSAGE_LENGTH_BITS;
  dbus_uint32_t value_pos : DBUS_MAXIMUM_MESSAGE_LENGTH_BITS;
  dbus_uint32_t array_start_pos : DBUS_MAXIMUM_MESSAGE_LENGTH_BITS;
};

struct DBusTypeReader
{
  dbus_uint32_t byte_order : 8;

  dbus_uint32_t finished : 1;   /* marks we're at end iterator for cases
                                 * where we don't have another way to tell
                                 */
  dbus_uint32_t array_len_offset : 3; /* bytes back from start_pos that len ends */
  const DBusString *type_str;
  int type_pos;
  const DBusString *value_str;
  int value_pos;

  const DBusTypeReaderClass *klass;
  union
  {
    struct {
      int start_pos;
    } array;
  } u;
};

struct DBusTypeWriter
{
  dbus_uint32_t byte_order : 8;

  dbus_uint32_t container_type : 8;

  dbus_uint32_t type_pos_is_expectation : 1; /* type_pos is an insertion point or an expected next type */

  dbus_uint32_t enabled : 1; /* whether to write values */

  DBusString *type_str;
  int type_pos;
  DBusString *value_str;
  int value_pos;

  union
  {
    struct {
      int start_pos; /* first element */
      int len_pos;
      int element_type_pos; /* position of array element type in type_str */
    } array;
  } u;
};

struct DBusArrayLenFixup
{
  int len_pos_in_reader;
  int new_len;
};

void        _dbus_type_reader_init                      (DBusTypeReader        *reader,
                                                         int                    byte_order,
                                                         const DBusString      *type_str,
                                                         int                    type_pos,
                                                         const DBusString      *value_str,
                                                         int                    value_pos);
void        _dbus_type_reader_init_from_mark            (DBusTypeReader        *reader,
                                                         int                    byte_order,
                                                         const DBusString      *type_str,
                                                         const DBusString      *value_str,
                                                         const DBusTypeMark    *mark);
void        _dbus_type_reader_init_types_only           (DBusTypeReader        *reader,
                                                         const DBusString      *type_str,
                                                         int                    type_pos);
void        _dbus_type_reader_init_types_only_from_mark (DBusTypeReader        *reader,
                                                         const DBusString      *type_str,
                                                         const DBusTypeMark    *mark);
void        _dbus_type_reader_save_mark                 (const DBusTypeReader  *reader,
                                                         DBusTypeMark          *mark);
int         _dbus_type_reader_get_current_type          (const DBusTypeReader  *reader);
dbus_bool_t _dbus_type_reader_array_is_empty            (const DBusTypeReader  *reader);
void        _dbus_type_reader_read_basic                (const DBusTypeReader  *reader,
                                                         void                  *value);
dbus_bool_t _dbus_type_reader_read_array_of_basic       (const DBusTypeReader  *reader,
                                                         int                    type,
                                                         void                 **array,
                                                         int                   *array_len);
void        _dbus_type_reader_recurse                   (DBusTypeReader        *reader,
                                                         DBusTypeReader        *subreader);
dbus_bool_t _dbus_type_reader_next                      (DBusTypeReader        *reader);
dbus_bool_t _dbus_type_reader_has_next                  (const DBusTypeReader  *reader);
void        _dbus_type_reader_get_signature             (const DBusTypeReader  *reader,
                                                         const DBusString     **str_p,
                                                         int                   *start_p,
                                                         int                   *len_p);
dbus_bool_t _dbus_type_reader_set_basic                 (DBusTypeReader        *reader,
                                                         const void            *value,
                                                         const DBusTypeReader  *realign_root);
dbus_bool_t _dbus_type_reader_greater_than              (const DBusTypeReader  *lhs,
                                                         const DBusTypeReader  *rhs);

void        _dbus_type_writer_init                 (DBusTypeWriter        *writer,
                                                    int                    byte_order,
                                                    DBusString            *type_str,
                                                    int                    type_pos,
                                                    DBusString            *value_str,
                                                    int                    value_pos);
void        _dbus_type_writer_init_values_only     (DBusTypeWriter        *writer,
                                                    int                    byte_order,
                                                    const DBusString      *type_str,
                                                    int                    type_pos,
                                                    DBusString            *value_str,
                                                    int                    value_pos);
dbus_bool_t _dbus_type_writer_write_basic          (DBusTypeWriter        *writer,
                                                    int                    type,
                                                    const void            *value);
dbus_bool_t _dbus_type_writer_write_array          (DBusTypeWriter        *writer,
                                                    int                    type,
                                                    const void            *array,
                                                    int                    array_len);
dbus_bool_t _dbus_type_writer_recurse              (DBusTypeWriter        *writer,
                                                    int                    container_type,
                                                    const DBusString      *contained_type,
                                                    int                    contained_type_start,
                                                    DBusTypeWriter        *sub);
dbus_bool_t _dbus_type_writer_unrecurse            (DBusTypeWriter        *writer,
                                                    DBusTypeWriter        *sub);
dbus_bool_t _dbus_type_writer_write_reader         (DBusTypeWriter        *writer,
                                                    DBusTypeReader        *reader);
dbus_bool_t _dbus_type_writer_write_reader_partial (DBusTypeWriter        *writer,
                                                    DBusTypeReader        *reader,
                                                    const DBusTypeReader  *start_after,
                                                    int                    start_after_new_pos,
                                                    int                    start_after_new_len,
                                                    DBusList             **fixups);
void        _dbus_type_writer_set_enabled          (DBusTypeWriter        *writer,
                                                    dbus_bool_t            enabled);



#endif /* DBUS_MARSHAL_RECURSIVE_H */
