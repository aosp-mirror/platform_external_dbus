/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gproxy.h convenience routines for calling methods, etc.
 *
 * Copyright (C) 2003  Red Hat, Inc.
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
#ifndef DBUS_GPROXY_H
#define DBUS_GPROXY_H

#if !defined (DBUS_INSIDE_DBUS_GLIB_H) && !defined (DBUS_COMPILATION)
#error "Only <dbus/dbus-glib.h> can be included directly, this file may disappear or change contents."
#endif

#include <dbus/dbus.h>
#include <glib.h>
#include <glib-object.h> /* for GCallback at the moment, we don't link to it */

G_BEGIN_DECLS

typedef struct DBusGProxy       DBusGProxy;

DBusGProxy*      dbus_gproxy_new_for_service       (DBusConnection      *connection,
                                                    const char          *service_name,
                                                    const char          *interface_name);
DBusGProxy*      dbus_gproxy_new_for_service_owner (DBusConnection      *connection,
                                                    const char          *service_name,
                                                    const char          *interface_name,
                                                    GError             **error);
DBusGProxy*      dbus_gproxy_new_for_object_id     (DBusConnection      *connection,
                                                    const DBusObjectID  *object_id,
                                                    const char          *interface_name);
DBusGProxy*      dbus_gproxy_new_for_interface     (DBusConnection      *connection,
                                                    const char          *interface_name);
void             dbus_gproxy_ref                   (DBusGProxy          *proxy);
void             dbus_gproxy_unref                 (DBusGProxy          *proxy);
gboolean         dbus_gproxy_connect_signal        (DBusGProxy          *proxy,
                                                    const char          *signal_name,
                                                    GCallback            callback,
                                                    void                *data,
                                                    GFreeFunc            free_data_func,
                                                    GError             **error);
DBusPendingCall* dbus_gproxy_begin_call            (DBusGProxy          *proxy,
                                                    const char          *method,
                                                    int                  first_arg_type,
                                                    ...);
gboolean         dbus_gproxy_end_call              (DBusGProxy          *proxy,
                                                    DBusPendingCall     *pending,
                                                    GError             **error,
                                                    int                  first_arg_type,
                                                    ...);
void             dbus_gproxy_oneway_call           (DBusGProxy          *proxy,
                                                    const char          *method,
                                                    int                  first_arg_type,
                                                    ...);
void             dbus_gproxy_send                  (DBusGProxy          *proxy,
                                                    DBusMessage         *message,
                                                    dbus_uint32_t       *client_serial);


G_END_DECLS

#endif /* DBUS_GPROXY_H */
