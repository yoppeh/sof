/**
 * @file sof_alloc.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Replaceable memory allocation in C.
 * @details sof_alloc_malloc() and sof_alloc_free() are static inline in
 * sof_alloc.h.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sof_alloc.h"

static int is_standard(const sof_alloc_t *alloc);

/// @brief Returns 1 if alloc means the standard functions.
static int is_standard(const sof_alloc_t *alloc) {
    return alloc == NULL || alloc->malloc == NULL;
}

void *sof_alloc_calloc(const sof_alloc_t *alloc, size_t count, size_t size) {
    if (count == 0 || size == 0 || count > SIZE_MAX / size) {
        return NULL;
    }
    if (is_standard(alloc)) {
        return calloc(count, size);
    }
    void *p = alloc->malloc(count * size, alloc->ctx);
    if (p != NULL) {
        memset(p, 0, count * size);
    }
    return p;
}

void *sof_alloc_realloc(const sof_alloc_t *alloc, void *p, size_t old_size, size_t new_size) {
    if (new_size == 0) {
        return NULL;
    }
    if (p == NULL) {
        return sof_alloc_malloc(alloc, new_size);
    }
    if (is_standard(alloc)) {
        return realloc(p, new_size);
    }
    if (alloc->realloc != NULL) {
        return alloc->realloc(p, old_size, new_size, alloc->ctx);
    }
    void *q = alloc->malloc(new_size, alloc->ctx);
    if (q == NULL) {
        return NULL;
    }
    memcpy(q, p, old_size < new_size ? old_size : new_size);
    sof_alloc_free(alloc, p, old_size);
    return q;
}

char *sof_alloc_strndup(const sof_alloc_t *alloc, const char *s, size_t length) {
    if (s == NULL) {
        return NULL;
    }
    char *copy = sof_alloc_malloc(alloc, length + 1);
    if (copy != NULL) {
        memcpy(copy, s, length);
        copy[length] = 0;
    }
    return copy;
}

int sof_alloc_valid(const sof_alloc_t *alloc) {
    return is_standard(alloc) || alloc->free != NULL || alloc->free_sized != NULL;
}
