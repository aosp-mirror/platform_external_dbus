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
 * @{
 */

/**
 * Registers a connection with the bus. This must be the first
 * thing an application does when connecting to the message bus.
 *
 * @todo if we get an error reply, it has to be converted into
 * DBusError and returned
 * 
 * @param connection the connection
 * @param error place to store errors
 * @returns the client's unique service name, #NULL on error
 */
char*
dbus_bus_register_client (DBusConnection *connection,
                          DBusError      *error)
{
  DBusMessage *message, *reply;
  char *name;
  
  message = dbus_message_new (DBUS_SERVICE_DBUS,
			      DBUS_MESSAGE_HELLO);

  if (!message)
    {
      _DBUS_SET_OOM (error);
      return NULL;
    }
  
  reply = dbus_connection_send_with_reply_and_block (connection, message, -1, error);

  dbus_message_unref (message);
  
  if (reply == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      return NULL;
    }

  if (!dbus_message_get_args (reply, error,
                              DBUS_TYPE_STRING, &name,
                              0))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      return NULL;
    }
  
  return name;
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
