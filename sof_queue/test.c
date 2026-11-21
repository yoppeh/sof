/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_queue.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <pthread.h>
#include <stdint.h>

#include "sof_queue.h"
#include "test.h"

/// @brief Threads, and items each producer enqueues, in the threaded tests.
#define PRODUCERS 4
#define CONSUMERS 4
#define ITEMS 20000
#define READERS 4
#define PASSES 2000

/// @brief Counts calls of the callbacks, through their ctx.
typedef struct counts_t {
    size_t deletes;
    /// @brief The sum of the data of the elements deleted, as integers.
    uintptr_t deleted_sum;
    size_t visits;
    /// @brief The number of visits after which the traversal stops, or 0.
    size_t stop_after;
} counts_t;

static void *as_data(uintptr_t n) {
    return (void *)n;
}

static uintptr_t as_int(const void *data) {
    return (uintptr_t)data;
}

static void count_delete(sof_queue_element_t *element, void *ctx) {
    counts_t *counts = ctx;
    counts->deletes++;
    counts->deleted_sum += as_int(element->data);
}

static int count_visit(sof_queue_element_t *element, void *ctx) {
    counts_t *counts = ctx;
    counts->visits++;
    if (counts->stop_after != 0 && counts->visits == counts->stop_after) {
        return 42;
    }
    return 0;
}

/// @brief Checks that a traversal sees the elements in order, numbered from
/// the ctx's next value.
static int check_order(sof_queue_element_t *element, void *ctx) {
    uintptr_t *next = ctx;
    if (as_int(element->data) != *next) {
        return 1;
    }
    (*next)++;
    return 0;
}

static void test_init(void) {
    sof_queue_t q;
    CHECK(sof_queue_init(&q, NULL, NULL, NULL) == SOF_QUEUE_OK);
    CHECK(q.ready == 1);
    CHECK(q.head == NULL);
    CHECK(q.tail == NULL);
    CHECK(q.count == 0);
    CHECK(q.del_func == NULL);
    CHECK(q.alloc.malloc == NULL);
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);

    counts_t counts = {0};
    CHECK(sof_queue_init(&q, count_delete, &counts, NULL) == SOF_QUEUE_OK);
    CHECK(q.del_func == count_delete);
    CHECK(q.ctx == &counts);
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
}

static void test_empty(void) {
    sof_queue_t q;
    void *data = as_data(7);
    CHECK(sof_queue_init(&q, NULL, NULL, NULL) == SOF_QUEUE_OK);
    CHECK(sof_queue_peek(&q) == NULL);
    CHECK(sof_queue_dequeue(&q, &data) == SOF_QUEUE_NO_MATCH);
    CHECK(data == as_data(7));
    CHECK(sof_queue_dequeue(&q, NULL) == SOF_QUEUE_NO_MATCH);
    CHECK(sof_queue_traverse(&q, count_visit, &(counts_t){0}) == 0);
    CHECK(sof_queue_clear(&q) == SOF_QUEUE_OK);
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
}

static void test_fifo(void) {
    sof_queue_t q;
    CHECK(sof_queue_init(&q, NULL, NULL, NULL) == SOF_QUEUE_OK);
    for (uintptr_t i = 1; i <= 1000; i++) {
        CHECK(sof_queue_enqueue(&q, as_data(i)) == SOF_QUEUE_OK);
        CHECK(q.count == i);
        CHECK(q.head->data == as_data(1));
        CHECK(q.tail->data == as_data(i));
    }
    for (uintptr_t i = 1; i <= 1000; i++) {
        void *data = NULL;
        CHECK(sof_queue_peek(&q)->data == as_data(i));
        CHECK(sof_queue_dequeue(&q, &data) == SOF_QUEUE_OK);
        CHECK(data == as_data(i));
        CHECK(q.count == 1000 - i);
    }
    CHECK(q.head == NULL);
    CHECK(q.tail == NULL);

    // The queue works again after being emptied.
    CHECK(sof_queue_enqueue(&q, as_data(5)) == SOF_QUEUE_OK);
    CHECK(q.head == q.tail);
    CHECK(sof_queue_peek(&q)->data == as_data(5));
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
}

static void test_null_data(void) {
    sof_queue_t q;
    void *data = as_data(1);
    CHECK(sof_queue_init(&q, NULL, NULL, NULL) == SOF_QUEUE_OK);
    CHECK(sof_queue_enqueue(&q, NULL) == SOF_QUEUE_OK);
    CHECK(sof_queue_enqueue(&q, as_data(2)) == SOF_QUEUE_OK);
    CHECK(q.count == 2);
    CHECK(sof_queue_peek(&q) != NULL);
    CHECK(sof_queue_peek(&q)->data == NULL);
    CHECK(sof_queue_dequeue(&q, &data) == SOF_QUEUE_OK);
    CHECK(data == NULL);
    CHECK(sof_queue_dequeue(&q, &data) == SOF_QUEUE_OK);
    CHECK(data == as_data(2));
    CHECK(sof_queue_dequeue(&q, &data) == SOF_QUEUE_NO_MATCH);
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
}

static void test_next(void) {
    sof_queue_t q;
    CHECK(sof_queue_init(&q, NULL, NULL, NULL) == SOF_QUEUE_OK);
    for (uintptr_t i = 0; i < 100; i++) {
        CHECK(sof_queue_enqueue(&q, as_data(i)) == SOF_QUEUE_OK);
    }
    uintptr_t expected = 0;
    for (sof_queue_element_t *e = sof_queue_peek(&q); e != NULL; e = sof_queue_next(&q, e)) {
        CHECK(e->data == as_data(expected));
        expected++;
    }
    CHECK(expected == 100);
    CHECK(sof_queue_next(&q, q.tail) == NULL);
    CHECK(sof_queue_next(&q, NULL) == NULL);
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
}

static void test_traverse(void) {
    sof_queue_t q;
    CHECK(sof_queue_init(&q, NULL, NULL, NULL) == SOF_QUEUE_OK);
    for (uintptr_t i = 0; i < 100; i++) {
        CHECK(sof_queue_enqueue(&q, as_data(i)) == SOF_QUEUE_OK);
    }
    uintptr_t next = 0;
    CHECK(sof_queue_traverse(&q, check_order, &next) == 0);
    CHECK(next == 100);

    counts_t counts = {0};
    CHECK(sof_queue_traverse(&q, count_visit, &counts) == 0);
    CHECK(counts.visits == 100);

    // A NULL callback visits nothing.
    CHECK(sof_queue_traverse(&q, NULL, NULL) == 0);
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
}

static void test_traverse_stop(void) {
    sof_queue_t q;
    CHECK(sof_queue_init(&q, NULL, NULL, NULL) == SOF_QUEUE_OK);
    for (uintptr_t i = 0; i < 100; i++) {
        CHECK(sof_queue_enqueue(&q, as_data(i)) == SOF_QUEUE_OK);
    }
    counts_t counts = {0};
    counts.stop_after = 1;
    CHECK(sof_queue_traverse(&q, count_visit, &counts) == 42);
    CHECK(counts.visits == 1);
    counts.visits = 0;
    counts.stop_after = 37;
    CHECK(sof_queue_traverse(&q, count_visit, &counts) == 42);
    CHECK(counts.visits == 37);
    counts.visits = 0;
    counts.stop_after = 100;
    CHECK(sof_queue_traverse(&q, count_visit, &counts) == 42);
    CHECK(counts.visits == 100);
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
}

static void test_delete_func(void) {
    sof_queue_t q;
    counts_t counts = {0};
    CHECK(sof_queue_init(&q, count_delete, &counts, NULL) == SOF_QUEUE_OK);
    for (uintptr_t i = 1; i <= 10; i++) {
        CHECK(sof_queue_enqueue(&q, as_data(i)) == SOF_QUEUE_OK);
    }

    // Data handed back to the caller doesn't go to the del_func.
    void *data;
    CHECK(sof_queue_dequeue(&q, &data) == SOF_QUEUE_OK);
    CHECK(data == as_data(1));
    CHECK(counts.deletes == 0);

    // Dropped data does.
    CHECK(sof_queue_dequeue(&q, NULL) == SOF_QUEUE_OK);
    CHECK(counts.deletes == 1);
    CHECK(counts.deleted_sum == 2);
    CHECK(q.count == 8);

    // sof_queue_clear() gives it every element, and the queue can be used again.
    CHECK(sof_queue_clear(&q) == SOF_QUEUE_OK);
    CHECK(counts.deletes == 9);
    CHECK(counts.deleted_sum == 55 - 1);
    CHECK(q.count == 0);
    CHECK(q.head == NULL);
    CHECK(q.tail == NULL);
    CHECK(q.ready == 1);
    CHECK(sof_queue_clear(&q) == SOF_QUEUE_OK);
    CHECK(counts.deletes == 9);

    // So does sof_queue_free().
    for (uintptr_t i = 1; i <= 5; i++) {
        CHECK(sof_queue_enqueue(&q, as_data(i)) == SOF_QUEUE_OK);
    }
    counts.deletes = 0;
    counts.deleted_sum = 0;
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
    CHECK(counts.deletes == 5);
    CHECK(counts.deleted_sum == 15);
}

static void test_random(void) {
    // Random enqueues and dequeues, checked against a counter at each end:
    // the queue always holds the numbers from taken to put - 1.
    sof_queue_t q;
    uint64_t state = 0x9e3779b97f4a7c15ULL;
    uintptr_t put = 0, taken = 0;
    CHECK(sof_queue_init(&q, NULL, NULL, NULL) == SOF_QUEUE_OK);
    for (size_t i = 0; i < 100000; i++) {
        if (next_random(&state) % 3 != 0) {
            CHECK(sof_queue_enqueue(&q, as_data(put)) == SOF_QUEUE_OK);
            put++;
        } else {
            void *data;
            sof_queue_error_t rc = sof_queue_dequeue(&q, &data);
            if (taken == put) {
                CHECK(rc == SOF_QUEUE_NO_MATCH);
            } else {
                CHECK(rc == SOF_QUEUE_OK);
                CHECK(data == as_data(taken));
                taken++;
            }
        }
        CHECK(q.count == put - taken);
    }
    uintptr_t next = taken;
    CHECK(sof_queue_traverse(&q, check_order, &next) == 0);
    CHECK(next == put);
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
}

static void test_null(void) {
    void *data = as_data(3);
    CHECK(sof_queue_init(NULL, NULL, NULL, NULL) == SOF_QUEUE_ERROR);
    CHECK(sof_queue_clear(NULL) == SOF_QUEUE_ERROR);
    CHECK(sof_queue_dequeue(NULL, &data) == SOF_QUEUE_ERROR);
    CHECK(sof_queue_dequeue(NULL, NULL) == SOF_QUEUE_ERROR);
    CHECK(data == as_data(3));
    CHECK(sof_queue_enqueue(NULL, data) == SOF_QUEUE_ERROR);
    CHECK(sof_queue_free(NULL) == SOF_QUEUE_ERROR);
    CHECK(sof_queue_shrink(NULL) == SOF_QUEUE_ERROR);
    CHECK(sof_queue_next(NULL, NULL) == NULL);
    CHECK(sof_queue_peek(NULL) == NULL);
    CHECK(sof_queue_traverse(NULL, count_visit, &(counts_t){0}) == -1);
}

/// @brief Checks that every function treats q as dead.
static void check_dead(sof_queue_t *q) {
    counts_t counts = {0};
    void *data = as_data(3);
    CHECK(q->ready == 0);
    CHECK(sof_queue_enqueue(q, as_data(1)) == SOF_QUEUE_ERROR);
    CHECK(q->count == 0);
    CHECK(q->head == NULL);
    CHECK(sof_queue_peek(q) == NULL);
    CHECK(sof_queue_dequeue(q, &data) == SOF_QUEUE_ERROR);
    CHECK(data == as_data(3));
    CHECK(sof_queue_traverse(q, count_visit, &counts) == -1);
    CHECK(counts.visits == 0);
    CHECK(sof_queue_clear(q) == SOF_QUEUE_ERROR);
    CHECK(sof_queue_shrink(q) == SOF_QUEUE_ERROR);
    CHECK(q->spare == NULL);
    CHECK(sof_queue_free(q) == SOF_QUEUE_ERROR);
}

static void test_failed_init(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    sof_queue_t q;
    CHECK(sof_queue_init(&q, NULL, NULL, &alloc) == SOF_QUEUE_ERROR);
    check_dead(&q);
    CHECK(state.allocations == 0);

    // It can be initialized again.
    CHECK(sof_queue_init(&q, NULL, NULL, NULL) == SOF_QUEUE_OK);
    CHECK(sof_queue_enqueue(&q, as_data(1)) == SOF_QUEUE_OK);
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
}

static void test_freed(void) {
    sof_queue_t q;
    counts_t counts = {0};
    CHECK(sof_queue_init(&q, count_delete, &counts, NULL) == SOF_QUEUE_OK);
    for (uintptr_t i = 0; i < 10; i++) {
        CHECK(sof_queue_enqueue(&q, as_data(i)) == SOF_QUEUE_OK);
    }
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
    CHECK(counts.deletes == 10);
    check_dead(&q);
    CHECK(counts.deletes == 10);
}

static void test_custom_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_queue_t q;
    CHECK(sof_queue_init(&q, NULL, NULL, &alloc) == SOF_QUEUE_OK);
    CHECK(state.allocations == 0);
    for (uintptr_t i = 0; i < 1000; i++) {
        CHECK(sof_queue_enqueue(&q, as_data(i)) == SOF_QUEUE_OK);
    }
    CHECK(state.allocations == 1000);
    CHECK(state.live == 1000);
    for (uintptr_t i = 0; i < 500; i++) {
        CHECK(sof_queue_dequeue(&q, NULL) == SOF_QUEUE_OK);
    }
    // Dequeued elements are kept as spares, not freed.
    CHECK(state.live == 1000);
    CHECK(q.spare_count == 500);
    CHECK(sof_queue_clear(&q) == SOF_QUEUE_OK);
    CHECK(state.live == 0);
    CHECK(q.spare == NULL);
    CHECK(q.spare_count == 0);
    for (uintptr_t i = 0; i < 10; i++) {
        CHECK(sof_queue_enqueue(&q, as_data(i)) == SOF_QUEUE_OK);
    }
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_spares(void) {
    // Dequeued elements are reused by later enqueues, newest first, and
    // sof_queue_shrink() frees what is left over.
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    counts_t counts = {0};
    sof_queue_t q;
    CHECK(sof_queue_init(&q, count_delete, &counts, &alloc) == SOF_QUEUE_OK);
    CHECK(q.spare == NULL);
    CHECK(q.spare_count == 0);
    for (uintptr_t i = 0; i < 100; i++) {
        CHECK(sof_queue_enqueue(&q, as_data(i)) == SOF_QUEUE_OK);
    }
    CHECK(state.allocations == 100);

    // Dropping and taking both keep the element; del_func sees dropped ones
    // before they become spares.
    sof_queue_element_t *first = sof_queue_peek(&q);
    void *data;
    CHECK(sof_queue_dequeue(&q, NULL) == SOF_QUEUE_OK);
    CHECK(counts.deletes == 1);
    CHECK(q.spare == first);
    CHECK(q.spare_count == 1);
    for (uintptr_t i = 1; i < 60; i++) {
        CHECK(sof_queue_dequeue(&q, &data) == SOF_QUEUE_OK);
        CHECK(data == as_data(i));
    }
    CHECK(counts.deletes == 1);
    CHECK(q.count == 40);
    CHECK(q.spare_count == 60);
    CHECK(state.live == 100);

    // Enqueues take spares before allocating.
    sof_queue_element_t *newest = q.spare;
    CHECK(sof_queue_enqueue(&q, as_data(100)) == SOF_QUEUE_OK);
    CHECK(q.tail == newest);
    CHECK(q.tail->next == NULL);
    CHECK(q.tail->data == as_data(100));
    for (uintptr_t i = 101; i < 160; i++) {
        CHECK(sof_queue_enqueue(&q, as_data(i)) == SOF_QUEUE_OK);
    }
    CHECK(q.spare == NULL);
    CHECK(q.spare_count == 0);
    CHECK(state.allocations == 100);
    CHECK(sof_queue_enqueue(&q, as_data(160)) == SOF_QUEUE_OK);
    CHECK(state.allocations == 101);
    CHECK(q.count == 101);
    uintptr_t next = 60;
    CHECK(sof_queue_traverse(&q, check_order, &next) == 0);
    CHECK(next == 161);

    // sof_queue_shrink() frees the spares and leaves the queue alone.
    for (uintptr_t i = 0; i < 50; i++) {
        CHECK(sof_queue_dequeue(&q, &data) == SOF_QUEUE_OK);
    }
    CHECK(q.spare_count == 50);
    CHECK(state.live == 101);
    CHECK(sof_queue_shrink(&q) == SOF_QUEUE_OK);
    CHECK(q.spare == NULL);
    CHECK(q.spare_count == 0);
    CHECK(state.live == 51);
    CHECK(q.count == 51);
    CHECK(sof_queue_shrink(&q) == SOF_QUEUE_OK);
    CHECK(state.live == 51);
    next = 110;
    CHECK(sof_queue_traverse(&q, check_order, &next) == 0);
    CHECK(next == 161);
    CHECK(counts.deletes == 1);

    // sof_queue_free() frees spares as well as elements.
    for (uintptr_t i = 0; i < 20; i++) {
        CHECK(sof_queue_dequeue(&q, &data) == SOF_QUEUE_OK);
    }
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
    CHECK(counts.deletes == 32);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_no_memory(void) {
    // Fail each allocation in turn, until a run makes none fail. The queue
    // must be unchanged by a failed enqueue, and nothing may leak. Some
    // dequeues along the way leave spares, and the enqueues that reuse them
    // must not allocate.
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_queue_t q;
        int failed = 0;
        uintptr_t put = 0, value = 0;
        CHECK(sof_queue_init(&q, NULL, NULL, &alloc) == SOF_QUEUE_OK);
        for (uintptr_t i = 0; i < 20; i++) {
            sof_queue_error_t rc = sof_queue_enqueue(&q, as_data(value++));
            if (rc == SOF_QUEUE_NO_MEM) {
                failed = 1;
                CHECK(q.count == put);
            } else {
                CHECK(rc == SOF_QUEUE_OK);
                put++;
            }
            if (i % 5 == 4) {
                // Take two, leaving spares for the next two enqueues, which
                // mustn't allocate.
                void *data;
                CHECK(sof_queue_dequeue(&q, &data) == SOF_QUEUE_OK);
                CHECK(sof_queue_dequeue(&q, NULL) == SOF_QUEUE_OK);
                put -= 2;
                size_t before = state.allocations;
                CHECK(sof_queue_enqueue(&q, as_data(value++)) == SOF_QUEUE_OK);
                CHECK(sof_queue_enqueue(&q, as_data(value++)) == SOF_QUEUE_OK);
                put += 2;
                CHECK(state.allocations == before);
            }
        }
        CHECK(q.count == put);
        if (put > 0) {
            // Spares left after a failed allocation are freed too.
            CHECK(sof_queue_dequeue(&q, NULL) == SOF_QUEUE_OK);
            put--;
        }
        uintptr_t next = 0, previous = 0;
        for (sof_queue_element_t *e = sof_queue_peek(&q); e != NULL; e = sof_queue_next(&q, e)) {
            CHECK(next == 0 || as_int(e->data) > previous);
            previous = as_int(e->data);
            next++;
        }
        CHECK(next == put);
        CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (!failed) {
            break;
        }
    }
}

/// @brief A queue shared by threads, with the lock and condition variable
/// the application supplies, as in the README's Threading section.
typedef struct shared_t {
    sof_queue_t q;
    pthread_mutex_t lock;
    pthread_cond_t ready;
    /// @brief Producers that haven't finished; consumers stop at 0 once the
    /// queue is empty.
    size_t producers_left;
} shared_t;

/// @brief One producer or consumer thread's state and results, checked by
/// the main thread after joining, since CHECK isn't safe on other threads.
typedef struct worker_t {
    shared_t *shared;
    uintptr_t id;
    /// @brief Items consumed, and the sum of their data.
    size_t count;
    uintptr_t sum;
    /// @brief Calls that failed, or items out of order.
    size_t errors;
    /// @brief The last item seen from each producer, plus one, or 0.
    uintptr_t last[PRODUCERS];
} worker_t;

/// @brief Enqueues ITEMS items, numbered id * ITEMS + seq.
static void *produce(void *arg) {
    worker_t *w = arg;
    shared_t *s = w->shared;
    for (uintptr_t seq = 0; seq < ITEMS; seq++) {
        pthread_mutex_lock(&s->lock);
        if (sof_queue_enqueue(&s->q, as_data(w->id * ITEMS + seq)) != SOF_QUEUE_OK) {
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

/// @brief Dequeues until the producers are done and the queue is empty,
/// checking that each producer's items come in the order it enqueued them.
static void *consume(void *arg) {
    worker_t *w = arg;
    shared_t *s = w->shared;
    while (1) {
        void *data;
        pthread_mutex_lock(&s->lock);
        sof_queue_error_t rc;
        while ((rc = sof_queue_dequeue(&s->q, &data)) == SOF_QUEUE_NO_MATCH && s->producers_left > 0) {
            pthread_cond_wait(&s->ready, &s->lock);
        }
        pthread_mutex_unlock(&s->lock);
        if (rc == SOF_QUEUE_NO_MATCH) {
            return NULL;
        }
        if (rc != SOF_QUEUE_OK) {
            w->errors++;
            return NULL;
        }
        uintptr_t item = as_int(data);
        uintptr_t producer = item / ITEMS, seq = item % ITEMS;
        if (producer >= PRODUCERS || seq + 1 <= w->last[producer]) {
            w->errors++;
        } else {
            w->last[producer] = seq + 1;
        }
        w->count++;
        w->sum += item;
    }
}

static void test_threads(void) {
    // Producers and consumers share one queue under a mutex, with a
    // condition variable so consumers wait for work. Every item must be
    // consumed exactly once, each producer's in order, without leaks.
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    shared_t s;
    CHECK(sof_queue_init(&s.q, NULL, NULL, &alloc) == SOF_QUEUE_OK);
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
    uintptr_t sum = 0;
    for (size_t i = 0; i < PRODUCERS; i++) {
        CHECK(producers[i].errors == 0);
    }
    for (size_t i = 0; i < CONSUMERS; i++) {
        CHECK(consumers[i].errors == 0);
        count += consumers[i].count;
        sum += consumers[i].sum;
    }
    uintptr_t total = (uintptr_t)PRODUCERS * ITEMS;
    CHECK(count == total);
    CHECK(sum == total * (total - 1) / 2);
    CHECK(s.q.count == 0);
    CHECK(sof_queue_free(&s.q) == SOF_QUEUE_OK);
    // Consumers' dequeues leave spares that producers reuse.
    CHECK(state.allocations <= total);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    pthread_cond_destroy(&s.ready);
    pthread_mutex_destroy(&s.lock);
}

/// @brief A queue shared by one writer and several readers under a
/// readers-writer lock.
typedef struct rw_shared_t {
    sof_queue_t q;
    pthread_rwlock_t lock;
} rw_shared_t;

/// @brief The walks each reader makes. Readers make a fixed number rather
/// than walking until the writer is done: glibc's rwlock prefers readers,
/// so readers that keep taking the lock can keep the writer out forever.

typedef struct reader_t {
    rw_shared_t *shared;
    size_t passes;
    size_t errors;
} reader_t;

/// @brief Checks, for sof_queue_traverse(), that the items increase by one and
/// counts them.
static int check_rising(sof_queue_element_t *element, void *ctx) {
    uintptr_t *state = ctx; // state[0]: items seen, state[1]: the next item
    if (state[0] > 0 && as_int(element->data) != state[1]) {
        return 1;
    }
    state[1] = as_int(element->data) + 1;
    state[0]++;
    return 0;
}

/// @brief Walks the queue PASSES times with sof_queue_traverse() and
/// sof_queue_next() under the read lock, checking what it sees each time.
static void *read_queue(void *arg) {
    reader_t *r = arg;
    rw_shared_t *s = r->shared;
    for (size_t pass = 0; pass < PASSES; pass++) {
        pthread_rwlock_rdlock(&s->lock);
        uintptr_t state[2] = {0, 0};
        if (sof_queue_traverse(&s->q, check_rising, state) != 0 || state[0] != s->q.count) {
            r->errors++;
        }
        size_t walked = 0;
        for (sof_queue_element_t *e = sof_queue_peek(&s->q); e != NULL; e = sof_queue_next(&s->q, e)) {
            walked++;
        }
        if (walked != s->q.count) {
            r->errors++;
        }
        pthread_rwlock_unlock(&s->lock);
        r->passes++;
    }
    return NULL;
}

static void test_threads_readers(void) {
    // One writer enqueues a run of numbers and dequeues from the front,
    // while readers walk the queue under a read lock and must always see a
    // run of consecutive numbers as long as count.
    rw_shared_t s;
    CHECK(sof_queue_init(&s.q, NULL, NULL, NULL) == SOF_QUEUE_OK);
    CHECK(pthread_rwlock_init(&s.lock, NULL) == 0);
    reader_t readers[READERS] = {{0}};
    pthread_t threads[READERS];
    for (size_t i = 0; i < READERS; i++) {
        readers[i].shared = &s;
        CHECK(pthread_create(&threads[i], NULL, read_queue, &readers[i]) == 0);
    }
    uint64_t random = 0x2545f4914f6cdd1dULL;
    size_t errors = 0;
    for (uintptr_t i = 0; i < ITEMS; i++) {
        pthread_rwlock_wrlock(&s.lock);
        if (sof_queue_enqueue(&s.q, as_data(i)) != SOF_QUEUE_OK) {
            errors++;
        }
        if (s.q.count > 50 + next_random(&random) % 100) {
            if (sof_queue_dequeue(&s.q, NULL) != SOF_QUEUE_OK) {
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
    CHECK(sof_queue_free(&s.q) == SOF_QUEUE_OK);
    pthread_rwlock_destroy(&s.lock);
}

/// @brief The functions that only read take a const queue and const
/// elements, and hand back elements whose data can still be changed.
static void test_const_queue(void) {
    sof_queue_t q;
    counts_t counts = {0};
    uintptr_t next = 1;
    CHECK(sof_queue_init(&q, NULL, NULL, NULL) == SOF_QUEUE_OK);
    for (uintptr_t i = 1; i <= 10; i++) {
        CHECK(sof_queue_enqueue(&q, as_data(i)) == SOF_QUEUE_OK);
    }
    const sof_queue_t *cq = &q;
    sof_queue_element_t *head = sof_queue_peek(cq);
    CHECK(head != NULL && as_int(head->data) == 1);
    const sof_queue_element_t *ce = head;
    CHECK(as_int(sof_queue_next(cq, ce)->data) == 2);
    CHECK(sof_queue_traverse(cq, count_visit, &counts) == 0 && counts.visits == 10);
    CHECK(sof_queue_traverse(cq, check_order, &next) == 0 && next == 11);
    head->data = as_data(100);
    CHECK(as_int(sof_queue_peek(cq)->data) == 100);
    CHECK(q.count == 10);
    CHECK(sof_queue_free(&q) == SOF_QUEUE_OK);
}

int main(void) {
    printf("sof_queue:\n");
    run("init", test_init);
    run("empty queue", test_empty);
    run("first in, first out", test_fifo);
    run("NULL data", test_null_data);
    run("next", test_next);
    run("traverse", test_traverse);
    run("traversal stops early", test_traverse_stop);
    run("read through a const queue", test_const_queue);
    run("del_func sees every dropped element", test_delete_func);
    run("random enqueues and dequeues", test_random);
    run("NULL arguments", test_null);
    run("failed init", test_failed_init);
    run("freed queue", test_freed);
    run("custom allocator", test_custom_allocator);
    run("dequeued elements reused, and shrink", test_spares);
    run("out of memory", test_no_memory);
    run("producers and consumers on threads", test_threads);
    run("readers alongside a writer on threads", test_threads_readers);
    return test_report();
}
