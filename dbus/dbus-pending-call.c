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
#include "dbus-message-pending.h"
#include "dbus-list.h"
#include "dbus-threads.h"
#include "dbus-test.h"
#include "dbus-connection-internal.h"

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
 * @brief Internals of DBusPendingCall
 *
 * Object representing a reply message that we're waiting for.
 */
struct DBusPendingCall
{
  DBusAtomic refcount;                            /**< reference count */

  DBusPendingCallNotifyFunction function;         /**< Notifier when reply arrives. */
  void                     *user_data;            /**< user data for function */
  DBusFreeFunction          free_user_data;       /**< free the user data */

  DBusConnection *connection;                     /**< Connections we're associated with */
  DBusMessage *reply;                             /**< Reply (after we've received it) */
  DBusTimeout *timeout;                           /**< Timeout */

  DBusList *timeout_link;                         /**< Preallocated timeout response */
  
  dbus_uint32_t reply_serial;                     /**< Expected serial of reply */

  unsigned int completed : 1;                     /**< TRUE if completed */
  unsigned int timeout_added : 1;                 /**< Have added the timeout */
};

/**
 * Creates a new pending reply object.
 *
 * @param connection connection where reply will arrive
 * @param reply_serial reply serial of the expected reply
 * @returns a new #DBusPendingCall or #NULL if no memory.
 */
DBusPendingCall*
_dbus_pending_call_new (DBusConnection *connection,
                       dbus_uint32_t    reply_serial)
{
  DBusPendingCall *pending;

  pending = dbus_new (DBusPendingCall, 1);
  
  if (pending == NULL)
    return NULL;

  pending->refcount.value = 1;
  pending->connection = connection;
  pending->reply_serial = reply_serial;

  return pending;
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
      if (pending->free_user_data)
        (* pending->free_user_data) (pending->user_data);


      if (pending->connection != NULL)
        {
          _dbus_connection_pending_destroyed_locked (connection, pending);
          pending->connection = NULL;
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

  _DBUS_LOCK (pending_call);
  old_free_func = pending->free_user_data;
  old_user_data = pending->user_data;

  pending->user_data = user_data;
  pending->free_user_data = free_user_data;
  pending->function = function;
  _DBUS_UNLOCK (pending_call);

  if (old_free_func)
    (* old_free_func) (old_user_data);
}

/** @} */

#ifdef DBUS_BUILD_TESTS
static DBusPendingResult
test_pending (DBusPendingCall *pending,
              DBusConnection     *connection,
              DBusMessage        *message,
              void               *user_data)
{
  return DBUS_PENDING_RESULT_NOT_YET_HANDLED;
}

static void
free_test_data (void *data)
{
  /* does nothing */
}

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
