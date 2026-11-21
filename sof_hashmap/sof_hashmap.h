/**
 * @file sof_hashmap.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Hashmap implementation in C.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_HASHMAP_H
#define SOF_HASHMAP_H

#include <stddef.h>

#include "sof_alloc.h"

/**
 * @brief Error codes for the hashmap functions.
 */
typedef enum sof_hashmap_error_t {
    SOF_HASHMAP_OK = 0,
    SOF_HASHMAP_NO_MEM,
    SOF_HASHMAP_NO_MATCH,
    SOF_HASHMAP_ERROR,
} sof_hashmap_error_t;

/**
 * @brief How a hashmap hashes, compares, copies and frees its keys.
 * @details Every callback gets the ctx given to sof_hashmap_init(). Any of them
 * can be NULL:
 * - hash NULL: the key's address is hashed, so keys are told apart by
 *   identity. hash must give equal keys equal hashes, and the same key the
 *   same hash every time it is called with the same ctx, in any map: the
 *   map keeps each key's hash rather than calling hash again, and
 *   sof_hashmap_hash() and the functions that take its result rely on it.
 *   The map mixes the bits of whatever it returns, so a simple hash is fine.
 * - equal NULL: keys are equal only if they are the same pointer. equal
 *   returns non-zero if a and b are equal. A key is always equal to itself:
 *   the map doesn't call equal when the key it is given is the very
 *   pointer it stores.
 * - copy NULL: the map stores the caller's key pointer, which must stay
 *   valid while it is in the map. Otherwise copy is called when a new key
 *   is inserted, and the map stores what it returns (NULL means it failed).
 * - free NULL: the map never frees keys. Otherwise free is called for each
 *   key the map gives up, after the map's del_func, so a copy and free pair
 *   makes the map own its keys.
 *
 * copy and free also get the map's allocator, the one given to
 * sof_hashmap_init(), so they can allocate and free keys with it through
 * sof_alloc_malloc() and sof_alloc_free().
 *
 * Keys can't be NULL, since an empty slot holds a NULL key.
 */
typedef struct sof_hashmap_key_callbacks_t {
    size_t (*hash)(const void *key, void *ctx);
    int (*equal)(const void *a, const void *b, void *ctx);
    void *(*copy)(const void *key, const sof_alloc_t *alloc, void *ctx);
    void (*free)(void *key, const sof_alloc_t *alloc, void *ctx);
} sof_hashmap_key_callbacks_t;

/**
 * @brief Key callbacks for NUL-terminated strings, which the map copies.
 * @details Inserting a string stores a copy of it, made with the map's
 * allocator, which the map frees, so the caller can pass a string in a buffer
 * it reuses. Uses sof_hashmap_string_hash() and sof_hashmap_string_equal().
 */
extern const sof_hashmap_key_callbacks_t sof_hashmap_string_keys;

/**
 * @brief Hashes a NUL-terminated string; a hash callback for string keys.
 * @details Combine it with sof_hashmap_string_equal() and no copy or free
 * callbacks for a map that stores the caller's string pointers.
 */
extern size_t sof_hashmap_string_hash(const void *key, void *ctx);

/**
 * @brief Compares two NUL-terminated strings; an equal callback for string
 * keys.
 */
extern int sof_hashmap_string_equal(const void *a, const void *b, void *ctx);

/**
 * @brief An element in the hashmap.
 * @details The hash is the FULL hash of the key: the result of the hash
 * callback (or of the key's address, if there is none) after the map mixes
 * its bits, not adjusted for the capacity of the hashmap. The key is the
 * pointer the map stores: the copy made by the copy callback, or the caller's
 * own pointer if there is none. The data is never touched by the hashmap
 * code; it belongs to the application.
 *
 * Elements live in the hashmap's own array, so a pointer to one is only good
 * until the next sof_hashmap_insert() of a new key that makes the map grow
 * (which moves every element to a larger array) or the next remove (which may
 * move other elements into the freed slot). An insert grows the map only when
 * count is already capacity / 4 * 3; sof_hashmap_reserve() makes room in
 * advance, and a remove never shrinks the map, so an insert after a remove
 * never grows it.
 */
typedef struct sof_hashmap_element_t {
    size_t hash;
    void *key;
    void *data;
} sof_hashmap_element_t;

/**
 * @brief Receives an element just before it is removed.
 * @details Called by sof_hashmap_remove(), sof_hashmap_remove_element(),
 * sof_hashmap_clear() and sof_hashmap_free() for each element they remove, so the
 * application can free the element's data, and its key if the map has no
 * free key callback (which is called after this). It must not call into the
 * map. ctx is the ctx given to sof_hashmap_init().
 */
typedef void (*sof_hashmap_element_delete_func_t)(sof_hashmap_element_t *element, void *ctx);

/**
 * @brief Called for each element visited by sof_hashmap_traverse().
 * @details Return 0 to continue, or a non-zero value to stop the traversal
 * early. It must not insert into or remove from the map. ctx is the ctx given
 * to sof_hashmap_traverse().
 */
typedef int (*sof_hashmap_traverse_func_t)(sof_hashmap_element_t *element, void *ctx);

/**
 * @brief A hashmap.
 * @details Keys are found by open addressing with linear probing. The
 * capacity is the number of slots in elements, always a power of two; the
 * hashmap doubles it before more than three quarters of the slots are in use.
 * The mask is the capacity minus one, and is used to calculate the index of
 * an element in the hashmap from its hash. The count is the number of
 * elements currently in the hashmap. keys, del_func and ctx are the callbacks
 * and context given to sof_hashmap_init(), and alloc a copy of its allocator
 * (zeroed, meaning the standard functions, if it was given none). These
 * fields can be read, but should not be changed by the application.
 *
 * sof_hashmap_init() must be called before using a hashmap, and sof_hashmap_free()
 * should be called when the hashmap is no longer needed. sof_hashmap_clear()
 * empties the hashmap without freeing it, and sof_hashmap_insert(),
 * sof_hashmap_find() and sof_hashmap_remove() add, find and remove elements.
 *
 * Every function accepts a NULL map or key, and a map that was freed or whose
 * sof_hashmap_init() failed (elements is NULL), as described for each.
 *
 * The hashmap does no locking. sof_hashmap_find(), sof_hashmap_find_hashed(),
 * sof_hashmap_hash(), sof_hashmap_next(), sof_hashmap_next_hashed() and
 * sof_hashmap_traverse() only read it (provided the hash and equal callbacks only
 * read), and so take a const map; every other function writes it. The
 * elements they return, or pass to the callback, are not const: the map
 * doesn't own their data, so the caller may change it.
 */
typedef struct sof_hashmap_t {
    size_t capacity;
    size_t mask;
    size_t count;
    sof_hashmap_element_t *elements;
    sof_hashmap_key_callbacks_t keys;
    sof_hashmap_element_delete_func_t del_func;
    void *ctx;
    sof_alloc_t alloc;
} sof_hashmap_t;

/**
 * @brief Removes every element, leaving an empty map that can still be used.
 * @details Each element goes to the del_func, then its key to the free key
 * callback, if there are any. The hashmap's array is neither freed nor
 * shrunk.
 * @param map The hashmap to empty.
 * @return SOF_HASHMAP_OK, or SOF_HASHMAP_ERROR if map is NULL.
 */
extern sof_hashmap_error_t sof_hashmap_clear(sof_hashmap_t *map);

/**
 * @brief Finds the element with the given key.
 * @details Only reads the map. The element returned isn't const, so the
 * caller can change its data.
 * @param map The hashmap to search.
 * @param key The key to search for. The map doesn't keep it.
 * @return The element holding the key, or NULL if the key is not found or
 * map, its elements or key is NULL.
 */
extern sof_hashmap_element_t *sof_hashmap_find(const sof_hashmap_t *map, const void *key);

/**
 * @brief Finds the element with the given key, using a hash the caller
 * already has instead of hashing the key.
 * @details hash must be key's full hash: what sof_hashmap_hash() returns for
 * key, or the hash field of an element whose key is equal to key, from this
 * map or another with the same hash key callback and ctx, as when looking up
 * each key of one map in another. The hash isn't checked: any other value
 * can miss a key that is there. The equal key callback is called as usual.
 * Only reads the map.
 * @param map The hashmap to search.
 * @param key The key to search for. The map doesn't keep it.
 * @param hash The full hash of key.
 * @return The element holding the key, or NULL if the key is not found or
 * map, its elements or key is NULL.
 */
extern sof_hashmap_element_t *sof_hashmap_find_hashed(const sof_hashmap_t *map, const void *key, size_t hash);

/**
 * @brief Removes every element and frees the hashmap's array.
 * @details Each element goes to the del_func, then its key to the free key
 * callback, if there are any. The sof_hashmap_t itself isn't freed (it belongs
 * to the caller); it is left empty, and acts like a map whose sof_hashmap_init()
 * failed until it is initialized again.
 * @param map The hashmap to free.
 * @return SOF_HASHMAP_OK, or SOF_HASHMAP_ERROR if map is NULL.
 */
extern sof_hashmap_error_t sof_hashmap_free(sof_hashmap_t *map);

/**
 * @brief Returns the full hash the map gives a key: the hash key callback's
 * result (or the key's address, if there is none) with its bits mixed, as
 * stored in an element's hash field.
 * @details For sof_hashmap_find_hashed(), sof_hashmap_insert_hashed() and
 * sof_hashmap_next_hashed(), so a key can be hashed once for several calls.
 * Only reads the map.
 * @param map The hashmap whose hash callback and ctx are used.
 * @param key The key to hash.
 * @return The full hash, or 0 if map, its elements or key is NULL.
 */
extern size_t sof_hashmap_hash(const sof_hashmap_t *map, const void *key);

/**
 * @brief Initializes a hashmap in a sof_hashmap_t the caller provides.
 * @param map The hashmap to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap.
 * @param keys The key callbacks, such as &sof_hashmap_string_keys. The map keeps
 * a copy of the structure, so it can be on the stack. NULL tells keys apart
 * by identity, as if every callback were NULL.
 * @param del_func Called with each element just before it is removed; see
 * sof_hashmap_element_delete_func_t. Can be NULL.
 * @param ctx Passed to del_func and every key callback. Can be NULL.
 * @param expected_keys How many keys to make room for. The map starts with
 * the smallest power-of-two capacity (at least 16 slots) that holds this many
 * keys without growing, and doubles it as usual if more are inserted. 0 gives
 * 16 slots.
 * @param alloc The memory functions the map uses for its array and passes to
 * the key callbacks, or NULL for the standard functions. The map keeps a
 * copy, so it can be on the stack.
 * @return SOF_HASHMAP_OK, SOF_HASHMAP_ERROR if map is NULL or alloc has a malloc but
 * no free, or SOF_HASHMAP_NO_MEM; on an error, the map is left empty with a NULL
 * elements array.
 */
extern sof_hashmap_error_t sof_hashmap_init(sof_hashmap_t *map, const sof_hashmap_key_callbacks_t *keys, sof_hashmap_element_delete_func_t del_func, void *ctx, size_t expected_keys, const sof_alloc_t *alloc);

/**
 * @brief Inserts an element with the given key, or finds the one already
 * there.
 * @details If the key is already present, nothing is inserted and the
 * existing element is returned. Otherwise the map stores the key's copy, if
 * there is a copy key callback, or the key pointer itself.
 * @param map The hashmap to insert into.
 * @param key The key to insert. Without a copy key callback, the map stores
 * this pointer, so it must stay valid while the key is in the map, and the
 * application frees it (in del_func, for example) unless there is a free key
 * callback. If the key already exists, the map keeps the existing element's
 * key, so the caller still owns this one.
 * The map grows, doubling its capacity and moving every element, only when
 * a new key is inserted while count is capacity / 4 * 3, so after
 * sof_hashmap_reserve(map, n), inserting until count reaches n neither
 * allocates (but for the copy key callback) nor moves elements, and neither
 * does an insert that follows a remove.
 * @param created If not NULL, set to 1 if a new element was created, or 0 if
 * the key already existed or the insert failed.
 * @return The new element, whose data is NULL, or the existing one. NULL if
 * memory couldn't be allocated, the copy key callback failed, or map, its
 * elements or key is NULL.
 */
extern sof_hashmap_element_t *sof_hashmap_insert(sof_hashmap_t *map, const void *key, int *created);

/**
 * @brief Inserts an element with the given key, or finds the one already
 * there, using a hash the caller already has instead of hashing the key.
 * @details The same as sof_hashmap_insert(), but for the hash. hash must be
 * key's full hash: what sof_hashmap_hash() returns for key, or the hash
 * field of an element whose key is equal to key, from this map or another
 * with the same hash key callback and ctx, as when inserting the keys of one
 * map into another. The hash isn't checked: any other value stores the key
 * where sof_hashmap_find() may not find it.
 * @param map The hashmap to insert into.
 * @param key The key to insert, as for sof_hashmap_insert().
 * @param hash The full hash of key.
 * @param created If not NULL, set to 1 if a new element was created, or 0 if
 * the key already existed or the insert failed.
 * @return The new element, whose data is NULL, or the existing one. NULL if
 * memory couldn't be allocated, the copy key callback failed, or map, its
 * elements or key is NULL.
 */
extern sof_hashmap_element_t *sof_hashmap_insert_hashed(sof_hashmap_t *map, const void *key, size_t hash, int *created);

/**
 * @brief Returns the next element in the hashmap, for looping over it
 * without a callback.
 * @details Set *position to 0 to start, then call this until it returns NULL.
 * The elements come in no particular order, the same order as
 * sof_hashmap_traverse(). Don't insert into or remove from the map until the loop
 * ends, or elements may be skipped or seen twice; changing their data is
 * fine. Only reads the map, though the elements returned aren't const.
 *
 *     size_t position = 0;
 *     sof_hashmap_element_t *e;
 *     while ((e = sof_hashmap_next(&map, &position)) != NULL) {
 *         ...
 *     }
 *
 * @param map The hashmap to loop over.
 * @param position Where the loop is; updated on each call. Only meaningful
 * to this function.
 * @return The next element, or NULL when there are no more or map or
 * position is NULL.
 */
extern sof_hashmap_element_t *sof_hashmap_next(const sof_hashmap_t *map, size_t *position);

/**
 * @brief Returns the next element whose full hash is hash, for comparing
 * keys without the equal key callback.
 * @details Set *position to 0 to start, then call this until it returns
 * NULL. The elements come in the order a lookup of a key with that hash
 * tries them, and every element whose key is equal to such a key is among
 * them; others whose hash is the same may be too, so the caller compares
 * each element's key with its own. That lets a caller with keys of its own
 * kind compare them directly, which can be faster than the callback. Don't
 * insert or remove until the loop ends. Only reads the map.
 *
 *     size_t position = 0;
 *     sof_hashmap_element_t *e;
 *     while ((e = sof_hashmap_next_hashed(&map, hash, &position)) != NULL) {
 *         if (... e->key is the key ...) {
 *             ...
 *         }
 *     }
 *
 * @param map The hashmap to search.
 * @param hash A full hash, from sof_hashmap_hash() or an element's hash.
 * @param position Where the loop is; updated on each call. Only meaningful
 * to this function.
 * @return The next element with that hash, or NULL when there are no more,
 * or map, its elements or position is NULL.
 */
extern sof_hashmap_element_t *sof_hashmap_next_hashed(const sof_hashmap_t *map, size_t hash, size_t *position);

/**
 * @brief Removes the element with the given key.
 * @details The element goes to the del_func, then its key to the free key
 * callback, if there are any.
 * @param map The hashmap to remove the element from.
 * @param key The key of the element to remove. It may be the element's own
 * key, which is freed if the map owns it.
 * @return SOF_HASHMAP_OK, SOF_HASHMAP_NO_MATCH if the key isn't in the map, or
 * SOF_HASHMAP_ERROR if map or key is NULL.
 */
extern sof_hashmap_error_t sof_hashmap_remove(sof_hashmap_t *map, const void *key);

/**
 * @brief Removes an element the caller already has, such as one from
 * sof_hashmap_find() or sof_hashmap_next().
 * @details Like sof_hashmap_remove(), without looking the key up again. Other
 * elements may move into its slot, so element pointers (and a sof_hashmap_next()
 * loop) aren't good afterwards.
 * @param map The hashmap holding the element.
 * @param element The element to remove.
 * @return SOF_HASHMAP_OK, or SOF_HASHMAP_ERROR if map or element is NULL or element
 * isn't one of map's elements.
 */
extern sof_hashmap_error_t sof_hashmap_remove_element(sof_hashmap_t *map, sof_hashmap_element_t *element);

/**
 * @brief Makes room for a number of keys, so inserting up to that many
 * doesn't grow the map.
 * @details If the map's capacity can't already hold keys elements without
 * growing, the elements move to an array of the smallest power-of-two
 * capacity that can, as sof_hashmap_init() sizes it for expected_keys, so
 * element pointers aren't good afterwards. The map never shrinks: a smaller
 * keys does nothing. Until count reaches keys, sof_hashmap_insert()
 * allocates nothing itself and moves no elements.
 * @param map The hashmap.
 * @param keys The number of keys, in all, to make room for.
 * @return SOF_HASHMAP_OK, SOF_HASHMAP_NO_MEM if the larger array couldn't be
 * allocated, which leaves the map as it was, or SOF_HASHMAP_ERROR if map is
 * NULL or not initialized.
 */
extern sof_hashmap_error_t sof_hashmap_reserve(sof_hashmap_t *map, size_t keys);

/**
 * @brief Calls cb for each element in the hashmap.
 * @details The elements are visited in no particular order. If cb returns a
 * non-zero value, the traversal stops and returns that value. Only reads the
 * map; cb may change an element's data.
 * @param map The hashmap to traverse.
 * @param cb The function called for each element. NULL visits nothing.
 * @param ctx Passed to every call of cb.
 * @return 0 if every element was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if map is NULL.
 */
extern int sof_hashmap_traverse(const sof_hashmap_t *map, sof_hashmap_traverse_func_t cb, void *ctx);

#endif // SOF_HASHMAP_H
