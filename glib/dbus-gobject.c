/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gobject.c Exporting a GObject remotely
 *
 * Copyright (C) 2003, 2004 Red Hat, Inc.
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
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include "dbus-gtest.h"
#include "dbus-gutils.h"
#include "dbus-gvalue.h"
#include <string.h>

/**
 * @addtogroup DBusGLibInternals
 * @{
 */

static GStaticMutex info_hash_mutex = G_STATIC_MUTEX_INIT;
static GHashTable *info_hash = NULL;

static char*
wincaps_to_uscore (const char *caps)
{
  const char *p;
  GString *str;

  str = g_string_new (NULL);
  p = caps;
  while (*p)
    {
      if (g_ascii_isupper (*p))
        {
          if (str->len > 0 &&
              (str->len < 2 || str->str[str->len-2] != '_'))
            g_string_append_c (str, '_');
          g_string_append_c (str, g_ascii_tolower (*p));
        }
      else
        {
          g_string_append_c (str, *p);
        }
      ++p;
    }

  return g_string_free (str, FALSE);
}

static char*
uscore_to_wincaps (const char *uscore)
{
  const char *p;
  GString *str;
  gboolean last_was_uscore;

  last_was_uscore = TRUE;
  
  str = g_string_new (NULL);
  p = uscore;
  while (*p)
    {
      if (*p == '-' || *p == '_')
        {
          last_was_uscore = TRUE;
        }
      else
        {
          if (last_was_uscore)
            {
              g_string_append_c (str, g_ascii_toupper (*p));
              last_was_uscore = FALSE;
            }
          else
            g_string_append_c (str, *p);
        }
      ++p;
    }

  return g_string_free (str, FALSE);
}

static void
gobject_unregister_function (DBusConnection  *connection,
                             void            *user_data)
{
  GObject *object;

  object = G_OBJECT (user_data);

  /* FIXME */

}

static int
gtype_to_dbus_type (GType type)
{
  switch (type)
    {
    case G_TYPE_CHAR:
    case G_TYPE_UCHAR:
      return DBUS_TYPE_BYTE;
      
    case G_TYPE_BOOLEAN:
      return DBUS_TYPE_BOOLEAN;

      /* long gets cut to 32 bits so the remote API is consistent
       * on all architectures
       */
      
    case G_TYPE_LONG:
    case G_TYPE_INT:
      return DBUS_TYPE_INT32;
    case G_TYPE_ULONG:
    case G_TYPE_UINT:
      return DBUS_TYPE_UINT32;

    case G_TYPE_INT64:
      return DBUS_TYPE_INT64;

    case G_TYPE_UINT64:
      return DBUS_TYPE_UINT64;
      
    case G_TYPE_FLOAT:
    case G_TYPE_DOUBLE:
      return DBUS_TYPE_DOUBLE;

    case G_TYPE_STRING:
      return DBUS_TYPE_STRING;

    default:
      return DBUS_TYPE_INVALID;
    }
}

static void
introspect_properties (GObject *object, GString *xml)
{
  unsigned int i;
  unsigned int n_specs;
  GType last_type;
  GParamSpec **specs;

  last_type = G_TYPE_INVALID;
  specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (object),
                                          &n_specs);

  for (i = 0; i < n_specs; i++ )
    {
      char *s;
      int dbus_type;
      gboolean can_set;
      gboolean can_get;
      GParamSpec *spec = specs[i];
      
      dbus_type = gtype_to_dbus_type (G_PARAM_SPEC_VALUE_TYPE (spec));
      if (dbus_type == DBUS_TYPE_INVALID)
	continue;
      
      if (spec->owner_type != last_type)
	{
          if (last_type != G_TYPE_INVALID)
            g_string_append (xml, "  </interface>\n");


          /* FIXME what should the namespace on the interface be in
           * general?  should people be able to set it for their
           * objects?
           */
          g_string_append (xml, "  <interface name=\"org.gtk.objects.");
          g_string_append (xml, g_type_name (spec->owner_type));
          g_string_append (xml, "\">\n");

          last_type = spec->owner_type;
	}

      can_set = ((spec->flags & G_PARAM_WRITABLE) != 0 &&
		 (spec->flags & G_PARAM_CONSTRUCT_ONLY) == 0);
      
      can_get = (spec->flags & G_PARAM_READABLE) != 0;

      s = uscore_to_wincaps (spec->name);
      
      if (can_set)
        {
          g_string_append (xml, "    <method name=\"set_");
          g_string_append (xml, s);
          g_string_append (xml, "\">\n");
          
          g_string_append (xml, "      <arg type=\"");
          g_string_append (xml, _dbus_gutils_type_to_string (dbus_type));
          g_string_append (xml, "\"/>\n");
        }

      if (can_get)
        {
          g_string_append (xml, "    <method name=\"get_");
          g_string_append (xml, s);
          g_string_append (xml, "\">\n");
          
          g_string_append (xml, "      <arg type=\"");
          g_string_append (xml, _dbus_gutils_type_to_string (dbus_type));
          g_string_append (xml, "\" direction=\"out\"/>\n");
        }

      g_free (s);
    }

  if (last_type != G_TYPE_INVALID)
    g_string_append (xml, "  </interface>\n");

  g_free (specs);
}

static void
introspect_signals (GType type, GString *xml)
{
  guint i;
  guint *ids, n_ids;

  ids = g_signal_list_ids (type, &n_ids);
  if (!n_ids)
    return;

  g_string_append (xml, "  <interface name=\"org.gtk.objects.");
  g_string_append (xml, g_type_name (type));
  g_string_append (xml, "\">\n");

  /* FIXME: recurse to parent types ? */
  for (i = 0; i < n_ids; i++)
    {
      guint arg;
      GSignalQuery query;
      
      g_signal_query (ids[i], &query);

      if (query.return_type)
	continue; /* FIXME: these could be listed as methods ? */

      g_string_append (xml, "    <signal name=\"");
      g_string_append (xml, query.signal_name);
      g_string_append (xml, "\">\n");

      for (arg = 0; arg < query.n_params; arg++)
	{
	  int dbus_type = gtype_to_dbus_type (query.param_types[arg]);

          g_string_append (xml, "      <arg type=\"");
          g_string_append (xml, _dbus_gutils_type_to_string (dbus_type));
          g_string_append (xml, "\"/>\n");
	}

      g_string_append (xml, "    </signal>\n");
    }

  g_string_append (xml, "  </interface>\n");
}

static DBusHandlerResult
handle_introspect (DBusConnection *connection,
                   DBusMessage    *message,
                   GObject        *object)
{
  GString *xml;
  unsigned int i;
  DBusMessage *ret;
  char **children;
  
  if (!dbus_connection_list_registered (connection, 
                                        dbus_message_get_path (message),
                                        &children))
    g_error ("Out of memory");
  
  xml = g_string_new (NULL);

  introspect_signals (G_OBJECT_TYPE (object), xml);
  introspect_properties (object, xml);
	  
  g_string_append (xml, "<node>\n");

  /* Append child nodes */
  for (i = 0; children[i]; i++)
      g_string_append_printf (xml, "  <node name=\"%s\"/>\n",
                              children[i]);
  
  /* Close the XML, and send it to the requesting app */
  g_string_append (xml, "</node>\n");

  ret = dbus_message_new_method_return (message);
  if (ret == NULL)
    g_error ("Out of memory");

  dbus_message_append_args (message,
                            DBUS_TYPE_STRING, xml->str,
                            DBUS_TYPE_INVALID);

  dbus_connection_send (connection, message, NULL);
  dbus_message_unref (message);

  g_string_free (xml, TRUE);

  dbus_free_string_array (children);
  
  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusMessage*
set_object_property (DBusConnection *connection,
                     DBusMessage    *message,
                     GObject        *object,
                     GParamSpec     *pspec)
{
  GValue value = { 0, };
  DBusMessage *ret;
  DBusMessageIter iter;

  dbus_message_iter_init (message, &iter);

  /* The g_object_set_property() will transform some types, e.g. it
   * will let you use a uchar to set an int property etc. Note that
   * any error in value range or value conversion will just
   * g_warning(). These GObject skels are not for secure applications.
   */
  if (dbus_gvalue_demarshal (&iter, &value))
    {
      g_object_set_property (object,
                             pspec->name,
                             &value);

      g_value_unset (&value);

      ret = dbus_message_new_method_return (message);
      if (ret == NULL)
        g_error ("out of memory");
    }
  else
    {
      ret = dbus_message_new_error (message,
                                    DBUS_ERROR_INVALID_ARGS,
                                    "Argument's D-BUS type can't be converted to a GType");
      if (ret == NULL)
        g_error ("out of memory");
    }

  return ret;
}

static DBusMessage*
get_object_property (DBusConnection *connection,
                     DBusMessage    *message,
                     GObject        *object,
                     GParamSpec     *pspec)
{
  GType value_type;
  GValue value;
  DBusMessage *ret;
  DBusMessageIter iter;

  value_type = G_PARAM_SPEC_VALUE_TYPE (pspec);

  ret = dbus_message_new_method_return (message);
  if (ret == NULL)
    g_error ("out of memory");

  g_value_init (&value, value_type);
  g_object_get_property (object, pspec->name, &value);

  value_type = G_VALUE_TYPE (&value);

  dbus_message_append_iter_init (message, &iter);

  if (!dbus_gvalue_marshal (&iter, &value))
    {
      dbus_message_unref (ret);
      ret = dbus_message_new_error (message,
                                    DBUS_ERROR_UNKNOWN_METHOD,
                                    "Can't convert GType of object property to a D-BUS type");
    }

  return ret;
}

static DBusHandlerResult
gobject_message_function (DBusConnection  *connection,
                          DBusMessage     *message,
                          void            *user_data)
{
  const DBusGObjectInfo *info;
  GParamSpec *pspec;
  GObject *object;
  const char *member;
  gboolean setter;
  gboolean getter;
  char *s;

  object = G_OBJECT (user_data);

  if (dbus_message_is_method_call (message,
                                   DBUS_INTERFACE_ORG_FREEDESKTOP_INTROSPECTABLE,
                                   "Introspect"))
    return handle_introspect (connection, message, object);

  member = dbus_message_get_member (message);

  /* Try the metainfo, which lets us invoke methods */

  g_static_mutex_lock (&info_hash_mutex);
  /* FIXME this needs to walk up the inheritance tree, not
   * just look at the most-derived class
   */
  info = g_hash_table_lookup (info_hash,
                              G_OBJECT_GET_CLASS (object));
  g_static_mutex_unlock (&info_hash_mutex);

  if (info != NULL)
    {



    }

  /* If no metainfo, we can still do properties and signals
   * via standard GLib introspection
   */
  setter = (member[0] == 's' && member[1] == 'e' && member[2] == 't' && member[3] == '_');
  getter = (member[0] == 'g' && member[1] == 'e' && member[2] == 't' && member[3] == '_');

  if (!(setter || getter))
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  s = wincaps_to_uscore (&member[4]);

  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object),
                                        s);

  g_free (s);

  if (pspec != NULL)
    {
      DBusMessage *ret;

      if (setter)
        {
          ret = set_object_property (connection, message,
                                     object, pspec);
        }
      else if (getter)
        {
          ret = get_object_property (connection, message,
                                     object, pspec);
        }
      else
        {
          g_assert_not_reached ();
          ret = NULL;
        }

      g_assert (ret != NULL);

      dbus_connection_send (connection, ret, NULL);
      dbus_message_unref (ret);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusObjectPathVTable gobject_dbus_vtable = {
  gobject_unregister_function,
  gobject_message_function,
  NULL
};

/** @} */ /* end of internals */

/**
 * @addtogroup DBusGLib
 * @{
 */

/**
 * Install introspection information about the given object class
 * sufficient to allow methods on the object to be invoked by name.
 * The introspection information is normally generated by
 * dbus-glib-tool, then this function is called in the
 * class_init() for the object class.
 *
 * Once introspection information has been installed, instances of the
 * object registered with dbus_g_connection_register_g_object() can have
 * their methods invoked remotely.
 *
 * @param object_class class struct of the object
 * @param info introspection data generated by dbus-glib-tool
 */
void
dbus_g_object_class_install_info (GObjectClass          *object_class,
                                  const DBusGObjectInfo *info)
{
  g_return_if_fail (G_IS_OBJECT_CLASS (object_class));

  g_static_mutex_lock (&info_hash_mutex);

  if (info_hash == NULL)
    {
      info_hash = g_hash_table_new (NULL, NULL); /* direct hash */
    }

  g_hash_table_replace (info_hash, object_class, (void*) info);

  g_static_mutex_unlock (&info_hash_mutex);
}

/**
 * Registers a GObject at the given path. Properties, methods, and signals
 * of the object can then be accessed remotely. Methods are only available
 * if method introspection data has been added to the object's class
 * with g_object_class_install_info().
 *
 * The registration will be cancelled if either the DBusConnection or
 * the GObject gets finalized.
 *
 * @param connection the D-BUS connection
 * @param at_path the path where the object will live (the object's name)
 * @param object the object
 */
void
dbus_g_connection_register_g_object (DBusGConnection       *connection,
                                     const char            *at_path,
                                     GObject               *object)
{
  g_return_if_fail (connection != NULL);
  g_return_if_fail (at_path != NULL);
  g_return_if_fail (G_IS_OBJECT (object));

  if (!dbus_connection_register_object_path (DBUS_CONNECTION_FROM_G_CONNECTION (connection),
                                             at_path,
                                             &gobject_dbus_vtable,
                                             object))
    g_error ("Failed to register GObject with DBusConnection");

  /* FIXME set up memory management (so we break the
   * registration if object or connection vanishes)
   */
}

/** @} */ /* end of public API */

#ifdef DBUS_BUILD_TESTS
#include <stdlib.h>

/**
 * @ingroup DBusGLibInternals
 * Unit test for GLib GObject integration ("skeletons")
 * @returns #TRUE on success.
 */
gboolean
_dbus_gobject_test (const char *test_data_dir)
{
  int i;
  static struct { const char *wincaps; const char *uscore; } name_pairs[] = {
    { "SetFoo", "set_foo" },
    { "Foo", "foo" },
    { "GetFooBar", "get_foo_bar" },
    { "Hello", "hello" }
    
    /* Impossible-to-handle cases */
    /* { "FrobateUIHandler", "frobate_ui_handler" } */
  };

  i = 0;
  while (i < (int) G_N_ELEMENTS (name_pairs))
    {
      char *uscore;
      char *wincaps;

      uscore = wincaps_to_uscore (name_pairs[i].wincaps);
      wincaps = uscore_to_wincaps (name_pairs[i].uscore);

      if (strcmp (uscore, name_pairs[i].uscore) != 0)
        {
          g_printerr ("\"%s\" should have been converted to \"%s\" not \"%s\"\n",
                      name_pairs[i].wincaps, name_pairs[i].uscore,
                      uscore);
          exit (1);
        }
      
      if (strcmp (wincaps, name_pairs[i].wincaps) != 0)
        {
          g_printerr ("\"%s\" should have been converted to \"%s\" not \"%s\"\n",
                      name_pairs[i].uscore, name_pairs[i].wincaps,
                      wincaps);
          exit (1);
        }
      
      g_free (uscore);
      g_free (wincaps);

      ++i;
    }
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
