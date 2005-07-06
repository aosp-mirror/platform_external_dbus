/* -*- mode: C; c-file-style: "gnu" -*- */
/* gather-introspect.c  Dump introspection data from service to stdout
 *
 * Copyright (C) 2005  Red Hat, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dbus/dbus-glib.h>

static void
usage (char *name, int ecode)
{
  fprintf (stderr, "Usage: %s <service> <destination object path>\n", name);
  exit (ecode);
}

int
main (int argc, char *argv[])
{
  DBusGConnection *connection;
  DBusGProxy *proxy;
  GError *error;
  const char *service;
  const char *path;
  char *introspect_data;
  
  if (argc != 3)
    usage (argv[0], 1);

  service = argv[1];
  path = argv[2];

  g_type_init ();

  error = NULL;
  connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
  if (connection == NULL)
    {
      fprintf (stderr, "Failed to open connection to session bus: %s\n",
               error->message);
      g_clear_error (&error);
      exit (1);
    }

  proxy = dbus_g_proxy_new_for_name (connection,
				     service, path,
				     DBUS_INTERFACE_INTROSPECTABLE);
  if (!dbus_g_proxy_call (proxy, "Introspect",  &error,
			  G_TYPE_INVALID,
			  G_TYPE_STRING, &introspect_data,
			  G_TYPE_INVALID))
    {
      fprintf (stderr, "Failed to get introspection data: %s\n",
               error->message);
      g_clear_error (&error);
      exit (1);
    }
      
  printf ("%s", introspect_data);
  g_free (introspect_data);

  g_object_unref (proxy);

  exit (0);
}
