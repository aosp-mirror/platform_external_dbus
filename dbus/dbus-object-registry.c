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
#include <string.h>

/**
 * @defgroup DBusObjectRegistry Map object IDs to implementations
 * @ingroup  DBusInternals
 * @brief DBusObjectRegistry is used by DBusConnection to track object IDs
 *
 * Types and functions related to DBusObjectRegistry
 *
 * @{
 */

typedef struct DBusObjectEntry DBusObjectEntry;

 /* 14 bits for object index, 32K objects */
#define DBUS_OBJECT_INDEX_BITS          (14)
#define DBUS_OBJECT_INDEX_MASK          (0x7fff)
#define DBUS_MAX_OBJECTS_PER_CONNECTION DBUS_OBJECT_INDEX_MASK
struct DBusObjectEntry
{
  unsigned int id_index      : 14; /**< Index of this entry in the entries array */
  unsigned int id_times_used : 18; /**< Count of times entry has been used; avoids recycling IDs too often */

  void *object_impl;               /**< Pointer to application-supplied implementation */
  const DBusObjectVTable *vtable;  /**< Virtual table for this object */
};

struct DBusObjectRegistry
{
  int refcount;
  DBusConnection *connection;

  DBusObjectEntry *entries;
  int n_entries_allocated;
  int n_entries_used;
};

DBusObjectRegistry*
_dbus_object_registry_new (DBusConnection *connection)
{
  DBusObjectRegistry *registry;

  registry = dbus_new0 (DBusObjectRegistry, 1);

  registry->refcount = 1;
  registry->connection = connection;

  return registry;
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
      _dbus_assert (registry->n_entries_used == 0);

      dbus_free (registry->entries);
      dbus_free (registry);
    }
}

#define ENTRY_TO_ID(entry)                                              \
  (((dbus_uint32_t) (entry)->id_index) |                                \
   (((dbus_uint32_t)(entry)->id_times_used) << DBUS_OBJECT_INDEX_BITS))

#define ID_TO_INDEX(id) \
  (((dbus_uint32_t) (id)) | DBUS_OBJECT_INDEX_MASK)

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
  dbus_object_id_set_high_bits (&info->object_id,
                                _dbus_connection_get_id (registry->connection));
  dbus_object_id_set_low_bits (&info->object_id,
                               ENTRY_TO_ID (entry));
}

dbus_bool_t
_dbus_object_registry_add_and_unlock (DBusObjectRegistry      *registry,
                                      const char             **interfaces,
                                      const DBusObjectVTable  *vtable,
                                      void                    *object_impl,
                                      DBusObjectID            *object_id)
{
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
              return FALSE;
            }

          new_alloc = registry->n_entries_allocated * 2;
          if (new_alloc > DBUS_MAX_OBJECTS_PER_CONNECTION)
            new_alloc = DBUS_MAX_OBJECTS_PER_CONNECTION;
        }

      new_entries = dbus_realloc (registry->entries,
                                  new_alloc * sizeof (DBusObjectEntry));

      if (new_entries == NULL)
        return FALSE;

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
      i = registry->n_entries_used;
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
      i = 0;
      while (i < registry->n_entries_used)
        {
          if (registry->entries[i].vtable == NULL)
            break;
          ++i;
        }

      _dbus_assert (i < registry->n_entries_used);
    }

  registry->entries[i].id_index = i;
  /* Overflow is OK here */
  registry->entries[i].id_times_used += 1;

  registry->entries[i].vtable = vtable;
  registry->entries[i].object_impl = object_impl;

  info_from_entry (registry, &info, &registry->entries[i]);

  /* Drop lock and invoke application code */
  _dbus_connection_unlock (registry->connection);

  (* vtable->registered) (&info);

  return TRUE;
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
      return;
    }

  info_from_entry (registry, &info, entry);
  vtable = entry->vtable;
  entry->vtable = NULL;
  entry->object_impl = NULL;
  registry->n_entries_used -= 1;

  /* Drop lock and invoke application code */
  _dbus_connection_unlock (registry->connection);

  (* vtable->unregistered) (&info);
}

void
_dbus_object_registry_handle_and_unlock (DBusObjectRegistry *registry,
                                         DBusMessage        *message)
{
  /* FIXME */
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

/**
 * @ingroup DBusObjectRegistry
 * Unit test for DBusObjectRegistry
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_object_registry_test (void)
{


  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
