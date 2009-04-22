/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#undef open

#define STRSAFE_NO_DEPRECATE

#ifndef DBUS_WINCE
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
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
#include "dbus-list.h"
#include "dbus-credentials.h"

#include <windows.h>
#include <ws2tcpip.h>
#include <wincrypt.h>

/* Declarations missing in mingw's headers */
extern BOOL WINAPI ConvertStringSidToSidA (LPCSTR  StringSid, PSID *Sid);
extern BOOL WINAPI ConvertSidToStringSidA (PSID Sid, LPSTR *StringSid);

#include <fcntl.h>

#include <process.h>
#include <stdio.h>
#include <io.h>

#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_WSPIAPI_H
// needed for w2k compatibility (getaddrinfo/freeaddrinfo/getnameinfo)
#ifdef __GNUC__
#define _inline
#include "wspiapi.h"
#else
#include <wspiapi.h>
#endif
#endif // HAVE_WSPIAPI_H

#ifndef O_BINARY
#define O_BINARY 0
#endif

typedef int socklen_t;

static char*
_dbus_win_error_string (int error_number)
{
  char *msg;

  FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER |
                 FORMAT_MESSAGE_IGNORE_INSERTS |
                 FORMAT_MESSAGE_FROM_SYSTEM,
                 NULL, error_number, 0,
                 (LPSTR) &msg, 0, NULL);

  if (msg[strlen (msg) - 1] == '\n')
    msg[strlen (msg) - 1] = '\0';
  if (msg[strlen (msg) - 1] == '\r')
    msg[strlen (msg) - 1] = '\0';

  return msg;
}

static void
_dbus_win_free_error_string (char *string)
{
  LocalFree (string);
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
  const char *buffer_c = _dbus_string_get_const_data (buffer);

  written = _write (pipe->fd_or_handle, buffer_c + start, len);
  if (written < 0)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Writing to pipe: %s\n",
                      strerror (errno));
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
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (_close (pipe->fd_or_handle) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Could not close pipe %d: %s", pipe->fd_or_handle, strerror (errno));
      return -1;
    }
  else
    {
      _dbus_pipe_invalidate (pipe);
      return 0;
    }
}

/**
 * Socket interface
 *
 */

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
_dbus_read_socket (int               fd,
                   DBusString       *buffer,
                   int               count)
{
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

 again:
 
  _dbus_verbose ("recv: count=%d fd=%d\n", count, fd);
  bytes_read = recv (fd, data, count, 0);
  
  if (bytes_read == SOCKET_ERROR)
	{
	  DBUS_SOCKET_SET_ERRNO();
	  _dbus_verbose ("recv: failed: %s\n", _dbus_strerror (errno));
	  bytes_read = -1;
	}
	else
	  _dbus_verbose ("recv: = %d\n", bytes_read);

  if (bytes_read < 0)
    {
      if (errno == EINTR)
        goto again;
      else    	
        {
          /* put length back (note that this doesn't actually realloc anything) */
          _dbus_string_set_length (buffer, start);
          return -1;
        }
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
_dbus_write_socket (int               fd,
                    const DBusString *buffer,
                    int               start,
                    int               len)
{
  const char *data;
  int bytes_written;

  data = _dbus_string_get_const_data_len (buffer, start, len);

 again:

  _dbus_verbose ("send: len=%d fd=%d\n", len, fd);
  bytes_written = send (fd, data, len, 0);

  if (bytes_written == SOCKET_ERROR)
    {
      DBUS_SOCKET_SET_ERRNO();
      _dbus_verbose ("send: failed: %s\n", _dbus_strerror (errno));
      bytes_written = -1;
    }
    else
      _dbus_verbose ("send: = %d\n", bytes_written);

  if (bytes_written < 0 && errno == EINTR)
    goto again;
    
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
_dbus_close_socket (int        fd,
                    DBusError *error)
{
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

 again:
  if (closesocket (fd) == SOCKET_ERROR)
    {
      DBUS_SOCKET_SET_ERRNO ();
      
      if (errno == EINTR)
        goto again;
        
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Could not close socket: socket=%d, , %s",
                      fd, _dbus_strerror (errno));
      return FALSE;
    }
  _dbus_verbose ("_dbus_close_socket: socket=%d, \n", fd);

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
#ifdef ENABLE_DBUSSOCKET
  DBusSocket *s;
  if (handle < 0)
    return;

  _dbus_lock_sockets();

  _dbus_handle_to_socket_unlocked (handle, &s);
  s->close_on_exec = TRUE;

  _dbus_unlock_sockets();
#else
  /* TODO unic code.
  int val;
  
  val = fcntl (fd, F_GETFD, 0);
  
  if (val < 0)
    return;

  val |= FD_CLOEXEC;
  
  fcntl (fd, F_SETFD, val);
  */
#endif
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
  u_long one = 1;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (ioctlsocket (handle, FIONBIO, &one) == SOCKET_ERROR)
    {
      dbus_set_error (error, _dbus_error_from_errno (WSAGetLastError ()),
                      "Failed to set socket %d:%d to nonblocking: %s", handle,
                      _dbus_strerror (WSAGetLastError ()));
      return FALSE;
    }

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
_dbus_write_socket_two (int               fd,
                        const DBusString *buffer1,
                        int               start1,
                        int               len1,
                        const DBusString *buffer2,
                        int               start2,
                        int               len2)
{
  WSABUF vectors[2];
  const char *data1;
  const char *data2;
  int rc;
  DWORD bytes_written;

  _dbus_assert (buffer1 != NULL);
  _dbus_assert (start1 >= 0);
  _dbus_assert (start2 >= 0);
  _dbus_assert (len1 >= 0);
  _dbus_assert (len2 >= 0);


  data1 = _dbus_string_get_const_data_len (buffer1, start1, len1);

  if (buffer2 != NULL)
    data2 = _dbus_string_get_const_data_len (buffer2, start2, len2);
  else
    {
      data2 = NULL;
      start2 = 0;
      len2 = 0;
    }

  vectors[0].buf = (char*) data1;
  vectors[0].len = len1;
  vectors[1].buf = (char*) data2;
  vectors[1].len = len2;

 again:
 
  _dbus_verbose ("WSASend: len1+2=%d+%d fd=%d\n", len1, len2, fd);
  rc = WSASend (fd, 
                vectors,
                data2 ? 2 : 1, 
                &bytes_written,
                0, 
                NULL, 
                NULL);
                
  if (rc < 0)
    {
      DBUS_SOCKET_SET_ERRNO ();
      _dbus_verbose ("WSASend: failed: %s\n", _dbus_strerror (errno));
      bytes_written = -1;
    }
  else
    _dbus_verbose ("WSASend: = %ld\n", bytes_written);
    
  if (bytes_written < 0 && errno == EINTR)
    goto again;
      
  return bytes_written;
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
  char buf[1024];
  int bufsize;
  int len;

  bufsize = sizeof (buf);
  len = _vsnprintf (buf, bufsize - 1, format, args);

  while (len == -1) /* try again */
    {
      char *p;

      bufsize *= 2;

      p = malloc (bufsize);
      len = _vsnprintf (p, bufsize - 1, format, args);
      free (p);
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

/** @} end of sysdeps-win */


/** Gets our UID
 * @returns process UID
 */
dbus_uid_t
_dbus_getuid (void)
{
	return DBUS_UID_UNSET;
}

/**
 * The only reason this is separate from _dbus_getpid() is to allow it
 * on Windows for logging but not for other purposes.
 * 
 * @returns process ID to put in log messages
 */
unsigned long
_dbus_pid_for_log (void)
{
  return _dbus_getpid ();
}

/** Gets our SID
 * @param points to sid buffer, need to be freed with LocalFree()
 * @returns process sid
 */
dbus_bool_t
_dbus_getsid(char **sid)
{
  HANDLE process_token = NULL;
  TOKEN_USER *token_user = NULL;
  DWORD n;
  PSID psid;
  int retval = FALSE;
  
  if (!OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &process_token)) 
    {
      _dbus_win_warn_win_error ("OpenProcessToken failed", GetLastError ());
      goto failed;
    }
  if ((!GetTokenInformation (process_token, TokenUser, NULL, 0, &n)
            && GetLastError () != ERROR_INSUFFICIENT_BUFFER)
           || (token_user = alloca (n)) == NULL
           || !GetTokenInformation (process_token, TokenUser, token_user, n, &n))
    {
      _dbus_win_warn_win_error ("GetTokenInformation failed", GetLastError ());
      goto failed;
    }
  psid = token_user->User.Sid;
  if (!IsValidSid (psid))
    {
      _dbus_verbose("%s invalid sid\n",__FUNCTION__);
      goto failed;
    }
  if (!ConvertSidToStringSidA (psid, sid))
    {
      _dbus_verbose("%s invalid sid\n",__FUNCTION__);
      goto failed;
    }
//okay:
  retval = TRUE;

failed:
  if (process_token != NULL)
    CloseHandle (process_token);

  _dbus_verbose("_dbus_getsid() returns %d\n",retval);
  return retval;
}


#ifdef DBUS_BUILD_TESTS
/** Gets our GID
 * @returns process GID
 */
dbus_gid_t
_dbus_getgid (void)
{
	return DBUS_GID_UNSET;
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
      DBUS_SOCKET_SET_ERRNO ();
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

  *fd1 = socket1;
  *fd2 = socket2;

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


#ifdef DBUS_ENABLE_VERBOSE_MODE
  msgp = msg;
  msgp += sprintf (msgp, "WSAEventSelect: to=%d\n\t", timeout_milliseconds);
  for (i = 0; i < n_fds; i++)
    {
      static dbus_bool_t warned = FALSE;
      DBusPollFD *fdp = &fds[i];


      if (fdp->events & _DBUS_POLLIN)
        msgp += sprintf (msgp, "R:%d ", fdp->fd);

      if (fdp->events & _DBUS_POLLOUT)
        msgp += sprintf (msgp, "W:%d ", fdp->fd);

      msgp += sprintf (msgp, "E:%d\n\t", fdp->fd);

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
      DBusPollFD *fdp = &fds[i];
      WSAEVENT ev;
      long lNetworkEvents = FD_OOB;

      ev = WSACreateEvent();

      if (fdp->events & _DBUS_POLLIN)
        lNetworkEvents |= FD_READ | FD_ACCEPT | FD_CLOSE;

      if (fdp->events & _DBUS_POLLOUT)
        lNetworkEvents |= FD_WRITE | FD_CONNECT;

      WSAEventSelect(fdp->fd, ev, lNetworkEvents);

      pEvents[i] = ev;
    }


  ready = WSAWaitForMultipleEvents (n_fds, pEvents, FALSE, timeout_milliseconds, FALSE);

  if (DBUS_SOCKET_API_RETURNS_ERROR (ready))
    {
      DBUS_SOCKET_SET_ERRNO ();
      if (errno != EWOULDBLOCK)
        _dbus_verbose ("WSAWaitForMultipleEvents: failed: %s\n", strerror (errno));
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

      for (i = 0; i < n_fds; i++)
        {
          DBusPollFD *fdp = &fds[i];
          WSANETWORKEVENTS ne;

          fdp->revents = 0;

          WSAEnumNetworkEvents(fdp->fd, pEvents[i], &ne);

          if (ne.lNetworkEvents & (FD_READ | FD_ACCEPT | FD_CLOSE))
            fdp->revents |= _DBUS_POLLIN;

          if (ne.lNetworkEvents & (FD_WRITE | FD_CONNECT))
            fdp->revents |= _DBUS_POLLOUT;

          if (ne.lNetworkEvents & (FD_OOB))
            fdp->revents |= _DBUS_POLLERR;

          if (ne.lNetworkEvents & (FD_READ | FD_ACCEPT | FD_CLOSE))
              msgp += sprintf (msgp, "R:%d ", fdp->fd);

          if (ne.lNetworkEvents & (FD_WRITE | FD_CONNECT))
              msgp += sprintf (msgp, "W:%d ", fdp->fd);

          if (ne.lNetworkEvents & (FD_OOB))
              msgp += sprintf (msgp, "E:%d ", fdp->fd);

          msgp += sprintf (msgp, "lNetworkEvents:%d ", ne.lNetworkEvents);

          if(ne.lNetworkEvents)
            ret++;

          WSAEventSelect(fdp->fd, pEvents[i], 0);
        }

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


#ifdef DBUS_ENABLE_VERBOSE_MODE
  msgp = msg;
  msgp += sprintf (msgp, "select: to=%d\n\t", timeout_milliseconds);
  for (i = 0; i < n_fds; i++)
    {
      static dbus_bool_t warned = FALSE;
      DBusPollFD *fdp = &fds[i];


      if (fdp->events & _DBUS_POLLIN)
        msgp += sprintf (msgp, "R:%d ", fdp->fd);

      if (fdp->events & _DBUS_POLLOUT)
        msgp += sprintf (msgp, "W:%d ", fdp->fd);

      msgp += sprintf (msgp, "E:%d\n\t", fdp->fd);

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
      DBusPollFD *fdp = &fds[i]; 

      if (fdp->events & _DBUS_POLLIN)
        FD_SET (fdp->fd, &read_set);

      if (fdp->events & _DBUS_POLLOUT)
        FD_SET (fdp->fd, &write_set);

      FD_SET (fdp->fd, &err_set);

      max_fd = MAX (max_fd, fdp->fd);
    }


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

        for (i = 0; i < n_fds; i++)
          {
            DBusPollFD *fdp = &fds[i];

            if (FD_ISSET (fdp->fd, &read_set))
              msgp += sprintf (msgp, "R:%d ", fdp->fd);

            if (FD_ISSET (fdp->fd, &write_set))
              msgp += sprintf (msgp, "W:%d ", fdp->fd);

            if (FD_ISSET (fdp->fd, &err_set))
              msgp += sprintf (msgp, "E:%d\n\t", fdp->fd);
          }
        msgp += sprintf (msgp, "\n");
        _dbus_verbose ("%s",msg);
#endif

        for (i = 0; i < n_fds; i++)
          {
            DBusPollFD *fdp = &fds[i];

            fdp->revents = 0;

            if (FD_ISSET (fdp->fd, &read_set))
              fdp->revents |= _DBUS_POLLIN;

            if (FD_ISSET (fdp->fd, &write_set))
              fdp->revents |= _DBUS_POLLOUT;

            if (FD_ISSET (fdp->fd, &err_set))
              fdp->revents |= _DBUS_POLLERR;
          }
      }
  return ready;
}

#endif  // USE_CHRIS_IMPL




/******************************************************************************
 
Original CVS version of dbus-sysdeps.c
 
******************************************************************************/
/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */


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
 * @param host the host name to connect to
 * @param port the port to connect to
 * @param family the address family to listen on, NULL for all
 * @param error return location for error code
 * @returns connection file descriptor or -1 on error
 */
int
_dbus_connect_tcp_socket (const char     *host,
                          const char     *port,
                          const char     *family,
                          DBusError      *error)
{
  int fd = -1, res;
  struct addrinfo hints;
  struct addrinfo *ai, *tmp;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  _dbus_win_startup_winsock ();

  fd = socket (AF_INET, SOCK_STREAM, 0);

  if (DBUS_SOCKET_IS_INVALID (fd))
    {
      DBUS_SOCKET_SET_ERRNO ();
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Failed to create socket: %s",
                      _dbus_strerror (errno));

      return -1;
    }

  _DBUS_ASSERT_ERROR_IS_CLEAR(error);

  _DBUS_ZERO (hints);

  if (!family)
    hints.ai_family = AF_UNSPEC;
  else if (!strcmp(family, "ipv4"))
    hints.ai_family = AF_INET;
  else if (!strcmp(family, "ipv6"))
    hints.ai_family = AF_INET6;
  else
    {
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Unknown address family %s", family);
      return -1;
    }
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_socktype = SOCK_STREAM;
#ifdef AI_ADDRCONFIG
  hints.ai_flags = AI_ADDRCONFIG;
#else
  hints.ai_flags = 0;
#endif

  if ((res = getaddrinfo(host, port, &hints, &ai)) != 0)
    {
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Failed to lookup host/port: \"%s:%s\": %s (%d)",
                      host, port, gai_strerror(res), res);
      closesocket (fd);
      return -1;
    }

  tmp = ai;
  while (tmp)
    {
      if ((fd = socket (tmp->ai_family, SOCK_STREAM, 0)) < 0)
        {
          freeaddrinfo(ai);
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                         "Failed to open socket: %s",
                         _dbus_strerror (errno));
          return -1;
        }
      _DBUS_ASSERT_ERROR_IS_CLEAR(error);

      if (connect (fd, (struct sockaddr*) tmp->ai_addr, tmp->ai_addrlen) < 0)
        {
          closesocket(fd);
      fd = -1;
          tmp = tmp->ai_next;
          continue;
        }

      break;
    }
  freeaddrinfo(ai);

  if (fd == -1)
    {
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Failed to connect to socket \"%s:%s\" %s",
                      host, port, _dbus_strerror(errno));
      return -1;
    }


  if (!_dbus_set_fd_nonblocking (fd, error))
    {
      closesocket (fd);
      fd = -1;

      return -1;
    }

  return fd;
}


void
_dbus_daemon_init(const char *host, dbus_uint32_t port);

/**
 * Creates a socket and binds it to the given path, then listens on
 * the socket. The socket is set to be nonblocking.  In case of port=0
 * a random free port is used and returned in the port parameter.
 * If inaddr_any is specified, the hostname is ignored.
 *
 * @param host the host name to listen on
 * @param port the port to listen on, if zero a free port will be used 
 * @param family the address family to listen on, NULL for all
 * @param retport string to return the actual port listened on
 * @param fds_p location to store returned file descriptors
 * @param error return location for errors
 * @returns the number of listening file descriptors or -1 on error
 */

int
_dbus_listen_tcp_socket (const char     *host,
                         const char     *port,
                         const char     *family,
                         DBusString     *retport,
                         int           **fds_p,
                         DBusError      *error)
{
  int nlisten_fd = 0, *listen_fd = NULL, res, i, port_num = -1;
  struct addrinfo hints;
  struct addrinfo *ai, *tmp;

  *fds_p = NULL;
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  _dbus_win_startup_winsock ();

  _DBUS_ZERO (hints);

  if (!family)
    hints.ai_family = AF_UNSPEC;
  else if (!strcmp(family, "ipv4"))
    hints.ai_family = AF_INET;
  else if (!strcmp(family, "ipv6"))
    hints.ai_family = AF_INET6;
  else
    {
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Unknown address family %s", family);
      return -1;
    }

  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_socktype = SOCK_STREAM;
#ifdef AI_ADDRCONFIG
  hints.ai_flags = AI_ADDRCONFIG | AI_PASSIVE;
#else
  hints.ai_flags = AI_PASSIVE;
#endif

 redo_lookup_with_port:
  if ((res = getaddrinfo(host, port, &hints, &ai)) != 0 || !ai)
    {
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Failed to lookup host/port: \"%s:%s\": %s (%d)",
                      host ? host : "*", port, gai_strerror(res), res);
      return -1;
    }

  tmp = ai;
  while (tmp)
    {
      int fd = -1, *newlisten_fd;
      if ((fd = socket (tmp->ai_family, SOCK_STREAM, 0)) < 0)
        {
          dbus_set_error (error,
                          _dbus_error_from_errno (errno),
                         "Failed to open socket: %s",
                         _dbus_strerror (errno));
          goto failed;
        }
      _DBUS_ASSERT_ERROR_IS_CLEAR(error);

      if (bind (fd, (struct sockaddr*) tmp->ai_addr, tmp->ai_addrlen) == SOCKET_ERROR)
        {
          closesocket (fd);
          dbus_set_error (error, _dbus_error_from_errno (errno),
                          "Failed to bind socket \"%s:%s\": %s",
                          host ? host : "*", port, _dbus_strerror (errno));
          goto failed;
    }

      if (listen (fd, 30 /* backlog */) == SOCKET_ERROR)
        {
          closesocket (fd);
          dbus_set_error (error, _dbus_error_from_errno (errno),
                          "Failed to listen on socket \"%s:%s\": %s",
                          host ? host : "*", port, _dbus_strerror (errno));
          goto failed;
        }

      newlisten_fd = dbus_realloc(listen_fd, sizeof(int)*(nlisten_fd+1));
      if (!newlisten_fd)
    {
          closesocket (fd);
      dbus_set_error (error, _dbus_error_from_errno (errno),
                          "Failed to allocate file handle array: %s",
                          _dbus_strerror (errno));
          goto failed;
    }
      listen_fd = newlisten_fd;
      listen_fd[nlisten_fd] = fd;
      nlisten_fd++;

      if (!_dbus_string_get_length(retport))
        {
          /* If the user didn't specify a port, or used 0, then
             the kernel chooses a port. After the first address
             is bound to, we need to force all remaining addresses
             to use the same port */
          if (!port || !strcmp(port, "0"))
            {
              sockaddr_gen addr;
              socklen_t addrlen = sizeof(addr);
              char portbuf[10];

              if ((res = getsockname(fd, &addr.Address, &addrlen)) != 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                                  "Failed to resolve port \"%s:%s\": %s (%d)",
                                  host ? host : "*", port, gai_strerror(res), res);
                  goto failed;
                }
              snprintf( portbuf, sizeof( portbuf ) - 1, "%d", addr.AddressIn.sin_port );
              if (!_dbus_string_append(retport, portbuf))
                {
                  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
                  goto failed;
    }

              /* Release current address list & redo lookup */
              port = _dbus_string_get_const_data(retport);
              freeaddrinfo(ai);
              goto redo_lookup_with_port;
            }
          else
            {
              if (!_dbus_string_append(retport, port))
                {
                    dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
                    goto failed;
                }
            }
        }
  
      tmp = tmp->ai_next;
    }
  freeaddrinfo(ai);
  ai = NULL;

  if (!nlisten_fd)
    {
      errno = WSAEADDRINUSE;
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to bind socket \"%s:%s\": %s",
                      host ? host : "*", port, _dbus_strerror (errno));
      return -1;
    }

  sscanf(_dbus_string_get_const_data(retport), "%d", &port_num);
  _dbus_daemon_init(host, port_num);

  for (i = 0 ; i < nlisten_fd ; i++)
    {
      if (!_dbus_set_fd_nonblocking (listen_fd[i], error))
        {
          goto failed;
        }
    }

  *fds_p = listen_fd;

  return nlisten_fd;

 failed:
  if (ai)
    freeaddrinfo(ai);
  for (i = 0 ; i < nlisten_fd ; i++)
    closesocket (listen_fd[i]);
  dbus_free(listen_fd);
  return -1;
}


/**
 * Accepts a connection on a listening socket.
 * Handles EINTR for you.
 *
 * @param listen_fd the listen file descriptor
 * @returns the connection fd of the client, or -1 on error
 */
int
_dbus_accept  (int listen_fd)
{
  int client_fd;

 retry:
  client_fd = accept (listen_fd, NULL, NULL);

  if (DBUS_SOCKET_IS_INVALID (client_fd))
    {
      DBUS_SOCKET_SET_ERRNO ();
      if (errno == EINTR)
        goto retry;
    }

  _dbus_verbose ("client fd %d accepted\n", client_fd);
  
  return client_fd;
}




dbus_bool_t
_dbus_send_credentials_socket (int            handle,
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
_dbus_read_credentials_socket  (int              handle,
                                DBusCredentials *credentials,
                                DBusError       *error)
{
  int bytes_read = 0;
  DBusString buf;
  
  // could fail due too OOM
  if (_dbus_string_init(&buf))
    {
      bytes_read = _dbus_read_socket(handle, &buf, 1 );

      if (bytes_read > 0) 
        _dbus_verbose("got one zero byte from server");

      _dbus_string_free(&buf);
    }

  _dbus_credentials_add_from_current_process (credentials);
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

/*---------------- DBusCredentials ----------------------------------

/**
 * Adds the credentials corresponding to the given username.
 *
 * @param credentials credentials to fill in 
 * @param username the username
 * @returns #TRUE if the username existed and we got some credentials
 */
dbus_bool_t
_dbus_credentials_add_from_user (DBusCredentials  *credentials,
                                     const DBusString *username)
{
  return _dbus_credentials_add_windows_sid (credentials,
                    _dbus_string_get_const_data(username));
}

/**
 * Adds the credentials of the current process to the
 * passed-in credentials object.
 *
 * @param credentials credentials to add to
 * @returns #FALSE if no memory; does not properly roll back on failure, so only some credentials may have been added
 */

dbus_bool_t
_dbus_credentials_add_from_current_process (DBusCredentials *credentials)
{
  dbus_bool_t retval = FALSE;
  char *sid = NULL;

  if (!_dbus_getsid(&sid))
    goto failed;

  if (!_dbus_credentials_add_unix_pid(credentials, _dbus_getpid()))
    goto failed;

  if (!_dbus_credentials_add_windows_sid (credentials,sid))
    goto failed;

  retval = TRUE;
  goto end;
failed:
  retval = FALSE;
end:
  if (sid)
    LocalFree(sid);

  return retval;
}

/**
 * Append to the string the identity we would like to have when we
 * authenticate, on UNIX this is the current process UID and on
 * Windows something else, probably a Windows SID string.  No escaping
 * is required, that is done in dbus-auth.c. The username here
 * need not be anything human-readable, it can be the machine-readable
 * form i.e. a user id.
 * 
 * @param str the string to append to
 * @returns #FALSE on no memory
 * @todo to which class belongs this 
 */
dbus_bool_t
_dbus_append_user_from_current_process (DBusString *str)
{
  dbus_bool_t retval = FALSE;
  char *sid = NULL;

  if (!_dbus_getsid(&sid))
    return FALSE;

  retval = _dbus_string_append (str,sid);

  LocalFree(sid);
  return retval;
}

/**
 * Gets our process ID
 * @returns process ID
 */
dbus_pid_t
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
}


/* _dbus_read() is static on Windows, only used below in this file.
 */
static int
_dbus_read (int               fd,
            DBusString       *buffer,
            int               count)
{
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

 again:

  bytes_read = _read (fd, data, count);

  if (bytes_read < 0)
    {
      if (errno == EINTR)
        goto again;
      else
        {
          /* put length back (note that this doesn't actually realloc anything) */
          _dbus_string_set_length (buffer, start);
          return -1;
        }
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
  int fd;
  struct _stati64 sb;
  int orig_len;
  int total;
  const char *filename_c;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  filename_c = _dbus_string_get_const_data (filename);

  fd = _open (filename_c, O_RDONLY | O_BINARY);
  if (fd < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to open \"%s\": %s",
                      filename_c,
                      strerror (errno));
      return FALSE;
    }

  _dbus_verbose ("file %s fd %d opened\n", filename_c, fd);

  if (_fstati64 (fd, &sb) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to stat \"%s\": %s",
                      filename_c,
                      strerror (errno));

      _dbus_verbose ("fstat() failed: %s",
                     strerror (errno));

      _close (fd);

      return FALSE;
    }

  if (sb.st_size > _DBUS_ONE_MEGABYTE)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "File size %lu of \"%s\" is too large.",
                      (unsigned long) sb.st_size, filename_c);
      _close (fd);
      return FALSE;
    }

  total = 0;
  orig_len = _dbus_string_get_length (str);
  if (sb.st_size > 0 && S_ISREG (sb.st_mode))
    {
      int bytes_read;

      while (total < (int) sb.st_size)
        {
          bytes_read = _dbus_read (fd, str, sb.st_size - total);
          if (bytes_read <= 0)
            {
              dbus_set_error (error, _dbus_error_from_errno (errno),
                              "Error reading \"%s\": %s",
                              filename_c,
                              strerror (errno));

              _dbus_verbose ("read() failed: %s",
                             strerror (errno));

              _close (fd);
              _dbus_string_set_length (str, orig_len);
              return FALSE;
            }
          else
            total += bytes_read;
        }

      _close (fd);
      return TRUE;
    }
  else if (sb.st_size != 0)
    {
      _dbus_verbose ("Can only open regular files at the moment.\n");
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "\"%s\" is not a regular file",
                      filename_c);
      _close (fd);
      return FALSE;
    }
  else
    {
      _close (fd);
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
  int fd;
  int bytes_to_write;
  const char *filename_c;
  DBusString tmp_filename;
  const char *tmp_filename_c;
  int total;
  const char *str_c;
  dbus_bool_t need_unlink;
  dbus_bool_t retval;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  fd = -1;
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

  fd = _open (tmp_filename_c, O_WRONLY | O_BINARY | O_EXCL | O_CREAT,
              0600);
  if (fd < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Could not create %s: %s", tmp_filename_c,
                      strerror (errno));
      goto out;
    }

  _dbus_verbose ("tmp file %s fd %d opened\n", tmp_filename_c, fd);

  need_unlink = TRUE;

  total = 0;
  bytes_to_write = _dbus_string_get_length (str);
  str_c = _dbus_string_get_const_data (str);

  while (total < bytes_to_write)
    {
      int bytes_written;

      bytes_written = _write (fd, str_c + total, bytes_to_write - total);

      if (bytes_written <= 0)
        {
          dbus_set_error (error, _dbus_error_from_errno (errno),
                          "Could not write to %s: %s", tmp_filename_c,
                          strerror (errno));
          goto out;
        }

      total += bytes_written;
    }

  if (_close (fd) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Could not close file %s: %s",
                      tmp_filename_c, strerror (errno));

      goto out;
    }

  fd = -1;

  /* Unlike rename(), MoveFileEx() can replace existing files */
  if (MoveFileExA (tmp_filename_c, filename_c, MOVEFILE_REPLACE_EXISTING) < 0)
    {
      char *emsg = _dbus_win_error_string (GetLastError ());
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Could not rename %s to %s: %s",
                      tmp_filename_c, filename_c,
                      emsg);
      _dbus_win_free_error_string (emsg);

      goto out;
    }

  need_unlink = FALSE;

  retval = TRUE;

 out:
  /* close first, then unlink */

  if (fd >= 0)
    _close (fd);

  if (need_unlink && _unlink (tmp_filename_c) < 0)
    _dbus_verbose ("failed to unlink temp file %s: %s\n",
                   tmp_filename_c, strerror (errno));

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
  int fd;
  const char *filename_c;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  filename_c = _dbus_string_get_const_data (filename);

  fd = _open (filename_c, O_WRONLY | O_BINARY | O_EXCL | O_CREAT,
              0600);
  if (fd < 0)
    {
      dbus_set_error (error,
                      DBUS_ERROR_FAILED,
                      "Could not create file %s: %s\n",
                      filename_c,
                      strerror (errno));
      return FALSE;
    }

  _dbus_verbose ("exclusive file %s fd %d opened\n", filename_c, fd);

  if (_close (fd) < 0)
    {
      dbus_set_error (error,
                      DBUS_ERROR_FAILED,
                      "Could not close file %s: %s\n",
                      filename_c,
                      strerror (errno));
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

  if (!CreateDirectory (filename_c, NULL))
    {
      if (GetLastError () == ERROR_ALREADY_EXISTS)
        return TRUE;

      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Failed to create directory %s: %s\n",
                      filename_c, strerror (errno));
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
  int old_len;
  char *p;
  HCRYPTPROV hprov;

  old_len = _dbus_string_get_length (str);

  if (!_dbus_string_lengthen (str, n_bytes))
    return FALSE;

  p = _dbus_string_get_data_len (str, old_len, n_bytes);

  if (!CryptAcquireContext (&hprov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    return FALSE;

  if (!CryptGenRandom (hprov, n_bytes, p))
    {
      CryptReleaseContext (hprov, 0);
      return FALSE;
    }

  CryptReleaseContext (hprov, 0);

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
  static char buf[1000];

  if (tmpdir == NULL)
    {
      if (!GetTempPath (sizeof (buf), buf))
        strcpy (buf, "\\");

      tmpdir = buf;
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

  if (_unlink (filename_c) < 0)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Failed to delete file %s: %s\n",
                      filename_c, strerror (errno));
      return FALSE;
    }
  else
    return TRUE;
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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

    _DBUS_ZERO(context);
    context.ContextFlags = CONTEXT_FULL;

    SuspendThread(hThread);

    if (!GetThreadContext(hThread, &context))
    {
        DPRINTF("Couldn't get thread context (error %ld)\n", GetLastError());
        ResumeThread(hThread);
        return;
    }

    _DBUS_ZERO(sf);

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
#ifdef _DEBUG
static const char *cDBusDaemonAddressInfo = "DBusDaemonAddressInfoDebug";
#else
static const char *cDBusDaemonAddressInfo = "DBusDaemonAddressInfo";
#endif

void
_dbus_daemon_init(const char *host, dbus_uint32_t port)
{
  HANDLE lock;
  char *adr = NULL;
  char szUserName[64];
  DWORD dwUserNameSize = sizeof(szUserName);
  char szDBusDaemonMutex[128];
  char szDBusDaemonAddressInfo[128];
  char szAddress[128];
  DWORD ret;

  _dbus_assert(host);
  _dbus_assert(port);

  _snprintf(szAddress, sizeof(szAddress) - 1, "tcp:host=%s,port=%d", host, port);
  ret = GetUserName(szUserName, &dwUserNameSize);
  _dbus_assert(ret != 0);
  _snprintf(szDBusDaemonMutex, sizeof(szDBusDaemonMutex) - 1, "%s:%s",
            cDBusDaemonMutex, szUserName);
  _snprintf(szDBusDaemonAddressInfo, sizeof(szDBusDaemonAddressInfo) - 1, "%s:%s",
            cDBusDaemonAddressInfo, szUserName);

  // before _dbus_global_lock to keep correct lock/release order
  hDBusDaemonMutex = CreateMutex( NULL, FALSE, szDBusDaemonMutex );
  ret = WaitForSingleObject( hDBusDaemonMutex, 1000 );
  if ( ret != WAIT_OBJECT_0 ) {
    _dbus_warn("Could not lock mutex %s (return code %d). daemon already running?\n", szDBusDaemonMutex, ret );
    _dbus_assert( !"Could not lock mutex, daemon already running?" );
  }

  // sync _dbus_daemon_init, _dbus_daemon_uninit and _dbus_daemon_already_runs
  lock = _dbus_global_lock( cUniqueDBusInitMutex );

  // create shm
  hDBusSharedMem = CreateFileMapping( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                      0, strlen( szAddress ) + 1, szDBusDaemonAddressInfo );
  _dbus_assert( hDBusSharedMem );

  adr = MapViewOfFile( hDBusSharedMem, FILE_MAP_WRITE, 0, 0, 0 );

  _dbus_assert( adr );

  strcpy( adr, szAddress);

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
  char *adr;
  char szUserName[64];
  DWORD dwUserNameSize = sizeof(szUserName);
  char szDBusDaemonAddressInfo[128];
  int i;

  if( !GetUserName(szUserName, &dwUserNameSize) )
      return FALSE;
  _snprintf(szDBusDaemonAddressInfo, sizeof(szDBusDaemonAddressInfo) - 1, "%s:%s",
            cDBusDaemonAddressInfo, szUserName);

  // read shm
  for(i=0;i<20;++i) {
      // we know that dbus-daemon is available, so we wait until shm is available
      sharedMem = OpenFileMapping( FILE_MAP_READ, FALSE, szDBusDaemonAddressInfo );
      if( sharedMem == 0 )
          Sleep( 100 );
      if ( sharedMem != 0)
          break;
  }

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
#ifdef _DEBUG
  const char * daemon_name = "dbus-daemond.exe";
#else
  const char * daemon_name = "dbus-daemon.exe";
#endif

  mutex = _dbus_global_lock ( cDBusAutolaunchMutex );

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (_dbus_daemon_already_runs(address))
    {
        _dbus_verbose("found already running dbus daemon\n");
        retval = TRUE;
        goto out;
    }

  if (!SearchPathA(NULL, daemon_name, NULL, sizeof(dbus_exe_path), dbus_exe_path, &lpFile))
    {
      printf ("please add the path to %s to your PATH environment variable\n", daemon_name);
      printf ("or start the daemon manually\n\n");
      printf ("");
      goto out;
    }

  // Create process
  ZeroMemory( &si, sizeof(si) );
  si.cb = sizeof(si);
  ZeroMemory( &pi, sizeof(pi) );

  _snprintf(dbus_args, sizeof(dbus_args) - 1, "\"%s\" %s", dbus_exe_path,  " --session");

//  argv[i] = "--config-file=bus\\session.conf";
//  printf("create process \"%s\" %s\n", dbus_exe_path, dbus_args);
  if(CreateProcessA(dbus_exe_path, dbus_args, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {

      retval = _dbus_get_autolaunch_shm( address );
    }
  
  if (retval == FALSE)
    dbus_set_error_const (error, DBUS_ERROR_FAILED, "Failed to launch dbus-daemon");

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
#define DBUS_STANDARD_SYSTEM_SERVICEDIR "/dbus-1/system-services"

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

  if (!_dbus_string_append (&servicedir_path, DBUS_DATADIR _DBUS_PATH_SEPARATOR))
        goto oom;

  common_progs = _dbus_getenv ("CommonProgramFiles");

  if (common_progs != NULL)
    {
      if (!_dbus_string_append (&servicedir_path, common_progs))
        goto oom;

      if (!_dbus_string_append (&servicedir_path, _DBUS_PATH_SEPARATOR))
        goto oom;
    }

  if (!_dbus_split_paths_and_append (&servicedir_path, 
                               DBUS_STANDARD_SESSION_SERVICEDIR, 
                               dirs))
    goto oom;

  _dbus_string_free (&servicedir_path);  
  return TRUE;

 oom:
  _dbus_string_free (&servicedir_path);
  return FALSE;
}

/**
 * Returns the standard directories for a system bus to look for service
 * activation files
 *
 * On UNIX this should be the standard xdg freedesktop.org data directories:
 *
 * XDG_DATA_DIRS=${XDG_DATA_DIRS-/usr/local/share:/usr/share}
 *
 * and
 *
 * DBUS_DATADIR
 *
 * On Windows there is no system bus and this function can return nothing.
 *
 * @param dirs the directory list we are returning
 * @returns #FALSE on OOM
 */

dbus_bool_t
_dbus_get_standard_system_servicedirs (DBusList **dirs)
{
  *dirs = NULL;
  return TRUE;
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
  // no volatile argument with mingw
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
  // no volatile argument with mingw
  return InterlockedDecrement (&atomic->value) + 1;
}

#endif /* asserts or tests enabled */

/**
 * Called when the bus daemon is signaled to reload its configuration; any
 * caches should be nuked. Of course any caches that need explicit reload
 * are probably broken, but c'est la vie.
 *
 * 
 */
void
_dbus_flush_caches (void)
{

}

dbus_bool_t _dbus_windows_user_is_process_owner (const char *windows_sid)
{
    return TRUE;
}

/**
 * See if errno is EAGAIN or EWOULDBLOCK (this has to be done differently
 * for Winsock so is abstracted)
 *
 * @returns #TRUE if errno == EAGAIN or errno == EWOULDBLOCK
 */
dbus_bool_t
_dbus_get_is_errno_eagain_or_ewouldblock (void)
{
  return errno == EAGAIN || errno == EWOULDBLOCK;
}

/**
 * return the absolute path of the dbus installation 
 *
 * @param s buffer for installation path
 * @param len length of buffer
 * @returns #FALSE on failure
 */
dbus_bool_t 
_dbus_get_install_root(char *s, int len)
{
  char *p = NULL;
  int ret = GetModuleFileName(NULL,s,len);
  if ( ret == 0 
    || ret == len && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
      *s = '\0';
      return FALSE;
    }
  else if ((p = strstr(s,"\\bin\\")))
    {
      *(p+1)= '\0';
      return TRUE;
    }
  else
    {
      *s = '\0';
      return FALSE;
    }
}

/** 
  find config file either from installation or build root according to 
  the following path layout 
    install-root/
      bin/dbus-daemon[d].exe
      etc/<config-file>.conf 

    build-root/
      bin/dbus-daemon[d].exe
      bus/<config-file>.conf 
*/
dbus_bool_t 
_dbus_get_config_file_name(DBusString *config_file, char *s)
{
  char path[MAX_PATH*2];
  int path_size = sizeof(path);
  int len = 4 + strlen(s);

  if (!_dbus_get_install_root(path,path_size))
    return FALSE;

  if(len > sizeof(path)-2)
    return FALSE;
  strcat(path,"etc\\");
  strcat(path,s);
  if (_dbus_file_exists(path)) 
    {
      // find path from executable 
      if (!_dbus_string_append (config_file, path))
        return FALSE;
    }
  else 
    {
      if (!_dbus_get_install_root(path,path_size))
        return FALSE;
      if(len + strlen(path) > sizeof(path)-2)
        return FALSE;
      strcat(path,"bus\\");
      strcat(path,s);
  
      if (_dbus_file_exists(path)) 
        {
          if (!_dbus_string_append (config_file, path))
            return FALSE;
        }
    }
  return TRUE;
}    

/**
 * Append the absolute path of the system.conf file
 * (there is no system bus on Windows so this can just
 * return FALSE and print a warning or something)
 * 
 * @param str the string to append to
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_append_system_config_file (DBusString *str)
{
  return _dbus_get_config_file_name(str, "system.conf");
}

/**
 * Append the absolute path of the session.conf file.
 * 
 * @param str the string to append to
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_append_session_config_file (DBusString *str)
{
  return _dbus_get_config_file_name(str, "session.conf");
}

/* See comment in dbus-sysdeps-unix.c */
dbus_bool_t
_dbus_lookup_session_address (dbus_bool_t *supported,
                              DBusString  *address,
                              DBusError   *error)
{
  /* Probably fill this in with something based on COM? */
  *supported = FALSE;
  return TRUE;
}

/**
 * Appends the directory in which a keyring for the given credentials
 * should be stored.  The credentials should have either a Windows or
 * UNIX user in them.  The directory should be an absolute path.
 *
 * On UNIX the directory is ~/.dbus-keyrings while on Windows it should probably
 * be something else, since the dotfile convention is not normal on Windows.
 * 
 * @param directory string to append directory to
 * @param credentials credentials the directory should be for
 *  
 * @returns #FALSE on no memory
 */
dbus_bool_t
_dbus_append_keyring_directory_for_credentials (DBusString      *directory,
                                                DBusCredentials *credentials)
{
  DBusString homedir;
  DBusString dotdir;
  dbus_uid_t uid;
  const char *homepath;

  _dbus_assert (credentials != NULL);
  _dbus_assert (!_dbus_credentials_are_anonymous (credentials));
  
  if (!_dbus_string_init (&homedir))
    return FALSE;

  homepath = _dbus_getenv("HOMEPATH");
  if (homepath != NULL && *homepath != '\0')
    {
      _dbus_string_append(&homedir,homepath);
    }
  
#ifdef DBUS_BUILD_TESTS
  {
    const char *override;
    
    override = _dbus_getenv ("DBUS_TEST_HOMEDIR");
    if (override != NULL && *override != '\0')
      {
        _dbus_string_set_length (&homedir, 0);
        if (!_dbus_string_append (&homedir, override))
          goto failed;

        _dbus_verbose ("Using fake homedir for testing: %s\n",
                       _dbus_string_get_const_data (&homedir));
      }
    else
      {
        static dbus_bool_t already_warned = FALSE;
        if (!already_warned)
          {
            _dbus_warn ("Using your real home directory for testing, set DBUS_TEST_HOMEDIR to avoid\n");
            already_warned = TRUE;
          }
      }
  }
#endif

  _dbus_string_init_const (&dotdir, ".dbus-keyrings");
  if (!_dbus_concat_dir_and_file (&homedir,
                                  &dotdir))
    goto failed;
  
  if (!_dbus_string_copy (&homedir, 0,
                          directory, _dbus_string_get_length (directory))) {
    goto failed;
  }

  _dbus_string_free (&homedir);
  return TRUE;
  
 failed: 
  _dbus_string_free (&homedir);
  return FALSE;
}

/** @} end of sysdeps-win */
/* tests in dbus-sysdeps-util.c */

