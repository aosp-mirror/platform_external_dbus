/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/* dbus-nonce.c  Nonce handling functions used by nonce-tcp (internal to D-Bus implementation)
 *
 * Copyright (C) 2009 Klaralvdalens Datakonsult AB, a KDAB Group company, info@kdab.net
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

// major sections of this file are modified code from libassuan, (C) FSF
#include "dbus-nonce.h"
#include "dbus-internals.h"
#include "dbus-protocol.h"
#include "dbus-sysdeps.h"

#include <stdio.h>

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifndef ENOFILE
# define ENOFILE ENOENT
#endif

dbus_bool_t
_dbus_check_nonce (int fd, const DBusString *nonce)
{
  DBusString buffer;
  DBusString p;
  size_t nleft;
  dbus_bool_t result;
  int n;

  nleft = 16;

  _dbus_string_init (&buffer);
  _dbus_string_init (&p);
//PENDING(kdab) replace errno by DBusError
  while (nleft)
    {
      n = _dbus_read_socket (fd, &p, nleft);
      if (n == -1 && _dbus_get_is_errno_eintr())
        ;
      else if (n == -1 && _dbus_get_is_errno_eagain_or_ewouldblock())
        _dbus_sleep_milliseconds (100);
      else if (n==-1)
        {
          _dbus_string_free (&p);
          _dbus_string_free (&buffer);
          return FALSE;
        }
      else if (!n)
        {
        _dbus_string_free (&p);
        _dbus_string_free (&buffer);
          errno = EIO;
          return FALSE;
        }
      else
        {
          _dbus_string_append_len(&buffer, _dbus_string_get_const_data (&p), n);
          nleft -= n;
        }
    }

  result =  _dbus_string_equal_len (&buffer, nonce, 16);
  if (!result)
      errno = EACCES;

  _dbus_string_free (&p);
  _dbus_string_free (&buffer);

  return result;
}

//PENDING(kdab) document
dbus_bool_t
_dbus_read_nonce (const DBusString *fname, DBusString *nonce)
{
  //PENDING(kdab) replace errno by DBusError
  FILE *fp;
  char buffer[17];
  buffer[sizeof buffer - 1] = '\0';
  size_t nread;
  _dbus_verbose ("reading nonce from file: %s\n", _dbus_string_get_const_data (fname));


  fp = fopen (_dbus_string_get_const_data (fname), "rb");
  if (!fp)
    return FALSE;
  nread = fread (buffer, 1, sizeof buffer - 1, fp);
  fclose (fp);
  if (!nread)
    {
      errno = ENOFILE;
      return FALSE;
    }

  if (!_dbus_string_append_len (nonce, buffer, sizeof buffer - 1 ))
    {
      errno = ENOMEM;
      return FALSE;
    }
  return TRUE;
}

int
_dbus_accept_with_nonce (int listen_fd, const DBusString *nonce)
{
  _dbus_assert (nonce != NULL);
  int fd;
  fd = _dbus_accept (listen_fd);
  if (_dbus_socket_is_invalid (fd))
    return fd;
  if (_dbus_check_nonce(fd, nonce) != TRUE) {
    _dbus_verbose ("nonce check failed. Closing socket.\n");
    _dbus_close_socket(fd, NULL);
    return -1;
  }

  return fd;
}

int
_dbus_accept_with_noncefile (int listen_fd, const DBusString *noncefile)
{
  _dbus_assert (noncefile != NULL);
  DBusString nonce;
  _dbus_string_init (&nonce);
  //PENDING(kdab): set better errors
  if (_dbus_read_nonce (noncefile, &nonce) != TRUE)
    return -1;
  return _dbus_accept_with_nonce (listen_fd, &nonce);
}

dbus_bool_t
_dbus_generate_noncefilename (DBusString *buf, DBusError *error)
{
  dbus_bool_t ret;
  DBusString randomStr;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  ret = _dbus_string_init (&randomStr);
  if (!ret)
    goto oom;
  ret = _dbus_generate_random_ascii (&randomStr, 8);
  if (!ret)
    goto oom;
  if (!_dbus_string_append (buf, _dbus_get_tmpdir())
      || !_dbus_string_append (buf, DBUS_DIR_SEPARATOR "dbus_nonce-")
      || !_dbus_string_append (buf, _dbus_string_get_const_data (&randomStr)) )
    goto oom;

  _dbus_string_free (&randomStr);
  return TRUE;
oom:
  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
  _dbus_string_free (&randomStr);
  return FALSE;
}

dbus_bool_t
_dbus_generate_and_write_nonce (const DBusString *filename, DBusError *error)
{
  DBusString nonce;
  dbus_bool_t ret;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  _dbus_string_init (&nonce);

  if (!_dbus_generate_random_bytes (&nonce, 16))
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      _dbus_string_free (&nonce);
      return FALSE;
    }

  ret = _dbus_string_save_to_file (filename, &nonce, error);

  _dbus_string_free (&nonce);

  return ret;
}

dbus_bool_t
_dbus_send_nonce(int fd, const DBusString *noncefile, DBusError *error)
{
  dbus_bool_t read_result;
  int send_result;
  size_t sendLen;
  DBusString nonce;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (_dbus_string_get_length (noncefile) == 0)
    return FALSE;

  if ( !_dbus_string_init (&nonce) )
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return FALSE;
  }

  read_result = _dbus_read_nonce (noncefile, &nonce);

  if (!read_result)
    {
      dbus_set_error (error,
                      _dbus_error_from_errno (errno),
                      "Could not read nonce from file %s (%s)",
                      _dbus_string_get_const_data (noncefile), _dbus_strerror(errno));
      _dbus_string_free (&nonce);
      return FALSE;
    }

  send_result = _dbus_write_socket (fd, &nonce, 0, _dbus_string_get_length (&nonce));

  _dbus_string_free (&nonce);

  if (send_result == -1)
  {
    dbus_set_error (error,
                    _dbus_error_from_errno (errno),
                    "Failed to send nonce (fd=%d): %s",
                    fd, _dbus_strerror(errno));
    return FALSE;
  }

  return TRUE;
}

/** @} end of nonce */
