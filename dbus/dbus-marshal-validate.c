/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-marshal-validate.c Validation routines for marshaled data
 *
 * Copyright (C) 2005 Red Hat, Inc.
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
#include "dbus-marshal-validate.h"

/**
 * @addtogroup DBusMarshal
 *
 * @{
 */

/** @} */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>

dbus_bool_t
_dbus_marshal_validate_test (void)
{

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
