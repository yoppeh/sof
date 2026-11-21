/**
 * @file uniq.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Duplicate line finding in C, for the uniq program.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_hash.h"
#include "uniq.h"

static int key_equal(const void *a, const void *b, void *ctx);
static void key_free(void *key, const sof_alloc_t *alloc, void *ctx);
static size_t key_hash(const void *key, void *ctx);
static uniq_line_t *line_new(const uniq_t *u, const uniq_line_t *from, size_t index);
static void probe(uniq_line_t *key, const char *bytes, size_t length);

/// @brief The hashmap's keys are uniq_line_t, made by line_new() and freed
/// by the map.
/// @details uniq_add() makes the line before inserting it, rather than
/// letting a copy callback make it from the probe, so the map never stores a
/// pointer to the probe, which gcc would otherwise warn of returning.
static const sof_hashmap_key_callbacks_t line_keys = {key_hash, key_equal, NULL, key_free};

/// @brief Compares the bytes of two lines.
static int key_equal(const void *a, const void *b, void *ctx) {
    const uniq_line_t *la = a, *lb = b;
    return la->line.length == lb->line.length && sof_string_cmp(&la->line, &lb->line) == 0;
}

/// @brief Frees a uniq_line_t made by key_copy().
static void key_free(void *key, const sof_alloc_t *alloc, void *ctx) {
    uniq_line_t *line = key;
    sof_string_free(&line->line);
    sof_alloc_free(alloc, line, sizeof(uniq_line_t));
}

/// @brief Hashes the bytes of a line.
static size_t key_hash(const void *key, void *ctx) {
    const uniq_line_t *line = key;
    return (size_t)sof_hash_bytes(line->line.data, line->line.length, 0);
}

/// @brief Makes a uniq_line_t holding a copy of from's bytes, with a count
/// of 0 and the given index.
static uniq_line_t *line_new(const uniq_t *u, const uniq_line_t *from, size_t index) {
    uniq_line_t *line = sof_alloc_malloc(&u->alloc, sizeof(uniq_line_t));
    if (line == NULL) {
        return NULL;
    }
    if (sof_string_dup(&line->line, &from->line, &u->alloc) != SOF_STRING_OK) {
        sof_alloc_free(&u->alloc, line, sizeof(uniq_line_t));
        return NULL;
    }
    line->count = 0;
    line->index = index;
    return line;
}

/// @brief Fills in a uniq_line_t to look a line up with, whose string is a
/// read-only view of the caller's bytes (empty for NULL), only hashed,
/// compared and copied.
static void probe(uniq_line_t *key, const char *bytes, size_t length) {
    memset(key, 0, sizeof(*key));
    key->line = sof_string_view(bytes, length);
}

const uniq_line_t *uniq_add(uniq_t *u, const char *bytes, size_t length, int *created) {
    if (created != NULL) {
        *created = 0;
    }
    if (u == NULL || u->map.elements == NULL) {
        return NULL;
    }
    uniq_line_t key;
    probe(&key, bytes, length);
    sof_hashmap_element_t *e = sof_hashmap_find(&u->map, &key);
    uniq_line_t *line;
    if (e != NULL) {
        line = e->key;
    } else {
        line = line_new(u, &key, u->lines.count);
        if (line == NULL) {
            return NULL;
        }
        e = sof_hashmap_insert(&u->map, line, NULL);
        if (e == NULL) {
            key_free(line, &u->alloc, NULL);
            return NULL;
        }
        if (sof_vector_push(&u->lines, &line) != SOF_VECTOR_OK) {
            // The map frees the line.
            sof_hashmap_remove_element(&u->map, e);
            return NULL;
        }
        if (created != NULL) {
            *created = 1;
        }
    }
    line->count++;
    u->total++;
    return line;
}

uniq_error_t uniq_clear(uniq_t *u) {
    if (u == NULL || u->map.elements == NULL) {
        return UNIQ_ERROR;
    }
    // The vector only points to the lines, which the map frees.
    sof_vector_clear(&u->lines);
    sof_hashmap_clear(&u->map);
    u->total = 0;
    return UNIQ_OK;
}

const uniq_line_t *uniq_find(const uniq_t *u, const char *bytes, size_t length) {
    if (u == NULL || u->map.elements == NULL) {
        return NULL;
    }
    uniq_line_t key;
    probe(&key, bytes, length);
    const sof_hashmap_element_t *e = sof_hashmap_find(&u->map, &key);
    return e == NULL ? NULL : e->key;
}

uniq_error_t uniq_free(uniq_t *u) {
    if (u == NULL) {
        return UNIQ_ERROR;
    }
    // Either may never have been initialized, if uniq_init() failed; zeroed,
    // they are taken as empty.
    sof_hashmap_free(&u->map);
    sof_vector_free(&u->lines);
    memset(u, 0, sizeof(*u));
    return UNIQ_OK;
}

uniq_error_t uniq_init(uniq_t *u, size_t expected, const sof_alloc_t *alloc) {
    if (u == NULL) {
        return UNIQ_ERROR;
    }
    memset(u, 0, sizeof(*u));
    if (!sof_alloc_valid(alloc)) {
        return UNIQ_ERROR;
    }
    if (alloc != NULL) {
        u->alloc = *alloc;
    }
    if (sof_vector_init(&u->lines, sizeof(uniq_line_t *), expected, NULL, NULL, &u->alloc) != SOF_VECTOR_OK) {
        uniq_free(u);
        return UNIQ_NO_MEM;
    }
    if (sof_hashmap_init(&u->map, &line_keys, NULL, NULL, expected, &u->alloc) != SOF_HASHMAP_OK) {
        uniq_free(u);
        return UNIQ_NO_MEM;
    }
    return UNIQ_OK;
}

const uniq_line_t *uniq_next(const uniq_t *u, size_t *position) {
    if (u == NULL || u->map.elements == NULL || position == NULL || *position >= u->lines.count) {
        return NULL;
    }
    const uniq_line_t *const *line = sof_vector_at(&u->lines, (*position)++);
    return *line;
}

int uniq_traverse(const uniq_t *u, uniq_traverse_func_t cb, void *ctx) {
    if (u == NULL || u->map.elements == NULL) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    size_t position = 0;
    const uniq_line_t *line;
    while ((line = uniq_next(u, &position)) != NULL) {
        int rc = cb(line, ctx);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}
