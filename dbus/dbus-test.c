/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-test.c  Program to run all tests
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

#include "dbus-types.h"
#include "dbus-test.h"
#include <stdio.h>

int
main (int    argc,
      char **argv)
{
  printf ("%s: running linked list tests\n", argv[0]);
  if (!_dbus_list_test ())
    return 1;

  printf ("%s: running hash table tests\n", argv[0]);
  if (!_dbus_hash_test ())
    return 1;
  
  printf ("%s: completed successfully\n", argv[0]);
  return 0;
}
