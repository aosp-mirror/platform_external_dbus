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

  _dbus_verbose ("type reader %p init type_pos = %d value_pos = %d remaining sig '%s'\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));
}

int
_dbus_type_reader_get_current_type (DBusTypeReader *reader)
{
  int t;

  t = _dbus_string_get_byte (reader->type_str,
                             reader->type_pos);

  if (t == DBUS_STRUCT_BEGIN_CHAR)
    t = DBUS_TYPE_STRUCT;

  /* this should never be a stopping place */
  _dbus_assert (t != DBUS_STRUCT_END_CHAR);

#if 0
  _dbus_verbose ("type reader %p current type_pos = %d type = %s\n",
                 reader, reader->type_pos,
                 _dbus_type_to_string (t));
#endif
  
  return t;
}

int
_dbus_type_reader_get_array_type (DBusTypeReader *reader)
{
  int t;

  t = _dbus_type_reader_get_current_type (reader);

  if (t != DBUS_TYPE_ARRAY)
    return DBUS_TYPE_INVALID;

  t = _dbus_string_get_byte (reader->type_str,
                             reader->type_pos + 1);  
  
  return t;
}

void
_dbus_type_reader_read_basic (DBusTypeReader    *reader,
                              void              *value)
{
  int t;
  int next;

  t = _dbus_type_reader_get_current_type (reader);

  next = reader->value_pos;
  _dbus_demarshal_basic_type (reader->value_str,
                              t, value,
                              reader->byte_order,
                              &next);

  _dbus_verbose ("type reader %p read basic type_pos = %d value_pos = %d next = %d remaining sig '%s'\n",
                 reader, reader->type_pos, reader->value_pos, next,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));

  _dbus_verbose_bytes_of_string (reader->value_str,
                                 reader->value_pos,
                                 MIN (16,
                                      _dbus_string_get_length (reader->value_str) - reader->value_pos));
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

  t = _dbus_string_get_byte (reader->type_str, reader->type_pos);
  
  /* point subreader at the same place as reader */
  _dbus_type_reader_init (sub,
                          reader->byte_order,
                          reader->type_str,
                          reader->type_pos,
                          reader->value_str,
                          reader->value_pos);

  _dbus_assert (t == DBUS_STRUCT_BEGIN_CHAR); /* only this works right now */
  
  sub->type_pos += 1;

  /* no value_pos increment since the struct itself doesn't take up value space */

  _dbus_verbose ("type reader %p recursed type_pos = %d value_pos = %d remaining sig '%s'\n",
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

  /* FIXME handled calling next when there's no next */
  
  t = _dbus_string_get_byte (reader->type_str, reader->type_pos);
  
  _dbus_verbose ("type reader %p next() { type_pos = %d value_pos = %d remaining sig '%s'\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));
  
  switch (t)
    {
    case DBUS_STRUCT_BEGIN_CHAR:
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
        reader->value_pos = sub.value_pos;
      }
      break;

    default:
      /* FIXME for array etc. this is more complicated */
      _dbus_marshal_skip_basic_type (reader->value_str,
                                     t, reader->byte_order,
                                     &reader->value_pos);
      reader->type_pos += 1;
      break;
    }

  _dbus_verbose ("type reader %p }  type_pos = %d value_pos = %d remaining sig '%s'\n",
                 reader, reader->type_pos, reader->value_pos,
                 _dbus_string_get_const_data_len (reader->type_str, reader->type_pos, 0));

  /* FIXME this is wrong; we need to return FALSE when we finish the
   * container we've recursed into; even if the signature continues.
   */
  
  t = _dbus_string_get_byte (reader->type_str, reader->type_pos);

  if (t == DBUS_STRUCT_END_CHAR)
    {
      reader->type_pos += 1;
      return FALSE;
    }
  if (t == DBUS_TYPE_INVALID)
    return FALSE;
  
  return TRUE;
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
}

dbus_bool_t
_dbus_type_writer_write_basic (DBusTypeWriter *writer,
                               int             type,
                               const void     *value)
{
  dbus_bool_t retval;
  int old_value_len;

  old_value_len = _dbus_string_get_length (writer->value_str);
  
  /* First ensure that our type realloc will succeed */
  if (!_dbus_string_alloc_space (writer->type_str, 1))
    return FALSE;

  retval = FALSE;
        
  if (!_dbus_marshal_basic_type (writer->value_str,
                                 writer->value_pos,
                                 type,
                                 value,
                                 writer->byte_order))
    goto out;

  writer->value_pos += _dbus_string_get_length (writer->value_str) - old_value_len;
  
  /* Now insert the type */
  if (!_dbus_string_insert_byte (writer->type_str,
                                 writer->type_pos,
                                 type))
    _dbus_assert_not_reached ("failed to insert byte after prealloc");

  writer->type_pos += 1;
  
  retval = TRUE;
  
 out:
  return retval;
}

dbus_bool_t
_dbus_type_writer_write_array (DBusTypeWriter *writer,
                               int             type,
                               const void     *array,
                               int             array_len)
{


}

dbus_bool_t
_dbus_type_writer_recurse (DBusTypeWriter *writer,
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
  
  switch (container_type)
    {
    case DBUS_TYPE_STRUCT:
      {
        if (!_dbus_string_insert_byte (sub->type_str,
                                       sub->type_pos,
                                       DBUS_STRUCT_BEGIN_CHAR))
          return FALSE;

        sub->type_pos += 1;
      }
      break;
    default:
      _dbus_assert_not_reached ("container_type unhandled");
      break;
    }
  
  return TRUE;
}

dbus_bool_t
_dbus_type_writer_unrecurse (DBusTypeWriter *writer,
                             DBusTypeWriter *sub)
{
  _dbus_assert (sub->type_pos > 0); /* can't be recursed if this fails */

  if (sub->container_type == DBUS_TYPE_STRUCT)
    {
      if (!_dbus_string_insert_byte (sub->type_str,
                                     sub->type_pos, 
                                     DBUS_STRUCT_END_CHAR))
        return FALSE;
      sub->type_pos += 1;
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

typedef enum {
  ITEM_INVALID = -1,
  ITEM_INT32 = 0,
  ITEM_STRUCT_WITH_INT32S,
  ITEM_STRUCT_OF_STRUCTS,
  ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS,
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
    read_struct_of_structs_of_structs }
};

typedef struct
{
  /* Array of items in the above items[]; -1 terminated */
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
  { { ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_STRUCT_OF_STRUCTS_OF_STRUCTS, ITEM_INVALID } }
  
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

  data_block_save (block, &saved);
  
  data_block_init_reader_writer (block, 
                                 byte_order,
                                 &reader, &writer);

  i = 0;
  while (run->items[i] != ITEM_INVALID)
    {
      CheckMarshalItem *item = &items[run->items[i]];

      _dbus_verbose ("writing %s\n", item->desc);
      
      if (!(* item->write_item_func) (block, &writer))
        goto out;
      ++i;
    }

  i = 0;
  while (run->items[i] != ITEM_INVALID)
    {
      CheckMarshalItem *item = &items[run->items[i]];

      _dbus_verbose ("reading %s\n", item->desc);
      
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
