/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-tree-view.h GtkTreeView for a D-BUS interface description
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
#ifndef DBUS_TREE_VIEW_H
#define DBUS_TREE_VIEW_H

#include <gtk/gtk.h>
#include <dbus/dbus-glib.h>
#include <glib/dbus-gidl.h>

GtkWidget*   dbus_tree_view_new    (void);
void         dbus_tree_view_update (GtkTreeView  *view,
                                    const char  **path,
                                    NodeInfo     *info);
void         dbus_tree_view_clear  (GtkTreeView  *view);

#endif /* DBUS_TREE_VIEW_H */
