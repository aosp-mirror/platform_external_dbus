/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-pending-call.c Object representing a call in progress.
 *
 * Copyright (C) 2002, 2003 Red Hat Inc.
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

static dbus_int32_t notify_user_data_slot = -1;

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

  _dbus_assert (timeout_milliseconds >= 0 || timeout_milliseconds == -1);
  
  if (timeout_milliseconds == -1)
    timeout_milliseconds = _DBUS_DEFAULT_TIMEOUT_VALUE;

  /* it would probably seem logical to pass in _DBUS_INT_MAX for
   * infinite timeout, but then math in
   * _dbus_connection_block_for_reply would get all overflow-prone, so
   * smack that down.
   */
  if (timeout_milliseconds > _DBUS_ONE_HOUR_IN_MILLISECONDS * 6)
    timeout_milliseconds = _DBUS_ONE_HOUR_IN_MILLISECONDS * 6;
  
  if (!dbus_pending_call_allocate_data_slot (&notify_user_data_slot))
    return NULL;
  
  pending = dbus_new0 (DBusPendingCall, 1);
  
  if (pending == NULL)
    {
      dbus_pending_call_free_data_slot (&notify_user_data_slot);
      return NULL;
    }

  timeout = _dbus_timeout_new (timeout_milliseconds,
                               timeout_handler,
			       pending, NULL);  

  if (timeout == NULL)
    {
      dbus_pending_call_free_data_slot (&notify_user_data_slot);
      dbus_free (pending);
      return NULL;
    }
  
  pending->refcount.value = 1;
  pending->connection = connection;
  pending->timeout = timeout;

  _dbus_data_slot_list_init (&pending->slot_list);
  
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
  _dbus_assert (!pending->completed);
  
  pending->completed = TRUE;

  if (pending->function)
    {
      void *user_data;
      user_data = dbus_pending_call_get_data (pending,
                                              notify_user_data_slot);
      
      (* pending->function) (pending, user_data);
    }
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
 * @returns the pending call object
 */
DBusPendingCall *
dbus_pending_call_ref (DBusPendingCall *pending)
{
  _dbus_return_val_if_fail (pending != NULL, NULL);

  _dbus_atomic_inc (&pending->refcount);

  return pending;
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
      _dbus_data_slot_list_free (&pending->slot_list);
      
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

      dbus_pending_call_free_data_slot (&notify_user_data_slot);
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
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_pending_call_set_notify (DBusPendingCall              *pending,
                              DBusPendingCallNotifyFunction function,
                              void                         *user_data,
                              DBusFreeFunction              free_user_data)
{
  _dbus_return_val_if_fail (pending != NULL, FALSE);

  /* could invoke application code! */
  if (!dbus_pending_call_set_data (pending, notify_user_data_slot,
                                   user_data, free_user_data))
    return FALSE;
  
  pending->function = function;

  return TRUE;
}

/**
 * Cancels the pending call, such that any reply or error received
 * will just be ignored.  Drops the dbus library's internal reference
 * to the #DBusPendingCall so will free the call if nobody else is
 * holding a reference. However you usually get a reference
 * from dbus_connection_send() so probably your app owns a ref also.
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
 * @todo not thread safe? I guess it has to lock though it sucks
 *
 * @param pending the pending call
 * @returns #TRUE if a reply has been received */
dbus_bool_t
dbus_pending_call_get_completed (DBusPendingCall *pending)
{
  return pending->completed;
}

/**
 * Gets the reply, or returns #NULL if none has been received
 * yet. Ownership of the reply message passes to the caller. This
 * function can only be called once per pending call, since the reply
 * message is tranferred to the caller.
 * 
 * @param pending the pending call
 * @returns the reply message or #NULL.
 */
DBusMessage*
dbus_pending_call_steal_reply (DBusPendingCall *pending)
{
  DBusMessage *message;
  
  _dbus_return_val_if_fail (pending->completed, NULL);
  _dbus_return_val_if_fail (pending->reply != NULL, NULL);
  
  message = pending->reply;
  pending->reply = NULL;

  return message;
}

/**
 * Block until the pending call is completed.  The blocking is as with
 * dbus_connection_send_with_reply_and_block(); it does not enter the
 * main loop or process other messages, it simply waits for the reply
 * in question.
 *
 * If the pending call is already completed, this function returns
 * immediately.
 *
 * @todo when you start blocking, the timeout is reset, but it should
 * really only use time remaining since the pending call was created.
 *
 * @param pending the pending call
 */
void
dbus_pending_call_block (DBusPendingCall *pending)
{
  _dbus_connection_block_pending_call (pending);
}

static DBusDataSlotAllocator slot_allocator;
_DBUS_DEFINE_GLOBAL_LOCK (pending_call_slots);

/**
 * Allocates an integer ID to be used for storing application-specific
 * data on any DBusPendingCall. The allocated ID may then be used
 * with dbus_pending_call_set_data() and dbus_pending_call_get_data().
 * The passed-in slot must be initialized to -1, and is filled in
 * with the slot ID. If the passed-in slot is not -1, it's assumed
 * to be already allocated, and its refcount is incremented.
 * 
 * The allocated slot is global, i.e. all DBusPendingCall objects will
 * have a slot with the given integer ID reserved.
 *
 * @param slot_p address of a global variable storing the slot
 * @returns #FALSE on failure (no memory)
 */
dbus_bool_t
dbus_pending_call_allocate_data_slot (dbus_int32_t *slot_p)
{
  return _dbus_data_slot_allocator_alloc (&slot_allocator,
                                          _DBUS_LOCK_NAME (pending_call_slots),
                                          slot_p);
}

/**
 * Deallocates a global ID for #DBusPendingCall data slots.
 * dbus_pending_call_get_data() and dbus_pending_call_set_data() may
 * no longer be used with this slot.  Existing data stored on existing
 * DBusPendingCall objects will be freed when the #DBusPendingCall is
 * finalized, but may not be retrieved (and may only be replaced if
 * someone else reallocates the slot).  When the refcount on the
 * passed-in slot reaches 0, it is set to -1.
 *
 * @param slot_p address storing the slot to deallocate
 */
void
dbus_pending_call_free_data_slot (dbus_int32_t *slot_p)
{
  _dbus_return_if_fail (*slot_p >= 0);
  
  _dbus_data_slot_allocator_free (&slot_allocator, slot_p);
}

/**
 * Stores a pointer on a #DBusPendingCall, along
 * with an optional function to be used for freeing
 * the data when the data is set again, or when
 * the pending call is finalized. The slot number
 * must have been allocated with dbus_pending_call_allocate_data_slot().
 *
 * @param pending the pending_call
 * @param slot the slot number
 * @param data the data to store
 * @param free_data_func finalizer function for the data
 * @returns #TRUE if there was enough memory to store the data
 */
dbus_bool_t
dbus_pending_call_set_data (DBusPendingCall  *pending,
                            dbus_int32_t      slot,
                            void             *data,
                            DBusFreeFunction  free_data_func)
{
  DBusFreeFunction old_free_func;
  void *old_data;
  dbus_bool_t retval;

  _dbus_return_val_if_fail (pending != NULL, FALSE);
  _dbus_return_val_if_fail (slot >= 0, FALSE);

  retval = _dbus_data_slot_list_set (&slot_allocator,
                                     &pending->slot_list,
                                     slot, data, free_data_func,
                                     &old_free_func, &old_data);

  if (retval)
    {
      if (old_free_func)
        (* old_free_func) (old_data);
    }

  return retval;
}

/**
 * Retrieves data previously set with dbus_pending_call_set_data().
 * The slot must still be allocated (must not have been freed).
 *
 * @param pending the pending_call
 * @param slot the slot to get data from
 * @returns the data, or #NULL if not found
 */
void*
dbus_pending_call_get_data (DBusPendingCall   *pending,
                            dbus_int32_t       slot)
{
  void *res;

  _dbus_return_val_if_fail (pending != NULL, NULL);

  res = _dbus_data_slot_list_get (&slot_allocator,
                                  &pending->slot_list,
                                  slot);

  return res;
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
