/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-launch.h  dbus-launch utility
 *
 * Copyright (C) 2006 Thiago Macieira <thiago@kde.org>
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
#include "dbus-launch.h"

#ifdef DBUS_BUILD_X11
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

Display *xdisplay;
static Atom selection_atom;
static Atom address_atom;
static Atom pid_atom;

static int
x_io_error_handler (Display *xdisplay)
{
  verbose ("X IO error\n");
  kill_bus_and_exit (0);
  return 0;
}

static char *
get_local_hostname (void)
{
  static const int increment = 128;
  static char *cache = NULL;
  char *buffer = NULL;
  int size = 0;

  while (cache == NULL)
    {
      size += increment;
      buffer = realloc (buffer, size);
      if (buffer == NULL)
        return NULL;            /* out of memory */

      if (gethostname (buffer, size - 1) == -1 &&
          errno != ENAMETOOLONG)
        return NULL;

      buffer[size - 1] = '\0';  /* to make sure */
      cache = buffer;
    }

  return cache;
}

static char *
get_session_file (void)
{
  static const char prefix[] = "/.dbus-session-file_";
  char *hostname;
  char *display;
  char *home;
  char *result;
  char *p;

  display = xstrdup (getenv ("DISPLAY"));
  if (display == NULL)
    {
      verbose ("X11 integration disabled because X11 is not running\n");
      return NULL;
    }

  /* remove the screen part of the display name */
  p = strrchr (display, ':');
  if (p != NULL)
    for ( ; *p; ++p)
      if (*p == '.')
        {
          *p = '\0';
          break;
        }

  /* replace the : in the display with _ */
  for (p = display; *p; ++p)
    if (*p == ':')
      *p = '_';

  hostname = get_local_hostname ();
  if (hostname == NULL)
    {
      /* out of memory */
      free (display);
      return NULL;
    }

  home = getenv ("HOME");
  if (home == NULL)
    {
      /* try from the user database */
      struct passwd *user = getpwuid (getuid());
      if (user == NULL)
        {
          verbose ("X11 integration disabled because the home directory"
                   " could not be determined\n");
          free (display);
          return NULL;
        }

      home = user->pw_dir;
    }

  result = malloc (strlen (home) + strlen (prefix) + strlen (hostname) +
                   strlen (display) + 2);
  if (result == NULL)
    {
      /* out of memory */
      free (display);
      return NULL;
    }

  strcpy (result, home);
  strcat (result, prefix);
  strcat (result, hostname);
  strcat (result, "_");
  strcat (result, display);
  free (display);

  verbose ("session file: %s\n", result);
  return result;
}

static Display *
open_x11 (void)
{
  if (xdisplay != NULL)
    return xdisplay;

  xdisplay = XOpenDisplay (NULL);
  if (xdisplay != NULL)
    {
      verbose ("Connected to X11 display '%s'\n", DisplayString (xdisplay));
      XSetIOErrorHandler (x_io_error_handler);
    }
  return xdisplay;
}

static int
init_x_atoms (Display *display)
{
  static const char selection_prefix[] = "DBUS_SESSION_SELECTION_";
  static const char address_prefix[] = "DBUS_SESSION_ADDRESS";
  static const char pid_prefix[] = "DBUS_SESSION_PID";
  static int init = FALSE;
  char *atom_name;
  char *hostname;
  char *user_name;
  struct passwd *user;

  if (init)
    return TRUE;

  user = getpwuid (getuid ());
  if (user == NULL)
    {
      verbose ("Could not determine the user informations; aborting X11 integration.\n");
      return FALSE;
    }
  user_name = xstrdup(user->pw_name);

  hostname = get_local_hostname ();
  if (hostname == NULL)
    {
      verbose ("Could not create X11 atoms; aborting X11 integration.\n");
      free (user_name);
      return FALSE;
    }

  atom_name = malloc (strlen (hostname) + strlen (user_name) + 2 +
                      MAX (strlen (selection_prefix),
                           MAX (strlen (address_prefix),
                                strlen (pid_prefix))));
  if (atom_name == NULL)
    {
      verbose ("Could not create X11 atoms; aborting X11 integration.\n");
      free (user_name);
      return FALSE;
    }

  /* create the selection atom */
  strcpy (atom_name, selection_prefix);
  strcat (atom_name, user_name);
  strcat (atom_name, "_");
  strcat (atom_name, hostname);
  selection_atom = XInternAtom (display, atom_name, FALSE);

  /* create the address property atom */
  strcpy (atom_name, address_prefix);
  address_atom = XInternAtom (display, atom_name, FALSE);

  /* create the PID property atom */
  strcpy (atom_name, pid_prefix);
  pid_atom = XInternAtom (display, atom_name, FALSE);

  free (atom_name);
  free (user_name);
  init = TRUE;
  return TRUE;
}

/*
 * Gets the daemon address from the X11 display.
 * Returns FALSE if there was an error. Returning
 * TRUE does not mean the address exists.
 */
int
x11_get_address (char **paddress, pid_t *pid, long *wid)
{
  Atom type;
  Window owner;
  int format;
  unsigned long items;
  unsigned long after;
  char *data;

  *paddress = NULL;

  /* locate the selection owner */
  owner = XGetSelectionOwner (xdisplay, selection_atom);
  if (owner == None)
    return TRUE;                /* no owner */
  if (wid != NULL)
    *wid = (long) owner;

  /* get the bus address */
  XGetWindowProperty (xdisplay, owner, address_atom, 0, 1024, False,
                      XA_STRING, &type, &format, &items, &after,
                      (unsigned char **) &data);
  if (type == None || after != 0 || data == NULL || format != 8)
    return FALSE;               /* error */

  *paddress = xstrdup (data);
  XFree (data);

  /* get the PID */
  if (pid != NULL)
    {
      *pid = 0;
      XGetWindowProperty (xdisplay, owner, pid_atom, 0, sizeof pid, False,
                          XA_CARDINAL, &type, &format, &items, &after,
                          (unsigned char **) &data);
      if (type != None && after == 0 && data != NULL && format == 32)
        *pid = (pid_t) *(long*) data;
      XFree (data);
    }

  return TRUE;                  /* success */
}

/*
 * Saves the address in the X11 display. Returns 0 on success.
 * If an error occurs, returns -1. If the selection already exists,
 * returns 1. (i.e. another daemon is already running)
 */
static Window
set_address_in_x11(char *address, pid_t pid)
{
  char *current_address;
  Window wid;

  /* lock the X11 display to make sure we're doing this atomically */
  XGrabServer (xdisplay);

  if (!x11_get_address (&current_address, NULL, NULL))
    {
      /* error! */
      XUngrabServer (xdisplay);
      return None;
    }

  if (current_address != NULL)
    {
      /* someone saved the address in the meantime */
      XUngrabServer (xdisplay);
      free (current_address);
      return None;
    }

  /* Create our window */
  wid = XCreateSimpleWindow (xdisplay, RootWindow (xdisplay, 0), -20, -20, 10, 10,
                             0, WhitePixel (xdisplay, 0),
                             BlackPixel (xdisplay, 0));
  verbose ("Created window %d\n", wid);

  /* Save the property in the window */
  XChangeProperty (xdisplay, wid, address_atom, XA_STRING, 8, PropModeReplace,
                   (unsigned char *)address, strlen (address));
  XChangeProperty (xdisplay, wid, pid_atom, XA_CARDINAL, 32, PropModeReplace,
                   (unsigned char *)&pid, sizeof(pid) / 4);

  /* Now grab the selection */
  XSetSelectionOwner (xdisplay, selection_atom, wid, CurrentTime);

  /* Ungrab the server to let other people use it too */
  XUngrabServer (xdisplay);

  XFlush (xdisplay);

  return wid;
}

/*
 * Saves the session address in session file. Returns TRUE on
 * success, FALSE if an error occurs.
 */
static int
set_address_in_file (char *address, pid_t pid, Window wid)
{
  char *session_file;
  FILE *f;

  session_file = get_session_file();
  if (session_file == NULL)
    return FALSE;

  f = fopen (session_file, "w");
  if (f == NULL)
    return FALSE;               /* some kind of error */
  fprintf (f, "%s\n%ld\n%ld\n", address, (long)pid, (long)wid);

  fclose (f);
  free (session_file);

  return TRUE;
}

int
x11_save_address (char *address, pid_t pid, long *wid)
{
  Window id = set_address_in_x11 (address, pid);
  if (id != None)
    {
      if (!set_address_in_file (address, pid, id))
        return FALSE;

      if (wid != NULL)
        *wid = (long) id;
      return TRUE;
    }
  return FALSE;
}

int
x11_init (void)
{
  return open_x11 () != NULL && init_x_atoms (xdisplay);
}

void
x11_handle_event (void)
{
  if (xdisplay != NULL)
    {      
      while (XPending (xdisplay))
        {
          XEvent ignored;
          XNextEvent (xdisplay, &ignored);
        }
    }
}  

#else
void dummy_dbus_launch_x11 (void) { }
#endif
