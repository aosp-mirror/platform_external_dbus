/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-string.c String utility class (internal to D-BUS implementation)
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
#include "dbus-string.h"
/* we allow a system header here, for speed/convenience */
#include <string.h>

/**
 * @defgroup DBusString string class
 * @ingroup  DBusInternals
 * @brief DBusString data structure
 *
 * Types and functions related to DBusString. DBusString is intended
 * to be a string class that makes it hard to mess up security issues
 * (and just in general harder to write buggy code).  It should be
 * used (or extended and then used) rather than the libc stuff in
 * string.h.  The string class is a bit inconvenient at spots because
 * it handles out-of-memory failures and tries to be extra-robust.
 * 
 * A DBusString has a maximum length set at initialization time; this
 * can be used to ensure that a buffer doesn't get too big.  The
 * _dbus_string_lengthen() method checks for overflow, and for max
 * length being exceeded.
 * 
 * Try to avoid conversion to a plain C string, i.e. add methods on
 * the string object instead, only convert to C string when passing
 * things out to the public API. In particular, no sprintf, strcpy,
 * strcat, any of that should be used. The GString feature of
 * accepting negative numbers for "length of string" is also absent,
 * because it could keep us from detecting bogus huge lengths. i.e. if
 * we passed in some bogus huge length it would be taken to mean
 * "current length of string" instead of "broken crack"
 */

/**
 * @defgroup DBusStringInternals DBusString implementation details
 * @ingroup  DBusInternals
 * @brief DBusString implementation details
 *
 * The guts of DBusString.
 *
 * @{
 */

/**
 * @brief Internals of DBusString.
 * 
 * DBusString internals. DBusString is an opaque objects, it must be
 * used via accessor functions.
 */
typedef struct
{
  unsigned char *str;            /**< String data, plus nul termination */
  int            len;            /**< Length without nul */
  int            allocated;      /**< Allocated size of data */
  int            max_length;     /**< Max length of this string. */
  unsigned int   constant : 1;   /**< String data is not owned by DBusString */
  unsigned int   locked : 1;     /**< DBusString has been locked and can't be changed */
  unsigned int   invalid : 1;    /**< DBusString is invalid (e.g. already freed) */
} DBusRealString;

/**
 * Checks a bunch of assertions about a string object
 *
 * @param real the DBusRealString
 */
#define DBUS_GENERIC_STRING_PREAMBLE(real) _dbus_assert ((real) != NULL); _dbus_assert (!(real)->invalid); _dbus_assert ((real)->len >= 0); _dbus_assert ((real)->allocated >= 0); _dbus_assert ((real)->max_length >= 0); _dbus_assert ((real)->len <= (real)->allocated); _dbus_assert ((real)->len <= (real)->max_length)

/**
 * Checks assertions about a string object that needs to be
 * modifiable - may not be locked or const. Also declares
 * the "real" variable pointing to DBusRealString. 
 * @param str the string
 */
#define DBUS_STRING_PREAMBLE(str) DBusRealString *real = (DBusRealString*) str; \
  DBUS_GENERIC_STRING_PREAMBLE (real);                                          \
  _dbus_assert (!(real)->constant);                                             \
  _dbus_assert (!(real)->locked)

/**
 * Checks assertions about a string object that may be locked but
 * can't be const. i.e. a string object that we can free.  Also
 * declares the "real" variable pointing to DBusRealString.
 *
 * @param str the string
 */
#define DBUS_LOCKED_STRING_PREAMBLE(str) DBusRealString *real = (DBusRealString*) str; \
  DBUS_GENERIC_STRING_PREAMBLE (real);                                                 \
  _dbus_assert (!(real)->constant)

/**
 * Checks assertions about a string that may be const or locked.  Also
 * declares the "real" variable pointing to DBusRealString.
 * @param str the string.
 */
#define DBUS_CONST_STRING_PREAMBLE(str) const DBusRealString *real = (DBusRealString*) str; \
  DBUS_GENERIC_STRING_PREAMBLE (real)

/** @} */

/**
 * @addtogroup DBusString
 * @{
 */

/** Assert that the string's memory is 8-byte aligned.
 *
 *  @todo Currently we just hope libc returns 8-byte aligned memory
 *  (which is true for GNU libc), but really we need to ensure it by
 *  allocating 8 extra bytes and keeping an "align_offset : 3" field
 *  in DBusString, or something along those lines.
 */
#define ASSERT_8_BYTE_ALIGNED(s) \
  _dbus_assert (_DBUS_ALIGN_ADDRESS (((const DBusRealString*)s)->str, 8) == ((const DBusRealString*)s)->str)

/**
 * Initializes a string. The maximum length may be _DBUS_INT_MAX for
 * no maximum. The string starts life with zero length.
 * The string must eventually be freed with _dbus_string_free().
 *
 * @todo the max length feature is useless, because it looks to the
 * app like out of memory, and the app might try to "recover" - but
 * recovery in this case is impossible, as we can't ever "get more
 * memory" - so should delete the max length feature I think. Well, at
 * least there's a strong caveat that it can only be used when
 * out-of-memory is a permanent fatal error.
 *
 * @todo we could make this init routine not alloc any memory and
 * return void, would simplify a lot of code, however it might
 * complexify things elsewhere because _dbus_string_get_data()
 * etc. could suddenly fail as they'd need to alloc new memory.
 * 
 * @param str memory to hold the string
 * @param max_length the maximum size of the string
 * @returns #TRUE on success */
dbus_bool_t
_dbus_string_init (DBusString *str,
                   int         max_length)
{
  DBusRealString *real;
  
  _dbus_assert (str != NULL);
  _dbus_assert (max_length >= 0);

  _dbus_assert (sizeof (DBusString) == sizeof (DBusRealString));
  
  real = (DBusRealString*) str;

  /* It's very important not to touch anything
   * other than real->str if we're going to fail,
   * since we also use this function to reset
   * an existing string, e.g. in _dbus_string_steal_data()
   */
  
#define INITIAL_ALLOC 2
  
  real->str = dbus_malloc (INITIAL_ALLOC);
  if (real->str == NULL)
    return FALSE;  
  
  real->allocated = INITIAL_ALLOC;
  real->len = 0;
  real->str[real->len] = '\0';
  
  real->max_length = max_length;
  real->constant = FALSE;
  real->locked = FALSE;
  real->invalid = FALSE;

  ASSERT_8_BYTE_ALIGNED (str);
  
  return TRUE;
}

/**
 * Initializes a constant string. The value parameter is not copied
 * (should be static), and the string may never be modified.
 * It is safe but not necessary to call _dbus_string_free()
 * on a const string.
 * 
 * @param str memory to use for the string
 * @param value a string to be stored in str (not copied!!!)
 */
void
_dbus_string_init_const (DBusString *str,
                         const char *value)
{
  _dbus_string_init_const_len (str, value,
                               strlen (value));
}

/**
 * Initializes a constant string with a length. The value parameter is
 * not copied (should be static), and the string may never be
 * modified.  It is safe but not necessary to call _dbus_string_free()
 * on a const string.
 * 
 * @param str memory to use for the string
 * @param value a string to be stored in str (not copied!!!)
 * @param len the length to use
 */
void
_dbus_string_init_const_len (DBusString *str,
                             const char *value,
                             int         len)
{
  DBusRealString *real;
  
  _dbus_assert (str != NULL);
  _dbus_assert (value != NULL);

  real = (DBusRealString*) str;
  
  real->str = (char*) value;
  real->len = len;
  real->allocated = real->len;
  real->max_length = real->len;
  real->constant = TRUE;
  real->invalid = FALSE;

  /* We don't require const strings to be 8-byte aligned as the
   * memory is coming from elsewhere.
   */
}

/**
 * Frees a string created by _dbus_string_init().
 *
 * @param str memory where the string is stored.
 */
void
_dbus_string_free (DBusString *str)
{
  DBusRealString *real = (DBusRealString*) str;
  DBUS_GENERIC_STRING_PREAMBLE (real);
  
  if (real->constant)
    return;
  dbus_free (real->str);

  real->invalid = TRUE;
}

/**
 * Locks a string such that any attempts to change the string
 * will result in aborting the program. Also, if the string
 * is wasting a lot of memory (allocation is larger than what
 * the string is really using), _dbus_string_lock() will realloc
 * the string's data to "compact" it.
 *
 * @param str the string to lock.
 */
void
_dbus_string_lock (DBusString *str)
{  
  DBUS_LOCKED_STRING_PREAMBLE (str); /* can lock multiple times */

  real->locked = TRUE;

  /* Try to realloc to avoid excess memory usage, since
   * we know we won't change the string further
   */
#define MAX_WASTE 24
  if (real->allocated > (real->len + MAX_WASTE))
    {
      char *new_str;
      int new_allocated;

      new_allocated = real->len + 1;

      new_str = dbus_realloc (real->str, new_allocated);
      if (new_str != NULL)
        {
          real->str = new_str;
          real->allocated = new_allocated;
          ASSERT_8_BYTE_ALIGNED (str);
        }
    }
}

static dbus_bool_t
set_length (DBusRealString *real,
            int             new_length)
{
  /* Note, we are setting the length without nul termination */

  /* exceeding max length is the same as failure to allocate memory */
  if (new_length > real->max_length)
    return FALSE;
  
  while (new_length >= real->allocated)
    {
      int new_allocated;
      char *new_str;
      
      new_allocated = 2 + real->allocated * 2;
      if (new_allocated < real->allocated)
        return FALSE; /* overflow */
        
      new_str = dbus_realloc (real->str, new_allocated);
      if (new_str == NULL)
        return FALSE;

      real->str = new_str;
      real->allocated = new_allocated;

      ASSERT_8_BYTE_ALIGNED (real);
    }

  real->len = new_length;
  real->str[real->len] = '\0';

  return TRUE;
}

static dbus_bool_t
open_gap (int             len,
          DBusRealString *dest,
          int             insert_at)
{
  if (len == 0)
    return TRUE;

  if (!set_length (dest, dest->len + len))
    return FALSE;

  memmove (dest->str + insert_at + len, 
           dest->str + insert_at,
           dest->len - len - insert_at);

  return TRUE;
}

/**
 * Gets the raw character buffer from the string.  The returned buffer
 * will be nul-terminated, but note that strings may contain binary
 * data so there may be extra nul characters prior to the termination.
 * This function should be little-used, extend DBusString or add
 * stuff to dbus-sysdeps.c instead. It's an error to use this
 * function on a const string.
 *
 * @param str the string
 * @param data_return place to store the returned data
 */
void
_dbus_string_get_data (DBusString        *str,
                       char             **data_return)
{
  DBUS_STRING_PREAMBLE (str);
  _dbus_assert (data_return != NULL);
  
  *data_return = real->str;
}

/**
 * Gets the raw character buffer from a const string.
 *
 * @todo should return the const char* instead of using an out param;
 * the temporary variable encourages a bug where you use const data
 * after modifying the string and possibly causing a realloc.
 *
 * @param str the string
 * @param data_return location to store returned data
 */
void
_dbus_string_get_const_data (const DBusString  *str,
                             const char       **data_return)
{
  DBUS_CONST_STRING_PREAMBLE (str);
  _dbus_assert (data_return != NULL);
  
  *data_return = real->str;
}

/**
 * Gets a sub-portion of the raw character buffer from the
 * string. The "len" field is required simply for error
 * checking, to be sure you don't try to use more
 * string than exists. The nul termination of the
 * returned buffer remains at the end of the entire
 * string, not at start + len.
 *
 * @param str the string
 * @param data_return location to return the buffer
 * @param start byte offset to return
 * @param len length of segment to return
 */
void
_dbus_string_get_data_len (DBusString *str,
                           char      **data_return,
                           int         start,
                           int         len)
{
  DBUS_STRING_PREAMBLE (str);
  _dbus_assert (data_return != NULL);
  _dbus_assert (start >= 0);
  _dbus_assert (len >= 0);
  _dbus_assert ((start + len) <= real->len);
  
  *data_return = real->str + start;
}

/**
 * const version of _dbus_string_get_data_len().
 *
 * @todo should return the const char* instead of using an out param;
 * the temporary variable encourages a bug where you use const data
 * after modifying the string and possibly causing a realloc.
 * 
 * @param str the string
 * @param data_return location to return the buffer
 * @param start byte offset to return
 * @param len length of segment to return
 */
void
_dbus_string_get_const_data_len (const DBusString  *str,
                                 const char       **data_return,
                                 int                start,
                                 int                len)
{
  DBUS_CONST_STRING_PREAMBLE (str);
  _dbus_assert (data_return != NULL);
  _dbus_assert (start >= 0);
  _dbus_assert (len >= 0);
  _dbus_assert ((start + len) <= real->len);
  
  *data_return = real->str + start;
}

/**
 * Sets the value of the byte at the given position.
 *
 * @param str the string
 * @param i the position
 * @param byte the new value
 */
void
_dbus_string_set_byte (DBusString    *str,
                       int            i,
                       unsigned char  byte)
{
  DBUS_STRING_PREAMBLE (str);
  _dbus_assert (i < real->len);

  real->str[i] = byte;
}

/**
 * Gets the byte at the given position.
 *
 * @param str the string
 * @param start the position
 * @returns the byte at that position
 */
unsigned char
_dbus_string_get_byte (const DBusString  *str,
                       int                start)
{
  DBUS_CONST_STRING_PREAMBLE (str);
  _dbus_assert (start < real->len);

  return real->str[start];
}

/**
 * Inserts the given byte at the given position.
 *
 * @param str the string
 * @param i the position
 * @param byte the value to insert
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_string_insert_byte (DBusString   *str,
                          int           i,
                          unsigned char byte)
{
  DBUS_STRING_PREAMBLE (str);
  _dbus_assert (i <= real->len);

  if (!open_gap (1, real, i))
    return FALSE;
  
  real->str[i] = byte;

  return TRUE;
}

/**
 * Like _dbus_string_get_data(), but removes the
 * gotten data from the original string. The caller
 * must free the data returned. This function may
 * fail due to lack of memory, and return #FALSE.
 *
 * @param str the string
 * @param data_return location to return the buffer
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_string_steal_data (DBusString        *str,
                         char             **data_return)
{
  DBUS_STRING_PREAMBLE (str);
  _dbus_assert (data_return != NULL);
  
  *data_return = real->str;

  /* reset the string */
  if (!_dbus_string_init (str, real->max_length))
    {
      /* hrm, put it back then */
      real->str = *data_return;
      *data_return = NULL;
      return FALSE;
    }

  return TRUE;
}

/**
 * Like _dbus_string_get_data_len(), but removes the gotten data from
 * the original string. The caller must free the data returned. This
 * function may fail due to lack of memory, and return #FALSE.
 * The returned string is nul-terminated and has length len.
 *
 * @param str the string
 * @param data_return location to return the buffer
 * @param start the start of segment to steal
 * @param len the length of segment to steal
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_string_steal_data_len (DBusString        *str,
                             char             **data_return,
                             int                start,
                             int                len)
{
  DBusString dest;
  
  DBUS_STRING_PREAMBLE (str);
  _dbus_assert (data_return != NULL);
  _dbus_assert (start >= 0);
  _dbus_assert (len >= 0);
  _dbus_assert ((start + len) <= real->len);

  if (!_dbus_string_init (&dest, real->max_length))
    return FALSE;

  if (!_dbus_string_move_len (str, start, len, &dest, 0))
    {
      _dbus_string_free (&dest);
      return FALSE;
    }
  
  if (!_dbus_string_steal_data (&dest, data_return))
    {
      _dbus_string_free (&dest);
      return FALSE;
    }

  _dbus_string_free (&dest);
  return TRUE;
}

/**
 * Gets the length of a string (not including nul termination).
 *
 * @returns the length.
 */
int
_dbus_string_get_length (const DBusString  *str)
{
  DBUS_CONST_STRING_PREAMBLE (str);
  
  return real->len;
}

/**
 * Makes a string longer by the given number of bytes.  Checks whether
 * adding additional_length to the current length would overflow an
 * integer, and checks for exceeding a string's max length.
 * The new bytes are not initialized, other than nul-terminating
 * the end of the string. The uninitialized bytes may contain
 * unexpected nul bytes or other junk.
 *
 * @param str a string
 * @param additional_length length to add to the string.
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_string_lengthen (DBusString *str,
                       int         additional_length)
{
  DBUS_STRING_PREAMBLE (str);  
  _dbus_assert (additional_length >= 0);
  
  if ((real->len + additional_length) < real->len)
    return FALSE; /* overflow */
  
  return set_length (real,
                     real->len + additional_length);
}

/**
 * Makes a string shorter by the given number of bytes.
 *
 * @param str a string
 * @param length_to_remove length to remove from the string.
 */
void
_dbus_string_shorten (DBusString *str,
                      int         length_to_remove)
{
  DBUS_STRING_PREAMBLE (str);
  _dbus_assert (length_to_remove >= 0);
  _dbus_assert (length_to_remove <= real->len);

  set_length (real,
              real->len - length_to_remove);
}

/**
 * Sets the length of a string. Can be used to truncate or lengthen
 * the string. If the string is lengthened, the function may fail and
 * return #FALSE. Newly-added bytes are not initialized, as with
 * _dbus_string_lengthen().
 *
 * @param str a string
 * @param length new length of the string.
 * @returns #FALSE on failure.
 */
dbus_bool_t
_dbus_string_set_length (DBusString *str,
                         int         length)
{
  DBUS_STRING_PREAMBLE (str);
  _dbus_assert (length >= 0);

  return set_length (real, length);
}

/**
 * Align the length of a string to a specific alignment (typically 4 or 8)
 * by appending nul bytes to the string.
 *
 * @param str a string
 * @param alignment the alignment
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_string_align_length (DBusString *str,
                           int         alignment)
{
  int new_len;
  int delta;
  DBUS_STRING_PREAMBLE (str);
  _dbus_assert (alignment >= 1);
  _dbus_assert (alignment <= 8); /* it has to be a bug if > 8 */

  new_len = _DBUS_ALIGN_VALUE (real->len, alignment);

  delta = new_len - real->len;
  _dbus_assert (delta >= 0);

  if (delta == 0)
    return TRUE;

  if (!set_length (real, new_len))
    return FALSE;

  memset (real->str + (new_len - delta),
          '\0', delta);

  return TRUE;
}

static dbus_bool_t
append (DBusRealString *real,
        const char     *buffer,
        int             buffer_len)
{
  if (buffer_len == 0)
    return TRUE;

  if (!_dbus_string_lengthen ((DBusString*)real, buffer_len))
    return FALSE;

  memcpy (real->str + (real->len - buffer_len),
          buffer,
          buffer_len);

  return TRUE;
}

/**
 * Appends a nul-terminated C-style string to a DBusString.
 *
 * @param str the DBusString
 * @param buffer the nul-terminated characters to append
 * @returns #FALSE if not enough memory.
 */
dbus_bool_t
_dbus_string_append (DBusString *str,
                     const char *buffer)
{
  int buffer_len;
  
  DBUS_STRING_PREAMBLE (str);
  _dbus_assert (buffer != NULL);
  
  buffer_len = strlen (buffer);

  return append (real, buffer, buffer_len);
}

/**
 * Appends block of bytes with the given length to a DBusString.
 *
 * @param str the DBusString
 * @param buffer the bytes to append
 * @param len the number of bytes to append
 * @returns #FALSE if not enough memory.
 */
dbus_bool_t
_dbus_string_append_len (DBusString *str,
                         const char *buffer,
                         int         len)
{
  DBUS_STRING_PREAMBLE (str);
  _dbus_assert (buffer != NULL);
  _dbus_assert (len >= 0);

  return append (real, buffer, len);
}

/**
 * Appends a single byte to the string, returning #FALSE
 * if not enough memory.
 *
 * @param str the string
 * @param byte the byte to append
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_string_append_byte (DBusString    *str,
                          unsigned char  byte)
{
  DBUS_STRING_PREAMBLE (str);

  if (!set_length (real, real->len + 1))
    return FALSE;

  real->str[real->len-1] = byte;

  return TRUE;
}

/**
 * Appends a single Unicode character, encoding the character
 * in UTF-8 format.
 *
 * @param str the string
 * @param ch the Unicode character
 */
dbus_bool_t
_dbus_string_append_unichar (DBusString    *str,
                             dbus_unichar_t ch)
{
  int len;
  int first;
  int i;
  char *out;
  
  DBUS_STRING_PREAMBLE (str);

  /* this code is from GLib but is pretty standard I think */
  
  len = 0;
  
  if (ch < 0x80)
    {
      first = 0;
      len = 1;
    }
  else if (ch < 0x800)
    {
      first = 0xc0;
      len = 2;
    }
  else if (ch < 0x10000)
    {
      first = 0xe0;
      len = 3;
    }
   else if (ch < 0x200000)
    {
      first = 0xf0;
      len = 4;
    }
  else if (ch < 0x4000000)
    {
      first = 0xf8;
      len = 5;
    }
  else
    {
      first = 0xfc;
      len = 6;
    }

  if (!set_length (real, real->len + len))
    return FALSE;

  out = real->str + (real->len - len);
  
  for (i = len - 1; i > 0; --i)
    {
      out[i] = (ch & 0x3f) | 0x80;
      ch >>= 6;
    }
  out[0] = ch | first;

  return TRUE;
}

static void
delete (DBusRealString *real,
        int             start,
        int             len)
{
  if (len == 0)
    return;
  
  memmove (real->str + start, real->str + start + len, real->len - (start + len));
  real->len -= len;
  real->str[real->len] = '\0';
}

/**
 * Deletes a segment of a DBusString with length len starting at
 * start. (Hint: to clear an entire string, setting length to 0
 * with _dbus_string_set_length() is easier.)
 *
 * @param str the DBusString
 * @param start where to start deleting
 * @param len the number of bytes to delete
 */
void
_dbus_string_delete (DBusString       *str,
                     int               start,
                     int               len)
{
  DBUS_STRING_PREAMBLE (str);
  _dbus_assert (start >= 0);
  _dbus_assert (len >= 0);
  _dbus_assert ((start + len) <= real->len);
  
  delete (real, start, len);
}

static dbus_bool_t
copy (DBusRealString *source,
      int             start,
      int             len,
      DBusRealString *dest,
      int             insert_at)
{
  if (len == 0)
    return TRUE;

  if (!open_gap (len, dest, insert_at))
    return FALSE;
  
  memcpy (dest->str + insert_at,
          source->str + start,
          len);

  return TRUE;
}

/**
 * Checks assertions for two strings we're copying a segment between,
 * and declares real_source/real_dest variables.
 *
 * @param source the source string
 * @param start the starting offset
 * @param dest the dest string
 * @param insert_at where the copied segment is inserted
 */
#define DBUS_STRING_COPY_PREAMBLE(source, start, dest, insert_at)       \
  DBusRealString *real_source = (DBusRealString*) source;               \
  DBusRealString *real_dest = (DBusRealString*) dest;                   \
  _dbus_assert ((source) != (dest));                                    \
  DBUS_GENERIC_STRING_PREAMBLE (real_source);                           \
  DBUS_GENERIC_STRING_PREAMBLE (real_dest);                             \
  _dbus_assert (!real_dest->constant);                                  \
  _dbus_assert (!real_dest->locked);                                    \
  _dbus_assert ((start) >= 0);                                          \
  _dbus_assert ((start) <= real_source->len);                           \
  _dbus_assert ((insert_at) >= 0);                                      \
  _dbus_assert ((insert_at) <= real_dest->len)

/**
 * Moves the end of one string into another string. Both strings
 * must be initialized, valid strings.
 *
 * @param source the source string
 * @param start where to chop off the source string
 * @param dest the destination string
 * @param insert_at where to move the chopped-off part of source string
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
_dbus_string_move (DBusString       *source,
                   int               start,
                   DBusString       *dest,
                   int               insert_at)
{
  DBUS_STRING_COPY_PREAMBLE (source, start, dest, insert_at);
  
  if (!copy (real_source, start,
             real_source->len - start,
             real_dest,
             insert_at))
    return FALSE;

  delete (real_source, start,
          real_source->len - start);

  return TRUE;
}

/**
 * Like _dbus_string_move(), but does not delete the section
 * of the source string that's copied to the dest string.
 *
 * @param source the source string
 * @param start where to start copying the source string
 * @param dest the destination string
 * @param insert_at where to place the copied part of source string
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
_dbus_string_copy (const DBusString *source,
                   int               start,
                   DBusString       *dest,
                   int               insert_at)
{
  DBUS_STRING_COPY_PREAMBLE (source, start, dest, insert_at);

  return copy (real_source, start,
               real_source->len - start,
               real_dest,
               insert_at);
}

/**
 * Like _dbus_string_move(), but can move a segment from
 * the middle of the source string.
 * 
 * @param source the source string
 * @param start first byte of source string to move
 * @param len length of segment to move
 * @param dest the destination string
 * @param insert_at where to move the bytes from the source string
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
_dbus_string_move_len (DBusString       *source,
                       int               start,
                       int               len,
                       DBusString       *dest,
                       int               insert_at)

{
  DBUS_STRING_COPY_PREAMBLE (source, start, dest, insert_at);
  _dbus_assert (len >= 0);
  _dbus_assert ((start + len) <= real_source->len);

  if (!copy (real_source, start, len,
             real_dest,
             insert_at))
    return FALSE;

  delete (real_source, start,
          len);

  return TRUE;
}

/**
 * Like _dbus_string_copy(), but can copy a segment from the middle of
 * the source string.
 *
 * @param source the source string
 * @param start where to start copying the source string
 * @param len length of segment to copy
 * @param dest the destination string
 * @param insert_at where to place the copied segment of source string
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
_dbus_string_copy_len (const DBusString *source,
                       int               start,
                       int               len,
                       DBusString       *dest,
                       int               insert_at)
{
  DBUS_STRING_COPY_PREAMBLE (source, start, dest, insert_at);
  _dbus_assert (len >= 0);
  _dbus_assert ((start + len) <= real_source->len);
  
  return copy (real_source, start, len,
               real_dest,
               insert_at);
}

/**
 * Replaces a segment of dest string with a segment of source string.
 *
 * @todo optimize the case where the two lengths are the same, and
 * avoid memmoving the data in the trailing part of the string twice.
 * 
 * @param source the source string
 * @param start where to start copying the source string
 * @param len length of segment to copy
 * @param dest the destination string
 * @param replace_at start of segment of dest string to replace
 * @param replace_len length of segment of dest string to replace
 * @returns #FALSE if not enough memory
 *
 */
dbus_bool_t
_dbus_string_replace_len (const DBusString *source,
                          int               start,
                          int               len,
                          DBusString       *dest,
                          int               replace_at,
                          int               replace_len)
{
  DBUS_STRING_COPY_PREAMBLE (source, start, dest, replace_at);
  _dbus_assert (len >= 0);
  _dbus_assert ((start + len) <= real_source->len);
  _dbus_assert (replace_at >= 0);
  _dbus_assert ((replace_at + replace_len) <= real_dest->len);

  if (!copy (real_source, start, len,
             real_dest, replace_at))
    return FALSE;

  delete (real_dest, replace_at + len, replace_len);

  return TRUE;
}

/* Unicode macros from GLib */

/** computes length and mask of a unicode character
 * @param Char the char
 * @param Mask the mask variable to assign to
 * @param Len the length variable to assign to
 */
#define UTF8_COMPUTE(Char, Mask, Len)					      \
  if (Char < 128)							      \
    {									      \
      Len = 1;								      \
      Mask = 0x7f;							      \
    }									      \
  else if ((Char & 0xe0) == 0xc0)					      \
    {									      \
      Len = 2;								      \
      Mask = 0x1f;							      \
    }									      \
  else if ((Char & 0xf0) == 0xe0)					      \
    {									      \
      Len = 3;								      \
      Mask = 0x0f;							      \
    }									      \
  else if ((Char & 0xf8) == 0xf0)					      \
    {									      \
      Len = 4;								      \
      Mask = 0x07;							      \
    }									      \
  else if ((Char & 0xfc) == 0xf8)					      \
    {									      \
      Len = 5;								      \
      Mask = 0x03;							      \
    }									      \
  else if ((Char & 0xfe) == 0xfc)					      \
    {									      \
      Len = 6;								      \
      Mask = 0x01;							      \
    }									      \
  else									      \
    Len = -1;

/**
 * computes length of a unicode character in UTF-8
 * @param Char the char
 */
#define UTF8_LENGTH(Char)              \
  ((Char) < 0x80 ? 1 :                 \
   ((Char) < 0x800 ? 2 :               \
    ((Char) < 0x10000 ? 3 :            \
     ((Char) < 0x200000 ? 4 :          \
      ((Char) < 0x4000000 ? 5 : 6)))))
   
/**
 * Gets a UTF-8 value.
 *
 * @param Result variable for extracted unicode char.
 * @param Chars the bytes to decode
 * @param Count counter variable
 * @param Mask mask for this char
 * @param Len length for this char in bytes
 */
#define UTF8_GET(Result, Chars, Count, Mask, Len)			      \
  (Result) = (Chars)[0] & (Mask);					      \
  for ((Count) = 1; (Count) < (Len); ++(Count))				      \
    {									      \
      if (((Chars)[(Count)] & 0xc0) != 0x80)				      \
	{								      \
	  (Result) = -1;						      \
	  break;							      \
	}								      \
      (Result) <<= 6;							      \
      (Result) |= ((Chars)[(Count)] & 0x3f);				      \
    }

/**
 * Check whether a unicode char is in a valid range.
 *
 * @param Char the character
 */
#define UNICODE_VALID(Char)                   \
    ((Char) < 0x110000 &&                     \
     ((Char) < 0xD800 || (Char) >= 0xE000) && \
     (Char) != 0xFFFE && (Char) != 0xFFFF)   

/**
 * Gets a unicode character from a UTF-8 string. Does no validation;
 * you must verify that the string is valid UTF-8 in advance and must
 * pass in the start of a character.
 *
 * @param str the string
 * @param start the start of the UTF-8 character.
 * @param ch_return location to return the character
 * @param end_return location to return the byte index of next character
 * @returns #TRUE on success, #FALSE otherwise.
 */
void
_dbus_string_get_unichar (const DBusString *str,
                          int               start,
                          dbus_unichar_t   *ch_return,
                          int              *end_return)
{
  int i, mask, len;
  dbus_unichar_t result;
  unsigned char c;
  unsigned char *p;
  DBUS_CONST_STRING_PREAMBLE (str);

  if (ch_return)
    *ch_return = 0;
  if (end_return)
    *end_return = real->len;
  
  mask = 0;
  p = real->str + start;
  c = *p;
  
  UTF8_COMPUTE (c, mask, len);
  if (len == -1)
    return;
  UTF8_GET (result, p, i, mask, len);

  if (result == (dbus_unichar_t)-1)
    return;

  if (ch_return)
    *ch_return = result;
  if (end_return)
    *end_return = start + len;
}

/**
 * Finds the given substring in the string,
 * returning #TRUE and filling in the byte index
 * where the substring was found, if it was found.
 * Returns #FALSE if the substring wasn't found.
 * Sets *start to the length of the string if the substring
 * is not found.
 *
 * @param str the string
 * @param start where to start looking
 * @param substr the substring
 * @param found return location for where it was found, or #NULL
 * @returns #TRUE if found
 */
dbus_bool_t
_dbus_string_find (const DBusString *str,
                   int               start,
                   const char       *substr,
                   int              *found)
{
  int i;
  DBUS_CONST_STRING_PREAMBLE (str);
  _dbus_assert (substr != NULL);
  _dbus_assert (start <= real->len);
  
  /* we always "find" an empty string */
  if (*substr == '\0')
    {
      if (found)
        *found = 0;
      return TRUE;
    }
  
  i = start;
  while (i < real->len)
    {
      if (real->str[i] == substr[0])
        {
          int j = i + 1;
          
          while (j < real->len)
            {
              if (substr[j - i] == '\0')
                break;
              else if (real->str[j] != substr[j - i])
                break;
              
              ++j;
            }

          if (substr[j - i] == '\0')
            {
              if (found)
                *found = i;
              return TRUE;
            }
        }
      
      ++i;
    }

  if (found)
    *found = real->len;
  
  return FALSE;
}

/**
 * Finds the given substring in the string,
 * up to a certain position,
 * returning #TRUE and filling in the byte index
 * where the substring was found, if it was found.
 * Returns #FALSE if the substring wasn't found.
 * Sets *start to the length of the string if the substring
 * is not found.
 *
 * @param str the string
 * @param start where to start looking
 * @param end where to stop looking
 * @param substr the substring
 * @param found return location for where it was found, or #NULL
 * @returns #TRUE if found
 */
dbus_bool_t
_dbus_string_find_to (const DBusString *str,
		      int               start,
		      int               end,
		      const char       *substr,
		      int              *found)
{
  int i;
  DBUS_CONST_STRING_PREAMBLE (str);
  _dbus_assert (substr != NULL);
  _dbus_assert (start <= real->len);
  _dbus_assert (end <= real->len);
  _dbus_assert (start < end);

  /* we always "find" an empty string */
  if (*substr == '\0')
    {
      if (found)
        *found = 0;
      return TRUE;
    }

  i = start;
  while (i < real->len && i < end)
    {
      if (real->str[i] == substr[0])
        {
          int j = i + 1;
          
          while (j < real->len && j < end)
            {
              if (substr[j - i] == '\0')
                break;
              else if (real->str[j] != substr[j - i])
                break;
              
              ++j;
            }

          if (substr[j - i] == '\0')
            {
              if (found)
                *found = i;
              return TRUE;
            }
        }
      
      ++i;
    }

  if (found)
    *found = end;
  
  return FALSE;  
}

/**
 * Finds a blank (space or tab) in the string. Returns #TRUE
 * if found, #FALSE otherwise. If a blank is not found sets
 * *found to the length of the string.
 *
 * @param str the string
 * @param start byte index to start looking
 * @param found place to store the location of the first blank
 * @returns #TRUE if a blank was found
 */
dbus_bool_t
_dbus_string_find_blank (const DBusString *str,
                         int               start,
                         int              *found)
{
  int i;
  DBUS_CONST_STRING_PREAMBLE (str);
  _dbus_assert (start <= real->len);
  
  i = start;
  while (i < real->len)
    {
      if (real->str[i] == ' ' ||
          real->str[i] == '\t')
        {
          if (found)
            *found = i;
          return TRUE;
        }
      
      ++i;
    }

  if (found)
    *found = real->len;
  
  return FALSE;
}

/**
 * Skips blanks from start, storing the first non-blank in *end
 *
 * @param str the string
 * @param start where to start
 * @param end where to store the first non-blank byte index
 */
void
_dbus_string_skip_blank (const DBusString *str,
                         int               start,
                         int              *end)
{
  int i;
  DBUS_CONST_STRING_PREAMBLE (str);
  _dbus_assert (start <= real->len);
  
  i = start;
  while (i < real->len)
    {
      if (!(real->str[i] == ' ' ||
            real->str[i] == '\t'))
        break;
      
      ++i;
    }

  _dbus_assert (i == real->len || !(real->str[i] == ' ' ||
                                    real->str[i] == '\t'));
  
  if (end)
    *end = i;
}

/**
 * Assigns a newline-terminated or \r\n-terminated line from the front
 * of the string to the given dest string. The dest string's previous
 * contents are deleted. If the source string contains no newline,
 * moves the entire source string to the dest string.
 * 
 * @param source the source string
 * @param dest the destination string (contents are replaced)
 * @returns #FALSE if no memory, or source has length 0
 */
dbus_bool_t
_dbus_string_pop_line (DBusString *source,
                       DBusString *dest)
{
  int eol;
  dbus_bool_t have_newline;
  
  _dbus_string_set_length (dest, 0);
  
  eol = 0;
  if (_dbus_string_find (source, 0, "\n", &eol))
    {
      have_newline = TRUE;
      eol += 1; /* include newline */
    }
  else
    {
      eol = _dbus_string_get_length (source);
      have_newline = FALSE;
    }

  if (eol == 0)
    return FALSE; /* eof */
  
  if (!_dbus_string_move_len (source, 0, eol,
                              dest, 0))
    {
      return FALSE;
    }

  /* dump the newline and the \r if we have one */
  if (have_newline)
    {
      dbus_bool_t have_cr;
      
      _dbus_assert (_dbus_string_get_length (dest) > 0);

      if (_dbus_string_get_length (dest) > 1 &&
          _dbus_string_get_byte (dest,
                                 _dbus_string_get_length (dest) - 2) == '\r')
        have_cr = TRUE;
      else
        have_cr = FALSE;
        
      _dbus_string_set_length (dest,
                               _dbus_string_get_length (dest) -
                               (have_cr ? 2 : 1));
    }
  
  return TRUE;
}

/**
 * Deletes up to and including the first blank space
 * in the string.
 *
 * @param str the string
 */
void
_dbus_string_delete_first_word (DBusString *str)
{
  int i;
  
  i = 0;
  if (_dbus_string_find_blank (str, 0, &i))
    _dbus_string_skip_blank (str, i, &i);

  _dbus_string_delete (str, 0, i);
}

/**
 * Deletes any leading blanks in the string
 *
 * @param str the string
 */
void
_dbus_string_delete_leading_blanks (DBusString *str)
{
  int i;
  
  i = 0;
  _dbus_string_skip_blank (str, 0, &i);

  if (i > 0)
    _dbus_string_delete (str, 0, i);
}

/**
 * Tests two DBusString for equality.
 *
 * @param a first string
 * @param b second string
 * @returns #TRUE if equal
 */
dbus_bool_t
_dbus_string_equal (const DBusString *a,
                    const DBusString *b)
{
  const unsigned char *ap;
  const unsigned char *bp;
  const unsigned char *a_end;
  const DBusRealString *real_a = (const DBusRealString*) a;
  const DBusRealString *real_b = (const DBusRealString*) b;
  DBUS_GENERIC_STRING_PREAMBLE (real_a);
  DBUS_GENERIC_STRING_PREAMBLE (real_b);

  if (real_a->len != real_b->len)
    return FALSE;

  ap = real_a->str;
  bp = real_b->str;
  a_end = real_a->str + real_a->len;
  while (ap != a_end)
    {
      if (*ap != *bp)
        return FALSE;
      
      ++ap;
      ++bp;
    }

  return TRUE;
}

/**
 * Tests two DBusString for equality up to the given length.
 *
 * @todo write a unit test
 *
 * @param a first string
 * @param b second string
 * @param len the lengh
 * @returns #TRUE if equal for the given number of bytes
 */
dbus_bool_t
_dbus_string_equal_len (const DBusString *a,
                        const DBusString *b,
                        int               len)
{
  const unsigned char *ap;
  const unsigned char *bp;
  const unsigned char *a_end;
  const DBusRealString *real_a = (const DBusRealString*) a;
  const DBusRealString *real_b = (const DBusRealString*) b;
  DBUS_GENERIC_STRING_PREAMBLE (real_a);
  DBUS_GENERIC_STRING_PREAMBLE (real_b);

  if (real_a->len != real_b->len &&
      (real_a->len < len || real_b->len < len))
    return FALSE;

  ap = real_a->str;
  bp = real_b->str;
  a_end = real_a->str + MIN (real_a->len, len);
  while (ap != a_end)
    {
      if (*ap != *bp)
        return FALSE;
      
      ++ap;
      ++bp;
    }

  return TRUE;
}

/**
 * Checks whether a string is equal to a C string.
 *
 * @param a the string
 * @param c_str the C string
 * @returns #TRUE if equal
 */
dbus_bool_t
_dbus_string_equal_c_str (const DBusString *a,
                          const char       *c_str)
{
  const unsigned char *ap;
  const unsigned char *bp;
  const unsigned char *a_end;
  const DBusRealString *real_a = (const DBusRealString*) a;
  DBUS_GENERIC_STRING_PREAMBLE (real_a);

  ap = real_a->str;
  bp = (const unsigned char*) c_str;
  a_end = real_a->str + real_a->len;
  while (ap != a_end && *bp)
    {
      if (*ap != *bp)
        return FALSE;
      
      ++ap;
      ++bp;
    }

  if (*ap && *bp == '\0')
    return FALSE;
  else if (ap == a_end && *bp)
    return FALSE;
  
  return TRUE;
}

/**
 * Checks whether a string starts with the given C string.
 *
 * @param a the string
 * @param c_str the C string
 * @returns #TRUE if string starts with it
 */
dbus_bool_t
_dbus_string_starts_with_c_str (const DBusString *a,
                                const char       *c_str)
{
  const unsigned char *ap;
  const unsigned char *bp;
  const unsigned char *a_end;
  const DBusRealString *real_a = (const DBusRealString*) a;
  DBUS_GENERIC_STRING_PREAMBLE (real_a);

  ap = real_a->str;
  bp = (const unsigned char*) c_str;
  a_end = real_a->str + real_a->len;
  while (ap != a_end && *bp)
    {
      if (*ap != *bp)
        return FALSE;
      
      ++ap;
      ++bp;
    }

  if (*bp == '\0')
    return TRUE;
  else
    return FALSE;
}

/**
 * Returns whether a string ends with the given suffix
 *
 * @param a the string
 * @param c_str the C-style string
 * @returns #TRUE if the string ends with the suffix
 */
dbus_bool_t
_dbus_string_ends_with_c_str (const DBusString *a,
                              const char       *c_str)
{
  const unsigned char *ap;
  const unsigned char *bp;
  const unsigned char *a_end;
  int c_str_len;
  const DBusRealString *real_a = (const DBusRealString*) a;
  DBUS_GENERIC_STRING_PREAMBLE (real_a);

  c_str_len = strlen (c_str);
  if (real_a->len < c_str_len)
    return FALSE;
  
  ap = real_a->str + (real_a->len - c_str_len);
  bp = (const unsigned char*) c_str;
  a_end = real_a->str + real_a->len;
  while (ap != a_end)
    {
      if (*ap != *bp)
        return FALSE;
      
      ++ap;
      ++bp;
    }

  _dbus_assert (*ap == '\0');
  _dbus_assert (*bp == '\0');
  
  return TRUE;
}

static const signed char base64_table[] = {
  /* 0 */ 'A',
  /* 1 */ 'B',
  /* 2 */ 'C',
  /* 3 */ 'D',
  /* 4 */ 'E',
  /* 5 */ 'F',
  /* 6 */ 'G',
  /* 7 */ 'H',
  /* 8 */ 'I',
  /* 9 */ 'J',
  /* 10 */ 'K',
  /* 11 */ 'L',
  /* 12 */ 'M',
  /* 13 */ 'N',
  /* 14 */ 'O',
  /* 15 */ 'P',
  /* 16 */ 'Q',
  /* 17 */ 'R',
  /* 18 */ 'S',
  /* 19 */ 'T',
  /* 20 */ 'U',
  /* 21 */ 'V',
  /* 22 */ 'W',
  /* 23 */ 'X',
  /* 24 */ 'Y',
  /* 25 */ 'Z',
  /* 26 */ 'a',
  /* 27 */ 'b',
  /* 28 */ 'c',
  /* 29 */ 'd',
  /* 30 */ 'e',
  /* 31 */ 'f',
  /* 32 */ 'g',
  /* 33 */ 'h',
  /* 34 */ 'i',
  /* 35 */ 'j',
  /* 36 */ 'k',
  /* 37 */ 'l',
  /* 38 */ 'm',
  /* 39 */ 'n',
  /* 40 */ 'o',
  /* 41 */ 'p',
  /* 42 */ 'q',
  /* 43 */ 'r',
  /* 44 */ 's',
  /* 45 */ 't',
  /* 46 */ 'u',
  /* 47 */ 'v',
  /* 48 */ 'w',
  /* 49 */ 'x',
  /* 50 */ 'y',
  /* 51 */ 'z',
  /* 52 */ '0',
  /* 53 */ '1',
  /* 54 */ '2',
  /* 55 */ '3',
  /* 56 */ '4',
  /* 57 */ '5',
  /* 58 */ '6',
  /* 59 */ '7',
  /* 60 */ '8',
  /* 61 */ '9',
  /* 62 */ '+',
  /* 63 */ '/'
};

/** The minimum char that's a valid char in Base64-encoded text */
#define UNBASE64_MIN_CHAR (43)
/** The maximum char that's a valid char in Base64-encoded text */
#define UNBASE64_MAX_CHAR (122)
/** Must subtract this from a char's integer value before offsetting
 * into unbase64_table
 */
#define UNBASE64_TABLE_OFFSET UNBASE64_MIN_CHAR
static const signed char unbase64_table[] = {
  /* 43 + */ 62,
  /* 44 , */ -1,
  /* 45 - */ -1,
  /* 46 . */ -1,
  /* 47 / */ 63,
  /* 48 0 */ 52,
  /* 49 1 */ 53,
  /* 50 2 */ 54,
  /* 51 3 */ 55,
  /* 52 4 */ 56,
  /* 53 5 */ 57,
  /* 54 6 */ 58,
  /* 55 7 */ 59,
  /* 56 8 */ 60,
  /* 57 9 */ 61,
  /* 58 : */ -1,
  /* 59 ; */ -1,
  /* 60 < */ -1,
  /* 61 = */ -1,
  /* 62 > */ -1,
  /* 63 ? */ -1,
  /* 64 @ */ -1,
  /* 65 A */ 0,
  /* 66 B */ 1,
  /* 67 C */ 2,
  /* 68 D */ 3,
  /* 69 E */ 4,
  /* 70 F */ 5,
  /* 71 G */ 6,
  /* 72 H */ 7,
  /* 73 I */ 8,
  /* 74 J */ 9,
  /* 75 K */ 10,
  /* 76 L */ 11,
  /* 77 M */ 12,
  /* 78 N */ 13,
  /* 79 O */ 14,
  /* 80 P */ 15,
  /* 81 Q */ 16,
  /* 82 R */ 17,
  /* 83 S */ 18,
  /* 84 T */ 19,
  /* 85 U */ 20,
  /* 86 V */ 21,
  /* 87 W */ 22,
  /* 88 X */ 23,
  /* 89 Y */ 24,
  /* 90 Z */ 25,
  /* 91 [ */ -1,
  /* 92 \ */ -1,
  /* 93 ] */ -1,
  /* 94 ^ */ -1,
  /* 95 _ */ -1,
  /* 96 ` */ -1,
  /* 97 a */ 26,
  /* 98 b */ 27,
  /* 99 c */ 28,
  /* 100 d */ 29,
  /* 101 e */ 30,
  /* 102 f */ 31,
  /* 103 g */ 32,
  /* 104 h */ 33,
  /* 105 i */ 34,
  /* 106 j */ 35,
  /* 107 k */ 36,
  /* 108 l */ 37,
  /* 109 m */ 38,
  /* 110 n */ 39,
  /* 111 o */ 40,
  /* 112 p */ 41,
  /* 113 q */ 42,
  /* 114 r */ 43,
  /* 115 s */ 44,
  /* 116 t */ 45,
  /* 117 u */ 46,
  /* 118 v */ 47,
  /* 119 w */ 48,
  /* 120 x */ 49,
  /* 121 y */ 50,
  /* 122 z */ 51
};

/**
 * Encodes a string using Base64, as documented in RFC 2045.
 *
 * @param source the string to encode
 * @param start byte index to start encoding
 * @param dest string where encoded data should be placed
 * @param insert_at where to place encoded data
 * @returns #TRUE if encoding was successful, #FALSE if no memory etc.
 */
dbus_bool_t
_dbus_string_base64_encode (const DBusString *source,
                            int               start,
                            DBusString       *dest,
                            int               insert_at)
{
  int source_len;
  int dest_len;
  const unsigned char *s;
  unsigned char *d;
  const unsigned char *triplet_end;
  const unsigned char *final_end;
  DBUS_STRING_COPY_PREAMBLE (source, start, dest, insert_at);  
  _dbus_assert (source != dest);
  
  /* For each 24 bits (3 bytes) of input, we have 4 chars of
   * output.
   */
  source_len = real_source->len - start;
  dest_len = (source_len / 3) * 4;
  if (source_len % 3 != 0)
    dest_len += 4;

  if (source_len == 0)
    return TRUE;
  
  if (!open_gap (dest_len, real_dest, insert_at))
    return FALSE;

  d = real_dest->str + insert_at;
  s = real_source->str + start;
  final_end = real_source->str + (start + source_len);
  triplet_end = final_end - (source_len % 3);
  _dbus_assert (triplet_end <= final_end);
  _dbus_assert ((final_end - triplet_end) < 3);

#define ENCODE_64(v) (base64_table[ (unsigned char) (v) ])
#define SIX_BITS_MASK (0x3f)
  _dbus_assert (SIX_BITS_MASK < _DBUS_N_ELEMENTS (base64_table));
  
  while (s != triplet_end)
    {
      unsigned int triplet;

      triplet = s[2] | (s[1] << 8) | (s[0] << 16);

      /* Encode each 6 bits. */

      *d++ = ENCODE_64 (triplet >> 18);
      *d++ = ENCODE_64 ((triplet >> 12) & SIX_BITS_MASK);
      *d++ = ENCODE_64 ((triplet >> 6) & SIX_BITS_MASK);
      *d++ = ENCODE_64 (triplet & SIX_BITS_MASK);
      
      s += 3;
    }

  switch (final_end - triplet_end)
    {
    case 2:
      {
        unsigned int doublet;
        
        doublet = s[1] | (s[0] << 8);        

        *d++ = ENCODE_64 (doublet >> 12);
        *d++ = ENCODE_64 ((doublet >> 6) & SIX_BITS_MASK);
        *d++ = ENCODE_64 (doublet & SIX_BITS_MASK);
        *d++ = '=';
      }
      break;
    case 1:
      {
        unsigned int singlet;
        
        singlet = s[0];

        *d++ = ENCODE_64 ((singlet >> 6) & SIX_BITS_MASK);
        *d++ = ENCODE_64 (singlet & SIX_BITS_MASK);
        *d++ = '=';
        *d++ = '=';
      }
      break;
    case 0:
      break;
    }

  _dbus_assert (d == (real_dest->str + (insert_at + dest_len)));

  return TRUE;
}

/**
 * Decodes a string from Base64, as documented in RFC 2045.
 *
 * @param source the string to decode
 * @param start byte index to start decode
 * @param dest string where decoded data should be placed
 * @param insert_at where to place decoded data
 * @returns #TRUE if decoding was successful, #FALSE if no memory etc.
 */
dbus_bool_t
_dbus_string_base64_decode (const DBusString *source,
                            int               start,
                            DBusString       *dest,
                            int               insert_at)
{
  int source_len;
  const char *s;
  const char *end;
  DBusString result;
  unsigned int triplet = 0;
  int sextet_count;
  int pad_count;
  DBUS_STRING_COPY_PREAMBLE (source, start, dest, insert_at);
  _dbus_assert (source != dest);
  
  source_len = real_source->len - start;
  s = real_source->str + start;
  end = real_source->str + source_len;

  if (source_len == 0)
    return TRUE;

  if (!_dbus_string_init (&result, _DBUS_INT_MAX))
    return FALSE;

  pad_count = 0;
  sextet_count = 0;
  while (s != end)
    {
      /* The idea is to just skip anything that isn't
       * a base64 char - it's allowed to have whitespace,
       * newlines, etc. in here. We also ignore trailing
       * base64 chars, though that's suspicious.
       */
      
      if (*s >= UNBASE64_MIN_CHAR &&
          *s <= UNBASE64_MAX_CHAR)
        {
          if (*s == '=')
            {
              /* '=' is padding, doesn't represent additional data
               * but does increment our count.
               */
              pad_count += 1;
              sextet_count += 1;
            }
          else
            {
              int val;

              val = unbase64_table[(*s) - UNBASE64_TABLE_OFFSET];

              if (val >= 0)
                {
                  triplet <<= 6;
                  triplet |= (unsigned int) val;
                  sextet_count += 1;
                }
            }

          if (sextet_count == 4)
            {
              /* no pad = 3 bytes, 1 pad = 2 bytes, 2 pad = 1 byte */
              if (pad_count < 1)
                _dbus_string_append_byte (&result,
                                          triplet >> 16);
              
              if (pad_count < 2)
                _dbus_string_append_byte (&result,
                                          (triplet >> 8) & 0xff);              
              
              _dbus_string_append_byte (&result,
                                        triplet & 0xff);
              
              sextet_count = 0;
              pad_count = 0;
              triplet = 0;
            }
        }
      
      ++s;
    }

  if (!_dbus_string_move (&result, 0, dest, insert_at))
    {
      _dbus_string_free (&result);
      return FALSE;
    }

  _dbus_string_free (&result);

  return TRUE;
}

/**
 * Encodes a string in hex, the way MD5 and SHA-1 are usually
 * encoded. (Each byte is two hex digits.)
 *
 * @param source the string to encode
 * @param start byte index to start encoding
 * @param dest string where encoded data should be placed
 * @param insert_at where to place encoded data
 * @returns #TRUE if encoding was successful, #FALSE if no memory etc.
 */
dbus_bool_t
_dbus_string_hex_encode (const DBusString *source,
                         int               start,
                         DBusString       *dest,
                         int               insert_at)
{
  DBusString result;
  const char hexdigits[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    'a', 'b', 'c', 'd', 'e', 'f'
  };
  const unsigned char *p;
  const unsigned char *end;
  dbus_bool_t retval;
  
  _dbus_assert (start <= _dbus_string_get_length (source));

  if (!_dbus_string_init (&result, _DBUS_INT_MAX))
    return FALSE;

  retval = FALSE;
  
  _dbus_string_get_const_data (source, (const char**) &p);
  end = p + _dbus_string_get_length (source);
  p += start;
  
  while (p != end)
    {
      if (!_dbus_string_append_byte (&result,
                                     hexdigits[(*p >> 4)]))
        goto out;
      
      if (!_dbus_string_append_byte (&result,
                                     hexdigits[(*p & 0x0f)]))
        goto out;

      ++p;
    }

  if (!_dbus_string_move (&result, 0, dest, insert_at))
    goto out;

  retval = TRUE;

 out:
  _dbus_string_free (&result);
  return retval;
}

/**
 * Decodes a string from hex encoding.
 *
 * @param source the string to decode
 * @param start byte index to start decode
 * @param dest string where decoded data should be placed
 * @param insert_at where to place decoded data
 * @returns #TRUE if decoding was successful, #FALSE if no memory etc.
 */
dbus_bool_t
_dbus_string_hex_decode (const DBusString *source,
                         int               start,
                         DBusString       *dest,
                         int               insert_at)
{
  DBusString result;
  const unsigned char *p;
  const unsigned char *end;
  dbus_bool_t retval;
  dbus_bool_t high_bits;
  
  _dbus_assert (start <= _dbus_string_get_length (source));

  if (!_dbus_string_init (&result, _DBUS_INT_MAX))
    return FALSE;

  retval = FALSE;

  high_bits = TRUE;
  _dbus_string_get_const_data (source, (const char**) &p);
  end = p + _dbus_string_get_length (source);
  p += start;
  
  while (p != end)
    {
      unsigned int val;

      switch (*p)
        {
        case '0':
          val = 0;
          break;
        case '1':
          val = 1;
          break;
        case '2':
          val = 2;
          break;
        case '3':
          val = 3;
          break;
        case '4':
          val = 4;
          break;
        case '5':
          val = 5;
          break;
        case '6':
          val = 6;
          break;
        case '7':
          val = 7;
          break;
        case '8':
          val = 8;
          break;
        case '9':
          val = 9;
          break;
        case 'a':
        case 'A':
          val = 10;
          break;
        case 'b':
        case 'B':
          val = 11;
          break;
        case 'c':
        case 'C':
          val = 12;
          break;
        case 'd':
        case 'D':
          val = 13;
          break;
        case 'e':
        case 'E':
          val = 14;
          break;
        case 'f':
        case 'F':
          val = 15;
          break;
        default:
          val = 0;
          _dbus_verbose ("invalid character '%c' in hex encoded text\n",
                         *p);
          goto out;
        }

      if (high_bits)
        {
          if (!_dbus_string_append_byte (&result,
                                         val << 4))
            goto out;
        }
      else
        {
          int len;
          unsigned char b;

          len = _dbus_string_get_length (&result);
          
          b = _dbus_string_get_byte (&result, len - 1);

          b |= val;

          _dbus_string_set_byte (&result, len - 1, b);
        }

      high_bits = !high_bits;

      ++p;
    }

  if (!_dbus_string_move (&result, 0, dest, insert_at))
    goto out;

  retval = TRUE;
  
 out:
  _dbus_string_free (&result);  
  return retval;
}

/**
 * Checks that the given range of the string is valid ASCII with no
 * nul bytes. If the given range is not contained in the string,
 * returns #FALSE.
 *
 * @param str the string
 * @param start first byte index to check
 * @param len number of bytes to check
 * @returns #TRUE if the byte range exists and is all valid ASCII
 */
dbus_bool_t
_dbus_string_validate_ascii (const DBusString *str,
                             int               start,
                             int               len)
{
  const unsigned char *s;
  const unsigned char *end;
  DBUS_CONST_STRING_PREAMBLE (str);
  _dbus_assert (start >= 0);
  _dbus_assert (len >= 0);
  
  if ((start + len) > real->len)
    return FALSE;
  
  s = real->str + start;
  end = s + len;
  while (s != end)
    {
      if (*s == '\0' ||
          ((*s & ~0x7f) != 0))
        return FALSE;
        
      ++s;
    }
  
  return TRUE;
}

/**
 * Checks that the given range of the string
 * is valid UTF-8. If the given range is not contained
 * in the string, returns #FALSE. If the string
 * contains any nul bytes in the given range, returns
 * #FALSE.
 *
 * @todo right now just calls _dbus_string_validate_ascii()
 * 
 * @param str the string
 * @param start first byte index to check
 * @param len number of bytes to check
 * @returns #TRUE if the byte range exists and is all valid UTF-8
 */
dbus_bool_t
_dbus_string_validate_utf8  (const DBusString *str,
                             int               start,
                             int               len)
{
  /* FIXME actually validate UTF-8 */
  return _dbus_string_validate_ascii (str, start, len);
}

/**
 * Checks that the given range of the string
 * is all nul bytes. If the given range is
 * not contained in the string, returns #FALSE.
 * 
 * @param str the string
 * @param start first byte index to check
 * @param len number of bytes to check
 * @returns #TRUE if the byte range exists and is all nul bytes
 */
dbus_bool_t
_dbus_string_validate_nul (const DBusString *str,
                           int               start,
                           int               len)
{
  const unsigned char *s;
  const unsigned char *end;
  DBUS_CONST_STRING_PREAMBLE (str);
  _dbus_assert (start >= 0);
  _dbus_assert (len >= 0);
  
  if ((start + len) > real->len)
    return FALSE;
  
  s = real->str + start;
  end = s + len;
  while (s != end)
    {
      if (*s != '\0')
        return FALSE;
      ++s;
    }
  
  return TRUE;
}

/**
 * Clears all allocated bytes in the string to zero.
 *
 * @param str the string
 */
void
_dbus_string_zero (DBusString *str)
{
  DBUS_STRING_PREAMBLE (str);

  memset (real->str, '\0', real->allocated);
}

/** @} */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>

static void
test_max_len (DBusString *str,
              int         max_len)
{
  if (max_len > 0)
    {
      if (!_dbus_string_set_length (str, max_len - 1))
        _dbus_assert_not_reached ("setting len to one less than max should have worked");
    }

  if (!_dbus_string_set_length (str, max_len))
    _dbus_assert_not_reached ("setting len to max len should have worked");

  if (_dbus_string_set_length (str, max_len + 1))
    _dbus_assert_not_reached ("setting len to one more than max len should not have worked");

  if (!_dbus_string_set_length (str, 0))
    _dbus_assert_not_reached ("setting len to zero should have worked");
}

static void
test_base64_roundtrip (const unsigned char *data,
                       int                  len)
{
  DBusString orig;
  DBusString encoded;
  DBusString decoded;

  if (len < 0)
    len = strlen (data);
  
  if (!_dbus_string_init (&orig, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("could not init string");

  if (!_dbus_string_init (&encoded, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("could not init string");
  
  if (!_dbus_string_init (&decoded, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("could not init string");

  if (!_dbus_string_append_len (&orig, data, len))
    _dbus_assert_not_reached ("couldn't append orig data");

  if (!_dbus_string_base64_encode (&orig, 0, &encoded, 0))
    _dbus_assert_not_reached ("could not encode");

  if (!_dbus_string_base64_decode (&encoded, 0, &decoded, 0))
    _dbus_assert_not_reached ("could not decode");

  if (!_dbus_string_equal (&orig, &decoded))
    {
      const char *s;
      
      printf ("Original string %d bytes encoded %d bytes decoded %d bytes\n",
              _dbus_string_get_length (&orig),
              _dbus_string_get_length (&encoded),
              _dbus_string_get_length (&decoded));
      printf ("Original: %s\n", data);
      _dbus_string_get_const_data (&decoded, &s);
      printf ("Decoded: %s\n", s);
      _dbus_assert_not_reached ("original string not the same as string decoded from base64");
    }
  
  _dbus_string_free (&orig);
  _dbus_string_free (&encoded);
  _dbus_string_free (&decoded);  
}

static void
test_hex_roundtrip (const unsigned char *data,
                    int                  len)
{
  DBusString orig;
  DBusString encoded;
  DBusString decoded;

  if (len < 0)
    len = strlen (data);
  
  if (!_dbus_string_init (&orig, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("could not init string");

  if (!_dbus_string_init (&encoded, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("could not init string");
  
  if (!_dbus_string_init (&decoded, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("could not init string");

  if (!_dbus_string_append_len (&orig, data, len))
    _dbus_assert_not_reached ("couldn't append orig data");

  if (!_dbus_string_hex_encode (&orig, 0, &encoded, 0))
    _dbus_assert_not_reached ("could not encode");

  if (!_dbus_string_hex_decode (&encoded, 0, &decoded, 0))
    _dbus_assert_not_reached ("could not decode");
    
  if (!_dbus_string_equal (&orig, &decoded))
    {
      const char *s;
      
      printf ("Original string %d bytes encoded %d bytes decoded %d bytes\n",
              _dbus_string_get_length (&orig),
              _dbus_string_get_length (&encoded),
              _dbus_string_get_length (&decoded));
      printf ("Original: %s\n", data);
      _dbus_string_get_const_data (&decoded, &s);
      printf ("Decoded: %s\n", s);
      _dbus_assert_not_reached ("original string not the same as string decoded from base64");
    }
  
  _dbus_string_free (&orig);
  _dbus_string_free (&encoded);
  _dbus_string_free (&decoded);  
}

typedef void (* TestRoundtripFunc) (const unsigned char *data,
                                    int                  len);
static void
test_roundtrips (TestRoundtripFunc func)
{
  (* func) ("Hello this is a string\n", -1);
  (* func) ("Hello this is a string\n1", -1);
  (* func) ("Hello this is a string\n12", -1);
  (* func) ("Hello this is a string\n123", -1);
  (* func) ("Hello this is a string\n1234", -1);
  (* func) ("Hello this is a string\n12345", -1);
  (* func) ("", 0);
  (* func) ("1", 1);
  (* func) ("12", 2);
  (* func) ("123", 3);
  (* func) ("1234", 4);
  (* func) ("12345", 5);
  (* func) ("", 1);
  (* func) ("1", 2);
  (* func) ("12", 3);
  (* func) ("123", 4);
  (* func) ("1234", 5);
  (* func) ("12345", 6);
  {
    unsigned char buf[512];
    int i;
    
    i = 0;
    while (i < _DBUS_N_ELEMENTS (buf))
      {
        buf[i] = i;
        ++i;
      }
    i = 0;
    while (i < _DBUS_N_ELEMENTS (buf))
      {
        (* func) (buf, i);
        ++i;
      }
  }
}


/**
 * @ingroup DBusStringInternals
 * Unit test for DBusString.
 *
 * @todo Need to write tests for _dbus_string_copy() and
 * _dbus_string_move() moving to/from each of start/middle/end of a
 * string. Also need tests for _dbus_string_move_len ()
 * 
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_string_test (void)
{
  DBusString str;
  DBusString other;
  int i, end;
  long v;
  double d;
  int lens[] = { 0, 1, 2, 3, 4, 5, 10, 16, 17, 18, 25, 31, 32, 33, 34, 35, 63, 64, 65, 66, 67, 68, 69, 70, 71, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136 };
  char *s;
  dbus_unichar_t ch;
  
  i = 0;
  while (i < _DBUS_N_ELEMENTS (lens))
    {
      if (!_dbus_string_init (&str, lens[i]))
        _dbus_assert_not_reached ("failed to init string");
      
      test_max_len (&str, lens[i]);
      _dbus_string_free (&str);

      ++i;
    }

  /* Test shortening and setting length */
  i = 0;
  while (i < _DBUS_N_ELEMENTS (lens))
    {
      int j;
      
      if (!_dbus_string_init (&str, lens[i]))
        _dbus_assert_not_reached ("failed to init string");
      
      if (!_dbus_string_set_length (&str, lens[i]))
        _dbus_assert_not_reached ("failed to set string length");

      j = lens[i];
      while (j > 0)
        {
          _dbus_assert (_dbus_string_get_length (&str) == j);
          if (j > 0)
            {
              _dbus_string_shorten (&str, 1);
              _dbus_assert (_dbus_string_get_length (&str) == (j - 1));
            }
          --j;
        }
      
      _dbus_string_free (&str);

      ++i;
    }

  /* Test appending data */
  if (!_dbus_string_init (&str, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("failed to init string");

  i = 0;
  while (i < 10)
    {
      if (!_dbus_string_append (&str, "a"))
        _dbus_assert_not_reached ("failed to append string to string\n");

      _dbus_assert (_dbus_string_get_length (&str) == i * 2 + 1);

      if (!_dbus_string_append_byte (&str, 'b'))
        _dbus_assert_not_reached ("failed to append byte to string\n");

      _dbus_assert (_dbus_string_get_length (&str) == i * 2 + 2);
                    
      ++i;
    }

  _dbus_string_free (&str);

  /* Check steal_data */
  
  if (!_dbus_string_init (&str, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("failed to init string");

  if (!_dbus_string_append (&str, "Hello World"))
    _dbus_assert_not_reached ("could not append to string");

  i = _dbus_string_get_length (&str);
  
  if (!_dbus_string_steal_data (&str, &s))
    _dbus_assert_not_reached ("failed to steal data");

  _dbus_assert (_dbus_string_get_length (&str) == 0);
  _dbus_assert (((int)strlen (s)) == i);

  dbus_free (s);

  /* Check move */
  
  if (!_dbus_string_append (&str, "Hello World"))
    _dbus_assert_not_reached ("could not append to string");

  i = _dbus_string_get_length (&str);

  if (!_dbus_string_init (&other, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("could not init string");
  
  if (!_dbus_string_move (&str, 0, &other, 0))
    _dbus_assert_not_reached ("could not move");

  _dbus_assert (_dbus_string_get_length (&str) == 0);
  _dbus_assert (_dbus_string_get_length (&other) == i);

  if (!_dbus_string_append (&str, "Hello World"))
    _dbus_assert_not_reached ("could not append to string");
  
  if (!_dbus_string_move (&str, 0, &other, _dbus_string_get_length (&other)))
    _dbus_assert_not_reached ("could not move");

  _dbus_assert (_dbus_string_get_length (&str) == 0);
  _dbus_assert (_dbus_string_get_length (&other) == i * 2);

    if (!_dbus_string_append (&str, "Hello World"))
    _dbus_assert_not_reached ("could not append to string");
  
  if (!_dbus_string_move (&str, 0, &other, _dbus_string_get_length (&other) / 2))
    _dbus_assert_not_reached ("could not move");

  _dbus_assert (_dbus_string_get_length (&str) == 0);
  _dbus_assert (_dbus_string_get_length (&other) == i * 3);
  
  _dbus_string_free (&other);

  /* Check copy */
  
  if (!_dbus_string_append (&str, "Hello World"))
    _dbus_assert_not_reached ("could not append to string");

  i = _dbus_string_get_length (&str);
  
  if (!_dbus_string_init (&other, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("could not init string");
  
  if (!_dbus_string_copy (&str, 0, &other, 0))
    _dbus_assert_not_reached ("could not copy");

  _dbus_assert (_dbus_string_get_length (&str) == i);
  _dbus_assert (_dbus_string_get_length (&other) == i);

  if (!_dbus_string_copy (&str, 0, &other, _dbus_string_get_length (&other)))
    _dbus_assert_not_reached ("could not copy");

  _dbus_assert (_dbus_string_get_length (&str) == i);
  _dbus_assert (_dbus_string_get_length (&other) == i * 2);
  _dbus_assert (_dbus_string_equal_c_str (&other,
                                          "Hello WorldHello World"));

  if (!_dbus_string_copy (&str, 0, &other, _dbus_string_get_length (&other) / 2))
    _dbus_assert_not_reached ("could not copy");

  _dbus_assert (_dbus_string_get_length (&str) == i);
  _dbus_assert (_dbus_string_get_length (&other) == i * 3);
  _dbus_assert (_dbus_string_equal_c_str (&other,
                                          "Hello WorldHello WorldHello World"));
  
  _dbus_string_free (&str);
  _dbus_string_free (&other);

  /* Check replace */

  if (!_dbus_string_init (&str, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("failed to init string");
  
  if (!_dbus_string_append (&str, "Hello World"))
    _dbus_assert_not_reached ("could not append to string");

  i = _dbus_string_get_length (&str);
  
  if (!_dbus_string_init (&other, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("could not init string");
  
  if (!_dbus_string_replace_len (&str, 0, _dbus_string_get_length (&str),
                                 &other, 0, _dbus_string_get_length (&other)))
    _dbus_assert_not_reached ("could not replace");

  _dbus_assert (_dbus_string_get_length (&str) == i);
  _dbus_assert (_dbus_string_get_length (&other) == i);
  _dbus_assert (_dbus_string_equal_c_str (&other, "Hello World"));
  
  if (!_dbus_string_replace_len (&str, 0, _dbus_string_get_length (&str),
                                 &other, 5, 1))
    _dbus_assert_not_reached ("could not replace center space");

  _dbus_assert (_dbus_string_get_length (&str) == i);
  _dbus_assert (_dbus_string_get_length (&other) == i * 2 - 1);
  _dbus_assert (_dbus_string_equal_c_str (&other,
                                          "HelloHello WorldWorld"));

  
  if (!_dbus_string_replace_len (&str, 1, 1,
                                 &other,
                                 _dbus_string_get_length (&other) - 1,
                                 1))
    _dbus_assert_not_reached ("could not replace end character");
  
  _dbus_assert (_dbus_string_get_length (&str) == i);
  _dbus_assert (_dbus_string_get_length (&other) == i * 2 - 1);
  _dbus_assert (_dbus_string_equal_c_str (&other,
                                          "HelloHello WorldWorle"));
  
  _dbus_string_free (&str);
  _dbus_string_free (&other);
  
  /* Check append/get unichar */
  
  if (!_dbus_string_init (&str, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("failed to init string");

  ch = 0;
  if (!_dbus_string_append_unichar (&str, 0xfffc))
    _dbus_assert_not_reached ("failed to append unichar");

  _dbus_string_get_unichar (&str, 0, &ch, &i);

  _dbus_assert (ch == 0xfffc);
  _dbus_assert (i == _dbus_string_get_length (&str));

  _dbus_string_free (&str);

  /* Check insert/set/get byte */
  
  if (!_dbus_string_init (&str, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("failed to init string");

  if (!_dbus_string_append (&str, "Hello"))
    _dbus_assert_not_reached ("failed to append Hello");

  _dbus_assert (_dbus_string_get_byte (&str, 0) == 'H');
  _dbus_assert (_dbus_string_get_byte (&str, 1) == 'e');
  _dbus_assert (_dbus_string_get_byte (&str, 2) == 'l');
  _dbus_assert (_dbus_string_get_byte (&str, 3) == 'l');
  _dbus_assert (_dbus_string_get_byte (&str, 4) == 'o');

  _dbus_string_set_byte (&str, 1, 'q');
  _dbus_assert (_dbus_string_get_byte (&str, 1) == 'q');

  if (!_dbus_string_insert_byte (&str, 0, 255))
    _dbus_assert_not_reached ("can't insert byte");

  if (!_dbus_string_insert_byte (&str, 2, 'Z'))
    _dbus_assert_not_reached ("can't insert byte");

  if (!_dbus_string_insert_byte (&str, _dbus_string_get_length (&str), 'W'))
    _dbus_assert_not_reached ("can't insert byte");
  
  _dbus_assert (_dbus_string_get_byte (&str, 0) == 255);
  _dbus_assert (_dbus_string_get_byte (&str, 1) == 'H');
  _dbus_assert (_dbus_string_get_byte (&str, 2) == 'Z');
  _dbus_assert (_dbus_string_get_byte (&str, 3) == 'q');
  _dbus_assert (_dbus_string_get_byte (&str, 4) == 'l');
  _dbus_assert (_dbus_string_get_byte (&str, 5) == 'l');
  _dbus_assert (_dbus_string_get_byte (&str, 6) == 'o');
  _dbus_assert (_dbus_string_get_byte (&str, 7) == 'W');

  _dbus_string_free (&str);
  
  /* Check append/parse int/double */
  
  if (!_dbus_string_init (&str, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("failed to init string");

  if (!_dbus_string_append_int (&str, 27))
    _dbus_assert_not_reached ("failed to append int");

  i = _dbus_string_get_length (&str);

  if (!_dbus_string_parse_int (&str, 0, &v, &end))
    _dbus_assert_not_reached ("failed to parse int");

  _dbus_assert (v == 27);
  _dbus_assert (end == i);

  _dbus_string_free (&str);
  
  if (!_dbus_string_init (&str, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("failed to init string");
  
  if (!_dbus_string_append_double (&str, 50.3))
    _dbus_assert_not_reached ("failed to append float");

  i = _dbus_string_get_length (&str);

  if (!_dbus_string_parse_double (&str, 0, &d, &end))
    _dbus_assert_not_reached ("failed to parse float");

  _dbus_assert (d > (50.3 - 1e-6) && d < (50.3 + 1e-6));
  _dbus_assert (end == i);

  _dbus_string_free (&str);

  /* Test find */
  if (!_dbus_string_init (&str, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("failed to init string");

  if (!_dbus_string_append (&str, "Hello"))
    _dbus_assert_not_reached ("couldn't append to string");
  
  if (!_dbus_string_find (&str, 0, "He", &i))
    _dbus_assert_not_reached ("didn't find 'He'");
  _dbus_assert (i == 0);

  if (!_dbus_string_find (&str, 0, "Hello", &i))
    _dbus_assert_not_reached ("didn't find 'Hello'");
  _dbus_assert (i == 0);
  
  if (!_dbus_string_find (&str, 0, "ello", &i))
    _dbus_assert_not_reached ("didn't find 'ello'");
  _dbus_assert (i == 1);

  if (!_dbus_string_find (&str, 0, "lo", &i))
    _dbus_assert_not_reached ("didn't find 'lo'");
  _dbus_assert (i == 3);

  if (!_dbus_string_find (&str, 2, "lo", &i))
    _dbus_assert_not_reached ("didn't find 'lo'");
  _dbus_assert (i == 3);

  if (_dbus_string_find (&str, 4, "lo", &i))
    _dbus_assert_not_reached ("did find 'lo'");
  
  if (!_dbus_string_find (&str, 0, "l", &i))
    _dbus_assert_not_reached ("didn't find 'l'");
  _dbus_assert (i == 2);

  if (!_dbus_string_find (&str, 0, "H", &i))
    _dbus_assert_not_reached ("didn't find 'H'");
  _dbus_assert (i == 0);

  if (!_dbus_string_find (&str, 0, "", &i))
    _dbus_assert_not_reached ("didn't find ''");
  _dbus_assert (i == 0);
  
  if (_dbus_string_find (&str, 0, "Hello!", NULL))
    _dbus_assert_not_reached ("Did find 'Hello!'");

  if (_dbus_string_find (&str, 0, "Oh, Hello", NULL))
    _dbus_assert_not_reached ("Did find 'Oh, Hello'");
  
  if (_dbus_string_find (&str, 0, "ill", NULL))
    _dbus_assert_not_reached ("Did find 'ill'");

  if (_dbus_string_find (&str, 0, "q", NULL))
    _dbus_assert_not_reached ("Did find 'q'");

  if (!_dbus_string_find_to (&str, 0, 2, "He", NULL))
    _dbus_assert_not_reached ("Didn't find 'He'");

  if (_dbus_string_find_to (&str, 0, 2, "Hello", NULL))
    _dbus_assert_not_reached ("Did find 'Hello'");
  
  _dbus_string_free (&str);

  /* Base 64 and Hex encoding */
  test_roundtrips (test_base64_roundtrip);
  test_roundtrips (test_hex_roundtrip);
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
