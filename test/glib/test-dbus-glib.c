/* -*- mode: C; c-file-style: "gnu" -*- */
#include "dbus-glib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main (int argc, char **argv)
{
  DBusConnection *connection;
  GMainLoop *loop;
  GError *error;
  DBusGProxy *driver;
  DBusPendingCall *call;
  char **service_list;
  int service_list_len;
  int i;

  g_type_init ();
  
  loop = g_main_loop_new (NULL, FALSE);

  error = NULL;
  connection = dbus_bus_get_with_g_main (DBUS_BUS_SESSION,
                                         &error);
  if (connection == NULL)
    {
      g_printerr ("Failed to open connection to bus: %s\n",
                  error->message);
      g_error_free (error);
      exit (1);
    }

  /* Create a proxy object for the "bus driver" */
  
  driver = dbus_gproxy_new_for_service (connection,
                                        DBUS_SERVICE_ORG_FREEDESKTOP_DBUS,
                                        DBUS_PATH_ORG_FREEDESKTOP_DBUS,
                                        DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS);

  /* Call ListServices method */
  
  call = dbus_gproxy_begin_call (driver, "ListServices", DBUS_TYPE_INVALID);

  error = NULL;
  if (!dbus_gproxy_end_call (driver, call, &error,
                             DBUS_TYPE_ARRAY, DBUS_TYPE_STRING,
                             &service_list, &service_list_len,
                             DBUS_TYPE_INVALID))
    {
      g_printerr ("Failed to complete ListServices call: %s\n",
                  error->message);
      g_error_free (error);
      exit (1);
    }

  g_print ("Services on the message bus:\n");
  i = 0;
  while (i < service_list_len)
    {
      g_assert (service_list[i] != NULL);
      g_print ("  %s\n", service_list[i]);
      ++i;
    }
  g_assert (service_list[i] == NULL);
  
  dbus_free_string_array (service_list);

  g_object_unref (G_OBJECT (driver));
  
  g_print ("Successfully completed %s\n", argv[0]);
  
  return 0;
}
