/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gidl.c data structure describing an interface, to be generated from IDL
 *             or something
 *
 * Copyright (C) 2003  Red Hat, Inc.
 *
 * Licensed under the Academic Free License version 1.2
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

struct InterfaceInfo
{
  int refcount;
  char *name;
  GSList *methods;
  GSList *signals;
};

struct MethodInfo
{
  int refcount;
  GSList *args;
  char *name;
};

struct SignalInfo
{
  int refcount;
  GSList *args;
  char *name;
};

struct ArgInfo
{
  int refcount;
  char *name;
  int type;
  ArgDirection direction;
};

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

InterfaceInfo*
interface_info_new (const char *name)
{
  InterfaceInfo *info;

  info = g_new0 (InterfaceInfo, 1);
  info->refcount = 1;
  info->name = g_strdup (name);
  
  return info;
}

void
interface_info_ref (InterfaceInfo *info)
{
  info->refcount += 1;
}

void
interface_info_unref (InterfaceInfo *info)
{
  info->refcount -= 1;
  if (info->refcount == 0)
    {
      free_method_list (&info->methods);
      free_signal_list (&info->signals);
      g_free (info->name);
      g_free (info);
    }
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
  info->refcount = 1;
  info->name = g_strdup (name);
  
  return info;
}

void
method_info_ref (MethodInfo *info)
{
  info->refcount += 1;
}

void
method_info_unref (MethodInfo *info)
{
  info->refcount -= 1;
  if (info->refcount == 0)
    {
      free_arg_list (&info->args);
      g_free (info->name);
      g_free (info);
    }
}

const char*
method_info_get_name (MethodInfo *info)
{
  return info->name;
}

GSList*
method_info_get_args (MethodInfo *info)
{
  return info->args;
}

MethodStyle
method_info_get_style (MethodInfo *info)
{
  return info->style;
}

void
method_info_add_arg (MethodInfo    *info,
                     ArgInfo       *arg)
{
  arg_info_ref (arg);
  info->args = g_slist_append (info->args, arg);
}

SignalInfo*
signal_info_new (const char *name)
{
  SignalInfo *info;

  info = g_new0 (SignalInfo, 1);
  info->refcount = 1;
  info->name = g_strdup (name);
  
  return info;
}

void
signal_info_ref (SignalInfo *info)
{
  info->refcount += 1;
}

void
signal_info_unref (SignalInfo *info)
{
  info->refcount -= 1;
  if (info->refcount == 0)
    {
      free_arg_list (&info->args);
      g_free (info->name);
      g_free (info);
    }
}

const char*
signal_info_get_name (SignalInfo *info)
{
  return info->name;
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
  arg_info_ref (arg);
  info->args = g_slist_append (info->args, arg);
}

ArgInfo*
arg_info_new (const char  *name,
              ArgDirection direction,
              int          type)
{
  ArgInfo *info;

  info = g_new0 (ArgInfo, 1);
  info->refcount = 1;
  info->name = g_strdup (name);
  info->direction = direction;
  info->type = type;
  
  return info;
}

void
arg_info_ref (ArgInfo *info)
{
  info->refcount += 1;
}

void
arg_info_unref (ArgInfo *info)
{
  info->refcount -= 1;
  if (info->refcount == 0)
    {
      g_free (info->name);
      g_free (info);
    }
}
const char*
arg_info_get_name (ArgInfo *info)
{
  return info->name;
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
dbus_bool_t
_dbus_gidl_test (void)
{

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
