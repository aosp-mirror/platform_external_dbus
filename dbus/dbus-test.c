/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-test.c  Program to run all tests
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

#include <config.h>
#include "dbus-test.h"
#include "dbus-sysdeps.h"
#include <stdio.h>
#include <stdlib.h>

static void
die (const char *failure)
{
  fprintf (stderr, "Unit test failed: %s\n", failure);
  exit (1);
}

/**
 * An exported symbol to be run in order to execute
 * unit tests. Should not be used by
 * any app other than our test app, this symbol
 * won't exist in some builds of the library.
 * (with --enable-tests=no)
 *
 * @param test_data_dir the directory with test data (test/data normally)
 */
void
dbus_internal_do_not_use_run_tests (const char *test_data_dir)
{
#ifdef DBUS_BUILD_TESTS
  if (test_data_dir == NULL)
    test_data_dir = _dbus_getenv ("DBUS_TEST_DATA");

  if (test_data_dir != NULL)
    printf ("Test data in %s\n", test_data_dir);
  else
    printf ("No test data!\n");
  
  printf ("%s: running string tests\n", "dbus-test");
  if (!_dbus_string_test ())
    die ("strings");

  printf ("%s: running md5 tests\n", "dbus-test");
  if (!_dbus_md5_test ())
    die ("md5");

  printf ("%s: running SHA-1 tests\n", "dbus-test");
  if (!_dbus_sha_test (test_data_dir))
    die ("SHA-1");
  
  printf ("%s: running auth tests\n", "dbus-test");
  if (!_dbus_auth_test (test_data_dir))
    die ("auth");
  
  printf ("%s: running address parse tests\n", "dbus-test");
  if (!_dbus_address_test ())
    die ("address parsing");
  
  printf ("%s: running marshalling tests\n", "dbus-test");
  if (!_dbus_marshal_test ())
    die ("marshalling");

  printf ("%s: running message tests\n", "dbus-test");
  if (!_dbus_message_test (test_data_dir))
    die ("messages");

  printf ("%s: running memory pool tests\n", "dbus-test");
  if (!_dbus_mem_pool_test ())
    die ("memory pools");

  printf ("%s: running linked list tests\n", "dbus-test");
  if (!_dbus_list_test ())
    die ("lists");

  printf ("%s: running hash table tests\n", "dbus-test");
  if (!_dbus_hash_test ())
    die ("hash tables");
  
  printf ("%s: completed successfully\n", "dbus-test");
#else
  printf ("Not compiled with unit tests, not running any\n");
#endif
}


