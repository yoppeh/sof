/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for the sof_sort library.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 *
 * Every sort is run on shuffled permutations, duplicates, sorted,
 * reversed and other patterned input, elements of many sizes at aligned and
 * unaligned addresses, an inconsistent compare function, and the NULL and
 * invalid cases. The stable sorts are checked for stability, the
 * O(n log n) sorts for their number of compares on patterned input and
 * against McIlroy's adversary, quicksort for linear time on sorted and
 * equal input, and sof_sort_merge() with the test allocator. The searches are checked
 * against linear scans of random sorted arrays, and on their NULL and
 * invalid cases.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_sort.h"
#include "test.h"

/// @brief The largest array most tests sort.
#define MAX_COUNT 5000

/// @brief The size of the large arrays the O(n log n) sorts are given.
#define LARGE_COUNT 200000

/// @brief The largest element test_element_sizes() sorts.
#define MAX_SIZE 40

/// @brief The number of records test_element_sizes() sorts.
#define RECORDS 500

/// @brief The largest array test_inputs() and test_stable_sizes() sort.
#define WIDE_COUNT 10000

/// @brief The largest element test_inputs() and test_stable_sizes() sort.
#define WIDE_SIZE 24

/// @brief The number of input patterns make_input() makes.
#define INPUTS 7

/// @brief A sort function with the arguments all of them share.
typedef sof_sort_error_t (*sort_func_t)(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx);

/// @brief The ctx the compare functions take: counts calls, and can reverse
/// the order.
typedef struct counter_t {
    /// @brief The number of compares.
    size_t calls;
    /// @brief Non-zero to sort in descending order.
    int descending;
} counter_t;

/// @brief A key with the position it started at, for the stability test.
typedef struct pair_t {
    uint32_t key;
    uint32_t index;
} pair_t;

/// @brief The ctx compare_key() takes: how many bytes at the front of each
/// element are its key, and a count of compares.
typedef struct key_ctx_t {
    /// @brief The key's width in bytes, 1 to 4, stored big-endian.
    size_t width;
    /// @brief The number of compares.
    size_t calls;
} key_ctx_t;

/// @brief The ctx compare_adversary() takes: McIlroy's adversary for
/// quicksort, which decides the order of elements only as the sort asks.
typedef struct adversary_t {
    /// @brief Each element's value: gas until it is frozen.
    uint32_t *value;
    /// @brief The value of an element not yet frozen, above every frozen one.
    uint32_t gas;
    /// @brief The value the next element frozen gets.
    uint32_t solid;
    /// @brief The gas element most recently compared, the likely pivot.
    uint32_t candidate;
    /// @brief The number of compares.
    size_t calls;
} adversary_t;

static int compare_adversary(const void *a, const void *b, void *ctx);
static int compare_byte(const void *a, const void *b, void *ctx);
static int compare_key(const void *a, const void *b, void *ctx);
static int compare_random(const void *a, const void *b, void *ctx);
static uint32_t decode_key(const unsigned char *e, size_t width);
static void encode_element(unsigned char *e, uint32_t key, size_t width, size_t size);
static size_t log_bound(size_t n);
static void make_input(uint32_t *keys, size_t n, int pattern, uint32_t mask, uint64_t *state);
static uint64_t mix(uint32_t key);
static int compare_pair(const void *a, const void *b, void *ctx);
static int compare_u32(const void *a, const void *b, void *ctx);
static int compare_u64(const void *a, const void *b, void *ctx);
static int counts_match(const uint32_t *a, const uint32_t *b, size_t n, size_t range);
static int is_sorted(const uint32_t *a, size_t n);
static sof_sort_error_t merge_standard(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx);
static void shuffle(uint32_t *a, size_t n, uint64_t *state);

/// @brief Every sort, with sof_sort_merge() using the standard allocator.
static const struct {
    const char *name;
    sort_func_t sort;
    int stable;
    int quadratic;
} sorts[] = {
    {"bubble", sof_sort_bubble, 1, 1},
    {"heap", sof_sort_heap, 0, 0},
    {"insertion", sof_sort_insertion, 1, 1},
    {"merge", merge_standard, 1, 0},
    {"quick", sof_sort_quick, 0, 0},
};
#define NUM_SORTS (sizeof(sorts) / sizeof(sorts[0]))

static uint32_t array[LARGE_COUNT];
static uint32_t original[LARGE_COUNT];
static uint32_t histogram[LARGE_COUNT];
static unsigned char records[RECORDS * MAX_SIZE];

/// @brief McIlroy's "killer adversary" for quicksort (Software: Practice
/// and Experience, 1999). Elements are indices into ctx's values, which
/// start as gas; when two gas elements meet, one is frozen at the next
/// solid value, the one that isn't the likely pivot. Gas compares above
/// every solid value, so the order is consistent, but a quicksort that
/// can't defend itself takes n^2 compares.
static int compare_adversary(const void *a, const void *b, void *ctx) {
    adversary_t *v = ctx;
    uint32_t x, y;
    memcpy(&x, a, sizeof(x));
    memcpy(&y, b, sizeof(y));
    v->calls++;
    if (v->value[x] == v->gas && v->value[y] == v->gas) {
        if (x == v->candidate) {
            v->value[x] = v->solid++;
        } else {
            v->value[y] = v->solid++;
        }
    }
    if (v->value[x] == v->gas) {
        v->candidate = x;
    } else if (v->value[y] == v->gas) {
        v->candidate = y;
    }
    return (v->value[x] > v->value[y]) - (v->value[x] < v->value[y]);
}

/// @brief Compares the first byte of two records.
static int compare_byte(const void *a, const void *b, void *ctx) {
    const unsigned char *x = a, *y = b;
    return (*x > *y) - (*x < *y);
}

/// @brief Compares the big-endian keys at the front of two elements, whose
/// width is in ctx (a key_ctx_t), counting the call.
static int compare_key(const void *a, const void *b, void *ctx) {
    key_ctx_t *k = ctx;
    k->calls++;
    return memcmp(a, b, k->width);
}

/// @brief An inconsistent compare: a random answer each time, from the
/// xorshift state in ctx.
static int compare_random(const void *a, const void *b, void *ctx) {
    return (int)(next_random(ctx) % 3) - 1;
}

/// @brief Returns the big-endian key of width bytes at e.
static uint32_t decode_key(const unsigned char *e, size_t width) {
    uint32_t key = 0;
    for (size_t j = 0; j < width; j++) {
        key = key << 8 | e[j];
    }
    return key;
}

/// @brief Writes key big-endian into the first width bytes of the element
/// of size bytes at e, and fills the rest with bytes made from the key, so
/// they can be checked to have travelled with it.
static void encode_element(unsigned char *e, uint32_t key, size_t width, size_t size) {
    for (size_t j = 0; j < width; j++) {
        e[j] = (unsigned char)(key >> (8 * (width - 1 - j)));
    }
    for (size_t j = width; j < size; j++) {
        e[j] = (unsigned char)(key * 31 + j);
    }
}

/// @brief The most compares an O(n log n) sort may take on n elements:
/// 4 n (log2(n) + 2), rounded up.
static size_t log_bound(size_t n) {
    size_t log2n = 0;
    for (size_t m = n; m > 1; m >>= 1) {
        log2n++;
    }
    return 4 * n * (log2n + 2);
}

/// @brief Fills keys with n keys in one of INPUTS patterns, each masked
/// with mask: random, sorted, reversed, all equal, organ pipe, few unique
/// and sawtooth.
static void make_input(uint32_t *keys, size_t n, int pattern, uint32_t mask, uint64_t *state) {
    for (size_t i = 0; i < n; i++) {
        uint32_t key = 0;
        switch (pattern) {
        case 0:
            key = (uint32_t)next_random(state);
            break;
        case 1:
            key = (uint32_t)i;
            break;
        case 2:
            key = (uint32_t)(n - 1 - i);
            break;
        case 3:
            key = 7;
            break;
        case 4:
            key = (uint32_t)(i < n / 2 ? i : n - 1 - i);
            break;
        case 5:
            key = (uint32_t)(next_random(state) % 4);
            break;
        default:
            key = (uint32_t)(i % 17);
            break;
        }
        keys[i] = key & mask;
    }
}

/// @brief Scrambles a key, for an order-independent checksum of a multiset
/// of keys.
static uint64_t mix(uint32_t key) {
    uint64_t x = (key + 1) * 0x9e3779b97f4a7c15ULL;
    x ^= x >> 29;
    x *= 0xbf58476d1ce4e5b9ULL;
    return x ^ (x >> 32);
}

/// @brief Compares two pairs by key only.
static int compare_pair(const void *a, const void *b, void *ctx) {
    const pair_t *x = a, *y = b;
    return (x->key > y->key) - (x->key < y->key);
}

/// @brief Compares two uint32_t, counting the call in ctx (a counter_t) if
/// it isn't NULL, and reversing the order if it asks.
static int compare_u32(const void *a, const void *b, void *ctx) {
    uint32_t x, y;
    memcpy(&x, a, sizeof(x));
    memcpy(&y, b, sizeof(y));
    counter_t *counter = ctx;
    if (counter != NULL) {
        counter->calls++;
        if (counter->descending) {
            return (y > x) - (y < x);
        }
    }
    return (x > y) - (x < y);
}

/// @brief Returns 1 if a and b hold the same values, all below range, as
/// many times each, or 0.
static int counts_match(const uint32_t *a, const uint32_t *b, size_t n, size_t range) {
    memset(histogram, 0, range * sizeof(histogram[0]));
    for (size_t i = 0; i < n; i++) {
        histogram[a[i]]++;
    }
    for (size_t i = 0; i < n; i++) {
        if (histogram[b[i]]-- == 0) {
            return 0;
        }
    }
    return 1;
}

/// @brief Returns 1 if the n values at a are in ascending order, or 0.
static int is_sorted(const uint32_t *a, size_t n) {
    for (size_t i = 1; i < n; i++) {
        if (a[i - 1] > a[i]) {
            return 0;
        }
    }
    return 1;
}

/// @brief sof_sort_merge() with the standard allocator, to fit sort_func_t.
static sof_sort_error_t merge_standard(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx) {
    return sof_sort_merge(base, count, size, cmp_func, ctx, NULL);
}

/// @brief Shuffles the n values at a.
static void shuffle(uint32_t *a, size_t n, uint64_t *state) {
    for (size_t i = n; i > 1; i--) {
        size_t j = next_random(state) % i;
        uint32_t t = a[i - 1];
        a[i - 1] = a[j];
        a[j] = t;
    }
}

/// @brief Every size from 0 to 100, then larger ones: shuffled 0..n-1 must
/// come out as 0..n-1.
static void test_permutations(void) {
    static const size_t larger[] = {127, 128, 129, 257, 1000, 4096, MAX_COUNT};
    uint64_t state = 0x1234567887654321ULL;
    for (size_t s = 0; s < NUM_SORTS; s++) {
        for (size_t k = 0; k < 101 + sizeof(larger) / sizeof(larger[0]); k++) {
            size_t n = k < 101 ? k : larger[k - 101];
            for (size_t i = 0; i < n; i++) {
                array[i] = (uint32_t)i;
            }
            shuffle(array, n, &state);
            CHECK(sorts[s].sort(array, n, sizeof(uint32_t), compare_u32, NULL) == SOF_SORT_OK);
            for (size_t i = 0; i < n; i++) {
                CHECK(array[i] == i);
            }
        }
    }
}

/// @brief Random values from small ranges, so there are many duplicates.
static void test_duplicates(void) {
    static const size_t ranges[] = {1, 2, 3, 8, 100};
    uint64_t state = 0xfeedfacecafebeefULL;
    for (size_t s = 0; s < NUM_SORTS; s++) {
        for (size_t r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++) {
            size_t n = 3000;
            for (size_t i = 0; i < n; i++) {
                original[i] = array[i] = (uint32_t)(next_random(&state) % ranges[r]);
            }
            CHECK(sorts[s].sort(array, n, sizeof(uint32_t), compare_u32, NULL) == SOF_SORT_OK);
            CHECK(is_sorted(array, n));
            CHECK(counts_match(original, array, n, ranges[r]));
        }
    }
}

/// @brief Fills a with n values below n in one of the patterns, numbered
/// from 0; returns 0 past the last pattern.
static int make_pattern(uint32_t *a, size_t n, int pattern) {
    for (size_t i = 0; i < n; i++) {
        switch (pattern) {
        case 0: // sorted
            a[i] = (uint32_t)i;
            break;
        case 1: // reversed
            a[i] = (uint32_t)(n - 1 - i);
            break;
        case 2: // all equal
            a[i] = 7 % n;
            break;
        case 3: // organ pipe: up, then down
            a[i] = (uint32_t)(i < n / 2 ? i : n - 1 - i);
            break;
        case 4: // sawtooth
            a[i] = (uint32_t)(i % 50);
            break;
        case 5: // sorted, with every 100th element swapped with its neighbour
            a[i] = (uint32_t)(i % 100 == 0 && i + 1 < n ? i + 1 : i % 100 == 1 ? i - 1 : i);
            break;
        default:
            return 0;
        }
    }
    return 1;
}

/// @brief Sorted, reversed and other patterns that trouble simple sorts.
static void test_patterns(void) {
    size_t n = 3000;
    for (size_t s = 0; s < NUM_SORTS; s++) {
        for (int p = 0; make_pattern(original, n, p); p++) {
            memcpy(array, original, n * sizeof(uint32_t));
            CHECK(sorts[s].sort(array, n, sizeof(uint32_t), compare_u32, NULL) == SOF_SORT_OK);
            CHECK(is_sorted(array, n));
            CHECK(counts_match(original, array, n, n));
        }
    }
}

/// @brief The O(n log n) sorts on large patterned and random arrays, with
/// their compares counted: none may approach n^2.
static void test_large(void) {
    size_t n = LARGE_COUNT;
    size_t log2n = 0;
    for (size_t m = n; m > 1; m >>= 1) {
        log2n++;
    }
    log2n++; // rounded up
    uint64_t state = 0x0123456789abcdefULL;
    for (size_t s = 0; s < NUM_SORTS; s++) {
        if (sorts[s].quadratic) {
            continue;
        }
        for (int p = 0; p <= 6; p++) {
            if (p == 6) {
                for (size_t i = 0; i < n; i++) {
                    original[i] = (uint32_t)(next_random(&state) % n);
                }
            } else {
                make_pattern(original, n, p);
            }
            memcpy(array, original, n * sizeof(uint32_t));
            counter_t counter = {0, 0};
            CHECK(sorts[s].sort(array, n, sizeof(uint32_t), compare_u32, &counter) == SOF_SORT_OK);
            CHECK(is_sorted(array, n));
            CHECK(counts_match(original, array, n, n));
            CHECK(counter.calls <= 4 * n * log2n);
        }
    }
}

/// @brief Every sort on every input pattern, at every size from 0 to 300
/// and some larger ones (the quadratic sorts up to 100), with elements of
/// 1 to 24 bytes, at aligned and unaligned addresses. The output must be
/// sorted, hold the same keys, keep each element's other bytes with its
/// key, and, for the O(n log n) sorts, take no more than log_bound()
/// compares.
static void test_inputs(void) {
    static unsigned char elements[WIDE_COUNT * WIDE_SIZE + 4];
    static uint32_t keys[WIDE_COUNT];
    static const size_t sizes[] = {1, 2, 3, 4, 8, 12, 16, WIDE_SIZE};
    static const size_t larger[] = {1000, 4099, WIDE_COUNT};
    uint64_t state = 0x1357924680acebdfULL;
    for (size_t s = 0; s < NUM_SORTS; s++) {
        for (size_t z = 0; z < sizeof(sizes) / sizeof(sizes[0]); z++) {
            size_t size = sizes[z];
            size_t width = size < 4 ? size : 4;
            uint32_t mask = width == 4 ? UINT32_MAX : ((uint32_t)1 << (8 * width)) - 1;
            for (int p = 0; p < INPUTS; p++) {
                for (size_t k = 0; k < 301 + sizeof(larger) / sizeof(larger[0]); k++) {
                    size_t n = k < 301 ? k : larger[k - 301];
                    if (sorts[s].quadratic && n > 100) {
                        continue;
                    }
                    // Offsets of 0 to 3 bytes, so 4- and 8-byte elements
                    // are often unaligned.
                    unsigned char *base = elements + n % 4;
                    make_input(keys, n, p, mask, &state);
                    uint64_t sum = 0;
                    for (size_t i = 0; i < n; i++) {
                        encode_element(base + i * size, keys[i], width, size);
                        sum += mix(keys[i]);
                    }
                    key_ctx_t ctx = {width, 0};
                    CHECK(sorts[s].sort(base, n, size, compare_key, &ctx) == SOF_SORT_OK);
                    uint32_t previous = 0;
                    for (size_t i = 0; i < n; i++) {
                        unsigned char *e = base + i * size;
                        uint32_t key = decode_key(e, width);
                        CHECK(key >= previous);
                        previous = key;
                        for (size_t j = width; j < size; j++) {
                            CHECK(e[j] == (unsigned char)(key * 31 + j));
                        }
                        sum -= mix(key);
                    }
                    CHECK(sum == 0);
                    if (!sorts[s].quadratic) {
                        CHECK(ctx.calls <= log_bound(n));
                    }
                }
            }
        }
    }
}

/// @brief The stable sorts keep equal keys in order with elements of 3 to
/// 24 bytes, which merge sort copies in different ways, aligned and
/// unaligned, on every input pattern with keys of a few values.
static void test_stable_sizes(void) {
    static unsigned char elements[WIDE_COUNT * WIDE_SIZE + 4];
    static uint32_t keys[WIDE_COUNT];
    // Each element is a key of width bytes, then its starting index in 2.
    static const struct {
        size_t size, width;
    } shapes[] = {{3, 1}, {4, 2}, {8, 4}, {12, 4}, {16, 2}, {WIDE_SIZE, 4}};
    static const size_t counts[] = {0, 1, 2, 17, 100, 129, 1000, WIDE_COUNT};
    uint64_t state = 0x2468ace013579bdfULL;
    for (size_t s = 0; s < NUM_SORTS; s++) {
        if (!sorts[s].stable) {
            continue;
        }
        for (size_t h = 0; h < sizeof(shapes) / sizeof(shapes[0]); h++) {
            size_t size = shapes[h].size, width = shapes[h].width;
            for (int p = 0; p < INPUTS; p++) {
                for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); c++) {
                    size_t n = counts[c];
                    if (sorts[s].quadratic && n > 1000) {
                        continue;
                    }
                    for (size_t offset = 0; offset < 4; offset += 3) {
                        unsigned char *base = elements + offset;
                        // Keys of a few values only, so there are many equal.
                        make_input(keys, n, p, 7, &state);
                        for (size_t i = 0; i < n; i++) {
                            unsigned char *e = base + i * size;
                            memset(e, 0, size);
                            encode_element(e, keys[i], width, width);
                            e[width] = (unsigned char)(i >> 8);
                            e[width + 1] = (unsigned char)i;
                        }
                        key_ctx_t ctx = {width, 0};
                        CHECK(sorts[s].sort(base, n, size, compare_key, &ctx) == SOF_SORT_OK);
                        for (size_t i = 1; i < n; i++) {
                            unsigned char *e = base + i * size, *d = e - size;
                            uint32_t key = decode_key(e, width), before = decode_key(d, width);
                            CHECK(before <= key);
                            CHECK(before < key || decode_key(d + width, 2) < decode_key(e + width, 2));
                        }
                    }
                }
            }
        }
    }
}

/// @brief The O(n log n) sorts against McIlroy's adversary, which drives a
/// plain median-of-three quicksort to n^2 compares: they must stay within
/// log_bound(), and the result must be in the order the adversary chose.
static void test_adversary(void) {
    static const size_t counts[] = {100, 1000, 10000, LARGE_COUNT};
    for (size_t s = 0; s < NUM_SORTS; s++) {
        if (sorts[s].quadratic) {
            continue;
        }
        for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); c++) {
            size_t n = counts[c];
            adversary_t v = {original, (uint32_t)n, 0, 0, 0};
            for (size_t i = 0; i < n; i++) {
                array[i] = (uint32_t)i;
                original[i] = (uint32_t)n;
            }
            CHECK(sorts[s].sort(array, n, sizeof(uint32_t), compare_adversary, &v) == SOF_SORT_OK);
            CHECK(v.calls <= log_bound(n));
            for (size_t i = 1; i < n; i++) {
                CHECK(original[array[i - 1]] <= original[array[i]]);
            }
        }
    }
}

/// @brief An inconsistent compare function leaves the array in some order,
/// but with every element still in it and nothing outside it touched
/// (which the sanitizers and valgrind check).
static void test_inconsistent(void) {
    static unsigned char elements[WIDE_COUNT * WIDE_SIZE + 4];
    static const size_t sizes[] = {1, 4, 8, 12};
    static const size_t counts[] = {2, 3, 16, 17, 100, 129, 1000, 5000};
    uint64_t state = 0x0f1e2d3c4b5a6978ULL;
    for (size_t s = 0; s < NUM_SORTS; s++) {
        for (size_t z = 0; z < sizeof(sizes) / sizeof(sizes[0]); z++) {
            size_t size = sizes[z], width = size < 4 ? size : 4;
            for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); c++) {
                size_t n = counts[c];
                if (sorts[s].quadratic && n > 1000) {
                    continue;
                }
                unsigned char *base = elements + 1;
                uint64_t sum = 0;
                for (size_t i = 0; i < n; i++) {
                    uint32_t key = (uint32_t)i & (width == 4 ? UINT32_MAX : 0xff);
                    encode_element(base + i * size, key, width, size);
                    sum += mix(key);
                }
                CHECK(sorts[s].sort(base, n, size, compare_random, &state) == SOF_SORT_OK);
                for (size_t i = 0; i < n; i++) {
                    unsigned char *e = base + i * size;
                    uint32_t key = decode_key(e, width);
                    for (size_t j = width; j < size; j++) {
                        CHECK(e[j] == (unsigned char)(key * 31 + j));
                    }
                    sum -= mix(key);
                }
                CHECK(sum == 0);
            }
        }
    }
}

/// @brief Quicksort takes O(n) compares on sorted, reversed and all-equal
/// input, which its partial insertion sort and its handling of runs equal
/// to the previous pivot are for.
static void test_quick_linear(void) {
    size_t n = LARGE_COUNT;
    for (int p = 0; p < 3; p++) {
        for (size_t i = 0; i < n; i++) {
            array[i] = p == 0 ? (uint32_t)i : p == 1 ? (uint32_t)(n - i) : 5;
        }
        counter_t counter = {0, 0};
        CHECK(sof_sort_quick(array, n, sizeof(uint32_t), compare_u32, &counter) == SOF_SORT_OK);
        CHECK(is_sorted(array, n));
        CHECK(counter.calls <= 4 * n);
    }
}

/// @brief Records of 1 to 40 bytes, sorted by their first byte: every
/// other byte must travel with it.
static void test_element_sizes(void) {
    static const size_t sizes[] = {1, 2, 3, 7, 8, 9, 13, 16, 17, MAX_SIZE};
    uint64_t state = 0xa5a5a5a55a5a5a5aULL;
    for (size_t s = 0; s < NUM_SORTS; s++) {
        for (size_t z = 0; z < sizeof(sizes) / sizeof(sizes[0]); z++) {
            size_t size = sizes[z];
            uint32_t counts[256] = {0};
            for (size_t r = 0; r < RECORDS; r++) {
                unsigned char *record = records + r * size;
                record[0] = (unsigned char)next_random(&state);
                counts[record[0]]++;
                for (size_t k = 1; k < size; k++) {
                    record[k] = (unsigned char)(record[0] + k * 37);
                }
            }
            CHECK(sorts[s].sort(records, RECORDS, size, compare_byte, NULL) == SOF_SORT_OK);
            for (size_t r = 0; r < RECORDS; r++) {
                unsigned char *record = records + r * size;
                CHECK(r == 0 || record[-(ptrdiff_t)size] <= record[0]);
                CHECK(counts[record[0]]-- > 0);
                for (size_t k = 1; k < size; k++) {
                    CHECK(record[k] == (unsigned char)(record[0] + k * 37));
                }
            }
        }
    }
}

/// @brief The stable sorts keep equal keys in the order they started in.
static void test_stability(void) {
    static pair_t pairs[MAX_COUNT];
    uint64_t state = 0x5eedf00d5eedf00dULL;
    for (size_t s = 0; s < NUM_SORTS; s++) {
        if (!sorts[s].stable) {
            continue;
        }
        for (size_t n = 0; n <= MAX_COUNT; n += n < 40 ? 1 : 997) {
            for (size_t i = 0; i < n; i++) {
                pairs[i].key = (uint32_t)(next_random(&state) % 10);
                pairs[i].index = (uint32_t)i;
            }
            CHECK(sorts[s].sort(pairs, n, sizeof(pair_t), compare_pair, NULL) == SOF_SORT_OK);
            for (size_t i = 1; i < n; i++) {
                CHECK(pairs[i - 1].key <= pairs[i].key);
                CHECK(pairs[i - 1].key < pairs[i].key || pairs[i - 1].index < pairs[i].index);
            }
        }
    }
}

/// @brief ctx reaches the compare function: it reverses the order, and
/// counts the compares.
static void test_ctx(void) {
    uint64_t state = 0xc0ffee00c0ffee00ULL;
    for (size_t s = 0; s < NUM_SORTS; s++) {
        size_t n = 1000;
        for (size_t i = 0; i < n; i++) {
            array[i] = (uint32_t)i;
        }
        shuffle(array, n, &state);
        counter_t counter = {0, 1};
        CHECK(sorts[s].sort(array, n, sizeof(uint32_t), compare_u32, &counter) == SOF_SORT_OK);
        CHECK(counter.calls > 0);
        for (size_t i = 0; i < n; i++) {
            CHECK(array[i] == n - 1 - i);
        }
        // Nothing to compare in an empty or one-element array.
        counter.calls = 0;
        CHECK(sorts[s].sort(array, 0, sizeof(uint32_t), compare_u32, &counter) == SOF_SORT_OK);
        CHECK(sorts[s].sort(array, 1, sizeof(uint32_t), compare_u32, &counter) == SOF_SORT_OK);
        CHECK(counter.calls == 0);
        CHECK(array[0] == n - 1);
    }
}

/// @brief Bubble and insertion sort take exactly n - 1 compares, and merge
/// sort no more than n, on an array that is already sorted.
static void test_sorted_compares(void) {
    size_t n = 1000;
    for (size_t i = 0; i < n; i++) {
        array[i] = (uint32_t)i;
    }
    counter_t counter = {0, 0};
    CHECK(sof_sort_bubble(array, n, sizeof(uint32_t), compare_u32, &counter) == SOF_SORT_OK);
    CHECK(counter.calls == n - 1);
    counter.calls = 0;
    CHECK(sof_sort_insertion(array, n, sizeof(uint32_t), compare_u32, &counter) == SOF_SORT_OK);
    CHECK(counter.calls == n - 1);
    counter.calls = 0;
    CHECK(sof_sort_merge(array, n, sizeof(uint32_t), compare_u32, &counter, NULL) == SOF_SORT_OK);
    CHECK(counter.calls <= n);
    CHECK(is_sorted(array, n));
}

/// @brief NULL and invalid arguments: SOF_SORT_ERROR, with nothing compared or
/// changed. A NULL array with a count of 0 is empty and sorts.
static void test_null(void) {
    uint32_t a[3] = {3, 1, 2};
    for (size_t s = 0; s < NUM_SORTS; s++) {
        counter_t counter = {0, 0};
        CHECK(sorts[s].sort(NULL, 0, sizeof(uint32_t), compare_u32, &counter) == SOF_SORT_OK);
        CHECK(sorts[s].sort(NULL, 3, sizeof(uint32_t), compare_u32, &counter) == SOF_SORT_ERROR);
        CHECK(sorts[s].sort(a, 3, sizeof(uint32_t), NULL, &counter) == SOF_SORT_ERROR);
        CHECK(sorts[s].sort(a, 3, 0, compare_u32, &counter) == SOF_SORT_ERROR);
        CHECK(sorts[s].sort(NULL, 0, sizeof(uint32_t), NULL, &counter) == SOF_SORT_ERROR);
        CHECK(counter.calls == 0);
        CHECK(a[0] == 3 && a[1] == 1 && a[2] == 2);
    }
}

/// @brief Known answers on a small array with a run of equal elements.
static void test_search_known(void) {
    const uint32_t a[] = {1, 3, 3, 3, 5, 8};
    size_t n = sizeof(a) / sizeof(a[0]);
    static const struct {
        uint32_t key;
        size_t lower, upper;
    } cases[] = {
        {0, 0, 0}, {1, 0, 1}, {2, 1, 1}, {3, 1, 4}, {4, 4, 4},
        {5, 4, 5}, {6, 5, 5}, {8, 5, 6}, {9, 6, 6},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint32_t key = cases[i].key;
        CHECK(sof_sort_lower_bound(a, n, sizeof(uint32_t), &key, compare_u32, NULL) == cases[i].lower);
        CHECK(sof_sort_upper_bound(a, n, sizeof(uint32_t), &key, compare_u32, NULL) == cases[i].upper);
        uint32_t *found = sof_sort_search(a, n, sizeof(uint32_t), &key, compare_u32, NULL);
        if (cases[i].lower == cases[i].upper) {
            CHECK(found == NULL);
        } else {
            // The first of the equal elements.
            CHECK(found == a + cases[i].lower);
            CHECK(*found == key);
        }
    }
    // One element, and an empty array that isn't NULL.
    uint32_t key = 1;
    CHECK(sof_sort_search(a, 1, sizeof(uint32_t), &key, compare_u32, NULL) == a);
    CHECK(sof_sort_lower_bound(a, 0, sizeof(uint32_t), &key, compare_u32, NULL) == 0);
    CHECK(sof_sort_upper_bound(a, 0, sizeof(uint32_t), &key, compare_u32, NULL) == 0);
    CHECK(sof_sort_search(a, 0, sizeof(uint32_t), &key, compare_u32, NULL) == NULL);
}

/// @brief Random sorted arrays of every size up to 300, with many or few
/// duplicates, every key in range and either side of it, checked against a
/// linear scan; the compares are counted against the binary search bound.
static void test_search_random(void) {
    static const uint32_t ranges[] = {1, 2, 5, 50, 1000};
    uint64_t state = 0x0ddba11c0ffee123ULL;
    for (size_t n = 0; n <= 300; n++) {
        size_t limit = 0;
        while (((size_t)1 << limit) < n + 1) {
            limit++;
        }
        for (size_t r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++) {
            uint32_t range = ranges[r];
            for (size_t i = 0; i < n; i++) {
                // Odd values only, so even keys fall between elements.
                array[i] = 2 * (uint32_t)(next_random(&state) % range) + 1;
            }
            CHECK(sof_sort_quick(array, n, sizeof(uint32_t), compare_u32, NULL) == SOF_SORT_OK);
            for (uint32_t key = 0; key <= 2 * range + 1; key += range > 50 ? (uint32_t)(next_random(&state) % 40) + 1 : 1) {
                size_t lower = 0, upper = 0;
                while (lower < n && array[lower] < key) {
                    lower++;
                }
                while (upper < n && array[upper] <= key) {
                    upper++;
                }
                counter_t counter = {0, 0};
                CHECK(sof_sort_lower_bound(array, n, sizeof(uint32_t), &key, compare_u32, &counter) == lower);
                CHECK(counter.calls <= limit);
                counter.calls = 0;
                CHECK(sof_sort_upper_bound(array, n, sizeof(uint32_t), &key, compare_u32, &counter) == upper);
                CHECK(counter.calls <= limit);
                counter.calls = 0;
                uint32_t *found = sof_sort_search(array, n, sizeof(uint32_t), &key, compare_u32, &counter);
                CHECK(found == (lower < upper ? array + lower : NULL));
                CHECK(counter.calls <= limit + 1);
            }
        }
    }
}

/// @brief Searches use the same order as the sort: ctx reverses it, and
/// records of any size are found by their first byte.
static void test_search_order_and_sizes(void) {
    uint64_t state = 0xabcdef0123456789ULL;
    size_t n = 1000;
    for (size_t i = 0; i < n; i++) {
        array[i] = (uint32_t)(next_random(&state) % 100);
    }
    counter_t descending = {0, 1};
    CHECK(sof_sort_quick(array, n, sizeof(uint32_t), compare_u32, &descending) == SOF_SORT_OK);
    for (uint32_t key = 0; key <= 100; key++) {
        size_t lower = 0, upper = 0;
        while (lower < n && array[lower] > key) {
            lower++;
        }
        while (upper < n && array[upper] >= key) {
            upper++;
        }
        CHECK(sof_sort_lower_bound(array, n, sizeof(uint32_t), &key, compare_u32, &descending) == lower);
        CHECK(sof_sort_upper_bound(array, n, sizeof(uint32_t), &key, compare_u32, &descending) == upper);
    }

    static const size_t sizes[] = {1, 3, 8, 13, MAX_SIZE};
    for (size_t z = 0; z < sizeof(sizes) / sizeof(sizes[0]); z++) {
        size_t size = sizes[z];
        for (size_t r = 0; r < RECORDS; r++) {
            records[r * size] = (unsigned char)(next_random(&state) % 200);
        }
        CHECK(sof_sort_merge(records, RECORDS, size, compare_byte, NULL, NULL) == SOF_SORT_OK);
        for (unsigned key = 0; key < 256; key++) {
            unsigned char k = (unsigned char)key;
            size_t lower = 0;
            while (lower < RECORDS && records[lower * size] < k) {
                lower++;
            }
            unsigned char *found = sof_sort_search(records, RECORDS, size, &k, compare_byte, NULL);
            CHECK(sof_sort_lower_bound(records, RECORDS, size, &k, compare_byte, NULL) == lower);
            CHECK(found == (lower < RECORDS && records[lower * size] == k ? records + lower * size : NULL));
        }
    }
}

/// @brief Compares a uint64_t element with a uint64_t key, without ctx.
static int compare_u64(const void *a, const void *b, void *ctx) {
    uint64_t x, y;
    memcpy(&x, a, sizeof(x));
    memcpy(&y, b, sizeof(y));
    return (x > y) - (x < y);
}

/// @brief Weighted choice from running totals: the first total above a
/// number drawn below the last is chosen in proportion to its weight.
static void test_search_running_totals(void) {
    static const uint64_t weights[] = {5, 0, 1, 10, 0, 0, 4};
    size_t n = sizeof(weights) / sizeof(weights[0]);
    uint64_t totals[sizeof(weights) / sizeof(weights[0])], sum = 0;
    for (size_t i = 0; i < n; i++) {
        sum += weights[i];
        totals[i] = sum;
    }
    size_t chosen[sizeof(weights) / sizeof(weights[0])] = {0};
    for (uint64_t target = 0; target < sum; target++) {
        size_t i = sof_sort_upper_bound(totals, n, sizeof(uint64_t), &target, compare_u64, NULL);
        CHECK(i < n);
        chosen[i]++;
    }
    for (size_t i = 0; i < n; i++) {
        CHECK(chosen[i] == weights[i]);
    }
}

/// @brief NULL and invalid arguments: nothing found, 0 from the bounds, and
/// nothing compared. A NULL array with a count of 0 is empty.
static void test_search_null(void) {
    const uint32_t a[3] = {1, 2, 3};
    uint32_t key = 2;
    counter_t counter = {0, 0};
    CHECK(sof_sort_search(NULL, 0, sizeof(uint32_t), &key, compare_u32, &counter) == NULL);
    CHECK(sof_sort_lower_bound(NULL, 0, sizeof(uint32_t), &key, compare_u32, &counter) == 0);
    CHECK(sof_sort_upper_bound(NULL, 0, sizeof(uint32_t), &key, compare_u32, &counter) == 0);
    CHECK(sof_sort_search(NULL, 3, sizeof(uint32_t), &key, compare_u32, &counter) == NULL);
    CHECK(sof_sort_lower_bound(NULL, 3, sizeof(uint32_t), &key, compare_u32, &counter) == 0);
    CHECK(sof_sort_upper_bound(NULL, 3, sizeof(uint32_t), &key, compare_u32, &counter) == 0);
    CHECK(sof_sort_search(a, 3, sizeof(uint32_t), NULL, compare_u32, &counter) == NULL);
    CHECK(sof_sort_lower_bound(a, 3, sizeof(uint32_t), NULL, compare_u32, &counter) == 0);
    CHECK(sof_sort_upper_bound(a, 3, sizeof(uint32_t), NULL, compare_u32, &counter) == 0);
    CHECK(sof_sort_search(a, 3, sizeof(uint32_t), &key, NULL, &counter) == NULL);
    CHECK(sof_sort_lower_bound(a, 3, sizeof(uint32_t), &key, NULL, &counter) == 0);
    CHECK(sof_sort_upper_bound(a, 3, sizeof(uint32_t), &key, NULL, &counter) == 0);
    CHECK(sof_sort_search(a, 3, 0, &key, compare_u32, &counter) == NULL);
    CHECK(sof_sort_lower_bound(a, 3, 0, &key, compare_u32, &counter) == 0);
    CHECK(sof_sort_upper_bound(a, 3, 0, &key, compare_u32, &counter) == 0);
    CHECK(counter.calls == 0);
    // The same arguments, all valid, do search.
    CHECK(sof_sort_search(a, 3, sizeof(uint32_t), &key, compare_u32, &counter) == a + 1);
    CHECK(counter.calls > 0);
}

/// @brief sof_sort_merge() allocates one buffer of the array's size, frees it
/// with that size, and on failure leaves the array unchanged.
static void test_merge_alloc(void) {
    size_t n = 1000;
    uint64_t state = 0xdeadbeef01234567ULL;
    for (size_t i = 0; i < n; i++) {
        original[i] = (uint32_t)i;
    }
    shuffle(original, n, &state);
    // Fail each allocation in turn until the sort succeeds.
    for (size_t fail_at = 1;; fail_at++) {
        test_alloc_t tracker = {fail_at, 0, 0, 0};
        sof_alloc_t alloc = test_allocator(&tracker);
        memcpy(array, original, n * sizeof(uint32_t));
        sof_sort_error_t err = sof_sort_merge(array, n, sizeof(uint32_t), compare_u32, NULL, &alloc);
        CHECK(tracker.live == 0);
        CHECK(tracker.bad == 0);
        if (err == SOF_SORT_OK) {
            CHECK(tracker.allocations == fail_at - 1);
            for (size_t i = 0; i < n; i++) {
                CHECK(array[i] == i);
            }
            break;
        }
        CHECK(err == SOF_SORT_NO_MEM);
        CHECK(memcmp(array, original, n * sizeof(uint32_t)) == 0);
    }

    // Nothing to allocate for 0 or 1 elements.
    test_alloc_t counts = {0, 0, 0, 0};
    sof_alloc_t alloc = test_allocator(&counts);
    CHECK(sof_sort_merge(array, 0, sizeof(uint32_t), compare_u32, NULL, &alloc) == SOF_SORT_OK);
    CHECK(sof_sort_merge(array, 1, sizeof(uint32_t), compare_u32, NULL, &alloc) == SOF_SORT_OK);
    CHECK(counts.allocations == 0);

    // count * size overflows: no memory, and nothing allocated or touched.
    CHECK(sof_sort_merge(array, SIZE_MAX / 2 + 1, 2, compare_u32, NULL, &alloc) == SOF_SORT_NO_MEM);
    CHECK(counts.allocations == 0);

    // An allocator with a malloc but no way to free is rejected.
    sof_alloc_t broken = {test_alloc_malloc, NULL, NULL, NULL, &counts};
    CHECK(sof_sort_merge(array, n, sizeof(uint32_t), compare_u32, NULL, &broken) == SOF_SORT_ERROR);
    CHECK(counts.allocations == 0);
}

int main(void) {
    printf("sof_sort:\n");
    run("shuffled permutations of every size", test_permutations);
    run("many duplicates", test_duplicates);
    run("sorted, reversed and patterned input", test_patterns);
    run("large input, with compares counted", test_large);
    run("every input pattern, size and element size", test_inputs);
    run("stability with elements of 3 to 24 bytes", test_stable_sizes);
    run("McIlroy's adversary, with compares counted", test_adversary);
    run("an inconsistent compare function", test_inconsistent);
    run("quicksort on sorted, reversed and equal input", test_quick_linear);
    run("elements of 1 to 40 bytes", test_element_sizes);
    run("stability of bubble, insertion and merge sort", test_stability);
    run("ctx passed to the compare function", test_ctx);
    run("compares on sorted input", test_sorted_compares);
    run("NULL and invalid arguments", test_null);
    run("merge sort allocation", test_merge_alloc);
    run("searching: known answers", test_search_known);
    run("searching: random arrays against a linear scan", test_search_random);
    run("searching: reversed order and record sizes", test_search_order_and_sizes);
    run("searching: running totals", test_search_running_totals);
    run("searching: NULL and invalid arguments", test_search_null);
    return test_report();
}
