/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-address.c  Server address parser.
 *
 * Copyright (C) 2003  CodeFactory AB
 * Copyright (C) 2004  Red Hat, Inc.
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

#include <config.h>
#include "dbus-address.h"
#include "dbus-internals.h"
#include "dbus-list.h"
#include "dbus-string.h"
#include "dbus-protocol.h"

/**
 * @defgroup DBusAddressInternals Address parsing
 * @ingroup  DBusInternals
 * @brief Implementation of parsing addresses of D-BUS servers.
 *
 * @{
 */

/**
 * Internals of DBusAddressEntry 
 */
struct DBusAddressEntry
{
  DBusString method; /**< The address type (unix, tcp, etc.) */

  DBusList *keys;    /**< List of keys */
  DBusList *values;  /**< List of values */
};

/** @} */ /* End of internals */

static void
dbus_address_entry_free (DBusAddressEntry *entry)
{
  DBusList *link;
  
  _dbus_string_free (&entry->method);

  link = _dbus_list_get_first_link (&entry->keys);
  while (link != NULL)
    {
      _dbus_string_free (link->data);
      dbus_free (link->data);
      
      link = _dbus_list_get_next_link (&entry->keys, link);
    }
  _dbus_list_clear (&entry->keys);
  
  link = _dbus_list_get_first_link (&entry->values);
  while (link != NULL)
    {
      _dbus_string_free (link->data);
      dbus_free (link->data);
      
      link = _dbus_list_get_next_link (&entry->values, link);
    }
  _dbus_list_clear (&entry->values);
  
  dbus_free (entry);
}

/**
 * @defgroup DBusAddress Address parsing
 * @ingroup  DBus
 * @brief Parsing addresses of D-BUS servers.
 *
 * @{
 */

/**
 * Frees a #NULL-terminated array of address entries.
 *
 * @param entries the array.
 */
void
dbus_address_entries_free (DBusAddressEntry **entries)
{
  int i;
  
  for (i = 0; entries[i] != NULL; i++)
    dbus_address_entry_free (entries[i]);
  dbus_free (entries);
}

static DBusAddressEntry *
create_entry (void)
{
  DBusAddressEntry *entry;

  entry = dbus_new0 (DBusAddressEntry, 1);

  if (entry == NULL)
    return NULL;

  if (!_dbus_string_init (&entry->method))
    {
      dbus_free (entry);
      return NULL;
    }

  return entry;
}

/**
 * Returns the method string of an address entry.
 *
 * @param entry the entry.
 * @returns a string describing the method. This string
 * must not be freed.
 */
const char *
dbus_address_entry_get_method (DBusAddressEntry *entry)
{
  return _dbus_string_get_const_data (&entry->method);
}

/**
 * Returns a value from a key of an entry.
 *
 * @param entry the entry.
 * @param key the key.
 * @returns the key value. This string must not be fred.
 */
const char *
dbus_address_entry_get_value (DBusAddressEntry *entry,
			      const char       *key)
{
  DBusList *values, *keys;

  keys = _dbus_list_get_first_link (&entry->keys);
  values = _dbus_list_get_first_link (&entry->values);

  while (keys != NULL)
    {
      _dbus_assert (values != NULL);

      if (_dbus_string_equal_c_str (keys->data, key))
        return _dbus_string_get_const_data (values->data);

      keys = _dbus_list_get_next_link (&entry->keys, keys);
      values = _dbus_list_get_next_link (&entry->values, values);
    }
  
  return NULL;
}

/**
 * Parses an address string of the form:
 *
 * method:key=value,key=value;method:key=value
 *
 * @todo document address format in the specification
 *
 * @todo need to be able to escape ';' and ',' in the
 * key values, and the parsing needs to handle that.
 * 
 * @param address the address.
 * @param entry return location to an array of entries.
 * @param array_len return location for array length.
 * @param error address where an error can be returned.
 * @returns #TRUE on success, #FALSE otherwise.
 */
dbus_bool_t
dbus_parse_address (const char         *address,
		    DBusAddressEntry ***entry,
		    int                *array_len,
                    DBusError          *error)
{
  DBusString str;
  int pos, end_pos, len, i;
  DBusList *entries, *link;
  DBusAddressEntry **entry_array;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  _dbus_string_init_const (&str, address);

  entries = NULL;
  pos = 0;
  len = _dbus_string_get_length (&str);
  
  while (pos < len)
    {
      DBusAddressEntry *entry;

      int found_pos;

      entry = create_entry ();
      if (!entry)
	{
	  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);

	  goto error;
	}
      
      /* Append the entry */
      if (!_dbus_list_append (&entries, entry))
	{
	  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
	  dbus_address_entry_free (entry);
	  goto error;
	}
      
      /* Look for a semi-colon */
      if (!_dbus_string_find (&str, pos, ";", &end_pos))
	end_pos = len;
      
      /* Look for the colon : */
      if (!_dbus_string_find_to (&str, pos, end_pos, ":", &found_pos))
	{
	  dbus_set_error (error, DBUS_ERROR_BAD_ADDRESS, "Address does not contain a colon");
	  goto error;
	}

      if (!_dbus_string_copy_len (&str, pos, found_pos - pos, &entry->method, 0))
	{
	  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
	  goto error;
	}
	  
      pos = found_pos + 1;

      while (pos < end_pos)
	{
	  int comma_pos, equals_pos;

	  if (!_dbus_string_find_to (&str, pos, end_pos, ",", &comma_pos))
	    comma_pos = end_pos;
	  
	  if (!_dbus_string_find_to (&str, pos, comma_pos, "=", &equals_pos) ||
	      equals_pos == pos || equals_pos + 1 == comma_pos)
	    {
	      dbus_set_error (error, DBUS_ERROR_BAD_ADDRESS,
                              "'=' character not found or has no value following it");
              goto error;
	    }
	  else
	    {
	      DBusString *key;
	      DBusString *value;

	      key = dbus_new0 (DBusString, 1);

	      if (!key)
		{
		  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);		  
		  goto error;
		}

	      value = dbus_new0 (DBusString, 1);
	      if (!value)
		{
		  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
		  dbus_free (key);
		  goto error;
		}
	      
	      if (!_dbus_string_init (key))
		{
		  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
		  dbus_free (key);
		  dbus_free (value);
		  
		  goto error;
		}
	      
	      if (!_dbus_string_init (value))
		{
		  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
		  _dbus_string_free (key);

		  dbus_free (key);
		  dbus_free (value);		  
		  goto error;
		}

	      if (!_dbus_string_copy_len (&str, pos, equals_pos - pos, key, 0))
		{
		  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
		  _dbus_string_free (key);
		  _dbus_string_free (value);

		  dbus_free (key);
		  dbus_free (value);		  
		  goto error;
		}

	      if (!_dbus_string_copy_len (&str, equals_pos + 1, comma_pos - equals_pos - 1, value, 0))
		{
		  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);		  
		  _dbus_string_free (key);
		  _dbus_string_free (value);

		  dbus_free (key);
		  dbus_free (value);		  
		  goto error;
		}

	      if (!_dbus_list_append (&entry->keys, key))
		{
		  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);		  
		  _dbus_string_free (key);
		  _dbus_string_free (value);

		  dbus_free (key);
		  dbus_free (value);		  
		  goto error;
		}

	      if (!_dbus_list_append (&entry->values, value))
		{
		  dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);		  
		  _dbus_string_free (value);

		  dbus_free (value);
		  goto error;		  
		}
	    }

	  pos = comma_pos + 1;
	}

      pos = end_pos + 1;
    }

  *array_len = _dbus_list_get_length (&entries);
  
  entry_array = dbus_new (DBusAddressEntry *, *array_len + 1);

  if (!entry_array)
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      
      goto error;
    }
  
  entry_array [*array_len] = NULL;

  link = _dbus_list_get_first_link (&entries);
  i = 0;
  while (link != NULL)
    {
      entry_array[i] = link->data;
      i++;
      link = _dbus_list_get_next_link (&entries, link);
    }

  _dbus_list_clear (&entries);
  *entry = entry_array;

  return TRUE;

 error:
  
  link = _dbus_list_get_first_link (&entries);
  while (link != NULL)
    {
      dbus_address_entry_free (link->data);
      link = _dbus_list_get_next_link (&entries, link);
    }

  _dbus_list_clear (&entries);
  
  return FALSE;
  
}


/** @} */ /* End of public API */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"

dbus_bool_t
_dbus_address_test (void)
{
  DBusAddressEntry **entries;
  int len;  
  DBusError error;

  dbus_error_init (&error);
  
  if (!dbus_parse_address ("unix:path=/tmp/foo;debug:name=test,sliff=sloff;",
			   &entries, &len, &error))
    _dbus_assert_not_reached ("could not parse address");
  _dbus_assert (len == 2);
  _dbus_assert (strcmp (dbus_address_entry_get_value (entries[0], "path"), "/tmp/foo") == 0);
  _dbus_assert (strcmp (dbus_address_entry_get_value (entries[1], "name"), "test") == 0);
  _dbus_assert (strcmp (dbus_address_entry_get_value (entries[1], "sliff"), "sloff") == 0);
  
  dbus_address_entries_free (entries);

  /* Different possible errors */
  if (dbus_parse_address ("foo", &entries, &len, &error))
    _dbus_assert_not_reached ("Parsed incorrect address.");
  else
    dbus_error_free (&error);
  
  if (dbus_parse_address ("foo:bar", &entries, &len, &error))
    _dbus_assert_not_reached ("Parsed incorrect address.");
  else
    dbus_error_free (&error);
  
  if (dbus_parse_address ("foo:bar,baz", &entries, &len, &error))
    _dbus_assert_not_reached ("Parsed incorrect address.");
  else
    dbus_error_free (&error);
  
  if (dbus_parse_address ("foo:bar=foo,baz", &entries, &len, &error))
    _dbus_assert_not_reached ("Parsed incorrect address.");
  else
    dbus_error_free (&error);
  
  if (dbus_parse_address ("foo:bar=foo;baz", &entries, &len, &error))
    _dbus_assert_not_reached ("Parsed incorrect address.");
  else
    dbus_error_free (&error);
  
  if (dbus_parse_address ("foo:=foo", &entries, &len, &error))
    _dbus_assert_not_reached ("Parsed incorrect address.");
  else
    dbus_error_free (&error);
  
  if (dbus_parse_address ("foo:foo=", &entries, &len, &error))
    _dbus_assert_not_reached ("Parsed incorrect address.");
  else
    dbus_error_free (&error);

  if (dbus_parse_address ("foo:foo,bar=baz", &entries, &len, &error))
    _dbus_assert_not_reached ("Parsed incorrect address.");
  else
    dbus_error_free (&error);

  return TRUE;
}

#endif
