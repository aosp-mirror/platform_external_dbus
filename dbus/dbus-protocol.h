/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-protocol.h  D-Bus protocol constants
 *
 * Copyright (C) 2002, 2003  CodeFactory AB
 * Copyright (C) 2004, 2005 Red Hat, Inc.
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

#ifndef DBUS_PROTOCOL_H
#define DBUS_PROTOCOL_H

/* Don't include anything in here from anywhere else. It's
 * intended for use by any random library.
 */

#ifdef  __cplusplus
extern "C" {
#endif

/* Message byte order */
#define DBUS_LITTLE_ENDIAN ('l')  /* LSB first */
#define DBUS_BIG_ENDIAN    ('B')  /* MSB first */

/* Protocol version */
#define DBUS_MAJOR_PROTOCOL_VERSION 1

/* Never a legitimate type */
#define DBUS_TYPE_INVALID       ((int) '\0')
#define DBUS_TYPE_INVALID_AS_STRING        "\0"

/* Primitive types */
#define DBUS_TYPE_BYTE          ((int) 'y')
#define DBUS_TYPE_BYTE_AS_STRING           "y"
#define DBUS_TYPE_BOOLEAN       ((int) 'b')
#define DBUS_TYPE_BOOLEAN_AS_STRING        "b"
#define DBUS_TYPE_INT16         ((int) 'n')
#define DBUS_TYPE_INT16_AS_STRING          "n"
#define DBUS_TYPE_UINT16        ((int) 'q')
#define DBUS_TYPE_UINT16_AS_STRING         "q"
#define DBUS_TYPE_INT32         ((int) 'i')
#define DBUS_TYPE_INT32_AS_STRING          "i"
#define DBUS_TYPE_UINT32        ((int) 'u')
#define DBUS_TYPE_UINT32_AS_STRING         "u"
#define DBUS_TYPE_INT64         ((int) 'x')
#define DBUS_TYPE_INT64_AS_STRING          "x"
#define DBUS_TYPE_UINT64        ((int) 't')
#define DBUS_TYPE_UINT64_AS_STRING         "t"

#define DBUS_TYPE_DOUBLE        ((int) 'd')
#define DBUS_TYPE_DOUBLE_AS_STRING         "d"
#define DBUS_TYPE_STRING        ((int) 's')
#define DBUS_TYPE_STRING_AS_STRING         "s"
#define DBUS_TYPE_OBJECT_PATH   ((int) 'o')
#define DBUS_TYPE_OBJECT_PATH_AS_STRING    "o"
#define DBUS_TYPE_SIGNATURE     ((int) 'g')
#define DBUS_TYPE_SIGNATURE_AS_STRING      "g"

/* Compound types */
#define DBUS_TYPE_ARRAY         ((int) 'a')
#define DBUS_TYPE_ARRAY_AS_STRING          "a"
#define DBUS_TYPE_VARIANT       ((int) 'v')
#define DBUS_TYPE_VARIANT_AS_STRING        "v"

/* STRUCT and DICT_ENTRY are sort of special since their codes can't
 * appear in a type string, instead
 * DBUS_STRUCT_BEGIN_CHAR/DBUS_DICT_ENTRY_BEGIN_CHAR have to appear
 */
#define DBUS_TYPE_STRUCT        ((int) 'r')
#define DBUS_TYPE_STRUCT_AS_STRING         "r"
#define DBUS_TYPE_DICT_ENTRY    ((int) 'e')
#define DBUS_TYPE_DICT_ENTRY_AS_STRING     "e"

/* Does not count INVALID */
#define DBUS_NUMBER_OF_TYPES    (16)

/* characters other than typecodes that appear in type signatures */
#define DBUS_STRUCT_BEGIN_CHAR   ((int) '(')
#define DBUS_STRUCT_BEGIN_CHAR_AS_STRING   "("
#define DBUS_STRUCT_END_CHAR     ((int) ')')
#define DBUS_STRUCT_END_CHAR_AS_STRING     ")"
#define DBUS_DICT_ENTRY_BEGIN_CHAR   ((int) '{')
#define DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING   "{"
#define DBUS_DICT_ENTRY_END_CHAR     ((int) '}')
#define DBUS_DICT_ENTRY_END_CHAR_AS_STRING     "}"

/* Max length in bytes of a bus name, interface, or member (not object
 * path, paths are unlimited). This is limited because lots of stuff
 * is O(n) in this number, plus it would be obnoxious to type in a
 * paragraph-long method name so most likely something like that would
 * be an exploit.
 */
#define DBUS_MAXIMUM_NAME_LENGTH 255

/* This one is 255 so it fits in a byte */
#define DBUS_MAXIMUM_SIGNATURE_LENGTH 255

/* Max length of a match rule string; to keep people from hosing the
 * daemon with some huge rule
 */
#define DBUS_MAXIMUM_MATCH_RULE_LENGTH 1024

/* Max arg number you can match on in a match rule, e.g.
 * arg0='hello' is OK, arg3489720987='hello' is not
 */
#define DBUS_MAXIMUM_MATCH_RULE_ARG_NUMBER 63
  
/* Max length of a marshaled array in bytes (64M, 2^26) We use signed
 * int for lengths so must be INT_MAX or less.  We need something a
 * bit smaller than INT_MAX because the array is inside a message with
 * header info, etc.  so an INT_MAX array wouldn't allow the message
 * overhead.  The 64M number is an attempt at a larger number than
 * we'd reasonably ever use, but small enough that your bus would chew
 * through it fairly quickly without locking up forever. If you have
 * data that's likely to be larger than this, you should probably be
 * sending it in multiple incremental messages anyhow.
 */
#define DBUS_MAXIMUM_ARRAY_LENGTH (67108864)
/* Number of bits you need in an unsigned to store the max array size */
#define DBUS_MAXIMUM_ARRAY_LENGTH_BITS 26

/* The maximum total message size including header and body; similar
 * rationale to max array size.
 */
#define DBUS_MAXIMUM_MESSAGE_LENGTH (DBUS_MAXIMUM_ARRAY_LENGTH * 2)
/* Number of bits you need in an unsigned to store the max message size */
#define DBUS_MAXIMUM_MESSAGE_LENGTH_BITS 27

/* Depth of recursion in the type tree. This is automatically limited
 * to DBUS_MAXIMUM_SIGNATURE_LENGTH since you could only have an array
 * of array of array of ... that fit in the max signature.  But that's
 * probably a bit too large.
 */
#define DBUS_MAXIMUM_TYPE_RECURSION_DEPTH 32

/* Types of message */
#define DBUS_MESSAGE_TYPE_INVALID       0
#define DBUS_MESSAGE_TYPE_METHOD_CALL   1
#define DBUS_MESSAGE_TYPE_METHOD_RETURN 2
#define DBUS_MESSAGE_TYPE_ERROR         3
#define DBUS_MESSAGE_TYPE_SIGNAL        4

/* Header flags */
#define DBUS_HEADER_FLAG_NO_REPLY_EXPECTED 0x1
#define DBUS_HEADER_FLAG_NO_AUTO_START     0x2

/* Header fields */
#define DBUS_HEADER_FIELD_INVALID        0
#define DBUS_HEADER_FIELD_PATH           1
#define DBUS_HEADER_FIELD_INTERFACE      2
#define DBUS_HEADER_FIELD_MEMBER         3
#define DBUS_HEADER_FIELD_ERROR_NAME     4
#define DBUS_HEADER_FIELD_REPLY_SERIAL   5
#define DBUS_HEADER_FIELD_DESTINATION    6
#define DBUS_HEADER_FIELD_SENDER         7
#define DBUS_HEADER_FIELD_SIGNATURE      8

#define DBUS_HEADER_FIELD_LAST DBUS_HEADER_FIELD_SIGNATURE

/* Header format is defined as a signature:
 *   byte                            byte order
 *   byte                            message type ID
 *   byte                            flags
 *   byte                            protocol version
 *   uint32                          body length
 *   uint32                          serial
 *   array of struct (byte,variant)  (field name, value)
 *
 * The length of the header can be computed as the
 * fixed size of the initial data, plus the length of
 * the array at the end, plus padding to an 8-boundary.
 */
#define DBUS_HEADER_SIGNATURE                   \
     DBUS_TYPE_BYTE_AS_STRING                   \
     DBUS_TYPE_BYTE_AS_STRING                   \
     DBUS_TYPE_BYTE_AS_STRING                   \
     DBUS_TYPE_BYTE_AS_STRING                   \
     DBUS_TYPE_UINT32_AS_STRING                 \
     DBUS_TYPE_UINT32_AS_STRING                 \
     DBUS_TYPE_ARRAY_AS_STRING                  \
     DBUS_STRUCT_BEGIN_CHAR_AS_STRING           \
     DBUS_TYPE_BYTE_AS_STRING                   \
     DBUS_TYPE_VARIANT_AS_STRING                \
     DBUS_STRUCT_END_CHAR_AS_STRING


/**
 * The smallest header size that can occur.  (It won't be valid due to
 * missing required header fields.) This is 4 bytes, two uint32, an
 * array length. This isn't any kind of resource limit, just the
 * necessary/logical outcome of the header signature.
 */
#define DBUS_MINIMUM_HEADER_SIZE 16

/* Errors */
/* WARNING these get autoconverted to an enum in dbus-glib.h. Thus,
 * if you change the order it breaks the ABI. Keep them in order.
 * Also, don't change the formatting since that will break the sed
 * script.
 */
#define DBUS_ERROR_FAILED                     "org.freedesktop.DBus.Error.Failed"
#define DBUS_ERROR_NO_MEMORY                  "org.freedesktop.DBus.Error.NoMemory"
#define DBUS_ERROR_SERVICE_UNKNOWN            "org.freedesktop.DBus.Error.ServiceUnknown"
#define DBUS_ERROR_NAME_HAS_NO_OWNER          "org.freedesktop.DBus.Error.NameHasNoOwner"
#define DBUS_ERROR_NO_REPLY                   "org.freedesktop.DBus.Error.NoReply"
#define DBUS_ERROR_IO_ERROR                   "org.freedesktop.DBus.Error.IOError"
#define DBUS_ERROR_BAD_ADDRESS                "org.freedesktop.DBus.Error.BadAddress"
#define DBUS_ERROR_NOT_SUPPORTED              "org.freedesktop.DBus.Error.NotSupported"
#define DBUS_ERROR_LIMITS_EXCEEDED            "org.freedesktop.DBus.Error.LimitsExceeded"
#define DBUS_ERROR_ACCESS_DENIED              "org.freedesktop.DBus.Error.AccessDenied"
#define DBUS_ERROR_AUTH_FAILED                "org.freedesktop.DBus.Error.AuthFailed"
#define DBUS_ERROR_NO_SERVER                  "org.freedesktop.DBus.Error.NoServer"
#define DBUS_ERROR_TIMEOUT                    "org.freedesktop.DBus.Error.Timeout"
#define DBUS_ERROR_NO_NETWORK                 "org.freedesktop.DBus.Error.NoNetwork"
#define DBUS_ERROR_ADDRESS_IN_USE             "org.freedesktop.DBus.Error.AddressInUse"
#define DBUS_ERROR_DISCONNECTED               "org.freedesktop.DBus.Error.Disconnected"
#define DBUS_ERROR_INVALID_ARGS               "org.freedesktop.DBus.Error.InvalidArgs"
#define DBUS_ERROR_FILE_NOT_FOUND             "org.freedesktop.DBus.Error.FileNotFound"
#define DBUS_ERROR_UNKNOWN_METHOD             "org.freedesktop.DBus.Error.UnknownMethod"
#define DBUS_ERROR_TIMED_OUT                  "org.freedesktop.DBus.Error.TimedOut"
#define DBUS_ERROR_MATCH_RULE_NOT_FOUND       "org.freedesktop.DBus.Error.MatchRuleNotFound"
#define DBUS_ERROR_MATCH_RULE_INVALID         "org.freedesktop.DBus.Error.MatchRuleInvalid"
#define DBUS_ERROR_SPAWN_EXEC_FAILED          "org.freedesktop.DBus.Error.Spawn.ExecFailed"
#define DBUS_ERROR_SPAWN_FORK_FAILED          "org.freedesktop.DBus.Error.Spawn.ForkFailed"
#define DBUS_ERROR_SPAWN_CHILD_EXITED         "org.freedesktop.DBus.Error.Spawn.ChildExited"
#define DBUS_ERROR_SPAWN_CHILD_SIGNALED       "org.freedesktop.DBus.Error.Spawn.ChildSignaled"
#define DBUS_ERROR_SPAWN_FAILED               "org.freedesktop.DBus.Error.Spawn.Failed"
#define DBUS_ERROR_UNIX_PROCESS_ID_UNKNOWN    "org.freedesktop.DBus.Error.UnixProcessIdUnknown"
#define DBUS_ERROR_INVALID_SIGNATURE          "org.freedesktop.DBus.Error.InvalidSignature"
#define DBUS_ERROR_SELINUX_SECURITY_CONTEXT_UNKNOWN    "org.freedesktop.DBus.Error.SELinuxSecurityContextUnknown"

#define DBUS_INTROSPECT_1_0_XML_NAMESPACE         "http://www.freedesktop.org/standards/dbus"
#define DBUS_INTROSPECT_1_0_XML_PUBLIC_IDENTIFIER "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
#define DBUS_INTROSPECT_1_0_XML_SYSTEM_IDENTIFIER "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd"
#define DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE "<!DOCTYPE node PUBLIC \""DBUS_INTROSPECT_1_0_XML_PUBLIC_IDENTIFIER"\"\n\""DBUS_INTROSPECT_1_0_XML_SYSTEM_IDENTIFIER"\">\n"


#ifdef __cplusplus
}
#endif

#endif /* DBUS_PROTOCOL_H */
