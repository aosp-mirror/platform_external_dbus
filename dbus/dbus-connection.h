/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-connection.h DBusConnection object
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
#if !defined (DBUS_INSIDE_DBUS_H) && !defined (DBUS_COMPILATION)
#error "Only <dbus/dbus.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef DBUS_CONNECTION_H
#define DBUS_CONNECTION_H

#include <dbus/dbus-errors.h>
#include <dbus/dbus-message.h>
#include <dbus/dbus-memory.h>

DBUS_BEGIN_DECLS;

typedef struct DBusConnection DBusConnection;
typedef struct DBusWatch DBusWatch;
typedef struct DBusMessageHandler DBusMessageHandler;

typedef enum
{
  DBUS_HANDLER_RESULT_REMOVE_MESSAGE,     /**< Remove this message, no further processing. */
  DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS /**< Run any additional handlers that are interested in this message. */
} DBusHandlerResult;

typedef enum
{
  DBUS_WATCH_READABLE = 1 << 0, /**< As in POLLIN */
  DBUS_WATCH_WRITABLE = 1 << 1, /**< As in POLLOUT */
  DBUS_WATCH_ERROR    = 1 << 2, /**< As in POLLERR (can't watch for this, but
                                 *   the flag can be passed to dbus_connection_handle_watch()).
                                 */
  DBUS_WATCH_HANGUP   = 1 << 3  /**< As in POLLHUP (can't watch for it, but
                                 *   can be present in current state). */
} DBusWatchFlags;

typedef void (* DBusAddWatchFunction)    (DBusWatch      *watch,
                                          void           *data);

typedef void (* DBusRemoveWatchFunction) (DBusWatch      *watch,
                                          void           *data);

typedef void (* DBusDisconnectFunction)  (DBusConnection *connection,
                                          void           *data);

DBusConnection* dbus_connection_open                 (const char     *address,
                                                      DBusResultCode *result);
void            dbus_connection_ref                  (DBusConnection *connection);
void            dbus_connection_unref                (DBusConnection *connection);
void            dbus_connection_disconnect           (DBusConnection *connection);
dbus_bool_t     dbus_connection_get_is_connected     (DBusConnection *connection);
dbus_bool_t     dbus_connection_get_is_authenticated (DBusConnection *connection);
void            dbus_connection_flush                (DBusConnection *connection);
int             dbus_connection_get_n_messages       (DBusConnection *connection);
DBusMessage*    dbus_connection_peek_message         (DBusConnection *connection);
DBusMessage*    dbus_connection_pop_message          (DBusConnection *connection);
dbus_bool_t     dbus_connection_dispatch_message     (DBusConnection *connection);

dbus_bool_t dbus_connection_send_message            (DBusConnection     *connection,
                                                     DBusMessage        *message,
                                                     DBusResultCode     *result);
dbus_bool_t dbus_connection_send_message_with_reply (DBusConnection     *connection,
                                                     DBusMessage        *message,
                                                     DBusMessageHandler *reply_handler,
                                                     int                 timeout_milliseconds,
                                                     DBusResultCode     *result);

void dbus_connection_set_disconnect_function (DBusConnection          *connection,
                                              DBusDisconnectFunction   function,
                                              void                    *data,
                                              DBusFreeFunction         free_data_function);
void dbus_connection_set_watch_functions     (DBusConnection          *connection,
                                              DBusAddWatchFunction     add_function,
                                              DBusRemoveWatchFunction  remove_function,
                                              void                    *data,
                                              DBusFreeFunction         free_data_function);
void dbus_connection_handle_watch            (DBusConnection          *connection,
                                              DBusWatch               *watch,
                                              unsigned int             condition);


int          dbus_watch_get_fd    (DBusWatch        *watch);
unsigned int dbus_watch_get_flags (DBusWatch        *watch);
void*        dbus_watch_get_data  (DBusWatch        *watch);
void         dbus_watch_set_data  (DBusWatch        *watch,
                                   void             *data,
                                   DBusFreeFunction  free_data_function);


/* Handlers */
dbus_bool_t dbus_connection_add_filter         (DBusConnection      *connection,
                                                DBusMessageHandler  *handler);
void        dbus_connection_remove_filter      (DBusConnection      *connection,
                                                DBusMessageHandler  *handler);

dbus_bool_t dbus_connection_register_handler   (DBusConnection      *connection,
                                                DBusMessageHandler  *handler,
                                                const char         **messages_to_handle,
                                                int                  n_messages);
void        dbus_connection_unregister_handler (DBusConnection      *connection,
                                                DBusMessageHandler  *handler,
                                                const char         **messages_to_handle,
                                                int                  n_messages);


int         dbus_connection_allocate_data_slot (void);
void        dbus_connection_free_data_slot     (int               slot);
dbus_bool_t dbus_connection_set_data           (DBusConnection   *connection,
                                                int               slot,
                                                void             *data,
                                                DBusFreeFunction  free_data_func);
void*       dbus_connection_get_data           (DBusConnection   *connection,
                                                int               slot);

void dbus_connection_set_max_message_size       (DBusConnection *connection,
                                                 long            size);
long dbus_connection_get_max_message_size       (DBusConnection *connection);
void dbus_connection_set_max_live_messages_size (DBusConnection *connection,
                                                 long            size);
long dbus_connection_get_max_live_messages_size (DBusConnection *connection);


DBUS_END_DECLS;

#endif /* DBUS_CONNECTION_H */
