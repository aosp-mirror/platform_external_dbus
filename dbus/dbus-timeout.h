/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-timeout.h DBusTimeout internal interfaces
 *
 * Copyright (C) 2003  CodeFactory AB
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
#ifndef DBUS_TIMEOUT_H
#define DBUS_TIMEOUT_H

#include <dbus/dbus-connection.h>
#include <dbus/dbus-internals.h>

DBUS_BEGIN_DECLS;

typedef struct DBusTimeoutList DBusTimeoutList;

/* Public methods on DBusTimeout are in dbus-connection.h */

typedef void (* DBusTimeoutHandler) (void *data);

DBusTimeout* _dbus_timeout_new   (int                 interval,
				  DBusTimeoutHandler  handler,
				  void               *data,
				  DBusFreeFunction    free_data_function);
void         _dbus_timeout_ref   (DBusTimeout        *timeout);
void         _dbus_timeout_unref (DBusTimeout        *timeout);


DBusTimeoutList *_dbus_timeout_list_new            (void);
void             _dbus_timeout_list_free           (DBusTimeoutList           *timeout_list);
void             _dbus_timeout_list_set_functions  (DBusTimeoutList           *timeout_list,
						    DBusAddTimeoutFunction     add_function,
						    DBusRemoveTimeoutFunction  remove_function,
						    void                      *data,
						    DBusFreeFunction           free_data_function);
dbus_bool_t      _dbus_timeout_list_add_timeout    (DBusTimeoutList           *timeout_list,
						    DBusTimeout               *timeout);
void             _dbus_timeout_list_remove_timeout (DBusTimeoutList           *timeout_list,
						    DBusTimeout               *timeout);


DBUS_END_DECLS;

#endif /* DBUS_TIMEOUT_H */
