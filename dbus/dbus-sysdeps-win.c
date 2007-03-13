/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-sysdeps.c Wrappers around system/libc features (internal to D-BUS implementation)
 * 
 * Copyright (C) 2002, 2003  Red Hat, Inc.
 * Copyright (C) 2003 CodeFactory AB
 * Copyright (C) 2005 Novell, Inc.
 * Copyright (C) 2006 Ralf Habacker <ralf.habacker@freenet.de>
 * Copyright (C) 2006 Peter Kümmel  <syntheticpp@gmx.net>
 * Copyright (C) 2006 Christian Ehrlicher <ch.ehrlicher@gmx.de>
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
#undef open

#define STRSAFE_NO_DEPRECATE

#ifndef DBUS_WINCE
#define _WIN32_WINNT 0x0500
#endif

#include "dbus-internals.h"
#include "dbus-sysdeps.h"
#include "dbus-threads.h"
#include "dbus-protocol.h"
#include "dbus-string.h"
#include "dbus-sysdeps-win.h"
#include "dbus-protocol.h"
#include "dbus-hash.h"
#include "dbus-sockets-win.h"
#include "dbus-userdb.h"
#include "dbus-list.h"

#include <windows.h>
#include <fcntl.h>

#include <process.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef HAVE_SOCKLEN_T
#define socklen_t int
#endif

_DBUS_DEFINE_GLOBAL_LOCK (win_fds);
_DBUS_DEFINE_GLOBAL_LOCK (sid_atom_cache);


static
void 
_dbus_lock_sockets()
{
	_dbus_assert (win_fds!=0); 
	_DBUS_LOCK   (win_fds);
}

static
void 
_dbus_unlock_sockets()
{
	_dbus_assert (win_fds!=0); 
	_DBUS_UNLOCK (win_fds);
}

#ifdef _DBUS_WIN_USE_RANDOMIZER
static int  win_encap_randomizer;
#endif
static DBusHashTable *sid_atom_cache = NULL;


static DBusString dbusdir;
static int working_dir_init = 0;

int _dbus_init_working_dir(char *s)
{
  /* change working directory to one level above 
     of dbus-daemon executable path.  
     This allows the usage of relative path in 
     config files or command line parameters */
  DBusString daemon_path,bin_path;

  if (!_dbus_string_init (&daemon_path))
    return FALSE;
  
  if (!_dbus_string_init (&bin_path))
    return FALSE;

  if (!_dbus_string_init (&dbusdir))
    return FALSE;
  
  _dbus_string_append(&daemon_path,s);
  _dbus_string_get_dirname(&daemon_path,&bin_path);
  _dbus_string_get_dirname(&bin_path,&dbusdir);
  chdir(_dbus_string_get_const_data(&dbusdir));
  _dbus_verbose ("Change working path to %s\n",_dbus_string_get_const_data (&dbusdir));
  working_dir_init = 1;
  return TRUE;
}

DBusString *_dbus_get_working_dir(void)
{
  if (!working_dir_init) 
    return NULL;
	
  _dbus_verbose ("retrieving working path %s\n",_dbus_string_get_const_data (&dbusdir));
  return &dbusdir;
}

/**
 * File interface
 *
 */
dbus_bool_t
_dbus_file_open (DBusFile   *file,
                 const char *filename,
                 int         oflag,
                 int         pmode)
{
  if (pmode!=-1)
    file->FDATA = _open (filename, oflag, pmode);
  else
    file->FDATA = _open (filename, oflag);
  if (file->FDATA >= 0)
    return TRUE;
  else
    {
      file->FDATA = -1;
      return FALSE;
    }
}

dbus_bool_t
_dbus_file_close (DBusFile  *file,
                  DBusError *error)
{
  const int fd = file->FDATA;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  _dbus_assert (fd >= 0);

  if (_close (fd) == -1)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Could not close fd %d: %s", fd,
                      _dbus_strerror (errno));
      return FALSE;
    }

  file->FDATA = -1;
  _dbus_verbose ("closed C file descriptor %d:\n",fd);

  return TRUE;
}

int
_dbus_file_read(DBusFile   *file,
                DBusString *buffer,
                int         count)
{
  const int fd = file->FDATA;
  int bytes_read;
  int start;
  char *data;
  _dbus_assert (count >= 0);

  start = _dbus_string_get_length (buffer);

  if (!_dbus_string_lengthen (buffer, count))
    {
      errno = ENOMEM;
      return -1;
    }

  data = _dbus_string_get_data_len (buffer, start, count);

  _dbus_assert (fd >= 0);

  _dbus_verbose ("read: count=%d fd=%d\n", count, fd);
  bytes_read = read (fd, data, count);

  if (bytes_read == -1)
    _dbus_verbose ("read: failed: %s\n", _dbus_strerror (errno));
  else
    _dbus_verbose ("read: = %d\n", bytes_read);

  if (bytes_read < 0)
    {
      /* put length back (note that this doesn't actually realloc anything) */
      _dbus_string_set_length (buffer, start);
      return -1;
    }
  else
    {
      /* put length back (doesn't actually realloc) */
      _dbus_string_set_length (buffer, start + bytes_read);

#if 0

      if (bytes_read > 0)
        _dbus_verbose_bytes_of_string (buffer, start, bytes_read);
#endif

      return bytes_read;
    }
}

int
_dbus_file_write (DBusFile         *file,
                  const DBusString *buffer,
                  int               start,
                  int               len)
{
  const int fd = file->FDATA;
  const char *data;
  int bytes_written;

  data = _dbus_string_get_const_data_len (buffer, start, len);

  _dbus_assert (fd >= 0);

  _dbus_verbose ("write: len=%d fd=%d\n", len, fd);
  bytes_written = write (fd, data, len);

  if (bytes_written == -1)
    _dbus_verbose ("write: failed: %s\n", _dbus_strerror (errno));
  else
    _dbus_verbose ("write: = %d\n", bytes_written);

#if 0

  if (bytes_written > 0)
    _dbus_verbose_bytes_of_string (buffer, start, bytes_written);
#endif

  return bytes_written;
}

dbus_bool_t
_dbus_is_valid_file (DBusFile* file)
{
  return file->FDATA >= 0;
}

dbus_bool_t _dbus_fstat (DBusFile    *file,
                         struct stat *sb)
{
  return fstat(file->FDATA, sb) >= 0;
}

/**
 * write data to a pipe.
 *
 * @param pipe the pipe instance
 * @param buffer the buffer to write data from
 * @param start the first byte in the buffer to write
 * @param len the number of bytes to try to write
 * @param error error return
 * @returns the number of bytes written or -1 on error
 */
int
_dbus_pipe_write (DBusPipe         *pipe,
                  const DBusString *buffer,
                  int               start,
                  int               len,
                  DBusError        *error)
{
  int written;
  DBusFile file;
  file.FDATA = pipe->fd_or_handle;
  written = _dbus_file_write (&file, buffer, start, len);
  if (written < 0)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Writing to pipe: %s\n",
                      _dbus_strerror (errno));
    }
  return written;
}

/**
 * close a pipe.
 *
 * @param pipe the pipe instance
 * @param error return location for an error
 * @returns #FALSE if error is set
 */
int
_dbus_pipe_close  (DBusPipe         *pipe,
                   DBusError        *error)
{
  DBusFile file;
  file.FDATA = pipe->fd_or_handle;
  if (_dbus_file_close (&file, error) < 0)
    {
      return -1;
    }
  else
    {
      _dbus_pipe_invalidate (pipe);
      return 0;
    }
}

#undef FDATA

/**
 * Socket interface
 *
 */

static DBusSocket *win_fds = NULL;
static int win_n_fds = 0; // is this the size? rename to win_fds_size? #

#if 0
#define TO_HANDLE(n)   ((n)^win32_encap_randomizer)
#define FROM_HANDLE(n) ((n)^win32_encap_randomizer)
#else
#define TO_HANDLE(n)   ((n)+0x10000000)
#define FROM_HANDLE(n) ((n)-0x10000000)
#define IS_HANDLE(n)   ((n)&0x10000000)
#endif


static
void
_dbus_win_deallocate_fd (int fd)
{
  _DBUS_LOCK (win_fds);
  win_fds[FROM_HANDLE (fd)].is_used = 0;
  _DBUS_UNLOCK (win_fds);
}

static
int
_dbus_win_allocate_fd (void)
{
  int i;

  _DBUS_LOCK (win_fds);

  if (win_fds == NULL)
    {
#ifdef _DBUS_WIN_USE_RANDOMIZER
      DBusString random;
#endif

      win_n_fds = 16;
      /* Use malloc to avoid memory leak failure in dbus-test */
      win_fds = malloc (win_n_fds * sizeof (*win_fds));

      _dbus_assert (win_fds != NULL);

      for (i = 0; i < win_n_fds; i++)
        win_fds[i].is_used = 0;

#ifdef _DBUS_WIN_USE_RANDOMIZER

      _dbus_string_init (&random);
      _dbus_generate_random_bytes (&random, sizeof (int));
      memmove (&win_encap_randomizer, _dbus_string_get_const_data (&random), sizeof (int));
      win_encap_randomizer &= 0xFF;
      _dbus_string_free (&random);
#endif

    }

  for (i = 0; i < win_n_fds && win_fds[i].is_used != 0; i++)
    ;

  if (i == win_n_fds)
    {
      int oldn = win_n_fds;
      int j;

      win_n_fds += 16;
      win_fds = realloc (win_fds, win_n_fds * sizeof (*win_fds));

      _dbus_assert (win_fds != NULL);

      for (j = oldn; j < win_n_fds; j++)
        win_fds[i].is_used = 0;
    }

  memset(&win_fds[i], 0, sizeof(win_fds[i]));

  win_fds[i].is_used = 1;
  win_fds[i].fd = -1;
  win_fds[i].port_file_fd = -1;
  win_fds[i].close_on_exec = FALSE;
  win_fds[i].non_blocking = FALSE;

  _DBUS_UNLOCK (win_fds);

  return i;
}

static
int
_dbus_create_handle_from_socket (int s)
{
  int i;
  int handle = -1;

  // check: parameter must be a valid value
  _dbus_assert(s != -1);
  _dbus_assert(!IS_HANDLE(s));

  // get index of a new position in the map
  i = _dbus_win_allocate_fd ();

  // fill new posiiton in the map: value->index
  win_fds[i].fd = s;
  win_fds[i].is_used = 1;

  // create handle from the index: index->handle
  handle = TO_HANDLE (i);

  _dbus_verbose ("_dbus_create_handle_from_value, value: %d, handle: %d\n", s, handle);

  return handle;
}

int
_dbus_socket_to_handle (DBusSocket *s)
{
  int i;
  int handle = -1;

  // check: parameter must be a valid socket
  _dbus_assert(s != NULL);
  _dbus_assert(s->fd != -1);
  _dbus_assert(!IS_HANDLE(s->fd));

  _DBUS_LOCK (win_fds);

  // at the first call there is no win_fds
  // will be constructed  _dbus_create_handle_from_socket
  // because handle = -1
  if (win_fds != NULL)
    {
      // search for the value in the map
      // find the index of the value: value->index
      for (i = 0; i < win_n_fds; i++)
        if (win_fds[i].is_used == 1 && win_fds[i].fd == s->fd)
          {
            // create handle from the index: index->handle
            handle = TO_HANDLE (i);
            break;
          }
    }
  _DBUS_UNLOCK (win_fds);


  if (handle == -1)
    {
      handle = _dbus_create_handle_from_socket(s->fd);
    }

  _dbus_assert(handle != -1);

  return handle;
}

static
void 
_dbus_handle_to_socket_unlocked (int          handle,
                                 DBusSocket **ptr)
{
  int i;

  // check: parameter must be a valid handle
  _dbus_assert(handle != -1);
  _dbus_assert(IS_HANDLE(handle));
  _dbus_assert(ptr != NULL);

  // map from handle to index: handle->index
  i = FROM_HANDLE (handle);

  _dbus_assert (win_fds != NULL);
  _dbus_assert (i >= 0 && i < win_n_fds);

  // check for if fd is valid
  _dbus_assert (win_fds[i].is_used == 1);

  // get socket from index: index->socket
  *ptr = &win_fds[i];

  _dbus_verbose ("_dbus_socket_to_handle_unlocked: socket=%d, handle=%d, index=%d\n", (*ptr)->fd, handle, i);
}

void 
_dbus_handle_to_socket (int          handle,
                        DBusSocket **ptr)
{
  _dbus_lock_sockets();
  _dbus_handle_to_socket_unlocked (handle, ptr);
  _dbus_unlock_sockets();
}

#undef TO_HANDLE
#undef IS_HANDLE
#undef FROM_HANDLE
#define FROM_HANDLE(n) 1==DBUS_WIN_DONT_USE__FROM_HANDLE__DIRECTLY
#define win_fds 1==DBUS_WIN_DONT_USE_win_fds_DIRECTLY




/**
 * Thin wrapper around the read() system call that appends
 * the data it reads to the DBusString buffer. It appends
 * up to the given count, and returns the same value
 * and same errno as read(). The only exception is that
 * _dbus_read() handles EINTR for you. _dbus_read() can
 * return ENOMEM, even though regular UNIX read doesn't.
 *
 * @param fd the file descriptor to read from
 * @param buffer the buffer to append data to
 * @param count the amount of data to read
 * @returns the number of bytes read or -1
 */
int
_dbus_read_socket (int               handle,
                   DBusString       *buffer,
                   int               count)
{
  DBusSocket *s;
  int bytes_read;
  int start;
  char *data;

  _dbus_assert (count >= 0);

  start = _dbus_string_get_length (buffer);

  if (!_dbus_string_lengthen (buffer, count))
    {
      errno = ENOMEM;
      return -1;
    }

  data = _dbus_string_get_data_len (buffer, start, count);

  _dbus_handle_to_socket(handle, &s);

  if(s->is_used)
    {
      _dbus_verbose ("recv: count=%d socket=%d\n", count, s->fd);
      bytes_read = recv (s->fd, data, count, 0);
      if (bytes_read == SOCKET_ERROR)
        {
          DBUS_SOCKET_SET_ERRNO();
          _dbus_verbose ("recv: failed: %s\n", _dbus_strerror (errno));
          bytes_read = -1;
        }
      else
        _dbus_verbose ("recv: = %d\n", bytes_read);
    }
  else
    {
      _dbus_assert_not_reached ("no valid socket");
    }

  if (bytes_read < 0)
    {
      /* put length back (note that this doesn't actually realloc anything) */
      _dbus_string_set_length (buffer, start);
      return -1;
    }
  else
    {
      /* put length back (doesn't actually realloc) */
      _dbus_string_set_length (buffer, start + bytes_read);

#if 0

      if (bytes_read > 0)
        _dbus_verbose_bytes_of_string (buffer, start, bytes_read);
#endif

      return bytes_read;
    }
}

/**
 * Thin wrapper around the write() system call that writes a part of a
 * DBusString and handles EINTR for you.
 * 
 * @param fd the file descriptor to write
 * @param buffer the buffer to write data from
 * @param start the first byte in the buffer to write
 * @param len the number of bytes to try to write
 * @returns the number of bytes written or -1 on error
 */
int
_dbus_write_socket (int               handle,
                    const DBusString *buffer,
                    int               start,
                    int               len)
{
  DBusSocket *s;
  int is_used;
  const char *data;
  int bytes_written;

  data = _dbus_string_get_const_data_len (buffer, start, len);

  _dbus_handle_to_socket(handle, &s);

  if (s->is_used)
    {
      _dbus_verbose ("send: len=%d socket=%d\n", len, s->fd);
      bytes_written = send (s->fd, data, len, 0);
      if (bytes_written == SOCKET_ERROR)
        {
          DBUS_SOCKET_SET_ERRNO();
          _dbus_verbose ("send: failed: %s\n", _dbus_strerror (errno));
          bytes_written = -1;
        }
      else
        _dbus_verbose ("send: = %d\n", bytes_written);
    }
  else
    {
      _dbus_assert_not_reached ("unhandled fd type");
    }

#if 0
  if (bytes_written > 0)
    _dbus_verbose_bytes_of_string (buffer, start, bytes_written);
#endif

  return bytes_written;
}


/**
 * Closes a file descriptor.
 *
 * @param fd the file descriptor
 * @param error error object
 * @returns #FALSE if error set
 */
dbus_bool_t
_dbus_close_socket (int        handle,
                    DBusError *error)
{
  DBusSocket *s;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  _dbus_lock_sockets();

  _dbus_handle_to_socket_unlocked (handle, &s);


  if (s->is_used)
    {
      if (s->port_file_fd >= 0)
        {
          _chsize (s->port_file_fd, 0);
          close (s->port_file_fd);
          s->port_file_fd = -1;
          unlink (_dbus_string_get_const_data (&s->port_file));
          free ((char *) _dbus_string_get_const_data (&s->port_file));
        }

      if (closesocket (s->fd) == SOCKET_ERROR)
        {
          DBUS_SOCKET_SET_ERRNO ();
          dbus_set_error (error, _dbus_error_from_errno (errno),
              "Could not close socket: socket=%d, handle=%d, %s",
                          s->fd, handle, _dbus_strerror (errno));
          _dbus_unlock_sockets();
          return FALSE;
        }
      _dbus_verbose ("_dbus_close_socket: socket=%d, handle=%d\n",
                     s->fd, handle);
    }
  else
    {
      _dbus_assert_not_reached ("unhandled fd type");
    }

  _dbus_unlock_sockets();

  _dbus_win_deallocate_fd (handle);

  return TRUE;

}

/**
 * Sets the file descriptor to be close
 * on exec. Should be called for all file
 * descriptors in D-Bus code.
 *
 * @param fd the file descriptor
 */
void
_dbus_fd_set_close_on_exec (int handle)
{
  DBusSocket *s;
  if (handle < 0)
    return;

  _dbus_lock_sockets();

  _dbus_handle_to_socket_unlocked (handle, &s);
  s->close_on_exec = TRUE;

  _dbus_unlock_sockets();
}

/**
 * Sets a file descriptor to be nonblocking.
 *
 * @param fd the file descriptor.
 * @param error address of error location.
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_set_fd_nonblocking (int             handle,
                          DBusError      *error)
{
  DBusSocket *s;
  u_long one = 1;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  _dbus_lock_sockets();

  _dbus_handle_to_socket_unlocked(handle, &s);

  if (s->is_used)
    {
      if (ioctlsocket (s->fd, FIONBIO, &one) == SOCKET_ERROR)
        {
          dbus_set_error (error, _dbus_error_from_errno (WSAGetLastError ()),
                          "Failed to set socket %d:%d to nonblocking: %s", s->fd,
                          _dbus_strerror (WSAGetLastError ()));
          _dbus_unlock_sockets();
          return FALSE;
        }
    }
  else
    {
      _dbus_assert_not_reached ("unhandled fd type");
    }

  _dbus_unlock_sockets();

  return TRUE;
}


/**
 * Like _dbus_write() but will use writev() if possible
 * to write both buffers in sequence. The return value
 * is the number of bytes written in the first buffer,
 * plus the number written in the second. If the first
 * buffer is written successfully and an error occurs
 * writing the second, the number of bytes in the first
 * is returned (i.e. the error is ignored), on systems that
 * don't have writev. Handles EINTR for you.
 * The second buffer may be #NULL.
 *
 * @param fd the file descriptor
 * @param buffer1 first buffer
 * @param start1 first byte to write in first buffer
 * @param len1 number of bytes to write from first buffer
 * @param buffer2 second buffer, or #NULL
 * @param start2 first byte to write in second buffer
 * @param len2 number of bytes to write in second buffer
 * @returns total bytes written from both buffers, or -1 on error
 */
int
_dbus_write_socket_two (int               handle,
                        const DBusString *buffer1,
                        int               start1,
                        int               len1,
                        const DBusString *buffer2,
                        int               start2,
                        int               len2)
{
  DBusSocket *s;
  WSABUF vectors[2];
  const char *data1;
  const char *data2;
  int rc;
  DWORD bytes_written;
  int ret1;

  _dbus_assert (buffer1 != NULL);
  _dbus_assert (start1 >= 0);
  _dbus_assert (start2 >= 0);
  _dbus_assert (len1 >= 0);
  _dbus_assert (len2 >= 0);

  _dbus_handle_to_socket(handle, &s);

  data1 = _dbus_string_get_const_data_len (buffer1, start1, len1);

  if (buffer2 != NULL)
    data2 = _dbus_string_get_const_data_len (buffer2, start2, len2);
  else
    {
      data2 = NULL;
      start2 = 0;
      len2 = 0;
    }

  if (s->is_used)
    {
      vectors[0].buf = (char*) data1;
      vectors[0].len = len1;
      vectors[1].buf = (char*) data2;
      vectors[1].len = len2;

      _dbus_verbose ("WSASend: len1+2=%d+%d socket=%d\n", len1, len2, s->fd);
      rc = WSASend (s->fd, vectors, data2 ? 2 : 1, &bytes_written,
                    0, NULL, NULL);
      if (rc < 0)
        {
          DBUS_SOCKET_SET_ERRNO ();
          _dbus_verbose ("WSASend: failed: %s\n", _dbus_strerror (errno));
          bytes_written = -1;
        }
      else
        _dbus_verbose ("WSASend: = %ld\n", bytes_written);
      return bytes_written;
    }
  else
    {
      _dbus_assert_not_reached ("unhandled fd type");
    }
  return 0;
}

/**
 * @def _DBUS_MAX_SUN_PATH_LENGTH
 *
 * Maximum length of the path to a UNIX domain socket,
 * sockaddr_un::sun_path member. POSIX requires that all systems
 * support at least 100 bytes here, including the nul termination.
 * We use 99 for the max value to allow for the nul.
 *
 * We could probably also do sizeof (addr.sun_path)
 * but this way we are the same on all platforms
 * which is probably a good idea.
 */

/**
 * Creates a socket and connects it to the UNIX domain socket at the
 * given path.  The connection fd is returned, and is set up as
 * nonblocking.
 * 
 * On Windows there are no UNIX domain sockets. Instead, connects to a
 * localhost-bound TCP socket, whose port number is stored in a file
 * at the given path.
 * 
 * Uses abstract sockets instead of filesystem-linked sockets if
 * requested (it's possible only on Linux; see "man 7 unix" on Linux).
 * On non-Linux abstract socket usage always fails.
 *
 * @param path the path to UNIX domain socket
 * @param abstract #TRUE to use abstract namespace
 * @param error return location for error code
 * @returns connection file descriptor or -1 on error
 */
int
_dbus_connect_unix_socket (const char     *path,
                           dbus_bool_t     abstract,
                           DBusError      *error)
{
#ifdef DBUS_WINCE
	return -1;
#else
  int fd, n, port;
  char buf[7];

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  _dbus_verbose ("connecting to pseudo-unix socket at %s\n",
                 path);

  if (abstract)
    {
      dbus_set_error (error, DBUS_ERROR_NOT_SUPPORTED,
                      "Implementation does not support abstract socket namespace\n");
      return -1;
    }

  fd = _sopen (path, O_RDONLY, SH_DENYNO);

  if (fd == -1)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to open file %s: %s",
                      path, _dbus_strerror (errno));
      return -1;
    }

  n = read (fd, buf, sizeof (buf) - 1);
  close (fd);

  if (n == 0)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Failed to read port number from file %s",
                      path);
      return -1;
    }

  buf[n] = '\0';
  port = atoi (buf);

  if (port <= 0 || port > 0xFFFF)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Invalid port numer in file %s",
                      path);
      return -1;
    }

  return _dbus_connect_tcp_socket (NULL, port, error);
#endif //DBUS_WINCE

}

/**
 * Creates a socket and binds it to the given path,
 * then listens on the socket. The socket is
 * set to be nonblocking.
 *
 * Uses abstract sockets instead of filesystem-linked
 * sockets if requested (it's possible only on Linux;
 * see "man 7 unix" on Linux).
 * On non-Linux abstract socket usage always fails.
 *
 * @param path the socket name
 * @param abstract #TRUE to use abstract namespace
 * @param error return location for errors
 * @returns the listening file descriptor or -1 on error
 */
int
_dbus_listen_unix_socket (const char     *path,
                          dbus_bool_t     abstract,
                          DBusError      *error)
{
#ifdef DBUS_WINCE
	return -1;
#else
  DBusSocket *s;
  int listen_handle;
  struct sockaddr sa;
  int addr_len;
  int filefd;
  int n, l;
  DBusString portstr;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  _dbus_verbose ("listening on pseudo-unix socket at %s\n",
                 path);

  if (abstract)
    {
      dbus_set_error (error, DBUS_ERROR_NOT_SUPPORTED,
                      "Implementation does not support abstract socket namespace\n");
      return -1;
    }

  listen_handle = _dbus_listen_tcp_socket (NULL, 0, error);

  if (listen_handle == -1)
    return -1;

  _dbus_handle_to_socket(listen_handle, &s);

  addr_len = sizeof (sa);
  if (getsockname (s->fd, &sa, &addr_len) == SOCKET_ERROR)
    {
      DBUS_SOCKET_SET_ERRNO ();
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "getsockname failed: %s",
                      _dbus_strerror (errno));
      _dbus_close_socket (listen_handle, NULL);
      return -1;
    }

  _dbus_assert (((struct sockaddr_in*) &sa)->sin_family == AF_INET);

  filefd = _sopen (path, O_CREAT|O_WRONLY|_O_SHORT_LIVED, SH_DENYWR, 0666);

  if (filefd == -1)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to create pseudo-unix socket port number file %s: %s",
                      path, _dbus_strerror (errno));
      _dbus_close_socket (listen_handle, NULL);
      return -1;
    }

  _dbus_lock_sockets();
  _dbus_handle_to_socket_unlocked(listen_handle, &s);
  s->port_file_fd = filefd;
  _dbus_unlock_sockets();

  /* Use strdup() to avoid memory leak in dbus-test */
  path = strdup (path);
  if (!path)
    {
      _DBUS_SET_OOM (error);
      _dbus_close_socket (listen_handle, NULL);
      return -1;
    }

  _dbus_string_init_const (&s->port_file, path);

  if (!_dbus_string_init (&portstr))
    {
      _DBUS_SET_OOM (error);
      _dbus_close_socket (listen_handle, NULL);
      return -1;
    }

  if (!_dbus_string_append_int (&portstr, ntohs (((struct sockaddr_in*) &sa)->sin_port)))
    {
      _DBUS_SET_OOM (error);
      _dbus_close_socket (listen_handle, NULL);
      return -1;
    }

  l = _dbus_string_get_length (&portstr);
  n = write (filefd, _dbus_string_get_const_data (&portstr), l);
  _dbus_string_free (&portstr);

  if (n == -1)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to write port number to file %s: %s",
                      path, _dbus_strerror (errno));
      _dbus_close_socket (listen_handle, NULL);
      return -1;
    }
  else if (n < l)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to write port number to file %s",
                      path);
      _dbus_close_socket (listen_handle, NULL);
      return -1;
    }

  return listen_handle;
#endif //DBUS_WINCE
}

#if 0

/**
 * Opens the client side of a Windows named pipe. The connection D-BUS
 * file descriptor index is returned. It is set up as nonblocking.
 * 
 * @param path the path to named pipe socket
 * @param error return location for error code
 * @returns connection D-BUS file descriptor or -1 on error
 */
int
_dbus_connect_named_pipe (const char     *path,
                          DBusError      *error)
{
  _dbus_assert_not_reached ("not implemented");
}

#endif


dbus_bool_t
_dbus_account_to_win_sid (const wchar_t  *waccount,
                          void          **ppsid,
                          DBusError      *error)
{
  dbus_bool_t retval = FALSE;
  DWORD sid_length, wdomain_length;
  SID_NAME_USE use;
  wchar_t *wdomain;

  *ppsid = NULL;

  sid_length = 0;
  wdomain_length = 0;
  if (!LookupAccountNameW (NULL, waccount, NULL, &sid_length,
                           NULL, &wdomain_length, &use)
      && GetLastError () != ERROR_INSUFFICIENT_BUFFER)
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      return FALSE;
    }

  *ppsid = dbus_malloc (sid_length);
  if (!*ppsid)
    {
      _DBUS_SET_OOM (error);
      return FALSE;
    }

  wdomain = dbus_new (wchar_t, wdomain_length);
  if (!wdomain)
    {
      _DBUS_SET_OOM (error);
      goto out1;
    }

  if (!LookupAccountNameW (NULL, waccount, (PSID) *ppsid, &sid_length,
                           wdomain, &wdomain_length, &use))
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      goto out2;
    }

  if (!IsValidSid ((PSID) *ppsid))
    {
      dbus_set_error_const (error, DBUS_ERROR_FAILED, "Invalid SID");
      goto out2;
    }

  retval = TRUE;

out2:
  dbus_free (wdomain);
out1:
  if (!retval)
    {
      dbus_free (*ppsid);
      *ppsid = NULL;
    }

  return retval;
}


dbus_bool_t
fill_win_user_info_name_and_groups (wchar_t 	  *wname,
                                    wchar_t 	  *wdomain,
                                    DBusUserInfo *info,
                                    DBusError    *error)
{
#ifdef DBUS_WINCE
	return TRUE;
#else
  dbus_bool_t retval = FALSE;
  char *name, *domain;
  LPLOCALGROUP_USERS_INFO_0 local_groups = NULL;
  LPGROUP_USERS_INFO_0 global_groups = NULL;
  DWORD nread, ntotal;

  name = _dbus_win_utf16_to_utf8 (wname, error);
  if (!name)
    return FALSE;

  domain = _dbus_win_utf16_to_utf8 (wdomain, error);
  if (!domain)
    goto out0;

  info->username = dbus_malloc (strlen (domain) + 1 + strlen (name) + 1);
  if (!info->username)
    {
      _DBUS_SET_OOM (error);
      goto out1;
    }

  strcpy (info->username, domain);
  strcat (info->username, "\\");
  strcat (info->username, name);

  info->n_group_ids = 0;
  if (NetUserGetLocalGroups (NULL, wname, 0, LG_INCLUDE_INDIRECT,
                             (LPBYTE *) &local_groups, MAX_PREFERRED_LENGTH,
                             &nread, &ntotal) == NERR_Success)
    {
      DWORD i;
      int n;

      info->group_ids = dbus_new (dbus_gid_t, nread);
      if (!info->group_ids)
        {
          _DBUS_SET_OOM (error);
          goto out3;
        }

      for (i = n = 0; i < nread; i++)
        {
          PSID group_sid;
          if (_dbus_account_to_win_sid (local_groups[i].lgrui0_name,
                                        &group_sid, error))
            {
              info->group_ids[n++] = _dbus_win_sid_to_uid_t (group_sid);
              dbus_free (group_sid);
            }
        }
      info->n_group_ids = n;
    }

  if (NetUserGetGroups (NULL, wname, 0,
                        (LPBYTE *) &global_groups, MAX_PREFERRED_LENGTH,
                        &nread, &ntotal) == NERR_Success)
    {
      DWORD i;
      int n = info->n_group_ids;

      info->group_ids = dbus_realloc (info->group_ids, (n + nread) * sizeof (dbus_gid_t));
      if (!info->group_ids)
        {
          _DBUS_SET_OOM (error);
          goto out4;
        }

      for (i = 0; i < nread; i++)
        {
          PSID group_sid;
          if (_dbus_account_to_win_sid (global_groups[i].grui0_name,
                                        &group_sid, error))
            {
              info->group_ids[n++] = _dbus_win_sid_to_uid_t (group_sid);
              dbus_free (group_sid);
            }
        }
      info->n_group_ids = n;
    }

  if (info->n_group_ids > 0)
    {
      /* FIXME: find out actual primary group */
      info->primary_gid = info->group_ids[0];
    }
  else
    {
      info->group_ids = dbus_new (dbus_gid_t, 1);
      info->n_group_ids = 1;
      info->group_ids[0] = DBUS_GID_UNSET;
      info->primary_gid = DBUS_GID_UNSET;
    }

  retval = TRUE;

out4:
  if (global_groups != NULL)
    NetApiBufferFree (global_groups);
out3:
  if (local_groups != NULL)
    NetApiBufferFree (local_groups);
out1:
  dbus_free (domain);
out0:
  dbus_free (name);

  return retval;
#endif //DBUS_WINCE
}

dbus_bool_t
fill_win_user_info_homedir (wchar_t  	 *wname,
                            wchar_t  	 *wdomain,
                            DBusUserInfo *info,
                            DBusError    *error)
{
#ifdef DBUS_WINCE
	//TODO
	return TRUE;
#else
  dbus_bool_t retval = FALSE;
  USER_INFO_1 *user_info = NULL;
  wchar_t wcomputername[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD wcomputername_length = MAX_COMPUTERNAME_LENGTH + 1;
  dbus_bool_t local_computer;
  wchar_t *dc = NULL;
  NET_API_STATUS ret = 0;

  /* If the domain is this computer's name, assume it's a local user.
   * Otherwise look up a DC for the domain, and ask it.
   */

  GetComputerNameW (wcomputername, &wcomputername_length);
  local_computer = (wcsicmp (wcomputername, wdomain) == 0);

  if (!local_computer)
    {
      ret = NetGetAnyDCName (NULL, wdomain, (LPBYTE *) &dc);
      if (ret != NERR_Success)
        {
          info->homedir = _dbus_strdup ("\\");
          _dbus_warn("NetGetAnyDCName() failed with errorcode %d '%s'\n",ret,_dbus_lm_strerror(ret));
          return TRUE;
        }
    }

  /* No way to find out the profile of another user, let's try the
   * "home directory" from NetUserGetInfo's USER_INFO_1.
   */
  ret = NetUserGetInfo (dc, wname, 1, (LPBYTE *) &user_info);
  if (ret == NERR_Success )
    if(user_info->usri1_home_dir != NULL &&
        user_info->usri1_home_dir != (LPWSTR)0xfeeefeee &&  /* freed memory http://www.gamedev.net/community/forums/topic.asp?topic_id=158402 */
        user_info->usri1_home_dir[0] != '\0')
      {
        info->homedir = _dbus_win_utf16_to_utf8 (user_info->usri1_home_dir, error);
        if (!info->homedir)
          goto out1;
      }
    else
      {
        _dbus_verbose("NetUserGetInfo() returned no home dir entry\n");
        /* Not set, so use something random. */
        info->homedir = _dbus_strdup ("\\");
      }
  else
    {
      char *dc_string = _dbus_win_utf16_to_utf8(dc,error);
	  char *user_name = _dbus_win_utf16_to_utf8(wname,error);
      _dbus_warn("NetUserGetInfo() for user '%s' failed with errorcode %d '%s', %s\n",user_name, ret,_dbus_lm_strerror(ret),dc_string);
      dbus_free(user_name);
      dbus_free(dc_string);
      /* Not set, so use something random. */
      info->homedir = _dbus_strdup ("\\");
    }

  retval = TRUE;

out1:
  if (dc != NULL)
    NetApiBufferFree (dc);
  if (user_info != NULL)
    NetApiBufferFree (user_info);

  return retval;
#endif //DBUS_WINCE
}

dbus_bool_t
fill_win_user_info_from_name (wchar_t      *wname,
                              DBusUserInfo *info,
                              DBusError    *error)
{
#ifdef DBUS_WINCE
	return TRUE;
	//TODO
#else
  dbus_bool_t retval = FALSE;
  PSID sid;
  wchar_t *wdomain;
  DWORD sid_length, wdomain_length;
  SID_NAME_USE use;

  sid_length = 0;
  wdomain_length = 0;
  if (!LookupAccountNameW (NULL, wname, NULL, &sid_length,
                           NULL, &wdomain_length, &use) &&
      GetLastError () != ERROR_INSUFFICIENT_BUFFER)
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      return FALSE;
    }

  sid = dbus_malloc (sid_length);
  if (!sid)
    {
      _DBUS_SET_OOM (error);
      return FALSE;
    }

  wdomain = dbus_new (wchar_t, wdomain_length);
  if (!wdomain)
    {
      _DBUS_SET_OOM (error);
      goto out0;
    }

  if (!LookupAccountNameW (NULL, wname, sid, &sid_length,
                           wdomain, &wdomain_length, &use))
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      goto out1;
    }

  if (!IsValidSid (sid))
    {
      dbus_set_error_const (error, DBUS_ERROR_FAILED, "Invalid SID");
      goto out1;
    }

  info->uid = _dbus_win_sid_to_uid_t (sid);

  if (!fill_win_user_info_name_and_groups (wname, wdomain, info, error))
    goto out1;

  if (!fill_win_user_info_homedir (wname, wdomain, info, error))
    goto out1;

  retval = TRUE;

out1:
  dbus_free (wdomain);
out0:
  dbus_free (sid);

  return retval;
#endif //DBUS_WINCE
}

dbus_bool_t
_dbus_win_sid_to_name_and_domain (dbus_uid_t uid,
                                  wchar_t  **wname,
                                  wchar_t  **wdomain,
                                  DBusError *error)
{
#ifdef DBUS_WINCE
	return TRUE;
	//TODO
#else
  PSID sid;
  DWORD wname_length, wdomain_length;
  SID_NAME_USE use;

  if (!_dbus_uid_t_to_win_sid (uid, &sid))
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      return FALSE;
    }

  wname_length = 0;
  wdomain_length = 0;
  if (!LookupAccountSidW (NULL, sid, NULL, &wname_length,
                          NULL, &wdomain_length, &use) &&
      GetLastError () != ERROR_INSUFFICIENT_BUFFER)
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      goto out0;
    }

  *wname = dbus_new (wchar_t, wname_length);
  if (!*wname)
    {
      _DBUS_SET_OOM (error);
      goto out0;
    }

  *wdomain = dbus_new (wchar_t, wdomain_length);
  if (!*wdomain)
    {
      _DBUS_SET_OOM (error);
      goto out1;
    }

  if (!LookupAccountSidW (NULL, sid, *wname, &wname_length,
                          *wdomain, &wdomain_length, &use))
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      goto out2;
    }

  return TRUE;

out2:
  dbus_free (*wdomain);
  *wdomain = NULL;
out1:
  dbus_free (*wname);
  *wname = NULL;
out0:
  LocalFree (sid);

  return FALSE;
#endif //DBUS_WINCE
}

dbus_bool_t
fill_win_user_info_from_uid (dbus_uid_t    uid,
                             DBusUserInfo *info,
                             DBusError    *error)
{
#ifdef DBUS_WINCE
	return TRUE;
	//TODO
#else
  PSID sid;
  dbus_bool_t retval = FALSE;
  wchar_t *wname, *wdomain;

  info->uid = uid;

  if (!_dbus_win_sid_to_name_and_domain (uid, &wname, &wdomain, error))
    {
      _dbus_verbose("%s after _dbus_win_sid_to_name_and_domain\n",__FUNCTION__);
      return FALSE;
    }

  if (!fill_win_user_info_name_and_groups (wname, wdomain, info, error))
    {
      _dbus_verbose("%s after fill_win_user_info_name_and_groups\n",__FUNCTION__);
      goto out0;
    }


  if (!fill_win_user_info_homedir (wname, wdomain, info, error))
    {
      _dbus_verbose("%s after fill_win_user_info_homedir\n",__FUNCTION__);
      goto out0;
    }

  retval = TRUE;

out0:
  dbus_free (wdomain);
  dbus_free (wname);

  return retval;
#endif //DBUS_WINCE
}




void
_dbus_win_startup_winsock (void)
{
  /* Straight from MSDN, deuglified */

  static dbus_bool_t beenhere = FALSE;

  WORD wVersionRequested;
  WSADATA wsaData;
  int err;

  if (beenhere)
    return;

  wVersionRequested = MAKEWORD (2, 0);

  err = WSAStartup (wVersionRequested, &wsaData);
  if (err != 0)
    {
      _dbus_assert_not_reached ("Could not initialize WinSock");
      _dbus_abort ();
    }

  /* Confirm that the WinSock DLL supports 2.0.  Note that if the DLL
   * supports versions greater than 2.0 in addition to 2.0, it will
   * still return 2.0 in wVersion since that is the version we
   * requested.
   */
  if (LOBYTE (wsaData.wVersion) != 2 ||
      HIBYTE (wsaData.wVersion) != 0)
    {
      _dbus_assert_not_reached ("No usable WinSock found");
      _dbus_abort ();
    }

  beenhere = TRUE;
}









/************************************************************************
 
 UTF / string code
 
 ************************************************************************/

/**
 * Measure the message length without terminating nul 
 */
int _dbus_printf_string_upper_bound (const char *format,
                                     va_list args)
{
  /* MSVCRT's vsnprintf semantics are a bit different */
  /* The C library source in the Platform SDK indicates that this
   * would work, but alas, it doesn't. At least not on Windows
   * 2000. Presumably those sources correspond to the C library on
   * some newer or even future Windows version.
   *
    len = _vsnprintf (NULL, _DBUS_INT_MAX, format, args);
   */
  char p[1024];
  int len;
  len = vsnprintf (p, sizeof(p)-1, format, args);
  if (len == -1) // try again
    {
      char *p;
      p = malloc (strlen(format)*3);
      len = vsnprintf (p, sizeof(p)-1, format, args);
      free(p);
    }
  return len;
}


/**
 * Returns the UTF-16 form of a UTF-8 string. The result should be
 * freed with dbus_free() when no longer needed.
 *
 * @param str the UTF-8 string
 * @param error return location for error code
 */
wchar_t *
_dbus_win_utf8_to_utf16 (const char *str,
                         DBusError  *error)
{
  DBusString s;
  int n;
  wchar_t *retval;

  _dbus_string_init_const (&s, str);

  if (!_dbus_string_validate_utf8 (&s, 0, _dbus_string_get_length (&s)))
    {
      dbus_set_error_const (error, DBUS_ERROR_FAILED, "Invalid UTF-8");
      return NULL;
    }

  n = MultiByteToWideChar (CP_UTF8, 0, str, -1, NULL, 0);

  if (n == 0)
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      return NULL;
    }

  retval = dbus_new (wchar_t, n);

  if (!retval)
    {
      _DBUS_SET_OOM (error);
      return NULL;
    }

  if (MultiByteToWideChar (CP_UTF8, 0, str, -1, retval, n) != n)
    {
      dbus_free (retval);
      dbus_set_error_const (error, DBUS_ERROR_FAILED, "MultiByteToWideChar inconsistency");
      return NULL;
    }

  return retval;
}

/**
 * Returns the UTF-8 form of a UTF-16 string. The result should be
 * freed with dbus_free() when no longer needed.
 *
 * @param str the UTF-16 string
 * @param error return location for error code
 */
char *
_dbus_win_utf16_to_utf8 (const wchar_t *str,
                         DBusError     *error)
{
  int n;
  char *retval;

  n = WideCharToMultiByte (CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);

  if (n == 0)
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      return NULL;
    }

  retval = dbus_malloc (n);

  if (!retval)
    {
      _DBUS_SET_OOM (error);
      return NULL;
    }

  if (WideCharToMultiByte (CP_UTF8, 0, str, -1, retval, n, NULL, NULL) != n)
    {
      dbus_free (retval);
      dbus_set_error_const (error, DBUS_ERROR_FAILED, "WideCharToMultiByte inconsistency");
      return NULL;
    }

  return retval;
}






/************************************************************************
 
 uid ... <-> win sid functions
 
 ************************************************************************/

dbus_bool_t
_dbus_win_account_to_sid (const wchar_t *waccount,
                          void      	 **ppsid,
                          DBusError 	  *error)
{
  dbus_bool_t retval = FALSE;
  DWORD sid_length, wdomain_length;
  SID_NAME_USE use;
  wchar_t *wdomain;

  *ppsid = NULL;

  sid_length = 0;
  wdomain_length = 0;
  if (!LookupAccountNameW (NULL, waccount, NULL, &sid_length,
                           NULL, &wdomain_length, &use) &&
      GetLastError () != ERROR_INSUFFICIENT_BUFFER)
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      return FALSE;
    }

  *ppsid = dbus_malloc (sid_length);
  if (!*ppsid)
    {
      _DBUS_SET_OOM (error);
      return FALSE;
    }

  wdomain = dbus_new (wchar_t, wdomain_length);
  if (!wdomain)
    {
      _DBUS_SET_OOM (error);
      goto out1;
    }

  if (!LookupAccountNameW (NULL, waccount, (PSID) *ppsid, &sid_length,
                           wdomain, &wdomain_length, &use))
    {
      _dbus_win_set_error_from_win_error (error, GetLastError ());
      goto out2;
    }

  if (!IsValidSid ((PSID) *ppsid))
    {
      dbus_set_error_const (error, DBUS_ERROR_FAILED, "Invalid SID");
      goto out2;
    }

  retval = TRUE;

out2:
  dbus_free (wdomain);
out1:
  if (!retval)
    {
      dbus_free (*ppsid);
      *ppsid = NULL;
    }

  return retval;
}

static void
_sid_atom_cache_shutdown (void *unused)
{
  DBusHashIter iter;
  _DBUS_LOCK (sid_atom_cache);
  _dbus_hash_iter_init (sid_atom_cache, &iter);
  while (_dbus_hash_iter_next (&iter))
    {
      ATOM atom;
      atom = (ATOM) _dbus_hash_iter_get_value (&iter);
      GlobalDeleteAtom(atom);
      _dbus_hash_iter_remove_entry(&iter);
    }
  _DBUS_UNLOCK (sid_atom_cache);
  _dbus_hash_table_unref (sid_atom_cache);
  sid_atom_cache = NULL;
}

/**
 * Returns the 2-way associated dbus_uid_t form a SID.
 *
 * @param psid pointer to the SID
 */
dbus_uid_t
_dbus_win_sid_to_uid_t (PSID psid)
{
  dbus_uid_t uid;
  dbus_uid_t olduid;
  char *string;
  ATOM atom;

  if (!IsValidSid (psid))
    {
      _dbus_verbose("%s invalid sid\n",__FUNCTION__);
      return DBUS_UID_UNSET;
    }
  if (!ConvertSidToStringSidA (psid, &string))
    {
      _dbus_verbose("%s invalid sid\n",__FUNCTION__);
      return DBUS_UID_UNSET;
    }

  atom = GlobalAddAtom(string);

  if (atom == 0)
    {
      _dbus_verbose("%s GlobalAddAtom failed\n",__FUNCTION__);
      LocalFree (string);
      return DBUS_UID_UNSET;
    }

  _DBUS_LOCK (sid_atom_cache);

  if (sid_atom_cache == NULL)
    {
      sid_atom_cache = _dbus_hash_table_new (DBUS_HASH_ULONG, NULL, NULL);
      _dbus_register_shutdown_func (_sid_atom_cache_shutdown, NULL);
    }

  uid = atom;
  olduid = (dbus_uid_t) _dbus_hash_table_lookup_ulong (sid_atom_cache, uid);

  if (olduid)
    {
      _dbus_verbose("%s sid with id %i found in cache\n",__FUNCTION__, olduid);
      uid = olduid;
    }
  else
    {
      _dbus_hash_table_insert_ulong (sid_atom_cache, uid, (void*) uid);
      _dbus_verbose("%s sid %s added with uid %i to cache\n",__FUNCTION__, string, uid);
    }

  _DBUS_UNLOCK (sid_atom_cache);

  return uid;
}

dbus_bool_t  _dbus_uid_t_to_win_sid (dbus_uid_t uid, PSID *ppsid)
{
  void* atom;
  char string[255];

  atom = _dbus_hash_table_lookup_ulong (sid_atom_cache, uid);
  if (atom == NULL)
    {
      _dbus_verbose("%s uid %i not found in cache\n",__FUNCTION__,uid);
      return FALSE;
    }
  memset( string, '.', sizeof(string) );
  if (!GlobalGetAtomNameA( (ATOM) atom, string, 255 ))
    {
      _dbus_verbose("%s uid %i not found in cache\n",__FUNCTION__, uid);
      return FALSE;
    }
  if (!ConvertStringSidToSidA(string, ppsid))
    {
      _dbus_verbose("%s could not convert %s into sid \n",__FUNCTION__, string);
      return FALSE;
    }
  _dbus_verbose("%s converted %s into sid \n",__FUNCTION__, string);
  return TRUE;
}


/** @} end of sysdeps-win */


/** Gets our UID
 * @returns process UID
 */
dbus_uid_t
_dbus_getuid(void)
{
  dbus_uid_t retval = DBUS_UID_UNSET;
  HANDLE process_token = NULL;
  TOKEN_USER *token_user = NULL;
  DWORD n;

  if (!OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &process_token))
    _dbus_win_warn_win_error ("OpenProcessToken failed", GetLastError ());
  else if ((!GetTokenInformation (process_token, TokenUser, NULL, 0, &n)
            && GetLastError () != ERROR_INSUFFICIENT_BUFFER)
           || (token_user = alloca (n)) == NULL
           || !GetTokenInformation (process_token, TokenUser, token_user, n, &n))
    _dbus_win_warn_win_error ("GetTokenInformation failed", GetLastError ());
  else
    retval = _dbus_win_sid_to_uid_t (token_user->User.Sid);

  if (process_token != NULL)
    CloseHandle (process_token);

  _dbus_verbose("_dbus_getuid() returns %d\n",retval);
  return retval;
}

#ifdef DBUS_BUILD_TESTS
/** Gets our GID
 * @returns process GID
 */
dbus_gid_t
_dbus_getgid (void)
{
  dbus_gid_t retval = DBUS_GID_UNSET;
  HANDLE process_token = NULL;
  TOKEN_PRIMARY_GROUP *token_primary_group = NULL;
  DWORD n;

  if (!OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &process_token))
    _dbus_win_warn_win_error ("OpenProcessToken failed", GetLastError ());
  else if ((!GetTokenInformation (process_token, TokenPrimaryGroup,
                                  NULL, 0, &n) &&
            GetLastError () != ERROR_INSUFFICIENT_BUFFER) ||
           (token_primary_group = alloca (n)) == NULL ||
           !GetTokenInformation (process_token, TokenPrimaryGroup,
                                 token_primary_group, n, &n))
    _dbus_win_warn_win_error ("GetTokenInformation failed", GetLastError ());
  else
    retval = _dbus_win_sid_to_uid_t (token_primary_group->PrimaryGroup);

  if (process_token != NULL)
    CloseHandle (process_token);

  return retval;
}

#if 0
dbus_bool_t
_dbus_domain_test (const char *test_data_dir)
{
  if (!_dbus_test_oom_handling ("spawn_nonexistent",
                                check_spawn_nonexistent,
                                NULL))
    return FALSE;
}

#endif

#endif //DBUS_BUILD_TESTS

/************************************************************************
 
 pipes
 
 ************************************************************************/

/**
 * Creates a full-duplex pipe (as in socketpair()).
 * Sets both ends of the pipe nonblocking.
 *
 * @todo libdbus only uses this for the debug-pipe server, so in
 * principle it could be in dbus-sysdeps-util.c, except that
 * dbus-sysdeps-util.c isn't in libdbus when tests are enabled and the
 * debug-pipe server is used.
 * 
 * @param fd1 return location for one end
 * @param fd2 return location for the other end
 * @param blocking #TRUE if pipe should be blocking
 * @param error error return
 * @returns #FALSE on failure (if error is set)
 */
dbus_bool_t
_dbus_full_duplex_pipe (int        *fd1,
                        int        *fd2,
                        dbus_bool_t blocking,
                        DBusError  *error)
{
  SOCKET temp, socket1 = -1, socket2 = -1;
  struct sockaddr_in saddr;
  int len;
  u_long arg;
  fd_set read_set, write_set;
  struct timeval tv;
  DBusSocket sock;


  _dbus_win_startup_winsock ();

  temp = socket (AF_INET, SOCK_STREAM, 0);
  if (temp == INVALID_SOCKET)
    {
      DBUS_SOCKET_SET_ERRNO ();
      goto out0;
    }

  arg = 1;
  if (ioctlsocket (temp, FIONBIO, &arg) == SOCKET_ERROR)
    {
      DBUS_SOCKET_SET_ERRNO ();
      goto out0;
    }

  _DBUS_ZERO (saddr);
  saddr.sin_family = AF_INET;
  saddr.sin_port = 0;
  saddr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

  if (bind (temp, (struct sockaddr *)&saddr, sizeof (saddr)))
    {
      DBUS_SOCKET_SET_ERRNO ();
      goto out0;
    }

  if (listen (temp, 1) == SOCKET_ERROR)
    {
      DBUS_SOCKET_SET_ERRNO ();
      goto out0;
    }

  len = sizeof (saddr);
  if (getsockname (temp, (struct sockaddr *)&saddr, &len))
    {
      DBUS_SOCKET_SET_ERRNO ();
      goto out0;
    }

  socket1 = socket (AF_INET, SOCK_STREAM, 0);
  if (socket1 == INVALID_SOCKET)
    {
      DBUS_SOCKET_SET_ERRNO ();
      goto out0;
    }

  arg = 1;
  if (ioctlsocket (socket1, FIONBIO, &arg) == SOCKET_ERROR)
    {
      DBUS_SOCKET_SET_ERRNO ();
      goto out1;
    }

  if (connect (socket1, (struct sockaddr  *)&saddr, len) != SOCKET_ERROR ||
      WSAGetLastError () != WSAEWOULDBLOCK)
    {
      dbus_set_error_const (error, DBUS_ERROR_FAILED,
                            "_dbus_full_duplex_pipe socketpair() emulation failed");
      goto out1;
    }

  FD_ZERO (&read_set);
  FD_SET (temp, &read_set);

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  if (select (0, &read_set, NULL, NULL, NULL) == SOCKET_ERROR)
    {
      DBUS_SOCKET_SET_ERRNO ();
      goto out1;
    }

  _dbus_assert (FD_ISSET (temp, &read_set));

  socket2 = accept (temp, (struct sockaddr *) &saddr, &len);
  if (socket2 == INVALID_SOCKET)
    {
      DBUS_SOCKET_SET_ERRNO ();
      goto out1;
    }

  FD_ZERO (&write_set);
  FD_SET (socket1, &write_set);

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  if (select (0, NULL, &write_set, NULL, NULL) == SOCKET_ERROR)
    {
      DBUS_SOCKET_SET_ERRNO ();
      goto out2;
    }

  _dbus_assert (FD_ISSET (socket1, &write_set));

  if (blocking)
    {
      arg = 0;
      if (ioctlsocket (socket1, FIONBIO, &arg) == SOCKET_ERROR)
        {
          DBUS_SOCKET_SET_ERRNO ();
          goto out2;
        }

      arg = 0;
      if (ioctlsocket (socket2, FIONBIO, &arg) == SOCKET_ERROR)
        {
          DBUS_SOCKET_SET_ERRNO ();
          goto out2;
        }
    }
  else
    {
      arg = 1;
      if (ioctlsocket (socket2, FIONBIO, &arg) == SOCKET_ERROR)
        {
          DBUS_SOCKET_SET_ERRNO ();
          goto out2;
        }
    }

  sock.fd = socket1;
  *fd1 = _dbus_socket_to_handle (&sock);
  sock.fd = socket2;
  *fd2 = _dbus_socket_to_handle (&sock);

  _dbus_verbose ("full-duplex pipe %d:%d <-> %d:%d\n",
                 *fd1, socket1, *fd2, socket2);

  closesocket (temp);

  return TRUE;

out2:
  closesocket (socket2);
out1:
  closesocket (socket1);
out0:
  closesocket (temp);

  dbus_set_error (error, _dbus_error_from_errno (errno),
                  "Could not setup socket pair: %s",
                  _dbus_strerror (errno));

  return FALSE;
}

/**
 * Wrapper for poll().
 *
 * @param fds the file descriptors to poll
 * @param n_fds number of descriptors in the array
 * @param timeout_milliseconds timeout or -1 for infinite
 * @returns numbers of fds with revents, or <0 on error
 */
#define USE_CHRIS_IMPL 0
#if USE_CHRIS_IMPL
int
_dbus_poll (DBusPollFD *fds,
            int         n_fds,
            int         timeout_milliseconds)
{
#define DBUS_POLL_CHAR_BUFFER_SIZE 2000
  char msg[DBUS_POLL_CHAR_BUFFER_SIZE];
  char *msgp;

  int ret = 0;
  int i;
  struct timeval tv;
  int ready;

#define DBUS_STACK_WSAEVENTS 256
  WSAEVENT eventsOnStack[DBUS_STACK_WSAEVENTS];
  WSAEVENT *pEvents = NULL;
  if (n_fds > DBUS_STACK_WSAEVENTS)
    pEvents = calloc(sizeof(WSAEVENT), n_fds);
  else
    pEvents = eventsOnStack;

  _dbus_lock_sockets();

#ifdef DBUS_ENABLE_VERBOSE_MODE
  msgp = msg;
  msgp += sprintf (msgp, "WSAEventSelect: to=%d\n\t", timeout_milliseconds);
  for (i = 0; i < n_fds; i++)
    {
      static dbus_bool_t warned = FALSE;
      DBusSocket *s;
      DBusPollFD *fdp = &fds[i];

      _dbus_handle_to_socket_unlocked(fdp->fd, &s);  

      if (s->is_used == 0)
        {
          _dbus_warn ("no valid socket");
          warned = TRUE;
        }

      if (fdp->events & _DBUS_POLLIN)
        msgp += sprintf (msgp, "R:%d ", s->fd);

      if (fdp->events & _DBUS_POLLOUT)
        msgp += sprintf (msgp, "W:%d ", s->fd);

      msgp += sprintf (msgp, "E:%d\n\t", s->fd);

      // FIXME: more robust code for long  msg
      //        create on heap when msg[] becomes too small
      if (msgp >= msg + DBUS_POLL_CHAR_BUFFER_SIZE)
        {
          _dbus_assert_not_reached ("buffer overflow in _dbus_poll");
        }
    }

  msgp += sprintf (msgp, "\n");
  _dbus_verbose ("%s",msg);
#endif
  for (i = 0; i < n_fds; i++)
    {
      DBusSocket *s;
      DBusPollFD *fdp = &fds[i];
      WSAEVENT ev;
      long lNetworkEvents = FD_OOB;

      _dbus_handle_to_socket_unlocked(fdp->fd, &s); 

      if (s->is_used == 0)
        continue;

      ev = WSACreateEvent();

      if (fdp->events & _DBUS_POLLIN)
        lNetworkEvents |= FD_READ | FD_ACCEPT | FD_CLOSE;

      if (fdp->events & _DBUS_POLLOUT)
        lNetworkEvents |= FD_WRITE | FD_CONNECT;

      WSAEventSelect(s->fd, ev, lNetworkEvents);

      pEvents[i] = ev;
    }

  _dbus_unlock_sockets();

  ready = WSAWaitForMultipleEvents (n_fds, pEvents, FALSE, timeout_milliseconds, FALSE);

  if (DBUS_SOCKET_API_RETURNS_ERROR (ready))
    {
      DBUS_SOCKET_SET_ERRNO ();
      if (errno != EWOULDBLOCK)
        _dbus_verbose ("WSAWaitForMultipleEvents: failed: %s\n", _dbus_strerror (errno));
      ret = -1;
    }
  else if (ready == WSA_WAIT_TIMEOUT)
    {
      _dbus_verbose ("WSAWaitForMultipleEvents: WSA_WAIT_TIMEOUT\n");
      ret = 0;
    }
  else if (ready >= WSA_WAIT_EVENT_0 && ready < (int)(WSA_WAIT_EVENT_0 + n_fds))
    {
      msgp = msg;
      msgp += sprintf (msgp, "WSAWaitForMultipleEvents: =%d\n\t", ready);

      _dbus_lock_sockets();
      for (i = 0; i < n_fds; i++)
        {
          DBusSocket *s;
          DBusPollFD *fdp = &fds[i];
          WSANETWORKEVENTS ne;

          _dbus_handle_to_socket_unlocked(fdp->fd, &s); 

          fdp->revents = 0;

          WSAEnumNetworkEvents(s->fd, pEvents[i], &ne);

          if (ne.lNetworkEvents & (FD_READ | FD_ACCEPT | FD_CLOSE))
            fdp->revents |= _DBUS_POLLIN;

          if (ne.lNetworkEvents & (FD_WRITE | FD_CONNECT))
            fdp->revents |= _DBUS_POLLOUT;

          if (ne.lNetworkEvents & (FD_OOB))
            fdp->revents |= _DBUS_POLLERR;

          if (ne.lNetworkEvents & (FD_READ | FD_ACCEPT | FD_CLOSE))
              msgp += sprintf (msgp, "R:%d ", s->fd);

          if (ne.lNetworkEvents & (FD_WRITE | FD_CONNECT))
              msgp += sprintf (msgp, "W:%d ", s->fd);

          if (ne.lNetworkEvents & (FD_OOB))
              msgp += sprintf (msgp, "E:%d ", s->fd);

          msgp += sprintf (msgp, "lNetworkEvents:%d ", ne.lNetworkEvents);

          if(ne.lNetworkEvents)
            ret++;

          WSAEventSelect(s->fd, pEvents[i], 0);
        }
      _dbus_unlock_sockets();

      msgp += sprintf (msgp, "\n");
      _dbus_verbose ("%s",msg);
    }
  else
    {
      _dbus_verbose ("WSAWaitForMultipleEvents: failed for unknown reason!");
      ret = -1;
    }

  for(i = 0; i < n_fds; i++)
    {
      WSACloseEvent(pEvents[i]);
    }

  if (n_fds > DBUS_STACK_WSAEVENTS)
    free(pEvents);

  return ret;
}

#else   // USE_CHRIS_IMPL

int
_dbus_poll (DBusPollFD *fds,
            int         n_fds,
            int         timeout_milliseconds)
{
#define DBUS_POLL_CHAR_BUFFER_SIZE 2000
  char msg[DBUS_POLL_CHAR_BUFFER_SIZE];
  char *msgp;

  fd_set read_set, write_set, err_set;
  int max_fd = 0;
  int i;
  struct timeval tv;
  int ready;

  FD_ZERO (&read_set);
  FD_ZERO (&write_set);
  FD_ZERO (&err_set);

  _dbus_lock_sockets();

#ifdef DBUS_ENABLE_VERBOSE_MODE
  msgp = msg;
  msgp += sprintf (msgp, "select: to=%d\n\t", timeout_milliseconds);
  for (i = 0; i < n_fds; i++)
    {
      static dbus_bool_t warned = FALSE;
      DBusSocket *s;
      DBusPollFD *fdp = &fds[i];

      _dbus_handle_to_socket_unlocked(fdp->fd, &s);  

      if (s->is_used == 0)
        {
          _dbus_warn ("no valid socket");
          warned = TRUE;
        }

      if (fdp->events & _DBUS_POLLIN)
        msgp += sprintf (msgp, "R:%d ", s->fd);

      if (fdp->events & _DBUS_POLLOUT)
        msgp += sprintf (msgp, "W:%d ", s->fd);

      msgp += sprintf (msgp, "E:%d\n\t", s->fd);

      // FIXME: more robust code for long  msg
      //        create on heap when msg[] becomes too small
      if (msgp >= msg + DBUS_POLL_CHAR_BUFFER_SIZE)
        {
          _dbus_assert_not_reached ("buffer overflow in _dbus_poll");
        }
    }

  msgp += sprintf (msgp, "\n");
  _dbus_verbose ("%s",msg);
#endif
  for (i = 0; i < n_fds; i++)
    {
      DBusSocket *s;
      DBusPollFD *fdp = &fds[i];

      _dbus_handle_to_socket_unlocked(fdp->fd, &s); 

      if (s->is_used != 1)
        continue;

      if (fdp->events & _DBUS_POLLIN)
        FD_SET (s->fd, &read_set);

      if (fdp->events & _DBUS_POLLOUT)
        FD_SET (s->fd, &write_set);

      FD_SET (s->fd, &err_set);

      max_fd = MAX (max_fd, s->fd);
    }

  _dbus_unlock_sockets();

  tv.tv_sec = timeout_milliseconds / 1000;
  tv.tv_usec = (timeout_milliseconds % 1000) * 1000;

  ready = select (max_fd + 1, &read_set, &write_set, &err_set,
                  timeout_milliseconds < 0 ? NULL : &tv);

  if (DBUS_SOCKET_API_RETURNS_ERROR (ready))
    {
      DBUS_SOCKET_SET_ERRNO ();
      if (errno != EWOULDBLOCK)
        _dbus_verbose ("select: failed: %s\n", _dbus_strerror (errno));
    }
  else if (ready == 0)
    _dbus_verbose ("select: = 0\n");
  else
    if (ready > 0)
      {
#ifdef DBUS_ENABLE_VERBOSE_MODE
        msgp = msg;
        msgp += sprintf (msgp, "select: = %d:\n\t", ready);
        _dbus_lock_sockets();
        for (i = 0; i < n_fds; i++)
          {
            DBusSocket *s;
            DBusPollFD *fdp = &fds[i];

            _dbus_handle_to_socket_unlocked(fdp->fd, &s); 

            if (FD_ISSET (s->fd, &read_set))
              msgp += sprintf (msgp, "R:%d ", s->fd);

            if (FD_ISSET (s->fd, &write_set))
              msgp += sprintf (msgp, "W:%d ", s->fd);

            if (FD_ISSET (s->fd, &err_set))
              msgp += sprintf (msgp, "E:%d\n\t", s->fd);
          }
        msgp += sprintf (msgp, "\n");
        _dbus_verbose ("%s",msg);
#endif

        for (i = 0; i < n_fds; i++)
          {
            DBusSocket *s;
            DBusPollFD *fdp = &fds[i];

            _dbus_handle_to_socket_unlocked(fdp->fd, &s); 

            fdp->revents = 0;

            if (FD_ISSET (s->fd, &read_set))
              fdp->revents |= _DBUS_POLLIN;

            if (FD_ISSET (s->fd, &write_set))
              fdp->revents |= _DBUS_POLLOUT;

            if (FD_ISSET (s->fd, &err_set))
              fdp->revents |= _DBUS_POLLERR;
          }
        _dbus_unlock_sockets();
      }
  return ready;
}
#endif  // USE_CHRIS_IMPL


/************************************************************************
 
 error handling
 
 ************************************************************************/


/**
 * Assigns an error name and message corresponding to a Win32 error
 * code to a DBusError. Does nothing if error is #NULL.
 *
 * @param error the error.
 * @param code the Win32 error code
 */
void
_dbus_win_set_error_from_win_error (DBusError *error,
                                    int        code)
{
  char *msg;

  /* As we want the English message, use the A API */
  FormatMessageA (FORMAT_MESSAGE_ALLOCATE_BUFFER |
                  FORMAT_MESSAGE_IGNORE_INSERTS |
                  FORMAT_MESSAGE_FROM_SYSTEM,
                  NULL, code, MAKELANGID (LANG_ENGLISH, SUBLANG_ENGLISH_US),
                  (LPTSTR) &msg, 0, NULL);
  if (msg)
    {
      char *msg_copy;

      msg_copy = dbus_malloc (strlen (msg));
      strcpy (msg_copy, msg);
      LocalFree (msg);

      dbus_set_error (error, "win32.error", "%s", msg_copy);
    }
  else
    dbus_set_error_const (error, "win32.error", "Unknown error code or FormatMessage failed");
}

void
_dbus_win_warn_win_error (const char *message,
                          int         code)
{
  DBusError error;

  dbus_error_init (&error);
  _dbus_win_set_error_from_win_error (&error, code);
  _dbus_warn ("%s: %s\n", message, error.message);
  dbus_error_free (&error);
}

/**
 * A wrapper around strerror() because some platforms
 * may be lame and not have strerror().
 *
 * @param error_number errno.
 * @returns error description.
 */
const char*
_dbus_strerror (int error_number)
{
#ifdef DBUS_WINCE
  // TODO
  return "unknown";
#else
  const char *msg;

  switch (error_number)
    {
    case WSAEINTR:
      return "Interrupted function call";
    case WSAEACCES:
      return "Permission denied";
    case WSAEFAULT:
      return "Bad address";
    case WSAEINVAL:
      return "Invalid argument";
    case WSAEMFILE:
      return "Too many open files";
    case WSAEWOULDBLOCK:
      return "Resource temporarily unavailable";
    case WSAEINPROGRESS:
      return "Operation now in progress";
    case WSAEALREADY:
      return "Operation already in progress";
    case WSAENOTSOCK:
      return "Socket operation on nonsocket";
    case WSAEDESTADDRREQ:
      return "Destination address required";
    case WSAEMSGSIZE:
      return "Message too long";
    case WSAEPROTOTYPE:
      return "Protocol wrong type for socket";
    case WSAENOPROTOOPT:
      return "Bad protocol option";
    case WSAEPROTONOSUPPORT:
      return "Protocol not supported";
    case WSAESOCKTNOSUPPORT:
      return "Socket type not supported";
    case WSAEOPNOTSUPP:
      return "Operation not supported";
    case WSAEPFNOSUPPORT:
      return "Protocol family not supported";
    case WSAEAFNOSUPPORT:
      return "Address family not supported by protocol family";
    case WSAEADDRINUSE:
      return "Address already in use";
    case WSAEADDRNOTAVAIL:
      return "Cannot assign requested address";
    case WSAENETDOWN:
      return "Network is down";
    case WSAENETUNREACH:
      return "Network is unreachable";
    case WSAENETRESET:
      return "Network dropped connection on reset";
    case WSAECONNABORTED:
      return "Software caused connection abort";
    case WSAECONNRESET:
      return "Connection reset by peer";
    case WSAENOBUFS:
      return "No buffer space available";
    case WSAEISCONN:
      return "Socket is already connected";
    case WSAENOTCONN:
      return "Socket is not connected";
    case WSAESHUTDOWN:
      return "Cannot send after socket shutdown";
    case WSAETIMEDOUT:
      return "Connection timed out";
    case WSAECONNREFUSED:
      return "Connection refused";
    case WSAEHOSTDOWN:
      return "Host is down";
    case WSAEHOSTUNREACH:
      return "No route to host";
    case WSAEPROCLIM:
      return "Too many processes";
    case WSAEDISCON:
      return "Graceful shutdown in progress";
    case WSATYPE_NOT_FOUND:
      return "Class type not found";
    case WSAHOST_NOT_FOUND:
      return "Host not found";
    case WSATRY_AGAIN:
      return "Nonauthoritative host not found";
    case WSANO_RECOVERY:
      return "This is a nonrecoverable error";
    case WSANO_DATA:
      return "Valid name, no data record of requested type";
    case WSA_INVALID_HANDLE:
      return "Specified event object handle is invalid";
    case WSA_INVALID_PARAMETER:
      return "One or more parameters are invalid";
    case WSA_IO_INCOMPLETE:
      return "Overlapped I/O event object not in signaled state";
    case WSA_IO_PENDING:
      return "Overlapped operations will complete later";
    case WSA_NOT_ENOUGH_MEMORY:
      return "Insufficient memory available";
    case WSA_OPERATION_ABORTED:
      return "Overlapped operation aborted";
#ifdef WSAINVALIDPROCTABLE

    case WSAINVALIDPROCTABLE:
      return "Invalid procedure table from service provider";
#endif
#ifdef WSAINVALIDPROVIDER

    case WSAINVALIDPROVIDER:
      return "Invalid service provider version number";
#endif
#ifdef WSAPROVIDERFAILEDINIT

    case WSAPROVIDERFAILEDINIT:
      return "Unable to initialize a service provider";
#endif

    case WSASYSCALLFAILURE:
      return "System call failure";
    }
  msg = strerror (error_number);
  if (msg == NULL)
    msg = "unknown";

  return msg;
#endif //DBUS_WINCE
}



/* lan manager error codes */
const char*
_dbus_lm_strerror(int error_number)
{
#ifdef DBUS_WINCE
  // TODO
  return "unknown";
#else
  const char *msg;
  switch (error_number)
    {
    case NERR_NetNotStarted:
      return "The workstation driver is not installed.";
    case NERR_UnknownServer:
      return "The server could not be located.";
    case NERR_ShareMem:
      return "An internal error occurred. The network cannot access a shared memory segment.";
    case NERR_NoNetworkResource:
      return "A network resource shortage occurred.";
    case NERR_RemoteOnly:
      return "This operation is not supported on workstations.";
    case NERR_DevNotRedirected:
      return "The device is not connected.";
    case NERR_ServerNotStarted:
      return "The Server service is not started.";
    case NERR_ItemNotFound:
      return "The queue is empty.";
    case NERR_UnknownDevDir:
      return "The device or directory does not exist.";
    case NERR_RedirectedPath:
      return "The operation is invalid on a redirected resource.";
    case NERR_DuplicateShare:
      return "The name has already been shared.";
    case NERR_NoRoom:
      return "The server is currently out of the requested resource.";
    case NERR_TooManyItems:
      return "Requested addition of items exceeds the maximum allowed.";
    case NERR_InvalidMaxUsers:
      return "The Peer service supports only two simultaneous users.";
    case NERR_BufTooSmall:
      return "The API return buffer is too small.";
    case NERR_RemoteErr:
      return "A remote API error occurred.";
    case NERR_LanmanIniError:
      return "An error occurred when opening or reading the configuration file.";
    case NERR_NetworkError:
      return "A general network error occurred.";
    case NERR_WkstaInconsistentState:
      return "The Workstation service is in an inconsistent state. Restart the computer before restarting the Workstation service.";
    case NERR_WkstaNotStarted:
      return "The Workstation service has not been started.";
    case NERR_BrowserNotStarted:
      return "The requested information is not available.";
    case NERR_InternalError:
      return "An internal error occurred.";
    case NERR_BadTransactConfig:
      return "The server is not configured for transactions.";
    case NERR_InvalidAPI:
      return "The requested API is not supported on the remote server.";
    case NERR_BadEventName:
      return "The event name is invalid.";
    case NERR_DupNameReboot:
      return "The computer name already exists on the network. Change it and restart the computer.";
    case NERR_CfgCompNotFound:
      return "The specified component could not be found in the configuration information.";
    case NERR_CfgParamNotFound:
      return "The specified parameter could not be found in the configuration information.";
    case NERR_LineTooLong:
      return "A line in the configuration file is too long.";
    case NERR_QNotFound:
      return "The printer does not exist.";
    case NERR_JobNotFound:
      return "The print job does not exist.";
    case NERR_DestNotFound:
      return "The printer destination cannot be found.";
    case NERR_DestExists:
      return "The printer destination already exists.";
    case NERR_QExists:
      return "The printer queue already exists.";
    case NERR_QNoRoom:
      return "No more printers can be added.";
    case NERR_JobNoRoom:
      return "No more print jobs can be added.";
    case NERR_DestNoRoom:
      return "No more printer destinations can be added.";
    case NERR_DestIdle:
      return "This printer destination is idle and cannot accept control operations.";
    case NERR_DestInvalidOp:
      return "This printer destination request contains an invalid control function.";
    case NERR_ProcNoRespond:
      return "The print processor is not responding.";
    case NERR_SpoolerNotLoaded:
      return "The spooler is not running.";
    case NERR_DestInvalidState:
      return "This operation cannot be performed on the print destination in its current state.";
    case NERR_QInvalidState:
      return "This operation cannot be performed on the printer queue in its current state.";
    case NERR_JobInvalidState:
      return "This operation cannot be performed on the print job in its current state.";
    case NERR_SpoolNoMemory:
      return "A spooler memory allocation failure occurred.";
    case NERR_DriverNotFound:
      return "The device driver does not exist.";
    case NERR_DataTypeInvalid:
      return "The data type is not supported by the print processor.";
    case NERR_ProcNotFound:
      return "The print processor is not installed.";
    case NERR_ServiceTableLocked:
      return "The service database is locked.";
    case NERR_ServiceTableFull:
      return "The service table is full.";
    case NERR_ServiceInstalled:
      return "The requested service has already been started.";
    case NERR_ServiceEntryLocked:
      return "The service does not respond to control actions.";
    case NERR_ServiceNotInstalled:
      return "The service has not been started.";
    case NERR_BadServiceName:
      return "The service name is invalid.";
    case NERR_ServiceCtlTimeout:
      return "The service is not responding to the control function.";
    case NERR_ServiceCtlBusy:
      return "The service control is busy.";
    case NERR_BadServiceProgName:
      return "The configuration file contains an invalid service program name.";
    case NERR_ServiceNotCtrl:
      return "The service could not be controlled in its present state.";
    case NERR_ServiceKillProc:
      return "The service ended abnormally.";
    case NERR_ServiceCtlNotValid:
      return "The requested pause or stop is not valid for this service.";
    case NERR_NotInDispatchTbl:
      return "The service control dispatcher could not find the service name in the dispatch table.";
    case NERR_BadControlRecv:
      return "The service control dispatcher pipe read failed.";
    case NERR_ServiceNotStarting:
      return "A thread for the new service could not be created.";
    case NERR_AlreadyLoggedOn:
      return "This workstation is already logged on to the local-area network.";
    case NERR_NotLoggedOn:
      return "The workstation is not logged on to the local-area network.";
    case NERR_BadUsername:
      return "The user name or group name parameter is invalid.";
    case NERR_BadPassword:
      return "The password parameter is invalid.";
    case NERR_UnableToAddName_W:
      return "@W The logon processor did not add the message alias.";
    case NERR_UnableToAddName_F:
      return "The logon processor did not add the message alias.";
    case NERR_UnableToDelName_W:
      return "@W The logoff processor did not delete the message alias.";
    case NERR_UnableToDelName_F:
      return "The logoff processor did not delete the message alias.";
    case NERR_LogonsPaused:
      return "Network logons are paused.";
    case NERR_LogonServerConflict:
      return "A centralized logon-server conflict occurred.";
    case NERR_LogonNoUserPath:
      return "The server is configured without a valid user path.";
    case NERR_LogonScriptError:
      return "An error occurred while loading or running the logon script.";
    case NERR_StandaloneLogon:
      return "The logon server was not specified. Your computer will be logged on as STANDALONE.";
    case NERR_LogonServerNotFound:
      return "The logon server could not be found.";
    case NERR_LogonDomainExists:
      return "There is already a logon domain for this computer.";
    case NERR_NonValidatedLogon:
      return "The logon server could not validate the logon.";
    case NERR_ACFNotFound:
      return "The security database could not be found.";
    case NERR_GroupNotFound:
      return "The group name could not be found.";
    case NERR_UserNotFound:
      return "The user name could not be found.";
    case NERR_ResourceNotFound:
      return "The resource name could not be found.";
    case NERR_GroupExists:
      return "The group already exists.";
    case NERR_UserExists:
      return "The user account already exists.";
    case NERR_ResourceExists:
      return "The resource permission list already exists.";
    case NERR_NotPrimary:
      return "This operation is only allowed on the primary domain controller of the domain.";
    case NERR_ACFNotLoaded:
      return "The security database has not been started.";
    case NERR_ACFNoRoom:
      return "There are too many names in the user accounts database.";
    case NERR_ACFFileIOFail:
      return "A disk I/O failure occurred.";
    case NERR_ACFTooManyLists:
      return "The limit of 64 entries per resource was exceeded.";
    case NERR_UserLogon:
      return "Deleting a user with a session is not allowed.";
    case NERR_ACFNoParent:
      return "The parent directory could not be located.";
    case NERR_CanNotGrowSegment:
      return "Unable to add to the security database session cache segment.";
    case NERR_SpeGroupOp:
      return "This operation is not allowed on this special group.";
    case NERR_NotInCache:
      return "This user is not cached in user accounts database session cache.";
    case NERR_UserInGroup:
      return "The user already belongs to this group.";
    case NERR_UserNotInGroup:
      return "The user does not belong to this group.";
    case NERR_AccountUndefined:
      return "This user account is undefined.";
    case NERR_AccountExpired:
      return "This user account has expired.";
    case NERR_InvalidWorkstation:
      return "The user is not allowed to log on from this workstation.";
    case NERR_InvalidLogonHours:
      return "The user is not allowed to log on at this time.";
    case NERR_PasswordExpired:
      return "The password of this user has expired.";
    case NERR_PasswordCantChange:
      return "The password of this user cannot change.";
    case NERR_PasswordHistConflict:
      return "This password cannot be used now.";
    case NERR_PasswordTooShort:
      return "The password does not meet the password policy requirements. Check the minimum password length, password complexity and password history requirements.";
    case NERR_PasswordTooRecent:
      return "The password of this user is too recent to change.";
    case NERR_InvalidDatabase:
      return "The security database is corrupted.";
    case NERR_DatabaseUpToDate:
      return "No updates are necessary to this replicant network/local security database.";
    case NERR_SyncRequired:
      return "This replicant database is outdated; synchronization is required.";
    case NERR_UseNotFound:
      return "The network connection could not be found.";
    case NERR_BadAsgType:
      return "This asg_type is invalid.";
    case NERR_DeviceIsShared:
      return "This device is currently being shared.";
    case NERR_NoComputerName:
      return "The computer name could not be added as a message alias. The name may already exist on the network.";
    case NERR_MsgAlreadyStarted:
      return "The Messenger service is already started.";
    case NERR_MsgInitFailed:
      return "The Messenger service failed to start.";
    case NERR_NameNotFound:
      return "The message alias could not be found on the network.";
    case NERR_AlreadyForwarded:
      return "This message alias has already been forwarded.";
    case NERR_AddForwarded:
      return "This message alias has been added but is still forwarded.";
    case NERR_AlreadyExists:
      return "This message alias already exists locally.";
    case NERR_TooManyNames:
      return "The maximum number of added message aliases has been exceeded.";
    case NERR_DelComputerName:
      return "The computer name could not be deleted.";
    case NERR_LocalForward:
      return "Messages cannot be forwarded back to the same workstation.";
    case NERR_GrpMsgProcessor:
      return "An error occurred in the domain message processor.";
    case NERR_PausedRemote:
      return "The message was sent, but the recipient has paused the Messenger service.";
    case NERR_BadReceive:
      return "The message was sent but not received.";
    case NERR_NameInUse:
      return "The message alias is currently in use. Try again later.";
    case NERR_MsgNotStarted:
      return "The Messenger service has not been started.";
    case NERR_NotLocalName:
      return "The name is not on the local computer.";
    case NERR_NoForwardName:
      return "The forwarded message alias could not be found on the network.";
    case NERR_RemoteFull:
      return "The message alias table on the remote station is full.";
    case NERR_NameNotForwarded:
      return "Messages for this alias are not currently being forwarded.";
    case NERR_TruncatedBroadcast:
      return "The broadcast message was truncated.";
    case NERR_InvalidDevice:
      return "This is an invalid device name.";
    case NERR_WriteFault:
      return "A write fault occurred.";
    case NERR_DuplicateName:
      return "A duplicate message alias exists on the network.";
    case NERR_DeleteLater:
      return "@W This message alias will be deleted later.";
    case NERR_IncompleteDel:
      return "The message alias was not successfully deleted from all networks.";
    case NERR_MultipleNets:
      return "This operation is not supported on computers with multiple networks.";
    case NERR_NetNameNotFound:
      return "This shared resource does not exist.";
    case NERR_DeviceNotShared:
      return "This device is not shared.";
    case NERR_ClientNameNotFound:
      return "A session does not exist with that computer name.";
    case NERR_FileIdNotFound:
      return "There is not an open file with that identification number.";
    case NERR_ExecFailure:
      return "A failure occurred when executing a remote administration command.";
    case NERR_TmpFile:
      return "A failure occurred when opening a remote temporary file.";
    case NERR_TooMuchData:
      return "The data returned from a remote administration command has been truncated to 64K.";
    case NERR_DeviceShareConflict:
      return "This device cannot be shared as both a spooled and a non-spooled resource.";
    case NERR_BrowserTableIncomplete:
      return "The information in the list of servers may be incorrect.";
    case NERR_NotLocalDomain:
      return "The computer is not active in this domain.";
#ifdef NERR_IsDfsShare

    case NERR_IsDfsShare:
      return "The share must be removed from the Distributed File System before it can be deleted.";
#endif

    case NERR_DevInvalidOpCode:
      return "The operation is invalid for this device.";
    case NERR_DevNotFound:
      return "This device cannot be shared.";
    case NERR_DevNotOpen:
      return "This device was not open.";
    case NERR_BadQueueDevString:
      return "This device name list is invalid.";
    case NERR_BadQueuePriority:
      return "The queue priority is invalid.";
    case NERR_NoCommDevs:
      return "There are no shared communication devices.";
    case NERR_QueueNotFound:
      return "The queue you specified does not exist.";
    case NERR_BadDevString:
      return "This list of devices is invalid.";
    case NERR_BadDev:
      return "The requested device is invalid.";
    case NERR_InUseBySpooler:
      return "This device is already in use by the spooler.";
    case NERR_CommDevInUse:
      return "This device is already in use as a communication device.";
    case NERR_InvalidComputer:
      return "This computer name is invalid.";
    case NERR_MaxLenExceeded:
      return "The string and prefix specified are too long.";
    case NERR_BadComponent:
      return "This path component is invalid.";
    case NERR_CantType:
      return "Could not determine the type of input.";
    case NERR_TooManyEntries:
      return "The buffer for types is not big enough.";
    case NERR_ProfileFileTooBig:
      return "Profile files cannot exceed 64K.";
    case NERR_ProfileOffset:
      return "The start offset is out of range.";
    case NERR_ProfileCleanup:
      return "The system cannot delete current connections to network resources.";
    case NERR_ProfileUnknownCmd:
      return "The system was unable to parse the command line in this file.";
    case NERR_ProfileLoadErr:
      return "An error occurred while loading the profile file.";
    case NERR_ProfileSaveErr:
      return "@W Errors occurred while saving the profile file. The profile was partially saved.";
    case NERR_LogOverflow:
      return "Log file %1 is full.";
    case NERR_LogFileChanged:
      return "This log file has changed between reads.";
    case NERR_LogFileCorrupt:
      return "Log file %1 is corrupt.";
    case NERR_SourceIsDir:
      return "The source path cannot be a directory.";
    case NERR_BadSource:
      return "The source path is illegal.";
    case NERR_BadDest:
      return "The destination path is illegal.";
    case NERR_DifferentServers:
      return "The source and destination paths are on different servers.";
    case NERR_RunSrvPaused:
      return "The Run server you requested is paused.";
    case NERR_ErrCommRunSrv:
      return "An error occurred when communicating with a Run server.";
    case NERR_ErrorExecingGhost:
      return "An error occurred when starting a background process.";
    case NERR_ShareNotFound:
      return "The shared resource you are connected to could not be found.";
    case NERR_InvalidLana:
      return "The LAN adapter number is invalid.";
    case NERR_OpenFiles:
      return "There are open files on the connection.";
    case NERR_ActiveConns:
      return "Active connections still exist.";
    case NERR_BadPasswordCore:
      return "This share name or password is invalid.";
    case NERR_DevInUse:
      return "The device is being accessed by an active process.";
    case NERR_LocalDrive:
      return "The drive letter is in use locally.";
    case NERR_AlertExists:
      return "The specified client is already registered for the specified event.";
    case NERR_TooManyAlerts:
      return "The alert table is full.";
    case NERR_NoSuchAlert:
      return "An invalid or nonexistent alert name was raised.";
    case NERR_BadRecipient:
      return "The alert recipient is invalid.";
    case NERR_AcctLimitExceeded:
      return "A user's session with this server has been deleted.";
    case NERR_InvalidLogSeek:
      return "The log file does not contain the requested record number.";
    case NERR_BadUasConfig:
      return "The user accounts database is not configured correctly.";
    case NERR_InvalidUASOp:
      return "This operation is not permitted when the Netlogon service is running.";
    case NERR_LastAdmin:
      return "This operation is not allowed on the last administrative account.";
    case NERR_DCNotFound:
      return "Could not find domain controller for this domain.";
    case NERR_LogonTrackingError:
      return "Could not set logon information for this user.";
    case NERR_NetlogonNotStarted:
      return "The Netlogon service has not been started.";
    case NERR_CanNotGrowUASFile:
      return "Unable to add to the user accounts database.";
    case NERR_TimeDiffAtDC:
      return "This server's clock is not synchronized with the primary domain controller's clock.";
    case NERR_PasswordMismatch:
      return "A password mismatch has been detected.";
    case NERR_NoSuchServer:
      return "The server identification does not specify a valid server.";
    case NERR_NoSuchSession:
      return "The session identification does not specify a valid session.";
    case NERR_NoSuchConnection:
      return "The connection identification does not specify a valid connection.";
    case NERR_TooManyServers:
      return "There is no space for another entry in the table of available servers.";
    case NERR_TooManySessions:
      return "The server has reached the maximum number of sessions it supports.";
    case NERR_TooManyConnections:
      return "The server has reached the maximum number of connections it supports.";
    case NERR_TooManyFiles:
      return "The server cannot open more files because it has reached its maximum number.";
    case NERR_NoAlternateServers:
      return "There are no alternate servers registered on this server.";
    case NERR_TryDownLevel:
      return "Try down-level (remote admin protocol) version of API instead.";
    case NERR_UPSDriverNotStarted:
      return "The UPS driver could not be accessed by the UPS service.";
    case NERR_UPSInvalidConfig:
      return "The UPS service is not configured correctly.";
    case NERR_UPSInvalidCommPort:
      return "The UPS service could not access the specified Comm Port.";
    case NERR_UPSSignalAsserted:
      return "The UPS indicated a line fail or low battery situation. Service not started.";
    case NERR_UPSShutdownFailed:
      return "The UPS service failed to perform a system shut down.";
    case NERR_BadDosRetCode:
      return "The program below returned an MS-DOS error code:";
    case NERR_ProgNeedsExtraMem:
      return "The program below needs more memory:";
    case NERR_BadDosFunction:
      return "The program below called an unsupported MS-DOS function:";
    case NERR_RemoteBootFailed:
      return "The workstation failed to boot.";
    case NERR_BadFileCheckSum:
      return "The file below is corrupt.";
    case NERR_NoRplBootSystem:
      return "No loader is specified in the boot-block definition file.";
    case NERR_RplLoadrNetBiosErr:
      return "NetBIOS returned an error:      The NCB and SMB are dumped above.";
    case NERR_RplLoadrDiskErr:
      return "A disk I/O error occurred.";
    case NERR_ImageParamErr:
      return "Image parameter substitution failed.";
    case NERR_TooManyImageParams:
      return "Too many image parameters cross disk sector boundaries.";
    case NERR_NonDosFloppyUsed:
      return "The image was not generated from an MS-DOS diskette formatted with /S.";
    case NERR_RplBootRestart:
      return "Remote boot will be restarted later.";
    case NERR_RplSrvrCallFailed:
      return "The call to the Remoteboot server failed.";
    case NERR_CantConnectRplSrvr:
      return "Cannot connect to the Remoteboot server.";
    case NERR_CantOpenImageFile:
      return "Cannot open image file on the Remoteboot server.";
    case NERR_CallingRplSrvr:
      return "Connecting to the Remoteboot server...";
    case NERR_StartingRplBoot:
      return "Connecting to the Remoteboot server...";
    case NERR_RplBootServiceTerm:
      return "Remote boot service was stopped; check the error log for the cause of the problem.";
    case NERR_RplBootStartFailed:
      return "Remote boot startup failed; check the error log for the cause of the problem.";
    case NERR_RPL_CONNECTED:
      return "A second connection to a Remoteboot resource is not allowed.";
    case NERR_BrowserConfiguredToNotRun:
      return "The browser service was configured with MaintainServerList=No.";
    case NERR_RplNoAdaptersStarted:
      return "Service failed to start since none of the network adapters started with this service.";
    case NERR_RplBadRegistry:
      return "Service failed to start due to bad startup information in the registry.";
    case NERR_RplBadDatabase:
      return "Service failed to start because its database is absent or corrupt.";
    case NERR_RplRplfilesShare:
      return "Service failed to start because RPLFILES share is absent.";
    case NERR_RplNotRplServer:
      return "Service failed to start because RPLUSER group is absent.";
    case NERR_RplCannotEnum:
      return "Cannot enumerate service records.";
    case NERR_RplWkstaInfoCorrupted:
      return "Workstation record information has been corrupted.";
    case NERR_RplWkstaNotFound:
      return "Workstation record was not found.";
    case NERR_RplWkstaNameUnavailable:
      return "Workstation name is in use by some other workstation.";
    case NERR_RplProfileInfoCorrupted:
      return "Profile record information has been corrupted.";
    case NERR_RplProfileNotFound:
      return "Profile record was not found.";
    case NERR_RplProfileNameUnavailable:
      return "Profile name is in use by some other profile.";
    case NERR_RplProfileNotEmpty:
      return "There are workstations using this profile.";
    case NERR_RplConfigInfoCorrupted:
      return "Configuration record information has been corrupted.";
    case NERR_RplConfigNotFound:
      return "Configuration record was not found.";
    case NERR_RplAdapterInfoCorrupted:
      return "Adapter ID record information has been corrupted.";
    case NERR_RplInternal:
      return "An internal service error has occurred.";
    case NERR_RplVendorInfoCorrupted:
      return "Vendor ID record information has been corrupted.";
    case NERR_RplBootInfoCorrupted:
      return "Boot block record information has been corrupted.";
    case NERR_RplWkstaNeedsUserAcct:
      return "The user account for this workstation record is missing.";
    case NERR_RplNeedsRPLUSERAcct:
      return "The RPLUSER local group could not be found.";
    case NERR_RplBootNotFound:
      return "Boot block record was not found.";
    case NERR_RplIncompatibleProfile:
      return "Chosen profile is incompatible with this workstation.";
    case NERR_RplAdapterNameUnavailable:
      return "Chosen network adapter ID is in use by some other workstation.";
    case NERR_RplConfigNotEmpty:
      return "There are profiles using this configuration.";
    case NERR_RplBootInUse:
      return "There are workstations, profiles, or configurations using this boot block.";
    case NERR_RplBackupDatabase:
      return "Service failed to backup Remoteboot database.";
    case NERR_RplAdapterNotFound:
      return "Adapter record was not found.";
    case NERR_RplVendorNotFound:
      return "Vendor record was not found.";
    case NERR_RplVendorNameUnavailable:
      return "Vendor name is in use by some other vendor record.";
    case NERR_RplBootNameUnavailable:
      return "(boot name, vendor ID) is in use by some other boot block record.";
    case NERR_RplConfigNameUnavailable:
      return "Configuration name is in use by some other configuration.";
    case NERR_DfsInternalCorruption:
      return "The internal database maintained by the Dfs service is corrupt.";
    case NERR_DfsVolumeDataCorrupt:
      return "One of the records in the internal Dfs database is corrupt.";
    case NERR_DfsNoSuchVolume:
      return "There is no DFS name whose entry path matches the input Entry Path.";
    case NERR_DfsVolumeAlreadyExists:
      return "A root or link with the given name already exists.";
    case NERR_DfsAlreadyShared:
      return "The server share specified is already shared in the Dfs.";
    case NERR_DfsNoSuchShare:
      return "The indicated server share does not support the indicated DFS namespace.";
    case NERR_DfsNotALeafVolume:
      return "The operation is not valid on this portion of the namespace.";
    case NERR_DfsLeafVolume:
      return "The operation is not valid on this portion of the namespace.";
    case NERR_DfsVolumeHasMultipleServers:
      return "The operation is ambiguous because the link has multiple servers.";
    case NERR_DfsCantCreateJunctionPoint:
      return "Unable to create a link.";
    case NERR_DfsServerNotDfsAware:
      return "The server is not Dfs Aware.";
    case NERR_DfsBadRenamePath:
      return "The specified rename target path is invalid.";
    case NERR_DfsVolumeIsOffline:
      return "The specified DFS link is offline.";
    case NERR_DfsNoSuchServer:
      return "The specified server is not a server for this link.";
    case NERR_DfsCyclicalName:
      return "A cycle in the Dfs name was detected.";
    case NERR_DfsNotSupportedInServerDfs:
      return "The operation is not supported on a server-based Dfs.";
    case NERR_DfsDuplicateService:
      return "This link is already supported by the specified server-share.";
    case NERR_DfsCantRemoveLastServerShare:
      return "Can't remove the last server-share supporting this root or link.";
    case NERR_DfsVolumeIsInterDfs:
      return "The operation is not supported for an Inter-DFS link.";
    case NERR_DfsInconsistent:
      return "The internal state of the Dfs Service has become inconsistent.";
    case NERR_DfsServerUpgraded:
      return "The Dfs Service has been installed on the specified server.";
    case NERR_DfsDataIsIdentical:
      return "The Dfs data being reconciled is identical.";
    case NERR_DfsCantRemoveDfsRoot:
      return "The DFS root cannot be deleted. Uninstall DFS if required.";
    case NERR_DfsChildOrParentInDfs:
      return "A child or parent directory of the share is already in a Dfs.";
    case NERR_DfsInternalError:
      return "Dfs internal error.";
      /* the following are not defined in mingw */
#if 0

    case NERR_SetupAlreadyJoined:
      return "This machine is already joined to a domain.";
    case NERR_SetupNotJoined:
      return "This machine is not currently joined to a domain.";
    case NERR_SetupDomainController:
      return "This machine is a domain controller and cannot be unjoined from a domain.";
    case NERR_DefaultJoinRequired:
      return "The destination domain controller does not support creating machine accounts in OUs.";
    case NERR_InvalidWorkgroupName:
      return "The specified workgroup name is invalid.";
    case NERR_NameUsesIncompatibleCodePage:
      return "The specified computer name is incompatible with the default language used on the domain controller.";
    case NERR_ComputerAccountNotFound:
      return "The specified computer account could not be found.";
    case NERR_PersonalSku:
      return "This version of Windows cannot be joined to a domain.";
    case NERR_PasswordMustChange:
      return "The password must change at the next logon.";
    case NERR_AccountLockedOut:
      return "The account is locked out.";
    case NERR_PasswordTooLong:
      return "The password is too long.";
    case NERR_PasswordNotComplexEnough:
      return "The password does not meet the complexity policy.";
    case NERR_PasswordFilterError:
      return "The password does not meet the requirements of the password filter DLLs.";
#endif

    }
  msg = strerror (error_number);
  if (msg == NULL)
    msg = "unknown";

  return msg;
#endif //DBUS_WINCE
}









/******************************************************************************
 
Original CVS version of dbus-sysdeps.c
 
******************************************************************************/
/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-sysdeps.c Wrappers around system/libc features (internal to D-Bus implementation)
 * 
 * Copyright (C) 2002, 2003  Red Hat, Inc.
 * Copyright (C) 2003 CodeFactory AB
 * Copyright (C) 2005 Novell, Inc.
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


/**
 * @addtogroup DBusInternalsUtils
 * @{
 */

int _dbus_mkdir (const char *path,
                 mode_t mode)
{
  return _mkdir(path);
}

/**
 * Exit the process, returning the given value.
 *
 * @param code the exit code
 */
void
_dbus_exit (int code)
{
  _exit (code);
}

/**
 * Creates a socket and connects to a socket at the given host 
 * and port. The connection fd is returned, and is set up as
 * nonblocking.
 *
 * @param host the host name to connect to, NULL for loopback
 * @param port the prot to connect to
 * @param error return location for error code
 * @returns connection file descriptor or -1 on error
 */
int
_dbus_connect_tcp_socket (const char     *host,
                          dbus_uint32_t   port,
                          DBusError      *error)
{
  DBusSocket s;
  int handle;
  struct sockaddr_in addr;
  struct hostent *he;
  struct in_addr *haddr;
  struct in_addr ina;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  _dbus_win_startup_winsock ();

  s.fd = socket (AF_INET, SOCK_STREAM, 0);

  if (DBUS_SOCKET_IS_INVALID (s.fd))
    {
      DBUS_SOCKET_SET_ERRNO ();
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Failed to create socket: %s",
                      _dbus_strerror (errno));

      return -1;
    }

  if (host == NULL)
    {
      host = "localhost";
      ina.s_addr = htonl (INADDR_LOOPBACK);
      haddr = &ina;
    }

  he = gethostbyname (host);
  if (he == NULL)
    {
      DBUS_SOCKET_SET_ERRNO ();
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Failed to lookup hostname: %s",
                      host);
      DBUS_CLOSE_SOCKET (s.fd);
      return -1;
    }

  haddr = ((struct in_addr *) (he->h_addr_list)[0]);

  _DBUS_ZERO (addr);
  memcpy (&addr.sin_addr, haddr, sizeof(struct in_addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons (port);

  if (DBUS_SOCKET_API_RETURNS_ERROR
      (connect (s.fd, (struct sockaddr*) &addr, sizeof (addr)) < 0))
    {
      DBUS_SOCKET_SET_ERRNO ();
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Failed to connect to socket %s:%d %s",
                      host, port, _dbus_strerror (errno));

      DBUS_CLOSE_SOCKET (s.fd);
      s.fd = -1;

      return -1;
    }

  handle = _dbus_socket_to_handle (&s);

  if (!_dbus_set_fd_nonblocking (handle, error))
    {
      _dbus_close_socket (handle, NULL);
      handle = -1;

      return -1;
    }

  return handle;
}

void
_dbus_daemon_init(const char *host, dbus_uint32_t port);
/**
 * Creates a socket and binds it to the given port,
 * then listens on the socket. The socket is
 * set to be nonblocking. 
 * In case of port=0 a random free port is used and 
 * returned in the port parameter. 
 *
 * @param host the interface to listen on, NULL for loopback, empty for any
 * @param port the port to listen on, if zero a free port will be used 
 * @param error return location for errors
 * @returns the listening file descriptor or -1 on error
 */

int
_dbus_listen_tcp_socket (const char     *host,
                         dbus_uint32_t  *port,
                         DBusError      *error)
{
  DBusSocket slisten;
  int handle;
  struct sockaddr_in addr;
  struct hostent *he;
  struct in_addr *haddr;
  socklen_t len = (socklen_t) sizeof (struct sockaddr);
  struct in_addr ina;


  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  _dbus_win_startup_winsock ();

  slisten.fd = socket (AF_INET, SOCK_STREAM, 0);

  if (DBUS_SOCKET_IS_INVALID (slisten.fd))
    {
      DBUS_SOCKET_SET_ERRNO ();
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to create socket \"%s:%d\": %s",
                      host, port, _dbus_strerror (errno));
      return -1;
    }
  if (host == NULL)
    {
      host = "localhost";
      ina.s_addr = htonl (INADDR_LOOPBACK);
      haddr = &ina;
    }
  else if (!host[0])
    {
      ina.s_addr = htonl (INADDR_ANY);
      haddr = &ina;
    }
  else
    {
      he = gethostbyname (host);
      if (he == NULL)
        {
          DBUS_SOCKET_SET_ERRNO ();
          dbus_set_error (error,
                          _dbus_error_from_errno (errno),
                          "Failed to lookup hostname: %s",
                          host);
          DBUS_CLOSE_SOCKET (slisten.fd);
          return -1;
        }

      haddr = ((struct in_addr *) (he->h_addr_list)[0]);
    }

  _DBUS_ZERO (addr);
  memcpy (&addr.sin_addr, haddr, sizeof (struct in_addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons (*port);

  if (bind (slisten.fd, (struct sockaddr*) &addr, sizeof (struct sockaddr)))
    {
      DBUS_SOCKET_SET_ERRNO ();
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to bind socket \"%s:%d\": %s",
                      host, *port, _dbus_strerror (errno));
      DBUS_CLOSE_SOCKET (slisten.fd);
      return -1;
    }

  if (DBUS_SOCKET_API_RETURNS_ERROR (listen (slisten.fd, 30 /* backlog */)))
    {
      DBUS_SOCKET_SET_ERRNO ();
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to listen on socket \"%s:%d\": %s",
                      host, *port, _dbus_strerror (errno));
      DBUS_CLOSE_SOCKET (slisten.fd);
      return -1;
    }

  getsockname(slisten.fd, (struct sockaddr*) &addr, &len);
  *port = (dbus_uint32_t) ntohs(addr.sin_port);
  
  _dbus_daemon_init(host, ntohs(addr.sin_port));

  handle = _dbus_socket_to_handle (&slisten);

  if (!_dbus_set_fd_nonblocking (handle, error))
    {
      _dbus_close_socket (handle, NULL);
      return -1;
    }

  return handle;
}

/**
 * Accepts a connection on a listening socket.
 * Handles EINTR for you.
 *
 * @param listen_fd the listen file descriptor
 * @returns the connection fd of the client, or -1 on error
 */
int
_dbus_accept  (int listen_handle)
{
  DBusSocket *slisten;
  DBusSocket sclient;
  struct sockaddr addr;
  socklen_t addrlen;

  _dbus_handle_to_socket(listen_handle, &slisten);

  addrlen = sizeof (addr);

  //FIXME:  why do we not try it again on Windows?
#if !defined(DBUS_WIN) && !defined(DBUS_WINCE)
retry:
#endif

  sclient.fd = accept (slisten->fd, &addr, &addrlen);

  if (DBUS_SOCKET_IS_INVALID (sclient.fd))
    {
      DBUS_SOCKET_SET_ERRNO ();
#if !defined(DBUS_WIN) && !defined(DBUS_WINCE)
      if (errno == EINTR)
        goto retry;
#else
      return -1;
#endif
    }

  return _dbus_socket_to_handle (&sclient);
}



dbus_bool_t
write_credentials_byte (int            handle,
                        DBusError      *error)
{
/* FIXME: for the session bus credentials shouldn't matter (?), but
 * for the system bus they are presumably essential. A rough outline
 * of a way to implement the credential transfer would be this:
 *
 * client waits to *read* a byte.
 *
 * server creates a named pipe with a random name, sends a byte
 * contining its length, and its name.
 *
 * client reads the name, connects to it (using Win32 API).
 *
 * server waits for connection to the named pipe, then calls
 * ImpersonateNamedPipeClient(), notes its now-current credentials,
 * calls RevertToSelf(), closes its handles to the named pipe, and
 * is done. (Maybe there is some other way to get the SID of a named
 * pipe client without having to use impersonation?)
 *
 * client closes its handles and is done.
 * 
 * Ralf: Why not sending credentials over the given this connection ?
 * Using named pipes makes it impossible to be connected from a unix client.  
 *
 */
  int bytes_written;
  DBusString buf; 

  _dbus_string_init_const_len (&buf, "\0", 1);
again:
  bytes_written = _dbus_write_socket (handle, &buf, 0, 1 );

  if (bytes_written < 0 && errno == EINTR)
    goto again;

  if (bytes_written < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to write credentials byte: %s",
                     _dbus_strerror (errno));
      return FALSE;
    }
  else if (bytes_written == 0)
    {
      dbus_set_error (error, DBUS_ERROR_IO_ERROR,
                      "wrote zero bytes writing credentials byte");
      return FALSE;
    }
  else
    {
      _dbus_assert (bytes_written == 1);
      _dbus_verbose ("wrote 1 zero byte, credential sending isn't implemented yet\n");
      return TRUE;
    }
  return TRUE;
}

/**
 * Reads a single byte which must be nul (an error occurs otherwise),
 * and reads unix credentials if available. Fills in pid/uid/gid with
 * -1 if no credentials are available. Return value indicates whether
 * a byte was read, not whether we got valid credentials. On some
 * systems, such as Linux, reading/writing the byte isn't actually
 * required, but we do it anyway just to avoid multiple codepaths.
 * 
 * Fails if no byte is available, so you must select() first.
 *
 * The point of the byte is that on some systems we have to
 * use sendmsg()/recvmsg() to transmit credentials.
 *
 * @param client_fd the client file descriptor
 * @param credentials struct to fill with credentials of client
 * @param error location to store error code
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_read_credentials_unix_socket  (int              handle,
                                     DBusCredentials *credentials,
                                     DBusError       *error)
{
  int bytes_read;
  DBusString buf;
  _dbus_string_init(&buf);

  bytes_read = _dbus_read_socket(handle, &buf, 1 );
  if (bytes_read > 0) 
    {
		_dbus_verbose("got one zero byte from server");
    }

  _dbus_string_free(&buf);
  _dbus_credentials_from_current_process (credentials);
  _dbus_verbose("FIXME: get faked credentials from current process");

  return TRUE;
}

/**
* Checks to make sure the given directory is 
* private to the user 
*
* @param dir the name of the directory
* @param error error return
* @returns #FALSE on failure
**/
dbus_bool_t
_dbus_check_dir_is_private_to_user (DBusString *dir, DBusError *error)
{
  const char *directory;
  struct stat sb;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  return TRUE;
}


/**
 * Gets user info for the given user ID.
 *
 * @param info user info object to initialize
 * @param uid the user ID
 * @param error error return
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_user_info_fill_uid (DBusUserInfo *info,
                          dbus_uid_t    uid,
                          DBusError    *error)
{
  return fill_user_info (info, uid,
                         NULL, error);
}

/**
 * Gets user info for the given username.
 *
 * @param info user info object to initialize
 * @param username the username
 * @param error error return
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_user_info_fill (DBusUserInfo     *info,
                      const DBusString *username,
                      DBusError        *error)
{
  return fill_user_info (info, DBUS_UID_UNSET,
                         username, error);
}


dbus_bool_t
fill_user_info (DBusUserInfo       *info,
                dbus_uid_t          uid,
                const DBusString   *username,
                DBusError          *error)
{
  const char *username_c;

  /* exactly one of username/uid provided */
  _dbus_assert (username != NULL || uid != DBUS_UID_UNSET);
  _dbus_assert (username == NULL || uid == DBUS_UID_UNSET);

  info->uid = DBUS_UID_UNSET;
  info->primary_gid = DBUS_GID_UNSET;
  info->group_ids = NULL;
  info->n_group_ids = 0;
  info->username = NULL;
  info->homedir = NULL;

  if (username != NULL)
    username_c = _dbus_string_get_const_data (username);
  else
    username_c = NULL;

  if (uid != DBUS_UID_UNSET)
    {
      if (!fill_win_user_info_from_uid (uid, info, error))
        {
          _dbus_verbose("%s after fill_win_user_info_from_uid\n",__FUNCTION__);
          return FALSE;
        }
    }
  else
    {
      wchar_t *wname = _dbus_win_utf8_to_utf16 (username_c, error);

      if (!wname)
        return FALSE;

      if (!fill_win_user_info_from_name (wname, info, error))
        {
          dbus_free (wname);
          return FALSE;
        }
      dbus_free (wname);
    }

  return TRUE;
}


/**
 * Appends the given filename to the given directory.
 *
 * @todo it might be cute to collapse multiple '/' such as "foo//"
 * concat "//bar"
 *
 * @param dir the directory name
 * @param next_component the filename
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_concat_dir_and_file (DBusString       *dir,
                           const DBusString *next_component)
{
  dbus_bool_t dir_ends_in_slash;
  dbus_bool_t file_starts_with_slash;

  if (_dbus_string_get_length (dir) == 0 ||
      _dbus_string_get_length (next_component) == 0)
    return TRUE;

  dir_ends_in_slash =
    ('/' == _dbus_string_get_byte (dir, _dbus_string_get_length (dir) - 1) ||
     '\\' == _dbus_string_get_byte (dir, _dbus_string_get_length (dir) - 1));

  file_starts_with_slash =
    ('/' == _dbus_string_get_byte (next_component, 0) ||
     '\\' == _dbus_string_get_byte (next_component, 0));

  if (dir_ends_in_slash && file_starts_with_slash)
    {
      _dbus_string_shorten (dir, 1);
    }
  else if (!(dir_ends_in_slash || file_starts_with_slash))
    {
      if (!_dbus_string_append_byte (dir, '\\'))
        return FALSE;
    }

  return _dbus_string_copy (next_component, 0, dir,
                            _dbus_string_get_length (dir));
}




/**
 * Gets our process ID
 * @returns process ID
 */
unsigned long
_dbus_getpid (void)
{
  return GetCurrentProcessId ();
}

/** nanoseconds in a second */
#define NANOSECONDS_PER_SECOND       1000000000
/** microseconds in a second */
#define MICROSECONDS_PER_SECOND      1000000
/** milliseconds in a second */
#define MILLISECONDS_PER_SECOND      1000
/** nanoseconds in a millisecond */
#define NANOSECONDS_PER_MILLISECOND  1000000
/** microseconds in a millisecond */
#define MICROSECONDS_PER_MILLISECOND 1000

/**
 * Sleeps the given number of milliseconds.
 * @param milliseconds number of milliseconds
 */
void
_dbus_sleep_milliseconds (int milliseconds)
{
  Sleep (milliseconds);
}


/**
 * Get current time, as in gettimeofday().
 *
 * @param tv_sec return location for number of seconds
 * @param tv_usec return location for number of microseconds
 */
void
_dbus_get_current_time (long *tv_sec,
                        long *tv_usec)
{
  FILETIME ft;
  dbus_uint64_t *time64 = (dbus_uint64_t *) &ft;

  GetSystemTimeAsFileTime (&ft);

  /* Convert from 100s of nanoseconds since 1601-01-01
  * to Unix epoch. Yes, this is Y2038 unsafe.
  */
  *time64 -= DBUS_INT64_CONSTANT (116444736000000000);
  *time64 /= 10;

  if (tv_sec)
    *tv_sec = *time64 / 1000000;

  if (tv_usec)
    *tv_usec = *time64 % 1000000;
}


/**
 * signal (SIGPIPE, SIG_IGN);
 */
void
_dbus_disable_sigpipe (void)
{
    _dbus_verbose("FIXME: implement _dbus_disable_sigpipe (void)\n");
}

/**
 * Gets the credentials of the current process.
 *
 * @param credentials credentials to fill in.
 */
void
_dbus_credentials_from_current_process (DBusCredentials *credentials)
{
  credentials->pid = _dbus_getpid ();
  credentials->uid = _dbus_getuid ();
  credentials->gid = _dbus_getgid ();
}

/**
 * Appends the contents of the given file to the string,
 * returning error code. At the moment, won't open a file
 * more than a megabyte in size.
 *
 * @param str the string to append to
 * @param filename filename to load
 * @param error place to set an error
 * @returns #FALSE if error was set
 */
dbus_bool_t
_dbus_file_get_contents (DBusString       *str,
                         const DBusString *filename,
                         DBusError        *error)
{
  DBusFile file;
  struct stat sb;
  int orig_len;
  int total;
  const char *filename_c;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  filename_c = _dbus_string_get_const_data (filename);

  /* O_BINARY useful on Cygwin and Win32 */
  if (!_dbus_file_open (&file, filename_c, O_RDONLY | O_BINARY, -1))
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to open \"%s\": %s",
                      filename_c,
                      _dbus_strerror (errno));
      return FALSE;
    }

  if (!_dbus_fstat (&file, &sb))
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to stat \"%s\": %s",
                      filename_c,
                      _dbus_strerror (errno));

      _dbus_verbose ("fstat() failed: %s",
                     _dbus_strerror (errno));

      _dbus_file_close (&file, NULL);

      return FALSE;
    }

  if (sb.st_size > _DBUS_ONE_MEGABYTE)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "File size %lu of \"%s\" is too large.",
                      (unsigned long) sb.st_size, filename_c);
      _dbus_file_close (&file, NULL);
      return FALSE;
    }

  total = 0;
  orig_len = _dbus_string_get_length (str);
  if (sb.st_size > 0 && S_ISREG (sb.st_mode))
    {
      int bytes_read;

      while (total < (int) sb.st_size)
        {
          bytes_read = _dbus_file_read (&file, str,
                                        sb.st_size - total);
          if (bytes_read <= 0)
            {
              dbus_set_error (error, _dbus_error_from_errno (errno),
                              "Error reading \"%s\": %s",
                              filename_c,
                              _dbus_strerror (errno));

              _dbus_verbose ("read() failed: %s",
                             _dbus_strerror (errno));

              _dbus_file_close (&file, NULL);
              _dbus_string_set_length (str, orig_len);
              return FALSE;
            }
          else
            total += bytes_read;
        }

      _dbus_file_close (&file, NULL);
      return TRUE;
    }
  else if (sb.st_size != 0)
    {
      _dbus_verbose ("Can only open regular files at the moment.\n");
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "\"%s\" is not a regular file",
                      filename_c);
      _dbus_file_close (&file, NULL);
      return FALSE;
    }
  else
    {
      _dbus_file_close (&file, NULL);
      return TRUE;
    }
}

/**
 * Writes a string out to a file. If the file exists,
 * it will be atomically overwritten by the new data.
 *
 * @param str the string to write out
 * @param filename the file to save string to
 * @param error error to be filled in on failure
 * @returns #FALSE on failure
 */
dbus_bool_t
_dbus_string_save_to_file (const DBusString *str,
                           const DBusString *filename,
                           DBusError        *error)
{
  DBusFile file;
  int bytes_to_write;
  const char *filename_c;
  DBusString tmp_filename;
  const char *tmp_filename_c;
  int total;
  dbus_bool_t need_unlink;
  dbus_bool_t retval;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  retval = FALSE;
  need_unlink = FALSE;

  if (!_dbus_string_init (&tmp_filename))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return FALSE;
    }

  if (!_dbus_string_copy (filename, 0, &tmp_filename, 0))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      _dbus_string_free (&tmp_filename);
      return FALSE;
    }

  if (!_dbus_string_append (&tmp_filename, "."))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      _dbus_string_free (&tmp_filename);
      return FALSE;
    }

#define N_TMP_FILENAME_RANDOM_BYTES 8
  if (!_dbus_generate_random_ascii (&tmp_filename, N_TMP_FILENAME_RANDOM_BYTES))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      _dbus_string_free (&tmp_filename);
      return FALSE;
    }

  filename_c = _dbus_string_get_const_data (filename);
  tmp_filename_c = _dbus_string_get_const_data (&tmp_filename);

  if (!_dbus_file_open (&file, tmp_filename_c, O_WRONLY | O_BINARY | O_EXCL | O_CREAT,
                        0600))
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Could not create %s: %s", tmp_filename_c,
                      _dbus_strerror (errno));
      goto out;
    }

  need_unlink = TRUE;

  total = 0;
  bytes_to_write = _dbus_string_get_length (str);

  while (total < bytes_to_write)
    {
      int bytes_written;

      bytes_written = _dbus_file_write (&file, str, total,
                                        bytes_to_write - total);

      if (bytes_written <= 0)
        {
          dbus_set_error (error, _dbus_error_from_errno (errno),
                          "Could not write to %s: %s", tmp_filename_c,
                          _dbus_strerror (errno));

          goto out;
        }

      total += bytes_written;
    }

  if (!_dbus_file_close (&file, NULL))
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Could not close file %s: %s",
                      tmp_filename_c, _dbus_strerror (errno));

      goto out;
    }


  if ((unlink (filename_c) == -1 && errno != ENOENT) ||
       rename (tmp_filename_c, filename_c) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Could not rename %s to %s: %s",
                      tmp_filename_c, filename_c,
                      _dbus_strerror (errno));

      goto out;
    }

  need_unlink = FALSE;

  retval = TRUE;

out:
  /* close first, then unlink, to prevent ".nfs34234235" garbage
   * files
   */

  if (_dbus_is_valid_file(&file))
    _dbus_file_close (&file, NULL);

  if (need_unlink && unlink (tmp_filename_c) < 0)
    _dbus_verbose ("Failed to unlink temp file %s: %s\n",
                   tmp_filename_c, _dbus_strerror (errno));

  _dbus_string_free (&tmp_filename);

  if (!retval)
    _DBUS_ASSERT_ERROR_IS_SET (error);

  return retval;
}


/** Creates the given file, failing if the file already exists.
 *
 * @param filename the filename
 * @param error error location
 * @returns #TRUE if we created the file and it didn't exist
 */
dbus_bool_t
_dbus_create_file_exclusively (const DBusString *filename,
                               DBusError        *error)
{
  DBusFile file;
  const char *filename_c;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  filename_c = _dbus_string_get_const_data (filename);

  if (!_dbus_file_open (&file, filename_c, O_WRONLY | O_BINARY | O_EXCL | O_CREAT,
                        0600))
    {
      dbus_set_error (error,
                      DBUS_ERROR_FAILED,
                      "Could not create file %s: %s\n",
                      filename_c,
                      _dbus_strerror (errno));
      return FALSE;
    }

  if (!_dbus_file_close (&file, NULL))
    {
      dbus_set_error (error,
                      DBUS_ERROR_FAILED,
                      "Could not close file %s: %s\n",
                      filename_c,
                      _dbus_strerror (errno));
      return FALSE;
    }

  return TRUE;
}


/**
 * Creates a directory; succeeds if the directory
 * is created or already existed.
 *
 * @param filename directory filename
 * @param error initialized error object
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_create_directory (const DBusString *filename,
                        DBusError        *error)
{
  const char *filename_c;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  filename_c = _dbus_string_get_const_data (filename);

  if (_dbus_mkdir (filename_c, 0700) < 0)
    {
      if (errno == EEXIST)
        return TRUE;

      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Failed to create directory %s: %s\n",
                      filename_c, _dbus_strerror (errno));
      return FALSE;
    }
  else
    return TRUE;
}


static void
pseudorandom_generate_random_bytes_buffer (char *buffer,
    int   n_bytes)
{
  long tv_usec;
  int i;

  /* fall back to pseudorandom */
  _dbus_verbose ("Falling back to pseudorandom for %d bytes\n",
                 n_bytes);

  _dbus_get_current_time (NULL, &tv_usec);
  srand (tv_usec);

  i = 0;
  while (i < n_bytes)
    {
      double r;
      unsigned int b;

      r = rand ();
      b = (r / (double) RAND_MAX) * 255.0;

      buffer[i] = b;

      ++i;
    }
}

static dbus_bool_t
pseudorandom_generate_random_bytes (DBusString *str,
                                    int         n_bytes)
{
  int old_len;
  char *p;

  old_len = _dbus_string_get_length (str);

  if (!_dbus_string_lengthen (str, n_bytes))
    return FALSE;

  p = _dbus_string_get_data_len (str, old_len, n_bytes);

  pseudorandom_generate_random_bytes_buffer (p, n_bytes);

  return TRUE;
}

/**
 * Gets the temporary files directory by inspecting the environment variables 
 * TMPDIR, TMP, and TEMP in that order. If none of those are set "/tmp" is returned
 *
 * @returns location of temp directory
 */
const char*
_dbus_get_tmpdir(void)
{
  static const char* tmpdir = NULL;

  if (tmpdir == NULL)
    {
      if (tmpdir == NULL)
        tmpdir = getenv("TMP");
      if (tmpdir == NULL)
        tmpdir = getenv("TEMP");
      if (tmpdir == NULL)
        tmpdir = getenv("TMPDIR");
      if (tmpdir == NULL)
          tmpdir = "C:\\Temp";
    }

  _dbus_assert(tmpdir != NULL);

  return tmpdir;
}


/**
 * Deletes the given file.
 *
 * @param filename the filename
 * @param error error location
 * 
 * @returns #TRUE if unlink() succeeded
 */
dbus_bool_t
_dbus_delete_file (const DBusString *filename,
                   DBusError        *error)
{
  const char *filename_c;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  filename_c = _dbus_string_get_const_data (filename);

  if (unlink (filename_c) < 0)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Failed to delete file %s: %s\n",
                      filename_c, _dbus_strerror (errno));
      return FALSE;
    }
  else
    return TRUE;
}

/**
 * Generates the given number of random bytes,
 * using the best mechanism we can come up with.
 *
 * @param str the string
 * @param n_bytes the number of random bytes to append to string
 * @returns #TRUE on success, #FALSE if no memory
 */
dbus_bool_t
_dbus_generate_random_bytes (DBusString *str,
                             int         n_bytes)
{
  return pseudorandom_generate_random_bytes (str, n_bytes);
}

#if !defined (DBUS_DISABLE_ASSERT) || defined(DBUS_BUILD_TESTS)

#ifdef _MSC_VER
# ifdef BACKTRACES
#  undef BACKTRACES
# endif
#else
# define BACKTRACES
#endif

#ifdef BACKTRACES
/*
 * Backtrace Generator
 *
 * Copyright 2004 Eric Poech
 * Copyright 2004 Robert Shearman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <winver.h>
#include <imagehlp.h>
#include <stdio.h>

#define DPRINTF _dbus_warn

#ifdef _MSC_VER
#define BOOL int

#define __i386__
#endif

//#define MAKE_FUNCPTR(f) static typeof(f) * p##f

//MAKE_FUNCPTR(StackWalk);
//MAKE_FUNCPTR(SymGetModuleBase);
//MAKE_FUNCPTR(SymFunctionTableAccess);
//MAKE_FUNCPTR(SymInitialize);
//MAKE_FUNCPTR(SymGetSymFromAddr);
//MAKE_FUNCPTR(SymGetModuleInfo);
static BOOL (WINAPI *pStackWalk)(
  DWORD MachineType,
  HANDLE hProcess,
  HANDLE hThread,
  LPSTACKFRAME StackFrame,
  PVOID ContextRecord,
  PREAD_PROCESS_MEMORY_ROUTINE ReadMemoryRoutine,
  PFUNCTION_TABLE_ACCESS_ROUTINE FunctionTableAccessRoutine,
  PGET_MODULE_BASE_ROUTINE GetModuleBaseRoutine,
  PTRANSLATE_ADDRESS_ROUTINE TranslateAddress
);
static DWORD (WINAPI *pSymGetModuleBase)(
  HANDLE hProcess,
  DWORD dwAddr
);
static PVOID  (WINAPI *pSymFunctionTableAccess)(
  HANDLE hProcess,
  DWORD AddrBase
);
static BOOL  (WINAPI *pSymInitialize)(
  HANDLE hProcess,
  PSTR UserSearchPath,
  BOOL fInvadeProcess
);
static BOOL  (WINAPI *pSymGetSymFromAddr)(
  HANDLE hProcess,
  DWORD Address,
  PDWORD Displacement,
  PIMAGEHLP_SYMBOL Symbol
);
static BOOL  (WINAPI *pSymGetModuleInfo)(
  HANDLE hProcess,
  DWORD dwAddr,
  PIMAGEHLP_MODULE ModuleInfo
);
static DWORD  (WINAPI *pSymSetOptions)(
  DWORD SymOptions
);


static BOOL init_backtrace()
{
    HMODULE hmodDbgHelp = LoadLibraryA("dbghelp");
/*
    #define GETFUNC(x) \
    p##x = (typeof(x)*)GetProcAddress(hmodDbgHelp, #x); \
    if (!p##x) \
    { \
        return FALSE; \
    }
    */


//    GETFUNC(StackWalk);
//    GETFUNC(SymGetModuleBase);
//    GETFUNC(SymFunctionTableAccess);
//    GETFUNC(SymInitialize);
//    GETFUNC(SymGetSymFromAddr);
//    GETFUNC(SymGetModuleInfo);

#define FUNC(x) #x

      pStackWalk = (BOOL  (WINAPI *)(
DWORD MachineType,
HANDLE hProcess,
HANDLE hThread,
LPSTACKFRAME StackFrame,
PVOID ContextRecord,
PREAD_PROCESS_MEMORY_ROUTINE ReadMemoryRoutine,
PFUNCTION_TABLE_ACCESS_ROUTINE FunctionTableAccessRoutine,
PGET_MODULE_BASE_ROUTINE GetModuleBaseRoutine,
PTRANSLATE_ADDRESS_ROUTINE TranslateAddress
))GetProcAddress (hmodDbgHelp, FUNC(StackWalk));
    pSymGetModuleBase=(DWORD  (WINAPI *)(
  HANDLE hProcess,
  DWORD dwAddr
))GetProcAddress (hmodDbgHelp, FUNC(SymGetModuleBase));
    pSymFunctionTableAccess=(PVOID  (WINAPI *)(
  HANDLE hProcess,
  DWORD AddrBase
))GetProcAddress (hmodDbgHelp, FUNC(SymFunctionTableAccess));
    pSymInitialize = (BOOL  (WINAPI *)(
  HANDLE hProcess,
  PSTR UserSearchPath,
  BOOL fInvadeProcess
))GetProcAddress (hmodDbgHelp, FUNC(SymInitialize));
    pSymGetSymFromAddr = (BOOL  (WINAPI *)(
  HANDLE hProcess,
  DWORD Address,
  PDWORD Displacement,
  PIMAGEHLP_SYMBOL Symbol
))GetProcAddress (hmodDbgHelp, FUNC(SymGetSymFromAddr));
    pSymGetModuleInfo = (BOOL  (WINAPI *)(
  HANDLE hProcess,
  DWORD dwAddr,
  PIMAGEHLP_MODULE ModuleInfo
))GetProcAddress (hmodDbgHelp, FUNC(SymGetModuleInfo));
pSymSetOptions = (DWORD  (WINAPI *)(
DWORD SymOptions
))GetProcAddress (hmodDbgHelp, FUNC(SymSetOptions));


    pSymSetOptions(SYMOPT_UNDNAME);

    pSymInitialize(GetCurrentProcess(), NULL, TRUE);

    return TRUE;
}

static void dump_backtrace_for_thread(HANDLE hThread)
{
    STACKFRAME sf;
    CONTEXT context;
    DWORD dwImageType;

    if (!pStackWalk)
        if (!init_backtrace())
            return;

    /* can't use this function for current thread as GetThreadContext
     * doesn't support getting context from current thread */
    if (hThread == GetCurrentThread())
        return;

    DPRINTF("Backtrace:\n");

    memset(&context, 0, sizeof(context));
    context.ContextFlags = CONTEXT_FULL;

    SuspendThread(hThread);

    if (!GetThreadContext(hThread, &context))
    {
        DPRINTF("Couldn't get thread context (error %ld)\n", GetLastError());
        ResumeThread(hThread);
        return;
    }

    memset(&sf, 0, sizeof(sf));

#ifdef __i386__
    sf.AddrFrame.Offset = context.Ebp;
    sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrPC.Offset = context.Eip;
    sf.AddrPC.Mode = AddrModeFlat;
    dwImageType = IMAGE_FILE_MACHINE_I386;
#else
# error You need to fill in the STACKFRAME structure for your architecture
#endif

    while (pStackWalk(dwImageType, GetCurrentProcess(),
                     hThread, &sf, &context, NULL, pSymFunctionTableAccess,
                     pSymGetModuleBase, NULL))
    {
        BYTE buffer[256];
        IMAGEHLP_SYMBOL * pSymbol = (IMAGEHLP_SYMBOL *)buffer;
        DWORD dwDisplacement;

        pSymbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL);
        pSymbol->MaxNameLength = sizeof(buffer) - sizeof(IMAGEHLP_SYMBOL) + 1;

        if (!pSymGetSymFromAddr(GetCurrentProcess(), sf.AddrPC.Offset,
                                &dwDisplacement, pSymbol))
        {
            IMAGEHLP_MODULE ModuleInfo;
            ModuleInfo.SizeOfStruct = sizeof(ModuleInfo);

            if (!pSymGetModuleInfo(GetCurrentProcess(), sf.AddrPC.Offset,
                                   &ModuleInfo))
                DPRINTF("1\t%p\n", (void*)sf.AddrPC.Offset);
            else
                DPRINTF("2\t%s+0x%lx\n", ModuleInfo.ImageName,
                    sf.AddrPC.Offset - ModuleInfo.BaseOfImage);
        }
        else if (dwDisplacement)
            DPRINTF("3\t%s+0x%lx\n", pSymbol->Name, dwDisplacement);
        else
            DPRINTF("4\t%s\n", pSymbol->Name);
    }

    ResumeThread(hThread);
}

static DWORD WINAPI dump_thread_proc(LPVOID lpParameter)
{
    dump_backtrace_for_thread((HANDLE)lpParameter);
    return 0;
}

/* cannot get valid context from current thread, so we have to execute
 * backtrace from another thread */
static void dump_backtrace()
{
    HANDLE hCurrentThread;
    HANDLE hThread;
    DWORD dwThreadId;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
        GetCurrentProcess(), &hCurrentThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    hThread = CreateThread(NULL, 0, dump_thread_proc, (LPVOID)hCurrentThread,
        0, &dwThreadId);
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    CloseHandle(hCurrentThread);
}

void _dbus_print_backtrace(void)
{
  init_backtrace();
  dump_backtrace();
}
#else
void _dbus_print_backtrace(void)
{
  _dbus_verbose ("  D-Bus not compiled with backtrace support\n");
}
#endif

/**
 * Sends a single nul byte with our UNIX credentials as ancillary
 * data.  Returns #TRUE if the data was successfully written.  On
 * systems that don't support sending credentials, just writes a byte,
 * doesn't send any credentials.  On some systems, such as Linux,
 * reading/writing the byte isn't actually required, but we do it
 * anyway just to avoid multiple codepaths.
 *
 * Fails if no byte can be written, so you must select() first.
 *
 * The point of the byte is that on some systems we have to
 * use sendmsg()/recvmsg() to transmit credentials.
 *
 * @param server_fd file descriptor for connection to server
 * @param error return location for error code
 * @returns #TRUE if the byte was sent
 */
dbus_bool_t
_dbus_send_credentials_unix_socket  (int              server_fd,
                                     DBusError       *error)
{
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (write_credentials_byte (server_fd, error))
    return TRUE;
  else
    return FALSE;
}

static dbus_uint32_t fromAscii(char ascii)
{
    if(ascii >= '0' && ascii <= '9')
        return ascii - '0';
    if(ascii >= 'A' && ascii <= 'F')
        return ascii - 'A' + 10;
    if(ascii >= 'a' && ascii <= 'f')
        return ascii - 'a' + 10;
    return 0;    
}

dbus_bool_t _dbus_read_local_machine_uuid   (DBusGUID         *machine_id,
                                             dbus_bool_t       create_if_not_found,
                                             DBusError        *error)
{
#ifdef DBUS_WINCE
	return TRUE;
  // TODO
#else
    HW_PROFILE_INFOA info;
    char *lpc = &info.szHwProfileGuid[0];
    dbus_uint32_t u;

    //  the hw-profile guid lives long enough
    if(!GetCurrentHwProfileA(&info))
      {
        dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL); // FIXME
        return FALSE;  
      }

    // Form: {12340001-4980-1920-6788-123456789012}
    lpc++;
    // 12340001
    u = ((fromAscii(lpc[0]) <<  0) |
         (fromAscii(lpc[1]) <<  4) |
         (fromAscii(lpc[2]) <<  8) |
         (fromAscii(lpc[3]) << 12) |
         (fromAscii(lpc[4]) << 16) |
         (fromAscii(lpc[5]) << 20) |
         (fromAscii(lpc[6]) << 24) |
         (fromAscii(lpc[7]) << 28));
    machine_id->as_uint32s[0] = u;

    lpc += 9;
    // 4980-1920
    u = ((fromAscii(lpc[0]) <<  0) |
         (fromAscii(lpc[1]) <<  4) |
         (fromAscii(lpc[2]) <<  8) |
         (fromAscii(lpc[3]) << 12) |
         (fromAscii(lpc[5]) << 16) |
         (fromAscii(lpc[6]) << 20) |
         (fromAscii(lpc[7]) << 24) |
         (fromAscii(lpc[8]) << 28));
    machine_id->as_uint32s[1] = u;
    
    lpc += 10;
    // 6788-1234
    u = ((fromAscii(lpc[0]) <<  0) |
         (fromAscii(lpc[1]) <<  4) |
         (fromAscii(lpc[2]) <<  8) |
         (fromAscii(lpc[3]) << 12) |
         (fromAscii(lpc[5]) << 16) |
         (fromAscii(lpc[6]) << 20) |
         (fromAscii(lpc[7]) << 24) |
         (fromAscii(lpc[8]) << 28));
    machine_id->as_uint32s[2] = u;
    
    lpc += 9;
    // 56789012
    u = ((fromAscii(lpc[0]) <<  0) |
         (fromAscii(lpc[1]) <<  4) |
         (fromAscii(lpc[2]) <<  8) |
         (fromAscii(lpc[3]) << 12) |
         (fromAscii(lpc[4]) << 16) |
         (fromAscii(lpc[5]) << 20) |
         (fromAscii(lpc[6]) << 24) |
         (fromAscii(lpc[7]) << 28));
    machine_id->as_uint32s[3] = u;
#endif
    return TRUE;
}

static
HANDLE _dbus_global_lock (const char *mutexname)
{
  HANDLE mutex;
  DWORD gotMutex;

  mutex = CreateMutex( NULL, FALSE, mutexname );
  if( !mutex )
    {
      return FALSE;
    }

   gotMutex = WaitForSingleObject( mutex, INFINITE );
   switch( gotMutex )
     {
       case WAIT_ABANDONED:
               ReleaseMutex (mutex);
               CloseHandle (mutex);
               return 0;
       case WAIT_FAILED:
       case WAIT_TIMEOUT:
               return 0;
     }

   return mutex;
}

static
void _dbus_global_unlock (HANDLE mutex)
{
  ReleaseMutex (mutex);
  CloseHandle (mutex); 
}

// for proper cleanup in dbus-daemon
static HANDLE hDBusDaemonMutex = NULL;
static HANDLE hDBusSharedMem = NULL;
// sync _dbus_daemon_init, _dbus_daemon_uninit and _dbus_daemon_already_runs
static const char *cUniqueDBusInitMutex = "UniqueDBusInitMutex";
// sync _dbus_get_autolaunch_address
static const char *cDBusAutolaunchMutex = "DBusAutolaunchMutex";
// mutex to determine if dbus-daemon is already started (per user)
static const char *cDBusDaemonMutex = "DBusDaemonMutex";
// named shm for dbus adress info (per user)
static const char *cDBusDaemonAddressInfo = "DBusDaemonAddressInfo";

void
_dbus_daemon_init(const char *host, dbus_uint32_t port)
{
  HANDLE lock;
  const char *adr = NULL;
  char szUserName[64];
  DWORD dwUserNameSize = sizeof(szUserName);
  char szDBusDaemonMutex[128];
  char szDBusDaemonAddressInfo[128];
  char szAddress[128];

  _dbus_assert(host);
  _dbus_assert(port);

  _snprintf(szAddress, sizeof(szAddress) - 1, "tcp:host=%s,port=%d", host, port);

  _dbus_assert( GetUserName(szUserName, &dwUserNameSize) != 0);
  _snprintf(szDBusDaemonMutex, sizeof(szDBusDaemonMutex) - 1, "%s:%s",
            cDBusDaemonMutex, szUserName);
  _snprintf(szDBusDaemonAddressInfo, sizeof(szDBusDaemonAddressInfo) - 1, "%s:%s",
            cDBusDaemonAddressInfo, szUserName);

  // before _dbus_global_lock to keep correct lock/release order
  hDBusDaemonMutex = CreateMutex( NULL, FALSE, szDBusDaemonMutex );

  _dbus_assert(WaitForSingleObject( hDBusDaemonMutex, 1000 ) == WAIT_OBJECT_0);

  // sync _dbus_daemon_init, _dbus_daemon_uninit and _dbus_daemon_already_runs
  lock = _dbus_global_lock( cUniqueDBusInitMutex );

  // create shm
  hDBusSharedMem = CreateFileMapping( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                      0, strlen( szAddress ) + 1, szDBusDaemonAddressInfo );
  _dbus_assert( hDBusSharedMem );

  adr = MapViewOfFile( hDBusSharedMem, FILE_MAP_WRITE, 0, 0, 0 );

  _dbus_assert( adr );

  strcpy(adr, szAddress);

  // cleanup
  UnmapViewOfFile( adr );

  _dbus_global_unlock( lock );
}

void
_dbus_daemon_release()
{
  HANDLE lock;

  // sync _dbus_daemon_init, _dbus_daemon_uninit and _dbus_daemon_already_runs
  lock = _dbus_global_lock( cUniqueDBusInitMutex );

  CloseHandle( hDBusSharedMem );

  hDBusSharedMem = NULL;

  ReleaseMutex( hDBusDaemonMutex );

  CloseHandle( hDBusDaemonMutex );

  hDBusDaemonMutex = NULL;

  _dbus_global_unlock( lock );
}

static dbus_bool_t
_dbus_get_autolaunch_shm(DBusString *adress)
{
  HANDLE sharedMem;
  const char *adr;
  char szUserName[64];
  DWORD dwUserNameSize = sizeof(szUserName);
  char szDBusDaemonAddressInfo[128];

  if( !GetUserName(szUserName, &dwUserNameSize) )
      return FALSE;
  _snprintf(szDBusDaemonAddressInfo, sizeof(szDBusDaemonAddressInfo) - 1, "%s:%s",
            cDBusDaemonAddressInfo, szUserName);

  // read shm
  do {
      // we know that dbus-daemon is available, so we wait until shm is available
      sharedMem = OpenFileMapping( FILE_MAP_READ, FALSE, szDBusDaemonAddressInfo );
      if( sharedMem == 0 )
          Sleep( 100 );
  } while( sharedMem == 0 );

  if( sharedMem == 0 )
      return FALSE;

  adr = MapViewOfFile( sharedMem, FILE_MAP_READ, 0, 0, 0 );

  if( adr == 0 )
      return FALSE;

  _dbus_string_init( adress );

  _dbus_string_append( adress, adr ); 

  // cleanup
  UnmapViewOfFile( adr );

  CloseHandle( sharedMem );

  return TRUE;
}

static dbus_bool_t
_dbus_daemon_already_runs (DBusString *adress)
{
  HANDLE lock;
  HANDLE daemon;
  dbus_bool_t bRet = TRUE;
  char szUserName[64];
  DWORD dwUserNameSize = sizeof(szUserName);
  char szDBusDaemonMutex[128];

  // sync _dbus_daemon_init, _dbus_daemon_uninit and _dbus_daemon_already_runs
  lock = _dbus_global_lock( cUniqueDBusInitMutex );

  if( !GetUserName(szUserName, &dwUserNameSize) )
      return FALSE;
  _snprintf(szDBusDaemonMutex, sizeof(szDBusDaemonMutex) - 1, "%s:%s",
            cDBusDaemonMutex, szUserName);

  // do checks
  daemon = CreateMutex( NULL, FALSE, szDBusDaemonMutex );
  if(WaitForSingleObject( daemon, 10 ) != WAIT_TIMEOUT)
    {
      ReleaseMutex (daemon);
      CloseHandle (daemon);

      _dbus_global_unlock( lock );
      return FALSE;
    }

  // read shm
  bRet = _dbus_get_autolaunch_shm( adress );

  // cleanup
  CloseHandle ( daemon );

  _dbus_global_unlock( lock );

  return bRet;
}

dbus_bool_t
_dbus_get_autolaunch_address (DBusString *address, 
                              DBusError *error)
{
  HANDLE mutex;
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  dbus_bool_t retval = FALSE;
  LPSTR lpFile;
  char dbus_exe_path[MAX_PATH];
  char dbus_args[MAX_PATH * 2];

  mutex = _dbus_global_lock ( cDBusAutolaunchMutex );

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (_dbus_daemon_already_runs(address))
    {
        printf("dbus daemon already exists\n");
        retval = TRUE;
        goto out;
    }

  if (!SearchPathA(NULL, "dbus-daemon.exe", NULL, sizeof(dbus_exe_path), dbus_exe_path, &lpFile))
    {
      printf ("could not find dbus-daemon executable\n");
      goto out;
    }

  // Create process
  ZeroMemory( &si, sizeof(si) );
  si.cb = sizeof(si);
  ZeroMemory( &pi, sizeof(pi) );

  _snprintf(dbus_args, sizeof(dbus_args) - 1, "\"%s\" %s", dbus_exe_path,  " --session");

//  argv[i] = "--config-file=bus\\session.conf";
  printf("create process \"%s\" %s\n", dbus_exe_path, dbus_args);
  if(CreateProcessA(dbus_exe_path, dbus_args, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
      retval = TRUE;

      // Wait until started (see _dbus_get_autolaunch_shm())
      WaitForInputIdle(pi.hProcess, INFINITE);

      retval = _dbus_get_autolaunch_shm( address );
    } else {
      retval = FALSE;
    }
  
out:
  if (retval)
    _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  else
    _DBUS_ASSERT_ERROR_IS_SET (error);
  
  _dbus_global_unlock (mutex);

  return retval;
 }


/** Makes the file readable by every user in the system.
 *
 * @param filename the filename
 * @param error error location
 * @returns #TRUE if the file's permissions could be changed.
 */
dbus_bool_t
_dbus_make_file_world_readable(const DBusString *filename,
                               DBusError *error)
{
  // TODO
  return TRUE;
}


#define DBUS_STANDARD_SESSION_SERVICEDIR "/dbus-1/services"

// @TODO: this function is duplicated from dbus-sysdeps-unix.c
//        and differs only in the path separator, may be we should 
//        use a dbus path separator variable

#define _dbus_path_seperator ";"

static dbus_bool_t
split_paths_and_append (DBusString *dirs, 
                        const char *suffix, 
                        DBusList **dir_list)
{
   int start;
   int i;
   int len;
   char *cpath;
   const DBusString file_suffix;

   start = 0;
   i = 0;

   _dbus_string_init_const (&file_suffix, suffix);

   len = _dbus_string_get_length (dirs);

   while (_dbus_string_find (dirs, start, _dbus_path_seperator, &i))
     {
       DBusString path;

       if (!_dbus_string_init (&path))
          goto oom;

       if (!_dbus_string_copy_len (dirs,
                                   start,
                                   i - start,
                                   &path,
                                   0))
          {
            _dbus_string_free (&path);
            goto oom;
          }

        _dbus_string_chop_white (&path);

        /* check for an empty path */
        if (_dbus_string_get_length (&path) == 0)
          goto next;

        if (!_dbus_concat_dir_and_file (&path,
                                        &file_suffix))
          {
            _dbus_string_free (&path);
            goto oom;
          }

        if (!_dbus_string_copy_data(&path, &cpath))
          {
            _dbus_string_free (&path);
            goto oom;
          }

        if (!_dbus_list_append (dir_list, cpath))
          {
            _dbus_string_free (&path);              
            dbus_free (cpath);
            goto oom;
          }

       next:
        _dbus_string_free (&path);
        start = i + 1;
    } 
      
  if (start != len)
    { 
      DBusString path;

      if (!_dbus_string_init (&path))
        goto oom;

      if (!_dbus_string_copy_len (dirs,
                                  start,
                                  len - start,
                                  &path,
                                  0))
        {
          _dbus_string_free (&path);
          goto oom;
        }

      if (!_dbus_concat_dir_and_file (&path,
                                      &file_suffix))
        {
          _dbus_string_free (&path);
          goto oom;
        }

      if (!_dbus_string_copy_data(&path, &cpath))
        {
          _dbus_string_free (&path);
          goto oom;
        }

      if (!_dbus_list_append (dir_list, cpath))
        {
          _dbus_string_free (&path);              
          dbus_free (cpath);
          goto oom;
        }

      _dbus_string_free (&path); 
    }

  return TRUE;

 oom:
  _dbus_list_foreach (dir_list, (DBusForeachFunction)dbus_free, NULL); 
  _dbus_list_clear (dir_list);
  return FALSE;
}

/**
 * Returns the standard directories for a session bus to look for service 
 * activation files 
 *
 * On Windows this should be data directories:
 *
 * %CommonProgramFiles%/dbus
 *
 * and
 *
 * DBUS_DATADIR
 *
 * @param dirs the directory list we are returning
 * @returns #FALSE on OOM 
 */

dbus_bool_t 
_dbus_get_standard_session_servicedirs (DBusList **dirs)
{
  const char *common_progs;
  DBusString servicedir_path;

  if (!_dbus_string_init (&servicedir_path))
    return FALSE;

  if (!_dbus_string_append (&servicedir_path, DBUS_DATADIR";"))
        goto oom;

  common_progs = _dbus_getenv ("CommonProgramFiles");

  if (common_progs != NULL)
    {
      if (!_dbus_string_append (&servicedir_path, common_progs))
        goto oom;

      if (!_dbus_string_append (&servicedir_path, ";"))
        goto oom;
    }

  if (!split_paths_and_append (&servicedir_path, 
                               DBUS_STANDARD_SESSION_SERVICEDIR, 
                               dirs))
    goto oom;

  _dbus_string_free (&servicedir_path);  
  return TRUE;

 oom:
  _dbus_string_free (&servicedir_path);
  return FALSE;
}

_DBUS_DEFINE_GLOBAL_LOCK (atomic);

/**
 * Atomically increments an integer
 *
 * @param atomic pointer to the integer to increment
 * @returns the value before incrementing
 *
 */
dbus_int32_t
_dbus_atomic_inc (DBusAtomic *atomic)
{
  // +/- 1 is needed here!
  return InterlockedIncrement (&atomic->value) - 1;
}

/**
 * Atomically decrement an integer
 *
 * @param atomic pointer to the integer to decrement
 * @returns the value before decrementing
 *
 */
dbus_int32_t
_dbus_atomic_dec (DBusAtomic *atomic)
{
  // +/- 1 is needed here!
  return InterlockedDecrement (&atomic->value) + 1;
}

#endif /* asserts or tests enabled */

/** @} end of sysdeps-win */

/* tests in dbus-sysdeps-util.c */
