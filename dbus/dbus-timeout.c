/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-timeout.c DBusTimeout implementation
 *
 * Copyright (C) 2003  CodeFactory AB
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

#include "dbus-internals.h"
#include "dbus-timeout.h"
#include "dbus-list.h"

/**
 * @defgroup DBusTimeoutInternals DBusTimeout implementation details
 * @ingroup  DBusInternals
 * @brief implementation details for DBusTimeout
 * 
 * @{
 */

struct DBusTimeout
{
  int refcount;                                /**< Reference count */
  int interval;                                /**< Timeout interval in milliseconds. */

  DBusTimeoutHandler handler;                  /**< Timeout handler. */
  void *handler_data;                          /**< Timeout handler data. */
  DBusFreeFunction free_handler_data_function; /**< Free the timeout handler data. */
  
  void *data;		   	               /**< Application data. */
  DBusFreeFunction free_data_function;         /**< Free the application data. */
};

/**
 * Creates a new DBusTimeout.
 * @param interval the timeout interval in milliseconds.
 * @param handler function to call when the timeout occurs.
 * @param data data to pass to the handler
 * @param free_data_function function to be called to free the data.
 * @returns the new DBusTimeout object,
 */
DBusTimeout*
_dbus_timeout_new (int                 interval,
		   DBusTimeoutHandler  handler,
		   void               *data,
		   DBusFreeFunction    free_data_function)
{
  DBusTimeout *timeout;

  timeout = dbus_new0 (DBusTimeout, 1);
  timeout->refcount = 1;
  timeout->interval = interval;

  timeout->handler = handler;
  timeout->handler_data = data;
  timeout->free_handler_data_function = free_data_function;
  
  return timeout;
}

/**
 * Increments the reference count of a DBusTimeout object.
 *
 * @param timeout the timeout object.
 */
void
_dbus_timeout_ref (DBusTimeout *timeout)
{
  timeout->refcount += 1;
}

/**
 * Decrements the reference count of a DBusTimeout object
 * and finalizes the object if the count reaches zero.
 *
 * @param timeout the timeout object.
 */
void
_dbus_timeout_unref (DBusTimeout *timeout)
{
  _dbus_assert (timeout != NULL);
  _dbus_assert (timeout->refcount > 0);
  
  timeout->refcount -= 1;
  if (timeout->refcount == 0)
    {
      dbus_timeout_set_data (timeout, NULL, NULL); /* call free_data_function */

      if (timeout->free_handler_data_function)
	(* timeout->free_handler_data_function) (timeout->handler_data);
      
      dbus_free (timeout);
    }
}

/**
 * @typedef DBusTimeoutList
 *
 * Opaque data type representing a list of timeouts
 * and a set of DBusAddTimeoutFunction/DBusRemoveTimeoutFunction.
 * Automatically handles removing/re-adding timeouts
 * when the DBusAddTimeoutFunction is updated or changed.
 * Holds a reference count to each timeout.
 *
 */

/**
 * DBusTimeoutList implementation details. All fields
 * are private.
 *
 */
struct DBusTimeoutList
{
  DBusList *timeouts; /**< Timeout objects. */

  DBusAddTimeoutFunction add_timeout_function;       /**< Callback for adding a timeout. */
  DBusRemoveTimeoutFunction remove_timeout_function; /**< Callback for removing a timeout. */
  void *timeout_data;                                /**< Data for timeout callbacks */
  DBusFreeFunction timeout_free_data_function;       /**< Free function for timeout callback data */
};

/**
 * Creates a new timeout list. Returns #NULL if insufficient
 * memory exists.
 *
 * @returns the new timeout list, or #NULL on failure.
 */
DBusTimeoutList*
_dbus_timeout_list_new (void)
{
  DBusTimeoutList *timeout_list;

  timeout_list = dbus_new0 (DBusTimeoutList, 1);
  if (timeout_list == NULL)
    return NULL;

  return timeout_list;
}

/**
 * Frees a DBusTimeoutList.
 *
 * @param timeout_list the timeout list.
 */
void
_dbus_timeout_list_free (DBusTimeoutList *timeout_list)
{
  /* free timeout_data and remove timeouts as a side effect */
  _dbus_timeout_list_set_functions (timeout_list,
				    NULL, NULL, NULL, NULL);

  _dbus_list_foreach (&timeout_list->timeouts,
		      (DBusForeachFunction) _dbus_timeout_unref,
		      NULL);
  _dbus_list_clear (&timeout_list->timeouts);

  dbus_free (timeout_list);
}

/**
 * Sets the timeout functions. This function is the "backend"
 * for dbus_connection_set_timeout_functions().
 *
 * @param timeout_list the timeout list
 * @param add_function the add timeout function.
 * @param remove_function the remove timeout function.
 * @param data the data for those functions.
 * @param free_data_function the function to free the data.
 * @returns #FALSE if no memory
 *
 */
dbus_bool_t
_dbus_timeout_list_set_functions (DBusTimeoutList           *timeout_list,
				  DBusAddTimeoutFunction     add_function,
				  DBusRemoveTimeoutFunction  remove_function,
				  void                      *data,
				  DBusFreeFunction           free_data_function)
{
  /* Add timeouts with the new function, failing on OOM */
  if (add_function != NULL)
    {
      DBusList *link;
      
      link = _dbus_list_get_first_link (&timeout_list->timeouts);
      while (link != NULL)
        {
          DBusList *next = _dbus_list_get_next_link (&timeout_list->timeouts,
                                                     link);
      
          if (!(* add_function) (link->data, data))
            {
              /* remove it all again and return FALSE */
              DBusList *link2;
              
              link2 = _dbus_list_get_first_link (&timeout_list->timeouts);
              while (link2 != link)
                {
                  DBusList *next = _dbus_list_get_next_link (&timeout_list->timeouts,
                                                             link2);

                  (* remove_function) (link2->data, data);
                  
                  link2 = next;
                }

              return FALSE;
            }
      
          link = next;
        }
    }
  
  /* Remove all current timeouts from previous timeout handlers */

  if (timeout_list->remove_timeout_function != NULL)
    {
      _dbus_list_foreach (&timeout_list->timeouts,
			  (DBusForeachFunction) timeout_list->remove_timeout_function,
			  timeout_list->timeout_data);
    }

  if (timeout_list->timeout_free_data_function != NULL)
    (* timeout_list->timeout_free_data_function) (timeout_list->timeout_data);

  timeout_list->add_timeout_function = add_function;
  timeout_list->remove_timeout_function = remove_function;
  timeout_list->timeout_data = data;
  timeout_list->timeout_free_data_function = free_data_function;

  return TRUE;
}

/**
 * Adds a new timeout to the timeout list, invoking the
 * application DBusAddTimeoutFunction if appropriate.
 *
 * @param timeout_list the timeout list.
 * @param timeout the timeout to add.
 * @returns #TRUE on success, #FALSE If no memory.
 */
dbus_bool_t
_dbus_timeout_list_add_timeout (DBusTimeoutList *timeout_list,
				DBusTimeout     *timeout)
{
  if (!_dbus_list_append (&timeout_list->timeouts, timeout))
    return FALSE;

  _dbus_timeout_ref (timeout);

  if (timeout_list->add_timeout_function != NULL)
    {
      if (!(* timeout_list->add_timeout_function) (timeout,
                                                   timeout_list->timeout_data))
        {
          _dbus_list_remove_last (&timeout_list->timeouts, timeout);
          _dbus_timeout_unref (timeout);
          return FALSE;
        }
    }

  return TRUE;
}

/**
 * Removes a timeout from the timeout list, invoking the
 * application's DBusRemoveTimeoutFunction if appropriate.
 *
 * @param timeout_list the timeout list.
 * @param timeout the timeout to remove.
 */
void
_dbus_timeout_list_remove_timeout (DBusTimeoutList *timeout_list,
				   DBusTimeout     *timeout)
{
  if (!_dbus_list_remove (&timeout_list->timeouts, timeout))
    _dbus_assert_not_reached ("Nonexistent timeout was removed");

  if (timeout_list->remove_timeout_function != NULL)
    (* timeout_list->remove_timeout_function) (timeout,
					       timeout_list->timeout_data);

  _dbus_timeout_unref (timeout);
}

/** @} */

/**
 * @defgroup DBusTimeout DBusTimeout
 * @ingroup  DBus
 * @brief Object representing a timeout
 *
 * Types and functions related to DBusTimeout. A timeout
 * represents a timeout that the main loop needs to monitor,
 * as in Qt's QTimer or GLib's g_timeout_add().
 * 
 * @{
 */


/**
 * @typedef DBusTimeout
 *
 * Opaque object representing a timeout.
 */

/**
 * Gets the timeout interval. The dbus_timeout_handle()
 * should be called each time this interval elapses,
 * starting after it elapses once.
 *
 * @param timeout the DBusTimeout object.
 * @returns the interval in milliseconds.
 */
int
dbus_timeout_get_interval (DBusTimeout *timeout)
{
  return timeout->interval;
}

/**
 * Gets data previously set with dbus_timeout_set_data()
 * or #NULL if none.
 *
 * @param timeout the DBusTimeout object.
 * @returns previously-set data.
 */
void*
dbus_timeout_get_data (DBusTimeout *timeout)
{
  return timeout->data;
}

/**
 * Sets data which can be retrieved with dbus_timeout_get_data().
 * Intended for use by the DBusAddTimeoutFunction and
 * DBusRemoveTimeoutFunction to store their own data.  For example with
 * Qt you might store the QTimer for this timeout and with GLib
 * you might store a g_timeout_add result id.
 *
 * @param timeout the DBusTimeout object.
 * @param data the data.
 * @param free_data_function function to be called to free the data.
 */
void
dbus_timeout_set_data (DBusTimeout      *timeout,
		       void             *data,
		       DBusFreeFunction  free_data_function)
{
  if (timeout->free_data_function != NULL)
    (* timeout->free_data_function) (timeout->data);

  timeout->data = data;
  timeout->free_data_function = free_data_function;
}

/**
 * Calls the timeout handler for this timeout.
 * This function should be called when the timeout
 * occurs.
 *
 * @param timeout the DBusTimeout object.
 */
void
dbus_timeout_handle (DBusTimeout *timeout)
{
  (* timeout->handler) (timeout->handler_data);
}
