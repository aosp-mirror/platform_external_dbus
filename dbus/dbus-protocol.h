/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-protocol.h  D-Bus protocol constants
 *
 * Copyright (C) 2002, 2003  CodeFactory AB
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
#define DBUS_MAJOR_PROTOCOL_VERSION 0

/* Data types */
#define DBUS_TYPE_INVALID       0
#define DBUS_TYPE_NIL           1
#define DBUS_TYPE_BYTE          2
#define DBUS_TYPE_BOOLEAN       3
#define DBUS_TYPE_INT32         4
#define DBUS_TYPE_UINT32        5
#define DBUS_TYPE_INT64         6
#define DBUS_TYPE_UINT64        7
#define DBUS_TYPE_DOUBLE        8
#define DBUS_TYPE_STRING        9
#define DBUS_TYPE_NAMED         10
#define DBUS_TYPE_ARRAY         11
#define DBUS_TYPE_DICT          12
#define DBUS_TYPE_OBJECT_PATH   13
  
#define DBUS_TYPE_LAST DBUS_TYPE_OBJECT_PATH

/* Max length in bytes of a service or interface or member name */
#define DBUS_MAXIMUM_NAME_LENGTH 256

/* Types of message */
#define DBUS_MESSAGE_TYPE_INVALID       0
#define DBUS_MESSAGE_TYPE_METHOD_CALL   1
#define DBUS_MESSAGE_TYPE_METHOD_RETURN 2
#define DBUS_MESSAGE_TYPE_ERROR         3
#define DBUS_MESSAGE_TYPE_SIGNAL        4
  
/* Header flags */
#define DBUS_HEADER_FLAG_NO_REPLY_EXPECTED 0x1
  
/* Header fields */
#define DBUS_HEADER_FIELD_PATH           "path"
#define DBUS_HEADER_FIELD_INTERFACE      "ifce"
#define DBUS_HEADER_FIELD_MEMBER         "mebr"
#define DBUS_HEADER_FIELD_ERROR_NAME     "ernm"
#define DBUS_HEADER_FIELD_SERVICE        "srvc"
#define DBUS_HEADER_FIELD_REPLY	         "rply"
#define DBUS_HEADER_FIELD_SENDER_SERVICE "sdrs"

/* Services */
#define DBUS_SERVICE_ORG_FREEDESKTOP_DBUS      "org.freedesktop.DBus"
#define DBUS_SERVICE_ORG_FREEDESKTOP_BROADCAST "org.freedesktop.Broadcast"

/* Paths */
#define DBUS_PATH_ORG_FREEDESKTOP_DBUS  "/org/freedesktop/DBus"
#define DBUS_PATH_ORG_FREEDESKTOP_LOCAL "/org/freedesktop/Local"
  
/* Service owner flags */
#define DBUS_SERVICE_FLAG_PROHIBIT_REPLACEMENT 0x1
#define DBUS_SERVICE_FLAG_REPLACE_EXISTING     0x2

/* Service replies */
#define DBUS_SERVICE_REPLY_PRIMARY_OWNER  0x1
#define DBUS_SERVICE_REPLY_IN_QUEUE       0x2
#define DBUS_SERVICE_REPLY_SERVICE_EXISTS 0x4
#define DBUS_SERVICE_REPLY_ALREADY_OWNER  0x8

/* Activation replies */
#define DBUS_ACTIVATION_REPLY_ACTIVATED      0x0
#define DBUS_ACTIVATION_REPLY_ALREADY_ACTIVE 0x1
  
/* Interfaces, these #define don't do much other than
 * catch typos at compile time
 */
#define DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS  "org.freedesktop.DBus"

/* This is a special interface whose methods can only be invoked
 * by the local implementation (messages from remote apps aren't
 * allowed to specify this interface).
 */
#define DBUS_INTERFACE_ORG_FREEDESKTOP_LOCAL "org.freedesktop.Local"
  
#ifdef __cplusplus
}
#endif

#endif /* DBUS_PROTOCOL_H */
