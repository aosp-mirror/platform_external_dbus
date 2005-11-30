/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-glib-tool.c Tool used by apps using glib bindings
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
#include "dbus-gidl.h"
#include "dbus-gparser.h"
#include "dbus-gutils.h"
#include "dbus-glib-tool.h"
#include "dbus-binding-tool-glib.h"
#include <locale.h>
#include <libintl.h>
#define _(x) dgettext (GETTEXT_PACKAGE, x)
#define N_(x) x
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>

#ifdef DBUS_BUILD_TESTS
static void run_all_tests (const char *test_data_dir);
#endif

typedef enum {
  DBUS_BINDING_OUTPUT_NONE,
  DBUS_BINDING_OUTPUT_PRETTY,
  DBUS_BINDING_OUTPUT_GLIB_SERVER,
  DBUS_BINDING_OUTPUT_GLIB_CLIENT
} DBusBindingOutputMode;

static void
indent (int depth)
{
  depth *= 2; /* 2-space indent */
  
  while (depth > 0)
    {
      putc (' ', stdout);
      --depth;
    }
}

static void pretty_print (BaseInfo *base,
                          int       depth);

static void
pretty_print_list (GSList *list,
                   int     depth)
{
  GSList *tmp;
  
  tmp = list;
  while (tmp != NULL)
    {
      pretty_print (tmp->data, depth);
      tmp = tmp->next;
    }
}

static void
pretty_print (BaseInfo *base,
              int       depth)
{
  InfoType t;
  const char *name;

  t = base_info_get_type (base);
  name = base_info_get_name (base);

  indent (depth);
  
  switch (t)
    {
    case INFO_TYPE_NODE:
      {
        NodeInfo *n = (NodeInfo*) base;
        
        if (name == NULL)
          printf (_("<anonymous node> {\n"));
        else
          printf (_("node \"%s\" {\n"), name);

        pretty_print_list (node_info_get_interfaces (n), depth + 1);
        pretty_print_list (node_info_get_nodes (n), depth + 1);

        indent (depth);
        printf ("}\n");
      }
      break;
    case INFO_TYPE_INTERFACE:
      {
        InterfaceInfo *i = (InterfaceInfo*) base;
	GSList *annotations, *elt;

        g_assert (name != NULL);

        printf (_("interface \"%s\" {\n"), name);

	annotations = interface_info_get_annotations (i);
	for (elt = annotations; elt; elt = elt->next)
	  {
	    const char *name = elt->data;
	    const char *value = interface_info_get_annotation (i, name);

	    printf (_(" (binding \"%s\": \"%s\") "),
		    name, value);
	  }
	g_slist_free (annotations);

        pretty_print_list (interface_info_get_methods (i), depth + 1);
        pretty_print_list (interface_info_get_signals (i), depth + 1);
        pretty_print_list (interface_info_get_properties (i), depth + 1);

        indent (depth);
        printf ("}\n");
      }
      break;
    case INFO_TYPE_METHOD:
      {
        MethodInfo *m = (MethodInfo*) base;
	GSList *annotations, *elt;

        g_assert (name != NULL);

	annotations = method_info_get_annotations (m);
        printf (_("method \"%s\""), name);
	for (elt = annotations; elt; elt = elt->next)
	  {
	    const char *name = elt->data;
	    const char *value = method_info_get_annotation (m, name);

	    printf (_(" (annotation \"%s\": \"%s\") "),
		    name, value);
	  }
	g_slist_free (annotations);

        pretty_print_list (method_info_get_args (m), depth + 1);

        indent (depth);
        printf (")\n");
      }
      break;
    case INFO_TYPE_SIGNAL:
      {
        SignalInfo *s = (SignalInfo*) base;

        g_assert (name != NULL);

        printf (_("signal \"%s\" (\n"), name);

        pretty_print_list (signal_info_get_args (s), depth + 1);

        indent (depth);
        printf (")\n");
      }
      break;
    case INFO_TYPE_PROPERTY:
      {
        PropertyInfo *a = (PropertyInfo*) base;
        const char *pt = property_info_get_type (a);
        PropertyAccessFlags acc = property_info_get_access (a);

        printf ("%s%s %s",
                acc & PROPERTY_READ ? "read" : "",
                acc & PROPERTY_WRITE ? "write" : "",
                pt);
        if (name)
          printf (" %s\n", name);
        else
          printf ("\n");
      }
      break;
    case INFO_TYPE_ARG:
      {
        ArgInfo *a = (ArgInfo*) base;
        const char *at = arg_info_get_type (a);
        ArgDirection d = arg_info_get_direction (a);

        printf ("%s %s",
                d == ARG_IN ? "in" : "out",
                at);
        if (name)
          printf (" %s\n", name);
        else
          printf ("\n");
      }
      break;
    }
}

GQuark
dbus_binding_tool_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("dbus_binding_tool_error");

  return quark;
}

static void lose (const char *fmt, ...) G_GNUC_NORETURN G_GNUC_PRINTF (1, 2);
static void lose_gerror (const char *prefix, GError *error) G_GNUC_NORETURN;

static void
lose (const char *str, ...)
{
  va_list args;

  va_start (args, str);

  vfprintf (stderr, str, args);
  fputc ('\n', stderr);

  va_end (args);

  exit (1);
}

static void
lose_gerror (const char *prefix, GError *error) 
{
  lose ("%s: %s", prefix, error->message);
}

static void
usage (int ecode)
{
  fprintf (stderr, "dbus-binding-tool [--version] [--help] [--pretty-print]\n");
  exit (ecode);
}

static void
version (void)
{
  printf ("D-BUS Binding Tool %s\n"
          "Copyright (C) 2003-2005 Red Hat, Inc.\n"
          "This is free software; see the source for copying conditions.\n"
          "There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n",
          VERSION);
  exit (0);
}

int
main (int argc, char **argv)
{
  const char *output_file;
  const char *prefix;
  char *output_file_tmp;
  int i;
  GSList *files;
  DBusBindingOutputMode outputmode;
  gboolean end_of_args;
  GSList *tmp;
  GIOChannel *channel;
  GError *error;
  time_t newest_src;
  struct stat srcbuf;
  struct stat targetbuf;
  gboolean force;
  gboolean ignore_unsupported;

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, DBUS_LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE); 

  g_type_init ();

  outputmode = DBUS_BINDING_OUTPUT_NONE;
  end_of_args = FALSE;
  files = NULL;
  output_file = NULL;
  prefix = "";
  ignore_unsupported = FALSE;
  force = FALSE;
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
          else if (strcmp (arg, "--force") == 0)
            force = TRUE;
#ifdef DBUS_BUILD_TESTS
          else if (strcmp (arg, "--self-test") == 0)
            run_all_tests (NULL);
#endif /* DBUS_BUILD_TESTS */
          else if (strncmp (arg, "--mode=", 7) == 0)
            {
	      const char *mode = arg + 7;
	      if (!strcmp (mode, "pretty"))
		outputmode = DBUS_BINDING_OUTPUT_PRETTY;
	      else if (!strcmp (mode, "glib-server"))
		outputmode = DBUS_BINDING_OUTPUT_GLIB_SERVER;
	      else if (!strcmp (mode, "glib-client"))
		outputmode = DBUS_BINDING_OUTPUT_GLIB_CLIENT;
	      else
		usage (1);
	    }
          else if (strcmp (arg, "--ignore-unsupported") == 0)
            ignore_unsupported = TRUE;
	  else if (strncmp (arg, "--output=", 9) == 0)
	    {
	      output_file = arg + 9;
	    }
          else if (strncmp (arg, "--prefix=", 9) == 0)
            {
              prefix = arg + 9;
            }
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
      
      ++i;
    }

  error = NULL;

  files = g_slist_reverse (files);

  if (output_file && !force)
    {
      newest_src = 0;
      for (tmp = files; tmp != NULL; tmp = tmp->next)
	{
	  const char *filename;

	  filename = tmp->data;
	  if (stat (filename, &srcbuf) < 0)
	    lose ("Couldn't stat %s: %s", filename, g_strerror (errno));

	  if (srcbuf.st_mtime > newest_src)
	    newest_src = srcbuf.st_mtime;
	}

      if (stat (output_file, &targetbuf) > 0
	  && targetbuf.st_mtime >= newest_src)
	exit (0);
    }
  
  if (output_file)
    {
      output_file_tmp = g_strconcat (output_file, ".tmp", NULL);

      if (!(channel = g_io_channel_new_file (output_file_tmp, "w", &error)))
	lose_gerror (_("Couldn't open temporary file"), error);
    }
  else
    {
      channel = g_io_channel_unix_new (fileno (stdout));
      output_file_tmp = NULL; /* silence gcc */
    }
  if (!g_io_channel_set_encoding (channel, NULL, &error))
    lose_gerror (_("Couldn't set channel encoding to NULL"), error);


  for (tmp = files; tmp != NULL; tmp = tmp->next)
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
	  lose_gerror (_("Unable to load \"%s\""), error);
        }
      else
	{
	  switch (outputmode)
	    {
	    case DBUS_BINDING_OUTPUT_PRETTY:
	      pretty_print ((BaseInfo*) node, 0);
	      break;
	    case DBUS_BINDING_OUTPUT_GLIB_SERVER:
	      if (!dbus_binding_tool_output_glib_server ((BaseInfo *) node, channel, prefix, &error))
		lose_gerror (_("Compilation failed"), error);
	      break;
	    case DBUS_BINDING_OUTPUT_GLIB_CLIENT:
	      if (!dbus_binding_tool_output_glib_client ((BaseInfo *) node, channel, ignore_unsupported, &error))
		lose_gerror (_("Compilation failed"), error);
	      break;
	    case DBUS_BINDING_OUTPUT_NONE:
	      break;
	    }
	}

      if (node)
        node_info_unref (node);
    }

  if (g_io_channel_shutdown (channel, TRUE, &error) != G_IO_STATUS_NORMAL)
    lose_gerror (_("Failed to shutdown IO channel"), error);
  g_io_channel_unref (channel);

  if (output_file)
    {
      if (rename (output_file_tmp, output_file) < 0)
	lose ("Failed to rename %s to %s: %s", output_file_tmp, output_file,
	      g_strerror (errno));
      g_free (output_file_tmp);
    }

  return 0;
}


#ifdef DBUS_BUILD_TESTS
static void
test_die (const char *failure)
{
  lose ("Unit test failed: %s", failure);
}

/**
 * @ingroup DBusGTool
 * Unit test for GLib utility tool
 * @returns #TRUE on success.
 */
static gboolean
_dbus_gtool_test (const char *test_data_dir)
{

  return TRUE;
}

static void
run_all_tests (const char *test_data_dir)
{
  if (test_data_dir == NULL)
    test_data_dir = g_getenv ("DBUS_TEST_DATA");

  if (test_data_dir != NULL)
    printf ("Test data in %s\n", test_data_dir);
  else
    printf ("No test data!\n");

  printf ("%s: running binding tests\n", "dbus-binding-tool");
  if (!_dbus_gtool_test (test_data_dir))
    test_die ("gtool");

  printf ("%s: completed successfully\n", "dbus-binding-tool");
}

#endif /* DBUS_BUILD_TESTS */
