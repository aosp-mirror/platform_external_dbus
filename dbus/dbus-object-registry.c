/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-object-registry.c  DBusObjectRegistry (internals of DBusConnection)
 *
 * Copyright (C) 2003  Red Hat Inc.
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
#include "dbus-object-registry.h"
#include "dbus-connection-internal.h"
#include "dbus-internals.h"
#include "dbus-hash.h"
#include <string.h>

/**
 * @defgroup DBusObjectRegistry Map object IDs to implementations
 * @ingroup  DBusInternals
 * @brief DBusObjectRegistry is used by DBusConnection to track object IDs
 *
 * Types and functions related to DBusObjectRegistry. These
 * are all internal.
 *
 * @{
 */

typedef struct DBusObjectEntry DBusObjectEntry;
typedef struct DBusInterfaceEntry DBusInterfaceEntry;

#define DBUS_MAX_OBJECTS_PER_INTERFACE 65535
struct DBusInterfaceEntry
{
  unsigned int n_objects : 16;   /**< Number of objects with this interface */
  unsigned int n_allocated : 16; /**< Allocated size of objects array */
  dbus_uint16_t *objects;        /**< Index of each object with the interface */
  char name[4];                  /**< Name of interface (actually allocated larger) */
};

 /* 14 bits for object index, 32K objects */
#define DBUS_OBJECT_INDEX_BITS          (14)
#define DBUS_OBJECT_INDEX_MASK          (0x3fff)
#define DBUS_MAX_OBJECTS_PER_CONNECTION DBUS_OBJECT_INDEX_MASK
struct DBusObjectEntry
{
  unsigned int id_index      : 14; /**< Index of this entry in the entries array */
  unsigned int id_times_used : 18; /**< Count of times entry has been used; avoids recycling IDs too often */

  void *object_impl;               /**< Pointer to application-supplied implementation */
  const DBusObjectVTable *vtable;  /**< Virtual table for this object */
  DBusInterfaceEntry **interfaces; /**< NULL-terminated list of interfaces */
};

struct DBusObjectRegistry
{
  int refcount;
  DBusConnection *connection;

  DBusObjectEntry *entries;
  int n_entries_allocated;
  int n_entries_used;

  DBusHashTable *interface_table;
};

static void
free_interface_entry (void *entry)
{
  DBusInterfaceEntry *iface = entry;

  if (iface == NULL) /* DBusHashTable stupidity */
    return;
  
  dbus_free (iface->objects);
  dbus_free (iface);
}

DBusObjectRegistry*
_dbus_object_registry_new (DBusConnection *connection)
{
  DBusObjectRegistry *registry;
  DBusHashTable *interface_table;
  
  /* the connection passed in here isn't fully constructed,
   * so don't do anything more than store a pointer to
   * it
   */

  registry = NULL;
  interface_table = NULL;
  
  registry = dbus_new0 (DBusObjectRegistry, 1);
  if (registry == NULL)
    goto oom;

  interface_table = _dbus_hash_table_new (DBUS_HASH_STRING,
                                          NULL, free_interface_entry);
  if (interface_table == NULL)
    goto oom;
  
  registry->refcount = 1;
  registry->connection = connection;
  registry->interface_table = interface_table;
  
  return registry;

 oom:
  if (registry)
    dbus_free (registry);
  if (interface_table)
    _dbus_hash_table_unref (interface_table);

  return NULL;
}

void
_dbus_object_registry_ref (DBusObjectRegistry *registry)
{
  _dbus_assert (registry->refcount > 0);

  registry->refcount += 1;
}

void
_dbus_object_registry_unref (DBusObjectRegistry *registry)
{
  _dbus_assert (registry->refcount > 0);

  registry->refcount -= 1;

  if (registry->refcount == 0)
    {
      int i;
      
      _dbus_assert (registry->n_entries_used == 0);
      _dbus_assert (_dbus_hash_table_get_n_entries (registry->interface_table) == 0);

      i = 0;
      while (i < registry->n_entries_allocated)
        {
          if (registry->entries[i].interfaces)
            dbus_free (registry->entries[i].interfaces);
          ++i;
        }
      
      _dbus_hash_table_unref (registry->interface_table);
      dbus_free (registry->entries);
      dbus_free (registry);
    }
}

#define ENTRY_TO_ID(entry)                                              \
  (((dbus_uint32_t) (entry)->id_index) |                                \
   (((dbus_uint32_t)(entry)->id_times_used) << DBUS_OBJECT_INDEX_BITS))

#define ID_TO_INDEX(id) \
  (((dbus_uint32_t) (id)) & DBUS_OBJECT_INDEX_MASK)

#define ID_TO_TIMES_USED(id) \
  (((dbus_uint32_t) (id)) >> DBUS_OBJECT_INDEX_BITS)

static DBusObjectEntry*
validate_id (DBusObjectRegistry *registry,
             const DBusObjectID *object_id)
{
  int idx;
  int times_used;
  dbus_uint32_t low_bits;

  low_bits = dbus_object_id_get_low_bits (object_id);

  idx = ID_TO_INDEX (low_bits);
  times_used = ID_TO_TIMES_USED (low_bits);
  
  if (idx >= registry->n_entries_allocated)
    return NULL;
  if (registry->entries[idx].vtable == NULL)
    return NULL;
  if (registry->entries[idx].id_times_used != times_used)
    return NULL;
  _dbus_assert (registry->entries[idx].id_index == idx);
  _dbus_assert (registry->n_entries_used > 0);

  return &registry->entries[idx];
}

static void
info_from_entry (DBusObjectRegistry *registry,
                 DBusObjectInfo     *info,
                 DBusObjectEntry    *entry)
{
  info->connection = registry->connection;
  info->object_impl = entry->object_impl;
#ifdef DBUS_BUILD_TESTS
  if (registry->connection)
#endif
    dbus_object_id_set_high_bits (&info->object_id,
                                  _dbus_connection_get_id (registry->connection));
#ifdef DBUS_BUILD_TESTS
  else
    dbus_object_id_set_high_bits (&info->object_id, 1);
#endif
  
  dbus_object_id_set_low_bits (&info->object_id,
                               ENTRY_TO_ID (entry));
}

static DBusInterfaceEntry*
lookup_interface (DBusObjectRegistry *registry,
                  const char         *name,
                  dbus_bool_t         create_if_not_found)
{
  DBusInterfaceEntry *entry;
  int sz;
  int len;
  
  entry = _dbus_hash_table_lookup_string (registry->interface_table,
                                          name);
  if (entry != NULL || !create_if_not_found)
    return entry;
  
  _dbus_assert (create_if_not_found);

  len = strlen (name);
  sz = _DBUS_STRUCT_OFFSET (DBusInterfaceEntry, name) + len + 1;
  entry = dbus_malloc (sz);
  if (entry == NULL)
    return NULL;
  entry->n_objects = 0;
  entry->n_allocated = 0;
  entry->objects = NULL;
  memcpy (entry->name, name, len + 1);

  if (!_dbus_hash_table_insert_string (registry->interface_table,
                                       entry->name, entry))
    {
      dbus_free (entry);
      return NULL;
    }
  
  return entry;
}

static void
delete_interface (DBusObjectRegistry *registry,
                  DBusInterfaceEntry *entry)
{
  _dbus_hash_table_remove_string (registry->interface_table,
                                  entry->name);
}

static dbus_bool_t
interface_entry_add_object (DBusInterfaceEntry *entry,
                            dbus_uint16_t       object_index)
{
  if (entry->n_objects == entry->n_allocated)
    {
      unsigned int new_alloc;
      dbus_uint16_t *new_objects;
      
      if (entry->n_allocated == 0)
        new_alloc = 2;
      else
        new_alloc = entry->n_allocated * 2;

      /* Right now MAX_OBJECTS_PER_INTERFACE can't possibly be reached
       * since the max number of objects _total_ is smaller, but the
       * code is here for future robustness.
       */
      
      if (new_alloc > DBUS_MAX_OBJECTS_PER_INTERFACE)
        new_alloc = DBUS_MAX_OBJECTS_PER_INTERFACE;
      if (new_alloc == entry->n_allocated)
        {
          _dbus_warn ("Attempting to register another instance with interface %s, but max count %d reached\n",
                      entry->name, DBUS_MAX_OBJECTS_PER_INTERFACE);
          return FALSE;
        }

      new_objects = dbus_realloc (entry->objects, new_alloc * sizeof (dbus_uint16_t));
      if (new_objects == NULL)
        return FALSE;
      entry->objects = new_objects;
      entry->n_allocated = new_alloc;
    }

  _dbus_assert (entry->n_objects < entry->n_allocated);

  entry->objects[entry->n_objects] = object_index;
  entry->n_objects += 1;

  return TRUE;
}

static void
interface_entry_remove_object (DBusInterfaceEntry *entry,
                               dbus_uint16_t       object_index)
{
  unsigned int i;

  i = 0;
  while (i < entry->n_objects)
    {
      if (entry->objects[i] == object_index)
        break;
      ++i;
    }

  if (i == entry->n_objects)
    {
      _dbus_assert_not_reached ("Tried to remove object from an interface that didn't list that object\n");
      return;
    }

  memmove (&entry->objects[i],
           &entry->objects[i+1],
           (entry->n_objects - i - 1) * sizeof (entry->objects[0]));
  entry->n_objects -= 1;  
}

static void
object_remove_from_interfaces (DBusObjectRegistry *registry,
                               DBusObjectEntry    *entry)
{
  if (entry->interfaces != NULL)
    {
      int i;
      
      i = 0;
      while (entry->interfaces[i] != NULL)
        {
          DBusInterfaceEntry *iface = entry->interfaces[i];
          
          interface_entry_remove_object (iface, entry->id_index);
          if (iface->n_objects == 0)
            delete_interface (registry, iface);
          ++i;
        }
    }
}

dbus_bool_t
_dbus_object_registry_add_and_unlock (DBusObjectRegistry      *registry,
                                      const char             **interfaces,
                                      const DBusObjectVTable  *vtable,
                                      void                    *object_impl,
                                      DBusObjectID            *object_id)
{
  int idx;
  int i;
  DBusObjectInfo info;
  
  if (registry->n_entries_used == registry->n_entries_allocated)
    {
      DBusObjectEntry *new_entries;
      int new_alloc;

      if (registry->n_entries_allocated == 0)
        new_alloc = 16;
      else
        {
          if (registry->n_entries_allocated == DBUS_MAX_OBJECTS_PER_CONNECTION)
            {
              _dbus_warn ("Attempting to register a new D-BUS object, but maximum object count of %d reached\n",
                          DBUS_MAX_OBJECTS_PER_CONNECTION);
              goto out_0;
            }

          new_alloc = registry->n_entries_allocated * 2;
          if (new_alloc > DBUS_MAX_OBJECTS_PER_CONNECTION)
            new_alloc = DBUS_MAX_OBJECTS_PER_CONNECTION;
        }

      new_entries = dbus_realloc (registry->entries,
                                  new_alloc * sizeof (DBusObjectEntry));

      if (new_entries == NULL)
        goto out_0;

      memset (&new_entries[registry->n_entries_allocated],
              '\0',
              sizeof (DBusObjectEntry) * (new_alloc - registry->n_entries_allocated));

      registry->entries = new_entries;
      registry->n_entries_allocated = new_alloc;
    }
  _dbus_assert (registry->n_entries_used < registry->n_entries_allocated);

  /* We linear search for an available entry. However, short-circuit
   * the hopefully-common situation where we don't have a sparse
   * array.
   */
  if (registry->entries[registry->n_entries_used].vtable == NULL)
    {
      idx = registry->n_entries_used;
    }
  else
    {
      /* If we do have a sparse array, we try to get rid of it rather
       * than using empty slots on the end, so we won't hit this case
       * next time.
       */

      /* If index n_entries_used is occupied, then
       * there is at least one entry outside of
       * the range [0, n_entries_used). Thus, there is
       * at least one blank entry inside that range.
       */
      idx = 0;
      while (idx < registry->n_entries_used)
        {
          if (registry->entries[idx].vtable == NULL)
            break;
          ++idx;
        }

      _dbus_assert (idx < registry->n_entries_used);
    }
  
  registry->entries[idx].id_index = idx;
  /* Overflow is OK here, but zero isn't as it's a null ID */
  registry->entries[idx].id_times_used += 1;
  if (registry->entries[idx].id_times_used == 0)
    registry->entries[idx].id_times_used += 1;
    
  registry->entries[idx].vtable = vtable;
  registry->entries[idx].object_impl = object_impl;

  registry->n_entries_used += 1;

  i = 0;
  if (interfaces != NULL)
    {
      while (interfaces[i] != NULL)
        ++i;
    }

  if (i > 0)
    {
      DBusInterfaceEntry **new_interfaces;
      
      new_interfaces = 
        dbus_realloc (registry->entries[idx].interfaces,
                      (i + 1) * sizeof (DBusInterfaceEntry*));
      
      if (new_interfaces == NULL)
        {
          /* maintain invariant that .interfaces array points to something
           * valid in oom handler (entering this function it pointed to
           * stale data but a valid malloc block)
           */
          dbus_free (registry->entries[idx].interfaces);
          registry->entries[idx].interfaces = NULL;
          goto out_1;
        }

      /* NULL-init so it's NULL-terminated and the OOM
       * case can see how far we got
       */
      while (i >= 0)
        {
          new_interfaces[i] = NULL;
          --i;
        }
      
      registry->entries[idx].interfaces = new_interfaces;
    }
  else
    {
      dbus_free (registry->entries[idx].interfaces);
      registry->entries[idx].interfaces = NULL;
    }

  /* Fill in interfaces */
  if (interfaces != NULL)
    {
      i = 0;
      while (interfaces[i] != NULL)
        {
          DBusInterfaceEntry *iface;
          
          iface = lookup_interface (registry, interfaces[i],
                                    TRUE);
          if (iface == NULL)
            goto out_1;
          
          if (!interface_entry_add_object (iface, idx))
            {
              if (iface->n_objects == 0)
                delete_interface (registry, iface);
              goto out_1;
            }
          
          registry->entries[idx].interfaces[i] = iface;
          
          ++i;
        }
    }
  
  info_from_entry (registry, &info, &registry->entries[idx]);
  if (object_id)
    *object_id = info.object_id;
  
  /* Drop lock and invoke application code */
#ifdef DBUS_BUILD_TESTS
  if (registry->connection)
#endif
    _dbus_connection_unlock (registry->connection);
  
  (* vtable->registered) (&info);

  return TRUE;
  
 out_1:    
  registry->entries[idx].vtable = NULL;
  registry->entries[idx].object_impl = NULL;
  registry->n_entries_used -= 1;

  object_remove_from_interfaces (registry,
                                 &registry->entries[idx]);
  
 out_0:
#ifdef DBUS_BUILD_TESTS
  if (registry->connection)
#endif
    _dbus_connection_unlock (registry->connection);
  return FALSE;
}

void
_dbus_object_registry_remove_and_unlock (DBusObjectRegistry *registry,
                                         const DBusObjectID *object_id)
{
  DBusObjectInfo info;
  DBusObjectEntry *entry;
  const DBusObjectVTable *vtable;

  entry = validate_id (registry, object_id);
  if (entry == NULL)
    {
      _dbus_warn ("Tried to unregister a nonexistent D-BUS object ID\n");
#ifdef DBUS_BUILD_TESTS
      if (registry->connection)
#endif
        _dbus_connection_unlock (registry->connection);
      
      return;
    }

  object_remove_from_interfaces (registry, entry);
  
  info_from_entry (registry, &info, entry);
  vtable = entry->vtable;
  entry->vtable = NULL;
  entry->object_impl = NULL;
  registry->n_entries_used -= 1;
  
  /* Drop lock and invoke application code */
#ifdef DBUS_BUILD_TESTS
  if (registry->connection)
#endif
    _dbus_connection_unlock (registry->connection);

  (* vtable->unregistered) (&info);
}

DBusHandlerResult
_dbus_object_registry_handle_and_unlock (DBusObjectRegistry *registry,
                                         DBusMessage        *message)
{
  /* FIXME */
  return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

void
_dbus_object_registry_free_all_unlocked (DBusObjectRegistry *registry)
{
  int i;
  
  i = 0;
  while (registry->n_entries_used > 0)
    {
      _dbus_assert (i < registry->n_entries_allocated);
      if (registry->entries[i].vtable != NULL)
        {
          DBusObjectInfo info;
          const DBusObjectVTable *vtable;

          object_remove_from_interfaces (registry,
                                         &registry->entries[i]);
          
          info_from_entry (registry, &info, &registry->entries[i]);
          vtable = registry->entries[i].vtable;
          registry->entries[i].vtable = NULL;
          registry->entries[i].object_impl = NULL;
          registry->n_entries_used -= 1;
          _dbus_assert (registry->n_entries_used >= 0);

          (* vtable->unregistered) (&info);
        }

      ++i;
    }

  _dbus_assert (registry->n_entries_used == 0);
}

/** @} */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>

static void
noop_message_function (DBusObjectInfo *info,
                       DBusMessage    *message)
{
  /* nothing */
}

static void
add_and_remove_objects (DBusObjectRegistry *registry)
{
#define N_OBJECTS 73
  DBusObjectID ids[N_OBJECTS];
  const char *zero_interfaces[] = { NULL };
  const char *one_interface[] = { "org.freedesktop.Test.Blah", NULL };
  const char *three_interfaces[] = { "org.freedesktop.Test.Blah",
                                     "org.freedesktop.Test.Baz",
                                     "org.freedesktop.Test.Foo",
                                     NULL };
  int i;
  
  i = 0;
  while (i < N_OBJECTS)
    {
      DBusCallbackObject *callback;
      const char **interfaces;
      
      callback = dbus_callback_object_new (noop_message_function, NULL, NULL);
      if (callback == NULL)
        goto out;
      
      switch (i % 3)
        {
        case 0:
          interfaces = zero_interfaces;
          break;
        case 1:
          interfaces = one_interface;
          break;
        case 2:
          interfaces = three_interfaces;
          break;
        }
      
      if (!_dbus_object_registry_add_and_unlock (registry,
                                                 interfaces,
                                                 dbus_callback_object_vtable,
                                                 callback,
                                                 &ids[i]))
        {
          dbus_callback_object_unref (callback);
          goto out;
        }

      dbus_callback_object_unref (callback);
      
      ++i;
    }
                                     
  i = 0;
  while (i < N_OBJECTS)
    {
      if (i > (N_OBJECTS - 20) || (i % 3) == 0)
        {
          _dbus_object_registry_remove_and_unlock (registry,
                                                   &ids[i]);
          dbus_object_id_set_null (&ids[i]);
        }
      
      ++i;
    }
                                     
  i = 0;
  while (i < N_OBJECTS)
    {
      if (dbus_object_id_is_null (&ids[i]))
        {
          DBusCallbackObject *callback;
          const char **interfaces;
      
          callback = dbus_callback_object_new (noop_message_function, NULL, NULL);
          if (callback == NULL)
            goto out;
          
          switch (i % 4)
            {
            case 0:
              interfaces = NULL;
              break;
            case 1:
              interfaces = zero_interfaces;
              break;
            case 2:
              interfaces = one_interface;
              break;
            case 3:
              interfaces = three_interfaces;
              break;
            }
      
          if (!_dbus_object_registry_add_and_unlock (registry,
                                                     interfaces,
                                                     dbus_callback_object_vtable,
                                                     callback,
                                                     &ids[i]))
            {
              dbus_callback_object_unref (callback);
              goto out;
            }
          
          dbus_callback_object_unref (callback);
        }
      
      ++i;
    }

  i = 0;
  while (i < (N_OBJECTS - 30))
    {
      _dbus_assert (!dbus_object_id_is_null (&ids[i]));
      
      _dbus_object_registry_remove_and_unlock (registry,
                                               &ids[i]);
      ++i;
    }

 out:
  /* unregister the rest this way, to test this function */
  _dbus_object_registry_free_all_unlocked (registry);
}

static dbus_bool_t
object_registry_test_iteration (void *data)
{
  DBusObjectRegistry *registry;
  
  registry = _dbus_object_registry_new (NULL);
  if (registry == NULL)
    return TRUE;

  /* we do this twice since realloc behavior will differ each time,
   * and the IDs will get recycled leading to slightly different
   * codepaths
   */
  add_and_remove_objects (registry);
  add_and_remove_objects (registry);
  
  _dbus_object_registry_unref (registry);

  return TRUE;
}

/**
 * @ingroup DBusObjectRegistry
 * Unit test for DBusObjectRegistry
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_object_registry_test (void)
{
  _dbus_test_oom_handling ("object registry",
                           object_registry_test_iteration,
                           NULL);
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
