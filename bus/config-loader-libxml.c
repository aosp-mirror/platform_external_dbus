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
#include <errno.h>
#include <string.h>

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
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  _dbus_string_get_const_data (file, &filename);
  
  errno = 0;
  reader = xmlNewTextReaderFilename (filename);

  if (reader == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Failed to load configuration file %s: %s\n",
                      filename,
                      errno != 0 ? strerror (errno) : "Unknown error");
        
      return NULL;
    }

  dbus_error_init (&tmp_error);
  xmlTextReaderSetErrorHandler (reader, xml_text_reader_error, &tmp_error);

  while (xmlTextReaderRead(reader) == 1)
    {
      if (dbus_error_is_set (&tmp_error))
        goto reader_out;

      


    }
  
 reader_out:
  xmlFreeTextReader (reader);
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
  bus_config_parser_unref (parser);
  return NULL;
}
