/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-message-handler.c Sender/receiver of messages.
 *
 * Copyright (C) 2002  Red Hat Inc.
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
#include "dbus-message-handler.h"
#include "dbus-list.h"
#include "dbus-threads.h"
#include "dbus-connection-internal.h"

/**
 * @defgroup DBusMessageHandlerInternals DBusMessageHandler implementation details
 * @ingroup DBusInternals
 * @brief DBusMessageHandler private implementation details.
 *
 * The guts of DBusMessageHandler and its methods.
 *
 * @{
 */

_DBUS_DEFINE_GLOBAL_LOCK (message_handler);

/**
 * @brief Internals of DBusMessageHandler
 * 
 * Object that can send and receive messages.
 */
struct DBusMessageHandler
{
  int refcount;                                   /**< reference count */

  DBusHandleMessageFunction function;             /**< handler function */
  void                     *user_data;            /**< user data for function */
  DBusFreeFunction          free_user_data;       /**< free the user data */

  DBusList *connections;                          /**< connections we're registered with */
};

/**
 * Add this connection to the list used by this message handler.
 * When the message handler goes away, the connection
 * will be notified.
 *
 * @param handler the message handler
 * @param connection the connection
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
_dbus_message_handler_add_connection (DBusMessageHandler *handler,
                                      DBusConnection     *connection)
{
  dbus_bool_t res;
  
  _DBUS_LOCK (message_handler);
  /* This is a bit wasteful - we just put the connection in the list
   * once per time it's added. :-/
   */
  if (!_dbus_list_prepend (&handler->connections, connection))
    res = FALSE;
  else
    res = TRUE;

  _DBUS_UNLOCK (message_handler);
  
  return res;
}

/**
 * Reverses the effect of _dbus_message_handler_add_connection().
 * @param handler the message handler
 * @param connection the connection
 */
void
_dbus_message_handler_remove_connection (DBusMessageHandler *handler,
                                         DBusConnection     *connection)
{
  _DBUS_LOCK (message_handler);
  if (!_dbus_list_remove (&handler->connections, connection))
    _dbus_warn ("Function _dbus_message_handler_remove_connection() called when the connection hadn't been added\n");
  _DBUS_UNLOCK (message_handler);
}


/**
 * Handles the given message, by dispatching the handler function
 * for this DBusMessageHandler, if any.
 * 
 * @param handler the handler
 * @param connection the connection that received the message
 * @param message the message
 *
 * @returns what to do with the message
 */
DBusHandlerResult
_dbus_message_handler_handle_message (DBusMessageHandler        *handler,
                                      DBusConnection            *connection,
                                      DBusMessage               *message)
{
  DBusHandleMessageFunction function;
  void  *user_data;
  
  _DBUS_LOCK (message_handler);
  function = handler->function;
  user_data = handler->user_data;
  _DBUS_UNLOCK (message_handler);
  
  /* This function doesn't ref handler/connection/message
   * since that's done in dbus_connection_dispatch().
   */
  if (function != NULL)
    return (* function) (handler, connection, message, user_data);
  else
    return DBUS_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

/** @} */

/**
 * @defgroup DBusMessageHandler DBusMessageHandler
 * @ingroup  DBus
 * @brief Message processor  
 *
 * A DBusMessageHandler is an object that can send and receive
 * messages. Typically the handler is registered with one or
 * more DBusConnection objects and processes some types of
 * messages received from the connection.
 *
 * @{
 */

/**
 * @typedef DBusMessageHandler
 *
 * Opaque data type representing a message handler.
 */

/**
 * Creates a new message handler. The handler function
 * may be #NULL for a no-op handler or a handler to
 * be assigned a function later.
 *
 * @param function function to call to handle a message
 * @param user_data data to pass to the function
 * @param free_user_data function to call to free the user data
 * @returns a new DBusMessageHandler or #NULL if no memory.
 */
DBusMessageHandler*
dbus_message_handler_new (DBusHandleMessageFunction function,
                          void                     *user_data,
                          DBusFreeFunction          free_user_data)
{
  DBusMessageHandler *handler;

  handler = dbus_new (DBusMessageHandler, 1);

  if (handler == NULL)
    return NULL;
  
  handler->refcount = 1;
  handler->function = function;
  handler->user_data = user_data;
  handler->free_user_data = free_user_data;
  handler->connections = NULL;

  return handler;
}

/**
 * Increments the reference count on a message handler.
 *
 * @param handler the handler
 */
void
dbus_message_handler_ref (DBusMessageHandler *handler)
{
  _DBUS_LOCK (message_handler);
  _dbus_assert (handler != NULL);
  
  handler->refcount += 1;
  _DBUS_UNLOCK (message_handler);
}

/**
 * Decrements the reference count on a message handler,
 * freeing the handler if the count reaches 0.
 *
 * @param handler the handler
 */
void
dbus_message_handler_unref (DBusMessageHandler *handler)
{
  int refcount;
  
  _DBUS_LOCK (message_handler);
  
  _dbus_assert (handler != NULL);
  _dbus_assert (handler->refcount > 0);

  handler->refcount -= 1;
  refcount = handler->refcount;
  
  _DBUS_UNLOCK (message_handler);
  
  if (refcount == 0)
    {
      DBusList *link;
      
      if (handler->free_user_data)
        (* handler->free_user_data) (handler->user_data);
       
      link = _dbus_list_get_first_link (&handler->connections);
       while (link != NULL)
         {
           DBusConnection *connection = link->data;

           _dbus_connection_handler_destroyed_locked (connection, handler);
           
           link = _dbus_list_get_next_link (&handler->connections, link);
         }

       _dbus_list_clear (&handler->connections);

       dbus_free (handler);
    }
}

/**
 * Gets the user data for the handler (the same user data
 * passed to the handler function.)
 *
 * @param handler the handler
 * @returns the user data
 */
void*
dbus_message_handler_get_data (DBusMessageHandler *handler)
{
  void* user_data;
  _DBUS_LOCK (message_handler);
  user_data = handler->user_data;
  _DBUS_UNLOCK (message_handler);
  return user_data;
}

/**
 * Sets the user data for the handler (the same user data
 * to be passed to the handler function). Frees any previously-existing
 * user data with the previous free_user_data function.
 *
 * @param handler the handler
 * @param user_data the user data
 * @param free_user_data free function for the data
 */
void
dbus_message_handler_set_data (DBusMessageHandler *handler,
                               void               *user_data,
                               DBusFreeFunction    free_user_data)
{
  DBusFreeFunction old_free_func;
  void *old_user_data;
  
  _DBUS_LOCK (message_handler);
  old_free_func = handler->free_user_data;
  old_user_data = handler->user_data;

  handler->user_data = user_data;
  handler->free_user_data = free_user_data;
  _DBUS_UNLOCK (message_handler);

  if (old_free_func)
    (* old_free_func) (old_user_data);

}

/**
 * Sets the handler function. Call dbus_message_handler_set_data()
 * to set the user data for the function.
 *
 * @param handler the handler
 * @param function the function
 */
void
dbus_message_handler_set_function (DBusMessageHandler        *handler,
                                   DBusHandleMessageFunction  function)
{
  _DBUS_LOCK (message_handler);
  handler->function = function;
  _DBUS_UNLOCK (message_handler);
}

/** @} */
