/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_vector.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sof_vector.h"
#include "test.h"

/// @brief Readers, and the writer's changes, in the threaded test.
#define READERS 4
#define ITEMS 5000
#define PASSES 500

/// @brief The size of the plain array test_random() checks the vector
/// against.
#define MODEL_SIZE 600

/// @brief The ctx of the callbacks: what they count, and the order of the
/// elements deleted.
typedef struct counts_t {
    size_t compares;
    size_t deletes;
    /// @brief The sum of the elements deleted.
    uint64_t deleted_sum;
    /// @brief The first elements deleted, in order.
    uint64_t deleted[8];
    size_t visits;
    /// @brief The number of visits after which the traversal stops, or 0.
    size_t stop_after;
} counts_t;

/// @brief An element of an odd size, to check that nothing assumes alignment
/// or a power of two.
typedef struct triple_t {
    unsigned char bytes[3];
} triple_t;

/// @brief An element for the stable sort: ordered by key only, with seq
/// recording where it started.
typedef struct pair_t {
    uint64_t key;
    uint64_t seq;
} pair_t;

/// @brief Reads a uint64_t element, which may be unaligned.
static uint64_t value(const void *element) {
    uint64_t v;
    memcpy(&v, element, sizeof(v));
    return v;
}

/// @brief Orders uint64_t elements, counting the calls if ctx isn't NULL.
static int compare(const void *a, const void *b, void *ctx) {
    counts_t *counts = ctx;
    if (counts != NULL) {
        counts->compares++;
    }
    uint64_t x = value(a), y = value(b);
    return x < y ? -1 : x > y ? 1 : 0;
}

static int compare_pair(const void *a, const void *b, void *ctx) {
    const pair_t *x = a, *y = b;
    return x->key < y->key ? -1 : x->key > y->key ? 1 : 0;
}

static void count_delete(void *element, void *ctx) {
    counts_t *counts = ctx;
    if (counts->deletes < sizeof(counts->deleted) / sizeof(counts->deleted[0])) {
        counts->deleted[counts->deletes] = value(element);
    }
    counts->deletes++;
    counts->deleted_sum += value(element);
}

static int count_visit(void *element, void *ctx) {
    counts_t *counts = ctx;
    counts->visits++;
    if (counts->stop_after != 0 && counts->visits == counts->stop_after) {
        return 42;
    }
    return 0;
}

/// @brief Doubles each element, for sof_vector_traverse().
static int double_it(void *element, void *ctx) {
    uint64_t v = value(element) * 2;
    memcpy(element, &v, sizeof(v));
    return 0;
}

/// @brief Pushes the values from to to - 1.
static int push_range(sof_vector_t *v, uint64_t from, uint64_t to) {
    for (uint64_t i = from; i < to; i++) {
        if (sof_vector_push(v, &i) != SOF_VECTOR_OK) {
            return 0;
        }
    }
    return 1;
}

/// @brief Returns 1 if element i of v is want, for each i, or 0.
static int holds(sof_vector_t *v, const uint64_t *want, size_t count) {
    if (v->count != count) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        if (value(sof_vector_at(v, i)) != want[i]) {
            return 0;
        }
    }
    return 1;
}

/// @brief Returns 1 if v's elements are in order by compare().
static int is_sorted(sof_vector_t *v) {
    for (size_t i = 1; i < v->count; i++) {
        if (value(sof_vector_at(v, i - 1)) > value(sof_vector_at(v, i))) {
            return 0;
        }
    }
    return 1;
}

/// @brief Checks that v is empty and acts as not initialized.
static void check_dead(sof_vector_t *v) {
    uint64_t x = 1;
    CHECK(v->ready == 0);
    CHECK(v->elements == NULL);
    CHECK(v->count == 0);
    CHECK(v->capacity == 0);
    CHECK(sof_vector_at(v, 0) == NULL);
    CHECK(sof_vector_push(v, &x) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_insert(v, 0, &x) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_pop(v, &x) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_remove(v, 0, NULL) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_swap_remove(v, 0, NULL) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_reserve(v, 10) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_resize(v, 10) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_shrink(v) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_sort(v, compare, NULL) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_sort_stable(v, compare, NULL) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_find(v, &x, compare, NULL, NULL) == NULL);
    CHECK(sof_vector_search(v, &x, compare, NULL, NULL) == NULL);
    CHECK(sof_vector_next(v, &x) == NULL);
    CHECK(sof_vector_traverse(v, count_visit, NULL) == -1);
    CHECK(sof_vector_clear(v) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_free(v) == SOF_VECTOR_ERROR);
    CHECK(x == 1);
    CHECK(v->ready == 0);
}

static void test_init(void) {
    sof_vector_t v;
    int ctx;
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, count_delete, &ctx, NULL) == SOF_VECTOR_OK);
    CHECK(v.ready == 1);
    CHECK(v.elements == NULL);
    CHECK(v.element_size == sizeof(uint64_t));
    CHECK(v.count == 0);
    CHECK(v.capacity == 0);
    CHECK(v.del_func == count_delete);
    CHECK(v.ctx == &ctx);
    CHECK(v.alloc.malloc == NULL);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);

    CHECK(sof_vector_init(&v, sizeof(uint64_t), 100, NULL, NULL, NULL) == SOF_VECTOR_OK);
    CHECK(v.elements != NULL);
    CHECK(v.capacity == 100);
    CHECK(v.count == 0);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_bad_init(void) {
    sof_vector_t v;
    CHECK(sof_vector_init(NULL, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_ERROR);

    memset(&v, 0xff, sizeof(v));
    CHECK(sof_vector_init(&v, 0, 0, NULL, NULL, NULL) == SOF_VECTOR_ERROR);
    check_dead(&v);

    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    memset(&v, 0xff, sizeof(v));
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, &alloc) == SOF_VECTOR_ERROR);
    check_dead(&v);

    // Room for more elements than memory can hold.
    memset(&v, 0xff, sizeof(v));
    CHECK(sof_vector_init(&v, sizeof(uint64_t), SIZE_MAX / 4, NULL, NULL, NULL) == SOF_VECTOR_NO_MEM);
    check_dead(&v);
}

static void test_empty(void) {
    sof_vector_t v;
    uint64_t x = 7;
    size_t index = 99;
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    CHECK(sof_vector_at(&v, 0) == NULL);
    CHECK(sof_vector_pop(&v, &x) == SOF_VECTOR_NO_MATCH);
    CHECK(x == 7);
    CHECK(sof_vector_remove(&v, 0, &x) == SOF_VECTOR_NO_MATCH);
    CHECK(sof_vector_swap_remove(&v, 0, &x) == SOF_VECTOR_NO_MATCH);
    CHECK(x == 7);
    CHECK(sof_vector_find(&v, &x, compare, NULL, &index) == NULL);
    CHECK(index == 99);
    CHECK(sof_vector_search(&v, &x, compare, NULL, &index) == NULL);
    CHECK(index == 0);
    CHECK(sof_vector_next(&v, &x) == NULL);
    CHECK(sof_vector_traverse(&v, count_visit, NULL) == 0);
    CHECK(sof_vector_sort(&v, compare, NULL) == SOF_VECTOR_OK);
    CHECK(sof_vector_sort_stable(&v, compare, NULL) == SOF_VECTOR_OK);
    CHECK(sof_vector_shrink(&v) == SOF_VECTOR_OK);
    CHECK(sof_vector_resize(&v, 0) == SOF_VECTOR_OK);
    CHECK(sof_vector_clear(&v) == SOF_VECTOR_OK);
    CHECK(v.elements == NULL);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_push_at(void) {
    sof_vector_t v;
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 1000));
    CHECK(v.count == 1000);
    for (uint64_t i = 0; i < 1000; i++) {
        CHECK(value(sof_vector_at(&v, i)) == i);
    }
    CHECK(sof_vector_at(&v, 1000) == NULL);
    CHECK(sof_vector_at(&v, SIZE_MAX) == NULL);
    CHECK(sof_vector_push(&v, NULL) == SOF_VECTOR_ERROR);
    CHECK(v.count == 1000);

    // Elements can be changed through at().
    uint64_t x = 12345;
    memcpy(sof_vector_at(&v, 500), &x, sizeof(x));
    CHECK(value(sof_vector_at(&v, 500)) == 12345);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_growth(void) {
    sof_vector_t v;
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    uint64_t x = 0;
    CHECK(sof_vector_push(&v, &x) == SOF_VECTOR_OK);
    CHECK(v.capacity == 16);
    CHECK(push_range(&v, 1, 16));
    CHECK(v.capacity == 16);
    CHECK(push_range(&v, 16, 17));
    CHECK(v.capacity == 32);
    CHECK(push_range(&v, 17, 33));
    CHECK(v.capacity == 64);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);

    // A vector given a capacity doubles from there.
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 3, NULL, NULL, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 4));
    CHECK(v.capacity == 6);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_pop(void) {
    sof_vector_t v;
    counts_t counts = {0};
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, count_delete, &counts, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 10));
    size_t capacity = v.capacity;
    uint64_t x;
    CHECK(sof_vector_pop(&v, &x) == SOF_VECTOR_OK);
    CHECK(x == 9);
    CHECK(counts.deletes == 0);
    CHECK(sof_vector_pop(&v, NULL) == SOF_VECTOR_OK);
    CHECK(counts.deletes == 1);
    CHECK(counts.deleted[0] == 8);
    CHECK(v.count == 8);
    while (sof_vector_pop(&v, &x) == SOF_VECTOR_OK) {
    }
    CHECK(x == 0);
    CHECK(v.count == 0);
    CHECK(v.capacity == capacity);
    CHECK(counts.deletes == 1);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_insert(void) {
    sof_vector_t v;
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    uint64_t a = 1, b = 2, c = 3, d = 4;
    CHECK(sof_vector_insert(&v, 1, &a) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_insert(&v, 0, &c) == SOF_VECTOR_OK); // 3
    CHECK(sof_vector_insert(&v, 0, &a) == SOF_VECTOR_OK); // 1 3
    CHECK(sof_vector_insert(&v, 1, &b) == SOF_VECTOR_OK); // 1 2 3
    CHECK(sof_vector_insert(&v, 3, &d) == SOF_VECTOR_OK); // 1 2 3 4
    CHECK(sof_vector_insert(&v, 5, &d) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_insert(&v, 0, NULL) == SOF_VECTOR_ERROR);
    uint64_t want[] = {1, 2, 3, 4};
    CHECK(holds(&v, want, 4));

    // Inserting at the front of a full array.
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 4, NULL, NULL, NULL) == SOF_VECTOR_OK);
    for (uint64_t i = 0; i < 100; i++) {
        CHECK(sof_vector_insert(&v, 0, &i) == SOF_VECTOR_OK);
    }
    for (uint64_t i = 0; i < 100; i++) {
        CHECK(value(sof_vector_at(&v, i)) == 99 - i);
    }
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_own_element(void) {
    // Inserting or pushing a copy of an element of the vector itself, with
    // and without the array growing, before and after the element.
    for (int full = 0; full < 2; full++) {
        for (size_t index = 0; index <= 4; index++) {
            for (size_t source = 0; source < 4; source++) {
                sof_vector_t v;
                CHECK(sof_vector_init(&v, sizeof(uint64_t), full ? 4 : 8, NULL, NULL, NULL) == SOF_VECTOR_OK);
                CHECK(push_range(&v, 10, 14));
                CHECK(sof_vector_insert(&v, index, sof_vector_at(&v, source)) == SOF_VECTOR_OK);
                uint64_t want[5], *w = want;
                for (size_t i = 0; i < 4; i++) {
                    if (i == index) {
                        *w++ = 10 + source;
                    }
                    *w++ = 10 + i;
                }
                if (index == 4) {
                    *w++ = 10 + source;
                }
                CHECK(holds(&v, want, 5));
                CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
            }
        }
    }

    sof_vector_t v;
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 4, NULL, NULL, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 4));
    CHECK(sof_vector_push(&v, sof_vector_at(&v, 1)) == SOF_VECTOR_OK);
    uint64_t want[] = {0, 1, 2, 3, 1};
    CHECK(holds(&v, want, 5));

    // With room, pushing the last element copies it into the next slot.
    CHECK(v.capacity > v.count);
    CHECK(sof_vector_push(&v, sof_vector_at(&v, 4)) == SOF_VECTOR_OK);
    CHECK(sof_vector_push(&v, sof_vector_at(&v, 0)) == SOF_VECTOR_OK);
    uint64_t want2[] = {0, 1, 2, 3, 1, 1, 0};
    CHECK(holds(&v, want2, 7));
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_push_sizes(void) {
    // Pushing, which copies straight into the next slot when there is room,
    // gives the same array as inserting at the end, for element sizes around
    // the word sizes and across the pushes that grow the array.
    uint64_t state = 0x2545f4914f6cdd1dULL;
    unsigned char element[40];
    for (size_t size = 1; size <= sizeof(element); size++) {
        for (size_t start = 0; start <= 5; start += 5) {
            sof_vector_t pushed, inserted;
            CHECK(sof_vector_init(&pushed, size, start, NULL, NULL, NULL) == SOF_VECTOR_OK);
            CHECK(sof_vector_init(&inserted, size, start, NULL, NULL, NULL) == SOF_VECTOR_OK);
            for (size_t n = 0; n < 100; n++) {
                for (size_t i = 0; i < size; i++) {
                    element[i] = (unsigned char)next_random(&state);
                }
                size_t room = pushed.capacity - pushed.count;
                CHECK(sof_vector_push(&pushed, element) == SOF_VECTOR_OK);
                CHECK(sof_vector_insert(&inserted, inserted.count, element) == SOF_VECTOR_OK);
                CHECK(room == 0 || pushed.capacity == pushed.count + room - 1);
                CHECK(memcmp(sof_vector_at(&pushed, n), element, size) == 0);
            }
            CHECK(pushed.count == 100);
            CHECK(pushed.capacity == inserted.capacity);
            CHECK(memcmp(pushed.elements, inserted.elements, 100 * size) == 0);
            CHECK(sof_vector_free(&pushed) == SOF_VECTOR_OK);
            CHECK(sof_vector_free(&inserted) == SOF_VECTOR_OK);
        }
    }
}

static void test_odd_size(void) {
    sof_vector_t v;
    CHECK(sof_vector_init(&v, sizeof(triple_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    CHECK(v.element_size == 3);
    for (int i = 0; i < 200; i++) {
        triple_t t = {{(unsigned char)i, (unsigned char)(i * 7), (unsigned char)(255 - i)}};
        CHECK(sof_vector_insert(&v, (size_t)i / 2, &t) == SOF_VECTOR_OK);
    }
    CHECK(sof_vector_remove(&v, 50, NULL) == SOF_VECTOR_OK);
    CHECK(sof_vector_swap_remove(&v, 10, NULL) == SOF_VECTOR_OK);
    CHECK(v.count == 198);
    // Every element is still whole.
    for (size_t i = 0; i < v.count; i++) {
        const triple_t *t = sof_vector_at(&v, i);
        CHECK(t->bytes[1] == (unsigned char)(t->bytes[0] * 7));
        CHECK(t->bytes[2] == (unsigned char)(255 - t->bytes[0]));
    }
    // A pointer into the middle of an element isn't an element.
    CHECK(sof_vector_next(&v, (unsigned char *)sof_vector_at(&v, 0) + 1) == NULL);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_remove(void) {
    sof_vector_t v;
    counts_t counts = {0};
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, count_delete, &counts, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 6));
    uint64_t x = 99;
    CHECK(sof_vector_remove(&v, 6, &x) == SOF_VECTOR_NO_MATCH);
    CHECK(x == 99);
    CHECK(sof_vector_remove(&v, 2, &x) == SOF_VECTOR_OK);
    CHECK(x == 2);
    CHECK(counts.deletes == 0);
    uint64_t want1[] = {0, 1, 3, 4, 5};
    CHECK(holds(&v, want1, 5));
    CHECK(sof_vector_remove(&v, 0, NULL) == SOF_VECTOR_OK);
    CHECK(counts.deletes == 1);
    CHECK(counts.deleted[0] == 0);
    CHECK(sof_vector_remove(&v, 3, NULL) == SOF_VECTOR_OK);
    CHECK(counts.deleted[1] == 5);
    uint64_t want2[] = {1, 3, 4};
    CHECK(holds(&v, want2, 3));
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_swap_remove(void) {
    sof_vector_t v;
    counts_t counts = {0};
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, count_delete, &counts, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 6));
    uint64_t x = 99;
    CHECK(sof_vector_swap_remove(&v, 6, &x) == SOF_VECTOR_NO_MATCH);
    CHECK(x == 99);
    CHECK(sof_vector_swap_remove(&v, 1, &x) == SOF_VECTOR_OK);
    CHECK(x == 1);
    uint64_t want1[] = {0, 5, 2, 3, 4};
    CHECK(holds(&v, want1, 5));
    CHECK(sof_vector_swap_remove(&v, 4, NULL) == SOF_VECTOR_OK);
    CHECK(counts.deletes == 1);
    CHECK(counts.deleted[0] == 4);
    uint64_t want2[] = {0, 5, 2, 3};
    CHECK(holds(&v, want2, 4));
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_reserve(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_vector_t v;
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, &alloc) == SOF_VECTOR_OK);
    CHECK(sof_vector_reserve(&v, 0) == SOF_VECTOR_OK);
    CHECK(v.elements == NULL);
    CHECK(sof_vector_reserve(&v, 1000) == SOF_VECTOR_OK);
    CHECK(v.capacity == 1000);
    size_t allocations = state.allocations;
    CHECK(push_range(&v, 0, 1000));
    CHECK(state.allocations == allocations);
    CHECK(sof_vector_reserve(&v, 10) == SOF_VECTOR_OK);
    CHECK(v.capacity == 1000);
    CHECK(sof_vector_reserve(&v, SIZE_MAX) == SOF_VECTOR_NO_MEM);
    CHECK(v.capacity == 1000);
    CHECK(v.count == 1000);
    CHECK(value(sof_vector_at(&v, 999)) == 999);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_resize(void) {
    sof_vector_t v;
    counts_t counts = {0};
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, count_delete, &counts, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 1, 4));
    CHECK(sof_vector_resize(&v, 40) == SOF_VECTOR_OK);
    CHECK(v.count == 40);
    CHECK(v.capacity >= 40);
    CHECK(value(sof_vector_at(&v, 2)) == 3);
    for (size_t i = 3; i < 40; i++) {
        CHECK(value(sof_vector_at(&v, i)) == 0);
    }
    // Shrinking drops from the end, first to last, and keeps the array.
    for (uint64_t i = 0; i < 40; i++) {
        memcpy(sof_vector_at(&v, i), &i, sizeof(i));
    }
    size_t capacity = v.capacity;
    CHECK(sof_vector_resize(&v, 35) == SOF_VECTOR_OK);
    CHECK(v.count == 35);
    CHECK(v.capacity == capacity);
    CHECK(counts.deletes == 5);
    for (size_t i = 0; i < 5; i++) {
        CHECK(counts.deleted[i] == 35 + i);
    }
    CHECK(sof_vector_resize(&v, 35) == SOF_VECTOR_OK);
    CHECK(counts.deletes == 5);
    CHECK(sof_vector_resize(&v, SIZE_MAX) == SOF_VECTOR_NO_MEM);
    CHECK(v.count == 35);
    CHECK(sof_vector_resize(&v, 0) == SOF_VECTOR_OK);
    CHECK(counts.deletes == 40);
    CHECK(counts.deleted_sum == 40 * 39 / 2);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
    CHECK(counts.deletes == 40);
}

static void test_shrink(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_vector_t v;
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, &alloc) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 20));
    CHECK(v.capacity == 32);
    CHECK(sof_vector_shrink(&v) == SOF_VECTOR_OK);
    CHECK(v.capacity == 20);
    CHECK(value(sof_vector_at(&v, 19)) == 19);
    size_t allocations = state.allocations;
    CHECK(sof_vector_shrink(&v) == SOF_VECTOR_OK);
    CHECK(state.allocations == allocations);
    CHECK(sof_vector_clear(&v) == SOF_VECTOR_OK);
    CHECK(v.capacity == 20);
    CHECK(sof_vector_shrink(&v) == SOF_VECTOR_OK);
    CHECK(v.capacity == 0);
    CHECK(v.elements == NULL);
    CHECK(state.live == 0);
    // It can still be used.
    CHECK(push_range(&v, 0, 3));
    CHECK(value(sof_vector_at(&v, 2)) == 2);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_find(void) {
    sof_vector_t v;
    counts_t counts = {0};
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    uint64_t values[] = {5, 3, 9, 3, 7};
    for (size_t i = 0; i < 5; i++) {
        CHECK(sof_vector_push(&v, &values[i]) == SOF_VECTOR_OK);
    }
    uint64_t key = 3;
    size_t index = 99;
    CHECK(sof_vector_find(&v, &key, compare, &counts, &index) == sof_vector_at(&v, 1));
    CHECK(index == 1);
    CHECK(counts.compares == 2);
    CHECK(sof_vector_find(&v, &key, compare, NULL, NULL) == sof_vector_at(&v, 1));
    key = 7;
    CHECK(sof_vector_find(&v, &key, compare, NULL, &index) == sof_vector_at(&v, 4));
    CHECK(index == 4);
    key = 4;
    index = 99;
    counts.compares = 0;
    CHECK(sof_vector_find(&v, &key, compare, &counts, &index) == NULL);
    CHECK(index == 99);
    CHECK(counts.compares == 5);
    CHECK(sof_vector_find(&v, NULL, compare, NULL, &index) == NULL);
    CHECK(sof_vector_find(&v, &key, NULL, NULL, &index) == NULL);
    CHECK(index == 99);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_search(void) {
    sof_vector_t v;
    counts_t counts = {0};
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    // 0, 2, 4, ... 198, with 100 three times.
    for (uint64_t i = 0; i < 100; i++) {
        uint64_t x = i * 2;
        CHECK(sof_vector_push(&v, &x) == SOF_VECTOR_OK);
        if (x == 100) {
            CHECK(sof_vector_push(&v, &x) == SOF_VECTOR_OK);
            CHECK(sof_vector_push(&v, &x) == SOF_VECTOR_OK);
        }
    }
    size_t index = 99;
    uint64_t key = 100;
    CHECK(sof_vector_search(&v, &key, compare, &counts, &index) == sof_vector_at(&v, 50));
    CHECK(index == 50);
    CHECK(counts.compares <= 9);
    for (uint64_t i = 0; i < 100; i++) {
        key = i * 2;
        CHECK(sof_vector_search(&v, &key, compare, NULL, &index) != NULL);
        CHECK(value(sof_vector_at(&v, index)) == key);
        key = i * 2 + 1;
        CHECK(sof_vector_search(&v, &key, compare, NULL, &index) == NULL);
        // Where key would go to keep the vector sorted.
        CHECK(index == v.count || value(sof_vector_at(&v, index)) > key);
        CHECK(index == 0 || value(sof_vector_at(&v, index - 1)) < key);
    }
    key = 1000;
    CHECK(sof_vector_search(&v, &key, compare, NULL, &index) == NULL);
    CHECK(index == v.count);
    CHECK(sof_vector_search(&v, &key, compare, NULL, NULL) == NULL);
    key = 0;
    CHECK(sof_vector_search(&v, &key, compare, NULL, NULL) == sof_vector_at(&v, 0));

    // A sorted insert: search for where, then insert there.
    key = 55;
    CHECK(sof_vector_search(&v, &key, compare, NULL, &index) == NULL);
    CHECK(sof_vector_insert(&v, index, &key) == SOF_VECTOR_OK);
    CHECK(is_sorted(&v));
    CHECK(sof_vector_search(&v, &key, compare, NULL, NULL) != NULL);

    index = 99;
    CHECK(sof_vector_search(&v, NULL, compare, NULL, &index) == NULL);
    CHECK(sof_vector_search(&v, &key, NULL, NULL, &index) == NULL);
    CHECK(index == 99);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_sort(void) {
    sof_vector_t v;
    counts_t counts = {0};
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    uint64_t random = 0x9e3779b97f4a7c15ULL, total = 0;
    for (int i = 0; i < 5000; i++) {
        uint64_t x = next_random(&random) % 1000;
        total += x;
        CHECK(sof_vector_push(&v, &x) == SOF_VECTOR_OK);
    }
    CHECK(sof_vector_sort(&v, compare, &counts) == SOF_VECTOR_OK);
    CHECK(counts.compares > 0);
    CHECK(is_sorted(&v));
    uint64_t sum = 0;
    for (size_t i = 0; i < v.count; i++) {
        sum += value(sof_vector_at(&v, i));
    }
    CHECK(sum == total);
    CHECK(sof_vector_sort(&v, NULL, NULL) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_sort_stable(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_vector_t v;
    CHECK(sof_vector_init(&v, sizeof(pair_t), 0, NULL, NULL, &alloc) == SOF_VECTOR_OK);
    uint64_t random = 0x2545f4914f6cdd1dULL;
    for (uint64_t i = 0; i < 3000; i++) {
        pair_t p = {next_random(&random) % 50, i};
        CHECK(sof_vector_push(&v, &p) == SOF_VECTOR_OK);
    }
    CHECK(sof_vector_sort_stable(&v, compare_pair, NULL) == SOF_VECTOR_OK);
    for (size_t i = 1; i < v.count; i++) {
        const pair_t *a = sof_vector_at(&v, i - 1), *b = sof_vector_at(&v, i);
        CHECK(a->key < b->key || (a->key == b->key && a->seq < b->seq));
    }
    CHECK(sof_vector_sort_stable(&v, NULL, NULL) == SOF_VECTOR_ERROR);

    // The buffer comes from the vector's allocator; if it can't be had, the
    // vector is unchanged.
    for (size_t i = 0; i < v.count; i++) {
        ((pair_t *)sof_vector_at(&v, i))->key = v.count - i;
    }
    state.fail_at = state.allocations + 1;
    CHECK(sof_vector_sort_stable(&v, compare_pair, NULL) == SOF_VECTOR_NO_MEM);
    for (size_t i = 0; i < v.count; i++) {
        CHECK(((pair_t *)sof_vector_at(&v, i))->key == v.count - i);
    }
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_next(void) {
    sof_vector_t v;
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 50));
    uint64_t expect = 0;
    for (void *e = sof_vector_at(&v, 0); e != NULL; e = sof_vector_next(&v, e)) {
        CHECK(value(e) == expect);
        expect++;
    }
    CHECK(expect == 50);
    uint64_t outside = 3;
    CHECK(sof_vector_next(&v, &outside) == NULL);
    CHECK(sof_vector_next(&v, NULL) == NULL);
    CHECK(sof_vector_next(&v, (unsigned char *)sof_vector_at(&v, 3) + 1) == NULL);
    // Past the last element, as when the vector has shrunk past it.
    void *last = sof_vector_at(&v, 49);
    CHECK(sof_vector_pop(&v, NULL) == SOF_VECTOR_OK);
    CHECK(sof_vector_next(&v, last) == NULL);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_traverse(void) {
    sof_vector_t v;
    counts_t counts = {0};
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 100));
    CHECK(sof_vector_traverse(&v, count_visit, &counts) == 0);
    CHECK(counts.visits == 100);
    CHECK(sof_vector_traverse(&v, NULL, NULL) == 0);
    // The callback may change the elements.
    CHECK(sof_vector_traverse(&v, double_it, NULL) == 0);
    for (uint64_t i = 0; i < 100; i++) {
        CHECK(value(sof_vector_at(&v, i)) == i * 2);
    }
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_traverse_stop(void) {
    sof_vector_t v;
    counts_t counts = {0};
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 100));
    counts.stop_after = 30;
    CHECK(sof_vector_traverse(&v, count_visit, &counts) == 42);
    CHECK(counts.visits == 30);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_delete_func(void) {
    sof_vector_t v;
    counts_t counts = {0};
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, count_delete, &counts, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 10));
    CHECK(sof_vector_clear(&v) == SOF_VECTOR_OK);
    CHECK(counts.deletes == 10);
    for (size_t i = 0; i < 8; i++) {
        CHECK(counts.deleted[i] == i);
    }
    CHECK(v.count == 0);
    CHECK(v.capacity == 16);

    counts = (counts_t){0};
    CHECK(push_range(&v, 100, 105));
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
    CHECK(counts.deletes == 5);
    CHECK(counts.deleted[0] == 100);
    CHECK(counts.deleted[4] == 104);
    CHECK(counts.deleted_sum == 510);
    CHECK(v.ready == 0);
}

static void test_random(void) {
    // Random inserts, pushes, pops, removes, swap removes and resizes,
    // checked against a plain array doing the same.
    sof_vector_t v;
    counts_t counts = {0};
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, count_delete, &counts, NULL) == SOF_VECTOR_OK);
    uint64_t model[MODEL_SIZE];
    size_t n = 0;
    uint64_t random = 0x0123456789abcdefULL, deleted_sum = 0;
    for (int step = 0; step < 20000; step++) {
        uint64_t x = next_random(&random) % 100000, op = next_random(&random) % 6;
        if (op == 0 && n < MODEL_SIZE) {
            size_t i = (size_t)(next_random(&random) % (n + 1));
            CHECK(sof_vector_insert(&v, i, &x) == SOF_VECTOR_OK);
            memmove(&model[i + 1], &model[i], (n - i) * sizeof(uint64_t));
            model[i] = x;
            n++;
        } else if (op == 1 && n < MODEL_SIZE) {
            CHECK(sof_vector_push(&v, &x) == SOF_VECTOR_OK);
            model[n++] = x;
        } else if (op == 2 && n > 0) {
            uint64_t y;
            CHECK(sof_vector_pop(&v, &y) == SOF_VECTOR_OK);
            CHECK(y == model[--n]);
        } else if (op == 3 && n > 0) {
            size_t i = (size_t)(next_random(&random) % n);
            deleted_sum += model[i];
            CHECK(sof_vector_remove(&v, i, NULL) == SOF_VECTOR_OK);
            memmove(&model[i], &model[i + 1], (n - i - 1) * sizeof(uint64_t));
            n--;
        } else if (op == 4 && n > 0) {
            size_t i = (size_t)(next_random(&random) % n);
            uint64_t y;
            CHECK(sof_vector_swap_remove(&v, i, &y) == SOF_VECTOR_OK);
            CHECK(y == model[i]);
            model[i] = model[--n];
        } else if (op == 5) {
            size_t to = (size_t)(next_random(&random) % (n + 10));
            if (to > MODEL_SIZE) {
                to = MODEL_SIZE;
            }
            CHECK(sof_vector_resize(&v, to) == SOF_VECTOR_OK);
            for (size_t i = to; i < n; i++) {
                deleted_sum += model[i];
            }
            for (size_t i = n; i < to; i++) {
                model[i] = 0;
            }
            n = to;
        }
        CHECK(v.count == n);
    }
    CHECK(holds(&v, model, n));
    CHECK(counts.deleted_sum == deleted_sum);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_null(void) {
    uint64_t x = 1;
    size_t index = 99;
    CHECK(sof_vector_at(NULL, 0) == NULL);
    CHECK(sof_vector_clear(NULL) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_find(NULL, &x, compare, NULL, &index) == NULL);
    CHECK(sof_vector_free(NULL) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_insert(NULL, 0, &x) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_next(NULL, &x) == NULL);
    CHECK(sof_vector_pop(NULL, &x) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_push(NULL, &x) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_remove(NULL, 0, &x) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_reserve(NULL, 10) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_resize(NULL, 10) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_search(NULL, &x, compare, NULL, &index) == NULL);
    CHECK(sof_vector_shrink(NULL) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_sort(NULL, compare, NULL) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_sort_stable(NULL, compare, NULL) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_swap_remove(NULL, 0, &x) == SOF_VECTOR_ERROR);
    CHECK(sof_vector_traverse(NULL, count_visit, NULL) == -1);
    CHECK(x == 1);
    CHECK(index == 99);
}

static void test_failed_init(void) {
    sof_vector_t v;
    memset(&v, 0xff, sizeof(v));
    CHECK(sof_vector_init(&v, 0, 0, NULL, NULL, NULL) == SOF_VECTOR_ERROR);
    check_dead(&v);
}

static void test_freed(void) {
    sof_vector_t v;
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 100));
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
    check_dead(&v);
    // And it can be initialized again.
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 10));
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

static void test_custom_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_vector_t v;
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 5, NULL, NULL, &alloc) == SOF_VECTOR_OK);
    CHECK(state.allocations == 1);
    CHECK(push_range(&v, 0, 1000));
    CHECK(sof_vector_resize(&v, 3000) == SOF_VECTOR_OK);
    CHECK(sof_vector_resize(&v, 10) == SOF_VECTOR_OK);
    CHECK(sof_vector_shrink(&v) == SOF_VECTOR_OK);
    CHECK(sof_vector_reserve(&v, 77) == SOF_VECTOR_OK);
    CHECK(sof_vector_sort_stable(&v, compare, NULL) == SOF_VECTOR_OK);
    CHECK(state.live == 1);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // Without a realloc, growing uses malloc, a copy and a free.
    state = (test_alloc_t){0};
    alloc.realloc = NULL;
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, &alloc) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 500));
    CHECK(value(sof_vector_at(&v, 499)) == 499);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // A vector freed before anything was added frees nothing.
    state = (test_alloc_t){0};
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, &alloc) == SOF_VECTOR_OK);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
    CHECK(state.allocations == 0);
    CHECK(state.bad == 0);
}

static void test_no_memory(void) {
    // Fail each allocation in turn, until a run makes none fail. A failed
    // init leaves the vector dead, a failed change leaves it unchanged, and
    // nothing may leak.
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_vector_t v;
        int failed = 0;
        sof_vector_error_t rc = sof_vector_init(&v, sizeof(uint64_t), 2, NULL, NULL, &alloc);
        if (rc == SOF_VECTOR_NO_MEM) {
            check_dead(&v);
            CHECK(state.live == 0);
            continue;
        }
        CHECK(rc == SOF_VECTOR_OK);
        uint64_t model[100];
        size_t put = 0;
        for (uint64_t i = 0; i < 40; i++) {
            size_t capacity = v.capacity;
            void *elements = v.elements;
            rc = i % 2 ? sof_vector_push(&v, &i) : sof_vector_insert(&v, 0, &i);
            if (rc == SOF_VECTOR_NO_MEM) {
                failed = 1;
                CHECK(v.capacity == capacity);
                CHECK(v.elements == elements);
            } else {
                CHECK(rc == SOF_VECTOR_OK);
                if (i % 2) {
                    model[put] = i;
                } else {
                    memmove(&model[1], &model[0], put * sizeof(uint64_t));
                    model[0] = i;
                }
                put++;
            }
            CHECK(holds(&v, model, put));
        }
        rc = sof_vector_resize(&v, 100);
        if (rc == SOF_VECTOR_NO_MEM) {
            failed = 1;
            CHECK(holds(&v, model, put));
        } else {
            CHECK(rc == SOF_VECTOR_OK);
            CHECK(sof_vector_resize(&v, put) == SOF_VECTOR_OK);
        }
        rc = sof_vector_shrink(&v);
        if (rc == SOF_VECTOR_NO_MEM) {
            failed = 1;
        } else {
            CHECK(rc == SOF_VECTOR_OK);
        }
        rc = sof_vector_sort_stable(&v, compare, NULL);
        if (rc == SOF_VECTOR_NO_MEM) {
            failed = 1;
            CHECK(holds(&v, model, put));
        } else {
            CHECK(rc == SOF_VECTOR_OK);
            CHECK(is_sorted(&v));
        }
        CHECK(v.count == put);
        CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (!failed) {
            break;
        }
    }
}

/// @brief A vector shared by one writer and several readers under a
/// readers-writer lock.
typedef struct rw_shared_t {
    sof_vector_t v;
    pthread_rwlock_t lock;
} rw_shared_t;

/// @brief A reader thread's results. Readers make a fixed number of passes
/// rather than reading until the writer is done: glibc's rwlock prefers
/// readers, so readers that keep taking the lock can keep the writer out
/// forever.
typedef struct reader_t {
    rw_shared_t *shared;
    size_t passes;
    size_t errors;
} reader_t;

/// @brief Walks the vector PASSES times under the read lock, checking each
/// time that it is sorted and that a binary search finds its elements.
static void *read_vector(void *arg) {
    reader_t *r = arg;
    rw_shared_t *s = r->shared;
    for (size_t pass = 0; pass < PASSES; pass++) {
        pthread_rwlock_rdlock(&s->lock);
        counts_t counts = {0};
        if (sof_vector_traverse(&s->v, count_visit, &counts) != 0 || counts.visits != s->v.count) {
            r->errors++;
        }
        size_t walked = 0;
        for (void *e = sof_vector_at(&s->v, 0); e != NULL; e = sof_vector_next(&s->v, e)) {
            walked++;
        }
        if (walked != s->v.count || !is_sorted(&s->v)) {
            r->errors++;
        }
        if (s->v.count > 0) {
            void *middle = sof_vector_at(&s->v, s->v.count / 2);
            if (sof_vector_search(&s->v, middle, compare, NULL, NULL) == NULL) {
                r->errors++;
            }
        }
        pthread_rwlock_unlock(&s->lock);
        r->passes++;
    }
    return NULL;
}

static void test_threads_readers(void) {
    // One writer keeps the vector sorted, inserting random numbers where a
    // search puts them and removing at random, while readers check it under
    // a read lock.
    rw_shared_t s;
    CHECK(sof_vector_init(&s.v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    CHECK(pthread_rwlock_init(&s.lock, NULL) == 0);
    reader_t readers[READERS] = {{0}};
    pthread_t threads[READERS];
    for (size_t i = 0; i < READERS; i++) {
        readers[i].shared = &s;
        CHECK(pthread_create(&threads[i], NULL, read_vector, &readers[i]) == 0);
    }
    uint64_t random = 0x2545f4914f6cdd1dULL;
    size_t errors = 0;
    for (uint64_t i = 0; i < ITEMS; i++) {
        pthread_rwlock_wrlock(&s.lock);
        uint64_t x = next_random(&random);
        size_t index;
        sof_vector_search(&s.v, &x, compare, NULL, &index);
        if (sof_vector_insert(&s.v, index, &x) != SOF_VECTOR_OK) {
            errors++;
        }
        if (next_random(&random) % 3 == 0) {
            if (sof_vector_remove(&s.v, (size_t)(next_random(&random) % s.v.count), NULL) != SOF_VECTOR_OK) {
                errors++;
            }
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
    CHECK(is_sorted(&s.v));
    CHECK(sof_vector_free(&s.v) == SOF_VECTOR_OK);
    pthread_rwlock_destroy(&s.lock);
}

/// @brief The functions that only read take a const vector, and hand back
/// elements that can still be changed.
static void test_const_vector(void) {
    sof_vector_t v;
    counts_t counts = {0};
    size_t index = 0;
    uint64_t key = 7;
    CHECK(sof_vector_init(&v, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_VECTOR_OK);
    CHECK(push_range(&v, 0, 10));
    const sof_vector_t *cv = &v;
    void *e = sof_vector_at(cv, 3);
    CHECK(e != NULL && value(e) == 3);
    CHECK(sof_vector_at(cv, 10) == NULL);
    CHECK(value(sof_vector_next(cv, e)) == 4);
    CHECK(sof_vector_find(cv, &key, compare, NULL, &index) == sof_vector_at(cv, 7) && index == 7);
    key = 8;
    CHECK(sof_vector_search(cv, &key, compare, NULL, &index) == sof_vector_at(cv, 8) && index == 8);
    CHECK(sof_vector_traverse(cv, count_visit, &counts) == 0 && counts.visits == 10);
    CHECK(sof_vector_traverse(cv, double_it, NULL) == 0);
    CHECK(value(sof_vector_at(cv, 3)) == 6);
    CHECK(v.count == 10);
    CHECK(sof_vector_free(&v) == SOF_VECTOR_OK);
}

int main(void) {
    printf("sof_vector:\n");
    run("init", test_init);
    run("bad init arguments", test_bad_init);
    run("empty vector", test_empty);
    run("push and at", test_push_at);
    run("array growth", test_growth);
    run("push matches insert at the end", test_push_sizes);
    run("pop", test_pop);
    run("insert", test_insert);
    run("inserting an element of the vector itself", test_own_element);
    run("elements of an odd size", test_odd_size);
    run("remove", test_remove);
    run("swap remove", test_swap_remove);
    run("reserve", test_reserve);
    run("resize", test_resize);
    run("shrink", test_shrink);
    run("find", test_find);
    run("binary search", test_search);
    run("sort", test_sort);
    run("stable sort", test_sort_stable);
    run("next", test_next);
    run("traverse", test_traverse);
    run("traversal stops early", test_traverse_stop);
    run("read through a const vector", test_const_vector);
    run("del_func sees every dropped element", test_delete_func);
    run("random changes against a plain array", test_random);
    run("NULL arguments", test_null);
    run("failed init", test_failed_init);
    run("freed vector", test_freed);
    run("custom allocator", test_custom_allocator);
    run("out of memory", test_no_memory);
    run("readers alongside a writer on threads", test_threads_readers);
    return test_report();
}
