/**
 * @file sof_rbtree.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Global header file for the red-black tree library.
 * @version 0.0.1
 * @date 2026-11-02
 * 
 * @copyright Copyright (c) 2026, Warren Mann
 */

#ifndef SOF_RBTREE_H
#define SOF_RBTREE_H

#include <stddef.h>
#include <stdint.h>

#include "sof_alloc.h"

/**
 * @brief Masks and macros for a node's flags field.
 * @details The high-order bit tracks the node's color. The remaining bits can
 * be used by the application for whatever it wants and won't be modified by
 * the sof_rbtree code. This user data can be accessed with SOF_RBTREE_SET_USER(n, d),
 * SOF_RBTREE_GET_USER(n) and the SOF_RBTREE_USER_MASK bit mask.
 */
#define SOF_RBTREE_COLOR_MASK           0x80000000
#define SOF_RBTREE_USER_MASK            0x7fffffff
#define SOF_RBTREE_COLOR_BLACK          0x00000000
#define SOF_RBTREE_COLOR_RED            0x80000000
#define SOF_RBTREE_COLOR(n)             ((n)->flags & SOF_RBTREE_COLOR_MASK)
#define SOF_RBTREE_COLOR_IS_BLACK(n)    (SOF_RBTREE_COLOR(n) == SOF_RBTREE_COLOR_BLACK)
#define SOF_RBTREE_COLOR_IS_RED(n)      (SOF_RBTREE_COLOR(n) == SOF_RBTREE_COLOR_RED)
#define SOF_RBTREE_SET_USER(n, d)       ((n)->flags = ((n)->flags & SOF_RBTREE_COLOR_MASK) | ((uint32_t)(d) & SOF_RBTREE_USER_MASK))
#define SOF_RBTREE_GET_USER(n)          ((n)->flags & SOF_RBTREE_USER_MASK)

/**
 * @brief Error codes for the sof_rbtree functions.
 */
typedef enum sof_rbtree_error_t {
    SOF_RBTREE_OK = 0,
    SOF_RBTREE_NO_MEM,
    SOF_RBTREE_NO_MATCH,
    SOF_RBTREE_ERROR,
} sof_rbtree_error_t;

/**
 * @brief A red-black tree node.
 * @details The data field is not referenced by the sof_rbtree code in any way.
 * The parent, left and right fields should not be altered by the
 * application. The key field is set and used by the sof_rbtree code internally,
 * but it is the application's responsibility to free the key in the
 * sof_rbtree_node_delete_func_t that is called just before a node is removed (if
 * necessary).
 */
typedef struct sof_rbtree_node_t {
    /// @brief This node's parent in the red-black tree.
    struct sof_rbtree_node_t *parent;
    /// @brief Subtree with keys having a lower ordinal value than this node.
    struct sof_rbtree_node_t *left;
    /// @brief Subtree with keys having a higher ordinal value than this node.
    struct sof_rbtree_node_t *right;
    /// @brief Color bit and application flags.
    /// @details The highest-order bit is used for red-black tracking. The
    /// other bits are zeroed when the node is created, then never again
    /// altered by the sof_rbtree code, and may be used by the application.
    uint32_t flags;
    /// @brief The key value used to order the nodes.
    void *key;
    /// @brief Application data.
    void *data;
} sof_rbtree_node_t;

/**
 * @brief Compares two node keys.
 * @details Required for ordering the nodes. It should return a value less
 * than 0 if a < b, 0 if a == b, or a value greater than 0 if a > b. ctx is
 * the ctx given to sof_rbtree_init().
 */
typedef int (*sof_rbtree_key_compare_func_t)(const void *a, const void *b, void *ctx);

/**
 * @brief Receives a node just before it is removed.
 * @details Called by sof_rbtree_remove(), sof_rbtree_remove_node(), sof_rbtree_clear()
 * and sof_rbtree_free() for each node they remove, so the application can free
 * the node's key and data, if necessary. It must not call into the tree. ctx
 * is the ctx given to sof_rbtree_init().
 */
typedef void (*sof_rbtree_node_delete_func_t)(sof_rbtree_node_t *node, void *ctx);

/**
 * @brief Called for each node visited during a traversal.
 * @details Return 0 to continue, or a non-zero value to stop the traversal
 * early. It must not insert into or remove from the tree. ctx is the ctx
 * given to the traversal function.
 */
typedef int (*sof_rbtree_traverse_func_t)(sof_rbtree_node_t *node, void *ctx);

/**
 * @brief A red-black tree.
 * @details Tracks the tree root (which will change as nodes are added), the
 * key comparison and node delete functions and the context pointer passed to
 * them, the number of nodes, a special nil node that is used internally for
 * tree maintenance, and a copy of the allocator given to sof_rbtree_init()
 * (zeroed, meaning the standard functions, if it was given none). These
 * fields can be read, but should not be changed by the application.
 *
 * The application provides the sof_rbtree_t and sets it up with sof_rbtree_init().
 * Nodes point to its nil_node, so it must not be moved or copied while it
 * holds nodes.
 *
 * Every function accepts a NULL tree, and a tree that was freed or whose
 * sof_rbtree_init() failed (root is NULL), as described for each.
 *
 * sof_rbtree_find(), sof_rbtree_get_keys(), sof_rbtree_maximum(),
 * sof_rbtree_minimum(), sof_rbtree_next(), sof_rbtree_prev() and the
 * traversals only read the tree, and take a const tree and const nodes. The
 * nodes they return, or pass to the callback, are not const, since the
 * application may change a node's data and user flags.
 */
typedef struct sof_rbtree_t {
    /// @brief The root of the red-black tree.
    sof_rbtree_node_t *root;
    /// @brief Callback function for comparing key values.
    sof_rbtree_key_compare_func_t cmp_func;
    /// @brief Callback letting the application free a node's key and data.
    /// @details Can be NULL if nothing needs to be freed.
    sof_rbtree_node_delete_func_t del_func;
    /// @brief Application context passed to cmp_func and del_func.
    void *ctx;
    /// @brief NIL node used by the red-black tree code.
    sof_rbtree_node_t nil_node;
    /// @brief The number of nodes in the tree.
    size_t count;
    /// @brief The memory functions nodes are allocated and freed with.
    sof_alloc_t alloc;
} sof_rbtree_t;

/**
 * @brief Remove every node in a subtree, or in the whole tree.
 * @details If subtree is NULL or the root, every node in the tree is removed,
 * leaving an empty tree that can still be used. An empty subtree (a node's
 * missing child, which is &tree->nil_node) removes nothing. Otherwise the
 * subtree's nodes are removed one at a time, so the rest of the tree stays
 * balanced; this allocates a temporary array of node pointers. Each node goes
 * to the del_func, if there is one, before it is removed.
 * @param tree The sof_rbtree containing the subtree to be removed.
 * @param subtree The subtree to be removed. If NULL, the entire tree is
 * removed.
 * @return SOF_RBTREE_OK, SOF_RBTREE_ERROR if tree is NULL, or SOF_RBTREE_NO_MEM if memory
 * for removing a partial subtree couldn't be allocated, in which case the
 * tree is unchanged.
 */
extern sof_rbtree_error_t sof_rbtree_clear(sof_rbtree_t *tree, sof_rbtree_node_t *subtree);

/**
 * @brief Find the node with the given key.
 * @details The cmp_func is used to find the key in the tree.
 * @param tree The sof_rbtree to be searched.
 * @param key The key value to be searched for.
 * @return A pointer to the node with the matching key, or NULL if no such
 * node exists in the tree or tree is NULL.
 */
extern sof_rbtree_node_t *sof_rbtree_find(const sof_rbtree_t *tree, const void *key);

/**
 * @brief Remove every node and release the tree's memory.
 * @details Each node goes to the del_func, if there is one, before it is
 * removed. The sof_rbtree_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like a tree whose sof_rbtree_init() failed until it is
 * initialized again.
 * @param tree The sof_rbtree to be freed.
 * @return SOF_RBTREE_OK, or SOF_RBTREE_ERROR if tree is NULL.
 */
extern sof_rbtree_error_t sof_rbtree_free(sof_rbtree_t *tree);

/**
 * @brief Return an array of the tree's keys in ascending order.
 * @details The keys themselves are not copied. The array is allocated with
 * the tree's allocator, and the caller frees it with
 * sof_alloc_free(&tree->alloc, keys, (count + 1) * sizeof(void *)). The array has a NULL after the last
 * key, but a key can itself be NULL (or 0 stored as a pointer), so use count
 * to find its end. The array is its only allocation: the walk keeps its path
 * on the c stack (at most 2 * log2(count + 1) nodes, 128 pointers of room on
 * a 64-bit system). It only reads the tree, so readers can call it at once,
 * but it allocates, so the tree's allocator must then be safe to call from
 * several threads.
 * @param tree The sof_rbtree for which the keys will be returned.
 * @param count If not NULL, set to the number of keys in the array.
 * @return NULL on error, otherwise a pointer to the array of keys (just the
 * terminating NULL for an empty tree).
 */
extern void **sof_rbtree_get_keys(const sof_rbtree_t *tree, size_t *count);

/**
 * @brief Initialize a red-black tree in an sof_rbtree_t the caller provides.
 * @details The cmp_func is required; see sof_rbtree_key_compare_func_t. The
 * del_func is optional and is called with each node just before it is
 * removed, so the caller can free the node's key and data. ctx is passed to
 * every call of cmp_func and del_func, so they can use application state
 * without globals; it can be NULL.
 * @param tree The sof_rbtree to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap, but must not move while it holds nodes.
 * @param cmp_func The key comparison function.
 * @param del_func The node delete function, or NULL.
 * @param ctx The context pointer passed to cmp_func and del_func.
 * @param alloc The memory functions for nodes and the arrays sof_rbtree_clear()
 * and sof_rbtree_get_keys() allocate, or NULL for the standard functions. The
 * tree keeps a copy, so it can be on the stack.
 * @return SOF_RBTREE_OK, or SOF_RBTREE_ERROR if tree or cmp_func is NULL, or alloc
 * has a malloc but no free, in which case the tree is left empty with a NULL
 * root.
 */
extern sof_rbtree_error_t sof_rbtree_init(sof_rbtree_t *tree, sof_rbtree_key_compare_func_t cmp_func, sof_rbtree_node_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc);

/**
 * @brief Insert a node with the given key, or find the one already there.
 * @details The cmp_func is called to compare the key with the keys of other
 * nodes to find where the node belongs. If a node with this key is already in
 * the tree, no new node is created and that node is returned; the tree keeps
 * its original key, so the caller still owns the key it passed (free it if
 * necessary).
 * @param tree The sof_rbtree into which the node is to be inserted.
 * @param key The key value to be inserted.
 * @param created If not NULL, set to 1 if a new node was created, or 0 if the
 * key was already in the tree or the insert failed.
 * @return A pointer to the newly inserted node, whose data is NULL, or the
 * node with the matching key if it already exists in the tree. NULL if tree
 * is NULL or memory for the node couldn't be allocated.
 */
extern sof_rbtree_node_t *sof_rbtree_insert(sof_rbtree_t *tree, void *key, int *created);

/**
 * @brief Return the node with the highest key in a subtree.
 * @param tree The sof_rbtree containing the subtree to be searched.
 * @param subtree The subtree to be searched. If NULL, the entire tree is
 * searched.
 * @return A pointer to the node with the highest key in the subtree, or NULL
 * if the subtree is empty.
 */
extern sof_rbtree_node_t *sof_rbtree_maximum(const sof_rbtree_t *tree, const sof_rbtree_node_t *subtree);

/**
 * @brief Return the node with the lowest key in a subtree.
 * @param tree The sof_rbtree containing the subtree to be searched.
 * @param subtree The subtree to be searched. If NULL, the entire tree is
 * searched.
 * @return A pointer to the node with the lowest key in the subtree, or NULL
 * if the subtree is empty.
 */
extern sof_rbtree_node_t *sof_rbtree_minimum(const sof_rbtree_t *tree, const sof_rbtree_node_t *subtree);

/**
 * @brief Return the node with the next higher key after node.
 * @details Walks the tree in ascending order without a callback:
 * @code
 * for (n = sof_rbtree_minimum(tree, NULL); n; n = sof_rbtree_next(tree, n))
 * @endcode
 * Removing the current node while walking invalidates it, so get the next
 * node first.
 * @param tree The sof_rbtree containing node.
 * @param node The node to start from.
 * @return The next node, or NULL if node has the highest key (or is NULL).
 */
extern sof_rbtree_node_t *sof_rbtree_next(const sof_rbtree_t *tree, const sof_rbtree_node_t *node);

/**
 * @brief Return the node with the next lower key before node.
 * @details Walks the tree in descending order without a callback:
 * @code
 * for (n = sof_rbtree_maximum(tree, NULL); n; n = sof_rbtree_prev(tree, n))
 * @endcode
 * Removing the current node while walking invalidates it, so get the
 * previous node first.
 * @param tree The sof_rbtree containing node.
 * @param node The node to start from.
 * @return The previous node, or NULL if node has the lowest key (or is NULL).
 */
extern sof_rbtree_node_t *sof_rbtree_prev(const sof_rbtree_t *tree, const sof_rbtree_node_t *node);

/**
 * @brief Remove the node with the given key.
 * @details The node goes to the del_func, if there is one, before it is
 * removed.
 * @param tree The sof_rbtree to remove the node from.
 * @param key The key of the node to remove.
 * @return SOF_RBTREE_OK, SOF_RBTREE_NO_MATCH if the key isn't in the tree, or
 * SOF_RBTREE_ERROR if tree is NULL.
 */
extern sof_rbtree_error_t sof_rbtree_remove(sof_rbtree_t *tree, const void *key);

/**
 * @brief Remove a node the caller already has, such as one from
 * sof_rbtree_find() or sof_rbtree_next().
 * @details Like sof_rbtree_remove(), without looking the key up again. Other
 * nodes don't move, so pointers to them stay good.
 * @param tree The sof_rbtree containing the node.
 * @param node The node to remove.
 * @return SOF_RBTREE_OK, or SOF_RBTREE_ERROR if tree or node is NULL, node is the
 * tree's nil_node, or the tree is empty.
 */
extern sof_rbtree_error_t sof_rbtree_remove_node(sof_rbtree_t *tree, sof_rbtree_node_t *node);

/**
 * @brief Visit a subtree's nodes from lowest key to highest.
 * @details cb is called with each node visited, unless it is NULL, in which
 * case this function is less than useful. If cb returns a non-zero value,
 * the traversal stops and returns that value.
 * @param tree The sof_rbtree containing the subtree to be traversed.
 * @param subtree The subtree to be traversed. If NULL, the entire tree is
 * traversed.
 * @param cb The callback function to be called for each node visited.
 * @param ctx The context pointer passed to every call of cb.
 * @return 0 if every node was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if tree is NULL. An empty tree or subtree (a
 * node's missing child) visits nothing and returns 0.
 */
extern int sof_rbtree_traverse_ascending(const sof_rbtree_t *tree, const sof_rbtree_node_t *subtree, sof_rbtree_traverse_func_t cb, void *ctx);

/**
 * @brief Visit a subtree's nodes from highest key to lowest.
 * @details cb is called with each node visited, unless it is NULL, in which
 * case this function is less than useful. If cb returns a non-zero value,
 * the traversal stops and returns that value.
 * @param tree The sof_rbtree containing the subtree to be traversed.
 * @param subtree The subtree to be traversed. If NULL, the entire tree is
 * traversed.
 * @param cb The callback function to be called for each node visited.
 * @param ctx The context pointer passed to every call of cb.
 * @return 0 if every node was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if tree is NULL. An empty tree or subtree (a
 * node's missing child) visits nothing and returns 0.
 */
extern int sof_rbtree_traverse_descending(const sof_rbtree_t *tree, const sof_rbtree_node_t *subtree, sof_rbtree_traverse_func_t cb, void *ctx);

#endif // SOF_RBTREE_H
