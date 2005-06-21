#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

static void lose (const char *fmt, ...) G_GNUC_NORETURN G_GNUC_PRINTF (1, 2);
static void lose_gerror (const char *prefix, GError *error) G_GNUC_NORETURN;

static void
lose (const char *str, ...)
{
  va_list args;

  va_start (args, str);

  vfprintf (stderr, str, args);
  fputc ('\n', stderr);

  va_end (args);

  exit (1);
}

static void
lose_gerror (const char *prefix, GError *error) 
{
  lose ("%s: %s", prefix, error->message);
}

static void
print_hash_value (gpointer key, gpointer val, gpointer data)
{
  printf ("%s -> %s\n", (char *) key, (char *) val);
}

int
main (int argc, char **argv)
{
  DBusGConnection *bus;
  DBusGProxy *remote_object;
  DBusGProxy *remote_object_introspectable;
  GError *error = NULL;
  char **reply_list;
  char **reply_ptr;
  DBusGValue *hello_reply_struct;
  GHashTable *hello_reply_dict;
  char *introspect_data;

  g_type_init ();

  bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
  if (!bus)
    lose_gerror ("Couldn't connect to session bus", error);
  
  remote_object = dbus_g_proxy_new_for_name (bus,
					     "org.designfu.SampleService",
					     "/SomeObject",
					     "org.designfu.SampleInterface");

  if (!dbus_g_proxy_call (remote_object, "HelloWorld", &error,
			  G_TYPE_STRING, "Hello from example-client.c!", G_TYPE_INVALID,
			  G_TYPE_STRV, &reply_list, G_TYPE_INVALID))
    lose_gerror ("Failed to complete HelloWorld", error);

  
  /* FIXME - we don't support recursive values yet */
#if 0  
  if (!dbus_g_proxy_call (remote_object, "GetTuple", &error,
			  DBUS_TYPE_G_VALUE, &hello_reply_struct, G_TYPE_INVALID))
    lose_gerror ("Failed to complete GetTuple", error);
#endif  
  
  if (!dbus_g_proxy_call (remote_object, "GetDict", &error,
			  G_TYPE_INVALID,
			  DBUS_TYPE_G_STRING_STRING_HASHTABLE, &hello_reply_dict, G_TYPE_INVALID))
    lose_gerror ("Failed to complete GetDict", error);

  printf ("reply_list: ");
  for (reply_ptr = reply_list; *reply_ptr; reply_ptr++)
    printf ("\"%s\" ", *reply_ptr);
  printf ("\n");
  g_strfreev (reply_list);

  /* FIXME; do something with hello_reply_struct */
#if 0
  dbus_g_value_free (hello_reply_struct);
  printf ("\n");
#endif

  g_hash_table_foreach (hello_reply_dict, print_hash_value, NULL);
  g_hash_table_destroy (hello_reply_dict);

  remote_object_introspectable = dbus_g_proxy_new_for_name (bus,
							    "org.designfu.SampleService",
							    "/SomeObject",
							    "org.freedesktop.DBus.Introspectable");
  if (!dbus_g_proxy_call (remote_object_introspectable, "Introspect", &error,
			  G_TYPE_INVALID,
			  G_TYPE_STRING, &introspect_data, G_TYPE_INVALID))
    lose_gerror ("Failed to complete Introspect", error);
  printf ("%s", introspect_data);
  g_free (introspect_data);

  g_object_unref (G_OBJECT (remote_object_introspectable));
  g_object_unref (G_OBJECT (remote_object));

  exit(0);
}
