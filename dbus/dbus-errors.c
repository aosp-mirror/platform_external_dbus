/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-errors.c Error reporting
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
#include "dbus-errors.h"
#include "dbus-internals.h"
#include <stdarg.h>
#include <stdio.h>

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
 * @todo add docs with DBusError
 *
 * @todo add dbus_error_is_set() to check
 * whether an error is set.
 * 
 * @{
 */

typedef struct
{
  const char *name; /**< error name */
  char *message; /**< error message */

  unsigned int const_message : 1; /** Message is not owned by DBusError */

  unsigned int dummy2 : 1; /**< placeholder */
  unsigned int dummy3 : 1; /**< placeholder */
  unsigned int dummy4 : 1; /**< placeholder */
  unsigned int dummy5 : 1; /**< placeholder */

  void *padding1; /**< placeholder */
  
} DBusRealError;

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
    case DBUS_RESULT_INVALID_ARGS:
      return "Invalid argumemts.";
    case DBUS_RESULT_NO_REPLY:
      return "Did not get a reply message.";
    case DBUS_RESULT_FILE_NOT_FOUND:
      return "File doesn't exist.";
      
      /* no default, it would break our compiler warnings */
    }

  return "Invalid error code";
}

/**
 * Initializes a DBusError structure.
 * 
 * @todo calling dbus_error_init() in here is no good,
 * for the same reason a GError* has to be set to NULL
 * before you pass it in.
 *
 * @param error the DBusError.
 */
void
dbus_error_init (DBusError *error)
{
  DBusRealError *real;

  _dbus_assert (error != NULL);

  _dbus_assert (sizeof (DBusError) == sizeof (DBusRealError));

  real = (DBusRealError *)error;
  
  real->name = NULL;  
  real->message = NULL;

  real->const_message = TRUE;
}

/**
 * Frees an error created by dbus_error_init().
 *
 * @param error memory where the error is stored.
 */
void
dbus_error_free (DBusError *error)
{
  DBusRealError *real;

  real = (DBusRealError *)error;

  if (!real->const_message)
    dbus_free (real->message);
}

/**
 * Assigns an error name and message to a DBusError.
 * Does nothing if error is #NULL.
 *
 * @param error the error.
 * @param name the error name (not copied!!!)
 * @param message the error message (not copied!!!)
 */
void
dbus_set_error_const (DBusError  *error,
		      const char *name,
		      const char *message)
{
  DBusRealError *real;

  if (error == NULL)
    return;

  /* it's a bug to pile up errors */
  _dbus_assert (error->name == NULL);
  _dbus_assert (error->message == NULL);
  
  real = (DBusRealError *)error;
  
  real->name = name;
  real->message = (char *)message;
  real->const_message = TRUE;
}

/**
 * Assigns an error name and message to a DBusError.
 * Does nothing if error is #NULL.
 *
 * If no memory can be allocated for the error message, 
 * an out-of-memory error message will be set instead.
 *
 * @todo stdio.h shouldn't be included in this file,
 * should write _dbus_string_append_printf instead
 * 
 * @param error the error.
 * @param name the error name (not copied!!!)
 * @param format printf-style format string.
 */
void
dbus_set_error (DBusError  *error,
		const char *name,
		const char *format,
		...)
{
  DBusRealError *real;
  va_list args;
  int message_length;
  char *message;
  char c;

  if (error == NULL)
    return;

  /* it's a bug to pile up errors */
  _dbus_assert (error->name == NULL);
  _dbus_assert (error->message == NULL);
  
  va_start (args, format);
  /* Measure the message length */
  message_length = vsnprintf (&c, 1, format, args) + 1;
  va_end (args);
  
  message = dbus_malloc (message_length);
  
  if (!message)
    {
      dbus_set_error_const (error, DBUS_ERROR_NO_MEMORY,
			    "Failed to allocate memory for error message.");
      return;
    }
  
  va_start (args, format);  
  vsprintf (message, format, args);  
  va_end (args);

  real = (DBusRealError *)error;
  
  real->name = name;
  real->message = message;
  real->const_message = FALSE;
}

/** @} */
