#include "bus-test-loop.h"
#include <sys/time.h>
#include <stdio.h>

#define DBUS_COMPILATION /* cheat and use DBusList */
#include <dbus/dbus-list.h>
#undef DBUS_COMPILATION

typedef struct
{
  long time;
  DBusTimeout *timeout;

} LoopTimeout;

static DBusList *timeouts;

static long
get_time (void)
{
  struct timeval r;
  long time;

  /* Can't use dbus-sysdeps here since that isn't
   * available outside of libdbus.
   */
  gettimeofday (&r, NULL);

  time = r.tv_sec * 1000;
  time += r.tv_usec / 1000;

  return time;
}

static void
add_timeout (DBusTimeout *timeout,
	     void        *data)
{
  LoopTimeout *lt;

  lt = dbus_new (LoopTimeout, 1);
  lt->time = get_time () + dbus_timeout_get_interval (timeout);
  lt->timeout = timeout;

  _dbus_list_append (&timeouts, lt);
}

static void
remove_timeout (DBusTimeout *timeout,
		void        *data)
{
  DBusList *link;
  
  link = _dbus_list_get_first_link (&timeouts);
  while (link != NULL)
    {
      LoopTimeout *lt = link->data;
      if (lt->timeout == timeout)
	{
	  _dbus_list_remove (&timeouts, lt);
	  return;
	}
      link = _dbus_list_get_next_link (&timeouts, link);
    }
}

static dbus_bool_t running_loop;


void
bus_test_loop_quit (void)
{
  running_loop = FALSE;
}

void
bus_test_loop_run (void)
{
  running_loop = TRUE;

  /* Horribly inefficient main loop */
  while (running_loop)
    {
      DBusList *link, *list_copy;
      long time;

      time = get_time ();

      _dbus_list_copy (&timeouts, &list_copy);

      link = _dbus_list_get_first_link (&list_copy);
      while (link != NULL)
	{
	  LoopTimeout *lt = link->data;
	  if (lt->time <= time)
	    {
	      dbus_timeout_handle (lt->timeout);
	      _dbus_list_remove (&timeouts, lt);
	    }
	  link = _dbus_list_get_next_link (&list_copy, link);
	}
    }
}


void
bus_test_loop_hookup_with_server (DBusServer *server)
{
  dbus_server_set_timeout_functions (server,
				     add_timeout, remove_timeout,
				     NULL, NULL);
}

void
bus_test_loop_hookup_with_connection (DBusConnection *connection)
{
  dbus_connection_set_timeout_functions (connection,
					 add_timeout, remove_timeout,
					 NULL, NULL);
}
