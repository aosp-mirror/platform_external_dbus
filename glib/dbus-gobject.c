/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gobject.c Exporting a GObject remotely
 *
 * Copyright (C) 2003, 2004, 2005 Red Hat, Inc.
 * Copyright (C) 2005 Nokia
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
#include "dbus-gsignature.h"
#include "dbus-gvalue.h"
#include "dbus-gmarshal.h"
#include "dbus-gvalue-utils.h"
#include <string.h>

/**
 * @addtogroup DBusGLibInternals
 * @{
 */

typedef struct
{
  char *default_iface;
  GType code_enum;
} DBusGErrorInfo;

static GStaticRWLock globals_lock = G_STATIC_RW_LOCK_INIT;
static GHashTable *marshal_table = NULL;
static GData *error_metadata = NULL;

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

typedef enum
{
  RETVAL_NONE,    
  RETVAL_NOERROR,    
  RETVAL_ERROR
} RetvalType;

static const char *
arg_iterate (const char    *data,
	     const char   **name,
	     gboolean      *in,
	     gboolean      *constval,
	     RetvalType    *retval,
	     const char   **type)
{
  gboolean inarg;

  if (name)
    *name = data;

  data = string_table_next (data);
  switch (*data)
    {
    case 'I':
      inarg = TRUE;
      break;
    case 'O':
      inarg = FALSE;
      break;
    default:
      g_warning ("invalid arg direction '%c'", *data);
      inarg = FALSE;
      break;
    }
  if (in)
    *in = inarg;

  if (!inarg)
    {
      data = string_table_next (data);
      switch (*data)
	{
	case 'F':
	  if (constval)
	    *constval = FALSE;
	  break;
	case 'C':
	  if (constval)
	    *constval = TRUE;
	  break;
	default:
	  g_warning ("invalid arg const value '%c'", *data);
	  break;
	}
      data = string_table_next (data);
      switch (*data)
	{
	case 'N':
	  if (retval)
	    *retval = RETVAL_NONE;
	  break;
	case 'E':
	  if (retval)
	    *retval = RETVAL_ERROR;
	  break;
	case 'R':
	  if (retval)
	    *retval = RETVAL_NOERROR;
	  break;
	default:
	  g_warning ("invalid arg ret value '%c'", *data);
	  break;
	}
    }
  else
    {
      if (constval)
	*constval = FALSE;
      if (retval)
	*retval = FALSE;
    }
  
  data = string_table_next (data);
  if (type)
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

      arg = arg_iterate (arg, &name, &arg_in, NULL, NULL, &type);

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

static const char *
propsig_iterate (const char *data, const char **iface, const char **name)
{
  *iface = data;

  data = string_table_next (data);
  *name = data;

  return string_table_next (data);
}

static GQuark
dbus_g_object_type_dbus_metadata_quark (void)
{
  static GQuark quark;

  if (!quark)
    quark = g_quark_from_static_string ("DBusGObjectTypeDBusMetadataQuark");
  return quark;
}

static const DBusGObjectInfo *
lookup_object_info (GObject *object)
{
  const DBusGObjectInfo *ret;
  GType classtype;
  
  ret = NULL;
  
  for (classtype = G_TYPE_FROM_INSTANCE (object); classtype != 0; classtype = g_type_parent (classtype))
    {
      const DBusGObjectInfo *info;

      info = g_type_get_qdata (classtype, dbus_g_object_type_dbus_metadata_quark ()); 

      if (info != NULL && info->format_version >= 0)
	{
	  ret = info;
	  break;
	}
    }

  return ret;
}

static void
gobject_unregister_function (DBusConnection  *connection,
                             void            *user_data)
{
  GObject *object;

  object = G_OBJECT (user_data);

  /* FIXME */

}

typedef struct
{
  GString *xml;
  GType gtype;
  const DBusGObjectInfo *object_info;
} DBusGLibWriteIterfaceData;

typedef struct
{
  GSList *methods;
  GSList *signals;
  GSList *properties;
} DBusGLibWriteInterfaceValues;

static void
write_interface (gpointer key, gpointer val, gpointer user_data)
{
  const char *name;
  GSList *methods;
  GSList *signals;
  GSList *properties;
  GString *xml;
  const DBusGObjectInfo *object_info;
  DBusGLibWriteIterfaceData *data;
  DBusGLibWriteInterfaceValues *values;

  name = key;

  values = val;
  methods = values->methods;
  signals = values->signals;
  properties = values->properties;

  data = user_data;
  xml = data->xml;
  object_info = data->object_info;

  g_string_append_printf (xml, "  <interface name=\"%s\">\n", name);

  /* FIXME: recurse to parent types ? */
  for (; methods; methods = methods->next)
    {
      DBusGMethodInfo *method;
      const char *args;
      method = methods->data;

      g_string_append_printf (xml, "    <method name=\"%s\">\n",
			      method_name_from_object_info (object_info, method));

      args = method_arg_info_from_object_info (object_info, method);

      while (*args)
	{
	  const char *name;
	  gboolean arg_in;
	  const char *type;
	  
	  args = arg_iterate (args, &name, &arg_in, NULL, NULL, &type);

	  /* FIXME - handle container types */
	  g_string_append_printf (xml, "      <arg name=\"%s\" type=\"%s\" direction=\"%s\"/>\n",
				  name, type, arg_in ? "in" : "out");

	}
      g_string_append (xml, "    </method>\n");

    }
  g_slist_free (values->methods);

  for (; signals; signals = signals->next)
    {
      guint id;
      guint arg;
      const char *signame;
      GSignalQuery query;
      char *s;

      signame = signals->data;

      s = _dbus_gutils_wincaps_to_uscore (signame);
      
      id = g_signal_lookup (s, data->gtype);
      g_assert (id != 0);

      g_signal_query (id, &query);
      g_assert (query.return_type == G_TYPE_NONE);

      g_string_append_printf (xml, "    <signal name=\"%s\">\n", signame);

      for (arg = 0; arg < query.n_params; arg++)
	{
	  char *dbus_type = _dbus_gtype_to_signature (query.param_types[arg]);

	  g_assert (dbus_type != NULL);

          g_string_append (xml, "      <arg type=\"");
          g_string_append (xml, dbus_type);
          g_string_append (xml, "\"/>\n");
	  g_free (dbus_type);
	}

      g_string_append (xml, "    </signal>\n");
      g_free (s);
    }
  g_slist_free (values->signals);

  for (; properties; properties = properties->next)
    {
      const char *propname;
      GParamSpec *spec;
      char *dbus_type;
      gboolean can_set;
      gboolean can_get;
      char *s;

      propname = properties->data;
      spec = NULL;

      s = _dbus_gutils_wincaps_to_uscore (spec->name);

      spec = g_object_class_find_property (g_type_class_peek (data->gtype), s);
      g_assert (spec != NULL);
      g_free (s);
      
      dbus_type = _dbus_gtype_to_signature (G_PARAM_SPEC_VALUE_TYPE (spec));
      g_assert (dbus_type != NULL);
      
      can_set = ((spec->flags & G_PARAM_WRITABLE) != 0 &&
		 (spec->flags & G_PARAM_CONSTRUCT_ONLY) == 0);
      
      can_get = (spec->flags & G_PARAM_READABLE) != 0;
      
      if (can_set || can_get)
	{
	  g_string_append_printf (xml, "    <property name=\"%s\" ", propname);
	  g_string_append (xml, "type=\"");
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
      
      g_free (dbus_type);
      g_free (s);

      g_string_append (xml, "    </property>\n");
    }
  g_slist_free (values->properties);

  g_free (values);
  g_string_append (xml, "  </interface>\n");
}

static DBusGLibWriteInterfaceValues *
lookup_values (GHashTable *interfaces, const char *method_interface)
{
  DBusGLibWriteInterfaceValues *values;
  if ((values = g_hash_table_lookup (interfaces, (gpointer) method_interface)) == NULL)
    {
      values = g_new0 (DBusGLibWriteInterfaceValues, 1);
      g_hash_table_insert (interfaces, (gpointer) method_interface, values);
    }
  return values;
}

static void
introspect_interfaces (GObject *object, GString *xml)
{
  const DBusGObjectInfo *info;
  DBusGLibWriteIterfaceData data;
  int i;
  GHashTable *interfaces;
  DBusGLibWriteInterfaceValues *values;
  const char *propsig;

  info = lookup_object_info (object);

  g_assert (info != NULL);

  /* Gather a list of all interfaces, indexed into their methods */
  interfaces = g_hash_table_new (g_str_hash, g_str_equal);
  for (i = 0; i < info->n_method_infos; i++)
    {
      const char *method_name;
      const char *method_interface;
      const char *method_args;
      const DBusGMethodInfo *method;

      method = &(info->method_infos[i]);

      method_interface = method_interface_from_object_info (info, method);
      method_name = method_name_from_object_info (info, method);
      method_args = method_arg_info_from_object_info (info, method);

      values = lookup_values (interfaces, method_interface);
      values->methods = g_slist_prepend (values->methods, (gpointer) method);
    }

  propsig = info->exported_signals;
  while (*propsig)
    {
      const char *iface;
      const char *signame;

      propsig = propsig_iterate (propsig, &iface, &signame);

      values = lookup_values (interfaces, iface);
      values->signals = g_slist_prepend (values->signals, (gpointer) signame);
    }

  propsig = info->exported_properties;
  while (*propsig)
    {
      const char *iface;
      const char *propname;

      propsig = propsig_iterate (propsig, &iface, &propname);

      values = lookup_values (interfaces, iface);
      values->properties = g_slist_prepend (values->properties, (gpointer) propname);
    }
  
  memset (&data, 0, sizeof (data));
  data.xml = xml;
  data.gtype = G_TYPE_FROM_INSTANCE (object);
  data.object_info = info;
  g_hash_table_foreach (interfaces, write_interface, &data);
  
  g_hash_table_destroy (interfaces);
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
  if (_dbus_gvalue_demarshal (&context, &sub, &value, NULL))
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
  GType value_gtype;
  GValue value = {0, };
  gchar *variant_sig;
  DBusMessage *ret;
  DBusMessageIter iter, subiter;

  ret = dbus_message_new_method_return (message);
  if (ret == NULL)
    g_error ("out of memory");


  g_value_init (&value, pspec->value_type);
  g_object_get_property (object, pspec->name, &value);

  variant_sig = _dbus_gvalue_to_signature (&value);
  if (variant_sig == NULL)
    {
      value_gtype = G_VALUE_TYPE (&value);
      g_warning ("Cannot marshal type \"%s\" in variant", g_type_name (value_gtype));
      g_value_unset (&value);
      return ret;
    }

  dbus_message_iter_init_append (ret, &iter);
  if (!dbus_message_iter_open_container (&iter,
					 DBUS_TYPE_VARIANT,
					 variant_sig,
					 &subiter))
    {
      g_free (variant_sig);
      g_value_unset (&value);
      return ret;
    }

  if (!_dbus_gvalue_marshal (&subiter, &value))
    {
      dbus_message_unref (ret);
      ret = dbus_message_new_error (message,
                                    DBUS_ERROR_UNKNOWN_METHOD,
                                    "Can't convert GType of object property to a D-BUS type");
    }

  dbus_message_iter_close_container (&iter, &subiter);

  g_value_unset (&value);
  g_free (variant_sig);

  return ret;
}

static gboolean
lookup_object_and_method (GObject      *object,
			  DBusMessage  *message,
			  const DBusGObjectInfo **object_ret,
			  const DBusGMethodInfo **method_ret)
{
  const char *interface;
  const char *member;
  const char *signature;
  gboolean ret;
  const DBusGObjectInfo *info;
  int i;

  interface = dbus_message_get_interface (message);
  member = dbus_message_get_member (message);
  signature = dbus_message_get_signature (message);
  ret = FALSE;

  info = lookup_object_info (object);
  *object_ret = info;
  
  for (i = 0; i < info->n_method_infos; i++)
    {
      const char *expected_member;
      const char *expected_interface;
      char *expected_signature;
      const DBusGMethodInfo *method;

      method = &(info->method_infos[i]);

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
	  return TRUE;
	}
      g_free (expected_signature);
    }

  return ret;
}

static char *
gerror_domaincode_to_dbus_error_name (const DBusGObjectInfo *object_info,
				      const char *msg_interface,
				      GQuark domain, gint code)
{
  const char *domain_str;
  const char *code_str;
  GString *dbus_error_name;

  domain_str = object_error_domain_prefix_from_object_info (object_info);
  code_str = object_error_code_from_object_info (object_info, domain, code);

  if (!domain_str || !code_str)
    {
      DBusGErrorInfo *info;

      g_static_rw_lock_reader_lock (&globals_lock);

      if (error_metadata != NULL)
	info = g_datalist_id_get_data (&error_metadata, domain);
      else
	info = NULL;

      g_static_rw_lock_reader_unlock (&globals_lock);

      if (info)
	{
	  GEnumValue *value;
	  GEnumClass *klass;

	  klass = g_type_class_ref (info->code_enum);
	  value = g_enum_get_value (klass, code);
	  g_type_class_unref (klass);

	  domain_str = info->default_iface;
	  code_str = value->value_nick;
	}
    }

  if (!domain_str)
    domain_str = msg_interface;

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
      if (error->domain == DBUS_GERROR)
	reply = dbus_message_new_error (message,
					dbus_g_error_get_name (error),
					error->message);
      else
	{
	  char *error_name;
	  error_name = gerror_domaincode_to_dbus_error_name (object_info,
							     dbus_message_get_interface (message),
							     error->domain, error->code);
	  reply = dbus_message_new_error (message, error_name, error->message);
	  g_free (error_name); 
	}
    }
  return reply;
}

/**
 * The context of an asynchronous method call.  See dbus_g_method_return() and
 * dbus_g_method_return_error().
 */
struct _DBusGMethodInvocation {
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
  GValue return_value = {0,};
  GClosure closure;
  char *in_signature;
  GArray *out_param_values = NULL;
  GValueArray *out_param_gvalues = NULL;
  int out_param_count;
  int out_param_pos, out_param_gvalue_pos;
  DBusHandlerResult result;
  DBusMessage *reply;
  gboolean have_retval;
  gboolean retval_signals_error;
  gboolean retval_is_synthetic;
  gboolean retval_is_constant;
  const char *arg_metadata;

  gerror = NULL;

  /* Determine whether or not this method should be invoked in a new
     thread
   */
  if (strcmp (string_table_lookup (get_method_data (object_info, method), 2), "A") == 0)
    call_only = TRUE;
  else
    call_only = FALSE;

  have_retval = FALSE;
  retval_signals_error = FALSE;
  retval_is_synthetic = FALSE;
  retval_is_constant = FALSE;

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

    types_array = _dbus_gtypes_from_arg_signature (in_signature, FALSE);
    n_params = types_array->len;
    types = (const GType*) types_array->data;

    value_array = _dbus_gvalue_demarshal_message (&context, message, n_params, types, &error);
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
  g_value_array_prepend (value_array, NULL);
  g_value_init (g_value_array_get_nth (value_array, 0), G_TYPE_OBJECT);
  g_value_set_object (g_value_array_get_nth (value_array, 0), object);
  
  if (call_only)
    {
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
    }
  else
    {
      RetvalType retval;
      gboolean arg_in;
      gboolean arg_const;
      const char *argsig;

      arg_metadata = method_arg_info_from_object_info (object_info, method);
      
      /* Count number of output parameters, and look for a return value */
      out_param_count = 0;
      while (*arg_metadata)
	{
	  arg_metadata = arg_iterate (arg_metadata, NULL, &arg_in, &arg_const, &retval, &argsig);
	  if (arg_in)
	    continue;
	  if (retval != RETVAL_NONE)
	    {
	      DBusSignatureIter tmp_sigiter;
	      /* This is the function return value */
	      g_assert (!have_retval);
	      have_retval = TRUE;
	      retval_is_synthetic = FALSE;

	      switch (retval)
		{
		case RETVAL_NONE:
		  g_assert_not_reached ();
		  break;
		case RETVAL_NOERROR:
		  retval_signals_error = FALSE;
		  break;
		case RETVAL_ERROR:
		  retval_signals_error = TRUE;
		  break;
		}

	      retval_is_constant = arg_const;

	      /* Initialize our return GValue with the specified type */
	      dbus_signature_iter_init (&tmp_sigiter, argsig);
	      g_value_init (&return_value, _dbus_gtype_from_signature_iter (&tmp_sigiter, FALSE));
	    }
	  else
	    {
	      /* It's a regular output value */
	      out_param_count++;
	    }
	}

      /* For compatibility, if we haven't found a return value, we assume
       * the function returns a gboolean for signalling an error
       * (and therefore also takes a GError).  We also note that it
       * is a "synthetic" return value; i.e. we aren't going to be
       * sending it over the bus, it's just to signal an error.
       */
      if (!have_retval)
	{
	  have_retval = TRUE;
	  retval_is_synthetic = TRUE;
	  retval_signals_error = TRUE;
	  g_value_init (&return_value, G_TYPE_BOOLEAN);
	}

      /* Create an array to store the actual values of OUT parameters
       * (other than the real function return, if any).  Then, create
       * a GValue boxed POINTER to each of those values, and append to
       * the invocation, so the method can return the OUT parameters.
       */
      out_param_values = g_array_sized_new (FALSE, TRUE, sizeof (GTypeCValue), out_param_count);

      /* We have a special array of GValues for toplevel GValue return
       * types.
       */
      out_param_gvalues = g_value_array_new (out_param_count);
      out_param_pos = 0;
      out_param_gvalue_pos = 0;

      /* Reset argument metadata pointer */
      arg_metadata = method_arg_info_from_object_info (object_info, method);
      
      /* Iterate over output arguments again, this time allocating space for
       * them as appopriate.
       */
      while (*arg_metadata)
	{
	  GValue value = {0, };
	  GTypeCValue storage;
	  DBusSignatureIter tmp_sigiter;
	  GType current_gtype;

	  arg_metadata = arg_iterate (arg_metadata, NULL, &arg_in, NULL, &retval, &argsig);
	  /* Skip over input arguments and the return value, if any */
	  if (arg_in || retval != RETVAL_NONE)
	    continue;

	  dbus_signature_iter_init (&tmp_sigiter, argsig);
	  current_gtype = _dbus_gtype_from_signature_iter (&tmp_sigiter, FALSE);

	  g_value_init (&value, G_TYPE_POINTER);

	  /* We special case variants to make method invocation a bit nicer */
	  if (current_gtype != G_TYPE_VALUE)
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
	}
    }

  /* Append GError as final argument if necessary */
  if (retval_signals_error)
    {
      g_assert (have_retval);
      g_value_array_append (value_array, NULL);
      g_value_init (g_value_array_get_nth (value_array, value_array->n_values - 1), G_TYPE_POINTER);
      g_value_set_pointer (g_value_array_get_nth (value_array, value_array->n_values - 1), &gerror);
    }
  
  /* Actually invoke method */
  method->marshaller (&closure, have_retval ? &return_value : NULL,
		      value_array->n_values,
		      value_array->values,
		      NULL, method->function);
  if (call_only)
    {
      result = DBUS_HANDLER_RESULT_HANDLED;
      goto done;
    }
  if (retval_signals_error)
    had_error = _dbus_gvalue_signals_error (&return_value);
  else
    had_error = FALSE;

  if (!had_error)
    {
      DBusMessageIter iter;

      reply = dbus_message_new_method_return (message);
      if (reply == NULL)
	goto nomem;

      /* Append output arguments to reply */
      dbus_message_iter_init_append (reply, &iter);

      /* First, append the return value, unless it's synthetic */
      if (have_retval && !retval_is_synthetic)
	{
	  if (!_dbus_gvalue_marshal (&iter, &return_value))
	    goto nomem;
	  if (!retval_is_constant)
	    g_value_unset (&return_value);
	}

      /* Grab the argument metadata and iterate over it */
      arg_metadata = method_arg_info_from_object_info (object_info, method);
      
      /* Now append any remaining return values */
      out_param_pos = 0;
      out_param_gvalue_pos = 0;
      while (*arg_metadata)
	{
	  GValue gvalue = {0, };
	  const char *arg_name;
	  gboolean arg_in;
	  gboolean constval;
	  RetvalType retval;
	  const char *arg_signature;
	  DBusSignatureIter argsigiter;

	  do
	    {
	      /* Iterate over only output values; skip over input
		 arguments and the return value */
	      arg_metadata = arg_iterate (arg_metadata, &arg_name, &arg_in, &constval, &retval, &arg_signature);
	    }
	  while ((arg_in || retval != RETVAL_NONE) && *arg_metadata);

	  /* If the last argument we saw was input or the return
  	   * value, we must be done iterating over output arguments.
	   */
	  if (arg_in || retval != RETVAL_NONE)
	    break;

	  dbus_signature_iter_init (&argsigiter, arg_signature);
	  
	  g_value_init (&gvalue, _dbus_gtype_from_signature_iter (&argsigiter, FALSE));
	  if (G_VALUE_TYPE (&gvalue) != G_TYPE_VALUE)
	    {
	      if (!_dbus_gvalue_take (&gvalue,
				     &(g_array_index (out_param_values, GTypeCValue, out_param_pos))))
		g_assert_not_reached ();
	      out_param_pos++;
	    }
	  else
	    {
	      g_value_set_static_boxed (&gvalue, out_param_gvalues->values + out_param_gvalue_pos);
	      out_param_gvalue_pos++;
	    }
	      
	  if (!_dbus_gvalue_marshal (&iter, &gvalue))
	    goto nomem;
	  /* Here we actually free the allocated value; we
	   * took ownership of it with _dbus_gvalue_take, unless
	   * an annotation has specified this value as constant.
	   */
	  if (!constval)
	    g_value_unset (&gvalue);
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
  if (!call_only)
    {
      g_array_free (out_param_values, TRUE);
      g_value_array_free (out_param_gvalues);
    }
  g_value_array_free (value_array);
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

static const DBusObjectPathVTable gobject_dbus_vtable = {
  gobject_unregister_function,
  gobject_message_function,
  NULL
};

typedef struct {
  GClosure         closure;
  DBusGConnection *connection;
  GObject         *object;
  const char      *signame;
  const char      *sigiface;
} DBusGSignalClosure;

static GClosure *
dbus_g_signal_closure_new (DBusGConnection *connection,
			   GObject         *object,
			   const char      *signame,
			   const char      *sigiface)
{
  DBusGSignalClosure *closure;
  
  closure = (DBusGSignalClosure*) g_closure_new_simple (sizeof (DBusGSignalClosure), NULL);

  closure->connection = dbus_g_connection_ref (connection);
  closure->object = object;
  closure->signame = signame;
  closure->sigiface = sigiface;
  return (GClosure*) closure;
}

static void
dbus_g_signal_closure_finalize (gpointer data,
				GClosure *closure)
{
  DBusGSignalClosure *sigclosure = (DBusGSignalClosure *) closure;

  dbus_g_connection_unref (sigclosure->connection);
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
				    sigclosure->sigiface,
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
      if (!_dbus_gvalue_marshal (&iter,
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
export_signals (DBusGConnection *connection, const DBusGObjectInfo *info, GObject *object)
{
  GType gtype;
  const char *sigdata;
  const char *iface;
  const char *signame;

  gtype = G_TYPE_FROM_INSTANCE (object);

  sigdata = info->exported_signals;
  
  while (*sigdata != '\0')
    {
      guint id;
      GSignalQuery query;
      GClosure *closure;
      char *s;

      sigdata = propsig_iterate (sigdata, &iface, &signame);
      
      s = _dbus_gutils_wincaps_to_uscore (signame);

      id = g_signal_lookup (s, gtype);
      if (id == 0)
	{
	  g_warning ("signal \"%s\" (from \"%s\") exported but not found in object class \"%s\"",
		     s, signame, g_type_name (gtype));
	  g_free (s);
	  continue;
	}

      g_signal_query (id, &query);

      if (query.return_type != G_TYPE_NONE)
	{
	  g_warning ("Not exporting signal \"%s\" for object class \"%s\" as it has a return type \"%s\"",
		     s, g_type_name (gtype), g_type_name (query.return_type));
	  g_free (s);
	  continue; /* FIXME: these could be listed as methods ? */
	}
      
      closure = dbus_g_signal_closure_new (connection, object, signame, (char*) iface);
      g_closure_set_marshal (closure, signal_emitter_marshaller);

      g_signal_connect_closure_by_id (object,
				      id,
				      0,
				      closure,
				      FALSE);

      g_closure_add_finalize_notifier (closure, NULL,
				       dbus_g_signal_closure_finalize);
      g_free (s);
    }
}

#include "dbus-glib-error-switch.h"

void
dbus_set_g_error (GError    **gerror,
		  DBusError  *error)
{
  int code;

  code = dbus_error_to_gerror_code (error->name);
  if (code != DBUS_GERROR_REMOTE_EXCEPTION)
    g_set_error (gerror, DBUS_GERROR,
		 code,
		 "%s",
		 error->message);
  else
    g_set_error (gerror, DBUS_GERROR,
		 code,
		 "%s%c%s",
		 error->message ? error->message : "",
		 '\0',
		 error->name);
}

static void
dbus_g_error_info_free (gpointer p)
{
  DBusGErrorInfo *info;

  info = p;

  g_free (info->default_iface);
  g_free (info);
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
  g_return_if_fail (G_TYPE_IS_CLASSED (object_type));

  _dbus_g_value_types_init ();

  g_type_set_qdata (object_type,
		    dbus_g_object_type_dbus_metadata_quark (),
		    (gpointer) info);
}

/**
 * Register a GError domain and set of codes with D-BUS.  You must
 * have created a GEnum for the error codes.  This function will not
 * be needed with an introspection-capable GLib.
 *
 * @param domain the GError domain 
 * @param default_iface the D-BUS interface used for error values by default, or #NULL
 * @param code_enum a GType for a GEnum of the error codes
 */
void
dbus_g_error_domain_register (GQuark                domain,
			      const char           *default_iface,
			      GType                 code_enum)
{
  DBusGErrorInfo *info;
  
  g_return_if_fail (g_quark_to_string (domain) != NULL);
  g_return_if_fail (code_enum != G_TYPE_INVALID);
  g_return_if_fail (G_TYPE_FUNDAMENTAL (code_enum) == G_TYPE_ENUM);

  g_static_rw_lock_writer_lock (&globals_lock);

  if (error_metadata == NULL)
    g_datalist_init (&error_metadata);

  info = g_datalist_id_get_data (&error_metadata, domain);

  if (info != NULL)
    {
      g_warning ("Metadata for error domain \"%s\" already registered\n",
		 g_quark_to_string (domain));
    }
  else
    {
      info = g_new0 (DBusGErrorInfo, 1);
      info->default_iface = g_strdup (default_iface);
      info->code_enum = code_enum;

      g_datalist_id_set_data_full (&error_metadata,
				   domain,
				   info,
				   dbus_g_error_info_free);
    }

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
  const DBusGObjectInfo *info;
  g_return_if_fail (connection != NULL);
  g_return_if_fail (at_path != NULL);
  g_return_if_fail (G_IS_OBJECT (object));

  info = lookup_object_info (object);
  if (info == NULL)
    {
      g_warning ("No introspection data registered for object class \"%s\"",
		 g_type_name (G_TYPE_FROM_INSTANCE (object)));
      return;
    }

  if (!dbus_connection_register_object_path (DBUS_CONNECTION_FROM_G_CONNECTION (connection),
                                             at_path,
                                             &gobject_dbus_vtable,
                                             object))
    {
      g_error ("Failed to register GObject with DBusConnection");
      return;
    }

  export_signals (connection, info, object);

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
  guint i;

  ret = sig->rettype;
  types = sig->params;

  for (i = 0; i < sig->n_params; i++)
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
  guint i;

  if (a->rettype != b->rettype
      || a->n_params != b->n_params)
    return FALSE;

  atypes = a->params;
  btypes = b->params;

  for (i = 0; i < a->n_params; i++)
    {
      if (*btypes != *atypes)
	return FALSE;
      atypes++;
      btypes++;
    }
      
  return TRUE;
}

GClosureMarshal
_dbus_gobject_lookup_marshaller (GType        rettype,
				 guint        n_params,
				 const GType *param_types)
{
  GClosureMarshal ret;
  DBusGFuncSignature sig;
  GType *params;
  guint i;

  /* Convert to fundamental types */
  rettype = G_TYPE_FUNDAMENTAL (rettype);
  params = g_new (GType, n_params);
  for (i = 0; i < n_params; i++)
    params[i] = G_TYPE_FUNDAMENTAL (param_types[i]);

  sig.rettype = rettype;
  sig.n_params = n_params;
  sig.params = params;
  
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
	      switch (params[0])
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
		case G_TYPE_BOXED:
		  ret = g_cclosure_marshal_VOID__BOXED;
		  break;
		}
	    }
	  else if (n_params == 3
		   && params[0] == G_TYPE_STRING
		   && params[1] == G_TYPE_STRING
		   && params[2] == G_TYPE_STRING)
	    {
	      ret = _dbus_g_marshal_NONE__STRING_STRING_STRING;
	    }
	}
    }

  g_free (params);
  return ret;
}

/**
 * Register a GClosureMarshal to be used for signal invocations,
 * giving its return type and a list of parameter types,
 * followed by G_TYPE_INVALID.

 * This function will not be needed once GLib includes libffi.
 *
 * @param marshaller a GClosureMarshal to be used for invocation
 * @param rettype a GType for the return type of the function
 * @param ... The parameter GTypes, followed by G_TYPE_INVALID
 */
void
dbus_g_object_register_marshaller (GClosureMarshal  marshaller,
				   GType            rettype,
				   ...)
{
  va_list args;
  GArray *types;
  GType gtype;

  va_start (args, rettype);

  types = g_array_new (TRUE, TRUE, sizeof (GType));

  while ((gtype = va_arg (args, GType)) != G_TYPE_INVALID)
    g_array_append_val (types, gtype);

  dbus_g_object_register_marshaller_array (marshaller, rettype,
					   types->len, (GType*) types->data);

  g_array_free (types, TRUE);
  va_end (args);
}

/**
 * Register a GClosureMarshal to be used for signal invocations.
 * See also #dbus_g_object_register_marshaller
 *
 * @param marshaller a GClosureMarshal to be used for invocation
 * @param rettype a GType for the return type of the function
 * @param n_types number of function parameters
 * @param types a C array of GTypes values
 */
void
dbus_g_object_register_marshaller_array (GClosureMarshal  marshaller,
					 GType            rettype,
					 guint            n_types,
					 const GType*     types)
{
  DBusGFuncSignature *sig;
  guint i;

  g_static_rw_lock_writer_lock (&globals_lock);

  if (marshal_table == NULL)
    marshal_table = g_hash_table_new_full (funcsig_hash,
					   funcsig_equal,
					   g_free,
					   NULL);
  sig = g_new0 (DBusGFuncSignature, 1);
  sig->rettype = G_TYPE_FUNDAMENTAL (rettype);
  sig->n_params = n_types;
  sig->params = g_new (GType, n_types);
  for (i = 0; i < n_types; i++)
    sig->params[i] = G_TYPE_FUNDAMENTAL (types[i]);

  g_hash_table_insert (marshal_table, sig, marshaller);

  g_static_rw_lock_writer_unlock (&globals_lock);
}

/**
 * Get the sender of a message so we can send a
 * "reply" later (i.e. send a message directly
 * to a service which invoked the method at a 
 * later time).
 *
 * @param context the method context
 *
 * @return the unique name of teh sender
 */
gchar *
dbus_g_method_get_sender (DBusGMethodInvocation *context)
{
  const gchar *sender;

  sender = dbus_message_get_sender (dbus_g_message_get_message (context->message));

  if (sender == NULL)
    return NULL;
    
  return strdup (sender);
}

/**
 * Get the reply message to append reply values
 * Used as a sidedoor when you can't generate dbus values
 * of the correct type due to glib binding limitations
 *
 * @param context the method context
 */
DBusMessage *
dbus_g_method_get_reply (DBusGMethodInvocation *context)
{
  return dbus_message_new_method_return (dbus_g_message_get_message (context->message));
}

/**
 * Send a manually created reply message
 * Used as a sidedoor when you can't generate dbus values
 * of the correct type due to glib binding limitations
 *
 * @param context the method context
 * @param reply the reply message, will be unreffed
 */
void
dbus_g_method_send_reply (DBusGMethodInvocation *context, DBusMessage *reply)
{
  dbus_connection_send (dbus_g_connection_get_connection (context->connection), reply, NULL);
  dbus_message_unref (reply);

  dbus_g_connection_unref (context->connection);
  dbus_g_message_unref (context->message);
  g_free (context);
}


/**
 * Send a return message for a given method invocation, with arguments.
 * This function also frees the sending context.
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
  argsig = _dbus_gtypes_from_arg_signature (out_sig, FALSE);

  dbus_message_iter_init_append (reply, &iter);

  va_start (args, context);
  for (i = 0; i < argsig->len; i++)
    {
      GValue value = {0,};
      char *error;
      g_value_init (&value, g_array_index (argsig, GType, i));
      error = NULL;
      G_VALUE_COLLECT (&value, args, G_VALUE_NOCOPY_CONTENTS, &error);
      if (error)
	{
	  g_warning(error);
	  g_free (error);
	}
      _dbus_gvalue_marshal (&iter, &value);
    }
  va_end (args);

  dbus_connection_send (dbus_g_connection_get_connection (context->connection), reply, NULL);
  dbus_message_unref (reply);

  dbus_g_connection_unref (context->connection);
  dbus_g_message_unref (context->message);
  g_free (context);
  g_free (out_sig);
}

/**
 * Send a error message for a given method invocation.
 * This function also frees the sending context.
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
  g_free (context);
}

/** @} */ /* end of public API */

const char * _dbus_gobject_get_path (GObject *obj)
{
  return g_object_get_data (obj, "dbus_glib_object_path");
}

#ifdef DBUS_BUILD_TESTS
#include <stdlib.h>

static void
_dummy_function (void)
{
}

/* Data structures copied from one generated by current dbus-binding-tool;
 * we need to support this layout forever
 */
static const DBusGMethodInfo dbus_glib_internal_test_methods[] = {
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 0 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 49 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 117 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 191 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 270 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 320 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 391 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 495 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 623 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 693 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 765 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 838 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 911 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 988 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1064 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1140 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1204 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1278 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1347 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1408 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1460 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1533 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1588 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1647 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1730 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1784 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1833 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1895 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1947 },
  { (GCallback) _dummy_function, g_cclosure_marshal_VOID__VOID, 1999 },
};

const DBusGObjectInfo dbus_glib_internal_test_object_info = {
  0,
  dbus_glib_internal_test_methods,
  30,
"org.freedesktop.DBus.Tests.MyObject\0DoNothing\0S\0\0org.freedesktop.DBus.Tests.MyObject\0Increment\0S\0x\0I\0u\0arg1\0O\0F\0N\0u\0\0org.freedesktop.DBus.Tests.MyObject\0IncrementRetval\0S\0x\0I\0u\0arg1\0O\0F\0R\0u\0\0org.freedesktop.DBus.Tests.MyObject\0IncrementRetvalError\0S\0x\0I\0u\0arg1\0O\0F\0E\0u\0\0org.freedesktop.DBus.Tests.MyObject\0ThrowError\0S\0\0org.freedesktop.DBus.Tests.MyObject\0Uppercase\0S\0arg0\0I\0s\0arg1\0O\0F\0N\0s\0\0org.freedesktop.DBus.Tests.MyObject\0ManyArgs\0S\0x\0I\0u\0str\0I\0s\0trouble\0I\0d\0d_ret\0O\0F\0N\0d\0str_ret\0O\0F\0N\0s\0\0org.freedesktop.DBus.Tests.MyObject\0ManyReturn\0S\0arg0\0O\0F\0N\0u\0arg1\0O\0F\0N\0s\0arg2\0O\0F\0N\0i\0arg3\0O\0F\0N\0u\0arg4\0O\0F\0N\0u\0arg5\0O\0C\0N\0s\0\0org.freedesktop.DBus.Tests.MyObject\0Stringify\0S\0val\0I\0v\0arg1\0O\0F\0N\0s\0\0org.freedesktop.DBus.Tests.MyObject\0Unstringify\0S\0val\0I\0s\0arg1\0O\0F\0N\0v\0\0org.freedesktop.DBus.Tests.MyObject\0Recursive1\0S\0arg0\0I\0au\0arg1\0O\0F\0N\0u\0\0org.freedesktop.DBus.Tests.MyObject\0Recursive2\0S\0arg0\0I\0u\0arg1\0O\0F\0N\0au\0\0org.freedesktop.DBus.Tests.MyObject\0ManyUppercase\0S\0arg0\0I\0as\0arg1\0O\0F\0N\0as\0\0org.freedesktop.DBus.Tests.MyObject\0StrHashLen\0S\0arg0\0I\0a{ss}\0arg1\0O\0F\0N\0u\0\0org.freedesktop.DBus.Tests.MyObject\0SendCar\0S\0arg0\0I\0(suv)\0arg1\0O\0F\0N\0(uo)\0\0org.freedesktop.DBus.Tests.MyObject\0GetHash\0S\0arg0\0O\0F\0N\0a{ss}\0\0org.freedesktop.DBus.Tests.MyObject\0RecArrays\0S\0val\0I\0aas\0arg1\0O\0F\0N\0aau\0\0org.freedesktop.DBus.Tests.MyObject\0Objpath\0S\0arg0\0I\0o\0arg1\0O\0C\0N\0o\0\0org.freedesktop.DBus.Tests.MyObject\0GetObjs\0S\0arg0\0O\0F\0N\0ao\0\0org.freedesktop.DBus.Tests.MyObject\0IncrementVal\0S\0\0org.freedesktop.DBus.Tests.MyObject\0AsyncIncrement\0A\0x\0I\0u\0arg1\0O\0F\0N\0u\0\0org.freedesktop.DBus.Tests.MyObject\0AsyncThrowError\0A\0\0org.freedesktop.DBus.Tests.MyObject\0GetVal\0S\0arg0\0O\0F\0N\0u\0\0org.freedesktop.DBus.Tests.MyObject\0ManyStringify\0S\0arg0\0I\0a{sv}\0arg1\0O\0F\0N\0a{sv}\0\0org.freedesktop.DBus.Tests.MyObject\0EmitFrobnicate\0S\0\0org.freedesktop.DBus.Tests.MyObject\0Terminate\0S\0\0org.freedesktop.DBus.Tests.FooObject\0GetValue\0S\0arg0\0O\0F\0N\0u\0\0org.freedesktop.DBus.Tests.FooObject\0EmitSignals\0S\0\0org.freedesktop.DBus.Tests.FooObject\0EmitSignal2\0S\0\0org.freedesktop.DBus.Tests.FooObject\0Terminate\0S\0\0\0",
"org.freedesktop.DBus.Tests.MyObject\0Frobnicate\0org.freedesktop.DBus.Tests.FooObject\0Sig0\0org.freedesktop.DBus.Tests.FooObject\0Sig1\0org.freedesktop.DBus.Tests.FooObject\0Sig2\0\0",
"\0"
};


/**
 * @ingroup DBusGLibInternals
 * Unit test for GLib GObject integration ("skeletons")
 * @returns #TRUE on success.
 */
gboolean
_dbus_gobject_test (const char *test_data_dir)
{
  int i;
  const char *arg;
  const char *arg_name;
  gboolean arg_in;
  gboolean constval;
  RetvalType retval;
  const char *arg_signature;
  const char *sigdata;
  const char *iface;
  const char *signame;
  
  static struct { const char *wincaps; const char *uscore; } name_pairs[] = {
    { "SetFoo", "set_foo" },
    { "Foo", "foo" },
    { "GetFooBar", "get_foo_bar" },
    { "Hello", "hello" }
    
    /* Impossible-to-handle cases */
    /* { "FrobateUIHandler", "frobate_ui_handler" } */
  };

  /* Test lookup in our hardcoded object info; if these tests fail
   * then it likely means you changed the generated object info in an
   * incompatible way and broke the lookup functions.  In that case
   * you need to bump the version and use a new structure instead. */
  /* DoNothing */
  arg = method_arg_info_from_object_info (&dbus_glib_internal_test_object_info,
					  &(dbus_glib_internal_test_methods[0]));
  g_assert (*arg == '\0');

  /* Increment */
  arg = method_arg_info_from_object_info (&dbus_glib_internal_test_object_info,
					  &(dbus_glib_internal_test_methods[1]));
  g_assert (*arg != '\0');
  arg = arg_iterate (arg, &arg_name, &arg_in, &constval, &retval, &arg_signature);
  g_assert (!strcmp (arg_name, "x"));
  g_assert (arg_in == TRUE);
  g_assert (!strcmp (arg_signature, "u"));
  g_assert (*arg != '\0');
  arg = arg_iterate (arg, &arg_name, &arg_in, &constval, &retval, &arg_signature);
  g_assert (arg_in == FALSE);
  g_assert (retval == RETVAL_NONE);
  g_assert (!strcmp (arg_signature, "u"));
  g_assert (*arg == '\0');

  /* IncrementRetval */
  arg = method_arg_info_from_object_info (&dbus_glib_internal_test_object_info,
					  &(dbus_glib_internal_test_methods[2]));
  g_assert (*arg != '\0');
  arg = arg_iterate (arg, &arg_name, &arg_in, &constval, &retval, &arg_signature);
  g_assert (!strcmp (arg_name, "x"));
  g_assert (arg_in == TRUE);
  g_assert (!strcmp (arg_signature, "u"));
  g_assert (*arg != '\0');
  arg = arg_iterate (arg, &arg_name, &arg_in, &constval, &retval, &arg_signature);
  g_assert (retval == RETVAL_NOERROR);
  g_assert (arg_in == FALSE);
  g_assert (!strcmp (arg_signature, "u"));
  g_assert (*arg == '\0');

  /* IncrementRetvalError */
  arg = method_arg_info_from_object_info (&dbus_glib_internal_test_object_info,
					  &(dbus_glib_internal_test_methods[3]));
  g_assert (*arg != '\0');
  arg = arg_iterate (arg, &arg_name, &arg_in, &constval, &retval, &arg_signature);
  g_assert (!strcmp (arg_name, "x"));
  g_assert (arg_in == TRUE);
  g_assert (!strcmp (arg_signature, "u"));
  g_assert (*arg != '\0');
  arg = arg_iterate (arg, &arg_name, &arg_in, &constval, &retval, &arg_signature);
  g_assert (retval == RETVAL_ERROR);
  g_assert (arg_in == FALSE);
  g_assert (!strcmp (arg_signature, "u"));
  g_assert (*arg == '\0');
  
  /* Stringify */
  arg = method_arg_info_from_object_info (&dbus_glib_internal_test_object_info,
					  &(dbus_glib_internal_test_methods[8]));
  g_assert (*arg != '\0');
  arg = arg_iterate (arg, &arg_name, &arg_in, &constval, &retval, &arg_signature);
  g_assert (!strcmp (arg_name, "val"));
  g_assert (arg_in == TRUE);
  g_assert (!strcmp (arg_signature, "v"));
  g_assert (*arg != '\0');
  arg = arg_iterate (arg, &arg_name, &arg_in, &constval, &retval, &arg_signature);
  g_assert (retval == RETVAL_NONE);
  g_assert (arg_in == FALSE);
  g_assert (!strcmp (arg_signature, "s"));
  g_assert (*arg == '\0');

  sigdata = dbus_glib_internal_test_object_info.exported_signals;
  g_assert (*sigdata != '\0');
  sigdata = propsig_iterate (sigdata, &iface, &signame);
  g_assert (!strcmp (iface, "org.freedesktop.DBus.Tests.MyObject"));
  g_assert (!strcmp (signame, "Frobnicate"));
  g_assert (*sigdata != '\0');
  sigdata = propsig_iterate (sigdata, &iface, &signame);
  g_assert (!strcmp (iface, "org.freedesktop.DBus.Tests.FooObject"));
  g_assert (!strcmp (signame, "Sig0"));
  g_assert (*sigdata != '\0');
  sigdata = propsig_iterate (sigdata, &iface, &signame);
  g_assert (!strcmp (iface, "org.freedesktop.DBus.Tests.FooObject"));
  g_assert (!strcmp (signame, "Sig1"));
  g_assert (*sigdata != '\0');
  sigdata = propsig_iterate (sigdata, &iface, &signame);
  g_assert (!strcmp (iface, "org.freedesktop.DBus.Tests.FooObject"));
  g_assert (!strcmp (signame, "Sig2"));
  g_assert (*sigdata == '\0');
  

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
