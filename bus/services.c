/* -*- mode: C; c-file-style: "gnu" -*- */
/* services.c  Service management
 *
 * Copyright (C) 2003  Red Hat, Inc.
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
#include <dbus/dbus-hash.h>
#include <dbus/dbus-list.h>
#include <dbus/dbus-mempool.h>

#include "driver.h"
#include "services.h"
#include "connection.h"
#include "utils.h"

struct BusService
{
  char *name;
  DBusList *owners;
  
  unsigned int prohibit_replacement : 1;
};

static DBusHashTable *service_hash = NULL;
static DBusMemPool   *service_pool = NULL;

static dbus_bool_t
init_hash (void)
{
  if (service_hash == NULL)
    {
      service_hash = _dbus_hash_table_new (DBUS_HASH_STRING,
                                           NULL, NULL);
      service_pool = _dbus_mem_pool_new (sizeof (BusService),
                                         TRUE);

      if (service_hash == NULL || service_pool == NULL)
        {
          if (service_hash)
            {
              _dbus_hash_table_unref (service_hash);
              service_hash = NULL;
            }
          if (service_pool)
            {
              _dbus_mem_pool_free (service_pool);
              service_pool = NULL;
            }
          return FALSE;
        }
    }
  return TRUE;
}

BusService*
bus_service_lookup (const DBusString *service_name)
{
  const char *c_name;
  BusService *service;
  
  if (!init_hash ())
    return NULL;
  
  _dbus_string_get_const_data (service_name, &c_name);

  service = _dbus_hash_table_lookup_string (service_hash,
                                            c_name);

  return service;
}

BusService*
bus_service_ensure (const DBusString          *service_name,
                    DBusConnection            *owner_if_created,
                    BusTransaction            *transaction,
                    DBusError                 *error)
{
  const char *c_name;
  BusService *service;

  _dbus_assert (owner_if_created != NULL);
  _dbus_assert (transaction != NULL);
  
  if (!init_hash ())
    return NULL;
  
  _dbus_string_get_const_data (service_name, &c_name);

  service = _dbus_hash_table_lookup_string (service_hash,
                                            c_name);
  if (service != NULL)
    return service;
  
  service = _dbus_mem_pool_alloc (service_pool);
  if (service == NULL)
    {
      BUS_SET_OOM (error);
      return NULL;
    }

  service->name = _dbus_strdup (c_name);
  if (service->name == NULL)
    {
      _dbus_mem_pool_dealloc (service_pool, service);
      BUS_SET_OOM (error);
      return NULL;
    }

  if (!bus_driver_send_service_created (service->name, transaction, error))
    {
      dbus_free (service->name);
      _dbus_mem_pool_dealloc (service_pool, service);
      return NULL;
    }

  if (!bus_service_add_owner (service, owner_if_created,
                              transaction, error))
    {
      dbus_free (service->name);
      _dbus_mem_pool_dealloc (service_pool, service);
      return NULL;
    }
  
  if (!_dbus_hash_table_insert_string (service_hash,
                                       service->name,
                                       service))
    {
      _dbus_list_clear (&service->owners);
      dbus_free (service->name);
      _dbus_mem_pool_dealloc (service_pool, service);
      BUS_SET_OOM (error);
      return NULL;
    }
  
  return service;
}

dbus_bool_t
bus_service_add_owner (BusService     *service,
                       DBusConnection *owner,
                       BusTransaction *transaction,
                       DBusError      *error)
{
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
  
  return TRUE;
}

dbus_bool_t
bus_service_remove_owner (BusService     *service,
                          DBusConnection *owner,
                          BusTransaction *transaction,
                          DBusError      *error)
{
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

  if (_dbus_list_length_is_one (&service->owners))
    {
      /* We are the only owner - send service deleted */
      if (!bus_driver_send_service_deleted (service->name,
                                            transaction, error))
        return FALSE;
    }
  else
    {
      DBusList *link;
      link = _dbus_list_get_first (&service->owners);
      link = _dbus_list_get_next_link (&service->owners, link);

      if (link != NULL)
        {
          /* This will be our new owner */
          if (!bus_driver_send_service_acquired (link->data,
                                                 service->name,
                                                 transaction,
                                                 error))
            return FALSE;
        }
    }
  
  _dbus_list_remove_last (&service->owners, owner);
  bus_connection_remove_owned_service (owner, service);

  if (service->owners == NULL)
    {
      /* Delete service (already sent message that it was deleted above) */
      _dbus_hash_table_remove_string (service_hash, service->name);
      
      dbus_free (service->name);
      _dbus_mem_pool_dealloc (service_pool, service);
    }

  return TRUE;
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
bus_service_foreach (BusServiceForeachFunction  function,
                     void                      *data)
{
  DBusHashIter iter;
  
  if (service_hash == NULL)
    return;
  
  _dbus_hash_iter_init (service_hash, &iter);
  while (_dbus_hash_iter_next (&iter))
    {
      BusService *service = _dbus_hash_iter_get_value (&iter);

      (* function) (service, data);
    }
}

char **
bus_services_list (int *array_len)
{
  int i, j, len;
  char **retval;
  DBusHashIter iter;
   
  len = _dbus_hash_table_get_n_entries (service_hash);
  retval = dbus_new (char *, len + 1);

  if (retval == NULL)
    return NULL;

  _dbus_hash_iter_init (service_hash, &iter);
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
  
  return retval;
  
 error:
  for (j = 0; j < i; j++)
    dbus_free (retval[i]);
  dbus_free (retval);

  return NULL;
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
