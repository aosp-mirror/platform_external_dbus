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

  DBusList *stack;
};

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


      dbus_free (parser);
    }
}

dbus_bool_t
bus_config_parser_check_doctype (BusConfigParser   *parser,
                                 const char        *doctype,
                                 DBusError         *error)
{
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
  

}

dbus_bool_t
bus_config_parser_end_element (BusConfigParser   *parser,
                               const char        *element_name,
                               DBusError         *error)
{
  

}

dbus_bool_t
bus_config_parser_content (BusConfigParser   *parser,
                           const DBusString  *content,
                           DBusError         *error)
{
  

}

