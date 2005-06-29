/* -*- mode: C; c-file-style: "gnu" -*- */
#include <dbus/dbus-glib.h>
/* NOTE - outside of D-BUS core this would be
 * include <dbus/dbus-glib-bindings.h>
 */
#include "tools/dbus-glib-bindings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <glib/gquark.h>
#include "my-object-marshal.h"

typedef struct MyObject MyObject;
typedef struct MyObjectClass MyObjectClass;

GType my_object_get_type (void);

struct MyObject
{
  GObject parent;
  char *this_is_a_string;
  guint val;
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

gboolean my_object_increment (MyObject *obj, gint32 x, gint32 *ret, GError **error);

gboolean my_object_throw_error (MyObject *obj, GError **error);

gboolean my_object_uppercase (MyObject *obj, const char *str, char **ret, GError **error);

gboolean my_object_many_args (MyObject *obj, guint32 x, const char *str, double trouble, double *d_ret, char **str_ret, GError **error);

gboolean my_object_many_return (MyObject *obj, guint32 *arg0, char **arg1, gint32 *arg2, guint32 *arg3, guint32 *arg4, char **arg5, GError **error);

gboolean my_object_recursive1 (MyObject *obj, GArray *array, guint32 *len_ret, GError **error);
gboolean my_object_recursive2 (MyObject *obj, guint32 reqlen, GArray **array, GError **error);

gboolean my_object_objpath (MyObject *obj, const char *in, char **arg1, GError **error);

gboolean my_object_stringify (MyObject *obj, GValue *value, char **ret, GError **error);
gboolean my_object_unstringify (MyObject *obj, const char *str, GValue *value, GError **error);

gboolean my_object_many_uppercase (MyObject *obj, const char * const *in, char ***out, GError **error);

gboolean my_object_str_hash_len (MyObject *obj, GHashTable *table, guint *len, GError **error);

gboolean my_object_get_hash (MyObject *obj, GHashTable **table, GError **error);

gboolean my_object_increment_val (MyObject *obj, GError **error);

gboolean my_object_get_val (MyObject *obj, guint *ret, GError **error);

gboolean my_object_get_value (MyObject *obj, guint *ret, GError **error);

gboolean my_object_emit_signals (MyObject *obj, GError **error);
gboolean my_object_emit_signal2 (MyObject *obj, GError **error);

gboolean my_object_emit_frobnicate (MyObject *obj, GError **error);

gboolean my_object_terminate (MyObject *obj, GError **error);

#include "test-service-glib-glue.h"

GQuark my_object_error_quark (void);

/* Properties */
enum
{
  PROP_0,
  PROP_THIS_IS_A_STRING
};

enum
{
  FROBNICATE,
  SIG0,
  SIG1,
  SIG2,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

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
  obj->val = 0;
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
  signals[FROBNICATE] =
    g_signal_new ("frobnicate",
		  G_OBJECT_CLASS_TYPE (mobject_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__INT,
                  G_TYPE_NONE, 1, G_TYPE_INT);

  signals[SIG0] =
    g_signal_new ("sig0",
		  G_OBJECT_CLASS_TYPE (mobject_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  my_object_marshal_VOID__STRING_INT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING);

  signals[SIG1] =
    g_signal_new ("sig1",
		  G_OBJECT_CLASS_TYPE (mobject_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  my_object_marshal_VOID__STRING_BOXED,
                  G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_VALUE);

  signals[SIG2] =
    g_signal_new ("sig2",
		  G_OBJECT_CLASS_TYPE (mobject_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, DBUS_TYPE_G_STRING_STRING_HASHTABLE);
}

GQuark
my_object_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("my_object_error");

  return quark;
}

static GObject *obj;
static GObject *obj2;

gboolean
my_object_do_nothing (MyObject *obj, GError **error)
{
  return TRUE;
}

gboolean
my_object_increment (MyObject *obj, gint32 x, gint32 *ret, GError **error)
{
  *ret = x +1;
  return TRUE;
}

gboolean
my_object_throw_error (MyObject *obj, GError **error)
{
  dbus_g_error_set (error,
		    "org.freedesktop.DBus.Tests.MyObject.Foo",
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

gboolean
my_object_many_return (MyObject *obj, guint32 *arg0, char **arg1, gint32 *arg2, guint32 *arg3, guint32 *arg4, char **arg5, GError **error)
{
  *arg0 = 42;
  *arg1 = g_strdup ("42");
  *arg2 = -67;
  *arg3 = 2;
  *arg4 = 26;
  *arg5 = g_strdup ("hello world");
  return TRUE;
}

gboolean
my_object_stringify (MyObject *obj, GValue *value, char **ret, GError **error)
{
  GValue valstr = {0, };

  g_value_init (&valstr, G_TYPE_STRING);
  if (!g_value_transform (value, &valstr))
    {
      g_set_error (error,
		   MY_OBJECT_ERROR,
		   MY_OBJECT_ERROR_FOO,
		   "couldn't transform value");
      return FALSE;
    }
  *ret = g_value_dup_string (&valstr);
  g_value_unset (&valstr);
  return TRUE;
}

gboolean
my_object_unstringify (MyObject *obj, const char *str, GValue *value, GError **error)
{
  if (str[0] == '\0' || !g_ascii_isdigit (str[0])) {
    g_value_init (value, G_TYPE_STRING);
    g_value_set_string (value, str);
  } else {
    g_value_init (value, G_TYPE_INT);
    g_value_set_int (value, (int) g_ascii_strtoull (str, NULL, 10));
  } 
  return TRUE;
}

gboolean
my_object_recursive1 (MyObject *obj, GArray *array, guint32 *len_ret, GError **error)
{
  *len_ret = array->len;
  return TRUE;
}

gboolean
my_object_recursive2 (MyObject *obj, guint32 reqlen, GArray **ret, GError **error)
{
  guint32 val;
  GArray *array;
  
  array = g_array_new (FALSE, TRUE, sizeof (guint32));

  while (reqlen > 0) {
    val = 42;
    g_array_append_val (array, val);
    val = 26;
    g_array_append_val (array, val);
    reqlen--;
  }
  val = 2;
  g_array_append_val (array, val);
  *ret = array;
  return TRUE;
}

gboolean
my_object_many_uppercase (MyObject *obj, const char * const *in, char ***out, GError **error)
{
  int len;
  int i;

  len = g_strv_length ((char**) in);

  *out = g_new0 (char *, len + 1);
  for (i = 0; i < len; i++)
    {
      (*out)[i] = g_ascii_strup (in[i], -1);
    }
  (*out)[i] = NULL;
  
  return TRUE;
}

gboolean
my_object_objpath (MyObject *obj, const char *incoming, char **outgoing, GError **error)
{
  if (strcmp (incoming, "/org/freedesktop/DBus/Tests/MyTestObject"))
    {
      g_set_error (error,
		   MY_OBJECT_ERROR,
		   MY_OBJECT_ERROR_FOO,
		   "invalid incoming object");
      return FALSE;
    }
  *outgoing = g_strdup ("/org/freedesktop/DBus/Tests/MyTestObject2");
  return TRUE;
}

static void
hash_foreach (gpointer key, gpointer val, gpointer user_data)
{
  const char *keystr = key;
  const char *valstr = val;
  guint *count = user_data;

  *count += (strlen (keystr) + strlen (valstr));
  g_print ("%s -> %s\n", keystr, valstr);
}

gboolean
my_object_str_hash_len (MyObject *obj, GHashTable *table, guint *len, GError **error)
{
  *len = 0;
  g_hash_table_foreach (table, hash_foreach, len);
  return TRUE;
}

gboolean
my_object_get_hash (MyObject *obj, GHashTable **ret, GError **error)
{
  GHashTable *table;

  table = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (table, "foo", "bar");
  g_hash_table_insert (table, "baz", "whee");
  g_hash_table_insert (table, "cow", "crack");
  *ret = table;
  return TRUE;
}

gboolean
my_object_increment_val (MyObject *obj, GError **error)
{
  obj->val++;
  return TRUE;
}

gboolean
my_object_get_val (MyObject *obj, guint *ret, GError **error)
{
  *ret = obj->val;
  return TRUE;
}

gboolean
my_object_get_value (MyObject *obj, guint *ret, GError **error)
{
  *ret = obj->val;
  return TRUE;
}

gboolean
my_object_emit_frobnicate (MyObject *obj, GError **error)
{
  g_signal_emit (obj, signals[FROBNICATE], 0, 42);
  return TRUE;
}

gboolean
my_object_emit_signals (MyObject *obj, GError **error)
{
  GValue val = {0, };

  g_signal_emit (obj, signals[SIG0], 0, "foo", 22, "moo");

  g_value_init (&val, G_TYPE_STRING);
  g_value_set_string (&val, "bar");
  g_signal_emit (obj, signals[SIG1], 0, "baz", &val);
  g_value_unset (&val);

  return TRUE;
}

gboolean
my_object_emit_signal2 (MyObject *obj, GError **error)
{
  GHashTable *table;

  table = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (table, "baz", "cow");
  g_hash_table_insert (table, "bar", "foo");
  g_signal_emit (obj, signals[SIG2], 0, table);
  g_hash_table_destroy (table);
  return TRUE;
}

static GMainLoop *loop;

gboolean
my_object_terminate (MyObject *obj, GError **error)
{
  g_main_loop_quit (loop);
  return TRUE;
}

#define TEST_SERVICE_NAME "org.freedesktop.DBus.TestSuiteGLibService"

int
main (int argc, char **argv)
{
  DBusGConnection *connection;
  GError *error;
  DBusGProxy *driver_proxy;
  guint32 request_name_ret;

  g_type_init ();

  g_printerr ("Launching test-service-glib\n");

  g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
  g_log_set_always_fatal (G_LOG_LEVEL_WARNING);
  
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
  obj2 = g_object_new (MY_TYPE_OBJECT, NULL);

  dbus_g_object_type_install_info (MY_TYPE_OBJECT,
				   &dbus_glib_my_object_object_info);
  dbus_g_connection_register_g_object (connection,
                                       "/org/freedesktop/DBus/Tests/MyTestObject",
                                       obj);
  dbus_g_connection_register_g_object (connection,
                                       "/org/freedesktop/DBus/Tests/MyTestObject2",
                                       obj2);

  driver_proxy = dbus_g_proxy_new_for_name (connection,
                                            DBUS_SERVICE_DBUS,
                                            DBUS_PATH_DBUS,
                                            DBUS_INTERFACE_DBUS);

  if (!org_freedesktop_DBus_request_name (driver_proxy,
					  TEST_SERVICE_NAME,
					  0, &request_name_ret, &error))
    {
      g_assert (error != NULL);
      g_printerr ("Failed to get name: %s\n",
		  error->message);
      g_clear_error (&error);
      exit (1);
    }

  if (!(request_name_ret == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER))
    {
      g_printerr ("Got result code %u from requesting name\n", request_name_ret);
      exit (1);
    }

  g_printerr ("GLib test service has name '%s'\n", TEST_SERVICE_NAME);
  g_printerr ("GLib test service entering main loop\n");

  g_main_loop_run (loop);
  
  g_printerr ("Successfully completed %s\n", argv[0]);
  
  return 0;
}
