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

void         dbus_pack_int32   (dbus_int32_t         value,
				int                  byte_order,
				unsigned char       *data);
dbus_int32_t dbus_unpack_int32 (int                  byte_order,
				const unsigned char *data);

      
dbus_bool_t _dbus_marshal_double     (DBusString          *str,
				      int                  byte_order,
				      double               value);
dbus_bool_t _dbus_marshal_int32      (DBusString          *str,
				      int                  byte_order,
				      dbus_int32_t         value);
dbus_bool_t _dbus_marshal_uint32     (DBusString          *str,
				      int                  byte_order,
				      dbus_uint32_t        value);
dbus_bool_t _dbus_marshal_string     (DBusString          *str,
				      int                  byte_order,
				      const char          *value);
dbus_bool_t _dbus_marshal_byte_array (DBusString          *str,
				      int                  byte_order,
				      const unsigned char *value,
				      int                  len);

double        _dbus_demarshal_double (DBusString *str,
				      int         byte_order,
				      int         pos,
				      int        *new_pos);
dbus_int32_t  _dbus_demarshal_int32  (DBusString *str,
				      int         byte_order,
				      int         pos,
				      int        *new_pos);
dbus_uint32_t _dbus_demarshal_uint32 (DBusString *str,
				      int         byte_order,
				      int         pos,
				      int        *new_pos);
char *        _dbus_demarshal_string (DBusString *str,
				      int         byte_order,
				      int         pos,
				      int        *new_pos);

dbus_bool_t _dbus_marshal_get_field_end_pos (DBusString *str,
					     int 	 byte_order,
					     int         pos,
					     int        *end_pos);




#endif /* DBUS_PROTOCOL_H */
