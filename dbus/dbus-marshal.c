/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-marshal.c  Marshalling routines
 *
 * Copyright (C) 2002  CodeFactory AB
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

#include "dbus-marshal.h"
#include "dbus-internals.h"
#define DBUS_CAN_USE_DBUS_STRING_PRIVATE 1
#include "dbus-string-private.h"

#include <string.h>

/* from ORBit */
static void
swap_bytes (unsigned char *data,
            unsigned int   len)
{
  unsigned char *p1 = data;
  unsigned char *p2 = data + len - 1;

  while (p1 < p2)
    {
      unsigned char tmp = *p1;
      *p1 = *p2;
      *p2 = tmp;

      --p2;
      ++p1;
    }
}

/**
 * @defgroup DBusMarshal marshaling and unmarshaling
 * @ingroup  DBusInternals
 * @brief functions to marshal/unmarshal data from the wire
 *
 * Types and functions related to converting primitive data types from
 * wire format to native machine format, and vice versa.
 *
 * @{
 */

/**
 * Unpacks a 32 bit unsigned integer from a data pointer
 *
 * @param byte_order The byte order to use
 * @param data the data pointer
 * @returns the integer
 */
dbus_uint32_t
_dbus_unpack_uint32 (int                  byte_order,
                     const unsigned char *data)
{
  _dbus_assert (_DBUS_ALIGN_ADDRESS (data, 4) == data);
  
  if (byte_order == DBUS_LITTLE_ENDIAN)
    return DBUS_UINT32_FROM_LE (*(dbus_uint32_t*)data);
  else
    return DBUS_UINT32_FROM_BE (*(dbus_uint32_t*)data);
}  

/**
 * Unpacks a 32 bit signed integer from a data pointer
 *
 * @param byte_order The byte order to use
 * @param data the data pointer
 * @returns the integer
 */
dbus_int32_t
_dbus_unpack_int32 (int                  byte_order,
                    const unsigned char *data)
{
  _dbus_assert (_DBUS_ALIGN_ADDRESS (data, 4) == data);
  
  if (byte_order == DBUS_LITTLE_ENDIAN)
    return DBUS_INT32_FROM_LE (*(dbus_int32_t*)data);
  else
    return DBUS_INT32_FROM_BE (*(dbus_int32_t*)data);
}

/**
 * Packs a 32 bit unsigned integer into a data pointer.
 *
 * @param value the value
 * @param byte_order the byte order to use
 * @param data the data pointer
 */
void
_dbus_pack_uint32 (dbus_uint32_t   value,
                   int             byte_order,
                   unsigned char  *data)
{
  _dbus_assert (_DBUS_ALIGN_ADDRESS (data, 4) == data);
  
  if ((byte_order) == DBUS_LITTLE_ENDIAN)                  
    *((dbus_uint32_t*)(data)) = DBUS_UINT32_TO_LE (value);       
  else
    *((dbus_uint32_t*)(data)) = DBUS_UINT32_TO_BE (value);
}

/**
 * Packs a 32 bit signed integer into a data pointer.
 *
 * @param value the value
 * @param byte_order the byte order to use
 * @param data the data pointer
 */
void
_dbus_pack_int32 (dbus_int32_t   value,
                  int            byte_order,
                  unsigned char *data)
{
  _dbus_assert (_DBUS_ALIGN_ADDRESS (data, 4) == data);
  
  if ((byte_order) == DBUS_LITTLE_ENDIAN)                  
    *((dbus_int32_t*)(data)) = DBUS_INT32_TO_LE (value);       
  else
    *((dbus_int32_t*)(data)) = DBUS_INT32_TO_BE (value);
}

/**
 * Sets the 4 bytes at the given offset to a marshaled signed integer,
 * replacing anything found there previously.
 *
 * @param str the string to write the marshalled int to
 * @param offset the byte offset where int should be written
 * @param byte_order the byte order to use
 * @param value the value
 * 
 */
void
_dbus_marshal_set_int32 (DBusString          *str,
                         int                  byte_order,
                         int                  offset,
                         dbus_int32_t         value)
{
  char *data;
  
  _dbus_assert (byte_order == DBUS_LITTLE_ENDIAN ||
                byte_order == DBUS_BIG_ENDIAN);
  
  _dbus_string_get_data_len (str, &data, offset, 4);

  _dbus_pack_int32 (value, byte_order, data);
}

/**
 * Sets the 4 bytes at the given offset to a marshaled unsigned
 * integer, replacing anything found there previously.
 *
 * @param str the string to write the marshalled int to
 * @param offset the byte offset where int should be written
 * @param byte_order the byte order to use
 * @param value the value
 * 
 */
void
_dbus_marshal_set_uint32 (DBusString          *str,
                          int                  byte_order,
                          int                  offset,
                          dbus_uint32_t        value)
{
  char *data;
  
  _dbus_assert (byte_order == DBUS_LITTLE_ENDIAN ||
                byte_order == DBUS_BIG_ENDIAN);
  
  _dbus_string_get_data_len (str, &data, offset, 4);

  _dbus_pack_uint32 (value, byte_order, data);
}

/**
 * Sets the existing marshaled string at the given offset with
 * a new marshaled string. The given offset must point to
 * an existing string or the wrong length will be deleted
 * and replaced with the new string.
 *
 * @param str the string to write the marshalled string to
 * @param offset the byte offset where string should be written
 * @param byte_order the byte order to use
 * @param value the value
 * @param len the length to use
 * @returns #TRUE on success
 * 
 */
dbus_bool_t
_dbus_marshal_set_string (DBusString          *str,
                          int                  byte_order,
                          int                  offset,
                          const DBusString    *value,
			  int                  len)
{
  int old_len;
  
  _dbus_assert (byte_order == DBUS_LITTLE_ENDIAN ||
                byte_order == DBUS_BIG_ENDIAN);
  
  old_len = _dbus_demarshal_uint32 (str, byte_order,
                                    offset, NULL);

  if (!_dbus_string_replace_len (value, 0, len,
                                 str, offset + 4, old_len))
    return FALSE;

  _dbus_marshal_set_uint32 (str, byte_order,
                            offset, len);

  return TRUE;
}

/**
 * Marshals a double value.
 *
 * @param str the string to append the marshalled value to
 * @param byte_order the byte order to use
 * @param value the value
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_marshal_double (DBusString *str,
		      int         byte_order,
		      double      value)
{
  _dbus_assert (sizeof (double) == 8);

  if (!_dbus_string_align_length (str, sizeof (double)))
    return FALSE;
  
  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    swap_bytes ((unsigned char *)&value, sizeof (double));

  return _dbus_string_append_len (str, (const char *)&value, sizeof (double));
}

/**
 * Marshals a 32 bit signed integer value.
 *
 * @param str the string to append the marshalled value to
 * @param byte_order the byte order to use
 * @param value the value
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_marshal_int32  (DBusString   *str,
		      int           byte_order,
		      dbus_int32_t  value)
{
  if (!_dbus_string_align_length (str, sizeof (dbus_int32_t)))
    return FALSE;
  
  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    value = DBUS_INT32_SWAP_LE_BE (value);

  return _dbus_string_append_len (str, (const char *)&value, sizeof (dbus_int32_t));
}

/**
 * Marshals a 32 bit unsigned integer value.
 *
 * @param str the string to append the marshalled value to
 * @param byte_order the byte order to use
 * @param value the value
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_marshal_uint32 (DBusString    *str,
		      int            byte_order,
		      dbus_uint32_t  value)
{
  if (!_dbus_string_align_length (str, sizeof (dbus_uint32_t)))
    return FALSE;
  
  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    value = DBUS_UINT32_SWAP_LE_BE (value);

  return _dbus_string_append_len (str, (const char *)&value, sizeof (dbus_uint32_t));
}

/**
 * Marshals a UTF-8 string
 *
 * @param str the string to append the marshalled value to
 * @param byte_order the byte order to use
 * @param value the string
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_marshal_string (DBusString    *str,
		      int            byte_order,
		      const char    *value)
{
  int len, old_string_len;

  old_string_len = _dbus_string_get_length (str);
  
  len = strlen (value);

  if (!_dbus_marshal_uint32 (str, byte_order, len))
    {
      /* Restore the previous length */
      _dbus_string_set_length (str, old_string_len);

      return FALSE;
    }

  return _dbus_string_append_len (str, value, len + 1);
}

/**
 * Marshals a byte array
 *
 * @param str the string to append the marshalled value to
 * @param byte_order the byte order to use
 * @param value the array
 * @param len number of elements in the array
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_marshal_byte_array (DBusString          *str,
			  int                  byte_order,
			  const unsigned char *value,
			  int                  len)
{
  int old_string_len;

  old_string_len = _dbus_string_get_length (str);
  
  if (!_dbus_marshal_uint32 (str, byte_order, len))
    {
      /* Restore the previous length */
      _dbus_string_set_length (str, old_string_len);

      return FALSE;
    }

  return _dbus_string_append_len (str, value, len);
}

/**
 * Marshals a 32 bit signed integer array
 *
 * @param str the string to append the marshalled value to
 * @param byte_order the byte order to use
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_marshal_int32_array (DBusString         *str,
			   int                 byte_order,
			   const dbus_int32_t *value,
			   int                 len)
{
  int i, old_string_len;

  old_string_len = _dbus_string_get_length (str);

  if (!_dbus_marshal_uint32 (str, byte_order, len))
    goto error;

  for (i = 0; i < len; i++)
    if (!_dbus_marshal_int32 (str, byte_order, value[i]))
      goto error;

  return TRUE;
  
 error:
  /* Restore previous length */
  _dbus_string_set_length (str, old_string_len);
  
  return FALSE;
}

/**
 * Marshals a 32 bit unsigned integer array
 *
 * @param str the string to append the marshalled value to
 * @param byte_order the byte order to use
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_marshal_uint32_array (DBusString          *str,
			    int                  byte_order,
			    const dbus_uint32_t  *value,
			    int                  len)
{
  int i, old_string_len;

  old_string_len = _dbus_string_get_length (str);

  if (!_dbus_marshal_uint32 (str, byte_order, len))
    goto error;

  for (i = 0; i < len; i++)
    if (!_dbus_marshal_uint32 (str, byte_order, value[i]))
      goto error;

  return TRUE;
  
 error:
  /* Restore previous length */
  _dbus_string_set_length (str, old_string_len);
  
  return FALSE;  
}

/**
 * Marshals a double array
 *
 * @param str the string to append the marshalled value to
 * @param byte_order the byte order to use
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_marshal_double_array (DBusString          *str,
			    int                  byte_order,
			    const double        *value,
			    int                  len)
{
  int i, old_string_len;

  old_string_len = _dbus_string_get_length (str);

  if (!_dbus_marshal_uint32 (str, byte_order, len))
    goto error;

  for (i = 0; i < len; i++)
    if (!_dbus_marshal_double (str, byte_order, value[i]))
      goto error;

  return TRUE;
  
 error:
  /* Restore previous length */
  _dbus_string_set_length (str, old_string_len);
  
  return FALSE;    
}

/**
 * Marshals a string array
 *
 * @param str the string to append the marshalled value to
 * @param byte_order the byte order to use
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_marshal_string_array (DBusString  *str,
			    int          byte_order,
			    const char **value,
			    int          len)
{
  int i, old_string_len;

  old_string_len = _dbus_string_get_length (str);

  if (!_dbus_marshal_uint32 (str, byte_order, len))
    goto error;

  for (i = 0; i < len; i++)
    if (!_dbus_marshal_string (str, byte_order, value[i]))
      goto error;

  return TRUE;
  
 error:
  /* Restore previous length */
  _dbus_string_set_length (str, old_string_len);
  
  return FALSE;      
}


/**
 * Marshals a dict
 * @param str the string to append the marshalled value to
 * @param byte_order the byte order to use
 * @param dict the dict
 * @returns #TRUE on success
 */
dbus_bool_t
_dbus_marshal_dict (DBusString *str,
		    int         byte_order,
		    DBusDict   *dict)
{
  int old_string_len;
  int i, len;
  char **keys;
  
  old_string_len = _dbus_string_get_length (str);

  if (!dbus_dict_get_keys (dict, &keys, &len))
    goto error;

  if (len == 0)
    return TRUE;

  if (!_dbus_marshal_string_array (str, byte_order,
				   keys, len))
    goto error;

  for (i = 0; i < len; i++)
    {
      int value_type;

      value_type = dbus_dict_get_value_type (dict, keys[i]);

      if (!_dbus_string_append_byte (str, value_type))
	goto error;
      
      switch (dbus_dict_get_value_type (dict, keys[i]))
	{
	case DBUS_TYPE_BOOLEAN:
	  {
	    dbus_bool_t value;

	    if (!dbus_dict_get_boolean (dict, keys[i], &value))
	      goto error;
	    
	    if (!_dbus_string_append_byte (str, (value != FALSE)))
	      goto error;

	    break;
	  }
	  
	case DBUS_TYPE_INT32:
	  {
	    dbus_int32_t value;
	    
	    if (!dbus_dict_get_int32 (dict, keys[i], &value))
	      goto error;
	    
	    if (!_dbus_marshal_int32 (str, byte_order, value))
	      goto error;

	    break;
	  }
	case DBUS_TYPE_UINT32:
	  {
	    dbus_uint32_t value;
	    
	    if (!dbus_dict_get_uint32 (dict, keys[i], &value))
	      goto error;
	    
	    if (!_dbus_marshal_uint32 (str, byte_order, value))
	      goto error;

	    break;
	  }
	case DBUS_TYPE_DOUBLE:
	  {
	    double value;
	    
	    if (!dbus_dict_get_double (dict, keys[i], &value))
	      goto error;

	    if (!_dbus_marshal_double (str, byte_order, value))
	      goto error;

	    break;
	  }
	case DBUS_TYPE_INT32_ARRAY:
	  {
	    const dbus_int32_t *value;
	    int len;

	    if (!dbus_dict_get_int32_array (dict, keys[i], &value, &len))
	      goto error;

	    if (!_dbus_marshal_int32_array (str, byte_order, value, len))
	      goto error;
	    
	    break;
	  }
	case DBUS_TYPE_STRING:
	  {
	    const char *value;

	    if (!dbus_dict_get_string (dict, keys[i], &value))
	      goto error;

	    if (!_dbus_marshal_string (str, byte_order, value))
	      goto error;
	    
	    break;
	  }	  
	case DBUS_TYPE_BOOLEAN_ARRAY:
	  {
	    const unsigned char *value;
	    int len;

	    if (!dbus_dict_get_boolean_array (dict, keys[i], &value, &len))
	      goto error;

	    if (!_dbus_marshal_byte_array (str, byte_order, value, len))
	      goto error;
	    
	    break;
	  }	  
	case DBUS_TYPE_UINT32_ARRAY:
	  {
	    const dbus_uint32_t *value;
	    int len;

	    if (!dbus_dict_get_uint32_array (dict, keys[i], &value, &len))
	      goto error;

	    if (!_dbus_marshal_uint32_array (str, byte_order, value, len))
	      goto error;
	    
	    break;
	  }	  
	case DBUS_TYPE_DOUBLE_ARRAY:
	  {
	    const double *value;
	    int len;

	    if (!dbus_dict_get_double_array (dict, keys[i], &value, &len))
	      goto error;

	    if (!_dbus_marshal_double_array (str, byte_order, value, len))
	      goto error;
	    
	    break;
	  }
	case DBUS_TYPE_STRING_ARRAY:
	  {
	    const char **value;
	    int len;

	    if (!dbus_dict_get_string_array (dict, keys[i], &value, &len))
	      goto error;

	    if (!_dbus_marshal_string_array (str, byte_order, value, len))
	      goto error;
	    
	    break;
	  }
	default:
	  _dbus_warn ("unknwon value type %d\n", dbus_dict_get_value_type (dict, keys[i]));
	  _dbus_assert_not_reached ("unknown value type in dict");
	}
    }

  dbus_free_string_array (keys);

  return TRUE;
  
 error:
  
  dbus_free_string_array (keys);
  
  /* Restore previous length */
  _dbus_string_set_length (str, old_string_len);

  return FALSE;
}


/**
 * Demarshals a double.
 *
 * @param str the string containing the data
 * @param byte_order the byte order
 * @param pos the position in the string
 * @param new_pos the new position of the string
 * @returns the demarshaled double.
 */
double
_dbus_demarshal_double (const DBusString  *str,
			int          byte_order,
			int          pos,
			int         *new_pos)
{
  double retval;
  const char *buffer;

  pos = _DBUS_ALIGN_VALUE (pos, sizeof (double));

  _dbus_string_get_const_data_len (str, &buffer, pos, sizeof (double));

  retval = *(double *)buffer;
  
  if (byte_order != DBUS_COMPILER_BYTE_ORDER)
    swap_bytes ((unsigned char *)&retval, sizeof (double));

  if (new_pos)
    *new_pos = pos + sizeof (double);
  
  return retval;  
}

/**
 * Demarshals a 32 bit signed integer.
 *
 * @param str the string containing the data
 * @param byte_order the byte order
 * @param pos the position in the string
 * @param new_pos the new position of the string
 * @returns the demarshaled integer.
 */
dbus_int32_t
_dbus_demarshal_int32  (const DBusString *str,
			int               byte_order,
			int               pos,
			int              *new_pos)
{
  const DBusRealString *real = (const DBusRealString*) str;
  
  pos = _DBUS_ALIGN_VALUE (pos, sizeof (dbus_int32_t));
  
  if (new_pos)
    *new_pos = pos + sizeof (dbus_int32_t);

  if (byte_order == DBUS_LITTLE_ENDIAN)
    return DBUS_INT32_FROM_LE (*(dbus_int32_t*)(real->str + pos));
  else
    return DBUS_INT32_FROM_BE (*(dbus_int32_t*)(real->str + pos));
}

/**
 * Demarshals a 32 bit unsigned integer.
 *
 * @param str the string containing the data
 * @param byte_order the byte order
 * @param pos the position in the string
 * @param new_pos the new position of the string
 * @returns the demarshaled integer.
 */
dbus_uint32_t
_dbus_demarshal_uint32  (const DBusString *str,
			 int         byte_order,
			 int         pos,
			 int        *new_pos)
{
  const DBusRealString *real = (const DBusRealString*) str;
  
  pos = _DBUS_ALIGN_VALUE (pos, sizeof (dbus_uint32_t));
  
  if (new_pos)
    *new_pos = pos + sizeof (dbus_uint32_t);

  if (byte_order == DBUS_LITTLE_ENDIAN)
    return DBUS_UINT32_FROM_LE (*(dbus_uint32_t*)(real->str + pos));
  else
    return DBUS_UINT32_FROM_BE (*(dbus_uint32_t*)(real->str + pos));
}

/**
 * Demarshals an UTF-8 string.
 *
 * @todo Should we check the string to make sure
 * that it's  valid UTF-8, and maybe "fix" the string
 * if it's broken?
 *
 * @todo Should probably demarshal to a DBusString,
 * having memcpy() in here is Evil(tm).
 *
 * @param str the string containing the data
 * @param byte_order the byte order
 * @param pos the position in the string
 * @param new_pos the new position of the string
 * @returns the demarshaled string.
 */
char *
_dbus_demarshal_string (const DBusString *str,
			int         byte_order,
			int         pos,
			int        *new_pos)
{
  int len;
  char *retval;
  const char *data;
  
  len = _dbus_demarshal_uint32 (str, byte_order, pos, &pos);

  retval = dbus_malloc (len + 1);

  if (!retval)
    return NULL;

  _dbus_string_get_const_data_len (str, &data, pos, len);

  if (!data)
    return NULL;

  memcpy (retval, data, len + 1);

  if (new_pos)
    *new_pos = pos + len + 1;
  
  return retval;
}

/**
 * Demarshals a byte array.
 *
 * @todo Should probably demarshal to a DBusString,
 * having memcpy() in here is Evil(tm).
 *
 * @param str the string containing the data
 * @param byte_order the byte order
 * @param pos the position in the string
 * @param new_pos the new position of the string
 * @param array_len length of the demarshaled data
 * @returns the demarshaled data.
 */
unsigned char *
_dbus_demarshal_byte_array (const DBusString *str,
			    int         byte_order,
			    int         pos,
			    int        *new_pos,
			    int        *array_len)
{
  int len;
  unsigned char *retval;
  const char *data;

  len = _dbus_demarshal_uint32 (str, byte_order, pos, &pos);

  retval = dbus_malloc (len);

  if (!retval)
    return NULL;

  _dbus_string_get_const_data_len (str, &data, pos, len);

  if (!data)
    return NULL;

  memcpy (retval, data, len);

  if (new_pos)
    *new_pos = pos + len;

  if (array_len)
    *array_len = len;

  return retval;
}

/**
 * Demarshals a 32 bit signed integer array.
 *
 * @param str the string containing the data
 * @param byte_order the byte order
 * @param pos the position in the string
 * @param new_pos the new position of the string
 * @param array_len length of the demarshaled data
 * @returns the demarshaled data.
 */
dbus_int32_t *
_dbus_demarshal_int32_array (const DBusString *str,
			     int         byte_order,
			     int         pos,
			     int        *new_pos,
			     int        *array_len)
{
  int len, i;
  dbus_int32_t *retval;
  
  len = _dbus_demarshal_uint32 (str, byte_order, pos, &pos);

  retval = dbus_new (dbus_int32_t, len);

  if (!retval)
    return NULL;

  for (i = 0; i < len; i++)
    retval[i] = _dbus_demarshal_int32 (str, byte_order, pos, &pos);

  if (new_pos)
    *new_pos = pos;

  if (array_len)
    *array_len = len;
  
  return retval;
}

/**
 * Demarshals a 32 bit unsigned integer array.
 *
 * @param str the string containing the data
 * @param byte_order the byte order
 * @param pos the position in the string
 * @param new_pos the new position of the string
 * @param array_len length of the demarshaled data
 * @returns the demarshaled data.
 */
dbus_uint32_t *
_dbus_demarshal_uint32_array (const DBusString *str,
			      int         byte_order,
			      int         pos,
			      int        *new_pos,
			      int        *array_len)
{
  int len, i;
  dbus_uint32_t *retval;
  
  len = _dbus_demarshal_uint32 (str, byte_order, pos, &pos);

  retval = dbus_new (dbus_uint32_t, len);

  if (!retval)
    return NULL;

  for (i = 0; i < len; i++)
    retval[i] = _dbus_demarshal_uint32 (str, byte_order, pos, &pos);

  if (new_pos)
    *new_pos = pos;

  if (array_len)
    *array_len = len;
  
  return retval;  
}

/**
 * Demarshals a double array.
 *
 * @param str the string containing the data
 * @param byte_order the byte order
 * @param pos the position in the string
 * @param new_pos the new position of the string
 * @param array_len length of the demarshaled data
 * @returns the demarshaled data.
 */
double *
_dbus_demarshal_double_array (const DBusString *str,
			      int         byte_order,
			      int         pos,
			      int        *new_pos,
			      int        *array_len)
{
  int len, i;
  double *retval;
  
  len = _dbus_demarshal_uint32 (str, byte_order, pos, &pos);

  retval = dbus_new (double, len);

  if (!retval)
    return NULL;

  for (i = 0; i < len; i++)
    retval[i] = _dbus_demarshal_double (str, byte_order, pos, &pos);

  if (new_pos)
    *new_pos = pos;

  if (array_len)
    *array_len = len;
  
  return retval;  
}

/**
 * Demarshals a string array.
 *
 * @param str the string containing the data
 * @param byte_order the byte order
 * @param pos the position in the string
 * @param new_pos the new position of the string
 * @param array_len length of the demarshaled data
 * @returns the demarshaled data.
 */
char **
_dbus_demarshal_string_array (const DBusString *str,
			      int         byte_order,
			      int         pos,
			      int        *new_pos,
			      int        *array_len)
{
  int len, i, j;
  char **retval;

  len = _dbus_demarshal_uint32 (str, byte_order, pos, &pos);

  retval = dbus_new (char *, len + 1);

  if (!retval)
    return NULL;

  retval[len] = NULL;
  
  for (i = 0; i < len; i++)
    {
      retval[i] = _dbus_demarshal_string (str, byte_order, pos, &pos);

      if (retval[i] == 0)
	goto error;
    }

 if (new_pos)
    *new_pos = pos;

  if (array_len)
    *array_len = len;
  
  return retval;

 error:
  for (j = 0; j < i; j++)
    dbus_free (retval[i]);
  dbus_free (retval);

  return NULL;
}

/**
 * Demarshals a dict
 *
 * @param str the string containing the data
 * @param byte_order the byte order
 * @param pos the position in the string
 * @param new_pos the new position in the string
 * @returns the demarshalled dict
 */
DBusDict *
_dbus_demarshal_dict (const DBusString *str,
		      int               byte_order,
		      int               pos,
		      int              *new_pos)
{
  char **keys;
  int i, len;
  DBusDict *dict;
  
  dict = dbus_dict_new ();
  if (!dict)
    return NULL;

  keys = _dbus_demarshal_string_array (str, byte_order, pos, &pos, &len);
  
  if (!keys)
    goto error;

  for (i = 0; i < len; i++)
    {
      int value_type;
      
      switch ((value_type = _dbus_string_get_byte (str, pos ++)))
	{
	case DBUS_TYPE_BOOLEAN:
	  {
	    dbus_bool_t value;

	    value = _dbus_string_get_byte (str, pos ++);

	    if (!dbus_dict_set_boolean (dict, keys[i], value))
	      goto error;
	    break;
	  }
	case DBUS_TYPE_INT32:
	  {
	    dbus_int32_t value;

	    value = _dbus_demarshal_int32 (str, byte_order, pos, &pos);

	    if (!dbus_dict_set_int32 (dict, keys[i], value))
	      goto error;
	    
	    break;
	  }
	case DBUS_TYPE_UINT32:
	  {
	    dbus_uint32_t value;

	    value = _dbus_demarshal_uint32 (str, byte_order, pos, &pos);

	    if (!dbus_dict_set_uint32 (dict, keys[i], value))
	      goto error;
	    
	    break;
	  }
	case DBUS_TYPE_DOUBLE:
	  {
	    double value;

	    value = _dbus_demarshal_double (str, byte_order, pos, &pos);

	    if (!dbus_dict_set_double (dict, keys[i], value))
	      goto error;
	    
	    break;
	  }
	case DBUS_TYPE_STRING:
	  {
	    char *value;

	    value = _dbus_demarshal_string (str, byte_order, pos, &pos);

	    if (!value)
	      goto error;

	    if (!dbus_dict_set_string (dict, keys[i], value))
	      {
		dbus_free (value);
		goto error;
	      }

	    dbus_free (value);
	    
	    break;
	  }
	case DBUS_TYPE_BOOLEAN_ARRAY:
	  {
	    unsigned char *value;
	    int len;
	    
	    value = _dbus_demarshal_byte_array (str, byte_order, pos, &pos, &len);

	    if (!value)
	      goto error;

	    if (!dbus_dict_set_boolean_array (dict, keys[i], value, len))
	      {
		dbus_free (value);
		goto error;
	      }

	    dbus_free (value);
	    break;
	  }
	case DBUS_TYPE_INT32_ARRAY:
	  {
	    dbus_int32_t *value;
	    int len;
	    
	    value = _dbus_demarshal_int32_array (str, byte_order, pos, &pos, &len);

	    if (!value)
	      goto error;

	    if (!dbus_dict_set_int32_array (dict, keys[i], value, len))
	      {
		dbus_free (value);
		goto error;
	      }

	    dbus_free (value);
	    break;
	  }
	case DBUS_TYPE_UINT32_ARRAY:
	  {
	    dbus_uint32_t *value;
	    int len;
	    
	    value = _dbus_demarshal_uint32_array (str, byte_order, pos, &pos, &len);

	    if (!value)
	      goto error;

	    if (!dbus_dict_set_uint32_array (dict, keys[i], value, len))
	      {
		dbus_free (value);
		goto error;
	      }

	    dbus_free (value);
	    break;
	  }
	case DBUS_TYPE_DOUBLE_ARRAY:
	  {
	    double *value;
	    int len;
	    
	    value = _dbus_demarshal_double_array (str, byte_order, pos, &pos, &len);

	    if (!value)
	      goto error;

	    if (!dbus_dict_set_double_array (dict, keys[i], value, len))
	      {
		dbus_free (value);
		goto error;
	      }

	    dbus_free (value);
	    break;
	  }
	case DBUS_TYPE_STRING_ARRAY:
	  {
	    char **value;
	    int len;
	    
	    value = _dbus_demarshal_string_array (str, byte_order, pos, &pos, &len);

	    if (!value)
	      goto error;

	    if (!dbus_dict_set_string_array (dict, keys[i], value, len))
	      {
		dbus_free_string_array (value);
		goto error;
	      }

	    dbus_free_string_array (value);
	    break;
	  }
	default:
	  _dbus_warn ("unknown value type %d\n", value_type);
	  _dbus_assert_not_reached ("unknown value arg");
	}
    }
  
  dbus_free_string_array (keys);
  return dict;
  
 error:
  dbus_free_string_array (keys);
  dbus_dict_unref (dict);
  return NULL;
}

/** 
 * Returns the position right after the end of an argument.  PERFORMS
 * NO VALIDATION WHATSOEVER. The message must have been previously
 * validated.
 *
 * @param str a string
 * @param byte_order the byte order to use
 * @param pos the pos where the arg starts
 * @param end_pos pointer where the position right
 * after the end position will follow
 * @returns TRUE if more data exists after the arg
 */
dbus_bool_t
_dbus_marshal_get_arg_end_pos (const DBusString *str,
                               int               byte_order,
                               int               pos,
                               int              *end_pos)
{
  const char *data;

  if (pos >= _dbus_string_get_length (str))
    return FALSE;

  _dbus_string_get_const_data_len (str, &data, pos, 1);
  
  switch (*data)
    {
    case DBUS_TYPE_INVALID:
      return FALSE;
      break;

    case DBUS_TYPE_NIL:
      *end_pos = pos + 1;
      break;

    case DBUS_TYPE_BOOLEAN:
      *end_pos = pos + 2;
      break;
      
    case DBUS_TYPE_INT32:
      *end_pos = _DBUS_ALIGN_VALUE (pos + 1, sizeof (dbus_int32_t)) + sizeof (dbus_int32_t);

      break;

    case DBUS_TYPE_UINT32:
      *end_pos = _DBUS_ALIGN_VALUE (pos + 1, sizeof (dbus_uint32_t)) + sizeof (dbus_uint32_t);

      break;

    case DBUS_TYPE_DOUBLE:
      *end_pos = _DBUS_ALIGN_VALUE (pos + 1, sizeof (double)) + sizeof (double);

      break;

    case DBUS_TYPE_STRING:
      {
	int len;

	/* Demarshal the length */
	len = _dbus_demarshal_uint32 (str, byte_order, pos + 1, &pos);

	*end_pos = pos + len + 1;
      }
      break;

    case DBUS_TYPE_BOOLEAN_ARRAY:
    case DBUS_TYPE_BYTE_ARRAY:
      {
	int len;

	/* Demarshal the length */
	len = _dbus_demarshal_uint32 (str, byte_order, pos + 1, &pos);
	
	*end_pos = pos + len;
      }
      break;

    case DBUS_TYPE_INT32_ARRAY:
      {
	int len, new_pos;

	/* Demarshal the length */
	len = _dbus_demarshal_uint32 (str, byte_order, pos + 1, &new_pos);
	
	*end_pos = _DBUS_ALIGN_VALUE (new_pos, sizeof (dbus_int32_t))
	  + (len * sizeof (dbus_int32_t));
      }
      break;

    case DBUS_TYPE_UINT32_ARRAY:
      {
	int len, new_pos;

	/* Demarshal the length */
	len = _dbus_demarshal_uint32 (str, byte_order, pos + 1, &new_pos);

	*end_pos = _DBUS_ALIGN_VALUE (new_pos, sizeof (dbus_uint32_t))
	  + (len * sizeof (dbus_uint32_t));
      }
      break;

    case DBUS_TYPE_DOUBLE_ARRAY:
      {
	int len, new_pos;
	
	/* Demarshal the length */
	len = _dbus_demarshal_uint32 (str, byte_order, pos + 1, &new_pos);

	*end_pos = _DBUS_ALIGN_VALUE (new_pos, sizeof (double))
	  + (len * sizeof (double));
      }
      break;
      
    case DBUS_TYPE_STRING_ARRAY:
      {
	int len, i;
	
	/* Demarshal the length */
	len = _dbus_demarshal_uint32 (str, byte_order, pos + 1, &pos);

	for (i = 0; i < len; i++)
	  {
	    int str_len;
	    
	    /* Demarshal string length */
	    str_len = _dbus_demarshal_uint32 (str, byte_order, pos, &pos);
	    pos += str_len + 1;
	  }

	*end_pos = pos;
      }
      break;

    case DBUS_TYPE_DICT:
      {
	int len, i;

	/* Demarshal the length */
	len = _dbus_demarshal_uint32 (str, byte_order, pos + 1, &pos);

	for (i = 0; i < len; i++)
	  {
	    int str_len;
	    
	    /* Demarshal string length */
	    str_len = _dbus_demarshal_uint32 (str, byte_order, pos, &pos);
	    pos += str_len + 1;
	  }

	/* Now check the values */
	for (i = 0; i < len; i++)
	  {
	    if (!_dbus_marshal_get_arg_end_pos (str, byte_order, pos, &pos))
		return FALSE;
	  }

	*end_pos = pos;
	
	break;
      }
    default:
      _dbus_warn ("Unknown message arg type %d\n", *data);
      _dbus_assert_not_reached ("Unknown message argument type\n");
      return FALSE;
    }

  if (*end_pos > _dbus_string_get_length (str))
    return FALSE;
  
  return TRUE;
}

/**
 * Demarshals and validates a length; returns < 0 if the validation
 * fails. The length is required to be small enough that
 * len*sizeof(double) will not overflow, and small enough to fit in a
 * signed integer. DOES NOT check whether the length points
 * beyond the end of the string, because it doesn't know the
 * size of array elements.
 *
 * @param str the string
 * @param byte_order the byte order
 * @param pos the unaligned string position (snap to next aligned)
 * @param new_pos return location for new position.
 */
static int
demarshal_and_validate_len (const DBusString *str,
                            int               byte_order,
                            int               pos,
                            int              *new_pos)
{
  int align_4 = _DBUS_ALIGN_VALUE (pos, 4);
  unsigned int len;

  _dbus_assert (new_pos != NULL);
  
  if ((align_4 + 4) >= _dbus_string_get_length (str))
    {
      _dbus_verbose ("not enough room in message for array length\n");
      return -1;
    }
  
  if (!_dbus_string_validate_nul (str, pos,
                                  align_4 - pos))
    {
      _dbus_verbose ("array length alignment padding not initialized to nul\n");
      return -1;
    }

  len = _dbus_demarshal_uint32 (str, byte_order, align_4, new_pos);

  /* note that the len may be a number of doubles, so we need it to be
   * at least SIZE_T_MAX / 8, but make it smaller just to keep things
   * sane.  We end up using ints for most sizes to avoid unsigned mess
   * so limit to maximum 32-bit signed int divided by at least 8, more
   * for a bit of paranoia margin. INT_MAX/32 is about 65 megabytes.
   */  
#define MAX_ARRAY_LENGTH (((unsigned int)_DBUS_INT_MAX) / 32)
  if (len > MAX_ARRAY_LENGTH)
    {
      _dbus_verbose ("array length %u exceeds maximum of %u\n",
                     len, MAX_ARRAY_LENGTH);
      return -1;
    }
  else
    return (int) len;
}

static dbus_bool_t
validate_string (const DBusString *str,
                 int               pos,
                 int               len_without_nul,
                 int              *end_pos)
{
  *end_pos = pos + len_without_nul + 1;
  
  if (*end_pos > _dbus_string_get_length (str))
    {
      _dbus_verbose ("string length outside length of the message\n");
      return FALSE;
    }
  
  if (_dbus_string_get_byte (str, pos + len_without_nul) != '\0')
    {
      _dbus_verbose ("string arg not nul-terminated\n");
      return FALSE;
    }
  
  if (!_dbus_string_validate_utf8 (str, pos, len_without_nul))
    {
      _dbus_verbose ("string is not valid UTF-8\n");
      return FALSE;
    }

  return TRUE;
}   

/** 
 * Validates an argument, checking that it is well-formed, for example
 * no ludicrous length fields, strings are nul-terminated, etc.
 * Returns the end position of the argument in end_pos, and
 * returns #TRUE if a valid arg begins at "pos"
 *
 * @todo security: need to audit this function.
 * 
 * @param str a string
 * @param byte_order the byte order to use
 * @param pos the pos where the arg starts (offset of its typecode)
 * @param end_pos pointer where the position right
 * after the end position will follow
 * @returns #TRUE if the arg is valid.
 */
dbus_bool_t
_dbus_marshal_validate_arg (const DBusString *str,
                            int	              byte_order,
                            int               pos,
                            int              *end_pos)
{
  const char *data;

  if (pos >= _dbus_string_get_length (str))
    return FALSE;

  _dbus_string_get_const_data_len (str, &data, pos, 1);
  
  switch (*data)
    {
    case DBUS_TYPE_INVALID:
      return FALSE;
      break;

    case DBUS_TYPE_NIL:
      *end_pos = pos + 1;
      break;

    case DBUS_TYPE_BOOLEAN:
      {
	unsigned char c;

        if (2 > _dbus_string_get_length (str) - pos)
          {
            _dbus_verbose ("no room for boolean value\n");
            return FALSE;
          }
        
	c = _dbus_string_get_byte (str, pos + 1);

	if (c != 0 && c != 1)
	  {
	    _dbus_verbose ("boolean value must be either 0 or 1, not %d\n", c);
	    return FALSE;
	  }
	
      *end_pos = pos + 2;
      break;
      }
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT32:
      {
        int align_4 = _DBUS_ALIGN_VALUE (pos + 1, 4);
        
        if (!_dbus_string_validate_nul (str, pos + 1,
                                        align_4 - pos - 1))
          {
            _dbus_verbose ("int32/uint32 alignment padding not initialized to nul\n");
            return FALSE;
          }

        *end_pos = align_4 + 4;
      }
      break;

    case DBUS_TYPE_DOUBLE:
      {
        int align_8 = _DBUS_ALIGN_VALUE (pos + 1, 8);

        _dbus_verbose_bytes_of_string (str, pos, (align_8 + 8 - pos));
        
        if (!_dbus_string_validate_nul (str, pos + 1,
                                        align_8 - pos - 1))
          {
            _dbus_verbose ("double alignment padding not initialized to nul\n");
            return FALSE;
          }

        *end_pos = align_8 + 8;
      }
      break;

    case DBUS_TYPE_STRING:
      {
	int len;

	/* Demarshal the length, which does NOT include
         * nul termination
         */
	len = demarshal_and_validate_len (str, byte_order, pos + 1, &pos);
        if (len < 0)
          return FALSE;

        if (!validate_string (str, pos, len, end_pos))
          return FALSE;
      }
      break;

    case DBUS_TYPE_BOOLEAN_ARRAY:
      {
	int len, i;

	len = demarshal_and_validate_len (str, byte_order, pos + 1, &pos);
        if (len < 0)
          return FALSE;

        if (len > _dbus_string_get_length (str) - pos)
          {
            _dbus_verbose ("boolean array length outside length of the message\n");
            return FALSE;
          }
        
	i = 0;
	while (i < len)
	  {
	    unsigned char c = _dbus_string_get_byte (str, pos + i);

	    if (c != 0 && c != 1)
	      {
		_dbus_verbose ("boolean value must be either 0 or 1, not %d (pos %d)\n", c, pos);
		return FALSE;
	      }

	    i++;
	  }
	*end_pos = pos + len;
	break;
      }
    case DBUS_TYPE_BYTE_ARRAY:
      {
	int len;

	len = demarshal_and_validate_len (str, byte_order, pos + 1, &pos);
        if (len < 0)
          return FALSE;
	
	*end_pos = pos + len;
      }
      break;

    case DBUS_TYPE_INT32_ARRAY:
    case DBUS_TYPE_UINT32_ARRAY:
      {
	int len;

        len = demarshal_and_validate_len (str, byte_order, pos + 1, &pos);
        if (len < 0)
          return FALSE;

        _dbus_assert (_DBUS_ALIGN_VALUE (pos, 4) == (unsigned int) pos);
        
	*end_pos = pos + len * 4;
      }
      break;

    case DBUS_TYPE_DOUBLE_ARRAY:
      {
	int len;
        int align_8;

        len = demarshal_and_validate_len (str, byte_order, pos + 1, &pos);
        if (len < 0)
          return FALSE;

        align_8 = _DBUS_ALIGN_VALUE (pos, 8);
        if (!_dbus_string_validate_nul (str, pos,
                                        align_8 - pos))
          {
            _dbus_verbose ("double array alignment padding not initialized to nul\n");
            return FALSE;
          }
        
	*end_pos = align_8 + len * 8;
      }
      break;
      
    case DBUS_TYPE_STRING_ARRAY:
      {
        int len;
        int i;
        
        len = demarshal_and_validate_len (str, byte_order, pos + 1, &pos);
        if (len < 0)
          return FALSE;

	for (i = 0; i < len; i++)
	  {
	    int str_len;
	    
	    str_len = demarshal_and_validate_len (str, byte_order,
                                                  pos, &pos);
            if (str_len < 0)
              return FALSE;

            if (!validate_string (str, pos, str_len, &pos))
              return FALSE;            
	  }

	*end_pos = pos;
      }
      break;

      case DBUS_TYPE_DICT:
	{
	  int len;
	  int i;

	  len = demarshal_and_validate_len (str, byte_order, pos + 1, &pos);
	  if (len < 0)
	    return FALSE;
	  
	  for (i = 0; i < len; i++)
	    {
	      int str_len;
	      
	      str_len = demarshal_and_validate_len (str, byte_order,
						    pos, &pos);
	      if (str_len < 0)
		return FALSE;
	      
	      if (!validate_string (str, pos, str_len, &pos))
		return FALSE;            
	    }

	  /* Now validate each argument */
	  for (i = 0; i < len; i++)
	    {
	      if (pos >= _dbus_string_get_length (str))
		{
		  _dbus_verbose ("not enough values in dict\n");
		  return FALSE;
		}

	      if (_dbus_string_get_byte (str, pos) == DBUS_TYPE_NIL)
		{
		  _dbus_verbose ("can't have NIL values in dicts\n");
		  return FALSE;
		}
	      
	      if (!_dbus_marshal_validate_arg (str, byte_order, pos, &pos))
		return FALSE;
	    }

	  *end_pos = pos;

	  break;
	}
	
    default:
      _dbus_verbose ("Unknown message arg type %d\n", *data);
      return FALSE;
    }

  if (*end_pos > _dbus_string_get_length (str))
    return FALSE;
  
  return TRUE;
}


/**
 * If in verbose mode, print a block of binary data.
 *
 * @todo right now it prints even if not in verbose mode
 * 
 * @param data the data
 * @param len the length of the data
 */
void
_dbus_verbose_bytes (const unsigned char *data,
                     int                  len)
{
  int i;
  const unsigned char *aligned;

  _dbus_assert (len >= 0);
  
  /* Print blanks on first row if appropriate */
  aligned = _DBUS_ALIGN_ADDRESS (data, 4);
  if (aligned > data)
    aligned -= 4;
  _dbus_assert (aligned <= data);

  if (aligned != data)
    {
      _dbus_verbose ("%4d\t%p: ", - (data - aligned), aligned); 
      while (aligned != data)
        {
          _dbus_verbose ("    ");
          ++aligned;
        }
    }

  /* now print the bytes */
  i = 0;
  while (i < len)
    {
      if (_DBUS_ALIGN_ADDRESS (&data[i], 4) == &data[i])
        {
          _dbus_verbose ("%4d\t%p: ",
                   i, &data[i]);
        }
      
      if (data[i] >= 32 &&
          data[i] <= 126)
        _dbus_verbose (" '%c' ", data[i]);
      else
        _dbus_verbose ("0x%s%x ",
                 data[i] <= 0xf ? "0" : "", data[i]);

      ++i;

      if (_DBUS_ALIGN_ADDRESS (&data[i], 4) == &data[i])
        {
          if (i > 3)
            _dbus_verbose ("BE: %d LE: %d",
                           _dbus_unpack_uint32 (DBUS_BIG_ENDIAN, &data[i-4]),
                           _dbus_unpack_uint32 (DBUS_LITTLE_ENDIAN, &data[i-4]));

          if (i > 7 && 
              _DBUS_ALIGN_ADDRESS (&data[i], 8) == &data[i])
            {
              _dbus_verbose (" dbl: %g",
                             *(double*)&data[i-8]);
            }
          
          _dbus_verbose ("\n");
        }
    }

  _dbus_verbose ("\n");
}

/**
 * Dump the given part of the string to verbose log.
 *
 * @param str the string
 * @param start the start of range to dump
 * @param len length of range
 */
void
_dbus_verbose_bytes_of_string (const DBusString    *str,
                               int                  start,
                               int                  len)
{
  const char *d;
  int real_len;

  real_len = _dbus_string_get_length (str);

  _dbus_assert (start >= 0);
  
  if (start > real_len)
    {
      _dbus_verbose ("  [%d,%d) is not inside string of length %d\n",
                     start, len, real_len);
      return;
    }

  if ((start + len) > real_len)
    {
      _dbus_verbose ("  [%d,%d) extends outside string of length %d\n",
                     start, len, real_len);
      len = real_len - start;
    }
  
  
  _dbus_string_get_const_data_len (str, &d, start, len);

  _dbus_verbose_bytes (d, len);
}

/** @} */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>

dbus_bool_t
_dbus_marshal_test (void)
{
  DBusString str;
  char *tmp1, *tmp2;
  dbus_int32_t array1[3] = { 0x123, 0x456, 0x789 }, *array2;
  int pos = 0, i, len;
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
  DBusDict *dict;
  
  if (!_dbus_string_init (&str, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("failed to init string");

  /* Marshal doubles */
  if (!_dbus_marshal_double (&str, DBUS_BIG_ENDIAN, 3.14))
    _dbus_assert_not_reached ("could not marshal double value");
  if (!_dbus_demarshal_double (&str, DBUS_BIG_ENDIAN, pos, &pos) == 3.14)
    _dbus_assert_not_reached ("demarshal failed");

  if (!_dbus_marshal_double (&str, DBUS_LITTLE_ENDIAN, 3.14))
    _dbus_assert_not_reached ("could not marshal double value");
  if (!_dbus_demarshal_double (&str, DBUS_LITTLE_ENDIAN, pos, &pos) == 3.14)
    _dbus_assert_not_reached ("demarshal failed");
  
  /* Marshal signed integers */
  if (!_dbus_marshal_int32 (&str, DBUS_BIG_ENDIAN, -12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  if (!_dbus_demarshal_int32 (&str, DBUS_BIG_ENDIAN, pos, &pos) == -12345678)
    _dbus_assert_not_reached ("demarshal failed");

  if (!_dbus_marshal_int32 (&str, DBUS_LITTLE_ENDIAN, -12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  if (!_dbus_demarshal_int32 (&str, DBUS_LITTLE_ENDIAN, pos, &pos) == -12345678)
    _dbus_assert_not_reached ("demarshal failed");
  
  /* Marshal unsigned integers */
  if (!_dbus_marshal_uint32 (&str, DBUS_BIG_ENDIAN, 0x12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  if (!_dbus_demarshal_uint32 (&str, DBUS_BIG_ENDIAN, pos, &pos) == 0x12345678)
    _dbus_assert_not_reached ("demarshal failed");
  
  if (!_dbus_marshal_uint32 (&str, DBUS_LITTLE_ENDIAN, 0x12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  if (!_dbus_demarshal_uint32 (&str, DBUS_LITTLE_ENDIAN, pos, &pos) == 0x12345678)
    _dbus_assert_not_reached ("demarshal failed");

  /* Marshal strings */
  tmp1 = "This is the dbus test string";
  if (!_dbus_marshal_string (&str, DBUS_BIG_ENDIAN, tmp1))
    _dbus_assert_not_reached ("could not marshal string");
  tmp2 = _dbus_demarshal_string (&str, DBUS_BIG_ENDIAN, pos, &pos);
  if (!strcmp (tmp1, tmp2) == 0)
    _dbus_assert_not_reached ("demarshal failed");
  dbus_free (tmp2);

  tmp1 = "This is the dbus test string";
  if (!_dbus_marshal_string (&str, DBUS_LITTLE_ENDIAN, tmp1))
    _dbus_assert_not_reached ("could not marshal string");
  tmp2 = _dbus_demarshal_string (&str, DBUS_LITTLE_ENDIAN, pos, &pos);
  if (!strcmp (tmp1, tmp2) == 0)
    _dbus_assert_not_reached ("demarshal failed");
  dbus_free (tmp2);

  /* Marshal signed integer arrays */
  if (!_dbus_marshal_int32_array (&str, DBUS_BIG_ENDIAN, array1, 3))
    _dbus_assert_not_reached ("could not marshal integer array");
  array2 = _dbus_demarshal_int32_array (&str, DBUS_BIG_ENDIAN, pos, &pos, &len);

  if (len != 3)
    _dbus_assert_not_reached ("Signed integer array lengths differ!\n");
  dbus_free (array2);
  

  /* Marshal dicts */
  dict = dbus_dict_new ();

  if (dbus_dict_get_value_type (dict, "foo") != DBUS_TYPE_NIL)
    _dbus_assert_not_reached ("didn't return DBUS_TYPE_NIL for non-existant entry");
  
  if (!dbus_dict_set_boolean (dict, "boolean", TRUE))
    _dbus_assert_not_reached ("could not add boolean value");

  if (!dbus_dict_set_int32 (dict, "int32", 0x12345678))
    _dbus_assert_not_reached ("could not add int32 value");

  if (!dbus_dict_set_uint32 (dict, "uint32", 0x87654321))
    _dbus_assert_not_reached ("could not add uint32 value");

  if (!dbus_dict_set_double (dict, "double", 3.14159))
    _dbus_assert_not_reached ("could not add double value");

  if (!dbus_dict_set_string (dict, "string", "test string"))
    _dbus_assert_not_reached ("could not add string value");

  if (!dbus_dict_set_boolean_array (dict, "boolean_array", boolean_array, 4))
    _dbus_assert_not_reached ("could not add boolean array");

  if (!dbus_dict_set_int32_array (dict, "int32_array", int32_array, 5))
    _dbus_assert_not_reached ("could not add int32 array");

  if (!dbus_dict_set_uint32_array (dict, "uint32_array", uint32_array, 5))
    _dbus_assert_not_reached ("could not add uint32 array");

  if (!dbus_dict_set_double_array (dict, "double_array", double_array, 3))
    _dbus_assert_not_reached ("could not add double array");

  if (!dbus_dict_set_string_array (dict, "string_array", string_array, 4))
    _dbus_assert_not_reached ("could not add string array");

  if (!_dbus_marshal_dict (&str, DBUS_BIG_ENDIAN, dict))
    _dbus_assert_not_reached ("could not marshal dict");
  
  dbus_dict_unref (dict);
  
  dict = _dbus_demarshal_dict (&str, DBUS_BIG_ENDIAN, pos, &pos);

  if (!dbus_dict_get_boolean (dict, "boolean", &our_bool) ||
      !our_bool)
    _dbus_assert_not_reached ("could not get boolean value");

  if (!dbus_dict_get_int32 (dict, "int32", &our_int) || our_int != 0x12345678)
    _dbus_assert_not_reached ("could not get int32 value or int32 values differ");
  
  if (!dbus_dict_get_uint32 (dict, "uint32", &our_uint) || our_uint != 0x87654321)
    _dbus_assert_not_reached ("could not get uint32 value or uint32 values differ");

  if (!dbus_dict_get_double (dict, "double", &our_double)
      || our_double != 3.14159)
     _dbus_assert_not_reached ("could not get double value or double values differ");

  if (!dbus_dict_get_string (dict, "string", &our_string) || strcmp (our_string, "test string") != 0)
    _dbus_assert_not_reached ("could not get string value or string values differ");

  if (!dbus_dict_get_boolean_array (dict, "boolean_array", &our_boolean_array, &len) ||
      len != 4 || memcmp (boolean_array, our_boolean_array, 4) != 0)
    _dbus_assert_not_reached ("could not get boolean array value or boolean array values differ");

  if (!dbus_dict_get_int32_array (dict, "int32_array", &our_int32_array, &len) ||
      len != 5 || memcmp (int32_array, our_int32_array, 5 * sizeof (dbus_int32_t)) != 0)
    _dbus_assert_not_reached ("could not get int32 array value or int32 array values differ");

  if (!dbus_dict_get_uint32_array (dict, "uint32_array", &our_uint32_array, &len) ||
      len != 5 || memcmp (uint32_array, our_uint32_array, 5 * sizeof (dbus_uint32_t) ) != 0)
    _dbus_assert_not_reached ("could not get uint32 array value or uint32 array values differ");

  if (!dbus_dict_get_double_array (dict, "double_array", &our_double_array, &len) ||
      len != 3 || memcmp (double_array, our_double_array, 3 * sizeof (double)) != 0)
    _dbus_assert_not_reached ("could not get double array value or double array values differ");

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
  
  _dbus_string_free (&str);
  
      
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
