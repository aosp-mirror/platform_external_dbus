/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-errors.h Error reporting
 *
 * Copyright (C) 2002  Red Hat Inc.
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

#ifndef DBUS_ERROR_H
#define DBUS_ERROR_H

#include <dbus/dbus-macros.h>
#include <dbus/dbus-types.h>

DBUS_BEGIN_DECLS;

typedef struct DBusError DBusError;

struct DBusError
{
  const char *name;    /**< error name */
  const char *message; /**< error message */

  unsigned int dummy1 : 1; /**< placeholder */
  unsigned int dummy2 : 1; /**< placeholder */
};

typedef enum
{
  DBUS_RESULT_SUCCESS,         /**< Operation was successful. */
  DBUS_RESULT_FAILED,          /**< Operation failed for unspecified reason. */
  DBUS_RESULT_NO_MEMORY,       /**< Operation failed for lack of memory. */
  DBUS_RESULT_IO_ERROR,        /**< Operation failed because of an IO error,
                                *   typically the other end closed the
                                *   connection.
                                */
  DBUS_RESULT_BAD_ADDRESS,     /**< Address was bad, could not be parsed. */
  DBUS_RESULT_NOT_SUPPORTED,   /**< Feature is not supported. */
  DBUS_RESULT_LIMITS_EXCEEDED, /**< Some kernel resource limit exceeded. */
  DBUS_RESULT_ACCESS_DENIED,   /**< Some sort of permissions/security problem. */
  DBUS_RESULT_AUTH_FAILED,     /**< Could not authenticate. */
  DBUS_RESULT_NO_SERVER,       /**< No one listening on the other end. */
  DBUS_RESULT_TIMEOUT,         /**< Timed out trying to connect. */
  DBUS_RESULT_NO_NETWORK,      /**< Can't find the network */
  DBUS_RESULT_ADDRESS_IN_USE,  /**< Someone's already using the address */
  DBUS_RESULT_DISCONNECTED,    /**< No more connection. */
  DBUS_RESULT_INVALID_ARGS,    /**< One or more invalid arguments encountered. */
  DBUS_RESULT_NO_REPLY,        /**< Did not get a reply message. */
  DBUS_RESULT_FILE_NOT_FOUND   /**< File doesn't exist */
} DBusResultCode;

void        dbus_error_init      (DBusError  *error);
void        dbus_error_free      (DBusError  *error);
dbus_bool_t dbus_set_error       (DBusError  *error,
				  const char *name,
				  const char *message,
				  ...);
void        dbus_set_error_const (DBusError  *error,
				  const char *name,
				  const char *message); 
				   
void        dbus_set_result       (DBusResultCode *code_address,
                                   DBusResultCode  code);
const char* dbus_result_to_string (DBusResultCode  code);

DBUS_END_DECLS;

#endif /* DBUS_ERROR_H */
