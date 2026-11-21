/**
 * @file cachesim.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Cache simulation in C, for the cachesim program.
 * @details A cachesim_t replays a trace of keys through several
 * least-recently-used caches of different sizes at once, each a sof_lru_t,
 * and counts the hits of each. A cachesim_trace_t generates traces: keys
 * numbered from 0, drawn from a sof_random_t generator either uniformly or
 * with a Zipf-like skew, in which key k (counting from 0) is drawn in
 * proportion to 1 / (k + 1)^skew. The weights are worked out in integer
 * arithmetic, so a seed gives the same trace on every platform and compiler.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef CACHESIM_H
#define CACHESIM_H

#include <stddef.h>
#include <stdint.h>

#include "sof_alloc.h"
#include "sof_lru.h"
#include "sof_random.h"

/**
 * @brief The largest skew cachesim_trace_init() accepts.
 */
#define CACHESIM_SKEW_MAX 16

/**
 * @brief Error codes for the cachesim functions.
 * @details CACHESIM_NO_MATCH is never returned; it is there so the codes
 * match those of the other libraries.
 */
typedef enum cachesim_error_t {
    CACHESIM_OK = 0,
    CACHESIM_NO_MEM,
    CACHESIM_NO_MATCH,
    CACHESIM_ERROR,
} cachesim_error_t;

/**
 * @brief One simulated cache.
 * @details size is the most keys it holds, hits the number of accesses that
 * found their key in it, and lru the cache itself, which holds copies of the
 * keys. The fields can be read, but should not be changed by the
 * application.
 */
typedef struct cachesim_cache_t {
    size_t size;
    size_t hits;
    sof_lru_t lru;
} cachesim_cache_t;

/**
 * @brief A simulator: several caches that see the same trace.
 * @details caches holds count caches, in the order of the sizes given to
 * cachesim_init(), accesses the number of keys replayed, and alloc a copy of
 * the allocator (zeroed, meaning the standard functions, if it was given
 * none). The fields can be read, but should not be changed by the
 * application.
 *
 * cachesim_init() must be called before using a simulator, and
 * cachesim_free() should be called when it is no longer needed.
 * cachesim_access() replays a key, cachesim_hits() gives a cache's hits, and
 * cachesim_clear() starts over.
 *
 * Every function accepts a NULL simulator, and one that was freed or whose
 * cachesim_init() failed (caches is NULL), as described for each.
 *
 * The simulator does no locking. cachesim_hits() only reads it; every other
 * function writes it.
 */
typedef struct cachesim_t {
    cachesim_cache_t *caches;
    size_t count;
    size_t accesses;
    sof_alloc_t alloc;
} cachesim_t;

/**
 * @brief A trace generator.
 * @details random is the generator keys are drawn with, keys the number of
 * different keys, and cdf, for a skewed trace, the running sums of the keys'
 * weights, keys of them, of which the last is the total; it is NULL for a
 * uniform trace. alloc is a copy of the allocator (zeroed for the standard
 * functions). The fields can be read, but should not be changed by the
 * application.
 *
 * cachesim_trace_init() must be called before using a generator, and
 * cachesim_trace_free() should be called when it is no longer needed.
 *
 * Every function accepts a NULL generator, and one that was freed or whose
 * cachesim_trace_init() failed (keys is 0), as described for each.
 *
 * The generator does no locking; every function writes it.
 */
typedef struct cachesim_trace_t {
    sof_random_t random;
    uint64_t keys;
    uint64_t *cdf;
    sof_alloc_t alloc;
} cachesim_trace_t;

/**
 * @brief Replays one access to a key through every cache.
 * @details Each cache that holds the key counts a hit and makes the key its
 * most recently used. Each that doesn't takes a copy of it, evicting its
 * least recently used key if it is full.
 * @param sim The simulator.
 * @param key The key, NUL-terminated. The simulator copies it, so it can be
 * in a buffer the caller reuses.
 * @return CACHESIM_OK, CACHESIM_NO_MEM if a cache couldn't copy the key, in
 * which case the caches before it have counted the access and the rest
 * haven't, and accesses isn't counted, so the counts should be cleared, or
 * CACHESIM_ERROR if sim or key is NULL or sim isn't initialized.
 */
extern cachesim_error_t cachesim_access(cachesim_t *sim, const char *key);

/**
 * @brief Empties every cache and sets the counts to 0, keeping the sizes.
 * @param sim The simulator.
 * @return CACHESIM_OK, or CACHESIM_ERROR if sim is NULL or not initialized.
 */
extern cachesim_error_t cachesim_clear(cachesim_t *sim);

/**
 * @brief Frees everything the simulator allocated.
 * @details The cachesim_t itself isn't freed (it belongs to the caller); it
 * is left empty, and acts like a simulator whose cachesim_init() failed
 * until it is initialized again.
 * @param sim The simulator.
 * @return CACHESIM_OK, or CACHESIM_ERROR if sim is NULL.
 */
extern cachesim_error_t cachesim_free(cachesim_t *sim);

/**
 * @brief Returns the number of hits a cache has counted.
 * @details Its misses are sim->accesses less its hits.
 * @param sim The simulator.
 * @param index The cache, from 0 to count - 1, in the order of the sizes
 * given to cachesim_init().
 * @return The hits, or 0 if index is count or more, or sim is NULL or not
 * initialized.
 */
extern size_t cachesim_hits(const cachesim_t *sim, size_t index);

/**
 * @brief Initializes a simulator in a cachesim_t the caller provides.
 * @details Each cache allocates room for all of its keys here, so a cache
 * whose size is far more than the number of different keys in the trace
 * wastes memory.
 * @param sim The simulator to initialize. It can be anywhere: on the stack,
 * in another structure or on the heap.
 * @param sizes The size of each cache, in keys. The simulator keeps a copy,
 * so they can be on the stack. A size can be given more than once.
 * @param count The number of sizes.
 * @param alloc The memory functions the simulator and its caches use, or
 * NULL for the standard functions. The simulator keeps a copy, so it can be
 * on the stack.
 * @return CACHESIM_OK, CACHESIM_NO_MEM, or CACHESIM_ERROR if sim or sizes is
 * NULL, count or a size is 0, or alloc has a malloc but no free; on an
 * error, sim is left empty.
 */
extern cachesim_error_t cachesim_init(cachesim_t *sim, const size_t *sizes, size_t count, const sof_alloc_t *alloc);

/**
 * @brief Frees everything the generator allocated.
 * @details The cachesim_trace_t itself isn't freed (it belongs to the
 * caller); it is left empty, and acts like a generator whose
 * cachesim_trace_init() failed until it is initialized again.
 * @param trace The generator.
 * @return CACHESIM_OK, or CACHESIM_ERROR if trace is NULL.
 */
extern cachesim_error_t cachesim_trace_free(cachesim_trace_t *trace);

/**
 * @brief Initializes a trace generator in a cachesim_trace_t the caller
 * provides.
 * @details With a skew of 0, every key is equally likely and nothing is
 * allocated. Otherwise key k, counting from 0, is drawn in proportion to
 * 1 / (k + 1)^skew, so a skew of 1 is Zipf's law: key 0 comes twice as
 * often as key 1 and three times as often as key 2. The skew is rounded to
 * a multiple of 1/65536, and the weights are worked out from it in integer
 * arithmetic, with 30 bits after the point, so a seed gives the same keys
 * on every platform. A table of the weights' running sums, 8 bytes per key,
 * is allocated and filled, which takes a moment for millions of keys. Keys
 * so unlikely that their weight rounds to 0 are never drawn.
 * @param trace The generator to initialize. It can be anywhere: on the
 * stack, in another structure or on the heap.
 * @param keys The number of different keys.
 * @param skew The skew, from 0 to CACHESIM_SKEW_MAX.
 * @param seed The seed for the random numbers. Any value is fine.
 * @param alloc The memory functions the generator uses, or NULL for the
 * standard functions. The generator keeps a copy, so it can be on the
 * stack.
 * @return CACHESIM_OK, CACHESIM_NO_MEM, or CACHESIM_ERROR if trace is NULL,
 * keys is 0, skew is out of range or not a number, or alloc has a malloc but
 * no free; on an error, trace is left empty.
 */
extern cachesim_error_t cachesim_trace_init(cachesim_trace_t *trace, uint64_t keys, double skew, uint64_t seed, const sof_alloc_t *alloc);

/**
 * @brief Draws the next key of a trace.
 * @param trace The generator.
 * @param key Set to the key, from 0 to keys - 1.
 * @return CACHESIM_OK, or CACHESIM_ERROR, leaving *key alone, if trace or
 * key is NULL or trace isn't initialized.
 */
extern cachesim_error_t cachesim_trace_next(cachesim_trace_t *trace, uint64_t *key);

#endif // CACHESIM_H
