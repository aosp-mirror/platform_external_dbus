/* -*- mode: C; c-file-style: "gnu" -*- */
/* bus.c  message bus context object
 *
 * Copyright (C) 2003 Red Hat, Inc.
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

#include "bus.h"
#include "loop.h"
#include "activation.h"
#include "connection.h"
#include "services.h"
#include "utils.h"
#include "policy.h"
#include "config-parser.h"
#include <dbus/dbus-list.h>
#include <dbus/dbus-hash.h>
#include <dbus/dbus-internals.h>

struct BusContext
{
  int refcount;
  char *address;
  DBusList *servers;
  BusConnections *connections;
  BusActivation *activation;
  BusRegistry *registry;
  DBusList *default_rules;      /**< Default policy rules */
  DBusList *mandatory_rules;    /**< Mandatory policy rules */
  DBusHashTable *rules_by_uid;  /**< per-UID policy rules */
  DBusHashTable *rules_by_gid;  /**< per-GID policy rules */
};

static dbus_bool_t
server_watch_callback (DBusWatch     *watch,
                       unsigned int   condition,
                       void          *data)
{
  DBusServer *server = data;

  return dbus_server_handle_watch (server, watch, condition);
}

static dbus_bool_t
add_server_watch (DBusWatch  *watch,
                  void       *data)
{
  return bus_loop_add_watch (watch, server_watch_callback, data,
                             NULL);
}

static void
remove_server_watch (DBusWatch  *watch,
                     void       *data)
{
  bus_loop_remove_watch (watch, server_watch_callback, data);
}


static void
server_timeout_callback (DBusTimeout   *timeout,
                         void          *data)
{
  /* can return FALSE on OOM but we just let it fire again later */
  dbus_timeout_handle (timeout);
}

static dbus_bool_t
add_server_timeout (DBusTimeout *timeout,
                    void        *data)
{
  return bus_loop_add_timeout (timeout, server_timeout_callback, data, NULL);
}

static void
remove_server_timeout (DBusTimeout *timeout,
                       void        *data)
{
  bus_loop_remove_timeout (timeout, server_timeout_callback, data);
}

static void
new_connection_callback (DBusServer     *server,
                         DBusConnection *new_connection,
                         void           *data)
{
  BusContext *context = data;
  
  if (!bus_connections_setup_connection (context->connections, new_connection))
    {
      _dbus_verbose ("No memory to setup new connection\n");

      /* if we don't do this, it will get unref'd without
       * being disconnected... kind of strange really
       * that we have to do this, people won't get it right
       * in general.
       */
      dbus_connection_disconnect (new_connection);
    }
  
  /* on OOM, we won't have ref'd the connection so it will die. */
}

static void
free_rule_func (void *data,
                void *user_data)
{
  BusPolicyRule *rule = data;

  bus_policy_rule_unref (rule);
}

static void
free_rule_list_func (void *data)
{
  DBusList **list = data;

  _dbus_list_foreach (list, free_rule_func, NULL);
  
  _dbus_list_clear (list);

  dbus_free (list);
}

static dbus_bool_t
setup_server (BusContext *context,
              DBusServer *server,
              char      **auth_mechanisms,
              DBusError  *error)
{
  if (!dbus_server_set_auth_mechanisms (server, (const char**) auth_mechanisms))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }
  
  dbus_server_set_new_connection_function (server,
                                           new_connection_callback,
                                           context, NULL);
  
  if (!dbus_server_set_watch_functions (server,
                                        add_server_watch,
                                        remove_server_watch,
                                        NULL,
                                        server,
                                        NULL))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  if (!dbus_server_set_timeout_functions (server,
                                          add_server_timeout,
                                          remove_server_timeout,
                                          NULL,
                                          server, NULL))
    {
      BUS_SET_OOM (error);
      return FALSE;
    }

  return TRUE;
}

BusContext*
bus_context_new (const DBusString *config_file,
                 DBusError        *error)
{
  BusContext *context;
  DBusList *link;
  DBusList **addresses;
  BusConfigParser *parser;
  DBusString full_address;
  const char *user;
  char **auth_mechanisms;
  DBusList **auth_mechanisms_list;
  int len;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (!_dbus_string_init (&full_address))
    return NULL;

  parser = NULL;
  context = NULL;
  auth_mechanisms = NULL;
  
  parser = bus_config_load (config_file, error);
  if (parser == NULL)
    goto failed;
  
  context = dbus_new0 (BusContext, 1);
  if (context == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }
  
  context->refcount = 1;

  /* Build an array of auth mechanisms */
  
  auth_mechanisms_list = bus_config_parser_get_mechanisms (parser);
  len = _dbus_list_get_length (auth_mechanisms_list);

  if (len > 0)
    {
      int i;

      auth_mechanisms = dbus_new0 (char*, len + 1);
      if (auth_mechanisms == NULL)
        goto failed;
      
      i = 0;
      link = _dbus_list_get_first_link (auth_mechanisms_list);
      while (link != NULL)
        {
          auth_mechanisms[i] = _dbus_strdup (link->data);
          if (auth_mechanisms[i] == NULL)
            goto failed;
          link = _dbus_list_get_next_link (auth_mechanisms_list, link);
        }
    }
  else
    {
      auth_mechanisms = NULL;
    }

  /* Listen on our addresses */
  
  addresses = bus_config_parser_get_addresses (parser);  
  
  link = _dbus_list_get_first_link (addresses);
  while (link != NULL)
    {
      DBusServer *server;
      
      server = dbus_server_listen (link->data, error);
      if (server == NULL)
        goto failed;
      else if (!setup_server (context, server, auth_mechanisms, error))
        goto failed;

      if (!_dbus_list_append (&context->servers, server))
        {
          BUS_SET_OOM (error);
          goto failed;
        }          
      
      link = _dbus_list_get_next_link (addresses, link);
    }

  /* Here we change our credentials if required,
   * as soon as we've set up our sockets
   */
  user = bus_config_parser_get_user (parser);
  if (user != NULL)
    {
      DBusCredentials creds;
      DBusString u;

      _dbus_string_init_const (&u, user);

      if (!_dbus_credentials_from_username (&u, &creds) ||
          creds.uid < 0 ||
          creds.gid < 0)
        {
          dbus_set_error (error, DBUS_ERROR_FAILED,
                          "Could not get UID and GID for username \"%s\"",
                          user);
          goto failed;
        }
      
      if (!_dbus_change_identity (creds.uid, creds.gid, error))
        goto failed;
    }
  
  /* We have to build the address backward, so that
   * <listen> later in the config file have priority
   */
  link = _dbus_list_get_last_link (&context->servers);
  while (link != NULL)
    {
      char *addr;
      
      addr = dbus_server_get_address (link->data);
      if (addr == NULL)
        {
          BUS_SET_OOM (error);
          goto failed;
        }

      if (_dbus_string_get_length (&full_address) > 0)
        {
          if (!_dbus_string_append (&full_address, ";"))
            {
              BUS_SET_OOM (error);
              goto failed;
            }
        }

      if (!_dbus_string_append (&full_address, addr))
        {
          BUS_SET_OOM (error);
          goto failed;
        }

      dbus_free (addr);

      link = _dbus_list_get_prev_link (&context->servers, link);
    }

  if (!_dbus_string_copy_data (&full_address, &context->address))
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  /* Create activation subsystem */
  
  context->activation = bus_activation_new (context, &full_address,
                                            bus_config_parser_get_service_dirs (parser),
                                            error);
  if (context->activation == NULL)
    {
      _DBUS_ASSERT_ERROR_IS_SET (error);
      goto failed;
    }

  context->connections = bus_connections_new (context);
  if (context->connections == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  context->registry = bus_registry_new (context);
  if (context->registry == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }
  
  context->rules_by_uid = _dbus_hash_table_new (DBUS_HASH_ULONG,
                                                NULL,
                                                free_rule_list_func);
  if (context->rules_by_uid == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  context->rules_by_gid = _dbus_hash_table_new (DBUS_HASH_ULONG,
                                                NULL,
                                                free_rule_list_func);
  if (context->rules_by_gid == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }

  /* Now become a daemon if appropriate */
  if (bus_config_parser_get_fork (parser))
    {
      if (!_dbus_become_daemon (error))
        goto failed;
    }
  
  bus_config_parser_unref (parser);
  _dbus_string_free (&full_address);
  dbus_free_string_array (auth_mechanisms);
  
  return context;
  
 failed:  
  if (parser != NULL)
    bus_config_parser_unref (parser);

  if (context != NULL)
    bus_context_unref (context);

  _dbus_string_free (&full_address);
  dbus_free_string_array (auth_mechanisms);
  return NULL;
}

static void
shutdown_server (BusContext *context,
                 DBusServer *server)
{
  if (server == NULL ||
      !dbus_server_get_is_connected (server))
    return;
  
  if (!dbus_server_set_watch_functions (server,
                                        NULL, NULL, NULL,
                                        context,
                                        NULL))
    _dbus_assert_not_reached ("setting watch functions to NULL failed");
  
  if (!dbus_server_set_timeout_functions (server,
                                          NULL, NULL, NULL,
                                          context,
                                          NULL))
    _dbus_assert_not_reached ("setting timeout functions to NULL failed");
  
  dbus_server_disconnect (server);
}

void
bus_context_shutdown (BusContext  *context)
{
  DBusList *link;

  link = _dbus_list_get_first_link (&context->servers);
  while (link != NULL)
    {
      shutdown_server (context, link->data);

      link = _dbus_list_get_next_link (&context->servers, link);
    }
}

void
bus_context_ref (BusContext *context)
{
  _dbus_assert (context->refcount > 0);
  context->refcount += 1;
}

void
bus_context_unref (BusContext *context)
{
  _dbus_assert (context->refcount > 0);
  context->refcount -= 1;

  if (context->refcount == 0)
    {
      DBusList *link;
      
      _dbus_verbose ("Finalizing bus context %p\n", context);
      
      bus_context_shutdown (context);

      if (context->connections)
        {
          bus_connections_unref (context->connections);
          context->connections = NULL;
        }
      
      if (context->registry)
        {
          bus_registry_unref (context->registry);
          context->registry = NULL;
        }
      
      if (context->activation)
        {
          bus_activation_unref (context->activation);
          context->activation = NULL;
        }

      link = _dbus_list_get_first_link (&context->servers);
      while (link != NULL)
        {
          dbus_server_unref (link->data);
          
          link = _dbus_list_get_next_link (&context->servers, link);
        }
      _dbus_list_clear (&context->servers);

      if (context->rules_by_uid)
        {
          _dbus_hash_table_unref (context->rules_by_uid);
          context->rules_by_uid = NULL;
        }

      if (context->rules_by_gid)
        {
          _dbus_hash_table_unref (context->rules_by_gid);
          context->rules_by_gid = NULL;
        }
      
      dbus_free (context->address);
      dbus_free (context);
    }
}

BusRegistry*
bus_context_get_registry (BusContext  *context)
{
  return context->registry;
}

BusConnections*
bus_context_get_connections (BusContext  *context)
{
  return context->connections;
}

BusActivation*
bus_context_get_activation (BusContext  *context)
{
  return context->activation;
}

static dbus_bool_t
list_allows_user (dbus_bool_t           def,
                  DBusList            **list,
                  unsigned long         uid,
                  const unsigned long  *group_ids,
                  int                   n_group_ids)
{
  DBusList *link;
  dbus_bool_t allowed;
  
  allowed = def;

  link = _dbus_list_get_first_link (list);
  while (link != NULL)
    {
      BusPolicyRule *rule = link->data;
      link = _dbus_list_get_next_link (list, link);
      
      if (rule->type == BUS_POLICY_RULE_USER)
        {
          if (rule->d.user.uid != uid)
            continue;
        }
      else if (rule->type == BUS_POLICY_RULE_GROUP)
        {
          int i;

          i = 0;
          while (i < n_group_ids)
            {
              if (rule->d.group.gid == group_ids[i])
                break;
              ++i;
            }

          if (i == n_group_ids)
            continue;
        }
      else
        continue;

      allowed = rule->allow;
    }
  
  return allowed;
}

dbus_bool_t
bus_context_allow_user (BusContext   *context,
                        unsigned long uid)
{
  dbus_bool_t allowed;
  unsigned long *group_ids;
  int n_group_ids;

  /* On OOM or error we always reject the user */
  if (!_dbus_get_groups (uid, &group_ids, &n_group_ids))
    {
      _dbus_verbose ("Did not get any groups for UID %lu\n",
                     uid);
      return FALSE;
    }
  
  allowed = FALSE;

  allowed = list_allows_user (allowed,
                              &context->default_rules,
                              uid,
                              group_ids, n_group_ids);

  allowed = list_allows_user (allowed,
                              &context->mandatory_rules,
                              uid,
                              group_ids, n_group_ids);

  dbus_free (group_ids);

  return allowed;
}

static dbus_bool_t
add_list_to_policy (DBusList       **list,
                    BusPolicy       *policy)
{
  DBusList *link;

  link = _dbus_list_get_first_link (list);
  while (link != NULL)
    {
      BusPolicyRule *rule = link->data;
      link = _dbus_list_get_next_link (list, link);

      switch (rule->type)
        {
        case BUS_POLICY_RULE_USER:
        case BUS_POLICY_RULE_GROUP:
          /* These aren't per-connection policies */
          break;

        case BUS_POLICY_RULE_OWN:
        case BUS_POLICY_RULE_SEND:
        case BUS_POLICY_RULE_RECEIVE:
          /* These are per-connection */
          if (!bus_policy_append_rule (policy, rule))
            return FALSE;
          break;
        }
    }
  
  return TRUE;
}

BusPolicy*
bus_context_create_connection_policy (BusContext      *context,
                                      DBusConnection  *connection)
{
  BusPolicy *policy;
  unsigned long uid;
  DBusList **list;

  _dbus_assert (dbus_connection_get_is_authenticated (connection));
  
  policy = bus_policy_new ();
  if (policy == NULL)
    return NULL;

  if (!add_list_to_policy (&context->default_rules,
                                      policy))
    goto failed;

  /* we avoid the overhead of looking up user's groups
   * if we don't have any group rules anyway
   */
  if (_dbus_hash_table_get_n_entries (context->rules_by_gid) > 0)
    {
      const unsigned long *groups;
      int n_groups;
      int i;
      
      if (!bus_connection_get_groups (connection, &groups, &n_groups))
        goto failed;
      
      i = 0;
      while (i < n_groups)
        {
          list = _dbus_hash_table_lookup_ulong (context->rules_by_gid,
                                                groups[i]);
          
          if (list != NULL)
            {
              if (!add_list_to_policy (list, policy))
                goto failed;
            }
          
          ++i;
        }
    }

  if (!dbus_connection_get_unix_user (connection, &uid))
    goto failed;

  list = _dbus_hash_table_lookup_ulong (context->rules_by_uid,
                                        uid);

  if (!add_list_to_policy (list, policy))
    goto failed;
  
  if (!add_list_to_policy (&context->mandatory_rules,
                           policy))
    goto failed;

  bus_policy_optimize (policy);
  
  return policy;
  
 failed:
  bus_policy_unref (policy);
  return NULL;
}
