/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message-builder.c Build messages from text files for testing (internal to D-BUS implementation)
 * 
 * Copyright (C) 2003 Red Hat, Inc.
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
#include <config.h>

#ifdef DBUS_BUILD_TESTS

#include "dbus-message-builder.h"
#include "dbus-hash.h"
#include "dbus-internals.h"
#include "dbus-marshal.h"

/**
 * @defgroup DBusMessageBuilder code for loading test message data
 * @ingroup  DBusInternals
 * @brief code for loading up test data for unit tests
 *
 * The code in here is used for unit testing, it loads
 * up message data from a description in a file.
 *
 * @{
 */

static dbus_bool_t
pop_line (DBusString *source,
          DBusString *dest)
{
  int eol;
  
  _dbus_string_set_length (dest, 0);
  
  eol = 0;
  if (_dbus_string_find (source, 0, "\n", &eol))
    eol += 1; /* include newline */
  else
    eol = _dbus_string_get_length (source);

  if (eol == 0)
    {
      _dbus_verbose ("no more data in file\n");
      return FALSE;
    }
  
  if (!_dbus_string_move_len (source, 0, eol,
                              dest, 0))
    {
      _dbus_warn ("failed to pop line\n");
      return FALSE;
    }

  /* dump the newline */
  _dbus_string_set_length (dest,
                           _dbus_string_get_length (dest) - 1);
  
  return TRUE;
}

static void
strip_command_name (DBusString *str)
{
  int i;
  
  i = 0;
  if (_dbus_string_find_blank (str, 0, &i))
    _dbus_string_skip_blank (str, i, &i);

  _dbus_string_delete (str, 0, i);
}

static void
strip_leading_space (DBusString *str)
{
  int i;
  
  i = 0;
  _dbus_string_skip_blank (str, 0, &i);

  if (i > 0)
    _dbus_string_delete (str, 0, i);
}

typedef struct
{
  DBusString name;
  int length; /**< length to write */
  int offset; /**< where to write it into the data */
  int endian; /**< endianness to write with */
} SavedLength;

static void
free_saved_length (void *data)
{
  SavedLength *sl = data;

  _dbus_string_free (&sl->name);
  dbus_free (sl);
}

static SavedLength*
ensure_saved_length (DBusHashTable    *hash,
                     const DBusString *name)
{
  SavedLength *sl;
  const char *s;

  _dbus_string_get_const_data (name, &s);

  sl = _dbus_hash_table_lookup_string (hash, s);
  if (sl != NULL)
    return sl;
  
  sl = dbus_new0 (SavedLength, 1);

  if (!_dbus_string_init (&sl->name, _DBUS_INT_MAX))
    {
      dbus_free (sl);
      return NULL;
    }

  if (!_dbus_string_copy (name, 0, &sl->name, 0))
    goto failed;

  _dbus_string_get_const_data (&sl->name, &s);

  if (!_dbus_hash_table_insert_string (hash, (char*)s, sl))
    goto failed;

  sl->length = -1;
  sl->offset = -1;
  sl->endian = -1;
  
  return sl;
  
 failed:
  free_saved_length (sl);
  return NULL;
}

static dbus_bool_t
save_length (DBusHashTable    *hash,
             const DBusString *name,
             int               length)
{
  SavedLength *sl;

  sl = ensure_saved_length (hash, name);

  if (sl == NULL)
    return FALSE;
  else if (sl->length >= 0)
    {
      _dbus_warn ("Same SAVE_LENGTH given twice\n");
      return FALSE;
    }
  else
    sl->length = length;

  return TRUE;
}

static dbus_bool_t
save_offset (DBusHashTable    *hash,
             const DBusString *name,
             int               offset,
             int               endian)
{
  SavedLength *sl;

  sl = ensure_saved_length (hash, name);

  if (sl == NULL)
    return FALSE;
  else if (sl->offset >= 0)
    {
      _dbus_warn ("Same LENGTH given twice\n");
      return FALSE;
    }
  else
    {
      sl->offset = offset;
      sl->endian = endian;
    }

  return TRUE;
}

/** Saves the segment to delete in order to unalign the next item */
#define SAVE_FOR_UNALIGN(str, boundary)                                 \
  int align_pad_start = _dbus_string_get_length (str);                  \
  int align_pad_end = _DBUS_ALIGN_VALUE (align_pad_start, (boundary))

/** Deletes the alignment padding */
#define PERFORM_UNALIGN(str)                                    \
  if (unalign)                                                  \
    {                                                           \
      _dbus_string_delete ((str), align_pad_start,              \
                           align_pad_end - align_pad_start);    \
      unalign = FALSE;                                          \
    }


static dbus_bool_t
append_quoted_string (DBusString       *dest,
                      const DBusString *quoted)
{
  dbus_bool_t in_quotes = FALSE;
  int i;

  i = 0;
  while (i < _dbus_string_get_length (quoted))
    {
      unsigned char b;

      b = _dbus_string_get_byte (quoted, i);
      
      if (in_quotes)
        {
          if (b == '\'')
            in_quotes = FALSE;
          else
            {
              if (!_dbus_string_append_byte (dest, b))
                return FALSE;
            }
        }
      else
        {
          if (b == '\'')
            in_quotes = TRUE;
          else if (b == ' ' || b == '\n' || b == '\t')
            break; /* end on whitespace if not quoted */
          else
            {
              if (!_dbus_string_append_byte (dest, b))
                return FALSE;
            }
        }
      
      ++i;
    }

  if (!_dbus_string_append_byte (dest, '\0'))
    return FALSE;
  return TRUE;
}

static dbus_bool_t
append_saved_length (DBusString       *dest,
                     DBusHashTable    *length_hash,
                     const DBusString *name,
                     int               offset,
                     int               endian)
{
  if (!save_offset (length_hash, name,
                    offset, endian))
    {
      _dbus_warn ("failed to save offset to LENGTH\n");
      return FALSE;
    }
  
  if (!_dbus_marshal_int32 (dest, endian,
                            -1))
    {
      _dbus_warn ("failed to append a length\n");
      return FALSE;
    }

  return TRUE;
}

/**
 * Reads the given filename, which should be in "message description
 * language" (look at some examples), and builds up the message data
 * from it.  The message data may be invalid, or valid.
 *
 * The parser isn't very strict, it's just a hack for test programs.
 * 
 * The file format is:
 * @code
 *   VALID_HEADER normal header; byte order, padding, header len, body len, serial
 *   BIG_ENDIAN switch to big endian
 *   LITTLE_ENDIAN switch to little endian
 *   OPPOSITE_ENDIAN switch to opposite endian
 *   ALIGN <N> aligns to the given value
 *   UNALIGN skips alignment for the next marshal
 *   BYTE <N> inserts the given integer in [0,255] or char in 'a' format
 *   SAVE_LENGTH <name> records the current length under the given name
 *   LENGTH <name> inserts the saved length of the same name
 *   CHOP <N> chops last N bytes off the data
 *   FIELD_NAME <abcd> inserts 4-byte field name
 *   TYPE <typename> inserts a typecode byte 
 * @endcode
 * 
 * Following commands insert aligned data unless
 * preceded by "UNALIGN":
 * @code
 *   INT32 <N> marshals an INT32
 *   UINT32 <N> marshals a UINT32
 *   DOUBLE <N> marshals a double
 *   STRING 'Foo' marshals a string
 * @endcode
 *
 * @todo add support for array types INT32_ARRAY { 3, 4, 5, 6 }
 * and so forth.
 * 
 * @param dest the string to append the message data to
 * @param filename the filename to load
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_message_data_load (DBusString       *dest,
                         const DBusString *filename)
{
  DBusString file;
  DBusResultCode result;
  DBusString line;
  dbus_bool_t retval;
  int line_no;
  dbus_bool_t unalign;
  DBusHashTable *length_hash;
  int endian;
  DBusHashIter iter;
  
  retval = FALSE;
  length_hash = NULL;
  
  if (!_dbus_string_init (&file, _DBUS_INT_MAX))
    return FALSE;

  if (!_dbus_string_init (&line, _DBUS_INT_MAX))
    {
      _dbus_string_free (&file);
      return FALSE;
    }
  
  if ((result = _dbus_file_get_contents (&file, filename)) != DBUS_RESULT_SUCCESS)
    {
      const char *s;
      _dbus_string_get_const_data (filename, &s);
      _dbus_warn ("Getting contents of %s failed: %s\n",
                     s, dbus_result_to_string (result));
                     
      goto out;
    }

  length_hash = _dbus_hash_table_new (DBUS_HASH_STRING,
                                      NULL,
                                      free_saved_length);
  if (length_hash == NULL)
    goto out;
  
  endian = DBUS_COMPILER_BYTE_ORDER;
  unalign = FALSE;
  line_no = 0;
 next_iteration:
  while (pop_line (&file, &line))
    {
      dbus_bool_t just_set_unalign;

      just_set_unalign = FALSE;
      line_no += 1;

      strip_leading_space (&line);
      
      if (_dbus_string_starts_with_c_str (&line,
                                          "#"))
        {
          /* Ignore this comment */
          goto next_iteration;
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "VALID_HEADER"))
        {
          int i;
          DBusString name;
          
          if (!_dbus_string_append_byte (dest, endian))
            {
              _dbus_warn ("could not append endianness\n");
              goto parse_failed;
            }

          i = 0;
          while (i < 3)
            {
              if (!_dbus_string_append_byte (dest, '\0'))
                {
                  _dbus_warn ("could not append nul pad\n");
                  goto parse_failed;
                }
              ++i;
            }

          _dbus_string_init_const (&name, "Header");
          if (!append_saved_length (dest, length_hash,
                                    &name, _dbus_string_get_length (dest),
                                    endian))
            goto parse_failed;

          _dbus_string_init_const (&name, "Body");
          if (!append_saved_length (dest, length_hash,
                                    &name, _dbus_string_get_length (dest),
                                    endian))
            goto parse_failed;
          
          /* client serial */
          if (!_dbus_marshal_int32 (dest, endian, 1))
            {
              _dbus_warn ("couldn't append client serial\n");
              goto parse_failed;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "BIG_ENDIAN"))
        {
          endian = DBUS_BIG_ENDIAN;
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "LITTLE_ENDIAN"))
        {
          endian = DBUS_LITTLE_ENDIAN;
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "OPPOSITE_ENDIAN"))
        {
          if (endian == DBUS_BIG_ENDIAN)
            endian = DBUS_LITTLE_ENDIAN;
          else
            endian = DBUS_BIG_ENDIAN;
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "ALIGN"))
        {
          long val;

          strip_command_name (&line);

          if (!_dbus_string_parse_int (&line, 0, &val, NULL))
            goto parse_failed;

          if (val > 16)
            {
              _dbus_warn ("Aligning to %ld boundary is crack\n",
                             val);
              goto parse_failed;
            }
          
          if (!_dbus_string_align_length (dest, val))
            goto parse_failed;
        }
      else if (_dbus_string_starts_with_c_str (&line, "UNALIGN"))
        {
          unalign = TRUE;
          just_set_unalign = TRUE;
        }
      else if (_dbus_string_starts_with_c_str (&line, "CHOP"))
        {
          long val;

          /* FIXME if you CHOP the offset for a LENGTH
           * command, we segfault.
           */
          
          strip_command_name (&line);

          if (!_dbus_string_parse_int (&line, 0, &val, NULL))
            goto parse_failed;

          if (val > _dbus_string_get_length (dest))
            {
              _dbus_warn ("Trying to chop %ld bytes but we only have %d\n",
                          val,
                          _dbus_string_get_length (dest));
              goto parse_failed;
            }
          
          _dbus_string_shorten (dest, val);
        }
      else if (_dbus_string_starts_with_c_str (&line, "BYTE"))
        {
          unsigned char the_byte;
          
          strip_command_name (&line);

          if (_dbus_string_equal_c_str (&line, "'\\''"))
            the_byte = '\'';
          else if (_dbus_string_get_byte (&line, 0) == '\'' &&
                   _dbus_string_get_length (&line) >= 3 &&
                   _dbus_string_get_byte (&line, 2) == '\'')
            the_byte = _dbus_string_get_byte (&line, 1);
          else
            {
              long val;
              if (!_dbus_string_parse_int (&line, 0, &val, NULL))
                goto parse_failed;
              if (val > 255)
                {
                  _dbus_warn ("A byte must be in range 0-255 not %ld\n",
                                 val);
                  goto parse_failed;
                }
              the_byte = (unsigned char) val;
            }

          _dbus_string_append_byte (dest, the_byte);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "SAVE_LENGTH"))
        {
          strip_command_name (&line);

          if (!save_length (length_hash, &line,
                            _dbus_string_get_length (dest)))
            {
              _dbus_warn ("failed to save length\n");
              goto parse_failed;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "LENGTH"))
        {
          SAVE_FOR_UNALIGN (dest, 4);
          
          strip_command_name (&line);

          if (!append_saved_length (dest, length_hash,
                                    &line,
                                    unalign ? align_pad_start : align_pad_end,
                                    endian))
            {
              _dbus_warn ("failed to add LENGTH\n");
              goto parse_failed;
            }

          PERFORM_UNALIGN (dest);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "FIELD_NAME"))
        {
          strip_command_name (&line);

          if (_dbus_string_get_length (&line) != 4)
            {
              const char *s;
              _dbus_string_get_const_data (&line, &s);
              _dbus_warn ("Field name must be four characters not \"%s\"\n",
                             s);
              goto parse_failed;
            }

          if (unalign)
            unalign = FALSE;
          else
            _dbus_string_align_length (dest, 4);
          
          if (!_dbus_string_copy (&line, 0, dest,
                                  _dbus_string_get_length (dest)))
            goto parse_failed;
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "TYPE"))
        {
          int code;
          
          strip_command_name (&line);          

          if (_dbus_string_starts_with_c_str (&line, "INVALID"))
            code = DBUS_TYPE_INVALID;
          else if (_dbus_string_starts_with_c_str (&line, "NIL"))
            code = DBUS_TYPE_NIL;
          else if (_dbus_string_starts_with_c_str (&line, "INT32"))
            code = DBUS_TYPE_INT32;
          else if (_dbus_string_starts_with_c_str (&line, "UINT32"))
            code = DBUS_TYPE_UINT32;
          else if (_dbus_string_starts_with_c_str (&line, "DOUBLE"))
            code = DBUS_TYPE_DOUBLE;
          else if (_dbus_string_starts_with_c_str (&line, "STRING"))
            code = DBUS_TYPE_STRING;
          else if (_dbus_string_starts_with_c_str (&line, "INT32_ARRAY"))
            code = DBUS_TYPE_INT32_ARRAY;
          else if (_dbus_string_starts_with_c_str (&line, "UINT32_ARRAY"))
            code = DBUS_TYPE_UINT32_ARRAY;
          else if (_dbus_string_starts_with_c_str (&line, "DOUBLE_ARRAY"))
            code = DBUS_TYPE_DOUBLE_ARRAY;
          else if (_dbus_string_starts_with_c_str (&line, "BYTE_ARRAY"))
            code = DBUS_TYPE_BYTE_ARRAY;
          else if (_dbus_string_starts_with_c_str (&line, "STRING_ARRAY"))
            code = DBUS_TYPE_STRING_ARRAY;
          else
            {
              const char *s;
              _dbus_string_get_const_data (&line, &s);
              _dbus_warn ("%s is not a valid type name\n", s);
              goto parse_failed;
            }

          if (!_dbus_string_append_byte (dest, code))
            {
              _dbus_warn ("could not append typecode byte\n");
              goto parse_failed;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "INT32"))
        {
          SAVE_FOR_UNALIGN (dest, 4);
          long val;
          
          strip_command_name (&line);

          if (!_dbus_string_parse_int (&line, 0, &val, NULL))
            goto parse_failed;
          
          if (!_dbus_marshal_int32 (dest, endian,
                                    val))
            {
              _dbus_warn ("failed to append INT32\n");
              goto parse_failed;
            }

          PERFORM_UNALIGN (dest);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "UINT32"))
        {
          SAVE_FOR_UNALIGN (dest, 4);
          long val;
          
          strip_command_name (&line);

          /* FIXME should have _dbus_string_parse_uint32 */
          if (!_dbus_string_parse_int (&line, 0, &val, NULL))
            goto parse_failed;
          
          if (!_dbus_marshal_uint32 (dest, endian,
                                     val))
            {
              _dbus_warn ("failed to append UINT32\n");
              goto parse_failed;
            }

          PERFORM_UNALIGN (dest);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "DOUBLE"))
        {
          SAVE_FOR_UNALIGN (dest, 8);
          double val;
          
          strip_command_name (&line);

          if (!_dbus_string_parse_double (&line, 0, &val, NULL))
            goto parse_failed;
          
          if (!_dbus_marshal_double (dest, endian,
                                     val))
            {
              _dbus_warn ("failed to append DOUBLE\n");
              goto parse_failed;
            }

          PERFORM_UNALIGN (dest);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "STRING"))
        {
          SAVE_FOR_UNALIGN (dest, 4);
          int size_offset;
          int old_len;
          
          strip_command_name (&line);

          size_offset = _dbus_string_get_length (dest);
          size_offset = _DBUS_ALIGN_VALUE (size_offset, 4);
          if (!_dbus_marshal_uint32 (dest, endian, 0))
            {
              _dbus_warn ("Failed to append string size\n");
              goto parse_failed;
            }

          old_len = _dbus_string_get_length (dest);
          if (!append_quoted_string (dest, &line))
            {
              _dbus_warn ("Failed to append quoted string\n");
              goto parse_failed;
            }

          _dbus_marshal_set_uint32 (dest, endian, size_offset,
                                    /* subtract 1 for nul */
                                    _dbus_string_get_length (dest) - old_len - 1);
          
          PERFORM_UNALIGN (dest);
        }
      else
        goto parse_failed;
      
      if (!just_set_unalign && unalign)
        {
          _dbus_warn ("UNALIGN prior to something that isn't aligned\n");
          goto parse_failed;
        }

      goto next_iteration; /* skip parse_failed */
      
    parse_failed:
      {
        const char *s;
        _dbus_string_get_const_data (&line, &s);
        _dbus_warn ("couldn't process line %d \"%s\"\n",
                    line_no, s);
        goto out;
      }
    }

  _dbus_hash_iter_init (length_hash, &iter);
  while (_dbus_hash_iter_next (&iter))
    {
      SavedLength *sl = _dbus_hash_iter_get_value (&iter);
      const char *s;

      _dbus_string_get_const_data (&sl->name, &s);
      
      if (sl->length < 0)
        {
          _dbus_warn ("Used LENGTH %s but never did SAVE_LENGTH\n",
                      s);
          goto out;
        }
      else if (sl->offset < 0)
        {
          _dbus_warn ("Did SAVE_LENGTH %s but never used LENGTH\n",
                      s);
          goto out;
        }
      else
        {
          _dbus_verbose ("Filling in length %s endian = %d offset = %d length = %d\n",
                         s, sl->endian, sl->offset, sl->length);
          _dbus_marshal_set_int32 (dest,
                                   sl->endian,
                                   sl->offset,
                                   sl->length);
        }
    }
  
  retval = TRUE;
  
 out:
  if (length_hash != NULL)
    _dbus_hash_table_unref (length_hash);
  
  _dbus_string_free (&file);
  _dbus_string_free (&line);
  return retval;
}

/** @} */
#endif /* DBUS_BUILD_TESTS */
