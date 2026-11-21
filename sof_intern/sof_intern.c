/**
 * @file sof_intern.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief String interning in C.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_hash.h"
#include "sof_hashmap.h"
#include "sof_intern.h"

/// @brief The size of an ordinary block of strings, header included. A string
/// too long for one gets a block of its own.
#define BLOCK_SIZE ((size_t)64 * 1024)

/// @brief What every entry, and so every block header, is aligned to.
#define ALIGN (_Alignof(sof_intern_entry_t))

/// @brief size rounded up to a multiple of ALIGN.
#define ALIGN_UP(size) (((size) + ALIGN - 1) / ALIGN * ALIGN)

/// @brief The space a block's header takes, before its first entry.
#define HEADER_SIZE ALIGN_UP(sizeof(sof_intern_block_t))

static int entry_equal(const void *a, const void *b, void *ctx);
static size_t entry_hash(const void *key, void *ctx);
static void free_blocks(sof_intern_t *pool);
static const char *lookup(const sof_intern_t *pool, const void *data, size_t length, size_t hash);
static sof_intern_entry_t *store(sof_intern_t *pool, const sof_intern_entry_t *probe);

/// @brief The key callbacks of the pool's hashmap, whose keys are entries,
/// equal if their bytes are, and hashed by the hash they carry. The pool
/// keeps the entries, so the map doesn't copy or free them.
static const sof_hashmap_key_callbacks_t entry_keys = {entry_hash, entry_equal, NULL, NULL};

/// @brief Compares two entries by their bytes.
static int entry_equal(const void *a, const void *b, void *ctx) {
    const sof_intern_entry_t *ea = a, *eb = b;
    return ea->length == eb->length && memcmp(ea->data, eb->data, ea->length) == 0;
}

/// @brief Returns the hash an entry carries, worked out once for its bytes.
static size_t entry_hash(const void *key, void *ctx) {
    return (size_t)((const sof_intern_entry_t *)key)->hash;
}

/// @brief Frees every block of strings, leaving the pool with none.
static void free_blocks(sof_intern_t *pool) {
    sof_intern_block_t *block = pool->blocks;
    while (block != NULL) {
        sof_intern_block_t *next = block->next;
        sof_alloc_free(&pool->alloc, block, block->size);
        block = next;
    }
    pool->blocks = NULL;
    pool->used = 0;
}

/// @brief Returns the interned string of length bytes at data, whose full
/// hash in the pool's map is hash, or NULL if there isn't one. The pool is
/// initialized.
/// @details The entries with that hash are compared here rather than by the
/// map's equal callback. Comparing through the callback means building an
/// entry for the string on the stack and reading its fields back inside the
/// call, which on an Apple M4 made finding an existing string 40% slower
/// (about 39 ns against 28 for 263,533 words); passing the length and bytes
/// in registers avoids it. On x86-64 it makes little difference.
static const char *lookup(const sof_intern_t *pool, const void *data, size_t length, size_t hash) {
    size_t position = 0;
    sof_hashmap_element_t *e;
    while ((e = sof_hashmap_next_hashed(&pool->map, hash, &position)) != NULL) {
        const sof_intern_entry_t *entry = e->key;
        if (entry->length == length && memcmp(entry->data, data, length) == 0) {
            return entry->data;
        }
    }
    return NULL;
}

/// @brief Copies probe's string into the pool's blocks, with its entry before
/// it and a NUL after it, allocating a block if need be.
/// @return The entry, or NULL if memory couldn't be allocated.
static sof_intern_entry_t *store(sof_intern_t *pool, const sof_intern_entry_t *probe) {
    size_t length = probe->length;
    if (length > SIZE_MAX - HEADER_SIZE - sizeof(sof_intern_entry_t) - ALIGN) {
        return NULL;
    }
    size_t need = ALIGN_UP(sizeof(sof_intern_entry_t) + length + 1);
    sof_intern_block_t *block = pool->blocks;
    char *place;
    if (block != NULL && need <= block->size - pool->used) {
        place = (char *)block + pool->used;
        pool->used += need;
    } else if (HEADER_SIZE + need > BLOCK_SIZE) {
        // A long string gets a block of its own, put after the first so the
        // space left in that one can still be used.
        sof_intern_block_t *own = sof_alloc_malloc(&pool->alloc, HEADER_SIZE + need);
        if (own == NULL) {
            return NULL;
        }
        own->size = HEADER_SIZE + need;
        if (block != NULL) {
            own->next = block->next;
            block->next = own;
        } else {
            own->next = NULL;
            pool->blocks = own;
            pool->used = own->size;
        }
        place = (char *)own + HEADER_SIZE;
    } else {
        sof_intern_block_t *fresh = sof_alloc_malloc(&pool->alloc, BLOCK_SIZE);
        if (fresh == NULL) {
            return NULL;
        }
        fresh->size = BLOCK_SIZE;
        fresh->next = block;
        pool->blocks = fresh;
        place = (char *)fresh + HEADER_SIZE;
        pool->used = HEADER_SIZE + need;
    }
    sof_intern_entry_t *entry = (sof_intern_entry_t *)place;
    char *bytes = (char *)(entry + 1);
    if (length > 0) {
        memcpy(bytes, probe->data, length);
    }
    bytes[length] = '\0';
    entry->data = bytes;
    entry->length = length;
    entry->hash = probe->hash;
    return entry;
}

sof_intern_error_t sof_intern_clear(sof_intern_t *pool) {
    if (pool == NULL) {
        return SOF_INTERN_ERROR;
    }
    sof_hashmap_clear(&pool->map);
    free_blocks(pool);
    return SOF_INTERN_OK;
}

size_t sof_intern_count(const sof_intern_t *pool) {
    return pool == NULL ? 0 : pool->map.count;
}

const char *sof_intern_cstr(sof_intern_t *pool, const char *s, int *created) {
    if (s == NULL) {
        if (created != NULL) {
            *created = 0;
        }
        return NULL;
    }
    return sof_intern_string(pool, s, strlen(s), created);
}

const char *sof_intern_find(const sof_intern_t *pool, const void *data, size_t length) {
    if (pool == NULL || pool->map.elements == NULL || data == NULL) {
        return NULL;
    }
    sof_intern_entry_t probe = {data, length, sof_hash_bytes(data, length, 0)};
    return lookup(pool, data, length, sof_hashmap_hash(&pool->map, &probe));
}

sof_intern_error_t sof_intern_free(sof_intern_t *pool) {
    if (pool == NULL) {
        return SOF_INTERN_ERROR;
    }
    sof_hashmap_free(&pool->map);
    free_blocks(pool);
    memset(pool, 0, sizeof(*pool));
    return SOF_INTERN_OK;
}

uint64_t sof_intern_hash(const char *s) {
    if (s == NULL) {
        return sof_hash_bytes(NULL, 0, 0);
    }
    return ((const sof_intern_entry_t *)s - 1)->hash;
}

sof_intern_error_t sof_intern_init(sof_intern_t *pool, size_t expected_strings, const sof_alloc_t *alloc) {
    if (pool == NULL) {
        return SOF_INTERN_ERROR;
    }
    memset(pool, 0, sizeof(*pool));
    if (!sof_alloc_valid(alloc)) {
        return SOF_INTERN_ERROR;
    }
    if (alloc != NULL) {
        pool->alloc = *alloc;
    }
    if (sof_hashmap_init(&pool->map, &entry_keys, NULL, NULL, expected_strings, &pool->alloc) != SOF_HASHMAP_OK) {
        memset(pool, 0, sizeof(*pool));
        return SOF_INTERN_NO_MEM;
    }
    return SOF_INTERN_OK;
}

size_t sof_intern_length(const char *s) {
    if (s == NULL) {
        return 0;
    }
    return ((const sof_intern_entry_t *)s - 1)->length;
}

const char *sof_intern_next(const sof_intern_t *pool, size_t *position) {
    if (pool == NULL) {
        return NULL;
    }
    sof_hashmap_element_t *e = sof_hashmap_next(&pool->map, position);
    return e == NULL ? NULL : ((const sof_intern_entry_t *)e->key)->data;
}

const char *sof_intern_string(sof_intern_t *pool, const void *data, size_t length, int *created) {
    if (created != NULL) {
        *created = 0;
    }
    if (pool == NULL || pool->map.elements == NULL || data == NULL) {
        return NULL;
    }
    // The string is hashed once, for the lookup and, if it is new, its entry
    // and the map's insert.
    sof_intern_entry_t probe = {data, length, sof_hash_bytes(data, length, 0)};
    size_t hash = sof_hashmap_hash(&pool->map, &probe);
    const char *found = lookup(pool, data, length, hash);
    if (found != NULL) {
        return found;
    }
    // Remember where the blocks stood, so a failed insert can give the space
    // back.
    sof_intern_block_t *first = pool->blocks;
    sof_intern_block_t *second = first == NULL ? NULL : first->next;
    size_t used = pool->used;
    sof_intern_entry_t *entry = store(pool, &probe);
    if (entry == NULL) {
        return NULL;
    }
    if (sof_hashmap_insert_hashed(&pool->map, entry, hash, NULL) == NULL) {
        if (pool->blocks != first) {
            // A new first block, ordinary or the string's own.
            sof_intern_block_t *fresh = pool->blocks;
            pool->blocks = first;
            sof_alloc_free(&pool->alloc, fresh, fresh->size);
        } else if (first != NULL && first->next != second) {
            // A block of the string's own, after the first.
            sof_intern_block_t *own = first->next;
            first->next = second;
            sof_alloc_free(&pool->alloc, own, own->size);
        }
        pool->used = used;
        return NULL;
    }
    if (created != NULL) {
        *created = 1;
    }
    return entry->data;
}

int sof_intern_traverse(const sof_intern_t *pool, sof_intern_traverse_func_t cb, void *ctx) {
    if (pool == NULL) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    size_t position = 0;
    const char *s;
    while ((s = sof_intern_next(pool, &position)) != NULL) {
        int rc = cb(s, sof_intern_length(s), ctx);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}
