/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-errors.c Error reporting
 *
 * Copyright (C) 2002  Red Hat Inc.
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
#include "dbus-errors.h"

/**
 * @defgroup DBusErrors Error reporting
 * @ingroup  DBus
 * @brief Error reporting
 *
 * Types and functions related to reporting errors.
 *
 *
 * In essence D-BUS error reporting works as follows:
 *
 * @code
 * DBusResultCode result = DBUS_RESULT_SUCCESS;
 * dbus_some_function (arg1, arg2, &result);
 * if (result != DBUS_RESULT_SUCCESS)
 *   printf ("an error occurred\n");
 * @endcode
 * 
 * @{
 */

/**
 * Set a result code at a result code location,
 * if code_address is not #NULL.
 *
 * @param code_address place to store the result code.
 * @param code the result code itself.
 */
void
dbus_set_result (DBusResultCode *code_address,
                 DBusResultCode  code)
{
  if (code_address)
    *code_address = code;
}

/**
 * Returns a string describing the given result code.
 *
 * @param code the result code to describe.
 * @returns a constant string describing the code.
 */
const char*
dbus_result_to_string (DBusResultCode code)
{
  /* This is a switch to the compiler will complain if we
   * aren't handling some codes
   */
  switch (code)
    {
    case DBUS_RESULT_SUCCESS:
      return "Success";
    case DBUS_RESULT_FAILED:
      return "Unknown error";
    case DBUS_RESULT_NO_MEMORY:
      return "Not enough memory available";
    case DBUS_RESULT_IO_ERROR:
      return "Error reading or writing data";
    case DBUS_RESULT_BAD_ADDRESS:
      return "Could not parse address";
    case DBUS_RESULT_NOT_SUPPORTED:
      return "Feature not supported";
    case DBUS_RESULT_LIMITS_EXCEEDED:
      return "Resource limits exceeded";
    case DBUS_RESULT_ACCESS_DENIED:
      return "Permission denied";
    case DBUS_RESULT_AUTH_FAILED:
      return "Could not authenticate to server";
    case DBUS_RESULT_NO_SERVER:
      return "No server";
    case DBUS_RESULT_TIMEOUT:
      return "Connection timed out";
    case DBUS_RESULT_NO_NETWORK:
      return "Network unavailable";
    case DBUS_RESULT_ADDRESS_IN_USE:
      return "Address already in use";
    case DBUS_RESULT_DISCONNECTED:
      return "Disconnected.";
    case DBUS_RESULT_INVALID_FIELDS:
      return "Invalid fields.";
    case DBUS_RESULT_NO_REPLY:
      return "Did not get a reply message.";
    case DBUS_RESULT_FILE_NOT_FOUND:
      return "File doesn't exist.";
      
      /* no default, it would break our compiler warnings */
    }

  return "Invalid error code";
}

/** @} */
