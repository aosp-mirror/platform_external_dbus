/* -*- mode: C; c-file-style: "gnu" -*- */
/* config-parser.c  XML-library-agnostic configuration file parser
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
#include "config-parser.h"
#include "test.h"
#include <dbus/dbus-list.h>
#include <dbus/dbus-internals.h>
#include <string.h>

typedef enum
{
  ELEMENT_BUSCONFIG,
  ELEMENT_INCLUDE,
  ELEMENT_USER,
  ELEMENT_LISTEN,
  ELEMENT_AUTH,
  ELEMENT_POLICY,
  ELEMENT_LIMIT
} ElementType;

typedef struct
{
  ElementType type;

  union
  {
    struct
    {
      BusConfigParser *parser;
    } include;

    struct
    {
      char *username;
    } user;

    struct
    {
      char *address;
    } listen;

    struct
    {
      char *mechanism;
    } auth;

    struct
    {
      char *context;
      char *user;
      char *group;
      DBusList *rules;
    } policy;

    struct
    {
      int foo;
    } limit;
    
  } d;
  
} Element;

struct BusConfigParser
{
  int refcount;

  DBusList *stack; /**< stack of Element */

  char *user;      /**< user to run as */
};


static Element*
push_element (BusConfigParser *parser,
              ElementType      type)
{
  Element *e;

  e = dbus_new0 (Element, 1);
  if (e == NULL)
    return NULL;
  
  e->type = type;

  return e;
}

static void
pop_element (BusConfigParser *parser)
{
  Element *e;

  e = _dbus_list_pop_last (&parser->stack);

  dbus_free (e);
}

BusConfigParser*
bus_config_parser_new (void)
{
  BusConfigParser *parser;

  parser = dbus_new0 (BusConfigParser, 1);
  if (parser == NULL)
    return NULL;

  parser->refcount = 1;

  return parser;
}

void
bus_config_parser_ref (BusConfigParser *parser)
{
  _dbus_assert (parser->refcount > 0);

  parser->refcount += 1;
}

void
bus_config_parser_unref (BusConfigParser *parser)
{
  _dbus_assert (parser->refcount > 0);

  parser->refcount -= 1;

  if (parser->refcount == 0)
    {
      while (parser->stack != NULL)
        pop_element (parser);
      
      dbus_free (parser->user);

      dbus_free (parser);
    }
}

dbus_bool_t
bus_config_parser_check_doctype (BusConfigParser   *parser,
                                 const char        *doctype,
                                 DBusError         *error)
{
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  if (strcmp (doctype, "busconfig") != 0)
    {
      dbus_set_error (error,
                      DBUS_ERROR_FAILED,
                      "Document has the wrong type %s",
                      doctype);
      return FALSE;
    }
  else
    return TRUE;
}

dbus_bool_t
bus_config_parser_start_element (BusConfigParser   *parser,
                                 const char        *element_name,
                                 const char       **attribute_names,
                                 const char       **attribute_values,
                                 DBusError         *error)
{
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);  

  return TRUE;
}

dbus_bool_t
bus_config_parser_end_element (BusConfigParser   *parser,
                               const char        *element_name,
                               DBusError         *error)
{
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  return TRUE;
}

dbus_bool_t
bus_config_parser_content (BusConfigParser   *parser,
                           const DBusString  *content,
                           DBusError         *error)
{
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  return TRUE;
}

dbus_bool_t
bus_config_parser_finished (BusConfigParser   *parser,
                            DBusError         *error)
{
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);  

  return TRUE;
}

const char*
bus_config_parser_get_user (BusConfigParser *parser)
{


  return NULL;
}

#ifdef DBUS_BUILD_TESTS
#include <stdio.h>

typedef enum
{
  VALID,
  INVALID,
  UNKNOWN
} Validity;

static dbus_bool_t
do_load (const DBusString *full_path,
         Validity          validity,
         dbus_bool_t       oom_possible)
{
  BusConfigParser *parser;
  DBusError error;

  dbus_error_init (&error);
  
  parser = bus_config_load (full_path, &error);
  if (parser == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (&error);
      
      if (oom_possible &&
          dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY))
        {
          _dbus_verbose ("Failed to load valid file due to OOM\n");
          dbus_error_free (&error);
          return TRUE;
        }
      else if (validity == VALID)
        {          
          _dbus_warn ("Failed to load valid file but still had memory: %s\n",
                      error.message);

          dbus_error_free (&error);
          return FALSE;
        }
      else
        {
          dbus_error_free (&error);
          return TRUE;
        }
    }
  else
    {
      _DBUS_ASSERT_ERROR_IS_CLEAR (&error);
      
      bus_config_parser_unref (parser);
      
      if (validity == INVALID)
        {
          _dbus_warn ("Accepted invalid file\n");
          return FALSE;
        }

      return TRUE;
    }
}

static dbus_bool_t
check_oom_loading (const DBusString *full_path,
                   Validity          validity)
{
  int approx_mallocs;

  /* Run once to see about how many mallocs are involved */
  
  _dbus_set_fail_alloc_counter (_DBUS_INT_MAX);

  if (!do_load (full_path, validity, FALSE))
    return FALSE;

  approx_mallocs = _DBUS_INT_MAX - _dbus_get_fail_alloc_counter ();

  _dbus_verbose ("=================\nabout %d mallocs total\n=================\n",
                 approx_mallocs);
  
  approx_mallocs += 10; /* fudge factor */
  
  /* Now run failing each malloc */
  
  while (approx_mallocs >= 0)
    {
      
      _dbus_set_fail_alloc_counter (approx_mallocs);

      _dbus_verbose ("\n===\n(will fail malloc %d)\n===\n",
                     approx_mallocs);

      if (!do_load (full_path, validity, TRUE))
        return FALSE;
      
      approx_mallocs -= 1;
    }

  _dbus_set_fail_alloc_counter (_DBUS_INT_MAX);

  _dbus_verbose ("=================\n all iterations passed\n=================\n");

  return TRUE;
}

static dbus_bool_t
process_test_subdir (const DBusString *test_base_dir,
                     const char       *subdir,
                     Validity          validity)
{
  DBusString test_directory;
  DBusString filename;
  DBusDirIter *dir;
  dbus_bool_t retval;
  DBusError error;

  retval = FALSE;
  dir = NULL;
  
  if (!_dbus_string_init (&test_directory, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("didn't allocate test_directory\n");

  _dbus_string_init_const (&filename, subdir);
  
  if (!_dbus_string_copy (test_base_dir, 0,
                          &test_directory, 0))
    _dbus_assert_not_reached ("couldn't copy test_base_dir to test_directory");
  
  if (!_dbus_concat_dir_and_file (&test_directory, &filename))    
    _dbus_assert_not_reached ("couldn't allocate full path");

  _dbus_string_free (&filename);
  if (!_dbus_string_init (&filename, _DBUS_INT_MAX))
    _dbus_assert_not_reached ("didn't allocate filename string\n");

  dbus_error_init (&error);
  dir = _dbus_directory_open (&test_directory, &error);
  if (dir == NULL)
    {
      const char *s;
      _dbus_string_get_const_data (&test_directory, &s);
      _dbus_warn ("Could not open %s: %s\n", s,
                  error.message);
      dbus_error_free (&error);
      goto failed;
    }

  printf ("Testing:\n");
  
 next:
  while (_dbus_directory_get_next_file (dir, &filename, &error))
    {
      DBusString full_path;
      
      if (!_dbus_string_init (&full_path, _DBUS_INT_MAX))
        _dbus_assert_not_reached ("couldn't init string");

      if (!_dbus_string_copy (&test_directory, 0, &full_path, 0))
        _dbus_assert_not_reached ("couldn't copy dir to full_path");

      if (!_dbus_concat_dir_and_file (&full_path, &filename))
        _dbus_assert_not_reached ("couldn't concat file to dir");

      if (!_dbus_string_ends_with_c_str (&full_path, ".conf"))
        {
          const char *filename_c;
          _dbus_string_get_const_data (&filename, &filename_c);
          _dbus_verbose ("Skipping non-.conf file %s\n",
                         filename_c);
	  _dbus_string_free (&full_path);
          goto next;
        }

      {
        const char *s;
        _dbus_string_get_const_data (&filename, &s);
        printf ("    %s\n", s);
      }
      
      _dbus_verbose (" expecting %s\n",
                     validity == VALID ? "valid" :
                     (validity == INVALID ? "invalid" :
                      (validity == UNKNOWN ? "unknown" : "???")));

      if (!check_oom_loading (&full_path, validity))
        _dbus_assert_not_reached ("test failed");
      
      _dbus_string_free (&full_path);
    }

  if (dbus_error_is_set (&error))
    {
      const char *s;
      _dbus_string_get_const_data (&test_directory, &s);
      _dbus_warn ("Could not get next file in %s: %s\n",
                  s, error.message);
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

dbus_bool_t
bus_config_parser_test (const DBusString *test_data_dir)
{
  if (test_data_dir == NULL ||
      _dbus_string_get_length (test_data_dir) == 0)
    {
      printf ("No test data\n");
      return TRUE;
    }
  
  if (!process_test_subdir (test_data_dir, "valid-config-files", VALID))
    return FALSE;
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */

