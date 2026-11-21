/**
 * @file sof_heap.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A binary-heap priority queue of fixed-size values in c.
 * @details Elements move by the hole method: the element being placed waits
 * in the scratch slot after the last element of the array, the elements in
 * its way move one level into the hole it leaves, and it is copied in once,
 * where it belongs. So each level costs one copy rather than a swap's three,
 * and nothing is allocated to hold it.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_heap.h"

/// @brief The capacity of the array when the first element is pushed onto a
/// heap initialized with a capacity of 0.
#define SOF_HEAP_FIRST_CAPACITY 16

static unsigned char *at(const sof_heap_t *h, size_t i);
static int contains(const sof_heap_t *h, const void *element);
static sof_heap_error_t grow(sof_heap_t *h);
static void remove_all(sof_heap_t *h);
static void sift_down(sof_heap_t *h, size_t i);
static void sift_up(sof_heap_t *h, size_t i);
static void take(sof_heap_t *h, size_t i, void *removed);

sof_heap_error_t sof_heap_clear(sof_heap_t *h) {
    if (h == NULL || !h->ready) {
        return SOF_HEAP_ERROR;
    }
    remove_all(h);
    return SOF_HEAP_OK;
}

sof_heap_error_t sof_heap_free(sof_heap_t *h) {
    if (h == NULL || !h->ready) {
        return SOF_HEAP_ERROR;
    }
    remove_all(h);
    if (h->elements != NULL) {
        sof_alloc_free(&h->alloc, h->elements, (h->capacity + 1) * h->element_size);
    }
    memset(h, 0, sizeof(*h));
    return SOF_HEAP_OK;
}

sof_heap_error_t sof_heap_init(sof_heap_t *h, size_t element_size, size_t capacity, sof_heap_compare_func_t cmp_func, sof_heap_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc) {
    if (h == NULL) {
        return SOF_HEAP_ERROR;
    }
    memset(h, 0, sizeof(*h));
    if (element_size == 0 || cmp_func == NULL || !sof_alloc_valid(alloc)) {
        return SOF_HEAP_ERROR;
    }
    if (alloc != NULL) {
        h->alloc = *alloc;
    }
    if (capacity > 0) {
        // The block holds the scratch slot too.
        if (capacity == SIZE_MAX || capacity + 1 > SIZE_MAX / element_size) {
            memset(h, 0, sizeof(*h));
            return SOF_HEAP_NO_MEM;
        }
        h->elements = sof_alloc_malloc(&h->alloc, (capacity + 1) * element_size);
        if (h->elements == NULL) {
            memset(h, 0, sizeof(*h));
            return SOF_HEAP_NO_MEM;
        }
        h->capacity = capacity;
    }
    h->element_size = element_size;
    h->cmp_func = cmp_func;
    h->del_func = del_func;
    h->ctx = ctx;
    h->ready = 1;
    return SOF_HEAP_OK;
}

const void *sof_heap_next(const sof_heap_t *h, const void *element) {
    if (h == NULL || !h->ready || element == NULL || !contains(h, element)) {
        return NULL;
    }
    const unsigned char *e = (const unsigned char *)element + h->element_size;
    if (e == at(h, h->count)) {
        return NULL;
    }
    return e;
}

const void *sof_heap_peek(const sof_heap_t *h) {
    if (h == NULL || !h->ready || h->count == 0) {
        return NULL;
    }
    return h->elements;
}

sof_heap_error_t sof_heap_pop(sof_heap_t *h, void *element) {
    if (h == NULL || !h->ready) {
        return SOF_HEAP_ERROR;
    }
    if (h->count == 0) {
        return SOF_HEAP_NO_MATCH;
    }
    take(h, 0, element);
    return SOF_HEAP_OK;
}

sof_heap_error_t sof_heap_push(sof_heap_t *h, const void *element) {
    if (h == NULL || !h->ready || element == NULL) {
        return SOF_HEAP_ERROR;
    }
    if (h->count == h->capacity) {
        // The element may be in the array, which growing can move, so it is
        // found again by its position.
        int inside = contains(h, element);
        size_t offset = inside ? (size_t)((const unsigned char *)element - h->elements) : 0;
        sof_heap_error_t rc = grow(h);
        if (rc != SOF_HEAP_OK) {
            return rc;
        }
        if (inside) {
            element = h->elements + offset;
        }
    }
    memcpy(at(h, h->capacity), element, h->element_size);
    h->count++;
    sift_up(h, h->count - 1);
    return SOF_HEAP_OK;
}

sof_heap_error_t sof_heap_remove(sof_heap_t *h, const void *element, void *removed) {
    if (h == NULL || !h->ready) {
        return SOF_HEAP_ERROR;
    }
    if (element == NULL || !contains(h, element)) {
        return SOF_HEAP_NO_MATCH;
    }
    take(h, (size_t)((const unsigned char *)element - h->elements) / h->element_size, removed);
    return SOF_HEAP_OK;
}

sof_heap_error_t sof_heap_replace(sof_heap_t *h, const void *element, void *removed) {
    if (h == NULL || !h->ready || element == NULL) {
        return SOF_HEAP_ERROR;
    }
    if (h->count == 0) {
        return SOF_HEAP_NO_MATCH;
    }
    // Copied first, since it may be the element about to be removed.
    memcpy(at(h, h->capacity), element, h->element_size);
    if (removed != NULL) {
        memcpy(removed, h->elements, h->element_size);
    } else if (h->del_func != NULL) {
        h->del_func(h->elements, h->ctx);
    }
    sift_down(h, 0);
    return SOF_HEAP_OK;
}

int sof_heap_traverse(const sof_heap_t *h, sof_heap_traverse_func_t cb, void *ctx) {
    if (h == NULL || !h->ready) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    for (size_t i = 0; i < h->count; i++) {
        int rc = cb(at(h, i), ctx);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

/// @brief Returns element i of the array; i == capacity is the scratch slot.
static unsigned char *at(const sof_heap_t *h, size_t i) {
    return h->elements + i * h->element_size;
}

/// @brief Returns 1 if element points to the start of one of the heap's
/// elements, or 0. Compared as integers, since element may point anywhere.
static int contains(const sof_heap_t *h, const void *element) {
    if (h->count == 0) {
        return 0;
    }
    uintptr_t p = (uintptr_t)element, base = (uintptr_t)h->elements;
    if (p < base || p - base >= h->count * h->element_size) {
        return 0;
    }
    return (p - base) % h->element_size == 0;
}

/// @brief Doubles the array, or allocates its first SOF_HEAP_FIRST_CAPACITY
/// elements, with the scratch slot after them. Leaves the heap unchanged if
/// it can't.
static sof_heap_error_t grow(sof_heap_t *h) {
    size_t capacity = h->capacity == 0 ? SOF_HEAP_FIRST_CAPACITY : h->capacity * 2;
    if (capacity < h->capacity || capacity == SIZE_MAX || capacity + 1 > SIZE_MAX / h->element_size) {
        return SOF_HEAP_NO_MEM;
    }
    size_t old_size = h->elements == NULL ? 0 : (h->capacity + 1) * h->element_size;
    unsigned char *elements = sof_alloc_realloc(&h->alloc, h->elements, old_size, (capacity + 1) * h->element_size);
    if (elements == NULL) {
        return SOF_HEAP_NO_MEM;
    }
    h->elements = elements;
    h->capacity = capacity;
    return SOF_HEAP_OK;
}

/// @brief Removes every element, from the end of the array back, giving
/// each to the del_func first, and leaves the heap empty with its array.
static void remove_all(sof_heap_t *h) {
    if (h->del_func != NULL) {
        while (h->count > 0) {
            h->count--;
            h->del_func(at(h, h->count), h->ctx);
        }
    }
    h->count = 0;
}

/// @brief Places the element in the scratch slot at index i or below, moving
/// smaller children up into the hole until it is no greater than both of
/// its children.
static void sift_down(sof_heap_t *h, size_t i) {
    const unsigned char *moving = at(h, h->capacity);
    size_t n = h->count;
    // i has a child while 2 * i + 1 < n, which is i < n / 2 without the
    // chance of overflow.
    while (i < n / 2) {
        size_t child = 2 * i + 1;
        if (child + 1 < n && h->cmp_func(at(h, child + 1), at(h, child), h->ctx) < 0) {
            child++;
        }
        if (h->cmp_func(at(h, child), moving, h->ctx) >= 0) {
            break;
        }
        memcpy(at(h, i), at(h, child), h->element_size);
        i = child;
    }
    memcpy(at(h, i), moving, h->element_size);
}

/// @brief Places the element in the scratch slot at index i or above, moving
/// greater parents down into the hole until its parent is no greater.
static void sift_up(sof_heap_t *h, size_t i) {
    const unsigned char *moving = at(h, h->capacity);
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (h->cmp_func(moving, at(h, parent), h->ctx) >= 0) {
            break;
        }
        memcpy(at(h, i), at(h, parent), h->element_size);
        i = parent;
    }
    memcpy(at(h, i), moving, h->element_size);
}

/// @brief Removes element i, copying it to removed or giving it to the
/// del_func, and fills its place with the last element, moved up or down to
/// where it belongs.
static void take(sof_heap_t *h, size_t i, void *removed) {
    if (removed != NULL) {
        memcpy(removed, at(h, i), h->element_size);
    } else if (h->del_func != NULL) {
        h->del_func(at(h, i), h->ctx);
    }
    h->count--;
    if (i == h->count) {
        return;
    }
    memcpy(at(h, h->capacity), at(h, h->count), h->element_size);
    if (i > 0 && h->cmp_func(at(h, h->capacity), at(h, (i - 1) / 2), h->ctx) < 0) {
        sift_up(h, i);
    } else {
        sift_down(h, i);
    }
}
