/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-memory.c  D-BUS memory handling
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

#include "dbus-memory.h"
#include <stdlib.h>

/**
 * @defgroup Memory Memory Allocation
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
 * @returns the new memory block or NULL on failure
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
 * @returns the new memory block or NULL on failure
 */

/**
 * Allocates the given number of bytes, as with standard
 * malloc(). Guaranteed to return NULL if bytes is zero
 * on all platforms. Returns NULL if the allocation fails.
 * The memory must be released with dbus_free().
 *
 * @param bytes number of bytes to allocate
 * @return allocated memory, or NULL if the allocation fails.
 */
void*
dbus_malloc (size_t bytes)
{
  if (bytes == 0) /* some system mallocs handle this, some don't */
    return NULL;
  else
    return malloc (bytes);
}

/**
 * Allocates the given number of bytes, as with standard malloc(), but
 * all bytes are initialized to zero as with calloc(). Guaranteed to
 * return NULL if bytes is zero on all platforms. Returns NULL if the
 * allocation fails.  The memory must be released with dbus_free().
 *
 * @param bytes number of bytes to allocate
 * @return allocated memory, or NULL if the allocation fails.
 */
void*
dbus_malloc0 (size_t bytes)
{
  if (bytes == 0)
    return NULL;
  else
    return calloc (bytes, 1);
}

/**
 * Resizes a block of memory previously allocated by dbus_malloc() or
 * dbus_malloc0(). Guaranteed to free the memory and return NULL if bytes
 * is zero on all platforms. Returns NULL if the resize fails.
 * If the resize fails, the memory is not freed.
 *
 * @param memory block to be resized
 * @param bytes new size of the memory block
 * @return allocated memory, or NULL if the resize fails.
 */
void*
dbus_realloc (void  *memory,
              size_t bytes)
{
  if (bytes == 0) /* guarantee this is safe */
    {
      dbus_free (memory);
      return NULL;
    }
  else
    {
      return realloc (memory, bytes);
    }
}

/**
 * Frees a block of memory previously allocated by dbus_malloc() or
 * dbus_malloc0().
 * 
 * @param memory block to be freed
 */
void
dbus_free (void  *memory)
{
  if (memory) /* we guarantee it's safe to free (NULL) */
    free (memory);
}

/** @} */
