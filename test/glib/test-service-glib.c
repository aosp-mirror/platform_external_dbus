/* -*- mode: C; c-file-style: "gnu" -*- */
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MyObject MyObject;
typedef struct MyObjectClass MyObjectClass;

GType my_object_get_type (void);

struct MyObject
{
  GObject parent;
};

struct MyObjectClass
{
  GObjectClass parent;
};

#define MY_TYPE_OBJECT              (my_object_get_type ())
#define MY_OBJECT_OBJECT(object)    (G_TYPE_CHECK_INSTANCE_CAST ((object), MY_TYPE_OBJECT, MyObject))
#define MY_OBJECT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), MY_TYPE_OBJECT, MyObjectClass))
#define MY_IS_OBJECT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), MY_TYPE_OBJECT))
#define MY_IS_OBJECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), MY_TYPE_OBJECT))
#define MY_OBJECT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), MY_TYPE_OBJECT, MyObjectClass))

G_DEFINE_TYPE(MyObject, my_object, G_TYPE_OBJECT)


static void
my_object_init (MyObject *obj)
{
  
}

static void
my_object_class_init (MyObjectClass *obj_class)
{
  
}
     
static GMainLoop *loop;

int
main (int argc, char **argv)
{
  DBusGConnection *connection;
  GError *error;
  GObject *obj;
  
  g_type_init ();
  
  loop = g_main_loop_new (NULL, FALSE);

  error = NULL;
  connection = dbus_g_bus_get (DBUS_BUS_STARTER,
                               &error);
  if (connection == NULL)
    {
      g_printerr ("Failed to open connection to bus: %s\n",
                  error->message);
      g_error_free (error);
      exit (1);
    }

  obj = g_object_new (MY_TYPE_OBJECT, NULL);

  dbus_g_connection_register_g_object (connection,
                                       "/org/freedesktop/my_test_object",
                                       obj);
  
  g_print ("GLib test service entering main loop\n");

  g_main_loop_run (loop);
  
  g_print ("Successfully completed %s\n", argv[0]);
  
  return 0;
}
