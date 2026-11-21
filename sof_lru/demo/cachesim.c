/**
 * @file cachesim.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Cache simulation in C, for the cachesim program.
 * @details The caches are sof_lru_t caches with string keys, and the traces
 * come from a sof_random_t generator. A skewed trace draws keys from a
 * table of running sums of their weights, worked out in fixed point with 30
 * bits after the point: log2 of each key's rank by repeated squaring, and 2
 * to the power of minus that times the skew from a product of the square
 * roots of 1/2, which are found with an integer square root. Nothing is
 * left to floating point but rounding the skew, so the weights, and the
 * trace, are the same everywhere. A key is drawn by sof_sort_upper_bound()
 * on the table.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <string.h>

#include "sof_bit.h"
#include "sof_sort.h"
#include "cachesim.h"

/// @brief The bits after the point in the fixed-point numbers.
#define FRACTION_BITS 30

/// @brief 1 in fixed point.
#define FIXED_ONE ((uint64_t)1 << FRACTION_BITS)

/// @brief The bits after the point of the skew as it is used.
#define SKEW_BITS 16

static int cdf_compare(const void *a, const void *b, void *ctx);
static uint64_t exp2_neg(uint64_t f, const uint64_t *roots);
static uint64_t isqrt(uint64_t n);
static uint64_t log2_fixed(uint64_t k);
static cachesim_error_t trace_weights(cachesim_trace_t *trace, uint64_t skew);

/// @brief Compares a running sum in a trace's table with a target, both
/// uint64_t.
static int cdf_compare(const void *a, const void *b, void *ctx) {
    (void)ctx;
    uint64_t sum = *(const uint64_t *)a, target = *(const uint64_t *)b;
    return sum < target ? -1 : sum > target;
}

/// @brief Returns 2^-f, for f in fixed point from 0 up to 1, in fixed point.
/// @details roots[i] is 2^(-2^-i) in fixed point, for i from 1 to
/// FRACTION_BITS, so each bit of f set multiplies in one of them.
static uint64_t exp2_neg(uint64_t f, const uint64_t *roots) {
    uint64_t result = FIXED_ONE;
    for (unsigned i = 1; i <= FRACTION_BITS; i++) {
        if (f & ((uint64_t)1 << (FRACTION_BITS - i))) {
            result = (result * roots[i]) >> FRACTION_BITS;
        }
    }
    return result;
}

/// @brief Returns the integer square root of n, rounded down.
static uint64_t isqrt(uint64_t n) {
    uint64_t root = 0, bit = (uint64_t)1 << 62;
    while (bit > n) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

/// @brief Returns log2(k), for k of 1 or more, in fixed point.
/// @details The whole part is the position of k's highest bit. k is then
/// scaled to a fixed-point m from 1 up to 2, and each bit of the fraction
/// comes from squaring m: if the square is 2 or more, the bit is 1 and the
/// square is halved. k is at most the number of keys, which fits in a
/// size_t (trace_weights() checks), so it fits in a sof_bit_t.
static uint64_t log2_fixed(uint64_t k) {
    unsigned n = sof_bit_highest((sof_bit_t)k);
    uint64_t m = n <= FRACTION_BITS ? k << (FRACTION_BITS - n) : k >> (n - FRACTION_BITS);
    uint64_t result = (uint64_t)n << FRACTION_BITS;
    for (unsigned bit = FRACTION_BITS; bit-- > 0;) {
        m = (m * m) >> FRACTION_BITS;
        if (m >= 2 * FIXED_ONE) {
            m >>= 1;
            result |= (uint64_t)1 << bit;
        }
    }
    return result;
}

/// @brief Allocates and fills a skewed trace's table of running sums.
/// @details Key k's weight is 2^scale / (k + 1)^skew, with scale chosen so
/// that the weights of keys keys can't add up to more than 2^63, and skew
/// in fixed point with SKEW_BITS bits after the point.
static cachesim_error_t trace_weights(cachesim_trace_t *trace, uint64_t skew) {
    if (trace->keys > SIZE_MAX / sizeof(uint64_t)) {
        return CACHESIM_NO_MEM;
    }
    trace->cdf = sof_alloc_malloc(&trace->alloc, (size_t)trace->keys * sizeof(uint64_t));
    if (trace->cdf == NULL) {
        return CACHESIM_NO_MEM;
    }
    // keys fits in a size_t, so in a sof_bit_t, and is at most 2^61, so
    // bits is at most 61.
    unsigned bits = sof_bit_ceil_log2((sof_bit_t)trace->keys);
    unsigned scale = 63 - bits;
    uint64_t roots[FRACTION_BITS + 1];
    roots[0] = FIXED_ONE / 2;
    for (unsigned i = 1; i <= FRACTION_BITS; i++) {
        roots[i] = isqrt(roots[i - 1] << FRACTION_BITS);
    }
    uint64_t sum = 0;
    for (uint64_t k = 0; k < trace->keys; k++) {
        // e = skew * log2(k + 1), and the weight is 2^scale * 2^-e.
        uint64_t e = (skew * log2_fixed(k + 1)) >> SKEW_BITS;
        uint64_t whole = e >> FRACTION_BITS, weight = 0;
        if (whole < 64) {
            uint64_t m = exp2_neg(e & (FIXED_ONE - 1), roots);
            if (scale >= FRACTION_BITS) {
                weight = (m << (scale - FRACTION_BITS)) >> whole;
            } else if (FRACTION_BITS - scale + whole < 64) {
                weight = m >> (FRACTION_BITS - scale + whole);
            }
        }
        sum += weight;
        trace->cdf[k] = sum;
    }
    return CACHESIM_OK;
}

cachesim_error_t cachesim_access(cachesim_t *sim, const char *key) {
    if (sim == NULL || key == NULL || sim->caches == NULL) {
        return CACHESIM_ERROR;
    }
    for (size_t i = 0; i < sim->count; i++) {
        cachesim_cache_t *cache = &sim->caches[i];
        int created;
        if (sof_lru_insert(&cache->lru, key, &created) == NULL) {
            return CACHESIM_NO_MEM;
        }
        if (!created) {
            cache->hits++;
        }
    }
    sim->accesses++;
    return CACHESIM_OK;
}

cachesim_error_t cachesim_clear(cachesim_t *sim) {
    if (sim == NULL || sim->caches == NULL) {
        return CACHESIM_ERROR;
    }
    for (size_t i = 0; i < sim->count; i++) {
        sof_lru_clear(&sim->caches[i].lru);
        sim->caches[i].hits = 0;
    }
    sim->accesses = 0;
    return CACHESIM_OK;
}

cachesim_error_t cachesim_free(cachesim_t *sim) {
    if (sim == NULL) {
        return CACHESIM_ERROR;
    }
    if (sim->caches != NULL) {
        for (size_t i = 0; i < sim->count; i++) {
            sof_lru_free(&sim->caches[i].lru);
        }
        sof_alloc_free(&sim->alloc, sim->caches, sim->count * sizeof(cachesim_cache_t));
    }
    memset(sim, 0, sizeof(*sim));
    return CACHESIM_OK;
}

size_t cachesim_hits(const cachesim_t *sim, size_t index) {
    if (sim == NULL || sim->caches == NULL || index >= sim->count) {
        return 0;
    }
    return sim->caches[index].hits;
}

cachesim_error_t cachesim_init(cachesim_t *sim, const size_t *sizes, size_t count, const sof_alloc_t *alloc) {
    if (sim == NULL) {
        return CACHESIM_ERROR;
    }
    memset(sim, 0, sizeof(*sim));
    if (sizes == NULL || count == 0 || !sof_alloc_valid(alloc)) {
        return CACHESIM_ERROR;
    }
    for (size_t i = 0; i < count; i++) {
        if (sizes[i] == 0) {
            return CACHESIM_ERROR;
        }
    }
    if (alloc != NULL) {
        sim->alloc = *alloc;
    }
    cachesim_cache_t *caches = sof_alloc_calloc(&sim->alloc, count, sizeof(cachesim_cache_t));
    if (caches == NULL) {
        memset(sim, 0, sizeof(*sim));
        return CACHESIM_NO_MEM;
    }
    for (size_t i = 0; i < count; i++) {
        caches[i].size = sizes[i];
        sof_lru_error_t err = sof_lru_init(&caches[i].lru, &sof_hashmap_string_keys, NULL, NULL, sizes[i], &sim->alloc);
        if (err != SOF_LRU_OK) {
            while (i-- > 0) {
                sof_lru_free(&caches[i].lru);
            }
            sof_alloc_free(&sim->alloc, caches, count * sizeof(cachesim_cache_t));
            memset(sim, 0, sizeof(*sim));
            return err == SOF_LRU_NO_MEM ? CACHESIM_NO_MEM : CACHESIM_ERROR;
        }
    }
    sim->caches = caches;
    sim->count = count;
    return CACHESIM_OK;
}

cachesim_error_t cachesim_trace_free(cachesim_trace_t *trace) {
    if (trace == NULL) {
        return CACHESIM_ERROR;
    }
    if (trace->cdf != NULL) {
        sof_alloc_free(&trace->alloc, trace->cdf, (size_t)trace->keys * sizeof(uint64_t));
    }
    memset(trace, 0, sizeof(*trace));
    return CACHESIM_OK;
}

cachesim_error_t cachesim_trace_init(cachesim_trace_t *trace, uint64_t keys, double skew, uint64_t seed, const sof_alloc_t *alloc) {
    if (trace == NULL) {
        return CACHESIM_ERROR;
    }
    memset(trace, 0, sizeof(*trace));
    if (keys == 0 || !(skew >= 0 && skew <= CACHESIM_SKEW_MAX) || !sof_alloc_valid(alloc)) {
        return CACHESIM_ERROR;
    }
    if (alloc != NULL) {
        trace->alloc = *alloc;
    }
    trace->keys = keys;
    // Multiplying by a power of 2 is exact, so the only rounding is to the
    // nearest multiple of 1/65536, the same on any IEEE 754 machine.
    uint64_t fixed_skew = (uint64_t)(skew * (double)((uint64_t)1 << SKEW_BITS) + 0.5);
    if (fixed_skew != 0) {
        cachesim_error_t err = trace_weights(trace, fixed_skew);
        if (err != CACHESIM_OK) {
            memset(trace, 0, sizeof(*trace));
            return err;
        }
    }
    sof_random_init(&trace->random, seed);
    return CACHESIM_OK;
}

cachesim_error_t cachesim_trace_next(cachesim_trace_t *trace, uint64_t *key) {
    if (trace == NULL || key == NULL || trace->keys == 0) {
        return CACHESIM_ERROR;
    }
    if (trace->cdf == NULL) {
        *key = sof_random_below(&trace->random, trace->keys);
        return CACHESIM_OK;
    }
    // The key is the first whose running sum is more than a number drawn
    // below the total, so each is drawn in proportion to its weight.
    // The target is below the last sum, so a key is always found.
    uint64_t target = sof_random_below(&trace->random, trace->cdf[trace->keys - 1]);
    *key = sof_sort_upper_bound(trace->cdf, (size_t)trace->keys, sizeof(uint64_t), &target, cdf_compare, NULL);
    return CACHESIM_OK;
}
