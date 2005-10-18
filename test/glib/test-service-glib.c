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

#define MY_OBJECT_ERROR (my_object_error_quark ())

#define MY_TYPE_ERROR (my_object_error_get_type ()) 

gboolean my_object_do_nothing (MyObject *obj, GError **error);

gboolean my_object_increment (MyObject *obj, gint32 x, gint32 *ret, GError **error);

gint32   my_object_increment_retval (MyObject *obj, gint32 x);

gint32   my_object_increment_retval_error (MyObject *obj, gint32 x, GError **error);

gboolean my_object_throw_error (MyObject *obj, GError **error);

gboolean my_object_uppercase (MyObject *obj, const char *str, char **ret, GError **error);

gboolean my_object_many_args (MyObject *obj, guint32 x, const char *str, double trouble, double *d_ret, char **str_ret, GError **error);

gboolean my_object_many_return (MyObject *obj, guint32 *arg0, char **arg1, gint32 *arg2, guint32 *arg3, guint32 *arg4, const char **arg5, GError **error);

gboolean my_object_recursive1 (MyObject *obj, GArray *array, guint32 *len_ret, GError **error);
gboolean my_object_recursive2 (MyObject *obj, guint32 reqlen, GArray **array, GError **error);

gboolean my_object_many_stringify (MyObject *obj, GHashTable *vals, GHashTable **ret, GError **error);

gboolean my_object_rec_arrays (MyObject *obj, GPtrArray *in, GPtrArray **ret, GError **error);

gboolean my_object_objpath (MyObject *obj, const char *in, const char **arg1, GError **error);

gboolean my_object_get_objs (MyObject *obj, GPtrArray **objs, GError **error);

gboolean my_object_stringify (MyObject *obj, GValue *value, char **ret, GError **error);
gboolean my_object_unstringify (MyObject *obj, const char *str, GValue *value, GError **error);

gboolean my_object_many_uppercase (MyObject *obj, const char * const *in, char ***out, GError **error);

gboolean my_object_str_hash_len (MyObject *obj, GHashTable *table, guint *len, GError **error);

gboolean my_object_send_car (MyObject *obj, GValueArray *invals, GValueArray **outvals, GError **error);

gboolean my_object_get_hash (MyObject *obj, GHashTable **table, GError **error);

gboolean my_object_increment_val (MyObject *obj, GError **error);

gboolean my_object_get_val (MyObject *obj, guint *ret, GError **error);

gboolean my_object_get_value (MyObject *obj, guint *ret, GError **error);

gboolean my_object_emit_signals (MyObject *obj, GError **error);
gboolean my_object_emit_signal2 (MyObject *obj, GError **error);

gboolean my_object_emit_frobnicate (MyObject *obj, GError **error);

gboolean my_object_echo_variant (MyObject *obj, GValue *variant, GValue *ret, GError **error);

gboolean my_object_process_variant_of_array_of_ints123 (MyObject *obj, GValue *variant, GError **error);

gboolean my_object_terminate (MyObject *obj, GError **error);

void my_object_async_increment (MyObject *obj, gint32 x, DBusGMethodInvocation *context);

void my_object_async_throw_error (MyObject *obj, DBusGMethodInvocation *context);

#include "test-service-glib-glue.h"

GQuark my_object_error_quark (void);

GType my_object_error_get_type (void);

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

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
my_object_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{

			ENUM_ENTRY (MY_OBJECT_ERROR_FOO, "Foo"),
			ENUM_ENTRY (MY_OBJECT_ERROR_BAR, "Bar"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("MyObjectError", values);
	}

	return etype;
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

gint32
my_object_increment_retval (MyObject *obj, gint32 x)
{
  return x + 1;
}

gint32
my_object_increment_retval_error (MyObject *obj, gint32 x, GError **error)
{
  if (x + 1 > 10)
    {
      g_set_error (error,
		   MY_OBJECT_ERROR,
		   MY_OBJECT_ERROR_FOO,
		   "%s",
		   "x is bigger than 9");    
      return FALSE;
    }
  return x + 1;
}

gboolean
my_object_throw_error (MyObject *obj, GError **error)
{
  g_set_error (error,
	       MY_OBJECT_ERROR,
	       MY_OBJECT_ERROR_FOO,
	       "%s",
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
my_object_many_return (MyObject *obj, guint32 *arg0, char **arg1, gint32 *arg2, guint32 *arg3, guint32 *arg4, const char **arg5, GError **error)
{
  *arg0 = 42;
  *arg1 = g_strdup ("42");
  *arg2 = -67;
  *arg3 = 2;
  *arg4 = 26;
  *arg5 = "hello world"; /* Annotation specifies as const */
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

static void
hash_foreach_stringify (gpointer key, gpointer val, gpointer user_data)
{
  const char *keystr = key;
  const GValue *value = val;
  GValue *sval;
  GHashTable *ret = user_data;

  sval = g_new0 (GValue, 1);
  g_value_init (sval, G_TYPE_STRING);
  if (!g_value_transform (value, sval))
    g_assert_not_reached ();

  g_hash_table_insert (ret, g_strdup (keystr), sval);
}

static void
unset_and_free_gvalue (gpointer val)
{
  g_value_unset (val);
  g_free (val);
}

gboolean
my_object_many_stringify (MyObject *obj, GHashTable /* char * -> GValue * */ *vals, GHashTable /* char * -> GValue * */ **ret, GError **error)
{
  *ret = g_hash_table_new_full (g_str_hash, g_str_equal,
				g_free, unset_and_free_gvalue);
  g_hash_table_foreach (vals, hash_foreach_stringify, *ret);
  return TRUE;
}

gboolean
my_object_rec_arrays (MyObject *obj, GPtrArray *in, GPtrArray **ret, GError **error)
{
  char **strs;
  GArray *ints;
  guint v_UINT;
  
  if (in->len != 2)
    {
      g_set_error (error,
		   MY_OBJECT_ERROR,
		   MY_OBJECT_ERROR_FOO,
		   "invalid array len");
      return FALSE;
    }
  
  strs = g_ptr_array_index (in, 0);
  if (!*strs || strcmp (*strs, "foo"))
    {
      g_set_error (error,
		   MY_OBJECT_ERROR,
		   MY_OBJECT_ERROR_FOO,
		   "invalid string 0");
      return FALSE;
    }
  strs++;
  if (!*strs || strcmp (*strs, "bar"))
    {
      g_set_error (error,
		   MY_OBJECT_ERROR,
		   MY_OBJECT_ERROR_FOO,
		   "invalid string 1");
      return FALSE;
    }
  strs++;
  if (*strs)
    {
      g_set_error (error,
		   MY_OBJECT_ERROR,
		   MY_OBJECT_ERROR_FOO,
		   "invalid string array len in pos 0");
      return FALSE;
    }
  strs = g_ptr_array_index (in, 1);
  if (!*strs || strcmp (*strs, "baz"))
    {
      g_set_error (error,
		   MY_OBJECT_ERROR,
		   MY_OBJECT_ERROR_FOO,
		   "invalid string 0");
      return FALSE;
    }
  strs++;
  if (!*strs || strcmp (*strs, "whee"))
    {
      g_set_error (error,
		   MY_OBJECT_ERROR,
		   MY_OBJECT_ERROR_FOO,
		   "invalid string 1");
      return FALSE;
    }
  strs++;
  if (!*strs || strcmp (*strs, "moo"))
    {
      g_set_error (error,
		   MY_OBJECT_ERROR,
		   MY_OBJECT_ERROR_FOO,
		   "invalid string 2");
      return FALSE;
    }
  strs++;
  if (*strs)
    {
      g_set_error (error,
		   MY_OBJECT_ERROR,
		   MY_OBJECT_ERROR_FOO,
		   "invalid string array len in pos 1");
      return FALSE;
    }

  *ret = g_ptr_array_new ();

  ints = g_array_new (TRUE, TRUE, sizeof (guint));
  v_UINT = 10;
  g_array_append_val (ints, v_UINT);
  v_UINT = 42;
  g_array_append_val (ints, v_UINT);
  v_UINT = 27;
  g_array_append_val (ints, v_UINT);
  g_ptr_array_add (*ret, ints);

  ints = g_array_new (TRUE, TRUE, sizeof (guint));
  v_UINT = 30;
  g_array_append_val (ints, v_UINT);
  g_ptr_array_add (*ret, ints);
  return TRUE;
}

gboolean
my_object_objpath (MyObject *obj, const char *incoming, const char **outgoing, GError **error)
{
  if (strcmp (incoming, "/org/freedesktop/DBus/Tests/MyTestObject"))
    {
      g_set_error (error,
		   MY_OBJECT_ERROR,
		   MY_OBJECT_ERROR_FOO,
		   "invalid incoming object");
      return FALSE;
    }
  *outgoing = "/org/freedesktop/DBus/Tests/MyTestObject2";
  return TRUE;
}

gboolean
my_object_get_objs (MyObject *obj, GPtrArray **objs, GError **error)
{
  *objs = g_ptr_array_new ();

  g_ptr_array_add (*objs, g_strdup ("/org/freedesktop/DBus/Tests/MyTestObject"));
  g_ptr_array_add (*objs, g_strdup ("/org/freedesktop/DBus/Tests/MyTestObject2"));

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
my_object_send_car (MyObject *obj, GValueArray *invals, GValueArray **outvals, GError **error)
{
  if (invals->n_values != 3
      || G_VALUE_TYPE (g_value_array_get_nth (invals, 0)) != G_TYPE_STRING
      || G_VALUE_TYPE (g_value_array_get_nth (invals, 1)) != G_TYPE_UINT
      || G_VALUE_TYPE (g_value_array_get_nth (invals, 2)) != G_TYPE_VALUE)
    {
      g_set_error (error,
		   MY_OBJECT_ERROR,
		   MY_OBJECT_ERROR_FOO,
		   "invalid incoming values");
      return FALSE;
    }
  *outvals = g_value_array_new (2);
  g_value_array_append (*outvals, NULL);
  g_value_init (g_value_array_get_nth (*outvals, (*outvals)->n_values - 1), G_TYPE_UINT);
  g_value_set_uint (g_value_array_get_nth (*outvals, (*outvals)->n_values - 1),
		    g_value_get_uint (g_value_array_get_nth (invals, 1)) + 1);
  g_value_array_append (*outvals, NULL);
  g_value_init (g_value_array_get_nth (*outvals, (*outvals)->n_values - 1), DBUS_TYPE_G_OBJECT_PATH);
  g_value_set_boxed (g_value_array_get_nth (*outvals, (*outvals)->n_values - 1),
		     g_strdup ("/org/freedesktop/DBus/Tests/MyTestObject2"));
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
my_object_echo_variant (MyObject *obj, GValue *variant, GValue *ret, GError **error)
{
    GType t;
    t = G_VALUE_TYPE(variant);
    g_value_init (ret, t);
    g_value_copy (variant, ret);

    return TRUE;
}

gboolean 
my_object_process_variant_of_array_of_ints123 (MyObject *obj, GValue *variant, GError **error)
{
  GArray *array;
  int i;
  int j;

  j = 0;

  array = (GArray *)g_value_get_boxed (variant);

  for (i = 0; i <= 2; i++)
    {
      j = g_array_index (array, int, i);
      if (j != i + 1)
        goto error;
    }

  return TRUE;

error:
  *error = g_error_new (MY_OBJECT_ERROR,
		       MY_OBJECT_ERROR_FOO,
		       "Error decoding a variant of type ai (i + 1 = %i, j = %i)",
		       i, j + 1);
  return FALSE;
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

typedef struct {
  gint32 x;
  DBusGMethodInvocation *context;
} IncrementData;

static gboolean
do_async_increment (IncrementData *data)
{
  gint32 newx = data->x + 1;
  dbus_g_method_return (data->context, newx);
  g_free (data);
  return FALSE;
}

void
my_object_async_increment (MyObject *obj, gint32 x, DBusGMethodInvocation *context)
{
  IncrementData *data = g_new0 (IncrementData, 1);
  data->x = x;
  data->context = context;
  g_idle_add ((GSourceFunc)do_async_increment, data);
}

static gboolean
do_async_error (IncrementData *data)
{
  GError *error;
  error = g_error_new (MY_OBJECT_ERROR,
		       MY_OBJECT_ERROR_FOO,
		       "%s",
		       "this method always loses");
  dbus_g_method_return_error (data->context, error);
  g_free (data);
  return FALSE;
}

void
my_object_async_throw_error (MyObject *obj, DBusGMethodInvocation *context)
{
  IncrementData *data = g_new0(IncrementData, 1);
  data->context = context;
  g_idle_add ((GSourceFunc)do_async_error,  data);
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
  g_thread_init (NULL); dbus_g_thread_init ();

  dbus_g_object_type_install_info (MY_TYPE_OBJECT,
				   &dbus_glib_my_object_object_info);

  dbus_g_error_domain_register (MY_OBJECT_ERROR,
				NULL,
				MY_TYPE_ERROR);

  g_printerr ("Launching test-service-glib\n");

  loop = g_main_loop_new (NULL, FALSE);

  {
    GLogLevelFlags fatal_mask;
    
    fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
    fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
    g_log_set_always_fatal (fatal_mask);
  }

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
