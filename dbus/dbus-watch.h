/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-watch.h DBusWatch internal interfaces
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
#ifndef DBUS_WATCH_H
#define DBUS_WATCH_H

#include <dbus/dbus-internals.h>
#include <dbus/dbus-connection.h>

DBUS_BEGIN_DECLS;

/* Public methods on DBusWatch are in dbus-connection.h */

typedef struct DBusWatchList DBusWatchList;

DBusWatch* _dbus_watch_new        (int           fd,
                                   unsigned int  flags);
void       _dbus_watch_ref        (DBusWatch    *watch);
void       _dbus_watch_unref      (DBusWatch    *watch);
void       _dbus_watch_invalidate (DBusWatch    *watch);

void       _dbus_watch_sanitize_condition (DBusWatch *watch,
                                           unsigned int *condition);

DBusWatchList* _dbus_watch_list_new           (void);
void           _dbus_watch_list_free          (DBusWatchList           *watch_list);
dbus_bool_t    _dbus_watch_list_set_functions (DBusWatchList           *watch_list,
                                               DBusAddWatchFunction     add_function,
                                               DBusRemoveWatchFunction  remove_function,
                                               void                    *data,
                                               DBusFreeFunction         free_data_function);
dbus_bool_t    _dbus_watch_list_add_watch     (DBusWatchList           *watch_list,
                                               DBusWatch               *watch);
void           _dbus_watch_list_remove_watch  (DBusWatchList           *watch_list,
                                               DBusWatch               *watch);



DBUS_END_DECLS;

#endif /* DBUS_WATCH_H */
