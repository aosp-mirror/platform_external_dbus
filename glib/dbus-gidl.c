/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gidl.c data structure describing an interface, to be generated from IDL
 *             or something
 *
 * Copyright (C) 2003  Red Hat, Inc.
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

#include "dbus-gidl.h"

#ifndef DOXYGEN_SHOULD_SKIP_THIS

struct BaseInfo
{
  unsigned int refcount : 28;
  unsigned int type     : 4;
  char *name;
};

struct NodeInfo
{
  BaseInfo base;
  GSList *interfaces;
  GSList *nodes;
};

struct InterfaceInfo
{
  BaseInfo base;
  /* Since we have BaseInfo now these could be one list */
  GSList *methods;
  GSList *signals;
};

struct MethodInfo
{
  BaseInfo base;
  GSList *args;
};

struct SignalInfo
{
  BaseInfo base;
  GSList *args;
};

struct ArgInfo
{
  BaseInfo base;
  int type;
  ArgDirection direction;
};

BaseInfo *
base_info_ref (BaseInfo *info)
{
  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->refcount > 0, NULL);
  
  info->refcount += 1;

  return info;
}

static void
base_info_free (void *ptr)
{
  BaseInfo *info;

  info = ptr;
  
  g_free (info->name);
  g_free (info);
}

void
base_info_unref (BaseInfo *info)
{
  g_return_if_fail (info != NULL);
  g_return_if_fail (info->refcount > 0);
  
  /* This is sort of bizarre, BaseInfo was tacked on later */

  switch (info->type)
    {
    case INFO_TYPE_NODE:
      node_info_unref ((NodeInfo*) info);
      break;
    case INFO_TYPE_INTERFACE:
      interface_info_unref ((InterfaceInfo*) info);
      break;
    case INFO_TYPE_SIGNAL:
      signal_info_unref ((SignalInfo*) info);
      break;
    case INFO_TYPE_METHOD:
      method_info_unref ((MethodInfo*) info);
      break;
    case INFO_TYPE_ARG:
      arg_info_unref ((ArgInfo*) info);
      break;
    }
}

InfoType
base_info_get_type (BaseInfo      *info)
{
  return info->type;
}

const char*
base_info_get_name (BaseInfo *info)
{
  return info->name;
}

void
base_info_set_name (BaseInfo      *info,
                    const char    *name)
{
  char *old;

  old = info->name;
  info->name = g_strdup (name);
  g_free (old);
}

GType
base_info_get_gtype (void)
{
  static GType our_type = 0;
  
  if (our_type == 0)
    our_type = g_boxed_type_register_static ("BaseInfo",
                                             (GBoxedCopyFunc) base_info_ref,
                                             (GBoxedFreeFunc) base_info_unref);

  return our_type;
}

static void
free_interface_list (GSList **interfaces_p)
{
  GSList *tmp;
  tmp = *interfaces_p;
  while (tmp != NULL)
    {
      interface_info_unref (tmp->data);
      tmp = tmp->next;
    }
  g_slist_free (*interfaces_p);
  *interfaces_p = NULL;
}

static void
free_node_list (GSList **nodes_p)
{
  GSList *tmp;
  tmp = *nodes_p;
  while (tmp != NULL)
    {
      node_info_unref (tmp->data);
      tmp = tmp->next;
    }
  g_slist_free (*nodes_p);
  *nodes_p = NULL;
}

static void
free_method_list (GSList **methods_p)
{
  GSList *tmp;
  tmp = *methods_p;
  while (tmp != NULL)
    {
      method_info_unref (tmp->data);
      tmp = tmp->next;
    }
  g_slist_free (*methods_p);
  *methods_p = NULL;
}

static void
free_signal_list (GSList **signals_p)
{
  GSList *tmp;
  tmp = *signals_p;
  while (tmp != NULL)
    {
      signal_info_unref (tmp->data);
      tmp = tmp->next;
    }
  g_slist_free (*signals_p);
  *signals_p = NULL;
}

NodeInfo*
node_info_new (const char *name)
{
  NodeInfo *info;

  /* name can be NULL */
  
  info = g_new0 (NodeInfo, 1);
  info->base.refcount = 1;
  info->base.name = g_strdup (name);
  info->base.type = INFO_TYPE_NODE;
  
  return info;
}

NodeInfo *
node_info_ref (NodeInfo *info)
{
  info->base.refcount += 1;

  return info;
}

void
node_info_unref (NodeInfo *info)
{
  info->base.refcount -= 1;
  if (info->base.refcount == 0)
    {
      free_interface_list (&info->interfaces);
      free_node_list (&info->nodes);
      base_info_free (info);
    }
}

const char*
node_info_get_name (NodeInfo *info)
{
  return info->base.name;
}

GSList*
node_info_get_interfaces (NodeInfo *info)
{
  return info->interfaces;
}

void
node_info_add_interface (NodeInfo *info,
                         InterfaceInfo    *interface)
{
  interface_info_ref (interface);
  info->interfaces = g_slist_append (info->interfaces, interface);
}

GSList*
node_info_get_nodes (NodeInfo *info)
{
  return info->nodes;
}

void
node_info_add_node (NodeInfo *info,
                    NodeInfo *node)
{
  node_info_ref (node);
  info->nodes = g_slist_append (info->nodes, node);
}

InterfaceInfo*
interface_info_new (const char *name)
{
  InterfaceInfo *info;

  info = g_new0 (InterfaceInfo, 1);
  info->base.refcount = 1;
  info->base.name = g_strdup (name);
  info->base.type = INFO_TYPE_INTERFACE;
  
  return info;
}

InterfaceInfo *
interface_info_ref (InterfaceInfo *info)
{
  info->base.refcount += 1;

  return info;
}

void
interface_info_unref (InterfaceInfo *info)
{
  info->base.refcount -= 1;
  if (info->base.refcount == 0)
    {
      free_method_list (&info->methods);
      free_signal_list (&info->signals);
      base_info_free (info);
    }
}

const char*
interface_info_get_name (InterfaceInfo *info)
{
  return info->base.name;
}

GSList*
interface_info_get_methods (InterfaceInfo *info)
{
  return info->methods;
}

GSList*
interface_info_get_signals (InterfaceInfo *info)
{
  return info->signals;
}

void
interface_info_add_method (InterfaceInfo *info,
                           MethodInfo    *method)
{
  method_info_ref (method);
  info->methods = g_slist_append (info->methods, method);
}

void
interface_info_add_signal (InterfaceInfo *info,
                           SignalInfo    *signal)
{
  signal_info_ref (signal);
  info->signals = g_slist_append (info->signals, signal);
}

static void
free_arg_list (GSList **args_p)
{
  GSList *tmp;
  tmp = *args_p;
  while (tmp != NULL)
    {
      arg_info_unref (tmp->data);
      tmp = tmp->next;
    }
  g_slist_free (*args_p);
  *args_p = NULL;
}

MethodInfo*
method_info_new (const char *name)
{
  MethodInfo *info;

  info = g_new0 (MethodInfo, 1);
  info->base.refcount = 1;
  info->base.name = g_strdup (name);
  info->base.type = INFO_TYPE_METHOD;
  
  return info;
}

MethodInfo *
method_info_ref (MethodInfo *info)
{
  info->base.refcount += 1;

  return info;
}

void
method_info_unref (MethodInfo *info)
{
  info->base.refcount -= 1;
  if (info->base.refcount == 0)
    {
      free_arg_list (&info->args);
      base_info_free (info);
    }
}

const char*
method_info_get_name (MethodInfo *info)
{
  return info->base.name;
}

GSList*
method_info_get_args (MethodInfo *info)
{
  return info->args;
}

static int
args_sort_by_direction (const void *a,
                        const void *b)
{
  const ArgInfo *arg_a = a;
  const ArgInfo *arg_b = b;

  if (arg_a->direction == arg_b->direction)
    return 0;
  else if (arg_a->direction == ARG_IN)
    return -1; /* in is less than out */
  else
    return 1;
}                  

void
method_info_add_arg (MethodInfo    *info,
                     ArgInfo       *arg)
{
  arg_info_ref (arg);
  info->args = g_slist_append (info->args, arg);

  /* Keep "in" args sorted before "out" and otherwise maintain
   * stable order (g_slist_sort is stable, at least in sufficiently
   * new glib)
   */
  info->args = g_slist_sort (info->args, args_sort_by_direction);
}

SignalInfo*
signal_info_new (const char *name)
{
  SignalInfo *info;

  info = g_new0 (SignalInfo, 1);
  info->base.refcount = 1;
  info->base.name = g_strdup (name);
  info->base.type = INFO_TYPE_SIGNAL;
  
  return info;
}

SignalInfo *
signal_info_ref (SignalInfo *info)
{
  info->base.refcount += 1;

  return info;
}

void
signal_info_unref (SignalInfo *info)
{
  info->base.refcount -= 1;
  if (info->base.refcount == 0)
    {
      free_arg_list (&info->args);
      base_info_free (info);
    }
}

const char*
signal_info_get_name (SignalInfo *info)
{
  return info->base.name;
}

GSList*
signal_info_get_args (SignalInfo *info)
{
  return info->args;
}

void
signal_info_add_arg (SignalInfo    *info,
                     ArgInfo       *arg)
{
  g_assert (arg->direction == ARG_OUT);
  
  arg_info_ref (arg);
  info->args = g_slist_append (info->args, arg);

  /* signal args don't need sorting since only "out" is allowed */
}

ArgInfo*
arg_info_new (const char  *name,
              ArgDirection direction,
              int          type)
{
  ArgInfo *info;

  info = g_new0 (ArgInfo, 1);
  info->base.refcount = 1;
  info->base.type = INFO_TYPE_ARG;
  
  /* name can be NULL */
  info->base.name = g_strdup (name);
  info->direction = direction;
  info->type = type;

  return info;
}

ArgInfo *
arg_info_ref (ArgInfo *info)
{
  info->base.refcount += 1;

  return info;
}

void
arg_info_unref (ArgInfo *info)
{
  info->base.refcount -= 1;
  if (info->base.refcount == 0)
    {
      base_info_free (info);
    }
}
const char*
arg_info_get_name (ArgInfo *info)
{
  return info->base.name;
}

int
arg_info_get_type (ArgInfo *info)
{
  return info->type;
}

ArgDirection
arg_info_get_direction (ArgInfo *info)
{
  return info->direction;
}

#ifdef DBUS_BUILD_TESTS

/**
 * @ingroup DBusGIDL
 * Unit test for GLib IDL internals
 * @returns #TRUE on success.
 */
gboolean
_dbus_gidl_test (void)
{

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
