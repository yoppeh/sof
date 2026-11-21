/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_lru.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sof_lru.h"
#include "test.h"

/// @brief The number of distinct keys the randomized tests draw from.
#define UNIVERSE 100

/// @brief Counts key copies and frees and deleted entries, and remembers the
/// last deleted key, through the cache's ctx.
typedef struct counts_t {
    size_t copies;
    size_t frees;
    size_t deletes;
    char last[32];
    const void *last_key;
} counts_t;

static void *counting_copy(const void *key, const sof_alloc_t *alloc, void *ctx) {
    void *copy = sof_hashmap_string_keys.copy(key, alloc, NULL);
    if (copy != NULL) {
        ((counts_t *)ctx)->copies++;
    }
    return copy;
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

/// @brief A delete function for string keys, which counts and remembers the
/// deleted key.
static void string_deleted(sof_lru_node_t *node, void *ctx) {
    counts_t *counts = ctx;
    counts->deletes++;
    snprintf(counts->last, sizeof(counts->last), "%s", (const char *)node->key);
}

/// @brief A delete function for identity keys, which counts and remembers
/// the deleted key.
static void key_deleted(sof_lru_node_t *node, void *ctx) {
    counts_t *counts = ctx;
    counts->deletes++;
    counts->last_key = node->key;
}

/// @brief A hash that gives only three values, so keys pile up in long runs
/// and evictions move elements around in the cache's map.
static size_t weak_hash(const void *key, void *ctx) {
    return (size_t)(uintptr_t)key % 3;
}

static const sof_hashmap_key_callbacks_t weak_identity_keys = {
    weak_hash,
    NULL,
    NULL,
    NULL,
};

/// @brief Returns an identity key for i: never NULL.
static void *int_key(size_t i) {
    return (void *)(uintptr_t)(i + 1);
}

/// @brief Returns 1 if the cache holds exactly the n string keys in order,
/// newest first, walking both ways.
static int order_is(const sof_lru_t *lru, const char *const *order, size_t n) {
    if (lru->count != n) {
        return 0;
    }
    size_t i = 0;
    for (sof_lru_node_t *node = sof_lru_newest(lru); node != NULL; node = sof_lru_next(lru, node)) {
        if (i >= n || strcmp(node->key, order[i]) != 0) {
            return 0;
        }
        i++;
    }
    if (i != n) {
        return 0;
    }
    for (sof_lru_node_t *node = sof_lru_oldest(lru); node != NULL; node = sof_lru_prev(lru, node)) {
        if (i == 0 || strcmp(node->key, order[i - 1]) != 0) {
            return 0;
        }
        i--;
    }
    return i == 0;
}

/// @brief Inserts the string keys k0 to k(count - 1).
static void insert_range(sof_lru_t *lru, size_t count) {
    char buffer[32];
    for (size_t i = 0; i < count; i++) {
        snprintf(buffer, sizeof(buffer), "k%zu", i);
        sof_lru_insert(lru, buffer, NULL);
    }
}

/// @brief Counts the entries a traversal visits, and stops after stop_after
/// of them if that isn't 0.
typedef struct visits_t {
    size_t visited;
    size_t stop_after;
} visits_t;

static int count_visit(sof_lru_node_t *node, void *ctx) {
    visits_t *v = ctx;
    v->visited++;
    return v->stop_after != 0 && v->visited == v->stop_after ? 42 : 0;
}

static void test_insert_find(void) {
    counts_t counts = {0};
    sof_lru_t lru;
    CHECK(sof_lru_init(&lru, &counting_string_keys, string_deleted, &counts, 4, NULL) == SOF_LRU_OK);
    CHECK(lru.capacity == 4);
    CHECK(lru.count == 0);
    CHECK(sof_lru_newest(&lru) == NULL);
    CHECK(sof_lru_oldest(&lru) == NULL);
    char buffer[8] = "a";
    int created = -1;
    sof_lru_node_t *a = sof_lru_insert(&lru, buffer, &created);
    CHECK(a != NULL);
    CHECK(created == 1);
    CHECK(a->data == NULL);
    CHECK(a->key != buffer);
    CHECK(strcmp(a->key, "a") == 0);
    CHECK(counts.copies == 1);
    a->data = &counts;
    // The cache keeps its own copy, so the buffer can be reused.
    strcpy(buffer, "b");
    sof_lru_node_t *b = sof_lru_insert(&lru, buffer, &created);
    CHECK(b != NULL && b != a);
    CHECK(created == 1);
    CHECK(sof_lru_insert(&lru, "a", &created) == a);
    CHECK(created == 0);
    CHECK(a->data == &counts);
    CHECK(counts.copies == 2);
    CHECK(sof_lru_find(&lru, "a") == a);
    CHECK(sof_lru_find(&lru, "b") == b);
    CHECK(sof_lru_find(&lru, "c") == NULL);
    CHECK(sof_lru_peek(&lru, "a") == a);
    CHECK(sof_lru_peek(&lru, "c") == NULL);
    CHECK(lru.count == 2);
    CHECK(sof_lru_insert(&lru, "c", NULL) != NULL);
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
    CHECK(counts.deletes == 3);
    CHECK(counts.frees == 3);
}

static void test_eviction(void) {
    counts_t counts = {0};
    sof_lru_t lru;
    CHECK(sof_lru_init(&lru, &counting_string_keys, string_deleted, &counts, 3, NULL) == SOF_LRU_OK);
    sof_lru_insert(&lru, "a", NULL);
    sof_lru_insert(&lru, "b", NULL);
    sof_lru_insert(&lru, "c", NULL);
    CHECK(order_is(&lru, (const char *[]){"c", "b", "a"}, 3));
    CHECK(counts.deletes == 0);
    // A new key in a full cache evicts the oldest.
    int created = 0;
    sof_lru_node_t *d = sof_lru_insert(&lru, "d", &created);
    CHECK(d != NULL && created == 1);
    CHECK(strcmp(d->key, "d") == 0);
    CHECK(counts.deletes == 1);
    CHECK(strcmp(counts.last, "a") == 0);
    CHECK(counts.frees == 1);
    CHECK(sof_lru_peek(&lru, "a") == NULL);
    CHECK(order_is(&lru, (const char *[]){"d", "c", "b"}, 3));
    // Finding a key makes it the newest, so it outlives the others.
    CHECK(sof_lru_find(&lru, "b") != NULL);
    CHECK(order_is(&lru, (const char *[]){"b", "d", "c"}, 3));
    sof_lru_insert(&lru, "e", NULL);
    CHECK(strcmp(counts.last, "c") == 0);
    CHECK(order_is(&lru, (const char *[]){"e", "b", "d"}, 3));
    // Inserting a key already there makes it the newest, and evicts nothing.
    CHECK(sof_lru_insert(&lru, "d", &created) == d);
    CHECK(created == 0);
    CHECK(counts.deletes == 2);
    CHECK(order_is(&lru, (const char *[]){"d", "e", "b"}, 3));
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
    CHECK(counts.deletes == 5);
    CHECK(counts.frees == counts.copies);
}

static void test_peek(void) {
    sof_lru_t lru;
    CHECK(sof_lru_init(&lru, &sof_hashmap_string_keys, NULL, NULL, 3, NULL) == SOF_LRU_OK);
    sof_lru_insert(&lru, "a", NULL);
    sof_lru_insert(&lru, "b", NULL);
    sof_lru_insert(&lru, "c", NULL);
    CHECK(sof_lru_peek(&lru, "a") != NULL);
    CHECK(order_is(&lru, (const char *[]){"c", "b", "a"}, 3));
    // a is still the oldest, so it is the one evicted.
    sof_lru_insert(&lru, "d", NULL);
    CHECK(sof_lru_peek(&lru, "a") == NULL);
    CHECK(order_is(&lru, (const char *[]){"d", "c", "b"}, 3));
    // Finding the newest leaves the order as it was.
    CHECK(sof_lru_find(&lru, "d") != NULL);
    CHECK(order_is(&lru, (const char *[]){"d", "c", "b"}, 3));
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
}

static void test_stable_nodes(void) {
    // A node pointer stays good until its own entry goes, however much the
    // rest of the cache changes.
    enum { CAPACITY = 64 };
    sof_lru_t lru;
    CHECK(sof_lru_init(&lru, &weak_identity_keys, NULL, NULL, CAPACITY, NULL) == SOF_LRU_OK);
    sof_lru_node_t *nodes[CAPACITY];
    for (size_t i = 0; i < CAPACITY; i++) {
        nodes[i] = sof_lru_insert(&lru, int_key(i), NULL);
        CHECK(nodes[i] != NULL);
        nodes[i]->data = &nodes[i];
    }
    uint64_t state = 0x1234567;
    for (size_t round = 0; round < 1000; round++) {
        size_t i = next_random(&state) % CAPACITY;
        CHECK(sof_lru_find(&lru, int_key(i)) == nodes[i]);
    }
    // Remove the even keys and insert new ones in their place; the odd keys
    // keep their nodes.
    for (size_t i = 0; i < CAPACITY; i += 2) {
        CHECK(sof_lru_remove(&lru, int_key(i)) == SOF_LRU_OK);
    }
    for (size_t i = 0; i < CAPACITY / 2; i++) {
        CHECK(sof_lru_insert(&lru, int_key(CAPACITY + i), NULL) != NULL);
    }
    CHECK(lru.count == CAPACITY);
    for (size_t i = 1; i < CAPACITY; i += 2) {
        CHECK(sof_lru_peek(&lru, int_key(i)) == nodes[i]);
        CHECK(nodes[i]->data == &nodes[i]);
        CHECK(nodes[i]->key == int_key(i));
    }
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
}

static void test_remove(void) {
    counts_t counts = {0};
    sof_lru_t lru, other;
    CHECK(sof_lru_init(&lru, &counting_string_keys, string_deleted, &counts, 4, NULL) == SOF_LRU_OK);
    CHECK(sof_lru_init(&other, &sof_hashmap_string_keys, NULL, NULL, 4, NULL) == SOF_LRU_OK);
    sof_lru_insert(&lru, "a", NULL);
    sof_lru_node_t *b = sof_lru_insert(&lru, "b", NULL);
    sof_lru_node_t *c = sof_lru_insert(&lru, "c", NULL);
    sof_lru_insert(&lru, "d", NULL);
    CHECK(sof_lru_remove(&lru, "b") == SOF_LRU_OK);
    CHECK(counts.deletes == 1);
    CHECK(strcmp(counts.last, "b") == 0);
    CHECK(counts.frees == 1);
    CHECK(sof_lru_remove(&lru, "b") == SOF_LRU_NO_MATCH);
    CHECK(sof_lru_remove(&lru, "z") == SOF_LRU_NO_MATCH);
    CHECK(order_is(&lru, (const char *[]){"d", "c", "a"}, 3));
    // The key given can be the entry's own, which the cache frees.
    CHECK(sof_lru_remove(&lru, c->key) == SOF_LRU_OK);
    CHECK(order_is(&lru, (const char *[]){"d", "a"}, 2));
    // remove_node takes the newest, the oldest or one between.
    sof_lru_node_t *oldest = sof_lru_oldest(&lru);
    CHECK(sof_lru_remove_node(&lru, oldest) == SOF_LRU_OK);
    CHECK(order_is(&lru, (const char *[]){"d"}, 1));
    // A node that is no longer in use, one of another cache's, and pointers
    // that aren't nodes are refused.
    CHECK(sof_lru_remove_node(&lru, b) == SOF_LRU_ERROR);
    sof_lru_node_t *x = sof_lru_insert(&other, "x", NULL);
    CHECK(sof_lru_remove_node(&lru, x) == SOF_LRU_ERROR);
    sof_lru_node_t *d = sof_lru_newest(&lru);
    CHECK(sof_lru_remove_node(&lru, (sof_lru_node_t *)((char *)d + 1)) == SOF_LRU_ERROR);
    CHECK(sof_lru_remove_node(&lru, (sof_lru_node_t *)&lru) == SOF_LRU_ERROR);
    CHECK(sof_lru_remove_node(&lru, lru.nodes + lru.capacity) == SOF_LRU_ERROR);
    CHECK(counts.deletes == 3);
    CHECK(sof_lru_remove_node(&lru, d) == SOF_LRU_OK);
    CHECK(lru.count == 0);
    CHECK(sof_lru_newest(&lru) == NULL && sof_lru_oldest(&lru) == NULL);
    // Removed nodes are reused, so a full cache's worth of new keys evicts
    // nothing.
    insert_range(&lru, 4);
    CHECK(counts.deletes == 4);
    CHECK(lru.count == 4);
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
    CHECK(sof_lru_free(&other) == SOF_LRU_OK);
    CHECK(counts.deletes == 8);
    CHECK(counts.frees == counts.copies);
}

static void test_traverse_next(void) {
    sof_lru_t lru;
    CHECK(sof_lru_init(&lru, &sof_hashmap_string_keys, NULL, NULL, 8, NULL) == SOF_LRU_OK);
    visits_t v = {0};
    CHECK(sof_lru_traverse(&lru, count_visit, &v) == 0);
    CHECK(v.visited == 0);
    insert_range(&lru, 5);
    CHECK(order_is(&lru, (const char *[]){"k4", "k3", "k2", "k1", "k0"}, 5));
    CHECK(sof_lru_traverse(&lru, count_visit, &v) == 0);
    CHECK(v.visited == 5);
    v.visited = 0;
    v.stop_after = 2;
    CHECK(sof_lru_traverse(&lru, count_visit, &v) == 42);
    CHECK(v.visited == 2);
    CHECK(sof_lru_traverse(&lru, NULL, NULL) == 0);
    // Traversing doesn't change the order.
    CHECK(order_is(&lru, (const char *[]){"k4", "k3", "k2", "k1", "k0"}, 5));
    CHECK(sof_lru_next(&lru, sof_lru_oldest(&lru)) == NULL);
    CHECK(sof_lru_prev(&lru, sof_lru_newest(&lru)) == NULL);
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
}

static void test_clear(void) {
    counts_t counts = {0};
    sof_lru_t lru;
    CHECK(sof_lru_init(&lru, &counting_string_keys, string_deleted, &counts, 10, NULL) == SOF_LRU_OK);
    insert_range(&lru, 7);
    CHECK(sof_lru_clear(&lru) == SOF_LRU_OK);
    CHECK(counts.deletes == 7);
    CHECK(counts.frees == 7);
    CHECK(lru.count == 0);
    CHECK(sof_lru_newest(&lru) == NULL && sof_lru_oldest(&lru) == NULL);
    CHECK(sof_lru_peek(&lru, "k0") == NULL);
    // Every node is free again: ten new keys evict nothing, the eleventh
    // evicts the first.
    insert_range(&lru, 10);
    CHECK(counts.deletes == 7);
    CHECK(sof_lru_insert(&lru, "new", NULL) != NULL);
    CHECK(counts.deletes == 8);
    CHECK(strcmp(counts.last, "k0") == 0);
    CHECK(sof_lru_clear(&lru) == SOF_LRU_OK);
    CHECK(sof_lru_clear(&lru) == SOF_LRU_OK);
    CHECK(counts.deletes == 18);
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
    CHECK(counts.deletes == 18);
    CHECK(counts.frees == counts.copies);
}

static void test_capacity_one(void) {
    counts_t counts = {0};
    sof_lru_t lru;
    CHECK(sof_lru_init(&lru, NULL, key_deleted, &counts, 1, NULL) == SOF_LRU_OK);
    for (size_t i = 0; i < 10; i++) {
        sof_lru_node_t *node = sof_lru_insert(&lru, int_key(i), NULL);
        CHECK(node == &lru.nodes[0]);
        CHECK(lru.count == 1);
        CHECK(sof_lru_newest(&lru) == node && sof_lru_oldest(&lru) == node);
        CHECK(counts.deletes == i);
        if (i > 0) {
            CHECK(counts.last_key == int_key(i - 1));
            CHECK(sof_lru_peek(&lru, int_key(i - 1)) == NULL);
        }
    }
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
    CHECK(counts.deletes == 10);
}

/// @brief Runs random inserts, finds, peeks and removes on caches of several
/// sizes, checking each against a list kept in order of use.
static void random_run(const sof_hashmap_key_callbacks_t *keys) {
    static const size_t capacities[] = {1, 2, 7, 32, 99};
    uint64_t state = 0x9e3779b97f4a7c15ULL;
    for (size_t c = 0; c < sizeof(capacities) / sizeof(capacities[0]); c++) {
        size_t capacity = capacities[c];
        counts_t counts = {0};
        sof_lru_t lru;
        CHECK(sof_lru_init(&lru, keys, key_deleted, &counts, capacity, NULL) == SOF_LRU_OK);
        // ref[0] is the newest key, ref[n - 1] the oldest.
        void *ref[UNIVERSE];
        size_t n = 0;
        size_t expected_deletes = 0;
        for (size_t round = 0; round < 20000; round++) {
            void *key = int_key(next_random(&state) % UNIVERSE);
            size_t at = 0;
            while (at < n && ref[at] != key) {
                at++;
            }
            int found = at < n;
            uint64_t op = next_random(&state) % 8;
            if (op < 3) {
                int created = -1;
                sof_lru_node_t *node = sof_lru_insert(&lru, key, &created);
                CHECK(node != NULL && node->key == key);
                CHECK(created == !found);
                if (!found && n == capacity) {
                    // The oldest is evicted.
                    expected_deletes++;
                    CHECK(counts.last_key == ref[n - 1]);
                    n--;
                }
                if (!found) {
                    at = n++;
                }
                memmove(&ref[1], &ref[0], at * sizeof(void *));
                ref[0] = key;
            } else if (op < 5) {
                sof_lru_node_t *node = sof_lru_find(&lru, key);
                CHECK((node != NULL) == found);
                if (found) {
                    CHECK(node->key == key);
                    memmove(&ref[1], &ref[0], at * sizeof(void *));
                    ref[0] = key;
                }
            } else if (op < 6) {
                sof_lru_node_t *node = sof_lru_peek(&lru, key);
                CHECK((node != NULL) == found);
            } else if (op < 7) {
                CHECK(sof_lru_remove(&lru, key) == (found ? SOF_LRU_OK : SOF_LRU_NO_MATCH));
                if (found) {
                    expected_deletes++;
                    CHECK(counts.last_key == key);
                    memmove(&ref[at], &ref[at + 1], (n - at - 1) * sizeof(void *));
                    n--;
                }
            } else if (n > 0) {
                // Remove the oldest, as a caller trimming the cache would.
                CHECK(sof_lru_remove_node(&lru, sof_lru_oldest(&lru)) == SOF_LRU_OK);
                expected_deletes++;
                CHECK(counts.last_key == ref[n - 1]);
                n--;
            }
            CHECK(counts.deletes == expected_deletes);
            CHECK(lru.count == n);
            size_t i = 0;
            for (sof_lru_node_t *node = sof_lru_newest(&lru); node != NULL; node = sof_lru_next(&lru, node)) {
                CHECK(i < n && node->key == ref[i]);
                i++;
            }
            CHECK(i == n);
            for (sof_lru_node_t *node = sof_lru_oldest(&lru); node != NULL; node = sof_lru_prev(&lru, node)) {
                CHECK(i > 0 && node->key == ref[i - 1]);
                i--;
            }
            for (size_t k = 0; k < n; k++) {
                CHECK(sof_lru_peek(&lru, ref[k]) != NULL);
            }
        }
        CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
        CHECK(counts.deletes == expected_deletes + n);
    }
}

static void test_random(void) {
    random_run(NULL);
}

static void test_random_collisions(void) {
    random_run(&weak_identity_keys);
}

/// @brief Counts hash calls, key copies and key frees for
/// test_stored_hashes().
typedef struct hash_counts_t {
    size_t hashes;
    size_t copies;
    size_t frees;
    int weak;
} hash_counts_t;

/// @brief A string hash that counts its calls, and gives only three values
/// if weak is set.
static size_t counted_hash(const void *key, void *ctx) {
    hash_counts_t *counts = ctx;
    counts->hashes++;
    size_t h = sof_hashmap_string_hash(key, NULL);
    return counts->weak ? h % 3 : h;
}

static void *counted_copy(const void *key, const sof_alloc_t *alloc, void *ctx) {
    ((hash_counts_t *)ctx)->copies++;
    return sof_hashmap_string_keys.copy(key, alloc, NULL);
}

static void counted_free(void *key, const sof_alloc_t *alloc, void *ctx) {
    ((hash_counts_t *)ctx)->frees++;
    sof_hashmap_string_keys.free(key, alloc, NULL);
}

static void test_stored_hashes(void) {
    // Each node keeps its key's hash, so evicting it and removing it by node
    // or by key hash nothing but the key given, with good hashes and with
    // colliding ones that make evictions move elements in the map.
    static const sof_hashmap_key_callbacks_t keys = {counted_hash, sof_hashmap_string_equal, counted_copy, counted_free};
    for (int weak = 0; weak < 2; weak++) {
        hash_counts_t counts = {0, 0, 0, weak};
        sof_lru_t lru;
        char buf[32];
        size_t expected = 0;
        uint64_t state = 0x1234;
        CHECK(sof_lru_init(&lru, &keys, NULL, &counts, 7, NULL) == SOF_LRU_OK);
        for (size_t i = 0; i < 2000; i++) {
            snprintf(buf, sizeof(buf), "key%zu", i);
            sof_lru_node_t *node = sof_lru_insert(&lru, buf, NULL);
            expected++;
            CHECK(node != NULL && strcmp(node->key, buf) == 0);
            uint64_t op = next_random(&state) % 8;
            if (op == 0) {
                CHECK(sof_lru_remove(&lru, buf) == SOF_LRU_OK);
                expected++;
            } else if (op == 1) {
                CHECK(sof_lru_remove_node(&lru, sof_lru_oldest(&lru)) == SOF_LRU_OK);
            }
            CHECK(counts.hashes == expected);
            // Every node's hash is its map element's, and the map holds
            // exactly the cache's keys.
            CHECK(lru.map.count == lru.count);
            size_t position = 0;
            sof_hashmap_element_t *e;
            while ((e = sof_hashmap_next(&lru.map, &position)) != NULL) {
                sof_lru_node_t *n = e->data;
                CHECK(n->key == e->key && n->hash == e->hash);
            }
        }
        CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
        CHECK(counts.copies == 2000 && counts.frees == 2000);
    }
}

static void test_null_and_freed(void) {
    CHECK(sof_lru_init(NULL, NULL, NULL, NULL, 4, NULL) == SOF_LRU_ERROR);
    CHECK(sof_lru_clear(NULL) == SOF_LRU_ERROR);
    CHECK(sof_lru_free(NULL) == SOF_LRU_ERROR);
    int created = -1;
    CHECK(sof_lru_insert(NULL, "a", &created) == NULL);
    CHECK(created == 0);
    CHECK(sof_lru_find(NULL, "a") == NULL);
    CHECK(sof_lru_peek(NULL, "a") == NULL);
    CHECK(sof_lru_newest(NULL) == NULL);
    CHECK(sof_lru_oldest(NULL) == NULL);
    CHECK(sof_lru_next(NULL, NULL) == NULL);
    CHECK(sof_lru_prev(NULL, NULL) == NULL);
    CHECK(sof_lru_remove(NULL, "a") == SOF_LRU_ERROR);
    CHECK(sof_lru_remove_node(NULL, NULL) == SOF_LRU_ERROR);
    CHECK(sof_lru_traverse(NULL, count_visit, NULL) == -1);

    // NULL keys and nodes in a working cache.
    sof_lru_t lru;
    CHECK(sof_lru_init(&lru, &sof_hashmap_string_keys, NULL, NULL, 4, NULL) == SOF_LRU_OK);
    sof_lru_node_t *a = sof_lru_insert(&lru, "a", NULL);
    created = -1;
    CHECK(sof_lru_insert(&lru, NULL, &created) == NULL);
    CHECK(created == 0);
    CHECK(sof_lru_find(&lru, NULL) == NULL);
    CHECK(sof_lru_peek(&lru, NULL) == NULL);
    CHECK(sof_lru_remove(&lru, NULL) == SOF_LRU_ERROR);
    CHECK(sof_lru_remove_node(&lru, NULL) == SOF_LRU_ERROR);
    CHECK(sof_lru_next(&lru, NULL) == NULL);
    CHECK(sof_lru_prev(&lru, NULL) == NULL);
    CHECK(lru.count == 1);

    // A freed cache is empty, and stays that way until initialized again.
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
    CHECK(lru.nodes == NULL);
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
    CHECK(sof_lru_clear(&lru) == SOF_LRU_OK);
    CHECK(sof_lru_insert(&lru, "a", &created) == NULL);
    CHECK(created == 0);
    CHECK(sof_lru_find(&lru, "a") == NULL);
    CHECK(sof_lru_peek(&lru, "a") == NULL);
    CHECK(sof_lru_newest(&lru) == NULL);
    CHECK(sof_lru_oldest(&lru) == NULL);
    CHECK(sof_lru_remove(&lru, "a") == SOF_LRU_NO_MATCH);
    CHECK(sof_lru_remove_node(&lru, a) == SOF_LRU_ERROR);
    CHECK(sof_lru_traverse(&lru, count_visit, NULL) == 0);
    CHECK(lru.count == 0);

    // A cache whose init failed acts the same.
    CHECK(sof_lru_init(&lru, &sof_hashmap_string_keys, NULL, NULL, 0, NULL) == SOF_LRU_ERROR);
    CHECK(lru.nodes == NULL);
    CHECK(sof_lru_insert(&lru, "a", NULL) == NULL);
    CHECK(sof_lru_find(&lru, "a") == NULL);
    CHECK(sof_lru_remove(&lru, "a") == SOF_LRU_NO_MATCH);
    CHECK(sof_lru_traverse(&lru, count_visit, NULL) == 0);
    CHECK(sof_lru_clear(&lru) == SOF_LRU_OK);
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);

    // And it can be initialized again.
    CHECK(sof_lru_init(&lru, &sof_hashmap_string_keys, NULL, NULL, 2, NULL) == SOF_LRU_OK);
    CHECK(sof_lru_insert(&lru, "a", NULL) != NULL);
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
}

static void test_bad_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    sof_lru_t lru;
    CHECK(sof_lru_init(&lru, &sof_hashmap_string_keys, NULL, NULL, 4, &alloc) == SOF_LRU_ERROR);
    CHECK(lru.nodes == NULL);
    CHECK(state.allocations == 0);
}

static void test_allocations(void) {
    // init allocates the nodes and the map's array; after that, inserts
    // allocate only key copies, however many keys are evicted.
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_lru_t lru;
    CHECK(sof_lru_init(&lru, &sof_hashmap_string_keys, NULL, NULL, 100, &alloc) == SOF_LRU_OK);
    CHECK(state.allocations == 2);
    insert_range(&lru, 1000);
    CHECK(state.allocations == 2 + 1000);
    CHECK(state.live == 2 + 100);
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_no_memory(void) {
    // Fail each allocation in turn, through init and inserts that copy keys
    // and evict, until a run makes none fail. A failed insert leaves the
    // cache as it was.
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        counts_t counts = {0};
        sof_lru_t lru;
        sof_lru_error_t ri = sof_lru_init(&lru, &counting_string_keys, string_deleted, &counts, 8, &alloc);
        CHECK(ri == SOF_LRU_OK || ri == SOF_LRU_NO_MEM);
        int failed = ri != SOF_LRU_OK;
        char buffer[32];
        for (size_t i = 0; i < 20; i++) {
            snprintf(buffer, sizeof(buffer), "k%zu", i);
            size_t count = lru.count;
            size_t deletes = counts.deletes;
            sof_lru_node_t *oldest = sof_lru_oldest(&lru);
            sof_lru_node_t *node = sof_lru_insert(&lru, buffer, NULL);
            if (node == NULL) {
                failed = 1;
                CHECK(lru.count == count);
                CHECK(counts.deletes == deletes);
                CHECK(sof_lru_oldest(&lru) == oldest);
                CHECK(sof_lru_peek(&lru, buffer) == NULL);
            } else {
                CHECK(strcmp(node->key, buffer) == 0);
                CHECK(sof_lru_newest(&lru) == node);
            }
        }
        CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
        CHECK(counts.frees == counts.copies);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            CHECK(!failed);
            break;
        }
    }
}

/// @brief The functions that only read take a const cache and const nodes,
/// and hand back entries whose data can still be changed.
static void test_const_cache(void) {
    sof_lru_t lru;
    visits_t visits = {0};
    CHECK(sof_lru_init(&lru, &sof_hashmap_string_keys, NULL, NULL, 3, NULL) == SOF_LRU_OK);
    sof_lru_insert(&lru, "a", NULL);
    sof_lru_insert(&lru, "b", NULL);
    sof_lru_insert(&lru, "c", NULL);
    const sof_lru_t *clru = &lru;
    sof_lru_node_t *a = sof_lru_peek(clru, "a");
    CHECK(a != NULL && strcmp(a->key, "a") == 0);
    a->data = &visits;
    CHECK(sof_lru_peek(clru, "a")->data == &visits);
    CHECK(sof_lru_peek(clru, "d") == NULL);
    CHECK(sof_lru_oldest(clru) == a);
    const sof_lru_node_t *newest = sof_lru_newest(clru);
    CHECK(newest != NULL && strcmp(newest->key, "c") == 0);
    CHECK(strcmp(sof_lru_next(clru, newest)->key, "b") == 0);
    CHECK(sof_lru_prev(clru, newest) == NULL);
    CHECK(order_is(clru, (const char *[]){"c", "b", "a"}, 3));
    CHECK(sof_lru_traverse(clru, count_visit, &visits) == 0 && visits.visited == 3);
    CHECK(sof_lru_free(&lru) == SOF_LRU_OK);
}

int main(void) {
    printf("sof_lru:\n");
    run("insert, find and peek", test_insert_find);
    run("eviction of the least recently used", test_eviction);
    run("peek leaves the order", test_peek);
    run("node pointers stay good", test_stable_nodes);
    run("remove and remove_node", test_remove);
    run("traverse, next and prev", test_traverse_next);
    run("read through a const cache", test_const_cache);
    run("clear", test_clear);
    run("capacity of one", test_capacity_one);
    run("random operations against a reference", test_random);
    run("random operations with colliding hashes", test_random_collisions);
    run("stored hashes: evicting and removing don't hash again", test_stored_hashes);
    run("NULL and freed caches", test_null_and_freed);
    run("bad allocator", test_bad_allocator);
    run("allocations", test_allocations);
    run("failing allocations", test_no_memory);
    return test_report();
}
