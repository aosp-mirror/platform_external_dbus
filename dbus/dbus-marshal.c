/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-marshal.c  Marshalling routines
 *
 * Copyright (C) 2002  CodeFactory AB
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

#include "dbus-marshal.h"
#include "dbus-internals.h"

#include <string.h>

#define DBUS_UINT32_SWAP_LE_BE_CONSTANT(val)	((dbus_uint32_t) ( \
    (((dbus_uint32_t) (val) & (dbus_uint32_t) 0x000000ffU) << 24) |  \
    (((dbus_uint32_t) (val) & (dbus_uint32_t) 0x0000ff00U) <<  8) |  \
    (((dbus_uint32_t) (val) & (dbus_uint32_t) 0x00ff0000U) >>  8) |  \
    (((dbus_uint32_t) (val) & (dbus_uint32_t) 0xff000000U) >> 24)))

#define DBUS_UINT32_SWAP_LE_BE(val) (DBUS_UINT32_SWAP_LE_BE_CONSTANT (val))

#ifdef WORDS_BIGENDIAN
#define DBUS_INT32_TO_BE(val)	((dbus_int32_t) (val))
#define DBUS_UINT32_TO_BE(val)	((dbus_uint32_t) (val))
#define DBUS_INT32_TO_LE(val)	((dbus_int32_t) DBUS_UINT32_SWAP_LE_BE (val))
#define DBUS_UINT32_TO_LE(val)	(DBUS_UINT32_SWAP_LE_BE (val))
#else
#define DBUS_INT32_TO_LE(val)	((dbus_int32_t) (val))
#define DBUS_UINT32_TO_LE(val)	((dbus_uint32_t) (val))
#define DBUS_INT32_TO_BE(val)	((dbus_int32_t) DBUS_UINT32_SWAP_LE_BE (val))
#define DBUS_UINT32_TO_BE(val)	(DBUS_UINT32_SWAP_LE_BE (val))
#endif

/* The transformation is symmetric, so the FROM just maps to the TO. */
#define DBUS_INT32_FROM_LE(val)	 (DBUS_INT32_TO_LE (val))
#define DBUS_UINT32_FROM_LE(val) (DBUS_UINT32_TO_LE (val))
#define DBUS_INT32_FROM_BE(val)	 (DBUS_INT32_TO_BE (val))
#define DBUS_UINT32_FROM_BE(val) (DBUS_UINT32_TO_BE (val))


/* This alignment thing is from ORBit2 */
/* Align a value upward to a boundary, expressed as a number of bytes.
 * E.g. align to an 8-byte boundary with argument of 8.
 */

/*
 *   (this + boundary - 1)
 *          &
 *    ~(boundary - 1)
 */

#define DBUS_ALIGN_VALUE(this, boundary) \
  (( ((unsigned long)(this)) + (((unsigned long)(boundary)) -1)) & (~(((unsigned long)(boundary))-1)))

#define DBUS_ALIGN_ADDRESS(this, boundary) \
  ((void*)DBUS_ALIGN_VALUE(this, boundary))

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

static dbus_uint32_t
unpack_uint32 (int                  byte_order,
               const unsigned char *data)
{
  _dbus_assert (DBUS_ALIGN_ADDRESS (data, 4) == data);
  
  if (byte_order == DBUS_LITTLE_ENDIAN)
    return DBUS_UINT32_FROM_LE (*(dbus_uint32_t*)data);
  else
    return DBUS_UINT32_FROM_BE (*(dbus_uint32_t*)data);
}             

static dbus_int32_t
unpack_int32 (int                  byte_order,
              const unsigned char *data)
{
  _dbus_assert (DBUS_ALIGN_ADDRESS (data, 4) == data);
  
  if (byte_order == DBUS_LITTLE_ENDIAN)
    return DBUS_INT32_FROM_LE (*(dbus_int32_t*)data);
  else
    return DBUS_INT32_FROM_BE (*(dbus_int32_t*)data);
}

/**
 * @defgroup DBusMarshal marshaling and unmarshaling
 * @ingroup  DBusInternals
 * @brief functions to marshal/unmarshal data from the wire
 *
 * Types and functions related to converting primitive data types from
 * wire format to native machine format, and vice versa.
 *
 * @{
 */

dbus_bool_t
_dbus_marshal_double (DBusString *str,
		      int         byte_order,
		      double      value)
{
  if (!_dbus_string_set_length (str,
				DBUS_ALIGN_VALUE (_dbus_string_get_length (str),
						  sizeof (double))))
    return FALSE;
  
  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    swap_bytes ((unsigned char *)&value, sizeof (double));

  return _dbus_string_append_len (str, (const char *)&value, sizeof (double));
}

dbus_bool_t
_dbus_marshal_int32  (DBusString   *str,
		      int           byte_order,
		      dbus_int32_t  value)
{
  if (!_dbus_string_set_length (str,
				DBUS_ALIGN_VALUE (_dbus_string_get_length (str),
						  sizeof (dbus_int32_t))))
    return FALSE;
  
  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    swap_bytes ((unsigned char *)&value, sizeof (dbus_int32_t));

  return _dbus_string_append_len (str, (const char *)&value, sizeof (dbus_int32_t));
}

dbus_bool_t
_dbus_marshal_uint32 (DBusString    *str,
		      int            byte_order,
		      dbus_uint32_t  value)
{
  if (!_dbus_string_set_length (str,
				DBUS_ALIGN_VALUE (_dbus_string_get_length (str),
						  sizeof (dbus_uint32_t))))
    return FALSE;

  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    swap_bytes ((unsigned char *)&value, sizeof (dbus_uint32_t));

  return _dbus_string_append_len (str, (const char *)&value, sizeof (dbus_uint32_t));
}

dbus_bool_t
_dbus_marshal_utf8_string (DBusString    *str,
			   int            byte_order,
			   const char    *value)
{
  int len;

  len = strlen (value);

  if (!_dbus_marshal_uint32 (str, byte_order, len))
    return FALSE;

  return _dbus_string_append_len (str, value, len + 1);
}

dbus_bool_t
_dbus_marshal_byte_array (DBusString          *str,
			  int                  byte_order,
			  const unsigned char *value,
			  int                  len)
{
  if (!_dbus_marshal_uint32 (str, byte_order, len))
    return FALSE;

  return _dbus_string_append_len (str, value, len);
}

double
_dbus_demarshal_double (DBusString  *str,
			int          byte_order,
			int          pos,
			int         *new_pos)
{
  double retval;
  const char *buffer;

  pos = DBUS_ALIGN_VALUE (pos, sizeof (double));
  
  _dbus_string_get_const_data_len (str, &buffer, pos, sizeof (double));

  retval = *(double *)buffer;
  
  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    swap_bytes ((unsigned char *)&retval, sizeof (double));

  if (new_pos)
    *new_pos = pos + sizeof (double);
  
  return retval;  
}

dbus_int32_t
_dbus_demarshal_int32  (DBusString *str,
			int         byte_order,
			int         pos,
			int        *new_pos)
{
  const char *buffer;

  pos = DBUS_ALIGN_VALUE (pos, sizeof (dbus_int32_t));
  
  _dbus_string_get_const_data_len (str, &buffer, pos, sizeof (dbus_int32_t));

  if (new_pos)
    *new_pos = pos + sizeof (dbus_int32_t);

  return unpack_int32 (byte_order, buffer);
}

dbus_uint32_t
_dbus_demarshal_uint32  (DBusString *str,
			 int         byte_order,
			 int         pos,
			 int        *new_pos)
{
  const char *buffer;

  pos = DBUS_ALIGN_VALUE (pos, sizeof (dbus_uint32_t));
  
  _dbus_string_get_const_data_len (str, &buffer, pos, sizeof (dbus_uint32_t));

  if (new_pos)
    *new_pos = pos + sizeof (dbus_uint32_t);

  return unpack_uint32 (byte_order, buffer);
}

char *
_dbus_demarshal_utf8_string (DBusString *str,
			     int         byte_order,
			     int         pos,
			     int        *new_pos)
{
  int len;
  char *retval;
  const char *data;

  len = _dbus_demarshal_uint32 (str, byte_order, pos, &pos);

  retval = dbus_malloc (len + 1);

  if (!retval)
    return NULL;

  _dbus_string_get_const_data_len (str, &data, pos, 3);

  if (!data)
    return NULL;

  memcpy (retval, data, len + 1);

  if (new_pos)
    *new_pos = pos + len + 1;
  
  return retval;
}

/**
 * If in verbose mode, print a block of binary data.
 *
 * @param data the data
 * @param len the length of the data
 */
void
_dbus_verbose_bytes (const unsigned char *data,
                     int                  len)
{
  int i;
  const unsigned char *aligned;

  /* Print blanks on first row if appropriate */
  aligned = DBUS_ALIGN_ADDRESS (data, 4);
  if (aligned > data)
    aligned -= 4;
  _dbus_assert (aligned <= data);

  if (aligned != data)
    {
      _dbus_verbose ("%5d\t%p: ", - (data - aligned), aligned); 
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
      if (DBUS_ALIGN_ADDRESS (&data[i], 4) == &data[i])
        {
          _dbus_verbose ("%5d\t%p: ",
                   i, &data[i]);
        }
      
      if (data[i] >= 32 &&
          data[i] <= 126)
        _dbus_verbose (" '%c' ", data[i]);
      else
        _dbus_verbose ("0x%s%x ",
                 data[i] <= 0xf ? "0" : "", data[i]);

      ++i;

      if (DBUS_ALIGN_ADDRESS (&data[i], 4) == &data[i])
        {
          if (i > 3)
            _dbus_verbose ("big: %d little: %d",
                     unpack_uint32 (DBUS_BIG_ENDIAN, &data[i-4]),
                     unpack_uint32 (DBUS_LITTLE_ENDIAN, &data[i-4]));
          
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

  _dbus_string_get_const_data_len (str, &d, start, len);

  _dbus_verbose_bytes (d, len);
}

/** @} */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>

dbus_bool_t
_dbus_marshal_test (void)
{
  DBusString str;
  char *tmp1, *tmp2;
  int pos = 0;
  
  if (!_dbus_string_init (&str, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("failed to init string");


  /* Marshal doubles */
  if (!_dbus_marshal_double (&str, DBUS_BIG_ENDIAN, 3.14))
    _dbus_assert_not_reached ("could not marshal double value");
  _dbus_assert (_dbus_demarshal_double (&str, DBUS_BIG_ENDIAN, pos, &pos) == 3.14);

  
  if (!_dbus_marshal_double (&str, DBUS_LITTLE_ENDIAN, 3.14))
    _dbus_assert_not_reached ("could not marshal double value");
  _dbus_assert (_dbus_demarshal_double (&str, DBUS_LITTLE_ENDIAN, pos, &pos) == 3.14);
  
  /* Marshal signed integers */
  if (!_dbus_marshal_int32 (&str, DBUS_BIG_ENDIAN, -12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  _dbus_assert (_dbus_demarshal_int32 (&str, DBUS_BIG_ENDIAN, pos, &pos) == -12345678);

  if (!_dbus_marshal_int32 (&str, DBUS_LITTLE_ENDIAN, -12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  _dbus_assert (_dbus_demarshal_int32 (&str, DBUS_LITTLE_ENDIAN, pos, &pos) == -12345678);
  
  /* Marshal unsigned integers */
  if (!_dbus_marshal_uint32 (&str, DBUS_BIG_ENDIAN, 0x12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  _dbus_assert (_dbus_demarshal_uint32 (&str, DBUS_BIG_ENDIAN, pos, &pos) == 0x12345678);
  
  if (!_dbus_marshal_uint32 (&str, DBUS_LITTLE_ENDIAN, 0x12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  _dbus_assert (_dbus_demarshal_uint32 (&str, DBUS_LITTLE_ENDIAN, pos, &pos) == 0x12345678);

  /* Marshal strings */
  tmp1 = "This is the dbus test string";
  if (!_dbus_marshal_utf8_string (&str, DBUS_BIG_ENDIAN, tmp1))
    _dbus_assert_not_reached ("could not marshal string");
  tmp2 = _dbus_demarshal_utf8_string (&str, DBUS_BIG_ENDIAN, pos, &pos);
  _dbus_assert (strcmp (tmp1, tmp2) == 0);
  dbus_free (tmp2);

  tmp1 = "This is the dbus test string";
  if (!_dbus_marshal_utf8_string (&str, DBUS_LITTLE_ENDIAN, tmp1))
    _dbus_assert_not_reached ("could not marshal string");
  tmp2 = _dbus_demarshal_utf8_string (&str, DBUS_LITTLE_ENDIAN, pos, &pos);
  _dbus_assert (strcmp (tmp1, tmp2) == 0);
  dbus_free (tmp2);

  _dbus_string_free (&str);
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
