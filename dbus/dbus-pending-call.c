/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-pending-call.c Object representing a call in progress.
 *
 * Copyright (C) 2002, 2003 Red Hat Inc.
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
#include "dbus-connection-internal.h"
#include "dbus-pending-call.h"
#include "dbus-list.h"
#include "dbus-threads.h"
#include "dbus-test.h"

/**
 * @defgroup DBusPendingCallInternals DBusPendingCall implementation details
 * @ingroup DBusInternals
 * @brief DBusPendingCall private implementation details.
 *
 * The guts of DBusPendingCall and its methods.
 *
 * @{
 */

/**
 * Creates a new pending reply object.
 *
 * @param connection connection where reply will arrive
 * @param timeout_milliseconds length of timeout, -1 for default
 * @param timeout_handler timeout handler, takes pending call as data
 * @returns a new #DBusPendingCall or #NULL if no memory.
 */
DBusPendingCall*
_dbus_pending_call_new (DBusConnection    *connection,
                        int                timeout_milliseconds,
                        DBusTimeoutHandler timeout_handler)
{
  DBusPendingCall *pending;
  DBusTimeout *timeout;

  _dbus_return_val_if_fail (timeout_milliseconds >= 0 || timeout_milliseconds == -1, FALSE);
  
  if (timeout_milliseconds == -1)
    timeout_milliseconds = _DBUS_DEFAULT_TIMEOUT_VALUE;
  
  pending = dbus_new (DBusPendingCall, 1);
  
  if (pending == NULL)
    return NULL;

  timeout = _dbus_timeout_new (timeout_milliseconds,
                               timeout_handler,
			       pending, NULL);  

  if (timeout == NULL)
    {
      dbus_free (pending);
      return NULL;
    }
  
  pending->refcount.value = 1;
  pending->connection = connection;
  pending->timeout = timeout;
  
  return pending;
}

/**
 * Calls notifier function for the pending call
 * and sets the call to completed.
 *
 * @param pending the pending call
 * 
 */
void
_dbus_pending_call_notify (DBusPendingCall *pending)
{
  pending->completed = TRUE;

  if (pending->function)
    (* pending->function) (pending, pending->user_data);
}

/** @} */

/**
 * @defgroup DBusPendingCall DBusPendingCall
 * @ingroup  DBus
 * @brief Pending reply to a method call message
 *
 * A DBusPendingCall is an object representing an
 * expected reply. A #DBusPendingCall can be created
 * when you send a message that should have a reply.
 *
 * @{
 */

/**
 * @typedef DBusPendingCall
 *
 * Opaque data type representing a message pending.
 */

/**
 * Increments the reference count on a pending call.
 *
 * @param pending the pending call object
 */
void
dbus_pending_call_ref (DBusPendingCall *pending)
{
  _dbus_return_if_fail (pending != NULL);

  _dbus_atomic_inc (&pending->refcount);
}

/**
 * Decrements the reference count on a pending call,
 * freeing it if the count reaches 0.
 *
 * @param pending the pending call object
 */
void
dbus_pending_call_unref (DBusPendingCall *pending)
{
  dbus_bool_t last_unref;

  _dbus_return_if_fail (pending != NULL);

  last_unref = (_dbus_atomic_dec (&pending->refcount) == 1);

  if (last_unref)
    {
      /* If we get here, we should be already detached
       * from the connection, or never attached.
       */
      _dbus_assert (pending->connection == NULL);
      _dbus_assert (!pending->timeout_added);  

      /* this assumes we aren't holding connection lock... */
      if (pending->free_user_data)
        (* pending->free_user_data) (pending->user_data);

      if (pending->timeout != NULL)
        _dbus_timeout_unref (pending->timeout);
      
      if (pending->timeout_link)
        {
          dbus_message_unref ((DBusMessage *)pending->timeout_link->data);
          _dbus_list_free_link (pending->timeout_link);
          pending->timeout_link = NULL;
        }

      if (pending->reply)
        {
          dbus_message_unref (pending->reply);
          pending->reply = NULL;
        }
      
      dbus_free (pending);
    }
}

/**
 * Sets a notification function to be called when the reply is
 * received or the pending call times out.
 *
 * @param pending the pending call
 * @param function notifier function
 * @param user_data data to pass to notifier function
 * @param free_user_data function to free the user data
 * 
 */
void
dbus_pending_call_set_notify (DBusPendingCall              *pending,
                              DBusPendingCallNotifyFunction function,
                              void                         *user_data,
                              DBusFreeFunction              free_user_data)
{
  DBusFreeFunction old_free_func;
  void *old_user_data;

  _dbus_return_if_fail (pending != NULL);

  old_free_func = pending->free_user_data;
  old_user_data = pending->user_data;

  pending->user_data = user_data;
  pending->free_user_data = free_user_data;
  pending->function = function;

  if (old_free_func)
    (* old_free_func) (old_user_data);
}

/**
 * Cancels the pending call, such that any reply
 * or error received will just be ignored.
 * Drops at least one reference to the #DBusPendingCall
 * so will free the call if nobody else is holding
 * a reference.
 * 
 * @param pending the pending call
 */
void
dbus_pending_call_cancel (DBusPendingCall *pending)
{
  if (pending->connection)
    _dbus_connection_remove_pending_call (pending->connection,
                                          pending);
}

/**
 * Checks whether the pending call has received a reply
 * yet, or not.
 *
 * @param pending the pending call
 * @returns #TRUE if a reply has been received
 */
dbus_bool_t
dbus_pending_call_get_completed (DBusPendingCall *pending)
{
  return pending->completed;
}

/**
 * Gets the reply, or returns #NULL if none has been received yet. The
 * reference count is not incremented on the returned message, so you
 * have to keep a reference count on the pending call (or add one
 * to the message).
 *
 * @param pending the pending call
 * @returns the reply message or #NULL.
 */
DBusMessage*
dbus_pending_call_get_reply (DBusPendingCall *pending)
{
  return pending->reply;
}

/**
 * Block until the pending call is completed.  The blocking is as with
 * dbus_connection_send_with_reply_and_block(); it does not enter the
 * main loop or process other messages, it simply waits for the reply
 * in question.
 *
 * @todo when you start blocking, the timeout is reset, but it should
 * really only use time remaining since the pending call was created.
 *
 * @param pending the pending call
 */
void
dbus_pending_call_block (DBusPendingCall *pending)
{
  DBusMessage *message;
  
  message = _dbus_connection_block_for_reply (pending->connection,
                                              pending->reply_serial,
                                              dbus_timeout_get_interval (pending->timeout));

  _dbus_connection_lock (pending->connection);
  _dbus_pending_call_complete_and_unlock (pending, message);
  dbus_message_unref (message);
}

/** @} */

#ifdef DBUS_BUILD_TESTS

/**
 * @ingroup DBusPendingCallInternals
 * Unit test for DBusPendingCall.
 *
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_pending_call_test (const char *test_data_dir)
{  

  return TRUE;
}
#endif /* DBUS_BUILD_TESTS */
