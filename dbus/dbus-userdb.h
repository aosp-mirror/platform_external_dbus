/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-userdb.h User database abstraction
 * 
 * Copyright (C) 2003  Red Hat, Inc.
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

#ifndef DBUS_USERDB_H
#define DBUS_USERDB_H

#include <dbus/dbus-sysdeps.h>

DBUS_BEGIN_DECLS;

typedef struct DBusUserDatabase DBusUserDatabase;

DBusUserDatabase* _dbus_user_database_new        (void);
void              _dbus_user_database_ref        (DBusUserDatabase  *db);
void              _dbus_user_database_unref      (DBusUserDatabase  *db);
dbus_bool_t       _dbus_user_database_get_groups (DBusUserDatabase  *db,
                                                  dbus_uid_t         uid,
                                                  dbus_gid_t       **group_ids,
                                                  int               *n_group_ids,
                                                  DBusError         *error);

DBUS_END_DECLS;

#endif /* DBUS_USERDB_H */
