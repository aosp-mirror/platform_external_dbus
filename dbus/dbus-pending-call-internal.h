/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-pending-call-internal.h DBusPendingCall internal interfaces
 *
 * Copyright (C) 2002  Red Hat Inc.
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
#ifndef DBUS_PENDING_CALL_INTERNAL_H
#define DBUS_PENDING_CALL_INTERNAL_H


#include <dbus/dbus-internals.h>
#include <dbus/dbus-message.h>
#include <dbus/dbus-connection.h>
#include <dbus/dbus-list.h>

DBUS_BEGIN_DECLS

dbus_bool_t     _dbus_pending_call_is_timeout_added  (DBusPendingCall  *pending);
void            _dbus_pending_call_set_timeout_added (DBusPendingCall  *pending,
                                                      dbus_bool_t       is_added);
DBusTimeout    *_dbus_pending_call_get_timeout       (DBusPendingCall  *pending);
dbus_uint32_t   _dbus_pending_call_get_reply_serial  (DBusPendingCall  *pending);
void            _dbus_pending_call_set_reply_serial  (DBusPendingCall *pending,
                                                      dbus_uint32_t serial);
DBusConnection *_dbus_pending_call_get_connection    (DBusPendingCall *pending);
void            _dbus_pending_call_set_connection    (DBusPendingCall *pending,
                                                     DBusConnection *connection);

void              _dbus_pending_call_complete                  (DBusPendingCall    *pending);
void              _dbus_pending_call_set_reply                 (DBusPendingCall    *pending,
                                                                DBusMessage        *message);
void              _dbus_pending_call_clear_connection          (DBusPendingCall *pending);

void              _dbus_pending_call_queue_timeout_error       (DBusPendingCall *pending, 
                                                                DBusConnection  *connection);
void		  _dbus_pending_call_set_reply_serial  (DBusPendingCall *pending,
                                                        dbus_uint32_t serial);
dbus_bool_t       _dbus_pending_call_set_timeout_error (DBusPendingCall *pending,
                                                        DBusMessage *message,
                                                        dbus_uint32_t serial);
DBusPendingCall*  _dbus_pending_call_new               (DBusConnection     *connection,
                                                        int                 timeout_milliseconds,
                                                        DBusTimeoutHandler  timeout_handler);


DBUS_END_DECLS

#endif /* DBUS_PENDING_CALL_INTERNAL_H */
