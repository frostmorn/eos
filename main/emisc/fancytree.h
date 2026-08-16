#pragma once
#include <stddef.h>
#include <stdbool.h>

///////////////////////////////////////////////////////////////////
// EOS Project header file
///////////////////////////////////////////////////////////////////
//
// (^__^)==\~ Generic first-child/next-sibling n-ary tree //////////
//
// One tested implementation of parent/child pointer-juggling,
// shared by every subsystem that needs a hierarchy (device tree,
// application contexts, and whatever comes next) instead of each
// one hand-rolling its own - which is exactly how the device tree
// and the application-context tree ended up with two different,
// one of them silently broken, ways of doing the same thing.
//
// Your struct needs to contain EOS_TREE_FIELDS(my_node_t) in order
// to use generated tree functions
//
// In the header:  EOS_TREE_DECLARE(my_node_t);
// In the source:  EOS_TREE_DEFINE(my_node_t);
//
// That generates, for T = my_node_t:
//
//   bool T##_tree_attach(T *node, T *parent)
//     Attaches 'node' as the new last child of 'parent'. Fails
//     (returns false) if node/parent is NULL, node == parent, node
//     is already attached (node->parent != NULL), or parent is
//     currently a descendant of node (would create a cycle).
//
//   void T##_tree_detach(T *node)
//     Detaches just 'node' from its parent. Node's own children are
//     NOT removed - they're reparented onto node's former parent
//     (matching how a Unix process's orphaned children get
//     reparented rather than killed). No-op if node has no parent.
//
//   void T##_tree_detach_subtree(T *node, void (*on_detach)(T*, void*), void *ctx)
//     Detaches 'node' AND all of its descendants. Descendants are
//     detached first, deepest/leftmost first, each one passed to
//     on_detach(node, ctx) right after it's unlinked - so the
//     callback can free it, shut down a driver, etc. 'node' itself
//     is passed to on_detach() last. on_detach may be NULL.
//
//   bool T##_tree_walk(T *node, bool (*visit)(T*, void*), void *ctx)
//     Pre-order walk of 'node' and all descendants. Stops early and
//     returns false the moment visit() returns false; returns true
//     if every node was visited.
//
///////////////////////////////////////////////////////////////////

#define EOS_TREE_FIELDS(T) \
/* Default tree fields  */ \
   T *parent;              \
   T *child;               \
   T *next;                

#define EOS_TREE_DECLARE(T)                 \
  bool T##_tree_attach(T *node, T *parent); \
                                            \
  void T##_tree_detach(T *node);            \
                                            \
  void T##_tree_detach_subtree(             \
    T *node,                                \
    void (*on_detach)(T *node, void *ctx),  \
    void *ctx                               \
  );                                        \
                                            \
  bool T##_tree_walk(                       \
    T *node,                                \
    bool (*visit)(T *node, void *ctx),      \
    void *ctx                               \
  )

#define EOS_TREE_DEFINE(T) \
\
static bool T##_tree_is_descendant(T *maybe_ancestor, T *node){ \
  for (T *p = maybe_ancestor->parent; p; p = p->parent) \
    if (p == node) return true; \
  return false; \
} \
\
bool T##_tree_attach(T *node, T *parent){ \
  if (!node || !parent) return false; \
  if (node == parent) return false; \
  if (node->parent != NULL) return false; /* already attached */ \
  if (T##_tree_is_descendant(parent, node)) return false; /* would cycle */ \
\
  node->parent = parent; \
  node->next = NULL; \
\
  if (parent->child == NULL){ \
    parent->child = node; \
  } else { \
    T *cur = parent->child; \
    while (cur->next != NULL) cur = cur->next; \
    cur->next = node; \
  } \
\
  return true; \
} \
\
void T##_tree_detach(T *node){ \
  if (!node || !node->parent) return; \
\
  T *parent = node->parent; \
\
  /* unlink node from parent's child chain */ \
  if (parent->child == node){ \
    parent->child = node->next; \
  } else { \
    T *cur = parent->child; \
    while (cur->next != NULL && cur->next != node) cur = cur->next; \
    if (cur->next == node) cur->next = node->next; \
  } \
\
  /* reparent node's own children onto node's former parent */ \
  T *kid = node->child; \
  while (kid != NULL){ \
    T *next_kid = kid->next; \
    kid->parent = parent; \
    kid->next = NULL; \
    if (parent->child == NULL){ \
      parent->child = kid; \
    } else { \
      T *cur = parent->child; \
      while (cur->next != NULL) cur = cur->next; \
      cur->next = kid; \
    } \
    kid = next_kid; \
  } \
\
  node->parent = NULL; \
  node->child = NULL; \
  node->next = NULL; \
} \
\
void T##_tree_detach_subtree(T *node, void (*on_detach)(T *node, void *ctx), void *ctx){ \
  if (!node) return; \
\
  T *kid = node->child; \
  while (kid != NULL){ \
    T *next_kid = kid->next; \
    T##_tree_detach_subtree(kid, on_detach, ctx); \
    kid = next_kid; \
  } \
\
  T *parent = node->parent; \
  if (parent){ \
    if (parent->child == node){ \
      parent->child = node->next; \
    } else { \
      T *cur = parent->child; \
      while (cur->next != NULL && cur->next != node) cur = cur->next; \
      if (cur->next == node) cur->next = node->next; \
    } \
  } \
\
  node->parent = NULL; \
  node->child = NULL; \
  node->next = NULL; \
\
  if (on_detach) on_detach(node, ctx); \
} \
\
bool T##_tree_walk(T *node, bool (*visit)(T *node, void *ctx), void *ctx){ \
  if (!node) return true; \
  if (!visit(node, ctx)) return false; \
  for (T *kid = node->child; kid != NULL; kid = kid->next) \
    if (!T##_tree_walk(kid, visit, ctx)) return false; \
  return true; \
}
