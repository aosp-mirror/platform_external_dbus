/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-bus.h  Convenience functions for communicating with the bus.
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

#include "dbus-bus.h"
#include "dbus-protocol.h"

/**
 * @defgroup DBusBus Convenience functinos for communicating with the bus.
 * @ingroup DBus
 * @brief Convenience functinos for communicating with the bus.
 *
 * @{
 */

/**
 * Registers a connection with the bus. This is needed to send messages
 * to other clients.
 *
 * @param connection The connection
 * @param result address where a result code can be returned. 
 * @returns the service name of which the client is known as.
 */
char *
dbus_bus_register_client (DBusConnection *connection,
			  DBusResultCode *result)
{
  DBusMessage *message, *reply;
  DBusResultCode code;
  char *name;
  
  message = dbus_message_new (DBUS_SERVICE_DBUS,
			      DBUS_MESSAGE_HELLO);

  if (!message)
    {
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return NULL;
    }
  
  reply = dbus_connection_send_message_with_reply_and_block (connection, message, -1, result);

  dbus_message_unref (message);
  
  if (!reply)
    return NULL;

  code = dbus_message_get_fields (reply,
				  DBUS_TYPE_STRING, &name,
				  0);
  if (code != DBUS_RESULT_SUCCESS)
    {
      dbus_set_result (result, code);
      return NULL;
    }

  dbus_set_result (result, DBUS_RESULT_SUCCESS);
		   
  return name;
}

/**
 * Asks the bus to try to acquire a certain service.
 *
 * @param connection the connection
 * @param service_name the service name
 * @param flags flags
 * @param result address where a result code can be returned. 
 * @returns a result code.
 */ 
int
dbus_bus_acquire_service (DBusConnection *connection,
			  const char     *service_name,
			  unsigned int    flags,
			  DBusResultCode *result)
{
  DBusMessage *message, *reply;
  int service_result;
  DBusResultCode code;
  
  message = dbus_message_new (DBUS_SERVICE_DBUS,
                              DBUS_MESSAGE_ACQUIRE_SERVICE);

  if (!message)
    {
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return -1;
    }
 
  if (!dbus_message_append_fields (message,
				   DBUS_TYPE_STRING, service_name,
				   DBUS_TYPE_UINT32, flags,
				   0))
    {
      dbus_message_unref (message);
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return -1;
    }
  
  reply = dbus_connection_send_message_with_reply_and_block (connection, message, -1, result);
  dbus_message_unref (message);
  
  if (!reply)
    return -1;

  code = dbus_message_get_fields (reply,
				  DBUS_TYPE_UINT32, &service_result,
				  0);
  if (code != DBUS_RESULT_SUCCESS)
    {
      dbus_set_result (result, code);
      return -1;
    }

  dbus_set_result (result, DBUS_RESULT_SUCCESS);

  return service_result;
}

/**
 * Checks whether a certain service exists.
 *
 * @param connection the connection
 * @param service_name the service name
 * @param result address where a result code can be returned. 
 * @returns #TRUE if the service exists, #FALSE otherwise.
 */
dbus_bool_t
dbus_bus_service_exists (DBusConnection *connection,
			 const char     *service_name,
			 DBusResultCode *result)
{
  DBusMessage *message, *reply;
  unsigned int exists;
  DBusResultCode code;
  
  message = dbus_message_new (DBUS_SERVICE_DBUS,
                              DBUS_MESSAGE_SERVICE_EXISTS);
  if (!message)
    {
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return FALSE;
    }
  
  if (!dbus_message_append_fields (message,
				   DBUS_TYPE_STRING, service_name,
				   0))
    {
      dbus_message_unref (message);
      dbus_set_result (result, DBUS_RESULT_NO_MEMORY);
      return FALSE;
    }
  
  reply = dbus_connection_send_message_with_reply_and_block (connection, message, -1, result);
  dbus_message_unref (message);

  if (!reply)
    return FALSE;

  code = dbus_message_get_fields (reply,
				  DBUS_TYPE_UINT32, &exists,
				  0);
  if (code != DBUS_RESULT_SUCCESS)
    {
      dbus_set_result (result, code);
      return FALSE;
    }
  
  dbus_set_result (result, DBUS_RESULT_SUCCESS);
  return (result != FALSE);
}

/** @} */
