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
typedef struct DBusTimeout DBusTimeout;
typedef struct DBusMessageHandler DBusMessageHandler;
typedef struct DBusPreallocatedSend DBusPreallocatedSend;

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
                                 *   the flag can be passed to dbus_watch_handle()).
                                 */
  DBUS_WATCH_HANGUP   = 1 << 3  /**< As in POLLHUP (can't watch for it, but
                                 *   can be present in current state). */
} DBusWatchFlags;

typedef enum
{
  DBUS_DISPATCH_DATA_REMAINS,  /**< There is more data to potentially convert to messages. */
  DBUS_DISPATCH_COMPLETE,      /**< All currently available data has been processed. */
  DBUS_DISPATCH_NEED_MEMORY    /**< More memory is needed to continue. */
} DBusDispatchStatus;

typedef dbus_bool_t (* DBusAddWatchFunction)       (DBusWatch      *watch,
                                                    void           *data);
typedef void        (* DBusWatchToggledFunction)   (DBusWatch      *watch,
                                                    void           *data);
typedef void        (* DBusRemoveWatchFunction)    (DBusWatch      *watch,
                                                    void           *data);
typedef dbus_bool_t (* DBusAddTimeoutFunction)     (DBusTimeout    *timeout,
                                                    void           *data);
typedef void        (* DBusTimeoutToggledFunction) (DBusTimeout    *timeout,
                                                    void           *data);
typedef void        (* DBusRemoveTimeoutFunction)  (DBusTimeout    *timeout,
                                                    void           *data);
typedef void        (* DBusDispatchStatusFunction) (DBusConnection *connection,
                                                    DBusDispatchStatus new_status,
                                                    void           *data);
typedef void        (* DBusWakeupMainFunction)     (void           *data);
typedef dbus_bool_t (* DBusAllowUnixUserFunction)  (DBusConnection *connection,
                                                    unsigned long   uid,
                                                    void           *data);

DBusConnection*    dbus_connection_open                         (const char                 *address,
                                                                 DBusError                  *error);
void               dbus_connection_ref                          (DBusConnection             *connection);
void               dbus_connection_unref                        (DBusConnection             *connection);
void               dbus_connection_disconnect                   (DBusConnection             *connection);
dbus_bool_t        dbus_connection_get_is_connected             (DBusConnection             *connection);
dbus_bool_t        dbus_connection_get_is_authenticated         (DBusConnection             *connection);
void               dbus_connection_flush                        (DBusConnection             *connection);
DBusMessage*       dbus_connection_borrow_message               (DBusConnection             *connection);
void               dbus_connection_return_message               (DBusConnection             *connection,
                                                                 DBusMessage                *message);
void               dbus_connection_steal_borrowed_message       (DBusConnection             *connection,
                                                                 DBusMessage                *message);
DBusMessage*       dbus_connection_pop_message                  (DBusConnection             *connection);
DBusDispatchStatus dbus_connection_get_dispatch_status          (DBusConnection             *connection);
DBusDispatchStatus dbus_connection_dispatch                     (DBusConnection             *connection);
dbus_bool_t        dbus_connection_send                         (DBusConnection             *connection,
                                                                 DBusMessage                *message,
                                                                 dbus_uint32_t              *client_serial);
dbus_bool_t        dbus_connection_send_with_reply              (DBusConnection             *connection,
                                                                 DBusMessage                *message,
                                                                 DBusMessageHandler         *reply_handler,
                                                                 int                         timeout_milliseconds);
DBusMessage *      dbus_connection_send_with_reply_and_block    (DBusConnection             *connection,
                                                                 DBusMessage                *message,
                                                                 int                         timeout_milliseconds,
                                                                 DBusError                  *error);
dbus_bool_t        dbus_connection_set_watch_functions          (DBusConnection             *connection,
                                                                 DBusAddWatchFunction        add_function,
                                                                 DBusRemoveWatchFunction     remove_function,
                                                                 DBusWatchToggledFunction    toggled_function,
                                                                 void                       *data,
                                                                 DBusFreeFunction            free_data_function);
dbus_bool_t        dbus_connection_set_timeout_functions        (DBusConnection             *connection,
                                                                 DBusAddTimeoutFunction      add_function,
                                                                 DBusRemoveTimeoutFunction   remove_function,
                                                                 DBusTimeoutToggledFunction  toggled_function,
                                                                 void                       *data,
                                                                 DBusFreeFunction            free_data_function);
void               dbus_connection_set_wakeup_main_function     (DBusConnection             *connection,
                                                                 DBusWakeupMainFunction      wakeup_main_function,
                                                                 void                       *data,
                                                                 DBusFreeFunction            free_data_function);
void               dbus_connection_set_dispatch_status_function (DBusConnection             *connection,
                                                                 DBusDispatchStatusFunction  function,
                                                                 void                       *data,
                                                                 DBusFreeFunction            free_data_function);
dbus_bool_t        dbus_connection_get_unix_user                (DBusConnection             *connection,
                                                                 unsigned long              *uid);
void               dbus_connection_set_unix_user_function       (DBusConnection             *connection,
                                                                 DBusAllowUnixUserFunction   function,
                                                                 void                       *data,
                                                                 DBusFreeFunction            free_data_function);


int          dbus_watch_get_fd      (DBusWatch        *watch);
unsigned int dbus_watch_get_flags   (DBusWatch        *watch);
void*        dbus_watch_get_data    (DBusWatch        *watch);
void         dbus_watch_set_data    (DBusWatch        *watch,
                                     void             *data,
                                     DBusFreeFunction  free_data_function);
dbus_bool_t  dbus_watch_handle      (DBusWatch        *watch,
                                     unsigned int      flags);
dbus_bool_t  dbus_watch_get_enabled (DBusWatch        *watch);

int         dbus_timeout_get_interval (DBusTimeout      *timeout);
void*       dbus_timeout_get_data     (DBusTimeout      *timeout);
void        dbus_timeout_set_data     (DBusTimeout      *timeout,
                                       void             *data,
                                       DBusFreeFunction  free_data_function);
dbus_bool_t dbus_timeout_handle       (DBusTimeout      *timeout);
dbus_bool_t dbus_timeout_get_enabled  (DBusTimeout      *timeout);

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

void        dbus_connection_set_change_sigpipe (dbus_bool_t       will_modify_sigpipe); 

void dbus_connection_set_max_message_size       (DBusConnection *connection,
                                                 long            size);
long dbus_connection_get_max_message_size       (DBusConnection *connection);
void dbus_connection_set_max_received_size (DBusConnection *connection,
                                                 long            size);
long dbus_connection_get_max_received_size (DBusConnection *connection);
long dbus_connection_get_outgoing_size     (DBusConnection *connection);

DBusPreallocatedSend* dbus_connection_preallocate_send       (DBusConnection       *connection);
void                  dbus_connection_free_preallocated_send (DBusConnection       *connection,
                                                              DBusPreallocatedSend *preallocated);
void                  dbus_connection_send_preallocated      (DBusConnection       *connection,
                                                              DBusPreallocatedSend *preallocated,
                                                              DBusMessage          *message,
                                                              dbus_uint32_t        *client_serial);


DBUS_END_DECLS;

#endif /* DBUS_CONNECTION_H */
