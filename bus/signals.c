/* -*- mode: C; c-file-style: "gnu" -*- */
/* signals.c  Bus signal connection implementation
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
#include "signals.h"
#include "services.h"
#include "utils.h"

struct BusMatchRule
{
  int refcount;       /**< reference count */

  DBusConnection *matches_go_to; /**< Owner of the rule */

  unsigned int flags; /**< BusMatchFlags */

  int   message_type;
  char *interface;
  char *member;
  char *sender;
  char *destination;
  char *path;
};

BusMatchRule*
bus_match_rule_new (DBusConnection *matches_go_to)
{
  BusMatchRule *rule;

  rule = dbus_new0 (BusMatchRule, 1);
  if (rule == NULL)
    return NULL;

  rule->refcount = 1;
  rule->matches_go_to = matches_go_to;

  return rule;
}

void
bus_match_rule_ref (BusMatchRule *rule)
{
  _dbus_assert (rule->refcount > 0);

  rule->refcount += 1;
}

void
bus_match_rule_unref (BusMatchRule *rule)
{
  _dbus_assert (rule->refcount > 0);

  rule->refcount -= 1;
  if (rule->refcount == 0)
    {
      dbus_free (rule->interface);
      dbus_free (rule->member);
      dbus_free (rule->sender);
      dbus_free (rule->destination);
      dbus_free (rule->path);
      dbus_free (rule);
    }
}

#ifdef DBUS_ENABLE_VERBOSE_MODE
static char*
match_rule_to_string (BusMatchRule *rule)
{
  DBusString str;
  char *ret;
  
  if (!_dbus_string_init (&str))
    {
      char *s;
      while ((s = _dbus_strdup ("nomem")) == NULL)
        ; /* only OK for debug spew... */
      return s;
    }
  
  if (rule->flags & BUS_MATCH_MESSAGE_TYPE)
    {
      /* FIXME make type readable */
      if (!_dbus_string_append_printf (&str, "type='%d'", rule->message_type))
        goto nomem;
    }

  if (rule->flags & BUS_MATCH_INTERFACE)
    {
      if (_dbus_string_get_length (&str) > 0)
        {
          if (!_dbus_string_append (&str, ","))
            goto nomem;
        }
      
      if (!_dbus_string_append_printf (&str, "interface='%s'", rule->interface))
        goto nomem;
    }

  if (rule->flags & BUS_MATCH_MEMBER)
    {
      if (_dbus_string_get_length (&str) > 0)
        {
          if (!_dbus_string_append (&str, ","))
            goto nomem;
        }
      
      if (!_dbus_string_append_printf (&str, "member='%s'", rule->member))
        goto nomem;
    }

  if (rule->flags & BUS_MATCH_PATH)
    {
      if (_dbus_string_get_length (&str) > 0)
        {
          if (!_dbus_string_append (&str, ","))
            goto nomem;
        }
      
      if (!_dbus_string_append_printf (&str, "path='%s'", rule->path))
        goto nomem;
    }

  if (rule->flags & BUS_MATCH_SENDER)
    {
      if (_dbus_string_get_length (&str) > 0)
        {
          if (!_dbus_string_append (&str, ","))
            goto nomem;
        }
      
      if (!_dbus_string_append_printf (&str, "sender='%s'", rule->sender))
        goto nomem;
    }

  if (rule->flags & BUS_MATCH_DESTINATION)
    {
      if (_dbus_string_get_length (&str) > 0)
        {
          if (!_dbus_string_append (&str, ","))
            goto nomem;
        }
      
      if (!_dbus_string_append_printf (&str, "destination='%s'", rule->destination))
        goto nomem;
    }
  
  if (!_dbus_string_steal_data (&str, &ret))
    goto nomem;

  _dbus_string_free (&str);
  return ret;
  
 nomem:
  _dbus_string_free (&str);
  {
    char *s;
    while ((s = _dbus_strdup ("nomem")) == NULL)
      ;  /* only OK for debug spew... */
    return s;
  }
}
#endif /* DBUS_ENABLE_VERBOSE_MODE */

dbus_bool_t
bus_match_rule_set_message_type (BusMatchRule *rule,
                                 int           type)
{
  rule->flags |= BUS_MATCH_MESSAGE_TYPE;

  rule->message_type = type;

  return TRUE;
}

dbus_bool_t
bus_match_rule_set_interface (BusMatchRule *rule,
                              const char   *interface)
{
  char *new;

  _dbus_assert (interface != NULL);

  new = _dbus_strdup (interface);
  if (new == NULL)
    return FALSE;

  rule->flags |= BUS_MATCH_INTERFACE;
  dbus_free (rule->interface);
  rule->interface = new;

  return TRUE;
}

dbus_bool_t
bus_match_rule_set_member (BusMatchRule *rule,
                           const char   *member)
{
  char *new;

  _dbus_assert (member != NULL);

  new = _dbus_strdup (member);
  if (new == NULL)
    return FALSE;

  rule->flags |= BUS_MATCH_MEMBER;
  dbus_free (rule->member);
  rule->member = new;

  return TRUE;
}

dbus_bool_t
bus_match_rule_set_sender (BusMatchRule *rule,
                           const char   *sender)
{
  char *new;

  _dbus_assert (sender != NULL);

  new = _dbus_strdup (sender);
  if (new == NULL)
    return FALSE;

  rule->flags |= BUS_MATCH_SENDER;
  dbus_free (rule->sender);
  rule->sender = new;

  return TRUE;
}

dbus_bool_t
bus_match_rule_set_destination (BusMatchRule *rule,
                                const char   *destination)
{
  char *new;

  _dbus_assert (destination != NULL);

  new = _dbus_strdup (destination);
  if (new == NULL)
    return FALSE;

  rule->flags |= BUS_MATCH_DESTINATION;
  dbus_free (rule->destination);
  rule->destination = new;

  return TRUE;
}

dbus_bool_t
bus_match_rule_set_path (BusMatchRule *rule,
                         const char   *path)
{
  char *new;

  _dbus_assert (path != NULL);

  new = _dbus_strdup (path);
  if (new == NULL)
    return FALSE;

  rule->flags |= BUS_MATCH_PATH;
  dbus_free (rule->path);
  rule->path = new;

  return TRUE;
}

/*
 * The format is comma-separated with strings quoted with single quotes
 * as for the shell (to escape a literal single quote, use '\'').
 *
 * type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='Foo',
 * path='/bar/foo',destination=':452345-34'
 *
 */
BusMatchRule*
bus_match_rule_parse (DBusConnection   *matches_go_to,
                      const DBusString *rule_text,
                      DBusError        *error)
{
  BusMatchRule *rule;

  rule = bus_match_rule_new (matches_go_to);
  if (rule == NULL)
    goto oom;

  /* FIXME implement for real */
  
  if (!bus_match_rule_set_message_type (rule,
                                        DBUS_MESSAGE_TYPE_SIGNAL))
    goto oom;
  
  return rule;
  
 oom:
  if (rule)
    bus_match_rule_unref (rule);
  BUS_SET_OOM (error);
  return NULL;
}

struct BusMatchmaker
{
  int refcount;

  DBusList *all_rules;
};

BusMatchmaker*
bus_matchmaker_new (void)
{
  BusMatchmaker *matchmaker;

  matchmaker = dbus_new0 (BusMatchmaker, 1);
  if (matchmaker == NULL)
    return NULL;

  matchmaker->refcount = 1;
  
  return matchmaker;
}

void
bus_matchmaker_ref (BusMatchmaker *matchmaker)
{
  _dbus_assert (matchmaker->refcount > 0);

  matchmaker->refcount += 1;
}

void
bus_matchmaker_unref (BusMatchmaker *matchmaker)
{
  _dbus_assert (matchmaker->refcount > 0);

  matchmaker->refcount -= 1;
  if (matchmaker->refcount == 0)
    {
      while (matchmaker->all_rules != NULL)
        {
          BusMatchRule *rule;

          rule = matchmaker->all_rules->data;
          bus_match_rule_unref (rule);
          _dbus_list_remove_link (&matchmaker->all_rules,
                                  matchmaker->all_rules);
        }

      dbus_free (matchmaker);
    }
}

/* The rule can't be modified after it's added. */
dbus_bool_t
bus_matchmaker_add_rule (BusMatchmaker   *matchmaker,
                         BusMatchRule    *rule)
{
  _dbus_assert (bus_connection_is_active (rule->matches_go_to));

  if (!_dbus_list_append (&matchmaker->all_rules, rule))
    return FALSE;

  if (!bus_connection_add_match_rule (rule->matches_go_to, rule))
    {
      _dbus_list_remove_last (&matchmaker->all_rules, rule);
      return FALSE;
    }
  
  bus_match_rule_ref (rule);

#ifdef DBUS_ENABLE_VERBOSE_MODE
  {
    char *s = match_rule_to_string (rule);

    _dbus_verbose ("Added match rule %s to connection %p\n",
                   s, rule->matches_go_to);
    dbus_free (s);
  }
#endif
  
  return TRUE;
}

static dbus_bool_t
match_rule_equal (BusMatchRule *a,
                  BusMatchRule *b)
{
  if (a->flags != b->flags)
    return FALSE;

  if ((a->flags & BUS_MATCH_MESSAGE_TYPE) &&
      a->message_type != b->message_type)
    return FALSE;

  if ((a->flags & BUS_MATCH_MEMBER) &&
      strcmp (a->member, b->member) != 0)
    return FALSE;

  if ((a->flags & BUS_MATCH_PATH) &&
      strcmp (a->path, b->path) != 0)
    return FALSE;
  
  if ((a->flags & BUS_MATCH_INTERFACE) &&
      strcmp (a->interface, b->interface) != 0)
    return FALSE;

  if ((a->flags & BUS_MATCH_SENDER) &&
      strcmp (a->sender, b->sender) != 0)
    return FALSE;

  if ((a->flags & BUS_MATCH_DESTINATION) &&
      strcmp (a->destination, b->destination) != 0)
    return FALSE;

  return TRUE;
}

static void
bus_matchmaker_remove_rule_link (BusMatchmaker   *matchmaker,
                                 DBusList        *link)
{
  BusMatchRule *rule = link->data;
  
  bus_connection_remove_match_rule (rule->matches_go_to, rule);
  _dbus_list_remove_link (&matchmaker->all_rules, link);

#ifdef DBUS_ENABLE_VERBOSE_MODE
  {
    char *s = match_rule_to_string (rule);

    _dbus_verbose ("Removed match rule %s for connection %p\n",
                   s, rule->matches_go_to);
    dbus_free (s);
  }
#endif
  
  bus_match_rule_unref (rule);  
}

void
bus_matchmaker_remove_rule (BusMatchmaker   *matchmaker,
                            BusMatchRule    *rule)
{
  bus_connection_remove_match_rule (rule->matches_go_to, rule);
  _dbus_list_remove (&matchmaker->all_rules, rule);

#ifdef DBUS_ENABLE_VERBOSE_MODE
  {
    char *s = match_rule_to_string (rule);

    _dbus_verbose ("Removed match rule %s for connection %p\n",
                   s, rule->matches_go_to);
    dbus_free (s);
  }
#endif
  
  bus_match_rule_unref (rule);
}

/* Remove a single rule which is equal to the given rule by value */
dbus_bool_t
bus_matchmaker_remove_rule_by_value (BusMatchmaker   *matchmaker,
                                     BusMatchRule    *value,
                                     DBusError       *error)
{
  /* FIXME this is an unoptimized linear scan */

  DBusList *link;

  /* we traverse backward because bus_connection_remove_match_rule()
   * removes the most-recently-added rule
   */
  link = _dbus_list_get_last_link (&matchmaker->all_rules);
  while (link != NULL)
    {
      BusMatchRule *rule;
      DBusList *prev;

      rule = link->data;
      prev = _dbus_list_get_prev_link (&matchmaker->all_rules, link);

      if (match_rule_equal (rule, value))
        {
          bus_matchmaker_remove_rule_link (matchmaker, link);
          break;
        }

      link = prev;
    }

  if (link == NULL)
    {
      dbus_set_error (error, DBUS_ERROR_MATCH_RULE_NOT_FOUND,
                      "The given match rule wasn't found and can't be removed");
      return FALSE;
    }

  return TRUE;
}

void
bus_matchmaker_disconnected (BusMatchmaker   *matchmaker,
                             DBusConnection  *disconnected)
{
  DBusList *link;

  /* FIXME
   *
   * This scans all match rules on the bus. We could avoid that
   * for the rules belonging to the connection, since we keep
   * a list of those; but for the rules that just refer to
   * the connection we'd need to do something more elaborate.
   * 
   */
  
  _dbus_assert (bus_connection_is_active (disconnected));

  link = _dbus_list_get_first_link (&matchmaker->all_rules);
  while (link != NULL)
    {
      BusMatchRule *rule;
      DBusList *next;

      rule = link->data;
      next = _dbus_list_get_next_link (&matchmaker->all_rules, link);

      if (rule->matches_go_to == disconnected)
        {
          bus_matchmaker_remove_rule_link (matchmaker, link);
        }
      else if (((rule->flags & BUS_MATCH_SENDER) && *rule->sender == ':') ||
               ((rule->flags & BUS_MATCH_DESTINATION) && *rule->destination == ':'))
        {
          /* The rule matches to/from a base service, see if it's the
           * one being disconnected, since we know this service name
           * will never be recycled.
           */
          const char *name;

          name = bus_connection_get_name (disconnected);
          _dbus_assert (name != NULL); /* because we're an active connection */

          if (((rule->flags & BUS_MATCH_SENDER) &&
               strcmp (rule->sender, name) == 0) ||
              ((rule->flags & BUS_MATCH_DESTINATION) &&
               strcmp (rule->destination, name) == 0))
            {
              bus_matchmaker_remove_rule_link (matchmaker, link);
            }
        }

      link = next;
    }
}

static dbus_bool_t
connection_is_primary_owner (DBusConnection *connection,
                             const char     *service_name)
{
  BusService *service;
  DBusString str;
  BusRegistry *registry;

  registry = bus_connection_get_registry (connection);

  _dbus_string_init_const (&str, service_name);
  service = bus_registry_lookup (registry, &str);

  if (service == NULL)
    return FALSE; /* Service doesn't exist so connection can't own it. */

  return bus_service_get_primary_owner (service) == connection;
}

static dbus_bool_t
match_rule_matches (BusMatchRule    *rule,
                    BusConnections  *connections,
                    DBusConnection  *sender,
                    DBusConnection  *addressed_recipient,
                    DBusMessage     *message)
{
  /* All features of the match rule are AND'd together,
   * so FALSE if any of them don't match.
   */

  if (rule->flags & BUS_MATCH_MESSAGE_TYPE)
    {
      _dbus_assert (rule->message_type != DBUS_MESSAGE_TYPE_INVALID);

      if (rule->message_type != dbus_message_get_type (message))
        return FALSE;
    }

  if (rule->flags & BUS_MATCH_INTERFACE)
    {
      const char *iface;

      _dbus_assert (rule->interface != NULL);

      iface = dbus_message_get_interface (message);
      if (iface == NULL)
        return FALSE;

      if (strcmp (iface, rule->interface) != 0)
        return FALSE;
    }

  if (rule->flags & BUS_MATCH_MEMBER)
    {
      const char *member;

      _dbus_assert (rule->member != NULL);

      member = dbus_message_get_member (message);
      if (member == NULL)
        return FALSE;

      if (strcmp (member, rule->member) != 0)
        return FALSE;
    }

  if (rule->flags & BUS_MATCH_SENDER)
    {
      _dbus_assert (rule->sender != NULL);

      if (!connection_is_primary_owner (sender, rule->sender))
        return FALSE;
    }

  if (rule->flags & BUS_MATCH_DESTINATION)
    {
      const char *destination;

      _dbus_assert (rule->destination != NULL);

      if (addressed_recipient == NULL)
        return FALSE;

      destination = dbus_message_get_destination (message);
      if (destination == NULL)
        return FALSE;

      if (!connection_is_primary_owner (addressed_recipient, rule->destination))
        return FALSE;
    }

  if (rule->flags & BUS_MATCH_PATH)
    {
      const char *path;

      _dbus_assert (rule->path != NULL);

      path = dbus_message_get_path (message);
      if (path == NULL)
        return FALSE;

      if (strcmp (path, rule->path) != 0)
        return FALSE;
    }

  return TRUE;
}

dbus_bool_t
bus_matchmaker_get_recipients (BusMatchmaker   *matchmaker,
                               BusConnections  *connections,
                               DBusConnection  *sender,
                               DBusConnection  *addressed_recipient,
                               DBusMessage     *message,
                               DBusList       **recipients_p)
{
  /* FIXME for now this is a wholly unoptimized linear search */

  DBusList *link;

  _dbus_assert (*recipients_p == NULL);

  /* This avoids sending same message to the same connection twice.
   * Purpose of the stamp instead of a bool is to avoid iterating over
   * all connections resetting the bool each time.
   */
  bus_connections_increment_stamp (connections);

  /* addressed_recipient is already receiving the message, don't add to list.
   * NULL addressed_recipient means either bus driver, or this is a signal
   * and thus lacks a specific addressed_recipient.
   */
  if (addressed_recipient != NULL)
    bus_connection_mark_stamp (addressed_recipient);

  link = _dbus_list_get_first_link (&matchmaker->all_rules);
  while (link != NULL)
    {
      BusMatchRule *rule;

      rule = link->data;

#ifdef DBUS_ENABLE_VERBOSE_MODE
      {
        char *s = match_rule_to_string (rule);
        
        _dbus_verbose ("Checking whether message matches rule %s for connection %p\n",
                       s, rule->matches_go_to);
        dbus_free (s);
      }
#endif
      
      if (match_rule_matches (rule, connections,
                              sender, addressed_recipient, message))
        {
          _dbus_verbose ("Rule matched\n");
          
          /* Append to the list if we haven't already */
          if (bus_connection_mark_stamp (rule->matches_go_to))
            {
              if (!_dbus_list_append (recipients_p, rule->matches_go_to))
                goto nomem;
            }
#ifdef DBUS_ENABLE_VERBOSE_MODE
          else
            {
              _dbus_verbose ("Connection already receiving this message, so not adding again\n");
            }
#endif /* DBUS_ENABLE_VERBOSE_MODE */
        }

      link = _dbus_list_get_next_link (&matchmaker->all_rules, link);
    }

  return TRUE;

 nomem:
  _dbus_list_clear (recipients_p);
  return FALSE;
}

#ifdef DBUS_BUILD_TESTS
#include "test.h"

dbus_bool_t
bus_signals_test (const DBusString *test_data_dir)
{
  BusMatchmaker *matchmaker;

  matchmaker = bus_matchmaker_new ();
  bus_matchmaker_ref (matchmaker);
  bus_matchmaker_unref (matchmaker);
  bus_matchmaker_unref (matchmaker);
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */

