/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message-handler.h Sender/receiver of messages.
 *
 * Copyright (C) 2002  Red Hat Inc.
 *
 * Licensed under the Academic Free License version 2.0
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

#ifndef DBUS_MESSAGE_HANDLER_H
#define DBUS_MESSAGE_HANDLER_H

#include <dbus/dbus-macros.h>
#include <dbus/dbus-types.h>
#include <dbus/dbus-connection.h>

DBUS_BEGIN_DECLS;

typedef DBusHandlerResult (* DBusHandleMessageFunction) (DBusMessageHandler *handler,
                                                         DBusConnection     *connection,
                                                         DBusMessage        *message,
                                                         void               *user_data);

DBusMessageHandler* dbus_message_handler_new (DBusHandleMessageFunction  function,
                                              void                      *user_data,
                                              DBusFreeFunction           free_user_data);


DBusMessageHandler* dbus_message_handler_ref   (DBusMessageHandler *handler);
void         dbus_message_handler_unref (DBusMessageHandler *handler);


void* dbus_message_handler_get_data       (DBusMessageHandler        *handler);
void  dbus_message_handler_set_data       (DBusMessageHandler        *handler,
                                           void                      *data,
                                           DBusFreeFunction           free_user_data);
void  dbus_message_handler_set_function   (DBusMessageHandler        *handler,
                                           DBusHandleMessageFunction  function);

DBUS_END_DECLS;

#endif /* DBUS_MESSAGE_HANDLER_H */
