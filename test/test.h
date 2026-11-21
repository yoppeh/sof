/**
 * @file test.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief The test harness shared by every library's tests.
 * @details Each library's tests are a program of their own, built from the
 * library's test.c by its Makefile. A test is a void function that uses
 * CHECK(); main() calls run() for each test and returns test_report(). Each
 * test prints its name and either "ok" or the first failed check, as in:
 *
 *     static void test_insert(void) {
 *         CHECK(1 + 1 == 2);
 *     }
 *
 *     int main(void) {
 *         run("insert", test_insert);
 *         return test_report();
 *     }
 *
 * Everything here is static, so the header is the whole harness: there is no
 * file to compile with it, and it belongs to the one source file of a test
 * program that includes it.
 *
 * If sof_alloc.h was included first, there is also a test allocator: see
 * test_alloc_t.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#ifndef TEST_H
#define TEST_H

#include <stdint.h>
#include <stdio.h>

/// @brief The number of tests that have failed so far.
static int test_failure_count;

/**
 * @brief Fails the current test if cond is false.
 * @details Prints "FAILED" with the file, line and condition, counts the
 * failure and returns from the test function, so the rest of the test is
 * skipped. Use only in a void function.
 */
#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("FAILED\n    %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            test_failure_count++;                                           \
            return;                                                         \
        }                                                                   \
    } while (0)

/**
 * @brief A deterministic pseudo-random number generator (xorshift64*).
 * @param state The generator's state, which must not start at 0. Seed it
 * with a fixed value so the tests run the same way every time.
 * @return The next number.
 */
static inline uint64_t next_random(uint64_t *state) {
    *state ^= *state >> 12;
    *state ^= *state << 25;
    *state ^= *state >> 27;
    return *state * 0x2545f4914f6cdd1dULL;
}

/**
 * @brief Runs one test.
 * @details Prints "name... ", then calls test, then prints "ok" if no check
 * failed.
 * @param name What is tested.
 * @param test The test function.
 */
static inline void run(const char *name, void (*test)(void)) {
    int before = test_failure_count;
    printf("%s... ", name);
    fflush(stdout);
    test();
    if (test_failure_count == before) {
        printf("ok\n");
    }
}

/**
 * @brief Prints the result of all the tests run; called by main().
 * @details Prints "all tests passed" or "N test(s) failed".
 * @return 0 if every test passed, or 1, for main() to return.
 */
static inline int test_report(void) {
    if (test_failure_count != 0) {
        printf("%d test(s) failed\n", test_failure_count);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}

#ifdef SOF_ALLOC_H

#include <stddef.h>
#include <stdlib.h>

/**
 * @brief The state of a test allocator.
 * @details test_allocator() makes a sof_alloc_t whose functions count every
 * allocation, can fail a chosen one, and check every free and realloc. Each
 * block has a hidden header holding its size, so a free_sized or realloc
 * given the wrong size, or a pointer the allocator didn't return, is counted
 * in bad. Set the fields to 0 to start, and fail_at to fail an allocation.
 */
typedef struct test_alloc_t {
    /// @brief The allocation to fail, counting from 1, or 0 for none.
    size_t fail_at;
    /// @brief Allocations asked for, by malloc or realloc, including failed
    /// ones.
    size_t allocations;
    /// @brief Blocks allocated and not yet freed.
    long live;
    /// @brief Calls with a wrong size, a size of 0, or a block it didn't
    /// allocate.
    size_t bad;
} test_alloc_t;

/// @brief The hidden header of a test allocator's block, as large as the
/// strictest alignment so the block after it stays aligned.
typedef union test_alloc_header_t {
    struct {
        size_t size;
        size_t magic;
    } info;
    max_align_t align;
} test_alloc_header_t;

/// @brief Marks a block the test allocator returned and hasn't freed.
#define TEST_ALLOC_MAGIC ((size_t)0x7e57a110c)

/// @brief Counts an allocation, and returns 1 if it is the one to fail.
static inline int test_alloc_fails(test_alloc_t *state) {
    state->allocations++;
    return state->fail_at != 0 && state->allocations == state->fail_at;
}

/// @brief Returns the header of block p, counting a bad call if it isn't a
/// live block of size bytes (or of any size, if size is 0).
static inline test_alloc_header_t *test_alloc_header(test_alloc_t *state, void *p, size_t size) {
    test_alloc_header_t *header = (test_alloc_header_t *)p - 1;
    if (header->info.magic != TEST_ALLOC_MAGIC || (size != 0 && header->info.size != size)) {
        state->bad++;
    }
    return header;
}

static inline void *test_alloc_malloc(size_t size, void *ctx) {
    test_alloc_t *state = ctx;
    if (size == 0) {
        state->bad++;
    }
    if (test_alloc_fails(state)) {
        return NULL;
    }
    test_alloc_header_t *header = malloc(sizeof(test_alloc_header_t) + size);
    if (header == NULL) {
        return NULL;
    }
    header->info.size = size;
    header->info.magic = TEST_ALLOC_MAGIC;
    state->live++;
    return header + 1;
}

static inline void *test_alloc_realloc(void *p, size_t old_size, size_t new_size, void *ctx) {
    test_alloc_t *state = ctx;
    if (p == NULL || old_size == 0 || new_size == 0) {
        state->bad++;
        return NULL;
    }
    test_alloc_header_t *header = test_alloc_header(state, p, old_size);
    if (test_alloc_fails(state)) {
        return NULL;
    }
    test_alloc_header_t *moved = realloc(header, sizeof(test_alloc_header_t) + new_size);
    if (moved == NULL) {
        return NULL;
    }
    moved->info.size = new_size;
    return moved + 1;
}

static inline void test_alloc_free_sized(void *p, size_t size, void *ctx) {
    test_alloc_t *state = ctx;
    if (p == NULL || size == 0) {
        state->bad++;
        return;
    }
    test_alloc_header_t *header = test_alloc_header(state, p, size);
    header->info.magic = 0;
    state->live--;
    free(header);
}

static inline void test_alloc_free(void *p, void *ctx) {
    test_alloc_t *state = ctx;
    if (p == NULL) {
        state->bad++;
        return;
    }
    test_alloc_header_t *header = test_alloc_header(state, p, 0);
    header->info.magic = 0;
    state->live--;
    free(header);
}

/**
 * @brief Returns a sof_alloc_t of the test allocator, with malloc, realloc
 * and free_sized, using state.
 */
static inline sof_alloc_t test_allocator(test_alloc_t *state) {
    sof_alloc_t alloc = {test_alloc_malloc, test_alloc_realloc, NULL, test_alloc_free_sized, state};
    return alloc;
}

#endif // SOF_ALLOC_H

#endif // TEST_H
