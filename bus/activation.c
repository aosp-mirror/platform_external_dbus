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
#include <dbus/dbus-spawn.h>
#include <dbus/dbus-timeout.h>
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
  int refcount;
  BusActivation *activation;
  char *service_name;
  DBusList *entries;
  DBusBabysitter *babysitter;
  DBusTimeout *timeout;
  unsigned int timeout_added : 1;
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
handle_timeout_callback (DBusTimeout   *timeout,
                         void          *data)
{
  BusPendingActivation *pending_activation = data;

  while (!dbus_timeout_handle (pending_activation->timeout))
    _dbus_wait_for_memory ();
}

static void
bus_pending_activation_ref (BusPendingActivation *pending_activation)
{
  _dbus_assert (pending_activation->refcount > 0);
  pending_activation->refcount += 1;
}

static void
bus_pending_activation_unref (BusPendingActivation *pending_activation)
{
  DBusList *link;
  
  if (pending_activation == NULL) /* hash table requires this */
    return;

  _dbus_assert (pending_activation->refcount > 0);
  pending_activation->refcount -= 1;

  if (pending_activation->refcount > 0)
    return;
  
  if (pending_activation->timeout_added)
    {
      _dbus_loop_remove_timeout (bus_context_get_loop (pending_activation->activation->context),
                                 pending_activation->timeout,
                                 handle_timeout_callback, pending_activation);
      pending_activation->timeout_added = FALSE;
    }

  if (pending_activation->timeout)
    _dbus_timeout_unref (pending_activation->timeout);
  
  if (pending_activation->babysitter)
    {
      if (!_dbus_babysitter_set_watch_functions (pending_activation->babysitter,
                                                 NULL, NULL, NULL,
                                                 pending_activation->babysitter,
                                                 NULL))
        _dbus_assert_not_reached ("setting watch functions to NULL failed");
      
      _dbus_babysitter_unref (pending_activation->babysitter);
    }
  
  dbus_free (pending_activation->service_name);

  link = _dbus_list_get_first_link (&pending_activation->entries);

  while (link != NULL)
    {
      BusPendingActivationEntry *entry = link->data;

      bus_pending_activation_entry_free (entry);

      link = _dbus_list_get_next_link (&pending_activation->entries, link);
    }
  _dbus_list_clear (&pending_activation->entries);
  
  dbus_free (pending_activation);
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
							  (DBusFreeFunction)bus_pending_activation_unref);

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

typedef struct
{
  BusPendingActivation *pending_activation;
  DBusPreallocatedHash *hash_entry;
} RestorePendingData;

static void
restore_pending (void *data)
{
  RestorePendingData *d = data;

  _dbus_assert (d->pending_activation != NULL);
  _dbus_assert (d->hash_entry != NULL);

  _dbus_verbose ("Restoring pending activation for service %s, has timeout = %d\n",
                 d->pending_activation->service_name,
                 d->pending_activation->timeout_added);
  
  _dbus_hash_table_insert_string_preallocated (d->pending_activation->activation->pending_activations,
                                               d->hash_entry,
                                               d->pending_activation->service_name, d->pending_activation);

  bus_pending_activation_ref (d->pending_activation);
  
  d->hash_entry = NULL;
}

static void
free_pending_restore_data (void *data)
{
  RestorePendingData *d = data;

  if (d->hash_entry)
    _dbus_hash_table_free_preallocated_entry (d->pending_activation->activation->pending_activations,
                                              d->hash_entry);

  bus_pending_activation_unref (d->pending_activation);
  
  dbus_free (d);
}

static dbus_bool_t
add_restore_pending_to_transaction (BusTransaction       *transaction,
                                    BusPendingActivation *pending_activation)
{
  RestorePendingData *d;

  d = dbus_new (RestorePendingData, 1);
  if (d == NULL)
    return FALSE;
  
  d->pending_activation = pending_activation;
  d->hash_entry = _dbus_hash_table_preallocate_entry (d->pending_activation->activation->pending_activations);
  
  bus_pending_activation_ref (d->pending_activation);
  
  if (d->hash_entry == NULL ||
      !bus_transaction_add_cancel_hook (transaction, restore_pending, d,
                                        free_pending_restore_data))
    {
      free_pending_restore_data (d);
      return FALSE;
    }

  _dbus_verbose ("Saved pending activation to be restored if the transaction fails\n");
  
  return TRUE;
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

	  if (!dbus_message_append_args (message,
					 DBUS_TYPE_UINT32, DBUS_ACTIVATION_REPLY_ACTIVATED,
					 0))
	    {
	      dbus_message_unref (message);
	      BUS_SET_OOM (error);
	      goto error;
	    }
          
	  if (!bus_transaction_send_from_driver (transaction, entry->connection, message))
	    {
	      dbus_message_unref (message);
	      BUS_SET_OOM (error);
	      goto error;
	    }
          
          dbus_message_unref (message);
	}

      link = next;
    }

  if (!add_restore_pending_to_transaction (transaction, pending_activation))
    {
      _dbus_verbose ("Could not add cancel hook to transaction to revert removing pending activation\n");
      BUS_SET_OOM (error);
      goto error;
    }
  
  _dbus_hash_table_remove_string (activation->pending_activations, service_name);

  return TRUE;

 error:
  return FALSE;
}

/**
 * FIXME @todo the error messages here would ideally be preallocated
 * so we don't need to allocate memory to send them.
 * Using the usual tactic, prealloc an OOM message, then
 * if we can't alloc the real error send the OOM error instead.
 */
static dbus_bool_t
try_send_activation_failure (BusPendingActivation *pending_activation,
                             const DBusError      *how)
{
  BusActivation *activation;
  DBusMessage *message;
  DBusList *link;
  BusTransaction *transaction;
  
  activation = pending_activation->activation;

  transaction = bus_transaction_new (activation->context);
  if (transaction == NULL)
    return FALSE;
  
  link = _dbus_list_get_first_link (&pending_activation->entries);
  while (link != NULL)
    {
      BusPendingActivationEntry *entry = link->data;
      DBusList *next = _dbus_list_get_next_link (&pending_activation->entries, link);
      
      if (dbus_connection_get_is_connected (entry->connection))
	{
	  message = dbus_message_new_error_reply (entry->activation_message,
                                                  how->name,
                                                  how->message);
	  if (!message)
            goto error;
          
	  if (!bus_transaction_send_from_driver (transaction, entry->connection, message))
	    {
	      dbus_message_unref (message);
	      goto error;
	    }

          dbus_message_unref (message);
	}

      link = next;
    }

  bus_transaction_execute_and_free (transaction);
  
  return TRUE;

 error:
  if (transaction)
    bus_transaction_cancel_and_free (transaction);
  return FALSE;
}

/**
 * Free the pending activation and send an error message to all the
 * connections that were waiting for it.
 */
static void
pending_activation_failed (BusPendingActivation *pending_activation,
                           const DBusError      *how)
{
  /* FIXME use preallocated OOM messages instead of bus_wait_for_memory() */
  while (!try_send_activation_failure (pending_activation, how))
    _dbus_wait_for_memory ();

  /* Destroy this pending activation */
  _dbus_hash_table_remove_string (pending_activation->activation->pending_activations,
                                  pending_activation->service_name);
}

static dbus_bool_t
babysitter_watch_callback (DBusWatch     *watch,
                           unsigned int   condition,
                           void          *data)
{
  BusPendingActivation *pending_activation = data;
  dbus_bool_t retval;
  DBusBabysitter *babysitter;

  babysitter = pending_activation->babysitter;
  
  _dbus_babysitter_ref (babysitter);
  
  retval = _dbus_babysitter_handle_watch (babysitter, watch, condition);

  if (_dbus_babysitter_get_child_exited (babysitter))
    {
      DBusError error;

      dbus_error_init (&error);
      _dbus_babysitter_set_child_exit_error (babysitter, &error);

      /* Destroys the pending activation */
      pending_activation_failed (pending_activation, &error);

      dbus_error_free (&error);
    }
  
  _dbus_babysitter_unref (babysitter);

  return retval;
}

static dbus_bool_t
add_babysitter_watch (DBusWatch      *watch,
                      void           *data)
{
  BusPendingActivation *pending_activation = data;

  return _dbus_loop_add_watch (bus_context_get_loop (pending_activation->activation->context),
                               watch, babysitter_watch_callback, pending_activation,
                               NULL);
}

static void
remove_babysitter_watch (DBusWatch      *watch,
                         void           *data)
{
  BusPendingActivation *pending_activation = data;
  
  _dbus_loop_remove_watch (bus_context_get_loop (pending_activation->activation->context),
                           watch, babysitter_watch_callback, pending_activation);
}

static dbus_bool_t
pending_activation_timed_out (void *data)
{
  BusPendingActivation *pending_activation = data;
  DBusError error;
  
  /* Kill the spawned process, since it sucks
   * (not sure this is what we want to do, but
   * may as well try it for now)
   */
  _dbus_babysitter_kill_child (pending_activation->babysitter);

  dbus_error_init (&error);

  dbus_set_error (&error, DBUS_ERROR_TIMED_OUT,
                  "Activation of %s timed out",
                  pending_activation->service_name);

  pending_activation_failed (pending_activation, &error);

  dbus_error_free (&error);

  return TRUE;
}

static void
cancel_pending (void *data)
{
  BusPendingActivation *pending_activation = data;

  _dbus_verbose ("Canceling pending activation of %s\n",
                 pending_activation->service_name);

  if (pending_activation->babysitter)
    _dbus_babysitter_kill_child (pending_activation->babysitter);
  
  _dbus_hash_table_remove_string (pending_activation->activation->pending_activations,
                                  pending_activation->service_name);
}

static void
free_pending_cancel_data (void *data)
{
  BusPendingActivation *pending_activation = data;
  
  bus_pending_activation_unref (pending_activation);
}

static dbus_bool_t
add_cancel_pending_to_transaction (BusTransaction       *transaction,
                                   BusPendingActivation *pending_activation)
{  
  if (!bus_transaction_add_cancel_hook (transaction, cancel_pending,
                                        pending_activation,
                                        free_pending_cancel_data))
    return FALSE;

  bus_pending_activation_ref (pending_activation); 
  
  _dbus_verbose ("Saved pending activation to be canceled if the transaction fails\n");
  
  return TRUE;
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
      _dbus_verbose ("Service \"%s\" is already active\n", service_name);
      
      message = dbus_message_new_reply (activation_message);

      if (!message)
	{
          _dbus_verbose ("No memory to create reply to activate message\n");
	  BUS_SET_OOM (error);
	  return FALSE;
	}

      if (!dbus_message_append_args (message,
				     DBUS_TYPE_UINT32, DBUS_ACTIVATION_REPLY_ALREADY_ACTIVE, 
				     0))
	{
          _dbus_verbose ("No memory to set args of reply to activate message\n");
	  BUS_SET_OOM (error);
	  dbus_message_unref (message);
	  return FALSE;
	}

      retval = bus_transaction_send_from_driver (transaction, connection, message);
      dbus_message_unref (message);
      if (!retval)
        {
          _dbus_verbose ("Failed to send reply\n");
          BUS_SET_OOM (error);
        }

      return retval;
    }

  pending_activation_entry = dbus_new0 (BusPendingActivationEntry, 1);
  if (!pending_activation_entry)
    {
      _dbus_verbose ("Failed to create pending activation entry\n");
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
      /* FIXME security - a client could keep sending activations over and
       * over, growing this queue.
       */
      if (!_dbus_list_append (&pending_activation->entries, pending_activation_entry))
	{
          _dbus_verbose ("Failed to append a new entry to pending activation\n");
          
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
          _dbus_verbose ("Failed to create pending activation\n");
          
	  BUS_SET_OOM (error);
	  bus_pending_activation_entry_free (pending_activation_entry);	  
	  return FALSE;
	}

      pending_activation->activation = activation;
      pending_activation->refcount = 1;
      
      pending_activation->service_name = _dbus_strdup (service_name);
      if (!pending_activation->service_name)
	{
          _dbus_verbose ("Failed to copy service name for pending activation\n");
          
	  BUS_SET_OOM (error);
	  bus_pending_activation_unref (pending_activation);
	  bus_pending_activation_entry_free (pending_activation_entry);	  
	  return FALSE;
	}

      pending_activation->timeout =
        _dbus_timeout_new (bus_context_get_activation_timeout (activation->context),
                           pending_activation_timed_out,
                           pending_activation,
                           NULL);
      if (!pending_activation->timeout)
	{
          _dbus_verbose ("Failed to create timeout for pending activation\n");
          
	  BUS_SET_OOM (error);
	  bus_pending_activation_unref (pending_activation);
	  bus_pending_activation_entry_free (pending_activation_entry);	  
	  return FALSE;
	}

      if (!_dbus_loop_add_timeout (bus_context_get_loop (activation->context),
                                   pending_activation->timeout,
                                   handle_timeout_callback,
                                   pending_activation,
                                   NULL))
	{
          _dbus_verbose ("Failed to add timeout for pending activation\n");
          
	  BUS_SET_OOM (error);
	  bus_pending_activation_unref (pending_activation);
	  bus_pending_activation_entry_free (pending_activation_entry);	  
	  return FALSE;
	}

      pending_activation->timeout_added = TRUE;
      
      if (!_dbus_list_append (&pending_activation->entries, pending_activation_entry))
	{
          _dbus_verbose ("Failed to add entry to just-created pending activation\n");
          
	  BUS_SET_OOM (error);
	  bus_pending_activation_unref (pending_activation);
	  bus_pending_activation_entry_free (pending_activation_entry);	  
	  return FALSE;
	}
      
      if (!_dbus_hash_table_insert_string (activation->pending_activations,
					   pending_activation->service_name,
                                           pending_activation))
	{
          _dbus_verbose ("Failed to put pending activation in hash table\n");
          
	  BUS_SET_OOM (error);
	  bus_pending_activation_unref (pending_activation);
	  return FALSE;
	}
    }

  if (!add_cancel_pending_to_transaction (transaction, pending_activation))
    {
      _dbus_verbose ("Failed to add pending activation cancel hook to transaction\n");
      BUS_SET_OOM (error);
      _dbus_hash_table_remove_string (activation->pending_activations,
				      pending_activation->service_name);
      return FALSE;
    }
  
  /* FIXME we need to support a full command line, not just a single
   * argv[0]
   */
  
  /* Now try to spawn the process */
  argv[0] = entry->exec;
  argv[1] = NULL;

  if (!_dbus_spawn_async_with_babysitter (&pending_activation->babysitter, argv,
                                          child_setup, activation, 
                                          error))
    {
      _dbus_verbose ("Failed to spawn child\n");
      _DBUS_ASSERT_ERROR_IS_SET (error);
      return FALSE;
    }

  _dbus_assert (pending_activation->babysitter != NULL);
  
  if (!_dbus_babysitter_set_watch_functions (pending_activation->babysitter,
                                             add_babysitter_watch,
                                             remove_babysitter_watch,
                                             NULL,
                                             pending_activation,
                                             NULL))
    {
      BUS_SET_OOM (error);
      _dbus_verbose ("Failed to set babysitter watch functions\n");
      return FALSE;
    }
  
  return TRUE;
}
