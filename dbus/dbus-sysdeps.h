/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-sysdeps.h Wrappers around system/libc features (internal to D-BUS implementation)
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

#ifndef DBUS_SYSDEPS_H
#define DBUS_SYSDEPS_H

#include <dbus/dbus-string.h>
#include <dbus/dbus-errors.h>

/* this is perhaps bogus, but strcmp() etc. are faster if we use the
 * stuff straight out of string.h, so have this here for now.
 */
#include <string.h>

/* and it would just be annoying to abstract this */
#include <errno.h>

DBUS_BEGIN_DECLS;

/* The idea of this file is to encapsulate everywhere that we're
 * relying on external libc features, for ease of security
 * auditing. The idea is from vsftpd. This also gives us a chance to
 * make things more convenient to use, e.g.  by reading into a
 * DBusString. Operating system headers aren't intended to be used
 * outside of this file and a limited number of others (such as
 * dbus-memory.c)
 */

void _dbus_abort (void);

const char* _dbus_getenv (const char *varname);
dbus_bool_t _dbus_setenv (const char *varname,
			  const char *value);

int _dbus_read      (int               fd,
                     DBusString       *buffer,
                     int               count);
int _dbus_write     (int               fd,
                     const DBusString *buffer,
                     int               start,
                     int               len);
int _dbus_write_two (int               fd,
                     const DBusString *buffer1,
                     int               start1,
                     int               len1,
                     const DBusString *buffer2,
                     int               start2,
                     int               len2);

typedef struct
{
  /* -1 if not available */
  int pid;
  int uid;
  int gid;
} DBusCredentials;

int _dbus_connect_unix_socket (const char     *path,
                               DBusError      *error);
int _dbus_listen_unix_socket  (const char     *path,
                               DBusError      *error);
int _dbus_connect_tcp_socket  (const char     *host,
                               dbus_uint32_t   port,
                               DBusError      *error);
int _dbus_listen_tcp_socket   (const char     *host,
                               dbus_uint32_t   port,
                               DBusError      *error);
int _dbus_accept              (int             listen_fd);

dbus_bool_t _dbus_read_credentials_unix_socket (int              client_fd,
                                                DBusCredentials *credentials,
                                                DBusError       *error);
dbus_bool_t _dbus_send_credentials_unix_socket (int              server_fd,
                                                DBusError       *error);


dbus_bool_t _dbus_credentials_from_username        (const DBusString      *username,
                                                    DBusCredentials       *credentials);
dbus_bool_t _dbus_credentials_from_user_id         (unsigned long          user_id,
                                                    DBusCredentials       *credentials);
dbus_bool_t _dbus_credentials_from_uid_string      (const DBusString      *uid_str,
                                                    DBusCredentials       *credentials);
void        _dbus_credentials_from_current_process (DBusCredentials       *credentials);
dbus_bool_t _dbus_credentials_match                (const DBusCredentials *expected_credentials,
                                                    const DBusCredentials *provided_credentials);

dbus_bool_t _dbus_string_append_our_uid (DBusString *str);

dbus_bool_t _dbus_homedir_from_username          (const DBusString       *username,
                                                  DBusString             *homedir);
dbus_bool_t _dbus_user_info_from_current_process (const DBusString      **username,
                                                  const DBusString      **homedir,
                                                  const DBusCredentials **credentials);

dbus_bool_t _dbus_get_group_id (const DBusString  *group_name,
                                unsigned long     *gid);
dbus_bool_t _dbus_get_groups   (unsigned long      uid,
                                unsigned long    **group_ids,
                                int               *n_group_ids);

typedef int dbus_atomic_t;

dbus_atomic_t _dbus_atomic_inc (dbus_atomic_t *atomic);
dbus_atomic_t _dbus_atomic_dec (dbus_atomic_t *atomic);

#define _DBUS_POLLIN      0x0001    /* There is data to read */
#define _DBUS_POLLPRI     0x0002    /* There is urgent data to read */
#define _DBUS_POLLOUT     0x0004    /* Writing now will not block */
#define _DBUS_POLLERR     0x0008    /* Error condition */
#define _DBUS_POLLHUP     0x0010    /* Hung up */
#define _DBUS_POLLNVAL    0x0020    /* Invalid request: fd not open */

typedef struct
{
  int fd;
  short events;
  short revents;
} DBusPollFD;

int _dbus_poll (DBusPollFD *fds,
                int         n_fds,
                int         timeout_milliseconds);

void _dbus_sleep_milliseconds (int milliseconds);

void _dbus_get_current_time (long *tv_sec,
                             long *tv_usec);


dbus_bool_t _dbus_file_get_contents   (DBusString       *str,
                                       const DBusString *filename,
                                       DBusError        *error);
dbus_bool_t _dbus_string_save_to_file (const DBusString *str,
                                       const DBusString *filename,
                                       DBusError        *error);

dbus_bool_t    _dbus_create_file_exclusively (const DBusString *filename,
                                              DBusError        *error);
dbus_bool_t    _dbus_delete_file             (const DBusString *filename,
                                              DBusError        *error);
dbus_bool_t    _dbus_create_directory        (const DBusString *filename,
                                              DBusError        *error);

dbus_bool_t _dbus_concat_dir_and_file (DBusString       *dir,
                                       const DBusString *next_component);

typedef struct DBusDirIter DBusDirIter;

DBusDirIter* _dbus_directory_open          (const DBusString *filename,
                                            DBusError        *error);
dbus_bool_t  _dbus_directory_get_next_file (DBusDirIter      *iter,
                                            DBusString       *filename,
                                            DBusError        *error);
void         _dbus_directory_close         (DBusDirIter      *iter);


dbus_bool_t _dbus_generate_random_bytes (DBusString *str,
                                         int         n_bytes);

const char *_dbus_errno_to_string  (int errnum);
const char* _dbus_error_from_errno (int error_number);

typedef void (* DBusSpawnChildSetupFunc) (void *user_data);

dbus_bool_t _dbus_spawn_async (char                    **argv,
			       DBusSpawnChildSetupFunc   child_setup,
			       void                     *user_data,
			       DBusError                *error);


void _dbus_disable_sigpipe (void);

void _dbus_fd_set_close_on_exec (int fd);

void _dbus_exit (int code);

typedef struct
{
  unsigned long mode;
  unsigned long nlink;
  unsigned long uid;
  unsigned long gid;
  unsigned long size;
  unsigned long atime;
  unsigned long mtime;
  unsigned long ctime;
} DBusStat;

dbus_bool_t _dbus_stat             (const DBusString *filename,
                                    DBusStat         *statbuf,
                                    DBusError        *error);
dbus_bool_t _dbus_full_duplex_pipe (int              *fd1,
                                    int              *fd2,
                                    DBusError        *error);
dbus_bool_t _dbus_close            (int               fd,
                                    DBusError        *error);

void        _dbus_print_backtrace  (void);

dbus_bool_t _dbus_become_daemon    (DBusError *error);

dbus_bool_t _dbus_change_identity  (unsigned long  uid,
                                    unsigned long  gid,
                                    DBusError     *error);

DBUS_END_DECLS;

#endif /* DBUS_SYSDEPS_H */
