/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-string.h String utility class (internal to D-BUS implementation)
 * 
 * Copyright (C) 2002  Red Hat, Inc.
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

#ifndef DBUS_STRING_H
#define DBUS_STRING_H

#include <config.h>

#include <dbus/dbus-memory.h>
#include <dbus/dbus-types.h>

DBUS_BEGIN_DECLS;

typedef struct DBusString DBusString;

struct DBusString
{
  void *dummy1; /**< placeholder */
  int   dummy2; /**< placeholder */
  int   dummy3; /**< placeholder */
  int   dummy4; /**< placeholder */
  unsigned int dummy5 : 1; /** placeholder */
  unsigned int dummy6 : 1; /** placeholder */
  unsigned int dummy7 : 1; /** placeholder */
};

dbus_bool_t _dbus_string_init           (DBusString *str,
                                         int         max_length);
void        _dbus_string_init_const     (DBusString *str,
                                         const char *value);
void        _dbus_string_free           (DBusString *str);
void        _dbus_string_lock           (DBusString *str);

void        _dbus_string_get_data           (DBusString        *str,
                                             char             **data_return);
void        _dbus_string_get_const_data     (const DBusString  *str,
                                             const char       **data_return);
void        _dbus_string_get_data_len       (DBusString        *str,
                                             char             **data_return,
                                             int                start,
                                             int                len);
void        _dbus_string_get_const_data_len (const DBusString  *str,
                                             const char       **data_return,
                                             int                start,
                                             int                len);
dbus_bool_t _dbus_string_steal_data         (DBusString        *str,
                                             char             **data_return);
dbus_bool_t _dbus_string_steal_data_len     (DBusString        *str,
                                             char             **data_return,
                                             int                start,
                                             int                len);

int  _dbus_string_get_length         (const DBusString  *str);

dbus_bool_t _dbus_string_lengthen   (DBusString *str,
                                     int         additional_length);
void        _dbus_string_shorten    (DBusString *str,
                                     int         length_to_remove);
dbus_bool_t _dbus_string_set_length (DBusString *str,
                                     int         length);

dbus_bool_t _dbus_string_append         (DBusString    *str,
                                         const char    *buffer);
dbus_bool_t _dbus_string_append_len     (DBusString    *str,
                                         const char    *buffer,
                                         int            len);
dbus_bool_t _dbus_string_append_int     (DBusString    *str,
                                         long           value);
dbus_bool_t _dbus_string_append_double  (DBusString    *str,
                                         double         value);
dbus_bool_t _dbus_string_append_byte    (DBusString    *str,
                                         unsigned char  byte);
dbus_bool_t _dbus_string_append_unichar (DBusString    *str,
                                         dbus_unichar_t ch);


void        _dbus_string_delete     (DBusString       *str,
                                     int               start,
                                     int               len);
dbus_bool_t _dbus_string_move       (DBusString       *source,
                                     int               start,
                                     DBusString       *dest,
                                     int               insert_at);
dbus_bool_t _dbus_string_copy       (const DBusString *source,
                                     int               start,
                                     DBusString       *dest,
                                     int               insert_at);
dbus_bool_t _dbus_string_move_len   (DBusString       *source,
                                     int               start,
                                     int               len,
                                     DBusString       *dest,
                                     int               insert_at);
dbus_bool_t _dbus_string_copy_len   (const DBusString *source,
                                     int               start,
                                     int               len,
                                     DBusString       *dest,
                                     int               insert_at);


void       _dbus_string_get_unichar (const DBusString *str,
                                     int               start,
                                     dbus_unichar_t   *ch_return,
                                     int              *end_return);

dbus_bool_t _dbus_string_parse_int    (const DBusString *str,
                                       int               start,
                                       long             *value_return,
                                       int              *end_return);
dbus_bool_t _dbus_string_parse_double (const DBusString *str,
                                       int               start,
                                       double           *value,
                                       int              *end_return);

dbus_bool_t _dbus_string_find         (const DBusString *str,
                                       int               start,
                                       const char       *substr,
                                       int              *found);

dbus_bool_t _dbus_string_find_blank   (const DBusString *str,
                                       int               start,
                                       int              *found);

void        _dbus_string_skip_blank   (const DBusString *str,
                                       int               start,
                                       int              *end);

dbus_bool_t _dbus_string_equal        (const DBusString *a,
                                       const DBusString *b);

dbus_bool_t _dbus_string_equal_c_str  (const DBusString *a,
                                       const char       *c_str);

dbus_bool_t _dbus_string_base64_encode (const DBusString *source,
                                        int               start,
                                        DBusString       *dest,
                                        int               insert_at);
dbus_bool_t _dbus_string_base64_decode (const DBusString *source,
                                        int               start,
                                        DBusString       *dest,
                                        int               insert_at);

DBUS_END_DECLS;

#endif /* DBUS_STRING_H */
