/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-test.h  Declarations of test functions.
 *
 * Copyright (C) 2002  Red Hat Inc.
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

#ifndef DBUS_TEST_H
#define DBUS_TEST_H

#include <dbus/dbus-types.h>
#include <dbus/dbus-string.h>

typedef enum
{
  _DBUS_MESSAGE_VALID,
  _DBUS_MESSAGE_INVALID,
  _DBUS_MESSAGE_INCOMPLETE,
  _DBUS_MESSAGE_UNKNOWN
} DBusMessageValidity;

dbus_bool_t _dbus_hash_test            (void);
dbus_bool_t _dbus_dict_test            (void);
dbus_bool_t _dbus_list_test            (void);
dbus_bool_t _dbus_marshal_test         (void);
dbus_bool_t _dbus_mem_pool_test        (void);
dbus_bool_t _dbus_string_test          (void);
dbus_bool_t _dbus_address_test         (void);
dbus_bool_t _dbus_server_test          (void);
dbus_bool_t _dbus_message_test         (const char *test_data_dir);
dbus_bool_t _dbus_auth_test            (const char *test_data_dir);
dbus_bool_t _dbus_md5_test             (void);
dbus_bool_t _dbus_sha_test             (const char *test_data_dir);
dbus_bool_t _dbus_keyring_test         (void);
dbus_bool_t _dbus_data_slot_test       (void);
dbus_bool_t _dbus_sysdeps_test         (void);
dbus_bool_t _dbus_spawn_test           (const char *test_data_dir);
dbus_bool_t _dbus_userdb_test          (const char *test_data_dir);
dbus_bool_t _dbus_memory_test	       (void);
dbus_bool_t _dbus_object_tree_test     (void);
dbus_bool_t _dbus_pending_call_test    (const char *test_data_dir);

void        dbus_internal_do_not_use_run_tests         (const char          *test_data_dir);
dbus_bool_t dbus_internal_do_not_use_try_message_file  (const DBusString    *filename,
                                                        dbus_bool_t          is_raw,
                                                        DBusMessageValidity  expected_validity);
dbus_bool_t dbus_internal_do_not_use_try_message_data  (const DBusString    *data,
                                                        DBusMessageValidity  expected_validity);
dbus_bool_t dbus_internal_do_not_use_load_message_file (const DBusString    *filename,
                                                        dbus_bool_t          is_raw,
                                                        DBusString          *data);


/* returns FALSE on fatal failure */
typedef dbus_bool_t (* DBusForeachMessageFileFunc) (const DBusString   *filename,
                                                    dbus_bool_t         is_raw,
                                                    DBusMessageValidity expected_validity,
                                                    void               *data);

dbus_bool_t dbus_internal_do_not_use_foreach_message_file (const char                 *test_data_dir,
                                                           DBusForeachMessageFileFunc  func,
                                                           void                       *user_data);

                                                           


#endif /* DBUS_TEST_H */
