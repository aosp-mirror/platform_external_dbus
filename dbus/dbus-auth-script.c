/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-auth-script.c Test DBusAuth using a special script file (internal to D-BUS implementation)
 * 
 * Copyright (C) 2003 Red Hat, Inc.
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
#include <config.h>

#ifdef DBUS_BUILD_TESTS

#include "dbus-auth-script.h"
#include "dbus-auth.h"
#include "dbus-string.h"
#include "dbus-hash.h"
#include "dbus-internals.h"
#include "dbus-marshal.h"

/**
 * @defgroup DBusAuthScript code for running unit test scripts for DBusAuth
 * @ingroup  DBusInternals
 * @brief DBusAuth unit test scripting
 *
 * The code in here is used for unit testing, it loads
 * up a script that tests DBusAuth.
 *
 * @{
 */

static dbus_bool_t
append_quoted_string (DBusString       *dest,
                      const DBusString *quoted)
{
  dbus_bool_t in_quotes = FALSE;
  int i;

  i = 0;
  while (i < _dbus_string_get_length (quoted))
    {
      unsigned char b;

      b = _dbus_string_get_byte (quoted, i);
      
      if (in_quotes)
        {
          if (b == '\'')
            in_quotes = FALSE;
          else
            {
              if (!_dbus_string_append_byte (dest, b))
                return FALSE;
            }
        }
      else
        {
          if (b == '\'')
            in_quotes = TRUE;
          else if (b == ' ' || b == '\n' || b == '\t')
            break; /* end on whitespace if not quoted */
          else
            {
              if (!_dbus_string_append_byte (dest, b))
                return FALSE;
            }
        }
      
      ++i;
    }

  if (!_dbus_string_append_byte (dest, '\0'))
    return FALSE;
  return TRUE;
}

static dbus_bool_t
same_first_word (const DBusString *a,
                 const DBusString *b)
{
  int first_a_blank, first_b_blank;

  _dbus_string_find_blank (a, 0, &first_a_blank);
  _dbus_string_find_blank (b, 0, &first_b_blank);

  if (first_a_blank != first_b_blank)
    return FALSE;

  return _dbus_string_equal_len (a, b, first_a_blank);
}

static DBusAuthState
auth_state_from_string (const DBusString *str)
{ 
  if (_dbus_string_starts_with_c_str (str, "WAITING_FOR_INPUT"))
    return DBUS_AUTH_STATE_WAITING_FOR_INPUT;
  else if (_dbus_string_starts_with_c_str (str, "WAITING_FOR_MEMORY"))
    return DBUS_AUTH_STATE_WAITING_FOR_MEMORY;
  else if (_dbus_string_starts_with_c_str (str, "HAVE_BYTES_TO_SEND"))
    return DBUS_AUTH_STATE_HAVE_BYTES_TO_SEND;
  else if (_dbus_string_starts_with_c_str (str, "NEED_DISCONNECT"))
    return DBUS_AUTH_STATE_NEED_DISCONNECT;
  else if (_dbus_string_starts_with_c_str (str, "AUTHENTICATED_WITH_UNUSED_BYTES"))
    return DBUS_AUTH_STATE_AUTHENTICATED_WITH_UNUSED_BYTES;
  else if (_dbus_string_starts_with_c_str (str, "AUTHENTICATED"))
    return DBUS_AUTH_STATE_AUTHENTICATED;
  else
    return -1;
}

static const char*
auth_state_to_string (DBusAuthState state)
{
  switch (state)
    {
    case DBUS_AUTH_STATE_WAITING_FOR_INPUT:
      return "WAITING_FOR_INPUT";
    case DBUS_AUTH_STATE_WAITING_FOR_MEMORY:
      return "WAITING_FOR_MEMORY";
    case DBUS_AUTH_STATE_HAVE_BYTES_TO_SEND:
      return "HAVE_BYTES_TO_SEND";
    case DBUS_AUTH_STATE_NEED_DISCONNECT:
      return "NEED_DISCONNECT";
    case DBUS_AUTH_STATE_AUTHENTICATED_WITH_UNUSED_BYTES:
      return "AUTHENTICATED_WITH_UNUSED_BYTES";
    case DBUS_AUTH_STATE_AUTHENTICATED:
      return "AUTHENTICATED";
    }

  return "unknown";
}

dbus_bool_t
_dbus_auth_script_run (const DBusString *filename)
{
  DBusString file;
  DBusResultCode result;
  DBusString line;
  dbus_bool_t retval;
  int line_no;
  DBusAuth *auth;
  DBusString from_auth;
  DBusAuthState state;
  
  retval = FALSE;
  auth = NULL;
  
  if (!_dbus_string_init (&file, _DBUS_INT_MAX))
    return FALSE;

  if (!_dbus_string_init (&line, _DBUS_INT_MAX))
    {
      _dbus_string_free (&file);
      return FALSE;
    }

  if (!_dbus_string_init (&from_auth, _DBUS_INT_MAX))
    {
      _dbus_string_free (&file);
      _dbus_string_free (&line);
      return FALSE;
    }
  
  if ((result = _dbus_file_get_contents (&file, filename)) != DBUS_RESULT_SUCCESS)
    {
      const char *s;
      _dbus_string_get_const_data (filename, &s);
      _dbus_warn ("Getting contents of %s failed: %s\n",
                  s, dbus_result_to_string (result));
                     
      goto out;
    }

  state = DBUS_AUTH_STATE_NEED_DISCONNECT;
  line_no = 0;
 next_iteration:
  while (_dbus_string_pop_line (&file, &line))
    {      
      line_no += 1;

      _dbus_string_delete_leading_blanks (&line);

      if (auth != NULL)
        {
          while ((state = _dbus_auth_do_work (auth)) ==
                 DBUS_AUTH_STATE_HAVE_BYTES_TO_SEND)
            {
              const DBusString *tmp;
              if (_dbus_auth_get_bytes_to_send (auth, &tmp))
                {
                  int count = _dbus_string_get_length (tmp);

                  if (_dbus_string_copy (tmp, 0, &from_auth,
                                         _dbus_string_get_length (&from_auth)))
                    _dbus_auth_bytes_sent (auth, count);
                }
            }
        }
      
      if (_dbus_string_get_length (&line) == 0)
        {
          /* empty line */
          goto next_iteration;
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "#"))
        {
          /* Ignore this comment */
          goto next_iteration;
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "CLIENT"))
        {
          if (auth != NULL)
            {
              _dbus_warn ("already created a DBusAuth (CLIENT or SERVER given twice)\n");
              goto out;
            }

          auth = _dbus_auth_client_new ();
          if (auth == NULL)
            {
              _dbus_warn ("no memory to create DBusAuth\n");
              goto out;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "SERVER"))
        {
          if (auth != NULL)
            {
              _dbus_warn ("already created a DBusAuth (CLIENT or SERVER given twice)\n");
              goto out;
            }

          auth = _dbus_auth_server_new ();
          if (auth == NULL)
            {
              _dbus_warn ("no memory to create DBusAuth\n");
              goto out;
            }
        }
      else if (auth == NULL)
        {
          _dbus_warn ("must specify CLIENT or SERVER\n");
          goto out;

        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "SEND"))
        {
          DBusString to_send;
          
          _dbus_string_delete_first_word (&line);

          if (!_dbus_string_init (&to_send, _DBUS_INT_MAX))
            {
              _dbus_warn ("no memory to allocate string\n");
              goto out;
            }

          if (!append_quoted_string (&to_send, &line))
            {
              _dbus_warn ("failed to append quoted string line %d\n",
                          line_no);
              _dbus_string_free (&to_send);
              goto out;
            }

          if (!_dbus_auth_bytes_received (auth, &to_send))
            {
              _dbus_warn ("not enough memory to call bytes_received\n");
              _dbus_string_free (&to_send);
              goto out;
            }

          _dbus_string_free (&to_send);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "EXPECT_STATE"))
        {
          DBusAuthState expected;
          
          _dbus_string_delete_first_word (&line);

          expected = auth_state_from_string (&line);
          if (expected < 0)
            {
              _dbus_warn ("bad auth state given to EXPECT_STATE\n");
              goto parse_failed;
            }

          if (expected != state)
            {
              _dbus_warn ("expected auth state %s but got %s on line %d\n",
                          auth_state_to_string (expected),
                          auth_state_to_string (state),
                          line_no);
              goto out;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "EXPECT_COMMAND"))
        {
          DBusString received;
          
          _dbus_string_delete_first_word (&line);

          if (!_dbus_string_init (&received, _DBUS_INT_MAX))
            {
              _dbus_warn ("no mem to allocate string received\n");
              goto out;
            }

          if (!_dbus_string_pop_line (&from_auth, &received))
            {
              const char *command;
              _dbus_string_get_const_data (&line, &command);
              _dbus_warn ("no line popped from the DBusAuth being tested, expected command %s on line %d\n",
                          command, line_no);
              _dbus_string_free (&received);
              goto out;
            }

          if (!same_first_word (&received, &line))
            {
              const char *s1, *s2;
              _dbus_string_get_const_data (&line, &s1);
              _dbus_string_get_const_data (&received, &s2);
              _dbus_warn ("expected command '%s' and got '%s' line %d\n",
                          s1, s2, line_no);
              _dbus_string_free (&received);
              goto out;
            }
          
          _dbus_string_free (&received);
        }
      else
        goto parse_failed;

      goto next_iteration; /* skip parse_failed */
      
    parse_failed:
      {
        const char *s;
        _dbus_string_get_const_data (&line, &s);
        _dbus_warn ("couldn't process line %d \"%s\"\n",
                    line_no, s);
        goto out;
      }
    }

  if (auth != NULL &&
      state == DBUS_AUTH_STATE_AUTHENTICATED_WITH_UNUSED_BYTES)
    {
      _dbus_warn ("did not expect unused bytes (scripts must specify explicitly if they are expected)\n");
      goto out;
    }

  if (_dbus_string_get_length (&from_auth) > 0)
    {
      const char *s;
      _dbus_warn ("script did not have EXPECT_ statements for all the data received from the DBusAuth\n");
      _dbus_string_get_const_data (&from_auth, &s);
      _dbus_warn ("Leftover data: %s\n", s);
      goto out;
    }
  
  retval = TRUE;
  
 out:
  if (auth)
    _dbus_auth_unref (auth);

  _dbus_string_free (&file);
  _dbus_string_free (&file);
  _dbus_string_free (&from_auth);
  
  return retval;
}

/** @} */
#endif /* DBUS_BUILD_TESTS */
