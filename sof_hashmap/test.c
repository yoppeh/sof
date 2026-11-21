/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_hashmap.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_hashmap.h"
#include "test.h"

/// @brief Threads, and the work each does, in the threaded tests.
#define READERS 4
#define PASSES 500
#define ITEMS 10000
#define WORKERS 4
#define WORKER_KEYS 2000
#define WORKER_OPS 50000

/// @brief Counts key copies, key frees and calls to the del_func, to show
/// the map frees what it copies and hands every removed element over.
typedef struct counts_t {
    size_t copies;
    size_t frees;
    size_t deletes;
    /// @brief Elements the del_func got whose key was already freed.
    size_t bad_deletes;
} counts_t;

static void *counting_copy(const void *key, const sof_alloc_t *alloc, void *ctx) {
    ((counts_t *)ctx)->copies++;
    return sof_hashmap_string_keys.copy(key, alloc, NULL);
}

static void counting_free(void *key, const sof_alloc_t *alloc, void *ctx) {
    ((counts_t *)ctx)->frees++;
    sof_hashmap_string_keys.free(key, alloc, NULL);
}

static void counting_delete(sof_hashmap_element_t *element, void *ctx) {
    counts_t *counts = ctx;
    counts->deletes++;
    // The key is freed after this, so it must still read correctly here.
    if (strncmp(element->key, "key", 3) != 0) {
        counts->bad_deletes++;
    }
}

static const sof_hashmap_key_callbacks_t counting_string_keys = {
    sof_hashmap_string_hash,
    sof_hashmap_string_equal,
    counting_copy,
    counting_free,
};

/// @brief Initializes map with counting string keys and del_func.
static sof_hashmap_error_t init_counting(sof_hashmap_t *map, counts_t *counts) {
    memset(counts, 0, sizeof(counts_t));
    return sof_hashmap_init(map, &counting_string_keys, counting_delete, counts, 0, NULL);
}

/// @brief Writes "key<n>" to buf, which must hold 32 bytes.
static const char *key_name(char *buf, size_t n) {
    snprintf(buf, 32, "key%zu", n);
    return buf;
}

static int count_element(sof_hashmap_element_t *element, void *ctx) {
    (*(size_t *)ctx)++;
    return 0;
}

static int stop_at_third(sof_hashmap_element_t *element, void *ctx) {
    return ++(*(size_t *)ctx) == 3 ? 7 : 0;
}

/// @brief Inserts key0..key<count - 1>, each with its number as the data.
static int insert_keys(sof_hashmap_t *map, size_t count) {
    char buf[32];
    for (size_t i = 0; i < count; i++) {
        int created = 0;
        sof_hashmap_element_t *e = sof_hashmap_insert(map, key_name(buf, i), &created);
        if (e == NULL || !created || e->key == buf || strcmp(e->key, buf) != 0) {
            return -1;
        }
        e->data = (void *)(uintptr_t)i;
    }
    return 0;
}

/// @brief Returns the element for key<n>, or NULL.
static sof_hashmap_element_t *find_key(sof_hashmap_t *map, size_t n) {
    char buf[32];
    return sof_hashmap_find(map, key_name(buf, n));
}

static void test_insert_find(void) {
    sof_hashmap_t map;
    counts_t counts;
    const size_t n = 100000;
    CHECK(init_counting(&map, &counts) == SOF_HASHMAP_OK);
    CHECK(insert_keys(&map, n) == 0);
    CHECK(map.count == n);
    CHECK(counts.copies == n);
    for (size_t i = 0; i < n; i++) {
        sof_hashmap_element_t *e = find_key(&map, i);
        CHECK(e != NULL);
        CHECK((uintptr_t)e->data == i);
    }
    CHECK(find_key(&map, n) == NULL);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
    CHECK(counts.frees == n);
    CHECK(counts.deletes == n);
    CHECK(counts.bad_deletes == 0);
}

static void test_insert_existing(void) {
    sof_hashmap_t map;
    counts_t counts;
    char buf[32];
    CHECK(init_counting(&map, &counts) == SOF_HASHMAP_OK);
    CHECK(insert_keys(&map, 10) == 0);
    int created = 1;
    sof_hashmap_element_t *e = sof_hashmap_insert(&map, key_name(buf, 3), &created);
    CHECK(e != NULL);
    CHECK(created == 0);
    CHECK((uintptr_t)e->data == 3);
    CHECK(map.count == 10);
    CHECK(counts.copies == 10);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
    CHECK(counts.frees == 10);
}

static void test_traverse(void) {
    sof_hashmap_t map;
    size_t seen = 0;
    CHECK(sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, NULL) == SOF_HASHMAP_OK);
    CHECK(insert_keys(&map, 1000) == 0);
    CHECK(sof_hashmap_traverse(&map, count_element, &seen) == 0);
    CHECK(seen == 1000);
    // A non-zero return stops the traversal and is passed back.
    seen = 0;
    CHECK(sof_hashmap_traverse(&map, stop_at_third, &seen) == 7);
    CHECK(seen == 3);
    CHECK(sof_hashmap_traverse(&map, NULL, NULL) == 0);
    CHECK(sof_hashmap_traverse(NULL, count_element, &seen) == -1);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
}

static void test_next(void) {
    sof_hashmap_t map;
    static char seen[1000];
    size_t position = 0, count = 0;
    sof_hashmap_element_t *e;
    CHECK(sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, NULL) == SOF_HASHMAP_OK);
    CHECK(sof_hashmap_next(&map, &position) == NULL);
    CHECK(insert_keys(&map, 1000) == 0);
    // Every element comes back exactly once, and data can be changed on the
    // way.
    position = 0;
    while ((e = sof_hashmap_next(&map, &position)) != NULL) {
        size_t i = (uintptr_t)e->data;
        CHECK(i < 1000 && !seen[i]);
        seen[i] = 1;
        e->data = (void *)(uintptr_t)(i + 1000);
        count++;
    }
    CHECK(count == 1000);
    CHECK(sof_hashmap_next(&map, &position) == NULL);
    CHECK((uintptr_t)find_key(&map, 7)->data == 1007);
    // A loop can stop and pick up where it left off.
    position = 0;
    count = 0;
    for (size_t i = 0; i < 10; i++) {
        CHECK(sof_hashmap_next(&map, &position) != NULL);
        count++;
    }
    size_t saved = position;
    while (sof_hashmap_next(&map, &saved) != NULL) {
        count++;
    }
    CHECK(count == 1000);
    CHECK(sof_hashmap_next(NULL, &position) == NULL);
    CHECK(sof_hashmap_next(&map, NULL) == NULL);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
    position = 0;
    CHECK(sof_hashmap_next(&map, &position) == NULL);
}

static void test_clear(void) {
    sof_hashmap_t map;
    counts_t counts;
    CHECK(init_counting(&map, &counts) == SOF_HASHMAP_OK);
    CHECK(insert_keys(&map, 1000) == 0);
    size_t capacity = map.capacity;
    CHECK(sof_hashmap_clear(&map) == SOF_HASHMAP_OK);
    CHECK(counts.deletes == 1000);
    CHECK(counts.bad_deletes == 0);
    CHECK(counts.frees == 1000);
    CHECK(map.count == 0);
    CHECK(map.capacity == capacity);
    CHECK(find_key(&map, 1) == NULL);
    CHECK(insert_keys(&map, 10) == 0);
    CHECK(find_key(&map, 1) != NULL);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
    CHECK(counts.frees == counts.copies);
    CHECK(counts.deletes == counts.copies);
}

static void test_remove(void) {
    sof_hashmap_t map;
    counts_t counts;
    const size_t n = 100000;
    char buf[32];
    CHECK(init_counting(&map, &counts) == SOF_HASHMAP_OK);
    CHECK(insert_keys(&map, n) == 0);
    // Remove every other key; the rest must still be found, wherever they
    // sit in a run of probed slots.
    for (size_t i = 0; i < n; i += 2) {
        CHECK(sof_hashmap_remove(&map, key_name(buf, i)) == SOF_HASHMAP_OK);
    }
    CHECK(counts.deletes == n / 2);
    CHECK(counts.bad_deletes == 0);
    CHECK(counts.frees == n / 2);
    CHECK(map.count == n / 2);
    for (size_t i = 0; i < n; i++) {
        sof_hashmap_element_t *e = find_key(&map, i);
        if (i % 2 == 0) {
            CHECK(e == NULL);
        } else {
            CHECK(e != NULL);
            CHECK((uintptr_t)e->data == i);
        }
    }
    CHECK(sof_hashmap_remove(&map, key_name(buf, 0)) == SOF_HASHMAP_NO_MATCH);
    CHECK(counts.deletes == n / 2);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
    CHECK(counts.frees == n);
    CHECK(counts.deletes == n);
}

static void test_remove_own_key(void) {
    sof_hashmap_t map;
    counts_t counts;
    CHECK(init_counting(&map, &counts) == SOF_HASHMAP_OK);
    CHECK(insert_keys(&map, 10) == 0);
    sof_hashmap_element_t *e = find_key(&map, 4);
    CHECK(e != NULL);
    // The element's own key, which the map frees.
    CHECK(sof_hashmap_remove(&map, e->key) == SOF_HASHMAP_OK);
    CHECK(map.count == 9);
    CHECK(counts.frees == 1);
    CHECK(find_key(&map, 4) == NULL);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
    CHECK(counts.frees == 10);
}

static void test_remove_element(void) {
    sof_hashmap_t map;
    counts_t counts;
    sof_hashmap_element_t outside = {0};
    CHECK(init_counting(&map, &counts) == SOF_HASHMAP_OK);
    CHECK(insert_keys(&map, 100) == 0);
    sof_hashmap_element_t *e = find_key(&map, 5);
    CHECK(e != NULL);
    CHECK(sof_hashmap_remove_element(&map, e) == SOF_HASHMAP_OK);
    CHECK(map.count == 99);
    CHECK(counts.deletes == 1 && counts.frees == 1);
    CHECK(find_key(&map, 5) == NULL);
    for (size_t i = 0; i < 100; i++) {
        CHECK(i == 5 || find_key(&map, i) != NULL);
    }
    // Pointers that aren't one of the map's elements are refused.
    sof_hashmap_element_t *empty = NULL;
    for (size_t i = 0; i < map.capacity && empty == NULL; i++) {
        if (map.elements[i].key == NULL) {
            empty = &map.elements[i];
        }
    }
    CHECK(empty != NULL);
    CHECK(sof_hashmap_remove_element(&map, empty) == SOF_HASHMAP_ERROR);
    CHECK(sof_hashmap_remove_element(&map, &outside) == SOF_HASHMAP_ERROR);
    CHECK(sof_hashmap_remove_element(&map, (sof_hashmap_element_t *)((char *)map.elements + 1)) == SOF_HASHMAP_ERROR);
    CHECK(sof_hashmap_remove_element(&map, map.elements + map.capacity) == SOF_HASHMAP_ERROR);
    CHECK(sof_hashmap_remove_element(&map, NULL) == SOF_HASHMAP_ERROR);
    CHECK(sof_hashmap_remove_element(NULL, e) == SOF_HASHMAP_ERROR);
    CHECK(map.count == 99);
    // Remove the rest by always taking the first element.
    size_t position = 0;
    while ((e = sof_hashmap_next(&map, &position)) != NULL) {
        CHECK(sof_hashmap_remove_element(&map, e) == SOF_HASHMAP_OK);
        position = 0;
    }
    CHECK(map.count == 0);
    CHECK(counts.deletes == 100 && counts.frees == 100);
    CHECK(counts.bad_deletes == 0);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
    CHECK(counts.deletes == 100);
}

static void test_identity_keys(void) {
    // With no callbacks, keys are told apart by address, not contents.
    sof_hashmap_t map;
    static int things[1000];
    char a[] = "same", b[] = "same";
    CHECK(sof_hashmap_init(&map, NULL, NULL, NULL, 0, NULL) == SOF_HASHMAP_OK);
    for (size_t i = 0; i < 1000; i++) {
        sof_hashmap_element_t *e = sof_hashmap_insert(&map, &things[i], NULL);
        CHECK(e != NULL);
        CHECK(e->key == &things[i]);
        e->data = (void *)(uintptr_t)i;
    }
    for (size_t i = 0; i < 1000; i++) {
        sof_hashmap_element_t *e = sof_hashmap_find(&map, &things[i]);
        CHECK(e != NULL);
        CHECK((uintptr_t)e->data == i);
    }
    CHECK(sof_hashmap_insert(&map, a, NULL) != NULL);
    CHECK(sof_hashmap_find(&map, a) != NULL);
    CHECK(sof_hashmap_find(&map, b) == NULL);
    CHECK(sof_hashmap_remove(&map, &things[7]) == SOF_HASHMAP_OK);
    CHECK(sof_hashmap_find(&map, &things[7]) == NULL);
    CHECK(map.count == 1000);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
}

/// @brief Case-insensitive string keys; ctx counts calls to equal.
static size_t nocase_hash(const void *key, void *ctx) {
    const unsigned char *s = key;
    size_t hash = 0;
    while (*s != '\0') {
        hash = hash * 31 + tolower(*s++);
    }
    return hash;
}

static int nocase_equal(const void *a, const void *b, void *ctx) {
    (*(size_t *)ctx)++;
    return strcasecmp(a, b) == 0;
}

static void test_custom_keys(void) {
    // Not copied or freed, so the map stores the caller's pointers.
    static const sof_hashmap_key_callbacks_t nocase_keys = {nocase_hash, nocase_equal, NULL, NULL};
    sof_hashmap_t map;
    size_t compares = 0;
    int created;
    const char *hello = "Hello";
    CHECK(sof_hashmap_init(&map, &nocase_keys, NULL, &compares, 0, NULL) == SOF_HASHMAP_OK);
    sof_hashmap_element_t *e = sof_hashmap_insert(&map, hello, &created);
    CHECK(e != NULL && created == 1);
    CHECK(e->key == hello);
    CHECK(sof_hashmap_insert(&map, "HELLO", &created) == e && created == 0);
    CHECK(sof_hashmap_find(&map, "hello") == e);
    CHECK(compares > 0);
    CHECK(sof_hashmap_remove(&map, "hElLo") == SOF_HASHMAP_OK);
    CHECK(map.count == 0);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
}

static void *failing_copy(const void *key, const sof_alloc_t *alloc, void *ctx) {
    return NULL;
}

static void test_failed_copy(void) {
    static const sof_hashmap_key_callbacks_t failing_keys = {
        sof_hashmap_string_hash, sof_hashmap_string_equal, failing_copy, NULL,
    };
    sof_hashmap_t map;
    int created = 1;
    CHECK(sof_hashmap_init(&map, &failing_keys, NULL, NULL, 0, NULL) == SOF_HASHMAP_OK);
    CHECK(sof_hashmap_insert(&map, "key", &created) == NULL);
    CHECK(created == 0);
    CHECK(map.count == 0);
    CHECK(sof_hashmap_find(&map, "key") == NULL);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
}

static void test_null(void) {
    sof_hashmap_t map;
    int created = 1;
    size_t seen = 0, position = 0;
    CHECK(sof_hashmap_init(NULL, &sof_hashmap_string_keys, NULL, NULL, 0, NULL) == SOF_HASHMAP_ERROR);
    CHECK(sof_hashmap_insert(NULL, "key", &created) == NULL);
    CHECK(created == 0);
    CHECK(sof_hashmap_find(NULL, "key") == NULL);
    CHECK(sof_hashmap_find_hashed(NULL, "key", 1) == NULL);
    CHECK(sof_hashmap_hash(NULL, "key") == 0);
    CHECK(sof_hashmap_next_hashed(NULL, 1, &position) == NULL);
    created = 1;
    CHECK(sof_hashmap_insert_hashed(NULL, "key", 1, &created) == NULL);
    CHECK(created == 0);
    CHECK(sof_hashmap_remove(NULL, "key") == SOF_HASHMAP_ERROR);
    CHECK(sof_hashmap_remove_element(NULL, NULL) == SOF_HASHMAP_ERROR);
    CHECK(sof_hashmap_traverse(NULL, count_element, NULL) == -1);
    CHECK(sof_hashmap_next(NULL, &position) == NULL);
    CHECK(sof_hashmap_clear(NULL) == SOF_HASHMAP_ERROR);
    CHECK(sof_hashmap_reserve(NULL, 10) == SOF_HASHMAP_ERROR);
    CHECK(sof_hashmap_free(NULL) == SOF_HASHMAP_ERROR);

    CHECK(sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, NULL) == SOF_HASHMAP_OK);
    CHECK(sof_hashmap_insert(&map, NULL, NULL) == NULL);
    CHECK(sof_hashmap_find(&map, NULL) == NULL);
    CHECK(sof_hashmap_find_hashed(&map, NULL, 1) == NULL);
    CHECK(sof_hashmap_hash(&map, NULL) == 0);
    CHECK(sof_hashmap_next_hashed(&map, 1, NULL) == NULL);
    created = 1;
    CHECK(sof_hashmap_insert_hashed(&map, NULL, 1, &created) == NULL);
    CHECK(created == 0);
    CHECK(sof_hashmap_remove(&map, NULL) == SOF_HASHMAP_ERROR);
    CHECK(map.count == 0);

    // A freed map is empty: nothing is found or inserted, and it can be
    // traversed, cleared and freed again.
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
    CHECK(sof_hashmap_find(&map, "key") == NULL);
    CHECK(sof_hashmap_insert(&map, "key", NULL) == NULL);
    CHECK(sof_hashmap_find_hashed(&map, "key", 1) == NULL);
    CHECK(sof_hashmap_hash(&map, "key") == 0);
    position = 0;
    CHECK(sof_hashmap_next_hashed(&map, 1, &position) == NULL);
    created = 1;
    CHECK(sof_hashmap_insert_hashed(&map, "key", 1, &created) == NULL);
    CHECK(created == 0);
    CHECK(sof_hashmap_remove(&map, "key") == SOF_HASHMAP_NO_MATCH);
    CHECK(sof_hashmap_traverse(&map, count_element, &seen) == 0);
    CHECK(seen == 0);
    CHECK(sof_hashmap_reserve(&map, 10) == SOF_HASHMAP_ERROR);
    CHECK(sof_hashmap_clear(&map) == SOF_HASHMAP_OK);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
}

/// @brief A string hash that counts its calls in the size_t at ctx.
static size_t counting_hash(const void *key, void *ctx) {
    (*(size_t *)ctx)++;
    return sof_hashmap_string_hash(key, NULL);
}

static void test_hashed(void) {
    // Keys moved from one map to another with their stored hashes are never
    // hashed again, and land where the ordinary functions find them.
    static const sof_hashmap_key_callbacks_t keys = {
        counting_hash, sof_hashmap_string_equal, NULL, NULL,
    };
    static char names[3000][32];
    size_t hashes = 0;
    sof_hashmap_t a, b;
    CHECK(sof_hashmap_init(&a, &keys, NULL, &hashes, 0, NULL) == SOF_HASHMAP_OK);
    CHECK(sof_hashmap_init(&b, &keys, NULL, &hashes, 0, NULL) == SOF_HASHMAP_OK);
    for (size_t i = 0; i < 3000; i++) {
        CHECK(sof_hashmap_insert(&a, key_name(names[i], i), NULL) != NULL);
    }
    CHECK(hashes == 3000);
    // Every other key goes into b, which grows several times on the way.
    size_t position = 0;
    sof_hashmap_element_t *e;
    size_t moved = 0;
    while ((e = sof_hashmap_next(&a, &position)) != NULL) {
        if (moved++ % 2 == 0) {
            int created = 0;
            sof_hashmap_element_t *made = sof_hashmap_insert_hashed(&b, e->key, e->hash, &created);
            CHECK(made != NULL && created == 1 && made->key == e->key && made->hash == e->hash);
            made->data = e->key;
            // A second insert finds it.
            CHECK(sof_hashmap_insert_hashed(&b, e->key, e->hash, &created) == made && created == 0);
        }
    }
    CHECK(b.count == 1500 && b.capacity == 2048);
    CHECK(hashes == 3000);
    position = 0;
    size_t found = 0;
    while ((e = sof_hashmap_next(&a, &position)) != NULL) {
        sof_hashmap_element_t *f = sof_hashmap_find_hashed(&b, e->key, e->hash);
        CHECK(f == NULL || (f->key == e->key && f->data == e->key));
        found += f != NULL;
        // The map's own element and hash find it too.
        CHECK(sof_hashmap_find_hashed(&a, e->key, e->hash) == e);
    }
    CHECK(found == 1500);
    CHECK(hashes == 3000);
    // The ordinary find hashes, and agrees.
    for (size_t i = 0; i < 3000; i++) {
        sof_hashmap_element_t *f = sof_hashmap_find(&b, names[i]);
        CHECK(f == NULL || strcmp(f->key, names[i]) == 0);
    }
    CHECK(hashes == 6000);
    // An equal key at another address is found by its hash.
    sof_hashmap_element_t *first = sof_hashmap_find(&a, "key7");
    CHECK(first != NULL);
    char copy[32] = "key7";
    CHECK(sof_hashmap_find_hashed(&a, copy, first->hash) == first);
    CHECK(sof_hashmap_free(&a) == SOF_HASHMAP_OK);
    CHECK(sof_hashmap_free(&b) == SOF_HASHMAP_OK);

    // Maps keyed by identity, with no hash callback, work the same way.
    int values[100];
    CHECK(sof_hashmap_init(&a, NULL, NULL, NULL, 0, NULL) == SOF_HASHMAP_OK);
    CHECK(sof_hashmap_init(&b, NULL, NULL, NULL, 0, NULL) == SOF_HASHMAP_OK);
    for (size_t i = 0; i < 100; i++) {
        CHECK(sof_hashmap_insert(&a, &values[i], NULL) != NULL);
    }
    position = 0;
    while ((e = sof_hashmap_next(&a, &position)) != NULL) {
        CHECK(sof_hashmap_insert_hashed(&b, e->key, e->hash, NULL) != NULL);
    }
    for (size_t i = 0; i < 100; i++) {
        CHECK(sof_hashmap_find(&b, &values[i]) != NULL);
    }
    CHECK(sof_hashmap_free(&a) == SOF_HASHMAP_OK);
    CHECK(sof_hashmap_free(&b) == SOF_HASHMAP_OK);
}

/// @brief An equal callback that counts its calls in the size_t at ctx and
/// compares strings.
static int counting_equal(const void *a, const void *b, void *ctx) {
    (*(size_t *)ctx)++;
    return strcmp(a, b) == 0;
}

static void test_same_pointer(void) {
    // A key that is the map's own pointer is found without calling equal;
    // an equal key at another address is compared as usual.
    static const sof_hashmap_key_callbacks_t keys = {
        sof_hashmap_string_hash, counting_equal, NULL, NULL,
    };
    size_t calls = 0;
    sof_hashmap_t map;
    static char names[200][32];
    CHECK(sof_hashmap_init(&map, &keys, NULL, &calls, 0, NULL) == SOF_HASHMAP_OK);
    for (size_t i = 0; i < 200; i++) {
        CHECK(sof_hashmap_insert(&map, key_name(names[i], i), NULL) != NULL);
    }
    calls = 0;
    for (size_t i = 0; i < 200; i++) {
        sof_hashmap_element_t *e = sof_hashmap_find(&map, names[i]);
        CHECK(e != NULL && e->key == names[i]);
        CHECK(sof_hashmap_find_hashed(&map, names[i], e->hash) == e);
        CHECK(sof_hashmap_insert(&map, names[i], NULL) == e);
    }
    CHECK(calls == 0);
    char copy[32];
    sof_hashmap_element_t *e = sof_hashmap_find(&map, key_name(copy, 5));
    CHECK(e != NULL && e->key == names[5]);
    CHECK(calls >= 1);
    calls = 0;
    CHECK(sof_hashmap_remove(&map, names[7]) == SOF_HASHMAP_OK);
    CHECK(calls == 0);
    CHECK(sof_hashmap_find(&map, key_name(copy, 7)) == NULL);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
}

/// @brief A hash that gives every key the same value.
static size_t constant_hash(const void *key, void *ctx) {
    return 42;
}

static void test_hash_and_next_hashed(void) {
    sof_hashmap_t map;
    static char names[100][32];
    CHECK(sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, NULL) == SOF_HASHMAP_OK);
    for (size_t i = 0; i < 100; i++) {
        CHECK(sof_hashmap_insert(&map, key_name(names[i], i), NULL) != NULL);
    }
    // The hash is the one the elements store, and finds them.
    for (size_t i = 0; i < 100; i++) {
        sof_hashmap_element_t *e = sof_hashmap_find(&map, names[i]);
        size_t hash = sof_hashmap_hash(&map, names[i]);
        CHECK(e != NULL && e->hash == hash);
        CHECK(sof_hashmap_find_hashed(&map, names[i], hash) == e);
        size_t position = 0, seen = 0;
        sof_hashmap_element_t *f;
        while ((f = sof_hashmap_next_hashed(&map, hash, &position)) != NULL) {
            CHECK(f->hash == hash);
            seen += f == e;
        }
        CHECK(seen == 1);
        // The loop stays at its end.
        CHECK(sof_hashmap_next_hashed(&map, hash, &position) == NULL);
    }
    size_t position = 0;
    CHECK(sof_hashmap_next_hashed(&map, sof_hashmap_hash(&map, "absent"), &position) == NULL);
    // An insert with the hash from sof_hashmap_hash() is found as usual.
    CHECK(sof_hashmap_insert_hashed(&map, "new", sof_hashmap_hash(&map, "new"), NULL) != NULL);
    CHECK(sof_hashmap_find(&map, "new") != NULL);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);

    // With every hash the same, the loop gives every element once, from one
    // run of 12 slots in 16.
    static const sof_hashmap_key_callbacks_t same = {constant_hash, sof_hashmap_string_equal, NULL, NULL};
    CHECK(sof_hashmap_init(&map, &same, NULL, NULL, 0, NULL) == SOF_HASHMAP_OK);
    for (size_t i = 0; i < 12; i++) {
        CHECK(sof_hashmap_insert(&map, names[i], NULL) != NULL);
    }
    size_t hash = sof_hashmap_hash(&map, "anything");
    CHECK(hash == sof_hashmap_hash(&map, names[3]));
    char seen[12] = {0};
    size_t count = 0;
    position = 0;
    sof_hashmap_element_t *e;
    while ((e = sof_hashmap_next_hashed(&map, hash, &position)) != NULL) {
        size_t n = (size_t)strtoul((const char *)e->key + 3, NULL, 10);
        CHECK(n < 12 && !seen[n]);
        seen[n] = 1;
        count++;
    }
    CHECK(count == 12);
    position = 0;
    CHECK(sof_hashmap_next_hashed(&map, hash + 1, &position) == NULL);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
}

static void test_hashed_no_memory(void) {
    // sof_hashmap_insert_hashed() copies keys and grows the map with the
    // map's allocator, and a failure leaves the map as it was.
    sof_hashmap_t source;
    CHECK(sof_hashmap_init(&source, &sof_hashmap_string_keys, NULL, NULL, 0, NULL) == SOF_HASHMAP_OK);
    CHECK(insert_keys(&source, 100) == 0);
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_hashmap_t map;
        if (sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, &alloc) == SOF_HASHMAP_OK) {
            size_t inserted = 0, failed = 0, position = 0;
            sof_hashmap_element_t *e;
            while ((e = sof_hashmap_next(&source, &position)) != NULL) {
                int created = 1;
                sof_hashmap_element_t *made = sof_hashmap_insert_hashed(&map, e->key, e->hash, &created);
                if (made != NULL) {
                    CHECK(created == 1 && made->key != e->key && strcmp(made->key, e->key) == 0);
                    inserted++;
                } else {
                    CHECK(created == 0);
                    failed++;
                }
            }
            CHECK(failed == (state.allocations >= n));
            CHECK(map.count == inserted);
            size_t found = 0;
            position = 0;
            while ((e = sof_hashmap_next(&source, &position)) != NULL) {
                found += sof_hashmap_find(&map, e->key) != NULL;
            }
            CHECK(found == inserted);
        }
        CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            break;
        }
    }
    CHECK(sof_hashmap_free(&source) == SOF_HASHMAP_OK);
}

static void test_reserve(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_hashmap_t map;
    CHECK(sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, &alloc) == SOF_HASHMAP_OK);
    CHECK(insert_keys(&map, 10) == 0);
    // Room the map already has changes nothing, and it never shrinks.
    CHECK(sof_hashmap_reserve(&map, 12) == SOF_HASHMAP_OK);
    CHECK(map.capacity == 16);
    CHECK(sof_hashmap_reserve(&map, 0) == SOF_HASHMAP_OK);
    CHECK(map.capacity == 16);
    // More room moves the elements to the capacity init would choose.
    CHECK(sof_hashmap_reserve(&map, 1000) == SOF_HASHMAP_OK);
    CHECK(map.capacity == 2048 && map.mask == 2047);
    CHECK(map.count == 10);
    for (size_t i = 0; i < 10; i++) {
        sof_hashmap_element_t *e = find_key(&map, i);
        CHECK(e != NULL && (uintptr_t)e->data == i);
    }
    // Inserting up to the number reserved allocates only the key copies.
    size_t allocations = state.allocations;
    char buf[32];
    for (size_t i = 10; i < 1000; i++) {
        CHECK(sof_hashmap_insert(&map, key_name(buf, i), NULL) != NULL);
    }
    CHECK(map.capacity == 2048);
    CHECK(state.allocations == allocations + 990);
    // A full map grows on the next new key, but not on one after a remove.
    while (map.count < map.capacity / 4 * 3) {
        CHECK(sof_hashmap_insert(&map, key_name(buf, map.count), NULL) != NULL);
    }
    CHECK(map.capacity == 2048);
    CHECK(sof_hashmap_remove(&map, key_name(buf, 0)) == SOF_HASHMAP_OK);
    allocations = state.allocations;
    CHECK(sof_hashmap_insert(&map, "after a remove", NULL) != NULL);
    CHECK(map.capacity == 2048);
    CHECK(state.allocations == allocations + 1);
    CHECK(sof_hashmap_insert(&map, "one more", NULL) != NULL);
    CHECK(map.capacity == 4096);
    // Too many keys, or no memory, leave the map as it was.
    size_t count = map.count;
    CHECK(sof_hashmap_reserve(&map, SIZE_MAX) == SOF_HASHMAP_NO_MEM);
    CHECK(map.capacity == 4096 && map.count == count);
    state.fail_at = state.allocations + 1;
    CHECK(sof_hashmap_reserve(&map, 10000) == SOF_HASHMAP_NO_MEM);
    CHECK(map.capacity == 4096 && map.count == count);
    CHECK(find_key(&map, 5) != NULL);
    state.fail_at = 0;
    CHECK(sof_hashmap_reserve(&map, 10000) == SOF_HASHMAP_OK);
    CHECK(map.capacity == 16384 && map.count == count);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_expected_keys(void) {
    // Each size gets the smallest power of two that holds it without growing.
    static const struct {
        size_t keys;
        size_t capacity;
    } sizes[] = {
        {0, 16}, {1, 16}, {12, 16}, {13, 32}, {1000, 2048}, {1536, 2048}, {1537, 4096}, {100000, 262144},
    };
    sof_hashmap_t map;
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        CHECK(sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, sizes[i].keys, NULL) == SOF_HASHMAP_OK);
        CHECK(map.capacity == sizes[i].capacity);
        CHECK(map.mask == sizes[i].capacity - 1);
        CHECK(insert_keys(&map, sizes[i].keys) == 0);
        CHECK(map.capacity == sizes[i].capacity);
        // One more key makes it grow only if it was exactly full.
        size_t full = sizes[i].capacity / 4 * 3;
        CHECK(sof_hashmap_insert(&map, "extra", NULL) != NULL);
        CHECK(map.capacity == (sizes[i].keys == full ? sizes[i].capacity * 2 : sizes[i].capacity));
        CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
    }
    // More keys than any array could hold fail cleanly.
    CHECK(sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, SIZE_MAX, NULL) == SOF_HASHMAP_NO_MEM);
    CHECK(map.elements == NULL);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
}

/// @brief A poor hash whose low bits are all zero.
static size_t shifted_hash(const void *key, void *ctx) {
    return sof_hashmap_string_hash(key, ctx) << 20;
}

static void test_hash_mixing(void) {
    // The map mixes the hash's bits, so keys still spread out across the
    // slots instead of piling up in one long run.
    static const sof_hashmap_key_callbacks_t shifted_keys = {
        shifted_hash, sof_hashmap_string_equal, NULL, NULL,
    };
    sof_hashmap_t map;
    static char names[10000][32];
    CHECK(sof_hashmap_init(&map, &shifted_keys, NULL, NULL, 0, NULL) == SOF_HASHMAP_OK);
    for (size_t i = 0; i < 10000; i++) {
        CHECK(sof_hashmap_insert(&map, key_name(names[i], i), NULL) != NULL);
    }
    size_t longest = 0;
    for (size_t i = 0; i < map.capacity; i++) {
        if (map.elements[i].key != NULL) {
            size_t distance = (i - (map.elements[i].hash & map.mask)) & map.mask;
            if (distance > longest) {
                longest = distance;
            }
        }
    }
    CHECK(longest < 100);
    for (size_t i = 0; i < 10000; i++) {
        CHECK(find_key(&map, i) != NULL);
    }
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
}

static void test_custom_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_hashmap_t map;
    char buf[32];
    CHECK(sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, &alloc) == SOF_HASHMAP_OK);
    // The map keeps a copy of the allocator.
    alloc.malloc = NULL;
    CHECK(map.alloc.ctx == &state);
    CHECK(state.live == 1);
    // The string keys are copied with the map's allocator, and freed with the
    // sizes they were allocated with.
    CHECK(insert_keys(&map, 1000) == 0);
    CHECK(state.live == 1001);
    for (size_t i = 0; i < 500; i++) {
        CHECK(sof_hashmap_remove(&map, key_name(buf, i)) == SOF_HASHMAP_OK);
    }
    CHECK(state.live == 501);
    CHECK(sof_hashmap_clear(&map) == SOF_HASHMAP_OK);
    CHECK(state.live == 1);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // An allocator with a malloc but no free is refused, without allocating.
    size_t allocations = state.allocations;
    alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    CHECK(sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, &alloc) == SOF_HASHMAP_ERROR);
    CHECK(map.elements == NULL);
    CHECK(state.allocations == allocations);
}

static void test_no_memory(void) {
    // Fail each allocation in turn, through init, inserts that copy keys and
    // grow the map, removes and free, until a run makes none fail.
    char buf[32];
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_hashmap_t map;
        sof_hashmap_error_t rc = sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, &alloc);
        if (rc != SOF_HASHMAP_OK) {
            CHECK(rc == SOF_HASHMAP_NO_MEM);
            CHECK(map.elements == NULL);
            CHECK(sof_hashmap_insert(&map, "key", NULL) == NULL);
        } else {
            size_t inserted = 0;
            int failed = 0;
            for (size_t i = 0; i < 200; i++) {
                if (sof_hashmap_insert(&map, key_name(buf, i), NULL) != NULL) {
                    inserted++;
                } else {
                    failed++;
                }
            }
            // Only the chosen allocation fails, and a failed insert leaves
            // the map as it was.
            CHECK(failed == (state.allocations >= n));
            CHECK(map.count == inserted);
            size_t found = 0;
            for (size_t i = 0; i < 200; i++) {
                found += find_key(&map, i) != NULL;
            }
            CHECK(found == inserted);
            for (size_t i = 0; i < 100; i++) {
                sof_hashmap_remove(&map, key_name(buf, i));
            }
        }
        CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            break;
        }
    }
}

/// @brief A map shared by one writer and several readers under a
/// readers-writer lock, as in the README's Threading section. The map always
/// holds exactly the keys key<lo> to key<hi - 1>, each with its number as the
/// data; lo and hi are changed only under the write lock.
typedef struct rw_shared_t {
    sof_hashmap_t map;
    pthread_rwlock_t lock;
    size_t lo;
    size_t hi;
} rw_shared_t;

/// @brief One reader thread's state and results, checked by the main thread
/// after joining, since CHECK isn't safe on other threads. Readers make a
/// fixed number of passes rather than reading until the writer is done:
/// glibc's rwlock prefers readers, so readers that keep taking the lock can
/// keep the writer out forever.
typedef struct reader_t {
    rw_shared_t *shared;
    uint64_t random;
    size_t passes;
    size_t errors;
} reader_t;

/// @brief What check_element() expects, and what it found.
typedef struct range_check_t {
    size_t lo;
    size_t hi;
    size_t seen;
    size_t sum;
} range_check_t;

/// @brief Checks, for sof_hashmap_traverse(), that an element's key is in the
/// range and matches its data, and counts and sums it. Returns 1 to stop at
/// a bad element.
static int check_element(sof_hashmap_element_t *element, void *ctx) {
    range_check_t *check = ctx;
    const char *key = element->key;
    if (strncmp(key, "key", 3) != 0) {
        return 1;
    }
    size_t n = (size_t)strtoull(key + 3, NULL, 10);
    if (n < check->lo || n >= check->hi || (uintptr_t)element->data != n) {
        return 1;
    }
    check->seen++;
    check->sum += n;
    return 0;
}

/// @brief The sum of lo to hi - 1.
static size_t range_sum(size_t lo, size_t hi) {
    return hi * (hi - 1) / 2 - (lo == 0 ? 0 : lo * (lo - 1) / 2);
}

/// @brief Reads the map PASSES times under the read lock, with
/// sof_hashmap_traverse(), sof_hashmap_next() and sof_hashmap_find(), checking each time
/// that it holds exactly the keys from lo to hi - 1.
static void *read_map(void *arg) {
    reader_t *r = arg;
    rw_shared_t *s = r->shared;
    char buf[32];
    for (size_t pass = 0; pass < PASSES; pass++) {
        pthread_rwlock_rdlock(&s->lock);
        size_t lo = s->lo, hi = s->hi;
        if (s->map.count != hi - lo) {
            r->errors++;
        }
        range_check_t check = {lo, hi, 0, 0};
        if (sof_hashmap_traverse(&s->map, check_element, &check) != 0 || check.seen != hi - lo || check.sum != range_sum(lo, hi)) {
            r->errors++;
        }
        range_check_t looped = {lo, hi, 0, 0};
        size_t position = 0;
        sof_hashmap_element_t *e;
        while ((e = sof_hashmap_next(&s->map, &position)) != NULL) {
            if (check_element(e, &looped) != 0) {
                r->errors++;
                break;
            }
        }
        if (looped.seen != hi - lo || looped.sum != range_sum(lo, hi)) {
            r->errors++;
        }
        for (size_t i = 0; i < 8 && hi > lo; i++) {
            size_t n = lo + next_random(&r->random) % (hi - lo);
            e = sof_hashmap_find(&s->map, key_name(buf, n));
            if (e == NULL || (uintptr_t)e->data != n) {
                r->errors++;
            }
        }
        if (sof_hashmap_find(&s->map, key_name(buf, hi)) != NULL || (lo > 0 && sof_hashmap_find(&s->map, key_name(buf, lo - 1)) != NULL)) {
            r->errors++;
        }
        pthread_rwlock_unlock(&s->lock);
        r->passes++;
    }
    return NULL;
}

static void test_threads_readers(void) {
    // One writer inserts key<hi> and removes key<lo> from the other end, so
    // the map grows (moving every element) and removes shift elements back,
    // while readers look at the map under a read lock and must always see
    // exactly the keys from lo to hi - 1.
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    rw_shared_t s;
    s.lo = s.hi = 0;
    CHECK(sof_hashmap_init(&s.map, &sof_hashmap_string_keys, NULL, NULL, 0, &alloc) == SOF_HASHMAP_OK);
    CHECK(pthread_rwlock_init(&s.lock, NULL) == 0);
    size_t capacity = s.map.capacity;
    reader_t readers[READERS] = {{0}};
    pthread_t threads[READERS];
    for (size_t i = 0; i < READERS; i++) {
        readers[i].shared = &s;
        readers[i].random = 0x9e3779b97f4a7c15ULL + i;
        CHECK(pthread_create(&threads[i], NULL, read_map, &readers[i]) == 0);
    }
    uint64_t random = 0x2545f4914f6cdd1dULL;
    size_t errors = 0;
    char buf[32];
    for (size_t i = 0; i < ITEMS; i++) {
        pthread_rwlock_wrlock(&s.lock);
        sof_hashmap_element_t *e = sof_hashmap_insert(&s.map, key_name(buf, s.hi), NULL);
        if (e == NULL) {
            errors++;
        } else {
            e->data = (void *)(uintptr_t)s.hi;
        }
        s.hi++;
        // The map holds about a quarter of the keys inserted so far.
        size_t target = 1 + s.hi / 4 + next_random(&random) % 64;
        while (s.map.count > target) {
            if (sof_hashmap_remove(&s.map, key_name(buf, s.lo)) != SOF_HASHMAP_OK) {
                errors++;
            }
            s.lo++;
        }
        pthread_rwlock_unlock(&s.lock);
    }
    for (size_t i = 0; i < READERS; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    CHECK(errors == 0);
    for (size_t i = 0; i < READERS; i++) {
        CHECK(readers[i].errors == 0);
        CHECK(readers[i].passes == PASSES);
    }
    CHECK(s.hi == ITEMS);
    CHECK(s.map.count == s.hi - s.lo);
    CHECK(s.map.capacity > capacity);
    range_check_t check = {s.lo, s.hi, 0, 0};
    CHECK(sof_hashmap_traverse(&s.map, check_element, &check) == 0);
    CHECK(check.seen == s.hi - s.lo);
    CHECK(sof_hashmap_free(&s.map) == SOF_HASHMAP_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    pthread_rwlock_destroy(&s.lock);
}

/// @brief One thread with a map of its own, its allocator, its callback
/// counts and the contents the map should have, checked by the main thread
/// after joining.
typedef struct worker_t {
    uint64_t random;
    test_alloc_t state;
    counts_t counts;
    /// @brief The data key<i> should have, plus one, or 0 if it shouldn't be
    /// in the map.
    uintptr_t expected[WORKER_KEYS];
    size_t present;
    size_t errors;
} worker_t;

/// @brief Makes WORKER_OPS random inserts and removes in a map of the
/// worker's own, with no lock, checking each against expected, then checks
/// the whole map and frees it.
static void *work_alone(void *arg) {
    worker_t *w = arg;
    sof_alloc_t alloc = test_allocator(&w->state);
    sof_hashmap_t map;
    char buf[32];
    if (sof_hashmap_init(&map, &counting_string_keys, counting_delete, &w->counts, 0, &alloc) != SOF_HASHMAP_OK) {
        w->errors++;
        return NULL;
    }
    for (size_t op = 0; op < WORKER_OPS; op++) {
        size_t n = next_random(&w->random) % WORKER_KEYS;
        key_name(buf, n);
        if (next_random(&w->random) % 3 != 0) {
            int created;
            sof_hashmap_element_t *e = sof_hashmap_insert(&map, buf, &created);
            if (e == NULL || created != (w->expected[n] == 0)) {
                w->errors++;
                continue;
            }
            w->present += created;
            w->expected[n] = op + 1;
            e->data = (void *)(uintptr_t)(op + 1);
        } else {
            sof_hashmap_error_t rc = sof_hashmap_remove(&map, buf);
            if (rc != (w->expected[n] != 0 ? SOF_HASHMAP_OK : SOF_HASHMAP_NO_MATCH)) {
                w->errors++;
            }
            if (rc == SOF_HASHMAP_OK) {
                w->present--;
                w->expected[n] = 0;
            }
        }
        if (map.count != w->present) {
            w->errors++;
        }
    }
    for (size_t n = 0; n < WORKER_KEYS; n++) {
        sof_hashmap_element_t *e = sof_hashmap_find(&map, key_name(buf, n));
        if (w->expected[n] == 0 ? e != NULL : e == NULL || (uintptr_t)e->data != w->expected[n]) {
            w->errors++;
        }
    }
    if (sof_hashmap_free(&map) != SOF_HASHMAP_OK) {
        w->errors++;
    }
    return NULL;
}

static void test_threads_separate(void) {
    // Separate maps on separate threads need no lock: each thread makes its
    // own random changes, with its own allocator and callback ctx, and ends
    // with exactly what it expects and nothing leaked.
    static worker_t workers[WORKERS];
    pthread_t threads[WORKERS];
    memset(workers, 0, sizeof(workers));
    for (size_t i = 0; i < WORKERS; i++) {
        workers[i].random = 0x2545f4914f6cdd1dULL * (i + 1);
        CHECK(pthread_create(&threads[i], NULL, work_alone, &workers[i]) == 0);
    }
    for (size_t i = 0; i < WORKERS; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    for (size_t i = 0; i < WORKERS; i++) {
        worker_t *w = &workers[i];
        CHECK(w->errors == 0);
        CHECK(w->counts.copies > 0);
        CHECK(w->counts.frees == w->counts.copies);
        CHECK(w->counts.deletes == w->counts.copies);
        CHECK(w->counts.bad_deletes == 0);
        CHECK(w->state.live == 0);
        CHECK(w->state.bad == 0);
    }
}

/// @brief The functions that only read take a const map, and hand back
/// elements whose data can still be changed.
static void test_const_map(void) {
    sof_hashmap_t map;
    counts_t counts;
    char buf[32];
    CHECK(init_counting(&map, &counts) == SOF_HASHMAP_OK);
    CHECK(insert_keys(&map, 10) == 0);
    const sof_hashmap_t *cmap = &map;
    sof_hashmap_element_t *e = sof_hashmap_find(cmap, key_name(buf, 3));
    CHECK(e != NULL);
    CHECK((uintptr_t)e->data == 3);
    e->data = (void *)(uintptr_t)30;
    e = sof_hashmap_find(cmap, key_name(buf, 3));
    CHECK(e != NULL);
    CHECK((uintptr_t)e->data == 30);
    CHECK(sof_hashmap_find_hashed(cmap, e->key, e->hash) == e);
    CHECK(sof_hashmap_find(cmap, key_name(buf, 10)) == NULL);
    size_t position = 0;
    size_t seen = 0;
    while (sof_hashmap_next(cmap, &position) != NULL) {
        seen++;
    }
    CHECK(seen == 10);
    size_t visited = 0;
    CHECK(sof_hashmap_traverse(cmap, count_element, &visited) == 0);
    CHECK(visited == 10);
    CHECK(map.count == 10);
    CHECK(counts.copies == 10);
    CHECK(sof_hashmap_free(&map) == SOF_HASHMAP_OK);
    CHECK(counts.frees == 10);
}

int main(void) {
    printf("sof_hashmap:\n");
    run("insert and find", test_insert_find);
    run("insert an existing key", test_insert_existing);
    run("traverse", test_traverse);
    run("loop with sof_hashmap_next", test_next);
    run("read through a const map", test_const_map);
    run("clear", test_clear);
    run("remove", test_remove);
    run("remove with the element's own key", test_remove_own_key);
    run("remove an element", test_remove_element);
    run("identity keys", test_identity_keys);
    run("custom key callbacks", test_custom_keys);
    run("failed key copy", test_failed_copy);
    run("find and insert with a stored hash", test_hashed);
    run("out of memory inserting with a stored hash", test_hashed_no_memory);
    run("a key's own pointer skips equal", test_same_pointer);
    run("hash and loop over the elements with a hash", test_hash_and_next_hashed);
    run("NULL arguments and freed maps", test_null);
    run("initial capacity from expected keys", test_expected_keys);
    run("reserve, and when inserts grow the map", test_reserve);
    run("poor hashes are mixed", test_hash_mixing);
    run("custom allocator", test_custom_allocator);
    run("out of memory", test_no_memory);
    run("readers alongside a writer on threads", test_threads_readers);
    run("separate maps on separate threads", test_threads_separate);
    return test_report();
}
