/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-userdb.c User database abstraction
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
#include "dbus-userdb.h"
#include "dbus-hash.h"
#include "dbus-test.h"
#include "dbus-internals.h"
#include <string.h>

typedef struct DBusUserEntry DBusUserEntry;

struct DBusUserEntry
{
  dbus_uid_t  uid;

  dbus_gid_t *group_ids;
  int         n_group_ids;
};

struct DBusUserDatabase
{
  int refcount;

  DBusHashTable *users;
};

static void
free_user_entry (void *data)
{
  DBusUserEntry *entry = data;

  if (entry == NULL) /* hash table will pass NULL */
    return;

  dbus_free (entry->group_ids);
  
  dbus_free (entry);
}

static DBusUserEntry*
_dbus_user_database_lookup (DBusUserDatabase *db,
                            dbus_uid_t        uid,
                            DBusError        *error)
{
  DBusUserEntry *entry;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  entry = _dbus_hash_table_lookup_ulong (db->users, uid);
  if (entry)
    return entry;
  else
    {
      entry = dbus_new0 (DBusUserEntry, 1);
      if (entry == NULL)
        {
          dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
          return NULL;
        }

      if (!_dbus_get_groups (uid, &entry->group_ids, &entry->n_group_ids, error))
        {
          _DBUS_ASSERT_ERROR_IS_SET (error);
          free_user_entry (entry);
          return NULL;
        }

      if (!_dbus_hash_table_insert_ulong (db->users, entry->uid, entry))
        {
          dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
          free_user_entry (entry);
          return NULL;
        }

      return entry;
    }
}

/**
 * @addtogroup DBusInternalsUtils
 * @{
 */

/**
 * Creates a new user database object used to look up and
 * cache user information.
 * @returns new database, or #NULL on out of memory
 */
DBusUserDatabase*
_dbus_user_database_new (void)
{
  DBusUserDatabase *db;
  
  db = dbus_new0 (DBusUserDatabase, 1);
  if (db == NULL)
    return NULL;

  db->refcount = 1;

  db->users = _dbus_hash_table_new (DBUS_HASH_ULONG,
                                    NULL, free_user_entry);

  if (db->users == NULL)
    goto failed;
  
  return db;
  
 failed:
  _dbus_user_database_unref (db);
  return NULL;
}

/**
 * Increments refcount of user database.
 * @param db the database
 */
void
_dbus_user_database_ref (DBusUserDatabase  *db)
{
  _dbus_assert (db->refcount > 0);

  db->refcount += 1;
}

/**
 * Decrements refcount of user database.
 * @param db the database
 */
void
_dbus_user_database_unref (DBusUserDatabase  *db)
{
  _dbus_assert (db->refcount > 0);

  db->refcount -= 1;
  if (db->refcount == 0)
    {
      if (db->users)
        _dbus_hash_table_unref (db->users);
      
      dbus_free (db);
    }
}

/**
 * Gets all groups for a particular user. Returns #FALSE
 * if no memory, or user isn't known, but always initializes
 * group_ids to a NULL array. Sets error to the reason
 * for returning #FALSE.
 *
 * @param db the user database object
 * @param uid the user ID
 * @param group_ids return location for array of group IDs
 * @param n_group_ids return location for length of returned array
 * @param error return location for error
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_user_database_get_groups (DBusUserDatabase  *db,
                                dbus_uid_t         uid,
                                dbus_gid_t       **group_ids,
                                int               *n_group_ids,
                                DBusError         *error)
{
  DBusUserEntry *entry;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  *group_ids = NULL;
  *n_group_ids = 0;
  
  entry = _dbus_user_database_lookup (db, uid, error);
  if (entry == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      return FALSE;
    }

  if (entry->n_group_ids > 0)
    {
      *group_ids = dbus_new (dbus_gid_t, entry->n_group_ids);
      if (*group_ids == NULL)
        {
          dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
          return FALSE;
        }

      *n_group_ids = entry->n_group_ids;

      memcpy (*group_ids, entry->group_ids, entry->n_group_ids * sizeof (dbus_gid_t));
    }

  return TRUE;
}

/** @} */

#ifdef DBUS_BUILD_TESTS
/**
 * Unit test for dbus-userdb.c.
 * 
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_userdb_test (const char *test_data_dir)
{

  return TRUE;
}
#endif /* DBUS_BUILD_TESTS */
