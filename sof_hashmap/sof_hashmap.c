/**
 * @file sof_hashmap.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Hashmap implementation in C.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_hash.h"
#include "sof_hashmap.h"

static const size_t min_capacity = 16;

static size_t capacity_for(size_t keys);
static size_t hash_key(const sof_hashmap_t *map, const void *key);
static sof_hashmap_element_t *insert_hashed(sof_hashmap_t *map, const void *key, size_t full_hash, int *created);
static sof_hashmap_element_t *lookup(const sof_hashmap_t *map, const void *key, size_t full_hash, size_t *index);
static void place(sof_hashmap_t *map, const sof_hashmap_element_t *element);
static void release_all(sof_hashmap_t *map);
static sof_hashmap_error_t resize(sof_hashmap_t *map, size_t capacity);

size_t sof_hashmap_string_hash(const void *key, void *ctx) {
    return (size_t)sof_hash_string(key, 0);
}

int sof_hashmap_string_equal(const void *a, const void *b, void *ctx) {
    return strcmp(a, b) == 0;
}

static void *string_copy(const void *key, const sof_alloc_t *alloc, void *ctx) {
    return sof_alloc_strndup(alloc, key, strlen(key));
}

static void string_free(void *key, const sof_alloc_t *alloc, void *ctx) {
    sof_alloc_free(alloc, key, strlen(key) + 1);
}

const sof_hashmap_key_callbacks_t sof_hashmap_string_keys = {
    sof_hashmap_string_hash,
    sof_hashmap_string_equal,
    string_copy,
    string_free,
};

/// @brief Returns the smallest power-of-two capacity, at least min_capacity,
/// that holds keys elements without growing, which sof_hashmap_insert() does
/// once three quarters of it are in use; or 0 if there is none.
static size_t capacity_for(size_t keys) {
    size_t capacity = min_capacity;
    while (capacity / 4 * 3 < keys) {
        if (capacity > SIZE_MAX / 2) {
            return 0;
        }
        capacity <<= 1;
    }
    return capacity;
}

/// @brief Hashes key with the map's hash callback, or by address, then mixes
/// the result.
static size_t hash_key(const sof_hashmap_t *map, const void *key) {
    uint64_t x = map->keys.hash != NULL ? map->keys.hash(key, map->ctx) : (uintptr_t)key;
    // The slot comes from the low bits, which vary little in aligned
    // addresses and in many simple hashes, so keys would pile up in long
    // runs. Mix all the bits down into them.
    return (size_t)sof_hash_mix(x);
}

/// @brief Inserts key, whose full hash is full_hash, or finds it; the map is
/// initialized and key isn't NULL. *created, if given, is already 0.
static sof_hashmap_element_t *insert_hashed(sof_hashmap_t *map, const void *key, size_t full_hash, int *created) {
    size_t index = 0;
    sof_hashmap_element_t *e = lookup(map, key, full_hash, &index);
    if (e != NULL) {
        return e;
    }
    // Grow before the map is three quarters full: past that, runs of probed
    // slots get long. It also guarantees an empty slot to end every probe.
    if (map->count >= map->capacity / 4 * 3) {
        if (map->capacity > SIZE_MAX / 2 || resize(map, map->capacity << 1) != SOF_HASHMAP_OK) {
            return NULL;
        }
        lookup(map, key, full_hash, &index);
    }
    void *stored = (void *)key;
    if (map->keys.copy != NULL) {
        stored = map->keys.copy(key, &map->alloc, map->ctx);
        if (stored == NULL) {
            return NULL;
        }
    }
    e = &map->elements[index];
    e->hash = full_hash;
    e->key = stored;
    e->data = NULL;
    map->count++;
    if (created != NULL) {
        *created = 1;
    }
    return e;
}

/// @brief Returns the element holding key, or NULL with *index set to the
/// empty slot that ended the search. A stored key that is the same pointer
/// as key is equal to it without calling equal, which saves comparing a key
/// with itself when the caller has the map's own pointer.
static sof_hashmap_element_t *lookup(const sof_hashmap_t *map, const void *key, size_t full_hash, size_t *index) {
    size_t i = full_hash & map->mask;
    while (map->elements[i].key != NULL) {
        if (map->elements[i].hash == full_hash) {
            if (map->elements[i].key == key || (map->keys.equal != NULL && map->keys.equal(map->elements[i].key, key, map->ctx) != 0)) {
                return &map->elements[i];
            }
        }
        i = (i + 1) & map->mask;
    }
    *index = i;
    return NULL;
}

/// @brief Puts an element whose key isn't in the map into the first empty
/// slot of its run, without calling any key callback.
static void place(sof_hashmap_t *map, const sof_hashmap_element_t *element) {
    size_t i = element->hash & map->mask;
    while (map->elements[i].key != NULL) {
        i = (i + 1) & map->mask;
    }
    map->elements[i] = *element;
    map->count++;
}

/// @brief Gives every element to the del_func and every key to the free key
/// callback, if there are any.
static void release_all(sof_hashmap_t *map) {
    if (map->del_func == NULL && map->keys.free == NULL) {
        return;
    }
    for (size_t i = 0; i < map->capacity; i++) {
        sof_hashmap_element_t *e = &map->elements[i];
        if (e->key != NULL) {
            if (map->del_func != NULL) {
                map->del_func(e, map->ctx);
            }
            if (map->keys.free != NULL) {
                map->keys.free(e->key, &map->alloc, map->ctx);
            }
        }
    }
}

/// @brief Moves the elements to a new array of capacity slots, a power of two
/// larger than the count. Leaves the map as it was if it can't be allocated.
static sof_hashmap_error_t resize(sof_hashmap_t *map, size_t capacity) {
    sof_hashmap_t new_map = *map;
    new_map.capacity = capacity;
    new_map.mask = new_map.capacity - 1;
    new_map.count = 0;
    new_map.elements = sof_alloc_calloc(&map->alloc, new_map.capacity, sizeof(sof_hashmap_element_t));
    if (new_map.elements == NULL) {
        return SOF_HASHMAP_NO_MEM;
    }
    for (size_t i = 0; i < map->capacity; i++) {
        if (map->elements[i].key != NULL) {
            place(&new_map, &map->elements[i]);
        }
    }
    sof_alloc_free(&map->alloc, map->elements, map->capacity * sizeof(sof_hashmap_element_t));
    *map = new_map;
    return SOF_HASHMAP_OK;
}

sof_hashmap_error_t sof_hashmap_clear(sof_hashmap_t *map) {
    if (map == NULL) {
        return SOF_HASHMAP_ERROR;
    }
    if (map->elements != NULL) {
        release_all(map);
        memset(map->elements, 0, map->capacity * sizeof(sof_hashmap_element_t));
    }
    map->count = 0;
    return SOF_HASHMAP_OK;
}

sof_hashmap_element_t *sof_hashmap_find(const sof_hashmap_t *map, const void *key) {
    if (map == NULL || map->elements == NULL || key == NULL) {
        return NULL;
    }
    size_t index;
    return lookup(map, key, hash_key(map, key), &index);
}

sof_hashmap_element_t *sof_hashmap_find_hashed(const sof_hashmap_t *map, const void *key, size_t hash) {
    if (map == NULL || map->elements == NULL || key == NULL) {
        return NULL;
    }
    size_t index;
    return lookup(map, key, hash, &index);
}

sof_hashmap_error_t sof_hashmap_free(sof_hashmap_t *map) {
    if (map == NULL) {
        return SOF_HASHMAP_ERROR;
    }
    if (map->elements != NULL) {
        release_all(map);
        sof_alloc_free(&map->alloc, map->elements, map->capacity * sizeof(sof_hashmap_element_t));
    }
    memset(map, 0, sizeof(sof_hashmap_t));
    return SOF_HASHMAP_OK;
}

size_t sof_hashmap_hash(const sof_hashmap_t *map, const void *key) {
    if (map == NULL || map->elements == NULL || key == NULL) {
        return 0;
    }
    return hash_key(map, key);
}

sof_hashmap_error_t sof_hashmap_init(sof_hashmap_t *map, const sof_hashmap_key_callbacks_t *keys, sof_hashmap_element_delete_func_t del_func, void *ctx, size_t expected_keys, const sof_alloc_t *alloc) {
    if (map == NULL) {
        return SOF_HASHMAP_ERROR;
    }
    memset(map, 0, sizeof(sof_hashmap_t));
    if (!sof_alloc_valid(alloc)) {
        return SOF_HASHMAP_ERROR;
    }
    if (keys != NULL) {
        map->keys = *keys;
    }
    map->del_func = del_func;
    map->ctx = ctx;
    if (alloc != NULL) {
        map->alloc = *alloc;
    }
    size_t capacity = capacity_for(expected_keys);
    if (capacity == 0) {
        memset(map, 0, sizeof(sof_hashmap_t));
        return SOF_HASHMAP_NO_MEM;
    }
    map->elements = sof_alloc_calloc(&map->alloc, capacity, sizeof(sof_hashmap_element_t));
    if (map->elements == NULL) {
        memset(map, 0, sizeof(sof_hashmap_t));
        return SOF_HASHMAP_NO_MEM;
    }
    map->capacity = capacity;
    map->mask = capacity - 1;
    return SOF_HASHMAP_OK;
}

sof_hashmap_element_t *sof_hashmap_insert(sof_hashmap_t *map, const void *key, int *created) {
    if (created != NULL) {
        *created = 0;
    }
    if (map == NULL || map->elements == NULL || key == NULL) {
        return NULL;
    }
    return insert_hashed(map, key, hash_key(map, key), created);
}

sof_hashmap_element_t *sof_hashmap_insert_hashed(sof_hashmap_t *map, const void *key, size_t hash, int *created) {
    if (created != NULL) {
        *created = 0;
    }
    if (map == NULL || map->elements == NULL || key == NULL) {
        return NULL;
    }
    return insert_hashed(map, key, hash, created);
}

sof_hashmap_element_t *sof_hashmap_next(const sof_hashmap_t *map, size_t *position) {
    if (map == NULL || position == NULL) {
        return NULL;
    }
    for (size_t i = *position; i < map->capacity; i++) {
        if (map->elements[i].key != NULL) {
            *position = i + 1;
            return &map->elements[i];
        }
    }
    *position = map->capacity;
    return NULL;
}

sof_hashmap_element_t *sof_hashmap_next_hashed(const sof_hashmap_t *map, size_t hash, size_t *position) {
    if (map == NULL || map->elements == NULL || position == NULL) {
        return NULL;
    }
    // *position counts the slots already probed from the home slot. Every
    // run ends at an empty slot, since the map is never more than three
    // quarters full.
    for (size_t step = *position; step < map->capacity; step++) {
        sof_hashmap_element_t *e = &map->elements[(hash + step) & map->mask];
        if (e->key == NULL) {
            *position = map->capacity;
            return NULL;
        }
        if (e->hash == hash) {
            *position = step + 1;
            return e;
        }
    }
    *position = map->capacity;
    return NULL;
}

sof_hashmap_error_t sof_hashmap_remove(sof_hashmap_t *map, const void *key) {
    if (map == NULL || key == NULL) {
        return SOF_HASHMAP_ERROR;
    }
    sof_hashmap_element_t *e = sof_hashmap_find(map, key);
    if (e == NULL) {
        return SOF_HASHMAP_NO_MATCH;
    }
    return sof_hashmap_remove_element(map, e);
}

sof_hashmap_error_t sof_hashmap_remove_element(sof_hashmap_t *map, sof_hashmap_element_t *element) {
    if (map == NULL || map->elements == NULL || element == NULL) {
        return SOF_HASHMAP_ERROR;
    }
    // Compare addresses as integers: element may point anywhere.
    uintptr_t offset = (uintptr_t)element - (uintptr_t)map->elements;
    if ((uintptr_t)element < (uintptr_t)map->elements || offset % sizeof(sof_hashmap_element_t) != 0 || offset / sizeof(sof_hashmap_element_t) >= map->capacity || element->key == NULL) {
        return SOF_HASHMAP_ERROR;
    }
    size_t gap = offset / sizeof(sof_hashmap_element_t);
    if (map->del_func != NULL) {
        map->del_func(element, map->ctx);
    }
    void *removed_key = element->key;
    // Emptying the slot would hide any key probed past it, so close the gap
    // by moving back each following key that may live there (backward shift
    // deletion), until an empty slot ends the run.
    size_t next = gap;
    while (1) {
        next = (next + 1) & map->mask;
        if (map->elements[next].key == NULL) {
            break;
        }
        size_t home = map->elements[next].hash & map->mask;
        // The key at next stays put if its home slot lies after the gap, up
        // to next, going around the end of the array if need be.
        int stays = gap <= next ? (gap < home && home <= next) : (gap < home || home <= next);
        if (!stays) {
            map->elements[gap] = map->elements[next];
            gap = next;
        }
    }
    memset(&map->elements[gap], 0, sizeof(sof_hashmap_element_t));
    map->count--;
    if (map->keys.free != NULL) {
        map->keys.free(removed_key, &map->alloc, map->ctx);
    }
    return SOF_HASHMAP_OK;
}

sof_hashmap_error_t sof_hashmap_reserve(sof_hashmap_t *map, size_t keys) {
    if (map == NULL || map->elements == NULL) {
        return SOF_HASHMAP_ERROR;
    }
    size_t capacity = capacity_for(keys);
    if (capacity == 0) {
        return SOF_HASHMAP_NO_MEM;
    }
    if (capacity <= map->capacity) {
        return SOF_HASHMAP_OK;
    }
    return resize(map, capacity);
}

int sof_hashmap_traverse(const sof_hashmap_t *map, sof_hashmap_traverse_func_t cb, void *ctx) {
    if (map == NULL) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    for (size_t i = 0; i < map->capacity; i++) {
        if (map->elements[i].key != NULL) {
            int rc = cb(&map->elements[i], ctx);
            if (rc != 0) {
                return rc;
            }
        }
    }
    return 0;
}
