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
#define DBUS_TYPE_DOUBLE        6
#define DBUS_TYPE_STRING        7
#define DBUS_TYPE_NAMED         8
#define DBUS_TYPE_ARRAY         9
#define DBUS_TYPE_DICT          10

#define DBUS_TYPE_LAST DBUS_TYPE_DICT

/* Max length in bytes of a service or message name */
#define DBUS_MAXIMUM_NAME_LENGTH 256

/* Header flags */
#define DBUS_HEADER_FLAG_ERROR 0x1
  
/* Header fields */
#define DBUS_HEADER_FIELD_NAME    "name"
#define DBUS_HEADER_FIELD_SERVICE "srvc"
#define DBUS_HEADER_FIELD_REPLY	  "rply"
#define DBUS_HEADER_FIELD_SENDER  "sndr"

/* Services */
#define DBUS_SERVICE_DBUS      "org.freedesktop.DBus"
#define DBUS_SERVICE_BROADCAST "org.freedesktop.DBus.Broadcast"

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
  
/* Messages */
#define DBUS_MESSAGE_ACTIVATE_SERVICE      "org.freedesktop.DBus.ActivateService"  
#define DBUS_MESSAGE_SERVICE_EXISTS        "org.freedesktop.DBus.ServiceExists"
#define DBUS_MESSAGE_HELLO                 "org.freedesktop.DBus.Hello"
#define DBUS_MESSAGE_LIST_SERVICES         "org.freedesktop.DBus.ListServices"
#define DBUS_MESSAGE_ACQUIRE_SERVICE       "org.freedesktop.DBus.AcquireService"
#define DBUS_MESSAGE_SERVICE_ACQUIRED      "org.freedesktop.DBus.ServiceAcquired"
#define DBUS_MESSAGE_SERVICE_CREATED       "org.freedesktop.DBus.ServiceCreated"
#define DBUS_MESSAGE_SERVICE_DELETED       "org.freedesktop.DBus.ServiceDeleted"
#define DBUS_MESSAGE_SERVICE_LOST          "org.freedesktop.DBus.ServiceLost"


/* This namespace is reserved for locally-synthesized messages, you can't
 * send messages that have this namespace.
 */
#define DBUS_NAMESPACE_LOCAL_MESSAGE       "org.freedesktop.Local."
#define DBUS_MESSAGE_LOCAL_DISCONNECT      DBUS_NAMESPACE_LOCAL_MESSAGE"Disconnect"
  
#ifdef __cplusplus
}
#endif

#endif /* DBUS_PROTOCOL_H */
