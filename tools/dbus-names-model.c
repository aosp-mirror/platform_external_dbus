/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-names-model.c GtkTreeModel for names on the bus
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
#include "dbus-names-model.h"
#include <glib/gi18n.h>

enum
{
  MODEL_COLUMN_NAME_DATA,
  
  MODEL_COLUMN_LAST
};

typedef struct
{
  int   refcount;
  char *name;
} NameData;

static NameData*
name_data_new (const char *name)
{
  NameData *nd;

  nd = g_new0 (NameData, 1);

  nd->refcount = 1;
  nd->name = g_strdup (name);

  return nd;
}

static NameData*
name_data_ref (NameData *nd)
{
  nd->refcount += 1;
  return nd;
}

static void
name_data_unref (NameData *nd)
{
  nd->refcount -= 1;
  if (nd->refcount == 0)
    {
      g_free (nd->name);
      g_free (nd);
    }
}

static GType
name_data_get_gtype (void)
{
  static GType our_type = 0;

  if (our_type == 0)
    our_type = g_boxed_type_register_static ("NameData",
                                             (GBoxedCopyFunc) name_data_ref,
                                             (GBoxedFreeFunc) name_data_unref);

  return our_type;
}

#define NAME_DATA_TYPE (name_data_get_gtype())


typedef struct NamesModel NamesModel;
typedef struct NamesModelClass NamesModelClass;

GType names_model_get_type (void);

struct NamesModel
{
  GtkTreeStore parent;
  DBusGConnection *connection;
  DBusGProxy *driver_proxy;
  DBusGPendingCall *pending_list_names;
};

struct NamesModelClass
{
  GtkTreeStoreClass parent;
};

#define TYPE_NAMES_MODEL              (names_model_get_type ())
#define NAMES_MODEL(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), TYPE_NAMES_MODEL, NamesModel))
#define NAMES_MODEL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_NAMES_MODEL, NamesModelClass))
#define IS_NAMES_MODEL(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), TYPE_NAMES_MODEL))
#define IS_NAMES_MODEL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_NAMES_MODEL))
#define NAMES_MODEL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_NAMES_MODEL, NamesModelClass))

static void
have_names_notify (DBusGPendingCall *call,
                   void             *data)
{
  NamesModel *names_model;
  GError *error;
  char **names;
  int n_elements;
  int i;

  names_model = NAMES_MODEL (data);

  g_assert (names_model->pending_list_names);
  g_assert (names_model->driver_proxy);

  names = NULL;
  error = NULL;
  if (!dbus_g_proxy_end_call (names_model->driver_proxy,
                              names_model->pending_list_names,
                              &error, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING,
                              &names, &n_elements, DBUS_TYPE_INVALID))
    {
      g_assert (names == NULL);
      g_assert (error != NULL);
      
      g_printerr (_("Failed to load names on the bus: %s\n"), error->message);
      return;
    }

  i = 0;
  while (names[i])
    {
      NameData *nd;
      GtkTreeIter iter;
      
      nd = name_data_new (names[i]);

      gtk_tree_store_append (GTK_TREE_STORE (names_model),
                             &iter, NULL);

      gtk_tree_store_set (GTK_TREE_STORE (names_model),
                          &iter,
                          MODEL_COLUMN_NAME_DATA, nd,
                          -1);

      name_data_unref (nd);
      
      ++i;
    }
  
  g_strfreev (names);
}

static void
names_model_reload (NamesModel *names_model)
{
  GtkTreeStore *tree_store;

  tree_store = GTK_TREE_STORE (names_model);

  if (names_model->pending_list_names)
    {
      dbus_g_pending_call_cancel (names_model->pending_list_names);
      dbus_g_pending_call_unref (names_model->pending_list_names);
      names_model->pending_list_names = NULL;
    }
  
  gtk_tree_store_clear (tree_store);
  
  if (names_model->connection == NULL)
    return;
  
  names_model->pending_list_names =
    dbus_g_proxy_begin_call (names_model->driver_proxy,
                             "ListNames",
                             DBUS_TYPE_INVALID);

  dbus_g_pending_call_set_notify (names_model->pending_list_names,
                                  have_names_notify, names_model, NULL);
}

static void
names_model_set_connection (NamesModel      *names_model,
                            DBusGConnection *connection)
{
  const char *match_rule = "type='signal',member='NameOwnerChanged'";

  g_return_if_fail (IS_NAMES_MODEL (names_model));
  
  if (connection == names_model->connection)
    return;

  if (names_model->connection)
    {
      dbus_g_proxy_call_no_reply (names_model->driver_proxy,
                                  "RemoveMatch", 
                                  DBUS_TYPE_STRING, &match_rule,
                                  DBUS_TYPE_INVALID);
      g_object_unref (names_model->driver_proxy);
      names_model->driver_proxy = NULL;
      dbus_g_connection_unref (names_model->connection);
      names_model->connection = NULL;
    }
  
  if (connection)
    {
      names_model->connection = connection;
      dbus_g_connection_ref (names_model->connection);
      
      names_model->driver_proxy =
        dbus_g_proxy_new_for_name (names_model->connection,
                                   DBUS_SERVICE_ORG_FREEDESKTOP_DBUS,
                                   DBUS_PATH_ORG_FREEDESKTOP_DBUS,
                                   DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS);
      g_assert (names_model->driver_proxy);
    }

  names_model_reload (names_model);
}

G_DEFINE_TYPE(NamesModel, names_model, GTK_TYPE_TREE_STORE)

/* Properties */
enum
{
  PROP_0,
  PROP_CONNECTION
};

static void
names_model_dispose (GObject *object)
{
  NamesModel *names_model = NAMES_MODEL (object);

  names_model_set_connection (names_model, NULL);

  g_assert (names_model->connection == NULL);
  g_assert (names_model->driver_proxy == NULL);
  g_assert (names_model->pending_list_names == NULL);

  (G_OBJECT_CLASS (names_model_parent_class)->dispose) (object);
}

static void
names_model_finalize (GObject *object)
{
  NamesModel *names_model = NAMES_MODEL (object);

  g_assert (names_model->connection == NULL);
  g_assert (names_model->driver_proxy == NULL);
  g_assert (names_model->pending_list_names == NULL);

  (G_OBJECT_CLASS (names_model_parent_class)->finalize) (object);
}

static void
names_model_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  NamesModel *names_model;

  names_model = NAMES_MODEL (object);

  switch (prop_id)
    {
    case PROP_CONNECTION:
      names_model_set_connection (names_model, g_value_get_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
names_model_get_property (GObject      *object,
                          guint         prop_id,
                          GValue       *value,
                          GParamSpec   *pspec)
{
  NamesModel *names_model;

  names_model = NAMES_MODEL (object);

  switch (prop_id)
    {
    case PROP_CONNECTION:
      g_value_set_boxed (value, names_model->connection);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
names_model_init (NamesModel *names_model)
{
  GtkTreeStore *tree_store;
  GType types[MODEL_COLUMN_LAST];

  tree_store = GTK_TREE_STORE (names_model);

  types[0] = NAME_DATA_TYPE;
  gtk_tree_store_set_column_types (tree_store, MODEL_COLUMN_LAST, types);
}

static void
names_model_class_init (NamesModelClass *names_model_class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (names_model_class);

  gobject_class->finalize = names_model_finalize;
  gobject_class->dispose = names_model_dispose;
  gobject_class->set_property = names_model_set_property;
  gobject_class->get_property = names_model_get_property;

  g_object_class_install_property (gobject_class,
				   PROP_CONNECTION,
				   g_param_spec_boxed ("connection",
                                                       _("Bus connection"),
                                                       _("Connection to the message bus"),
                                                       DBUS_TYPE_G_CONNECTION,
                                                       G_PARAM_READWRITE));
}

GtkTreeModel*
names_model_new (DBusGConnection *connection)
{
  NamesModel *names_model;

  names_model = g_object_new (TYPE_NAMES_MODEL,
                              "connection", connection,
                              NULL);

  return GTK_TREE_MODEL (names_model);
}

