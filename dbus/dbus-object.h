/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-object.h  Objects
 *
 * Copyright (C) 2003  Red Hat Inc.
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

#ifndef DBUS_OBJECT_H
#define DBUS_OBJECT_H

#include <dbus/dbus-arch-deps.h>
#include <dbus/dbus-types.h>
#include <dbus/dbus-message.h>
#include <dbus/dbus-objectid.h>

DBUS_BEGIN_DECLS;

typedef struct DBusConnection     DBusConnection;
typedef struct DBusObjectVTable   DBusObjectVTable;
typedef struct DBusObjectInfo     DBusObjectInfo;
typedef struct DBusCallbackObject DBusCallbackObject;

struct DBusObjectInfo
{
  void               *object_impl; /**< Object information */
  DBusObjectID        object_id;
  DBusConnection     *connection;
};

typedef void (* DBusObjectRegisteredFunction)   (DBusObjectInfo *info);
typedef void (* DBusObjectUnregisteredFunction) (DBusObjectInfo *info);
typedef void (* DBusObjectMessageFunction)      (DBusObjectInfo *info,
                                                 DBusMessage    *message);

struct DBusObjectVTable
{
  DBusObjectRegisteredFunction   registered;
  DBusObjectUnregisteredFunction unregistered;
  DBusObjectMessageFunction      message;
};

extern const DBusObjectVTable *dbus_callback_object_vtable;

DBusCallbackObject* dbus_callback_object_new          (DBusObjectMessageFunction   function,
                                                       void                       *user_data,
                                                       DBusFreeFunction            free_user_data);
void                dbus_callback_object_ref          (DBusCallbackObject         *callback);
void                dbus_callback_object_unref        (DBusCallbackObject         *callback);
void*               dbus_callback_object_get_data     (DBusCallbackObject         *callback);
void                dbus_callback_object_set_data     (DBusCallbackObject         *callback,
                                                       void                       *data,
                                                       DBusFreeFunction            free_user_data);
void                dbus_callback_object_set_function (DBusCallbackObject         *callback,
                                                       DBusObjectMessageFunction   function);


DBUS_END_DECLS;

#endif /* DBUS_OBJECT_H */
