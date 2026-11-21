/**
 * @file sof_rbtree.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief sof_rbtree implementation
 * @version 0.0.1
 * @date 2026-11-02
 * 
 * @copyright Copyright (c) 2026, Warren Mann
 */

#include <limits.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_rbtree.h"

/// @brief The most nodes on any path from the root: a red-black tree of n
/// nodes is at most 2 * log2(n + 1) high, and n + 1 <= 2^(bits in size_t),
/// so 128 on a 64-bit system.
#define SOF_RBTREE_MAX_HEIGHT           (2 * sizeof(size_t) * CHAR_BIT)

// Changing a node's color would break the tree, so these aren't public.
#define SOF_RBTREE_SET_BLACK(n)         ((n)->flags &= SOF_RBTREE_USER_MASK)
#define SOF_RBTREE_SET_RED(n)           ((n)->flags |= SOF_RBTREE_COLOR_RED)

static size_t collect_subtree(sof_rbtree_t *tree, sof_rbtree_node_t *node, sof_rbtree_node_t **nodes, size_t i);
static void delete_fixup(sof_rbtree_t *tree, sof_rbtree_node_t *x);
static void delete_subtree(sof_rbtree_t *tree, sof_rbtree_node_t *node);
static void insert_fixup(sof_rbtree_t *tree, sof_rbtree_node_t *node);
static sof_rbtree_node_t *mutable_node(const sof_rbtree_node_t *node);
static void rotate_left(sof_rbtree_t *tree, sof_rbtree_node_t *x);
static void rotate_right(sof_rbtree_t *tree, sof_rbtree_node_t *x);
static void transplant(sof_rbtree_t *tree, sof_rbtree_node_t *u, sof_rbtree_node_t *v);

sof_rbtree_error_t sof_rbtree_clear(sof_rbtree_t *tree, sof_rbtree_node_t *subtree) {
    sof_rbtree_node_t **nodes;
    size_t count;
    size_t i;
    if (tree == NULL) {
        return SOF_RBTREE_ERROR;
    }
    if (tree->root == NULL || tree->root == &tree->nil_node) {
        // Freed or empty: nothing to do.
        return SOF_RBTREE_OK;
    }
    if (subtree == &tree->nil_node) {
        // An empty subtree (such as a leaf's missing child): nothing to do.
        return SOF_RBTREE_OK;
    }
    if (subtree == NULL || subtree == tree->root) {
        // The whole tree: no balancing needed, free every node.
        delete_subtree(tree, tree->root);
        tree->root = &tree->nil_node;
        return SOF_RBTREE_OK;
    }
    // Part of the tree: remove one node at a time so the rest stays balanced.
    // sof_rbtree_remove_node relinks nodes without moving them, so the collected
    // pointers stay valid until each is removed.
    count = collect_subtree(tree, subtree, NULL, 0);
    nodes = sof_alloc_malloc(&tree->alloc, count * sizeof(sof_rbtree_node_t *));
    if (nodes == NULL) {
        return SOF_RBTREE_NO_MEM;
    }
    collect_subtree(tree, subtree, nodes, 0);
    for (i = 0; i < count; i++) {
        sof_rbtree_remove_node(tree, nodes[i]);
    }
    sof_alloc_free(&tree->alloc, nodes, count * sizeof(sof_rbtree_node_t *));
    return SOF_RBTREE_OK;
}

sof_rbtree_error_t sof_rbtree_remove(sof_rbtree_t *tree, const void *key) {
    sof_rbtree_node_t *node;
    if (tree == NULL) {
        return SOF_RBTREE_ERROR;
    }
    node = sof_rbtree_find(tree, key);
    if (node == NULL) {
        return SOF_RBTREE_NO_MATCH;
    }
    return sof_rbtree_remove_node(tree, node);
}

sof_rbtree_error_t sof_rbtree_remove_node(sof_rbtree_t *tree, sof_rbtree_node_t *node) {
    sof_rbtree_node_t *w = node;
    sof_rbtree_node_t *x;
    sof_rbtree_node_t *y = node;
    uint32_t color;
    if (tree == NULL || tree->root == NULL || tree->root == &tree->nil_node || node == NULL || node == &tree->nil_node) {
        return SOF_RBTREE_ERROR;
    }
    color = y->flags & SOF_RBTREE_COLOR_MASK;
    if (node->left == &tree->nil_node) {
        x = node->right;
        transplant(tree, node, node->right);
    } else if (node->right == &tree->nil_node) {
        x = node->left;
        transplant(tree, node, node->left);
    } else {
        y = sof_rbtree_minimum(tree, node->right);
        color = y->flags & SOF_RBTREE_COLOR_MASK;
        x = y->right;
        if (y->parent == node) {
            x->parent = y;
        } else {
            transplant(tree, y, y->right);
            y->right = node->right;
            y->right->parent = y;
        }
        transplant(tree, node, y);
        y->left = node->left;
        y->left->parent = y;
        y->flags &= SOF_RBTREE_USER_MASK;
        y->flags |= node->flags & SOF_RBTREE_COLOR_MASK;
    }
    if (tree->del_func != NULL) {
        tree->del_func(w, tree->ctx);
    }
    sof_alloc_free(&tree->alloc, w, sizeof(sof_rbtree_node_t));
    if (color == 0) {
        delete_fixup(tree, x);
    }
    tree->count--;
    return SOF_RBTREE_OK;
}

sof_rbtree_error_t sof_rbtree_free(sof_rbtree_t *tree) {
    if (tree == NULL) {
        return SOF_RBTREE_ERROR;
    }
    sof_rbtree_clear(tree, NULL);
    memset(tree, 0, sizeof(sof_rbtree_t));
    return SOF_RBTREE_OK;
}

void **sof_rbtree_get_keys(const sof_rbtree_t *tree, size_t *count) {
    void **keys;
    // The path from the root to the current node, which is never longer
    // than the tree is high.
    const sof_rbtree_node_t *stack[SOF_RBTREE_MAX_HEIGHT];
    const sof_rbtree_node_t *n;
    size_t i = 0;
    size_t j = 0;
    if (tree == NULL) {
        return NULL;
    }
    size_t size = (tree->count + 1) * sizeof(void *);
    keys = sof_alloc_malloc(&tree->alloc, size);
    if (keys == NULL) {
        return NULL;
    }
    // A freed tree has a NULL root; treat it as empty.
    n = tree->root == NULL ? &tree->nil_node : tree->root;
    while (n != &tree->nil_node || i > 0) {
        if (n != &tree->nil_node) {
            stack[i++] = n;
            n = n->left;
        } else {
            n = stack[--i];
            keys[j++] = n->key;
            n = n->right;
        }
    }
    keys[j] = NULL;
    if (count != NULL) {
        *count = j;
    }
    return keys;
}

sof_rbtree_node_t *sof_rbtree_insert(sof_rbtree_t *tree, void *key, int *created) {
    sof_rbtree_node_t *child;
    sof_rbtree_node_t *parent;
    sof_rbtree_node_t *node = NULL;
    uint32_t color = SOF_RBTREE_COLOR_RED;
    int i = 0;
    if (created != NULL) {
        *created = 0;
    }
    if (tree == NULL || tree->root == NULL) {
        return NULL;
    }
    child = tree->root;
    parent = &tree->nil_node;
    while (child != &tree->nil_node) {
        parent = child;
        i = tree->cmp_func(key, child->key, tree->ctx);
        if (i < 0) {
            child = child->left;
        } else if (i == 0) {
            return child;
        } else {
            child = child->right;
        }
    }
    node = sof_alloc_malloc(&tree->alloc, sizeof(sof_rbtree_node_t));
    if (node == NULL) {
        return NULL;
    }
    if (parent == &tree->nil_node) {
        tree->root = node;
        color = SOF_RBTREE_COLOR_BLACK;
    } else if (i < 0) {
        parent->left = node;
    } else {
        parent->right = node;
    }
    node->parent = parent;
    node->left = &tree->nil_node;
    node->right = &tree->nil_node;
    node->flags = color;
    node->key = key;
    node->data = NULL;
    insert_fixup(tree, node);
    tree->count++;
    if (created != NULL) {
        *created = 1;
    }
    return node;
}

sof_rbtree_node_t *sof_rbtree_find(const sof_rbtree_t *tree, const void *key) {
    if (tree == NULL || tree->root == NULL) {
        return NULL;
    }
    sof_rbtree_node_t *node = tree->root;
    while (node != &tree->nil_node) {
        int i = tree->cmp_func(key, node->key, tree->ctx);
        if (i == 0) {
            return node;
        } else if (i < 0) {
            node = node->left;
        } else {
            node = node->right;
        }
    }
    return NULL;
}

sof_rbtree_node_t *sof_rbtree_maximum(const sof_rbtree_t *tree, const sof_rbtree_node_t *subtree) {
    if (tree == NULL || tree->root == NULL) {
        return NULL;
    }
    if (subtree == NULL) {
        subtree = tree->root;
    }
    if (subtree == &tree->nil_node) {
        return NULL;
    }
    while (subtree->right != &tree->nil_node) {
        subtree = subtree->right;
    }
    return mutable_node(subtree);
}

sof_rbtree_node_t *sof_rbtree_minimum(const sof_rbtree_t *tree, const sof_rbtree_node_t *subtree) {
    if (tree == NULL || tree->root == NULL) {
        return NULL;
    }
    if (subtree == NULL) {
        subtree = tree->root;
    }
    if (subtree == &tree->nil_node) {
        return NULL;
    }
    while (subtree->left != &tree->nil_node) {
        subtree = subtree->left;
    }
    return mutable_node(subtree);
}

sof_rbtree_error_t sof_rbtree_init(sof_rbtree_t *tree, sof_rbtree_key_compare_func_t cmp_func, sof_rbtree_node_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc) {
    if (tree == NULL) {
        return SOF_RBTREE_ERROR;
    }
    memset(tree, 0, sizeof(sof_rbtree_t));
    if (cmp_func == NULL || !sof_alloc_valid(alloc)) {
        return SOF_RBTREE_ERROR;
    }
    if (alloc != NULL) {
        tree->alloc = *alloc;
    }
    tree->root = &tree->nil_node;
    tree->cmp_func = cmp_func;
    tree->del_func = del_func;
    tree->ctx = ctx;
    tree->nil_node.parent = tree->nil_node.left = tree->nil_node.right = &tree->nil_node;
    tree->nil_node.flags = SOF_RBTREE_COLOR_BLACK;
    return SOF_RBTREE_OK;
}

sof_rbtree_node_t *sof_rbtree_next(const sof_rbtree_t *tree, const sof_rbtree_node_t *node) {
    sof_rbtree_node_t *parent;
    if (tree == NULL || tree->root == NULL || node == NULL || node == &tree->nil_node) {
        return NULL;
    }
    if (node->right != &tree->nil_node) {
        return sof_rbtree_minimum(tree, node->right);
    }
    // Climb until we come up from a left child; that parent is next.
    parent = node->parent;
    while (parent != &tree->nil_node && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent == &tree->nil_node ? NULL : parent;
}

sof_rbtree_node_t *sof_rbtree_prev(const sof_rbtree_t *tree, const sof_rbtree_node_t *node) {
    sof_rbtree_node_t *parent;
    if (tree == NULL || tree->root == NULL || node == NULL || node == &tree->nil_node) {
        return NULL;
    }
    if (node->left != &tree->nil_node) {
        return sof_rbtree_maximum(tree, node->left);
    }
    // Climb until we come up from a right child; that parent is previous.
    parent = node->parent;
    while (parent != &tree->nil_node && node == parent->left) {
        node = parent;
        parent = parent->parent;
    }
    return parent == &tree->nil_node ? NULL : parent;
}

int sof_rbtree_traverse_ascending(const sof_rbtree_t *tree, const sof_rbtree_node_t *subtree, sof_rbtree_traverse_func_t cb, void *ctx) {
    int i;
    if (tree == NULL) {
        return -1;
    }
    if (tree->root == NULL) {
        return 0;
    }
    if (subtree == NULL) {
        subtree = tree->root;
    }
    if (subtree == &tree->nil_node) {
        return 0;
    }
    if (subtree->left != &tree->nil_node) {
        i = sof_rbtree_traverse_ascending(tree, subtree->left, cb, ctx);
        if (i != 0) {
            return i;
        }
    }
    if (cb != NULL) {
        i = cb(mutable_node(subtree), ctx);
        if (i != 0) {
            return i;
        }
    }
    if (subtree->right != &tree->nil_node) {
        i = sof_rbtree_traverse_ascending(tree, subtree->right, cb, ctx);
        if (i != 0) {
            return i;
        }
    }
    return 0;
}

int sof_rbtree_traverse_descending(const sof_rbtree_t *tree, const sof_rbtree_node_t *subtree, sof_rbtree_traverse_func_t cb, void *ctx) {
    int i;
    if (tree == NULL) {
        return -1;
    }
    if (tree->root == NULL) {
        return 0;
    }
    if (subtree == NULL) {
        subtree = tree->root;
    }
    if (subtree == &tree->nil_node) {
        return 0;
    }
    if (subtree->right != &tree->nil_node) {
        i = sof_rbtree_traverse_descending(tree, subtree->right, cb, ctx);
        if (i != 0) {
            return i;
        }
    }
    if (cb != NULL) {
        i = cb(mutable_node(subtree), ctx);
        if (i != 0) {
            return i;
        }
    }
    if (subtree->left != &tree->nil_node) {
        i = sof_rbtree_traverse_descending(tree, subtree->left, cb, ctx);
        if (i != 0) {
            return i;
        }
    }
    return 0;
}

static void delete_fixup(sof_rbtree_t *tree, sof_rbtree_node_t *x) {
    sof_rbtree_node_t *w;
    while (x != tree->root && SOF_RBTREE_COLOR_IS_BLACK(x)) {
        if (x == x->parent->left) {
            w = x->parent->right;
            if (SOF_RBTREE_COLOR_IS_RED(w)) {
                SOF_RBTREE_SET_BLACK(w);
                SOF_RBTREE_SET_RED(x->parent);
                rotate_left(tree, x->parent);
                w = x->parent->right;
            }
            if (SOF_RBTREE_COLOR_IS_BLACK(w->left) && SOF_RBTREE_COLOR_IS_BLACK(w->right)) {
                SOF_RBTREE_SET_RED(w);
                x = x->parent;
            } else {
                if (SOF_RBTREE_COLOR_IS_BLACK(w->right)) {
                    SOF_RBTREE_SET_RED(w);
                    SOF_RBTREE_SET_BLACK(w->left);
                    rotate_right(tree, w);
                    w = x->parent->right;
                }
                w->flags &= SOF_RBTREE_USER_MASK;
                w->flags |= x->parent->flags & SOF_RBTREE_COLOR_MASK;
                SOF_RBTREE_SET_BLACK(x->parent);
                SOF_RBTREE_SET_BLACK(w->right);
                rotate_left(tree, x->parent);
                x = tree->root;
            }
        } else {
            w = x->parent->left;
            if (SOF_RBTREE_COLOR_IS_RED(w)) {
                SOF_RBTREE_SET_BLACK(w);
                SOF_RBTREE_SET_RED(x->parent);
                rotate_right(tree, x->parent);
                w = x->parent->left;
            }
            if (SOF_RBTREE_COLOR_IS_BLACK(w->left) && SOF_RBTREE_COLOR_IS_BLACK(w->right)) {
                SOF_RBTREE_SET_RED(w);
                x = x->parent;
            } else {
                if (SOF_RBTREE_COLOR_IS_BLACK(w->left)) {
                    SOF_RBTREE_SET_RED(w);
                    SOF_RBTREE_SET_BLACK(w->right);
                    rotate_left(tree, w);
                    w = x->parent->left;
                }
                w->flags &= SOF_RBTREE_USER_MASK;
                w->flags |= x->parent->flags & SOF_RBTREE_COLOR_MASK;
                SOF_RBTREE_SET_BLACK(x->parent);
                SOF_RBTREE_SET_BLACK(w->left);
                rotate_right(tree, x->parent);
                x = tree->root;
            }
        }
    }
    SOF_RBTREE_SET_BLACK(x);
}

/// Lists a subtree's nodes in order into nodes (when not NULL), starting at
/// index i, and returns the index after the last one: called with NULL and 0,
/// it counts the subtree's nodes.
static size_t collect_subtree(sof_rbtree_t *tree, sof_rbtree_node_t *node, sof_rbtree_node_t **nodes, size_t i) {
    if (node == &tree->nil_node) {
        return i;
    }
    i = collect_subtree(tree, node->left, nodes, i);
    if (nodes != NULL) {
        nodes[i] = node;
    }
    return collect_subtree(tree, node->right, nodes, i + 1);
}

/// Frees every node of the subtree rooted at node, without rebalancing; only
/// for deleting the whole tree.
static void delete_subtree(sof_rbtree_t *tree, sof_rbtree_node_t *node) {
    if (node->left != &tree->nil_node) {
        delete_subtree(tree, node->left);
    }
    if (node->right != &tree->nil_node) {
        delete_subtree(tree, node->right);
    }
    if (tree->del_func != NULL) {
        tree->del_func(node, tree->ctx);
    }
    sof_alloc_free(&tree->alloc, node, sizeof(sof_rbtree_node_t));
    tree->count--;
}

static void insert_fixup(sof_rbtree_t *tree, sof_rbtree_node_t *node) {
    while (SOF_RBTREE_COLOR_IS_RED(node->parent)) {
        sof_rbtree_node_t *parent = node->parent;
        sof_rbtree_node_t *grandparent = parent->parent;
        if (parent == grandparent->left) {
            if (SOF_RBTREE_COLOR_IS_RED(grandparent->right)) {
                SOF_RBTREE_SET_BLACK(parent);
                SOF_RBTREE_SET_BLACK(grandparent->right);
                SOF_RBTREE_SET_RED(grandparent);
                node = grandparent;
            } else {
                if (node == parent->right) {
                    node = parent;
                    rotate_left(tree, node);
                }
                SOF_RBTREE_SET_BLACK(node->parent);
                SOF_RBTREE_SET_RED(node->parent->parent);
                rotate_right(tree, node->parent->parent);
            }
        } else {
            if (SOF_RBTREE_COLOR_IS_RED(grandparent->left)) {
                SOF_RBTREE_SET_BLACK(parent);
                SOF_RBTREE_SET_BLACK(grandparent->left);
                SOF_RBTREE_SET_RED(grandparent);
                node = grandparent;
            } else {
                if (node == parent->left) {
                    node = parent;
                    rotate_right(tree, node);
                }
                SOF_RBTREE_SET_BLACK(node->parent);
                SOF_RBTREE_SET_RED(node->parent->parent);
                rotate_left(tree, node->parent->parent);
            }
        }
    }
    SOF_RBTREE_SET_BLACK(tree->root);
    tree->nil_node.left = tree->nil_node.right = tree->nil_node.parent = &tree->nil_node;
    tree->nil_node.flags = SOF_RBTREE_COLOR_BLACK;
}

/// @brief Returns a node of a tree the caller only reads as one it may
/// change, as strchr() does with its string: functions that only read the
/// tree take const nodes, but the application owns each node's data and
/// user flags, so the nodes they hand back aren't const.
static sof_rbtree_node_t *mutable_node(const sof_rbtree_node_t *node) {
    return (sof_rbtree_node_t *)node;
}

static void rotate_left(sof_rbtree_t *tree, sof_rbtree_node_t *x) {
    sof_rbtree_node_t *y = x->right;
    x->right = y->left;
    if (y->left != &tree->nil_node) {
        y->left->parent = x;
    }
    y->parent = x->parent;
    if (tree->root == x) {
        tree->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
}

static void rotate_right(sof_rbtree_t *tree, sof_rbtree_node_t *x) {
    sof_rbtree_node_t *y = x->left;
    x->left = y->right;
    if (y->right != &tree->nil_node) {
        y->right->parent = x;
    }
    y->parent = x->parent;
    if (tree->root == x) {
        tree->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->right = x;
    x->parent = y;
}

static void transplant(sof_rbtree_t *tree, sof_rbtree_node_t *u, sof_rbtree_node_t *v) {
    if (u->parent == &tree->nil_node) {
        tree->root = v;
    } else if (u == u->parent->left) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }
    v->parent = u->parent;
}
