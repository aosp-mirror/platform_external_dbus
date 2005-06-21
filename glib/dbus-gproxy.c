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
#include "dbus-gvalue.h"
#include "dbus-gvalue-utils.h"
#include "dbus-gobject.h"
#include <string.h>
#include <glib/gi18n.h>
#include <gobject/gvaluecollector.h>

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

  GData *signal_signatures;   /**< D-BUS signatures for each signal */
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
static void dbus_g_proxy_finalize           (GObject         *object);
static void dbus_g_proxy_dispose            (GObject         *object);
static void dbus_g_proxy_destroy            (DBusGProxy      *proxy);
static void dbus_g_proxy_emit_remote_signal (DBusGProxy      *proxy,
                                             DBusMessage     *message);

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

  GHashTable *proxy_lists; /**< Hash used to route incoming signals
                            *   and iterate over proxies
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

      if (manager->proxy_lists)
        {
          /* can't have any proxies left since they hold
           * a reference to the proxy manager.
           */
          g_assert (g_hash_table_size (manager->proxy_lists) == 0);
          
          g_hash_table_destroy (manager->proxy_lists);
          manager->proxy_lists = NULL;
        }
      
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

static void
dbus_g_proxy_manager_register (DBusGProxyManager *manager,
                               DBusGProxy        *proxy)
{
  DBusGProxyList *list;

  LOCK_MANAGER (manager);

  if (manager->proxy_lists == NULL)
    {
      list = NULL;
      manager->proxy_lists = g_hash_table_new_full (tristring_hash,
                                                    tristring_equal,
                                                    NULL,
                                                    (GFreeFunc) g_proxy_list_free);
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

  if (list->proxies == NULL)
    {
      g_hash_table_remove (manager->proxy_lists,
                           tri);
      list = NULL;
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
      DBusGProxyList *list;

      /* dbus spec requires these, libdbus validates */
      g_assert (dbus_message_get_path (message) != NULL);
      g_assert (dbus_message_get_interface (message) != NULL);
      g_assert (dbus_message_get_member (message) != NULL);
      
      tri = tristring_from_message (message);

      if (manager->proxy_lists)
        list = g_hash_table_lookup (manager->proxy_lists, tri);
      else
        list = NULL;

#if 0
      g_print ("proxy got %s,%s,%s = list %p\n",
               tri,
               tri + strlen (tri) + 1,
               tri + strlen (tri) + 1 + strlen (tri + strlen (tri) + 1) + 1,
               list);
#endif
      
      g_free (tri);

      /* Emit the signal */
      
      if (list != NULL)
        {
          GSList *tmp;
          GSList *copy;

          copy = g_slist_copy (list->proxies);
          g_slist_foreach (copy, (GFunc) g_object_ref, NULL);
          
          tmp = copy;
          while (tmp != NULL)
            {
              DBusGProxy *proxy;

              proxy = DBUS_G_PROXY (tmp->data);

              UNLOCK_MANAGER (manager);
              dbus_g_proxy_emit_remote_signal (proxy, message);
              g_object_unref (G_OBJECT (proxy));
              LOCK_MANAGER (manager);
              
              tmp = tmp->next;
            }

          g_slist_free (copy);
        }
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
}

static void
dbus_g_proxy_class_init (DBusGProxyClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  
  parent_class = g_type_class_peek_parent (klass);
  
  object_class->finalize = dbus_g_proxy_finalize;
  object_class->dispose = dbus_g_proxy_dispose;
  
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
dbus_g_proxy_dispose (GObject *object)
{
  DBusGProxy *proxy;

  proxy = DBUS_G_PROXY (object);

  if (proxy->manager)
    {
      dbus_g_proxy_manager_unregister (proxy->manager, proxy);
      dbus_g_proxy_manager_unref (proxy->manager);
      proxy->manager = NULL;
    }
  
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
  GValue value = {0, };
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
    value_array = dbus_gvalue_demarshal_message (&context, message,
						 gsignature->len, types, NULL);
  }

  if (value_array == NULL)
    return;
  
  g_value_init (&value, G_TYPE_FROM_INSTANCE (proxy));
  g_value_set_instance (&value, proxy);
  g_value_array_prepend (value_array, &value);

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
      
      msg_gsignature = dbus_gtypes_from_arg_signature (dbus_message_get_signature (message),
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
  
  proxy = g_object_new (DBUS_TYPE_G_PROXY, NULL);

  /* These should all be construct-only mandatory properties,
   * for now we just don't let people use g_object_new().
   */
  
  proxy->manager = dbus_g_proxy_manager_get (DBUS_CONNECTION_FROM_G_CONNECTION (connection));
  
  proxy->name = g_strdup (name);
  proxy->path = g_strdup (path_name);
  proxy->interface = g_strdup (interface_name);

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
  DBusGProxy *proxy;

  g_return_val_if_fail (connection != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (path_name != NULL, NULL);
  g_return_val_if_fail (interface_name != NULL, NULL);
  
  proxy = dbus_g_proxy_new (connection, name,
                            path_name, interface_name);

  return proxy;
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
  DBusMessage *request, *reply;
  DBusError derror;
  const char *unique_name;
  
  g_return_val_if_fail (connection != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (path_name != NULL, NULL);
  g_return_val_if_fail (interface_name != NULL, NULL);

  dbus_error_init (&derror);

  proxy = NULL;
  unique_name = NULL;
  reply = NULL;

  request = dbus_message_new_method_call (DBUS_SERVICE_DBUS,
					  DBUS_PATH_DBUS,
					  DBUS_INTERFACE_DBUS,
					  "GetNameOwner");
  if (request == NULL)
    g_error ("Out of memory");
  
  if (! dbus_message_append_args (request, 
				  DBUS_TYPE_STRING, &name, 
				  DBUS_TYPE_INVALID))
    g_error ("Out of memory");

  reply =
    dbus_connection_send_with_reply_and_block (DBUS_CONNECTION_FROM_G_CONNECTION (connection),
                                               request,
                                               2000, &derror);
  if (reply == NULL)
    goto error;

  if (dbus_set_error_from_message (&derror, reply))
    goto error;

  if (! dbus_message_get_args (reply, &derror, 
			       DBUS_TYPE_STRING, &unique_name, 
			       DBUS_TYPE_INVALID))
    goto error;
      

  proxy = dbus_g_proxy_new (connection, unique_name,
                            path_name, interface_name);

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

      if (!dbus_gvalue_marshal (&msgiter, gvalue))
	g_assert_not_reached ();
    }
  return message;
 oom:
  return NULL;
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

static DBusGPendingCall *
dbus_g_proxy_begin_call_internal (DBusGProxy    *proxy,
				  const char    *method,
				  GValueArray   *args)
{
  DBusMessage *message;
  DBusPendingCall *pending;

  g_return_val_if_fail (DBUS_IS_G_PROXY (proxy), FALSE);
  g_return_val_if_fail (!DBUS_G_PROXY_DESTROYED (proxy), FALSE);

  pending = NULL;

  message = dbus_g_proxy_marshal_args_to_message (proxy, method, args);
  if (!message)
    goto oom;
  
  if (!dbus_connection_send_with_reply (proxy->manager->connection,
                                        message,
                                        &pending,
                                        -1))
    goto oom;
  g_assert (pending != NULL);

  return DBUS_G_PENDING_CALL_FROM_PENDING_CALL (pending);
 oom:
  g_error ("Out of memory");
  return NULL;
}

static gboolean
dbus_g_proxy_end_call_internal (DBusGProxy       *proxy,
				DBusGPendingCall *pending,
				GError          **error,
				GType             first_arg_type,
				va_list           args)
{
  DBusMessage *reply;
  DBusMessageIter msgiter;
  DBusError derror;
  va_list args_unwind;
  int n_retvals_processed;
  gboolean ret;
  GType valtype;

  reply = NULL;
  ret = FALSE;
  n_retvals_processed = 0;
  dbus_pending_call_block (DBUS_PENDING_CALL_FROM_G_PENDING_CALL (pending));
  reply = dbus_pending_call_steal_reply (DBUS_PENDING_CALL_FROM_G_PENDING_CALL (pending));

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
	    g_set_error (error, DBUS_GERROR,
			 DBUS_GERROR_INVALID_ARGS,
			 _("Too few arguments in reply"));

	  return_storage = va_arg (args, gpointer);
	  if (return_storage == NULL)
	    goto next;

	  /* We handle variants specially; the caller is expected
	   * to have already allocated storage for them.
	   */
	  if (arg_type == DBUS_TYPE_VARIANT
	      && g_type_is_a (valtype, G_TYPE_VALUE))
	    {
	      if (!dbus_gvalue_demarshal_variant (&context, &msgiter, (GValue*) return_storage, NULL))
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

	      /* FIXME, should use error here instead of NULL */ 
	      if (!dbus_gvalue_demarshal (&context, &msgiter, &gvalue, NULL))
		{
		  g_set_error (error,
			       DBUS_GERROR,
			       DBUS_GERROR_INVALID_ARGS,
			       _("Couldn't convert argument, expected \"%s\""),
			       g_type_name (valtype));
		  goto out;
		}

	      if (G_VALUE_TYPE (&gvalue) != valtype)
		{
		  g_set_error (error, DBUS_GERROR,
			       DBUS_GERROR_INVALID_ARGS,
			       _("Reply argument was \"%s\", expected \"%s\""),
			       g_type_name (G_VALUE_TYPE (&gvalue)),
			       g_type_name (valtype));  
		  goto out;
		}

	      /* Anything that can be demarshaled must be storable */
	      if (!dbus_gvalue_store (&gvalue, (gpointer*) return_storage))
		g_assert_not_reached ();
	      /* Ownership of the value passes to the client, don't unset */
	    }
	  
	next:
	  n_retvals_processed++;
	  dbus_message_iter_next (&msgiter);
	  valtype = va_arg (args, GType);
	}
      if (dbus_message_iter_get_arg_type (&msgiter) != DBUS_TYPE_INVALID)
	{
	  g_set_error (error, DBUS_GERROR,
		       DBUS_GERROR_INVALID_ARGS,
		       _("Too many arguments in reply"));
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

  if (pending)
    dbus_g_pending_call_unref (pending);
  if (reply)
    dbus_message_unref (reply);
  return ret;
}


/**
 * Invokes a method on a remote interface. This function does not
 * block; instead it returns an opaque #DBusPendingCall object that
 * tracks the pending call.  The method call will not be sent over the
 * wire until the application returns to the main loop, or blocks in
 * dbus_connection_flush() to write out pending data.  The call will
 * be completed after a timeout, or when a reply is received.
 * To collect the results of the call (which may be an error,
 * or a reply), use dbus_g_proxy_end_call().
 *
 * @todo this particular function shouldn't die on out of memory,
 * since you should be able to do a call with large arguments.
 * 
 * @param proxy a proxy for a remote interface
 * @param method the name of the method to invoke
 * @param first_arg_type type of the first argument
 *
 * @returns opaque pending call object
 *  */
DBusGPendingCall*
dbus_g_proxy_begin_call (DBusGProxy *proxy,
			 const char  *method,
			 GType       first_arg_type,
                        ...)
{
  DBusGPendingCall *pending;
  va_list args;
  GValueArray *arg_values;
  
  va_start (args, first_arg_type);

  DBUS_G_VALUE_ARRAY_COLLECT_ALL (arg_values, first_arg_type, args);
  
  pending = dbus_g_proxy_begin_call_internal (proxy, method, arg_values);

  g_value_array_free (arg_values);

  va_end (args);

  return pending;
}

/**
 * Collects the results of a method call. The method call was normally
 * initiated with dbus_g_proxy_end_call(). This function will block if
 * the results haven't yet been received; use
 * dbus_g_pending_call_set_notify() to be notified asynchronously that a
 * pending call has been completed. If it's completed, it will not block.
 *
 * If the call results in an error, the error is set as normal for
 * GError and the function returns #FALSE.
 *
 * Otherwise, the "out" parameters and return value of the
 * method are stored in the provided varargs list.
 * The list should be terminated with G_TYPE_INVALID.
 *
 * This function unrefs the pending call.
 *
 * @param proxy a proxy for a remote interface
 * @param pending the pending call from dbus_g_proxy_begin_call()
 * @param error return location for an error
 * @param first_arg_type type of first "out" argument
 * @returns #FALSE if an error is set
 */
gboolean
dbus_g_proxy_end_call (DBusGProxy          *proxy,
                       DBusGPendingCall    *pending,
                       GError             **error,
                       GType                first_arg_type,
                       ...)
{
  gboolean ret;
  va_list args;

  va_start (args, first_arg_type);

  ret = dbus_g_proxy_end_call_internal (proxy, pending, error, first_arg_type, args);

  va_end (args);
  
  return ret;
}

/**
 * Function for synchronously invoking a method and receiving reply
 * values.  This function is equivalent to dbus_g_proxy_begin_call
 * followed by dbus_g_proxy_end_call.  All of the input arguments are
 * specified first, followed by G_TYPE_INVALID, followed by all of the
 * output values, followed by G_TYPE_INVALID.
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
  DBusGPendingCall *pending;
  va_list args;
  GValueArray *in_args;

  va_start (args, first_arg_type);

  DBUS_G_VALUE_ARRAY_COLLECT_ALL (in_args, first_arg_type, args);

  pending = dbus_g_proxy_begin_call_internal (proxy, method, in_args);

  g_value_array_free (in_args);

  first_arg_type = va_arg (args, GType);
  ret = dbus_g_proxy_end_call_internal (proxy, pending, error, first_arg_type, args);

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

  return;
  
 oom:
  g_error ("Out of memory");
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
