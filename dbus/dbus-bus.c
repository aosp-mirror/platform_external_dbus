/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-bus.c  Convenience functions for communicating with the bus.
 *
 * Copyright (C) 2003  CodeFactory AB
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

#include "dbus-bus.h"
#include "dbus-protocol.h"
#include "dbus-internals.h"

/**
 * @defgroup DBusBus Message bus APIs
 * @ingroup DBus
 * @brief Functions for communicating with the message bus
 *
 */


/**
 * @defgroup DBusBusInternals Message bus APIs internals
 * @ingroup DBusInternals
 * @brief Internals of functions for communicating with the message bus
 *
 * @{
 */

/**
 * Block of message-bus-related data we attach to each
 * #DBusConnection used with these convenience functions.
 *
 */
typedef struct
{
  char *base_service; /**< Base service name of this connection */

} BusData;

/** The slot we have reserved to store BusData
 */
static int bus_data_slot = -1;
/** Number of connections using the slot
 */
static int bus_data_slot_refcount = 0;

/**
 * Lock for bus_data_slot and bus_data_slot_refcount
 */
static DBusMutex *slot_lock;

/**
 * Initialize the mutex used for bus_data_slot
 *
 * @returns the mutex
 */
DBusMutex *
_dbus_bus_init_lock (void)
{
  slot_lock = dbus_mutex_new ();
  return slot_lock;
}

static dbus_bool_t
data_slot_ref (void)
{
  dbus_mutex_lock (slot_lock);

  if (bus_data_slot < 0)
    {
      bus_data_slot = dbus_connection_allocate_data_slot ();
      
      if (bus_data_slot < 0)
        {
          dbus_mutex_unlock (slot_lock);
          return FALSE;
        }

      _dbus_assert (bus_data_slot_refcount == 0);
    }

  bus_data_slot_refcount += 1;

  dbus_mutex_unlock (slot_lock);

  return TRUE;
}

static void
data_slot_unref (void)
{
  dbus_mutex_lock (slot_lock);

  _dbus_assert (bus_data_slot_refcount > 0);
  _dbus_assert (bus_data_slot >= 0);

  bus_data_slot_refcount -= 1;

  if (bus_data_slot_refcount == 0)
    {
      dbus_connection_free_data_slot (bus_data_slot);
      bus_data_slot = -1;
    }

  dbus_mutex_unlock (slot_lock);
}

static void
bus_data_free (void *data)
{
  BusData *bd = data;

  dbus_free (bd->base_service);
  dbus_free (bd);

  data_slot_unref ();
}

static BusData*
ensure_bus_data (DBusConnection *connection)
{
  BusData *bd;

  if (!data_slot_ref ())
    return NULL;

  bd = dbus_connection_get_data (connection, bus_data_slot);
  if (bd == NULL)
    {      
      bd = dbus_new0 (BusData, 1);
      if (bd == NULL)
        {
          data_slot_unref ();
          return NULL;
        }
      
      if (!dbus_connection_set_data (connection, bus_data_slot, bd,
                                     bus_data_free))
        {
          dbus_free (bd);
          data_slot_unref ();
          return NULL;
        }

      /* Data slot refcount now held by the BusData */
    }
  else
    {
      data_slot_unref ();
    }

  return bd;
}

/** @} */ /* end of implementation details docs */

/**
 * @addtogroup DBusBus
 * @{
 */

/**
 * Registers a connection with the bus. This must be the first
 * thing an application does when connecting to the message bus.
 * If registration succeeds, the base service name will be set,
 * and can be obtained using dbus_bus_get_base_service().
 *
 * @todo if we get an error reply, it has to be converted into
 * DBusError and returned
 * 
 * @param connection the connection
 * @param error place to store errors
 * @returns #TRUE on success
 */
dbus_bool_t
dbus_bus_register (DBusConnection *connection,
                   DBusError      *error)
{
  DBusMessage *message, *reply;
  char *name;
  BusData *bd;

  bd = ensure_bus_data (connection);
  if (bd == NULL)
    {
      _DBUS_SET_OOM (error);
      return FALSE;
    }

  if (bd->base_service != NULL)
    {
      _dbus_warn ("Attempt to register the same DBusConnection with the message bus, but it is already registered\n");
      /* This isn't an error, it's a programming bug. We'll be nice
       * and not _dbus_assert_not_reached()
       */
      return TRUE;
    }
  
  message = dbus_message_new (DBUS_SERVICE_DBUS,
			      DBUS_MESSAGE_HELLO);

  if (!message)
    {
      _DBUS_SET_OOM (error);
      return FALSE;
    }
  
  reply = dbus_connection_send_with_reply_and_block (connection, message, -1, error);

  dbus_message_unref (message);
  
  if (reply == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      return FALSE;
    }

  if (!dbus_message_get_args (reply, error,
                              DBUS_TYPE_STRING, &name,
                              0))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      return FALSE;
    }

  bd->base_service = name;
  
  return TRUE;
}


/**
 * Sets the base service name of the connection.
 * Can only be used if you registered with the
 * bus manually (i.e. if you did not call
 * dbus_bus_register()). Can only be called
 * once per connection.
 *
 * @param connection the connection
 * @param base_service the base service name
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
dbus_bus_set_base_service (DBusConnection *connection,
                           const char     *base_service)
{
  BusData *bd;

  bd = ensure_bus_data (connection);
  if (bd == NULL)
    return FALSE;

  _dbus_assert (bd->base_service == NULL);
  _dbus_assert (base_service != NULL);
  
  bd->base_service = _dbus_strdup (base_service);
  return bd->base_service != NULL;
}

/**
 * Gets the base service name of the connection.
 * Only possible after the connection has been registered
 * with the message bus.
 *
 * @param connection the connection
 * @returns the base service name
 */
const char*
dbus_bus_get_base_service (DBusConnection *connection)
{
  BusData *bd;

  bd = ensure_bus_data (connection);
  if (bd == NULL)
    return NULL;
  
  return bd->base_service;
}

/**
 * Asks the bus to try to acquire a certain service.
 *
 * @todo these docs are not complete, need to document the
 * return value and flags
 * 
 * @todo if we get an error reply, it has to be converted into
 * DBusError and returned
 *
 * @param connection the connection
 * @param service_name the service name
 * @param flags flags
 * @param error location to store the error
 * @returns a result code, -1 if error is set
 */ 
int
dbus_bus_acquire_service (DBusConnection *connection,
			  const char     *service_name,
			  unsigned int    flags,
                          DBusError      *error)
{
  DBusMessage *message, *reply;
  int service_result;
  
  message = dbus_message_new (DBUS_SERVICE_DBUS,
                              DBUS_MESSAGE_ACQUIRE_SERVICE);

  if (message == NULL)
    {
      _DBUS_SET_OOM (error);
      return -1;
    }
 
  if (!dbus_message_append_args (message,
				 DBUS_TYPE_STRING, service_name,
				 DBUS_TYPE_UINT32, flags,
				 0))
    {
      dbus_message_unref (message);
      _DBUS_SET_OOM (error);
      return -1;
    }
  
  reply = dbus_connection_send_with_reply_and_block (connection, message, -1,
                                                     error);
  
  dbus_message_unref (message);
  
  if (reply == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      return -1;
    }

  if (!dbus_message_get_args (reply, error,
                              DBUS_TYPE_UINT32, &service_result,
                              0))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      return -1;
    }

  return service_result;
}

/**
 * Checks whether a certain service exists.
 *
 * @todo the SERVICE_EXISTS message should use BOOLEAN not UINT32
 *
 * @param connection the connection
 * @param service_name the service name
 * @param error location to store any errors
 * @returns #TRUE if the service exists, #FALSE if not or on error
 */
dbus_bool_t
dbus_bus_service_exists (DBusConnection *connection,
			 const char     *service_name,
                         DBusError      *error)
{
  DBusMessage *message, *reply;
  unsigned int exists;
  
  message = dbus_message_new (DBUS_SERVICE_DBUS,
                              DBUS_MESSAGE_SERVICE_EXISTS);
  if (message == NULL)
    {
      _DBUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_append_args (message,
				 DBUS_TYPE_STRING, service_name,
				 0))
    {
      dbus_message_unref (message);
      _DBUS_SET_OOM (error);
      return FALSE;
    }
  
  reply = dbus_connection_send_with_reply_and_block (connection, message, -1, error);
  dbus_message_unref (message);

  if (reply == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      return FALSE;
    }

  if (!dbus_message_get_args (reply, error,
                              DBUS_TYPE_UINT32, &exists,
                              0))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      return FALSE;
    }
  
  return (exists != FALSE);
}

/** @} */
