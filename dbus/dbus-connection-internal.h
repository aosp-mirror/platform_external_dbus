/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-connection-internal.h DBusConnection internal interfaces
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
#ifndef DBUS_CONNECTION_INTERNAL_H
#define DBUS_CONNECTION_INTERNAL_H

#include <dbus/dbus-internals.h>
#include <dbus/dbus-connection.h>
#include <dbus/dbus-message.h>
#include <dbus/dbus-transport.h>
#include <dbus/dbus-resources.h>
#include <dbus/dbus-list.h>
#include <dbus/dbus-timeout.h>
#include <dbus/dbus-dataslot.h>

DBUS_BEGIN_DECLS

typedef enum
{
  DBUS_ITERATION_DO_WRITING = 1 << 0, /**< Write messages out. */
  DBUS_ITERATION_DO_READING = 1 << 1, /**< Read messages in. */
  DBUS_ITERATION_BLOCK      = 1 << 2  /**< Block if nothing to do. */
} DBusIterationFlags;

/** default timeout value when waiting for a message reply, 25 seconds */
#define _DBUS_DEFAULT_TIMEOUT_VALUE (25 * 1000)

void              _dbus_connection_lock                        (DBusConnection     *connection);
void              _dbus_connection_unlock                      (DBusConnection     *connection);
DBusConnection *  _dbus_connection_ref_unlocked                (DBusConnection     *connection);
void              _dbus_connection_unref_unlocked              (DBusConnection     *connection);
dbus_bool_t       _dbus_connection_queue_received_message      (DBusConnection     *connection,
                                                                DBusMessage        *message);
void              _dbus_connection_queue_received_message_link (DBusConnection     *connection,
                                                                DBusList           *link);
dbus_bool_t       _dbus_connection_has_messages_to_send_unlocked (DBusConnection     *connection);
DBusMessage*      _dbus_connection_get_message_to_send         (DBusConnection     *connection);
void              _dbus_connection_message_sent                (DBusConnection     *connection,
                                                                DBusMessage        *message);
dbus_bool_t       _dbus_connection_add_watch                   (DBusConnection     *connection,
                                                                DBusWatch          *watch);
void              _dbus_connection_remove_watch                (DBusConnection     *connection,
                                                                DBusWatch          *watch);
void              _dbus_connection_toggle_watch                (DBusConnection     *connection,
                                                                DBusWatch          *watch,
                                                                dbus_bool_t         enabled);
dbus_bool_t       _dbus_connection_handle_watch                (DBusWatch          *watch,
                                                                unsigned int        condition,
                                                                void               *data);
dbus_bool_t       _dbus_connection_add_timeout                 (DBusConnection     *connection,
                                                                DBusTimeout        *timeout);
void              _dbus_connection_remove_timeout              (DBusConnection     *connection,
                                                                DBusTimeout        *timeout);
void              _dbus_connection_toggle_timeout              (DBusConnection     *connection,
                                                                DBusTimeout        *timeout,
                                                                dbus_bool_t         enabled);
DBusConnection*   _dbus_connection_new_for_transport           (DBusTransport      *transport);
void              _dbus_connection_do_iteration_unlocked       (DBusConnection     *connection,
                                                                unsigned int        flags,
                                                                int                 timeout_milliseconds);

DBusPendingCall*  _dbus_pending_call_new                       (DBusConnection     *connection,
                                                                int                 timeout_milliseconds,
                                                                DBusTimeoutHandler  timeout_handler);
void              _dbus_pending_call_notify                    (DBusPendingCall    *pending);
void              _dbus_connection_remove_pending_call         (DBusConnection     *connection,
                                                                DBusPendingCall    *pending);
void              _dbus_connection_block_pending_call          (DBusPendingCall    *pending);
void              _dbus_pending_call_complete_and_unlock       (DBusPendingCall    *pending,
                                                                DBusMessage        *message);
dbus_bool_t       _dbus_connection_send_and_unlock             (DBusConnection     *connection,
                                                                DBusMessage        *message,
                                                                dbus_uint32_t      *client_serial);

/**
 * @addtogroup DBusPendingCallInternals DBusPendingCall implementation details
 * @{
 */
/**
 * @brief Internals of DBusPendingCall
 *
 * Object representing a reply message that we're waiting for.
 */
struct DBusPendingCall
{
  DBusAtomic refcount;                            /**< reference count */

  DBusDataSlotList slot_list;                     /**< Data stored by allocated integer ID */
  
  DBusPendingCallNotifyFunction function;         /**< Notifier when reply arrives. */

  DBusConnection *connection;                     /**< Connections we're associated with */
  DBusMessage *reply;                             /**< Reply (after we've received it) */
  DBusTimeout *timeout;                           /**< Timeout */

  DBusList *timeout_link;                         /**< Preallocated timeout response */
  
  dbus_uint32_t reply_serial;                     /**< Expected serial of reply */

  unsigned int completed : 1;                     /**< TRUE if completed */
  unsigned int timeout_added : 1;                 /**< Have added the timeout */
};

/** @} End of DBusPendingCallInternals */


DBUS_END_DECLS

#endif /* DBUS_CONNECTION_INTERNAL_H */
