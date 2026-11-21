/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_bloom.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it. Most
 * tests run twice, for a classic filter and for a blocked one.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sof_bit.h"
#include "sof_bloom.h"
#include "test.h"

/// @brief fingerprint() of a classic filter for 1000 keys at 1%, with
/// 64-bit sof_bit_t words, after adding the 8-byte keys 0 to 999; the same
/// as before blocked filters were added to the library.
#define CLASSIC_FINGERPRINT 0x09a248beb18512edULL

/// @brief fingerprint() of a blocked filter made the same way.
#define BLOCKED_FINGERPRINT 0x1dfce9677166f115ULL

/// @brief The flags the tests that run for both kinds of filter give to
/// sof_bloom_init(): 0 for classic, then SOF_BLOOM_BLOCKED.
static unsigned flags;

/// @brief Returns the number of bits set in the filter, counted afresh.
static size_t bits_set(const sof_bloom_t *bloom) {
    size_t n = 0;
    if ((bloom->flags & SOF_BLOOM_BLOCKED) != 0) {
        // A blocked filter's bits are 32-bit words.
        const uint32_t *w = (const uint32_t *)bloom->bits;
        for (size_t i = 0; i < bloom->blocks * 8; i++) {
            n += sof_bit_count(w[i]);
        }
        return n;
    }
    for (size_t i = 0; i < bloom->words; i++) {
        n += sof_bit_count(bloom->bits[i]);
    }
    return n;
}

/// @brief Returns a value made from every word of the filter and its
/// position, the same on any byte order, to tell whether two filters' bits
/// are the same.
static uint64_t fingerprint(const sof_bloom_t *bloom) {
    uint64_t f = 0;
    if ((bloom->flags & SOF_BLOOM_BLOCKED) != 0) {
        const uint32_t *w = (const uint32_t *)bloom->bits;
        for (size_t i = 0; i < bloom->blocks * 8; i++) {
            f = (f ^ w[i]) * 0x100000001b3ULL + i;
        }
        return f;
    }
    for (size_t i = 0; i < bloom->words; i++) {
        f = (f ^ (uint64_t)bloom->bits[i]) * 0x100000001b3ULL + i;
    }
    return f;
}

static void test_sizing(void) {
    sof_bloom_t bloom;
    // 1000 keys at 1%: 9586 bits and 7 hashes, rounded up to whole words.
    CHECK(sof_bloom_init(&bloom, 1000, 0.01, 0, NULL) == SOF_BLOOM_OK);
    CHECK(bloom.bits != NULL);
    CHECK(bloom.memory == bloom.bits);
    CHECK(bloom.flags == 0 && bloom.blocks == 0);
    CHECK(bloom.hashes == 7);
    CHECK(bloom.size == bloom.words * SOF_BIT_BITS);
    CHECK(bloom.size >= 9586 && bloom.size < 9586 + SOF_BIT_BITS);
    CHECK(bloom.set == 0 && bloom.count == 0);
    CHECK(bits_set(&bloom) == 0);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
    // A tenth the rate: about 4.8 more bits per key and 3 more hashes.
    CHECK(sof_bloom_init(&bloom, 1000, 0.001, 0, NULL) == SOF_BLOOM_OK);
    CHECK(bloom.hashes == 10);
    CHECK(bloom.size >= 14378 && bloom.size < 14378 + SOF_BIT_BITS);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
    // 0 keys is taken as 1, and a high rate still gives one word and hash.
    CHECK(sof_bloom_init(&bloom, 0, 0.5, 0, NULL) == SOF_BLOOM_OK);
    CHECK(bloom.words == 1);
    CHECK(bloom.hashes == 1);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
    CHECK(sof_bloom_init(&bloom, 0, 0.999, 0, NULL) == SOF_BLOOM_OK);
    CHECK(bloom.words == 1);
    CHECK(bloom.hashes == 1);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
}

static void test_sizing_blocked(void) {
    sof_bloom_t bloom;
    // Bits per key for each rate, from the formula in sof_bloom_init()'s
    // documentation: about 6.0, 10.5, 16.9 and 26.3.
    static const struct {
        double rate, low, high;
    } sizes[] = {
        {0.1, 5.9, 6.1},
        {0.01, 10.4, 10.7},
        {0.001, 16.7, 17.1},
        {0.0001, 26.1, 26.6},
    };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        CHECK(sof_bloom_init(&bloom, 100000, sizes[i].rate, SOF_BLOOM_BLOCKED, NULL) == SOF_BLOOM_OK);
        CHECK(bloom.flags == SOF_BLOOM_BLOCKED);
        CHECK(bloom.hashes == 8);
        CHECK(bloom.size == bloom.blocks * 256);
        CHECK(bloom.size == bloom.words * SOF_BIT_BITS);
        double bits_per_key = (double)bloom.size / 100000.0;
        CHECK(bits_per_key > sizes[i].low && bits_per_key < sizes[i].high);
        // More bits than a classic filter for the same rate.
        sof_bloom_t classic;
        CHECK(sof_bloom_init(&classic, 100000, sizes[i].rate, 0, NULL) == SOF_BLOOM_OK);
        CHECK(bloom.size > classic.size);
        CHECK(sof_bloom_free(&classic) == SOF_BLOOM_OK);
        // The blocks start on a 64-byte boundary, less than 64 bytes into
        // the memory.
        CHECK(((uintptr_t)bloom.bits & 63) == 0);
        CHECK((char *)bloom.bits >= (char *)bloom.memory && (char *)bloom.bits < (char *)bloom.memory + 64);
        CHECK(bloom.set == 0 && bloom.count == 0 && bits_set(&bloom) == 0);
        CHECK(sof_bloom_false_positive_rate(&bloom) == 0.0);
        CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
    }
    // 0 keys is taken as 1, and a high rate still gives one block.
    CHECK(sof_bloom_init(&bloom, 0, 0.5, SOF_BLOOM_BLOCKED, NULL) == SOF_BLOOM_OK);
    CHECK(bloom.blocks == 1 && bloom.hashes == 8);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
    CHECK(sof_bloom_init(&bloom, 0, 0.999, SOF_BLOOM_BLOCKED, NULL) == SOF_BLOOM_OK);
    CHECK(bloom.blocks == 1);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
    // A key sets one bit in each word of one block.
    CHECK(sof_bloom_init(&bloom, 1000, 0.01, SOF_BLOOM_BLOCKED, NULL) == SOF_BLOOM_OK);
    CHECK(sof_bloom_add_string(&bloom, "apple", NULL) == SOF_BLOOM_OK);
    CHECK(bloom.set == 8 && bits_set(&bloom) == 8);
    const uint32_t *w = (const uint32_t *)bloom.bits;
    size_t used = 0;
    for (size_t b = 0; b < bloom.blocks; b++) {
        size_t in_block = 0;
        for (size_t i = 0; i < 8; i++) {
            in_block += sof_bit_count(w[b * 8 + i]);
        }
        if (in_block != 0) {
            used++;
            for (size_t i = 0; i < 8; i++) {
                CHECK(sof_bit_count(w[b * 8 + i]) == 1);
            }
        }
    }
    CHECK(used == 1);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
}

static void test_add_contains(void) {
    // 10,000 random keys, then 100,000 others that weren't added.
    enum { KEYS = 10000, OTHERS = 100000 };
    sof_bloom_t bloom;
    uint64_t state = 0xb100f;
    CHECK(sof_bloom_init(&bloom, KEYS, 0.01, flags, NULL) == SOF_BLOOM_OK);
    CHECK(sof_bloom_false_positive_rate(&bloom) == 0.0);
    for (size_t i = 0; i < KEYS; i++) {
        uint64_t key = next_random(&state);
        int before = sof_bloom_contains(&bloom, &key, sizeof(key));
        size_t count = bloom.count;
        int created = -1;
        CHECK(sof_bloom_add(&bloom, &key, sizeof(key), &created) == SOF_BLOOM_OK);
        // created is 1 exactly when the filter didn't already say yes.
        CHECK(created == !before);
        CHECK(bloom.count == count + (size_t)created);
        CHECK(sof_bloom_contains(&bloom, &key, sizeof(key)) == 1);
        // Adding it again changes nothing.
        CHECK(sof_bloom_add(&bloom, &key, sizeof(key), &created) == SOF_BLOOM_OK);
        CHECK(created == 0);
        CHECK(bloom.count == count + (size_t)!before);
    }
    // A few keys may have been false positives when added, so count can be
    // a little short of KEYS.
    CHECK(sof_bloom_count(&bloom) <= KEYS && sof_bloom_count(&bloom) > KEYS - KEYS / 50);
    CHECK(bloom.set == bits_set(&bloom));
    // No false negatives.
    state = 0xb100f;
    for (size_t i = 0; i < KEYS; i++) {
        uint64_t key = next_random(&state);
        CHECK(sof_bloom_contains(&bloom, &key, sizeof(key)) == 1);
    }
    // False positives near the 1% asked for, both measured and estimated.
    size_t false_positives = 0;
    state = 0x07e45;
    for (size_t i = 0; i < OTHERS; i++) {
        uint64_t key = next_random(&state);
        false_positives += (size_t)sof_bloom_contains(&bloom, &key, sizeof(key));
    }
    double measured = (double)false_positives / OTHERS;
    CHECK(measured > 0.005 && measured < 0.015);
    double estimate = sof_bloom_false_positive_rate(&bloom);
    CHECK(estimate > 0.005 && estimate < 0.015);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
}

static void test_rates(void) {
    // For each rate, 100,000 random keys, every one found afterwards, and
    // 1,000,000 others, of which the share wrongly found is within a
    // tolerance of the rate: the expected number of false positives, r *
    // OTHERS, has a standard deviation of about its square root, so the
    // tolerance is wider for the smaller rates.
    enum { KEYS = 100000, OTHERS = 1000000 };
    static const struct {
        double rate, tolerance;
    } rates[] = {
        {0.1, 0.05},
        {0.01, 0.10},
        {0.001, 0.20},
        {0.0001, 0.40},
    };
    for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
        sof_bloom_t bloom;
        double rate = rates[r].rate, tolerance = rates[r].tolerance;
        CHECK(sof_bloom_init(&bloom, KEYS, rate, flags, NULL) == SOF_BLOOM_OK);
        uint64_t state = 0x5eed0 + r;
        for (size_t i = 0; i < KEYS; i++) {
            uint64_t key = next_random(&state);
            CHECK(sof_bloom_add(&bloom, &key, sizeof(key), NULL) == SOF_BLOOM_OK);
        }
        state = 0x5eed0 + r;
        size_t found = 0;
        for (size_t i = 0; i < KEYS; i++) {
            uint64_t key = next_random(&state);
            found += (size_t)sof_bloom_contains(&bloom, &key, sizeof(key));
        }
        CHECK(found == KEYS);
        // Keys of 9 bytes, so none of them is one of the 8-byte keys added.
        size_t false_positives = 0;
        state = 0x07e45 + r;
        for (size_t i = 0; i < OTHERS; i++) {
            unsigned char key[9];
            uint64_t x = next_random(&state);
            memcpy(key, &x, sizeof(x));
            key[8] = 0;
            false_positives += (size_t)sof_bloom_contains(&bloom, key, sizeof(key));
        }
        double measured = (double)false_positives / OTHERS;
        CHECK(measured > rate * (1.0 - tolerance) && measured < rate * (1.0 + tolerance));
        double estimate = sof_bloom_false_positive_rate(&bloom);
        CHECK(estimate > rate * 0.85 && estimate < rate * 1.15);
        CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
    }
}

static void test_determinism(void) {
    // The same keys into two filters of the same size give the same bits,
    // and those bits are the same as they have always been: they depend
    // only on the keys, the size and sof_hash. The classic constant is for
    // 64-bit sof_bit_t words.
    sof_bloom_t a, b;
    CHECK(sof_bloom_init(&a, 1000, 0.01, flags, NULL) == SOF_BLOOM_OK);
    CHECK(sof_bloom_init(&b, 1000, 0.01, flags, NULL) == SOF_BLOOM_OK);
    CHECK(a.size == b.size);
    for (uint64_t i = 0; i < 1000; i++) {
        CHECK(sof_bloom_add(&a, &i, sizeof(i), NULL) == SOF_BLOOM_OK);
    }
    for (uint64_t i = 1000; i-- > 0;) {
        CHECK(sof_bloom_add(&b, &i, sizeof(i), NULL) == SOF_BLOOM_OK);
    }
    // The counts can differ, since which keys were false positives when
    // added depends on the order.
    CHECK(a.set == b.set);
    CHECK(fingerprint(&a) == fingerprint(&b));
    if ((flags & SOF_BLOOM_BLOCKED) != 0) {
        CHECK(a.blocks == 42);
        CHECK(fingerprint(&a) == BLOCKED_FINGERPRINT);
    } else if (sizeof(sof_bit_t) == 8) {
        CHECK(a.size == 9600);
        CHECK(fingerprint(&a) == CLASSIC_FINGERPRINT);
    }
    CHECK(sof_bloom_free(&a) == SOF_BLOOM_OK);
    CHECK(sof_bloom_free(&b) == SOF_BLOOM_OK);
}

static void test_strings(void) {
    sof_bloom_t bloom;
    int created = -1;
    CHECK(sof_bloom_init(&bloom, 1000, 0.01, flags, NULL) == SOF_BLOOM_OK);
    CHECK(sof_bloom_add_string(&bloom, "apple", &created) == SOF_BLOOM_OK);
    CHECK(created == 1);
    CHECK(sof_bloom_count(&bloom) == 1);
    CHECK(bloom.set > 0 && bloom.set <= bloom.hashes);
    CHECK(sof_bloom_contains_string(&bloom, "apple") == 1);
    // The NUL isn't part of the key, so a string and its bytes are the same.
    CHECK(sof_bloom_contains(&bloom, "apple", 5) == 1);
    CHECK(sof_bloom_contains_string(&bloom, "apples") == 0);
    CHECK(sof_bloom_contains_string(&bloom, "appl") == 0);
    CHECK(sof_bloom_add(&bloom, "pear", 4, NULL) == SOF_BLOOM_OK);
    CHECK(sof_bloom_contains_string(&bloom, "pear") == 1);
    CHECK(sof_bloom_add_string(&bloom, "pear", &created) == SOF_BLOOM_OK);
    CHECK(created == 0);
    CHECK(sof_bloom_count(&bloom) == 2);
    // NULL data is the empty key.
    CHECK(sof_bloom_contains(&bloom, NULL, 0) == 0);
    CHECK(sof_bloom_contains_string(&bloom, "") == 0);
    CHECK(sof_bloom_add_string(&bloom, NULL, &created) == SOF_BLOOM_OK);
    CHECK(created == 1);
    CHECK(sof_bloom_contains_string(&bloom, "") == 1);
    CHECK(sof_bloom_contains(&bloom, NULL, 5) == 1);
    CHECK(sof_bloom_contains(&bloom, "", 0) == 1);
    CHECK(sof_bloom_add(&bloom, NULL, 3, &created) == SOF_BLOOM_OK);
    CHECK(created == 0);
    CHECK(sof_bloom_count(&bloom) == 3);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
}

static void test_clear(void) {
    sof_bloom_t bloom;
    char buf[32];
    CHECK(sof_bloom_init(&bloom, 100, 0.01, flags, NULL) == SOF_BLOOM_OK);
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key%d", i);
        CHECK(sof_bloom_add_string(&bloom, buf, NULL) == SOF_BLOOM_OK);
    }
    size_t words = bloom.words;
    sof_bit_t *bits = bloom.bits;
    CHECK(sof_bloom_clear(&bloom) == SOF_BLOOM_OK);
    CHECK(bloom.bits == bits && bloom.words == words);
    CHECK(bloom.flags == flags);
    CHECK(bloom.set == 0 && bits_set(&bloom) == 0);
    CHECK(sof_bloom_count(&bloom) == 0);
    CHECK(sof_bloom_false_positive_rate(&bloom) == 0.0);
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key%d", i);
        CHECK(sof_bloom_contains_string(&bloom, buf) == 0);
    }
    // It still works after clearing.
    CHECK(sof_bloom_add_string(&bloom, "key5", NULL) == SOF_BLOOM_OK);
    CHECK(sof_bloom_contains_string(&bloom, "key5") == 1);
    CHECK(sof_bloom_count(&bloom) == 1);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
}

static void test_overfull(void) {
    // A filter sized for 10 keys, given 10,000, still has no false
    // negatives, but nearly every bit is set and nearly everything is a yes.
    sof_bloom_t bloom;
    uint64_t state = 0xf0115;
    CHECK(sof_bloom_init(&bloom, 10, 0.01, flags, NULL) == SOF_BLOOM_OK);
    for (size_t i = 0; i < 10000; i++) {
        uint64_t key = next_random(&state);
        CHECK(sof_bloom_add(&bloom, &key, sizeof(key), NULL) == SOF_BLOOM_OK);
    }
    CHECK(sof_bloom_count(&bloom) < 10000);
    CHECK(bloom.set == bits_set(&bloom));
    CHECK(sof_bloom_false_positive_rate(&bloom) > 0.99);
    state = 0xf0115;
    for (size_t i = 0; i < 10000; i++) {
        uint64_t key = next_random(&state);
        CHECK(sof_bloom_contains(&bloom, &key, sizeof(key)) == 1);
    }
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
}

static void test_bad_arguments(void) {
    sof_bloom_t bloom;
    const double rates[] = {0.0, 1.0, -0.5, 2.0, nan(""), INFINITY};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        memset(&bloom, 0xff, sizeof(bloom));
        CHECK(sof_bloom_init(&bloom, 100, rates[i], flags, NULL) == SOF_BLOOM_ERROR);
        CHECK(bloom.bits == NULL && bloom.words == 0 && bloom.size == 0);
        CHECK(bloom.memory == NULL && bloom.blocks == 0 && bloom.flags == 0);
        CHECK(sof_bloom_add_string(&bloom, "key", NULL) == SOF_BLOOM_ERROR);
    }
    // Flags that aren't sof_bloom_flag_t flags.
    const unsigned bad_flags[] = {2, 4, SOF_BLOOM_BLOCKED | 8, 0x80000000U};
    for (size_t i = 0; i < sizeof(bad_flags) / sizeof(bad_flags[0]); i++) {
        memset(&bloom, 0xff, sizeof(bloom));
        CHECK(sof_bloom_init(&bloom, 100, 0.01, bad_flags[i], NULL) == SOF_BLOOM_ERROR);
        CHECK(bloom.bits == NULL && bloom.memory == NULL && bloom.flags == 0);
        CHECK(sof_bloom_add_string(&bloom, "key", NULL) == SOF_BLOOM_ERROR);
    }
}

static void test_bad_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    sof_bloom_t bloom;
    CHECK(sof_bloom_init(&bloom, 100, 0.01, flags, &alloc) == SOF_BLOOM_ERROR);
    CHECK(bloom.bits == NULL);
    CHECK(sof_bloom_add_string(&bloom, "key", NULL) == SOF_BLOOM_ERROR);
    CHECK(state.allocations == 0);
}

static void test_sized_frees(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_bloom_t bloom;
    // One allocation, for the bits, freed with its size.
    CHECK(sof_bloom_init(&bloom, 5000, 0.001, flags, &alloc) == SOF_BLOOM_OK);
    CHECK(state.allocations == 1 && state.live == 1);
    CHECK(sof_bloom_add_string(&bloom, "key", NULL) == SOF_BLOOM_OK);
    CHECK(sof_bloom_clear(&bloom) == SOF_BLOOM_OK);
    CHECK(state.allocations == 1);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    // A filter too large for a size_t fails without trying to allocate.
    CHECK(sof_bloom_init(&bloom, SIZE_MAX, 1e-9, flags, &alloc) == SOF_BLOOM_NO_MEM);
    CHECK(bloom.bits == NULL && bloom.words == 0);
    CHECK(state.allocations == 1);
    CHECK(sof_bloom_add_string(&bloom, "key", NULL) == SOF_BLOOM_ERROR);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
    if ((flags & SOF_BLOOM_BLOCKED) != 0 && sizeof(size_t) == 8) {
        // So does a blocked filter for a rate so small that it would need
        // more than 2^32 blocks.
        CHECK(sof_bloom_init(&bloom, 1000, 1e-300, flags, &alloc) == SOF_BLOOM_NO_MEM);
        CHECK(bloom.bits == NULL && bloom.memory == NULL);
        CHECK(state.allocations == 1);
        // And a blocked filter of more than 2^32 blocks (about 2^32 * 24
        // keys at 1%), though a size_t could count it.
        CHECK(sof_bloom_init(&bloom, (size_t)1 << 37, 0.01, flags, &alloc) == SOF_BLOOM_NO_MEM);
        CHECK(bloom.bits == NULL && bloom.blocks == 0);
        CHECK(state.allocations == 1);
    }
}

static void test_no_memory(void) {
    test_alloc_t state = {0};
    state.fail_at = 1;
    sof_alloc_t alloc = test_allocator(&state);
    sof_bloom_t bloom;
    int created = -1;
    CHECK(sof_bloom_init(&bloom, 100, 0.01, flags, &alloc) == SOF_BLOOM_NO_MEM);
    CHECK(state.allocations == 1);
    CHECK(bloom.bits == NULL && bloom.words == 0 && bloom.size == 0 && bloom.hashes == 0);
    CHECK(bloom.memory == NULL && bloom.blocks == 0 && bloom.flags == 0);
    CHECK(sof_bloom_add_string(&bloom, "key", &created) == SOF_BLOOM_ERROR);
    CHECK(created == 0);
    CHECK(sof_bloom_contains_string(&bloom, "key") == 0);
    CHECK(sof_bloom_false_positive_rate(&bloom) == 0.0);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    // Initialized again, it works.
    CHECK(sof_bloom_init(&bloom, 100, 0.01, flags, &alloc) == SOF_BLOOM_OK);
    CHECK(sof_bloom_add_string(&bloom, "key", NULL) == SOF_BLOOM_OK);
    CHECK(sof_bloom_contains_string(&bloom, "key") == 1);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_null_and_freed(void) {
    sof_bloom_t bloom;
    int created = -1;
    CHECK(sof_bloom_init(NULL, 100, 0.01, flags, NULL) == SOF_BLOOM_ERROR);
    CHECK(sof_bloom_add(NULL, "key", 3, &created) == SOF_BLOOM_ERROR);
    CHECK(created == 0);
    created = -1;
    CHECK(sof_bloom_add_string(NULL, "key", &created) == SOF_BLOOM_ERROR);
    CHECK(created == 0);
    CHECK(sof_bloom_clear(NULL) == SOF_BLOOM_ERROR);
    CHECK(sof_bloom_contains(NULL, "key", 3) == 0);
    CHECK(sof_bloom_contains_string(NULL, "key") == 0);
    CHECK(sof_bloom_count(NULL) == 0);
    CHECK(sof_bloom_false_positive_rate(NULL) == 0.0);
    CHECK(sof_bloom_free(NULL) == SOF_BLOOM_ERROR);
    // A freed filter is empty, and can't be added to.
    CHECK(sof_bloom_init(&bloom, 100, 0.01, flags, NULL) == SOF_BLOOM_OK);
    CHECK(sof_bloom_add_string(&bloom, "key", NULL) == SOF_BLOOM_OK);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
    CHECK(sof_bloom_free(&bloom) == SOF_BLOOM_OK);
    CHECK(bloom.bits == NULL && bloom.words == 0 && bloom.size == 0 && bloom.set == 0);
    CHECK(bloom.memory == NULL && bloom.blocks == 0 && bloom.flags == 0);
    CHECK(sof_bloom_contains_string(&bloom, "key") == 0);
    CHECK(sof_bloom_count(&bloom) == 0);
    CHECK(sof_bloom_false_positive_rate(&bloom) == 0.0);
    created = -1;
    CHECK(sof_bloom_add_string(&bloom, "key", &created) == SOF_BLOOM_ERROR);
    CHECK(created == 0);
    CHECK(sof_bloom_clear(&bloom) == SOF_BLOOM_OK);
    CHECK(sof_bloom_contains_string(&bloom, "key") == 0);
}

int main(void) {
    printf("sof_bloom:\n");
    run("classic sizing", test_sizing);
    run("blocked sizing, alignment and one block per key", test_sizing_blocked);
    static const struct {
        const char *name;
        void (*test)(void);
    } tests[] = {
        {"add and contains, with false positives near the rate", test_add_contains},
        {"no false negatives, and false positives at several rates", test_rates},
        {"the same bits for the same keys", test_determinism},
        {"strings, bytes and NULL", test_strings},
        {"clear", test_clear},
        {"more keys than expected", test_overfull},
        {"bad false positive rates and flags", test_bad_arguments},
        {"bad allocator", test_bad_allocator},
        {"allocator sizes and too large a filter", test_sized_frees},
        {"failing allocation", test_no_memory},
        {"NULL and freed filters", test_null_and_freed},
    };
    static const struct {
        const char *kind;
        unsigned flags;
    } kinds[] = {
        {"classic", 0},
        {"blocked", SOF_BLOOM_BLOCKED},
    };
    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        flags = kinds[k].flags;
        for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
            char name[128];
            snprintf(name, sizeof(name), "%s: %s", kinds[k].kind, tests[i].name);
            run(name, tests[i].test);
        }
    }
    return test_report();
}
