/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message.c  DBusMessage object
 *
 * Copyright (C) 2002, 2003, 2004  Red Hat Inc.
 * Copyright (C) 2002, 2003  CodeFactory AB
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

#include "dbus-internals.h"
#include "dbus-marshal.h"
#include "dbus-message.h"
#include "dbus-message-internal.h"
#include "dbus-memory.h"
#include "dbus-list.h"
#include "dbus-message-builder.h"
#include "dbus-dataslot.h"
#include <string.h>

/**
 * @defgroup DBusMessageInternals DBusMessage implementation details
 * @ingroup DBusInternals
 * @brief DBusMessage private implementation details.
 *
 * The guts of DBusMessage and its methods.
 *
 * @{
 */

/**
 * Cached information about a header field in the message
 */
typedef struct
{
  int name_offset;  /**< Offset to name of field */
  int value_offset; /**< Offset to value of field */
} HeaderField;

/** Offset to byte order from start of header */
#define BYTE_ORDER_OFFSET    0
/** Offset to type from start of header */
#define TYPE_OFFSET          1
/** Offset to flags from start of header */
#define FLAGS_OFFSET         2
/** Offset to version from start of header */
#define VERSION_OFFSET       3
/** Offset to header length from start of header */
#define HEADER_LENGTH_OFFSET 4
/** Offset to body length from start of header */
#define BODY_LENGTH_OFFSET   8
/** Offset to client serial from start of header */
#define CLIENT_SERIAL_OFFSET 12


/**
 * @brief Internals of DBusMessage
 * 
 * Object representing a message received from or to be sent to
 * another application. This is an opaque object, all members
 * are private.
 */
struct DBusMessage
{
  DBusAtomic refcount; /**< Reference count */

  DBusString header; /**< Header network data, stored
                      * separately from body so we can
                      * independently realloc it.
                      */

  HeaderField header_fields[DBUS_HEADER_FIELD_LAST + 1]; /**< Track the location
							  * of each field in "header"
							  */

  dbus_uint32_t client_serial; /**< Cached client serial value for speed */
  dbus_uint32_t reply_serial;  /**< Cached reply serial value for speed */
  
  int header_padding; /**< bytes of alignment in header */
  
  DBusString body;   /**< Body network data. */

  char byte_order; /**< Message byte order. */

  unsigned int locked : 1; /**< Message being sent, no modifications allowed. */
  
  DBusList *size_counters;   /**< 0-N DBusCounter used to track message size. */
  long size_counter_delta;   /**< Size we incremented the size counters by.   */

  dbus_uint32_t changed_stamp; /**< Incremented when iterators are invalidated. */

  DBusDataSlotList slot_list;   /**< Data stored by allocated integer ID */

#ifndef DBUS_DISABLE_CHECKS
  int generation; /**< _dbus_current_generation when message was created */
#endif
};

enum {
  DBUS_MESSAGE_ITER_TYPE_MESSAGE,
  DBUS_MESSAGE_ITER_TYPE_ARRAY,
  DBUS_MESSAGE_ITER_TYPE_DICT
};

/** typedef for internals of message iterator */
typedef struct DBusMessageRealIter DBusMessageRealIter;

/**
 * @brief Internals of DBusMessageIter
 * 
 * Object representing a position in a message. All fields are internal.
 */
struct DBusMessageRealIter
{
  DBusMessageRealIter *parent_iter; /**< parent iter, or NULL */
  DBusMessage *message; /**< Message used */
  dbus_uint32_t changed_stamp; /**< stamp to detect invalid iters */
  
  /* This is an int instead of an enum to get a guaranteed size for the dummy: */
  int type; /**< type of iter */
  
  int pos; /**< Current position in the string */
  int end; /**< position right after the container */
  int container_start; /**< offset of the start of the container */
  int container_length_pos; /**< offset of the length of the container */
  
  int wrote_dict_key; /**< whether we wrote the dict key for the current dict element */

  int array_type_pos; /**< pointer to the position of the array element type */
  int array_type_done; /**< TRUE if the array type is fully finished */
};

/**
 * Gets the data to be sent over the network for this message.
 * The header and then the body should be written out.
 * This function is guaranteed to always return the same
 * data once a message is locked (with _dbus_message_lock()).
 *
 * @param message the message.
 * @param header return location for message header data.
 * @param body return location for message body data.
 */
void
_dbus_message_get_network_data (DBusMessage          *message,
                                const DBusString    **header,
                                const DBusString    **body)
{
  _dbus_assert (message->locked);
  
  *header = &message->header;
  *body = &message->body;
}

static void
clear_header_padding (DBusMessage *message)
{
  _dbus_string_shorten (&message->header,
                        message->header_padding);
  message->header_padding = 0;
}              

#ifdef DBUS_DISABLE_CHECKS
#define is_valid_error_name(x) TRUE
#else
static dbus_bool_t
is_valid_error_name (const char *error_name)                                          
{
  DBusString the_error_name;

  if (error_name == NULL)
    return FALSE;
  
  _dbus_string_init_const (&the_error_name, error_name);
  return _dbus_string_validate_error_name (&the_error_name, 0,   
                                           _dbus_string_get_length (&the_error_name));
}
#endif

static dbus_bool_t
append_header_padding (DBusMessage *message)
{
  int old_len;
  old_len = _dbus_string_get_length (&message->header);
  if (!_dbus_string_align_length (&message->header, 8))
    return FALSE;

  message->header_padding = _dbus_string_get_length (&message->header) - old_len;

  return TRUE;
}

#ifdef DBUS_BUILD_TESTS
/* tests-only until it's actually used */
static dbus_int32_t
get_int_field (DBusMessage *message,
               int          field)
{
  int offset;

  _dbus_assert (field <= DBUS_HEADER_FIELD_LAST);
  
  offset = message->header_fields[field].value_offset;
  
  if (offset < 0)
    return -1; /* useless if -1 is a valid value of course */
  
  return _dbus_demarshal_int32 (&message->header,
                                message->byte_order,
                                offset,
                                NULL);
}
#endif

static dbus_uint32_t
get_uint_field (DBusMessage *message,
                int          field)
{
  int offset;
  
  _dbus_assert (field <= DBUS_HEADER_FIELD_LAST);
  
  offset = message->header_fields[field].value_offset;
  
  if (offset < 0)
    return 0; /* useless if 0 is a valid value of course */
  
  return _dbus_demarshal_uint32 (&message->header,
                                 message->byte_order,
                                 offset,
                                 NULL);
}

static const char*
get_string_field (DBusMessage *message,
                  int          field,
                  int         *len)
{
  int offset;
  const char *data;

  _dbus_return_val_if_fail (message->generation == _dbus_current_generation,
                            NULL);
  
  offset = message->header_fields[field].value_offset;

  _dbus_assert (field <= DBUS_HEADER_FIELD_LAST);
  
  if (offset < 0)
    return NULL;

  /* offset points to string length, string data follows it */
  /* FIXME _dbus_demarshal_const_string() that returned
   * a reference to the string plus its len might be nice.
   */
  
  if (len)
    *len = _dbus_demarshal_uint32 (&message->header,
                                   message->byte_order,
                                   offset,
                                   NULL);

  data = _dbus_string_get_const_data (&message->header);
  
  return data + (offset + 4); 
}

/* returns FALSE if no memory, TRUE with NULL path if no field */
static dbus_bool_t
get_path_field_decomposed (DBusMessage  *message,
                           int           field,
                           char       ***path)
{
  int offset;

  offset = message->header_fields[field].value_offset;

  _dbus_assert (field <= DBUS_HEADER_FIELD_LAST);
  
  if (offset < 0)
    {
      *path = NULL;
      return TRUE;
    }

  return _dbus_demarshal_object_path (&message->header,
                                      message->byte_order,
                                      offset,
                                      NULL,
                                      path, NULL);
}

#ifdef DBUS_BUILD_TESTS
static dbus_bool_t
append_int_field (DBusMessage *message,
                  int          field,
                  int          value)
{
  _dbus_assert (!message->locked);

  clear_header_padding (message);
  
  message->header_fields[field].name_offset =
    _dbus_string_get_length (&message->header);
  
  if (!_dbus_string_append_byte (&message->header, field))
    goto failed;

  if (!_dbus_string_append_byte (&message->header, DBUS_TYPE_INT32))
    goto failed;

  if (!_dbus_string_align_length (&message->header, 4))
    goto failed;
  
  message->header_fields[field].value_offset =
    _dbus_string_get_length (&message->header);
  
  if (!_dbus_marshal_int32 (&message->header, message->byte_order,
                            value))
    goto failed;

  if (!append_header_padding (message))
    goto failed;
  
  return TRUE;
  
 failed:
  _dbus_string_set_length (&message->header,
			   message->header_fields[field].name_offset);
  message->header_fields[field].name_offset  = -1;
  message->header_fields[field].value_offset = -1;

  /* this must succeed because it was allocated on function entry and
   * DBusString doesn't ever realloc smaller
   */
  if (!append_header_padding (message))
    _dbus_assert_not_reached ("failed to reappend header padding");
  return FALSE;
}
#endif

static dbus_bool_t
append_uint_field (DBusMessage *message,
                   int          field,
		   int          value)
{
  _dbus_assert (!message->locked);

  clear_header_padding (message);
  
  message->header_fields[field].name_offset =
    _dbus_string_get_length (&message->header);
  
  if (!_dbus_string_append_byte (&message->header, field))
    goto failed;

  if (!_dbus_string_append_byte (&message->header, DBUS_TYPE_UINT32))
    goto failed;

  if (!_dbus_string_align_length (&message->header, 4))
    goto failed;
  
  message->header_fields[field].value_offset =
    _dbus_string_get_length (&message->header);
  
  if (!_dbus_marshal_uint32 (&message->header, message->byte_order,
                             value))
    goto failed;

  if (!append_header_padding (message))
    goto failed;
  
  return TRUE;
  
 failed:
  _dbus_string_set_length (&message->header,
			   message->header_fields[field].name_offset);
  message->header_fields[field].name_offset  = -1;
  message->header_fields[field].value_offset = -1;

  /* this must succeed because it was allocated on function entry and
   * DBusString doesn't ever realloc smaller
   */
  if (!append_header_padding (message))
    _dbus_assert_not_reached ("failed to reappend header padding");
  return FALSE;
}

/** The maximum number of bytes of overhead to append to a string
 * (fieldcode + typecode + alignment + length + nul + headerpadding)
 */
#define MAX_BYTES_OVERHEAD_TO_APPEND_A_STRING (1 + 1 + 3 + 4 + 1 + 8)

static dbus_bool_t
append_string_field_len (DBusMessage *message,
                         int          field,
                         int          type,
                         const char  *value,
                         int          value_len)
{
  _dbus_assert (!message->locked);

  clear_header_padding (message);
  
  message->header_fields[field].name_offset =
    _dbus_string_get_length (&message->header);
  
  if (!_dbus_string_append_byte (&message->header, field))
    goto failed;
  
  if (!_dbus_string_append_byte (&message->header, type))
    goto failed;

  if (!_dbus_string_align_length (&message->header, 4))
    goto failed;
  
  message->header_fields[field].value_offset =
    _dbus_string_get_length (&message->header);
  
  if (!_dbus_marshal_string_len (&message->header, message->byte_order,
                                 value, value_len))
    goto failed;

  if (!append_header_padding (message))
    goto failed;
  
  return TRUE;
  
 failed:
  _dbus_string_set_length (&message->header,
			   message->header_fields[field].name_offset);
  message->header_fields[field].name_offset  = -1;
  message->header_fields[field].value_offset = -1;

  /* this must succeed because it was allocated on function entry and
   * DBusString doesn't ever realloc smaller
   */
  if (!append_header_padding (message))
    _dbus_assert_not_reached ("failed to reappend header padding");
  
  return FALSE;
}

static dbus_bool_t
append_string_field (DBusMessage *message,
                     int          field,
                     int          type,
                     const char  *value)
{
  int value_len;

  value_len = strlen (value);

  return append_string_field_len (message, field, type, value, value_len);
}

static int
get_type_alignment (int type)
{
  int alignment;
  
  switch (type)
    {
    case DBUS_TYPE_NIL:
    case DBUS_TYPE_BYTE:
    case DBUS_TYPE_BOOLEAN:
      alignment = 0;
      break;

    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT32:
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
      /* These are aligned 4 because they have a length as the
       * first field;
       */
    case DBUS_TYPE_CUSTOM:
    case DBUS_TYPE_DICT:
      alignment = 4;
      break;

    case DBUS_TYPE_INT64:
    case DBUS_TYPE_UINT64:
    case DBUS_TYPE_DOUBLE:
      alignment = 8;
      break;

    case DBUS_TYPE_ARRAY:
      _dbus_assert_not_reached ("passed an ARRAY type to get_type_alignment()");
      alignment = 0; /* quiet gcc */
      break;

    case DBUS_TYPE_INVALID:
    default:
      _dbus_assert_not_reached ("passed an invalid or unknown type to get_type_alignment()");
      alignment = 0; /* quiet gcc */
      break;
    }

  return alignment;
}

static dbus_bool_t
iterate_one_field (const DBusString *str,
                   int               byte_order,
                   int               name_offset,
                   int              *next_offset_p,
                   int              *field_name_p,
                   DBusString       *append_copy_to,
                   int              *copy_name_offset_p,
                   int              *copy_value_offset_p)
{
  int name, type, array_type;
  int alignment;
  int type_len;
  int type_pos;
  int value_pos;
  int value_len;
  int value_end;
  int pos;

#if 0
  _dbus_verbose ("%s: name_offset=%d, append to %p\n",
                 _DBUS_FUNCTION_NAME, name_offset, append_copy_to);
#endif
  
  pos = name_offset;
  
  name = _dbus_string_get_byte (str, name_offset);
  pos++;

  type_pos = pos;
  type = _dbus_string_get_byte (str, type_pos);
  pos++;
  type_len = 1;

  array_type = type;
  /* find out the type of our array */
  while (array_type == DBUS_TYPE_ARRAY)
    {
      pos++;
      type_len++;
      array_type = _dbus_string_get_byte (str, pos);
    }

#if 0
  _dbus_verbose ("%s: name %s, type '%c' %d at %d len %d, array type '%c' %d\n",
                 _DBUS_FUNCTION_NAME,
                 _dbus_header_field_to_string (name),
                 type, type, type_pos, type_len, array_type, array_type);
#endif
  
#ifndef DBUS_DISABLE_ASSERT
  if (!_dbus_type_is_valid (array_type))
    {
      _dbus_warn ("type '%c' %d is not valid in %s\n",
                  array_type, array_type, _DBUS_FUNCTION_NAME);
      _dbus_assert_not_reached ("invalid type");
    }
#endif
      
  alignment = get_type_alignment (array_type);
      
  if (alignment > 0)
    pos = _DBUS_ALIGN_VALUE (pos, alignment);

  _dbus_verbose ("%s: alignment %d value at pos %d\n",
                 _DBUS_FUNCTION_NAME, alignment, pos);
  
  /* pos now points to our value */
  if (!_dbus_marshal_get_arg_end_pos (str, byte_order,
                                      type, pos, &value_end))
    _dbus_assert_not_reached ("failed to get the byte after this header");

  value_pos = pos;
  value_len = value_end - value_pos;

  _dbus_verbose ("%s: value_pos %d value_len %d value_end %d\n",
                 _DBUS_FUNCTION_NAME, value_pos, value_len, value_end);
  
  if (next_offset_p)
    *next_offset_p = pos + value_len;

  if (field_name_p)
    *field_name_p = name;

  if (append_copy_to)
    {
      int orig_len;

      orig_len = _dbus_string_get_length (append_copy_to);

      if (copy_name_offset_p)
        *copy_name_offset_p = orig_len;
      
      if (!_dbus_string_append_byte (append_copy_to, name))
        goto failed_copy;

      if (!_dbus_string_copy_len (str, type_pos, type_len,
                                  append_copy_to,
                                  _dbus_string_get_length (append_copy_to)))
        goto failed_copy;

      if (!_dbus_string_align_length (append_copy_to, alignment))
        goto failed_copy;

      if (copy_value_offset_p)
        *copy_value_offset_p = _dbus_string_get_length (append_copy_to);
      
      if (!_dbus_string_copy_len (str, value_pos, value_len,
                                  append_copy_to,
                                  _dbus_string_get_length (append_copy_to)))
        goto failed_copy;
      
      return TRUE;

    failed_copy:
      _dbus_verbose ("%s: Failed copying old fields to new string\n",
                     _DBUS_FUNCTION_NAME);
      _dbus_string_set_length (append_copy_to, orig_len);
      return FALSE;
    }
  else
    return TRUE;
}

#ifndef DBUS_DISABLE_ASSERT
static void
verify_header_fields (DBusMessage *message)
{
  int i;
  i = 0;
  while (i <= DBUS_HEADER_FIELD_LAST)
    {
      if (message->header_fields[i].name_offset >= 0)
        _dbus_assert (_dbus_string_get_byte (&message->header,
                                             message->header_fields[i].name_offset) ==
                      i);
      ++i;
    }
}
#else /* DBUS_DISABLE_ASSERT */
#define verify_header_fields(x)
#endif /* DBUS_DISABLE_ASSERT */

/* In this function we delete one field and re-align all the fields
 * following it.
 */
static dbus_bool_t
delete_one_and_re_align (DBusMessage *message,
                         int          name_offset_to_delete)
{
  DBusString copy;
  int new_fields_front_padding;
  int next_offset;
  int field_name;
  dbus_bool_t retval;
  HeaderField new_header_fields[DBUS_HEADER_FIELD_LAST+1];
  
  _dbus_assert (name_offset_to_delete < _dbus_string_get_length (&message->header));
  verify_header_fields (message);

  _dbus_verbose ("%s: Deleting one field at offset %d\n",
                 _DBUS_FUNCTION_NAME,
                 name_offset_to_delete);
  
  retval = FALSE;

  clear_header_padding (message);
  
  if (!_dbus_string_init_preallocated (&copy,
                                       _dbus_string_get_length (&message->header) -
                                       name_offset_to_delete + 8))
    {
      _dbus_verbose ("%s: Failed to init string to hold copy of fields\n",
                     _DBUS_FUNCTION_NAME);
      goto out_0;
    }
  
  /* Align the name offset of the first field in the same way it's
   * aligned in the real header
   */
  new_fields_front_padding = name_offset_to_delete % 8;

  if (!_dbus_string_insert_bytes (&copy, 0, new_fields_front_padding,
                                  '\0'))
    _dbus_assert_not_reached ("Should not have failed to insert bytes into preallocated string\n");

  memcpy (new_header_fields, message->header_fields,
          sizeof (new_header_fields));
  
  /* Now just re-marshal each field in the header to our temporary
   * buffer, skipping the first one. The tricky part is that the
   * fields are padded as if for previous_name_offset, but are in fact
   * at unaligned_name_offset
   */

  if (!iterate_one_field (&message->header,
                          message->byte_order,
                          name_offset_to_delete,
                          &next_offset,
                          &field_name, NULL, NULL, NULL))
    _dbus_assert_not_reached ("shouldn't have failed to alloc memory to skip the deleted field");

  _dbus_verbose ("  Skipping %s field which will be deleted; next_offset = %d\n",
                 _dbus_header_field_to_string (field_name), next_offset);
  
  /* This field no longer exists */
  if (field_name <= DBUS_HEADER_FIELD_LAST)
    {
      new_header_fields[field_name].name_offset = -1;
      new_header_fields[field_name].value_offset = -1;
    }
  
  while (next_offset < _dbus_string_get_length (&message->header))
    {
      int copy_name_offset;
      int copy_value_offset;

      if (!iterate_one_field (&message->header,
                              message->byte_order,
                              next_offset,
                              &next_offset,
                              &field_name,
                              &copy,
                              &copy_name_offset,
                              &copy_value_offset))
        {
          _dbus_verbose ("%s: OOM iterating one field\n",
                         _DBUS_FUNCTION_NAME);
          goto out_1;
        }
      
      if (field_name <= DBUS_HEADER_FIELD_LAST)
        {
          new_header_fields[field_name].name_offset = copy_name_offset - new_fields_front_padding + name_offset_to_delete;
          new_header_fields[field_name].value_offset = copy_value_offset - new_fields_front_padding + name_offset_to_delete;
          
          _dbus_verbose ("  Re-adding %s field at name_offset = %d value_offset = %d; next_offset = %d\n",
                         _dbus_header_field_to_string (field_name),
                         new_header_fields[field_name].name_offset,
                         new_header_fields[field_name].value_offset,
                         next_offset);
        }
    }

  if (!_dbus_string_replace_len (&copy,
                                 new_fields_front_padding,
                                 _dbus_string_get_length (&copy) - new_fields_front_padding,
                                 &message->header,
                                 name_offset_to_delete,
                                 _dbus_string_get_length (&message->header) - name_offset_to_delete))
    {
      _dbus_verbose ("%s: OOM moving copy back into header\n",
                     _DBUS_FUNCTION_NAME);
      goto out_1;
    }
  
  memcpy (message->header_fields, new_header_fields,
          sizeof (new_header_fields));
  verify_header_fields (message);
  
  retval = TRUE;
  
 out_1:
  _dbus_string_free (&copy);
  
 out_0:
  if (!append_header_padding (message))
    _dbus_assert_not_reached ("Failed to re-append header padding in re_align_field_recurse()");
  
  return retval;
}

static dbus_bool_t
delete_field (DBusMessage *message,
	      int          field,
              int          prealloc_header_space)
{
  int offset;

  _dbus_assert (!message->locked);

  /* Prealloc */
  if (!_dbus_string_lengthen (&message->header, prealloc_header_space))
    {
      _dbus_verbose ("failed to prealloc %d bytes header space\n",
                     prealloc_header_space);
      return FALSE;
    }
  _dbus_string_shorten (&message->header, prealloc_header_space);

  /* Delete */
  offset = message->header_fields[field].name_offset;
  if (offset < 0)
    {
      _dbus_verbose ("header field didn't exist, no need to delete\n");
      return TRUE; /* field didn't exist */
    }

  return delete_one_and_re_align (message, offset);
}

#ifdef DBUS_BUILD_TESTS
static dbus_bool_t
set_int_field (DBusMessage *message,
               int          field,
               int          value)
{
  int offset = message->header_fields[field].value_offset;

  _dbus_assert (!message->locked);

  _dbus_verbose ("set_int_field() field %d value '%d'\n",
                 field, value);
  
  if (offset < 0)
    {
      /* need to append the field */
      return append_int_field (message, field, value);
    }
  else
    {
      _dbus_marshal_set_int32 (&message->header,
                               message->byte_order,
                               offset, value);

      return TRUE;
    }
}
#endif

static dbus_bool_t
set_uint_field (DBusMessage  *message,
                int           field,
                dbus_uint32_t value)
{
  int offset = message->header_fields[field].value_offset;

  _dbus_assert (!message->locked);

  _dbus_verbose ("set_uint_field() field %d value '%u'\n",
                 field, value);
  
  if (offset < 0)
    {
      /* need to append the field */
      return append_uint_field (message, field, value);
    }
  else
    {
      _dbus_marshal_set_uint32 (&message->header,
                                message->byte_order,
                                offset, value);

      return TRUE;
    }
}

static dbus_bool_t
set_string_field (DBusMessage *message,
                  int          field,
                  int          type,
                  const char  *value)
{
  int prealloc;
  int value_len;
  
  _dbus_assert (!message->locked);

  value_len = value ? strlen (value) : 0;
  
  /* the prealloc is so the append_string_field()
   * below won't fail, leaving us in inconsistent state
   */
  prealloc = value_len + MAX_BYTES_OVERHEAD_TO_APPEND_A_STRING;

  _dbus_verbose ("set_string_field() field %d prealloc %d value '%s'\n",
                 field, prealloc, value);
  
  if (!delete_field (message, field, prealloc))
    return FALSE;

  if (value != NULL)
    {
      /* need to append the field */
      if (!append_string_field_len (message, field, type, value, value_len))
        _dbus_assert_not_reached ("Appending string field shouldn't have failed, due to preallocation");
    }
  
  return TRUE;
}

/**
 * Sets the serial number of a message. 
 * This can only be done once on a message.
 * 
 * @param message the message
 * @param serial the serial
 */
void
_dbus_message_set_serial (DBusMessage  *message,
                          dbus_int32_t  serial)
{
  _dbus_assert (!message->locked);
  _dbus_assert (dbus_message_get_serial (message) == 0);

  _dbus_marshal_set_uint32 (&message->header,
                            message->byte_order,
                            CLIENT_SERIAL_OFFSET,
			    serial);

  message->client_serial = serial;
}

/**
 * Sets the reply serial of a message (the client serial
 * of the message this is a reply to).
 *
 * @param message the message
 * @param reply_serial the client serial
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_reply_serial (DBusMessage   *message,
                               dbus_uint32_t  reply_serial)
{
  _dbus_assert (!message->locked);

  if (set_uint_field (message,
		      DBUS_HEADER_FIELD_REPLY_SERIAL,
                      reply_serial))
    {
      message->reply_serial = reply_serial;
      return TRUE;
    }
  else
    return FALSE;
}

/**
 * Returns the serial of a message or 0 if none has been specified.
 * The message's serial number is provided by the application sending
 * the message and is used to identify replies to this message.  All
 * messages received on a connection will have a serial, but messages
 * you haven't sent yet may return 0.
 *
 * @param message the message
 * @returns the client serial
 */
dbus_uint32_t
dbus_message_get_serial (DBusMessage *message)
{
  return message->client_serial;
}

/**
 * Returns the serial that the message is a reply to or 0 if none.
 *
 * @param message the message
 * @returns the reply serial
 */
dbus_uint32_t
dbus_message_get_reply_serial  (DBusMessage *message)
{
  return message->reply_serial;
}

/**
 * Adds a counter to be incremented immediately with the
 * size of this message, and decremented by the size
 * of this message when this message if finalized.
 * The link contains a counter with its refcount already
 * incremented, but the counter itself not incremented.
 * Ownership of link and counter refcount is passed to
 * the message.
 *
 * @param message the message
 * @param link link with counter as data
 */
void
_dbus_message_add_size_counter_link (DBusMessage  *message,
                                     DBusList     *link)
{
  /* right now we don't recompute the delta when message
   * size changes, and that's OK for current purposes
   * I think, but could be important to change later.
   * Do recompute it whenever there are no outstanding counters,
   * since it's basically free.
   */
  if (message->size_counters == NULL)
    {
      message->size_counter_delta =
        _dbus_string_get_length (&message->header) +
        _dbus_string_get_length (&message->body);
      
#if 0
      _dbus_verbose ("message has size %ld\n",
                     message->size_counter_delta);
#endif
    }
  
  _dbus_list_append_link (&message->size_counters, link);
  
  _dbus_counter_adjust (link->data, message->size_counter_delta);
}

/**
 * Adds a counter to be incremented immediately with the
 * size of this message, and decremented by the size
 * of this message when this message if finalized.
 *
 * @param message the message
 * @param counter the counter
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_message_add_size_counter (DBusMessage *message,
                                DBusCounter *counter)
{
  DBusList *link;

  link = _dbus_list_alloc_link (counter);
  if (link == NULL)
    return FALSE;

  _dbus_counter_ref (counter);
  _dbus_message_add_size_counter_link (message, link);

  return TRUE;
}

/**
 * Removes a counter tracking the size of this message, and decrements
 * the counter by the size of this message.
 *
 * @param message the message
 * @param link_return return the link used
 * @param counter the counter
 */
void
_dbus_message_remove_size_counter (DBusMessage  *message,
                                   DBusCounter  *counter,
                                   DBusList    **link_return)
{
  DBusList *link;

  link = _dbus_list_find_last (&message->size_counters,
                               counter);
  _dbus_assert (link != NULL);

  _dbus_list_unlink (&message->size_counters,
                     link);
  if (link_return)
    *link_return = link;
  else
    _dbus_list_free_link (link);

  _dbus_counter_adjust (counter, - message->size_counter_delta);

  _dbus_counter_unref (counter);
}

static dbus_bool_t
dbus_message_create_header (DBusMessage *message,
                            int          type,
                            const char  *service,
                            const char  *path,
                            const char  *interface,
                            const char  *member,
                            const char  *error_name)
{
  unsigned int flags;

  _dbus_assert ((interface && member) ||
                (error_name) ||
                !(interface || member || error_name));
  _dbus_assert (error_name == NULL || is_valid_error_name (error_name));
  
  if (!_dbus_string_append_byte (&message->header, message->byte_order))
    return FALSE;

  if (!_dbus_string_append_byte (&message->header, type))
    return FALSE;
  
  flags = 0;
  if (!_dbus_string_append_byte (&message->header, flags))
    return FALSE;

  if (!_dbus_string_append_byte (&message->header, DBUS_MAJOR_PROTOCOL_VERSION))
    return FALSE;

  /* header length */
  if (!_dbus_marshal_uint32 (&message->header, message->byte_order, 0))
    return FALSE;

  /* body length */
  if (!_dbus_marshal_uint32 (&message->header, message->byte_order, 0))
    return FALSE;

  /* serial */
  if (!_dbus_marshal_uint32 (&message->header, message->byte_order, 0))
    return FALSE;
  
  /* Marshal all the fields (Marshall Fields?) */
  
  if (path != NULL)
    {
      if (!append_string_field (message,
                                DBUS_HEADER_FIELD_PATH,
				DBUS_TYPE_OBJECT_PATH,
                                path))
        return FALSE;
    }
  
  if (service != NULL)
    {
      if (!append_string_field (message,
                                DBUS_HEADER_FIELD_DESTINATION,
				DBUS_TYPE_STRING,
                                service))
        return FALSE;
    }

  if (interface != NULL)
    {
      if (!append_string_field (message,
                                DBUS_HEADER_FIELD_INTERFACE,
				DBUS_TYPE_STRING,
                                interface))
        return FALSE;
    }

  if (member != NULL)
    {
      if (!append_string_field (message,
                                DBUS_HEADER_FIELD_MEMBER,
				DBUS_TYPE_STRING,
                                member))
        return FALSE;
    }

  if (error_name != NULL)
    {
      if (!append_string_field (message,
                                DBUS_HEADER_FIELD_ERROR_NAME,
				DBUS_TYPE_STRING,
                                error_name))
        return FALSE;
    }

  /* @todo if we make signature optional when body is empty, we don't
   * need to do this here.
   */
  if (!append_string_field (message,
                            DBUS_HEADER_FIELD_SIGNATURE,
                            DBUS_TYPE_STRING,
                            ""))
    return FALSE;
  
  return TRUE;
}

/**
 * Locks a message. Allows checking that applications don't keep a
 * reference to a message in the outgoing queue and change it
 * underneath us. Messages are locked when they enter the outgoing
 * queue (dbus_connection_send_message()), and the library complains
 * if the message is modified while locked.
 *
 * @param message the message to lock.
 */
void
_dbus_message_lock (DBusMessage  *message)
{
  if (!message->locked)
    {
      /* Fill in our lengths */
      _dbus_marshal_set_uint32 (&message->header,
				message->byte_order,
				HEADER_LENGTH_OFFSET,
				_dbus_string_get_length (&message->header));

      _dbus_marshal_set_uint32 (&message->header,
				message->byte_order,
				BODY_LENGTH_OFFSET,
				_dbus_string_get_length (&message->body));

      message->locked = TRUE;
    }
}

/** @} */

/**
 * @defgroup DBusMessage DBusMessage
 * @ingroup  DBus
 * @brief Message to be sent or received over a DBusConnection.
 *
 * A DBusMessage is the most basic unit of communication over a
 * DBusConnection. A DBusConnection represents a stream of messages
 * received from a remote application, and a stream of messages
 * sent to a remote application.
 *
 * @{
 */

/**
 * Sets the signature of the message, i.e. the arguments in the
 * message payload. The signature includes only "in" arguments for
 * #DBUS_MESSAGE_TYPE_METHOD_CALL and only "out" arguments for
 * #DBUS_MESSAGE_TYPE_METHOD_RETURN, so is slightly different from
 * what you might expect (it does not include the signature of the
 * entire C++-style method).
 *
 * The signature is a string made up of type codes such
 * as #DBUS_TYPE_STRING. The string is terminated with nul
 * (nul is also the value of #DBUS_TYPE_INVALID).
 * 
 * @param message the message
 * @param signature the type signature or #NULL to unset
 * @returns #FALSE if no memory
 */
static dbus_bool_t
dbus_message_set_signature (DBusMessage *message,
                            const char  *signature)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);
  
  return set_string_field (message,
                           DBUS_HEADER_FIELD_SIGNATURE,
                           DBUS_TYPE_STRING,
                           signature);
}

/**
 * Appends to the signature of the message.
 * (currently a static function, maybe should be public?)
 * 
 * @param message the message
 * @param append_bytes nul-terminated bytes to append to the type signature
 * @returns #FALSE if no memory
 */
static dbus_bool_t
dbus_message_append_to_signature (DBusMessage *message,
                                  const char  *append_bytes)
{
  const char *signature;
  DBusString append_str;
  dbus_bool_t retval;
  
  _dbus_return_val_if_fail (append_bytes != NULL, FALSE);
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);

  retval = FALSE;
  
  /* FIXME Just really inefficient for the moment; later we could
   * speed it up a lot by poking more directly at the header data
   */
  signature = dbus_message_get_signature (message);

  if (!_dbus_string_init (&append_str))
    return FALSE;

  if (signature && !_dbus_string_append (&append_str, signature))
    goto out;

  if (!_dbus_string_append (&append_str, append_bytes))
    goto out;
  
  if (!set_string_field (message,
                         DBUS_HEADER_FIELD_SIGNATURE,
                         DBUS_TYPE_STRING,
                         _dbus_string_get_const_data (&append_str)))
    goto out;

  retval = TRUE;
  
 out:
  
  _dbus_string_free (&append_str);

  return retval;
}

/**
 * Appends one byte to the signature of the message.
 * Internal function.
 * 
 * @param message the message
 * @param append_byte the byte 
 * @returns #FALSE if no memory
 */
static dbus_bool_t
_dbus_message_append_byte_to_signature (DBusMessage  *message,
                                        unsigned char append_byte)
{
  char buf[2];
  
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);
  
  buf[0] = append_byte;
  buf[1] = '\0';

  return dbus_message_append_to_signature (message, buf);
}

/**
 * Removes the last byte from the signature of the message.
 * Internal function.
 * 
 * @param message the message
 */
static void
_dbus_message_remove_byte_from_signature (DBusMessage  *message)
{
  const char *signature;
  
  _dbus_return_if_fail (message != NULL);
  _dbus_return_if_fail (!message->locked);

  signature = dbus_message_get_signature (message);

  _dbus_return_if_fail (signature != NULL);
  
  if (!delete_field (message,
                     DBUS_HEADER_FIELD_SIGNATURE,
                     0))
    _dbus_assert_not_reached ("failed to delete signature field");

  /* reappend one shorter (could this be any less efficient? the code will
   * go away later anyhow)
   */
  if (!append_string_field_len (message, DBUS_HEADER_FIELD_SIGNATURE,
                                DBUS_TYPE_STRING, signature,
                                strlen (signature) - 1))
    _dbus_assert_not_reached ("reappending shorter signature shouldn't have failed");
}

/**
 * @typedef DBusMessage
 *
 * Opaque data type representing a message received from or to be
 * sent to another application.
 */

static void
free_size_counter (void *element,
                   void *data)
{
  DBusCounter *counter = element;
  DBusMessage *message = data;
  
  _dbus_counter_adjust (counter, - message->size_counter_delta);

  _dbus_counter_unref (counter);
}

static void
dbus_message_finalize (DBusMessage *message)
{
  _dbus_assert (message->refcount.value == 0);
  
  /* This calls application callbacks! */
  _dbus_data_slot_list_free (&message->slot_list);
  
  _dbus_list_foreach (&message->size_counters,
                      free_size_counter, message);
  _dbus_list_clear (&message->size_counters);
  
  _dbus_string_free (&message->header);
  _dbus_string_free (&message->body);
  
  dbus_free (message);
}

/* Message Cache
 * 
 * We cache some DBusMessage to reduce the overhead of allocating
 * them.  In my profiling this consistently made about an 8%
 * difference.  It avoids the malloc for the message, the malloc for
 * the slot list, the malloc for the header string and body string,
 * and the associated free() calls. It does introduce another global
 * lock which could be a performance issue in certain cases.
 *
 * For the echo client/server the round trip time goes from around
 * .000077 to .000069 with the message cache on my laptop. The sysprof
 * change is as follows (numbers are cumulative percentage):
 *
 *  with message cache implemented as array as it is now (0.000069 per):
 *    new_empty_header           1.46
 *      mutex_lock               0.56    # i.e. _DBUS_LOCK(message_cache)
 *      mutex_unlock             0.25
 *      self                     0.41
 *    unref                      2.24
 *      self                     0.68
 *      list_clear               0.43
 *      mutex_lock               0.33    # i.e. _DBUS_LOCK(message_cache)
 *      mutex_unlock             0.25
 *
 *  with message cache implemented as list (0.000070 per roundtrip):
 *    new_empty_header           2.72
 *      list_pop_first           1.88
 *    unref                      3.3
 *      list_prepend             1.63
 *
 * without cache (0.000077 per roundtrip):
 *    new_empty_header           6.7
 *      string_init_preallocated 3.43
 *        dbus_malloc            2.43
 *      dbus_malloc0             2.59
 * 
 *    unref                      4.02
 *      string_free              1.82
 *        dbus_free              1.63
 *      dbus_free                0.71
 *
 * If you implement the message_cache with a list, the primary reason
 * it's slower is that you add another thread lock (on the DBusList
 * mempool).
 */

/* Avoid caching huge messages */
#define MAX_MESSAGE_SIZE_TO_CACHE _DBUS_ONE_MEGABYTE
/* Avoid caching too many messages */
#define MAX_MESSAGE_CACHE_SIZE    5

_DBUS_DEFINE_GLOBAL_LOCK (message_cache);
static DBusMessage *message_cache[MAX_MESSAGE_CACHE_SIZE];
static int message_cache_count = 0;
static dbus_bool_t message_cache_shutdown_registered = FALSE;

static void
dbus_message_cache_shutdown (void *data)
{
  int i;

  _DBUS_LOCK (message_cache);

  i = 0;
  while (i < MAX_MESSAGE_CACHE_SIZE)
    {
      if (message_cache[i])
        dbus_message_finalize (message_cache[i]);
      
      ++i;
    }

  message_cache_count = 0;
  message_cache_shutdown_registered = FALSE;
  
  _DBUS_UNLOCK (message_cache);
}

/**
 * Tries to get a message from the message cache.  The retrieved
 * message will have junk in it, so it still needs to be cleared out
 * in dbus_message_new_empty_header()
 *
 * @returns the message, or #NULL if none cached
 */
static DBusMessage*
dbus_message_get_cached (void)
{
  DBusMessage *message;
  int i;

  message = NULL;
  
  _DBUS_LOCK (message_cache);

  _dbus_assert (message_cache_count >= 0);
  
  if (message_cache_count == 0)
    {
      _DBUS_UNLOCK (message_cache);
      return NULL;
    }

  /* This is not necessarily true unless count > 0, and
   * message_cache is uninitialized until the shutdown is
   * registered
   */
  _dbus_assert (message_cache_shutdown_registered);
  
  i = 0;
  while (i < MAX_MESSAGE_CACHE_SIZE)
    {
      if (message_cache[i])
        {
          message = message_cache[i];
          message_cache[i] = NULL;
          message_cache_count -= 1;
          break;
        }
      ++i;
    }
  _dbus_assert (message_cache_count >= 0);
  _dbus_assert (i < MAX_MESSAGE_CACHE_SIZE);
  _dbus_assert (message != NULL);

  _DBUS_UNLOCK (message_cache);

  _dbus_assert (message->refcount.value == 0);
  _dbus_assert (message->size_counters == NULL);
  
  return message;
}

/**
 * Tries to cache a message, otherwise finalize it.
 *
 * @param message the message
 */
static void
dbus_message_cache_or_finalize (DBusMessage *message)
{
  dbus_bool_t was_cached;
  int i;

  _dbus_assert (message->refcount.value == 0);
  
  /* This calls application code and has to be done first thing
   * without holding the lock
   */
  _dbus_data_slot_list_clear (&message->slot_list);
  
  _dbus_list_foreach (&message->size_counters,
                      free_size_counter, message);
  _dbus_list_clear (&message->size_counters);
  
  was_cached = FALSE;

  _DBUS_LOCK (message_cache);

  if (!message_cache_shutdown_registered)
    {
      _dbus_assert (message_cache_count == 0);
      
      if (!_dbus_register_shutdown_func (dbus_message_cache_shutdown, NULL))
        goto out;

      i = 0;
      while (i < MAX_MESSAGE_CACHE_SIZE)
        {
          message_cache[i] = NULL;
          ++i;
        }
      
      message_cache_shutdown_registered = TRUE;
    }

  _dbus_assert (message_cache_count >= 0);
  
  if ((_dbus_string_get_length (&message->header) +
       _dbus_string_get_length (&message->body)) >
      MAX_MESSAGE_SIZE_TO_CACHE)
    goto out;
  
  if (message_cache_count >= MAX_MESSAGE_CACHE_SIZE)
    goto out;

  /* Find empty slot */
  i = 0;
  while (message_cache[i] != NULL)
    ++i;

  _dbus_assert (i < MAX_MESSAGE_CACHE_SIZE);

  _dbus_assert (message_cache[i] == NULL);
  message_cache[i] = message;
  message_cache_count += 1;
  was_cached = TRUE;
  
 out:
  _DBUS_UNLOCK (message_cache);

  if (!was_cached)
    dbus_message_finalize (message);
}

static DBusMessage*
dbus_message_new_empty_header (void)
{
  DBusMessage *message;
  int i;
  dbus_bool_t from_cache;

  message = dbus_message_get_cached ();

  if (message != NULL)
    {
      from_cache = TRUE;
    }
  else
    {
      from_cache = FALSE;
      message = dbus_new (DBusMessage, 1);
      if (message == NULL)
        return NULL;
#ifndef DBUS_DISABLE_CHECKS
      message->generation = _dbus_current_generation;
#endif
    }

  message->refcount.value = 1;
  message->byte_order = DBUS_COMPILER_BYTE_ORDER;
  message->client_serial = 0;
  message->reply_serial = 0;
  message->locked = FALSE;
  message->header_padding = 0;
  message->size_counters = NULL;
  message->size_counter_delta = 0;
  message->changed_stamp = 0;

  if (!from_cache)
    _dbus_data_slot_list_init (&message->slot_list);
  
  i = 0;
  while (i <= DBUS_HEADER_FIELD_LAST)
    {
      message->header_fields[i].name_offset  = -1;
      message->header_fields[i].value_offset = -1;
      ++i;
    }

  if (from_cache)
    {
      /* These can't fail since they shorten the string */
      _dbus_string_set_length (&message->header, 0);
      _dbus_string_set_length (&message->body, 0);
    }
  else
    {
      if (!_dbus_string_init_preallocated (&message->header, 32))
        {
          dbus_free (message);
          return NULL;
        }
      
      if (!_dbus_string_init_preallocated (&message->body, 32))
        {
          _dbus_string_free (&message->header);
          dbus_free (message);
          return NULL;
        }
    }
  
  return message;
}

/**
 * Constructs a new message of the given message type.
 * Types include #DBUS_MESSAGE_TYPE_METHOD_CALL,
 * #DBUS_MESSAGE_TYPE_SIGNAL, and so forth.
 *
 * @param message_type type of message
 * @returns new message or #NULL If no memory
 */
DBusMessage*
dbus_message_new (int message_type)
{
  DBusMessage *message;

  _dbus_return_val_if_fail (message_type != DBUS_MESSAGE_TYPE_INVALID, NULL);
  
  message = dbus_message_new_empty_header ();
  if (message == NULL)
    return NULL;
  
  if (!dbus_message_create_header (message,
                                   message_type,
                                   NULL, NULL, NULL, NULL, NULL))
    {
      dbus_message_unref (message);
      return NULL;
    }
  
  return message;
}

/**
 * Constructs a new message to invoke a method on a remote
 * object. Returns #NULL if memory can't be allocated for the
 * message. The service may be #NULL in which case no service is set;
 * this is appropriate when using D-BUS in a peer-to-peer context (no
 * message bus). The interface may be #NULL, which means that
 * if multiple methods with the given name exist it is undefined
 * which one will be invoked.
  *
 * @param service service that the message should be sent to or #NULL
 * @param path object path the message should be sent to
 * @param interface interface to invoke method on
 * @param method method to invoke
 * 
 * @returns a new DBusMessage, free with dbus_message_unref()
 * @see dbus_message_unref()
 */
DBusMessage*
dbus_message_new_method_call (const char *service,
                              const char *path,
                              const char *interface,
                              const char *method)
{
  DBusMessage *message;

  _dbus_return_val_if_fail (path != NULL, NULL);
  _dbus_return_val_if_fail (method != NULL, NULL);
  
  message = dbus_message_new_empty_header ();
  if (message == NULL)
    return NULL;
  
  if (!dbus_message_create_header (message,
                                   DBUS_MESSAGE_TYPE_METHOD_CALL,
                                   service, path, interface, method, NULL))
    {
      dbus_message_unref (message);
      return NULL;
    }
  
  return message;
}

/**
 * Constructs a message that is a reply to a method call. Returns
 * #NULL if memory can't be allocated for the message.
 *
 * @param method_call the message which the created
 * message is a reply to.
 * @returns a new DBusMessage, free with dbus_message_unref()
 * @see dbus_message_new_method_call(), dbus_message_unref()
 */ 
DBusMessage*
dbus_message_new_method_return (DBusMessage *method_call)
{
  DBusMessage *message;
  const char *sender;

  _dbus_return_val_if_fail (method_call != NULL, NULL);
  
  sender = get_string_field (method_call,
                             DBUS_HEADER_FIELD_SENDER,
			     NULL);
  
  /* sender is allowed to be null here in peer-to-peer case */

  message = dbus_message_new_empty_header ();
  if (message == NULL)
    return NULL;
  
  if (!dbus_message_create_header (message,
                                   DBUS_MESSAGE_TYPE_METHOD_RETURN,
                                   sender, NULL, NULL, NULL, NULL))
    {
      dbus_message_unref (message);
      return NULL;
    }

  dbus_message_set_no_reply (message, TRUE);

  if (!dbus_message_set_reply_serial (message,
                                      dbus_message_get_serial (method_call)))
    {
      dbus_message_unref (message);
      return NULL;
    }

  return message;
}

/**
 * Constructs a new message representing a signal emission. Returns
 * #NULL if memory can't be allocated for the message.
 * A signal is identified by its originating interface, and
 * the name of the signal.
 *
 * @param path the path to the object emitting the signal
 * @param interface the interface the signal is emitted from
 * @param name name of the signal
 * @returns a new DBusMessage, free with dbus_message_unref()
 * @see dbus_message_unref()
 */
DBusMessage*
dbus_message_new_signal (const char *path,
                         const char *interface,
                         const char *name)
{
  DBusMessage *message;

  _dbus_return_val_if_fail (path != NULL, NULL);
  _dbus_return_val_if_fail (interface != NULL, NULL);
  _dbus_return_val_if_fail (name != NULL, NULL);
  
  message = dbus_message_new_empty_header ();
  if (message == NULL)
    return NULL;
  
  if (!dbus_message_create_header (message,
                                   DBUS_MESSAGE_TYPE_SIGNAL,
                                   NULL, path, interface, name, NULL))
    {
      dbus_message_unref (message);
      return NULL;
    }

  dbus_message_set_no_reply (message, TRUE);
  
  return message;
}

/**
 * Creates a new message that is an error reply to a certain message.
 * Error replies are possible in response to method calls primarily.
 *
 * @param reply_to the original message
 * @param error_name the error name
 * @param error_message the error message string or #NULL for none
 * @returns a new error message
 */
DBusMessage*
dbus_message_new_error (DBusMessage *reply_to,
                        const char  *error_name,
                        const char  *error_message)
{
  DBusMessage *message;
  const char *sender;
  DBusMessageIter iter;

  _dbus_return_val_if_fail (reply_to != NULL, NULL);
  _dbus_return_val_if_fail (error_name != NULL, NULL);
  _dbus_return_val_if_fail (is_valid_error_name (error_name), NULL);
  
  sender = get_string_field (reply_to,
                             DBUS_HEADER_FIELD_SENDER,
			     NULL);

  /* sender may be NULL for non-message-bus case or
   * when the message bus is dealing with an unregistered
   * connection.
   */
  message = dbus_message_new_empty_header ();
  if (message == NULL)
    return NULL;
  
  if (!dbus_message_create_header (message,
                                   DBUS_MESSAGE_TYPE_ERROR,
                                   sender, NULL, NULL, NULL, error_name))
    {
      dbus_message_unref (message);
      return NULL;
    }

  dbus_message_set_no_reply (message, TRUE);
  
  if (!dbus_message_set_reply_serial (message,
                                      dbus_message_get_serial (reply_to)))
    {
      dbus_message_unref (message);
      return NULL;
    }

  if (error_message != NULL)
    {
      dbus_message_append_iter_init (message, &iter);
      if (!dbus_message_iter_append_string (&iter, error_message))
        {
          dbus_message_unref (message);
          return NULL;
        }
    }
  
  return message;
}

/**
 * Creates a new message that is an error reply to a certain message.
 * Error replies are possible in response to method calls primarily.
 *
 * @param reply_to the original message
 * @param error_name the error name
 * @param error_format the error message string to be printed
 * @param ... value of first argument, list of additional values to print
 * @returns a new error message
 */
DBusMessage*
dbus_message_new_error_printf (DBusMessage *reply_to,
			       const char  *error_name,
			       const char  *error_format,
			       ...)
{
  va_list args;
  DBusString str;
  DBusMessage *message;

  _dbus_return_val_if_fail (reply_to != NULL, NULL);
  _dbus_return_val_if_fail (error_name != NULL, NULL);
  _dbus_return_val_if_fail (is_valid_error_name (error_name), NULL);
  
  if (!_dbus_string_init (&str))
    return NULL;

  va_start (args, error_format);

  if (_dbus_string_append_printf_valist (&str, error_format, args))
    message = dbus_message_new_error (reply_to, error_name,
				      _dbus_string_get_const_data (&str));
  else
    message = NULL;
  
  _dbus_string_free (&str);

  va_end (args);

  return message;
}


/**
 * Creates a new message that is an exact replica of the message
 * specified, except that its refcount is set to 1.
 *
 * @param message the message.
 * @returns the new message.
 */
DBusMessage *
dbus_message_copy (const DBusMessage *message)
{
  DBusMessage *retval;
  int i;

  _dbus_return_val_if_fail (message != NULL, NULL);
  
  retval = dbus_new0 (DBusMessage, 1);
  if (retval == NULL)
    return NULL;
  
  retval->refcount.value = 1;
  retval->byte_order = message->byte_order;
  retval->client_serial = message->client_serial;
  retval->reply_serial = message->reply_serial;
  retval->header_padding = message->header_padding;
  retval->locked = FALSE;
#ifndef DBUS_DISABLE_CHECKS
  retval->generation = message->generation;
#endif
  
  if (!_dbus_string_init_preallocated (&retval->header,
                                       _dbus_string_get_length (&message->header)))
    {
      dbus_free (retval);
      return NULL;
    }
  
  if (!_dbus_string_init_preallocated (&retval->body,
                                       _dbus_string_get_length (&message->body)))
    {
      _dbus_string_free (&retval->header);
      dbus_free (retval);
      return NULL;
    }
  
  if (!_dbus_string_copy (&message->header, 0,
			  &retval->header, 0))
    goto failed_copy;

  if (!_dbus_string_copy (&message->body, 0,
			  &retval->body, 0))
    goto failed_copy;
  
  for (i = 0; i <= DBUS_HEADER_FIELD_LAST; i++)
    {
      retval->header_fields[i] = message->header_fields[i];
    }
  
  return retval;

 failed_copy:
  _dbus_string_free (&retval->header);
  _dbus_string_free (&retval->body);
  dbus_free (retval);
  
  return NULL;  
}


/**
 * Increments the reference count of a DBusMessage.
 *
 * @param message The message
 * @returns the message
 * @see dbus_message_unref
 */
DBusMessage *
dbus_message_ref (DBusMessage *message)
{
  dbus_int32_t old_refcount;

  _dbus_return_val_if_fail (message != NULL, NULL);
  _dbus_return_val_if_fail (message->generation == _dbus_current_generation, NULL);
  
  old_refcount = _dbus_atomic_inc (&message->refcount);
  _dbus_assert (old_refcount >= 1);

  return message;
}

/**
 * Decrements the reference count of a DBusMessage.
 *
 * @param message The message
 * @see dbus_message_ref
 */
void
dbus_message_unref (DBusMessage *message)
{
 dbus_int32_t old_refcount;

  _dbus_return_if_fail (message != NULL);
  _dbus_return_if_fail (message->generation == _dbus_current_generation);
  
  old_refcount = _dbus_atomic_dec (&message->refcount);
  
  _dbus_assert (old_refcount >= 0);

  if (old_refcount == 1)
    {
      /* Calls application callbacks! */
      dbus_message_cache_or_finalize (message);
    }
}

/**
 * Gets the type of a message. Types include
 * DBUS_MESSAGE_TYPE_METHOD_CALL, DBUS_MESSAGE_TYPE_METHOD_RETURN,
 * DBUS_MESSAGE_TYPE_ERROR, DBUS_MESSAGE_TYPE_SIGNAL, but other types
 * are allowed and all code must silently ignore messages of unknown
 * type. DBUS_MESSAGE_TYPE_INVALID will never be returned, however.
 *
 *
 * @param message the message
 * @returns the type of the message
 */
int
dbus_message_get_type (DBusMessage *message)
{
  int type;

  type = _dbus_string_get_byte (&message->header, 1);
  _dbus_assert (type != DBUS_MESSAGE_TYPE_INVALID);

  return type;
}

/**
 * Sets the object path this message is being sent to (for
 * DBUS_MESSAGE_TYPE_METHOD_CALL) or the one a signal is being
 * emitted from (for DBUS_MESSAGE_TYPE_SIGNAL).
 *
 * @param message the message
 * @param object_path the path or #NULL to unset
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_path (DBusMessage   *message,
                       const char    *object_path)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);

  return set_string_field (message,
                           DBUS_HEADER_FIELD_PATH,
                           DBUS_TYPE_OBJECT_PATH,
                           object_path);
}

/**
 * Gets the object path this message is being sent to
 * (for DBUS_MESSAGE_TYPE_METHOD_CALL) or being emitted
 * from (for DBUS_MESSAGE_TYPE_SIGNAL).
 *
 * @param message the message
 * @returns the path (should not be freed)
 */
const char*
dbus_message_get_path (DBusMessage   *message)
{
  _dbus_return_val_if_fail (message != NULL, NULL);
  
  return get_string_field (message, DBUS_HEADER_FIELD_PATH, NULL);
}

/**
 * Gets the object path this message is being sent to
 * (for DBUS_MESSAGE_TYPE_METHOD_CALL) or being emitted
 * from (for DBUS_MESSAGE_TYPE_SIGNAL) in a decomposed
 * format (one array element per path component).
 * Free the returned array with dbus_free_string_array().
 *
 * An empty but non-NULL path array means the path "/".
 * So the path "/foo/bar" becomes { "foo", "bar", NULL }
 * and the path "/" becomes { NULL }.
 *
 * @param message the message
 * @param path place to store allocated array of path components; #NULL set here if no path field exists
 * @returns #FALSE if no memory to allocate the array
 */
dbus_bool_t
dbus_message_get_path_decomposed (DBusMessage   *message,
                                  char        ***path)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (path != NULL, FALSE);

  return get_path_field_decomposed (message,
				    DBUS_HEADER_FIELD_PATH,
                                    path);
}

/**
 * Sets the interface this message is being sent to
 * (for DBUS_MESSAGE_TYPE_METHOD_CALL) or
 * the interface a signal is being emitted from
 * (for DBUS_MESSAGE_TYPE_SIGNAL).
 *
 * @param message the message
 * @param interface the interface or #NULL to unset
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_interface (DBusMessage  *message,
                            const char   *interface)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);

  return set_string_field (message,
                           DBUS_HEADER_FIELD_INTERFACE,
                           DBUS_TYPE_STRING,
                           interface);
}

/**
 * Gets the interface this message is being sent to
 * (for DBUS_MESSAGE_TYPE_METHOD_CALL) or being emitted
 * from (for DBUS_MESSAGE_TYPE_SIGNAL).
 * The interface name is fully-qualified (namespaced).
 *
 * @param message the message
 * @returns the message interface (should not be freed)
 */
const char*
dbus_message_get_interface (DBusMessage *message)
{
  _dbus_return_val_if_fail (message != NULL, NULL);
  
  return get_string_field (message, DBUS_HEADER_FIELD_INTERFACE, NULL);
}

/**
 * Sets the interface member being invoked
 * (DBUS_MESSAGE_TYPE_METHOD_CALL) or emitted
 * (DBUS_MESSAGE_TYPE_SIGNAL).
 * The interface name is fully-qualified (namespaced).
 *
 * @param message the message
 * @param member the member or #NULL to unset
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_member (DBusMessage  *message,
                       const char   *member)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);

  return set_string_field (message,
                           DBUS_HEADER_FIELD_MEMBER,
                           DBUS_TYPE_STRING,
                           member);
}

/**
 * Gets the interface member being invoked
 * (DBUS_MESSAGE_TYPE_METHOD_CALL) or emitted
 * (DBUS_MESSAGE_TYPE_SIGNAL).
 * 
 * @param message the message
 * @returns the member name (should not be freed)
 */
const char*
dbus_message_get_member (DBusMessage *message)
{
  _dbus_return_val_if_fail (message != NULL, NULL);
  
  return get_string_field (message,
			   DBUS_HEADER_FIELD_MEMBER,
			   NULL);
}

/**
 * Sets the name of the error (DBUS_MESSAGE_TYPE_ERROR).
 * The name is fully-qualified (namespaced).
 *
 * @param message the message
 * @param error_name the name or #NULL to unset
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_error_name (DBusMessage  *message,
                             const char   *error_name)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);
  _dbus_return_val_if_fail (error_name == NULL || is_valid_error_name (error_name), FALSE);
  
  return set_string_field (message,
                           DBUS_HEADER_FIELD_ERROR_NAME,
                           DBUS_TYPE_STRING,
                           error_name);
}

/**
 * Gets the error name (DBUS_MESSAGE_TYPE_ERROR only).
 * 
 * @param message the message
 * @returns the error name (should not be freed)
 */
const char*
dbus_message_get_error_name (DBusMessage *message)
{
  _dbus_return_val_if_fail (message != NULL, NULL);
  
  return get_string_field (message,
			   DBUS_HEADER_FIELD_ERROR_NAME,
			   NULL);
}

/**
 * Sets the message's destination service.
 *
 * @param message the message
 * @param destination the destination service name or #NULL to unset
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_destination (DBusMessage  *message,
                              const char   *destination)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);

  return set_string_field (message,
                           DBUS_HEADER_FIELD_DESTINATION,
                           DBUS_TYPE_STRING,
                           destination);
}

/**
 * Gets the destination service of a message.
 * 
 * @param message the message
 * @returns the message destination service (should not be freed)
 */
const char*
dbus_message_get_destination (DBusMessage *message)
{
  _dbus_return_val_if_fail (message != NULL, NULL);
  
  return get_string_field (message,
			   DBUS_HEADER_FIELD_DESTINATION,
			   NULL);
}

/**
 * Appends fields to a message given a variable argument list. The
 * variable argument list should contain the type of the argument
 * followed by the value to add.  Array values are specified by an int
 * typecode followed by a pointer to the array followed by an int
 * giving the length of the array.  The argument list must be
 * terminated with #DBUS_TYPE_INVALID.
 *
 * This function doesn't support dicts or non-fundamental arrays.
 *
 * This function supports #DBUS_TYPE_INT64 and #DBUS_TYPE_UINT64
 * only if #DBUS_HAVE_INT64 is defined.
 *
 * @param message the message
 * @param first_arg_type type of the first argument
 * @param ... value of first argument, list of additional type-value pairs
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_append_args (DBusMessage *message,
			  int first_arg_type,
			  ...)
{
  dbus_bool_t retval;
  va_list var_args;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  
  va_start (var_args, first_arg_type);
  retval = dbus_message_append_args_valist (message,
					    first_arg_type,
					    var_args);
  va_end (var_args);

  return retval;
}

/* Swap into our byte order if message isn't already.
 * Done only when required, which allows the bus to avoid
 * ever doing this as it routes messages.
 */
static void
_dbus_message_ensure_our_byte_order (DBusMessage *message)
{
  if (message->byte_order == DBUS_COMPILER_BYTE_ORDER)
    return;

  


}

/**
 * Gets arguments from a message given a variable argument list.
 * The variable argument list should contain the type of the
 * argumen followed by a pointer to where the value should be
 * stored. The list is terminated with #DBUS_TYPE_INVALID.
 *
 * @param message the message
 * @param error error to be filled in on failure
 * @param first_arg_type the first argument type
 * @param ... location for first argument value, then list of type-location pairs
 * @returns #FALSE if the error was set
 */
dbus_bool_t
dbus_message_get_args (DBusMessage     *message,
                       DBusError       *error,
		       int              first_arg_type,
		       ...)
{
  dbus_bool_t retval;
  va_list var_args;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_error_is_set (error, FALSE);
  
  va_start (var_args, first_arg_type);
  retval = dbus_message_get_args_valist (message, error, first_arg_type, var_args);
  va_end (var_args);

  return retval;
}

/**
 * This function takes a va_list for use by language bindings
 *
 * @todo We need to free the argument data when an error occurs.
 *
 * @see dbus_message_get_args
 * @param message the message
 * @param error error to be filled in
 * @param first_arg_type type of the first argument
 * @param var_args return location for first argument, followed by list of type/location pairs
 * @returns #FALSE if error was set
 */
dbus_bool_t
dbus_message_get_args_valist (DBusMessage     *message,
                              DBusError       *error,
			      int              first_arg_type,
			      va_list          var_args)
{
  DBusMessageIter iter;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_error_is_set (error, FALSE);
  
  dbus_message_iter_init (message, &iter);
  return dbus_message_iter_get_args_valist (&iter, error, first_arg_type, var_args);
}

/**
 * Gets arguments from a message iterator given a variable argument list.
 * The variable argument list should contain the type of the
 * argumen followed by a pointer to where the value should be
 * stored. The list is terminated with 0.
 *
 * @param iter the message iterator 
 * @param error error to be filled in on failure
 * @param first_arg_type the first argument type
 * @param ... location for first argument value, then list of type-location pairs
 * @returns #FALSE if the error was set
 */
dbus_bool_t
dbus_message_iter_get_args (DBusMessageIter *iter,
			    DBusError       *error,
			    int              first_arg_type,
			    ...)
{
  dbus_bool_t retval;
  va_list var_args;

  _dbus_return_val_if_fail (iter != NULL, FALSE);
  _dbus_return_val_if_error_is_set (error, FALSE);
  
  va_start (var_args, first_arg_type);
  retval = dbus_message_iter_get_args_valist (iter, error, first_arg_type, var_args);
  va_end (var_args);

  return retval;
}

/**
 * Initializes a DBusMessageIter representing the arguments of the
 * message passed in.
 *
 * @param message the message
 * @param iter pointer to an iterator to initialize
 * @returns #FALSE if the message has no arguments
 */
dbus_bool_t
dbus_message_iter_init (DBusMessage     *message,
			DBusMessageIter *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (iter != NULL, FALSE);
  
  _dbus_assert (sizeof (DBusMessageRealIter) <= sizeof (DBusMessageIter));
  
  real->message = message;
  real->parent_iter = NULL;
  real->changed_stamp = message->changed_stamp;
  
  real->type = DBUS_MESSAGE_ITER_TYPE_MESSAGE;
  real->pos = 0;
  real->end = _dbus_string_get_length (&message->body);
  
  real->container_start = 0;
  real->container_length_pos = 0;
  real->wrote_dict_key = 0;
  real->array_type_pos = 0;

  return real->end > real->pos;
}

#ifndef DBUS_DISABLE_CHECKS
static dbus_bool_t
dbus_message_iter_check (DBusMessageRealIter *iter)
{
  if (iter == NULL)
    {
      _dbus_warn ("dbus iterator check failed: iterator is NULL\n");
      return FALSE;
    }
  
  if (iter->changed_stamp != iter->message->changed_stamp)
    {
      _dbus_warn ("dbus iterator check failed: invalid iterator, must re-initialize it after modifying the message\n");
      return FALSE;
    }
  
  if (iter->pos < 0 || iter->pos > iter->end)
    {
      _dbus_warn ("dbus iterator check failed: invalid position\n");
      return FALSE;
    }

  return TRUE;
}
#endif /* DBUS_DISABLE_CHECKS */

static int
skip_array_type (DBusMessageRealIter *iter, int pos)
{
  const char *data;

  do
    {
      data = _dbus_string_get_const_data_len (&iter->message->body,
					      pos++, 1);
    }
  while (*data == DBUS_TYPE_ARRAY);
  
  return pos;
}

/* FIXME what are these _dbus_type_is_valid() checks for?
 * haven't we validated the message?
 */
static int
dbus_message_iter_get_data_start (DBusMessageRealIter *iter, int *type)
{
  const char *data;
  int pos, len;
  
  switch (iter->type)
    {
    case DBUS_MESSAGE_ITER_TYPE_MESSAGE:
      data = _dbus_string_get_const_data_len (&iter->message->body,
					      iter->pos, 1);
      if (_dbus_type_is_valid (*data))
	*type = *data;
      else
	*type = DBUS_TYPE_INVALID;
      
      return skip_array_type (iter, iter->pos);
      
    case DBUS_MESSAGE_ITER_TYPE_ARRAY:
      data = _dbus_string_get_const_data_len (&iter->message->body,
					      iter->array_type_pos, 1);
      if (_dbus_type_is_valid (*data))
	*type = *data;
      else
	*type = DBUS_TYPE_INVALID;
      
      return iter->pos;
      
    case DBUS_MESSAGE_ITER_TYPE_DICT:
      /* Get the length of the string */
      len = _dbus_demarshal_uint32 (&iter->message->body,
				    iter->message->byte_order,
				    iter->pos, &pos);
      pos = pos + len + 1;

      data = _dbus_string_get_const_data_len (&iter->message->body,
					      pos, 1);
      if (_dbus_type_is_valid (*data))
	*type = *data;
      else
	*type = DBUS_TYPE_INVALID;

      return skip_array_type (iter, pos);
      
    default:
      _dbus_assert_not_reached ("Invalid iter type");
      break;
    }
  *type = DBUS_TYPE_INVALID;
  return iter->pos;
}


/**
 * Checks if an iterator has any more fields.
 *
 * @param iter the message iter
 * @returns #TRUE if there are more fields
 * following
 */
dbus_bool_t
dbus_message_iter_has_next (DBusMessageIter *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  int end_pos;
  int type, pos;

  _dbus_return_val_if_fail (dbus_message_iter_check (real), FALSE);

  if (real->pos >= real->end)
    return FALSE;
  
  pos = dbus_message_iter_get_data_start (real, &type);
  
  if (!_dbus_marshal_get_arg_end_pos (&real->message->body,
                                      real->message->byte_order,
				      type, pos, &end_pos))
    return FALSE;
  
  if (end_pos >= real->end)
    return FALSE;

  return TRUE;  
}

/**
 * Moves the iterator to the next field.
 *
 * @param iter The message iter
 * @returns #TRUE if the iterator was moved to the next field
 */
dbus_bool_t
dbus_message_iter_next (DBusMessageIter *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  int end_pos;
  int type, pos;

  _dbus_return_val_if_fail (dbus_message_iter_check (real), FALSE);

  pos = dbus_message_iter_get_data_start (real, &type);
  
  if (!_dbus_marshal_get_arg_end_pos (&real->message->body,
                                      real->message->byte_order,
				      type, pos, &end_pos))
    return FALSE;

  if (end_pos >= real->end)
    return FALSE;

  real->pos = end_pos;

  return TRUE;
}

/**
 * Returns the argument type of the argument that the
 * message iterator points at.
 *
 * @param iter the message iter
 * @returns the field type
 */
int
dbus_message_iter_get_arg_type (DBusMessageIter *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  int type, pos;

  _dbus_return_val_if_fail (dbus_message_iter_check (real), DBUS_TYPE_INVALID);

  if (real->pos >= real->end)
    {
      _dbus_verbose ("  iterator at or beyond end of message\n");
      return DBUS_TYPE_INVALID;
    }

  pos = dbus_message_iter_get_data_start (real, &type);
  
  return type;
}

/* FIXME why do we validate the typecode in here, hasn't the message
 * already been verified?
 */
static int
iter_get_array_type (DBusMessageRealIter *iter, int *array_type_pos)
{
  const char *data;
  int _array_type_pos;
  int len, pos;
  
  switch (iter->type)
    {
    case DBUS_MESSAGE_ITER_TYPE_MESSAGE:
      _array_type_pos = iter->pos + 1;
      break;
    case DBUS_MESSAGE_ITER_TYPE_ARRAY:
      _array_type_pos = iter->array_type_pos + 1;
      break;
    case DBUS_MESSAGE_ITER_TYPE_DICT:
      /* Get the length of the string */
      len = _dbus_demarshal_uint32 (&iter->message->body,
				    iter->message->byte_order,
				    iter->pos, &pos);
      pos = pos + len + 1;
      data = _dbus_string_get_const_data_len (&iter->message->body,
					      pos + 1, 1);
      _array_type_pos = pos + 1;
      break;
    default:
      _dbus_assert_not_reached ("wrong iter type");
      return DBUS_TYPE_INVALID;
    }

  if (array_type_pos != NULL)
    *array_type_pos = _array_type_pos;
  
  data = _dbus_string_get_const_data_len (&iter->message->body,
					  _array_type_pos, 1);
  if (_dbus_type_is_valid (*data))
    return  *data;
  
  return DBUS_TYPE_INVALID;
}


/**
 * Returns the element type of the array that the
 * message iterator points at. Note that you need
 * to check that the iterator points to an array
 * prior to using this function.
 *
 * @param iter the message iter
 * @returns the field type
 */
int
dbus_message_iter_get_array_type (DBusMessageIter *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  int type, pos;

  _dbus_return_val_if_fail (dbus_message_iter_check (real), DBUS_TYPE_INVALID);

  if (real->pos >= real->end)
    return DBUS_TYPE_INVALID;

  pos = dbus_message_iter_get_data_start (real, &type);

  _dbus_assert (type == DBUS_TYPE_ARRAY);

  return iter_get_array_type (real, NULL);
}


/**
 * Returns the string value that an iterator may point to.
 * Note that you need to check that the iterator points to
 * a string value before using this function.
 *
 * @see dbus_message_iter_get_arg_type
 * @param iter the message iter
 * @returns the string
 */
char *
dbus_message_iter_get_string (DBusMessageIter *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  int type, pos;

  _dbus_return_val_if_fail (dbus_message_iter_check (real), NULL);
  pos = dbus_message_iter_get_data_start (real, &type);
  
  _dbus_assert (type == DBUS_TYPE_STRING);

  return _dbus_demarshal_string (&real->message->body, real->message->byte_order,
                                 pos, NULL);
}

/**
 * Returns the object path value that an iterator may point to.
 * Note that you need to check that the iterator points to
 * an object path value before using this function.
 *
 * @see dbus_message_iter_get_arg_type
 * @param iter the message iter
 * @returns the path
 */
char *
dbus_message_iter_get_object_path (DBusMessageIter  *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  int type, pos;

  _dbus_return_val_if_fail (dbus_message_iter_check (real), NULL);

  pos = dbus_message_iter_get_data_start (real, &type);
  
  _dbus_assert (type == DBUS_TYPE_OBJECT_PATH);

  return _dbus_demarshal_string (&real->message->body, real->message->byte_order,
                                 pos, NULL);
}

/**
 * Returns the name and data from a custom type that an iterator may
 * point to. Note that you need to check that the iterator points to a
 * custom type before using this function.
 *
 * @see dbus_message_iter_get_arg_type
 * @param iter the message iter
 * @param name return location for the name of the custom type
 * @param value return location for data
 * @param len return location for length of data
 * @returns TRUE if get succeed
 * 
 */
dbus_bool_t
dbus_message_iter_get_custom (DBusMessageIter   *iter,
                              char             **name,
                              unsigned char    **value,
                              int               *len)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  int type, pos;
  char *_name;

  _dbus_return_val_if_fail (dbus_message_iter_check (real), FALSE);

  pos = dbus_message_iter_get_data_start (real, &type);
  
  _dbus_assert (type == DBUS_TYPE_CUSTOM);
  
  _name = _dbus_demarshal_string (&real->message->body, real->message->byte_order,
				  pos, &pos);

  if (_name == NULL)
    return FALSE;
  
  if (!_dbus_demarshal_byte_array (&real->message->body, real->message->byte_order,
				   pos, NULL, value, len))
    {
      dbus_free (_name);
      return FALSE;
    }

  *name = _name;
  
  return TRUE;
}

static void
_dbus_message_iter_get_basic_type (DBusMessageIter *iter,
				   char             type,
				   void            *value)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  int item_type, pos;

  _dbus_return_if_fail (dbus_message_iter_check (real));

  pos = dbus_message_iter_get_data_start (real, &item_type);
  
  _dbus_assert (type == item_type);
  
  _dbus_demarshal_basic_type (&real->message->body,
			      type, value,
			      real->message->byte_order,
			      &pos);
}


/**
 * This function takes a va_list for use by language bindings
 *
 * This function supports #DBUS_TYPE_INT64 and #DBUS_TYPE_UINT64
 * only if #DBUS_HAVE_INT64 is defined.
 *
 * @todo this function (or some lower-level non-convenience function)
 * needs better error handling; should allow the application to
 * distinguish between out of memory, and bad data from the remote
 * app. It also needs to not leak a bunch of args when it gets
 * to the arg that's bad, as that would be a security hole
 * (allow one app to force another to leak memory)
 *
 * @todo We need to free the argument data when an error occurs.
 *
 * @see dbus_message_get_args
 * @param iter the message iter
 * @param error error to be filled in
 * @param first_arg_type type of the first argument
 * @param var_args return location for first argument, followed by list of type/location pairs
 * @returns #FALSE if error was set
 */
dbus_bool_t
dbus_message_iter_get_args_valist (DBusMessageIter *iter,
				   DBusError       *error,
				   int              first_arg_type,
				   va_list          var_args)
{
  int spec_type, msg_type, i;
  dbus_bool_t retval;

  _dbus_return_val_if_fail (iter != NULL, FALSE);
  _dbus_return_val_if_error_is_set (error, FALSE);

  retval = FALSE;
  
  spec_type = first_arg_type;
  i = 0;
  
  while (spec_type != DBUS_TYPE_INVALID)
    {
      msg_type = dbus_message_iter_get_arg_type (iter);      
      
      if (msg_type != spec_type)
	{
          dbus_set_error (error, DBUS_ERROR_INVALID_ARGS,
                          "Argument %d is specified to be of type \"%s\", but "
                          "is actually of type \"%s\"\n", i,
                          _dbus_type_to_string (spec_type),
                          _dbus_type_to_string (msg_type));

          goto out;
	}

      switch (spec_type)
	{
	case DBUS_TYPE_NIL:
	  break;
	case DBUS_TYPE_BOOLEAN:
	  {
	    dbus_bool_t *ptr;

	    ptr = va_arg (var_args, dbus_bool_t *);

	    *ptr = dbus_message_iter_get_boolean (iter);
	    break;
	  }
	case DBUS_TYPE_BYTE:
	case DBUS_TYPE_INT32:
	case DBUS_TYPE_UINT32:
#ifdef DBUS_HAVE_INT64
	case DBUS_TYPE_INT64:
	case DBUS_TYPE_UINT64:
#endif /* DBUS_HAVE_INT64 */
	case DBUS_TYPE_DOUBLE:
	  {
	    void *ptr = va_arg (var_args, double *);
	    _dbus_message_iter_get_basic_type (iter, spec_type, ptr);
	    break;
	  }

	case DBUS_TYPE_STRING:
	  {
	    char **ptr;

	    ptr = va_arg (var_args, char **);

	    *ptr = dbus_message_iter_get_string (iter);

	    if (!*ptr)
              {
                dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
                goto out;
              }
	    
	    break;
	  }

	  case DBUS_TYPE_OBJECT_PATH:
	  {
	    char **ptr;

	    ptr = va_arg (var_args, char **);

	    *ptr = dbus_message_iter_get_object_path (iter);

	    if (!*ptr)
	      {
	        dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
	        goto out;
	      }

	    break;
	  }

	case DBUS_TYPE_CUSTOM:
	  {
	    char **name;
	    unsigned char **data;
	    int *len;
 
	    name = va_arg (var_args, char **);
	    data = va_arg (var_args, unsigned char **);
	    len = va_arg (var_args, int *);

	    if (!dbus_message_iter_get_custom (iter, name, data, len))
	      {
                dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
		goto out;
	      }
	  }
	  break;
	case DBUS_TYPE_ARRAY:
	  {
	    void **data;
	    int *len, type;
	    dbus_bool_t err = FALSE;
 
	    type = va_arg (var_args, int);
	    data = va_arg (var_args, void *);
	    len = va_arg (var_args, int *);

            _dbus_return_val_if_fail (data != NULL, FALSE);
            _dbus_return_val_if_fail (len != NULL, FALSE);
            
	    if (dbus_message_iter_get_array_type (iter) != type)
	      {
		dbus_set_error (error, DBUS_ERROR_INVALID_ARGS,
				"Argument %d is specified to be of type \"array of %s\", but "
				"is actually of type \"array of %s\"\n", i,
				_dbus_type_to_string (type),
				_dbus_type_to_string (dbus_message_iter_get_array_type (iter)));
		goto out;
	      }
	    
	    switch (type)
	      {
	      case DBUS_TYPE_BYTE:
		err = !dbus_message_iter_get_byte_array (iter, (unsigned char **)data, len);
		break;
	      case DBUS_TYPE_BOOLEAN:
		err = !dbus_message_iter_get_boolean_array (iter, (unsigned char **)data, len);
		break;
	      case DBUS_TYPE_INT32:
		err = !dbus_message_iter_get_int32_array (iter, (dbus_int32_t **)data, len);
		break;
	      case DBUS_TYPE_UINT32:
		err = !dbus_message_iter_get_uint32_array (iter, (dbus_uint32_t **)data, len);
		break;
#ifdef DBUS_HAVE_INT64
              case DBUS_TYPE_INT64:
		err = !dbus_message_iter_get_int64_array (iter, (dbus_int64_t **)data, len);
		break;
	      case DBUS_TYPE_UINT64:
		err = !dbus_message_iter_get_uint64_array (iter, (dbus_uint64_t **)data, len);
		break;
#endif /* DBUS_HAVE_INT64 */
	      case DBUS_TYPE_DOUBLE:
		err = !dbus_message_iter_get_double_array (iter, (double **)data, len);
		break;
	      case DBUS_TYPE_STRING:
		err = !dbus_message_iter_get_string_array (iter, (char ***)data, len);
		break;
	      case DBUS_TYPE_OBJECT_PATH:
	        err = !dbus_message_iter_get_object_path_array (iter, (char ***)data, len);
	        break;

	      case DBUS_TYPE_NIL:
	      case DBUS_TYPE_ARRAY:
	      case DBUS_TYPE_CUSTOM:
	      case DBUS_TYPE_DICT:
		_dbus_warn ("dbus_message_get_args_valist doesn't support recursive arrays\n");
		dbus_set_error (error, DBUS_ERROR_NOT_SUPPORTED, NULL);
		goto out;
	      default:
		_dbus_warn ("Unknown field type %d\n", type);
		dbus_set_error (error, DBUS_ERROR_NOT_SUPPORTED, NULL);
		goto out;
	      }
	    if (err)
	      {
	        dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
		goto out;
	      }
	  }
	  break;
	case DBUS_TYPE_DICT:
	  _dbus_warn ("dbus_message_get_args_valist doesn't support dicts\n");
	  dbus_set_error (error, DBUS_ERROR_NOT_SUPPORTED, NULL);
	  goto out;
	default:	  
	  dbus_set_error (error, DBUS_ERROR_NOT_SUPPORTED, NULL);
	  _dbus_warn ("Unknown field type %d\n", spec_type);
	  goto out;
	}

      spec_type = va_arg (var_args, int);
      if (!dbus_message_iter_next (iter) && spec_type != DBUS_TYPE_INVALID)
        {
          dbus_set_error (error, DBUS_ERROR_INVALID_ARGS,
                          "Message has only %d arguments, but more were expected", i);
          goto out;
        }

      i++;
    }
  
  retval = TRUE;
  
 out:
  
  return retval;
}

/**
 * Returns the byte value that an iterator may point to.
 * Note that you need to check that the iterator points to
 * a byte value before using this function.
 *
 * @see dbus_message_iter_get_arg_type
 * @param iter the message iter
 * @returns the byte value
 */
unsigned char
dbus_message_iter_get_byte (DBusMessageIter *iter)
{
  unsigned char value = 0;

  _dbus_message_iter_get_basic_type (iter, DBUS_TYPE_BYTE, &value);

  return value;
}

/**
 * Returns the boolean value that an iterator may point to.
 * Note that you need to check that the iterator points to
 * a boolean value before using this function.
 *
 * @see dbus_message_iter_get_arg_type
 * @param iter the message iter
 * @returns the boolean value
 */
dbus_bool_t
dbus_message_iter_get_boolean (DBusMessageIter *iter)
{
  unsigned char value = 0;

  _dbus_message_iter_get_basic_type (iter, DBUS_TYPE_BOOLEAN, &value);

  return (value != FALSE);
}

/**
 * Returns the 32 bit signed integer value that an iterator may point to.
 * Note that you need to check that the iterator points to
 * a 32-bit integer value before using this function.
 *
 * @see dbus_message_iter_get_arg_type
 * @param iter the message iter
 * @returns the integer
 */
dbus_int32_t
dbus_message_iter_get_int32 (DBusMessageIter *iter)
{
  dbus_int32_t value = 0;

  _dbus_message_iter_get_basic_type (iter, DBUS_TYPE_INT32, &value);

  return value;
}

/**
 * Returns the 32 bit unsigned integer value that an iterator may point to.
 * Note that you need to check that the iterator points to
 * a 32-bit unsigned integer value before using this function.
 *
 * @see dbus_message_iter_get_arg_type
 * @param iter the message iter
 * @returns the integer
 */
dbus_uint32_t
dbus_message_iter_get_uint32 (DBusMessageIter *iter)
{
  dbus_int32_t value = 0;

  _dbus_message_iter_get_basic_type (iter, DBUS_TYPE_UINT32, &value);

  return value;
}

#ifdef DBUS_HAVE_INT64

/**
 * Returns the 64 bit signed integer value that an iterator may point
 * to.  Note that you need to check that the iterator points to a
 * 64-bit integer value before using this function.
 *
 * This function only exists if #DBUS_HAVE_INT64 is defined.
 *
 * @see dbus_message_iter_get_arg_type
 * @param iter the message iter
 * @returns the integer
 */
dbus_int64_t
dbus_message_iter_get_int64 (DBusMessageIter *iter)
{
  dbus_int64_t value = 0;

  _dbus_message_iter_get_basic_type (iter, DBUS_TYPE_INT64, &value);

  return value;
}

/**
 * Returns the 64 bit unsigned integer value that an iterator may point to.
 * Note that you need to check that the iterator points to
 * a 64-bit unsigned integer value before using this function.
 * 
 * This function only exists if #DBUS_HAVE_INT64 is defined.
 * 
 * @see dbus_message_iter_get_arg_type
 * @param iter the message iter
 * @returns the integer
 */
dbus_uint64_t
dbus_message_iter_get_uint64 (DBusMessageIter *iter)
{
  dbus_uint64_t value = 0;

  _dbus_message_iter_get_basic_type (iter, DBUS_TYPE_UINT64, &value);

  return value;
}

#endif /* DBUS_HAVE_INT64 */

/**
 * Returns the double value that an iterator may point to.
 * Note that you need to check that the iterator points to
 * a string value before using this function.
 *
 * @see dbus_message_iter_get_arg_type
 * @param iter the message iter
 * @returns the double
 */
double
dbus_message_iter_get_double (DBusMessageIter *iter)
{
  double value = 0.0;

  _dbus_message_iter_get_basic_type (iter, DBUS_TYPE_DOUBLE, &value);

  return value;
}

/**
 * Initializes an iterator for the array that the iterator
 * may point to. Note that you need to check that the iterator
 * points to an array prior to using this function.
 *
 * The array element type is returned in array_type, and the array
 * iterator can only be used to get that type of data.
 *
 * @param iter the iterator
 * @param array_iter pointer to an iterator to initialize
 * @param array_type gets set to the type of the array elements
 * @returns #FALSE if the array is empty
 */
dbus_bool_t
dbus_message_iter_init_array_iterator (DBusMessageIter *iter,
				       DBusMessageIter *array_iter,
				       int             *array_type)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  DBusMessageRealIter *array_real = (DBusMessageRealIter *)array_iter;
  int type, pos, len_pos, len, array_type_pos;
  int _array_type;

  _dbus_return_val_if_fail (dbus_message_iter_check (real), FALSE);

  pos = dbus_message_iter_get_data_start (real, &type);
  
  _dbus_assert (type == DBUS_TYPE_ARRAY);

  _array_type = iter_get_array_type (real, &array_type_pos);
  
  len_pos = _DBUS_ALIGN_VALUE (pos, sizeof (dbus_uint32_t));
  len = _dbus_demarshal_uint32 (&real->message->body, real->message->byte_order,
				pos, &pos);
  
  array_real->parent_iter = real;
  array_real->message = real->message;
  array_real->changed_stamp = real->message->changed_stamp;
  
  array_real->type = DBUS_MESSAGE_ITER_TYPE_ARRAY;
  array_real->pos = pos;
  array_real->end = pos + len;
  
  array_real->container_start = pos;
  array_real->container_length_pos = len_pos;
  array_real->wrote_dict_key = 0;
  array_real->array_type_pos = array_type_pos;
  array_real->array_type_done = TRUE;
  
  if (array_type != NULL)
    *array_type = _array_type;

  return len > 0;
}


/**
 * Initializes an iterator for the dict that the iterator
 * may point to. Note that you need to check that the iterator
 * points to a dict prior to using this function.
 *
 * @param iter the iterator
 * @param dict_iter pointer to an iterator to initialize
 * @returns #FALSE if the dict is empty
 */
dbus_bool_t
dbus_message_iter_init_dict_iterator (DBusMessageIter *iter,
				      DBusMessageIter *dict_iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  DBusMessageRealIter *dict_real = (DBusMessageRealIter *)dict_iter;
  int type, pos, len_pos, len;

  _dbus_return_val_if_fail (dbus_message_iter_check (real), FALSE);

  pos = dbus_message_iter_get_data_start (real, &type);
  
  _dbus_assert (type == DBUS_TYPE_DICT);

  len_pos = _DBUS_ALIGN_VALUE (pos, sizeof (dbus_uint32_t));
  len = _dbus_demarshal_uint32 (&real->message->body, real->message->byte_order,
				pos, &pos);
  
  dict_real->parent_iter = real;
  dict_real->message = real->message;
  dict_real->changed_stamp = real->message->changed_stamp;
  
  dict_real->type = DBUS_MESSAGE_ITER_TYPE_DICT;
  dict_real->pos = pos;
  dict_real->end = pos + len;
  
  dict_real->container_start = pos;
  dict_real->container_length_pos = len_pos;
  dict_real->wrote_dict_key = 0;

  return len > 0;
}

static dbus_bool_t
_dbus_message_iter_get_basic_type_array  (DBusMessageIter *iter,
					  char             type,
					  void           **array,
					  int             *array_len)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  int item_type, pos;

  _dbus_return_val_if_fail (dbus_message_iter_check (real), FALSE);

  pos = dbus_message_iter_get_data_start (real, &item_type);
  
  _dbus_assert (item_type == DBUS_TYPE_ARRAY);

  item_type = iter_get_array_type (real, NULL);
  
  _dbus_assert (type == item_type);

  return _dbus_demarshal_basic_type_array (&real->message->body,
					   item_type, array, array_len,
					   real->message->byte_order, &pos);
}

/**
 * Returns the byte array that the iterator may point to.
 * Note that you need to check that the iterator points
 * to a byte array prior to using this function.
 *
 * @param iter the iterator
 * @param value return location for array values
 * @param len return location for length of byte array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_get_byte_array (DBusMessageIter  *iter,
				  unsigned char   **value,
                                  int              *len)
{
  return _dbus_message_iter_get_basic_type_array (iter, DBUS_TYPE_BYTE,
						  (void **) value, len);
}

/**
 * Returns the boolean array that the iterator may point to. Note that
 * you need to check that the iterator points to an array of the
 * correct type prior to using this function.
 *
 * @param iter the iterator
 * @param value return location for the array
 * @param len return location for the array length
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_get_boolean_array (DBusMessageIter   *iter,
				     unsigned char    **value,
				     int               *len)
{
  return _dbus_message_iter_get_basic_type_array (iter, DBUS_TYPE_BOOLEAN,
						  (void **) value, len);
}

/**
 * Returns the 32 bit signed integer array that the iterator may point
 * to. Note that you need to check that the iterator points to an
 * array of the correct type prior to using this function.
 *
 * @param iter the iterator
 * @param value return location for the array
 * @param len return location for the array length
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_get_int32_array  (DBusMessageIter *iter,
				    dbus_int32_t   **value,
				    int             *len)
{
  return _dbus_message_iter_get_basic_type_array (iter, DBUS_TYPE_INT32,
						  (void **) value, len);
}

/**
 * Returns the 32 bit unsigned integer array that the iterator may point
 * to. Note that you need to check that the iterator points to an
 * array of the correct type prior to using this function.
 *
 * @param iter the iterator
 * @param value return location for the array
 * @param len return location for the array length
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_get_uint32_array  (DBusMessageIter *iter,
				     dbus_uint32_t  **value,
				     int             *len)
{
  return _dbus_message_iter_get_basic_type_array (iter, DBUS_TYPE_UINT32,
						  (void **) value, len);
}

#ifdef DBUS_HAVE_INT64

/**
 * Returns the 64 bit signed integer array that the iterator may point
 * to. Note that you need to check that the iterator points to an
 * array of the correct type prior to using this function.
 * 
 * This function only exists if #DBUS_HAVE_INT64 is defined.
 *
 * @param iter the iterator
 * @param value return location for the array
 * @param len return location for the array length
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_get_int64_array  (DBusMessageIter *iter,
				    dbus_int64_t   **value,
				    int             *len)
{
  return _dbus_message_iter_get_basic_type_array (iter, DBUS_TYPE_INT64,
						  (void **) value, len);
}

/**
 * Returns the 64 bit unsigned integer array that the iterator may point
 * to. Note that you need to check that the iterator points to an
 * array of the correct type prior to using this function.
 *
 * This function only exists if #DBUS_HAVE_INT64 is defined.
 *
 * @param iter the iterator
 * @param value return location for the array
 * @param len return location for the array length
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_get_uint64_array  (DBusMessageIter *iter,
				     dbus_uint64_t  **value,
				     int             *len)
{
  return _dbus_message_iter_get_basic_type_array (iter, DBUS_TYPE_UINT64,
						  (void **) value, len);
}

#endif /* DBUS_HAVE_INT64 */

/**
 * Returns the double array that the iterator may point to. Note that
 * you need to check that the iterator points to an array of the
 * correct type prior to using this function.
 *
 * @param iter the iterator
 * @param value return location for the array
 * @param len return location for the array length
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_get_double_array  (DBusMessageIter *iter,
				     double         **value,
				     int             *len)
{
  return _dbus_message_iter_get_basic_type_array (iter, DBUS_TYPE_DOUBLE,
						  (void **) value, len);
}

/**
 * Returns the string array that the iterator may point to.
 * Note that you need to check that the iterator points
 * to a string array prior to using this function.
 *
 * The returned value is a #NULL-terminated array of strings.
 * Each string is a separate malloc block, and the array
 * itself is a malloc block. You can free this type of
 * string array with dbus_free_string_array().
 *
 * @param iter the iterator
 * @param value return location for string values
 * @param len return location for length of byte array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_get_string_array (DBusMessageIter *iter,
				    char          ***value,
				    int             *len)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  int type, pos;

  _dbus_return_val_if_fail (dbus_message_iter_check (real), FALSE);

  pos = dbus_message_iter_get_data_start (real, &type);
  
  _dbus_assert (type == DBUS_TYPE_ARRAY);

  type = iter_get_array_type (real, NULL);
  _dbus_assert (type == DBUS_TYPE_STRING);

  if (!_dbus_demarshal_string_array (&real->message->body, real->message->byte_order,
				     pos, NULL, value, len))
    return FALSE;
  else
    return TRUE;
}

/**
 * Returns the object path array that the iterator may point to.
 * Note that you need to check that the iterator points
 * to an object path array prior to using this function.
 *
 * The returned value is a #NULL-terminated array of strings.
 * Each string is a separate malloc block, and the array
 * itself is a malloc block. You can free this type of
 * array with dbus_free_string_array().
 *
 * @param iter the iterator
 * @param value return location for string values
 * @param len return location for length of byte array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_get_object_path_array (DBusMessageIter *iter,
                                         char          ***value,
                                         int             *len)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  int type, pos;

  _dbus_return_val_if_fail (dbus_message_iter_check (real), FALSE);

  pos = dbus_message_iter_get_data_start (real, &type);
  
  _dbus_assert (type == DBUS_TYPE_ARRAY);

  type = iter_get_array_type (real, NULL);
  _dbus_assert (type == DBUS_TYPE_OBJECT_PATH);

  if (!_dbus_demarshal_string_array (&real->message->body, real->message->byte_order,
				     pos, NULL, value, len))
    return FALSE;
  else
    return TRUE;
}

/**
 * Returns the key name fot the dict entry that an iterator
 * may point to. Note that you need to check that the iterator
 * points to a dict entry before using this function.
 *
 * @see dbus_message_iter_init_dict_iterator
 * @param iter the message iter
 * @returns the key name
 */
char *
dbus_message_iter_get_dict_key (DBusMessageIter   *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (dbus_message_iter_check (real), NULL);

  _dbus_assert (real->type == DBUS_MESSAGE_ITER_TYPE_DICT);

  return _dbus_demarshal_string (&real->message->body, real->message->byte_order,
                                 real->pos, NULL);
}

/**
 * Initializes a DBusMessageIter pointing to the end of the
 * message. This iterator can be used to append data to the
 * message.
 *
 * @param message the message
 * @param iter pointer to an iterator to initialize
 */
void
dbus_message_append_iter_init (DBusMessage     *message,
			       DBusMessageIter *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_if_fail (message != NULL);
  _dbus_return_if_fail (iter != NULL);
  
  real->message = message;
  real->parent_iter = NULL;
  real->changed_stamp = message->changed_stamp;
  
  real->type = DBUS_MESSAGE_ITER_TYPE_MESSAGE;
  real->end = _dbus_string_get_length (&real->message->body);
  real->pos = real->end;
  
  real->container_length_pos = 0;
  real->wrote_dict_key = 0;
}

#ifndef DBUS_DISABLE_CHECKS
static dbus_bool_t
dbus_message_iter_append_check (DBusMessageRealIter *iter)
{
  if (iter == NULL)
    {
      _dbus_warn ("dbus iterator check failed: NULL iterator\n");
      return FALSE;
    }
  
  if (iter->message->locked)
    {
      _dbus_warn ("dbus iterator check failed: message is locked (has already been sent)\n");
      return FALSE;
    }
      
  if (iter->changed_stamp != iter->message->changed_stamp)
    {
      _dbus_warn ("dbus iterator check failed: invalid iterator, must re-initialize it after modifying the message");
      return FALSE;
    }
  
  if (iter->pos != iter->end)
    {
      _dbus_warn ("dbus iterator check failed: can only append at end of message");
      return FALSE;
    }
  
  if (iter->pos != _dbus_string_get_length (&iter->message->body))
    {
      _dbus_warn ("dbus iterator check failed: append pos not at end of message string");
      return FALSE;
    }

  return TRUE;
}
#endif /* DBUS_DISABLE_CHECKS */

static dbus_bool_t
dbus_message_iter_append_type (DBusMessageRealIter *iter,
			       int                  type)
{
  const char *data;

  switch (iter->type)
    {
    case DBUS_MESSAGE_ITER_TYPE_MESSAGE:
      if (!_dbus_string_append_byte (&iter->message->body, type))
        return FALSE;

      if (!_dbus_message_append_byte_to_signature (iter->message, type))
        {
          _dbus_string_shorten (&iter->message->body, 1);
          return FALSE;
        }
      break;
      
    case DBUS_MESSAGE_ITER_TYPE_ARRAY:
      data = _dbus_string_get_const_data_len (&iter->message->body,
					      iter->array_type_pos, 1);
      if (type != *data)
	{
	  _dbus_warn ("Appended element of wrong type for array\n");
	  return FALSE;
	}
      break;
      
    case DBUS_MESSAGE_ITER_TYPE_DICT:
      if (!iter->wrote_dict_key)
	{
	  _dbus_warn ("Appending dict data before key name\n");
	  return FALSE;
	}
      
      if (!_dbus_string_append_byte (&iter->message->body, type))
        return FALSE;
      
      break;
      
    default:
      _dbus_assert_not_reached ("Invalid iter type");
      break;
    }
  
  return TRUE;
}

static void
dbus_message_iter_update_after_change (DBusMessageRealIter *iter)
{
  iter->changed_stamp = iter->message->changed_stamp;
  
  /* Set new end of iter */
  iter->end = _dbus_string_get_length (&iter->message->body);
  iter->pos = iter->end;

  /* Set container length */
  if (iter->type == DBUS_MESSAGE_ITER_TYPE_DICT ||
      (iter->type == DBUS_MESSAGE_ITER_TYPE_ARRAY && iter->array_type_done))
    _dbus_marshal_set_uint32 (&iter->message->body,
			      iter->message->byte_order,
			      iter->container_length_pos,
			      iter->end - iter->container_start);
  
  if (iter->parent_iter)
    dbus_message_iter_update_after_change (iter->parent_iter);
}

static void
dbus_message_iter_append_done (DBusMessageRealIter *iter)
{
  iter->message->changed_stamp++;
  dbus_message_iter_update_after_change (iter);
  iter->wrote_dict_key = FALSE;
}

/**
 * Appends a nil value to the message
 *
 * @param iter an iterator pointing to the end of the message
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_nil (DBusMessageIter *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (dbus_message_iter_append_check (real), FALSE);

  if (!dbus_message_iter_append_type (real, DBUS_TYPE_NIL))
    return FALSE;
  
  dbus_message_iter_append_done (real);
  
  return TRUE;
}

static dbus_bool_t
dbus_message_iter_append_basic (DBusMessageIter *iter,
				char             type,
				void            *value)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (dbus_message_iter_append_check (real), FALSE);

  if (!dbus_message_iter_append_type (real, type))
    return FALSE;

  if (!_dbus_marshal_basic_type (&real->message->body,
				 type, value,
				 real->message->byte_order))
    {
      _dbus_string_set_length (&real->message->body, real->pos);
      return FALSE;
    }

  dbus_message_iter_append_done (real);
  
  return TRUE;  
}

/**
 * Appends a boolean value to the message
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the boolean value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_boolean (DBusMessageIter *iter,
				  dbus_bool_t     value)
{
  unsigned char val = (value != FALSE);
  return dbus_message_iter_append_basic (iter, DBUS_TYPE_BOOLEAN, &val);
}

/**
 * Appends a byte to the message
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the byte value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_byte (DBusMessageIter *iter,
			       unsigned char    value)
{
  return dbus_message_iter_append_basic (iter, DBUS_TYPE_BYTE, &value);
}

/**
 * Appends a 32 bit signed integer to the message.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the integer value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_int32   (DBusMessageIter *iter,
				  dbus_int32_t  value)
{
  return dbus_message_iter_append_basic (iter, DBUS_TYPE_INT32, &value);
}

/**
 * Appends a 32 bit unsigned integer to the message.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the integer value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_uint32 (DBusMessageIter *iter,
				 dbus_uint32_t    value)
{
  return dbus_message_iter_append_basic (iter, DBUS_TYPE_UINT32, &value);
}

#ifdef DBUS_HAVE_INT64

/**
 * Appends a 64 bit signed integer to the message.
 *
 * This function only exists if #DBUS_HAVE_INT64 is defined.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the integer value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_int64   (DBusMessageIter *iter,
				  dbus_int64_t  value)
{
  return dbus_message_iter_append_basic (iter, DBUS_TYPE_INT64, &value);
}

/**
 * Appends a 64 bit unsigned integer to the message.
 *
 * This function only exists if #DBUS_HAVE_INT64 is defined.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the integer value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_uint64 (DBusMessageIter *iter,
				 dbus_uint64_t    value)
{
  return dbus_message_iter_append_basic (iter, DBUS_TYPE_UINT64, &value);
}

#endif /* DBUS_HAVE_INT64 */

/**
 * Appends a double value to the message.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the double value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_double (DBusMessageIter *iter,
				 double           value)
{
  return dbus_message_iter_append_basic (iter, DBUS_TYPE_DOUBLE, &value);
}

/**
 * Appends a UTF-8 string to the message.
 *
 * @todo add return_val_if_fail(UTF-8 is valid)
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the string
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_string (DBusMessageIter *iter,
				 const char      *value)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (dbus_message_iter_append_check (real), FALSE);
  
  if (!dbus_message_iter_append_type (real, DBUS_TYPE_STRING))
    return FALSE;
  
  if (!_dbus_marshal_string (&real->message->body, real->message->byte_order, value))
    {
      _dbus_string_set_length (&real->message->body, real->pos);
      return FALSE;
    }

  dbus_message_iter_append_done (real);
  
  return TRUE;
}

/**
 * Appends an object path to the message.
 *
 * @todo add return_val_if_fail(UTF-8 is valid)
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the object path
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_object_path (DBusMessageIter *iter,
				      const char      *value)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (dbus_message_iter_append_check (real), FALSE);
  
  if (!dbus_message_iter_append_type (real, DBUS_TYPE_OBJECT_PATH))
    return FALSE;
  
  if (!_dbus_marshal_string (&real->message->body, real->message->byte_order, value))
    {
      _dbus_string_set_length (&real->message->body, real->pos);
      return FALSE;
    }

  dbus_message_iter_append_done (real);
  
  return TRUE;
}

/**
 * Appends a custom type data chunk to the message. A custom
 * type is simply an arbitrary UTF-8 string used as a type
 * tag, plus an array of arbitrary bytes to be interpreted
 * according to the type tag.
 *
 * @param iter an iterator pointing to the end of the message
 * @param name the name of the type
 * @param data the binary data used to store the value
 * @param len the length of the binary data in bytes
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_custom (DBusMessageIter      *iter,
                                 const char           *name,
                                 const unsigned char  *data,
                                 int                   len)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (dbus_message_iter_append_check (real), FALSE);

  if (!dbus_message_iter_append_type (real, DBUS_TYPE_CUSTOM))
    return FALSE;
  
   if (!_dbus_marshal_string (&real->message->body, real->message->byte_order, name))
    {
      _dbus_string_set_length (&real->message->body, real->pos);
      return FALSE;
    }
   
  if (!_dbus_marshal_byte_array (&real->message->body, real->message->byte_order, data, len))
    {
      _dbus_string_set_length (&real->message->body, real->pos);
      return FALSE;
    }

  dbus_message_iter_append_done (real);
  
  return TRUE;
}


/**
 * Appends a dict key name to the message. The iterator used
 * must point to a dict.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the string
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_dict_key (DBusMessageIter *iter,
				   const char      *value)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (dbus_message_iter_append_check (real), FALSE);
  _dbus_assert (real->type == DBUS_MESSAGE_ITER_TYPE_DICT);
  
  if (real->wrote_dict_key)
    {
      _dbus_warn ("Appending multiple dict key names\n");
      return FALSE;
    }
  
  if (!_dbus_marshal_string (&real->message->body, real->message->byte_order, value))
    {
      return FALSE;
    }

  dbus_message_iter_append_done (real);
  real->wrote_dict_key = TRUE;
  
  return TRUE;
}

static dbus_bool_t
array_iter_type_mark_done (DBusMessageRealIter *iter)
{
  int len_pos;
  
  if (iter->type == DBUS_MESSAGE_ITER_TYPE_ARRAY)
    array_iter_type_mark_done (iter->parent_iter);
  else
    return TRUE;

  len_pos = _DBUS_ALIGN_VALUE (_dbus_string_get_length (&iter->message->body),
			       sizeof (dbus_uint32_t));

  /* Empty length for now, backfill later */
  if (!_dbus_marshal_uint32 (&iter->message->body, iter->message->byte_order, 0))
    {
      _dbus_string_set_length (&iter->message->body, iter->pos);
      return FALSE;
    }

  iter->container_start = _dbus_string_get_length (&iter->message->body);
  iter->container_length_pos = len_pos;
  iter->array_type_done = TRUE;

  return TRUE;
}

static dbus_bool_t
append_array_type (DBusMessageRealIter *real,
		   int                  element_type,
		   dbus_bool_t         *array_type_done,
		   int                 *array_type_pos)
{
  int existing_element_type;
  
  if (!dbus_message_iter_append_type (real, DBUS_TYPE_ARRAY))
    return FALSE;
  
  if (real->type == DBUS_MESSAGE_ITER_TYPE_ARRAY &&
      real->array_type_done)
    {
      existing_element_type = iter_get_array_type (real, array_type_pos);
      if (existing_element_type != element_type)
	{
	  _dbus_warn ("Appending array of %s, when expecting array of %s\n",
		      _dbus_type_to_string (element_type),
                      _dbus_type_to_string (existing_element_type));
	  _dbus_string_set_length (&real->message->body, real->pos);
	  return FALSE;
	}
      if (array_type_done != NULL)
	  *array_type_done = TRUE;
    }
  else
    {
      if (array_type_pos != NULL)
	*array_type_pos = _dbus_string_get_length (&real->message->body);


      if (!_dbus_message_append_byte_to_signature (real->message, element_type))
        {
          _dbus_string_set_length (&real->message->body, real->pos);
          return FALSE;
        }
      
      /* Append element type */
      if (!_dbus_string_append_byte (&real->message->body, element_type))
	{
          _dbus_message_remove_byte_from_signature (real->message);
	  _dbus_string_set_length (&real->message->body, real->pos);
	  return FALSE;
	}

      if (array_type_done != NULL)
	*array_type_done = element_type != DBUS_TYPE_ARRAY;
      
      if (element_type != DBUS_TYPE_ARRAY &&
	  !array_iter_type_mark_done (real))
        {
          _dbus_message_remove_byte_from_signature (real->message);
          return FALSE;
        }
    }

  return TRUE;
}

/**
 * Appends an array to the message and initializes an iterator that
 * can be used to append to the array.
 *
 * @param iter an iterator pointing to the end of the message
 * @param array_iter pointer to an iter that will be initialized
 * @param element_type the type of the array elements
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_array (DBusMessageIter      *iter,
				DBusMessageIter      *array_iter,
				int                   element_type)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  DBusMessageRealIter *array_real = (DBusMessageRealIter *)array_iter;
  int len_pos;
  int array_type_pos;
  dbus_bool_t array_type_done;

  if (element_type == DBUS_TYPE_NIL)
    {
      _dbus_warn ("Can't create NIL arrays\n");
      return FALSE;
    }
  
  _dbus_return_val_if_fail (dbus_message_iter_append_check (real), FALSE);

  if (!append_array_type (real, element_type, &array_type_done, &array_type_pos))
    return FALSE;

  len_pos = _DBUS_ALIGN_VALUE (_dbus_string_get_length (&real->message->body), sizeof (dbus_uint32_t));

  if (array_type_done)
    {
      /* Empty length for now, backfill later */
      if (!_dbus_marshal_uint32 (&real->message->body, real->message->byte_order, 0))
	{
	  _dbus_string_set_length (&real->message->body, real->pos);
	  return FALSE;
	}
    }
  
  array_real->parent_iter = real;
  array_real->message = real->message;
  array_real->changed_stamp = real->message->changed_stamp;
  
  array_real->type = DBUS_MESSAGE_ITER_TYPE_ARRAY;
  array_real->pos = _dbus_string_get_length (&real->message->body);
  array_real->end = array_real->end;
  
  array_real->container_start = array_real->pos;
  array_real->container_length_pos = len_pos;
  array_real->wrote_dict_key = 0;
  array_real->array_type_done = array_type_done;
  array_real->array_type_pos = array_type_pos;

  dbus_message_iter_append_done (array_real);
  
  return TRUE;
}

/**
 * Appends a dict to the message and initializes an iterator that
 * can be used to append to the dict.
 *
 * @param iter an iterator pointing to the end of the message
 * @param dict_iter pointer to an iter that will be initialized
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_dict (DBusMessageIter      *iter,
			       DBusMessageIter      *dict_iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  DBusMessageRealIter *dict_real = (DBusMessageRealIter *)dict_iter;
  int len_pos;

  _dbus_return_val_if_fail (dbus_message_iter_append_check (real), FALSE);
  
  if (!dbus_message_iter_append_type (real, DBUS_TYPE_DICT))
    return FALSE;  
  
  len_pos = _DBUS_ALIGN_VALUE (_dbus_string_get_length (&real->message->body), sizeof (dbus_uint32_t));

  /* Empty length for now, backfill later */
  if (!_dbus_marshal_uint32 (&real->message->body, real->message->byte_order, 0))
    {
      _dbus_string_set_length (&real->message->body, real->pos);
      return FALSE;
    }
  
  dict_real->parent_iter = real;
  dict_real->message = real->message;
  dict_real->changed_stamp = real->message->changed_stamp;
  
  dict_real->type = DBUS_MESSAGE_ITER_TYPE_DICT;
  dict_real->pos = _dbus_string_get_length (&real->message->body);
  dict_real->end = dict_real->end;
  
  dict_real->container_start = dict_real->pos;
  dict_real->container_length_pos = len_pos;
  dict_real->wrote_dict_key = 0;

  dbus_message_iter_append_done (dict_real);
  
  real->wrote_dict_key = FALSE;
  
  return TRUE;
}

static dbus_bool_t
_dbus_message_iter_append_basic_array (DBusMessageIter *iter,
				       char             type,
				       const void      *value,
				       int              len)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (dbus_message_iter_append_check (real), FALSE);

  if (!append_array_type (real, type, NULL, NULL))
    return FALSE;
  
  if (!_dbus_marshal_basic_type_array (&real->message->body,
				       type, value, len,
				       real->message->byte_order))
    {
      _dbus_string_set_length (&real->message->body, real->pos);
      return FALSE;
    }

  dbus_message_iter_append_done (real);
  
  return TRUE;
}


/**
 * This function takes a va_list for use by language bindings.
 * It's otherwise the same as dbus_message_append_args().
 *
 * @todo: Shouldn't this function clean up the changes to the message
 *        on failures? (Yes)
  
 * @see dbus_message_append_args.  
 * @param message the message
 * @param first_arg_type type of first argument
 * @param var_args value of first argument, then list of type/value pairs
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_append_args_valist (DBusMessage *message,
				 int          first_arg_type,
				 va_list      var_args)
{
  int type, old_len;
  DBusMessageIter iter;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  
  old_len = _dbus_string_get_length (&message->body);
  
  type = first_arg_type;

  dbus_message_append_iter_init (message, &iter);
  
  while (type != DBUS_TYPE_INVALID)
    {
      switch (type)
	{
	case DBUS_TYPE_NIL:
	  if (!dbus_message_iter_append_nil (&iter))
	    goto errorout;
	  break;
	case DBUS_TYPE_BYTE:
	  /* Read an int from varargs, because the original unsigned
	   * char has been promoted to int. */
	  if (!dbus_message_iter_append_byte (&iter, va_arg (var_args, int)))
	    goto errorout;
	  break;
	case DBUS_TYPE_BOOLEAN:
	  if (!dbus_message_iter_append_boolean (&iter, va_arg (var_args, dbus_bool_t)))
	    goto errorout;
	  break;
	case DBUS_TYPE_INT32:
          /* FIXME this is probably wrong, because an int passed in probably gets
           * converted to plain "int" not necessarily 32-bit.
           */
	  if (!dbus_message_iter_append_int32 (&iter, va_arg (var_args, dbus_int32_t)))
	    goto errorout;
	  break;
	case DBUS_TYPE_UINT32:
          /* FIXME this is probably wrong, because an int passed in probably gets
           * converted to plain "int" not necessarily 32-bit.
           */
	  if (!dbus_message_iter_append_uint32 (&iter, va_arg (var_args, dbus_uint32_t)))
	    goto errorout;	    
	  break;
#ifdef DBUS_HAVE_INT64
        case DBUS_TYPE_INT64:
	  if (!dbus_message_iter_append_int64 (&iter, va_arg (var_args, dbus_int64_t)))
	    goto errorout;
	  break;
	case DBUS_TYPE_UINT64:
	  if (!dbus_message_iter_append_uint64 (&iter, va_arg (var_args, dbus_uint64_t)))
	    goto errorout;	    
	  break;
#endif /* DBUS_HAVE_INT64 */
	case DBUS_TYPE_DOUBLE:
	  if (!dbus_message_iter_append_double (&iter, va_arg (var_args, double)))
	    goto errorout;
	  break;
	case DBUS_TYPE_STRING:
	  if (!dbus_message_iter_append_string (&iter, va_arg (var_args, const char *)))
	    goto errorout;
	  break;
        case DBUS_TYPE_OBJECT_PATH:
	  if (!dbus_message_iter_append_object_path (&iter, va_arg (var_args, const char*)))
	    goto errorout;
          break;
	case DBUS_TYPE_CUSTOM:
	  {
	    const char *name;
	    unsigned char *data;
	    int len;
 
	    name = va_arg (var_args, const char *);
	    data = va_arg (var_args, unsigned char *);
	    len = va_arg (var_args, int);

	    if (!dbus_message_iter_append_custom (&iter, name, data, len))
	      goto errorout;
	    break;
	  }
	case DBUS_TYPE_ARRAY:
	  {
	    void *data;
	    int len, type;
 
	    type = va_arg (var_args, int);
	    data = va_arg (var_args, void *);
	    len = va_arg (var_args, int);

	    switch (type)
	      {
	      case DBUS_TYPE_BYTE:
	      case DBUS_TYPE_BOOLEAN:
	      case DBUS_TYPE_INT32:
	      case DBUS_TYPE_UINT32:
#ifdef DBUS_HAVE_INT64
              case DBUS_TYPE_INT64:
	      case DBUS_TYPE_UINT64:
#endif /* DBUS_HAVE_INT64 */
	      case DBUS_TYPE_DOUBLE:
		if (!_dbus_message_iter_append_basic_array (&iter, type, data, len))
		  goto errorout;
		break;
	      case DBUS_TYPE_STRING:
		if (!dbus_message_iter_append_string_array (&iter, (const char **)data, len))
		  goto errorout;
		break;
	      case DBUS_TYPE_OBJECT_PATH:
		if (!dbus_message_iter_append_object_path_array (&iter, (const char **)data, len))
		  goto errorout;
		break;
	      case DBUS_TYPE_NIL:
	      case DBUS_TYPE_ARRAY:
	      case DBUS_TYPE_CUSTOM:
	      case DBUS_TYPE_DICT:
		_dbus_warn ("dbus_message_append_args_valist doesn't support recursive arrays\n");
		goto errorout;
	      default:
		_dbus_warn ("Unknown field type %d\n", type);
		goto errorout;
	      }
	  }
	  break;
	  
	case DBUS_TYPE_DICT:
	  _dbus_warn ("dbus_message_append_args_valist doesn't support dicts\n");
	  goto errorout;
	default:
	  _dbus_warn ("Unknown field type %d\n", type);
	  goto errorout;
	}

      type = va_arg (var_args, int);
    }

  return TRUE;

 errorout:
  return FALSE;
}

/**
 * Appends a boolean array to the message.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_boolean_array (DBusMessageIter     *iter,
					unsigned const char *value,
					int                  len)
{
  return _dbus_message_iter_append_basic_array (iter, DBUS_TYPE_BOOLEAN,
						value, len);
}

/**
 * Appends a 32 bit signed integer array to the message.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_int32_array (DBusMessageIter    *iter,
				      const dbus_int32_t *value,
				      int                 len)
{
  return _dbus_message_iter_append_basic_array (iter, DBUS_TYPE_INT32,
						value, len);
}

/**
 * Appends a 32 bit unsigned integer array to the message.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_uint32_array (DBusMessageIter     *iter,
				       const dbus_uint32_t *value,
				       int                  len)
{
  return _dbus_message_iter_append_basic_array (iter, DBUS_TYPE_UINT32,
						value, len);
}

#ifdef DBUS_HAVE_INT64

/**
 * Appends a 64 bit signed integer array to the message.
 *
 * This function only exists if #DBUS_HAVE_INT64 is defined.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_int64_array (DBusMessageIter    *iter,
				      const dbus_int64_t *value,
				      int                 len)
{
  return _dbus_message_iter_append_basic_array (iter, DBUS_TYPE_INT64,
						value, len);
}

/**
 * Appends a 64 bit unsigned integer array to the message.
 *
 * This function only exists if #DBUS_HAVE_INT64 is defined.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_uint64_array (DBusMessageIter     *iter,
				       const dbus_uint64_t *value,
				       int                  len)
{
  return _dbus_message_iter_append_basic_array (iter, DBUS_TYPE_UINT64,
						value, len);
}
#endif /* DBUS_HAVE_INT64 */

/**
 * Appends a double array to the message.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_double_array (DBusMessageIter *iter,
				       const double    *value,
				       int              len)
{
  return _dbus_message_iter_append_basic_array (iter, DBUS_TYPE_DOUBLE,
						value, len);
}

/**
 * Appends a byte array to the message.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_byte_array (DBusMessageIter     *iter,
				     unsigned const char *value,
				     int                  len)
{
  return _dbus_message_iter_append_basic_array (iter, DBUS_TYPE_BYTE,
						value, len);
}

/**
 * Appends a string array to the message.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_string_array (DBusMessageIter *iter,
				       const char     **value,
				       int              len)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (dbus_message_iter_append_check (real), FALSE);

  if (!append_array_type (real, DBUS_TYPE_STRING, NULL, NULL))
    return FALSE;
  
  if (!_dbus_marshal_string_array (&real->message->body, real->message->byte_order, value, len))
    {
      _dbus_string_set_length (&real->message->body, real->pos);
      return FALSE;
    }

  dbus_message_iter_append_done (real);
  
  return TRUE;
}

/**
 * Appends an object path array to the message.
 *
 * @param iter an iterator pointing to the end of the message
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_iter_append_object_path_array (DBusMessageIter *iter,
					    const char     **value,
					    int              len)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (dbus_message_iter_append_check (real), FALSE);

  if (!append_array_type (real, DBUS_TYPE_OBJECT_PATH, NULL, NULL))
    return FALSE;
  
  if (!_dbus_marshal_string_array (&real->message->body, real->message->byte_order, value, len))
    {
      _dbus_string_set_length (&real->message->body, real->pos);
      return FALSE;
    }

  dbus_message_iter_append_done (real);
  
  return TRUE;
}

/**
 * Sets the message sender.
 *
 * @param message the message
 * @param sender the sender or #NULL to unset
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_sender (DBusMessage  *message,
                         const char   *sender)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);

  return set_string_field (message,
                           DBUS_HEADER_FIELD_SENDER,
                           DBUS_TYPE_STRING,
                           sender);
}

/**
 * Sets a flag indicating that the message does not want a reply; if
 * this flag is set, the other end of the connection may (but is not
 * required to) optimize by not sending method return or error
 * replies. If this flag is set, there is no way to know whether the
 * message successfully arrived at the remote end.
 *
 * @param message the message
 * @param no_reply #TRUE if no reply is desired
 */
void
dbus_message_set_no_reply (DBusMessage *message,
                           dbus_bool_t  no_reply)
{
  char *header;

  _dbus_return_if_fail (message != NULL);
  _dbus_return_if_fail (!message->locked);
  
  header = _dbus_string_get_data_len (&message->header, FLAGS_OFFSET, 1);
  
  if (no_reply)
    *header |= DBUS_HEADER_FLAG_NO_REPLY_EXPECTED;
  else
    *header &= ~DBUS_HEADER_FLAG_NO_REPLY_EXPECTED;    
}

/**
 * Returns #TRUE if the message does not expect
 * a reply.
 *
 * @param message the message
 * @returns #TRUE if the message sender isn't waiting for a reply
 */
dbus_bool_t
dbus_message_get_no_reply (DBusMessage *message)
{
  const char *header;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  
  header = _dbus_string_get_const_data_len (&message->header, FLAGS_OFFSET, 1);

  return (*header & DBUS_HEADER_FLAG_NO_REPLY_EXPECTED) != 0;
}


/**
 * Sets a flag indicating that the addressed service will be auto-activated
 * before the message is delivered. When this flag is set, the message is held
 * until the service is succesfully activated or fail to activate. In case of
 * failure, the reply will be an activation error.
 *
 * @param message the message
 * @param auto_activation #TRUE if auto-activation is desired
 */
void
dbus_message_set_auto_activation (DBusMessage *message,
				  dbus_bool_t  auto_activation)
{
  char *header;

  _dbus_return_if_fail (message != NULL);
  _dbus_return_if_fail (!message->locked);
  
  header = _dbus_string_get_data_len (&message->header, FLAGS_OFFSET, 1);
  
  if (auto_activation)
    *header |= DBUS_HEADER_FLAG_AUTO_ACTIVATION;
  else
    *header &= ~DBUS_HEADER_FLAG_AUTO_ACTIVATION;
}

/**
 * Returns #TRUE if the message will cause the addressed service to be
 * auto-activated.
 *
 * @param message the message
 * @returns #TRUE if the message will use auto-activation
 */
dbus_bool_t
dbus_message_get_auto_activation (DBusMessage *message)
{
  const char *header;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  
  header = _dbus_string_get_const_data_len (&message->header, FLAGS_OFFSET, 1);

  return (*header & DBUS_HEADER_FLAG_AUTO_ACTIVATION) != 0;
}

/**
 * Gets the service which originated this message,
 * or #NULL if unknown or inapplicable.
 *
 * @param message the message
 * @returns the service name or #NULL
 */
const char*
dbus_message_get_sender (DBusMessage *message)
{
  _dbus_return_val_if_fail (message != NULL, NULL);
  
  return get_string_field (message, 
			   DBUS_HEADER_FIELD_SENDER,
			   NULL);
}

/**
 * Gets the type signature of the message, i.e. the arguments in the
 * message payload. The signature includes only "in" arguments for
 * #DBUS_MESSAGE_TYPE_METHOD_CALL and only "out" arguments for
 * #DBUS_MESSAGE_TYPE_METHOD_RETURN, so is slightly different from
 * what you might expect (it does not include the signature of the
 * entire C++-style method).
 *
 * The signature is a string made up of type codes such
 * as #DBUS_TYPE_STRING. The string is terminated with nul
 * (nul is also the value of #DBUS_TYPE_INVALID).
 * 
 * @param message the message
 * @returns the type signature
 */
const char*
dbus_message_get_signature (DBusMessage *message)
{
  _dbus_return_val_if_fail (message != NULL, NULL);

  return get_string_field (message, 
			   DBUS_HEADER_FIELD_SIGNATURE,
			   NULL);
}

static dbus_bool_t
_dbus_message_has_type_interface_member (DBusMessage *message,
                                         int          type,
                                         const char  *interface,
                                         const char  *method)
{
  const char *n;

  _dbus_assert (message != NULL);
  _dbus_assert (interface != NULL);
  _dbus_assert (method != NULL);

  if (dbus_message_get_type (message) != type)
    return FALSE;

  /* Optimize by checking the short method name first
   * instead of the longer interface name
   */  

  n = dbus_message_get_member (message);

  if (n && strcmp (n, method) == 0)
    {
      n = dbus_message_get_interface (message);
      
      if (n && strcmp (n, interface) == 0)
        return TRUE;
    }

  return FALSE;
}

/**
 * Checks whether the message is a method call with the given
 * interface and member fields.  If the message is not
 * #DBUS_MESSAGE_TYPE_METHOD_CALL, or has a different interface or member field,
 * returns #FALSE.
 *
 * @param message the message
 * @param interface the name to check (must not be #NULL)
 * @param method the name to check (must not be #NULL)
 * 
 * @returns #TRUE if the message is the specified method call
 */
dbus_bool_t
dbus_message_is_method_call (DBusMessage *message,
                             const char  *interface,
                             const char  *method)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (interface != NULL, FALSE);
  _dbus_return_val_if_fail (method != NULL, FALSE);

  return _dbus_message_has_type_interface_member (message,
                                                  DBUS_MESSAGE_TYPE_METHOD_CALL,
                                                  interface, method);
}

/**
 * Checks whether the message is a signal with the given
 * interface and member fields.  If the message is not
 * #DBUS_MESSAGE_TYPE_SIGNAL, or has a different interface or member field,
 * returns #FALSE.
 *
 * @param message the message
 * @param interface the name to check (must not be #NULL)
 * @param signal_name the name to check (must not be #NULL)
 * 
 * @returns #TRUE if the message is the specified signal
 */
dbus_bool_t
dbus_message_is_signal (DBusMessage *message,
                        const char  *interface,
                        const char  *signal_name)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (interface != NULL, FALSE);
  _dbus_return_val_if_fail (signal_name != NULL, FALSE);

  return _dbus_message_has_type_interface_member (message,
                                                  DBUS_MESSAGE_TYPE_SIGNAL,
                                                  interface, signal_name);
}

/**
 * Checks whether the message is an error reply with the given error
 * name.  If the message is not #DBUS_MESSAGE_TYPE_ERROR, or has a
 * different name, returns #FALSE.
 *
 * @param message the message
 * @param error_name the name to check (must not be #NULL)
 * 
 * @returns #TRUE if the message is the specified error
 */
dbus_bool_t
dbus_message_is_error (DBusMessage *message,
                       const char  *error_name)
{
  const char *n;
  
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (error_name != NULL, FALSE);
  _dbus_return_val_if_fail (is_valid_error_name (error_name), FALSE);
  
  if (dbus_message_get_type (message) != DBUS_MESSAGE_TYPE_ERROR)
    return FALSE;

  n = dbus_message_get_error_name (message);

  if (n && strcmp (n, error_name) == 0)
    return TRUE;
  else
    return FALSE;
}

/**
 * Checks whether the message was sent to the given service.  If the
 * message has no service specified or has a different name, returns
 * #FALSE.
 *
 * @param message the message
 * @param service the service to check (must not be #NULL)
 * 
 * @returns #TRUE if the message has the given destination service
 */
dbus_bool_t
dbus_message_has_destination (DBusMessage  *message,
                              const char   *service)
{
  const char *s;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (service != NULL, FALSE);
  
  s = dbus_message_get_destination (message);

  if (s && strcmp (s, service) == 0)
    return TRUE;
  else
    return FALSE;
}

/**
 * Checks whether the message has the given service as its sender.  If
 * the message has no sender specified or has a different sender,
 * returns #FALSE. Note that if a peer application owns multiple
 * services, its messages will have only one of those services as the
 * sender (usually the base service). So you can't use this
 * function to prove the sender didn't own service Foo, you can
 * only use it to prove that it did.
 *
 * @param message the message
 * @param service the service to check (must not be #NULL)
 * 
 * @returns #TRUE if the message has the given origin service
 */
dbus_bool_t
dbus_message_has_sender (DBusMessage  *message,
                         const char   *service)
{
  const char *s;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (service != NULL, FALSE);
  
  s = dbus_message_get_sender (message);

  if (s && strcmp (s, service) == 0)
    return TRUE;
  else
    return FALSE;
}

/**
 * Checks whether the message has the given signature;
 * see dbus_message_get_signature() for more details on
 * what the signature looks like.
 *
 * @param message the message
 * @param signature typecode array
 * @returns #TRUE if message has the given signature
*/
dbus_bool_t
dbus_message_has_signature (DBusMessage   *message,
                            const char    *signature)
{
  const char *s;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (signature != NULL, FALSE);
  
  s = dbus_message_get_signature (message);

  if (s && strcmp (s, signature) == 0)
    return TRUE;
  else
    return FALSE;
}

/**
 * Sets a #DBusError based on the contents of the given
 * message. The error is only set if the message
 * is an error message, as in DBUS_MESSAGE_TYPE_ERROR.
 * The name of the error is set to the name of the message,
 * and the error message is set to the first argument
 * if the argument exists and is a string.
 *
 * The return value indicates whether the error was set (the error is
 * set if and only if the message is an error message).
 * So you can check for an error reply and convert it to DBusError
 * in one go.
 *
 * @param error the error to set
 * @param message the message to set it from
 * @returns #TRUE if dbus_message_get_is_error() returns #TRUE for the message
 */
dbus_bool_t
dbus_set_error_from_message (DBusError   *error,
                             DBusMessage *message)
{
  char *str;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_error_is_set (error, FALSE);
  
  if (dbus_message_get_type (message) != DBUS_MESSAGE_TYPE_ERROR)
    return FALSE;

  str = NULL;
  dbus_message_get_args (message, NULL,
                         DBUS_TYPE_STRING, &str,
                         DBUS_TYPE_INVALID);

  dbus_set_error (error, dbus_message_get_error_name (message),
                  str ? "%s" : NULL, str);

  dbus_free (str);
  
  return TRUE;
}

/** @} */

/**
 * @addtogroup DBusMessageInternals
 *
 * @{
 */
/**
 * @typedef DBusMessageLoader
 *
 * The DBusMessageLoader object encapsulates the process of converting
 * a byte stream into a series of DBusMessage. It buffers the incoming
 * bytes as efficiently as possible, and generates a queue of
 * messages. DBusMessageLoader is typically used as part of a
 * DBusTransport implementation. The DBusTransport then hands off
 * the loaded messages to a DBusConnection, making the messages
 * visible to the application.
 *
 * @todo write tests for break-loader that a) randomly delete header
 * fields and b) set string fields to zero-length and other funky
 * values.
 * 
 */

/* we definitely use signed ints for sizes, so don't exceed
 * _DBUS_INT_MAX; and add 16 for paranoia, since a message
 * over 128M is pretty nuts anyhow.
 */

/**
 * The maximum sane message size.
 */
#define MAX_SANE_MESSAGE_SIZE (_DBUS_INT_MAX/16)

/**
 * Implementation details of DBusMessageLoader.
 * All members are private.
 */
struct DBusMessageLoader
{
  int refcount;        /**< Reference count. */

  DBusString data;     /**< Buffered data */
  
  DBusList *messages;  /**< Complete messages. */

  long max_message_size; /**< Maximum size of a message */
  
  unsigned int buffer_outstanding : 1; /**< Someone is using the buffer to read */

  unsigned int corrupted : 1; /**< We got broken data, and are no longer working */
};

/**
 * The initial buffer size of the message loader.
 * 
 * @todo this should be based on min header size plus some average
 * body size, or something. Or rather, the min header size only, if we
 * want to try to read only the header, store that in a DBusMessage,
 * then read only the body and store that, etc., depends on
 * how we optimize _dbus_message_loader_get_buffer() and what
 * the exact message format is.
 */
#define INITIAL_LOADER_DATA_LEN 32

/**
 * Creates a new message loader. Returns #NULL if memory can't
 * be allocated.
 *
 * @returns new loader, or #NULL.
 */
DBusMessageLoader*
_dbus_message_loader_new (void)
{
  DBusMessageLoader *loader;

  loader = dbus_new0 (DBusMessageLoader, 1);
  if (loader == NULL)
    return NULL;
  
  loader->refcount = 1;

  /* Try to cap message size at something that won't *totally* hose
   * the system if we have a couple of them.
   */
  loader->max_message_size = _DBUS_ONE_MEGABYTE * 32;
  
  if (!_dbus_string_init (&loader->data))
    {
      dbus_free (loader);
      return NULL;
    }

  /* preallocate the buffer for speed, ignore failure */
  _dbus_string_set_length (&loader->data, INITIAL_LOADER_DATA_LEN);
  _dbus_string_set_length (&loader->data, 0);
  
  return loader;
}

/**
 * Increments the reference count of the loader.
 *
 * @param loader the loader.
 * @returns the loader
 */
DBusMessageLoader *
_dbus_message_loader_ref (DBusMessageLoader *loader)
{
  loader->refcount += 1;

  return loader;
}

/**
 * Decrements the reference count of the loader and finalizes the
 * loader when the count reaches zero.
 *
 * @param loader the loader.
 */
void
_dbus_message_loader_unref (DBusMessageLoader *loader)
{
  loader->refcount -= 1;
  if (loader->refcount == 0)
    {
      _dbus_list_foreach (&loader->messages,
                          (DBusForeachFunction) dbus_message_unref,
                          NULL);
      _dbus_list_clear (&loader->messages);
      _dbus_string_free (&loader->data);
      dbus_free (loader);
    }
}

/**
 * Gets the buffer to use for reading data from the network.  Network
 * data is read directly into an allocated buffer, which is then used
 * in the DBusMessage, to avoid as many extra memcpy's as possible.
 * The buffer must always be returned immediately using
 * _dbus_message_loader_return_buffer(), even if no bytes are
 * successfully read.
 *
 * @todo this function can be a lot more clever. For example
 * it can probably always return a buffer size to read exactly
 * the body of the next message, thus avoiding any memory wastage
 * or reallocs.
 *
 * @todo we need to enforce a max length on strings in header fields.
 * 
 * @param loader the message loader.
 * @param buffer the buffer
 */
void
_dbus_message_loader_get_buffer (DBusMessageLoader  *loader,
                                 DBusString        **buffer)
{
  _dbus_assert (!loader->buffer_outstanding);

  *buffer = &loader->data;
  
  loader->buffer_outstanding = TRUE;
}

/**
 * The smallest header size that can occur. 
 * (It won't be valid)
 */
#define DBUS_MINIMUM_HEADER_SIZE 16

static dbus_bool_t
decode_string_field (const DBusString   *data,
		     int                 field,
                     HeaderField        *header_field,
		     DBusString         *field_data,
                     int                 pos,
                     int                 type)
{
  int string_data_pos;

  _dbus_assert (header_field != NULL);
  _dbus_assert (field_data != NULL);
  
  if (header_field->name_offset >= 0)
    {
      _dbus_verbose ("%s field provided twice\n",
		     _dbus_header_field_to_string (field));
      return FALSE;
    }

  if (type != DBUS_TYPE_STRING)
    {
      _dbus_verbose ("%s field has wrong type %s\n",
                     _dbus_header_field_to_string (field),
		     _dbus_type_to_string (type));
      return FALSE;
    }

  /* skip padding after typecode, skip string length;
   * we assume that the string arg has already been validated
   * for sanity and UTF-8
   */
  string_data_pos = _DBUS_ALIGN_VALUE (pos, 4) + 4;
  _dbus_assert (string_data_pos < _dbus_string_get_length (data));
  
  _dbus_string_init_const (field_data,
                           _dbus_string_get_const_data (data) + string_data_pos);

  header_field->name_offset  = pos - 2;
  header_field->value_offset = _DBUS_ALIGN_VALUE (pos, 4);
  
#if 0
  _dbus_verbose ("Found field %s at offset %d\n",
                 _dbus_header_field_to_string (field),
		 header_field->value_offset);
#endif

  return TRUE;
}

/* FIXME because the service/interface/member/error names are already
 * validated to be in the particular ASCII subset, UTF-8 validating
 * them could be skipped as a probably-interesting optimization.
 * The UTF-8 validation shows up in callgrind-type profiles but
 * not so much in sample/time-based profiles.
 */
static dbus_bool_t
decode_header_data (const DBusString   *data,
		    int		        header_len,
		    int                 byte_order,
                    int                 message_type,
                    HeaderField         fields[DBUS_HEADER_FIELD_LAST + 1],
		    int                *message_padding)
{
  DBusString field_data;
  int pos, new_pos;
  int i;
  int field;
  int type;
  dbus_bool_t signature_required;
  
  if (header_len < 16)
    {
      _dbus_verbose ("Header length %d is too short\n", header_len);
      return FALSE;
    }
  
  i = 0;
  while (i <= DBUS_HEADER_FIELD_LAST)
    {
      fields[i].name_offset  = -1;
      fields[i].value_offset = -1;
      ++i;
    }
  
  pos = 16;
  while (pos < header_len)
    {
      field = _dbus_string_get_byte (data, pos);
      if (field == DBUS_HEADER_FIELD_INVALID)
	break; /* Must be padding */
      pos++;

      if (!_dbus_marshal_validate_type (data, pos, &type, &pos))
	{
          _dbus_verbose ("Failed to validate type of named header field pos = %d\n",
                         pos);
	  return FALSE;
	}
      
      if (!_dbus_marshal_validate_arg (data, byte_order, 0, type, -1, pos, &new_pos))
        {
          _dbus_verbose ("Failed to validate argument to named header field pos = %d\n",
                         pos);
          return FALSE;
        }

      if (new_pos > header_len)
        {
          _dbus_verbose ("Named header field tries to extend beyond header length\n");
          return FALSE;
        }
      
      switch (field)
        {
        case DBUS_HEADER_FIELD_DESTINATION:
          if (!decode_string_field (data, field, &fields[field],
				    &field_data, pos, type))
            return FALSE;

	  if (!_dbus_string_validate_service (&field_data, 0,
					      _dbus_string_get_length (&field_data)))
	    {
	      _dbus_verbose ("service field has invalid content \"%s\"\n",
			     _dbus_string_get_const_data (&field_data));
	      return FALSE;
	    }
          break;

        case DBUS_HEADER_FIELD_INTERFACE:
	  if (!decode_string_field (data, field, &fields[field],
				    &field_data, pos, type))
            return FALSE;

	  if (!_dbus_string_validate_interface (&field_data, 0,
						_dbus_string_get_length (&field_data)))
	    {
	      _dbus_verbose ("interface field has invalid content \"%s\"\n",
			     _dbus_string_get_const_data (&field_data));
	      return FALSE;
	    }
      
	  if (_dbus_string_equal_c_str (&field_data,
					DBUS_INTERFACE_ORG_FREEDESKTOP_LOCAL))
	    {
	      _dbus_verbose ("Message is on the local interface\n");
	      return FALSE;
	    }
          break;

        case DBUS_HEADER_FIELD_MEMBER:
          if (!decode_string_field (data, field, &fields[field],
				    &field_data, pos, type))
            return FALSE;
	  
	  if (!_dbus_string_validate_member (&field_data, 0,
					     _dbus_string_get_length (&field_data)))
	    {
	      _dbus_verbose ("member field has invalid content \"%s\"\n",
			     _dbus_string_get_const_data (&field_data));
	      return FALSE;
	    }
          break;

        case DBUS_HEADER_FIELD_ERROR_NAME:
          if (!decode_string_field (data, field, &fields[field],
				    &field_data, pos, type))
            return FALSE;
	  
	  if (!_dbus_string_validate_error_name (&field_data, 0,
						 _dbus_string_get_length (&field_data)))
	    {
	      _dbus_verbose ("error-name field has invalid content \"%s\"\n",
			     _dbus_string_get_const_data (&field_data));
	      return FALSE;
	    }
          break;
          
	case DBUS_HEADER_FIELD_SENDER:
          if (!decode_string_field (data, field, &fields[field],
				    &field_data, pos, type))
            return FALSE;
	  
	  if (!_dbus_string_validate_service (&field_data, 0,
					      _dbus_string_get_length (&field_data)))
	    {
	      _dbus_verbose ("sender-service field has invalid content \"%s\"\n",
			     _dbus_string_get_const_data (&field_data));
	      return FALSE;
	    }
	  break;

	case DBUS_HEADER_FIELD_PATH:

          /* Path was already validated as part of standard
           * type validation, since there's an OBJECT_PATH
           * type.
           */
          
          if (fields[field].name_offset >= 0)
            {
              _dbus_verbose ("path field provided twice\n");
              return FALSE;
            }
          if (type != DBUS_TYPE_OBJECT_PATH)
            {
              _dbus_verbose ("path field has wrong type\n");
              return FALSE;
            }

          fields[field].name_offset  = pos - 2;
          fields[field].value_offset = _DBUS_ALIGN_VALUE (pos, 4);

          /* No forging signals from the local path */
          {
            const char *s;
            s = _dbus_string_get_const_data_len (data,
                                                 fields[field].value_offset,
                                                 _dbus_string_get_length (data) -
                                                 fields[field].value_offset);
            if (strcmp (s, DBUS_PATH_ORG_FREEDESKTOP_LOCAL) == 0)
              {
                _dbus_verbose ("Message is on the local path\n");
                return FALSE;
              }
          }
          
          _dbus_verbose ("Found path at offset %d\n",
                         fields[field].value_offset);
	  break;
          
	case DBUS_HEADER_FIELD_REPLY_SERIAL:
          if (fields[field].name_offset >= 0)
            {
              _dbus_verbose ("reply field provided twice\n");
              return FALSE;
            }

          if (type != DBUS_TYPE_UINT32)
            {
              _dbus_verbose ("reply field has wrong type\n");
              return FALSE;
            }
          
          fields[field].name_offset  = pos - 2;
          fields[field].value_offset = _DBUS_ALIGN_VALUE (pos, 4);

          _dbus_verbose ("Found reply serial %u at offset %d\n",
                         _dbus_demarshal_uint32 (data,
                                                 byte_order,
                                                 fields[field].value_offset,
                                                 NULL),
                         fields[field].value_offset);
	  break;

	case DBUS_HEADER_FIELD_SIGNATURE:
          if (!decode_string_field (data, field, &fields[field],
				    &field_data, pos, type))
            return FALSE;

	  if (!_dbus_string_validate_signature (&field_data, 0,
                                                _dbus_string_get_length (&field_data)))
	    {
	      _dbus_verbose ("signature field has invalid content \"%s\"\n",
			     _dbus_string_get_const_data (&field_data));
	      return FALSE;
	    }
	  break;
          
        default:
	  _dbus_verbose ("Ignoring an unknown header field: %d at offset %d\n",
			 field, pos);
	}
      
      pos = new_pos;
    }

  if (pos < header_len)
    {
      /* Alignment padding, verify that it's nul */
      if ((header_len - pos) >= 8)
	{
	  _dbus_verbose ("too much header alignment padding\n");
	  return FALSE;
	}

      if (!_dbus_string_validate_nul (data,
                                      pos, (header_len - pos)))
        {
          _dbus_verbose ("header alignment padding is not nul\n");
          return FALSE;
        }
    }

  /* Depending on message type, enforce presence of certain fields. */
  signature_required = TRUE;
  
  switch (message_type)
    {
    case DBUS_MESSAGE_TYPE_SIGNAL:
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
      if (fields[DBUS_HEADER_FIELD_PATH].value_offset < 0)
        {
          _dbus_verbose ("No path field provided\n");
          return FALSE;
        }
      /* FIXME make this optional, only for method calls */
      if (fields[DBUS_HEADER_FIELD_INTERFACE].value_offset < 0)
        {
          _dbus_verbose ("No interface field provided\n");
          return FALSE;
        }
      if (fields[DBUS_HEADER_FIELD_MEMBER].value_offset < 0)
        {
          _dbus_verbose ("No member field provided\n");
          return FALSE;
        }
      break;
    case DBUS_MESSAGE_TYPE_ERROR:
      if (fields[DBUS_HEADER_FIELD_ERROR_NAME].value_offset < 0)
        {
          _dbus_verbose ("No error-name field provided\n");
          return FALSE;
        }
      if (fields[DBUS_HEADER_FIELD_REPLY_SERIAL].value_offset < 0)
        {
          _dbus_verbose ("No reply serial field provided in error\n");
          return FALSE;
        }
      break;
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
      if (fields[DBUS_HEADER_FIELD_REPLY_SERIAL].value_offset < 0)
        {
          _dbus_verbose ("No reply serial field provided in method return\n");
          return FALSE;
        }
      break;
    default:
      /* An unknown type, spec requires us to ignore it */
      signature_required = FALSE;
      break;
    }

  /* FIXME allow omitting signature field for a message with no arguments? */
  if (signature_required)
    {
      if (fields[DBUS_HEADER_FIELD_SIGNATURE].value_offset < 0)
        {
          _dbus_verbose ("No signature field provided\n");
          return FALSE;
        }
    }
  
  if (message_padding)
    *message_padding = header_len - pos;  
  
  return TRUE;
}

/**
 * Returns a buffer obtained from _dbus_message_loader_get_buffer(),
 * indicating to the loader how many bytes of the buffer were filled
 * in. This function must always be called, even if no bytes were
 * successfully read.
 *
 * @param loader the loader.
 * @param buffer the buffer.
 * @param bytes_read number of bytes that were read into the buffer.
 */
void
_dbus_message_loader_return_buffer (DBusMessageLoader  *loader,
                                    DBusString         *buffer,
                                    int                 bytes_read)
{
  _dbus_assert (loader->buffer_outstanding);
  _dbus_assert (buffer == &loader->data);

  loader->buffer_outstanding = FALSE;
}

static dbus_bool_t
load_one_message (DBusMessageLoader *loader,
                  int                byte_order,
                  int                message_type,
                  int                header_len,
                  int                body_len)
{
  DBusMessage *message;
  HeaderField fields[DBUS_HEADER_FIELD_LAST + 1];
  int i;
  int next_arg;
  dbus_bool_t oom;
  int header_padding;
  
  message = NULL;
  oom = FALSE;
  
#if 0
  _dbus_verbose_bytes_of_string (&loader->data, 0, header_len /* + body_len */);
#endif	  

  if (!decode_header_data (&loader->data,
                           header_len, byte_order,
                           message_type,
                           fields, &header_padding))
    {
      _dbus_verbose ("Header was invalid\n");
      loader->corrupted = TRUE;
      goto failed;
    }
          
  next_arg = header_len;
  while (next_arg < (header_len + body_len))
    {
      int type;
      int prev = next_arg;

      if (!_dbus_marshal_validate_type (&loader->data, next_arg,
                                        &type, &next_arg))
        {
          _dbus_verbose ("invalid typecode at offset %d\n", prev);
          loader->corrupted = TRUE;
          goto failed;
        }
      
      if (!_dbus_marshal_validate_arg (&loader->data,
                                       byte_order,
                                       0,
                                       type, -1,
                                       next_arg,
                                       &next_arg))
        {
          _dbus_verbose ("invalid type data at %d, next_arg\n", next_arg);
          loader->corrupted = TRUE;
          goto failed;
        }

      _dbus_assert (next_arg > prev);
    }
          
  if (next_arg > (header_len + body_len))
    {
      _dbus_verbose ("end of last arg at %d but message has len %d+%d=%d\n",
                     next_arg, header_len, body_len,
                     header_len + body_len);
      loader->corrupted = TRUE;
      goto failed;
    }

  message = dbus_message_new_empty_header ();
  if (message == NULL)
    {
      _dbus_verbose ("Failed to allocate empty message\n");
      oom = TRUE;
      goto failed;
    }

  message->byte_order = byte_order;
  message->header_padding = header_padding;
	  
  /* Copy in the offsets we found */
  i = 0;
  while (i <= DBUS_HEADER_FIELD_LAST)
    {
      message->header_fields[i] = fields[i];
      ++i;
    }
          
  if (!_dbus_list_append (&loader->messages, message))
    {
      _dbus_verbose ("Failed to append new message to loader queue\n");
      oom = TRUE;
      goto failed;
    }

  _dbus_assert (_dbus_string_get_length (&message->header) == 0);
  _dbus_assert (_dbus_string_get_length (&message->body) == 0);

  _dbus_assert (_dbus_string_get_length (&loader->data) >=
                (header_len + body_len));
          
  if (!_dbus_string_move_len (&loader->data, 0, header_len, &message->header, 0))
    {
      _dbus_verbose ("Failed to move header into new message\n");
      oom = TRUE;
      goto failed;
    }
          
  if (!_dbus_string_move_len (&loader->data, 0, body_len, &message->body, 0))
    {
      _dbus_verbose ("Failed to move body into new message\n");
      
      oom = TRUE;
      goto failed;
    }

  _dbus_assert (_dbus_string_get_length (&message->header) == header_len);
  _dbus_assert (_dbus_string_get_length (&message->body) == body_len);

  /* Fill in caches (we checked the types of these fields
   * earlier)
   */
  message->reply_serial = get_uint_field (message,
                                          DBUS_HEADER_FIELD_REPLY_SERIAL);
  
  message->client_serial = _dbus_demarshal_uint32 (&message->header,
                                                   message->byte_order,
                                                   CLIENT_SERIAL_OFFSET,
                                                   NULL);
  if (message->client_serial == 0 ||
      (message->header_fields[DBUS_HEADER_FIELD_REPLY_SERIAL].value_offset >= 0 && message->reply_serial == 0))
    {
      _dbus_verbose ("client_serial = %d reply_serial = %d, one of these no good\n",
                     message->client_serial,
                     message->reply_serial);
      
      loader->corrupted = TRUE;
      goto failed;
    }
  
  _dbus_verbose ("Loaded message %p\n", message);

  _dbus_assert (!oom);
  _dbus_assert (!loader->corrupted);

  return TRUE;

 failed:
  
  /* Clean up */
  
  if (message != NULL)
    {
      /* Put the data back so we can try again later if it was an OOM issue */
      if (_dbus_string_get_length (&message->body) > 0)
        {
          dbus_bool_t result;
          
          result = _dbus_string_copy_len (&message->body, 0, body_len,
                                          &loader->data, 0);
          
          _dbus_assert (result); /* because DBusString never reallocs smaller */
        }
      
      if (_dbus_string_get_length (&message->header) > 0)
        {
          dbus_bool_t result;
          
          result = _dbus_string_copy_len (&message->header, 0, header_len,
                                          &loader->data, 0);
          
          _dbus_assert (result); /* because DBusString never reallocs smaller */
        }

      /* does nothing if the message isn't in the list */
      _dbus_list_remove_last (&loader->messages, message);

      dbus_message_unref (message);
    }

  
  return !oom;
}

/**
 * Converts buffered data into messages.
 *
 * @todo we need to check that the proper named header fields exist
 * for each message type.
 * 
 * @todo If a message has unknown type, we should probably eat it
 * right here rather than passing it out to applications.  However
 * it's not an error to see messages of unknown type.
 * 
 * @param loader the loader.
 * @returns #TRUE if we had enough memory to finish.
 */
dbus_bool_t
_dbus_message_loader_queue_messages (DBusMessageLoader *loader)
{
  while (!loader->corrupted && _dbus_string_get_length (&loader->data) >= 16)
    {
      const char *header_data;
      int byte_order, message_type, header_len, body_len;
      dbus_uint32_t header_len_unsigned, body_len_unsigned;
      
      header_data = _dbus_string_get_const_data_len (&loader->data, 0, 16);

      _dbus_assert (_DBUS_ALIGN_ADDRESS (header_data, 4) == header_data);

      if (header_data[VERSION_OFFSET] != DBUS_MAJOR_PROTOCOL_VERSION)
        {
          _dbus_verbose ("Message has protocol version %d ours is %d\n",
                         (int) header_data[VERSION_OFFSET], DBUS_MAJOR_PROTOCOL_VERSION);
          loader->corrupted = TRUE;
          return TRUE;
        }
      
      byte_order = header_data[BYTE_ORDER_OFFSET];

      if (byte_order != DBUS_LITTLE_ENDIAN &&
	  byte_order != DBUS_BIG_ENDIAN)
	{
          _dbus_verbose ("Message with bad byte order '%c' received\n",
                         byte_order);
	  loader->corrupted = TRUE;
	  return TRUE;
	}

      /* Unknown types are ignored, but INVALID is
       * disallowed
       */
      message_type = header_data[TYPE_OFFSET];
      if (message_type == DBUS_MESSAGE_TYPE_INVALID)
        {
          _dbus_verbose ("Message with bad type '%d' received\n",
                         message_type);
	  loader->corrupted = TRUE;
	  return TRUE;
        }      
      
      header_len_unsigned = _dbus_unpack_uint32 (byte_order, 
                                                 (const unsigned char *) header_data + 4);
      body_len_unsigned = _dbus_unpack_uint32 (byte_order, 
                                               (const unsigned char *) header_data + 8);

      if (header_len_unsigned < 16)
        {
          _dbus_verbose ("Message had broken too-small header length %u\n",
                         header_len_unsigned);
          loader->corrupted = TRUE;
          return TRUE;
        }

      if (header_len_unsigned > (unsigned) MAX_SANE_MESSAGE_SIZE ||
          body_len_unsigned > (unsigned) MAX_SANE_MESSAGE_SIZE)
        {
          _dbus_verbose ("Header or body length too large (%u %u)\n",
                         header_len_unsigned,
                         body_len_unsigned);
          loader->corrupted = TRUE;
          return TRUE;
        }

      /* Now that we know the values are in signed range, get
       * rid of stupid unsigned, just causes bugs
       */
      header_len = header_len_unsigned;
      body_len = body_len_unsigned;

      if (_DBUS_ALIGN_VALUE (header_len, 8) != header_len_unsigned)
        {
	  
          _dbus_verbose ("header length %d is not aligned to 8 bytes\n",
                         header_len);
          loader->corrupted = TRUE;
          return TRUE;
        }
      
      if (header_len + body_len > loader->max_message_size)
	{
          _dbus_verbose ("Message claimed length header = %d body = %d exceeds max message length %ld\n",
                         header_len, body_len, loader->max_message_size);
	  loader->corrupted = TRUE;
	  return TRUE;
	}

      if (_dbus_string_get_length (&loader->data) >= (header_len + body_len))
	{
          if (!load_one_message (loader, byte_order, message_type,
                                 header_len, body_len))
            return FALSE;
	}
      else
        return TRUE;
    }

  return TRUE;
}

/**
 * Peeks at first loaded message, returns #NULL if no messages have
 * been queued.
 *
 * @param loader the loader.
 * @returns the next message, or #NULL if none.
 */
DBusMessage*
_dbus_message_loader_peek_message (DBusMessageLoader *loader)
{
  if (loader->messages)
    return loader->messages->data;
  else
    return NULL;
}

/**
 * Pops a loaded message (passing ownership of the message
 * to the caller). Returns #NULL if no messages have been
 * queued.
 *
 * @param loader the loader.
 * @returns the next message, or #NULL if none.
 */
DBusMessage*
_dbus_message_loader_pop_message (DBusMessageLoader *loader)
{
  return _dbus_list_pop_first (&loader->messages);
}

/**
 * Pops a loaded message inside a list link (passing ownership of the
 * message and link to the caller). Returns #NULL if no messages have
 * been loaded.
 *
 * @param loader the loader.
 * @returns the next message link, or #NULL if none.
 */
DBusList*
_dbus_message_loader_pop_message_link (DBusMessageLoader *loader)
{
  return _dbus_list_pop_first_link (&loader->messages);
}

/**
 * Returns a popped message link, used to undo a pop.
 *
 * @param loader the loader
 * @param link the link with a message in it
 */
void
_dbus_message_loader_putback_message_link (DBusMessageLoader  *loader,
                                           DBusList           *link)
{
  _dbus_list_prepend_link (&loader->messages, link);
}

/**
 * Checks whether the loader is confused due to bad data.
 * If messages are received that are invalid, the
 * loader gets confused and gives up permanently.
 * This state is called "corrupted."
 *
 * @param loader the loader
 * @returns #TRUE if the loader is hosed.
 */
dbus_bool_t
_dbus_message_loader_get_is_corrupted (DBusMessageLoader *loader)
{
  return loader->corrupted;
}

/**
 * Sets the maximum size message we allow.
 *
 * @param loader the loader
 * @param size the max message size in bytes
 */
void
_dbus_message_loader_set_max_message_size (DBusMessageLoader  *loader,
                                           long                size)
{
  if (size > MAX_SANE_MESSAGE_SIZE)
    {
      _dbus_verbose ("clamping requested max message size %ld to %d\n",
                     size, MAX_SANE_MESSAGE_SIZE);
      size = MAX_SANE_MESSAGE_SIZE;
    }
  loader->max_message_size = size;
}

/**
 * Gets the maximum allowed message size in bytes.
 *
 * @param loader the loader
 * @returns max size in bytes
 */
long
_dbus_message_loader_get_max_message_size (DBusMessageLoader  *loader)
{
  return loader->max_message_size;
}

static DBusDataSlotAllocator slot_allocator;
_DBUS_DEFINE_GLOBAL_LOCK (message_slots);

/**
 * Allocates an integer ID to be used for storing application-specific
 * data on any DBusMessage. The allocated ID may then be used
 * with dbus_message_set_data() and dbus_message_get_data().
 * The passed-in slot must be initialized to -1, and is filled in
 * with the slot ID. If the passed-in slot is not -1, it's assumed
 * to be already allocated, and its refcount is incremented.
 * 
 * The allocated slot is global, i.e. all DBusMessage objects will
 * have a slot with the given integer ID reserved.
 *
 * @param slot_p address of a global variable storing the slot
 * @returns #FALSE on failure (no memory)
 */
dbus_bool_t
dbus_message_allocate_data_slot (dbus_int32_t *slot_p)
{
  return _dbus_data_slot_allocator_alloc (&slot_allocator,
                                          _DBUS_LOCK_NAME (message_slots),
                                          slot_p);
}

/**
 * Deallocates a global ID for message data slots.
 * dbus_message_get_data() and dbus_message_set_data() may no
 * longer be used with this slot.  Existing data stored on existing
 * DBusMessage objects will be freed when the message is
 * finalized, but may not be retrieved (and may only be replaced if
 * someone else reallocates the slot).  When the refcount on the
 * passed-in slot reaches 0, it is set to -1.
 *
 * @param slot_p address storing the slot to deallocate
 */
void
dbus_message_free_data_slot (dbus_int32_t *slot_p)
{
  _dbus_return_if_fail (*slot_p >= 0);
  
  _dbus_data_slot_allocator_free (&slot_allocator, slot_p);
}

/**
 * Stores a pointer on a DBusMessage, along
 * with an optional function to be used for freeing
 * the data when the data is set again, or when
 * the message is finalized. The slot number
 * must have been allocated with dbus_message_allocate_data_slot().
 *
 * @param message the message
 * @param slot the slot number
 * @param data the data to store
 * @param free_data_func finalizer function for the data
 * @returns #TRUE if there was enough memory to store the data
 */
dbus_bool_t
dbus_message_set_data (DBusMessage     *message,
                       dbus_int32_t     slot,
                       void            *data,
                       DBusFreeFunction free_data_func)
{
  DBusFreeFunction old_free_func;
  void *old_data;
  dbus_bool_t retval;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (slot >= 0, FALSE);

  retval = _dbus_data_slot_list_set (&slot_allocator,
                                     &message->slot_list,
                                     slot, data, free_data_func,
                                     &old_free_func, &old_data);

  if (retval)
    {
      /* Do the actual free outside the message lock */
      if (old_free_func)
        (* old_free_func) (old_data);
    }

  return retval;
}

/**
 * Retrieves data previously set with dbus_message_set_data().
 * The slot must still be allocated (must not have been freed).
 *
 * @param message the message
 * @param slot the slot to get data from
 * @returns the data, or #NULL if not found
 */
void*
dbus_message_get_data (DBusMessage   *message,
                       dbus_int32_t   slot)
{
  void *res;

  _dbus_return_val_if_fail (message != NULL, NULL);
  
  res = _dbus_data_slot_list_get (&slot_allocator,
                                  &message->slot_list,
                                  slot);

  return res;
}

/**
 * Utility function to convert a machine-readable (not translated)
 * string into a D-BUS message type.
 *
 * @code
 *   "method_call"    -> DBUS_MESSAGE_TYPE_METHOD_CALL
 *   "method_return"  -> DBUS_MESSAGE_TYPE_METHOD_RETURN
 *   "signal"         -> DBUS_MESSAGE_TYPE_SIGNAL
 *   "error"          -> DBUS_MESSAGE_TYPE_ERROR
 *   anything else    -> DBUS_MESSAGE_TYPE_INVALID
 * @endcode
 * 
 */
int
dbus_message_type_from_string (const char *type_str)
{
  if (strcmp (type_str, "method_call") == 0)
    return DBUS_MESSAGE_TYPE_METHOD_CALL;
  if (strcmp (type_str, "method_return") == 0)
    return DBUS_MESSAGE_TYPE_METHOD_RETURN;
  else if (strcmp (type_str, "signal") == 0)
    return DBUS_MESSAGE_TYPE_SIGNAL;
  else if (strcmp (type_str, "error") == 0)
    return DBUS_MESSAGE_TYPE_ERROR;
  else
    return DBUS_MESSAGE_TYPE_INVALID;
}

/**
 * Utility function to convert a D-BUS message type into a
 * machine-readable string (not translated).
 *
 * @code
 *   DBUS_MESSAGE_TYPE_METHOD_CALL    -> "method_call"
 *   DBUS_MESSAGE_TYPE_METHOD_RETURN  -> "method_return"
 *   DBUS_MESSAGE_TYPE_SIGNAL         -> "signal"
 *   DBUS_MESSAGE_TYPE_ERROR          -> "error"
 *   DBUS_MESSAGE_TYPE_INVALID        -> "invalid"
 * @endcode
 * 
 */
const char *
dbus_message_type_to_string (int type)
{
  switch (type)
    {
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
      return "method_call";
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
      return "method_return";
    case DBUS_MESSAGE_TYPE_SIGNAL:
      return "signal";
    case DBUS_MESSAGE_TYPE_ERROR:
      return "error";
    default:
      return "invalid";
    }
}

/** @} */
#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>
#include <stdlib.h>

static void
message_iter_test (DBusMessage *message)
{
  DBusMessageIter iter, dict, dict2, array, array2;
  char *str;
  unsigned char *data;
  dbus_int32_t *our_int_array;
  int len;
  
  dbus_message_iter_init (message, &iter);

  /* String tests */
  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_STRING)
    _dbus_assert_not_reached ("Argument type isn't string");

  str = dbus_message_iter_get_string (&iter);
  if (strcmp (str, "Test string") != 0)
    _dbus_assert_not_reached ("Strings differ");
  dbus_free (str);

  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");

  /* Signed integer tests */
  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_INT32)
    _dbus_assert_not_reached ("Argument type isn't int32");

  if (dbus_message_iter_get_int32 (&iter) != -0x12345678)
    _dbus_assert_not_reached ("Signed integers differ");

  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of fields");
  
  /* Unsigned integer tests */
  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_UINT32)
    _dbus_assert_not_reached ("Argument type isn't int32");

  if (dbus_message_iter_get_uint32 (&iter) != 0xedd1e)
    _dbus_assert_not_reached ("Unsigned integers differ");

  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");

  /* Double tests */
  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_DOUBLE)
    _dbus_assert_not_reached ("Argument type isn't double");

  if (dbus_message_iter_get_double (&iter) != 3.14159)
    _dbus_assert_not_reached ("Doubles differ");

  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");

  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_ARRAY)
    _dbus_assert_not_reached ("Argument type not an array");

  if (dbus_message_iter_get_array_type (&iter) != DBUS_TYPE_DOUBLE)
    _dbus_assert_not_reached ("Array type not double");

  
  dbus_message_iter_init_array_iterator (&iter, &array, NULL);

  if (dbus_message_iter_get_arg_type (&array) != DBUS_TYPE_DOUBLE)
    _dbus_assert_not_reached ("Argument type isn't double");

  if (dbus_message_iter_get_double (&array) != 1.5)
    _dbus_assert_not_reached ("Unsigned integers differ");

  if (!dbus_message_iter_next (&array))
    _dbus_assert_not_reached ("Reached end of arguments");

  if (dbus_message_iter_get_arg_type (&array) != DBUS_TYPE_DOUBLE)
    _dbus_assert_not_reached ("Argument type isn't double");

  if (dbus_message_iter_get_double (&array) != 2.5)
    _dbus_assert_not_reached ("Unsigned integers differ");

  if (dbus_message_iter_next (&array))
    _dbus_assert_not_reached ("Didn't reach end of arguments");
  
  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");
  

  /* dict */

  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_DICT)
    _dbus_assert_not_reached ("not dict type");
     
  dbus_message_iter_init_dict_iterator (&iter, &dict);

  str = dbus_message_iter_get_dict_key (&dict);
  if (str == NULL || strcmp (str, "test") != 0)
    _dbus_assert_not_reached ("wrong dict key");
  dbus_free (str);

  if (dbus_message_iter_get_arg_type (&dict) != DBUS_TYPE_UINT32)
    _dbus_assert_not_reached ("wrong dict entry type");

  if (dbus_message_iter_get_uint32 (&dict) != 0xDEADBEEF)
    _dbus_assert_not_reached ("wrong dict entry value");

  /* dict (in dict) */

  if (!dbus_message_iter_next (&dict))
    _dbus_assert_not_reached ("reached end of dict");
  
  if (dbus_message_iter_get_arg_type (&dict) != DBUS_TYPE_DICT)
    _dbus_assert_not_reached ("not dict type");
    
  dbus_message_iter_init_dict_iterator (&dict, &dict2);
  
  str = dbus_message_iter_get_dict_key (&dict2);
  if (str == NULL || strcmp (str, "dictkey") != 0)
    _dbus_assert_not_reached ("wrong dict key");
  dbus_free (str);
  
  if (dbus_message_iter_get_arg_type (&dict2) != DBUS_TYPE_STRING)
    _dbus_assert_not_reached ("wrong dict entry type");
  
  str = dbus_message_iter_get_string (&dict2);
  if (str == NULL || strcmp (str, "dictvalue") != 0)
    _dbus_assert_not_reached ("wrong dict entry value");
  dbus_free (str);
  
  if (dbus_message_iter_next (&dict2))
    _dbus_assert_not_reached ("didn't reach end of dict");

  if (!dbus_message_iter_next (&dict))
    _dbus_assert_not_reached ("reached end of dict");
  
  /* array of array of int32 (in dict) */

  str = dbus_message_iter_get_dict_key (&dict);
  if (str == NULL || strcmp (str, "array") != 0)
    _dbus_assert_not_reached ("wrong dict key");
  dbus_free (str);
  
  if (dbus_message_iter_get_arg_type (&dict) != DBUS_TYPE_ARRAY)
    _dbus_assert_not_reached ("Argument type not an array");

  if (dbus_message_iter_get_array_type (&dict) != DBUS_TYPE_ARRAY)
    _dbus_assert_not_reached ("Array type not array");

  dbus_message_iter_init_array_iterator (&dict, &array, NULL);

  if (dbus_message_iter_get_arg_type (&array) != DBUS_TYPE_ARRAY)
    _dbus_assert_not_reached ("Argument type isn't array");
  
  if (dbus_message_iter_get_array_type (&array) != DBUS_TYPE_INT32)
    _dbus_assert_not_reached ("Array type not int32");
  
  dbus_message_iter_init_array_iterator (&array, &array2, NULL);

  if (dbus_message_iter_get_arg_type (&array2) != DBUS_TYPE_INT32)
    _dbus_assert_not_reached ("Argument type isn't int32");

  if (dbus_message_iter_get_int32 (&array2) != 0x12345678)
    _dbus_assert_not_reached ("Signed integers differ");

  if (!dbus_message_iter_next (&array2))
    _dbus_assert_not_reached ("Reached end of arguments");

  if (dbus_message_iter_get_int32 (&array2) != 0x23456781)
    _dbus_assert_not_reached ("Signed integers differ");

  if (dbus_message_iter_next (&array2))
    _dbus_assert_not_reached ("Didn't reached end of arguments");

  if (!dbus_message_iter_next (&array))
    _dbus_assert_not_reached ("Reached end of arguments");

  if (dbus_message_iter_get_array_type (&array) != DBUS_TYPE_INT32)
    _dbus_assert_not_reached ("Array type not int32");

  if (!dbus_message_iter_get_int32_array (&array,
					  &our_int_array,
					  &len))
    _dbus_assert_not_reached ("couldn't get int32 array");

  _dbus_assert (len == 3);
  _dbus_assert (our_int_array[0] == 0x34567812 &&
		our_int_array[1] == 0x45678123 &&
		our_int_array[2] == 0x56781234);
  dbus_free (our_int_array);
  
  if (dbus_message_iter_next (&array))
    _dbus_assert_not_reached ("Didn't reach end of array");

  if (dbus_message_iter_next (&dict))
    _dbus_assert_not_reached ("Didn't reach end of dict");
  
  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");
  
  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_BYTE)
    {
      _dbus_warn ("type was: %d\n", dbus_message_iter_get_arg_type (&iter));
      _dbus_assert_not_reached ("wrong type after dict (should be byte)");
    }
  
  if (dbus_message_iter_get_byte (&iter) != 0xF0)
    _dbus_assert_not_reached ("wrong value after dict");


  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");
  
  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_NIL)
    _dbus_assert_not_reached ("not a nil type");
  
  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");
  
  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_CUSTOM)
    _dbus_assert_not_reached ("wrong type after dict");

  if (!dbus_message_iter_get_custom (&iter, &str, &data, &len))
    _dbus_assert_not_reached ("failed to get custom type");

  _dbus_assert (strcmp (str, "MyTypeName")==0);
  _dbus_assert (len == 5);
  _dbus_assert (strcmp (data, "data")==0);
  dbus_free (str);
  dbus_free (data);
  
  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");

  if (dbus_message_iter_get_byte (&iter) != 0xF0)
    _dbus_assert_not_reached ("wrong value after custom");

  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");

  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_ARRAY)
    _dbus_assert_not_reached ("no array");

  if (dbus_message_iter_get_array_type (&iter) != DBUS_TYPE_INT32)
    _dbus_assert_not_reached ("Array type not int32");

  if (dbus_message_iter_init_array_iterator (&iter, &array, NULL))
    _dbus_assert_not_reached ("non empty array");
  
  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");

  if (dbus_message_iter_get_byte (&iter) != 0xF0)
    _dbus_assert_not_reached ("wrong value after empty array");

  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");

  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_DICT)
    _dbus_assert_not_reached ("non dict");

  if (dbus_message_iter_init_dict_iterator (&iter, &dict))
    _dbus_assert_not_reached ("non empty dict");

  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");

  if (dbus_message_iter_get_byte (&iter) != 0xF0)
    _dbus_assert_not_reached ("wrong value after empty dict");

  if (dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Didn't reach end of arguments");
}


static dbus_bool_t
check_message_handling_type (DBusMessageIter *iter,
			     int type)
{
  DBusMessageIter child_iter;
  
  switch (type)
    {
    case DBUS_TYPE_NIL:
      break;
    case DBUS_TYPE_BYTE:
      dbus_message_iter_get_byte (iter);
      break;
    case DBUS_TYPE_BOOLEAN:
      dbus_message_iter_get_boolean (iter);
      break;
    case DBUS_TYPE_INT32:
      dbus_message_iter_get_int32 (iter);
      break;
    case DBUS_TYPE_UINT32:
      dbus_message_iter_get_uint32 (iter);
      break;
    case DBUS_TYPE_INT64:
#ifdef DBUS_HAVE_INT64
      dbus_message_iter_get_int64 (iter);
#endif
      break;
    case DBUS_TYPE_UINT64:
#ifdef DBUS_HAVE_INT64
      dbus_message_iter_get_uint64 (iter);
#endif
      break;
    case DBUS_TYPE_DOUBLE:
      dbus_message_iter_get_double (iter);
      break;
    case DBUS_TYPE_STRING:
      {
	char *str;
	str = dbus_message_iter_get_string (iter);
	if (str == NULL)
	  {
	    _dbus_warn ("NULL string in message\n");
	    return FALSE;
	  }
	dbus_free (str);
      }
      break;
    case DBUS_TYPE_CUSTOM:
      {
	char *name;
	unsigned char *data;
	int len;
	
	if (!dbus_message_iter_get_custom (iter, &name, &data, &len))
	  {
	    _dbus_warn ("error reading name from custom type\n");
	    return FALSE;
	  }
	dbus_free (data);
	dbus_free (name);
      }
      break;
    case DBUS_TYPE_ARRAY:
      {
	int array_type;

	dbus_message_iter_init_array_iterator (iter, &child_iter, &array_type);

	while (dbus_message_iter_has_next (&child_iter))
	  {
	    if (!check_message_handling_type (&child_iter, array_type))
	      {
		_dbus_warn ("error in array element\n");
		return FALSE;
	      }
	    
	    if (!dbus_message_iter_next (&child_iter))
	      break;
	  }
      }
      break;
    case DBUS_TYPE_DICT:
      {
	int entry_type;
	char *key;
	
	dbus_message_iter_init_dict_iterator (iter, &child_iter);

	while ((entry_type = dbus_message_iter_get_arg_type (&child_iter)) != DBUS_TYPE_INVALID)
	  {
	    key = dbus_message_iter_get_dict_key (&child_iter);
	    if (key == NULL)
	      {
		_dbus_warn ("error reading dict key\n");
		return FALSE;
	      }
	    dbus_free (key);
	    
	    if (!check_message_handling_type (&child_iter, entry_type))
	      {
		_dbus_warn ("error in dict value\n");
		return FALSE;
	      }
	    
	    if (!dbus_message_iter_next (&child_iter))
	      break;
	  }
      }
      break;
      
    default:
      _dbus_warn ("unknown type %d\n", type);
      return FALSE;
      break;
    }
  return TRUE;
}
  
  
static dbus_bool_t
check_message_handling (DBusMessage *message)
{
  DBusMessageIter iter;
  int type;
  dbus_bool_t retval;
  dbus_uint32_t client_serial;
  
  retval = FALSE;
  
  client_serial = dbus_message_get_serial (message);

  /* can't use set_serial due to the assertions at the start of it */
  _dbus_marshal_set_uint32 (&message->header,
                            message->byte_order,
                            CLIENT_SERIAL_OFFSET,
                            client_serial);
  
  if (client_serial != dbus_message_get_serial (message))
    {
      _dbus_warn ("get/set cycle for client_serial did not succeed\n");
      goto failed;
    }
  
  /* If we implement message_set_arg (message, n, value)
   * then we would want to test it here
   */

  dbus_message_iter_init (message, &iter);
  while ((type = dbus_message_iter_get_arg_type (&iter)) != DBUS_TYPE_INVALID)
    {
      if (!check_message_handling_type (&iter, type))
	goto failed;

      if (!dbus_message_iter_next (&iter))
        break;
    }
  
  retval = TRUE;
  
 failed:
  return retval;
}

static dbus_bool_t
check_have_valid_message (DBusMessageLoader *loader)
{
  DBusMessage *message;
  dbus_bool_t retval;

  message = NULL;
  retval = FALSE;

  if (!_dbus_message_loader_queue_messages (loader))
    _dbus_assert_not_reached ("no memory to queue messages");
  
  if (_dbus_message_loader_get_is_corrupted (loader))
    {
      _dbus_warn ("loader corrupted on message that was expected to be valid\n");
      goto failed;
    }
  
  message = _dbus_message_loader_pop_message (loader);
  if (message == NULL)
    {
      _dbus_warn ("didn't load message that was expected to be valid (message not popped)\n");
      goto failed;
    }
  
  if (_dbus_string_get_length (&loader->data) > 0)
    {
      _dbus_warn ("had leftover bytes from expected-to-be-valid single message\n");
      goto failed;
    }

  /* Verify that we're able to properly deal with the message.
   * For example, this would detect improper handling of messages
   * in nonstandard byte order.
   */
  if (!check_message_handling (message))
    goto failed;  
  
  retval = TRUE;

 failed:
  if (message)
    dbus_message_unref (message);

  return retval;
}

static dbus_bool_t
check_invalid_message (DBusMessageLoader *loader)
{
  dbus_bool_t retval;

  retval = FALSE;

  if (!_dbus_message_loader_queue_messages (loader))
    _dbus_assert_not_reached ("no memory to queue messages");
  
  if (!_dbus_message_loader_get_is_corrupted (loader))
    {
      _dbus_warn ("loader not corrupted on message that was expected to be invalid\n");
      goto failed;
    }

  retval = TRUE;

 failed:
  return retval;
}

static dbus_bool_t
check_incomplete_message (DBusMessageLoader *loader)
{
  DBusMessage *message;
  dbus_bool_t retval;

  message = NULL;
  retval = FALSE;

  if (!_dbus_message_loader_queue_messages (loader))
    _dbus_assert_not_reached ("no memory to queue messages");
  
  if (_dbus_message_loader_get_is_corrupted (loader))
    {
      _dbus_warn ("loader corrupted on message that was expected to be valid (but incomplete)\n");
      goto failed;
    }
  
  message = _dbus_message_loader_pop_message (loader);
  if (message != NULL)
    {
      _dbus_warn ("loaded message that was expected to be incomplete\n");
      goto failed;
    }

  retval = TRUE;

 failed:
  if (message)
    dbus_message_unref (message);
  return retval;
}

static dbus_bool_t
check_loader_results (DBusMessageLoader      *loader,
                      DBusMessageValidity     validity)
{
  if (!_dbus_message_loader_queue_messages (loader))
    _dbus_assert_not_reached ("no memory to queue messages");
  
  switch (validity)
    {
    case _DBUS_MESSAGE_VALID:
      return check_have_valid_message (loader);
    case _DBUS_MESSAGE_INVALID:
      return check_invalid_message (loader);
    case _DBUS_MESSAGE_INCOMPLETE:
      return check_incomplete_message (loader);
    case _DBUS_MESSAGE_UNKNOWN:
      return TRUE;
    }

  _dbus_assert_not_reached ("bad DBusMessageValidity");
  return FALSE;
}


/**
 * Loads the message in the given message file.
 *
 * @param filename filename to load
 * @param is_raw if #TRUE load as binary data, if #FALSE as message builder language
 * @param data string to load message into
 * @returns #TRUE if the message was loaded
 */
dbus_bool_t
dbus_internal_do_not_use_load_message_file (const DBusString    *filename,
                                            dbus_bool_t          is_raw,
                                            DBusString          *data)
{
  dbus_bool_t retval;

  retval = FALSE;  

  if (is_raw)
    {
      DBusError error;

      _dbus_verbose ("Loading raw %s\n", _dbus_string_get_const_data (filename));
      dbus_error_init (&error);
      if (!_dbus_file_get_contents (data, filename, &error))
        {
          _dbus_warn ("Could not load message file %s: %s\n",
                      _dbus_string_get_const_data (filename),
                      error.message);
          dbus_error_free (&error);
          goto failed;
        }
    }
  else
    {
      if (!_dbus_message_data_load (data, filename))
        {
          _dbus_warn ("Could not load message file %s\n",
                      _dbus_string_get_const_data (filename));
          goto failed;
        }
    }

  retval = TRUE;
  
 failed:

  return retval;
}

/**
 * Tries loading the message in the given message file
 * and verifies that DBusMessageLoader can handle it.
 *
 * @param filename filename to load
 * @param is_raw if #TRUE load as binary data, if #FALSE as message builder language
 * @param expected_validity what the message has to be like to return #TRUE
 * @returns #TRUE if the message has the expected validity
 */
dbus_bool_t
dbus_internal_do_not_use_try_message_file (const DBusString    *filename,
                                           dbus_bool_t          is_raw,
                                           DBusMessageValidity  expected_validity)
{
  DBusString data;
  dbus_bool_t retval;

  retval = FALSE;
  
  if (!_dbus_string_init (&data))
    _dbus_assert_not_reached ("could not allocate string\n");

  if (!dbus_internal_do_not_use_load_message_file (filename, is_raw,
                                                   &data))
    goto failed;

  retval = dbus_internal_do_not_use_try_message_data (&data, expected_validity);

 failed:

  if (!retval)
    {
      if (_dbus_string_get_length (&data) > 0)
        _dbus_verbose_bytes_of_string (&data, 0,
                                       _dbus_string_get_length (&data));
      
      _dbus_warn ("Failed message loader test on %s\n",
                  _dbus_string_get_const_data (filename));
    }
  
  _dbus_string_free (&data);

  return retval;
}

/**
 * Tries loading the given message data.
 *
 *
 * @param data the message data
 * @param expected_validity what the message has to be like to return #TRUE
 * @returns #TRUE if the message has the expected validity
 */
dbus_bool_t
dbus_internal_do_not_use_try_message_data (const DBusString    *data,
                                           DBusMessageValidity  expected_validity)
{
  DBusMessageLoader *loader;
  dbus_bool_t retval;
  int len;
  int i;

  loader = NULL;
  retval = FALSE;

  /* Write the data one byte at a time */
  
  loader = _dbus_message_loader_new ();

  /* check some trivial loader functions */
  _dbus_message_loader_ref (loader);
  _dbus_message_loader_unref (loader);
  _dbus_message_loader_get_max_message_size (loader);
  
  len = _dbus_string_get_length (data);
  for (i = 0; i < len; i++)
    {
      DBusString *buffer;

      _dbus_message_loader_get_buffer (loader, &buffer);
      _dbus_string_append_byte (buffer,
                                _dbus_string_get_byte (data, i));
      _dbus_message_loader_return_buffer (loader, buffer, 1);
    }
  
  if (!check_loader_results (loader, expected_validity))
    goto failed;

  _dbus_message_loader_unref (loader);
  loader = NULL;

  /* Write the data all at once */
  
  loader = _dbus_message_loader_new ();

  {
    DBusString *buffer;
    
    _dbus_message_loader_get_buffer (loader, &buffer);
    _dbus_string_copy (data, 0, buffer,
                       _dbus_string_get_length (buffer));
    _dbus_message_loader_return_buffer (loader, buffer, 1);
  }
  
  if (!check_loader_results (loader, expected_validity))
    goto failed;

  _dbus_message_loader_unref (loader);
  loader = NULL;  

  /* Write the data 2 bytes at a time */
  
  loader = _dbus_message_loader_new ();

  len = _dbus_string_get_length (data);
  for (i = 0; i < len; i += 2)
    {
      DBusString *buffer;

      _dbus_message_loader_get_buffer (loader, &buffer);
      _dbus_string_append_byte (buffer,
                                _dbus_string_get_byte (data, i));
      if ((i+1) < len)
        _dbus_string_append_byte (buffer,
                                  _dbus_string_get_byte (data, i+1));
      _dbus_message_loader_return_buffer (loader, buffer, 1);
    }
  
  if (!check_loader_results (loader, expected_validity))
    goto failed;

  _dbus_message_loader_unref (loader);
  loader = NULL;
  
  retval = TRUE;
  
 failed:
  
  if (loader)
    _dbus_message_loader_unref (loader);
  
  return retval;
}

static dbus_bool_t
process_test_subdir (const DBusString          *test_base_dir,
                     const char                *subdir,
                     DBusMessageValidity        validity,
                     DBusForeachMessageFileFunc function,
                     void                      *user_data)
{
  DBusString test_directory;
  DBusString filename;
  DBusDirIter *dir;
  dbus_bool_t retval;
  DBusError error;

  retval = FALSE;
  dir = NULL;
  
  if (!_dbus_string_init (&test_directory))
    _dbus_assert_not_reached ("didn't allocate test_directory\n");

  _dbus_string_init_const (&filename, subdir);
  
  if (!_dbus_string_copy (test_base_dir, 0,
                          &test_directory, 0))
    _dbus_assert_not_reached ("couldn't copy test_base_dir to test_directory");
  
  if (!_dbus_concat_dir_and_file (&test_directory, &filename))    
    _dbus_assert_not_reached ("couldn't allocate full path");

  _dbus_string_free (&filename);
  if (!_dbus_string_init (&filename))
    _dbus_assert_not_reached ("didn't allocate filename string\n");

  dbus_error_init (&error);
  dir = _dbus_directory_open (&test_directory, &error);
  if (dir == NULL)
    {
      _dbus_warn ("Could not open %s: %s\n",
                  _dbus_string_get_const_data (&test_directory),
                  error.message);
      dbus_error_free (&error);
      goto failed;
    }

  printf ("Testing %s:\n", subdir);
  
 next:
  while (_dbus_directory_get_next_file (dir, &filename, &error))
    {
      DBusString full_path;
      dbus_bool_t is_raw;
      
      if (!_dbus_string_init (&full_path))
        _dbus_assert_not_reached ("couldn't init string");

      if (!_dbus_string_copy (&test_directory, 0, &full_path, 0))
        _dbus_assert_not_reached ("couldn't copy dir to full_path");

      if (!_dbus_concat_dir_and_file (&full_path, &filename))
        _dbus_assert_not_reached ("couldn't concat file to dir");

      if (_dbus_string_ends_with_c_str (&filename, ".message"))
        is_raw = FALSE;
      else if (_dbus_string_ends_with_c_str (&filename, ".message-raw"))
        is_raw = TRUE;
      else
        {
          _dbus_verbose ("Skipping non-.message file %s\n",
                         _dbus_string_get_const_data (&filename));
	  _dbus_string_free (&full_path);
          goto next;
        }

      printf ("    %s\n",
              _dbus_string_get_const_data (&filename));
      
      _dbus_verbose (" expecting %s for %s\n",
                     validity == _DBUS_MESSAGE_VALID ? "valid" :
                     (validity == _DBUS_MESSAGE_INVALID ? "invalid" :
                      (validity == _DBUS_MESSAGE_INCOMPLETE ? "incomplete" : "unknown")),
                     _dbus_string_get_const_data (&filename));
      
      if (! (*function) (&full_path, is_raw, validity, user_data))
        {
          _dbus_string_free (&full_path);
          goto failed;
        }
      else
        _dbus_string_free (&full_path);
    }

  if (dbus_error_is_set (&error))
    {
      _dbus_warn ("Could not get next file in %s: %s\n",
                  _dbus_string_get_const_data (&test_directory),
                  error.message);
      dbus_error_free (&error);
      goto failed;
    }
    
  retval = TRUE;
  
 failed:

  if (dir)
    _dbus_directory_close (dir);
  _dbus_string_free (&test_directory);
  _dbus_string_free (&filename);

  return retval;
}
                     
/**
 * Runs the given function on every message file in the test suite.
 * The function should return #FALSE on test failure or fatal error.
 *
 * @param test_data_dir root dir of the test suite data files (top_srcdir/test/data)
 * @param func the function to run
 * @param user_data data for function
 * @returns #FALSE if there's a failure
 */
dbus_bool_t
dbus_internal_do_not_use_foreach_message_file (const char                *test_data_dir,
                                               DBusForeachMessageFileFunc func,
                                               void                      *user_data)
{
  DBusString test_directory;
  dbus_bool_t retval;

  retval = FALSE;
  
  _dbus_string_init_const (&test_directory, test_data_dir);

  if (!process_test_subdir (&test_directory, "valid-messages",
                            _DBUS_MESSAGE_VALID, func, user_data))
    goto failed;

  if (!process_test_subdir (&test_directory, "invalid-messages",
                            _DBUS_MESSAGE_INVALID, func, user_data))
    goto failed;
  
  if (!process_test_subdir (&test_directory, "incomplete-messages",
                            _DBUS_MESSAGE_INCOMPLETE, func, user_data))
    goto failed;

  retval = TRUE;
  
 failed:

  _dbus_string_free (&test_directory);
  
  return retval;
}

static void
verify_test_message (DBusMessage *message)
{
  DBusMessageIter iter, dict;
  DBusError error;
  dbus_int32_t our_int;
  char *our_str;
  double our_double;
  dbus_bool_t our_bool;
  unsigned char our_byte_1, our_byte_2;
  dbus_uint32_t our_uint32;
  dbus_int32_t *our_uint32_array;
  int our_uint32_array_len;
  dbus_int32_t *our_int32_array;
  int our_int32_array_len;
  char **our_string_array;
  int our_string_array_len;
#ifdef DBUS_HAVE_INT64
  dbus_int64_t our_int64;
  dbus_uint64_t our_uint64;
  dbus_int64_t *our_uint64_array;
  int our_uint64_array_len;
  dbus_int64_t *our_int64_array;
  int our_int64_array_len;
#endif
  double *our_double_array;
  int our_double_array_len;
  unsigned char *our_byte_array;
  int our_byte_array_len;
  unsigned char *our_boolean_array;
  int our_boolean_array_len;
  
  dbus_message_iter_init (message, &iter);

  dbus_error_init (&error);
  if (!dbus_message_iter_get_args (&iter, &error,
				   DBUS_TYPE_INT32, &our_int,
#ifdef DBUS_HAVE_INT64
                                   DBUS_TYPE_INT64, &our_int64,
                                   DBUS_TYPE_UINT64, &our_uint64,
#endif
				   DBUS_TYPE_STRING, &our_str,
				   DBUS_TYPE_DOUBLE, &our_double,
				   DBUS_TYPE_BOOLEAN, &our_bool,
				   DBUS_TYPE_BYTE, &our_byte_1,
				   DBUS_TYPE_BYTE, &our_byte_2,
				   DBUS_TYPE_NIL,
				   DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32,
                                   &our_uint32_array, &our_uint32_array_len,
                                   DBUS_TYPE_ARRAY, DBUS_TYPE_INT32,
                                   &our_int32_array, &our_int32_array_len,
#ifdef DBUS_HAVE_INT64
				   DBUS_TYPE_ARRAY, DBUS_TYPE_UINT64,
                                   &our_uint64_array, &our_uint64_array_len,
                                   DBUS_TYPE_ARRAY, DBUS_TYPE_INT64,
                                   &our_int64_array, &our_int64_array_len,
#endif
                                   DBUS_TYPE_ARRAY, DBUS_TYPE_STRING,
                                   &our_string_array, &our_string_array_len,
                                   DBUS_TYPE_ARRAY, DBUS_TYPE_DOUBLE,
                                   &our_double_array, &our_double_array_len,
                                   DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
                                   &our_byte_array, &our_byte_array_len,
                                   DBUS_TYPE_ARRAY, DBUS_TYPE_BOOLEAN,
                                   &our_boolean_array, &our_boolean_array_len,
				   0))
    {
      _dbus_warn ("error: %s - %s\n", error.name,
                  (error.message != NULL) ? error.message : "no message");
      _dbus_assert_not_reached ("Could not get arguments");
    }

  if (our_int != -0x12345678)
    _dbus_assert_not_reached ("integers differ!");

#ifdef DBUS_HAVE_INT64
  if (our_int64 != DBUS_INT64_CONSTANT (-0x123456789abcd))
    _dbus_assert_not_reached ("64-bit integers differ!");
  if (our_uint64 != DBUS_UINT64_CONSTANT (0x123456789abcd))
    _dbus_assert_not_reached ("64-bit unsigned integers differ!");
#endif
  
  if (our_double != 3.14159)
    _dbus_assert_not_reached ("doubles differ!");

  if (strcmp (our_str, "Test string") != 0)
    _dbus_assert_not_reached ("strings differ!");
  dbus_free (our_str);

  if (!our_bool)
    _dbus_assert_not_reached ("booleans differ");

  if (our_byte_1 != 42)
    _dbus_assert_not_reached ("bytes differ!");

  if (our_byte_2 != 24)
    _dbus_assert_not_reached ("bytes differ!");

  if (our_uint32_array_len != 4 ||
      our_uint32_array[0] != 0x12345678 ||
      our_uint32_array[1] != 0x23456781 ||
      our_uint32_array[2] != 0x34567812 ||
      our_uint32_array[3] != 0x45678123)
    _dbus_assert_not_reached ("uint array differs");
  dbus_free (our_uint32_array);

  if (our_int32_array_len != 4 ||
      our_int32_array[0] != 0x12345678 ||
      our_int32_array[1] != -0x23456781 ||
      our_int32_array[2] != 0x34567812 ||
      our_int32_array[3] != -0x45678123)
    _dbus_assert_not_reached ("int array differs");
  dbus_free (our_int32_array);

#ifdef DBUS_HAVE_INT64
  if (our_uint64_array_len != 4 ||
      our_uint64_array[0] != 0x12345678 ||
      our_uint64_array[1] != 0x23456781 ||
      our_uint64_array[2] != 0x34567812 ||
      our_uint64_array[3] != 0x45678123)
    _dbus_assert_not_reached ("uint64 array differs");
  dbus_free (our_uint64_array);
  
  if (our_int64_array_len != 4 ||
      our_int64_array[0] != 0x12345678 ||
      our_int64_array[1] != -0x23456781 ||
      our_int64_array[2] != 0x34567812 ||
      our_int64_array[3] != -0x45678123)
    _dbus_assert_not_reached ("int64 array differs");
  dbus_free (our_int64_array);
#endif /* DBUS_HAVE_INT64 */
  
  if (our_string_array_len != 4)
    _dbus_assert_not_reached ("string array has wrong length");

  if (strcmp (our_string_array[0], "Foo") != 0 ||
      strcmp (our_string_array[1], "bar") != 0 ||
      strcmp (our_string_array[2], "") != 0 ||
      strcmp (our_string_array[3], "woo woo woo woo") != 0)
    _dbus_assert_not_reached ("string array differs");

  dbus_free_string_array (our_string_array);

  if (our_double_array_len != 3)
    _dbus_assert_not_reached ("double array had wrong length");

  /* On all IEEE machines (i.e. everything sane) exact equality
   * should be preserved over the wire
   */
  if (our_double_array[0] != 0.1234 ||
      our_double_array[1] != 9876.54321 ||
      our_double_array[2] != -300.0)
    _dbus_assert_not_reached ("double array had wrong values");

  dbus_free (our_double_array);

  if (our_byte_array_len != 4)
    _dbus_assert_not_reached ("byte array had wrong length");

  if (our_byte_array[0] != 'a' ||
      our_byte_array[1] != 'b' ||
      our_byte_array[2] != 'c' ||
      our_byte_array[3] != 234)
    _dbus_assert_not_reached ("byte array had wrong values");

  dbus_free (our_byte_array);

  if (our_boolean_array_len != 5)
    _dbus_assert_not_reached ("bool array had wrong length");

  if (our_boolean_array[0] != TRUE ||
      our_boolean_array[1] != FALSE ||
      our_boolean_array[2] != TRUE ||
      our_boolean_array[3] != TRUE ||
      our_boolean_array[4] != FALSE)
    _dbus_assert_not_reached ("bool array had wrong values");

  dbus_free (our_boolean_array);

  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_DICT)
    _dbus_assert_not_reached ("not dict type");
     
  dbus_message_iter_init_dict_iterator (&iter, &dict);

  our_str = dbus_message_iter_get_dict_key (&dict);
  if (our_str == NULL || strcmp (our_str, "test") != 0)
    _dbus_assert_not_reached ("wrong dict key");
  dbus_free (our_str);

  if (dbus_message_iter_get_arg_type (&dict) != DBUS_TYPE_UINT32)
    {
      _dbus_verbose ("dict entry type: %d\n", dbus_message_iter_get_arg_type (&dict));
      _dbus_assert_not_reached ("wrong dict entry type");
    }

  if ((our_uint32 = dbus_message_iter_get_uint32 (&dict)) != 0xDEADBEEF)
    {
      _dbus_verbose ("dict entry val: %x\n", our_uint32);
      _dbus_assert_not_reached ("wrong dict entry value");
    }

  if (dbus_message_iter_next (&dict))
    _dbus_assert_not_reached ("Didn't reach end of dict");
  
  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");
  
  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_UINT32)
    _dbus_assert_not_reached ("wrong type after dict");
  
  if (dbus_message_iter_get_uint32 (&iter) != 0xCAFEBABE)
    _dbus_assert_not_reached ("wrong value after dict");

  if (dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Didn't reach end of arguments");
}

/**
 * @ingroup DBusMessageInternals
 * Unit test for DBusMessage.
 *
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_message_test (const char *test_data_dir)
{
  DBusMessage *message;
  DBusMessageLoader *loader;
  DBusMessageIter iter, child_iter, child_iter2, child_iter3;
  int i;
  const char *data;
  DBusMessage *copy;
  const char *name1;
  const char *name2;
  const dbus_uint32_t our_uint32_array[] =
    { 0x12345678, 0x23456781, 0x34567812, 0x45678123 };
  const dbus_uint32_t our_int32_array[] =
    { 0x12345678, -0x23456781, 0x34567812, -0x45678123 };
#ifdef DBUS_HAVE_INT64
  const dbus_uint64_t our_uint64_array[] =
    { 0x12345678, 0x23456781, 0x34567812, 0x45678123 };
  const dbus_uint64_t our_int64_array[] =
    { 0x12345678, -0x23456781, 0x34567812, -0x45678123 };
#endif
  const char *our_string_array[] = { "Foo", "bar", "", "woo woo woo woo" };
  const double our_double_array[] = { 0.1234, 9876.54321, -300.0 };
  const unsigned char our_byte_array[] = { 'a', 'b', 'c', 234 };
  const unsigned char our_boolean_array[] = { TRUE, FALSE, TRUE, TRUE, FALSE };
  char sig[64];
  const char *s;
  char *t;
  DBusError error;
  
  _dbus_assert (sizeof (DBusMessageRealIter) <= sizeof (DBusMessageIter));

  message = dbus_message_new_method_call ("org.freedesktop.DBus.TestService",
                                          "/org/freedesktop/TestPath",
                                          "Foo.TestInterface",
                                          "TestMethod");
  _dbus_assert (dbus_message_has_destination (message, "org.freedesktop.DBus.TestService"));
  _dbus_assert (dbus_message_is_method_call (message, "Foo.TestInterface",
                                             "TestMethod"));
  _dbus_assert (strcmp (dbus_message_get_path (message),
                        "/org/freedesktop/TestPath") == 0);
  _dbus_message_set_serial (message, 1234);
  /* string length including nul byte not a multiple of 4 */
  if (!dbus_message_set_sender (message, "org.foo.bar1"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (dbus_message_has_sender (message, "org.foo.bar1"));
  dbus_message_set_reply_serial (message, 5678);
  if (!dbus_message_set_sender (message, NULL))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (!dbus_message_has_sender (message, "org.foo.bar1"));
  _dbus_assert (dbus_message_get_serial (message) == 1234);
  _dbus_assert (dbus_message_get_reply_serial (message) == 5678);
  _dbus_assert (dbus_message_has_destination (message, "org.freedesktop.DBus.TestService"));

  _dbus_assert (dbus_message_get_no_reply (message) == FALSE);
  dbus_message_set_no_reply (message, TRUE);
  _dbus_assert (dbus_message_get_no_reply (message) == TRUE);
  dbus_message_set_no_reply (message, FALSE);
  _dbus_assert (dbus_message_get_no_reply (message) == FALSE);

  /* Set/get some header fields */
  
  if (!dbus_message_set_path (message, "/foo"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_path (message),
                        "/foo") == 0);

  if (!dbus_message_set_interface (message, "org.Foo"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_interface (message),
                        "org.Foo") == 0);
  
  if (!dbus_message_set_member (message, "Bar"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_member (message),
                        "Bar") == 0);

  /* Set/get them with longer values */
  if (!dbus_message_set_path (message, "/foo/bar"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_path (message),
                        "/foo/bar") == 0);

  if (!dbus_message_set_interface (message, "org.Foo.Bar"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_interface (message),
                        "org.Foo.Bar") == 0);
  
  if (!dbus_message_set_member (message, "BarFoo"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_member (message),
                        "BarFoo") == 0);

  /* Realloc shorter again */
  
  if (!dbus_message_set_path (message, "/foo"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_path (message),
                        "/foo") == 0);

  if (!dbus_message_set_interface (message, "org.Foo"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_interface (message),
                        "org.Foo") == 0);
  
  if (!dbus_message_set_member (message, "Bar"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_member (message),
                        "Bar") == 0);
  
  dbus_message_unref (message);
  
  /* Test the vararg functions */
  message = dbus_message_new_method_call ("org.freedesktop.DBus.TestService",
                                          "/org/freedesktop/TestPath",
                                          "Foo.TestInterface",
                                          "TestMethod");
  _dbus_message_set_serial (message, 1);
  dbus_message_append_args (message,
			    DBUS_TYPE_INT32, -0x12345678,
#ifdef DBUS_HAVE_INT64
                            DBUS_TYPE_INT64, DBUS_INT64_CONSTANT (-0x123456789abcd),
                            DBUS_TYPE_UINT64, DBUS_UINT64_CONSTANT (0x123456789abcd),
#endif
			    DBUS_TYPE_STRING, "Test string",
			    DBUS_TYPE_DOUBLE, 3.14159,
			    DBUS_TYPE_BOOLEAN, TRUE,
			    DBUS_TYPE_BYTE, (unsigned char) 42,
			    DBUS_TYPE_BYTE, 24,
			    DBUS_TYPE_NIL,
			    DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, our_uint32_array,
                            _DBUS_N_ELEMENTS (our_uint32_array),
                            DBUS_TYPE_ARRAY, DBUS_TYPE_INT32, our_int32_array,
                            _DBUS_N_ELEMENTS (our_int32_array),
#ifdef DBUS_HAVE_INT64
                            DBUS_TYPE_ARRAY, DBUS_TYPE_UINT64, our_uint64_array,
                            _DBUS_N_ELEMENTS (our_uint64_array),
                            DBUS_TYPE_ARRAY, DBUS_TYPE_INT64, our_int64_array,
                            _DBUS_N_ELEMENTS (our_int64_array),
#endif
                            DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, our_string_array,
                            _DBUS_N_ELEMENTS (our_string_array),
                            DBUS_TYPE_ARRAY, DBUS_TYPE_DOUBLE, our_double_array,
                            _DBUS_N_ELEMENTS (our_double_array),
                            DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, our_byte_array,
                            _DBUS_N_ELEMENTS (our_byte_array),
                            DBUS_TYPE_ARRAY, DBUS_TYPE_BOOLEAN, our_boolean_array,
                            _DBUS_N_ELEMENTS (our_boolean_array),
			    0);
  
  dbus_message_append_iter_init (message, &iter);
  dbus_message_iter_append_dict (&iter, &child_iter);
  dbus_message_iter_append_dict_key (&child_iter, "test");
  dbus_message_iter_append_uint32 (&child_iter, 0xDEADBEEF);
  dbus_message_iter_append_uint32 (&iter, 0xCAFEBABE);

  i = 0;
  sig[i++] = DBUS_TYPE_INT32;
#ifdef DBUS_HAVE_INT64
  sig[i++] = DBUS_TYPE_INT64;
  sig[i++] = DBUS_TYPE_UINT64;
#endif
  sig[i++] = DBUS_TYPE_STRING;
  sig[i++] = DBUS_TYPE_DOUBLE;
  sig[i++] = DBUS_TYPE_BOOLEAN;
  sig[i++] = DBUS_TYPE_BYTE;
  sig[i++] = DBUS_TYPE_BYTE;
  sig[i++] = DBUS_TYPE_NIL;
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_UINT32;
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_INT32;
#ifdef DBUS_HAVE_INT64
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_UINT64;
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_INT64;
#endif
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_STRING;
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_DOUBLE;
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_BYTE;
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_BOOLEAN;
  sig[i++] = DBUS_TYPE_DICT;
  sig[i++] = DBUS_TYPE_UINT32;
  sig[i++] = DBUS_TYPE_INVALID;

  _dbus_assert (i < (int) _DBUS_N_ELEMENTS (sig));
  
  _dbus_verbose_bytes_of_string (&message->header, 0,
                                 _dbus_string_get_length (&message->header));
  _dbus_verbose_bytes_of_string (&message->body, 0,
                                 _dbus_string_get_length (&message->body));
  
  _dbus_verbose ("Signature expected \"%s\" actual \"%s\"\n",
                 sig, dbus_message_get_signature (message));
  
  s = dbus_message_get_signature (message);
  
  _dbus_assert (dbus_message_has_signature (message, sig));
  _dbus_assert (strcmp (s, sig) == 0);
  
  verify_test_message (message);

  copy = dbus_message_copy (message);
  
  _dbus_assert (message->client_serial == copy->client_serial);
  _dbus_assert (message->reply_serial == copy->reply_serial);
  _dbus_assert (message->header_padding == copy->header_padding);
  
  _dbus_assert (_dbus_string_get_length (&message->header) ==
                _dbus_string_get_length (&copy->header));

  _dbus_assert (_dbus_string_get_length (&message->body) ==
                _dbus_string_get_length (&copy->body));
  
  verify_test_message (copy);

  name1 = dbus_message_get_interface (message);
  name2 = dbus_message_get_interface (copy);

  _dbus_assert (strcmp (name1, name2) == 0);

  name1 = dbus_message_get_member (message);
  name2 = dbus_message_get_member (copy);

  _dbus_assert (strcmp (name1, name2) == 0);
  
  dbus_message_unref (message);  
  dbus_message_unref (copy);

  message = dbus_message_new_method_call ("org.freedesktop.DBus.TestService",
                                          "/org/freedesktop/TestPath",
                                          "Foo.TestInterface",
                                          "TestMethod");

  _dbus_message_set_serial (message, 1);
  dbus_message_set_reply_serial (message, 0x12345678);

  dbus_message_append_iter_init (message, &iter);
  dbus_message_iter_append_string (&iter, "Test string");
  dbus_message_iter_append_int32 (&iter, -0x12345678);
  dbus_message_iter_append_uint32 (&iter, 0xedd1e);
  dbus_message_iter_append_double (&iter, 3.14159);

  dbus_message_iter_append_array (&iter, &child_iter, DBUS_TYPE_DOUBLE);
  dbus_message_iter_append_double (&child_iter, 1.5);
  dbus_message_iter_append_double (&child_iter, 2.5);

  /* dict */
  dbus_message_iter_append_dict (&iter, &child_iter);
  dbus_message_iter_append_dict_key (&child_iter, "test");
  dbus_message_iter_append_uint32 (&child_iter, 0xDEADBEEF);

  /* dict (in dict) */
  dbus_message_iter_append_dict_key (&child_iter, "testdict");
  dbus_message_iter_append_dict (&child_iter, &child_iter2);

  dbus_message_iter_append_dict_key (&child_iter2, "dictkey");
  dbus_message_iter_append_string (&child_iter2, "dictvalue");

  /* array of array of int32  (in dict) */
  dbus_message_iter_append_dict_key (&child_iter, "array");
  dbus_message_iter_append_array (&child_iter, &child_iter2, DBUS_TYPE_ARRAY);
  dbus_message_iter_append_array (&child_iter2, &child_iter3, DBUS_TYPE_INT32);
  dbus_message_iter_append_int32 (&child_iter3, 0x12345678);
  dbus_message_iter_append_int32 (&child_iter3, 0x23456781);
  _dbus_warn ("next call expected to fail with wrong array type\n");
  _dbus_assert (!dbus_message_iter_append_array (&child_iter2, &child_iter3, DBUS_TYPE_UINT32));
  dbus_message_iter_append_array (&child_iter2, &child_iter3, DBUS_TYPE_INT32);
  dbus_message_iter_append_int32 (&child_iter3, 0x34567812);
  dbus_message_iter_append_int32 (&child_iter3, 0x45678123);
  dbus_message_iter_append_int32 (&child_iter3, 0x56781234);
  
  dbus_message_iter_append_byte (&iter, 0xF0);

  dbus_message_iter_append_nil (&iter);

  dbus_message_iter_append_custom (&iter, "MyTypeName",
                                   "data", 5);
  
  dbus_message_iter_append_byte (&iter, 0xF0);

  dbus_message_iter_append_array (&iter, &child_iter, DBUS_TYPE_INT32);

  dbus_message_iter_append_byte (&iter, 0xF0);

  dbus_message_iter_append_dict (&iter, &child_iter);

  dbus_message_iter_append_byte (&iter, 0xF0);

  message_iter_test (message);
  
  /* Message loader test */
  _dbus_message_lock (message);
  loader = _dbus_message_loader_new ();

  /* check ref/unref */
  _dbus_message_loader_ref (loader);
  _dbus_message_loader_unref (loader);
  
  /* Write the header data one byte at a time */
  data = _dbus_string_get_const_data (&message->header);
  for (i = 0; i < _dbus_string_get_length (&message->header); i++)
    {
      DBusString *buffer;

      _dbus_message_loader_get_buffer (loader, &buffer);
      _dbus_string_append_byte (buffer, data[i]);
      _dbus_message_loader_return_buffer (loader, buffer, 1);
    }

  /* Write the body data one byte at a time */
  data = _dbus_string_get_const_data (&message->body);
  for (i = 0; i < _dbus_string_get_length (&message->body); i++)
    {
      DBusString *buffer;

      _dbus_message_loader_get_buffer (loader, &buffer);
      _dbus_string_append_byte (buffer, data[i]);
      _dbus_message_loader_return_buffer (loader, buffer, 1);
    }

  copy = dbus_message_copy (message); /* save for tests below */
  dbus_message_unref (message);

  /* Now pop back the message */
  if (!_dbus_message_loader_queue_messages (loader))
    _dbus_assert_not_reached ("no memory to queue messages");
  
  if (_dbus_message_loader_get_is_corrupted (loader))
    _dbus_assert_not_reached ("message loader corrupted");
  
  message = _dbus_message_loader_pop_message (loader);
  if (!message)
    _dbus_assert_not_reached ("received a NULL message");

  if (dbus_message_get_reply_serial (message) != 0x12345678)
    _dbus_assert_not_reached ("reply serial fields differ");
  
  message_iter_test (message);
  
  dbus_message_unref (message);
  _dbus_message_loader_unref (loader);

  message = dbus_message_new_method_return (copy);
  if (message == NULL)
    _dbus_assert_not_reached ("out of memory\n");
  dbus_message_unref (copy);

  if (!dbus_message_append_args (message,
                                 DBUS_TYPE_STRING, "hello",
                                 DBUS_TYPE_INVALID))
    _dbus_assert_not_reached ("no memory");

  if (!dbus_message_has_signature (message, "s"))
    _dbus_assert_not_reached ("method return has wrong signature");

  dbus_error_init (&error);
  if (!dbus_message_get_args (message, &error, DBUS_TYPE_STRING,
                              &t, DBUS_TYPE_INVALID))
    
    {
      _dbus_warn ("Failed to get expected string arg: %s\n", error.message);
      exit (1);
    }
  dbus_free (t);
  
  dbus_message_unref (message);

  /* This ServiceAcquired message used to trigger a bug in
   * setting header fields, adding to regression test.
   */
  message = dbus_message_new_signal (DBUS_PATH_ORG_FREEDESKTOP_DBUS,
                                     DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS,
                                     "ServiceAcquired");
  
  if (message == NULL)
    _dbus_assert_not_reached ("out of memory");

  _dbus_verbose ("Bytes after creation\n");
  _dbus_verbose_bytes_of_string (&message->header, 0,
                                 _dbus_string_get_length (&message->header));
  
  if (!dbus_message_set_destination (message, ":1.0") ||
      !dbus_message_append_args (message,
                                 DBUS_TYPE_STRING, ":1.0",
                                 DBUS_TYPE_INVALID))
    _dbus_assert_not_reached ("out of memory");

  _dbus_verbose ("Bytes after set_destination() and append_args()\n");
  _dbus_verbose_bytes_of_string (&message->header, 0,
                                 _dbus_string_get_length (&message->header));
  
  if (!dbus_message_set_sender (message, "org.freedesktop.DBus"))
    _dbus_assert_not_reached ("out of memory");

  _dbus_verbose ("Bytes after set_sender()\n");
  _dbus_verbose_bytes_of_string (&message->header, 0,
                                 _dbus_string_get_length (&message->header));

  /* When the bug happened the above set_destination() would
   * corrupt the signature
   */
  if (!dbus_message_has_signature (message, "s"))
    {
      _dbus_warn ("Signature should be 's' but is '%s'\n",
                  dbus_message_get_signature (message));
      _dbus_assert_not_reached ("signal has wrong signature");
    }
  
  /* have to set destination again to reproduce the bug */
  if (!dbus_message_set_destination (message, ":1.0"))
    _dbus_assert_not_reached ("out of memory");

  _dbus_verbose ("Bytes after set_destination()\n");
  _dbus_verbose_bytes_of_string (&message->header, 0,
                                 _dbus_string_get_length (&message->header));
  
  /* When the bug happened the above set_destination() would
   * corrupt the signature
   */
  if (!dbus_message_has_signature (message, "s"))
    {
      _dbus_warn ("Signature should be 's' but is '%s'\n",
                  dbus_message_get_signature (message));
      _dbus_assert_not_reached ("signal has wrong signature");
    }

  dbus_error_init (&error);
  if (!dbus_message_get_args (message, &error, DBUS_TYPE_STRING,
                              &t, DBUS_TYPE_INVALID))
    
    {
      _dbus_warn ("Failed to get expected string arg for signal: %s\n", error.message);
      exit (1);
    }
  dbus_free (t);  
  
  dbus_message_unref (message);
  
  /* Now load every message in test_data_dir if we have one */
  if (test_data_dir == NULL)
    return TRUE;

  return dbus_internal_do_not_use_foreach_message_file (test_data_dir,
                                                        (DBusForeachMessageFileFunc)
                                                        dbus_internal_do_not_use_try_message_file,
                                                        NULL);
}

#endif /* DBUS_BUILD_TESTS */
