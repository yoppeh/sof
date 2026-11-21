/**
 * @file sof_mem_internal.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Helpers the sof_mem allocators share; not part of the public API.
 * @details Everything here is static inline, so it adds no symbols to the
 * library. Only the library's own .c files include it.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_MEM_INTERNAL_H
#define SOF_MEM_INTERNAL_H

#include <stdint.h>

#include "sof_mem.h"

/// @brief Every block handed out is aligned to this.
#define SOF_MEM_ALIGN (_Alignof(max_align_t))

/// @brief The size of a block's header, rounded up so the usable bytes after
/// it stay aligned.
#define SOF_MEM_HEADER ((sizeof(sof_mem_block_t) + SOF_MEM_ALIGN - 1) & ~(SOF_MEM_ALIGN - 1))

/// @brief Returns the usable bytes of block b.
static inline unsigned char *sof_mem_data(sof_mem_block_t *b) {
    return (unsigned char *)b + SOF_MEM_HEADER;
}

/// @brief Returns the block whose usable bytes start at p.
static inline sof_mem_block_t *sof_mem_header(void *p) {
    return (sof_mem_block_t *)((unsigned char *)p - SOF_MEM_HEADER);
}

/// @brief Rounds n up to a multiple of SOF_MEM_ALIGN, or returns 0 if that
/// overflows.
static inline size_t sof_mem_round_up(size_t n) {
    if (n > SIZE_MAX - (SOF_MEM_ALIGN - 1)) {
        return 0;
    }
    return (n + SOF_MEM_ALIGN - 1) & ~(SOF_MEM_ALIGN - 1);
}

/// @brief Returns the block to use after current in list (the first if
/// current is NULL), allocating one of size usable bytes from alloc and
/// adding it to the end of the list if there is none, or NULL if that fails.
/// The caller has checked that the header and size together don't overflow.
static inline sof_mem_block_t *sof_mem_next_block(const sof_alloc_t *alloc, sof_mem_block_t **list,
                                                  sof_mem_block_t *current, size_t size, size_t *allocated) {
    sof_mem_block_t *next = current != NULL ? current->next : *list;
    if (next != NULL) {
        return next;
    }
    next = sof_alloc_malloc(alloc, SOF_MEM_HEADER + size);
    if (next == NULL) {
        return NULL;
    }
    next->next = NULL;
    next->prev = NULL;
    next->size = size;
    next->serial = 0;
    // With no next, current is the last block, or the list is empty.
    if (current != NULL) {
        current->next = next;
    } else {
        *list = next;
    }
    *allocated += SOF_MEM_HEADER + size;
    return next;
}

/// @brief sof_mem_pool_alloc() for a pool known to be ready, with the reuse
/// of a freed object inline; the slab's classes are such pools.
static inline void *sof_mem_pool_take(sof_mem_pool_t *pool) {
    void *p = pool->free_list;
    if (p == NULL) {
        return sof_mem_pool_alloc(pool);
    }
    pool->free_list = *(void **)p;
    pool->count++;
    return p;
}

/// @brief sof_mem_pool_release() of an object p, not NULL, to a pool known
/// to be ready.
static inline void sof_mem_pool_put(sof_mem_pool_t *pool, void *p) {
    *(void **)p = pool->free_list;
    pool->free_list = p;
    pool->count--;
}

#endif // SOF_MEM_INTERNAL_H
