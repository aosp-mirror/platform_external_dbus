/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message.c  DBusMessage object
 *
 * Copyright (C) 2002, 2003, 2004, 2005  Red Hat Inc.
 * Copyright (C) 2002, 2003  CodeFactory AB
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

#include "dbus-internals.h"
#include "dbus-marshal-recursive.h"
#include "dbus-marshal-validate.h"
#include "dbus-marshal-header.h"
#include "dbus-message.h"
#include "dbus-message-internal.h"
#include "dbus-object-tree.h"
#include "dbus-memory.h"
#include "dbus-list.h"
#include "dbus-dataslot.h"
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

static dbus_bool_t dbus_message_iter_get_args        (DBusMessageIter *iter,
                                                      DBusError       *error,
                                                      int              first_arg_type,
                                                      ...);
static dbus_bool_t dbus_message_iter_get_args_valist (DBusMessageIter *iter,
                                                      DBusError       *error,
                                                      int              first_arg_type,
                                                      va_list          var_args);

/* Not thread locked, but strictly const/read-only so should be OK
 */
/** An static string representing an empty signature */
_DBUS_STRING_DEFINE_STATIC(_dbus_empty_signature_str,  "");

/** How many bits are in the changed_stamp used to validate iterators */
#define CHANGED_STAMP_BITS 21

/**
 * @brief Internals of DBusMessage
 *
 * Object representing a message received from or to be sent to
 * another application. This is an opaque object, all members
 * are private.
 */
struct DBusMessage
{
  DBusAtomic refcount; /**< Reference count */

  DBusHeader header; /**< Header network data and associated cache */

  DBusString body;   /**< Body network data. */

  char byte_order; /**< Message byte order. */

  unsigned int locked : 1; /**< Message being sent, no modifications allowed. */

  DBusList *size_counters;   /**< 0-N DBusCounter used to track message size. */
  long size_counter_delta;   /**< Size we incremented the size counters by.   */

  dbus_uint32_t changed_stamp : CHANGED_STAMP_BITS; /**< Incremented when iterators are invalidated. */

  DBusDataSlotList slot_list;   /**< Data stored by allocated integer ID */

#ifndef DBUS_DISABLE_CHECKS
  int generation; /**< _dbus_current_generation when message was created */
#endif
};

/* these have wacky values to help trap uninitialized iterators;
 * but has to fit in 3 bits
 */
enum {
  DBUS_MESSAGE_ITER_TYPE_READER = 3,
  DBUS_MESSAGE_ITER_TYPE_WRITER = 7
};

/** typedef for internals of message iterator */
typedef struct DBusMessageRealIter DBusMessageRealIter;

/**
 * @brief Internals of DBusMessageIter
 *
 * Object representing a position in a message. All fields are internal.
 */
struct DBusMessageRealIter
{
  DBusMessage *message; /**< Message used */
  dbus_uint32_t changed_stamp : CHANGED_STAMP_BITS; /**< stamp to detect invalid iters */
  dbus_uint32_t iter_type : 3;      /**< whether this is a reader or writer iter */
  dbus_uint32_t sig_refcount : 8;   /**< depth of open_signature() */
  union
  {
    DBusTypeWriter writer; /**< writer */
    DBusTypeReader reader; /**< reader */
  } u; /**< the type writer or reader that does all the work */
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

  *header = &message->header.data;
  *body = &message->body;
}

/**
 * Sets the serial number of a message.
 * This can only be done once on a message.
 *
 * @param message the message
 * @param serial the serial
 */
void
_dbus_message_set_serial (DBusMessage   *message,
                          dbus_uint32_t  serial)
{
  _dbus_return_if_fail (message != NULL);
  _dbus_return_if_fail (!message->locked);
  _dbus_return_if_fail (dbus_message_get_serial (message) == 0);

  _dbus_header_set_serial (&message->header, serial);
}

/**
 * Adds a counter to be incremented immediately with the
 * size of this message, and decremented by the size
 * of this message when this message if finalized.
 * The link contains a counter with its refcount already
 * incremented, but the counter itself not incremented.
 * Ownership of link and counter refcount is passed to
 * the message.
 *
 * @param message the message
 * @param link link with counter as data
 */
void
_dbus_message_add_size_counter_link (DBusMessage  *message,
                                     DBusList     *link)
{
  /* right now we don't recompute the delta when message
   * size changes, and that's OK for current purposes
   * I think, but could be important to change later.
   * Do recompute it whenever there are no outstanding counters,
   * since it's basically free.
   */
  if (message->size_counters == NULL)
    {
      message->size_counter_delta =
        _dbus_string_get_length (&message->header.data) +
        _dbus_string_get_length (&message->body);

#if 0
      _dbus_verbose ("message has size %ld\n",
                     message->size_counter_delta);
#endif
    }

  _dbus_list_append_link (&message->size_counters, link);

  _dbus_counter_adjust (link->data, message->size_counter_delta);
}

/**
 * Adds a counter to be incremented immediately with the
 * size of this message, and decremented by the size
 * of this message when this message if finalized.
 *
 * @param message the message
 * @param counter the counter
 * @returns #FALSE if no memory
 */
dbus_bool_t
_dbus_message_add_size_counter (DBusMessage *message,
                                DBusCounter *counter)
{
  DBusList *link;

  link = _dbus_list_alloc_link (counter);
  if (link == NULL)
    return FALSE;

  _dbus_counter_ref (counter);
  _dbus_message_add_size_counter_link (message, link);

  return TRUE;
}

/**
 * Removes a counter tracking the size of this message, and decrements
 * the counter by the size of this message.
 *
 * @param message the message
 * @param link_return return the link used
 * @param counter the counter
 */
void
_dbus_message_remove_size_counter (DBusMessage  *message,
                                   DBusCounter  *counter,
                                   DBusList    **link_return)
{
  DBusList *link;

  link = _dbus_list_find_last (&message->size_counters,
                               counter);
  _dbus_assert (link != NULL);

  _dbus_list_unlink (&message->size_counters,
                     link);
  if (link_return)
    *link_return = link;
  else
    _dbus_list_free_link (link);

  _dbus_counter_adjust (counter, - message->size_counter_delta);

  _dbus_counter_unref (counter);
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
    {
      _dbus_header_update_lengths (&message->header,
                                   _dbus_string_get_length (&message->body));

      /* must have a signature if you have a body */
      _dbus_assert (_dbus_string_get_length (&message->body) == 0 ||
                    dbus_message_get_signature (message) != NULL);

      message->locked = TRUE;
    }
}

static dbus_bool_t
set_or_delete_string_field (DBusMessage *message,
                            int          field,
                            int          typecode,
                            const char  *value)
{
  if (value == NULL)
    return _dbus_header_delete_field (&message->header, field);
  else
    return _dbus_header_set_field_basic (&message->header,
                                         field,
                                         typecode,
                                         &value);
}

static void
get_const_signature (DBusHeader        *header,
                     const DBusString **type_str_p,
                     int               *type_pos_p)
{
  if (_dbus_header_get_field_raw (header,
                                  DBUS_HEADER_FIELD_SIGNATURE,
                                  type_str_p,
                                  type_pos_p))
    {
      *type_pos_p += 1; /* skip the signature length which is 1 byte */
    }
  else
    {
      *type_str_p = &_dbus_empty_signature_str;
      *type_pos_p = 0;
    }
}

#if 0
/* Probably we don't need to use this */
/**
 * Sets the signature of the message, i.e. the arguments in the
 * message payload. The signature includes only "in" arguments for
 * #DBUS_MESSAGE_TYPE_METHOD_CALL and only "out" arguments for
 * #DBUS_MESSAGE_TYPE_METHOD_RETURN, so is slightly different from
 * what you might expect (it does not include the signature of the
 * entire C++-style method).
 *
 * The signature is a string made up of type codes such as
 * #DBUS_TYPE_INT32. The string is terminated with nul (nul is also
 * the value of #DBUS_TYPE_INVALID). The macros such as
 * #DBUS_TYPE_INT32 evaluate to integers; to assemble a signature you
 * may find it useful to use the string forms, such as
 * #DBUS_TYPE_INT32_AS_STRING.
 *
 * An "unset" or #NULL signature is considered the same as an empty
 * signature. In fact dbus_message_get_signature() will never return
 * #NULL.
 *
 * @param message the message
 * @param signature the type signature or #NULL to unset
 * @returns #FALSE if no memory
 */
static dbus_bool_t
_dbus_message_set_signature (DBusMessage *message,
                             const char  *signature)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);
  _dbus_return_val_if_fail (signature == NULL ||
                            _dbus_check_is_valid_signature (signature));
  /* can't delete the signature if you have a message body */
  _dbus_return_val_if_fail (_dbus_string_get_length (&message->body) == 0 ||
                            signature != NULL);

  return set_or_delete_string_field (message,
                                     DBUS_HEADER_FIELD_SIGNATURE,
                                     DBUS_TYPE_SIGNATURE,
                                     signature);
}
#endif

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
 * Returns the serial of a message or 0 if none has been specified.
 * The message's serial number is provided by the application sending
 * the message and is used to identify replies to this message.  All
 * messages received on a connection will have a serial, but messages
 * you haven't sent yet may return 0.
 *
 * @param message the message
 * @returns the client serial
 */
dbus_uint32_t
dbus_message_get_serial (DBusMessage *message)
{
  _dbus_return_val_if_fail (message != NULL, 0);

  return _dbus_header_get_serial (&message->header);
}

/**
 * Sets the reply serial of a message (the client serial
 * of the message this is a reply to).
 *
 * @param message the message
 * @param reply_serial the client serial
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_reply_serial (DBusMessage   *message,
                               dbus_uint32_t  reply_serial)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);

  return _dbus_header_set_field_basic (&message->header,
                                       DBUS_HEADER_FIELD_REPLY_SERIAL,
                                       DBUS_TYPE_UINT32,
                                       &reply_serial);
}

/**
 * Returns the serial that the message is a reply to or 0 if none.
 *
 * @param message the message
 * @returns the reply serial
 */
dbus_uint32_t
dbus_message_get_reply_serial  (DBusMessage *message)
{
  dbus_uint32_t v_UINT32;

  _dbus_return_val_if_fail (message != NULL, 0);

  if (_dbus_header_get_field_basic (&message->header,
                                    DBUS_HEADER_FIELD_REPLY_SERIAL,
                                    DBUS_TYPE_UINT32,
                                    &v_UINT32))
    return v_UINT32;
  else
    return 0;
}

static void
free_size_counter (void *element,
                   void *data)
{
  DBusCounter *counter = element;
  DBusMessage *message = data;

  _dbus_counter_adjust (counter, - message->size_counter_delta);

  _dbus_counter_unref (counter);
}

static void
dbus_message_finalize (DBusMessage *message)
{
  _dbus_assert (message->refcount.value == 0);

  /* This calls application callbacks! */
  _dbus_data_slot_list_free (&message->slot_list);

  _dbus_list_foreach (&message->size_counters,
                      free_size_counter, message);
  _dbus_list_clear (&message->size_counters);

  _dbus_header_free (&message->header);
  _dbus_string_free (&message->body);

  dbus_free (message);
}

/* Message Cache
 *
 * We cache some DBusMessage to reduce the overhead of allocating
 * them.  In my profiling this consistently made about an 8%
 * difference.  It avoids the malloc for the message, the malloc for
 * the slot list, the malloc for the header string and body string,
 * and the associated free() calls. It does introduce another global
 * lock which could be a performance issue in certain cases.
 *
 * For the echo client/server the round trip time goes from around
 * .000077 to .000069 with the message cache on my laptop. The sysprof
 * change is as follows (numbers are cumulative percentage):
 *
 *  with message cache implemented as array as it is now (0.000069 per):
 *    new_empty_header           1.46
 *      mutex_lock               0.56    # i.e. _DBUS_LOCK(message_cache)
 *      mutex_unlock             0.25
 *      self                     0.41
 *    unref                      2.24
 *      self                     0.68
 *      list_clear               0.43
 *      mutex_lock               0.33    # i.e. _DBUS_LOCK(message_cache)
 *      mutex_unlock             0.25
 *
 *  with message cache implemented as list (0.000070 per roundtrip):
 *    new_empty_header           2.72
 *      list_pop_first           1.88
 *    unref                      3.3
 *      list_prepend             1.63
 *
 * without cache (0.000077 per roundtrip):
 *    new_empty_header           6.7
 *      string_init_preallocated 3.43
 *        dbus_malloc            2.43
 *      dbus_malloc0             2.59
 *
 *    unref                      4.02
 *      string_free              1.82
 *        dbus_free              1.63
 *      dbus_free                0.71
 *
 * If you implement the message_cache with a list, the primary reason
 * it's slower is that you add another thread lock (on the DBusList
 * mempool).
 */

/** Avoid caching huge messages */
#define MAX_MESSAGE_SIZE_TO_CACHE _DBUS_ONE_MEGABYTE

/** Avoid caching too many messages */
#define MAX_MESSAGE_CACHE_SIZE    5

_DBUS_DEFINE_GLOBAL_LOCK (message_cache);
static DBusMessage *message_cache[MAX_MESSAGE_CACHE_SIZE];
static int message_cache_count = 0;
static dbus_bool_t message_cache_shutdown_registered = FALSE;

static void
dbus_message_cache_shutdown (void *data)
{
  int i;

  _DBUS_LOCK (message_cache);

  i = 0;
  while (i < MAX_MESSAGE_CACHE_SIZE)
    {
      if (message_cache[i])
        dbus_message_finalize (message_cache[i]);

      ++i;
    }

  message_cache_count = 0;
  message_cache_shutdown_registered = FALSE;

  _DBUS_UNLOCK (message_cache);
}

/**
 * Tries to get a message from the message cache.  The retrieved
 * message will have junk in it, so it still needs to be cleared out
 * in dbus_message_new_empty_header()
 *
 * @returns the message, or #NULL if none cached
 */
static DBusMessage*
dbus_message_get_cached (void)
{
  DBusMessage *message;
  int i;

  message = NULL;

  _DBUS_LOCK (message_cache);

  _dbus_assert (message_cache_count >= 0);

  if (message_cache_count == 0)
    {
      _DBUS_UNLOCK (message_cache);
      return NULL;
    }

  /* This is not necessarily true unless count > 0, and
   * message_cache is uninitialized until the shutdown is
   * registered
   */
  _dbus_assert (message_cache_shutdown_registered);

  i = 0;
  while (i < MAX_MESSAGE_CACHE_SIZE)
    {
      if (message_cache[i])
        {
          message = message_cache[i];
          message_cache[i] = NULL;
          message_cache_count -= 1;
          break;
        }
      ++i;
    }
  _dbus_assert (message_cache_count >= 0);
  _dbus_assert (i < MAX_MESSAGE_CACHE_SIZE);
  _dbus_assert (message != NULL);

  _DBUS_UNLOCK (message_cache);

  _dbus_assert (message->refcount.value == 0);
  _dbus_assert (message->size_counters == NULL);

  return message;
}

/**
 * Tries to cache a message, otherwise finalize it.
 *
 * @param message the message
 */
static void
dbus_message_cache_or_finalize (DBusMessage *message)
{
  dbus_bool_t was_cached;
  int i;

  _dbus_assert (message->refcount.value == 0);

  /* This calls application code and has to be done first thing
   * without holding the lock
   */
  _dbus_data_slot_list_clear (&message->slot_list);

  _dbus_list_foreach (&message->size_counters,
                      free_size_counter, message);
  _dbus_list_clear (&message->size_counters);

  was_cached = FALSE;

  _DBUS_LOCK (message_cache);

  if (!message_cache_shutdown_registered)
    {
      _dbus_assert (message_cache_count == 0);

      if (!_dbus_register_shutdown_func (dbus_message_cache_shutdown, NULL))
        goto out;

      i = 0;
      while (i < MAX_MESSAGE_CACHE_SIZE)
        {
          message_cache[i] = NULL;
          ++i;
        }

      message_cache_shutdown_registered = TRUE;
    }

  _dbus_assert (message_cache_count >= 0);

  if ((_dbus_string_get_length (&message->header.data) +
       _dbus_string_get_length (&message->body)) >
      MAX_MESSAGE_SIZE_TO_CACHE)
    goto out;

  if (message_cache_count >= MAX_MESSAGE_CACHE_SIZE)
    goto out;

  /* Find empty slot */
  i = 0;
  while (message_cache[i] != NULL)
    ++i;

  _dbus_assert (i < MAX_MESSAGE_CACHE_SIZE);

  _dbus_assert (message_cache[i] == NULL);
  message_cache[i] = message;
  message_cache_count += 1;
  was_cached = TRUE;

 out:
  _DBUS_UNLOCK (message_cache);

  if (!was_cached)
    dbus_message_finalize (message);
}

static DBusMessage*
dbus_message_new_empty_header (void)
{
  DBusMessage *message;
  dbus_bool_t from_cache;

  message = dbus_message_get_cached ();

  if (message != NULL)
    {
      from_cache = TRUE;
    }
  else
    {
      from_cache = FALSE;
      message = dbus_new (DBusMessage, 1);
      if (message == NULL)
        return NULL;
#ifndef DBUS_DISABLE_CHECKS
      message->generation = _dbus_current_generation;
#endif
    }

  message->refcount.value = 1;
  message->byte_order = DBUS_COMPILER_BYTE_ORDER;
  message->locked = FALSE;
  message->size_counters = NULL;
  message->size_counter_delta = 0;
  message->changed_stamp = 0;

  if (!from_cache)
    _dbus_data_slot_list_init (&message->slot_list);

  if (from_cache)
    {
      _dbus_header_reinit (&message->header, message->byte_order);
      _dbus_string_set_length (&message->body, 0);
    }
  else
    {
      if (!_dbus_header_init (&message->header, message->byte_order))
        {
          dbus_free (message);
          return NULL;
        }

      if (!_dbus_string_init_preallocated (&message->body, 32))
        {
          _dbus_header_free (&message->header);
          dbus_free (message);
          return NULL;
        }
    }

  return message;
}

/**
 * Constructs a new message of the given message type.
 * Types include #DBUS_MESSAGE_TYPE_METHOD_CALL,
 * #DBUS_MESSAGE_TYPE_SIGNAL, and so forth.
 *
 * @param message_type type of message
 * @returns new message or #NULL If no memory
 */
DBusMessage*
dbus_message_new (int message_type)
{
  DBusMessage *message;

  _dbus_return_val_if_fail (message_type != DBUS_MESSAGE_TYPE_INVALID, NULL);

  message = dbus_message_new_empty_header ();
  if (message == NULL)
    return NULL;

  if (!_dbus_header_create (&message->header,
                            message_type,
                            NULL, NULL, NULL, NULL, NULL))
    {
      dbus_message_unref (message);
      return NULL;
    }

  return message;
}

/**
 * Constructs a new message to invoke a method on a remote
 * object. Returns #NULL if memory can't be allocated for the
 * message. The destination may be #NULL in which case no destination
 * is set; this is appropriate when using D-BUS in a peer-to-peer
 * context (no message bus). The interface may be #NULL, which means
 * that if multiple methods with the given name exist it is undefined
 * which one will be invoked.
  *
 * @param destination service that the message should be sent to or #NULL
 * @param path object path the message should be sent to
 * @param interface interface to invoke method on
 * @param method method to invoke
 *
 * @returns a new DBusMessage, free with dbus_message_unref()
 * @see dbus_message_unref()
 */
DBusMessage*
dbus_message_new_method_call (const char *destination,
                              const char *path,
                              const char *interface,
                              const char *method)
{
  DBusMessage *message;

  _dbus_return_val_if_fail (path != NULL, NULL);
  _dbus_return_val_if_fail (method != NULL, NULL);
  _dbus_return_val_if_fail (destination == NULL ||
                            _dbus_check_is_valid_service (destination), NULL);
  _dbus_return_val_if_fail (_dbus_check_is_valid_path (path), NULL);
  _dbus_return_val_if_fail (interface == NULL ||
                            _dbus_check_is_valid_interface (interface), NULL);
  _dbus_return_val_if_fail (_dbus_check_is_valid_member (method), NULL);

  message = dbus_message_new_empty_header ();
  if (message == NULL)
    return NULL;

  if (!_dbus_header_create (&message->header,
                            DBUS_MESSAGE_TYPE_METHOD_CALL,
                            destination, path, interface, method, NULL))
    {
      dbus_message_unref (message);
      return NULL;
    }

  return message;
}

/**
 * Constructs a message that is a reply to a method call. Returns
 * #NULL if memory can't be allocated for the message.
 *
 * @param method_call the message which the created
 * message is a reply to.
 * @returns a new DBusMessage, free with dbus_message_unref()
 * @see dbus_message_new_method_call(), dbus_message_unref()
 */
DBusMessage*
dbus_message_new_method_return (DBusMessage *method_call)
{
  DBusMessage *message;
  const char *sender;

  _dbus_return_val_if_fail (method_call != NULL, NULL);

  sender = dbus_message_get_sender (method_call);

  /* sender is allowed to be null here in peer-to-peer case */

  message = dbus_message_new_empty_header ();
  if (message == NULL)
    return NULL;

  if (!_dbus_header_create (&message->header,
                            DBUS_MESSAGE_TYPE_METHOD_RETURN,
                            sender, NULL, NULL, NULL, NULL))
    {
      dbus_message_unref (message);
      return NULL;
    }

  dbus_message_set_no_reply (message, TRUE);

  if (!dbus_message_set_reply_serial (message,
                                      dbus_message_get_serial (method_call)))
    {
      dbus_message_unref (message);
      return NULL;
    }

  return message;
}

/**
 * Constructs a new message representing a signal emission. Returns
 * #NULL if memory can't be allocated for the message.  A signal is
 * identified by its originating interface, and the name of the
 * signal.
 *
 * @param path the path to the object emitting the signal
 * @param interface the interface the signal is emitted from
 * @param name name of the signal
 * @returns a new DBusMessage, free with dbus_message_unref()
 * @see dbus_message_unref()
 */
DBusMessage*
dbus_message_new_signal (const char *path,
                         const char *interface,
                         const char *name)
{
  DBusMessage *message;

  _dbus_return_val_if_fail (path != NULL, NULL);
  _dbus_return_val_if_fail (interface != NULL, NULL);
  _dbus_return_val_if_fail (name != NULL, NULL);
  _dbus_return_val_if_fail (_dbus_check_is_valid_path (path), NULL);
  _dbus_return_val_if_fail (_dbus_check_is_valid_interface (interface), NULL);
  _dbus_return_val_if_fail (_dbus_check_is_valid_member (name), NULL);

  message = dbus_message_new_empty_header ();
  if (message == NULL)
    return NULL;

  if (!_dbus_header_create (&message->header,
                            DBUS_MESSAGE_TYPE_SIGNAL,
                            NULL, path, interface, name, NULL))
    {
      dbus_message_unref (message);
      return NULL;
    }

  dbus_message_set_no_reply (message, TRUE);

  return message;
}

/**
 * Creates a new message that is an error reply to a certain message.
 * Error replies are possible in response to method calls primarily.
 *
 * @param reply_to the original message
 * @param error_name the error name
 * @param error_message the error message string or #NULL for none
 * @returns a new error message
 */
DBusMessage*
dbus_message_new_error (DBusMessage *reply_to,
                        const char  *error_name,
                        const char  *error_message)
{
  DBusMessage *message;
  const char *sender;
  DBusMessageIter iter;

  _dbus_return_val_if_fail (reply_to != NULL, NULL);
  _dbus_return_val_if_fail (error_name != NULL, NULL);
  _dbus_return_val_if_fail (_dbus_check_is_valid_error_name (error_name), NULL);

  sender = dbus_message_get_sender (reply_to);

  /* sender may be NULL for non-message-bus case or
   * when the message bus is dealing with an unregistered
   * connection.
   */
  message = dbus_message_new_empty_header ();
  if (message == NULL)
    return NULL;

  if (!_dbus_header_create (&message->header,
                            DBUS_MESSAGE_TYPE_ERROR,
                            sender, NULL, NULL, NULL, error_name))
    {
      dbus_message_unref (message);
      return NULL;
    }

  dbus_message_set_no_reply (message, TRUE);

  if (!dbus_message_set_reply_serial (message,
                                      dbus_message_get_serial (reply_to)))
    {
      dbus_message_unref (message);
      return NULL;
    }

  if (error_message != NULL)
    {
      dbus_message_iter_init_append (message, &iter);
      if (!dbus_message_iter_append_basic (&iter,
                                           DBUS_TYPE_STRING,
                                           &error_message))
        {
          dbus_message_unref (message);
          return NULL;
        }
    }

  return message;
}

/**
 * Creates a new message that is an error reply to a certain message.
 * Error replies are possible in response to method calls primarily.
 *
 * @param reply_to the original message
 * @param error_name the error name
 * @param error_format the error message format as with printf
 * @param ... format string arguments
 * @returns a new error message
 */
DBusMessage*
dbus_message_new_error_printf (DBusMessage *reply_to,
			       const char  *error_name,
			       const char  *error_format,
			       ...)
{
  va_list args;
  DBusString str;
  DBusMessage *message;

  _dbus_return_val_if_fail (reply_to != NULL, NULL);
  _dbus_return_val_if_fail (error_name != NULL, NULL);
  _dbus_return_val_if_fail (_dbus_check_is_valid_error_name (error_name), NULL);

  if (!_dbus_string_init (&str))
    return NULL;

  va_start (args, error_format);

  if (_dbus_string_append_printf_valist (&str, error_format, args))
    message = dbus_message_new_error (reply_to, error_name,
				      _dbus_string_get_const_data (&str));
  else
    message = NULL;

  _dbus_string_free (&str);

  va_end (args);

  return message;
}


/**
 * Creates a new message that is an exact replica of the message
 * specified, except that its refcount is set to 1, its message serial
 * is reset to 0, and if the original message was "locked" (in the
 * outgoing message queue and thus not modifiable) the new message
 * will not be locked.
 *
 * @param message the message.
 * @returns the new message.
 */
DBusMessage *
dbus_message_copy (const DBusMessage *message)
{
  DBusMessage *retval;

  _dbus_return_val_if_fail (message != NULL, NULL);

  retval = dbus_new0 (DBusMessage, 1);
  if (retval == NULL)
    return NULL;

  retval->refcount.value = 1;
  retval->byte_order = message->byte_order;
  retval->locked = FALSE;
#ifndef DBUS_DISABLE_CHECKS
  retval->generation = message->generation;
#endif

  if (!_dbus_header_copy (&message->header, &retval->header))
    {
      dbus_free (retval);
      return NULL;
    }

  if (!_dbus_string_init_preallocated (&retval->body,
                                       _dbus_string_get_length (&message->body)))
    {
      _dbus_header_free (&retval->header);
      dbus_free (retval);
      return NULL;
    }

  if (!_dbus_string_copy (&message->body, 0,
			  &retval->body, 0))
    goto failed_copy;

  return retval;

 failed_copy:
  _dbus_header_free (&retval->header);
  _dbus_string_free (&retval->body);
  dbus_free (retval);

  return NULL;
}


/**
 * Increments the reference count of a DBusMessage.
 *
 * @param message The message
 * @returns the message
 * @see dbus_message_unref
 */
DBusMessage *
dbus_message_ref (DBusMessage *message)
{
  dbus_int32_t old_refcount;

  _dbus_return_val_if_fail (message != NULL, NULL);
  _dbus_return_val_if_fail (message->generation == _dbus_current_generation, NULL);

  old_refcount = _dbus_atomic_inc (&message->refcount);
  _dbus_assert (old_refcount >= 1);

  return message;
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
 dbus_int32_t old_refcount;

  _dbus_return_if_fail (message != NULL);
  _dbus_return_if_fail (message->generation == _dbus_current_generation);

  old_refcount = _dbus_atomic_dec (&message->refcount);

  _dbus_assert (old_refcount >= 0);

  if (old_refcount == 1)
    {
      /* Calls application callbacks! */
      dbus_message_cache_or_finalize (message);
    }
}

/**
 * Gets the type of a message. Types include
 * #DBUS_MESSAGE_TYPE_METHOD_CALL, #DBUS_MESSAGE_TYPE_METHOD_RETURN,
 * #DBUS_MESSAGE_TYPE_ERROR, #DBUS_MESSAGE_TYPE_SIGNAL, but other
 * types are allowed and all code must silently ignore messages of
 * unknown type. DBUS_MESSAGE_TYPE_INVALID will never be returned,
 * however.
 *
 *
 * @param message the message
 * @returns the type of the message
 */
int
dbus_message_get_type (DBusMessage *message)
{
  _dbus_return_val_if_fail (message != NULL, DBUS_MESSAGE_TYPE_INVALID);

  return _dbus_header_get_message_type (&message->header);
}

/**
 * Appends fields to a message given a variable argument list. The
 * variable argument list should contain the type of each argument
 * followed by the value to append. Appendable types are basic types,
 * and arrays of fixed-length basic types. To append variable-length
 * basic types, or any more complex value, you have to use an iterator
 * rather than this function.
 *
 * To append a basic type, specify its type code followed by the
 * value. For example:
 *
 * @code
 * DBUS_TYPE_INT32, 42,
 * DBUS_TYPE_STRING, "Hello World"
 * @endcode
 * or
 * @code
 * dbus_int32_t val = 42;
 * DBUS_TYPE_INT32, val
 * @endcode
 *
 * Be sure that your provided value is the right size. For example, this
 * won't work:
 * @code
 * DBUS_TYPE_INT64, 42
 * @endcode
 * Because the "42" will be a 32-bit integer. You need to cast to
 * 64-bit.
 *
 * To append an array of fixed-length basic types, pass in the
 * DBUS_TYPE_ARRAY typecode, the element typecode, the address of
 * the array pointer, and a 32-bit integer giving the number of
 * elements in the array. So for example:
 * @code
 * const dbus_int32_t array[] = { 1, 2, 3 };
 * const dbus_int32_t *v_ARRAY = array;
 * DBUS_TYPE_ARRAY, DBUS_TYPE_INT32, &v_ARRAY, 3
 * @endcode
 *
 * @warning in C, given "int array[]", "&array == array" (the
 * comp.lang.c FAQ says otherwise, but gcc and the FAQ don't agree).
 * So if you're using an array instead of a pointer you have to create
 * a pointer variable, assign the array to it, then take the address
 * of the pointer variable. For strings it works to write
 * const char *array = "Hello" and then use &array though.
 *
 * The last argument to this function must be #DBUS_TYPE_INVALID,
 * marking the end of the argument list.
 *
 * @todo support DBUS_TYPE_STRUCT and DBUS_TYPE_VARIANT and complex arrays
 *
 * @todo If this fails due to lack of memory, the message is hosed and
 * you have to start over building the whole message.
 *
 * @param message the message
 * @param first_arg_type type of the first argument
 * @param ... value of first argument, list of additional type-value pairs
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_append_args (DBusMessage *message,
			  int          first_arg_type,
			  ...)
{
  dbus_bool_t retval;
  va_list var_args;

  _dbus_return_val_if_fail (message != NULL, FALSE);

  va_start (var_args, first_arg_type);
  retval = dbus_message_append_args_valist (message,
					    first_arg_type,
					    var_args);
  va_end (var_args);

  return retval;
}

/**
 * This function takes a va_list for use by language bindings.
 * It's otherwise the same as dbus_message_append_args().
 *
 * @todo for now, if this function fails due to OOM it will leave
 * the message half-written and you have to discard the message
 * and start over.
 *
 * @see dbus_message_append_args.
 * @param message the message
 * @param first_arg_type type of first argument
 * @param var_args value of first argument, then list of type/value pairs
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_message_append_args_valist (DBusMessage *message,
				 int          first_arg_type,
				 va_list      var_args)
{
  int type;
  DBusMessageIter iter;

  _dbus_return_val_if_fail (message != NULL, FALSE);

  type = first_arg_type;

  dbus_message_iter_init_append (message, &iter);

  while (type != DBUS_TYPE_INVALID)
    {
      if (_dbus_type_is_basic (type))
        {
          const DBusBasicValue *value;
          value = va_arg (var_args, const DBusBasicValue*);

          if (!dbus_message_iter_append_basic (&iter,
                                               type,
                                               value))
            goto failed;
        }
      else if (type == DBUS_TYPE_ARRAY)
        {
          int element_type;
          const DBusBasicValue **value;
          int n_elements;
          DBusMessageIter array;
          char buf[2];

          element_type = va_arg (var_args, int);

#ifndef DBUS_DISABLE_CHECKS
          if (!_dbus_type_is_fixed (element_type))
            {
              _dbus_warn ("arrays of %s can't be appended with %s for now\n",
                          _dbus_type_to_string (element_type),
                          _DBUS_FUNCTION_NAME);
              goto failed;
            }
#endif

          value = va_arg (var_args, const DBusBasicValue**);
          n_elements = va_arg (var_args, int);

          buf[0] = element_type;
          buf[1] = '\0';
          if (!dbus_message_iter_open_container (&iter,
                                                 DBUS_TYPE_ARRAY,
                                                 buf,
                                                 &array))
            goto failed;

          if (!dbus_message_iter_append_fixed_array (&array,
                                                     element_type,
                                                     value,
                                                     n_elements))
            goto failed;

          if (!dbus_message_iter_close_container (&iter, &array))
            goto failed;
        }
#ifndef DBUS_DISABLE_CHECKS
      else
        {
          _dbus_warn ("type %s isn't supported yet in %s\n",
                      _dbus_type_to_string (type), _DBUS_FUNCTION_NAME);
          goto failed;
        }
#endif

      type = va_arg (var_args, int);
    }

  return TRUE;

 failed:
  return FALSE;
}

/**
 * Gets arguments from a message given a variable argument list.  The
 * supported types include those supported by
 * dbus_message_append_args(); that is, basic types and arrays of
 * fixed-length basic types.  The arguments are the same as they would
 * be for dbus_message_iter_get_basic() or
 * dbus_message_iter_get_fixed_array().
 *
 * In addition to those types, arrays of string, object path, and
 * signature are supported; but these are returned as allocated memory
 * and must be freed with dbus_free_string_array(), while the other
 * types are returned as const references.
 *
 * The variable argument list should contain the type of the argument
 * followed by a pointer to where the value should be stored. The list
 * is terminated with #DBUS_TYPE_INVALID.
 *
 * The returned values are constant; do not free them. They point
 * into the #DBusMessage.
 *
 * If the requested arguments are not present, or do not have the
 * requested types, then an error will be set.
 *
 * @todo support DBUS_TYPE_STRUCT and DBUS_TYPE_VARIANT and complex arrays
 *
 * @param message the message
 * @param error error to be filled in on failure
 * @param first_arg_type the first argument type
 * @param ... location for first argument value, then list of type-location pairs
 * @returns #FALSE if the error was set
 */
dbus_bool_t
dbus_message_get_args (DBusMessage     *message,
                       DBusError       *error,
		       int              first_arg_type,
		       ...)
{
  dbus_bool_t retval;
  va_list var_args;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_error_is_set (error, FALSE);

  va_start (var_args, first_arg_type);
  retval = dbus_message_get_args_valist (message, error, first_arg_type, var_args);
  va_end (var_args);

  return retval;
}

/**
 * This function takes a va_list for use by language bindings. It is
 * otherwise the same as dbus_message_get_args().
 *
 * @see dbus_message_get_args
 * @param message the message
 * @param error error to be filled in
 * @param first_arg_type type of the first argument
 * @param var_args return location for first argument, followed by list of type/location pairs
 * @returns #FALSE if error was set
 */
dbus_bool_t
dbus_message_get_args_valist (DBusMessage     *message,
                              DBusError       *error,
			      int              first_arg_type,
			      va_list          var_args)
{
  DBusMessageIter iter;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_error_is_set (error, FALSE);

  dbus_message_iter_init (message, &iter);
  return dbus_message_iter_get_args_valist (&iter, error, first_arg_type, var_args);
}

/**
 * Reads arguments from a message iterator given a variable argument
 * list. Only arguments of basic type and arrays of fixed-length
 * basic type may be read with this function. See
 * dbus_message_get_args() for more details.
 *
 * @todo this is static for now because there's no corresponding
 * iter_append_args() and I'm not sure we need this function to be
 * public since dbus_message_get_args() is what you usually want
 *
 * @param iter the message iterator
 * @param error error to be filled in on failure
 * @param first_arg_type the first argument type
 * @param ... location for first argument value, then list of type-location pairs
 * @returns #FALSE if the error was set
 */
static dbus_bool_t
dbus_message_iter_get_args (DBusMessageIter *iter,
			    DBusError       *error,
			    int              first_arg_type,
			    ...)
{
  dbus_bool_t retval;
  va_list var_args;

  _dbus_return_val_if_fail (iter != NULL, FALSE);
  _dbus_return_val_if_error_is_set (error, FALSE);

  va_start (var_args, first_arg_type);
  retval = dbus_message_iter_get_args_valist (iter, error, first_arg_type, var_args);
  va_end (var_args);

  return retval;
}

static void
_dbus_message_iter_init_common (DBusMessage         *message,
                                DBusMessageRealIter *real,
                                int                  iter_type)
{
  _dbus_assert (sizeof (DBusMessageRealIter) <= sizeof (DBusMessageIter));

  real->message = message;
  real->changed_stamp = message->changed_stamp;
  real->iter_type = iter_type;
  real->sig_refcount = 0;
}

/**
 * Initializes a #DBusMessageIter for reading the arguments of the
 * message passed in.
 *
 * @param message the message
 * @param iter pointer to an iterator to initialize
 * @returns #FALSE if the message has no arguments
 */
dbus_bool_t
dbus_message_iter_init (DBusMessage     *message,
			DBusMessageIter *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  const DBusString *type_str;
  int type_pos;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (iter != NULL, FALSE);

  get_const_signature (&message->header, &type_str, &type_pos);

  _dbus_message_iter_init_common (message, real,
                                  DBUS_MESSAGE_ITER_TYPE_READER);

  _dbus_type_reader_init (&real->u.reader,
                          message->byte_order,
                          type_str, type_pos,
                          &message->body,
                          0);

  return _dbus_type_reader_has_next (&real->u.reader);
}

#ifndef DBUS_DISABLE_CHECKS
static dbus_bool_t
_dbus_message_iter_check (DBusMessageRealIter *iter)
{
  if (iter == NULL)
    {
      _dbus_warn ("dbus message iterator is NULL\n");
      return FALSE;
    }

  if (iter->iter_type == DBUS_MESSAGE_ITER_TYPE_READER)
    {
      if (iter->u.reader.byte_order != iter->message->byte_order)
        {
          _dbus_warn ("dbus message changed byte order since iterator was created\n");
          return FALSE;
        }
    }
  else if (iter->iter_type == DBUS_MESSAGE_ITER_TYPE_WRITER)
    {
      if (iter->u.writer.byte_order != iter->message->byte_order)
        {
          _dbus_warn ("dbus message changed byte order since append iterator was created\n");
          return FALSE;
        }
    }
  else
    {
      _dbus_warn ("dbus message iterator looks uninitialized or corrupted\n");
      return FALSE;
    }

  if (iter->changed_stamp != iter->message->changed_stamp)
    {
      _dbus_warn ("dbus message iterator invalid because the message has been modified (or perhaps the iterator is just uninitialized)\n");
      return FALSE;
    }

  return TRUE;
}
#endif /* DBUS_DISABLE_CHECKS */

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
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (_dbus_message_iter_check (real), FALSE);
  _dbus_return_val_if_fail (real->iter_type == DBUS_MESSAGE_ITER_TYPE_READER, FALSE);

  return _dbus_type_reader_has_next (&real->u.reader);
}

/**
 * Moves the iterator to the next field, if any. If there's no next
 * field, returns #FALSE. If the iterator moves forward, returns
 * #TRUE.
 *
 * @param iter the message iter
 * @returns #TRUE if the iterator was moved to the next field
 */
dbus_bool_t
dbus_message_iter_next (DBusMessageIter *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (_dbus_message_iter_check (real), FALSE);
  _dbus_return_val_if_fail (real->iter_type == DBUS_MESSAGE_ITER_TYPE_READER, FALSE);

  return _dbus_type_reader_next (&real->u.reader);
}

/**
 * Returns the argument type of the argument that the message iterator
 * points to. If the iterator is at the end of the message, returns
 * #DBUS_TYPE_INVALID. You can thus write a loop as follows:
 *
 * @code
 * dbus_message_iter_init (&iter);
 * while ((current_type = dbus_message_iter_get_arg_type (&iter)) != DBUS_TYPE_INVALID)
 *   dbus_message_iter_next (&iter);
 * @endcode
 *
 * @param iter the message iter
 * @returns the argument type
 */
int
dbus_message_iter_get_arg_type (DBusMessageIter *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (_dbus_message_iter_check (real), DBUS_TYPE_INVALID);
  _dbus_return_val_if_fail (real->iter_type == DBUS_MESSAGE_ITER_TYPE_READER, FALSE);

  return _dbus_type_reader_get_current_type (&real->u.reader);
}

/**
 * Returns the element type of the array that the message iterator
 * points to. Note that you need to check that the iterator points to
 * an array prior to using this function.
 *
 * @param iter the message iter
 * @returns the array element type
 */
int
dbus_message_iter_get_element_type (DBusMessageIter *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_val_if_fail (_dbus_message_iter_check (real), DBUS_TYPE_INVALID);
  _dbus_return_val_if_fail (real->iter_type == DBUS_MESSAGE_ITER_TYPE_READER, DBUS_TYPE_INVALID);
  _dbus_return_val_if_fail (dbus_message_iter_get_arg_type (iter) == DBUS_TYPE_ARRAY, DBUS_TYPE_INVALID);

  return _dbus_type_reader_get_element_type (&real->u.reader);
}

/**
 * Recurses into a container value when reading values from a message,
 * initializing a sub-iterator to use for traversing the child values
 * of the container.
 *
 * Note that this recurses into a value, not a type, so you can only
 * recurse if the value exists. The main implication of this is that
 * if you have for example an empty array of array of int32, you can
 * recurse into the outermost array, but it will have no values, so
 * you won't be able to recurse further. There's no array of int32 to
 * recurse into.
 *
 * @param iter the message iterator
 * @param sub the sub-iterator to initialize
 */
void
dbus_message_iter_recurse (DBusMessageIter  *iter,
                           DBusMessageIter  *sub)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  DBusMessageRealIter *real_sub = (DBusMessageRealIter *)sub;

  _dbus_return_if_fail (_dbus_message_iter_check (real));
  _dbus_return_if_fail (sub != NULL);

  *real_sub = *real;
  _dbus_type_reader_recurse (&real->u.reader, &real_sub->u.reader);
}

/**
 * Reads a basic-typed value from the message iterator.
 * Basic types are the non-containers such as integer and string.
 *
 * The value argument should be the address of a location to store
 * the returned value. So for int32 it should be a "dbus_int32_t*"
 * and for string a "const char**". The returned value is
 * by reference and should not be freed.
 *
 * All returned values are guaranteed to fit in 8 bytes. So you can
 * write code like this:
 *
 * @code
 * #ifdef DBUS_HAVE_INT64
 * dbus_uint64_t value;
 * int type;
 * dbus_message_iter_get_basic (&read_iter, &value);
 * type = dbus_message_iter_get_arg_type (&read_iter);
 * dbus_message_iter_append_basic (&write_iter, type, &value);
 * #endif
 * @endcode
 *
 * To avoid the #DBUS_HAVE_INT64 conditional, create a struct or
 * something that occupies at least 8 bytes, e.g. you could use a
 * struct with two int32 values in it. dbus_uint64_t is just one
 * example of a type that's large enough to hold any possible value.
 *
 * Be sure you have somehow checked that
 * dbus_message_iter_get_arg_type() matches the type you are
 * expecting, or you'll crash when you try to use an integer as a
 * string or something.
 *
 * @param iter the iterator
 * @param value location to store the value
 */
void
dbus_message_iter_get_basic (DBusMessageIter  *iter,
                             void             *value)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_if_fail (_dbus_message_iter_check (real));
  _dbus_return_if_fail (value != NULL);

  _dbus_type_reader_read_basic (&real->u.reader,
                                value);
}

/**
 * Reads a block of fixed-length values from the message iterator.
 * Fixed-length values are those basic types that are not string-like,
 * such as integers, bool, double. The block read will be from the
 * current position in the array until the end of the array.
 *
 * The value argument should be the address of a location to store the
 * returned array. So for int32 it should be a "const dbus_int32_t**"
 * The returned value is by reference and should not be freed.
 *
 * @param iter the iterator
 * @param value location to store the block
 * @param n_elements number of elements in the block
 */
void
dbus_message_iter_get_fixed_array (DBusMessageIter  *iter,
                                   void             *value,
                                   int              *n_elements)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_if_fail (_dbus_message_iter_check (real));
  _dbus_return_if_fail (value != NULL);
  _dbus_return_if_fail (_dbus_type_is_fixed (_dbus_type_reader_get_element_type (&real->u.reader)));

  _dbus_type_reader_read_fixed_multi (&real->u.reader,
                                      value, n_elements);
}

/**
 * This function takes a va_list for use by language bindings and is
 * otherwise the same as dbus_message_iter_get_args().
 * dbus_message_get_args() is the place to go for complete
 * documentation.
 *
 * @todo this is static for now, should be public if
 * dbus_message_iter_get_args_valist() is made public.
 *
 * @see dbus_message_get_args
 * @param iter the message iter
 * @param error error to be filled in
 * @param first_arg_type type of the first argument
 * @param var_args return location for first argument, followed by list of type/location pairs
 * @returns #FALSE if error was set
 */
static dbus_bool_t
dbus_message_iter_get_args_valist (DBusMessageIter *iter,
				   DBusError       *error,
				   int              first_arg_type,
				   va_list          var_args)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  int spec_type, msg_type, i;
  dbus_bool_t retval;

  _dbus_return_val_if_fail (_dbus_message_iter_check (real), FALSE);
  _dbus_return_val_if_error_is_set (error, FALSE);

  retval = FALSE;

  spec_type = first_arg_type;
  i = 0;

  while (spec_type != DBUS_TYPE_INVALID)
    {
      msg_type = dbus_message_iter_get_arg_type (iter);

      if (msg_type != spec_type)
	{
          dbus_set_error (error, DBUS_ERROR_INVALID_ARGS,
                          "Argument %d is specified to be of type \"%s\", but "
                          "is actually of type \"%s\"\n", i,
                          _dbus_type_to_string (spec_type),
                          _dbus_type_to_string (msg_type));

          goto out;
	}

      if (_dbus_type_is_basic (spec_type))
        {
          DBusBasicValue *ptr;

          ptr = va_arg (var_args, DBusBasicValue*);

          _dbus_return_val_if_fail (ptr != NULL, FALSE);

          _dbus_type_reader_read_basic (&real->u.reader,
                                        ptr);
        }
      else if (spec_type == DBUS_TYPE_ARRAY)
        {
          int element_type;
          int spec_element_type;
          const DBusBasicValue **ptr;
          int *n_elements_p;
          DBusTypeReader array;

          spec_element_type = va_arg (var_args, int);
          element_type = _dbus_type_reader_get_element_type (&real->u.reader);

          if (spec_element_type != element_type)
            {
              dbus_set_error (error, DBUS_ERROR_INVALID_ARGS,
                              "Argument %d is specified to be an array of \"%s\", but "
                              "is actually an array of \"%s\"\n",
                              i,
                              _dbus_type_to_string (spec_element_type),
                              _dbus_type_to_string (element_type));

              goto out;
            }

          if (_dbus_type_is_fixed (spec_element_type))
            {
              ptr = va_arg (var_args, const DBusBasicValue**);
              n_elements_p = va_arg (var_args, int*);

              _dbus_return_val_if_fail (ptr != NULL, FALSE);
              _dbus_return_val_if_fail (n_elements_p != NULL, FALSE);

              _dbus_type_reader_recurse (&real->u.reader, &array);

              _dbus_type_reader_read_fixed_multi (&array,
                                                  ptr, n_elements_p);
            }
          else if (spec_element_type == DBUS_TYPE_STRING ||
                   spec_element_type == DBUS_TYPE_SIGNATURE ||
                   spec_element_type == DBUS_TYPE_OBJECT_PATH)
            {
              char ***str_array_p;
              int i;
              char **str_array;

              str_array_p = va_arg (var_args, char***);
              n_elements_p = va_arg (var_args, int*);

              _dbus_return_val_if_fail (str_array_p != NULL, FALSE);
              _dbus_return_val_if_fail (n_elements_p != NULL, FALSE);

              /* Count elements in the array */
              _dbus_type_reader_recurse (&real->u.reader, &array);

              i = 0;
              if (_dbus_type_reader_has_next (&array))
                {
                  while (_dbus_type_reader_next (&array))
                    ++i;
                }

              str_array = dbus_new0 (char*, i + 1);
              if (str_array == NULL)
                {
                  _DBUS_SET_OOM (error);
                  goto out;
                }

              /* Now go through and dup each string */
              _dbus_type_reader_recurse (&real->u.reader, &array);

              i = 0;
              if (_dbus_type_reader_has_next (&array))
                {
                  do
                    {
                      const char *s;
                      _dbus_type_reader_read_basic (&array,
                                                    &s);

                      str_array[i] = _dbus_strdup (s);
                      if (str_array[i] == NULL)
                        {
                          dbus_free_string_array (str_array);
                          _DBUS_SET_OOM (error);
                          goto out;
                        }

                      ++i;
                    }
                  while (_dbus_type_reader_next (&array));
                }

              *str_array_p = str_array;
              *n_elements_p = i;
            }
#ifndef DBUS_DISABLE_CHECKS
          else
            {
              _dbus_warn ("you can't read arrays of container types (struct, variant, array) with %s for now\n",
                          _DBUS_FUNCTION_NAME);
              goto out;
            }
#endif
        }
#ifndef DBUS_DISABLE_CHECKS
      else
        {
          _dbus_warn ("you can only read arrays and basic types with %s for now\n",
                      _DBUS_FUNCTION_NAME);
          goto out;
        }
#endif

      spec_type = va_arg (var_args, int);
      if (!_dbus_type_reader_next (&real->u.reader) && spec_type != DBUS_TYPE_INVALID)
        {
          dbus_set_error (error, DBUS_ERROR_INVALID_ARGS,
                          "Message has only %d arguments, but more were expected", i);
          goto out;
        }

      i++;
    }

  retval = TRUE;

 out:

  return retval;
}

/**
 * Initializes a #DBusMessageIter for appending arguments to the end
 * of a message.
 *
 * @todo If appending any of the arguments fails due to lack of
 * memory, generally the message is hosed and you have to start over
 * building the whole message.
 *
 * @param message the message
 * @param iter pointer to an iterator to initialize
 */
void
dbus_message_iter_init_append (DBusMessage     *message,
			       DBusMessageIter *iter)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;

  _dbus_return_if_fail (message != NULL);
  _dbus_return_if_fail (iter != NULL);

  _dbus_message_iter_init_common (message, real,
                                  DBUS_MESSAGE_ITER_TYPE_WRITER);

  /* We create the signature string and point iterators at it "on demand"
   * when a value is actually appended. That means that init() never fails
   * due to OOM.
   */
  _dbus_type_writer_init_types_delayed (&real->u.writer,
                                        message->byte_order,
                                        &message->body,
                                        _dbus_string_get_length (&message->body));
}

/**
 * Creates a temporary signature string containing the current
 * signature, stores it in the iterator, and points the iterator to
 * the end of it. Used any time we write to the message.
 *
 * @param real an iterator without a type_str
 * @returns #FALSE if no memory
 */
static dbus_bool_t
_dbus_message_iter_open_signature (DBusMessageRealIter *real)
{
  DBusString *str;
  const DBusString *current_sig;
  int current_sig_pos;

  _dbus_assert (real->iter_type == DBUS_MESSAGE_ITER_TYPE_WRITER);

  if (real->u.writer.type_str != NULL)
    {
      _dbus_assert (real->sig_refcount > 0);
      real->sig_refcount += 1;
      return TRUE;
    }

  str = dbus_new (DBusString, 1);
  if (str == NULL)
    return FALSE;

  if (!_dbus_header_get_field_raw (&real->message->header,
                                   DBUS_HEADER_FIELD_SIGNATURE,
                                   &current_sig, &current_sig_pos))
    current_sig = NULL;

  if (current_sig)
    {
      int current_len;

      current_len = _dbus_string_get_byte (current_sig, current_sig_pos);
      current_sig_pos += 1; /* move on to sig data */

      if (!_dbus_string_init_preallocated (str, current_len + 4))
        {
          dbus_free (str);
          return FALSE;
        }

      if (!_dbus_string_copy_len (current_sig, current_sig_pos, current_len,
                                  str, 0))
        {
          _dbus_string_free (str);
          dbus_free (str);
          return FALSE;
        }
    }
  else
    {
      if (!_dbus_string_init_preallocated (str, 4))
        {
          dbus_free (str);
          return FALSE;
        }
    }

  real->sig_refcount = 1;

  _dbus_type_writer_add_types (&real->u.writer,
                               str, _dbus_string_get_length (str));
  return TRUE;
}

/**
 * Sets the new signature as the message signature, frees the
 * signature string, and marks the iterator as not having a type_str
 * anymore. Frees the signature even if it fails, so you can't
 * really recover from failure. Kinda busted.
 *
 * @param real an iterator without a type_str
 * @returns #FALSE if no memory
 */
static dbus_bool_t
_dbus_message_iter_close_signature (DBusMessageRealIter *real)
{
  DBusString *str;
  const char *v_STRING;
  dbus_bool_t retval;

  _dbus_assert (real->iter_type == DBUS_MESSAGE_ITER_TYPE_WRITER);
  _dbus_assert (real->u.writer.type_str != NULL);
  _dbus_assert (real->sig_refcount > 0);

  real->sig_refcount -= 1;

  if (real->sig_refcount > 0)
    return TRUE;
  _dbus_assert (real->sig_refcount == 0);

  retval = TRUE;

  str = real->u.writer.type_str;

  v_STRING = _dbus_string_get_const_data (str);
  if (!_dbus_header_set_field_basic (&real->message->header,
                                     DBUS_HEADER_FIELD_SIGNATURE,
                                     DBUS_TYPE_SIGNATURE,
                                     &v_STRING))
    retval = FALSE;

  _dbus_type_writer_remove_types (&real->u.writer);
  _dbus_string_free (str);
  dbus_free (str);

  return retval;
}

#ifndef DBUS_DISABLE_CHECKS
static dbus_bool_t
_dbus_message_iter_append_check (DBusMessageRealIter *iter)
{
  if (!_dbus_message_iter_check (iter))
    return FALSE;

  if (iter->message->locked)
    {
      _dbus_warn ("dbus append iterator can't be used: message is locked (has already been sent)\n");
      return FALSE;
    }

  return TRUE;
}
#endif /* DBUS_DISABLE_CHECKS */

/**
 * Appends a basic-typed value to the message. The basic types are the
 * non-container types such as integer and string.
 *
 * The "value" argument should be the address of a basic-typed value.
 * So for string, const char**. For integer, dbus_int32_t*.
 *
 * @todo If this fails due to lack of memory, the message is hosed and
 * you have to start over building the whole message.
 *
 * @param iter the append iterator
 * @param type the type of the value
 * @param value the address of the value
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_iter_append_basic (DBusMessageIter *iter,
                                int              type,
                                const void      *value)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  dbus_bool_t ret;

  _dbus_return_val_if_fail (_dbus_message_iter_append_check (real), FALSE);
  _dbus_return_val_if_fail (real->iter_type == DBUS_MESSAGE_ITER_TYPE_WRITER, FALSE);
  _dbus_return_val_if_fail (_dbus_type_is_basic (type), FALSE);
  _dbus_return_val_if_fail (value != NULL, FALSE);

  if (!_dbus_message_iter_open_signature (real))
    return FALSE;

  ret = _dbus_type_writer_write_basic (&real->u.writer, type, value);

  if (!_dbus_message_iter_close_signature (real))
    ret = FALSE;

  return ret;
}

/**
 * Appends a block of fixed-length values to an array. The
 * fixed-length types are all basic types that are not string-like. So
 * int32, double, bool, etc. You must call
 * dbus_message_iter_open_container() to open an array of values
 * before calling this function. You may call this function multiple
 * times (and intermixed with calls to
 * dbus_message_iter_append_basic()) for the same array.
 *
 * The "value" argument should be the address of the array.  So for
 * integer, "dbus_int32_t**" is expected for example.
 *
 * @warning in C, given "int array[]", "&array == array" (the
 * comp.lang.c FAQ says otherwise, but gcc and the FAQ don't agree).
 * So if you're using an array instead of a pointer you have to create
 * a pointer variable, assign the array to it, then take the address
 * of the pointer variable.
 * @code
 * const dbus_int32_t array[] = { 1, 2, 3 };
 * const dbus_int32_t *v_ARRAY = array;
 * if (!dbus_message_iter_append_fixed_array (&iter, DBUS_TYPE_INT32, &v_ARRAY, 3))
 *   fprintf (stderr, "No memory!\n");
 * @endcode
 * For strings it works to write const char *array = "Hello" and then
 * use &array though.
 *
 * @todo If this fails due to lack of memory, the message is hosed and
 * you have to start over building the whole message.
 *
 * @param iter the append iterator
 * @param element_type the type of the array elements
 * @param value the address of the array
 * @param n_elements the number of elements to append
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_iter_append_fixed_array (DBusMessageIter *iter,
                                      int              element_type,
                                      const void      *value,
                                      int              n_elements)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  dbus_bool_t ret;

  _dbus_return_val_if_fail (_dbus_message_iter_append_check (real), FALSE);
  _dbus_return_val_if_fail (real->iter_type == DBUS_MESSAGE_ITER_TYPE_WRITER, FALSE);
  _dbus_return_val_if_fail (_dbus_type_is_fixed (element_type), FALSE);
  _dbus_return_val_if_fail (real->u.writer.container_type == DBUS_TYPE_ARRAY, FALSE);
  _dbus_return_val_if_fail (value != NULL, FALSE);
  _dbus_return_val_if_fail (n_elements >= 0, FALSE);
  _dbus_return_val_if_fail (n_elements <=
                            DBUS_MAXIMUM_ARRAY_LENGTH / _dbus_type_get_alignment (element_type),
                            FALSE);

  ret = _dbus_type_writer_write_fixed_multi (&real->u.writer, element_type, value, n_elements);

  return ret;
}

/**
 * Appends a container-typed value to the message; you are required to
 * append the contents of the container using the returned
 * sub-iterator, and then call
 * dbus_message_iter_close_container(). Container types are for
 * example struct, variant, and array. For variants, the
 * contained_signature should be the type of the single value inside
 * the variant. For structs, contained_signature should be #NULL; it
 * will be set to whatever types you write into the struct.  For
 * arrays, contained_signature should be the type of the array
 * elements.
 *
 * @todo If this fails due to lack of memory, the message is hosed and
 * you have to start over building the whole message.
 *
 * @param iter the append iterator
 * @param type the type of the value
 * @param contained_signature the type of container contents
 * @param sub sub-iterator to initialize
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_iter_open_container (DBusMessageIter *iter,
                                  int              type,
                                  const char      *contained_signature,
                                  DBusMessageIter *sub)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  DBusMessageRealIter *real_sub = (DBusMessageRealIter *)sub;
  DBusString contained_str;

  _dbus_return_val_if_fail (_dbus_message_iter_append_check (real), FALSE);
  _dbus_return_val_if_fail (real->iter_type == DBUS_MESSAGE_ITER_TYPE_WRITER, FALSE);
  _dbus_return_val_if_fail (_dbus_type_is_container (type), FALSE);
  _dbus_return_val_if_fail (sub != NULL, FALSE);
  _dbus_return_val_if_fail ((type == DBUS_TYPE_STRUCT &&
                             contained_signature == NULL) ||
                            contained_signature != NULL, FALSE);

  if (!_dbus_message_iter_open_signature (real))
    return FALSE;

  _dbus_string_init_const (&contained_str, contained_signature);

  *real_sub = *real;
  return _dbus_type_writer_recurse (&real->u.writer,
                                    type,
                                    &contained_str, 0,
                                    &real_sub->u.writer);
}


/**
 * Closes a container-typed value appended to the message; may write
 * out more information to the message known only after the entire
 * container is written, and may free resources created by
 * dbus_message_iter_open_container().
 *
 * @todo If this fails due to lack of memory, the message is hosed and
 * you have to start over building the whole message.
 *
 * @param iter the append iterator
 * @param sub sub-iterator to close
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_iter_close_container (DBusMessageIter *iter,
                                   DBusMessageIter *sub)
{
  DBusMessageRealIter *real = (DBusMessageRealIter *)iter;
  DBusMessageRealIter *real_sub = (DBusMessageRealIter *)sub;
  dbus_bool_t ret;

  _dbus_return_val_if_fail (_dbus_message_iter_append_check (real), FALSE);
  _dbus_return_val_if_fail (real->iter_type == DBUS_MESSAGE_ITER_TYPE_WRITER, FALSE);
  _dbus_return_val_if_fail (_dbus_message_iter_append_check (real_sub), FALSE);
  _dbus_return_val_if_fail (real_sub->iter_type == DBUS_MESSAGE_ITER_TYPE_WRITER, FALSE);

  ret = _dbus_type_writer_unrecurse (&real->u.writer,
                                     &real_sub->u.writer);

  if (!_dbus_message_iter_close_signature (real))
    ret = FALSE;

  return ret;
}

/**
 * Sets a flag indicating that the message does not want a reply; if
 * this flag is set, the other end of the connection may (but is not
 * required to) optimize by not sending method return or error
 * replies. If this flag is set, there is no way to know whether the
 * message successfully arrived at the remote end. Normally you know a
 * message was received when you receive the reply to it.
 *
 * @param message the message
 * @param no_reply #TRUE if no reply is desired
 */
void
dbus_message_set_no_reply (DBusMessage *message,
                           dbus_bool_t  no_reply)
{
  _dbus_return_if_fail (message != NULL);
  _dbus_return_if_fail (!message->locked);

  _dbus_header_toggle_flag (&message->header,
                            DBUS_HEADER_FLAG_NO_REPLY_EXPECTED,
                            no_reply);
}

/**
 * Returns #TRUE if the message does not expect
 * a reply.
 *
 * @param message the message
 * @returns #TRUE if the message sender isn't waiting for a reply
 */
dbus_bool_t
dbus_message_get_no_reply (DBusMessage *message)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);

  return _dbus_header_get_flag (&message->header,
                                DBUS_HEADER_FLAG_NO_REPLY_EXPECTED);
}

/**
 * Sets a flag indicating that the addressed service will be
 * auto-activated before the message is delivered. When this flag is
 * set, the message is held until the service is succesfully activated
 * or fails to activate. In case of failure, the reply will be an
 * activation error. If this flag is not set (the default
 *
 * @param message the message
 * @param auto_activation #TRUE if auto-activation is desired
 */
void
dbus_message_set_auto_activation (DBusMessage *message,
				  dbus_bool_t  auto_activation)
{
  _dbus_return_if_fail (message != NULL);
  _dbus_return_if_fail (!message->locked);

  _dbus_header_toggle_flag (&message->header,
                            DBUS_HEADER_FLAG_AUTO_ACTIVATION,
                            auto_activation);
}

/**
 * Returns #TRUE if the message will cause the addressed service to be
 * auto-activated.
 *
 * @param message the message
 * @returns #TRUE if the message will use auto-activation
 */
dbus_bool_t
dbus_message_get_auto_activation (DBusMessage *message)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);

  return _dbus_header_get_flag (&message->header,
                                DBUS_HEADER_FLAG_AUTO_ACTIVATION);
}


/**
 * Sets the object path this message is being sent to (for
 * DBUS_MESSAGE_TYPE_METHOD_CALL) or the one a signal is being
 * emitted from (for DBUS_MESSAGE_TYPE_SIGNAL).
 *
 * @param message the message
 * @param object_path the path or #NULL to unset
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_path (DBusMessage   *message,
                       const char    *object_path)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);
  _dbus_return_val_if_fail (object_path == NULL ||
                            _dbus_check_is_valid_path (object_path),
                            FALSE);

  return set_or_delete_string_field (message,
                                     DBUS_HEADER_FIELD_PATH,
                                     DBUS_TYPE_OBJECT_PATH,
                                     object_path);
}

/**
 * Gets the object path this message is being sent to (for
 * DBUS_MESSAGE_TYPE_METHOD_CALL) or being emitted from (for
 * DBUS_MESSAGE_TYPE_SIGNAL). Returns #NULL if none.
 *
 * @param message the message
 * @returns the path (should not be freed) or #NULL
 */
const char*
dbus_message_get_path (DBusMessage   *message)
{
  const char *v;

  _dbus_return_val_if_fail (message != NULL, NULL);

  v = NULL; /* in case field doesn't exist */
  _dbus_header_get_field_basic (&message->header,
                                DBUS_HEADER_FIELD_PATH,
                                DBUS_TYPE_OBJECT_PATH,
                                &v);
  return v;
}

/**
 * Gets the object path this message is being sent to
 * (for DBUS_MESSAGE_TYPE_METHOD_CALL) or being emitted
 * from (for DBUS_MESSAGE_TYPE_SIGNAL) in a decomposed
 * format (one array element per path component).
 * Free the returned array with dbus_free_string_array().
 *
 * An empty but non-NULL path array means the path "/".
 * So the path "/foo/bar" becomes { "foo", "bar", NULL }
 * and the path "/" becomes { NULL }.
 *
 * @todo this could be optimized by using the len from the message
 * instead of calling strlen() again
 *
 * @param message the message
 * @param path place to store allocated array of path components; #NULL set here if no path field exists
 * @returns #FALSE if no memory to allocate the array
 */
dbus_bool_t
dbus_message_get_path_decomposed (DBusMessage   *message,
                                  char        ***path)
{
  const char *v;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (path != NULL, FALSE);

  *path = NULL;

  v = dbus_message_get_path (message);
  if (v != NULL)
    {
      if (!_dbus_decompose_path (v, strlen (v),
                                 path, NULL))
        return FALSE;
    }
  return TRUE;
}

/**
 * Sets the interface this message is being sent to
 * (for DBUS_MESSAGE_TYPE_METHOD_CALL) or
 * the interface a signal is being emitted from
 * (for DBUS_MESSAGE_TYPE_SIGNAL).
 *
 * @param message the message
 * @param interface the interface or #NULL to unset
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_interface (DBusMessage  *message,
                            const char   *interface)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);
  _dbus_return_val_if_fail (interface == NULL ||
                            _dbus_check_is_valid_interface (interface),
                            FALSE);

  return set_or_delete_string_field (message,
                                     DBUS_HEADER_FIELD_INTERFACE,
                                     DBUS_TYPE_STRING,
                                     interface);
}

/**
 * Gets the interface this message is being sent to
 * (for DBUS_MESSAGE_TYPE_METHOD_CALL) or being emitted
 * from (for DBUS_MESSAGE_TYPE_SIGNAL).
 * The interface name is fully-qualified (namespaced).
 * Returns #NULL if none.
 *
 * @param message the message
 * @returns the message interface (should not be freed) or #NULL
 */
const char*
dbus_message_get_interface (DBusMessage *message)
{
  const char *v;

  _dbus_return_val_if_fail (message != NULL, NULL);

  v = NULL; /* in case field doesn't exist */
  _dbus_header_get_field_basic (&message->header,
                                DBUS_HEADER_FIELD_INTERFACE,
                                DBUS_TYPE_STRING,
                                &v);
  return v;
}

/**
 * Sets the interface member being invoked
 * (DBUS_MESSAGE_TYPE_METHOD_CALL) or emitted
 * (DBUS_MESSAGE_TYPE_SIGNAL).
 * The interface name is fully-qualified (namespaced).
 *
 * @param message the message
 * @param member the member or #NULL to unset
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_member (DBusMessage  *message,
                         const char   *member)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);
  _dbus_return_val_if_fail (member == NULL ||
                            _dbus_check_is_valid_member (member),
                            FALSE);

  return set_or_delete_string_field (message,
                                     DBUS_HEADER_FIELD_MEMBER,
                                     DBUS_TYPE_STRING,
                                     member);
}

/**
 * Gets the interface member being invoked
 * (DBUS_MESSAGE_TYPE_METHOD_CALL) or emitted
 * (DBUS_MESSAGE_TYPE_SIGNAL). Returns #NULL if none.
 *
 * @param message the message
 * @returns the member name (should not be freed) or #NULL
 */
const char*
dbus_message_get_member (DBusMessage *message)
{
  const char *v;

  _dbus_return_val_if_fail (message != NULL, NULL);

  v = NULL; /* in case field doesn't exist */
  _dbus_header_get_field_basic (&message->header,
                                DBUS_HEADER_FIELD_MEMBER,
                                DBUS_TYPE_STRING,
                                &v);
  return v;
}

/**
 * Sets the name of the error (DBUS_MESSAGE_TYPE_ERROR).
 * The name is fully-qualified (namespaced).
 *
 * @param message the message
 * @param error_name the name or #NULL to unset
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_error_name (DBusMessage  *message,
                             const char   *error_name)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);
  _dbus_return_val_if_fail (error_name == NULL ||
                            _dbus_check_is_valid_error_name (error_name),
                            FALSE);

  return set_or_delete_string_field (message,
                                     DBUS_HEADER_FIELD_ERROR_NAME,
                                     DBUS_TYPE_STRING,
                                     error_name);
}

/**
 * Gets the error name (DBUS_MESSAGE_TYPE_ERROR only)
 * or #NULL if none.
 *
 * @param message the message
 * @returns the error name (should not be freed) or #NULL
 */
const char*
dbus_message_get_error_name (DBusMessage *message)
{
  const char *v;

  _dbus_return_val_if_fail (message != NULL, NULL);

  v = NULL; /* in case field doesn't exist */
  _dbus_header_get_field_basic (&message->header,
                                DBUS_HEADER_FIELD_ERROR_NAME,
                                DBUS_TYPE_STRING,
                                &v);
  return v;
}

/**
 * Sets the message's destination service.
 *
 * @param message the message
 * @param destination the destination service name or #NULL to unset
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_destination (DBusMessage  *message,
                              const char   *destination)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);
  _dbus_return_val_if_fail (destination == NULL ||
                            _dbus_check_is_valid_service (destination),
                            FALSE);

  return set_or_delete_string_field (message,
                                     DBUS_HEADER_FIELD_DESTINATION,
                                     DBUS_TYPE_STRING,
                                     destination);
}

/**
 * Gets the destination service of a message or #NULL if there is
 * none set.
 *
 * @param message the message
 * @returns the message destination service (should not be freed) or #NULL
 */
const char*
dbus_message_get_destination (DBusMessage *message)
{
  const char *v;

  _dbus_return_val_if_fail (message != NULL, NULL);

  v = NULL; /* in case field doesn't exist */
  _dbus_header_get_field_basic (&message->header,
                                DBUS_HEADER_FIELD_DESTINATION,
                                DBUS_TYPE_STRING,
                                &v);
  return v;
}

/**
 * Sets the message sender.
 *
 * @param message the message
 * @param sender the sender or #NULL to unset
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_message_set_sender (DBusMessage  *message,
                         const char   *sender)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (!message->locked, FALSE);
  _dbus_return_val_if_fail (sender == NULL ||
                            _dbus_check_is_valid_service (sender),
                            FALSE);

  return set_or_delete_string_field (message,
                                     DBUS_HEADER_FIELD_SENDER,
                                     DBUS_TYPE_STRING,
                                     sender);
}

/**
 * Gets the service which originated this message,
 * or #NULL if unknown or inapplicable.
 *
 * @param message the message
 * @returns the service name or #NULL
 */
const char*
dbus_message_get_sender (DBusMessage *message)
{
  const char *v;

  _dbus_return_val_if_fail (message != NULL, NULL);

  v = NULL; /* in case field doesn't exist */
  _dbus_header_get_field_basic (&message->header,
                                DBUS_HEADER_FIELD_SENDER,
                                DBUS_TYPE_STRING,
                                &v);
  return v;
}

/**
 * Gets the type signature of the message, i.e. the arguments in the
 * message payload. The signature includes only "in" arguments for
 * #DBUS_MESSAGE_TYPE_METHOD_CALL and only "out" arguments for
 * #DBUS_MESSAGE_TYPE_METHOD_RETURN, so is slightly different from
 * what you might expect (it does not include the signature of the
 * entire C++-style method).
 *
 * The signature is a string made up of type codes such as
 * #DBUS_TYPE_INT32. The string is terminated with nul (nul is also
 * the value of #DBUS_TYPE_INVALID).
 *
 * @param message the message
 * @returns the type signature
 */
const char*
dbus_message_get_signature (DBusMessage *message)
{
  const DBusString *type_str;
  int type_pos;

  _dbus_return_val_if_fail (message != NULL, NULL);

  get_const_signature (&message->header, &type_str, &type_pos);

  return _dbus_string_get_const_data_len (type_str, type_pos, 0);
}

static dbus_bool_t
_dbus_message_has_type_interface_member (DBusMessage *message,
                                         int          type,
                                         const char  *interface,
                                         const char  *member)
{
  const char *n;

  _dbus_assert (message != NULL);
  _dbus_assert (interface != NULL);
  _dbus_assert (member != NULL);

  if (dbus_message_get_type (message) != type)
    return FALSE;

  /* Optimize by checking the short member name first
   * instead of the longer interface name
   */

  n = dbus_message_get_member (message);

  if (n && strcmp (n, member) == 0)
    {
      n = dbus_message_get_interface (message);

      if (n == NULL || strcmp (n, interface) == 0)
        return TRUE;
    }

  return FALSE;
}

/**
 * Checks whether the message is a method call with the given
 * interface and member fields.  If the message is not
 * #DBUS_MESSAGE_TYPE_METHOD_CALL, or has a different interface or
 * member field, returns #FALSE. If the interface field is missing,
 * then it will be assumed equal to the provided interface.  The D-BUS
 * protocol allows method callers to leave out the interface name.
 *
 * @param message the message
 * @param interface the name to check (must not be #NULL)
 * @param method the name to check (must not be #NULL)
 *
 * @returns #TRUE if the message is the specified method call
 */
dbus_bool_t
dbus_message_is_method_call (DBusMessage *message,
                             const char  *interface,
                             const char  *method)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (interface != NULL, FALSE);
  _dbus_return_val_if_fail (method != NULL, FALSE);
  /* don't check that interface/method are valid since it would be
   * expensive, and not catch many common errors
   */

  return _dbus_message_has_type_interface_member (message,
                                                  DBUS_MESSAGE_TYPE_METHOD_CALL,
                                                  interface, method);
}

/**
 * Checks whether the message is a signal with the given interface and
 * member fields.  If the message is not #DBUS_MESSAGE_TYPE_SIGNAL, or
 * has a different interface or member field, returns #FALSE.  If the
 * interface field in the message is missing, it is assumed to match
 * any interface you pass in to this function.
 *
 * @param message the message
 * @param interface the name to check (must not be #NULL)
 * @param signal_name the name to check (must not be #NULL)
 *
 * @returns #TRUE if the message is the specified signal
 */
dbus_bool_t
dbus_message_is_signal (DBusMessage *message,
                        const char  *interface,
                        const char  *signal_name)
{
  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (interface != NULL, FALSE);
  _dbus_return_val_if_fail (signal_name != NULL, FALSE);
  /* don't check that interface/name are valid since it would be
   * expensive, and not catch many common errors
   */

  return _dbus_message_has_type_interface_member (message,
                                                  DBUS_MESSAGE_TYPE_SIGNAL,
                                                  interface, signal_name);
}

/**
 * Checks whether the message is an error reply with the given error
 * name.  If the message is not #DBUS_MESSAGE_TYPE_ERROR, or has a
 * different name, returns #FALSE.
 *
 * @param message the message
 * @param error_name the name to check (must not be #NULL)
 *
 * @returns #TRUE if the message is the specified error
 */
dbus_bool_t
dbus_message_is_error (DBusMessage *message,
                       const char  *error_name)
{
  const char *n;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (error_name != NULL, FALSE);
  /* don't check that error_name is valid since it would be expensive,
   * and not catch many common errors
   */

  if (dbus_message_get_type (message) != DBUS_MESSAGE_TYPE_ERROR)
    return FALSE;

  n = dbus_message_get_error_name (message);

  if (n && strcmp (n, error_name) == 0)
    return TRUE;
  else
    return FALSE;
}

/**
 * Checks whether the message was sent to the given service.  If the
 * message has no service specified or has a different name, returns
 * #FALSE.
 *
 * @param message the message
 * @param service the service to check (must not be #NULL)
 *
 * @returns #TRUE if the message has the given destination service
 */
dbus_bool_t
dbus_message_has_destination (DBusMessage  *message,
                              const char   *service)
{
  const char *s;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (service != NULL, FALSE);
  /* don't check that service name is valid since it would be expensive,
   * and not catch many common errors
   */

  s = dbus_message_get_destination (message);

  if (s && strcmp (s, service) == 0)
    return TRUE;
  else
    return FALSE;
}

/**
 * Checks whether the message has the given service as its sender.  If
 * the message has no sender specified or has a different sender,
 * returns #FALSE. Note that if a peer application owns multiple
 * services, its messages will have only one of those services as the
 * sender (usually the base service). So you can't use this
 * function to prove the sender didn't own service Foo, you can
 * only use it to prove that it did.
 *
 * @todo this function is probably useless unless we make a hard guarantee
 * that the sender field in messages will always be the base service name
 *
 * @param message the message
 * @param service the service to check (must not be #NULL)
 *
 * @returns #TRUE if the message has the given origin service
 */
dbus_bool_t
dbus_message_has_sender (DBusMessage  *message,
                         const char   *service)
{
  const char *s;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (service != NULL, FALSE);
  /* don't check that service name is valid since it would be expensive,
   * and not catch many common errors
   */

  s = dbus_message_get_sender (message);

  if (s && strcmp (s, service) == 0)
    return TRUE;
  else
    return FALSE;
}

/**
 * Checks whether the message has the given signature; see
 * dbus_message_get_signature() for more details on what the signature
 * looks like.
 *
 * @param message the message
 * @param signature typecode array
 * @returns #TRUE if message has the given signature
*/
dbus_bool_t
dbus_message_has_signature (DBusMessage   *message,
                            const char    *signature)
{
  const char *s;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (signature != NULL, FALSE);
  /* don't check that signature is valid since it would be expensive,
   * and not catch many common errors
   */

  s = dbus_message_get_signature (message);

  if (s && strcmp (s, signature) == 0)
    return TRUE;
  else
    return FALSE;
}

/**
 * Sets a #DBusError based on the contents of the given
 * message. The error is only set if the message
 * is an error message, as in DBUS_MESSAGE_TYPE_ERROR.
 * The name of the error is set to the name of the message,
 * and the error message is set to the first argument
 * if the argument exists and is a string.
 *
 * The return value indicates whether the error was set (the error is
 * set if and only if the message is an error message).  So you can
 * check for an error reply and convert it to DBusError in one go:
 * @code
 *  if (dbus_set_error_from_message (error, reply))
 *    return error;
 *  else
 *    process reply;
 * @endcode
 *
 * @param error the error to set
 * @param message the message to set it from
 * @returns #TRUE if dbus_message_get_is_error() returns #TRUE for the message
 */
dbus_bool_t
dbus_set_error_from_message (DBusError   *error,
                             DBusMessage *message)
{
  const char *str;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_error_is_set (error, FALSE);

  if (dbus_message_get_type (message) != DBUS_MESSAGE_TYPE_ERROR)
    return FALSE;

  str = NULL;
  dbus_message_get_args (message, NULL,
                         DBUS_TYPE_STRING, &str,
                         DBUS_TYPE_INVALID);

  dbus_set_error (error, dbus_message_get_error_name (message),
                  str ? "%s" : NULL, str);

  return TRUE;
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
 * @todo write tests for break-loader that a) randomly delete header
 * fields and b) set string fields to zero-length and other funky
 * values.
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

  if (!_dbus_string_init (&loader->data))
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
 * @returns the loader
 */
DBusMessageLoader *
_dbus_message_loader_ref (DBusMessageLoader *loader)
{
  loader->refcount += 1;

  return loader;
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
 * @todo we need to enforce a max length on strings in header fields.
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
 * The smallest header size that can occur.  (It won't be valid due to
 * missing required header fields.) This is 4 bytes, two uint32, an
 * array length.
 */
#define DBUS_MINIMUM_HEADER_SIZE 16

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
}

/*
 * FIXME when we move the header out of the buffer, that memmoves all
 * buffered messages. Kind of crappy.
 *
 * Also we copy the header and body, which is kind of crappy.  To
 * avoid this, we have to allow header and body to be in a single
 * memory block, which is good for messages we read and bad for
 * messages we are creating. But we could move_len() the buffer into
 * this single memory block, and move_len() will just swap the buffers
 * if you're moving the entire buffer replacing the dest string.
 *
 * We could also have the message loader tell the transport how many
 * bytes to read; so it would first ask for some arbitrary number like
 * 256, then if the message was incomplete it would use the
 * header/body len to ask for exactly the size of the message (or
 * blocks the size of a typical kernel buffer for the socket). That
 * way we don't get trailing bytes in the buffer that have to be
 * memmoved. Though I suppose we also don't have a chance of reading a
 * bunch of small messages at once, so the optimization may be stupid.
 *
 * Another approach would be to keep a "start" index into
 * loader->data and only delete it occasionally, instead of after
 * each message is loaded.
 *
 * load_message() returns FALSE if not enough memory
 */
static dbus_bool_t
load_message (DBusMessageLoader *loader,
              DBusMessage       *message,
              int                byte_order,
              int                fields_array_len,
              int                header_len,
              int                body_len)
{
  dbus_bool_t oom;
  DBusValidity validity;
  const DBusString *type_str;
  int type_pos;

  oom = FALSE;

#if 0
  _dbus_verbose_bytes_of_string (&loader->data, 0, header_len /* + body_len */);
#endif

  /* 1. VALIDATE AND COPY OVER HEADER */
  _dbus_assert (_dbus_string_get_length (&message->header.data) == 0);
  _dbus_assert ((header_len + body_len) <= _dbus_string_get_length (&loader->data));

  if (!_dbus_header_load_untrusted (&message->header,
                                    &validity,
                                    byte_order,
                                    fields_array_len,
                                    header_len,
                                    body_len,
                                    &loader->data, 0,
                                    _dbus_string_get_length (&loader->data)))
    {
      _dbus_verbose ("Failed to load header for new message code %d\n", validity);
      if (validity == DBUS_VALID)
        oom = TRUE;
      goto failed;
    }

  _dbus_assert (validity == DBUS_VALID);

  message->byte_order = byte_order;

  /* 2. VALIDATE BODY */

  get_const_signature (&message->header, &type_str, &type_pos);

  /* Because the bytes_remaining arg is NULL, this validates that the
   * body is the right length
   */
  validity = _dbus_validate_body_with_reason (type_str,
                                              type_pos,
                                              byte_order,
                                              NULL,
                                              &loader->data,
                                              header_len,
                                              body_len);
  if (validity != DBUS_VALID)
    {
      _dbus_verbose ("Failed to validate message body code %d\n", validity);
      goto failed;
    }

  /* 3. COPY OVER BODY AND QUEUE MESSAGE */

  if (!_dbus_list_append (&loader->messages, message))
    {
      _dbus_verbose ("Failed to append new message to loader queue\n");
      oom = TRUE;
      goto failed;
    }

  _dbus_assert (_dbus_string_get_length (&message->body) == 0);
  _dbus_assert (_dbus_string_get_length (&loader->data) >=
                (header_len + body_len));

  if (!_dbus_string_copy_len (&loader->data, header_len, body_len, &message->body, 0))
    {
      _dbus_verbose ("Failed to move body into new message\n");
      oom = TRUE;
      goto failed;
    }

  _dbus_string_delete (&loader->data, 0, header_len + body_len);

  _dbus_assert (_dbus_string_get_length (&message->header.data) == header_len);
  _dbus_assert (_dbus_string_get_length (&message->body) == body_len);

  _dbus_verbose ("Loaded message %p\n", message);

  _dbus_assert (!oom);
  _dbus_assert (!loader->corrupted);

  return TRUE;

 failed:

  /* Clean up */

  /* does nothing if the message isn't in the list */
  _dbus_list_remove_last (&loader->messages, message);

  if (!oom)
    loader->corrupted = TRUE;

  _dbus_verbose_bytes_of_string (&loader->data, 0, _dbus_string_get_length (&loader->data));

  return !oom;
}

/**
 * Converts buffered data into messages, if we have enough data.  If
 * we don't have enough data, does nothing.
 *
 * @todo we need to check that the proper named header fields exist
 * for each message type.
 *
 * @todo If a message has unknown type, we should probably eat it
 * right here rather than passing it out to applications.  However
 * it's not an error to see messages of unknown type.
 *
 * @param loader the loader.
 * @returns #TRUE if we had enough memory to finish.
 */
dbus_bool_t
_dbus_message_loader_queue_messages (DBusMessageLoader *loader)
{
  while (!loader->corrupted &&
         _dbus_string_get_length (&loader->data) >= DBUS_MINIMUM_HEADER_SIZE)
    {
      DBusValidity validity;
      int byte_order, fields_array_len, header_len, body_len;

      if (_dbus_header_have_message_untrusted (loader->max_message_size,
                                               &validity,
                                               &byte_order,
                                               &fields_array_len,
                                               &header_len,
                                               &body_len,
                                               &loader->data, 0,
                                               _dbus_string_get_length (&loader->data)))
        {
          DBusMessage *message;

          _dbus_assert (validity == DBUS_VALID);

          message = dbus_message_new_empty_header ();
          if (message == NULL)
            return FALSE;

          if (!load_message (loader, message,
                             byte_order, fields_array_len,
                             header_len, body_len))
            {
              dbus_message_unref (message);
              return FALSE;
            }
	}
      else
        {
          _dbus_verbose ("Initial peek at header says we don't have a whole message yet, or data broken with invalid code %d\n",
                         validity);
          if (validity != DBUS_VALID)
            loader->corrupted = TRUE;
          return TRUE;
        }
    }

  return TRUE;
}

/**
 * Peeks at first loaded message, returns #NULL if no messages have
 * been queued.
 *
 * @param loader the loader.
 * @returns the next message, or #NULL if none.
 */
DBusMessage*
_dbus_message_loader_peek_message (DBusMessageLoader *loader)
{
  if (loader->messages)
    return loader->messages->data;
  else
    return NULL;
}

/**
 * Pops a loaded message (passing ownership of the message
 * to the caller). Returns #NULL if no messages have been
 * queued.
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
 * Pops a loaded message inside a list link (passing ownership of the
 * message and link to the caller). Returns #NULL if no messages have
 * been loaded.
 *
 * @param loader the loader.
 * @returns the next message link, or #NULL if none.
 */
DBusList*
_dbus_message_loader_pop_message_link (DBusMessageLoader *loader)
{
  return _dbus_list_pop_first_link (&loader->messages);
}

/**
 * Returns a popped message link, used to undo a pop.
 *
 * @param loader the loader
 * @param link the link with a message in it
 */
void
_dbus_message_loader_putback_message_link (DBusMessageLoader  *loader,
                                           DBusList           *link)
{
  _dbus_list_prepend_link (&loader->messages, link);
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
  if (size > DBUS_MAXIMUM_MESSAGE_LENGTH)
    {
      _dbus_verbose ("clamping requested max message size %ld to %d\n",
                     size, DBUS_MAXIMUM_MESSAGE_LENGTH);
      size = DBUS_MAXIMUM_MESSAGE_LENGTH;
    }
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

static DBusDataSlotAllocator slot_allocator;
_DBUS_DEFINE_GLOBAL_LOCK (message_slots);

/**
 * Allocates an integer ID to be used for storing application-specific
 * data on any DBusMessage. The allocated ID may then be used
 * with dbus_message_set_data() and dbus_message_get_data().
 * The passed-in slot must be initialized to -1, and is filled in
 * with the slot ID. If the passed-in slot is not -1, it's assumed
 * to be already allocated, and its refcount is incremented.
 *
 * The allocated slot is global, i.e. all DBusMessage objects will
 * have a slot with the given integer ID reserved.
 *
 * @param slot_p address of a global variable storing the slot
 * @returns #FALSE on failure (no memory)
 */
dbus_bool_t
dbus_message_allocate_data_slot (dbus_int32_t *slot_p)
{
  return _dbus_data_slot_allocator_alloc (&slot_allocator,
                                          _DBUS_LOCK_NAME (message_slots),
                                          slot_p);
}

/**
 * Deallocates a global ID for message data slots.
 * dbus_message_get_data() and dbus_message_set_data() may no
 * longer be used with this slot.  Existing data stored on existing
 * DBusMessage objects will be freed when the message is
 * finalized, but may not be retrieved (and may only be replaced if
 * someone else reallocates the slot).  When the refcount on the
 * passed-in slot reaches 0, it is set to -1.
 *
 * @param slot_p address storing the slot to deallocate
 */
void
dbus_message_free_data_slot (dbus_int32_t *slot_p)
{
  _dbus_return_if_fail (*slot_p >= 0);

  _dbus_data_slot_allocator_free (&slot_allocator, slot_p);
}

/**
 * Stores a pointer on a DBusMessage, along
 * with an optional function to be used for freeing
 * the data when the data is set again, or when
 * the message is finalized. The slot number
 * must have been allocated with dbus_message_allocate_data_slot().
 *
 * @param message the message
 * @param slot the slot number
 * @param data the data to store
 * @param free_data_func finalizer function for the data
 * @returns #TRUE if there was enough memory to store the data
 */
dbus_bool_t
dbus_message_set_data (DBusMessage     *message,
                       dbus_int32_t     slot,
                       void            *data,
                       DBusFreeFunction free_data_func)
{
  DBusFreeFunction old_free_func;
  void *old_data;
  dbus_bool_t retval;

  _dbus_return_val_if_fail (message != NULL, FALSE);
  _dbus_return_val_if_fail (slot >= 0, FALSE);

  retval = _dbus_data_slot_list_set (&slot_allocator,
                                     &message->slot_list,
                                     slot, data, free_data_func,
                                     &old_free_func, &old_data);

  if (retval)
    {
      /* Do the actual free outside the message lock */
      if (old_free_func)
        (* old_free_func) (old_data);
    }

  return retval;
}

/**
 * Retrieves data previously set with dbus_message_set_data().
 * The slot must still be allocated (must not have been freed).
 *
 * @param message the message
 * @param slot the slot to get data from
 * @returns the data, or #NULL if not found
 */
void*
dbus_message_get_data (DBusMessage   *message,
                       dbus_int32_t   slot)
{
  void *res;

  _dbus_return_val_if_fail (message != NULL, NULL);

  res = _dbus_data_slot_list_get (&slot_allocator,
                                  &message->slot_list,
                                  slot);

  return res;
}

/**
 * Utility function to convert a machine-readable (not translated)
 * string into a D-BUS message type.
 *
 * @code
 *   "method_call"    -> DBUS_MESSAGE_TYPE_METHOD_CALL
 *   "method_return"  -> DBUS_MESSAGE_TYPE_METHOD_RETURN
 *   "signal"         -> DBUS_MESSAGE_TYPE_SIGNAL
 *   "error"          -> DBUS_MESSAGE_TYPE_ERROR
 *   anything else    -> DBUS_MESSAGE_TYPE_INVALID
 * @endcode
 *
 */
int
dbus_message_type_from_string (const char *type_str)
{
  if (strcmp (type_str, "method_call") == 0)
    return DBUS_MESSAGE_TYPE_METHOD_CALL;
  if (strcmp (type_str, "method_return") == 0)
    return DBUS_MESSAGE_TYPE_METHOD_RETURN;
  else if (strcmp (type_str, "signal") == 0)
    return DBUS_MESSAGE_TYPE_SIGNAL;
  else if (strcmp (type_str, "error") == 0)
    return DBUS_MESSAGE_TYPE_ERROR;
  else
    return DBUS_MESSAGE_TYPE_INVALID;
}

/**
 * Utility function to convert a D-BUS message type into a
 * machine-readable string (not translated).
 *
 * @code
 *   DBUS_MESSAGE_TYPE_METHOD_CALL    -> "method_call"
 *   DBUS_MESSAGE_TYPE_METHOD_RETURN  -> "method_return"
 *   DBUS_MESSAGE_TYPE_SIGNAL         -> "signal"
 *   DBUS_MESSAGE_TYPE_ERROR          -> "error"
 *   DBUS_MESSAGE_TYPE_INVALID        -> "invalid"
 * @endcode
 *
 */
const char *
dbus_message_type_to_string (int type)
{
  switch (type)
    {
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
      return "method_call";
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
      return "method_return";
    case DBUS_MESSAGE_TYPE_SIGNAL:
      return "signal";
    case DBUS_MESSAGE_TYPE_ERROR:
      return "error";
    default:
      return "invalid";
    }
}

/** @} */
#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>
#include <stdlib.h>

static dbus_bool_t
check_have_valid_message (DBusMessageLoader *loader)
{
  DBusMessage *message;
  dbus_bool_t retval;

  message = NULL;
  retval = FALSE;

  if (!_dbus_message_loader_queue_messages (loader))
    _dbus_assert_not_reached ("no memory to queue messages");

  if (_dbus_message_loader_get_is_corrupted (loader))
    {
      _dbus_warn ("loader corrupted on message that was expected to be valid\n");
      goto failed;
    }

  message = _dbus_message_loader_pop_message (loader);
  if (message == NULL)
    {
      _dbus_warn ("didn't load message that was expected to be valid (message not popped)\n");
      goto failed;
    }

  if (_dbus_string_get_length (&loader->data) > 0)
    {
      _dbus_warn ("had leftover bytes from expected-to-be-valid single message\n");
      goto failed;
    }

#if 0
  /* FIXME */
  /* Verify that we're able to properly deal with the message.
   * For example, this would detect improper handling of messages
   * in nonstandard byte order.
   */
  if (!check_message_handling (message))
    goto failed;
#endif

  retval = TRUE;

 failed:
  if (message)
    dbus_message_unref (message);

  return retval;
}

static dbus_bool_t
check_invalid_message (DBusMessageLoader *loader)
{
  dbus_bool_t retval;

  retval = FALSE;

  if (!_dbus_message_loader_queue_messages (loader))
    _dbus_assert_not_reached ("no memory to queue messages");

  if (!_dbus_message_loader_get_is_corrupted (loader))
    {
      _dbus_warn ("loader not corrupted on message that was expected to be invalid\n");
      goto failed;
    }

  retval = TRUE;

 failed:
  return retval;
}

static dbus_bool_t
check_incomplete_message (DBusMessageLoader *loader)
{
  DBusMessage *message;
  dbus_bool_t retval;

  message = NULL;
  retval = FALSE;

  if (!_dbus_message_loader_queue_messages (loader))
    _dbus_assert_not_reached ("no memory to queue messages");

  if (_dbus_message_loader_get_is_corrupted (loader))
    {
      _dbus_warn ("loader corrupted on message that was expected to be valid (but incomplete)\n");
      goto failed;
    }

  message = _dbus_message_loader_pop_message (loader);
  if (message != NULL)
    {
      _dbus_warn ("loaded message that was expected to be incomplete\n");
      goto failed;
    }

  retval = TRUE;

 failed:
  if (message)
    dbus_message_unref (message);
  return retval;
}

static dbus_bool_t
check_loader_results (DBusMessageLoader      *loader,
                      DBusMessageValidity     validity)
{
  if (!_dbus_message_loader_queue_messages (loader))
    _dbus_assert_not_reached ("no memory to queue messages");

  switch (validity)
    {
    case _DBUS_MESSAGE_VALID:
      return check_have_valid_message (loader);
    case _DBUS_MESSAGE_INVALID:
      return check_invalid_message (loader);
    case _DBUS_MESSAGE_INCOMPLETE:
      return check_incomplete_message (loader);
    case _DBUS_MESSAGE_UNKNOWN:
      return TRUE;
    }

  _dbus_assert_not_reached ("bad DBusMessageValidity");
  return FALSE;
}


/**
 * Loads the message in the given message file.
 *
 * @param filename filename to load
 * @param is_raw if #TRUE load as binary data, if #FALSE as message builder language
 * @param data string to load message into
 * @returns #TRUE if the message was loaded
 */
dbus_bool_t
dbus_internal_do_not_use_load_message_file (const DBusString    *filename,
                                            dbus_bool_t          is_raw,
                                            DBusString          *data)
{
  dbus_bool_t retval;

  retval = FALSE;

  if (is_raw)
    {
      DBusError error;

      _dbus_verbose ("Loading raw %s\n", _dbus_string_get_const_data (filename));
      dbus_error_init (&error);
      if (!_dbus_file_get_contents (data, filename, &error))
        {
          _dbus_warn ("Could not load message file %s: %s\n",
                      _dbus_string_get_const_data (filename),
                      error.message);
          dbus_error_free (&error);
          goto failed;
        }
    }
  else
    {
      if (FALSE) /* Message builder disabled, probably permanently,
                  * I want to do it another way
                  */
        {
          _dbus_warn ("Could not load message file %s\n",
                      _dbus_string_get_const_data (filename));
          goto failed;
        }
    }

  retval = TRUE;

 failed:

  return retval;
}

/**
 * Tries loading the message in the given message file
 * and verifies that DBusMessageLoader can handle it.
 *
 * @param filename filename to load
 * @param is_raw if #TRUE load as binary data, if #FALSE as message builder language
 * @param expected_validity what the message has to be like to return #TRUE
 * @returns #TRUE if the message has the expected validity
 */
dbus_bool_t
dbus_internal_do_not_use_try_message_file (const DBusString    *filename,
                                           dbus_bool_t          is_raw,
                                           DBusMessageValidity  expected_validity)
{
  DBusString data;
  dbus_bool_t retval;

  retval = FALSE;

  if (!_dbus_string_init (&data))
    _dbus_assert_not_reached ("could not allocate string\n");

  if (!dbus_internal_do_not_use_load_message_file (filename, is_raw,
                                                   &data))
    goto failed;

  retval = dbus_internal_do_not_use_try_message_data (&data, expected_validity);

 failed:

  if (!retval)
    {
      if (_dbus_string_get_length (&data) > 0)
        _dbus_verbose_bytes_of_string (&data, 0,
                                       _dbus_string_get_length (&data));

      _dbus_warn ("Failed message loader test on %s\n",
                  _dbus_string_get_const_data (filename));
    }

  _dbus_string_free (&data);

  return retval;
}

/**
 * Tries loading the given message data.
 *
 *
 * @param data the message data
 * @param expected_validity what the message has to be like to return #TRUE
 * @returns #TRUE if the message has the expected validity
 */
dbus_bool_t
dbus_internal_do_not_use_try_message_data (const DBusString    *data,
                                           DBusMessageValidity  expected_validity)
{
  DBusMessageLoader *loader;
  dbus_bool_t retval;
  int len;
  int i;

  loader = NULL;
  retval = FALSE;

  /* Write the data one byte at a time */

  loader = _dbus_message_loader_new ();

  /* check some trivial loader functions */
  _dbus_message_loader_ref (loader);
  _dbus_message_loader_unref (loader);
  _dbus_message_loader_get_max_message_size (loader);

  len = _dbus_string_get_length (data);
  for (i = 0; i < len; i++)
    {
      DBusString *buffer;

      _dbus_message_loader_get_buffer (loader, &buffer);
      _dbus_string_append_byte (buffer,
                                _dbus_string_get_byte (data, i));
      _dbus_message_loader_return_buffer (loader, buffer, 1);
    }

  if (!check_loader_results (loader, expected_validity))
    goto failed;

  _dbus_message_loader_unref (loader);
  loader = NULL;

  /* Write the data all at once */

  loader = _dbus_message_loader_new ();

  {
    DBusString *buffer;

    _dbus_message_loader_get_buffer (loader, &buffer);
    _dbus_string_copy (data, 0, buffer,
                       _dbus_string_get_length (buffer));
    _dbus_message_loader_return_buffer (loader, buffer, 1);
  }

  if (!check_loader_results (loader, expected_validity))
    goto failed;

  _dbus_message_loader_unref (loader);
  loader = NULL;

  /* Write the data 2 bytes at a time */

  loader = _dbus_message_loader_new ();

  len = _dbus_string_get_length (data);
  for (i = 0; i < len; i += 2)
    {
      DBusString *buffer;

      _dbus_message_loader_get_buffer (loader, &buffer);
      _dbus_string_append_byte (buffer,
                                _dbus_string_get_byte (data, i));
      if ((i+1) < len)
        _dbus_string_append_byte (buffer,
                                  _dbus_string_get_byte (data, i+1));
      _dbus_message_loader_return_buffer (loader, buffer, 1);
    }

  if (!check_loader_results (loader, expected_validity))
    goto failed;

  _dbus_message_loader_unref (loader);
  loader = NULL;

  retval = TRUE;

 failed:

  if (loader)
    _dbus_message_loader_unref (loader);

  return retval;
}

static dbus_bool_t
process_test_subdir (const DBusString          *test_base_dir,
                     const char                *subdir,
                     DBusMessageValidity        validity,
                     DBusForeachMessageFileFunc function,
                     void                      *user_data)
{
  DBusString test_directory;
  DBusString filename;
  DBusDirIter *dir;
  dbus_bool_t retval;
  DBusError error;

  retval = FALSE;
  dir = NULL;

  if (!_dbus_string_init (&test_directory))
    _dbus_assert_not_reached ("didn't allocate test_directory\n");

  _dbus_string_init_const (&filename, subdir);

  if (!_dbus_string_copy (test_base_dir, 0,
                          &test_directory, 0))
    _dbus_assert_not_reached ("couldn't copy test_base_dir to test_directory");

  if (!_dbus_concat_dir_and_file (&test_directory, &filename))
    _dbus_assert_not_reached ("couldn't allocate full path");

  _dbus_string_free (&filename);
  if (!_dbus_string_init (&filename))
    _dbus_assert_not_reached ("didn't allocate filename string\n");

  dbus_error_init (&error);
  dir = _dbus_directory_open (&test_directory, &error);
  if (dir == NULL)
    {
      _dbus_warn ("Could not open %s: %s\n",
                  _dbus_string_get_const_data (&test_directory),
                  error.message);
      dbus_error_free (&error);
      goto failed;
    }

  printf ("Testing %s:\n", subdir);

 next:
  while (_dbus_directory_get_next_file (dir, &filename, &error))
    {
      DBusString full_path;
      dbus_bool_t is_raw;

      if (!_dbus_string_init (&full_path))
        _dbus_assert_not_reached ("couldn't init string");

      if (!_dbus_string_copy (&test_directory, 0, &full_path, 0))
        _dbus_assert_not_reached ("couldn't copy dir to full_path");

      if (!_dbus_concat_dir_and_file (&full_path, &filename))
        _dbus_assert_not_reached ("couldn't concat file to dir");

      if (_dbus_string_ends_with_c_str (&filename, ".message"))
        is_raw = FALSE;
      else if (_dbus_string_ends_with_c_str (&filename, ".message-raw"))
        is_raw = TRUE;
      else
        {
          _dbus_verbose ("Skipping non-.message file %s\n",
                         _dbus_string_get_const_data (&filename));
	  _dbus_string_free (&full_path);
          goto next;
        }

      printf ("    %s\n",
              _dbus_string_get_const_data (&filename));

      _dbus_verbose (" expecting %s for %s\n",
                     validity == _DBUS_MESSAGE_VALID ? "valid" :
                     (validity == _DBUS_MESSAGE_INVALID ? "invalid" :
                      (validity == _DBUS_MESSAGE_INCOMPLETE ? "incomplete" : "unknown")),
                     _dbus_string_get_const_data (&filename));

      if (! (*function) (&full_path, is_raw, validity, user_data))
        {
          _dbus_string_free (&full_path);
          goto failed;
        }
      else
        _dbus_string_free (&full_path);
    }

  if (dbus_error_is_set (&error))
    {
      _dbus_warn ("Could not get next file in %s: %s\n",
                  _dbus_string_get_const_data (&test_directory),
                  error.message);
      dbus_error_free (&error);
      goto failed;
    }

  retval = TRUE;

 failed:

  if (dir)
    _dbus_directory_close (dir);
  _dbus_string_free (&test_directory);
  _dbus_string_free (&filename);

  return retval;
}

/**
 * Runs the given function on every message file in the test suite.
 * The function should return #FALSE on test failure or fatal error.
 *
 * @param test_data_dir root dir of the test suite data files (top_srcdir/test/data)
 * @param func the function to run
 * @param user_data data for function
 * @returns #FALSE if there's a failure
 */
dbus_bool_t
dbus_internal_do_not_use_foreach_message_file (const char                *test_data_dir,
                                               DBusForeachMessageFileFunc func,
                                               void                      *user_data)
{
  DBusString test_directory;
  dbus_bool_t retval;

  retval = FALSE;

  _dbus_string_init_const (&test_directory, test_data_dir);

  if (!process_test_subdir (&test_directory, "valid-messages",
                            _DBUS_MESSAGE_VALID, func, user_data))
    goto failed;

  if (!process_test_subdir (&test_directory, "invalid-messages",
                            _DBUS_MESSAGE_INVALID, func, user_data))
    goto failed;

  if (!process_test_subdir (&test_directory, "incomplete-messages",
                            _DBUS_MESSAGE_INCOMPLETE, func, user_data))
    goto failed;

  retval = TRUE;

 failed:

  _dbus_string_free (&test_directory);

  return retval;
}

#define GET_AND_CHECK(iter, typename, literal)                                  \
  do {                                                                          \
    if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_##typename)         \
      _dbus_assert_not_reached ("got wrong argument type from message iter");   \
    dbus_message_iter_get_basic (&iter, &v_##typename);                         \
    if (v_##typename != literal)                                                \
      _dbus_assert_not_reached ("got wrong value from message iter");           \
  } while (0)

#define GET_AND_CHECK_STRCMP(iter, typename, literal)                           \
  do {                                                                          \
    if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_##typename)         \
      _dbus_assert_not_reached ("got wrong argument type from message iter");   \
    dbus_message_iter_get_basic (&iter, &v_##typename);                         \
    if (strcmp (v_##typename, literal) != 0)                                    \
      _dbus_assert_not_reached ("got wrong value from message iter");           \
  } while (0)

#define GET_AND_CHECK_AND_NEXT(iter, typename, literal)         \
  do {                                                          \
    GET_AND_CHECK(iter, typename, literal);                     \
    if (!dbus_message_iter_next (&iter))                        \
      _dbus_assert_not_reached ("failed to move iter to next"); \
  } while (0)

#define GET_AND_CHECK_STRCMP_AND_NEXT(iter, typename, literal)  \
  do {                                                          \
    GET_AND_CHECK_STRCMP(iter, typename, literal);              \
    if (!dbus_message_iter_next (&iter))                        \
      _dbus_assert_not_reached ("failed to move iter to next"); \
  } while (0)

static void
message_iter_test (DBusMessage *message)
{
  DBusMessageIter iter, array, array2;
  const char *v_STRING;
  double v_DOUBLE;
  dbus_int32_t v_INT32;
  dbus_uint32_t v_UINT32;
#ifdef DBUS_HAVE_INT64
  dbus_int64_t v_INT64;
  dbus_uint64_t v_UINT64;
#endif
  unsigned char v_BYTE;
  unsigned char v_BOOLEAN;

  const dbus_int32_t *our_int_array;
  int len;

  dbus_message_iter_init (message, &iter);

  GET_AND_CHECK_STRCMP_AND_NEXT (iter, STRING, "Test string");
  GET_AND_CHECK_AND_NEXT (iter, INT32, -0x12345678);
  GET_AND_CHECK_AND_NEXT (iter, UINT32, 0xedd1e);
  GET_AND_CHECK_AND_NEXT (iter, DOUBLE, 3.14159);

  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_ARRAY)
    _dbus_assert_not_reached ("Argument type not an array");

  if (dbus_message_iter_get_array_type (&iter) != DBUS_TYPE_DOUBLE)
    _dbus_assert_not_reached ("Array type not double");

  dbus_message_iter_recurse (&iter, &array);

  GET_AND_CHECK_AND_NEXT (array, DOUBLE, 1.5);
  GET_AND_CHECK (array, DOUBLE, 2.5);

  if (dbus_message_iter_next (&array))
    _dbus_assert_not_reached ("Didn't reach end of array");

  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");

  GET_AND_CHECK_AND_NEXT (iter, BYTE, 0xF0);

  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_ARRAY)
    _dbus_assert_not_reached ("no array");

  if (dbus_message_iter_get_array_type (&iter) != DBUS_TYPE_INT32)
    _dbus_assert_not_reached ("Array type not int32");

  /* Empty array */
  dbus_message_iter_recurse (&iter, &array);

  if (dbus_message_iter_next (&array))
    _dbus_assert_not_reached ("Didn't reach end of array");

  if (!dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Reached end of arguments");

  GET_AND_CHECK (iter, BYTE, 0xF0);

  if (dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Didn't reach end of arguments");
}

static void
verify_test_message (DBusMessage *message)
{
  DBusMessageIter iter;
  DBusError error;
  dbus_int32_t our_int;
  const char *our_str;
  double our_double;
  unsigned char our_bool;
  unsigned char our_byte_1, our_byte_2;
  dbus_uint32_t our_uint32;
  const dbus_int32_t *our_uint32_array = (void*)0xdeadbeef;
  int our_uint32_array_len;
  dbus_int32_t *our_int32_array = (void*)0xdeadbeef;
  int our_int32_array_len;
#ifdef DBUS_HAVE_INT64
  dbus_int64_t our_int64;
  dbus_uint64_t our_uint64;
  dbus_int64_t *our_uint64_array = (void*)0xdeadbeef;
  int our_uint64_array_len;
  const dbus_int64_t *our_int64_array = (void*)0xdeadbeef;
  int our_int64_array_len;
#endif
  const double *our_double_array = (void*)0xdeadbeef;
  int our_double_array_len;
  const unsigned char *our_byte_array = (void*)0xdeadbeef;
  int our_byte_array_len;
  const unsigned char *our_boolean_array = (void*)0xdeadbeef;
  int our_boolean_array_len;

  dbus_message_iter_init (message, &iter);

  dbus_error_init (&error);
  if (!dbus_message_iter_get_args (&iter, &error,
				   DBUS_TYPE_INT32, &our_int,
#ifdef DBUS_HAVE_INT64
                                   DBUS_TYPE_INT64, &our_int64,
                                   DBUS_TYPE_UINT64, &our_uint64,
#endif
				   DBUS_TYPE_STRING, &our_str,
				   DBUS_TYPE_DOUBLE, &our_double,
				   DBUS_TYPE_BOOLEAN, &our_bool,
				   DBUS_TYPE_BYTE, &our_byte_1,
				   DBUS_TYPE_BYTE, &our_byte_2,
				   DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32,
                                   &our_uint32_array, &our_uint32_array_len,
                                   DBUS_TYPE_ARRAY, DBUS_TYPE_INT32,
                                   &our_int32_array, &our_int32_array_len,
#ifdef DBUS_HAVE_INT64
				   DBUS_TYPE_ARRAY, DBUS_TYPE_UINT64,
                                   &our_uint64_array, &our_uint64_array_len,
                                   DBUS_TYPE_ARRAY, DBUS_TYPE_INT64,
                                   &our_int64_array, &our_int64_array_len,
#endif
                                   DBUS_TYPE_ARRAY, DBUS_TYPE_DOUBLE,
                                   &our_double_array, &our_double_array_len,
                                   DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
                                   &our_byte_array, &our_byte_array_len,
                                   DBUS_TYPE_ARRAY, DBUS_TYPE_BOOLEAN,
                                   &our_boolean_array, &our_boolean_array_len,
				   0))
    {
      _dbus_warn ("error: %s - %s\n", error.name,
                  (error.message != NULL) ? error.message : "no message");
      _dbus_assert_not_reached ("Could not get arguments");
    }

  if (our_int != -0x12345678)
    _dbus_assert_not_reached ("integers differ!");

#ifdef DBUS_HAVE_INT64
  if (our_int64 != DBUS_INT64_CONSTANT (-0x123456789abcd))
    _dbus_assert_not_reached ("64-bit integers differ!");
  if (our_uint64 != DBUS_UINT64_CONSTANT (0x123456789abcd))
    _dbus_assert_not_reached ("64-bit unsigned integers differ!");
#endif

  if (our_double != 3.14159)
    _dbus_assert_not_reached ("doubles differ!");

  if (strcmp (our_str, "Test string") != 0)
    _dbus_assert_not_reached ("strings differ!");

  if (!our_bool)
    _dbus_assert_not_reached ("booleans differ");

  if (our_byte_1 != 42)
    _dbus_assert_not_reached ("bytes differ!");

  if (our_byte_2 != 24)
    _dbus_assert_not_reached ("bytes differ!");

  if (our_uint32_array_len != 4 ||
      our_uint32_array[0] != 0x12345678 ||
      our_uint32_array[1] != 0x23456781 ||
      our_uint32_array[2] != 0x34567812 ||
      our_uint32_array[3] != 0x45678123)
    _dbus_assert_not_reached ("uint array differs");

  if (our_int32_array_len != 4 ||
      our_int32_array[0] != 0x12345678 ||
      our_int32_array[1] != -0x23456781 ||
      our_int32_array[2] != 0x34567812 ||
      our_int32_array[3] != -0x45678123)
    _dbus_assert_not_reached ("int array differs");

#ifdef DBUS_HAVE_INT64
  if (our_uint64_array_len != 4 ||
      our_uint64_array[0] != 0x12345678 ||
      our_uint64_array[1] != 0x23456781 ||
      our_uint64_array[2] != 0x34567812 ||
      our_uint64_array[3] != 0x45678123)
    _dbus_assert_not_reached ("uint64 array differs");

  if (our_int64_array_len != 4 ||
      our_int64_array[0] != 0x12345678 ||
      our_int64_array[1] != -0x23456781 ||
      our_int64_array[2] != 0x34567812 ||
      our_int64_array[3] != -0x45678123)
    _dbus_assert_not_reached ("int64 array differs");
#endif /* DBUS_HAVE_INT64 */

  if (our_double_array_len != 3)
    _dbus_assert_not_reached ("double array had wrong length");

  /* On all IEEE machines (i.e. everything sane) exact equality
   * should be preserved over the wire
   */
  if (our_double_array[0] != 0.1234 ||
      our_double_array[1] != 9876.54321 ||
      our_double_array[2] != -300.0)
    _dbus_assert_not_reached ("double array had wrong values");

  if (our_byte_array_len != 4)
    _dbus_assert_not_reached ("byte array had wrong length");

  if (our_byte_array[0] != 'a' ||
      our_byte_array[1] != 'b' ||
      our_byte_array[2] != 'c' ||
      our_byte_array[3] != 234)
    _dbus_assert_not_reached ("byte array had wrong values");

  if (our_boolean_array_len != 5)
    _dbus_assert_not_reached ("bool array had wrong length");

  if (our_boolean_array[0] != TRUE ||
      our_boolean_array[1] != FALSE ||
      our_boolean_array[2] != TRUE ||
      our_boolean_array[3] != TRUE ||
      our_boolean_array[4] != FALSE)
    _dbus_assert_not_reached ("bool array had wrong values");

  if (dbus_message_iter_next (&iter))
    _dbus_assert_not_reached ("Didn't reach end of arguments");
}

/**
 * @ingroup DBusMessageInternals
 * Unit test for DBusMessage.
 *
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_message_test (const char *test_data_dir)
{
  DBusMessage *message;
  DBusMessageLoader *loader;
  DBusMessageIter iter, child_iter, child_iter2, child_iter3;
  int i;
  const char *data;
  DBusMessage *copy;
  const char *name1;
  const char *name2;
  const dbus_uint32_t our_uint32_array[] =
    { 0x12345678, 0x23456781, 0x34567812, 0x45678123 };
  const dbus_uint32_t our_int32_array[] =
    { 0x12345678, -0x23456781, 0x34567812, -0x45678123 };
  const dbus_uint32_t *v_ARRAY_UINT32 = our_uint32_array;
  const dbus_int32_t *v_ARRAY_INT32 = our_int32_array;
#ifdef DBUS_HAVE_INT64
  const dbus_uint64_t our_uint64_array[] =
    { 0x12345678, 0x23456781, 0x34567812, 0x45678123 };
  const dbus_uint64_t our_int64_array[] =
    { 0x12345678, -0x23456781, 0x34567812, -0x45678123 };
  const dbus_uint64_t *v_ARRAY_UINT64 = our_uint64_array;
  const dbus_int64_t *v_ARRAY_INT64 = our_int64_array;
#endif
  const char *our_string_array[] = { "Foo", "bar", "", "woo woo woo woo" };
  const char **v_ARRAY_STRING = our_string_array;
  const double our_double_array[] = { 0.1234, 9876.54321, -300.0 };
  const double *v_ARRAY_DOUBLE = our_double_array;
  const unsigned char our_byte_array[] = { 'a', 'b', 'c', 234 };
  const unsigned char *v_ARRAY_BYTE = our_byte_array;
  const unsigned char our_boolean_array[] = { TRUE, FALSE, TRUE, TRUE, FALSE };
  const unsigned char *v_ARRAY_BOOLEAN = our_boolean_array;
  char sig[64];
  const char *s;
  char *t;
  DBusError error;
  const char *v_STRING;
  double v_DOUBLE;
  dbus_int32_t v_INT32;
  dbus_uint32_t v_UINT32;
#ifdef DBUS_HAVE_INT64
  dbus_int64_t v_INT64;
  dbus_uint64_t v_UINT64;
#endif
  unsigned char v_BYTE;
  unsigned char v2_BYTE;
  unsigned char v_BOOLEAN;

  _dbus_assert (sizeof (DBusMessageRealIter) <= sizeof (DBusMessageIter));

  message = dbus_message_new_method_call ("org.freedesktop.DBus.TestService",
                                          "/org/freedesktop/TestPath",
                                          "Foo.TestInterface",
                                          "TestMethod");
  _dbus_assert (dbus_message_has_destination (message, "org.freedesktop.DBus.TestService"));
  _dbus_assert (dbus_message_is_method_call (message, "Foo.TestInterface",
                                             "TestMethod"));
  _dbus_assert (strcmp (dbus_message_get_path (message),
                        "/org/freedesktop/TestPath") == 0);
  _dbus_message_set_serial (message, 1234);

  /* string length including nul byte not a multiple of 4 */
  if (!dbus_message_set_sender (message, "org.foo.bar1"))
    _dbus_assert_not_reached ("out of memory");

  _dbus_assert (dbus_message_has_sender (message, "org.foo.bar1"));
  dbus_message_set_reply_serial (message, 5678);

  _dbus_verbose_bytes_of_string (&message->header.data, 0,
                                 _dbus_string_get_length (&message->header.data));
  _dbus_verbose_bytes_of_string (&message->body, 0,
                                 _dbus_string_get_length (&message->body));

  if (!dbus_message_set_sender (message, NULL))
    _dbus_assert_not_reached ("out of memory");


  _dbus_verbose_bytes_of_string (&message->header.data, 0,
                                 _dbus_string_get_length (&message->header.data));
  _dbus_verbose_bytes_of_string (&message->body, 0,
                                 _dbus_string_get_length (&message->body));


  _dbus_assert (!dbus_message_has_sender (message, "org.foo.bar1"));
  _dbus_assert (dbus_message_get_serial (message) == 1234);
  _dbus_assert (dbus_message_get_reply_serial (message) == 5678);
  _dbus_assert (dbus_message_has_destination (message, "org.freedesktop.DBus.TestService"));

  _dbus_assert (dbus_message_get_no_reply (message) == FALSE);
  dbus_message_set_no_reply (message, TRUE);
  _dbus_assert (dbus_message_get_no_reply (message) == TRUE);
  dbus_message_set_no_reply (message, FALSE);
  _dbus_assert (dbus_message_get_no_reply (message) == FALSE);

  /* Set/get some header fields */

  if (!dbus_message_set_path (message, "/foo"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_path (message),
                        "/foo") == 0);

  if (!dbus_message_set_interface (message, "org.Foo"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_interface (message),
                        "org.Foo") == 0);

  if (!dbus_message_set_member (message, "Bar"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_member (message),
                        "Bar") == 0);

  /* Set/get them with longer values */
  if (!dbus_message_set_path (message, "/foo/bar"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_path (message),
                        "/foo/bar") == 0);

  if (!dbus_message_set_interface (message, "org.Foo.Bar"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_interface (message),
                        "org.Foo.Bar") == 0);

  if (!dbus_message_set_member (message, "BarFoo"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_member (message),
                        "BarFoo") == 0);

  /* Realloc shorter again */

  if (!dbus_message_set_path (message, "/foo"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_path (message),
                        "/foo") == 0);

  if (!dbus_message_set_interface (message, "org.Foo"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_interface (message),
                        "org.Foo") == 0);

  if (!dbus_message_set_member (message, "Bar"))
    _dbus_assert_not_reached ("out of memory");
  _dbus_assert (strcmp (dbus_message_get_member (message),
                        "Bar") == 0);

  dbus_message_unref (message);

  /* Test the vararg functions */
  message = dbus_message_new_method_call ("org.freedesktop.DBus.TestService",
                                          "/org/freedesktop/TestPath",
                                          "Foo.TestInterface",
                                          "TestMethod");
  _dbus_message_set_serial (message, 1);

  v_INT32 = -0x12345678;
#ifdef DBUS_HAVE_INT64
  v_INT64 = DBUS_INT64_CONSTANT (-0x123456789abcd);
  v_UINT64 = DBUS_UINT64_CONSTANT (0x123456789abcd);
#endif
  v_STRING = "Test string";
  v_DOUBLE = 3.14159;
  v_BOOLEAN = TRUE;
  v_BYTE = 42;
  v2_BYTE = 24;

  dbus_message_append_args (message,
			    DBUS_TYPE_INT32, &v_INT32,
#ifdef DBUS_HAVE_INT64
                            DBUS_TYPE_INT64, &v_INT64,
                            DBUS_TYPE_UINT64, &v_UINT64,
#endif
			    DBUS_TYPE_STRING, &v_STRING,
			    DBUS_TYPE_DOUBLE, &v_DOUBLE,
			    DBUS_TYPE_BOOLEAN, &v_BOOLEAN,
			    DBUS_TYPE_BYTE, &v_BYTE,
			    DBUS_TYPE_BYTE, &v2_BYTE,
			    DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, &v_ARRAY_UINT32,
                            _DBUS_N_ELEMENTS (our_uint32_array),
                            DBUS_TYPE_ARRAY, DBUS_TYPE_INT32, &v_ARRAY_INT32,
                            _DBUS_N_ELEMENTS (our_int32_array),
#ifdef DBUS_HAVE_INT64
                            DBUS_TYPE_ARRAY, DBUS_TYPE_UINT64, &v_ARRAY_UINT64,
                            _DBUS_N_ELEMENTS (our_uint64_array),
                            DBUS_TYPE_ARRAY, DBUS_TYPE_INT64, &v_ARRAY_INT64,
                            _DBUS_N_ELEMENTS (our_int64_array),
#endif
                            DBUS_TYPE_ARRAY, DBUS_TYPE_DOUBLE, &v_ARRAY_DOUBLE,
                            _DBUS_N_ELEMENTS (our_double_array),
                            DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, &v_ARRAY_BYTE,
                            _DBUS_N_ELEMENTS (our_byte_array),
                            DBUS_TYPE_ARRAY, DBUS_TYPE_BOOLEAN, &v_ARRAY_BOOLEAN,
                            _DBUS_N_ELEMENTS (our_boolean_array),
			    DBUS_TYPE_INVALID);

  i = 0;
  sig[i++] = DBUS_TYPE_INT32;
#ifdef DBUS_HAVE_INT64
  sig[i++] = DBUS_TYPE_INT64;
  sig[i++] = DBUS_TYPE_UINT64;
#endif
  sig[i++] = DBUS_TYPE_STRING;
  sig[i++] = DBUS_TYPE_DOUBLE;
  sig[i++] = DBUS_TYPE_BOOLEAN;
  sig[i++] = DBUS_TYPE_BYTE;
  sig[i++] = DBUS_TYPE_BYTE;
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_UINT32;
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_INT32;
#ifdef DBUS_HAVE_INT64
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_UINT64;
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_INT64;
#endif
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_DOUBLE;
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_BYTE;
  sig[i++] = DBUS_TYPE_ARRAY;
  sig[i++] = DBUS_TYPE_BOOLEAN;
  sig[i++] = DBUS_TYPE_INVALID;

  _dbus_assert (i < (int) _DBUS_N_ELEMENTS (sig));

  _dbus_verbose ("HEADER\n");
  _dbus_verbose_bytes_of_string (&message->header.data, 0,
                                 _dbus_string_get_length (&message->header.data));
  _dbus_verbose ("BODY\n");
  _dbus_verbose_bytes_of_string (&message->body, 0,
                                 _dbus_string_get_length (&message->body));

  _dbus_verbose ("Signature expected \"%s\" actual \"%s\"\n",
                 sig, dbus_message_get_signature (message));

  s = dbus_message_get_signature (message);

  _dbus_assert (dbus_message_has_signature (message, sig));
  _dbus_assert (strcmp (s, sig) == 0);

  verify_test_message (message);

  copy = dbus_message_copy (message);

  _dbus_assert (dbus_message_get_reply_serial (message) ==
                dbus_message_get_reply_serial (copy));
  _dbus_assert (message->header.padding == copy->header.padding);

  _dbus_assert (_dbus_string_get_length (&message->header.data) ==
                _dbus_string_get_length (&copy->header.data));

  _dbus_assert (_dbus_string_get_length (&message->body) ==
                _dbus_string_get_length (&copy->body));

  verify_test_message (copy);

  name1 = dbus_message_get_interface (message);
  name2 = dbus_message_get_interface (copy);

  _dbus_assert (strcmp (name1, name2) == 0);

  name1 = dbus_message_get_member (message);
  name2 = dbus_message_get_member (copy);

  _dbus_assert (strcmp (name1, name2) == 0);

  dbus_message_unref (message);
  dbus_message_unref (copy);

#if 0
  /* FIXME */
  message = dbus_message_new_method_call ("org.freedesktop.DBus.TestService",
                                          "/org/freedesktop/TestPath",
                                          "Foo.TestInterface",
                                          "TestMethod");

  _dbus_message_set_serial (message, 1);
  dbus_message_set_reply_serial (message, 0x12345678);

  dbus_message_iter_init_append (message, &iter);
  dbus_message_iter_append_string (&iter, "Test string");
  dbus_message_iter_append_int32 (&iter, -0x12345678);
  dbus_message_iter_append_uint32 (&iter, 0xedd1e);
  dbus_message_iter_append_double (&iter, 3.14159);

  dbus_message_iter_append_array (&iter, &child_iter, DBUS_TYPE_DOUBLE);
  dbus_message_iter_append_double (&child_iter, 1.5);
  dbus_message_iter_append_double (&child_iter, 2.5);

  /* dict */
  dbus_message_iter_append_dict (&iter, &child_iter);
  dbus_message_iter_append_dict_key (&child_iter, "test");
  dbus_message_iter_append_uint32 (&child_iter, 0xDEADBEEF);

  /* dict (in dict) */
  dbus_message_iter_append_dict_key (&child_iter, "testdict");
  dbus_message_iter_append_dict (&child_iter, &child_iter2);

  dbus_message_iter_append_dict_key (&child_iter2, "dictkey");
  dbus_message_iter_append_string (&child_iter2, "dictvalue");

  /* array of array of int32  (in dict) */
  dbus_message_iter_append_dict_key (&child_iter, "array");
  dbus_message_iter_append_array (&child_iter, &child_iter2, DBUS_TYPE_ARRAY);
  dbus_message_iter_append_array (&child_iter2, &child_iter3, DBUS_TYPE_INT32);
  dbus_message_iter_append_int32 (&child_iter3, 0x12345678);
  dbus_message_iter_append_int32 (&child_iter3, 0x23456781);
  _dbus_warn ("next call expected to fail with wrong array type\n");
  _dbus_assert (!dbus_message_iter_append_array (&child_iter2, &child_iter3, DBUS_TYPE_UINT32));
  dbus_message_iter_append_array (&child_iter2, &child_iter3, DBUS_TYPE_INT32);
  dbus_message_iter_append_int32 (&child_iter3, 0x34567812);
  dbus_message_iter_append_int32 (&child_iter3, 0x45678123);
  dbus_message_iter_append_int32 (&child_iter3, 0x56781234);

  dbus_message_iter_append_byte (&iter, 0xF0);

  dbus_message_iter_append_nil (&iter);

  dbus_message_iter_append_custom (&iter, "MyTypeName",
                                   "data", 5);

  dbus_message_iter_append_byte (&iter, 0xF0);

  dbus_message_iter_append_array (&iter, &child_iter, DBUS_TYPE_INT32);

  dbus_message_iter_append_byte (&iter, 0xF0);

  dbus_message_iter_append_dict (&iter, &child_iter);

  dbus_message_iter_append_byte (&iter, 0xF0);

  message_iter_test (message);

  /* Message loader test */
  _dbus_message_lock (message);
  loader = _dbus_message_loader_new ();

  /* check ref/unref */
  _dbus_message_loader_ref (loader);
  _dbus_message_loader_unref (loader);

  /* Write the header data one byte at a time */
  data = _dbus_string_get_const_data (&message->header);
  for (i = 0; i < _dbus_string_get_length (&message->header); i++)
    {
      DBusString *buffer;

      _dbus_message_loader_get_buffer (loader, &buffer);
      _dbus_string_append_byte (buffer, data[i]);
      _dbus_message_loader_return_buffer (loader, buffer, 1);
    }

  /* Write the body data one byte at a time */
  data = _dbus_string_get_const_data (&message->body);
  for (i = 0; i < _dbus_string_get_length (&message->body); i++)
    {
      DBusString *buffer;

      _dbus_message_loader_get_buffer (loader, &buffer);
      _dbus_string_append_byte (buffer, data[i]);
      _dbus_message_loader_return_buffer (loader, buffer, 1);
    }

  copy = dbus_message_copy (message); /* save for tests below */
  dbus_message_unref (message);

  /* Now pop back the message */
  if (!_dbus_message_loader_queue_messages (loader))
    _dbus_assert_not_reached ("no memory to queue messages");

  if (_dbus_message_loader_get_is_corrupted (loader))
    _dbus_assert_not_reached ("message loader corrupted");

  message = _dbus_message_loader_pop_message (loader);
  if (!message)
    _dbus_assert_not_reached ("received a NULL message");

  if (dbus_message_get_reply_serial (message) != 0x12345678)
    _dbus_assert_not_reached ("reply serial fields differ");

  message_iter_test (message);

  dbus_message_unref (message);
  _dbus_message_loader_unref (loader);

  message = dbus_message_new_method_return (copy);
  if (message == NULL)
    _dbus_assert_not_reached ("out of memory\n");
  dbus_message_unref (copy);

  if (!dbus_message_append_args (message,
                                 DBUS_TYPE_STRING, "hello",
                                 DBUS_TYPE_INVALID))
    _dbus_assert_not_reached ("no memory");

  if (!dbus_message_has_signature (message, "s"))
    _dbus_assert_not_reached ("method return has wrong signature");

  dbus_error_init (&error);
  if (!dbus_message_get_args (message, &error, DBUS_TYPE_STRING,
                              &t, DBUS_TYPE_INVALID))

    {
      _dbus_warn ("Failed to get expected string arg: %s\n", error.message);
      exit (1);
    }
  dbus_free (t);

  dbus_message_unref (message);

  /* This ServiceAcquired message used to trigger a bug in
   * setting header fields, adding to regression test.
   */
  message = dbus_message_new_signal (DBUS_PATH_ORG_FREEDESKTOP_DBUS,
                                     DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS,
                                     "ServiceAcquired");

  if (message == NULL)
    _dbus_assert_not_reached ("out of memory");

  _dbus_verbose ("Bytes after creation\n");
  _dbus_verbose_bytes_of_string (&message->header, 0,
                                 _dbus_string_get_length (&message->header));

  if (!dbus_message_set_destination (message, ":1.0") ||
      !dbus_message_append_args (message,
                                 DBUS_TYPE_STRING, ":1.0",
                                 DBUS_TYPE_INVALID))
    _dbus_assert_not_reached ("out of memory");

  _dbus_verbose ("Bytes after set_destination() and append_args()\n");
  _dbus_verbose_bytes_of_string (&message->header, 0,
                                 _dbus_string_get_length (&message->header));

  if (!dbus_message_set_sender (message, "org.freedesktop.DBus"))
    _dbus_assert_not_reached ("out of memory");

  _dbus_verbose ("Bytes after set_sender()\n");
  _dbus_verbose_bytes_of_string (&message->header, 0,
                                 _dbus_string_get_length (&message->header));

  /* When the bug happened the above set_destination() would
   * corrupt the signature
   */
  if (!dbus_message_has_signature (message, "s"))
    {
      _dbus_warn ("Signature should be 's' but is '%s'\n",
                  dbus_message_get_signature (message));
      _dbus_assert_not_reached ("signal has wrong signature");
    }

  /* have to set destination again to reproduce the bug */
  if (!dbus_message_set_destination (message, ":1.0"))
    _dbus_assert_not_reached ("out of memory");

  _dbus_verbose ("Bytes after set_destination()\n");
  _dbus_verbose_bytes_of_string (&message->header, 0,
                                 _dbus_string_get_length (&message->header));

  /* When the bug happened the above set_destination() would
   * corrupt the signature
   */
  if (!dbus_message_has_signature (message, "s"))
    {
      _dbus_warn ("Signature should be 's' but is '%s'\n",
                  dbus_message_get_signature (message));
      _dbus_assert_not_reached ("signal has wrong signature");
    }

  dbus_error_init (&error);
  if (!dbus_message_get_args (message, &error, DBUS_TYPE_STRING,
                              &t, DBUS_TYPE_INVALID))

    {
      _dbus_warn ("Failed to get expected string arg for signal: %s\n", error.message);
      exit (1);
    }
  dbus_free (t);

  dbus_message_unref (message);

  /* Now load every message in test_data_dir if we have one */
  if (test_data_dir == NULL)
    return TRUE;

  return dbus_internal_do_not_use_foreach_message_file (test_data_dir,
                                                        (DBusForeachMessageFileFunc)
                                                        dbus_internal_do_not_use_try_message_file,
                                                        NULL);

#endif /* Commented out most tests for now */

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
