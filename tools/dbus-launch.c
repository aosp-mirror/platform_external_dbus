/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-launch.c  dbus-launch utility
 *
 * Copyright (C) 2003 Red Hat, Inc.
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
#include <config.h>
#include <dbus/dbus.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef DBUS_BUILD_X11
#include <X11/Xlib.h>
#endif

static void
usage (void)
{
  fprintf (stderr, "dbus-launch [--version] [--exit-with-session]\n");
  exit (1);
}

static void
version (void)
{
  printf ("D-BUS Message Bus Launcher %s\n"
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
  dbus_bool_t exit_with_session;
  int i;  

  exit_with_session = FALSE;
  
  prev_arg = NULL;
  i = 1;
  while (i < argc)
    {
      const char *arg = argv[i];
      
      if (strcmp (arg, "--help") == 0 ||
          strcmp (arg, "-h") == 0 ||
          strcmp (arg, "-?") == 0)
        usage ();
      else if (strcmp (arg, "--version") == 0)
        version ();
      else if (strcmp (arg, "--exit-with-session") == 0)
        exit_with_session = TRUE;
      else
        usage ();
      
      prev_arg = arg;
      
      ++i;
    }
  
  
  
  return 0;
}
