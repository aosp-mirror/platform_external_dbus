/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-sysdeps.h Wrappers around system/libc features (internal to D-Bus implementation)
 * 
 * Copyright (C) 2002, 2003  Red Hat, Inc.
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

#ifndef DBUS_SYSDEPS_H
#define DBUS_SYSDEPS_H

#include <config.h>

#include <dbus/dbus-errors.h>

/* this is perhaps bogus, but strcmp() etc. are faster if we use the
 * stuff straight out of string.h, so have this here for now.
 */
#include <string.h>

/* and it would just be annoying to abstract this */
#include <errno.h>

#include <stdarg.h>

DBUS_BEGIN_DECLS

/* Forward declarations */

/** An opaque string type */
typedef struct DBusString DBusString;

/** An opaque list type */
typedef struct DBusList DBusList;

/**
 * @addtogroup DBusSysdeps
 *
 * @{
 */

/* The idea of this file is to encapsulate everywhere that we're
 * relying on external libc features, for ease of security
 * auditing. The idea is from vsftpd. This also gives us a chance to
 * make things more convenient to use, e.g.  by reading into a
 * DBusString. Operating system headers aren't intended to be used
 * outside of this file and a limited number of others (such as
 * dbus-memory.c)
 */

#if     __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
#define _DBUS_GNUC_PRINTF( format_idx, arg_idx )    \
  __attribute__((__format__ (__printf__, format_idx, arg_idx)))
#define _DBUS_GNUC_NORETURN                         \
  __attribute__((__noreturn__))
#else   /* !__GNUC__ */
#define _DBUS_GNUC_PRINTF( format_idx, arg_idx )
#define _DBUS_GNUC_NORETURN
#endif  /* !__GNUC__ */

/** @def _DBUS_GNUC_PRINTF
 * used to tell gcc about printf format strings
 */
/** @def _DBUS_GNUC_NORETURN
 * used to tell gcc about functions that never return, such as _dbus_abort()
 */

void _dbus_abort (void) _DBUS_GNUC_NORETURN;

const char* _dbus_getenv (const char *varname);
dbus_bool_t _dbus_setenv (const char *varname,
			  const char *value);

/** A process ID */
typedef unsigned long dbus_pid_t;
/** A user ID */
typedef unsigned long dbus_uid_t;
/** A group ID */
typedef unsigned long dbus_gid_t;

/** an invalid PID used to represent an uninitialized dbus_pid_t field */
#define DBUS_PID_UNSET ((dbus_pid_t) -1)
/** an invalid UID used to represent an uninitialized dbus_uid_t field */
#define DBUS_UID_UNSET ((dbus_uid_t) -1)
/** an invalid GID used to represent an uninitialized dbus_gid_t field */
#define DBUS_GID_UNSET ((dbus_gid_t) -1)

/** an appropriate printf format for dbus_pid_t */
#define DBUS_PID_FORMAT "%lu"
/** an appropriate printf format for dbus_uid_t */
#define DBUS_UID_FORMAT "%lu"
/** an appropriate printf format for dbus_gid_t */
#define DBUS_GID_FORMAT "%lu"


/**
 * Socket interface
 *
 *  @todo Use for the file descriptors a struct
 *           - struct DBusSocket{ int d; }; -
 *        instead of int to get type-safety which 
 *        will be checked by the compiler.
 * 
 */

dbus_bool_t _dbus_open_tcp_socket  (int              *fd,
                                    DBusError        *error);
dbus_bool_t _dbus_close_socket     (int               fd,
                                    DBusError        *error);
int         _dbus_read_socket      (int               fd,
                                    DBusString       *buffer,
                                    int               count);
int         _dbus_write_socket     (int               fd,
                                    const DBusString *buffer,
                                    int               start,
                                    int               len);
int         _dbus_write_socket_two (int               fd,
                                    const DBusString *buffer1,
                                    int               start1,
                                    int               len1,
                                    const DBusString *buffer2,
                                    int               start2,
                                    int               len2);
int _dbus_connect_tcp_socket  (const char     *host,
                               dbus_uint32_t   port,
                               DBusError      *error);
int _dbus_listen_tcp_socket   (const char     *host,
                               dbus_uint32_t   port,
                               DBusError      *error);
int _dbus_accept              (int             listen_fd);

/**
 * Struct representing socket credentials
 */
typedef struct
{
  dbus_pid_t pid; /**< process ID or DBUS_PID_UNSET */
  dbus_uid_t uid; /**< user ID or DBUS_UID_UNSET */
  dbus_gid_t gid; /**< group ID or DBUS_GID_UNSET */
} DBusCredentials;

/* FIXME these read/send credentials should get moved to sysdeps-unix.h,
 * or renamed to reflect what they mean cross-platform
 */
dbus_bool_t _dbus_read_credentials_unix_socket (int              client_fd,
                                                DBusCredentials *credentials,
                                                DBusError       *error);
dbus_bool_t _dbus_send_credentials_unix_socket (int              server_fd,
                                                DBusError       *error);


void        _dbus_credentials_clear                (DBusCredentials       *credentials);
void        _dbus_credentials_from_current_process (DBusCredentials       *credentials);
dbus_bool_t _dbus_credentials_match                (const DBusCredentials *expected_credentials,
                                                    const DBusCredentials *provided_credentials);


/** Information about a UNIX user */
typedef struct DBusUserInfo  DBusUserInfo;
/** Information about a UNIX group */
typedef struct DBusGroupInfo DBusGroupInfo;

/**
 * Information about a UNIX user
 */
struct DBusUserInfo
{
  dbus_uid_t  uid;            /**< UID */
  dbus_gid_t  primary_gid;    /**< GID */
  dbus_gid_t *group_ids;      /**< Groups IDs, *including* above primary group */
  int         n_group_ids;    /**< Size of group IDs array */
  char       *username;       /**< Username */
  char       *homedir;        /**< Home directory */
};

/**
 * Information about a UNIX group
 */
struct DBusGroupInfo
{
  dbus_gid_t  gid;            /**< GID */
  char       *groupname;      /**< Group name */
};

dbus_bool_t _dbus_user_info_fill     (DBusUserInfo     *info,
                                      const DBusString *username,
                                      DBusError        *error);
dbus_bool_t _dbus_user_info_fill_uid (DBusUserInfo     *info,
                                      dbus_uid_t        uid,
                                      DBusError        *error);
void        _dbus_user_info_free     (DBusUserInfo     *info);

dbus_bool_t _dbus_group_info_fill     (DBusGroupInfo    *info,
                                       const DBusString *groupname,
                                       DBusError        *error);
dbus_bool_t _dbus_group_info_fill_gid (DBusGroupInfo    *info,
                                       dbus_gid_t        gid,
                                       DBusError        *error);
void        _dbus_group_info_free     (DBusGroupInfo    *info);


unsigned long _dbus_getpid (void);
dbus_uid_t    _dbus_getuid (void);
dbus_gid_t    _dbus_getgid (void);

/** Opaque type representing an atomically-modifiable integer
 * that can be used from multiple threads.
 */
typedef struct DBusAtomic DBusAtomic;

/**
 * An atomic integer safe to increment or decrement from multiple threads.
 */
struct DBusAtomic
{
  volatile dbus_int32_t value; /**< Value of the atomic integer. */
};

dbus_int32_t _dbus_atomic_inc (DBusAtomic *atomic);
dbus_int32_t _dbus_atomic_dec (DBusAtomic *atomic);

/** There is data to read */
#define _DBUS_POLLIN      0x0001
/** There is urgent data to read */
#define _DBUS_POLLPRI     0x0002
/** Writing now will not block */
#define _DBUS_POLLOUT     0x0004
/** Error condition */
#define _DBUS_POLLERR     0x0008
/** Hung up */
#define _DBUS_POLLHUP     0x0010
/** Invalid request: fd not open */
#define _DBUS_POLLNVAL    0x0020

/**
 * A portable struct pollfd wrapper. 
 */
typedef struct
{
  int fd;            /**< File descriptor */
  short events;      /**< Events to poll for */
  short revents;     /**< Events that occurred */
} DBusPollFD;

int _dbus_poll (DBusPollFD *fds,
                int         n_fds,
                int         timeout_milliseconds);

void _dbus_sleep_milliseconds (int milliseconds);

void _dbus_get_current_time (long *tv_sec,
                             long *tv_usec);

/**
 * File/directory interface
 */
dbus_bool_t _dbus_file_exists         (const char       *file);
dbus_bool_t _dbus_file_get_contents   (DBusString       *str,
                                       const DBusString *filename,
                                       DBusError        *error);
dbus_bool_t _dbus_string_save_to_file (const DBusString *str,
                                       const DBusString *filename,
                                       DBusError        *error);

dbus_bool_t _dbus_make_file_world_readable   (const DBusString *filename,
                                              DBusError *error);

dbus_bool_t    _dbus_create_file_exclusively (const DBusString *filename,
                                              DBusError        *error);
dbus_bool_t    _dbus_delete_file             (const DBusString *filename,
                                              DBusError        *error);
dbus_bool_t    _dbus_create_directory        (const DBusString *filename,
                                              DBusError        *error);
dbus_bool_t    _dbus_delete_directory        (const DBusString *filename,
					      DBusError        *error);

dbus_bool_t _dbus_concat_dir_and_file (DBusString       *dir,
                                       const DBusString *next_component);
dbus_bool_t _dbus_string_get_dirname  (const DBusString *filename,
                                       DBusString       *dirname);
dbus_bool_t _dbus_path_is_absolute    (const DBusString *filename);

dbus_bool_t _dbus_get_standard_session_servicedirs (DBusList **dirs);

/** Opaque type for reading a directory listing */
typedef struct DBusDirIter DBusDirIter;

DBusDirIter* _dbus_directory_open          (const DBusString *filename,
                                            DBusError        *error);
dbus_bool_t  _dbus_directory_get_next_file (DBusDirIter      *iter,
                                            DBusString       *filename,
                                            DBusError        *error);
void         _dbus_directory_close         (DBusDirIter      *iter);

dbus_bool_t  _dbus_check_dir_is_private_to_user    (DBusString *dir,
                                                    DBusError *error);

void _dbus_fd_set_close_on_exec (int fd);

const char* _dbus_get_tmpdir      (void);

/**
 * Random numbers 
 */
void        _dbus_generate_pseudorandom_bytes_buffer (char *buffer,
                                                      int   n_bytes);
void        _dbus_generate_random_bytes_buffer (char       *buffer,
                                                int         n_bytes);
dbus_bool_t _dbus_generate_random_bytes        (DBusString *str,
                                                int         n_bytes);
dbus_bool_t _dbus_generate_random_ascii        (DBusString *str,
                                                int         n_bytes);

const char* _dbus_error_from_errno (int error_number);

void _dbus_disable_sigpipe (void);


void _dbus_exit (int code) _DBUS_GNUC_NORETURN;

int _dbus_printf_string_upper_bound (const char *format,
                                     va_list args);


/**
 * Portable struct with stat() results
 */
typedef struct
{
  unsigned long mode;  /**< File mode */
  unsigned long nlink; /**< Number of hard links */
  dbus_uid_t    uid;   /**< User owning file */
  dbus_gid_t    gid;   /**< Group owning file */
  unsigned long size;  /**< Size of file */
  unsigned long atime; /**< Access time */
  unsigned long mtime; /**< Modify time */
  unsigned long ctime; /**< Creation time */
} DBusStat;

dbus_bool_t _dbus_stat             (const DBusString *filename,
                                    DBusStat         *statbuf,
                                    DBusError        *error);
dbus_bool_t _dbus_full_duplex_pipe (int              *fd1,
                                    int              *fd2,
                                    dbus_bool_t       blocking,
                                    DBusError        *error);

void        _dbus_print_backtrace  (void);

dbus_bool_t _dbus_become_daemon   (const DBusString *pidfile,
				   int               print_pid_fd,
                                   DBusError        *error);
dbus_bool_t _dbus_write_pid_file  (const DBusString *filename,
                                   unsigned long     pid,
                                   DBusError        *error);
dbus_bool_t _dbus_change_identity (unsigned long     uid,
                                   unsigned long     gid,
                                   DBusError        *error);

/** A UNIX signal handler */
typedef void (* DBusSignalHandler) (int sig);

void _dbus_set_signal_handler (int               sig,
                               DBusSignalHandler handler);

dbus_bool_t _dbus_user_at_console (const char *username,
                                   DBusError  *error);

/* Define DBUS_VA_COPY() to do the right thing for copying va_list variables. 
 * config.h may have already defined DBUS_VA_COPY as va_copy or __va_copy. 
 */
#if !defined (DBUS_VA_COPY)
#  if defined (__GNUC__) && defined (__PPC__) && (defined (_CALL_SYSV) || defined (_WIN32))
#    define DBUS_VA_COPY(ap1, ap2)   (*(ap1) = *(ap2))
#  elif defined (DBUS_VA_COPY_AS_ARRAY)
#    define DBUS_VA_COPY(ap1, ap2)   memcpy ((ap1), (ap2), sizeof (va_list))
#  else /* va_list is a pointer */
#    define DBUS_VA_COPY(ap1, ap2)   ((ap1) = (ap2))
#  endif /* va_list is a pointer */
#endif /* !DBUS_VA_COPY */


/**
 * Casts a primitive C type to a byte array and then indexes
 * a particular byte of the array.
 */
#define _DBUS_BYTE_OF_PRIMITIVE(p, i) \
    (((const char*)&(p))[(i)])
/** On x86 there is an 80-bit FPU, and if you do "a == b" it may have a
 * or b in an 80-bit register, thus failing to compare the two 64-bit
 * doubles for bitwise equality. So this macro compares the two doubles
 * bitwise.
 */
#define _DBUS_DOUBLES_BITWISE_EQUAL(a, b)                                       \
     (_DBUS_BYTE_OF_PRIMITIVE (a, 0) == _DBUS_BYTE_OF_PRIMITIVE (b, 0) &&       \
      _DBUS_BYTE_OF_PRIMITIVE (a, 1) == _DBUS_BYTE_OF_PRIMITIVE (b, 1) &&       \
      _DBUS_BYTE_OF_PRIMITIVE (a, 2) == _DBUS_BYTE_OF_PRIMITIVE (b, 2) &&       \
      _DBUS_BYTE_OF_PRIMITIVE (a, 3) == _DBUS_BYTE_OF_PRIMITIVE (b, 3) &&       \
      _DBUS_BYTE_OF_PRIMITIVE (a, 4) == _DBUS_BYTE_OF_PRIMITIVE (b, 4) &&       \
      _DBUS_BYTE_OF_PRIMITIVE (a, 5) == _DBUS_BYTE_OF_PRIMITIVE (b, 5) &&       \
      _DBUS_BYTE_OF_PRIMITIVE (a, 6) == _DBUS_BYTE_OF_PRIMITIVE (b, 6) &&       \
      _DBUS_BYTE_OF_PRIMITIVE (a, 7) == _DBUS_BYTE_OF_PRIMITIVE (b, 7))

dbus_bool_t _dbus_parse_uid (const DBusString  *uid_str,
                             dbus_uid_t        *uid);

dbus_bool_t _dbus_get_autolaunch_address (DBusString *address, 
					  DBusError *error);

/** Type representing a universally unique ID
 * @todo rename to UUID instead of GUID
 */
typedef union DBusGUID DBusGUID;

dbus_bool_t _dbus_read_local_machine_uuid   (DBusGUID         *machine_id,
                                             dbus_bool_t       create_if_not_found,
                                             DBusError        *error);

/**
 * Initialize threads as in dbus_threads_init_default(), appropriately
 * for the platform.
 * @returns #FALSE if no memory
 */
dbus_bool_t _dbus_threads_init_platform_specific (void);

/** @} */

DBUS_END_DECLS

#endif /* DBUS_SYSDEPS_H */
