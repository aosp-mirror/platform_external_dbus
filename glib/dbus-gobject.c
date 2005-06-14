/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gobject.c Exporting a GObject remotely
 *
 * Copyright (C) 2003, 2004, 2005 Red Hat, Inc.
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
#include <gobject/gvaluecollector.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include "dbus-gtest.h"
#include "dbus-gutils.h"
#include "dbus-gobject.h"
#include "dbus-gvalue.h"
#include "dbus-gmarshal.h"
#include "dbus-gvalue-utils.h"
#include <string.h>

/**
 * @addtogroup DBusGLibInternals
 * @{
 */

static GStaticRWLock globals_lock = G_STATIC_RW_LOCK_INIT;
static GHashTable *info_hash = NULL;
static GHashTable *marshal_table = NULL;

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

static const char *
string_table_next (const char *table)
{
  return (table + (strlen (table) + 1));
}

static const char *
string_table_lookup (const char *table, int index)
{
  const char *ret;

  ret = table;

  while (index--)
    ret = string_table_next (ret);

  return ret;
}

static const char *
get_method_data (const DBusGObjectInfo *object,
		 const DBusGMethodInfo *method)
{
  return object->data + method->data_offset;
}

static char *
object_error_domain_prefix_from_object_info (const DBusGObjectInfo *info)
{
  /* FIXME */
  return NULL;
}

static char *
object_error_code_from_object_info (const DBusGObjectInfo *info, GQuark domain, gint code)
{
  /* FIXME */
  return NULL;
}

static const char *
method_interface_from_object_info (const DBusGObjectInfo *object,
			      const DBusGMethodInfo *method)
{
  return string_table_lookup (get_method_data (object, method), 0);
}

static const char *
method_name_from_object_info (const DBusGObjectInfo *object,
			      const DBusGMethodInfo *method)
{
  return string_table_lookup (get_method_data (object, method), 1);
}

static const char *
method_arg_info_from_object_info (const DBusGObjectInfo *object,
				  const DBusGMethodInfo *method)
{
  return string_table_lookup (get_method_data (object, method), 3);/*RB was 2*/
}

static const char *
arg_iterate (const char *data, const char **name, gboolean *in,
	     const char **type)
{
  *name = data;

  data = string_table_next (data);
  switch (*data)
    {
    case 'I':
      *in = TRUE;
      break;
    case 'O':
      *in = FALSE;
      break;
    default:
      g_warning ("invalid arg direction");
      break;
    }
  
  data = string_table_next (data);
  *type = data;

  return string_table_next (data);
}

static char *
method_dir_signature_from_object_info (const DBusGObjectInfo *object,
				       const DBusGMethodInfo *method,
				       gboolean               in)
{
  const char *arg;
  GString *ret;

  arg = method_arg_info_from_object_info (object, method);

  ret = g_string_new (NULL);

  while (*arg)
    {
      const char *name;
      gboolean arg_in;
      const char *type;

      arg = arg_iterate (arg, &name, &arg_in, &type);

      if (arg_in == in)
	g_string_append (ret, type);
    }

  return g_string_free (ret, FALSE);
}

static char *
method_input_signature_from_object_info (const DBusGObjectInfo *object,
					 const DBusGMethodInfo *method)
{
  return method_dir_signature_from_object_info (object, method, TRUE);
}

static char *
method_output_signature_from_object_info (const DBusGObjectInfo *object,
					  const DBusGMethodInfo *method)
{
  return method_dir_signature_from_object_info (object, method, FALSE);
}

static void
gobject_unregister_function (DBusConnection  *connection,
                             void            *user_data)
{
  GObject *object;

  object = G_OBJECT (user_data);

  /* FIXME */

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
      const char *dbus_type;
      gboolean can_set;
      gboolean can_get;
      GParamSpec *spec = specs[i];
      
      dbus_type = dbus_gtype_to_signature (G_PARAM_SPEC_VALUE_TYPE (spec));
      if (dbus_type == NULL)
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
      
      if (can_set || can_get)
        {
          g_string_append (xml, "    <property name=\"");
          g_string_append (xml, s);
          g_string_append (xml, "\" type=\"");
          g_string_append (xml, dbus_type);
          g_string_append (xml, "\" access=\"");

          if (can_set && can_get)
            g_string_append (xml, "readwrite");
          else if (can_get)
            g_string_append (xml, "read");
          else
            {
              g_assert (can_set);
              g_string_append (xml, "write");
            }
          
          g_string_append (xml, "\"/>\n");
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

      if (query.return_type != G_TYPE_NONE)
	continue; /* FIXME: these could be listed as methods ? */

      g_string_append (xml, "    <signal name=\"");
      g_string_append (xml, query.signal_name);
      g_string_append (xml, "\">\n");

      for (arg = 0; arg < query.n_params; arg++)
	{
	  const char *dbus_type = dbus_gtype_to_signature (query.param_types[arg]);

	  if (!dbus_type)
	    continue;
	  
          g_string_append (xml, "      <arg type=\"");
          g_string_append (xml, dbus_type);
          g_string_append (xml, "\"/>\n");
	}

      g_string_append (xml, "    </signal>\n");
    }

  g_string_append (xml, "  </interface>\n");
}

typedef struct
{
  GString *xml;
  const DBusGObjectInfo *object_info;
} DBusGlibWriteIterfaceData;

static void
write_interface (gpointer key, gpointer val, gpointer user_data)
{
  const char *name;
  GSList *methods;
  GString *xml;
  const DBusGObjectInfo *object_info;
  DBusGlibWriteIterfaceData *data;

  name = key;
  methods = val;
  data = user_data;
  xml = data->xml;
  object_info = data->object_info;

  g_string_append_printf (xml, "  <interface name=\"%s\">\n", name);

  /* FIXME: recurse to parent types ? */
  for (; methods; methods = methods->next)
    {
      DBusGMethodInfo *method;
      method = methods->data;
      const char *args;

      g_string_append_printf (xml, "    <method name=\"%s\">\n",
			      method_name_from_object_info (object_info, method));

      args = method_arg_info_from_object_info (object_info, method);

      while (*args)
	{
	  const char *name;
	  gboolean arg_in;
	  const char *type;
	  
	  args = arg_iterate (args, &name, &arg_in, &type);

	  /* FIXME - handle container types */
	  g_string_append_printf (xml, "      <arg name=\"%s\" type=\"%s\" direction=\"%s\"/>\n",
				  name, type, arg_in ? "in" : "out");

	}
      g_string_append (xml, "    </method>\n");
    }

  g_string_append (xml, "  </interface>\n");
}

static void
introspect_interfaces (GObject *object, GString *xml)
{
  GType classtype;

  g_static_rw_lock_reader_lock (&globals_lock);

  for (classtype = G_TYPE_FROM_INSTANCE (object); classtype != 0; classtype = g_type_parent (classtype))
    {
      const DBusGObjectInfo *info;
      DBusGlibWriteIterfaceData data;

      info = g_hash_table_lookup (info_hash,
				  g_type_class_peek (classtype));

      if (info != NULL && info->format_version == 0)
	{
	  int i;
	  GHashTable *interfaces;

	  /* Gather a list of all interfaces, indexed into their methods */
	  interfaces = g_hash_table_new (g_str_hash, g_str_equal);
	  for (i = 0; i < info->n_infos; i++)
	    {
	      const char *method_name;
	      const char *method_interface;
	      const char *method_args;
	      const DBusGMethodInfo *method;
	      GSList *methods;

	      method = &(info->infos[i]);

	      method_interface = method_interface_from_object_info (info, method);
	      method_name = method_name_from_object_info (info, method);
	      method_args = method_arg_info_from_object_info (info, method);

	      if ((methods = g_hash_table_lookup (interfaces, method_interface)) == NULL)
		  methods = g_slist_prepend (NULL, (gpointer) method);
	      else
		  methods = g_slist_prepend (methods, (gpointer) method);
	      g_hash_table_insert (interfaces, (gpointer) method_interface, methods);
	    }

	  memset (&data, 0, sizeof (data));
	  data.xml = xml;
	  data.object_info = info;
	  g_hash_table_foreach (interfaces, write_interface, &data);

	  g_hash_table_destroy (interfaces);
	}
    }

  g_static_rw_lock_reader_unlock (&globals_lock);
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

  g_string_append (xml, DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);
  
  g_string_append (xml, "<node>\n");

  /* We are introspectable, though I guess that was pretty obvious */
  g_string_append_printf (xml, "  <interface name=\"%s\">\n", DBUS_INTERFACE_INTROSPECTABLE);
  g_string_append (xml, "    <method name=\"Introspect\">\n");
  g_string_append_printf (xml, "      <arg name=\"data\" direction=\"out\" type=\"%s\"/>\n", DBUS_TYPE_STRING_AS_STRING);
  g_string_append (xml, "    </method>\n");
  g_string_append (xml, "  </interface>\n");

  /* We support get/set properties */
  g_string_append_printf (xml, "  <interface name=\"%s\">\n", DBUS_INTERFACE_PROPERTIES);
  g_string_append (xml, "    <method name=\"Get\">\n");
  g_string_append_printf (xml, "      <arg name=\"interface\" direction=\"in\" type=\"%s\"/>\n", DBUS_TYPE_STRING_AS_STRING);
  g_string_append_printf (xml, "      <arg name=\"propname\" direction=\"in\" type=\"%s\"/>\n", DBUS_TYPE_STRING_AS_STRING);
  g_string_append_printf (xml, "      <arg name=\"value\" direction=\"out\" type=\"%s\"/>\n", DBUS_TYPE_VARIANT_AS_STRING);
  g_string_append (xml, "    </method>\n");
  g_string_append (xml, "    <method name=\"Set\">\n");
  g_string_append_printf (xml, "      <arg name=\"interface\" direction=\"in\" type=\"%s\"/>\n", DBUS_TYPE_STRING_AS_STRING);
  g_string_append_printf (xml, "      <arg name=\"propname\" direction=\"in\" type=\"%s\"/>\n", DBUS_TYPE_STRING_AS_STRING);
  g_string_append_printf (xml, "      <arg name=\"value\" direction=\"in\" type=\"%s\"/>\n", DBUS_TYPE_VARIANT_AS_STRING);
  g_string_append (xml, "    </method>\n");
  g_string_append (xml, "  </interface>\n");
  
  introspect_signals (G_OBJECT_TYPE (object), xml);
  introspect_properties (object, xml);
  introspect_interfaces (object, xml);

  /* Append child nodes */
  for (i = 0; children[i]; i++)
      g_string_append_printf (xml, "  <node name=\"%s\"/>\n",
                              children[i]);
  
  /* Close the XML, and send it to the requesting app */
  g_string_append (xml, "</node>\n");

  ret = dbus_message_new_method_return (message);
  if (ret == NULL)
    g_error ("Out of memory");

  dbus_message_append_args (ret,
                            DBUS_TYPE_STRING, &xml->str,
                            DBUS_TYPE_INVALID);

  dbus_connection_send (connection, ret, NULL);
  dbus_message_unref (ret);

  g_string_free (xml, TRUE);

  dbus_free_string_array (children);
  
  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusMessage*
set_object_property (DBusConnection  *connection,
                     DBusMessage     *message,
                     DBusMessageIter *iter,
                     GObject         *object,
                     GParamSpec      *pspec)
{
  GValue value = { 0, };
  DBusMessage *ret;
  DBusMessageIter sub;
  DBusGValueMarshalCtx context;

  dbus_message_iter_recurse (iter, &sub);

  context.gconnection = DBUS_G_CONNECTION_FROM_CONNECTION (connection);
  context.proxy = NULL;

  g_value_init (&value, pspec->value_type);
  if (dbus_gvalue_demarshal (&context, &sub, &value, NULL))
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
  GValue value = {0, };
  DBusMessage *ret;
  DBusMessageIter iter;

  value_type = G_PARAM_SPEC_VALUE_TYPE (pspec);

  ret = dbus_message_new_method_return (message);
  if (ret == NULL)
    g_error ("out of memory");

  g_value_init (&value, value_type);
  g_object_get_property (object, pspec->name, &value);

  value_type = G_VALUE_TYPE (&value);

  dbus_message_iter_init_append (message, &iter);

  if (!dbus_gvalue_marshal (&iter, &value))
    {
      dbus_message_unref (ret);
      ret = dbus_message_new_error (message,
                                    DBUS_ERROR_UNKNOWN_METHOD,
                                    "Can't convert GType of object property to a D-BUS type");
    }

  return ret;
}

static gboolean
lookup_object_and_method (GObject      *object,
			  DBusMessage  *message,
			  const DBusGObjectInfo **object_ret,
			  const DBusGMethodInfo **method_ret)
{
  GType classtype;
  const char *interface;
  const char *member;
  const char *signature;
  gboolean ret;

  interface = dbus_message_get_interface (message);
  member = dbus_message_get_member (message);
  signature = dbus_message_get_signature (message);
  ret = FALSE;

  g_static_rw_lock_reader_lock (&globals_lock);

  if (!info_hash)
    goto out;
  
  for (classtype = G_TYPE_FROM_INSTANCE (object); classtype != 0; classtype = g_type_parent (classtype))
    {
      const DBusGObjectInfo *info;

      info = g_hash_table_lookup (info_hash,
				  g_type_class_peek (classtype));

      *object_ret = info;

      if (info != NULL && info->format_version == 0)
	{
	  int i;
	  for (i = 0; i < info->n_infos; i++)
	    {
	      const char *expected_member;
	      const char *expected_interface;
	      char *expected_signature;
	      const DBusGMethodInfo *method;

	      method = &(info->infos[i]);

	      /* Check method interface/name and input signature */ 
	      expected_interface = method_interface_from_object_info (*object_ret, method);
	      expected_member = method_name_from_object_info (*object_ret, method);
	      expected_signature = method_input_signature_from_object_info (*object_ret, method);

	      if ((interface == NULL
		   || strcmp (expected_interface, interface) == 0)
		  && strcmp (expected_member, member) == 0
		  && strcmp (expected_signature, signature) == 0)
		{
		  g_free (expected_signature);
		  *method_ret = method;
		  ret = TRUE;
		  goto out;
		}
	      g_free (expected_signature);
	    }
	}
    }
 out:
  g_static_rw_lock_reader_unlock (&globals_lock);
  return ret;
}

static char *
gerror_domaincode_to_dbus_error_name (const DBusGObjectInfo *object_info,
				      GQuark domain, gint code)
{
  const char *domain_str;
  const char *code_str;
  GString *dbus_error_name;

  domain_str = object_error_domain_prefix_from_object_info (object_info);
  code_str = object_error_code_from_object_info (object_info, domain, code);

  if (!domain_str || !code_str)
    {
      /* If we can't map it sensibly, make up an error name */
      char *domain_from_quark;
      
      dbus_error_name = g_string_new ("org.freedesktop.DBus.GLib.UnmappedError.");

      domain_from_quark = uscore_to_wincaps (g_quark_to_string (domain));
      g_string_append (dbus_error_name, domain_from_quark);
      g_free (domain_from_quark);
	
      g_string_append_printf (dbus_error_name, ".Code%d", code);
    }
  else
    {
      dbus_error_name = g_string_new (domain_str);
      g_string_append_c (dbus_error_name, '.');
      g_string_append (dbus_error_name, code_str);
    }

  return g_string_free (dbus_error_name, FALSE);
}

static DBusMessage *
gerror_to_dbus_error_message (const DBusGObjectInfo *object_info,
			      DBusMessage     *message,
			      GError          *error)
{
  DBusMessage *reply;

  if (!error)
    {
      char *error_msg;
      
      error_msg = g_strdup_printf ("Method invoked for %s returned FALSE but did not set error", dbus_message_get_member (message));
      reply = dbus_message_new_error (message, "org.freedesktop.DBus.GLib.ErrorError", error_msg);
      g_free (error_msg);
    }
  else
    {
      char *error_name;
      error_name = gerror_domaincode_to_dbus_error_name (object_info, error->domain, error->code);
      reply = dbus_message_new_error (message, error_name, error->message);
      g_free (error_name); 
    }
  return reply;
}

/**
 * The context of an asynchronous method call.  See dbus_g_method_return() and
 * dbus_g_method_return_error().
 */
struct DBusGMethodInvocation {
  DBusGConnection *connection; /**< The connection */
  DBusGMessage *message; /**< The message which generated the method call */
  const DBusGObjectInfo *object; /**< The object the method was called on */
  const DBusGMethodInfo *method; /**< The method called */
};

static DBusHandlerResult
invoke_object_method (GObject         *object,
		      const DBusGObjectInfo *object_info,
		      const DBusGMethodInfo *method,
		      DBusConnection  *connection,
		      DBusMessage     *message)
{
  gboolean had_error, call_only;
  GError *gerror;
  GValueArray *value_array;
  GValue object_value = {0,};
  GValue error_value = {0,};
  GValue return_value = {0,};
  GClosure closure;
  char *in_signature;
  char *out_signature = NULL;
  int current_type;
  DBusSignatureIter out_signature_iter;
  GArray *out_param_values = NULL;
  GValueArray *out_param_gvalues = NULL;
  int out_param_count;
  int out_param_pos, out_param_gvalue_pos;
  DBusHandlerResult result;
  DBusMessage *reply;

  gerror = NULL;

  if (strcmp (string_table_lookup (get_method_data (object_info, method), 2), "A") == 0) {
    call_only = TRUE;
  } else {
    call_only = FALSE;
  }

  /* This is evil.  We do this to work around the fact that
   * the generated glib marshallers check a flag in the closure object
   * which we don't care about.  We don't need/want to create
   * a new closure for each invocation.
   */
  memset (&closure, 0, sizeof (closure));

  in_signature = method_input_signature_from_object_info (object_info, method); 
  
  /* Convert method IN parameters to GValueArray */
  {
    GArray *types_array;
    guint n_params;
    const GType *types;
    DBusGValueMarshalCtx context;
    GError *error = NULL;
    
    context.gconnection = DBUS_G_CONNECTION_FROM_CONNECTION (connection);
    context.proxy = NULL;

    types_array = dbus_gtypes_from_arg_signature (in_signature, FALSE);
    n_params = types_array->len;
    types = (const GType*) types_array->data;

    value_array = dbus_gvalue_demarshal_message (&context, message, n_params, types, &error);
    if (value_array == NULL)
      {
	g_free (in_signature); 
	g_array_free (types_array, TRUE);
	reply = dbus_message_new_error (message, "org.freedesktop.DBus.GLib.ErrorError", error->message);
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);
	g_error_free (error);
	return DBUS_HANDLER_RESULT_HANDLED;
      }
    g_array_free (types_array, TRUE);
  }

  /* Prepend object as first argument */ 
  g_value_init (&object_value, G_TYPE_OBJECT);
  g_value_set_object (&object_value, object);
  g_value_array_prepend (value_array, &object_value);

  if (call_only) {
    GValue context_value = {0,};
    DBusGMethodInvocation *context;
    context = g_new (DBusGMethodInvocation, 1);
    context->connection = dbus_g_connection_ref (DBUS_G_CONNECTION_FROM_CONNECTION (connection));
    context->message = dbus_g_message_ref (DBUS_G_MESSAGE_FROM_MESSAGE (message));
    context->object = object_info;
    context->method = method;
    g_value_init (&context_value, G_TYPE_POINTER);
    g_value_set_pointer (&context_value, context);
    g_value_array_append (value_array, &context_value);
  } else {
  out_signature = method_output_signature_from_object_info (object_info, method); 

  /* Count number of output parameters */
  dbus_signature_iter_init (&out_signature_iter, out_signature);
  out_param_count = 0;
  while ((current_type = dbus_signature_iter_get_current_type (&out_signature_iter)) != DBUS_TYPE_INVALID)
    {
      out_param_count++;
      dbus_signature_iter_next (&out_signature_iter);
    }

  /* Create an array to store the actual values of OUT
   * parameters.  Then, create a GValue boxed POINTER
   * to each of those values, and append to the invocation,
   * so the method can return the OUT parameters.
   */
  out_param_values = g_array_sized_new (FALSE, TRUE, sizeof (GTypeCValue), out_param_count);

  /* We have a special array of GValues for toplevel GValue return
   * types.
   */
  out_param_gvalues = g_value_array_new (out_param_count);
  out_param_pos = 0;
  out_param_gvalue_pos = 0;
  dbus_signature_iter_init (&out_signature_iter, out_signature);
  while ((current_type = dbus_signature_iter_get_current_type (&out_signature_iter)) != DBUS_TYPE_INVALID)
    {
      GValue value = {0, };
      GTypeCValue storage;

      g_value_init (&value, G_TYPE_POINTER);

      /* We special case variants to make method invocation a bit nicer */
      if (current_type != DBUS_TYPE_VARIANT)
	{
	  memset (&storage, 0, sizeof (storage));
	  g_array_append_val (out_param_values, storage);
	  g_value_set_pointer (&value, &(g_array_index (out_param_values, GTypeCValue, out_param_pos)));
	  out_param_pos++;
	}
      else
	{
	  g_value_array_append (out_param_gvalues, NULL);
	  g_value_set_pointer (&value, out_param_gvalues->values + out_param_gvalue_pos);
	  out_param_gvalue_pos++;
	}
      g_value_array_append (value_array, &value);
      dbus_signature_iter_next (&out_signature_iter);
    }

  /* Append GError as final argument */
  g_value_init (&error_value, G_TYPE_POINTER);
  g_value_set_pointer (&error_value, &gerror);
  g_value_array_append (value_array, &error_value);
  }
  /* Actually invoke method */
  g_value_init (&return_value, G_TYPE_BOOLEAN);
  method->marshaller (&closure, &return_value,
		      value_array->n_values,
		      value_array->values,
		      NULL, method->function);
  if (call_only) {
    result = DBUS_HANDLER_RESULT_HANDLED;
    goto done;
  }
  had_error = !g_value_get_boolean (&return_value);

  if (!had_error)
    {
      DBusMessageIter iter;

      reply = dbus_message_new_method_return (message);
      if (reply == NULL)
	goto nomem;

      /* Append OUT arguments to reply */
      dbus_message_iter_init_append (reply, &iter);
      dbus_signature_iter_init (&out_signature_iter, out_signature);
      out_param_pos = 0;
      out_param_gvalue_pos = 0;
      while ((current_type = dbus_signature_iter_get_current_type (&out_signature_iter)) != DBUS_TYPE_INVALID)
	{
	  GValue gvalue = {0, };
	  
	  g_value_init (&gvalue, dbus_gtype_from_signature_iter (&out_signature_iter, FALSE));
	  if (current_type != DBUS_TYPE_VARIANT)
	    {
	      if (!dbus_gvalue_take (&gvalue,
				     &(g_array_index (out_param_values, GTypeCValue, out_param_pos))))
		g_assert_not_reached ();
	      out_param_pos++;
	    }
	  else
	    {
	      g_value_set_static_boxed (&gvalue, out_param_gvalues->values + out_param_gvalue_pos);
	      out_param_gvalue_pos++;
	    }
	      
	  if (!dbus_gvalue_marshal (&iter, &gvalue))
	    goto nomem;
	  /* Here we actually free the allocated value; we
	   * took ownership of it with dbus_gvalue_take.
	   */
	  g_value_unset (&gvalue);
	  dbus_signature_iter_next (&out_signature_iter);
	}
    }
  else
    reply = gerror_to_dbus_error_message (object_info, message, gerror);

  if (reply)
    {
      dbus_connection_send (connection, reply, NULL);
      dbus_message_unref (reply);
    }

  result = DBUS_HANDLER_RESULT_HANDLED;
 done:
  g_free (in_signature);
  g_free (out_signature);
  if (!call_only) {
    g_array_free (out_param_values, TRUE);
    g_value_array_free (out_param_gvalues);
    g_value_unset (&object_value);
    g_value_unset (&error_value);
  }
  g_value_array_free (value_array);
  g_value_unset (&return_value);
  return result;
 nomem:
  result = DBUS_HANDLER_RESULT_NEED_MEMORY;
  goto done;
}

static DBusHandlerResult
gobject_message_function (DBusConnection  *connection,
                          DBusMessage     *message,
                          void            *user_data)
{
  GParamSpec *pspec;
  GObject *object;
  gboolean setter;
  gboolean getter;
  char *s;
  const char *wincaps_propname;
  /* const char *wincaps_propiface; */
  DBusMessageIter iter;
  const DBusGMethodInfo *method;
  const DBusGObjectInfo *object_info;

  object = G_OBJECT (user_data);

  if (dbus_message_is_method_call (message,
                                   DBUS_INTERFACE_INTROSPECTABLE,
                                   "Introspect"))
    return handle_introspect (connection, message, object);
  
  /* Try the metainfo, which lets us invoke methods */
  if (lookup_object_and_method (object, message, &object_info, &method))
    return invoke_object_method (object, object_info, method, connection, message);

  /* If no metainfo, we can still do properties and signals
   * via standard GLib introspection
   */
  getter = FALSE;
  setter = FALSE;
  if (dbus_message_is_method_call (message,
                                   DBUS_INTERFACE_PROPERTIES,
                                   "Get"))
    getter = TRUE;
  else if (dbus_message_is_method_call (message,
                                        DBUS_INTERFACE_PROPERTIES,
                                        "Set"))
    setter = TRUE;

  if (!(setter || getter))
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  dbus_message_iter_init (message, &iter);

  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_STRING)
    {
      g_warning ("Property get or set does not have an interface string as first arg\n");
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
  /* We never use the interface name; if we did, we'd need to
   * remember that it can be empty string for "pick one for me"
   */
  /* dbus_message_iter_get_basic (&iter, &wincaps_propiface); */
  dbus_message_iter_next (&iter);

  if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_STRING)
    {
      g_warning ("Property get or set does not have a property name string as second arg\n");
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
  dbus_message_iter_get_basic (&iter, &wincaps_propname);
  dbus_message_iter_next (&iter);
  
  s = _dbus_gutils_wincaps_to_uscore (wincaps_propname);

  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object),
                                        s);

  g_free (s);

  if (pspec != NULL)
    {
      DBusMessage *ret;

      if (setter)
        {
          if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_VARIANT)
            {
              g_warning ("Property set does not have a variant value as third arg\n");
              return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }
          
          ret = set_object_property (connection, message, &iter,
                                     object, pspec);
          dbus_message_iter_next (&iter);
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

      if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_INVALID)
        g_warning ("Property get or set had too many arguments\n");
      
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

typedef struct {
  GClosure         closure;
  DBusGConnection *connection;
  GObject         *object;
  char            *signame;
} DBusGSignalClosure;

static GClosure *
dbus_g_signal_closure_new (DBusGConnection *connection,
			   GObject         *object,
			   const char      *signame)
{
  DBusGSignalClosure *closure;
  
  closure = (DBusGSignalClosure*) g_closure_new_simple (sizeof (DBusGSignalClosure), NULL);

  closure->connection = dbus_g_connection_ref (connection);
  closure->object = object;
  closure->signame = g_strdup (signame);
  return (GClosure*) closure;
}

static void
dbus_g_signal_closure_finalize (gpointer data,
				GClosure *closure)
{
  DBusGSignalClosure *sigclosure = (DBusGSignalClosure *) closure;

  dbus_g_connection_unref (sigclosure->connection);
  g_free (sigclosure->signame);
}

static void
signal_emitter_marshaller (GClosure        *closure,
			   GValue          *retval,
			   guint            n_param_values,
			   const GValue    *param_values,
			   gpointer         invocation_hint,
			   gpointer         marshal_data)
{
  DBusGSignalClosure *sigclosure;
  DBusMessage *signal;
  DBusMessageIter iter;
  guint i;
  const char *path;

  sigclosure = (DBusGSignalClosure *) closure;
  
  g_assert (retval == NULL);

  path = _dbus_gobject_get_path (sigclosure->object);

  g_assert (path != NULL);

  signal = dbus_message_new_signal (path,
				    "org.gtk.objects",
				    sigclosure->signame);
  if (!signal)
    {
      g_error ("out of memory");
      return;
    }

  dbus_message_iter_init_append (signal, &iter);

  /* First argument is the object itself, and we can't marshall that */
  for (i = 1; i < n_param_values; i++)
    {
      if (!dbus_gvalue_marshal (&iter,
				(GValue *) (&(param_values[i]))))
	{
	  g_warning ("failed to marshal parameter %d for signal %s",
		     i, sigclosure->signame);
	  goto out;
	}
    }
  dbus_connection_send (DBUS_CONNECTION_FROM_G_CONNECTION (sigclosure->connection),
			signal, NULL);
 out:
  dbus_message_unref (signal);
}

static void
export_signals (DBusGConnection *connection, GObject *object)
{
  guint i;
  guint *ids, n_ids;

  ids = g_signal_list_ids (G_TYPE_FROM_INSTANCE (object), &n_ids);
  if (!n_ids)
    return;

  /* FIXME: recurse to parent types ? */
  for (i = 0; i < n_ids; i++)
    {
      GSignalQuery query;
      GClosure *closure;
      
      g_signal_query (ids[i], &query);

      if (query.return_type != G_TYPE_NONE) {
	g_warning("Not exporting signal '%s' as it has a return type %s", query.signal_name, g_type_name (query.return_type));
	continue; /* FIXME: these could be listed as methods ? */
      }

      closure = dbus_g_signal_closure_new (connection, object, query.signal_name);
      g_closure_set_marshal (closure, signal_emitter_marshaller);

      g_signal_connect_closure_by_id (object,
				      ids[i],
				      0,
				      closure,
				      FALSE);

      g_closure_add_finalize_notifier (closure, NULL,
				       dbus_g_signal_closure_finalize);
    }
}

/** @} */ /* end of internals */

/**
 * @addtogroup DBusGLib
 * @{
 */

/**
 * Install introspection information about the given object GType
 * sufficient to allow methods on the object to be invoked by name.
 * The introspection information is normally generated by
 * dbus-glib-tool, then this function is called in the
 * class_init() for the object class.
 *
 * Once introspection information has been installed, instances of the
 * object registered with dbus_g_connection_register_g_object() can have
 * their methods invoked remotely.
 *
 * @param object_type GType for the object
 * @param info introspection data generated by dbus-glib-tool
 */
void
dbus_g_object_type_install_info (GType                  object_type,
				 const DBusGObjectInfo *info)
{
  GObjectClass *object_class;

  g_return_if_fail (G_TYPE_IS_OBJECT (object_type));

  dbus_g_value_types_init ();

  object_class = g_type_class_peek (object_type);

  g_return_if_fail (G_IS_OBJECT_CLASS (object_class));

  g_static_rw_lock_writer_lock (&globals_lock);

  if (info_hash == NULL)
    {
      info_hash = g_hash_table_new (NULL, NULL); /* direct hash */
    }

  g_hash_table_replace (info_hash, object_class, (void*) info);

  g_static_rw_lock_writer_unlock (&globals_lock);
}

static void
unregister_gobject (DBusGConnection *connection, GObject *dead)
{
  char *path;
  path = g_object_steal_data (dead, "dbus_glib_object_path");
  dbus_connection_unregister_object_path (DBUS_CONNECTION_FROM_G_CONNECTION (connection), path);
  g_free (path);
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
                                             object)) {
    g_error ("Failed to register GObject with DBusConnection");
    return;
  }

  export_signals (connection, object);

  g_object_set_data (object, "dbus_glib_object_path", g_strdup (at_path));
  g_object_weak_ref (object, (GWeakNotify)unregister_gobject, connection);
}

GObject *
dbus_g_connection_lookup_g_object (DBusGConnection       *connection,
				   const char            *at_path)
{
  gpointer ret;
  if (!dbus_connection_get_object_path_data (DBUS_CONNECTION_FROM_G_CONNECTION (connection), at_path, &ret))
    return NULL;
  return ret;
}

typedef struct {
  GType    rettype;
  guint    n_params;
  GType   *params;
} DBusGFuncSignature;

static guint
funcsig_hash (gconstpointer key)
{
  const DBusGFuncSignature *sig = key;
  GType *types;
  guint ret;

  ret = sig->rettype;
  types = sig->params;

  while (*types != G_TYPE_INVALID)
    {
      ret += (int) (*types);
      types++;
    }
      
  return ret;
}

static gboolean
funcsig_equal (gconstpointer aval,
	       gconstpointer bval)
{
  const DBusGFuncSignature *a = aval;
  const DBusGFuncSignature *b = bval;
  const GType *atypes;
  const GType *btypes;

  if (a->rettype != b->rettype)
    return FALSE;

  atypes = a->params;
  btypes = b->params;

  while (*atypes != G_TYPE_INVALID)
    {
      if (*btypes != *atypes)
	return FALSE;
      atypes++;
      btypes++;
    }
  if (*btypes != G_TYPE_INVALID)
    return FALSE;
      
  return TRUE;
}

GClosureMarshal
_dbus_gobject_lookup_marshaller (GType        rettype,
				 guint        n_params,
				 const GType *param_types)
{
  GClosureMarshal ret;
  DBusGFuncSignature sig;

  sig.rettype = rettype;
  sig.n_params = n_params;
  sig.params = (GType*) param_types;
  
  g_static_rw_lock_reader_lock (&globals_lock);

  if (marshal_table)
    ret = g_hash_table_lookup (marshal_table, &sig);
  else
    ret = NULL;

  g_static_rw_lock_reader_unlock (&globals_lock);

  if (ret == NULL)
    {
      if (rettype == G_TYPE_NONE)
	{
	  if (n_params == 0)
	    ret = g_cclosure_marshal_VOID__VOID;
	  else if (n_params == 1)
	    {
	      switch (param_types[0])
		{
		case G_TYPE_BOOLEAN:
		  ret = g_cclosure_marshal_VOID__BOOLEAN;
		  break;
		case G_TYPE_UCHAR:
		  ret = g_cclosure_marshal_VOID__UCHAR;
		  break;
		case G_TYPE_INT:
		  ret = g_cclosure_marshal_VOID__INT;
		  break;
		case G_TYPE_UINT:
		  ret = g_cclosure_marshal_VOID__UINT;
		  break;
		case G_TYPE_DOUBLE:
		  ret = g_cclosure_marshal_VOID__DOUBLE;
		  break;
		case G_TYPE_STRING:
		  ret = g_cclosure_marshal_VOID__STRING;
		  break;
		}
	    }
	}
      else if (n_params == 3
	       && param_types[0] == G_TYPE_STRING
	       && param_types[1] == G_TYPE_STRING
	       && param_types[2] == G_TYPE_STRING)
	{
	  ret = _dbus_g_marshal_NONE__STRING_STRING_STRING;
	}
    }

  return ret;
}

/**
 * Register a GClosureMarshal to be used for signal invocations.
 * This function will not be needed once GLib includes libffi.
 *
 * @param rettype a GType for the return type of the function
 * @param n_types number of function parameters
 * @param param_types a C array of GTypes values
 * @param marshaller a GClosureMarshal to be used for invocation
 */
void
dbus_g_object_register_marshaller (GType            rettype,
				   guint            n_types,
				   const GType      *param_types,
				   GClosureMarshal   marshaller)
{
  DBusGFuncSignature *sig;
  
  g_static_rw_lock_writer_lock (&globals_lock);

  if (marshal_table == NULL)
    marshal_table = g_hash_table_new_full (funcsig_hash,
					   funcsig_equal,
					   g_free,
					   NULL);
  sig = g_new0 (DBusGFuncSignature, 1);
  sig->rettype = rettype;
  sig->n_params = n_types;
  sig->params = g_new (GType, n_types);
  memcpy (sig->params, param_types, n_types * sizeof (GType));

  g_hash_table_insert (marshal_table, sig, marshaller);

  g_static_rw_lock_writer_unlock (&globals_lock);
}

/**
 * Send a return message for a given method invocation, with arguments.
 *
 * @param context the method context
 */
void
dbus_g_method_return (DBusGMethodInvocation *context, ...)
{
  DBusMessage *reply;
  DBusMessageIter iter;
  va_list args;
  char *out_sig;
  GArray *argsig;
  guint i;

  reply = dbus_message_new_method_return (dbus_g_message_get_message (context->message));
  out_sig = method_output_signature_from_object_info (context->object, context->method);
  argsig = dbus_gtypes_from_arg_signature (out_sig, FALSE);

  dbus_message_iter_init_append (reply, &iter);

  va_start (args, context);
  for (i = 0; i < argsig->len; i++) {
    GValue value = {0,};
    char *error;
    g_value_init (&value, g_array_index (argsig, GType, i));
    error = NULL;
    G_VALUE_COLLECT (&value, args, 0, &error);
    if (error) {
      g_warning(error);
      g_free (error);
    }
    dbus_gvalue_marshal (&iter, &value);
  }
  va_end (args);

  dbus_connection_send (dbus_g_connection_get_connection (context->connection), reply, NULL);
  dbus_message_unref (reply);

  dbus_g_connection_unref (context->connection);
  dbus_g_message_unref (context->message);
  g_free (context);
}

/**
 * Send a error message for a given method invocation.
 *
 * @param context the method context
 * @param error the error to send.
 */
void
dbus_g_method_return_error (DBusGMethodInvocation *context, GError *error)
{
  DBusMessage *reply;
  reply = gerror_to_dbus_error_message (context->object, dbus_g_message_get_message (context->message), error);
  dbus_connection_send (dbus_g_connection_get_connection (context->connection), reply, NULL);
  dbus_message_unref (reply);
}

/** @} */ /* end of public API */

const char * _dbus_gobject_get_path (GObject *obj)
{
  return g_object_get_data (obj, "dbus_glib_object_path");
}

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

      uscore = _dbus_gutils_wincaps_to_uscore (name_pairs[i].wincaps);
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
