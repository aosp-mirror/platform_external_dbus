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
#include <stdio.h>
#include <stdarg.h>

/**
 * @defgroup DBusInternals D-BUS internal implementation details
 * @brief Documentation useful when developing or debugging D-BUS itself.
 * 
 */

/**
 * @defgroup DBusInternalsUtils Utilities
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

/** @} */
