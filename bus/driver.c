/* -*- mode: C; c-file-style: "gnu" -*- */
/* driver.c  Bus client (driver)
 *
 * Copyright (C) 2003 CodeFactory AB
 * Copyright (C) 2003, 2004 Red Hat, Inc.
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

#include "activation.h"
#include "connection.h"
#include "driver.h"
#include "dispatch.h"
#include "services.h"
#include "signals.h"
#include "utils.h"
#include <dbus/dbus-string.h>
#include <dbus/dbus-internals.h>
#include <string.h>

static dbus_bool_t bus_driver_send_welcome_message (DBusConnection *connection,
                                                    DBusMessage    *hello_message,
                                                    BusTransaction *transaction,
                                                    DBusError      *error);

dbus_bool_t
bus_driver_send_service_owner_changed (const char     *service_name,
				       const char     *old_owner,
				       const char     *new_owner,
				       BusTransaction *transaction,
				       DBusError      *error)
{
  DBusMessage *message;
  dbus_bool_t retval;
  const char null_service[] = { '\000' };

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  _dbus_verbose ("sending service owner changed: %s [%s -> %s]\n",
                 service_name, 
                 old_owner ? old_owner : null_service, 
                 new_owner ? new_owner : null_service);

  message = dbus_message_new_signal (DBUS_PATH_ORG_FREEDESKTOP_DBUS,
                                     DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS,
                                     "ServiceOwnerChanged");
  
  if (message == NULL)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_set_sender (message, DBUS_SERVICE_ORG_FREEDESKTOP_DBUS))
    goto oom;

  if (!dbus_message_append_args (message,
                                 DBUS_TYPE_STRING, service_name,
                                 DBUS_TYPE_STRING, old_owner ? old_owner : null_service,
                                 DBUS_TYPE_STRING, new_owner ? new_owner : null_service,
                                 DBUS_TYPE_INVALID))
    goto oom;

  _dbus_assert (dbus_message_has_signature (message, "sss"));
  
  retval = bus_dispatch_matches (transaction, NULL, NULL, message, error);
  dbus_message_unref (message);

  return retval;

 oom:
  dbus_message_unref (message);
  BUS_SET_OOM (error);
  return FALSE;
}

dbus_bool_t
bus_driver_send_service_lost (DBusConnection *connection,
			      const char     *service_name,
                              BusTransaction *transaction,
                              DBusError      *error)
{
  DBusMessage *message;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  message = dbus_message_new_signal (DBUS_PATH_ORG_FREEDESKTOP_DBUS,
                                     DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS,
                                     "ServiceLost");
  
  if (message == NULL)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_set_destination (message, bus_connection_get_name (connection)) ||
      !dbus_message_append_args (message,
                                 DBUS_TYPE_STRING, service_name,
                                 DBUS_TYPE_INVALID))
    {
      dbus_message_unref (message);
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!bus_transaction_send_from_driver (transaction, connection, message))
    {
      dbus_message_unref (message);
      BUS_SET_OOM (error);
      return FALSE;
    }
  else
    {
      dbus_message_unref (message);
      return TRUE;
    }
}

dbus_bool_t
bus_driver_send_service_acquired (DBusConnection *connection,
                                  const char     *service_name,
                                  BusTransaction *transaction,
                                  DBusError      *error)
{
  DBusMessage *message;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  message = dbus_message_new_signal (DBUS_PATH_ORG_FREEDESKTOP_DBUS,
                                     DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS,
                                     "ServiceAcquired");

  if (message == NULL)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_set_destination (message, bus_connection_get_name (connection)) ||
      !dbus_message_append_args (message,
                                 DBUS_TYPE_STRING, service_name,
                                 DBUS_TYPE_INVALID))
    {
      dbus_message_unref (message);
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!bus_transaction_send_from_driver (transaction, connection, message))
    {
      dbus_message_unref (message);
      BUS_SET_OOM (error);
      return FALSE;
    }
  else
    {
      dbus_message_unref (message);
      return TRUE;
    }
}

static dbus_bool_t
create_unique_client_name (BusRegistry *registry,
                           DBusString  *str)
{
  /* We never want to use the same unique client name twice, because
   * we want to guarantee that if you send a message to a given unique
   * name, you always get the same application. So we use two numbers
   * for INT_MAX * INT_MAX combinations, should be pretty safe against
   * wraparound.
   */
  /* FIXME these should be in BusRegistry rather than static vars */
  static int next_major_number = 0;
  static int next_minor_number = 0;
  int len;
  
  len = _dbus_string_get_length (str);
  
  while (TRUE)
    {
      /* start out with 1-0, go to 1-1, 1-2, 1-3,
       * up to 1-MAXINT, then 2-0, 2-1, etc.
       */
      if (next_minor_number <= 0)
        {
          next_major_number += 1;
          next_minor_number = 0;
          if (next_major_number <= 0)
            _dbus_assert_not_reached ("INT_MAX * INT_MAX clients were added");
        }

      _dbus_assert (next_major_number > 0);
      _dbus_assert (next_minor_number >= 0);

      /* appname:MAJOR-MINOR */
      
      if (!_dbus_string_append (str, ":"))
        return FALSE;
      
      if (!_dbus_string_append_int (str, next_major_number))
        return FALSE;

      if (!_dbus_string_append (str, "."))
        return FALSE;
      
      if (!_dbus_string_append_int (str, next_minor_number))
        return FALSE;

      next_minor_number += 1;
      
      /* Check if a client with the name exists */
      if (bus_registry_lookup (registry, str) == NULL)
	break;

      /* drop the number again, try the next one. */
      _dbus_string_set_length (str, len);
    }

  return TRUE;
}

static dbus_bool_t
bus_driver_handle_hello (DBusConnection *connection,
                         BusTransaction *transaction,
                         DBusMessage    *message,
                         DBusError      *error)
{
  DBusString unique_name;
  BusService *service;
  dbus_bool_t retval;
  BusRegistry *registry;
  BusConnections *connections;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (bus_connection_is_active (connection))
    {
      /* We already handled an Hello message for this connection. */
      dbus_set_error (error, DBUS_ERROR_FAILED,
                      "Already handled an Hello message");
      return FALSE;
    }

  /* Note that when these limits are exceeded we don't disconnect the
   * connection; we just sort of leave it hanging there until it times
   * out or disconnects itself or is dropped due to the max number of
   * incomplete connections. It's even OK if the connection wants to
   * retry the hello message, we support that.
   */
  connections = bus_connection_get_connections (connection);
  if (!bus_connections_check_limits (connections, connection,
                                     error))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      return FALSE;
    }
  
  if (!_dbus_string_init (&unique_name))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  retval = FALSE;

  registry = bus_connection_get_registry (connection);
  
  if (!create_unique_client_name (registry, &unique_name))
    {
      BUS_SET_OOM (error);
      goto out_0;
    }

  if (!bus_connection_complete (connection, &unique_name, error))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto out_0;
    }
  
  if (!dbus_message_set_sender (message,
                                bus_connection_get_name (connection)))
    {
      BUS_SET_OOM (error);
      goto out_0;
    }
  
  if (!bus_driver_send_welcome_message (connection, message, transaction, error))
    goto out_0;

  /* Create the service */
  service = bus_registry_ensure (registry,
                                 &unique_name, connection, transaction, error);
  if (service == NULL)
    goto out_0;
  
  bus_service_set_prohibit_replacement (service, TRUE);

  _dbus_assert (bus_connection_is_active (connection));
  retval = TRUE;
  
 out_0:
  _dbus_string_free (&unique_name);
  return retval;
}

static dbus_bool_t
bus_driver_send_welcome_message (DBusConnection *connection,
                                 DBusMessage    *hello_message,
                                 BusTransaction *transaction,
                                 DBusError      *error)
{
  DBusMessage *welcome;
  const char *name;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  name = bus_connection_get_name (connection);
  _dbus_assert (name != NULL);
  
  welcome = dbus_message_new_method_return (hello_message);
  if (welcome == NULL)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_append_args (welcome,
                                 DBUS_TYPE_STRING, name,
                                 DBUS_TYPE_INVALID))
    {
      dbus_message_unref (welcome);
      BUS_SET_OOM (error);
      return FALSE;
    }

  _dbus_assert (dbus_message_has_signature (welcome, "s"));
  
  if (!bus_transaction_send_from_driver (transaction, connection, welcome))
    {
      dbus_message_unref (welcome);
      BUS_SET_OOM (error);
      return FALSE;
    }
  else
    {
      dbus_message_unref (welcome);
      return TRUE;
    }
}

static dbus_bool_t
bus_driver_handle_list_services (DBusConnection *connection,
                                 BusTransaction *transaction,
                                 DBusMessage    *message,
                                 DBusError      *error)
{
  DBusMessage *reply;
  int len;
  char **services;
  BusRegistry *registry;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  registry = bus_connection_get_registry (connection);
  
  reply = dbus_message_new_method_return (message);
  if (reply == NULL)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!bus_registry_list_services (registry, &services, &len))
    {
      dbus_message_unref (reply);
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  if (!dbus_message_append_args (reply,
                                 DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, services, len,
                                 DBUS_TYPE_INVALID))
    {
      dbus_free_string_array (services);
      dbus_message_unref (reply);
      BUS_SET_OOM (error);
      return FALSE;
    }

  dbus_free_string_array (services);
  
  if (!bus_transaction_send_from_driver (transaction, connection, reply))
    {
      dbus_message_unref (reply);
      BUS_SET_OOM (error);
      return FALSE;
    }
  else
    {
      dbus_message_unref (reply);
      return TRUE;
    }
}

static dbus_bool_t
bus_driver_handle_acquire_service (DBusConnection *connection,
                                   BusTransaction *transaction,
                                   DBusMessage    *message,
                                   DBusError      *error)
{
  DBusMessage *reply;
  DBusString service_name;
  char *name;
  int service_reply;
  dbus_uint32_t flags;
  dbus_bool_t retval;
  BusRegistry *registry;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  registry = bus_connection_get_registry (connection);
  
  if (!dbus_message_get_args (message, error,
                              DBUS_TYPE_STRING, &name,
                              DBUS_TYPE_UINT32, &flags,
                              DBUS_TYPE_INVALID))
    return FALSE;
  
  _dbus_verbose ("Trying to own service %s with flags 0x%x\n", name, flags);
  
  retval = FALSE;
  reply = NULL;

  _dbus_string_init_const (&service_name, name);

  if (!bus_registry_acquire_service (registry, connection,
                                     &service_name, flags,
                                     &service_reply, transaction,
                                     error))
    goto out;
  
  reply = dbus_message_new_method_return (message);
  if (reply == NULL)
    {
      BUS_SET_OOM (error);
      goto out;
    }

  if (!dbus_message_append_args (reply, DBUS_TYPE_UINT32, service_reply, DBUS_TYPE_INVALID))
    {
      BUS_SET_OOM (error);
      goto out;
    }

  if (!bus_transaction_send_from_driver (transaction, connection, reply))
    {
      BUS_SET_OOM (error);
      goto out;
    }

  retval = TRUE;
  
 out:
  dbus_free (name);
  if (reply)
    dbus_message_unref (reply);
  return retval;
} 

static dbus_bool_t
bus_driver_handle_service_exists (DBusConnection *connection,
                                  BusTransaction *transaction,
                                  DBusMessage    *message,
                                  DBusError      *error)
{
  DBusMessage *reply;
  DBusString service_name;
  BusService *service;
  dbus_bool_t service_exists;
  char *name;
  dbus_bool_t retval;
  BusRegistry *registry;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  registry = bus_connection_get_registry (connection);
  
  if (!dbus_message_get_args (message, error,
                              DBUS_TYPE_STRING, &name,
                              DBUS_TYPE_INVALID))
    return FALSE;

  retval = FALSE;

  if (strcmp (name, DBUS_SERVICE_ORG_FREEDESKTOP_DBUS) == 0)
    {
      service_exists = TRUE;
    }
  else
    {
      _dbus_string_init_const (&service_name, name);
      service = bus_registry_lookup (registry, &service_name);
      service_exists = service != NULL;
    }
  
  reply = dbus_message_new_method_return (message);
  if (reply == NULL)
    {
      BUS_SET_OOM (error);
      goto out;
    }

  if (!dbus_message_append_args (reply,
                                 DBUS_TYPE_BOOLEAN, service_exists,
                                 0))
    {
      BUS_SET_OOM (error);
      goto out;
    }

  if (!bus_transaction_send_from_driver (transaction, connection, reply))
    {
      BUS_SET_OOM (error);
      goto out;
    }

  retval = TRUE;
  
 out:
  if (reply)
    dbus_message_unref (reply);
  dbus_free (name);

  return retval;
}

static dbus_bool_t
bus_driver_handle_activate_service (DBusConnection *connection,
                                    BusTransaction *transaction,
                                    DBusMessage    *message,
                                    DBusError      *error)
{
  dbus_uint32_t flags;
  char *name;
  dbus_bool_t retval;
  BusActivation *activation;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  activation = bus_connection_get_activation (connection);
  
  if (!dbus_message_get_args (message, error,
                              DBUS_TYPE_STRING, &name,
                              DBUS_TYPE_UINT32, &flags,
                              DBUS_TYPE_INVALID))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      _dbus_verbose ("No memory to get arguments to ActivateService\n");
      return FALSE;
    }

  retval = FALSE;

  if (!bus_activation_activate_service (activation, connection, transaction, FALSE,
                                        message, name, error))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      _dbus_verbose ("bus_activation_activate_service() failed\n");
      goto out;
    }

  retval = TRUE;
  
 out:
  dbus_free (name);
  return retval;
}

static dbus_bool_t
send_ack_reply (DBusConnection *connection,
                BusTransaction *transaction,
                DBusMessage    *message,
                DBusError      *error)
{
  DBusMessage *reply;

  reply = dbus_message_new_method_return (message);
  if (reply == NULL)
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!bus_transaction_send_from_driver (transaction, connection, reply))
    {
      BUS_SET_OOM (error);
      dbus_message_unref (reply);
      return FALSE;
    }

  dbus_message_unref (reply);
  
  return TRUE;
}

static dbus_bool_t
bus_driver_handle_add_match (DBusConnection *connection,
                             BusTransaction *transaction,
                             DBusMessage    *message,
                             DBusError      *error)
{
  BusMatchRule *rule;
  char *text;
  DBusString str;
  BusMatchmaker *matchmaker;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  text = NULL;
  rule = NULL;

  if (bus_connection_get_n_match_rules (connection) >=
      bus_context_get_max_match_rules_per_connection (bus_transaction_get_context (transaction)))
    {
      dbus_set_error (error, DBUS_ERROR_LIMITS_EXCEEDED,
                      "Connection \"%s\" is not allowed to add more match rules "
                      "(increase limits in configuration file if required)",
                      bus_connection_is_active (connection) ?
                      bus_connection_get_name (connection) :
                      "(inactive)");
      goto failed;
    }
  
  if (!dbus_message_get_args (message, error,
                              DBUS_TYPE_STRING, &text,
                              DBUS_TYPE_INVALID))
    {
      _dbus_verbose ("No memory to get arguments to AddMatch\n");
      goto failed;
    }

  _dbus_string_init_const (&str, text);

  rule = bus_match_rule_parse (connection, &str, error);
  if (rule == NULL)
    goto failed;

  matchmaker = bus_connection_get_matchmaker (connection);

  if (!bus_matchmaker_add_rule (matchmaker, rule))
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  if (!send_ack_reply (connection, transaction,
                       message, error))
    {
      bus_matchmaker_remove_rule (matchmaker, rule);
      goto failed;
    }
  
  bus_match_rule_unref (rule);
  dbus_free (text);
  
  return TRUE;

 failed:
  _DBUS_ASSERT_ERROR_IS_SET (error);
  if (rule)
    bus_match_rule_unref (rule);
  if (text)
    dbus_free (text);
  return FALSE;
}

static dbus_bool_t
bus_driver_handle_remove_match (DBusConnection *connection,
                                BusTransaction *transaction,
                                DBusMessage    *message,
                                DBusError      *error)
{
  BusMatchRule *rule;
  char *text;
  DBusString str;
  BusMatchmaker *matchmaker;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  text = NULL;
  rule = NULL;
  
  if (!dbus_message_get_args (message, error,
                              DBUS_TYPE_STRING, &text,
                              DBUS_TYPE_INVALID))
    {
      _dbus_verbose ("No memory to get arguments to RemoveMatch\n");
      goto failed;
    }

  _dbus_string_init_const (&str, text);

  rule = bus_match_rule_parse (connection, &str, error);
  if (rule == NULL)
    goto failed;

  /* Send the ack before we remove the rule, since the ack is undone
   * on transaction cancel, but rule removal isn't.
   */
  if (!send_ack_reply (connection, transaction,
                       message, error))
    goto failed;
  
  matchmaker = bus_connection_get_matchmaker (connection);

  if (!bus_matchmaker_remove_rule_by_value (matchmaker, rule, error))
    goto failed;

  bus_match_rule_unref (rule);
  dbus_free (text);
  
  return TRUE;

 failed:
  _DBUS_ASSERT_ERROR_IS_SET (error);
  if (rule)
    bus_match_rule_unref (rule);
  if (text)
    dbus_free (text);
  return FALSE;
}

static dbus_bool_t
bus_driver_handle_get_service_owner (DBusConnection *connection,
				     BusTransaction *transaction,
				     DBusMessage    *message,
				     DBusError      *error)
{
  char *text;
  const char *base_name;
  DBusString str;
  BusRegistry *registry;
  BusService *service;
  DBusMessage *reply;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  registry = bus_connection_get_registry (connection);

  text = NULL;
  reply = NULL;

  if (! dbus_message_get_args (message, error,
			       DBUS_TYPE_STRING, &text,
			       DBUS_TYPE_INVALID))
      goto failed;

  _dbus_string_init_const (&str, text);
  service = bus_registry_lookup (registry, &str);
  if (service == NULL)
    {
      dbus_set_error (error, 
		      DBUS_ERROR_SERVICE_HAS_NO_OWNER,
		      "Could not get owner of service '%s': no such service", text);
      goto failed;
    }

  base_name = bus_connection_get_name (bus_service_get_primary_owner (service));
  if (base_name == NULL)
    {
      dbus_set_error (error,
		      DBUS_ERROR_FAILED,
		      "Could not determine base service for '%s'", text);
      goto failed;
    }
  _dbus_assert (*base_name == ':');

  reply = dbus_message_new_method_return (message);
  if (reply == NULL)
    goto oom;

  if (! dbus_message_append_args (reply, 
				  DBUS_TYPE_STRING, base_name,
				  DBUS_TYPE_INVALID))
    goto oom;
  
  if (! bus_transaction_send_from_driver (transaction, connection, reply))
    goto oom;

  dbus_message_unref (reply);
  dbus_free (text);

  return TRUE;

 oom:
  BUS_SET_OOM (error);

 failed:
  _DBUS_ASSERT_ERROR_IS_SET (error);
  if (reply)
    dbus_message_unref (reply);
  dbus_free (text);
  return FALSE;
}

static dbus_bool_t
bus_driver_handle_get_connection_unix_user (DBusConnection *connection,
                                            BusTransaction *transaction,
                                            DBusMessage    *message,
                                            DBusError      *error)
{
  char *service;
  DBusString str;
  BusRegistry *registry;
  BusService *serv;
  DBusConnection *conn;
  DBusMessage *reply;
  unsigned long uid;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  registry = bus_connection_get_registry (connection);

  service = NULL;
  reply = NULL;

  if (! dbus_message_get_args (message, error,
			       DBUS_TYPE_STRING, &service,
			       DBUS_TYPE_INVALID))
      goto failed;

  _dbus_verbose ("asked for UID of connection %s\n", service);

  _dbus_string_init_const (&str, service);
  serv = bus_registry_lookup (registry, &str);
  if (serv == NULL)
    {
      dbus_set_error (error, 
		      DBUS_ERROR_SERVICE_HAS_NO_OWNER,
		      "Could not get owner of service '%s': no such service", service);
      goto failed;
    }

  conn = bus_service_get_primary_owner (serv);

  reply = dbus_message_new_method_return (message);
  if (reply == NULL)
    goto oom;

  if (!dbus_connection_get_unix_user (conn, &uid))
    {
      dbus_set_error (error,
                      DBUS_ERROR_FAILED,
                      "Could not determine UID for '%s'", service);
      goto failed;
    }

  if (! dbus_message_append_args (reply,
                                  DBUS_TYPE_UINT32, (dbus_uint32_t) uid,
                                  DBUS_TYPE_INVALID))
    goto oom;

  if (! bus_transaction_send_from_driver (transaction, connection, reply))
    goto oom;

  dbus_message_unref (reply);
  dbus_free (service);

  return TRUE;

 oom:
  BUS_SET_OOM (error);

 failed:
  _DBUS_ASSERT_ERROR_IS_SET (error);
  if (reply)
    dbus_message_unref (reply);
  dbus_free (service);
  return FALSE;
}

static dbus_bool_t
bus_driver_handle_get_connection_unix_process_id (DBusConnection *connection,
						  BusTransaction *transaction,
						  DBusMessage    *message,
						  DBusError      *error)
{
  char *service;
  DBusString str;
  BusRegistry *registry;
  BusService *serv;
  DBusConnection *conn;
  DBusMessage *reply;
  unsigned long pid;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  registry = bus_connection_get_registry (connection);

  service = NULL;
  reply = NULL;

  if (! dbus_message_get_args (message, error,
			       DBUS_TYPE_STRING, &service,
			       DBUS_TYPE_INVALID))
      goto failed;

  _dbus_verbose ("asked for PID of connection %s\n", service);

  _dbus_string_init_const (&str, service);
  serv = bus_registry_lookup (registry, &str);
  if (serv == NULL)
    {
      dbus_set_error (error, 
		      DBUS_ERROR_SERVICE_HAS_NO_OWNER,
		      "Could not get owner of service '%s': no such service", service);
      goto failed;
    }

  conn = bus_service_get_primary_owner (serv);

  reply = dbus_message_new_method_return (message);
  if (reply == NULL)
    goto oom;

  if (!dbus_connection_get_unix_process_id (conn, &pid))
    {
      dbus_set_error (error,
                      DBUS_ERROR_UNIX_PROCESS_ID_UNKNOWN,
                      "Could not determine PID for '%s'", service);
      goto failed;
    }

  if (! dbus_message_append_args (reply,
                                  DBUS_TYPE_UINT32, (dbus_uint32_t) pid,
                                  DBUS_TYPE_INVALID))
    goto oom;

  if (! bus_transaction_send_from_driver (transaction, connection, reply))
    goto oom;

  dbus_message_unref (reply);
  dbus_free (service);

  return TRUE;

 oom:
  BUS_SET_OOM (error);

 failed:
  _DBUS_ASSERT_ERROR_IS_SET (error);
  if (reply)
    dbus_message_unref (reply);
  dbus_free (service);
  return FALSE;
}

static dbus_bool_t
bus_driver_handle_reload_config (DBusConnection *connection,
				 BusTransaction *transaction,
				 DBusMessage    *message,
				 DBusError      *error)
{
  BusContext *context;
  dbus_bool_t retval;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  retval = FALSE;

  context = bus_connection_get_context (connection);
  if (!bus_context_reload_config (context, error))
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto out;
    }

  retval = TRUE;
  
 out:
  return retval;
}

/* For speed it might be useful to sort this in order of
 * frequency of use (but doesn't matter with only a few items
 * anyhow)
 */
struct
{
  const char *name;
  dbus_bool_t (* handler) (DBusConnection *connection,
                           BusTransaction *transaction,
                           DBusMessage    *message,
                           DBusError      *error);
} message_handlers[] = {
  { "AcquireService", bus_driver_handle_acquire_service },
  { "ActivateService", bus_driver_handle_activate_service },
  { "Hello", bus_driver_handle_hello },
  { "ServiceExists", bus_driver_handle_service_exists },
  { "ListServices", bus_driver_handle_list_services },
  { "AddMatch", bus_driver_handle_add_match },
  { "RemoveMatch", bus_driver_handle_remove_match },
  { "GetServiceOwner", bus_driver_handle_get_service_owner },
  { "GetConnectionUnixUser", bus_driver_handle_get_connection_unix_user },
  { "GetConnectionUnixProcessID", bus_driver_handle_get_connection_unix_process_id },
  { "ReloadConfig", bus_driver_handle_reload_config }
};

dbus_bool_t
bus_driver_handle_message (DBusConnection *connection,
                           BusTransaction *transaction,
			   DBusMessage    *message,
                           DBusError      *error)
{
  const char *name, *sender;
  int i;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (dbus_message_get_type (message) != DBUS_MESSAGE_TYPE_METHOD_CALL)
    {
      _dbus_verbose ("Driver got a non-method-call message, ignoring\n");
      return TRUE; /* we just ignore this */
    }

  _dbus_assert (dbus_message_get_interface (message) != NULL);
  _dbus_assert (dbus_message_get_member (message) != NULL);

  name = dbus_message_get_member (message);
  sender = dbus_message_get_sender (message);
  
  if (strcmp (dbus_message_get_interface (message),
              DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS) != 0)
    {
      _dbus_verbose ("Driver got message to unknown interface \"%s\"\n",
                     dbus_message_get_interface (message));
      goto unknown;
    }
  
  _dbus_verbose ("Driver got a method call: %s\n",
		 dbus_message_get_member (message));
  
  /* security checks should have kept this from getting here */
  _dbus_assert (sender != NULL || strcmp (name, "Hello") == 0);

  if (dbus_message_get_reply_serial (message) != 0)
    {
      _dbus_verbose ("Client sent a reply to the bus driver, ignoring it\n");
      return TRUE;
    }
  
  i = 0;
  while (i < _DBUS_N_ELEMENTS (message_handlers))
    {
      if (strcmp (message_handlers[i].name, name) == 0)
        {
          _dbus_verbose ("Running driver handler for %s\n", name);
          if ((* message_handlers[i].handler) (connection, transaction, message, error))
            {
              _DBUS_ASSERT_ERROR_IS_CLEAR (error);
              _dbus_verbose ("Driver handler succeeded\n");
              return TRUE;
            }
          else
            {
              _DBUS_ASSERT_ERROR_IS_SET (error);
              _dbus_verbose ("Driver handler returned failure\n");
              return FALSE;
            }
        }
      
      ++i;
    }

 unknown:
  _dbus_verbose ("No driver handler for message \"%s\"\n",
                 name);

  dbus_set_error (error, DBUS_ERROR_UNKNOWN_METHOD,
                  "%s does not understand message %s",
                  DBUS_SERVICE_ORG_FREEDESKTOP_DBUS, name);
  
  return FALSE;
}

void
bus_driver_remove_connection (DBusConnection *connection)
{
  /* FIXME Does nothing for now, should unregister the connection
   * with the bus driver.
   */
}
