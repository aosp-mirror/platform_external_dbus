/* -*- mode: C; c-file-style: "gnu" -*- */
/* activation.c  Activation of services
 *
 * Copyright (C) 2003  CodeFactory AB
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
#include "activation.h"
#include "desktop-file.h"
#include "services.h"
#include "utils.h"
#include <dbus/dbus-internals.h>
#include <dbus/dbus-hash.h>
#include <dbus/dbus-list.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#define DBUS_SERVICE_SECTION "D-BUS Service"
#define DBUS_SERVICE_NAME "Name"
#define DBUS_SERVICE_EXEC "Exec"

struct BusActivation
{
  int refcount;
  DBusHashTable *entries;
  DBusHashTable *pending_activations;
  char *server_address;
  BusContext *context;
};

typedef struct
{
  char *name;
  char *exec;
} BusActivationEntry;

typedef struct BusPendingActivationEntry BusPendingActivationEntry;

struct BusPendingActivationEntry
{
  DBusMessage *activation_message;
  DBusConnection *connection;
};

typedef struct
{
  char *service_name;
  DBusList *entries;
} BusPendingActivation;

static void
bus_pending_activation_entry_free (BusPendingActivationEntry *entry)
{
  if (entry->activation_message)
    dbus_message_unref (entry->activation_message);
  
  if (entry->connection)
    dbus_connection_unref (entry->connection);

  dbus_free (entry);
}

static void
bus_pending_activation_free (BusPendingActivation *activation)
{
  DBusList *link;
  
  if (!activation)
    return;

  dbus_free (activation->service_name);

  link = _dbus_list_get_first_link (&activation->entries);

  while (link != NULL)
    {
      BusPendingActivationEntry *entry = link->data;

      bus_pending_activation_entry_free (entry);

      link = _dbus_list_get_next_link (&activation->entries, link);
    }
  _dbus_list_clear (&activation->entries);
  
  dbus_free (activation);
}

static void
bus_activation_entry_free (BusActivationEntry *entry)
{
  if (!entry)
    return;
  
  dbus_free (entry->name);
  dbus_free (entry->exec);

  dbus_free (entry);
}

static dbus_bool_t
add_desktop_file_entry (BusActivation  *activation,
                        BusDesktopFile *desktop_file,
                        DBusError      *error)
{
  char *name, *exec;
  BusActivationEntry *entry;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
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
  if (_dbus_hash_table_lookup_string (activation->entries, name))
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

  if (!_dbus_hash_table_insert_string (activation->entries, entry->name, entry))
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
load_directory (BusActivation *activation,
                const char    *directory,
                DBusError     *error)
{
  DBusDirIter *iter;
  DBusString dir, filename;
  DBusString full_path;
  BusDesktopFile *desktop_file;
  DBusError tmp_error;
  dbus_bool_t retval;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  _dbus_string_init_const (&dir, directory);

  iter = NULL;
  desktop_file = NULL;
  
  if (!_dbus_string_init (&filename))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!_dbus_string_init (&full_path))
    {
      BUS_SET_OOM (error);
      _dbus_string_free (&filename);
      return FALSE;
    }

  retval = FALSE;
  
  /* from this point it's safe to "goto out" */
  
  iter = _dbus_directory_open (&dir, error);
  if (iter == NULL)
    {
      _dbus_verbose ("Failed to open directory %s: %s\n",
                     directory, error ? error->message : "unknown");
      goto out;
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
          goto out;
        }
      
      if (!_dbus_string_ends_with_c_str (&filename, ".service"))
	{
          _dbus_verbose ("Skipping non-.service file %s\n",
                         _dbus_string_get_const_data (&filename));
          continue;
	}
      
      desktop_file = bus_desktop_file_load (&full_path, &tmp_error);

      if (desktop_file == NULL)
	{
	  _dbus_verbose ("Could not load %s: %s\n",
                         _dbus_string_get_const_data (&full_path),
			 tmp_error.message);

          if (dbus_error_has_name (&tmp_error, DBUS_ERROR_NO_MEMORY))
            {
              dbus_move_error (&tmp_error, error);
              goto out;
            }
          
	  dbus_error_free (&tmp_error);
	  continue;
	}

      if (!add_desktop_file_entry (activation, desktop_file, &tmp_error))
	{
          bus_desktop_file_free (desktop_file);
          desktop_file = NULL;
	  
	  _dbus_verbose ("Could not add %s to activation entry list: %s\n",
                         _dbus_string_get_const_data (&full_path), tmp_error.message);

          if (dbus_error_has_name (&tmp_error, DBUS_ERROR_NO_MEMORY))
            {
              dbus_move_error (&tmp_error, error);
              goto out;
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
      goto out;
    }
  
  retval = TRUE;
  
 out:
  if (!retval)
    _DBUS_ASSERT_ERROR_IS_SET (error);
  else
    _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  if (iter != NULL)
    _dbus_directory_close (iter);
  if (desktop_file)
    bus_desktop_file_free (desktop_file);
  _dbus_string_free (&filename);
  _dbus_string_free (&full_path);
  
  return retval;
}

BusActivation*
bus_activation_new (BusContext        *context,
		    const DBusString  *address,
                    DBusList         **directories,
                    DBusError         *error)
{
  BusActivation *activation;
  DBusList *link;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  activation = dbus_new0 (BusActivation, 1);
  if (activation == NULL)
    {
      BUS_SET_OOM (error);
      return NULL;
    }
  
  activation->refcount = 1;
  activation->context = context;
  
  if (!_dbus_string_copy_data (address, &activation->server_address))
    {
      BUS_SET_OOM (error);
      goto failed;
    }
  
  activation->entries = _dbus_hash_table_new (DBUS_HASH_STRING, NULL,
                                             (DBusFreeFunction)bus_activation_entry_free);
  if (activation->entries == NULL)
    {      
      BUS_SET_OOM (error);
      goto failed;
    }

  activation->pending_activations = _dbus_hash_table_new (DBUS_HASH_STRING, NULL,
							  (DBusFreeFunction)bus_pending_activation_free);

  if (activation->pending_activations == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }
  
  /* Load service files */
  link = _dbus_list_get_first_link (directories);
  while (link != NULL)
    {
      if (!load_directory (activation, link->data, error))
        goto failed;
      link = _dbus_list_get_next_link (directories, link);
    }

  return activation;
  
 failed:
  bus_activation_unref (activation);  
  return NULL;
}

void
bus_activation_ref (BusActivation *activation)
{
  _dbus_assert (activation->refcount > 0);
  
  activation->refcount += 1;
}

void
bus_activation_unref (BusActivation *activation)
{
  _dbus_assert (activation->refcount > 0);

  activation->refcount -= 1;

  if (activation->refcount == 0)
    {
      dbus_free (activation->server_address);
      if (activation->entries)
        _dbus_hash_table_unref (activation->entries);
      if (activation->pending_activations)
	_dbus_hash_table_unref (activation->pending_activations);
      dbus_free (activation);
    }
}

static void
child_setup (void *data)
{
  BusActivation *activation = data;
  const char *type;
  
  /* If no memory, we simply have the child exit, so it won't try
   * to connect to the wrong thing.
   */
  if (!_dbus_setenv ("DBUS_ACTIVATION_ADDRESS", activation->server_address))
    _dbus_exit (1);

  type = bus_context_get_type (activation->context);
  if (type != NULL)
    {
      if (!_dbus_setenv ("DBUS_BUS_TYPE", type))
        _dbus_exit (1);
    }
}

dbus_bool_t
bus_activation_service_created (BusActivation  *activation,
				const char     *service_name,
                                BusTransaction *transaction,
				DBusError      *error)
{
  BusPendingActivation *pending_activation;
  DBusMessage *message;
  DBusList *link;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  /* Check if it's a pending activation */
  pending_activation = _dbus_hash_table_lookup_string (activation->pending_activations, service_name);

  if (!pending_activation)
    return TRUE;

  link = _dbus_list_get_first_link (&pending_activation->entries);
  while (link != NULL)
    {
      BusPendingActivationEntry *entry = link->data;
      DBusList *next = _dbus_list_get_next_link (&pending_activation->entries, link);
      
      if (dbus_connection_get_is_connected (entry->connection))
	{
	  message = dbus_message_new_reply (entry->activation_message);
	  if (!message)
	    {
	      BUS_SET_OOM (error);
	      goto error;
	    }

	  if (!dbus_message_set_sender (message, DBUS_SERVICE_DBUS) ||
              !dbus_message_append_args (message,
					 DBUS_TYPE_UINT32, DBUS_ACTIVATION_REPLY_ACTIVATED,
					 0))
	    {
	      dbus_message_unref (message);
	      BUS_SET_OOM (error);
	      goto error;
	    }
          
	  if (!bus_transaction_send_message (transaction, entry->connection, message))
	    {
	      dbus_message_unref (message);
	      BUS_SET_OOM (error);
	      goto error;
	    }
	}

      bus_pending_activation_entry_free (entry);
      
      _dbus_list_remove_link (&pending_activation->entries, link);      
      link = next;
    }
  
  _dbus_hash_table_remove_string (activation->pending_activations, service_name);

  return TRUE;

 error:
  _dbus_hash_table_remove_string (activation->pending_activations, service_name);
  return FALSE;
}

dbus_bool_t
bus_activation_activate_service (BusActivation  *activation,
				 DBusConnection *connection,
                                 BusTransaction *transaction,
				 DBusMessage    *activation_message,
                                 const char     *service_name,
				 DBusError      *error)
{
  BusActivationEntry *entry;
  BusPendingActivation *pending_activation;
  BusPendingActivationEntry *pending_activation_entry;
  DBusMessage *message;
  DBusString service_str;
  char *argv[2];
  dbus_bool_t retval;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  entry = _dbus_hash_table_lookup_string (activation->entries, service_name);

  if (!entry)
    {
      dbus_set_error (error, DBUS_ERROR_ACTIVATE_SERVICE_NOT_FOUND,
		      "The service %s was not found in the activation entry list",
		      service_name);
      return FALSE;
    }

  /* Check if the service is active */
  _dbus_string_init_const (&service_str, service_name);
  if (bus_registry_lookup (bus_context_get_registry (activation->context), &service_str) != NULL)
    {
      message = dbus_message_new_reply (activation_message);

      if (!message)
	{
	  BUS_SET_OOM (error);
	  return FALSE;
	}

      if (!dbus_message_set_sender (message, DBUS_SERVICE_DBUS) ||
          !dbus_message_append_args (message,
				     DBUS_TYPE_UINT32, DBUS_ACTIVATION_REPLY_ALREADY_ACTIVE, 
				     0))
	{
	  BUS_SET_OOM (error);
	  dbus_message_unref (message);
	  return FALSE;
	}

      retval = bus_transaction_send_message (transaction, connection, message);
      dbus_message_unref (message);
      if (!retval)
	BUS_SET_OOM (error);

      return retval;
    }

  pending_activation_entry = dbus_new0 (BusPendingActivationEntry, 1);
  if (!pending_activation_entry)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  pending_activation_entry->activation_message = activation_message;
  dbus_message_ref (activation_message);
  pending_activation_entry->connection = connection;
  dbus_connection_ref (connection);
  
  /* Check if the service is being activated */
  pending_activation = _dbus_hash_table_lookup_string (activation->pending_activations, service_name);
  if (pending_activation)
    {
      if (!_dbus_list_append (&pending_activation->entries, pending_activation_entry))
	{
	  BUS_SET_OOM (error);
	  bus_pending_activation_entry_free (pending_activation_entry);

	  return FALSE;
	}
    }
  else
    {
      pending_activation = dbus_new0 (BusPendingActivation, 1);
      if (!pending_activation)
	{
	  BUS_SET_OOM (error);
	  bus_pending_activation_entry_free (pending_activation_entry);	  
	  return FALSE;
	}
      pending_activation->service_name = _dbus_strdup (service_name);
      if (!pending_activation->service_name)
	{
	  BUS_SET_OOM (error);
	  bus_pending_activation_free (pending_activation);
	  bus_pending_activation_entry_free (pending_activation_entry);	  
	  return FALSE;
	}

      if (!_dbus_list_append (&pending_activation->entries, pending_activation_entry))
	{
	  BUS_SET_OOM (error);
	  bus_pending_activation_free (pending_activation);
	  bus_pending_activation_entry_free (pending_activation_entry);	  
	  return FALSE;
	}
      
      if (!_dbus_hash_table_insert_string (activation->pending_activations,
					   pending_activation->service_name, pending_activation))
	{
	  BUS_SET_OOM (error);
	  bus_pending_activation_free (pending_activation);
	  return FALSE;
	}
    }
  
  /* FIXME we need to support a full command line, not just a single
   * argv[0]
   */
  
  /* Now try to spawn the process */
  argv[0] = entry->exec;
  argv[1] = NULL;

  if (!_dbus_spawn_async (argv,
			  child_setup, activation, 
			  error))
    {
      _dbus_hash_table_remove_string (activation->pending_activations,
				      pending_activation->service_name);
      return FALSE;
    }
  
  return TRUE;
}
