/**
 * @file sof_mem_track.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief An allocator that tracks what passes through it to its parent.
 * @details Each block has a hidden header before it, holding its size, a
 * magic number while it is live, and links in a doubly linked list of live
 * blocks, newest first. A free or realloc checks the magic number and the
 * size before doing anything, and the magic number is cleared before the
 * block goes back to the parent.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_mem.h"
#include "sof_mem_internal.h"

/// @brief Marks a header of a live block.
#define MAGIC ((size_t)0x7ac4b10c5afe1e55ULL)

/// @brief The hidden header of a tracked block.
struct sof_mem_track_header_t {
    struct sof_mem_track_header_t *prev;
    struct sof_mem_track_header_t *next;
    size_t size;
    size_t magic;
};

typedef struct sof_mem_track_header_t header_t;

/// @brief The size of a header, rounded up so the block after it stays
/// aligned.
#define TRACK_HEADER ((sizeof(header_t) + SOF_MEM_ALIGN - 1) & ~(SOF_MEM_ALIGN - 1))

static void *cb_malloc(size_t size, void *ctx);
static void *cb_realloc(void *p, size_t old_size, size_t new_size, void *ctx);
static void cb_free_sized(void *p, size_t size, void *ctx);
static header_t *check(sof_mem_track_t *t, void *p, size_t size);
static void *data(header_t *h);
static void free_all(sof_mem_track_t *t);
static void link_header(sof_mem_track_t *t, header_t *h);
static void note_peak(sof_mem_track_t *t);
static void unlink_header(sof_mem_track_t *t, header_t *h);

void *sof_mem_track_alloc(sof_mem_track_t *t, size_t size) {
    if (t == NULL || !t->ready || size == 0) {
        return NULL;
    }
    header_t *h = size <= SIZE_MAX - TRACK_HEADER ? sof_alloc_malloc(&t->alloc, TRACK_HEADER + size) : NULL;
    if (h == NULL) {
        t->failures++;
        return NULL;
    }
    h->size = size;
    h->magic = MAGIC;
    link_header(t, h);
    t->blocks++;
    t->bytes += size;
    t->allocations++;
    note_peak(t);
    return data(h);
}

sof_alloc_t sof_mem_track_allocator(sof_mem_track_t *t) {
    sof_alloc_t alloc = {cb_malloc, cb_realloc, NULL, cb_free_sized, t};
    return alloc;
}

sof_mem_error_t sof_mem_track_clear(sof_mem_track_t *t) {
    if (t == NULL || !t->ready) {
        return SOF_MEM_ERROR;
    }
    free_all(t);
    t->peak_bytes = 0;
    t->peak_blocks = 0;
    t->allocations = 0;
    t->reallocations = 0;
    t->frees = 0;
    t->failures = 0;
    t->errors = 0;
    return SOF_MEM_OK;
}

sof_mem_error_t sof_mem_track_free(sof_mem_track_t *t) {
    if (t == NULL || !t->ready) {
        return SOF_MEM_ERROR;
    }
    free_all(t);
    memset(t, 0, sizeof(*t));
    return SOF_MEM_OK;
}

sof_mem_error_t sof_mem_track_init(sof_mem_track_t *t, sof_mem_track_error_func_t error_func, void *ctx, const sof_alloc_t *alloc) {
    if (t == NULL) {
        return SOF_MEM_ERROR;
    }
    memset(t, 0, sizeof(*t));
    if (!sof_alloc_valid(alloc)) {
        return SOF_MEM_ERROR;
    }
    if (alloc != NULL) {
        t->alloc = *alloc;
    }
    t->error_func = error_func;
    t->ctx = ctx;
    t->ready = 1;
    return SOF_MEM_OK;
}

void *sof_mem_track_realloc(sof_mem_track_t *t, void *p, size_t old_size, size_t new_size) {
    if (t == NULL || !t->ready || new_size == 0) {
        return NULL;
    }
    if (p == NULL) {
        return sof_mem_track_alloc(t, new_size);
    }
    header_t *h = check(t, p, old_size);
    if (h == NULL) {
        return NULL;
    }
    if (new_size > SIZE_MAX - TRACK_HEADER) {
        t->failures++;
        return NULL;
    }
    old_size = h->size;
    header_t *moved = sof_alloc_realloc(&t->alloc, h, TRACK_HEADER + old_size, TRACK_HEADER + new_size);
    if (moved == NULL) {
        t->failures++;
        return NULL;
    }
    // The header moved with the block: point its neighbours at it again.
    moved->size = new_size;
    if (moved->prev != NULL) {
        moved->prev->next = moved;
    } else {
        t->live = moved;
    }
    if (moved->next != NULL) {
        moved->next->prev = moved;
    }
    t->bytes = t->bytes - old_size + new_size;
    t->reallocations++;
    note_peak(t);
    return data(moved);
}

sof_mem_error_t sof_mem_track_release(sof_mem_track_t *t, void *p, size_t size) {
    if (t == NULL || !t->ready) {
        return SOF_MEM_ERROR;
    }
    if (p == NULL) {
        return SOF_MEM_OK;
    }
    header_t *h = check(t, p, size);
    if (h == NULL) {
        return SOF_MEM_NO_MATCH;
    }
    unlink_header(t, h);
    t->blocks--;
    t->bytes -= h->size;
    t->frees++;
    h->magic = 0;
    sof_alloc_free(&t->alloc, h, TRACK_HEADER + h->size);
    return SOF_MEM_OK;
}

int sof_mem_track_traverse(const sof_mem_track_t *t, sof_mem_track_traverse_func_t cb, void *ctx) {
    if (t == NULL || !t->ready) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    for (header_t *h = t->live; h != NULL; h = h->next) {
        int rc = cb(data(h), h->size, ctx);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

/// @brief sof_mem_track_alloc() as a sof_alloc_t's malloc.
static void *cb_malloc(size_t size, void *ctx) {
    return sof_mem_track_alloc(ctx, size);
}

/// @brief sof_mem_track_realloc() as a sof_alloc_t's realloc.
static void *cb_realloc(void *p, size_t old_size, size_t new_size, void *ctx) {
    return sof_mem_track_realloc(ctx, p, old_size, new_size);
}

/// @brief sof_mem_track_release() as a sof_alloc_t's free_sized.
static void cb_free_sized(void *p, size_t size, void *ctx) {
    sof_mem_track_release(ctx, p, size);
}

/// @brief Returns the header of p if it is a live block, reporting a wrong
/// size, or reports it and returns NULL if it isn't.
static header_t *check(sof_mem_track_t *t, void *p, size_t size) {
    header_t *h = (header_t *)((unsigned char *)p - TRACK_HEADER);
    if (h->magic != MAGIC) {
        t->errors++;
        if (t->error_func != NULL) {
            t->error_func(p, size, SOF_MEM_TRACK_UNKNOWN_BLOCK, t->ctx);
        }
        return NULL;
    }
    if (h->size != size) {
        t->errors++;
        if (t->error_func != NULL) {
            t->error_func(p, size, SOF_MEM_TRACK_WRONG_SIZE, t->ctx);
        }
    }
    return h;
}

/// @brief Returns the block after header h.
static void *data(header_t *h) {
    return (unsigned char *)h + TRACK_HEADER;
}

/// @brief Frees every live block, without counting the frees.
static void free_all(sof_mem_track_t *t) {
    while (t->live != NULL) {
        header_t *h = t->live;
        t->live = h->next;
        h->magic = 0;
        sof_alloc_free(&t->alloc, h, TRACK_HEADER + h->size);
    }
    t->blocks = 0;
    t->bytes = 0;
}

/// @brief Puts h at the head of the live list.
static void link_header(sof_mem_track_t *t, header_t *h) {
    h->prev = NULL;
    h->next = t->live;
    if (t->live != NULL) {
        t->live->prev = h;
    }
    t->live = h;
}

/// @brief Updates the peaks from the present counts.
static void note_peak(sof_mem_track_t *t) {
    if (t->bytes > t->peak_bytes) {
        t->peak_bytes = t->bytes;
    }
    if (t->blocks > t->peak_blocks) {
        t->peak_blocks = t->blocks;
    }
}

/// @brief Takes h out of the live list.
static void unlink_header(sof_mem_track_t *t, header_t *h) {
    if (h->prev != NULL) {
        h->prev->next = h->next;
    } else {
        t->live = h->next;
    }
    if (h->next != NULL) {
        h->next->prev = h->prev;
    }
}
