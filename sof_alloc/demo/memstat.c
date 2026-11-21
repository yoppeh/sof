/**
 * @file memstat.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A counting allocator: a sof_alloc_t that passes every call on to a
 * parent and keeps statistics of what passes through.
 * @details See memstat.h.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "memstat.h"

/// @brief The widest bar memstat_report() draws in the histogram.
#define BAR_WIDTH 40

static void count_size(memstat_t *s, size_t size);
static void stat_free_sized(void *p, size_t size, void *ctx);
static void *stat_malloc(size_t size, void *ctx);
static void *stat_realloc(void *p, size_t old_size, size_t new_size, void *ctx);
static void track_peak(memstat_t *s);

/// @brief Counts a malloc or realloc asking for size bytes in the total and
/// the histogram.
static void count_size(memstat_t *s, size_t size) {
    s->total_bytes += size;
    s->histogram[sof_bit_ceil_log2(size)]++;
}

/// @brief The sof_alloc_t's free_sized: checks that p is a live block and
/// size its size, then frees it through the parent with its real size.
static void stat_free_sized(void *p, size_t size, void *ctx) {
    memstat_t *s = ctx;
    if (s == NULL) {
        return;
    }
    sof_hashmap_element_t *e = sof_hashmap_find(&s->live, p);
    if (e == NULL) {
        s->unknown++;
        return;
    }
    size_t real = (size_t)(uintptr_t)e->data;
    if (real != size) {
        s->wrong_sizes++;
    }
    sof_hashmap_remove_element(&s->live, e);
    sof_alloc_free(&s->alloc, p, real);
    s->frees++;
    s->blocks--;
    s->bytes -= real;
}

/// @brief The sof_alloc_t's malloc: allocates through the parent and
/// records the block.
static void *stat_malloc(size_t size, void *ctx) {
    memstat_t *s = ctx;
    if (s == NULL || !s->ready) {
        return NULL;
    }
    void *p = sof_alloc_malloc(&s->alloc, size);
    if (p == NULL) {
        s->failures++;
        return NULL;
    }
    sof_hashmap_element_t *e = sof_hashmap_insert(&s->live, p, NULL);
    if (e == NULL) {
        sof_alloc_free(&s->alloc, p, size);
        s->failures++;
        return NULL;
    }
    e->data = (void *)(uintptr_t)size;
    s->mallocs++;
    s->blocks++;
    s->bytes += size;
    count_size(s, size);
    track_peak(s);
    return p;
}

/// @brief The sof_alloc_t's realloc: checks p and old_size as a free does,
/// then resizes the block through the parent from its real size.
/// @details The block is taken out of the map before the parent is called
/// and put back, at its new address or its old one, after. sof_hashmap
/// promises that an insert after a remove never grows the map, so putting it
/// back never allocates and can't fail.
static void *stat_realloc(void *p, size_t old_size, size_t new_size, void *ctx) {
    memstat_t *s = ctx;
    if (s == NULL || !s->ready) {
        return NULL;
    }
    sof_hashmap_element_t *e = sof_hashmap_find(&s->live, p);
    if (e == NULL) {
        s->unknown++;
        return NULL;
    }
    size_t real = (size_t)(uintptr_t)e->data;
    if (real != old_size) {
        s->wrong_sizes++;
    }
    sof_hashmap_remove_element(&s->live, e);
    void *q = sof_alloc_realloc(&s->alloc, p, real, new_size);
    if (q == NULL) {
        e = sof_hashmap_insert(&s->live, p, NULL);
        e->data = (void *)(uintptr_t)real;
        s->failures++;
        return NULL;
    }
    e = sof_hashmap_insert(&s->live, q, NULL);
    e->data = (void *)(uintptr_t)new_size;
    s->reallocs++;
    s->bytes = s->bytes - real + new_size;
    count_size(s, new_size);
    track_peak(s);
    return q;
}

/// @brief Raises the peaks to the blocks and bytes live now, if they are
/// higher.
static void track_peak(memstat_t *s) {
    if (s->blocks > s->peak_blocks) {
        s->peak_blocks = s->blocks;
    }
    if (s->bytes > s->peak_bytes) {
        s->peak_bytes = s->bytes;
    }
}

sof_alloc_t memstat_allocator(memstat_t *s) {
    sof_alloc_t alloc = {stat_malloc, stat_realloc, NULL, stat_free_sized, s};
    return alloc;
}

memstat_error_t memstat_free(memstat_t *s) {
    if (s == NULL || !s->ready) {
        return MEMSTAT_ERROR;
    }
    size_t position = 0;
    sof_hashmap_element_t *e;
    while ((e = sof_hashmap_next(&s->live, &position)) != NULL) {
        sof_alloc_free(&s->alloc, e->key, (size_t)(uintptr_t)e->data);
    }
    sof_hashmap_free(&s->live);
    memset(s, 0, sizeof(*s));
    return MEMSTAT_OK;
}

memstat_error_t memstat_init(memstat_t *s, const sof_alloc_t *alloc) {
    if (s == NULL) {
        return MEMSTAT_ERROR;
    }
    memset(s, 0, sizeof(*s));
    if (!sof_alloc_valid(alloc)) {
        return MEMSTAT_ERROR;
    }
    if (alloc != NULL) {
        s->alloc = *alloc;
    }
    // No key callbacks: blocks are hashed and compared by address.
    sof_hashmap_error_t err = sof_hashmap_init(&s->live, NULL, NULL, NULL, 0, &s->alloc);
    if (err != SOF_HASHMAP_OK) {
        memset(s, 0, sizeof(*s));
        return err == SOF_HASHMAP_NO_MEM ? MEMSTAT_NO_MEM : MEMSTAT_ERROR;
    }
    s->ready = 1;
    return MEMSTAT_OK;
}

memstat_error_t memstat_report(const memstat_t *s, FILE *stream) {
    if (s == NULL || stream == NULL) {
        return MEMSTAT_ERROR;
    }
    fprintf(stream, "calls     %zu malloc, %zu realloc, %zu free, %zu failed\n", s->mallocs, s->reallocs, s->frees, s->failures);
    fprintf(stream, "asked     %zu bytes in all\n", s->total_bytes);
    fprintf(stream, "peak      %zu bytes in %zu blocks\n", s->peak_bytes, s->peak_blocks);
    fprintf(stream, "live      %zu bytes in %zu blocks\n", s->bytes, s->blocks);
    fprintf(stream, "problems  %zu wrong sizes, %zu unknown blocks\n", s->wrong_sizes, s->unknown);
    size_t most = 0;
    for (size_t k = 0; k < MEMSTAT_BUCKETS; k++) {
        if (s->histogram[k] > most) {
            most = s->histogram[k];
        }
    }
    if (most == 0) {
        return MEMSTAT_OK;
    }
    fprintf(stream, "%21s  %10s\n", "block size", "count");
    for (size_t k = 0; k < MEMSTAT_BUCKETS; k++) {
        if (s->histogram[k] == 0) {
            continue;
        }
        size_t high = k == SOF_BIT_BITS ? SIZE_MAX : (size_t)1 << k;
        size_t low = k == 0 ? 1 : ((size_t)1 << (k - 1)) + 1;
        char range[48];
        if (low == high) {
            snprintf(range, sizeof(range), "%zu", high);
        } else {
            snprintf(range, sizeof(range), "%zu-%zu", low, high);
        }
        // At least one mark, so no bucket that was used looks empty.
        size_t width = s->histogram[k] * BAR_WIDTH / most;
        fprintf(stream, "%21s  %10zu  ", range, s->histogram[k]);
        for (size_t i = 0; i < (width == 0 ? 1 : width); i++) {
            fputc('#', stream);
        }
        fputc('\n', stream);
    }
    return MEMSTAT_OK;
}
