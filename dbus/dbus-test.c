/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-test.c  Program to run all tests
 *
 * Copyright (C) 2002, 2003  Red Hat Inc.
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

#include <config.h>
#include "dbus-test.h"
#include "dbus-sysdeps.h"
#include "dbus-internals.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef DBUS_BUILD_TESTS
static void
die (const char *failure)
{
  fprintf (stderr, "Unit test failed: %s\n", failure);
  exit (1);
}

static void
check_memleaks (void)
{
  dbus_shutdown ();

  printf ("%s: checking for memleaks\n", "dbus-test");
  if (_dbus_get_malloc_blocks_outstanding () != 0)
    {
      _dbus_warn ("%d dbus_malloc blocks were not freed\n",
                  _dbus_get_malloc_blocks_outstanding ());
      die ("memleaks");
    }
}

#endif /* DBUS_BUILD_TESTS */

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
  if (!_dbus_threads_init_debug ())
    die ("debug threads init");
  
  if (test_data_dir == NULL)
    test_data_dir = _dbus_getenv ("DBUS_TEST_DATA");

  if (test_data_dir != NULL)
    printf ("Test data in %s\n", test_data_dir);
  else
    printf ("No test data!\n");

  printf ("%s: running string tests\n", "dbus-test");
  if (!_dbus_string_test ())
    die ("strings");

  check_memleaks ();
  
  printf ("%s: running sysdeps tests\n", "dbus-test");
  if (!_dbus_sysdeps_test ())
    die ("sysdeps");

  check_memleaks ();
  
  printf ("%s: running data slot tests\n", "dbus-test");
  if (!_dbus_data_slot_test ())
    die ("dataslot");

  check_memleaks ();
  
  printf ("%s: running address parse tests\n", "dbus-test");
  if (!_dbus_address_test ())
    die ("address parsing");

  check_memleaks ();

  printf ("%s: running server listen tests\n", "dbus-test");
  if (!_dbus_server_test ())
    die ("server listen");

  check_memleaks ();

  printf ("%s: running object tree tests\n", "dbus-test");
  if (!_dbus_object_tree_test ())
    die ("object tree");
  
  check_memleaks ();
  
  printf ("%s: running marshalling tests\n", "dbus-test");
  if (!_dbus_marshal_test ())
    die ("marshalling");

  check_memleaks ();

  printf ("%s: running memory tests\n", "dbus-test");
  if (!_dbus_memory_test ())
    die ("memory");
  
  check_memleaks ();

  printf ("%s: running memory pool tests\n", "dbus-test");
  if (!_dbus_mem_pool_test ())
    die ("memory pools");

  check_memleaks ();
  
  printf ("%s: running linked list tests\n", "dbus-test");
  if (!_dbus_list_test ())
    die ("lists");

  check_memleaks ();
  
  printf ("%s: running message tests\n", "dbus-test");
  if (!_dbus_message_test (test_data_dir))
    die ("messages");

  check_memleaks ();
  
  printf ("%s: running hash table tests\n", "dbus-test");
  if (!_dbus_hash_test ())
    die ("hash tables");

  check_memleaks ();

  printf ("%s: running spawn tests\n", "dbus-test");
  if (!_dbus_spawn_test (test_data_dir))
    die ("spawn");

  check_memleaks ();
  
  printf ("%s: running user database tests\n", "dbus-test");
  if (!_dbus_userdb_test (test_data_dir))
    die ("user database");

  check_memleaks ();
  
  printf ("%s: running keyring tests\n", "dbus-test");
  if (!_dbus_keyring_test ())
    die ("keyring");

  check_memleaks ();
  
#if 0
  printf ("%s: running md5 tests\n", "dbus-test");
  if (!_dbus_md5_test ())
    die ("md5");

  check_memleaks ();
#endif
  
  printf ("%s: running SHA-1 tests\n", "dbus-test");
  if (!_dbus_sha_test (test_data_dir))
    die ("SHA-1");

  check_memleaks ();
  
  printf ("%s: running auth tests\n", "dbus-test");
  if (!_dbus_auth_test (test_data_dir))
    die ("auth");

  check_memleaks ();

  printf ("%s: running pending call tests\n", "dbus-test");
  if (!_dbus_pending_call_test (test_data_dir))
    die ("auth");

  check_memleaks ();
  
  printf ("%s: completed successfully\n", "dbus-test");
#else
  printf ("Not compiled with unit tests, not running any\n");
#endif
}


