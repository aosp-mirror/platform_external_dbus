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


/* This alignment thing is from ORBit2 */
/* Align a value upward to a boundary, expressed as a number of bytes.
   E.g. align to an 8-byte boundary with argument of 8.  */

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



double
_dbus_demarshal_double (DBusString  *str,
			int          byte_order,
			int          start)
{
  double retval;
  const char *buffer;
  
  _dbus_string_get_const_data_len (str, &buffer, start, sizeof (double));

  retval = *(double *)buffer;
  
  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    swap_bytes ((unsigned char *)&retval, sizeof (double));

  return retval;  
}

dbus_int32_t
_dbus_demarshal_int32  (DBusString *str,
			int         byte_order,
			int         start)
{
  dbus_int32_t retval;
  const char *buffer;

  _dbus_string_get_const_data_len (str, &buffer, start, sizeof (dbus_int32_t));

  retval = *(dbus_int32_t *)buffer;

  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    swap_bytes ((unsigned char *)&retval, sizeof (dbus_int32_t));

  return retval;  
}

dbus_uint32_t
_dbus_demarshal_uint32  (DBusString *str,
			 int         byte_order,
			 int         start)
{
  dbus_uint32_t retval;
  const char *buffer;

  _dbus_string_get_const_data_len (str, &buffer, start, sizeof (dbus_uint32_t));

  retval = *(dbus_uint32_t *)buffer;

  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    swap_bytes ((unsigned char *)&retval, sizeof (dbus_uint32_t));

  return retval;  
}

/** @} */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>

dbus_bool_t
_dbus_marshal_test (void)
{
  DBusString str;
  int pos = 0;
  
  if (!_dbus_string_init (&str, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("failed to init string");


  /* Marshal doubles */
  if (!_dbus_marshal_double (&str, DBUS_BIG_ENDIAN, 3.14))
    _dbus_assert_not_reached ("could not marshal double value");
  _dbus_assert (_dbus_demarshal_double (&str, DBUS_BIG_ENDIAN, pos) == 3.14);
  pos += 8;
  
  if (!_dbus_marshal_double (&str, DBUS_LITTLE_ENDIAN, 3.14))
    _dbus_assert_not_reached ("could not marshal double value");
  _dbus_assert (_dbus_demarshal_double (&str, DBUS_LITTLE_ENDIAN, pos) == 3.14);
  pos += 8;
  
  /* Marshal signed integers */
  if (!_dbus_marshal_int32 (&str, DBUS_BIG_ENDIAN, -12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  _dbus_assert (_dbus_demarshal_int32 (&str, DBUS_BIG_ENDIAN, pos) == -12345678);
  pos += 4;

  if (!_dbus_marshal_int32 (&str, DBUS_LITTLE_ENDIAN, -12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  _dbus_assert (_dbus_demarshal_int32 (&str, DBUS_LITTLE_ENDIAN, pos) == -12345678);
  pos += 4;
  
  /* Marshal unsigned integers */
  if (!_dbus_marshal_uint32 (&str, DBUS_LITTLE_ENDIAN, 0x12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  _dbus_assert (_dbus_demarshal_uint32 (&str, DBUS_LITTLE_ENDIAN, pos) == 0x12345678);
  pos += 4;
  
  _dbus_string_free (&str);

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
