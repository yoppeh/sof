/**
 * @file sof_vector.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A growable array of fixed-size values in c.
 * @details The elements are kept first to last in one block of capacity *
 * element_size bytes. Adding to a full array doubles it, so a run of pushes
 * copies each element a constant number of times on average. Sorting is
 * done in place by sof_sort.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_sort.h"
#include "sof_vector.h"

/// @brief The capacity of the array when the first element is added to a
/// vector initialized with a capacity of 0.
#define SOF_VECTOR_FIRST_CAPACITY 16

static unsigned char *at(const sof_vector_t *v, size_t i);
static void drop(sof_vector_t *v, size_t from);
static sof_vector_error_t grow(sof_vector_t *v, size_t needed);
static int locate(const sof_vector_t *v, const void *element, size_t *offset);
static sof_vector_error_t set_capacity(sof_vector_t *v, size_t capacity);
static void take(sof_vector_t *v, size_t i, void *removed);

void *sof_vector_at(const sof_vector_t *v, size_t index) {
    if (v == NULL || !v->ready || index >= v->count) {
        return NULL;
    }
    return at(v, index);
}

sof_vector_error_t sof_vector_clear(sof_vector_t *v) {
    if (v == NULL || !v->ready) {
        return SOF_VECTOR_ERROR;
    }
    drop(v, 0);
    return SOF_VECTOR_OK;
}

void *sof_vector_find(const sof_vector_t *v, const void *key, sof_vector_compare_func_t cmp_func, void *ctx, size_t *index) {
    if (v == NULL || !v->ready || key == NULL || cmp_func == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < v->count; i++) {
        if (cmp_func(key, at(v, i), ctx) == 0) {
            if (index != NULL) {
                *index = i;
            }
            return at(v, i);
        }
    }
    return NULL;
}

sof_vector_error_t sof_vector_free(sof_vector_t *v) {
    if (v == NULL || !v->ready) {
        return SOF_VECTOR_ERROR;
    }
    drop(v, 0);
    if (v->elements != NULL) {
        sof_alloc_free(&v->alloc, v->elements, v->capacity * v->element_size);
    }
    memset(v, 0, sizeof(*v));
    return SOF_VECTOR_OK;
}

sof_vector_error_t sof_vector_init(sof_vector_t *v, size_t element_size, size_t capacity, sof_vector_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc) {
    if (v == NULL) {
        return SOF_VECTOR_ERROR;
    }
    memset(v, 0, sizeof(*v));
    if (element_size == 0 || !sof_alloc_valid(alloc)) {
        return SOF_VECTOR_ERROR;
    }
    if (alloc != NULL) {
        v->alloc = *alloc;
    }
    v->element_size = element_size;
    if (capacity > 0 && set_capacity(v, capacity) != SOF_VECTOR_OK) {
        memset(v, 0, sizeof(*v));
        return SOF_VECTOR_NO_MEM;
    }
    v->del_func = del_func;
    v->ctx = ctx;
    v->ready = 1;
    return SOF_VECTOR_OK;
}

sof_vector_error_t sof_vector_insert(sof_vector_t *v, size_t index, const void *element) {
    if (v == NULL || !v->ready || element == NULL || index > v->count) {
        return SOF_VECTOR_ERROR;
    }
    // The element may be in the array, which growing can move and the
    // insert shifts, so it is found again by its offset.
    size_t offset;
    int inside = locate(v, element, &offset);
    if (v->count == v->capacity) {
        sof_vector_error_t rc = grow(v, v->count + 1);
        if (rc != SOF_VECTOR_OK) {
            return rc;
        }
    }
    size_t size = v->element_size;
    unsigned char *place = at(v, index);
    memmove(place + size, place, (v->count - index) * size);
    if (inside) {
        element = v->elements + offset + (offset >= index * size ? size : 0);
    }
    memmove(place, element, size);
    v->count++;
    return SOF_VECTOR_OK;
}

void *sof_vector_next(const sof_vector_t *v, const void *element) {
    size_t offset;
    if (v == NULL || !v->ready || element == NULL || !locate(v, element, &offset) || offset % v->element_size != 0) {
        return NULL;
    }
    size_t i = offset / v->element_size + 1;
    return i < v->count ? at(v, i) : NULL;
}

sof_vector_error_t sof_vector_pop(sof_vector_t *v, void *element) {
    if (v == NULL || !v->ready) {
        return SOF_VECTOR_ERROR;
    }
    if (v->count == 0) {
        return SOF_VECTOR_NO_MATCH;
    }
    take(v, v->count - 1, element);
    v->count--;
    return SOF_VECTOR_OK;
}

sof_vector_error_t sof_vector_push(sof_vector_t *v, const void *element) {
    if (v == NULL || !v->ready || element == NULL) {
        return SOF_VECTOR_ERROR;
    }
    if (v->count < v->capacity) {
        // With room, nothing moves: the slot past the last element is free,
        // and an element of the vector can't overlap it, so it is copied
        // straight in.
        memcpy(at(v, v->count), element, v->element_size);
        v->count++;
        return SOF_VECTOR_OK;
    }
    return sof_vector_insert(v, v->count, element);
}

sof_vector_error_t sof_vector_remove(sof_vector_t *v, size_t index, void *removed) {
    if (v == NULL || !v->ready) {
        return SOF_VECTOR_ERROR;
    }
    if (index >= v->count) {
        return SOF_VECTOR_NO_MATCH;
    }
    take(v, index, removed);
    v->count--;
    memmove(at(v, index), at(v, index + 1), (v->count - index) * v->element_size);
    return SOF_VECTOR_OK;
}

sof_vector_error_t sof_vector_reserve(sof_vector_t *v, size_t capacity) {
    if (v == NULL || !v->ready) {
        return SOF_VECTOR_ERROR;
    }
    if (capacity <= v->capacity) {
        return SOF_VECTOR_OK;
    }
    return set_capacity(v, capacity);
}

sof_vector_error_t sof_vector_resize(sof_vector_t *v, size_t count) {
    if (v == NULL || !v->ready) {
        return SOF_VECTOR_ERROR;
    }
    if (count <= v->count) {
        drop(v, count);
        return SOF_VECTOR_OK;
    }
    if (count > v->capacity) {
        sof_vector_error_t rc = grow(v, count);
        if (rc != SOF_VECTOR_OK) {
            return rc;
        }
    }
    memset(at(v, v->count), 0, (count - v->count) * v->element_size);
    v->count = count;
    return SOF_VECTOR_OK;
}

void *sof_vector_search(const sof_vector_t *v, const void *key, sof_vector_compare_func_t cmp_func, void *ctx, size_t *index) {
    if (v == NULL || !v->ready || key == NULL || cmp_func == NULL) {
        return NULL;
    }
    // Finds the first element not less than key.
    size_t low = 0, high = v->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (cmp_func(key, at(v, middle), ctx) > 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (index != NULL) {
        *index = low;
    }
    if (low < v->count && cmp_func(key, at(v, low), ctx) == 0) {
        return at(v, low);
    }
    return NULL;
}

sof_vector_error_t sof_vector_shrink(sof_vector_t *v) {
    if (v == NULL || !v->ready) {
        return SOF_VECTOR_ERROR;
    }
    if (v->count == v->capacity) {
        return SOF_VECTOR_OK;
    }
    if (v->count == 0) {
        sof_alloc_free(&v->alloc, v->elements, v->capacity * v->element_size);
        v->elements = NULL;
        v->capacity = 0;
        return SOF_VECTOR_OK;
    }
    return set_capacity(v, v->count);
}

sof_vector_error_t sof_vector_sort(sof_vector_t *v, sof_vector_compare_func_t cmp_func, void *ctx) {
    if (v == NULL || !v->ready || cmp_func == NULL) {
        return SOF_VECTOR_ERROR;
    }
    if (sof_sort_quick(v->elements, v->count, v->element_size, cmp_func, ctx) != SOF_SORT_OK) {
        return SOF_VECTOR_ERROR;
    }
    return SOF_VECTOR_OK;
}

sof_vector_error_t sof_vector_sort_stable(sof_vector_t *v, sof_vector_compare_func_t cmp_func, void *ctx) {
    if (v == NULL || !v->ready || cmp_func == NULL) {
        return SOF_VECTOR_ERROR;
    }
    switch (sof_sort_merge(v->elements, v->count, v->element_size, cmp_func, ctx, &v->alloc)) {
    case SOF_SORT_OK:
        return SOF_VECTOR_OK;
    case SOF_SORT_NO_MEM:
        return SOF_VECTOR_NO_MEM;
    default:
        return SOF_VECTOR_ERROR;
    }
}

sof_vector_error_t sof_vector_swap_remove(sof_vector_t *v, size_t index, void *removed) {
    if (v == NULL || !v->ready) {
        return SOF_VECTOR_ERROR;
    }
    if (index >= v->count) {
        return SOF_VECTOR_NO_MATCH;
    }
    take(v, index, removed);
    v->count--;
    if (index != v->count) {
        memcpy(at(v, index), at(v, v->count), v->element_size);
    }
    return SOF_VECTOR_OK;
}

int sof_vector_traverse(const sof_vector_t *v, sof_vector_traverse_func_t cb, void *ctx) {
    if (v == NULL || !v->ready) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    for (size_t i = 0; i < v->count; i++) {
        int rc = cb(at(v, i), ctx);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

/// @brief Returns element i of the array.
static unsigned char *at(const sof_vector_t *v, size_t i) {
    return v->elements + i * v->element_size;
}

/// @brief Removes the elements from index from on, giving each to the
/// del_func first, from first to last. The array is kept.
static void drop(sof_vector_t *v, size_t from) {
    if (v->del_func != NULL) {
        for (size_t i = from; i < v->count; i++) {
            v->del_func(at(v, i), v->ctx);
        }
    }
    v->count = from;
}

/// @brief Makes room for at least needed elements: twice the capacity, the
/// first SOF_VECTOR_FIRST_CAPACITY, or needed, whichever is most. Leaves the
/// vector unchanged if it can't.
static sof_vector_error_t grow(sof_vector_t *v, size_t needed) {
    if (needed == 0) {
        // count + 1 overflowed.
        return SOF_VECTOR_NO_MEM;
    }
    size_t capacity = v->capacity == 0 ? SOF_VECTOR_FIRST_CAPACITY : v->capacity * 2;
    if (capacity < v->capacity || capacity > SIZE_MAX / v->element_size) {
        // Doubling overflows, but needed may still fit.
        capacity = needed;
    }
    if (capacity < needed) {
        capacity = needed;
    }
    return set_capacity(v, capacity);
}

/// @brief Returns 1, and sets offset to where element is from the start of
/// the array, if element points into one of the vector's elements, or 0.
/// Compared as integers, since element may point anywhere.
static int locate(const sof_vector_t *v, const void *element, size_t *offset) {
    if (v->count == 0) {
        return 0;
    }
    uintptr_t p = (uintptr_t)element, base = (uintptr_t)v->elements;
    if (p < base || p - base >= v->count * v->element_size) {
        return 0;
    }
    *offset = (size_t)(p - base);
    return 1;
}

/// @brief Reallocates the array to exactly capacity elements, which must not
/// be 0 or fewer than count. Leaves the vector unchanged if it can't.
static sof_vector_error_t set_capacity(sof_vector_t *v, size_t capacity) {
    if (capacity > SIZE_MAX / v->element_size) {
        return SOF_VECTOR_NO_MEM;
    }
    size_t old_size = v->elements == NULL ? 0 : v->capacity * v->element_size;
    unsigned char *elements = sof_alloc_realloc(&v->alloc, v->elements, old_size, capacity * v->element_size);
    if (elements == NULL) {
        return SOF_VECTOR_NO_MEM;
    }
    v->elements = elements;
    v->capacity = capacity;
    return SOF_VECTOR_OK;
}

/// @brief Hands over element i before it is removed: copies it to removed,
/// or gives it to the del_func if removed is NULL.
static void take(sof_vector_t *v, size_t i, void *removed) {
    if (removed != NULL) {
        memcpy(removed, at(v, i), v->element_size);
    } else if (v->del_func != NULL) {
        v->del_func(at(v, i), v->ctx);
    }
}
