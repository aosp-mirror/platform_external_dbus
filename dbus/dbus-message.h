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
#include <dbus/dbus-dict.h>
#include <dbus/dbus-types.h>
#include <stdarg.h>

DBUS_BEGIN_DECLS;

typedef struct DBusMessage DBusMessage;
typedef struct DBusMessageIter DBusMessageIter;

DBusMessage* dbus_message_new              (const char        *service,
					    const char        *name);
DBusMessage* dbus_message_new_reply        (DBusMessage       *original_message);
DBusMessage* dbus_message_new_error_reply  (DBusMessage       *original_message,
					    const char        *error_name,
					    const char        *error_message);
DBusMessage *dbus_message_copy             (const DBusMessage *message);

void         dbus_message_ref   (DBusMessage *message);
void         dbus_message_unref (DBusMessage *message);

const char*  dbus_message_get_name         (DBusMessage  *message);
const char*  dbus_message_get_service      (DBusMessage  *message);
dbus_bool_t  dbus_message_set_sender       (DBusMessage  *message,
                                            const char   *sender);
const char*  dbus_message_get_sender       (DBusMessage  *message);
void         dbus_message_set_is_error     (DBusMessage  *message,
                                            dbus_bool_t   is_error_reply);
dbus_bool_t  dbus_message_get_is_error     (DBusMessage  *message);
dbus_bool_t  dbus_message_name_is          (DBusMessage  *message,
                                            const char   *name);
dbus_int32_t dbus_message_get_serial       (DBusMessage  *message);
dbus_bool_t  dbus_message_set_reply_serial (DBusMessage  *message,
                                            dbus_int32_t  reply_serial);
dbus_int32_t dbus_message_get_reply_serial (DBusMessage  *message);


dbus_bool_t dbus_message_append_args          (DBusMessage          *message,
					       int                   first_arg_type,
					       ...);
dbus_bool_t dbus_message_append_args_valist   (DBusMessage          *message,
					       int                   first_arg_type,
					       va_list               var_args);
dbus_bool_t dbus_message_append_nil           (DBusMessage          *message);
dbus_bool_t dbus_message_append_boolean       (DBusMessage          *message,
					       dbus_bool_t           value);
dbus_bool_t dbus_message_append_int32         (DBusMessage          *message,
					       dbus_int32_t          value);
dbus_bool_t dbus_message_append_uint32        (DBusMessage          *message,
					       dbus_uint32_t         value);
dbus_bool_t dbus_message_append_double        (DBusMessage          *message,
					       double                value);
dbus_bool_t dbus_message_append_string        (DBusMessage          *message,
					       const char           *value);
dbus_bool_t dbus_message_append_boolean_array (DBusMessage          *message,
					       unsigned const char  *value,
					       int                   len);
dbus_bool_t dbus_message_append_int32_array   (DBusMessage          *message,
					       const dbus_int32_t   *value,
					       int                   len);
dbus_bool_t dbus_message_append_uint32_array  (DBusMessage          *message,
					       const dbus_uint32_t  *value,
					       int                   len);
dbus_bool_t dbus_message_append_double_array  (DBusMessage          *message,
					       const double         *value,
					       int                   len);
dbus_bool_t dbus_message_append_byte_array    (DBusMessage          *message,
					       unsigned const char  *value,
					       int                   len);
dbus_bool_t dbus_message_append_string_array  (DBusMessage          *message,
					       const char          **value,
					       int                   len);
dbus_bool_t dbus_message_append_dict          (DBusMessage          *message,
					       DBusDict             *dict);

DBusMessageIter *dbus_message_get_args_iter   (DBusMessage *message);
dbus_bool_t      dbus_message_get_args        (DBusMessage *message,
                                               DBusError   *error,
                                               int          first_arg_type,
                                               ...);
dbus_bool_t      dbus_message_get_args_valist (DBusMessage *message,
                                               DBusError   *error,
                                               int          first_arg_type,
                                               va_list      var_args);


void          dbus_message_iter_ref               (DBusMessageIter   *iter);
void          dbus_message_iter_unref             (DBusMessageIter   *iter);
dbus_bool_t   dbus_message_iter_has_next          (DBusMessageIter   *iter);
dbus_bool_t   dbus_message_iter_next              (DBusMessageIter   *iter);
int           dbus_message_iter_get_arg_type      (DBusMessageIter   *iter);
dbus_bool_t   dbus_message_iter_get_boolean       (DBusMessageIter   *iter);
int           dbus_message_iter_get_int32         (DBusMessageIter   *iter);
int           dbus_message_iter_get_uint32        (DBusMessageIter   *iter);
double        dbus_message_iter_get_double        (DBusMessageIter   *iter);
char *        dbus_message_iter_get_string        (DBusMessageIter   *iter);
dbus_bool_t   dbus_message_iter_get_boolean_array (DBusMessageIter   *iter,
						   unsigned char    **value,
						   int               *len);
dbus_bool_t   dbus_message_iter_get_int32_array   (DBusMessageIter   *iter,
						   dbus_int32_t     **value,
						   int               *len);
dbus_bool_t   dbus_message_iter_get_uint32_array  (DBusMessageIter   *iter,
						   dbus_uint32_t    **value,
						   int               *len);
dbus_bool_t   dbus_message_iter_get_double_array  (DBusMessageIter   *iter,
						   double           **value,
						   int               *len);
dbus_bool_t   dbus_message_iter_get_byte_array    (DBusMessageIter   *iter,
						   unsigned char    **value,
						   int               *len);
dbus_bool_t   dbus_message_iter_get_string_array  (DBusMessageIter   *iter,
						   char            ***value,
						   int               *len);
dbus_bool_t   dbus_message_iter_get_dict          (DBusMessageIter   *iter,
						   DBusDict         **dict);

DBUS_END_DECLS;

#endif /* DBUS_MESSAGE_H */
