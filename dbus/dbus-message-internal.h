/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message-internal.h DBusMessage object internal interfaces
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
#ifndef DBUS_MESSAGE_INTERNAL_H
#define DBUS_MESSAGE_INTERNAL_H

#include <dbus/dbus-message.h>

DBUS_BEGIN_DECLS;

typedef struct DBusMessageLoader DBusMessageLoader;

void _dbus_message_get_network_data (DBusMessage          *message,
                                     const DBusString    **header,
                                     const DBusString    **body);

void _dbus_message_lock             (DBusMessage          *message);


DBusMessageLoader* _dbus_message_loader_new                   (void);
void               _dbus_message_loader_ref                   (DBusMessageLoader  *loader);
void               _dbus_message_loader_unref                 (DBusMessageLoader  *loader);

void               _dbus_message_loader_get_buffer            (DBusMessageLoader  *loader,
                                                               DBusString        **buffer);
void               _dbus_message_loader_return_buffer         (DBusMessageLoader  *loader,
                                                               DBusString         *buffer,
                                                               int                 bytes_read);

DBusMessage*       _dbus_message_loader_pop_message           (DBusMessageLoader  *loader);

dbus_bool_t        _dbus_message_loader_get_is_corrupted      (DBusMessageLoader  *loader);

DBUS_END_DECLS;

#endif /* DBUS_MESSAGE_H */
