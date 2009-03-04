/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-sysdeps-util-unix.c Would be in dbus-sysdeps-unix.c, but not used in libdbus
 * 
 * Copyright (C) 2002, 2003, 2004, 2005  Red Hat, Inc.
 * Copyright (C) 2003 CodeFactory AB
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
#include "dbus-sysdeps.h"
#include "dbus-sysdeps-unix.h"
#include "dbus-internals.h"
#include "dbus-protocol.h"
#include "dbus-string.h"
#define DBUS_USERDB_INCLUDES_PRIVATE 1
#include "dbus-userdb.h"
#include "dbus-test.h"

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <grp.h>
#include <sys/socket.h>
#include <dirent.h>
#include <sys/un.h>

#ifdef HAVE_SYS_SYSLIMITS_H
#include <sys/syslimits.h>
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

/**
 * @addtogroup DBusInternalsUtils
 * @{
 */

/**
 * Does the chdir, fork, setsid, etc. to become a daemon process.
 *
 * @param pidfile #NULL, or pidfile to create
 * @param print_pid_fd file descriptor to print daemon's pid to, or -1 for none
 * @param error return location for errors
 * @returns #FALSE on failure
 */
dbus_bool_t
_dbus_become_daemon (const DBusString *pidfile,
		     int               print_pid_fd,
                     DBusError        *error)
{
  const char *s;
  pid_t child_pid;
  int dev_null_fd;

  _dbus_verbose ("Becoming a daemon...\n");

  _dbus_verbose ("chdir to /\n");
  if (chdir ("/") < 0)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Could not chdir() to root directory");
      return FALSE;
    }

  _dbus_verbose ("forking...\n");
  switch ((child_pid = fork ()))
    {
    case -1:
      _dbus_verbose ("fork failed\n");
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to fork daemon: %s", _dbus_strerror (errno));
      return FALSE;
      break;

    case 0:
      _dbus_verbose ("in child, closing std file descriptors\n");

      /* silently ignore failures here, if someone
       * doesn't have /dev/null we may as well try
       * to continue anyhow
       */
      
      dev_null_fd = open ("/dev/null", O_RDWR);
      if (dev_null_fd >= 0)
        {
          dup2 (dev_null_fd, 0);
          dup2 (dev_null_fd, 1);
          
          s = _dbus_getenv ("DBUS_DEBUG_OUTPUT");
          if (s == NULL || *s == '\0')
            dup2 (dev_null_fd, 2);
          else
            _dbus_verbose ("keeping stderr open due to DBUS_DEBUG_OUTPUT\n");
        }

      /* Get a predictable umask */
      _dbus_verbose ("setting umask\n");
      umask (022);
      break;

    default:
      if (pidfile)
        {
          _dbus_verbose ("parent writing pid file\n");
          if (!_dbus_write_pid_file (pidfile,
                                     child_pid,
                                     error))
            {
              _dbus_verbose ("pid file write failed, killing child\n");
              kill (child_pid, SIGTERM);
              return FALSE;
            }
        }

      /* Write PID if requested */
      if (print_pid_fd >= 0)
	{
	  DBusString pid;
	  int bytes;
	  
	  if (!_dbus_string_init (&pid))
	    {
	      _DBUS_SET_OOM (error);
              kill (child_pid, SIGTERM);
	      return FALSE;
	    }
	  
	  if (!_dbus_string_append_int (&pid, child_pid) ||
	      !_dbus_string_append (&pid, "\n"))
	    {
	      _dbus_string_free (&pid);
	      _DBUS_SET_OOM (error);
              kill (child_pid, SIGTERM);
	      return FALSE;
	    }
	  
	  bytes = _dbus_string_get_length (&pid);
	  if (_dbus_write_socket (print_pid_fd, &pid, 0, bytes) != bytes)
	    {
	      dbus_set_error (error, DBUS_ERROR_FAILED,
			      "Printing message bus PID: %s\n",
			      _dbus_strerror (errno));
	      _dbus_string_free (&pid);
              kill (child_pid, SIGTERM);
	      return FALSE;
	    }
	  
	  _dbus_string_free (&pid);
	}
      _dbus_verbose ("parent exiting\n");
      _exit (0);
      break;
    }

  _dbus_verbose ("calling setsid()\n");
  if (setsid () == -1)
    _dbus_assert_not_reached ("setsid() failed");
  
  return TRUE;
}


/**
 * Creates a file containing the process ID.
 *
 * @param filename the filename to write to
 * @param pid our process ID
 * @param error return location for errors
 * @returns #FALSE on failure
 */
dbus_bool_t
_dbus_write_pid_file (const DBusString *filename,
                      unsigned long     pid,
		      DBusError        *error)
{
  const char *cfilename;
  int fd;
  FILE *f;

  cfilename = _dbus_string_get_const_data (filename);
  
  fd = open (cfilename, O_WRONLY|O_CREAT|O_EXCL|O_BINARY, 0644);
  
  if (fd < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to open \"%s\": %s", cfilename,
                      _dbus_strerror (errno));
      return FALSE;
    }

  if ((f = fdopen (fd, "w")) == NULL)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to fdopen fd %d: %s", fd, _dbus_strerror (errno));
      _dbus_close (fd, NULL);
      return FALSE;
    }
  
  if (fprintf (f, "%lu\n", pid) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to write to \"%s\": %s", cfilename,
                      _dbus_strerror (errno));
      
      fclose (f);
      return FALSE;
    }

  if (fclose (f) == EOF)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to close \"%s\": %s", cfilename,
                      _dbus_strerror (errno));
      return FALSE;
    }
  
  return TRUE;
}


/**
 * Changes the user and group the bus is running as.
 *
 * @param uid the new user ID
 * @param gid the new group ID
 * @param error return location for errors
 * @returns #FALSE on failure
 */
dbus_bool_t
_dbus_change_identity  (dbus_uid_t     uid,
                        dbus_gid_t     gid,
                        DBusError     *error)
{
  /* setgroups() only works if we are a privileged process,
   * so we don't return error on failure; the only possible
   * failure is that we don't have perms to do it.
   *
   * not sure this is right, maybe if setuid()
   * is going to work then setgroups() should also work.
   */
  if (setgroups (0, NULL) < 0)
    _dbus_warn ("Failed to drop supplementary groups: %s\n",
                _dbus_strerror (errno));
  
  /* Set GID first, or the setuid may remove our permission
   * to change the GID
   */
  if (setgid (gid) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to set GID to %lu: %s", gid,
                      _dbus_strerror (errno));
      return FALSE;
    }
  
  if (setuid (uid) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to set UID to %lu: %s", uid,
                      _dbus_strerror (errno));
      return FALSE;
    }
  
  return TRUE;
}

/** Installs a UNIX signal handler
 *
 * @param sig the signal to handle
 * @param handler the handler
 */
void
_dbus_set_signal_handler (int               sig,
                          DBusSignalHandler handler)
{
  struct sigaction act;
  sigset_t empty_mask;
  
  sigemptyset (&empty_mask);
  act.sa_handler = handler;
  act.sa_mask    = empty_mask;
  act.sa_flags   = 0;
  sigaction (sig,  &act, NULL);
}


/**
 * Removes a directory; Directory must be empty
 * 
 * @param filename directory filename
 * @param error initialized error object
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_delete_directory (const DBusString *filename,
			DBusError        *error)
{
  const char *filename_c;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  filename_c = _dbus_string_get_const_data (filename);

  if (rmdir (filename_c) != 0)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
		      "Failed to remove directory %s: %s\n",
		      filename_c, _dbus_strerror (errno));
      return FALSE;
    }
  
  return TRUE;
}

/** Checks if a file exists
*
* @param file full path to the file
* @returns #TRUE if file exists
*/
dbus_bool_t 
_dbus_file_exists (const char *file)
{
  return (access (file, F_OK) == 0);
}

/** Checks if user is at the console
*
* @param username user to check
* @param error return location for errors
* @returns #TRUE is the user is at the consolei and there are no errors
*/
dbus_bool_t 
_dbus_user_at_console (const char *username,
                       DBusError  *error)
{

  DBusString f;
  dbus_bool_t result;

  result = FALSE;
  if (!_dbus_string_init (&f))
    {
      _DBUS_SET_OOM (error);
      return FALSE;
    }

  if (!_dbus_string_append (&f, DBUS_CONSOLE_AUTH_DIR))
    {
      _DBUS_SET_OOM (error);
      goto out;
    }


  if (!_dbus_string_append (&f, username))
    {
      _DBUS_SET_OOM (error);
      goto out;
    }

  result = _dbus_file_exists (_dbus_string_get_const_data (&f));

 out:
  _dbus_string_free (&f);

  return result;
}


/**
 * Checks whether the filename is an absolute path
 *
 * @param filename the filename
 * @returns #TRUE if an absolute path
 */
dbus_bool_t
_dbus_path_is_absolute (const DBusString *filename)
{
  if (_dbus_string_get_length (filename) > 0)
    return _dbus_string_get_byte (filename, 0) == '/';
  else
    return FALSE;
}

/**
 * stat() wrapper.
 *
 * @param filename the filename to stat
 * @param statbuf the stat info to fill in
 * @param error return location for error
 * @returns #FALSE if error was set
 */
dbus_bool_t
_dbus_stat (const DBusString *filename,
            DBusStat         *statbuf,
            DBusError        *error)
{
  const char *filename_c;
  struct stat sb;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  filename_c = _dbus_string_get_const_data (filename);

  if (stat (filename_c, &sb) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "%s", _dbus_strerror (errno));
      return FALSE;
    }

  statbuf->mode = sb.st_mode;
  statbuf->nlink = sb.st_nlink;
  statbuf->uid = sb.st_uid;
  statbuf->gid = sb.st_gid;
  statbuf->size = sb.st_size;
  statbuf->atime = sb.st_atime;
  statbuf->mtime = sb.st_mtime;
  statbuf->ctime = sb.st_ctime;

  return TRUE;
}


/**
 * Internals of directory iterator
 */
struct DBusDirIter
{
  DIR *d; /**< The DIR* from opendir() */
  
};

/**
 * Open a directory to iterate over.
 *
 * @param filename the directory name
 * @param error exception return object or #NULL
 * @returns new iterator, or #NULL on error
 */
DBusDirIter*
_dbus_directory_open (const DBusString *filename,
                      DBusError        *error)
{
  DIR *d;
  DBusDirIter *iter;
  const char *filename_c;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  filename_c = _dbus_string_get_const_data (filename);

  d = opendir (filename_c);
  if (d == NULL)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to read directory \"%s\": %s",
                      filename_c,
                      _dbus_strerror (errno));
      return NULL;
    }
  iter = dbus_new0 (DBusDirIter, 1);
  if (iter == NULL)
    {
      closedir (d);
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY,
                      "Could not allocate memory for directory iterator");
      return NULL;
    }

  iter->d = d;

  return iter;
}

/* it is never safe to retrun a size smaller than sizeof(struct dirent)
 * because the libc *could* try to access the  whole structure
 * (for instance it could try to memset it).
 * it is also incorrect to return a size bigger than that, because
 * the libc would never use it.
 * The only correct and safe value this function can ever return is
 * sizeof(struct dirent).
 */
static dbus_bool_t
dirent_buf_size(DIR * dirp, size_t *size)
{
  *size = sizeof(struct dirent);
  return TRUE;
}

/**
 * Get next file in the directory. Will not return "." or ".."  on
 * UNIX. If an error occurs, the contents of "filename" are
 * undefined. The error is never set if the function succeeds.
 *
 * @param iter the iterator
 * @param filename string to be set to the next file in the dir
 * @param error return location for error
 * @returns #TRUE if filename was filled in with a new filename
 */
dbus_bool_t
_dbus_directory_get_next_file (DBusDirIter      *iter,
                               DBusString       *filename,
                               DBusError        *error)
{
  struct dirent *d, *ent;
  size_t buf_size;
  int err;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
 
  if (!dirent_buf_size (iter->d, &buf_size))
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Can't calculate buffer size when reading directory");
      return FALSE;
    }

  d = (struct dirent *)dbus_malloc (buf_size);
  if (!d)
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY,
                      "No memory to read directory entry");
      return FALSE;
    }

 again:
  err = readdir_r (iter->d, d, &ent);
  if (err || !ent)
    {
      if (err != 0)
        dbus_set_error (error,
                        _dbus_error_from_errno (err),
                        "%s", _dbus_strerror (err));

      dbus_free (d);
      return FALSE;
    }
  else if (ent->d_name[0] == '.' &&
           (ent->d_name[1] == '\0' ||
            (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
    goto again;
  else
    {
      _dbus_string_set_length (filename, 0);
      if (!_dbus_string_append (filename, ent->d_name))
        {
          dbus_set_error (error, DBUS_ERROR_NO_MEMORY,
                          "No memory to read directory entry");
          dbus_free (d);
          return FALSE;
        }
      else
        {
          dbus_free (d);
          return TRUE;
        }
    }
}

/**
 * Closes a directory iteration.
 */
void
_dbus_directory_close (DBusDirIter *iter)
{
  closedir (iter->d);
  dbus_free (iter);
}

static dbus_bool_t
fill_user_info_from_group (struct group  *g,
                           DBusGroupInfo *info,
                           DBusError     *error)
{
  _dbus_assert (g->gr_name != NULL);
  
  info->gid = g->gr_gid;
  info->groupname = _dbus_strdup (g->gr_name);

  /* info->members = dbus_strdupv (g->gr_mem) */
  
  if (info->groupname == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return FALSE;
    }

  return TRUE;
}

static dbus_bool_t
fill_group_info (DBusGroupInfo    *info,
                 dbus_gid_t        gid,
                 const DBusString *groupname,
                 DBusError        *error)
{
  const char *group_c_str;

  _dbus_assert (groupname != NULL || gid != DBUS_GID_UNSET);
  _dbus_assert (groupname == NULL || gid == DBUS_GID_UNSET);

  if (groupname)
    group_c_str = _dbus_string_get_const_data (groupname);
  else
    group_c_str = NULL;
  
  /* For now assuming that the getgrnam() and getgrgid() flavors
   * always correspond to the pwnam flavors, if not we have
   * to add more configure checks.
   */
  
#if defined (HAVE_POSIX_GETPWNAM_R) || defined (HAVE_NONPOSIX_GETPWNAM_R)
  {
    struct group *g;
    int result;
    char buf[1024];
    struct group g_str;

    g = NULL;
#ifdef HAVE_POSIX_GETPWNAM_R

    if (group_c_str)
      result = getgrnam_r (group_c_str, &g_str, buf, sizeof (buf),
                           &g);
    else
      result = getgrgid_r (gid, &g_str, buf, sizeof (buf),
                           &g);
#else
    g = getgrnam_r (group_c_str, &g_str, buf, sizeof (buf));
    result = 0;
#endif /* !HAVE_POSIX_GETPWNAM_R */
    if (result == 0 && g == &g_str)
      {
        return fill_user_info_from_group (g, info, error);
      }
    else
      {
        dbus_set_error (error, _dbus_error_from_errno (errno),
                        "Group %s unknown or failed to look it up\n",
                        group_c_str ? group_c_str : "???");
        return FALSE;
      }
  }
#else /* ! HAVE_GETPWNAM_R */
  {
    /* I guess we're screwed on thread safety here */
    struct group *g;

    g = getgrnam (group_c_str);

    if (g != NULL)
      {
        return fill_user_info_from_group (g, info, error);
      }
    else
      {
        dbus_set_error (error, _dbus_error_from_errno (errno),
                        "Group %s unknown or failed to look it up\n",
                        group_c_str ? group_c_str : "???");
        return FALSE;
      }
  }
#endif  /* ! HAVE_GETPWNAM_R */
}

/**
 * Initializes the given DBusGroupInfo struct
 * with information about the given group name.
 *
 * @param info the group info struct
 * @param groupname name of group
 * @param error the error return
 * @returns #FALSE if error is set
 */
dbus_bool_t
_dbus_group_info_fill (DBusGroupInfo    *info,
                       const DBusString *groupname,
                       DBusError        *error)
{
  return fill_group_info (info, DBUS_GID_UNSET,
                          groupname, error);

}

/**
 * Initializes the given DBusGroupInfo struct
 * with information about the given group ID.
 *
 * @param info the group info struct
 * @param gid group ID
 * @param error the error return
 * @returns #FALSE if error is set
 */
dbus_bool_t
_dbus_group_info_fill_gid (DBusGroupInfo *info,
                           dbus_gid_t     gid,
                           DBusError     *error)
{
  return fill_group_info (info, gid, NULL, error);
}

/** @} */ /* End of DBusInternalsUtils functions */

/**
 * @addtogroup DBusString
 *
 * @{
 */
/**
 * Get the directory name from a complete filename
 * @param filename the filename
 * @param dirname string to append directory name to
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_string_get_dirname  (const DBusString *filename,
                           DBusString       *dirname)
{
  int sep;
  
  _dbus_assert (filename != dirname);
  _dbus_assert (filename != NULL);
  _dbus_assert (dirname != NULL);

  /* Ignore any separators on the end */
  sep = _dbus_string_get_length (filename);
  if (sep == 0)
    return _dbus_string_append (dirname, "."); /* empty string passed in */
    
  while (sep > 0 && _dbus_string_get_byte (filename, sep - 1) == '/')
    --sep;

  _dbus_assert (sep >= 0);
  
  if (sep == 0)
    return _dbus_string_append (dirname, "/");
  
  /* Now find the previous separator */
  _dbus_string_find_byte_backward (filename, sep, '/', &sep);
  if (sep < 0)
    return _dbus_string_append (dirname, ".");
  
  /* skip multiple separators */
  while (sep > 0 && _dbus_string_get_byte (filename, sep - 1) == '/')
    --sep;

  _dbus_assert (sep >= 0);
  
  if (sep == 0 &&
      _dbus_string_get_byte (filename, 0) == '/')
    return _dbus_string_append (dirname, "/");
  else
    return _dbus_string_copy_len (filename, 0, sep - 0,
                                  dirname, _dbus_string_get_length (dirname));
}
/** @} */ /* DBusString stuff */

