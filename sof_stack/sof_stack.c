/**
 * @file sof_stack.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A last-in, first-out stack of fixed-size values in c.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_stack.h"

/// @brief The capacity of the array when the first element is pushed onto a
/// stack initialized with a capacity of 0.
#define SOF_STACK_FIRST_CAPACITY 16

static int contains(const sof_stack_t *s, const void *element);
static sof_stack_error_t grow(sof_stack_t *s);
static void remove_to(sof_stack_t *s, size_t count);

sof_stack_error_t sof_stack_clear(sof_stack_t *s) {
    if (s == NULL || !s->ready) {
        return SOF_STACK_ERROR;
    }
    remove_to(s, 0);
    return SOF_STACK_OK;
}

sof_stack_error_t sof_stack_free(sof_stack_t *s) {
    if (s == NULL || !s->ready) {
        return SOF_STACK_ERROR;
    }
    remove_to(s, 0);
    sof_alloc_free(&s->alloc, s->elements, s->capacity * s->element_size);
    memset(s, 0, sizeof(*s));
    return SOF_STACK_OK;
}

sof_stack_error_t sof_stack_init(sof_stack_t *s, size_t element_size, size_t capacity, sof_stack_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc) {
    if (s == NULL) {
        return SOF_STACK_ERROR;
    }
    memset(s, 0, sizeof(*s));
    if (element_size == 0 || !sof_alloc_valid(alloc)) {
        return SOF_STACK_ERROR;
    }
    if (alloc != NULL) {
        s->alloc = *alloc;
    }
    if (capacity > 0) {
        if (capacity > SIZE_MAX / element_size) {
            memset(s, 0, sizeof(*s));
            return SOF_STACK_NO_MEM;
        }
        s->elements = sof_alloc_malloc(&s->alloc, capacity * element_size);
        if (s->elements == NULL) {
            memset(s, 0, sizeof(*s));
            return SOF_STACK_NO_MEM;
        }
        s->capacity = capacity;
    }
    s->element_size = element_size;
    s->del_func = del_func;
    s->ctx = ctx;
    s->ready = 1;
    return SOF_STACK_OK;
}

void *sof_stack_next(const sof_stack_t *s, const void *element) {
    if (s == NULL || !s->ready || element == NULL || !contains(s, element)) {
        return NULL;
    }
    const unsigned char *e = element;
    if (e == s->elements) {
        return NULL;
    }
    return (void *)(e - s->element_size);
}

void *sof_stack_peek(const sof_stack_t *s) {
    if (s == NULL || !s->ready || s->count == 0) {
        return NULL;
    }
    return s->elements + (s->count - 1) * s->element_size;
}

sof_stack_error_t sof_stack_pop(sof_stack_t *s, void *element) {
    if (s == NULL || !s->ready) {
        return SOF_STACK_ERROR;
    }
    if (s->count == 0) {
        return SOF_STACK_NO_MATCH;
    }
    s->count--;
    unsigned char *top = s->elements + s->count * s->element_size;
    if (element != NULL) {
        memmove(element, top, s->element_size);
    } else if (s->del_func != NULL) {
        s->del_func(top, s->ctx);
    }
    return SOF_STACK_OK;
}

sof_stack_error_t sof_stack_push(sof_stack_t *s, const void *element) {
    if (s == NULL || !s->ready || element == NULL) {
        return SOF_STACK_ERROR;
    }
    if (s->count == s->capacity) {
        // The element may be in the array, which growing can move, so it is
        // found again by its position.
        int inside = contains(s, element);
        size_t offset = inside ? (size_t)((const unsigned char *)element - s->elements) : 0;
        sof_stack_error_t rc = grow(s);
        if (rc != SOF_STACK_OK) {
            return rc;
        }
        if (inside) {
            element = s->elements + offset;
        }
    }
    memcpy(s->elements + s->count * s->element_size, element, s->element_size);
    s->count++;
    return SOF_STACK_OK;
}

int sof_stack_traverse(const sof_stack_t *s, sof_stack_traverse_func_t cb, void *ctx) {
    if (s == NULL || !s->ready) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    for (size_t i = s->count; i > 0; i--) {
        int rc = cb(s->elements + (i - 1) * s->element_size, ctx);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

sof_stack_error_t sof_stack_truncate(sof_stack_t *s, size_t count) {
    if (s == NULL || !s->ready) {
        return SOF_STACK_ERROR;
    }
    remove_to(s, count);
    return SOF_STACK_OK;
}

/// @brief Returns 1 if element points to the start of one of the stack's
/// elements, or 0. Compared as integers, since element may point anywhere.
static int contains(const sof_stack_t *s, const void *element) {
    if (s->count == 0) {
        return 0;
    }
    uintptr_t p = (uintptr_t)element, base = (uintptr_t)s->elements;
    if (p < base || p - base >= s->count * s->element_size) {
        return 0;
    }
    return (p - base) % s->element_size == 0;
}

/// @brief Doubles the array, or allocates its first SOF_STACK_FIRST_CAPACITY
/// elements. Leaves the stack unchanged if it can't.
static sof_stack_error_t grow(sof_stack_t *s) {
    size_t capacity = s->capacity == 0 ? SOF_STACK_FIRST_CAPACITY : s->capacity * 2;
    if (capacity < s->capacity || capacity > SIZE_MAX / s->element_size) {
        return SOF_STACK_NO_MEM;
    }
    unsigned char *elements = sof_alloc_realloc(&s->alloc, s->elements, s->capacity * s->element_size, capacity * s->element_size);
    if (elements == NULL) {
        return SOF_STACK_NO_MEM;
    }
    s->elements = elements;
    s->capacity = capacity;
    return SOF_STACK_OK;
}

/// @brief Removes elements from the top until count are left, giving each to
/// the del_func first, and keeps the array. Removes nothing if there are
/// count or fewer.
static void remove_to(sof_stack_t *s, size_t count) {
    if (count >= s->count) {
        return;
    }
    if (s->del_func != NULL) {
        while (s->count > count) {
            s->count--;
            s->del_func(s->elements + s->count * s->element_size, s->ctx);
        }
    }
    s->count = count;
}
