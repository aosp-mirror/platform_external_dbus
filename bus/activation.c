/* -*- mode: C; c-file-style: "gnu" -*- */
/* activation.c  Activation of services
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
#include "activation.h"
#include "desktop-file.h"
#include "utils.h"
#include <dbus/dbus-internals.h>
#include <dbus/dbus-hash.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#define DBUS_SERVICE_SECTION "D-BUS Service"
#define DBUS_SERVICE_NAME "Name"
#define DBUS_SERVICE_EXEC "Exec"

static DBusHashTable *activation_entries = NULL;
static char *server_address = NULL;

typedef struct
{
  char *name;
  char *exec;
} BusActivationEntry;

static DBusHashTable *pending_activations = NULL;
typedef struct
{
  char *service;
} BusPendingActivation;

static void
bus_activation_entry_free (BusActivationEntry *entry)
{
  if (!entry)
    return;
  
  dbus_free (entry->name);
  dbus_free (entry->exec);
}

static dbus_bool_t
add_desktop_file_entry (BusDesktopFile *desktop_file)
{
  char *name, *exec;
  BusActivationEntry *entry;
  
  if (!bus_desktop_file_get_string (desktop_file,
				    DBUS_SERVICE_SECTION,
				    DBUS_SERVICE_NAME,
				    &name))
    {
      _dbus_verbose ("No \""DBUS_SERVICE_NAME"\" key in .service file\n");
      return FALSE;
    }

  if (!bus_desktop_file_get_string (desktop_file,
				    DBUS_SERVICE_SECTION,
				    DBUS_SERVICE_EXEC,
				    &exec))
    {
      _dbus_verbose ("No \""DBUS_SERVICE_EXEC"\" key in .service file\n");
      
      dbus_free (name);
      return FALSE;
    }

  if (_dbus_hash_table_lookup_string (activation_entries, name))
    {
      _dbus_verbose ("Service %s already exists in activation entry list\n", name);
      dbus_free (name);
      dbus_free (exec);

      return FALSE;
    }
  
  BUS_HANDLE_OOM (entry = dbus_malloc0 (sizeof (BusActivationEntry)));
  entry->name = name;
  entry->exec = exec;

  BUS_HANDLE_OOM (_dbus_hash_table_insert_string (activation_entries, entry->name, entry));

  _dbus_verbose ("Added \"%s\" to list of services\n", entry->name);
  
  return TRUE;
}

static void
load_directory (const char *directory)
{
  DBusDirIter *iter;
  DBusString dir, filename;
  DBusResultCode result;

  _dbus_string_init_const (&dir, directory);
  
  iter = _dbus_directory_open (&dir, &result);
  if (iter == NULL)
    {
      _dbus_verbose ("Failed to open directory %s: &s\n", directory,
		     result);
    return;
    }

  BUS_HANDLE_OOM (_dbus_string_init (&filename, _DBUS_INT_MAX));
  
  /* Now read the files */
  while (_dbus_directory_get_next_file (iter, &filename, &result))
    {
      DBusString full_path;
      BusDesktopFile *desktop_file;
      DBusError error;
      
      if (!_dbus_string_ends_with_c_str (&filename, ".service"))
	{
          const char *filename_c;
          _dbus_string_get_const_data (&filename, &filename_c);
          _dbus_verbose ("Skipping non-.service file %s\n",
                         filename_c);
	  continue;
	}
      
      BUS_HANDLE_OOM (_dbus_string_init (&full_path, _DBUS_INT_MAX));
      BUS_HANDLE_OOM (_dbus_string_append (&full_path, directory));

      BUS_HANDLE_OOM (_dbus_concat_dir_and_file (&full_path, &filename));

      desktop_file = bus_desktop_file_load (&full_path, &error);

      if (!desktop_file)
	{
	  const char *full_path_c;

          _dbus_string_get_const_data (&full_path, &full_path_c);
	  
	  _dbus_verbose ("Could not load %s: %s\n", full_path_c,
			 error.message);
	  dbus_error_free (&error);
	  _dbus_string_free (&full_path);
	  continue;
	}

      if (!add_desktop_file_entry (desktop_file))
	{
	  const char *full_path_c;

          _dbus_string_get_const_data (&full_path, &full_path_c);
	  
	  _dbus_verbose ("Could not add %s to activation entry list.\n", full_path_c);
	}

      bus_desktop_file_free (desktop_file);
      _dbus_string_free (&full_path);
    }
}


void
bus_activation_init (const char *address,
		     const char **directories)
{
  int i;

  /* FIXME: We should split up the server addresses. */
  BUS_HANDLE_OOM (server_address = _dbus_strdup (address));
  
  BUS_HANDLE_OOM (activation_entries = _dbus_hash_table_new (DBUS_HASH_STRING, NULL,
							     (DBusFreeFunction)bus_activation_entry_free));

  i = 0;

  /* Load service files */
  while (directories[i] != NULL)
    {
      load_directory (directories[i]);
      i++;
    }
}

static void
child_setup (void *data)
{
  /* FIXME: Check return value in case of OOM */
  _dbus_setenv ("DBUS_ADDRESS", server_address);
}

dbus_bool_t
bus_activation_activate_service (const char  *service_name,
				 DBusError   *error)
{
  BusActivationEntry *entry;
  char *argv[2];
  
  entry = _dbus_hash_table_lookup_string (activation_entries, service_name);

  if (!entry)
    {
      dbus_set_error (error, DBUS_ERROR_ACTIVATE_SERVICE_NOT_FOUND,
		      "The service %s was not found in the activation entry list",
		      service_name);
      return FALSE;
    }

  /* Now try to spawn the process */
  argv[0] = entry->exec;
  argv[1] = NULL;

  if (!_dbus_spawn_async (argv,
			  child_setup, NULL, 
			  error))
    return FALSE;

  return TRUE;
}
