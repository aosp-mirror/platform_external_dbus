/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-marshal.h  Marshalling routines
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

#ifndef DBUS_MARSHAL_H
#define DBUS_MARSHAL_H

#include <config.h>
#include <dbus/dbus-protocol.h>
#include <dbus/dbus-types.h>
#include <dbus/dbus-string.h>

#ifndef PACKAGE
#error "config.h not included here"
#endif

#ifdef WORDS_BIGENDIAN
#define DBUS_COMPILER_BYTE_ORDER DBUS_BIG_ENDIAN
#else
#define DBUS_COMPILER_BYTE_ORDER DBUS_LITTLE_ENDIAN
#endif

#define DBUS_UINT32_SWAP_LE_BE_CONSTANT(val)	((dbus_uint32_t) ( \
    (((dbus_uint32_t) (val) & (dbus_uint32_t) 0x000000ffU) << 24) |  \
    (((dbus_uint32_t) (val) & (dbus_uint32_t) 0x0000ff00U) <<  8) |  \
    (((dbus_uint32_t) (val) & (dbus_uint32_t) 0x00ff0000U) >>  8) |  \
    (((dbus_uint32_t) (val) & (dbus_uint32_t) 0xff000000U) >> 24)))

#define DBUS_UINT32_SWAP_LE_BE(val) (DBUS_UINT32_SWAP_LE_BE_CONSTANT (val))
#define DBUS_INT32_SWAP_LE_BE(val)  ((dbus_int32_t)DBUS_UINT32_SWAP_LE_BE_CONSTANT (val))

#ifdef WORDS_BIGENDIAN
#define DBUS_INT32_TO_BE(val)	((dbus_int32_t) (val))
#define DBUS_UINT32_TO_BE(val)	((dbus_uint32_t) (val))
#define DBUS_INT32_TO_LE(val)	(DBUS_INT32_SWAP_LE_BE (val))
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

void          _dbus_pack_int32    (dbus_int32_t         value,
                                   int                  byte_order,
                                   unsigned char       *data);
dbus_int32_t  _dbus_unpack_int32  (int                  byte_order,
                                   const unsigned char *data);
void          _dbus_pack_uint32   (dbus_uint32_t        value,
                                   int                  byte_order,
                                   unsigned char       *data);
dbus_uint32_t _dbus_unpack_uint32 (int                  byte_order,
                                   const unsigned char *data);

void        _dbus_marshal_set_int32  (DBusString       *str,
                                      int               byte_order,
                                      int               offset,
                                      dbus_int32_t      value);
void        _dbus_marshal_set_uint32 (DBusString       *str,
                                      int               byte_order,
                                      int               offset,
                                      dbus_uint32_t     value);
dbus_bool_t _dbus_marshal_set_string (DBusString       *str,
                                      int               byte_order,
                                      int               offset,
                                      const DBusString *value,
				      int               len);

dbus_bool_t   _dbus_marshal_int32          (DBusString            *str,
					    int                    byte_order,
					    dbus_int32_t           value);
dbus_bool_t   _dbus_marshal_uint32         (DBusString            *str,
					    int                    byte_order,
					    dbus_uint32_t          value);
dbus_bool_t   _dbus_marshal_double         (DBusString            *str,
					    int                    byte_order,
					    double                 value);
dbus_bool_t   _dbus_marshal_string         (DBusString            *str,
					    int                    byte_order,
					    const char            *value);
dbus_bool_t   _dbus_marshal_byte_array     (DBusString            *str,
					    int                    byte_order,
					    const unsigned char   *value,
					    int                    len);
dbus_bool_t   _dbus_marshal_int32_array    (DBusString            *str,
					    int                    byte_order,
					    const dbus_int32_t    *value,
					    int                    len);
dbus_bool_t   _dbus_marshal_uint32_array   (DBusString            *str,
					    int                    byte_order,
					    const dbus_uint32_t   *value,
					    int                    len);
dbus_bool_t   _dbus_marshal_double_array   (DBusString            *str,
					    int                    byte_order,
					    const double          *value,
					    int                    len);
dbus_bool_t   _dbus_marshal_string_array   (DBusString            *str,
					    int                    byte_order,
					    const char           **value,
					    int                    len);
double        _dbus_demarshal_double       (const DBusString      *str,
					    int                    byte_order,
					    int                    pos,
					    int                   *new_pos);
dbus_int32_t  _dbus_demarshal_int32        (const DBusString      *str,
					    int                    byte_order,
					    int                    pos,
					    int                   *new_pos);
dbus_uint32_t _dbus_demarshal_uint32       (const DBusString      *str,
					    int                    byte_order,
					    int                    pos,
					    int                   *new_pos);
char *        _dbus_demarshal_string       (const DBusString      *str,
					    int                    byte_order,
					    int                    pos,
					    int                   *new_pos);
dbus_bool_t   _dbus_demarshal_byte_array   (const DBusString      *str,
					    int                    byte_order,
					    int                    pos,
					    int                   *new_pos,
					    unsigned char        **array,
					    int                   *array_len);
dbus_bool_t   _dbus_demarshal_int32_array  (const DBusString      *str,
					    int                    byte_order,
					    int                    pos,
					    int                   *new_pos,
					    dbus_int32_t         **array,
					    int                   *array_len);
dbus_bool_t   _dbus_demarshal_uint32_array (const DBusString      *str,
					    int                    byte_order,
					    int                    pos,
					    int                   *new_pos,
					    dbus_uint32_t        **array,
					    int                   *array_len);
dbus_bool_t   _dbus_demarshal_double_array (const DBusString      *str,
					    int                    byte_order,
					    int                    pos,
					    int                   *new_pos,
					    double               **array,
					    int                   *array_len);
dbus_bool_t   _dbus_demarshal_string_array (const DBusString      *str,
					    int                    byte_order,
					    int                    pos,
					    int                   *new_pos,
					    char                ***array,
					    int                   *array_len);




dbus_bool_t _dbus_marshal_get_arg_end_pos (const DBusString *str,
                                           int               byte_order,
					   int               type,
                                           int               pos,
                                           int              *end_pos);
dbus_bool_t _dbus_marshal_validate_type   (const DBusString *str,
                                           int               pos,
					   int              *type,
                                           int              *end_pos);
dbus_bool_t _dbus_marshal_validate_arg    (const DBusString *str,
                                           int               depth,
                                           int               byte_order,
					   int               type,
                                           int               pos,
                                           int              *end_pos);


#endif /* DBUS_PROTOCOL_H */
