/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_alloc.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sof_alloc.h"
#include "test.h"

/// @brief Threads, the blocks each one keeps at a time, and the operations
/// each makes, in the threaded tests.
#define THREADS 4
#define SLOTS 64
#define STEPS 20000

/// @brief Returns 1 if the size bytes at p are all 0.
static int all_zero(const void *p, size_t size) {
    const unsigned char *bytes = p;
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != 0) {
            return 0;
        }
    }
    return 1;
}

/// @brief The helpers with a NULL sof_alloc_t, and with one whose malloc is
/// NULL: both use the standard functions.
static void test_standard(void) {
    sof_alloc_t empty;
    memset(&empty, 0, sizeof(empty));
    const sof_alloc_t *standard[] = {NULL, &empty};
    for (size_t i = 0; i < 2; i++) {
        const sof_alloc_t *alloc = standard[i];
        CHECK(sof_alloc_valid(alloc));
        char *p = sof_alloc_malloc(alloc, 16);
        CHECK(p != NULL);
        memcpy(p, "0123456789abcde", 16);
        p = sof_alloc_realloc(alloc, p, 16, 4096);
        CHECK(p != NULL);
        CHECK(memcmp(p, "0123456789abcde", 16) == 0);
        sof_alloc_free(alloc, p, 4096);
        int *zeroed = sof_alloc_calloc(alloc, 100, sizeof(int));
        CHECK(zeroed != NULL);
        CHECK(all_zero(zeroed, 100 * sizeof(int)));
        sof_alloc_free(alloc, zeroed, 100 * sizeof(int));
        char *s = sof_alloc_strndup(alloc, "hello, world", 5);
        CHECK(s != NULL);
        CHECK(strcmp(s, "hello") == 0);
        sof_alloc_free(alloc, s, 6);
        sof_alloc_free(alloc, NULL, 0);
    }
}

/// @brief The helpers with the test allocator, which checks every size and
/// counts every block.
static void test_custom(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    CHECK(sof_alloc_valid(&alloc));
    char *p = sof_alloc_malloc(&alloc, 16);
    CHECK(p != NULL);
    CHECK(state.allocations == 1 && state.live == 1);
    memcpy(p, "0123456789abcde", 16);
    p = sof_alloc_realloc(&alloc, p, 16, 64);
    CHECK(p != NULL);
    CHECK(memcmp(p, "0123456789abcde", 16) == 0);
    CHECK(state.allocations == 2 && state.live == 1);
    p = sof_alloc_realloc(&alloc, p, 64, 8);
    CHECK(p != NULL);
    CHECK(memcmp(p, "01234567", 8) == 0);
    sof_alloc_free(&alloc, p, 8);
    CHECK(state.live == 0);

    int *zeroed = sof_alloc_calloc(&alloc, 100, sizeof(int));
    CHECK(zeroed != NULL);
    CHECK(all_zero(zeroed, 100 * sizeof(int)));
    sof_alloc_free(&alloc, zeroed, 100 * sizeof(int));

    char *s = sof_alloc_strndup(&alloc, "hello, world", 5);
    CHECK(s != NULL);
    CHECK(strcmp(s, "hello") == 0);
    sof_alloc_free(&alloc, s, 6);

    // Freeing NULL calls nothing.
    sof_alloc_free(&alloc, NULL, 10);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

/// @brief sof_alloc_realloc() of a sof_alloc_t with no realloc allocates,
/// copies and frees.
static void test_no_realloc(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.realloc = NULL;
    char *p = sof_alloc_malloc(&alloc, 16);
    CHECK(p != NULL);
    memcpy(p, "0123456789abcde", 16);
    char *q = sof_alloc_realloc(&alloc, p, 16, 1000);
    CHECK(q != NULL && q != p);
    CHECK(memcmp(q, "0123456789abcde", 16) == 0);
    CHECK(state.allocations == 2 && state.live == 1);
    q = sof_alloc_realloc(&alloc, q, 1000, 4);
    CHECK(q != NULL);
    CHECK(memcmp(q, "0123", 4) == 0);
    CHECK(state.live == 1);
    // A failed resize leaves the block as it was.
    state.fail_at = state.allocations + 1;
    CHECK(sof_alloc_realloc(&alloc, q, 4, 100) == NULL);
    CHECK(state.live == 1);
    CHECK(memcmp(q, "0123", 4) == 0);
    sof_alloc_free(&alloc, q, 4);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

/// @brief With only free, and no free_sized, the helpers free with free.
static void test_unsized_free(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    alloc.free = test_alloc_free;
    alloc.realloc = NULL;
    CHECK(sof_alloc_valid(&alloc));
    char *p = sof_alloc_malloc(&alloc, 10);
    CHECK(p != NULL);
    p = sof_alloc_realloc(&alloc, p, 10, 20);
    CHECK(p != NULL);
    CHECK(state.live == 1);
    sof_alloc_free(&alloc, p, 20);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

/// @brief Sizes of 0, overflowing sizes and NULL arguments call nothing and
/// return NULL.
static void test_edge_cases(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    CHECK(sof_alloc_malloc(&alloc, 0) == NULL);
    CHECK(sof_alloc_calloc(&alloc, 0, 8) == NULL);
    CHECK(sof_alloc_calloc(&alloc, 8, 0) == NULL);
    CHECK(sof_alloc_calloc(&alloc, SIZE_MAX / 2, 4) == NULL);
    CHECK(sof_alloc_calloc(NULL, SIZE_MAX / 2, 4) == NULL);
    CHECK(sof_alloc_strndup(&alloc, NULL, 3) == NULL);
    CHECK(state.allocations == 0);

    // Resizing NULL allocates; resizing to 0 returns NULL and keeps the block.
    char *p = sof_alloc_realloc(&alloc, NULL, 0, 8);
    CHECK(p != NULL);
    CHECK(state.allocations == 1 && state.live == 1);
    CHECK(sof_alloc_realloc(&alloc, p, 8, 0) == NULL);
    CHECK(state.live == 1);
    sof_alloc_free(&alloc, p, 8);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

/// @brief Failed allocations return NULL and leave nothing allocated.
static void test_failures(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    state.fail_at = 1;
    CHECK(sof_alloc_malloc(&alloc, 10) == NULL);
    state.fail_at = 2;
    CHECK(sof_alloc_calloc(&alloc, 10, 10) == NULL);
    state.fail_at = 3;
    CHECK(sof_alloc_strndup(&alloc, "abc", 3) == NULL);
    CHECK(state.live == 0);
    // A failed realloc leaves the block as it was.
    char *p = sof_alloc_strndup(&alloc, "abc", 3);
    CHECK(p != NULL);
    state.fail_at = state.allocations + 1;
    CHECK(sof_alloc_realloc(&alloc, p, 4, 100) == NULL);
    CHECK(strcmp(p, "abc") == 0);
    sof_alloc_free(&alloc, p, 4);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

/// @brief The test allocator notices a free with the wrong size, which the
/// other libraries' tests rely on.
static void test_wrong_size_noticed(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    char *p = sof_alloc_malloc(&alloc, 10);
    CHECK(p != NULL);
    sof_alloc_free(&alloc, p, 11);
    CHECK(state.bad == 1);
    CHECK(state.live == 0);
}

static void test_valid(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    CHECK(sof_alloc_valid(NULL));
    CHECK(sof_alloc_valid(&alloc));
    alloc.free_sized = NULL;
    CHECK(!sof_alloc_valid(&alloc));
    alloc.free = test_alloc_free;
    CHECK(sof_alloc_valid(&alloc));
    // Without a malloc, the standard functions are used, whatever else is
    // set.
    alloc.malloc = NULL;
    alloc.free = NULL;
    CHECK(sof_alloc_valid(&alloc));
}

/// @brief An application allocator shared by threads: the test allocator,
/// with a mutex around each call, as the README's Threading section asks of
/// an allocator used from several threads.
typedef struct locked_alloc_t {
    pthread_mutex_t lock;
    test_alloc_t state;
} locked_alloc_t;

static void *locked_malloc(size_t size, void *ctx) {
    locked_alloc_t *locked = ctx;
    pthread_mutex_lock(&locked->lock);
    void *p = test_alloc_malloc(size, &locked->state);
    pthread_mutex_unlock(&locked->lock);
    return p;
}

static void *locked_realloc(void *p, size_t old_size, size_t new_size, void *ctx) {
    locked_alloc_t *locked = ctx;
    pthread_mutex_lock(&locked->lock);
    void *q = test_alloc_realloc(p, old_size, new_size, &locked->state);
    pthread_mutex_unlock(&locked->lock);
    return q;
}

static void locked_free_sized(void *p, size_t size, void *ctx) {
    locked_alloc_t *locked = ctx;
    pthread_mutex_lock(&locked->lock);
    test_alloc_free_sized(p, size, &locked->state);
    pthread_mutex_unlock(&locked->lock);
}

/// @brief A block a thread holds, filled with bytes counting up from fill.
typedef struct block_t {
    unsigned char *p;
    size_t size;
    unsigned char fill;
} block_t;

/// @brief One thread's allocator, seed and results, checked by the main
/// thread after joining, since CHECK isn't safe on other threads.
typedef struct worker_t {
    const sof_alloc_t *alloc;
    uint64_t seed;
    /// @brief Calls that allocated, each of which the allocator counts once.
    size_t allocations;
    /// @brief Failed calls, and blocks whose contents were wrong.
    size_t errors;
} worker_t;

/// @brief Fills the size bytes at p with bytes counting up from fill.
static void fill_block(unsigned char *p, size_t size, unsigned char fill) {
    for (size_t i = 0; i < size; i++) {
        p[i] = (unsigned char)(fill + i);
    }
}

/// @brief Returns 1 if the size bytes at p count up from fill.
static int block_filled(const unsigned char *p, size_t size, unsigned char fill) {
    for (size_t i = 0; i < size; i++) {
        if (p[i] != (unsigned char)(fill + i)) {
            return 0;
        }
    }
    return 1;
}

/// @brief Allocates a block into an empty slot, by one of the helpers
/// chosen by r, and fills it.
static void new_block(worker_t *w, block_t *b, uint64_t r) {
    static const char text[] = "the quick brown fox jumps over the lazy dog, again and again";
    size_t size = 1 + (size_t)(r >> 8) % 512;
    switch (r % 4) {
    case 0:
        b->p = sof_alloc_malloc(w->alloc, size);
        break;
    case 1:
        b->p = sof_alloc_calloc(w->alloc, size, 1);
        if (b->p != NULL && !all_zero(b->p, size)) {
            w->errors++;
        }
        break;
    case 2:
        size = 1 + (size_t)(r >> 8) % (sizeof(text) - 1);
        b->p = (unsigned char *)sof_alloc_strndup(w->alloc, text, size - 1);
        if (b->p != NULL && (memcmp(b->p, text, size - 1) != 0 || b->p[size - 1] != '\0')) {
            w->errors++;
        }
        break;
    default:
        b->p = sof_alloc_realloc(w->alloc, NULL, 0, size);
        break;
    }
    w->allocations++;
    if (b->p == NULL) {
        w->errors++;
        return;
    }
    b->size = size;
    b->fill = (unsigned char)(r >> 32);
    fill_block(b->p, b->size, b->fill);
}

/// @brief Makes STEPS random allocations, reallocations and frees on
/// SLOTS blocks, checking each block's contents along the way, then frees
/// what is left.
static void *churn(void *arg) {
    worker_t *w = arg;
    block_t blocks[SLOTS] = {{0}};
    uint64_t random = w->seed;
    for (size_t step = 0; step < STEPS; step++) {
        uint64_t r = next_random(&random);
        block_t *b = &blocks[r % SLOTS];
        r = next_random(&random);
        if (b->p == NULL) {
            new_block(w, b, r);
        } else if (r % 2 == 0) {
            size_t size = 1 + (size_t)(r >> 8) % 2048;
            unsigned char *p = sof_alloc_realloc(w->alloc, b->p, b->size, size);
            w->allocations++;
            if (p == NULL) {
                w->errors++;
                continue;
            }
            if (!block_filled(p, size < b->size ? size : b->size, b->fill)) {
                w->errors++;
            }
            b->p = p;
            b->size = size;
            b->fill = (unsigned char)(r >> 32);
            fill_block(b->p, b->size, b->fill);
        } else {
            if (!block_filled(b->p, b->size, b->fill)) {
                w->errors++;
            }
            sof_alloc_free(w->alloc, b->p, b->size);
            b->p = NULL;
        }
    }
    for (size_t i = 0; i < SLOTS; i++) {
        if (blocks[i].p != NULL) {
            if (!block_filled(blocks[i].p, blocks[i].size, blocks[i].fill)) {
                w->errors++;
            }
            sof_alloc_free(w->alloc, blocks[i].p, blocks[i].size);
        }
    }
    return NULL;
}

/// @brief Runs churn() on THREADS threads at once with alloc, and returns
/// the allocations they made, or sets *errors.
static size_t run_threads(const sof_alloc_t *alloc, size_t *errors) {
    worker_t workers[THREADS] = {{0}};
    pthread_t threads[THREADS];
    size_t started = 0, allocations = 0;
    *errors = 0;
    for (size_t i = 0; i < THREADS; i++) {
        workers[i].alloc = alloc;
        workers[i].seed = 0x9e3779b97f4a7c15ULL + i;
        if (pthread_create(&threads[i], NULL, churn, &workers[i]) != 0) {
            (*errors)++;
            break;
        }
        started++;
    }
    for (size_t i = 0; i < started; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            (*errors)++;
        }
        *errors += workers[i].errors;
        allocations += workers[i].allocations;
    }
    return allocations;
}

static void test_threads_standard(void) {
    // Threads allocate, resize and free their own blocks at once through the
    // standard functions, and every block keeps its contents.
    size_t errors;
    size_t allocations = run_threads(NULL, &errors);
    CHECK(errors == 0);
    CHECK(allocations > 0);
}

static void test_threads_locked(void) {
    // The same with an application allocator shared by every thread and
    // guarded by its own mutex, with and without a realloc function. The
    // allocator must see every allocation, every size right and no leaks.
    for (int emulated = 0; emulated < 2; emulated++) {
        locked_alloc_t locked;
        memset(&locked.state, 0, sizeof(locked.state));
        CHECK(pthread_mutex_init(&locked.lock, NULL) == 0);
        sof_alloc_t alloc = {locked_malloc, emulated ? NULL : locked_realloc, NULL, locked_free_sized, &locked};
        CHECK(sof_alloc_valid(&alloc));
        size_t errors;
        size_t allocations = run_threads(&alloc, &errors);
        pthread_mutex_destroy(&locked.lock);
        CHECK(errors == 0);
        CHECK(locked.state.allocations == allocations);
        CHECK(locked.state.live == 0);
        CHECK(locked.state.bad == 0);
    }
}

int main(void) {
    printf("sof_alloc:\n");
    run("standard functions", test_standard);
    run("custom functions", test_custom);
    run("realloc without a realloc function", test_no_realloc);
    run("free without free_sized", test_unsized_free);
    run("sizes of 0, overflow and NULL", test_edge_cases);
    run("failed allocations", test_failures);
    run("the test allocator checks sizes", test_wrong_size_noticed);
    run("valid allocators", test_valid);
    run("standard functions on threads", test_threads_standard);
    run("an allocator shared by threads under a mutex", test_threads_locked);
    return test_report();
}
