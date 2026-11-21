/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_stack.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sof_stack.h"
#include "test.h"

/// @brief Threads, and items each producer pushes, in the threaded tests.
#define PRODUCERS 4
#define CONSUMERS 4
#define ITEMS 20000
#define READERS 4
#define PASSES 2000

/// @brief Counts calls of the callbacks, through their ctx.
typedef struct counts_t {
    size_t deletes;
    /// @brief The sum of the elements deleted.
    uint64_t deleted_sum;
    /// @brief The element deleted before the latest one, to check the order.
    uint64_t last_deleted;
    /// @brief Deletions that came out of order: each must be lower than the
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

/// @brief Reads an element of a stack of uint64_t, which may be unaligned
/// for a stack of another size.
static uint64_t value(const void *element) {
    uint64_t v;
    memcpy(&v, element, sizeof(v));
    return v;
}

/// @brief Pushes the values from to to - 1.
static int push_range(sof_stack_t *s, uint64_t from, uint64_t to) {
    for (uint64_t i = from; i < to; i++) {
        if (sof_stack_push(s, &i) != SOF_STACK_OK) {
            return 0;
        }
    }
    return 1;
}

static void count_delete(void *element, void *ctx) {
    counts_t *counts = ctx;
    uint64_t v = value(element);
    if (counts->deletes > 0 && v >= counts->last_deleted) {
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

/// @brief Checks that a traversal sees the elements in falling order, from
/// the ctx's value down.
static int check_falling(void *element, void *ctx) {
    uint64_t *next = ctx;
    if (value(element) != *next) {
        return 1;
    }
    (*next)--;
    return 0;
}

/// @brief Doubles each element, to check that callbacks can change them.
static int double_it(void *element, void *ctx) {
    uint64_t v = value(element) * 2;
    memcpy(element, &v, sizeof(v));
    return 0;
}

static void test_init(void) {
    sof_stack_t s;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(s.ready == 1);
    CHECK(s.elements == NULL);
    CHECK(s.element_size == sizeof(uint64_t));
    CHECK(s.count == 0);
    CHECK(s.capacity == 0);
    CHECK(s.del_func == NULL);
    CHECK(s.alloc.malloc == NULL);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);

    counts_t counts = {0};
    CHECK(sof_stack_init(&s, 5, 100, count_delete, &counts, NULL) == SOF_STACK_OK);
    CHECK(s.elements != NULL);
    CHECK(s.capacity == 100);
    CHECK(s.element_size == 5);
    CHECK(s.del_func == count_delete);
    CHECK(s.ctx == &counts);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

static void test_bad_init(void) {
    sof_stack_t s;
    CHECK(sof_stack_init(&s, 0, 0, NULL, NULL, NULL) == SOF_STACK_ERROR);
    CHECK(s.ready == 0);
    CHECK(sof_stack_init(&s, 0, 10, NULL, NULL, NULL) == SOF_STACK_ERROR);
    CHECK(s.ready == 0);

    // A capacity whose size overflows can't be allocated.
    CHECK(sof_stack_init(&s, 16, SIZE_MAX / 8, NULL, NULL, NULL) == SOF_STACK_NO_MEM);
    CHECK(s.ready == 0);
    CHECK(s.elements == NULL);
    CHECK(sof_stack_push(&s, &(uint64_t){1}) == SOF_STACK_ERROR);
}

static void test_empty(void) {
    sof_stack_t s;
    uint64_t v = 7;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(sof_stack_peek(&s) == NULL);
    CHECK(sof_stack_pop(&s, &v) == SOF_STACK_NO_MATCH);
    CHECK(v == 7);
    CHECK(sof_stack_pop(&s, NULL) == SOF_STACK_NO_MATCH);
    CHECK(sof_stack_traverse(&s, count_visit, &(counts_t){0}) == 0);
    CHECK(sof_stack_next(&s, &v) == NULL);
    CHECK(sof_stack_clear(&s) == SOF_STACK_OK);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

static void test_lifo(void) {
    sof_stack_t s;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    for (uint64_t i = 1; i <= 1000; i++) {
        CHECK(sof_stack_push(&s, &i) == SOF_STACK_OK);
        CHECK(s.count == i);
        CHECK(s.capacity >= s.count);
        CHECK(value(sof_stack_peek(&s)) == i);
        CHECK(value(s.elements) == 1);
    }
    for (uint64_t i = 1000; i >= 1; i--) {
        uint64_t v = 0;
        CHECK(value(sof_stack_peek(&s)) == i);
        CHECK(sof_stack_pop(&s, &v) == SOF_STACK_OK);
        CHECK(v == i);
        CHECK(s.count == i - 1);
    }
    CHECK(sof_stack_peek(&s) == NULL);

    // The stack works again after being emptied.
    uint64_t five = 5;
    CHECK(sof_stack_push(&s, &five) == SOF_STACK_OK);
    CHECK(value(sof_stack_peek(&s)) == 5);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

static void test_growth(void) {
    // The array doubles from the capacity given to sof_stack_init(), or from 16.
    sof_stack_t s;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(push_range(&s, 0, 1));
    CHECK(s.capacity == 16);
    CHECK(push_range(&s, 1, 17));
    CHECK(s.capacity == 32);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);

    CHECK(sof_stack_init(&s, sizeof(uint64_t), 1, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(s.capacity == 1);
    CHECK(push_range(&s, 0, 1));
    CHECK(s.capacity == 1);
    CHECK(push_range(&s, 1, 2));
    CHECK(s.capacity == 2);
    CHECK(push_range(&s, 2, 3));
    CHECK(s.capacity == 4);
    CHECK(push_range(&s, 3, 5));
    CHECK(s.capacity == 8);
    uint64_t next = 4;
    CHECK(sof_stack_traverse(&s, check_falling, &next) == 0);
    CHECK(next == (uint64_t)-1);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

static void test_odd_size(void) {
    sof_stack_t s;
    CHECK(sof_stack_init(&s, sizeof(triple_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(s.element_size == 3);
    for (unsigned i = 0; i < 300; i++) {
        triple_t t = {{(unsigned char)i, (unsigned char)(i >> 8), (unsigned char)(i * 7)}};
        CHECK(sof_stack_push(&s, &t) == SOF_STACK_OK);
    }
    for (unsigned i = 300; i-- > 0;) {
        triple_t t;
        CHECK(sof_stack_pop(&s, &t) == SOF_STACK_OK);
        CHECK(t.bytes[0] == (unsigned char)i);
        CHECK(t.bytes[1] == (unsigned char)(i >> 8));
        CHECK(t.bytes[2] == (unsigned char)(i * 7));
    }
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

static void test_push_own_element(void) {
    // Pushing the top element when the array is full must copy it before
    // the array moves.
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_stack_t s;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 1, NULL, NULL, &alloc) == SOF_STACK_OK);
    CHECK(push_range(&s, 41, 42));
    for (size_t i = 0; i < 10; i++) {
        size_t capacity = s.capacity;
        CHECK(sof_stack_push(&s, sof_stack_peek(&s)) == SOF_STACK_OK);
        CHECK(s.count == i + 2);
        CHECK(i + 1 < capacity || s.capacity == capacity * 2);
    }
    for (void *e = sof_stack_peek(&s); e != NULL; e = sof_stack_next(&s, e)) {
        CHECK(value(e) == 41);
    }

    // So must pushing the bottom one.
    while (s.count < s.capacity) {
        CHECK(sof_stack_push(&s, s.elements) == SOF_STACK_OK);
    }
    uint64_t bottom = 7;
    memcpy(s.elements, &bottom, sizeof(bottom));
    CHECK(sof_stack_push(&s, s.elements) == SOF_STACK_OK);
    CHECK(value(sof_stack_peek(&s)) == 7);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_peek(void) {
    sof_stack_t s;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(push_range(&s, 0, 10));
    void *top = sof_stack_peek(&s);
    CHECK(top == s.elements + 9 * sizeof(uint64_t));

    // The top element can be changed in place.
    uint64_t v = 99;
    memcpy(top, &v, sizeof(v));
    CHECK(sof_stack_pop(&s, &v) == SOF_STACK_OK);
    CHECK(v == 99);
    CHECK(value(sof_stack_peek(&s)) == 8);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

static void test_next(void) {
    sof_stack_t s;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(push_range(&s, 0, 100));
    uint64_t expected = 99;
    size_t walked = 0;
    for (void *e = sof_stack_peek(&s); e != NULL; e = sof_stack_next(&s, e)) {
        CHECK(value(e) == expected);
        expected--;
        walked++;
    }
    CHECK(walked == 100);
    CHECK(sof_stack_next(&s, s.elements) == NULL);
    CHECK(sof_stack_next(&s, NULL) == NULL);

    // Pointers that aren't elements of the stack give NULL: one elsewhere,
    // one into the middle of an element, one past the top, and one into the
    // array's unused room.
    uint64_t other = 5;
    CHECK(sof_stack_next(&s, &other) == NULL);
    CHECK(sof_stack_next(&s, s.elements + sizeof(uint64_t) + 1) == NULL);
    CHECK(sof_stack_next(&s, s.elements + 100 * sizeof(uint64_t)) == NULL);
    CHECK(s.capacity > 101);
    CHECK(sof_stack_next(&s, s.elements + 101 * sizeof(uint64_t)) == NULL);

    // An element above the top, once popped, isn't one any more.
    void *top = sof_stack_peek(&s);
    CHECK(sof_stack_pop(&s, NULL) == SOF_STACK_OK);
    CHECK(sof_stack_next(&s, top) == NULL);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

static void test_traverse(void) {
    sof_stack_t s;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(push_range(&s, 0, 100));
    uint64_t next = 99;
    CHECK(sof_stack_traverse(&s, check_falling, &next) == 0);
    CHECK(next == (uint64_t)-1);

    counts_t counts = {0};
    CHECK(sof_stack_traverse(&s, count_visit, &counts) == 0);
    CHECK(counts.visits == 100);

    // Callbacks may change the elements.
    CHECK(sof_stack_traverse(&s, double_it, NULL) == 0);
    for (uint64_t i = 100; i-- > 0;) {
        uint64_t v;
        CHECK(sof_stack_pop(&s, &v) == SOF_STACK_OK);
        CHECK(v == i * 2);
    }

    // A NULL callback visits nothing.
    CHECK(push_range(&s, 0, 10));
    CHECK(sof_stack_traverse(&s, NULL, NULL) == 0);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

static void test_traverse_stop(void) {
    sof_stack_t s;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(push_range(&s, 0, 100));
    counts_t counts = {0};
    counts.stop_after = 1;
    CHECK(sof_stack_traverse(&s, count_visit, &counts) == 42);
    CHECK(counts.visits == 1);
    counts.visits = 0;
    counts.stop_after = 37;
    CHECK(sof_stack_traverse(&s, count_visit, &counts) == 42);
    CHECK(counts.visits == 37);
    counts.visits = 0;
    counts.stop_after = 100;
    CHECK(sof_stack_traverse(&s, count_visit, &counts) == 42);
    CHECK(counts.visits == 100);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

static void test_delete_func(void) {
    sof_stack_t s;
    counts_t counts = {0};
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, count_delete, &counts, NULL) == SOF_STACK_OK);
    CHECK(push_range(&s, 1, 11));

    // An element copied out to the caller doesn't go to the del_func.
    uint64_t v;
    CHECK(sof_stack_pop(&s, &v) == SOF_STACK_OK);
    CHECK(v == 10);
    CHECK(counts.deletes == 0);

    // A dropped one does.
    CHECK(sof_stack_pop(&s, NULL) == SOF_STACK_OK);
    CHECK(counts.deletes == 1);
    CHECK(counts.deleted_sum == 9);
    CHECK(s.count == 8);

    // sof_stack_clear() gives it every element, from the top down, and keeps the
    // array, so the stack can be used again.
    size_t capacity = s.capacity;
    counts = (counts_t){0};
    CHECK(sof_stack_clear(&s) == SOF_STACK_OK);
    CHECK(counts.deletes == 8);
    CHECK(counts.deleted_sum == 36);
    CHECK(counts.out_of_order == 0);
    CHECK(s.count == 0);
    CHECK(s.capacity == capacity);
    CHECK(s.elements != NULL);
    CHECK(s.ready == 1);
    CHECK(sof_stack_clear(&s) == SOF_STACK_OK);
    CHECK(counts.deletes == 8);

    // So does sof_stack_free().
    CHECK(push_range(&s, 1, 6));
    counts = (counts_t){0};
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
    CHECK(counts.deletes == 5);
    CHECK(counts.deleted_sum == 15);
    CHECK(counts.out_of_order == 0);
}

static void test_truncate(void) {
    sof_stack_t s;
    counts_t counts = {0};
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, count_delete, &counts, NULL) == SOF_STACK_OK);
    CHECK(push_range(&s, 1, 11));
    size_t capacity = s.capacity;

    // Down to 4: 10 to 5 go to the del_func, from the top down, and 4 is on
    // top.
    CHECK(sof_stack_truncate(&s, 4) == SOF_STACK_OK);
    CHECK(s.count == 4);
    CHECK(counts.deletes == 6);
    CHECK(counts.deleted_sum == 45);
    CHECK(counts.out_of_order == 0);
    CHECK(value(sof_stack_peek(&s)) == 4);
    CHECK(s.capacity == capacity);

    // To the count it has, or more, removes nothing.
    counts = (counts_t){0};
    CHECK(sof_stack_truncate(&s, 4) == SOF_STACK_OK);
    CHECK(sof_stack_truncate(&s, 100) == SOF_STACK_OK);
    CHECK(s.count == 4);
    CHECK(counts.deletes == 0);

    // The stack goes on working above the new top.
    uint64_t v = 20;
    CHECK(sof_stack_push(&s, &v) == SOF_STACK_OK);
    CHECK(sof_stack_pop(&s, &v) == SOF_STACK_OK);
    CHECK(v == 20);

    // To 0, it empties the stack, as sof_stack_clear() does.
    CHECK(sof_stack_truncate(&s, 0) == SOF_STACK_OK);
    CHECK(s.count == 0);
    CHECK(counts.deletes == 4);
    CHECK(counts.deleted_sum == 10);
    CHECK(counts.out_of_order == 0);
    CHECK(sof_stack_peek(&s) == NULL);
    CHECK(sof_stack_truncate(&s, 0) == SOF_STACK_OK);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);

    // Without a del_func, only the count changes.
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(push_range(&s, 1, 6));
    CHECK(sof_stack_truncate(&s, 2) == SOF_STACK_OK);
    CHECK(s.count == 2);
    CHECK(value(sof_stack_peek(&s)) == 2);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

static void test_random(void) {
    // Random pushes and pops, checked against a copy kept in an array.
    sof_stack_t s;
    uint64_t state = 0x9e3779b97f4a7c15ULL;
    static uint64_t model[100000];
    size_t count = 0;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    for (size_t i = 0; i < 100000; i++) {
        uint64_t r = next_random(&state);
        if (r % 3 != 0) {
            CHECK(sof_stack_push(&s, &r) == SOF_STACK_OK);
            model[count++] = r;
        } else {
            uint64_t v;
            sof_stack_error_t rc = sof_stack_pop(&s, &v);
            if (count == 0) {
                CHECK(rc == SOF_STACK_NO_MATCH);
            } else {
                CHECK(rc == SOF_STACK_OK);
                CHECK(v == model[--count]);
            }
        }
        CHECK(s.count == count);
    }
    CHECK(count > 0);
    size_t i = count;
    for (void *e = sof_stack_peek(&s); e != NULL; e = sof_stack_next(&s, e)) {
        CHECK(i > 0);
        CHECK(value(e) == model[--i]);
    }
    CHECK(i == 0);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

static void test_null(void) {
    uint64_t v = 3;
    CHECK(sof_stack_init(NULL, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_ERROR);
    CHECK(sof_stack_clear(NULL) == SOF_STACK_ERROR);
    CHECK(sof_stack_free(NULL) == SOF_STACK_ERROR);
    CHECK(sof_stack_next(NULL, &v) == NULL);
    CHECK(sof_stack_peek(NULL) == NULL);
    CHECK(sof_stack_pop(NULL, &v) == SOF_STACK_ERROR);
    CHECK(sof_stack_pop(NULL, NULL) == SOF_STACK_ERROR);
    CHECK(v == 3);
    CHECK(sof_stack_push(NULL, &v) == SOF_STACK_ERROR);
    CHECK(sof_stack_traverse(NULL, count_visit, &(counts_t){0}) == -1);
    CHECK(sof_stack_truncate(NULL, 0) == SOF_STACK_ERROR);

    // A NULL element can't be pushed.
    sof_stack_t s;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(sof_stack_push(&s, NULL) == SOF_STACK_ERROR);
    CHECK(s.count == 0);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

/// @brief Checks that every function treats s as dead.
static void check_dead(sof_stack_t *s) {
    counts_t counts = {0};
    uint64_t v = 3;
    CHECK(s->ready == 0);
    CHECK(sof_stack_push(s, &v) == SOF_STACK_ERROR);
    CHECK(s->count == 0);
    CHECK(s->elements == NULL);
    CHECK(sof_stack_peek(s) == NULL);
    CHECK(sof_stack_next(s, &v) == NULL);
    CHECK(sof_stack_pop(s, &v) == SOF_STACK_ERROR);
    CHECK(v == 3);
    CHECK(sof_stack_traverse(s, count_visit, &counts) == -1);
    CHECK(counts.visits == 0);
    CHECK(sof_stack_truncate(s, 0) == SOF_STACK_ERROR);
    CHECK(sof_stack_clear(s) == SOF_STACK_ERROR);
    CHECK(sof_stack_free(s) == SOF_STACK_ERROR);
}

static void test_failed_init(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    sof_stack_t s;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 10, NULL, NULL, &alloc) == SOF_STACK_ERROR);
    check_dead(&s);
    CHECK(state.allocations == 0);

    CHECK(sof_stack_init(&s, 0, 10, NULL, NULL, NULL) == SOF_STACK_ERROR);
    check_dead(&s);

    // It can be initialized again.
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(push_range(&s, 0, 1));
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

static void test_freed(void) {
    sof_stack_t s;
    counts_t counts = {0};
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, count_delete, &counts, NULL) == SOF_STACK_OK);
    CHECK(push_range(&s, 0, 10));
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
    CHECK(counts.deletes == 10);
    check_dead(&s);
    CHECK(counts.deletes == 10);
}

static void test_custom_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_stack_t s;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, &alloc) == SOF_STACK_OK);
    CHECK(state.allocations == 0);
    CHECK(push_range(&s, 0, 1000));
    // 16, 32, ... 1024: one allocation and six reallocations.
    CHECK(state.allocations == 7);
    CHECK(state.live == 1);
    for (size_t i = 0; i < 500; i++) {
        CHECK(sof_stack_pop(&s, NULL) == SOF_STACK_OK);
    }
    CHECK(sof_stack_clear(&s) == SOF_STACK_OK);
    CHECK(state.live == 1);

    // A cleared stack keeps its array, so refilling it allocates nothing.
    CHECK(push_range(&s, 0, 1000));
    CHECK(state.allocations == 7);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // A capacity given to sof_stack_init() is allocated there.
    state = (test_alloc_t){0};
    CHECK(sof_stack_init(&s, 3, 50, NULL, NULL, &alloc) == SOF_STACK_OK);
    CHECK(state.allocations == 1);
    CHECK(state.live == 1);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_no_memory(void) {
    // Fail each allocation in turn, until a run makes none fail. A failed
    // init leaves the stack dead, a failed push leaves it unchanged, and
    // nothing may leak.
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_stack_t s;
        int failed = 0;
        sof_stack_error_t rc = sof_stack_init(&s, sizeof(uint64_t), 2, NULL, NULL, &alloc);
        if (rc == SOF_STACK_NO_MEM) {
            check_dead(&s);
            CHECK(state.live == 0);
            continue;
        }
        CHECK(rc == SOF_STACK_OK);
        uint64_t put = 0;
        for (uint64_t i = 0; i < 40; i++) {
            size_t capacity = s.capacity;
            void *elements = s.elements;
            rc = sof_stack_push(&s, &i);
            if (rc == SOF_STACK_NO_MEM) {
                failed = 1;
                CHECK(s.count == put);
                CHECK(s.capacity == capacity);
                CHECK(s.elements == elements);
            } else {
                CHECK(rc == SOF_STACK_OK);
                put++;
            }
        }
        CHECK(s.count == put);
        uint64_t previous = UINT64_MAX;
        size_t walked = 0;
        for (void *e = sof_stack_peek(&s); e != NULL; e = sof_stack_next(&s, e)) {
            CHECK(value(e) < previous);
            previous = value(e);
            walked++;
        }
        CHECK(walked == put);
        CHECK(sof_stack_free(&s) == SOF_STACK_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (!failed) {
            break;
        }
    }
}

/// @brief A stack shared by threads, with the lock and condition variable
/// the application supplies, as in the README's Threading section.
typedef struct shared_t {
    sof_stack_t s;
    pthread_mutex_t lock;
    pthread_cond_t ready;
    /// @brief Producers that haven't finished; consumers stop at 0 once the
    /// stack is empty.
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

/// @brief Pushes ITEMS items, numbered id * ITEMS + seq.
static void *produce(void *arg) {
    worker_t *w = arg;
    shared_t *s = w->shared;
    for (uint64_t seq = 0; seq < ITEMS; seq++) {
        uint64_t item = w->id * ITEMS + seq;
        pthread_mutex_lock(&s->lock);
        if (sof_stack_push(&s->s, &item) != SOF_STACK_OK) {
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

/// @brief Pops until the producers are done and the stack is empty.
static void *consume(void *arg) {
    worker_t *w = arg;
    shared_t *s = w->shared;
    while (1) {
        uint64_t item;
        pthread_mutex_lock(&s->lock);
        sof_stack_error_t rc;
        while ((rc = sof_stack_pop(&s->s, &item)) == SOF_STACK_NO_MATCH && s->producers_left > 0) {
            pthread_cond_wait(&s->ready, &s->lock);
        }
        pthread_mutex_unlock(&s->lock);
        if (rc == SOF_STACK_NO_MATCH) {
            return NULL;
        }
        if (rc != SOF_STACK_OK || item >= (uint64_t)PRODUCERS * ITEMS) {
            w->errors++;
            return NULL;
        }
        w->count++;
        w->sum += item;
    }
}

static void test_threads(void) {
    // Producers and consumers share one stack under a mutex, with a
    // condition variable so consumers wait for work. Every item must be
    // popped exactly once, without leaks.
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    shared_t s;
    CHECK(sof_stack_init(&s.s, sizeof(uint64_t), 0, NULL, NULL, &alloc) == SOF_STACK_OK);
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
    CHECK(s.s.count == 0);
    CHECK(sof_stack_free(&s.s) == SOF_STACK_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    pthread_cond_destroy(&s.ready);
    pthread_mutex_destroy(&s.lock);
}

/// @brief A stack shared by one writer and several readers under a
/// readers-writer lock.
typedef struct rw_shared_t {
    sof_stack_t s;
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

/// @brief Checks, for sof_stack_traverse(), that the items fall from the top
/// down and counts them. The writer pushes rising numbers, so whatever it
/// has popped, the stack always holds them in rising order.
static int check_descending(void *element, void *ctx) {
    uint64_t *state = ctx; // state[0]: items seen, state[1]: the last item
    if (state[0] > 0 && value(element) >= state[1]) {
        return 1;
    }
    state[1] = value(element);
    state[0]++;
    return 0;
}

/// @brief Walks the stack PASSES times with sof_stack_traverse() and sof_stack_next()
/// under the read lock, checking what it sees each time.
static void *read_stack(void *arg) {
    reader_t *r = arg;
    rw_shared_t *s = r->shared;
    for (size_t pass = 0; pass < PASSES; pass++) {
        pthread_rwlock_rdlock(&s->lock);
        uint64_t state[2] = {0, 0};
        if (sof_stack_traverse(&s->s, check_descending, state) != 0 || state[0] != s->s.count) {
            r->errors++;
        }
        size_t walked = 0;
        for (void *e = sof_stack_peek(&s->s); e != NULL; e = sof_stack_next(&s->s, e)) {
            walked++;
        }
        if (walked != s->s.count) {
            r->errors++;
        }
        pthread_rwlock_unlock(&s->lock);
        r->passes++;
    }
    return NULL;
}

static void test_threads_readers(void) {
    // One writer pushes rising numbers and pops at random, growing the array
    // as it goes, while readers walk the stack under a read lock and must
    // always see falling numbers, as many as count.
    rw_shared_t s;
    CHECK(sof_stack_init(&s.s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(pthread_rwlock_init(&s.lock, NULL) == 0);
    reader_t readers[READERS] = {{0}};
    pthread_t threads[READERS];
    for (size_t i = 0; i < READERS; i++) {
        readers[i].shared = &s;
        CHECK(pthread_create(&threads[i], NULL, read_stack, &readers[i]) == 0);
    }
    uint64_t random = 0x2545f4914f6cdd1dULL;
    size_t errors = 0;
    for (uint64_t i = 0; i < ITEMS; i++) {
        pthread_rwlock_wrlock(&s.lock);
        if (sof_stack_push(&s.s, &i) != SOF_STACK_OK) {
            errors++;
        }
        if (next_random(&random) % 3 == 0) {
            if (sof_stack_pop(&s.s, NULL) != SOF_STACK_OK) {
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
    CHECK(sof_stack_free(&s.s) == SOF_STACK_OK);
    pthread_rwlock_destroy(&s.lock);
}

/// @brief The functions that only read take a const stack, and hand back
/// elements that can still be changed.
static void test_const_stack(void) {
    sof_stack_t s;
    counts_t counts = {0};
    uint64_t next = 9;
    CHECK(sof_stack_init(&s, sizeof(uint64_t), 0, NULL, NULL, NULL) == SOF_STACK_OK);
    CHECK(push_range(&s, 0, 10));
    const sof_stack_t *cs = &s;
    void *top = sof_stack_peek(cs);
    CHECK(top != NULL && value(top) == 9);
    CHECK(value(sof_stack_next(cs, top)) == 8);
    CHECK(sof_stack_traverse(cs, count_visit, &counts) == 0 && counts.visits == 10);
    CHECK(sof_stack_traverse(cs, check_falling, &next) == 0);
    CHECK(sof_stack_traverse(cs, double_it, NULL) == 0);
    CHECK(value(sof_stack_peek(cs)) == 18);
    CHECK(s.count == 10);
    CHECK(sof_stack_free(&s) == SOF_STACK_OK);
}

int main(void) {
    printf("sof_stack:\n");
    run("init", test_init);
    run("bad init arguments", test_bad_init);
    run("empty stack", test_empty);
    run("last in, first out", test_lifo);
    run("array growth", test_growth);
    run("elements of an odd size", test_odd_size);
    run("pushing an element of the stack itself", test_push_own_element);
    run("peek", test_peek);
    run("next", test_next);
    run("traverse", test_traverse);
    run("traversal stops early", test_traverse_stop);
    run("read through a const stack", test_const_stack);
    run("del_func sees every dropped element", test_delete_func);
    run("truncate", test_truncate);
    run("random pushes and pops", test_random);
    run("NULL arguments", test_null);
    run("failed init", test_failed_init);
    run("freed stack", test_freed);
    run("custom allocator", test_custom_allocator);
    run("out of memory", test_no_memory);
    run("producers and consumers on threads", test_threads);
    run("readers alongside a writer on threads", test_threads_readers);
    return test_report();
}
