/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gutils.c Utils shared between convenience lib and installed lib
 *
 * Copyright (C) 2003  Red Hat, Inc.
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
#include "dbus-gutils.h"
#include "dbus-gtest.h"
#include <string.h>

#ifndef DOXYGEN_SHOULD_SKIP_THIS

char**
_dbus_gutils_split_path (const char *path)
{
  int len;
  char **split;
  int n_components;
  int i, j, comp;

  len = strlen (path);

  n_components = 0;
  i = 0;
  while (i < len)
    {
      if (path[i] == '/')
        n_components += 1;
      ++i;
    }

  split = g_new0 (char*, n_components + 1);

  comp = 0;
  i = 0;
  while (i < len)
    {
      if (path[i] == '/')
        ++i;
      j = i;

      while (j < len && path[j] != '/')
        ++j;

      /* Now [i, j) is the path component */
      g_assert (i < j);
      g_assert (path[i] != '/');
      g_assert (j == len || path[j] == '/');

      split[comp] = g_strndup (&path[i], j - i + 1);

      split[comp][j-i] = '\0';

      ++comp;
      i = j;
    }
  g_assert (i == len);

  return split;
}

const char *
_dbus_gutils_type_to_string (int type)
{
  switch (type)
    {
    case DBUS_TYPE_INVALID:
      return "invalid";
    case DBUS_TYPE_NIL:
      return "nil";
    case DBUS_TYPE_BOOLEAN:
      return "boolean";
    case DBUS_TYPE_INT32:
      return "int32";
    case DBUS_TYPE_UINT32:
      return "uint32";
    case DBUS_TYPE_DOUBLE:
      return "double";
    case DBUS_TYPE_STRING:
      return "string";
    case DBUS_TYPE_CUSTOM:
      return "custom";
    case DBUS_TYPE_ARRAY:
      return "array";
    case DBUS_TYPE_DICT:
      return "dict";
    case DBUS_TYPE_INT64:
      return "int64";
    case DBUS_TYPE_UINT64:
      return "uint64";
    default:
      return "unknown";
    }
}

#ifdef DBUS_BUILD_TESTS

/**
 * @ingroup DBusGLibInternals
 * Unit test for GLib utils internals
 * @returns #TRUE on success.
 */
gboolean
_dbus_gutils_test (const char *test_data_dir)
{

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
