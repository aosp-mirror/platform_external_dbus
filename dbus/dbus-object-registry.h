/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-object-registry.h  DBusObjectRegistry (internals of DBusConnection)
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
#ifndef DBUS_OBJECT_REGISTRY_H
#define DBUS_OBJECT_REGISTRY_H

#include <dbus/dbus-object.h>

DBUS_BEGIN_DECLS;

typedef struct DBusObjectRegistry DBusObjectRegistry;

DBusObjectRegistry* _dbus_object_registry_new   (DBusConnection     *connection);
void                _dbus_object_registry_ref   (DBusObjectRegistry *registry);
void                _dbus_object_registry_unref (DBusObjectRegistry *registry);

dbus_bool_t       _dbus_object_registry_add_and_unlock    (DBusObjectRegistry      *registry,
                                                           const char             **interfaces,
                                                           const DBusObjectVTable  *vtable,
                                                           void                    *object_impl,
                                                           DBusObjectID            *object_id);
void              _dbus_object_registry_remove_and_unlock (DBusObjectRegistry      *registry,
                                                           const DBusObjectID      *object_id);
DBusHandlerResult _dbus_object_registry_handle_and_unlock (DBusObjectRegistry      *registry,
                                                           DBusMessage             *message);
void              _dbus_object_registry_free_all_unlocked (DBusObjectRegistry      *registry);



DBUS_END_DECLS;

#endif /* DBUS_OBJECT_REGISTRY_H */
