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
  
  bindtextdomain (GETTEXT_PACKAGE, DBUS_LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE); 
  
  gtk_init (&argc, &argv);

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






