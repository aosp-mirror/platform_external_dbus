/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-marshal-validate.h  Validation routines for marshaled data
 *
 * Copyright (C) 2005  Red Hat, Inc.
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

#ifndef DBUS_MARSHAL_VALIDATE_H
#define DBUS_MARSHAL_VALIDATE_H

#include <config.h>
#include <dbus/dbus-marshal-basic.h>

#ifndef PACKAGE
#error "config.h not included here"
#endif

/**
 * This is primarily used in unit testing, so we can verify that each
 * invalid message is invalid for the expected reasons. Thus we really
 * want a distinct enum value for every codepath leaving the validator
 * functions. Enum values are specified manually for ease of debugging
 * (so you can see the enum value given a printf)
 */
typedef enum
{
  DBUS_VALID = 0,
  DBUS_INVALID_UNKNOWN_TYPECODE = 1,
  DBUS_INVALID_MISSING_ARRAY_ELEMENT_TYPE = 2,
  DBUS_INVALID_SIGNATURE_TOO_LONG = 3,
  DBUS_INVALID_EXCEEDED_MAXIMUM_ARRAY_RECURSION = 4,
  DBUS_INVALID_EXCEEDED_MAXIMUM_STRUCT_RECURSION = 5,
  DBUS_INVALID_STRUCT_ENDED_BUT_NOT_STARTED = 6,
  DBUS_INVALID_STRUCT_STARTED_BUT_NOT_ENDED = 7,
  DBUS_INVALID_STRUCT_HAS_NO_FIELDS = 8,
  DBUS_INVALID_ALIGNMENT_PADDING_NOT_NUL = 9,
  DBUS_INVALID_BOOLEAN_NOT_ZERO_OR_ONE = 10,
  DBUS_INVALID_NOT_ENOUGH_DATA = 11,
  DBUS_INVALID_TOO_MUCH_DATA = 12,
  DBUS_INVALID_BAD_BYTE_ORDER = 13,
  DBUS_INVALID_BAD_PROTOCOL_VERSION = 14,
  DBUS_INVALID_BAD_MESSAGE_TYPE = 15,
  DBUS_INVALID_BAD_SERIAL = 16,
  DBUS_INVALID_INSANE_FIELDS_ARRAY_LENGTH = 17,
  DBUS_INVALID_INSANE_BODY_LENGTH = 18,
  DBUS_INVALID_MESSAGE_TOO_LONG = 19,
  DBUS_INVALID_HEADER_FIELD_CODE = 20,
  DBUS_INVALID_HEADER_FIELD_HAS_WRONG_TYPE = 21,
  DBUS_INVALID_USES_LOCAL_INTERFACE = 22,
  DBUS_INVALID_USES_LOCAL_PATH = 23,
  DBUS_INVALID_HEADER_FIELD_APPEARS_TWICE = 24,
  DBUS_INVALID_BAD_DESTINATION = 25,
  DBUS_INVALID_BAD_INTERFACE = 26,
  DBUS_INVALID_BAD_MEMBER = 27,
  DBUS_INVALID_BAD_ERROR_NAME = 28,
  DBUS_INVALID_BAD_SENDER = 29,
  DBUS_INVALID_MISSING_PATH = 30,
  DBUS_INVALID_MISSING_INTERFACE = 31,
  DBUS_INVALID_MISSING_MEMBER = 32,
  DBUS_INVALID_MISSING_ERROR_NAME = 33,
  DBUS_INVALID_MISSING_REPLY_SERIAL = 34,
  DBUS_INVALID_STRING_LENGTH_OUT_OF_BOUNDS = 35,
  DBUS_INVALID_ARRAY_LENGTH_OUT_OF_BOUNDS = 36,
  DBUS_INVALID_ARRAY_LENGTH_EXCEEDS_MAXIMUM = 37,
  DBUS_INVALID_BAD_PATH = 38,
  DBUS_INVALID_SIGNATURE_LENGTH_OUT_OF_BOUNDS = 39,
  DBUS_INVALID_BAD_SIGNATURE = 40,
  DBUS_INVALID_BAD_UTF8_IN_STRING = 41,
  DBUS_INVALID_ARRAY_LENGTH_INCORRECT = 42,
  DBUS_INVALID_VARIANT_SIGNATURE_LENGTH_OUT_OF_BOUNDS = 43,
  DBUS_INVALID_VARIANT_SIGNATURE_BAD = 44,
  DBUS_INVALID_VARIANT_SIGNATURE_EMPTY = 45,
  DBUS_INVALID_VARIANT_SIGNATURE_SPECIFIES_MULTIPLE_VALUES = 46,
  DBUS_INVALID_VARIANT_SIGNATURE_MISSING_NUL = 47,
  DBUS_INVALID_STRING_MISSING_NUL = 48,
  DBUS_INVALID_SIGNATURE_MISSING_NUL = 49,
} DBusValidity;

DBusValidity _dbus_validate_signature_with_reason (const DBusString *type_str,
                                                   int               type_pos,
                                                   int               len);
DBusValidity _dbus_validate_body_with_reason      (const DBusString *expected_signature,
                                                   int               expected_signature_start,
                                                   int               byte_order,
                                                   int              *bytes_remaining,
                                                   const DBusString *value_str,
                                                   int               value_pos,
                                                   int               len);

dbus_bool_t _dbus_validate_path       (const DBusString *str,
                                       int               start,
                                       int               len);
dbus_bool_t _dbus_validate_interface  (const DBusString *str,
                                       int               start,
                                       int               len);
dbus_bool_t _dbus_validate_member     (const DBusString *str,
                                       int               start,
                                       int               len);
dbus_bool_t _dbus_validate_error_name (const DBusString *str,
                                       int               start,
                                       int               len);
dbus_bool_t _dbus_validate_service    (const DBusString *str,
                                       int               start,
                                       int               len);
dbus_bool_t _dbus_validate_signature  (const DBusString *str,
                                       int               start,
                                       int               len);

#ifdef DBUS_DISABLE_CHECKS

/* Be sure they don't exist, since we don't want to use them outside of checks
 * and so we want the compile failure.
 */
#define DECLARE_DBUS_NAME_CHECK(what)
#define DEFINE_DBUS_NAME_CHECK(what)

#else /* !DBUS_DISABLE_CHECKS */

/* A name check is used in _dbus_return_if_fail(), it's not suitable
 * for validating untrusted data. use _dbus_validate_##what for that.
 */
#define DECLARE_DBUS_NAME_CHECK(what) \
dbus_bool_t _dbus_check_is_valid_##what (const char *name)

#define DEFINE_DBUS_NAME_CHECK(what)                                    \
dbus_bool_t                                                             \
_dbus_check_is_valid_##what (const char *name)                          \
{                                                                       \
  DBusString str;                                                       \
                                                                        \
  if (name == NULL)                                                     \
    return FALSE;                                                       \
                                                                        \
  _dbus_string_init_const (&str, name);                                 \
  return _dbus_validate_##what (&str, 0,                                \
                                _dbus_string_get_length (&str));        \
}
#endif /* !DBUS_DISABLE_CHECKS */

DECLARE_DBUS_NAME_CHECK(path);
DECLARE_DBUS_NAME_CHECK(interface);
DECLARE_DBUS_NAME_CHECK(member);
DECLARE_DBUS_NAME_CHECK(error_name);
DECLARE_DBUS_NAME_CHECK(service);
DECLARE_DBUS_NAME_CHECK(signature);

#endif /* DBUS_MARSHAL_VALIDATE_H */
