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
#define VALUE(objid)         ((objid)->dbus_do_not_use_dummy1)
#define SERVER_MASK          DBUS_UINT64_CONSTANT (0xffff000000000000)
#define CLIENT_MASK          DBUS_UINT64_CONSTANT (0x0000ffff00000000)
#define IS_SERVER_MASK       DBUS_UINT64_CONSTANT (0x0000000080000000)
#define INSTANCE_MASK        DBUS_UINT64_CONSTANT (0x000000007fffffff)
#define SERVER_BITS(objid)   ((dbus_uint16_t) (VALUE (obj_id) >> 48))
#define CLIENT_BITS(objid)   ((dbus_uint16_t) ((VALUE (obj_id) & CLIENT_MASK) >> 32))
#define IS_SERVER_BIT(objid) ((VALUE (obj_id) & IS_SERVER_MASK) != 0)
#define INSTANCE_BITS(objid) ((dbus_uint32_t) (VALUE (obj_id) & INSTANCE_MASK))
#else
/* We care about the exact packing since in dbus-marshal.c we
 * just use the DBusObjectID struct as-is.
 */
#ifdef WORDS_BIGENDIAN
#define HIGH_VALUE(objid)    ((objid)->dbus_do_not_use_dummy2)
#define LOW_VALUE(objid)     ((objid)->dbus_do_not_use_dummy3)
#else
#define HIGH_VALUE(objid)    ((objid)->dbus_do_not_use_dummy3)
#define LOW_VALUE(objid)     ((objid)->dbus_do_not_use_dummy2)
#endif
#define SERVER_MASK          (0xffff0000)
#define CLIENT_MASK          (0x0000ffff)
#define IS_SERVER_MASK       (0x80000000)
#define INSTANCE_MASK        (0x7fffffff)
#define SERVER_BITS(objid)   ((HIGH_VALUE (objid) & SERVER_MASK) >> 16)
#define CLIENT_BITS(objid)   (HIGH_VALUE (objid) & CLIENT_MASK)
#define IS_SERVER_BIT(objid) ((LOW_VALUE (objid) & IS_SERVER_MASK) != 0)
#define INSTANCE_BITS(objid) (LOW_VALUE (objid) & INSTANCE_MASK)
#endif

/**
 * @defgroup DBusObjectID object IDs
 * @ingroup  DBusObjectID
 * @brief object ID datatype
 *
 * Value type representing an object ID, i.e. an object in the remote
 * application that can be communicated with.
 *
 * An object ID has three parts. 16 bits are provided by the server
 * side of a connection, and used for the high 16 bits of all object
 * IDs created by the client. 16 bits are provided by the client side
 * and used as the next 16 bits of all object IDs created by the
 * client. The next single bit is 1 if the object ID represents an
 * object on the server side of the connection and 0 otherwise.  Then
 * 31 bits are provided by the side creating an object instance and
 * differ for each instance created (each app should make a best
 * effort to avoid recycling the instance values).
 *
 * 0 is an invalid value for the server bits, the client bits,
 * and the object instance bits. An object ID is the null ID
 * if all 64 bits are 0.
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
  return LOW_VALUE (a) == LOW_VALUE (b) && HIGH_VALUE (a) == HIGH_VALUE (b);
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
  if (HIGH_VALUE (a) > HIGH_VALUE (b))
    return 1;
  else if (HIGH_VALUE (a) < HIGH_VALUE (b))
    return -1;
  else if (LOW_VALUE (a) > LOW_VALUE (b))
    return 1;
  else if (LOW_VALUE (a) < LOW_VALUE (b))
    return -1;
  else
    return 0;
#endif
}


/**
 * An object ID contains 64 bits of data. This function
 * returns the 16 bits that were provided by the server
 * side of the connection.
 *
 * @param obj_id the object ID
 * @returns the server bits of the ID
 * 
 */
dbus_uint16_t
dbus_object_id_get_server_bits (const DBusObjectID *obj_id)
{
  return SERVER_BITS (obj_id);
}

/**
 * An object ID contains 64 bits of data. This function
 * returns the 16 bits that were provided by the client
 * side of the connection.
 *
 * @param obj_id the object ID
 * @returns the client bits of the ID
 * 
 */
dbus_uint16_t
dbus_object_id_get_client_bits (const DBusObjectID *obj_id)
{
  return CLIENT_BITS (obj_id);
}

/**
 * An object ID contains 64 bits of data. This function
 * returns the bit flagging whether the object ID comes
 * from the client or the server side of the connection.
 *
 * There is no secure guarantee that the bit is accurate;
 * object ID values are simply conventional, to make
 * collisions relatively unlikely.
 *
 * @param obj_id the object ID
 * @returns the server-side bit of the ID
 * 
 */
dbus_bool_t
dbus_object_id_get_is_server_bit (const DBusObjectID *obj_id)
{
  return IS_SERVER_BIT (obj_id);
}

/**
 * An object ID contains 64 bits of data. This function
 * returns the 31 bits that identify the object instance.
 *
 * @param obj_id the object ID
 * @returns the instance bits of the ID
 * 
 */
dbus_uint32_t
dbus_object_id_get_instance_bits (const DBusObjectID *obj_id)
{
  return INSTANCE_BITS (obj_id);
}

/**
 * An object ID contains 64 bits of data. This function sets the 16
 * bits provided by the server side of a connection.
 *
 * @param obj_id the object ID
 * @param value the new value of the server bits
 * 
 */
void
dbus_object_id_set_server_bits (DBusObjectID       *obj_id,
                                dbus_uint16_t       value)
{
#ifdef DBUS_HAVE_INT64
  VALUE (obj_id) &= ~ SERVER_MASK;
  VALUE (obj_id) |= ((dbus_uint64_t) value) << 48;
#else
  HIGH_VALUE (obj_id) &= ~ SERVER_MASK;
  HIGH_VALUE (obj_id) |= ((dbus_uint32_t) value) << 16;
#endif
}

/**
 * An object ID contains 64 bits of data. This function sets the 16
 * bits provided by the client side of a connection.
 *
 * @param obj_id the object ID
 * @param value the new value of the client bits
 * 
 */
void
dbus_object_id_set_client_bits (DBusObjectID       *obj_id,
                                dbus_uint16_t       value)
{
#ifdef DBUS_HAVE_INT64
  VALUE (obj_id) &= ~ CLIENT_MASK;
  VALUE (obj_id) |= ((dbus_uint64_t) value) << 32;
#else
  HIGH_VALUE (obj_id) &= ~ CLIENT_MASK;
  HIGH_VALUE (obj_id) |= (dbus_uint32_t) value;
#endif
}

/**
 * An object ID contains 64 bits of data. This function sets the
 * single bit that flags an instance as server-side or client-side.
 *
 * @param obj_id the object ID
 * @param value the new value of the server-side bit
 * 
 */
void
dbus_object_id_set_is_server_bit (DBusObjectID       *obj_id,
                                  dbus_bool_t         value)
{
#ifdef DBUS_HAVE_INT64
  if (value)
    VALUE (obj_id) |= IS_SERVER_MASK;
  else
    VALUE (obj_id) &= ~ IS_SERVER_MASK;
#else
  if (value)
    LOW_VALUE (obj_id) |= IS_SERVER_MASK;
  else
    LOW_VALUE (obj_id) &= ~ IS_SERVER_MASK;
#endif
}

/**
 * An object ID contains 64 bits of data. This function sets the 31
 * bits identifying the object instance.
 *
 * @param obj_id the object ID
 * @param value the new value of the instance bits
 * 
 */
void
dbus_object_id_set_instance_bits (DBusObjectID       *obj_id,
                                  dbus_uint32_t       value)
{
#ifdef DBUS_HAVE_INT64
  VALUE (obj_id) &= ~ INSTANCE_MASK;
  VALUE (obj_id) |= (dbus_uint64_t) value;
#else
  LOW_VALUE (obj_id) &= ~ INSTANCE_MASK;
  LOW_VALUE (obj_id) |= (dbus_uint32_t) value;
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
  return HIGH_VALUE (obj_id) == 0 && LOW_VALUE (obj_id) == 0;
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

  /* Check basic get/set */
  
  dbus_object_id_set_server_bits (&tmp, 340);
  _dbus_assert (dbus_object_id_get_server_bits (&tmp) == 340);

  dbus_object_id_set_client_bits (&tmp, 1492);
  _dbus_assert (dbus_object_id_get_client_bits (&tmp) == 1492);
  _dbus_assert (dbus_object_id_get_server_bits (&tmp) == 340);

  dbus_object_id_set_is_server_bit (&tmp, TRUE);
  _dbus_assert (dbus_object_id_get_client_bits (&tmp) == 1492);
  _dbus_assert (dbus_object_id_get_server_bits (&tmp) == 340);
  _dbus_assert (dbus_object_id_get_is_server_bit (&tmp) == TRUE);

  dbus_object_id_set_instance_bits (&tmp, 2001);
  _dbus_assert (dbus_object_id_get_client_bits (&tmp) == 1492);
  _dbus_assert (dbus_object_id_get_server_bits (&tmp) == 340);
  _dbus_assert (dbus_object_id_get_is_server_bit (&tmp) == TRUE);
  _dbus_assert (dbus_object_id_get_instance_bits (&tmp) == 2001);

  /* check equality check */
  tmp2 = tmp;
  _dbus_assert (dbus_object_id_equal (&tmp, &tmp2));

  /* check get/set as integer */
#ifdef DBUS_HAVE_INT64
  _dbus_assert (dbus_object_id_get_as_integer (&tmp) ==
                ((DBUS_UINT64_CONSTANT (340) << 48) |
                 (DBUS_UINT64_CONSTANT (1492) << 32) |
                 (DBUS_UINT64_CONSTANT (1) << 31) |
                 (DBUS_UINT64_CONSTANT (2001))));

  dbus_object_id_set_as_integer (&tmp, _DBUS_UINT64_MAX);
  _dbus_assert (dbus_object_id_get_as_integer (&tmp) ==
                _DBUS_UINT64_MAX);
  _dbus_assert (dbus_object_id_get_server_bits (&tmp) ==
                0xffff);
  _dbus_assert (dbus_object_id_get_client_bits (&tmp) ==
                0xffff);
  _dbus_assert (dbus_object_id_get_is_server_bit (&tmp) ==
                TRUE);
  _dbus_assert (dbus_object_id_get_instance_bits (&tmp) ==
                0x7fffffff);

  dbus_object_id_set_as_integer (&tmp, 1);
  dbus_object_id_set_as_integer (&tmp2, 2);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == -1);
  dbus_object_id_set_as_integer (&tmp2, 0);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == 1);
  dbus_object_id_set_as_integer (&tmp2, 1);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == 0);
#endif

  /* Check comparison */
  tmp2 = tmp;
  
  dbus_object_id_set_server_bits (&tmp, 1);
  dbus_object_id_set_server_bits (&tmp2, 2);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == -1);
  dbus_object_id_set_server_bits (&tmp2, 0);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == 1);
  dbus_object_id_set_server_bits (&tmp2, 1);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == 0);

  dbus_object_id_set_client_bits (&tmp, 1);
  
  dbus_object_id_set_client_bits (&tmp2, 2);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == -1);
  dbus_object_id_set_client_bits (&tmp2, 0);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == 1);
  dbus_object_id_set_client_bits (&tmp2, 1);
  _dbus_assert (dbus_object_id_compare (&tmp, &tmp2) == 0);

  /* Check get/set again with high-limit numbers */  
  
  dbus_object_id_set_server_bits (&tmp, 0xf0f0);
  _dbus_assert (dbus_object_id_get_server_bits (&tmp) == 0xf0f0);

  dbus_object_id_set_client_bits (&tmp, 0xf00f);
  _dbus_assert (dbus_object_id_get_client_bits (&tmp) == 0xf00f);
  _dbus_assert (dbus_object_id_get_server_bits (&tmp) == 0xf0f0);

  dbus_object_id_set_is_server_bit (&tmp, TRUE);
  _dbus_assert (dbus_object_id_get_client_bits (&tmp) == 0xf00f);
  _dbus_assert (dbus_object_id_get_server_bits (&tmp) == 0xf0f0);
  _dbus_assert (dbus_object_id_get_is_server_bit (&tmp) == TRUE);

  dbus_object_id_set_instance_bits (&tmp, 0x7fffffff);
  _dbus_assert (dbus_object_id_get_client_bits (&tmp) == 0xf00f);
  _dbus_assert (dbus_object_id_get_server_bits (&tmp) == 0xf0f0);
  _dbus_assert (dbus_object_id_get_is_server_bit (&tmp) == TRUE);
  _dbus_assert (dbus_object_id_get_instance_bits (&tmp) == 0x7fffffff);
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
