/* -*- mode: C; c-file-style: "gnu" -*- */
/* services.c  Service management
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
#include "services.h"
#include "connection.h"
#include <dbus/dbus-hash.h>
#include <dbus/dbus-list.h>
#include <dbus/dbus-mempool.h>

struct BusService
{
  char *name;
  DBusList *owners;
};

static DBusHashTable *service_hash = NULL;
static DBusMemPool   *service_pool = NULL;

BusService*
bus_service_lookup (const DBusString *service_name,
                    dbus_bool_t       create_if_not_found)
{
  const char *c_name;
  BusService *service;
  
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
          return NULL;
        }
    }
  
  _dbus_string_get_const_data (service_name, &c_name);

  service = _dbus_hash_table_lookup_string (service_hash,
                                            c_name);
  if (service != NULL)
    return service;

  if (!create_if_not_found)
    return NULL;
  
  service = _dbus_mem_pool_alloc (service_pool);
  if (service == NULL)
    return NULL;

  service->name = _dbus_strdup (c_name);
  if (service->name == NULL)
    {
      _dbus_mem_pool_dealloc (service_pool, service);
      return NULL;
    }

  if (!_dbus_hash_table_insert_string (service_hash,
                                       service->name,
                                       service))
    {
      dbus_free (service->name);
      _dbus_mem_pool_dealloc (service_pool, service);
      return NULL;
    }

  return service;
}

dbus_bool_t
bus_service_add_owner (BusService     *service,
                       DBusConnection *owner)
{
  if (!_dbus_list_append (&service->owners,
                          owner))
    return FALSE;

  if (!bus_connection_add_owned_service (owner, service))
    {
      _dbus_list_remove_last (&service->owners, owner);
      return FALSE;
    }

  return TRUE;
}

void
bus_service_remove_owner (BusService     *service,
                          DBusConnection *owner)
{
  _dbus_list_remove_last (&service->owners, owner);
  bus_connection_remove_owned_service (owner, service);
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
