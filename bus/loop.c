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
#include "utils.h"
#include <dbus/dbus-list.h>
#include <dbus/dbus-sysdeps.h>

struct BusLoop
{
  int refcount;
  DBusList *callbacks;
  int callback_list_serial;
  int watch_count;
  int timeout_count;
  int depth; /**< number of recursive runs */
};

typedef enum
{
  CALLBACK_WATCH,
  CALLBACK_TIMEOUT
} CallbackType;

typedef struct
{
  CallbackType type;
  void *data;
  DBusFreeFunction free_data_func;
} Callback;

typedef struct
{
  Callback callback;
  BusWatchFunction function;
  DBusWatch *watch;
  /* last watch handle failed due to OOM */
  unsigned int last_iteration_oom : 1;
} WatchCallback;

typedef struct
{
  Callback callback;
  DBusTimeout *timeout;
  BusTimeoutFunction function;
  unsigned long last_tv_sec;
  unsigned long last_tv_usec;
} TimeoutCallback;

#define WATCH_CALLBACK(callback)   ((WatchCallback*)callback)
#define TIMEOUT_CALLBACK(callback) ((TimeoutCallback*)callback)

static WatchCallback*
watch_callback_new (DBusWatch        *watch,
                    BusWatchFunction  function,
                    void             *data,
                    DBusFreeFunction  free_data_func)
{
  WatchCallback *cb;

  cb = dbus_new (WatchCallback, 1);
  if (cb == NULL)
    return NULL;

  cb->watch = watch;
  cb->function = function;
  cb->last_iteration_oom = FALSE;
  cb->callback.type = CALLBACK_WATCH;
  cb->callback.data = data;
  cb->callback.free_data_func = free_data_func;

  return cb;
}

static TimeoutCallback*
timeout_callback_new (DBusTimeout        *timeout,
                      BusTimeoutFunction  function,
                      void               *data,
                      DBusFreeFunction    free_data_func)
{
  TimeoutCallback *cb;

  cb = dbus_new (TimeoutCallback, 1);
  if (cb == NULL)
    return NULL;

  cb->timeout = timeout;
  cb->function = function;
  _dbus_get_current_time (&cb->last_tv_sec,
                          &cb->last_tv_usec);
  cb->callback.type = CALLBACK_TIMEOUT;
  cb->callback.data = data;
  cb->callback.free_data_func = free_data_func;
  
  return cb;
}

static void
callback_free (Callback *cb)
{
  if (cb->free_data_func)
    (* cb->free_data_func) (cb->data);

  dbus_free (cb);
}

static dbus_bool_t
add_callback (BusLoop  *loop,
              Callback *cb)
{
  if (!_dbus_list_append (&loop->callbacks, cb))
    return FALSE;

  loop->callback_list_serial += 1;

  switch (cb->type)
    {
    case CALLBACK_WATCH:
      loop->watch_count += 1;
      break;
    case CALLBACK_TIMEOUT:
      loop->timeout_count += 1;
      break;
    }
  
  return TRUE;
}

static void
remove_callback (BusLoop  *loop,
                 DBusList *link)
{
  Callback *cb = link->data;
  
  switch (cb->type)
    {
    case CALLBACK_WATCH:
      loop->watch_count -= 1;
      break;
    case CALLBACK_TIMEOUT:
      loop->timeout_count -= 1;
      break;
    }
  
  callback_free (cb);
  _dbus_list_remove_link (&loop->callbacks, link);
  loop->callback_list_serial += 1;
}

BusLoop*
bus_loop_new (void)
{
  BusLoop *loop;

  loop = dbus_new0 (BusLoop, 1);
  if (loop == NULL)
    return NULL;

  loop->refcount = 1;
  
  return loop;
}

void
bus_loop_ref (BusLoop *loop)
{
  _dbus_assert (loop != NULL);
  _dbus_assert (loop->refcount > 0);

  loop->refcount += 1;
}

void
bus_loop_unref (BusLoop *loop)
{
  _dbus_assert (loop != NULL);
  _dbus_assert (loop->refcount > 0);

  loop->refcount -= 1;
  if (loop->refcount == 0)
    {
      
      dbus_free (loop);
    }
}

dbus_bool_t
bus_loop_add_watch (BusLoop          *loop,
                    DBusWatch        *watch,
                    BusWatchFunction  function,
                    void             *data,
                    DBusFreeFunction  free_data_func)
{
  WatchCallback *wcb;

  wcb = watch_callback_new (watch, function, data, free_data_func);
  if (wcb == NULL)
    return FALSE;

  if (!add_callback (loop, (Callback*) wcb))
    {
      wcb->callback.free_data_func = NULL; /* don't want to have this side effect */
      callback_free ((Callback*) wcb);
      return FALSE;
    }
  
  return TRUE;
}

void
bus_loop_remove_watch (BusLoop          *loop,
                       DBusWatch        *watch,
                       BusWatchFunction  function,
                       void             *data)
{
  DBusList *link;
  
  link = _dbus_list_get_first_link (&loop->callbacks);
  while (link != NULL)
    {
      DBusList *next = _dbus_list_get_next_link (&loop->callbacks, link);
      Callback *this = link->data;

      if (this->type == CALLBACK_WATCH &&
          WATCH_CALLBACK (this)->watch == watch &&
          this->data == data &&
          WATCH_CALLBACK (this)->function == function)
        {
          remove_callback (loop, link);
          
          return;
        }
      
      link = next;
    }

  _dbus_warn ("could not find watch %p function %p data %p to remove\n",
              watch, function, data);
}

dbus_bool_t
bus_loop_add_timeout (BusLoop            *loop,
                      DBusTimeout        *timeout,
                      BusTimeoutFunction  function,
                      void               *data,
                      DBusFreeFunction    free_data_func)
{
  TimeoutCallback *tcb;

  tcb = timeout_callback_new (timeout, function, data, free_data_func);
  if (tcb == NULL)
    return FALSE;

  if (!add_callback (loop, (Callback*) tcb))
    {
      tcb->callback.free_data_func = NULL; /* don't want to have this side effect */
      callback_free ((Callback*) tcb);
      return FALSE;
    }
  
  return TRUE;
}

void
bus_loop_remove_timeout (BusLoop            *loop,
                         DBusTimeout        *timeout,
                         BusTimeoutFunction  function,
                         void               *data)
{
  DBusList *link;
  
  link = _dbus_list_get_first_link (&loop->callbacks);
  while (link != NULL)
    {
      DBusList *next = _dbus_list_get_next_link (&loop->callbacks, link);
      Callback *this = link->data;

      if (this->type == CALLBACK_TIMEOUT &&
          TIMEOUT_CALLBACK (this)->timeout == timeout &&
          this->data == data &&
          TIMEOUT_CALLBACK (this)->function == function)
        {
          remove_callback (loop, link);
          
          return;
        }
      
      link = next;
    }

  _dbus_warn ("could not find timeout %p function %p data %p to remove\n",
              timeout, function, data);
}

/* Returns TRUE if we have any timeouts or ready file descriptors,
 * which is just used in test code as a debug hack
 */

dbus_bool_t
bus_loop_iterate (BusLoop     *loop,
                  dbus_bool_t  block)
{
  dbus_bool_t retval;
  DBusPollFD *fds;
  int n_fds;
  WatchCallback **watches_for_fds;
  int i;
  DBusList *link;
  int n_ready;
  int initial_serial;
  long timeout;
  dbus_bool_t oom_watch_pending;
  int orig_depth;
  
  retval = FALSE;
      
  fds = NULL;
  watches_for_fds = NULL;
  oom_watch_pending = FALSE;
  orig_depth = loop->depth;
  
#if 0
  _dbus_verbose (" iterate %d timeouts %d watches\n",
                 loop->timeout_count, loop->watch_count);
#endif
  
  if (loop->callbacks == NULL)
    {
      bus_loop_quit (loop);
      goto next_iteration;
    }

  /* count enabled watches */
  n_fds = 0;
  link = _dbus_list_get_first_link (&loop->callbacks);
  while (link != NULL)
    {
      DBusList *next = _dbus_list_get_next_link (&loop->callbacks, link);
      Callback *cb = link->data;
      if (cb->type == CALLBACK_WATCH)
        {
          WatchCallback *wcb = WATCH_CALLBACK (cb);

          if (!wcb->last_iteration_oom &&
              dbus_watch_get_enabled (wcb->watch))
            ++n_fds;
        }
      
      link = next;
    }

  /* fill our array of fds and watches */
  if (n_fds > 0)
    {
      fds = dbus_new0 (DBusPollFD, n_fds);
      while (fds == NULL)
        {
          bus_wait_for_memory ();
          fds = dbus_new0 (DBusPollFD, n_fds);
        }
          
      watches_for_fds = dbus_new (WatchCallback*, n_fds);
      while (watches_for_fds == NULL)
        {
          bus_wait_for_memory ();
          watches_for_fds = dbus_new (WatchCallback*, n_fds);
        }
      
      i = 0;
      link = _dbus_list_get_first_link (&loop->callbacks);
      while (link != NULL)
        {
          DBusList *next = _dbus_list_get_next_link (&loop->callbacks, link);
          Callback *cb = link->data;
          if (cb->type == CALLBACK_WATCH)
            {
              unsigned int flags;
              WatchCallback *wcb = WATCH_CALLBACK (cb);

              if (wcb->last_iteration_oom)
                {
                  /* we skip this one this time, but reenable it next time,
                   * and have a timeout on this iteration
                   */
                  wcb->last_iteration_oom = FALSE;
                  oom_watch_pending = TRUE;
                }
              else if (dbus_watch_get_enabled (wcb->watch))
                {
                  watches_for_fds[i] = wcb;
                  
                  flags = dbus_watch_get_flags (wcb->watch);
                  
                  fds[i].fd = dbus_watch_get_fd (wcb->watch);
                  if (flags & DBUS_WATCH_READABLE)
                    fds[i].events |= _DBUS_POLLIN;
                  if (flags & DBUS_WATCH_WRITABLE)
                    fds[i].events |= _DBUS_POLLOUT;

                  ++i;
                }
            }
              
          link = next;
        }

      _dbus_assert (i == n_fds);
    }

  timeout = -1;
  if (loop->timeout_count > 0)
    {
      unsigned long tv_sec;
      unsigned long tv_usec;

      retval = TRUE;
      
      _dbus_get_current_time (&tv_sec, &tv_usec);
          
      link = _dbus_list_get_first_link (&loop->callbacks);
      while (link != NULL)
        {
          DBusList *next = _dbus_list_get_next_link (&loop->callbacks, link);
          Callback *cb = link->data;

          if (cb->type == CALLBACK_TIMEOUT &&
              dbus_timeout_get_enabled (TIMEOUT_CALLBACK (cb)->timeout))
            {
              TimeoutCallback *tcb = TIMEOUT_CALLBACK (cb);
              unsigned long interval;
              unsigned long elapsed;

              if (tcb->last_tv_sec > tv_sec ||
                  (tcb->last_tv_sec == tv_sec &&
                   tcb->last_tv_usec > tv_usec))
                {
                  /* Clock went backward, pretend timeout
                   * was just installed.
                   */
                  tcb->last_tv_sec = tv_sec;
                  tcb->last_tv_usec = tv_usec;
                  _dbus_verbose ("System clock went backward\n");
                }
                  
              interval = dbus_timeout_get_interval (tcb->timeout);

              elapsed =
                (tv_sec - tcb->last_tv_sec) * 1000 +
                (tv_usec - tcb->last_tv_usec) / 1000;

              if (interval < elapsed)
                timeout = 0;
              else if (timeout < 0)
                timeout = interval - elapsed;
              else
                timeout = MIN (((unsigned long)timeout), interval - elapsed);

              _dbus_assert (timeout >= 0);
                  
              if (timeout == 0)
                break; /* it's not going to get shorter... */
            }
              
          link = next;
        }
    }

  if (!block)
    timeout = 0;

  /* if a watch is OOM, don't wait longer than the OOM
   * wait to re-enable it
   */
  if (oom_watch_pending)
    timeout = MIN (timeout, bus_get_oom_wait ());
  
  n_ready = _dbus_poll (fds, n_fds, timeout);

  initial_serial = loop->callback_list_serial;

  if (loop->timeout_count > 0)
    {
      unsigned long tv_sec;
      unsigned long tv_usec;

      _dbus_get_current_time (&tv_sec, &tv_usec);

      /* It'd be nice to avoid this O(n) thingy here */
      link = _dbus_list_get_first_link (&loop->callbacks);
      while (link != NULL)
        {
          DBusList *next = _dbus_list_get_next_link (&loop->callbacks, link);
          Callback *cb = link->data;

          if (initial_serial != loop->callback_list_serial)
            goto next_iteration;

          if (loop->depth != orig_depth)
            goto next_iteration;
              
          if (cb->type == CALLBACK_TIMEOUT &&
              dbus_timeout_get_enabled (TIMEOUT_CALLBACK (cb)->timeout))
            {
              TimeoutCallback *tcb = TIMEOUT_CALLBACK (cb);
              unsigned long interval;
              unsigned long elapsed;
                  
              if (tcb->last_tv_sec > tv_sec ||
                  (tcb->last_tv_sec == tv_sec &&
                   tcb->last_tv_usec > tv_usec))
                {
                  /* Clock went backward, pretend timeout
                   * was just installed.
                   */
                  tcb->last_tv_sec = tv_sec;
                  tcb->last_tv_usec = tv_usec;
                  _dbus_verbose ("System clock went backward\n");
                  goto next_timeout;
                }
                  
              interval = dbus_timeout_get_interval (tcb->timeout);

              elapsed =
                (tv_sec - tcb->last_tv_sec) * 1000 +
                (tv_usec - tcb->last_tv_usec) / 1000;

#if 0
              _dbus_verbose ("  interval = %lu elapsed = %lu\n",
                             interval, elapsed);
#endif
              
              if (interval <= elapsed)
                {
                  /* Save last callback time and fire this timeout */
                  tcb->last_tv_sec = tv_sec;
                  tcb->last_tv_usec = tv_usec;

#if 0
                  _dbus_verbose ("  invoking timeout\n");
#endif
                  
                  (* tcb->function) (tcb->timeout,
                                     cb->data);
                }
            }

        next_timeout:
          link = next;
        }
    }
      
  if (n_ready > 0)
    {
      i = 0;
      while (i < n_fds)
        {
          /* FIXME I think this "restart if we change the watches"
           * approach could result in starving watches
           * toward the end of the list.
           */
          if (initial_serial != loop->callback_list_serial)
            goto next_iteration;

          if (loop->depth != orig_depth)
            goto next_iteration;

          if (fds[i].revents != 0)
            {
              WatchCallback *wcb;
              unsigned int condition;
                  
              wcb = watches_for_fds[i];
                  
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
                  
              if (condition != 0 &&
                  dbus_watch_get_enabled (wcb->watch))
                {
                  if (!(* wcb->function) (wcb->watch,
                                          condition,
                                          ((Callback*)wcb)->data))
                    wcb->last_iteration_oom = TRUE;

                  retval = TRUE;
                }
            }
              
          ++i;
        }
    }
      
 next_iteration:
  dbus_free (fds);
  dbus_free (watches_for_fds);

  return retval;
}

void
bus_loop_run (BusLoop *loop)
{
  int our_exit_depth;

  bus_loop_ref (loop);
  
  our_exit_depth = loop->depth;
  loop->depth += 1;
  
  while (loop->depth != our_exit_depth)
    bus_loop_iterate (loop, TRUE);

  bus_loop_unref (loop);
}

void
bus_loop_quit (BusLoop *loop)
{
  _dbus_assert (loop->depth > 0);
  
  loop->depth -= 1;
}
