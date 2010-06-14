/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/* dbus-config.c client config api implementation
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

#include "dbus-config.h"
#include "dbus-sysdeps.h"

char *
_dbus_config_block_on_abort ()
{
  return _dbus_getenv ("DBUS_BLOCK_ON_ABORT");
}

char *
_dbus_config_common_program_files()
{
  return _dbus_getenv ("CommonProgramFiles");
}

char *
_dbus_config_datadir ()
{
  return _dbus_getenv ("DBUS_DATADIR");
}

char *
_dbus_config_debug_output ()
{
  return _dbus_getenv ("DBUS_DEBUG_OUTPUT");
}

char *
_dbus_config_disable_mem_pools()
{ 
  return _dbus_getenv ("DBUS_DISABLE_MEM_POOLS");
}

char *
_dbus_config_homedrive ()
{
  return _dbus_getenv("HOMEDRIVE");
}

char *
_dbus_config_homepath ()
{
  return _dbus_getenv("HOMEPATH");
}

char *
_dbus_config_fatal_warnings ()
{
  return _dbus_getenv ("DBUS_FATAL_WARNINGS");
}	  

char *
_dbus_config_malloc_fail_nth ()
{ 
  return _dbus_getenv ("DBUS_MALLOC_FAIL_NTH");
}

char *
_dbus_config_malloc_fail_greater_than ()
{ 
  return _dbus_getenv ("DBUS_MALLOC_FAIL_GREATER_THAN");
}

char *
_dbus_config_malloc_guards ()
{ 
  return _dbus_getenv ("DBUS_MALLOC_GUARDS");
}

char *
_dbus_config_malloc_backtraces ()
{
  return _dbus_getenv ("DBUS_MALLOC_BACKTRACES");
}

char *
_dbus_config_starter_bus_type ()
{
  return _dbus_getenv ("DBUS_STARTER_BUS_TYPE");
}

char *
_dbus_config_test_malloc_failures ()
{
  return _dbus_getenv ("DBUS_TEST_MALLOC_FAILURES");
}

char *
_dbus_config_test_homedir ()
{
  return _dbus_getenv ("DBUS_TEST_HOMEDIR");
}

char *
_dbus_config_test_data ()
{
  return _dbus_getenv ("DBUS_TEST_DATA");
}

char *
_dbus_config_xdg_data_home ()
{
  return _dbus_getenv ("XDG_DATA_HOME");
}

char *
_dbus_config_xdg_data_dirs ()
{
  return _dbus_getenv ("XDG_DATA_DIRS");
}

char *
_dbus_config_verbose ()
{
  return _dbus_getenv ("DBUS_VERBOSE");
}
