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
#include "dbus-protocol.h"
#include <string.h>

/**
 * @defgroup DBusObjectRegistry Map object IDs to implementations
 * @ingroup  DBusInternals
 * @brief DBusObjectRegistry is used by DBusConnection to track object IDs
 *
 * Types and functions related to DBusObjectRegistry. These
 * are all internal.
 *
 * @todo interface entries and signal connections are handled pretty
 * much identically, with lots of duplicate code.  Once we're sure
 * they will always be the same, we could merge this code.
 *
 * @{
 */

typedef struct DBusObjectEntry DBusObjectEntry;
typedef struct DBusInterfaceEntry DBusInterfaceEntry;
typedef struct DBusSignalEntry DBusSignalEntry;

#define DBUS_MAX_OBJECTS_PER_INTERFACE 65535
struct DBusInterfaceEntry
{
  unsigned int n_objects : 16;   /**< Number of objects with this interface */
  unsigned int n_allocated : 16; /**< Allocated size of objects array */
  dbus_uint16_t *objects;        /**< Index of each object with the interface */
  char name[4];                  /**< Name of interface (actually allocated larger) */
};

#define DBUS_MAX_CONNECTIONS_PER_SIGNAL 65535
struct DBusSignalEntry
{
  unsigned int n_connections : 16; /**< Number of connections to this signal */
  unsigned int n_allocated : 16;   /**< Allocated size of objects array */
  dbus_uint16_t *connections;      /**< Index of each object connected (can have dups for multiple
                                    * connections)
                                    */
  char name[4];                    /**< Name of signal (actually allocated larger) */
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
  DBusSignalEntry    **signals;    /**< Signal connections (contains dups, one each time we connect) */
};

struct DBusObjectRegistry
{
  int refcount;
  DBusConnection *connection;

  DBusObjectEntry *entries;
  int n_entries_allocated;
  int n_entries_used;

  DBusHashTable *interface_table;

  DBusHashTable *signal_table;
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

static void
free_signal_entry (void *entry)
{
  DBusSignalEntry *signal = entry;

  if (signal == NULL) /* DBusHashTable stupidity */
    return;
  
  dbus_free (signal->connections);
  dbus_free (signal);
}

DBusObjectRegistry*
_dbus_object_registry_new (DBusConnection *connection)
{
  DBusObjectRegistry *registry;
  DBusHashTable *interface_table;
  DBusHashTable *signal_table;
  
  /* the connection passed in here isn't fully constructed,
   * so don't do anything more than store a pointer to
   * it
   */

  registry = NULL;
  interface_table = NULL;
  signal_table = NULL;
  
  registry = dbus_new0 (DBusObjectRegistry, 1);
  if (registry == NULL)
    goto oom;

  interface_table = _dbus_hash_table_new (DBUS_HASH_STRING,
                                          NULL, free_interface_entry);
  if (interface_table == NULL)
    goto oom;

  signal_table = _dbus_hash_table_new (DBUS_HASH_STRING,
                                          NULL, free_signal_entry);
  if (signal_table == NULL)
    goto oom;
  
  registry->refcount = 1;
  registry->connection = connection;
  registry->interface_table = interface_table;
  registry->signal_table = signal_table;
  
  return registry;

 oom:
  if (registry)
    dbus_free (registry);
  if (interface_table)
    _dbus_hash_table_unref (interface_table);
  if (signal_table)
    _dbus_hash_table_unref (signal_table);
  
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
      _dbus_assert (_dbus_hash_table_get_n_entries (registry->signal_table) == 0);

      i = 0;
      while (i < registry->n_entries_allocated)
        {
          if (registry->entries[i].interfaces)
            dbus_free (registry->entries[i].interfaces);
          if (registry->entries[i].signals)
            dbus_free (registry->entries[i].signals);
          ++i;
        }
      
      _dbus_hash_table_unref (registry->interface_table);
      _dbus_hash_table_unref (registry->signal_table);
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
  dbus_uint32_t instance_bits;
  
  instance_bits = dbus_object_id_get_instance_bits (object_id);

  /* Verify that connection ID bits are the same */
#ifdef DBUS_BUILD_TESTS
  if (registry->connection)
#endif
    {
      DBusObjectID tmp_id;
      
      _dbus_connection_init_id (registry->connection,
                                &tmp_id);
      dbus_object_id_set_instance_bits (&tmp_id, instance_bits);
      
      if (!dbus_object_id_equal (&tmp_id, object_id))
        return NULL;
    }
  
  idx = ID_TO_INDEX (instance_bits);
  times_used = ID_TO_TIMES_USED (instance_bits);
  
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
id_from_entry (DBusObjectRegistry *registry,
               DBusObjectID       *object_id,
               DBusObjectEntry    *entry)
{
#ifdef DBUS_BUILD_TESTS
  if (registry->connection)
#endif
    _dbus_connection_init_id (registry->connection,
                              object_id);
#ifdef DBUS_BUILD_TESTS
  else
    {
      dbus_object_id_set_server_bits (object_id, 1);
      dbus_object_id_set_client_bits (object_id, 2);
    }
#endif

  _dbus_assert (dbus_object_id_get_server_bits (object_id) != 0);
  _dbus_assert (dbus_object_id_get_client_bits (object_id) != 0);
  
  dbus_object_id_set_instance_bits (object_id,
                                    ENTRY_TO_ID (entry));

  _dbus_assert (dbus_object_id_get_instance_bits (object_id) != 0);
}

static void
info_from_entry (DBusObjectRegistry *registry,
                 DBusObjectInfo     *info,
                 DBusObjectEntry    *entry)
{
  info->connection = registry->connection;
  info->object_impl = entry->object_impl;

  id_from_entry (registry, &info->object_id, entry);
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

static DBusSignalEntry*
lookup_signal (DBusObjectRegistry *registry,
               const char         *name,
               dbus_bool_t         create_if_not_found)
{
  DBusSignalEntry *entry;
  int sz;
  int len;
  
  entry = _dbus_hash_table_lookup_string (registry->signal_table,
                                          name);
  if (entry != NULL || !create_if_not_found)
    return entry;
  
  _dbus_assert (create_if_not_found);

  len = strlen (name);
  sz = _DBUS_STRUCT_OFFSET (DBusSignalEntry, name) + len + 1;
  entry = dbus_malloc (sz);
  if (entry == NULL)
    return NULL;
  entry->n_connections = 0;
  entry->n_allocated = 0;
  entry->connections = NULL;
  memcpy (entry->name, name, len + 1);

  if (!_dbus_hash_table_insert_string (registry->signal_table,
                                       entry->name, entry))
    {
      dbus_free (entry);
      return NULL;
    }
  
  return entry;
}

static void
delete_signal (DBusObjectRegistry *registry,
               DBusSignalEntry *entry)
{
  _dbus_hash_table_remove_string (registry->signal_table,
                                  entry->name);
}

static dbus_bool_t
signal_entry_add_object (DBusSignalEntry *entry,
                         dbus_uint16_t    object_index)
{
  if (entry->n_connections == entry->n_allocated)
    {
      unsigned int new_alloc;
      dbus_uint16_t *new_objects;
      
      if (entry->n_allocated == 0)
        new_alloc = 2;
      else
        new_alloc = entry->n_allocated * 2;

      /* Right now MAX_CONNECTIONS_PER_SIGNAL can't possibly be reached
       * since the max number of objects _total_ is smaller, but the
       * code is here for future robustness.
       */
      
      if (new_alloc > DBUS_MAX_CONNECTIONS_PER_SIGNAL)
        new_alloc = DBUS_MAX_CONNECTIONS_PER_SIGNAL;
      if (new_alloc == entry->n_allocated)
        {
          _dbus_warn ("Attempting to register another instance with signal %s, but max count %d reached\n",
                      entry->name, DBUS_MAX_CONNECTIONS_PER_SIGNAL);
          return FALSE;
        }

      new_objects = dbus_realloc (entry->connections, new_alloc * sizeof (dbus_uint16_t));
      if (new_objects == NULL)
        return FALSE;
      entry->connections = new_objects;
      entry->n_allocated = new_alloc;
    }

  _dbus_assert (entry->n_connections < entry->n_allocated);

  entry->connections[entry->n_connections] = object_index;
  entry->n_connections += 1;

  return TRUE;
}

static void
signal_entry_remove_object (DBusSignalEntry *entry,
                            dbus_uint16_t    object_index)
{
  unsigned int i;

  i = 0;
  while (i < entry->n_connections)
    {
      if (entry->connections[i] == object_index)
        break;
      ++i;
    }

  if (i == entry->n_connections)
    {
      _dbus_assert_not_reached ("Tried to remove object from an signal that didn't list that object\n");
      return;
    }

  memmove (&entry->connections[i],
           &entry->connections[i+1],
           (entry->n_connections - i - 1) * sizeof (entry->connections[0]));
  entry->n_connections -= 1;  
}

static void
object_remove_from_signals (DBusObjectRegistry *registry,
                            DBusObjectEntry    *entry)
{
  if (entry->signals != NULL)
    {
      int i;
      
      i = 0;
      while (entry->signals[i] != NULL)
        {
          DBusSignalEntry *iface = entry->signals[i];
          
          signal_entry_remove_object (iface, entry->id_index);
          if (iface->n_connections == 0)
            delete_signal (registry, iface);
          ++i;
        }
    }
}

/**
 * Connect this object to the given signal, such that if a
 * signal emission message is received with the given
 * signal name, the message will be routed to the
 * given object.
 *
 * Must be called with #DBusConnection lock held.
 * 
 * @param registry the object registry
 * @param object_id object that would like to see the signal
 * @param signal signal name
 *
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_object_registry_connect_locked (DBusObjectRegistry *registry,
                                      const DBusObjectID *object_id,
                                      const char         *signal_name)
{
  DBusSignalEntry **new_signals;
  DBusSignalEntry *signal;
  DBusObjectEntry *entry;
  int i;
  
  entry = validate_id (registry, object_id);
  if (entry == NULL)
    {
      _dbus_warn ("Tried to connect a nonexistent D-BUS object ID to signal \"%s\"\n",
                  signal_name);
      
      return FALSE;
    }

  /* O(n) in number of connections unfortunately, but in practice I
   * don't think it will matter.  It's marginally a space-time
   * tradeoff (save an n_signals field) but the NULL termination is
   * just as large as an n_signals once we have even a single
   * connection.
   */
  i = 0;
  if (entry->signals != NULL)
    {
      while (entry->signals[i] != NULL)
        ++i;
    }
  
  new_signals = dbus_realloc (entry->signals,
                              (i + 2) * sizeof (DBusSignalEntry*));
  
  if (new_signals == NULL)
    return FALSE;

  entry->signals = new_signals;
  
  signal = lookup_signal (registry, signal_name, TRUE); 
  if (signal == NULL)
    goto oom;

  if (!signal_entry_add_object (signal, entry->id_index))
    goto oom;
  
  entry->signals[i] = signal;
  ++i;
  entry->signals[i] = NULL;

  return TRUE;
  
 oom:
  if (signal && signal->n_connections == 0)
    delete_signal (registry, signal);
  
  return FALSE;
}

/**
 * Reverses effects of _dbus_object_registry_disconnect_locked().
 *
 * @param registry the object registry
 * @param object_id object that would like to see the signal
 * @param signal signal name
 */
void
_dbus_object_registry_disconnect_locked (DBusObjectRegistry      *registry,
                                         const DBusObjectID      *object_id,
                                         const char              *signal_name)
{
  DBusObjectEntry *entry;
  DBusSignalEntry *signal;
  
  entry = validate_id (registry, object_id);
  if (entry == NULL)
    {
      _dbus_warn ("Tried to disconnect signal \"%s\" from a nonexistent D-BUS object ID\n",
                  signal_name);
      
      return;
    }

  signal = lookup_signal (registry, signal_name, FALSE);
  if (signal == NULL)
    {
      _dbus_warn ("Tried to disconnect signal \"%s\" but no such signal is connected\n",
                  signal_name);
      return;
    }
  
  signal_entry_remove_object (signal, entry->id_index);

  if (signal->n_connections == 0)
    delete_signal (registry, signal);
}

static DBusHandlerResult
handle_method_call_and_unlock (DBusObjectRegistry *registry,
                               DBusMessage        *message)
{
  DBusInterfaceEntry *iface_entry;
  DBusObjectEntry *object_entry;
  DBusObjectInfo info;
  const DBusObjectVTable *vtable;
  
  _dbus_assert (registry != NULL);
  _dbus_assert (message != NULL);  

  /* FIXME handle calls to an object ID instead of just an
   * interface name
   */
  
  /* If the message isn't to a specific object ID, we send
   * it to the first object that supports the given interface.
   */
  iface_entry = lookup_interface (registry,
                                  dbus_message_get_name (message),
                                  FALSE);
  
  if (iface_entry == NULL)
    {
#ifdef DBUS_BUILD_TESTS
      if (registry->connection)
#endif
        _dbus_connection_unlock (registry->connection);

      return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }
  
  _dbus_assert (iface_entry->n_objects > 0);
  _dbus_assert (iface_entry->objects != NULL);

  object_entry = &registry->entries[iface_entry->objects[0]];


  /* Once we have an object entry, pass message to the object */
  
  _dbus_assert (object_entry->vtable != NULL);

  info_from_entry (registry, &info, object_entry);
  vtable = object_entry->vtable;
  
  /* Drop lock and invoke application code */
#ifdef DBUS_BUILD_TESTS
  if (registry->connection)
#endif
    _dbus_connection_unlock (registry->connection);
  
  (* vtable->message) (&info, message);

  return DBUS_HANDLER_RESULT_REMOVE_MESSAGE;
}

typedef struct
{
  DBusObjectID id;
} ObjectEmitData;

static DBusHandlerResult
handle_signal_and_unlock (DBusObjectRegistry *registry,
                          DBusMessage        *message)
{
  DBusSignalEntry *signal_entry;
  int i;
  ObjectEmitData *objects;
  int n_objects;
  
  _dbus_assert (registry != NULL);
  _dbus_assert (message != NULL);

  signal_entry = lookup_signal (registry,
                                dbus_message_get_name (message),
                                FALSE);
  
  if (signal_entry == NULL)
    {
#ifdef DBUS_BUILD_TESTS
      if (registry->connection)
#endif
        _dbus_connection_unlock (registry->connection);
      
      return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
    }
  
  _dbus_assert (signal_entry->n_connections > 0);
  _dbus_assert (signal_entry->connections != NULL);

  /* make a copy for safety vs. reentrancy */

  /* FIXME (?) if you disconnect a signal during (vs. before)
   * emission, you still receive that signal. To fix this uses more
   * memory because we don't have a per-connection object at the
   * moment. You would have to introduce a connection object and
   * refcount it and have a "disconnected" flag. This is more like
   * GObject semantics but also maybe not important at this level (the
   * GObject/Qt wrappers can mop it up).
   */
  
  n_objects = signal_entry->n_connections;
  objects = dbus_new (ObjectEmitData, n_objects);

  if (objects == NULL)
    {
#ifdef DBUS_BUILD_TESTS
      if (registry->connection)
#endif
        _dbus_connection_unlock (registry->connection);
      
      return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

  i = 0;
  while (i < signal_entry->n_connections)
    {
      DBusObjectEntry *object_entry;
      int idx;
      
      idx = signal_entry->connections[i];

      object_entry = &registry->entries[idx];

      _dbus_assert (object_entry->vtable != NULL);
      
      id_from_entry (registry,
                     &objects[i].id,
                     object_entry);
      
      ++i;
    }

#ifdef DBUS_BUILD_TESTS
  if (registry->connection)
#endif
    _dbus_connection_ref_unlocked (registry->connection);
  _dbus_object_registry_ref (registry);
  dbus_message_ref (message);
  
  i = 0;
  while (i < n_objects)
    {
      DBusObjectEntry *object_entry;

      /* If an object ID no longer exists, don't send the
       * signal
       */
      object_entry = validate_id (registry, &objects[i].id);
      if (object_entry != NULL)
        {
          const DBusObjectVTable *vtable;
          DBusObjectInfo info;

          info_from_entry (registry, &info, object_entry);
          vtable = object_entry->vtable;

          /* Drop lock and invoke application code */
#ifdef DBUS_BUILD_TESTS
          if (registry->connection)
#endif
            _dbus_connection_unlock (registry->connection);
          
          (* vtable->message) (&info, message);

          /* Reacquire lock */
#ifdef DBUS_BUILD_TESTS
          if (registry->connection)
#endif
            _dbus_connection_lock (registry->connection);
        }
      ++i;
    }

  dbus_message_unref (message);
  _dbus_object_registry_unref (registry);
#ifdef DBUS_BUILD_TESTS
  if (registry->connection)
#endif
    _dbus_connection_unref_unlocked (registry->connection);

  dbus_free (objects);
  
  /* Drop lock a final time */
#ifdef DBUS_BUILD_TESTS
  if (registry->connection)
#endif
    _dbus_connection_unlock (registry->connection);

  return DBUS_HANDLER_RESULT_REMOVE_MESSAGE;
}

/**
 * Handle a message, passing it to any objects in the registry that
 * should receive it.
 *
 * @todo handle messages to an object ID, not just those to
 * an interface name.
 * 
 * @param registry the object registry
 * @param message the message to handle
 * @returns what to do with the message next
 */
DBusHandlerResult
_dbus_object_registry_handle_and_unlock (DBusObjectRegistry *registry,
                                         DBusMessage        *message)
{
  int type;
  
  _dbus_assert (registry != NULL);
  _dbus_assert (message != NULL);
  
  type = dbus_message_get_type (message);

  switch (type)
    {
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
      return handle_method_call_and_unlock (registry, message);
    case DBUS_MESSAGE_TYPE_SIGNAL:
      return handle_signal_and_unlock (registry, message);
    default:
#ifdef DBUS_BUILD_TESTS
      if (registry->connection)
#endif
        _dbus_connection_unlock (registry->connection);

      return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
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

  object_remove_from_signals (registry, entry);
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
  DBusMessage *message;
  
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

  message = dbus_message_new_method_call ("org.freedesktop.Test.Foo", NULL);
  if (message != NULL)
    {
      if (_dbus_object_registry_handle_and_unlock (registry, message) !=
          DBUS_HANDLER_RESULT_REMOVE_MESSAGE)
        _dbus_assert_not_reached ("message not handled\n");
      dbus_message_unref (message);
    }

  message = dbus_message_new_method_call ("org.freedesktop.Test.Blah", NULL);
  if (message != NULL)
    {
      if (_dbus_object_registry_handle_and_unlock (registry, message) !=
          DBUS_HANDLER_RESULT_REMOVE_MESSAGE)
        _dbus_assert_not_reached ("message not handled\n");
      dbus_message_unref (message);
    }

  message = dbus_message_new_method_call ("org.freedesktop.Test.NotRegisteredIface", NULL);
  if (message != NULL)
    {
      if (_dbus_object_registry_handle_and_unlock (registry, message) !=
          DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS)
        _dbus_assert_not_reached ("message handled but no handler was registered\n");
      dbus_message_unref (message);
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
