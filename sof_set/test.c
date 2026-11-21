/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_set.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_set.h"
#include "test.h"

/// @brief The largest universe of keys the randomized tests draw from.
#define UNIVERSE 400

/// @brief The set operations, for the randomized tests.
enum { OP_UNION, OP_INTERSECTION, OP_DIFFERENCE, OP_SYMMETRIC, OP_COUNT };

/// @brief Counts key copies and frees, to show the set frees every key it
/// copies.
typedef struct counts_t {
    size_t copies;
    size_t frees;
} counts_t;

static void *counting_copy(const void *key, const sof_alloc_t *alloc, void *ctx) {
    ((counts_t *)ctx)->copies++;
    return sof_hashmap_string_keys.copy(key, alloc, NULL);
}

static void counting_free(void *key, const sof_alloc_t *alloc, void *ctx) {
    ((counts_t *)ctx)->frees++;
    sof_hashmap_string_keys.free(key, alloc, NULL);
}

static const sof_hashmap_key_callbacks_t counting_string_keys = {
    sof_hashmap_string_hash,
    sof_hashmap_string_equal,
    counting_copy,
    counting_free,
};

/// @brief A hash that gives only a few values, so keys pile up in long runs
/// that wrap around the end of the map's array.
static size_t weak_hash(const void *key, void *ctx) {
    return (size_t)strtoul((const char *)key + 3, NULL, 10) % 3;
}

static const sof_hashmap_key_callbacks_t weak_string_keys = {
    weak_hash,
    sof_hashmap_string_equal,
    counting_copy,
    counting_free,
};

/// @brief Hash and equal for strings, without copy or free: the set stores
/// the caller's pointers.
static const sof_hashmap_key_callbacks_t borrowed_string_keys = {
    sof_hashmap_string_hash,
    sof_hashmap_string_equal,
    NULL,
    NULL,
};

/// @brief Writes "key<n>" to buf, which must hold 32 bytes.
static const char *key_name(char *buf, size_t n) {
    snprintf(buf, 32, "key%zu", n);
    return buf;
}

/// @brief Adds key<n> to set for every n below count where member[n] is set.
static int add_members(sof_set_t *set, const char *member, size_t count) {
    char buf[32];
    for (size_t n = 0; n < count; n++) {
        if (member[n] && sof_set_add(set, key_name(buf, n), NULL) == NULL) {
            return -1;
        }
    }
    return 0;
}

/// @brief Adds key<first> to key<last - 1> to set.
static int add_range(sof_set_t *set, size_t first, size_t last) {
    char buf[32];
    for (size_t n = first; n < last; n++) {
        if (sof_set_add(set, key_name(buf, n), NULL) == NULL) {
            return -1;
        }
    }
    return 0;
}

/// @brief Returns 1 if set holds exactly the keys key<n> where member[n] is
/// set, for n below count.
static int matches(const sof_set_t *set, const char *member, size_t count) {
    char buf[32];
    size_t expected = 0;
    for (size_t n = 0; n < count; n++) {
        int found = sof_set_find(set, key_name(buf, n)) != NULL;
        if (found != (member[n] != 0)) {
            return 0;
        }
        expected += found;
    }
    return sof_set_count(set) == expected;
}

static int count_key(void *key, void *ctx) {
    (*(size_t *)ctx)++;
    return 0;
}

static int stop_at_third(void *key, void *ctx) {
    return ++(*(size_t *)ctx) == 3 ? 7 : 0;
}

static void test_add_find_remove(void) {
    sof_set_t set;
    counts_t counts = {0};
    char buf[32];
    CHECK(sof_set_init(&set, &counting_string_keys, &counts, 0, NULL) == SOF_SET_OK);
    CHECK(sof_set_count(&set) == 0);
    int created = 0;
    char *key = sof_set_add(&set, key_name(buf, 1), &created);
    CHECK(key != NULL && key != buf && strcmp(key, "key1") == 0);
    CHECK(created == 1);
    // Adding it again finds the set's own key and copies nothing.
    CHECK(sof_set_add(&set, "key1", &created) == key);
    CHECK(created == 0);
    CHECK(counts.copies == 1);
    CHECK(sof_set_find(&set, "key1") == key);
    CHECK(sof_set_find(&set, "key2") == NULL);
    CHECK(add_range(&set, 0, 1000) == 0);
    CHECK(sof_set_count(&set) == 1000);
    CHECK(set.map.count == 1000);
    // The key pointer doesn't move as the set grows.
    CHECK(sof_set_find(&set, "key1") == key);
    CHECK(sof_set_remove(&set, "key1") == SOF_SET_OK);
    CHECK(sof_set_remove(&set, "key1") == SOF_SET_NO_MATCH);
    CHECK(sof_set_find(&set, "key1") == NULL);
    CHECK(sof_set_count(&set) == 999);
    // The set's own key can be removed with itself.
    char *own = sof_set_find(&set, "key2");
    CHECK(sof_set_remove(&set, own) == SOF_SET_OK);
    CHECK(counts.frees == 2);
    CHECK(sof_set_free(&set) == SOF_SET_OK);
    CHECK(counts.copies == 1000);
    CHECK(counts.frees == 1000);
}

static void test_clear(void) {
    sof_set_t set;
    counts_t counts = {0};
    CHECK(sof_set_init(&set, &counting_string_keys, &counts, 0, NULL) == SOF_SET_OK);
    CHECK(add_range(&set, 0, 100) == 0);
    CHECK(sof_set_clear(&set) == SOF_SET_OK);
    CHECK(sof_set_count(&set) == 0);
    CHECK(counts.frees == 100);
    CHECK(sof_set_find(&set, "key5") == NULL);
    // The set still works after clearing.
    CHECK(add_range(&set, 0, 10) == 0);
    CHECK(sof_set_count(&set) == 10);
    CHECK(sof_set_free(&set) == SOF_SET_OK);
    CHECK(counts.frees == counts.copies);
}

static void test_identity_keys(void) {
    sof_set_t set;
    int a = 0, b = 0;
    CHECK(sof_set_init(&set, NULL, NULL, 0, NULL) == SOF_SET_OK);
    CHECK(sof_set_add(&set, &a, NULL) == &a);
    CHECK(sof_set_find(&set, &a) == &a);
    CHECK(sof_set_find(&set, &b) == NULL);
    CHECK(sof_set_free(&set) == SOF_SET_OK);
}

static void test_next_traverse(void) {
    sof_set_t set;
    static char seen[1000];
    char buf[32];
    CHECK(sof_set_init(&set, &sof_hashmap_string_keys, NULL, 0, NULL) == SOF_SET_OK);
    size_t position = 0;
    CHECK(sof_set_next(&set, &position) == NULL);
    CHECK(add_range(&set, 0, 1000) == 0);
    // Every key comes back exactly once.
    position = 0;
    size_t count = 0;
    char *key;
    while ((key = sof_set_next(&set, &position)) != NULL) {
        size_t n = strtoul(key + 3, NULL, 10);
        CHECK(n < 1000 && !seen[n]);
        CHECK(sof_set_find(&set, key_name(buf, n)) == key);
        seen[n] = 1;
        count++;
    }
    CHECK(count == 1000);
    CHECK(sof_set_next(&set, &position) == NULL);
    CHECK(sof_set_next(&set, NULL) == NULL);
    count = 0;
    CHECK(sof_set_traverse(&set, count_key, &count) == 0);
    CHECK(count == 1000);
    // A non-zero return stops the traversal and is passed back.
    count = 0;
    CHECK(sof_set_traverse(&set, stop_at_third, &count) == 7);
    CHECK(count == 3);
    CHECK(sof_set_traverse(&set, NULL, NULL) == 0);
    CHECK(sof_set_free(&set) == SOF_SET_OK);
}

static void test_operations(void) {
    sof_set_t a, b, c;
    counts_t counts = {0};
    char member[20];
    CHECK(sof_set_init(&a, &counting_string_keys, &counts, 0, NULL) == SOF_SET_OK);
    CHECK(sof_set_init(&b, &counting_string_keys, &counts, 0, NULL) == SOF_SET_OK);
    CHECK(sof_set_init(&c, &counting_string_keys, &counts, 0, NULL) == SOF_SET_OK);
    CHECK(add_range(&a, 0, 10) == 0);
    CHECK(add_range(&b, 5, 15) == 0);
    // c = a | b: 0..14.
    CHECK(sof_set_union(&c, &a) == SOF_SET_OK);
    CHECK(sof_set_equal(&c, &a) == 1);
    CHECK(sof_set_union(&c, &b) == SOF_SET_OK);
    for (size_t n = 0; n < 20; n++) {
        member[n] = n < 15;
    }
    CHECK(matches(&c, member, 20));
    // c = c & a: 0..9, with c the larger set.
    CHECK(sof_set_intersection(&c, &a) == SOF_SET_OK);
    CHECK(sof_set_equal(&c, &a) == 1);
    // c = c & b: 5..9.
    CHECK(sof_set_intersection(&c, &b) == SOF_SET_OK);
    for (size_t n = 0; n < 20; n++) {
        member[n] = n >= 5 && n < 10;
    }
    CHECK(matches(&c, member, 20));
    // c = a - b: 0..4, with a the larger set.
    CHECK(sof_set_clear(&c) == SOF_SET_OK);
    CHECK(sof_set_union(&c, &a) == SOF_SET_OK);
    CHECK(sof_set_difference(&c, &b) == SOF_SET_OK);
    for (size_t n = 0; n < 20; n++) {
        member[n] = n < 5;
    }
    CHECK(matches(&c, member, 20));
    // c = c - b again, with b the larger set: nothing changes.
    CHECK(sof_set_difference(&c, &b) == SOF_SET_OK);
    CHECK(matches(&c, member, 20));
    // c = a ^ b: 0..4 and 10..14.
    CHECK(sof_set_clear(&c) == SOF_SET_OK);
    CHECK(sof_set_union(&c, &a) == SOF_SET_OK);
    CHECK(sof_set_symmetric_difference(&c, &b) == SOF_SET_OK);
    for (size_t n = 0; n < 20; n++) {
        member[n] = n < 5 || (n >= 10 && n < 15);
    }
    CHECK(matches(&c, member, 20));
    // The sources are unchanged.
    for (size_t n = 0; n < 20; n++) {
        member[n] = n < 10;
    }
    CHECK(matches(&a, member, 20));
    for (size_t n = 0; n < 20; n++) {
        member[n] = n >= 5 && n < 15;
    }
    CHECK(matches(&b, member, 20));
    CHECK(sof_set_free(&a) == SOF_SET_OK);
    CHECK(sof_set_free(&b) == SOF_SET_OK);
    CHECK(sof_set_free(&c) == SOF_SET_OK);
    CHECK(counts.copies == counts.frees);
}

static void test_same_set(void) {
    sof_set_t a;
    counts_t counts = {0};
    CHECK(sof_set_init(&a, &counting_string_keys, &counts, 0, NULL) == SOF_SET_OK);
    CHECK(add_range(&a, 0, 50) == 0);
    CHECK(sof_set_union(&a, &a) == SOF_SET_OK);
    CHECK(sof_set_count(&a) == 50);
    CHECK(sof_set_intersection(&a, &a) == SOF_SET_OK);
    CHECK(sof_set_count(&a) == 50);
    CHECK(sof_set_equal(&a, &a) == 1);
    CHECK(sof_set_is_subset(&a, &a) == 1);
    CHECK(sof_set_is_disjoint(&a, &a) == 0);
    CHECK(sof_set_difference(&a, &a) == SOF_SET_OK);
    CHECK(sof_set_count(&a) == 0);
    CHECK(sof_set_is_disjoint(&a, &a) == 1);
    CHECK(add_range(&a, 0, 50) == 0);
    CHECK(sof_set_symmetric_difference(&a, &a) == SOF_SET_OK);
    CHECK(sof_set_count(&a) == 0);
    CHECK(sof_set_free(&a) == SOF_SET_OK);
    CHECK(counts.copies == 100);
    CHECK(counts.frees == 100);
}

static void test_predicates(void) {
    sof_set_t a, b, empty;
    CHECK(sof_set_init(&a, &sof_hashmap_string_keys, NULL, 0, NULL) == SOF_SET_OK);
    CHECK(sof_set_init(&b, &sof_hashmap_string_keys, NULL, 0, NULL) == SOF_SET_OK);
    CHECK(sof_set_init(&empty, &sof_hashmap_string_keys, NULL, 0, NULL) == SOF_SET_OK);
    CHECK(add_range(&a, 0, 10) == 0);
    CHECK(add_range(&b, 0, 20) == 0);
    CHECK(sof_set_is_subset(&a, &b) == 1);
    CHECK(sof_set_is_subset(&b, &a) == 0);
    CHECK(sof_set_is_subset(&empty, &a) == 1);
    CHECK(sof_set_is_subset(&a, &empty) == 0);
    CHECK(sof_set_equal(&a, &b) == 0);
    CHECK(sof_set_equal(&empty, &empty) == 1);
    CHECK(sof_set_is_disjoint(&a, &b) == 0);
    CHECK(sof_set_is_disjoint(&a, &empty) == 1);
    CHECK(sof_set_is_disjoint(&empty, &a) == 1);
    // Same size, different keys.
    CHECK(sof_set_clear(&b) == SOF_SET_OK);
    CHECK(add_range(&b, 5, 15) == 0);
    CHECK(sof_set_equal(&a, &b) == 0);
    CHECK(sof_set_is_subset(&a, &b) == 0);
    CHECK(sof_set_is_disjoint(&a, &b) == 0);
    CHECK(sof_set_clear(&b) == SOF_SET_OK);
    CHECK(add_range(&b, 10, 30) == 0);
    CHECK(sof_set_is_disjoint(&a, &b) == 1);
    CHECK(sof_set_is_disjoint(&b, &a) == 1);
    // Equal sets built in a different order.
    CHECK(sof_set_clear(&b) == SOF_SET_OK);
    for (size_t n = 10; n-- > 0;) {
        char buf[32];
        CHECK(sof_set_add(&b, key_name(buf, n), NULL) != NULL);
    }
    CHECK(sof_set_equal(&a, &b) == 1);
    CHECK(sof_set_equal(&b, &a) == 1);
    CHECK(sof_set_free(&a) == SOF_SET_OK);
    CHECK(sof_set_free(&b) == SOF_SET_OK);
    CHECK(sof_set_free(&empty) == SOF_SET_OK);
}

static void test_borrowed_keys(void) {
    // A set without a copy callback stores the other set's pointers.
    sof_set_t owner, view;
    CHECK(sof_set_init(&owner, &sof_hashmap_string_keys, NULL, 0, NULL) == SOF_SET_OK);
    CHECK(sof_set_init(&view, &borrowed_string_keys, NULL, 0, NULL) == SOF_SET_OK);
    CHECK(add_range(&owner, 0, 100) == 0);
    CHECK(sof_set_union(&view, &owner) == SOF_SET_OK);
    CHECK(sof_set_equal(&view, &owner) == 1);
    CHECK(sof_set_find(&view, "key42") == sof_set_find(&owner, "key42"));
    CHECK(sof_set_free(&view) == SOF_SET_OK);
    CHECK(sof_set_free(&owner) == SOF_SET_OK);
}

/// @brief Applies op to a and b, and the same to the reference arrays.
static sof_set_error_t apply(int op, sof_set_t *a, const sof_set_t *b, char *ma, const char *mb, size_t count) {
    for (size_t n = 0; n < count; n++) {
        switch (op) {
        case OP_UNION:
            ma[n] = ma[n] || mb[n];
            break;
        case OP_INTERSECTION:
            ma[n] = ma[n] && mb[n];
            break;
        case OP_DIFFERENCE:
            ma[n] = ma[n] && !mb[n];
            break;
        default:
            ma[n] = (ma[n] != 0) != (mb[n] != 0);
            break;
        }
    }
    switch (op) {
    case OP_UNION:
        return sof_set_union(a, b);
    case OP_INTERSECTION:
        return sof_set_intersection(a, b);
    case OP_DIFFERENCE:
        return sof_set_difference(a, b);
    default:
        return sof_set_symmetric_difference(a, b);
    }
}

/// @brief Compares every operation and comparison with a reference, on
/// random sets with the given key callbacks.
/// @brief Runs random set operations, checked against arrays. With shared,
/// both sets get the same ctx, so they use each other's stored hashes;
/// without, b's differs, and every key is hashed again.
static void random_operations(const sof_hashmap_key_callbacks_t *keys, size_t trials, int shared) {
    uint64_t state = 0x5e7c0ffee;
    counts_t counts = {0}, other = {0};
    counts_t *b_counts = shared ? &counts : &other;
    static char ma[UNIVERSE], mb[UNIVERSE];
    for (size_t trial = 0; trial < trials; trial++) {
        size_t count = 1 + next_random(&state) % UNIVERSE;
        unsigned density_a = next_random(&state) % 101, density_b = next_random(&state) % 101;
        int shape = next_random(&state) % 4;
        for (size_t n = 0; n < count; n++) {
            ma[n] = next_random(&state) % 100 < density_a;
            mb[n] = next_random(&state) % 100 < density_b;
            // Make some pairs subsets of, or equal to, each other.
            if (shape == 1) {
                mb[n] |= ma[n];
            } else if (shape == 2) {
                mb[n] = ma[n];
            }
        }
        int subset = 1, disjoint = 1, equal = 1;
        for (size_t n = 0; n < count; n++) {
            subset &= !ma[n] || mb[n];
            disjoint &= !(ma[n] && mb[n]);
            equal &= !ma[n] == !mb[n];
        }
        for (int op = 0; op < OP_COUNT; op++) {
            sof_set_t a, b;
            char ra[UNIVERSE];
            memcpy(ra, ma, count);
            CHECK(sof_set_init(&a, keys, &counts, 0, NULL) == SOF_SET_OK);
            CHECK(sof_set_init(&b, keys, b_counts, 0, NULL) == SOF_SET_OK);
            CHECK(add_members(&a, ma, count) == 0);
            CHECK(add_members(&b, mb, count) == 0);
            CHECK(sof_set_is_subset(&a, &b) == subset);
            CHECK(sof_set_is_disjoint(&a, &b) == disjoint);
            CHECK(sof_set_is_disjoint(&b, &a) == disjoint);
            CHECK(sof_set_equal(&a, &b) == equal);
            CHECK(apply(op, &a, &b, ra, mb, count) == SOF_SET_OK);
            CHECK(matches(&a, ra, count));
            CHECK(matches(&b, mb, count));
            CHECK(sof_set_free(&a) == SOF_SET_OK);
            CHECK(sof_set_free(&b) == SOF_SET_OK);
        }
    }
    CHECK(counts.copies == counts.frees);
    CHECK(other.copies == other.frees);
}

static void test_random(void) {
    random_operations(&counting_string_keys, 300, 1);
    random_operations(&counting_string_keys, 300, 0);
}

static void test_random_collisions(void) {
    random_operations(&weak_string_keys, 40, 1);
    random_operations(&weak_string_keys, 40, 0);
}

/// @brief A string hash that counts its calls in the size_t at ctx.
static size_t counting_hash(const void *key, void *ctx) {
    (*(size_t *)ctx)++;
    return sof_hashmap_string_hash(key, NULL);
}

/// @brief Runs every operation and comparison on key0..key199 and
/// key100..key299, with a's ctx at a_hashes and b's at b_hashes, checking
/// the results. Adds to *calls how many times the operations hashed keys.
static void hashes_in_operations(size_t *a_hashes, size_t *b_hashes, size_t *calls) {
    static const sof_hashmap_key_callbacks_t keys = {counting_hash, sof_hashmap_string_equal, NULL, NULL};
    static char names[300][32];
    for (size_t n = 0; n < 300; n++) {
        key_name(names[n], n);
    }
    for (int op = 0; op < OP_COUNT + 2; op++) {
        sof_set_t a, b;
        CHECK(sof_set_init(&a, &keys, a_hashes, 0, NULL) == SOF_SET_OK);
        CHECK(sof_set_init(&b, &keys, b_hashes, 0, NULL) == SOF_SET_OK);
        for (size_t n = 0; n < 200; n++) {
            CHECK(sof_set_add(&a, names[n], NULL) != NULL);
            CHECK(sof_set_add(&b, names[n + 100], NULL) != NULL);
        }
        size_t before = *a_hashes + (a_hashes != b_hashes ? *b_hashes : 0);
        size_t first = 0, last = 0;
        switch (op) {
        case OP_UNION:
            CHECK(sof_set_union(&a, &b) == SOF_SET_OK);
            first = 0, last = 300;
            break;
        case OP_INTERSECTION:
            CHECK(sof_set_intersection(&a, &b) == SOF_SET_OK);
            first = 100, last = 200;
            break;
        case OP_DIFFERENCE:
            CHECK(sof_set_difference(&a, &b) == SOF_SET_OK);
            first = 0, last = 100;
            break;
        case OP_SYMMETRIC:
            CHECK(sof_set_symmetric_difference(&a, &b) == SOF_SET_OK);
            break;
        case OP_COUNT:
            // The difference that walks src, the smaller set, left with
            // key100..key149.
            for (size_t n = 150; n < 300; n++) {
                CHECK(sof_set_remove(&b, names[n]) == SOF_SET_OK);
            }
            before = *a_hashes + (a_hashes != b_hashes ? *b_hashes : 0);
            CHECK(sof_set_difference(&a, &b) == SOF_SET_OK);
            CHECK(sof_set_count(&a) == 150);
            break;
        default:
            CHECK(sof_set_is_subset(&a, &b) == 0);
            CHECK(sof_set_is_disjoint(&a, &b) == 0);
            CHECK(sof_set_equal(&a, &b) == 0);
            break;
        }
        *calls += *a_hashes + (a_hashes != b_hashes ? *b_hashes : 0) - before;
        for (size_t n = first; n < last; n++) {
            CHECK(sof_set_find(&a, names[n]) == names[n]);
        }
        if (op == OP_SYMMETRIC) {
            CHECK(sof_set_count(&a) == 200);
            CHECK(sof_set_find(&a, names[150]) == NULL && sof_set_find(&a, names[250]) == names[250]);
        } else if (op < OP_COUNT) {
            CHECK(sof_set_count(&a) == last - first);
        }
        CHECK(sof_set_free(&a) == SOF_SET_OK);
        CHECK(sof_set_free(&b) == SOF_SET_OK);
    }
}

static void test_stored_hashes(void) {
    // Sets with the same hash callback and ctx use each other's stored
    // hashes, but for the finds checking the results, which are left out.
    size_t a_hashes = 0, b_hashes = 0, calls = 0;
    hashes_in_operations(&a_hashes, &a_hashes, &calls);
    CHECK(calls == 0);
    // A different ctx may mean a different hash, so keys are hashed again.
    hashes_in_operations(&a_hashes, &b_hashes, &calls);
    CHECK(calls > 0);
}

static void test_null_and_freed(void) {
    sof_set_t set, other;
    size_t position = 0, count = 0;
    int created = 1;
    CHECK(sof_set_init(NULL, NULL, NULL, 0, NULL) == SOF_SET_ERROR);
    CHECK(sof_set_add(NULL, "key", &created) == NULL);
    CHECK(created == 0);
    CHECK(sof_set_clear(NULL) == SOF_SET_ERROR);
    CHECK(sof_set_count(NULL) == 0);
    CHECK(sof_set_find(NULL, "key") == NULL);
    CHECK(sof_set_free(NULL) == SOF_SET_ERROR);
    CHECK(sof_set_next(NULL, &position) == NULL);
    CHECK(sof_set_remove(NULL, "key") == SOF_SET_ERROR);
    CHECK(sof_set_traverse(NULL, count_key, &count) == -1);
    CHECK(sof_set_init(&other, &sof_hashmap_string_keys, NULL, 0, NULL) == SOF_SET_OK);
    CHECK(sof_set_union(NULL, &other) == SOF_SET_ERROR);
    CHECK(sof_set_union(&other, NULL) == SOF_SET_ERROR);
    CHECK(sof_set_intersection(NULL, &other) == SOF_SET_ERROR);
    CHECK(sof_set_intersection(&other, NULL) == SOF_SET_ERROR);
    CHECK(sof_set_difference(NULL, &other) == SOF_SET_ERROR);
    CHECK(sof_set_difference(&other, NULL) == SOF_SET_ERROR);
    CHECK(sof_set_symmetric_difference(NULL, &other) == SOF_SET_ERROR);
    CHECK(sof_set_symmetric_difference(&other, NULL) == SOF_SET_ERROR);
    CHECK(sof_set_equal(NULL, &other) == -1);
    CHECK(sof_set_equal(&other, NULL) == -1);
    CHECK(sof_set_is_subset(NULL, &other) == -1);
    CHECK(sof_set_is_subset(&other, NULL) == -1);
    CHECK(sof_set_is_disjoint(NULL, &other) == -1);
    CHECK(sof_set_is_disjoint(&other, NULL) == -1);
    // NULL keys.
    CHECK(sof_set_add(&other, NULL, &created) == NULL);
    CHECK(created == 0);
    CHECK(sof_set_find(&other, NULL) == NULL);
    CHECK(sof_set_remove(&other, NULL) == SOF_SET_ERROR);
    // A freed set is empty, and can't be added to.
    CHECK(sof_set_init(&set, &sof_hashmap_string_keys, NULL, 0, NULL) == SOF_SET_OK);
    CHECK(add_range(&set, 0, 10) == 0);
    CHECK(add_range(&other, 0, 10) == 0);
    CHECK(sof_set_free(&set) == SOF_SET_OK);
    CHECK(sof_set_free(&set) == SOF_SET_OK);
    CHECK(sof_set_count(&set) == 0);
    CHECK(sof_set_find(&set, "key1") == NULL);
    CHECK(sof_set_add(&set, "key1", &created) == NULL);
    CHECK(created == 0);
    CHECK(sof_set_remove(&set, "key1") == SOF_SET_NO_MATCH);
    CHECK(sof_set_clear(&set) == SOF_SET_OK);
    position = 0;
    CHECK(sof_set_next(&set, &position) == NULL);
    CHECK(sof_set_traverse(&set, count_key, &count) == 0);
    CHECK(count == 0);
    CHECK(sof_set_union(&set, &other) == SOF_SET_ERROR);
    CHECK(sof_set_intersection(&set, &other) == SOF_SET_ERROR);
    CHECK(sof_set_difference(&set, &other) == SOF_SET_ERROR);
    CHECK(sof_set_symmetric_difference(&set, &other) == SOF_SET_ERROR);
    CHECK(sof_set_is_subset(&set, &other) == 1);
    CHECK(sof_set_is_subset(&other, &set) == 0);
    CHECK(sof_set_is_disjoint(&set, &other) == 1);
    CHECK(sof_set_equal(&set, &other) == 0);
    // As a source, a freed set is an empty one.
    CHECK(sof_set_union(&other, &set) == SOF_SET_OK);
    CHECK(sof_set_count(&other) == 10);
    CHECK(sof_set_difference(&other, &set) == SOF_SET_OK);
    CHECK(sof_set_count(&other) == 10);
    CHECK(sof_set_symmetric_difference(&other, &set) == SOF_SET_OK);
    CHECK(sof_set_count(&other) == 10);
    CHECK(sof_set_intersection(&other, &set) == SOF_SET_OK);
    CHECK(sof_set_count(&other) == 0);
    CHECK(sof_set_equal(&other, &set) == 1);
    CHECK(sof_set_free(&other) == SOF_SET_OK);
}

static void test_bad_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    sof_set_t set;
    CHECK(sof_set_init(&set, &sof_hashmap_string_keys, NULL, 0, &alloc) == SOF_SET_ERROR);
    CHECK(set.map.elements == NULL);
    CHECK(sof_set_add(&set, "key", NULL) == NULL);
    CHECK(state.allocations == 0);
}

static void test_expected_keys(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_set_t set;
    // Room for 1000 keys means one allocation for the array, and one per key
    // copy, with no growing.
    CHECK(sof_set_init(&set, &sof_hashmap_string_keys, NULL, 1000, &alloc) == SOF_SET_OK);
    CHECK(add_range(&set, 0, 1000) == 0);
    CHECK(state.allocations == 1001);
    CHECK(sof_set_free(&set) == SOF_SET_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

/// @brief Fails each allocation in turn, through init, adds that copy keys
/// and grow the set, a union into an empty set, and a symmetric difference,
/// until a run makes none fail. With shared, the sets have the same ctx, so
/// the union and the symmetric difference use the stored hashes; without, b
/// and c have another, so they hash the keys again.
static void no_memory(int shared) {
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        void *other = shared ? NULL : &state;
        sof_set_t a, b, c;
        sof_set_error_t ra = sof_set_init(&a, &sof_hashmap_string_keys, NULL, 0, &alloc);
        sof_set_error_t rb = sof_set_init(&b, &sof_hashmap_string_keys, other, 0, &alloc);
        sof_set_error_t rc = sof_set_init(&c, &sof_hashmap_string_keys, other, 0, &alloc);
        CHECK(ra == SOF_SET_OK || ra == SOF_SET_NO_MEM);
        CHECK(rb == SOF_SET_OK || rb == SOF_SET_NO_MEM);
        CHECK(rc == SOF_SET_OK || rc == SOF_SET_NO_MEM);
        add_range(&a, 0, 100);
        add_range(&b, 50, 150);
        sof_set_error_t ru = sof_set_union(&c, &a);
        CHECK(ru == SOF_SET_OK || (rc == SOF_SET_OK && ru == SOF_SET_NO_MEM) || (rc != SOF_SET_OK && ru == SOF_SET_ERROR));
        // Whatever the union managed to add came from a.
        CHECK(sof_set_is_subset(&c, &a) == 1);
        sof_set_error_t rs = sof_set_symmetric_difference(&a, &b);
        CHECK(rs == SOF_SET_OK || rs == SOF_SET_NO_MEM || (ra != SOF_SET_OK && rs == SOF_SET_ERROR));
        CHECK(sof_set_free(&a) == SOF_SET_OK);
        CHECK(sof_set_free(&b) == SOF_SET_OK);
        CHECK(sof_set_free(&c) == SOF_SET_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            CHECK(ru == SOF_SET_OK && rs == SOF_SET_OK);
            break;
        }
    }
}

static void test_no_memory(void) {
    no_memory(1);
    no_memory(0);
}

int main(void) {
    printf("sof_set:\n");
    run("add, find and remove", test_add_find_remove);
    run("clear", test_clear);
    run("identity keys", test_identity_keys);
    run("next and traverse", test_next_traverse);
    run("union, intersection, difference, symmetric difference", test_operations);
    run("operations on a set and itself", test_same_set);
    run("subset, equal and disjoint", test_predicates);
    run("borrowed keys", test_borrowed_keys);
    run("random sets against a reference", test_random);
    run("random sets with colliding hashes", test_random_collisions);
    run("stored hashes, with the same ctx and not", test_stored_hashes);
    run("NULL and freed sets", test_null_and_freed);
    run("bad allocator", test_bad_allocator);
    run("expected keys", test_expected_keys);
    run("failing allocations", test_no_memory);
    return test_report();
}
