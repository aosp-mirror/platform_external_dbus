/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-hash.h  Generic hash table utility (internal to D-BUS implementation)
 *
 * Copyright (C) 2002  Red Hat, Inc.
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

#ifndef DBUS_HASH_H
#define DBUS_HASH_H

DBUS_BEGIN_DECLS

#include <dbus/dbus-memory.h>
#include <dbus/dbus-types.h>

typedef struct DBusHashTable DBusHashTable;
typedef struct DBusHashIter  DBusHashIter;

/* The iterator is on the stack, but its real fields are
 * hidden privately.
 */
struct DBusHashIter
{
  void *dummy1;
  int   dummy2;
  void *dummy3;  
};

/* Allowing an arbitrary function as with GLib
 * would probably be nicer, but this is internal API so
 * who cares
 */
typedef enum
{
  DBUS_HASH_STRING,
  DBUS_HASH_INT
} DBusHashType;

DBusHashTable* _dbus_hash_table_new   (DBusHashType     type,
                                       DBusFreeFunction key_free_function,
                                       DBusFreeFunction value_free_function);
void           _dbus_hash_table_ref   (DBusHashTable   *table);
void           _dbus_hash_table_unref (DBusHashTable   *table);

/* usage "while (_dbus_hash_table_iterate (table, &iter)) { }" */
dbus_bool_t _dbus_hash_table_iterate (DBusHashTable *table,
                                      DBusHashIter  *iter);

void*       _dbus_hash_iter_get_value      (DBusHashEntry *iter);
void        _dbus_hash_iter_set_value      (DBusHashEntry *iter,
                                            void          *value);
int         _dbus_hash_iter_get_int_key    (DBusHashIter  *iter);
const char* _dbus_hash_iter_get_string_key (DBusHashIter  *iter);

void* _dbus_hash_table_lookup_string (DBusHashTable *table,
                                      const char    *key);
void* _dbus_hash_table_lookup_int    (DBusHashTable *table,
                                      int            key);
void  _dbus_hash_table_remove_string (DBusHashTable *table,
                                      const char    *key);
void  _dbus_hash_table_remove_int    (DBusHashTable *table,
                                      int            key);
void  _dbus_hash_table_insert_string (DBusHashTable *table,
                                      const char    *key,
                                      void          *value);
void  _dbus_hash_table_insert_int    (DBusHashTable *table,
                                      int            key,
                                      void          *value);


DBUS_END_DECLS

#endif /* DBUS_HASH_H */
