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
 * @{
 */

typedef struct DBusObjectSubtree DBusObjectSubtree;

static DBusObjectSubtree* _dbus_object_subtree_new   (const char                 **path,
                                                      const DBusObjectPathVTable  *vtable,
                                                      void                        *user_data);
static void               _dbus_object_subtree_ref   (DBusObjectSubtree           *subtree);
static void               _dbus_object_subtree_unref (DBusObjectSubtree           *subtree);

struct DBusObjectTree
{
  int                 refcount;
  DBusConnection     *connection;

  /* Each subtree is a separate malloc block since that
   * lets us refcount them and maybe helps with
   * reentrancy issues when calling back to application code
   */
  DBusObjectSubtree **subtrees;
  int                 n_subtrees;
  unsigned int        subtrees_sorted : 1;
};

struct DBusObjectSubtree
{
  DBusAtomic                         refcount;
  DBusObjectPathUnregisterFunction   unregister_function;
  DBusObjectPathMessageFunction      message_function;
  void                              *user_data;
  char                              *path[1]; /**< Allocated as large as necessary */
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

      dbus_free (tree->subtrees);
      dbus_free (tree);
    }
}

static int
path_cmp (const char **path_a,
          const char **path_b)
{
  /* strcmp() considers a shorter string less than a longer string if
   * the shorter string is the initial part of the longer. We
   * consider a path with less elements less than a path with more
   * elements.
   */
  int i;

  i = 0;
  while (path_a[i] != NULL)
    {
      int v;
      
      if (path_b[i] == NULL)
        return 1; /* a is longer than b */

      _dbus_assert (path_a[i] != NULL);
      _dbus_assert (path_b[i] != NULL);
      
      v = strcmp (path_a[i], path_b[i]);

      if (v != 0)
        return v;

      ++i;
    }

  _dbus_assert (path_a[i] == NULL);
  if (path_b[i] == NULL)
    return 0;
  
  /* b is longer than a */
  return -1;
}

static int
subtree_cmp (DBusObjectSubtree *subtree_a,
             DBusObjectSubtree *subtree_b)
{
  return path_cmp ((const char**) subtree_a->path,
                   (const char**) subtree_b->path);
}

static int
subtree_qsort_cmp (const void *a,
                   const void *b)
{
  DBusObjectSubtree **subtree_a_p = (void*) a;
  DBusObjectSubtree **subtree_b_p = (void*) b;

  return subtree_cmp (*subtree_a_p, *subtree_b_p);  
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
ensure_sorted (DBusObjectTree *tree)
{
  if (tree->subtrees && !tree->subtrees_sorted)
    {
      qsort (tree->subtrees,
             tree->n_subtrees,
             sizeof (DBusObjectSubtree*),
             subtree_qsort_cmp);
      tree->subtrees_sorted = TRUE;
    }
}

static dbus_bool_t
find_subtree (DBusObjectTree *tree,
              const char    **path,
              int            *idx_p)
{
  int i;
  
  if (tree->subtrees == NULL)
    return FALSE;

  ensure_sorted (tree);  

  /* FIXME this should be a binary search,
   * as that's the whole point of the sorting
   */
  i = 0;
  while (i < tree->n_subtrees)
    {
      int v;

      v = path_cmp (path,
                    (const char**) tree->subtrees[i]->path);
      
      if (v == 0)
        {
          if (idx_p)
            *idx_p = i;
          
          return TRUE;
        }
      else if (v < 0)
        {
          return FALSE;
        }
      
      ++i;
    }
  
  return FALSE;
}

static dbus_bool_t
find_handler (DBusObjectTree *tree,
              const char    **path,
              int            *idx_p)
{
  int i;
  int found_so_far;
  
  if (tree->subtrees == NULL)
    return FALSE;
  
  ensure_sorted (tree);
  
  /* FIXME this should be a binary search,
   * as that's the whole point of the sorting
   */
  found_so_far = -1;
  i = 0;
  while (i < tree->n_subtrees)
    {
      /* Longer paths are after shorter, so we scan
       * for the latest containing path in the array.
       * If we did a binary search we'd start with
       * the first search match.
       */
      if (path_contains ((const char**) tree->subtrees[i]->path,
                         path))
        found_so_far = i;
      else if (found_so_far >= 0)
        break; /* no need to scan further */
      
      ++i;
    }

  if (idx_p)
    *idx_p = found_so_far;

  return FALSE;
}

#ifndef DBUS_DISABLE_CHECKS
static void
check_already_exists (DBusObjectTree *tree,
                      const char    **path)
{
  int i;

  i = 0;
  while (i < tree->n_subtrees)
    {
      if (path_cmp (path, (const char**) tree->subtrees[i]->path) == 0)
        {
          _dbus_warn ("New path (path[0] = %s) already registered\n",
                      path[0]);
        }
      ++i;
    }
}
#endif

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
  DBusObjectSubtree **new_subtrees;
  int new_n_subtrees;

  _dbus_assert (tree != NULL);
  _dbus_assert (vtable->message_function != NULL);  
  _dbus_assert (path != NULL);
#ifndef DBUS_DISABLE_CHECKS
  check_already_exists (tree, path);
#endif
  _dbus_assert (path[0] != NULL);
  
  subtree = _dbus_object_subtree_new (path, vtable, user_data);
  if (subtree == NULL)
    return FALSE;
  
  /* FIXME we should do the "double alloc each time" standard thing */
  new_n_subtrees = tree->n_subtrees + 1;
  new_subtrees = dbus_realloc (tree->subtrees,
                               new_n_subtrees * sizeof (DBusObjectSubtree*));
  if (new_subtrees == NULL)
    {
      subtree->unregister_function = NULL;
      subtree->message_function = NULL;
      _dbus_object_subtree_unref (subtree);
      return FALSE;
    }

  new_subtrees[tree->n_subtrees] = subtree;
  tree->subtrees_sorted = FALSE;
  tree->n_subtrees = new_n_subtrees;
  tree->subtrees = new_subtrees;

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

  _dbus_assert (path != NULL);
  _dbus_assert (path[0] != NULL);

  if (!find_subtree (tree, path, &i))
    {
      _dbus_warn ("Attempted to unregister path (path[0] = %s path[1] = %s) which isn't registered\n",
                  path[0], path[1] ? path[1] : "null");
      return;
    }

  _dbus_assert (i >= 0);
  
  subtree = tree->subtrees[i];

  /* assumes a 0-byte memmove is OK */
  memmove (&tree->subtrees[i],
           &tree->subtrees[i+1],
           (tree->n_subtrees - i - 1) * sizeof (tree->subtrees[0]));
  tree->n_subtrees -= 1;

  subtree->message_function = NULL;
  
  /* Unlock and call application code */
#ifdef DBUS_BUILD_TESTS
  if (tree->connection)
#endif
    _dbus_connection_unlock (tree->connection);
  
  if (subtree->unregister_function)
    {
      (* subtree->unregister_function) (tree->connection,
                                        (const char**) subtree->path,
                                        subtree->user_data);
      subtree->unregister_function = NULL;
    }

  _dbus_object_subtree_unref (subtree);
}

/**
 * Free all the handlers in the tree. Lock on tree's connection
 * must not be held.
 *
 * @todo implement
 * 
 * @param tree the object tree
 */
void
_dbus_object_tree_free_all_unlocked (DBusObjectTree *tree)
{
  /* Delete them from the end, for slightly
   * more robustness against odd reentrancy.
   */
  while (tree->n_subtrees > 0)
    {
      DBusObjectSubtree *subtree;

      subtree = tree->subtrees[tree->n_subtrees - 1];
      tree->subtrees[tree->n_subtrees - 1] = NULL;
      tree->n_subtrees -= 1;

      subtree->message_function = NULL; /* it's been removed */

      /* Call application code */
      if (subtree->unregister_function)
        {
          (* subtree->unregister_function) (tree->connection,
                                            (const char**) subtree->path,
                                            subtree->user_data);
          subtree->unregister_function = NULL;
        }
      
      _dbus_object_subtree_unref (subtree);      
    }
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
  const char **path;
  int i;
  DBusList *list;
  DBusList *link;
  DBusHandlerResult result;
  
  path = NULL; /* dbus_message_get_object_path (message); */

  if (path == NULL)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  /* Find the deepest path that covers the path in the message */
  if (!find_handler (tree, path, &i))
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  /* Build a list of all paths that cover the path in the message */
  
  list = NULL;
  
  do 
    {
      DBusObjectSubtree *subtree;

      subtree = tree->subtrees[i];

      _dbus_object_subtree_ref (subtree);
      _dbus_list_append (&list, subtree);

      --i;
      
    } while (i > 0 && path_contains ((const char**) tree->subtrees[i]->path,
                                     path));

  /* Invoke each handler in the list */
  
  result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  
  link = _dbus_list_get_first_link (&list);
  while (link != NULL)
    {
      DBusObjectSubtree *subtree = link->data;
      DBusList *next = _dbus_list_get_next_link (&list, link);
      
      /* message_function is NULL if we're unregistered */
      if (subtree->message_function)
        {
#ifdef DBUS_BUILD_TESTS
          if (tree->connection)
#endif
            _dbus_connection_unlock (tree->connection);

          /* FIXME you could unregister the subtree in another thread
           * before we invoke the callback, and I can't figure out a
           * good way to solve this.
           */
          
          result = (* subtree->message_function) (tree->connection,
                                                  message, subtree->user_data);
          
          if (result == DBUS_HANDLER_RESULT_HANDLED)
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
  
  return result;
}

/**
 * Allocates a subtree object with a string array appended as one big
 * memory block, so result is freed with one dbus_free(). Returns
 * #NULL if memory allocation fails.
 *
 * @param array array to duplicate.
 * @returns newly-allocated subtree
 */
static DBusObjectSubtree*
allocate_subtree_object (const char **array)
{
  int len;
  int member_lens;
  int i;
  char *p;
  void *subtree;
  char **path_dest;
  const size_t front_padding = _DBUS_STRUCT_OFFSET (DBusObjectSubtree, path);
  
  if (array == NULL)
    return NULL;

  member_lens = 0;
  for (len = 0; array[len] != NULL; ++len)
    member_lens += strlen (array[len]) + 1;
  
  subtree = dbus_malloc (front_padding +
                         (len + 1) * sizeof (char*) +
                         member_lens);
  if (subtree == NULL)
    return NULL;

  path_dest = (char**) (((char*) subtree) + front_padding);
  
  path_dest[len] = NULL; /* NULL-terminate the array portion */
  p = ((char*) subtree) + (len + 1) * sizeof (char*) + front_padding;
  
  i = 0;
  while (i < len)
    {
      int this_len;

      path_dest[i] = p;
      
      this_len = strlen (array[i]);
      memcpy (p, array[i], this_len + 1);
      p += this_len + 1;

      ++i;
    }

  return subtree;
}

static DBusObjectSubtree*
_dbus_object_subtree_new (const char                 **path,
                          const DBusObjectPathVTable  *vtable,
                          void                        *user_data)
{
  DBusObjectSubtree *subtree;

  subtree = allocate_subtree_object (path);
  if (subtree == NULL)
    goto oom;

  _dbus_assert (path != NULL);
  _dbus_assert (path[0] != NULL);

  subtree->message_function = vtable->message_function;
  subtree->unregister_function = vtable->unregister_function;
  subtree->user_data = user_data;
  subtree->refcount.value = 1;
  
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

static void
spew_tree (DBusObjectTree *tree)
{
  int i;

  printf ("Tree of %d subpaths\n",
          tree->n_subtrees);
  
  i = 0;
  while (i < tree->n_subtrees)
    {
      char *s;

      s = flatten_path ((const char **) tree->subtrees[i]->path);

      printf ("  %d path = %s\n", i, s);

      dbus_free (s);
      
      ++i;
    }
}

static dbus_bool_t
test_subtree_cmp (const char **path1,
                  const char **path2,
                  int          expected,
                  dbus_bool_t  reverse)
{
  DBusObjectSubtree *subtree1;
  DBusObjectSubtree *subtree2;
  dbus_bool_t retval;
  DBusObjectPathVTable vtable;

  _DBUS_ZERO (vtable);

  retval = FALSE;
  
  subtree1 = _dbus_object_subtree_new (path1, &vtable, NULL);
  subtree2 = _dbus_object_subtree_new (path2, &vtable, NULL);
  if (subtree1 == NULL || subtree2 == NULL)
    goto out;

  _dbus_assert (subtree_cmp (subtree1, subtree2) == expected);

  retval = TRUE;
  
 out:

  if (subtree1)
    _dbus_object_subtree_unref (subtree1);

  if (subtree2)
    _dbus_object_subtree_unref (subtree2);

  if (retval && reverse)
    {
      /* Verify that the reverse also holds */
      if (expected > 0)
        return test_subtree_cmp (path2, path1, -1, FALSE);
      else if (expected < 0)
        return test_subtree_cmp (path2, path1, 1, FALSE);
      else
        return test_subtree_cmp (path2, path1, 0, FALSE);
    }
  
  return retval;
}

static void
test_path_contains (const char  **path1,
                    const char  **path2,
                    dbus_bool_t   expected)
{
  if (!path_contains (path1, path2) == expected)
    {
      char *s1, *s2;
      s1 = flatten_path (path1);
      s2 = flatten_path (path2);
      
      _dbus_warn ("Expected that path %s %s %s\n",
                  s1, expected ? "contains" : "doesn't contain", s2);
      
      dbus_free (s1);
      dbus_free (s2);
      
      exit (1);
    }
  
  if (path_cmp (path1, path2) == 0)
    {
      if (!path_contains (path2, path1))
        {
          char *s1, *s2;
          s1 = flatten_path (path1);
          s2 = flatten_path (path2);
          
          _dbus_warn ("Expected that path %s contains %s since the paths are equal\n",
                      s1, s2);
          
          dbus_free (s1);
          dbus_free (s2);
          
          exit (1);
        }
    }
  /* If path1 contains path2, then path2 can't contain path1 */
  else if (expected && path_contains (path2, path1))
    {
      char *s1, *s2;

      s1 = flatten_path (path1);
      s2 = flatten_path (path2);
      
      _dbus_warn ("Expected that path %s doesn't contain %s\n",
                  s1, s2);

      dbus_free (s1);
      dbus_free (s2);
      
      exit (1);
    }
}

static void
test_path_copy (const char **path)
{
  DBusObjectSubtree *subtree;

  subtree = allocate_subtree_object (path);
  if (subtree == NULL)
    return;
  
  _dbus_assert (path_cmp (path, (const char**) subtree->path) == 0);

  dbus_free (subtree);
}

typedef struct
{
  dbus_bool_t message_handled;
  dbus_bool_t handler_unregistered;

} TreeTestData;


static void
test_unregister_function (DBusConnection  *connection,
                          const char     **path,
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
  
  if (!_dbus_object_tree_register (tree, path,
                                   &vtable,
                                   &tree_test_data[i]))
    return FALSE;

  return TRUE;
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
  DBusObjectTree *tree;
  TreeTestData tree_test_data[6];
  int i;
  
  test_path_copy (path1);
  test_path_copy (path2);
  test_path_copy (path3);
  test_path_copy (path4);
  test_path_copy (path5);
  test_path_copy (path6);
  
  tree = NULL;

  test_path_contains (path1, path1, TRUE);
  test_path_contains (path1, path2, TRUE);
  test_path_contains (path1, path3, TRUE);
  test_path_contains (path1, path4, TRUE);
  test_path_contains (path1, path5, FALSE);
  test_path_contains (path1, path6, FALSE); 

  test_path_contains (path2, path1, FALSE);
  test_path_contains (path2, path2, TRUE);
  test_path_contains (path2, path3, TRUE);
  test_path_contains (path2, path4, TRUE);
  test_path_contains (path2, path5, FALSE);
  test_path_contains (path2, path6, FALSE);

  test_path_contains (path3, path1, FALSE);
  test_path_contains (path3, path2, FALSE);
  test_path_contains (path3, path3, TRUE);
  test_path_contains (path3, path4, FALSE);
  test_path_contains (path3, path5, FALSE);
  test_path_contains (path3, path6, FALSE);

  test_path_contains (path4, path1, FALSE);
  test_path_contains (path4, path2, FALSE);
  test_path_contains (path4, path3, FALSE);
  test_path_contains (path4, path4, TRUE);
  test_path_contains (path4, path5, FALSE);
  test_path_contains (path4, path6, FALSE);

  test_path_contains (path5, path1, FALSE);
  test_path_contains (path5, path2, FALSE);
  test_path_contains (path5, path3, FALSE);
  test_path_contains (path5, path4, FALSE);
  test_path_contains (path5, path5, TRUE);
  test_path_contains (path5, path6, TRUE);
  
  test_path_contains (path6, path1, FALSE);
  test_path_contains (path6, path2, FALSE);
  test_path_contains (path6, path3, FALSE);
  test_path_contains (path6, path4, FALSE);
  test_path_contains (path6, path5, FALSE);
  test_path_contains (path6, path6, TRUE);
  
  if (!test_subtree_cmp (path1, path1, 0, TRUE))
    goto out;
  if (!test_subtree_cmp (path3, path3, 0, TRUE))
    goto out;
  /* When testing -1, the reverse also gets tested */
  if (!test_subtree_cmp (path1, path2, -1, TRUE))
    goto out;
  if (!test_subtree_cmp (path1, path3, -1, TRUE))
    goto out;
  if (!test_subtree_cmp (path2, path3, -1, TRUE))
    goto out;
  if (!test_subtree_cmp (path2, path4, -1, TRUE))
    goto out;
  if (!test_subtree_cmp (path3, path4, -1, TRUE))
    goto out;
  if (!test_subtree_cmp (path5, path1, -1, TRUE))
    goto out;
  
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
  
  if (!do_register (tree, path2, 1, tree_test_data))
    goto out;
  
  _dbus_assert (find_subtree (tree, path1, NULL));  
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (!find_subtree (tree, path3, NULL));
  _dbus_assert (!find_subtree (tree, path4, NULL));
  _dbus_assert (!find_subtree (tree, path5, NULL));
  _dbus_assert (!find_subtree (tree, path6, NULL));
  
  if (!do_register (tree, path3, 2, tree_test_data))
    goto out;

  _dbus_assert (find_subtree (tree, path1, NULL));
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));
  _dbus_assert (!find_subtree (tree, path4, NULL));
  _dbus_assert (!find_subtree (tree, path5, NULL));
  _dbus_assert (!find_subtree (tree, path6, NULL));
  
  if (!do_register (tree, path4, 3, tree_test_data))
    goto out;


  _dbus_assert (find_subtree (tree, path1, NULL));
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (!find_subtree (tree, path5, NULL));
  _dbus_assert (!find_subtree (tree, path6, NULL));
  
  if (!do_register (tree, path5, 4, tree_test_data))
    goto out;

  _dbus_assert (find_subtree (tree, path1, NULL));
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (!find_subtree (tree, path6, NULL));
  
  if (!do_register (tree, path6, 5, tree_test_data))
    goto out;

  _dbus_assert (find_subtree (tree, path1, NULL));
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));

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

  _dbus_object_tree_unregister_and_unlock (tree, path1);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));
  
  _dbus_object_tree_unregister_and_unlock (tree, path2);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (!find_subtree (tree, path2, NULL));
  _dbus_assert (find_subtree (tree, path3, NULL));
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));

  _dbus_object_tree_unregister_and_unlock (tree, path3);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (!find_subtree (tree, path2, NULL));
  _dbus_assert (!find_subtree (tree, path3, NULL));
  _dbus_assert (find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));
  
  _dbus_object_tree_unregister_and_unlock (tree, path4);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (!find_subtree (tree, path2, NULL));
  _dbus_assert (!find_subtree (tree, path3, NULL));
  _dbus_assert (!find_subtree (tree, path4, NULL));
  _dbus_assert (find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));
  
  _dbus_object_tree_unregister_and_unlock (tree, path5);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (!find_subtree (tree, path2, NULL));
  _dbus_assert (!find_subtree (tree, path3, NULL));
  _dbus_assert (!find_subtree (tree, path4, NULL));
  _dbus_assert (!find_subtree (tree, path5, NULL));
  _dbus_assert (find_subtree (tree, path6, NULL));
  
  _dbus_object_tree_unregister_and_unlock (tree, path6);

  _dbus_assert (!find_subtree (tree, path1, NULL));
  _dbus_assert (!find_subtree (tree, path2, NULL));
  _dbus_assert (!find_subtree (tree, path3, NULL));
  _dbus_assert (!find_subtree (tree, path4, NULL));
  _dbus_assert (!find_subtree (tree, path5, NULL));
  _dbus_assert (!find_subtree (tree, path6, NULL));
  
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
  
  /* FIXME (once messages have an object path field) */
  
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
