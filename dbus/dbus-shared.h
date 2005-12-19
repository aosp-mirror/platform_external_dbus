/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-shared.h  Stuff used by both dbus/dbus.h low-level and C/C++ binding APIs
 *
 * Copyright (C) 2004 Red Hat, Inc.
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

#ifndef DBUS_SHARED_H
#define DBUS_SHARED_H

/* Don't include anything in here from anywhere else. It's
 * intended for use by any random library.
 */

#ifdef  __cplusplus
extern "C" {
#endif

/* Normally docs are in .c files, but there isn't a .c file for this. */
/**
 * @defgroup DBusShared Shared constants 
 * @ingroup  DBus
 *
 * Shared constants.
 *
 * @brief Stuff used by both dbus/dbus.h low-level and C/C++ binding APIs 
 * @{
 */


typedef enum
{
  DBUS_BUS_SESSION,    /**< The login session bus */
  DBUS_BUS_SYSTEM,     /**< The systemwide bus */
  DBUS_BUS_STARTER     /**< The bus that started us, if any */
} DBusBusType;

typedef enum
{
  DBUS_HANDLER_RESULT_HANDLED,         /**< Message has had its effect */ 
  DBUS_HANDLER_RESULT_NOT_YET_HANDLED, /**< Message has not had any effect */
  DBUS_HANDLER_RESULT_NEED_MEMORY      /**< Need more memory to return another result */
} DBusHandlerResult;

/* Services */
#define DBUS_SERVICE_DBUS      "org.freedesktop.DBus"

/* Paths */
#define DBUS_PATH_DBUS  "/org/freedesktop/DBus"
#define DBUS_PATH_LOCAL "/org/freedesktop/DBus/Local"

/* Interfaces, these #define don't do much other than
 * catch typos at compile time
 */
#define DBUS_INTERFACE_DBUS           "org.freedesktop.DBus"
#define DBUS_INTERFACE_INTROSPECTABLE "org.freedesktop.DBus.Introspectable"
#define DBUS_INTERFACE_PROPERTIES     "org.freedesktop.DBus.Properties"
#define DBUS_INTERFACE_PEER           "org.freedesktop.DBus.Peer"

/** This is a special interface whose methods can only be invoked
 * by the local implementation (messages from remote apps aren't
 * allowed to specify this interface).
 */
#define DBUS_INTERFACE_LOCAL "org.freedesktop.DBus.Local"

/* Owner flags */
#define DBUS_NAME_FLAG_ALLOW_REPLACEMENT 0x1 /**< Allow another service to become the primary owner if requested */
#define DBUS_NAME_FLAG_REPLACE_EXISTING  0x2 /**< Request to replace the current primary owner */
#define DBUS_NAME_FLAG_DO_NOT_QUEUE      0x4 /**< If we can not become the primary owner do not place us in the queue */

/* Replies to request for a name */
#define DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER  1 /**< Service has become the primary owner of the requested name */
#define DBUS_REQUEST_NAME_REPLY_IN_QUEUE       2 /**< Service could not become the primary owner and has been placed in the queue */
#define DBUS_REQUEST_NAME_REPLY_EXISTS         3 /**< Service is already in the queue */
#define DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER  4 /**< Service is already the primary owner */

/* Replies to releasing a name */
#define DBUS_RELEASE_NAME_REPLY_RELEASED        1 /**< Service was released from the given name */
#define DBUS_RELEASE_NAME_REPLY_NON_EXISTENT    2 /**< The given name does not exist on the bus */
#define DBUS_RELEASE_NAME_REPLY_NOT_OWNER       3 /**< Service is not an owner of the given name */

/* Replies to service starts */
#define DBUS_START_REPLY_SUCCESS         1 /**< service was auto started */
#define DBUS_START_REPLY_ALREADY_RUNNING 2 /**< service was already running */

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* DBUS_SHARED_H */
