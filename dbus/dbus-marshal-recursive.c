/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-marshal-recursive.c  Marshalling routines for recursive types
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

#include "dbus-marshal-recursive.h"
#include "dbus-internals.h"

/**
 * @addtogroup DBusMarshal
 * @{
 */

struct DBusTypeReaderClass
{
  const char *name;
  dbus_bool_t types_only; /* only iterates over types, not values */
  void        (* recurse)          (DBusTypeReader *sub,
                                    DBusTypeReader *parent);
  int         (* get_current_type) (DBusTypeReader *reader);
  void        (* next)             (DBusTypeReader *reader,
                                    int             current_type);
};

static int
first_type_in_signature (const DBusString *str,
                         int               pos)
{
  int t;

  t = _dbus_string_get_byte (str, pos);
  
  if (t == DBUS_STRUCT_BEGIN_CHAR)
    return DBUS_TYPE_STRUCT;
  else
    return t;
}

static int
element_type_get_alignment (const DBusString *str,
                            int               pos)
{
  return _dbus_type_get_alignment (first_type_in_signature (str, pos));
}

static void
reader_init (DBusTypeReader    *reader,
             int                byte_order,
             const DBusString  *type_str,
             int                type_pos,
             const DBusString  *value_str,
             int                value_pos)
{
  reader->byte_order = byte_order;
  reader->finished = FALSE;
  reader->type_str = type_str;
  reader->type_pos = type_pos;
  reader->value_str = value_str;
  reader->value_pos = value_pos;
}

static void
base_reader_recurse (DBusTypeReader *sub,
                     DBusTypeReader *parent)
{  
  /* point subreader at the same place as parent */
  reader_init (sub,
               parent->byte_order,
               parent->type_str,
               parent->type_pos,
               parent->value_str,
               parent->value_pos);
}

static void
struct_types_only_reader_recurse (DBusTypeReader *sub,
                                  DBusTypeReader *parent)
{
  base_reader_recurse (sub, parent);

  _dbus_assert (_dbus_string_get_byte (sub->type_str,
                                       sub->type_pos) == DBUS_STRUCT_BEGIN_CHAR);
  
  sub->type_pos += 1;
}

static void
struct_reader_recurse (DBusTypeReader *sub,
                       DBusTypeReader *parent)
{
  struct_types_only_reader_recurse (sub, parent);
  
  /* struct has 8 byte alignment */
  sub->value_pos = _DBUS_ALIGN_VALUE (sub->value_pos, 8);
}

static void
array_types_only_reader_recurse (DBusTypeReader *sub,
                                 DBusTypeReader *parent)
{
  base_reader_recurse (sub, parent);
  
  /* point type_pos at the array element type */
  sub->type_pos += 1;

  sub->u.array.element_type = first_type_in_signature (sub->type_str,
                                                       sub->type_pos);

  /* Init with values likely to crash things if misused */
  sub->u.array.start_pos = _DBUS_INT_MAX;
  sub->u.array.len = _DBUS_INT_MAX;
}

static void
array_reader_recurse (DBusTypeReader *sub,
                      DBusTypeReader *parent)
{
  dbus_uint32_t array_len;
  int alignment;

  _dbus_assert (!_dbus_type_reader_array_is_empty (parent));
  
  array_types_only_reader_recurse (sub, parent);
  
  sub->value_pos = _DBUS_ALIGN_VALUE (sub->value_pos, 4);
      
  _dbus_demarshal_basic_type (sub->value_str,
                              DBUS_TYPE_UINT32,
                              &array_len,
                              sub->byte_order,
                              &sub->value_pos);
      
  sub->u.array.len = array_len;
      
  alignment = element_type_get_alignment (sub->type_str,
                                          sub->type_pos);
      
  sub->value_pos = _DBUS_ALIGN_VALUE (sub->value_pos, alignment);
  
  sub->u.array.start_pos = sub->value_pos;

  _dbus_verbose ("    type reader %p array start = %d array len = %d array element type = %s\n",
                 sub,
                 sub->u.array.start_pos,
                 sub->u.array.len,
                 _dbus_type_to_string (sub->u.array.element_type));
}

static void
variant_reader_recurse (DBusTypeReader *sub,
                        DBusTypeReader *parent)
{
  int sig_len;

  _dbus_assert (!_dbus_type_reader_array_is_empty (parent));
  
  base_reader_recurse (sub, parent);

  /* Variant is 1 byte sig length (without nul), signature with nul,
   * padding to 8-boundary, then values
   */

  sig_len = _dbus_string_get_byte (sub->value_str, sub->value_pos);

  sub->type_str = sub->value_str;
  sub->type_pos = sub->value_pos + 1;
  
  sub->value_pos = sub->type_pos + sig_len + 1;
  
  sub->value_pos = _DBUS_ALIGN_VALUE (sub->value_pos, 8);

  _dbus_verbose ("    type reader %p variant containing '%s'\n",
                 sub,
                 _dbus_string_get_const_data_len (sub->type_str,
                                                  sub->type_pos, 0));
}

static int
base_reader_get_current_type (DBusTypeReader *reader)
{
  int t;

  t = first_type_in_signature (reader->type_str,
                               reader->type_pos);

  return t;
}

static int
struct_reader_get_current_type (DBusTypeReader *reader)
{
  int t;
  
  if (reader->finished)
    t = DBUS_TYPE_INVALID;
  else
    t = first_type_in_signature (reader->type_str,
                                 reader->type_pos);

  return t;
}

static int
array_types_only_reader_get_current_type (DBusTypeReader *reader)
{
  int t;

  if (reader->finished)
    t = DBUS_TYPE_INVALID;
  else
    t = reader->u.array.element_type;

  return t;
}

static int
array_reader_get_current_type (DBusTypeReader *reader)
{
  int t;
  int end_pos;
      
  /* return the array element type if elements remain, and
   * TYPE_INVALID otherwise
   */
      
  end_pos = reader->u.array.start_pos + reader->u.array.len;
      
  _dbus_assert (reader->value_pos <= end_pos);
  _dbus_assert (reader->value_pos >= reader->u.array.start_pos);
      
  if (reader->value_pos < end_pos)
    t = reader->u.array.element_type;
  else
    t = DBUS_TYPE_INVALID;

  return t;
}

static void
skip_one_complete_type (const DBusString *type_str,
                        int              *type_pos)
{
  while (_dbus_string_get_byte (type_str, *type_pos) == DBUS_TYPE_ARRAY)
    *type_pos += 1;

  if (_dbus_string_get_byte (type_str, *type_pos) == DBUS_STRUCT_BEGIN_CHAR)
    {
      int depth;
      depth = 1;
      *type_pos += 1;
      while (depth > 0)
        {
          switch (_dbus_string_get_byte (type_str, *type_pos))
            {
            case DBUS_STRUCT_BEGIN_CHAR:
              depth += 1;
              break;
            case DBUS_STRUCT_END_CHAR:
              depth -= 1;
              break;
            case DBUS_TYPE_INVALID:
              _dbus_assert_not_reached ("unbalanced parens in signature");
              break;
            }
          *type_pos += 1;
        }
    }
  else
    *type_pos += 1;
}

static void
skip_array_values (int               element_type,
                   const DBusString *value_str,
                   int              *value_pos,
                   int               byte_order)
{
  dbus_uint32_t array_len;
  int pos;
  int alignment;
  
  pos = _DBUS_ALIGN_VALUE (*value_pos, 4);
  
  _dbus_demarshal_basic_type (value_str,
                              DBUS_TYPE_UINT32,
                              &array_len,
                              byte_order,
                              &pos);

  alignment = _dbus_type_get_alignment (element_type);

  pos = _DBUS_ALIGN_VALUE (pos, alignment);
  
  *value_pos = pos + array_len;
}

static void
base_reader_next (DBusTypeReader *reader,
                  int             current_type)
{
  switch (current_type)
    {
    case DBUS_TYPE_STRUCT:
      /* Scan forward over the entire container contents */
      {
        DBusTypeReader sub;
        
        /* Recurse into the struct */
        _dbus_type_reader_recurse (reader, &sub);
        
        /* Skip everything in this subreader */
        while (_dbus_type_reader_next (&sub))
          {
            /* nothing */;
          }
        
        /* Now we are at the end of this container */
        reader->type_pos = sub.type_pos;

        if (!reader->klass->types_only)
          reader->value_pos = sub.value_pos;
      }
      break;
      
    case DBUS_TYPE_ARRAY:
      {        
        if (!reader->klass->types_only)
          skip_array_values (first_type_in_signature (reader->type_str,
                                                      reader->type_pos + 1),
                             reader->value_str, &reader->value_pos, reader->byte_order);

        skip_one_complete_type (reader->type_str, &reader->type_pos);
      }
      break;
      
    default:
      if (!reader->klass->types_only)
        _dbus_marshal_skip_basic_type (reader->value_str,
                                       current_type, reader->byte_order,
                                       &reader->value_pos);
      
      reader->type_pos += 1;
      break;
    }
}

static void
struct_reader_next (DBusTypeReader *reader,
                    int             current_type)
{
  int t;
  
  base_reader_next (reader, current_type);
  
  /* for STRUCT containers we return FALSE at the end of the struct,
   * for INVALID we return FALSE at the end of the signature.
   * In both cases we arrange for get_current_type() to return INVALID
   * which is defined to happen iff we're at the end (no more next())
   */
  t = _dbus_string_get_byte (reader->type_str, reader->type_pos);
  if (t == DBUS_STRUCT_END_CHAR)
    {
      reader->type_pos += 1;
      reader->finished = TRUE;
    }
}

static void
array_types_only_reader_next (DBusTypeReader *reader,
                              int             current_type)
{
  /* We have one "element" to be iterated over
   * in each array, which is its element type.
   * So the finished flag indicates whether we've
   * iterated over it yet or not.
   */
  reader->finished = TRUE;
}

static void
array_reader_next (DBusTypeReader *reader,
                   int             current_type)
{
  /* Skip one array element */
  int end_pos;
      
  end_pos = reader->u.array.start_pos + reader->u.array.len;
      
  _dbus_assert (reader->value_pos < end_pos);
  _dbus_assert (reader->value_pos >= reader->u.array.start_pos);
      
  if (reader->u.array.element_type == DBUS_TYPE_STRUCT)
    {
      DBusTypeReader sub;
          
      /* Recurse into the struct */
      _dbus_type_reader_recurse (reader, &sub);
          
      /* Skip everything in this element */
      while (_dbus_type_reader_next (&sub))
        {
          /* nothing */;
        }
          
      /* Now we are at the end of this element */
      reader->value_pos = sub.value_pos;
    }
  else if (reader->u.array.element_type == DBUS_TYPE_ARRAY)
    {
      skip_array_values (first_type_in_signature (reader->type_str,
                                                  reader->type_pos + 1),
                         reader->value_str, &reader->value_pos, reader->byte_order);
    }
  else
    {
      _dbus_marshal_skip_basic_type (reader->value_str,
                                     current_type, reader->byte_order,
                                     &reader->value_pos);
    }

  _dbus_assert (reader->value_pos <= end_pos);
      
  if (reader->value_pos == end_pos)
    {
      skip_one_complete_type (reader->type_str,
                              &reader->type_pos);
    }
}

static const DBusTypeReaderClass body_reader_class = {
  "body",
  FALSE,
  NULL, /* body is always toplevel, so doesn't get recursed into */
  base_reader_get_current_type,
  base_reader_next
};

static const DBusTypeReaderClass body_types_only_reader_class = {
  "body types",
  TRUE,
  NULL, /* body is always toplevel, so doesn't get recursed into */
  base_reader_get_current_type,
  base_reader_next
};

static const DBusTypeReaderClass struct_reader_class = {
  "struct",
  FALSE,
  struct_reader_recurse,
  struct_reader_get_current_type,
  struct_reader_next
};

static const DBusTypeReaderClass struct_types_only_reader_class = {
  "struct types",
  TRUE,
  struct_types_only_reader_recurse,
  struct_reader_get_current_type,
  struct_reader_next
};

static const DBusTypeReaderClass array_reader_class = {
  "array",
  FALSE,
  array_reader_recurse,
  array_reader_get_current_type,
  array_reader_next
};

static const DBusTypeReaderClass array_types_only_reader_class = {
  "array types",
  TRUE,
  array_types_only_reader_recurse,
  array_types_only_reader_get_current_type,
  array_types_only_reader_next
};

static const DBusTypeReaderClass variant_reader_class = {
  "variant",
  FALSE,
  variant_reader_recurse,
  base_reader_get_current_type,
  base_reader_next
};

void
_dbus_type_reader_init (DBusTypeReader    *reader,
                        int                byte_order,
                        const DBusString  *type_str,
                        int                type_pos,
                        const DBusString  *value_str,
                        int                value_pos)
{
  reader->klass = &body_reader_class;
  
  reader_init (reader, byte_order, type_str, type_pos,
               value_str, value_pos);
  
  _dbus_verbose ("  type reader %p init type_pos = %d value_pos = %d remaining sig '%s'\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));
}

void
_dbus_type_reader_init_types_only (DBusTypeReader    *reader,
                                   const DBusString  *type_str,
                                   int                type_pos)
{
  reader->klass = &body_types_only_reader_class;
  
  reader_init (reader, DBUS_COMPILER_BYTE_ORDER /* irrelevant */,
               type_str, type_pos, NULL, _DBUS_INT_MAX /* crashes if we screw up */);
  
  _dbus_verbose ("  type reader %p init types only type_pos = %d remaining sig '%s'\n",
                 reader, reader->type_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));
}

int
_dbus_type_reader_get_current_type (DBusTypeReader *reader)
{
  int t;

  t = (* reader->klass->get_current_type) (reader);

  _dbus_assert (t != DBUS_STRUCT_END_CHAR);
  _dbus_assert (t != DBUS_STRUCT_BEGIN_CHAR);
  
#if 0
  _dbus_verbose ("  type reader %p current type_pos = %d type = %s\n",
                 reader, reader->type_pos,
                 _dbus_type_to_string (t));
#endif
  
  return t;
}

dbus_bool_t
_dbus_type_reader_array_is_empty (DBusTypeReader *reader)
{
  dbus_uint32_t array_len;
  int len_pos;
  
  _dbus_assert (_dbus_type_reader_get_current_type (reader) == DBUS_TYPE_ARRAY);
  _dbus_assert (!reader->klass->types_only);

  len_pos = _DBUS_ALIGN_VALUE (reader->value_pos, 4);
  
  _dbus_demarshal_basic_type (reader->value_str,
                              DBUS_TYPE_UINT32,
                              &array_len,
                              reader->byte_order,
                              &len_pos);

  return array_len == 0;
}

void
_dbus_type_reader_read_basic (DBusTypeReader    *reader,
                              void              *value)
{
  int t;
  int next;

  _dbus_assert (!reader->klass->types_only);
  
  t = _dbus_type_reader_get_current_type (reader);
  
  next = reader->value_pos;
  _dbus_demarshal_basic_type (reader->value_str,
                              t, value,
                              reader->byte_order,
                              &next);
  
  
  _dbus_verbose ("  type reader %p read basic type_pos = %d value_pos = %d next = %d remaining sig '%s'\n",
                 reader, reader->type_pos, reader->value_pos, next,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));
}

dbus_bool_t
_dbus_type_reader_read_array_of_basic (DBusTypeReader    *reader,
                                       int                type,
                                       void             **array,
                                       int               *array_len)
{
  _dbus_assert (!reader->klass->types_only);  
  
}

/**
 * Initialize a new reader pointing to the first type and
 * corresponding value that's a child of the current container. It's
 * an error to call this if the current type is a non-container.
 * 
 * Note that DBusTypeReader traverses values, not types. So if you
 * have an empty array of array of int, you can't recurse into it. You
 * can only recurse into each element.
 *
 * @param reader the reader
 * @param sub a reader to init pointing to the first child
 */
void
_dbus_type_reader_recurse (DBusTypeReader *reader,
                           DBusTypeReader *sub)
{
  int t;
  
  t = first_type_in_signature (reader->type_str, reader->type_pos);

  switch (t)
    {
    case DBUS_TYPE_STRUCT:
      if (reader->klass->types_only)
        sub->klass = &struct_types_only_reader_class;
      else
        sub->klass = &struct_reader_class;
      break;
    case DBUS_TYPE_ARRAY:
      if (reader->klass->types_only)
        sub->klass = &array_types_only_reader_class;
      else
        sub->klass = &array_reader_class;
      break;
    case DBUS_TYPE_VARIANT:
      if (reader->klass->types_only)
        _dbus_assert_not_reached ("can't recurse into variant typecode");
      else
        sub->klass = &variant_reader_class;
      break;
    default:
      _dbus_verbose ("recursing into type %s\n", _dbus_type_to_string (t));
#ifndef DBUS_DISABLE_CHECKS
      if (t == DBUS_TYPE_INVALID)
        _dbus_warn ("You can't recurse into an empty array or off the end of a message body\n");
#endif /* DBUS_DISABLE_CHECKS */
      
      _dbus_assert_not_reached ("don't yet handle recursing into this type");
    }

  (* sub->klass->recurse) (sub, reader);
  
  _dbus_verbose ("  type reader %p RECURSED type_pos = %d value_pos = %d remaining sig '%s'\n",
                 sub, sub->type_pos, sub->value_pos,
                 _dbus_string_get_const_data_len (sub->type_str, sub->type_pos, 0));
}

/**
 * Skip to the next value on this "level". e.g. the next field in a
 * struct, the next value in an array, the next key or value in a
 * dict. Returns FALSE at the end of the current container.
 *
 * @param reader the reader
 * @returns FALSE if nothing more to read at or below this level
 */
dbus_bool_t
_dbus_type_reader_next (DBusTypeReader *reader)
{
  int t;
  
  t = _dbus_type_reader_get_current_type (reader);
  
  _dbus_verbose ("  type reader %p START next() { type_pos = %d value_pos = %d remaining sig '%s' current_type = %s\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0),
                 _dbus_type_to_string (t));

  if (t == DBUS_TYPE_INVALID)
    return FALSE;

  (* reader->klass->next) (reader, t);
  
  _dbus_verbose ("  type reader %p END next() type_pos = %d value_pos = %d remaining sig '%s' current_type = %s\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0),
                 _dbus_type_to_string (_dbus_type_reader_get_current_type (reader)));
  
  return _dbus_type_reader_get_current_type (reader) != DBUS_TYPE_INVALID;
}


/*
 *
 *
 *         DBusTypeWriter
 *
 *
 *
 */

void
_dbus_type_writer_init (DBusTypeWriter *writer,
                        int             byte_order,
                        DBusString     *type_str,
                        int             type_pos,
                        DBusString     *value_str,
                        int             value_pos)
{
  writer->byte_order = byte_order;
  writer->type_str = type_str;
  writer->type_pos = type_pos;
  writer->value_str = value_str;
  writer->value_pos = value_pos;
  writer->container_type = DBUS_TYPE_INVALID;
  writer->type_pos_is_expectation = FALSE;

  _dbus_verbose ("writer %p init remaining sig '%s'\n", writer,
                 _dbus_string_get_const_data_len (writer->type_str, writer->type_pos, 0));
}

static dbus_bool_t
_dbus_type_writer_write_basic_no_typecode (DBusTypeWriter *writer,
                                           int             type,
                                           const void     *value)
{
  int old_value_len;
  int bytes_written;

  old_value_len = _dbus_string_get_length (writer->value_str);
        
  if (!_dbus_marshal_basic_type (writer->value_str,
                                 writer->value_pos,
                                 type,
                                 value,
                                 writer->byte_order))
    return FALSE;

  bytes_written = _dbus_string_get_length (writer->value_str) - old_value_len;
  
  writer->value_pos += bytes_written;

  return TRUE;
}

/* If our parent is an array, things are a little bit complicated.
 *
 * The parent must have a complete element type, such as
 * "i" or "aai" or "(ii)" or "a(ii)". There can't be
 * unclosed parens, or an "a" with no following type.
 *
 * To recurse, the only allowed operation is to recurse into the
 * first type in the element type. So for "i" you can't recurse, for
 * "ai" you can recurse into the array, for "(ii)" you can recurse
 * into the struct.
 *
 * If you recurse into the array for "ai", then you must specify
 * "i" for the element type of the array you recurse into.
 * 
 * While inside an array at any level, we need to avoid writing to
 * type_str, since the type only appears once for the whole array,
 * it does not appear for each array element.
 *
 * While inside an array type_pos points to the expected next
 * typecode, rather than the next place we could write a typecode.
 */
static void
writer_recurse_init_and_check (DBusTypeWriter *writer,
                               int             container_type,
                               DBusTypeWriter *sub)
{
  _dbus_type_writer_init (sub,
                          writer->byte_order,
                          writer->type_str,
                          writer->type_pos,
                          writer->value_str,
                          writer->value_pos);
  
  sub->container_type = container_type;

  if (writer->type_pos_is_expectation ||
      (sub->container_type == DBUS_TYPE_ARRAY || sub->container_type == DBUS_TYPE_VARIANT))
    sub->type_pos_is_expectation = TRUE;
  else
    sub->type_pos_is_expectation = FALSE;
  
#ifndef DBUS_DISABLE_CHECKS
  if (writer->type_pos_is_expectation)
    {
      int expected;

      expected = first_type_in_signature (writer->type_str, writer->type_pos);
      
      if (expected != sub->container_type)
        {
          _dbus_warn ("Writing an element of type %s, but the expected type here is %s\n",
                      _dbus_type_to_string (sub->container_type),
                      _dbus_type_to_string (expected));
          _dbus_assert_not_reached ("bad array element or variant content written");
        }
    }
#endif /* DBUS_DISABLE_CHECKS */

  _dbus_verbose ("  type writer %p recurse parent type_pos = %d value_pos = %d is_expectation = %d container_type = %s remaining sig '%s'\n",
                 writer, writer->type_pos, writer->value_pos, writer->type_pos_is_expectation,
                 _dbus_type_to_string (writer->container_type),
                 _dbus_string_get_const_data_len (writer->type_str, writer->type_pos, 0));
  _dbus_verbose ("  type writer %p recurse sub    type_pos = %d value_pos = %d is_expectation = %d container_type = %s\n",
                 sub, sub->type_pos, sub->value_pos,
                 sub->type_pos_is_expectation,
                 _dbus_type_to_string (sub->container_type));
}

static dbus_bool_t
write_or_verify_typecode (DBusTypeWriter *writer,
                          int             typecode)
{
  /* A subwriter inside an array or variant will have type_pos
   * pointing to the expected typecode; a writer not inside an array
   * or variant has type_pos pointing to the next place to insert a
   * typecode.
   */
  _dbus_verbose ("  type writer %p write_or_verify start type_pos = %d remaining sig '%s'\n",
                 writer, writer->type_pos,
                 _dbus_string_get_const_data_len (writer->type_str, writer->type_pos, 0));
  
  if (writer->type_pos_is_expectation)
    {
#ifndef DBUS_DISABLE_CHECKS
      {
        int expected;
        
        expected = _dbus_string_get_byte (writer->type_str, writer->type_pos);
        
        if (expected != typecode)
          {
            _dbus_warn ("Array or Variant type requires that type %s be written, but %s was written\n",
                        _dbus_type_to_string (expected), _dbus_type_to_string (typecode));
            _dbus_assert_not_reached ("bad type inserted somewhere inside an array or variant");
          }
      }
#endif /* DBUS_DISABLE_CHECKS */

      /* if immediately inside an array we'd always be appending an element,
       * so the expected type doesn't change; if inside a struct or something
       * below an array, we need to move through said struct or something.
       */
      if (writer->container_type != DBUS_TYPE_ARRAY)
        writer->type_pos += 1;
    }
  else
    {
      if (!_dbus_string_insert_byte (writer->type_str,
                                     writer->type_pos,
                                     typecode))
        return FALSE;

      writer->type_pos += 1;
    }

  _dbus_verbose ("  type writer %p write_or_verify end type_pos = %d remaining sig '%s'\n",
                 writer, writer->type_pos,
                 _dbus_string_get_const_data_len (writer->type_str, writer->type_pos, 0));
  
  return TRUE;
}

dbus_bool_t
_dbus_type_writer_recurse_struct (DBusTypeWriter *writer,
                                  DBusTypeWriter *sub)
{
  writer_recurse_init_and_check (writer, DBUS_TYPE_STRUCT, sub);

  /* Ensure that we'll be able to add alignment padding and the typecode */
  if (!_dbus_string_alloc_space (sub->value_str, 8))
    return FALSE;

  if (!_dbus_string_alloc_space (sub->type_str, 1))
    return FALSE;
  
  if (!write_or_verify_typecode (sub, DBUS_STRUCT_BEGIN_CHAR))
    _dbus_assert_not_reached ("failed to insert struct typecode after prealloc");
  
  if (!_dbus_string_insert_bytes (sub->value_str,
                                  sub->value_pos,
                                  _DBUS_ALIGN_VALUE (sub->value_pos, 8) - sub->value_pos,
                                  '\0'))
    _dbus_assert_not_reached ("should not have failed to insert alignment padding for struct");
  sub->value_pos = _DBUS_ALIGN_VALUE (sub->value_pos, 8);
  
  return TRUE;
}

dbus_bool_t
_dbus_type_writer_recurse_array (DBusTypeWriter *writer,
                                 const char     *element_type,
                                 DBusTypeWriter *sub)
{
  int element_type_len;
  DBusString element_type_str;
  dbus_uint32_t value = 0;
  int alignment;
  int aligned;
  DBusString str;
  
  writer_recurse_init_and_check (writer, DBUS_TYPE_ARRAY, sub);

  _dbus_string_init_const (&element_type_str, element_type);
  element_type_len = _dbus_string_get_length (&element_type_str);
  
#ifndef DBUS_DISABLE_CHECKS
  if (writer->container_type == DBUS_TYPE_ARRAY)
    {
      if (!_dbus_string_equal_substring (&element_type_str, 0, element_type_len,
                                         writer->type_str, writer->u.array.element_type_pos + 1))
        {
          _dbus_warn ("Writing an array of '%s' but this is incompatible with the expected type of elements in the parent array\n",
                      element_type);
          _dbus_assert_not_reached ("incompatible type for child array");
        }
    }
#endif /* DBUS_DISABLE_CHECKS */  

  /* 4 bytes for the array length and 4 bytes possible padding */
  if (!_dbus_string_alloc_space (sub->value_str, 8))
    return FALSE;

  sub->type_pos += 1; /* move to point to the element type, since type_pos
                       * should be the expected type for further writes
                       */
  sub->u.array.element_type_pos = sub->type_pos;

  if (!writer->type_pos_is_expectation)
    {
      /* sub is a toplevel/outermost array so we need to write the type data */
      
      /* alloc space for array typecode, element signature, possible 7
       * bytes of padding
       */
      if (!_dbus_string_alloc_space (writer->type_str, 1 + element_type_len + 7))
        return FALSE;

      if (!_dbus_string_insert_byte (writer->type_str,
                                     writer->type_pos,
                                     DBUS_TYPE_ARRAY))
        _dbus_assert_not_reached ("failed to insert array typecode after prealloc");
      
      if (!_dbus_string_copy (&element_type_str, 0,
                              sub->type_str, sub->u.array.element_type_pos))
        _dbus_assert_not_reached ("should not have failed to insert array element typecodes");
    }

  /* If the parent is an array, we hold type_pos pointing at the array element type;
   * otherwise advance it to reflect the array value we just recursed into
   */
  if (writer->container_type != DBUS_TYPE_ARRAY)
    writer->type_pos += 1 + element_type_len;
  else
    _dbus_assert (writer->type_pos_is_expectation); /* because it's an array */
  
  /* Write the length */
  sub->u.array.len_pos = _DBUS_ALIGN_VALUE (sub->value_pos, 4);

  if (!_dbus_type_writer_write_basic_no_typecode (sub, DBUS_TYPE_UINT32,
                                                  &value))
    _dbus_assert_not_reached ("should not have failed to insert array len");
  
  _dbus_assert (sub->u.array.len_pos == sub->value_pos - 4);

  /* Write alignment padding for array elements */
  _dbus_string_init_const (&str, element_type);
  alignment = element_type_get_alignment (&str, 0);

  aligned = _DBUS_ALIGN_VALUE (sub->value_pos, alignment);
  if (aligned != sub->value_pos)
    {
      if (!_dbus_string_insert_bytes (sub->value_str,
                                      sub->value_pos,
                                      aligned - sub->value_pos,
                                      '\0'))
        _dbus_assert_not_reached ("should not have failed to insert alignment padding");
      
      sub->value_pos = aligned;
    }
  sub->u.array.start_pos = sub->value_pos;

  _dbus_assert (sub->u.array.start_pos == sub->value_pos);
  _dbus_assert (sub->u.array.len_pos < sub->u.array.start_pos);

  _dbus_verbose ("  type writer %p recurse array done remaining sig '%s'\n", sub,
                 _dbus_string_get_const_data_len (sub->type_str, sub->type_pos, 0));
  
  return TRUE;
}

/* Variant value will normally have:
 *   1 byte signature length not including nul
 *   signature typecodes (nul terminated)
 *   padding to 8-boundary
 *   body according to signature
 *
 * The signature string can only have a single type
 * in it but that type may be complex/recursive.
 *
 * So a typical variant type with the integer 3 will have these
 * octets:
 *   0x1 'i' '\0' [padding to 8-boundary] 0x0 0x0 0x0 0x3
 *
 * For an array of 4-byte types stuffed into variants, the padding to
 * 8-boundary is only the 1 byte that is required for the 4-boundary
 * anyhow for all array elements after the first one. And for single
 * variants in isolation, wasting a few bytes is hardly a big deal.
 *
 * The main world of hurt for writing out a variant is that the type
 * string is the same string as the value string. Which means
 * inserting to the type string will move the value_pos; and it means
 * that inserting to the type string could break type alignment.
 * 
 * This type alignment issue is why the body of the variant is always
 * 8-aligned. Then we know that re-8-aligning the start of the body
 * will always correctly align the full contents of the variant type.
 */
dbus_bool_t
_dbus_type_writer_recurse_variant (DBusTypeWriter *writer,
                                   const char     *contained_type,
                                   DBusTypeWriter *sub)
{
  int contained_type_len;
  DBusString contained_type_str;
  
  writer_recurse_init_and_check (writer, DBUS_TYPE_VARIANT, sub);

  _dbus_string_init_const (&contained_type_str, contained_type);
  
  contained_type_len = _dbus_string_get_length (&contained_type_str);
  
  /* Allocate space for the worst case, which is 1 byte sig
   * length, nul byte at end of sig, and 7 bytes padding to
   * 8-boundary.
   */
  if (!_dbus_string_alloc_space (sub->value_str, contained_type_len + 9))
    return FALSE;

  /* write VARIANT typecode to the parent's type string */
  if (!write_or_verify_typecode (sub, DBUS_TYPE_VARIANT))
    return FALSE;

  if (!_dbus_string_insert_byte (sub->value_str,
                                 sub->value_pos,
                                 contained_type_len))
    _dbus_assert_not_reached ("should not have failed to insert variant type sig len");

  sub->value_pos += 1;

  /* Here we switch over to the expected type sig we're about to write */
  sub->type_str = sub->value_str;
  sub->type_pos = sub->value_pos;
  
  if (!_dbus_string_copy (&contained_type_str, 0,
                          sub->value_str, sub->value_pos))
    _dbus_assert_not_reached ("should not have failed to insert variant type sig");

  sub->value_pos += contained_type_len;

  if (!_dbus_string_insert_byte (sub->value_str,
                                 sub->value_pos,
                                 DBUS_TYPE_INVALID))
    _dbus_assert_not_reached ("should not have failed to insert variant type nul termination");

  sub->value_pos += 1;
  
  if (!_dbus_string_insert_bytes (sub->value_str,
                                  sub->value_pos,
                                  _DBUS_ALIGN_VALUE (sub->value_pos, 8) - sub->value_pos,
                                  '\0'))
    _dbus_assert_not_reached ("should not have failed to insert alignment padding for variant body");
  sub->value_pos = _DBUS_ALIGN_VALUE (sub->value_pos, 8);
  
  return TRUE;
}

dbus_bool_t
_dbus_type_writer_unrecurse (DBusTypeWriter *writer,
                             DBusTypeWriter *sub)
{
  _dbus_assert (sub->type_pos > 0); /* can't be recursed if this fails */

  /* type_pos_is_expectation never gets unset once set, or we'd get all hosed */
  _dbus_assert (!writer->type_pos_is_expectation ||
                (writer->type_pos_is_expectation && sub->type_pos_is_expectation));

  _dbus_verbose ("  type writer %p unrecurse type_pos = %d value_pos = %d is_expectation = %d container_type = %s\n",
                 writer, writer->type_pos, writer->value_pos, writer->type_pos_is_expectation,
                 _dbus_type_to_string (writer->container_type));
  _dbus_verbose ("  type writer %p unrecurse sub type_pos = %d value_pos = %d is_expectation = %d container_type = %s\n",
                 sub, sub->type_pos, sub->value_pos,
                 sub->type_pos_is_expectation,
                 _dbus_type_to_string (sub->container_type));
  
  if (sub->container_type == DBUS_TYPE_STRUCT)
    {
      if (!write_or_verify_typecode (sub, DBUS_STRUCT_END_CHAR))
        return FALSE;
    }
  else if (sub->container_type == DBUS_TYPE_ARRAY)
    {
      dbus_uint32_t len;

      /* Set the array length */
      len = sub->value_pos - sub->u.array.start_pos;
      _dbus_marshal_set_uint32 (sub->value_str,
                                sub->byte_order,
                                sub->u.array.len_pos,
                                len);
      _dbus_verbose ("    filled in sub array len to %u at len_pos %d\n",
                     len, sub->u.array.len_pos);
    }

  /* Now get type_pos right for the parent writer. Here are the cases:
   *
   * Cases !writer->type_pos_is_expectation:
   *   (in these cases we want to update to the new insertion point)
   * 
   * - if we recursed into a STRUCT then we didn't know in advance
   *   what the types in the struct would be; so we have to fill in
   *   that information now.
   *       writer->type_pos = sub->type_pos
   * 
   * - if we recursed into anything else, we knew the full array
   *   type, or knew the single typecode marking VARIANT, so
   *   writer->type_pos is already correct.
   *       writer->type_pos should remain as-is
   *
   * - note that the parent is never an ARRAY or VARIANT, if it were
   *   then type_pos_is_expectation would be TRUE. The parent
   *   is thus known to be a toplevel or STRUCT.
   *
   * Cases where writer->type_pos_is_expectation:
   *   (in these cases we want to update to next expected type to write)
   * 
   * - we recursed from STRUCT into STRUCT and we didn't increment
   *   type_pos in the parent just to stay consistent with the
   *   !writer->type_pos_is_expectation case (though we could
   *   special-case this in recurse_struct instead if we wanted)
   *       writer->type_pos = sub->type_pos
   *
   * - we recursed from STRUCT into ARRAY or VARIANT and type_pos
   *   for parent should have been incremented already
   *       writer->type_pos should remain as-is
   * 
   * - we recursed from ARRAY into a sub-element, so type_pos in the
   *   parent is the element type and should remain the element type
   *   for the benefit of the next child element
   *       writer->type_pos should remain as-is
   *
   * - we recursed from VARIANT into its value, so type_pos in the
   *   parent makes no difference since there's only one value
   *   and we just finished writing it and won't use type_pos again
   *       writer->type_pos should remain as-is
   */
  if (sub->container_type == DBUS_TYPE_STRUCT &&
      (writer->container_type == DBUS_TYPE_STRUCT ||
       writer->container_type == DBUS_TYPE_INVALID))
    {
      /* Advance the parent to the next struct field */
      writer->type_pos = sub->type_pos;
    }
  
  writer->value_pos = sub->value_pos;

  _dbus_verbose ("  type writer %p unrecursed type_pos = %d value_pos = %d remaining sig '%s'\n",
                 writer, writer->type_pos, writer->value_pos,
                 _dbus_string_get_const_data_len (writer->type_str, writer->type_pos, 0));
  
  return TRUE;
}

dbus_bool_t
_dbus_type_writer_write_basic (DBusTypeWriter *writer,
                               int             type,
                               const void     *value)
{
  dbus_bool_t retval;
  
  /* First ensure that our type realloc will succeed */
  if (!_dbus_string_alloc_space (writer->type_str, 1))
    return FALSE;

  retval = FALSE;

  if (!_dbus_type_writer_write_basic_no_typecode (writer, type, value))
    goto out;
  
  if (!write_or_verify_typecode (writer, type))
    _dbus_assert_not_reached ("failed to write typecode after prealloc");
  
  retval = TRUE;
  
 out:
  _dbus_verbose ("  type writer %p basic type_pos = %d value_pos = %d is_expectation = %d\n",
                 writer, writer->type_pos, writer->value_pos, writer->type_pos_is_expectation);
  
  return retval;
}

dbus_bool_t
_dbus_type_writer_write_array (DBusTypeWriter *writer,
                               int             type,
                               const void     *array,
                               int             array_len)
{


}

/** @} */ /* end of DBusMarshal group */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include "dbus-list.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
  DBusString signature;
  DBusString body;
} DataBlock;

typedef struct
{
  int saved_sig_len;
  int saved_body_len;
} DataBlockState;

static dbus_bool_t
data_block_init (DataBlock *block)
{
  if (!_dbus_string_init (&block->signature))
    return FALSE;

  if (!_dbus_string_init (&block->body))
    {
      _dbus_string_free (&block->signature);
      return FALSE;
    }
  
  return TRUE;
}

static void
data_block_free (DataBlock *block)
{
  _dbus_string_free (&block->signature);
  _dbus_string_free (&block->body);
}

static void
data_block_save (DataBlock      *block,
                 DataBlockState *state)
{
  state->saved_sig_len = _dbus_string_get_length (&block->signature);
  state->saved_body_len = _dbus_string_get_length (&block->body);
}

static void
data_block_restore (DataBlock      *block,
                    DataBlockState *state)
{
  /* These set_length should be shortening things so should always work */
  
  if (!_dbus_string_set_length (&block->signature,
                                state->saved_sig_len))
    _dbus_assert_not_reached ("could not restore signature length");
  
  if (!_dbus_string_set_length (&block->body,
                                state->saved_body_len))
    _dbus_assert_not_reached ("could not restore body length");
}

static void
data_block_init_reader_writer (DataBlock      *block,
                               int             byte_order,
                               DBusTypeReader *reader,
                               DBusTypeWriter *writer)
{
  _dbus_type_reader_init (reader,
                          byte_order,
                          &block->signature,
                          _dbus_string_get_length (&block->signature),
                          &block->body,
                          _dbus_string_get_length (&block->body));
  
  _dbus_type_writer_init (writer,
                          byte_order,
                          &block->signature,
                          _dbus_string_get_length (&block->signature),
                          &block->body,
                          _dbus_string_get_length (&block->body));
}

static void
real_check_expected_type (DBusTypeReader *reader,
                          int             expected,
                          const char     *funcname,
                          int             line)
{
  int t;

  t = _dbus_type_reader_get_current_type (reader);
  
  if (t != expected)
    {
      _dbus_warn ("Read type %s while expecting %s at %s line %d\n",
                  _dbus_type_to_string (t),
                  _dbus_type_to_string (expected),
                  funcname, line);
      
      exit (1);
    }
}

#define check_expected_type(reader, expected) real_check_expected_type (reader, expected, _DBUS_FUNCTION_NAME, __LINE__)

#define NEXT_EXPECTING_TRUE(reader)  do { if (!_dbus_type_reader_next (reader))         \
 {                                                                                      \
    _dbus_warn ("_dbus_type_reader_next() should have returned TRUE at %s %d\n",        \
                              _DBUS_FUNCTION_NAME, __LINE__);                           \
    _dbus_assert_not_reached ("test failed");                                           \
 }                                                                                      \
} while (0)

#define NEXT_EXPECTING_FALSE(reader) do { if (_dbus_type_reader_next (reader))          \
 {                                                                                      \
    _dbus_warn ("_dbus_type_reader_next() should have returned FALSE at %s %d\n",       \
                              _DBUS_FUNCTION_NAME, __LINE__);                           \
    _dbus_assert_not_reached ("test failed");                                           \
 }                                                                                      \
 check_expected_type (reader, DBUS_TYPE_INVALID);                                       \
} while (0)

#define SAMPLE_INT32           12345678
#define SAMPLE_INT32_ALTERNATE 53781429
static dbus_bool_t
write_int32 (DataBlock      *block,
             DBusTypeWriter *writer)
{
  dbus_int32_t v = SAMPLE_INT32;

  return _dbus_type_writer_write_basic (writer,
                                        DBUS_TYPE_INT32,
                                        &v);
}

static dbus_bool_t
read_int32 (DataBlock      *block,
            DBusTypeReader *reader)
{
  dbus_int32_t v;

  check_expected_type (reader, DBUS_TYPE_INT32);
  
  _dbus_type_reader_read_basic (reader,
                                (dbus_int32_t*) &v);

  _dbus_assert (v == SAMPLE_INT32);

  return TRUE;
}

static dbus_bool_t
write_struct_of_int32 (DataBlock      *block,
                       DBusTypeWriter *writer)
{
  dbus_int32_t v;
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse_struct (writer,
                                  &sub))
    return FALSE;

  v = SAMPLE_INT32;
  if (!_dbus_type_writer_write_basic (&sub,
                                      DBUS_TYPE_INT32,
                                      &v))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  v = SAMPLE_INT32_ALTERNATE;
  if (!_dbus_type_writer_write_basic (&sub,
                                      DBUS_TYPE_INT32,
                                      &v))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_struct_of_int32 (DataBlock      *block,
                      DBusTypeReader *reader)
{
  dbus_int32_t v;
  DBusTypeReader sub;

  check_expected_type (reader, DBUS_TYPE_STRUCT);
  
  _dbus_type_reader_recurse (reader, &sub);

  check_expected_type (&sub, DBUS_TYPE_INT32);
  
  _dbus_type_reader_read_basic (&sub,
                                (dbus_int32_t*) &v);

  _dbus_assert (v == SAMPLE_INT32);

  NEXT_EXPECTING_TRUE (&sub);
  check_expected_type (&sub, DBUS_TYPE_INT32);
  
  _dbus_type_reader_read_basic (&sub,
                                (dbus_int32_t*) &v);

  _dbus_assert (v == SAMPLE_INT32_ALTERNATE);

  NEXT_EXPECTING_FALSE (&sub);
  
  return TRUE;
}

static dbus_bool_t
write_struct_of_structs (DataBlock      *block,
                         DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse_struct (writer,
                                         &sub))
    return FALSE;

  if (!write_struct_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  if (!write_struct_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  if (!write_struct_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_struct_of_structs (DataBlock      *block,
                        DBusTypeReader *reader)
{
  DBusTypeReader sub;
  
  check_expected_type (reader, DBUS_TYPE_STRUCT);
  
  _dbus_type_reader_recurse (reader, &sub);

  if (!read_struct_of_int32 (block, &sub))
    return FALSE;

  NEXT_EXPECTING_TRUE (&sub);
  if (!read_struct_of_int32 (block, &sub))
    return FALSE;

  NEXT_EXPECTING_TRUE (&sub);
  if (!read_struct_of_int32 (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_FALSE (&sub);
  
  return TRUE;
}

static dbus_bool_t
write_struct_of_structs_of_structs (DataBlock      *block,
                                    DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse_struct (writer,
                                         &sub))
    return FALSE;

  if (!write_struct_of_structs (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  if (!write_struct_of_structs (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_struct_of_structs_of_structs (DataBlock      *block,
                                   DBusTypeReader *reader)
{
  DBusTypeReader sub;
  
  check_expected_type (reader, DBUS_TYPE_STRUCT);
  
  _dbus_type_reader_recurse (reader, &sub);

  if (!read_struct_of_structs (block, &sub))
    return FALSE;

  NEXT_EXPECTING_TRUE (&sub);
  if (!read_struct_of_structs (block, &sub))
    return FALSE;

  NEXT_EXPECTING_FALSE (&sub);
  
  return TRUE;
}

static dbus_bool_t
write_array_of_int32 (DataBlock      *block,
                      DBusTypeWriter *writer)
{
  dbus_int32_t v;
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse_array (writer,
                                        DBUS_TYPE_INT32_AS_STRING,
                                        &sub))
    return FALSE;

  v = SAMPLE_INT32_ALTERNATE;
  if (!_dbus_type_writer_write_basic (&sub,
                                      DBUS_TYPE_INT32,
                                      &v))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  v = SAMPLE_INT32;
  if (!_dbus_type_writer_write_basic (&sub,
                                      DBUS_TYPE_INT32,
                                      &v))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  v = SAMPLE_INT32;
  if (!_dbus_type_writer_write_basic (&sub,
                                      DBUS_TYPE_INT32,
                                      &v))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_array_of_int32 (DataBlock      *block,
                     DBusTypeReader *reader)
{
  dbus_int32_t v;
  DBusTypeReader sub;

  check_expected_type (reader, DBUS_TYPE_ARRAY);
  
  _dbus_type_reader_recurse (reader, &sub);

  check_expected_type (&sub, DBUS_TYPE_INT32);
  
  _dbus_type_reader_read_basic (&sub,
                                (dbus_int32_t*) &v);

  _dbus_assert (v == SAMPLE_INT32_ALTERNATE);

  NEXT_EXPECTING_TRUE (&sub);
  check_expected_type (&sub, DBUS_TYPE_INT32);
  
  _dbus_type_reader_read_basic (&sub,
                                (dbus_int32_t*) &v);

  _dbus_assert (v == SAMPLE_INT32);

  NEXT_EXPECTING_TRUE (&sub);
  check_expected_type (&sub, DBUS_TYPE_INT32);
  
  _dbus_type_reader_read_basic (&sub,
                                (dbus_int32_t*) &v);

  _dbus_assert (v == SAMPLE_INT32);

  NEXT_EXPECTING_FALSE (&sub);
  
  return TRUE;
}


static dbus_bool_t
write_array_of_int32_empty (DataBlock      *block,
                            DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse_array (writer,
                                        DBUS_TYPE_INT32_AS_STRING,
                                        &sub))
    return FALSE;
  
  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_array_of_int32_empty (DataBlock      *block,
                           DBusTypeReader *reader)
{
  check_expected_type (reader, DBUS_TYPE_ARRAY);

  /* We are iterating over values not types. Thus we can't recurse
   * into the array
   */
  _dbus_assert (_dbus_type_reader_array_is_empty (reader));
  
  return TRUE;
}

static dbus_bool_t
write_array_of_array_of_int32 (DataBlock      *block,
                               DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse_array (writer,
                                        DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_INT32_AS_STRING,
                                        &sub))
    return FALSE;

  if (!write_array_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!write_array_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!write_array_of_int32_empty (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  if (!write_array_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_array_of_array_of_int32 (DataBlock      *block,
                              DBusTypeReader *reader)
{
  DBusTypeReader sub;
  
  check_expected_type (reader, DBUS_TYPE_ARRAY);
  
  _dbus_type_reader_recurse (reader, &sub);

  if (!read_array_of_int32 (block, &sub))
    return FALSE;

  NEXT_EXPECTING_TRUE (&sub);
  if (!read_array_of_int32 (block, &sub))
    return FALSE;

  NEXT_EXPECTING_TRUE (&sub);
  if (!read_array_of_int32_empty (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_TRUE (&sub);
  if (!read_array_of_int32 (block, &sub))
    return FALSE;

  NEXT_EXPECTING_FALSE (&sub);
  
  return TRUE;
}


static dbus_bool_t
write_array_of_array_of_int32_empty (DataBlock      *block,
                                     DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse_array (writer,
                                        DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_INT32_AS_STRING,
                                        &sub))
    return FALSE;

  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_array_of_array_of_int32_empty (DataBlock      *block,
                                    DBusTypeReader *reader)
{  
  check_expected_type (reader, DBUS_TYPE_ARRAY);

  /* We are iterating over values, not types. Thus
   * we can't recurse in here.
   */
  
  _dbus_assert (_dbus_type_reader_array_is_empty (reader));
  
  return TRUE;
}

static dbus_bool_t
write_array_of_array_of_array_of_int32 (DataBlock      *block,
                                        DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse_array (writer,
                                        DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_INT32_AS_STRING,
                                        &sub))
    return FALSE;

  if (!write_array_of_array_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!write_array_of_array_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!write_array_of_array_of_int32_empty (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_array_of_array_of_array_of_int32 (DataBlock      *block,
                                       DBusTypeReader *reader)
{
  DBusTypeReader sub;
  
  check_expected_type (reader, DBUS_TYPE_ARRAY);
  
  _dbus_type_reader_recurse (reader, &sub);

  if (!read_array_of_array_of_int32 (block, &sub))
    return FALSE;

  NEXT_EXPECTING_TRUE (&sub);
  if (!read_array_of_array_of_int32 (block, &sub))
    return FALSE;

  NEXT_EXPECTING_TRUE (&sub);
  if (!read_array_of_array_of_int32_empty (block, &sub))
    return FALSE;

  NEXT_EXPECTING_FALSE (&sub);
  
  return TRUE;
}

static dbus_bool_t
write_struct_of_array_of_int32 (DataBlock      *block,
                                DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse_struct (writer,
                                         &sub))
    return FALSE;

  if (!write_array_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!write_array_of_int32_empty (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
      
  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_struct_of_array_of_int32 (DataBlock      *block,
                               DBusTypeReader *reader)
{
  DBusTypeReader sub;

  check_expected_type (reader, DBUS_TYPE_STRUCT);
  
  _dbus_type_reader_recurse (reader, &sub);

  check_expected_type (&sub, DBUS_TYPE_ARRAY);

  if (!read_array_of_int32 (block, &sub))
    return FALSE;

  NEXT_EXPECTING_TRUE (&sub);
  if (!read_array_of_int32_empty (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_FALSE (&sub);
  
  return TRUE;
}

static dbus_bool_t
write_struct_of_struct_of_array_of_int32 (DataBlock      *block,
                                          DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse_struct (writer,
                                         &sub))
    return FALSE;

  if (!write_struct_of_array_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  if (!write_struct_of_array_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  if (!write_struct_of_array_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_struct_of_struct_of_array_of_int32 (DataBlock      *block,
                                         DBusTypeReader *reader)
{
  DBusTypeReader sub;
  
  check_expected_type (reader, DBUS_TYPE_STRUCT);
  
  _dbus_type_reader_recurse (reader, &sub);

  if (!read_struct_of_array_of_int32 (block, &sub))
    return FALSE;

  NEXT_EXPECTING_TRUE (&sub);
  if (!read_struct_of_array_of_int32 (block, &sub))
    return FALSE;

  NEXT_EXPECTING_TRUE (&sub);
  if (!read_struct_of_array_of_int32 (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_FALSE (&sub);
  
  return TRUE;
}

static dbus_bool_t
write_array_of_struct_of_int32 (DataBlock      *block,
                                DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);

  if (!_dbus_type_writer_recurse_array (writer,
                                        DBUS_STRUCT_BEGIN_CHAR_AS_STRING
                                        DBUS_TYPE_INT32_AS_STRING
                                        DBUS_TYPE_INT32_AS_STRING
                                        DBUS_STRUCT_END_CHAR_AS_STRING,
                                        &sub))
    return FALSE;

  if (!write_struct_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!write_struct_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!write_struct_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_array_of_struct_of_int32 (DataBlock      *block,
                               DBusTypeReader *reader)
{
  DBusTypeReader sub;

  check_expected_type (reader, DBUS_TYPE_ARRAY);
  
  _dbus_type_reader_recurse (reader, &sub);

  check_expected_type (&sub, DBUS_TYPE_STRUCT);

  if (!read_struct_of_int32 (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_TRUE (&sub);

  if (!read_struct_of_int32 (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_TRUE (&sub);

  if (!read_struct_of_int32 (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_FALSE (&sub);
  
  return TRUE;
}


static dbus_bool_t
write_array_of_array_of_struct_of_int32 (DataBlock      *block,
                                         DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);

  if (!_dbus_type_writer_recurse_array (writer,
                                        DBUS_TYPE_ARRAY_AS_STRING
                                        DBUS_STRUCT_BEGIN_CHAR_AS_STRING
                                        DBUS_TYPE_INT32_AS_STRING
                                        DBUS_TYPE_INT32_AS_STRING
                                        DBUS_STRUCT_END_CHAR_AS_STRING,
                                        &sub))
    return FALSE;

  if (!write_array_of_struct_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!write_array_of_struct_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!write_array_of_struct_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_array_of_array_of_struct_of_int32 (DataBlock      *block,
                                        DBusTypeReader *reader)
{
  DBusTypeReader sub;

  check_expected_type (reader, DBUS_TYPE_ARRAY);
  
  _dbus_type_reader_recurse (reader, &sub);

  check_expected_type (&sub, DBUS_TYPE_ARRAY);

  if (!read_array_of_struct_of_int32 (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_TRUE (&sub);

  if (!read_array_of_struct_of_int32 (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_TRUE (&sub);

  if (!read_array_of_struct_of_int32 (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_FALSE (&sub);
  
  return TRUE;
}

static dbus_bool_t
write_struct_of_array_of_struct_of_int32 (DataBlock      *block,
                                          DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse_struct (writer,
                                         &sub))
    return FALSE;

  if (!write_array_of_struct_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  if (!write_array_of_struct_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  if (!write_array_of_struct_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_struct_of_array_of_struct_of_int32 (DataBlock      *block,
                                         DBusTypeReader *reader)
{
  DBusTypeReader sub;
  
  check_expected_type (reader, DBUS_TYPE_STRUCT);
  
  _dbus_type_reader_recurse (reader, &sub);
  
  if (!read_array_of_struct_of_int32 (block, &sub))
    return FALSE;

  NEXT_EXPECTING_TRUE (&sub);
  if (!read_array_of_struct_of_int32 (block, &sub))
    return FALSE;

  NEXT_EXPECTING_TRUE (&sub);
  if (!read_array_of_struct_of_int32 (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_FALSE (&sub);
  
  return TRUE;
}

static dbus_bool_t
write_array_of_struct_of_array_of_int32 (DataBlock      *block,
                                         DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);

  if (!_dbus_type_writer_recurse_array (writer,
                                        DBUS_STRUCT_BEGIN_CHAR_AS_STRING
                                        DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_INT32_AS_STRING
                                        DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_INT32_AS_STRING
                                        DBUS_STRUCT_END_CHAR_AS_STRING,
                                        &sub))
    return FALSE;

  if (!write_struct_of_array_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!write_struct_of_array_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }

  if (!write_struct_of_array_of_int32 (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
read_array_of_struct_of_array_of_int32 (DataBlock      *block,
                                        DBusTypeReader *reader)
{
  DBusTypeReader sub;

  check_expected_type (reader, DBUS_TYPE_ARRAY);
  
  _dbus_type_reader_recurse (reader, &sub);

  check_expected_type (&sub, DBUS_TYPE_STRUCT);

  if (!read_struct_of_array_of_int32 (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_TRUE (&sub);

  if (!read_struct_of_array_of_int32 (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_TRUE (&sub);

  if (!read_struct_of_array_of_int32 (block, &sub))
    return FALSE;
  
  NEXT_EXPECTING_FALSE (&sub);
  
  return TRUE;
}

typedef enum {
  ITEM_INVALID = -1,

  ITEM_INT32 = 0,

  ITEM_STRUCT_OF_INT32,
  ITEM_STRUCT_OF_STRUCTS,
  ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS,

  ITEM_ARRAY_OF_INT32,
  ITEM_ARRAY_OF_INT32_EMPTY,
  ITEM_ARRAY_OF_ARRAY_OF_INT32,
  ITEM_ARRAY_OF_ARRAY_OF_INT32_EMPTY,
  ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32,

  ITEM_STRUCT_OF_ARRAY_OF_INT32,
  ITEM_STRUCT_OF_STRUCT_OF_ARRAY_OF_INT32,

  ITEM_ARRAY_OF_STRUCT_OF_INT32,
  ITEM_ARRAY_OF_ARRAY_OF_STRUCT_OF_INT32,

  ITEM_STRUCT_OF_ARRAY_OF_STRUCT_OF_INT32,
  ITEM_ARRAY_OF_STRUCT_OF_ARRAY_OF_INT32,

  ITEM_LAST
} WhichItem;


typedef dbus_bool_t (* WriteItemFunc) (DataBlock      *block,
                                       DBusTypeWriter *writer);
typedef dbus_bool_t (* ReadItemFunc)  (DataBlock      *block,
                                       DBusTypeReader *reader);

typedef struct
{
  const char *desc;
  WhichItem which;
  WriteItemFunc write_item_func;
  ReadItemFunc read_item_func;
} CheckMarshalItem;

static CheckMarshalItem items[] = {
  { "int32",
    ITEM_INT32, write_int32, read_int32 },
  { "struct with two int32",
    ITEM_STRUCT_OF_INT32, write_struct_of_int32, read_struct_of_int32 },
  { "struct with three structs of two int32",
    ITEM_STRUCT_OF_STRUCTS, write_struct_of_structs, read_struct_of_structs },
  { "struct of two structs of three structs of two int32",
    ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS,
    write_struct_of_structs_of_structs,
    read_struct_of_structs_of_structs },
  { "array of int32",
    ITEM_ARRAY_OF_INT32, write_array_of_int32, read_array_of_int32 },
  { "empty array of int32",
    ITEM_ARRAY_OF_INT32_EMPTY, write_array_of_int32_empty, read_array_of_int32_empty },
  { "array of array of int32",
    ITEM_ARRAY_OF_ARRAY_OF_INT32,
    write_array_of_array_of_int32, read_array_of_array_of_int32 },
  { "empty array of array of int32",
    ITEM_ARRAY_OF_ARRAY_OF_INT32_EMPTY,
    write_array_of_array_of_int32_empty, read_array_of_array_of_int32_empty },
  { "array of array of array of int32",
    ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32,
    write_array_of_array_of_array_of_int32, read_array_of_array_of_array_of_int32 },
  { "struct of array of int32",
    ITEM_STRUCT_OF_ARRAY_OF_INT32, write_struct_of_array_of_int32, read_struct_of_array_of_int32 },
  { "struct of struct of array of int32",
    ITEM_STRUCT_OF_STRUCT_OF_ARRAY_OF_INT32,
    write_struct_of_struct_of_array_of_int32, read_struct_of_struct_of_array_of_int32 },
  { "array of struct of int32",
    ITEM_ARRAY_OF_STRUCT_OF_INT32, write_array_of_struct_of_int32, read_array_of_struct_of_int32 },
  { "array of array of struct of int32",
    ITEM_ARRAY_OF_ARRAY_OF_STRUCT_OF_INT32,
    write_array_of_array_of_struct_of_int32, read_array_of_array_of_struct_of_int32 },

  { "struct of array of struct of int32",
    ITEM_STRUCT_OF_ARRAY_OF_STRUCT_OF_INT32,
    write_struct_of_array_of_struct_of_int32, read_struct_of_array_of_struct_of_int32 },
  { "array of struct of array of int32",
    ITEM_ARRAY_OF_STRUCT_OF_ARRAY_OF_INT32,
    write_array_of_struct_of_array_of_int32, read_array_of_struct_of_array_of_int32 },
};

typedef struct
{
  /* Array of items from the above items[]; -1 terminated */
  int items[20];
} TestRun;

static TestRun runs[] = {
  { { ITEM_INVALID } },

  /* INT32 */
  { { ITEM_INT32, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_INT32, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_INT32, ITEM_INT32, ITEM_INT32, ITEM_INT32, ITEM_INVALID } },

  /* STRUCT_OF_INT32 */
  { { ITEM_STRUCT_OF_INT32, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_INT32, ITEM_STRUCT_OF_INT32, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_INT32, ITEM_INT32, ITEM_STRUCT_OF_INT32, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_STRUCT_OF_INT32, ITEM_INT32, ITEM_STRUCT_OF_INT32, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_STRUCT_OF_INT32, ITEM_INT32, ITEM_INT32, ITEM_INT32, ITEM_STRUCT_OF_INT32, ITEM_INVALID } },

  /* STRUCT_OF_STRUCTS */
  { { ITEM_STRUCT_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS, ITEM_STRUCT_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS, ITEM_INT32, ITEM_STRUCT_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_INT32, ITEM_STRUCT_OF_STRUCTS, ITEM_INT32, ITEM_STRUCT_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_STRUCT_OF_STRUCTS, ITEM_INT32, ITEM_STRUCT_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS, ITEM_STRUCT_OF_STRUCTS, ITEM_STRUCT_OF_STRUCTS, ITEM_INVALID } },

  /* STRUCT_OF_STRUCTS_OF_STRUCTS */
  { { ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INT32, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_INT32, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INT32, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INT32, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },

  /* ARRAY_OF_INT32 */
  { { ITEM_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_INT32, ITEM_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_INT32, ITEM_ARRAY_OF_INT32, ITEM_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_INT32, ITEM_ARRAY_OF_INT32, ITEM_ARRAY_OF_INT32, ITEM_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_INT32, ITEM_INT32, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_ARRAY_OF_INT32, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_ARRAY_OF_INT32, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_INT32, ITEM_ARRAY_OF_INT32, ITEM_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_INT32, ITEM_INT32, ITEM_ARRAY_OF_INT32, ITEM_INVALID } },

  /* ARRAY_OF_ARRAY_OF_INT32 */
  { { ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_INT32, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_INT32, ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },

  /* ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32 */
  { { ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_INT32, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_INT32, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },

  /* STRUCT_OF_ARRAY_OF_INT32 */
  { { ITEM_STRUCT_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_ARRAY_OF_INT32, ITEM_STRUCT_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_ARRAY_OF_INT32, ITEM_INT32, ITEM_STRUCT_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_INT32, ITEM_STRUCT_OF_ARRAY_OF_INT32, ITEM_INT32, ITEM_STRUCT_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_STRUCT_OF_ARRAY_OF_INT32, ITEM_INT32, ITEM_STRUCT_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_ARRAY_OF_INT32, ITEM_STRUCT_OF_ARRAY_OF_INT32, ITEM_STRUCT_OF_ARRAY_OF_INT32, ITEM_INVALID } },

  /* STRUCT_OF_STRUCT_OF_ARRAY_OF_INT32 */
  { { ITEM_STRUCT_OF_STRUCT_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  
  /* ARRAY_OF_STRUCT_OF_INT32 */
  { { ITEM_ARRAY_OF_STRUCT_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_STRUCT_OF_INT32, ITEM_ARRAY_OF_STRUCT_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_STRUCT_OF_INT32, ITEM_INT32, ITEM_ARRAY_OF_STRUCT_OF_INT32, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_INT32, ITEM_ARRAY_OF_STRUCT_OF_INT32, ITEM_INT32, ITEM_ARRAY_OF_STRUCT_OF_INT32, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_ARRAY_OF_STRUCT_OF_INT32, ITEM_INT32, ITEM_ARRAY_OF_STRUCT_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_STRUCT_OF_INT32, ITEM_ARRAY_OF_STRUCT_OF_INT32, ITEM_ARRAY_OF_STRUCT_OF_INT32, ITEM_INVALID } },

  /* ARRAY_OF_ARRAY_OF_STRUCT_OF_INT32 */
  { { ITEM_ARRAY_OF_ARRAY_OF_STRUCT_OF_INT32, ITEM_INVALID } },
  
  /* STRUCT_OF_ARRAY_OF_STRUCT_OF_INT32 */
  { { ITEM_STRUCT_OF_ARRAY_OF_STRUCT_OF_INT32, ITEM_INVALID } },
  
  /* ARRAY_OF_STRUCT_OF_ARRAY_OF_INT32 */
  { { ITEM_ARRAY_OF_STRUCT_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  
};

static dbus_bool_t
perform_one_run (DataBlock *block,
                 int        byte_order,
                 TestRun   *run)
{
  DBusTypeReader reader;
  DBusTypeWriter writer;
  int i;
  DataBlockState saved;
  dbus_bool_t retval;

  retval = FALSE;

  {
    _dbus_verbose ("run byteorder %s items ",
                   byte_order == DBUS_LITTLE_ENDIAN ? "little" : "big");
    i = 0;
    while (run->items[i] != ITEM_INVALID)
      {
        CheckMarshalItem *item = &items[run->items[i]];
        
        _dbus_verbose ("%s ", item->desc);
        ++i;
      }
    _dbus_verbose (" = %d items\n", i);
  }
  
  data_block_save (block, &saved);
  
  data_block_init_reader_writer (block, 
                                 byte_order,
                                 &reader, &writer);

  i = 0;
  while (run->items[i] != ITEM_INVALID)
    {
      CheckMarshalItem *item = &items[run->items[i]];

      _dbus_verbose (">>writing %s\n", item->desc);
      
      if (!(* item->write_item_func) (block, &writer))
        goto out;
      ++i;
    }

  i = 0;
  while (run->items[i] != ITEM_INVALID)
    {
      CheckMarshalItem *item = &items[run->items[i]];

      _dbus_verbose (">>data for reading %s\n", item->desc);
      
      _dbus_verbose_bytes_of_string (reader.type_str, 0,
                                     _dbus_string_get_length (reader.type_str));
      _dbus_verbose_bytes_of_string (reader.value_str, 0,
                                     _dbus_string_get_length (reader.value_str));
      
      _dbus_verbose (">>reading %s\n", item->desc);
      
      if (!(* item->read_item_func) (block, &reader))
        goto out;

      _dbus_type_reader_next (&reader);
      
      ++i;
    }
  
  retval = TRUE;
  
 out:
  data_block_restore (block, &saved);
  return retval;
}

static dbus_bool_t
perform_all_runs (int byte_order,
                  int initial_offset)
{
  int i;
  DataBlock block;
  dbus_bool_t retval;

  retval = FALSE;
  
  if (!data_block_init (&block))
    return FALSE;

  if (!_dbus_string_lengthen (&block.signature, initial_offset))
    goto out;
  
  if (!_dbus_string_lengthen (&block.body, initial_offset))
    goto out;
  
  i = 0;
  while (i < _DBUS_N_ELEMENTS (runs))
    {
      if (!perform_one_run (&block, byte_order, &runs[i]))
        goto out;
      
      ++i;
    }

  retval = TRUE;
  
 out:
  data_block_free (&block);
  
  return retval;
}

static dbus_bool_t
perform_all_items (int byte_order,
                   int initial_offset)
{
  int i;
  DataBlock block;
  dbus_bool_t retval;
  TestRun run;

  retval = FALSE;
  
  if (!data_block_init (&block))
    return FALSE;


  if (!_dbus_string_lengthen (&block.signature, initial_offset))
    goto out;
  
  if (!_dbus_string_lengthen (&block.body, initial_offset))
    goto out;

  /* Create a run containing all the items */
  i = 0;
  while (i < _DBUS_N_ELEMENTS (items))
    {
      _dbus_assert (i == items[i].which);
      
      run.items[i] = items[i].which;
      
      ++i;
    }
  
  run.items[i] = ITEM_INVALID;

  if (!perform_one_run (&block, byte_order, &run))
    goto out;  
  
  retval = TRUE;
  
 out:
  data_block_free (&block);
  
  return retval;
}

static dbus_bool_t
recursive_marshal_test_iteration (void *data)
{
  int i;

  i = 0;
  while (i < 18)
    {
      if (!perform_all_runs (DBUS_LITTLE_ENDIAN, i))
        return FALSE;
      if (!perform_all_runs (DBUS_BIG_ENDIAN, i))
        return FALSE;
      if (!perform_all_items (DBUS_LITTLE_ENDIAN, i))
        return FALSE;
      if (!perform_all_items (DBUS_BIG_ENDIAN, i))
        return FALSE;
      
      ++i;
    }

  return TRUE;
}

typedef struct TestTypeNode               TestTypeNode;
typedef struct TestTypeNodeClass          TestTypeNodeClass;
typedef struct TestTypeNodeContainer      TestTypeNodeContainer;
typedef struct TestTypeNodeContainerClass TestTypeNodeContainerClass;

struct TestTypeNode
{
  const TestTypeNodeClass *klass;
  void *data; /* some data, such as the particular value we wrote that we expect to read again */
};

struct TestTypeNodeContainer
{
  TestTypeNode base;
  DBusList    *children;
};

struct TestTypeNodeClass
{
  int typecode;

  int instance_size;
  
  dbus_bool_t   (* construct)     (TestTypeNode   *node);
  void          (* destroy)       (TestTypeNode   *node);
  
  dbus_bool_t (* write_value)     (TestTypeNode   *node,
                                   DataBlock      *block,
                                   DBusTypeWriter *writer);
  dbus_bool_t (* read_value)      (TestTypeNode   *node,
                                   DataBlock      *block,
                                   DBusTypeReader *reader);
  dbus_bool_t (* build_signature) (TestTypeNode   *node,
                                   DBusString     *str);
};

struct TestTypeNodeContainerClass
{
  TestTypeNodeClass base;
};

static dbus_bool_t int32_write_value        (TestTypeNode   *node,
                                             DataBlock      *block,
                                             DBusTypeWriter *writer);
static dbus_bool_t int32_read_value         (TestTypeNode   *node,
                                             DataBlock      *block,
                                             DBusTypeReader *reader);
static dbus_bool_t struct_1_write_value     (TestTypeNode   *node,
                                             DataBlock      *block,
                                             DBusTypeWriter *writer);
static dbus_bool_t struct_1_read_value      (TestTypeNode   *node,
                                             DataBlock      *block,
                                             DBusTypeReader *reader);
static dbus_bool_t struct_1_build_signature (TestTypeNode   *node,
                                             DBusString     *str);
static dbus_bool_t struct_2_write_value     (TestTypeNode   *node,
                                             DataBlock      *block,
                                             DBusTypeWriter *writer);
static dbus_bool_t struct_2_read_value      (TestTypeNode   *node,
                                             DataBlock      *block,
                                             DBusTypeReader *reader);
static dbus_bool_t struct_2_build_signature (TestTypeNode   *node,
                                             DBusString     *str);
static dbus_bool_t array_build_signature    (TestTypeNode   *node,
                                             DBusString     *str);
static dbus_bool_t array_1_write_value      (TestTypeNode   *node,
                                             DataBlock      *block,
                                             DBusTypeWriter *writer);
static dbus_bool_t array_1_read_value       (TestTypeNode   *node,
                                             DataBlock      *block,
                                             DBusTypeReader *reader);
static dbus_bool_t array_0_write_value      (TestTypeNode   *node,
                                             DataBlock      *block,
                                             DBusTypeWriter *writer);
static dbus_bool_t array_0_read_value       (TestTypeNode   *node,
                                             DataBlock      *block,
                                             DBusTypeReader *reader);
static dbus_bool_t array_2_write_value      (TestTypeNode   *node,
                                             DataBlock      *block,
                                             DBusTypeWriter *writer);
static dbus_bool_t array_2_read_value       (TestTypeNode   *node,
                                             DataBlock      *block,
                                             DBusTypeReader *reader);

static void        container_destroy        (TestTypeNode   *node);


static const TestTypeNodeClass int32_class = {
  DBUS_TYPE_INT32,
  sizeof (TestTypeNode),
  NULL,
  NULL,
  int32_write_value,
  int32_read_value,
  NULL
};

static const TestTypeNodeClass uint32_class = {
  DBUS_TYPE_UINT32,
  sizeof (TestTypeNode),
  NULL,
  NULL,
  int32_write_value, /* recycle from int32 */
  int32_read_value,  /* recycle from int32 */
  NULL
};

static const TestTypeNodeClass struct_1_class = {
  DBUS_TYPE_STRUCT,
  sizeof (TestTypeNodeContainer),
  NULL,
  container_destroy,
  struct_1_write_value,
  struct_1_read_value,
  struct_1_build_signature
};

static const TestTypeNodeClass struct_2_class = {
  DBUS_TYPE_STRUCT,
  sizeof (TestTypeNodeContainer),
  NULL,
  container_destroy,
  struct_2_write_value,
  struct_2_read_value,
  struct_2_build_signature
};

static const TestTypeNodeClass array_0_class = {
  DBUS_TYPE_ARRAY,
  sizeof (TestTypeNodeContainer),
  NULL,
  container_destroy,
  array_0_write_value,
  array_0_read_value,
  array_build_signature
};

static const TestTypeNodeClass array_1_class = {
  DBUS_TYPE_ARRAY,
  sizeof (TestTypeNodeContainer),
  NULL,
  container_destroy,
  array_1_write_value,
  array_1_read_value,
  array_build_signature
};

static const TestTypeNodeClass array_2_class = {
  DBUS_TYPE_ARRAY,
  sizeof (TestTypeNodeContainer),
  NULL,
  container_destroy,
  array_2_write_value,
  array_2_read_value,
  array_build_signature
};

static const TestTypeNodeClass* const
basic_nodes[] = {
  &int32_class,
  &uint32_class
};

static const TestTypeNodeClass* const
container_nodes[] = {
  &struct_1_class,
  &array_1_class,
  &struct_2_class,
  &array_0_class,
  &array_2_class
};

static TestTypeNode*
node_new (const TestTypeNodeClass *klass)
{
  TestTypeNode *node;

  node = dbus_malloc0 (klass->instance_size);
  if (node == NULL)
    return NULL;

  node->klass = klass;
  
  if (klass->construct)
    {
      if (!(* klass->construct) (node))
        {
          dbus_free (node);
          return FALSE;
        }
    }

  return node;
}

static void
node_destroy (TestTypeNode *node)
{
  if (node->klass->destroy)
    (* node->klass->destroy) (node);
  dbus_free (node);
}

static dbus_bool_t
node_write_value (TestTypeNode   *node,
                  DataBlock      *block,
                  DBusTypeWriter *writer)
{
  return (* node->klass->write_value) (node, block, writer);
}

static dbus_bool_t
node_read_value (TestTypeNode   *node,
                 DataBlock      *block,
                 DBusTypeReader *reader)
{
  return (* node->klass->read_value) (node, block, reader);
}

static dbus_bool_t
node_build_signature (TestTypeNode *node,
                      DBusString   *str)
{
  if (node->klass->build_signature)
    return (* node->klass->build_signature) (node, str);
  else
    return _dbus_string_append_byte (str, node->klass->typecode);
}

static dbus_bool_t
node_append_child (TestTypeNode *node,
                   TestTypeNode *child)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;

  _dbus_assert (node->klass->instance_size >= (int) sizeof (TestTypeNodeContainer));

  return _dbus_list_append (&container->children, child);
}

typedef struct
{
  const DBusString   *signature;
  DataBlock          *block;
  int                 type_offset;
  int                 byte_order;
  TestTypeNode      **nodes;
  int                 n_nodes;
} NodeIterationData;

static dbus_bool_t
run_test_nodes_iteration (void *data)
{
  NodeIterationData *nid = data;
  DBusTypeReader reader;
  DBusTypeWriter writer;
  int i;

  /* Stuff to do:
   * 1. write the value
   * 2. strcmp-compare with the signature we built
   * 3. read the value
   * 4. type-iterate the signature and the value and see if they are the same type-wise
   */
  data_block_init_reader_writer (nid->block,
                                 nid->byte_order,
                                 &reader, &writer);

  i = 0;
  while (i < nid->n_nodes)
    {
      if (!node_write_value (nid->nodes[i], nid->block, &writer))
        return FALSE;

      ++i;
    }

  if (!_dbus_string_equal_substring (nid->signature, 0, _dbus_string_get_length (nid->signature),
                                     &nid->block->signature, nid->type_offset))
    {
      _dbus_warn ("Expected signature '%s' and got '%s' with initial offset %d\n",
                  _dbus_string_get_const_data (nid->signature),
                  _dbus_string_get_const_data_len (&nid->block->signature, nid->type_offset, 0),
                  nid->type_offset);
      _dbus_assert_not_reached ("wrong signature");
    }

  i = 0;
  while (i < nid->n_nodes)
    {
      if (!node_read_value (nid->nodes[i], nid->block, &reader))
        return FALSE;

      if (i + 1 == nid->n_nodes)
        NEXT_EXPECTING_FALSE (&reader);
      else
        NEXT_EXPECTING_TRUE (&reader);

      ++i;
    }

  /* FIXME type-iterate both signature and value */
  
  return TRUE;
}

static void
run_test_nodes_in_one_configuration (TestTypeNode    **nodes,
                                     int               n_nodes,
                                     const DBusString *signature,
                                     int               byte_order,
                                     int               initial_offset)
{
  DataBlock block;
  NodeIterationData nid;

  if (!data_block_init (&block))
    _dbus_assert_not_reached ("no memory");

  if (!_dbus_string_lengthen (&block.signature, initial_offset))
    _dbus_assert_not_reached ("no memory");
  
  if (!_dbus_string_lengthen (&block.body, initial_offset))
    _dbus_assert_not_reached ("no memory");

  nid.signature = signature;
  nid.block = &block;
  nid.type_offset = initial_offset;
  nid.nodes = nodes;
  nid.n_nodes = n_nodes;
  nid.byte_order = byte_order;
  
  _dbus_test_oom_handling ("running test node",
                           run_test_nodes_iteration,
                           &nid);

  data_block_free (&block);
}

static void
run_test_nodes (TestTypeNode **nodes,
                int            n_nodes)
{
  int i;
  DBusString signature;

  if (!_dbus_string_init (&signature))
    _dbus_assert_not_reached ("no memory");

  i = 0;
  while (i < n_nodes)
    {
      if (! node_build_signature (nodes[i], &signature))
        _dbus_assert_not_reached ("no memory");

      ++i;
    }
      
  _dbus_verbose (">>> test nodes with signature '%s'\n",
                 _dbus_string_get_const_data (&signature));
  
  i = 0;
  while (i < 18)
    {
      run_test_nodes_in_one_configuration (nodes, n_nodes, &signature,
                                           DBUS_LITTLE_ENDIAN, i);
      run_test_nodes_in_one_configuration (nodes, n_nodes, &signature,
                                           DBUS_BIG_ENDIAN, i);
      
      ++i;
    }

  _dbus_string_free (&signature);
}

#define N_VALUES (_DBUS_N_ELEMENTS (basic_nodes) * _DBUS_N_ELEMENTS (container_nodes) + _DBUS_N_ELEMENTS (basic_nodes))

static TestTypeNode*
value_generator (int *ip)
{
  int i = *ip;
  const TestTypeNodeClass *child_klass;
  const TestTypeNodeClass *container_klass;
  TestTypeNode *child;
  TestTypeNode *node;
  
  if (i == N_VALUES)
    {
      return NULL;
    }
  else if (i < _DBUS_N_ELEMENTS (basic_nodes))
    {
      node = node_new (basic_nodes[i]);
    }
  else
    {      
      /* imagine an array:
       * container 0 of basic 0
       * container 0 of basic 1
       * container 0 of basic 2
       * container 1 of basic 0
       * container 1 of basic 1
       * container 1 of basic 2
       */
      i -= _DBUS_N_ELEMENTS (basic_nodes);

      container_klass = container_nodes[i / _DBUS_N_ELEMENTS (basic_nodes)];
      child_klass = basic_nodes[i % _DBUS_N_ELEMENTS (basic_nodes)];
      
      node = node_new (container_klass);
      child = node_new (child_klass);
      
      node_append_child (node, child);
    }
  
  *ip += 1; /* increment the generator */

  return node;
}

static void
make_and_run_test_nodes (void)
{
  int i, j;
  
  /* We try to do this in order of "complicatedness" so that test
   * failures tend to show up in the simplest test case that
   * demonstrates the failure.  There are also some tests that run
   * more than once for this reason, first while going through simple
   * cases, second while going through a broader range of complex
   * cases.
   */
  /* Each basic node. The basic nodes should include:
   *
   * - each fixed-size type (in such a way that it has different values each time,
   *                         so we can tell if we mix two of them up)
   * - strings of length 0-9
   * - object path
   * - signature
   */
  /* Each container node. The container nodes should include:
   *
   *  struct with 1 and 2 copies of the contained item
   *  array with 0, 1, 2 copies of the contained item
   *  variant
   */
  /*  Let a "value" be a basic node, or a container containing a single basic node.
   *  Let n_values be the number of such values i.e. (n_container * n_basic + n_basic)
   *  When iterating through all values to make combinations, do the basic types
   *  first and the containers second.
   */
  /* Each item is shown with its number of iterations so we can keep a handle
   * on this unit test
   */
  
  /* n_basic iterations */
  _dbus_verbose (">>> >>> Each basic node by itself\n");
  for (i = 0; i < _DBUS_N_ELEMENTS (basic_nodes); i++)
    {
      const TestTypeNodeClass *klass = basic_nodes[i];
      TestTypeNode *node;
      
      node = node_new (klass);

      run_test_nodes (&node, 1);

      node_destroy (node);
    }
  
  /* n_container * n_basic iterations */
  _dbus_verbose (">>> >>> Each container of each basic (redundant with later tests)\n");
  for (i = 0; i < _DBUS_N_ELEMENTS (container_nodes); i++)
    {
      const TestTypeNodeClass *container_klass = container_nodes[i];
      for (j = 0; j < _DBUS_N_ELEMENTS (basic_nodes); j++)
        {
          const TestTypeNodeClass *child_klass = basic_nodes[j];
          TestTypeNode *child;
          TestTypeNode *container;
      
          container = node_new (container_klass);
          child = node_new (child_klass);

          node_append_child (container, child);
          
          run_test_nodes (&container, 1);

          node_destroy (container);
        }
    }
  
  /* n_values * n_values * 2 iterations */
  _dbus_verbose (">>> >>> Each value,value pair combination as toplevel, in both orders\n");
  {
    TestTypeNode *nodes[2];

    i = 0;
    while ((nodes[0] = value_generator (&i)))
      {
        j = 0;
        while ((nodes[1] = value_generator (&j)))
          {
            run_test_nodes (nodes, 2);

            node_destroy (nodes[1]);
          }

        node_destroy (nodes[0]);
      }
  }

  /* 1 iteration */
  _dbus_verbose (">>> >>> All values in one big toplevel\n");
  {
    TestTypeNode *nodes[N_VALUES];

    i = 0;
    while ((nodes[i] = value_generator (&i)))
      ;

    run_test_nodes (nodes, N_VALUES);

    for (i = 0; i < N_VALUES; i++)
      node_destroy (nodes[i]);
  }
  
  /* n_container * n_values iterations */
  _dbus_verbose (">>> >>> Each container containing each value\n");
  for (i = 0; i < _DBUS_N_ELEMENTS (container_nodes); i++)
    {
      const TestTypeNodeClass *container_klass = container_nodes[i];
      TestTypeNode *child;
      
      j = 0;
      while ((child = value_generator (&j)))
        {
          TestTypeNode *container;
          
          container = node_new (container_klass);

          node_append_child (container, child);
          
          run_test_nodes (&container, 1);
          
          node_destroy (container);
        }
    }
  
  /* n_container * n_values - container of same container of values */
  
  /* n_container * n_values - container of same container of same container of values */

  /* n_container * n_container * n_values - container of container of values */
  /* n_container * n_container * n_container * n_values - container of container of container of values */

  /* n_values * n_values * n_values * 6 - each trio of value,value,value in all orders */
  
  /* n_values * n_values * 2 - each value,value pair inside STRUCT, in both orders */

  /* 1 - all values in one big STRUCT */
}

dbus_bool_t _dbus_marshal_recursive_test (void);

dbus_bool_t
_dbus_marshal_recursive_test (void)
{
  /* The new comprehensive tests */

#if 1
  make_and_run_test_nodes ();
#endif
  
#if 0
  /* The old tests */
  _dbus_test_oom_handling ("recursive marshaling",
                           recursive_marshal_test_iteration,
                           NULL);  
#endif
  
  return TRUE;
}

#if 1
int
main (int argc, char **argv)
{
  _dbus_marshal_recursive_test ();

  return 0;
}
#endif /* main() */


/*
 *
 *
 *         Implementations of each type node class
 *
 *
 *
 */

static dbus_bool_t
int32_write_value (TestTypeNode   *node,
                   DataBlock      *block,
                   DBusTypeWriter *writer)
{
  /* also used for uint32 */
  dbus_int32_t v = _DBUS_POINTER_TO_INT (node->data);
  
  return _dbus_type_writer_write_basic (writer,
                                        node->klass->typecode,
                                        &v);
}

static dbus_bool_t
int32_read_value (TestTypeNode   *node,
                  DataBlock      *block,
                  DBusTypeReader *reader)
{
  /* also used for uint32 */
  dbus_int32_t v;

  check_expected_type (reader, node->klass->typecode);
  
  _dbus_type_reader_read_basic (reader,
                                (dbus_int32_t*) &v);
  
  _dbus_assert (v == _DBUS_POINTER_TO_INT (node->data));

  return TRUE;
}

static dbus_bool_t
struct_N_write_value (TestTypeNode   *node,
                      DataBlock      *block,
                      DBusTypeWriter *writer,
                      int             n_copies)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  DataBlockState saved;
  DBusTypeWriter sub;
  int i;

  _dbus_assert (container->children != NULL);
  
  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse_struct (writer,
                                         &sub))
    return FALSE;

  i = 0;
  while (i < n_copies)
    {
      DBusList *link;
      
      link = _dbus_list_get_first_link (&container->children);
      while (link != NULL)
        {
          TestTypeNode *child = link->data;
          DBusList *next = _dbus_list_get_next_link (&container->children, link);

          if (!node_write_value (child, block, &sub))
            {
              data_block_restore (block, &saved);
              return FALSE;
            }
          
          link = next;
        }

      ++i;
    }
  
  if (!_dbus_type_writer_unrecurse (writer, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  
  return TRUE;
}

static dbus_bool_t
struct_N_read_value (TestTypeNode   *node,
                     DataBlock      *block,
                     DBusTypeReader *reader,
                     int             n_copies)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  DBusTypeReader sub;
  int i;
  
  check_expected_type (reader, DBUS_TYPE_STRUCT);
  
  _dbus_type_reader_recurse (reader, &sub);

  i = 0;
  while (i < n_copies)
    {
      DBusList *link;
      
      link = _dbus_list_get_first_link (&container->children);
      while (link != NULL)
        {
          TestTypeNode *child = link->data;
          DBusList *next = _dbus_list_get_next_link (&container->children, link);

          node_read_value (child, block, &sub);

          if (i == (n_copies - 1) && next == NULL)
            NEXT_EXPECTING_FALSE (&sub);
          else
            NEXT_EXPECTING_TRUE (&sub);
          
          link = next;
        }

      ++i;
    }
  
  return TRUE;
}

static dbus_bool_t
struct_N_build_signature (TestTypeNode   *node,
                          DBusString     *str,
                          int             n_copies)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  int i;
  int orig_len;

  orig_len = _dbus_string_get_length (str);

  if (!_dbus_string_append_byte (str, DBUS_STRUCT_BEGIN_CHAR))
    goto oom;
  
  i = 0;
  while (i < n_copies)
    {
      DBusList *link;
      
      link = _dbus_list_get_first_link (&container->children);
      while (link != NULL)
        {
          TestTypeNode *child = link->data;
          DBusList *next = _dbus_list_get_next_link (&container->children, link);

          if (!node_build_signature (child, str))
            goto oom;
          
          link = next;
        }

      ++i;
    }

  if (!_dbus_string_append_byte (str, DBUS_STRUCT_END_CHAR))
    goto oom;
  
  return TRUE;
  
 oom:
  _dbus_string_set_length (str, orig_len);
  return FALSE;
}

static dbus_bool_t
struct_1_write_value (TestTypeNode   *node,
                      DataBlock      *block,
                      DBusTypeWriter *writer)
{
  return struct_N_write_value (node, block, writer, 1);
}

static dbus_bool_t
struct_1_read_value (TestTypeNode   *node,
                     DataBlock      *block,
                     DBusTypeReader *reader)
{
  return struct_N_read_value (node, block, reader, 1);
}

static dbus_bool_t
struct_1_build_signature (TestTypeNode   *node,
                          DBusString     *str)
{
  return struct_N_build_signature (node, str, 1);
}


static dbus_bool_t
struct_2_write_value (TestTypeNode   *node,
                      DataBlock      *block,
                      DBusTypeWriter *writer)
{
  return struct_N_write_value (node, block, writer, 2);
}

static dbus_bool_t
struct_2_read_value (TestTypeNode   *node,
                     DataBlock      *block,
                     DBusTypeReader *reader)
{
  return struct_N_read_value (node, block, reader, 2);
}

static dbus_bool_t
struct_2_build_signature (TestTypeNode   *node,
                          DBusString     *str)
{
  return struct_N_build_signature (node, str, 2);
}

static dbus_bool_t
array_N_write_value (TestTypeNode   *node,
                     DataBlock      *block,
                     DBusTypeWriter *writer,
                     int             n_copies)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  DataBlockState saved;
  DBusTypeWriter sub;
  DBusString element_signature;
  int i;

  _dbus_assert (container->children != NULL);

  data_block_save (block, &saved);
  
  if (!_dbus_string_init (&element_signature))
    return FALSE;

  if (!node_build_signature (_dbus_list_get_first (&container->children),
                             &element_signature))
    goto oom;
  
  if (!_dbus_type_writer_recurse_array (writer,
                                        _dbus_string_get_const_data (&element_signature),
                                        &sub))
    goto oom;

  i = 0;
  while (i < n_copies)
    {
      DBusList *link;
      
      link = _dbus_list_get_first_link (&container->children);
      while (link != NULL)
        {
          TestTypeNode *child = link->data;
          DBusList *next = _dbus_list_get_next_link (&container->children, link);

          if (!node_write_value (child, block, &sub))
            goto oom;
          
          link = next;
        }

      ++i;
    }
  
  if (!_dbus_type_writer_unrecurse (writer, &sub))
    goto oom;
  
  return TRUE;

 oom:
  data_block_restore (block, &saved);
  _dbus_string_free (&element_signature);
  return FALSE;
}

static dbus_bool_t
array_N_read_value (TestTypeNode   *node,
                    DataBlock      *block,
                    DBusTypeReader *reader,
                    int             n_copies)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  DBusTypeReader sub;
  int i;
  
  check_expected_type (reader, DBUS_TYPE_ARRAY);

  if (n_copies > 0)
    {
      _dbus_assert (!_dbus_type_reader_array_is_empty (reader));
      
      _dbus_type_reader_recurse (reader, &sub);

      i = 0;
      while (i < n_copies)
        {
          DBusList *link;
          
          link = _dbus_list_get_first_link (&container->children);
          while (link != NULL)
            {
              TestTypeNode *child = link->data;
              DBusList *next = _dbus_list_get_next_link (&container->children, link);
              
              node_read_value (child, block, &sub);
              
              if (i == (n_copies - 1) && next == NULL)
                NEXT_EXPECTING_FALSE (&sub);
              else
                NEXT_EXPECTING_TRUE (&sub);
              
              link = next;
            }
          
          ++i;
        }
    }
  else
    {
      _dbus_assert (_dbus_type_reader_array_is_empty (reader));
    }
    
  return TRUE;
}

static dbus_bool_t
array_build_signature (TestTypeNode   *node,
                       DBusString     *str)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  int orig_len;
  
  orig_len = _dbus_string_get_length (str);

  if (!_dbus_string_append_byte (str, DBUS_TYPE_ARRAY))
    goto oom;

  if (!node_build_signature (_dbus_list_get_first (&container->children),
                             str))
    goto oom;
  
  return TRUE;
  
 oom:
  _dbus_string_set_length (str, orig_len);
  return FALSE;
}

static dbus_bool_t
array_0_write_value (TestTypeNode   *node,
                     DataBlock      *block,
                     DBusTypeWriter *writer)
{
  return array_N_write_value (node, block, writer, 0);
}

static dbus_bool_t
array_0_read_value (TestTypeNode   *node,
                    DataBlock      *block,
                    DBusTypeReader *reader)
{
  return array_N_read_value (node, block, reader, 0);
}


static dbus_bool_t
array_1_write_value (TestTypeNode   *node,
                     DataBlock      *block,
                     DBusTypeWriter *writer)
{
  return array_N_write_value (node, block, writer, 1);
}

static dbus_bool_t
array_1_read_value (TestTypeNode   *node,
                    DataBlock      *block,
                    DBusTypeReader *reader)
{
  return array_N_read_value (node, block, reader, 1);
}

static dbus_bool_t
array_2_write_value (TestTypeNode   *node,
                     DataBlock      *block,
                     DBusTypeWriter *writer)
{
  return array_N_write_value (node, block, writer, 2);
}

static dbus_bool_t
array_2_read_value (TestTypeNode   *node,
                    DataBlock      *block,
                    DBusTypeReader *reader)
{
  return array_N_read_value (node, block, reader, 2);
}

static void
container_destroy (TestTypeNode *node)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  DBusList *link;
  
  link = _dbus_list_get_first_link (&container->children);
  while (link != NULL)
    {
      TestTypeNode *child = link->data;
      DBusList *next = _dbus_list_get_next_link (&container->children, link);

      node_destroy (child);

      _dbus_list_free_link (link);
      
      link = next;
    }
}

#endif /* DBUS_BUILD_TESTS */
