/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-marshal-basic.c  Marshalling routines for basic (primitive) types
 *
 * Copyright (C) 2002 CodeFactory AB
 * Copyright (C) 2003, 2004, 2005 Red Hat, Inc.
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
#include "dbus-marshal-basic.h"

#include <string.h>

/**
 * @defgroup DBusMarshal marshaling and unmarshaling
 * @ingroup  DBusInternals
 * @brief functions to marshal/unmarshal data from the wire
 *
 * Types and functions related to converting primitive data types from
 * wire format to native machine format, and vice versa.
 *
 * A signature is just a string with multiple types one after the other.
 * for example a type is "i" or "(ii)", a signature is "i(ii)"
 * where i is int and (ii) is struct { int; int; }
 *
 * @{
 */

static void
pack_4_octets (dbus_uint32_t   value,
               int             byte_order,
               unsigned char  *data)
{
  _dbus_assert (_DBUS_ALIGN_ADDRESS (data, 4) == data);

  if ((byte_order) == DBUS_LITTLE_ENDIAN)
    *((dbus_uint32_t*)(data)) = DBUS_UINT32_TO_LE (value);
  else
    *((dbus_uint32_t*)(data)) = DBUS_UINT32_TO_BE (value);
}

static void
pack_8_octets (DBusBasicValue     value,
               int                byte_order,
               unsigned char     *data)
{
  _dbus_assert (_DBUS_ALIGN_ADDRESS (data, 8) == data);

#ifdef DBUS_HAVE_INT64
  if ((byte_order) == DBUS_LITTLE_ENDIAN)
    *((dbus_uint64_t*)(data)) = DBUS_UINT64_TO_LE (value.u64);
  else
    *((dbus_uint64_t*)(data)) = DBUS_UINT64_TO_BE (value.u64);
#else
  *(DBus8ByteStruct*)data = value.u64;
  swap_8_octets ((DBusBasicValue*)data, byte_order);
#endif
}

/**
 * Packs a 32 bit unsigned integer into a data pointer.
 *
 * @param value the value
 * @param byte_order the byte order to use
 * @param data the data pointer
 */
void
_dbus_pack_uint32 (dbus_uint32_t   value,
                   int             byte_order,
                   unsigned char  *data)
{
  pack_4_octets (value, byte_order, data);
}

/**
 * Packs a 32 bit signed integer into a data pointer.
 *
 * @param value the value
 * @param byte_order the byte order to use
 * @param data the data pointer
 */
void
_dbus_pack_int32 (dbus_int32_t   value,
                  int            byte_order,
                  unsigned char *data)
{
  pack_4_octets ((dbus_uint32_t) value, byte_order, data);
}

static dbus_uint32_t
unpack_4_octets (int                  byte_order,
                 const unsigned char *data)
{
  _dbus_assert (_DBUS_ALIGN_ADDRESS (data, 4) == data);

  if (byte_order == DBUS_LITTLE_ENDIAN)
    return DBUS_UINT32_FROM_LE (*(dbus_uint32_t*)data);
  else
    return DBUS_UINT32_FROM_BE (*(dbus_uint32_t*)data);
}

#ifndef DBUS_HAVE_INT64
/* from ORBit */
static void
swap_bytes (unsigned char *data,
            unsigned int   len)
{
  unsigned char *p1 = data;
  unsigned char *p2 = data + len - 1;

  while (p1 < p2)
    {
      unsigned char tmp = *p1;
      *p1 = *p2;
      *p2 = tmp;

      --p2;
      ++p1;
    }
}
#endif /* !DBUS_HAVE_INT64 */

static void
swap_8_octets (DBusBasicValue    *value,
               int                byte_order)
{
  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    {
#ifdef DBUS_HAVE_INT64
      value->u64 = DBUS_UINT64_SWAP_LE_BE (value->u64);
#else
      swap_bytes ((unsigned char *)value, 8);
#endif
    }
}

static DBusBasicValue
unpack_8_octets (int                  byte_order,
                 const unsigned char *data)
{
  DBusBasicValue r;

  _dbus_assert (_DBUS_ALIGN_ADDRESS (data, 8) == data);
  _dbus_assert (sizeof (r) == 8);

#ifdef DBUS_HAVE_INT64
  if (byte_order == DBUS_LITTLE_ENDIAN)
    r.u64 = DBUS_UINT64_FROM_LE (*(dbus_uint64_t*)data);
  else
    r.u64 = DBUS_UINT64_FROM_BE (*(dbus_uint64_t*)data);
#else
  r.u64 = *(DBus8ByteStruct*)data;
  swap_8_octets (&r, byte_order);
#endif

  return r;
}

/**
 * Unpacks a 32 bit unsigned integer from a data pointer
 *
 * @param byte_order The byte order to use
 * @param data the data pointer
 * @returns the integer
 */
dbus_uint32_t
_dbus_unpack_uint32 (int                  byte_order,
                     const unsigned char *data)
{
  return unpack_4_octets (byte_order, data);
}

/**
 * Unpacks a 32 bit signed integer from a data pointer
 *
 * @param byte_order The byte order to use
 * @param data the data pointer
 * @returns the integer
 */
dbus_int32_t
_dbus_unpack_int32 (int                  byte_order,
                    const unsigned char *data)
{
  return (dbus_int32_t) unpack_4_octets (byte_order, data);
}

static void
set_4_octets (DBusString          *str,
              int                  offset,
              dbus_uint32_t        value,
              int                  byte_order)
{
  char *data;

  _dbus_assert (byte_order == DBUS_LITTLE_ENDIAN ||
                byte_order == DBUS_BIG_ENDIAN);

  data = _dbus_string_get_data_len (str, offset, 4);

  _dbus_pack_uint32 (value, byte_order, data);
}

static void
set_8_octets (DBusString          *str,
              int                  offset,
              DBusBasicValue       value,
              int                  byte_order)
{
  char *data;

  _dbus_assert (byte_order == DBUS_LITTLE_ENDIAN ||
                byte_order == DBUS_BIG_ENDIAN);

  data = _dbus_string_get_data_len (str, offset, 8);

  pack_8_octets (value, byte_order, data);
}

/**
 * Sets the 4 bytes at the given offset to a marshaled unsigned
 * integer, replacing anything found there previously.
 *
 * @param str the string to write the marshalled int to
 * @param pos the byte offset where int should be written
 * @param value the value
 * @param byte_order the byte order to use
 *
 */
void
_dbus_marshal_set_uint32 (DBusString          *str,
                          int                  pos,
                          dbus_uint32_t        value,
                          int                  byte_order)
{
  set_4_octets (str, pos, value, byte_order);
}

/**
 * Sets the existing marshaled string at the given offset with
 * a new marshaled string. The given offset must point to
 * an existing string or the wrong length will be deleted
 * and replaced with the new string.
 *
 * Note: no attempt is made by this function to re-align
 * any data which has been already marshalled after this
 * string. Use with caution.
 *
 * @param str the string to write the marshalled string to
 * @param pos the position of the marshaled string length
 * @param value the value
 * @param byte_order the byte order to use
 * @param old_end_pos place to store byte after the nul byte of the old value
 * @param new_end_pos place to store byte after the nul byte of the new value
 * @returns #TRUE on success, #FALSE if no memory
 *
 */
static dbus_bool_t
set_string (DBusString          *str,
            int                  pos,
            const char          *value,
            int                  byte_order,
            int                 *old_end_pos,
            int                 *new_end_pos)
{
  int old_len, new_len;
  DBusString dstr;

  _dbus_string_init_const (&dstr, value);

  old_len = _dbus_marshal_read_uint32 (str, pos, byte_order, NULL);

  new_len = _dbus_string_get_length (&dstr);

  if (!_dbus_string_replace_len (&dstr, 0, new_len,
                                 str, pos + 4, old_len))
    return FALSE;

  _dbus_marshal_set_uint32 (str, pos, new_len, byte_order);

  if (old_end_pos)
    *old_end_pos = pos + 4 + old_len + 1;
  if (new_end_pos)
    *new_end_pos = pos + 4 + new_len + 1;

  return TRUE;
}

/**
 * Sets the existing marshaled signature at the given offset to a new
 * marshaled signature. Same basic ideas as set_string().
 *
 * @param str the string to write the marshalled signature to
 * @param pos the position of the marshaled signature length
 * @param value the value
 * @param byte_order the byte order to use
 * @param old_end_pos place to store byte after the nul byte of the old value
 * @param new_end_pos place to store byte after the nul byte of the new value
 * @returns #TRUE on success, #FALSE if no memory
 *
 */
static dbus_bool_t
set_signature (DBusString          *str,
               int                  pos,
               const char          *value,
               int                  byte_order,
               int                 *old_end_pos,
               int                 *new_end_pos)
{
  int old_len, new_len;
  DBusString dstr;

  _dbus_string_init_const (&dstr, value);

  old_len = _dbus_string_get_byte (str, pos);
  new_len = _dbus_string_get_length (&dstr);

  if (!_dbus_string_replace_len (&dstr, 0, new_len,
                                 str, pos + 1, old_len))
    return FALSE;

  _dbus_string_set_byte (str, pos, new_len);

  if (old_end_pos)
    *old_end_pos = pos + 1 + old_len + 1;
  if (new_end_pos)
    *new_end_pos = pos + 1 + new_len + 1;

  return TRUE;
}

/**
 * Sets an existing basic type value to a new value.
 * Arguments work the same way as _dbus_marshal_basic_type().
 *
 * @param str the string
 * @param pos location of the current value
 * @param type the type of the current and new values
 * @param value the address of the new value
 * @param byte_order byte order for marshaling
 * @param old_end_pos location to store end position of the old value, or #NULL
 * @param new_end_pos location to store end position of the new value, or #NULL
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_marshal_set_basic (DBusString       *str,
                         int               pos,
                         int               type,
                         const void       *value,
                         int               byte_order,
                         int              *old_end_pos,
                         int              *new_end_pos)
{
  const DBusBasicValue *vp;

  vp = value;

  switch (type)
    {
    case DBUS_TYPE_BYTE:
    case DBUS_TYPE_BOOLEAN:
      _dbus_string_set_byte (str, pos, vp->byt);
      if (old_end_pos)
        *old_end_pos = pos + 1;
      if (new_end_pos)
        *new_end_pos = pos + 1;
      return TRUE;
      break;
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT32:
      pos = _DBUS_ALIGN_VALUE (pos, 4);
      set_4_octets (str, pos, vp->u32, byte_order);
      if (old_end_pos)
        *old_end_pos = pos + 4;
      if (new_end_pos)
        *new_end_pos = pos + 4;
      return TRUE;
      break;
    case DBUS_TYPE_INT64:
    case DBUS_TYPE_UINT64:
    case DBUS_TYPE_DOUBLE:
      pos = _DBUS_ALIGN_VALUE (pos, 8);
      set_8_octets (str, pos, *vp, byte_order);
      if (old_end_pos)
        *old_end_pos = pos + 8;
      if (new_end_pos)
        *new_end_pos = pos + 8;
      return TRUE;
      break;
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
      return set_string (str, pos, vp->str, byte_order,
                         old_end_pos, new_end_pos);
      break;
    case DBUS_TYPE_SIGNATURE:
      return set_signature (str, pos, vp->str, byte_order,
                            old_end_pos, new_end_pos);
      break;
    default:
      _dbus_assert_not_reached ("not a basic type");
      return FALSE;
      break;
    }
}

static dbus_uint32_t
read_4_octets (const DBusString *str,
               int               pos,
               int               byte_order,
               int              *new_pos)
{
  pos = _DBUS_ALIGN_VALUE (pos, 4);

  if (new_pos)
    *new_pos = pos + 4;

  return unpack_4_octets (byte_order,
                          _dbus_string_get_const_data (str) + pos);
}

/**
 * Convenience function to demarshal a 32 bit unsigned integer.
 *
 * @param str the string containing the data
 * @param byte_order the byte order
 * @param pos the position in the string
 * @param new_pos the new position of the string
 * @returns the demarshaled integer.
 */
dbus_uint32_t
_dbus_marshal_read_uint32  (const DBusString *str,
                            int               pos,
                            int               byte_order,
                            int              *new_pos)
{
  return read_4_octets (str, pos, byte_order, new_pos);
}

/**
 * Demarshals a basic-typed value. The "value" pointer is always
 * the address of a variable of the basic type. So e.g.
 * if the basic type is "double" then the pointer is
 * a double*, and if it's "char*" then the pointer is
 * a "char**".
 *
 * A value of type #DBusBasicValue is guaranteed to be large enough to
 * hold any of the types that may be returned, which is handy if you
 * are trying to do things generically. For example you can pass
 * a DBusBasicValue* in to this function, and then pass the same
 * DBusBasicValue* in to _dbus_marshal_basic_type() in order to
 * move a value from one place to another.
 *
 * @param str the string containing the data
 * @param pos position in the string
 * @param type type of value to demarshal
 * @param value pointer to return value data
 * @param byte_order the byte order
 * @param new_pos pointer to update with new position, or #NULL
 **/
void
_dbus_marshal_read_basic (const DBusString      *str,
                          int                    pos,
                          int                    type,
                          void                  *value,
                          int                    byte_order,
                          int                   *new_pos)
{
  const char *str_data;
  DBusBasicValue *vp;

  _dbus_assert (_dbus_type_is_basic (type));

  str_data = _dbus_string_get_const_data (str);
  vp = value;

  switch (type)
    {
    case DBUS_TYPE_BYTE:
    case DBUS_TYPE_BOOLEAN:
      vp->byt = _dbus_string_get_byte (str, pos);
      (pos)++;
      break;
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT32:
      pos = _DBUS_ALIGN_VALUE (pos, 4);
      vp->u32 = *(dbus_uint32_t *)(str_data + pos);
      if (byte_order != DBUS_COMPILER_BYTE_ORDER)
	vp->u32 = DBUS_UINT32_SWAP_LE_BE (vp->u32);
      pos += 4;
      break;
    case DBUS_TYPE_INT64:
    case DBUS_TYPE_UINT64:
    case DBUS_TYPE_DOUBLE:
      pos = _DBUS_ALIGN_VALUE (pos, 8);
#ifdef DBUS_HAVE_INT64
      if (byte_order != DBUS_COMPILER_BYTE_ORDER)
        vp->u64 = DBUS_UINT64_SWAP_LE_BE (*(dbus_uint64_t*)(str_data + pos));
      else
        vp->u64 = *(dbus_uint64_t*)(str_data + pos);
#else
      vp->u64 = *(DBus8ByteStruct*) (str_data + pos);
      swap_8_octets (vp, byte_order);
#endif
      pos += 8;
      break;
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
      {
        int len;

        len = _dbus_marshal_read_uint32 (str, pos, byte_order, &pos);

        vp->str = (char*) str_data + pos;

        pos += len + 1; /* length plus nul */
      }
      break;
    case DBUS_TYPE_SIGNATURE:
      {
        int len;

        len = _dbus_string_get_byte (str, pos);
        pos += 1;

        vp->str = (char*) str_data + pos;

        pos += len + 1; /* length plus nul */
      }
      break;
    default:
      _dbus_warn ("type %s not a basic type\n",
                  _dbus_type_to_string (type));
      _dbus_assert_not_reached ("not a basic type");
      break;
    }

  if (new_pos)
    *new_pos = pos;
}

static dbus_bool_t
marshal_4_octets (DBusString   *str,
                  int           insert_at,
                  dbus_uint32_t value,
                  int           byte_order,
                  int          *pos_after)
{
  dbus_bool_t retval;
  int orig_len;

  _dbus_assert (sizeof (value) == 4);

  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    value = DBUS_UINT32_SWAP_LE_BE (value);

  orig_len = _dbus_string_get_length (str);

  retval = _dbus_string_insert_4_aligned (str, insert_at,
                                          (const unsigned char *)&value);

  if (pos_after)
    {
      *pos_after = insert_at + (_dbus_string_get_length (str) - orig_len);
      _dbus_assert (*pos_after <= _dbus_string_get_length (str));
    }

  return retval;
}

static dbus_bool_t
marshal_8_octets (DBusString    *str,
                  int            insert_at,
                  DBusBasicValue value,
                  int            byte_order,
                  int           *pos_after)
{
  dbus_bool_t retval;
  int orig_len;

  _dbus_assert (sizeof (value) == 8);

  swap_8_octets (&value, byte_order);

  orig_len = _dbus_string_get_length (str);

  retval = _dbus_string_insert_8_aligned (str, insert_at,
                                          (const unsigned char *)&value);

  if (pos_after)
    *pos_after = insert_at + _dbus_string_get_length (str) - orig_len;

  return retval;
}

enum
  {
    MARSHAL_AS_STRING,
    MARSHAL_AS_SIGNATURE,
    MARSHAL_AS_BYTE_ARRAY
  };

static dbus_bool_t
marshal_len_followed_by_bytes (int                  marshal_as,
                               DBusString          *str,
                               int                  insert_at,
                               const unsigned char *value,
                               int                  data_len, /* doesn't include nul if any */
                               int                  byte_order,
                               int                 *pos_after)
{
  int pos;
  DBusString value_str;
  int value_len;

  _dbus_assert (byte_order == DBUS_LITTLE_ENDIAN || byte_order == DBUS_BIG_ENDIAN);
  if (insert_at > _dbus_string_get_length (str))
    _dbus_warn ("insert_at = %d string len = %d data_len = %d\n",
                insert_at, _dbus_string_get_length (str), data_len);

  if (marshal_as == MARSHAL_AS_BYTE_ARRAY)
    value_len = data_len;
  else
    value_len = data_len + 1; /* value has a nul */

  /* FIXME this is probably broken for byte arrays because
   * DBusString wants strings to be nul-terminated?
   * Maybe I planned on this when writing init_const_len though
   */
  _dbus_string_init_const_len (&value_str, value, value_len);

  pos = insert_at;

  if (marshal_as == MARSHAL_AS_SIGNATURE)
    {
      if (!_dbus_string_insert_byte (str, pos, data_len))
        goto oom;

      pos += 1;
    }
  else
    {
      if (!marshal_4_octets (str, pos, data_len,
                             byte_order, &pos))
        goto oom;
    }

  if (!_dbus_string_copy_len (&value_str, 0, value_len,
                              str, pos))
    goto oom;

#if 1
  /* too expensive */
  _dbus_assert (_dbus_string_equal_substring (&value_str, 0, value_len,
                                              str, pos));
  _dbus_verbose_bytes_of_string (str, pos, value_len);
#endif

  pos += value_len;

  if (pos_after)
    *pos_after = pos;

  return TRUE;

 oom:
  /* Delete what we've inserted */
  _dbus_string_delete (str, insert_at, pos - insert_at);

  return FALSE;
}

static dbus_bool_t
marshal_string (DBusString    *str,
                int            insert_at,
                const char    *value,
                int            byte_order,
                int           *pos_after)
{
  return marshal_len_followed_by_bytes (MARSHAL_AS_STRING,
                                        str, insert_at, value,
                                        strlen (value),
                                        byte_order, pos_after);
}

static dbus_bool_t
marshal_signature (DBusString    *str,
                   int            insert_at,
                   const char    *value,
                   int           *pos_after)
{
  return marshal_len_followed_by_bytes (MARSHAL_AS_SIGNATURE,
                                        str, insert_at, value,
                                        strlen (value),
                                        DBUS_COMPILER_BYTE_ORDER, /* irrelevant */
                                        pos_after);
}

/**
 * Marshals a basic-typed value. The "value" pointer is always the
 * address of a variable containing the basic type value.
 * So for example for int32 it will be dbus_int32_t*, and
 * for string it will be const char**. This is for symmetry
 * with _dbus_marshal_read_basic() and to have a simple
 * consistent rule.
 *
 * @param str string to marshal to
 * @param insert_at where to insert the value
 * @param type type of value
 * @param value pointer to a variable containing the value
 * @param byte_order byte order
 * @param pos_after #NULL or the position after the type
 * @returns #TRUE on success
 **/
dbus_bool_t
_dbus_marshal_write_basic (DBusString *str,
                           int         insert_at,
                           int         type,
                           const void *value,
                           int         byte_order,
                           int        *pos_after)
{
  const DBusBasicValue *vp;

  _dbus_assert (_dbus_type_is_basic (type));

  vp = value;

  switch (type)
    {
    case DBUS_TYPE_BYTE:
    case DBUS_TYPE_BOOLEAN:
      if (!_dbus_string_insert_byte (str, insert_at, vp->byt))
        return FALSE;
      if (pos_after)
        *pos_after = insert_at + 1;
      return TRUE;
      break;
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT32:
      return marshal_4_octets (str, insert_at, vp->u32,
                               byte_order, pos_after);
      break;
    case DBUS_TYPE_INT64:
    case DBUS_TYPE_UINT64:
    case DBUS_TYPE_DOUBLE:
      return marshal_8_octets (str, insert_at, *vp, byte_order, pos_after);
      break;

    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
      return marshal_string (str, insert_at, vp->str, byte_order, pos_after);
      break;
    case DBUS_TYPE_SIGNATURE:
      return marshal_signature (str, insert_at, vp->str, pos_after);
      break;
    default:
      _dbus_assert_not_reached ("not a basic type");
      return FALSE;
      break;
    }
}

static dbus_bool_t
marshal_1_octets_array (DBusString          *str,
                        int                  insert_at,
                        const unsigned char *value,
                        int                  len,
                        int                  byte_order,
                        int                 *pos_after)
{
  return marshal_len_followed_by_bytes (MARSHAL_AS_BYTE_ARRAY,
                                        str, insert_at, value, len,
                                        byte_order, pos_after);
}

static dbus_bool_t
marshal_4_octets_array (DBusString          *str,
                        int                  insert_at,
                        const dbus_uint32_t *value,
                        int                  len,
                        int                  byte_order)
{
  int old_string_len;
  int array_start;

  _dbus_assert_not_reached ("FIXME insert_at");

  old_string_len = _dbus_string_get_length (str);

  if (!marshal_4_octets (str, insert_at, len*4, byte_order, NULL))
    goto error;

  array_start = _dbus_string_get_length (str);

  if (!_dbus_string_append_len (str, (const unsigned char*) value,
                                len * 4))
    goto error;

  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    {
      const unsigned char *d;
      const unsigned char *end;

      d = _dbus_string_get_data (str) + array_start;
      end = d + len * 4;
      while (d != end)
        {
          *((dbus_uint32_t*)d) = DBUS_UINT32_SWAP_LE_BE (*((dbus_uint32_t*)d));
          d += 4;
        }
    }

  return TRUE;

 error:
  /* Restore previous length */
  _dbus_string_set_length (str, old_string_len);

  return FALSE;
}

static dbus_bool_t
marshal_8_octets_array (DBusString           *str,
                        int                   insert_at,
                        const DBusBasicValue *value,
                        int                   len,
                        int                   byte_order)
{
  int old_string_len;
  int array_start;

  _dbus_assert_not_reached ("FIXME insert_at");

  old_string_len = _dbus_string_get_length (str);

  /*  The array length is the length in bytes of the array,
   * *excluding* alignment padding.
   */
  if (!marshal_4_octets (str, insert_at, len*8, byte_order, NULL))
    goto error;

  array_start = _dbus_string_get_length (str);

  /* Note that we do alignment padding unconditionally
   * even if the array is empty; this means that
   * padding + len is always equal to the number of bytes
   * in the array.
   */

  if (!_dbus_string_align_length (str, 8))
    goto error;

  if (!_dbus_string_append_len (str, (const unsigned char*) value,
                                len * 8))
    goto error;

  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    {
      const unsigned char *d;
      const unsigned char *end;

      d = _dbus_string_get_data (str) + array_start;
      end = d + len * 8;
      while (d != end)
        {
#ifdef DBUS_HAVE_INT64
          *((dbus_uint64_t*)d) = DBUS_UINT64_SWAP_LE_BE (*((dbus_uint64_t*)d));
#else
          swap_8_bytes ((DBusBasicValue*) d);
#endif
          d += 8;
        }
    }

  return TRUE;

 error:
  /* Restore previous length */
  _dbus_string_set_length (str, old_string_len);

  return FALSE;
}

/**
 * Marshals a basic type array
 *
 * @param str string to marshal to
 * @param insert_at where to insert the value
 * @param element_type type of array elements
 * @param value pointer to value
 * @param len length of value data in elements
 * @param byte_order byte order
 * @param pos_after #NULL or the position after the type
 * @returns #TRUE on success
 **/
dbus_bool_t
_dbus_marshal_write_basic_array (DBusString *str,
                                 int         insert_at,
                                 int         element_type,
                                 const void *value,
                                 int         len,
                                 int         byte_order,
                                 int        *pos_after)
{
  /* FIXME use the insert_at arg and fill in pos_after */

  switch (element_type)
    {
    case DBUS_TYPE_BOOLEAN:
      /* FIXME: we canonicalize to 0 or 1 for the single boolean case
       * should we here too ? */
    case DBUS_TYPE_BYTE:
      return marshal_1_octets_array (str, insert_at, value, len, byte_order, pos_after);
      break;
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT32:
      return marshal_4_octets_array (str, insert_at, value, len, byte_order);
      break;
    case DBUS_TYPE_INT64:
    case DBUS_TYPE_UINT64:
    case DBUS_TYPE_DOUBLE:
      return marshal_8_octets_array (str, insert_at, value, len, byte_order);
      break;

    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
      _dbus_assert_not_reached ("handle string arrays");
      break;

    case DBUS_TYPE_SIGNATURE:
      _dbus_assert_not_reached ("handle signature");
      break;

    default:
      _dbus_assert_not_reached ("non basic type in array");
      break;
    }

  return FALSE;
}


/**
 * Skips over a basic-typed value, reporting the following position.
 *
 * @param str the string containing the data
 * @param type type of value to read
 * @param byte_order the byte order
 * @param pos pointer to position in the string,
 *            updated on return to new position
 **/
void
_dbus_marshal_skip_basic (const DBusString      *str,
                          int                    type,
                          int                    byte_order,
                          int                   *pos)
{
  switch (type)
    {
    case DBUS_TYPE_BYTE:
    case DBUS_TYPE_BOOLEAN:
      (*pos)++;
      break;
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT32:
      *pos = _DBUS_ALIGN_VALUE (*pos, 4);
      *pos += 4;
      break;
    case DBUS_TYPE_INT64:
    case DBUS_TYPE_UINT64:
    case DBUS_TYPE_DOUBLE:
      *pos = _DBUS_ALIGN_VALUE (*pos, 8);
      *pos += 8;
      break;
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
      {
        int len;

        len = _dbus_marshal_read_uint32 (str, *pos, byte_order, pos);

        *pos += len + 1; /* length plus nul */
      }
      break;
    case DBUS_TYPE_SIGNATURE:
      {
        int len;

        len = _dbus_string_get_byte (str, *pos);

        *pos += len + 2; /* length byte plus length plus nul */
      }
      break;
    default:
      _dbus_warn ("type %s not a basic type\n",
                  _dbus_type_to_string (type));
      _dbus_assert_not_reached ("not a basic type");
      break;
    }
}

/**
 * Skips an array, returning the next position.
 *
 * @param str the string containing the data
 * @param element_type the type of array elements
 * @param byte_order the byte order
 * @param pos pointer to position in the string,
 *            updated on return to new position
 */
void
_dbus_marshal_skip_array (const DBusString  *str,
                          int                element_type,
                          int                byte_order,
                          int               *pos)
{
  dbus_uint32_t array_len;
  int i;
  int alignment;

  i = _DBUS_ALIGN_VALUE (*pos, 4);

  array_len = _dbus_marshal_read_uint32 (str, i, byte_order, &i);

  alignment = _dbus_type_get_alignment (element_type);

  i = _DBUS_ALIGN_VALUE (i, alignment);

  *pos = i + array_len;
}

/**
 * Gets the alignment requirement for the given type;
 * will be 1, 4, or 8.
 *
 * @param typecode the type
 * @returns alignment of 1, 4, or 8
 */
int
_dbus_type_get_alignment (int typecode)
{
  switch (typecode)
    {
    case DBUS_TYPE_BYTE:
    case DBUS_TYPE_BOOLEAN:
    case DBUS_TYPE_VARIANT:
    case DBUS_TYPE_SIGNATURE:
      return 1;
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT32:
      /* this stuff is 4 since it starts with a length */
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_ARRAY:
      return 4;
    case DBUS_TYPE_INT64:
    case DBUS_TYPE_UINT64:
    case DBUS_TYPE_DOUBLE:
      /* struct is 8 since it could contain an 8-aligned item
       * and it's simpler to just always align structs to 8;
       * we want the amount of padding in a struct of a given
       * type to be predictable, not location-dependent.
       */
    case DBUS_TYPE_STRUCT:
      return 8;

    default:
      _dbus_assert_not_reached ("unknown typecode in _dbus_type_get_alignment()");
      return 0;
    }
}


/**
 * Return #TRUE if the typecode is a valid typecode.
 * #DBUS_TYPE_INVALID surprisingly enough is not considered valid, and
 * random unknown bytes aren't either. This function is safe with
 * untrusted data.
 *
 * @returns #TRUE if valid
 */
dbus_bool_t
_dbus_type_is_valid (int typecode)
{
  switch (typecode)
    {
    case DBUS_TYPE_BYTE:
    case DBUS_TYPE_BOOLEAN:
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT32:
    case DBUS_TYPE_INT64:
    case DBUS_TYPE_UINT64:
    case DBUS_TYPE_DOUBLE:
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE:
    case DBUS_TYPE_ARRAY:
    case DBUS_TYPE_STRUCT:
    case DBUS_TYPE_VARIANT:
      return TRUE;

    default:
      return FALSE;
    }
}

#define TYPE_IS_CONTAINER(typecode)             \
    ((typecode) == DBUS_TYPE_STRUCT ||          \
     (typecode) == DBUS_TYPE_VARIANT ||         \
     (typecode) == DBUS_TYPE_ARRAY)

/**
 * A "container type" can contain basic types, or nested
 * container types. #DBUS_TYPE_INVALID is not a container type.
 * This function will crash if passed a typecode that isn't
 * in dbus-protocol.h
 *
 * @returns #TRUE if type is a container
 */
dbus_bool_t
_dbus_type_is_container (int typecode)
{
  /* only reasonable (non-line-noise) typecodes are allowed */
  _dbus_assert (_dbus_type_is_valid (typecode) || typecode == DBUS_TYPE_INVALID);
  return TYPE_IS_CONTAINER (typecode);
}

/**
 * A "basic type" is a somewhat arbitrary concept, but the intent
 * is to include those types that are fully-specified by a single
 * typecode, with no additional type information or nested
 * values. So all numbers and strings are basic types and
 * structs, arrays, and variants are not basic types.
 * #DBUS_TYPE_INVALID is not a basic type.
 *
 * This function is defined to return #TRUE for exactly those
 * types that can be written with _dbus_marshal_basic_type()
 * and read with _dbus_marshal_read_basic().
 *
 * This function will crash if passed a typecode that isn't
 * in dbus-protocol.h
 *
 * @returns #TRUE if type is basic
 */
dbus_bool_t
_dbus_type_is_basic (int typecode)
{
  /* only reasonable (non-line-noise) typecodes are allowed */
  _dbus_assert (_dbus_type_is_valid (typecode) || typecode == DBUS_TYPE_INVALID);

  /* everything that isn't invalid or a container */
  return !(typecode == DBUS_TYPE_INVALID || TYPE_IS_CONTAINER (typecode));
}

/**
 * Tells you whether values of this type can change length if you set
 * them to some other value. For this purpose, you assume that the
 * first byte of the old and new value would be in the same location,
 * so alignment padding is not a factor.
 *
 * @returns #TRUE if the type can occupy different lengths
 */
dbus_bool_t
_dbus_type_length_varies (int typecode)
{
  switch (typecode)
    {
    case DBUS_TYPE_BYTE:
    case DBUS_TYPE_BOOLEAN:
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT32:
    case DBUS_TYPE_INT64:
    case DBUS_TYPE_UINT64:
    case DBUS_TYPE_DOUBLE:
      return FALSE;
    default:
      return TRUE;
    }
}

/**
 * If in verbose mode, print a block of binary data.
 *
 * @todo right now it prints even if not in verbose mode
 *
 * @param data the data
 * @param len the length of the data
 * @param offset where to start counting for byte indexes
 */
void
_dbus_verbose_bytes (const unsigned char *data,
                     int                  len,
                     int                  offset)
{
  int i;
  const unsigned char *aligned;

  _dbus_assert (len >= 0);

  /* Print blanks on first row if appropriate */
  aligned = _DBUS_ALIGN_ADDRESS (data, 4);
  if (aligned > data)
    aligned -= 4;
  _dbus_assert (aligned <= data);

  if (aligned != data)
    {
      _dbus_verbose ("%4d\t%p: ", - (data - aligned), aligned);
      while (aligned != data)
        {
          _dbus_verbose ("    ");
          ++aligned;
        }
    }

  /* now print the bytes */
  i = 0;
  while (i < len)
    {
      if (_DBUS_ALIGN_ADDRESS (&data[i], 4) == &data[i])
        {
          _dbus_verbose ("%4d\t%p: ",
                         offset + i, &data[i]);
        }

      if (data[i] >= 32 &&
          data[i] <= 126)
        _dbus_verbose (" '%c' ", data[i]);
      else
        _dbus_verbose ("0x%s%x ",
                       data[i] <= 0xf ? "0" : "", data[i]);

      ++i;

      if (_DBUS_ALIGN_ADDRESS (&data[i], 4) == &data[i])
        {
          if (i > 3)
            _dbus_verbose ("BE: %d LE: %d",
                           _dbus_unpack_uint32 (DBUS_BIG_ENDIAN, &data[i-4]),
                           _dbus_unpack_uint32 (DBUS_LITTLE_ENDIAN, &data[i-4]));

          if (i > 7 &&
              _DBUS_ALIGN_ADDRESS (&data[i], 8) == &data[i])
            {
              _dbus_verbose (" dbl: %g",
                             *(double*)&data[i-8]);
            }

          _dbus_verbose ("\n");
        }
    }

  _dbus_verbose ("\n");
}

/**
 * Dump the given part of the string to verbose log.
 *
 * @param str the string
 * @param start the start of range to dump
 * @param len length of range
 */
void
_dbus_verbose_bytes_of_string (const DBusString    *str,
                               int                  start,
                               int                  len)
{
  const char *d;
  int real_len;

  real_len = _dbus_string_get_length (str);

  _dbus_assert (start >= 0);

  if (start > real_len)
    {
      _dbus_verbose ("  [%d,%d) is not inside string of length %d\n",
                     start, len, real_len);
      return;
    }

  if ((start + len) > real_len)
    {
      _dbus_verbose ("  [%d,%d) extends outside string of length %d\n",
                     start, len, real_len);
      len = real_len - start;
    }

  d = _dbus_string_get_const_data_len (str, start, len);

  _dbus_verbose_bytes (d, len, start);
}

/** @} */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>

#define MARSHAL_BASIC(typename, byte_order, literal)                    \
  do {                                                                  \
     v_##typename = literal;                                            \
     if (!_dbus_marshal_write_basic (&str, pos, DBUS_TYPE_##typename,   \
                                    &v_##typename,                      \
                                    byte_order, NULL))                  \
       _dbus_assert_not_reached ("no memory");                          \
   } while (0)

#define DEMARSHAL_BASIC(typename, byte_order)                                   \
  do {                                                                          \
    _dbus_marshal_read_basic (&str, pos, DBUS_TYPE_##typename, &v_##typename,   \
                                byte_order, &pos);                              \
  } while (0)

#define DEMARSHAL_BASIC_AND_CHECK(typename, byte_order, literal)                        \
  do {                                                                                  \
    DEMARSHAL_BASIC (typename, byte_order);                                             \
    if (literal != v_##typename)                                                        \
      {                                                                                 \
        _dbus_verbose_bytes_of_string (&str, dump_pos,                                  \
                                     _dbus_string_get_length (&str) - dump_pos);        \
        _dbus_assert_not_reached ("demarshaled wrong value");                           \
      }                                                                                 \
  } while (0)

#define MARSHAL_TEST(typename, byte_order, literal)             \
  do {                                                          \
    MARSHAL_BASIC (typename, byte_order, literal);              \
    dump_pos = pos;                                             \
    DEMARSHAL_BASIC_AND_CHECK (typename, byte_order, literal);  \
  } while (0)

#define MARSHAL_TEST_STRCMP(typename, byte_order, literal)                              \
  do {                                                                                  \
    MARSHAL_BASIC (typename, byte_order, literal);                                      \
    dump_pos = pos;                                                                     \
    DEMARSHAL_BASIC (typename, byte_order);                                             \
    if (strcmp (literal, v_##typename) != 0)                                            \
      {                                                                                 \
        _dbus_verbose_bytes_of_string (&str, dump_pos,                                  \
                                       _dbus_string_get_length (&str) - dump_pos);      \
        _dbus_warn ("literal '%s'\nvalue  '%s'\n", literal, v_##typename);              \
        _dbus_assert_not_reached ("demarshaled wrong value");                           \
      }                                                                                 \
  } while (0)

dbus_bool_t
_dbus_marshal_test (void)
{
  DBusString str;
  int pos, dump_pos;
#if 0
  dbus_int32_t array1[3] = { 0x123, 0x456, 0x789 }, *array2;
#ifdef DBUS_HAVE_INT64
  dbus_int64_t array3[3] = { DBUS_INT64_CONSTANT (0x123ffffffff),
                             DBUS_INT64_CONSTANT (0x456ffffffff),
                             DBUS_INT64_CONSTANT (0x789ffffffff) }, *array4;
#endif
#endif
  DBusString t;
  double v_DOUBLE;
  double t_DOUBLE;
  dbus_int32_t v_INT32;
  dbus_uint32_t v_UINT32;
  dbus_int64_t v_INT64;
  dbus_uint64_t v_UINT64;
  unsigned char v_BYTE;
  unsigned char v_BOOLEAN;
  const char *v_STRING;
  const char *v_SIGNATURE;
  const char *v_OBJECT_PATH;
  int byte_order;

  if (!_dbus_string_init (&str))
    _dbus_assert_not_reached ("failed to init string");

  pos = 0;

  /* Marshal doubles */
  MARSHAL_BASIC (DOUBLE, DBUS_BIG_ENDIAN, 3.14);
  DEMARSHAL_BASIC (DOUBLE, DBUS_BIG_ENDIAN);
  t_DOUBLE = 3.14;
  if (!_DBUS_DOUBLES_BITWISE_EQUAL (t_DOUBLE, v_DOUBLE))
    _dbus_assert_not_reached ("got wrong double value");

  MARSHAL_BASIC (DOUBLE, DBUS_LITTLE_ENDIAN, 3.14);
  DEMARSHAL_BASIC (DOUBLE, DBUS_LITTLE_ENDIAN);
  t_DOUBLE = 3.14;
  if (!_DBUS_DOUBLES_BITWISE_EQUAL (t_DOUBLE, v_DOUBLE))
    _dbus_assert_not_reached ("got wrong double value");

  /* Marshal signed integers */
  MARSHAL_TEST (INT32, DBUS_BIG_ENDIAN, -12345678);
  MARSHAL_TEST (INT32, DBUS_LITTLE_ENDIAN, -12345678);

  /* Marshal unsigned integers */
  MARSHAL_TEST (UINT32, DBUS_BIG_ENDIAN, 0x12345678);
  MARSHAL_TEST (UINT32, DBUS_LITTLE_ENDIAN, 0x12345678);

#ifdef DBUS_HAVE_INT64
  /* Marshal signed integers */
  MARSHAL_TEST (INT64, DBUS_BIG_ENDIAN, DBUS_INT64_CONSTANT (-0x123456789abc7));
  MARSHAL_TEST (INT64, DBUS_LITTLE_ENDIAN, DBUS_INT64_CONSTANT (-0x123456789abc7));

  /* Marshal unsigned integers */
  MARSHAL_TEST (UINT64, DBUS_BIG_ENDIAN, DBUS_UINT64_CONSTANT (0x123456789abc7));
  MARSHAL_TEST (UINT64, DBUS_LITTLE_ENDIAN, DBUS_UINT64_CONSTANT (0x123456789abc7));
#endif /* DBUS_HAVE_INT64 */

  /* Marshal byte */
  MARSHAL_TEST (BYTE, DBUS_BIG_ENDIAN, 5);
  MARSHAL_TEST (BYTE, DBUS_LITTLE_ENDIAN, 5);

  /* Marshal all possible bools! */
  MARSHAL_TEST (BOOLEAN, DBUS_BIG_ENDIAN, FALSE);
  MARSHAL_TEST (BOOLEAN, DBUS_LITTLE_ENDIAN, FALSE);
  MARSHAL_TEST (BOOLEAN, DBUS_BIG_ENDIAN, TRUE);
  MARSHAL_TEST (BOOLEAN, DBUS_LITTLE_ENDIAN, TRUE);

  /* Marshal strings */
  MARSHAL_TEST_STRCMP (STRING, DBUS_BIG_ENDIAN, "");
  MARSHAL_TEST_STRCMP (STRING, DBUS_LITTLE_ENDIAN, "");
  MARSHAL_TEST_STRCMP (STRING, DBUS_BIG_ENDIAN, "This is the dbus test string");
  MARSHAL_TEST_STRCMP (STRING, DBUS_LITTLE_ENDIAN, "This is the dbus test string");

  /* object paths */
  MARSHAL_TEST_STRCMP (OBJECT_PATH, DBUS_BIG_ENDIAN, "/a/b/c");
  MARSHAL_TEST_STRCMP (OBJECT_PATH, DBUS_LITTLE_ENDIAN, "/a/b/c");

  /* signatures */
  MARSHAL_TEST_STRCMP (SIGNATURE, DBUS_BIG_ENDIAN, "");
  MARSHAL_TEST_STRCMP (SIGNATURE, DBUS_LITTLE_ENDIAN, "");
  MARSHAL_TEST_STRCMP (SIGNATURE, DBUS_BIG_ENDIAN, "a(ii)");
  MARSHAL_TEST_STRCMP (SIGNATURE, DBUS_LITTLE_ENDIAN, "a(ii)");

#if 0

  /*
   * FIXME restore the set/pack tests
   */

#ifdef DBUS_HAVE_INT64
  /* set/pack 64-bit integers */
  _dbus_string_set_length (&str, 8);

  /* signed little */
  _dbus_marshal_set_int64 (&str, DBUS_LITTLE_ENDIAN,
                           0, DBUS_INT64_CONSTANT (-0x123456789abc7));

  _dbus_assert (DBUS_INT64_CONSTANT (-0x123456789abc7) ==
                _dbus_unpack_int64 (DBUS_LITTLE_ENDIAN,
                                    _dbus_string_get_const_data (&str)));

  /* signed big */
  _dbus_marshal_set_int64 (&str, DBUS_BIG_ENDIAN,
                           0, DBUS_INT64_CONSTANT (-0x123456789abc7));

  _dbus_assert (DBUS_INT64_CONSTANT (-0x123456789abc7) ==
                _dbus_unpack_int64 (DBUS_BIG_ENDIAN,
                                    _dbus_string_get_const_data (&str)));

  /* signed little pack */
  _dbus_pack_int64 (DBUS_INT64_CONSTANT (-0x123456789abc7),
                    DBUS_LITTLE_ENDIAN,
                    _dbus_string_get_data (&str));

  _dbus_assert (DBUS_INT64_CONSTANT (-0x123456789abc7) ==
                _dbus_unpack_int64 (DBUS_LITTLE_ENDIAN,
                                    _dbus_string_get_const_data (&str)));

  /* signed big pack */
  _dbus_pack_int64 (DBUS_INT64_CONSTANT (-0x123456789abc7),
                    DBUS_BIG_ENDIAN,
                    _dbus_string_get_data (&str));

  _dbus_assert (DBUS_INT64_CONSTANT (-0x123456789abc7) ==
                _dbus_unpack_int64 (DBUS_BIG_ENDIAN,
                                    _dbus_string_get_const_data (&str)));

  /* unsigned little */
  _dbus_marshal_set_uint64 (&str, DBUS_LITTLE_ENDIAN,
                            0, DBUS_UINT64_CONSTANT (0x123456789abc7));

  _dbus_assert (DBUS_UINT64_CONSTANT (0x123456789abc7) ==
                _dbus_unpack_uint64 (DBUS_LITTLE_ENDIAN,
                                     _dbus_string_get_const_data (&str)));

  /* unsigned big */
  _dbus_marshal_set_uint64 (&str, DBUS_BIG_ENDIAN,
                            0, DBUS_UINT64_CONSTANT (0x123456789abc7));

  _dbus_assert (DBUS_UINT64_CONSTANT (0x123456789abc7) ==
                _dbus_unpack_uint64 (DBUS_BIG_ENDIAN,
                                     _dbus_string_get_const_data (&str)));

  /* unsigned little pack */
  _dbus_pack_uint64 (DBUS_UINT64_CONSTANT (0x123456789abc7),
                     DBUS_LITTLE_ENDIAN,
                     _dbus_string_get_data (&str));

  _dbus_assert (DBUS_UINT64_CONSTANT (0x123456789abc7) ==
                _dbus_unpack_uint64 (DBUS_LITTLE_ENDIAN,
                                     _dbus_string_get_const_data (&str)));

  /* unsigned big pack */
  _dbus_pack_uint64 (DBUS_UINT64_CONSTANT (0x123456789abc7),
                     DBUS_BIG_ENDIAN,
                     _dbus_string_get_data (&str));

  _dbus_assert (DBUS_UINT64_CONSTANT (0x123456789abc7) ==
                _dbus_unpack_uint64 (DBUS_BIG_ENDIAN,
                                     _dbus_string_get_const_data (&str)));
#endif /* DBUS_HAVE_INT64 */

  /* set/pack 32-bit integers */
  _dbus_string_set_length (&str, 4);

  /* signed little */
  _dbus_marshal_set_int32 (&str, DBUS_LITTLE_ENDIAN,
                           0, -0x123456);

  _dbus_assert (-0x123456 ==
                _dbus_unpack_int32 (DBUS_LITTLE_ENDIAN,
                                    _dbus_string_get_const_data (&str)));

  /* signed big */
  _dbus_marshal_set_int32 (&str, DBUS_BIG_ENDIAN,
                           0, -0x123456);

  _dbus_assert (-0x123456 ==
                _dbus_unpack_int32 (DBUS_BIG_ENDIAN,
                                    _dbus_string_get_const_data (&str)));

  /* signed little pack */
  _dbus_pack_int32 (-0x123456,
                    DBUS_LITTLE_ENDIAN,
                    _dbus_string_get_data (&str));

  _dbus_assert (-0x123456 ==
                _dbus_unpack_int32 (DBUS_LITTLE_ENDIAN,
                                    _dbus_string_get_const_data (&str)));

  /* signed big pack */
  _dbus_pack_int32 (-0x123456,
                    DBUS_BIG_ENDIAN,
                    _dbus_string_get_data (&str));

  _dbus_assert (-0x123456 ==
                _dbus_unpack_int32 (DBUS_BIG_ENDIAN,
                                    _dbus_string_get_const_data (&str)));

  /* unsigned little */
  _dbus_marshal_set_uint32 (&str, DBUS_LITTLE_ENDIAN,
                            0, 0x123456);

  _dbus_assert (0x123456 ==
                _dbus_unpack_uint32 (DBUS_LITTLE_ENDIAN,
                                     _dbus_string_get_const_data (&str)));

  /* unsigned big */
  _dbus_marshal_set_uint32 (&str, DBUS_BIG_ENDIAN,
                            0, 0x123456);

  _dbus_assert (0x123456 ==
                _dbus_unpack_uint32 (DBUS_BIG_ENDIAN,
                                     _dbus_string_get_const_data (&str)));

  /* unsigned little pack */
  _dbus_pack_uint32 (0x123456,
                     DBUS_LITTLE_ENDIAN,
                     _dbus_string_get_data (&str));

  _dbus_assert (0x123456 ==
                _dbus_unpack_uint32 (DBUS_LITTLE_ENDIAN,
                                     _dbus_string_get_const_data (&str)));

  /* unsigned big pack */
  _dbus_pack_uint32 (0x123456,
                     DBUS_BIG_ENDIAN,
                     _dbus_string_get_data (&str));

  _dbus_assert (0x123456 ==
                _dbus_unpack_uint32 (DBUS_BIG_ENDIAN,
                                     _dbus_string_get_const_data (&str)));

#endif /* set/pack tests for integers */

  /* Strings in-place set */
  byte_order = DBUS_LITTLE_ENDIAN;
  while (TRUE)
    {
      /* Init a string */
      _dbus_string_set_length (&str, 0);

      /* reset pos for the macros */
      pos = 0;

      MARSHAL_TEST_STRCMP (STRING, byte_order, "Hello world");

      /* Set it to something longer */
      _dbus_string_init_const (&t, "Hello world foo");

      v_STRING = _dbus_string_get_const_data (&t);
      _dbus_marshal_set_basic (&str, 0, DBUS_TYPE_STRING,
                               &v_STRING, byte_order, NULL, NULL);

      _dbus_marshal_read_basic (&str, 0, DBUS_TYPE_STRING,
                                &v_STRING, byte_order,
                                NULL);
      _dbus_assert (strcmp (v_STRING, "Hello world foo") == 0);

      /* Set it to something shorter */
      _dbus_string_init_const (&t, "Hello");

      v_STRING = _dbus_string_get_const_data (&t);
      _dbus_marshal_set_basic (&str, 0, DBUS_TYPE_STRING,
                               &v_STRING, byte_order, NULL, NULL);
      _dbus_marshal_read_basic (&str, 0, DBUS_TYPE_STRING,
                                &v_STRING, byte_order,
                                NULL);
      _dbus_assert (strcmp (v_STRING, "Hello") == 0);

      /* Do the other byte order */
      if (byte_order == DBUS_LITTLE_ENDIAN)
        byte_order = DBUS_BIG_ENDIAN;
      else
        break;
    }

  /* Clean up */
  _dbus_string_free (&str);

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
