/**
 * @file sof_lru.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A least-recently-used cache in C.
 * @details A fixed number of entries, found through a sof_hashmap and kept
 * in a list from the most to the least recently used. Inserting a new key
 * into a full cache evicts the least recently used entry.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_LRU_H
#define SOF_LRU_H

#include <stddef.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"

/**
 * @brief Error codes for the cache functions.
 */
typedef enum sof_lru_error_t {
    SOF_LRU_OK = 0,
    SOF_LRU_NO_MEM,
    SOF_LRU_NO_MATCH,
    SOF_LRU_ERROR,
} sof_lru_error_t;

/**
 * @brief An entry in the cache.
 * @details key is the pointer the cache's map stores: the copy made by the
 * copy key callback, or the caller's own pointer if there is none. data is
 * never touched by the cache; it belongs to the application. newer and older
 * link the entries from the most to the least recently used, and are NULL at
 * either end. hash is the hash the map stored for the key (the hash field of
 * its sof_hashmap_element_t), kept so that evicting or removing the entry
 * finds its map element without hashing the key again.
 *
 * Nodes live in an array allocated by sof_lru_init(), so a pointer to one
 * stays good until its entry is removed or evicted, or the cache is cleared
 * or freed. The fields can be read, but only data should be changed by the
 * application.
 */
typedef struct sof_lru_node_t {
    void *key;
    void *data;
    struct sof_lru_node_t *newer;
    struct sof_lru_node_t *older;
    size_t hash;
} sof_lru_node_t;

/**
 * @brief Receives an entry just before it is removed.
 * @details Called by sof_lru_insert() for the entry it evicts, and by
 * sof_lru_remove(), sof_lru_remove_node(), sof_lru_clear() and
 * sof_lru_free() for each entry they remove, so the application can free the
 * entry's data, and its key if the cache has no free key callback (which is
 * called after this). It must not call into the cache. ctx is the ctx given
 * to sof_lru_init().
 */
typedef void (*sof_lru_node_delete_func_t)(sof_lru_node_t *node, void *ctx);

/**
 * @brief Called for each entry visited by sof_lru_traverse().
 * @details Return 0 to continue, or a non-zero value to stop the traversal
 * early. It must not insert into or remove from the cache, or find in it,
 * since sof_lru_find() reorders it. ctx is the ctx given to
 * sof_lru_traverse().
 */
typedef int (*sof_lru_traverse_func_t)(sof_lru_node_t *node, void *ctx);

/**
 * @brief A least-recently-used cache.
 * @details capacity is the most entries the cache holds, and count the number
 * it holds now. nodes is the array of capacity nodes, newest and oldest the
 * ends of the list of entries in use, and spare the list of unused nodes,
 * linked through their older field. map finds a key's node: each map
 * element's data is the node holding its key. del_func and ctx are those
 * given to sof_lru_init(), and alloc a copy of its allocator (zeroed,
 * meaning the standard functions, if it was given none). These fields can be
 * read, but should not be changed by the application.
 *
 * sof_lru_init() must be called before using a cache, and sof_lru_free()
 * should be called when the cache is no longer needed. sof_lru_clear()
 * empties it without freeing it.
 *
 * Every function accepts a NULL cache or key, and a cache that was freed or
 * whose sof_lru_init() failed (nodes is NULL), as described for each.
 *
 * The cache does no locking. sof_lru_peek(), sof_lru_newest(),
 * sof_lru_oldest(), sof_lru_next(), sof_lru_prev() and sof_lru_traverse()
 * only read it (provided the hash and equal key callbacks only read), and so
 * take a const cache; the entries they return, or pass to the callback, are
 * not const, since the application may change their data. Every other
 * function writes it, sof_lru_find() included, since it moves the entry it
 * finds to the front.
 */
typedef struct sof_lru_t {
    size_t capacity;
    size_t count;
    sof_lru_node_t *nodes;
    sof_lru_node_t *newest;
    sof_lru_node_t *oldest;
    sof_lru_node_t *spare;
    sof_hashmap_t map;
    sof_lru_node_delete_func_t del_func;
    void *ctx;
    sof_alloc_t alloc;
} sof_lru_t;

/**
 * @brief Removes every entry, leaving an empty cache that can still be used.
 * @details Each entry goes to the del_func, then its key to the free key
 * callback, if there are any, from the most to the least recently used.
 * @param lru The cache to empty.
 * @return SOF_LRU_OK, or SOF_LRU_ERROR if lru is NULL.
 */
extern sof_lru_error_t sof_lru_clear(sof_lru_t *lru);

/**
 * @brief Finds the entry with the given key and makes it the most recently
 * used.
 * @details Takes a non-const cache because moving the entry to the front of
 * the list writes it; sof_lru_peek() finds an entry in a const cache
 * without moving it.
 * @param lru The cache to search.
 * @param key The key to search for. The cache doesn't keep it.
 * @return The entry holding the key, or NULL if the key is not found or lru,
 * its nodes or key is NULL.
 */
extern sof_lru_node_t *sof_lru_find(sof_lru_t *lru, const void *key);

/**
 * @brief Removes every entry and frees the cache's memory.
 * @details Each entry goes to the del_func, then its key to the free key
 * callback, if there are any. The sof_lru_t itself isn't freed (it belongs
 * to the caller); it is left empty, and acts like a cache whose
 * sof_lru_init() failed until it is initialized again.
 * @param lru The cache to free.
 * @return SOF_LRU_OK, or SOF_LRU_ERROR if lru is NULL.
 */
extern sof_lru_error_t sof_lru_free(sof_lru_t *lru);

/**
 * @brief Initializes a cache in a sof_lru_t the caller provides.
 * @details Allocates all capacity nodes and a map with room for them, so
 * inserting never allocates, apart from the copy key callback.
 * @param lru The cache to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap.
 * @param keys The key callbacks, as sof_hashmap_init() takes them, such as
 * &sof_hashmap_string_keys. The cache keeps a copy, so they can be on the
 * stack. NULL tells keys apart by identity.
 * @param del_func Called with each entry just before it is removed or
 * evicted; see sof_lru_node_delete_func_t. Can be NULL.
 * @param ctx Passed to del_func and every key callback. Can be NULL.
 * @param capacity The most entries the cache holds. Must not be 0.
 * @param alloc The memory functions the cache uses, and passes to the key
 * callbacks, or NULL for the standard functions. The cache keeps a copy, so
 * it can be on the stack.
 * @return SOF_LRU_OK, SOF_LRU_ERROR if lru is NULL, capacity is 0 or alloc
 * has a malloc but no free, or SOF_LRU_NO_MEM; on an error, the cache is
 * left empty with a NULL nodes array.
 */
extern sof_lru_error_t sof_lru_init(sof_lru_t *lru, const sof_hashmap_key_callbacks_t *keys, sof_lru_node_delete_func_t del_func, void *ctx, size_t capacity, const sof_alloc_t *alloc);

/**
 * @brief Inserts an entry with the given key, or finds the one already
 * there, and makes it the most recently used.
 * @details If the key is already present, nothing is inserted and the
 * existing entry is returned. Otherwise, if the cache is full, the least
 * recently used entry is evicted: it goes to the del_func, then its key to
 * the free key callback, if there are any. The new entry holds the key's
 * copy, if there is a copy key callback, or the key pointer itself. If the
 * insert fails, the cache is left as it was and nothing is evicted.
 * @param lru The cache to insert into.
 * @param key The key to insert. Without a copy key callback, the cache
 * stores this pointer, so it must stay valid while the key is in the cache,
 * and the application frees it (in del_func, for example) unless there is a
 * free key callback. If the key already exists, the cache keeps the existing
 * entry's key, so the caller still owns this one.
 * @param created If not NULL, set to 1 if a new entry was created, or 0 if
 * the key already existed or the insert failed.
 * @return The new entry, whose data is NULL, or the existing one. NULL if the
 * copy key callback failed, or lru, its nodes or key is NULL.
 */
extern sof_lru_node_t *sof_lru_insert(sof_lru_t *lru, const void *key, int *created);

/**
 * @brief Returns the most recently used entry.
 * @param lru The cache.
 * @return The entry, or NULL if the cache is empty or lru is NULL.
 */
extern sof_lru_node_t *sof_lru_newest(const sof_lru_t *lru);

/**
 * @brief Returns the next entry after node, from the most to the least
 * recently used.
 * @details Walks the cache without a callback:
 * @code
 * for (n = sof_lru_newest(lru); n; n = sof_lru_next(lru, n))
 * @endcode
 * Removing the current entry, or finding or inserting any, changes the order,
 * so get the next entry first.
 * @param lru The cache holding node.
 * @param node The entry to start from.
 * @return The next less recently used entry, or NULL if node is the oldest,
 * or lru or node is NULL.
 */
extern sof_lru_node_t *sof_lru_next(const sof_lru_t *lru, const sof_lru_node_t *node);

/**
 * @brief Returns the least recently used entry, the one the next insert of
 * a new key into a full cache evicts.
 * @param lru The cache.
 * @return The entry, or NULL if the cache is empty or lru is NULL.
 */
extern sof_lru_node_t *sof_lru_oldest(const sof_lru_t *lru);

/**
 * @brief Finds the entry with the given key without changing how recently it
 * was used.
 * @param lru The cache to search.
 * @param key The key to search for.
 * @return The entry holding the key, or NULL if the key is not found or lru,
 * its nodes or key is NULL.
 */
extern sof_lru_node_t *sof_lru_peek(const sof_lru_t *lru, const void *key);

/**
 * @brief Returns the entry before node, from the least to the most recently
 * used.
 * @details Walks the cache from the oldest entry:
 * @code
 * for (n = sof_lru_oldest(lru); n; n = sof_lru_prev(lru, n))
 * @endcode
 * @param lru The cache holding node.
 * @param node The entry to start from.
 * @return The next more recently used entry, or NULL if node is the newest,
 * or lru or node is NULL.
 */
extern sof_lru_node_t *sof_lru_prev(const sof_lru_t *lru, const sof_lru_node_t *node);

/**
 * @brief Removes the entry with the given key.
 * @details The entry goes to the del_func, then its key to the free key
 * callback, if there are any.
 * @param lru The cache to remove the entry from.
 * @param key The key of the entry to remove. It may be the entry's own key,
 * which is freed if the cache owns it.
 * @return SOF_LRU_OK, SOF_LRU_NO_MATCH if the key isn't in the cache, or
 * SOF_LRU_ERROR if lru or key is NULL.
 */
extern sof_lru_error_t sof_lru_remove(sof_lru_t *lru, const void *key);

/**
 * @brief Removes an entry the caller already has, such as one from
 * sof_lru_peek() or sof_lru_oldest().
 * @details Like sof_lru_remove(). Other entries' pointers stay good.
 * @param lru The cache holding the entry.
 * @param node The entry to remove.
 * @return SOF_LRU_OK, or SOF_LRU_ERROR if lru or node is NULL or node isn't
 * one of lru's entries.
 */
extern sof_lru_error_t sof_lru_remove_node(sof_lru_t *lru, sof_lru_node_t *node);

/**
 * @brief Calls cb for each entry, from the most to the least recently used.
 * @details If cb returns a non-zero value, the traversal stops and returns
 * that value. Visiting an entry doesn't change how recently it was used.
 * @param lru The cache to traverse.
 * @param cb The function called for each entry. NULL visits nothing.
 * @param ctx Passed to every call of cb.
 * @return 0 if every entry was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if lru is NULL.
 */
extern int sof_lru_traverse(const sof_lru_t *lru, sof_lru_traverse_func_t cb, void *ctx);

#endif // SOF_LRU_H
