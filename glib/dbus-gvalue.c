#include <dbus-gvalue.h>

gboolean
dbus_gvalue_demarshal (DBusMessageIter *iter, GValue *value)
{
  gboolean can_convert = TRUE;

  switch (dbus_message_iter_get_arg_type (iter))
    {
#define MAP(d_t, d_get, g_t, g_set) \
    case DBUS_##d_t: \
      g_value_init (value, G_##g_t); \
      g_value_##g_set (value, dbus_message_iter_##d_get (iter)); \
      break

    MAP(TYPE_BYTE, get_byte, TYPE_UCHAR, set_uchar);
    MAP(TYPE_BOOLEAN, get_boolean, TYPE_BOOLEAN , set_boolean);
    MAP(TYPE_INT32, get_int32, TYPE_INT , set_int);
    MAP(TYPE_UINT32, get_uint32, TYPE_UINT , set_uint);
#ifdef DBUS_HAVE_INT64
    MAP(TYPE_INT64, get_int64, TYPE_INT64 , set_int64);
    MAP(TYPE_UINT64, get_uint64, TYPE_UINT64 , set_uint64);
#endif
    MAP(TYPE_DOUBLE, get_double, TYPE_DOUBLE , set_double);
    case DBUS_TYPE_STRING:
      {
        char *s; /* FIXME use a const string accessor */

        g_value_init (value, G_TYPE_STRING);

        s = dbus_message_iter_get_string (iter);
        g_value_set_string (value, s);
        g_free (s);
      }
      break;
    default:
      /* FIXME: we need to define custom boxed types for arrays
	 etc. so we can map them transparently / pleasantly */
      can_convert = FALSE;
      break;
    }
#undef MAP
  return can_convert;
}
    
gboolean
dbus_gvalue_marshal (DBusMessageIter *iter, GValue *value)
{
  gboolean can_convert = TRUE;
  GType value_type = G_VALUE_TYPE (value);

  value_type = G_VALUE_TYPE (value);
  
  switch (value_type)
    {
    case G_TYPE_CHAR:
      dbus_message_iter_append_byte (iter,
                                     g_value_get_char (value));
      break;
    case G_TYPE_UCHAR:
      dbus_message_iter_append_byte (iter,
                                     g_value_get_uchar (value));
      break;
    case G_TYPE_BOOLEAN:
      dbus_message_iter_append_boolean (iter,
                                        g_value_get_boolean (value));
      break;
    case G_TYPE_INT:
      dbus_message_iter_append_int32 (iter,
                                      g_value_get_int (value));
      break;
    case G_TYPE_UINT:
      dbus_message_iter_append_uint32 (iter,
                                       g_value_get_uint (value));
      break;
      /* long gets cut to 32 bits so the remote API is consistent
       * on all architectures
       */
    case G_TYPE_LONG:
      dbus_message_iter_append_int32 (iter,
                                      g_value_get_long (value));
      break;
    case G_TYPE_ULONG:
      dbus_message_iter_append_uint32 (iter,
                                       g_value_get_ulong (value));
      break;
#ifdef DBUS_HAVE_INT64
    case G_TYPE_INT64:
      dbus_message_iter_append_int64 (iter,
                                      g_value_get_int64 (value));
      break;
    case G_TYPE_UINT64:
      dbus_message_iter_append_uint64 (iter,
                                       g_value_get_uint64 (value));
      break;
#endif
    case G_TYPE_FLOAT:
      dbus_message_iter_append_double (iter,
                                       g_value_get_float (value));
      break;
    case G_TYPE_DOUBLE:
      dbus_message_iter_append_double (iter,
                                       g_value_get_double (value));
      break;
    case G_TYPE_STRING:
      /* FIXME, the GValue string may not be valid UTF-8 */
      dbus_message_iter_append_string (iter,
                                       g_value_get_string (value));
      break;
    default:
      /* FIXME: we need to define custom boxed types for arrays
	 etc. so we can map them transparently / pleasantly */
      can_convert = FALSE;
      break;
    }

  return can_convert;
}

