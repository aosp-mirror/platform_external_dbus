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

#ifndef DBUS_INTERNALS_H
#define DBUS_INTERNALS_H

#include <config.h>

#include <dbus/dbus-memory.h>
#include <dbus/dbus-types.h>
#include <stdlib.h> /* for abort() */
#include <string.h> /* just so it's there in every file */

DBUS_BEGIN_DECLS;

void _dbus_warn (const char *format,
                 ...);

#define _dbus_assert(condition)                                         \
do {                                                                    \
  if (!(condition))                                                     \
    {                                                                   \
      _dbus_warn ("Assertion failed \"%s\" file \"%s\" line %d\n",      \
                  #condition, __FILE__, __LINE__);                      \
      abort ();                                                         \
    }                                                                   \
} while (0)

#define _dbus_assert_not_reached(explanation)                                   \
do {                                                                            \
    _dbus_warn ("File \"%s\" line %d should not have been reached: %s\n",       \
               __FILE__, __LINE__, (explanation));                              \
    abort ();                                                                   \
} while (0)

#define _DBUS_N_ELEMENTS(array) (sizeof ((array)) / sizeof ((array)[0]))

#define _DBUS_POINTER_TO_INT(pointer) ((long)(pointer))
#define _DBUS_INT_TO_POINTER(integer) ((void*)((long)(integer)))

char* _dbus_strdup (const char *str);

DBUS_END_DECLS;

#endif /* DBUS_INTERNALS_H */
