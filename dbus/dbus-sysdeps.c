/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-sysdeps.c Wrappers around system/libc features (internal to D-BUS implementation)
 * 
 * Copyright (C) 2002, 2003  Red Hat, Inc.
 * Copyright (C) 2003 CodeFactory AB
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
#include "dbus-test.h"
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
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
#include <grp.h>

#ifdef HAVE_WRITEV
#include <sys/uio.h>
#endif
#ifdef HAVE_POLL
#include <sys/poll.h>
#endif
#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#endif


#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef SUN_LEN
/* This system is not POSIX.1g.         */
#define SUN_LEN(ptr) ((size_t) (((struct sockaddr_un *) 0)->sun_path)  \
       + strlen ((ptr)->sun_path))
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
 * Wrapper for setenv(). If the value is #NULL, unsets
 * the environment variable.
 *
 * @todo if someone can verify it's safe, we could avoid the
 * memleak when doing an unset.
 *
 * @param varname name of environment variable
 * @param value value of environment variable
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_setenv (const char *varname,
              const char *value)
{
  _dbus_assert (varname != NULL);
  
  if (value == NULL)
    {
#ifdef HAVE_UNSETENV
      unsetenv (varname);
      return TRUE;
#else
      char *putenv_value;
      size_t len;

      len = strlen (varname);

      /* Use system malloc to avoid memleaks that dbus_malloc
       * will get upset about.
       */
      
      putenv_value = malloc (len + 1);
      if (putenv_value == NULL)
        return FALSE;

      strcpy (putenv_value, varname);
      
      return (putenv (putenv_value) == 0);
#endif
    }
  else
    {
#ifdef HAVE_SETENV
      return (setenv (varname, value, TRUE) == 0);
#else
      char *putenv_value;
      size_t len;
      size_t varname_len;
      size_t value_len;

      varname_len = strlen (varname);
      value_len = strlen (value);
      
      len = varname_len + value_len + 1 /* '=' */ ;

      /* Use system malloc to avoid memleaks that dbus_malloc
       * will get upset about.
       */
      
      putenv_value = malloc (len + 1);
      if (putenv_value == NULL)
        return FALSE;

      strcpy (putenv_value, varname);
      strcpy (putenv_value + varname_len, "=");
      strcpy (putenv_value + varname_len + 1, value);
      
      return (putenv (putenv_value) == 0);
#endif
    }
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
 * _dbus_read() handles EINTR for you. _dbus_read() can
 * return ENOMEM, even though regular UNIX read doesn't.
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

  data = _dbus_string_get_data_len (buffer, start, count);

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
  
  data = _dbus_string_get_const_data_len (buffer, start, len);
  
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

    data1 = _dbus_string_get_const_data_len (buffer1, start1, len1);

    if (buffer2 != NULL)
      data2 = _dbus_string_get_const_data_len (buffer2, start2, len2);
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

#define _DBUS_MAX_SUN_PATH_LENGTH 99

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
 * @param path the path to UNIX domain socket
 * @param error return location for error code
 * @returns connection file descriptor or -1 on error
 */
int
_dbus_connect_unix_socket (const char     *path,
                           DBusError      *error)
{
  int fd;
  struct sockaddr_un addr;  

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  fd = socket (PF_UNIX, SOCK_STREAM, 0);
  
  if (fd < 0)
    {
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Failed to create socket: %s",
                      _dbus_strerror (errno)); 
      
      return -1;
    }

  _DBUS_ZERO (addr);
  addr.sun_family = AF_UNIX;
  strncpy (addr.sun_path, path, _DBUS_MAX_SUN_PATH_LENGTH - 1);
  
  if (connect (fd, (struct sockaddr*) &addr, sizeof (addr)) < 0)
    {      
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Failed to connect to socket %s: %s",
                      path, _dbus_strerror (errno));

      close (fd);
      fd = -1;
      
      return -1;
    }

  if (!_dbus_set_fd_nonblocking (fd, error))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      
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
 * @todo we'd like to be able to use the abstract namespace on linux
 * (see "man 7 unix"). The question is whether to silently move all
 * paths into that namespace if we can (I think that's best) or to
 * require it to be specified explicitly in the dbus address.  Also,
 * need to sort out how to check for abstract namespace support.
 *
 * @param path the socket name
 * @param error return location for errors
 * @returns the listening file descriptor or -1 on error
 */
int
_dbus_listen_unix_socket (const char     *path,
                          DBusError      *error)
{
  int listen_fd;
  struct sockaddr_un addr;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  listen_fd = socket (PF_UNIX, SOCK_STREAM, 0);
  
  if (listen_fd < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to create socket \"%s\": %s",
                      path, _dbus_strerror (errno));
      return -1;
    }

  /* FIXME discussed security implications of this with Nalin,
   * and we couldn't think of where it would kick our ass, but
   * it still seems a bit sucky. It also has non-security suckage;
   * really we'd prefer to exit if the socket is already in use.
   * But there doesn't seem to be a good way to do this.
   *
   * Just to be extra careful, I threw in the stat() - clearly
   * the stat() can't *fix* any security issue, but it probably
   * makes it harder to exploit.
   */
  {
    struct stat sb;

    if (stat (path, &sb) == 0 &&
        S_ISSOCK (sb.st_mode))
      unlink (path);
  }
  
  _DBUS_ZERO (addr);
  addr.sun_family = AF_UNIX;
  strncpy (addr.sun_path, path, _DBUS_MAX_SUN_PATH_LENGTH - 1);
  
  if (bind (listen_fd, (struct sockaddr*) &addr, SUN_LEN (&addr)) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to bind socket \"%s\": %s",
                      path, _dbus_strerror (errno));
      close (listen_fd);
      return -1;
    }

  if (listen (listen_fd, 30 /* backlog */) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to listen on socket \"%s\": %s",
                      path, _dbus_strerror (errno));
      close (listen_fd);
      return -1;
    }

  if (!_dbus_set_fd_nonblocking (listen_fd, error))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      close (listen_fd);
      return -1;
    }

  /* Try opening up the permissions, but if we can't, just go ahead
   * and continue, maybe it will be good enough.
   */
  if (chmod (path, 0777) < 0)
    _dbus_warn ("Could not set mode 0777 on socket %s\n",
                path);
  
  return listen_fd;
}

/**
 * Creates a socket and connects to a socket at the given host 
 * and port. The connection fd is returned, and is set up as
 * nonblocking.
 *
 * @param host the host name to connect to
 * @param port the prot to connect to
 * @param error return location for error code
 * @returns connection file descriptor or -1 on error
 */
int
_dbus_connect_tcp_socket (const char     *host,
                          dbus_uint32_t   port,
                          DBusError      *error)
{
  int fd;
  struct sockaddr_in addr;
  struct hostent *he;
  struct in_addr *haddr;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  fd = socket (AF_INET, SOCK_STREAM, 0);
  
  if (fd < 0)
    {
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Failed to create socket: %s",
                      _dbus_strerror (errno)); 
      
      return -1;
    }

  if (host == NULL)
    host = "localhost";

  he = gethostbyname (host);
  if (he == NULL) 
    {
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Failed to lookup hostname: %s",
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
      dbus_set_error (error,
                       _dbus_error_from_errno (errno),
                      "Failed to connect to socket %s: %s:%d",
                      host, _dbus_strerror (errno), port);

      close (fd);
      fd = -1;
      
      return -1;
    }

  if (!_dbus_set_fd_nonblocking (fd, error))
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
 * @param error return location for errors
 * @returns the listening file descriptor or -1 on error
 */
int
_dbus_listen_tcp_socket (const char     *host,
                         dbus_uint32_t   port,
                         DBusError      *error)
{
  int listen_fd;
  struct sockaddr_in addr;
  struct hostent *he;
  struct in_addr *haddr;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  listen_fd = socket (AF_INET, SOCK_STREAM, 0);
  
  if (listen_fd < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to create socket \"%s:%d\": %s",
                      host, port, _dbus_strerror (errno));
      return -1;
    }

  if (host == NULL)
    host = "localhost";
  
  he = gethostbyname (host);
  if (he == NULL) 
    {
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Failed to lookup hostname: %s",
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
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to bind socket \"%s:%d\": %s",
                      host, port, _dbus_strerror (errno));
      close (listen_fd);
      return -1;
    }

  if (listen (listen_fd, 30 /* backlog */) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),  
                      "Failed to listen on socket \"%s:%d\": %s",
                      host, port, _dbus_strerror (errno));
      close (listen_fd);
      return -1;
    }

  if (!_dbus_set_fd_nonblocking (listen_fd, error))
    {
      close (listen_fd);
      return -1;
    }
  
  return listen_fd;
}

static dbus_bool_t
write_credentials_byte (int             server_fd,
                        DBusError      *error)
{
  int bytes_written;
  char buf[1] = { '\0' };

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
 again:

  bytes_written = write (server_fd, buf, 1);

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
 * @param error location to store error code
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_read_credentials_unix_socket  (int              client_fd,
                                     DBusCredentials *credentials,
                                     DBusError       *error)
{
  struct msghdr msg;
  struct iovec iov;
  char buf;

#ifdef HAVE_CMSGCRED 
  char cmsgmem[CMSG_SPACE (sizeof (struct cmsgcred))];
  struct cmsghdr *cmsg = (struct cmsghdr *) cmsgmem;
#endif

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  /* The POSIX spec certainly doesn't promise this, but
   * we need these assertions to fail as soon as we're wrong about
   * it so we can do the porting fixups
   */
  _dbus_assert (sizeof (pid_t) <= sizeof (credentials->pid));
  _dbus_assert (sizeof (uid_t) <= sizeof (credentials->uid));
  _dbus_assert (sizeof (gid_t) <= sizeof (credentials->gid));
  
  credentials->pid = -1;
  credentials->uid = -1;
  credentials->gid = -1;

#if defined(LOCAL_CREDS) && defined(HAVE_CMSGCRED)
  /* Set the socket to receive credentials on the next message */
  {
    int on = 1;
    if (setsockopt (client_fd, 0, LOCAL_CREDS, &on, sizeof (on)) < 0)
      {
	_dbus_verbose ("Unable to set LOCAL_CREDS socket option\n");
	return FALSE;
      }
  }
#endif

  iov.iov_base = &buf;
  iov.iov_len = 1;

  memset (&msg, 0, sizeof (msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

#ifdef HAVE_CMSGCRED
  memset (cmsgmem, 0, sizeof (cmsgmem));
  msg.msg_control = cmsgmem;
  msg.msg_controllen = sizeof (cmsgmem);
#endif

 again:
  if (recvmsg (client_fd, &msg, 0) < 0)
    {
      if (errno == EINTR)
	goto again;

      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to read credentials byte: %s",
                      _dbus_strerror (errno));
      return FALSE;
    }

  if (buf != '\0')
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Credentials byte was not nul");
      return FALSE;
    }

#ifdef HAVE_CMSGCRED
  if (cmsg->cmsg_len < sizeof (cmsgmem) || cmsg->cmsg_type != SCM_CREDS)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED);
      _dbus_verbose ("Message from recvmsg() was not SCM_CREDS\n");
      return FALSE;
    }
#endif

  _dbus_verbose ("read credentials byte\n");

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
      }
    else
      {
	_dbus_verbose ("Failed to getsockopt() credentials, returned len %d/%d: %s\n",
		       cr_len, (int) sizeof (cr), _dbus_strerror (errno));
      }
#elif defined(HAVE_CMSGCRED)
    struct cmsgcred *cred;

    cred = (struct cmsgcred *) CMSG_DATA (cmsg);

    credentials->pid = cred->cmcred_pid;
    credentials->uid = cred->cmcred_euid;
    credentials->gid = cred->cmcred_groups[0];
#else /* !SO_PEERCRED && !HAVE_CMSGCRED */
    _dbus_verbose ("Socket credentials not supported on this OS\n");
#endif
  }

  _dbus_verbose ("Credentials: pid %d  uid %d  gid %d\n",
		 credentials->pid,
		 credentials->uid,
		 credentials->gid);
    
  return TRUE;
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
  struct sockaddr addr;
  socklen_t addrlen;

  addrlen = sizeof (addr);
  
 retry:
  client_fd = accept (listen_fd, &addr, &addrlen);
  
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

  buf = _dbus_string_get_data_len (str, orig_len, MAX_LONG_LEN);

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

  buf = _dbus_string_get_data_len (str, orig_len, MAX_ULONG_LEN);

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

  buf = _dbus_string_get_data_len (str, orig_len, MAX_DOUBLE_LEN);

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

  p = _dbus_string_get_const_data_len (str, start,
                                       _dbus_string_get_length (str) - start);

  end = NULL;
  errno = 0;
  v = strtol (p, &end, 0);
  if (end == NULL || end == p || errno != 0)
    return FALSE;

  if (value_return)
    *value_return = v;
  if (end_return)
    *end_return = start + (end - p);

  return TRUE;
}

/**
 * Parses an unsigned integer contained in a DBusString. Either return
 * parameter may be #NULL if you aren't interested in it. The integer
 * is parsed and stored in value_return. Return parameters are not
 * initialized if the function returns #FALSE.
 *
 * @param str the string
 * @param start the byte index of the start of the integer
 * @param value_return return location of the integer value or #NULL
 * @param end_return return location of the end of the integer, or #NULL
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_string_parse_uint (const DBusString *str,
                         int               start,
                         unsigned long    *value_return,
                         int              *end_return)
{
  unsigned long v;
  const char *p;
  char *end;

  p = _dbus_string_get_const_data_len (str, start,
                                       _dbus_string_get_length (str) - start);

  end = NULL;
  errno = 0;
  v = strtoul (p, &end, 0);
  if (end == NULL || end == p || errno != 0)
    return FALSE;

  if (value_return)
    *value_return = v;
  if (end_return)
    *end_return = start + (end - p);

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
  
  p = _dbus_string_get_const_data_len (str, start,
                                       _dbus_string_get_length (str) - start);

  end = NULL;
  errno = 0;
  v = strtod (p, &end);
  if (end == NULL || end == p || errno != 0)
    return FALSE;

  if (value_return)
    *value_return = v;
  if (end_return)
    *end_return = start + (end - p);

  return TRUE;
}

/** @} */ /* DBusString group */

/**
 * @addtogroup DBusInternalsUtils
 * @{
 */

static dbus_bool_t
store_user_info (struct passwd    *p,
                 DBusCredentials  *credentials,
                 DBusString       *homedir,
                 DBusString       *username_out)
{
  int old_homedir_len;
  
  if (credentials != NULL)
    {
      credentials->uid = p->pw_uid;
      credentials->gid = p->pw_gid;
    }

  old_homedir_len = 0;
  if (homedir != NULL)
    {
      old_homedir_len = _dbus_string_get_length (homedir);
      
      if (!_dbus_string_append (homedir, p->pw_dir))
        {
          _dbus_verbose ("No memory to get homedir\n");
          return FALSE;
        }
    }
  
  if (username_out &&
      !_dbus_string_append (username_out, p->pw_name))
    {
      if (homedir)
        _dbus_string_set_length (homedir, old_homedir_len);
      _dbus_verbose ("No memory to get username\n");
      return FALSE;
    }
      
  _dbus_verbose ("Username %s has uid %d gid %d homedir %s\n",
                 p->pw_name, (int) p->pw_uid, (int) p->pw_gid,
                 p->pw_dir);

  return TRUE;
}
  
/**
 * Gets user info using either username or uid. Only
 * one of these may be passed in, either username
 * must be #NULL or uid must be < 0.
 *
 * @param username the username
 * @param uid the user ID
 * @param credentials to fill in or #NULL
 * @param homedir string to append homedir to or #NULL
 * @param username_out string to append username to or #NULL
 *
 * @returns #TRUE on success
 */
static dbus_bool_t
get_user_info (const DBusString *username,
               int               uid,
               DBusCredentials  *credentials,
               DBusString       *homedir,
               DBusString       *username_out)
{
  const char *username_c_str;
      
  /* exactly one of username/uid provided */
  _dbus_assert (username != NULL || uid >= 0);
  _dbus_assert (username == NULL || uid < 0);

  if (credentials)
    {
      credentials->pid = -1;
      credentials->uid = -1;
      credentials->gid = -1;
    }
  
  if (username != NULL)
    username_c_str = _dbus_string_get_const_data (username);
  else
    username_c_str = NULL;

  /* For now assuming that the getpwnam() and getpwuid() flavors
   * are always symmetrical, if not we have to add more configure
   * checks
   */
  
#if defined (HAVE_POSIX_GETPWNAME_R) || defined (HAVE_NONPOSIX_GETPWNAME_R)
  {
    struct passwd *p;
    int result;
    char buf[1024];
    struct passwd p_str;

    p = NULL;
#ifdef HAVE_POSIX_GETPWNAME_R
    if (uid >= 0)
      result = getpwuid_r (uid, &p_str, buf, sizeof (buf),
                           &p);
    else
      result = getpwnam_r (username_c_str, &p_str, buf, sizeof (buf),
                           &p);
#else
    if (uid >= 0)
      p = getpwuid_r (uid, &p_str, buf, sizeof (buf));
    else
      p = getpwnam_r (username_c_str, &p_str, buf, sizeof (buf));
    result = 0;
#endif /* !HAVE_POSIX_GETPWNAME_R */
    if (result == 0 && p == &p_str)
      {
        return store_user_info (p, credentials, homedir,
                                username_out);
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

    if (uid >= 0)
      p = getpwuid (uid);
    else
      p = getpwnam (username_c_str);

    if (p != NULL)
      {
        return store_user_info (p, credentials, homedir,
                                username_out);
      }
    else
      {
        _dbus_verbose ("User %s unknown\n", username_c_str);
        return FALSE;
      }
  }
#endif  /* ! HAVE_GETPWNAM_R */
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
  return get_user_info (username, -1, credentials, NULL, NULL);
}

/**
 * Gets the credentials corresponding to the given user ID.
 *
 * @param user_id the user ID
 * @param credentials credentials to fill in
 * @returns #TRUE if the username existed and we got some credentials
 */
dbus_bool_t
_dbus_credentials_from_user_id (unsigned long     user_id,
                                DBusCredentials  *credentials)
{
  return get_user_info (NULL, user_id, credentials, NULL, NULL);
}

_DBUS_DEFINE_GLOBAL_LOCK (user_info);

typedef struct
{
  DBusString name;
  DBusString dir;
  DBusCredentials creds;
} UserInfo;

static void
shutdown_user_info (void *data)
{
  UserInfo *u = data;

  _dbus_string_free (&u->name);
  _dbus_string_free (&u->dir);
}

/**
 * Gets information about the user running this process.
 *
 * @param username return location for username or #NULL
 * @param homedir return location for home directory or #NULL
 * @param credentials return location for credentials or #NULL
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_user_info_from_current_process (const DBusString      **username,
                                      const DBusString      **homedir,
                                      const DBusCredentials **credentials)
{
  static UserInfo u;
  static int initialized_generation = 0;
  
  if (!_DBUS_LOCK (user_info))
    return FALSE;

  if (initialized_generation != _dbus_current_generation)
    {
      if (!_dbus_string_init (&u.name))
        {
          _DBUS_UNLOCK (user_info);
          return FALSE;
        }

      if (!_dbus_string_init (&u.dir))
        {
          _dbus_string_free (&u.name);
          _DBUS_UNLOCK (user_info);
          return FALSE;
        }
      
      u.creds.uid = -1;
      u.creds.gid = -1;
      u.creds.pid = -1;

      if (!get_user_info (NULL, getuid (),
                          &u.creds, &u.dir, &u.name))
        goto fail_init;
      
      if (!_dbus_register_shutdown_func (shutdown_user_info,
                                         &u))
        goto fail_init;
      
      initialized_generation = _dbus_current_generation;
    fail_init:
      if (initialized_generation != _dbus_current_generation)
        {
          _dbus_string_free (&u.name);
          _dbus_string_free (&u.dir);
          _DBUS_UNLOCK (user_info);
          return FALSE;
        }
    }

  if (username)
    *username = &u.name;
  if (homedir)
    *homedir = &u.dir;
  if (credentials)
    *credentials = &u.creds;
  
  _DBUS_UNLOCK (user_info);

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
  return get_user_info (username, -1, NULL, homedir, NULL);
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
  /* The POSIX spec certainly doesn't promise this, but
   * we need these assertions to fail as soon as we're wrong about
   * it so we can do the porting fixups
   */
  _dbus_assert (sizeof (pid_t) <= sizeof (credentials->pid));
  _dbus_assert (sizeof (uid_t) <= sizeof (credentials->uid));
  _dbus_assert (sizeof (gid_t) <= sizeof (credentials->gid));
  
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
 * Gets group ID from group name.
 *
 * @param group_name name of the group
 * @param gid location to store group ID
 * @returns #TRUE if group was known
 */
dbus_bool_t
_dbus_get_group_id (const DBusString *group_name,
                    unsigned long    *gid)
{
  const char *group_c_str;
  
  group_c_str = _dbus_string_get_const_data (group_name);
  
  /* For now assuming that the getgrnam() and getgrgid() flavors
   * always correspond to the pwnam flavors, if not we have
   * to add more configure checks.
   */
  
#if defined (HAVE_POSIX_GETPWNAME_R) || defined (HAVE_NONPOSIX_GETPWNAME_R)
  {
    struct group *g;
    int result;
    char buf[1024];
    struct group g_str;

    g = NULL;
#ifdef HAVE_POSIX_GETPWNAME_R

    result = getgrnam_r (group_c_str, &g_str, buf, sizeof (buf),
                         &g);
#else
    p = getgrnam_r (group_c_str, &g_str, buf, sizeof (buf));
    result = 0;
#endif /* !HAVE_POSIX_GETPWNAME_R */
    if (result == 0 && g == &g_str)
      {
        *gid = g->gr_gid;
        return TRUE;
      }
    else
      {
        _dbus_verbose ("Group %s unknown\n", group_c_str);
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
        *gid = g->gr_gid;
        return TRUE;
      }
    else
      {
        _dbus_verbose ("Group %s unknown\n", group_c_str);
        return FALSE;
      }
  }
#endif  /* ! HAVE_GETPWNAM_R */
}

/**
 * Gets all groups for a particular user. Returns #FALSE
 * if no memory, or user isn't known, but always initializes
 * group_ids to a NULL array.
 *
 * @todo failing to distinguish "out of memory" from
 * "unknown user" is kind of bogus and would probably
 * result in a failure in a comprehensive test suite.
 *
 * @param uid the user ID
 * @param group_ids return location for array of group IDs
 * @param n_group_ids return location for length of returned array
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_get_groups (unsigned long   uid,
                  unsigned long **group_ids,
                  int            *n_group_ids)
{
  DBusCredentials creds;
  DBusString username;
  const char *username_c;
  dbus_bool_t retval;
  
  *group_ids = NULL;
  *n_group_ids = 0;

  retval = FALSE;

  if (!_dbus_string_init (&username))
    return FALSE;

  if (!get_user_info (NULL, uid, &creds,
                      NULL, &username) ||
      creds.gid < 0)
    goto out;

  username_c = _dbus_string_get_const_data (&username);
  
#ifdef HAVE_GETGROUPLIST
  {
    gid_t *buf;
    int buf_count;
    int i;
    
    buf_count = 17;
    buf = dbus_new (gid_t, buf_count);
    if (buf == NULL)
      goto out;
    
    if (getgrouplist (username_c,
                      creds.gid,
                      buf, &buf_count) < 0)
      {
        gid_t *new = dbus_realloc (buf, buf_count * sizeof (buf[0]));
        if (new == NULL)
          {
            dbus_free (buf);
            goto out;
          }
        
        buf = new;

        getgrouplist (username_c, creds.gid, buf, &buf_count);
      }

    *group_ids = dbus_new (unsigned long, buf_count);
    if (*group_ids == NULL)
      {
        dbus_free (buf);
        goto out;
      }
    
    for (i = 0; i < buf_count; ++i)
      (*group_ids)[i] = buf[i];

    *n_group_ids = buf_count;
    
    dbus_free (buf);
  }
#else  /* HAVE_GETGROUPLIST */
  {
    /* We just get the one group ID */
    *group_ids = dbus_new (unsigned long, 1);
    if (*group_ids == NULL)
      goto out;

    *n_group_ids = 1;

    (*group_ids)[0] = creds.gid;
  }
#endif /* HAVE_GETGROUPLIST */

    retval = TRUE;
    
  out:
    _dbus_string_free (&username);
    return retval;
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


_DBUS_DEFINE_GLOBAL_LOCK (atomic);

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
  
  _DBUS_LOCK (atomic);
  *atomic += 1;
  res = *atomic;
  _DBUS_UNLOCK (atomic);
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
  
  _DBUS_LOCK (atomic);
  *atomic -= 1;
  res = *atomic;
  _DBUS_UNLOCK (atomic);
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
  int max_fd = 0;
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
  struct stat sb;
  int orig_len;
  int total;
  const char *filename_c;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  filename_c = _dbus_string_get_const_data (filename);
  
  /* O_BINARY useful on Cygwin */
  fd = open (filename_c, O_RDONLY | O_BINARY);
  if (fd < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to open \"%s\": %s",
                      filename_c,
                      _dbus_strerror (errno));
      return FALSE;
    }

  if (fstat (fd, &sb) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to stat \"%s\": %s",
                      filename_c,
                      _dbus_strerror (errno));

      _dbus_verbose ("fstat() failed: %s",
                     _dbus_strerror (errno));
      
      close (fd);
      
      return FALSE;
    }

  if (sb.st_size > _DBUS_ONE_MEGABYTE)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "File size %lu of \"%s\" is too large.",
                      filename_c, (unsigned long) sb.st_size);
      close (fd);
      return FALSE;
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
              dbus_set_error (error, _dbus_error_from_errno (errno),
                              "Error reading \"%s\": %s",
                              filename_c,
                              _dbus_strerror (errno));

              _dbus_verbose ("read() failed: %s",
                             _dbus_strerror (errno));
              
              close (fd);
              _dbus_string_set_length (str, orig_len);
              return FALSE;
            }
          else
            total += bytes_read;
        }

      close (fd);
      return TRUE;
    }
  else if (sb.st_size != 0)
    {
      _dbus_verbose ("Can only open regular files at the moment.\n");
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "\"%s\" is not a regular file",
                      filename_c);
      close (fd);
      return FALSE;
    }
  else
    {
      close (fd);
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
      return FALSE;
    }
  
  if (!_dbus_string_append (&tmp_filename, "."))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return FALSE;
    }

#define N_TMP_FILENAME_RANDOM_BYTES 8
  if (!_dbus_generate_random_ascii (&tmp_filename, N_TMP_FILENAME_RANDOM_BYTES))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return FALSE;
    }
    
  filename_c = _dbus_string_get_const_data (filename);
  tmp_filename_c = _dbus_string_get_const_data (&tmp_filename);

  fd = open (tmp_filename_c, O_WRONLY | O_BINARY | O_EXCL | O_CREAT,
             0600);
  if (fd < 0)
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

      bytes_written = _dbus_write (fd, str, total,
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

  if (close (fd) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Could not close file %s: %s",
                      tmp_filename_c, _dbus_strerror (errno));

      goto out;
    }

  fd = -1;
  
  if (rename (tmp_filename_c, filename_c) < 0)
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

  if (fd >= 0)
    close (fd);
        
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
  int fd;
  const char *filename_c;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  filename_c = _dbus_string_get_const_data (filename);
  
  fd = open (filename_c, O_WRONLY | O_BINARY | O_EXCL | O_CREAT,
             0600);
  if (fd < 0)
    {
      dbus_set_error (error,
                      DBUS_ERROR_FAILED,
                      "Could not create file %s: %s\n",
                      filename_c,
                      _dbus_strerror (errno));
      return FALSE;
    }

  if (close (fd) < 0)
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

  if (mkdir (filename_c, 0700) < 0)
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

struct DBusDirIter
{
  DIR *d;
  
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
  ent = readdir (iter->d);
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
  closedir (iter->d);
  dbus_free (iter);
}

static dbus_bool_t
pseudorandom_generate_random_bytes (DBusString *str,
                                    int         n_bytes)
{
  int old_len;
  unsigned long tv_usec;
  int i;
  
  old_len = _dbus_string_get_length (str);

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
          
      if (!_dbus_string_append_byte (str, b))
        goto failed;
          
      ++i;
    }

  return TRUE;

 failed:
  _dbus_string_set_length (str, old_len);
  return FALSE;
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
  int fd;

  /* FALSE return means "no memory", if it could
   * mean something else then we'd need to return
   * a DBusError. So we always fall back to pseudorandom
   * if the I/O fails.
   */
  
  old_len = _dbus_string_get_length (str);
  fd = -1;

  /* note, urandom on linux will fall back to pseudorandom */
  fd = open ("/dev/urandom", O_RDONLY);
  if (fd < 0)
    return pseudorandom_generate_random_bytes (str, n_bytes);

  if (_dbus_read (fd, str, n_bytes) != n_bytes)
    {
      close (fd);
      _dbus_string_set_length (str, old_len);
      return pseudorandom_generate_random_bytes (str, n_bytes);
    }

  _dbus_verbose ("Read %d bytes from /dev/urandom\n",
                 n_bytes);
  
  close (fd);
  
  return TRUE;
}

/**
 * Generates the given number of random bytes, where the bytes are
 * chosen from the alphanumeric ASCII subset.
 *
 * @param str the string
 * @param n_bytes the number of random ASCII bytes to append to string
 * @returns #TRUE on success, #FALSE if no memory or other failure
 */
dbus_bool_t
_dbus_generate_random_ascii (DBusString *str,
                             int         n_bytes)
{
  static const char letters[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz";
  int i;
  int len;
  
  if (!_dbus_generate_random_bytes (str, n_bytes))
    return FALSE;
  
  len = _dbus_string_get_length (str);
  i = len - n_bytes;
  while (i < len)
    {
      _dbus_string_set_byte (str, i,
                             letters[_dbus_string_get_byte (str, i) %
                                     (sizeof (letters) - 1)]);

      ++i;
    }

  _dbus_assert (_dbus_string_validate_ascii (str, len - n_bytes,
                                             n_bytes));

  return TRUE;
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
  const char *msg;
  
  msg = strerror (error_number);
  if (msg == NULL)
    msg = "unknown";

  return msg;
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

/**
 * Converts a UNIX errno into a #DBusError name.
 *
 * @todo should cover more errnos, specifically those
 * from open().
 * 
 * @param error_number the errno.
 * @returns an error name
 */
const char*
_dbus_error_from_errno (int error_number)
{
  switch (error_number)
    {
    case 0:
      return DBUS_ERROR_FAILED;
      
#ifdef EPROTONOSUPPORT
    case EPROTONOSUPPORT:
      return DBUS_ERROR_NOT_SUPPORTED;
#endif
#ifdef EAFNOSUPPORT
    case EAFNOSUPPORT:
      return DBUS_ERROR_NOT_SUPPORTED;
#endif
#ifdef ENFILE
    case ENFILE:
      return DBUS_ERROR_LIMITS_EXCEEDED; /* kernel out of memory */
#endif
#ifdef EMFILE
    case EMFILE:
      return DBUS_ERROR_LIMITS_EXCEEDED;
#endif
#ifdef EACCES
    case EACCES:
      return DBUS_ERROR_ACCESS_DENIED;
#endif
#ifdef EPERM
    case EPERM:
      return DBUS_ERROR_ACCESS_DENIED;
#endif
#ifdef ENOBUFS
    case ENOBUFS:
      return DBUS_ERROR_NO_MEMORY;
#endif
#ifdef ENOMEM
    case ENOMEM:
      return DBUS_ERROR_NO_MEMORY;
#endif
#ifdef EINVAL
    case EINVAL:
      return DBUS_ERROR_FAILED;
#endif
#ifdef EBADF
    case EBADF:
      return DBUS_ERROR_FAILED;
#endif
#ifdef EFAULT
    case EFAULT:
      return DBUS_ERROR_FAILED;
#endif
#ifdef ENOTSOCK
    case ENOTSOCK:
      return DBUS_ERROR_FAILED;
#endif
#ifdef EISCONN
    case EISCONN:
      return DBUS_ERROR_FAILED;
#endif
#ifdef ECONNREFUSED
    case ECONNREFUSED:
      return DBUS_ERROR_NO_SERVER;
#endif
#ifdef ETIMEDOUT
    case ETIMEDOUT:
      return DBUS_ERROR_TIMEOUT;
#endif
#ifdef ENETUNREACH
    case ENETUNREACH:
      return DBUS_ERROR_NO_NETWORK;
#endif
#ifdef EADDRINUSE
    case EADDRINUSE:
      return DBUS_ERROR_ADDRESS_IN_USE;
#endif
#ifdef EEXIST
    case EEXIST:
      return DBUS_ERROR_FILE_NOT_FOUND;
#endif
#ifdef ENOENT
    case ENOENT:
      return DBUS_ERROR_FILE_NOT_FOUND;
#endif
    }

  return DBUS_ERROR_FAILED;
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
 * Creates a full-duplex pipe (as in socketpair()).
 * Sets both ends of the pipe nonblocking.
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
#ifdef HAVE_SOCKETPAIR
  int fds[2];

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Could not create full-duplex pipe");
      return FALSE;
    }

  if (!blocking &&
      (!_dbus_set_fd_nonblocking (fds[0], NULL) ||
       !_dbus_set_fd_nonblocking (fds[1], NULL)))
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Could not set full-duplex pipe nonblocking");
      
      close (fds[0]);
      close (fds[1]);
      
      return FALSE;
    }
  
  *fd1 = fds[0];
  *fd2 = fds[1];
  
  return TRUE;  
#else
  _dbus_warn ("_dbus_full_duplex_pipe() not implemented on this OS\n");
  dbus_set_error (error, DBUS_ERROR_FAILED,
                  "_dbus_full_duplex_pipe() not implemented on this OS");
  return FALSE;
#endif
}

/**
 * Closes a file descriptor.
 *
 * @param fd the file descriptor
 * @param error error object
 * @returns #FALSE if error set
 */
dbus_bool_t
_dbus_close (int        fd,
             DBusError *error)
{
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
 again:
  if (close (fd) < 0)
    {
      if (errno == EINTR)
        goto again;

      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Could not close fd %d", fd);
      return FALSE;
    }

  return TRUE;
}

/**
 * Sets a file descriptor to be nonblocking.
 *
 * @param fd the file descriptor.
 * @param error address of error location.
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_set_fd_nonblocking (int             fd,
                          DBusError      *error)
{
  int val;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  val = fcntl (fd, F_GETFL, 0);
  if (val < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to get flags from file descriptor %d: %s",
                      fd, _dbus_strerror (errno));
      _dbus_verbose ("Failed to get flags for fd %d: %s\n", fd,
                     _dbus_strerror (errno));
      return FALSE;
    }

  if (fcntl (fd, F_SETFL, val | O_NONBLOCK) < 0)
    {
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to set nonblocking flag of file descriptor %d: %s",
                      fd, _dbus_strerror (errno));
      _dbus_verbose ("Failed to set fd %d nonblocking: %s\n",
                     fd, _dbus_strerror (errno));

      return FALSE;
    }

  return TRUE;
}

/**
 * On GNU libc systems, print a crude backtrace to the verbose log.
 * On other systems, print "no backtrace support"
 *
 */
void
_dbus_print_backtrace (void)
{
#if defined (HAVE_BACKTRACE) && defined (DBUS_ENABLE_VERBOSE_MODE)
  void *bt[500];
  int bt_size;
  int i;
  char **syms;
  
  bt_size = backtrace (bt, 500);

  syms = backtrace_symbols (bt, bt_size);
  
  i = 0;
  while (i < bt_size)
    {
      _dbus_verbose ("  %s\n", syms[i]);
      ++i;
    }

  free (syms);
#else
  _dbus_verbose ("  D-BUS not compiled with backtrace support\n");
#endif
}

/**
 * Does the chdir, fork, setsid, etc. to become a daemon process.
 *
 * @param error return location for errors
 * @returns #FALSE on failure
 */
dbus_bool_t
_dbus_become_daemon (DBusError *error)
{
  const char *s;

  /* This is so we don't prevent unmounting of devices. We divert
   * all messages to syslog
   */
  if (chdir ("/") < 0)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Could not chdir() to root directory");
      return FALSE;
    }

  s = _dbus_getenv ("DBUS_DEBUG_OUTPUT");
  if (s == NULL || *s == '\0')
    {
      int dev_null_fd;

      /* silently ignore failures here, if someone
       * doesn't have /dev/null we may as well try
       * to continue anyhow
       */

      dev_null_fd = open ("/dev/null", O_RDWR);
      if (dev_null_fd >= 0)
        {
         dup2 (dev_null_fd, 0);
         dup2 (dev_null_fd, 1);
         dup2 (dev_null_fd, 2);
       }
    }

  /* Get a predictable umask */
  umask (022);

  switch (fork ())
    {
    case -1:
      dbus_set_error (error, _dbus_error_from_errno (errno),
                      "Failed to fork daemon: %s", _dbus_strerror (errno));
      return FALSE;
      break;

    case 0:      
      break;

    default:
      _exit (0);
      break;
    }

  if (setsid () == -1)
    _dbus_assert_not_reached ("setsid() failed");
  
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
_dbus_change_identity  (unsigned long  uid,
                        unsigned long  gid,
                        DBusError     *error)
{
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
  sigaction (sig,  &act, 0);
}


#ifdef DBUS_BUILD_TESTS
#include <stdlib.h>
static void
check_dirname (const char *filename,
               const char *dirname)
{
  DBusString f, d;
  
  _dbus_string_init_const (&f, filename);

  if (!_dbus_string_init (&d))
    _dbus_assert_not_reached ("no memory");

  if (!_dbus_string_get_dirname (&f, &d))
    _dbus_assert_not_reached ("no memory");

  if (!_dbus_string_equal_c_str (&d, dirname))
    {
      _dbus_warn ("For filename \"%s\" got dirname \"%s\" and expected \"%s\"\n",
                  filename,
                  _dbus_string_get_const_data (&d),
                  dirname);
      exit (1);
    }

  _dbus_string_free (&d);
}

static void
check_path_absolute (const char *path,
                     dbus_bool_t expected)
{
  DBusString p;

  _dbus_string_init_const (&p, path);

  if (_dbus_path_is_absolute (&p) != expected)
    {
      _dbus_warn ("For path \"%s\" expected absolute = %d got %d\n",
                  path, expected, _dbus_path_is_absolute (&p));
      exit (1);
    }
}

/**
 * Unit test for dbus-sysdeps.c.
 * 
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_sysdeps_test (void)
{
  check_dirname ("foo", ".");
  check_dirname ("foo/bar", "foo");
  check_dirname ("foo//bar", "foo");
  check_dirname ("foo///bar", "foo");
  check_dirname ("foo/bar/", "foo");
  check_dirname ("foo//bar/", "foo");
  check_dirname ("foo///bar/", "foo");
  check_dirname ("foo/bar//", "foo");
  check_dirname ("foo//bar////", "foo");
  check_dirname ("foo///bar///////", "foo");
  check_dirname ("/foo", "/");
  check_dirname ("////foo", "/");
  check_dirname ("/foo/bar", "/foo");
  check_dirname ("/foo//bar", "/foo");
  check_dirname ("/foo///bar", "/foo");
  check_dirname ("/", "/");
  check_dirname ("///", "/");
  check_dirname ("", ".");  

  check_path_absolute ("/", TRUE);
  check_path_absolute ("/foo", TRUE);
  check_path_absolute ("", FALSE);
  check_path_absolute ("foo", FALSE);
  check_path_absolute ("foo/bar", FALSE);
  
  return TRUE;
}
#endif /* DBUS_BUILD_TESTS */

/** @} end of sysdeps */

