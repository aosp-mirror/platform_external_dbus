/* -*- mode: C; c-file-style: "gnu" -*- */
/* loop.c  Main loop for daemon
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

#include "loop.h"
#include <dbus/dbus-list.h>
#include <dbus/dbus-sysdeps.h>

static DBusList *watches = NULL;
static int watch_list_serial = 0;
static dbus_bool_t exited = FALSE;

typedef struct
{
  DBusWatch *watch;
  BusWatchFunction function;
  void *data;
  DBusFreeFunction free_data_func;
} WatchCallback;

dbus_bool_t
bus_loop_add_watch (DBusWatch        *watch,
                    BusWatchFunction  function,
                    void             *data,
                    DBusFreeFunction  free_data_func)
{
  WatchCallback *cb;

  cb = dbus_new (WatchCallback, 1);
  if (cb == NULL)
    return FALSE;

  cb->watch = watch;
  cb->function = function;
  cb->data = data;
  cb->free_data_func = free_data_func;

  if (!_dbus_list_append (&watches, cb))
    {
      dbus_free (cb);
      return FALSE;
    }

  watch_list_serial += 1;
  
  return TRUE;
}

void
bus_loop_remove_watch (DBusWatch        *watch,
                       BusWatchFunction  function,
                       void             *data)
{
  DBusList *link;
  
  link = _dbus_list_get_first_link (&watches);
  while (link != NULL)
    {
      DBusList *next = _dbus_list_get_next_link (&watches, link);
      WatchCallback *cb = link->data;

      if (cb->watch == watch &&
          cb->function == function &&
          cb->data == data)
        {
          _dbus_list_remove_link (&watches, link);

          watch_list_serial += 1;
          
          if (cb->free_data_func)
            (* cb->free_data_func) (cb->data);
          dbus_free (cb);
          
          return;
        }
      
      link = next;
    }

  _dbus_warn ("could not find watch %p function %p data %p to remove\n",
              watch, function, data);
}

static void
wait_for_memory (void)
{
  _dbus_sleep_milliseconds (500);
}

void
bus_loop_run (void)
{
  while (!exited)
    {
      DBusPollFD *fds;
      int n_fds;
      WatchCallback **watches_for_fds;
      int i;
      DBusList *link;
      int n_ready;
      int initial_serial;
      
      fds = NULL;
      watches_for_fds = NULL;
      
      n_fds = _dbus_list_get_length (&watches);

      if (n_fds == 0)
        {
          bus_loop_quit ();
          goto next_iteration;
        }
      
      fds = dbus_new0 (DBusPollFD, n_fds);
      while (fds == NULL)
        {
          wait_for_memory ();
          fds = dbus_new0 (DBusPollFD, n_fds);
        }

      watches_for_fds = dbus_new (WatchCallback*, n_fds);
      while (watches_for_fds == NULL)
        {
          wait_for_memory ();
          watches_for_fds = dbus_new (WatchCallback*, n_fds);
        }
      
      i = 0;
      link = _dbus_list_get_first_link (&watches);
      while (link != NULL)
        {
          DBusList *next = _dbus_list_get_next_link (&watches, link);
          WatchCallback *cb = link->data;
          int flags;
          
          watches_for_fds[i] = cb;

          flags = dbus_watch_get_flags (cb->watch);
          
          fds[i].fd = dbus_watch_get_fd (cb->watch);
          if (flags & DBUS_WATCH_READABLE)
            fds[i].events |= _DBUS_POLLIN;
          if (flags & DBUS_WATCH_WRITABLE)
            fds[i].events |= _DBUS_POLLOUT;
          
          link = next;
          ++i;
        }

      n_ready = _dbus_poll (fds, n_fds, -1);

      if (n_ready > 0)
        {
          initial_serial = watch_list_serial;
          i = 0;
          while (i < n_fds)
            {
              /* FIXME I think this "restart if we change the watches"
               * approach could result in starving watches
               * toward the end of the list.
               */
              if (initial_serial != watch_list_serial)
                goto next_iteration;

              if (exited)
                goto next_iteration;

              if (fds[i].revents != 0)
                {
                  WatchCallback *cb;
                  unsigned int condition;
                  
                  cb = watches_for_fds[i];
                  
                  condition = 0;
                  if (fds[i].revents & _DBUS_POLLIN)
                    condition |= DBUS_WATCH_READABLE;
                  if (fds[i].revents & _DBUS_POLLOUT)
                    condition |= DBUS_WATCH_WRITABLE;
                  if (fds[i].revents & _DBUS_POLLHUP)
                    condition |= DBUS_WATCH_HANGUP;
                  if (fds[i].revents & _DBUS_POLLERR)
                    condition |= DBUS_WATCH_ERROR;

                  /* condition may still be 0 if we got some
                   * weird POLLFOO thing like POLLWRBAND
                   */
                  
                  if (condition != 0)
                    (* cb->function) (cb->watch,
                                      condition,
                                      cb->data);
                }
              
              ++i;
            }
        }
      
    next_iteration:
      dbus_free (fds);
      dbus_free (watches_for_fds);
    }
}

void
bus_loop_quit (void)
{
  exited = TRUE;
}
