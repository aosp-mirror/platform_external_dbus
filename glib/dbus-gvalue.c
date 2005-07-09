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

#include "config.h"
#include "dbus-gtest.h"
#include "dbus-gvalue.h"
#include "dbus-gobject.h"
#include "dbus-gvalue-utils.h"
#include "dbus/dbus-glib.h"
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include "dbus/dbus-signature.h"

static gboolean demarshal_static_variant (DBusGValueMarshalCtx    *context,
					  DBusMessageIter         *iter,
					  GValue                  *value,
					  GError                 **error);
static gpointer dbus_g_value_copy (gpointer value);


struct _DBusGValue
{
  enum {
    DBUS_G_VALUE_TYPE_TOPLEVEL,
    DBUS_G_VALUE_TYPE_ITERATOR
  } type;
  union {
    struct {
      DBusGConnection        *connection;
      DBusGProxy             *proxy;
      DBusMessage            *message;
      char                   *signature;
    } toplevel;
    struct {
      DBusGValue             *toplevel;
      DBusMessageIter         iterator;
    } recurse;
  } value;
};

static gboolean marshal_basic                   (DBusMessageIter           *iter,
						 const GValue              *value);
static gboolean demarshal_basic                 (DBusGValueMarshalCtx      *context,
						 DBusMessageIter           *iter,
						 GValue                    *value,
						 GError                   **error);
static gboolean marshal_strv                    (DBusMessageIter           *iter,
						 const GValue              *value);
static gboolean demarshal_strv                  (DBusGValueMarshalCtx      *context,
						 DBusMessageIter           *iter,
						 GValue                    *value,
						 GError                   **error);
static gboolean marshal_variant                 (DBusMessageIter           *iter,
						 const GValue              *value);
static gboolean demarshal_variant               (DBusGValueMarshalCtx      *context,
						 DBusMessageIter           *iter,
						 GValue                    *value,
						 GError                   **error);
static gboolean marshal_proxy                   (DBusMessageIter           *iter,
						 const GValue             *value);
static gboolean demarshal_proxy                 (DBusGValueMarshalCtx      *context,
						 DBusMessageIter           *iter,
						 GValue                    *value,
						 GError                   **error);
static gboolean marshal_object_path             (DBusMessageIter           *iter,
						 const GValue             *value);
static gboolean demarshal_object_path           (DBusGValueMarshalCtx      *context,
						 DBusMessageIter           *iter,
						 GValue                    *value,
						 GError                   **error);
static gboolean marshal_object                  (DBusMessageIter           *iter,
						 const GValue              *value);
static gboolean demarshal_object                (DBusGValueMarshalCtx      *context,
						 DBusMessageIter           *iter,
						 GValue                    *value,
						 GError                   **error);
static gboolean marshal_map                     (DBusMessageIter           *iter,
						 const GValue              *value);
static gboolean demarshal_map                   (DBusGValueMarshalCtx      *context,
						 DBusMessageIter           *iter,
						 GValue                    *value,
						 GError                   **error);

static gboolean marshal_collection              (DBusMessageIter           *iter,
						 const GValue              *value);
static gboolean marshal_collection_ptrarray     (DBusMessageIter           *iter,
						 const GValue              *value);
static gboolean marshal_collection_array        (DBusMessageIter           *iter,
						 const GValue              *value);
static gboolean demarshal_collection            (DBusGValueMarshalCtx      *context,
						 DBusMessageIter           *iter,
						 GValue                    *value,
						 GError                   **error);
static gboolean demarshal_collection_ptrarray   (DBusGValueMarshalCtx      *context,
						 DBusMessageIter           *iter,
						 GValue                    *value,
						 GError                   **error);
static gboolean demarshal_collection_array      (DBusGValueMarshalCtx      *context,
						 DBusMessageIter           *iter,
						 GValue                    *value,
						 GError                   **error);

static gboolean marshal_recurse                 (DBusMessageIter           *iter,
						 const GValue              *value);
static gboolean demarshal_recurse               (DBusGValueMarshalCtx      *context,
						 DBusMessageIter           *iter,
						 GValue                    *value,
						 GError                   **error);

typedef gboolean (*DBusGValueMarshalFunc)       (DBusMessageIter           *iter,
						 const GValue              *value);
typedef gboolean (*DBusGValueDemarshalFunc)     (DBusGValueMarshalCtx      *context,
						 DBusMessageIter           *iter,
						 GValue                    *value,
						 GError                   **error);

typedef struct {
  DBusGValueMarshalFunc       marshaller;
  DBusGValueDemarshalFunc     demarshaller;
} DBusGTypeMarshalVtable;

typedef struct {
  const char                       *sig;
  const DBusGTypeMarshalVtable     *vtable;
} DBusGTypeMarshalData;

static GQuark
dbus_g_type_metadata_data_quark ()
{
  static GQuark quark;
  if (!quark)
    quark = g_quark_from_static_string ("DBusGTypeMetaData");
  
  return quark;
}

static void
set_type_metadata (GType type, const DBusGTypeMarshalData *data)
{
  g_type_set_qdata (type, dbus_g_type_metadata_data_quark (), (gpointer) data);
}

#define MAP_BASIC(d_t, g_t)                     \
    case DBUS_TYPE_##d_t:                       \
      return G_TYPE_##g_t;
static GType
typecode_to_gtype (int type)
{
  switch (type)
    {
      MAP_BASIC (BOOLEAN, BOOLEAN);
      MAP_BASIC (BYTE,    UCHAR);
      MAP_BASIC (INT16,   INT);
      MAP_BASIC (INT32,   INT);
      MAP_BASIC (UINT16,  UINT);
      MAP_BASIC (UINT32,  UINT);
      MAP_BASIC (INT64,   INT64);
      MAP_BASIC (UINT64,  UINT64);
      MAP_BASIC (DOUBLE,  DOUBLE);
      MAP_BASIC (STRING,  STRING);
    default:
      return G_TYPE_INVALID;
    }
}
#undef MAP_BASIC

static gboolean
dbus_typecode_maps_to_basic (int typecode)
{
  return typecode_to_gtype (typecode) != G_TYPE_INVALID;
}

static GType
basic_typecode_to_gtype (int typecode)
{
  g_assert (dbus_type_is_basic (typecode));
  g_assert (dbus_typecode_maps_to_basic (typecode));
  return typecode_to_gtype (typecode);
}

static void
register_basic (int typecode, const DBusGTypeMarshalData *typedata)
{
  set_type_metadata (basic_typecode_to_gtype (typecode), typedata);
}

void
dbus_g_value_types_init (void)
{
  static gboolean types_initialized;


  if (types_initialized)
    return;

  g_assert (sizeof (DBusGValueIterator) >= sizeof (DBusMessageIter));

  dbus_g_type_specialized_init ();
  dbus_g_type_specialized_builtins_init ();
  
  static const DBusGTypeMarshalVtable basic_vtable = {
    marshal_basic,
    demarshal_basic
  };

  /* Register basic types */
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_BOOLEAN_AS_STRING,
      &basic_vtable,
    };
    register_basic (DBUS_TYPE_BOOLEAN, &typedata);
  }
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_BYTE_AS_STRING,
      &basic_vtable,
    };
    register_basic (DBUS_TYPE_BYTE, &typedata);
  }
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_INT16_AS_STRING,
      &basic_vtable,
    };
    register_basic (DBUS_TYPE_INT16, &typedata);
  }
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_UINT16_AS_STRING,
      &basic_vtable,
    };
    register_basic (DBUS_TYPE_UINT16, &typedata);
  }
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_UINT32_AS_STRING,
      &basic_vtable,
    };
    register_basic (DBUS_TYPE_UINT32, &typedata);
  }
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_INT32_AS_STRING,
      &basic_vtable,
    };
    register_basic (DBUS_TYPE_INT32, &typedata);
  }
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_UINT64_AS_STRING,
      &basic_vtable,
    };
    register_basic (DBUS_TYPE_UINT64, &typedata);
  }
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_INT64_AS_STRING,
      &basic_vtable,
    };
    register_basic (DBUS_TYPE_INT64, &typedata);
  }
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_DOUBLE_AS_STRING,
      &basic_vtable,
    };
    register_basic (DBUS_TYPE_DOUBLE, &typedata);
  }
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_STRING_AS_STRING,
      &basic_vtable,
    };
    register_basic (DBUS_TYPE_STRING, &typedata);
  }
  /* fundamental GTypes that don't map 1:1 with D-BUS types */
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_BYTE_AS_STRING,
      &basic_vtable,
    };
    set_type_metadata (G_TYPE_CHAR, &typedata);
  }
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_INT32_AS_STRING,
      &basic_vtable,
    };
    set_type_metadata (G_TYPE_LONG, &typedata);
  }
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_UINT32_AS_STRING,
      &basic_vtable,
    };
    set_type_metadata (G_TYPE_ULONG, &typedata);
  }
  {
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_DOUBLE_AS_STRING,
      &basic_vtable,
    };
    set_type_metadata (G_TYPE_FLOAT, &typedata);
  }

  /* Register complex types with builtin GType mappings */
  {
    static const DBusGTypeMarshalVtable vtable = {
      marshal_variant,
      demarshal_variant
    };
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_VARIANT_AS_STRING,
      &vtable
    };
    set_type_metadata (G_TYPE_VALUE, &typedata);
  };
  {
    static const DBusGTypeMarshalVtable vtable = {
      marshal_strv,
      demarshal_strv
    };
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_STRING_AS_STRING,
      &vtable
    };
    set_type_metadata (G_TYPE_STRV, &typedata);
  };


  /* Register some types specific to the D-BUS GLib bindings */
  {
    static const DBusGTypeMarshalVtable vtable = {
      marshal_proxy,
      demarshal_proxy
    };
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_OBJECT_PATH_AS_STRING,
      &vtable
    };
    set_type_metadata (DBUS_TYPE_G_PROXY, &typedata);
  }

  {
    static const DBusGTypeMarshalVtable vtable = {
      marshal_object_path,
      demarshal_object_path
    };
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_OBJECT_PATH_AS_STRING,
      &vtable
    };
    set_type_metadata (DBUS_TYPE_G_OBJECT_PATH, &typedata);
  }

  {
    static const DBusGTypeMarshalVtable vtable = {
      marshal_object,
      demarshal_object
    };
    static const DBusGTypeMarshalData typedata = {
      DBUS_TYPE_OBJECT_PATH_AS_STRING,
      &vtable
    };
    set_type_metadata (G_TYPE_OBJECT, &typedata);
  }

  types_initialized = TRUE;
}

/**
 * Get the GLib type ID for a DBusGObjectPath boxed type.
 *
 * @returns GLib type
 */
GType
dbus_g_object_path_get_g_type (void)
{
  static GType type_id = 0;

  if (!type_id)
    type_id = g_boxed_type_register_static ("DBusGObjectPath",
					    (GBoxedCopyFunc) g_strdup,
					    (GBoxedFreeFunc) g_free);
  return type_id;
}

/**
 * Get the GLib type ID for a DBusGValue boxed type.
 *
 * @returns GLib type
 */
GType
dbus_g_value_get_g_type (void)
{
  static GType type_id = 0;

  if (!type_id)
    type_id = g_boxed_type_register_static ("DBusGValue",
					    dbus_g_value_copy,
					    (GBoxedFreeFunc) dbus_g_value_free);
  return type_id;
}

void
dbus_g_value_open (DBusGValue          *value,
		   DBusGValueIterator  *iter)
{
  DBusGValue *real;

  g_return_if_fail (value->type == DBUS_G_VALUE_TYPE_TOPLEVEL);

  real = (DBusGValue*) iter;
  real->type = DBUS_G_VALUE_TYPE_ITERATOR;
  real->value.recurse.toplevel = value;

  dbus_message_iter_init (value->value.toplevel.message,
			  &(real->value.recurse.iterator));
  value->value.recurse.toplevel = value;
}

gboolean
dbus_g_value_iterator_get_values (DBusGValueIterator *iter,
				  GError            **error,
				  GValue             *first_val,
				  ...)
{
  GValue *value;
  va_list args;
  DBusGValue *iterval;

  va_start (args, first_val);

  iterval = (DBusGValue *) iter;

  value = first_val;
  do
    {
      DBusGValueMarshalCtx context;

      context.gconnection = (iterval->value.recurse.toplevel)->value.toplevel.connection;
      context.proxy = (iterval->value.recurse.toplevel)->value.toplevel.proxy;

      if (!dbus_gvalue_demarshal (&context,
				  &(iterval->value.recurse.iterator),
				  value,
				  error))
	return FALSE;
    } while ((value = va_arg (args, GValue *)) != NULL);
  
  return TRUE;
}

static char *
dbus_g_value_get_signature (DBusGValue *value)
{
  return value->value.toplevel.signature;
}

static gpointer
dbus_g_value_copy (gpointer value)
{
  /* FIXME */
  return NULL;
}

void
dbus_g_value_free (DBusGValue *value)
{
  if (value->type == DBUS_G_VALUE_TYPE_TOPLEVEL)
    {
      dbus_message_unref (value->value.toplevel.message);
      g_free (value->value.toplevel.signature);
    }
}

static GType
signature_iter_to_g_type_dict (const DBusSignatureIter *subiter, gboolean is_client)
{
  DBusSignatureIter iter;
  GType key_gtype;
  GType value_gtype;

  g_assert (dbus_signature_iter_get_current_type (subiter) == DBUS_TYPE_DICT_ENTRY);

  dbus_signature_iter_recurse (subiter, &iter);

  key_gtype = dbus_gtype_from_signature_iter (&iter, is_client); 
  if (key_gtype == G_TYPE_INVALID)
    return G_TYPE_INVALID;

  dbus_signature_iter_next (&iter);
  value_gtype = dbus_gtype_from_signature_iter (&iter, is_client);
  if (value_gtype == G_TYPE_INVALID)
    return G_TYPE_INVALID;

  if (!dbus_gtype_is_valid_hash_key (key_gtype)
      || !dbus_gtype_is_valid_hash_value (value_gtype))
    /* Later we need to return DBUS_TYPE_G_VALUE */
    return G_TYPE_INVALID; 

  return dbus_g_type_get_map ("GHashTable", key_gtype, value_gtype);
}

static GType
signature_iter_to_g_type_array (DBusSignatureIter *iter, gboolean is_client)
{
  GType elt_gtype;
  DBusGTypeMarshalData *typedata;

  elt_gtype = dbus_gtype_from_signature_iter (iter, is_client);
  if (elt_gtype == G_TYPE_INVALID)
    return G_TYPE_INVALID;

  typedata = g_type_get_qdata (elt_gtype, dbus_g_type_metadata_data_quark ());
  if (typedata == NULL)
    return G_TYPE_INVALID;
  
  if (elt_gtype == G_TYPE_OBJECT)
    return DBUS_TYPE_G_OBJECT_ARRAY;
  if (elt_gtype == G_TYPE_STRING)
    return G_TYPE_STRV;
  if (dbus_g_type_is_fixed (elt_gtype))
    return dbus_g_type_get_collection ("GArray", elt_gtype);
  else if (g_type_is_a (elt_gtype, G_TYPE_OBJECT)
	   || g_type_is_a (elt_gtype, G_TYPE_BOXED))
    return dbus_g_type_get_collection ("GPtrArray", elt_gtype);

  /* Later we need to return DBUS_TYPE_G_VALUE */
  return G_TYPE_INVALID; 
}

static gboolean
signature_iter_to_g_type_struct (DBusSignatureIter *origiter, gboolean is_client)
{
  /* FIXME allow structures */
  return G_TYPE_INVALID;
#if 0
  DBusSignatureIter iter;
  int current_type;

  iter = *origiter;

  while ((current_type = dbus_signature_iter_get_current_type (&iter)) != DBUS_TYPE_INVALID)
    {
      subtype = dbus_gtype_from_signature_iter (&iter, is_client);
      if (subtype == G_TYPE_INVALID)
	return G_TYPE_INVALID;
    }
  return DBUS_TYPE_G_VALUE ();
#endif
}

GType
dbus_gtype_from_signature_iter (DBusSignatureIter *iter, gboolean is_client)
{
  int current_type;

  current_type = dbus_signature_iter_get_current_type (iter);
  /* TODO: handle type 0? */
  if (dbus_typecode_maps_to_basic (current_type))
    return basic_typecode_to_gtype (current_type);
  else if (current_type == DBUS_TYPE_OBJECT_PATH)
    return DBUS_TYPE_G_OBJECT_PATH;
  else
    {
      DBusSignatureIter subiter;

      g_assert (dbus_type_is_container (current_type));

      if (current_type == DBUS_TYPE_VARIANT)
	return g_value_get_type ();
      
      dbus_signature_iter_recurse (iter, &subiter);

      if (current_type == DBUS_TYPE_ARRAY)
	{
	  int elt_type = dbus_signature_iter_get_current_type (&subiter);
	  if (elt_type == DBUS_TYPE_DICT_ENTRY)
	    return signature_iter_to_g_type_dict (&subiter, is_client);
	  else 
	    return signature_iter_to_g_type_array (&subiter, is_client);
	}
      else if (current_type == DBUS_TYPE_STRUCT)
	return signature_iter_to_g_type_struct (&subiter, is_client);
      else if (current_type == DBUS_TYPE_DICT_ENTRY)
	return G_TYPE_INVALID;
      else
	{
	  g_assert_not_reached ();
	  return G_TYPE_INVALID;
	}
    }
}

GType
dbus_gtype_from_signature (const char *signature, gboolean is_client)
{
  DBusSignatureIter iter;

  dbus_signature_iter_init (&iter, signature);

  return dbus_gtype_from_signature_iter (&iter, is_client);
}

static char *
dbus_gvalue_to_signature (GValue *value)
{
  char *ret;

  ret = dbus_gtype_to_signature (G_VALUE_TYPE (value));
  if (ret)
    return ret;
  else
    {
      DBusGValue *val;
      
      g_assert (G_VALUE_TYPE (value) == DBUS_TYPE_G_VALUE);

      val = g_value_get_boxed (value);
      
      return dbus_g_value_get_signature (val);
    }
}

char *
dbus_gtype_to_signature (GType gtype)
{
  char *ret;
  DBusGTypeMarshalData *typedata;

  if (dbus_g_type_is_collection (gtype))
    {
      GType elt_gtype;
      char *subsig;

      elt_gtype = dbus_g_type_get_collection_specialization (gtype);
      subsig = dbus_gtype_to_signature (elt_gtype);
      ret = g_strconcat (DBUS_TYPE_ARRAY_AS_STRING, subsig, NULL);
      g_free (subsig);
    }
  else if (dbus_g_type_is_map (gtype))
    {
      GType key_gtype;
      GType val_gtype;
      char *key_subsig;
      char *val_subsig;

      key_gtype = dbus_g_type_get_map_key_specialization (gtype);
      val_gtype = dbus_g_type_get_map_value_specialization (gtype);
      key_subsig = dbus_gtype_to_signature (key_gtype);
      val_subsig = dbus_gtype_to_signature (val_gtype);
      ret = g_strconcat (DBUS_TYPE_ARRAY_AS_STRING DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING, key_subsig, val_subsig, DBUS_DICT_ENTRY_END_CHAR_AS_STRING, NULL);
      g_free (key_subsig);
      g_free (val_subsig);
    }
  else
    {
      typedata = g_type_get_qdata (gtype, dbus_g_type_metadata_data_quark ());
      if (typedata == NULL)
	return NULL;
      ret = g_strdup (typedata->sig);
    }
  
  return ret;
}

GArray *
dbus_gtypes_from_arg_signature (const char *argsig, gboolean is_client)
{
  GArray *ret;
  int current_type;
  DBusSignatureIter sigiter;

  ret = g_array_new (FALSE, FALSE, sizeof (GType));

  dbus_signature_iter_init (&sigiter, argsig);
  while ((current_type = dbus_signature_iter_get_current_type (&sigiter)) != DBUS_TYPE_INVALID)
    {
      GType curtype;

      curtype = dbus_gtype_from_signature_iter (&sigiter, is_client);
      g_array_append_val (ret, curtype);
      dbus_signature_iter_next (&sigiter);
    }
  return ret;
}


static gboolean
demarshal_basic (DBusGValueMarshalCtx      *context,
		 DBusMessageIter           *iter,
		 GValue                    *value,
		 GError                   **error)
{
  int current_type;
  
  current_type = dbus_message_iter_get_arg_type (iter);
  g_assert (dbus_type_is_basic (current_type));

  switch (current_type)
    {
    case DBUS_TYPE_BOOLEAN:
      {
	dbus_bool_t bool;
	dbus_message_iter_get_basic (iter, &bool);
	g_value_set_boolean (value, bool);
	return TRUE;
      }
    case DBUS_TYPE_BYTE:
      {
	unsigned char byte;
	dbus_message_iter_get_basic (iter, &byte);
	g_value_set_uchar (value, byte);
	return TRUE;
      }
    case DBUS_TYPE_INT32:
      {
	dbus_int32_t intval;
	dbus_message_iter_get_basic (iter, &intval);
	g_value_set_int (value, intval);
	return TRUE;
      }
    case DBUS_TYPE_UINT32:
      {
	dbus_uint32_t intval;
	dbus_message_iter_get_basic (iter, &intval);
	g_value_set_uint (value, intval);
	return TRUE;
      }
    case DBUS_TYPE_INT64:
      {
	dbus_int64_t intval;
	dbus_message_iter_get_basic (iter, &intval);
	g_value_set_int64 (value, intval);
	return TRUE;
      }
    case DBUS_TYPE_UINT64:
      {
	dbus_uint64_t intval;
	dbus_message_iter_get_basic (iter, &intval);
	g_value_set_uint64 (value, intval);
	return TRUE;
      }
    case DBUS_TYPE_DOUBLE:
      {
	double dval;
	dbus_message_iter_get_basic (iter, &dval);
	g_value_set_double (value, dval);
	return TRUE;
      }
    case DBUS_TYPE_INT16:
      {
        dbus_int16_t v;
        dbus_message_iter_get_basic (iter, &v);
        g_value_set_int (value, v);
	return TRUE;
      }
    case DBUS_TYPE_UINT16:
      {
        dbus_uint16_t v;
        dbus_message_iter_get_basic (iter, &v);
        g_value_set_uint (value, v);
	return TRUE;
      }
    case DBUS_TYPE_STRING:
      {
        const char *s;
        dbus_message_iter_get_basic (iter, &s);
	g_value_set_string (value, s);
	return TRUE;
      }
    default:
      g_assert_not_reached ();
      return FALSE;
    }
}

static gboolean
demarshal_static_variant (DBusGValueMarshalCtx    *context,
			  DBusMessageIter         *iter,
			  GValue                  *value,
			  GError                 **error)
{
  char *sig;
  int current_type;
  DBusMessageIter subiter;
  GType variant_type;

  current_type = dbus_message_iter_get_arg_type (iter);
  dbus_message_iter_recurse (iter, &subiter);
  sig = dbus_message_iter_get_signature (&subiter);

  variant_type = dbus_gtype_from_signature (sig, context->proxy != NULL);
  if (variant_type != G_TYPE_INVALID)
    {
      g_value_init (value, variant_type);

      if (!dbus_gvalue_demarshal (context, &subiter, value, error))
	{
	  dbus_free (sig);
	  return FALSE;
	}
    }
  dbus_free (sig);
  return TRUE;
}

static gboolean
demarshal_variant (DBusGValueMarshalCtx    *context,
		   DBusMessageIter         *iter,
		   GValue                  *value,
		   GError                 **error)

{
  GValue *variant_val;
  variant_val = g_new0 (GValue, 1);

  if (!demarshal_static_variant (context, iter, variant_val, error))
    return FALSE;
  
  g_value_set_boxed_take_ownership (value, variant_val);
  return TRUE;
}

static gboolean
demarshal_proxy (DBusGValueMarshalCtx    *context,
		 DBusMessageIter         *iter,
		 GValue                  *value,
		 GError                 **error)
{
  DBusGProxy *new_proxy;
  const char *objpath;
  int current_type;

  current_type = dbus_message_iter_get_arg_type (iter);
  if (current_type != DBUS_TYPE_OBJECT_PATH)
    {
      g_set_error (error,
		   DBUS_GERROR,
		   DBUS_GERROR_INVALID_ARGS,
		   _("Expected D-BUS object path, got type code \'%c\'"), (guchar) current_type);
      return FALSE;
    }

  g_assert (context->proxy != NULL);
  
  dbus_message_iter_get_basic (iter, &objpath);

  new_proxy = dbus_g_proxy_new_from_proxy (context->proxy, NULL, objpath);
  g_value_set_object_take_ownership (value, new_proxy);

  return TRUE;
}

static gboolean
demarshal_object_path (DBusGValueMarshalCtx    *context,
		       DBusMessageIter         *iter,
		       GValue                  *value,
		       GError                 **error)
{
  const char *objpath;
  int current_type;

  current_type = dbus_message_iter_get_arg_type (iter);
  if (current_type != DBUS_TYPE_OBJECT_PATH)
    {
      g_set_error (error,
		   DBUS_GERROR,
		   DBUS_GERROR_INVALID_ARGS,
		   _("Expected D-BUS object path, got type code \'%c\'"), (guchar) current_type);
      return FALSE;
    }

  dbus_message_iter_get_basic (iter, &objpath);

  g_value_set_boxed_take_ownership (value, g_strdup (objpath));

  return TRUE;
}

static gboolean
demarshal_object (DBusGValueMarshalCtx    *context,
		  DBusMessageIter         *iter,
		  GValue                  *value,
		  GError                 **error)
{
  const char *objpath;
  int current_type;
  GObject *obj;

  current_type = dbus_message_iter_get_arg_type (iter);
  if (current_type != DBUS_TYPE_OBJECT_PATH)
    {
      g_set_error (error,
		   DBUS_GERROR,
		   DBUS_GERROR_INVALID_ARGS,
		   _("Expected D-BUS object path, got type code \'%c\'"), (guchar) current_type);
      return FALSE;
    }
  g_assert (context->proxy == NULL);

  dbus_message_iter_get_basic (iter, &objpath);

  obj = dbus_g_connection_lookup_g_object (context->gconnection, objpath);
  if (obj == NULL)
    {
      g_set_error (error,
		   DBUS_GERROR,
		   DBUS_GERROR_INVALID_ARGS,
		   _("Unregistered object at path '%s'"),
		   objpath);
      return FALSE;
    }
  g_value_set_object (value, obj);

  return TRUE;
}

static gboolean
demarshal_strv (DBusGValueMarshalCtx    *context,
		DBusMessageIter         *iter,
		GValue                  *value,
		GError                 **error)
{
  DBusMessageIter subiter;
  int current_type;
  char **ret;
  int len;
  int i;

  current_type = dbus_message_iter_get_arg_type (iter);
  if (current_type != DBUS_TYPE_ARRAY)
    {
      g_set_error (error,
		   DBUS_GERROR,
		   DBUS_GERROR_INVALID_ARGS,
		   _("Expected D-BUS array, got type code \'%c\'"), (guchar) current_type);
      return FALSE;
    }

  dbus_message_iter_recurse (iter, &subiter);

  current_type = dbus_message_iter_get_arg_type (&subiter);
  if (current_type != DBUS_TYPE_INVALID
      && current_type != DBUS_TYPE_STRING)
    {
      g_set_error (error,
		   DBUS_GERROR,
		   DBUS_GERROR_INVALID_ARGS,
		   _("Expected D-BUS string, got type code \'%c\'"), (guchar) current_type);
      return FALSE;
    }

  len = dbus_message_iter_get_array_len (&subiter);
  g_assert (len >= 0);
  ret = g_malloc (sizeof (char *) * (len + 1));
  
  i = 0;
  while ((current_type = dbus_message_iter_get_arg_type (&subiter)) != DBUS_TYPE_INVALID)
    {
      g_assert (i < len);
      g_assert (current_type == DBUS_TYPE_STRING);
      
      dbus_message_iter_get_basic (&subiter, &(ret[i]));
      ret[i] = g_strdup (ret[i]);

      dbus_message_iter_next (&subiter);
      i++;
    }
  ret[i] = NULL; 
  g_value_set_boxed_take_ownership (value, ret);
  
  return TRUE;
}

static gboolean
demarshal_map (DBusGValueMarshalCtx    *context,
	       DBusMessageIter         *iter,
	       GValue                  *value,
	       GError                 **error)
{
  GType gtype;
  DBusMessageIter subiter;
  int current_type;
  gpointer ret;
  GType key_gtype;
  GType value_gtype;
  DBusGTypeSpecializedAppendContext appendctx;

  current_type = dbus_message_iter_get_arg_type (iter);
  if (current_type != DBUS_TYPE_ARRAY)
    {
      g_set_error (error,
		   DBUS_GERROR,
		   DBUS_GERROR_INVALID_ARGS,
		   _("Expected D-BUS array, got type code \'%c\'"), (guchar) current_type);
      return FALSE;
    }

  gtype = G_VALUE_TYPE (value);

  dbus_message_iter_recurse (iter, &subiter);

  current_type = dbus_message_iter_get_arg_type (&subiter);
  if (current_type != DBUS_TYPE_INVALID
      && current_type != DBUS_TYPE_DICT_ENTRY)
    {
      g_set_error (error,
		   DBUS_GERROR,
		   DBUS_GERROR_INVALID_ARGS,
		   _("Expected D-BUS dict entry, got type code \'%c\'"), (guchar) current_type);
      return FALSE;
    }

  key_gtype = dbus_g_type_get_map_key_specialization (gtype);
  value_gtype = dbus_g_type_get_map_value_specialization (gtype);

  ret = dbus_g_type_specialized_construct (gtype);
  g_value_set_boxed_take_ownership (value, ret);

  dbus_g_type_specialized_init_append (value, &appendctx);

  while ((current_type = dbus_message_iter_get_arg_type (&subiter)) != DBUS_TYPE_INVALID)
    {
      DBusMessageIter entry_iter;
      GValue key_value = {0,};
      GValue value_value = {0,};

      current_type = dbus_message_iter_get_arg_type (&subiter);
      g_assert (current_type == DBUS_TYPE_DICT_ENTRY);

      dbus_message_iter_recurse (&subiter, &entry_iter);

      g_value_init (&key_value, key_gtype);
      if (!dbus_gvalue_demarshal (context,
				  &entry_iter,
				  &key_value,
				  error))
	return FALSE;

      dbus_message_iter_next (&entry_iter);

      g_value_init (&value_value, value_gtype);
      if (!dbus_gvalue_demarshal (context,
				  &entry_iter,
				  &value_value,
				  error))
	return FALSE;

      dbus_g_type_specialized_map_append (&appendctx, &key_value, &value_value);
      /* Ownership of values passes to map, don't unset */

      dbus_message_iter_next (&subiter);
    }
  
  return TRUE;
}

static DBusGValueDemarshalFunc
get_type_demarshaller (GType type)
{
  DBusGTypeMarshalData *typedata;

  typedata = g_type_get_qdata (type, dbus_g_type_metadata_data_quark ());
  if (typedata == NULL)
    {
      if (g_type_is_a (type, DBUS_TYPE_G_VALUE))
	return demarshal_recurse;
      if (dbus_g_type_is_collection (type))
	return demarshal_collection;
      if (dbus_g_type_is_map (type))
	return demarshal_map;

      g_warning ("No demarshaller registered for type \"%s\"", g_type_name (type));
      return NULL;
    }
  g_assert (typedata->vtable);
  return typedata->vtable->demarshaller;
}

static gboolean
demarshal_collection (DBusGValueMarshalCtx    *context,
		      DBusMessageIter         *iter,
		      GValue                  *value,
		      GError                 **error)
{
  GType coltype;
  GType subtype;
  
  coltype = G_VALUE_TYPE (value);
  subtype = dbus_g_type_get_collection_specialization (coltype);

  if (dbus_g_type_is_fixed (subtype))
    return demarshal_collection_array (context, iter, value, error);
  else
    return demarshal_collection_ptrarray (context, iter, value, error);
}

static gboolean
demarshal_collection_ptrarray (DBusGValueMarshalCtx    *context,
			       DBusMessageIter         *iter,
			       GValue                  *value,
			       GError                 **error)
{
  GType coltype;
  GType subtype;
  gpointer instance;
  DBusGTypeSpecializedAppendContext ctx;
  DBusGValueDemarshalFunc demarshaller;
  DBusMessageIter subiter;
  int current_type;

  current_type = dbus_message_iter_get_arg_type (iter);

  if (current_type != DBUS_TYPE_ARRAY)
    {
      g_set_error (error,
		   DBUS_GERROR,
		   DBUS_GERROR_INVALID_ARGS,
		   _("Expected D-BUS array, got type code \'%c\'"), (guchar) current_type);
      return FALSE;
    }

  dbus_message_iter_recurse (iter, &subiter);
  
  coltype = G_VALUE_TYPE (value);
  subtype = dbus_g_type_get_collection_specialization (coltype);

  demarshaller = get_type_demarshaller (subtype);

  if (!demarshaller)
    {
      g_set_error (error,
		   DBUS_GERROR,
		   DBUS_GERROR_INVALID_ARGS,
		   _("No demarshaller registered for type \"%s\" of collection \"%s\""),
		   g_type_name (coltype),
		   g_type_name (subtype));
      return FALSE;
    }

  instance = dbus_g_type_specialized_construct (coltype);
  g_value_set_boxed_take_ownership (value, instance);

  dbus_g_type_specialized_init_append (value, &ctx);

  while ((current_type = dbus_message_iter_get_arg_type (&subiter)) != DBUS_TYPE_INVALID)
    {
      GValue eltval = {0, };

      g_value_init (&eltval, subtype);

      if (!demarshaller (context, &subiter, &eltval, error))
	{
	  dbus_g_type_specialized_collection_end_append (&ctx);
	  g_value_unset (value);
	  return FALSE;
	}
      dbus_g_type_specialized_collection_append (&ctx, &eltval);
      
      dbus_message_iter_next (&subiter);
    }
  dbus_g_type_specialized_collection_end_append (&ctx);
  
  return TRUE;
}

static gboolean
demarshal_collection_array (DBusGValueMarshalCtx    *context,
			    DBusMessageIter         *iter,
			    GValue                  *value,
			    GError                 **error)
{
  DBusMessageIter subiter;
  GArray *ret;
  GType elt_gtype;
  int elt_size;
  void *msgarray;
  int msgarray_len;

  dbus_message_iter_recurse (iter, &subiter);

  elt_gtype = dbus_g_type_get_collection_specialization (G_VALUE_TYPE (value));
  g_assert (elt_gtype != G_TYPE_INVALID);
  g_assert (dbus_g_type_is_fixed (elt_gtype));

  elt_size = dbus_g_type_fixed_get_size (elt_gtype);
  
  ret = g_array_new (FALSE, TRUE, elt_size);

  msgarray = NULL;
  dbus_message_iter_get_fixed_array (&subiter,
				     &msgarray,
				     &msgarray_len);
  g_assert (msgarray != NULL);
  g_assert (msgarray_len >= 0);
  g_array_append_vals (ret, msgarray, (guint) msgarray_len);

  g_value_set_boxed_take_ownership (value, ret);
  
  return TRUE;
}

static gboolean
demarshal_recurse (DBusGValueMarshalCtx    *context,
		   DBusMessageIter         *iter,
		   GValue                  *value,
		   GError                 **error)
{
  return FALSE;
}

gboolean
dbus_gvalue_demarshal (DBusGValueMarshalCtx    *context,
		       DBusMessageIter         *iter,
		       GValue                  *value,
		       GError                 **error)
{
  GType gtype;
  DBusGValueDemarshalFunc demarshaller;

  gtype = G_VALUE_TYPE (value);

  demarshaller = get_type_demarshaller (gtype);

  if (demarshaller == NULL)
    {
      g_set_error (error,
		   DBUS_GERROR,
		   DBUS_GERROR_INVALID_ARGS,
		   _("No demarshaller registered for type \"%s\""),
		   g_type_name (gtype));
      return FALSE;
    }
  
  return demarshaller (context, iter, value, error);
}

gboolean
dbus_gvalue_demarshal_variant (DBusGValueMarshalCtx    *context,
			       DBusMessageIter         *iter,
			       GValue                  *value,
			       GError                 **error)
{
  return demarshal_static_variant (context, iter, value, error);
}

GValueArray *
dbus_gvalue_demarshal_message  (DBusGValueMarshalCtx    *context,
				DBusMessage             *message,
				guint                    n_types,
				const GType             *types,
				GError                 **error)
{
  GValueArray *ret;
  DBusMessageIter iter;
  int current_type;
  guint index;
  
  ret = g_value_array_new (6);  /* 6 is a typical maximum for arguments */

  dbus_message_iter_init (message, &iter);
  index = 0;
  while ((current_type = dbus_message_iter_get_arg_type (&iter)) != DBUS_TYPE_INVALID)
    {
      GValue *value;
      GType gtype;

      if (index >= n_types)
	{
	  g_set_error (error, DBUS_GERROR,
		       DBUS_GERROR_INVALID_ARGS,
		       _("Too many arguments in message"));
	  goto lose;
	}
      
      g_value_array_append (ret, NULL);
      value = g_value_array_get_nth (ret, index);

      gtype = types[index]; 
      g_value_init (value, gtype);

      if (!dbus_gvalue_demarshal (context, &iter, value, error))
	goto lose;
      dbus_message_iter_next (&iter);
      index++;
    }
  if (index < n_types)
    {
      g_set_error (error, DBUS_GERROR,
		   DBUS_GERROR_INVALID_ARGS,
		   _("Too few arguments in message"));
      goto lose;
    }

  return ret;
 lose:
  g_value_array_free (ret);
  return NULL;
}

static gboolean
marshal_basic (DBusMessageIter *iter, const GValue *value)
{
  GType value_type;

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
      return TRUE;
    case G_TYPE_UCHAR:
      {
        unsigned char b = g_value_get_uchar (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_BYTE,
                                             &b))
          goto nomem;
      }
      return TRUE;
    case G_TYPE_BOOLEAN:
      {
        dbus_bool_t b = g_value_get_boolean (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_BOOLEAN,
                                             &b))
          goto nomem;
      }
      return TRUE;
    case G_TYPE_INT:
      {
        dbus_int32_t v = g_value_get_int (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_INT32,
                                             &v))
          goto nomem;
      }
      return TRUE;
    case G_TYPE_UINT:
      {
        dbus_uint32_t v = g_value_get_uint (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_UINT32,
                                             &v))
          goto nomem;
      }
      return TRUE;
    case G_TYPE_LONG:
      {
        dbus_int32_t v = g_value_get_long (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_INT32,
                                             &v))
          goto nomem;
      }
      return TRUE;
    case G_TYPE_ULONG:
      {
        dbus_uint32_t v = g_value_get_ulong (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_UINT32,
                                             &v))
          goto nomem;
      }
      return TRUE;
    case G_TYPE_INT64:
      {
        gint64 v = g_value_get_int64 (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_INT64,
                                             &v))
          goto nomem;
      }
      return TRUE;
    case G_TYPE_UINT64:
      {
        guint64 v = g_value_get_uint64 (value);
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_UINT64,
                                             &v))
          goto nomem;
      }
      return TRUE;
    case G_TYPE_FLOAT:
      {
        double v = g_value_get_float (value);
        
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_DOUBLE,
                                             &v))
          goto nomem;
      }
      return TRUE;
    case G_TYPE_DOUBLE:
      {
        double v = g_value_get_double (value);
        
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_DOUBLE,
                                             &v))
          goto nomem;
      }
      return TRUE;
    case G_TYPE_STRING:
      /* FIXME, the GValue string may not be valid UTF-8 */
      {
        const char *v = g_value_get_string (value);
	if (!v)
	  v = "";
        if (!dbus_message_iter_append_basic (iter,
                                             DBUS_TYPE_STRING,
                                             &v))
          goto nomem;
      }
      return TRUE;
      
    default:
      {
	g_assert_not_reached ();
	return FALSE;
      }
    }

 nomem:
  g_error ("no memory");
  return FALSE;
}

static gboolean
marshal_strv (DBusMessageIter   *iter,
	      const GValue       *value)
{
  DBusMessageIter subiter;
  char **array;
  char **elt;
  gboolean ret = FALSE;

  g_assert (G_VALUE_TYPE (value) == g_strv_get_type ());

  array = g_value_get_boxed (value);

  if (!dbus_message_iter_open_container (iter,
					 DBUS_TYPE_ARRAY,
					 "s",
					 &subiter))
    goto out;

  for (elt = array; *elt; elt++)
    {
      if (!dbus_message_iter_append_basic (&subiter,
					   DBUS_TYPE_STRING,
					   elt))
	goto out;
    }

  if (!dbus_message_iter_close_container (iter, &subiter))
    goto out;
  ret = TRUE;
 out:
  return ret;
}

static gboolean
marshal_proxy (DBusMessageIter         *iter,
	       const GValue            *value)
{
  const char *path;
  DBusGProxy *proxy;

  g_assert (G_VALUE_TYPE (value) == dbus_g_proxy_get_type ());

  proxy = g_value_get_object (value);
  path = dbus_g_proxy_get_path (proxy);
  
  if (!dbus_message_iter_append_basic (iter,
				       DBUS_TYPE_OBJECT_PATH,
				       &path))
    return FALSE;
  return TRUE;
}

static gboolean
marshal_object_path (DBusMessageIter         *iter,
		     const GValue            *value)
{
  const char *path;

  g_assert (G_VALUE_TYPE (value) == DBUS_TYPE_G_OBJECT_PATH);

  path = (const char*) g_value_get_boxed (value);
  
  if (!dbus_message_iter_append_basic (iter,
				       DBUS_TYPE_OBJECT_PATH,
				       &path))
    return FALSE;
  return TRUE;
}

static gboolean
marshal_object (DBusMessageIter         *iter,
		const GValue            *value)
{
  const char *path;
  GObject *obj;

  obj = g_value_get_object (value);
  path = _dbus_gobject_get_path (obj);

  if (path == NULL)
    /* FIXME should throw error */
    return FALSE;
  
  if (!dbus_message_iter_append_basic (iter,
				       DBUS_TYPE_OBJECT_PATH,
				       &path))
    return FALSE;
  return TRUE;
}

struct DBusGLibHashMarshalData
{
  const char *entry_sig;
  DBusMessageIter *iter;
  gboolean err;
};

static void
marshal_map_entry (const GValue *key,
		   const GValue *value,
		   gpointer data)
{
  struct DBusGLibHashMarshalData *hashdata = data;
  DBusMessageIter subiter;

  if (hashdata->err)
    return;

  if (!dbus_message_iter_open_container (hashdata->iter,
					 DBUS_TYPE_DICT_ENTRY,
					 NULL,
					 &subiter))
    goto lose;

  if (!dbus_gvalue_marshal (&subiter, key))
    goto lose;

  if (!dbus_gvalue_marshal (&subiter, value))
    goto lose;

  if (!dbus_message_iter_close_container (hashdata->iter, &subiter))
    goto lose;
  
  return;
 lose:
  hashdata->err = TRUE;
}

static gboolean
marshal_map (DBusMessageIter   *iter,
	     const GValue      *value)
{
  GType gtype;
  DBusMessageIter arr_iter;
  gboolean ret;
  struct DBusGLibHashMarshalData hashdata;
  GType key_type;
  GType value_type;
  const char *key_sig;
  const char *value_sig;
  char *entry_sig;
  char *array_sig;

  gtype = G_VALUE_TYPE (value);

  ret = FALSE;

  key_type = dbus_g_type_get_map_key_specialization (gtype);
  g_assert (dbus_gtype_is_valid_hash_key (key_type));
  value_type = dbus_g_type_get_map_value_specialization (gtype);
  g_assert (dbus_gtype_is_valid_hash_value (value_type));

  key_sig = dbus_gtype_to_signature (key_type);
  value_sig = dbus_gtype_to_signature (value_type);
  entry_sig = g_strdup_printf ("%s%s", key_sig, value_sig);
  array_sig = g_strdup_printf ("%c%s%c",
			       DBUS_DICT_ENTRY_BEGIN_CHAR,
			       entry_sig,
			       DBUS_DICT_ENTRY_END_CHAR);
  if (!dbus_message_iter_open_container (iter,
					 DBUS_TYPE_ARRAY,
					 array_sig,
					 &arr_iter))
    goto lose;

  hashdata.iter = &arr_iter;
  hashdata.err = FALSE;
  hashdata.entry_sig = entry_sig;

  dbus_g_type_map_value_iterate (value,
				 marshal_map_entry,
				 &hashdata);

  if (!dbus_message_iter_close_container (iter, &arr_iter))
    goto lose;

 out:
  g_free (entry_sig);
  g_free (array_sig);
  return !hashdata.err;
 lose:
  hashdata.err = TRUE;
  goto out;
}

static gboolean
marshal_variant (DBusMessageIter          *iter,
		 const GValue             *value)
{
  GType value_gtype;
  DBusMessageIter subiter;
  char *variant_sig;
  GValue *real_value;
  gboolean ret = FALSE;

  real_value = g_value_get_boxed (value);
  value_gtype = G_VALUE_TYPE (real_value);

  variant_sig = dbus_gvalue_to_signature (real_value);
  if (variant_sig == NULL)
    {
      g_warning ("Unsupported value type \"%s\"",
		 g_type_name (value_gtype));
      return FALSE;
    }

  if (!dbus_message_iter_open_container (iter,
					 DBUS_TYPE_VARIANT,
					 variant_sig,
					 &subiter))
    goto out;

  if (!marshal_basic (&subiter, real_value))
    goto out;

  if (!dbus_message_iter_close_container (iter, &subiter))
    goto out;

  ret = TRUE;
 out:
  g_free (variant_sig);
  return ret;
}

static DBusGValueMarshalFunc
get_type_marshaller (GType type)
{
  DBusGTypeMarshalData *typedata;

  typedata = g_type_get_qdata (type, dbus_g_type_metadata_data_quark ());
  if (typedata == NULL)
    {
      if (g_type_is_a (type, DBUS_TYPE_G_VALUE))
	return marshal_recurse;
      if (dbus_g_type_is_collection (type))
	return marshal_collection;
      if (dbus_g_type_is_map (type))
	return marshal_map;

      g_warning ("No marshaller registered for type \"%s\"", g_type_name (type));
      return NULL;
    }
  g_assert (typedata->vtable);
  return typedata->vtable->marshaller;
}

typedef struct
{
  DBusMessageIter *iter;
  DBusGValueMarshalFunc marshaller;
  gboolean err;
} DBusGValueCollectionMarshalData;

static void
collection_marshal_iterator (const GValue *eltval,
			     gpointer      user_data)
{
  DBusGValueCollectionMarshalData *data = user_data;

  if (data->err)
    return;

  if (!data->marshaller (data->iter, eltval))
    data->err = TRUE;
}

static gboolean
marshal_collection (DBusMessageIter         *iter,
		    const GValue            *value)
{
  GType coltype;
  GType subtype;
  
  coltype = G_VALUE_TYPE (value);
  subtype = dbus_g_type_get_collection_specialization (coltype);

  if (dbus_g_type_is_fixed (subtype))
    return marshal_collection_array (iter, value);
  else
    return marshal_collection_ptrarray (iter, value);
}

static gboolean
marshal_collection_ptrarray (DBusMessageIter         *iter,
			     const GValue            *value)
{
  GType coltype;
  GType elt_gtype;
  DBusGValueCollectionMarshalData data;
  DBusMessageIter subiter;
  char *elt_sig;
  
  coltype = G_VALUE_TYPE (value);
  elt_gtype = dbus_g_type_get_collection_specialization (coltype);
  data.marshaller = get_type_marshaller (elt_gtype);
  if (!data.marshaller)
    return FALSE;

  /* FIXME - this means we can't send an array of DBusGValue right now... */
  elt_sig = dbus_gtype_to_signature (elt_gtype);

  if (!dbus_message_iter_open_container (iter,
					 DBUS_TYPE_ARRAY,
					 elt_sig,
					 &subiter))
    goto oom;
  g_free (elt_sig);

  data.iter = &subiter;
  data.err = FALSE;

  dbus_g_type_collection_value_iterate (value,
					collection_marshal_iterator,
					&data);

  if (!dbus_message_iter_close_container (iter, &subiter))
    goto oom;
  
  return !data.err;
 oom:
  g_error ("out of memory");
  return FALSE;
}


static gboolean
marshal_collection_array (DBusMessageIter   *iter,
			  const GValue      *value)
{
  GType elt_gtype;
  DBusMessageIter subiter;
  GArray *array;
  guint elt_size;
  char *subsignature_str;

  elt_gtype = dbus_g_type_get_collection_specialization (G_VALUE_TYPE (value));
  g_assert (dbus_g_type_is_fixed (elt_gtype));
  subsignature_str = dbus_gtype_to_signature (elt_gtype);
  g_assert (subsignature_str != NULL);
  
  elt_size = dbus_g_type_fixed_get_size (elt_gtype);

  array = g_value_get_boxed (value);

  if (!dbus_message_iter_open_container (iter,
					 DBUS_TYPE_ARRAY,
					 subsignature_str,
					 &subiter))
    goto oom;

  /* TODO - This assumes that basic values are the same size
   * is this always true?  If it is we can probably avoid
   * a lot of the overhead in _marshal_basic_instance...
   */
  if (!dbus_message_iter_append_fixed_array (&subiter,
					     subsignature_str[0],
					     &(array->data),
					     array->len))
    goto oom;

  if (!dbus_message_iter_close_container (iter, &subiter))
    goto oom;
  g_free (subsignature_str);
  return TRUE;
 oom:
  g_error ("out of memory");
  return FALSE;
}

static gboolean
marshal_recurse (DBusMessageIter         *iter,
		 const GValue            *value)
{
  return FALSE;
}

gboolean
dbus_gvalue_marshal (DBusMessageIter         *iter,
		     const GValue       *value)
{
  GType gtype;
  DBusGValueMarshalFunc marshaller;

  gtype = G_VALUE_TYPE (value);

  marshaller = get_type_marshaller (gtype);
  if (marshaller == NULL)
    return FALSE;
  return marshaller (iter, value);
}

#ifdef DBUS_BUILD_TESTS

static void
assert_type_maps_to (GType gtype, const char *expected_sig)
{
  char *sig;
  sig = dbus_gtype_to_signature (gtype);
  g_assert (sig != NULL);
  g_assert (!strcmp (expected_sig, sig));
  g_free (sig);
}

static void
assert_signature_maps_to (const char *sig, GType expected_gtype)
{
  g_assert (dbus_gtype_from_signature (sig, TRUE) == expected_gtype);
}

static void
assert_bidirectional_mapping (GType gtype, const char *expected_sig)
{
  assert_type_maps_to (gtype, expected_sig);
  assert_signature_maps_to (expected_sig, gtype);
}

/**
 * @ingroup DBusGLibInternals
 * Unit test for general glib stuff
 * @returns #TRUE on success.
 */
gboolean
_dbus_gvalue_test (const char *test_data_dir)
{
  dbus_g_value_types_init ();
  
  assert_bidirectional_mapping (G_TYPE_STRING, DBUS_TYPE_STRING_AS_STRING);
  assert_bidirectional_mapping (G_TYPE_UCHAR, DBUS_TYPE_BYTE_AS_STRING);
  assert_bidirectional_mapping (G_TYPE_UINT, DBUS_TYPE_UINT32_AS_STRING);

  assert_bidirectional_mapping (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
				DBUS_TYPE_ARRAY_AS_STRING DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING DBUS_DICT_ENTRY_END_CHAR_AS_STRING);
  assert_bidirectional_mapping (dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH),
				DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_OBJECT_PATH_AS_STRING);
  assert_bidirectional_mapping (dbus_g_type_get_collection ("GArray", G_TYPE_INT),
				DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_INT32_AS_STRING);

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
