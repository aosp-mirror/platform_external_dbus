/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-keyring.c Store secret cookies in your homedir
 *
 * Copyright (C) 2003  Red Hat Inc.
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

#include "dbus-keyring.h"
#include <dbus/dbus-string.h>
#include <dbus/dbus-list.h>
#include <dbus/dbus-sysdeps.h>

/**
 * @defgroup DBusKeyring keyring class
 * @ingroup  DBusInternals
 * @brief DBusKeyring data structure
 *
 * Types and functions related to DBusKeyring. DBusKeyring is intended
 * to manage cookies used to authenticate clients to servers.  This is
 * essentially the "verify that client can read the user's homedir"
 * authentication mechanism.  Both client and server must have access
 * to the homedir.
 *
 * The secret keys are not kept in locked memory, and are written to a
 * file in the user's homedir. However they are transient (only used
 * by a single server instance for a fixed period of time, then
 * discarded). Also, the keys are not sent over the wire.
 */

/**
 * @defgroup DBusKeyringInternals DBusKeyring implementation details
 * @ingroup  DBusInternals
 * @brief DBusKeyring implementation details
 *
 * The guts of DBusKeyring.
 *
 * @{
 */

/** The maximum time a key can be alive before we switch to a
 * new one. This isn't super-reliably enforced, since
 * system clocks can change or be wrong, but we make
 * a best effort to only use keys for a short time.
 */
#define MAX_KEY_LIFETIME_SECONDS (60*5)

typedef struct
{
  dbus_int32_t id; /**< identifier used to refer to the key */

  unsigned long creation_time; /**< when the key was generated,
                                *   as unix timestamp
                                */
  
  DBusString secret; /**< the actual key */

} DBusKey;

/**
 * @brief Internals of DBusKeyring.
 * 
 * DBusKeyring internals. DBusKeyring is an opaque object, it must be
 * used via accessor functions.
 */
struct DBusKeyring
{
  int refcount;             /**< Reference count */
  DBusString directory;     /**< Directory the below two items are inside */
  DBusString filename;      /**< Keyring filename */
  DBusString filename_lock; /**< Name of lockfile */
  DBusKey *keys; /**< Keys loaded from the file */
  int n_keys;    /**< Number of keys */
};

static DBusKeyring*
_dbus_keyring_new (void)
{
  DBusKeyring *keyring;

  keyring = dbus_new0 (DBusKeyring, 1);
  if (keyring == NULL)
    goto out_0;
  
  if (!_dbus_string_init (&keyring->directory))
    goto out_1;

  if (!_dbus_string_init (&keyring->filename))
    goto out_2;

  if (!_dbus_string_init (&keyring->filename_lock))
    goto out_3;

  keyring->refcount = 1;
  keyring->keys = NULL;
  keyring->n_keys = 0;

  return keyring;
  
 out_3:
  _dbus_string_free (&keyring->filename);
 out_2:
  _dbus_string_free (&keyring->directory);
 out_1:
  dbus_free (keyring);
 out_0:
  return NULL;
}

static void
free_keys (DBusKey *keys,
           int      n_keys)
{
  int i;

  /* should be safe for args NULL, 0 */
  
  i = 0;
  while (i < n_keys)
    {
      _dbus_string_free (&keys[i].secret);
      ++i;
    }

  dbus_free (keys);
}

/* Our locking scheme is highly unreliable.  However, there is
 * unfortunately no reliable locking scheme in user home directories;
 * between bugs in Linux NFS, people using Tru64 or other total crap
 * NFS, AFS, random-file-system-of-the-week, and so forth, fcntl() in
 * homedirs simply generates tons of bug reports. This has been
 * learned through hard experience with GConf, unfortunately.
 *
 * This bad hack might work better for the kind of lock we have here,
 * which we don't expect to hold for any length of time.  Crashing
 * while we hold it should be unlikely, and timing out such that we
 * delete a stale lock should also be unlikely except when the
 * filesystem is running really slowly.  Stuff might break in corner
 * cases but as long as it's not a security-level breakage it should
 * be OK.
 */

/** Maximum number of timeouts waiting for lock before we decide it's stale */
#define MAX_LOCK_TIMEOUTS 6
/** Length of each timeout while waiting for a lock */
#define LOCK_TIMEOUT 500

static dbus_bool_t
_dbus_keyring_lock (DBusKeyring *keyring)
{
  int n_timeouts;
  
  n_timeouts = 0;
  while (n_timeouts < MAX_LOCK_TIMEOUTS)
    {
      DBusError error;

      dbus_error_init (&error);
      if (_dbus_create_file_exclusively (&keyring->filename_lock,
                                         &error))
        break;

      _dbus_verbose ("Did not get lock file: %s\n",
                     error.message);
      dbus_error_free (&error);

      _dbus_sleep_milliseconds (LOCK_TIMEOUT);
      
      ++n_timeouts;
    }

  if (n_timeouts == MAX_LOCK_TIMEOUTS)
    {
      _dbus_verbose ("Lock file timed out, assuming stale\n");

      _dbus_delete_file (&keyring->filename_lock);

      if (!_dbus_create_file_exclusively (&keyring->filename_lock,
                                          NULL))
        {
          _dbus_verbose ("Couldn't create lock file after trying to delete the stale one, giving up\n");
          return FALSE;
        }
    }
  
  return TRUE;
}

static void
_dbus_keyring_unlock (DBusKeyring *keyring)
{
  if (!_dbus_delete_file (&keyring->filename_lock))
    _dbus_warn ("Failed to delete lock file\n");
}

/**
 * Reloads the keyring file, optionally adds one new key to the file,
 * removes all expired keys from the file, then resaves the file.
 * Stores the keys from the file in keyring->keys.
 *
 * @param keyring the keyring
 * @param add_new #TRUE to add a new key to the file before resave
 * @param error return location for errors
 * @returns #FALSE on failure
 */
static dbus_bool_t
_dbus_keyring_reload (DBusKeyring *keyring,
                      dbus_bool_t  add_new,
                      DBusError   *error)
{
  /* FIXME */

}

/** @} */ /* end of internals */

/**
 * @addtogroup DBusKeyring
 *
 * @{
 */

/**
 * Increments reference count of the keyring
 *
 * @param keyring the keyring
 */
void
_dbus_keyring_ref (DBusKeyring *keyring)
{
  keyring->refcount += 1;
}

/**
 * Decrements refcount and finalizes if it reaches
 * zero.
 *
 * @param keyring the keyring
 */
void
_dbus_keyring_unref (DBusKeyring *keyring)
{
  keyring->refcount -= 1;

  if (keyring->refcount == 0)
    {
      _dbus_string_free (&keyring->filename);
      _dbus_string_free (&keyring->filename_lock);
      _dbus_string_free (&keyring->directory);
      free_keys (keyring->keys, keyring->n_keys);
      dbus_free (keyring);      
    }
}

/**
 * Creates a new keyring that lives in the ~/.dbus-keyrings
 * directory of the given user. If the username is #NULL,
 * uses the user owning the current process.
 *
 * @param username username to get keyring for, or #NULL
 * @param context which keyring to get
 * @param error return location for errors
 * @returns the keyring or #NULL on error
 */
DBusKeyring*
_dbus_keyring_new_homedir (const DBusString *username,
                           const DBusString *context,
                           DBusError        *error)
{
  DBusString homedir;
  DBusKeyring *keyring;
  dbus_bool_t error_set;
  DBusString dotdir;
  DBusString lock_extension;
  
  keyring = NULL;
  error_set = FALSE;
  
  if (!_dbus_string_init (&homedir, _DBUS_INT_MAX))
    return FALSE;

  _dbus_string_init_const (&dotdir, ".dbus-keyrings");
  _dbus_string_init_const (&lock_extension, ".lock");
  
  if (username == NULL)
    {
      const DBusString *const_homedir;
      
      if (!_dbus_user_info_from_current_process (&username,
                                                 &const_homedir,
                                                 NULL))
        goto failed;

      if (!_dbus_string_copy (const_homedir, 0,
                              &homedir, 0))
        goto failed;
    }
  else
    {
      if (!_dbus_homedir_from_username (username, &homedir))
        goto failed;
    }

  keyring = _dbus_keyring_new ();
  if (keyring == NULL)
    goto failed;

  /* should have been validated already, but paranoia check here */
  if (!_dbus_keyring_validate_context (context))
    {
      error_set = TRUE;
      dbus_set_error_const (error,
                            DBUS_ERROR_FAILED,
                            "Invalid context in keyring creation");
      goto failed;
    }
      
  if (!_dbus_string_copy (&homedir, 0,
                          &keyring->directory, 0))
    goto failed;

  if (!_dbus_concat_dir_and_file (&keyring->directory,
                                  &dotdir))
    goto failed;

  if (!_dbus_string_copy (&keyring->directory, 0,
                          &keyring->filename, 0))
    goto failed;

  if (!_dbus_concat_dir_and_file (&keyring->filename,
                                  context))
    goto failed;

  if (!_dbus_string_copy (&keyring->filename, 0,
                          &keyring->filename_lock, 0))
    goto failed;

  if (!_dbus_concat_dir_and_file (&keyring->filename_lock,
                                  &lock_extension))
    goto failed;

  return keyring;
  
 failed:
  if (!error_set)
    dbus_set_error_const (error,
                          DBUS_ERROR_NO_MEMORY,
                          "No memory to create keyring");
  if (keyring)
    _dbus_keyring_unref (keyring);
  _dbus_string_free (&homedir);
  return FALSE;

}

/**
 * Checks whether the context is a valid context.
 * Contexts that might cause confusion when used
 * in filenames are not allowed (contexts can't
 * start with a dot or contain dir separators).
 *
 * @param context the context
 * @returns #TRUE if valid
 */
dbus_bool_t
_dbus_keyring_validate_context (const DBusString *context)
{
  if (_dbus_string_length (context) == 0)
    {
      _dbus_verbose ("context is zero-length\n");
      return FALSE;
    }

  if (!_dbus_string_validate_ascii (context, 0,
                                    _dbus_string_get_length (context)))
    {
      _dbus_verbose ("context not valid ascii\n");
      return FALSE;
    }
  
  /* no directory separators */  
  if (_dbus_string_find (context, 0, "/", NULL))
    {
      _dbus_verbose ("context contains a slash\n");
      return FALSE;
    }

  if (_dbus_string_find (context, 0, "\\", NULL))
    {
      _dbus_verbose ("context contains a backslash\n");
      return FALSE;
    }

  /* prevent attempts to use dotfiles or ".." or ".lock"
   * all of which might allow some kind of attack
   */
  if (_dbus_string_find (context, 0, ".", NULL))
    {
      _dbus_verbose ("context contains a dot\n");
      return FALSE;
    }

  return TRUE;
}

static DBusKey*
find_recent_key (DBusKeyring *keyring)
{
  int i;
  long tv_sec, tv_usec;

  _dbus_get_current_time (&tv_sec, &tv_usec);
  
  i = 0;
  while (i < keyring->n_keys)
    {
      DBusKey *key = &keyring->keys[i];

      if (tv_sec - MAX_KEY_LIFETIME_SECONDS < key->creation_time)
        return key;
      
      ++i;
    }

  return NULL;
}

/**
 * Gets a recent key to use for authentication.
 * If no recent key exists, creates one. Returns
 * the key ID. If a key can't be written to the keyring
 * file so no recent key can be created, returns -1.
 * All valid keys are > 0.
 *
 * @param keyring the keyring
 * @param error error on failure
 * @returns key ID to use for auth, or -1 on failure
 */
int
_dbus_keyring_get_best_key (DBusKeyring  *keyring,
                            DBusError   **error)
{
  DBusKey *key;

  key = find_recent_key (keyring);
  if (key)
    return key->id;

  /* All our keys are too old, or we've never loaded the
   * keyring. Create a new one.
   */
  if (!_dbus_keyring_reload (keyring, TRUE,
                             error))
    return -1;

  key = find_recent_key (keyring);
  if (key)
    return key->id;
  else
    {
      dbus_set_error_const (error,
                            DBUS_ERROR_FAILED,
                            "No recent-enough key found in keyring, and unable to create a new key");
      return -1;
    }
}

/** @} */ /* end of exposed API */

