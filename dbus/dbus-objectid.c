/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-objectid.c  DBusObjectID type
 *
 * Copyright (C) 2003  Red Hat Inc.
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

#include "dbus-objectid.h"
#include "dbus-internals.h"

#ifdef DBUS_HAVE_INT64
#define VALUE(objid) ((objid)->dbus_do_not_use_dummy1)
#define HIGH_BITS(objid) ((dbus_uint32_t) (VALUE (obj_id) >> 32))
#define LOW_BITS(objid)  ((dbus_uint32_t) (VALUE (obj_id) & DBUS_UINT64_CONSTANT (0x00000000ffffffff)))
#else
#define HIGH_BITS(objid) ((objid)->dbus_do_not_use_dummy1)
#define LOW_BITS(objid) ((objid)->dbus_do_not_use_dummy2)
#endif

/**
 * @defgroup DBusObjectID object IDs
 * @ingroup  DBusObjectID
 * @brief object ID datatype
 *
 * Value type representing an object ID, i.e. an object in the remote
 * application that can be communicated with.
 *
 * @{
 */

/**
 * Checks whether two object IDs have the same value.
 *
 * @param a the first object ID
 * @param b the second object ID
 * @returns #TRUE if they are equal
 */
dbus_bool_t
dbus_object_id_equal (const DBusObjectID *a,
                      const DBusObjectID *b)
{
#ifdef DBUS_HAVE_INT64
  return VALUE (a) == VALUE (b);
#else
  return HIGH_BITS (a) == HIGH_BITS (b) &&
    LOW_BITS (a) == LOW_BITS (b);
#endif
}

/**
 * Compares two object IDs, appropriate for
 * qsort(). Higher/lower IDs have no significance,
 * but the comparison can be used for data structures
 * that require ordering.
 *
 * @param a the first object ID
 * @param b the second object ID
 * @returns -1, 0, 1 as with strcmp()
 */
int
dbus_object_id_compare (const DBusObjectID *a,
                        const DBusObjectID *b)
{
#ifdef DBUS_HAVE_INT64
  if (VALUE (a) > VALUE (b))
    return 1;
  else if (VALUE (a) < VALUE (b))
    return -1;
  else
    return 0;
#else
  if (HIGH_BITS (a) > HIGH_BITS (b))
    return 1;
  else if (HIGH_BITS (a) < HIGH_BITS (b))
    return -1;
  else if (LOW_BITS (a) > LOW_BITS (b))
    return 1;
  else if (LOW_BITS (a) < LOW_BITS (b))
    return -1;
  else
    return 0;
#endif
}

/**
 * An object ID contains 64 bits of data. This function
 * returns half of those bits. If you are willing to limit
 * portability to compilers with a 64-bit type (this includes
 * C99 compilers and almost all other compilers) consider
 * dbus_object_id_get_as_integer() instead.
 *
 * @param obj_id the object ID
 * @returns the high bits of the ID
 * 
 */
dbus_uint32_t
dbus_object_id_get_high_bits (const DBusObjectID *obj_id)
{
  return HIGH_BITS (obj_id);
}

/**
 * An object ID contains 64 bits of data. This function
 * returns half of those bits. If you are willing to limit
 * portability to compilers with a 64-bit type (this includes
 * C99 compilers and almost all other compilers) consider
 * dbus_object_id_get_as_integer() instead.
 *
 * @param obj_id the object ID
 * @returns the low bits of the ID
 * 
 */
dbus_uint32_t
dbus_object_id_get_low_bits (const DBusObjectID *obj_id)
{
  return LOW_BITS (obj_id);
}

/**
 * An object ID contains 64 bits of data. This function
 * sets half of those bits. If you are willing to limit
 * portability to compilers with a 64-bit type (this includes
 * C99 compilers and almost all other compilers) consider
 * dbus_object_id_set_as_integer() instead.
 *
 * @param obj_id the object ID
 * @param value the new value of the high bits
 * 
 */
void
dbus_object_id_set_high_bits (DBusObjectID       *obj_id,
                              dbus_uint32_t       value)
{
#ifdef DBUS_HAVE_INT64
  VALUE (obj_id) = (((dbus_uint64_t) value) << 32) | LOW_BITS (obj_id);
#else
  HIGH_BITS (obj_id) = value;
#endif
}

/**
 * An object ID contains 64 bits of data. This function
 * sets half of those bits. If you are willing to limit
 * portability to compilers with a 64-bit type (this includes
 * C99 compilers and almost all other compilers) consider
 * dbus_object_id_set_as_integer() instead.
 *
 * @param obj_id the object ID
 * @param value the new value of the low bits
 * 
 */
void
dbus_object_id_set_low_bits (DBusObjectID       *obj_id,
                             dbus_uint32_t       value)
{
#ifdef DBUS_HAVE_INT64
  VALUE (obj_id) = ((dbus_uint64_t) value) |
    (((dbus_uint64_t) HIGH_BITS (obj_id)) << 32);
#else
  LOW_BITS (obj_id) = value;
#endif
}

/**
 * Set the object ID to an invalid value that cannot
 * correspond to a valid object.
 *
 * @param obj_id the object ID
 */
void
dbus_object_id_set_null (DBusObjectID *obj_id)
{
  memset (obj_id, '\0', sizeof (DBusObjectID));
}

/**
 * Check whether the object ID is set to a null value
 *
 * @param obj_id the object ID
 * @returns #TRUE if null
 */
dbus_bool_t
dbus_object_id_is_null (const DBusObjectID *obj_id)
{
#ifdef DBUS_HAVE_INT64
  return VALUE (obj_id) == 0;
#else
  return HIGH_BITS (obj_id) == 0 && LOW_BITS (obj_id) == 0;
#endif
}

#ifdef DBUS_HAVE_INT64
/**
 * An object ID contains 64 bits of data. This function
 * returns all of them as a 64-bit integer.
 *  
 * Use this function only if you are willing to limit portability to
 * compilers with a 64-bit type (this includes C99 compilers and
 * almost all other compilers).
 *
 * This function only exists if DBUS_HAVE_INT64 is defined.
 *
 * @param obj_id the object ID
 * @returns the object ID as a 64-bit integer.
 */
dbus_uint64_t
dbus_object_id_get_as_integer (const DBusObjectID *obj_id)
{
  return VALUE (obj_id);
}

/**
 * An object ID contains 64 bits of data. This function sets all of
 * them as a 64-bit integer.
 *  
 * Use this function only if you are willing to limit portability to
 * compilers with a 64-bit type (this includes C99 compilers and
 * almost all other compilers).
 * 
 * This function only exists if #DBUS_HAVE_INT64 is defined.
 *
 * @param obj_id the object ID
 * @param value the new value of the object ID
 */
void
dbus_object_id_set_as_integer (DBusObjectID       *obj_id,
                               dbus_uint64_t       value)
{
  VALUE (obj_id) = value;
}
#endif /* DBUS_HAVE_INT64 */

/** @} */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>

/**
 * Test for object ID routines.
 *
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_object_id_test (void)
{
  DBusObjectID tmp;
  DBusObjectID tmp2;

  dbus_object_id_set_high_bits (&tmp, 340);
  _dbus_assert (dbus_object_id_get_high_bits (&tmp) == 340);

  dbus_object_id_set_low_bits (&tmp, 1492);
  _dbus_assert (dbus_object_id_get_low_bits (&tmp) == 1492);
  _dbus_assert (dbus_object_id_get_high_bits (&tmp) == 340);
  
  tmp2 = tmp;
  _dbus_assert (dbus_object_id_equal (&tmp, &tmp2));
  
#ifdef DBUS_HAVE_INT64
  _dbus_assert (dbus_object_id_get_as_integer (&tmp) ==
                ((DBUS_UINT64_CONSTANT (340) << 32) |
                 DBUS_UINT64_CONSTANT (1492)));

  dbus_object_id_set_as_integer (&tmp, _DBUS_UINT64_MAX);
  _dbus_assert (dbus_object_id_get_as_integer (&tmp) ==
                _DBUS_UINT64_MAX);
  _dbus_assert (dbus_object_id_get_high_bits (&tmp) ==
                _DBUS_UINT_MAX);
  _dbus_assert (dbus_object_id_get_low_bits (&tmp) ==
                _DBUS_UINT_MAX);

  dbus_object_id_set_as_integer (&tmp, 1);
  dbus_object_id_set_as_integer (&tmp2, 2);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == -1);
  dbus_object_id_set_as_integer (&tmp2, 0);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == 1);
  dbus_object_id_set_as_integer (&tmp2, 1);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == 0);
#endif

  tmp2 = tmp;
  
  dbus_object_id_set_high_bits (&tmp, 1);
  dbus_object_id_set_high_bits (&tmp2, 2);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == -1);
  dbus_object_id_set_high_bits (&tmp2, 0);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == 1);
  dbus_object_id_set_high_bits (&tmp2, 1);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == 0);

  dbus_object_id_set_low_bits (&tmp, 1);
  
  dbus_object_id_set_low_bits (&tmp2, 2);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == -1);
  dbus_object_id_set_low_bits (&tmp2, 0);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == 1);
  dbus_object_id_set_low_bits (&tmp2, 1);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == 0);
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
