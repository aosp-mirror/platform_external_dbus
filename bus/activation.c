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
add_desktop_file_entry (BusDesktopFile *desktop_file,
                        DBusError      *error)
{
  char *name, *exec;
  BusActivationEntry *entry;

  name = NULL;
  exec = NULL;
  entry = NULL;
  
  if (!bus_desktop_file_get_string (desktop_file,
				    DBUS_SERVICE_SECTION,
				    DBUS_SERVICE_NAME,
				    &name))
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "No \""DBUS_SERVICE_NAME"\" key in .service file\n");
      goto failed;
    }

  if (!bus_desktop_file_get_string (desktop_file,
				    DBUS_SERVICE_SECTION,
				    DBUS_SERVICE_EXEC,
				    &exec))
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "No \""DBUS_SERVICE_EXEC"\" key in .service file\n");
      goto failed;
    }

  /* FIXME we need a better-defined algorithm for which service file to
   * pick than "whichever one is first in the directory listing"
   */
  if (_dbus_hash_table_lookup_string (activation_entries, name))
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Service %s already exists in activation entry list\n", name);
      goto failed;
    }
  
  entry = dbus_new0 (BusActivationEntry, 1);
  if (entry == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }
  
  entry->name = name;
  entry->exec = exec;

  if (!_dbus_hash_table_insert_string (activation_entries, entry->name, entry))
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  _dbus_verbose ("Added \"%s\" to list of services\n", entry->name);
  
  return TRUE;

 failed:
  dbus_free (name);
  dbus_free (exec);
  dbus_free (entry);
  
  return FALSE;
}

/* warning: this doesn't fully "undo" itself on failure, i.e. doesn't strip
 * hash entries it already added.
 */
static dbus_bool_t
load_directory (const char *directory,
                DBusError  *error)
{
  DBusDirIter *iter;
  DBusString dir, filename;
  DBusString full_path;
  BusDesktopFile *desktop_file;
  DBusError tmp_error;
  
  _dbus_string_init_const (&dir, directory);

  iter = NULL;
  desktop_file = NULL;
  
  if (!_dbus_string_init (&filename, _DBUS_INT_MAX))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!_dbus_string_init (&full_path, _DBUS_INT_MAX))
    {
      BUS_SET_OOM (error);
      _dbus_string_free (&filename);
      return FALSE;
    }

  /* from this point it's safe to "goto failed" */
  
  iter = _dbus_directory_open (&dir, error);
  if (iter == NULL)
    {
      _dbus_verbose ("Failed to open directory %s: %s\n",
                     directory, error ? error->message : "unknown");
      goto failed;
    }
  
  /* Now read the files */
  dbus_error_init (&tmp_error);
  while (_dbus_directory_get_next_file (iter, &filename, &tmp_error))
    {
      _dbus_assert (!dbus_error_is_set (&tmp_error));
      
      _dbus_string_set_length (&full_path, 0);
      
      if (!_dbus_string_append (&full_path, directory) ||
          !_dbus_concat_dir_and_file (&full_path, &filename))
        {
          BUS_SET_OOM (error);
          goto failed;
        }
      
      if (!_dbus_string_ends_with_c_str (&filename, ".service"))
	{
          const char *filename_c;
          _dbus_string_get_const_data (&filename, &filename_c);
          _dbus_verbose ("Skipping non-.service file %s\n",
                         filename_c);
          continue;
	}
      
      desktop_file = bus_desktop_file_load (&full_path, &tmp_error);

      if (desktop_file == NULL)
	{
	  const char *full_path_c;

          _dbus_string_get_const_data (&full_path, &full_path_c);
	  
	  _dbus_verbose ("Could not load %s: %s\n", full_path_c,
			 tmp_error.message);

          if (dbus_error_has_name (&tmp_error, DBUS_ERROR_NO_MEMORY))
            {
              dbus_move_error (&tmp_error, error);
              goto failed;
            }
          
	  dbus_error_free (&tmp_error);
	  continue;
	}

      if (!add_desktop_file_entry (desktop_file, &tmp_error))
	{
	  const char *full_path_c;

          bus_desktop_file_free (desktop_file);
          desktop_file = NULL;
          
          _dbus_string_get_const_data (&full_path, &full_path_c);
	  
	  _dbus_verbose ("Could not add %s to activation entry list: %s\n",
                         full_path_c, tmp_error.message);

          if (dbus_error_has_name (&tmp_error, DBUS_ERROR_NO_MEMORY))
            {
              dbus_move_error (&tmp_error, error);
              goto failed;
            }

          dbus_error_free (&tmp_error);
	  continue;
	}
      else
        {
          bus_desktop_file_free (desktop_file);
          desktop_file = NULL;
          continue;
        }
    }

  if (dbus_error_is_set (&tmp_error))
    {
      dbus_move_error (&tmp_error, error);
      goto failed;
    }
  
  return TRUE;
  
 failed:
  _DBUS_ASSERT_ERROR_IS_SET (error);
  
  if (iter != NULL)
    _dbus_directory_close (iter);
  if (desktop_file)
    bus_desktop_file_free (desktop_file);
  _dbus_string_free (&filename);
  _dbus_string_free (&full_path);
  
  return FALSE;
}

dbus_bool_t
bus_activation_init (const char  *address,
		     const char **directories,
                     DBusError   *error)
{
  int i;

  _dbus_assert (server_address == NULL);
  _dbus_assert (activation_entries == NULL);
  
  /* FIXME: We should split up the server addresses. */
  server_address = _dbus_strdup (address);
  if (server_address == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }
  
  activation_entries = _dbus_hash_table_new (DBUS_HASH_STRING, NULL,
                                             (DBusFreeFunction)bus_activation_entry_free);
  if (activation_entries == NULL)
    {      
      BUS_SET_OOM (error);
      goto failed;
    }

  /* Load service files */
  i = 0;
  while (directories[i] != NULL)
    {
      if (!load_directory (directories[i], error))
        goto failed;
      ++i;
    }

  return TRUE;
  
 failed:
  dbus_free (server_address);
  if (activation_entries)
    _dbus_hash_table_unref (activation_entries);
  
  return FALSE;
}

static void
child_setup (void *data)
{
  /* If no memory, we simply have the child exit, so it won't try
   * to connect to the wrong thing.
   */
  if (!_dbus_setenv ("DBUS_ADDRESS", server_address))
    _dbus_exit (1);
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

  /* FIXME we need to support a full command line, not just a single
   * argv[0]
   */
  
  /* Now try to spawn the process */
  argv[0] = entry->exec;
  argv[1] = NULL;

  if (!_dbus_spawn_async (argv,
			  child_setup, NULL, 
			  error))
    return FALSE;

  return TRUE;
}
