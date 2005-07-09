/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gtype-specialized.c: Non-DBus-specific functions for specialized GTypes
 *
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

#include "dbus-gtype-specialized.h"
#include <glib.h>
#include <string.h>
#include <gobject/gvaluecollector.h>

typedef enum {
  DBUS_G_SPECTYPE_COLLECTION,
  DBUS_G_SPECTYPE_MAP
} DBusGTypeSpecializedType;

typedef struct {
  DBusGTypeSpecializedType type;
  const DBusGTypeSpecializedVtable     *vtable;
} DBusGTypeSpecializedContainer;

typedef struct {
  GType types[6];
  const DBusGTypeSpecializedContainer     *klass;
} DBusGTypeSpecializedData;

static GHashTable /* char * -> data* */ *specialized_containers;

static GQuark
specialized_type_data_quark ()
{
  static GQuark quark;
  if (!quark)
    quark = g_quark_from_static_string ("DBusGTypeSpecializedData");
  
  return quark;
}

void
dbus_g_type_specialized_init (void)
{
  specialized_containers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static gboolean
specialized_types_is_initialized (void)
{
  return specialized_containers != NULL;
}

static DBusGTypeSpecializedData *
lookup_specialization_data (GType type)
{
  return g_type_get_qdata (type, specialized_type_data_quark ());
}

/* Copied from gboxed.c */
static void
proxy_value_init (GValue *value)
{
  value->data[0].v_pointer = NULL;
}

/* Adapted from gboxed.c */
static void
proxy_value_free (GValue *value)
{
  if (value->data[0].v_pointer && !(value->data[1].v_uint & G_VALUE_NOCOPY_CONTENTS))
    {
      DBusGTypeSpecializedData *data;
      GType type;

      type = G_VALUE_TYPE (value);
      data = lookup_specialization_data (type);
      g_assert (data != NULL);

      data->klass->vtable->free_func (type, value->data[0].v_pointer);
    }
}

/* Adapted from gboxed.c */
static void
proxy_value_copy (const GValue *src_value,
		  GValue       *dest_value)
{
  if (src_value->data[0].v_pointer)
    {
      DBusGTypeSpecializedData *data;
      GType type;
      type = G_VALUE_TYPE (src_value);
      data = lookup_specialization_data (type);
      g_assert (data != NULL);
      dest_value->data[0].v_pointer = data->klass->vtable->copy_func (type, src_value->data[0].v_pointer);
    }
  else
    dest_value->data[0].v_pointer = src_value->data[0].v_pointer;
}

/* Copied from gboxed.c */
static gpointer
proxy_value_peek_pointer (const GValue *value)
{
  return value->data[0].v_pointer;
}

/* Adapted from gboxed.c */
static gchar*
proxy_collect_value (GValue      *value,
		     guint        n_collect_values,
		     GTypeCValue *collect_values,
		     guint        collect_flags)
{
  DBusGTypeSpecializedData *data;
  GType type;

  type = G_VALUE_TYPE (value);
  data = lookup_specialization_data (type);

  if (!collect_values[0].v_pointer)
    value->data[0].v_pointer = NULL;
  else
    {
      if (collect_flags & G_VALUE_NOCOPY_CONTENTS)
	{
	  value->data[0].v_pointer = collect_values[0].v_pointer;
	  value->data[1].v_uint = G_VALUE_NOCOPY_CONTENTS;
	}
      else
	value->data[0].v_pointer = data->klass->vtable->copy_func (type, collect_values[0].v_pointer);
    }

  return NULL;
}

/* Adapted from gboxed.c */
static gchar*
proxy_lcopy_value (const GValue *value,
		   guint         n_collect_values,
		   GTypeCValue  *collect_values,
		   guint         collect_flags)
{
  gpointer *boxed_p = collect_values[0].v_pointer;

  if (!boxed_p)
    return g_strdup_printf ("value location for `%s' passed as NULL", G_VALUE_TYPE_NAME (value));

  if (!value->data[0].v_pointer)
    *boxed_p = NULL;
  else if (collect_flags & G_VALUE_NOCOPY_CONTENTS)
    *boxed_p = value->data[0].v_pointer;
  else
    {
      DBusGTypeSpecializedData *data;
      GType type;

      type = G_VALUE_TYPE (value);
      data = lookup_specialization_data (type);

      *boxed_p = data->klass->vtable->copy_func (type, value->data[0].v_pointer);
    }

  return NULL;
}

static char *
build_specialization_name (const char *prefix, GType first_type, GType second_type)
{
  GString *fullname;

  fullname = g_string_new (prefix);
  g_string_append_c (fullname, '+');
  g_string_append (fullname, g_type_name (first_type));
  if (second_type != G_TYPE_INVALID)
    {
      g_string_append_c (fullname, '+');
      g_string_append (fullname, g_type_name (second_type));
    }
  return g_string_free (fullname, FALSE);
}

static void
register_container (const char                         *name,
		    DBusGTypeSpecializedType            type,
		    const DBusGTypeSpecializedVtable   *vtable)
{
  DBusGTypeSpecializedContainer *klass;
  
  klass = g_new0 (DBusGTypeSpecializedContainer, 1);
  klass->type = type;
  klass->vtable = vtable;

  g_hash_table_insert (specialized_containers, g_strdup (name), klass);
}

void
dbus_g_type_register_collection (const char                                   *name,
				 const DBusGTypeSpecializedCollectionVtable   *vtable,
				 guint                                         flags)
{
  g_return_if_fail (specialized_types_is_initialized ());
  register_container (name, DBUS_G_SPECTYPE_COLLECTION, (const DBusGTypeSpecializedVtable*) vtable);
}

void
dbus_g_type_register_map (const char                            *name,
			  const DBusGTypeSpecializedMapVtable   *vtable,
			  guint                                  flags)
{
  g_return_if_fail (specialized_types_is_initialized ());
  register_container (name, DBUS_G_SPECTYPE_MAP, (const DBusGTypeSpecializedVtable*) vtable);
}

static GType
register_specialized_instance (const DBusGTypeSpecializedContainer   *klass,
			       char                                  *name,
			       GType                                  first_type,
			       GType                                  second_type)
{
  GType ret;
  
  static const GTypeValueTable vtable =
    {
      proxy_value_init,
      proxy_value_free,
      proxy_value_copy,
      proxy_value_peek_pointer,
      "p",
      proxy_collect_value,
      "p",
      proxy_lcopy_value,
    };
  static const GTypeInfo derived_info =
    {
      0,		/* class_size */
      NULL,		/* base_init */
      NULL,		/* base_finalize */
      NULL,		/* class_init */
      NULL,		/* class_finalize */
      NULL,		/* class_data */
      0,		/* instance_size */
      0,		/* n_preallocs */
      NULL,		/* instance_init */
      &vtable,		/* value_table */
    };

  ret = g_type_register_static (G_TYPE_BOXED, name, &derived_info, 0);
  /* install proxy functions upon successfull registration */
  if (ret != G_TYPE_INVALID)
    {
      DBusGTypeSpecializedData *data;
      data = g_new0 (DBusGTypeSpecializedData, 1);
      data->types[0] = first_type;
      data->types[1] = second_type;
      data->klass = klass;
      g_type_set_qdata (ret, specialized_type_data_quark (), data);
    }

  return ret;
}

static GType
lookup_or_register_specialized (const char  *container,
				GType       first_type,
				GType       second_type)
{
  GType ret;
  char *name;
  const DBusGTypeSpecializedContainer *klass;

  g_return_val_if_fail (specialized_types_is_initialized (), G_TYPE_INVALID);

  klass = g_hash_table_lookup (specialized_containers, container);
  g_return_val_if_fail (klass != NULL, G_TYPE_INVALID);

  name = build_specialization_name (container, first_type, second_type);
  ret = g_type_from_name (name);
  if (ret == G_TYPE_INVALID)
    {
      /* Take ownership of name */
      ret = register_specialized_instance (klass, name,
					   first_type,
					   second_type);
    }
  else
    g_free (name);
  return ret;
}

GType
dbus_g_type_get_collection (const char *container,
			    GType       specialization)
{
  return lookup_or_register_specialized (container, specialization, G_TYPE_INVALID);
}

GType
dbus_g_type_get_map (const char   *container,
		     GType         key_specialization,
		     GType         value_specialization)
{
  return lookup_or_register_specialized (container, key_specialization, value_specialization);
}

gboolean
dbus_g_type_is_collection (GType gtype)
{
  DBusGTypeSpecializedData *data;
  data = lookup_specialization_data (gtype);
  if (data == NULL)
    return FALSE;
  return data->klass->type == DBUS_G_SPECTYPE_COLLECTION;
}

gboolean
dbus_g_type_is_map (GType gtype)
{
  DBusGTypeSpecializedData *data;
  data = lookup_specialization_data (gtype);
  if (data == NULL)
    return FALSE;
  return data->klass->type == DBUS_G_SPECTYPE_MAP;
}

static GType
get_specialization_index (GType gtype, guint i)
{
  DBusGTypeSpecializedData *data;

  data = lookup_specialization_data (gtype);
  return data->types[i];
}

GType
dbus_g_type_get_collection_specialization (GType gtype)
{
  g_return_val_if_fail (dbus_g_type_is_collection (gtype), G_TYPE_INVALID);
  return get_specialization_index (gtype, 0);
}

GType
dbus_g_type_get_map_key_specialization (GType gtype)
{
  g_return_val_if_fail (dbus_g_type_is_map (gtype), G_TYPE_INVALID);
  return get_specialization_index (gtype, 0);
}

GType
dbus_g_type_get_map_value_specialization (GType gtype)
{
  g_return_val_if_fail (dbus_g_type_is_map (gtype), G_TYPE_INVALID);
  return get_specialization_index (gtype, 1);
}

gpointer
dbus_g_type_specialized_construct (GType type)
{
  DBusGTypeSpecializedData *data;
  g_return_val_if_fail (specialized_types_is_initialized (), FALSE);

  data = lookup_specialization_data (type);
  g_return_val_if_fail (data != NULL, FALSE);

  return data->klass->vtable->constructor (type);
}

gboolean
dbus_g_type_collection_get_fixed (GValue   *value,
				  gpointer *data_ret,
				  guint    *len_ret)
{
  DBusGTypeSpecializedData *data;
  GType gtype;

  g_return_val_if_fail (specialized_types_is_initialized (), FALSE);
  g_return_val_if_fail (G_VALUE_HOLDS_BOXED (value), FALSE);

  gtype = G_VALUE_TYPE (value);
  data = lookup_specialization_data (gtype);
  g_return_val_if_fail (data != NULL, FALSE);

  return ((DBusGTypeSpecializedCollectionVtable *) (data->klass->vtable))->fixed_accessor (gtype,
											   g_value_get_boxed (value),
											   data_ret, len_ret);
}

void
dbus_g_type_collection_value_iterate (const GValue                           *value,
				      DBusGTypeSpecializedCollectionIterator  iterator,
				      gpointer                                user_data)
{
  DBusGTypeSpecializedData *data;
  GType gtype;

  g_return_if_fail (specialized_types_is_initialized ());
  g_return_if_fail (G_VALUE_HOLDS_BOXED (value));

  gtype = G_VALUE_TYPE (value);
  data = lookup_specialization_data (gtype);
  g_return_if_fail (data != NULL);

  ((DBusGTypeSpecializedCollectionVtable *) data->klass->vtable)->iterator (gtype,
									    g_value_get_boxed (value),
									    iterator, user_data);
}

typedef struct {
  GValue *val;
  GType specialization_type;
  DBusGTypeSpecializedData *specdata;
} DBusGTypeSpecializedAppendContextReal;

void
dbus_g_type_specialized_init_append (GValue *value, DBusGTypeSpecializedAppendContext *ctx)
{
  DBusGTypeSpecializedAppendContextReal *realctx = (DBusGTypeSpecializedAppendContextReal *) ctx;
  GType gtype;
  DBusGTypeSpecializedData *specdata;
  
  g_return_if_fail (specialized_types_is_initialized ());
  g_return_if_fail (G_VALUE_HOLDS_BOXED (value));
  gtype = G_VALUE_TYPE (value);
  specdata = lookup_specialization_data (gtype);
  g_return_if_fail (specdata != NULL);
  
  realctx->val = value;
  realctx->specialization_type = specdata->types[0];
  realctx->specdata = specdata;
}

void
dbus_g_type_specialized_collection_append (DBusGTypeSpecializedAppendContext *ctx,
					   GValue                            *elt)
{
  DBusGTypeSpecializedAppendContextReal *realctx = (DBusGTypeSpecializedAppendContextReal *) ctx;
  ((DBusGTypeSpecializedCollectionVtable *) realctx->specdata->klass->vtable)->append_func (ctx, elt);
}

void
dbus_g_type_specialized_collection_end_append (DBusGTypeSpecializedAppendContext *ctx)
{
  DBusGTypeSpecializedAppendContextReal *realctx = (DBusGTypeSpecializedAppendContextReal *) ctx;
  if (((DBusGTypeSpecializedCollectionVtable *) realctx->specdata->klass->vtable)->end_append_func != NULL)
    ((DBusGTypeSpecializedCollectionVtable *) realctx->specdata->klass->vtable)->end_append_func (ctx);
}

void
dbus_g_type_specialized_map_append (DBusGTypeSpecializedAppendContext *ctx,
				    GValue                            *key,
				    GValue                            *val)
{
  DBusGTypeSpecializedAppendContextReal *realctx = (DBusGTypeSpecializedAppendContextReal *) ctx;
  ((DBusGTypeSpecializedMapVtable *) realctx->specdata->klass->vtable)->append_func (ctx, key, val);
}

void
dbus_g_type_map_value_iterate (const GValue                           *value,
			       DBusGTypeSpecializedMapIterator         iterator,
			       gpointer                                user_data)
{
  DBusGTypeSpecializedData *data;
  GType gtype;

  g_return_if_fail (specialized_types_is_initialized ());
  g_return_if_fail (G_VALUE_HOLDS_BOXED (value));

  gtype = G_VALUE_TYPE (value);
  data = lookup_specialization_data (gtype);
  g_return_if_fail (data != NULL);

  ((DBusGTypeSpecializedMapVtable *) data->klass->vtable)->iterator (gtype,
								     g_value_get_boxed (value),
								     iterator, user_data);
}
