/* -*- mode: C; c-file-style: "gnu" -*- */
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <glib/gquark.h>

typedef struct MyObject MyObject;
typedef struct MyObjectClass MyObjectClass;

GType my_object_get_type (void);

struct MyObject
{
  GObject parent;
  char *this_is_a_string;
};

struct MyObjectClass
{
  GObjectClass parent;
};

#define MY_TYPE_OBJECT              (my_object_get_type ())
#define MY_OBJECT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), MY_TYPE_OBJECT, MyObject))
#define MY_OBJECT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), MY_TYPE_OBJECT, MyObjectClass))
#define MY_IS_OBJECT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), MY_TYPE_OBJECT))
#define MY_IS_OBJECT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), MY_TYPE_OBJECT))
#define MY_OBJECT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), MY_TYPE_OBJECT, MyObjectClass))

G_DEFINE_TYPE(MyObject, my_object, G_TYPE_OBJECT)

typedef enum
{
  MY_OBJECT_ERROR_FOO,
  MY_OBJECT_ERROR_BAR
} MyObjectError;

#define MY_OBJECT_ERROR my_object_error_quark ()

gboolean my_object_do_nothing (MyObject *obj, GError **error);

gboolean my_object_increment (MyObject *obj, gint32 x, int *ret, GError **error);

gboolean my_object_throw_error (MyObject *obj, GError **error);

gboolean my_object_uppercase (MyObject *obj, const char *str, char **ret, GError **error);

gboolean my_object_many_args (MyObject *obj, guint32 x, const char *str, double trouble, double *d_ret, char **str_ret, GError **error);

#include "test-service-glib-glue.h"

GQuark my_object_error_quark (void);

/* Properties */
enum
{
  PROP_0,
  PROP_THIS_IS_A_STRING
};

static void
my_object_finalize (GObject *object)
{
  MyObject *mobject = MY_OBJECT (object);

  g_free (mobject->this_is_a_string);

  (G_OBJECT_CLASS (my_object_parent_class)->finalize) (object);
}

static void
my_object_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  MyObject *mobject;

  mobject = MY_OBJECT (object);
  
  switch (prop_id)
    {
    case PROP_THIS_IS_A_STRING:
      g_free (mobject->this_is_a_string);
      mobject->this_is_a_string = g_value_dup_string (value);
      break;
      
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
my_object_get_property (GObject      *object,
                        guint         prop_id,
                        GValue       *value,
                        GParamSpec   *pspec)
{
  MyObject *mobject;

  mobject = MY_OBJECT (object);
  
  switch (prop_id)
    {
    case PROP_THIS_IS_A_STRING:
      g_value_set_string (value, mobject->this_is_a_string);
      break;
      
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
my_object_init (MyObject *obj)
{
  
}

static void
my_object_class_init (MyObjectClass *mobject_class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (mobject_class);

  gobject_class->finalize = my_object_finalize;
  gobject_class->set_property = my_object_set_property;
  gobject_class->get_property = my_object_get_property;
  
  g_object_class_install_property (gobject_class,
				   PROP_THIS_IS_A_STRING,
				   g_param_spec_string ("this_is_a_string",
                                                        _("Sample string"),
                                                        _("Example of a string property"),
                                                        "default value",
                                                        G_PARAM_READWRITE));
}

GQuark
my_object_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("my_object_error");

  return quark;
}

gboolean
my_object_do_nothing (MyObject *obj, GError **error)
{
  return TRUE;
}

gboolean
my_object_increment (MyObject *obj, gint32 x, int *ret, GError **error)
{
  *ret = x +1;
  return TRUE;
}

gboolean
my_object_throw_error (MyObject *obj, GError **error)
{
  g_set_error (error,
	       MY_OBJECT_ERROR,
	       MY_OBJECT_ERROR_FOO,
	       "this method always loses");
  return FALSE;
}

gboolean
my_object_uppercase (MyObject *obj, const char *str, char **ret, GError **error)
{
  *ret = g_ascii_strup (str, -1);
  return TRUE;
}

gboolean
my_object_many_args (MyObject *obj, guint32 x, const char *str, double trouble, double *d_ret, char **str_ret, GError **error)
{
  *d_ret = trouble + (x * 2);
  *str_ret = g_ascii_strup (str, -1);
  return TRUE;
}
     
static GMainLoop *loop;

int
main (int argc, char **argv)
{
  DBusGConnection *connection;
  GError *error;
  GObject *obj;
  DBusGProxy *driver_proxy;
  DBusGPendingCall *call;
  const char *v_STRING;
  guint32 v_UINT32;

  g_type_init ();

  g_printerr ("Launching test-service-glib\n");
  
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

  dbus_g_object_class_install_info (G_OBJECT_GET_CLASS (obj),
				    &dbus_glib_my_object_object_info);
  dbus_g_connection_register_g_object (connection,
                                       "/org/freedesktop/DBus/Tests/MyTestObject",
                                       obj);

  driver_proxy = dbus_g_proxy_new_for_name (connection,
                                            DBUS_SERVICE_ORG_FREEDESKTOP_DBUS,
                                            DBUS_PATH_ORG_FREEDESKTOP_DBUS,
                                            DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS);

  v_STRING = "org.freedesktop.DBus.TestSuiteGLibService";
  v_UINT32 = 0;
  call = dbus_g_proxy_begin_call (driver_proxy, "RequestName",
                                  DBUS_TYPE_STRING,
                                  &v_STRING,
                                  DBUS_TYPE_UINT32,
                                  &v_UINT32,
                                  DBUS_TYPE_INVALID);
  if (!dbus_g_proxy_end_call (driver_proxy, call,
                              &error, DBUS_TYPE_UINT32, &v_UINT32,
                              DBUS_TYPE_INVALID))
    {
      g_assert (error != NULL);
      g_printerr ("Failed to get name: %s\n",
                  error->message);
      g_error_free (error);
      exit (1);
    }
  g_assert (error == NULL);
  dbus_g_pending_call_unref (call);

  if (!(v_UINT32 == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER))
    {
      g_printerr ("Got result code %u from requesting name\n", v_UINT32);
      exit (1);
    }

  g_printerr ("GLib test service has name '%s'\n", v_STRING);
  g_printerr ("GLib test service entering main loop\n");

  g_main_loop_run (loop);
  
  g_printerr ("Successfully completed %s\n", argv[0]);
  
  return 0;
}
