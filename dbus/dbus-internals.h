/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-internals.h  random utility stuff (internal to D-BUS implementation)
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
#ifdef DBUS_INSIDE_DBUS_H
#error "You can't include dbus-internals.h in the public header dbus.h"
#endif

#ifndef DBUS_INTERNALS_H
#define DBUS_INTERNALS_H

#include <config.h>

#include <dbus/dbus-memory.h>
#include <dbus/dbus-types.h>
#include <dbus/dbus-errors.h>
#include <dbus/dbus-sysdeps.h>

DBUS_BEGIN_DECLS;

void _dbus_warn    (const char *format,
                    ...);
void _dbus_verbose (const char *format,
                    ...);

const char* _dbus_strerror (int error_number);

DBusResultCode _dbus_result_from_errno (int error_number);

#define _dbus_assert(condition)                                         \
do {                                                                    \
  if (!(condition))                                                     \
    {                                                                   \
      _dbus_warn ("Assertion failed \"%s\" file \"%s\" line %d\n",      \
                  #condition, __FILE__, __LINE__);                      \
      _dbus_abort ();                                                   \
    }                                                                   \
} while (0)

#define _dbus_assert_not_reached(explanation)                                   \
do {                                                                            \
    _dbus_warn ("File \"%s\" line %d should not have been reached: %s\n",       \
               __FILE__, __LINE__, (explanation));                              \
    _dbus_abort ();                                                             \
} while (0)

#define _DBUS_N_ELEMENTS(array) ((int) (sizeof ((array)) / sizeof ((array)[0])))

#define _DBUS_POINTER_TO_INT(pointer) ((long)(pointer))
#define _DBUS_INT_TO_POINTER(integer) ((void*)((long)(integer)))

#define _DBUS_ZERO(object) (memset (&(object), '\0', sizeof ((object))))

#define _DBUS_STRUCT_OFFSET(struct_type, member)	\
    ((long) ((unsigned char*) &((struct_type*) 0)->member))

/* This alignment thing is from ORBit2 */
/* Align a value upward to a boundary, expressed as a number of bytes.
 * E.g. align to an 8-byte boundary with argument of 8.
 */

/*
 *   (this + boundary - 1)
 *          &
 *    ~(boundary - 1)
 */

#define _DBUS_ALIGN_VALUE(this, boundary) \
  (( ((unsigned long)(this)) + (((unsigned long)(boundary)) -1)) & (~(((unsigned long)(boundary))-1)))

#define _DBUS_ALIGN_ADDRESS(this, boundary) \
  ((void*)_DBUS_ALIGN_VALUE(this, boundary))

char* _dbus_strdup (const char *str);

#define _DBUS_INT_MIN	(-_DBUS_INT_MAX - 1)
#define _DBUS_INT_MAX	2147483647
#define _DBUS_MAX_SUN_PATH_LENGTH 99
#define _DBUS_ONE_KILOBYTE 1024
#define _DBUS_ONE_MEGABYTE 1024 * _DBUS_ONE_KILOBYTE

#undef	MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#undef	MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

#undef	ABS
#define ABS(a)	   (((a) < 0) ? -(a) : (a))

typedef void (* DBusForeachFunction) (void *element,
                                      void *data);

dbus_bool_t _dbus_set_fd_nonblocking (int             fd,
                                      DBusResultCode *result);

void _dbus_verbose_bytes           (const unsigned char *data,
                                    int                  len);
void _dbus_verbose_bytes_of_string (const DBusString    *str,
                                    int                  start,
                                    int                  len);


const char* _dbus_type_to_string (int type);

DBUS_END_DECLS;

#endif /* DBUS_INTERNALS_H */
