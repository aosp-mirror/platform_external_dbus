/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gvalue.c GValue to-from DBusMessageIter
 *
 * Copyright (C) 2004 Ximian, Inc.
 * Copyright (C) 2005 Red Hat, Inc.
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

#include <dbus-gvalue.h>
#include "dbus/dbus-signature.h"

/* This is slightly evil, we don't use g_value_set_foo() functions */
#define MAP_BASIC_INIT(d_t, g_t)                                     \
    case DBUS_TYPE_##d_t:                                       \
      g_value_init (value, G_TYPE_##g_t);                       \
      break

gboolean
dbus_gvalue_init      (int     type,
		       GValue *value)
{
  gboolean can_convert;

  can_convert = TRUE;

  switch (type)
    {
      MAP_BASIC_INIT (BOOLEAN, BOOLEAN);
      MAP_BASIC_INIT (BYTE,    UCHAR);
      MAP_BASIC_INIT (INT32,   INT);
      MAP_BASIC_INIT (UINT32,  UINT);
      MAP_BASIC_INIT (INT64,   INT64);
      MAP_BASIC_INIT (UINT64,  UINT64);
      MAP_BASIC_INIT (DOUBLE,  DOUBLE);

    case DBUS_TYPE_INT16:
        g_value_init (value, G_TYPE_INT);
      break;
    case DBUS_TYPE_UINT16:
        g_value_init (value, G_TYPE_UINT);
      break;
      
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE:
        g_value_init (value, G_TYPE_STRING);
      break;
      
    case DBUS_TYPE_STRUCT:
    case DBUS_TYPE_ARRAY:
    case DBUS_TYPE_VARIANT:
    default:
      can_convert = FALSE;
    }
#undef MAP_BASIC_INIT
  return can_convert;
}

/* FIXME - broken for containers
 */
static int
base_type_from_signature  (const char *signature)
{
  DBusSignatureIter iter;

  dbus_signature_iter_init (&iter, signature);

  return dbus_signature_iter_get_current_type (&iter);
}

const char *
dbus_gvalue_genmarshal_name_from_type (const char *signature)
{
  int type;

  type = base_type_from_signature (signature);
  switch (type)
    {
    case DBUS_TYPE_BOOLEAN:
      return "BOOLEAN";
    case DBUS_TYPE_BYTE:
      return "UCHAR";
    case DBUS_TYPE_INT32:
      return "INT";
    case DBUS_TYPE_UINT32:
      return "UINT";
    case DBUS_TYPE_INT64:
      return "INT64";
    case DBUS_TYPE_UINT64:
      return "UINT64";
    case DBUS_TYPE_DOUBLE:
      return "DOUBLE";
    case DBUS_TYPE_INT16:
      return "INT";
      break;
    case DBUS_TYPE_UINT16:
      return "UINT";
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE:
      return "STRING";
      
    case DBUS_TYPE_STRUCT:
    case DBUS_TYPE_ARRAY:
    case DBUS_TYPE_VARIANT:
      return NULL;
    }
  return NULL;
}

const char *
dbus_gvalue_binding_type_from_type (const char *signature)
{
  int type;

  type = base_type_from_signature (signature);

#define STRINGIFY(x) \
  case x: \
    return (#x)
  
  switch (type)
    {
      STRINGIFY(DBUS_TYPE_BOOLEAN);
      STRINGIFY(DBUS_TYPE_BYTE);
      STRINGIFY(DBUS_TYPE_INT32);
      STRINGIFY(DBUS_TYPE_UINT32);
      STRINGIFY(DBUS_TYPE_INT64);
      STRINGIFY(DBUS_TYPE_UINT64);
      STRINGIFY(DBUS_TYPE_DOUBLE);
    case DBUS_TYPE_INT16:
      return "DBUS_TYPE_INT32";
    case DBUS_TYPE_UINT16:
      return "DBUS_TYPE_UINT32";
    STRINGIFY(DBUS_TYPE_STRING);
    STRINGIFY(DBUS_TYPE_OBJECT_PATH);
    STRINGIFY(DBUS_TYPE_SIGNATURE);

    case DBUS_TYPE_STRUCT:
    case DBUS_TYPE_ARRAY:
    case DBUS_TYPE_VARIANT:
      return NULL;
    }
#undef STRINGIFY
  return NULL;
}

const char *
dbus_gvalue_ctype_from_type (const char *signature, gboolean in)
{
  int type;

  type = base_type_from_signature (signature);

  switch (type)
    {
    case DBUS_TYPE_BOOLEAN:
      return "gboolean";
    case DBUS_TYPE_BYTE:
      return "guchar";
    case DBUS_TYPE_INT32:
      return "gint32";
    case DBUS_TYPE_UINT32:
      return "guint32";
    case DBUS_TYPE_INT64:
      return "gint64";
    case DBUS_TYPE_UINT64:
      return "guint64";
    case DBUS_TYPE_DOUBLE:
      return "gdouble";
    case DBUS_TYPE_INT16:
      return "gint";
      break;
    case DBUS_TYPE_UINT16:
      return "guint";
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE:
      /* FIXME - kind of a hack */
      if (in)
	return "const char *";
      else
	return "char *";
    case DBUS_TYPE_STRUCT:
    case DBUS_TYPE_ARRAY:
    case DBUS_TYPE_VARIANT:
      return NULL;
    }
  return NULL;
}

const char *
dbus_gtype_to_dbus_type (GType type)
{
  switch (type)
    {
    case G_TYPE_CHAR:
    case G_TYPE_UCHAR:
      return DBUS_TYPE_BYTE_AS_STRING;
      
    case G_TYPE_BOOLEAN:
      return DBUS_TYPE_BOOLEAN_AS_STRING;

      /* long gets cut to 32 bits so the remote API is consistent
       * on all architectures
       */
      
    case G_TYPE_LONG:
    case G_TYPE_INT:
      return DBUS_TYPE_INT32_AS_STRING;
    case G_TYPE_ULONG:
    case G_TYPE_UINT:
      return DBUS_TYPE_UINT32_AS_STRING;

    case G_TYPE_INT64:
      return DBUS_TYPE_INT64_AS_STRING;

    case G_TYPE_UINT64:
      return DBUS_TYPE_UINT64_AS_STRING;
      
    case G_TYPE_FLOAT:
    case G_TYPE_DOUBLE:
      return DBUS_TYPE_DOUBLE_AS_STRING;

    case G_TYPE_STRING:
      return DBUS_TYPE_STRING_AS_STRING;

    default:
      return NULL;
    }
}

gboolean
dbus_gvalue_demarshal (DBusMessageIter *iter, GValue *value)
{
  gboolean can_convert = TRUE;

  g_assert (sizeof (dbus_bool_t) == sizeof (value->data[0].v_int));

  dbus_gvalue_init (dbus_message_iter_get_arg_type (iter), value);
  
/* This is slightly evil, we don't use g_value_set_foo() functions */
#define MAP_BASIC(d_t, g_t)                                     \
    case DBUS_TYPE_##d_t:                                       \
      dbus_message_iter_get_basic (iter, &value->data[0]);      \
      break

  switch (dbus_message_iter_get_arg_type (iter))
    {
      MAP_BASIC (BOOLEAN, BOOLEAN);
      MAP_BASIC (BYTE,    UCHAR);
      MAP_BASIC (INT32,   INT);
      MAP_BASIC (UINT32,  UINT);
      MAP_BASIC (INT64,   INT64);
      MAP_BASIC (UINT64,  UINT64);
      MAP_BASIC (DOUBLE,  DOUBLE);

    case DBUS_TYPE_INT16:
      {
        dbus_int16_t v;
        dbus_message_iter_get_basic (iter, &v);
        g_value_set_int (value, v);
      }
      break;
    case DBUS_TYPE_UINT16:
      {
        dbus_uint16_t v;
        dbus_message_iter_get_basic (iter, &v);
        g_value_set_uint (value, v);
      }
      break;
      
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE:
      {
        const char *s;

        dbus_message_iter_get_basic (iter, &s);
        g_value_set_string (value, s);
      }
      break;
      
    case DBUS_TYPE_STRUCT:
    case DBUS_TYPE_ARRAY:
    case DBUS_TYPE_VARIANT:
    default:
      can_convert = FALSE;
    }
#undef MAP_BASIC
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
      {
        char b = g_value_get_char (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_BYTE,
                                             &b))
          goto nomem;
      }
      break;
    case G_TYPE_UCHAR:
      {
        unsigned char b = g_value_get_uchar (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_BYTE,
                                             &b))
          goto nomem;
      }
      break;
    case G_TYPE_BOOLEAN:
      {
        dbus_bool_t b = g_value_get_boolean (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_BOOLEAN,
                                             &b))
          goto nomem;
      }
      break;
    case G_TYPE_INT:
      {
        dbus_int32_t v = g_value_get_int (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_INT32,
                                             &v))
          goto nomem;
      }
      break;
    case G_TYPE_UINT:
      {
        dbus_uint32_t v = g_value_get_uint (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_UINT32,
                                             &v))
          goto nomem;
      }
      break;
      /* long gets cut to 32 bits so the remote API is consistent
       * on all architectures
       */
    case G_TYPE_LONG:
      {
        dbus_int32_t v = g_value_get_long (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_INT32,
                                             &v))
          goto nomem;
      }
      break;
    case G_TYPE_ULONG:
      {
        dbus_uint32_t v = g_value_get_ulong (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_UINT32,
                                             &v))
          goto nomem;
      }
      break;
    case G_TYPE_INT64:
      {
        gint64 v = g_value_get_int64 (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_INT64,
                                             &v))
          goto nomem;
      }
      break;
    case G_TYPE_UINT64:
      {
        guint64 v = g_value_get_uint64 (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_UINT64,
                                             &v))
          goto nomem;
      }
      break;
    case G_TYPE_FLOAT:
      {
        double v = g_value_get_float (value);
        
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_DOUBLE,
                                             &v))
          goto nomem;
      }
      break;
    case G_TYPE_DOUBLE:
      {
        double v = g_value_get_double (value);
        
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_DOUBLE,
                                             &v))
          goto nomem;
      }
      break;
    case G_TYPE_STRING:
      /* FIXME, the GValue string may not be valid UTF-8 */
      {
        const char *v = g_value_get_string (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_STRING,
                                             &v))
          goto nomem;
      }
      break;
      
    default:
      /* FIXME: we need to define custom boxed types for arrays
	 etc. so we can map them transparently / pleasantly */
      can_convert = FALSE;
      break;
    }

  return can_convert;

 nomem:
  g_error ("no memory");
  return FALSE;
}

