/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_ring.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "sof_ring.h"
#include "test.h"

/// @brief Threads, items each producer pushes, and the ring's capacity, in
/// the threaded tests.
#define PRODUCERS 4
#define CONSUMERS 4
#define ITEMS 20000
#define SHARED_CAPACITY 64
#define READERS 4
#define PASSES 2000

/// @brief Counts calls of the callbacks, through their ctx.
typedef struct counts_t {
    size_t deletes;
    /// @brief The sum of the elements deleted.
    uint64_t deleted_sum;
    /// @brief The element deleted before the latest one, to check the order.
    uint64_t last_deleted;
    /// @brief Deletions that came out of order: each must be higher than the
    /// one before, since elements are pushed in rising order.
    size_t out_of_order;
    size_t visits;
    /// @brief The number of visits after which the traversal stops, or 0.
    size_t stop_after;
} counts_t;

/// @brief An element of an odd size, to check that nothing assumes alignment
/// or a power of two.
typedef struct triple_t {
    unsigned char bytes[3];
} triple_t;

/// @brief Reads an element of a ring of uint64_t, which may be unaligned for
/// a ring of another size.
static uint64_t value(const void *element) {
    uint64_t v;
    memcpy(&v, element, sizeof(v));
    return v;
}

/// @brief Pushes the values from to to - 1, failing if any doesn't fit.
static int push_range(sof_ring_t *r, uint64_t from, uint64_t to) {
    for (uint64_t i = from; i < to; i++) {
        if (sof_ring_push(r, &i) != SOF_RING_OK) {
            return 0;
        }
    }
    return 1;
}

/// @brief Pushes the values from to to - 1 with sof_ring_push_overwrite().
static int overwrite_range(sof_ring_t *r, uint64_t from, uint64_t to) {
    for (uint64_t i = from; i < to; i++) {
        if (sof_ring_push_overwrite(r, &i) != SOF_RING_OK) {
            return 0;
        }
    }
    return 1;
}

/// @brief Returns 1 if the ring holds exactly the values from to to - 1,
/// oldest first, by sof_ring_at() and by sof_ring_peek() and sof_ring_next().
static int holds_range(const sof_ring_t *r, uint64_t from, uint64_t to) {
    if (r->count != to - from) {
        return 0;
    }
    for (size_t i = 0; i < r->count; i++) {
        if (value(sof_ring_at(r, i)) != from + i) {
            return 0;
        }
    }
    uint64_t expected = from;
    for (void *e = sof_ring_peek(r); e != NULL; e = sof_ring_next(r, e)) {
        if (value(e) != expected++) {
            return 0;
        }
    }
    return expected == to;
}

static void count_delete(void *element, void *ctx) {
    counts_t *counts = ctx;
    uint64_t v = value(element);
    if (counts->deletes > 0 && v <= counts->last_deleted) {
        counts->out_of_order++;
    }
    counts->last_deleted = v;
    counts->deletes++;
    counts->deleted_sum += v;
}

static int count_visit(void *element, void *ctx) {
    counts_t *counts = ctx;
    counts->visits++;
    if (counts->stop_after != 0 && counts->visits == counts->stop_after) {
        return 42;
    }
    return 0;
}

/// @brief Checks that a traversal sees the elements in rising order, from
/// the ctx's value up.
static int check_rising(void *element, void *ctx) {
    uint64_t *next = ctx;
    if (value(element) != *next) {
        return 1;
    }
    (*next)++;
    return 0;
}

/// @brief Doubles each element, to check that callbacks can change them.
static int double_it(void *element, void *ctx) {
    uint64_t v = value(element) * 2;
    memcpy(element, &v, sizeof(v));
    return 0;
}

static void test_init(void) {
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 8, NULL, NULL, NULL) == SOF_RING_OK);
    CHECK(r.ready == 1);
    CHECK(r.elements != NULL);
    CHECK(r.element_size == sizeof(uint64_t));
    CHECK(r.capacity == 8);
    CHECK(r.count == 0);
    CHECK(r.head == 0);
    CHECK(r.del_func == NULL);
    CHECK(r.alloc.malloc == NULL);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);

    counts_t counts = {0};
    CHECK(sof_ring_init(&r, 5, 100, count_delete, &counts, NULL) == SOF_RING_OK);
    CHECK(r.capacity == 100);
    CHECK(r.element_size == 5);
    CHECK(r.del_func == count_delete);
    CHECK(r.ctx == &counts);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_bad_init(void) {
    sof_ring_t r;
    CHECK(sof_ring_init(&r, 0, 10, NULL, NULL, NULL) == SOF_RING_ERROR);
    CHECK(r.ready == 0);
    CHECK(sof_ring_init(&r, 8, 0, NULL, NULL, NULL) == SOF_RING_ERROR);
    CHECK(r.ready == 0);

    // A capacity whose size overflows can't be allocated.
    CHECK(sof_ring_init(&r, 16, SIZE_MAX / 8, NULL, NULL, NULL) == SOF_RING_NO_MEM);
    CHECK(r.ready == 0);
    CHECK(r.elements == NULL);
    CHECK(sof_ring_push(&r, &(uint64_t){1}) == SOF_RING_ERROR);
}

static void test_empty(void) {
    sof_ring_t r;
    uint64_t v = 7;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 4, NULL, NULL, NULL) == SOF_RING_OK);
    CHECK(sof_ring_peek(&r) == NULL);
    CHECK(sof_ring_at(&r, 0) == NULL);
    CHECK(sof_ring_pop(&r, &v) == SOF_RING_NO_MATCH);
    CHECK(v == 7);
    CHECK(sof_ring_pop(&r, NULL) == SOF_RING_NO_MATCH);
    CHECK(sof_ring_traverse(&r, count_visit, &(counts_t){0}) == 0);
    CHECK(sof_ring_next(&r, r.elements) == NULL);
    CHECK(sof_ring_clear(&r) == SOF_RING_OK);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_fifo(void) {
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 1000, NULL, NULL, NULL) == SOF_RING_OK);
    for (uint64_t i = 1; i <= 1000; i++) {
        CHECK(sof_ring_push(&r, &i) == SOF_RING_OK);
        CHECK(r.count == i);
        CHECK(value(sof_ring_peek(&r)) == 1);
        CHECK(value(sof_ring_at(&r, r.count - 1)) == i);
    }
    for (uint64_t i = 1; i <= 1000; i++) {
        uint64_t v = 0;
        CHECK(value(sof_ring_peek(&r)) == i);
        CHECK(sof_ring_pop(&r, &v) == SOF_RING_OK);
        CHECK(v == i);
        CHECK(r.count == 1000 - i);
    }
    CHECK(sof_ring_peek(&r) == NULL);

    // The ring works again after being emptied.
    uint64_t five = 5;
    CHECK(sof_ring_push(&r, &five) == SOF_RING_OK);
    CHECK(value(sof_ring_peek(&r)) == 5);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_full(void) {
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 5, NULL, NULL, NULL) == SOF_RING_OK);
    CHECK(push_range(&r, 0, 5));
    uint64_t v = 99;
    CHECK(sof_ring_push(&r, &v) == SOF_RING_FULL);
    CHECK(holds_range(&r, 0, 5));

    // Popping makes room for one more.
    CHECK(sof_ring_pop(&r, &v) == SOF_RING_OK);
    CHECK(v == 0);
    CHECK(push_range(&r, 5, 6));
    CHECK(sof_ring_push(&r, &v) == SOF_RING_FULL);
    CHECK(holds_range(&r, 1, 6));
    CHECK(sof_ring_free(&r) == SOF_RING_OK);

    // A ring of one.
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 1, NULL, NULL, NULL) == SOF_RING_OK);
    for (uint64_t i = 0; i < 10; i++) {
        CHECK(sof_ring_push(&r, &i) == SOF_RING_OK);
        CHECK(sof_ring_push(&r, &i) == SOF_RING_FULL);
        CHECK(sof_ring_pop(&r, &v) == SOF_RING_OK);
        CHECK(v == i);
        CHECK(r.head == 0);
    }
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_wrap(void) {
    // Pushing and popping walks the elements around the array, so every
    // head position and every amount of wrapping is reached.
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 7, NULL, NULL, NULL) == SOF_RING_OK);
    uint64_t from = 0, to = 0;
    for (size_t round = 0; round < 50; round++) {
        size_t fill = round % 8;
        while (to - from < fill) {
            CHECK(push_range(&r, to, to + 1));
            to++;
        }
        CHECK(holds_range(&r, from, to));
        CHECK(r.head < r.capacity);
        if (r.count > 0) {
            uint64_t v;
            CHECK(sof_ring_pop(&r, &v) == SOF_RING_OK);
            CHECK(v == from);
            from++;
        }
    }
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_overwrite(void) {
    sof_ring_t r;
    counts_t counts = {0};
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 4, count_delete, &counts, NULL) == SOF_RING_OK);

    // Until the ring is full it is the same as sof_ring_push().
    CHECK(overwrite_range(&r, 0, 4));
    CHECK(holds_range(&r, 0, 4));
    CHECK(counts.deletes == 0);

    // Then each push replaces the oldest, which goes to the del_func.
    CHECK(overwrite_range(&r, 4, 100));
    CHECK(holds_range(&r, 96, 100));
    CHECK(r.count == 4);
    CHECK(counts.deletes == 96);
    CHECK(counts.deleted_sum == 95 * 96 / 2);
    CHECK(counts.out_of_order == 0);

    // Pushing the oldest element itself makes it the newest without
    // deleting it.
    CHECK(sof_ring_push_overwrite(&r, sof_ring_peek(&r)) == SOF_RING_OK);
    CHECK(counts.deletes == 96);
    CHECK(value(sof_ring_at(&r, 0)) == 97);
    CHECK(value(sof_ring_at(&r, 3)) == 96);

    // Pushing another element of the ring copies it.
    counts = (counts_t){0};
    CHECK(sof_ring_push_overwrite(&r, sof_ring_at(&r, 3)) == SOF_RING_OK);
    CHECK(counts.deletes == 1);
    CHECK(counts.deleted_sum == 97);
    CHECK(value(sof_ring_at(&r, 2)) == 96);
    CHECK(value(sof_ring_at(&r, 3)) == 96);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_odd_size(void) {
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(triple_t), 37, NULL, NULL, NULL) == SOF_RING_OK);
    CHECK(r.element_size == 3);
    for (unsigned i = 0; i < 300; i++) {
        triple_t t = {{(unsigned char)i, (unsigned char)(i >> 8), (unsigned char)(i * 7)}};
        CHECK(sof_ring_push_overwrite(&r, &t) == SOF_RING_OK);
    }
    for (unsigned i = 300 - 37; i < 300; i++) {
        triple_t t;
        CHECK(sof_ring_pop(&r, &t) == SOF_RING_OK);
        CHECK(t.bytes[0] == (unsigned char)i);
        CHECK(t.bytes[1] == (unsigned char)(i >> 8));
        CHECK(t.bytes[2] == (unsigned char)(i * 7));
    }
    CHECK(r.count == 0);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_push_own_element(void) {
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 8, NULL, NULL, NULL) == SOF_RING_OK);
    CHECK(push_range(&r, 41, 42));
    while (r.count < r.capacity) {
        CHECK(sof_ring_push(&r, sof_ring_peek(&r)) == SOF_RING_OK);
    }
    for (void *e = sof_ring_peek(&r); e != NULL; e = sof_ring_next(&r, e)) {
        CHECK(value(e) == 41);
    }
    CHECK(sof_ring_push(&r, sof_ring_peek(&r)) == SOF_RING_FULL);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_peek_at(void) {
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 10, NULL, NULL, NULL) == SOF_RING_OK);
    CHECK(overwrite_range(&r, 0, 15));
    CHECK(r.head == 5);
    void *front = sof_ring_peek(&r);
    CHECK(front == r.elements + 5 * sizeof(uint64_t));
    CHECK(front == sof_ring_at(&r, 0));
    CHECK(sof_ring_at(&r, 4) == r.elements + 9 * sizeof(uint64_t));
    CHECK(sof_ring_at(&r, 5) == r.elements);
    CHECK(value(sof_ring_at(&r, 9)) == 14);
    CHECK(sof_ring_at(&r, 10) == NULL);
    CHECK(sof_ring_at(&r, SIZE_MAX) == NULL);

    // The front element can be changed in place.
    uint64_t v = 99;
    memcpy(front, &v, sizeof(v));
    CHECK(sof_ring_pop(&r, &v) == SOF_RING_OK);
    CHECK(v == 99);
    CHECK(value(sof_ring_peek(&r)) == 6);

    // Element pointers stay good while other elements come and go.
    void *last = sof_ring_at(&r, r.count - 1);
    CHECK(sof_ring_pop(&r, NULL) == SOF_RING_OK);
    CHECK(push_range(&r, 100, 102));
    CHECK(value(last) == 14);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_next(void) {
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 100, NULL, NULL, NULL) == SOF_RING_OK);
    CHECK(overwrite_range(&r, 0, 130));
    uint64_t expected = 30;
    size_t walked = 0;
    for (void *e = sof_ring_peek(&r); e != NULL; e = sof_ring_next(&r, e)) {
        CHECK(value(e) == expected);
        expected++;
        walked++;
    }
    CHECK(walked == 100);
    CHECK(sof_ring_next(&r, sof_ring_at(&r, 99)) == NULL);
    CHECK(sof_ring_next(&r, NULL) == NULL);

    // Pointers that aren't elements of the ring give NULL: one elsewhere,
    // one into the middle of an element, and one past the end of the array.
    uint64_t other = 5;
    CHECK(sof_ring_next(&r, &other) == NULL);
    CHECK(sof_ring_next(&r, r.elements + sizeof(uint64_t) + 1) == NULL);
    CHECK(sof_ring_next(&r, r.elements + 100 * sizeof(uint64_t)) == NULL);

    // A popped element isn't one any more, and neither is a free slot.
    void *front = sof_ring_peek(&r);
    CHECK(sof_ring_pop(&r, NULL) == SOF_RING_OK);
    CHECK(sof_ring_next(&r, front) == NULL);
    CHECK(sof_ring_next(&r, sof_ring_peek(&r)) == sof_ring_at(&r, 1));
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_traverse(void) {
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 100, NULL, NULL, NULL) == SOF_RING_OK);
    CHECK(overwrite_range(&r, 0, 150));
    uint64_t next = 50;
    CHECK(sof_ring_traverse(&r, check_rising, &next) == 0);
    CHECK(next == 150);

    counts_t counts = {0};
    CHECK(sof_ring_traverse(&r, count_visit, &counts) == 0);
    CHECK(counts.visits == 100);

    // Callbacks may change the elements.
    CHECK(sof_ring_traverse(&r, double_it, NULL) == 0);
    for (uint64_t i = 50; i < 150; i++) {
        uint64_t v;
        CHECK(sof_ring_pop(&r, &v) == SOF_RING_OK);
        CHECK(v == i * 2);
    }

    // A NULL callback visits nothing.
    CHECK(push_range(&r, 0, 10));
    CHECK(sof_ring_traverse(&r, NULL, NULL) == 0);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_traverse_stop(void) {
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 100, NULL, NULL, NULL) == SOF_RING_OK);
    CHECK(push_range(&r, 0, 100));
    counts_t counts = {0};
    counts.stop_after = 1;
    CHECK(sof_ring_traverse(&r, count_visit, &counts) == 42);
    CHECK(counts.visits == 1);
    counts.visits = 0;
    counts.stop_after = 37;
    CHECK(sof_ring_traverse(&r, count_visit, &counts) == 42);
    CHECK(counts.visits == 37);
    counts.visits = 0;
    counts.stop_after = 100;
    CHECK(sof_ring_traverse(&r, count_visit, &counts) == 42);
    CHECK(counts.visits == 100);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_delete_func(void) {
    sof_ring_t r;
    counts_t counts = {0};
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 16, count_delete, &counts, NULL) == SOF_RING_OK);
    CHECK(push_range(&r, 1, 11));

    // An element copied out to the caller doesn't go to the del_func.
    uint64_t v;
    CHECK(sof_ring_pop(&r, &v) == SOF_RING_OK);
    CHECK(v == 1);
    CHECK(counts.deletes == 0);

    // A dropped one does.
    CHECK(sof_ring_pop(&r, NULL) == SOF_RING_OK);
    CHECK(counts.deletes == 1);
    CHECK(counts.deleted_sum == 2);
    CHECK(r.count == 8);

    // A full ring refusing a push deletes nothing.
    CHECK(push_range(&r, 11, 19));
    CHECK(sof_ring_push(&r, &v) == SOF_RING_FULL);
    CHECK(counts.deletes == 1);

    // sof_ring_clear() gives it every element, from the oldest, and keeps
    // the array, so the ring can be used again.
    unsigned char *elements = r.elements;
    counts = (counts_t){0};
    CHECK(sof_ring_clear(&r) == SOF_RING_OK);
    CHECK(counts.deletes == 16);
    CHECK(counts.deleted_sum == 168);
    CHECK(counts.out_of_order == 0);
    CHECK(r.count == 0);
    CHECK(r.elements == elements);
    CHECK(r.ready == 1);
    CHECK(sof_ring_clear(&r) == SOF_RING_OK);
    CHECK(counts.deletes == 16);

    // So does sof_ring_free(), with the elements wrapped around the array.
    CHECK(overwrite_range(&r, 1, 21));
    counts = (counts_t){0};
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
    CHECK(counts.deletes == 16);
    CHECK(counts.deleted_sum == 200);
    CHECK(counts.out_of_order == 0);
}

static void test_random(void) {
    // Random pushes, overwrites and pops, checked against a copy kept in an
    // array.
    sof_ring_t r;
    uint64_t state = 0x9e3779b97f4a7c15ULL;
    static uint64_t model[200000];
    size_t front = 0, back = 0;
    const size_t capacity = 61;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), capacity, NULL, NULL, NULL) == SOF_RING_OK);
    for (size_t i = 0; i < 100000; i++) {
        uint64_t v = next_random(&state);
        switch (v % 5) {
        case 0:
        case 1: {
            sof_ring_error_t rc = sof_ring_push(&r, &v);
            if (back - front == capacity) {
                CHECK(rc == SOF_RING_FULL);
            } else {
                CHECK(rc == SOF_RING_OK);
                model[back++] = v;
            }
            break;
        }
        case 2:
            CHECK(sof_ring_push_overwrite(&r, &v) == SOF_RING_OK);
            if (back - front == capacity) {
                front++;
            }
            model[back++] = v;
            break;
        default: {
            uint64_t got;
            sof_ring_error_t rc = sof_ring_pop(&r, &got);
            if (back == front) {
                CHECK(rc == SOF_RING_NO_MATCH);
            } else {
                CHECK(rc == SOF_RING_OK);
                CHECK(got == model[front++]);
            }
            break;
        }
        }
        CHECK(r.count == back - front);
        if (r.count > 0) {
            CHECK(value(sof_ring_at(&r, r.count - 1)) == model[back - 1]);
        }
    }
    CHECK(r.count > 0);
    size_t i = front;
    for (void *e = sof_ring_peek(&r); e != NULL; e = sof_ring_next(&r, e)) {
        CHECK(i < back);
        CHECK(value(e) == model[i++]);
    }
    CHECK(i == back);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_null(void) {
    uint64_t v = 3;
    CHECK(sof_ring_init(NULL, sizeof(uint64_t), 4, NULL, NULL, NULL) == SOF_RING_ERROR);
    CHECK(sof_ring_at(NULL, 0) == NULL);
    CHECK(sof_ring_clear(NULL) == SOF_RING_ERROR);
    CHECK(sof_ring_free(NULL) == SOF_RING_ERROR);
    CHECK(sof_ring_next(NULL, &v) == NULL);
    CHECK(sof_ring_peek(NULL) == NULL);
    CHECK(sof_ring_pop(NULL, &v) == SOF_RING_ERROR);
    CHECK(sof_ring_pop(NULL, NULL) == SOF_RING_ERROR);
    CHECK(v == 3);
    CHECK(sof_ring_push(NULL, &v) == SOF_RING_ERROR);
    CHECK(sof_ring_push_overwrite(NULL, &v) == SOF_RING_ERROR);
    CHECK(sof_ring_traverse(NULL, count_visit, &(counts_t){0}) == -1);

    // A NULL element can't be pushed.
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 1, NULL, NULL, NULL) == SOF_RING_OK);
    CHECK(sof_ring_push(&r, NULL) == SOF_RING_ERROR);
    CHECK(sof_ring_push_overwrite(&r, NULL) == SOF_RING_ERROR);
    CHECK(r.count == 0);
    CHECK(push_range(&r, 0, 1));
    CHECK(sof_ring_push_overwrite(&r, NULL) == SOF_RING_ERROR);
    CHECK(value(sof_ring_peek(&r)) == 0);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

/// @brief Checks that every function treats r as dead.
static void check_dead(sof_ring_t *r) {
    counts_t counts = {0};
    uint64_t v = 3;
    CHECK(r->ready == 0);
    CHECK(sof_ring_push(r, &v) == SOF_RING_ERROR);
    CHECK(sof_ring_push_overwrite(r, &v) == SOF_RING_ERROR);
    CHECK(r->count == 0);
    CHECK(r->elements == NULL);
    CHECK(sof_ring_peek(r) == NULL);
    CHECK(sof_ring_at(r, 0) == NULL);
    CHECK(sof_ring_next(r, &v) == NULL);
    CHECK(sof_ring_pop(r, &v) == SOF_RING_ERROR);
    CHECK(v == 3);
    CHECK(sof_ring_traverse(r, count_visit, &counts) == -1);
    CHECK(counts.visits == 0);
    CHECK(sof_ring_clear(r) == SOF_RING_ERROR);
    CHECK(sof_ring_free(r) == SOF_RING_ERROR);
}

static void test_failed_init(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 10, NULL, NULL, &alloc) == SOF_RING_ERROR);
    check_dead(&r);
    CHECK(state.allocations == 0);

    CHECK(sof_ring_init(&r, 0, 10, NULL, NULL, NULL) == SOF_RING_ERROR);
    check_dead(&r);
    CHECK(sof_ring_init(&r, 8, 0, NULL, NULL, NULL) == SOF_RING_ERROR);
    check_dead(&r);

    // It can be initialized again.
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 1, NULL, NULL, NULL) == SOF_RING_OK);
    CHECK(push_range(&r, 0, 1));
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

static void test_freed(void) {
    sof_ring_t r;
    counts_t counts = {0};
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 10, count_delete, &counts, NULL) == SOF_RING_OK);
    CHECK(push_range(&r, 0, 10));
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
    CHECK(counts.deletes == 10);
    check_dead(&r);
    CHECK(counts.deletes == 10);
}

static void test_custom_allocator(void) {
    // The array is allocated once, in sof_ring_init(), and never again.
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 50, NULL, NULL, &alloc) == SOF_RING_OK);
    CHECK(state.allocations == 1);
    CHECK(state.live == 1);
    CHECK(overwrite_range(&r, 0, 1000));
    for (size_t i = 0; i < 25; i++) {
        CHECK(sof_ring_pop(&r, NULL) == SOF_RING_OK);
    }
    CHECK(push_range(&r, 0, 25));
    CHECK(sof_ring_push(&r, &(uint64_t){0}) == SOF_RING_FULL);
    CHECK(sof_ring_clear(&r) == SOF_RING_OK);
    CHECK(state.allocations == 1);
    CHECK(state.live == 1);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // An odd size is freed with the size it was allocated with.
    state = (test_alloc_t){0};
    CHECK(sof_ring_init(&r, 3, 7, NULL, NULL, &alloc) == SOF_RING_OK);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_no_memory(void) {
    // sof_ring_init() makes the only allocation, so failing it leaves the
    // ring dead and allocates nothing else.
    test_alloc_t state = {0};
    state.fail_at = 1;
    sof_alloc_t alloc = test_allocator(&state);
    sof_ring_t r;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 10, NULL, NULL, &alloc) == SOF_RING_NO_MEM);
    check_dead(&r);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // With that one allocation made, nothing more is needed.
    state = (test_alloc_t){0};
    state.fail_at = 2;
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 10, NULL, NULL, &alloc) == SOF_RING_OK);
    CHECK(overwrite_range(&r, 0, 100));
    CHECK(holds_range(&r, 90, 100));
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
    CHECK(state.allocations == 1);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

/// @brief A bounded ring shared by threads, with the lock and condition
/// variables the application supplies, as in the README's Threading section.
typedef struct shared_t {
    sof_ring_t r;
    pthread_mutex_t lock;
    /// @brief Signalled when an element is pushed, for consumers.
    pthread_cond_t not_empty;
    /// @brief Signalled when an element is popped, for producers.
    pthread_cond_t not_full;
    /// @brief Producers that haven't finished; consumers stop at 0 once the
    /// ring is empty.
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
    /// @brief Calls that failed, items that can't have been pushed, or items
    /// from one producer that came out of order.
    size_t errors;
    /// @brief The last item seen from each producer, plus one, or 0.
    uint64_t last[PRODUCERS];
} worker_t;

/// @brief Pushes ITEMS items, numbered id * ITEMS + seq, waiting while the
/// ring is full.
static void *produce(void *arg) {
    worker_t *w = arg;
    shared_t *s = w->shared;
    for (uint64_t seq = 0; seq < ITEMS; seq++) {
        uint64_t item = w->id * ITEMS + seq;
        pthread_mutex_lock(&s->lock);
        sof_ring_error_t rc;
        while ((rc = sof_ring_push(&s->r, &item)) == SOF_RING_FULL) {
            pthread_cond_wait(&s->not_full, &s->lock);
        }
        if (rc != SOF_RING_OK) {
            w->errors++;
        }
        pthread_cond_signal(&s->not_empty);
        pthread_mutex_unlock(&s->lock);
    }
    pthread_mutex_lock(&s->lock);
    s->producers_left--;
    pthread_cond_broadcast(&s->not_empty);
    pthread_mutex_unlock(&s->lock);
    return NULL;
}

/// @brief Pops until the producers are done and the ring is empty. Each
/// producer's items must come out in the order it pushed them.
static void *consume(void *arg) {
    worker_t *w = arg;
    shared_t *s = w->shared;
    while (1) {
        uint64_t item;
        pthread_mutex_lock(&s->lock);
        sof_ring_error_t rc;
        while ((rc = sof_ring_pop(&s->r, &item)) == SOF_RING_NO_MATCH && s->producers_left > 0) {
            pthread_cond_wait(&s->not_empty, &s->lock);
        }
        if (rc == SOF_RING_OK) {
            pthread_cond_signal(&s->not_full);
        }
        pthread_mutex_unlock(&s->lock);
        if (rc == SOF_RING_NO_MATCH) {
            return NULL;
        }
        if (rc != SOF_RING_OK || item >= (uint64_t)PRODUCERS * ITEMS) {
            w->errors++;
            return NULL;
        }
        size_t producer = item / ITEMS;
        if (w->last[producer] > item % ITEMS) {
            w->errors++;
        }
        w->last[producer] = item % ITEMS + 1;
        w->count++;
        w->sum += item;
    }
}

static void test_threads(void) {
    // Producers and consumers share one small ring under a mutex, with
    // condition variables so consumers wait for work and producers for room.
    // Every item must be popped exactly once, without leaks.
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    shared_t s;
    CHECK(sof_ring_init(&s.r, sizeof(uint64_t), SHARED_CAPACITY, NULL, NULL, &alloc) == SOF_RING_OK);
    CHECK(pthread_mutex_init(&s.lock, NULL) == 0);
    CHECK(pthread_cond_init(&s.not_empty, NULL) == 0);
    CHECK(pthread_cond_init(&s.not_full, NULL) == 0);
    s.producers_left = PRODUCERS;

    static worker_t producers[PRODUCERS], consumers[CONSUMERS];
    memset(producers, 0, sizeof(producers));
    memset(consumers, 0, sizeof(consumers));
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
    uint64_t sum = 0;
    for (size_t i = 0; i < PRODUCERS; i++) {
        CHECK(producers[i].errors == 0);
    }
    for (size_t i = 0; i < CONSUMERS; i++) {
        CHECK(consumers[i].errors == 0);
        count += consumers[i].count;
        sum += consumers[i].sum;
    }
    uint64_t total = (uint64_t)PRODUCERS * ITEMS;
    CHECK(count == total);
    CHECK(sum == total * (total - 1) / 2);
    CHECK(s.r.count == 0);
    CHECK(sof_ring_free(&s.r) == SOF_RING_OK);
    CHECK(state.allocations == 1);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    pthread_cond_destroy(&s.not_full);
    pthread_cond_destroy(&s.not_empty);
    pthread_mutex_destroy(&s.lock);
}

/// @brief A ring shared by one writer and several readers under a
/// readers-writer lock.
typedef struct rw_shared_t {
    sof_ring_t r;
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

/// @brief Checks, for sof_ring_traverse(), that the items rise by one from
/// the oldest, and counts them. The writer pushes consecutive numbers, so
/// whatever it has popped or overwritten, the ring holds a run of them.
static int check_consecutive(void *element, void *ctx) {
    uint64_t *state = ctx; // state[0]: items seen, state[1]: the last item
    if (state[0] > 0 && value(element) != state[1] + 1) {
        return 1;
    }
    state[1] = value(element);
    state[0]++;
    return 0;
}

/// @brief Walks the ring PASSES times with sof_ring_traverse(),
/// sof_ring_next() and sof_ring_at() under the read lock, checking what it
/// sees each time.
static void *read_ring(void *arg) {
    reader_t *rd = arg;
    rw_shared_t *s = rd->shared;
    for (size_t pass = 0; pass < PASSES; pass++) {
        pthread_rwlock_rdlock(&s->lock);
        uint64_t state[2] = {0, 0};
        if (sof_ring_traverse(&s->r, check_consecutive, state) != 0 || state[0] != s->r.count) {
            rd->errors++;
        }
        size_t walked = 0;
        for (void *e = sof_ring_peek(&s->r); e != NULL; e = sof_ring_next(&s->r, e)) {
            if (e != sof_ring_at(&s->r, walked)) {
                rd->errors++;
            }
            walked++;
        }
        if (walked != s->r.count) {
            rd->errors++;
        }
        pthread_rwlock_unlock(&s->lock);
        rd->passes++;
    }
    return NULL;
}

static void test_threads_readers(void) {
    // One writer pushes consecutive numbers, overwriting the oldest when the
    // ring is full, and pops at random, while readers walk the ring under a
    // read lock and must always see a consecutive run, as many as count.
    rw_shared_t s;
    CHECK(sof_ring_init(&s.r, sizeof(uint64_t), SHARED_CAPACITY, NULL, NULL, NULL) == SOF_RING_OK);
    CHECK(pthread_rwlock_init(&s.lock, NULL) == 0);
    reader_t readers[READERS] = {{0}};
    pthread_t threads[READERS];
    for (size_t i = 0; i < READERS; i++) {
        readers[i].shared = &s;
        CHECK(pthread_create(&threads[i], NULL, read_ring, &readers[i]) == 0);
    }
    uint64_t random = 0x2545f4914f6cdd1dULL;
    size_t errors = 0;
    for (uint64_t i = 0; i < ITEMS; i++) {
        pthread_rwlock_wrlock(&s.lock);
        if (sof_ring_push_overwrite(&s.r, &i) != SOF_RING_OK) {
            errors++;
        }
        if (next_random(&random) % 3 == 0) {
            if (sof_ring_pop(&s.r, NULL) != SOF_RING_OK) {
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
    CHECK(sof_ring_free(&s.r) == SOF_RING_OK);
    pthread_rwlock_destroy(&s.lock);
}

/// @brief The functions that only read take a const ring, and hand back
/// elements that can still be changed.
static void test_const_ring(void) {
    sof_ring_t r;
    counts_t counts = {0};
    CHECK(sof_ring_init(&r, sizeof(uint64_t), 8, NULL, NULL, NULL) == SOF_RING_OK);
    CHECK(overwrite_range(&r, 0, 12));
    const sof_ring_t *cr = &r;
    CHECK(holds_range(cr, 4, 12));
    void *e = sof_ring_peek(cr);
    CHECK(e != NULL && value(e) == 4);
    CHECK(value(sof_ring_next(cr, e)) == 5);
    CHECK(value(sof_ring_at(cr, 7)) == 11);
    CHECK(sof_ring_at(cr, 8) == NULL);
    uint64_t v = 40;
    memcpy(sof_ring_at(cr, 0), &v, sizeof(v));
    CHECK(value(sof_ring_peek(cr)) == 40);
    CHECK(sof_ring_traverse(cr, count_visit, &counts) == 0 && counts.visits == 8);
    CHECK(sof_ring_free(&r) == SOF_RING_OK);
}

int main(void) {
    printf("sof_ring:\n");
    run("init", test_init);
    run("bad init arguments", test_bad_init);
    run("empty ring", test_empty);
    run("first in, first out", test_fifo);
    run("full ring", test_full);
    run("wrapping around the array", test_wrap);
    run("overwriting the oldest", test_overwrite);
    run("elements of an odd size", test_odd_size);
    run("pushing an element of the ring itself", test_push_own_element);
    run("peek and at", test_peek_at);
    run("next", test_next);
    run("traverse", test_traverse);
    run("traversal stops early", test_traverse_stop);
    run("read through a const ring", test_const_ring);
    run("del_func sees every dropped element", test_delete_func);
    run("random pushes, overwrites and pops", test_random);
    run("NULL arguments", test_null);
    run("failed init", test_failed_init);
    run("freed ring", test_freed);
    run("custom allocator", test_custom_allocator);
    run("out of memory", test_no_memory);
    run("producers and consumers on threads", test_threads);
    run("readers alongside a writer on threads", test_threads_readers);
    return test_report();
}
