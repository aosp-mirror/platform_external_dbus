/* -*- mode: C; c-file-style: "gnu" -*- */
/* config-loader-libxml.c  libxml2 XML loader
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
#include <dbus/dbus-internals.h>
#include <libxml/xmlreader.h>
#include <libxml/parser.h>
#include <libxml/globals.h>
#include <libxml/xmlmemory.h>
#include <errno.h>
#include <string.h>

static void*
libxml_malloc (size_t size)
{
  return dbus_malloc (size);
}

static void*
libxml_realloc (void *ptr, size_t size)
{
  return dbus_realloc (ptr, size);
}

static void
libxml_free (void *ptr)
{
  dbus_free (ptr);
}

static char*
libxml_strdup (const char *str)
{
  return _dbus_strdup (str);
}

static void
xml_text_reader_error (void                   *arg,
                       const char             *msg,
                       xmlParserSeverities     severity,
                       xmlTextReaderLocatorPtr locator)
{
  DBusError *error = arg;
  
  if (!dbus_error_is_set (error))
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Error loading config file: %s",
                      msg);
    }
}

BusConfigParser*
bus_config_load (const DBusString *file,
                 DBusError        *error)
{
  xmlTextReader *reader;
  const char *filename;
  BusConfigParser *parser;
  DBusError tmp_error;
  int ret;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  _dbus_string_get_const_data (file, &filename);
  parser = NULL;
  reader = NULL;
  dbus_error_init (&tmp_error);

  if (xmlMemSetup (libxml_free,
                   libxml_malloc,
                   libxml_realloc,
                   libxml_strdup) != 0)
    {
      /* Current libxml can't possibly fail here, but just being
       * paranoid; don't really know why xmlMemSetup() returns an
       * error code, assuming some version of libxml had a reason.
       */
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "xmlMemSetup() didn't work for some reason\n");
      return NULL;
    }
  
  parser = bus_config_parser_new ();
  if (parser == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_NO_MEMORY, NULL);
      return NULL;
    }
  
  errno = 0;
  reader = xmlNewTextReaderFilename (filename);

  if (reader == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Failed to load configuration file %s: %s\n",
                      filename,
                      errno != 0 ? strerror (errno) : "Unknown error");
        
      goto failed;
    }

  xmlTextReaderSetErrorHandler (reader, xml_text_reader_error, &tmp_error);

  while ((ret = xmlTextReaderRead (reader)) == 1)
    {
      int type;
      
      if (dbus_error_is_set (&tmp_error))
        goto reader_out;
      
      /* "enum" anyone? http://dotgnu.org/pnetlib-doc/System/Xml/XmlNodeType.html for
       * the magic numbers
       */
      type = xmlTextReaderNodeType (reader);
      if (dbus_error_is_set (&tmp_error))
        goto reader_out;

      /* FIXME I don't really know exactly what I need to do to
       * resolve all entities and so on to get the full content of a
       * node or attribute value. I'm worried about whether I need to
       * manually handle stuff like &lt;
       */
    }

  if (ret == -1)
    {
      if (!dbus_error_is_set (&tmp_error))
        dbus_set_error (&tmp_error,
                        DBUS_ERROR_FAILED,
                        "Unknown failure loading configuration file");
    }
  
 reader_out:
  xmlFreeTextReader (reader);
  reader = NULL;
  if (dbus_error_is_set (&tmp_error))
    {
      dbus_move_error (&tmp_error, error);
      goto failed;
    }
  
  if (!bus_config_parser_finished (parser, error))
    goto failed;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  return parser;
  
 failed:
  _DBUS_ASSERT_ERROR_IS_SET (error);
  if (parser)
    bus_config_parser_unref (parser);
  _dbus_assert (reader == NULL); /* must go to reader_out first */
  return NULL;
}
