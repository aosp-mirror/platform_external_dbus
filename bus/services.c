/* -*- mode: C; c-file-style: "gnu" -*- */
/* services.c  Service management
 *
 * Copyright (C) 2003  Red Hat, Inc.
 * Copyright (C) 2003  CodeFactory AB
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <dbus/dbus-hash.h>
#include <dbus/dbus-list.h>
#include <dbus/dbus-mempool.h>
#include <dbus/dbus-marshal-validate.h>

#include "driver.h"
#include "services.h"
#include "connection.h"
#include "utils.h"
#include "activation.h"
#include "policy.h"
#include "bus.h"
#include "selinux.h"

struct BusService
{
  int refcount;

  BusRegistry *registry;
  char *name;
  DBusList *owners;
  
  unsigned int prohibit_replacement : 1;
};

struct BusRegistry
{
  int refcount;

  BusContext *context;
  
  DBusHashTable *service_hash;
  DBusMemPool   *service_pool;

  DBusHashTable *service_sid_table;
};

BusRegistry*
bus_registry_new (BusContext *context)
{
  BusRegistry *registry;

  registry = dbus_new0 (BusRegistry, 1);
  if (registry == NULL)
    return NULL;

  registry->refcount = 1;
  registry->context = context;
  
  registry->service_hash = _dbus_hash_table_new (DBUS_HASH_STRING,
                                                 NULL, NULL);
  if (registry->service_hash == NULL)
    goto failed;
  
  registry->service_pool = _dbus_mem_pool_new (sizeof (BusService),
                                               TRUE);
  if (registry->service_pool == NULL)
    goto failed;

  registry->service_sid_table = NULL;
  
  return registry;

 failed:
  bus_registry_unref (registry);
  return NULL;
}

BusRegistry *
bus_registry_ref (BusRegistry *registry)
{
  _dbus_assert (registry->refcount > 0);
  registry->refcount += 1;

  return registry;
}

void
bus_registry_unref  (BusRegistry *registry)
{
  _dbus_assert (registry->refcount > 0);
  registry->refcount -= 1;

  if (registry->refcount == 0)
    {
      if (registry->service_hash)
        _dbus_hash_table_unref (registry->service_hash);
      if (registry->service_pool)
        _dbus_mem_pool_free (registry->service_pool);
      if (registry->service_sid_table)
        _dbus_hash_table_unref (registry->service_sid_table);
      
      dbus_free (registry);
    }
}

BusService*
bus_registry_lookup (BusRegistry      *registry,
                     const DBusString *service_name)
{
  BusService *service;

  service = _dbus_hash_table_lookup_string (registry->service_hash,
                                            _dbus_string_get_const_data (service_name));

  return service;
}

BusService*
bus_registry_ensure (BusRegistry               *registry,
                     const DBusString          *service_name,
                     DBusConnection            *owner_if_created,
                     BusTransaction            *transaction,
                     DBusError                 *error)
{
  BusService *service;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  _dbus_assert (owner_if_created != NULL);
  _dbus_assert (transaction != NULL);

  service = _dbus_hash_table_lookup_string (registry->service_hash,
                                            _dbus_string_get_const_data (service_name));
  if (service != NULL)
    return service;
  
  service = _dbus_mem_pool_alloc (registry->service_pool);
  if (service == NULL)
    {
      BUS_SET_OOM (error);
      return NULL;
    }

  service->registry = registry;  
  service->refcount = 1;

  _dbus_verbose ("copying string %p '%s' to service->name\n",
                 service_name, _dbus_string_get_const_data (service_name));
  if (!_dbus_string_copy_data (service_name, &service->name))
    {
      _dbus_mem_pool_dealloc (registry->service_pool, service);
      BUS_SET_OOM (error);
      return NULL;
    }
  _dbus_verbose ("copied string %p '%s' to '%s'\n",
                 service_name, _dbus_string_get_const_data (service_name),
                 service->name);

  if (!bus_driver_send_service_owner_changed (service->name, 
					      NULL,
					      bus_connection_get_name (owner_if_created),
					      transaction, error))
    {
      bus_service_unref (service);
      return NULL;
    }

  if (!bus_activation_service_created (bus_context_get_activation (registry->context),
				       service->name, transaction, error))
    {
      bus_service_unref (service);
      return NULL;
    }
  
  if (!bus_service_add_owner (service, owner_if_created,
                              transaction, error))
    {
      bus_service_unref (service);
      return NULL;
    }
  
  if (!_dbus_hash_table_insert_string (registry->service_hash,
                                       service->name,
                                       service))
    {
      /* The add_owner gets reverted on transaction cancel */
      BUS_SET_OOM (error);
      return NULL;
    }
  
  return service;
}

void
bus_registry_foreach (BusRegistry               *registry,
                      BusServiceForeachFunction  function,
                      void                      *data)
{
  DBusHashIter iter;
  
  _dbus_hash_iter_init (registry->service_hash, &iter);
  while (_dbus_hash_iter_next (&iter))
    {
      BusService *service = _dbus_hash_iter_get_value (&iter);

      (* function) (service, data);
    }
}

dbus_bool_t
bus_registry_list_services (BusRegistry *registry,
                            char      ***listp,
                            int         *array_len)
{
  int i, j, len;
  char **retval;
  DBusHashIter iter;
   
  len = _dbus_hash_table_get_n_entries (registry->service_hash);
  retval = dbus_new (char *, len + 1);

  if (retval == NULL)
    return FALSE;

  _dbus_hash_iter_init (registry->service_hash, &iter);
  i = 0;
  while (_dbus_hash_iter_next (&iter))
    {
      BusService *service = _dbus_hash_iter_get_value (&iter);

      retval[i] = _dbus_strdup (service->name);
      if (retval[i] == NULL)
	goto error;

      i++;
    }

  retval[i] = NULL;
  
  if (array_len)
    *array_len = len;
  
  *listp = retval;
  return TRUE;
  
 error:
  for (j = 0; j < i; j++)
    dbus_free (retval[i]);
  dbus_free (retval);

  return FALSE;
}

dbus_bool_t
bus_registry_acquire_service (BusRegistry      *registry,
                              DBusConnection   *connection,
                              const DBusString *service_name,
                              dbus_uint32_t     flags,
                              dbus_uint32_t    *result,
                              BusTransaction   *transaction,
                              DBusError        *error)
{
  dbus_bool_t retval;
  DBusConnection *old_owner;
  DBusConnection *current_owner;
  BusClientPolicy *policy;
  BusService *service;
  BusActivation  *activation;
  BusSELinuxID *sid;
  
  retval = FALSE;

  if (!_dbus_validate_bus_name (service_name, 0,
                                _dbus_string_get_length (service_name)))
    {
      dbus_set_error (error, DBUS_ERROR_INVALID_ARGS,
                      "Requested bus name \"%s\" is not valid",
                      _dbus_string_get_const_data (service_name));
      
      _dbus_verbose ("Attempt to acquire invalid service name\n");
      
      goto out;
    }
  
  if (_dbus_string_get_byte (service_name, 0) == ':')
    {
      /* Not allowed; only base services can start with ':' */
      dbus_set_error (error, DBUS_ERROR_INVALID_ARGS,
                      "Cannot acquire a service starting with ':' such as \"%s\"",
                      _dbus_string_get_const_data (service_name));
      
      _dbus_verbose ("Attempt to acquire invalid base service name \"%s\"",
                     _dbus_string_get_const_data (service_name));
      
      goto out;
    }

  policy = bus_connection_get_policy (connection);
  _dbus_assert (policy != NULL);

  /* Note that if sid is #NULL then the bus's own context gets used
   * in bus_connection_selinux_allows_acquire_service()
   */
  sid = bus_selinux_id_table_lookup (registry->service_sid_table,
                                     service_name);

  if (!bus_selinux_allows_acquire_service (connection, sid,
					   _dbus_string_get_const_data (service_name), error))
    {

      if (dbus_error_is_set (error) &&
	  dbus_error_has_name (error, DBUS_ERROR_NO_MEMORY))
	{
	  goto out;
	}

      dbus_set_error (error, DBUS_ERROR_ACCESS_DENIED,
                      "Connection \"%s\" is not allowed to own the service \"%s\" due "
                      "to SELinux policy",
                      bus_connection_is_active (connection) ?
                      bus_connection_get_name (connection) :
                      "(inactive)",
                      _dbus_string_get_const_data (service_name));
      goto out;
    }
      
  if (!bus_client_policy_check_can_own (policy, connection,
                                        service_name))
    {
      dbus_set_error (error, DBUS_ERROR_ACCESS_DENIED,
                      "Connection \"%s\" is not allowed to own the service \"%s\" due "
                      "to security policies in the configuration file",
                      bus_connection_is_active (connection) ?
                      bus_connection_get_name (connection) :
                      "(inactive)",
                      _dbus_string_get_const_data (service_name));
      goto out;
    }

  if (bus_connection_get_n_services_owned (connection) >=
      bus_context_get_max_services_per_connection (registry->context))
    {
      dbus_set_error (error, DBUS_ERROR_LIMITS_EXCEEDED,
                      "Connection \"%s\" is not allowed to own more services "
                      "(increase limits in configuration file if required)",
                      bus_connection_is_active (connection) ?
                      bus_connection_get_name (connection) :
                      "(inactive)");
      goto out;
    }
  
  service = bus_registry_lookup (registry, service_name);

  if (service != NULL)
    old_owner = bus_service_get_primary_owner (service);
  else
    old_owner = NULL;
      
  if (service == NULL)
    {
      service = bus_registry_ensure (registry,
                                     service_name, connection, transaction, error);
      if (service == NULL)
        goto out;
    }

  current_owner = bus_service_get_primary_owner (service);

  if (old_owner == NULL)
    {
      _dbus_assert (current_owner == connection);

      bus_service_set_prohibit_replacement (service,
					    (flags & DBUS_NAME_FLAG_PROHIBIT_REPLACEMENT));      
			
      *result = DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER;      
    }
  else if (old_owner == connection)
    *result = DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER;
  else if (!((flags & DBUS_NAME_FLAG_REPLACE_EXISTING)))
    *result = DBUS_REQUEST_NAME_REPLY_EXISTS;
  else if (bus_service_get_prohibit_replacement (service))
    {
      /* Queue the connection */
      if (!bus_service_add_owner (service, connection,
                                  transaction, error))
        goto out;
      
      *result = DBUS_REQUEST_NAME_REPLY_IN_QUEUE;
    }
  else
    {
      /* Replace the current owner */

      /* We enqueue the new owner and remove the first one because
       * that will cause NameAcquired and NameLost messages to
       * be sent.
       */
      
      if (!bus_service_add_owner (service, connection,
                                  transaction, error))
        goto out;

      if (!bus_service_remove_owner (service, old_owner,
                                     transaction, error))
        goto out;
      
      _dbus_assert (connection == bus_service_get_primary_owner (service));
      *result = DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER;
    }

  activation = bus_context_get_activation (registry->context);
  retval = bus_activation_send_pending_auto_activation_messages (activation,
								 service,
								 transaction,
								 error);
  
 out:
  return retval;
}

dbus_bool_t
bus_registry_set_service_context_table (BusRegistry   *registry,
					DBusHashTable *table)
{
  DBusHashTable *new_table;
  DBusHashIter iter;
  
  new_table = bus_selinux_id_table_new ();
  if (!new_table)
    return FALSE;

  _dbus_hash_iter_init (table, &iter);
  while (_dbus_hash_iter_next (&iter))
    {
      const char *service = _dbus_hash_iter_get_string_key (&iter);
      const char *context = _dbus_hash_iter_get_value (&iter);

      if (!bus_selinux_id_table_insert (new_table,
					service,
					context))
	return FALSE;
    }
  
  if (registry->service_sid_table)
    _dbus_hash_table_unref (registry->service_sid_table);
  registry->service_sid_table = new_table;
  return TRUE;
}

static void
bus_service_unlink_owner (BusService      *service,
                          DBusConnection  *owner)
{
  _dbus_list_remove_last (&service->owners, owner);
  bus_connection_remove_owned_service (owner, service);
}

static void
bus_service_unlink (BusService *service)
{
  _dbus_assert (service->owners == NULL);

  /* the service may not be in the hash, if
   * the failure causing transaction cancel
   * was in the right place, but that's OK
   */
  _dbus_hash_table_remove_string (service->registry->service_hash,
                                  service->name);
  
  bus_service_unref (service);
}

static void
bus_service_relink (BusService           *service,
                    DBusPreallocatedHash *preallocated)
{
  _dbus_assert (service->owners == NULL);
  _dbus_assert (preallocated != NULL);

  _dbus_hash_table_insert_string_preallocated (service->registry->service_hash,
                                               preallocated,
                                               service->name,
                                               service);
  
  bus_service_ref (service);
}

/**
 * Data used to represent an ownership cancellation in
 * a bus transaction.
 */
typedef struct
{
  DBusConnection *connection; /**< the connection */
  BusService *service;        /**< service to cancel ownership of */
} OwnershipCancelData;

static void
cancel_ownership (void *data)
{
  OwnershipCancelData *d = data;

  /* We don't need to send messages notifying of these
   * changes, since we're reverting something that was
   * cancelled (effectively never really happened)
   */
  bus_service_unlink_owner (d->service, d->connection);
  
  if (d->service->owners == NULL)
    bus_service_unlink (d->service);
}

static void
free_ownership_cancel_data (void *data)
{
  OwnershipCancelData *d = data;

  dbus_connection_unref (d->connection);
  bus_service_unref (d->service);
  
  dbus_free (d);
}

static dbus_bool_t
add_cancel_ownership_to_transaction (BusTransaction *transaction,
                                     BusService     *service,
                                     DBusConnection *connection)
{
  OwnershipCancelData *d;

  d = dbus_new (OwnershipCancelData, 1);
  if (d == NULL)
    return FALSE;
  
  d->service = service;
  d->connection = connection;

  if (!bus_transaction_add_cancel_hook (transaction, cancel_ownership, d,
                                        free_ownership_cancel_data))
    {
      dbus_free (d);
      return FALSE;
    }

  bus_service_ref (d->service);
  dbus_connection_ref (d->connection);
  
  return TRUE;
}

/* this function is self-cancelling if you cancel the transaction */
dbus_bool_t
bus_service_add_owner (BusService     *service,
                       DBusConnection *owner,
                       BusTransaction *transaction,
                       DBusError      *error)
{
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
 /* Send service acquired message first, OOM will result
  * in cancelling the transaction
  */
  if (service->owners == NULL)
    {
      if (!bus_driver_send_service_acquired (owner, service->name, transaction, error))
        return FALSE;
    }
  
  if (!_dbus_list_append (&service->owners,
                          owner))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!bus_connection_add_owned_service (owner, service))
    {
      _dbus_list_remove_last (&service->owners, owner);
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!add_cancel_ownership_to_transaction (transaction,
                                            service,
                                            owner))
    {
      bus_service_unlink_owner (service, owner);
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  return TRUE;
}

typedef struct
{
  DBusConnection *connection;
  BusService     *service;
  DBusConnection *before_connection; /* restore to position before this connection in owners list */
  DBusList       *connection_link;
  DBusList       *service_link;
  DBusPreallocatedHash *hash_entry;
} OwnershipRestoreData;

static void
restore_ownership (void *data)
{
  OwnershipRestoreData *d = data;
  DBusList *link;

  _dbus_assert (d->service_link != NULL);
  _dbus_assert (d->connection_link != NULL);
  
  if (d->service->owners == NULL)
    {
      _dbus_assert (d->hash_entry != NULL);
      bus_service_relink (d->service, d->hash_entry);
    }
  else
    {
      _dbus_assert (d->hash_entry == NULL);
    }
  
  /* We don't need to send messages notifying of these
   * changes, since we're reverting something that was
   * cancelled (effectively never really happened)
   */
  link = _dbus_list_get_first_link (&d->service->owners);
  while (link != NULL)
    {
      if (link->data == d->before_connection)
        break;

      link = _dbus_list_get_next_link (&d->service->owners, link);
    }
  
  _dbus_list_insert_before_link (&d->service->owners, link, d->connection_link);

  /* Note that removing then restoring this changes the order in which
   * ServiceDeleted messages are sent on destruction of the
   * connection.  This should be OK as the only guarantee there is
   * that the base service is destroyed last, and we never even
   * tentatively remove the base service.
   */
  bus_connection_add_owned_service_link (d->connection, d->service_link);
  
  d->hash_entry = NULL;
  d->service_link = NULL;
  d->connection_link = NULL;
}

static void
free_ownership_restore_data (void *data)
{
  OwnershipRestoreData *d = data;

  if (d->service_link)
    _dbus_list_free_link (d->service_link);
  if (d->connection_link)
    _dbus_list_free_link (d->connection_link);
  if (d->hash_entry)
    _dbus_hash_table_free_preallocated_entry (d->service->registry->service_hash,
                                              d->hash_entry);

  dbus_connection_unref (d->connection);
  bus_service_unref (d->service);
  
  dbus_free (d);
}

static dbus_bool_t
add_restore_ownership_to_transaction (BusTransaction *transaction,
                                      BusService     *service,
                                      DBusConnection *connection)
{
  OwnershipRestoreData *d;
  DBusList *link;

  d = dbus_new (OwnershipRestoreData, 1);
  if (d == NULL)
    return FALSE;
  
  d->service = service;
  d->connection = connection;
  d->service_link = _dbus_list_alloc_link (service);
  d->connection_link = _dbus_list_alloc_link (connection);
  d->hash_entry = _dbus_hash_table_preallocate_entry (service->registry->service_hash);
  
  bus_service_ref (d->service);
  dbus_connection_ref (d->connection);

  d->before_connection = NULL;
  link = _dbus_list_get_first_link (&service->owners);
  while (link != NULL)
    {
      if (link->data == connection)
        {
          link = _dbus_list_get_next_link (&service->owners, link);

          if (link)
            d->before_connection = link->data;

          break;
        }
      
      link = _dbus_list_get_next_link (&service->owners, link);
    }
  
  if (d->service_link == NULL ||
      d->connection_link == NULL ||
      d->hash_entry == NULL ||
      !bus_transaction_add_cancel_hook (transaction, restore_ownership, d,
                                        free_ownership_restore_data))
    {
      free_ownership_restore_data (d);
      return FALSE;
    }
  
  return TRUE;
}

/* this function is self-cancelling if you cancel the transaction */
dbus_bool_t
bus_service_remove_owner (BusService     *service,
                          DBusConnection *owner,
                          BusTransaction *transaction,
                          DBusError      *error)
{
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  /* We send out notifications before we do any work we
   * might have to undo if the notification-sending failed
   */
  
  /* Send service lost message */
  if (bus_service_get_primary_owner (service) == owner)
    {
      if (!bus_driver_send_service_lost (owner, service->name,
                                         transaction, error))
        return FALSE;
    }

  if (service->owners == NULL)
    {
      _dbus_assert_not_reached ("Tried to remove owner of a service that has no owners");
    }
  else if (_dbus_list_length_is_one (&service->owners))
    {
      if (!bus_driver_send_service_owner_changed (service->name,
 						  bus_connection_get_name (owner),
 						  NULL,
 						  transaction, error))
        return FALSE;
    }
  else
    {
      DBusList *link;
      DBusConnection *new_owner;
      link = _dbus_list_get_first_link (&service->owners);
      _dbus_assert (link != NULL);
      link = _dbus_list_get_next_link (&service->owners, link);
      _dbus_assert (link != NULL);

      new_owner = link->data;

      if (!bus_driver_send_service_owner_changed (service->name,
 						  bus_connection_get_name (owner),
 						  bus_connection_get_name (new_owner),
 						  transaction, error))
        return FALSE;

      /* This will be our new owner */
      if (!bus_driver_send_service_acquired (new_owner,
                                             service->name,
                                             transaction,
                                             error))
        return FALSE;
    }

  if (!add_restore_ownership_to_transaction (transaction, service, owner))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  bus_service_unlink_owner (service, owner);

  if (service->owners == NULL)
    bus_service_unlink (service);

  return TRUE;
}

BusService *
bus_service_ref (BusService *service)
{
  _dbus_assert (service->refcount > 0);
  
  service->refcount += 1;

  return service;
}

void
bus_service_unref (BusService *service)
{
  _dbus_assert (service->refcount > 0);
  
  service->refcount -= 1;

  if (service->refcount == 0)
    {
      _dbus_assert (service->owners == NULL);
      
      dbus_free (service->name);
      _dbus_mem_pool_dealloc (service->registry->service_pool, service);
    }
}

DBusConnection*
bus_service_get_primary_owner (BusService *service)
{
  return _dbus_list_get_first (&service->owners);
}

const char*
bus_service_get_name (BusService *service)
{
  return service->name;
}

void
bus_service_set_prohibit_replacement (BusService  *service,
				      dbus_bool_t  prohibit_replacement)
{
  service->prohibit_replacement = prohibit_replacement != FALSE;
}

dbus_bool_t
bus_service_get_prohibit_replacement (BusService *service)
{
  return service->prohibit_replacement;
}

dbus_bool_t
bus_service_has_owner (BusService     *service,
		       DBusConnection *owner)
{
  DBusList *link;

  link = _dbus_list_get_first_link (&service->owners);
  
  while (link != NULL)
    {
      if (link->data == owner)
	return TRUE;
      
      link = _dbus_list_get_next_link (&service->owners, link);
    }

  return FALSE;
}
