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
  char *type;
  char *address;
  char *pidfile;
  DBusLoop *loop;
  DBusList *servers;
  BusConnections *connections;
  BusActivation *activation;
  BusRegistry *registry;
  DBusList *default_rules;       /**< Default policy rules */
  DBusList *mandatory_rules;     /**< Mandatory policy rules */
  DBusHashTable *rules_by_uid;   /**< per-UID policy rules */
  DBusHashTable *rules_by_gid;   /**< per-GID policy rules */
  int activation_timeout;        /**< How long to wait for an activation to time out */
  int auth_timeout;              /**< How long to wait for an authentication to time out */
  int max_completed_connections;    /**< Max number of authorized connections */
  int max_incomplete_connections;   /**< Max number of incomplete connections */
  int max_connections_per_user;     /**< Max number of connections auth'd as same user */
};

static int server_data_slot = -1;
static int server_data_slot_refcount = 0;

typedef struct
{
  BusContext *context;
} BusServerData;

#define BUS_SERVER_DATA(server) (dbus_server_get_data ((server), server_data_slot))

static dbus_bool_t
server_data_slot_ref (void)
{
  if (server_data_slot < 0)
    {
      server_data_slot = dbus_server_allocate_data_slot ();
      
      if (server_data_slot < 0)
        return FALSE;

      _dbus_assert (server_data_slot_refcount == 0);
    }  

  server_data_slot_refcount += 1;

  return TRUE;
}

static void
server_data_slot_unref (void)
{
  _dbus_assert (server_data_slot_refcount > 0);

  server_data_slot_refcount -= 1;
  
  if (server_data_slot_refcount == 0)
    {
      dbus_server_free_data_slot (server_data_slot);
      server_data_slot = -1;
    }
}

static BusContext*
server_get_context (DBusServer *server)
{
  BusContext *context;
  BusServerData *bd;
  
  if (!server_data_slot_ref ())
    return NULL;

  bd = BUS_SERVER_DATA (server);
  if (bd == NULL)
    {
      server_data_slot_unref ();
      return NULL;
    }

  context = bd->context;

  server_data_slot_unref ();

  return context;
}

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
  DBusServer *server = data;
  BusContext *context;
  
  context = server_get_context (server);
  
  return _dbus_loop_add_watch (context->loop,
                               watch, server_watch_callback, server,
                               NULL);
}

static void
remove_server_watch (DBusWatch  *watch,
                     void       *data)
{
  DBusServer *server = data;
  BusContext *context;
  
  context = server_get_context (server);
  
  _dbus_loop_remove_watch (context->loop,
                           watch, server_watch_callback, server);
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
  DBusServer *server = data;
  BusContext *context;
  
  context = server_get_context (server);

  return _dbus_loop_add_timeout (context->loop,
                                 timeout, server_timeout_callback, server, NULL);
}

static void
remove_server_timeout (DBusTimeout *timeout,
                       void        *data)
{
  DBusServer *server = data;
  BusContext *context;
  
  context = server_get_context (server);
  
  _dbus_loop_remove_timeout (context->loop,
                             timeout, server_timeout_callback, server);
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

static void
free_server_data (void *data)
{
  BusServerData *bd = data;  
  
  dbus_free (bd);
}

static dbus_bool_t
setup_server (BusContext *context,
              DBusServer *server,
              char      **auth_mechanisms,
              DBusError  *error)
{
  BusServerData *bd;

  bd = dbus_new0 (BusServerData, 1);
  if (!dbus_server_set_data (server,
                             server_data_slot,
                             bd, free_server_data))
    {
      dbus_free (bd);
      BUS_SET_OOM (error);
      return FALSE;
    }

  bd->context = context;
  
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
                 int               print_addr_fd,
                 DBusError        *error)
{
  BusContext *context;
  DBusList *link;
  DBusList **addresses;
  BusConfigParser *parser;
  DBusString full_address;
  const char *user, *pidfile;
  char **auth_mechanisms;
  DBusList **auth_mechanisms_list;
  int len;
  
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);

  if (!_dbus_string_init (&full_address))
    {
      BUS_SET_OOM (error);
      return NULL;
    }

  if (!server_data_slot_ref ())
    {
      BUS_SET_OOM (error);
      _dbus_string_free (&full_address);
      return NULL;
    }
  
  parser = NULL;
  context = NULL;
  auth_mechanisms = NULL;
  
  parser = bus_config_load (config_file, error);
  if (parser == NULL)
    goto failed;

  /* Check for an existing pid file. Of course this is a race;
   * we'd have to use fcntl() locks on the pid file to
   * avoid that. But we want to check for the pid file
   * before overwriting any existing sockets, etc.
   */
  pidfile = bus_config_parser_get_pidfile (parser);
  if (pidfile != NULL)
    {
      DBusString u;
      DBusStat stbuf;
      DBusError tmp_error;
      
      dbus_error_init (&tmp_error);
      _dbus_string_init_const (&u, pidfile);
      
      if (_dbus_stat (&u, &stbuf, &tmp_error))
	{
	  dbus_set_error (error, DBUS_ERROR_FAILED,
			  "The pid file \"%s\" exists, if the message bus is not running, remove this file",
			  pidfile);
	  dbus_error_free (&tmp_error);
	  goto failed;
	}
    }
  
  context = dbus_new0 (BusContext, 1);
  if (context == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }
  
  context->refcount = 1;

  /* we need another ref of the server data slot for the context
   * to own
   */
  if (!server_data_slot_ref ())
    _dbus_assert_not_reached ("second ref of server data slot failed");
  
#ifdef DBUS_BUILD_TESTS
  context->activation_timeout = 6000;  /* 6 seconds */
#else
  context->activation_timeout = 15000; /* 15 seconds */
#endif

  /* Making this long risks making a DOS attack easier, but too short
   * and legitimate auth will fail.  If interactive auth (ask user for
   * password) is allowed, then potentially it has to be quite long.
   * Ultimately it needs to come from the configuration file.
   */     
  context->auth_timeout = 3000; /* 3 seconds */

  context->max_incomplete_connections = 32;
  context->max_connections_per_user = 128;

  /* Note that max_completed_connections / max_connections_per_user
   * is the number of users that would have to work together to
   * DOS all the other users.
   */
  context->max_completed_connections = 1024;
  
  context->loop = _dbus_loop_new ();
  if (context->loop == NULL)
    {
      BUS_SET_OOM (error);
      goto failed;
    }
  
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

  /* note that type may be NULL */
  context->type = _dbus_strdup (bus_config_parser_get_type (parser));
  
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

  /* Note that we don't know whether the print_addr_fd is
   * one of the sockets we're using to listen on, or some
   * other random thing. But I think the answer is "don't do
   * that then"
   */
  if (print_addr_fd >= 0)
    {
      DBusString addr;
      const char *a = bus_context_get_address (context);
      int bytes;
      
      _dbus_assert (a != NULL);
      if (!_dbus_string_init (&addr))
        {
          BUS_SET_OOM (error);
          goto failed;
        }
      
      if (!_dbus_string_append (&addr, a) ||
          !_dbus_string_append (&addr, "\n"))
        {
          _dbus_string_free (&addr);
          BUS_SET_OOM (error);
          goto failed;
        }

      bytes = _dbus_string_get_length (&addr);
      if (_dbus_write (print_addr_fd, &addr, 0, bytes) != bytes)
        {
          dbus_set_error (error, DBUS_ERROR_FAILED,
                          "Printing message bus address: %s\n",
                          _dbus_strerror (errno));
          _dbus_string_free (&addr);
          goto failed;
        }

      if (print_addr_fd > 2)
        _dbus_close (print_addr_fd, NULL);

      _dbus_string_free (&addr);
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
      DBusString u;

      if (pidfile)
        _dbus_string_init_const (&u, pidfile);
      
      if (!_dbus_become_daemon (pidfile ? &u : NULL, error))
        goto failed;
    }
  else
    {
      /* Need to write PID file for ourselves, not for the child process */
      if (pidfile != NULL)
        {
          DBusString u;

          _dbus_string_init_const (&u, pidfile);
          
          if (!_dbus_write_pid_file (&u, _dbus_getpid (), error))
            goto failed;
        }
    }

  /* keep around the pid filename so we can delete it later */
  context->pidfile = _dbus_strdup (pidfile);
  
  bus_config_parser_unref (parser);
  _dbus_string_free (&full_address);
  dbus_free_string_array (auth_mechanisms);
  server_data_slot_unref ();
  
  return context;
  
 failed:  
  if (parser != NULL)
    bus_config_parser_unref (parser);

  if (context != NULL)
    bus_context_unref (context);

  _dbus_string_free (&full_address);
  dbus_free_string_array (auth_mechanisms);

  server_data_slot_unref ();
  
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

      if (context->loop)
        {
          _dbus_loop_unref (context->loop);
          context->loop = NULL;
        }
      
      dbus_free (context->type);
      dbus_free (context->address);

      if (context->pidfile)
	{
          DBusString u;
          _dbus_string_init_const (&u, context->pidfile);

          /* Deliberately ignore errors here, since there's not much
	   * we can do about it, and we're exiting anyways.
	   */
	  _dbus_delete_file (&u, NULL);

          dbus_free (context->pidfile); 
	}

      dbus_free (context);

      server_data_slot_unref ();
    }
}

/* type may be NULL */
const char*
bus_context_get_type (BusContext *context)
{
  return context->type;
}

const char*
bus_context_get_address (BusContext *context)
{
  return context->address;
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

DBusLoop*
bus_context_get_loop (BusContext *context)
{
  return context->loop;
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

int
bus_context_get_activation_timeout (BusContext *context)
{
  
  return context->activation_timeout;
}
