/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-internals.c  random utility stuff (internal to D-BUS implementation)
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
#include "dbus-internals.h"
#include "dbus-protocol.h"
#include "dbus-test.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

/**
 * @defgroup DBusInternals D-BUS internal implementation details
 * @brief Documentation useful when developing or debugging D-BUS itself.
 * 
 */

/**
 * @defgroup DBusInternalsUtils Utilities and portability
 * @ingroup DBusInternals
 * @brief Utility functions (_dbus_assert(), _dbus_warn(), etc.)
 * @{
 */

/**
 * @def _dbus_assert
 *
 * Aborts with an error message if the condition is false.
 * 
 * @param condition condition which must be true.
 */

/**
 * @def _dbus_assert_not_reached
 *
 * Aborts with an error message if called.
 * The given explanation will be printed.
 * 
 * @param explanation explanation of what happened if the code was reached.
 */

/**
 * @def _DBUS_N_ELEMENTS
 *
 * Computes the number of elements in a fixed-size array using
 * sizeof().
 *
 * @param array the array to count elements in.
 */

/**
 * @def _DBUS_POINTER_TO_INT
 *
 * Safely casts a void* to an integer; should only be used on void*
 * that actually contain integers, for example one created with
 * _DBUS_INT_TO_POINTER.  Only guaranteed to preserve 32 bits.
 * (i.e. it's used to store 32-bit ints in pointers, but
 * can't be used to store 64-bit pointers in ints.)
 *
 * @param pointer pointer to extract an integer from.
 */
/**
 * @def _DBUS_INT_TO_POINTER
 *
 * Safely stuffs an integer into a pointer, to be extracted later with
 * _DBUS_POINTER_TO_INT. Only guaranteed to preserve 32 bits.
 *
 * @param integer the integer to stuff into a pointer.
 */
/**
 * @def _DBUS_ZERO
 *
 * Sets all bits in an object to zero.
 *
 * @param object the object to be zeroed.
 */
/**
 * @def _DBUS_INT_MIN
 *
 * Minimum value of type "int"
 */
/**
 * @def _DBUS_INT_MAX
 *
 * Maximum value of type "int"
 */
/**
 * @def _DBUS_MAX_SUN_PATH_LENGTH
 *
 * Maximum length of the path to a UNIX domain socket,
 * sockaddr_un::sun_path member. POSIX requires that all systems
 * support at least 100 bytes here, including the nul termination.
 * We use 99 for the max value to allow for the nul.
 *
 * We could probably also do sizeof (addr.sun_path)
 * but this way we are the same on all platforms
 * which is probably a good idea.
 */

/**
 * @typedef DBusForeachFunction
 * 
 * Used to iterate over each item in a collection, such as
 * a DBusList.
 */

/**
 * Prints a warning message to stderr.
 *
 * @param format printf-style format string.
 */
void
_dbus_warn (const char *format,
            ...)
{
  /* FIXME not portable enough? */
  va_list args;

  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);
}

/**
 * Prints a warning message to stderr
 * if the user has enabled verbose mode.
 * This is the real function implementation,
 * use _dbus_verbose() macro in code.
 *
 * @param format printf-style format string.
 */
void
_dbus_verbose_real (const char *format,
                    ...)
{
  va_list args;
  static dbus_bool_t verbose = TRUE;
  static dbus_bool_t initted = FALSE;

  /* things are written a bit oddly here so that
   * in the non-verbose case we just have the one
   * conditional and return immediately.
   */
  if (!verbose)
    return;
  
  if (!initted)
    {
      verbose = _dbus_getenv ("DBUS_VERBOSE") != NULL;
      initted = TRUE;
      if (!verbose)
        return;
    }
  
  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);
}

/**
 * A wrapper around strerror() because some platforms
 * may be lame and not have strerror().
 *
 * @param error_number errno.
 * @returns error description.
 */
const char*
_dbus_strerror (int error_number)
{
  return strerror (error_number);
}

/**
 * Converts a UNIX errno into a DBusResultCode.
 *
 * @param error_number the errno.
 * @returns the result code.
 */
DBusResultCode
_dbus_result_from_errno (int error_number)
{
  switch (error_number)
    {
    case 0:
      return DBUS_RESULT_SUCCESS;
      
#ifdef EPROTONOSUPPORT
    case EPROTONOSUPPORT:
      return DBUS_RESULT_NOT_SUPPORTED;
#endif
#ifdef EAFNOSUPPORT
    case EAFNOSUPPORT:
      return DBUS_RESULT_NOT_SUPPORTED;
#endif
#ifdef ENFILE
    case ENFILE:
      return DBUS_RESULT_LIMITS_EXCEEDED; /* kernel out of memory */
#endif
#ifdef EMFILE
    case EMFILE:
      return DBUS_RESULT_LIMITS_EXCEEDED;
#endif
#ifdef EACCES
    case EACCES:
      return DBUS_RESULT_ACCESS_DENIED;
#endif
#ifdef EPERM
    case EPERM:
      return DBUS_RESULT_ACCESS_DENIED;
#endif
#ifdef ENOBUFS
    case ENOBUFS:
      return DBUS_RESULT_NO_MEMORY;
#endif
#ifdef ENOMEM
    case ENOMEM:
      return DBUS_RESULT_NO_MEMORY;
#endif
#ifdef EINVAL
    case EINVAL:
      return DBUS_RESULT_FAILED;
#endif
#ifdef EBADF
    case EBADF:
      return DBUS_RESULT_FAILED;
#endif
#ifdef EFAULT
    case EFAULT:
      return DBUS_RESULT_FAILED;
#endif
#ifdef ENOTSOCK
    case ENOTSOCK:
      return DBUS_RESULT_FAILED;
#endif
#ifdef EISCONN
    case EISCONN:
      return DBUS_RESULT_FAILED;
#endif
#ifdef ECONNREFUSED
    case ECONNREFUSED:
      return DBUS_RESULT_NO_SERVER;
#endif
#ifdef ETIMEDOUT
    case ETIMEDOUT:
      return DBUS_RESULT_TIMEOUT;
#endif
#ifdef ENETUNREACH
    case ENETUNREACH:
      return DBUS_RESULT_NO_NETWORK;
#endif
#ifdef EADDRINUSE
    case EADDRINUSE:
      return DBUS_RESULT_ADDRESS_IN_USE;
#endif      
    }

  return DBUS_RESULT_FAILED;
}

/**
 * Duplicates a string. Result must be freed with
 * dbus_free(). Returns #NULL if memory allocation fails.
 * If the string to be duplicated is #NULL, returns #NULL.
 * 
 * @param str string to duplicate.
 * @returns newly-allocated copy.
 */
char*
_dbus_strdup (const char *str)
{
  int len;
  char *copy;
  
  if (str == NULL)
    return NULL;
  
  len = strlen (str);

  copy = dbus_malloc (len + 1);
  if (copy == NULL)
    return NULL;

  memcpy (copy, str, len + 1);
  
  return copy;
}

/**
 * Sets a file descriptor to be nonblocking.
 *
 * @param fd the file descriptor.
 * @param result address of result code.
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_set_fd_nonblocking (int             fd,
                          DBusResultCode *result)
{
  int val;

  val = fcntl (fd, F_GETFL, 0);
  if (val < 0)
    {
      dbus_set_result (result, _dbus_result_from_errno (errno));
      _dbus_verbose ("Failed to get flags for fd %d: %s\n", fd,
                     _dbus_strerror (errno));
      return FALSE;
    }

  if (fcntl (fd, F_SETFL, val | O_NONBLOCK) < 0)
    {
      dbus_set_result (result, _dbus_result_from_errno (errno));      
      _dbus_verbose ("Failed to set fd %d nonblocking: %s\n",
                     fd, _dbus_strerror (errno));

      return FALSE;
    }

  return TRUE;
}

/**
 * Returns a string describing the given type.
 *
 * @param type the type to describe
 * @returns a constant string describing the type
 */
const char *
_dbus_type_to_string (int type)
{
  switch (type)
    {
    case DBUS_TYPE_INVALID:
      return "invalid";
    case DBUS_TYPE_INT32:
      return "int32";
    case DBUS_TYPE_UINT32:
      return "uint32";
    case DBUS_TYPE_DOUBLE:
      return "double";
    case DBUS_TYPE_STRING:
      return "string";
    case DBUS_TYPE_BYTE_ARRAY:
      return "byte array";
    default:
      return "unknown";
    }
}

/** @} */
