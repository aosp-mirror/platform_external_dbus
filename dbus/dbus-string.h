/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-string.h String utility class (internal to D-BUS implementation)
 * 
 * Copyright (C) 2002, 2003 Red Hat, Inc.
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

#ifndef DBUS_STRING_H
#define DBUS_STRING_H

#include <config.h>

#include <dbus/dbus-memory.h>
#include <dbus/dbus-types.h>
#include <dbus/dbus-sysdeps.h>

#include <stdarg.h>

DBUS_BEGIN_DECLS

/**
 * DBusString object
 */
struct DBusString
{
  void *dummy1; /**< placeholder */
  int   dummy2; /**< placeholder */
  int   dummy3; /**< placeholder */
  int   dummy4; /**< placeholder */
  unsigned int dummy5 : 1; /**< placeholder */
  unsigned int dummy6 : 1; /**< placeholder */
  unsigned int dummy7 : 1; /**< placeholder */
  unsigned int dummy8 : 3; /**< placeholder */
};

#ifdef DBUS_DISABLE_ASSERT
/* Some simple inlining hacks; the current linker is not smart enough
 * to inline non-exported symbols across files in the library.
 * Note that these break type safety (due to the casts)
 */
#define _dbus_string_get_length(s) (((DBusString*)(s))->dummy2)
#define _dbus_string_get_byte(s, i) (((const unsigned char*)(((DBusString*)(s))->dummy1))[(i)])
#define _dbus_string_get_const_data(s) ((const char*)(((DBusString*)(s))->dummy1))
#endif

dbus_bool_t   _dbus_string_init                  (DBusString        *str);
void          _dbus_string_init_const            (DBusString        *str,
                                                  const char        *value);
void          _dbus_string_init_const_len        (DBusString        *str,
                                                  const char        *value,
                                                  int                len);
dbus_bool_t   _dbus_string_init_preallocated     (DBusString        *str,
                                                  int                allocate_size);
void          _dbus_string_free                  (DBusString        *str);
void          _dbus_string_lock                  (DBusString        *str);
char*         _dbus_string_get_data              (DBusString        *str);
#ifndef _dbus_string_get_const_data
const char*   _dbus_string_get_const_data        (const DBusString  *str);
#endif /* _dbus_string_get_const_data */
char*         _dbus_string_get_data_len          (DBusString        *str,
                                                  int                start,
                                                  int                len);
const char*   _dbus_string_get_const_data_len    (const DBusString  *str,
                                                  int                start,
                                                  int                len);
void          _dbus_string_set_byte              (DBusString        *str,
                                                  int                i,
                                                  unsigned char      byte);
#ifndef _dbus_string_get_byte
unsigned char _dbus_string_get_byte              (const DBusString  *str,
                                                  int                start);
#endif /* _dbus_string_get_byte */
dbus_bool_t   _dbus_string_insert_bytes          (DBusString        *str,
                                                  int                i,
						  int                n_bytes,
                                                  unsigned char      byte);
dbus_bool_t   _dbus_string_insert_byte           (DBusString        *str,
                                                  int                i,
                                                  unsigned char      byte);
dbus_bool_t   _dbus_string_steal_data            (DBusString        *str,
                                                  char             **data_return);
dbus_bool_t   _dbus_string_steal_data_len        (DBusString        *str,
                                                  char             **data_return,
                                                  int                start,
                                                  int                len);
dbus_bool_t   _dbus_string_copy_data             (const DBusString  *str,
                                                  char             **data_return);
dbus_bool_t   _dbus_string_copy_data_len         (const DBusString  *str,
                                                  char             **data_return,
                                                  int                start,
                                                  int                len);
void          _dbus_string_copy_to_buffer        (const DBusString  *str,
                                                  char              *buffer,
						  int                len);
#ifndef _dbus_string_get_length
int           _dbus_string_get_length            (const DBusString  *str);
#endif /* !_dbus_string_get_length */

dbus_bool_t   _dbus_string_lengthen              (DBusString        *str,
                                                  int                additional_length);
void          _dbus_string_shorten               (DBusString        *str,
                                                  int                length_to_remove);
dbus_bool_t   _dbus_string_set_length            (DBusString        *str,
                                                  int                length);
dbus_bool_t   _dbus_string_align_length          (DBusString        *str,
                                                  int                alignment);
dbus_bool_t   _dbus_string_alloc_space           (DBusString        *str,
                                                  int                extra_bytes);
dbus_bool_t   _dbus_string_append                (DBusString        *str,
                                                  const char        *buffer);
dbus_bool_t   _dbus_string_append_len            (DBusString        *str,
                                                  const char        *buffer,
                                                  int                len);
dbus_bool_t   _dbus_string_append_int            (DBusString        *str,
                                                  long               value);
dbus_bool_t   _dbus_string_append_uint           (DBusString        *str,
                                                  unsigned long      value);
dbus_bool_t   _dbus_string_append_double         (DBusString        *str,
                                                  double             value);
dbus_bool_t   _dbus_string_append_byte           (DBusString        *str,
                                                  unsigned char      byte);
dbus_bool_t   _dbus_string_append_unichar        (DBusString        *str,
                                                  dbus_unichar_t     ch);
dbus_bool_t   _dbus_string_append_4_aligned      (DBusString        *str,
                                                  const unsigned char octets[4]);
dbus_bool_t   _dbus_string_append_8_aligned      (DBusString        *str,
                                                  const unsigned char octets[8]);
dbus_bool_t   _dbus_string_append_printf         (DBusString        *str,
                                                  const char        *format,
                                                  ...) _DBUS_GNUC_PRINTF (2, 3);
dbus_bool_t   _dbus_string_append_printf_valist  (DBusString        *str,
                                                  const char        *format,
                                                  va_list            args);
dbus_bool_t   _dbus_string_insert_4_aligned      (DBusString        *str,
                                                  int                insert_at,
                                                  const unsigned char octets[4]);
dbus_bool_t   _dbus_string_insert_8_aligned      (DBusString        *str,
                                                  int                insert_at,
                                                  const unsigned char octets[8]);
dbus_bool_t   _dbus_string_insert_alignment      (DBusString        *str,
                                                  int               *insert_at,
                                                  int                alignment);
void          _dbus_string_delete                (DBusString        *str,
                                                  int                start,
                                                  int                len);
dbus_bool_t   _dbus_string_move                  (DBusString        *source,
                                                  int                start,
                                                  DBusString        *dest,
                                                  int                insert_at);
dbus_bool_t   _dbus_string_copy                  (const DBusString  *source,
                                                  int                start,
                                                  DBusString        *dest,
                                                  int                insert_at);
dbus_bool_t   _dbus_string_move_len              (DBusString        *source,
                                                  int                start,
                                                  int                len,
                                                  DBusString        *dest,
                                                  int                insert_at);
dbus_bool_t   _dbus_string_copy_len              (const DBusString  *source,
                                                  int                start,
                                                  int                len,
                                                  DBusString        *dest,
                                                  int                insert_at);
dbus_bool_t   _dbus_string_replace_len           (const DBusString  *source,
                                                  int                start,
                                                  int                len,
                                                  DBusString        *dest,
                                                  int                replace_at,
                                                  int                replace_len);
void          _dbus_string_get_unichar           (const DBusString  *str,
                                                  int                start,
                                                  dbus_unichar_t    *ch_return,
                                                  int               *end_return);
dbus_bool_t   _dbus_string_parse_int             (const DBusString  *str,
                                                  int                start,
                                                  long              *value_return,
                                                  int               *end_return);
dbus_bool_t   _dbus_string_parse_uint            (const DBusString  *str,
                                                  int                start,
                                                  unsigned long     *value_return,
                                                  int               *end_return);
dbus_bool_t   _dbus_string_parse_double          (const DBusString  *str,
                                                  int                start,
                                                  double            *value,
                                                  int               *end_return);
dbus_bool_t   _dbus_string_parse_basic_type      (const DBusString  *str,
						  char               type,
                                                  int                start,
                                                  void              *value,
                                                  int               *end_return);
dbus_bool_t   _dbus_string_find                  (const DBusString  *str,
                                                  int                start,
                                                  const char        *substr,
                                                  int               *found);
dbus_bool_t   _dbus_string_find_to               (const DBusString  *str,
                                                  int                start,
                                                  int                end,
                                                  const char        *substr,
                                                  int               *found);
dbus_bool_t   _dbus_string_find_byte_backward    (const DBusString  *str,
                                                  int                start,
                                                  unsigned char      byte,
                                                  int               *found);
dbus_bool_t   _dbus_string_find_blank            (const DBusString  *str,
                                                  int                start,
                                                  int               *found);
void          _dbus_string_skip_blank            (const DBusString  *str,
                                                  int                start,
                                                  int               *end);
void          _dbus_string_skip_white            (const DBusString  *str,
                                                  int                start,
                                                  int               *end);
dbus_bool_t   _dbus_string_equal                 (const DBusString  *a,
                                                  const DBusString  *b);
dbus_bool_t   _dbus_string_equal_c_str           (const DBusString  *a,
                                                  const char        *c_str);
dbus_bool_t   _dbus_string_equal_len             (const DBusString  *a,
                                                  const DBusString  *b,
                                                  int                len);
dbus_bool_t   _dbus_string_equal_substring       (const DBusString  *a,
                                                  int                a_start,
                                                  int                a_len,
                                                  const DBusString  *b,
                                                  int                b_start);
dbus_bool_t   _dbus_string_starts_with_c_str     (const DBusString  *a,
                                                  const char        *c_str);
dbus_bool_t   _dbus_string_ends_with_c_str       (const DBusString  *a,
                                                  const char        *c_str);
dbus_bool_t   _dbus_string_pop_line              (DBusString        *source,
                                                  DBusString        *dest);
void          _dbus_string_delete_first_word     (DBusString        *str);
void          _dbus_string_delete_leading_blanks (DBusString        *str);
dbus_bool_t   _dbus_string_hex_encode            (const DBusString  *source,
                                                  int                start,
                                                  DBusString        *dest,
                                                  int                insert_at);
dbus_bool_t   _dbus_string_hex_decode            (const DBusString  *source,
                                                  int                start,
						  int               *end_return,
                                                  DBusString        *dest,
                                                  int                insert_at);
dbus_bool_t   _dbus_string_validate_ascii        (const DBusString  *str,
                                                  int                start,
                                                  int                len);
dbus_bool_t   _dbus_string_validate_utf8         (const DBusString  *str,
                                                  int                start,
                                                  int                len);
dbus_bool_t   _dbus_string_validate_nul          (const DBusString  *str,
                                                  int                start,
                                                  int                len);
dbus_bool_t   _dbus_string_validate_path         (const DBusString  *str,
                                                  int                start,
                                                  int                len);
dbus_bool_t   _dbus_string_validate_interface    (const DBusString  *str,
                                                  int                start,
                                                  int                len);
dbus_bool_t   _dbus_string_validate_member       (const DBusString  *str,
                                                  int                start,
                                                  int                len);
dbus_bool_t   _dbus_string_validate_error_name   (const DBusString  *str,
                                                  int                start,
                                                  int                len);
dbus_bool_t   _dbus_string_validate_service      (const DBusString  *str,
                                                  int                start,
                                                  int                len);
dbus_bool_t   _dbus_string_validate_signature    (const DBusString  *str,
                                                  int                start,
                                                  int                len);
void          _dbus_string_zero                  (DBusString        *str);


DBUS_END_DECLS

#endif /* DBUS_STRING_H */
