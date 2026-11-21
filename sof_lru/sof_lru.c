/**
 * @file sof_lru.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A least-recently-used cache in C.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"
#include "sof_lru.h"

static void push_front(sof_lru_t *lru, sof_lru_node_t *node);
static void release(sof_lru_t *lru, sof_lru_node_t *node);
static void release_all(sof_lru_t *lru);
static void reset_spare(sof_lru_t *lru);
static void unlink_node(sof_lru_t *lru, sof_lru_node_t *node);

/// @brief Makes node, which is in no list, the most recently used entry.
static void push_front(sof_lru_t *lru, sof_lru_node_t *node) {
    node->newer = NULL;
    node->older = lru->newest;
    if (lru->newest != NULL) {
        lru->newest->newer = node;
    } else {
        lru->oldest = node;
    }
    lru->newest = node;
}

/// @brief Gives node to the del_func, removes its key from the map, which
/// frees it if the map owns it, and puts node on the spare list.
static void release(sof_lru_t *lru, sof_lru_node_t *node) {
    if (lru->del_func != NULL) {
        lru->del_func(node, lru->ctx);
    }
    unlink_node(lru, node);
    // The node's stored hash finds the map's element without hashing the
    // key again; the key is the map's own pointer, so it is found at once.
    sof_hashmap_remove_element(&lru->map, sof_hashmap_find_hashed(&lru->map, node->key, node->hash));
    memset(node, 0, sizeof(sof_lru_node_t));
    node->older = lru->spare;
    lru->spare = node;
    lru->count--;
}

/// @brief Gives every entry to the del_func, newest first, and empties the
/// map, which frees the keys it owns.
static void release_all(sof_lru_t *lru) {
    if (lru->del_func != NULL) {
        for (sof_lru_node_t *n = lru->newest; n != NULL; n = n->older) {
            lru->del_func(n, lru->ctx);
        }
    }
    sof_hashmap_clear(&lru->map);
}

/// @brief Empties the list and links every node into the spare list.
static void reset_spare(sof_lru_t *lru) {
    memset(lru->nodes, 0, lru->capacity * sizeof(sof_lru_node_t));
    for (size_t i = 0; i + 1 < lru->capacity; i++) {
        lru->nodes[i].older = &lru->nodes[i + 1];
    }
    lru->spare = lru->nodes;
    lru->newest = NULL;
    lru->oldest = NULL;
    lru->count = 0;
}

/// @brief Takes node out of the list of entries, leaving the rest linked.
static void unlink_node(sof_lru_t *lru, sof_lru_node_t *node) {
    if (node->newer != NULL) {
        node->newer->older = node->older;
    } else {
        lru->newest = node->older;
    }
    if (node->older != NULL) {
        node->older->newer = node->newer;
    } else {
        lru->oldest = node->newer;
    }
    node->newer = NULL;
    node->older = NULL;
}

sof_lru_error_t sof_lru_clear(sof_lru_t *lru) {
    if (lru == NULL) {
        return SOF_LRU_ERROR;
    }
    if (lru->nodes != NULL) {
        release_all(lru);
        reset_spare(lru);
    }
    return SOF_LRU_OK;
}

sof_lru_node_t *sof_lru_find(sof_lru_t *lru, const void *key) {
    sof_lru_node_t *node = sof_lru_peek(lru, key);
    if (node != NULL && node != lru->newest) {
        unlink_node(lru, node);
        push_front(lru, node);
    }
    return node;
}

sof_lru_error_t sof_lru_free(sof_lru_t *lru) {
    if (lru == NULL) {
        return SOF_LRU_ERROR;
    }
    if (lru->nodes != NULL) {
        release_all(lru);
        sof_hashmap_free(&lru->map);
        sof_alloc_free(&lru->alloc, lru->nodes, lru->capacity * sizeof(sof_lru_node_t));
    }
    memset(lru, 0, sizeof(sof_lru_t));
    return SOF_LRU_OK;
}

sof_lru_error_t sof_lru_init(sof_lru_t *lru, const sof_hashmap_key_callbacks_t *keys, sof_lru_node_delete_func_t del_func, void *ctx, size_t capacity, const sof_alloc_t *alloc) {
    if (lru == NULL) {
        return SOF_LRU_ERROR;
    }
    memset(lru, 0, sizeof(sof_lru_t));
    if (capacity == 0 || !sof_alloc_valid(alloc)) {
        return SOF_LRU_ERROR;
    }
    if (alloc != NULL) {
        lru->alloc = *alloc;
    }
    lru->nodes = sof_alloc_calloc(&lru->alloc, capacity, sizeof(sof_lru_node_t));
    if (lru->nodes == NULL) {
        memset(lru, 0, sizeof(sof_lru_t));
        return SOF_LRU_NO_MEM;
    }
    // sof_lru_insert() adds the new key to the map before it evicts the
    // oldest, so the map holds one more key than the cache at most. Room for
    // that many means the map never grows, and inserting never allocates
    // anything but key copies.
    size_t expected_keys = capacity < SIZE_MAX ? capacity + 1 : capacity;
    if (sof_hashmap_init(&lru->map, keys, NULL, ctx, expected_keys, &lru->alloc) != SOF_HASHMAP_OK) {
        sof_alloc_free(&lru->alloc, lru->nodes, capacity * sizeof(sof_lru_node_t));
        memset(lru, 0, sizeof(sof_lru_t));
        return SOF_LRU_NO_MEM;
    }
    lru->capacity = capacity;
    lru->del_func = del_func;
    lru->ctx = ctx;
    reset_spare(lru);
    return SOF_LRU_OK;
}

sof_lru_node_t *sof_lru_insert(sof_lru_t *lru, const void *key, int *created) {
    if (created != NULL) {
        *created = 0;
    }
    if (lru == NULL || lru->nodes == NULL || key == NULL) {
        return NULL;
    }
    // Insert into the map first, so a failed key copy leaves the cache as it
    // was, with nothing evicted.
    int made = 0;
    sof_hashmap_element_t *e = sof_hashmap_insert(&lru->map, key, &made);
    if (e == NULL) {
        return NULL;
    }
    if (!made) {
        sof_lru_node_t *node = e->data;
        if (node != lru->newest) {
            unlink_node(lru, node);
            push_front(lru, node);
        }
        return node;
    }
    // A full cache evicts its oldest entry, whose node goes to the head of
    // the spare list and so holds the new key. Removing the evicted key from
    // the map may move e, so set its data and take its key and hash first.
    void *stored = e->key;
    size_t hash = e->hash;
    e->data = lru->spare != NULL ? lru->spare : lru->oldest;
    if (lru->spare == NULL) {
        release(lru, lru->oldest);
    }
    sof_lru_node_t *node = lru->spare;
    lru->spare = node->older;
    node->key = stored;
    node->hash = hash;
    node->data = NULL;
    push_front(lru, node);
    lru->count++;
    if (created != NULL) {
        *created = 1;
    }
    return node;
}

sof_lru_node_t *sof_lru_newest(const sof_lru_t *lru) {
    return lru != NULL ? lru->newest : NULL;
}

sof_lru_node_t *sof_lru_next(const sof_lru_t *lru, const sof_lru_node_t *node) {
    if (lru == NULL || node == NULL) {
        return NULL;
    }
    return node->older;
}

sof_lru_node_t *sof_lru_oldest(const sof_lru_t *lru) {
    return lru != NULL ? lru->oldest : NULL;
}

sof_lru_node_t *sof_lru_peek(const sof_lru_t *lru, const void *key) {
    if (lru == NULL || lru->nodes == NULL || key == NULL) {
        return NULL;
    }
    sof_hashmap_element_t *e = sof_hashmap_find(&lru->map, key);
    return e != NULL ? e->data : NULL;
}

sof_lru_node_t *sof_lru_prev(const sof_lru_t *lru, const sof_lru_node_t *node) {
    if (lru == NULL || node == NULL) {
        return NULL;
    }
    return node->newer;
}

sof_lru_error_t sof_lru_remove(sof_lru_t *lru, const void *key) {
    if (lru == NULL || key == NULL) {
        return SOF_LRU_ERROR;
    }
    sof_lru_node_t *node = sof_lru_peek(lru, key);
    if (node == NULL) {
        return SOF_LRU_NO_MATCH;
    }
    release(lru, node);
    return SOF_LRU_OK;
}

sof_lru_error_t sof_lru_remove_node(sof_lru_t *lru, sof_lru_node_t *node) {
    if (lru == NULL || lru->nodes == NULL || node == NULL) {
        return SOF_LRU_ERROR;
    }
    // Compare addresses as integers: node may point anywhere.
    uintptr_t offset = (uintptr_t)node - (uintptr_t)lru->nodes;
    if ((uintptr_t)node < (uintptr_t)lru->nodes || offset % sizeof(sof_lru_node_t) != 0 || offset / sizeof(sof_lru_node_t) >= lru->capacity || node->key == NULL) {
        return SOF_LRU_ERROR;
    }
    release(lru, node);
    return SOF_LRU_OK;
}

int sof_lru_traverse(const sof_lru_t *lru, sof_lru_traverse_func_t cb, void *ctx) {
    if (lru == NULL) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    for (sof_lru_node_t *n = lru->newest; n != NULL; n = n->older) {
        int rc = cb(n, ctx);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}
