/* -*- mode: C; c-file-style: "gnu" -*- */
/* policy.c  Policies for what a connection can do
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
        case DBUS_POLICY_RULE_SEND:
          dbus_free (rule->d.send.message_name);
          dbus_free (rule->d.send.destination);
          break;
        case DBUS_POLICY_RULE_RECEIVE:
          dbus_free (rule->d.receive.message_name);
          dbus_free (rule->d.receive.origin);
          break;
        case DBUS_POLICY_RULE_OWN:
          dbus_free (rule->d.own.service_name);
          break;
        }
      
      dbus_free (rule);
    }
}

struct BusPolicy
{
  int refcount;

  DBusList *rules;
};

BusPolicy*
bus_policy_new (void)
{
  BusPolicy *policy;

  policy = dbus_new0 (BusPolicy, 1);
  if (policy == NULL)
    return NULL;

  policy->refcount = 1;

  return policy;
}

void
bus_policy_ref (BusPolicy *policy)
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
bus_policy_unref (BusPolicy *policy)
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

dbus_bool_t
bus_policy_check_can_send (BusPolicy      *policy,
                           DBusConnection *sender,
                           DBusMessage    *message)
{
  

}

dbus_bool_t
bus_policy_check_can_receive (BusPolicy      *policy,
                              DBusConnection *receiver,
                              DBusMessage    *message)
{


}

dbus_bool_t
bus_policy_check_can_own (BusPolicy      *policy,
                          DBusConnection *connection,
                          const char     *service_name)
{


}

#endif /* BUS_POLICY_H */
