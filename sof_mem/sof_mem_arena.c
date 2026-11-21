/**
 * @file sof_mem_arena.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A bump allocator, with marks and a form that works in a buffer of
 * the caller's.
 * @details The standard blocks are kept in a list in the order they were
 * first used, and sof_mem_next_block() walks it, so after a clear or reset
 * the same blocks are used again in the same order before anything new is
 * asked of the parent. Large blocks are in a doubly linked list of their
 * own, newest first, so one can be unlinked without a search, and each has a
 * serial, so a reset frees exactly those allocated since its mark, from the
 * head of the list. An allocation is large exactly when its size is more
 * than a quarter of the block size, so release and realloc know which kind
 * of block they have from the size alone; realloc moves a block between the
 * kinds when its size crosses that line, to keep it so. A fixed arena has no
 * large blocks.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_mem.h"
#include "sof_mem_internal.h"

static void *cb_malloc(size_t size, void *ctx);
static void *cb_realloc(void *p, size_t old_size, size_t new_size, void *ctx);
static void cb_free_sized(void *p, size_t size, void *ctx);
static int is_large(const sof_mem_arena_t *a, size_t size);
static void *large_alloc(sof_mem_arena_t *a, size_t size);
static void large_free(sof_mem_arena_t *a, sof_mem_block_t *b);
static void *large_realloc(sof_mem_arena_t *a, void *p, size_t new_size);

void *sof_mem_arena_alloc(sof_mem_arena_t *a, size_t size) {
    if (a == NULL || !a->ready || size == 0) {
        return NULL;
    }
    if (is_large(a, size)) {
        return large_alloc(a, size);
    }
    size_t rounded = sof_mem_round_up(size);
    if (rounded == 0) {
        return NULL;
    }
    if (a->current == NULL || a->current->size - a->offset < rounded) {
        sof_mem_block_t *b;
        if (a->fixed) {
            // The buffer is the only block.
            b = a->current == NULL && rounded <= a->blocks->size ? a->blocks : NULL;
        } else {
            b = sof_mem_next_block(&a->alloc, &a->blocks, a->current, a->block_size, &a->allocated);
        }
        if (b == NULL) {
            return NULL;
        }
        a->current = b;
        a->offset = 0;
    }
    a->last = sof_mem_data(a->current) + a->offset;
    a->offset += rounded;
    return a->last;
}

sof_alloc_t sof_mem_arena_allocator(sof_mem_arena_t *a) {
    sof_alloc_t alloc = {cb_malloc, cb_realloc, NULL, cb_free_sized, a};
    return alloc;
}

sof_mem_error_t sof_mem_arena_clear(sof_mem_arena_t *a) {
    if (a == NULL || !a->ready) {
        return SOF_MEM_ERROR;
    }
    while (a->large != NULL) {
        large_free(a, a->large);
    }
    a->current = NULL;
    a->offset = 0;
    a->last = NULL;
    return SOF_MEM_OK;
}

sof_mem_error_t sof_mem_arena_free(sof_mem_arena_t *a) {
    if (sof_mem_arena_clear(a) != SOF_MEM_OK) {
        return SOF_MEM_ERROR;
    }
    while (!a->fixed && a->blocks != NULL) {
        sof_mem_block_t *next = a->blocks->next;
        sof_alloc_free(&a->alloc, a->blocks, SOF_MEM_HEADER + a->blocks->size);
        a->blocks = next;
    }
    memset(a, 0, sizeof(*a));
    return SOF_MEM_OK;
}

sof_mem_error_t sof_mem_arena_init(sof_mem_arena_t *a, size_t block_size, const sof_alloc_t *alloc) {
    if (a == NULL) {
        return SOF_MEM_ERROR;
    }
    memset(a, 0, sizeof(*a));
    if (!sof_alloc_valid(alloc)) {
        return SOF_MEM_ERROR;
    }
    if (block_size == 0) {
        block_size = SOF_MEM_ARENA_BLOCK_SIZE;
    }
    block_size = sof_mem_round_up(block_size);
    if (block_size == 0 || block_size > SIZE_MAX - SOF_MEM_HEADER) {
        return SOF_MEM_ERROR;
    }
    if (alloc != NULL) {
        a->alloc = *alloc;
    }
    a->block_size = block_size;
    a->ready = 1;
    return SOF_MEM_OK;
}

sof_mem_error_t sof_mem_arena_init_buffer(sof_mem_arena_t *a, void *buffer, size_t size) {
    if (a == NULL) {
        return SOF_MEM_ERROR;
    }
    memset(a, 0, sizeof(*a));
    if (buffer == NULL) {
        return SOF_MEM_ERROR;
    }
    // Skip to the first aligned address, then the header, and keep whole
    // aligned units after it.
    size_t skip = (SOF_MEM_ALIGN - (uintptr_t)buffer % SOF_MEM_ALIGN) % SOF_MEM_ALIGN;
    if (size < skip + SOF_MEM_HEADER + SOF_MEM_ALIGN) {
        return SOF_MEM_ERROR;
    }
    sof_mem_block_t *b = (sof_mem_block_t *)((unsigned char *)buffer + skip);
    b->next = NULL;
    b->prev = NULL;
    b->size = (size - skip - SOF_MEM_HEADER) & ~(SOF_MEM_ALIGN - 1);
    b->serial = 0;
    a->blocks = b;
    a->block_size = b->size;
    a->fixed = 1;
    a->ready = 1;
    return SOF_MEM_OK;
}

sof_mem_arena_mark_t sof_mem_arena_mark(sof_mem_arena_t *a) {
    sof_mem_arena_mark_t mark = {NULL, 0, 0};
    if (a == NULL || !a->ready) {
        return mark;
    }
    mark.block = a->current;
    mark.offset = a->offset;
    mark.serial = a->serial;
    // What was allocated before the mark can't grow past it, or be taken
    // back below it.
    a->last = NULL;
    return mark;
}

void *sof_mem_arena_realloc(sof_mem_arena_t *a, void *p, size_t old_size, size_t new_size) {
    if (a == NULL || !a->ready || new_size == 0) {
        return NULL;
    }
    if (p == NULL) {
        return sof_mem_arena_alloc(a, new_size);
    }
    int old_large = is_large(a, old_size);
    int new_large = is_large(a, new_size);
    if (old_large && new_large) {
        return large_realloc(a, p, new_size);
    }
    if (!old_large && !new_large) {
        size_t rounded = sof_mem_round_up(new_size);
        if (rounded == 0) {
            return NULL;
        }
        if (p == a->last) {
            size_t start = (size_t)(a->last - sof_mem_data(a->current));
            if (a->current->size - start >= rounded) {
                a->offset = start + rounded;
                return p;
            }
        } else if (rounded <= sof_mem_round_up(old_size)) {
            return p;
        }
    }
    void *q = sof_mem_arena_alloc(a, new_size);
    if (q == NULL) {
        return NULL;
    }
    memcpy(q, p, old_size < new_size ? old_size : new_size);
    sof_mem_arena_release(a, p, old_size);
    return q;
}

sof_mem_error_t sof_mem_arena_release(sof_mem_arena_t *a, void *p, size_t size) {
    if (a == NULL || !a->ready) {
        return SOF_MEM_ERROR;
    }
    if (p == NULL) {
        return SOF_MEM_OK;
    }
    if (is_large(a, size)) {
        large_free(a, sof_mem_header(p));
    } else if (p == a->last) {
        a->offset = (size_t)(a->last - sof_mem_data(a->current));
        a->last = NULL;
    }
    return SOF_MEM_OK;
}

sof_mem_error_t sof_mem_arena_reset(sof_mem_arena_t *a, sof_mem_arena_mark_t mark) {
    if (a == NULL || !a->ready) {
        return SOF_MEM_ERROR;
    }
    if (mark.serial > a->serial || (mark.block == NULL && mark.offset != 0) || (mark.block != NULL && mark.offset > mark.block->size)) {
        return SOF_MEM_ERROR;
    }
    while (a->large != NULL && a->large->serial >= mark.serial) {
        large_free(a, a->large);
    }
    a->current = mark.block;
    a->offset = mark.offset;
    a->last = NULL;
    return SOF_MEM_OK;
}

/// @brief sof_mem_arena_alloc() as a sof_alloc_t's malloc.
static void *cb_malloc(size_t size, void *ctx) {
    return sof_mem_arena_alloc(ctx, size);
}

/// @brief sof_mem_arena_realloc() as a sof_alloc_t's realloc.
static void *cb_realloc(void *p, size_t old_size, size_t new_size, void *ctx) {
    return sof_mem_arena_realloc(ctx, p, old_size, new_size);
}

/// @brief sof_mem_arena_release() as a sof_alloc_t's free_sized.
static void cb_free_sized(void *p, size_t size, void *ctx) {
    sof_mem_arena_release(ctx, p, size);
}

/// @brief Returns 1 if an allocation of size bytes gets a block of its own.
static int is_large(const sof_mem_arena_t *a, size_t size) {
    return !a->fixed && size > a->block_size / 4;
}

/// @brief Allocates a large block of size usable bytes and puts it at the
/// head of the large list.
static void *large_alloc(sof_mem_arena_t *a, size_t size) {
    if (size > SIZE_MAX - SOF_MEM_HEADER) {
        return NULL;
    }
    sof_mem_block_t *b = sof_alloc_malloc(&a->alloc, SOF_MEM_HEADER + size);
    if (b == NULL) {
        return NULL;
    }
    b->size = size;
    b->serial = a->serial++;
    b->prev = NULL;
    b->next = a->large;
    if (a->large != NULL) {
        a->large->prev = b;
    }
    a->large = b;
    a->allocated += SOF_MEM_HEADER + size;
    return sof_mem_data(b);
}

/// @brief Unlinks large block b and gives it back to the parent.
static void large_free(sof_mem_arena_t *a, sof_mem_block_t *b) {
    if (b->prev != NULL) {
        b->prev->next = b->next;
    } else {
        a->large = b->next;
    }
    if (b->next != NULL) {
        b->next->prev = b->prev;
    }
    a->allocated -= SOF_MEM_HEADER + b->size;
    sof_alloc_free(&a->alloc, b, SOF_MEM_HEADER + b->size);
}

/// @brief Resizes large block p through the parent, and links the block in
/// again where it moved to. It keeps its serial, so a reset treats it as
/// allocated when it first was.
static void *large_realloc(sof_mem_arena_t *a, void *p, size_t new_size) {
    if (new_size > SIZE_MAX - SOF_MEM_HEADER) {
        return NULL;
    }
    sof_mem_block_t *b = sof_mem_header(p);
    size_t old_size = b->size;
    b = sof_alloc_realloc(&a->alloc, b, SOF_MEM_HEADER + old_size, SOF_MEM_HEADER + new_size);
    if (b == NULL) {
        return NULL;
    }
    b->size = new_size;
    if (b->prev != NULL) {
        b->prev->next = b;
    } else {
        a->large = b;
    }
    if (b->next != NULL) {
        b->next->prev = b;
    }
    a->allocated = a->allocated - old_size + new_size;
    return sof_mem_data(b);
}
