/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message.c  DBusMessage object
 *
 * Copyright (C) 2002, 2003  Red Hat Inc.
 * Copyright (C) 2002, 2003  CodeFactory AB
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

#include "dbus-internals.h"
#include "dbus-marshal.h"
#include "dbus-message.h"
#include "dbus-message-internal.h"
#include "dbus-memory.h"
#include "dbus-list.h"
#include <string.h>

/**
 * @defgroup DBusMessageInternals DBusMessage implementation details
 * @ingroup DBusInternals
 * @brief DBusMessage private implementation details.
 *
 * The guts of DBusMessage and its methods.
 *
 * @{
 */

/**
 * @brief Internals of DBusMessage
 * 
 * Object representing a message received from or to be sent to
 * another application. This is an opaque object, all members
 * are private.
 */
struct DBusMessage
{
  int refcount; /**< Reference count */

  DBusString header; /**< Header network data, stored
                      * separately from body so we can
                      * independently realloc it.
                      */

  DBusString body;   /**< Body network data. */

  char byte_order; /**< Message byte order. */
  
  char *name; /**< Message name. */
  char *service; /**< Message destination service. */
  char *sender; /**< Message sender service. */
  
  dbus_int32_t client_serial; /**< Client serial or -1 if not set */
  dbus_int32_t reply_serial; /**< Reply serial or -1 if not set */

  DBusCounter *size_counter; /**< Counter for the size of the message, or #NULL */
  long size_counter_delta;   /**< Size we incremented the size counter by. */
  
  unsigned int locked : 1; /**< Message being sent, no modifications allowed. */
};

/**
 * @brief Internals of DBusMessageIter
 * 
 * Object representing a position in a message. All fields are internal.
 */
struct DBusMessageIter
{
  int refcount; /**< Reference count */

  int pos; /**< Current position in the string */
  
  DBusMessage *message; /**< Message used */
};

/**
 * Gets the data to be sent over the network for this message.
 * The header and then the body should be written out.
 * This function is guaranteed to always return the same
 * data once a message is locked (with _dbus_message_lock()).
 *
 * @param message the message.
 * @param header return location for message header data.
 * @param body return location for message body data.
 */
void
_dbus_message_get_network_data (DBusMessage          *message,
                                const DBusString    **header,
                                const DBusString    **body)
{
  _dbus_assert (message->locked);
  
  *header = &message->header;
  *body = &message->body;
}

/**
 * Sets the client serial of a message. 
 * This can only be done once on a message.
 *
 * @param message the message
 * @param client_serial the client serial
 */
void
_dbus_message_set_client_serial (DBusMessage  *message,
				 dbus_int32_t  client_serial)
{
  _dbus_assert (message->client_serial == -1);
  
  message->client_serial = client_serial;
}

/**
 * Returns the serial that the message is
 * a reply to.
 *
 * @param message the message
 * @returns the reply serial
 */
dbus_int32_t
_dbus_message_get_reply_serial  (DBusMessage *message)
{
  return message->reply_serial;
}

/**
 * Adds a counter to be incremented immediately with the
 * size of this message, and decremented by the size
 * of this message when this message if finalized.
 *
 * @param message the message
 * @param counter the counter
 */
void
_dbus_message_add_size_counter (DBusMessage *message,
                                DBusCounter *counter)
{
  _dbus_assert (message->size_counter == NULL); /* If this fails we may need to keep a list of
                                                 * counters instead of just one
                                                 */

  message->size_counter = counter;
  _dbus_counter_ref (message->size_counter);

  /* When we can change message size, we may want to
   * update this each time we do so, or we may want to
   * just KISS like this.
   */
  message->size_counter_delta =
    _dbus_string_get_length (&message->header) +
    _dbus_string_get_length (&message->body);

  _dbus_verbose ("message has size %ld\n",
                 message->size_counter_delta);
  
  _dbus_counter_adjust (message->size_counter, message->size_counter_delta);
}

static void
dbus_message_write_header (DBusMessage *message)
{
  char *len_data;
  
  _dbus_assert (message->client_serial != -1);
  
  _dbus_string_append_byte (&message->header, DBUS_COMPILER_BYTE_ORDER);
  _dbus_string_append_len (&message->header, "\0\0\0", 3);

  /* We just lengthen the string here and pack in the real length later */
  _dbus_string_lengthen (&message->header, 4);
  
  _dbus_marshal_int32 (&message->header, DBUS_COMPILER_BYTE_ORDER,
                       _dbus_string_get_length (&message->body));

  /* Marshal client serial */
  _dbus_assert (message->client_serial >= 0);
  _dbus_marshal_int32 (&message->header, DBUS_COMPILER_BYTE_ORDER,
                       message->client_serial);

  /* Marshal message service */
  if (message->service)
    {
      _dbus_string_align_length (&message->header, 4);
      _dbus_string_append_len (&message->header, DBUS_HEADER_FIELD_SERVICE, 4);
      _dbus_string_append_byte (&message->header, DBUS_TYPE_STRING);
      
      _dbus_marshal_string (&message->header, DBUS_COMPILER_BYTE_ORDER,
                            message->service);
    }

  /* Marshal message name */
  _dbus_assert (message->name != NULL);
  _dbus_string_align_length (&message->header, 4);
  _dbus_string_append_len (&message->header, DBUS_HEADER_FIELD_NAME, 4);
  _dbus_string_append_byte (&message->header, DBUS_TYPE_STRING);

  _dbus_marshal_string (&message->header, DBUS_COMPILER_BYTE_ORDER, message->name);

  /* Marshal reply serial */
  if (message->reply_serial != -1)
    {
      _dbus_string_align_length (&message->header, 4);
      _dbus_string_append_len (&message->header, DBUS_HEADER_FIELD_REPLY, 4);

      _dbus_string_append_byte (&message->header, DBUS_TYPE_INT32);
      _dbus_marshal_int32 (&message->header, DBUS_COMPILER_BYTE_ORDER,
                           message->reply_serial);
    }

  /* Marshal sender */
  if (message->sender)
    {
      _dbus_string_align_length (&message->header, 4);
      _dbus_string_append_len (&message->header, DBUS_HEADER_FIELD_SENDER, 4);
      _dbus_string_append_byte (&message->header, DBUS_TYPE_STRING);

      _dbus_marshal_string (&message->header, DBUS_COMPILER_BYTE_ORDER,
			    message->sender);
    }
  
  /* Fill in the length */
  _dbus_string_get_data_len (&message->header, &len_data, 4, 4);
  _dbus_pack_int32 (_dbus_string_get_length (&message->header),
                    DBUS_COMPILER_BYTE_ORDER, len_data);
}

/**
 * Unlocks a message so that it can be re-sent to another client.
 *
 * @see _dbus_message_lock
 * @param message the message to unlock.
 */
void
_dbus_message_unlock (DBusMessage *message)
{
  if (!message->locked)
    return;
  
  /* Restore header */
  _dbus_string_set_length (&message->header, 0);

  message->client_serial = -1;
  message->locked = FALSE;
}

/**
 * Locks a message. Allows checking that applications don't keep a
 * reference to a message in the outgoing queue and change it
 * underneath us. Messages are locked when they enter the outgoing
 * queue (dbus_connection_send_message()), and the library complains
 * if the message is modified while locked.
 *
 * @param message the message to lock.
 */
void
_dbus_message_lock (DBusMessage  *message)
{
  if (!message->locked)
    dbus_message_write_header (message);
  
  message->locked = TRUE;
}

/** @} */

/**
 * @defgroup DBusMessage DBusMessage
 * @ingroup  DBus
 * @brief Message to be sent or received over a DBusConnection.
 *
 * A DBusMessage is the most basic unit of communication over a
 * DBusConnection. A DBusConnection represents a stream of messages
 * received from a remote application, and a stream of messages
 * sent to a remote application.
 *
 * @{
 */

/**
 * @typedef DBusMessage
 *
 * Opaque data type representing a message received from or to be
 * sent to another application.
 */

/**
 * Constructs a new message. Returns #NULL if memory
 * can't be allocated for the message.
 *
 * @todo use DBusString internally to store service and name.
 *
 * @param service service that the message should be sent to
 * @param name name of the message
 * @returns a new DBusMessage, free with dbus_message_unref()
 * @see dbus_message_unref()
 */
DBusMessage*
dbus_message_new (const char *service,
		  const char *name)
{
  DBusMessage *message;

  message = dbus_new0 (DBusMessage, 1);
  if (message == NULL)
    return NULL;
  
  message->refcount = 1;
  message->byte_order = DBUS_COMPILER_BYTE_ORDER;

  message->service = _dbus_strdup (service);
  message->name = _dbus_strdup (name);
  
  message->client_serial = -1;
  message->reply_serial = -1;
  
  if (!_dbus_string_init (&message->header, _DBUS_INT_MAX))
    {
      dbus_free (message->service);
      dbus_free (message->name);
      dbus_free (message);
      return NULL;
    }

  if (!_dbus_string_init (&message->body, _DBUS_INT_MAX))
    {
      dbus_free (message->service);
      dbus_free (message->name);
      _dbus_string_free (&message->header);
      dbus_free (message);
      return NULL;
    }
  
  return message;
}

/**
 * Constructs a message that is a reply to some other
 * message. Returns #NULL if memory can't be allocated
 * for the message.
 *
 * @param name the name of the message
 * @param original_message the message which the created
 * message is a reply to.
 * @returns a new DBusMessage, free with dbus_message_unref()
 * @see dbus_message_new(), dbus_message_unref()
 */ 
DBusMessage*
dbus_message_new_reply (const char  *name,
			DBusMessage *original_message)
{
  DBusMessage *message;

  _dbus_assert (original_message->sender != NULL);
  
  message = dbus_message_new (original_message->sender, name);
  
  if (message == NULL)
    return NULL;

  message->reply_serial = original_message->client_serial;

  return message;
}


/**
 * Increments the reference count of a DBusMessage.
 *
 * @param message The message
 * @see dbus_message_unref
 */
void
dbus_message_ref (DBusMessage *message)
{
  _dbus_assert (message->refcount > 0);
  
  message->refcount += 1;
}

/**
 * Decrements the reference count of a DBusMessage.
 *
 * @param message The message
 * @see dbus_message_ref
 */
void
dbus_message_unref (DBusMessage *message)
{
  _dbus_assert (message->refcount > 0);

  message->refcount -= 1;
  if (message->refcount == 0)
    {
      if (message->size_counter != NULL)
        {
          _dbus_counter_adjust (message->size_counter,
                                - message->size_counter_delta);
          _dbus_counter_unref (message->size_counter);
        }
      
      _dbus_string_free (&message->header);
      _dbus_string_free (&message->body);

      dbus_free (message->service);
      dbus_free (message->name);
      dbus_free (message);
    }
}

/**
 * Gets the name of a message.
 *
 * @param message the message
 * @returns the message name (should not be freed)
 */
const char*
dbus_message_get_name (DBusMessage *message)
{
  return message->name;
}

/**
 * Gets the destination service of a message.
 *
 * @param message the message
 * @returns the message destination service (should not be freed)
 */
const char*
dbus_message_get_service (DBusMessage *message)
{
  return message->service;
}

/**
 * Appends fields to a message given a variable argument
 * list. The variable argument list should contain the type
 * of the field followed by the value to add.
 * The list is terminated with 0.
 *
 * @param message the message
 * @param first_field_type type of the first field
 * @param ... value of first field, list of additional type-value pairs
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_append_fields (DBusMessage *message,
                            int first_field_type,
			    ...)
{
  dbus_bool_t retval;
  va_list var_args;

  va_start (var_args, first_field_type);
  retval = dbus_message_append_fields_valist (message,
                                              first_field_type,
                                              var_args);
  va_end (var_args);

  return retval;
}

/**
 * This function takes a va_list for use by language bindings
 *
 * @see dbus_message_append_fields.  
 * @param message the message
 * @param first_field_type type of first field
 * @param var_args value of first field, then list of type/value pairs
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_append_fields_valist (DBusMessage *message,
                                   int          first_field_type,
				   va_list      var_args)
{
  int type, old_len;

  old_len = _dbus_string_get_length (&message->body);
  
  type = first_field_type;

  while (type != 0)
    {
      switch (type)
	{
	case DBUS_TYPE_INT32:
	  if (!dbus_message_append_int32 (message, va_arg (var_args, dbus_int32_t)))
	    goto enomem;
	  break;
	case DBUS_TYPE_UINT32:
	  if (!dbus_message_append_uint32 (message, va_arg (var_args, dbus_uint32_t)))
	    goto enomem;	    
	  break;
	case DBUS_TYPE_DOUBLE:
	  if (!dbus_message_append_double (message, va_arg (var_args, double)))
	    goto enomem;
	  break;
	case DBUS_TYPE_STRING:
	  if (!dbus_message_append_string (message, va_arg (var_args, const char *)))
	    goto enomem;
	  break;
	case DBUS_TYPE_BYTE_ARRAY:
	  {
	    int len;
	    unsigned char *data;

	    data = va_arg (var_args, unsigned char *);
	    len = va_arg (var_args, int);

	    if (!dbus_message_append_byte_array (message, data, len))
	      goto enomem;
	  }
          break;
	case DBUS_TYPE_STRING_ARRAY:
	  {
	    int len;
	    const char **data;
	    
	    data = va_arg (var_args, const char **);
	    len = va_arg (var_args, int);

	    if (!dbus_message_append_string_array (message, data, len))
	      goto enomem;
	  }
	  break;
	  
	default:
	  _dbus_warn ("Unknown field type %d\n", type);
	}

      type = va_arg (var_args, int);
    }

  return TRUE;

 enomem:
  _dbus_string_set_length (&message->body, old_len);
  return FALSE;
}

/**
 * Appends a 32 bit signed integer to the message.
 *
 * @param message the message
 * @param value the integer value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_append_int32 (DBusMessage  *message,
			   dbus_int32_t  value)
{
  _dbus_assert (!message->locked);

  if (!_dbus_string_append_byte (&message->body, DBUS_TYPE_INT32))
    {
      _dbus_string_shorten (&message->body, 1);
      return FALSE;
    }
  
  return _dbus_marshal_int32 (&message->body,
			      DBUS_COMPILER_BYTE_ORDER, value);
}

/**
 * Appends a 32 bit unsigned integer to the message.
 *
 * @param message the message
 * @param value the integer value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_append_uint32 (DBusMessage   *message,
			    dbus_uint32_t  value)
{
  _dbus_assert (!message->locked);

  if (!_dbus_string_append_byte (&message->body, DBUS_TYPE_UINT32))
    {
      _dbus_string_shorten (&message->body, 1);
      return FALSE;
    }
  
  return _dbus_marshal_uint32 (&message->body,
			       DBUS_COMPILER_BYTE_ORDER, value);
}

/**
 * Appends a double value to the message.
 *
 * @param message the message
 * @param value the double value
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_append_double (DBusMessage *message,
			    double       value)
{
  _dbus_assert (!message->locked);

  if (!_dbus_string_append_byte (&message->body, DBUS_TYPE_DOUBLE))
    {
      _dbus_string_shorten (&message->body, 1);
      return FALSE;
    }
  
  return _dbus_marshal_double (&message->body,
			       DBUS_COMPILER_BYTE_ORDER, value);
}

/**
 * Appends a UTF-8 string to the message.
 *
 * @param message the message
 * @param value the string
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_append_string (DBusMessage *message,
			    const char  *value)
{
  _dbus_assert (!message->locked);

  if (!_dbus_string_append_byte (&message->body, DBUS_TYPE_STRING))
    {
      _dbus_string_shorten (&message->body, 1);
      return FALSE;
    }
  
  return _dbus_marshal_string (&message->body,
			       DBUS_COMPILER_BYTE_ORDER, value);
}

/**
 * Appends a byte array to the message.
 *
 * @param message the message
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_append_byte_array (DBusMessage         *message,
				unsigned const char *value,
				int                 len)
{
  _dbus_assert (!message->locked);

  if (!_dbus_string_append_byte (&message->body, DBUS_TYPE_BYTE_ARRAY))
    {
      _dbus_string_shorten (&message->body, 1);
      return FALSE;
    }
  
  return _dbus_marshal_byte_array (&message->body,
				   DBUS_COMPILER_BYTE_ORDER, value, len);
}

/**
 * Appends a string array to the message.
 *
 * @param message the message
 * @param value the array
 * @param len the length of the array
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_append_string_array (DBusMessage *message,
				  const char **value,
				  int          len)
{
  _dbus_assert (!message->locked);

  if (!_dbus_string_append_byte (&message->body, DBUS_TYPE_STRING_ARRAY))
    {
      _dbus_string_shorten (&message->body, 1);
      return FALSE;
    }
  
  return _dbus_marshal_string_array (&message->body,
				     DBUS_COMPILER_BYTE_ORDER, value, len);
}

/**
 * Gets fields from a message given a variable argument list.
 * The variable argument list should contain the type of the
 * field followed by a pointer to where the value should be
 * stored. The list is terminated with 0.
 *
 * @param message the message
 * @param first_field_type the first field type
 * @param ... location for first field value, then list of type-location pairs
 * @returns result code
 */
DBusResultCode
dbus_message_get_fields (DBusMessage *message,
                         int          first_field_type,
			 ...)
{
  DBusResultCode retval;
  va_list var_args;

  _dbus_verbose_bytes_of_string (&message->header, 0,
				 _dbus_string_get_length (&message->header));
  va_start (var_args, first_field_type);
  retval = dbus_message_get_fields_valist (message, first_field_type, var_args);
  va_end (var_args);

  return retval;
}

/**
 * This function takes a va_list for use by language bindings
 *
 * @todo this function (or some lower-level non-convenience function)
 * needs better error handling; should allow the application to
 * distinguish between out of memory, and bad data from the remote
 * app. It also needs to not leak a bunch of args when it gets
 * to the arg that's bad, as that would be a security hole
 * (allow one app to force another to leak memory)
 *
 * @todo We need to free the field data when an error occurs.
 *
 * @see dbus_message_get_fields
 * @param message the message
 * @param first_field_type type of the first field
 * @param var_args return location for first field, followed by list of type/location pairs
 * @returns result code
 */
DBusResultCode
dbus_message_get_fields_valist (DBusMessage *message,
                                int          first_field_type,
				va_list      var_args)
{
  int spec_type, msg_type, i;
  DBusMessageIter *iter;

  iter = dbus_message_get_fields_iter (message);

  if (iter == NULL)
    return DBUS_RESULT_NO_MEMORY;
  
  spec_type = first_field_type;
  i = 0;
  
  while (spec_type != 0)
    {
      msg_type = dbus_message_iter_get_field_type (iter);      
      
      if (msg_type != spec_type)
	{
	  _dbus_verbose ("Field %d is specified to be of type \"%s\", but "
			 "is actually of type \"%s\"\n", i,
			 _dbus_type_to_string (spec_type),
			 _dbus_type_to_string (msg_type));
	  dbus_message_iter_unref (iter);

	  return DBUS_RESULT_INVALID_FIELDS;
	}

      switch (spec_type)
	{
	case DBUS_TYPE_INT32:
	  {
	    dbus_int32_t *ptr;

	    ptr = va_arg (var_args, dbus_int32_t *);

	    *ptr = dbus_message_iter_get_int32 (iter);
	    break;
	  }
	case DBUS_TYPE_UINT32:
	  {
	    dbus_uint32_t *ptr;

	    ptr = va_arg (var_args, dbus_uint32_t *);

	    *ptr = dbus_message_iter_get_uint32 (iter);
	    break;
	  }

	case DBUS_TYPE_DOUBLE:
	  {
	    double *ptr;

	    ptr = va_arg (var_args, double *);

	    *ptr = dbus_message_iter_get_double (iter);
	    break;
	  }

	case DBUS_TYPE_STRING:
	  {
	    char **ptr;

	    ptr = va_arg (var_args, char **);

	    *ptr = dbus_message_iter_get_string (iter);

	    if (!*ptr)
	      return DBUS_RESULT_NO_MEMORY;
	    
	    break;
	  }

	case DBUS_TYPE_BYTE_ARRAY:
	  {
	    unsigned char **ptr;
	    int *len;

	    ptr = va_arg (var_args, unsigned char **);
	    len = va_arg (var_args, int *);

	    *ptr = dbus_message_iter_get_byte_array (iter, len);

	    if (!*ptr)
	      return DBUS_RESULT_NO_MEMORY;
	    
	    break;
	  }
	case DBUS_TYPE_STRING_ARRAY:
	  {
	    char ***ptr;
	    int *len;

	    ptr = va_arg (var_args, char ***);
	    len = va_arg (var_args, int *);

	    *ptr = dbus_message_iter_get_string_array (iter, len);
	    
	    if (!*ptr)
	      return DBUS_RESULT_NO_MEMORY;
	    
	    break;
	  }
	default:	  
	  _dbus_warn ("Unknown field type %d\n", spec_type);
	}
      
      spec_type = va_arg (var_args, int);
      if (spec_type != 0 && !dbus_message_iter_next (iter))
	{
	  _dbus_verbose ("More fields than exist in the message were specified\n");

	  dbus_message_iter_unref (iter);  
	  return DBUS_RESULT_INVALID_FIELDS;
	}
      i++;
    }

  dbus_message_iter_unref (iter);
  return DBUS_RESULT_SUCCESS;
}

/**
 * Returns a DBusMessageIter representing the fields of the
 * message passed in.
 *
 * @todo IMO the message iter should follow the GtkTextIter pattern,
 * a static object with a "stamp" value used to detect invalid
 * iter uses (uninitialized or after changing the message).
 * ref/unref is kind of annoying to deal with, and slower too.
 * This implies not ref'ing the message from the iter.
 *
 * @param message the message
 * @returns a new iter.
 */
DBusMessageIter *
dbus_message_get_fields_iter (DBusMessage *message)
{
  DBusMessageIter *iter;
  
  iter = dbus_new (DBusMessageIter, 1);

  dbus_message_ref (message);
  
  iter->refcount = 1;
  iter->message = message;
  iter->pos = 0;

  return iter;
}

/**
 * Increments the reference count of a DBusMessageIter.
 *
 * @param iter the message iter
 * @see dbus_message_iter_unref
 */
void
dbus_message_iter_ref (DBusMessageIter *iter)
{
  _dbus_assert (iter->refcount > 0);
  
  iter->refcount += 1;
}

/**
 * Decrements the reference count of a DBusMessageIter.
 *
 * @param iter The message iter
 * @see dbus_message_iter_ref
 */
void
dbus_message_iter_unref (DBusMessageIter *iter)
{
  _dbus_assert (iter->refcount > 0);

  iter->refcount -= 1;

  if (iter->refcount == 0)
    {
      dbus_message_unref (iter->message);

      dbus_free (iter);
    }
}

/**
 * Checks if an iterator has any more fields.
 *
 * @param iter the message iter
 * @returns #TRUE if there are more fields
 * following
 */
dbus_bool_t
dbus_message_iter_has_next (DBusMessageIter *iter)
{
  int end_pos;
  
  if (!_dbus_marshal_get_field_end_pos (&iter->message->body, iter->message->byte_order,
					iter->pos, &end_pos))
    return FALSE;
  
  if (end_pos >= _dbus_string_get_length (&iter->message->body))
    return FALSE;
  
  return TRUE;  
}

/**
 * Moves the iterator to the next field.
 *
 * @param iter The message iter
 * @returns #TRUE if the iterator was moved to the next field
 */
dbus_bool_t
dbus_message_iter_next (DBusMessageIter *iter)
{
  int end_pos;
  
  if (!_dbus_marshal_get_field_end_pos (&iter->message->body, iter->message->byte_order,
					iter->pos, &end_pos))
    return FALSE;

  if (end_pos >= _dbus_string_get_length (&iter->message->body))
    return FALSE;

  iter->pos = end_pos;

  return TRUE;
}

/**
 * Returns the field type of the field that the
 * message iterator points at.
 *
 * @param iter the message iter
 * @returns the field type
 */
int
dbus_message_iter_get_field_type (DBusMessageIter *iter)
{
  const char *data;

  if (iter->pos >= _dbus_string_get_length (&iter->message->body))
    return DBUS_TYPE_INVALID;

  _dbus_string_get_const_data_len (&iter->message->body, &data, iter->pos, 1);

  if (*data > DBUS_TYPE_INVALID && *data <= DBUS_TYPE_STRING_ARRAY)
    return *data;

  return DBUS_TYPE_INVALID;
}

/**
 * Returns the string value that an iterator may point to.
 * Note that you need to check that the iterator points to
 * a string value before using this function.
 *
 * @see dbus_message_iter_get_field_type
 * @param iter the message iter
 * @returns the string
 */
char *
dbus_message_iter_get_string (DBusMessageIter *iter)
{
  _dbus_assert (dbus_message_iter_get_field_type (iter) == DBUS_TYPE_STRING);

  return _dbus_demarshal_string (&iter->message->body, iter->message->byte_order,
                                 iter->pos + 1, NULL);
}

/**
 * Returns the 32 bit signed integer value that an iterator may point to.
 * Note that you need to check that the iterator points to
 * a string value before using this function.
 *
 * @see dbus_message_iter_get_field_type
 * @param iter the message iter
 * @returns the integer
 */
int
dbus_message_iter_get_int32 (DBusMessageIter *iter)
{
  return _dbus_demarshal_int32 (&iter->message->body, iter->message->byte_order,
				iter->pos + 1, NULL);
}

/**
 * Returns the 32 bit unsigned integer value that an iterator may point to.
 * Note that you need to check that the iterator points to
 * a string value before using this function.
 *
 * @see dbus_message_iter_get_field_type
 * @param iter the message iter
 * @returns the integer
 */
int
dbus_message_iter_get_uint32 (DBusMessageIter *iter)
{
  return _dbus_demarshal_uint32 (&iter->message->body, iter->message->byte_order,
				 iter->pos + 1, NULL);
}

/**
 * Returns the double value that an iterator may point to.
 * Note that you need to check that the iterator points to
 * a string value before using this function.
 *
 * @see dbus_message_iter_get_field_type
 * @param iter the message iter
 * @returns the double
 */
double
dbus_message_iter_get_double (DBusMessageIter *iter)
{
  return _dbus_demarshal_double (&iter->message->body, iter->message->byte_order,
				 iter->pos + 1, NULL);
}

/**
 * Returns the byte array that the iterator may point to.
 * Note that you need to check that the iterator points
 * to a byte array prior to using this function.
 *
 * @todo this function should probably take "unsigned char **" as
 * an out param argument, and return boolean or result code.
 *
 * @param iter the iterator
 * @param len return location for length of byte array
 * @returns the byte array
 */
unsigned char *
dbus_message_iter_get_byte_array (DBusMessageIter *iter,
                                  int             *len)
{
  _dbus_assert (dbus_message_iter_get_field_type (iter) == DBUS_TYPE_BYTE_ARRAY);

  return _dbus_demarshal_byte_array (&iter->message->body, iter->message->byte_order,
				     iter->pos + 1, NULL, len);
}

/**
 * Returns the string array that the iterator may point to.
 * Note that you need to check that the iterator points
 * to a byte array prior to using this function.
 *
 * @todo this function should probably take "char **" as
 * an out param argument, and return boolean or result code.
 *
 * @param iter the iterator
 * @param len return location for length of byte array
 * @returns the byte array
 */
char **
dbus_message_iter_get_string_array (DBusMessageIter *iter,
				    int             *len)
{
  _dbus_assert (dbus_message_iter_get_field_type (iter) == DBUS_TYPE_STRING_ARRAY);

  return _dbus_demarshal_string_array (&iter->message->body, iter->message->byte_order,
				       iter->pos + 1, NULL, len);
}

/**
 * Sets the message sender. 
 *
 * @param message the message
 * @param sender the sender
 */
void
dbus_message_set_sender (DBusMessage  *message,
			  const char   *sender)
{
  _dbus_assert (!message->locked);  

  message->sender = _dbus_strdup (sender);
}

/** @} */

/**
 * @addtogroup DBusMessageInternals
 *
 * @{
 */
/**
 * @typedef DBusMessageLoader
 *
 * The DBusMessageLoader object encapsulates the process of converting
 * a byte stream into a series of DBusMessage. It buffers the incoming
 * bytes as efficiently as possible, and generates a queue of
 * messages. DBusMessageLoader is typically used as part of a
 * DBusTransport implementation. The DBusTransport then hands off
 * the loaded messages to a DBusConnection, making the messages
 * visible to the application.
 * 
 */

/**
 * Implementation details of DBusMessageLoader.
 * All members are private.
 */
struct DBusMessageLoader
{
  int refcount;        /**< Reference count. */

  DBusString data;     /**< Buffered data */
  
  DBusList *messages;  /**< Complete messages. */

  long max_message_size; /**< Maximum size of a message */
  
  unsigned int buffer_outstanding : 1; /**< Someone is using the buffer to read */

  unsigned int corrupted : 1; /**< We got broken data, and are no longer working */
};

/**
 * The initial buffer size of the message loader.
 * 
 * @todo this should be based on min header size plus some average
 * body size, or something. Or rather, the min header size only, if we
 * want to try to read only the header, store that in a DBusMessage,
 * then read only the body and store that, etc., depends on
 * how we optimize _dbus_message_loader_get_buffer() and what
 * the exact message format is.
 */
#define INITIAL_LOADER_DATA_LEN 32

/**
 * Creates a new message loader. Returns #NULL if memory can't
 * be allocated.
 *
 * @returns new loader, or #NULL.
 */
DBusMessageLoader*
_dbus_message_loader_new (void)
{
  DBusMessageLoader *loader;

  loader = dbus_new0 (DBusMessageLoader, 1);
  if (loader == NULL)
    return NULL;
  
  loader->refcount = 1;

  /* Try to cap message size at something that won't *totally* hose
   * the system if we have a couple of them.
   */
  loader->max_message_size = _DBUS_ONE_MEGABYTE * 32;
  
  if (!_dbus_string_init (&loader->data, _DBUS_INT_MAX))
    {
      dbus_free (loader);
      return NULL;
    }

  /* preallocate the buffer for speed, ignore failure */
  _dbus_string_set_length (&loader->data, INITIAL_LOADER_DATA_LEN);
  _dbus_string_set_length (&loader->data, 0);
  
  return loader;
}

/**
 * Increments the reference count of the loader.
 *
 * @param loader the loader.
 */
void
_dbus_message_loader_ref (DBusMessageLoader *loader)
{
  loader->refcount += 1;
}

/**
 * Decrements the reference count of the loader and finalizes the
 * loader when the count reaches zero.
 *
 * @param loader the loader.
 */
void
_dbus_message_loader_unref (DBusMessageLoader *loader)
{
  loader->refcount -= 1;
  if (loader->refcount == 0)
    {
      _dbus_list_foreach (&loader->messages,
                          (DBusForeachFunction) dbus_message_unref,
                          NULL);
      _dbus_list_clear (&loader->messages);
      _dbus_string_free (&loader->data);
      dbus_free (loader);
    }
}

/**
 * Gets the buffer to use for reading data from the network.  Network
 * data is read directly into an allocated buffer, which is then used
 * in the DBusMessage, to avoid as many extra memcpy's as possible.
 * The buffer must always be returned immediately using
 * _dbus_message_loader_return_buffer(), even if no bytes are
 * successfully read.
 *
 * @todo this function can be a lot more clever. For example
 * it can probably always return a buffer size to read exactly
 * the body of the next message, thus avoiding any memory wastage
 * or reallocs.
 * 
 * @param loader the message loader.
 * @param buffer the buffer
 */
void
_dbus_message_loader_get_buffer (DBusMessageLoader  *loader,
                                 DBusString        **buffer)
{
  _dbus_assert (!loader->buffer_outstanding);

  *buffer = &loader->data;
  
  loader->buffer_outstanding = TRUE;
}

/**
 * The smallest header size that can occur. 
 * (It won't be valid)
 */
#define DBUS_MINIMUM_HEADER_SIZE 16

/** Pack four characters as in "abcd" into a uint32 */
#define FOUR_CHARS_TO_UINT32(a, b, c, d)                \
                      ((((dbus_uint32_t)a) << 24) |     \
                       (((dbus_uint32_t)b) << 16) |     \
                       (((dbus_uint32_t)c) << 8)  |     \
                       ((dbus_uint32_t)d))

/** DBUS_HEADER_FIELD_NAME packed into a dbus_uint32_t */
#define DBUS_HEADER_FIELD_NAME_AS_UINT32    \
  FOUR_CHARS_TO_UINT32 ('n', 'a', 'm', 'e')

/** DBUS_HEADER_FIELD_SERVICE packed into a dbus_uint32_t */
#define DBUS_HEADER_FIELD_SERVICE_AS_UINT32 \
  FOUR_CHARS_TO_UINT32 ('s', 'r', 'v', 'c')

/** DBUS_HEADER_FIELD_REPLY packed into a dbus_uint32_t */
#define DBUS_HEADER_FIELD_REPLY_AS_UINT32   \
  FOUR_CHARS_TO_UINT32 ('r', 'p', 'l', 'y')

/** DBUS_HEADER_FIELD_SENDER Packed into a dbus_uint32_t */
#define DBUS_HEADER_FIELD_SENDER_AS_UINT32  \
  FOUR_CHARS_TO_UINT32 ('s', 'n', 'd', 'r')


/* FIXME should be using DBusString for the stuff we demarshal.  char*
 * evil. Also, out of memory handling here seems suboptimal.
 * Should probably report it as a distinct error from "corrupt message,"
 * so we can postpone parsing this message. Also, we aren't
 * checking for demarshal failure everywhere.
 */
static dbus_bool_t
decode_header_data (DBusString   *data,
		    int		  header_len,
		    int           byte_order,
		    dbus_int32_t *client_serial,
		    dbus_int32_t *reply_serial,
		    char        **service,
		    char        **name,
		    char        **sender)
{
  const char *field;
  int pos, new_pos;
  
  /* First demarshal the client serial */
  *client_serial = _dbus_demarshal_int32 (data, byte_order, 12, &pos);

  *reply_serial = -1;
  *service = NULL;
  *name = NULL;
  *sender = NULL;
  
  /* Now handle the fields */
  while (pos < header_len)
    {
      pos = _DBUS_ALIGN_VALUE (pos, 4);
      
      if ((pos + 4) > header_len)
        return FALSE;      
      
      _dbus_string_get_const_data_len (data, &field, pos, 4);
      pos += 4;

      _dbus_assert (_DBUS_ALIGN_ADDRESS (field, 4) == field);

      /* I believe FROM_BE is right, but if not we'll find out
       * I guess. ;-)
       */
      switch (DBUS_UINT32_FROM_BE (*(int*)field))
        {
        case DBUS_HEADER_FIELD_SERVICE_AS_UINT32:
          if (*service != NULL)
            {
              _dbus_verbose ("%s field provided twice\n",
                             DBUS_HEADER_FIELD_SERVICE);
              goto failed;
            }
          
	  *service = _dbus_demarshal_string (data, byte_order, pos + 1, &new_pos);
          /* FIXME check for demarshal failure SECURITY */
          break;
        case DBUS_HEADER_FIELD_NAME_AS_UINT32:
          if (*name != NULL)
            {
              _dbus_verbose ("%s field provided twice\n",
                             DBUS_HEADER_FIELD_NAME);
              goto failed;
            }
          
	  *name = _dbus_demarshal_string (data, byte_order, pos + 1, &new_pos);
          /* FIXME check for demarshal failure SECURITY */
          break;
	case DBUS_HEADER_FIELD_SENDER_AS_UINT32:
	  if (*sender != NULL)
	    {
	      _dbus_verbose ("%s field provided twice\n",
			     DBUS_HEADER_FIELD_NAME);
	      goto failed;
	    }

	  *sender = _dbus_demarshal_string (data, byte_order, pos + 1, &new_pos);
          /* FIXME check for demarshal failure SECURITY */
	  break;
	case DBUS_HEADER_FIELD_REPLY_AS_UINT32:
	  *reply_serial = _dbus_demarshal_int32 (data, byte_order, pos + 1, &new_pos);

	  break;
        default:
	  _dbus_verbose ("Ignoring an unknown header field: %c%c%c%c\n",
			 field[0], field[1], field[2], field[3]);
	  
	  if (!_dbus_marshal_get_field_end_pos (data, byte_order, pos, &new_pos))
            goto failed;
	}

      if (new_pos > header_len)
	return FALSE;
      
      pos = new_pos;
    }
  
  return TRUE;

 failed:
  dbus_free (*service);
  dbus_free (*name);
  return FALSE;
}

/**
 * Returns a buffer obtained from _dbus_message_loader_get_buffer(),
 * indicating to the loader how many bytes of the buffer were filled
 * in. This function must always be called, even if no bytes were
 * successfully read.
 *
 * @param loader the loader.
 * @param buffer the buffer.
 * @param bytes_read number of bytes that were read into the buffer.
 */
void
_dbus_message_loader_return_buffer (DBusMessageLoader  *loader,
                                    DBusString         *buffer,
                                    int                 bytes_read)
{
  _dbus_assert (loader->buffer_outstanding);
  _dbus_assert (buffer == &loader->data);

  loader->buffer_outstanding = FALSE;

  if (loader->corrupted)
    return;

  while (_dbus_string_get_length (&loader->data) >= 16)
    {
      DBusMessage *message;      
      const char *header_data;
      int byte_order, header_len, body_len;
      
      _dbus_string_get_const_data_len (&loader->data, &header_data, 0, 16);

      _dbus_assert (_DBUS_ALIGN_ADDRESS (header_data, 4) == header_data);
      
      byte_order = header_data[0];

      if (byte_order != DBUS_LITTLE_ENDIAN &&
	  byte_order != DBUS_BIG_ENDIAN)
	{
          _dbus_verbose ("Message with bad byte order '%c' received\n",
                         byte_order);
	  loader->corrupted = TRUE;
	  return;
	}

      header_len = _dbus_unpack_int32 (byte_order, header_data + 4);
      body_len = _dbus_unpack_int32 (byte_order, header_data + 8);

      if (header_len + body_len > loader->max_message_size)
	{
          _dbus_verbose ("Message claimed length header = %d body = %d exceeds max message length %d\n",
                         header_len, body_len, loader->max_message_size);
	  loader->corrupted = TRUE;
	  return;
	}

      if (_dbus_string_get_length (&loader->data) >= header_len + body_len)
	{
	  dbus_int32_t client_serial, reply_serial;
	  char *service, *name, *sender;

          /* FIXME right now if this doesn't have enough memory, the
           * loader becomes corrupted. Instead we should just not
           * parse this message for now.
           */
 	  if (!decode_header_data (&loader->data, header_len, byte_order,
				   &client_serial, &reply_serial, &service, &name, &sender))
	    {
	      loader->corrupted = TRUE;
	      return;
	    }

  	  message = dbus_message_new (service, name);
	  message->reply_serial = reply_serial;
	  message->client_serial = client_serial;
	  dbus_message_set_sender (message, sender);
	  
          dbus_free (service);
	  dbus_free (name);
	  dbus_free (sender);
	  
	  if (message == NULL)
            break; /* ugh, postpone this I guess. */
          	  
	  if (!_dbus_list_append (&loader->messages, message))
            {
              dbus_message_unref (message);
              break;
            }

          _dbus_assert (_dbus_string_get_length (&message->header) == 0);
          _dbus_assert (_dbus_string_get_length (&message->body) == 0);

	  if (!_dbus_string_move_len (&loader->data, 0, header_len, &message->header, 0))
            {
              _dbus_list_remove_last (&loader->messages, message);
              dbus_message_unref (message);
              break;
            }

	  if (!_dbus_string_move_len (&loader->data, 0, body_len, &message->body, 0))
            {
              dbus_bool_t result;

              /* put the header back, we'll try again later */
              result = _dbus_string_copy_len (&message->header, 0, header_len,
                                              &loader->data, 0);
              _dbus_assert (result); /* because DBusString never reallocs smaller */

              _dbus_list_remove_last (&loader->messages, message);
              dbus_message_unref (message);
              break;
            }

          _dbus_assert (_dbus_string_get_length (&message->header) == header_len);
          _dbus_assert (_dbus_string_get_length (&message->body) == body_len);
          
	  _dbus_verbose ("Loaded message %p\n", message);	  
	}
      else
	break;
    }
}

/**
 * Pops a loaded message (passing ownership of the message
 * to the caller). Returns #NULL if no messages have been
 * loaded.
 *
 * @param loader the loader.
 * @returns the next message, or #NULL if none.
 */
DBusMessage*
_dbus_message_loader_pop_message (DBusMessageLoader *loader)
{
  return _dbus_list_pop_first (&loader->messages);
}


/**
 * Checks whether the loader is confused due to bad data.
 * If messages are received that are invalid, the
 * loader gets confused and gives up permanently.
 * This state is called "corrupted."
 *
 * @param loader the loader
 * @returns #TRUE if the loader is hosed.
 */
dbus_bool_t
_dbus_message_loader_get_is_corrupted (DBusMessageLoader *loader)
{
  return loader->corrupted;
}

/**
 * Sets the maximum size message we allow.
 *
 * @param loader the loader
 * @param size the max message size in bytes
 */
void
_dbus_message_loader_set_max_message_size (DBusMessageLoader  *loader,
                                           long                size)
{
  loader->max_message_size = size;
}

/**
 * Gets the maximum allowed message size in bytes.
 *
 * @param loader the loader
 * @returns max size in bytes
 */
long
_dbus_message_loader_get_max_message_size (DBusMessageLoader  *loader)
{
  return loader->max_message_size;
}

/** @} */
#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>

static void
message_iter_test (DBusMessage *message)
{
  DBusMessageIter *iter;
  char *str;
  
  iter = dbus_message_get_fields_iter (message);

  /* String tests */
  if (dbus_message_iter_get_field_type (iter) != DBUS_TYPE_STRING)
    _dbus_assert_not_reached ("Field type isn't string");

  str = dbus_message_iter_get_string (iter);
  if (strcmp (str, "Test string") != 0)
    _dbus_assert_not_reached ("Strings differ");
  dbus_free (str);

  if (!dbus_message_iter_next (iter))
    _dbus_assert_not_reached ("Reached end of fields");

  /* Signed integer tests */
  if (dbus_message_iter_get_field_type (iter) != DBUS_TYPE_INT32)
    _dbus_assert_not_reached ("Field type isn't int32");

  if (dbus_message_iter_get_int32 (iter) != -0x12345678)
    _dbus_assert_not_reached ("Signed integers differ");

  if (!dbus_message_iter_next (iter))
    _dbus_assert_not_reached ("Reached end of fields");
  
  /* Unsigned integer tests */
  if (dbus_message_iter_get_field_type (iter) != DBUS_TYPE_UINT32)
    _dbus_assert_not_reached ("Field type isn't int32");

  if (dbus_message_iter_get_int32 (iter) != 0xedd1e)
    _dbus_assert_not_reached ("Unsigned integers differ");

  if (!dbus_message_iter_next (iter))
    _dbus_assert_not_reached ("Reached end of fields");

  /* Double tests */
  if (dbus_message_iter_get_field_type (iter) != DBUS_TYPE_DOUBLE)
    _dbus_assert_not_reached ("Field type isn't double");

  if (dbus_message_iter_get_double (iter) != 3.14159)
    _dbus_assert_not_reached ("Doubles differ");

  if (dbus_message_iter_next (iter))
    _dbus_assert_not_reached ("Didn't reach end of fields");
  
  dbus_message_iter_unref (iter);
}

/**
 * @ingroup DBusMessageInternals
 * Unit test for DBusMessage.
 *
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_message_test (void)
{
  DBusMessage *message;
  DBusMessageLoader *loader;
  int i;
  const char *data;
  dbus_int32_t our_int;
  char *our_str;
  double our_double;
  
  /* Test the vararg functions */
  message = dbus_message_new ("org.freedesktop.DBus.Test", "testMessage");
  message->client_serial = 1;
  dbus_message_append_fields (message,
			      DBUS_TYPE_INT32, -0x12345678,
			      DBUS_TYPE_STRING, "Test string",
			      DBUS_TYPE_DOUBLE, 3.14159,
			      0);

  if (dbus_message_get_fields (message,
			       DBUS_TYPE_INT32, &our_int,
			       DBUS_TYPE_STRING, &our_str,
			       DBUS_TYPE_DOUBLE, &our_double,
			       0) != DBUS_RESULT_SUCCESS)
    _dbus_assert_not_reached ("Could not get fields");

  if (our_int != -0x12345678)
    _dbus_assert_not_reached ("integers differ!");

  if (our_double != 3.14159)
    _dbus_assert_not_reached ("doubles differ!");

  if (strcmp (our_str, "Test string") != 0)
    _dbus_assert_not_reached ("strings differ!");
  
  message = dbus_message_new ("org.freedesktop.DBus.Test", "testMessage");
  message->client_serial = 1;
  message->reply_serial = 0x12345678;

  dbus_message_append_string (message, "Test string");
  dbus_message_append_int32 (message, -0x12345678);
  dbus_message_append_uint32 (message, 0xedd1e);
  dbus_message_append_double (message, 3.14159);

  message_iter_test (message);

  /* Message loader test */
  _dbus_message_lock (message);
  loader = _dbus_message_loader_new ();

  /* Write the header data one byte at a time */
  _dbus_string_get_const_data (&message->header, &data);
  for (i = 0; i < _dbus_string_get_length (&message->header); i++)
    {
      DBusString *buffer;

      _dbus_message_loader_get_buffer (loader, &buffer);
      _dbus_string_append_byte (buffer, data[i]);
      _dbus_message_loader_return_buffer (loader, buffer, 1);
    }

  /* Write the body data one byte at a time */
  _dbus_string_get_const_data (&message->body, &data);
  for (i = 0; i < _dbus_string_get_length (&message->body); i++)
    {
      DBusString *buffer;

      _dbus_message_loader_get_buffer (loader, &buffer);
      _dbus_string_append_byte (buffer, data[i]);
      _dbus_message_loader_return_buffer (loader, buffer, 1);
    }

  dbus_message_unref (message);

  /* Now pop back the message */
  if (_dbus_message_loader_get_is_corrupted (loader))
    _dbus_assert_not_reached ("message loader corrupted");
  
  message = _dbus_message_loader_pop_message (loader);
  if (!message)
    _dbus_assert_not_reached ("received a NULL message");

  if (message->reply_serial != 0x12345678)
    _dbus_assert_not_reached ("reply serial fields differ");
  
  message_iter_test (message);
  
  dbus_message_unref (message);
  _dbus_message_loader_unref (loader);

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
