/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-userdb.c User database abstraction
 * 
 * Copyright (C) 2003, 2004  Red Hat, Inc.
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
#include "dbus-userdb.h"
#include "dbus-hash.h"
#include "dbus-test.h"
#include "dbus-internals.h"
#include "dbus-protocol.h"
#include <string.h>

/**
 * Internals of DBusUserDatabase
 */
struct DBusUserDatabase
{
  int refcount; /**< Reference count */

  DBusHashTable *users; /**< Users in the database by UID */
  DBusHashTable *groups; /**< Groups in the database by GID */
  DBusHashTable *users_by_name; /**< Users in the database by name */
  DBusHashTable *groups_by_name; /**< Groups in the database by name */

};

static void
free_user_info (void *data)
{
  DBusUserInfo *info = data;

  if (info == NULL) /* hash table will pass NULL */
    return;

  _dbus_user_info_free (info);
  dbus_free (info);
}

static void
free_group_info (void *data)
{
  DBusGroupInfo *info = data;

  if (info == NULL) /* hash table will pass NULL */
    return;

  _dbus_group_info_free (info);
  dbus_free (info);
}

static DBusUserInfo*
_dbus_user_database_lookup (DBusUserDatabase *db,
                            dbus_uid_t        uid,
                            const DBusString *username,
                            DBusError        *error)
{
  DBusUserInfo *info;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  _dbus_assert (uid != DBUS_UID_UNSET || username != NULL);
  
  if (uid != DBUS_UID_UNSET)
    info = _dbus_hash_table_lookup_ulong (db->users, uid);
  else
    info = _dbus_hash_table_lookup_string (db->users_by_name, _dbus_string_get_const_data (username));
  
  if (info)
    {
      _dbus_verbose ("Using cache for UID "DBUS_UID_FORMAT" information\n",
                     uid);
      return info;
    }
  else
    {
      _dbus_verbose ("No cache for UID "DBUS_UID_FORMAT"\n",
                     uid);
      
      info = dbus_new0 (DBusUserInfo, 1);
      if (info == NULL)
        {
          dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
          return NULL;
        }

      if (uid != DBUS_UID_UNSET)
        {
          if (!_dbus_user_info_fill_uid (info, uid, error))
            {
              _DBUS_ASSERT_ERROR_IS_SET (error);
              free_user_info (info);
              return NULL;
            }
        }
      else
        {
          if (!_dbus_user_info_fill (info, username, error))
            {
              _DBUS_ASSERT_ERROR_IS_SET (error);
              free_user_info (info);
              return NULL;
            }
        }

      /* be sure we don't use these after here */
      uid = DBUS_UID_UNSET;
      username = NULL;

      /* insert into hash */
      if (!_dbus_hash_table_insert_ulong (db->users, info->uid, info))
        {
          dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
          free_user_info (info);
          return NULL;
        }

      if (!_dbus_hash_table_insert_string (db->users_by_name,
                                           info->username,
                                           info))
        {
          _dbus_hash_table_remove_ulong (db->users, info->uid);
          dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
          return NULL;
        }
      
      return info;
    }
}

static DBusGroupInfo*
_dbus_user_database_lookup_group (DBusUserDatabase *db,
                                  dbus_gid_t        gid,
                                  const DBusString *groupname,
                                  DBusError        *error)
{
  DBusGroupInfo *info;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (gid != DBUS_GID_UNSET)
    info = _dbus_hash_table_lookup_ulong (db->groups, gid);
  else
    info = _dbus_hash_table_lookup_string (db->groups_by_name,
                                           _dbus_string_get_const_data (groupname));
  if (info)
    {
      _dbus_verbose ("Using cache for GID "DBUS_GID_FORMAT" information\n",
                     gid);
      return info;
    }
  else
    {
      _dbus_verbose ("No cache for GID "DBUS_GID_FORMAT"\n",
                     gid);
      
      info = dbus_new0 (DBusGroupInfo, 1);
      if (info == NULL)
        {
          dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
          return NULL;
        }

      if (!_dbus_group_info_fill_gid (info, gid, error))
        {
          _DBUS_ASSERT_ERROR_IS_SET (error);
          free_group_info (info);
          return NULL;
        }

      if (!_dbus_hash_table_insert_ulong (db->groups, info->gid, info))
        {
          dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
          free_group_info (info);
          return NULL;
        }


      if (!_dbus_hash_table_insert_string (db->groups_by_name,
                                           info->groupname,
                                           info))
        {
          _dbus_hash_table_remove_ulong (db->groups, info->gid);
          dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
          return NULL;
        }
      
      return info;
    }
}

_DBUS_DEFINE_GLOBAL_LOCK(system_users);
static dbus_bool_t database_locked = FALSE;
static DBusUserDatabase *system_db = NULL;
static DBusString process_username;
static DBusString process_homedir;
      
static void
shutdown_system_db (void *data)
{
  _dbus_user_database_unref (system_db);
  system_db = NULL;
  _dbus_string_free (&process_username);
  _dbus_string_free (&process_homedir);
}

static dbus_bool_t
init_system_db (void)
{
  _dbus_assert (database_locked);
    
  if (system_db == NULL)
    {
      DBusError error;
      const DBusUserInfo *info;
      
      system_db = _dbus_user_database_new ();
      if (system_db == NULL)
        return FALSE;

      dbus_error_init (&error);

      if (!_dbus_user_database_get_uid (system_db,
                                        _dbus_getuid (),
                                        &info,
                                        &error))
        {
          _dbus_user_database_unref (system_db);
          system_db = NULL;
          
          if (dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY))
            {
              dbus_error_free (&error);
              return FALSE;
            }
          else
            {
              /* This really should not happen. */
              _dbus_warn ("Could not get password database information for UID of current process: %s\n",
                          error.message);
              dbus_error_free (&error);
              return FALSE;
            }
        }

      if (!_dbus_string_init (&process_username))
        {
          _dbus_user_database_unref (system_db);
          system_db = NULL;
          return FALSE;
        }

      if (!_dbus_string_init (&process_homedir))
        {
          _dbus_string_free (&process_username);
          _dbus_user_database_unref (system_db);
          system_db = NULL;
          return FALSE;
        }

      if (!_dbus_string_append (&process_username,
                                info->username) ||
          !_dbus_string_append (&process_homedir,
                                info->homedir) ||
          !_dbus_register_shutdown_func (shutdown_system_db, NULL))
        {
          _dbus_string_free (&process_username);
          _dbus_string_free (&process_homedir);
          _dbus_user_database_unref (system_db);
          system_db = NULL;
          return FALSE;
        }
    }

  return TRUE;
}

/**
 * @addtogroup DBusInternalsUtils
 * @{
 */

/**
 * Locks global system user database.
 */
void
_dbus_user_database_lock_system (void)
{
  _DBUS_LOCK (system_users);
  database_locked = TRUE;
}

/**
 * Unlocks global system user database.
 */
void
_dbus_user_database_unlock_system (void)
{
  database_locked = FALSE;
  _DBUS_UNLOCK (system_users);
}

/**
 * Gets the system global user database;
 * must be called with lock held (_dbus_user_database_lock_system()).
 *
 * @returns the database or #NULL if no memory
 */
DBusUserDatabase*
_dbus_user_database_get_system (void)
{
  _dbus_assert (database_locked);

  init_system_db ();
  
  return system_db;
}

/**
 * Gets username of user owning current process.  The returned string
 * is valid until dbus_shutdown() is called.
 *
 * @param username place to store pointer to username
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_username_from_current_process (const DBusString **username)
{
  _dbus_user_database_lock_system ();
  if (!init_system_db ())
    {
      _dbus_user_database_unlock_system ();
      return FALSE;
    }
  *username = &process_username;
  _dbus_user_database_unlock_system ();  

  return TRUE;
}

/**
 * Gets homedir of user owning current process.  The returned string
 * is valid until dbus_shutdown() is called.
 *
 * @param homedir place to store pointer to homedir
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_homedir_from_current_process (const DBusString  **homedir)
{
  _dbus_user_database_lock_system ();
  if (!init_system_db ())
    {
      _dbus_user_database_unlock_system ();
      return FALSE;
    }
  *homedir = &process_homedir;
  _dbus_user_database_unlock_system ();

  return TRUE;
}

/**
 * Gets user ID given username
 *
 * @param username the username
 * @param uid return location for UID
 * @returns #TRUE if username existed and we got the UID
 */
dbus_bool_t
_dbus_get_user_id (const DBusString  *username,
                   dbus_uid_t        *uid)
{
  DBusCredentials creds;

  if (!_dbus_credentials_from_username (username, &creds))
    return FALSE;

  if (creds.uid == DBUS_UID_UNSET)
    return FALSE;

  *uid = creds.uid;

  return TRUE;
}

/**
 * Checks to see if the UID sent in is the console user
 *
 * @param uid UID of person to check 
 * @param error return location for errors
 * @returns #TRUE if the UID is the same as the console user and there are no errors
 */
dbus_bool_t
_dbus_is_console_user (dbus_uid_t uid,
		       DBusError *error)
{

  DBusUserDatabase *db;
  const DBusUserInfo *info;
  dbus_bool_t result = FALSE; 

  _dbus_user_database_lock_system ();

  db = _dbus_user_database_get_system ();
  if (db == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED, "Could not get system database.");
      _dbus_user_database_unlock_system ();
      return FALSE;
    }

  info = _dbus_user_database_lookup (db, uid, NULL, error);

  if (info == NULL)
    {
      _dbus_user_database_unlock_system ();
       return FALSE;
    }

  result = _dbus_user_at_console (info->username, error);

  _dbus_user_database_unlock_system ();

  return result;
}

/**
 * Gets group ID given groupname
 *
 * @param groupname the groupname
 * @param gid return location for GID
 * @returns #TRUE if group name existed and we got the GID
 */
dbus_bool_t
_dbus_get_group_id (const DBusString  *groupname,
                    dbus_gid_t        *gid)
{
  DBusUserDatabase *db;
  const DBusGroupInfo *info;
  _dbus_user_database_lock_system ();

  db = _dbus_user_database_get_system ();
  if (db == NULL)
    {
      _dbus_user_database_unlock_system ();
      return FALSE;
    }

  if (!_dbus_user_database_get_groupname (db, groupname,
                                          &info, NULL))
    {
      _dbus_user_database_unlock_system ();
      return FALSE;
    }

  *gid = info->gid;
  
  _dbus_user_database_unlock_system ();
  return TRUE;
}

/**
 * Gets the home directory for the given user.
 *
 * @param username the username
 * @param homedir string to append home directory to
 * @returns #TRUE if user existed and we appended their homedir
 */
dbus_bool_t
_dbus_homedir_from_username (const DBusString *username,
                             DBusString       *homedir)
{
  DBusUserDatabase *db;
  const DBusUserInfo *info;
  _dbus_user_database_lock_system ();

  db = _dbus_user_database_get_system ();
  if (db == NULL)
    {
      _dbus_user_database_unlock_system ();
      return FALSE;
    }

  if (!_dbus_user_database_get_username (db, username,
                                         &info, NULL))
    {
      _dbus_user_database_unlock_system ();
      return FALSE;
    }

  if (!_dbus_string_append (homedir, info->homedir))
    {
      _dbus_user_database_unlock_system ();
      return FALSE;
    }
  
  _dbus_user_database_unlock_system ();
  return TRUE;
}

/**
 * Gets a UID from a UID string.
 *
 * @param uid_str the UID in string form
 * @param uid UID to fill in
 * @returns #TRUE if successfully filled in UID
 */
dbus_bool_t
_dbus_uid_from_string (const DBusString      *uid_str,
                       dbus_uid_t            *uid)
{
  int end;
  long val;
  
  if (_dbus_string_get_length (uid_str) == 0)
    {
      _dbus_verbose ("UID string was zero length\n");
      return FALSE;
    }

  val = -1;
  end = 0;
  if (!_dbus_string_parse_int (uid_str, 0, &val,
                               &end))
    {
      _dbus_verbose ("could not parse string as a UID\n");
      return FALSE;
    }
  
  if (end != _dbus_string_get_length (uid_str))
    {
      _dbus_verbose ("string contained trailing stuff after UID\n");
      return FALSE;
    }

  *uid = val;

  return TRUE;
}

/**
 * Gets the credentials corresponding to the given username.
 *
 * @param username the username
 * @param credentials credentials to fill in
 * @returns #TRUE if the username existed and we got some credentials
 */
dbus_bool_t
_dbus_credentials_from_username (const DBusString *username,
                                 DBusCredentials  *credentials)
{
  DBusUserDatabase *db;
  const DBusUserInfo *info;
  _dbus_user_database_lock_system ();

  db = _dbus_user_database_get_system ();
  if (db == NULL)
    {
      _dbus_user_database_unlock_system ();
      return FALSE;
    }

  if (!_dbus_user_database_get_username (db, username,
                                         &info, NULL))
    {
      _dbus_user_database_unlock_system ();
      return FALSE;
    }

  credentials->pid = DBUS_PID_UNSET;
  credentials->uid = info->uid;
  credentials->gid = info->primary_gid;
  
  _dbus_user_database_unlock_system ();
  return TRUE;
}

/**
 * Gets the credentials corresponding to the given UID.
 *
 * @param uid the UID
 * @param credentials credentials to fill in
 * @returns #TRUE if the UID existed and we got some credentials
 */
dbus_bool_t
_dbus_credentials_from_uid (dbus_uid_t        uid,
                            DBusCredentials  *credentials)
{
  DBusUserDatabase *db;
  const DBusUserInfo *info;
  _dbus_user_database_lock_system ();

  db = _dbus_user_database_get_system ();
  if (db == NULL)
    {
      _dbus_user_database_unlock_system ();
      return FALSE;
    }

  if (!_dbus_user_database_get_uid (db, uid,
                                    &info, NULL))
    {
      _dbus_user_database_unlock_system ();
      return FALSE;
    }

  _dbus_assert (info->uid == uid);
  
  credentials->pid = DBUS_PID_UNSET;
  credentials->uid = info->uid;
  credentials->gid = info->primary_gid;
  
  _dbus_user_database_unlock_system ();
  return TRUE;
}

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
                                    NULL, free_user_info);
  
  if (db->users == NULL)
    goto failed;

  db->groups = _dbus_hash_table_new (DBUS_HASH_ULONG,
                                     NULL, free_group_info);
  
  if (db->groups == NULL)
    goto failed;

  db->users_by_name = _dbus_hash_table_new (DBUS_HASH_STRING,
                                            NULL, NULL);
  if (db->users_by_name == NULL)
    goto failed;
  
  db->groups_by_name = _dbus_hash_table_new (DBUS_HASH_STRING,
                                             NULL, NULL);
  if (db->groups_by_name == NULL)
    goto failed;
  
  return db;
  
 failed:
  _dbus_user_database_unref (db);
  return NULL;
}

/**
 * Increments refcount of user database.
 * @param db the database
 * @returns the database
 */
DBusUserDatabase *
_dbus_user_database_ref (DBusUserDatabase  *db)
{
  _dbus_assert (db->refcount > 0);

  db->refcount += 1;

  return db;
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

      if (db->groups)
        _dbus_hash_table_unref (db->groups);

      if (db->users_by_name)
        _dbus_hash_table_unref (db->users_by_name);

      if (db->groups_by_name)
        _dbus_hash_table_unref (db->groups_by_name);
      
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
  DBusUserInfo *info;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  *group_ids = NULL;
  *n_group_ids = 0;
  
  info = _dbus_user_database_lookup (db, uid, NULL, error);
  if (info == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      return FALSE;
    }

  if (info->n_group_ids > 0)
    {
      *group_ids = dbus_new (dbus_gid_t, info->n_group_ids);
      if (*group_ids == NULL)
        {
          dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
          return FALSE;
        }

      *n_group_ids = info->n_group_ids;

      memcpy (*group_ids, info->group_ids, info->n_group_ids * sizeof (dbus_gid_t));
    }

  return TRUE;
}

/**
 * Gets the user information for the given UID,
 * returned user info should not be freed. 
 *
 * @param db user database
 * @param uid the user ID
 * @param info return location for const ref to user info
 * @param error error location
 * @returns #FALSE if error is set
 */
dbus_bool_t
_dbus_user_database_get_uid (DBusUserDatabase    *db,
                             dbus_uid_t           uid,
                             const DBusUserInfo **info,
                             DBusError           *error)
{
  *info = _dbus_user_database_lookup (db, uid, NULL, error);
  return *info != NULL;
}

/**
 * Gets the user information for the given GID,
 * returned group info should not be freed. 
 *
 * @param db user database
 * @param gid the group ID
 * @param info return location for const ref to group info
 * @param error error location
 * @returns #FALSE if error is set
 */
dbus_bool_t
_dbus_user_database_get_gid (DBusUserDatabase     *db,
                             dbus_gid_t            gid,
                             const DBusGroupInfo **info,
                             DBusError            *error)
{
  *info = _dbus_user_database_lookup_group (db, gid, NULL, error);
  return *info != NULL;
}

/**
 * Gets the user information for the given username.
 *
 * @param db user database
 * @param username the user name
 * @param info return location for const ref to user info
 * @param error error location
 * @returns #FALSE if error is set
 */
dbus_bool_t
_dbus_user_database_get_username  (DBusUserDatabase     *db,
                                   const DBusString     *username,
                                   const DBusUserInfo  **info,
                                   DBusError            *error)
{
  *info = _dbus_user_database_lookup (db, DBUS_UID_UNSET, username, error);
  return *info != NULL;
}

/**
 * Gets the user information for the given group name,
 * returned group info should not be freed. 
 *
 * @param db user database
 * @param groupname the group name
 * @param info return location for const ref to group info
 * @param error error location
 * @returns #FALSE if error is set
 */
dbus_bool_t
_dbus_user_database_get_groupname (DBusUserDatabase     *db,
                                   const DBusString     *groupname,
                                   const DBusGroupInfo **info,
                                   DBusError            *error)
{
  *info = _dbus_user_database_lookup_group (db, DBUS_GID_UNSET, groupname, error);
  return *info != NULL;
}

/** @} */

#ifdef DBUS_BUILD_TESTS
#include <stdio.h>

/**
 * Unit test for dbus-userdb.c.
 * 
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_userdb_test (const char *test_data_dir)
{
  const DBusString *username;
  const DBusString *homedir;

  if (!_dbus_username_from_current_process (&username))
    _dbus_assert_not_reached ("didn't get username");

  if (!_dbus_homedir_from_current_process (&homedir))
    _dbus_assert_not_reached ("didn't get homedir");  

  printf ("    Current user: %s homedir: %s\n",
          _dbus_string_get_const_data (username),
          _dbus_string_get_const_data (homedir));
  
  return TRUE;
}
#endif /* DBUS_BUILD_TESTS */
