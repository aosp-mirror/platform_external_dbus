/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-dict.h Dict object for key-value data.
 * 
 * Copyright (C) 2003  CodeFactory AB
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
#if !defined (DBUS_INSIDE_DBUS_H) && !defined (DBUS_COMPILATION)
#error "Only <dbus/dbus.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef DBUS_DICT_H
#define DBUS_DICT_H

#include <dbus/dbus-macros.h>
#include <dbus/dbus-types.h>

DBUS_BEGIN_DECLS;

typedef struct DBusDict DBusDict;

DBusDict *  dbus_dict_new            (void);
void        dbus_dict_ref            (DBusDict     *dict);
void        dbus_dict_unref          (DBusDict     *dict);
dbus_bool_t dbus_dict_contains       (DBusDict     *dict,
				      const char   *key);
dbus_bool_t dbus_dict_remove         (DBusDict     *dict,
				      const char   *key);
int         dbus_dict_get_value_type (DBusDict     *dict,
				      const char   *key);
dbus_bool_t dbus_dict_get_keys       (DBusDict     *dict,
				      char       ***keys,
				      int          *len);

dbus_bool_t dbus_dict_set_boolean       (DBusDict             *dict,
					 const char           *key,
					 dbus_bool_t           value);
dbus_bool_t dbus_dict_set_int32         (DBusDict             *dict,
					 const char           *key,
					 dbus_int32_t          value);
dbus_bool_t dbus_dict_set_uint32        (DBusDict             *dict,
					 const char           *key,
					 dbus_uint32_t         value);
dbus_bool_t dbus_dict_set_double        (DBusDict             *dict,
					 const char           *key,
					 double                value);
dbus_bool_t dbus_dict_set_string        (DBusDict             *dict,
					 const char           *key,
					 const char           *value);
dbus_bool_t dbus_dict_set_boolean_array (DBusDict             *dict,
					 const char           *key,
					 unsigned const char  *value,
					 int                   len);
dbus_bool_t dbus_dict_set_int32_array   (DBusDict             *dict,
					 const char           *key,
					 const dbus_int32_t   *value,
					 int                   len);
dbus_bool_t dbus_dict_set_uint32_array  (DBusDict             *dict,
					 const char           *key,
					 const dbus_uint32_t  *value,
					 int                   len);
dbus_bool_t dbus_dict_set_double_array  (DBusDict             *dict,
					 const char           *key,
					 const double         *value,
					 int                   len);
dbus_bool_t dbus_dict_set_byte_array    (DBusDict             *dict,
					 const char           *key,
					 unsigned const char  *value,
					 int                   len);
dbus_bool_t dbus_dict_set_string_array  (DBusDict             *dict,
					 const char           *key,
					 const char          **value,
					 int                   len);

dbus_bool_t dbus_dict_get_boolean       (DBusDict              *dict,
					 const char            *key,
					 dbus_bool_t           *value);
dbus_bool_t dbus_dict_get_int32         (DBusDict              *dict,
					 const char            *key,
					 dbus_int32_t          *value);
dbus_bool_t dbus_dict_get_uint32        (DBusDict              *dict,
					 const char            *key,
					 dbus_uint32_t         *value);
dbus_bool_t dbus_dict_get_double        (DBusDict              *dict,
					 const char            *key,
					 double                *value);
dbus_bool_t dbus_dict_get_string        (DBusDict              *dict,
					 const char            *key,
					 const char           **value);
dbus_bool_t dbus_dict_get_boolean_array (DBusDict              *dict,
					 const char            *key,
					 unsigned const char  **value,
					 int                   *len);
dbus_bool_t dbus_dict_get_int32_array   (DBusDict              *dict,
					 const char            *key,
					 const dbus_int32_t   **value,
					 int                   *len);
dbus_bool_t dbus_dict_get_uint32_array  (DBusDict              *dict,
					 const char            *key,
					 const dbus_uint32_t  **value,
					 int                   *len);
dbus_bool_t dbus_dict_get_double_array  (DBusDict              *dict,
					 const char            *key,
					 const double         **value,
					 int                   *len);
dbus_bool_t dbus_dict_get_byte_array    (DBusDict              *dict,
					 const char            *key,
					 unsigned const char  **value,
					 int                   *len);
dbus_bool_t dbus_dict_get_string_array  (DBusDict              *dict,
					 const char            *key,
					 const char          ***value,
					 int                   *len);


#endif /* DBUS_DICT_H */
