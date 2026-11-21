/**
 * @file sof_set.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A hash set, with union, intersection and difference, in C.
 * @details A sof_set_t is a set of keys of any type, held in a sof_hashmap_t
 * whose element data is unused. Membership is a hash lookup, so adding,
 * finding and removing a key take constant time on average, and each set
 * operation takes time in proportion to one of its sets (the smaller one,
 * where it can choose). The keys come in no particular order; for an ordered
 * set, use sof_rbtree.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_SET_H
#define SOF_SET_H

#include <stddef.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"

/**
 * @brief Error codes for the set functions.
 */
typedef enum sof_set_error_t {
    SOF_SET_OK = 0,
    SOF_SET_NO_MEM,
    SOF_SET_NO_MATCH,
    SOF_SET_ERROR,
} sof_set_error_t;

/**
 * @brief Called for each key visited by sof_set_traverse().
 * @details key is the pointer the set stores. Return 0 to continue, or a
 * non-zero value to stop the traversal early. It must not add to or remove
 * from the set. ctx is the ctx given to sof_set_traverse().
 */
typedef int (*sof_set_traverse_func_t)(void *key, void *ctx);

/**
 * @brief A set.
 * @details The keys are the keys of map, a sof_hashmap_t set up with the
 * key callbacks given to sof_set_init(); see sof_hashmap_key_callbacks_t
 * for what they do. With a copy and free pair, such as
 * &sof_hashmap_string_keys, the set owns its keys; without, it stores the
 * caller's pointers, which must stay valid while they are in the set. The
 * free key callback is called for every key the set gives up, so it also
 * serves as the set's delete callback.
 *
 * The keys themselves never move: a key pointer from sof_set_add(),
 * sof_set_find() or sof_set_next() is good until that key is removed.
 *
 * The functions that take two sets (the set operations and comparisons)
 * expect them to use the same kind of keys, with hash and equal callbacks
 * that agree. When one set takes keys from another, it adds them as
 * sof_set_add() would: it copies them if it has a copy callback, and
 * otherwise stores the other set's pointers, so a set that frees keys it
 * didn't copy must not take keys from another set. If both sets have the
 * same hash callback and ctx, each key is looked up or added by the hash the
 * other set stored for it (sof_hashmap_find_hashed() and
 * sof_hashmap_insert_hashed()), without hashing it again; otherwise every
 * key is hashed.
 *
 * map can be read (map.count is the number of keys), but should not be
 * changed by the application. sof_set_init() must be called before using a
 * set, and sof_set_free() when it is no longer needed.
 *
 * Every function accepts a NULL set or key, and a set that was freed or whose
 * sof_set_init() failed, as described for each. Such a set is empty: lookups
 * find nothing, and adds, and set operations into it, fail until it is
 * initialized again.
 *
 * The set does no locking. sof_set_count(), sof_set_find(),
 * sof_set_next(), sof_set_traverse(), sof_set_equal(),
 * sof_set_is_disjoint() and sof_set_is_subset() only read their sets, as
 * do the set operations their src (provided the hash and equal callbacks only
 * read); every other function writes.
 */
typedef struct sof_set_t {
    sof_hashmap_t map;
} sof_set_t;

/**
 * @brief Adds a key to the set, or finds it if it is already there.
 * @param set The set to add to.
 * @param key The key to add. Without a copy key callback, the set stores this
 * pointer. If the key is already present, the set keeps its own key, so the
 * caller still owns this one.
 * @param created If not NULL, set to 1 if the key was added, or 0 if it was
 * already there or the add failed.
 * @return The key the set stores (its copy, or key itself), or NULL if memory
 * couldn't be allocated, the copy key callback failed, or set, its map or key
 * is NULL.
 */
extern void *sof_set_add(sof_set_t *set, const void *key, int *created);

/**
 * @brief Removes every key, leaving an empty set that can still be used.
 * @details Each key goes to the free key callback, if there is one.
 * @param set The set to empty.
 * @return SOF_SET_OK, or SOF_SET_ERROR if set is NULL.
 */
extern sof_set_error_t sof_set_clear(sof_set_t *set);

/**
 * @brief Returns the number of keys in the set.
 * @param set The set.
 * @return The number of keys, or 0 if set is NULL.
 */
extern size_t sof_set_count(const sof_set_t *set);

/**
 * @brief Removes from dst every key that is in src: dst = dst - src.
 * @details Takes time in proportion to the smaller of the two sets. If dst
 * and src are the same set, it is emptied.
 * @param dst The set to remove keys from.
 * @param src The keys to remove. It isn't changed.
 * @return SOF_SET_OK, or SOF_SET_ERROR if dst or src is NULL or dst was
 * freed or never initialized.
 */
extern sof_set_error_t sof_set_difference(sof_set_t *dst, const sof_set_t *src);

/**
 * @brief Tells whether two sets hold the same keys.
 * @param a One set.
 * @param b The other.
 * @return 1 if they hold the same keys, 0 if not, or -1 if a or b is NULL.
 */
extern int sof_set_equal(const sof_set_t *a, const sof_set_t *b);

/**
 * @brief Finds a key in the set.
 * @param set The set to search.
 * @param key The key to search for. The set doesn't keep it.
 * @return The key the set stores that equals key, or NULL if there is none or
 * set, its map or key is NULL.
 */
extern void *sof_set_find(const sof_set_t *set, const void *key);

/**
 * @brief Removes every key and frees the set's memory.
 * @details Each key goes to the free key callback, if there is one. The
 * sof_set_t itself isn't freed (it belongs to the caller); it is left empty,
 * and acts like a set whose sof_set_init() failed until it is initialized
 * again.
 * @param set The set to free.
 * @return SOF_SET_OK, or SOF_SET_ERROR if set is NULL.
 */
extern sof_set_error_t sof_set_free(sof_set_t *set);

/**
 * @brief Initializes a set in a sof_set_t the caller provides.
 * @param set The set to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap.
 * @param keys The key callbacks, such as &sof_hashmap_string_keys. The set
 * keeps a copy of the structure, so it can be on the stack. NULL tells keys
 * apart by identity, as if every callback were NULL.
 * @param ctx Passed to every key callback. Can be NULL.
 * @param expected_keys How many keys to make room for before the set has to
 * grow; 0 for the smallest size.
 * @param alloc The memory functions the set uses and passes to the key
 * callbacks, or NULL for the standard functions. The set keeps a copy, so it
 * can be on the stack.
 * @return SOF_SET_OK, SOF_SET_ERROR if set is NULL or alloc has a malloc
 * but no free, or SOF_SET_NO_MEM; on an error, the set is left empty, as if
 * it had been freed.
 */
extern sof_set_error_t sof_set_init(sof_set_t *set, const sof_hashmap_key_callbacks_t *keys, void *ctx, size_t expected_keys, const sof_alloc_t *alloc);

/**
 * @brief Keeps in dst only the keys that are also in src: dst = dst & src.
 * @details Takes time in proportion to dst. If dst and src are the same set,
 * nothing changes.
 * @param dst The set to remove keys from.
 * @param src The keys to keep. It isn't changed.
 * @return SOF_SET_OK, or SOF_SET_ERROR if dst or src is NULL or dst was
 * freed or never initialized.
 */
extern sof_set_error_t sof_set_intersection(sof_set_t *dst, const sof_set_t *src);

/**
 * @brief Tells whether two sets have no key in common.
 * @details Takes time in proportion to the smaller set.
 * @param a One set.
 * @param b The other.
 * @return 1 if no key is in both, 0 if one is, or -1 if a or b is NULL.
 */
extern int sof_set_is_disjoint(const sof_set_t *a, const sof_set_t *b);

/**
 * @brief Tells whether every key in a is also in b.
 * @details Takes time in proportion to a, and none at all if a has more
 * keys than b.
 * @param a The set that may be a subset.
 * @param b The set that may hold it.
 * @return 1 if a is a subset of b (as an empty set, or a itself, always is),
 * 0 if not, or -1 if a or b is NULL.
 */
extern int sof_set_is_subset(const sof_set_t *a, const sof_set_t *b);

/**
 * @brief Returns the next key in the set, for looping over it without a
 * callback.
 * @details Set *position to 0 to start, then call this until it returns NULL.
 * The keys come in no particular order, the same order as
 * sof_set_traverse(). Don't add to or remove from the set until the loop
 * ends, or keys may be skipped or seen twice.
 *
 *     size_t position = 0;
 *     void *key;
 *     while ((key = sof_set_next(&set, &position)) != NULL) {
 *         ...
 *     }
 *
 * @param set The set to loop over.
 * @param position Where the loop is; updated on each call. Only meaningful
 * to this function.
 * @return The next key, or NULL when there are no more or set or position is
 * NULL.
 */
extern void *sof_set_next(const sof_set_t *set, size_t *position);

/**
 * @brief Removes a key from the set.
 * @details The set's key goes to the free key callback, if there is one.
 * @param set The set to remove the key from.
 * @param key The key to remove. It may be the set's own key, which is freed
 * if the set owns it.
 * @return SOF_SET_OK, SOF_SET_NO_MATCH if the key isn't in the set, or
 * SOF_SET_ERROR if set or key is NULL.
 */
extern sof_set_error_t sof_set_remove(sof_set_t *set, const void *key);

/**
 * @brief Leaves in dst the keys that are in one set but not both:
 * dst = dst ^ src.
 * @details Each key of src is removed from dst if dst has it, and added
 * otherwise, so it takes time in proportion to src. If dst and src are the
 * same set, it is emptied. If an add fails, dst is left part way, with some
 * of src's keys added or removed.
 * @param dst The set to change.
 * @param src The keys to add or remove. It isn't changed.
 * @return SOF_SET_OK, SOF_SET_NO_MEM if a key couldn't be added (or the
 * copy key callback failed), or SOF_SET_ERROR if dst or src is NULL or dst
 * was freed or never initialized.
 */
extern sof_set_error_t sof_set_symmetric_difference(sof_set_t *dst, const sof_set_t *src);

/**
 * @brief Calls cb for each key in the set.
 * @details The keys are visited in no particular order. If cb returns a
 * non-zero value, the traversal stops and returns that value.
 * @param set The set to traverse.
 * @param cb The function called for each key. NULL visits nothing.
 * @param ctx Passed to every call of cb.
 * @return 0 if every key was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if set is NULL.
 */
extern int sof_set_traverse(const sof_set_t *set, sof_set_traverse_func_t cb, void *ctx);

/**
 * @brief Adds every key in src to dst: dst = dst | src.
 * @details Takes time in proportion to src. Into an empty set, it makes a
 * copy of src. If dst and src are the same set, nothing changes. If an add
 * fails, dst is left with the keys added so far.
 * @param dst The set to add keys to.
 * @param src The keys to add. It isn't changed.
 * @return SOF_SET_OK, SOF_SET_NO_MEM if a key couldn't be added (or the
 * copy key callback failed), or SOF_SET_ERROR if dst or src is NULL or dst
 * was freed or never initialized.
 */
extern sof_set_error_t sof_set_union(sof_set_t *dst, const sof_set_t *src);

#endif // SOF_SET_H
