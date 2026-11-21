/**
 * @file sof_ring.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A fixed-capacity circular buffer of fixed-size values in c.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_ring.h"

static size_t position(const sof_ring_t *r, const void *element);
static void remove_all(sof_ring_t *r);
static unsigned char *slot(const sof_ring_t *r, size_t index);

void *sof_ring_at(const sof_ring_t *r, size_t index) {
    if (r == NULL || !r->ready || index >= r->count) {
        return NULL;
    }
    return slot(r, index);
}

sof_ring_error_t sof_ring_clear(sof_ring_t *r) {
    if (r == NULL || !r->ready) {
        return SOF_RING_ERROR;
    }
    remove_all(r);
    return SOF_RING_OK;
}

sof_ring_error_t sof_ring_free(sof_ring_t *r) {
    if (r == NULL || !r->ready) {
        return SOF_RING_ERROR;
    }
    remove_all(r);
    sof_alloc_free(&r->alloc, r->elements, r->capacity * r->element_size);
    memset(r, 0, sizeof(*r));
    return SOF_RING_OK;
}

sof_ring_error_t sof_ring_init(sof_ring_t *r, size_t element_size, size_t capacity, sof_ring_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc) {
    if (r == NULL) {
        return SOF_RING_ERROR;
    }
    memset(r, 0, sizeof(*r));
    if (element_size == 0 || capacity == 0 || !sof_alloc_valid(alloc)) {
        return SOF_RING_ERROR;
    }
    if (capacity > SIZE_MAX / element_size) {
        return SOF_RING_NO_MEM;
    }
    if (alloc != NULL) {
        r->alloc = *alloc;
    }
    r->elements = sof_alloc_malloc(&r->alloc, capacity * element_size);
    if (r->elements == NULL) {
        memset(r, 0, sizeof(*r));
        return SOF_RING_NO_MEM;
    }
    r->element_size = element_size;
    r->capacity = capacity;
    r->del_func = del_func;
    r->ctx = ctx;
    r->ready = 1;
    return SOF_RING_OK;
}

void *sof_ring_next(const sof_ring_t *r, const void *element) {
    if (r == NULL || !r->ready || element == NULL) {
        return NULL;
    }
    size_t index = position(r, element);
    if (index >= r->count || index + 1 == r->count) {
        return NULL;
    }
    return slot(r, index + 1);
}

void *sof_ring_peek(const sof_ring_t *r) {
    if (r == NULL || !r->ready || r->count == 0) {
        return NULL;
    }
    return r->elements + r->head * r->element_size;
}

sof_ring_error_t sof_ring_pop(sof_ring_t *r, void *element) {
    if (r == NULL || !r->ready) {
        return SOF_RING_ERROR;
    }
    if (r->count == 0) {
        return SOF_RING_NO_MATCH;
    }
    unsigned char *oldest = r->elements + r->head * r->element_size;
    if (element != NULL) {
        memmove(element, oldest, r->element_size);
    } else if (r->del_func != NULL) {
        r->del_func(oldest, r->ctx);
    }
    r->head = r->head + 1 == r->capacity ? 0 : r->head + 1;
    r->count--;
    return SOF_RING_OK;
}

sof_ring_error_t sof_ring_push(sof_ring_t *r, const void *element) {
    if (r == NULL || !r->ready || element == NULL) {
        return SOF_RING_ERROR;
    }
    if (r->count == r->capacity) {
        return SOF_RING_FULL;
    }
    // The slot is free, so it can't be the element being copied, if that is
    // in the ring.
    memcpy(slot(r, r->count), element, r->element_size);
    r->count++;
    return SOF_RING_OK;
}

sof_ring_error_t sof_ring_push_overwrite(sof_ring_t *r, const void *element) {
    if (r == NULL || !r->ready || element == NULL) {
        return SOF_RING_ERROR;
    }
    if (r->count < r->capacity) {
        return sof_ring_push(r, element);
    }
    // Full: the oldest slot becomes the newest. If element is the oldest
    // element itself, it is kept as it is, rather than deleted and then
    // copied from.
    unsigned char *oldest = r->elements + r->head * r->element_size;
    if ((const unsigned char *)element != oldest) {
        if (r->del_func != NULL) {
            r->del_func(oldest, r->ctx);
        }
        memmove(oldest, element, r->element_size);
    }
    r->head = r->head + 1 == r->capacity ? 0 : r->head + 1;
    return SOF_RING_OK;
}

int sof_ring_traverse(const sof_ring_t *r, sof_ring_traverse_func_t cb, void *ctx) {
    if (r == NULL || !r->ready) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    for (size_t i = 0; i < r->count; i++) {
        int rc = cb(slot(r, i), ctx);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

/// @brief Returns the position, counting from the oldest, of the element
/// element points to the start of, or SIZE_MAX if it doesn't point to the
/// start of a slot. A position of count or more is a free slot. Compared as
/// integers, since element may point anywhere.
static size_t position(const sof_ring_t *r, const void *element) {
    uintptr_t p = (uintptr_t)element, base = (uintptr_t)r->elements;
    if (p < base || p - base >= r->capacity * r->element_size || (p - base) % r->element_size != 0) {
        return SIZE_MAX;
    }
    size_t index = (p - base) / r->element_size;
    return index >= r->head ? index - r->head : index + (r->capacity - r->head);
}

/// @brief Removes every element, from oldest to newest, giving each to the
/// del_func first, and leaves the ring empty with its array.
static void remove_all(sof_ring_t *r) {
    if (r->del_func != NULL) {
        for (size_t i = 0; i < r->count; i++) {
            r->del_func(slot(r, i), r->ctx);
        }
    }
    r->count = 0;
    r->head = 0;
}

/// @brief Returns the slot index places after the oldest, wrapping around
/// the end of the array. index must be less than capacity.
static unsigned char *slot(const sof_ring_t *r, size_t index) {
    size_t to_end = r->capacity - r->head;
    size_t i = index < to_end ? r->head + index : index - to_end;
    return r->elements + i * r->element_size;
}
