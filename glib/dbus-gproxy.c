/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-gproxy.c Proxy for remote objects
 *
 * Copyright (C) 2003, 2004, 2005 Red Hat, Inc.
 * Copyright (C) 2005 Nokia
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
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus-signature.h>
#include "dbus-gutils.h"
#include "dbus-gsignature.h"
#include "dbus-gvalue.h"
#include "dbus-gvalue-utils.h"
#include "dbus-gobject.h"
#include <string.h>
#include <glib/gi18n.h>
#include <gobject/gvaluecollector.h>

#define DBUS_G_PROXY_CALL_TO_ID(x) (GPOINTER_TO_UINT(x))
#define DBUS_G_PROXY_ID_TO_CALL(x) (GUINT_TO_POINTER(x))

/**
 * @addtogroup DBusGLibInternals
 *
 * @{
 */

/**
 * DBusGProxyManager typedef
 */

typedef struct _DBusGProxyManager DBusGProxyManager;

/**
 * Internals of DBusGProxy
 */
struct _DBusGProxy
{
  GObject parent;             /**< Parent instance */
  
  DBusGProxyManager *manager; /**< Proxy manager */
  char *name;                 /**< Name messages go to or NULL */
  char *path;                 /**< Path messages go to or NULL */
  char *interface;            /**< Interface messages go to or NULL */

  DBusGProxyCall *name_call;  /**< Pending call id to retrieve name owner */
  guint for_owner : 1;        /**< Whether or not this proxy is for a name owner */
  guint associated : 1;       /**< Whether or not this proxy is associated (for name proxies) */

  /* FIXME: make threadsafe? */
  guint call_id_counter;      /**< Integer counter for pending calls */

  GData *signal_signatures;   /**< D-BUS signatures for each signal */

  GHashTable *pending_calls;  /**< Calls made on this proxy which have not yet returned */
};

/**
 * Class struct for DBusGProxy
 */
struct _DBusGProxyClass
{
  GObjectClass parent_class;  /**< Parent class */
};

static void dbus_g_proxy_init               (DBusGProxy      *proxy);
static void dbus_g_proxy_class_init         (DBusGProxyClass *klass);
static GObject *dbus_g_proxy_constructor    (GType                  type,
					     guint                  n_construct_properties,
					     GObjectConstructParam *construct_properties);
static void     dbus_g_proxy_set_property       (GObject               *object,
						 guint                  prop_id,
						 const GValue          *value,
						 GParamSpec            *pspec);
static void     dbus_g_proxy_get_property       (GObject               *object,
						 guint                  prop_id,
						 GValue                *value,
						 GParamSpec            *pspec);

static void dbus_g_proxy_finalize           (GObject         *object);
static void dbus_g_proxy_dispose            (GObject         *object);
static void dbus_g_proxy_destroy            (DBusGProxy      *proxy);
static void dbus_g_proxy_emit_remote_signal (DBusGProxy      *proxy,
                                             DBusMessage     *message);

static DBusGProxyCall *manager_begin_bus_call (DBusGProxyManager    *manager,
					       const char          *method,
					       DBusGProxyCallNotify notify,
					       gpointer             data,
					       GDestroyNotify       destroy,
					       GType                first_arg_type,
					       ...);
static guint dbus_g_proxy_begin_call_internal (DBusGProxy          *proxy,
					       const char          *method,
					       DBusGProxyCallNotify notify,
					       gpointer             data,
					       GDestroyNotify       destroy,
					       GValueArray         *args);
static gboolean dbus_g_proxy_end_call_internal (DBusGProxy        *proxy,
						guint              call_id,
						GError           **error,
						GType              first_arg_type,
						va_list            args);

/**
 * A list of proxies with a given name+path+interface, used to
 * route incoming signals.
 */
typedef struct
{
  GSList *proxies; /**< The list of proxies */

  char name[4]; /**< name (empty string for none), nul byte,
                 *   path, nul byte,
                 *   interface, nul byte
                 */
  
} DBusGProxyList;

/**
 * DBusGProxyManager's primary task is to route signals to the proxies
 * those signals are emitted on. In order to do this it also has to
 * track the owners of the names proxies are bound to.
 */
struct _DBusGProxyManager
{
  GStaticMutex lock; /**< Thread lock */
  int refcount;      /**< Reference count */
  DBusConnection *connection; /**< Connection we're associated with. */

  DBusGProxy *bus_proxy; /**< Special internal proxy used to talk to the bus */

  GHashTable *proxy_lists; /**< Hash used to route incoming signals
                            *   and iterate over proxies
                            */
  GHashTable *owner_names; /**< Hash to keep track of mapping from
			    *   base name -> [name,name,...] for proxies which
			    *   are for names.
			    */
  GSList *unassociated_proxies;     /**< List of name proxies for which
				     *   there was no result for
				     *   GetNameOwner
				     */
};

static DBusGProxyManager *dbus_g_proxy_manager_ref    (DBusGProxyManager *manager);
static DBusHandlerResult  dbus_g_proxy_manager_filter (DBusConnection    *connection,
                                                       DBusMessage       *message,
                                                       void              *user_data);


/** Lock the DBusGProxyManager */
#define LOCK_MANAGER(mgr)   (g_static_mutex_lock (&(mgr)->lock))
/** Unlock the DBusGProxyManager */
#define UNLOCK_MANAGER(mgr) (g_static_mutex_unlock (&(mgr)->lock))

static int g_proxy_manager_slot = -1;

/* Lock controlling get/set manager as data on each connection */
static GStaticMutex connection_g_proxy_lock = G_STATIC_MUTEX_INIT;

static DBusGProxyManager*
dbus_g_proxy_manager_get (DBusConnection *connection)
{
  DBusGProxyManager *manager;

  dbus_connection_allocate_data_slot (&g_proxy_manager_slot);
  if (g_proxy_manager_slot < 0)
    g_error ("out of memory");
  
  g_static_mutex_lock (&connection_g_proxy_lock);
  
  manager = dbus_connection_get_data (connection, g_proxy_manager_slot);
  if (manager != NULL)
    {
      dbus_connection_free_data_slot (&g_proxy_manager_slot);
      dbus_g_proxy_manager_ref (manager);
      g_static_mutex_unlock (&connection_g_proxy_lock);
      return manager;
    }
  
  manager = g_new0 (DBusGProxyManager, 1);

  manager->refcount = 1;
  manager->connection = connection;

  g_static_mutex_init (&manager->lock);

  /* Proxy managers keep the connection alive, which means that
   * DBusGProxy indirectly does. To free a connection you have to free
   * all the proxies referring to it.
   */
  dbus_connection_ref (manager->connection);

  dbus_connection_set_data (connection, g_proxy_manager_slot,
                            manager, NULL);

  dbus_connection_add_filter (connection, dbus_g_proxy_manager_filter,
                              manager, NULL);
  
  g_static_mutex_unlock (&connection_g_proxy_lock);
  
  return manager;
}

static DBusGProxyManager * 
dbus_g_proxy_manager_ref (DBusGProxyManager *manager)
{
  g_assert (manager != NULL);
  g_assert (manager->refcount > 0);

  LOCK_MANAGER (manager);
  
  manager->refcount += 1;

  UNLOCK_MANAGER (manager);

  return manager;
}

static void
dbus_g_proxy_manager_unref (DBusGProxyManager *manager)
{
  g_assert (manager != NULL);
  g_assert (manager->refcount > 0);

  LOCK_MANAGER (manager);
  manager->refcount -= 1;
  if (manager->refcount == 0)
    {
      UNLOCK_MANAGER (manager);

      if (manager->bus_proxy)
	g_object_unref (manager->bus_proxy);

      if (manager->proxy_lists)
        {
          /* can't have any proxies left since they hold
           * a reference to the proxy manager.
           */
          g_assert (g_hash_table_size (manager->proxy_lists) == 0);
          
          g_hash_table_destroy (manager->proxy_lists);
          manager->proxy_lists = NULL;

        }

      if (manager->owner_names)
	{
	  /* Since we destroyed all proxies, none can be tracking
	   * name owners
	   */
          g_assert (g_hash_table_size (manager->owner_names) == 0);

	  g_hash_table_destroy (manager->owner_names);
	  manager->owner_names = NULL;
	}

      g_assert (manager->unassociated_proxies == NULL);
      
      g_static_mutex_free (&manager->lock);

      g_static_mutex_lock (&connection_g_proxy_lock);

      dbus_connection_remove_filter (manager->connection, dbus_g_proxy_manager_filter,
                                     manager);
      
      dbus_connection_set_data (manager->connection,
                                g_proxy_manager_slot,
                                NULL, NULL);

      g_static_mutex_unlock (&connection_g_proxy_lock);
      
      dbus_connection_unref (manager->connection);
      g_free (manager);

      dbus_connection_free_data_slot (&g_proxy_manager_slot);
    }
  else
    {
      UNLOCK_MANAGER (manager);
    }
}

static guint
tristring_hash (gconstpointer key)
{
  const char *p = key;
  guint h = *p;

  if (h)
    {
      for (p += 1; *p != '\0'; p++)
        h = (h << 5) - h + *p;
    }

  /* skip nul and do the next substring */
  for (p += 1; *p != '\0'; p++)
    h = (h << 5) - h + *p;

  /* skip nul again and another substring */
  for (p += 1; *p != '\0'; p++)
    h = (h << 5) - h + *p;
  
  return h;
}

static gboolean
strequal_len (const char *a,
              const char *b,
              size_t     *lenp)
{
  size_t a_len;
  size_t b_len;

  a_len = strlen (a);
  b_len = strlen (b);

  if (a_len != b_len)
    return FALSE;

  if (memcmp (a, b, a_len) != 0)
    return FALSE;
  
  *lenp = a_len;

  return TRUE;
}

static gboolean
tristring_equal (gconstpointer  a,
                 gconstpointer  b)
{
  const char *ap = a;
  const char *bp = b;
  size_t len;

  if (!strequal_len (ap, bp, &len))
    return FALSE;

  ap += len + 1;
  bp += len + 1;

  if (!strequal_len (ap, bp, &len))
    return FALSE;

  ap += len + 1;
  bp += len + 1;

  if (strcmp (ap, bp) != 0)
    return FALSE;
  
  return TRUE;
}

static char*
tristring_alloc_from_strings (size_t      padding_before,
                              const char *name,
                              const char *path,
                              const char *interface)
{
  size_t name_len, iface_len, path_len, len;
  char *tri;
  
  if (name)
    name_len = strlen (name);
  else
    name_len = 0;

  path_len = strlen (path);
  
  iface_len = strlen (interface);

  tri = g_malloc (padding_before + name_len + path_len + iface_len + 3);

  len = padding_before;
  
  if (name)
    memcpy (&tri[len], name, name_len);

  len += name_len;
  tri[len] = '\0';
  len += 1;

  g_assert (len == (padding_before + name_len + 1));
  
  memcpy (&tri[len], path, path_len);
  len += path_len;
  tri[len] = '\0';
  len += 1;

  g_assert (len == (padding_before + name_len + path_len + 2));
  
  memcpy (&tri[len], interface, iface_len);
  len += iface_len;
  tri[len] = '\0';
  len += 1;

  g_assert (len == (padding_before + name_len + path_len + iface_len + 3));

  return tri;
}

static char*
tristring_from_proxy (DBusGProxy *proxy)
{
  return tristring_alloc_from_strings (0,
                                       proxy->name,
                                       proxy->path,
                                       proxy->interface);
}

static char*
tristring_from_message (DBusMessage *message)
{
  const char *path;
  const char *interface;

  path = dbus_message_get_path (message);
  interface = dbus_message_get_interface (message);

  g_assert (path);
  g_assert (interface);
  
  return tristring_alloc_from_strings (0,
                                       dbus_message_get_sender (message),
                                       path, interface);
}

static DBusGProxyList*
g_proxy_list_new (DBusGProxy *first_proxy)
{
  DBusGProxyList *list;
  
  list = (void*) tristring_alloc_from_strings (G_STRUCT_OFFSET (DBusGProxyList, name),
                                               first_proxy->name,
                                               first_proxy->path,
                                               first_proxy->interface);
  list->proxies = NULL;

  return list;
}

static void
g_proxy_list_free (DBusGProxyList *list)
{
  /* we don't hold a reference to the proxies in the list,
   * as they ref the GProxyManager
   */
  g_slist_free (list->proxies);  

  g_free (list);
}

static char*
g_proxy_get_match_rule (DBusGProxy *proxy)
{
  /* FIXME Escaping is required here */
  
  if (proxy->name)
    return g_strdup_printf ("type='signal',sender='%s',path='%s',interface='%s'",
                            proxy->name, proxy->path, proxy->interface);
  else
    return g_strdup_printf ("type='signal',path='%s',interface='%s'",
                            proxy->path, proxy->interface);
}

typedef struct
{
  char *name;
  guint refcount;
} DBusGProxyNameOwnerInfo;

static gint
find_name_in_info (gconstpointer a, gconstpointer b)
{
  const DBusGProxyNameOwnerInfo *info = a;
  const char *name = b;

  return strcmp (info->name, name);
}

typedef struct
{
  const char *name;
  const char *owner;
  DBusGProxyNameOwnerInfo *info;
} DBusGProxyNameOwnerForeachData;

static void
name_owner_foreach (gpointer key, gpointer val, gpointer data)
{
  const char *owner;
  DBusGProxyNameOwnerForeachData *foreach_data;
  GSList *names;
  GSList *link;

  owner = key;
  names = val;
  foreach_data = data;

  if (foreach_data->owner != NULL)
    return;

  g_assert (foreach_data->info == NULL);

  link = g_slist_find_custom (names, foreach_data->name, find_name_in_info);
  if (link)
    {
      foreach_data->owner = owner;
      foreach_data->info = link->data;
    }
}

static gboolean
dbus_g_proxy_manager_lookup_name_owner (DBusGProxyManager        *manager,
					const char               *name,
					DBusGProxyNameOwnerInfo **info,
					const char              **owner)
{
  DBusGProxyNameOwnerForeachData foreach_data;

  foreach_data.name = name;
  foreach_data.owner = NULL;
  foreach_data.info = NULL;
  
  g_hash_table_foreach (manager->owner_names, name_owner_foreach, &foreach_data);

  *info = foreach_data.info;
  *owner = foreach_data.owner;
  return *info != NULL;
}

static void
insert_nameinfo (DBusGProxyManager       *manager,
		 const char              *owner,
		 DBusGProxyNameOwnerInfo *info)
{
  GSList *names;
  gboolean insert;

  names = g_hash_table_lookup (manager->owner_names, owner);

  /* Only need to g_hash_table_insert the first time */
  insert = (names == NULL);

  names = g_slist_append (names, info); 

  if (insert)
    g_hash_table_insert (manager->owner_names, g_strdup (owner), names);
}

static void
dbus_g_proxy_manager_monitor_name_owner (DBusGProxyManager  *manager,
					 const char         *owner,
					 const char         *name)
{
  GSList *names;
  GSList *link;
  DBusGProxyNameOwnerInfo *nameinfo;

  names = g_hash_table_lookup (manager->owner_names, owner);
  link = g_slist_find_custom (names, name, find_name_in_info);
  
  if (!link)
    {
      nameinfo = g_new0 (DBusGProxyNameOwnerInfo, 1);
      nameinfo->name = g_strdup (name);
      nameinfo->refcount = 1;

      insert_nameinfo (manager, owner, nameinfo);
    }
  else
    {
      nameinfo = link->data;
      nameinfo->refcount++;
    }
}

static void
dbus_g_proxy_manager_unmonitor_name_owner (DBusGProxyManager  *manager,
					   const char         *name)
{
  DBusGProxyNameOwnerInfo *info;
  const char *owner;
  gboolean ret;

  ret = dbus_g_proxy_manager_lookup_name_owner (manager, name, &info, &owner);
  g_assert (ret);
  g_assert (info != NULL);
  g_assert (owner != NULL);

  info->refcount--;
  if (info->refcount == 0)
    {
      GSList *names;
      GSList *link;

      names = g_hash_table_lookup (manager->owner_names, owner);
      link = g_slist_find_custom (names, name, find_name_in_info);
      names = g_slist_delete_link (names, link);
      if (names != NULL)
	g_hash_table_insert (manager->owner_names, g_strdup (owner), names);
      else
	g_hash_table_remove (manager->owner_names, owner);

      g_free (info->name);
      g_free (info);
    }
}

typedef struct
{
  const char *name;
  GSList *destroyed;
} DBusGProxyUnassociateData;

static void
unassociate_proxies (gpointer key, gpointer val, gpointer user_data)
{
  DBusGProxyList *list;
  const char *name;
  GSList *tmp;
  DBusGProxyUnassociateData *data;

  list = val;
  data = user_data;
  name = data->name;
  
  for (tmp = list->proxies; tmp; tmp = tmp->next)
    {
      DBusGProxy *proxy = DBUS_G_PROXY (tmp->data);
      DBusGProxyManager *manager;

      manager = proxy->manager;

      if (!strcmp (proxy->name, name))
	{
	  if (!proxy->for_owner)
	    {
	      g_assert (proxy->associated);
	      g_assert (proxy->name_call == NULL);

	      proxy->associated = FALSE;
	      manager->unassociated_proxies = g_slist_prepend (manager->unassociated_proxies, proxy);
	    }
	  else
	    {
	      data->destroyed = g_slist_prepend (data->destroyed, proxy);
	    }
	}
    }
}

static void
dbus_g_proxy_manager_replace_name_owner (DBusGProxyManager  *manager,
					 const char         *name,
					 const char         *prev_owner,
					 const char         *new_owner)
{
  GSList *names;
	  
  if (prev_owner[0] == '\0')
    {
      GSList *tmp;
      GSList *removed;

      /* We have a new service, look at unassociated proxies */

      removed = NULL;

      for (tmp = manager->unassociated_proxies; tmp ; tmp = tmp->next)
	{
	  DBusGProxy *proxy;

	  proxy = tmp->data;

	  if (!strcmp (proxy->name, name))
	    {
	      removed = g_slist_prepend (removed, tmp);
	      
	      dbus_g_proxy_manager_monitor_name_owner (manager, new_owner, name);
	      proxy->associated = TRUE;
	    }
	}

      for (tmp = removed; tmp; tmp = tmp->next)
	manager->unassociated_proxies = g_slist_delete_link (manager->unassociated_proxies, tmp->data);
      g_slist_free (removed);
    }
  else
    {
      DBusGProxyNameOwnerInfo *info;
      GSList *link;

      /* Name owner changed or deleted */ 

      names = g_hash_table_lookup (manager->owner_names, prev_owner);

      link = g_slist_find_custom (names, name, find_name_in_info);

      info = NULL;
      if (link != NULL)
	{
	  info = link->data;
	  
	  names = g_slist_delete_link (names, link);

	  if (names == NULL)
	    g_hash_table_remove (manager->owner_names, prev_owner);
	}

      if (new_owner[0] == '\0')
	{
	  DBusGProxyUnassociateData data;
	  GSList *tmp;

	  data.name = name;
	  data.destroyed = NULL;

	  /* A service went away, we need to unassociate proxies */
	  g_hash_table_foreach (manager->proxy_lists,
				unassociate_proxies, &data);

	  UNLOCK_MANAGER (manager);

	  for (tmp = data.destroyed; tmp; tmp = tmp->next)
	    dbus_g_proxy_destroy (tmp->data);
	  g_slist_free (data.destroyed);

	  LOCK_MANAGER (manager);
	}
      else
	{
	  insert_nameinfo (manager, new_owner, info);
	}
    }
}

static void
got_name_owner_cb (DBusGProxy       *bus_proxy,
		   DBusGProxyCall   *call,
		   void             *user_data)
{
  DBusGProxy *proxy;
  GError *error;
  char *owner;

  proxy = user_data;
  error = NULL;
  owner = NULL;

  LOCK_MANAGER (proxy->manager);

  if (!dbus_g_proxy_end_call (bus_proxy, call, &error,
			      G_TYPE_STRING, &owner,
			      G_TYPE_INVALID))
    {
      if (error->domain == DBUS_GERROR && error->code == DBUS_GERROR_NAME_HAS_NO_OWNER)
	{
	  proxy->manager->unassociated_proxies = g_slist_prepend (proxy->manager->unassociated_proxies, proxy);
	}
      else
	g_warning ("Couldn't get name owner (%s): %s",
		   dbus_g_error_get_name (error),
		   error->message);

      g_clear_error (&error);
      goto out;
    }
  else
    {
      dbus_g_proxy_manager_monitor_name_owner (proxy->manager, owner, proxy->name);
      proxy->associated = TRUE;
    }

 out:
  proxy->name_call = NULL;
  UNLOCK_MANAGER (proxy->manager);
  g_free (owner);
}

static char *
get_name_owner (DBusConnection     *connection,
		const char         *name,
		GError            **error)
{
  DBusError derror;
  DBusMessage *request, *reply;
  char *base_name;
  
  dbus_error_init (&derror);

  base_name = NULL;
  reply = NULL;

  request = dbus_message_new_method_call (DBUS_SERVICE_DBUS,
					  DBUS_PATH_DBUS,
					  DBUS_INTERFACE_DBUS,
					  "GetNameOwner");
  if (request == NULL)
    g_error ("Out of memory");
  
  if (!dbus_message_append_args (request, 
				 DBUS_TYPE_STRING, &name, 
				 DBUS_TYPE_INVALID))
    g_error ("Out of memory");

  reply =
    dbus_connection_send_with_reply_and_block (connection,
                                               request,
                                               2000, &derror);
  if (reply == NULL)
    goto error;

  if (dbus_set_error_from_message (&derror, reply))
    goto error;

  if (!dbus_message_get_args (reply, &derror, 
			      DBUS_TYPE_STRING, &base_name, 
			      DBUS_TYPE_INVALID))
    goto error;

  base_name = g_strdup (base_name);
  goto out;

 error:
  g_assert (dbus_error_is_set (&derror));
  dbus_set_g_error (error, &derror);
  dbus_error_free (&derror);

 out:
  if (request)
    dbus_message_unref (request);
  if (reply)
    dbus_message_unref (reply);

  return base_name;
}


static void
dbus_g_proxy_manager_register (DBusGProxyManager *manager,
                               DBusGProxy        *proxy)
{
  DBusGProxyList *list;

  LOCK_MANAGER (manager);

  if (manager->proxy_lists == NULL)
    {
      g_assert (manager->owner_names == NULL);

      list = NULL;
      manager->proxy_lists = g_hash_table_new_full (tristring_hash,
                                                    tristring_equal,
                                                    NULL,
                                                    (GFreeFunc) g_proxy_list_free);
      manager->owner_names = g_hash_table_new_full (g_str_hash,
                                                    g_str_equal,
                                                    g_free,
                                                    NULL);
      /* FIXME - for now we listen for all NameOwnerChanged; once
       * Anders' detail patch lands we should add individual rules
       */
      dbus_bus_add_match (manager->connection,
                          "type='signal',sender='" DBUS_SERVICE_DBUS
			  "',path='" DBUS_PATH_DBUS
			  "',interface='" DBUS_INTERFACE_DBUS
			  "',member='NameOwnerChanged'",
			  NULL);
    }
  else
    {
      char *tri;

      tri = tristring_from_proxy (proxy);
      
      list = g_hash_table_lookup (manager->proxy_lists, tri);

      g_free (tri);
    }
      
  if (list == NULL)
    {
      list = g_proxy_list_new (proxy);
      
      g_hash_table_replace (manager->proxy_lists,
                            list->name, list);
    }

  if (list->proxies == NULL)
    {
      /* We have to add the match rule to the server,
       * but FIXME only if the server is a message bus,
       * not if it's a peer.
       */
      char *rule;

      rule = g_proxy_get_match_rule (proxy);
      
      /* We don't check for errors; it's not like anyone would handle them,
       * and we don't want a round trip here.
       */
      dbus_bus_add_match (manager->connection,
                          rule, NULL);

      g_free (rule);
    }

  g_assert (g_slist_find (list->proxies, proxy) == NULL);
  
  list->proxies = g_slist_prepend (list->proxies, proxy);

  if (!proxy->for_owner)
    {
      const char *owner;
      DBusGProxyNameOwnerInfo *info;

      if (!dbus_g_proxy_manager_lookup_name_owner (manager, proxy->name, &info, &owner))
	{
	  proxy->name_call = manager_begin_bus_call (manager, "GetNameOwner",
						     got_name_owner_cb,
						     proxy, NULL,
						     G_TYPE_STRING,
						     proxy->name, 
						     G_TYPE_INVALID);
	  
	  proxy->associated = FALSE;
	}
      else
	{
	  info->refcount++;
	  proxy->associated = TRUE;
	}
    }
  
  UNLOCK_MANAGER (manager);
}

static void
dbus_g_proxy_manager_unregister (DBusGProxyManager *manager,
                                DBusGProxy        *proxy)
{
  DBusGProxyList *list;
  char *tri;
  
  LOCK_MANAGER (manager);

#ifndef G_DISABLE_CHECKS
  if (manager->proxy_lists == NULL)
    {
      g_warning ("Trying to unregister a proxy but there aren't any registered");
      return;
    }
#endif

  tri = tristring_from_proxy (proxy);
  
  list = g_hash_table_lookup (manager->proxy_lists, tri);

#ifndef G_DISABLE_CHECKS
  if (list == NULL)
    {
      g_warning ("Trying to unregister a proxy but it isn't registered");
      return;
    }
#endif

  g_assert (g_slist_find (list->proxies, proxy) != NULL);
  
  list->proxies = g_slist_remove (list->proxies, proxy);

  g_assert (g_slist_find (list->proxies, proxy) == NULL);

  if (!proxy->for_owner)
    {
      if (!proxy->associated)
	{
	  GSList *link;

	  if (proxy->name_call != 0)
	    {
	      dbus_g_proxy_cancel_call (manager->bus_proxy, proxy->name_call);
	      proxy->name_call = 0;
	    }
	  else
	    {
	      link = g_slist_find (manager->unassociated_proxies, proxy);
	      g_assert (link != NULL);

	      manager->unassociated_proxies = g_slist_delete_link (manager->unassociated_proxies, link);
	    }
	}
      else
	{
	  g_assert (proxy->name_call == 0);
	  
	  dbus_g_proxy_manager_unmonitor_name_owner (manager, proxy->name);
	}
    }

  if (list->proxies == NULL)
    {
      char *rule;
      g_hash_table_remove (manager->proxy_lists,
                           tri);
      list = NULL;

      rule = g_proxy_get_match_rule (proxy);
      dbus_bus_remove_match (manager->connection,
                             rule, NULL);
      g_free (rule);
    }
  
  if (g_hash_table_size (manager->proxy_lists) == 0)
    {
      g_hash_table_destroy (manager->proxy_lists);
      manager->proxy_lists = NULL;
    }

  g_free (tri);
      
  UNLOCK_MANAGER (manager);
}

static void
list_proxies_foreach (gpointer key,
                      gpointer value,
                      gpointer user_data)
{
  DBusGProxyList *list;
  GSList **ret;
  GSList *tmp;
  
  list = value;
  ret = user_data;

  tmp = list->proxies;
  while (tmp != NULL)
    {
      DBusGProxy *proxy = DBUS_G_PROXY (tmp->data);

      g_object_ref (proxy);
      *ret = g_slist_prepend (*ret, proxy);
      
      tmp = tmp->next;
    }
}

static GSList*
dbus_g_proxy_manager_list_all (DBusGProxyManager *manager)
{
  GSList *ret;

  ret = NULL;

  if (manager->proxy_lists)
    {
      g_hash_table_foreach (manager->proxy_lists,
                            list_proxies_foreach,
                            &ret);
    }

  return ret;
}

static DBusHandlerResult
dbus_g_proxy_manager_filter (DBusConnection    *connection,
                             DBusMessage       *message,
                             void              *user_data)
{
  DBusGProxyManager *manager;
  
  if (dbus_message_get_type (message) != DBUS_MESSAGE_TYPE_SIGNAL)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  manager = user_data;

  dbus_g_proxy_manager_ref (manager);
  
  LOCK_MANAGER (manager);
  
  if (dbus_message_is_signal (message,
                              DBUS_INTERFACE_LOCAL,
                              "Disconnected"))
    {
      /* Destroy all the proxies, quite possibly resulting in unreferencing
       * the proxy manager and the connection as well.
       */
      GSList *all;
      GSList *tmp;

      all = dbus_g_proxy_manager_list_all (manager);

      tmp = all;
      while (tmp != NULL)
        {
          DBusGProxy *proxy;

          proxy = DBUS_G_PROXY (tmp->data);

          UNLOCK_MANAGER (manager);
          dbus_g_proxy_destroy (proxy);
          g_object_unref (G_OBJECT (proxy));
          LOCK_MANAGER (manager);
          
          tmp = tmp->next;
        }

      g_slist_free (all);

#ifndef G_DISABLE_CHECKS
      if (manager->proxy_lists != NULL)
        g_warning ("Disconnection emitted \"destroy\" on all DBusGProxy, but somehow new proxies were created in response to one of those destroy signals. This will cause a memory leak.");
#endif
    }
  else
    {
      char *tri;
      GSList *full_list;
      GSList *owned_names;
      GSList *tmp;
      const char *sender;

      /* First we handle NameOwnerChanged internally */
      if (dbus_message_is_signal (message,
				  DBUS_INTERFACE_DBUS,
				  "NameOwnerChanged"))
	{
	  const char *name;
	  const char *prev_owner;
	  const char *new_owner;
	  DBusError derr;

	  dbus_error_init (&derr);
	  if (!dbus_message_get_args (message,
				      &derr,
				      DBUS_TYPE_STRING,
				      &name,
				      DBUS_TYPE_STRING,
				      &prev_owner,
				      DBUS_TYPE_STRING,
				      &new_owner,
				      DBUS_TYPE_INVALID))
	    {
	      /* Ignore this error */
	      dbus_error_free (&derr);
	    }
	  else if (manager->owner_names != NULL)
	    {
	      dbus_g_proxy_manager_replace_name_owner (manager, name, prev_owner, new_owner);
	    }
	}

      sender = dbus_message_get_sender (message);

      /* dbus spec requires these, libdbus validates */
      g_assert (sender != NULL);
      g_assert (dbus_message_get_path (message) != NULL);
      g_assert (dbus_message_get_interface (message) != NULL);
      g_assert (dbus_message_get_member (message) != NULL);
      
      tri = tristring_from_message (message);

      if (manager->proxy_lists)
	{
	  DBusGProxyList *owner_list;
	  owner_list = g_hash_table_lookup (manager->proxy_lists, tri);
	  if (owner_list)
	    full_list = g_slist_copy (owner_list->proxies);
	  else
	    full_list = NULL;
	}
      else
	full_list = NULL;

      g_free (tri);

      if (manager->owner_names)
	{
	  owned_names = g_hash_table_lookup (manager->owner_names, sender);
	  for (tmp = owned_names; tmp; tmp = tmp->next)
	    {
	      DBusGProxyList *owner_list;
	      DBusGProxyNameOwnerInfo *nameinfo;

	      nameinfo = tmp->data;
	      g_assert (nameinfo->refcount > 0);
	      tri = tristring_alloc_from_strings (0, nameinfo->name,
						  dbus_message_get_path (message),
						  dbus_message_get_interface (message));

	      owner_list = g_hash_table_lookup (manager->proxy_lists, tri);
	      if (owner_list != NULL)
		full_list = g_slist_concat (full_list, g_slist_copy (owner_list->proxies));
	      g_free (tri);
	    }
	}

#if 0
      g_print ("proxy got %s,%s,%s = list %p\n",
               tri,
               tri + strlen (tri) + 1,
               tri + strlen (tri) + 1 + strlen (tri + strlen (tri) + 1) + 1,
               list);
#endif
      
      /* Emit the signal */
      
      g_slist_foreach (full_list, (GFunc) g_object_ref, NULL);
      
      for (tmp = full_list; tmp; tmp = tmp->next)
	{
	  DBusGProxy *proxy;
	  
	  proxy = DBUS_G_PROXY (tmp->data);
	  
	  UNLOCK_MANAGER (manager);
	  dbus_g_proxy_emit_remote_signal (proxy, message);
	  g_object_unref (G_OBJECT (proxy));
	  LOCK_MANAGER (manager);
	}
      g_slist_free (full_list);
    }

  UNLOCK_MANAGER (manager);
  dbus_g_proxy_manager_unref (manager);
  
  /* "Handling" signals doesn't make sense, they are for everyone
   * who cares
   */
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}



/*      ---------- DBusGProxy --------------   */

#define DBUS_G_PROXY_DESTROYED(proxy)  (DBUS_G_PROXY (proxy)->manager == NULL)

static void
marshal_dbus_message_to_g_marshaller (GClosure     *closure,
                                      GValue       *return_value,
                                      guint         n_param_values,
                                      const GValue *param_values,
                                      gpointer      invocation_hint,
                                      gpointer      marshal_data);
enum
{
  PROP_0,
  PROP_NAME,
  PROP_PATH,
  PROP_INTERFACE
};

enum
{
  DESTROY,
  RECEIVED,
  LAST_SIGNAL
};

static void *parent_class;
static guint signals[LAST_SIGNAL] = { 0 };

static void
dbus_g_proxy_init (DBusGProxy *proxy)
{
  g_datalist_init (&proxy->signal_signatures);
  proxy->pending_calls = g_hash_table_new_full (NULL, NULL, NULL,
						(GDestroyNotify) dbus_pending_call_unref);
}

static GObject *
dbus_g_proxy_constructor (GType                  type,
			  guint                  n_construct_properties,
			  GObjectConstructParam *construct_properties)
{
  DBusGProxy *proxy;
  DBusGProxyClass *klass;
  GObjectClass *parent_class;  

  klass = DBUS_G_PROXY_CLASS (g_type_class_peek (DBUS_TYPE_G_PROXY));

  parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));

  proxy = DBUS_G_PROXY (parent_class->constructor (type, n_construct_properties,
						    construct_properties));

  proxy->for_owner = (proxy->name[0] == ':');
  proxy->name_call = 0;
  proxy->associated = FALSE;

  return G_OBJECT (proxy);
}

static void
dbus_g_proxy_class_init (DBusGProxyClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  
  parent_class = g_type_class_peek_parent (klass);

  object_class->set_property = dbus_g_proxy_set_property;
  object_class->get_property = dbus_g_proxy_get_property;

  g_object_class_install_property (object_class,
				   PROP_NAME,
				   g_param_spec_string ("name",
							"name",
							"name",
							NULL,
							G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
				   PROP_PATH,
				   g_param_spec_string ("path",
							"path",
							"path",
							NULL,
							G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class,
				   PROP_INTERFACE,
				   g_param_spec_string ("interface",
							"interface",
							"interface",
							NULL,
							G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  
  object_class->finalize = dbus_g_proxy_finalize;
  object_class->dispose = dbus_g_proxy_dispose;
  object_class->constructor = dbus_g_proxy_constructor;
  
  signals[DESTROY] =
    g_signal_new ("destroy",
		  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_CLEANUP | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                  0,
		  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);

  signals[RECEIVED] =
    g_signal_new ("received",
		  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  marshal_dbus_message_to_g_marshaller,
                  G_TYPE_NONE, 2, DBUS_TYPE_MESSAGE, G_TYPE_POINTER);
}

static void
cancel_pending_call (gpointer key, gpointer val, gpointer data)
{
  DBusGProxyCall *call = key;
  DBusGProxy *proxy = data;

  dbus_g_proxy_cancel_call (proxy, call);
}

static void
dbus_g_proxy_dispose (GObject *object)
{
  DBusGProxy *proxy;

  proxy = DBUS_G_PROXY (object);

  /* Cancel outgoing pending calls */
  g_hash_table_foreach (proxy->pending_calls, cancel_pending_call, proxy);
  g_hash_table_destroy (proxy->pending_calls);

  if (proxy->manager && proxy != proxy->manager->bus_proxy)
    {
      dbus_g_proxy_manager_unregister (proxy->manager, proxy);
      dbus_g_proxy_manager_unref (proxy->manager);
    }
  proxy->manager = NULL;
  
  g_datalist_clear (&proxy->signal_signatures);
  
  g_signal_emit (object, signals[DESTROY], 0);
  
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
dbus_g_proxy_finalize (GObject *object)
{
  DBusGProxy *proxy;
  
  proxy = DBUS_G_PROXY (object);

  g_return_if_fail (DBUS_G_PROXY_DESTROYED (proxy));
  
  g_free (proxy->name);
  g_free (proxy->path);
  g_free (proxy->interface);
  
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
dbus_g_proxy_destroy (DBusGProxy *proxy)
{
  /* FIXME do we need the GTK_IN_DESTRUCTION style flag
   * from GtkObject?
   */
  g_object_run_dispose (G_OBJECT (proxy));
}

static void
dbus_g_proxy_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
  DBusGProxy *proxy = DBUS_G_PROXY (object);

  switch (prop_id)
    {
    case PROP_NAME:
      proxy->name = g_strdup (g_value_get_string (value));
      break;
    case PROP_PATH:
      proxy->path = g_strdup (g_value_get_string (value));
      break;
    case PROP_INTERFACE:
      proxy->interface = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void 
dbus_g_proxy_get_property (GObject *object,
			   guint prop_id,
			   GValue *value,
			   GParamSpec *pspec)
{
  DBusGProxy *proxy = DBUS_G_PROXY (object);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, proxy->name);
      break;
    case PROP_PATH:
      g_value_set_string (value, proxy->path);
      break;
    case PROP_INTERFACE:
      g_value_set_string (value, proxy->interface);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* this is to avoid people using g_signal_connect() directly,
 * to avoid confusion with local signal names, and because
 * of the horribly broken current setup (signals are added
 * globally to all proxies)
 */
static char*
create_signal_name (const char *interface,
                    const char *signal)
{
  GString *str;
  char *p;

  str = g_string_new (interface);

  g_string_append (str, "-");
  
  g_string_append (str, signal);

  /* GLib will silently barf on '.' in signal names */
  p = str->str;
  while (*p)
    {
      if (*p == '.')
        *p = '-';
      ++p;
    }
  
  return g_string_free (str, FALSE);
}

static void
marshal_dbus_message_to_g_marshaller (GClosure     *closure,
                                      GValue       *return_value,
                                      guint         n_param_values,
                                      const GValue *param_values,
                                      gpointer      invocation_hint,
                                      gpointer      marshal_data)
{
  /* Incoming here we have three params, the instance (Proxy), the
   * DBusMessage, the signature. We want to convert that to an
   * expanded GValue array, then call an appropriate normal GLib
   * marshaller.
   */
#define MAX_SIGNATURE_ARGS 20
  GValueArray *value_array;
  GSignalCMarshaller c_marshaller;
  DBusGProxy *proxy;
  DBusMessage *message;
  GArray *gsignature;
  const GType *types;

  g_assert (n_param_values == 3);

  proxy = g_value_get_object (&param_values[0]);
  message = g_value_get_boxed (&param_values[1]);
  gsignature = g_value_get_pointer (&param_values[2]);

  g_return_if_fail (DBUS_IS_G_PROXY (proxy));
  g_return_if_fail (message != NULL);
  g_return_if_fail (gsignature != NULL);

  c_marshaller = _dbus_gobject_lookup_marshaller (G_TYPE_NONE, gsignature->len,
						  (GType*) gsignature->data);

  g_return_if_fail (c_marshaller != NULL);
  
  {
    DBusGValueMarshalCtx context;
    context.gconnection = DBUS_G_CONNECTION_FROM_CONNECTION (proxy->manager->connection);
    context.proxy = proxy;

    types = (const GType*) gsignature->data;
    value_array = _dbus_gvalue_demarshal_message (&context, message,
						 gsignature->len, types, NULL);
  }

  if (value_array == NULL)
    return;
  
  g_value_array_prepend (value_array, NULL);
  g_value_init (g_value_array_get_nth (value_array, 0), G_TYPE_FROM_INSTANCE (proxy));
  g_value_set_instance (g_value_array_get_nth (value_array, 0), proxy);

  (* c_marshaller) (closure, return_value, value_array->n_values,
		    value_array->values, invocation_hint, marshal_data);
  
  g_value_array_free (value_array);
}

static void
dbus_g_proxy_emit_remote_signal (DBusGProxy  *proxy,
                                 DBusMessage *message)
{
  const char *interface;
  const char *signal;
  char *name;
  GQuark q;

  g_return_if_fail (!DBUS_G_PROXY_DESTROYED (proxy));

  interface = dbus_message_get_interface (message);
  signal = dbus_message_get_member (message);

  g_assert (interface != NULL);
  g_assert (signal != NULL);

  name = create_signal_name (interface, signal);

  /* If the quark isn't preexisting, there's no way there
   * are any handlers connected. We don't want to create
   * extra quarks for every possible signal.
   */
  q = g_quark_try_string (name);

  if (q != 0)
    {
      GArray *gsignature;
      GArray *msg_gsignature;
      guint i;
      
      gsignature = g_datalist_id_get_data (&proxy->signal_signatures, q);
      if (gsignature == NULL)
	goto out;
      
      msg_gsignature = _dbus_gtypes_from_arg_signature (dbus_message_get_signature (message),
						       TRUE);
      for (i = 0; i < gsignature->len; i++)
	{
	  if (msg_gsignature->len == i
	      || g_array_index (gsignature, GType, i) != g_array_index (msg_gsignature, GType, i))
	    goto mismatch;
	}
      if (msg_gsignature->len != i)
	goto mismatch;
      
      g_signal_emit (proxy,
		     signals[RECEIVED],
		     q,
		     message,
		     msg_gsignature);
    }

 out:
  g_free (name);
  return;
 mismatch:
#if 0
  /* Don't spew on remote errors */
  g_warning ("Unexpected message signature '%s' for signal '%s'\n",
	     dbus_message_get_signature (message),
	     name);
#endif
  goto out;
}

typedef struct
{
  DBusGProxy *proxy;
  guint call_id;
  DBusGProxyCallNotify func;
  void *data;
  GDestroyNotify free_data_func;
} GPendingNotifyClosure;

static void
d_pending_call_notify (DBusPendingCall *dcall,
                       void            *data)
{
  GPendingNotifyClosure *closure = data;

  (* closure->func) (closure->proxy, DBUS_G_PROXY_ID_TO_CALL (closure->call_id), closure->data);
}

static void
d_pending_call_free (void *data)
{
  GPendingNotifyClosure *closure = data;
  
  if (closure->free_data_func)
    (* closure->free_data_func) (closure->data);

  g_free (closure);
}
  
#define DBUS_G_VALUE_ARRAY_COLLECT_ALL(VALARRAY, FIRST_ARG_TYPE, ARGS) \
do { \
  GType valtype; \
  int i = 0; \
  VALARRAY = g_value_array_new (6); \
  valtype = FIRST_ARG_TYPE; \
  while (valtype != G_TYPE_INVALID) \
    { \
      const char *collect_err; \
      GValue *val; \
      g_value_array_append (VALARRAY, NULL); \
      val = g_value_array_get_nth (VALARRAY, i); \
      g_value_init (val, valtype); \
      collect_err = NULL; \
      G_VALUE_COLLECT (val, ARGS, G_VALUE_NOCOPY_CONTENTS, &collect_err); \
      valtype = va_arg (ARGS, GType); \
      i++; \
    } \
} while (0)

DBusGProxyCall *
manager_begin_bus_call (DBusGProxyManager    *manager,
			const char           *method,
			DBusGProxyCallNotify  notify,
			gpointer              user_data,
			GDestroyNotify        destroy,
			GType                 first_arg_type,
			...)
{
  DBusGProxyCall *call;
  va_list args;
  GValueArray *arg_values;
  
  va_start (args, first_arg_type);

  if (!manager->bus_proxy)
    {
      manager->bus_proxy = g_object_new (DBUS_TYPE_G_PROXY,
					 "name", DBUS_SERVICE_DBUS,
					 "path", DBUS_PATH_DBUS,
					 "interface", DBUS_INTERFACE_DBUS,
					 NULL);
      manager->bus_proxy->manager = manager;
    }

  DBUS_G_VALUE_ARRAY_COLLECT_ALL (arg_values, first_arg_type, args);
  
  call = DBUS_G_PROXY_ID_TO_CALL (dbus_g_proxy_begin_call_internal (manager->bus_proxy, method, notify, user_data, destroy, arg_values));

  g_value_array_free (arg_values);

  va_end (args);

  return call;
}

/** @} End of DBusGLibInternals */

/** @addtogroup DBusGLib
 * @{
 */

/**
 * Standard GObject get_type() function for DBusGProxy.
 *
 * @returns type ID for DBusGProxy class
 */
GType
dbus_g_proxy_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      static const GTypeInfo object_info =
        {
          sizeof (DBusGProxyClass),
          (GBaseInitFunc) NULL,
          (GBaseFinalizeFunc) NULL,
          (GClassInitFunc) dbus_g_proxy_class_init,
          NULL,           /* class_finalize */
          NULL,           /* class_data */
          sizeof (DBusGProxy),
          0,              /* n_preallocs */
          (GInstanceInitFunc) dbus_g_proxy_init,
        };
      
      object_type = g_type_register_static (G_TYPE_OBJECT,
                                            "DBusGProxy",
                                            &object_info, 0);
    }
  
  return object_type;
}

static DBusGProxy*
dbus_g_proxy_new (DBusGConnection *connection,
                  const char      *name,
                  const char      *path_name,
                  const char      *interface_name)
{
  DBusGProxy *proxy;

  g_assert (connection != NULL);
  
  proxy = g_object_new (DBUS_TYPE_G_PROXY, "name", name, "path", path_name, "interface", interface_name, NULL);

  /* These should all be construct-only mandatory properties,
   * for now we just don't let people use g_object_new().
   */
  
  proxy->manager = dbus_g_proxy_manager_get (DBUS_CONNECTION_FROM_G_CONNECTION (connection));

  dbus_g_proxy_manager_register (proxy->manager, proxy);
  
  return proxy;
}

/**
 * Creates a new proxy for a remote interface exported by a connection
 * on a message bus. Method calls and signal connections over this
 * proxy will go to the name owner; the name's owner is expected to
 * support the given interface name. THE NAME OWNER MAY CHANGE OVER
 * TIME, for example between two different method calls, unless the
 * name is a unique name. If you need a fixed owner, you need to
 * request the current owner and bind a proxy to its unique name
 * rather than to the generic name; see
 * dbus_g_proxy_new_for_name_owner().
 *
 * A name-associated proxy only makes sense with a message bus, not
 * for app-to-app direct dbus connections.
 *
 * This proxy will only emit the "destroy" signal if the
 * #DBusConnection is disconnected, the proxy has no remaining
 * references, or the name is a unique name and its owner
 * disappears. If a well-known name changes owner, the proxy will
 * still be alive.
 *
 * @param connection the connection to the remote bus
 * @param name any name on the message bus
 * @param path_name name of the object instance to call methods on
 * @param interface_name name of the interface to call methods on
 * @returns new proxy object
 */
DBusGProxy*
dbus_g_proxy_new_for_name (DBusGConnection *connection,
                           const char      *name,
                           const char      *path_name,
                           const char      *interface_name)
{
  g_return_val_if_fail (connection != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (path_name != NULL, NULL);
  g_return_val_if_fail (interface_name != NULL, NULL);

  return dbus_g_proxy_new (connection, name,
			   path_name, interface_name);
}

/**
 * Similar to dbus_g_proxy_new_for_name(), but makes a round-trip
 * request to the message bus to get the current name owner, then
 * binds the proxy to the unique name of the current owner, rather
 * than to the well-known name. As a result, the name owner will
 * not change over time, and the proxy will emit the "destroy" signal
 * when the owner disappears from the message bus.
 *
 * An example of the difference between dbus_g_proxy_new_for_name()
 * and dbus_g_proxy_new_for_name_owner(): if you provide the well-known name
 * "org.freedesktop.Database" dbus_g_proxy_new_for_name() remains bound
 * to that name as it changes owner. dbus_g_proxy_new_for_name_owner()
 * will fail if the name has no owner. If the name has an owner,
 * dbus_g_proxy_new_for_name_owner() will bind to the unique name
 * of that owner rather than the generic name.
 * 
 * @param connection the connection to the remote bus
 * @param name any name on the message bus
 * @param path_name name of the object inside the service to call methods on
 * @param interface_name name of the interface to call methods on
 * @param error return location for an error
 * @returns new proxy object, or #NULL on error
 */
DBusGProxy*
dbus_g_proxy_new_for_name_owner (DBusGConnection          *connection,
                                 const char               *name,
                                 const char               *path_name,
                                 const char               *interface_name,
                                 GError                  **error)
{
  DBusGProxy *proxy;
  char *unique_name;

  g_return_val_if_fail (connection != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (path_name != NULL, NULL);
  g_return_val_if_fail (interface_name != NULL, NULL);

  if (!(unique_name = get_name_owner (DBUS_CONNECTION_FROM_G_CONNECTION (connection), name, error)))
    return NULL;

  proxy = dbus_g_proxy_new (connection, unique_name,
			    path_name, interface_name);
  g_free (unique_name);
  return proxy;
}

/**
 * Creates a proxy using an existing proxy as a template, substituting
 * the specified interface and path.  Either or both may be NULL.
 *
 * @param proxy the proxy to use as a template
 * @param path of the object inside the peer to call methods on
 * @param interface name of the interface to call methods on
 * @returns new proxy object
 * 
 */
DBusGProxy*
dbus_g_proxy_new_from_proxy (DBusGProxy        *proxy,
			     const char        *interface,
			     const char        *path)
{
  g_return_val_if_fail (proxy != NULL, NULL);

  if (interface == NULL)
    interface = proxy->interface;
  if (path == NULL)
    path = proxy->path;

  return dbus_g_proxy_new (DBUS_G_CONNECTION_FROM_CONNECTION (proxy->manager->connection),
			   proxy->name,
			   path, interface);
}

/**
 * Creates a proxy for an object in peer application (one
 * we're directly connected to). That is, this function is
 * intended for use when there's no message bus involved,
 * we're doing a simple 1-to-1 communication between two
 * applications.
 *
 *
 * @param connection the connection to the peer
 * @param path_name name of the object inside the peer to call methods on
 * @param interface_name name of the interface to call methods on
 * @returns new proxy object
 * 
 */
DBusGProxy*
dbus_g_proxy_new_for_peer (DBusGConnection          *connection,
                           const char               *path_name,
                           const char               *interface_name)
{
  DBusGProxy *proxy;
  
  g_return_val_if_fail (connection != NULL, NULL);
  g_return_val_if_fail (path_name != NULL, NULL);
  g_return_val_if_fail (interface_name != NULL, NULL);

  proxy = dbus_g_proxy_new (connection, NULL,
                            path_name, interface_name);

  return proxy;
}

/**
 * Gets the bus name a proxy is bound to (may be #NULL in some cases).
 * If you created the proxy with dbus_g_proxy_new_for_name(), then
 * the name you passed to that will be returned.
 * If you created it with dbus_g_proxy_new_for_name_owner(), then the
 * unique connection name will be returned. If you created it
 * with dbus_g_proxy_new_for_peer() then #NULL will be returned.
 *
 * @param proxy the proxy
 * @returns the bus name the proxy sends messages to
 */
const char*
dbus_g_proxy_get_bus_name (DBusGProxy        *proxy)
{
  g_return_val_if_fail (DBUS_IS_G_PROXY (proxy), NULL);
  g_return_val_if_fail (!DBUS_G_PROXY_DESTROYED (proxy), NULL);

  return proxy->name;
}

/**
 * Gets the object interface proxy is bound to (may be #NULL in some cases).
 *
 * @param proxy the proxy
 * @returns an object interface 
 */
const char*
dbus_g_proxy_get_interface (DBusGProxy        *proxy)
{
  g_return_val_if_fail (DBUS_IS_G_PROXY (proxy), NULL);
  g_return_val_if_fail (!DBUS_G_PROXY_DESTROYED (proxy), NULL);

  return proxy->interface;
}

/**
 * Sets the object interface proxy is bound to
 *
 * @param proxy the proxy
 * @param interface_name an object interface 
 */
void
dbus_g_proxy_set_interface (DBusGProxy        *proxy,
			    const char        *interface_name)
{
  /* FIXME - need to unregister when we switch interface for now
   * later should support idea of unset interface
   */
  dbus_g_proxy_manager_unregister (proxy->manager, proxy);
  g_free (proxy->interface);
  proxy->interface = g_strdup (interface_name);
  dbus_g_proxy_manager_register (proxy->manager, proxy);
}

/**
 * Gets the path this proxy is bound to
 *
 * @param proxy the proxy
 * @returns an object path
 */
const char*
dbus_g_proxy_get_path (DBusGProxy        *proxy)
{
  g_return_val_if_fail (DBUS_IS_G_PROXY (proxy), NULL);
  g_return_val_if_fail (!DBUS_G_PROXY_DESTROYED (proxy), NULL);

  return proxy->path;
}

static DBusMessage *
dbus_g_proxy_marshal_args_to_message (DBusGProxy  *proxy,
				      const char  *method,
				      GValueArray *args)
{
  DBusMessage *message;
  DBusMessageIter msgiter;
  guint i;

  message = dbus_message_new_method_call (proxy->name,
                                          proxy->path,
                                          proxy->interface,
                                          method);
  if (message == NULL)
    goto oom;

  dbus_message_iter_init_append (message, &msgiter);
  for (i = 0; i < args->n_values; i++)
    {
      GValue *gvalue;

      gvalue = g_value_array_get_nth (args, i);

      if (!_dbus_gvalue_marshal (&msgiter, gvalue))
	g_assert_not_reached ();
    }
  return message;
 oom:
  return NULL;
}

static guint
dbus_g_proxy_begin_call_internal (DBusGProxy          *proxy,
				  const char          *method,
				  DBusGProxyCallNotify notify,
				  gpointer             user_data,
				  GDestroyNotify       destroy,
				  GValueArray         *args)
{
  DBusMessage *message;
  DBusPendingCall *pending;
  GPendingNotifyClosure *closure;
  guint call_id;

  pending = NULL;

  message = dbus_g_proxy_marshal_args_to_message (proxy, method, args);
  if (!message)
    goto oom;
  
  if (!dbus_connection_send_with_reply (proxy->manager->connection,
                                        message,
                                        &pending,
                                        -1))
    goto oom;
  dbus_message_unref (message);
  g_assert (pending != NULL);

  call_id = ++proxy->call_id_counter;

  if (notify != NULL)
    {
      closure = g_new (GPendingNotifyClosure, 1);
      closure->proxy = proxy; /* No need to ref as the lifecycle is tied to proxy */
      closure->call_id = call_id;
      closure->func = notify;
      closure->data = user_data;
      closure->free_data_func = destroy;
      dbus_pending_call_set_notify (pending, d_pending_call_notify,
				    closure,
				    d_pending_call_free);
    }

  g_hash_table_insert (proxy->pending_calls, GUINT_TO_POINTER (call_id), pending);
  
  return call_id;
 oom:
  g_error ("Out of memory");
  return 0;
}

static gboolean
dbus_g_proxy_end_call_internal (DBusGProxy        *proxy,
				guint              call_id,
				GError           **error,
				GType              first_arg_type,
				va_list            args)
{
  DBusMessage *reply;
  DBusMessageIter msgiter;
  DBusError derror;
  va_list args_unwind;
  guint over;
  int n_retvals_processed;
  gboolean ret;
  GType valtype;
  DBusPendingCall *pending;

  reply = NULL;
  ret = FALSE;
  n_retvals_processed = 0;
  over = 0;

  pending = g_hash_table_lookup (proxy->pending_calls, GUINT_TO_POINTER (call_id));
  
  dbus_pending_call_block (pending);
  reply = dbus_pending_call_steal_reply (pending);

  g_assert (reply != NULL);

  dbus_error_init (&derror);

  switch (dbus_message_get_type (reply))
    {
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:

      dbus_message_iter_init (reply, &msgiter);
      valtype = first_arg_type;
      while (valtype != G_TYPE_INVALID)
	{
	  int arg_type;
	  gpointer return_storage;
	  GValue gvalue = { 0, };
	  DBusGValueMarshalCtx context;

	  context.gconnection = DBUS_G_CONNECTION_FROM_CONNECTION (proxy->manager->connection);
	  context.proxy = proxy;

	  arg_type = dbus_message_iter_get_arg_type (&msgiter);
	  if (arg_type == DBUS_TYPE_INVALID)
	    {
	      g_set_error (error, DBUS_GERROR,
			   DBUS_GERROR_INVALID_ARGS,
			   _("Too few arguments in reply"));
	      goto out;
	    }

	  return_storage = va_arg (args, gpointer);
	  if (return_storage == NULL)
	    goto next;

	  /* We handle variants specially; the caller is expected
	   * to have already allocated storage for them.
	   */
	  if (arg_type == DBUS_TYPE_VARIANT
	      && g_type_is_a (valtype, G_TYPE_VALUE))
	    {
	      if (!_dbus_gvalue_demarshal_variant (&context, &msgiter, (GValue*) return_storage, NULL))
		{
		  g_set_error (error,
			       DBUS_GERROR,
			       DBUS_GERROR_INVALID_ARGS,
			       _("Couldn't convert argument, expected \"%s\""),
			       g_type_name (valtype));
		  goto out;
		}
	    }
	  else
	    {
	      g_value_init (&gvalue, valtype);

	      if (!_dbus_gvalue_demarshal (&context, &msgiter, &gvalue, error))
		goto out;

	      /* Anything that can be demarshaled must be storable */
	      if (!_dbus_gvalue_store (&gvalue, (gpointer*) return_storage))
		g_assert_not_reached ();
	      /* Ownership of the value passes to the client, don't unset */
	    }
	  
	next:
	  n_retvals_processed++;
	  dbus_message_iter_next (&msgiter);
	  valtype = va_arg (args, GType);
	}
      
      while (dbus_message_iter_get_arg_type (&msgiter) != DBUS_TYPE_INVALID)
	{
	  over++;
	  dbus_message_iter_next (&msgiter);
	}

      if (over > 0)
	{
	  g_set_error (error, DBUS_GERROR,
		       DBUS_GERROR_INVALID_ARGS,
		       _("Too many arguments in reply; expected %d, got %d"),
		       n_retvals_processed, over);
	  goto out;
	}
      break;
    case DBUS_MESSAGE_TYPE_ERROR:
      dbus_set_error_from_message (&derror, reply);
      dbus_set_g_error (error, &derror);
      dbus_error_free (&derror);
      goto out;
      break;
    default:
      dbus_set_error (&derror, DBUS_ERROR_FAILED,
                      "Reply was neither a method return nor an exception");
      dbus_set_g_error (error, &derror);
      dbus_error_free (&derror);
      goto out;
      break;
    }

  ret = TRUE;
 out:
  va_end (args);

  if (ret == FALSE)
    {
      int i;
      for (i = 0; i < n_retvals_processed; i++)
	{
	  gpointer retval;

	  retval = va_arg (args_unwind, gpointer);

	  g_free (retval);
	}
    }
  va_end (args_unwind);

  g_hash_table_remove (proxy->pending_calls, GUINT_TO_POINTER (call_id));

  if (reply)
    dbus_message_unref (reply);
  return ret;
}

/**
 * Asynchronously invokes a method on a remote interface. The method
 * call will not be sent over the wire until the application returns
 * to the main loop, or blocks in dbus_connection_flush() to write out
 * pending data.  The call will be completed after a timeout, or when
 * a reply is received.  When the call returns, the callback specified
 * will be invoked; you can then collect the results of the call
 * (which may be an error, or a reply), use dbus_g_proxy_end_call().
 *
 * @todo this particular function shouldn't die on out of memory,
 * since you should be able to do a call with large arguments.
 * 
 * @param proxy a proxy for a remote interface
 * @param method the name of the method to invoke
 * @param notify callback to be invoked when method returns
 * @param user_data user data passed to callback
 * @param destroy function called to destroy user_data
 * @param first_arg_type type of the first argument
 *
 * @returns call identifier
 *  */
DBusGProxyCall *
dbus_g_proxy_begin_call (DBusGProxy          *proxy,
			 const char          *method,
			 DBusGProxyCallNotify notify,
			 gpointer             user_data,
			 GDestroyNotify       destroy,
			 GType                first_arg_type,
			 ...)
{
  guint call_id;
  va_list args;
  GValueArray *arg_values;
  
  g_return_val_if_fail (DBUS_IS_G_PROXY (proxy), FALSE);
  g_return_val_if_fail (!DBUS_G_PROXY_DESTROYED (proxy), FALSE);

  va_start (args, first_arg_type);

  DBUS_G_VALUE_ARRAY_COLLECT_ALL (arg_values, first_arg_type, args);
  
  call_id = dbus_g_proxy_begin_call_internal (proxy, method, notify, user_data, destroy, arg_values);

  g_value_array_free (arg_values);

  va_end (args);

  return DBUS_G_PROXY_ID_TO_CALL (call_id);
}

/**
 * Collects the results of a method call. The method call was normally
 * initiated with dbus_g_proxy_end_call(). You may use this function
 * outside of the callback given to dbus_g_proxy_begin_call; in that
 * case this function will block if the results haven't yet been
 * received.
 *
 * If the call results in an error, the error is set as normal for
 * GError and the function returns #FALSE.
 *
 * Otherwise, the "out" parameters and return value of the
 * method are stored in the provided varargs list.
 * The list should be terminated with G_TYPE_INVALID.
 *
 * @param proxy a proxy for a remote interface
 * @param call the pending call ID from dbus_g_proxy_begin_call()
 * @param error return location for an error
 * @param first_arg_type type of first "out" argument
 * @returns #FALSE if an error is set
 */
gboolean
dbus_g_proxy_end_call (DBusGProxy          *proxy,
                       DBusGProxyCall      *call,
                       GError             **error,
                       GType                first_arg_type,
                       ...)
{
  gboolean ret;
  va_list args;

  va_start (args, first_arg_type);

  ret = dbus_g_proxy_end_call_internal (proxy, GPOINTER_TO_UINT (call), error, first_arg_type, args);

  va_end (args);
  
  return ret;
}

/**
 * Function for synchronously invoking a method and receiving reply
 * values.  This function is equivalent to dbus_g_proxy_begin_call
 * followed by dbus_g_proxy_end_call.  All of the input arguments are
 * specified first, followed by G_TYPE_INVALID, followed by all of the
 * output values, followed by a second G_TYPE_INVALID.  Note that  
 * this means you must always specify G_TYPE_INVALID twice.
 *
 * @param proxy a proxy for a remote interface
 * @param method method to invoke
 * @param error return location for an error
 * @param first_arg_type type of first "in" argument
 * @returns #FALSE if an error is set, TRUE otherwise
 */
gboolean
dbus_g_proxy_call (DBusGProxy        *proxy,
		   const char        *method,
		   GError           **error,
		   GType              first_arg_type,
		   ...)
{
  gboolean ret;
  guint call_id;
  va_list args;
  GValueArray *in_args;

  g_return_val_if_fail (DBUS_IS_G_PROXY (proxy), FALSE);
  g_return_val_if_fail (!DBUS_G_PROXY_DESTROYED (proxy), FALSE);

  va_start (args, first_arg_type);

  DBUS_G_VALUE_ARRAY_COLLECT_ALL (in_args, first_arg_type, args);

  call_id = dbus_g_proxy_begin_call_internal (proxy, method, NULL, NULL, NULL, in_args);

  g_value_array_free (in_args);

  first_arg_type = va_arg (args, GType);
  ret = dbus_g_proxy_end_call_internal (proxy, call_id, error, first_arg_type, args);

  va_end (args);

  return ret;
}

/**
 * Sends a method call message as with dbus_g_proxy_begin_call(), but
 * does not ask for a reply or allow you to receive one.
 *
 * @todo this particular function shouldn't die on out of memory,
 * since you should be able to do a call with large arguments.
 * 
 * @param proxy a proxy for a remote interface
 * @param method the name of the method to invoke
 * @param first_arg_type type of the first argument
 */
void
dbus_g_proxy_call_no_reply (DBusGProxy               *proxy,
			    const char               *method,
			    GType                     first_arg_type,
			    ...)
{
  DBusMessage *message;
  va_list args;
  GValueArray *in_args;
  
  g_return_if_fail (DBUS_IS_G_PROXY (proxy));
  g_return_if_fail (!DBUS_G_PROXY_DESTROYED (proxy));

  va_start (args, first_arg_type);
  DBUS_G_VALUE_ARRAY_COLLECT_ALL (in_args, first_arg_type, args);

  message = dbus_g_proxy_marshal_args_to_message (proxy, method, in_args);

  g_value_array_free (in_args);
  va_end (args);

  if (!message)
    goto oom;

  dbus_message_set_no_reply (message, TRUE);

  if (!dbus_connection_send (proxy->manager->connection,
                             message,
                             NULL))
    goto oom;
  dbus_message_unref (message);
  return;
  
 oom:
  g_error ("Out of memory");
}

/**
 * Cancels a pending method call. The method call was normally
 * initiated with dbus_g_proxy_begin_call().  This function
 * may not be used on pending calls that have already been
 * ended with dbus_g_proxy_end_call.
 *
 * @param proxy a proxy for a remote interface
 * @param call the pending call ID from dbus_g_proxy_begin_call()
 */
void
dbus_g_proxy_cancel_call (DBusGProxy        *proxy,
			  DBusGProxyCall    *call)
{
  guint call_id;
  DBusPendingCall *pending;
  
  g_return_if_fail (DBUS_IS_G_PROXY (proxy));
  g_return_if_fail (!DBUS_G_PROXY_DESTROYED (proxy));

  call_id = DBUS_G_PROXY_CALL_TO_ID (call);

  pending = g_hash_table_lookup (proxy->pending_calls, GUINT_TO_POINTER (call_id));
  g_return_if_fail (pending != NULL);

  dbus_pending_call_cancel (pending);

  g_hash_table_remove (proxy->pending_calls, GUINT_TO_POINTER (call_id));
}

/**
 * Sends a message to the interface we're proxying for.  Does not
 * block or wait for a reply. The message is only actually written out
 * when you return to the main loop or block in
 * dbus_connection_flush().
 *
 * The message is modified to be addressed to the target interface.
 * That is, a destination name field or whatever is needed will be
 * added to the message. The basic point of this function is to add
 * the necessary header fields, otherwise it's equivalent to
 * dbus_connection_send().
 *
 * This function adds a reference to the message, so the caller
 * still owns its original reference.
 * 
 * @param proxy a proxy for a remote interface
 * @param message the message to address and send
 * @param client_serial return location for message's serial, or #NULL */
void
dbus_g_proxy_send (DBusGProxy          *proxy,
                   DBusMessage         *message,
                   dbus_uint32_t       *client_serial)
{
  g_return_if_fail (DBUS_IS_G_PROXY (proxy));
  g_return_if_fail (!DBUS_G_PROXY_DESTROYED (proxy));
  
  if (proxy->name)
    {
      if (!dbus_message_set_destination (message, proxy->name))
        g_error ("Out of memory");
    }
  if (proxy->path)
    {
      if (!dbus_message_set_path (message, proxy->path))
        g_error ("Out of memory");
    }
  if (proxy->interface)
    {
      if (!dbus_message_set_interface (message, proxy->interface))
        g_error ("Out of memory");
    }
  
  if (!dbus_connection_send (proxy->manager->connection, message, client_serial))
    g_error ("Out of memory\n");
}

static void
array_free_all (gpointer array)
{
  g_array_free (array, TRUE);
}

/**
 * Specifies the argument signature of a signal;.only necessary
 * if the remote object does not support introspection.  The arguments
 * specified are the GLib types expected.
 *
 * @param proxy the proxy for a remote interface
 * @param signal_name the name of the signal
 * @param first_type the first argument type, or G_TYPE_INVALID if none
 */
void
dbus_g_proxy_add_signal  (DBusGProxy        *proxy,
                          const char        *signal_name,
			  GType              first_type,
                          ...)
{
  GQuark q;
  char *name;
  GArray *gtypesig;
  GType gtype;
  va_list args;

  g_return_if_fail (DBUS_IS_G_PROXY (proxy));
  g_return_if_fail (!DBUS_G_PROXY_DESTROYED (proxy));
  g_return_if_fail (signal_name != NULL);
  
  name = create_signal_name (proxy->interface, signal_name);
  
  q = g_quark_from_string (name);
  
  g_return_if_fail (g_datalist_id_get_data (&proxy->signal_signatures, q) == NULL);

  gtypesig = g_array_new (FALSE, TRUE, sizeof (GType));

  va_start (args, first_type);
  gtype = first_type;
  while (gtype != G_TYPE_INVALID)
    {
      g_array_append_val (gtypesig, gtype);
      gtype = va_arg (args, GType);
    }
  va_end (args);

#ifndef G_DISABLE_CHECKS
  if (_dbus_gobject_lookup_marshaller (G_TYPE_NONE, gtypesig->len, (const GType*) gtypesig->data) == NULL)
    g_warning ("No marshaller for signature of signal '%s'", signal_name);
#endif

  
  g_datalist_id_set_data_full (&proxy->signal_signatures,
                               q, gtypesig,
                               array_free_all);

  g_free (name);
}

/**
 * Connect a signal handler to a proxy for a remote interface.  When
 * the remote interface emits the specified signal, the proxy will
 * emit a corresponding GLib signal.
 *
 * @param proxy a proxy for a remote interface
 * @param signal_name the DBus signal name to listen for
 * @param handler the handler to connect
 * @param data data to pass to handler
 * @param free_data_func callback function to destroy data
 */
void
dbus_g_proxy_connect_signal (DBusGProxy             *proxy,
                             const char             *signal_name,
                             GCallback               handler,
                             void                   *data,
                             GClosureNotify          free_data_func)
{
  char *name;
  GClosure *closure;
  GQuark q;

  g_return_if_fail (DBUS_IS_G_PROXY (proxy));
  g_return_if_fail (!DBUS_G_PROXY_DESTROYED (proxy));
  g_return_if_fail (signal_name != NULL);
  g_return_if_fail (handler != NULL);
  
  name = create_signal_name (proxy->interface, signal_name);

  q = g_quark_try_string (name);

#ifndef G_DISABLE_CHECKS
  if (q == 0 || g_datalist_id_get_data (&proxy->signal_signatures, q) == NULL)
    {
      g_warning ("Must add the signal '%s' with dbus_g_proxy_add_signal() prior to connecting to it\n", name);
      g_free (name);
      return;
    }
#endif
  
  closure = g_cclosure_new (G_CALLBACK (handler), data, free_data_func);
  
  g_signal_connect_closure_by_id (G_OBJECT (proxy),
                                  signals[RECEIVED],
                                  q,
                                  closure, FALSE);
  
  g_free (name);
}

/**
 * Disconnect all signal handlers from a proxy that match the given
 * criteria.
 *
 * @param proxy a proxy for a remote interface
 * @param signal_name the DBus signal name to disconnect
 * @param handler the handler to disconnect
 * @param data the data that was registered with handler
 */
void
dbus_g_proxy_disconnect_signal (DBusGProxy             *proxy,
                                const char             *signal_name,
                                GCallback               handler,
                                void                   *data)
{
  char *name;
  GQuark q;
  
  g_return_if_fail (DBUS_IS_G_PROXY (proxy));
  g_return_if_fail (!DBUS_G_PROXY_DESTROYED (proxy));
  g_return_if_fail (signal_name != NULL);
  g_return_if_fail (handler != NULL);

  name = create_signal_name (proxy->interface, signal_name);

  q = g_quark_try_string (name);
  
  if (q != 0)
    {
      g_signal_handlers_disconnect_matched (G_OBJECT (proxy),
                                            G_SIGNAL_MATCH_DETAIL |
                                            G_SIGNAL_MATCH_FUNC   |
                                            G_SIGNAL_MATCH_DATA,
                                            signals[RECEIVED],
                                            q,
                                            NULL,
                                            G_CALLBACK (handler), data);
    }
  else
    {
      g_warning ("Attempt to disconnect from signal '%s' which is not registered\n",
                 name);
    }

  g_free (name);
}

/** @} End of DBusGLib public */

#ifdef DBUS_BUILD_TESTS

/**
 * @ingroup DBusGLibInternals
 * Unit test for GLib proxy functions
 * @returns #TRUE on success.
 */
gboolean
_dbus_g_proxy_test (void)
{
  
  
  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
