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

typedef dbus_bool_t (* DBusInnerGeneratorFunc)   (int           sequence,
                                                  DBusMessage **message_p);
typedef dbus_bool_t (* DBusMessageGeneratorFunc) (int           sequence,
                                                  DBusString   *data,
                                                  DBusValidity *expected_validity);

static void
set_reply_serial (DBusMessage *message)
{
  if (message == NULL)
    _dbus_assert_not_reached ("oom");
  if (!dbus_message_set_reply_serial (message, 100))
    _dbus_assert_not_reached ("oom");
}

static dbus_bool_t
generate_trivial_inner (int           sequence,
                        DBusMessage **message_p)
{
  DBusMessage *message;

  switch (sequence)
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
generate_outer (int                    sequence,
                DBusString            *data,
                DBusValidity          *expected_validity,
                DBusInnerGeneratorFunc func)
{
  DBusMessage *message;

  message = NULL;
  if (!(*func)(sequence, &message))
    return FALSE;

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
generate_trivial (int                    sequence,
                  DBusString            *data,
                  DBusValidity          *expected_validity)
{
  return generate_outer (sequence, data, expected_validity,
                         generate_trivial_inner);
}

static const DBusMessageGeneratorFunc generators[] = {
  generate_trivial
};

void
_dbus_message_data_free (DBusMessageData *data)
{
  _dbus_string_free (&data->data);
}

void
_dbus_message_data_iter_init (DBusMessageDataIter *iter)
{
  iter->generator = 0;
  iter->sequence = 0;
}

dbus_bool_t
_dbus_message_data_iter_get_and_next (DBusMessageDataIter *iter,
                                      DBusMessageData     *data)
{
  DBusMessageGeneratorFunc func;

 restart:
  if (iter->generator == _DBUS_N_ELEMENTS (generators))
    return FALSE;
  
  func = generators[iter->generator];

  if (!_dbus_string_init (&data->data))
    _dbus_assert_not_reached ("oom");
  
  if ((*func)(iter->sequence, &data->data, &data->expected_validity))
    iter->sequence += 1;
  else
    {
      iter->generator += 1;
      iter->sequence = 0;
      _dbus_string_free (&data->data);
      goto restart;
    }
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
