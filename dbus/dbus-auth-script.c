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

/* this is slightly different from the other append_quoted_string
 * in dbus-message-builder.c
 */
static dbus_bool_t
append_quoted_string (DBusString       *dest,
                      const DBusString *quoted)
{
  dbus_bool_t in_quotes = FALSE;
  dbus_bool_t in_backslash = FALSE;
  int i;

  i = 0;
  while (i < _dbus_string_get_length (quoted))
    {
      unsigned char b;

      b = _dbus_string_get_byte (quoted, i);

      if (in_backslash)
        {
          unsigned char a;
          
          if (b == 'r')
            a = '\r';
          else if (b == 'n')
            a = '\n';
          else if (b == '\\')
            a = '\\';
          else
            {
              _dbus_warn ("bad backslashed byte %c\n", b);
              return FALSE;
            }

          if (!_dbus_string_append_byte (dest, a))
            return FALSE;
          
          in_backslash = FALSE;
        }
      else if (b == '\\')
        {
          in_backslash = TRUE;
        }
      else if (in_quotes)
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

/**
 * Runs an "auth script" which is a script for testing the
 * authentication protocol. Scripts send and receive data, and then
 * include assertions about the state of both ends of the connection
 * after processing the data. A script succeeds if these assertions
 * hold.
 *
 * @param filename the file containing the script to run
 * @returns #TRUE if the script succeeds, #FALSE otherwise
 */
dbus_bool_t
_dbus_auth_script_run (const DBusString *filename)
{
  DBusString file;
  DBusError error;
  DBusString line;
  dbus_bool_t retval;
  int line_no;
  DBusAuth *auth;
  DBusString from_auth;
  DBusAuthState state;
  DBusString context;
  
  retval = FALSE;
  auth = NULL;

  _dbus_string_init_const (&context, "org_freedesktop_test");
  
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

  dbus_error_init (&error);
  if (!_dbus_file_get_contents (&file, filename, &error))    {
      const char *s;
      _dbus_string_get_const_data (filename, &s);
      _dbus_warn ("Getting contents of %s failed: %s\n",
                  s, error.message);
      dbus_error_free (&error);
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
          DBusCredentials creds;
          
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

          _dbus_credentials_from_current_process (&creds);
          _dbus_auth_set_credentials (auth, &creds);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "SERVER"))
        {
          DBusCredentials creds;
          
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

          _dbus_credentials_from_current_process (&creds);
          _dbus_auth_set_credentials (auth, &creds);
          _dbus_auth_set_context (auth, &context);
        }
      else if (auth == NULL)
        {
          _dbus_warn ("must specify CLIENT or SERVER\n");
          goto out;

        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "NO_CREDENTIALS"))
        {
          DBusCredentials creds = { -1, -1, -1 };
          _dbus_auth_set_credentials (auth, &creds);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "ROOT_CREDENTIALS"))
        {
          DBusCredentials creds = { -1, 0, 0 };
          _dbus_auth_set_credentials (auth, &creds);          
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "SILLY_CREDENTIALS"))
        {
          DBusCredentials creds = { -1, 4312, 1232 };
          _dbus_auth_set_credentials (auth, &creds);          
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

          {
            const char *s4;
            _dbus_string_get_const_data (&to_send, &s4);
            _dbus_verbose ("Sending '%s'\n", s4);
          }
          
          if (!_dbus_string_append (&to_send, "\r\n"))
            {
              _dbus_warn ("failed to append \r\n from line %d\n",
                          line_no);
              _dbus_string_free (&to_send);
              goto out;
            }

          /* Replace USERID_BASE64 with our username in base64 */
          {
            int where;
            
            if (_dbus_string_find (&to_send, 0,
                                   "USERID_BASE64", &where))
              {
                DBusString username;

                if (!_dbus_string_init (&username, _DBUS_INT_MAX))
                  {
                    _dbus_warn ("no memory for userid\n");
                    _dbus_string_free (&to_send);
                    goto out;
                  }

                if (!_dbus_string_append_our_uid (&username))
                  {
                    _dbus_warn ("no memory for userid\n");
                    _dbus_string_free (&username);
                    _dbus_string_free (&to_send);
                    goto out;
                  }

                _dbus_string_delete (&to_send, where, strlen ("USERID_BASE64"));
                
                if (!_dbus_string_base64_encode (&username, 0,
                                                 &to_send, where))
                  {
                    _dbus_warn ("no memory to subst USERID_BASE64\n");
                    _dbus_string_free (&username);
                    _dbus_string_free (&to_send);
                    goto out;
                  }

                _dbus_string_free (&username);
              }
            else if (_dbus_string_find (&to_send, 0,
                                        "USERNAME_BASE64", &where))
              {
                DBusString username;
                const DBusString *u;
                
                if (!_dbus_string_init (&username, _DBUS_INT_MAX))
                  {
                    _dbus_warn ("no memory for username\n");
                    _dbus_string_free (&to_send);
                    goto out;
                  }

                if (!_dbus_user_info_from_current_process (&u, NULL, NULL) ||
                    !_dbus_string_copy (u, 0, &username,
                                        _dbus_string_get_length (&username)))
                  {
                    _dbus_warn ("no memory for username\n");
                    _dbus_string_free (&username);
                    _dbus_string_free (&to_send);
                    goto out;
                  }

                _dbus_string_delete (&to_send, where, strlen ("USERNAME_BASE64"));
                
                if (!_dbus_string_base64_encode (&username, 0,
                                                 &to_send, where))
                  {
                    _dbus_warn ("no memory to subst USERNAME_BASE64\n");
                    _dbus_string_free (&username);
                    _dbus_string_free (&to_send);
                    goto out;
                  }

                _dbus_string_free (&username);
              }
          }

          {
            DBusString *buffer;

            _dbus_auth_get_buffer (auth, &buffer);
            if (!_dbus_string_copy (&to_send, 0,
                                    buffer, _dbus_string_get_length (buffer)))
              {
                _dbus_warn ("not enough memory to call bytes_received, or can't add bytes to auth object already in end state\n");
                _dbus_string_free (&to_send);
                _dbus_auth_return_buffer (auth, buffer, 0);
                goto out;
              }

            _dbus_auth_return_buffer (auth, buffer, _dbus_string_get_length (&to_send));
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
              _dbus_warn ("line %d expected command '%s' and got '%s'\n",
                          line_no, s1, s2);
              _dbus_string_free (&received);
              goto out;
            }
          
          _dbus_string_free (&received);
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "EXPECT_UNUSED"))
        {
          DBusString expected;
          const DBusString *unused;
          
          _dbus_string_delete_first_word (&line);

          if (!_dbus_string_init (&expected, _DBUS_INT_MAX))
            {
              _dbus_warn ("no mem to allocate string expected\n");
              goto out;
            }

          if (!append_quoted_string (&expected, &line))
            {
              _dbus_warn ("failed to append quoted string line %d\n",
                          line_no);
              _dbus_string_free (&expected);
              goto out;
            }

          _dbus_auth_get_unused_bytes (auth, &unused);
          
          if (_dbus_string_equal (&expected, unused))
            {
              _dbus_string_free (&expected);
            }
          else
            {
              const char *e1, *h1;
              _dbus_string_get_const_data (&expected, &e1);
              _dbus_string_get_const_data (unused, &h1);
              _dbus_warn ("Expected unused bytes '%s' and have '%s'\n",
                          e1, h1);
              _dbus_string_free (&expected);
              goto out;
            }
        }
      else if (_dbus_string_starts_with_c_str (&line,
                                               "EXPECT"))
        {
          DBusString expected;
          
          _dbus_string_delete_first_word (&line);

          if (!_dbus_string_init (&expected, _DBUS_INT_MAX))
            {
              _dbus_warn ("no mem to allocate string expected\n");
              goto out;
            }

          if (!append_quoted_string (&expected, &line))
            {
              _dbus_warn ("failed to append quoted string line %d\n",
                          line_no);
              _dbus_string_free (&expected);
              goto out;
            }

          if (_dbus_string_equal_len (&expected, &from_auth,
                                      _dbus_string_get_length (&expected)))
            {
              _dbus_string_delete (&from_auth, 0,
                                   _dbus_string_get_length (&expected));
              _dbus_string_free (&expected);
            }
          else
            {
              const char *e1, *h1;
              _dbus_string_get_const_data (&expected, &e1);
              _dbus_string_get_const_data (&from_auth, &h1);
              _dbus_warn ("Expected exact string '%s' and have '%s'\n",
                          e1, h1);
              _dbus_string_free (&expected);
              goto out;
            }
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
  _dbus_string_free (&line);
  _dbus_string_free (&from_auth);
  
  return retval;
}

/** @} */
#endif /* DBUS_BUILD_TESTS */
