/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-viewer.c Graphical D-BUS frontend utility
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
#include <config.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include "dbus-tree-view.h"
#include <glib/dbus-gparser.h>
#include <glib/dbus-gutils.h>
#include <dbus/dbus-glib.h>

#include <libintl.h>
#define _(x) dgettext (GETTEXT_PACKAGE, x)
#define N_(x) x

typedef struct
{
  int refcount;
  char *name;

} ServiceData;

static ServiceData*
service_data_new (const char *name)
{
  ServiceData *sd;

  sd = g_new0 (ServiceData, 1);

  sd->refcount = 1;
  sd->name = g_strdup (name);

  return sd;
}

static void
service_data_ref (ServiceData *sd)
{
  sd->refcount += 1;
}

static void
service_data_unref (ServiceData *sd)
{
  sd->refcount -= 1;
  if (sd->refcount == 0)
    {
      g_free (sd->name);
      g_free (sd);
    }
}

typedef struct
{
  GtkWidget *window;
  GtkWidget *treeview;
  GtkWidget *service_menu;

  GSList *services;
  
} TreeWindow;

static void
window_closed_callback (GtkWidget  *window,
                        TreeWindow *w)
{
  g_assert (window == w->window);
  w->window = NULL;
  gtk_main_quit ();
}

static TreeWindow*
tree_window_new (void)
{
  TreeWindow *w;
  GtkWidget *sw;
  GtkWidget *vbox;
  GtkWidget *hbox;

  /* Should use glade, blah */
  
  w = g_new0 (TreeWindow, 1);
  w->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  gtk_window_set_title (GTK_WINDOW (w->window), "D-BUS Viewer");
  gtk_window_set_default_size (GTK_WINDOW (w->window), 400, 500);

  g_signal_connect (w->window, "destroy", G_CALLBACK (window_closed_callback),
                    w);
  gtk_container_set_border_width (GTK_CONTAINER (w->window), 6);

  vbox = gtk_vbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (w->window), vbox);
  
  hbox = gtk_hbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (vbox), hbox);

  /* Create tree view */
  
  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);

  gtk_box_pack_start (GTK_BOX (hbox), sw, TRUE, TRUE, 0);

  w->treeview = dbus_tree_view_new ();

  gtk_container_add (GTK_CONTAINER (sw), w->treeview);

  /* Create services option menu */

  

  /* Show everything */
  gtk_widget_show_all (w->window);

  return w;
}

static void
show_error_dialog (GtkWindow *transient_parent,
                   GtkWidget **weak_ptr,
                   const char *message_format,
                   ...)
{
  char *message;
  va_list args;

  if (message_format)
    {
      va_start (args, message_format);
      message = g_strdup_vprintf (message_format, args);
      va_end (args);
    }
  else
    message = NULL;

  if (weak_ptr == NULL || *weak_ptr == NULL)
    {
      GtkWidget *dialog;
      dialog = gtk_message_dialog_new (transient_parent,
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_ERROR,
                                       GTK_BUTTONS_CLOSE,
                                       message);

      g_signal_connect (G_OBJECT (dialog), "response", G_CALLBACK (gtk_widget_destroy), NULL);

      if (weak_ptr != NULL)
        {
          *weak_ptr = dialog;
          g_object_add_weak_pointer (G_OBJECT (dialog), (void**)weak_ptr);
        }

      gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
      
      gtk_widget_show_all (dialog);
    }
  else 
    {
      g_return_if_fail (GTK_IS_MESSAGE_DIALOG (*weak_ptr));

      gtk_label_set_text (GTK_LABEL (GTK_MESSAGE_DIALOG (*weak_ptr)->label), message);

      gtk_window_present (GTK_WINDOW (*weak_ptr));
    }
}

static gboolean
load_child_nodes (const char *service_name,
                  NodeInfo   *parent,
                  GString    *path,
                  GError    **error)
{
  DBusGConnection *connection;
  GSList *tmp;
  
  connection = dbus_g_bus_get (DBUS_BUS_SESSION, error);
  if (connection == NULL)
    return FALSE;
  
  tmp = node_info_get_nodes (parent);
  while (tmp != NULL)
    {
      DBusGProxy *proxy;
      DBusGPendingCall *call;
      const char *data;
      NodeInfo *child;
      NodeInfo *complete_child;
      int save_len;

      complete_child = NULL;
      call = NULL;
      
      child = tmp->data;

      save_len = path->len;

      if (save_len > 1)
        g_string_append (path, "/");
      g_string_append (path, base_info_get_name ((BaseInfo*)child));

      if (*service_name == ':')
        {
          proxy = dbus_g_proxy_new_for_name (connection,
                                             service_name,
                                             path->str,
                                             DBUS_INTERFACE_ORG_FREEDESKTOP_INTROSPECTABLE);
          g_assert (proxy != NULL);
        }
      else
        {
          proxy = dbus_g_proxy_new_for_name_owner (connection,
                                                   service_name,
                                                   path->str,
                                                   DBUS_INTERFACE_ORG_FREEDESKTOP_INTROSPECTABLE,
                                                   error);
          if (proxy == NULL)
            goto done;
        }
  
      call = dbus_g_proxy_begin_call (proxy, "Introspect",
                                      DBUS_TYPE_INVALID);
      
      data = NULL;
      if (!dbus_g_proxy_end_call (proxy, call, error, DBUS_TYPE_STRING, &data,
                                  DBUS_TYPE_INVALID))
        goto done;
      
      complete_child = description_load_from_string (data, -1, error);
      if (complete_child == NULL)
        {
          g_printerr ("%s\n", data);
          goto done;
        }
      
    done:
      if (call)
        dbus_g_pending_call_unref (call);
      g_object_unref (proxy);

      if (complete_child == NULL)
        return FALSE;

      /* change complete_child's name to relative */
      base_info_set_name ((BaseInfo*)complete_child,
                          base_info_get_name ((BaseInfo*)child));
      
      /* Stitch in complete_child rather than child */
      node_info_replace_node (parent, child, complete_child);
      node_info_unref (complete_child); /* ref still held by parent */
      
      /* Now recurse */
      if (!load_child_nodes (service_name, complete_child, path, error))
        return FALSE;

      /* restore path */
      g_string_set_size (path, save_len);
      
      tmp = tmp->next;
    }

  return TRUE;
}

static NodeInfo*
load_from_service (const char *service_name,
                   GError    **error)
{
  DBusGConnection *connection;
  DBusGProxy *root_proxy;
  DBusGPendingCall *call;
  const char *data;
  NodeInfo *node;
  GString *path;

  node = NULL;
  call = NULL;
  path = NULL;
  
  connection = dbus_g_bus_get (DBUS_BUS_SESSION, error);
  if (connection == NULL)
    return NULL;

#if 1
  /* this will end up autolaunching the service when we introspect it */
  root_proxy = dbus_g_proxy_new_for_name (connection,
                                          service_name,
                                          "/",
                                          DBUS_INTERFACE_ORG_FREEDESKTOP_INTROSPECTABLE);
  g_assert (root_proxy != NULL);
#else
  /* this will be an error if the service doesn't exist */
  root_proxy = dbus_g_proxy_new_for_name_owner (connection,
                                                service_name,
                                                "/",
                                                DBUS_INTERFACE_ORG_FREEDESKTOP_INTROSPECTABLE,
                                                error);
  if (root_proxy == NULL)
    {
      g_printerr ("Failed to get owner of '%s'\n", service_name);
      return NULL;
    }
#endif
  
  call = dbus_g_proxy_begin_call (root_proxy, "Introspect",
                                  DBUS_TYPE_INVALID);

  data = NULL;
  if (!dbus_g_proxy_end_call (root_proxy, call, error, DBUS_TYPE_STRING, &data,
                              DBUS_TYPE_INVALID))
    {
      g_printerr ("Failed to Introspect() %s\n",
                  dbus_g_proxy_get_bus_name (root_proxy));
      goto out;
    }

  node = description_load_from_string (data, -1, error);

  /* g_print ("%s\n", data); */
  
  if (node == NULL)
    goto out;

  base_info_set_name ((BaseInfo*)node, "/");

  path = g_string_new ("/");
  
  if (!load_child_nodes (dbus_g_proxy_get_bus_name (root_proxy),
                         node, path, error))
    {
      node_info_unref (node);
      node = NULL;
      goto out;
    }
  
 out:
  if (call)
    dbus_g_pending_call_unref (call);
    
  g_object_unref (root_proxy);

  if (path)
    g_string_free (path, TRUE);
  
  return node;
}

static void
usage (int ecode)
{
  fprintf (stderr, "dbus-viewer [--version] [--help]\n");
  exit (ecode);
}

static void
version (void)
{
  printf ("D-BUS Message Bus Viewer %s\n"
          "Copyright (C) 2003 Red Hat, Inc.\n"
          "This is free software; see the source for copying conditions.\n"
          "There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n",
          VERSION);
  exit (0);
}

int
main (int argc, char **argv)
{
  const char *prev_arg;
  int i;
  GSList *files;
  gboolean end_of_args;
  GSList *tmp;
  gboolean services;
  
  bindtextdomain (GETTEXT_PACKAGE, DBUS_LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE); 
  
  gtk_init (&argc, &argv);

  services = FALSE;
  end_of_args = FALSE;
  files = NULL;
  prev_arg = NULL;
  i = 1;
  while (i < argc)
    {
      const char *arg = argv[i];

      if (!end_of_args)
        {
          if (strcmp (arg, "--help") == 0 ||
              strcmp (arg, "-h") == 0 ||
              strcmp (arg, "-?") == 0)
            usage (0);
          else if (strcmp (arg, "--version") == 0)
            version ();
          else if (strcmp (arg, "--services") == 0)
            services = TRUE;
          else if (arg[0] == '-' &&
                   arg[1] == '-' &&
                   arg[2] == '\0')
            end_of_args = TRUE;
          else if (arg[0] == '-')
            {
              usage (1);
            }
          else
            {
              files = g_slist_prepend (files, (char*) arg);
            }
        }
      else
        files = g_slist_prepend (files, (char*) arg);
      
      prev_arg = arg;
      
      ++i;
    }

  files = g_slist_reverse (files);

  tmp = files;
  while (tmp != NULL)
    {
      NodeInfo *node;
      GError *error;
      const char *filename;

      filename = tmp->data;

      error = NULL;
      if (services)
        node = load_from_service (filename, &error);
      else
        node = description_load_from_file (filename,
                                           &error);
      
      if (node == NULL)
        {
          g_assert (error != NULL);
          show_error_dialog (NULL, NULL,
                             _("Unable to load \"%s\": %s\n"),
                             filename, error->message);
          g_error_free (error);
        }
      else
        {
          TreeWindow *w;
          char **path;
          const char *name;

          name = node_info_get_name (node);
          if (name == NULL ||
              name[0] != '/')
            {
              g_printerr (_("Assuming root node of \"%s\" is at path /, since no absolute path is specified"), filename);
              name = "/";
            }

          path = _dbus_gutils_split_path (name);
          
          w = tree_window_new ();          
          dbus_tree_view_update (GTK_TREE_VIEW (w->treeview),
                                 (const char**) path,
                                 node);
          node_info_unref (node);

          g_strfreev (path);
        }
      
      tmp = tmp->next;
    }

  gtk_main ();
  
  return 0;
}

