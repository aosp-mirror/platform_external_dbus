/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-internals.c  random utility stuff (internal to D-BUS implementation)
 *
 * Copyright (C) 2002, 2003  Red Hat, Inc.
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
 * @def _DBUS_INT16_MIN
 *
 * Minimum value of type "int16"
 */
/**
 * @def _DBUS_INT16_MAX
 *
 * Maximum value of type "int16"
 */
/**
 * @def _DBUS_UINT16_MAX
 *
 * Maximum value of type "uint16"
 */

/**
 * @def _DBUS_INT32_MIN
 *
 * Minimum value of type "int32"
 */
/**
 * @def _DBUS_INT32_MAX
 *
 * Maximum value of type "int32"
 */
/**
 * @def _DBUS_UINT32_MAX
 *
 * Maximum value of type "uint32"
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
 * @def _DBUS_UINT_MAX
 *
 * Maximum value of type "uint"
 */

/**
 * @typedef DBusForeachFunction
 * 
 * Used to iterate over each item in a collection, such as
 * a DBusList.
 */

/**
 * @def _DBUS_LOCK_NAME
 *
 * Expands to name of a global lock variable.
 */

/**
 * @def _DBUS_DEFINE_GLOBAL_LOCK
 *
 * Defines a global lock variable with the given name.
 * The lock must be added to the list to initialize
 * in dbus_threads_init().
 */

/**
 * @def _DBUS_DECLARE_GLOBAL_LOCK
 *
 * Expands to declaration of a global lock defined
 * with _DBUS_DEFINE_GLOBAL_LOCK.
 * The lock must be added to the list to initialize
 * in dbus_threads_init().
 */

/**
 * @def _DBUS_LOCK
 *
 * Locks a global lock
 */

/**
 * @def _DBUS_UNLOCK
 *
 * Unlocks a global lock
 */

/**
 * Fixed "out of memory" error message, just to avoid
 * making up a different string every time and wasting
 * space.
 */
const char _dbus_no_memory_message[] = "Not enough memory";

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

#ifdef DBUS_ENABLE_VERBOSE_MODE

static dbus_bool_t verbose_initted = FALSE;

#define PTHREAD_IN_VERBOSE 0
#if PTHREAD_IN_VERBOSE
#include <pthread.h>
#endif

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
  static dbus_bool_t need_pid = TRUE;
  int len;
  
  /* things are written a bit oddly here so that
   * in the non-verbose case we just have the one
   * conditional and return immediately.
   */
  if (!verbose)
    return;
  
  if (!verbose_initted)
    {
      verbose = _dbus_getenv ("DBUS_VERBOSE") != NULL;
      verbose_initted = TRUE;
      if (!verbose)
        return;
    }

  /* Print out pid before the line */
  if (need_pid)
    {
#if PTHREAD_IN_VERBOSE
      fprintf (stderr, "%lu: 0x%lx: ", _dbus_getpid (), pthread_self ());
#else
      fprintf (stderr, "%lu: ", _dbus_getpid ());
#endif
    }
      

  /* Only print pid again if the next line is a new line */
  len = strlen (format);
  if (format[len-1] == '\n')
    need_pid = TRUE;
  else
    need_pid = FALSE;
  
  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);

  fflush (stderr);
}

/**
 * Reinitializes the verbose logging code, used
 * as a hack in dbus-spawn.c so that a child
 * process re-reads its pid
 *
 */
void
_dbus_verbose_reset_real (void)
{
  verbose_initted = FALSE;
}

#endif /* DBUS_ENABLE_VERBOSE_MODE */

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
  size_t len;
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
 * Duplicates a block of memory. Returns
 * #NULL on failure.
 *
 * @param mem memory to copy
 * @param n_bytes number of bytes to copy
 * @returns the copy
 */
void*
_dbus_memdup (const void  *mem,
              size_t       n_bytes)
{
  void *copy;

  copy = dbus_malloc (n_bytes);
  if (copy == NULL)
    return NULL;

  memcpy (copy, mem, n_bytes);
  
  return copy;
}

/**
 * Duplicates a string array. Result may be freed with
 * dbus_free_string_array(). Returns #NULL if memory allocation fails.
 * If the array to be duplicated is #NULL, returns #NULL.
 * 
 * @param array array to duplicate.
 * @returns newly-allocated copy.
 */
char**
_dbus_dup_string_array (const char **array)
{
  int len;
  int i;
  char **copy;
  
  if (array == NULL)
    return NULL;

  for (len = 0; array[len] != NULL; ++len)
    ;

  copy = dbus_new0 (char*, len + 1);
  if (copy == NULL)
    return NULL;

  i = 0;
  while (i < len)
    {
      copy[i] = _dbus_strdup (array[i]);
      if (copy[i] == NULL)
        {
          dbus_free_string_array (copy);
          return NULL;
        }

      ++i;
    }

  return copy;
}

/**
 * Checks whether a string array contains the given string.
 * 
 * @param array array to search.
 * @param str string to look for
 * @returns #TRUE if array contains string
 */
dbus_bool_t
_dbus_string_array_contains (const char **array,
                             const char  *str)
{
  int i;

  i = 0;
  while (array[i] != NULL)
    {
      if (strcmp (array[i], str) == 0)
        return TRUE;
      ++i;
    }

  return FALSE;
}

#ifdef DBUS_BUILD_TESTS
/**
 * Returns a string describing the given name.
 *
 * @param header_field the field to describe
 * @returns a constant string describing the field
 */
const char *
_dbus_header_field_to_string (int header_field)
{
  switch (header_field)
    {
    case DBUS_HEADER_FIELD_INVALID:
      return "invalid";
    case DBUS_HEADER_FIELD_PATH:
      return "path";
    case DBUS_HEADER_FIELD_INTERFACE:
      return "interface";
    case DBUS_HEADER_FIELD_MEMBER:
      return "member";
    case DBUS_HEADER_FIELD_ERROR_NAME:
      return "error-name";
    case DBUS_HEADER_FIELD_REPLY_SERIAL:
      return "reply-serial";
    case DBUS_HEADER_FIELD_DESTINATION:
      return "destination";
    case DBUS_HEADER_FIELD_SENDER:
      return "sender";
    case DBUS_HEADER_FIELD_SIGNATURE:
      return "signature";
    default:
      return "unknown";
    }
}
#endif /* DBUS_BUILD_TESTS */

#ifndef DBUS_DISABLE_CHECKS
/** String used in _dbus_return_if_fail macro */
const char _dbus_return_if_fail_warning_format[] =
"%lu: arguments to %s() were incorrect, assertion \"%s\" failed in file %s line %d.\n"
"This is normally a bug in some application using the D-BUS library.\n";
#endif

#ifndef DBUS_DISABLE_ASSERT
/**
 * Internals of _dbus_assert(); it's a function
 * rather than a macro with the inline code so
 * that the assertion failure blocks don't show up
 * in test suite coverage, and to shrink code size.
 *
 * @param condition TRUE if assertion succeeded
 * @param condition_text condition as a string
 * @param file file the assertion is in
 * @param line line the assertion is in
 * @param func function the assertion is in
 */
void
_dbus_real_assert (dbus_bool_t  condition,
                   const char  *condition_text,
                   const char  *file,
                   int          line,
                   const char  *func)
{
  if (_DBUS_UNLIKELY (!condition))
    {
      _dbus_warn ("%lu: assertion failed \"%s\" file \"%s\" line %d function %s\n",
                  _dbus_getpid (), condition_text, file, line, func);
      _dbus_abort ();
    }
}

/**
 * Internals of _dbus_assert_not_reached(); it's a function
 * rather than a macro with the inline code so
 * that the assertion failure blocks don't show up
 * in test suite coverage, and to shrink code size.
 *
 * @param explanation what was reached that shouldn't have been
 * @param file file the assertion is in
 * @param line line the assertion is in
 */
void
_dbus_real_assert_not_reached (const char *explanation,
                               const char *file,
                               int         line)
{
  _dbus_warn ("File \"%s\" line %d process %lu should not have been reached: %s\n",
              file, line, _dbus_getpid (), explanation);
  _dbus_abort ();
}
#endif /* DBUS_DISABLE_ASSERT */
  
#ifdef DBUS_BUILD_TESTS
static dbus_bool_t
run_failing_each_malloc (int                    n_mallocs,
                         const char            *description,
                         DBusTestMemoryFunction func,
                         void                  *data)
{
  n_mallocs += 10; /* fudge factor to ensure reallocs etc. are covered */
  
  while (n_mallocs >= 0)
    {      
      _dbus_set_fail_alloc_counter (n_mallocs);

      _dbus_verbose ("\n===\n%s: (will fail malloc %d with %d failures)\n===\n",
                     description, n_mallocs,
                     _dbus_get_fail_alloc_failures ());

      if (!(* func) (data))
        return FALSE;
      
      n_mallocs -= 1;
    }

  _dbus_set_fail_alloc_counter (_DBUS_INT_MAX);

  return TRUE;
}                        

/**
 * Tests how well the given function responds to out-of-memory
 * situations. Calls the function repeatedly, failing a different
 * call to malloc() each time. If the function ever returns #FALSE,
 * the test fails. The function should return #TRUE whenever something
 * valid (such as returning an error, or succeeding) occurs, and #FALSE
 * if it gets confused in some way.
 *
 * @param description description of the test used in verbose output
 * @param func function to call
 * @param data data to pass to function
 * @returns #TRUE if the function never returns FALSE
 */
dbus_bool_t
_dbus_test_oom_handling (const char             *description,
                         DBusTestMemoryFunction  func,
                         void                   *data)
{
  int approx_mallocs;
  const char *setting;
  int max_failures_to_try;
  int i;

  /* Run once to see about how many mallocs are involved */
  
  _dbus_set_fail_alloc_counter (_DBUS_INT_MAX);

  _dbus_verbose ("Running once to count mallocs\n");
  
  if (!(* func) (data))
    return FALSE;
  
  approx_mallocs = _DBUS_INT_MAX - _dbus_get_fail_alloc_counter ();

  _dbus_verbose ("\n=================\n%s: about %d mallocs total\n=================\n",
                 description, approx_mallocs);

  setting = _dbus_getenv ("DBUS_TEST_MALLOC_FAILURES");
  if (setting != NULL)
    {
      DBusString str;
      long v;
      _dbus_string_init_const (&str, setting);
      v = 4;
      if (!_dbus_string_parse_int (&str, 0, &v, NULL))
        _dbus_warn ("couldn't parse '%s' as integer\n", setting);
      max_failures_to_try = v;
    }
  else
    {
      max_failures_to_try = 4;
    }

  i = setting ? max_failures_to_try - 1 : 1;
  while (i < max_failures_to_try)
    {
      _dbus_set_fail_alloc_failures (i);
      if (!run_failing_each_malloc (approx_mallocs, description, func, data))
        return FALSE;
      ++i;
    }
  
  _dbus_verbose ("\n=================\n%s: all iterations passed\n=================\n",
                 description);

  return TRUE;
}
#endif /* DBUS_BUILD_TESTS */

/** @} */
