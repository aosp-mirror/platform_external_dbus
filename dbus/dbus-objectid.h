/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-objectid.h  DBusObjectID type
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

#ifndef DBUS_OBJECTID_H
#define DBUS_OBJECTID_H

#include <dbus/dbus-arch-deps.h>
#include <dbus/dbus-types.h>

DBUS_BEGIN_DECLS;

typedef struct DBusObjectID DBusObjectID;

struct DBusObjectID
{
#ifdef DBUS_HAVE_INT64
  dbus_uint64_t dbus_do_not_use_dummy1;
#else
  dbus_uint32_t dbus_do_not_use_dummy1;
  dbus_uint32_t dbus_do_not_use_dummy2;
#endif
};

dbus_bool_t            dbus_object_id_equal          (const DBusObjectID *a,
                                                      const DBusObjectID *b);
int                    dbus_object_id_compare        (const DBusObjectID *a,
                                                      const DBusObjectID *b);
dbus_uint32_t          dbus_object_id_get_high_bits  (const DBusObjectID *obj_id);
dbus_uint32_t          dbus_object_id_get_low_bits   (const DBusObjectID *obj_id);
void                   dbus_object_id_set_high_bits  (DBusObjectID       *obj_id,
                                                      dbus_uint32_t       value);
void                   dbus_object_id_set_low_bits   (DBusObjectID       *obj_id,
                                                      dbus_uint32_t       value);
#ifdef DBUS_HAVE_INT64
dbus_uint64_t          dbus_object_id_get_as_integer (const DBusObjectID *obj_id);
void                   dbus_object_id_set_as_integer (DBusObjectID       *obj_id,
                                                      dbus_uint64_t       value);
#endif

DBUS_END_DECLS;

#endif /* DBUS_OBJECTID_H */
