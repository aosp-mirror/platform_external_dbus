/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message-builder.c Build messages from text files for testing (internal to D-BUS implementation)
 * 
 * Copyright (C) 2003, 2004 Red Hat, Inc.
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

/**
 * Saved length
 */
typedef struct
{
  DBusString name; /**< Name of the length */
  int start;  /**< Calculate length since here */
  int length; /**< length to write */
  int offset; /**< where to write it into the data */
  int endian; /**< endianness to write with */
} SavedLength;

static void
free_saved_length (void *data)
{
  SavedLength *sl = data;

  if (sl == NULL)
    return; /* all hash free functions have to accept NULL */
  
  _dbus_string_free (&sl->name);
  dbus_free (sl);
}

static SavedLength*
ensure_saved_length (DBusHashTable    *hash,
                     const DBusString *name)
{
  SavedLength *sl;
  const char *s;
  
  s = _dbus_string_get_const_data (name);

  sl = _dbus_hash_table_lookup_string (hash, s);
  if (sl != NULL)
    return sl;
  
  sl = dbus_new0 (SavedLength, 1);

  if (!_dbus_string_init (&sl->name))
    {
      dbus_free (sl);
      return NULL;
    }

  if (!_dbus_string_copy (name, 0, &sl->name, 0))
    goto failed;

  s = _dbus_string_get_const_data (&sl->name);

  if (!_dbus_hash_table_insert_string (hash, (char*)s, sl))
    goto failed;

  sl->start = -1;
  sl->length = -1;
  sl->offset = -1;
  sl->endian = -1;
  
  return sl;
  
 failed:
  free_saved_length (sl);
  return NULL;
}

static dbus_bool_t
save_start (DBusHashTable    *hash,
            const DBusString *name,
            int               start)
{
  SavedLength *sl;

  sl = ensure_saved_length (hash, name);

  if (sl == NULL)
    return FALSE;
  else if (sl->start >= 0)
    {
      _dbus_warn ("Same START_LENGTH given twice\n");
      return FALSE;
    }
  else
    sl->start = start;

  return TRUE;
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
      _dbus_warn ("Same END_LENGTH given twice\n");
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
                      const DBusString *quoted,
		      int               start_pos,
		      int              *new_pos)
{
  dbus_bool_t in_quotes = FALSE;
  int i;

  /* FIXME: We might want to add escaping in case we want to put '
   * characters in our strings.
   */
  
  i = start_pos;
  while (i < _dbus_string_get_length (quoted))
    {
      unsigned char b;

      b = _dbus_string_get_byte (quoted, i);
      
      if (in_quotes)
        {
          if (b == '\'')
	    break;
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

  if (new_pos)
    *new_pos = i;
  
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
  
  if (!_dbus_marshal_uint32 (dest, endian,
                             -1))
    {
      _dbus_warn ("failed to append a length\n");
      return FALSE;
    }

  return TRUE;
}

static int
message_type_from_string (const DBusString *str,
                          int               start)
{
  const char *s;

  s = _dbus_string_get_const_data_len (str, start,
                                       _dbus_string_get_length (str) - start);

  if (strncmp (s, "method_call", strlen ("method_call")) == 0)
    return DBUS_MESSAGE_TYPE_METHOD_CALL;
  else if (strncmp (s, "method_return", strlen ("method_return")) == 0)
    return DBUS_MESSAGE_TYPE_METHOD_RETURN;
  else if (strncmp (s, "signal", strlen ("signal")) == 0)
    return DBUS_MESSAGE_TYPE_SIGNAL;
  else if (strncmp (s, "error", strlen ("error")) == 0)
    return DBUS_MESSAGE_TYPE_ERROR;
  else if (strncmp (s, "invalid", strlen ("invalid")) == 0)
    return DBUS_MESSAGE_TYPE_INVALID;
  else
    return -1;
}

static dbus_bool_t
append_string_field (DBusString *dest,
                     int         endian,
                     int         field,
                     int         type,
                     const char *value)
{
  int len;
  
  if (!_dbus_string_append_byte (dest, field))
    {
      _dbus_warn ("couldn't append field name byte\n");
      return FALSE;
    }
  
  if (!_dbus_string_append_byte (dest, type))
    {
      _dbus_warn ("could not append typecode byte\n");
      return FALSE;
    }

  len = strlen (value);

  if (!_dbus_marshal_uint32 (dest, endian, len))
    {
      _dbus_warn ("couldn't append string length\n");
      return FALSE;
    }
  
  if (!_dbus_string_append (dest, value))
    {
      _dbus_warn ("couldn't append field value\n");
      return FALSE;
    }

  if (!_dbus_string_append_byte (dest, 0))
    {
      _dbus_warn ("couldn't append string nul term\n");
      return FALSE;
    }

  return TRUE;
}

static dbus_bool_t
parse_basic_type (DBusString *src, char type,
		  DBusString *dest, dbus_bool_t *unalign,
		  int endian)
{
  int align;
  int align_pad_start, align_pad_end;
  unsigned char data[16];

  switch (type)
    {
    case DBUS_TYPE_BYTE:
    case DBUS_TYPE_BOOLEAN:
      align = 1;
      break;
    case DBUS_TYPE_UINT32:
    case DBUS_TYPE_INT32:
      align = 4;
      break;
    case DBUS_TYPE_DOUBLE:
      align = 8;
      break;
    default:
      _dbus_assert_not_reached ("not a basic type");
      break;
    }

  align_pad_start = _dbus_string_get_length (dest);
  align_pad_end = _DBUS_ALIGN_VALUE (align_pad_start, align);

  _dbus_string_delete_first_word (src);

  if (!_dbus_string_parse_basic_type (src, type, 0, data, NULL))
    {
      _dbus_verbose ("failed to parse type '%c'", type);
      return FALSE;
    }

  if (!_dbus_marshal_basic_type (dest, type, data, endian))
    {
      _dbus_verbose ("failed to marshal type '%c'", type);
      return FALSE;
    }

  if (*unalign)
    {
      _dbus_string_delete (dest, align_pad_start,
                           align_pad_end - align_pad_start);
      *unalign = FALSE;
    }

  return TRUE;
}

static dbus_bool_t
parse_basic_array (DBusString *src, char type,
		   DBusString *dest, dbus_bool_t *unalign,
		   int endian)
{
  int array_align, elem_size;
  int i, len, allocated;
  unsigned char *values, b;
  int values_offset;
  int align_pad_start, align_pad_end;
  dbus_bool_t retval = FALSE;

  array_align = 4; /* length */
  switch (type)
    {
    case DBUS_TYPE_BYTE:
    case DBUS_TYPE_BOOLEAN:
      elem_size = 1;
      break;
    case DBUS_TYPE_UINT32:
    case DBUS_TYPE_INT32:
      elem_size = 4;
      break;
    case DBUS_TYPE_DOUBLE:
      array_align = 8;
      elem_size = 8;
      break;
    default:
      _dbus_assert_not_reached ("not a basic type");
      break;
    }

  align_pad_start = _dbus_string_get_length (dest);
  align_pad_end = _DBUS_ALIGN_VALUE (align_pad_start, array_align);

  len = 0;
  allocated = 2;
  values = NULL;
  values_offset = 0;
	  
  _dbus_string_delete_first_word (src);
  _dbus_string_skip_blank (src, 0, &i);
  b = _dbus_string_get_byte (src, i++);

  if (b != '{')
    goto failed;

  while (i < _dbus_string_get_length (src))
    {
      _dbus_string_skip_blank (src, i, &i);

      if (!values || len == allocated - 1)
	{
	  allocated *= 2;
	  values = dbus_realloc (values, allocated * elem_size);
	  if (!values)
	    {
	      _dbus_warn ("could not allocate memory for '%c' ARRAY\n", type);
	      goto failed;
	    }
	}

      if (!_dbus_string_parse_basic_type (src, type, i, values + values_offset, &i))
	{
	  _dbus_warn ("could not parse integer element %d of '%c' ARRAY\n", len, type);
	  goto failed;
	}

      values_offset += elem_size;
      len++;
	      
      _dbus_string_skip_blank (src, i, &i);

      b = _dbus_string_get_byte (src, i++);

      if (b == '}')
	break;
      else if (b != ',')
	goto failed;
    }

  if (!_dbus_marshal_basic_type_array (dest, type, values, len, endian))
    {
      _dbus_warn ("failed to append '%c' ARRAY\n", type);
      goto failed;
    }

  if (*unalign)
    {
      _dbus_string_delete (dest, align_pad_start,
                           align_pad_end - align_pad_start);
      *unalign = FALSE;
    }

  retval = TRUE;

 failed:
  dbus_free (values);
  return retval;
}

static char
lookup_basic_type (const DBusString *str, dbus_bool_t *is_array)
{
  int i;
  char type = DBUS_TYPE_INVALID;
  static struct {
    const char *name;
    char        type;
  } name_to_type[] = {
    { "BYTE",    DBUS_TYPE_BYTE },
    { "BOOLEAN", DBUS_TYPE_BOOLEAN },
    { "INT32",   DBUS_TYPE_INT32 },
    { "UINT32",  DBUS_TYPE_UINT32 },
    { "DOUBLE",  DBUS_TYPE_DOUBLE }
  };

  for (i = 0; i < _DBUS_N_ELEMENTS(name_to_type); i++)
    {
      const char *name = name_to_type[i].name;
      if (_dbus_string_starts_with_c_str (str, name)) 
	{
	  int offset = strlen (name);
	  type = name_to_type[i].type;
	  if (is_array)
	    *is_array = _dbus_string_find (str, offset, "_ARRAY", NULL);
	  break;
	}
    }

  return type;
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
 *   VALID_HEADER <type> normal header; byte order, type, padding, header len, body len, serial
 *   REQUIRED_FIELDS add required fields with placeholder values
 *   BIG_ENDIAN switch to big endian
 *   LITTLE_ENDIAN switch to little endian
 *   OPPOSITE_ENDIAN switch to opposite endian
 *   ALIGN <N> aligns to the given value
 *   UNALIGN skips alignment for the next marshal
 *   BYTE <N> inserts the given integer in [0,255] or char in 'a' format
 *   START_LENGTH <name> marks the start of a length to measure
 *   END_LENGTH <name> records the length since START_LENGTH under the given name
 *                     (or if no START_LENGTH, absolute length)
 *   LENGTH <name> inserts the saved length of the same name
 *   CHOP <N> chops last N bytes off the data
 *   HEADER_FIELD <fieldname> inserts a header field name byte
 *   TYPE <typename> inserts a typecode byte 
 * @endcode
 * 
 * Following commands insert aligned data unless
 * preceded by "UNALIGN":
 * @code
 *   INT32 <N> marshals an INT32
 *   UINT32 <N> marshals a UINT32
 *   INT64 <N> marshals an INT64
 *   UINT64 <N> marshals a UINT64
 *   DOUBLE <N> marshals a double
 *   STRING 'Foo' marshals a string
 *   OBJECT_PATH '/foo/bar' marshals an object path
 *   BYTE_ARRAY { 'a', 3, 4, 5, 6} marshals a BYTE array
 *   BOOLEAN_ARRAY { false, true, false} marshals a BOOLEAN array
 *   INT32_ARRAY { 3, 4, 5, 6} marshals an INT32 array
 *   UINT32_ARRAY { 3, 4, 5, 6} marshals an UINT32 array
 *   DOUBLE_ARRAY { 1.0, 2.0, 3.0, 4.0} marshals a DOUBLE array  
 *   STRING_ARRAY { "foo", "bar", "gazonk"} marshals a STRING array  
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
  DBusError error;
  DBusString line;
  dbus_bool_t retval;
  int line_no;
  dbus_bool_t unalign;
  DBusHashTable *length_hash;
  int endian;
  DBusHashIter iter;
  char type;
  dbus_bool_t is_array;
  
  retval = FALSE;
  length_hash = NULL;
  
  if (!_dbus_string_init (&file))
    return FALSE;

  if (!_dbus_string_init (&line))
    {
      _dbus_string_free (&file);
      return FALSE;
    }

  _dbus_verbose ("Loading %s\n", _dbus_string_get_const_data (filename));

  dbus_error_init (&error);
  if (!_dbus_file_get_contents (&file, filename, &error))
    {
      _dbus_warn ("Getting contents of %s failed: %s\n",
                  _dbus_string_get_const_data (filename), error.message);
      dbus_error_free (&error);
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
  while (_dbus_string_pop_line (&file, &line))
    {
      dbus_bool_t just_set_unalign;

      just_set_unalign = FALSE;
      line_no += 1;

      _dbus_string_delete_leading_blanks (&line);

      if (_dbus_string_get_length (&line) == 0)
        {
          /* empty line */
          goto next_iteration;
        }
      else if (_dbus_string_starts_with_c_str (&line,
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
          int message_type;

          if (_dbus_string_get_length (&line) < (int) strlen ("VALID_HEADER "))
            {
              _dbus_warn ("no args to VALID_HEADER\n");
              goto parse_failed;
            }
          
          if (!_dbus_string_append_byte (dest, endian))
            {
              _dbus_warn ("could not append endianness\n");
              goto parse_failed;
            }

          message_type = message_type_from_string (&line,
                                                   strlen ("VALID_HEADER "));
          if (message_type < 0)
            {
              _dbus_warn ("VALID_HEADER not followed by space then known message type\n");
              goto parse_failed;
            }
          
          if (!_dbus_string_append_byte (dest, message_type))
            {
              _dbus_warn ("could not append message type\n");
              goto parse_failed;
            }
          
          i = 0;
          while (i < 2)
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
          if (!_dbus_marshal_uint32 (dest, endian, 1))
            {
              _dbus_warn ("couldn't append client serial\n");
              goto parse_failed;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "REQUIRED_FIELDS"))
        {
          if (!append_string_field (dest, endian,
                                    DBUS_HEADER_FIELD_INTERFACE,
                                    DBUS_TYPE_STRING,
                                    "org.freedesktop.BlahBlahInterface"))
            goto parse_failed;
          if (!append_string_field (dest, endian,
                                    DBUS_HEADER_FIELD_MEMBER,
                                    DBUS_TYPE_STRING,
                                    "BlahBlahMethod"))
            goto parse_failed;
          if (!append_string_field (dest, endian,
                                    DBUS_HEADER_FIELD_PATH,
                                    DBUS_TYPE_OBJECT_PATH,
                                    "/blah/blah/path"))
            goto parse_failed;

          /* FIXME later we'll validate this, and then it will break
           * and the .message files will have to include the right thing
           */
          if (!append_string_field (dest, endian,
                                    DBUS_HEADER_FIELD_SIGNATURE,
                                    DBUS_TYPE_STRING,
                                    "iii"))
            goto parse_failed;
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
          int end;
          int orig_len;
          
          _dbus_string_delete_first_word (&line);

          if (!_dbus_string_parse_int (&line, 0, &val, &end))
            {
              _dbus_warn ("Failed to parse integer\n");
              goto parse_failed;
            }

          if (val > 8)
            {
              _dbus_warn ("Aligning to %ld boundary is crack\n",
                          val);
              goto parse_failed;
            }

          orig_len = _dbus_string_get_length (dest);
          
          if (!_dbus_string_align_length (dest, val))
            goto parse_failed;

          if (_dbus_string_parse_int (&line, end, &val, NULL))
            {
              /* If there's an optional second int argument,
               * fill in align padding with that value
               */
              if (val < 0 || val > 255)
                {
                  _dbus_warn ("can't fill align padding with %ld, must be a byte value\n", val);
                  goto parse_failed;
                }

              end = orig_len;
              while (end < _dbus_string_get_length (dest))
                {
                  _dbus_string_set_byte (dest, end, val);
                  ++end;
                }
            }
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
          
          _dbus_string_delete_first_word (&line);

          if (!_dbus_string_parse_int (&line, 0, &val, NULL))
            {
              _dbus_warn ("Failed to parse integer to chop\n");
              goto parse_failed;
            }

          if (val > _dbus_string_get_length (dest))
            {
              _dbus_warn ("Trying to chop %ld bytes but we only have %d\n",
                          val,
                          _dbus_string_get_length (dest));
              goto parse_failed;
            }
          
          _dbus_string_shorten (dest, val);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "START_LENGTH"))
        {
          _dbus_string_delete_first_word (&line);

          if (!save_start (length_hash, &line,
                           _dbus_string_get_length (dest)))
            {
              _dbus_warn ("failed to save length start\n");
              goto parse_failed;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "END_LENGTH"))
        {
          _dbus_string_delete_first_word (&line);

          if (!save_length (length_hash, &line,
                            _dbus_string_get_length (dest)))
            {
              _dbus_warn ("failed to save length end\n");
              goto parse_failed;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "LENGTH"))
        {
          SAVE_FOR_UNALIGN (dest, 4);
          
          _dbus_string_delete_first_word (&line);

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
                                               "HEADER_FIELD"))
        {
	  int field;

          _dbus_string_delete_first_word (&line);

          if (_dbus_string_starts_with_c_str (&line, "INVALID"))
            field = DBUS_HEADER_FIELD_INVALID;
          else if (_dbus_string_starts_with_c_str (&line, "PATH"))
	    field = DBUS_HEADER_FIELD_PATH;
          else if (_dbus_string_starts_with_c_str (&line, "INTERFACE"))
	    field = DBUS_HEADER_FIELD_INTERFACE;
          else if (_dbus_string_starts_with_c_str (&line, "MEMBER"))
	    field = DBUS_HEADER_FIELD_MEMBER;
          else if (_dbus_string_starts_with_c_str (&line, "ERROR_NAME"))
	    field = DBUS_HEADER_FIELD_ERROR_NAME;
          else if (_dbus_string_starts_with_c_str (&line, "REPLY_SERIAL"))
	    field = DBUS_HEADER_FIELD_REPLY_SERIAL;
          else if (_dbus_string_starts_with_c_str (&line, "DESTINATION"))
	    field = DBUS_HEADER_FIELD_DESTINATION;
          else if (_dbus_string_starts_with_c_str (&line, "SENDER"))
	    field = DBUS_HEADER_FIELD_SENDER;
          else if (_dbus_string_starts_with_c_str (&line, "SIGNATURE"))
	    field = DBUS_HEADER_FIELD_SIGNATURE;
	  else if (_dbus_string_starts_with_c_str (&line, "UNKNOWN"))
	    field = 22; /* random unknown header field */
          else
            {
              _dbus_warn ("%s is not a valid header field name\n",
			  _dbus_string_get_const_data (&line));
              goto parse_failed;
            }

          if (!_dbus_string_append_byte (dest, field))
	    {
              _dbus_warn ("could not append header field name byte\n");
	      goto parse_failed;
	    }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "TYPE"))
        {
          int code;
          
          _dbus_string_delete_first_word (&line);          

          if (_dbus_string_starts_with_c_str (&line, "INVALID"))
            code = DBUS_TYPE_INVALID;
          else if (_dbus_string_starts_with_c_str (&line, "NIL"))
            code = DBUS_TYPE_NIL;
	  else if ((code = lookup_basic_type (&line, NULL)) != DBUS_TYPE_INVALID)
	    ;
          else if (_dbus_string_starts_with_c_str (&line, "STRING"))
            code = DBUS_TYPE_STRING;
          else if (_dbus_string_starts_with_c_str (&line, "OBJECT_PATH"))
            code = DBUS_TYPE_OBJECT_PATH;
          else if (_dbus_string_starts_with_c_str (&line, "CUSTOM"))
            code = DBUS_TYPE_CUSTOM;
          else if (_dbus_string_starts_with_c_str (&line, "ARRAY"))
            code = DBUS_TYPE_ARRAY;
          else if (_dbus_string_starts_with_c_str (&line, "DICT"))
            code = DBUS_TYPE_DICT;
          else
            {
              _dbus_warn ("%s is not a valid type name\n", _dbus_string_get_const_data (&line));
              goto parse_failed;
            }

          if (!_dbus_string_append_byte (dest, code))
            {
              _dbus_warn ("could not append typecode byte\n");
              goto parse_failed;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
					       "STRING_ARRAY"))
	{
	  SAVE_FOR_UNALIGN (dest, 4);
	  int i, len, allocated;
	  char **values;
	  char *val;
	  DBusString val_str;
	  unsigned char b;

	  allocated = 4;
	  values = dbus_new (char *, allocated);
	  if (!values)
	    {
	      _dbus_warn ("could not allocate memory for STRING_ARRAY\n");
	      goto parse_failed;
	    }
	  
	  len = 0;
	  
	  _dbus_string_delete_first_word (&line);
	  _dbus_string_skip_blank (&line, 0, &i);
	  b = _dbus_string_get_byte (&line, i++);

	  if (b != '{')
	    goto parse_failed;

	  _dbus_string_init (&val_str);
	  while (i < _dbus_string_get_length (&line))
	    {
	      _dbus_string_skip_blank (&line, i, &i);

	      if (!append_quoted_string (&val_str, &line, i, &i))
		{
		  _dbus_warn ("could not parse quoted string for STRING_ARRAY\n");
		  goto parse_failed;
		}
	      i++;

	      if (!_dbus_string_steal_data (&val_str, &val))
		{
		  _dbus_warn ("could not allocate memory for STRING_ARRAY string\n");
		  goto parse_failed;
		}
	      
	      values[len++] = val;
	      if (len == allocated)
		{
		  allocated *= 2;
		  values = dbus_realloc (values, allocated * sizeof (char *));
		  if (!values)
		    {
		      _dbus_warn ("could not allocate memory for STRING_ARRAY\n");
		      goto parse_failed;
		    }
		}
	      
	      _dbus_string_skip_blank (&line, i, &i);
	      
	      b = _dbus_string_get_byte (&line, i++);

	      if (b == '}')
		break;
	      else if (b != ',')
		{
		  _dbus_warn ("missing comma when parsing STRING_ARRAY\n");
		  goto parse_failed;
		}
	    }
	  _dbus_string_free (&val_str);
	  
          if (!_dbus_marshal_string_array (dest, endian, (const char **)values, len))
            {
              _dbus_warn ("failed to append STRING_ARRAY\n");
              goto parse_failed;
            }

	  values[len] = NULL;
	  dbus_free_string_array (values);
	  
	  PERFORM_UNALIGN (dest);
	}
      else if (_dbus_string_starts_with_c_str (&line,
                                               "STRING"))
        {
          SAVE_FOR_UNALIGN (dest, 4);
          int size_offset;
          int old_len;
          
          _dbus_string_delete_first_word (&line);

          size_offset = _dbus_string_get_length (dest);
          size_offset = _DBUS_ALIGN_VALUE (size_offset, 4);
          if (!_dbus_marshal_uint32 (dest, endian, 0))
            {
              _dbus_warn ("Failed to append string size\n");
              goto parse_failed;
            }

          old_len = _dbus_string_get_length (dest);
          if (!append_quoted_string (dest, &line, 0, NULL))
            {
              _dbus_warn ("Failed to append quoted string\n");
              goto parse_failed;
            }

          _dbus_marshal_set_uint32 (dest, endian, size_offset,
                                    /* subtract 1 for nul */
                                    _dbus_string_get_length (dest) - old_len - 1);
          
          PERFORM_UNALIGN (dest);
        }
      else if ((type = lookup_basic_type (&line, &is_array)) != DBUS_TYPE_INVALID)
	{
	  if (is_array)
	    {
	      if (!parse_basic_array (&line, type, dest, &unalign, endian))
		goto parse_failed;
	    }
	  else
	    {
	      if (!parse_basic_type (&line, type, dest, &unalign, endian))
		goto parse_failed;
	    }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                              "OBJECT_PATH"))
        {
          SAVE_FOR_UNALIGN (dest, 4);
          int size_offset;
          int old_len;
          
          _dbus_string_delete_first_word (&line);
          
          size_offset = _dbus_string_get_length (dest);
          size_offset = _DBUS_ALIGN_VALUE (size_offset, 4);
          if (!_dbus_marshal_uint32 (dest, endian, 0))
            {
              _dbus_warn ("Failed to append string size\n");
              goto parse_failed;
            }

          old_len = _dbus_string_get_length (dest);
          if (!append_quoted_string (dest, &line, 0, NULL))
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
        _dbus_warn ("couldn't process line %d \"%s\"\n",
                    line_no, _dbus_string_get_const_data (&line));
        goto out;
      }
    }

  _dbus_hash_iter_init (length_hash, &iter);
  while (_dbus_hash_iter_next (&iter))
    {
      SavedLength *sl = _dbus_hash_iter_get_value (&iter);
      const char *s;

      s = _dbus_string_get_const_data (&sl->name);
      
      if (sl->length < 0)
        {
          _dbus_warn ("Used LENGTH %s but never did END_LENGTH\n",
                      s);
          goto out;
        }
      else if (sl->offset < 0)
        {
          _dbus_warn ("Did END_LENGTH %s but never used LENGTH\n",
                      s);
          goto out;
        }
      else
        {
          if (sl->start < 0)
            sl->start = 0;
          
          _dbus_verbose ("Filling in length %s endian = %d offset = %d start = %d length = %d\n",
                         s, sl->endian, sl->offset, sl->start, sl->length);
          _dbus_marshal_set_int32 (dest,
                                   sl->endian,
                                   sl->offset,
                                   sl->length - sl->start);
        }

      _dbus_hash_iter_remove_entry (&iter);
    }
  
  retval = TRUE;

  _dbus_verbose_bytes_of_string (dest, 0, _dbus_string_get_length (dest));
  
 out:
  if (length_hash != NULL)
    _dbus_hash_table_unref (length_hash);
  
  _dbus_string_free (&file);
  _dbus_string_free (&line);
  return retval;
}

/** @} */
#endif /* DBUS_BUILD_TESTS */
