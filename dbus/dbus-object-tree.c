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

DBusObjectSubtree* _dbus_object_subtree_new   (const char                 **path,
                                               const DBusObjectTreeVTable  *vtable,
                                               void                        *user_data);
void               _dbus_object_subtree_ref   (DBusObjectSubtree           *subtree);
void               _dbus_object_subtree_unref (DBusObjectSubtree           *subtree);

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
  int                   refcount;
  char                **path;
  int                   n_path_elements;
  DBusObjectTreeVTable  vtable;
  void                 *user_data;
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
      if (tree->subtrees)
        {
          int i;
          i = 0;
          while (i < tree->n_subtrees)
            {
              _dbus_object_subtree_unref (tree->subtrees[i]);
              ++i;
            }
        }

      dbus_free (tree);
    }
}

static int
path_cmp (const char **path_a,
          const char **path_b)
{
  /* The comparison is as if the path were flattened
   * into a single string. strcmp() considers
   * a shorter string less than a longer string
   * if the shorter string is the initial part
   * of the longer
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

/* Returns TRUE if a is a subdir of b or vice
 * versa. This is the case if one is a subpath
 * of the other.
 */
static dbus_bool_t
path_overlaps (const char **path_a,
               const char **path_b)
{
  int i;

  i = 0;
  while (path_a[i] != NULL)
    {
      int v;
      
      if (path_b[i] == NULL)
        return TRUE; /* b is subpath of a */

      _dbus_assert (path_a[i] != NULL);
      _dbus_assert (path_b[i] != NULL);
      
      v = strcmp (path_a[i], path_b[i]);

      if (v != 0)
        return FALSE; /* they overlap until here and then are different,
                       * not overlapping
                       */

      ++i;
    }

  /* b is either the same as or a superset of a */
  _dbus_assert (path_a[i] == NULL);
  return TRUE;
}

static dbus_bool_t
find_subtree (DBusObjectTree *tree,
              const char    **path,
              int            *idx_p)
{
  int i;
  
  if (tree->subtrees == NULL)
    return FALSE;
  
  if (!tree->subtrees_sorted)
    {
      qsort (tree->subtrees,
             tree->n_subtrees,
             sizeof (DBusObjectSubtree*),
             subtree_qsort_cmp);
      tree->subtrees_sorted = TRUE;
    }

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
      else if (v > 0)
        return FALSE;
      
      ++i;
    }

  return FALSE;
}

#ifndef DBUS_DISABLE_CHECKS
static void
check_overlap (DBusObjectTree *tree,
               const char    **path)
{
  int i;

  i = 0;
  while (i < tree->n_subtrees)
    {
      if (path_overlaps (path, (const char**) tree->subtrees[i]->path))
        {
          _dbus_warn ("New path (path[0] = %s) overlaps old path (path[0] = %s)\n",
                      path[0], tree->subtrees[i]->path[0]);
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
                            const DBusObjectTreeVTable  *vtable,
                            void                        *user_data)
{
  DBusObjectSubtree  *subtree;
  DBusObjectSubtree **new_subtrees;
  int new_n_subtrees;

  _dbus_assert (path != NULL);
#ifndef DBUS_DISABLE_CHECKS
  check_overlap (tree, path);
#endif
  _dbus_assert (path[0] != NULL);
  
  subtree = _dbus_object_subtree_new (path, vtable, user_data);
  if (subtree == NULL)
    return FALSE;
  
  /* FIXME we should do the "double alloc each time" standard thing */
  new_n_subtrees = tree->n_subtrees + 1;
  new_subtrees = dbus_realloc (tree->subtrees,
                               new_n_subtrees);
  if (new_subtrees == NULL)
    {
      _DBUS_ZERO (subtree->vtable); /* to avoid assertion in unref() */
      _dbus_object_subtree_unref (subtree);
      return FALSE;
    }

  tree->subtrees[tree->n_subtrees] = subtree;
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
      _dbus_warn ("Attempted to unregister subtree (path[0] = %s) which isn't registered\n",
                  path[0]);
      return;
    }

  subtree = tree->subtrees[i];

  /* assumes a 0-byte memmove is OK */
  memmove (&tree->subtrees[i],
           &tree->subtrees[i+1],
           (tree->n_subtrees - i - 1) * sizeof (tree->subtrees[0]));
  tree->n_subtrees -= 1;
  
  _dbus_object_subtree_ref (subtree);

  /* Unlock and call application code */
  _dbus_connection_unlock (tree->connection);
  
  if (subtree->vtable.unregister_function)
    {
      (* subtree->vtable.unregister_function) (tree->connection,
                                               (const char**) subtree->path,
                                               subtree->user_data);
      _DBUS_ZERO (subtree->vtable);
    }
}

/**
 * Tries to dispatch a message by directing it to the object tree
 * node listed in the message header, if any.
 *
 * @param tree the global object tree
 * @param message the message to dispatch
 * @returns whether message was handled successfully
 */
DBusHandlerResult
_dbus_object_tree_dispatch_and_unlock (DBusObjectTree          *tree,
                                       DBusMessage             *message)
{
  
  
}

DBusObjectSubtree*
_dbus_object_subtree_new (const char                 **path,
                          const DBusObjectTreeVTable  *vtable,
                          void                        *user_data)
{
  DBusObjectSubtree *subtree;

  subtree = dbus_new0 (DBusObjectSubtree, 1);
  if (subtree == NULL)
    goto oom;

  _dbus_assert (path != NULL);
  _dbus_assert (path[0] != NULL);
  
  subtree->path = _dbus_dup_string_array (path);
  if (subtree->path == NULL)
    goto oom;

  subtree->vtable = *vtable;
  subtree->user_data = user_data;
  
  subtree->refcount = 1;

  /* count path elements */
  while (subtree->path[subtree->n_path_elements])
    subtree->n_path_elements += 1;
  
  return subtree;

 oom:
  if (subtree)
    {
      dbus_free_string_array (subtree->path);
      dbus_free (subtree);
    }
  
  return NULL;
}

void
_dbus_object_subtree_ref (DBusObjectSubtree *subtree)
{
  _dbus_assert (subtree->refcount > 0);

  subtree->refcount += 1;
}

void
_dbus_object_subtree_unref (DBusObjectSubtree *subtree)
{
  _dbus_assert (subtree->refcount > 0);

  subtree->refcount -= 1;

  if (subtree->refcount == 0)
    {
      _dbus_assert (subtree->vtable.unregister_function == NULL);
      
      dbus_free_string_array (subtree->path);

      dbus_free (subtree);
    }
}

/** @} */

#ifdef DBUS_BUILD_TESTS
#include "dbus-test.h"
#include <stdio.h>

static dbus_bool_t
test_subtree_cmp (const char **path1,
                  const char **path2,
                  int          expected,
                  dbus_bool_t  reverse)
{
  DBusObjectSubtree *subtree1;
  DBusObjectSubtree *subtree2;
  dbus_bool_t retval;
  DBusObjectTreeVTable vtable;

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
test_path_overlap (const char  **path1,
                   const char  **path2,
                   dbus_bool_t   expected)
{
  _dbus_assert (path_overlaps (path1, path2) == expected);
  _dbus_assert (path_overlaps (path2, path1) == expected);
}

static dbus_bool_t
object_tree_test_iteration (void *data)
{
  const char *path1[] = { "foo", NULL };
  const char *path2[] = { "foo", "bar", NULL };
  const char *path3[] = { "foo", "bar", "baz", NULL };
  const char *path4[] = { "foo", "bar", "boo", NULL };
  const char *path5[] = { "blah", NULL };
  DBusObjectSubtree *subtree1;
  DBusObjectSubtree *subtree2;
  DBusObjectTree *tree;

  tree = NULL;
  subtree1 = NULL;
  subtree2 = NULL;

  test_path_overlap (path1, path1, TRUE);
  test_path_overlap (path1, path2, TRUE);
  test_path_overlap (path1, path3, TRUE);
  test_path_overlap (path1, path4, TRUE);
  test_path_overlap (path1, path5, FALSE); 

  test_path_overlap (path2, path2, TRUE);
  test_path_overlap (path2, path3, TRUE);
  test_path_overlap (path2, path4, TRUE);
  test_path_overlap (path2, path5, FALSE);

  test_path_overlap (path3, path3, TRUE);
  test_path_overlap (path3, path4, FALSE);
  test_path_overlap (path3, path5, FALSE);

  test_path_overlap (path4, path4, TRUE);
  test_path_overlap (path4, path5, FALSE);

  test_path_overlap (path5, path5, TRUE);
  
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

 out:
  if (subtree1)
    _dbus_object_subtree_unref (subtree1);
  if (subtree2)
    _dbus_object_subtree_unref (subtree2);
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
