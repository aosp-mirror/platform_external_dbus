/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-test.h  Declarations of test functions.
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

#ifndef DBUS_TEST_H
#define DBUS_TEST_H

#include <dbus/dbus-types.h>

dbus_bool_t _dbus_hash_test     (void);
dbus_bool_t _dbus_list_test     (void);
dbus_bool_t _dbus_marshal_test  (void);
dbus_bool_t _dbus_mem_pool_test (void);
dbus_bool_t _dbus_string_test   (void);
dbus_bool_t _dbus_message_test  (const char *test_data_dir);

void dbus_internal_symbol_do_not_use_run_tests (const char *test_data_dir);

#endif /* DBUS_TEST_H */
