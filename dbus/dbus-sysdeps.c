/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-sysdeps.c Wrappers around system/libc features (internal to D-BUS implementation)
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

#include "dbus-sysdeps.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

/**
 * @addtogroup DBusInternalsUtils
 * @{
 */
/**
 * Aborts the program with SIGABRT (dumping core).
 */
void
_dbus_abort (void)
{
  abort ();
  _exit (1); /* in case someone manages to ignore SIGABRT */
}

/**
 * Wrapper for getenv().
 *
 * @param varname name of environment variable
 * @returns value of environment variable or #NULL if unset
 */
const char*
_dbus_getenv (const char *varname)
{  
  return getenv (varname);
}

/** @} */

/**
 * @addtogroup DBusString
 *
 * @{
 */
/**
 * Appends an integer to a DBusString.
 * 
 * @param str the string
 * @param value the integer value
 * @returns #FALSE if not enough memory or other failure.
 */
dbus_bool_t
_dbus_string_append_int (DBusString *str,
                         long        value)
{
  /* this calculation is from comp.lang.c faq */
#define MAX_LONG_LEN ((sizeof (long) * 8 + 2) / 3 + 1)  /* +1 for '-' */
  int orig_len;
  int i;
  char *buf;
  
  orig_len = _dbus_string_get_length (str);

  if (!_dbus_string_lengthen (str, MAX_LONG_LEN))
    return FALSE;

  _dbus_string_get_data_len (str, &buf, orig_len, MAX_LONG_LEN);

  snprintf (buf, MAX_LONG_LEN, "%ld", value);

  i = 0;
  while (*buf)
    {
      ++buf;
      ++i;
    }
  
  _dbus_string_shorten (str, MAX_LONG_LEN - i);
  
  return TRUE;
}

/**
 * Appends a double to a DBusString.
 * 
 * @param str the string
 * @param value the floating point value
 * @returns #FALSE if not enough memory or other failure.
 */
dbus_bool_t
_dbus_string_append_double (DBusString *str,
                            double      value)
{
#define MAX_DOUBLE_LEN 64 /* this is completely made up :-/ */
  int orig_len;
  char *buf;
  int i;
  
  orig_len = _dbus_string_get_length (str);

  if (!_dbus_string_lengthen (str, MAX_DOUBLE_LEN))
    return FALSE;

  _dbus_string_get_data_len (str, &buf, orig_len, MAX_DOUBLE_LEN);

  snprintf (buf, MAX_LONG_LEN, "%g", value);

  i = 0;
  while (*buf)
    {
      ++buf;
      ++i;
    }
  
  _dbus_string_shorten (str, MAX_DOUBLE_LEN - i);
  
  return TRUE;
}

/**
 * Parses an integer contained in a DBusString. Either return parameter
 * may be #NULL if you aren't interested in it. The integer is parsed
 * and stored in value_return. Return parameters are not initialized
 * if the function returns #FALSE.
 *
 * @param str the string
 * @param start the byte index of the start of the integer
 * @param value_return return location of the integer value or #NULL
 * @param end_return return location of the end of the integer, or #NULL
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_string_parse_int (const DBusString *str,
                        int               start,
                        long             *value_return,
                        int              *end_return)
{
  long v;
  const char *p;
  char *end;

  _dbus_string_get_const_data_len (str, &p, start,
                                   _dbus_string_get_length (str) - start);

  end = NULL;
  errno = 0;
  v = strtol (p, &end, 0);
  if (end == NULL || end == p || errno != 0)
    return FALSE;

  if (value_return)
    *value_return = v;
  if (end_return)
    *end_return = (end - p);

  return TRUE;
}

/**
 * Parses a floating point number contained in a DBusString. Either
 * return parameter may be #NULL if you aren't interested in it. The
 * integer is parsed and stored in value_return. Return parameters are
 * not initialized if the function returns #FALSE.
 *
 * @todo this function is currently locale-dependent. Should
 * ask alexl to relicense g_ascii_strtod() code and put that in
 * here instead, so it's locale-independent.
 *
 * @param str the string
 * @param start the byte index of the start of the float
 * @param value_return return location of the float value or #NULL
 * @param end_return return location of the end of the float, or #NULL
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_string_parse_double (const DBusString *str,
                           int               start,
                           double           *value_return,
                           int              *end_return)
{
  double v;
  const char *p;
  char *end;

  _dbus_warn ("_dbus_string_parse_double() needs to be made locale-independent\n");
  
  _dbus_string_get_const_data_len (str, &p, start,
                                   _dbus_string_get_length (str) - start);

  end = NULL;
  errno = 0;
  v = strtod (p, &end);
  if (end == NULL || end == p || errno != 0)
    return FALSE;

  if (value_return)
    *value_return = v;
  if (end_return)
    *end_return = (end - p);

  return TRUE;
}

/** @} end of DBusString */
