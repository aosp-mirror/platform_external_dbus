/* -*- mode: C; c-file-style: "gnu" -*- */
/* policy.c  Bus security policy
 *
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

#include "policy.h"
#include "services.h"
#include "test.h"
#include <dbus/dbus-list.h>
#include <dbus/dbus-hash.h>
#include <dbus/dbus-internals.h>

BusPolicyRule*
bus_policy_rule_new (BusPolicyRuleType type,
                     dbus_bool_t       allow)
{
  BusPolicyRule *rule;

  rule = dbus_new0 (BusPolicyRule, 1);
  if (rule == NULL)
    return NULL;

  rule->type = type;
  rule->refcount = 1;
  rule->allow = allow;

  switch (rule->type)
    {
    case BUS_POLICY_RULE_USER:
      rule->d.user.uid = DBUS_UID_UNSET;
      break;
    case BUS_POLICY_RULE_GROUP:
      rule->d.group.gid = DBUS_GID_UNSET;
      break;
    case BUS_POLICY_RULE_SEND:
    case BUS_POLICY_RULE_RECEIVE:
    case BUS_POLICY_RULE_OWN:
      break;
    }
  
  return rule;
}

void
bus_policy_rule_ref (BusPolicyRule *rule)
{
  _dbus_assert (rule->refcount > 0);

  rule->refcount += 1;
}

void
bus_policy_rule_unref (BusPolicyRule *rule)
{
  _dbus_assert (rule->refcount > 0);

  rule->refcount -= 1;

  if (rule->refcount == 0)
    {
      switch (rule->type)
        {
        case BUS_POLICY_RULE_SEND:
          dbus_free (rule->d.send.message_name);
          dbus_free (rule->d.send.destination);
          break;
        case BUS_POLICY_RULE_RECEIVE:
          dbus_free (rule->d.receive.message_name);
          dbus_free (rule->d.receive.origin);
          break;
        case BUS_POLICY_RULE_OWN:
          dbus_free (rule->d.own.service_name);
          break;
        case BUS_POLICY_RULE_USER:
          dbus_free (rule->d.user.user);
          break;
        case BUS_POLICY_RULE_GROUP:
          dbus_free (rule->d.group.group);
          break;
        }
      
      dbus_free (rule);
    }
}

struct BusPolicy
{
  int refcount;

  DBusList *default_rules;       /**< Default policy rules */
  DBusList *mandatory_rules;     /**< Mandatory policy rules */
  DBusHashTable *rules_by_uid;   /**< per-UID policy rules */
  DBusHashTable *rules_by_gid;   /**< per-GID policy rules */
};

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

BusPolicy*
bus_policy_new (void)
{
  BusPolicy *policy;

  policy = dbus_new0 (BusPolicy, 1);
  if (policy == NULL)
    return NULL;

  policy->refcount = 1;
  
  policy->rules_by_uid = _dbus_hash_table_new (DBUS_HASH_ULONG,
                                               NULL,
                                               free_rule_list_func);
  if (policy->rules_by_uid == NULL)
    goto failed;

  policy->rules_by_gid = _dbus_hash_table_new (DBUS_HASH_ULONG,
                                               NULL,
                                               free_rule_list_func);
  if (policy->rules_by_gid == NULL)
    goto failed;
  
  return policy;
  
 failed:
  bus_policy_unref (policy);
  return NULL;
}

void
bus_policy_ref (BusPolicy *policy)
{
  _dbus_assert (policy->refcount > 0);

  policy->refcount += 1;
}

void
bus_policy_unref (BusPolicy *policy)
{
  _dbus_assert (policy->refcount > 0);

  policy->refcount -= 1;

  if (policy->refcount == 0)
    {
      _dbus_list_foreach (&policy->default_rules, free_rule_func, NULL);
      _dbus_list_clear (&policy->default_rules);

      _dbus_list_foreach (&policy->mandatory_rules, free_rule_func, NULL);
      _dbus_list_clear (&policy->mandatory_rules);
      
      if (policy->rules_by_uid)
        {
          _dbus_hash_table_unref (policy->rules_by_uid);
          policy->rules_by_uid = NULL;
        }

      if (policy->rules_by_gid)
        {
          _dbus_hash_table_unref (policy->rules_by_gid);
          policy->rules_by_gid = NULL;
        }
      
      dbus_free (policy);
    }
}

static dbus_bool_t
add_list_to_client (DBusList        **list,
                    BusClientPolicy  *client)
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
          if (!bus_client_policy_append_rule (client, rule))
            return FALSE;
          break;
        }
    }
  
  return TRUE;
}

BusClientPolicy*
bus_policy_create_client_policy (BusPolicy      *policy,
                                 DBusConnection *connection)
{
  BusClientPolicy *client;
  unsigned long uid;
  DBusList **list;

  _dbus_assert (dbus_connection_get_is_authenticated (connection));
  
  client = bus_client_policy_new ();
  if (client == NULL)
    return NULL;

  if (!add_list_to_client (&policy->default_rules,
                           client))
    goto failed;

  /* we avoid the overhead of looking up user's groups
   * if we don't have any group rules anyway
   */
  if (_dbus_hash_table_get_n_entries (policy->rules_by_gid) > 0)
    {
      const unsigned long *groups;
      int n_groups;
      int i;
      
      if (!bus_connection_get_groups (connection, &groups, &n_groups))
        goto failed;
      
      i = 0;
      while (i < n_groups)
        {
          list = _dbus_hash_table_lookup_ulong (policy->rules_by_gid,
                                                groups[i]);
          
          if (list != NULL)
            {
              if (!add_list_to_client (list, client))
                goto failed;
            }
          
          ++i;
        }
    }

  if (!dbus_connection_get_unix_user (connection, &uid))
    goto failed;

  list = _dbus_hash_table_lookup_ulong (policy->rules_by_uid,
                                        uid);

  if (!add_list_to_client (list, client))
    goto failed;
  
  if (!add_list_to_client (&policy->mandatory_rules,
                           client))
    goto failed;

  bus_client_policy_optimize (client);
  
  return client;
  
 failed:
  bus_client_policy_unref (client);
  return NULL;
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

  /* FIXME there's currently no handling of wildcard user/group rules.
   */
  
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
bus_policy_allow_user (BusPolicy    *policy,
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
                              &policy->default_rules,
                              uid,
                              group_ids, n_group_ids);

  allowed = list_allows_user (allowed,
                              &policy->mandatory_rules,
                              uid,
                              group_ids, n_group_ids);

  dbus_free (group_ids);

  return allowed;
}

dbus_bool_t
bus_policy_append_default_rule (BusPolicy      *policy,
                                BusPolicyRule  *rule)
{
  if (!_dbus_list_append (&policy->default_rules, rule))
    return FALSE;

  bus_policy_rule_ref (rule);

  return TRUE;
}

dbus_bool_t
bus_policy_append_mandatory_rule (BusPolicy      *policy,
                                  BusPolicyRule  *rule)
{
  if (!_dbus_list_append (&policy->mandatory_rules, rule))
    return FALSE;

  bus_policy_rule_ref (rule);

  return TRUE;
}

static DBusList**
get_list (DBusHashTable *hash,
          unsigned long  key)
{
  DBusList **list;

  list = _dbus_hash_table_lookup_ulong (hash, key);

  if (list == NULL)
    {
      list = dbus_new0 (DBusList*, 1);
      if (list == NULL)
        return NULL;

      if (!_dbus_hash_table_insert_ulong (hash, key, list))
        {
          dbus_free (list);
          return NULL;
        }
    }

  return list;
}

dbus_bool_t
bus_policy_append_user_rule (BusPolicy      *policy,
                             dbus_uid_t      uid,
                             BusPolicyRule  *rule)
{
  DBusList **list;

  list = get_list (policy->rules_by_uid, uid);

  if (list == NULL)
    return FALSE;

  if (!_dbus_list_append (list, rule))
    return FALSE;

  bus_policy_rule_ref (rule);

  return TRUE;
}

dbus_bool_t
bus_policy_append_group_rule (BusPolicy      *policy,
                              dbus_gid_t      gid,
                              BusPolicyRule  *rule)
{
  DBusList **list;

  list = get_list (policy->rules_by_gid, gid);

  if (list == NULL)
    return FALSE;

  if (!_dbus_list_append (list, rule))
    return FALSE;

  bus_policy_rule_ref (rule);

  return TRUE;
}

struct BusClientPolicy
{
  int refcount;

  DBusList *rules;
};

BusClientPolicy*
bus_client_policy_new (void)
{
  BusClientPolicy *policy;

  policy = dbus_new0 (BusClientPolicy, 1);
  if (policy == NULL)
    return NULL;

  policy->refcount = 1;

  return policy;
}

void
bus_client_policy_ref (BusClientPolicy *policy)
{
  _dbus_assert (policy->refcount > 0);

  policy->refcount += 1;
}

static void
rule_unref_foreach (void *data,
                    void *user_data)
{
  BusPolicyRule *rule = data;

  bus_policy_rule_unref (rule);
}

void
bus_client_policy_unref (BusClientPolicy *policy)
{
  _dbus_assert (policy->refcount > 0);

  policy->refcount -= 1;

  if (policy->refcount == 0)
    {
      _dbus_list_foreach (&policy->rules,
                          rule_unref_foreach,
                          NULL);

      _dbus_list_clear (&policy->rules);
      
      dbus_free (policy);
    }
}

static void
remove_rules_by_type_up_to (BusClientPolicy   *policy,
                            BusPolicyRuleType  type,
                            DBusList          *up_to)
{
  DBusList *link;

  link = _dbus_list_get_first (&policy->rules);
  while (link != up_to)
    {
      BusPolicyRule *rule = link->data;
      DBusList *next = _dbus_list_get_next_link (&policy->rules, link);

      bus_policy_rule_unref (rule);
      _dbus_list_remove_link (&policy->rules, link);

      link = next;
    }
}

void
bus_client_policy_optimize (BusClientPolicy *policy)
{
  DBusList *link;

  /* The idea here is that if we have:
   * 
   * <allow send="foo"/>
   * <deny send="*"/>
   *
   * (for example) the deny will always override the allow.  So we
   * delete the allow. Ditto for deny followed by allow, etc. This is
   * a dumb thing to put in a config file, but the <include> feature
   * of files allows for an "inheritance and override" pattern where
   * it could make sense. If an included file wants to "start over"
   * with a blanket deny, no point keeping the rules from the parent
   * file.
   */

  _dbus_verbose ("Optimizing policy with %d rules\n",
                 _dbus_list_get_length (&policy->rules));
  
  link = _dbus_list_get_first (&policy->rules);
  while (link != NULL)
    {
      BusPolicyRule *rule = link->data;
      DBusList *next = _dbus_list_get_next_link (&policy->rules, link);
      dbus_bool_t remove_preceding;

      remove_preceding = FALSE;
      
      switch (rule->type)
        {
        case BUS_POLICY_RULE_SEND:
          remove_preceding =
            rule->d.send.message_name == NULL &&
            rule->d.send.destination == NULL;
          break;
        case BUS_POLICY_RULE_RECEIVE:
          remove_preceding =
            rule->d.receive.message_name == NULL &&
            rule->d.receive.origin == NULL;
          break;
        case BUS_POLICY_RULE_OWN:
          remove_preceding =
            rule->d.own.service_name == NULL;
          break;
        case BUS_POLICY_RULE_USER:
        case BUS_POLICY_RULE_GROUP:
          _dbus_assert_not_reached ("invalid rule");
          break;
        }
                
      if (remove_preceding)
        remove_rules_by_type_up_to (policy, rule->type,
                                    link);
      
      link = next;
    }

  _dbus_verbose ("After optimization, policy has %d rules\n",
                 _dbus_list_get_length (&policy->rules));
}

dbus_bool_t
bus_client_policy_append_rule (BusClientPolicy *policy,
                               BusPolicyRule   *rule)
{
  if (!_dbus_list_append (&policy->rules, rule))
    return FALSE;

  bus_policy_rule_ref (rule);

  return TRUE;
}

dbus_bool_t
bus_client_policy_check_can_send (BusClientPolicy *policy,
                                  BusRegistry     *registry,
                                  DBusConnection  *receiver,
                                  DBusMessage     *message)
{
  DBusList *link;
  dbus_bool_t allowed;
  
  /* policy->rules is in the order the rules appeared
   * in the config file, i.e. last rule that applies wins
   */

  allowed = FALSE;
  link = _dbus_list_get_first (&policy->rules);
  while (link != NULL)
    {
      BusPolicyRule *rule = link->data;

      link = _dbus_list_get_next_link (&policy->rules, link);
      
      /* Rule is skipped if it specifies a different
       * message name from the message, or a different
       * destination from the message
       */
      
      if (rule->type != BUS_POLICY_RULE_SEND)
        continue;

      if (rule->d.send.message_name != NULL)
        {
          if (!dbus_message_name_is (message,
                                     rule->d.send.message_name))
            continue;
        }

      if (rule->d.send.destination != NULL)
        {
          /* receiver can be NULL for messages that are sent to the
           * message bus itself, we check the strings in that case as
           * built-in services don't have a DBusConnection but messages
           * to them have a destination service name.
           */
          if (receiver == NULL)
            {
              if (!dbus_message_sender_is (message,
                                           rule->d.send.destination))
                continue;
            }
          else
            {
              DBusString str;
              BusService *service;
              
              _dbus_string_init_const (&str, rule->d.send.destination);
              
              service = bus_registry_lookup (registry, &str);
              if (service == NULL)
                continue;

              if (!bus_service_has_owner (service, receiver))
                continue;
            }
        }

      /* Use this rule */
      allowed = rule->allow;
    }

  return allowed;
}

dbus_bool_t
bus_client_policy_check_can_receive (BusClientPolicy *policy,
                                     BusRegistry     *registry,
                                     DBusConnection  *sender,
                                     DBusMessage     *message)
{
  DBusList *link;
  dbus_bool_t allowed;
  
  /* policy->rules is in the order the rules appeared
   * in the config file, i.e. last rule that applies wins
   */

  allowed = FALSE;
  link = _dbus_list_get_first (&policy->rules);
  while (link != NULL)
    {
      BusPolicyRule *rule = link->data;

      link = _dbus_list_get_next_link (&policy->rules, link);
      
      /* Rule is skipped if it specifies a different
       * message name from the message, or a different
       * origin from the message
       */
      
      if (rule->type != BUS_POLICY_RULE_RECEIVE)
        continue;

      if (rule->d.receive.message_name != NULL)
        {
          if (!dbus_message_name_is (message,
                                     rule->d.receive.message_name))
            continue;
        }

      if (rule->d.receive.origin != NULL)
        {          
          /* sender can be NULL for messages that originate from the
           * message bus itself, we check the strings in that case as
           * built-in services don't have a DBusConnection but will
           * still set the sender on their messages.
           */
          if (sender == NULL)
            {
              if (!dbus_message_sender_is (message,
                                           rule->d.receive.origin))
                continue;
            }
          else
            {
              BusService *service;
              DBusString str;

              _dbus_string_init_const (&str, rule->d.receive.origin);
              
              service = bus_registry_lookup (registry, &str);
              
              if (service == NULL)
                continue;

              if (!bus_service_has_owner (service, sender))
                continue;
            }
        }

      /* Use this rule */
      allowed = rule->allow;
    }

  return allowed;
}

dbus_bool_t
bus_client_policy_check_can_own (BusClientPolicy  *policy,
                                 DBusConnection   *connection,
                                 const DBusString *service_name)
{
  DBusList *link;
  dbus_bool_t allowed;
  
  /* policy->rules is in the order the rules appeared
   * in the config file, i.e. last rule that applies wins
   */

  allowed = FALSE;
  link = _dbus_list_get_first (&policy->rules);
  while (link != NULL)
    {
      BusPolicyRule *rule = link->data;

      link = _dbus_list_get_next_link (&policy->rules, link);
      
      /* Rule is skipped if it specifies a different service name from
       * the desired one.
       */
      
      if (rule->type != BUS_POLICY_RULE_OWN)
        continue;

      if (rule->d.own.service_name != NULL)
        {
          if (!_dbus_string_equal_c_str (service_name,
                                         rule->d.own.service_name))
            continue;
        }

      /* Use this rule */
      allowed = rule->allow;
    }

  return allowed;
}

#ifdef DBUS_BUILD_TESTS

dbus_bool_t
bus_policy_test (const DBusString *test_data_dir)
{
  /* This doesn't do anything for now because I decided to do it in
   * dispatch.c instead by having some of the clients in dispatch.c
   * have particular policies applied to them.
   */
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
