/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-tree-view.c GtkTreeView for a D-BUS interface description
 *
 * Copyright (C) 2003 Red Hat, Inc.
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
#include <string.h>
#include <config.h>
#include "dbus-tree-view.h"
#include <glib/gi18n.h>

enum
{
  MODEL_COLUMN_INFO,

  MODEL_COLUMN_LAST
};

enum
{
  VIEW_COLUMN_NAME,

  VIEW_COLUMN_LAST
};

/* We stuff the node tree into a GtkTreeStore, rather
 * than bothering to write a custom model
 */
static GtkTreeModel*
model_new (void)
{
  GtkTreeModel *model;
  GtkTreeStore *store;

  store = gtk_tree_store_new (MODEL_COLUMN_LAST,
                              BASE_INFO_TYPE);

  model = GTK_TREE_MODEL (store);

  return model;
}

static void set_info (GtkTreeModel *model,
                      GtkTreeIter  *root,
                      BaseInfo     *info);

static void
append_child_list (GtkTreeModel *model,
                   GtkTreeIter  *parent,
                   GSList       *children)
{
  GSList *tmp;
  GtkTreeStore *store;

  store = GTK_TREE_STORE (model);

  /* parent may be NULL for root */

  tmp = children;
  while (tmp != NULL)
    {
      GtkTreeIter iter;

      gtk_tree_store_append (store, &iter, parent);

      set_info (model, &iter, tmp->data);

      tmp = tmp->next;
    }
}

static void
set_info (GtkTreeModel *model,
          GtkTreeIter  *root,
          BaseInfo     *info)
{
  GtkTreeStore *store;
  GtkTreeIter child;

  store = GTK_TREE_STORE (model);

  /* Remeber that root is NULL for "/" path */

  /* Clear existing children */
  while (gtk_tree_model_iter_children (model, &child, root))
    gtk_tree_store_remove (store, &child);

  /* Set our new value; we simply discard NodeInfo for "/" at the
   * moment.
   */
  if (root != NULL)
    {
      gtk_tree_store_set (store, root,
                          MODEL_COLUMN_INFO, info,
                          -1);
    }

  /* Fill in new children */
  switch (base_info_get_type (info))
    {
    case INFO_TYPE_NODE:
      append_child_list (model, root,
                         node_info_get_interfaces ((NodeInfo*)info));
      append_child_list (model, root,
                         node_info_get_nodes ((NodeInfo*)info));
      break;
    case INFO_TYPE_INTERFACE:
      append_child_list (model, root,
                         interface_info_get_methods ((InterfaceInfo*)info));
      append_child_list (model, root,
                         interface_info_get_signals ((InterfaceInfo*)info));
      append_child_list (model, root,
                         interface_info_get_properties ((InterfaceInfo*)info));
      break;
    case INFO_TYPE_METHOD:
      append_child_list (model, root,
                         method_info_get_args ((MethodInfo*)info));
      break;
    case INFO_TYPE_SIGNAL:
      append_child_list (model, root,
                         signal_info_get_args ((SignalInfo*)info));
      break;
    case INFO_TYPE_PROPERTY:
      /* no children */
      break;
    case INFO_TYPE_ARG:
      /* no children */
      break;
    }
}

static void
ensure_tree_node (GtkTreeModel  *model,
                  const char   **path,
                  GtkTreeIter   *iter)
{
  GtkTreeStore *store;
  int i;
  GtkTreeIter child;
  GtkTreeIter *parent;
  GtkTreeIter prev;

  store = GTK_TREE_STORE (model);

  /* The path[0] == NULL case for path "/" can't happen since no tree
   * node is created for that
   */
  g_assert (path[0] != NULL);

  parent = NULL;

  i = 0;
  while (path[i] != NULL)
    {
      gboolean found;

      found = FALSE;

      if (gtk_tree_model_iter_children (model, &child, parent))
        {
          /* Scan for the right path */
          do
            {
              BaseInfo *info;

              info = NULL;
              gtk_tree_model_get (model, &child,
                                  MODEL_COLUMN_INFO, &info,
                                  -1);

              if (info != NULL &&
                  base_info_get_type (info) == INFO_TYPE_NODE &&
                  strcmp (base_info_get_name (info), path[i]) == 0)
                {
                  /* Found it */
                  found = TRUE;
                  break;
                }
            }
          while (gtk_tree_model_iter_next (model, &child));
        }

      if (!found)
        {
          NodeInfo *node;

          node = node_info_new (path[i]);

          gtk_tree_store_append (store, &child, parent);
          gtk_tree_store_set (store, &child,
                              MODEL_COLUMN_INFO, node,
                              -1);
        }

      prev = child;
      parent = &prev;

      ++i;
    }

  g_assert (parent == &prev);
  *iter = prev;
}

static void
model_update (GtkTreeModel  *model,
              const char   **path,
              NodeInfo      *node)
{
  GtkTreeStore *store;

  store = GTK_TREE_STORE (model);

  if (path[0] == NULL)
    {
      /* Setting '/' */

      set_info (model, NULL, (BaseInfo*) node);
    }
  else
    {
      GtkTreeIter iter;
      BaseInfo *old;

      /* Be sure we have the parent node */
      ensure_tree_node (model, path, &iter);

      /* Force the canonical relative path name on the node */
      old = NULL;
      gtk_tree_model_get (model, &iter,
                          MODEL_COLUMN_INFO, &old,
                          -1);
      base_info_set_name ((BaseInfo*) node,
                          base_info_get_name (old));

      /* Fill in the new children */
      set_info (model, &iter, (BaseInfo*) node);
    }
}

static void
info_set_func_text (GtkTreeViewColumn *tree_column,
                    GtkCellRenderer   *cell,
                    GtkTreeModel      *model,
                    GtkTreeIter       *iter,
                    gpointer           data)
{
  BaseInfo *info;
  GString *str;

  info = NULL;
  gtk_tree_model_get (model, iter,
                      MODEL_COLUMN_INFO, &info,
                      -1);

  if (info == NULL)
    return;

  str = g_string_new (NULL);

  switch (base_info_get_type (info))
    {
    case INFO_TYPE_NODE:
      g_string_append (str, "<i>path</i>");
      break;
    case INFO_TYPE_INTERFACE:
      g_string_append (str, "<i>interface</i>");
      break;
    case INFO_TYPE_METHOD:
      g_string_append (str, "<i>method</i>");
      break;
    case INFO_TYPE_SIGNAL:
      g_string_append (str, "<i>signal</i>");
      break;
    case INFO_TYPE_PROPERTY:
      g_string_append (str, "<i>property</i>");
      g_string_append_printf (str, " <b>%s</b>",
                              property_info_get_type ((PropertyInfo*)info));
      break;
    case INFO_TYPE_ARG:
      g_string_append_printf (str, "<i>arg</i> %s",
                              arg_info_get_direction ((ArgInfo*)info) == ARG_IN ?
                              "in" : "out");
      g_string_append_printf (str, " <b>%s</b>",
                              arg_info_get_type ((ArgInfo*)info));
      break;
    }

  g_string_append (str, " ");
  g_string_append (str, base_info_get_name (info));

  g_object_set (GTK_CELL_RENDERER (cell),
                "markup", str->str,
                NULL);

  g_string_free (str, TRUE);

  /* base_info_unref (info); */
}

GtkWidget*
dbus_tree_view_new (void)
{
  GtkWidget *treeview;
  GtkCellRenderer *cell_renderer;
  GtkTreeViewColumn *column;

  treeview = gtk_tree_view_new ();

  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column, _("Name"));

  cell_renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column,
                                   cell_renderer,
                                   TRUE);
  gtk_tree_view_column_set_cell_data_func (column, cell_renderer,
                                           info_set_func_text, NULL, NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (treeview),
                               column);

  return treeview;
}

void
dbus_tree_view_update (GtkTreeView *view,
                       const char **path,
                       NodeInfo    *node)
{
  GtkTreeModel *model;

  g_return_if_fail (GTK_IS_TREE_VIEW (view));

  model = gtk_tree_view_get_model (view);

  if (model == NULL)
    {
      model = model_new ();
      model_update (model, path, node);
      gtk_tree_view_set_model (view, model);
      g_object_unref (G_OBJECT (model));
    }
  else
    {
      model_update (model, path, node);
    }
}

void
dbus_tree_view_clear (GtkTreeView  *view)
{
  GtkTreeModel *model;

  g_return_if_fail (GTK_IS_TREE_VIEW (view));

  model = gtk_tree_view_get_model (view);

  if (model != NULL)
    gtk_tree_store_clear (GTK_TREE_STORE (model));
}

