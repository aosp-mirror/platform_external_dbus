/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-sysdeps-util.c Would be in dbus-sysdeps.c, but not used in libdbus
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

/* #define ENABLE_DBUSGROUPINFO */

#ifdef ENABLE_DBUSGROUPINFO
typedef struct {
    int gid;
    char *groupname;
} DBusGroupInfo;
#endif

#undef open

#define STRSAFE_NO_DEPRECATE

#include "dbus-sysdeps.h"
#include "dbus-internals.h"
#include "dbus-protocol.h"
#include "dbus-string.h"
#include "dbus-sysdeps.h"
#include "dbus-sysdeps-win.h"
#include "dbus-memory.h"

#include <io.h>
#include <sys/stat.h>
#include <aclapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#ifdef __MINGW32__
/* save string functions version
   using DBusString needs to much time because of uncommon api 
*/ 
#define errno_t int

errno_t strcat_s(char *dest, int size, char *src) 
{
  _dbus_assert(strlen(dest) + strlen(src) +1 <= size);
  strcat(dest,src);
  return 0;
}

errno_t strcpy_s(char *dest, int size, char *src)
{
  _dbus_assert(strlen(src) +1 <= size);
  strcpy(dest,src);  
  return 0;
}
#endif

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
                     DBusPipe         *print_pid_pipe,
                     DBusError        *error)
{
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
  DBusFile file;
  FILE *f;

  cfilename = _dbus_string_get_const_data (filename);

  if (!_dbus_file_open(&file, cfilename, O_WRONLY|O_CREAT|O_EXCL|O_BINARY, 0644))
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to open \"%s\": %s", cfilename,
                      _dbus_strerror (errno));
      return FALSE;
    }

  if ((f = fdopen (file.FDATA, "w")) == NULL)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to fdopen fd %d: %s", file.FDATA, _dbus_strerror (errno));
      _dbus_file_close (&file, NULL);
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
 * Verify that after the fork we can successfully change to this user.
 *
 * @param user the username given in the daemon configuration
 * @returns #TRUE if username is valid
 */
dbus_bool_t
_dbus_verify_daemon_user (const char *user)
{
  return TRUE;
}

/**
 * Changes the user and group the bus is running as.
 *
 * @param user the user to become
 * @param error return location for errors
 * @returns #FALSE on failure
 */
dbus_bool_t
_dbus_change_to_daemon_user  (const char    *user,
                              DBusError     *error)
{
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
  return TRUE;
}

/** Checks if user is at the console
*
* @param username user to check
* @param error return location for errors
* @returns #TRUE is the user is at the consolei and there are no errors
*/
dbus_bool_t
_dbus_user_at_console(const char *username,
                      DBusError  *error)
{
#ifdef DBUS_WINCE
	return TRUE;
#else
  dbus_bool_t retval = FALSE;
  wchar_t *wusername;
  DWORD sid_length;
  PSID user_sid, console_user_sid;
  HWINSTA winsta;

  wusername = _dbus_win_utf8_to_utf16 (username, error);
  if (!wusername)
    return FALSE;

  if (!_dbus_win_account_to_sid (wusername, &user_sid, error))
    goto out0;

  /* Now we have the SID for username. Get the SID of the
   * user at the "console" (window station WinSta0)
   */
  if (!(winsta = OpenWindowStation ("WinSta0", FALSE, READ_CONTROL)))
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      goto out2;
    }

  sid_length = 0;
  GetUserObjectInformation (winsta, UOI_USER_SID,
                            NULL, 0, &sid_length);
  if (sid_length == 0)
    {
      /* Nobody is logged on */
      goto out2;
    }

  if (sid_length < 0 || sid_length > 1000)
    {
      dbus_set_error_const (error, DBUS_ERROR_FAILED, "Invalid SID length");
      goto out3;
    }

  console_user_sid = dbus_malloc (sid_length);
  if (!console_user_sid)
    {
      _DBUS_SET_OOM (error);
      goto out3;
    }

  if (!GetUserObjectInformation (winsta, UOI_USER_SID,
                                 console_user_sid, sid_length, &sid_length))
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      goto out4;
    }

  if (!IsValidSid (console_user_sid))
    {
      dbus_set_error_const (error, DBUS_ERROR_FAILED, "Invalid SID");
      goto out4;
    }

  retval = EqualSid (user_sid, console_user_sid);

out4:
  dbus_free (console_user_sid);
out3:
  CloseWindowStation (winsta);
out2:
  dbus_free (user_sid);
out0:
  dbus_free (wusername);

  return retval;
#endif //DBUS_WINCE
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

/** Installs a signal handler
 *
 * @param sig the signal to handle
 * @param handler the handler
 */
void
_dbus_set_signal_handler (int               sig,
                          DBusSignalHandler handler)
{
  _dbus_verbose ("_dbus_set_signal_handler() has to be implemented\n");
}

/** Checks if a file exists
*
* @param file full path to the file
* @returns #TRUE if file exists
*/
dbus_bool_t 
_dbus_file_exists (const char *file)
{
  HANDLE h = CreateFile(
          file, /* LPCTSTR lpFileName*/
          0, /* DWORD dwDesiredAccess */
          0, /* DWORD dwShareMode*/
          NULL, /* LPSECURITY_ATTRIBUTES lpSecurityAttributes */
          OPEN_EXISTING, /* DWORD dwCreationDisposition */
          FILE_ATTRIBUTE_NORMAL, /* DWORD dwFlagsAndAttributes */
          NULL /* HANDLE hTemplateFile */
        );

    /* file not found, use local copy of session.conf  */
    if (h != INVALID_HANDLE_VALUE && GetLastError() != ERROR_PATH_NOT_FOUND)
      {
        CloseHandle(h);
        return TRUE;
      }
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
_dbus_stat(const DBusString *filename,
           DBusStat         *statbuf,
           DBusError        *error)
{
#ifdef DBUS_WINCE
	return TRUE;
	//TODO
#else
  const char *filename_c;
#if !defined(DBUS_WIN) && !defined(DBUS_WINCE)

  struct stat sb;
#else

  WIN32_FILE_ATTRIBUTE_DATA wfad;
  char *lastdot;
  DWORD rc;
  PSID owner_sid, group_sid;
  PSECURITY_DESCRIPTOR sd;
#endif

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  filename_c = _dbus_string_get_const_data (filename);

  if (!GetFileAttributesEx (filename_c, GetFileExInfoStandard, &wfad))
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      return FALSE;
    }

  if (wfad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    statbuf->mode = _S_IFDIR;
  else
    statbuf->mode = _S_IFREG;

  statbuf->mode |= _S_IREAD;
  if (wfad.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
    statbuf->mode |= _S_IWRITE;

  lastdot = strrchr (filename_c, '.');
  if (lastdot && stricmp (lastdot, ".exe") == 0)
    statbuf->mode |= _S_IEXEC;

  statbuf->mode |= (statbuf->mode & 0700) >> 3;
  statbuf->mode |= (statbuf->mode & 0700) >> 6;

  statbuf->nlink = 1;

  sd = NULL;
  rc = GetNamedSecurityInfo ((char *) filename_c, SE_FILE_OBJECT,
                             OWNER_SECURITY_INFORMATION |
                             GROUP_SECURITY_INFORMATION,
                             &owner_sid, &group_sid,
                             NULL, NULL,
                             &sd);
  if (rc != ERROR_SUCCESS)
    {
      _dbus_win_set_error_from_win_error (error, rc);
      if (sd != NULL)
        LocalFree (sd);
      return FALSE;
    }

  statbuf->uid = _dbus_win_sid_to_uid_t (owner_sid);
  statbuf->gid = _dbus_win_sid_to_uid_t (group_sid);

  LocalFree (sd);

  statbuf->size = ((dbus_int64_t) wfad.nFileSizeHigh << 32) + wfad.nFileSizeLow;

  statbuf->atime =
    (((dbus_int64_t) wfad.ftLastAccessTime.dwHighDateTime << 32) +
     wfad.ftLastAccessTime.dwLowDateTime) / 10000000 - DBUS_INT64_CONSTANT (116444736000000000);

  statbuf->mtime =
    (((dbus_int64_t) wfad.ftLastWriteTime.dwHighDateTime << 32) +
     wfad.ftLastWriteTime.dwLowDateTime) / 10000000 - DBUS_INT64_CONSTANT (116444736000000000);

  statbuf->ctime =
    (((dbus_int64_t) wfad.ftCreationTime.dwHighDateTime << 32) +
     wfad.ftCreationTime.dwLowDateTime) / 10000000 - DBUS_INT64_CONSTANT (116444736000000000);

  return TRUE;
#endif //DBUS_WINCE
}


#ifdef HAVE_DIRENT_H

// mingw ships with dirent.h
#include <dirent.h>
#define _dbus_opendir opendir
#define _dbus_readdir readdir
#define _dbus_closedir closedir

#else

#ifdef HAVE_IO_H
#include <io.h> // win32 file functions
#endif

#include <sys/types.h>
#include <stdlib.h>

/* This file is part of the KDE project
Copyright (C) 2000 Werner Almesberger

libc/sys/linux/sys/dirent.h - Directory entry as returned by readdir

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.
*/
#define HAVE_NO_D_NAMLEN	/* no struct dirent->d_namlen */
#define HAVE_DD_LOCK  		/* have locking mechanism */

#define MAXNAMLEN 255		/* sizeof(struct dirent.d_name)-1 */

#define __dirfd(dir) (dir)->dd_fd

/* struct dirent - same as Unix */
struct dirent
  {
    long d_ino;                    /* inode (always 1 in WIN32) */
    off_t d_off;                /* offset to this dirent */
    unsigned short d_reclen;    /* length of d_name */
    char d_name[_MAX_FNAME+1];    /* filename (null terminated) */
  };

/* typedef DIR - not the same as Unix */
typedef struct
  {
    long handle;                /* _findfirst/_findnext handle */
    short offset;                /* offset into directory */
    short finished;             /* 1 if there are not more files */
    struct _finddata_t fileinfo;  /* from _findfirst/_findnext */
    char *dir;                  /* the dir we are reading */
    struct dirent dent;         /* the dirent to return */
  }
DIR;

/**********************************************************************
* Implement dirent-style opendir/readdir/closedir on Window 95/NT
*
* Functions defined are opendir(), readdir() and closedir() with the
* same prototypes as the normal dirent.h implementation.
*
* Does not implement telldir(), seekdir(), rewinddir() or scandir().
* The dirent struct is compatible with Unix, except that d_ino is
* always 1 and d_off is made up as we go along.
*
* The DIR typedef is not compatible with Unix.
**********************************************************************/

DIR * _dbus_opendir(const char *dir)
{
  DIR *dp;
  char *filespec;
  long handle;
  int index;

  filespec = malloc(strlen(dir) + 2 + 1);
  strcpy(filespec, dir);
  index = strlen(filespec) - 1;
  if (index >= 0 && (filespec[index] == '/' || filespec[index] == '\\'))
    filespec[index] = '\0';
  strcat(filespec, "\\*");

  dp = (DIR *)malloc(sizeof(DIR));
  dp->offset = 0;
  dp->finished = 0;
  dp->dir = strdup(dir);

  if ((handle = _findfirst(filespec, &(dp->fileinfo))) < 0)
    {
      if (errno == ENOENT)
        dp->finished = 1;
      else
        return NULL;
    }

  dp->handle = handle;
  free(filespec);

  return dp;
}

struct dirent * _dbus_readdir(DIR *dp)
  {
    if (!dp || dp->finished)
      return NULL;

    if (dp->offset != 0)
      {
        if (_findnext(dp->handle, &(dp->fileinfo)) < 0)
          {
            dp->finished = 1;
            errno = 0;
            return NULL;
          }
      }
    dp->offset++;

    strncpy(dp->dent.d_name, dp->fileinfo.name, _MAX_FNAME);
    dp->dent.d_ino = 1;
    dp->dent.d_reclen = strlen(dp->dent.d_name);
    dp->dent.d_off = dp->offset;

    return &(dp->dent);
  }


int _dbus_closedir(DIR *dp)
{
  if (!dp)
    return 0;
  _findclose(dp->handle);
  if (dp->dir)
    free(dp->dir);
  if (dp)
    free(dp);

  return 0;
}

#endif //#ifdef HAVE_DIRENT_H

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

  d = _dbus_opendir (filename_c);
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
      _dbus_closedir (d);
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY,
                      "Could not allocate memory for directory iterator");
      return NULL;
    }

  iter->d = d;

  return iter;
}

/**
 * Get next file in the directory. Will not return "." or ".."  on
 * UNIX. If an error occurs, the contents of "filename" are
 * undefined. The error is never set if the function succeeds.
 *
 * @todo for thread safety, I think we have to use
 * readdir_r(). (GLib has the same issue, should file a bug.)
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
  struct dirent *ent;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

again:
  errno = 0;
  ent = _dbus_readdir (iter->d);
  if (ent == NULL)
    {
      if (errno != 0)
        dbus_set_error (error,
                        _dbus_error_from_errno (errno),
                        "%s", _dbus_strerror (errno));
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
          return FALSE;
        }
      else
        return TRUE;
    }
}

/**
 * Closes a directory iteration.
 */
void
_dbus_directory_close (DBusDirIter *iter)
{
  _dbus_closedir (iter->d);
  dbus_free (iter);
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
    return _dbus_string_get_byte (filename, 1) == ':'
           || _dbus_string_get_byte (filename, 0) == '\\'
           || _dbus_string_get_byte (filename, 0) == '/';
  else
    return FALSE;
}

#ifdef ENABLE_DBUSGROPINFO
static dbus_bool_t
fill_group_info(DBusGroupInfo    *info,
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

  if (group_c_str)
    {
      PSID group_sid;
      wchar_t *wgroupname = _dbus_win_utf8_to_utf16 (group_c_str, error);

      if (!wgroupname)
        return FALSE;

      if (!_dbus_win_account_to_sid (wgroupname, &group_sid, error))
        {
          dbus_free (wgroupname);
          return FALSE;
        }

      info->gid = _dbus_win_sid_to_uid_t (group_sid);
      info->groupname = _dbus_strdup (group_c_str);

      dbus_free (group_sid);
      dbus_free (wgroupname);

      return TRUE;
    }
  else
    {
      dbus_bool_t retval = FALSE;
      wchar_t *wname, *wdomain;
      char *name, *domain;

      info->gid = gid;

      if (!_dbus_win_sid_to_name_and_domain (gid, &wname, &wdomain, error))
        return FALSE;

      name = _dbus_win_utf16_to_utf8 (wname, error);
      if (!name)
        goto out0;

      domain = _dbus_win_utf16_to_utf8 (wdomain, error);
      if (!domain)
        goto out1;

      info->groupname = dbus_malloc (strlen (domain) + 1 + strlen (name) + 1);

      strcpy (info->groupname, domain);
      strcat (info->groupname, "\\");
      strcat (info->groupname, name);

      retval = TRUE;

      dbus_free (domain);
out1:
      dbus_free (name);
out0:
      dbus_free (wname);
      dbus_free (wdomain);

      return retval;
    }
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
#endif

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
_dbus_string_get_dirname(const DBusString *filename,
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

  while (sep > 0 &&
         (_dbus_string_get_byte (filename, sep - 1) == '/' ||
          _dbus_string_get_byte (filename, sep - 1) == '\\'))
    --sep;

  _dbus_assert (sep >= 0);

  if (sep == 0 ||
      (sep == 2 &&
       _dbus_string_get_byte (filename, 1) == ':' &&
       isalpha (_dbus_string_get_byte (filename, 0))))
    return _dbus_string_copy_len (filename, 0, sep + 1,
                                  dirname, _dbus_string_get_length (dirname));

  {
    int sep1, sep2;
    _dbus_string_find_byte_backward (filename, sep, '/', &sep1);
    _dbus_string_find_byte_backward (filename, sep, '\\', &sep2);

    sep = MAX (sep1, sep2);
  }
  if (sep < 0)
    return _dbus_string_append (dirname, ".");

  while (sep > 0 &&
         (_dbus_string_get_byte (filename, sep - 1) == '/' ||
          _dbus_string_get_byte (filename, sep - 1) == '\\'))
    --sep;

  _dbus_assert (sep >= 0);

  if ((sep == 0 ||
       (sep == 2 &&
        _dbus_string_get_byte (filename, 1) == ':' &&
        isalpha (_dbus_string_get_byte (filename, 0))))
      &&
      (_dbus_string_get_byte (filename, sep) == '/' ||
       _dbus_string_get_byte (filename, sep) == '\\'))
    return _dbus_string_copy_len (filename, 0, sep + 1,
                                  dirname, _dbus_string_get_length (dirname));
  else
    return _dbus_string_copy_len (filename, 0, sep - 0,
                                  dirname, _dbus_string_get_length (dirname));
}


/**
 * Checks to see if the UNIX user ID matches the UID of
 * the process. Should always return #FALSE on Windows.
 *
 * @param uid the UNIX user ID
 * @returns #TRUE if this uid owns the process.
 */
dbus_bool_t
_dbus_unix_user_is_process_owner (dbus_uid_t uid)
{
  return FALSE;
}

/*=====================================================================
  unix emulation functions - should be removed sometime in the future
 =====================================================================*/

/**
 * Checks to see if the UNIX user ID is at the console.
 * Should always fail on Windows (set the error to
 * #DBUS_ERROR_NOT_SUPPORTED).
 *
 * @param uid UID of person to check 
 * @param error return location for errors
 * @returns #TRUE if the UID is the same as the console user and there are no errors
 */
dbus_bool_t
_dbus_unix_user_is_at_console (dbus_uid_t         uid,
                               DBusError         *error)
{
  return FALSE;
}


/**
 * Parse a UNIX group from the bus config file. On Windows, this should
 * simply always fail (just return #FALSE).
 *
 * @param groupname the groupname text
 * @param gid_p place to return the gid
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_parse_unix_group_from_config (const DBusString  *groupname,
                                    dbus_gid_t        *gid_p)
{
  return FALSE;
}

/**
 * Parse a UNIX user from the bus config file. On Windows, this should
 * simply always fail (just return #FALSE).
 *
 * @param username the username text
 * @param uid_p place to return the uid
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_parse_unix_user_from_config (const DBusString  *username,
                                   dbus_uid_t        *uid_p)
{
  return FALSE;
}


/**
 * Gets all groups corresponding to the given UNIX user ID. On UNIX,
 * just calls _dbus_groups_from_uid(). On Windows, should always
 * fail since we don't know any UNIX groups.
 *
 * @param uid the UID
 * @param group_ids return location for array of group IDs
 * @param n_group_ids return location for length of returned array
 * @returns #TRUE if the UID existed and we got some credentials
 */
dbus_bool_t
_dbus_unix_groups_from_uid (dbus_uid_t            uid,
                            dbus_gid_t          **group_ids,
                            int                  *n_group_ids)
{
  return FALSE;
}



/** @} */ /* DBusString stuff */

