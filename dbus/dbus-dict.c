/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-dict.h Dict object for key-value data.
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
#include "dbus-dict.h"
#include "dbus-hash.h"
#include "dbus-internals.h"
#include "dbus-protocol.h"

/**
 * @defgroup DBusDict DBusDict
 * @ingroup DBus
 * @brief key/value data structure.
 *
 * A DBusDict is a data structure that can store and lookup different
 * values by name.
 *
 * @{
 */

struct DBusDict
{
  int refcount;

  DBusHashTable *table;
};

typedef struct
{
  int type;
  union {
    dbus_bool_t boolean_value;
    dbus_int32_t int32_value;
    dbus_uint32_t uint32_value;
    double double_value;
    char *string_value;
    struct {
      unsigned char *value;
      int len;
    } byte_array;
    struct {
      dbus_int32_t *value;
      int len;
    } int32_array;
    struct {
      dbus_uint32_t *value;
      int len;
    } uint32_array;
    struct {
      double *value;
      int len;
    } double_array;
    struct {
      char **value;
      int len;
    } string_array;
  } v;
} DBusDictEntry;

static void
dbus_dict_entry_free (DBusDictEntry *entry)
{
  if (!entry)
    return;
  
  switch (entry->type)
    {
    case DBUS_TYPE_INVALID:
    case DBUS_TYPE_BOOLEAN:
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT32:
    case DBUS_TYPE_DOUBLE:
      break;
    case DBUS_TYPE_STRING:
      dbus_free (entry->v.string_value);
      break;
    case DBUS_TYPE_BYTE_ARRAY:
    case DBUS_TYPE_BOOLEAN_ARRAY:
      dbus_free (entry->v.byte_array.value);
      break;
    case DBUS_TYPE_INT32_ARRAY:
      dbus_free (entry->v.int32_array.value);
      break;
    case DBUS_TYPE_UINT32_ARRAY:
      dbus_free (entry->v.uint32_array.value);
      break;
    case DBUS_TYPE_DOUBLE_ARRAY:
      dbus_free (entry->v.uint32_array.value);
      break;
    case DBUS_TYPE_STRING_ARRAY:
      dbus_free_string_array (entry->v.string_array.value);
      break;
    default:
      _dbus_assert_not_reached ("Unknown or invalid dict entry type\n");
    }

  dbus_free (entry);
}

/**
 * Constructs a new DBusDict. Returns #NULL if memory can't be
 * allocated.
 *
 * @returns a new DBusDict or #NULL.
 */
DBusDict *
dbus_dict_new (void)
{
  DBusDict *dict;

  dict = dbus_new0 (DBusDict, 1);

  if (!dict)
    return NULL;

  dict->table = _dbus_hash_table_new (DBUS_HASH_STRING, dbus_free, (DBusFreeFunction)dbus_dict_entry_free);

  if (!dict->table)
    {
      dbus_free (dict);
      return NULL;
    }
  
  dict->refcount = 1;
  
  return dict;
}

/**
 * Increments the reference count of a DBusDict.
 *
 * @param dict the dict.
 * @see dbus_dict_unref
 */
void
dbus_dict_ref (DBusDict *dict)
{
  dict->refcount += 1;
  
  _dbus_assert (dict->refcount > 1);
}

/**
 * Decrements the reference count of a DBusDict
 *
 * @param dict the dict
 * @see dbus_dict_ref
 */
void
dbus_dict_unref (DBusDict *dict)
{
  dict->refcount -= 1;

  _dbus_assert (dict->refcount >= 0);

  if (dict->refcount == 0)
    {
      _dbus_hash_table_unref (dict->table);
      dbus_free (dict);
    }
}

/**
 * Checks if the dict contains the specified key.
 *
 * @param dict the dict.
 * @param key the key
 * @returns #TRUE if the dict contains the specified key.
 */
dbus_bool_t
dbus_dict_contains (DBusDict   *dict,
		    const char *key)
{
  return (_dbus_hash_table_lookup_string (dict->table, key) != NULL);
}

/**
 * Removes the dict entry for the given key. If no dict entry for the
 * key exists, this function does nothing.
 *
 * @param dict the dict
 * @param key the key
 * @returns #TRUE if the entry existed
 */
dbus_bool_t
dbus_dict_remove (DBusDict   *dict,
		  const char *key)
{
  return _dbus_hash_table_remove_string (dict->table, key);
}

/**
 * Returns the type of the value in the dict entry specified by the key.
 *
 * @param dict the dict
 * @param key the key
 * @returns the value type or DBUS_TYPE_NIL if the key wasn't found.
 */
int
dbus_dict_get_value_type (DBusDict   *dict,
			  const char *key)
{
  DBusDictEntry *entry;

  entry = _dbus_hash_table_lookup_string (dict->table, key);

  if (!entry)
    return DBUS_TYPE_NIL;
  else
    return entry->type;
}

/**
 * Returns the keys in the dict as a string array.
 *
 * @param dict the dict
 * @param keys return location for string array
 * @param len return location for string array length
 * ®returns #TRUE on success
 */
dbus_bool_t
dbus_dict_get_keys (DBusDict *dict,
		    char   ***keys,
		    int      *len)
{
  int size, i;
  char **tmp;
  char *key;
  DBusHashIter iter;
  
  size = _dbus_hash_table_get_n_entries (dict->table);
  *len = size;
  
  if (size == 0)
    {
      *keys = NULL;
      return TRUE;
    }

  tmp = dbus_new0 (char *, size + 1);
  if (!tmp)
    return FALSE;

  
  i = 0;
  _dbus_hash_iter_init (dict->table, &iter);

  while (_dbus_hash_iter_next (&iter))
    {
      key = _dbus_strdup (_dbus_hash_iter_get_string_key (&iter));

      if (!key)
	{
	  dbus_free_string_array (tmp);
	  return FALSE;
	}

      tmp[i] = key;
      i++;
    }

  *keys = tmp;

  return TRUE;
}

static dbus_bool_t
insert_entry (DBusDict *dict, 
              const char *key,
              DBusDictEntry **entry)
{
  char *tmp;

  tmp = _dbus_strdup (key);

  if (!tmp)
    return FALSE;
  
  *entry = dbus_new0 (DBusDictEntry, 1);
  
  if (!*entry)
    {
      dbus_free (tmp);
      return FALSE;
    }

  if (!_dbus_hash_table_insert_string (dict->table, tmp, *entry))
    {
      dbus_free (tmp);
      dbus_dict_entry_free (*entry);

      return FALSE;
    }

  return TRUE;
}
	      
/**
 * Adds a boolean value to the dict. If a value with the same key
 * already exists, then it will be replaced by the new value.
 *
 * @param dict the dict
 * @param key the key
 * @param value the value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_dict_set_boolean (DBusDict    *dict,
		       const char  *key,
		       dbus_bool_t  value)
{
  DBusDictEntry *entry;

  if (insert_entry (dict, key, &entry))
    {
      entry->type = DBUS_TYPE_BOOLEAN;
      entry->v.boolean_value = value;

      return TRUE;
    }
  else
    return FALSE;
}

/**
 * Adds a 32 bit signed integer value to the dict. If a value with the
 * same key already exists, then it will be replaced by the new value.
 *
 * @param dict the dict
 * @param key the key
 * @param value the value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_dict_set_int32 (DBusDict     *dict,
		     const char   *key,
		     dbus_int32_t  value)
{
  DBusDictEntry *entry;

  if (insert_entry (dict, key, &entry))
    {
      entry->type = DBUS_TYPE_INT32;
      entry->v.int32_value = value;

      return TRUE;
    }
  else
    return FALSE;
}

/**
 * Adds a 32 bit unsigned integer value to the dict. If a value with
 * the same key already exists, then it will be replaced by the new
 * value.
 *
 * @param dict the dict
 * @param key the key
 * @param value the value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_dict_set_uint32 (DBusDict      *dict,
		      const char    *key,
		      dbus_uint32_t  value)
{
  DBusDictEntry *entry;

  if (insert_entry (dict, key, &entry))
    {
      entry->type = DBUS_TYPE_UINT32;
      entry->v.uint32_value = value;

      return TRUE;
    }
  else
    return FALSE;
}

/**
 * Adds a 32 bit double value to the dict. If a value with the same
 * key already exists, then it will be replaced by the new value.
 *
 * @param dict the dict
 * @param key the key
 * @param value the value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_dict_set_double (DBusDict   *dict,
		      const char *key,
		      double      value)
{
  DBusDictEntry *entry;

  if (insert_entry (dict, key, &entry))
    {
      entry->type = DBUS_TYPE_DOUBLE;
      entry->v.double_value = value;

      return TRUE;
    }
  else
    return FALSE;
}

/**
 * Adds a string to the dict. If a value with the same key already
 * exists, then it will be replaced by the new value.
 *
 * @param dict the dict
 * @param key the key
 * @param value the value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_dict_set_string (DBusDict   *dict,
		      const char *key,
		      const char *value)
{
  DBusDictEntry *entry;
  char *tmp;

  tmp = _dbus_strdup (value);

  if (!tmp)
    return FALSE;
  
  if (insert_entry (dict, key, &entry))
    {
      entry->type = DBUS_TYPE_STRING;
      entry->v.string_value = tmp;

      return TRUE;
    }
  else
    return FALSE;
}

/**
 * Adds a boolean array to the dict. If a value with the same key
 * already exists, then it will be replaced by the new value.
 *
 * @param dict the dict
 * @param key the key
 * @param value the value
 * @param len the array length
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_dict_set_boolean_array (DBusDict            *dict,
			     const char          *key,
			     unsigned const char *value,
			     int                  len)
{
  DBusDictEntry *entry;
  unsigned char *tmp;

  if (len == 0)
    tmp = NULL;
  else
    {
      tmp = dbus_malloc (len);
      
      if (!tmp)
	return FALSE;

      memcpy (tmp, value, len);
    }
  
  if (insert_entry (dict, key, &entry))
    {
      entry->type = DBUS_TYPE_BOOLEAN_ARRAY;
      entry->v.byte_array.value = tmp;
      entry->v.byte_array.len = len;
      
      return TRUE;
    }
  else
    return FALSE;
}

/**
 * Adds a 32 bit signed integer array to the dict. If a value with the
 * same key already exists, then it will be replaced by the new value.
 *
 * @param dict the dict
 * @param key the key
 * @param value the value
 * @param len the array length
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_dict_set_int32_array (DBusDict           *dict,
			   const char         *key,
			   const dbus_int32_t *value,
			   int                 len)
{
  DBusDictEntry *entry;
  dbus_int32_t *tmp;

  if (len == 0)
    tmp = NULL;
  else
    {
      tmp = dbus_new (dbus_int32_t, len);
  
      if (!tmp)
	return FALSE;
    }
  
  memcpy (tmp, value, len * sizeof (dbus_int32_t));
  
  if (insert_entry (dict, key, &entry))
    {
      entry->type = DBUS_TYPE_INT32_ARRAY;
      entry->v.int32_array.value = tmp;
      entry->v.int32_array.len = len;
      
      return TRUE;
    }
  else
    return FALSE;
}

/**
 * Adds a 32 bit unsigned integer array to the dict. If a value with
 * the same key already exists, then it will be replaced by the new
 * value.
 *
 * @param dict the dict
 * @param key the key
 * @param value the value
 * @param len the array length
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_dict_set_uint32_array (DBusDict             *dict,
			    const char           *key,
			    const dbus_uint32_t  *value,
			    int                   len)
{
  DBusDictEntry *entry;
  dbus_uint32_t *tmp;

  if (len == 0)
    tmp = NULL;
  else
    {
      tmp = dbus_new (dbus_uint32_t, len);
      
      if (!tmp)
	return FALSE;
      
      memcpy (tmp, value, len * sizeof (dbus_uint32_t));
    }
  
  if (insert_entry (dict, key, &entry))
    {
      entry->type = DBUS_TYPE_UINT32_ARRAY;
      entry->v.uint32_array.value = tmp;
      entry->v.int32_array.len = len;

      return TRUE;
    }
  else
    return FALSE;
}

/**
 * Adds a double array to the dict. If a value with the same key
 * already exists, then it will be replaced by the new value.
 *
 * @param dict the dict
 * @param key the key
 * @param value the value
 * @param len the array length
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_dict_set_double_array (DBusDict     *dict,
			    const char   *key,
			    const double *value,
			    int           len)
{
  DBusDictEntry *entry;
  double *tmp;

  if (len == 0)
    tmp = NULL;
  else
    {
      tmp = dbus_new (double, len);

      if (!tmp)
	return FALSE;

      memcpy (tmp, value, len * sizeof (double));
    }
  
  if (insert_entry (dict, key, &entry))
    {
      entry->type = DBUS_TYPE_DOUBLE_ARRAY;
      entry->v.double_array.value = tmp;
      entry->v.double_array.len = len;

      return TRUE;
    }
  else
    return FALSE;
}

/**
 * Adds a byte array to the dict. If a value with the same key
 * already exists, then it will be replaced by the new value.
 *
 * @param dict the dict
 * @param key the key
 * @param value the value
 * @param len the array length
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_dict_set_byte_array (DBusDict             *dict,
			  const char           *key,
			  unsigned const char  *value,
			  int                   len)
{
  DBusDictEntry *entry;
  unsigned char *tmp;

  if (len == 0)
    tmp = NULL;
  else
    {
      tmp = dbus_malloc (len);
      
      if (!tmp)
	return FALSE;

      memcpy (tmp, value, len);
    }
  
  if (insert_entry (dict, key, &entry))
    {
      entry->type = DBUS_TYPE_BYTE_ARRAY;
      entry->v.byte_array.value = tmp;
      entry->v.byte_array.len = len;
      
      return TRUE;
    }
  else
    return FALSE;
}


/**
 * Adds a string array to the dict. If a value with the same key
 * already exists, then it will be replaced by the new value.
 *
 * @param dict the dict
 * @param key the key
 * @param value the value
 * @param len the array length
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_dict_set_string_array (DBusDict    *dict,
			    const char  *key,
			    const char **value,
			    int          len)
{
  DBusDictEntry *entry;
  char **tmp;
  int i;

  tmp = dbus_new0 (char *, len + 1);
  if (!tmp)
    return FALSE;

  tmp[len] = NULL;
  
  for (i = 0; i < len; i++)
    {
      tmp[i] = _dbus_strdup (value[i]);
      if (!tmp[i])
	{
	  dbus_free_string_array (tmp);
	  return FALSE;
	}
    }

  if (insert_entry (dict, key, &entry))
    {
      entry->type = DBUS_TYPE_STRING_ARRAY;
      entry->v.string_array.value = tmp;
      entry->v.string_array.len = len;
      
      return TRUE;
    }
  else
    return FALSE;
}

/**
 * Gets a boolean value from a dict using a key.
 *
 * @param dict the dict
 * @param key the key
 * @param value return location for the value
 * @returns #TRUE if the key exists and the value is of the correct
 * type
 */
dbus_bool_t
dbus_dict_get_boolean (DBusDict    *dict,
		       const char  *key,
		       dbus_bool_t *value)
{
  DBusDictEntry *entry;

  entry = _dbus_hash_table_lookup_string (dict->table, key);

  if (!entry || entry->type != DBUS_TYPE_BOOLEAN)
    return FALSE;

  *value = entry->v.boolean_value;

  return TRUE;
}

/**
 * Gets a 32 bit signed integer value from a dict using a key.
 *
 * @param dict the dict
 * @param key the key
 * @param value return location for the value
 * @returns #TRUE if the key exists and the value is of the correct
 * type
 */
dbus_bool_t
dbus_dict_get_int32 (DBusDict     *dict,
		     const char   *key,
		     dbus_int32_t *value)
{
  DBusDictEntry *entry;

  entry = _dbus_hash_table_lookup_string (dict->table, key);

  if (!entry || entry->type != DBUS_TYPE_INT32)
    return FALSE;

  *value = entry->v.int32_value;

  return TRUE;
}

/**
 * Gets a 32 bit unsigned integer value from a dict using a key.
 *
 * @param dict the dict
 * @param key the key
 * @param value return location for the value
 * @returns #TRUE if the key exists and the value is of the correct
 * type
 */
dbus_bool_t
dbus_dict_get_uint32 (DBusDict      *dict,
		      const char    *key,
		      dbus_uint32_t *value)
{
  DBusDictEntry *entry;

  entry = _dbus_hash_table_lookup_string (dict->table, key);

  if (!entry || entry->type != DBUS_TYPE_UINT32)
    return FALSE;

  *value = entry->v.uint32_value;

  return TRUE;
}

/**
 * Gets a double value from a dict using a key.
 *
 * @param dict the dict
 * @param key the key
 * @param value return location for the value
 * @returns #TRUE if the key exists and the value is of the correct
 * type
 */
dbus_bool_t
dbus_dict_get_double (DBusDict   *dict,
		      const char *key,
		      double     *value)
{
  DBusDictEntry *entry;

  entry = _dbus_hash_table_lookup_string (dict->table, key);

  if (!entry || entry->type != DBUS_TYPE_DOUBLE)
    return FALSE;

  *value = entry->v.double_value;

  return TRUE;
}

/**
 * Gets a string from a dict using a key.
 *
 * @param dict the dict
 * @param key the key
 * @param value return location for the value
 * @returns #TRUE if the key exists and the value is of the correct
 * type
 */
dbus_bool_t
dbus_dict_get_string (DBusDict    *dict,
		      const char  *key,
		      const char **value)
{
  DBusDictEntry *entry;
  
  entry = _dbus_hash_table_lookup_string (dict->table, key);
  
  if (!entry || entry->type != DBUS_TYPE_STRING)
    return FALSE;

  *value = entry->v.string_value;

  return TRUE;
}

/**
 * Gets a boolean array from a dict using a key.
 *
 * @param dict the dict
 * @param key the key
 * @param value return location for the value
 * @param len return location for the array length
 * @returns #TRUE if the key exists and the value is of the correct
 * type
 */
dbus_bool_t
dbus_dict_get_boolean_array (DBusDict             *dict,
			     const char           *key,
			     unsigned const char **value,
			     int                  *len)
{
  DBusDictEntry *entry;
  
  entry = _dbus_hash_table_lookup_string (dict->table, key);
  
  if (!entry || entry->type != DBUS_TYPE_BOOLEAN_ARRAY)
    return FALSE;

  *value = entry->v.byte_array.value;
  *len = entry->v.byte_array.len;
  
  return TRUE;
}

/**
 * Gets a 32 bit signed integer array from a dict using a key.
 *
 * @param dict the dict
 * @param key the key
 * @param value return location for the value
 * @param len return location for the array length
 * @returns #TRUE if the key exists and the value is of the correct
 * type
 */
dbus_bool_t
dbus_dict_get_int32_array (DBusDict            *dict,
			   const char          *key,
			   const dbus_int32_t **value,
			   int                 *len)
{
  DBusDictEntry *entry;
  
  entry = _dbus_hash_table_lookup_string (dict->table, key);
  
  if (!entry || entry->type != DBUS_TYPE_INT32_ARRAY)
    return FALSE;

  *value = entry->v.int32_array.value;
  *len = entry->v.int32_array.len;
  
  return TRUE;
}

/**
 * Gets a 32 bit unsigned integer array from a dict using a key.
 *
 * @param dict the dict
 * @param key the key
 * @param value return location for the value
 * @param len return location for the array length
 * @returns #TRUE if the key exists and the value is of the correct
 * type
 */
dbus_bool_t
dbus_dict_get_uint32_array (DBusDict             *dict,
			    const char           *key,
			    const dbus_uint32_t **value,
			    int                  *len)
{
  DBusDictEntry *entry;
  
  entry = _dbus_hash_table_lookup_string (dict->table, key);
  
  if (!entry || entry->type != DBUS_TYPE_UINT32_ARRAY)
    return FALSE;

  *value = entry->v.uint32_array.value;
  *len = entry->v.uint32_array.len;
  
  return TRUE;
}

/**
 * Gets a double array from a dict using a key.
 *
 * @param dict the dict
 * @param key the key
 * @param value return location for the value
 * @param len return location for the array length
 * @returns #TRUE if the key exists and the value is of the correct
 * type
 */
dbus_bool_t
dbus_dict_get_double_array (DBusDict      *dict,
			    const char    *key,
			    const double **value,
			    int           *len)
{
  DBusDictEntry *entry;
  
  entry = _dbus_hash_table_lookup_string (dict->table, key);
  
  if (!entry || entry->type != DBUS_TYPE_DOUBLE_ARRAY)
    return FALSE;

  *value = entry->v.double_array.value;
  *len = entry->v.double_array.len;
  
  return TRUE;
}

/**
 * Gets a byte array from a dict using a key.
 *
 * @param dict the dict
 * @param key the key
 * @param value return location for the value
 * @param len return location for the array length
 * @returns #TRUE if the key exists and the value is of the correct
 * type
 */
dbus_bool_t
dbus_dict_get_byte_array (DBusDict             *dict,
			  const char           *key,
			  unsigned const char **value,
			  int                  *len)
{
  DBusDictEntry *entry;
  
  entry = _dbus_hash_table_lookup_string (dict->table, key);
  
  if (!entry || entry->type != DBUS_TYPE_BYTE_ARRAY)
    return FALSE;

  *value = entry->v.byte_array.value;
  *len = entry->v.byte_array.len;
  
  return TRUE;
}

/**
 * Gets a string array from a dict using a key.
 *
 * @param dict the dict
 * @param key the key
 * @param value return location for the value
 * @param len return location for the array length
 * @returns #TRUE if the key exists and the value is of the correct
 * type
 */
dbus_bool_t
dbus_dict_get_string_array (DBusDict     *dict,
			    const char   *key,
			    const char ***value,
			    int           *len)
{
  DBusDictEntry *entry;
  
  entry = _dbus_hash_table_lookup_string (dict->table, key);
  
  if (!entry || entry->type != DBUS_TYPE_STRING_ARRAY)
    return FALSE;

  *value = (const char **)entry->v.string_array.value;
  *len = entry->v.string_array.len;
  
  return TRUE;
}

/** @} */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"

dbus_bool_t
_dbus_dict_test (void)
{
  DBusDict *dict;
  dbus_bool_t our_bool;
  dbus_int32_t our_int;
  dbus_uint32_t our_uint;
  double our_double;
  const char *our_string;
  const unsigned char boolean_array[] = { TRUE, FALSE, FALSE, TRUE };
  const unsigned char *our_boolean_array;
  const dbus_int32_t int32_array[] = { 0x12345678, -1911, 0, 0xaffe, 0xedd1e };
  const dbus_int32_t *our_int32_array;
  const dbus_uint32_t uint32_array[] = { 0x12345678, 0, 0xdeadbeef, 0x87654321, 0xffffffff };
  const dbus_uint32_t *our_uint32_array;
  const double double_array[] = { 3.14159, 1.2345, 6.7890 };
  const double *our_double_array;
  const char *string_array[] = { "This", "Is", "A", "Test" };
  const char **our_string_array;
  int i, len;

  /* We don't test much here since the hash table tests cover a great
     deal of the functionality. */

  dict = dbus_dict_new ();

  if (dbus_dict_get_value_type (dict, "foo") != DBUS_TYPE_NIL)
    _dbus_assert_not_reached ("didn't return DBUS_TYPE_NIL for non-existant entry");

  if (!dbus_dict_set_boolean (dict, "boolean", TRUE))
    _dbus_assert_not_reached ("could not add boolean value");

  if (!dbus_dict_get_boolean (dict, "boolean", &our_bool) ||
      !our_bool)
    _dbus_assert_not_reached ("could not get boolean value");

  if (!dbus_dict_set_int32 (dict, "int32", 0x12345678))
    _dbus_assert_not_reached ("could not add int32 value");

  if (!dbus_dict_get_int32 (dict, "int32", &our_int) || our_int != 0x12345678)
    _dbus_assert_not_reached ("could not get int32 value or int32 values differ");
  
  if (!dbus_dict_set_uint32 (dict, "uint32", 0x87654321))
    _dbus_assert_not_reached ("could not add uint32 value");

  if (!dbus_dict_get_uint32 (dict, "uint32", &our_uint) || our_uint != 0x87654321)
    _dbus_assert_not_reached ("could not get uint32 value or uint32 values differ");

  if (!dbus_dict_set_double (dict, "double", 3.14159))
    _dbus_assert_not_reached ("could not add double value");

  if (!dbus_dict_get_double (dict, "double", &our_double) || our_double != 3.14159)
    _dbus_assert_not_reached ("could not get double value or double values differ");

  if (!dbus_dict_set_string (dict, "string", "test string"))
    _dbus_assert_not_reached ("could not add string value");

  if (!dbus_dict_get_string (dict, "string", &our_string) || strcmp (our_string, "test string") != 0)
    _dbus_assert_not_reached ("could not get string value or string values differ");

  if (!dbus_dict_set_boolean_array (dict, "boolean_array", boolean_array, 4))
    _dbus_assert_not_reached ("could not add boolean array");

  if (!dbus_dict_get_boolean_array (dict, "boolean_array", &our_boolean_array, &len) ||
      len != 4 || memcmp (boolean_array, our_boolean_array, 4) != 0)
    _dbus_assert_not_reached ("could not get boolean array value or boolean array values differ");

  if (!dbus_dict_set_int32_array (dict, "int32_array", int32_array, 5))
    _dbus_assert_not_reached ("could not add int32 array");

  if (!dbus_dict_get_int32_array (dict, "int32_array", &our_int32_array, &len) ||
      len != 5 || memcmp (int32_array, our_int32_array, 5 * sizeof (dbus_int32_t)) != 0)
    _dbus_assert_not_reached ("could not get int32 array value or int32 array values differ");

  if (!dbus_dict_set_uint32_array (dict, "uint32_array", uint32_array, 5))
    _dbus_assert_not_reached ("could not add uint32 array");

  if (!dbus_dict_get_uint32_array (dict, "uint32_array", &our_uint32_array, &len) ||
      len != 5 || memcmp (uint32_array, our_uint32_array, 5 * sizeof (dbus_uint32_t) ) != 0)
    _dbus_assert_not_reached ("could not get uint32 array value or uint32 array values differ");

  if (!dbus_dict_set_double_array (dict, "double_array", double_array, 3))
    _dbus_assert_not_reached ("could not add double array");

  if (!dbus_dict_get_double_array (dict, "double_array", &our_double_array, &len) ||
      len != 3 || memcmp (double_array, our_double_array, 3 * sizeof (double)) != 0)
    _dbus_assert_not_reached ("could not get double array value or double array values differ");

  if (!dbus_dict_set_string_array (dict, "string_array", string_array, 4))
    _dbus_assert_not_reached ("could not add string array");

  if (!dbus_dict_get_string_array (dict, "string_array", &our_string_array, &len))
    _dbus_assert_not_reached ("could not get string array value");

  if (len != 4)
    _dbus_assert_not_reached ("string array lengths differ");

  for (i = 0; i < len; i++)
    {
      if (strcmp (our_string_array[i], string_array[i]) != 0)
	_dbus_assert_not_reached ("string array fields differ");
    }

  dbus_dict_unref (dict);

  return TRUE;
}
#endif /* DBUS_BUILD_TESTS */
