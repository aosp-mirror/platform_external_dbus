/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-auth.h Authentication
 *
 * Copyright (C) 2002  Red Hat Inc.
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
#ifndef DBUS_AUTH_H
#define DBUS_AUTH_H

#include <dbus/dbus-macros.h>
#include <dbus/dbus-errors.h>
#include <dbus/dbus-string.h>

DBUS_BEGIN_DECLS;

typedef struct DBusAuth DBusAuth;

typedef enum
{
  DBUS_AUTH_STATE_WAITING_FOR_INPUT,
  DBUS_AUTH_STATE_WAITING_FOR_MEMORY,
  DBUS_AUTH_STATE_HAVE_BYTES_TO_SEND,
  DBUS_AUTH_STATE_NEED_DISCONNECT,
  DBUS_AUTH_STATE_AUTHENTICATED
} DBusAuthState;

DBusAuth*     _dbus_auth_server_new        (void);
DBusAuth*     _dbus_auth_client_new        (void);
void          _dbus_auth_ref               (DBusAuth         *auth);
void          _dbus_auth_unref             (DBusAuth         *auth);
DBusAuthState _dbus_auth_do_work           (DBusAuth         *auth);
dbus_bool_t   _dbus_auth_get_bytes_to_send (DBusAuth         *auth,
                                            DBusString       *str);
dbus_bool_t   _dbus_auth_bytes_received    (DBusAuth         *auth,
                                            const DBusString *str);
dbus_bool_t   _dbus_auth_get_unused_bytes  (DBusAuth         *auth,
                                            DBusString       *str);
dbus_bool_t   _dbus_auth_needs_encoding    (DBusAuth         *auth);
dbus_bool_t   _dbus_auth_encode_data       (DBusAuth         *auth,
                                            const DBusString *plaintext,
                                            DBusString       *encoded);
dbus_bool_t   _dbus_auth_needs_decoding    (DBusAuth         *auth);
dbus_bool_t   _dbus_auth_decode_data       (DBusAuth         *auth,
                                            const DBusString *encoded,
                                            DBusString       *plaintext);


DBUS_END_DECLS;

#endif /* DBUS_AUTH_H */
