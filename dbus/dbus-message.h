/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message.h DBusMessage object
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

#ifndef DBUS_MESSAGE_H
#define DBUS_MESSAGE_H

#include <dbus/dbus-macros.h>
#include <dbus/dbus-types.h>

DBUS_BEGIN_DECLS;

typedef struct DBusMessage DBusMessage;
typedef struct DBusMessageIter DBusMessageIter;

DBusMessage* dbus_message_new   (const char *service,
				 const char *name);

void         dbus_message_ref   (DBusMessage *message);
void         dbus_message_unref (DBusMessage *message);

const char*  dbus_message_get_name (DBusMessage *message);

dbus_bool_t dbus_message_append_int32      (DBusMessage         *message,
					    dbus_int32_t         value);
dbus_bool_t dbus_message_append_uint32     (DBusMessage         *message,
					    dbus_uint32_t        value);
dbus_bool_t dbus_message_append_double     (DBusMessage         *message,
					    double               value);
dbus_bool_t dbus_message_append_string     (DBusMessage         *message,
					    const char          *value);
dbus_bool_t dbus_message_append_byte_array (DBusMessage         *message,
					    unsigned const char *value,
					    int                  len);

DBusMessageIter *dbus_message_get_fields_iter     (DBusMessage     *message);

void        dbus_message_iter_ref            (DBusMessageIter *iter);
void        dbus_message_iter_unref          (DBusMessageIter *iter);

dbus_bool_t dbus_message_iter_has_next	     (DBusMessageIter *iter);
dbus_bool_t dbus_message_iter_next           (DBusMessageIter *iter);
int         dbus_message_iter_get_field_type (DBusMessageIter *iter);
int         dbus_message_iter_get_int32      (DBusMessageIter *iter);
int         dbus_message_iter_get_uint32     (DBusMessageIter *iter);
double      dbus_message_iter_get_double     (DBusMessageIter *iter);
char *      dbus_message_iter_get_string     (DBusMessageIter *iter);



DBUS_END_DECLS;

#endif /* DBUS_MESSAGE_H */
