/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-binding-tool-glib.c: Output C glue
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
#include "dbus-gidl.h"
#include "dbus-gparser.h"
#include "dbus-gutils.h"
#include "dbus-gvalue.h"
#include "dbus-glib-tool.h"
#include "dbus-binding-tool-glib.h"
#include <glib/gi18n.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MARSHAL_PREFIX "dbus_glib_marshal"

typedef struct
{
  GIOChannel *channel;
  
  GError **error;
  
  GHashTable *generated;
} DBusBindingToolCData;

static gboolean gather_marshallers (BaseInfo *base, DBusBindingToolCData *data, GError **error);
static gboolean generate_glue (BaseInfo *base, DBusBindingToolCData *data, GError **error);
static gboolean generate_client_glue (BaseInfo *base, DBusBindingToolCData *data, GError **error);

static char *
compute_marshaller (MethodInfo *method, GError **error)
{
  GSList *elt;
  GString *ret;
  gboolean first;

  /* All methods required to return boolean for now;
   * will be conditional on method info later */
  ret = g_string_new ("BOOLEAN:");

  first = TRUE;
  /* Append input arguments */
  for (elt = method_info_get_args (method); elt; elt = elt->next)
    {
      ArgInfo *arg = elt->data;

      if (arg_info_get_direction (arg) == ARG_IN)
	{
	  const char *marshal_name = dbus_gvalue_genmarshal_name_from_type (arg_info_get_type (arg));
	  if (!marshal_name)
	    {
	      g_set_error (error,
			   DBUS_BINDING_TOOL_ERROR,
			   DBUS_BINDING_TOOL_ERROR_UNSUPPORTED_CONVERSION,
			   _("Unsupported conversion from D-BUS type %d to glib-genmarshal type"),
			   arg_info_get_type (arg));
	      g_string_free (ret, TRUE);
	      return NULL;
	    }
	  if (!first)
	    g_string_append (ret, ",");
	  else
	    first = FALSE;
	  g_string_append (ret, marshal_name);
	}
    }

  /* Append pointer for each out arg storage */
  for (elt = method_info_get_args (method); elt; elt = elt->next)
    {
      ArgInfo *arg = elt->data;

      if (arg_info_get_direction (arg) == ARG_OUT)
	{
	  if (!first)
	    g_string_append (ret, ",");
	  else
	    first = FALSE;
	  g_string_append (ret, "POINTER");
	}
    }

  /* Final GError parameter */
  if (!first)
    g_string_append (ret, ",");
  g_string_append (ret, "POINTER");

  return g_string_free (ret, FALSE);

}

static char *
compute_marshaller_name (MethodInfo *method, GError **error)
{
  GSList *elt;
  GString *ret;

  /* All methods required to return boolean for now;
   * will be conditional on method info later */
  ret = g_string_new (MARSHAL_PREFIX "_BOOLEAN_");

  /* Append input arguments */
  for (elt = method_info_get_args (method); elt; elt = elt->next)
    {
      ArgInfo *arg = elt->data;

      if (arg_info_get_direction (arg) == ARG_IN)
	{
	  const char *marshal_name;
	  int type; 

	  type = arg_info_get_type (arg);
	  marshal_name = dbus_gvalue_genmarshal_name_from_type (type);
	  if (!marshal_name)
	    {
	      g_set_error (error,
			   DBUS_BINDING_TOOL_ERROR,
			   DBUS_BINDING_TOOL_ERROR_UNSUPPORTED_CONVERSION,
			   _("Unsupported conversion from D-BUS type %d to glib-genmarshal type"),
			   type);
	      g_string_free (ret, TRUE);
	      return NULL;
	    }

	  g_string_append (ret, "_");
	  g_string_append (ret, dbus_gvalue_genmarshal_name_from_type (arg_info_get_type (arg)));
	}
    }

  /* Append pointer for each out arg storage */
  for (elt = method_info_get_args (method); elt; elt = elt->next)
    {
      ArgInfo *arg = elt->data;

      if (arg_info_get_direction (arg) == ARG_OUT)
	{
	  g_string_append (ret, "_POINTER");
	}
    }

  /* Final GError parameter */
  g_string_append (ret, "_POINTER");

  return g_string_free (ret, FALSE);
}

static gboolean
gather_marshallers_list (GSList *list, DBusBindingToolCData *data, GError **error)
{
  GSList *tmp;

  tmp = list;
  while (tmp != NULL)
    {
      if (!gather_marshallers (tmp->data, data, error))
	return FALSE;
      tmp = tmp->next;
    }
  return TRUE;
}

static gboolean
gather_marshallers (BaseInfo *base, DBusBindingToolCData *data, GError **error)
{
  if (base_info_get_type (base) == INFO_TYPE_NODE)
    {
      if (!gather_marshallers_list (node_info_get_nodes ((NodeInfo *) base),
				    data, error))
	return FALSE;
      if (!gather_marshallers_list (node_info_get_interfaces ((NodeInfo *) base),
				    data, error))
	return FALSE;
    }
  else
    {
      InterfaceInfo *interface;
      GSList *methods;
      GSList *tmp;
      const char *interface_c_name;

      interface = (InterfaceInfo *) base;
      interface_c_name = interface_info_get_binding_name (interface, "C");
      if (interface_c_name == NULL)
        {
          return TRUE;
        }

      methods = interface_info_get_methods (interface);

      /* Generate the necessary marshallers for the methods. */

      for (tmp = methods; tmp != NULL; tmp = g_slist_next (tmp))
        {
          MethodInfo *method;
          char *marshaller_name;

          method = (MethodInfo *) tmp->data;
          if (method_info_get_binding_name (method, "C") == NULL)
            {
              continue;
            }

          marshaller_name = compute_marshaller (method, error);
	  if (!marshaller_name)
	    return FALSE;

	  if (g_hash_table_lookup (data->generated, marshaller_name))
	    {
	      g_free (marshaller_name);
	      continue;
	    }

	  g_hash_table_insert (data->generated, marshaller_name, NULL);
        }

    }
  return TRUE;
}

static gboolean
generate_glue_list (GSList *list, DBusBindingToolCData *data, GError **error)
{
  GSList *tmp;

  tmp = list;
  while (tmp != NULL)
    {
      if (!generate_glue (tmp->data, data, error))
	return FALSE;
      tmp = tmp->next;
    }
  return TRUE;
}

#define WRITE_OR_LOSE(x) do { gsize bytes_written; if (!g_io_channel_write_chars (channel, x, -1, &bytes_written, error)) goto io_lose; } while (0)

static gboolean
write_printf_to_iochannel (const char *fmt, GIOChannel *channel, GError **error, ...)
{
  char *str;
  va_list args;
  GIOStatus status;
  gsize written;
  gboolean ret;

  va_start (args, error);

  str = g_strdup_vprintf (fmt, args);
  if ((status = g_io_channel_write_chars (channel, str, -1, &written, error)) == G_IO_STATUS_NORMAL)
    ret = TRUE;
  else
    ret = FALSE;

  g_free (str);

  va_end (args);

  return ret;
}

static gboolean
generate_glue (BaseInfo *base, DBusBindingToolCData *data, GError **error)
{
  if (base_info_get_type (base) == INFO_TYPE_NODE)
    {
      if (!generate_glue_list (node_info_get_nodes ((NodeInfo *) base),
			       data, error))
	return FALSE;
      if (!generate_glue_list (node_info_get_interfaces ((NodeInfo *) base),
			       data, error))
	return FALSE;
    }
  else
    {
      GIOChannel *channel;
      InterfaceInfo *interface;
      GSList *methods;
      GSList *tmp;
      gsize i;
      int count;
      const char *interface_c_name;
      GString *object_introspection_data_blob;

      channel = data->channel;

      interface = (InterfaceInfo *) base;
      interface_c_name = interface_info_get_binding_name (interface, "C");
      if (interface_c_name == NULL)
        {
          return TRUE;
        }

      object_introspection_data_blob = g_string_new_len ("", 0);

      methods = interface_info_get_methods (interface);
      count = 0;

      /* Table of marshalled methods. */

      if (!write_printf_to_iochannel ("static const DBusGMethodInfo dbus_glib_%s_methods[] = {\n", channel, error, interface_info_get_binding_name (interface, "C")))
	goto io_lose;
      for (tmp = methods; tmp != NULL; tmp = g_slist_next (tmp))
        {
          MethodInfo *method;
          char *marshaller_name;
	  const char *method_c_name;
	  GSList *args;

          method = (MethodInfo *) tmp->data;
	  method_c_name = method_info_get_binding_name (method, "C");
          if (method_c_name == NULL)
            {
              continue;
            }

          if (!write_printf_to_iochannel ("  { (GCallback) %s, ", channel, error,
					  method_c_name))
	    goto io_lose;

          marshaller_name = compute_marshaller_name (method, error);
	  if (!marshaller_name)
	    goto io_lose;

          if (!write_printf_to_iochannel ("%s, %d },\n", channel, error,
					  marshaller_name,
					  object_introspection_data_blob->len))
	    {
	      g_free (marshaller_name);
	      goto io_lose;
	    }

	  /* Object method data blob format:
	   * <iface>\0<name>\0(<argname>\0<argdirection>\0<argtype>\0)*\0
	   */

	  g_string_append (object_introspection_data_blob, interface_info_get_name (interface));
	  g_string_append_c (object_introspection_data_blob, '\0');

	  g_string_append (object_introspection_data_blob, method_info_get_name (method));
	  g_string_append_c (object_introspection_data_blob, '\0');

	  for (args = method_info_get_args (method); args; args = args->next)
	    {
	      ArgInfo *arg;
	      char direction;

	      arg = args->data;

	      g_string_append (object_introspection_data_blob, arg_info_get_name (arg));
	      g_string_append_c (object_introspection_data_blob, '\0');

	      switch (arg_info_get_direction (arg))
		{
		case ARG_IN:
		  direction = 'I';
		  break;
		case ARG_OUT:
		  direction = 'O';
		  break;
		case ARG_INVALID:
		  break;
		}
	      g_string_append_c (object_introspection_data_blob, direction);
	      g_string_append_c (object_introspection_data_blob, '\0');

	      g_string_append_c (object_introspection_data_blob, arg_info_get_type (arg));
	      g_string_append_c (object_introspection_data_blob, '\0');
	    }

	  g_string_append_c (object_introspection_data_blob, '\0');

          count++;
        }
      WRITE_OR_LOSE ("};\n\n");

      /* Information about the object. */

      if (!write_printf_to_iochannel ("const DBusGObjectInfo dbus_glib_%s_object_info = {\n",
				      channel, error, interface_c_name))
	goto io_lose;
      WRITE_OR_LOSE ("  0,\n");
      if (!write_printf_to_iochannel ("  dbus_glib_%s_methods,\n", channel, error, interface_c_name))
	goto io_lose;
      if (!write_printf_to_iochannel ("  %d,\n", channel, error, count))
	goto io_lose;
      WRITE_OR_LOSE("  \"");
      for (i = 0; i < object_introspection_data_blob->len; i++)
	{
	  if (object_introspection_data_blob->str[i] != '\0')
	    {
	      if (!g_io_channel_write_chars (channel, object_introspection_data_blob->str + i, 1, NULL, error))
		return FALSE;
	    }
	  else
	    {
	      if (!g_io_channel_write_chars (channel, "\\0", -1, NULL, error))
		return FALSE;
	    }
	}
      WRITE_OR_LOSE ("\"\n};\n\n");

      g_string_free (object_introspection_data_blob, TRUE);
    }
  return TRUE;
 io_lose:
  return FALSE;
}

static void
write_marshaller (gpointer key, gpointer value, gpointer user_data)
{
  DBusBindingToolCData *data;
  const char *marshaller;
  gsize bytes_written;

  data = user_data;
  marshaller = key;

  if (data->error && *data->error)
    return;

  if (g_io_channel_write_chars (data->channel, marshaller, -1, &bytes_written, data->error) == G_IO_STATUS_NORMAL)
    g_io_channel_write_chars (data->channel, "\n", -1, &bytes_written, data->error);
}

gboolean
dbus_binding_tool_output_glib_server (BaseInfo *info, GIOChannel *channel, GError **error)
{
  gboolean ret;
  GPtrArray *argv;
  gint child_stdout;
  GIOChannel *genmarshal_stdout;
  GPid child_pid;
  DBusBindingToolCData data;
  char *tempfile_name;
  gint tempfile_fd;
  GIOStatus iostatus;
  char buf[4096];
  gsize bytes_read, bytes_written;

  memset (&data, 0, sizeof (data));

  data.generated = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);
  data.error = error;
  genmarshal_stdout = NULL;
  tempfile_name = NULL;

  if (!gather_marshallers (info, &data, error))
    goto io_lose;

  tempfile_fd = g_file_open_tmp ("dbus-binding-tool-c-marshallers.XXXXXX",
				 &tempfile_name, error);
  if (tempfile_fd < 0)
    goto io_lose;

  data.channel = g_io_channel_unix_new (tempfile_fd);
  if (!g_io_channel_set_encoding (data.channel, NULL, error))
    goto io_lose;
  g_hash_table_foreach (data.generated, write_marshaller, &data); 
  if (error && *error != NULL)
    {
      ret = FALSE;
      g_io_channel_close (data.channel);
      g_io_channel_unref (data.channel);
      goto io_lose;
    }

  g_io_channel_close (data.channel);
  g_io_channel_unref (data.channel);
  
  /* Now spawn glib-genmarshal to insert all our required marshallers */
  argv = g_ptr_array_new ();
  g_ptr_array_add (argv, "glib-genmarshal");
  g_ptr_array_add (argv, "--header");
  g_ptr_array_add (argv, "--body");
  g_ptr_array_add (argv, "--prefix=" MARSHAL_PREFIX);
  g_ptr_array_add (argv, tempfile_name);
  g_ptr_array_add (argv, NULL);
  if (!g_spawn_async_with_pipes (NULL, (char**)argv->pdata, NULL,
				 G_SPAWN_SEARCH_PATH,
				 NULL, NULL,
				 &child_pid,
				 NULL,
				 &child_stdout, NULL, error))
    {
      g_ptr_array_free (argv, TRUE);
      goto io_lose;
    }
  g_ptr_array_free (argv, TRUE);

  genmarshal_stdout = g_io_channel_unix_new (child_stdout);
  if (!g_io_channel_set_encoding (genmarshal_stdout, NULL, error))
    goto io_lose;

  WRITE_OR_LOSE ("/* Generated by dbus-binding-tool; do not edit! */\n\n");

  while ((iostatus = g_io_channel_read_chars (genmarshal_stdout, buf, sizeof (buf),
					      &bytes_read, error)) == G_IO_STATUS_NORMAL)
    if (g_io_channel_write_chars (channel, buf, bytes_read, &bytes_written, error) != G_IO_STATUS_NORMAL)
      goto io_lose;
  if (iostatus != G_IO_STATUS_EOF)
    goto io_lose;

  g_io_channel_close (genmarshal_stdout);

  WRITE_OR_LOSE ("#include <dbus/dbus-glib.h>\n");

  g_io_channel_ref (data.channel);
  data.channel = channel;
  if (!generate_glue (info, &data, error))
    goto io_lose;
  
  ret = TRUE;
 cleanup:
  if (tempfile_name)
    unlink (tempfile_name);
  g_free (tempfile_name);
  if (genmarshal_stdout)
    g_io_channel_unref (genmarshal_stdout);
  if (data.channel)
    g_io_channel_unref (data.channel);
  g_hash_table_destroy (data.generated);

  return ret;
 io_lose:
  ret = FALSE;
  goto cleanup;
}

static char *
iface_to_c_prefix (const char *iface)
{
  char **components;
  char **component;
  GString *ret;
  gboolean first;
  
  components = g_strsplit (iface, ".", 0);

  first = TRUE;
  ret = g_string_new ("");
  for (component = components; *component; component++)
    {
      if (!first)
	g_string_append_c (ret, '_');
      else
	first = FALSE;
      g_string_append (ret, *component);
    }
  g_strfreev (components);
  return g_string_free (ret, FALSE);
}

static char *
compute_client_method_name (InterfaceInfo *iface, MethodInfo *method)
{
  GString *ret;
  char *method_name_uscored;
  char *iface_prefix;

  iface_prefix = iface_to_c_prefix (interface_info_get_name (iface));
  ret = g_string_new (iface_prefix);
  g_free (iface_prefix);
  
  method_name_uscored = _dbus_gutils_wincaps_to_uscore (method_info_get_name (method));
  g_string_append_c (ret, '_');
  g_string_append (ret, method_name_uscored);
  g_free (method_name_uscored);
  return g_string_free (ret, FALSE);
}

static gboolean
write_formal_parameters (InterfaceInfo *iface, MethodInfo *method, GIOChannel *channel, GError **error)
{
  GSList *args;

  for (args = method_info_get_args (method); args; args = args->next)
    {
      ArgInfo *arg;
      const char *type_str;
      int direction;

      arg = args->data;

      WRITE_OR_LOSE (", ");

      direction = arg_info_get_direction (arg);

      /* FIXME - broken for containers */
      type_str = dbus_gvalue_ctype_from_type (arg_info_get_type (arg), direction == ARG_IN);

      if (!type_str)
	{
	  g_set_error (error,
		       DBUS_BINDING_TOOL_ERROR,
		       DBUS_BINDING_TOOL_ERROR_UNSUPPORTED_CONVERSION,
		       _("Unsupported conversion from D-BUS type %d to glib C type"),
		       arg_info_get_type (arg));
	  return FALSE;
	}

      switch (direction)
	{
	case ARG_IN:
	  if (!write_printf_to_iochannel ("%s IN_%s", channel, error,
					  type_str,
					  arg_info_get_name (arg)))
	    goto io_lose;
	  break;
	case ARG_OUT:
	  if (!write_printf_to_iochannel ("%s* OUT_%s", channel, error,
					  type_str,
					  arg_info_get_name (arg)))
	    goto io_lose;
	  break;
	case ARG_INVALID:
	  break;
	}
    }

  return TRUE;
 io_lose:
  return FALSE;
}

static gboolean
write_args_for_direction (InterfaceInfo *iface, MethodInfo *method, GIOChannel *channel, int direction, GError **error)
{
  GSList *args;

  for (args = method_info_get_args (method); args; args = args->next)
    {
      ArgInfo *arg;
      const char *type_str;

      arg = args->data;

      if (direction != arg_info_get_direction (arg))
	continue;

      /* FIXME - broken for containers */
      type_str = dbus_gvalue_binding_type_from_type (arg_info_get_type (arg));
      if (!type_str)
	{
	  g_set_error (error,
		       DBUS_BINDING_TOOL_ERROR,
		       DBUS_BINDING_TOOL_ERROR_UNSUPPORTED_CONVERSION,
		       _("Unsupported conversion from D-BUS type %c"),
		       (char) arg_info_get_type (arg));
	  return FALSE;
	}

      
      switch (direction)
	{
	case ARG_IN:
	  if (!write_printf_to_iochannel ("                                  %s, &IN_%s,\n", channel, error,
					  type_str, arg_info_get_name (arg)))
	    goto io_lose;
	  break;
	case ARG_OUT:
	  if (!write_printf_to_iochannel ("                               %s, OUT_%s,\n", channel, error,
					  type_str, arg_info_get_name (arg)))
	    goto io_lose;
	  break;
	case ARG_INVALID:
	  break;
	}
    }

  return TRUE;
 io_lose:
  return FALSE;
}

static gboolean
generate_client_glue_list (GSList *list, DBusBindingToolCData *data, GError **error)
{
  GSList *tmp;

  tmp = list;
  while (tmp != NULL)
    {
      if (!generate_client_glue (tmp->data, data, error))
	return FALSE;
      tmp = tmp->next;
    }
  return TRUE;
}

static gboolean
generate_client_glue (BaseInfo *base, DBusBindingToolCData *data, GError **error)
{
  if (base_info_get_type (base) == INFO_TYPE_NODE)
    {
      if (!generate_client_glue_list (node_info_get_nodes ((NodeInfo *) base),
				      data, error))
	return FALSE;
      if (!generate_client_glue_list (node_info_get_interfaces ((NodeInfo *) base),
				      data, error))
	return FALSE;
    }
  else
    {
      GIOChannel *channel;
      InterfaceInfo *interface;
      GSList *methods;
      GSList *tmp;
      int count;

      channel = data->channel;

      interface = (InterfaceInfo *) base;

      methods = interface_info_get_methods (interface);
      count = 0;

      for (tmp = methods; tmp != NULL; tmp = g_slist_next (tmp))
        {
          MethodInfo *method;
	  char *method_name;

          method = (MethodInfo *) tmp->data;

	  method_name = compute_client_method_name (interface, method);

	  WRITE_OR_LOSE ("static gboolean\n");
	  if (!write_printf_to_iochannel ("%s (DBusGProxy *proxy", channel, error,
					  method_name))
	    goto io_lose;
	  g_free (method_name);

	  if (!write_formal_parameters (interface, method, channel, error))
	    goto io_lose;

	  WRITE_OR_LOSE (", GError **error)\n\n");
	  
	  WRITE_OR_LOSE ("{\n");
	  WRITE_OR_LOSE ("  gboolean ret;\n\n");
	  WRITE_OR_LOSE ("  DBusGPendingCall *call;\n\n");
	  
	  if (!write_printf_to_iochannel ("  call = dbus_g_proxy_begin_call (proxy, \"%s\",\n",
					  channel, error,
					  method_info_get_name (method)))
	    goto io_lose;

	  if (!write_args_for_direction (interface, method, channel, ARG_IN, error))
	    goto io_lose;

	  WRITE_OR_LOSE ("                                  DBUS_TYPE_INVALID);\n");
	  WRITE_OR_LOSE ("  ret = dbus_g_proxy_end_call (proxy, call, error,\n");
	  
	  if (!write_args_for_direction (interface, method, channel, ARG_OUT, error))
	    goto io_lose;

	  WRITE_OR_LOSE ("                               DBUS_TYPE_INVALID);\n");

	  WRITE_OR_LOSE ("  dbus_g_pending_call_unref (call);\n");
	  WRITE_OR_LOSE ("  return ret;\n");

	  WRITE_OR_LOSE ("}\n\n");
	}
    }
  return TRUE;
 io_lose:
  return FALSE;
}


gboolean
dbus_binding_tool_output_glib_client (BaseInfo *info, GIOChannel *channel, GError **error)
{
  DBusBindingToolCData data;
  gboolean ret;

  memset (&data, 0, sizeof (data));
  
  data.channel = channel;

  WRITE_OR_LOSE ("/* Generated by dbus-binding-tool; do not edit! */\n\n");
  WRITE_OR_LOSE ("#include <glib/gtypes.h>\n");
  WRITE_OR_LOSE ("#include <glib/gerror.h>\n");
  WRITE_OR_LOSE ("#include <dbus/dbus-glib.h>\n\n");

  ret = generate_client_glue (info, &data, error);

  return ret;
 io_lose:
  return FALSE;
}
