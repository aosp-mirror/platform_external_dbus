/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-userdb.h User database abstraction
 * 
 * Copyright (C) 2003  Red Hat, Inc.
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

#ifndef DBUS_USERDB_H
#define DBUS_USERDB_H

#include <dbus/dbus-sysdeps.h>

DBUS_BEGIN_DECLS

typedef struct DBusUserDatabase DBusUserDatabase;

DBusUserDatabase* _dbus_user_database_new           (void);
DBusUserDatabase* _dbus_user_database_ref           (DBusUserDatabase     *db);
void              _dbus_user_database_unref         (DBusUserDatabase     *db);
dbus_bool_t       _dbus_user_database_get_groups    (DBusUserDatabase     *db,
                                                     dbus_uid_t            uid,
                                                     dbus_gid_t          **group_ids,
                                                     int                  *n_group_ids,
                                                     DBusError            *error);
dbus_bool_t       _dbus_user_database_get_uid       (DBusUserDatabase     *db,
                                                     dbus_uid_t            uid,
                                                     const DBusUserInfo  **info,
                                                     DBusError            *error);
dbus_bool_t       _dbus_user_database_get_gid       (DBusUserDatabase     *db,
                                                     dbus_gid_t            gid,
                                                     const DBusGroupInfo **info,
                                                     DBusError            *error);
dbus_bool_t       _dbus_user_database_get_username  (DBusUserDatabase     *db,
                                                     const DBusString     *username,
                                                     const DBusUserInfo  **info,
                                                     DBusError            *error);
dbus_bool_t       _dbus_user_database_get_groupname (DBusUserDatabase     *db,
                                                     const DBusString     *groupname,
                                                     const DBusGroupInfo **info,
                                                     DBusError            *error);


DBusUserDatabase* _dbus_user_database_get_system    (void);
void              _dbus_user_database_lock_system   (void);
void              _dbus_user_database_unlock_system (void);

dbus_bool_t _dbus_username_from_current_process (const DBusString **username);
dbus_bool_t _dbus_homedir_from_current_process  (const DBusString **homedir);
dbus_bool_t _dbus_homedir_from_username         (const DBusString  *username,
                                                 DBusString        *homedir);
dbus_bool_t _dbus_get_user_id                   (const DBusString  *username,
                                                 dbus_uid_t        *uid);
dbus_bool_t _dbus_get_group_id                  (const DBusString  *group_name,
                                                 dbus_gid_t        *gid);
dbus_bool_t _dbus_uid_from_string               (const DBusString  *uid_str,
                                                 dbus_uid_t        *uid);
dbus_bool_t _dbus_credentials_from_username     (const DBusString  *username,
                                                 DBusCredentials   *credentials);
dbus_bool_t _dbus_credentials_from_uid          (dbus_uid_t         user_id,
                                                 DBusCredentials   *credentials);
dbus_bool_t _dbus_is_console_user               (dbus_uid_t         uid,
                                                 DBusError         *error);


DBUS_END_DECLS

#endif /* DBUS_USERDB_H */
