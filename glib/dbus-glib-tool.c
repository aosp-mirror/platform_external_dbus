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
#include <locale.h>
#include <libintl.h>
#define _(x) dgettext (GETTEXT_PACKAGE, x)
#define N_(x) x
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DBUS_BUILD_TESTS
static void run_all_tests (const char *test_data_dir);
#endif

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

        g_assert (name != NULL);

        printf (_("interface \"%s\" {\n"), name);

        pretty_print_list (interface_info_get_methods (i), depth + 1);
        pretty_print_list (interface_info_get_signals (i), depth + 1);

        indent (depth);
        printf ("}\n");
      }
      break;
    case INFO_TYPE_METHOD:
      {
        MethodInfo *m = (MethodInfo*) base;

        g_assert (name != NULL);

        printf (_("method \"%s\" (\n"), name);

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
    case INFO_TYPE_ARG:
      {
        ArgInfo *a = (ArgInfo*) base;
        int at = arg_info_get_type (a);
        ArgDirection d = arg_info_get_direction (a);

        printf ("%s %s",
                d == ARG_IN ? "in" : "out",
                _dbus_gutils_type_to_string (at));
        if (name)
          printf (" %s\n", name);
        else
          printf ("\n");
      }
      break;
    }
}

static void
usage (int ecode)
{
  fprintf (stderr, "dbus-glib-tool [--version] [--help]\n");
  exit (ecode);
}

static void
version (void)
{
  printf ("D-BUS GLib Tool %s\n"
          "Copyright (C) 2003, 2004 Red Hat, Inc.\n"
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
  gboolean just_pretty_print;

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, DBUS_LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE); 

  just_pretty_print = FALSE;
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
#ifdef DBUS_BUILD_TESTS
          else if (strcmp (arg, "--self-test") == 0)
            run_all_tests (NULL);
#endif /* DBUS_BUILD_TESTS */
          else if (strcmp (arg, "--pretty-print") == 0)
            just_pretty_print = TRUE;
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
          fprintf (stderr, _("Unable to load \"%s\": %s\n"),
                   filename, error->message);
          g_error_free (error);
          exit (1);
        }
      else if (just_pretty_print)
        {
          pretty_print ((BaseInfo*) node, 0);
        }
      else
        {
          /* FIXME process the file to generate metadata variable
           * definition rather than just printing it.
           * i.e. we want to create DBusGObjectInfo.
           * This probably requires extending the introspection XML format to
           * allow a "native function name":
           *  <method name="Frobate" native="my_object_frobate">
           */
          pretty_print ((BaseInfo*) node, 0);
        }

      if (node)
        node_info_unref (node);
      
      tmp = tmp->next;
    }
  
  return 0;
}


#ifdef DBUS_BUILD_TESTS
static void
test_die (const char *failure)
{
  fprintf (stderr, "Unit test failed: %s\n", failure);
  exit (1);
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

  printf ("%s: running gtool tests\n", "dbus-glib-tool");
  if (!_dbus_gtool_test (test_data_dir))
    test_die ("gtool");

  printf ("%s: completed successfully\n", "dbus-glib-tool");
}

#endif /* DBUS_BUILD_TESTS */
