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

void
_dbus_type_reader_init (DBusTypeReader    *reader,
                        int                byte_order,
                        const DBusString  *type_str,
                        int                type_pos,
                        const DBusString  *value_str,
                        int                value_pos)
{
  reader->byte_order = byte_order;
  reader->type_str = type_str;
  reader->type_pos = type_pos;
  reader->value_str = value_str;
  reader->value_pos = value_pos;
  reader->container_type = DBUS_TYPE_INVALID;

  _dbus_verbose ("  type reader %p init type_pos = %d value_pos = %d remaining sig '%s'\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));
}

int
_dbus_type_reader_get_current_type (DBusTypeReader *reader)
{
  int t;

  /* for INVALID t will == DBUS_TYPE_INVALID when we
   * reach the end of type_str, for STRUCT we have to
   * check the finished flag
   */
  if (reader->container_type == DBUS_TYPE_INVALID)
    {
      t = first_type_in_signature (reader->type_str,
                                   reader->type_pos);
    }
  else if (reader->container_type == DBUS_TYPE_STRUCT)
    {
      if (reader->u.strct.finished)
        t = DBUS_TYPE_INVALID;
      else
        t = first_type_in_signature (reader->type_str,
                                     reader->type_pos);
    }
  else if (reader->container_type == DBUS_TYPE_ARRAY)
    {
      /* return the array element type if elements remain, and
       * TYPE_INVALID otherwise
       */
      int end_pos;

      end_pos = reader->u.array.start_pos + reader->u.array.len;

      _dbus_assert (reader->value_pos <= end_pos);
      _dbus_assert (reader->value_pos >= reader->u.array.start_pos);

      if (reader->value_pos < end_pos)
        t = reader->u.array.element_type;
      else
        t = DBUS_TYPE_INVALID;
    }
  else
    {
      _dbus_assert_not_reached ("reader->container_type should not be set to this");
      t = DBUS_TYPE_INVALID; /* quiet gcc */
    }

  _dbus_assert (t != DBUS_STRUCT_END_CHAR);
  _dbus_assert (t != DBUS_STRUCT_BEGIN_CHAR);
  
#if 0
  _dbus_verbose ("  type reader %p current type_pos = %d type = %s\n",
                 reader, reader->type_pos,
                 _dbus_type_to_string (t));
#endif
  
  return t;
}

int
_dbus_type_reader_get_array_length (DBusTypeReader *reader)
{
  /* FIXME if this is in number of elements I don't know how to compute it
   * since we only have bytes and elements are variable-length
   */
}

void
_dbus_type_reader_read_basic (DBusTypeReader    *reader,
                              void              *value)
{
  if (reader->container_type == DBUS_TYPE_INVALID ||
      reader->container_type == DBUS_TYPE_STRUCT ||
      reader->container_type == DBUS_TYPE_ARRAY)
    {
      int t;
      int next;
      
      t = _dbus_type_reader_get_current_type (reader);

      next = reader->value_pos;
      _dbus_demarshal_basic_type (reader->value_str,
                                  t, value,
                                  reader->byte_order,
                                  &next);


      _dbus_verbose ("  type reader %p read basic type_pos = %d value_pos = %d next = %d remaining sig '%s'\n",
                     reader, reader->type_pos, reader->value_pos, next,
                     _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));
      
      _dbus_verbose_bytes_of_string (reader->value_str,
                                     reader->value_pos,
                                     MIN (16,
                                          _dbus_string_get_length (reader->value_str) - reader->value_pos));
    }
  else
    {
      _dbus_assert_not_reached ("reader->container_type should not be set to this");
    }
}

dbus_bool_t
_dbus_type_reader_read_array_of_basic (DBusTypeReader    *reader,
                                       int                type,
                                       void             **array,
                                       int               *array_len)
{
  
  
}

/**
 * Initialize a new reader pointing to the first type and
 * corresponding value that's a child of the current container. It's
 * an error to call this if the current type is a non-container.
 *
 * @param reader the reader
 * @param sub a reader to init pointing to the first child
 */
void
_dbus_type_reader_recurse (DBusTypeReader *reader,
                           DBusTypeReader *sub)
{
  int t;

  /* FIXME are we recursing over the type signature or over the values.
   * Arrays don't necessarily have values for each element of the type
   * signature. Thus we get a mismatch where we need to "bail out" and
   * return the signature of each element, but can't return an element
   * or recurse into the element signature. Not sure how to handle this;
   * maybe think about how we will handle variant types and do something
   * similar since they also have the idea of a signature for the whole
   * sub-item?
   */
  
  t = first_type_in_signature (reader->type_str, reader->type_pos);
  
  /* point subreader at the same place as reader */
  _dbus_type_reader_init (sub,
                          reader->byte_order,
                          reader->type_str,
                          reader->type_pos,
                          reader->value_str,
                          reader->value_pos);

  if (t == DBUS_TYPE_STRUCT)
    {
      sub->container_type = DBUS_TYPE_STRUCT;
      
      sub->type_pos += 1;

      /* struct has 8 byte alignment */
      sub->value_pos = _DBUS_ALIGN_VALUE (sub->value_pos, 8);

      sub->u.strct.finished = FALSE;
    }
  else if (t == DBUS_TYPE_ARRAY)
    {
      dbus_uint32_t array_len;
      int alignment;

      sub->container_type = DBUS_TYPE_ARRAY;
      
      /* point type_pos at the array element type */
      sub->type_pos += 1;

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

      sub->u.array.element_type = first_type_in_signature (sub->type_str,
                                                           sub->type_pos);
      sub->u.array.start_pos = sub->value_pos;

      _dbus_verbose ("    type reader %p array start = %d array len = %d array element type = %s\n",
                     reader,
                     sub->u.array.start_pos,
                     sub->u.array.len,
                     _dbus_type_to_string (sub->u.array.element_type));
    }
  else
    {
      _dbus_verbose ("recursing into type %s\n", _dbus_type_to_string (t));
      if (t == DBUS_TYPE_INVALID)
        _dbus_warn ("You can't recurse into an empty array or off the end of a message body\n");
      
      _dbus_assert_not_reached ("don't yet handle recursing into this type");
    }

  _dbus_verbose ("  type reader %p RECURSED type_pos = %d value_pos = %d remaining sig '%s'\n",
                 sub, sub->type_pos, sub->value_pos,
                 _dbus_string_get_const_data_len (sub->type_str, sub->type_pos, 0));
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
  
  if (reader->container_type == DBUS_TYPE_INVALID ||
      reader->container_type == DBUS_TYPE_STRUCT)
    {
      switch (t)
        {
        case DBUS_TYPE_STRUCT:
        case DBUS_TYPE_ARRAY:
          /* Scan forward over the entire container contents */
          /* FIXME this is super slow for arrays. We need to special
           * case skipping all the elements at once instead of scanning.
           */
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
            reader->value_pos = sub.value_pos;
          }
          break;

        default:
          _dbus_marshal_skip_basic_type (reader->value_str,
                                         t, reader->byte_order,
                                         &reader->value_pos);
          reader->type_pos += 1;
          break;
        }

      /* for STRUCT containers we return FALSE at the end of the struct,
       * for INVALID we return FALSE at the end of the signature.
       * In both cases we arrange for get_current_type() to return INVALID
       * which is defined to happen iff we're at the end (no more next())
       */
      if (reader->container_type == DBUS_TYPE_STRUCT)
        {
          t = _dbus_string_get_byte (reader->type_str, reader->type_pos);
          if (t == DBUS_STRUCT_END_CHAR)
            {
              reader->type_pos += 1;
              reader->u.strct.finished = TRUE;
            }
        }
    }
  else if (reader->container_type == DBUS_TYPE_ARRAY)
    {
      /* Skip one array element */
      int end_pos;

      end_pos = reader->u.array.start_pos + reader->u.array.len;

      _dbus_assert (reader->value_pos < end_pos);
      _dbus_assert (reader->value_pos >= reader->u.array.start_pos);

      if (reader->u.array.element_type == DBUS_TYPE_ARRAY ||
          reader->u.array.element_type == DBUS_TYPE_STRUCT)
        {
          DBusTypeReader sub;
          
          /* Recurse into the array element */
          _dbus_type_reader_recurse (reader, &sub);

            /* Skip everything in this element */
          while (_dbus_type_reader_next (&sub))
            {
              /* nothing */;
            }

          /* Now we are at the end of this element */
          reader->value_pos = sub.value_pos;
        }
      else
        {
          _dbus_marshal_skip_basic_type (reader->value_str,
                                         t, reader->byte_order,
                                         &reader->value_pos);
        }

      _dbus_assert (reader->value_pos <= end_pos);
      
      if (reader->value_pos == end_pos)
        {
          skip_one_complete_type (reader->type_str,
                                  &reader->type_pos);
        }
    }
  else
    {
      _dbus_assert_not_reached ("reader->container_type should not be set to this");
    }
  
  _dbus_verbose ("  type reader %p END next() type_pos = %d value_pos = %d remaining sig '%s' current_type = %s\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0),
                 _dbus_type_to_string (_dbus_type_reader_get_current_type (reader)));
  
  return _dbus_type_reader_get_current_type (reader) != DBUS_TYPE_INVALID;
}

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
  writer->inside_array = FALSE;
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
  
  /* Now insert the type unless we're already covered by the array signature */
  if (!writer->inside_array)
    {
      if (!_dbus_string_insert_byte (writer->type_str,
                                     writer->type_pos,
                                     type))
        _dbus_assert_not_reached ("failed to insert byte after prealloc");
      
      writer->type_pos += 1;
    }
  
  retval = TRUE;
  
 out:
  _dbus_verbose ("  type writer %p basic type_pos = %d value_pos = %d inside_array = %d\n",
                 writer, writer->type_pos, writer->value_pos, writer->inside_array);
  
  return retval;
}

dbus_bool_t
_dbus_type_writer_write_array (DBusTypeWriter *writer,
                               int             type,
                               const void     *array,
                               int             array_len)
{


}

static void
writer_recurse_init_and_check (DBusTypeWriter *writer,
                               int             container_type,
                               const char     *array_element_type,
                               DBusTypeWriter *sub)
{
  _dbus_type_writer_init (sub,
                          writer->byte_order,
                          writer->type_str,
                          writer->type_pos,
                          writer->value_str,
                          writer->value_pos);
  
  sub->container_type = container_type;

  /* While inside an array, we never want to write to the type str.
   * We are inside an array if we're currently recursing into one.
   */
  if (writer->inside_array || sub->container_type == DBUS_TYPE_ARRAY)
    sub->inside_array = TRUE;
  else
    sub->inside_array = FALSE;

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
   */
#ifndef DBUS_DISABLE_CHECKS
  if (writer->container_type == DBUS_TYPE_ARRAY)
    {
      if ((sub->container_type == DBUS_TYPE_STRUCT &&
           writer->u.array.element_type[0] != DBUS_STRUCT_BEGIN_CHAR) ||
          (sub->container_type != DBUS_TYPE_STRUCT &&
           writer->u.array.element_type[0] != sub->container_type))
        {
          _dbus_warn ("Recursing into an array with element type %s not allowed with container type %s\n",
                      writer->u.array.element_type, _dbus_type_to_string (sub->container_type));
        }

      if (sub->container_type == DBUS_TYPE_ARRAY)
        {
          DBusString parent_elements;
          DBusString our_elements;

          _dbus_assert (writer->u.array.element_type[0] == DBUS_TYPE_ARRAY);
          
          _dbus_string_init_const (&parent_elements, &writer->u.array.element_type[1]);
          _dbus_string_init_const (&our_elements, array_element_type);

          if (!_dbus_string_equal (&parent_elements, &our_elements))
            {
              _dbus_warn ("Parent array expects elements '%s' and we are writing an array of '%s'\n",
                          writer->u.array.element_type,
                          array_element_type);
            }
        }
    }
#endif /* DBUS_DISABLE_CHECKS */

  _dbus_verbose ("  type writer %p recurse type_pos = %d value_pos = %d inside_array = %d container_type = %s\n",
                 writer, writer->type_pos, writer->value_pos, writer->inside_array,
                 _dbus_type_to_string (writer->container_type));
  _dbus_verbose ("  type writer %p new sub type_pos = %d value_pos = %d inside_array = %d container_type = %s element_type = '%s'\n",
                 sub, sub->type_pos, sub->value_pos,
                 sub->inside_array,
                 _dbus_type_to_string (sub->container_type),
                 array_element_type ? array_element_type : "n/a");
}

dbus_bool_t
_dbus_type_writer_recurse (DBusTypeWriter *writer,
                           int             container_type,
                           DBusTypeWriter *sub)
{
  writer_recurse_init_and_check (writer, container_type, NULL, sub);
  
  switch (container_type)
    {
    case DBUS_TYPE_STRUCT:
      {
        if (!writer->inside_array)
          {
            /* Ensure that we'll be able to add alignment padding */
            if (!_dbus_string_alloc_space (sub->value_str, 8))
              return FALSE;
            
            if (!_dbus_string_insert_byte (sub->type_str,
                                           sub->type_pos,
                                           DBUS_STRUCT_BEGIN_CHAR))
              return FALSE;

            sub->type_pos += 1;
          }

        if (!_dbus_string_insert_bytes (sub->value_str,
                                        sub->value_pos,
                                        _DBUS_ALIGN_VALUE (sub->value_pos, 8) - sub->value_pos,
                                        '\0'))
          _dbus_assert_not_reached ("should not have failed to insert alignment padding for struct");
        sub->value_pos = _DBUS_ALIGN_VALUE (sub->value_pos, 8);
      }
      break;
    case DBUS_TYPE_ARRAY:
      _dbus_assert_not_reached ("use recurse_array() for arrays");
      break;
    default:
      _dbus_assert_not_reached ("container_type unhandled");
      break;
    }
  
  return TRUE;
}

dbus_bool_t
_dbus_type_writer_recurse_array (DBusTypeWriter *writer,
                                 const char     *element_type,
                                 DBusTypeWriter *sub)
{
  int element_type_len;
  DBusString element_type_str;
  
  writer_recurse_init_and_check (writer, DBUS_TYPE_ARRAY, element_type, sub);

  _dbus_string_init_const (&element_type_str, element_type);
  element_type_len = _dbus_string_get_length (&element_type_str);

  /* 4 bytes for the array length and 4 bytes possible padding */
  if (!_dbus_string_alloc_space (sub->value_str, 8))
    return FALSE;

  if (!writer->inside_array)
    {
      /* alloc space for array typecode, element signature, possible 7
       * bytes of padding
       */
      if (!_dbus_string_alloc_space (sub->type_str, 1 + element_type_len + 7))
        return FALSE;
    }
      
  if (!_dbus_string_copy_data (&element_type_str,
                               &sub->u.array.element_type))
    return FALSE;

  if (!writer->inside_array)
    {
      if (!_dbus_string_insert_byte (sub->type_str,
                                     sub->type_pos,
                                     DBUS_TYPE_ARRAY))
        _dbus_assert_not_reached ("should not have failed to insert array typecode");

      sub->type_pos += 1;
      
      if (!_dbus_string_copy (&element_type_str, 0,
                              sub->type_str, sub->type_pos))
        _dbus_assert_not_reached ("should not have failed to insert array element typecodes");
  
      sub->type_pos += element_type_len;
    }
  
  sub->u.array.len_pos = sub->value_pos;

  {
    dbus_uint32_t value = 0;
    int alignment;
    int aligned;
    DBusString str;
    
    if (!_dbus_type_writer_write_basic_no_typecode (sub, DBUS_TYPE_UINT32,
                                                    &value))
      _dbus_assert_not_reached ("should not have failed to insert array len");

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
  }

  _dbus_assert (sub->u.array.start_pos == sub->value_pos);
  _dbus_assert (sub->u.array.len_pos < sub->u.array.start_pos);
  
  /* value_pos now points to the place for array data, and len_pos to the length */

  return TRUE;
}

dbus_bool_t
_dbus_type_writer_unrecurse (DBusTypeWriter *writer,
                             DBusTypeWriter *sub)
{
  _dbus_assert (sub->type_pos > 0); /* can't be recursed if this fails */

  _dbus_verbose ("  type writer %p unrecurse type_pos = %d value_pos = %d inside_array = %d container_type = %s\n",
                 writer, writer->type_pos, writer->value_pos, writer->inside_array,
                 _dbus_type_to_string (writer->container_type));
  _dbus_verbose ("  type writer %p unrecurse sub type_pos = %d value_pos = %d inside_array = %d container_type = %s element_type = '%s'\n",
                 sub, sub->type_pos, sub->value_pos,
                 sub->inside_array,
                 _dbus_type_to_string (sub->container_type),
                 sub->container_type == DBUS_TYPE_ARRAY ?
                 sub->u.array.element_type : "n/a");
  
  if (sub->container_type == DBUS_TYPE_STRUCT)
    {
      if (!sub->inside_array)
        {
          if (!_dbus_string_insert_byte (sub->type_str,
                                         sub->type_pos, 
                                         DBUS_STRUCT_END_CHAR))
            return FALSE;
          sub->type_pos += 1;
        }
    }
  else if (sub->container_type == DBUS_TYPE_ARRAY)
    {
      dbus_uint32_t len;
      
      dbus_free (sub->u.array.element_type);
      sub->u.array.element_type = NULL;

      /* Set the array length */
      len = sub->value_pos - sub->u.array.start_pos;
      _dbus_marshal_set_uint32 (sub->value_str,
                                sub->byte_order,
                                sub->u.array.len_pos,
                                len);
      _dbus_verbose ("    filled in sub array len to %u at len_pos %d\n",
                     len, sub->u.array.len_pos);
    }

  /* Jump the parent writer to the new location */
  writer->type_pos = sub->type_pos;
  writer->value_pos = sub->value_pos;
  
  return TRUE;
}

/** @} */ /* end of DBusMarshal group */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
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

static void
check_expected_type (DBusTypeReader *reader,
                     int             expected)
{
  int t;

  t = _dbus_type_reader_get_current_type (reader);
  
  if (t != expected)
    {
      _dbus_warn ("Read type %s while expecting %s\n",
                  _dbus_type_to_string (t),
                  _dbus_type_to_string (expected));

      _dbus_verbose_bytes_of_string (reader->type_str, 0,
                                     _dbus_string_get_length (reader->type_str));
      _dbus_verbose_bytes_of_string (reader->value_str, 0,
                                     _dbus_string_get_length (reader->value_str));
      
      exit (1);
    }
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
write_struct_with_int32s (DataBlock      *block,
                          DBusTypeWriter *writer)
{
  dbus_int32_t v;
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse (writer,
                                  DBUS_TYPE_STRUCT,
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
read_struct_with_int32s (DataBlock      *block,
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

  _dbus_type_reader_next (&sub);
  check_expected_type (&sub, DBUS_TYPE_INT32);
  
  _dbus_type_reader_read_basic (&sub,
                                (dbus_int32_t*) &v);

  _dbus_assert (v == SAMPLE_INT32_ALTERNATE);
  
  return TRUE;
}

static dbus_bool_t
write_struct_of_structs (DataBlock      *block,
                         DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse (writer,
                                  DBUS_TYPE_STRUCT,
                                  &sub))
    return FALSE;

  if (!write_struct_with_int32s (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  if (!write_struct_with_int32s (block, &sub))
    {
      data_block_restore (block, &saved);
      return FALSE;
    }
  if (!write_struct_with_int32s (block, &sub))
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

  if (!read_struct_with_int32s (block, &sub))
    return FALSE;
  _dbus_type_reader_next (&sub);
  if (!read_struct_with_int32s (block, &sub))
    return FALSE;
  _dbus_type_reader_next (&sub);
  if (!read_struct_with_int32s (block, &sub))
    return FALSE;
  
  return TRUE;
}

static dbus_bool_t
write_struct_of_structs_of_structs (DataBlock      *block,
                                    DBusTypeWriter *writer)
{
  DataBlockState saved;
  DBusTypeWriter sub;

  data_block_save (block, &saved);
  
  if (!_dbus_type_writer_recurse (writer,
                                  DBUS_TYPE_STRUCT,
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
  _dbus_type_reader_next (&sub);
  if (!read_struct_of_structs (block, &sub))
    return FALSE;
  
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

  _dbus_type_reader_next (&sub);
  check_expected_type (&sub, DBUS_TYPE_INT32);
  
  _dbus_type_reader_read_basic (&sub,
                                (dbus_int32_t*) &v);

  _dbus_assert (v == SAMPLE_INT32);

  _dbus_type_reader_next (&sub);
  check_expected_type (&sub, DBUS_TYPE_INT32);
  
  _dbus_type_reader_read_basic (&sub,
                                (dbus_int32_t*) &v);

  _dbus_assert (v == SAMPLE_INT32);
  
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
  DBusTypeReader sub;

  check_expected_type (reader, DBUS_TYPE_ARRAY);
  
  _dbus_type_reader_recurse (reader, &sub);

  check_expected_type (&sub, DBUS_TYPE_INVALID);
  
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
  _dbus_type_reader_next (&sub);
  if (!read_array_of_int32 (block, &sub))
    return FALSE;
  _dbus_type_reader_next (&sub);
  if (!read_array_of_int32_empty (block, &sub))
    return FALSE;
  _dbus_type_reader_next (&sub);
  if (!read_array_of_int32 (block, &sub))
    return FALSE;
  _dbus_type_reader_next (&sub);
  
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
  DBusTypeReader sub;
  DBusTypeReader sub2;
  
  check_expected_type (reader, DBUS_TYPE_ARRAY);
  
  _dbus_type_reader_recurse (reader, &sub);

  check_expected_type (reader, DBUS_TYPE_ARRAY);

  _dbus_type_reader_recurse (&sub, &sub2);

  check_expected_type (reader, DBUS_TYPE_INVALID);
  
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
  _dbus_type_reader_next (&sub);
  if (!read_array_of_array_of_int32 (block, &sub))
    return FALSE;
  _dbus_type_reader_next (&sub);
  if (!read_array_of_array_of_int32_empty (block, &sub))
    return FALSE;
  _dbus_type_reader_next (&sub);
  
  return TRUE;
}

typedef enum {
  ITEM_INVALID = -1,
  ITEM_INT32 = 0,
  ITEM_STRUCT_WITH_INT32S,
  ITEM_STRUCT_OF_STRUCTS,
  ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS,
  ITEM_ARRAY_OF_INT32,
  ITEM_ARRAY_OF_INT32_EMPTY,
  ITEM_ARRAY_OF_ARRAY_OF_INT32,
  ITEM_ARRAY_OF_ARRAY_OF_INT32_EMPTY,
  ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32,
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
    ITEM_STRUCT_WITH_INT32S, write_struct_with_int32s, read_struct_with_int32s },
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
    write_array_of_array_of_array_of_int32, read_array_of_array_of_array_of_int32 }
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

  /* STRUCT_WITH_INT32S */
  { { ITEM_STRUCT_WITH_INT32S, ITEM_INVALID } },
  { { ITEM_STRUCT_WITH_INT32S, ITEM_STRUCT_WITH_INT32S, ITEM_INVALID } },
  { { ITEM_STRUCT_WITH_INT32S, ITEM_INT32, ITEM_STRUCT_WITH_INT32S, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_STRUCT_WITH_INT32S, ITEM_INT32, ITEM_STRUCT_WITH_INT32S, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_STRUCT_WITH_INT32S, ITEM_INT32, ITEM_INT32, ITEM_INT32, ITEM_STRUCT_WITH_INT32S, ITEM_INVALID } },

  /* STRUCT_OF_STRUCTS */
  { { ITEM_STRUCT_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS, ITEM_STRUCT_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS, ITEM_INT32, ITEM_STRUCT_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_WITH_INT32S, ITEM_STRUCT_OF_STRUCTS, ITEM_INT32, ITEM_STRUCT_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_INT32, ITEM_STRUCT_OF_STRUCTS, ITEM_INT32, ITEM_STRUCT_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS, ITEM_STRUCT_OF_STRUCTS, ITEM_STRUCT_OF_STRUCTS, ITEM_INVALID } },

  /* STRUCT_OF_STRUCTS_OF_STRUCTS */
  { { ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INT32, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
  { { ITEM_STRUCT_WITH_INT32S, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INT32, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } },
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
  { { ITEM_STRUCT_WITH_INT32S, ITEM_ARRAY_OF_INT32, ITEM_ARRAY_OF_INT32, ITEM_INVALID } },
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
  { { ITEM_STRUCT_WITH_INT32S, ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },
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
  { { ITEM_STRUCT_WITH_INT32S, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } },
  { { ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_INT32, ITEM_ARRAY_OF_ARRAY_OF_ARRAY_OF_INT32, ITEM_INVALID } }

  
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

dbus_bool_t _dbus_marshal_recursive_test (void);

dbus_bool_t
_dbus_marshal_recursive_test (void)
{
  _dbus_test_oom_handling ("recursive marshaling",
                           recursive_marshal_test_iteration,
                           NULL);  
  
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

#endif /* DBUS_BUILD_TESTS */
