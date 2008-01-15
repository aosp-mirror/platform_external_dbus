/* -*- mode: C; c-file-style: "gnu" -*- */
/* dir-watch-inotify.c  OS specific directory change notification for message bus
 *
 * Copyright (C) 2003 Red Hat, Inc.
 *           (c) 2006 Mandriva
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

#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#include <dbus/dbus-internals.h>
#include <dbus/dbus-watch.h>
#include "dir-watch.h"

#define MAX_DIRS_TO_WATCH 128

/* use a static array to avoid handling OOM */
static int wds[MAX_DIRS_TO_WATCH];
static int num_wds = 0;
static int inotify_fd = -1;
static DBusWatch *watch = NULL;
static DBusLoop *loop = NULL;

static dbus_bool_t
_inotify_watch_callback (DBusWatch *watch, unsigned int condition, void *data)
{
  return dbus_watch_handle (watch, condition);
}

static dbus_bool_t
_handle_inotify_watch (DBusWatch *watch, unsigned int flags, void *data)
{
  struct inotify_event ev;
  size_t res;
  pid_t pid;

  res = read (inotify_fd, &ev, sizeof(ev));

  if (res > 0)
    {
      pid = getpid ();
      _dbus_verbose ("Sending SIGHUP signal on reception of a inotify event\n");
      (void) kill (pid, SIGHUP);
    }
  else if (res < 0 && errno == EBADF)
    {
      if (watch != NULL)
	{
	  _dbus_loop_remove_watch (loop, watch, _inotify_watch_callback, NULL);
          _dbus_watch_unref (watch);
	  watch = NULL;
	}
      pid = getpid ();
      _dbus_verbose ("Sending SIGHUP signal since inotify fd has been closed\n");
      (void) kill (pid, SIGHUP);
    }

  return TRUE;
}
void
bus_watch_directory (const char *dir, BusContext *context)
{
  int wd;

  _dbus_assert (dir != NULL);

  if (inotify_fd == -1) {
     inotify_fd = inotify_init ();
     if (inotify_fd <= 0) {
      _dbus_warn ("Cannot initialize inotify\n");
      goto out;
     } 
     loop = bus_context_get_loop (context);

     watch = _dbus_watch_new (inotify_fd, DBUS_WATCH_READABLE, TRUE,
                              _handle_inotify_watch, NULL, NULL);

	if (watch == NULL)
          {
            _dbus_warn ("Unable to create inotify watch\n");
	    goto out;
	  }

	if (!_dbus_loop_add_watch (loop, watch, _inotify_watch_callback,
                                   NULL, NULL))
          {
            _dbus_warn ("Unable to add reload watch to main loop");
	    _dbus_watch_unref (watch);
	    watch = NULL;
            goto out;
	  }
  }


  if (num_wds >= MAX_DIRS_TO_WATCH )
    {
      _dbus_warn ("Cannot watch config directory '%s'. Already watching %d directories\n", dir, MAX_DIRS_TO_WATCH);
      goto out;
    }

  wd = inotify_add_watch (inotify_fd, dir, IN_MODIFY);
  if (wd < 0)
    {
      _dbus_warn ("Cannot setup inotify for '%s'; error '%s'\n", dir, _dbus_strerror (errno));
      goto out;
    }

  wds[num_wds++] = wd;
  _dbus_verbose ("Added watch on config directory '%s'\n", dir);

 out:
  ;
}

void 
bus_drop_all_directory_watches (void)
{
  int i;
 
  _dbus_verbose ("Dropping all watches on config directories\n");
 
  for (i = 0; i < num_wds; i++)
    {
      if (inotify_rm_watch(inotify_fd, wds[i]) != 0)
	{
	  _dbus_verbose ("Error closing fd %d for config directory watch\n", wds[i]);
	}
    }
  
  num_wds = 0;
}
