/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/* dbus-config.h client config api header
 *
 * Copyright (C) 2010  Ralf Habacker <ralf.habacker@freenet.de>
 *
 * Licensed under the Academic Free License version 2.1
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef DBUS_CONFIG_H
#define DBUS_CONFIG_H

// session bus address 
// system bus address 
// activation bus address 

char *
_dbus_config_block_on_abort ();
char *
_dbus_config_common_program_files ();
char *
_dbus_config_datadir ();
char *
_dbus_config_debug_output ();
char *
_dbus_config_disable_mem_pools ();
char *
_dbus_config_fatal_warnings ();
char *
_dbus_config_homedrive ();
char *
_dbus_config_homepath ();
char *
_dbus_config_malloc_backtraces ();
char *
_dbus_config_malloc_fail_nth ();
char *
_dbus_config_malloc_fail_greater_than ();
char *
_dbus_config_malloc_guards ();
char *
_dbus_config_starter_bus_type ();
char *
_dbus_config_test_malloc_failures ();
char *
_dbus_config_test_homedir ();
char *
_dbus_config_test_data ();
char *
_dbus_config_verbose ();
char *
_dbus_config_xdg_data_home ();
char *
_dbus_config_xdg_data_dirs ();

#endif
