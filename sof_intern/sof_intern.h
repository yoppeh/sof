/**
 * @file sof_intern.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief String interning in C.
 * @details A sof_intern_t keeps one copy of each distinct string given to
 * it, and returns the same pointer for every string with the same bytes, so
 * interned strings can be compared, and hashed, by pointer. Strings are
 * given as bytes and a length, and can hold any bytes, NULs included; each
 * copy is followed by a NUL, so it can also be used as a c string. Copies
 * are kept in large blocks, never move, and stay until the pool is cleared
 * or freed. Each keeps its length and its hash, so neither has to be
 * worked out again.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_INTERN_H
#define SOF_INTERN_H

#include <stddef.h>
#include <stdint.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"

/**
 * @brief Error codes for the sof_intern functions.
 */
typedef enum sof_intern_error_t {
    SOF_INTERN_OK = 0,
    SOF_INTERN_NO_MEM,
    SOF_INTERN_NO_MATCH,
    SOF_INTERN_ERROR,
} sof_intern_error_t;

/**
 * @brief The header kept just before each interned string.
 * @details data points just past the header, to the string's bytes, length
 * is their number, not counting the NUL that follows them, and hash is
 * sof_hash_bytes(data, length, 0). The library uses the same type to look
 * strings up, so it is public, but the application only needs
 * sof_intern_length() and sof_intern_hash().
 */
typedef struct sof_intern_entry_t {
    const char *data;
    size_t length;
    uint64_t hash;
} sof_intern_entry_t;

/**
 * @brief A block of interned strings.
 * @details Blocks are allocated by the pool and linked through next; size is
 * the size each was allocated with, header included. The application has no
 * use for them.
 */
typedef struct sof_intern_block_t {
    struct sof_intern_block_t *next;
    size_t size;
} sof_intern_block_t;

/**
 * @brief Called for each string visited by sof_intern_traverse().
 * @details s is the interned string, NUL-terminated, and length its length
 * in bytes, not counting the NUL. Return 0 to continue, or a non-zero value
 * to stop the traversal early. It must not intern into, clear or free the
 * pool. ctx is the ctx given to sof_intern_traverse().
 */
typedef int (*sof_intern_traverse_func_t)(const char *s, size_t length, void *ctx);

/**
 * @brief A string pool.
 * @details map is a hashmap whose keys are the entries of the interned
 * strings, found by their bytes. blocks is the list of blocks the strings
 * are kept in, the newest first, and used is how much of the first block is
 * taken, header included. alloc is a copy of the allocator (zeroed, meaning
 * the standard functions, if it was given none). These fields can be read,
 * but should not be changed by the application.
 *
 * sof_intern_init() must be called before using a pool, and
 * sof_intern_free() should be called when it is no longer needed.
 * sof_intern_string() and sof_intern_cstr() intern strings,
 * sof_intern_find() looks them up without interning, and
 * sof_intern_length() and sof_intern_hash() give an interned string's
 * length and hash.
 *
 * Every function accepts a NULL pool and NULL data, and a pool that was
 * freed or whose sof_intern_init() failed (map.elements is NULL), as
 * described for each.
 *
 * The pool does no locking. sof_intern_count(), sof_intern_find(),
 * sof_intern_hash(), sof_intern_length(), sof_intern_next() and
 * sof_intern_traverse() only read it; every other function writes it.
 */
typedef struct sof_intern_t {
    sof_hashmap_t map;
    sof_intern_block_t *blocks;
    size_t used;
    sof_alloc_t alloc;
} sof_intern_t;

/**
 * @brief Forgets every string, leaving an empty pool that can still be used.
 * @details Every block of strings is freed, so pointers to interned strings
 * are no longer good. The hashmap keeps its array at its current size.
 * @param pool The pool to empty.
 * @return SOF_INTERN_OK, or SOF_INTERN_ERROR if pool is NULL.
 */
extern sof_intern_error_t sof_intern_clear(sof_intern_t *pool);

/**
 * @brief Returns the number of distinct strings in the pool.
 * @param pool The pool.
 * @return The number, or 0 if pool is NULL or not initialized.
 */
extern size_t sof_intern_count(const sof_intern_t *pool);

/**
 * @brief Interns a NUL-terminated string.
 * @details The same as sof_intern_string(pool, s, strlen(s), created).
 * @param pool The pool.
 * @param s The string.
 * @param created If not NULL, set to 1 if the string was new to the pool, or
 * 0 if it was already there or interning failed.
 * @return The interned string, or NULL if memory couldn't be allocated, or
 * pool or s is NULL or pool isn't initialized.
 */
extern const char *sof_intern_cstr(sof_intern_t *pool, const char *s, int *created);

/**
 * @brief Looks a string up without interning it.
 * @param pool The pool.
 * @param data The bytes of the string, which need not be NUL-terminated.
 * @param length The number of bytes.
 * @return The interned string with these bytes, or NULL if there isn't one,
 * or pool or data is NULL or pool isn't initialized.
 */
extern const char *sof_intern_find(const sof_intern_t *pool, const void *data, size_t length);

/**
 * @brief Frees everything the pool allocated.
 * @details Pointers to interned strings are no longer good. The
 * sof_intern_t itself isn't freed (it belongs to the caller); it is left
 * empty, and acts like a pool whose sof_intern_init() failed until it is
 * initialized again.
 * @param pool The pool to free.
 * @return SOF_INTERN_OK, or SOF_INTERN_ERROR if pool is NULL.
 */
extern sof_intern_error_t sof_intern_free(sof_intern_t *pool);

/**
 * @brief Returns the hash of an interned string.
 * @details The hash is kept with the string, so this takes constant time. It
 * is sof_hash_bytes(s, length, 0), which depends only on the string's
 * bytes, so it is the same in every run and every pool, unlike the string's
 * address. Use it to hash something made of interned strings, such as a
 * sequence of words. s must be a string returned by this library, from a
 * pool that hasn't since been cleared or freed.
 * @param s The interned string.
 * @return Its hash, or the hash of the empty string if s is NULL.
 */
extern uint64_t sof_intern_hash(const char *s);

/**
 * @brief Initializes a pool in a sof_intern_t the caller provides.
 * @param pool The pool to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap, and can be moved once initialized.
 * @param expected_strings How many distinct strings to make room for in the
 * hashmap before it grows; 0 is fine.
 * @param alloc The memory functions the pool uses, or NULL for the standard
 * functions. The pool keeps a copy, so it can be on the stack.
 * @return SOF_INTERN_OK, SOF_INTERN_NO_MEM, or SOF_INTERN_ERROR if pool is
 * NULL or alloc has a malloc but no free; on an error, the pool is left
 * empty.
 */
extern sof_intern_error_t sof_intern_init(sof_intern_t *pool, size_t expected_strings, const sof_alloc_t *alloc);

/**
 * @brief Returns the length of an interned string.
 * @details The length is kept with the string, so this takes constant time
 * and works for strings holding NULs. s must be a string returned by this
 * library, from a pool that hasn't since been cleared or freed.
 * @param s The interned string.
 * @return Its length in bytes, not counting the NUL after it, or 0 if s is
 * NULL.
 */
extern size_t sof_intern_length(const char *s);

/**
 * @brief Returns the next string in the pool, for looping over them without a
 * callback.
 * @details Set *position to 0 to start, then call this until it returns NULL.
 * The strings come in no particular order, the same order as
 * sof_intern_traverse(). Don't intern new strings until the loop ends.
 *
 *     size_t position = 0;
 *     const char *s;
 *     while ((s = sof_intern_next(&pool, &position)) != NULL) {
 *         ...
 *     }
 *
 * @param pool The pool to loop over.
 * @param position Where the loop is; updated on each call.
 * @return The next string, or NULL when there are no more, or pool or
 * position is NULL.
 */
extern const char *sof_intern_next(const sof_intern_t *pool, size_t *position);

/**
 * @brief Interns a string: returns the pool's copy of it, making one if
 * there isn't one yet.
 * @details Strings with the same bytes always give the same pointer, and
 * different strings different pointers. The copy is followed by a NUL, and
 * stays where it is until the pool is cleared or freed.
 * @param pool The pool.
 * @param data The bytes of the string, which need not be NUL-terminated and
 * can hold NULs. The pool doesn't keep this pointer.
 * @param length The number of bytes; 0 interns the empty string.
 * @param created If not NULL, set to 1 if the string was new to the pool, or
 * 0 if it was already there or interning failed.
 * @return The interned string, or NULL if memory couldn't be allocated, or
 * pool or data is NULL or pool isn't initialized.
 */
extern const char *sof_intern_string(sof_intern_t *pool, const void *data, size_t length, int *created);

/**
 * @brief Calls cb for each string in the pool.
 * @details The strings are visited in no particular order. If cb returns a
 * non-zero value, the traversal stops and returns that value.
 * @param pool The pool to traverse.
 * @param cb The function called for each string. NULL visits nothing.
 * @param ctx Passed to every call of cb.
 * @return 0 if every string was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if pool is NULL.
 */
extern int sof_intern_traverse(const sof_intern_t *pool, sof_intern_traverse_func_t cb, void *ctx);

#endif // SOF_INTERN_H
