/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message-factory.c Generator of valid and invalid message data for test suite
 *
 * Copyright (C) 2005 Red Hat Inc.
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

#ifdef DBUS_BUILD_TESTS
#include "dbus-message-factory.h"
#include "dbus-message-private.h"
#include "dbus-test.h"
#include <stdio.h>

#define BYTE_ORDER_OFFSET  0
#define BODY_LENGTH_OFFSET 4

static void
iter_recurse (DBusMessageDataIter *iter)
{
  iter->depth += 1;
  _dbus_assert (iter->depth < _DBUS_MESSAGE_DATA_MAX_NESTING);
}

static int
iter_get_sequence (DBusMessageDataIter *iter)
{
  return iter->sequence_nos[iter->depth];
}

static void
iter_set_sequence (DBusMessageDataIter *iter,
                   int                  sequence)
{
  iter->sequence_nos[iter->depth] = sequence;
}

static void
iter_unrecurse (DBusMessageDataIter *iter)
{
  iter->depth -= 1;
  _dbus_assert (iter->depth >= 0);
}

static void
iter_next (DBusMessageDataIter *iter)
{
  iter->sequence_nos[iter->depth] += 1;
}

static dbus_bool_t
iter_first_in_series (DBusMessageDataIter *iter)
{
  int i;

  i = iter->depth;
  while (i < _DBUS_MESSAGE_DATA_MAX_NESTING)
    {
      if (iter->sequence_nos[i] != 0)
        return FALSE;
      ++i;
    }
  return TRUE;
}

typedef dbus_bool_t (* DBusInnerGeneratorFunc)   (DBusMessageDataIter *iter,
                                                  DBusMessage        **message_p);
typedef dbus_bool_t (* DBusMessageGeneratorFunc) (DBusMessageDataIter *iter,
                                                  DBusString          *data,
                                                  DBusValidity        *expected_validity);

static void
set_reply_serial (DBusMessage *message)
{
  if (message == NULL)
    _dbus_assert_not_reached ("oom");
  if (!dbus_message_set_reply_serial (message, 100))
    _dbus_assert_not_reached ("oom");
}

static dbus_bool_t
generate_trivial_inner (DBusMessageDataIter *iter,
                        DBusMessage        **message_p)
{
  DBusMessage *message;

  switch (iter_get_sequence (iter))
    {
    case 0:
      message = dbus_message_new_method_call ("org.freedesktop.TextEditor",
                                              "/foo/bar",
                                              "org.freedesktop.DocumentFactory",
                                              "Create");
      break;
    case 1:
      message = dbus_message_new (DBUS_MESSAGE_TYPE_METHOD_RETURN);
      set_reply_serial (message);
      break;
    case 2:
      message = dbus_message_new_signal ("/foo/bar",
                                         "org.freedesktop.DocumentFactory",
                                         "Created");
      break;
    case 3:
      message = dbus_message_new (DBUS_MESSAGE_TYPE_ERROR);

      if (!dbus_message_set_error_name (message,
                                        "org.freedesktop.TestErrorName"))
        _dbus_assert_not_reached ("oom");
      
      {
        DBusMessageIter iter;
        const char *v_STRING = "This is an error";
        
        dbus_message_iter_init_append (message, &iter);
        if (!dbus_message_iter_append_basic (&iter,
                                             DBUS_TYPE_STRING,
                                             &v_STRING))
          _dbus_assert_not_reached ("oom");
      }
      
      set_reply_serial (message);
      break;
    default:
      return FALSE;
    }
  
  if (message == NULL)
    _dbus_assert_not_reached ("oom");

  *message_p = message;
  
  return TRUE;
}

static dbus_bool_t
generate_many_bodies_inner (DBusMessageDataIter *iter,
                            DBusMessage        **message_p)
{
  DBusMessage *message;
  DBusString signature;
  DBusString body;
  
  message = dbus_message_new_method_call ("org.freedesktop.Foo",
                                          "/",
                                          "org.freedesktop.Blah",
                                          "NahNahNah");
  if (message == NULL)
    _dbus_assert_not_reached ("oom");

  set_reply_serial (message);

  if (!_dbus_string_init (&signature) || !_dbus_string_init (&body))
    _dbus_assert_not_reached ("oom");
  
  if (dbus_internal_do_not_use_generate_bodies (iter_get_sequence (iter),
                                                message->byte_order,
                                                &signature, &body))
    {
      const char *v_SIGNATURE;

      v_SIGNATURE = _dbus_string_get_const_data (&signature);
      if (!_dbus_header_set_field_basic (&message->header,
                                         DBUS_HEADER_FIELD_SIGNATURE,
                                         DBUS_TYPE_SIGNATURE,
                                         &v_SIGNATURE))
        _dbus_assert_not_reached ("oom");

      if (!_dbus_string_move (&body, 0, &message->body, 0))
        _dbus_assert_not_reached ("oom");

      _dbus_marshal_set_uint32 (&message->header.data, BODY_LENGTH_OFFSET,
                                _dbus_string_get_length (&message->body),
                                message->byte_order);
      
      *message_p = message;
    }
  else
    {
      dbus_message_unref (message);
      *message_p = NULL;
    }
  
  _dbus_string_free (&signature);
  _dbus_string_free (&body);

  return *message_p != NULL;
}

static dbus_bool_t
generate_outer (DBusMessageDataIter   *iter,
                DBusString            *data,
                DBusValidity          *expected_validity,
                DBusInnerGeneratorFunc func)
{
  DBusMessage *message;

  message = NULL;
  if (!(*func)(iter, &message))
    return FALSE;

  iter_next (iter);
  
  _dbus_assert (message != NULL);

  _dbus_message_set_serial (message, 1);
  _dbus_message_lock (message);
  
  *expected_validity = DBUS_VALID;

  /* move for efficiency, since we'll nuke the message anyway */
  if (!_dbus_string_move (&message->header.data, 0,
                          data, 0))
    _dbus_assert_not_reached ("oom");

  if (!_dbus_string_copy (&message->body, 0,
                          data, _dbus_string_get_length (data)))
    _dbus_assert_not_reached ("oom");

  dbus_message_unref (message);

  return TRUE;
}

static dbus_bool_t
generate_trivial (DBusMessageDataIter   *iter,
                  DBusString            *data,
                  DBusValidity          *expected_validity)
{
  return generate_outer (iter, data, expected_validity,
                         generate_trivial_inner);
}

static dbus_bool_t
generate_many_bodies (DBusMessageDataIter   *iter,
                      DBusString            *data,
                      DBusValidity          *expected_validity)
{
  return generate_outer (iter, data, expected_validity,
                         generate_many_bodies_inner);
}

static dbus_bool_t
generate_wrong_length (DBusMessageDataIter *iter,
                       DBusString          *data,
                       DBusValidity        *expected_validity)
{
  int lengths[] = { -42, -17, -16, -15, -9, -8, -7, -6, -5, -4, -3, -2, -1,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 30 };
  int adjust;
  int len_seq;

 restart:
  len_seq = iter_get_sequence (iter);
  if (len_seq == _DBUS_N_ELEMENTS (lengths))
    return FALSE;

  _dbus_assert (len_seq < _DBUS_N_ELEMENTS (lengths));
  
  iter_recurse (iter);
  if (!generate_many_bodies (iter, data, expected_validity))
    {
      iter_set_sequence (iter, 0); /* reset to first body */
      iter_unrecurse (iter);
      iter_next (iter);            /* next length adjustment */
      goto restart;
    }
  iter_unrecurse (iter);

  adjust = lengths[len_seq];

  if (adjust < 0)
    {
      if ((_dbus_string_get_length (data) + adjust) < DBUS_MINIMUM_HEADER_SIZE)
        _dbus_string_set_length (data, DBUS_MINIMUM_HEADER_SIZE);
      else
        _dbus_string_shorten (data, - adjust);
      *expected_validity = DBUS_INVALID_FOR_UNKNOWN_REASON;
    }
  else
    {      
      if (!_dbus_string_lengthen (data, adjust))
        _dbus_assert_not_reached ("oom");
      *expected_validity = DBUS_INVALID_TOO_MUCH_DATA;
    }

  /* Fixup lengths */
  {
    int old_body_len;
    int new_body_len;
    int byte_order;
    
    _dbus_assert (_dbus_string_get_length (data) >= DBUS_MINIMUM_HEADER_SIZE);
    
    byte_order = _dbus_string_get_byte (data, BYTE_ORDER_OFFSET);
    old_body_len = _dbus_marshal_read_uint32 (data,
                                              BODY_LENGTH_OFFSET,
                                              byte_order,
                                              NULL);
    _dbus_assert (old_body_len < _dbus_string_get_length (data));
    new_body_len = old_body_len + adjust;
    if (new_body_len < 0)
      {
        new_body_len = 0;
        /* we just munged the header, and aren't sure how */
        *expected_validity = DBUS_VALIDITY_UNKNOWN;
      }

    _dbus_verbose ("changing body len from %u to %u by adjust %d\n",
                   old_body_len, new_body_len, adjust);
    
    _dbus_marshal_set_uint32 (data, BODY_LENGTH_OFFSET,
                              new_body_len,
                              byte_order);
  }

  return TRUE;
}

static dbus_bool_t
generate_byte_changed (DBusMessageDataIter *iter,
                       DBusString          *data,
                       DBusValidity        *expected_validity)
{
  int byte_seq;
  int v_BYTE;

  /* This is a little convoluted to make the bodies the
   * outer loop and each byte of each body the inner
   * loop
   */

 restart:
  if (!generate_many_bodies (iter, data, expected_validity))
    return FALSE;

  iter_recurse (iter);
  byte_seq = iter_get_sequence (iter);
  iter_next (iter);
  iter_unrecurse (iter);

  if (byte_seq == _dbus_string_get_length (data))
    {
      _dbus_string_set_length (data, 0);
      /* reset byte count */
      iter_recurse (iter);
      iter_set_sequence (iter, 0);
      iter_unrecurse (iter);
      goto restart;
    }
  else
    {
      /* Undo the "next" in generate_many_bodies */
      iter_set_sequence (iter, iter_get_sequence (iter) - 1);
    }

  _dbus_assert (byte_seq < _dbus_string_get_length (data));
  v_BYTE = _dbus_string_get_byte (data, byte_seq);
  v_BYTE += byte_seq; /* arbitrary but deterministic change to the byte */
  _dbus_string_set_byte (data, byte_seq, v_BYTE);
  *expected_validity = DBUS_VALIDITY_UNKNOWN;

  return TRUE;
}

typedef struct
{
  const char *name;
  DBusMessageGeneratorFunc func;  
} DBusMessageGenerator;

static const DBusMessageGenerator generators[] = {
  { "trivial example of each message type", generate_trivial },
  { "assorted arguments", generate_many_bodies },
  { "wrong body lengths", generate_wrong_length },
  { "each byte modified", generate_byte_changed }
};

void
_dbus_message_data_free (DBusMessageData *data)
{
  _dbus_string_free (&data->data);
}

void
_dbus_message_data_iter_init (DBusMessageDataIter *iter)
{
  int i;
  
  iter->depth = 0;
  i = 0;
  while (i < _DBUS_MESSAGE_DATA_MAX_NESTING)
    {
      iter->sequence_nos[i] = 0;
      ++i;
    } 
}

dbus_bool_t
_dbus_message_data_iter_get_and_next (DBusMessageDataIter *iter,
                                      DBusMessageData     *data)
{
  DBusMessageGeneratorFunc func;
  int generator;

 restart:
  generator = iter_get_sequence (iter);
  
  if (generator == _DBUS_N_ELEMENTS (generators))
    return FALSE;

  iter_recurse (iter);
  
  if (iter_first_in_series (iter))
    printf (" testing message loading: %s\n", generators[generator].name);
  
  func = generators[generator].func;

  if (!_dbus_string_init (&data->data))
    _dbus_assert_not_reached ("oom");
  
  if ((*func)(iter, &data->data, &data->expected_validity))
    ;
  else
    {
      iter_set_sequence (iter, 0);
      iter_unrecurse (iter);
      iter_next (iter); /* next generator */
      _dbus_string_free (&data->data);
      goto restart;
    }
  iter_unrecurse (iter);
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
