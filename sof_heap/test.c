/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_heap.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "sof_heap.h"
#include "sof_sort.h"
#include "test.h"

/// @brief Threads, and items each producer pushes, in the threaded tests.
#define PRODUCERS 4
#define CONSUMERS 4
#define ITEMS 20000
#define READERS 4
#define PASSES 2000

/// @brief The ctx of the compare and delete callbacks: what they count, and
/// which way the heap is ordered.
typedef struct counts_t {
    /// @brief 1 to put the largest first.
    int reverse;
    size_t compares;
    size_t deletes;
    /// @brief The sum of the elements deleted.
    uint64_t deleted_sum;
    size_t visits;
    /// @brief The number of visits after which the traversal stops, or 0.
    size_t stop_after;
} counts_t;

/// @brief An element of an odd size, to check that nothing assumes alignment
/// or a power of two. Ordered by its first two bytes.
typedef struct triple_t {
    unsigned char bytes[3];
} triple_t;

/// @brief Reads an element of a heap of uint64_t, which may be unaligned for
/// a heap of another size.
static uint64_t value(const void *element) {
    uint64_t v;
    memcpy(&v, element, sizeof(v));
    return v;
}

/// @brief Orders uint64_t elements, smallest first, or largest first if the
/// ctx says so, counting the calls. ctx may be NULL.
static int compare(const void *a, const void *b, void *ctx) {
    counts_t *counts = ctx;
    uint64_t x = value(a), y = value(b);
    if (counts != NULL) {
        counts->compares++;
        if (counts->reverse) {
            return x < y ? 1 : x > y ? -1 : 0;
        }
    }
    return x < y ? -1 : x > y ? 1 : 0;
}

/// @brief compare() as sof_sort_quick() takes it.
static int sort_compare(const void *a, const void *b, void *ctx) {
    return compare(a, b, ctx);
}

static int compare_triple(const void *a, const void *b, void *ctx) {
    const triple_t *x = a, *y = b;
    int kx = x->bytes[0] << 8 | x->bytes[1], ky = y->bytes[0] << 8 | y->bytes[1];
    return kx - ky;
}

static void count_delete(void *element, void *ctx) {
    counts_t *counts = ctx;
    counts->deletes++;
    counts->deleted_sum += value(element);
}

static int count_visit(const void *element, void *ctx) {
    counts_t *counts = ctx;
    counts->visits++;
    if (counts->stop_after != 0 && counts->visits == counts->stop_after) {
        return 42;
    }
    return 0;
}

/// @brief Adds each element to the ctx's sum, for sof_heap_traverse().
static int add_up(const void *element, void *ctx) {
    *(uint64_t *)ctx += value(element);
    return 0;
}

/// @brief Returns 1 if no element of h is less than its parent, by h's own
/// compare function.
static int is_heap(const sof_heap_t *h) {
    for (size_t i = 1; i < h->count; i++) {
        const unsigned char *child = h->elements + i * h->element_size;
        const unsigned char *parent = h->elements + (i - 1) / 2 * h->element_size;
        if (h->cmp_func(child, parent, h->ctx) < 0) {
            return 0;
        }
    }
    return 1;
}

/// @brief Pushes the values from to to - 1.
static int push_range(sof_heap_t *h, uint64_t from, uint64_t to) {
    for (uint64_t i = from; i < to; i++) {
        if (sof_heap_push(h, &i) != SOF_HEAP_OK) {
            return 0;
        }
    }
    return 1;
}

/// @brief Returns the sum of h's elements.
static uint64_t sum(const sof_heap_t *h) {
    uint64_t total = 0;
    for (const void *e = sof_heap_peek(h); e != NULL; e = sof_heap_next(h, e)) {
        total += value(e);
    }
    return total;
}

static void test_init(void) {
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(h.ready == 1);
    CHECK(h.elements == NULL);
    CHECK(h.element_size == sizeof(uint64_t));
    CHECK(h.count == 0);
    CHECK(h.capacity == 0);
    CHECK(h.cmp_func == compare);
    CHECK(h.del_func == NULL);
    CHECK(h.alloc.malloc == NULL);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);

    counts_t counts = {0};
    CHECK(sof_heap_init(&h, 5, 100, compare, count_delete, &counts, NULL) == SOF_HEAP_OK);
    CHECK(h.elements != NULL);
    CHECK(h.capacity == 100);
    CHECK(h.element_size == 5);
    CHECK(h.del_func == count_delete);
    CHECK(h.ctx == &counts);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_bad_init(void) {
    sof_heap_t h;
    CHECK(sof_heap_init(&h, 0, 0, compare, NULL, NULL, NULL) == SOF_HEAP_ERROR);
    CHECK(h.ready == 0);
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 10, NULL, NULL, NULL, NULL) == SOF_HEAP_ERROR);
    CHECK(h.ready == 0);
    CHECK(h.elements == NULL);

    // A capacity whose size, with the scratch slot, overflows can't be
    // allocated.
    CHECK(sof_heap_init(&h, 16, SIZE_MAX / 8, compare, NULL, NULL, NULL) == SOF_HEAP_NO_MEM);
    CHECK(h.ready == 0);
    CHECK(h.elements == NULL);
    CHECK(sof_heap_init(&h, 1, SIZE_MAX, compare, NULL, NULL, NULL) == SOF_HEAP_NO_MEM);
    CHECK(h.ready == 0);
    CHECK(sof_heap_init(&h, 2, SIZE_MAX / 2, compare, NULL, NULL, NULL) == SOF_HEAP_NO_MEM);
    CHECK(h.ready == 0);
    CHECK(sof_heap_push(&h, &(uint64_t){1}) == SOF_HEAP_ERROR);
}

static void test_empty(void) {
    sof_heap_t h;
    uint64_t v = 7;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(sof_heap_peek(&h) == NULL);
    CHECK(sof_heap_pop(&h, &v) == SOF_HEAP_NO_MATCH);
    CHECK(v == 7);
    CHECK(sof_heap_pop(&h, NULL) == SOF_HEAP_NO_MATCH);
    CHECK(sof_heap_replace(&h, &v, &v) == SOF_HEAP_NO_MATCH);
    CHECK(h.count == 0);
    CHECK(sof_heap_remove(&h, &v, NULL) == SOF_HEAP_NO_MATCH);
    CHECK(sof_heap_traverse(&h, count_visit, &(counts_t){0}) == 0);
    CHECK(sof_heap_next(&h, &v) == NULL);
    CHECK(sof_heap_clear(&h) == SOF_HEAP_OK);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_order(void) {
    // Random values come out in the order sof_sort_quick() puts them in.
    static uint64_t values[10000], sorted[10000];
    uint64_t state = 0x9e3779b97f4a7c15ULL;
    for (size_t i = 0; i < 10000; i++) {
        values[i] = next_random(&state);
    }
    memcpy(sorted, values, sizeof(values));
    CHECK(sof_sort_quick(sorted, 10000, sizeof(uint64_t), sort_compare, NULL) == SOF_SORT_OK);

    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    for (size_t i = 0; i < 10000; i++) {
        CHECK(sof_heap_push(&h, &values[i]) == SOF_HEAP_OK);
        CHECK(h.count == i + 1);
        if (i % 1000 == 0) {
            CHECK(is_heap(&h));
        }
    }
    CHECK(is_heap(&h));
    for (size_t i = 0; i < 10000; i++) {
        uint64_t v = 0;
        CHECK(value(sof_heap_peek(&h)) == sorted[i]);
        CHECK(sof_heap_pop(&h, &v) == SOF_HEAP_OK);
        CHECK(v == sorted[i]);
        CHECK(h.count == 10000 - i - 1);
    }
    CHECK(sof_heap_peek(&h) == NULL);

    // The heap works again after being emptied.
    CHECK(sof_heap_push(&h, &(uint64_t){5}) == SOF_HEAP_OK);
    CHECK(value(sof_heap_peek(&h)) == 5);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_reverse(void) {
    // A compare function that reverses the order, through ctx, makes a
    // largest-first heap, and is called with the ctx given to sof_heap_init().
    counts_t counts = {0};
    counts.reverse = 1;
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, &counts, NULL) == SOF_HEAP_OK);
    uint64_t state = 0x2545f4914f6cdd1dULL;
    for (size_t i = 0; i < 1000; i++) {
        uint64_t v = next_random(&state) % 500;
        CHECK(sof_heap_push(&h, &v) == SOF_HEAP_OK);
    }
    CHECK(counts.compares > 0);
    uint64_t previous = UINT64_MAX;
    for (size_t i = 0; i < 1000; i++) {
        uint64_t v;
        CHECK(sof_heap_pop(&h, &v) == SOF_HEAP_OK);
        CHECK(v <= previous);
        previous = v;
    }
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_duplicates(void) {
    // Many equal elements come out together, in order.
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    uint64_t state = 0x1234567ULL;
    size_t counts[4] = {0};
    for (size_t i = 0; i < 2000; i++) {
        uint64_t v = next_random(&state) % 4;
        counts[v]++;
        CHECK(sof_heap_push(&h, &v) == SOF_HEAP_OK);
    }
    for (uint64_t k = 0; k < 4; k++) {
        for (size_t i = 0; i < counts[k]; i++) {
            uint64_t v;
            CHECK(sof_heap_pop(&h, &v) == SOF_HEAP_OK);
            CHECK(v == k);
        }
    }
    CHECK(h.count == 0);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_growth(void) {
    // The array doubles from the capacity given to sof_heap_init(), or from 16.
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(push_range(&h, 0, 1));
    CHECK(h.capacity == 16);
    CHECK(push_range(&h, 1, 17));
    CHECK(h.capacity == 32);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);

    CHECK(sof_heap_init(&h, sizeof(uint64_t), 1, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(h.capacity == 1);
    CHECK(push_range(&h, 0, 1));
    CHECK(h.capacity == 1);
    CHECK(push_range(&h, 1, 2));
    CHECK(h.capacity == 2);
    CHECK(push_range(&h, 2, 3));
    CHECK(h.capacity == 4);
    CHECK(push_range(&h, 3, 5));
    CHECK(h.capacity == 8);
    CHECK(is_heap(&h));
    CHECK(sum(&h) == 10);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_odd_size(void) {
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(triple_t), 0, compare_triple, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(h.element_size == 3);
    // Pushed in a scrambled order: 7 is coprime with 300.
    for (unsigned j = 0; j < 300; j++) {
        unsigned i = j * 7 % 300;
        triple_t t = {{(unsigned char)(i >> 8), (unsigned char)i, (unsigned char)(i * 7)}};
        CHECK(sof_heap_push(&h, &t) == SOF_HEAP_OK);
    }
    CHECK(is_heap(&h));
    for (unsigned i = 0; i < 300; i++) {
        triple_t t;
        CHECK(sof_heap_pop(&h, &t) == SOF_HEAP_OK);
        CHECK(t.bytes[0] == (unsigned char)(i >> 8));
        CHECK(t.bytes[1] == (unsigned char)i);
        CHECK(t.bytes[2] == (unsigned char)(i * 7));
    }
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_push_own_element(void) {
    // Pushing the smallest element when the array is full must copy it
    // before the array moves.
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 1, compare, NULL, NULL, &alloc) == SOF_HEAP_OK);
    CHECK(push_range(&h, 41, 42));
    for (size_t i = 0; i < 10; i++) {
        size_t capacity = h.capacity;
        CHECK(sof_heap_push(&h, sof_heap_peek(&h)) == SOF_HEAP_OK);
        CHECK(h.count == i + 2);
        CHECK(i + 1 < capacity || h.capacity == capacity * 2);
    }
    for (const void *e = sof_heap_peek(&h); e != NULL; e = sof_heap_next(&h, e)) {
        CHECK(value(e) == 41);
    }
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);

    // So must pushing the last one, which is larger.
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 4, compare, NULL, NULL, &alloc) == SOF_HEAP_OK);
    CHECK(push_range(&h, 1, 5));
    CHECK(h.count == h.capacity);
    const unsigned char *last = h.elements + 3 * sizeof(uint64_t);
    uint64_t expected = value(last);
    CHECK(sof_heap_push(&h, last) == SOF_HEAP_OK);
    CHECK(h.capacity == 8);
    CHECK(is_heap(&h));
    CHECK(sum(&h) == 10 + expected);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_peek(void) {
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    for (uint64_t i = 10; i > 0; i--) {
        CHECK(sof_heap_push(&h, &i) == SOF_HEAP_OK);
        CHECK(value(sof_heap_peek(&h)) == i);
    }
    CHECK(sof_heap_peek(&h) == h.elements);
    CHECK(push_range(&h, 20, 30));
    CHECK(value(sof_heap_peek(&h)) == 1);
    CHECK(sof_heap_pop(&h, NULL) == SOF_HEAP_OK);
    CHECK(value(sof_heap_peek(&h)) == 2);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_next(void) {
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(push_range(&h, 0, 100));
    size_t walked = 0;
    for (const void *e = sof_heap_peek(&h); e != NULL; e = sof_heap_next(&h, e)) {
        CHECK(e == h.elements + walked * sizeof(uint64_t));
        walked++;
    }
    CHECK(walked == 100);
    CHECK(sum(&h) == 99 * 100 / 2);
    CHECK(sof_heap_next(&h, h.elements + 99 * sizeof(uint64_t)) == NULL);
    CHECK(sof_heap_next(&h, NULL) == NULL);

    // Pointers that aren't elements of the heap give NULL: one elsewhere,
    // one into the middle of an element, one past the last, and one into
    // the array's unused room.
    uint64_t other = 5;
    CHECK(sof_heap_next(&h, &other) == NULL);
    CHECK(sof_heap_next(&h, h.elements + sizeof(uint64_t) + 1) == NULL);
    CHECK(sof_heap_next(&h, h.elements + 100 * sizeof(uint64_t)) == NULL);
    CHECK(h.capacity > 101);
    CHECK(sof_heap_next(&h, h.elements + 101 * sizeof(uint64_t)) == NULL);

    // The last element, once the heap shrinks past it, isn't one any more.
    const void *last = h.elements + 99 * sizeof(uint64_t);
    CHECK(sof_heap_pop(&h, NULL) == SOF_HEAP_OK);
    CHECK(sof_heap_next(&h, last) == NULL);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_traverse(void) {
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(push_range(&h, 0, 100));
    counts_t counts = {0};
    CHECK(sof_heap_traverse(&h, count_visit, &counts) == 0);
    CHECK(counts.visits == 100);
    uint64_t total = 0;
    CHECK(sof_heap_traverse(&h, add_up, &total) == 0);
    CHECK(total == 99 * 100 / 2);

    // A NULL callback visits nothing.
    CHECK(sof_heap_traverse(&h, NULL, NULL) == 0);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_traverse_stop(void) {
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(push_range(&h, 0, 100));
    counts_t counts = {0};
    counts.stop_after = 1;
    CHECK(sof_heap_traverse(&h, count_visit, &counts) == 42);
    CHECK(counts.visits == 1);
    counts.visits = 0;
    counts.stop_after = 37;
    CHECK(sof_heap_traverse(&h, count_visit, &counts) == 42);
    CHECK(counts.visits == 37);
    counts.visits = 0;
    counts.stop_after = 100;
    CHECK(sof_heap_traverse(&h, count_visit, &counts) == 42);
    CHECK(counts.visits == 100);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_remove(void) {
    // Removing the element at each index in turn, from a heap of random
    // values, gives back that element and leaves a heap of the rest.
    static uint64_t values[200];
    uint64_t state = 0xdeadbeefULL;
    for (size_t i = 0; i < 200; i++) {
        values[i] = next_random(&state) % 1000;
    }
    for (size_t at = 0; at < 200; at++) {
        sof_heap_t h;
        CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
        uint64_t total = 0;
        for (size_t i = 0; i < 200; i++) {
            CHECK(sof_heap_push(&h, &values[i]) == SOF_HEAP_OK);
            total += values[i];
        }
        const void *element = h.elements + at * sizeof(uint64_t);
        uint64_t expected = value(element), removed = UINT64_MAX;
        CHECK(sof_heap_remove(&h, element, &removed) == SOF_HEAP_OK);
        CHECK(removed == expected);
        CHECK(h.count == 199);
        CHECK(is_heap(&h));
        CHECK(sum(&h) == total - expected);
        uint64_t previous = 0;
        while (sof_heap_pop(&h, &removed) == SOF_HEAP_OK) {
            CHECK(removed >= previous);
            previous = removed;
        }
        CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
    }

    // Pointers that aren't elements are refused, and change nothing.
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(push_range(&h, 0, 10));
    uint64_t other = 3, removed = 77;
    CHECK(sof_heap_remove(&h, &other, &removed) == SOF_HEAP_NO_MATCH);
    CHECK(sof_heap_remove(&h, NULL, &removed) == SOF_HEAP_NO_MATCH);
    CHECK(sof_heap_remove(&h, h.elements + 1, &removed) == SOF_HEAP_NO_MATCH);
    CHECK(sof_heap_remove(&h, h.elements + 10 * sizeof(uint64_t), &removed) == SOF_HEAP_NO_MATCH);
    CHECK(removed == 77);
    CHECK(h.count == 10);

    // Removing every element, the last one first, empties the heap.
    while (h.count > 0) {
        CHECK(sof_heap_remove(&h, h.elements + (h.count - 1) * sizeof(uint64_t), NULL) == SOF_HEAP_OK);
        CHECK(is_heap(&h));
    }
    CHECK(sof_heap_peek(&h) == NULL);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_remove_moves_up(void) {
    // A removed element's place can be filled by one smaller than its
    // parent, which must then move up. Each value here is no smaller than
    // its parent, so they stay where they are pushed: large values on the
    // left, small ones on the right, and the last, 4, on the right. It fills
    // the place of 301, at index 9, under 300 and 100, and must rise past
    // both.
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    uint64_t values[] = {0, 100, 1, 200, 300, 2, 3, 201, 202, 301, 302, 4};
    size_t n = sizeof(values) / sizeof(values[0]);
    for (size_t i = 0; i < n; i++) {
        CHECK(sof_heap_push(&h, &values[i]) == SOF_HEAP_OK);
    }
    CHECK(memcmp(h.elements, values, sizeof(values)) == 0);
    CHECK(sof_heap_remove(&h, h.elements + 9 * sizeof(uint64_t), NULL) == SOF_HEAP_OK);
    CHECK(is_heap(&h));
    CHECK(value(h.elements + 1 * sizeof(uint64_t)) == 4);
    CHECK(value(h.elements + 4 * sizeof(uint64_t)) == 100);
    CHECK(value(h.elements + 9 * sizeof(uint64_t)) == 300);
    uint64_t previous = 0, v;
    size_t popped = 0;
    while (sof_heap_pop(&h, &v) == SOF_HEAP_OK) {
        CHECK(popped == 0 || v > previous);
        CHECK(v != 301);
        previous = v;
        popped++;
    }
    CHECK(popped == n - 1);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_replace(void) {
    // Keeping the 10 largest of a stream: a heap of 10 and sof_heap_replace()
    // for each value larger than the smallest kept.
    static uint64_t values[10000];
    uint64_t state = 0xabcdef1234ULL;
    for (size_t i = 0; i < 10000; i++) {
        values[i] = next_random(&state);
    }
    test_alloc_t astate = {0};
    sof_alloc_t alloc = test_allocator(&astate);
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 10, compare, NULL, NULL, &alloc) == SOF_HEAP_OK);
    for (size_t i = 0; i < 10000; i++) {
        if (h.count < 10) {
            CHECK(sof_heap_push(&h, &values[i]) == SOF_HEAP_OK);
        } else if (values[i] > value(sof_heap_peek(&h))) {
            uint64_t removed;
            uint64_t smallest = value(sof_heap_peek(&h));
            CHECK(sof_heap_replace(&h, &values[i], &removed) == SOF_HEAP_OK);
            CHECK(removed == smallest);
            CHECK(h.count == 10);
        }
    }
    // sof_heap_replace() never allocates.
    CHECK(astate.allocations == 1);
    CHECK(sof_sort_quick(values, 10000, sizeof(uint64_t), sort_compare, NULL) == SOF_SORT_OK);
    for (size_t i = 0; i < 10; i++) {
        uint64_t v;
        CHECK(sof_heap_pop(&h, &v) == SOF_HEAP_OK);
        CHECK(v == values[9990 + i]);
    }
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
    CHECK(astate.live == 0);
    CHECK(astate.bad == 0);

    // Replacing with something smaller than every element puts it first.
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(push_range(&h, 10, 20));
    uint64_t removed;
    CHECK(sof_heap_replace(&h, &(uint64_t){3}, &removed) == SOF_HEAP_OK);
    CHECK(removed == 10);
    CHECK(value(sof_heap_peek(&h)) == 3);

    // Replacing an element with itself, or with another element of the
    // heap, copies it before it is overwritten.
    CHECK(sof_heap_replace(&h, sof_heap_peek(&h), &removed) == SOF_HEAP_OK);
    CHECK(removed == 3);
    CHECK(value(sof_heap_peek(&h)) == 3);
    const void *last = h.elements + (h.count - 1) * sizeof(uint64_t);
    uint64_t expected = value(last);
    CHECK(sof_heap_replace(&h, last, &removed) == SOF_HEAP_OK);
    CHECK(removed == 3);
    CHECK(is_heap(&h));
    CHECK(sum(&h) == 11 + 12 + 13 + 14 + 15 + 16 + 17 + 18 + 19 + expected);
    CHECK(h.count == 10);

    // A NULL element can't be pushed.
    CHECK(sof_heap_replace(&h, NULL, &removed) == SOF_HEAP_ERROR);
    CHECK(h.count == 10);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_delete_func(void) {
    sof_heap_t h;
    counts_t counts = {0};
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, count_delete, &counts, NULL) == SOF_HEAP_OK);
    CHECK(push_range(&h, 1, 11));

    // An element copied out to the caller doesn't go to the del_func.
    uint64_t v;
    CHECK(sof_heap_pop(&h, &v) == SOF_HEAP_OK);
    CHECK(v == 1);
    CHECK(sof_heap_remove(&h, sof_heap_peek(&h), &v) == SOF_HEAP_OK);
    CHECK(v == 2);
    CHECK(sof_heap_replace(&h, &(uint64_t){100}, &v) == SOF_HEAP_OK);
    CHECK(v == 3);
    CHECK(counts.deletes == 0);

    // A dropped one does, from each function that removes.
    CHECK(sof_heap_pop(&h, NULL) == SOF_HEAP_OK);
    CHECK(counts.deletes == 1);
    CHECK(counts.deleted_sum == 4);
    CHECK(sof_heap_remove(&h, sof_heap_peek(&h), NULL) == SOF_HEAP_OK);
    CHECK(counts.deletes == 2);
    CHECK(counts.deleted_sum == 4 + 5);
    CHECK(sof_heap_replace(&h, &(uint64_t){200}, NULL) == SOF_HEAP_OK);
    CHECK(counts.deletes == 3);
    CHECK(counts.deleted_sum == 4 + 5 + 6);
    CHECK(h.count == 6);

    // sof_heap_clear() gives it every element and keeps the array, so the heap
    // can be used again.
    size_t capacity = h.capacity;
    counts.deletes = 0;
    counts.deleted_sum = 0;
    CHECK(sof_heap_clear(&h) == SOF_HEAP_OK);
    CHECK(counts.deletes == 6);
    CHECK(counts.deleted_sum == 7 + 8 + 9 + 10 + 100 + 200);
    CHECK(h.count == 0);
    CHECK(h.capacity == capacity);
    CHECK(h.elements != NULL);
    CHECK(h.ready == 1);
    CHECK(sof_heap_clear(&h) == SOF_HEAP_OK);
    CHECK(counts.deletes == 6);

    // So does sof_heap_free().
    CHECK(push_range(&h, 1, 6));
    counts.deletes = 0;
    counts.deleted_sum = 0;
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
    CHECK(counts.deletes == 5);
    CHECK(counts.deleted_sum == 15);
}

static void test_random(void) {
    // Random pushes, pops, removes and replaces, checked against a copy kept
    // in an unsorted array.
    static uint64_t model[100000];
    size_t count = 0;
    uint64_t state = 0x9e3779b97f4a7c15ULL;
    counts_t counts = {0};
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, &counts, NULL) == SOF_HEAP_OK);
    for (size_t i = 0; i < 100000; i++) {
        uint64_t r = next_random(&state);
        uint64_t v = r >> 32 & 0xfff;
        size_t smallest = 0;
        for (size_t j = 1; j < count; j++) {
            if (model[j] < model[smallest]) {
                smallest = j;
            }
        }
        uint64_t out = UINT64_MAX;
        switch (r % 8) {
        case 0:
        case 1:
        case 2:
        case 3:
            CHECK(sof_heap_push(&h, &v) == SOF_HEAP_OK);
            model[count++] = v;
            break;
        case 4:
        case 5:
            if (count == 0) {
                CHECK(sof_heap_pop(&h, &out) == SOF_HEAP_NO_MATCH);
            } else {
                CHECK(sof_heap_pop(&h, &out) == SOF_HEAP_OK);
                CHECK(out == model[smallest]);
                model[smallest] = model[--count];
            }
            break;
        case 6:
            if (count == 0) {
                CHECK(sof_heap_replace(&h, &v, &out) == SOF_HEAP_NO_MATCH);
            } else {
                CHECK(sof_heap_replace(&h, &v, &out) == SOF_HEAP_OK);
                CHECK(out == model[smallest]);
                model[smallest] = v;
            }
            break;
        default:
            if (count == 0) {
                CHECK(sof_heap_remove(&h, h.elements, &out) == SOF_HEAP_NO_MATCH);
            } else {
                size_t at = (size_t)(r >> 16) % count;
                uint64_t expected = value(h.elements + at * sizeof(uint64_t));
                CHECK(sof_heap_remove(&h, h.elements + at * sizeof(uint64_t), &out) == SOF_HEAP_OK);
                CHECK(out == expected);
                size_t j = 0;
                while (j < count && model[j] != expected) {
                    j++;
                }
                CHECK(j < count);
                model[j] = model[--count];
            }
            break;
        }
        CHECK(h.count == count);
        if (i % 4096 == 0) {
            CHECK(is_heap(&h));
        }
    }
    CHECK(is_heap(&h));
    CHECK(count > 0);
    CHECK(counts.compares > 0);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_null(void) {
    uint64_t v = 3;
    CHECK(sof_heap_init(NULL, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_ERROR);
    CHECK(sof_heap_clear(NULL) == SOF_HEAP_ERROR);
    CHECK(sof_heap_free(NULL) == SOF_HEAP_ERROR);
    CHECK(sof_heap_next(NULL, &v) == NULL);
    CHECK(sof_heap_peek(NULL) == NULL);
    CHECK(sof_heap_pop(NULL, &v) == SOF_HEAP_ERROR);
    CHECK(sof_heap_pop(NULL, NULL) == SOF_HEAP_ERROR);
    CHECK(sof_heap_push(NULL, &v) == SOF_HEAP_ERROR);
    CHECK(sof_heap_remove(NULL, &v, &v) == SOF_HEAP_ERROR);
    CHECK(sof_heap_replace(NULL, &v, &v) == SOF_HEAP_ERROR);
    CHECK(v == 3);
    CHECK(sof_heap_traverse(NULL, count_visit, &(counts_t){0}) == -1);

    // A NULL element can't be pushed.
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(sof_heap_push(&h, NULL) == SOF_HEAP_ERROR);
    CHECK(h.count == 0);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

/// @brief Checks that every function treats h as dead.
static void check_dead(sof_heap_t *h) {
    counts_t counts = {0};
    uint64_t v = 3;
    CHECK(h->ready == 0);
    CHECK(sof_heap_push(h, &v) == SOF_HEAP_ERROR);
    CHECK(h->count == 0);
    CHECK(h->elements == NULL);
    CHECK(sof_heap_peek(h) == NULL);
    CHECK(sof_heap_next(h, &v) == NULL);
    CHECK(sof_heap_pop(h, &v) == SOF_HEAP_ERROR);
    CHECK(sof_heap_remove(h, &v, &v) == SOF_HEAP_ERROR);
    CHECK(sof_heap_replace(h, &v, &v) == SOF_HEAP_ERROR);
    CHECK(v == 3);
    CHECK(sof_heap_traverse(h, count_visit, &counts) == -1);
    CHECK(counts.visits == 0);
    CHECK(sof_heap_clear(h) == SOF_HEAP_ERROR);
    CHECK(sof_heap_free(h) == SOF_HEAP_ERROR);
}

static void test_failed_init(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 10, compare, NULL, NULL, &alloc) == SOF_HEAP_ERROR);
    check_dead(&h);
    CHECK(state.allocations == 0);

    CHECK(sof_heap_init(&h, 0, 10, compare, NULL, NULL, NULL) == SOF_HEAP_ERROR);
    check_dead(&h);
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 10, NULL, NULL, NULL, NULL) == SOF_HEAP_ERROR);
    check_dead(&h);

    // It can be initialized again.
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(push_range(&h, 0, 1));
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

static void test_freed(void) {
    sof_heap_t h;
    counts_t counts = {0};
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, count_delete, &counts, NULL) == SOF_HEAP_OK);
    CHECK(push_range(&h, 0, 10));
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
    CHECK(counts.deletes == 10);
    check_dead(&h);
    CHECK(counts.deletes == 10);
}

static void test_custom_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_heap_t h;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, &alloc) == SOF_HEAP_OK);
    CHECK(state.allocations == 0);
    CHECK(push_range(&h, 0, 1000));
    // 16, 32, ... 1024: one allocation and six reallocations.
    CHECK(state.allocations == 7);
    CHECK(state.live == 1);
    for (size_t i = 0; i < 500; i++) {
        CHECK(sof_heap_pop(&h, NULL) == SOF_HEAP_OK);
    }
    CHECK(sof_heap_clear(&h) == SOF_HEAP_OK);
    CHECK(state.live == 1);

    // A cleared heap keeps its array, so refilling it allocates nothing.
    CHECK(push_range(&h, 0, 1000));
    CHECK(state.allocations == 7);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // A capacity given to sof_heap_init() is allocated there.
    state = (test_alloc_t){0};
    CHECK(sof_heap_init(&h, 3, 50, compare_triple, NULL, NULL, &alloc) == SOF_HEAP_OK);
    CHECK(state.allocations == 1);
    CHECK(state.live == 1);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // A heap freed before anything was pushed frees nothing.
    state = (test_alloc_t){0};
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, &alloc) == SOF_HEAP_OK);
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
    CHECK(state.allocations == 0);
    CHECK(state.bad == 0);
}

static void test_no_memory(void) {
    // Fail each allocation in turn, until a run makes none fail. A failed
    // init leaves the heap dead, a failed push leaves it unchanged, and
    // nothing may leak.
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_heap_t h;
        int failed = 0;
        sof_heap_error_t rc = sof_heap_init(&h, sizeof(uint64_t), 2, compare, NULL, NULL, &alloc);
        if (rc == SOF_HEAP_NO_MEM) {
            check_dead(&h);
            CHECK(state.live == 0);
            continue;
        }
        CHECK(rc == SOF_HEAP_OK);
        uint64_t put = 0, total = 0;
        for (uint64_t i = 40; i > 0; i--) {
            size_t capacity = h.capacity;
            void *elements = h.elements;
            uint64_t smallest = h.count > 0 ? value(sof_heap_peek(&h)) : 0;
            rc = sof_heap_push(&h, &i);
            if (rc == SOF_HEAP_NO_MEM) {
                failed = 1;
                CHECK(h.count == put);
                CHECK(h.capacity == capacity);
                CHECK(h.elements == elements);
                CHECK(value(sof_heap_peek(&h)) == smallest);
            } else {
                CHECK(rc == SOF_HEAP_OK);
                put++;
                total += i;
            }
        }
        CHECK(h.count == put);
        CHECK(is_heap(&h));
        CHECK(sum(&h) == total);
        CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (!failed) {
            break;
        }
    }
}

/// @brief A heap shared by threads, with the lock and condition variable
/// the application supplies, as in the README's Threading section.
typedef struct shared_t {
    sof_heap_t h;
    pthread_mutex_t lock;
    pthread_cond_t ready;
    /// @brief Producers that haven't finished; consumers stop at 0 once the
    /// heap is empty.
    size_t producers_left;
} shared_t;

/// @brief One producer or consumer thread's state and results, checked by
/// the main thread after joining, since CHECK isn't safe on other threads.
typedef struct worker_t {
    shared_t *shared;
    uint64_t id;
    /// @brief Items popped, and the sum of them.
    size_t count;
    uint64_t sum;
    /// @brief Calls that failed, or items that can't have been pushed.
    size_t errors;
} worker_t;

/// @brief Pushes ITEMS items, numbered id * ITEMS + seq, in falling order so
/// each is the smallest when pushed.
static void *produce(void *arg) {
    worker_t *w = arg;
    shared_t *s = w->shared;
    for (uint64_t seq = ITEMS; seq-- > 0;) {
        uint64_t item = w->id * ITEMS + seq;
        pthread_mutex_lock(&s->lock);
        if (sof_heap_push(&s->h, &item) != SOF_HEAP_OK) {
            w->errors++;
        }
        pthread_cond_signal(&s->ready);
        pthread_mutex_unlock(&s->lock);
    }
    pthread_mutex_lock(&s->lock);
    s->producers_left--;
    pthread_cond_broadcast(&s->ready);
    pthread_mutex_unlock(&s->lock);
    return NULL;
}

/// @brief Pops until the producers are done and the heap is empty. Each pop
/// is checked to be no greater than what is left, under the same lock.
static void *consume(void *arg) {
    worker_t *w = arg;
    shared_t *s = w->shared;
    while (1) {
        uint64_t item;
        pthread_mutex_lock(&s->lock);
        sof_heap_error_t rc;
        while ((rc = sof_heap_pop(&s->h, &item)) == SOF_HEAP_NO_MATCH && s->producers_left > 0) {
            pthread_cond_wait(&s->ready, &s->lock);
        }
        if (rc == SOF_HEAP_OK && s->h.count > 0 && value(sof_heap_peek(&s->h)) < item) {
            w->errors++;
        }
        pthread_mutex_unlock(&s->lock);
        if (rc == SOF_HEAP_NO_MATCH) {
            return NULL;
        }
        if (rc != SOF_HEAP_OK || item >= (uint64_t)PRODUCERS * ITEMS) {
            w->errors++;
            return NULL;
        }
        w->count++;
        w->sum += item;
    }
}

static void test_threads(void) {
    // Producers and consumers share one heap under a mutex, with a condition
    // variable so consumers wait for work. Every item must be popped exactly
    // once, without leaks.
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    shared_t s;
    CHECK(sof_heap_init(&s.h, sizeof(uint64_t), 0, compare, NULL, NULL, &alloc) == SOF_HEAP_OK);
    CHECK(pthread_mutex_init(&s.lock, NULL) == 0);
    CHECK(pthread_cond_init(&s.ready, NULL) == 0);
    s.producers_left = PRODUCERS;

    worker_t producers[PRODUCERS] = {{0}}, consumers[CONSUMERS] = {{0}};
    pthread_t threads[PRODUCERS + CONSUMERS];
    for (size_t i = 0; i < CONSUMERS; i++) {
        consumers[i].shared = &s;
        consumers[i].id = i;
        CHECK(pthread_create(&threads[PRODUCERS + i], NULL, consume, &consumers[i]) == 0);
    }
    for (size_t i = 0; i < PRODUCERS; i++) {
        producers[i].shared = &s;
        producers[i].id = i;
        CHECK(pthread_create(&threads[i], NULL, produce, &producers[i]) == 0);
    }
    for (size_t i = 0; i < PRODUCERS + CONSUMERS; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }

    size_t count = 0;
    uint64_t total_sum = 0;
    for (size_t i = 0; i < PRODUCERS; i++) {
        CHECK(producers[i].errors == 0);
    }
    for (size_t i = 0; i < CONSUMERS; i++) {
        CHECK(consumers[i].errors == 0);
        count += consumers[i].count;
        total_sum += consumers[i].sum;
    }
    uint64_t total = (uint64_t)PRODUCERS * ITEMS;
    CHECK(count == total);
    CHECK(total_sum == total * (total - 1) / 2);
    CHECK(s.h.count == 0);
    CHECK(sof_heap_free(&s.h) == SOF_HEAP_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    pthread_cond_destroy(&s.ready);
    pthread_mutex_destroy(&s.lock);
}

/// @brief A heap shared by one writer and several readers under a
/// readers-writer lock.
typedef struct rw_shared_t {
    sof_heap_t h;
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

/// @brief Checks, for sof_heap_traverse(), that no element is smaller than the
/// first, and counts them.
static int check_first_smallest(const void *element, void *ctx) {
    uint64_t *state = ctx; // state[0]: items seen, state[1]: the first item
    if (state[0] == 0) {
        state[1] = value(element);
    } else if (value(element) < state[1]) {
        return 1;
    }
    state[0]++;
    return 0;
}

/// @brief Walks the heap PASSES times with sof_heap_traverse() and sof_heap_next()
/// under the read lock, checking what it sees each time.
static void *read_heap(void *arg) {
    reader_t *r = arg;
    rw_shared_t *s = r->shared;
    for (size_t pass = 0; pass < PASSES; pass++) {
        pthread_rwlock_rdlock(&s->lock);
        uint64_t state[2] = {0, 0};
        if (sof_heap_traverse(&s->h, check_first_smallest, state) != 0 || state[0] != s->h.count) {
            r->errors++;
        }
        size_t walked = 0;
        for (const void *e = sof_heap_peek(&s->h); e != NULL; e = sof_heap_next(&s->h, e)) {
            walked++;
        }
        if (walked != s->h.count || !is_heap(&s->h)) {
            r->errors++;
        }
        pthread_rwlock_unlock(&s->lock);
        r->passes++;
    }
    return NULL;
}

static void test_threads_readers(void) {
    // One writer pushes random numbers and pops at random, growing the array
    // as it goes, while readers walk the heap under a read lock and must
    // always see a heap, with the smallest first and as many as count.
    rw_shared_t s;
    CHECK(sof_heap_init(&s.h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(pthread_rwlock_init(&s.lock, NULL) == 0);
    reader_t readers[READERS] = {{0}};
    pthread_t threads[READERS];
    for (size_t i = 0; i < READERS; i++) {
        readers[i].shared = &s;
        CHECK(pthread_create(&threads[i], NULL, read_heap, &readers[i]) == 0);
    }
    uint64_t random = 0x2545f4914f6cdd1dULL;
    size_t errors = 0;
    for (uint64_t i = 0; i < ITEMS; i++) {
        pthread_rwlock_wrlock(&s.lock);
        uint64_t v = next_random(&random);
        if (sof_heap_push(&s.h, &v) != SOF_HEAP_OK) {
            errors++;
        }
        if (next_random(&random) % 3 == 0) {
            if (sof_heap_pop(&s.h, NULL) != SOF_HEAP_OK) {
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
    CHECK(sof_heap_free(&s.h) == SOF_HEAP_OK);
    pthread_rwlock_destroy(&s.lock);
}

/// @brief The functions that only read take a const heap.
static void test_const_heap(void) {
    sof_heap_t h;
    counts_t counts = {0};
    uint64_t total = 0;
    CHECK(sof_heap_init(&h, sizeof(uint64_t), 0, compare, NULL, NULL, NULL) == SOF_HEAP_OK);
    CHECK(push_range(&h, 1, 11));
    const sof_heap_t *ch = &h;
    const void *top = sof_heap_peek(ch);
    CHECK(top != NULL && value(top) == 1);
    CHECK(sof_heap_next(ch, top) != NULL);
    CHECK(sum(ch) == 55);
    CHECK(sof_heap_traverse(ch, count_visit, &counts) == 0 && counts.visits == 10);
    CHECK(sof_heap_traverse(ch, add_up, &total) == 0 && total == 55);
    CHECK(h.count == 10 && is_heap(ch));
    CHECK(sof_heap_free(&h) == SOF_HEAP_OK);
}

int main(void) {
    printf("sof_heap:\n");
    run("init", test_init);
    run("bad init arguments", test_bad_init);
    run("empty heap", test_empty);
    run("smallest first", test_order);
    run("largest first, through ctx", test_reverse);
    run("equal elements", test_duplicates);
    run("array growth", test_growth);
    run("elements of an odd size", test_odd_size);
    run("pushing an element of the heap itself", test_push_own_element);
    run("peek", test_peek);
    run("next", test_next);
    run("traverse", test_traverse);
    run("traversal stops early", test_traverse_stop);
    run("read through a const heap", test_const_heap);
    run("remove from anywhere", test_remove);
    run("remove moving the last element up", test_remove_moves_up);
    run("replace", test_replace);
    run("del_func sees every dropped element", test_delete_func);
    run("random pushes, pops, removes and replaces", test_random);
    run("NULL arguments", test_null);
    run("failed init", test_failed_init);
    run("freed heap", test_freed);
    run("custom allocator", test_custom_allocator);
    run("out of memory", test_no_memory);
    run("producers and consumers on threads", test_threads);
    run("readers alongside a writer on threads", test_threads_readers);
    return test_report();
}
