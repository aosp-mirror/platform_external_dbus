/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-memory.c  D-BUS memory handling
 *
 * Copyright (C) 2002, 2003  Red Hat Inc.
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

#include "dbus-memory.h"
#include "dbus-internals.h"
#include <stdlib.h>


/**
 * @defgroup DBusMemory Memory Allocation
 * @ingroup  DBus
 * @brief dbus_malloc(), dbus_free(), etc.
 *
 * Functions and macros related to allocating and releasing
 * blocks of memory.
 *
 * @{
 */

/**
 * @def dbus_new
 *
 * Safe macro for using dbus_malloc(). Accepts the type
 * to allocate and the number of type instances to
 * allocate as arguments, and returns a memory block
 * cast to the desired type, instead of as a void*.
 *
 * @param type type name to allocate
 * @param count number of instances in the allocated array
 * @returns the new memory block or #NULL on failure
 */

/**
 * @def dbus_new0
 *
 * Safe macro for using dbus_malloc0(). Accepts the type
 * to allocate and the number of type instances to
 * allocate as arguments, and returns a memory block
 * cast to the desired type, instead of as a void*.
 * The allocated array is initialized to all-bits-zero.
 *
 * @param type type name to allocate
 * @param count number of instances in the allocated array
 * @returns the new memory block or #NULL on failure
 */

/**
 * @typedef DBusFreeFunction
 *
 * The type of a function which frees a block of memory.
 *
 * @param memory the memory to free
 */

#ifdef DBUS_BUILD_TESTS
static dbus_bool_t inited = FALSE;
static int fail_counts = -1;
static size_t fail_size = 0;
static dbus_bool_t guards = FALSE;
/** value stored in guard padding for debugging buffer overrun */
#define GUARD_VALUE 0xdeadbeef
/** size of the information about the block stored in guard mode */
#define GUARD_INFO_SIZE 8
/** size of the GUARD_VALUE-filled padding after the header info  */
#define GUARD_START_PAD 16
/** size of the GUARD_VALUE-filled padding at the end of the block */
#define GUARD_END_PAD 16
/** size of stuff at start of block */
#define GUARD_START_OFFSET (GUARD_START_PAD + GUARD_INFO_SIZE)
/** total extra size over the requested allocation for guard stuff */
#define GUARD_EXTRA_SIZE (GUARD_START_OFFSET + GUARD_END_PAD)
#endif

#ifdef DBUS_BUILD_TESTS
static void
initialize_malloc_debug (void)
{
  if (!inited)
    {
      if (_dbus_getenv ("DBUS_MALLOC_FAIL_NTH") != NULL)
	{
	  fail_counts = atoi (_dbus_getenv ("DBUS_MALLOC_FAIL_NTH"));
	  _dbus_set_fail_alloc_counter (fail_counts);
	}
      
      if (_dbus_getenv ("DBUS_MALLOC_FAIL_GREATER_THAN") != NULL)
	fail_size = atoi (_dbus_getenv ("DBUS_MALLOC_FAIL_GREATER_THAN"));

      if (_dbus_getenv ("DBUS_MALLOC_GUARDS") != NULL)
        guards = TRUE;
      
      inited = TRUE;
    }
}

/**
 * Where the block came from.
 */
typedef enum
{
  SOURCE_UNKNOWN,
  SOURCE_MALLOC,
  SOURCE_REALLOC,
  SOURCE_MALLOC_ZERO,
  SOURCE_REALLOC_NULL
} BlockSource;

static const char*
source_string (BlockSource source)
{
  switch (source)
    {
    case SOURCE_UNKNOWN:
      return "unknown";
    case SOURCE_MALLOC:
      return "malloc";
    case SOURCE_REALLOC:
      return "realloc";
    case SOURCE_MALLOC_ZERO:
      return "malloc0";
    case SOURCE_REALLOC_NULL:
      return "realloc(NULL)";
    }
  _dbus_assert_not_reached ("Invalid malloc block source ID");
  return "invalid!";
}

static void
check_guards (void *free_block)
{
  if (free_block != NULL)
    {
      unsigned char *block = ((unsigned char*)free_block) - GUARD_START_OFFSET;
      size_t requested_bytes = *(dbus_uint32_t*)block;
      BlockSource source = *(dbus_uint32_t*)(block + 4);
      unsigned int i;
      dbus_bool_t failed;

      failed = FALSE;

#if 0
      _dbus_verbose ("Checking %d bytes request from source %s\n",
                     requested_bytes, source_string (source));
#endif
      
      i = GUARD_INFO_SIZE;
      while (i < GUARD_START_OFFSET)
        {
          dbus_uint32_t value = *(dbus_uint32_t*) &block[i];
          if (value != GUARD_VALUE)
            {
              _dbus_warn ("Block of %u bytes from %s had start guard value 0x%x at %d expected 0x%x\n",
                          requested_bytes, source_string (source),
                          value, i, GUARD_VALUE);
              failed = TRUE;
            }
          
          i += 4;
        }

      i = GUARD_START_OFFSET + requested_bytes;
      while (i < (GUARD_START_OFFSET + requested_bytes + GUARD_END_PAD))
        {
          dbus_uint32_t value = *(dbus_uint32_t*) &block[i];
          if (value != GUARD_VALUE)
            {
              _dbus_warn ("Block of %u bytes from %s had end guard value 0x%x at %d expected 0x%x\n",
                          requested_bytes, source_string (source),
                          value, i, GUARD_VALUE);
              failed = TRUE;
            }
          
          i += 4;
        }

      if (failed)
        _dbus_assert_not_reached ("guard value corruption");
    }
}

static void*
set_guards (void       *real_block,
            size_t      requested_bytes,
            BlockSource source)
{
  unsigned char *block = real_block;
  unsigned int i;
  
  if (block == NULL)
    return NULL;

  _dbus_assert (GUARD_START_OFFSET + GUARD_END_PAD == GUARD_EXTRA_SIZE);
  
  *((dbus_uint32_t*)block) = requested_bytes;
  *((dbus_uint32_t*)(block + 4)) = source;

  i = GUARD_INFO_SIZE;
  while (i < GUARD_START_OFFSET)
    {
      (*(dbus_uint32_t*) &block[i]) = GUARD_VALUE;
      
      i += 4;
    }

  i = GUARD_START_OFFSET + requested_bytes;
  while (i < (GUARD_START_OFFSET + requested_bytes + GUARD_END_PAD))
    {
      (*(dbus_uint32_t*) &block[i]) = GUARD_VALUE;
      
      i += 4;
    }
  
  check_guards (block + GUARD_START_OFFSET);
  
  return block + GUARD_START_OFFSET;
}

#endif

/**
 * Allocates the given number of bytes, as with standard
 * malloc(). Guaranteed to return #NULL if bytes is zero
 * on all platforms. Returns #NULL if the allocation fails.
 * The memory must be released with dbus_free().
 *
 * @param bytes number of bytes to allocate
 * @return allocated memory, or #NULL if the allocation fails.
 */
void*
dbus_malloc (size_t bytes)
{
#ifdef DBUS_BUILD_TESTS
  initialize_malloc_debug ();
  
  if (_dbus_decrement_fail_alloc_counter ())
    {
      if (fail_counts != -1)
	_dbus_set_fail_alloc_counter (fail_counts);

      _dbus_verbose (" FAILING malloc of %d bytes\n", bytes);
      
      return NULL;
    }
#endif
  
  if (bytes == 0) /* some system mallocs handle this, some don't */
    return NULL;
#if DBUS_BUILD_TESTS
  else if (fail_size != 0 && bytes > fail_size)
    return NULL;
  else if (guards)
    {
      void *block;

      block = malloc (bytes + GUARD_EXTRA_SIZE);
      return set_guards (block, bytes, SOURCE_MALLOC);
    }
#endif
  else
    return malloc (bytes);
}

/**
 * Allocates the given number of bytes, as with standard malloc(), but
 * all bytes are initialized to zero as with calloc(). Guaranteed to
 * return #NULL if bytes is zero on all platforms. Returns #NULL if the
 * allocation fails.  The memory must be released with dbus_free().
 *
 * @param bytes number of bytes to allocate
 * @return allocated memory, or #NULL if the allocation fails.
 */
void*
dbus_malloc0 (size_t bytes)
{
#ifdef DBUS_BUILD_TESTS
  initialize_malloc_debug ();
  
  if (_dbus_decrement_fail_alloc_counter ())
    {
      if (fail_counts != -1)
	_dbus_set_fail_alloc_counter (fail_counts);

      _dbus_verbose (" FAILING malloc0 of %d bytes\n", bytes);
      
      return NULL;
    }
#endif

  if (bytes == 0)
    return NULL;
#if DBUS_BUILD_TESTS
  else if (fail_size != 0 && bytes > fail_size)
    return NULL;
  else if (guards)
    {
      void *block;

      block = calloc (bytes + GUARD_EXTRA_SIZE, 1);
      return set_guards (block, bytes, SOURCE_MALLOC_ZERO);
    }
#endif
  else
    return calloc (bytes, 1);
}

/**
 * Resizes a block of memory previously allocated by dbus_malloc() or
 * dbus_malloc0(). Guaranteed to free the memory and return #NULL if bytes
 * is zero on all platforms. Returns #NULL if the resize fails.
 * If the resize fails, the memory is not freed.
 *
 * @param memory block to be resized
 * @param bytes new size of the memory block
 * @return allocated memory, or #NULL if the resize fails.
 */
void*
dbus_realloc (void  *memory,
              size_t bytes)
{
#ifdef DBUS_BUILD_TESTS
  initialize_malloc_debug ();
  
  if (_dbus_decrement_fail_alloc_counter ())
    {
      if (fail_counts != -1)
	_dbus_set_fail_alloc_counter (fail_counts);

      _dbus_verbose (" FAILING realloc of %d bytes\n", bytes);
      
      return NULL;
    }
#endif
  
  if (bytes == 0) /* guarantee this is safe */
    {
      dbus_free (memory);
      return NULL;
    }
#if DBUS_BUILD_TESTS
  else if (fail_size != 0 && bytes > fail_size)
    return NULL;
  else if (guards)
    {
      if (memory)
        {
          void *block;
          
          check_guards (memory);
          
          block = realloc (((unsigned char*)memory) - GUARD_START_OFFSET,
                           bytes + GUARD_EXTRA_SIZE);
          
          /* old guards shouldn't have moved */
          check_guards (((unsigned char*)block) + GUARD_START_OFFSET);
          
          return set_guards (block, bytes, SOURCE_REALLOC);
        }
      else
        {
          void *block;
          
          block = malloc (bytes + GUARD_EXTRA_SIZE);
          return set_guards (block, bytes, SOURCE_REALLOC_NULL);   
        }
    }
#endif
  else
    {
      return realloc (memory, bytes);
    }
}

/**
 * Frees a block of memory previously allocated by dbus_malloc() or
 * dbus_malloc0(). If passed #NULL, does nothing.
 * 
 * @param memory block to be freed
 */
void
dbus_free (void  *memory)
{
#ifdef DBUS_BUILD_TESTS
  if (guards)
    {
      check_guards (memory);
      if (memory)
        free (((unsigned char*)memory) - GUARD_START_OFFSET);
      return;
    }
#endif
    
  if (memory) /* we guarantee it's safe to free (NULL) */
    free (memory);
}

/**
 * Frees a #NULL-terminated array of strings.
 * If passed #NULL, does nothing.
 *
 * @param str_array the array to be freed
 */
void
dbus_free_string_array (char **str_array)
{
  if (str_array)
    {
      int i;

      i = 0;
      while (str_array[i])
	{
	  dbus_free (str_array[i]);
	  i++;
	}

      dbus_free (str_array);
    }
}

/** @} */
