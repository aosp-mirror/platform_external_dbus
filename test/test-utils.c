#include "test-utils.h"

typedef struct
{
  DBusLoop *loop;
  DBusConnection *connection;

} CData;

dbus_bool_t
test_connection_dispatch_one_message  (DBusConnection *connection)
{
  DBusDispatchStatus status;

  while ((status = dbus_connection_dispatch (connection)) == DBUS_DISPATCH_NEED_MEMORY)
    _dbus_wait_for_memory ();
  
  return status == DBUS_DISPATCH_DATA_REMAINS;
}

void
test_connection_dispatch_all_messages (DBusConnection *connection)
{
  while (test_connection_dispatch_one_message (connection))
    ;
}

static dbus_bool_t
connection_watch_callback (DBusWatch     *watch,
                           unsigned int   condition,
                           void          *data)
{
  CData *cd = data;
  dbus_bool_t retval;

  dbus_connection_ref (cd->connection);

  _dbus_verbose (" Handling watch\n");
  
  retval = dbus_connection_handle_watch (cd->connection, watch, condition);

  _dbus_verbose (" Watch handled\n");
  
  test_connection_dispatch_all_messages (cd->connection);

  _dbus_verbose (" Dispatched all\n");
  
  dbus_connection_unref (cd->connection);

  return retval;
}

static dbus_bool_t
add_watch (DBusWatch *watch,
	   void      *data)
{
  CData *cd = data;

  return _dbus_loop_add_watch (cd->loop,
                               watch,
                               connection_watch_callback,
                               cd, NULL);
}

static void
remove_watch (DBusWatch *watch,
	      void      *data)
{
  CData *cd = data;
  
  _dbus_loop_remove_watch (cd->loop,
                           watch, connection_watch_callback, cd);  
}

static void
connection_timeout_callback (DBusTimeout   *timeout,
                             void          *data)
{
  CData *cd = data;

  dbus_connection_ref (cd->connection);

  /* Can return FALSE on OOM but we just let it fire again later */
  dbus_timeout_handle (timeout);
  
  test_connection_dispatch_all_messages (cd->connection);
  
  dbus_connection_unref (cd->connection);
}

static dbus_bool_t
add_timeout (DBusTimeout *timeout,
	     void        *data)
{
  CData *cd = data;

  return _dbus_loop_add_timeout (cd->loop,
                                 timeout, connection_timeout_callback, cd, NULL);
}

static void
remove_timeout (DBusTimeout *timeout,
		void        *data)
{
  CData *cd = data;

  _dbus_loop_remove_timeout (cd->loop,
                             timeout, connection_timeout_callback, cd);
}

static void
cdata_free (void *data)
{
  CData *cd = data;

  dbus_connection_unref (cd->connection);
  _dbus_loop_unref (cd->loop);
  
  dbus_free (cd);
}

static CData*
cdata_new (DBusLoop       *loop,
           DBusConnection *connection)
{
  CData *cd;

  cd = dbus_new0 (CData, 1);
  if (cd == NULL)
    return NULL;

  cd->loop = loop;
  cd->connection = connection;

  dbus_connection_ref (cd->connection);
  _dbus_loop_ref (cd->loop);

  return cd;
}

dbus_bool_t
test_connection_setup (DBusLoop       *loop,
                       DBusConnection *connection)
{
  CData *cd;

  cd = cdata_new (loop, connection);
  if (cd == NULL)
    goto nomem;

  /* Because dbus-mainloop.c checks dbus_timeout_get_enabled(),
   * dbus_watch_get_enabled() directly, we don't have to provide
   * "toggled" callbacks.
   */
  
  if (!dbus_connection_set_watch_functions (connection,
                                            add_watch,
                                            remove_watch,
                                            NULL,
                                            cd, cdata_free))
    goto nomem;


  cd = cdata_new (loop, connection);
  if (cd == NULL)
    goto nomem;
  
  if (!dbus_connection_set_timeout_functions (connection,
                                              add_timeout,
                                              remove_timeout,
                                              NULL,
                                              cd, cdata_free))
    {
      dbus_connection_set_watch_functions (connection, NULL, NULL, NULL, NULL, NULL);
      goto nomem;
    }

  return TRUE;
  
 nomem:
  if (cd)
    cdata_free (cd);
  return FALSE;
}

void
test_connection_shutdown (DBusLoop       *loop,
                          DBusConnection *connection)
{
  if (!dbus_connection_set_watch_functions (connection,
                                            NULL,
                                            NULL,
                                            NULL,
                                            NULL, NULL))
    _dbus_assert_not_reached ("setting watch functions to NULL failed");
  
  if (!dbus_connection_set_timeout_functions (connection,
                                              NULL,
                                              NULL,
                                              NULL,
                                              NULL, NULL))
    _dbus_assert_not_reached ("setting timeout functions to NULL failed");

}
