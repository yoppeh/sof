/**
 * @file sof_set.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A hash set, with union, intersection and difference, in C.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stddef.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"
#include "sof_set.h"

/// @brief Passes each element's key to the set's traverse callback.
typedef struct traverse_ctx_t {
    sof_set_traverse_func_t cb;
    void *ctx;
} traverse_ctx_t;

static sof_hashmap_element_t *find_element(const sof_set_t *set, const sof_hashmap_element_t *e, int same);
static void keep_where(sof_set_t *dst, const sof_set_t *src, int found);
static int same_hash(const sof_set_t *a, const sof_set_t *b);
static sof_set_error_t set_error(sof_hashmap_error_t err);
static int traverse_element(sof_hashmap_element_t *element, void *ctx);

/// @brief Returns set's element for the key of e, an element of another set,
/// or NULL. If same, which same_hash() gives, e's stored hash is used
/// rather than hashing the key again.
static sof_hashmap_element_t *find_element(const sof_set_t *set, const sof_hashmap_element_t *e, int same) {
    return same ? sof_hashmap_find_hashed(&set->map, e->key, e->hash) : sof_hashmap_find(&set->map, e->key);
}

/// @brief Removes from dst each key whose presence in src isn't found (1 to
/// keep the keys src has, 0 to keep those it hasn't).
static void keep_where(sof_set_t *dst, const sof_set_t *src, int found) {
    sof_hashmap_t *map = &dst->map;
    int same = same_hash(dst, src);
    size_t i = 0;
    while (i < map->capacity) {
        sof_hashmap_element_t *e = &map->elements[i];
        if (e->key != NULL && (find_element(src, e, same) != NULL) != found) {
            // Removing closes the gap by moving later keys of the run back,
            // so slot i may now hold a key not yet looked at: look again.
            // Only keys already kept can be moved past i (from the start of
            // the array, when the run wraps), and they are kept again.
            sof_hashmap_remove_element(map, e);
        } else {
            i++;
        }
    }
}

/// @brief Returns 1 if a and b hash keys alike, with the same hash callback
/// and ctx, so a hash stored in one is good in the other; or 0.
static int same_hash(const sof_set_t *a, const sof_set_t *b) {
    return a->map.keys.hash == b->map.keys.hash && a->map.ctx == b->map.ctx;
}

/// @brief Returns the set error matching a hashmap error.
static sof_set_error_t set_error(sof_hashmap_error_t err) {
    switch (err) {
    case SOF_HASHMAP_OK:
        return SOF_SET_OK;
    case SOF_HASHMAP_NO_MEM:
        return SOF_SET_NO_MEM;
    case SOF_HASHMAP_NO_MATCH:
        return SOF_SET_NO_MATCH;
    default:
        return SOF_SET_ERROR;
    }
}

/// @brief Calls the set's traverse callback with the element's key.
static int traverse_element(sof_hashmap_element_t *element, void *ctx) {
    traverse_ctx_t *t = ctx;
    return t->cb(element->key, t->ctx);
}

void *sof_set_add(sof_set_t *set, const void *key, int *created) {
    if (set == NULL) {
        if (created != NULL) {
            *created = 0;
        }
        return NULL;
    }
    sof_hashmap_element_t *e = sof_hashmap_insert(&set->map, key, created);
    return e != NULL ? e->key : NULL;
}

sof_set_error_t sof_set_clear(sof_set_t *set) {
    if (set == NULL) {
        return SOF_SET_ERROR;
    }
    return set_error(sof_hashmap_clear(&set->map));
}

size_t sof_set_count(const sof_set_t *set) {
    return set != NULL ? set->map.count : 0;
}

sof_set_error_t sof_set_difference(sof_set_t *dst, const sof_set_t *src) {
    if (dst == NULL || src == NULL || dst->map.elements == NULL) {
        return SOF_SET_ERROR;
    }
    if (dst == src) {
        return sof_set_clear(dst);
    }
    if (src->map.count < dst->map.count) {
        int same = same_hash(dst, src);
        size_t position = 0;
        sof_hashmap_element_t *e;
        while ((e = sof_hashmap_next(&src->map, &position)) != NULL) {
            sof_hashmap_element_t *found = find_element(dst, e, same);
            if (found != NULL) {
                sof_hashmap_remove_element(&dst->map, found);
            }
        }
    } else {
        keep_where(dst, src, 0);
    }
    return SOF_SET_OK;
}

int sof_set_equal(const sof_set_t *a, const sof_set_t *b) {
    if (a == NULL || b == NULL) {
        return -1;
    }
    return a->map.count == b->map.count && sof_set_is_subset(a, b) == 1;
}

void *sof_set_find(const sof_set_t *set, const void *key) {
    if (set == NULL) {
        return NULL;
    }
    sof_hashmap_element_t *e = sof_hashmap_find(&set->map, key);
    return e != NULL ? e->key : NULL;
}

sof_set_error_t sof_set_free(sof_set_t *set) {
    if (set == NULL) {
        return SOF_SET_ERROR;
    }
    return set_error(sof_hashmap_free(&set->map));
}

sof_set_error_t sof_set_init(sof_set_t *set, const sof_hashmap_key_callbacks_t *keys, void *ctx, size_t expected_keys, const sof_alloc_t *alloc) {
    if (set == NULL) {
        return SOF_SET_ERROR;
    }
    // The free key callback is the set's only delete callback: a set has no
    // data of its own for a del_func to free.
    return set_error(sof_hashmap_init(&set->map, keys, NULL, ctx, expected_keys, alloc));
}

sof_set_error_t sof_set_intersection(sof_set_t *dst, const sof_set_t *src) {
    if (dst == NULL || src == NULL || dst->map.elements == NULL) {
        return SOF_SET_ERROR;
    }
    if (dst != src) {
        keep_where(dst, src, 1);
    }
    return SOF_SET_OK;
}

int sof_set_is_disjoint(const sof_set_t *a, const sof_set_t *b) {
    if (a == NULL || b == NULL) {
        return -1;
    }
    if (a == b) {
        return a->map.count == 0;
    }
    const sof_set_t *small = a->map.count <= b->map.count ? a : b;
    const sof_set_t *large = small == a ? b : a;
    int same = same_hash(a, b);
    size_t position = 0;
    sof_hashmap_element_t *e;
    while ((e = sof_hashmap_next(&small->map, &position)) != NULL) {
        if (find_element(large, e, same) != NULL) {
            return 0;
        }
    }
    return 1;
}

int sof_set_is_subset(const sof_set_t *a, const sof_set_t *b) {
    if (a == NULL || b == NULL) {
        return -1;
    }
    if (a == b) {
        return 1;
    }
    if (a->map.count > b->map.count) {
        return 0;
    }
    int same = same_hash(a, b);
    size_t position = 0;
    sof_hashmap_element_t *e;
    while ((e = sof_hashmap_next(&a->map, &position)) != NULL) {
        if (find_element(b, e, same) == NULL) {
            return 0;
        }
    }
    return 1;
}

void *sof_set_next(const sof_set_t *set, size_t *position) {
    if (set == NULL) {
        return NULL;
    }
    sof_hashmap_element_t *e = sof_hashmap_next(&set->map, position);
    return e != NULL ? e->key : NULL;
}

sof_set_error_t sof_set_remove(sof_set_t *set, const void *key) {
    if (set == NULL) {
        return SOF_SET_ERROR;
    }
    return set_error(sof_hashmap_remove(&set->map, key));
}

sof_set_error_t sof_set_symmetric_difference(sof_set_t *dst, const sof_set_t *src) {
    if (dst == NULL || src == NULL || dst->map.elements == NULL) {
        return SOF_SET_ERROR;
    }
    if (dst == src) {
        return sof_set_clear(dst);
    }
    int same = same_hash(dst, src);
    size_t position = 0;
    sof_hashmap_element_t *e;
    while ((e = sof_hashmap_next(&src->map, &position)) != NULL) {
        sof_hashmap_element_t *found = find_element(dst, e, same);
        if (found != NULL) {
            sof_hashmap_remove_element(&dst->map, found);
        } else if ((same ? sof_hashmap_insert_hashed(&dst->map, e->key, e->hash, NULL) : sof_hashmap_insert(&dst->map, e->key, NULL)) == NULL) {
            return SOF_SET_NO_MEM;
        }
    }
    return SOF_SET_OK;
}

int sof_set_traverse(const sof_set_t *set, sof_set_traverse_func_t cb, void *ctx) {
    if (set == NULL) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    traverse_ctx_t t = {cb, ctx};
    return sof_hashmap_traverse(&set->map, traverse_element, &t);
}

sof_set_error_t sof_set_union(sof_set_t *dst, const sof_set_t *src) {
    if (dst == NULL || src == NULL || dst->map.elements == NULL) {
        return SOF_SET_ERROR;
    }
    if (dst == src) {
        return SOF_SET_OK;
    }
    int same = same_hash(dst, src);
    size_t position = 0;
    sof_hashmap_element_t *e;
    while ((e = sof_hashmap_next(&src->map, &position)) != NULL) {
        if ((same ? sof_hashmap_insert_hashed(&dst->map, e->key, e->hash, NULL) : sof_hashmap_insert(&dst->map, e->key, NULL)) == NULL) {
            return SOF_SET_NO_MEM;
        }
    }
    return SOF_SET_OK;
}
