/* -*- mode: C; c-file-style: "gnu" -*- */
/* loop.h  Main loop for daemon
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

#ifndef BUS_LOOP_H
#define BUS_LOOP_H

#include <dbus/dbus.h>

typedef struct BusLoop BusLoop;

typedef dbus_bool_t (* BusWatchFunction)   (DBusWatch     *watch,
                                            unsigned int   condition,
                                            void          *data);
typedef void        (* BusTimeoutFunction) (DBusTimeout   *timeout,
                                            void          *data);


BusLoop*    bus_loop_new            (void);
void        bus_loop_ref            (BusLoop            *loop);
void        bus_loop_unref          (BusLoop            *loop);
dbus_bool_t bus_loop_add_watch      (BusLoop            *loop,
                                     DBusWatch          *watch,
                                     BusWatchFunction    function,
                                     void               *data,
                                     DBusFreeFunction    free_data_func);
void        bus_loop_remove_watch   (BusLoop            *loop,
                                     DBusWatch          *watch,
                                     BusWatchFunction    function,
                                     void               *data);
dbus_bool_t bus_loop_add_timeout    (BusLoop            *loop,
                                     DBusTimeout        *timeout,
                                     BusTimeoutFunction  function,
                                     void               *data,
                                     DBusFreeFunction    free_data_func);
void        bus_loop_remove_timeout (BusLoop            *loop,
                                     DBusTimeout        *timeout,
                                     BusTimeoutFunction  function,
                                     void               *data);
void        bus_loop_run            (BusLoop            *loop);
void        bus_loop_quit           (BusLoop            *loop);
dbus_bool_t bus_loop_iterate        (BusLoop            *loop,
                                     dbus_bool_t         block);


#endif /* BUS_LOOP_H */
