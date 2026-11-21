/**
 * @file sof_bloom.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A Bloom filter in c.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_bit.h"
#include "sof_bloom.h"
#include "sof_hash.h"

/// @brief ln 2, which the sizing formulas use.
#define LN2 0.69314718055994530942

/// @brief Mixed into a key's hash to make the step between its bits.
#define STEP_SALT 0x9e3779b97f4a7c15ULL

/// @brief The number of 32-bit words in a block of a blocked filter, and so
/// the number of bits a key sets in one.
#define BLOCK_WORDS 8

/// @brief The bytes in a block of a blocked filter.
#define BLOCK_BYTES (BLOCK_WORDS * sizeof(uint32_t))

/// @brief A blocked filter's blocks start on a boundary of this many bytes,
/// a cache line, so a block never straddles two.
#define BLOCK_ALIGN 64

/// @brief The most blocks a blocked filter can have, so that the 32-bit
/// half of a hash times the number of blocks fits in 64 bits.
#define MAX_BLOCKS 4294967296.0

/**
 * @brief The odd constants a blocked filter multiplies the low half of a
 * key's hash by, one for each word of the block, and takes the top 5 bits of
 * the product as the bit to set in that word. They are Apache Parquet's.
 */
static const uint32_t BLOCK_SALT[BLOCK_WORDS] = {
    0x47b6137bU, 0x44974d91U, 0x8824ad5bU, 0xa2b7289dU, 0x705495c7U, 0x2df1424bU, 0x9efc4947U, 0x5c6bfb31U,
};

/**
 * @brief Where a key's bits are: the first, and how to get to the next.
 * @details Enhanced double hashing (Dillinger and Manolios): bit i is at
 * position % size, after position has been advanced by step, and step by i,
 * i times. Two 64-bit values made from one hash are as good as k independent
 * hashes for a Bloom filter, and cost one call to sof_hash_bytes().
 */
typedef struct probe_t {
    uint64_t position;
    uint64_t step;
} probe_t;

static uint32_t *block_of(const sof_bloom_t *bloom, uint64_t h);
static double block_rate(double keys_per_block);
static size_t memory_size(const sof_bloom_t *bloom);
static probe_t probe_start(const void *data, size_t length);
static size_t probe_next(probe_t *probe, unsigned i, size_t size);

/**
 * @brief Returns the block of a blocked filter that a key with hash h is in.
 * @details The high half of the hash, times the number of blocks, divided
 * by 2^32: a multiply and a shift that spread the hashes evenly over the
 * blocks, with no division. The low half chooses the bits in the block.
 */
static uint32_t *block_of(const sof_bloom_t *bloom, uint64_t h) {
    size_t block = (size_t)(((h >> 32) * (uint64_t)bloom->blocks) >> 32);
    return (uint32_t *)bloom->bits + block * BLOCK_WORDS;
}

/**
 * @brief Returns a blocked filter's false positive rate with an average of
 * keys_per_block keys in each block.
 * @details A block that holds j keys has each bit of a word set with chance
 * 1 - (31/32)^j, so says yes to a key it doesn't hold with chance
 * (1 - (31/32)^j)^8. The number of keys in a block is Poisson distributed,
 * so the rate is the sum of that, weighted by e^-L L^j / j!. The weights are
 * worked out relative to the most likely j's, from it outwards until they
 * are too small to matter, and divided by their sum at the end, which needs
 * no exp() or factorials and can't overflow.
 */
static double block_rate(double keys_per_block) {
    double lambda = keys_per_block;
    if (!(lambda > 0.0)) {
        return 0.0;
    }
    const double q = 31.0 / 32.0;
    double mode = floor(lambda);
    double q_mode = pow(q, mode);
    double total = 0.0, rate = 0.0;
    // Upwards from the mode, j = mode, mode + 1...
    double weight = 1.0, qj = q_mode;
    for (double j = mode;; j++) {
        double r = 1.0 - qj;
        r *= r;
        r *= r;
        total += weight;
        rate += weight * r * r;
        weight *= lambda / (j + 1.0);
        qj *= q;
        if (weight < 1e-17) {
            break;
        }
    }
    // Downwards, j = mode - 1 ... 0.
    weight = 1.0;
    qj = q_mode;
    for (double j = mode; j > 0.0; j--) {
        weight *= j / lambda;
        qj /= q;
        if (weight < 1e-17) {
            break;
        }
        double r = 1.0 - qj;
        r *= r;
        r *= r;
        total += weight;
        rate += weight * r * r;
    }
    return rate / total;
}

/// @brief Returns the size the filter's memory was allocated with.
static size_t memory_size(const sof_bloom_t *bloom) {
    if ((bloom->flags & SOF_BLOOM_BLOCKED) != 0) {
        return bloom->blocks * BLOCK_BYTES + (BLOCK_ALIGN - 1);
    }
    return bloom->words * sizeof(sof_bit_t);
}

/// @brief Hashes a key and returns where its bits start.
static probe_t probe_start(const void *data, size_t length) {
    uint64_t h = sof_hash_bytes(data, length, 0);
    probe_t probe = {h, sof_hash_mix(h ^ STEP_SALT)};
    return probe;
}

/// @brief Returns the bit position for hash i of the key, then advances the
/// probe to the next.
static size_t probe_next(probe_t *probe, unsigned i, size_t size) {
    size_t bit = (size_t)(probe->position % size);
    probe->position += probe->step;
    probe->step += i;
    return bit;
}

sof_bloom_error_t sof_bloom_add(sof_bloom_t *bloom, const void *data, size_t length, int *created) {
    if (created != NULL) {
        *created = 0;
    }
    if (bloom == NULL || bloom->bits == NULL) {
        return SOF_BLOOM_ERROR;
    }
    size_t added = 0;
    if ((bloom->flags & SOF_BLOOM_BLOCKED) != 0) {
        // One bit in each word of the key's block, the same count kept
        // without a branch. Copying the block in and out whole with
        // memcpy() was 10% faster for small filters on an AMD Ryzen, but
        // up to 9% slower for large ones on every machine measured.
        uint64_t h = sof_hash_bytes(data, length, 0);
        uint32_t *block = block_of(bloom, h);
        uint32_t low = (uint32_t)h;
        for (unsigned i = 0; i < BLOCK_WORDS; i++) {
            uint32_t mask = (uint32_t)1 << ((low * BLOCK_SALT[i]) >> 27);
            uint32_t w = block[i];
            added += (w & mask) == 0;
            block[i] = w | mask;
        }
    } else {
        // The fields are copied into locals because a store through a
        // sof_bit_t pointer (a size_t by default) could change them as far
        // as the compiler knows, and it would read them again for every
        // bit. The count is kept without a branch, which would mispredict
        // for new keys.
        sof_bit_t *bits = bloom->bits;
        size_t size = bloom->size;
        unsigned hashes = bloom->hashes;
        probe_t probe = probe_start(data, length);
        for (unsigned i = 0; i < hashes; i++) {
            size_t bit = probe_next(&probe, i, size);
            sof_bit_t *word = &bits[bit / SOF_BIT_BITS];
            sof_bit_t w = *word;
            sof_bit_t mask = sof_bit_set(0, (unsigned)(bit % SOF_BIT_BITS));
            added += (w & mask) == 0;
            *word = w | mask;
        }
    }
    if (added != 0) {
        bloom->set += added;
        bloom->count++;
        if (created != NULL) {
            *created = 1;
        }
    }
    return SOF_BLOOM_OK;
}

sof_bloom_error_t sof_bloom_add_string(sof_bloom_t *bloom, const char *s, int *created) {
    return sof_bloom_add(bloom, s, s != NULL ? strlen(s) : 0, created);
}

sof_bloom_error_t sof_bloom_clear(sof_bloom_t *bloom) {
    if (bloom == NULL) {
        return SOF_BLOOM_ERROR;
    }
    if (bloom->bits != NULL) {
        memset(bloom->bits, 0, bloom->words * sizeof(sof_bit_t));
    }
    bloom->set = 0;
    bloom->count = 0;
    return SOF_BLOOM_OK;
}

int sof_bloom_contains(const sof_bloom_t *bloom, const void *data, size_t length) {
    if (bloom == NULL || bloom->bits == NULL) {
        return 0;
    }
    if ((bloom->flags & SOF_BLOOM_BLOCKED) != 0) {
        // Every word is tested, and the results combined, with no branch
        // until the end.
        uint64_t h = sof_hash_bytes(data, length, 0);
        const uint32_t *block = block_of(bloom, h);
        uint32_t low = (uint32_t)h, missing = 0;
        for (unsigned i = 0; i < BLOCK_WORDS; i++) {
            missing |= ~block[i] & ((uint32_t)1 << ((low * BLOCK_SALT[i]) >> 27));
        }
        return missing == 0;
    }
    // Locals, as in sof_bloom_add().
    const sof_bit_t *bits = bloom->bits;
    size_t size = bloom->size;
    unsigned hashes = bloom->hashes;
    probe_t probe = probe_start(data, length);
    for (unsigned i = 0; i < hashes; i++) {
        size_t bit = probe_next(&probe, i, size);
        if (!sof_bit_test(bits[bit / SOF_BIT_BITS], (unsigned)(bit % SOF_BIT_BITS))) {
            return 0;
        }
    }
    return 1;
}

int sof_bloom_contains_string(const sof_bloom_t *bloom, const char *s) {
    return sof_bloom_contains(bloom, s, s != NULL ? strlen(s) : 0);
}

size_t sof_bloom_count(const sof_bloom_t *bloom) {
    return bloom != NULL && bloom->bits != NULL ? bloom->count : 0;
}

double sof_bloom_false_positive_rate(const sof_bloom_t *bloom) {
    if (bloom == NULL || bloom->bits == NULL) {
        return 0.0;
    }
    if ((bloom->flags & SOF_BLOOM_BLOCKED) != 0) {
        // With an average of L keys per block, a bit is 1 with chance
        // 1 - e^(-L/32), so the fraction of bits set gives L.
        if (bloom->set >= bloom->size) {
            return 1.0;
        }
        double keys_per_block = -32.0 * log1p(-(double)bloom->set / (double)bloom->size);
        return block_rate(keys_per_block);
    }
    return pow((double)bloom->set / (double)bloom->size, (double)bloom->hashes);
}

sof_bloom_error_t sof_bloom_free(sof_bloom_t *bloom) {
    if (bloom == NULL) {
        return SOF_BLOOM_ERROR;
    }
    if (bloom->memory != NULL) {
        sof_alloc_free(&bloom->alloc, bloom->memory, memory_size(bloom));
    }
    sof_alloc_t alloc = bloom->alloc;
    memset(bloom, 0, sizeof(*bloom));
    bloom->alloc = alloc;
    return SOF_BLOOM_OK;
}

sof_bloom_error_t sof_bloom_init(sof_bloom_t *bloom, size_t expected_keys, double false_positive_rate, unsigned flags, const sof_alloc_t *alloc) {
    if (bloom == NULL) {
        return SOF_BLOOM_ERROR;
    }
    memset(bloom, 0, sizeof(*bloom));
    // Written so that NaN fails too.
    if (!(false_positive_rate > 0.0 && false_positive_rate < 1.0) || (flags & ~(unsigned)SOF_BLOOM_BLOCKED) != 0 || !sof_alloc_valid(alloc)) {
        return SOF_BLOOM_ERROR;
    }
    if (alloc != NULL) {
        bloom->alloc = *alloc;
    }
    double n = expected_keys > 0 ? (double)expected_keys : 1.0;
    if ((flags & SOF_BLOOM_BLOCKED) != 0) {
        // Find the most keys per block that still give the rate, by
        // bisection: block_rate() rises with the keys per block. Below
        // 1e-12 keys per block there would be more than MAX_BLOCKS, and at
        // 1e4 the rate is 1. The bisection is on a log scale, since the
        // answer can be anywhere between; 64 halvings of that range leave
        // it far closer than a block. It takes 2 to 40 microseconds.
        double lo = 1e-12, hi = 1e4;
        for (int i = 0; i < 64; i++) {
            double mid = sqrt(lo * hi);
            if (block_rate(mid) > false_positive_rate) {
                hi = mid;
            } else {
                lo = mid;
            }
        }
        double blocks = ceil(n / lo);
        // The bit count, blocks * 256, and so the byte count, must fit in a
        // size_t, and block_of() needs no more than MAX_BLOCKS.
        if (blocks > MAX_BLOCKS || blocks * BLOCK_BYTES * 8 > (double)(SIZE_MAX / 2)) {
            return SOF_BLOOM_NO_MEM;
        }
        bloom->blocks = (size_t)blocks;
        bloom->words = bloom->blocks * BLOCK_BYTES / sizeof(sof_bit_t);
        bloom->hashes = BLOCK_WORDS;
        bloom->flags = flags;
        bloom->memory = sof_alloc_calloc(&bloom->alloc, memory_size(bloom), 1);
        // The blocks start at the first 64-byte boundary in the memory.
        uintptr_t start = ((uintptr_t)bloom->memory + (BLOCK_ALIGN - 1)) & ~(uintptr_t)(BLOCK_ALIGN - 1);
        bloom->bits = bloom->memory != NULL ? (sof_bit_t *)start : NULL;
    } else {
        double ln_p = log(false_positive_rate);
        double words = ceil(-n * ln_p / (LN2 * LN2) / SOF_BIT_BITS);
        // The bit count, words * SOF_BIT_BITS, and so the byte count, must
        // fit in a size_t. Half of SIZE_MAX is more memory than any machine
        // has.
        if (words * SOF_BIT_BITS > (double)(SIZE_MAX / 2)) {
            return SOF_BLOOM_NO_MEM;
        }
        double hashes = floor(-ln_p / LN2 + 0.5);
        bloom->words = words < 1.0 ? 1 : (size_t)words;
        bloom->hashes = hashes < 1.0 ? 1 : (unsigned)hashes;
        bloom->bits = sof_alloc_calloc(&bloom->alloc, bloom->words, sizeof(sof_bit_t));
        bloom->memory = bloom->bits;
    }
    bloom->size = bloom->words * SOF_BIT_BITS;
    if (bloom->bits == NULL) {
        sof_alloc_t copy = bloom->alloc;
        memset(bloom, 0, sizeof(*bloom));
        bloom->alloc = copy;
        return SOF_BLOOM_NO_MEM;
    }
    return SOF_BLOOM_OK;
}
