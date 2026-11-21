/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for the sof_mem allocators.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_mem.h"
#include "test.h"

/// @brief The block size most arena tests use: small, so a few allocations
/// fill a block, and anything over 256 bytes is large.
#define BLOCK 1024

/// @brief n rounded up to the alignment of every block, as the library
/// rounds sizes: 16 bytes on x86-64 Linux, 8 on arm64 macOS.
#define ROUND(n) (((n) + _Alignof(max_align_t) - 1) & ~(_Alignof(max_align_t) - 1))

/// @brief The size of a block's header, as the library computes it.
#define HEADER ROUND(sizeof(sof_mem_block_t))

/// @brief The number of blocks the random tests keep at once.
#define SLOTS 64

/// @brief The number of operations the random tests make.
#define OPERATIONS 20000

/// @brief A block the random tests have allocated, and the byte it is
/// filled with.
typedef struct slot_t {
    unsigned char *p;
    size_t size;
    unsigned char fill;
} slot_t;

/// @brief What the tracker's callbacks count, through their ctx.
typedef struct track_counts_t {
    size_t wrong_size;
    size_t unknown;
    const void *last_p;
    size_t last_size;
    size_t visits;
    size_t visited_bytes;
    /// @brief The number of visits after which the traversal stops, or 0.
    size_t stop_after;
} track_counts_t;

/// @brief Memory for fixed arenas, aligned for any type.
static union {
    max_align_t align;
    unsigned char bytes[1 << 20];
} buffer;

static int aligned(const void *p);
static void check_arena_dead(sof_mem_arena_t *a);
static void check_pool_dead(sof_mem_pool_t *pool);
static void check_slab_dead(sof_mem_slab_t *slab);
static void check_track_dead(sof_mem_track_t *t);
static size_t count_list(const sof_mem_block_t *b);
static int filled(const void *p, size_t size, unsigned char fill);
static void random_ops(const sof_alloc_t *alloc, uint64_t seed, slot_t *slots, int *ok);
static size_t slab_count(const sof_mem_slab_t *slab);
static int slots_intact(const slot_t *slots);
static void track_error(const void *p, size_t size, sof_mem_track_problem_t problem, void *ctx);
static int track_visit(void *p, size_t size, void *ctx);

/// @brief Returns 1 if p is aligned for any type.
static int aligned(const void *p) {
    return (uintptr_t)p % _Alignof(max_align_t) == 0;
}

/// @brief Checks that an arena is empty and acts as not initialized.
static void check_arena_dead(sof_mem_arena_t *a) {
    CHECK(a->ready == 0 && a->fixed == 0);
    CHECK(a->blocks == NULL && a->large == NULL && a->current == NULL);
    CHECK(a->allocated == 0);
    CHECK(sof_mem_arena_alloc(a, 8) == NULL);
    CHECK(sof_mem_arena_realloc(a, NULL, 0, 8) == NULL);
    CHECK(sof_mem_arena_release(a, NULL, 0) == SOF_MEM_ERROR);
    CHECK(sof_mem_arena_clear(a) == SOF_MEM_ERROR);
    CHECK(sof_mem_arena_free(a) == SOF_MEM_ERROR);
    sof_mem_arena_mark_t mark = sof_mem_arena_mark(a);
    CHECK(mark.block == NULL && mark.offset == 0 && mark.serial == 0);
    CHECK(sof_mem_arena_reset(a, mark) == SOF_MEM_ERROR);
    sof_alloc_t alloc = sof_mem_arena_allocator(a);
    CHECK(sof_alloc_valid(&alloc));
    CHECK(sof_alloc_malloc(&alloc, 8) == NULL);
}

/// @brief Checks that a pool is empty and acts as not initialized.
static void check_pool_dead(sof_mem_pool_t *pool) {
    CHECK(pool->ready == 0);
    CHECK(pool->slabs == NULL && pool->free_list == NULL && pool->current == NULL);
    CHECK(pool->allocated == 0 && pool->count == 0);
    CHECK(sof_mem_pool_alloc(pool) == NULL);
    CHECK(sof_mem_pool_release(pool, NULL) == SOF_MEM_ERROR);
    CHECK(sof_mem_pool_clear(pool) == SOF_MEM_ERROR);
    CHECK(sof_mem_pool_free(pool) == SOF_MEM_ERROR);
    sof_alloc_t alloc = sof_mem_pool_allocator(pool);
    CHECK(sof_alloc_valid(&alloc));
    CHECK(sof_alloc_malloc(&alloc, 8) == NULL);
    CHECK(sof_alloc_malloc(&alloc, 100000) == NULL);
}

/// @brief Checks that a slab allocator is empty and acts as not initialized.
static void check_slab_dead(sof_mem_slab_t *slab) {
    CHECK(slab->ready == 0);
    CHECK(slab->pools == NULL && slab->classes == 0 && slab->max_size == 0);
    CHECK(sof_mem_slab_alloc(slab, 8) == NULL);
    CHECK(sof_mem_slab_alloc(slab, 100000) == NULL);
    CHECK(sof_mem_slab_realloc(slab, NULL, 0, 8) == NULL);
    CHECK(sof_mem_slab_release(slab, NULL, 0) == SOF_MEM_ERROR);
    CHECK(sof_mem_slab_clear(slab) == SOF_MEM_ERROR);
    CHECK(sof_mem_slab_free(slab) == SOF_MEM_ERROR);
    sof_alloc_t alloc = sof_mem_slab_allocator(slab);
    CHECK(sof_alloc_valid(&alloc));
    CHECK(sof_alloc_malloc(&alloc, 8) == NULL);
}

/// @brief Checks that a tracker is empty and acts as not initialized.
static void check_track_dead(sof_mem_track_t *t) {
    CHECK(t->ready == 0);
    CHECK(t->live == NULL && t->blocks == 0 && t->bytes == 0);
    CHECK(sof_mem_track_alloc(t, 8) == NULL);
    CHECK(sof_mem_track_realloc(t, NULL, 0, 8) == NULL);
    CHECK(sof_mem_track_release(t, NULL, 0) == SOF_MEM_ERROR);
    CHECK(sof_mem_track_clear(t) == SOF_MEM_ERROR);
    CHECK(sof_mem_track_free(t) == SOF_MEM_ERROR);
    CHECK(sof_mem_track_traverse(t, track_visit, NULL) == -1);
    sof_alloc_t alloc = sof_mem_track_allocator(t);
    CHECK(sof_alloc_valid(&alloc));
    CHECK(sof_alloc_malloc(&alloc, 8) == NULL);
}

/// @brief Returns the number of blocks in a list.
static size_t count_list(const sof_mem_block_t *b) {
    size_t n = 0;
    for (; b != NULL; b = b->next) {
        n++;
    }
    return n;
}

/// @brief Returns 1 if all size bytes at p are fill.
static int filled(const void *p, size_t size, unsigned char fill) {
    const unsigned char *bytes = p;
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != fill) {
            return 0;
        }
    }
    return 1;
}

/// @brief Makes OPERATIONS random allocations, reallocations and frees of
/// sizes up to 3000 bytes through alloc, filling each block and checking it
/// is intact before it is changed, and every block now and then. The blocks
/// left are in slots. Sets *ok to 0 on any failure.
static void random_ops(const sof_alloc_t *alloc, uint64_t seed, slot_t *slots, int *ok) {
    uint64_t random = seed;
    *ok = 0;
    for (int op = 0; op < OPERATIONS; op++) {
        uint64_t r = next_random(&random);
        slot_t *slot = &slots[r % SLOTS];
        // Mostly small sizes, with some large.
        size_t size = (r >> 8) % 8 == 0 ? 257 + (r >> 16) % 3000 : 1 + (r >> 16) % 256;
        if (slot->p == NULL) {
            slot->p = sof_alloc_malloc(alloc, size);
            if (slot->p == NULL || !aligned(slot->p)) {
                return;
            }
            slot->size = size;
            slot->fill = (unsigned char)(r >> 40);
            memset(slot->p, slot->fill, size);
        } else if ((r >> 32) % 3 == 0) {
            if (!filled(slot->p, slot->size, slot->fill)) {
                return;
            }
            sof_alloc_free(alloc, slot->p, slot->size);
            slot->p = NULL;
        } else {
            if (!filled(slot->p, slot->size, slot->fill)) {
                return;
            }
            unsigned char *p = sof_alloc_realloc(alloc, slot->p, slot->size, size);
            if (p == NULL || !aligned(p) || !filled(p, size < slot->size ? size : slot->size, slot->fill)) {
                return;
            }
            slot->p = p;
            slot->size = size;
            memset(p, slot->fill, size);
        }
        if (op % 97 == 0 && !slots_intact(slots)) {
            return;
        }
    }
    *ok = slots_intact(slots);
}

/// @brief Returns the number of blocks handed out by all of a slab
/// allocator's pools.
static size_t slab_count(const sof_mem_slab_t *slab) {
    size_t n = 0;
    for (size_t i = 0; i < slab->classes; i++) {
        n += slab->pools[i].count;
    }
    return n;
}

/// @brief Returns 1 if every block in slots still holds its fill byte, so no
/// two blocks overlap.
static int slots_intact(const slot_t *slots) {
    for (size_t i = 0; i < SLOTS; i++) {
        if (slots[i].p != NULL && !filled(slots[i].p, slots[i].size, slots[i].fill)) {
            return 0;
        }
    }
    return 1;
}

/// @brief A tracker's error_func: counts each problem in a track_counts_t.
static void track_error(const void *p, size_t size, sof_mem_track_problem_t problem, void *ctx) {
    track_counts_t *counts = ctx;
    if (problem == SOF_MEM_TRACK_WRONG_SIZE) {
        counts->wrong_size++;
    } else {
        counts->unknown++;
    }
    counts->last_p = p;
    counts->last_size = size;
}

/// @brief A tracker's traversal callback: counts blocks and bytes, and stops
/// after stop_after visits.
static int track_visit(void *p, size_t size, void *ctx) {
    track_counts_t *counts = ctx;
    if (counts == NULL) {
        return 0;
    }
    counts->visits++;
    counts->visited_bytes += size;
    counts->last_p = p;
    counts->last_size = size;
    if (counts->stop_after != 0 && counts->visits == counts->stop_after) {
        return 42;
    }
    return 0;
}

static void test_arena_init(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_arena_t a;
    CHECK(sof_mem_arena_init(&a, 0, NULL) == SOF_MEM_OK);
    CHECK(a.ready == 1 && a.fixed == 0);
    CHECK(a.block_size == SOF_MEM_ARENA_BLOCK_SIZE);
    CHECK(a.blocks == NULL && a.current == NULL && a.large == NULL && a.allocated == 0);
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
    check_arena_dead(&a);
    // The block size is rounded up, and nothing is allocated yet.
    CHECK(sof_mem_arena_init(&a, 1000, &alloc) == SOF_MEM_OK);
    CHECK(a.block_size == ROUND(1000));
    CHECK(state.allocations == 0);
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
    CHECK(state.allocations == 0);
}

static void test_arena_bad_init(void) {
    CHECK(sof_mem_arena_init(NULL, 0, NULL) == SOF_MEM_ERROR);
    test_alloc_t state = {0};
    sof_alloc_t no_free = test_allocator(&state);
    no_free.free_sized = NULL;
    sof_mem_arena_t a;
    memset(&a, 0xff, sizeof(a));
    CHECK(sof_mem_arena_init(&a, 0, &no_free) == SOF_MEM_ERROR);
    check_arena_dead(&a);
    memset(&a, 0xff, sizeof(a));
    CHECK(sof_mem_arena_init(&a, SIZE_MAX, NULL) == SOF_MEM_ERROR);
    check_arena_dead(&a);
    memset(&a, 0xff, sizeof(a));
    CHECK(sof_mem_arena_init(&a, SIZE_MAX - 4, NULL) == SOF_MEM_ERROR);
    check_arena_dead(&a);
}

static void test_arena_alloc(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_arena_t a;
    CHECK(sof_mem_arena_init(&a, BLOCK, &alloc) == SOF_MEM_OK);
    CHECK(sof_mem_arena_alloc(&a, 0) == NULL);
    CHECK(state.allocations == 0);
    // 30 allocations of 100 bytes, rounded up, take several blocks.
    unsigned char *p[30];
    for (int i = 0; i < 30; i++) {
        p[i] = sof_mem_arena_alloc(&a, 100);
        CHECK(p[i] != NULL);
        CHECK(aligned(p[i]));
        memset(p[i], i, 100);
    }
    for (int i = 0; i < 30; i++) {
        CHECK(filled(p[i], 100, (unsigned char)i));
    }
    size_t per_block = BLOCK / ROUND(100);
    size_t blocks = (30 + per_block - 1) / per_block;
    CHECK(count_list(a.blocks) == blocks);
    CHECK(state.live == (long)blocks);
    CHECK(a.allocated == blocks * (HEADER + BLOCK));
    CHECK(a.large == NULL);
    // Allocations in one block follow each other.
    CHECK(p[1] == p[0] + ROUND(100));
    // Odd sizes stay aligned.
    for (size_t size = 1; size < 40; size++) {
        void *q = sof_mem_arena_alloc(&a, size);
        CHECK(q != NULL && aligned(q));
    }
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    check_arena_dead(&a);
}

static void test_arena_large(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_arena_t a;
    CHECK(sof_mem_arena_init(&a, BLOCK, &alloc) == SOF_MEM_OK);
    // A quarter of the block is still small; one byte more is large.
    void *small = sof_mem_arena_alloc(&a, BLOCK / 4);
    CHECK(small != NULL && a.large == NULL);
    CHECK(state.live == 1);
    unsigned char *big = sof_mem_arena_alloc(&a, BLOCK / 4 + 1);
    CHECK(big != NULL && aligned(big));
    CHECK(count_list(a.large) == 1);
    CHECK(state.live == 2);
    CHECK(a.allocated == 2 * HEADER + BLOCK + BLOCK / 4 + 1);
    unsigned char *huge = sof_mem_arena_alloc(&a, 10 * BLOCK);
    CHECK(huge != NULL && aligned(huge));
    memset(huge, 7, 10 * BLOCK);
    CHECK(count_list(a.large) == 2);
    CHECK(state.live == 3);
    // The small block is untouched by the large ones.
    CHECK(a.offset == BLOCK / 4);
    // Releasing a large block gives it back at once, from anywhere in the list.
    CHECK(sof_mem_arena_release(&a, big, BLOCK / 4 + 1) == SOF_MEM_OK);
    CHECK(count_list(a.large) == 1);
    CHECK(state.live == 2);
    CHECK(filled(huge, 10 * BLOCK, 7));
    CHECK(sof_mem_arena_release(&a, huge, 10 * BLOCK) == SOF_MEM_OK);
    CHECK(a.large == NULL);
    CHECK(state.live == 1);
    CHECK(a.allocated == HEADER + BLOCK);
    // An allocation too big to add a header to fails.
    CHECK(sof_mem_arena_alloc(&a, SIZE_MAX) == NULL);
    CHECK(sof_mem_arena_alloc(&a, SIZE_MAX - HEADER + 1) == NULL);
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_arena_release(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_arena_t a;
    CHECK(sof_mem_arena_init(&a, BLOCK, &alloc) == SOF_MEM_OK);
    CHECK(sof_mem_arena_release(&a, NULL, 10) == SOF_MEM_OK);
    unsigned char *x = sof_mem_arena_alloc(&a, 32);
    unsigned char *y = sof_mem_arena_alloc(&a, 32);
    size_t offset = a.offset;
    // Releasing a block that isn't the latest keeps its space.
    CHECK(sof_mem_arena_release(&a, x, 32) == SOF_MEM_OK);
    CHECK(a.offset == offset);
    // Releasing the latest takes it back, and the next allocation reuses it.
    CHECK(sof_mem_arena_release(&a, y, 32) == SOF_MEM_OK);
    CHECK(a.offset == (size_t)(y - x));
    CHECK(a.last == NULL);
    unsigned char *z = sof_mem_arena_alloc(&a, 20);
    CHECK(z == y);
    // Only one step back: after z is released, x isn't the latest.
    CHECK(sof_mem_arena_release(&a, z, 20) == SOF_MEM_OK);
    CHECK(sof_mem_arena_release(&a, x, 32) == SOF_MEM_OK);
    CHECK(a.offset == (size_t)(y - x));
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_arena_realloc(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_arena_t a;
    CHECK(sof_mem_arena_init(&a, BLOCK, &alloc) == SOF_MEM_OK);
    CHECK(sof_mem_arena_realloc(&a, NULL, 0, 0) == NULL);
    // NULL allocates.
    unsigned char *x = sof_mem_arena_realloc(&a, NULL, 0, 16);
    CHECK(x != NULL);
    memset(x, 1, 16);
    // The latest allocation grows and shrinks in place.
    CHECK(sof_mem_arena_realloc(&a, x, 16, 200) == x);
    CHECK(a.offset == ROUND(200));
    memset(x, 1, 200);
    CHECK(sof_mem_arena_realloc(&a, x, 200, 40) == x);
    CHECK(a.offset == ROUND(40));
    CHECK(filled(x, 40, 1));
    CHECK(sof_mem_arena_realloc(&a, x, 40, 0) == NULL);
    CHECK(filled(x, 40, 1));
    // Once it isn't the latest, it shrinks in place but grows by moving.
    unsigned char *y = sof_mem_arena_alloc(&a, 16);
    memset(y, 2, 16);
    size_t smaller = 40 - _Alignof(max_align_t) + 1;
    CHECK(sof_mem_arena_realloc(&a, x, 40, smaller) == x);
    unsigned char *moved = sof_mem_arena_realloc(&a, x, smaller, 100);
    CHECK(moved != NULL && moved != x);
    CHECK(filled(moved, smaller, 1));
    CHECK(filled(y, 16, 2));
    x = moved;
    memset(x, 1, 100);
    // The latest grows in place while it fits in its block.
    CHECK(sof_mem_arena_realloc(&a, x, 100, 250) == x);
    memset(x + 100, 1, 150);
    CHECK(sof_mem_arena_realloc(&a, x, 250, 256) == x);
    CHECK(filled(x, 250, 1));
    memset(x, 1, 256);
    // It moves to a new block when it doesn't.
    size_t blocks = count_list(a.blocks);
    unsigned char *t;
    do {
        t = sof_mem_arena_alloc(&a, 16);
        CHECK(t != NULL);
    } while (a.offset <= BLOCK - 256 + 16);
    memset(t, 3, 16);
    unsigned char *v = sof_mem_arena_realloc(&a, t, 16, 256);
    CHECK(v != NULL && v != t);
    CHECK(filled(v, 16, 3));
    CHECK(count_list(a.blocks) == blocks + 1);
    CHECK(a.last == v);
    memset(v, 3, 256);
    CHECK(sof_mem_arena_realloc(&a, v, 256, 256) == v);
    CHECK(sof_mem_arena_realloc(&a, v, 256, 160) == v);
    CHECK(filled(x, 256, 1));
    // Small to large, large to large and large to small, with three large
    // blocks in the list so each move has neighbours to relink.
    unsigned char *l1 = sof_mem_arena_alloc(&a, 300);
    unsigned char *l2 = sof_mem_arena_alloc(&a, 300);
    memset(l1, 4, 300);
    memset(l2, 5, 300);
    unsigned char *l3 = sof_mem_arena_realloc(&a, v, 160, 400);
    CHECK(l3 != NULL && l3 != v);
    CHECK(filled(l3, 160, 3));
    CHECK(count_list(a.large) == 3);
    memset(l3, 3, 400);
    unsigned char *l2b = sof_mem_arena_realloc(&a, l2, 300, 5000);
    CHECK(l2b != NULL);
    CHECK(filled(l2b, 300, 5));
    memset(l2b, 5, 5000);
    CHECK(count_list(a.large) == 3);
    CHECK(a.allocated == count_list(a.blocks) * (HEADER + BLOCK) + 3 * HEADER + 300 + 5000 + 400);
    unsigned char *s = sof_mem_arena_realloc(&a, l2b, 5000, 64);
    CHECK(s != NULL && s != l2b);
    CHECK(filled(s, 64, 5));
    CHECK(count_list(a.large) == 2);
    CHECK(filled(l1, 300, 4));
    CHECK(filled(l3, 400, 3));
    CHECK(sof_mem_arena_realloc(&a, l1, 300, SIZE_MAX) == NULL);
    CHECK(filled(l1, 300, 4));
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_arena_clear(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_arena_t a;
    CHECK(sof_mem_arena_init(&a, BLOCK, &alloc) == SOF_MEM_OK);
    void *first[40];
    for (int i = 0; i < 40; i++) {
        first[i] = sof_mem_arena_alloc(&a, 64 + i);
        CHECK(first[i] != NULL);
    }
    CHECK(sof_mem_arena_alloc(&a, 2000) != NULL);
    CHECK(sof_mem_arena_alloc(&a, 3000) != NULL);
    size_t blocks = count_list(a.blocks);
    CHECK(blocks > 2);
    CHECK(state.live == (long)blocks + 2);
    CHECK(sof_mem_arena_clear(&a) == SOF_MEM_OK);
    CHECK(a.large == NULL && a.current == NULL && a.offset == 0 && a.last == NULL);
    CHECK(state.live == (long)blocks);
    CHECK(a.allocated == blocks * (HEADER + BLOCK));
    // The same allocations again reuse the same blocks, in the same order,
    // and ask the parent for nothing.
    size_t allocations = state.allocations;
    for (int i = 0; i < 40; i++) {
        CHECK(sof_mem_arena_alloc(&a, 64 + i) == first[i]);
    }
    CHECK(state.allocations == allocations);
    CHECK(count_list(a.blocks) == blocks);
    // Going past them adds a block at the end.
    for (int i = 0; i < 20; i++) {
        CHECK(sof_mem_arena_alloc(&a, 200) != NULL);
    }
    CHECK(count_list(a.blocks) > blocks);
    CHECK(sof_mem_arena_clear(&a) == SOF_MEM_OK);
    CHECK(sof_mem_arena_clear(&a) == SOF_MEM_OK);
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_arena_marks(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_arena_t a;
    CHECK(sof_mem_arena_init(&a, BLOCK, &alloc) == SOF_MEM_OK);
    sof_mem_arena_mark_t empty = sof_mem_arena_mark(&a);
    CHECK(empty.block == NULL && empty.offset == 0);
    unsigned char *x = sof_mem_arena_alloc(&a, 100);
    memset(x, 1, 100);
    unsigned char *big = sof_mem_arena_alloc(&a, 2000);
    memset(big, 2, 2000);
    sof_mem_arena_mark_t mark = sof_mem_arena_mark(&a);
    // An allocation before the mark can't grow in place or be taken back.
    CHECK(a.last == NULL);
    size_t offset = a.offset;
    CHECK(sof_mem_arena_release(&a, x, 100) == SOF_MEM_OK);
    CHECK(a.offset == offset);
    // Allocate past the mark: standard blocks, large blocks, and a large
    // block from before the mark resized after it.
    for (int i = 0; i < 30; i++) {
        CHECK(sof_mem_arena_alloc(&a, 100) != NULL);
    }
    CHECK(sof_mem_arena_alloc(&a, 3000) != NULL);
    CHECK(sof_mem_arena_alloc(&a, 4000) != NULL);
    big = sof_mem_arena_realloc(&a, big, 2000, 6000);
    CHECK(big != NULL && filled(big, 2000, 2));
    memset(big, 2, 6000);
    size_t blocks = count_list(a.blocks);
    CHECK(blocks > 1);
    CHECK(count_list(a.large) == 3);
    // Going back frees the large blocks allocated since, keeps the one from
    // before, and keeps every standard block for reuse.
    size_t allocations = state.allocations;
    CHECK(sof_mem_arena_reset(&a, mark) == SOF_MEM_OK);
    CHECK(count_list(a.large) == 1);
    CHECK((unsigned char *)a.large + HEADER == big);
    CHECK(filled(big, 6000, 2));
    CHECK(filled(x, 100, 1));
    CHECK(count_list(a.blocks) == blocks);
    CHECK(state.live == (long)blocks + 1);
    CHECK(a.current == mark.block && a.offset == mark.offset);
    // The next allocation comes right after x, and reuse asks the parent
    // for nothing.
    CHECK(sof_mem_arena_alloc(&a, 100) == x + ROUND(100));
    for (int i = 0; i < 29; i++) {
        CHECK(sof_mem_arena_alloc(&a, 100) != NULL);
    }
    CHECK(state.allocations == allocations);
    // A mark can be used again, and an earlier mark after a later one.
    CHECK(sof_mem_arena_reset(&a, mark) == SOF_MEM_OK);
    CHECK(sof_mem_arena_reset(&a, mark) == SOF_MEM_OK);
    CHECK(sof_mem_arena_reset(&a, empty) == SOF_MEM_OK);
    CHECK(a.current == NULL && a.offset == 0 && a.large == NULL);
    CHECK(sof_mem_arena_alloc(&a, 100) == x);
    // Marks that can't be this arena's are refused.
    sof_mem_arena_mark_t bad = mark;
    bad.serial = a.serial + 1;
    CHECK(sof_mem_arena_reset(&a, bad) == SOF_MEM_ERROR);
    bad = mark;
    bad.offset = mark.block->size + 1;
    CHECK(sof_mem_arena_reset(&a, bad) == SOF_MEM_ERROR);
    bad = empty;
    bad.offset = 16;
    CHECK(sof_mem_arena_reset(&a, bad) == SOF_MEM_ERROR);
    CHECK(sof_mem_arena_reset(NULL, mark) == SOF_MEM_ERROR);
    sof_mem_arena_mark_t none = sof_mem_arena_mark(NULL);
    CHECK(none.block == NULL && none.offset == 0 && none.serial == 0);
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_arena_nested_marks(void) {
    sof_mem_arena_t a;
    CHECK(sof_mem_arena_init(&a, BLOCK, NULL) == SOF_MEM_OK);
    // Each level allocates, marks and recurses, then resets to its mark: the
    // offsets unwind exactly.
    sof_mem_arena_mark_t marks[10];
    size_t offsets[10];
    for (int level = 0; level < 10; level++) {
        CHECK(sof_mem_arena_alloc(&a, 50 + level * 30) != NULL);
        CHECK(sof_mem_arena_alloc(&a, 500) != NULL);
        marks[level] = sof_mem_arena_mark(&a);
        offsets[level] = a.offset;
    }
    for (int level = 9; level >= 0; level--) {
        CHECK(sof_mem_arena_alloc(&a, 700) != NULL);
        CHECK(sof_mem_arena_reset(&a, marks[level]) == SOF_MEM_OK);
        CHECK(a.offset == offsets[level]);
        CHECK(a.current == marks[level].block);
    }
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
}

static void test_arena_buffer(void) {
    sof_mem_arena_t a;
    // A buffer that isn't aligned is aligned first.
    unsigned char *start = buffer.bytes + 1;
    CHECK(sof_mem_arena_init_buffer(&a, start, 4000) == SOF_MEM_OK);
    CHECK(a.ready == 1 && a.fixed == 1);
    CHECK(a.allocated == 0);
    CHECK(a.block_size % _Alignof(max_align_t) == 0);
    CHECK(a.block_size <= 4000 - 1 - HEADER && a.block_size + 2 * _Alignof(max_align_t) > 4000 - HEADER);
    CHECK((unsigned char *)a.blocks >= start && aligned(a.blocks));
    // Every allocation comes from the buffer, large ones too, until it is
    // full.
    unsigned char *big = sof_mem_arena_alloc(&a, 2000);
    CHECK(big != NULL && aligned(big));
    CHECK(big >= start && big + 2000 <= start + 4000);
    CHECK(a.large == NULL);
    size_t total = ROUND(2000);
    unsigned char *p;
    while ((p = sof_mem_arena_alloc(&a, 100)) != NULL) {
        CHECK(p >= start && p + 100 <= start + 4000);
        total += ROUND(100);
    }
    CHECK(total <= a.block_size && total + ROUND(100) > a.block_size);
    CHECK(sof_mem_arena_alloc(&a, 5000) == NULL);
    CHECK(sof_mem_arena_alloc(&a, SIZE_MAX) == NULL);
    // The latest grows in place while it fits, and fails, unchanged, when
    // it doesn't.
    CHECK(sof_mem_arena_clear(&a) == SOF_MEM_OK);
    CHECK(sof_mem_arena_alloc(&a, 2000) == big);
    unsigned char *q = sof_mem_arena_alloc(&a, 100);
    memset(q, 9, 100);
    CHECK(sof_mem_arena_realloc(&a, q, 100, 1000) == q);
    CHECK(sof_mem_arena_realloc(&a, q, 1000, 3000) == NULL);
    CHECK(filled(q, 100, 9));
    // Marks work the same way.
    sof_mem_arena_mark_t mark = sof_mem_arena_mark(&a);
    CHECK(sof_mem_arena_alloc(&a, 300) != NULL);
    CHECK(sof_mem_arena_reset(&a, mark) == SOF_MEM_OK);
    CHECK(sof_mem_arena_alloc(&a, 300) == q + ROUND(1000));
    // Freeing leaves the buffer alone, and it can be used again.
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
    check_arena_dead(&a);
    CHECK(sof_mem_arena_init_buffer(&a, buffer.bytes, sizeof(buffer.bytes)) == SOF_MEM_OK);
    CHECK(a.block_size == sizeof(buffer.bytes) - HEADER);
    CHECK(sof_mem_arena_alloc(&a, 100000) == buffer.bytes + HEADER);
    sof_alloc_t alloc = sof_mem_arena_allocator(&a);
    char *s = sof_alloc_strndup(&alloc, "fixed", 5);
    CHECK(s != NULL && strcmp(s, "fixed") == 0);
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
}

static void test_arena_bad_buffer(void) {
    sof_mem_arena_t a;
    CHECK(sof_mem_arena_init_buffer(NULL, buffer.bytes, 4096) == SOF_MEM_ERROR);
    memset(&a, 0xff, sizeof(a));
    CHECK(sof_mem_arena_init_buffer(&a, NULL, 4096) == SOF_MEM_ERROR);
    check_arena_dead(&a);
    memset(&a, 0xff, sizeof(a));
    CHECK(sof_mem_arena_init_buffer(&a, buffer.bytes, HEADER) == SOF_MEM_ERROR);
    check_arena_dead(&a);
    // One aligned unit after the header is the least it takes.
    CHECK(sof_mem_arena_init_buffer(&a, buffer.bytes, HEADER + _Alignof(max_align_t)) == SOF_MEM_OK);
    CHECK(sof_mem_arena_alloc(&a, _Alignof(max_align_t)) != NULL);
    CHECK(sof_mem_arena_alloc(&a, 1) == NULL);
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
    memset(&a, 0xff, sizeof(a));
    CHECK(sof_mem_arena_init_buffer(&a, buffer.bytes + 1, HEADER + _Alignof(max_align_t)) == SOF_MEM_ERROR);
    check_arena_dead(&a);
}

static void test_arena_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t parent = test_allocator(&state);
    sof_mem_arena_t a;
    CHECK(sof_mem_arena_init(&a, BLOCK, &parent) == SOF_MEM_OK);
    sof_alloc_t alloc = sof_mem_arena_allocator(&a);
    CHECK(sof_alloc_valid(&alloc));
    CHECK(alloc.ctx == &a);
    char *s = sof_alloc_strndup(&alloc, "hello", 5);
    CHECK(s != NULL && strcmp(s, "hello") == 0);
    unsigned char *z = sof_alloc_calloc(&alloc, 10, 10);
    CHECK(z != NULL && filled(z, 100, 0));
    z = sof_alloc_realloc(&alloc, z, 100, 150);
    CHECK(z != NULL && filled(z, 100, 0));
    void *big = sof_alloc_malloc(&alloc, 5000);
    CHECK(big != NULL && count_list(a.large) == 1);
    sof_alloc_free(&alloc, big, 5000);
    CHECK(a.large == NULL);
    sof_alloc_free(&alloc, z, 150);
    sof_alloc_free(&alloc, s, 6);
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    // An allocator of a NULL arena is valid, but allocates nothing.
    alloc = sof_mem_arena_allocator(NULL);
    CHECK(sof_alloc_valid(&alloc));
    CHECK(sof_alloc_malloc(&alloc, 8) == NULL);
    // Freeing through it does nothing. The pointer is hidden from gcc, which
    // would otherwise warn of free() on a stack object, not seeing that the
    // arena's free is called instead.
    void *volatile not_heap = &a;
    sof_alloc_free(&alloc, not_heap, 8);
}

static void test_arena_random(void) {
    test_alloc_t state = {0};
    sof_alloc_t parent = test_allocator(&state);
    sof_mem_arena_t a;
    CHECK(sof_mem_arena_init(&a, BLOCK, &parent) == SOF_MEM_OK);
    sof_alloc_t alloc = sof_mem_arena_allocator(&a);
    slot_t slots[SLOTS] = {{0}};
    int ok;
    random_ops(&alloc, 0x1234567887654321ULL, slots, &ok);
    CHECK(ok);
    CHECK(sof_mem_arena_clear(&a) == SOF_MEM_OK);
    memset(slots, 0, sizeof(slots));
    random_ops(&alloc, 0x1111222233334444ULL, slots, &ok);
    CHECK(ok);
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_arena_no_memory(void) {
    // Fail each allocation from the parent in turn, until a run makes none
    // fail. Each failure returns NULL and leaves what was allocated intact,
    // and nothing leaks.
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_mem_arena_t a;
        CHECK(sof_mem_arena_init(&a, BLOCK, &alloc) == SOF_MEM_OK);
        slot_t slots[SLOTS] = {{0}};
        int failed = 0;
        for (size_t i = 0; i < SLOTS; i++) {
            size_t size = i % 5 == 0 ? 400 + i : 100 + i;
            slots[i].p = sof_mem_arena_alloc(&a, size);
            if (slots[i].p == NULL) {
                failed = 1;
                continue;
            }
            slots[i].size = size;
            slots[i].fill = (unsigned char)i;
            memset(slots[i].p, slots[i].fill, size);
        }
        for (size_t i = 0; i < SLOTS; i += 3) {
            if (slots[i].p == NULL) {
                continue;
            }
            size_t size = slots[i].size * 3;
            unsigned char *p = sof_mem_arena_realloc(&a, slots[i].p, slots[i].size, size);
            if (p == NULL) {
                failed = 1;
                continue;
            }
            memset(p, slots[i].fill, size);
            slots[i].p = p;
            slots[i].size = size;
        }
        CHECK(slots_intact(slots));
        CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (!failed) {
            break;
        }
    }
}

static void test_arena_null(void) {
    CHECK(sof_mem_arena_alloc(NULL, 8) == NULL);
    CHECK(sof_mem_arena_realloc(NULL, NULL, 0, 8) == NULL);
    CHECK(sof_mem_arena_release(NULL, NULL, 0) == SOF_MEM_ERROR);
    CHECK(sof_mem_arena_clear(NULL) == SOF_MEM_ERROR);
    CHECK(sof_mem_arena_free(NULL) == SOF_MEM_ERROR);
    sof_mem_arena_t a;
    CHECK(sof_mem_arena_init(&a, BLOCK, NULL) == SOF_MEM_OK);
    void *p = sof_mem_arena_alloc(&a, 8);
    CHECK(p != NULL);
    CHECK(sof_mem_arena_release(NULL, p, 8) == SOF_MEM_ERROR);
    CHECK(sof_mem_arena_realloc(NULL, p, 8, 16) == NULL);
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
}

static void test_arena_freed(void) {
    sof_mem_arena_t a;
    CHECK(sof_mem_arena_init(&a, BLOCK, NULL) == SOF_MEM_OK);
    CHECK(sof_mem_arena_alloc(&a, 100) != NULL);
    CHECK(sof_mem_arena_alloc(&a, 1000) != NULL);
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
    check_arena_dead(&a);
    // It can be initialized again.
    CHECK(sof_mem_arena_init(&a, 0, NULL) == SOF_MEM_OK);
    CHECK(sof_mem_arena_alloc(&a, 100) != NULL);
    CHECK(sof_mem_arena_free(&a) == SOF_MEM_OK);
}

static void test_pool_init(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_pool_t pool;
    CHECK(sof_mem_pool_init(&pool, 48, 0, &alloc) == SOF_MEM_OK);
    CHECK(pool.ready == 1);
    CHECK(pool.object_size == 48 && pool.slot_size == ROUND(48));
    CHECK(pool.per_slab == (SOF_MEM_POOL_SLAB_SIZE - HEADER) / pool.slot_size);
    CHECK(pool.count == 0 && pool.allocated == 0 && pool.slabs == NULL);
    CHECK(state.allocations == 0);
    CHECK(sof_mem_pool_free(&pool) == SOF_MEM_OK);
    check_pool_dead(&pool);
    // A tiny object still has room for the free list's pointer, and is
    // aligned.
    CHECK(sof_mem_pool_init(&pool, 1, 3, NULL) == SOF_MEM_OK);
    CHECK(pool.slot_size >= sizeof(void *));
    CHECK(pool.slot_size % _Alignof(max_align_t) == 0);
    CHECK(pool.per_slab == 3);
    CHECK(sof_mem_pool_free(&pool) == SOF_MEM_OK);
    // An object bigger than the default slab gets a slab of one.
    CHECK(sof_mem_pool_init(&pool, 2 * SOF_MEM_POOL_SLAB_SIZE, 0, NULL) == SOF_MEM_OK);
    CHECK(pool.per_slab == 1);
    CHECK(sof_mem_pool_free(&pool) == SOF_MEM_OK);
}

static void test_pool_bad_init(void) {
    CHECK(sof_mem_pool_init(NULL, 8, 0, NULL) == SOF_MEM_ERROR);
    sof_mem_pool_t pool;
    memset(&pool, 0xff, sizeof(pool));
    CHECK(sof_mem_pool_init(&pool, 0, 0, NULL) == SOF_MEM_ERROR);
    check_pool_dead(&pool);
    test_alloc_t state = {0};
    sof_alloc_t no_free = test_allocator(&state);
    no_free.free_sized = NULL;
    memset(&pool, 0xff, sizeof(pool));
    CHECK(sof_mem_pool_init(&pool, 8, 0, &no_free) == SOF_MEM_ERROR);
    check_pool_dead(&pool);
    memset(&pool, 0xff, sizeof(pool));
    CHECK(sof_mem_pool_init(&pool, SIZE_MAX, 0, NULL) == SOF_MEM_ERROR);
    check_pool_dead(&pool);
    memset(&pool, 0xff, sizeof(pool));
    CHECK(sof_mem_pool_init(&pool, 64, SIZE_MAX / 32, NULL) == SOF_MEM_ERROR);
    check_pool_dead(&pool);
}

static void test_pool_alloc(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_pool_t pool;
    CHECK(sof_mem_pool_init(&pool, 24, 4, &alloc) == SOF_MEM_OK);
    unsigned char *p[10];
    for (int i = 0; i < 10; i++) {
        p[i] = sof_mem_pool_alloc(&pool);
        CHECK(p[i] != NULL && aligned(p[i]));
        memset(p[i], i, 24);
    }
    for (int i = 0; i < 10; i++) {
        CHECK(filled(p[i], 24, (unsigned char)i));
    }
    CHECK(pool.count == 10);
    CHECK(count_list(pool.slabs) == 3);
    CHECK(state.live == 3);
    CHECK(pool.allocated == 3 * (HEADER + 4 * pool.slot_size));
    // Released objects come back last in, first out.
    CHECK(sof_mem_pool_release(&pool, p[3]) == SOF_MEM_OK);
    CHECK(sof_mem_pool_release(&pool, p[7]) == SOF_MEM_OK);
    CHECK(sof_mem_pool_release(&pool, NULL) == SOF_MEM_OK);
    CHECK(pool.count == 8);
    CHECK(sof_mem_pool_alloc(&pool) == p[7]);
    CHECK(sof_mem_pool_alloc(&pool) == p[3]);
    CHECK(pool.count == 10);
    CHECK(state.live == 3);
    for (int i = 0; i < 10; i++) {
        if (i != 3 && i != 7) {
            CHECK(filled(p[i], 24, (unsigned char)i));
        }
    }
    CHECK(sof_mem_pool_free(&pool) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    check_pool_dead(&pool);
}

static void test_pool_clear(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_pool_t pool;
    CHECK(sof_mem_pool_init(&pool, 32, 5, &alloc) == SOF_MEM_OK);
    void *first[12];
    for (int i = 0; i < 12; i++) {
        first[i] = sof_mem_pool_alloc(&pool);
    }
    sof_mem_pool_release(&pool, first[2]);
    CHECK(sof_mem_pool_clear(&pool) == SOF_MEM_OK);
    CHECK(pool.count == 0 && pool.free_list == NULL && pool.current == NULL);
    CHECK(count_list(pool.slabs) == 3);
    CHECK(state.live == 3);
    size_t allocations = state.allocations;
    for (int i = 0; i < 12; i++) {
        CHECK(sof_mem_pool_alloc(&pool) == first[i]);
    }
    CHECK(state.allocations == allocations);
    for (int i = 0; i < 4; i++) {
        CHECK(sof_mem_pool_alloc(&pool) != NULL);
    }
    CHECK(count_list(pool.slabs) == 4);
    CHECK(sof_mem_pool_free(&pool) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_pool_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t parent = test_allocator(&state);
    sof_mem_pool_t pool;
    CHECK(sof_mem_pool_init(&pool, 40, 8, &parent) == SOF_MEM_OK);
    sof_alloc_t alloc = sof_mem_pool_allocator(&pool);
    CHECK(sof_alloc_valid(&alloc));
    // Up to object_size comes from the pool.
    unsigned char *a = sof_alloc_malloc(&alloc, 40);
    unsigned char *b = sof_alloc_malloc(&alloc, 1);
    CHECK(a != NULL && b != NULL);
    CHECK(pool.count == 2 && state.live == 1);
    // Larger goes to the parent.
    unsigned char *c = sof_alloc_malloc(&alloc, 41);
    CHECK(c != NULL);
    CHECK(pool.count == 2 && state.live == 2);
    memset(a, 1, 40);
    memset(c, 3, 41);
    // Within the pool, a realloc keeps the object.
    CHECK(sof_alloc_realloc(&alloc, b, 1, 40) == b);
    // Out of the pool and back.
    unsigned char *a2 = sof_alloc_realloc(&alloc, a, 40, 100);
    CHECK(a2 != NULL && a2 != a);
    CHECK(filled(a2, 40, 1));
    CHECK(pool.count == 1 && state.live == 3);
    memset(a2, 1, 100);
    a2 = sof_alloc_realloc(&alloc, a2, 100, 1000);
    CHECK(a2 != NULL && filled(a2, 100, 1));
    CHECK(pool.count == 1 && state.live == 3);
    unsigned char *a3 = sof_alloc_realloc(&alloc, a2, 1000, 16);
    CHECK(a3 != NULL && filled(a3, 16, 1));
    CHECK(pool.count == 2 && state.live == 2);
    // Frees go where the size says.
    sof_alloc_free(&alloc, c, 41);
    CHECK(state.live == 1);
    sof_alloc_free(&alloc, a3, 16);
    sof_alloc_free(&alloc, b, 40);
    CHECK(pool.count == 0);
    char *s = sof_alloc_strndup(&alloc, "short", 5);
    CHECK(s != NULL && pool.count == 1);
    sof_alloc_free(&alloc, s, 6);
    CHECK(sof_mem_pool_free(&pool) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    alloc = sof_mem_pool_allocator(NULL);
    CHECK(sof_alloc_valid(&alloc));
    CHECK(sof_alloc_malloc(&alloc, 8) == NULL);
    CHECK(sof_alloc_realloc(&alloc, &pool, 8, 16) == NULL);
    sof_alloc_free(&alloc, &pool, 8);
}

static void test_pool_random(void) {
    test_alloc_t state = {0};
    sof_alloc_t parent = test_allocator(&state);
    sof_mem_pool_t pool;
    CHECK(sof_mem_pool_init(&pool, 56, 7, &parent) == SOF_MEM_OK);
    sof_alloc_t alloc = sof_mem_pool_allocator(&pool);
    slot_t slots[SLOTS] = {{0}};
    int ok;
    random_ops(&alloc, 0x0badc0ffee15900dULL, slots, &ok);
    CHECK(ok);
    size_t objects = 0;
    for (size_t i = 0; i < SLOTS; i++) {
        objects += slots[i].p != NULL && slots[i].size <= 56;
    }
    CHECK(pool.count == objects);
    for (size_t i = 0; i < SLOTS; i++) {
        sof_alloc_free(&alloc, slots[i].p, slots[i].size);
    }
    CHECK(pool.count == 0);
    CHECK(sof_mem_pool_free(&pool) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_pool_no_memory(void) {
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t parent = test_allocator(&state);
        sof_mem_pool_t pool;
        CHECK(sof_mem_pool_init(&pool, 16, 4, &parent) == SOF_MEM_OK);
        sof_alloc_t alloc = sof_mem_pool_allocator(&pool);
        slot_t slots[SLOTS] = {{0}};
        int failed = 0;
        for (size_t i = 0; i < SLOTS; i++) {
            size_t size = i % 4 == 0 ? 100 : 16;
            slots[i].p = sof_alloc_malloc(&alloc, size);
            if (slots[i].p == NULL) {
                failed = 1;
                continue;
            }
            slots[i].size = size;
            slots[i].fill = (unsigned char)i;
            memset(slots[i].p, slots[i].fill, size);
        }
        // Grow some out of the pool, which can fail too.
        for (size_t i = 1; i < SLOTS; i += 4) {
            if (slots[i].p == NULL) {
                continue;
            }
            unsigned char *p = sof_alloc_realloc(&alloc, slots[i].p, 16, 64);
            if (p == NULL) {
                failed = 1;
                CHECK(filled(slots[i].p, 16, slots[i].fill));
                continue;
            }
            memset(p, slots[i].fill, 64);
            slots[i].p = p;
            slots[i].size = 64;
        }
        CHECK(slots_intact(slots));
        for (size_t i = 0; i < SLOTS; i++) {
            sof_alloc_free(&alloc, slots[i].p, slots[i].size);
        }
        CHECK(pool.count == 0);
        CHECK(sof_mem_pool_free(&pool) == SOF_MEM_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (!failed) {
            break;
        }
    }
}

static void test_pool_null(void) {
    CHECK(sof_mem_pool_alloc(NULL) == NULL);
    CHECK(sof_mem_pool_release(NULL, NULL) == SOF_MEM_ERROR);
    CHECK(sof_mem_pool_clear(NULL) == SOF_MEM_ERROR);
    CHECK(sof_mem_pool_free(NULL) == SOF_MEM_ERROR);
}

static void test_pool_freed(void) {
    sof_mem_pool_t pool;
    CHECK(sof_mem_pool_init(&pool, 16, 0, NULL) == SOF_MEM_OK);
    CHECK(sof_mem_pool_alloc(&pool) != NULL);
    CHECK(sof_mem_pool_free(&pool) == SOF_MEM_OK);
    check_pool_dead(&pool);
    CHECK(sof_mem_pool_init(&pool, 16, 0, NULL) == SOF_MEM_OK);
    CHECK(sof_mem_pool_alloc(&pool) != NULL);
    CHECK(sof_mem_pool_free(&pool) == SOF_MEM_OK);
}

static void test_slab_init(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_slab_t slab;
    CHECK(sof_mem_slab_init(&slab, 0, 0, &alloc) == SOF_MEM_OK);
    CHECK(slab.ready == 1);
    CHECK(slab.max_size == SOF_MEM_SLAB_MAX_SIZE);
    CHECK(slab.pools[slab.classes - 1].object_size == slab.max_size);
    // Only the array of pools is allocated.
    CHECK(state.live == 1);
    // The classes are aligned and increase, and the default slab holds as
    // many of each as fit in SOF_MEM_POOL_SLAB_SIZE bytes.
    for (size_t i = 0; i < slab.classes; i++) {
        sof_mem_pool_t *pool = &slab.pools[i];
        CHECK(pool->object_size % _Alignof(max_align_t) == 0);
        CHECK(pool->slot_size == pool->object_size);
        CHECK(i == 0 || pool->object_size > slab.pools[i - 1].object_size);
        CHECK(pool->per_slab == (SOF_MEM_POOL_SLAB_SIZE - HEADER) / pool->object_size);
    }
    CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    check_slab_dead(&slab);
    // max_size rounds up to a class; slab_size sets the slabs.
    CHECK(sof_mem_slab_init(&slab, 1000, 4096, NULL) == SOF_MEM_OK);
    CHECK(slab.max_size == 1024);
    CHECK(slab.pools[slab.classes - 1].per_slab == (4096 - HEADER) / 1024);
    CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
    CHECK(sof_mem_slab_init(&slab, SOF_MEM_SLAB_LIMIT, 100, NULL) == SOF_MEM_OK);
    CHECK(slab.max_size == SOF_MEM_SLAB_LIMIT);
    CHECK(slab.pools[slab.classes - 1].per_slab == 1);
    CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
    CHECK(sof_mem_slab_init(&slab, 1, 0, NULL) == SOF_MEM_OK);
    CHECK(slab.classes == 1 && slab.max_size == _Alignof(max_align_t));
    CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
}

static void test_slab_bad_init(void) {
    CHECK(sof_mem_slab_init(NULL, 0, 0, NULL) == SOF_MEM_ERROR);
    sof_mem_slab_t slab;
    memset(&slab, 0xff, sizeof(slab));
    CHECK(sof_mem_slab_init(&slab, SOF_MEM_SLAB_LIMIT + 1, 0, NULL) == SOF_MEM_ERROR);
    check_slab_dead(&slab);
    test_alloc_t state = {0};
    sof_alloc_t no_free = test_allocator(&state);
    no_free.free_sized = NULL;
    memset(&slab, 0xff, sizeof(slab));
    CHECK(sof_mem_slab_init(&slab, 0, 0, &no_free) == SOF_MEM_ERROR);
    check_slab_dead(&slab);
    sof_alloc_t alloc = test_allocator(&state);
    state.fail_at = 1;
    memset(&slab, 0xff, sizeof(slab));
    CHECK(sof_mem_slab_init(&slab, 0, 0, &alloc) == SOF_MEM_NO_MEM);
    check_slab_dead(&slab);
    CHECK(state.live == 0);
}

static void test_slab_classes(void) {
    sof_mem_slab_t slab;
    CHECK(sof_mem_slab_init(&slab, SOF_MEM_SLAB_LIMIT, 0, NULL) == SOF_MEM_OK);
    // Every size goes to the smallest class that holds it, which wastes
    // less than the alignment or a fifth of the class.
    for (size_t size = 1; size <= slab.max_size; size++) {
        void *p = sof_mem_slab_alloc(&slab, size);
        CHECK(p != NULL && aligned(p));
        size_t class = slab.classes;
        for (size_t i = 0; i < slab.classes; i++) {
            if (slab.pools[i].count == 1) {
                class = i;
            }
        }
        CHECK(class < slab.classes);
        size_t class_size = slab.pools[class].object_size;
        CHECK(class_size >= size);
        CHECK(class == 0 || slab.pools[class - 1].object_size < size);
        CHECK(class_size - size < _Alignof(max_align_t) || (class_size - size) * 5 <= class_size);
        CHECK(sof_mem_slab_release(&slab, p, size) == SOF_MEM_OK);
        CHECK(slab_count(&slab) == 0);
    }
    CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
}

static void test_slab_alloc(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_slab_t slab;
    CHECK(sof_mem_slab_init(&slab, 0, 4096, &alloc) == SOF_MEM_OK);
    CHECK(sof_mem_slab_alloc(&slab, 0) == NULL);
    // Blocks of many sizes are distinct, aligned and keep their contents.
    unsigned char *p[100];
    for (int i = 0; i < 100; i++) {
        p[i] = sof_mem_slab_alloc(&slab, 1 + i * 10);
        CHECK(p[i] != NULL && aligned(p[i]));
        memset(p[i], i, 1 + i * 10);
    }
    for (int i = 0; i < 100; i++) {
        CHECK(filled(p[i], 1 + i * 10, (unsigned char)i));
    }
    CHECK(slab_count(&slab) == 100);
    // A released block is the next one of its class handed out.
    CHECK(sof_mem_slab_release(&slab, p[50], 501) == SOF_MEM_OK);
    CHECK(sof_mem_slab_alloc(&slab, 500) == p[50]);
    CHECK(sof_mem_slab_release(&slab, NULL, 10) == SOF_MEM_OK);
    // Larger than max_size goes to the parent, and back to it.
    long live = state.live;
    unsigned char *big = sof_mem_slab_alloc(&slab, slab.max_size + 1);
    CHECK(big != NULL);
    CHECK(state.live == live + 1);
    CHECK(slab_count(&slab) == 100);
    CHECK(sof_mem_slab_release(&slab, big, slab.max_size + 1) == SOF_MEM_OK);
    CHECK(state.live == live);
    for (int i = 0; i < 100; i++) {
        CHECK(sof_mem_slab_release(&slab, p[i], 1 + i * 10) == SOF_MEM_OK);
    }
    CHECK(slab_count(&slab) == 0);
    CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_slab_realloc(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_slab_t slab;
    CHECK(sof_mem_slab_init(&slab, 0, 0, &alloc) == SOF_MEM_OK);
    CHECK(sof_mem_slab_realloc(&slab, NULL, 0, 0) == NULL);
    unsigned char *p = sof_mem_slab_realloc(&slab, NULL, 0, 129);
    CHECK(p != NULL);
    memset(p, 1, 129);
    // Within its class it stays; across classes it moves.
    CHECK(sof_mem_slab_realloc(&slab, p, 129, 160) == p);
    memset(p, 1, 160);
    CHECK(sof_mem_slab_realloc(&slab, p, 160, 0) == NULL);
    unsigned char *q = sof_mem_slab_realloc(&slab, p, 160, 161);
    CHECK(q != NULL && q != p && filled(q, 160, 1));
    CHECK(slab_count(&slab) == 1);
    memset(q, 1, 161);
    q = sof_mem_slab_realloc(&slab, q, 161, 20);
    CHECK(q != NULL && filled(q, 20, 1));
    CHECK(slab_count(&slab) == 1);
    // Out to the parent, resized there, and back.
    long live = state.live;
    q = sof_mem_slab_realloc(&slab, q, 20, 5000);
    CHECK(q != NULL && filled(q, 20, 1));
    CHECK(slab_count(&slab) == 0 && state.live == live + 1);
    memset(q, 1, 5000);
    q = sof_mem_slab_realloc(&slab, q, 5000, 9000);
    CHECK(q != NULL && filled(q, 5000, 1));
    CHECK(state.live == live + 1);
    // Back in, to a class not used before, which takes a slab, and the
    // parent's block is freed.
    q = sof_mem_slab_realloc(&slab, q, 9000, 100);
    CHECK(q != NULL && filled(q, 100, 1));
    CHECK(slab_count(&slab) == 1 && state.live == live + 1);
    CHECK(sof_mem_slab_release(&slab, q, 100) == SOF_MEM_OK);
    CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_slab_clear(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_mem_slab_t slab;
    CHECK(sof_mem_slab_init(&slab, 0, 1024, &alloc) == SOF_MEM_OK);
    void *first[50];
    for (int i = 0; i < 50; i++) {
        first[i] = sof_mem_slab_alloc(&slab, 8 + (i % 5) * 40);
    }
    long live = state.live;
    CHECK(sof_mem_slab_clear(&slab) == SOF_MEM_OK);
    CHECK(slab_count(&slab) == 0);
    CHECK(state.live == live);
    size_t allocations = state.allocations;
    for (int i = 0; i < 50; i++) {
        CHECK(sof_mem_slab_alloc(&slab, 8 + (i % 5) * 40) == first[i]);
    }
    CHECK(state.allocations == allocations);
    CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_slab_random(void) {
    test_alloc_t state = {0};
    sof_alloc_t parent = test_allocator(&state);
    sof_mem_slab_t slab;
    CHECK(sof_mem_slab_init(&slab, 256, 0, &parent) == SOF_MEM_OK);
    sof_alloc_t alloc = sof_mem_slab_allocator(&slab);
    slot_t slots[SLOTS] = {{0}};
    int ok;
    random_ops(&alloc, 0x5eed5eed5eed5eedULL, slots, &ok);
    CHECK(ok);
    size_t small = 0;
    for (size_t i = 0; i < SLOTS; i++) {
        small += slots[i].p != NULL && slots[i].size <= 256;
    }
    CHECK(slab_count(&slab) == small);
    for (size_t i = 0; i < SLOTS; i++) {
        sof_alloc_free(&alloc, slots[i].p, slots[i].size);
    }
    CHECK(slab_count(&slab) == 0);
    CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_slab_no_memory(void) {
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t parent = test_allocator(&state);
        sof_mem_slab_t slab;
        sof_mem_error_t rc = sof_mem_slab_init(&slab, 0, 2048, &parent);
        if (rc == SOF_MEM_NO_MEM) {
            check_slab_dead(&slab);
            CHECK(state.live == 0);
            continue;
        }
        CHECK(rc == SOF_MEM_OK);
        sof_alloc_t alloc = sof_mem_slab_allocator(&slab);
        slot_t slots[SLOTS] = {{0}};
        int failed = 0;
        for (size_t i = 0; i < SLOTS; i++) {
            size_t size = i % 8 == 0 ? 2000 : 10 + i * 13;
            slots[i].p = sof_alloc_malloc(&alloc, size);
            if (slots[i].p == NULL) {
                failed = 1;
                continue;
            }
            slots[i].size = size;
            slots[i].fill = (unsigned char)i;
            memset(slots[i].p, slots[i].fill, size);
        }
        for (size_t i = 1; i < SLOTS; i += 3) {
            if (slots[i].p == NULL) {
                continue;
            }
            size_t size = slots[i].size * 2;
            unsigned char *p = sof_alloc_realloc(&alloc, slots[i].p, slots[i].size, size);
            if (p == NULL) {
                failed = 1;
                CHECK(filled(slots[i].p, slots[i].size, slots[i].fill));
                continue;
            }
            memset(p, slots[i].fill, size);
            slots[i].p = p;
            slots[i].size = size;
        }
        CHECK(slots_intact(slots));
        for (size_t i = 0; i < SLOTS; i++) {
            sof_alloc_free(&alloc, slots[i].p, slots[i].size);
        }
        CHECK(slab_count(&slab) == 0);
        CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (!failed) {
            break;
        }
    }
}

static void test_slab_null(void) {
    CHECK(sof_mem_slab_alloc(NULL, 8) == NULL);
    CHECK(sof_mem_slab_realloc(NULL, NULL, 0, 8) == NULL);
    CHECK(sof_mem_slab_release(NULL, NULL, 0) == SOF_MEM_ERROR);
    CHECK(sof_mem_slab_clear(NULL) == SOF_MEM_ERROR);
    CHECK(sof_mem_slab_free(NULL) == SOF_MEM_ERROR);
    sof_alloc_t alloc = sof_mem_slab_allocator(NULL);
    CHECK(sof_alloc_valid(&alloc));
    CHECK(sof_alloc_malloc(&alloc, 8) == NULL);
}

static void test_slab_freed(void) {
    sof_mem_slab_t slab;
    CHECK(sof_mem_slab_init(&slab, 0, 0, NULL) == SOF_MEM_OK);
    CHECK(sof_mem_slab_alloc(&slab, 100) != NULL);
    CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
    check_slab_dead(&slab);
    CHECK(sof_mem_slab_init(&slab, 0, 0, NULL) == SOF_MEM_OK);
    CHECK(sof_mem_slab_alloc(&slab, 100) != NULL);
    CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
}

static void test_track_counts(void) {
    test_alloc_t state = {0};
    sof_alloc_t parent = test_allocator(&state);
    sof_mem_track_t t;
    CHECK(sof_mem_track_init(&t, NULL, NULL, &parent) == SOF_MEM_OK);
    CHECK(t.ready == 1 && t.blocks == 0 && t.bytes == 0 && t.live == NULL);
    CHECK(sof_mem_track_alloc(&t, 0) == NULL);
    unsigned char *a = sof_mem_track_alloc(&t, 100);
    unsigned char *b = sof_mem_track_alloc(&t, 50);
    unsigned char *c = sof_mem_track_alloc(&t, 7);
    CHECK(a != NULL && b != NULL && c != NULL);
    CHECK(aligned(a) && aligned(b) && aligned(c));
    memset(a, 1, 100);
    memset(b, 2, 50);
    memset(c, 3, 7);
    CHECK(t.blocks == 3 && t.bytes == 157);
    CHECK(t.allocations == 3 && t.peak_bytes == 157 && t.peak_blocks == 3);
    CHECK(state.live == 3);
    // A realloc in the middle of the list keeps it linked.
    b = sof_mem_track_realloc(&t, b, 50, 5000);
    CHECK(b != NULL && filled(b, 50, 2));
    CHECK(t.bytes == 5107 && t.blocks == 3 && t.reallocations == 1);
    CHECK(t.peak_bytes == 5107);
    b = sof_mem_track_realloc(&t, b, 5000, 10);
    CHECK(b != NULL && filled(b, 10, 2));
    CHECK(t.bytes == 117 && t.peak_bytes == 5107);
    CHECK(sof_mem_track_release(&t, b, 10) == SOF_MEM_OK);
    CHECK(t.blocks == 2 && t.bytes == 107 && t.frees == 1);
    CHECK(filled(a, 100, 1) && filled(c, 7, 3));
    CHECK(sof_mem_track_release(&t, NULL, 10) == SOF_MEM_OK);
    // A NULL realloc allocates.
    unsigned char *d = sof_mem_track_realloc(&t, NULL, 0, 30);
    CHECK(d != NULL && t.blocks == 3 && t.allocations == 4);
    CHECK(sof_mem_track_realloc(&t, d, 30, 0) == NULL);
    CHECK(t.blocks == 3);
    CHECK(sof_mem_track_release(&t, a, 100) == SOF_MEM_OK);
    CHECK(sof_mem_track_release(&t, c, 7) == SOF_MEM_OK);
    CHECK(sof_mem_track_release(&t, d, 30) == SOF_MEM_OK);
    CHECK(t.blocks == 0 && t.bytes == 0 && t.live == NULL && t.frees == 4);
    CHECK(t.errors == 0 && t.failures == 0);
    CHECK(state.live == 0);
    CHECK(sof_mem_track_free(&t) == SOF_MEM_OK);
    CHECK(state.bad == 0);
    check_track_dead(&t);
}

static void test_track_errors(void) {
    test_alloc_t state = {0};
    sof_alloc_t parent = test_allocator(&state);
    track_counts_t counts = {0};
    sof_mem_track_t t;
    CHECK(sof_mem_track_init(&t, track_error, &counts, &parent) == SOF_MEM_OK);
    // A free with the wrong size is reported, and frees with the right one,
    // so the parent sees nothing wrong.
    unsigned char *a = sof_mem_track_alloc(&t, 100);
    CHECK(sof_mem_track_release(&t, a, 99) == SOF_MEM_OK);
    CHECK(counts.wrong_size == 1 && counts.last_p == a && counts.last_size == 99);
    CHECK(t.errors == 1 && t.blocks == 0 && t.bytes == 0);
    CHECK(state.live == 0 && state.bad == 0);
    // So does a realloc.
    a = sof_mem_track_alloc(&t, 100);
    memset(a, 5, 100);
    a = sof_mem_track_realloc(&t, a, 64, 200);
    CHECK(a != NULL && filled(a, 100, 5));
    CHECK(counts.wrong_size == 2 && t.bytes == 200);
    CHECK(state.bad == 0);
    // A pointer the tracker didn't give out is reported and left alone.
    static union {
        max_align_t align;
        unsigned char bytes[256];
    } fake;
    void *p = fake.bytes + 128;
    CHECK(sof_mem_track_release(&t, p, 16) == SOF_MEM_NO_MATCH);
    CHECK(counts.unknown == 1 && counts.last_p == p);
    CHECK(sof_mem_track_realloc(&t, p, 16, 32) == NULL);
    CHECK(counts.unknown == 2);
    // So is one inside a tracked block.
    memset(a, 0, 200);
    CHECK(sof_mem_track_release(&t, a + 128, 16) == SOF_MEM_NO_MATCH);
    CHECK(counts.unknown == 3);
    CHECK(t.errors == 5 && t.blocks == 1);
    // Without an error_func the problems are only counted.
    sof_mem_track_t quiet;
    CHECK(sof_mem_track_init(&quiet, NULL, NULL, NULL) == SOF_MEM_OK);
    void *q = sof_mem_track_alloc(&quiet, 10);
    CHECK(sof_mem_track_release(&quiet, p, 16) == SOF_MEM_NO_MATCH);
    CHECK(sof_mem_track_release(&quiet, q, 11) == SOF_MEM_OK);
    CHECK(quiet.errors == 2 && quiet.blocks == 0);
    CHECK(sof_mem_track_free(&quiet) == SOF_MEM_OK);
    CHECK(sof_mem_track_free(&t) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_track_traverse(void) {
    sof_mem_track_t t;
    CHECK(sof_mem_track_init(&t, NULL, NULL, NULL) == SOF_MEM_OK);
    track_counts_t counts = {0};
    CHECK(sof_mem_track_traverse(&t, track_visit, &counts) == 0);
    CHECK(counts.visits == 0);
    void *blocks[5];
    for (int i = 0; i < 5; i++) {
        blocks[i] = sof_mem_track_alloc(&t, 10 * (i + 1));
    }
    CHECK(sof_mem_track_traverse(&t, track_visit, &counts) == 0);
    CHECK(counts.visits == 5 && counts.visited_bytes == 150);
    // Newest first, so the oldest is last.
    CHECK(counts.last_p == blocks[0] && counts.last_size == 10);
    // A non-zero return stops it.
    memset(&counts, 0, sizeof(counts));
    counts.stop_after = 2;
    CHECK(sof_mem_track_traverse(&t, track_visit, &counts) == 42);
    CHECK(counts.visits == 2 && counts.last_p == blocks[3]);
    CHECK(sof_mem_track_traverse(&t, NULL, NULL) == 0);
    CHECK(sof_mem_track_traverse(NULL, track_visit, &counts) == -1);
    // It only reads, so it takes a const tracker.
    const sof_mem_track_t *ct = &t;
    memset(&counts, 0, sizeof(counts));
    CHECK(sof_mem_track_traverse(ct, track_visit, &counts) == 0);
    CHECK(counts.visits == 5 && counts.visited_bytes == 150);
    CHECK(sof_mem_track_free(&t) == SOF_MEM_OK);
}

static void test_track_clear(void) {
    test_alloc_t state = {0};
    sof_alloc_t parent = test_allocator(&state);
    track_counts_t counts = {0};
    sof_mem_track_t t;
    CHECK(sof_mem_track_init(&t, track_error, &counts, &parent) == SOF_MEM_OK);
    for (int i = 0; i < 10; i++) {
        CHECK(sof_mem_track_alloc(&t, 100 + i) != NULL);
    }
    void *p = sof_mem_track_alloc(&t, 8);
    sof_mem_track_release(&t, p, 9);
    CHECK(state.live == 10 && t.errors == 1);
    // Clear frees what is live and starts the counts again, keeping the
    // error_func.
    CHECK(sof_mem_track_clear(&t) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(t.live == NULL && t.blocks == 0 && t.bytes == 0 && t.peak_bytes == 0 && t.peak_blocks == 0);
    CHECK(t.allocations == 0 && t.frees == 0 && t.errors == 0 && t.failures == 0);
    CHECK(t.error_func == track_error && t.ctx == &counts);
    p = sof_mem_track_alloc(&t, 8);
    sof_mem_track_release(&t, p, 9);
    CHECK(counts.wrong_size == 2);
    // Free frees what is still live, too.
    CHECK(sof_mem_track_alloc(&t, 1000) != NULL);
    CHECK(sof_mem_track_free(&t) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    check_track_dead(&t);
}

static void test_track_failures(void) {
    test_alloc_t state = {0};
    sof_alloc_t parent = test_allocator(&state);
    sof_mem_track_t t;
    CHECK(sof_mem_track_init(&t, NULL, NULL, &parent) == SOF_MEM_OK);
    unsigned char *a = sof_mem_track_alloc(&t, 10);
    unsigned char *b = sof_mem_track_alloc(&t, 20);
    unsigned char *c = sof_mem_track_alloc(&t, 30);
    memset(b, 2, 20);
    state.fail_at = state.allocations + 1;
    CHECK(sof_mem_track_alloc(&t, 40) == NULL);
    CHECK(t.failures == 1 && t.blocks == 3 && t.allocations == 3);
    state.fail_at = state.allocations + 1;
    CHECK(sof_mem_track_realloc(&t, b, 20, 2000) == NULL);
    CHECK(t.failures == 2 && t.bytes == 60 && filled(b, 20, 2));
    CHECK(sof_mem_track_alloc(&t, SIZE_MAX) == NULL);
    CHECK(sof_mem_track_realloc(&t, b, 20, SIZE_MAX) == NULL);
    CHECK(t.failures == 4);
    // The list is intact.
    track_counts_t counts = {0};
    CHECK(sof_mem_track_traverse(&t, track_visit, &counts) == 0);
    CHECK(counts.visits == 3 && counts.visited_bytes == 60);
    CHECK(sof_mem_track_release(&t, b, 20) == SOF_MEM_OK);
    CHECK(sof_mem_track_release(&t, a, 10) == SOF_MEM_OK);
    CHECK(sof_mem_track_release(&t, c, 30) == SOF_MEM_OK);
    CHECK(sof_mem_track_free(&t) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_track_random(void) {
    test_alloc_t state = {0};
    sof_alloc_t parent = test_allocator(&state);
    sof_mem_track_t t;
    CHECK(sof_mem_track_init(&t, NULL, NULL, &parent) == SOF_MEM_OK);
    sof_alloc_t alloc = sof_mem_track_allocator(&t);
    slot_t slots[SLOTS] = {{0}};
    int ok;
    random_ops(&alloc, 0x7ac7ac7ac7ac7acULL, slots, &ok);
    CHECK(ok);
    size_t blocks = 0, bytes = 0;
    for (size_t i = 0; i < SLOTS; i++) {
        if (slots[i].p != NULL) {
            blocks++;
            bytes += slots[i].size;
        }
    }
    CHECK(t.blocks == blocks && t.bytes == bytes && t.errors == 0);
    CHECK(state.live == (long)blocks);
    CHECK(sof_mem_track_free(&t) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_track_null(void) {
    CHECK(sof_mem_track_init(NULL, NULL, NULL, NULL) == SOF_MEM_ERROR);
    sof_mem_track_t t;
    test_alloc_t state = {0};
    sof_alloc_t no_free = test_allocator(&state);
    no_free.free_sized = NULL;
    memset(&t, 0xff, sizeof(t));
    CHECK(sof_mem_track_init(&t, NULL, NULL, &no_free) == SOF_MEM_ERROR);
    check_track_dead(&t);
    CHECK(sof_mem_track_alloc(NULL, 8) == NULL);
    CHECK(sof_mem_track_realloc(NULL, NULL, 0, 8) == NULL);
    CHECK(sof_mem_track_release(NULL, NULL, 0) == SOF_MEM_ERROR);
    CHECK(sof_mem_track_clear(NULL) == SOF_MEM_ERROR);
    CHECK(sof_mem_track_free(NULL) == SOF_MEM_ERROR);
    sof_alloc_t alloc = sof_mem_track_allocator(NULL);
    CHECK(sof_alloc_valid(&alloc));
    CHECK(sof_alloc_malloc(&alloc, 8) == NULL);
}

static void test_track_freed(void) {
    sof_mem_track_t t;
    CHECK(sof_mem_track_init(&t, NULL, NULL, NULL) == SOF_MEM_OK);
    CHECK(sof_mem_track_alloc(&t, 100) != NULL);
    CHECK(sof_mem_track_free(&t) == SOF_MEM_OK);
    check_track_dead(&t);
    CHECK(sof_mem_track_init(&t, NULL, NULL, NULL) == SOF_MEM_OK);
    CHECK(sof_mem_track_alloc(&t, 100) != NULL);
    CHECK(sof_mem_track_free(&t) == SOF_MEM_OK);
}

static void test_stacked(void) {
    // A tracker over a slab allocator over an arena in a fixed buffer: no
    // malloc at all, frees of every size in constant time, and a count of
    // what is live. The fixed arena can't reclaim what it gives out, so the
    // slab allocator serves every size used, up to 3256 bytes, itself.
    sof_mem_arena_t arena;
    CHECK(sof_mem_arena_init_buffer(&arena, buffer.bytes, sizeof(buffer.bytes)) == SOF_MEM_OK);
    sof_alloc_t arena_alloc = sof_mem_arena_allocator(&arena);
    sof_mem_slab_t slab;
    CHECK(sof_mem_slab_init(&slab, SOF_MEM_SLAB_LIMIT, 8192, &arena_alloc) == SOF_MEM_OK);
    sof_alloc_t slab_alloc = sof_mem_slab_allocator(&slab);
    sof_mem_track_t t;
    CHECK(sof_mem_track_init(&t, NULL, NULL, &slab_alloc) == SOF_MEM_OK);
    sof_alloc_t alloc = sof_mem_track_allocator(&t);
    slot_t slots[SLOTS] = {{0}};
    int ok;
    random_ops(&alloc, 0x57ac4ed57ac4ed5ULL, slots, &ok);
    CHECK(ok);
    for (size_t i = 0; i < SLOTS; i++) {
        unsigned char *p = slots[i].p;
        CHECK(p == NULL || (p >= buffer.bytes && p + slots[i].size <= buffer.bytes + sizeof(buffer.bytes)));
    }
    CHECK(t.errors == 0 && t.failures == 0);
    for (size_t i = 0; i < SLOTS; i++) {
        sof_alloc_free(&alloc, slots[i].p, slots[i].size);
    }
    CHECK(t.blocks == 0 && t.bytes == 0 && t.errors == 0);
    CHECK(slab_count(&slab) == 0);
    CHECK(sof_mem_track_free(&t) == SOF_MEM_OK);
    CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
    CHECK(sof_mem_arena_free(&arena) == SOF_MEM_OK);
    // The same over an ordinary arena, with the test allocator under it all,
    // and a mark taken back: everything goes back to the parent at the end.
    test_alloc_t state = {0};
    sof_alloc_t parent = test_allocator(&state);
    CHECK(sof_mem_arena_init(&arena, 0, &parent) == SOF_MEM_OK);
    arena_alloc = sof_mem_arena_allocator(&arena);
    sof_mem_arena_mark_t mark = sof_mem_arena_mark(&arena);
    CHECK(sof_mem_slab_init(&slab, 0, 0, &arena_alloc) == SOF_MEM_OK);
    slab_alloc = sof_mem_slab_allocator(&slab);
    memset(slots, 0, sizeof(slots));
    random_ops(&slab_alloc, 0x1357913579135791ULL, slots, &ok);
    CHECK(ok);
    for (size_t i = 0; i < SLOTS; i++) {
        sof_alloc_free(&slab_alloc, slots[i].p, slots[i].size);
    }
    CHECK(sof_mem_slab_free(&slab) == SOF_MEM_OK);
    CHECK(sof_mem_arena_reset(&arena, mark) == SOF_MEM_OK);
    CHECK(arena.large == NULL);
    CHECK(sof_mem_arena_free(&arena) == SOF_MEM_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

int main(void) {
    printf("sof_mem:\n");
    run("arena init", test_arena_init);
    run("arena bad init arguments", test_arena_bad_init);
    run("arena alloc", test_arena_alloc);
    run("arena large allocations", test_arena_large);
    run("arena release", test_arena_release);
    run("arena realloc", test_arena_realloc);
    run("arena clear reuses the blocks", test_arena_clear);
    run("arena marks", test_arena_marks);
    run("arena nested marks", test_arena_nested_marks);
    run("arena in a buffer", test_arena_buffer);
    run("arena bad buffers", test_arena_bad_buffer);
    run("arena as a sof_alloc_t", test_arena_allocator);
    run("arena random allocs, reallocs and frees", test_arena_random);
    run("arena out of memory", test_arena_no_memory);
    run("arena NULL arguments", test_arena_null);
    run("freed arena", test_arena_freed);
    run("pool init", test_pool_init);
    run("pool bad init arguments", test_pool_bad_init);
    run("pool alloc and release", test_pool_alloc);
    run("pool clear reuses the slabs", test_pool_clear);
    run("pool as a sof_alloc_t", test_pool_allocator);
    run("pool random allocs, reallocs and frees", test_pool_random);
    run("pool out of memory", test_pool_no_memory);
    run("pool NULL arguments", test_pool_null);
    run("freed pool", test_pool_freed);
    run("slab init", test_slab_init);
    run("slab bad init arguments", test_slab_bad_init);
    run("slab size classes", test_slab_classes);
    run("slab alloc and release", test_slab_alloc);
    run("slab realloc", test_slab_realloc);
    run("slab clear reuses the slabs", test_slab_clear);
    run("slab random allocs, reallocs and frees", test_slab_random);
    run("slab out of memory", test_slab_no_memory);
    run("slab NULL arguments", test_slab_null);
    run("freed slab", test_slab_freed);
    run("track counts", test_track_counts);
    run("track catches bad frees", test_track_errors);
    run("track traverse", test_track_traverse);
    run("track clear", test_track_clear);
    run("track parent failures", test_track_failures);
    run("track random allocs, reallocs and frees", test_track_random);
    run("track NULL arguments", test_track_null);
    run("freed track", test_track_freed);
    run("tracker over slab over arena", test_stacked);
    return test_report();
}
