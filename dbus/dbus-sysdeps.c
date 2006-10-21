/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-sysdeps.c Wrappers around system/libc features shared between UNIX and Windows (internal to D-Bus implementation)
 * 
 * Copyright (C) 2002, 2003, 2006  Red Hat, Inc.
 * Copyright (C) 2003 CodeFactory AB
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
#include "dbus-sysdeps.h"
#include "dbus-threads.h"
#include "dbus-protocol.h"
#include "dbus-string.h"

/* NOTE: If you include any unix/windows-specific headers here, you are probably doing something
 * wrong and should be putting some code in dbus-sysdeps-unix.c or dbus-sysdeps-win.c.
 *
 * These are the standard ANSI C headers...
 */
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* This is UNIX-specific (on windows it's just in stdlib.h I believe)
 * but OK since the same stuff does exist on Windows in stdlib.h
 * and covered by a configure check.
 */
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

_DBUS_DEFINE_GLOBAL_LOCK (win_fds);
_DBUS_DEFINE_GLOBAL_LOCK (sid_atom_cache);

/**
 * @defgroup DBusSysdeps Internal system-dependent API
 * @ingroup DBusInternals
 * @brief Internal system-dependent API available on UNIX and Windows
 *
 * The system-dependent API has a dual purpose. First, it encapsulates
 * all usage of operating system APIs for ease of auditing and to
 * avoid cluttering the rest of the code with bizarre OS quirks and
 * headers. Second, it abstracts different operating system APIs for
 * portability.
 * 
 * @{
 */

/**
 * Aborts the program with SIGABRT (dumping core).
 */
void
_dbus_abort (void)
{
  const char *s;
  
  _dbus_print_backtrace ();
  
  s = _dbus_getenv ("DBUS_BLOCK_ON_ABORT");
  if (s && *s)
    {
      /* don't use _dbus_warn here since it can _dbus_abort() */
      fprintf (stderr, "  Process %lu sleeping for gdb attach\n", (unsigned long) _dbus_getpid());
      _dbus_sleep_milliseconds (1000 * 180);
    }
  
  abort ();
  _dbus_exit (1); /* in case someone manages to ignore SIGABRT ? */
}

/**
 * Wrapper for setenv(). If the value is #NULL, unsets
 * the environment variable.
 *
 * There is an unfixable memleak in that it is unsafe to
 * free memory malloced for use with setenv. This is because
 * we can not rely on internal implementation details of
 * the underlying libc library.
 *
 * @param varname name of environment variable
 * @param value value of environment variable
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_setenv (const char *varname,
              const char *value)
{
  _dbus_assert (varname != NULL);
  
  if (value == NULL)
    {
#ifdef HAVE_UNSETENV
      unsetenv (varname);
      return TRUE;
#else
      char *putenv_value;
      size_t len;

      len = strlen (varname);

      /* Use system malloc to avoid memleaks that dbus_malloc
       * will get upset about.
       */
      
      putenv_value = malloc (len + 1);
      if (putenv_value == NULL)
        return FALSE;

      strcpy (putenv_value, varname);
      
      return (putenv (putenv_value) == 0);
#endif
    }
  else
    {
#ifdef HAVE_SETENV
      return (setenv (varname, value, TRUE) == 0);
#else
      char *putenv_value;
      size_t len;
      size_t varname_len;
      size_t value_len;

      varname_len = strlen (varname);
      value_len = strlen (value);
      
      len = varname_len + value_len + 1 /* '=' */ ;

      /* Use system malloc to avoid memleaks that dbus_malloc
       * will get upset about.
       */
      
      putenv_value = malloc (len + 1);
      if (putenv_value == NULL)
        return FALSE;

      strcpy (putenv_value, varname);
      strcpy (putenv_value + varname_len, "=");
      strcpy (putenv_value + varname_len + 1, value);
      
      return (putenv (putenv_value) == 0);
#endif
    }
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

  buf = _dbus_string_get_data_len (str, orig_len, MAX_LONG_LEN);

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
 * Appends an unsigned integer to a DBusString.
 * 
 * @param str the string
 * @param value the integer value
 * @returns #FALSE if not enough memory or other failure.
 */
dbus_bool_t
_dbus_string_append_uint (DBusString    *str,
                          unsigned long  value)
{
  /* this is wrong, but definitely on the high side. */
#define MAX_ULONG_LEN (MAX_LONG_LEN * 2)
  int orig_len;
  int i;
  char *buf;
  
  orig_len = _dbus_string_get_length (str);

  if (!_dbus_string_lengthen (str, MAX_ULONG_LEN))
    return FALSE;

  buf = _dbus_string_get_data_len (str, orig_len, MAX_ULONG_LEN);

  snprintf (buf, MAX_ULONG_LEN, "%lu", value);

  i = 0;
  while (*buf)
    {
      ++buf;
      ++i;
    }
  
  _dbus_string_shorten (str, MAX_ULONG_LEN - i);
  
  return TRUE;
}

#ifdef DBUS_BUILD_TESTS
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

  buf = _dbus_string_get_data_len (str, orig_len, MAX_DOUBLE_LEN);

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
#endif /* DBUS_BUILD_TESTS */

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

  p = _dbus_string_get_const_data_len (str, start,
                                       _dbus_string_get_length (str) - start);

  end = NULL;
  errno = 0;
  v = strtol (p, &end, 0);
  if (end == NULL || end == p || errno != 0)
    return FALSE;

  if (value_return)
    *value_return = v;
  if (end_return)
    *end_return = start + (end - p);

  return TRUE;
}

/**
 * Parses an unsigned integer contained in a DBusString. Either return
 * parameter may be #NULL if you aren't interested in it. The integer
 * is parsed and stored in value_return. Return parameters are not
 * initialized if the function returns #FALSE.
 *
 * @param str the string
 * @param start the byte index of the start of the integer
 * @param value_return return location of the integer value or #NULL
 * @param end_return return location of the end of the integer, or #NULL
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_string_parse_uint (const DBusString *str,
                         int               start,
                         unsigned long    *value_return,
                         int              *end_return)
{
  unsigned long v;
  const char *p;
  char *end;

  p = _dbus_string_get_const_data_len (str, start,
                                       _dbus_string_get_length (str) - start);

  end = NULL;
  errno = 0;
  v = strtoul (p, &end, 0);
  if (end == NULL || end == p || errno != 0)
    return FALSE;

  if (value_return)
    *value_return = v;
  if (end_return)
    *end_return = start + (end - p);

  return TRUE;
}

#ifdef DBUS_BUILD_TESTS
static dbus_bool_t
ascii_isspace (char c)
{
  return (c == ' ' ||
	  c == '\f' ||
	  c == '\n' ||
	  c == '\r' ||
	  c == '\t' ||
	  c == '\v');
}
#endif /* DBUS_BUILD_TESTS */

#ifdef DBUS_BUILD_TESTS
static dbus_bool_t
ascii_isdigit (char c)
{
  return c >= '0' && c <= '9';
}
#endif /* DBUS_BUILD_TESTS */

#ifdef DBUS_BUILD_TESTS
static dbus_bool_t
ascii_isxdigit (char c)
{
  return (ascii_isdigit (c) ||
	  (c >= 'a' && c <= 'f') ||
	  (c >= 'A' && c <= 'F'));
}
#endif /* DBUS_BUILD_TESTS */

#ifdef DBUS_BUILD_TESTS
/* Calls strtod in a locale-independent fashion, by looking at
 * the locale data and patching the decimal comma to a point.
 *
 * Relicensed from glib.
 */
static double
ascii_strtod (const char *nptr,
	      char      **endptr)
{
  char *fail_pos;
  double val;
  struct lconv *locale_data;
  const char *decimal_point;
  int decimal_point_len;
  const char *p, *decimal_point_pos;
  const char *end = NULL; /* Silence gcc */

  fail_pos = NULL;

  locale_data = localeconv ();
  decimal_point = locale_data->decimal_point;
  decimal_point_len = strlen (decimal_point);

  _dbus_assert (decimal_point_len != 0);
  
  decimal_point_pos = NULL;
  if (decimal_point[0] != '.' ||
      decimal_point[1] != 0)
    {
      p = nptr;
      /* Skip leading space */
      while (ascii_isspace (*p))
	p++;
      
      /* Skip leading optional sign */
      if (*p == '+' || *p == '-')
	p++;
      
      if (p[0] == '0' &&
	  (p[1] == 'x' || p[1] == 'X'))
	{
	  p += 2;
	  /* HEX - find the (optional) decimal point */
	  
	  while (ascii_isxdigit (*p))
	    p++;
	  
	  if (*p == '.')
	    {
	      decimal_point_pos = p++;
	      
	      while (ascii_isxdigit (*p))
		p++;
	      
	      if (*p == 'p' || *p == 'P')
		p++;
	      if (*p == '+' || *p == '-')
		p++;
	      while (ascii_isdigit (*p))
		p++;
	      end = p;
	    }
	}
      else
	{
	  while (ascii_isdigit (*p))
	    p++;
	  
	  if (*p == '.')
	    {
	      decimal_point_pos = p++;
	      
	      while (ascii_isdigit (*p))
		p++;
	      
	      if (*p == 'e' || *p == 'E')
		p++;
	      if (*p == '+' || *p == '-')
		p++;
	      while (ascii_isdigit (*p))
		p++;
	      end = p;
	    }
	}
      /* For the other cases, we need not convert the decimal point */
    }

  /* Set errno to zero, so that we can distinguish zero results
     and underflows */
  errno = 0;
  
  if (decimal_point_pos)
    {
      char *copy, *c;

      /* We need to convert the '.' to the locale specific decimal point */
      copy = dbus_malloc (end - nptr + 1 + decimal_point_len);
      
      c = copy;
      memcpy (c, nptr, decimal_point_pos - nptr);
      c += decimal_point_pos - nptr;
      memcpy (c, decimal_point, decimal_point_len);
      c += decimal_point_len;
      memcpy (c, decimal_point_pos + 1, end - (decimal_point_pos + 1));
      c += end - (decimal_point_pos + 1);
      *c = 0;

      val = strtod (copy, &fail_pos);

      if (fail_pos)
	{
	  if (fail_pos > decimal_point_pos)
	    fail_pos = (char *)nptr + (fail_pos - copy) - (decimal_point_len - 1);
	  else
	    fail_pos = (char *)nptr + (fail_pos - copy);
	}
      
      dbus_free (copy);
	  
    }
  else
    val = strtod (nptr, &fail_pos);

  if (endptr)
    *endptr = fail_pos;
  
  return val;
}
#endif /* DBUS_BUILD_TESTS */

#ifdef DBUS_BUILD_TESTS
/**
 * Parses a floating point number contained in a DBusString. Either
 * return parameter may be #NULL if you aren't interested in it. The
 * integer is parsed and stored in value_return. Return parameters are
 * not initialized if the function returns #FALSE.
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

  p = _dbus_string_get_const_data_len (str, start,
                                       _dbus_string_get_length (str) - start);

  end = NULL;
  errno = 0;
  v = ascii_strtod (p, &end);
  if (end == NULL || end == p || errno != 0)
    return FALSE;

  if (value_return)
    *value_return = v;
  if (end_return)
    *end_return = start + (end - p);

  return TRUE;
}
#endif /* DBUS_BUILD_TESTS */

/** @} */ /* DBusString group */

/**
 * @addtogroup DBusInternalsUtils
 * @{
 */

/**
 * Frees the members of info
 * (but not info itself)
 * @param info the user info struct
 */
void
_dbus_user_info_free (DBusUserInfo *info)
{
  dbus_free (info->group_ids);
  dbus_free (info->username);
  dbus_free (info->homedir);
}

/**
 * Frees the members of info (but not info itself).
 *
 * @param info the group info
 */
void
_dbus_group_info_free (DBusGroupInfo    *info)
{
  dbus_free (info->groupname);
}

/**
 * Sets fields in DBusCredentials to DBUS_PID_UNSET,
 * DBUS_UID_UNSET, DBUS_GID_UNSET.
 *
 * @param credentials the credentials object to fill in
 */
void
_dbus_credentials_clear (DBusCredentials *credentials)
{
  credentials->pid = DBUS_PID_UNSET;
  credentials->uid = DBUS_UID_UNSET;
  credentials->gid = DBUS_GID_UNSET;
}

/**
 * Checks whether the provided_credentials are allowed to log in
 * as the expected_credentials.
 *
 * @param expected_credentials credentials we're trying to log in as
 * @param provided_credentials credentials we have
 * @returns #TRUE if we can log in
 */
dbus_bool_t
_dbus_credentials_match (const DBusCredentials *expected_credentials,
                         const DBusCredentials *provided_credentials)
{
  if (provided_credentials->uid == DBUS_UID_UNSET)
    return FALSE;
  else if (expected_credentials->uid == DBUS_UID_UNSET)
    return FALSE;
  else if (provided_credentials->uid == 0)
    return TRUE;
  else if (provided_credentials->uid == expected_credentials->uid)
    return TRUE;
  else
    return FALSE;
}

_DBUS_DEFINE_GLOBAL_LOCK (atomic);

#ifdef DBUS_USE_ATOMIC_INT_486
/* Taken from CVS version 1.7 of glibc's sysdeps/i386/i486/atomicity.h */
/* Since the asm stuff here is gcc-specific we go ahead and use "inline" also */
static inline dbus_int32_t
atomic_exchange_and_add (DBusAtomic            *atomic,
                         volatile dbus_int32_t  val)
{
  register dbus_int32_t result;

  __asm__ __volatile__ ("lock; xaddl %0,%1"
                        : "=r" (result), "=m" (atomic->value)
			: "0" (val), "m" (atomic->value));
  return result;
}
#endif

/**
 * Atomically increments an integer
 *
 * @param atomic pointer to the integer to increment
 * @returns the value before incrementing
 *
 * @todo implement arch-specific faster atomic ops
 */
dbus_int32_t
_dbus_atomic_inc (DBusAtomic *atomic)
{
#ifdef DBUS_USE_ATOMIC_INT_486
  return atomic_exchange_and_add (atomic, 1);
#else
  dbus_int32_t res;
  _DBUS_LOCK (atomic);
  res = atomic->value;
  atomic->value += 1;
  _DBUS_UNLOCK (atomic);
  return res;
#endif
}

/**
 * Atomically decrement an integer
 *
 * @param atomic pointer to the integer to decrement
 * @returns the value before decrementing
 *
 * @todo implement arch-specific faster atomic ops
 */
dbus_int32_t
_dbus_atomic_dec (DBusAtomic *atomic)
{
#ifdef DBUS_USE_ATOMIC_INT_486
  return atomic_exchange_and_add (atomic, -1);
#else
  dbus_int32_t res;
  
  _DBUS_LOCK (atomic);
  res = atomic->value;
  atomic->value -= 1;
  _DBUS_UNLOCK (atomic);
  return res;
#endif
}

void
_dbus_generate_pseudorandom_bytes_buffer (char *buffer,
                                          int   n_bytes)
{
  long tv_usec;
  int i;
  
  /* fall back to pseudorandom */
  _dbus_verbose ("Falling back to pseudorandom for %d bytes\n",
                 n_bytes);
  
  _dbus_get_current_time (NULL, &tv_usec);
  srand (tv_usec);
  
  i = 0;
  while (i < n_bytes)
    {
      double r;
      unsigned int b;
          
      r = rand ();
      b = (r / (double) RAND_MAX) * 255.0;

      buffer[i] = b;

      ++i;
    }
}

/**
 * Fills n_bytes of the given buffer with random bytes.
 *
 * @param buffer an allocated buffer
 * @param n_bytes the number of bytes in buffer to write to
 */
void
_dbus_generate_random_bytes_buffer (char *buffer,
                                    int   n_bytes)
{
  DBusString str;

  if (!_dbus_string_init (&str))
    {
      _dbus_generate_pseudorandom_bytes_buffer (buffer, n_bytes);
      return;
    }

  if (!_dbus_generate_random_bytes (&str, n_bytes))
    {
      _dbus_string_free (&str);
      _dbus_generate_pseudorandom_bytes_buffer (buffer, n_bytes);
      return;
    }

  _dbus_string_copy_to_buffer (&str, buffer, n_bytes);

  _dbus_string_free (&str);
}

/**
 * Generates the given number of random bytes, where the bytes are
 * chosen from the alphanumeric ASCII subset.
 *
 * @param str the string
 * @param n_bytes the number of random ASCII bytes to append to string
 * @returns #TRUE on success, #FALSE if no memory or other failure
 */
dbus_bool_t
_dbus_generate_random_ascii (DBusString *str,
                             int         n_bytes)
{
  static const char letters[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz";
  int i;
  int len;
  
  if (!_dbus_generate_random_bytes (str, n_bytes))
    return FALSE;
  
  len = _dbus_string_get_length (str);
  i = len - n_bytes;
  while (i < len)
    {
      _dbus_string_set_byte (str, i,
                             letters[_dbus_string_get_byte (str, i) %
                                     (sizeof (letters) - 1)]);

      ++i;
    }

  _dbus_assert (_dbus_string_validate_ascii (str, len - n_bytes,
                                             n_bytes));

  return TRUE;
}

/**
 * Gets a UID from a UID string.
 *
 * @param uid_str the UID in string form
 * @param uid UID to fill in
 * @returns #TRUE if successfully filled in UID
 */
dbus_bool_t
_dbus_parse_uid (const DBusString      *uid_str,
                 dbus_uid_t            *uid)
{
  int end;
  long val;
  
  if (_dbus_string_get_length (uid_str) == 0)
    {
      _dbus_verbose ("UID string was zero length\n");
      return FALSE;
    }

  val = -1;
  end = 0;
  if (!_dbus_string_parse_int (uid_str, 0, &val,
                               &end))
    {
      _dbus_verbose ("could not parse string as a UID\n");
      return FALSE;
    }
  
  if (end != _dbus_string_get_length (uid_str))
    {
      _dbus_verbose ("string contained trailing stuff after UID\n");
      return FALSE;
    }

  *uid = val;

  return TRUE;
}

/**
 * Converts a UNIX or Windows errno
 * into a #DBusError name.
 *
 * @todo should cover more errnos, specifically those
 * from open().
 * 
 * @param error_number the errno.
 * @returns an error name
 */
const char*
_dbus_error_from_errno (int error_number)
{
  switch (error_number)
    {
    case 0:
      return DBUS_ERROR_FAILED;
      
#ifdef EPROTONOSUPPORT
    case EPROTONOSUPPORT:
      return DBUS_ERROR_NOT_SUPPORTED;
#endif
#ifdef EAFNOSUPPORT
    case EAFNOSUPPORT:
      return DBUS_ERROR_NOT_SUPPORTED;
#endif
#ifdef ENFILE
    case ENFILE:
      return DBUS_ERROR_LIMITS_EXCEEDED; /* kernel out of memory */
#endif
#ifdef EMFILE
    case EMFILE:
      return DBUS_ERROR_LIMITS_EXCEEDED;
#endif
#ifdef EACCES
    case EACCES:
      return DBUS_ERROR_ACCESS_DENIED;
#endif
#ifdef EPERM
    case EPERM:
      return DBUS_ERROR_ACCESS_DENIED;
#endif
#ifdef ENOBUFS
    case ENOBUFS:
      return DBUS_ERROR_NO_MEMORY;
#endif
#ifdef ENOMEM
    case ENOMEM:
      return DBUS_ERROR_NO_MEMORY;
#endif
#ifdef EINVAL
    case EINVAL:
      return DBUS_ERROR_FAILED;
#endif
#ifdef EBADF
    case EBADF:
      return DBUS_ERROR_FAILED;
#endif
#ifdef EFAULT
    case EFAULT:
      return DBUS_ERROR_FAILED;
#endif
#ifdef ENOTSOCK
    case ENOTSOCK:
      return DBUS_ERROR_FAILED;
#endif
#ifdef EISCONN
    case EISCONN:
      return DBUS_ERROR_FAILED;
#endif
#ifdef ECONNREFUSED
    case ECONNREFUSED:
      return DBUS_ERROR_NO_SERVER;
#endif
#ifdef ETIMEDOUT
    case ETIMEDOUT:
      return DBUS_ERROR_TIMEOUT;
#endif
#ifdef ENETUNREACH
    case ENETUNREACH:
      return DBUS_ERROR_NO_NETWORK;
#endif
#ifdef EADDRINUSE
    case EADDRINUSE:
      return DBUS_ERROR_ADDRESS_IN_USE;
#endif
#ifdef EEXIST
    case EEXIST:
      return DBUS_ERROR_FILE_EXISTS;
#endif
#ifdef ENOENT
    case ENOENT:
      return DBUS_ERROR_FILE_NOT_FOUND;
#endif
    }

  return DBUS_ERROR_FAILED;
}

/** @} end of sysdeps */

/* tests in dbus-sysdeps-util.c */
