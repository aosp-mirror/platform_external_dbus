/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-marshal-recursive.c  Marshalling routines for recursive types
 *
 * Copyright (C) 2004, 2005 Red Hat, Inc.
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
#include "dbus-marshal-basic.h"
#include "dbus-internals.h"

/**
 * @addtogroup DBusMarshal
 * @{
 */

/** turn this on to get deluged in TypeReader verbose spam */
#define RECURSIVE_MARSHAL_READ_TRACE  0

/** turn this on to get deluged in TypeWriter verbose spam */
#define RECURSIVE_MARSHAL_WRITE_TRACE 0

static void
free_fixups (DBusList **fixups)
{
  DBusList *link;

  link = _dbus_list_get_first_link (fixups);
  while (link != NULL)
    {
      DBusList *next;

      next = _dbus_list_get_next_link (fixups, link);

      dbus_free (link->data);
      _dbus_list_free_link (link);

      link = next;
    }

  *fixups = NULL;
}

static void
apply_and_free_fixups (DBusList      **fixups,
                       DBusTypeReader *reader)
{
  DBusList *link;

#if RECURSIVE_MARSHAL_WRITE_TRACE
  if (*fixups)
    _dbus_verbose (" %d FIXUPS to apply\n",
                   _dbus_list_get_length (fixups));
#endif

  link = _dbus_list_get_first_link (fixups);
  while (link != NULL)
    {
      DBusList *next;

      next = _dbus_list_get_next_link (fixups, link);

      if (reader)
        {
          DBusArrayLenFixup *f;

          f = link->data;

#if RECURSIVE_MARSHAL_WRITE_TRACE
          _dbus_verbose (" applying FIXUP to reader %p at pos %d new_len = %d old len %d\n",
                         reader, f->len_pos_in_reader, f->new_len,
                         _dbus_marshal_read_uint32 (reader->value_str,
                                                    f->len_pos_in_reader,
                                                    reader->byte_order, NULL));
#endif

          _dbus_marshal_set_uint32 ((DBusString*) reader->value_str,
                                    f->len_pos_in_reader,
                                    f->new_len,
                                    reader->byte_order);
        }

      dbus_free (link->data);
      _dbus_list_free_link (link);

      link = next;
    }

  *fixups = NULL;
}

/**
 * Virtual table for a type reader.
 */
struct DBusTypeReaderClass
{
  const char *name;       /**< name for debugging */
  int         id;         /**< index in all_reader_classes */
  dbus_bool_t types_only; /**< only iterates over types, not values */
  void        (* recurse)          (DBusTypeReader        *sub,
                                    DBusTypeReader        *parent); /**< recurse with this reader as sub */
  dbus_bool_t (* check_finished)   (const DBusTypeReader  *reader); /**< check whether reader is at the end */
  void        (* next)             (DBusTypeReader        *reader,
                                    int                    current_type); /**< go to the next value */
  void        (* init_from_mark)   (DBusTypeReader        *reader,
                                    const DBusTypeMark    *mark);  /**< uncompress from a mark */
};

static int
first_type_in_signature (const DBusString *str,
                         int               pos)
{
  unsigned char t;

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

  /* Init with values likely to crash things if misused */
  sub->u.array.start_pos = _DBUS_INT_MAX;
  sub->array_len_offset = 7;
}

/** compute position of array length given array_len_offset, which is
    the offset back from start_pos to end of the len */
#define ARRAY_READER_LEN_POS(reader) \
  ((reader)->u.array.start_pos - ((int)(reader)->array_len_offset) - 4)

static int
array_reader_get_array_len (const DBusTypeReader *reader)
{
  dbus_uint32_t array_len;
  int len_pos;

  len_pos = ARRAY_READER_LEN_POS (reader);

  _dbus_assert (_DBUS_ALIGN_VALUE (len_pos, 4) == (unsigned) len_pos);
  array_len = _dbus_unpack_uint32 (reader->byte_order,
                                   _dbus_string_get_const_data_len (reader->value_str, len_pos, 4));

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("   reader %p len_pos %d array len %u len_offset %d\n",
                 reader, len_pos, array_len, reader->array_len_offset);
#endif

  _dbus_assert (reader->u.array.start_pos - len_pos - 4 < 8);

  return array_len;
}

static void
array_reader_recurse (DBusTypeReader *sub,
                      DBusTypeReader *parent)
{
  int alignment;
  int len_pos;

  array_types_only_reader_recurse (sub, parent);

  sub->value_pos = _DBUS_ALIGN_VALUE (sub->value_pos, 4);

  len_pos = sub->value_pos;

  sub->value_pos += 4; /* for the length */

  alignment = element_type_get_alignment (sub->type_str,
                                          sub->type_pos);

  sub->value_pos = _DBUS_ALIGN_VALUE (sub->value_pos, alignment);

  sub->u.array.start_pos = sub->value_pos;
  _dbus_assert ((sub->u.array.start_pos - (len_pos + 4)) < 8); /* only 3 bits in array_len_offset */
  sub->array_len_offset = sub->u.array.start_pos - (len_pos + 4);

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("    type reader %p array start = %d len_offset = %d array len = %d array element type = %s\n",
                 sub,
                 sub->u.array.start_pos,
                 sub->array_len_offset,
                 array_reader_get_array_len (sub),
                 _dbus_type_to_string (first_type_in_signature (sub->type_str,
                                                                sub->type_pos)));
#endif
}

static void
variant_reader_recurse (DBusTypeReader *sub,
                        DBusTypeReader *parent)
{
  int sig_len;

  base_reader_recurse (sub, parent);

  /* Variant is 1 byte sig length (without nul), signature with nul,
   * padding to 8-boundary, then values
   */

  sig_len = _dbus_string_get_byte (sub->value_str, sub->value_pos);

  sub->type_str = sub->value_str;
  sub->type_pos = sub->value_pos + 1;

  sub->value_pos = sub->type_pos + sig_len + 1;

  sub->value_pos = _DBUS_ALIGN_VALUE (sub->value_pos, 8);

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("    type reader %p variant containing '%s'\n",
                 sub,
                 _dbus_string_get_const_data_len (sub->type_str,
                                                  sub->type_pos, 0));
#endif
}

static dbus_bool_t
array_reader_check_finished (const DBusTypeReader *reader)
{
  int end_pos;

  /* return the array element type if elements remain, and
   * TYPE_INVALID otherwise
   */

  end_pos = reader->u.array.start_pos + array_reader_get_array_len (reader);

  _dbus_assert (reader->value_pos <= end_pos);
  _dbus_assert (reader->value_pos >= reader->u.array.start_pos);

  return reader->value_pos == end_pos;
}

/* this is written a little oddly to try and overoptimize */
static void
skip_one_complete_type (const DBusString *type_str,
                        int              *type_pos)
{
  const unsigned char *p;
  const unsigned char *start;

  start = _dbus_string_get_const_data (type_str);
  p = start + *type_pos;

  while (*p == DBUS_TYPE_ARRAY)
    ++p;

  if (*p == DBUS_STRUCT_BEGIN_CHAR)
    {
      int depth;

      depth = 1;

      while (TRUE)
        {
          _dbus_assert (*p != DBUS_TYPE_INVALID);

          ++p;

          _dbus_assert (*p != DBUS_TYPE_INVALID);

          if (*p == DBUS_STRUCT_BEGIN_CHAR)
            depth += 1;
          else if (*p == DBUS_STRUCT_END_CHAR)
            {
              depth -= 1;
              if (depth == 0)
                {
                  ++p;
                  break;
                }
            }
        }
    }
  else
    {
      ++p;
    }

  _dbus_assert (*p != DBUS_STRUCT_END_CHAR);

  *type_pos = (int) (p - start);
}

static int
find_len_of_complete_type (const DBusString *type_str,
                           int               type_pos)
{
  int end;

  end = type_pos;

  skip_one_complete_type (type_str, &end);

  return end - type_pos;
}

static void
base_reader_next (DBusTypeReader *reader,
                  int             current_type)
{
  switch (current_type)
    {
    case DBUS_TYPE_STRUCT:
    case DBUS_TYPE_VARIANT:
      /* Scan forward over the entire container contents */
      {
        DBusTypeReader sub;

        if (reader->klass->types_only && current_type == DBUS_TYPE_VARIANT)
          ;
        else
          {
            /* Recurse into the struct or variant */
            _dbus_type_reader_recurse (reader, &sub);

            /* Skip everything in this subreader */
            while (_dbus_type_reader_next (&sub))
              {
                /* nothing */;
              }
          }
        if (!reader->klass->types_only)
          reader->value_pos = sub.value_pos;

        /* Now we are at the end of this container; for variants, the
         * subreader's type_pos is totally inapplicable (it's in the
         * value string) but we know that we increment by one past the
         * DBUS_TYPE_VARIANT
         */
        if (current_type == DBUS_TYPE_VARIANT)
          reader->type_pos += 1;
        else
          reader->type_pos = sub.type_pos;
      }
      break;

    case DBUS_TYPE_ARRAY:
      {
        if (!reader->klass->types_only)
          _dbus_marshal_skip_array (reader->value_str,
                                    first_type_in_signature (reader->type_str,
                                                             reader->type_pos + 1),
                                    reader->byte_order,
                                    &reader->value_pos);

        skip_one_complete_type (reader->type_str, &reader->type_pos);
      }
      break;

    default:
      if (!reader->klass->types_only)
        _dbus_marshal_skip_basic (reader->value_str,
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

  end_pos = reader->u.array.start_pos + array_reader_get_array_len (reader);

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("  reader %p array next START start_pos = %d end_pos = %d value_pos = %d current_type = %s\n",
                 reader,
                 reader->u.array.start_pos,
                 end_pos, reader->value_pos,
                 _dbus_type_to_string (current_type));
#endif

  _dbus_assert (reader->value_pos < end_pos);
  _dbus_assert (reader->value_pos >= reader->u.array.start_pos);

  switch (first_type_in_signature (reader->type_str,
                                   reader->type_pos))
    {
    case DBUS_TYPE_STRUCT:
    case DBUS_TYPE_VARIANT:
      {
        DBusTypeReader sub;

        /* Recurse into the struct or variant */
        _dbus_type_reader_recurse (reader, &sub);

        /* Skip everything in this element */
        while (_dbus_type_reader_next (&sub))
          {
            /* nothing */;
          }

        /* Now we are at the end of this element */
        reader->value_pos = sub.value_pos;
      }
      break;

    case DBUS_TYPE_ARRAY:
      {
        _dbus_marshal_skip_array (reader->value_str,
                                  first_type_in_signature (reader->type_str,
                                                           reader->type_pos + 1),
                                  reader->byte_order,
                                  &reader->value_pos);
      }
      break;

    default:
      {
        _dbus_marshal_skip_basic (reader->value_str,
                                  current_type, reader->byte_order,
                                  &reader->value_pos);
      }
      break;
    }

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("  reader %p array next END start_pos = %d end_pos = %d value_pos = %d current_type = %s\n",
                 reader,
                 reader->u.array.start_pos,
                 end_pos, reader->value_pos,
                 _dbus_type_to_string (current_type));
#endif

  _dbus_assert (reader->value_pos <= end_pos);

  if (reader->value_pos == end_pos)
    {
      skip_one_complete_type (reader->type_str,
                              &reader->type_pos);
    }
}

static void
array_init_from_mark (DBusTypeReader     *reader,
                      const DBusTypeMark *mark)
{
  /* Fill in the array-specific fields from the mark. The general
   * fields are already filled in.
   */
  reader->u.array.start_pos = mark->array_start_pos;
  reader->array_len_offset = mark->array_len_offset;
}

static const DBusTypeReaderClass body_reader_class = {
  "body", 0,
  FALSE,
  NULL, /* body is always toplevel, so doesn't get recursed into */
  NULL,
  base_reader_next,
  NULL
};

static const DBusTypeReaderClass body_types_only_reader_class = {
  "body types", 1,
  TRUE,
  NULL, /* body is always toplevel, so doesn't get recursed into */
  NULL,
  base_reader_next,
  NULL
};

static const DBusTypeReaderClass struct_reader_class = {
  "struct", 2,
  FALSE,
  struct_reader_recurse,
  NULL,
  struct_reader_next,
  NULL
};

static const DBusTypeReaderClass struct_types_only_reader_class = {
  "struct types", 3,
  TRUE,
  struct_types_only_reader_recurse,
  NULL,
  struct_reader_next,
  NULL
};

static const DBusTypeReaderClass array_reader_class = {
  "array", 4,
  FALSE,
  array_reader_recurse,
  array_reader_check_finished,
  array_reader_next,
  array_init_from_mark
};

static const DBusTypeReaderClass array_types_only_reader_class = {
  "array types", 5,
  TRUE,
  array_types_only_reader_recurse,
  NULL,
  array_types_only_reader_next,
  NULL
};

static const DBusTypeReaderClass variant_reader_class = {
  "variant", 6,
  FALSE,
  variant_reader_recurse,
  NULL,
  base_reader_next,
  NULL
};

static const DBusTypeReaderClass const *
all_reader_classes[] = {
  &body_reader_class,
  &body_types_only_reader_class,
  &struct_reader_class,
  &struct_types_only_reader_class,
  &array_reader_class,
  &array_types_only_reader_class,
  &variant_reader_class
};

/**
 * Initializes a type reader.
 *
 * @param reader the reader
 * @param byte_order the byte order of the block to read
 * @param type_str the signature of the block to read
 * @param type_pos location of signature
 * @param value_str the string containing values block
 * @param value_pos start of values block
 */
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

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("  type reader %p init type_pos = %d value_pos = %d remaining sig '%s'\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));
#endif
}

/**
 * Initializes a type reader that's been compressed into a
 * DBusTypeMark.  The args have to be the same as those passed in to
 * create the original #DBusTypeReader.
 *
 * @param reader the reader
 * @param byte_order the byte order of the value block
 * @param type_str string containing the type signature
 * @param value_str string containing the values block
 * @param mark the mark to decompress from
 */
void
_dbus_type_reader_init_from_mark (DBusTypeReader     *reader,
                                  int                 byte_order,
                                  const DBusString   *type_str,
                                  const DBusString   *value_str,
                                  const DBusTypeMark *mark)
{
  reader->klass = all_reader_classes[mark->container_type];

  reader_init (reader, byte_order,
               mark->type_pos_in_value_str ? value_str : type_str,
               mark->type_pos,
               value_str, mark->value_pos);

  if (reader->klass->init_from_mark)
    (* reader->klass->init_from_mark) (reader, mark);

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("  type reader %p init from mark type_pos = %d value_pos = %d remaining sig '%s'\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));
#endif
}

/**
 * Like _dbus_type_reader_init() but the iteration is over the
 * signature, not over values.
 *
 * @param reader the reader
 * @param type_str the signature string
 * @param type_pos location in the signature string
 */
void
_dbus_type_reader_init_types_only (DBusTypeReader    *reader,
                                   const DBusString  *type_str,
                                   int                type_pos)
{
  reader->klass = &body_types_only_reader_class;

  reader_init (reader, DBUS_COMPILER_BYTE_ORDER /* irrelevant */,
               type_str, type_pos, NULL, _DBUS_INT_MAX /* crashes if we screw up */);

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("  type reader %p init types only type_pos = %d remaining sig '%s'\n",
                 reader, reader->type_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));
#endif
}

/**
 * Like _dbus_type_reader_init_from_mark() but only iterates over
 * the signature, not the values.
 *
 * @param reader the reader
 * @param type_str the signature string
 * @param mark the mark to decompress from
 */
void
_dbus_type_reader_init_types_only_from_mark (DBusTypeReader     *reader,
                                             const DBusString   *type_str,
                                             const DBusTypeMark *mark)
{
  reader->klass = all_reader_classes[mark->container_type];
  _dbus_assert (reader->klass->types_only);
  _dbus_assert (!mark->type_pos_in_value_str);

  reader_init (reader, DBUS_COMPILER_BYTE_ORDER, /* irrelevant */
               type_str, mark->type_pos,
               NULL, _DBUS_INT_MAX /* crashes if we screw up */);

  if (reader->klass->init_from_mark)
    (* reader->klass->init_from_mark) (reader, mark);

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("  type reader %p init types only from mark type_pos = %d remaining sig '%s'\n",
                 reader, reader->type_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));
#endif
}

/**
 * Compresses a type reader into a #DBusTypeMark, useful for example
 * if you want to cache a bunch of positions in a block of values.
 *
 * @param reader the reader
 * @param mark the mark to init
 */
void
_dbus_type_reader_save_mark (const DBusTypeReader *reader,
                             DBusTypeMark         *mark)
{
  mark->type_pos_in_value_str = (reader->type_str == reader->value_str);
  mark->container_type = reader->klass->id;
  _dbus_assert (all_reader_classes[reader->klass->id] == reader->klass);

  mark->type_pos = reader->type_pos;
  mark->value_pos = reader->value_pos;

  /* these are just junk if the reader isn't really an array of course */
  mark->array_len_offset = reader->array_len_offset;
  mark->array_start_pos = reader->u.array.start_pos;
}

/**
 * Gets the type of the value the reader is currently pointing to;
 * or for a types-only reader gets the type it's currently pointing to.
 * If the reader is at the end of a block or end of a container such
 * as an array, returns #DBUS_TYPE_INVALID.
 *
 * @param reader the reader
 */
int
_dbus_type_reader_get_current_type (const DBusTypeReader *reader)
{
  int t;

  if (reader->finished ||
      (reader->klass->check_finished &&
       (* reader->klass->check_finished) (reader)))
    t = DBUS_TYPE_INVALID;
  else
    t = first_type_in_signature (reader->type_str,
                                 reader->type_pos);

  _dbus_assert (t != DBUS_STRUCT_END_CHAR);
  _dbus_assert (t != DBUS_STRUCT_BEGIN_CHAR);

#if 0
  _dbus_verbose ("  type reader %p current type_pos = %d type = %s\n",
                 reader, reader->type_pos,
                 _dbus_type_to_string (t));
#endif

  return t;
}

/**
 * Gets the type of an element of the array the reader is currently
 * pointing to. It's an error to call this if
 * _dbus_type_reader_get_current_type() doesn't return #DBUS_TYPE_ARRAY
 * for this reader.
 *
 * @param reader the reader
 */
int
_dbus_type_reader_get_element_type (const DBusTypeReader  *reader)
{
  int element_type;

  _dbus_assert (_dbus_type_reader_get_current_type (reader) == DBUS_TYPE_ARRAY);

  element_type = first_type_in_signature (reader->type_str,
                                          reader->type_pos + 1);

  return element_type;
}

/**
 * Gets the current position in the value block
 * @param reader the reader
 */
int
_dbus_type_reader_get_value_pos (const DBusTypeReader  *reader)
{
  return reader->value_pos;
}

/**
 * Checks whether an array has any elements.
 *
 * @param reader the reader
 */
static dbus_bool_t
_dbus_type_reader_array_is_empty (const DBusTypeReader *reader)
{
  return array_reader_get_array_len (reader) == 0;
}

/**
 * Get the address of the marshaled value in the data being read.  The
 * address may not be aligned; you have to align it to the type of the
 * value you want to read. Most of the demarshal routines do this for
 * you.
 *
 * @param reader the reader
 * @param value_location the address of the marshaled value
 */
void
_dbus_type_reader_read_raw (const DBusTypeReader  *reader,
                            const unsigned char  **value_location)
{
  _dbus_assert (!reader->klass->types_only);

  *value_location = _dbus_string_get_const_data_len (reader->value_str,
                                                     reader->value_pos,
                                                     0);
}

/**
 * Reads a basic-typed value, as with _dbus_marshal_read_basic().
 *
 * @param reader the reader
 * @param value the address of the value
 */
void
_dbus_type_reader_read_basic (const DBusTypeReader    *reader,
                              void                    *value)
{
  int t;

  _dbus_assert (!reader->klass->types_only);

  t = _dbus_type_reader_get_current_type (reader);

  _dbus_marshal_read_basic (reader->value_str,
                            reader->value_pos,
                            t, value,
                            reader->byte_order,
                            NULL);


#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("  type reader %p read basic type_pos = %d value_pos = %d remaining sig '%s'\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));
#endif
}

/**
 * Reads a block of fixed-length basic values, from the current point
 * in an array to the end of the array.  Does not work for arrays of
 * string or container types.
 *
 * This function returns the array in-place; it does not make a copy,
 * and it does not swap the bytes.
 *
 * If you ask for #DBUS_TYPE_DOUBLE you will get a "const double*" back
 * and the "value" argument should be a "const double**" and so on.
 *
 * @param reader the reader to read from
 * @param value place to return the array values
 * @param n_elements place to return number of array elements
 */
void
_dbus_type_reader_read_fixed_multi (const DBusTypeReader  *reader,
                                    void                  *value,
                                    int                   *n_elements)
{
  int element_type;
  int end_pos;
  int remaining_len;
  int alignment;
  int total_len;

  _dbus_assert (!reader->klass->types_only);
  _dbus_assert (reader->klass == &array_reader_class);

  element_type = first_type_in_signature (reader->type_str,
                                          reader->type_pos);

  _dbus_assert (element_type != DBUS_TYPE_INVALID); /* why we don't use get_current_type() */
  _dbus_assert (_dbus_type_is_fixed (element_type));

  alignment = _dbus_type_get_alignment (element_type);

  _dbus_assert (reader->value_pos >= reader->u.array.start_pos);

  total_len = array_reader_get_array_len (reader);
  end_pos = reader->u.array.start_pos + total_len;
  remaining_len = end_pos - reader->value_pos;

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("end_pos %d total_len %d remaining_len %d value_pos %d\n",
                 end_pos, total_len, remaining_len, reader->value_pos);
#endif

  _dbus_assert (remaining_len <= total_len);

  if (remaining_len == 0)
    *(const DBusBasicValue**) value = NULL;
  else
    *(const DBusBasicValue**) value =
      (void*) _dbus_string_get_const_data_len (reader->value_str,
                                               reader->value_pos,
                                               remaining_len);

  *n_elements = remaining_len / alignment;
  _dbus_assert ((remaining_len % alignment) == 0);

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("  type reader %p read fixed array type_pos = %d value_pos = %d remaining sig '%s'\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));
#endif
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

  _dbus_assert (sub->klass == all_reader_classes[sub->klass->id]);

  (* sub->klass->recurse) (sub, reader);

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("  type reader %p RECURSED type_pos = %d value_pos = %d remaining sig '%s'\n",
                 sub, sub->type_pos, sub->value_pos,
                 _dbus_string_get_const_data_len (sub->type_str, sub->type_pos, 0));
#endif
}

/**
 * Skip to the next value on this "level". e.g. the next field in a
 * struct, the next value in an array. Returns FALSE at the end of the
 * current container.
 *
 * @param reader the reader
 * @returns FALSE if nothing more to read at or below this level
 */
dbus_bool_t
_dbus_type_reader_next (DBusTypeReader *reader)
{
  int t;

  t = _dbus_type_reader_get_current_type (reader);

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("  type reader %p START next() { type_pos = %d value_pos = %d remaining sig '%s' current_type = %s\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0),
                 _dbus_type_to_string (t));
#endif

  if (t == DBUS_TYPE_INVALID)
    return FALSE;

  (* reader->klass->next) (reader, t);

#if RECURSIVE_MARSHAL_READ_TRACE
  _dbus_verbose ("  type reader %p END next() type_pos = %d value_pos = %d remaining sig '%s' current_type = %s\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0),
                 _dbus_type_to_string (_dbus_type_reader_get_current_type (reader)));
#endif

  return _dbus_type_reader_get_current_type (reader) != DBUS_TYPE_INVALID;
}

/**
 * Check whether there's another value on this "level". e.g. the next
 * field in a struct, the next value in an array. Returns FALSE at the
 * end of the current container.
 *
 * You probably don't want to use this; it makes for an awkward for/while
 * loop. A nicer one is "while ((current_type = get_current_type()) != INVALID)"
 *
 * @param reader the reader
 * @returns FALSE if nothing more to read at or below this level
 */
dbus_bool_t
_dbus_type_reader_has_next (const DBusTypeReader *reader)
{
  /* Not efficient but works for now. */
  DBusTypeReader copy;

  copy = *reader;
  return _dbus_type_reader_next (&copy);
}

/**
 * Gets the string and range of said string containing the signature
 * of the current value. Essentially a more complete version of
 * _dbus_type_reader_get_current_type() (returns the full type
 * rather than only the outside of the onion).
 *
 * Note though that the first byte in a struct signature is
 * #DBUS_STRUCT_BEGIN_CHAR while the current type will be
 * #DBUS_TYPE_STRUCT so it isn't true that the first byte of the
 * signature is always the same as the current type. Another
 * difference is that this function will still return a signature when
 * inside an empty array; say you recurse into empty array of int32,
 * the signature is "i" but the current type will always be
 * #DBUS_TYPE_INVALID since there are no elements to be currently
 * pointing to.
 *
 * @param reader the reader
 * @param str_p place to return the string with the type in it
 * @param start_p place to return start of the type
 * @param len_p place to return the length of the type
 */
void
_dbus_type_reader_get_signature (const DBusTypeReader  *reader,
                                 const DBusString     **str_p,
                                 int                   *start_p,
                                 int                   *len_p)
{
  *str_p = reader->type_str;
  *start_p = reader->type_pos;
  *len_p = find_len_of_complete_type (reader->type_str, reader->type_pos);
}

typedef struct
{
  DBusString replacement;
  int padding;
} ReplacementBlock;

static dbus_bool_t
replacement_block_init (ReplacementBlock *block,
                        DBusTypeReader   *reader)
{
  if (!_dbus_string_init (&block->replacement))
    return FALSE;

  /* % 8 is the padding to have the same align properties in
   * our replacement string as we do at the position being replaced
   */
  block->padding = reader->value_pos % 8;

  if (!_dbus_string_lengthen (&block->replacement, block->padding))
    goto oom;

  return TRUE;

 oom:
  _dbus_string_free (&block->replacement);
  return FALSE;
}

static dbus_bool_t
replacement_block_replace (ReplacementBlock     *block,
                           DBusTypeReader       *reader,
                           const DBusTypeReader *realign_root)
{
  DBusTypeWriter writer;
  DBusTypeReader realign_reader;
  DBusList *fixups;
  int orig_len;

  _dbus_assert (realign_root != NULL);

  orig_len = _dbus_string_get_length (&block->replacement);

  realign_reader = *realign_root;

#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("INITIALIZING replacement block writer %p at value_pos %d\n",
                 &writer, _dbus_string_get_length (&block->replacement));
#endif
  _dbus_type_writer_init_values_only (&writer,
                                      realign_reader.byte_order,
                                      realign_reader.type_str,
                                      realign_reader.type_pos,
                                      &block->replacement,
                                      _dbus_string_get_length (&block->replacement));

  _dbus_assert (realign_reader.value_pos <= reader->value_pos);

#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("COPYING from reader at value_pos %d to writer %p starting after value_pos %d\n",
                 realign_reader.value_pos, &writer, reader->value_pos);
#endif
  fixups = NULL;
  if (!_dbus_type_writer_write_reader_partial (&writer,
                                               &realign_reader,
                                               reader,
                                               block->padding,
                                               _dbus_string_get_length (&block->replacement) - block->padding,
                                               &fixups))
    goto oom;

#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("REPLACEMENT at padding %d len %d\n", block->padding,
                 _dbus_string_get_length (&block->replacement) - block->padding);
  _dbus_verbose_bytes_of_string (&block->replacement, block->padding,
                                 _dbus_string_get_length (&block->replacement) - block->padding);
  _dbus_verbose ("TO BE REPLACED at value_pos = %d (align pad %d) len %d realign_reader.value_pos %d\n",
                 reader->value_pos, reader->value_pos % 8,
                 realign_reader.value_pos - reader->value_pos,
                 realign_reader.value_pos);
  _dbus_verbose_bytes_of_string (reader->value_str,
                                 reader->value_pos,
                                 realign_reader.value_pos - reader->value_pos);
#endif

  /* Move the replacement into position
   * (realign_reader should now be at the end of the block to be replaced)
   */
  if (!_dbus_string_replace_len (&block->replacement, block->padding,
                                 _dbus_string_get_length (&block->replacement) - block->padding,
                                 (DBusString*) reader->value_str,
                                 reader->value_pos,
                                 realign_reader.value_pos - reader->value_pos))
    goto oom;

  /* Process our fixups now that we can't have an OOM error */
  apply_and_free_fixups (&fixups, reader);

  return TRUE;

 oom:
  _dbus_string_set_length (&block->replacement, orig_len);
  free_fixups (&fixups);
  return FALSE;
}

static void
replacement_block_free (ReplacementBlock *block)
{
  _dbus_string_free (&block->replacement);
}

/* In the variable-length case, we have to fix alignment after we insert.
 * The strategy is as follows:
 *
 *  - pad a new string to have the same alignment as the
 *    start of the current basic value
 *  - write the new basic value
 *  - copy from the original reader to the new string,
 *    which will fix the alignment of types following
 *    the new value
 *    - this copy has to start at realign_root,
 *      but not really write anything until it
 *      passes the value being set
 *    - as an optimization, we can stop copying
 *      when the source and dest values are both
 *      on an 8-boundary, since we know all following
 *      padding and alignment will be identical
 *  - copy the new string back to the original
 *    string, replacing the relevant part of the
 *    original string
 *  - now any arrays in the original string that
 *    contained the replaced string may have the
 *    wrong length; so we have to fix that
 */
static dbus_bool_t
reader_set_basic_variable_length (DBusTypeReader       *reader,
                                  int                   current_type,
                                  const void           *value,
                                  const DBusTypeReader *realign_root)
{
  dbus_bool_t retval;
  ReplacementBlock block;
  DBusTypeWriter writer;

  _dbus_assert (realign_root != NULL);

  retval = FALSE;

  if (!replacement_block_init (&block, reader))
    return FALSE;

  /* Write the new basic value */
#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("INITIALIZING writer %p to write basic value at value_pos %d of replacement string\n",
                 &writer, _dbus_string_get_length (&block.replacement));
#endif
  _dbus_type_writer_init_values_only (&writer,
                                      reader->byte_order,
                                      reader->type_str,
                                      reader->type_pos,
                                      &block.replacement,
                                      _dbus_string_get_length (&block.replacement));
#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("WRITING basic value to writer %p (replacement string)\n", &writer);
#endif
  if (!_dbus_type_writer_write_basic (&writer, current_type, value))
    goto out;

  if (!replacement_block_replace (&block,
                                  reader,
                                  realign_root))
    goto out;

  retval = TRUE;

 out:
  replacement_block_free (&block);
  return retval;
}

static void
reader_set_basic_fixed_length (DBusTypeReader *reader,
                               int             current_type,
                               const void     *value)
{
  _dbus_marshal_set_basic ((DBusString*) reader->value_str,
                           reader->value_pos,
                           current_type,
                           value,
                           reader->byte_order,
                           NULL, NULL);
}

/**
 * Sets a new value for the basic type value pointed to by the reader,
 * leaving the reader valid to continue reading. Any other readers
 * will be invalidated if you set a variable-length type such as a
 * string.
 *
 * The provided realign_root is the reader to start from when
 * realigning the data that follows the newly-set value. The reader
 * parameter must point to a value below the realign_root parameter.
 * If the type being set is fixed-length, then realign_root may be
 * #NULL. Only values reachable from realign_root will be realigned,
 * so if your string contains other values you will need to deal with
 * those somehow yourself. It is OK if realign_root is the same
 * reader as the reader parameter, though if you aren't setting the
 * root it may not be such a good idea.
 *
 * @todo DBusTypeReader currently takes "const" versions of the type
 * and value strings, and this function modifies those strings by
 * casting away the const, which is of course bad if we want to get
 * picky. (To be truly clean you'd have an object which contained the
 * type and value strings and set_basic would be a method on that
 * object... this would also make DBusTypeReader the same thing as
 * DBusTypeMark. But since DBusMessage is effectively that object for
 * D-BUS it doesn't seem worth creating some random object.)
 *
 * @todo optimize this by only rewriting until the old and new values
 * are at the same alignment. Frequently this should result in only
 * replacing the value that's immediately at hand.
 *
 * @param reader reader indicating where to set a new value
 * @param value address of the value to set
 * @param realign_root realign from here
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
_dbus_type_reader_set_basic (DBusTypeReader       *reader,
                             const void           *value,
                             const DBusTypeReader *realign_root)
{
  int current_type;

  _dbus_assert (!reader->klass->types_only);
  _dbus_assert (reader->value_str == realign_root->value_str);
  _dbus_assert (reader->value_pos >= realign_root->value_pos);

  current_type = _dbus_type_reader_get_current_type (reader);

#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("  SET BASIC type reader %p type_pos = %d value_pos = %d remaining sig '%s' realign_root = %p with value_pos %d current_type = %s\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0),
                 realign_root,
                 realign_root ? realign_root->value_pos : -1,
                 _dbus_type_to_string (current_type));
  _dbus_verbose_bytes_of_string (realign_root->value_str, realign_root->value_pos,
                                 _dbus_string_get_length (realign_root->value_str) -
                                 realign_root->value_pos);
#endif

  _dbus_assert (_dbus_type_is_basic (current_type));

  if (_dbus_type_is_fixed (current_type))
    {
      reader_set_basic_fixed_length (reader, current_type, value);
      return TRUE;
    }
  else
    {
      _dbus_assert (realign_root != NULL);
      return reader_set_basic_variable_length (reader, current_type,
                                               value, realign_root);
    }
}

/**
 * Recursively deletes any value pointed to by the reader, leaving the
 * reader valid to continue reading. Any other readers will be
 * invalidated.
 *
 * The provided realign_root is the reader to start from when
 * realigning the data that follows the newly-set value.
 * See _dbus_type_reader_set_basic() for more details on the
 * realign_root paramter.
 *
 * @todo for now this does not delete the typecodes associated with
 * the value, so this function should only be used for array elements.
 *
 * @param reader reader indicating where to delete a value
 * @param realign_root realign from here
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
_dbus_type_reader_delete (DBusTypeReader        *reader,
                          const DBusTypeReader  *realign_root)
{
  dbus_bool_t retval;
  ReplacementBlock block;

  _dbus_assert (realign_root != NULL);
  _dbus_assert (reader->klass == &array_reader_class);

  retval = FALSE;

  if (!replacement_block_init (&block, reader))
    return FALSE;

  if (!replacement_block_replace (&block,
                                  reader,
                                  realign_root))
    goto out;

  retval = TRUE;

 out:
  replacement_block_free (&block);
  return retval;
}

/**
 * Compares two readers, which must be iterating over the same value data.
 * Returns #TRUE if the first parameter is further along than the second parameter.
 *
 * @param lhs left-hand-side (first) parameter
 * @param rhs left-hand-side (first) parameter
 * @returns whether lhs is greater than rhs
 */
dbus_bool_t
_dbus_type_reader_greater_than (const DBusTypeReader  *lhs,
                                const DBusTypeReader  *rhs)
{
  _dbus_assert (lhs->value_str == rhs->value_str);

  return lhs->value_pos > rhs->value_pos;
}

/*
 *
 *
 *         DBusTypeWriter
 *
 *
 *
 */

/**
 * Initialize a write iterator, which is used to write out values in
 * serialized D-BUS format.
 *
 * The type_pos passed in is expected to be inside an already-valid,
 * though potentially empty, type signature. This means that the byte
 * after type_pos must be either #DBUS_TYPE_INVALID (aka nul) or some
 * other valid type. #DBusTypeWriter won't enforce that the signature
 * is already valid (you can append the nul byte at the end if you
 * like), but just be aware that you need the nul byte eventually and
 * #DBusTypeWriter isn't going to write it for you.
 *
 * @param writer the writer to init
 * @param byte_order the byte order to marshal into
 * @param type_str the string to write typecodes into
 * @param type_pos where to insert typecodes
 * @param value_str the string to write values into
 * @param value_pos where to insert values
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
  writer->enabled = TRUE;

#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("writer %p init remaining sig '%s'\n", writer,
                 writer->type_str ?
                 _dbus_string_get_const_data_len (writer->type_str, writer->type_pos, 0) :
                 "unknown");
#endif
}

/**
 * Initialize a write iterator, with the signature to be provided
 * later.
 *
 * @param writer the writer to init
 * @param byte_order the byte order to marshal into
 * @param value_str the string to write values into
 * @param value_pos where to insert values
 *
 */
void
_dbus_type_writer_init_types_delayed (DBusTypeWriter *writer,
                                      int             byte_order,
                                      DBusString     *value_str,
                                      int             value_pos)
{
  _dbus_type_writer_init (writer, byte_order,
                          NULL, 0, value_str, value_pos);
}

/**
 * Adds type string to the writer, if it had none.
 *
 * @param writer the writer to init
 * @param type_str type string to add
 * @param type_pos type position
 *
 */
void
_dbus_type_writer_add_types (DBusTypeWriter *writer,
                             DBusString     *type_str,
                             int             type_pos)
{
  if (writer->type_str == NULL) /* keeps us from using this as setter */
    {
      writer->type_str = type_str;
      writer->type_pos = type_pos;
    }
}

/**
 * Removes type string from the writer.
 *
 * @param writer the writer to remove from
 */
void
_dbus_type_writer_remove_types (DBusTypeWriter *writer)
{
  writer->type_str = NULL;
  writer->type_pos = -1;
}

/**
 * Like _dbus_type_writer_init(), except the type string
 * passed in should correspond to an existing signature that
 * matches what you're going to write out. The writer will
 * check what you write vs. this existing signature.
 *
 * @param writer the writer to init
 * @param byte_order the byte order to marshal into
 * @param type_str the string with signature
 * @param type_pos start of signature
 * @param value_str the string to write values into
 * @param value_pos where to insert values
 *
 */
void
_dbus_type_writer_init_values_only (DBusTypeWriter   *writer,
                                    int               byte_order,
                                    const DBusString *type_str,
                                    int               type_pos,
                                    DBusString       *value_str,
                                    int               value_pos)
{
  _dbus_type_writer_init (writer, byte_order,
                          (DBusString*)type_str, type_pos,
                          value_str, value_pos);

  writer->type_pos_is_expectation = TRUE;
}

static dbus_bool_t
_dbus_type_writer_write_basic_no_typecode (DBusTypeWriter *writer,
                                           int             type,
                                           const void     *value)
{
  if (writer->enabled)
    return _dbus_marshal_write_basic (writer->value_str,
                                      writer->value_pos,
                                      type,
                                      value,
                                      writer->byte_order,
                                      &writer->value_pos);
  else
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

  sub->enabled = writer->enabled;

#ifndef DBUS_DISABLE_CHECKS
  if (writer->type_pos_is_expectation && writer->type_str)
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

#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("  type writer %p recurse parent %s type_pos = %d value_pos = %d is_expectation = %d remaining sig '%s' enabled = %d\n",
                 writer,
                 _dbus_type_to_string (writer->container_type),
                 writer->type_pos, writer->value_pos, writer->type_pos_is_expectation,
                 writer->type_str ?
                 _dbus_string_get_const_data_len (writer->type_str, writer->type_pos, 0) :
                 "unknown",
                 writer->enabled);
  _dbus_verbose ("  type writer %p recurse sub %s   type_pos = %d value_pos = %d is_expectation = %d enabled = %d\n",
                 sub,
                 _dbus_type_to_string (sub->container_type),
                 sub->type_pos, sub->value_pos,
                 sub->type_pos_is_expectation,
                 sub->enabled);
#endif
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
#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("  type writer %p write_or_verify start type_pos = %d remaining sig '%s' enabled = %d\n",
                 writer, writer->type_pos,
                 writer->type_str ?
                 _dbus_string_get_const_data_len (writer->type_str, writer->type_pos, 0) :
                 "unknown",
                 writer->enabled);
#endif

  if (writer->type_str == NULL)
    return TRUE;

  if (writer->type_pos_is_expectation)
    {
#ifndef DBUS_DISABLE_CHECKS
      {
        int expected;

        expected = _dbus_string_get_byte (writer->type_str, writer->type_pos);

        if (expected != typecode)
          {
            _dbus_warn ("Array or variant type requires that type %s be written, but %s was written\n",
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

#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("  type writer %p write_or_verify end type_pos = %d remaining sig '%s'\n",
                 writer, writer->type_pos,
                 _dbus_string_get_const_data_len (writer->type_str, writer->type_pos, 0));
#endif

  return TRUE;
}

static dbus_bool_t
writer_recurse_struct (DBusTypeWriter   *writer,
                       const DBusString *contained_type,
                       int               contained_type_start,
                       int               contained_type_len,
                       DBusTypeWriter   *sub)
{
  /* FIXME right now contained_type is ignored; we could probably
   * almost trivially fix the code so if it's present we
   * write it out and then set type_pos_is_expectation
   */

  /* Ensure that we'll be able to add alignment padding and the typecode */
  if (writer->enabled)
    {
      if (!_dbus_string_alloc_space (sub->value_str, 8))
        return FALSE;
    }

  if (!write_or_verify_typecode (sub, DBUS_STRUCT_BEGIN_CHAR))
    _dbus_assert_not_reached ("failed to insert struct typecode after prealloc");

  if (writer->enabled)
    {
      if (!_dbus_string_insert_bytes (sub->value_str,
                                      sub->value_pos,
                                      _DBUS_ALIGN_VALUE (sub->value_pos, 8) - sub->value_pos,
                                      '\0'))
        _dbus_assert_not_reached ("should not have failed to insert alignment padding for struct");
      sub->value_pos = _DBUS_ALIGN_VALUE (sub->value_pos, 8);
    }

  return TRUE;
}


static dbus_bool_t
writer_recurse_array (DBusTypeWriter   *writer,
                      const DBusString *contained_type,
                      int               contained_type_start,
                      int               contained_type_len,
                      DBusTypeWriter   *sub,
                      dbus_bool_t       is_array_append)
{
  dbus_uint32_t value = 0;
  int alignment;
  int aligned;

#ifndef DBUS_DISABLE_CHECKS
  if (writer->container_type == DBUS_TYPE_ARRAY &&
      writer->type_str)
    {
      if (!_dbus_string_equal_substring (contained_type,
                                         contained_type_start,
                                         contained_type_len,
                                         writer->type_str,
                                         writer->u.array.element_type_pos + 1))
        {
          _dbus_warn ("Writing an array of '%s' but this is incompatible with the expected type of elements in the parent array\n",
                      _dbus_string_get_const_data_len (contained_type,
                                                       contained_type_start,
                                                       contained_type_len));
          _dbus_assert_not_reached ("incompatible type for child array");
        }
    }
#endif /* DBUS_DISABLE_CHECKS */

  if (writer->enabled && !is_array_append)
    {
      /* 3 pad + 4 bytes for the array length, and 4 bytes possible padding
       * before array values
       */
      if (!_dbus_string_alloc_space (sub->value_str, 3 + 4 + 4))
        return FALSE;
    }

  if (writer->type_str != NULL)
    {
      sub->type_pos += 1; /* move to point to the element type, since type_pos
                           * should be the expected type for further writes
                           */
      sub->u.array.element_type_pos = sub->type_pos;
    }

  if (!writer->type_pos_is_expectation)
    {
      /* sub is a toplevel/outermost array so we need to write the type data */

      /* alloc space for array typecode, element signature */
      if (!_dbus_string_alloc_space (writer->type_str, 1 + contained_type_len))
        return FALSE;

      if (!_dbus_string_insert_byte (writer->type_str,
                                     writer->type_pos,
                                     DBUS_TYPE_ARRAY))
        _dbus_assert_not_reached ("failed to insert array typecode after prealloc");

      if (!_dbus_string_copy_len (contained_type,
                                  contained_type_start, contained_type_len,
                                  sub->type_str,
                                  sub->u.array.element_type_pos))
        _dbus_assert_not_reached ("should not have failed to insert array element typecodes");
    }

  if (writer->type_str != NULL)
    {
      /* If the parent is an array, we hold type_pos pointing at the array element type;
       * otherwise advance it to reflect the array value we just recursed into
       */
      if (writer->container_type != DBUS_TYPE_ARRAY)
        writer->type_pos += 1 + contained_type_len;
      else
        _dbus_assert (writer->type_pos_is_expectation); /* because it's an array */
    }

  if (writer->enabled)
    {
      /* Write (or jump over, if is_array_append) the length */
      sub->u.array.len_pos = _DBUS_ALIGN_VALUE (sub->value_pos, 4);

      if (is_array_append)
        {
          sub->value_pos += 4;
        }
      else
        {
          if (!_dbus_type_writer_write_basic_no_typecode (sub, DBUS_TYPE_UINT32,
                                                          &value))
            _dbus_assert_not_reached ("should not have failed to insert array len");
        }

      _dbus_assert (sub->u.array.len_pos == sub->value_pos - 4);

      /* Write alignment padding for array elements
       * Note that we write the padding *even for empty arrays*
       * to avoid wonky special cases
       */
      alignment = element_type_get_alignment (contained_type, contained_type_start);

      aligned = _DBUS_ALIGN_VALUE (sub->value_pos, alignment);
      if (aligned != sub->value_pos)
        {
          if (!is_array_append)
            {
              if (!_dbus_string_insert_bytes (sub->value_str,
                                              sub->value_pos,
                                              aligned - sub->value_pos,
                                              '\0'))
                _dbus_assert_not_reached ("should not have failed to insert alignment padding");
            }

          sub->value_pos = aligned;
        }

      sub->u.array.start_pos = sub->value_pos;

      if (is_array_append)
        {
          dbus_uint32_t len;

          _dbus_assert (_DBUS_ALIGN_VALUE (sub->u.array.len_pos, 4) ==
                        (unsigned) sub->u.array.len_pos);
          len = _dbus_unpack_uint32 (sub->byte_order,
                                     _dbus_string_get_const_data_len (sub->value_str,
                                                                      sub->u.array.len_pos,
                                                                      4));

          sub->value_pos += len;
        }
    }
  else
    {
      /* not enabled, so we won't write the len_pos; set it to -1 to so indicate */
      sub->u.array.len_pos = -1;
      sub->u.array.start_pos = sub->value_pos;
    }

  _dbus_assert (sub->u.array.len_pos < sub->u.array.start_pos);
  _dbus_assert (is_array_append || sub->u.array.start_pos == sub->value_pos);

#if RECURSIVE_MARSHAL_WRITE_TRACE
      _dbus_verbose ("  type writer %p recurse array done remaining sig '%s' array start_pos = %d len_pos = %d value_pos = %d\n", sub,
                     sub->type_str ?
                     _dbus_string_get_const_data_len (sub->type_str, sub->type_pos, 0) :
                     "unknown",
                     sub->u.array.start_pos, sub->u.array.len_pos, sub->value_pos);
#endif

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
static dbus_bool_t
writer_recurse_variant (DBusTypeWriter   *writer,
                        const DBusString *contained_type,
                        int               contained_type_start,
                        int               contained_type_len,
                        DBusTypeWriter   *sub)
{
  if (writer->enabled)
    {
      /* Allocate space for the worst case, which is 1 byte sig
       * length, nul byte at end of sig, and 7 bytes padding to
       * 8-boundary.
       */
      if (!_dbus_string_alloc_space (sub->value_str, contained_type_len + 9))
        return FALSE;
    }

  /* write VARIANT typecode to the parent's type string */
  if (!write_or_verify_typecode (writer, DBUS_TYPE_VARIANT))
    return FALSE;

  /* If not enabled, mark that we have no type_str anymore ... */

  if (!writer->enabled)
    {
      sub->type_str = NULL;
      sub->type_pos = -1;

      return TRUE;
    }

  /* If we're enabled then continue ... */

  if (!_dbus_string_insert_byte (sub->value_str,
                                 sub->value_pos,
                                 contained_type_len))
    _dbus_assert_not_reached ("should not have failed to insert variant type sig len");

  sub->value_pos += 1;

  /* Here we switch over to the expected type sig we're about to write */
  sub->type_str = sub->value_str;
  sub->type_pos = sub->value_pos;

  if (!_dbus_string_copy_len (contained_type, contained_type_start, contained_type_len,
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

static dbus_bool_t
_dbus_type_writer_recurse_contained_len (DBusTypeWriter   *writer,
                                         int               container_type,
                                         const DBusString *contained_type,
                                         int               contained_type_start,
                                         int               contained_type_len,
                                         DBusTypeWriter   *sub,
                                         dbus_bool_t       is_array_append)
{
  writer_recurse_init_and_check (writer, container_type, sub);

  switch (container_type)
    {
    case DBUS_TYPE_STRUCT:
      return writer_recurse_struct (writer,
                                    contained_type, contained_type_start, contained_type_len,
                                    sub);
      break;
    case DBUS_TYPE_ARRAY:
      return writer_recurse_array (writer,
                                   contained_type, contained_type_start, contained_type_len,
                                   sub, is_array_append);
      break;
    case DBUS_TYPE_VARIANT:
      return writer_recurse_variant (writer,
                                     contained_type, contained_type_start, contained_type_len,
                                     sub);
      break;
    default:
      _dbus_assert_not_reached ("tried to recurse into type that doesn't support that");
      return FALSE;
      break;
    }
}

/**
 * Opens a new container and writes out the initial information for that container.
 *
 * @param writer the writer
 * @param container_type the type of the container to open
 * @param contained_type the array element type or variant content type
 * @param contained_type_start position to look for the type
 * @param sub the new sub-writer to write container contents
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_type_writer_recurse (DBusTypeWriter   *writer,
                           int               container_type,
                           const DBusString *contained_type,
                           int               contained_type_start,
                           DBusTypeWriter   *sub)
{
  int contained_type_len;

  if (contained_type)
    contained_type_len = find_len_of_complete_type (contained_type, contained_type_start);
  else
    contained_type_len = 0;

  return _dbus_type_writer_recurse_contained_len (writer, container_type,
                                                  contained_type,
                                                  contained_type_start,
                                                  contained_type_len,
                                                  sub,
                                                  FALSE);
}

/**
 * Append to an existing array. Essentially, the writer will read an
 * existing length at the write location; jump over that length; and
 * write new fields. On unrecurse(), the existing length will be
 * updated.
 *
 * @param writer the writer
 * @param contained_type element type
 * @param contained_type_start position of element type
 * @param sub the subwriter to init
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_type_writer_append_array (DBusTypeWriter   *writer,
                                const DBusString *contained_type,
                                int               contained_type_start,
                                DBusTypeWriter   *sub)
{
  int contained_type_len;

  if (contained_type)
    contained_type_len = find_len_of_complete_type (contained_type, contained_type_start);
  else
    contained_type_len = 0;

  return _dbus_type_writer_recurse_contained_len (writer, DBUS_TYPE_ARRAY,
                                                  contained_type,
                                                  contained_type_start,
                                                  contained_type_len,
                                                  sub,
                                                  TRUE);
}

static int
writer_get_array_len (DBusTypeWriter *writer)
{
  _dbus_assert (writer->container_type == DBUS_TYPE_ARRAY);
  return writer->value_pos - writer->u.array.start_pos;
}

/**
 * Closes a container created by _dbus_type_writer_recurse()
 * and writes any additional information to the values block.
 *
 * @param writer the writer
 * @param sub the sub-writer created by _dbus_type_writer_recurse()
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_type_writer_unrecurse (DBusTypeWriter *writer,
                             DBusTypeWriter *sub)
{
  /* type_pos_is_expectation never gets unset once set, or we'd get all hosed */
  _dbus_assert (!writer->type_pos_is_expectation ||
                (writer->type_pos_is_expectation && sub->type_pos_is_expectation));

#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("  type writer %p unrecurse type_pos = %d value_pos = %d is_expectation = %d container_type = %s\n",
                 writer, writer->type_pos, writer->value_pos, writer->type_pos_is_expectation,
                 _dbus_type_to_string (writer->container_type));
  _dbus_verbose ("  type writer %p unrecurse sub type_pos = %d value_pos = %d is_expectation = %d container_type = %s\n",
                 sub, sub->type_pos, sub->value_pos,
                 sub->type_pos_is_expectation,
                 _dbus_type_to_string (sub->container_type));
#endif

  if (sub->container_type == DBUS_TYPE_STRUCT)
    {
      if (!write_or_verify_typecode (sub, DBUS_STRUCT_END_CHAR))
        return FALSE;
    }
  else if (sub->container_type == DBUS_TYPE_ARRAY)
    {
      if (sub->u.array.len_pos >= 0) /* len_pos == -1 if we weren't enabled when we passed it */
        {
          dbus_uint32_t len;

          /* Set the array length */
          len = writer_get_array_len (sub);
          _dbus_marshal_set_uint32 (sub->value_str,
                                    sub->u.array.len_pos,
                                    len,
                                    sub->byte_order);
#if RECURSIVE_MARSHAL_WRITE_TRACE
          _dbus_verbose ("    filled in sub array len to %u at len_pos %d\n",
                         len, sub->u.array.len_pos);
#endif
        }
#if RECURSIVE_MARSHAL_WRITE_TRACE
      else
        {
          _dbus_verbose ("    not filling in sub array len because we were disabled when we passed the len\n");
        }
#endif
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
  if (writer->type_str != NULL)
    {
      if (sub->container_type == DBUS_TYPE_STRUCT &&
          (writer->container_type == DBUS_TYPE_STRUCT ||
           writer->container_type == DBUS_TYPE_INVALID))
        {
          /* Advance the parent to the next struct field */
          writer->type_pos = sub->type_pos;
        }
    }

  writer->value_pos = sub->value_pos;

#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("  type writer %p unrecursed type_pos = %d value_pos = %d remaining sig '%s'\n",
                 writer, writer->type_pos, writer->value_pos,
                 writer->type_str ?
                 _dbus_string_get_const_data_len (writer->type_str, writer->type_pos, 0) :
                 "unknown");
#endif

  return TRUE;
}

/**
 * Writes out a basic type.
 *
 * @param writer the writer
 * @param type the type to write
 * @param value the address of the value to write
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_type_writer_write_basic (DBusTypeWriter *writer,
                               int             type,
                               const void     *value)
{
  dbus_bool_t retval;

  /* First ensure that our type realloc will succeed */
  if (!writer->type_pos_is_expectation && writer->type_str != NULL)
    {
      if (!_dbus_string_alloc_space (writer->type_str, 1))
        return FALSE;
    }

  retval = FALSE;

  if (!_dbus_type_writer_write_basic_no_typecode (writer, type, value))
    goto out;

  if (!write_or_verify_typecode (writer, type))
    _dbus_assert_not_reached ("failed to write typecode after prealloc");

  retval = TRUE;

 out:
#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("  type writer %p basic type_pos = %d value_pos = %d is_expectation = %d enabled = %d\n",
                 writer, writer->type_pos, writer->value_pos, writer->type_pos_is_expectation,
                 writer->enabled);
#endif

  return retval;
}

/**
 * Writes a block of fixed-length basic values, i.e. those that are
 * both _dbus_type_is_fixed() and _dbus_type_is_basic(). The block
 * must be written inside an array.
 *
 * The value parameter should be the address of said array of values,
 * so e.g. if it's an array of double, pass in "const double**"
 *
 * @param writer the writer
 * @param element_type type of stuff in the array
 * @param value address of the array
 * @param n_elements number of elements in the array
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_type_writer_write_fixed_multi (DBusTypeWriter        *writer,
                                     int                    element_type,
                                     const void            *value,
                                     int                    n_elements)
{
  _dbus_assert (writer->container_type == DBUS_TYPE_ARRAY);
  _dbus_assert (_dbus_type_is_fixed (element_type));
  _dbus_assert (writer->type_pos_is_expectation);
  _dbus_assert (n_elements >= 0);

#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("  type writer %p entering fixed multi type_pos = %d value_pos = %d n_elements %d\n",
                 writer, writer->type_pos, writer->value_pos, n_elements);
#endif

  if (!write_or_verify_typecode (writer, element_type))
    _dbus_assert_not_reached ("OOM should not happen if only verifying typecode");

  if (writer->enabled)
    {
      if (!_dbus_marshal_write_fixed_multi (writer->value_str,
                                            writer->value_pos,
                                            element_type,
                                            value,
                                            n_elements,
                                            writer->byte_order,
                                            &writer->value_pos))
        return FALSE;
    }

#if RECURSIVE_MARSHAL_WRITE_TRACE
  _dbus_verbose ("  type writer %p fixed multi written new type_pos = %d new value_pos = %d n_elements %d\n",
                 writer, writer->type_pos, writer->value_pos, n_elements);
#endif

  return TRUE;
}

static void
enable_if_after (DBusTypeWriter       *writer,
                 DBusTypeReader       *reader,
                 const DBusTypeReader *start_after)
{
  if (start_after)
    {
      if (!writer->enabled && _dbus_type_reader_greater_than (reader, start_after))
        {
          _dbus_type_writer_set_enabled (writer, TRUE);
#if RECURSIVE_MARSHAL_WRITE_TRACE
          _dbus_verbose ("ENABLING writer %p at %d because reader at value_pos %d is after reader at value_pos %d\n",
                         writer, writer->value_pos, reader->value_pos, start_after->value_pos);
#endif
        }

      _dbus_assert ((!writer->enabled && !_dbus_type_reader_greater_than (reader, start_after)) ||
                    (writer->enabled && _dbus_type_reader_greater_than (reader, start_after)));
    }
}

static dbus_bool_t
append_fixup (DBusList               **fixups,
              const DBusArrayLenFixup *fixup)
{
  DBusArrayLenFixup *f;

  f = dbus_new (DBusArrayLenFixup, 1);
  if (f == NULL)
    return FALSE;

  *f = *fixup;

  if (!_dbus_list_append (fixups, f))
    {
      dbus_free (f);
      return FALSE;
    }

  _dbus_assert (f->len_pos_in_reader == fixup->len_pos_in_reader);
  _dbus_assert (f->new_len == fixup->new_len);

  return TRUE;
}

/* This loop is trivial if you ignore all the start_after nonsense,
 * so if you're trying to figure it out, start by ignoring that
 */
static dbus_bool_t
writer_write_reader_helper (DBusTypeWriter       *writer,
                            DBusTypeReader       *reader,
                            const DBusTypeReader *start_after,
                            int                   start_after_new_pos,
                            int                   start_after_new_len,
                            DBusList            **fixups,
                            dbus_bool_t           inside_start_after)
{
  int current_type;

  while ((current_type = _dbus_type_reader_get_current_type (reader)) != DBUS_TYPE_INVALID)
    {
      if (_dbus_type_is_container (current_type))
        {
          DBusTypeReader subreader;
          DBusTypeWriter subwriter;
          const DBusString *sig_str;
          int sig_start;
          int sig_len;
          dbus_bool_t enabled_at_recurse;
          dbus_bool_t past_start_after;
          int reader_array_len_pos;
          int reader_array_start_pos;
          dbus_bool_t this_is_start_after;

          /* type_pos is checked since e.g. in a struct the struct
           * and its first field have the same value_pos.
           * type_str will differ in reader/start_after for variants
           * where type_str is inside the value_str
           */
          if (!inside_start_after && start_after &&
              reader->value_pos == start_after->value_pos &&
              reader->type_str == start_after->type_str &&
              reader->type_pos == start_after->type_pos)
            this_is_start_after = TRUE;
          else
            this_is_start_after = FALSE;

          _dbus_type_reader_recurse (reader, &subreader);

          if (current_type == DBUS_TYPE_ARRAY)
            {
              reader_array_len_pos = ARRAY_READER_LEN_POS (&subreader);
              reader_array_start_pos = subreader.u.array.start_pos;
            }
          else
            {
              /* quiet gcc */
              reader_array_len_pos = -1;
              reader_array_start_pos = -1;
            }

          _dbus_type_reader_get_signature (&subreader, &sig_str,
                                           &sig_start, &sig_len);

#if RECURSIVE_MARSHAL_WRITE_TRACE
          _dbus_verbose ("about to recurse into %s reader at %d subreader at %d writer at %d start_after reader at %d write target len %d inside_start_after = %d this_is_start_after = %d\n",
                         _dbus_type_to_string (current_type),
                         reader->value_pos,
                         subreader.value_pos,
                         writer->value_pos,
                         start_after ? start_after->value_pos : -1,
                         _dbus_string_get_length (writer->value_str),
                         inside_start_after, this_is_start_after);
#endif

          if (!inside_start_after && !this_is_start_after)
            enable_if_after (writer, &subreader, start_after);
          enabled_at_recurse = writer->enabled;
          if (!_dbus_type_writer_recurse_contained_len (writer, current_type,
                                                        sig_str, sig_start, sig_len,
                                                        &subwriter, FALSE))
            goto oom;

#if RECURSIVE_MARSHAL_WRITE_TRACE
          _dbus_verbose ("recursed into subwriter at %d write target len %d\n",
                         subwriter.value_pos,
                         _dbus_string_get_length (subwriter.value_str));
#endif

          if (!writer_write_reader_helper (&subwriter, &subreader, start_after,
                                           start_after_new_pos, start_after_new_len,
                                           fixups,
                                           inside_start_after ||
                                           this_is_start_after))
            goto oom;

#if RECURSIVE_MARSHAL_WRITE_TRACE
          _dbus_verbose ("about to unrecurse from %s subreader at %d writer at %d subwriter at %d  write target len %d\n",
                         _dbus_type_to_string (current_type),
                         subreader.value_pos,
                         writer->value_pos,
                         subwriter.value_pos,
                         _dbus_string_get_length (writer->value_str));
#endif

          if (!inside_start_after && !this_is_start_after)
            enable_if_after (writer, &subreader, start_after);
          past_start_after = writer->enabled;
          if (!_dbus_type_writer_unrecurse (writer, &subwriter))
            goto oom;

          /* If we weren't enabled when we recursed, we didn't
           * write an array len; if we passed start_after
           * somewhere inside the array, then we need to generate
           * a fixup.
           */
          if (start_after != NULL &&
              !enabled_at_recurse && past_start_after &&
              current_type == DBUS_TYPE_ARRAY &&
              fixups != NULL)
            {
              DBusArrayLenFixup fixup;
              int bytes_written_after_start_after;
              int bytes_before_start_after;
              int old_len;

              /* this subwriter access is moderately unkosher since we
               * already unrecursed, but it works as long as unrecurse
               * doesn't break us on purpose
               */
              bytes_written_after_start_after = writer_get_array_len (&subwriter);

              bytes_before_start_after =
                start_after->value_pos - reader_array_start_pos;

              fixup.len_pos_in_reader = reader_array_len_pos;
              fixup.new_len =
                bytes_before_start_after +
                start_after_new_len +
                bytes_written_after_start_after;

              _dbus_assert (_DBUS_ALIGN_VALUE (fixup.len_pos_in_reader, 4) ==
                            (unsigned) fixup.len_pos_in_reader);

              old_len = _dbus_unpack_uint32 (reader->byte_order,
                                             _dbus_string_get_const_data_len (reader->value_str,
                                                                              fixup.len_pos_in_reader, 4));

              if (old_len != fixup.new_len && !append_fixup (fixups, &fixup))
                goto oom;

#if RECURSIVE_MARSHAL_WRITE_TRACE
              _dbus_verbose ("Generated fixup len_pos_in_reader = %d new_len = %d reader_array_start_pos = %d start_after->value_pos = %d bytes_before_start_after = %d start_after_new_len = %d bytes_written_after_start_after = %d\n",
                             fixup.len_pos_in_reader,
                             fixup.new_len,
                             reader_array_start_pos,
                             start_after->value_pos,
                             bytes_before_start_after,
                             start_after_new_len,
                             bytes_written_after_start_after);
#endif
            }
        }
      else
        {
          DBusBasicValue val;

          _dbus_assert (_dbus_type_is_basic (current_type));

#if RECURSIVE_MARSHAL_WRITE_TRACE
          _dbus_verbose ("Reading basic value %s at %d\n",
                         _dbus_type_to_string (current_type),
                         reader->value_pos);
#endif

          _dbus_type_reader_read_basic (reader, &val);

#if RECURSIVE_MARSHAL_WRITE_TRACE
          _dbus_verbose ("Writing basic value %s at %d write target len %d inside_start_after = %d\n",
                         _dbus_type_to_string (current_type),
                         writer->value_pos,
                         _dbus_string_get_length (writer->value_str),
                         inside_start_after);
#endif
          if (!inside_start_after)
            enable_if_after (writer, reader, start_after);
          if (!_dbus_type_writer_write_basic (writer, current_type, &val))
            goto oom;
#if RECURSIVE_MARSHAL_WRITE_TRACE
          _dbus_verbose ("Wrote basic value %s, new value_pos %d write target len %d\n",
                         _dbus_type_to_string (current_type),
                         writer->value_pos,
                         _dbus_string_get_length (writer->value_str));
#endif
        }

      _dbus_type_reader_next (reader);
    }

  return TRUE;

 oom:
  if (fixups)
    apply_and_free_fixups (fixups, NULL); /* NULL for reader to apply to */

  return FALSE;
}

/**
 * Iterate through all values in the given reader, writing a copy of
 * each value to the writer.  The reader will be moved forward to its
 * end position.
 *
 * If a reader start_after is provided, it should be a reader for the
 * same data as the reader to be written. Only values occurring after
 * the value pointed to by start_after will be written to the writer.
 *
 * If start_after is provided, then the copy of the reader will be
 * partial. This means that array lengths will not have been copied.
 * The assumption is that you wrote a new version of the value at
 * start_after to the writer. You have to pass in the start position
 * and length of the new value. (If you are deleting the value
 * at start_after, pass in 0 for the length.)
 *
 * If the fixups parameter is non-#NULL, then any array length that
 * was read but not written due to start_after will be provided
 * as a #DBusArrayLenFixup. The fixup contains the position of the
 * array length in the source data, and the correct array length
 * assuming you combine the source data before start_after with
 * the written data at start_after and beyond.
 *
 * @param writer the writer to copy to
 * @param reader the reader to copy from
 * @param start_after #NULL or a reader showing where to start
 * @param start_after_new_pos the position of start_after equivalent in the target data
 * @param start_after_new_len the length of start_after equivalent in the target data
 * @param fixups list to append #DBusArrayLenFixup if the write was partial
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_type_writer_write_reader_partial (DBusTypeWriter       *writer,
                                        DBusTypeReader       *reader,
                                        const DBusTypeReader *start_after,
                                        int                   start_after_new_pos,
                                        int                   start_after_new_len,
                                        DBusList            **fixups)
{
  DBusTypeWriter orig;
  int orig_type_len;
  int orig_value_len;
  int new_bytes;
  int orig_enabled;

  orig = *writer;
  orig_type_len = _dbus_string_get_length (writer->type_str);
  orig_value_len = _dbus_string_get_length (writer->value_str);
  orig_enabled = writer->enabled;

  if (start_after)
    _dbus_type_writer_set_enabled (writer, FALSE);

  if (!writer_write_reader_helper (writer, reader, start_after,
                                   start_after_new_pos,
                                   start_after_new_len,
                                   fixups, FALSE))
    goto oom;

  _dbus_type_writer_set_enabled (writer, orig_enabled);
  return TRUE;

 oom:
  if (!writer->type_pos_is_expectation)
    {
      new_bytes = _dbus_string_get_length (writer->type_str) - orig_type_len;
      _dbus_string_delete (writer->type_str, orig.type_pos, new_bytes);
    }
  new_bytes = _dbus_string_get_length (writer->value_str) - orig_value_len;
  _dbus_string_delete (writer->value_str, orig.value_pos, new_bytes);

  *writer = orig;

  return FALSE;
}

/**
 * Iterate through all values in the given reader, writing a copy of
 * each value to the writer.  The reader will be moved forward to its
 * end position.
 *
 * @param writer the writer to copy to
 * @param reader the reader to copy from
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_type_writer_write_reader (DBusTypeWriter       *writer,
                                DBusTypeReader       *reader)
{
  return _dbus_type_writer_write_reader_partial (writer, reader, NULL, 0, 0, NULL);
}

/**
 * If disabled, a writer can still be iterated forward and recursed/unrecursed
 * but won't write any values. Types will still be written unless the
 * writer is a "values only" writer, because the writer needs access to
 * a valid signature to be able to iterate.
 *
 * @param writer the type writer
 * @param enabled #TRUE if values should be written
 */
void
_dbus_type_writer_set_enabled (DBusTypeWriter   *writer,
                               dbus_bool_t       enabled)
{
  writer->enabled = enabled != FALSE;
}

/** @} */ /* end of DBusMarshal group */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include "dbus-list.h"
#include <stdio.h>
#include <stdlib.h>

/* Whether to do the OOM stuff (only with other expensive tests) */
#define TEST_OOM_HANDLING 0
/* We do start offset 0 through 9, to get various alignment cases. Still this
 * obviously makes the test suite run 10x as slow.
 */
#define MAX_INITIAL_OFFSET 9

/* Largest iteration count to test copying, realignment,
 * etc. with. i.e. we only test this stuff with some of the smaller
 * data sets.
 */
#define MAX_ITERATIONS_FOR_EXPENSIVE_TESTS 1000

typedef struct
{
  int byte_order;
  int initial_offset;
  DBusString signature;
  DBusString body;
} DataBlock;

typedef struct
{
  int saved_sig_len;
  int saved_body_len;
} DataBlockState;

#define N_FENCE_BYTES 5
#define FENCE_BYTES_STR "abcde"
#define INITIAL_PADDING_BYTE '\0'

static dbus_bool_t
data_block_init (DataBlock *block,
                 int        byte_order,
                 int        initial_offset)
{
  if (!_dbus_string_init (&block->signature))
    return FALSE;

  if (!_dbus_string_init (&block->body))
    {
      _dbus_string_free (&block->signature);
      return FALSE;
    }

  if (!_dbus_string_insert_bytes (&block->signature, 0, initial_offset,
                                  INITIAL_PADDING_BYTE) ||
      !_dbus_string_insert_bytes (&block->body, 0, initial_offset,
                                  INITIAL_PADDING_BYTE) ||
      !_dbus_string_append (&block->signature, FENCE_BYTES_STR) ||
      !_dbus_string_append (&block->body, FENCE_BYTES_STR))
    {
      _dbus_string_free (&block->signature);
      _dbus_string_free (&block->body);
      return FALSE;
    }

  block->byte_order = byte_order;
  block->initial_offset = initial_offset;

  return TRUE;
}

static void
data_block_save (DataBlock      *block,
                 DataBlockState *state)
{
  state->saved_sig_len = _dbus_string_get_length (&block->signature) - N_FENCE_BYTES;
  state->saved_body_len = _dbus_string_get_length (&block->body) - N_FENCE_BYTES;
}

static void
data_block_restore (DataBlock      *block,
                    DataBlockState *state)
{
  _dbus_string_delete (&block->signature,
                       state->saved_sig_len,
                       _dbus_string_get_length (&block->signature) - state->saved_sig_len - N_FENCE_BYTES);
  _dbus_string_delete (&block->body,
                       state->saved_body_len,
                       _dbus_string_get_length (&block->body) - state->saved_body_len - N_FENCE_BYTES);
}

static void
data_block_verify (DataBlock *block)
{
  if (!_dbus_string_ends_with_c_str (&block->signature,
                                     FENCE_BYTES_STR))
    {
      int offset;

      offset = _dbus_string_get_length (&block->signature) - N_FENCE_BYTES - 8;
      if (offset < 0)
        offset = 0;

      _dbus_verbose_bytes_of_string (&block->signature,
                                     offset,
                                     _dbus_string_get_length (&block->signature) - offset);
      _dbus_assert_not_reached ("block did not verify: bad bytes at end of signature");
    }
  if (!_dbus_string_ends_with_c_str (&block->body,
                                     FENCE_BYTES_STR))
    {
      int offset;

      offset = _dbus_string_get_length (&block->body) - N_FENCE_BYTES - 8;
      if (offset < 0)
        offset = 0;

      _dbus_verbose_bytes_of_string (&block->body,
                                     offset,
                                     _dbus_string_get_length (&block->body) - offset);
      _dbus_assert_not_reached ("block did not verify: bad bytes at end of body");
    }

  _dbus_assert (_dbus_string_validate_nul (&block->signature,
                                           0, block->initial_offset));
  _dbus_assert (_dbus_string_validate_nul (&block->body,
                                           0, block->initial_offset));
}

static void
data_block_free (DataBlock *block)
{
  data_block_verify (block);

  _dbus_string_free (&block->signature);
  _dbus_string_free (&block->body);
}

static void
data_block_reset (DataBlock *block)
{
  data_block_verify (block);

  _dbus_string_delete (&block->signature,
                       block->initial_offset,
                       _dbus_string_get_length (&block->signature) - N_FENCE_BYTES - block->initial_offset);
  _dbus_string_delete (&block->body,
                       block->initial_offset,
                       _dbus_string_get_length (&block->body) - N_FENCE_BYTES - block->initial_offset);

  data_block_verify (block);
}

static void
data_block_init_reader_writer (DataBlock      *block,
                               DBusTypeReader *reader,
                               DBusTypeWriter *writer)
{
  if (reader)
    _dbus_type_reader_init (reader,
                            block->byte_order,
                            &block->signature,
                            block->initial_offset,
                            &block->body,
                            block->initial_offset);

  if (writer)
    _dbus_type_writer_init (writer,
                            block->byte_order,
                            &block->signature,
                            _dbus_string_get_length (&block->signature) - N_FENCE_BYTES,
                            &block->body,
                            _dbus_string_get_length (&block->body) - N_FENCE_BYTES);
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

      _dbus_assert_not_reached ("read wrong type");
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

typedef struct TestTypeNode               TestTypeNode;
typedef struct TestTypeNodeClass          TestTypeNodeClass;
typedef struct TestTypeNodeContainer      TestTypeNodeContainer;
typedef struct TestTypeNodeContainerClass TestTypeNodeContainerClass;

struct TestTypeNode
{
  const TestTypeNodeClass *klass;
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

  int subclass_detail; /* a bad hack to avoid a bunch of subclass casting */

  dbus_bool_t   (* construct)     (TestTypeNode   *node);
  void          (* destroy)       (TestTypeNode   *node);

  dbus_bool_t (* write_value)     (TestTypeNode   *node,
                                   DataBlock      *block,
                                   DBusTypeWriter *writer,
                                   int             seed);
  dbus_bool_t (* read_value)      (TestTypeNode   *node,
                                   DBusTypeReader *reader,
                                   int             seed);
  dbus_bool_t (* set_value)       (TestTypeNode   *node,
                                   DBusTypeReader *reader,
                                   DBusTypeReader *realign_root,
                                   int             seed);
  dbus_bool_t (* build_signature) (TestTypeNode   *node,
                                   DBusString     *str);
  dbus_bool_t (* write_multi)     (TestTypeNode   *node,
                                   DataBlock      *block,
                                   DBusTypeWriter *writer,
                                   int             seed,
                                   int             count);
  dbus_bool_t (* read_multi)      (TestTypeNode   *node,
                                   DBusTypeReader *reader,
                                   int             seed,
                                   int             count);
};

struct TestTypeNodeContainerClass
{
  TestTypeNodeClass base;
};

/* FIXME this could be chilled out substantially by unifying
 * the basic types into basic_write_value/basic_read_value
 * and by merging read_value and set_value into one function
 * taking a flag argument.
 */
static dbus_bool_t int32_write_value       (TestTypeNode   *node,
                                            DataBlock      *block,
                                            DBusTypeWriter *writer,
                                            int             seed);
static dbus_bool_t int32_read_value        (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            int             seed);
static dbus_bool_t int32_set_value         (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            DBusTypeReader *realign_root,
                                            int             seed);
static dbus_bool_t int32_write_multi       (TestTypeNode   *node,
                                            DataBlock      *block,
                                            DBusTypeWriter *writer,
                                            int             seed,
                                            int             count);
static dbus_bool_t int32_read_multi        (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            int             seed,
                                            int             count);
static dbus_bool_t int64_write_value       (TestTypeNode   *node,
                                            DataBlock      *block,
                                            DBusTypeWriter *writer,
                                            int             seed);
static dbus_bool_t int64_read_value        (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            int             seed);
static dbus_bool_t int64_set_value         (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            DBusTypeReader *realign_root,
                                            int             seed);
static dbus_bool_t string_write_value      (TestTypeNode   *node,
                                            DataBlock      *block,
                                            DBusTypeWriter *writer,
                                            int             seed);
static dbus_bool_t string_read_value       (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            int             seed);
static dbus_bool_t string_set_value        (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            DBusTypeReader *realign_root,
                                            int             seed);
static dbus_bool_t bool_write_value        (TestTypeNode   *node,
                                            DataBlock      *block,
                                            DBusTypeWriter *writer,
                                            int             seed);
static dbus_bool_t bool_read_value         (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            int             seed);
static dbus_bool_t bool_set_value          (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            DBusTypeReader *realign_root,
                                            int             seed);
static dbus_bool_t byte_write_value        (TestTypeNode   *node,
                                            DataBlock      *block,
                                            DBusTypeWriter *writer,
                                            int             seed);
static dbus_bool_t byte_read_value         (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            int             seed);
static dbus_bool_t byte_set_value          (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            DBusTypeReader *realign_root,
                                            int             seed);
static dbus_bool_t double_write_value      (TestTypeNode   *node,
                                            DataBlock      *block,
                                            DBusTypeWriter *writer,
                                            int             seed);
static dbus_bool_t double_read_value       (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            int             seed);
static dbus_bool_t double_set_value        (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            DBusTypeReader *realign_root,
                                            int             seed);
static dbus_bool_t object_path_write_value (TestTypeNode   *node,
                                            DataBlock      *block,
                                            DBusTypeWriter *writer,
                                            int             seed);
static dbus_bool_t object_path_read_value  (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            int             seed);
static dbus_bool_t object_path_set_value   (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            DBusTypeReader *realign_root,
                                            int             seed);
static dbus_bool_t signature_write_value   (TestTypeNode   *node,
                                            DataBlock      *block,
                                            DBusTypeWriter *writer,
                                            int             seed);
static dbus_bool_t signature_read_value    (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            int             seed);
static dbus_bool_t signature_set_value     (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            DBusTypeReader *realign_root,
                                            int             seed);
static dbus_bool_t struct_write_value      (TestTypeNode   *node,
                                            DataBlock      *block,
                                            DBusTypeWriter *writer,
                                            int             seed);
static dbus_bool_t struct_read_value       (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            int             seed);
static dbus_bool_t struct_set_value        (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            DBusTypeReader *realign_root,
                                            int             seed);
static dbus_bool_t struct_build_signature  (TestTypeNode   *node,
                                            DBusString     *str);
static dbus_bool_t array_write_value       (TestTypeNode   *node,
                                            DataBlock      *block,
                                            DBusTypeWriter *writer,
                                            int             seed);
static dbus_bool_t array_read_value        (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            int             seed);
static dbus_bool_t array_set_value         (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            DBusTypeReader *realign_root,
                                            int             seed);
static dbus_bool_t array_build_signature   (TestTypeNode   *node,
                                            DBusString     *str);
static dbus_bool_t variant_write_value     (TestTypeNode   *node,
                                            DataBlock      *block,
                                            DBusTypeWriter *writer,
                                            int             seed);
static dbus_bool_t variant_read_value      (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            int             seed);
static dbus_bool_t variant_set_value       (TestTypeNode   *node,
                                            DBusTypeReader *reader,
                                            DBusTypeReader *realign_root,
                                            int             seed);
static void        container_destroy       (TestTypeNode   *node);


static const TestTypeNodeClass int32_class = {
  DBUS_TYPE_INT32,
  sizeof (TestTypeNode),
  0,
  NULL,
  NULL,
  int32_write_value,
  int32_read_value,
  int32_set_value,
  NULL,
  int32_write_multi,
  int32_read_multi
};

static const TestTypeNodeClass uint32_class = {
  DBUS_TYPE_UINT32,
  sizeof (TestTypeNode),
  0,
  NULL,
  NULL,
  int32_write_value, /* recycle from int32 */
  int32_read_value,  /* recycle from int32 */
  int32_set_value,   /* recycle from int32 */
  NULL,
  int32_write_multi, /* recycle from int32 */
  int32_read_multi   /* recycle from int32 */
};

static const TestTypeNodeClass int64_class = {
  DBUS_TYPE_INT64,
  sizeof (TestTypeNode),
  0,
  NULL,
  NULL,
  int64_write_value,
  int64_read_value,
  int64_set_value,
  NULL,
  NULL, /* FIXME */
  NULL  /* FIXME */
};

static const TestTypeNodeClass uint64_class = {
  DBUS_TYPE_UINT64,
  sizeof (TestTypeNode),
  0,
  NULL,
  NULL,
  int64_write_value, /* recycle from int64 */
  int64_read_value,  /* recycle from int64 */
  int64_set_value,   /* recycle from int64 */
  NULL,
  NULL, /* FIXME */
  NULL  /* FIXME */
};

static const TestTypeNodeClass string_0_class = {
  DBUS_TYPE_STRING,
  sizeof (TestTypeNode),
  0, /* string length */
  NULL,
  NULL,
  string_write_value,
  string_read_value,
  string_set_value,
  NULL,
  NULL,
  NULL
};

static const TestTypeNodeClass string_1_class = {
  DBUS_TYPE_STRING,
  sizeof (TestTypeNode),
  1, /* string length */
  NULL,
  NULL,
  string_write_value,
  string_read_value,
  string_set_value,
  NULL,
  NULL,
  NULL
};

/* with nul, a len 3 string should fill 4 bytes and thus is "special" */
static const TestTypeNodeClass string_3_class = {
  DBUS_TYPE_STRING,
  sizeof (TestTypeNode),
  3, /* string length */
  NULL,
  NULL,
  string_write_value,
  string_read_value,
  string_set_value,
  NULL,
  NULL,
  NULL
};

/* with nul, a len 8 string should fill 9 bytes and thus is "special" (far-fetched I suppose) */
static const TestTypeNodeClass string_8_class = {
  DBUS_TYPE_STRING,
  sizeof (TestTypeNode),
  8, /* string length */
  NULL,
  NULL,
  string_write_value,
  string_read_value,
  string_set_value,
  NULL,
  NULL,
  NULL
};

static const TestTypeNodeClass bool_class = {
  DBUS_TYPE_BOOLEAN,
  sizeof (TestTypeNode),
  0,
  NULL,
  NULL,
  bool_write_value,
  bool_read_value,
  bool_set_value,
  NULL,
  NULL, /* FIXME */
  NULL  /* FIXME */
};

static const TestTypeNodeClass byte_class = {
  DBUS_TYPE_BYTE,
  sizeof (TestTypeNode),
  0,
  NULL,
  NULL,
  byte_write_value,
  byte_read_value,
  byte_set_value,
  NULL,
  NULL, /* FIXME */
  NULL  /* FIXME */
};

static const TestTypeNodeClass double_class = {
  DBUS_TYPE_DOUBLE,
  sizeof (TestTypeNode),
  0,
  NULL,
  NULL,
  double_write_value,
  double_read_value,
  double_set_value,
  NULL,
  NULL, /* FIXME */
  NULL  /* FIXME */
};

static const TestTypeNodeClass object_path_class = {
  DBUS_TYPE_OBJECT_PATH,
  sizeof (TestTypeNode),
  0,
  NULL,
  NULL,
  object_path_write_value,
  object_path_read_value,
  object_path_set_value,
  NULL,
  NULL,
  NULL
};

static const TestTypeNodeClass signature_class = {
  DBUS_TYPE_SIGNATURE,
  sizeof (TestTypeNode),
  0,
  NULL,
  NULL,
  signature_write_value,
  signature_read_value,
  signature_set_value,
  NULL,
  NULL,
  NULL
};

static const TestTypeNodeClass struct_1_class = {
  DBUS_TYPE_STRUCT,
  sizeof (TestTypeNodeContainer),
  1, /* number of times children appear as fields */
  NULL,
  container_destroy,
  struct_write_value,
  struct_read_value,
  struct_set_value,
  struct_build_signature,
  NULL,
  NULL
};

static const TestTypeNodeClass struct_2_class = {
  DBUS_TYPE_STRUCT,
  sizeof (TestTypeNodeContainer),
  2, /* number of times children appear as fields */
  NULL,
  container_destroy,
  struct_write_value,
  struct_read_value,
  struct_set_value,
  struct_build_signature,
  NULL,
  NULL
};

static dbus_bool_t arrays_write_fixed_in_blocks = FALSE;

static const TestTypeNodeClass array_0_class = {
  DBUS_TYPE_ARRAY,
  sizeof (TestTypeNodeContainer),
  0, /* number of array elements */
  NULL,
  container_destroy,
  array_write_value,
  array_read_value,
  array_set_value,
  array_build_signature,
  NULL,
  NULL
};

static const TestTypeNodeClass array_1_class = {
  DBUS_TYPE_ARRAY,
  sizeof (TestTypeNodeContainer),
  1, /* number of array elements */
  NULL,
  container_destroy,
  array_write_value,
  array_read_value,
  array_set_value,
  array_build_signature,
  NULL,
  NULL
};

static const TestTypeNodeClass array_2_class = {
  DBUS_TYPE_ARRAY,
  sizeof (TestTypeNodeContainer),
  2, /* number of array elements */
  NULL,
  container_destroy,
  array_write_value,
  array_read_value,
  array_set_value,
  array_build_signature,
  NULL,
  NULL
};

static const TestTypeNodeClass array_9_class = {
  DBUS_TYPE_ARRAY,
  sizeof (TestTypeNodeContainer),
  9, /* number of array elements */
  NULL,
  container_destroy,
  array_write_value,
  array_read_value,
  array_set_value,
  array_build_signature,
  NULL,
  NULL
};

static const TestTypeNodeClass variant_class = {
  DBUS_TYPE_VARIANT,
  sizeof (TestTypeNodeContainer),
  0,
  NULL,
  container_destroy,
  variant_write_value,
  variant_read_value,
  variant_set_value,
  NULL,
  NULL,
  NULL
};

static const TestTypeNodeClass* const
basic_nodes[] = {
  &int32_class,
  &uint32_class,
  &int64_class,
  &uint64_class,
  &bool_class,
  &byte_class,
  &double_class,
  &string_0_class,
  &string_1_class,
  &string_3_class,
  &string_8_class,
  &object_path_class,
  &signature_class
};
#define N_BASICS (_DBUS_N_ELEMENTS (basic_nodes))

static const TestTypeNodeClass* const
container_nodes[] = {
  &struct_1_class,
  &array_1_class,
  &struct_2_class,
  &array_0_class,
  &array_2_class,
  &variant_class
  /* array_9_class is omitted on purpose, it's too slow;
   * we only use it in one hardcoded test below
   */
};
#define N_CONTAINERS (_DBUS_N_ELEMENTS (container_nodes))

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
                  DBusTypeWriter *writer,
                  int             seed)
{
  dbus_bool_t retval;

  retval = (* node->klass->write_value) (node, block, writer, seed);

#if 0
  /* Handy to see where things break, but too expensive to do all the time */
  data_block_verify (block);
#endif

  return retval;
}

static dbus_bool_t
node_read_value (TestTypeNode   *node,
                 DBusTypeReader *reader,
                 int             seed)
{
  DBusTypeMark mark;
  DBusTypeReader restored;

  _dbus_type_reader_save_mark (reader, &mark);

  if (!(* node->klass->read_value) (node, reader, seed))
    return FALSE;

  _dbus_type_reader_init_from_mark (&restored,
                                    reader->byte_order,
                                    reader->type_str,
                                    reader->value_str,
                                    &mark);

  if (!(* node->klass->read_value) (node, &restored, seed))
    return FALSE;

  return TRUE;
}

/* Warning: if this one fails due to OOM, it has side effects (can
 * modify only some of the sub-values). OK in a test suite, but we
 * never do this in real code.
 */
static dbus_bool_t
node_set_value (TestTypeNode   *node,
                DBusTypeReader *reader,
                DBusTypeReader *realign_root,
                int             seed)
{
  if (!(* node->klass->set_value) (node, reader, realign_root, seed))
    return FALSE;

  return TRUE;
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

  if (!_dbus_list_append (&container->children, child))
    _dbus_assert_not_reached ("no memory"); /* we never check the return value on node_append_child anyhow - it's run from outside the malloc-failure test code */

  return TRUE;
}

static dbus_bool_t
node_write_multi (TestTypeNode   *node,
                  DataBlock      *block,
                  DBusTypeWriter *writer,
                  int             seed,
                  int             n_copies)
{
  dbus_bool_t retval;

  _dbus_assert (node->klass->write_multi != NULL);
  retval = (* node->klass->write_multi) (node, block, writer, seed, n_copies);

#if 0
  /* Handy to see where things break, but too expensive to do all the time */
  data_block_verify (block);
#endif

  return retval;
}

static dbus_bool_t
node_read_multi (TestTypeNode   *node,
                 DBusTypeReader *reader,
                 int             seed,
                 int             n_copies)
{
  _dbus_assert (node->klass->read_multi != NULL);

  if (!(* node->klass->read_multi) (node, reader, seed, n_copies))
    return FALSE;

  return TRUE;
}

static int n_iterations_completed_total = 0;
static int n_iterations_completed_this_test = 0;
static int n_iterations_expected_this_test = 0;

typedef struct
{
  const DBusString   *signature;
  DataBlock          *block;
  int                 type_offset;
  TestTypeNode      **nodes;
  int                 n_nodes;
} NodeIterationData;

static dbus_bool_t
run_test_copy (NodeIterationData *nid)
{
  DataBlock *src;
  DataBlock dest;
  dbus_bool_t retval;
  DBusTypeReader reader;
  DBusTypeWriter writer;

  _dbus_verbose ("%s\n", _DBUS_FUNCTION_NAME);

  src = nid->block;

  retval = FALSE;

  if (!data_block_init (&dest, src->byte_order, src->initial_offset))
    return FALSE;

  data_block_init_reader_writer (src, &reader, NULL);
  data_block_init_reader_writer (&dest, NULL, &writer);

  /* DBusTypeWriter assumes it's writing into an existing signature,
   * so doesn't add nul on its own. We have to do that.
   */
  if (!_dbus_string_insert_byte (&dest.signature,
                                 dest.initial_offset, '\0'))
    goto out;

  if (!_dbus_type_writer_write_reader (&writer, &reader))
    goto out;

  /* Data blocks should now be identical */
  if (!_dbus_string_equal (&src->signature, &dest.signature))
    {
      _dbus_verbose ("SOURCE\n");
      _dbus_verbose_bytes_of_string (&src->signature, 0,
                                     _dbus_string_get_length (&src->signature));
      _dbus_verbose ("DEST\n");
      _dbus_verbose_bytes_of_string (&dest.signature, 0,
                                     _dbus_string_get_length (&dest.signature));
      _dbus_assert_not_reached ("signatures did not match");
    }

  if (!_dbus_string_equal (&src->body, &dest.body))
    {
      _dbus_verbose ("SOURCE\n");
      _dbus_verbose_bytes_of_string (&src->body, 0,
                                     _dbus_string_get_length (&src->body));
      _dbus_verbose ("DEST\n");
      _dbus_verbose_bytes_of_string (&dest.body, 0,
                                     _dbus_string_get_length (&dest.body));
      _dbus_assert_not_reached ("bodies did not match");
    }

  retval = TRUE;

 out:

  data_block_free (&dest);

  return retval;
}

static dbus_bool_t
run_test_values_only_write (NodeIterationData *nid)
{
  DBusTypeReader reader;
  DBusTypeWriter writer;
  int i;
  dbus_bool_t retval;
  int sig_len;

  _dbus_verbose ("%s\n", _DBUS_FUNCTION_NAME);

  retval = FALSE;

  data_block_reset (nid->block);

  sig_len = _dbus_string_get_length (nid->signature);

  _dbus_type_writer_init_values_only (&writer,
                                      nid->block->byte_order,
                                      nid->signature, 0,
                                      &nid->block->body,
                                      _dbus_string_get_length (&nid->block->body) - N_FENCE_BYTES);
  _dbus_type_reader_init (&reader,
                          nid->block->byte_order,
                          nid->signature, 0,
                          &nid->block->body,
                          nid->block->initial_offset);

  i = 0;
  while (i < nid->n_nodes)
    {
      if (!node_write_value (nid->nodes[i], nid->block, &writer, i))
        goto out;

      ++i;
    }

  /* if we wrote any typecodes then this would fail */
  _dbus_assert (sig_len == _dbus_string_get_length (nid->signature));

  /* But be sure we wrote out the values correctly */
  i = 0;
  while (i < nid->n_nodes)
    {
      if (!node_read_value (nid->nodes[i], &reader, i))
        goto out;

      if (i + 1 == nid->n_nodes)
        NEXT_EXPECTING_FALSE (&reader);
      else
        NEXT_EXPECTING_TRUE (&reader);

      ++i;
    }

  retval = TRUE;

 out:
  data_block_reset (nid->block);
  return retval;
}

/* offset the seed for setting, so we set different numbers than
 * we originally wrote. Don't offset by a huge number since in
 * some cases it's value = possibilities[seed % n_possibilities]
 * and we don't want to wrap around. bool_from_seed
 * is just seed % 2 even.
 */
#define SET_SEED 1
static dbus_bool_t
run_test_set_values (NodeIterationData *nid)
{
  DBusTypeReader reader;
  DBusTypeReader realign_root;
  dbus_bool_t retval;
  int i;

  _dbus_verbose ("%s\n", _DBUS_FUNCTION_NAME);

  retval = FALSE;

  data_block_init_reader_writer (nid->block,
                                 &reader, NULL);

  realign_root = reader;

  i = 0;
  while (i < nid->n_nodes)
    {
      if (!node_set_value (nid->nodes[i],
                           &reader, &realign_root,
                           i + SET_SEED))
        goto out;

      if (i + 1 == nid->n_nodes)
        NEXT_EXPECTING_FALSE (&reader);
      else
        NEXT_EXPECTING_TRUE (&reader);

      ++i;
    }

  /* Check that the new values were set */

  reader = realign_root;

  i = 0;
  while (i < nid->n_nodes)
    {
      if (!node_read_value (nid->nodes[i], &reader,
                            i + SET_SEED))
        goto out;

      if (i + 1 == nid->n_nodes)
        NEXT_EXPECTING_FALSE (&reader);
      else
        NEXT_EXPECTING_TRUE (&reader);

      ++i;
    }

  retval = TRUE;

 out:
  return retval;
}

static dbus_bool_t
run_test_delete_values (NodeIterationData *nid)
{
  DBusTypeReader reader;
  dbus_bool_t retval;
  int t;

  _dbus_verbose ("%s\n", _DBUS_FUNCTION_NAME);

  retval = FALSE;

  data_block_init_reader_writer (nid->block,
                                 &reader, NULL);

  while ((t = _dbus_type_reader_get_current_type (&reader)) != DBUS_TYPE_INVALID)
    {
      /* Right now, deleting only works on array elements.  We delete
       * all array elements, and then verify that there aren't any
       * left.
       */
      if (t == DBUS_TYPE_ARRAY)
        {
          DBusTypeReader array;
          int n_elements;
          int elem_type;

          _dbus_type_reader_recurse (&reader, &array);
          n_elements = 0;
          while (_dbus_type_reader_get_current_type (&array) != DBUS_TYPE_INVALID)
            {
              n_elements += 1;
              _dbus_type_reader_next (&array);
            }

          /* reset to start of array */
          _dbus_type_reader_recurse (&reader, &array);
          _dbus_verbose ("recursing into deletion loop reader.value_pos = %d array.value_pos = %d array.u.start_pos = %d\n",
                         reader.value_pos, array.value_pos, array.u.array.start_pos);
          while ((elem_type = _dbus_type_reader_get_current_type (&array)) != DBUS_TYPE_INVALID)
            {
              /* We don't want to always delete from the same part of the array. */
              static int cycle = 0;
              int elem;

              _dbus_assert (n_elements > 0);
              _dbus_assert (!_dbus_type_reader_array_is_empty (&reader));

              elem = cycle;
              if (elem == 3 || elem >= n_elements) /* end of array */
                elem = n_elements - 1;

              _dbus_verbose ("deleting array element %d of %d type %s cycle %d reader pos %d elem pos %d\n",
                             elem, n_elements, _dbus_type_to_string (elem_type),
                             cycle, reader.value_pos, array.value_pos);
              while (elem > 0)
                {
                  if (!_dbus_type_reader_next (&array))
                    _dbus_assert_not_reached ("should have had another element\n");
                  --elem;
                }

              if (!_dbus_type_reader_delete (&array, &reader))
                goto out;

              n_elements -= 1;

              /* reset */
              _dbus_type_reader_recurse (&reader, &array);

              if (cycle > 2)
                cycle = 0;
              else
                cycle += 1;
            }
        }
      _dbus_type_reader_next (&reader);
    }

  /* Check that there are no array elements left */
  data_block_init_reader_writer (nid->block,
                                 &reader, NULL);

  while ((t = _dbus_type_reader_get_current_type (&reader)) != DBUS_TYPE_INVALID)
    {
      if (t == DBUS_TYPE_ARRAY)
        _dbus_assert (_dbus_type_reader_array_is_empty (&reader));

      _dbus_type_reader_next (&reader);
    }

  retval = TRUE;

 out:
  return retval;
}

static dbus_bool_t
run_test_nodes_iteration (void *data)
{
  NodeIterationData *nid = data;
  DBusTypeReader reader;
  DBusTypeWriter writer;
  int i;
  dbus_bool_t retval;

  /* Stuff to do:
   * 1. write the value
   * 2. strcmp-compare with the signature we built
   * 3. read the value
   * 4. type-iterate the signature and the value and see if they are the same type-wise
   */
  retval = FALSE;

  data_block_init_reader_writer (nid->block,
                                 &reader, &writer);

  /* DBusTypeWriter assumes it's writing into an existing signature,
   * so doesn't add nul on its own. We have to do that.
   */
  if (!_dbus_string_insert_byte (&nid->block->signature,
                                 nid->type_offset, '\0'))
    goto out;

  i = 0;
  while (i < nid->n_nodes)
    {
      if (!node_write_value (nid->nodes[i], nid->block, &writer, i))
        goto out;

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
      if (!node_read_value (nid->nodes[i], &reader, i))
        goto out;

      if (i + 1 == nid->n_nodes)
        NEXT_EXPECTING_FALSE (&reader);
      else
        NEXT_EXPECTING_TRUE (&reader);

      ++i;
    }

  if (n_iterations_expected_this_test <= MAX_ITERATIONS_FOR_EXPENSIVE_TESTS)
    {
      /* this set values test uses code from copy and
       * values_only_write so would ideally be last so you get a
       * simpler test case for problems with copying or values_only
       * writing; but it also needs an already-written DataBlock so it
       * has to go first. Comment it out if it breaks, and see if the
       * later tests also break - debug them first if so.
       */
      if (!run_test_set_values (nid))
        goto out;

      if (!run_test_delete_values (nid))
        goto out;

      if (!run_test_copy (nid))
        goto out;

      if (!run_test_values_only_write (nid))
        goto out;
    }

  /* FIXME type-iterate both signature and value and compare the resulting
   * tree to the node tree perhaps
   */

  retval = TRUE;

 out:

  data_block_reset (nid->block);

  return retval;
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

  if (!data_block_init (&block, byte_order, initial_offset))
    _dbus_assert_not_reached ("no memory");

  nid.signature = signature;
  nid.block = &block;
  nid.type_offset = initial_offset;
  nid.nodes = nodes;
  nid.n_nodes = n_nodes;

  if (TEST_OOM_HANDLING &&
      n_iterations_expected_this_test <= MAX_ITERATIONS_FOR_EXPENSIVE_TESTS)
    {
      _dbus_test_oom_handling ("running test node",
                               run_test_nodes_iteration,
                               &nid);
    }
  else
    {
      if (!run_test_nodes_iteration (&nid))
        _dbus_assert_not_reached ("no memory");
    }

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
  while (i <= MAX_INITIAL_OFFSET)
    {
      run_test_nodes_in_one_configuration (nodes, n_nodes, &signature,
                                           DBUS_LITTLE_ENDIAN, i);
      run_test_nodes_in_one_configuration (nodes, n_nodes, &signature,
                                           DBUS_BIG_ENDIAN, i);

      ++i;
    }

  n_iterations_completed_this_test += 1;
  n_iterations_completed_total += 1;

  if (n_iterations_completed_this_test == n_iterations_expected_this_test)
    {
      fprintf (stderr, " 100%% %d this test (%d cumulative)\n",
               n_iterations_completed_this_test,
               n_iterations_completed_total);
    }
  /* this happens to turn out well with mod == 1 */
  else if ((n_iterations_completed_this_test %
            (int)(n_iterations_expected_this_test / 10.0)) == 1)
    {
      fprintf (stderr, " %d%% ", (int) (n_iterations_completed_this_test / (double) n_iterations_expected_this_test * 100));
    }

  _dbus_string_free (&signature);
}

#define N_VALUES (N_BASICS * N_CONTAINERS + N_BASICS)

static TestTypeNode*
value_generator (int *ip)
{
  int i = *ip;
  const TestTypeNodeClass *child_klass;
  const TestTypeNodeClass *container_klass;
  TestTypeNode *child;
  TestTypeNode *node;

  _dbus_assert (i <= N_VALUES);

  if (i == N_VALUES)
    {
      return NULL;
    }
  else if (i < N_BASICS)
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
      i -= N_BASICS;

      container_klass = container_nodes[i / N_BASICS];
      child_klass = basic_nodes[i % N_BASICS];

      node = node_new (container_klass);
      child = node_new (child_klass);

      node_append_child (node, child);
    }

  *ip += 1; /* increment the generator */

  return node;
}

static void
make_and_run_values_inside_container (const TestTypeNodeClass *container_klass,
                                      int                      n_nested)
{
  TestTypeNode *root;
  TestTypeNode *container;
  TestTypeNode *child;
  int i;

  root = node_new (container_klass);
  container = root;
  for (i = 1; i < n_nested; i++)
    {
      child = node_new (container_klass);
      node_append_child (container, child);
      container = child;
    }

  /* container should now be the most-nested container */

  i = 0;
  while ((child = value_generator (&i)))
    {
      node_append_child (container, child);

      run_test_nodes (&root, 1);

      _dbus_list_clear (&((TestTypeNodeContainer*)container)->children);
      node_destroy (child);
    }

  node_destroy (root);
}

static void
start_next_test (const char *format,
                 int         expected)
{
  n_iterations_completed_this_test = 0;
  n_iterations_expected_this_test = expected;

  fprintf (stderr, ">>> >>> ");
  fprintf (stderr, format,
           n_iterations_expected_this_test);
}

static void
make_and_run_test_nodes (void)
{
  int i, j, k, m;

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
   * - strings of various lengths
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
  /* Each item is shown with its number of iterations to complete so
   * we can keep a handle on this unit test
   */

  /* FIXME test just an empty body, no types at all */

  start_next_test ("Each value by itself %d iterations\n", N_VALUES);
  {
    TestTypeNode *node;
    i = 0;
    while ((node = value_generator (&i)))
      {
        run_test_nodes (&node, 1);

        node_destroy (node);
      }
  }

  start_next_test ("Each value by itself with arrays as blocks %d iterations\n", N_VALUES);
  arrays_write_fixed_in_blocks = TRUE;
  {
    TestTypeNode *node;
    i = 0;
    while ((node = value_generator (&i)))
      {
        run_test_nodes (&node, 1);

        node_destroy (node);
      }
  }
  arrays_write_fixed_in_blocks = FALSE;

  start_next_test ("All values in one big toplevel %d iteration\n", 1);
  {
    TestTypeNode *nodes[N_VALUES];

    i = 0;
    while ((nodes[i] = value_generator (&i)))
      ;

    run_test_nodes (nodes, N_VALUES);

    for (i = 0; i < N_VALUES; i++)
      node_destroy (nodes[i]);
  }

  start_next_test ("Each value,value pair combination as toplevel, in both orders %d iterations\n",
                   N_VALUES * N_VALUES);
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

  start_next_test ("Each container containing each value %d iterations\n",
                   N_CONTAINERS * N_VALUES);
  for (i = 0; i < N_CONTAINERS; i++)
    {
      const TestTypeNodeClass *container_klass = container_nodes[i];

      make_and_run_values_inside_container (container_klass, 1);
    }

  start_next_test ("Each container containing each value with arrays as blocks %d iterations\n",
                   N_CONTAINERS * N_VALUES);
  arrays_write_fixed_in_blocks = TRUE;
  for (i = 0; i < N_CONTAINERS; i++)
    {
      const TestTypeNodeClass *container_klass = container_nodes[i];

      make_and_run_values_inside_container (container_klass, 1);
    }
  arrays_write_fixed_in_blocks = FALSE;

  start_next_test ("Each container of same container of each value %d iterations\n",
                   N_CONTAINERS * N_VALUES);
  for (i = 0; i < N_CONTAINERS; i++)
    {
      const TestTypeNodeClass *container_klass = container_nodes[i];

      make_and_run_values_inside_container (container_klass, 2);
    }

  start_next_test ("Each container of same container of same container of each value %d iterations\n",
                   N_CONTAINERS * N_VALUES);
  for (i = 0; i < N_CONTAINERS; i++)
    {
      const TestTypeNodeClass *container_klass = container_nodes[i];

      make_and_run_values_inside_container (container_klass, 3);
    }

  start_next_test ("Each value,value pair inside a struct %d iterations\n",
                   N_VALUES * N_VALUES);
  {
    TestTypeNode *val1, *val2;
    TestTypeNode *node;

    node = node_new (&struct_1_class);

    i = 0;
    while ((val1 = value_generator (&i)))
      {
        j = 0;
        while ((val2 = value_generator (&j)))
          {
            TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;

            node_append_child (node, val1);
            node_append_child (node, val2);

            run_test_nodes (&node, 1);

            _dbus_list_clear (&container->children);
            node_destroy (val2);
          }
        node_destroy (val1);
      }
    node_destroy (node);
  }

  start_next_test ("All values in one big struct %d iteration\n",
                   1);
  {
    TestTypeNode *node;
    TestTypeNode *child;

    node = node_new (&struct_1_class);

    i = 0;
    while ((child = value_generator (&i)))
      node_append_child (node, child);

    run_test_nodes (&node, 1);

    node_destroy (node);
  }

  start_next_test ("Each value in a large array %d iterations\n",
                   N_VALUES);
  {
    TestTypeNode *val;
    TestTypeNode *node;

    node = node_new (&array_9_class);

    i = 0;
    while ((val = value_generator (&i)))
      {
        TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;

        node_append_child (node, val);

        run_test_nodes (&node, 1);

        _dbus_list_clear (&container->children);
        node_destroy (val);
      }

    node_destroy (node);
  }

  start_next_test ("Each container of each container of each value %d iterations\n",
                   N_CONTAINERS * N_CONTAINERS * N_VALUES);
  for (i = 0; i < N_CONTAINERS; i++)
    {
      const TestTypeNodeClass *outer_container_klass = container_nodes[i];
      TestTypeNode *outer_container = node_new (outer_container_klass);

      for (j = 0; j < N_CONTAINERS; j++)
        {
          TestTypeNode *child;
          const TestTypeNodeClass *inner_container_klass = container_nodes[j];
          TestTypeNode *inner_container = node_new (inner_container_klass);

          node_append_child (outer_container, inner_container);

          m = 0;
          while ((child = value_generator (&m)))
            {
              node_append_child (inner_container, child);

              run_test_nodes (&outer_container, 1);

              _dbus_list_clear (&((TestTypeNodeContainer*)inner_container)->children);
              node_destroy (child);
            }
          _dbus_list_clear (&((TestTypeNodeContainer*)outer_container)->children);
          node_destroy (inner_container);
        }
      node_destroy (outer_container);
    }

  start_next_test ("Each container of each container of each container of each value %d iterations\n",
                   N_CONTAINERS * N_CONTAINERS * N_CONTAINERS * N_VALUES);
  for (i = 0; i < N_CONTAINERS; i++)
    {
      const TestTypeNodeClass *outer_container_klass = container_nodes[i];
      TestTypeNode *outer_container = node_new (outer_container_klass);

      for (j = 0; j < N_CONTAINERS; j++)
        {
          const TestTypeNodeClass *inner_container_klass = container_nodes[j];
          TestTypeNode *inner_container = node_new (inner_container_klass);

          node_append_child (outer_container, inner_container);

          for (k = 0; k < N_CONTAINERS; k++)
            {
              TestTypeNode *child;
              const TestTypeNodeClass *center_container_klass = container_nodes[k];
              TestTypeNode *center_container = node_new (center_container_klass);

              node_append_child (inner_container, center_container);

              m = 0;
              while ((child = value_generator (&m)))
                {
                  node_append_child (center_container, child);

                  run_test_nodes (&outer_container, 1);

                  _dbus_list_clear (&((TestTypeNodeContainer*)center_container)->children);
                  node_destroy (child);
                }
              _dbus_list_clear (&((TestTypeNodeContainer*)inner_container)->children);
              node_destroy (center_container);
            }
          _dbus_list_clear (&((TestTypeNodeContainer*)outer_container)->children);
          node_destroy (inner_container);
        }
      node_destroy (outer_container);
    }

#if 0
  /* This one takes a really long time, so comment it out for now */
  start_next_test ("Each value,value,value triplet combination as toplevel, in all orders %d iterations\n",
                   N_VALUES * N_VALUES * N_VALUES);
  {
    TestTypeNode *nodes[3];

    i = 0;
    while ((nodes[0] = value_generator (&i)))
      {
        j = 0;
        while ((nodes[1] = value_generator (&j)))
          {
            k = 0;
            while ((nodes[2] = value_generator (&k)))
              {
                run_test_nodes (nodes, 3);

                node_destroy (nodes[2]);
              }
            node_destroy (nodes[1]);
          }
        node_destroy (nodes[0]);
      }
  }
#endif /* #if 0 expensive test */

  fprintf (stderr, "%d total iterations of recursive marshaling tests\n",
           n_iterations_completed_total);
  fprintf (stderr, "each iteration ran at initial offsets 0 through %d in both big and little endian\n",
           MAX_INITIAL_OFFSET);
  fprintf (stderr, "out of memory handling %s tested\n",
           TEST_OOM_HANDLING ? "was" : "was not");
}

dbus_bool_t
_dbus_marshal_recursive_test (void)
{
  make_and_run_test_nodes ();

  return TRUE;
}

/*
 *
 *
 *         Implementations of each type node class
 *
 *
 *
 */
#define MAX_MULTI_COUNT 5


#define SAMPLE_INT32           12345678
#define SAMPLE_INT32_ALTERNATE 53781429
static dbus_int32_t
int32_from_seed (int seed)
{
  /* Generate an integer value that's predictable from seed.  We could
   * just use seed itself, but that would only ever touch one byte of
   * the int so would miss some kinds of bug.
   */
  dbus_int32_t v;

  v = 42; /* just to quiet compiler afaik */
  switch (seed % 5)
    {
    case 0:
      v = SAMPLE_INT32;
      break;
    case 1:
      v = SAMPLE_INT32_ALTERNATE;
      break;
    case 2:
      v = -1;
      break;
    case 3:
      v = _DBUS_INT_MAX;
      break;
    case 4:
      v = 1;
      break;
    }

  if (seed > 1)
    v *= seed; /* wraps around eventually, which is fine */

  return v;
}

static dbus_bool_t
int32_write_value (TestTypeNode   *node,
                   DataBlock      *block,
                   DBusTypeWriter *writer,
                   int             seed)
{
  /* also used for uint32 */
  dbus_int32_t v;

  v = int32_from_seed (seed);

  return _dbus_type_writer_write_basic (writer,
                                        node->klass->typecode,
                                        &v);
}

static dbus_bool_t
int32_read_value (TestTypeNode   *node,
                  DBusTypeReader *reader,
                  int             seed)
{
  /* also used for uint32 */
  dbus_int32_t v;

  check_expected_type (reader, node->klass->typecode);

  _dbus_type_reader_read_basic (reader,
                                (dbus_int32_t*) &v);

  _dbus_assert (v == int32_from_seed (seed));

  return TRUE;
}

static dbus_bool_t
int32_set_value (TestTypeNode   *node,
                 DBusTypeReader *reader,
                 DBusTypeReader *realign_root,
                 int             seed)
{
  /* also used for uint32 */
  dbus_int32_t v;

  v = int32_from_seed (seed);

  return _dbus_type_reader_set_basic (reader,
                                      &v,
                                      realign_root);
}

static dbus_bool_t
int32_write_multi (TestTypeNode   *node,
                   DataBlock      *block,
                   DBusTypeWriter *writer,
                   int             seed,
                   int             count)
{
  /* also used for uint32 */
  dbus_int32_t values[MAX_MULTI_COUNT];
  dbus_int32_t *v_ARRAY_INT32 = values;
  int i;

  for (i = 0; i < count; ++i)
    values[i] = int32_from_seed (seed + i);

  return _dbus_type_writer_write_fixed_multi (writer,
                                              node->klass->typecode,
                                              &v_ARRAY_INT32, count);
}

static dbus_bool_t
int32_read_multi (TestTypeNode   *node,
                  DBusTypeReader *reader,
                  int             seed,
                  int             count)
{
  /* also used for uint32 */
  dbus_int32_t *values;
  int n_elements;
  int i;

  check_expected_type (reader, node->klass->typecode);

  _dbus_type_reader_read_fixed_multi (reader,
                                      &values,
                                      &n_elements);

  if (n_elements != count)
    _dbus_warn ("got %d elements expected %d\n", n_elements, count);
  _dbus_assert (n_elements == count);

  for (i = 0; i < count; i++)
    _dbus_assert (_dbus_unpack_int32 (reader->byte_order,
                                      (const unsigned char*)values + (i * 4)) ==
                  int32_from_seed (seed + i));

  return TRUE;
}

#ifdef DBUS_HAVE_INT64
static dbus_int64_t
int64_from_seed (int seed)
{
  dbus_int32_t v32;
  dbus_int64_t v;

  v32 = int32_from_seed (seed);

  v = - (dbus_int32_t) ~ v32;
  v |= (((dbus_int64_t)v32) << 32);

  return v;
}
#endif

static dbus_bool_t
int64_write_value (TestTypeNode   *node,
                   DataBlock      *block,
                   DBusTypeWriter *writer,
                   int             seed)
{
#ifdef DBUS_HAVE_INT64
  /* also used for uint64 */
  dbus_int64_t v;

  v = int64_from_seed (seed);

  return _dbus_type_writer_write_basic (writer,
                                        node->klass->typecode,
                                        &v);
#else
  return TRUE;
#endif
}

static dbus_bool_t
int64_read_value (TestTypeNode   *node,
                  DBusTypeReader *reader,
                  int             seed)
{
#ifdef DBUS_HAVE_INT64
  /* also used for uint64 */
  dbus_int64_t v;

  check_expected_type (reader, node->klass->typecode);

  _dbus_type_reader_read_basic (reader,
                                (dbus_int64_t*) &v);

  _dbus_assert (v == int64_from_seed (seed));

  return TRUE;
#else
  return TRUE;
#endif
}

static dbus_bool_t
int64_set_value (TestTypeNode   *node,
                 DBusTypeReader *reader,
                 DBusTypeReader *realign_root,
                 int             seed)
{
#ifdef DBUS_HAVE_INT64
  /* also used for uint64 */
  dbus_int64_t v;

  v = int64_from_seed (seed);

  return _dbus_type_reader_set_basic (reader,
                                      &v,
                                      realign_root);
#else
  return TRUE;
#endif
}

#define MAX_SAMPLE_STRING_LEN 10
static void
string_from_seed (char *buf,
                  int   len,
                  int   seed)
{
  int i;
  unsigned char v;

  _dbus_assert (len < MAX_SAMPLE_STRING_LEN);

  /* vary the length slightly, though we also have multiple string
   * value types for this, varying it here tests the set_value code
   */
  switch (seed % 3)
    {
    case 1:
      len += 2;
      break;
    case 2:
      len -= 2;
      break;
    }
  if (len < 0)
    len = 0;

  v = (unsigned char) ('A' + seed);

  i = 0;
  while (i < len)
    {
      if (v < 'A' || v > 'z')
        v = 'A';

      buf[i] = v;

      v += 1;
      ++i;
    }

  buf[i] = '\0';
}

static dbus_bool_t
string_write_value (TestTypeNode   *node,
                    DataBlock      *block,
                    DBusTypeWriter *writer,
                    int             seed)
{
  char buf[MAX_SAMPLE_STRING_LEN];
  const char *v_string = buf;

  string_from_seed (buf, node->klass->subclass_detail,
                    seed);

  return _dbus_type_writer_write_basic (writer,
                                        node->klass->typecode,
                                        &v_string);
}

static dbus_bool_t
string_read_value (TestTypeNode   *node,
                   DBusTypeReader *reader,
                   int             seed)
{
  const char *v;
  char buf[MAX_SAMPLE_STRING_LEN];

  check_expected_type (reader, node->klass->typecode);

  _dbus_type_reader_read_basic (reader,
                                (const char **) &v);

  string_from_seed (buf, node->klass->subclass_detail,
                    seed);

  if (strcmp (buf, v) != 0)
    {
      _dbus_warn ("read string '%s' expected '%s'\n",
                  v, buf);
      _dbus_assert_not_reached ("test failed");
    }

  return TRUE;
}

static dbus_bool_t
string_set_value (TestTypeNode   *node,
                  DBusTypeReader *reader,
                  DBusTypeReader *realign_root,
                  int             seed)
{
  char buf[MAX_SAMPLE_STRING_LEN];
  const char *v_string = buf;

  string_from_seed (buf, node->klass->subclass_detail,
                    seed);

#if RECURSIVE_MARSHAL_WRITE_TRACE
 {
   const char *old;
   _dbus_type_reader_read_basic (reader, &old);
   _dbus_verbose ("SETTING new string '%s' len %d in place of '%s' len %d\n",
                  v_string, strlen (v_string), old, strlen (old));
 }
#endif

  return _dbus_type_reader_set_basic (reader,
                                      &v_string,
                                      realign_root);
}

#define BOOL_FROM_SEED(seed) (seed % 2)

static dbus_bool_t
bool_write_value (TestTypeNode   *node,
                  DataBlock      *block,
                  DBusTypeWriter *writer,
                  int             seed)
{
  unsigned char v;

  v = BOOL_FROM_SEED (seed);

  return _dbus_type_writer_write_basic (writer,
                                        node->klass->typecode,
                                        &v);
}

static dbus_bool_t
bool_read_value (TestTypeNode   *node,
                 DBusTypeReader *reader,
                 int             seed)
{
  unsigned char v;

  check_expected_type (reader, node->klass->typecode);

  _dbus_type_reader_read_basic (reader,
                                (unsigned char*) &v);

  _dbus_assert (v == BOOL_FROM_SEED (seed));

  return TRUE;
}

static dbus_bool_t
bool_set_value (TestTypeNode   *node,
                DBusTypeReader *reader,
                DBusTypeReader *realign_root,
                int             seed)
{
  unsigned char v;

  v = BOOL_FROM_SEED (seed);

  return _dbus_type_reader_set_basic (reader,
                                      &v,
                                      realign_root);
}

#define BYTE_FROM_SEED(seed) ((unsigned char) int32_from_seed (seed))

static dbus_bool_t
byte_write_value (TestTypeNode   *node,
                  DataBlock      *block,
                  DBusTypeWriter *writer,
                  int             seed)
{
  unsigned char v;

  v = BYTE_FROM_SEED (seed);

  return _dbus_type_writer_write_basic (writer,
                                        node->klass->typecode,
                                        &v);
}

static dbus_bool_t
byte_read_value (TestTypeNode   *node,
                 DBusTypeReader *reader,
                 int             seed)
{
  unsigned char v;

  check_expected_type (reader, node->klass->typecode);

  _dbus_type_reader_read_basic (reader,
                                (unsigned char*) &v);

  _dbus_assert (v == BYTE_FROM_SEED (seed));

  return TRUE;
}


static dbus_bool_t
byte_set_value (TestTypeNode   *node,
                DBusTypeReader *reader,
                DBusTypeReader *realign_root,
                int             seed)
{
  unsigned char v;

  v = BYTE_FROM_SEED (seed);

  return _dbus_type_reader_set_basic (reader,
                                      &v,
                                      realign_root);
}

static double
double_from_seed (int seed)
{
  return SAMPLE_INT32 * (double) seed + 0.3;
}

static dbus_bool_t
double_write_value (TestTypeNode   *node,
                    DataBlock      *block,
                    DBusTypeWriter *writer,
                    int             seed)
{
  double v;

  v = double_from_seed (seed);

  return _dbus_type_writer_write_basic (writer,
                                        node->klass->typecode,
                                        &v);
}

static dbus_bool_t
double_read_value (TestTypeNode   *node,
                   DBusTypeReader *reader,
                   int             seed)
{
  double v;
  double expected;

  check_expected_type (reader, node->klass->typecode);

  _dbus_type_reader_read_basic (reader,
                                (double*) &v);

  expected = double_from_seed (seed);

  if (!_DBUS_DOUBLES_BITWISE_EQUAL (v, expected))
    {
#ifdef DBUS_HAVE_INT64
      _dbus_warn ("Expected double %g got %g\n bits = 0x%llx vs.\n bits = 0x%llx)\n",
                  expected, v,
                  *(dbus_uint64_t*)(char*)&expected,
                  *(dbus_uint64_t*)(char*)&v);
#endif
      _dbus_assert_not_reached ("test failed");
    }

  return TRUE;
}

static dbus_bool_t
double_set_value (TestTypeNode   *node,
                DBusTypeReader *reader,
                DBusTypeReader *realign_root,
                int             seed)
{
  double v;

  v = double_from_seed (seed);

  return _dbus_type_reader_set_basic (reader,
                                      &v,
                                      realign_root);
}

#define MAX_SAMPLE_OBJECT_PATH_LEN 10
static void
object_path_from_seed (char *buf,
                       int   seed)
{
  int i;
  unsigned char v;
  int len;

  len = seed % 9;
  _dbus_assert (len < MAX_SAMPLE_OBJECT_PATH_LEN);

  v = (unsigned char) ('A' + seed);

  i = 0;
  while (i + 1 < len)
    {
      if (v < 'A' || v > 'z')
        v = 'A';

      buf[i] = '/';
      ++i;
      buf[i] = v;
      ++i;

      v += 1;
    }

  buf[i] = '\0';
}

static dbus_bool_t
object_path_write_value (TestTypeNode   *node,
                         DataBlock      *block,
                         DBusTypeWriter *writer,
                         int             seed)
{
  char buf[MAX_SAMPLE_OBJECT_PATH_LEN];
  const char *v_string = buf;

  object_path_from_seed (buf, seed);

  return _dbus_type_writer_write_basic (writer,
                                        node->klass->typecode,
                                        &v_string);
}

static dbus_bool_t
object_path_read_value (TestTypeNode   *node,
                        DBusTypeReader *reader,
                        int             seed)
{
  const char *v;
  char buf[MAX_SAMPLE_OBJECT_PATH_LEN];

  check_expected_type (reader, node->klass->typecode);

  _dbus_type_reader_read_basic (reader,
                                (const char **) &v);

  object_path_from_seed (buf, seed);

  if (strcmp (buf, v) != 0)
    {
      _dbus_warn ("read object path '%s' expected '%s'\n",
                  v, buf);
      _dbus_assert_not_reached ("test failed");
    }

  return TRUE;
}

static dbus_bool_t
object_path_set_value (TestTypeNode   *node,
                       DBusTypeReader *reader,
                       DBusTypeReader *realign_root,
                       int             seed)
{
  char buf[MAX_SAMPLE_OBJECT_PATH_LEN];
  const char *v_string = buf;

  object_path_from_seed (buf, seed);

  return _dbus_type_reader_set_basic (reader,
                                      &v_string,
                                      realign_root);
}

#define MAX_SAMPLE_SIGNATURE_LEN 10
static void
signature_from_seed (char *buf,
                     int   seed)
{
  int i;
  const char *s;
  /* try to avoid ascending, descending, or alternating length to help find bugs */
  const char *sample_signatures[] = {
    "asax"
    "",
    "asau(xxxx)",
    "x",
    "ai",
    "a(ii)"
  };

  s = sample_signatures[seed % _DBUS_N_ELEMENTS(sample_signatures)];

  for (i = 0; s[i]; i++)
    {
      buf[i] = s[i];
    }
  buf[i] = '\0';
}

static dbus_bool_t
signature_write_value (TestTypeNode   *node,
                       DataBlock      *block,
                       DBusTypeWriter *writer,
                       int             seed)
{
  char buf[MAX_SAMPLE_SIGNATURE_LEN];
  const char *v_string = buf;

  signature_from_seed (buf, seed);

  return _dbus_type_writer_write_basic (writer,
                                        node->klass->typecode,
                                        &v_string);
}

static dbus_bool_t
signature_read_value (TestTypeNode   *node,
                      DBusTypeReader *reader,
                      int             seed)
{
  const char *v;
  char buf[MAX_SAMPLE_SIGNATURE_LEN];

  check_expected_type (reader, node->klass->typecode);

  _dbus_type_reader_read_basic (reader,
                                (const char **) &v);

  signature_from_seed (buf, seed);

  if (strcmp (buf, v) != 0)
    {
      _dbus_warn ("read signature value '%s' expected '%s'\n",
                  v, buf);
      _dbus_assert_not_reached ("test failed");
    }

  return TRUE;
}


static dbus_bool_t
signature_set_value (TestTypeNode   *node,
                     DBusTypeReader *reader,
                     DBusTypeReader *realign_root,
                     int             seed)
{
  char buf[MAX_SAMPLE_SIGNATURE_LEN];
  const char *v_string = buf;

  signature_from_seed (buf, seed);

  return _dbus_type_reader_set_basic (reader,
                                      &v_string,
                                      realign_root);
}

static dbus_bool_t
struct_write_value (TestTypeNode   *node,
                    DataBlock      *block,
                    DBusTypeWriter *writer,
                    int             seed)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  DataBlockState saved;
  DBusTypeWriter sub;
  int i;
  int n_copies;

  n_copies = node->klass->subclass_detail;

  _dbus_assert (container->children != NULL);

  data_block_save (block, &saved);

  if (!_dbus_type_writer_recurse (writer, DBUS_TYPE_STRUCT,
                                  NULL, 0,
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

          if (!node_write_value (child, block, &sub, seed + i))
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
struct_read_or_set_value (TestTypeNode   *node,
                          DBusTypeReader *reader,
                          DBusTypeReader *realign_root,
                          int             seed)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  DBusTypeReader sub;
  int i;
  int n_copies;

  n_copies = node->klass->subclass_detail;

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

          if (realign_root == NULL)
            {
              if (!node_read_value (child, &sub, seed + i))
                return FALSE;
            }
          else
            {
              if (!node_set_value (child, &sub, realign_root, seed + i))
                return FALSE;
            }

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
struct_read_value (TestTypeNode   *node,
                   DBusTypeReader *reader,
                   int             seed)
{
  return struct_read_or_set_value (node, reader, NULL, seed);
}

static dbus_bool_t
struct_set_value (TestTypeNode   *node,
                  DBusTypeReader *reader,
                  DBusTypeReader *realign_root,
                  int             seed)
{
  return struct_read_or_set_value (node, reader, realign_root, seed);
}

static dbus_bool_t
struct_build_signature (TestTypeNode   *node,
                        DBusString     *str)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  int i;
  int orig_len;
  int n_copies;

  n_copies = node->klass->subclass_detail;

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
array_write_value (TestTypeNode   *node,
                   DataBlock      *block,
                   DBusTypeWriter *writer,
                   int             seed)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  DataBlockState saved;
  DBusTypeWriter sub;
  DBusString element_signature;
  int i;
  int n_copies;
  int element_type;
  TestTypeNode *child;

  n_copies = node->klass->subclass_detail;

  _dbus_assert (container->children != NULL);

  data_block_save (block, &saved);

  if (!_dbus_string_init (&element_signature))
    return FALSE;

  child = _dbus_list_get_first (&container->children);

  if (!node_build_signature (child,
                             &element_signature))
    goto oom;

  element_type = first_type_in_signature (&element_signature, 0);

  if (!_dbus_type_writer_recurse (writer, DBUS_TYPE_ARRAY,
                                  &element_signature, 0,
                                  &sub))
    goto oom;

  if (arrays_write_fixed_in_blocks &&
      _dbus_type_is_fixed (element_type) &&
      child->klass->write_multi)
    {
      if (!node_write_multi (child, block, &sub, seed, n_copies))
        goto oom;
    }
  else
    {
      i = 0;
      while (i < n_copies)
        {
          DBusList *link;

          link = _dbus_list_get_first_link (&container->children);
          while (link != NULL)
            {
              TestTypeNode *child = link->data;
              DBusList *next = _dbus_list_get_next_link (&container->children, link);

              if (!node_write_value (child, block, &sub, seed + i))
                goto oom;

              link = next;
            }

          ++i;
        }
    }

  if (!_dbus_type_writer_unrecurse (writer, &sub))
    goto oom;

  _dbus_string_free (&element_signature);
  return TRUE;

 oom:
  data_block_restore (block, &saved);
  _dbus_string_free (&element_signature);
  return FALSE;
}

static dbus_bool_t
array_read_or_set_value (TestTypeNode   *node,
                         DBusTypeReader *reader,
                         DBusTypeReader *realign_root,
                         int             seed)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  DBusTypeReader sub;
  int i;
  int n_copies;
  TestTypeNode *child;

  n_copies = node->klass->subclass_detail;

  check_expected_type (reader, DBUS_TYPE_ARRAY);

  child = _dbus_list_get_first (&container->children);

  if (n_copies > 0)
    {
      _dbus_assert (!_dbus_type_reader_array_is_empty (reader));

      _dbus_type_reader_recurse (reader, &sub);

      if (realign_root == NULL && arrays_write_fixed_in_blocks &&
          _dbus_type_is_fixed (_dbus_type_reader_get_element_type (reader)) &&
          child->klass->read_multi)
        {
          if (!node_read_multi (child, &sub, seed, n_copies))
            return FALSE;
        }
      else
        {
          i = 0;
          while (i < n_copies)
            {
              DBusList *link;

              link = _dbus_list_get_first_link (&container->children);
              while (link != NULL)
                {
                  TestTypeNode *child = link->data;
                  DBusList *next = _dbus_list_get_next_link (&container->children, link);

                  _dbus_assert (child->klass->typecode ==
                                _dbus_type_reader_get_element_type (reader));

                  if (realign_root == NULL)
                    {
                      if (!node_read_value (child, &sub, seed + i))
                        return FALSE;
                    }
                  else
                    {
                      if (!node_set_value (child, &sub, realign_root, seed + i))
                        return FALSE;
                    }

                  if (i == (n_copies - 1) && next == NULL)
                    NEXT_EXPECTING_FALSE (&sub);
                  else
                    NEXT_EXPECTING_TRUE (&sub);

                  link = next;
                }

              ++i;
            }
        }
    }
  else
    {
      _dbus_assert (_dbus_type_reader_array_is_empty (reader));
    }

  return TRUE;
}

static dbus_bool_t
array_read_value (TestTypeNode   *node,
                  DBusTypeReader *reader,
                  int             seed)
{
  return array_read_or_set_value (node, reader, NULL, seed);
}

static dbus_bool_t
array_set_value (TestTypeNode   *node,
                 DBusTypeReader *reader,
                 DBusTypeReader *realign_root,
                 int             seed)
{
  return array_read_or_set_value (node, reader, realign_root, seed);
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

 /* 10 is random just to add another seed that we use in the suite */
#define VARIANT_SEED 10

static dbus_bool_t
variant_write_value (TestTypeNode   *node,
                     DataBlock      *block,
                     DBusTypeWriter *writer,
                     int             seed)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  DataBlockState saved;
  DBusTypeWriter sub;
  DBusString content_signature;
  TestTypeNode *child;

  _dbus_assert (container->children != NULL);
  _dbus_assert (_dbus_list_length_is_one (&container->children));

  child = _dbus_list_get_first (&container->children);

  data_block_save (block, &saved);

  if (!_dbus_string_init (&content_signature))
    return FALSE;

  if (!node_build_signature (child,
                             &content_signature))
    goto oom;

  if (!_dbus_type_writer_recurse (writer, DBUS_TYPE_VARIANT,
                                  &content_signature, 0,
                                  &sub))
    goto oom;

  if (!node_write_value (child, block, &sub, seed + VARIANT_SEED))
    goto oom;

  if (!_dbus_type_writer_unrecurse (writer, &sub))
    goto oom;

  _dbus_string_free (&content_signature);
  return TRUE;

 oom:
  data_block_restore (block, &saved);
  _dbus_string_free (&content_signature);
  return FALSE;
}

static dbus_bool_t
variant_read_or_set_value (TestTypeNode   *node,
                           DBusTypeReader *reader,
                           DBusTypeReader *realign_root,
                           int             seed)
{
  TestTypeNodeContainer *container = (TestTypeNodeContainer*) node;
  DBusTypeReader sub;
  TestTypeNode *child;

  _dbus_assert (container->children != NULL);
  _dbus_assert (_dbus_list_length_is_one (&container->children));

  child = _dbus_list_get_first (&container->children);

  check_expected_type (reader, DBUS_TYPE_VARIANT);

  _dbus_type_reader_recurse (reader, &sub);

  if (realign_root == NULL)
    {
      if (!node_read_value (child, &sub, seed + VARIANT_SEED))
        return FALSE;
    }
  else
    {
      if (!node_set_value (child, &sub, realign_root, seed + VARIANT_SEED))
        return FALSE;
    }

  NEXT_EXPECTING_FALSE (&sub);

  return TRUE;
}

static dbus_bool_t
variant_read_value (TestTypeNode   *node,
                    DBusTypeReader *reader,
                    int             seed)
{
  return variant_read_or_set_value (node, reader, NULL, seed);
}

static dbus_bool_t
variant_set_value (TestTypeNode   *node,
                   DBusTypeReader *reader,
                   DBusTypeReader *realign_root,
                   int             seed)
{
  return variant_read_or_set_value (node, reader, realign_root, seed);
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
