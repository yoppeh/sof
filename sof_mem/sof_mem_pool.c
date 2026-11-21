/**
 * @file sof_mem_pool.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A pool of objects of one size.
 * @details Slabs are kept in a list in the order they were first used, and
 * sof_mem_next_block() walks it, so after a clear the same slabs are used
 * again before anything new is asked of the parent. Released objects are
 * kept on a free list threaded through the objects themselves, so a slot is
 * at least a pointer.
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

void *sof_mem_pool_alloc(sof_mem_pool_t *pool) {
    if (pool == NULL || !pool->ready) {
        return NULL;
    }
    void *p;
    if (pool->free_list != NULL) {
        p = pool->free_list;
        pool->free_list = *(void **)p;
    } else {
        if (pool->current == NULL || pool->used == pool->per_slab) {
            sof_mem_block_t *b = sof_mem_next_block(&pool->alloc, &pool->slabs, pool->current, pool->slot_size * pool->per_slab, &pool->allocated);
            if (b == NULL) {
                return NULL;
            }
            pool->current = b;
            pool->used = 0;
        }
        p = sof_mem_data(pool->current) + pool->used * pool->slot_size;
        pool->used++;
    }
    pool->count++;
    return p;
}

sof_alloc_t sof_mem_pool_allocator(sof_mem_pool_t *pool) {
    sof_alloc_t alloc = {cb_malloc, cb_realloc, NULL, cb_free_sized, pool};
    return alloc;
}

sof_mem_error_t sof_mem_pool_clear(sof_mem_pool_t *pool) {
    if (pool == NULL || !pool->ready) {
        return SOF_MEM_ERROR;
    }
    pool->current = NULL;
    pool->free_list = NULL;
    pool->used = 0;
    pool->count = 0;
    return SOF_MEM_OK;
}

sof_mem_error_t sof_mem_pool_free(sof_mem_pool_t *pool) {
    if (pool == NULL || !pool->ready) {
        return SOF_MEM_ERROR;
    }
    while (pool->slabs != NULL) {
        sof_mem_block_t *next = pool->slabs->next;
        sof_alloc_free(&pool->alloc, pool->slabs, SOF_MEM_HEADER + pool->slabs->size);
        pool->slabs = next;
    }
    memset(pool, 0, sizeof(*pool));
    return SOF_MEM_OK;
}

sof_mem_error_t sof_mem_pool_init(sof_mem_pool_t *pool, size_t object_size, size_t per_slab, const sof_alloc_t *alloc) {
    if (pool == NULL) {
        return SOF_MEM_ERROR;
    }
    memset(pool, 0, sizeof(*pool));
    if (object_size == 0 || !sof_alloc_valid(alloc)) {
        return SOF_MEM_ERROR;
    }
    // Each object holds the free list's pointer while it is released.
    size_t slot_size = sof_mem_round_up(object_size < sizeof(void *) ? sizeof(void *) : object_size);
    if (slot_size == 0) {
        return SOF_MEM_ERROR;
    }
    if (per_slab == 0) {
        per_slab = SOF_MEM_POOL_SLAB_SIZE > SOF_MEM_HEADER + slot_size ? (SOF_MEM_POOL_SLAB_SIZE - SOF_MEM_HEADER) / slot_size : 1;
    }
    if (per_slab > (SIZE_MAX - SOF_MEM_HEADER) / slot_size) {
        return SOF_MEM_ERROR;
    }
    if (alloc != NULL) {
        pool->alloc = *alloc;
    }
    pool->object_size = object_size;
    pool->slot_size = slot_size;
    pool->per_slab = per_slab;
    pool->ready = 1;
    return SOF_MEM_OK;
}

sof_mem_error_t sof_mem_pool_release(sof_mem_pool_t *pool, void *p) {
    if (pool == NULL || !pool->ready) {
        return SOF_MEM_ERROR;
    }
    if (p == NULL) {
        return SOF_MEM_OK;
    }
    *(void **)p = pool->free_list;
    pool->free_list = p;
    pool->count--;
    return SOF_MEM_OK;
}

/// @brief A sof_alloc_t's malloc for a pool: an object for sizes up to
/// object_size, and the parent for the rest.
static void *cb_malloc(size_t size, void *ctx) {
    sof_mem_pool_t *pool = ctx;
    if (pool == NULL || !pool->ready) {
        return NULL;
    }
    if (size <= pool->object_size) {
        return sof_mem_pool_alloc(pool);
    }
    return sof_alloc_malloc(&pool->alloc, size);
}

/// @brief A sof_alloc_t's realloc for a pool, moving the block between the
/// pool and the parent when its size crosses object_size.
static void *cb_realloc(void *p, size_t old_size, size_t new_size, void *ctx) {
    sof_mem_pool_t *pool = ctx;
    if (pool == NULL || !pool->ready) {
        return NULL;
    }
    int old_small = old_size <= pool->object_size;
    int new_small = new_size <= pool->object_size;
    if (old_small && new_small) {
        return p;
    }
    if (!old_small && !new_small) {
        return sof_alloc_realloc(&pool->alloc, p, old_size, new_size);
    }
    void *q = cb_malloc(new_size, pool);
    if (q == NULL) {
        return NULL;
    }
    memcpy(q, p, old_size < new_size ? old_size : new_size);
    cb_free_sized(p, old_size, pool);
    return q;
}

/// @brief A sof_alloc_t's free_sized for a pool, telling objects from the
/// parent's blocks by size.
static void cb_free_sized(void *p, size_t size, void *ctx) {
    sof_mem_pool_t *pool = ctx;
    if (pool == NULL || !pool->ready) {
        return;
    }
    if (size <= pool->object_size) {
        sof_mem_pool_release(pool, p);
    } else {
        sof_alloc_free(&pool->alloc, p, size);
    }
}
