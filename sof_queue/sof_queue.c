/**
 * @file sof_queue.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A first-in, first-out queue in c.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <string.h>

#include "sof_queue.h"

static void release_spares(sof_queue_t *q);
static void remove_all(sof_queue_t *q);

sof_queue_error_t sof_queue_clear(sof_queue_t *q) {
    if (q == NULL || !q->ready) {
        return SOF_QUEUE_ERROR;
    }
    remove_all(q);
    release_spares(q);
    return SOF_QUEUE_OK;
}

sof_queue_error_t sof_queue_dequeue(sof_queue_t *q, void **data) {
    if (q == NULL || !q->ready) {
        return SOF_QUEUE_ERROR;
    }
    sof_queue_element_t *e = q->head;
    if (e == NULL) {
        return SOF_QUEUE_NO_MATCH;
    }
    q->head = e->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    q->count--;
    if (data != NULL) {
        *data = e->data;
    } else if (q->del_func != NULL) {
        q->del_func(e, q->ctx);
    }
    // Keep the element for the next enqueue rather than freeing it.
    e->next = q->spare;
    q->spare = e;
    q->spare_count++;
    return SOF_QUEUE_OK;
}

sof_queue_error_t sof_queue_enqueue(sof_queue_t *q, void *data) {
    if (q == NULL || !q->ready) {
        return SOF_QUEUE_ERROR;
    }
    sof_queue_element_t *e = q->spare;
    if (e != NULL) {
        q->spare = e->next;
        q->spare_count--;
    } else {
        e = sof_alloc_malloc(&q->alloc, sizeof(sof_queue_element_t));
        if (e == NULL) {
            return SOF_QUEUE_NO_MEM;
        }
    }
    e->next = NULL;
    e->data = data;
    if (q->tail != NULL) {
        q->tail->next = e;
    } else {
        q->head = e;
    }
    q->tail = e;
    q->count++;
    return SOF_QUEUE_OK;
}

sof_queue_error_t sof_queue_free(sof_queue_t *q) {
    if (q == NULL || !q->ready) {
        return SOF_QUEUE_ERROR;
    }
    remove_all(q);
    release_spares(q);
    memset(q, 0, sizeof(*q));
    return SOF_QUEUE_OK;
}

sof_queue_error_t sof_queue_init(sof_queue_t *q, sof_queue_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc) {
    if (q == NULL) {
        return SOF_QUEUE_ERROR;
    }
    memset(q, 0, sizeof(*q));
    if (!sof_alloc_valid(alloc)) {
        return SOF_QUEUE_ERROR;
    }
    if (alloc != NULL) {
        q->alloc = *alloc;
    }
    q->del_func = del_func;
    q->ctx = ctx;
    q->ready = 1;
    return SOF_QUEUE_OK;
}

sof_queue_element_t *sof_queue_next(const sof_queue_t *q, const sof_queue_element_t *element) {
    if (q == NULL || !q->ready || element == NULL) {
        return NULL;
    }
    return element->next;
}

sof_queue_element_t *sof_queue_peek(const sof_queue_t *q) {
    if (q == NULL || !q->ready) {
        return NULL;
    }
    return q->head;
}

sof_queue_error_t sof_queue_shrink(sof_queue_t *q) {
    if (q == NULL || !q->ready) {
        return SOF_QUEUE_ERROR;
    }
    release_spares(q);
    return SOF_QUEUE_OK;
}

int sof_queue_traverse(const sof_queue_t *q, sof_queue_traverse_func_t cb, void *ctx) {
    if (q == NULL || !q->ready) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    for (sof_queue_element_t *e = q->head; e != NULL; e = e->next) {
        int rc = cb(e, ctx);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

/// @brief Frees the spare elements kept by sof_queue_dequeue().
static void release_spares(sof_queue_t *q) {
    sof_queue_element_t *e = q->spare;
    while (e != NULL) {
        sof_queue_element_t *next = e->next;
        sof_alloc_free(&q->alloc, e, sizeof(sof_queue_element_t));
        e = next;
    }
    q->spare = NULL;
    q->spare_count = 0;
}

/// @brief Removes every element, from head to tail, giving each to the
/// del_func first, and leaves the queue empty.
static void remove_all(sof_queue_t *q) {
    sof_queue_element_t *e = q->head;
    while (e != NULL) {
        sof_queue_element_t *next = e->next;
        if (q->del_func != NULL) {
            q->del_func(e, q->ctx);
        }
        sof_alloc_free(&q->alloc, e, sizeof(sof_queue_element_t));
        e = next;
    }
    q->head = q->tail = NULL;
    q->count = 0;
}
