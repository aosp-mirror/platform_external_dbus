/* -*- mode: C; c-file-style: "gnu" -*- */
/* policy.h  Bus security policy
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

#ifndef BUS_POLICY_H
#define BUS_POLICY_H

#include <dbus/dbus.h>
#include <dbus/dbus-string.h>
#include "bus.h"

typedef enum
{
  BUS_POLICY_RULE_SEND,
  BUS_POLICY_RULE_RECEIVE,
  BUS_POLICY_RULE_OWN,
  BUS_POLICY_RULE_USER,
  BUS_POLICY_RULE_GROUP
} BusPolicyRuleType;

struct BusPolicyRule
{
  int refcount;
  
  BusPolicyRuleType type;

  unsigned int allow : 1; /**< #TRUE if this allows, #FALSE if it denies */
  
  union
  {
    struct
    {
      /* either can be NULL meaning "any" */
      char *message_name;
      char *destination;
    } send;

    struct
    {
      /* either can be NULL meaning "any" */
      char *message_name;
      char *origin;
    } receive;

    struct
    {
      /* can be NULL meaning "any" */
      char *service_name;
    } own;

    struct
    {
      char *user;
      unsigned long uid;
    } user;

    struct
    {
      char *group;
      unsigned long gid;
    } group;
    
  } d;
};

BusPolicyRule* bus_policy_rule_new   (BusPolicyRuleType type,
                                      dbus_bool_t       allow);
void           bus_policy_rule_ref   (BusPolicyRule    *rule);
void           bus_policy_rule_unref (BusPolicyRule    *rule);

BusPolicy*       bus_policy_new                  (void);
void             bus_policy_ref                  (BusPolicy      *policy);
void             bus_policy_unref                (BusPolicy      *policy);
BusClientPolicy* bus_policy_create_client_policy (BusPolicy      *policy,
                                                  DBusConnection *connection);
dbus_bool_t      bus_policy_allow_user           (BusPolicy      *policy,
                                                  unsigned long   uid);

BusClientPolicy* bus_client_policy_new               (void);
void             bus_client_policy_ref               (BusClientPolicy  *policy);
void             bus_client_policy_unref             (BusClientPolicy  *policy);
dbus_bool_t      bus_client_policy_check_can_send    (BusClientPolicy  *policy,
                                                      BusRegistry      *registry,
                                                      DBusConnection   *receiver,
                                                      DBusMessage      *message);
dbus_bool_t      bus_client_policy_check_can_receive (BusClientPolicy  *policy,
                                                      BusRegistry      *registry,
                                                      DBusConnection   *sender,
                                                      DBusMessage      *message);
dbus_bool_t      bus_client_policy_check_can_own     (BusClientPolicy  *policy,
                                                      DBusConnection   *connection,
                                                      const DBusString *service_name);
dbus_bool_t      bus_client_policy_append_rule       (BusClientPolicy  *policy,
                                                      BusPolicyRule    *rule);
void             bus_client_policy_optimize          (BusClientPolicy  *policy);


#endif /* BUS_POLICY_H */
