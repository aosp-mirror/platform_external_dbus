/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-compiler-main.c main() for GLib stubs/skels generator
 *
 * Copyright (C) 2003  Red Hat, Inc.
 *
 * Licensed under the Academic Free License version 2.0
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

#include "dbus-gidl.h"
#include <locale.h>

#ifdef DBUS_BUILD_TESTS
static void run_all_tests (const char *test_data_dir);
#endif

int
main (int argc, char **argv)
{
  setlocale(LC_ALL, "");

  return 0;
}

#ifdef DBUS_BUILD_TESTS
static void
test_die (const char *failure)
{
  fprintf (stderr, "Unit test failed: %s\n", failure);
  exit (1);
}

static void
run_all_tests (const char *test_data_dir)
{
  if (test_data_dir == NULL)
    test_data_dir = _dbus_getenv ("DBUS_TEST_DATA");

  if (test_data_dir != NULL)
    printf ("Test data in %s\n", test_data_dir);
  else
    printf ("No test data!\n");

  printf ("%s: running gtool tests\n", "dbus-glib-tool");
  if (!_dbus_gtool_test (test_data_dir))
    test_die ("gtool");

  printf ("%s: completed successfully\n", "dbus-glib-test");
}

/**
 * @ingroup DBusGTool
 * Unit test for GLib utility tool
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_gtool_test (const char *test_data_dir)
{

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
