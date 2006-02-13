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
  DBUS_G_SPECTYPE_MAP,
  DBUS_G_SPECTYPE_STRUCT
} DBusGTypeSpecializedType;

typedef struct {
  DBusGTypeSpecializedType type;
  const DBusGTypeSpecializedVtable     *vtable;
} DBusGTypeSpecializedContainer;

typedef struct {
  guint num_types;
  GType *types;
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

      if (data->klass->vtable->free_func)
        {
          data->klass->vtable->free_func (type, value->data[0].v_pointer);
        }
      else
        {
          g_assert (data->klass->vtable->simple_free_func != NULL);
          data->klass->vtable->simple_free_func (value->data[0].v_pointer);
        }
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
        {
	  value->data[0].v_pointer = data->klass->vtable->copy_func (type, collect_values[0].v_pointer);
        }
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
build_specialization_name (const char *prefix, guint num_types, GType *types)
{
  GString *fullname;
  guint i;

  fullname = g_string_new (prefix);

  g_string_append_c (fullname, '_');
  for (i=0; i < num_types; i++)
    {
      if (i!=0)
        g_string_append_c (fullname, '+');
      g_string_append (fullname, g_type_name (types[i]));
    }
  g_string_append_c (fullname, '_');
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

void
dbus_g_type_register_struct (const char                             *name,
			     const DBusGTypeSpecializedStructVtable *vtable,
			     guint                                   flags)
{
  g_return_if_fail (specialized_types_is_initialized ());
  register_container (name, DBUS_G_SPECTYPE_STRUCT, (const DBusGTypeSpecializedVtable*) vtable);
}


const DBusGTypeSpecializedMapVtable* dbus_g_type_map_peek_vtable (GType map_type)
{
  DBusGTypeSpecializedData *data;
  g_return_val_if_fail (dbus_g_type_is_map(map_type), NULL);

  data = lookup_specialization_data (map_type);
  g_assert (data != NULL);

  return (DBusGTypeSpecializedMapVtable *)(data->klass->vtable);
}

const DBusGTypeSpecializedCollectionVtable* dbus_g_type_collection_peek_vtable (GType collection_type)
{
  DBusGTypeSpecializedData *data;
  g_return_val_if_fail (dbus_g_type_is_collection(collection_type), NULL);

  data = lookup_specialization_data (collection_type);
  g_assert (data != NULL);

  return (DBusGTypeSpecializedCollectionVtable *)(data->klass->vtable);
}

const DBusGTypeSpecializedStructVtable* dbus_g_type_struct_peek_vtable (GType struct_type)
{
  DBusGTypeSpecializedData *data;
  g_return_val_if_fail (dbus_g_type_is_struct (struct_type), NULL);

  data = lookup_specialization_data (struct_type);
  g_assert (data != NULL);

  return (DBusGTypeSpecializedStructVtable *)(data->klass->vtable);
}

static GType
register_specialized_instance (const DBusGTypeSpecializedContainer   *klass,
			       char                                  *name,
			       guint                                  num_types,
			       GType                                 *types)
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
      data->num_types = num_types;
      data->types = g_memdup (types, sizeof (GType) * num_types);
      data->klass = klass;
      g_type_set_qdata (ret, specialized_type_data_quark (), data);
    }

  return ret;
}

static GType
lookup_or_register_specialized (const char  *container,
				guint        num_types,
				GType       *types)
{
  GType ret;
  char *name;
  const DBusGTypeSpecializedContainer *klass;

  g_return_val_if_fail (specialized_types_is_initialized (), G_TYPE_INVALID);

  klass = g_hash_table_lookup (specialized_containers, container);
  g_return_val_if_fail (klass != NULL, G_TYPE_INVALID);

  name = build_specialization_name (container, num_types, types);
  ret = g_type_from_name (name);
  if (ret == G_TYPE_INVALID)
    {
      /* Take ownership of name */
      ret = register_specialized_instance (klass, name,
					   num_types,
					   types);
    }
  else
    g_free (name);
  return ret;
}

GType
dbus_g_type_get_collection (const char *container,
			    GType       specialization)
{
  return lookup_or_register_specialized (container, 1, &specialization);
}

GType
dbus_g_type_get_map (const char   *container,
		     GType         key_specialization,
		     GType         value_specialization)
{
  GType types[2] = {key_specialization, value_specialization};
  return lookup_or_register_specialized (container, 2, types);
}

GType
dbus_g_type_get_structv (const char   *container,
                        guint         num_items,
                        GType        *types)
{
  return lookup_or_register_specialized (container, num_items, types);
}

GType
dbus_g_type_get_struct (const char *container,
                        GType first_type,
                        ...)
{
  GArray *types;
  GType curtype;
  va_list args;
  va_start (args, first_type);

  types = g_array_new (FALSE, FALSE, sizeof (GType));
  curtype = first_type;
  while (curtype != G_TYPE_INVALID)
    {
      g_array_append_val (types, curtype);
      curtype = va_arg (args, GType);
    }
  va_end (args);
  return lookup_or_register_specialized (container, types->len, (GType*)types->data);

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

gboolean
dbus_g_type_is_struct (GType gtype)
{
  DBusGTypeSpecializedData *data;
  data = lookup_specialization_data (gtype);
  if (data == NULL)
    return FALSE;
  return data->klass->type == DBUS_G_SPECTYPE_STRUCT;
}


static GType
get_specialization_index (GType gtype, guint i)
{
  DBusGTypeSpecializedData *data;

  data = lookup_specialization_data (gtype);
  if (i < data->num_types)
    return data->types[i];
  else
    return G_TYPE_INVALID;
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

GType
dbus_g_type_get_struct_member_type (GType gtype, guint index)
{
  g_return_val_if_fail (dbus_g_type_is_struct (gtype), G_TYPE_INVALID);
  return get_specialization_index (gtype, index);
}

guint
dbus_g_type_get_struct_size (GType gtype)
{
  DBusGTypeSpecializedData *data;
  g_return_val_if_fail (dbus_g_type_is_struct (gtype), G_TYPE_INVALID);

  data = lookup_specialization_data (gtype);
  return data->num_types;
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
  g_return_if_fail (specdata->num_types != 0);

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

gboolean
dbus_g_type_struct_get_member (const GValue *value,
			       guint         index,
			       GValue       *dest)
{
  DBusGTypeSpecializedData *data;
  GType gtype;

  g_return_val_if_fail (specialized_types_is_initialized (), FALSE);
  g_return_val_if_fail (G_VALUE_HOLDS_BOXED (value), FALSE);

  gtype = G_VALUE_TYPE (value);
  data = lookup_specialization_data (gtype);
  g_return_val_if_fail (data != NULL, FALSE);

  return ((DBusGTypeSpecializedStructVtable *) (data->klass->vtable))->get_member(gtype,
											   g_value_get_boxed (value),
											   index, dest);
}

gboolean
dbus_g_type_struct_set_member (GValue       *value,
			       guint         index,
			       const GValue *src)
{
  DBusGTypeSpecializedData *data;
  GType gtype;

  g_return_val_if_fail (specialized_types_is_initialized (), FALSE);
  g_return_val_if_fail (G_VALUE_HOLDS_BOXED (value), FALSE);

  gtype = G_VALUE_TYPE (value);
  data = lookup_specialization_data (gtype);
  g_return_val_if_fail (data != NULL, FALSE);

  return ((DBusGTypeSpecializedStructVtable *) (data->klass->vtable))->set_member(gtype,
											   g_value_get_boxed (value),
											   index, src);
}

/**
 * dbus_g_type_struct_get:
 * @value: a GValue containing a DBusGTypeStruct type
 * @member: struct member to get
 * @...: location in which to return the value of this member,
 *       followed optionally by more member/return locations pairs, followed by
 *       by G_MAXUINT
 *
 * Collects the selected values of this struct into the return locations
 * provided.
 *
 * Returns: FALSE on failure
 */

gboolean
dbus_g_type_struct_get                   (const GValue *value,
                                          guint first_member,
                                          ...)
{
  va_list var_args;
  GType type;
  guint size,i;
  gchar *error;
  GValue val = {0,};

  g_return_val_if_fail (dbus_g_type_is_struct (G_VALUE_TYPE (value)), FALSE);

  va_start (var_args, first_member);
  size = dbus_g_type_get_struct_size (G_VALUE_TYPE (value));
  i = first_member;
  while (i != G_MAXUINT)
    {
      if (i >= size)
        goto error;

      type = dbus_g_type_get_struct_member_type (G_VALUE_TYPE (value),i);

      g_value_init (&val, type);
      dbus_g_type_struct_get_member (value, i, &val);

      G_VALUE_LCOPY (&val, var_args, 0, &error);
      if (error)
        {
          g_warning ("%s, %s", G_STRFUNC, error);
          g_free (error);
          g_value_unset (&val);
          goto error;
        }
      g_value_unset (&val);
      i = va_arg (var_args, guint);
    }
  va_end (var_args);
  return TRUE;
error:
  va_end (var_args);
  return FALSE;
}

/**
 * dbus_g_type_struct_set:
 * @value: a GValue containing a DBusGTypeStruct type
 * @member: struct member to set
 * @...: value for the first member, followed optionally by
 *       more member/value pairs, followed by G_MAXUINT
 *
 * Sets the selected members of the struct in @value.
 *
 * Returns: FALSE on failure
 */

gboolean
dbus_g_type_struct_set                   (GValue *value,
                                          guint first_member,
                                          ...)
{
  va_list var_args;
  GType type;
  guint size,i;
  gchar *error;
  GValue val = {0,};

  g_return_val_if_fail (dbus_g_type_is_struct (G_VALUE_TYPE (value)), FALSE);

  va_start (var_args, first_member);
  size = dbus_g_type_get_struct_size (G_VALUE_TYPE (value));
  i = first_member;
  while (i != G_MAXUINT)
    {
      if (i >= size)
        goto error;

      type = dbus_g_type_get_struct_member_type (G_VALUE_TYPE (value),i);

      g_value_init (&val, type);

      G_VALUE_COLLECT (&val, var_args, 0, &error);
      if (error)
        {
          g_warning ("%s, %s", G_STRFUNC, error);
          g_free (error);
          g_value_unset (&val);
          goto error;
        }

      dbus_g_type_struct_set_member (value, i, &val);

      g_value_unset (&val);
      i = va_arg (var_args, guint);
    }
  va_end (var_args);
  return TRUE;
error:
  va_end (var_args);
  return FALSE;
}

