/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-object-tree.c  DBusObjectTree (internals of DBusConnection)
 *
 * Copyright (C) 2003  Red Hat Inc.
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
#include "dbus-object-tree.h"
#include "dbus-connection-internal.h"
#include "dbus-internals.h"
#include "dbus-hash.h"
#include "dbus-protocol.h"
#include <string.h>
#include <stdlib.h>

/**
 * @defgroup DBusObjectTree A hierarchy of objects with container-contained relationship
 * @ingroup  DBusInternals
 * @brief DBusObjectTree is used by DBusConnection to track the object tree
 *
 * Types and functions related to DBusObjectTree. These
 * are all internal.
 *
 * @todo this is totally broken, because of the following case:
 *    /foo, /foo/bar, /foo/baz
 *  if we then receive a message to /foo/baz we need to hand it
 * to /foo/baz and /foo but not /foo/bar. So we should be
 * using a real tree structure as with GConfListeners.
 *
 * @{
 */

typedef struct DBusObjectSubtree DBusObjectSubtree;

static DBusObjectSubtree* _dbus_object_subtree_new   (const char                  *name,
                                                      const DBusObjectPathVTable  *vtable,
                                                      void                        *user_data);
static void               _dbus_object_subtree_ref   (DBusObjectSubtree           *subtree);
static void               _dbus_object_subtree_unref (DBusObjectSubtree           *subtree);

struct DBusObjectTree
{
  int                 refcount;
  DBusConnection     *connection;

  DBusObjectSubtree  *root;
};

struct DBusObjectSubtree
{
  DBusAtomic                         refcount;
  DBusObjectSubtree                 *parent;
  DBusObjectPathUnregisterFunction   unregister_function;
  DBusObjectPathMessageFunction      message_function;
  void                              *user_data;
  DBusObjectSubtree                **subtrees;
  int                                n_subtrees;
  unsigned int                       subtrees_sorted : 1;
  char                               name[1]; /**< Allocated as large as necessary */
};

DBusObjectTree*
_dbus_object_tree_new (DBusConnection *connection)
{
  DBusObjectTree *tree;

  /* the connection passed in here isn't fully constructed,
   * so don't do anything more than store a pointer to
   * it
   */

  tree = dbus_new0 (DBusObjectTree, 1);
  if (tree == NULL)
    goto oom;

  tree->refcount = 1;
  tree->connection = connection;
  tree->root = _dbus_object_subtree_new ("/", NULL, NULL);
  if (tree->root == NULL)
    goto oom;

  return tree;

 oom:
  if (tree)
    {
      dbus_free (tree);
    }

  return NULL;
}

void
_dbus_object_tree_ref (DBusObjectTree *tree)
{
  _dbus_assert (tree->refcount > 0);

  tree->refcount += 1;
}

void
_dbus_object_tree_unref (DBusObjectTree *tree)
{
  _dbus_assert (tree->refcount > 0);

  tree->refcount -= 1;

  if (tree->refcount == 0)
    {
      _dbus_object_tree_free_all_unlocked (tree);

      dbus_free (tree);
    }
}

static int
subtree_cmp (DBusObjectSubtree *subtree_a,
             DBusObjectSubtree *subtree_b)
{
  return strcmp (subtree_a->name, subtree_b->name);
}

static int
subtree_qsort_cmp (const void *a,
                   const void *b)
{
  DBusObjectSubtree **subtree_a_p = (void*) a;
  DBusObjectSubtree **subtree_b_p = (void*) b;

  return subtree_cmp (*subtree_a_p, *subtree_b_p);
}

static void
ensure_sorted (DBusObjectSubtree *subtree)
{
  if (subtree->subtrees && !subtree->subtrees_sorted)
    {
      qsort (subtree->subtrees,
             subtree->n_subtrees,
             sizeof (DBusObjectSubtree*),
             subtree_qsort_cmp);
      subtree->subtrees_sorted = TRUE;
    }
}

#define VERBOSE_FIND 0

static DBusObjectSubtree*
find_subtree_recurse (DBusObjectSubtree  *subtree,
                      const char        **path,
                      dbus_bool_t         return_deepest_match,
                      dbus_bool_t         create_if_not_found,
                      int                *index_in_parent)
{
  int i;

  _dbus_assert (!(return_deepest_match && create_if_not_found));

  if (path[0] == NULL)
    {
#if VERBOSE_FIND
      _dbus_verbose ("  path exhausted, returning %s\n",
                     subtree->name);
#endif
      return subtree;
    }

#if VERBOSE_FIND
  _dbus_verbose ("  searching children of %s for %s\n",
                 subtree->name, path[0]);
#endif
  
  ensure_sorted (subtree);

  /* FIXME we should do a binary search here instead
   * of O(n)
   */

  i = 0;
  while (i < subtree->n_subtrees)
    {
      int v;

      v = strcmp (path[0], subtree->subtrees[i]->name);

#if VERBOSE_FIND
      _dbus_verbose ("  %s cmp %s = %d\n",
                     path[0], subtree->subtrees[i]->name,
                     v);
#endif
      
      if (v == 0)
        {
          if (index_in_parent)
            {
#if VERBOSE_FIND
              _dbus_verbose ("  storing parent index %d\n", i);
#endif
              *index_in_parent = i;
            }

          if (return_deepest_match)
            {
              DBusObjectSubtree *next;

              next = find_subtree_recurse (subtree->subtrees[i],
                                           &path[1], return_deepest_match,
                                           create_if_not_found, index_in_parent);
              if (next == NULL)
                {
#if VERBOSE_FIND
                  _dbus_verbose ("  no deeper match found, returning %s\n",
                                 subtree->name);
#endif
                  return subtree;
                }
              else
                return next;
            }
          else
            return find_subtree_recurse (subtree->subtrees[i],
                                         &path[1], return_deepest_match,
                                         create_if_not_found, index_in_parent);
        }
      else if (v < 0)
        {
          goto not_found;
        }

      ++i;
    }

 not_found:
#if VERBOSE_FIND
  _dbus_verbose ("  no match found, current tree %s, create_if_not_found = %d\n",
                 subtree->name, create_if_not_found);
#endif
  
  if (create_if_not_found)
    {
      DBusObjectSubtree* child;
      DBusObjectSubtree **new_subtrees;
      int new_n_subtrees;

#if VERBOSE_FIND
      _dbus_verbose ("  creating subtree %s\n",
                     path[0]);
#endif
      
      child = _dbus_object_subtree_new (path[0],
                                        NULL, NULL);
      if (child == NULL)
        return NULL;

      /* FIXME we should do the "double alloc each time" standard thing */
      new_n_subtrees = subtree->n_subtrees + 1;
      new_subtrees = dbus_realloc (subtree->subtrees,
                                   new_n_subtrees * sizeof (DBusObjectSubtree*));
      if (new_subtrees == NULL)
        {
          child->unregister_function = NULL;
          child->message_function = NULL;
          _dbus_object_subtree_unref (child);
          return FALSE;
        }

      new_subtrees[subtree->n_subtrees] = child;
      if (index_in_parent)
        *index_in_parent = subtree->n_subtrees;
      subtree->subtrees_sorted = FALSE;
      subtree->n_subtrees = new_n_subtrees;
      subtree->subtrees = new_subtrees;

      child->parent = subtree;

      return find_subtree_recurse (child,
                                   &path[1], return_deepest_match,
                                   create_if_not_found, index_in_parent);
    }
  else
    return return_deepest_match ? subtree : NULL;
}

static DBusObjectSubtree*
find_subtree (DBusObjectTree *tree,
              const char    **path,
              int            *index_in_parent)
{
  DBusObjectSubtree *subtree;

#if VERBOSE_FIND
  _dbus_verbose ("Looking for exact registered subtree\n");
#endif
  
  subtree = find_subtree_recurse (tree->root, path, FALSE, FALSE, index_in_parent);

  if (subtree && subtree->message_function == NULL)
    return NULL;
  else
    return subtree;
}

static DBusObjectSubtree*
find_handler (DBusObjectTree *tree,
              const char    **path)
{
#if VERBOSE_FIND
  _dbus_verbose ("Looking for deepest handler\n");
#endif
  return find_subtree_recurse (tree->root, path, TRUE, FALSE, NULL);
}

static DBusObjectSubtree*
ensure_subtree (DBusObjectTree *tree,
                const char    **path)
{
#if VERBOSE_FIND
  _dbus_verbose ("Ensuring subtree\n");
#endif
  return find_subtree_recurse (tree->root, path, FALSE, TRUE, NULL);
}

/**
 * Registers a new subtree in the global object tree.
 *
 * @param tree the global object tree
 * @param path NULL-terminated array of path elements giving path to subtree
 * @param vtable the vtable used to traverse this subtree
 * @param user_data user data to pass to methods in the vtable
 * @returns #FALSE if not enough memory
 */
dbus_bool_t
_dbus_object_tree_register (DBusObjectTree              *tree,
                            const char                 **path,
                            const DBusObjectPathVTable  *vtable,
                            void                        *user_data)
{
  DBusObjectSubtree  *subtree;

  _dbus_assert (tree != NULL);
  _dbus_assert (vtable->message_function != NULL);
  _dbus_assert (path != NULL);

  subtree = ensure_subtree (tree, path);
  if (subtree == NULL)
    return FALSE;

  if (subtree->message_function != NULL)
    {
      _dbus_warn ("A handler is already registered for the path starting with path[0] = \"%s\"\n",
                  path[0] ? path[0] : "null");
      return FALSE;
    }

  subtree->message_function = vtable->message_function;
  subtree->unregister_function = vtable->unregister_function;
  subtree->user_data = user_data;

  return TRUE;
}

/**
 * Unregisters an object subtree that was registered with the
 * same path.
 *
 * @param tree the global object tree
 * @param path path to the subtree (same as the one passed to _dbus_object_tree_register())
 */
void
_dbus_object_tree_unregister_and_unlock (DBusObjectTree          *tree,
                                         const char             **path)
{
  int i;
  DBusObjectSubtree *subtree;
  DBusObjectPathUnregisterFunction unregister_function;
  void *user_data;
  DBusConnection *connection;

  _dbus_assert (path != NULL);

  subtree = find_subtree (tree, path, &i);

  if (subtree == NULL)
    {
      _dbus_warn ("Attempted to unregister path (path[0] = %s path[1] = %s) which isn't registered\n",
                  path[0] ? path[0] : "null",
                  path[1] ? path[1] : "null");
      return;
    }

  _dbus_assert (subtree->parent == NULL ||
                (i >= 0 && subtree->parent->subtrees[i] == subtree));

  subtree->message_function = NULL;

  unregister_function = subtree->unregister_function;
  user_data = subtree->user_data;

  subtree->unregister_function = NULL;
  subtree->user_data = NULL;

  /* If we have no subtrees of our own, remove from
   * our parent (FIXME could also be more aggressive
   * and remove our parent if it becomes empty)
   */
  if (subtree->parent && subtree->n_subtrees == 0)
    {
      /* assumes a 0-byte memmove is OK */
      memmove (&subtree->parent->subtrees[i],
               &subtree->parent->subtrees[i+1],
               (subtree->parent->n_subtrees - i - 1) *
               sizeof (subtree->parent->subtrees[0]));
      subtree->parent->n_subtrees -= 1;

      subtree->parent = NULL;

      _dbus_object_subtree_unref (subtree);
    }
  subtree = NULL;

  connection = tree->connection;

  /* Unlock and call application code */
#ifdef DBUS_BUILD_TESTS
  if (connection)
#endif
    {
      _dbus_connection_ref_unlocked (connection);
      _dbus_connection_unlock (connection);
    }

  if (unregister_function)
    (* unregister_function) (connection, user_data);

#ifdef DBUS_BUILD_TESTS
  if (connection)
#endif
    dbus_connection_unref (connection);
}

static void
free_subtree_recurse (DBusConnection    *connection,
                      DBusObjectSubtree *subtree)
{
  /* Delete them from the end, for slightly
   * more robustness against odd reentrancy.
   */
  while (subtree->n_subtrees > 0)
    {
      DBusObjectSubtree *child;

      child = subtree->subtrees[subtree->n_subtrees - 1];
      subtree->subtrees[subtree->n_subtrees - 1] = NULL;
      subtree->n_subtrees -= 1;
      child->parent = NULL;

      free_subtree_recurse (connection, child);
    }

  /* Call application code */
  if (subtree->unregister_function)
    {
      (* subtree->unregister_function) (connection,
                                        subtree->user_data);
      subtree->message_function = NULL;
      subtree->unregister_function = NULL;
      subtree->user_data = NULL;
    }

  /* Now free ourselves */
  _dbus_object_subtree_unref (subtree);
}

/**
 * Free all the handlers in the tree. Lock on tree's connection
 * must not be held.
 *
 * @param tree the object tree
 */
void
_dbus_object_tree_free_all_unlocked (DBusObjectTree *tree)
{
  if (tree->root)
    free_subtree_recurse (tree->connection,
                          tree->root);
  tree->root = NULL;
}

/**
 * Tries to dispatch a message by directing it to handler for the
 * object path listed in the message header, if any. Messages are
 * dispatched first to the registered handler that matches the largest
 * number of path elements; that is, message to /foo/bar/baz would go
 * to the handler for /foo/bar before the one for /foo.
 *
 * @todo thread problems
 *
 * @param tree the global object tree
 * @param message the message to dispatch
 * @returns whether message was handled successfully
 */
DBusHandlerResult
_dbus_object_tree_dispatch_and_unlock (DBusObjectTree          *tree,
                                       DBusMessage             *message)
{
  char **path;
  DBusList *list;
  DBusList *link;
  DBusHandlerResult result;
  DBusObjectSubtree *subtree;

#if 0
  _dbus_verbose ("Dispatch of message by object path\n");
#endif
  
  path = NULL;
  if (!dbus_message_get_path_decomposed (message, &path))
    {
      _dbus_verbose ("No memory to get decomposed path\n");
      return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

  if (path == NULL)
    {
      _dbus_verbose ("No path field in message\n");
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

  /* Find the deepest path that covers the path in the message */
  subtree = find_handler (tree, (const char**) path);

  /* Build a list of all paths that cover the path in the message */

  list = NULL;

  while (subtree != NULL)
    {
      if (subtree->message_function != NULL)
        {
          _dbus_object_subtree_ref (subtree);

          /* run deepest paths first */
          if (!_dbus_list_append (&list, subtree))
            {
              result = DBUS_HANDLER_RESULT_NEED_MEMORY;
              _dbus_object_subtree_unref (subtree);
              goto free_and_return;
            }
        }

      subtree = subtree->parent;
    }

  _dbus_verbose ("%d handlers in the path tree for this message\n",
                 _dbus_list_get_length (&list));

  /* Invoke each handler in the list */

  result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  link = _dbus_list_get_first_link (&list);
  while (link != NULL)
    {
      DBusList *next = _dbus_list_get_next_link (&list, link);
      subtree = link->data;

      /* message_function is NULL if we're unregistered
       * due to reentrancy
       */
      if (subtree->message_function)
        {
          DBusObjectPathMessageFunction message_function;
          void *user_data;

          message_function = subtree->message_function;
          user_data = subtree->user_data;

#if 0
          _dbus_verbose ("  (invoking a handler)\n");
#endif
          
#ifdef DBUS_BUILD_TESTS
          if (tree->connection)
#endif
            _dbus_connection_unlock (tree->connection);

          /* FIXME you could unregister the subtree in another thread
           * before we invoke the callback, and I can't figure out a
           * good way to solve this.
           */

          result = (* message_function) (tree->connection,
                                         message,
                                         user_data);

          if (result != DBUS_HANDLER_RESULT_NOT_YET_HANDLED)
            goto free_and_return;

#ifdef DBUS_BUILD_TESTS
          if (tree->connection)
#endif
            _dbus_connection_lock (tree->connection);
        }

      link = next;
    }

#ifdef DBUS_BUILD_TESTS
  if (tree->connection)
#endif
    _dbus_connection_unlock (tree->connection);

 free_and_return:
  while (list != NULL)
    {
      link = _dbus_list_get_first_link (&list);
      _dbus_object_subtree_unref (link->data);
      _dbus_list_remove_link (&list, link);
    }
  dbus_free_string_array (path);

  return result;
}

/**
 * Allocates a subtree object.
 *
 * @param name name to duplicate.
 * @returns newly-allocated subtree
 */
static DBusObjectSubtree*
allocate_subtree_object (const char *name)
{
  int len;
  DBusObjectSubtree *subtree;
  const size_t front_padding = _DBUS_STRUCT_OFFSET (DBusObjectSubtree, name);

  _dbus_assert (name != NULL);

  len = strlen (name);

  subtree = dbus_malloc (front_padding + (len + 1));

  if (subtree == NULL)
    return NULL;

  memcpy (subtree->name, name, len + 1);

  return subtree;
}

static DBusObjectSubtree*
_dbus_object_subtree_new (const char                  *name,
                          const DBusObjectPathVTable  *vtable,
                          void                        *user_data)
{
  DBusObjectSubtree *subtree;

  subtree = allocate_subtree_object (name);
  if (subtree == NULL)
    goto oom;

  _dbus_assert (name != NULL);

  subtree->parent = NULL;

  if (vtable)
    {
      subtree->message_function = vtable->message_function;
      subtree->unregister_function = vtable->unregister_function;
    }
  else
    {
      subtree->message_function = NULL;
      subtree->unregister_function = NULL;
    }

  subtree->user_data = user_data;
  subtree->refcount.value = 1;
  subtree->subtrees = NULL;
  subtree->n_subtrees = 0;
  subtree->subtrees_sorted = TRUE;
  
  return subtree;

 oom:
  if (subtree)
    {
      dbus_free (subtree);
    }

  return NULL;
}

static void
_dbus_object_subtree_ref (DBusObjectSubtree *subtree)
{
  _dbus_assert (subtree->refcount.value > 0);
  _dbus_atomic_inc (&subtree->refcount);
}

static void
_dbus_object_subtree_unref (DBusObjectSubtree *subtree)
{
  _dbus_assert (subtree->refcount.value > 0);

  if (_dbus_atomic_dec (&subtree->refcount) == 1)
    {
      _dbus_assert (subtree->unregister_function == NULL);
      _dbus_assert (subtree->message_function == NULL);

      dbus_free (subtree->subtrees);
      dbus_free (subtree);
    }
}

/** @} */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>

static char*
flatten_path (const char **path)
{
  DBusString str;
  int i;
  char *s;

  if (!_dbus_string_init (&str))
    return NULL;

  i = 0;
  while (path[i])
    {
      if (!_dbus_string_append_byte (&str, '/'))
        goto nomem;

      if (!_dbus_string_append (&str, path[i]))
        goto nomem;

      ++i;
    }

  if (!_dbus_string_steal_data (&str, &s))
    goto nomem;

  _dbus_string_free (&str);

  return s;

 nomem:
  _dbus_string_free (&str);
  return NULL;
}

/* Returns TRUE if container is a parent of child
 */
static dbus_bool_t
path_contains (const char **container,
               const char **child)
{
  int i;

  i = 0;
  while (child[i] != NULL)
    {
      int v;

      if (container[i] == NULL)
        return TRUE; /* container ran out, child continues;
                      * thus the container is a parent of the
                      * child.
                      */

      _dbus_assert (container[i] != NULL);
      _dbus_assert (child[i] != NULL);

      v = strcmp (container[i], child[i]);

      if (v != 0)
        return FALSE; /* they overlap until here and then are different,
                       * not overlapping
                       */

      ++i;
    }

  /* Child ran out; if container also did, they are equal;
   * otherwise, the child is a parent of the container.
   */
  if (container[i] == NULL)
    return TRUE; /* equal is counted as containing */
  else
    return FALSE;
}

static void
spew_subtree_recurse (DBusObjectSubtree *subtree,
                      int                indent)
{
  int i;

  i = 0;
  while (i < indent)
    {
      _dbus_verbose (" ");
      ++i;
    }

  _dbus_verbose ("%s (%d children)\n",
                 subtree->name, subtree->n_subtrees);

  i = 0;
  while (i < subtree->n_subtrees)
    {
      spew_subtree_recurse (subtree->subtrees[i], indent + 2);

      ++i;
    }
}

static void
spew_tree (DBusObjectTree *tree)
{
  spew_subtree_recurse (tree->root, 0);
}

typedef struct
{
  const char **path;
  dbus_bool_t message_handled;
  dbus_bool_t handler_unregistered;

} TreeTestData;


static void
test_unregister_function (DBusConnection  *connection,
                          void            *user_data)
{
  TreeTestData *ttd = user_data;

  ttd->handler_unregistered = TRUE;
}

static DBusHandlerResult
test_message_function (DBusConnection  *connection,
                       DBusMessage     *message,
                       void            *user_data)
{
  TreeTestData *ttd = user_data;

  ttd->message_handled = TRUE;

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static dbus_bool_t
do_register (DBusObjectTree *tree,
             const char    **path,
             int             i,
             TreeTestData   *tree_test_data)
{
  DBusObjectPathVTable vtable = { test_unregister_function,
                                  test_message_function, NULL };

  tree_test_data[i].message_handled = FALSE;
  tree_test_data[i].handler_unregistered = FALSE;
  tree_test_data[i].path = path;

  if (!_dbus_object_tree_register (tree, path,
                                   &vtable,
                                   &tree_test_data[i]))
    return FALSE;

  return TRUE;
}

static dbus_bool_t
do_test_dispatch (DBusObjectTree *tree,
                  const char    **path,
                  int             i,
                  TreeTestData   *tree_test_data,
                  int             n_test_data)
{
  DBusMessage *message;
  int j;
  DBusHandlerResult result;
  char *flat;

  message = NULL;
  
  flat = flatten_path (path);
  if (flat == NULL)
    goto oom;

  message = dbus_message_new_method_call (NULL,
                                          flat,
                                          "org.freedesktop.TestInterface",
                                          "Foo");
  dbus_free (flat);
  if (message == NULL)
    goto oom;

  j = 0;
  while (j < n_test_data)
    {
      tree_test_data[j].message_handled = FALSE;
      ++j;
    }

  result = _dbus_object_tree_dispatch_and_unlock (tree, message);
  if (result == DBUS_HANDLER_RESULT_NEED_MEMORY)
    goto oom;

  _dbus_assert (tree_test_data[i].message_handled);

  j = 0;
  while (j < n_test_data)
    {
      if (tree_test_data[j].message_handled)
        _dbus_assert (path_contains (tree_test_data[j].path,
                                     path));
      else
        _dbus_assert (!path_contains (tree_test_data[j].path,
                                      path));

      ++j;
    }

  dbus_message_unref (message);

  return TRUE;

 oom:
  if (message)
    dbus_message_unref (message);
  return FALSE;
}

static dbus_bool_t
object_tree_test_iteration (void *data)
{
  const char *path1[] = { "foo", NULL };
  const char *path2[] = { "foo", "bar", NULL };
  const char *path3[] = { "foo", "bar", "baz", NULL };
  const char *path4[] = { "foo", "bar", "boo", NULL };
  const char *path5[] = { "blah", NULL };
  const char *path6[] = { "blah", "boof", NULL };
  const char *path7[] = { "blah", "boof", "this", "is", "really", "long", NULL };
  const char *path8[] = { "childless", NULL };
  DBusObjectTree *tree;
  TreeTestData tree_test_data[8];
  int i;

  tree = NULL;

  tree = _dbus_object_tree_new (NULL);
  if (tree == NULL)
    goto out;

  if (!do_register (tree, path1, 0, tree_test_data))
    goto out;

  _dbus_assert (find_subtree (tree, path1, NULL));
  _dbus_assert (!find_subtree (tree, path2, NULL));
  _dbus_assert (!find_subtree (tree, path3, NULL));
  _dbus_assert (!find_subtree (tree, path4, NULL));
  _dbus_assert (!find_subtree (tree, path5, NULL));
  _dbus_assert (!find_subtree (tree, path6, NULL));
  _dbus_assert (!find_subtree (tree, path7, NULL));
  _dbus_assert (!find_subtree (tree, path8, NULL));

  _dbus_assert (find_handler (tree, path1));
  _dbus_assert (find_handler (tree, path2));
  _dbus_assert (find_handler (tree, path3));
  _dbus_assert (find_handler (tree, path4));
  _dbus_assert (find_handler (tree, path5) == tree->root);
  _dbus_assert (find_handler (tree, path6) == tree->root);
  _dbus_assert (find_handler (tree, path7) == tree->root);
  _dbus_assert (find_handler (tree, path8) == tree->root);

  if (!do_register (tree, path2, 1, tree_test_data))
    goto out;

  _dbus_assert (find_subtree (tree, path1, NULL));
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (!find_subtree (tree, path3, NULL));
  _dbus_assert (!find_subtree (tree, path4, NULL));
  _dbus_assert (!find_subtree (tree, path5, NULL));
  _dbus_assert (!find_subtree (tree, path6, NULL));
  _dbus_assert (!find_subtree (tree, path7, NULL));
  _dbus_assert (!find_subtree (tree, path8, NULL));

  if (!do_register (tree, path3, 2, tree_test_data))
    goto out;

  _dbus_assert (find_subtree (tree, path1, NULL));
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));
  _dbus_assert (!find_subtree (tree, path4, NULL));
  _dbus_assert (!find_subtree (tree, path5, NULL));
  _dbus_assert (!find_subtree (tree, path6, NULL));
  _dbus_assert (!find_subtree (tree, path7, NULL));
  _dbus_assert (!find_subtree (tree, path8, NULL));
  
  if (!do_register (tree, path4, 3, tree_test_data))
    goto out;

  _dbus_assert (find_subtree (tree, path1, NULL));
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));  
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (!find_subtree (tree, path5, NULL));
  _dbus_assert (!find_subtree (tree, path6, NULL));
  _dbus_assert (!find_subtree (tree, path7, NULL));
  _dbus_assert (!find_subtree (tree, path8, NULL));
  
  if (!do_register (tree, path5, 4, tree_test_data))
    goto out;

  _dbus_assert (find_subtree (tree, path1, NULL));
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (!find_subtree (tree, path6, NULL));
  _dbus_assert (!find_subtree (tree, path7, NULL));
  _dbus_assert (!find_subtree (tree, path8, NULL));
  
  _dbus_assert (find_handler (tree, path1) != tree->root);
  _dbus_assert (find_handler (tree, path2) != tree->root);
  _dbus_assert (find_handler (tree, path3) != tree->root);
  _dbus_assert (find_handler (tree, path4) != tree->root);
  _dbus_assert (find_handler (tree, path5) != tree->root);
  _dbus_assert (find_handler (tree, path6) != tree->root);
  _dbus_assert (find_handler (tree, path7) != tree->root);
  _dbus_assert (find_handler (tree, path8) == tree->root);

  if (!do_register (tree, path6, 5, tree_test_data))
    goto out;

  _dbus_assert (find_subtree (tree, path1, NULL));
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));
  _dbus_assert (!find_subtree (tree, path7, NULL));
  _dbus_assert (!find_subtree (tree, path8, NULL));

  if (!do_register (tree, path7, 6, tree_test_data))
    goto out;

  _dbus_assert (find_subtree (tree, path1, NULL));
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));
  _dbus_assert (find_subtree (tree, path7, NULL));
  _dbus_assert (!find_subtree (tree, path8, NULL));

  if (!do_register (tree, path8, 7, tree_test_data))
    goto out;

  _dbus_assert (find_subtree (tree, path1, NULL));
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));
  _dbus_assert (find_subtree (tree, path7, NULL));
  _dbus_assert (find_subtree (tree, path8, NULL));
  
  _dbus_assert (find_handler (tree, path1) != tree->root);
  _dbus_assert (find_handler (tree, path2) != tree->root);
  _dbus_assert (find_handler (tree, path3) != tree->root);
  _dbus_assert (find_handler (tree, path4) != tree->root);
  _dbus_assert (find_handler (tree, path5) != tree->root);
  _dbus_assert (find_handler (tree, path6) != tree->root);
  _dbus_assert (find_handler (tree, path7) != tree->root);
  _dbus_assert (find_handler (tree, path8) != tree->root);
  
  /* Check that destroying tree calls unregister funcs */
  _dbus_object_tree_unref (tree);

  i = 0;
  while (i < (int) _DBUS_N_ELEMENTS (tree_test_data))
    {
      _dbus_assert (tree_test_data[i].handler_unregistered);
      _dbus_assert (!tree_test_data[i].message_handled);
      ++i;
    }

  /* Now start again and try the individual unregister function */
  tree = _dbus_object_tree_new (NULL);
  if (tree == NULL)
    goto out;

  if (!do_register (tree, path1, 0, tree_test_data))
    goto out;
  if (!do_register (tree, path2, 1, tree_test_data))
    goto out;
  if (!do_register (tree, path3, 2, tree_test_data))
    goto out;
  if (!do_register (tree, path4, 3, tree_test_data))
    goto out;
  if (!do_register (tree, path5, 4, tree_test_data))
    goto out;
  if (!do_register (tree, path6, 5, tree_test_data))
    goto out;
  if (!do_register (tree, path7, 6, tree_test_data))
    goto out;
  if (!do_register (tree, path8, 7, tree_test_data))
    goto out;
  
  _dbus_object_tree_unregister_and_unlock (tree, path1);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));
  _dbus_assert (find_subtree (tree, path7, NULL));
  _dbus_assert (find_subtree (tree, path8, NULL));

  _dbus_object_tree_unregister_and_unlock (tree, path2);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (!find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));
  _dbus_assert (find_subtree (tree, path7, NULL));
  _dbus_assert (find_subtree (tree, path8, NULL));
  
  _dbus_object_tree_unregister_and_unlock (tree, path3);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (!find_subtree (tree, path2, NULL));
  _dbus_assert (!find_subtree (tree, path3, NULL));
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));
  _dbus_assert (find_subtree (tree, path7, NULL));
  _dbus_assert (find_subtree (tree, path8, NULL));
  
  _dbus_object_tree_unregister_and_unlock (tree, path4);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (!find_subtree (tree, path2, NULL));
  _dbus_assert (!find_subtree (tree, path3, NULL));
  _dbus_assert (!find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));
  _dbus_assert (find_subtree (tree, path7, NULL));
  _dbus_assert (find_subtree (tree, path8, NULL));
  
  _dbus_object_tree_unregister_and_unlock (tree, path5);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (!find_subtree (tree, path2, NULL));
  _dbus_assert (!find_subtree (tree, path3, NULL));
  _dbus_assert (!find_subtree (tree, path4, NULL));
  _dbus_assert (!find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));
  _dbus_assert (find_subtree (tree, path7, NULL));
  _dbus_assert (find_subtree (tree, path8, NULL));
  
  _dbus_object_tree_unregister_and_unlock (tree, path6);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (!find_subtree (tree, path2, NULL));
  _dbus_assert (!find_subtree (tree, path3, NULL));
  _dbus_assert (!find_subtree (tree, path4, NULL));
  _dbus_assert (!find_subtree (tree, path5, NULL));
  _dbus_assert (!find_subtree (tree, path6, NULL));
  _dbus_assert (find_subtree (tree, path7, NULL));
  _dbus_assert (find_subtree (tree, path8, NULL));

  _dbus_object_tree_unregister_and_unlock (tree, path7);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (!find_subtree (tree, path2, NULL));
  _dbus_assert (!find_subtree (tree, path3, NULL));
  _dbus_assert (!find_subtree (tree, path4, NULL));
  _dbus_assert (!find_subtree (tree, path5, NULL));
  _dbus_assert (!find_subtree (tree, path6, NULL));
  _dbus_assert (!find_subtree (tree, path7, NULL));
  _dbus_assert (find_subtree (tree, path8, NULL));

  _dbus_object_tree_unregister_and_unlock (tree, path8);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (!find_subtree (tree, path2, NULL));
  _dbus_assert (!find_subtree (tree, path3, NULL));
  _dbus_assert (!find_subtree (tree, path4, NULL));
  _dbus_assert (!find_subtree (tree, path5, NULL));
  _dbus_assert (!find_subtree (tree, path6, NULL));
  _dbus_assert (!find_subtree (tree, path7, NULL));
  _dbus_assert (!find_subtree (tree, path8, NULL));
  
  i = 0;
  while (i < (int) _DBUS_N_ELEMENTS (tree_test_data))
    {
      _dbus_assert (tree_test_data[i].handler_unregistered);
      _dbus_assert (!tree_test_data[i].message_handled);
      ++i;
    }

  /* Register it all again, and test dispatch */

  if (!do_register (tree, path1, 0, tree_test_data))
    goto out;
  if (!do_register (tree, path2, 1, tree_test_data))
    goto out;
  if (!do_register (tree, path3, 2, tree_test_data))
    goto out;
  if (!do_register (tree, path4, 3, tree_test_data))
    goto out;
  if (!do_register (tree, path5, 4, tree_test_data))
    goto out;
  if (!do_register (tree, path6, 5, tree_test_data))
    goto out;
  if (!do_register (tree, path7, 6, tree_test_data))
    goto out;
  if (!do_register (tree, path8, 7, tree_test_data))
    goto out;

#if 0
  spew_tree (tree);
#endif
  
  if (!do_test_dispatch (tree, path1, 0, tree_test_data, _DBUS_N_ELEMENTS (tree_test_data)))
    goto out;
  if (!do_test_dispatch (tree, path2, 1, tree_test_data, _DBUS_N_ELEMENTS (tree_test_data)))
    goto out;
  if (!do_test_dispatch (tree, path3, 2, tree_test_data, _DBUS_N_ELEMENTS (tree_test_data)))
    goto out;
  if (!do_test_dispatch (tree, path4, 3, tree_test_data, _DBUS_N_ELEMENTS (tree_test_data)))
    goto out;
  if (!do_test_dispatch (tree, path5, 4, tree_test_data, _DBUS_N_ELEMENTS (tree_test_data)))
    goto out;
  if (!do_test_dispatch (tree, path6, 5, tree_test_data, _DBUS_N_ELEMENTS (tree_test_data)))
    goto out;
  if (!do_test_dispatch (tree, path7, 6, tree_test_data, _DBUS_N_ELEMENTS (tree_test_data)))
    goto out;
  if (!do_test_dispatch (tree, path8, 7, tree_test_data, _DBUS_N_ELEMENTS (tree_test_data)))
    goto out;
  
 out:
  if (tree)
    _dbus_object_tree_unref (tree);

  return TRUE;
}

/**
 * @ingroup DBusObjectTree
 * Unit test for DBusObjectTree
 * @returns #TRUE on success.
 */
dbus_bool_t
_dbus_object_tree_test (void)
{
  _dbus_test_oom_handling ("object tree",
                           object_tree_test_iteration,
                           NULL);

  return TRUE;
}

#endif /* DBUS_BUILD_TESTS */
