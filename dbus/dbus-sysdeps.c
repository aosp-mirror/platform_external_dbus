/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-sysdeps.c Wrappers around system/libc features (internal to D-BUS implementation)
 * 
 * Copyright (C) 2002  Red Hat, Inc.
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

#include "dbus-internals.h"
#include "dbus-sysdeps.h"
#include "dbus-threads.h"
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <dirent.h>
#include <sys/un.h>
#include <pwd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>

#ifdef HAVE_WRITEV
#include <sys/uio.h>
#endif
#ifdef HAVE_POLL
#include <sys/poll.h>
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

/**
 * @addtogroup DBusInternalsUtils
 * @{
 */
/**
 * Aborts the program with SIGABRT (dumping core).
 */
void
_dbus_abort (void)
{
  abort ();
  _exit (1); /* in case someone manages to ignore SIGABRT */
}

/**
 * Wrapper for setenv().
 *
 * @param varname name of environment variable
 * @param value value of environment variable
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_setenv (const char *varname, const char *value)
{
  return (setenv (varname, value, TRUE) == 0);
}

/**
 * Wrapper for getenv().
 *
 * @param varname name of environment variable
 * @returns value of environment variable or #NULL if unset
 */
const char*
_dbus_getenv (const char *varname)
{  
  return getenv (varname);
}

/**
 * Thin wrapper around the read() system call that appends
 * the data it reads to the DBusString buffer. It appends
 * up to the given count, and returns the same value
 * and same errno as read(). The only exception is that
 * _dbus_read() handles EINTR for you.
 *
 * @param fd the file descriptor to read from
 * @param buffer the buffer to append data to
 * @param count the amount of data to read
 * @returns the number of bytes read or -1
 */
int
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

  _dbus_string_get_data_len (buffer, &data, start, count);

 again:
  
  bytes_read = read (fd, data, count);

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
_dbus_write (int               fd,
             const DBusString *buffer,
             int               start,
             int               len)
{
  const char *data;
  int bytes_written;
  
  _dbus_string_get_const_data_len (buffer, &data, start, len);
  
 again:

  bytes_written = write (fd, data, len);

  if (bytes_written < 0 && errno == EINTR)
    goto again;

#if 0
  if (bytes_written > 0)
    _dbus_verbose_bytes_of_string (buffer, start, bytes_written);
#endif
  
  return bytes_written;
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
_dbus_write_two (int               fd,
                 const DBusString *buffer1,
                 int               start1,
                 int               len1,
                 const DBusString *buffer2,
                 int               start2,
                 int               len2)
{
  _dbus_assert (buffer1 != NULL);
  _dbus_assert (start1 >= 0);
  _dbus_assert (start2 >= 0);
  _dbus_assert (len1 >= 0);
  _dbus_assert (len2 >= 0);
  
#ifdef HAVE_WRITEV
  {
    struct iovec vectors[2];
    const char *data1;
    const char *data2;
    int bytes_written;

    _dbus_string_get_const_data_len (buffer1, &data1, start1, len1);

    if (buffer2 != NULL)
      _dbus_string_get_const_data_len (buffer2, &data2, start2, len2);
    else
      {
        data2 = NULL;
        start2 = 0;
        len2 = 0;
      }
   
    vectors[0].iov_base = (char*) data1;
    vectors[0].iov_len = len1;
    vectors[1].iov_base = (char*) data2;
    vectors[1].iov_len = len2;

  again:
   
    bytes_written = writev (fd,
                            vectors,
                            data2 ? 2 : 1);

    if (bytes_written < 0 && errno == EINTR)
      goto again;
   
    return bytes_written;
  }
#else /* HAVE_WRITEV */
  {
    int ret1;
    
    ret1 = _dbus_write (fd, buffer1, start1, len1);
    if (ret1 == len1 && buffer2 != NULL)
      {
        ret2 = _dbus_write (fd, buffer2, start2, len2);
        if (ret2 < 0)
          ret2 = 0; /* we can't report an error as the first write was OK */
       
        return ret1 + ret2;
      }
    else
      return ret1;
  }
#endif /* !HAVE_WRITEV */   
}

/**
 * Creates a socket and connects it to the UNIX domain socket at the
 * given path.  The connection fd is returned, and is set up as
 * nonblocking.
 *
 * @param path the path to UNIX domain socket
 * @param result return location for error code
 * @returns connection file descriptor or -1 on error
 */
int
_dbus_connect_unix_socket (const char     *path,
                           DBusResultCode *result)
{
  int fd;
  struct sockaddr_un addr;  
  
  fd = socket (AF_LOCAL, SOCK_STREAM, 0);
  
  if (fd < 0)
    {
      dbus_set_result (result,
                       _dbus_result_from_errno (errno));
      
      _dbus_verbose ("Failed to create socket: %s\n",
                     _dbus_strerror (errno)); 
      
      return -1;
    }

  _DBUS_ZERO (addr);
  addr.sun_family = AF_LOCAL;
  strncpy (addr.sun_path, path, _DBUS_MAX_SUN_PATH_LENGTH);
  addr.sun_path[_DBUS_MAX_SUN_PATH_LENGTH] = '\0';
  
  if (connect (fd, (struct sockaddr*) &addr, sizeof (addr)) < 0)
    {      
      dbus_set_result (result,
                       _dbus_result_from_errno (errno));

      _dbus_verbose ("Failed to connect to socket %s: %s\n",
                     path, _dbus_strerror (errno));

      close (fd);
      fd = -1;
      
      return -1;
    }

  if (!_dbus_set_fd_nonblocking (fd, result))
    {
      close (fd);
      fd = -1;

      return -1;
    }

  return fd;
}

/**
 * Creates a socket and binds it to the given path,
 * then listens on the socket. The socket is
 * set to be nonblocking. 
 *
 * @param path the socket name
 * @param result return location for errors
 * @returns the listening file descriptor or -1 on error
 */
int
_dbus_listen_unix_socket (const char     *path,
                          DBusResultCode *result)
{
  int listen_fd;
  struct sockaddr_un addr;

  listen_fd = socket (AF_LOCAL, SOCK_STREAM, 0);
  
  if (listen_fd < 0)
    {
      dbus_set_result (result, _dbus_result_from_errno (errno));
      _dbus_verbose ("Failed to create socket \"%s\": %s\n",
                     path, _dbus_strerror (errno));
      return -1;
    }

  _DBUS_ZERO (addr);
  addr.sun_family = AF_LOCAL;
  strncpy (addr.sun_path, path, _DBUS_MAX_SUN_PATH_LENGTH);
  addr.sun_path[_DBUS_MAX_SUN_PATH_LENGTH] = '\0';
  
  if (bind (listen_fd, (struct sockaddr*) &addr, SUN_LEN (&addr)) < 0)
    {
      dbus_set_result (result, _dbus_result_from_errno (errno));
      _dbus_verbose ("Failed to bind socket \"%s\": %s\n",
                     path, _dbus_strerror (errno));
      close (listen_fd);
      return -1;
    }

  if (listen (listen_fd, 30 /* backlog */) < 0)
    {
      dbus_set_result (result, _dbus_result_from_errno (errno));      
      _dbus_verbose ("Failed to listen on socket \"%s\": %s\n",
                     path, _dbus_strerror (errno));
      close (listen_fd);
      return -1;
    }

  if (!_dbus_set_fd_nonblocking (listen_fd, result))
    {
      close (listen_fd);
      return -1;
    }
  
  return listen_fd;
}

/**
 * Creates a socket and connects to a socket at the given host 
 * and port. The connection fd is returned, and is set up as
 * nonblocking.
 *
 * @param host the host name to connect to
 * @param port the prot to connect to
 * @param result return location for error code
 * @returns connection file descriptor or -1 on error
 */
int
_dbus_connect_tcp_socket (const char     *host,
                          dbus_uint32_t   port,
                          DBusResultCode *result)
{
  int fd;
  struct sockaddr_in addr;
  struct hostent *he;
  struct in_addr *haddr;
  
  fd = socket (AF_INET, SOCK_STREAM, 0);
  
  if (fd < 0)
    {
      dbus_set_result (result,
                       _dbus_result_from_errno (errno));
      
      _dbus_verbose ("Failed to create socket: %s\n",
                     _dbus_strerror (errno)); 
      
      return -1;
    }

  if (host == NULL)
    host = "localhost";

  he = gethostbyname (host);
  if (he == NULL) 
    {
      dbus_set_result (result,
                       _dbus_result_from_errno (errno));
      _dbus_verbose ("Failed to lookup hostname: %s\n",
                     host);
      return -1;
    }
  
  haddr = ((struct in_addr *) (he->h_addr_list)[0]);

  _DBUS_ZERO (addr);
  memcpy (&addr.sin_addr, haddr, sizeof(struct in_addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons (port);
  
  if (connect (fd, (struct sockaddr*) &addr, sizeof (addr)) < 0)
    {      
      dbus_set_result (result,
                       _dbus_result_from_errno (errno));

      _dbus_verbose ("Failed to connect to socket %s: %s:%d\n",
                     host, port, _dbus_strerror (errno));

      close (fd);
      fd = -1;
      
      return -1;
    }

  if (!_dbus_set_fd_nonblocking (fd, result))
    {
      close (fd);
      fd = -1;

      return -1;
    }

  return fd;
}

/**
 * Creates a socket and binds it to the given path,
 * then listens on the socket. The socket is
 * set to be nonblocking. 
 *
 * @param host the host name to listen on
 * @param port the prot to listen on
 * @param result return location for errors
 * @returns the listening file descriptor or -1 on error
 */
int
_dbus_listen_tcp_socket (const char     *host,
                         dbus_uint32_t   port,
                         DBusResultCode *result)
{
  int listen_fd;
  struct sockaddr_in addr;
  struct hostent *he;
  struct in_addr *haddr;
  
  listen_fd = socket (AF_INET, SOCK_STREAM, 0);
  
  if (listen_fd < 0)
    {
      dbus_set_result (result, _dbus_result_from_errno (errno));
      _dbus_verbose ("Failed to create socket \"%s:%d\": %s\n",
                     host, port, _dbus_strerror (errno));
      return -1;
    }

  if (host == NULL)
    host = "localhost";
  
  he = gethostbyname (host);
  if (he == NULL) 
    {
      dbus_set_result (result,
                       _dbus_result_from_errno (errno));
      _dbus_verbose ("Failed to lookup hostname: %s\n",
                     host);
      return -1;
    }
  
  haddr = ((struct in_addr *) (he->h_addr_list)[0]);

  _DBUS_ZERO (addr);
  memcpy (&addr.sin_addr, haddr, sizeof (struct in_addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons (port);

  if (bind (listen_fd, (struct sockaddr*) &addr, sizeof (struct sockaddr)))
    {
      dbus_set_result (result, _dbus_result_from_errno (errno));
      _dbus_verbose ("Failed to bind socket \"%s:%d\": %s\n",
                     host, port, _dbus_strerror (errno));
      close (listen_fd);
      return -1;
    }

  if (listen (listen_fd, 30 /* backlog */) < 0)
    {
      dbus_set_result (result, _dbus_result_from_errno (errno));      
      _dbus_verbose ("Failed to listen on socket \"%s:%d\": %s\n",
                     host, port, _dbus_strerror (errno));
      close (listen_fd);
      return -1;
    }

  if (!_dbus_set_fd_nonblocking (listen_fd, result))
    {
      close (listen_fd);
      return -1;
    }
  
  return listen_fd;
}

/* try to read a single byte and return #TRUE if we read it
 * and it's equal to nul.
 */
static dbus_bool_t
read_credentials_byte (int             client_fd,
                       DBusResultCode *result)
{
  char buf[1];
  int bytes_read;

 again:
  bytes_read = read (client_fd, buf, 1);
  if (bytes_read < 0)
    {
      if (errno == EINTR)
        goto again;
      else
        {
          dbus_set_result (result, _dbus_result_from_errno (errno));      
          _dbus_verbose ("Failed to read credentials byte: %s\n",
                         _dbus_strerror (errno));
          return FALSE;
        }
    }
  else if (bytes_read == 0)
    {
      dbus_set_result (result, DBUS_RESULT_IO_ERROR);
      _dbus_verbose ("EOF reading credentials byte\n");
      return FALSE;
    }
  else
    {
      _dbus_assert (bytes_read == 1);

      if (buf[0] != '\0')
        {
          dbus_set_result (result, DBUS_RESULT_FAILED);
          _dbus_verbose ("Credentials byte was not nul\n");
          return FALSE;
        }

      _dbus_verbose ("read credentials byte\n");
      
      return TRUE;
    }
}

static dbus_bool_t
write_credentials_byte (int             server_fd,
                        DBusResultCode *result)
{
  int bytes_written;
  char buf[1] = { '\0' };
  
 again:

  bytes_written = write (server_fd, buf, 1);

  if (bytes_written < 0 && errno == EINTR)
    goto again;

  if (bytes_written < 0)
    {
      dbus_set_result (result, _dbus_result_from_errno (errno));      
      _dbus_verbose ("Failed to write credentials byte: %s\n",
                     _dbus_strerror (errno));
      return FALSE;
    }
  else if (bytes_written == 0)
    {
      dbus_set_result (result, DBUS_RESULT_IO_ERROR);
      _dbus_verbose ("wrote zero bytes writing credentials byte\n");
      return FALSE;
    }
  else
    {
      _dbus_assert (bytes_written == 1);
      _dbus_verbose ("wrote credentials byte\n");
      return TRUE;
    }
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
 * @param result location to store result code
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_read_credentials_unix_socket  (int              client_fd,
                                     DBusCredentials *credentials,
                                     DBusResultCode  *result)
{
  credentials->pid = -1;
  credentials->uid = -1;
  credentials->gid = -1;
  
  if (read_credentials_byte (client_fd, result))
    {
#ifdef SO_PEERCRED
      struct ucred cr;   
      int cr_len = sizeof (cr);
   
      if (getsockopt (client_fd, SOL_SOCKET, SO_PEERCRED, &cr, &cr_len) == 0 &&
          cr_len == sizeof (cr))
        {
          credentials->pid = cr.pid;
          credentials->uid = cr.uid;
          credentials->gid = cr.gid;
          _dbus_verbose ("Got credentials pid %d uid %d gid %d\n",
                         credentials->pid,
                         credentials->uid,
                         credentials->gid);
        }
      else
        {
          _dbus_verbose ("Failed to getsockopt() credentials, returned len %d/%d: %s\n",
                         cr_len, (int) sizeof (cr), _dbus_strerror (errno));
        }
#else /* !SO_PEERCRED */
      _dbus_verbose ("Socket credentials not supported on this OS\n");
#endif

      return TRUE;
    }
  else
    return FALSE;
}

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
 * @param result return location for error code
 * @returns #TRUE if the byte was sent
 */
dbus_bool_t
_dbus_send_credentials_unix_socket  (int              server_fd,
                                     DBusResultCode  *result)
{
  if (write_credentials_byte (server_fd, result))
    return TRUE;
  else
    return FALSE;
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
  
  if (client_fd < 0)
    {
      if (errno == EINTR)
        goto retry;
    }
  
  return client_fd;
}

/** @} */

/**
 * @addtogroup DBusString
 *
 * @{
 */
/**
 * Appends an integer to a DBusString.
 * 
 * @param str the string
 * @param value the integer value
 * @returns #FALSE if not enough memory or other failure.
 */
dbus_bool_t
_dbus_string_append_int (DBusString *str,
                         long        value)
{
  /* this calculation is from comp.lang.c faq */
#define MAX_LONG_LEN ((sizeof (long) * 8 + 2) / 3 + 1)  /* +1 for '-' */
  int orig_len;
  int i;
  char *buf;
  
  orig_len = _dbus_string_get_length (str);

  if (!_dbus_string_lengthen (str, MAX_LONG_LEN))
    return FALSE;

  _dbus_string_get_data_len (str, &buf, orig_len, MAX_LONG_LEN);

  snprintf (buf, MAX_LONG_LEN, "%ld", value);

  i = 0;
  while (*buf)
    {
      ++buf;
      ++i;
    }
  
  _dbus_string_shorten (str, MAX_LONG_LEN - i);
  
  return TRUE;
}

/**
 * Appends an unsigned integer to a DBusString.
 * 
 * @param str the string
 * @param value the integer value
 * @returns #FALSE if not enough memory or other failure.
 */
dbus_bool_t
_dbus_string_append_uint (DBusString    *str,
                          unsigned long  value)
{
  /* this is wrong, but definitely on the high side. */
#define MAX_ULONG_LEN (MAX_LONG_LEN * 2)
  int orig_len;
  int i;
  char *buf;
  
  orig_len = _dbus_string_get_length (str);

  if (!_dbus_string_lengthen (str, MAX_ULONG_LEN))
    return FALSE;

  _dbus_string_get_data_len (str, &buf, orig_len, MAX_ULONG_LEN);

  snprintf (buf, MAX_ULONG_LEN, "%lu", value);

  i = 0;
  while (*buf)
    {
      ++buf;
      ++i;
    }
  
  _dbus_string_shorten (str, MAX_ULONG_LEN - i);
  
  return TRUE;
}

/**
 * Appends a double to a DBusString.
 * 
 * @param str the string
 * @param value the floating point value
 * @returns #FALSE if not enough memory or other failure.
 */
dbus_bool_t
_dbus_string_append_double (DBusString *str,
                            double      value)
{
#define MAX_DOUBLE_LEN 64 /* this is completely made up :-/ */
  int orig_len;
  char *buf;
  int i;
  
  orig_len = _dbus_string_get_length (str);

  if (!_dbus_string_lengthen (str, MAX_DOUBLE_LEN))
    return FALSE;

  _dbus_string_get_data_len (str, &buf, orig_len, MAX_DOUBLE_LEN);

  snprintf (buf, MAX_LONG_LEN, "%g", value);

  i = 0;
  while (*buf)
    {
      ++buf;
      ++i;
    }
  
  _dbus_string_shorten (str, MAX_DOUBLE_LEN - i);
  
  return TRUE;
}

/**
 * Parses an integer contained in a DBusString. Either return parameter
 * may be #NULL if you aren't interested in it. The integer is parsed
 * and stored in value_return. Return parameters are not initialized
 * if the function returns #FALSE.
 *
 * @param str the string
 * @param start the byte index of the start of the integer
 * @param value_return return location of the integer value or #NULL
 * @param end_return return location of the end of the integer, or #NULL
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_string_parse_int (const DBusString *str,
                        int               start,
                        long             *value_return,
                        int              *end_return)
{
  long v;
  const char *p;
  char *end;

  _dbus_string_get_const_data_len (str, &p, start,
                                   _dbus_string_get_length (str) - start);

  end = NULL;
  errno = 0;
  v = strtol (p, &end, 0);
  if (end == NULL || end == p || errno != 0)
    return FALSE;

  if (value_return)
    *value_return = v;
  if (end_return)
    *end_return = (end - p);

  return TRUE;
}

/**
 * Parses a floating point number contained in a DBusString. Either
 * return parameter may be #NULL if you aren't interested in it. The
 * integer is parsed and stored in value_return. Return parameters are
 * not initialized if the function returns #FALSE.
 *
 * @todo this function is currently locale-dependent. Should
 * ask alexl to relicense g_ascii_strtod() code and put that in
 * here instead, so it's locale-independent.
 *
 * @param str the string
 * @param start the byte index of the start of the float
 * @param value_return return location of the float value or #NULL
 * @param end_return return location of the end of the float, or #NULL
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_string_parse_double (const DBusString *str,
                           int               start,
                           double           *value_return,
                           int              *end_return)
{
  double v;
  const char *p;
  char *end;

  _dbus_warn ("_dbus_string_parse_double() needs to be made locale-independent\n");
  
  _dbus_string_get_const_data_len (str, &p, start,
                                   _dbus_string_get_length (str) - start);

  end = NULL;
  errno = 0;
  v = strtod (p, &end);
  if (end == NULL || end == p || errno != 0)
    return FALSE;

  if (value_return)
    *value_return = v;
  if (end_return)
    *end_return = (end - p);

  return TRUE;
}

/** @} */ /* DBusString group */

/**
 * @addtogroup DBusInternalsUtils
 * @{
 */

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
  const char *username_c_str;
  
  credentials->pid = -1;
  credentials->uid = -1;
  credentials->gid = -1;

  _dbus_string_get_const_data (username, &username_c_str);
  
#ifdef HAVE_GETPWNAM_R
  {
    struct passwd *p;
    int result;
    char buf[1024];
    struct passwd p_str;

    p = NULL;
    result = getpwnam_r (username_c_str, &p_str, buf, sizeof (buf),
                         &p);

    if (result == 0 && p == &p_str)
      {
        credentials->uid = p->pw_uid;
        credentials->gid = p->pw_gid;

        _dbus_verbose ("Username %s has uid %d gid %d\n",
                       username_c_str, credentials->uid, credentials->gid);
        return TRUE;
      }
    else
      {
        _dbus_verbose ("User %s unknown\n", username_c_str);
        return FALSE;
      }
  }
#else /* ! HAVE_GETPWNAM_R */
  {
    /* I guess we're screwed on thread safety here */
    struct passwd *p;

    p = getpwnam (username_c_str);

    if (p != NULL)
      {
        credentials->uid = p->pw_uid;
        credentials->gid = p->pw_gid;

        _dbus_verbose ("Username %s has uid %d gid %d\n",
                       username_c_str, credentials->uid, credentials->gid);
        return TRUE;
      }
    else
      {
        _dbus_verbose ("User %s unknown\n", username_c_str);
        return FALSE;
      }
  }
#endif  
}

/**
 * Gets credentials from a UID string. (Parses a string to a UID
 * and converts to a DBusCredentials.)
 *
 * @param uid_str the UID in string form
 * @param credentials credentials to fill in
 * @returns #TRUE if successfully filled in some credentials
 */
dbus_bool_t
_dbus_credentials_from_uid_string (const DBusString      *uid_str,
                                   DBusCredentials       *credentials)
{
  int end;
  long uid;

  credentials->pid = -1;
  credentials->uid = -1;
  credentials->gid = -1;
  
  if (_dbus_string_get_length (uid_str) == 0)
    {
      _dbus_verbose ("UID string was zero length\n");
      return FALSE;
    }

  uid = -1;
  end = 0;
  if (!_dbus_string_parse_int (uid_str, 0, &uid,
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

  credentials->uid = uid;

  return TRUE;
}

/**
 * Gets the credentials of the current process.
 *
 * @param credentials credentials to fill in.
 */
void
_dbus_credentials_from_current_process (DBusCredentials *credentials)
{
  credentials->pid = getpid ();
  credentials->uid = getuid ();
  credentials->gid = getgid ();
}

/**
 * Checks whether the provided_credentials are allowed to log in
 * as the expected_credentials.
 *
 * @param expected_credentials credentials we're trying to log in as
 * @param provided_credentials credentials we have
 * @returns #TRUE if we can log in
 */
dbus_bool_t
_dbus_credentials_match (const DBusCredentials *expected_credentials,
                         const DBusCredentials *provided_credentials)
{
  if (provided_credentials->uid < 0)
    return FALSE;
  else if (expected_credentials->uid < 0)
    return FALSE;
  else if (provided_credentials->uid == 0)
    return TRUE;
  else if (provided_credentials->uid == expected_credentials->uid)
    return TRUE;
  else
    return FALSE;
}

/**
 * Appends the uid of the current process to the given string.
 *
 * @param str the string to append to
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_string_append_our_uid (DBusString *str)
{
  return _dbus_string_append_int (str, getuid ());
}


static DBusMutex *atomic_lock = NULL;
/**
 * Initializes the global mutex for the fallback implementation
 * of atomic integers.
 *
 * @returns the mutex
 */
DBusMutex *
_dbus_atomic_init_lock (void)
{
  atomic_lock = dbus_mutex_new ();
  return atomic_lock;
}

/**
 * Atomically increments an integer
 *
 * @param atomic pointer to the integer to increment
 * @returns the value after incrementing
 *
 * @todo implement arch-specific faster atomic ops
 */
dbus_atomic_t
_dbus_atomic_inc (dbus_atomic_t *atomic)
{
  dbus_atomic_t res;
  
  dbus_mutex_lock (atomic_lock);
  *atomic += 1;
  res = *atomic;
  dbus_mutex_unlock (atomic_lock);
  return res;
}

/**
 * Atomically decrement an integer
 *
 * @param atomic pointer to the integer to decrement
 * @returns the value after decrementing
 *
 * @todo implement arch-specific faster atomic ops
 */
dbus_atomic_t
_dbus_atomic_dec (dbus_atomic_t *atomic)
{
  dbus_atomic_t res;
  
  dbus_mutex_lock (atomic_lock);
  *atomic -= 1;
  res = *atomic;
  dbus_mutex_unlock (atomic_lock);
  return res;
}

/**
 * Wrapper for poll().
 *
 * @todo need a fallback implementation using select()
 *
 * @param fds the file descriptors to poll
 * @param n_fds number of descriptors in the array
 * @param timeout_milliseconds timeout or -1 for infinite
 * @returns numbers of fds with revents, or <0 on error
 */
int
_dbus_poll (DBusPollFD *fds,
            int         n_fds,
            int         timeout_milliseconds)
{
#ifdef HAVE_POLL
  /* This big thing is a constant expression and should get optimized
   * out of existence. So it's more robust than a configure check at
   * no cost.
   */
  if (_DBUS_POLLIN == POLLIN &&
      _DBUS_POLLPRI == POLLPRI &&
      _DBUS_POLLOUT == POLLOUT &&
      _DBUS_POLLERR == POLLERR &&
      _DBUS_POLLHUP == POLLHUP &&
      _DBUS_POLLNVAL == POLLNVAL &&
      sizeof (DBusPollFD) == sizeof (struct pollfd) &&
      _DBUS_STRUCT_OFFSET (DBusPollFD, fd) ==
      _DBUS_STRUCT_OFFSET (struct pollfd, fd) &&
      _DBUS_STRUCT_OFFSET (DBusPollFD, events) ==
      _DBUS_STRUCT_OFFSET (struct pollfd, events) &&
      _DBUS_STRUCT_OFFSET (DBusPollFD, revents) ==
      _DBUS_STRUCT_OFFSET (struct pollfd, revents))
    {
      return poll ((struct pollfd*) fds,
                   n_fds, 
                   timeout_milliseconds);
    }
  else
    {
      /* We have to convert the DBusPollFD to an array of
       * struct pollfd, poll, and convert back.
       */
      _dbus_warn ("didn't implement poll() properly for this system yet\n");
      return -1;
    }
#else /* ! HAVE_POLL */

  fd_set read_set, write_set, err_set;
  int max_fd;
  int i;
  struct timeval tv;
  int ready;
  
  FD_ZERO (&read_set);
  FD_ZERO (&write_set);
  FD_ZERO (&err_set);

  for (i = 0; i < n_fds; i++)
    {
      DBusPollFD f = fds[i];

      if (f.events & _DBUS_POLLIN)
	FD_SET (f.fd, &read_set);

      if (f.events & _DBUS_POLLOUT)
	FD_SET (f.fd, &write_set);

      FD_SET (f.fd, &err_set);

      max_fd = MAX (max_fd, f.fd);
    }
    
  tv.tv_sec = timeout_milliseconds / 1000;
  tv.tv_usec = (timeout_milliseconds % 1000) * 1000;

  ready = select (max_fd + 1, &read_set, &write_set, &err_set, &tv);

  if (ready > 0)
    {
      for (i = 0; i < n_fds; i++)
	{
	  DBusPollFD f = fds[i];

	  f.revents = 0;

	  if (FD_ISSET (f.fd, &read_set))
	    f.revents |= _DBUS_POLLIN;

	  if (FD_ISSET (f.fd, &write_set))
	    f.revents |= _DBUS_POLLOUT;

	  if (FD_ISSET (f.fd, &err_set))
	    f.revents |= _DBUS_POLLERR;
	}
    }

  return ready;
#endif
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
#ifdef HAVE_NANOSLEEP
  struct timespec req;
  struct timespec rem;

  req.tv_sec = milliseconds / MILLISECONDS_PER_SECOND;
  req.tv_nsec = (milliseconds % MILLISECONDS_PER_SECOND) * NANOSECONDS_PER_MILLISECOND;
  rem.tv_sec = 0;
  rem.tv_nsec = 0;

  while (nanosleep (&req, &rem) < 0 && errno == EINTR)
    req = rem;
#elif defined (HAVE_USLEEP)
  usleep (milliseconds * MICROSECONDS_PER_MILLISECOND);
#else /* ! HAVE_USLEEP */
  sleep (MAX (milliseconds / 1000, 1));
#endif
}

/**
 * Get current time, as in gettimeofday().
 *
 * @param tv_sec return location for number of seconds
 * @param tv_usec return location for number of microseconds (thousandths)
 */
void
_dbus_get_current_time (long *tv_sec,
                        long *tv_usec)
{
  struct timeval t;

  gettimeofday (&t, NULL);

  if (tv_sec)
    *tv_sec = t.tv_sec;
  if (tv_usec)
    *tv_usec = t.tv_usec;
}

/**
 * Appends the contents of the given file to the string,
 * returning result code. At the moment, won't open a file
 * more than a megabyte in size.
 *
 * @param str the string to append to
 * @param filename filename to load
 * @returns result
 */
DBusResultCode
_dbus_file_get_contents (DBusString       *str,
                         const DBusString *filename)
{
  int fd;
  struct stat sb;
  int orig_len;
  int total;
  const char *filename_c;

  _dbus_string_get_const_data (filename, &filename_c);
  
  /* O_BINARY useful on Cygwin */
  fd = open (filename_c, O_RDONLY | O_BINARY);
  if (fd < 0)
    return _dbus_result_from_errno (errno);

  if (fstat (fd, &sb) < 0)
    {
      DBusResultCode result;      

      result = _dbus_result_from_errno (errno); /* prior to close() */

      _dbus_verbose ("fstat() failed: %s",
                     _dbus_strerror (errno));
      
      close (fd);
      
      return result;
    }

  if (sb.st_size > _DBUS_ONE_MEGABYTE)
    {
      _dbus_verbose ("File size %lu is too large.\n",
                     (unsigned long) sb.st_size);
      close (fd);
      return DBUS_RESULT_FAILED;
    }
  
  total = 0;
  orig_len = _dbus_string_get_length (str);
  if (sb.st_size > 0 && S_ISREG (sb.st_mode))
    {
      int bytes_read;

      while (total < (int) sb.st_size)
        {
          bytes_read = _dbus_read (fd, str,
                                   sb.st_size - total);
          if (bytes_read <= 0)
            {
              DBusResultCode result;
              
              result = _dbus_result_from_errno (errno); /* prior to close() */

              _dbus_verbose ("read() failed: %s",
                             _dbus_strerror (errno));
              
              close (fd);
              _dbus_string_set_length (str, orig_len);
              return result;
            }
          else
            total += bytes_read;
        }

      close (fd);
      return DBUS_RESULT_SUCCESS;
    }
  else if (sb.st_size != 0)
    {
      _dbus_verbose ("Can only open regular files at the moment.\n");
      close (fd);
      return DBUS_RESULT_FAILED;
    }
  else
    {
      close (fd);
      return DBUS_RESULT_SUCCESS;
    }
}

/**
 * Writes a string out to a file.
 *
 * @param str the string to write out
 * @param filename the file to save string to
 * @returns result code
 */
DBusResultCode
_dbus_string_save_to_file (const DBusString *str,
                           const DBusString *filename)
{
  int fd;
  int bytes_to_write;
  const char *filename_c;
  int total;

  _dbus_string_get_const_data (filename, &filename_c);
  
  fd = open (filename_c, O_WRONLY | O_BINARY | O_EXCL | O_CREAT,
             0600);
  if (fd < 0)
    return _dbus_result_from_errno (errno);

  total = 0;
  bytes_to_write = _dbus_string_get_length (str);

  while (total < bytes_to_write)
    {
      int bytes_written;

      bytes_written = _dbus_write (fd, str, total,
                                   bytes_to_write - total);

      if (bytes_written <= 0)
        {
          DBusResultCode result;
          
          result = _dbus_result_from_errno (errno); /* prior to close() */
          
          _dbus_verbose ("write() failed: %s",
                         _dbus_strerror (errno));
          
          close (fd);          
          return result;
        }

      total += bytes_written;
    }

  close (fd);
  return DBUS_RESULT_SUCCESS;
}

/**
 * Appends the given filename to the given directory.
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
  
  dir_ends_in_slash = '/' == _dbus_string_get_byte (dir,
                                                    _dbus_string_get_length (dir) - 1);

  file_starts_with_slash = '/' == _dbus_string_get_byte (next_component, 0);

  if (dir_ends_in_slash && file_starts_with_slash)
    {
      _dbus_string_shorten (dir, 1);
    }
  else if (!(dir_ends_in_slash || file_starts_with_slash))
    {
      if (!_dbus_string_append_byte (dir, '/'))
        return FALSE;
    }

  return _dbus_string_copy (next_component, 0, dir,
                            _dbus_string_get_length (dir));
}

struct DBusDirIter
{
  DIR *d;
  
};

/**
 * Open a directory to iterate over.
 *
 * @param filename the directory name
 * @param result return location for error code if #NULL returned
 * @returns new iterator, or #NULL on error
 */
DBusDirIter*
_dbus_directory_open (const DBusString *filename,
                      DBusResultCode   *result)
{
  DIR *d;
  DBusDirIter *iter;
  const char *filename_c;

  _dbus_string_get_const_data (filename, &filename_c);

  d = opendir (filename_c);
  if (d == NULL)
    {
      dbus_set_result (result, _dbus_result_from_errno (errno));
      return NULL;
    }
  
  iter = dbus_new0 (DBusDirIter, 1);
  if (iter == NULL)
    {
      closedir (d);
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return NULL;
    }

  iter->d = d;

  return iter;
}

/**
 * Get next file in the directory. Will not return "." or ".."
 * on UNIX. If an error occurs, the contents of "filename"
 * are undefined. #DBUS_RESULT_SUCCESS is always returned
 * in result if no error occurs.
 *
 * @todo for thread safety, I think we have to use
 * readdir_r(). (GLib has the same issue, should file a bug.)
 *
 * @param iter the iterator
 * @param filename string to be set to the next file in the dir
 * @param result return location for error, or #DBUS_RESULT_SUCCESS
 * @returns #TRUE if filename was filled in with a new filename
 */
dbus_bool_t
_dbus_directory_get_next_file (DBusDirIter      *iter,
                               DBusString       *filename,
                               DBusResultCode   *result)
{
  /* we always have to put something in result, since return
   * value means whether there's a filename and doesn't
   * reliably indicate whether an error was set.
   */
  struct dirent *ent;
  
  dbus_set_result (result, DBUS_RESULT_SUCCESS);

 again:
  errno = 0;
  ent = readdir (iter->d);
  if (ent == NULL)
    {
      dbus_set_result (result,
                       _dbus_result_from_errno (errno));
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
          dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
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
  closedir (iter->d);
  dbus_free (iter);
}

/**
 * Generates the given number of random bytes,
 * using the best mechanism we can come up with.
 *
 * @param str the string
 * @param n_bytes the number of random bytes to append to string
 * @returns #TRUE on success, #FALSE if no memory or other failure
 */
dbus_bool_t
_dbus_generate_random_bytes (DBusString *str,
                             int         n_bytes)
{
  int old_len;
  int fd;
  
  old_len = _dbus_string_get_length (str);
  fd = -1;

  /* note, urandom on linux will fall back to pseudorandom */
  fd = open ("/dev/urandom", O_RDONLY);
  if (fd < 0)
    {
      unsigned long tv_usec;
      int i;

      /* fall back to pseudorandom */
      
      _dbus_get_current_time (NULL, &tv_usec);
      srand (tv_usec);
      
      i = 0;
      while (i < n_bytes)
        {
          double r;
          int b;
          
          r = rand ();
          b = (r / (double) RAND_MAX) * 255.0;
          
          if (!_dbus_string_append_byte (str, b))
            goto failed;
          
          ++i;
        }

      return TRUE;
    }
  else
    {
      if (_dbus_read (fd, str, n_bytes) != n_bytes)
        goto failed;

      close (fd);

      return TRUE;
    }

 failed:
  _dbus_string_set_length (str, old_len);
  if (fd >= 0)
    close (fd);
  return FALSE;
}

/**
 * A wrapper around strerror()
 *
 * @param errnum the errno
 * @returns an error message (never #NULL)
 */
const char *
_dbus_errno_to_string (int errnum)
{
  const char *msg;
  
  msg = strerror (errnum);
  if (msg == NULL)
    msg = "unknown";

  return msg;
}

/* Avoids a danger in threaded situations (calling close()
 * on a file descriptor twice, and another thread has
 * re-opened it since the first close)
 */
static int
close_and_invalidate (int *fd)
{
  int ret;

  if (*fd < 0)
    return -1;
  else
    {
      ret = close (*fd);
      *fd = -1;
    }

  return ret;
}

static dbus_bool_t
make_pipe (int        p[2],
           DBusError *error)
{
  if (pipe (p) < 0)
    {
      dbus_set_error (error,
		      DBUS_ERROR_SPAWN_FAILED,
		      "Failed to create pipe for communicating with child process (%s)",
		      _dbus_errno_to_string (errno));
      return FALSE;
    }
  else
    {
      _dbus_fd_set_close_on_exec (p[0]);
      _dbus_fd_set_close_on_exec (p[1]);      
      return TRUE;
    }
}

enum
{
  CHILD_CHDIR_FAILED,
  CHILD_EXEC_FAILED,
  CHILD_DUP2_FAILED,
  CHILD_FORK_FAILED
};

static void
write_err_and_exit (int fd, int msg)
{
  int en = errno;
  
  write (fd, &msg, sizeof(msg));
  write (fd, &en, sizeof(en));
  
  _exit (1);
}

static dbus_bool_t
read_ints (int        fd,
	   int       *buf,
	   int        n_ints_in_buf,
	   int       *n_ints_read,
	   DBusError *error)
{
  size_t bytes = 0;    
  
  while (TRUE)
    {
      size_t chunk;    

      if (bytes >= sizeof(int)*2)
        break; /* give up, who knows what happened, should not be
                * possible.
                */
          
    again:
      chunk = read (fd,
                    ((char*)buf) + bytes,
                    sizeof(int) * n_ints_in_buf - bytes);
      if (chunk < 0 && errno == EINTR)
        goto again;
          
      if (chunk < 0)
        {
          /* Some weird shit happened, bail out */
              
          dbus_set_error (error,
			  DBUS_ERROR_SPAWN_FAILED,
			  "Failed to read from child pipe (%s)",
			  _dbus_errno_to_string (errno));

          return FALSE;
        }
      else if (chunk == 0)
        break; /* EOF */
      else /* chunk > 0 */
	bytes += chunk;
    }

  *n_ints_read = (int)(bytes / sizeof(int));

  return TRUE;
}

static void
do_exec (int                       child_err_report_fd,
	 char                    **argv,
	 DBusSpawnChildSetupFunc   child_setup,
	 void                     *user_data)
{
#ifdef DBUS_BUILD_TESTS
  int i, max_open;
#endif

  if (child_setup)
    (* child_setup) (user_data);

#ifdef DBUS_BUILD_TESTS
  max_open = sysconf (_SC_OPEN_MAX);
  
  for (i = 3; i < max_open; i++)
    {
      int retval;

      retval = fcntl (i, F_GETFD);

      if (retval != -1 && !(retval & FD_CLOEXEC))
	_dbus_warn ("Fd %d did not have the close-on-exec flag set!\n", i);
    }
#endif
  
  execv (argv[0], argv);

  /* Exec failed */
  write_err_and_exit (child_err_report_fd,
                      CHILD_EXEC_FAILED);
  
}

/**
 * Spawns a new process. The executable name and argv[0]
 * are the same, both are provided in argv[0]. The child_setup
 * function is passed the given user_data and is run in the child
 * just before calling exec().
 *
 * @todo this code should be reviewed/double-checked as it's fairly
 * complex and no one has reviewed it yet.
 *
 * @param argv the executable and arguments
 * @param child_setup function to call in child pre-exec()
 * @param user_data user data for setup function
 * @param error error object to be filled in if function fails
 * @returns #TRUE on success, #FALSE if error is filled in
 */
dbus_bool_t
_dbus_spawn_async (char                    **argv,
		   DBusSpawnChildSetupFunc   child_setup,
		   void                     *user_data,
		   DBusError                *error)
{
  int pid = -1, grandchild_pid;
  int child_err_report_pipe[2] = { -1, -1 };
  int status;
  
  if (!make_pipe (child_err_report_pipe, error))
    return FALSE;

  pid = fork ();
  
  if (pid < 0)
    {
      dbus_set_error (error,
		      DBUS_ERROR_SPAWN_FORK_FAILED,
		      "Failed to fork (%s)",
		      _dbus_errno_to_string (errno));
      return FALSE;
    }
  else if (pid == 0)
    {
      /* Immediate child. */
      
      /* Be sure we crash if the parent exits
       * and we write to the err_report_pipe
       */
      signal (SIGPIPE, SIG_DFL);

      /* Close the parent's end of the pipes;
       * not needed in the close_descriptors case,
       * though
       */
      close_and_invalidate (&child_err_report_pipe[0]);

      /* We need to fork an intermediate child that launches the
       * final child. The purpose of the intermediate child
       * is to exit, so we can waitpid() it immediately.
       * Then the grandchild will not become a zombie.
       */
      grandchild_pid = fork ();
      
      if (grandchild_pid < 0)
	{
	  write_err_and_exit (child_err_report_pipe[1],
			      CHILD_FORK_FAILED);              
	}
      else if (grandchild_pid == 0)
	{
	  do_exec (child_err_report_pipe[1],
		   argv,
		   child_setup, user_data);
	}
      else
	{
	  _exit (0);
	}
    }
  else
    {
      /* Parent */

      int buf[2];
      int n_ints = 0;    
      
      /* Close the uncared-about ends of the pipes */
      close_and_invalidate (&child_err_report_pipe[1]);

    wait_again:
      if (waitpid (pid, &status, 0) < 0)
	{
	  if (errno == EINTR)
	    goto wait_again;
	  else if (errno == ECHILD)
	    ; /* do nothing, child already reaped */
	  else
	    _dbus_warn ("waitpid() should not fail in "
			"'_dbus_spawn_async'");
	}

      if (!read_ints (child_err_report_pipe[0],
                      buf, 2, &n_ints,
                      error))
	  goto cleanup_and_fail;
      
      if (n_ints >= 2)
        {
          /* Error from the child. */
          switch (buf[0])
            {
	    default:
              dbus_set_error (error,
			      DBUS_ERROR_SPAWN_FAILED,
			      "Unknown error executing child process \"%s\"",
			      argv[0]);
              break;
	    }

	  goto cleanup_and_fail;
	}


      /* Success against all odds! return the information */
      close_and_invalidate (&child_err_report_pipe[0]);

      return TRUE;
    }

 cleanup_and_fail:

  /* There was an error from the Child, reap the child to avoid it being
     a zombie.
  */
  if (pid > 0)
    {
    wait_failed:
      if (waitpid (pid, NULL, 0) < 0)
	{
          if (errno == EINTR)
            goto wait_failed;
          else if (errno == ECHILD)
            ; /* do nothing, child already reaped */
          else
            _dbus_warn ("waitpid() should not fail in "
			"'_dbus_spawn_async'");
	}
    }
  
  close_and_invalidate (&child_err_report_pipe[0]);
  close_and_invalidate (&child_err_report_pipe[1]);

  return FALSE;
}

/**
 * signal (SIGPIPE, SIG_IGN);
 */
void
_dbus_disable_sigpipe (void)
{
  signal (SIGPIPE, SIG_IGN);
}

/**
 * Sets the file descriptor to be close
 * on exec. Should be called for all file
 * descriptors in D-BUS code.
 *
 * @param fd the file descriptor
 */
void
_dbus_fd_set_close_on_exec (int fd)
{
  int val;
  
  val = fcntl (fd, F_GETFD, 0);
  
  if (val < 0)
    return;

  val |= FD_CLOEXEC;
  
  fcntl (fd, F_SETFD, val);
}

/** @} end of sysdeps */
