/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gparser.c parse DBus description files
 *
 * Copyright (C) 2003  Red Hat, Inc.
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
#include "dbus-gparser.h"
#include "dbus-gidl.h"

struct Parser
{
  int refcount;

};

Parser*
parser_new (void)
{
  Parser *parser;

  parser = g_new0 (Parser, 1);

  parser->refcount = 1;

  return parser;
}

void
parser_ref (Parser *parser)
{
  parser->refcount += 1;
}

void
parser_unref (Parser *parser)
{
  parser->refcount -= 1;
  if (parser->refcount == 0)
    {
      

      g_free (parser);
    }
}

gboolean
parser_check_doctype (Parser      *parser,
                      const char  *doctype,
                      GError     **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL);
  
  if (strcmp (doctype, "dbus_description") != 0)
    {
      g_set_error (error,
                   G_MARKUP_ERROR_PARSE,
                   "D-BUS description file has the wrong document type %s, use dbus_description",
                   doctype);
      return FALSE;
    }
  else
    return TRUE;
}

gboolean
parser_start_element (Parser      *parser,
                      const char  *element_name,
                      const char **attribute_names,
                      const char **attribute_values,
                      GError     **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL);

  return TRUE;
}

gboolean
parser_end_element (Parser      *parser,
                    const char  *element_name,
                    GError     **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL);

  return TRUE;
}

gboolean
parser_content (Parser      *parser,
                const char  *content,
                int          len,
                GError     **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL);

  return TRUE;
}

gboolean
parser_finished (Parser      *parser,
                 GError     **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL);

  return TRUE;
}
