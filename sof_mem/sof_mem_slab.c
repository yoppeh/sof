/**
 * @file sof_mem_slab.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Small blocks of any size, from a pool per size class.
 * @details The classes are every multiple of the alignment up to 128 bytes,
 * then four between each power of two and the next: for a size in
 * (2^p, 2^(p+1)], the classes are 2^p plus one to four quarters of 2^p.
 * class_index() and class_size() convert between a size and its class,
 * with a small table of bit lengths, and the pools are in an array indexed
 * by class.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_mem.h"
#include "sof_mem_internal.h"

/// @brief The largest size whose classes are every multiple of the
/// alignment; above it, there are four classes per power of two.
#define LINEAR_LIMIT 128

/// @brief log2(LINEAR_LIMIT).
#define LINEAR_LOG 7

/// @brief The bit length of each number below SOF_MEM_SLAB_LIMIT /
/// LINEAR_LIMIT, for class_index(): a size above LINEAR_LIMIT, less one and
/// shifted down LINEAR_LOG bits, is one of these numbers.
static const unsigned char bit_length[SOF_MEM_SLAB_LIMIT / LINEAR_LIMIT] = {
    0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
};

static void *cb_malloc(size_t size, void *ctx);
static void *cb_realloc(void *p, size_t old_size, size_t new_size, void *ctx);
static void cb_free_sized(void *p, size_t size, void *ctx);
static size_t class_index(size_t size);
static size_t class_size(size_t index);

void *sof_mem_slab_alloc(sof_mem_slab_t *slab, size_t size) {
    if (slab == NULL || !slab->ready || size == 0) {
        return NULL;
    }
    if (size > slab->max_size) {
        return sof_alloc_malloc(&slab->alloc, size);
    }
    return sof_mem_pool_take(&slab->pools[class_index(size)]);
}

sof_alloc_t sof_mem_slab_allocator(sof_mem_slab_t *slab) {
    sof_alloc_t alloc = {cb_malloc, cb_realloc, NULL, cb_free_sized, slab};
    return alloc;
}

sof_mem_error_t sof_mem_slab_clear(sof_mem_slab_t *slab) {
    if (slab == NULL || !slab->ready) {
        return SOF_MEM_ERROR;
    }
    for (size_t i = 0; i < slab->classes; i++) {
        sof_mem_pool_clear(&slab->pools[i]);
    }
    return SOF_MEM_OK;
}

sof_mem_error_t sof_mem_slab_free(sof_mem_slab_t *slab) {
    if (slab == NULL || !slab->ready) {
        return SOF_MEM_ERROR;
    }
    for (size_t i = 0; i < slab->classes; i++) {
        sof_mem_pool_free(&slab->pools[i]);
    }
    sof_alloc_free(&slab->alloc, slab->pools, slab->classes * sizeof(sof_mem_pool_t));
    memset(slab, 0, sizeof(*slab));
    return SOF_MEM_OK;
}

sof_mem_error_t sof_mem_slab_init(sof_mem_slab_t *slab, size_t max_size, size_t slab_size, const sof_alloc_t *alloc) {
    if (slab == NULL) {
        return SOF_MEM_ERROR;
    }
    memset(slab, 0, sizeof(*slab));
    if (max_size > SOF_MEM_SLAB_LIMIT || !sof_alloc_valid(alloc)) {
        return SOF_MEM_ERROR;
    }
    if (max_size == 0) {
        max_size = SOF_MEM_SLAB_MAX_SIZE;
    }
    if (slab_size == 0) {
        slab_size = SOF_MEM_POOL_SLAB_SIZE;
    }
    if (alloc != NULL) {
        slab->alloc = *alloc;
    }
    size_t classes = class_index(max_size) + 1;
    slab->pools = sof_alloc_calloc(&slab->alloc, classes, sizeof(sof_mem_pool_t));
    if (slab->pools == NULL) {
        memset(slab, 0, sizeof(*slab));
        return SOF_MEM_NO_MEM;
    }
    for (size_t i = 0; i < classes; i++) {
        // A pool fails only if its slab would be too large to allocate, and
        // then the larger classes' would be too.
        size_t size = class_size(i);
        size_t per_slab = slab_size > SOF_MEM_HEADER + size ? (slab_size - SOF_MEM_HEADER) / size : 1;
        if (sof_mem_pool_init(&slab->pools[i], size, per_slab, &slab->alloc) != SOF_MEM_OK) {
            sof_alloc_free(&slab->alloc, slab->pools, classes * sizeof(sof_mem_pool_t));
            memset(slab, 0, sizeof(*slab));
            return SOF_MEM_ERROR;
        }
    }
    slab->classes = classes;
    slab->max_size = class_size(classes - 1);
    slab->ready = 1;
    return SOF_MEM_OK;
}

void *sof_mem_slab_realloc(sof_mem_slab_t *slab, void *p, size_t old_size, size_t new_size) {
    if (slab == NULL || !slab->ready || new_size == 0) {
        return NULL;
    }
    if (p == NULL) {
        return sof_mem_slab_alloc(slab, new_size);
    }
    int old_small = old_size <= slab->max_size;
    int new_small = new_size <= slab->max_size;
    if (old_small && new_small && class_index(old_size) == class_index(new_size)) {
        return p;
    }
    if (!old_small && !new_small) {
        return sof_alloc_realloc(&slab->alloc, p, old_size, new_size);
    }
    void *q = sof_mem_slab_alloc(slab, new_size);
    if (q == NULL) {
        return NULL;
    }
    memcpy(q, p, old_size < new_size ? old_size : new_size);
    sof_mem_slab_release(slab, p, old_size);
    return q;
}

sof_mem_error_t sof_mem_slab_release(sof_mem_slab_t *slab, void *p, size_t size) {
    if (slab == NULL || !slab->ready) {
        return SOF_MEM_ERROR;
    }
    if (p == NULL) {
        return SOF_MEM_OK;
    }
    if (size > slab->max_size) {
        sof_alloc_free(&slab->alloc, p, size);
    } else {
        sof_mem_pool_put(&slab->pools[class_index(size)], p);
    }
    return SOF_MEM_OK;
}

/// @brief sof_mem_slab_alloc() as a sof_alloc_t's malloc.
static void *cb_malloc(size_t size, void *ctx) {
    return sof_mem_slab_alloc(ctx, size);
}

/// @brief sof_mem_slab_realloc() as a sof_alloc_t's realloc.
static void *cb_realloc(void *p, size_t old_size, size_t new_size, void *ctx) {
    return sof_mem_slab_realloc(ctx, p, old_size, new_size);
}

/// @brief sof_mem_slab_release() as a sof_alloc_t's free_sized.
static void cb_free_sized(void *p, size_t size, void *ctx) {
    sof_mem_slab_release(ctx, p, size);
}

/// @brief Returns the index of the smallest class that holds size bytes.
/// size is from 1 to SOF_MEM_SLAB_LIMIT; 0 is treated as 1.
static size_t class_index(size_t size) {
    if (size <= LINEAR_LIMIT) {
        return size == 0 ? 0 : (size - 1) / SOF_MEM_ALIGN;
    }
    // size is in (2^p, 2^(p+1)], so (size - 1) >> LINEAR_LOG has p -
    // LINEAR_LOG + 1 bits.
    size_t p = LINEAR_LOG - 1 + bit_length[(size - 1) >> LINEAR_LOG];
    size_t quarter = (size_t)1 << (p - 2);
    return LINEAR_LIMIT / SOF_MEM_ALIGN + (p - LINEAR_LOG) * 4 + (size - 1 - ((size_t)1 << p)) / quarter;
}

/// @brief Returns the size of class index.
static size_t class_size(size_t index) {
    size_t linear = LINEAR_LIMIT / SOF_MEM_ALIGN;
    if (index < linear) {
        return (index + 1) * SOF_MEM_ALIGN;
    }
    index -= linear;
    size_t p = LINEAR_LOG + index / 4;
    return ((size_t)1 << p) + (index % 4 + 1) * ((size_t)1 << (p - 2));
}
