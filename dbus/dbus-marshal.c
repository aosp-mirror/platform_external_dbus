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
    swap_bytes ((unsigned char *)&value, sizeof (dbus_int32_t));

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
    swap_bytes ((unsigned char *)&value, sizeof (dbus_uint32_t));

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
 * Demarshals a double.
 *
 * @param str the string containing the data
 * @param byte_order the byte order
 * @param pos the position in the string
 * @param new_pos the new position of the string
 * @returns the demarshaled double.
 */
double
_dbus_demarshal_double (DBusString  *str,
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
_dbus_demarshal_int32  (DBusString *str,
			int         byte_order,
			int         pos,
			int        *new_pos)
{
  const char *buffer;

  pos = _DBUS_ALIGN_VALUE (pos, sizeof (dbus_int32_t));
  
  _dbus_string_get_const_data_len (str, &buffer, pos, sizeof (dbus_int32_t));

  if (new_pos)
    *new_pos = pos + sizeof (dbus_int32_t);

  return _dbus_unpack_int32 (byte_order, buffer);
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
_dbus_demarshal_uint32  (DBusString *str,
			 int         byte_order,
			 int         pos,
			 int        *new_pos)
{
  const char *buffer;

  pos = _DBUS_ALIGN_VALUE (pos, sizeof (dbus_uint32_t));
  
  _dbus_string_get_const_data_len (str, &buffer, pos, sizeof (dbus_uint32_t));

  if (new_pos)
    *new_pos = pos + sizeof (dbus_uint32_t);

  return _dbus_unpack_uint32 (byte_order, buffer);
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
_dbus_demarshal_string (DBusString *str,
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
_dbus_demarshal_byte_array (DBusString *str,
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
_dbus_demarshal_int32_array (DBusString *str,
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
_dbus_demarshal_uint32_array (DBusString *str,
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
_dbus_demarshal_double_array (DBusString *str,
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
_dbus_demarshal_string_array (DBusString *str,
			      int         byte_order,
			      int         pos,
			      int        *new_pos,
			      int        *array_len)
{
  int len, i, j;
  char **retval;
  
  len = _dbus_demarshal_uint32 (str, byte_order, pos, &pos);
  
  retval = dbus_new (char *, len);

  if (!retval)
    return NULL;

  for (i = 0; i < len; i++)
    {
      retval[i] = _dbus_demarshal_string (str, byte_order, pos, &pos);

      if (retval[i] == 0)
	goto error;
    }

  return retval;

 error:
  for (j = 0; j < i; j++)
    dbus_free (retval[i]);
  dbus_free (retval);

  return NULL;
}

/** 
 * Returns the position right after the end position 
 * end position of a field
 *
 * @todo warns on invalid type in a message, but
 * probably the whole message needs to be dumped,
 * or we might even drop the connection due
 * to bad protocol. Needs better error handling.
 * Possible security issue.
 *
 * @param str a string
 * @param byte_order the byte order to use
 * @param pos the pos where the field starts
 * @param end_pos pointer where the position right
 * after the end position will follow
 * @returns TRUE if more data exists after the field
 */
dbus_bool_t
_dbus_marshal_get_field_end_pos (DBusString *str,
				 int	     byte_order,
				 int         pos,
				 int        *end_pos)
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

	break;
      }

    case DBUS_TYPE_BYTE_ARRAY:
      {
	int len;

	/* Demarshal the length */
	len = _dbus_demarshal_uint32 (str, byte_order, pos + 1, &pos);
	
	*end_pos = pos + len;

	break;
      }

    case DBUS_TYPE_INT32_ARRAY:
      {
	int len, new_pos;

	/* Demarshal the length */
	len = _dbus_demarshal_uint32 (str, byte_order, pos + 1, &new_pos);
	
	*end_pos = _DBUS_ALIGN_VALUE (new_pos, sizeof (dbus_int32_t))
	  + (len * sizeof (dbus_int32_t));

	break;
      }

    case DBUS_TYPE_UINT32_ARRAY:
      {
	int len, new_pos;

	/* Demarshal the length */
	len = _dbus_demarshal_uint32 (str, byte_order, pos + 1, &new_pos);

	*end_pos = _DBUS_ALIGN_VALUE (new_pos, sizeof (dbus_uint32_t))
	  + (len * sizeof (dbus_uint32_t));
	
	break;
      }

    case DBUS_TYPE_DOUBLE_ARRAY:
      {
	int len, new_pos;
	
	/* Demarshal the length */
	len = _dbus_demarshal_uint32 (str, byte_order, pos + 1, &new_pos);

	*end_pos = _DBUS_ALIGN_VALUE (new_pos, sizeof (double))
	  + (len * sizeof (double));
	
	break;
      }
      
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
	break;
      }      
    default:
      _dbus_warn ("Unknown message field type %d\n", *data);
      return FALSE;
    }

  if (*end_pos >= _dbus_string_get_length (str))
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

  /* Print blanks on first row if appropriate */
  aligned = _DBUS_ALIGN_ADDRESS (data, 4);
  if (aligned > data)
    aligned -= 4;
  _dbus_assert (aligned <= data);

  if (aligned != data)
    {
      _dbus_verbose ("%5d\t%p: ", - (data - aligned), aligned); 
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
          _dbus_verbose ("%5d\t%p: ",
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
            _dbus_verbose ("big: %d little: %d",
                           _dbus_unpack_uint32 (DBUS_BIG_ENDIAN, &data[i-4]),
                           _dbus_unpack_uint32 (DBUS_LITTLE_ENDIAN, &data[i-4]));
          
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
  int pos = 0, len;
  
  if (!_dbus_string_init (&str, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("failed to init string");

  /* Marshal doubles */
  if (!_dbus_marshal_double (&str, DBUS_BIG_ENDIAN, 3.14))
    _dbus_assert_not_reached ("could not marshal double value");
  _dbus_assert (_dbus_demarshal_double (&str, DBUS_BIG_ENDIAN, pos, &pos) == 3.14);

  if (!_dbus_marshal_double (&str, DBUS_LITTLE_ENDIAN, 3.14))
    _dbus_assert_not_reached ("could not marshal double value");
  _dbus_assert (_dbus_demarshal_double (&str, DBUS_LITTLE_ENDIAN, pos, &pos) == 3.14);
  
  /* Marshal signed integers */
  if (!_dbus_marshal_int32 (&str, DBUS_BIG_ENDIAN, -12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  _dbus_assert (_dbus_demarshal_int32 (&str, DBUS_BIG_ENDIAN, pos, &pos) == -12345678);

  if (!_dbus_marshal_int32 (&str, DBUS_LITTLE_ENDIAN, -12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  _dbus_assert (_dbus_demarshal_int32 (&str, DBUS_LITTLE_ENDIAN, pos, &pos) == -12345678);
  
  /* Marshal unsigned integers */
  if (!_dbus_marshal_uint32 (&str, DBUS_BIG_ENDIAN, 0x12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  _dbus_assert (_dbus_demarshal_uint32 (&str, DBUS_BIG_ENDIAN, pos, &pos) == 0x12345678);
  
  if (!_dbus_marshal_uint32 (&str, DBUS_LITTLE_ENDIAN, 0x12345678))
    _dbus_assert_not_reached ("could not marshal signed integer value");
  _dbus_assert (_dbus_demarshal_uint32 (&str, DBUS_LITTLE_ENDIAN, pos, &pos) == 0x12345678);

  /* Marshal strings */
  tmp1 = "This is the dbus test string";
  if (!_dbus_marshal_string (&str, DBUS_BIG_ENDIAN, tmp1))
    _dbus_assert_not_reached ("could not marshal string");
  tmp2 = _dbus_demarshal_string (&str, DBUS_BIG_ENDIAN, pos, &pos);
  _dbus_assert (strcmp (tmp1, tmp2) == 0);
  dbus_free (tmp2);

  tmp1 = "This is the dbus test string";
  if (!_dbus_marshal_string (&str, DBUS_LITTLE_ENDIAN, tmp1))
    _dbus_assert_not_reached ("could not marshal string");
  tmp2 = _dbus_demarshal_string (&str, DBUS_LITTLE_ENDIAN, pos, &pos);
  _dbus_assert (strcmp (tmp1, tmp2) == 0);
  dbus_free (tmp2);

  /* Marshal signed integer arrays */
  if (!_dbus_marshal_int32_array (&str, DBUS_BIG_ENDIAN, array1, 3))
    _dbus_assert_not_reached ("could not marshal integer array");
  _dbus_verbose_bytes_of_string (&str, 0, _dbus_string_get_length (&str));
  array2 = _dbus_demarshal_int32_array (&str, DBUS_BIG_ENDIAN, pos, &pos, &len);
  printf ("length is: %d\n", len);
  if (len != 3)
    _dbus_assert_not_reached ("Signed integer array lengths differ!\n");

  


  _dbus_string_free (&str);
  
      
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
