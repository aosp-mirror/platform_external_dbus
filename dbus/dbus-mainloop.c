/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-mainloop.c  Main loop utility
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

#include "dbus-mainloop.h"

#include <dbus/dbus-list.h>
#include <dbus/dbus-sysdeps.h>

struct DBusLoop
{
  int refcount;
  DBusList *callbacks;
  int callback_list_serial;
  int watch_count;
  int timeout_count;
  int depth; /**< number of recursive runs */
  DBusList *need_dispatch;
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
  DBusWatchFunction function;
  DBusWatch *watch;
  /* last watch handle failed due to OOM */
  unsigned int last_iteration_oom : 1;
} WatchCallback;

typedef struct
{
  Callback callback;
  DBusTimeout *timeout;
  DBusTimeoutFunction function;
  unsigned long last_tv_sec;
  unsigned long last_tv_usec;
} TimeoutCallback;

#define WATCH_CALLBACK(callback)   ((WatchCallback*)callback)
#define TIMEOUT_CALLBACK(callback) ((TimeoutCallback*)callback)

static WatchCallback*
watch_callback_new (DBusWatch        *watch,
                    DBusWatchFunction  function,
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
                      DBusTimeoutFunction  function,
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
add_callback (DBusLoop  *loop,
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
remove_callback (DBusLoop  *loop,
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

DBusLoop*
_dbus_loop_new (void)
{
  DBusLoop *loop;

  loop = dbus_new0 (DBusLoop, 1);
  if (loop == NULL)
    return NULL;

  loop->refcount = 1;
  
  return loop;
}

void
_dbus_loop_ref (DBusLoop *loop)
{
  _dbus_assert (loop != NULL);
  _dbus_assert (loop->refcount > 0);

  loop->refcount += 1;
}

void
_dbus_loop_unref (DBusLoop *loop)
{
  _dbus_assert (loop != NULL);
  _dbus_assert (loop->refcount > 0);

  loop->refcount -= 1;
  if (loop->refcount == 0)
    {
      while (loop->need_dispatch)
        {
          DBusConnection *connection = _dbus_list_pop_first (&loop->need_dispatch);

          dbus_connection_unref (connection);
        }
      
      dbus_free (loop);
    }
}

dbus_bool_t
_dbus_loop_add_watch (DBusLoop          *loop,
                      DBusWatch        *watch,
                      DBusWatchFunction  function,
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
_dbus_loop_remove_watch (DBusLoop          *loop,
                         DBusWatch        *watch,
                         DBusWatchFunction  function,
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
_dbus_loop_add_timeout (DBusLoop            *loop,
                        DBusTimeout        *timeout,
                        DBusTimeoutFunction  function,
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
_dbus_loop_remove_timeout (DBusLoop            *loop,
                           DBusTimeout        *timeout,
                           DBusTimeoutFunction  function,
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

/* Convolutions from GLib, there really must be a better way
 * to do this.
 */
static dbus_bool_t
check_timeout (unsigned long    tv_sec,
               unsigned long    tv_usec,
               TimeoutCallback *tcb,
               int             *timeout)
{
  long sec;
  long msec;
  unsigned long expiration_tv_sec;
  unsigned long expiration_tv_usec;
  long interval_seconds;
  long interval_milliseconds;
  int interval;

  interval = dbus_timeout_get_interval (tcb->timeout);
  
  interval_seconds = interval / 1000;
  interval_milliseconds = interval - interval_seconds * 1000;
  
  expiration_tv_sec = tcb->last_tv_sec + interval_seconds;
  expiration_tv_usec = tcb->last_tv_usec + interval_milliseconds * 1000;
  if (expiration_tv_usec >= 1000000)
    {
      expiration_tv_usec -= 1000000;
      expiration_tv_sec += 1;
    }
  
  sec = expiration_tv_sec - tv_sec;
  msec = (expiration_tv_usec - tv_usec) / 1000;

#if 0
  printf ("Interval is %ld seconds %ld msecs\n",
          interval_seconds,
          interval_milliseconds);
  printf ("Now is %lu seconds %lu usecs\n",
          tv_sec, tv_usec);
  printf ("Exp is %lu seconds %lu usecs\n",
          expiration_tv_sec, expiration_tv_usec);
  printf ("Pre-correction, remaining sec %ld msec %ld\n", sec, msec);
#endif
  
  /* We do the following in a rather convoluted fashion to deal with
   * the fact that we don't have an integral type big enough to hold
   * the difference of two timevals in millseconds.
   */
  if (sec < 0 || (sec == 0 && msec < 0))
    msec = 0;
  else
    {
      if (msec < 0)
	{
	  msec += 1000;
	  sec -= 1;
	}
      
      if (sec > interval_seconds ||
	  (sec == interval_seconds && msec > interval_milliseconds))
	{
          _dbus_verbose ("System clock went backward interval_seconds %ld interval_msecs %ld sec %ld msec %ld last_tv_sec %lu last_tv_usec %lu tv_sec %lu tv_usec %lu\n",
                         interval_seconds, interval_milliseconds, sec, msec, tcb->last_tv_sec,
                         tcb->last_tv_usec, tv_sec, tv_usec);
          
	  /* The system time has been set backwards, reset the timeout */          
          
          tcb->last_tv_sec = tv_sec;
          tcb->last_tv_usec = tv_usec;
          
          msec = MIN (_DBUS_INT_MAX, interval);
	}
      else
	{
	  msec = MIN (_DBUS_INT_MAX, (unsigned int)msec + 1000 * (unsigned int)sec);
	}
    }

  *timeout = msec;

#if 0
  printf ("Timeout expires in %d milliseconds\n", *timeout);
#endif
  
  return msec == 0;
}

static void
_dbus_loop_dispatch (DBusLoop *loop)
{
 next:
  while (loop->need_dispatch != NULL)
    {
      DBusConnection *connection = _dbus_list_pop_first (&loop->need_dispatch);

      while (TRUE)
        {
          DBusDispatchStatus status;
          
          status = dbus_connection_dispatch (connection);

          if (status == DBUS_DISPATCH_COMPLETE)
            {
              dbus_connection_unref (connection);
              goto next;
            }
          else
            {
              if (status == DBUS_DISPATCH_NEED_MEMORY)
                _dbus_wait_for_memory ();
            }
        }
    }
}

dbus_bool_t
_dbus_loop_queue_dispatch (DBusLoop       *loop,
                           DBusConnection *connection)
{
  
  if (_dbus_list_append (&loop->need_dispatch, connection))
    {
      dbus_connection_ref (connection);
      return TRUE;
    }
  else
    return FALSE;
}

/* Returns TRUE if we have any timeouts or ready file descriptors,
 * which is just used in test code as a debug hack
 */

dbus_bool_t
_dbus_loop_iterate (DBusLoop     *loop,
                    dbus_bool_t   block)
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
      _dbus_loop_quit (loop);
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
          _dbus_wait_for_memory ();
          fds = dbus_new0 (DBusPollFD, n_fds);
        }
          
      watches_for_fds = dbus_new (WatchCallback*, n_fds);
      while (watches_for_fds == NULL)
        {
          _dbus_wait_for_memory ();
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
              int msecs_remaining;

              check_timeout (tv_sec, tv_usec, tcb, &msecs_remaining);

              if (timeout < 0)
                timeout = msecs_remaining;
              else
                timeout = MIN (msecs_remaining, timeout);
              
              _dbus_assert (timeout >= 0);
                  
              if (timeout == 0)
                break; /* it's not going to get shorter... */
            }
              
          link = next;
        }
    }

  /* Never block if we have stuff to dispatch */
  if (!block || loop->need_dispatch != NULL)
    {
      timeout = 0;
#if 0
      printf ("timeout is 0 as we aren't blocking\n");
#endif
    }

  /* if a watch is OOM, don't wait longer than the OOM
   * wait to re-enable it
   */
  if (oom_watch_pending)
    timeout = MIN (timeout, _dbus_get_oom_wait ());
  
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
              int msecs_remaining;
              
              if (check_timeout (tv_sec, tv_usec,
                                 tcb, &msecs_remaining))
                {
                  /* Save last callback time and fire this timeout */
                  tcb->last_tv_sec = tv_sec;
                  tcb->last_tv_usec = tv_usec;

#if 0
                  printf ("  invoking timeout\n");
#endif
                  
                  (* tcb->function) (tcb->timeout,
                                     cb->data);
                }
            }

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

  if (loop->need_dispatch != NULL)
    {
      retval = TRUE;
      _dbus_loop_dispatch (loop);
    }
  
  return retval;
}

void
_dbus_loop_run (DBusLoop *loop)
{
  int our_exit_depth;

  _dbus_loop_ref (loop);
  
  our_exit_depth = loop->depth;
  loop->depth += 1;
  
  while (loop->depth != our_exit_depth)
    _dbus_loop_iterate (loop, TRUE);

  _dbus_loop_unref (loop);
}

void
_dbus_loop_quit (DBusLoop *loop)
{
  _dbus_assert (loop->depth > 0);
  
  loop->depth -= 1;
}

int
_dbus_get_oom_wait (void)
{
#ifdef DBUS_BUILD_TESTS
  /* make tests go fast */
  return 0;
#else
  return 500;
#endif
}

void
_dbus_wait_for_memory (void)
{
  _dbus_sleep_milliseconds (_dbus_get_oom_wait ());
}

