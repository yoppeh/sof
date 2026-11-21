/**
 * @file deps.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A dependency graph in C, for the deps program.
 * @details Targets are found by name in a sof_hashmap, and ordered by
 * Kahn's algorithm, with the targets that are ready waiting in a sof_queue.
 * A cycle is found by a walk that keeps the targets on it in a sof_set.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"
#include "sof_queue.h"
#include "sof_set.h"
#include "deps.h"

static void free_nodes(deps_t *d);
static int grow(deps_t *d, deps_node_t ***array, size_t *capacity, size_t count);
static int is_space(char c);
static deps_error_t kahn(const deps_t *d, size_t *waiting, deps_node_t **order, size_t *count);
static int ready(const deps_t *d);

/// @brief Frees every node and what it holds, leaving the nodes array.
static void free_nodes(deps_t *d) {
    for (size_t i = 0; i < d->count; i++) {
        deps_node_t *node = d->nodes[i];
        sof_alloc_free(&d->alloc, node->deps, node->dep_capacity * sizeof(deps_node_t *));
        sof_alloc_free(&d->alloc, node->dependents, node->dependent_capacity * sizeof(deps_node_t *));
        sof_alloc_free(&d->alloc, node, sizeof(deps_node_t));
    }
    d->count = 0;
    d->edges = 0;
}

/// @brief Makes room in an array of node pointers for one more than count,
/// doubling its capacity if it is full.
/// @return 1, or 0 if memory ran out, leaving the array as it was.
static int grow(deps_t *d, deps_node_t ***array, size_t *capacity, size_t count) {
    if (count < *capacity) {
        return 1;
    }
    size_t new_capacity = *capacity == 0 ? 4 : 2 * *capacity;
    if (new_capacity > SIZE_MAX / sizeof(deps_node_t *)) {
        return 0;
    }
    deps_node_t **grown = sof_alloc_realloc(&d->alloc, *array, *capacity * sizeof(deps_node_t *), new_capacity * sizeof(deps_node_t *));
    if (grown == NULL) {
        return 0;
    }
    *array = grown;
    *capacity = new_capacity;
    return 1;
}

/// @brief Returns 1 if c separates names in a list: the whitespace of the C
/// locale, without depending on the locale as isspace() does.
static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/// @brief Kahn's algorithm: puts the targets that can be ordered in order,
/// if order isn't NULL, and sets count to how many there are. waiting, with
/// room for d->count, is left holding, for each target by index, how many of
/// its dependencies weren't ordered: 0 for the targets that were.
static deps_error_t kahn(const deps_t *d, size_t *waiting, deps_node_t **order, size_t *count) {
    sof_queue_t queue;
    if (sof_queue_init(&queue, NULL, NULL, &d->alloc) != SOF_QUEUE_OK) {
        return DEPS_ERROR;
    }
    deps_error_t err = DEPS_OK;
    for (size_t i = 0; i < d->count && err == DEPS_OK; i++) {
        waiting[i] = d->nodes[i]->dep_count;
        if (waiting[i] == 0 && sof_queue_enqueue(&queue, d->nodes[i]) != SOF_QUEUE_OK) {
            err = DEPS_NO_MEM;
        }
    }
    size_t n = 0;
    void *data;
    while (err == DEPS_OK && sof_queue_dequeue(&queue, &data) == SOF_QUEUE_OK) {
        deps_node_t *node = data;
        if (order != NULL) {
            order[n] = node;
        }
        n++;
        // The dependents are in order of index, so those made ready
        // together are queued in that order.
        for (size_t i = 0; i < node->dependent_count && err == DEPS_OK; i++) {
            deps_node_t *dependent = node->dependents[i];
            if (--waiting[dependent->index] == 0 && sof_queue_enqueue(&queue, dependent) != SOF_QUEUE_OK) {
                err = DEPS_NO_MEM;
            }
        }
    }
    sof_queue_free(&queue);
    *count = err == DEPS_OK ? n : 0;
    return err;
}

/// @brief Returns 1 if d is initialized: not NULL, freed or failed.
static int ready(const deps_t *d) {
    return d != NULL && d->map.elements != NULL;
}

deps_node_t *deps_add(deps_t *d, const char *name, int *created) {
    if (created != NULL) {
        *created = 0;
    }
    if (!ready(d) || name == NULL || name[0] == '\0') {
        return NULL;
    }
    int made;
    sof_hashmap_element_t *element = sof_hashmap_insert(&d->map, name, &made);
    if (element == NULL) {
        return NULL;
    }
    if (!made) {
        return element->data;
    }
    deps_node_t *node = NULL;
    if (grow(d, &d->nodes, &d->capacity, d->count)) {
        node = sof_alloc_calloc(&d->alloc, 1, sizeof(deps_node_t));
    }
    if (node == NULL) {
        sof_hashmap_remove_element(&d->map, element);
        return NULL;
    }
    // Nothing was inserted since, so element is still good.
    node->key = element->key;
    node->index = d->count;
    element->data = node;
    d->nodes[d->count++] = node;
    if (created != NULL) {
        *created = 1;
    }
    return node;
}

deps_error_t deps_add_list(deps_t *d, const char *target, const char *list) {
    if (!ready(d) || target == NULL || target[0] == '\0') {
        return DEPS_ERROR;
    }
    if (deps_add(d, target, NULL) == NULL) {
        return DEPS_NO_MEM;
    }
    if (list == NULL) {
        return DEPS_OK;
    }
    // sof_regex can split on a regular expression, but it needs PCRE2,
    // too much to take on for splitting at whitespace.
    const char *p = list;
    while (1) {
        while (is_space(*p)) {
            p++;
        }
        if (*p == '\0') {
            return DEPS_OK;
        }
        const char *start = p;
        while (*p != '\0' && !is_space(*p)) {
            p++;
        }
        size_t length = (size_t)(p - start);
        char *name = sof_alloc_strndup(&d->alloc, start, length);
        if (name == NULL) {
            return DEPS_NO_MEM;
        }
        deps_error_t err = deps_depend(d, target, name);
        sof_alloc_free(&d->alloc, name, length + 1);
        if (err != DEPS_OK) {
            return err;
        }
    }
}

deps_error_t deps_clear(deps_t *d) {
    if (!ready(d)) {
        return DEPS_ERROR;
    }
    free_nodes(d);
    sof_hashmap_clear(&d->map);
    return DEPS_OK;
}

deps_error_t deps_cycle(const deps_t *d, deps_node_t **cycle, size_t *length) {
    if (length != NULL) {
        *length = 0;
    }
    if (!ready(d) || cycle == NULL || length == NULL) {
        return DEPS_ERROR;
    }
    if (d->count == 0) {
        return DEPS_NO_MATCH;
    }
    // waiting is filled by kahn(); on_path holds the targets on the walk,
    // compared by pointer.
    size_t *waiting = sof_alloc_calloc(&d->alloc, d->count, sizeof(size_t));
    if (waiting == NULL) {
        return DEPS_NO_MEM;
    }
    sof_set_t on_path;
    sof_set_error_t set_err = sof_set_init(&on_path, NULL, NULL, 0, &d->alloc);
    if (set_err != SOF_SET_OK) {
        sof_alloc_free(&d->alloc, waiting, d->count * sizeof(size_t));
        return set_err == SOF_SET_NO_MEM ? DEPS_NO_MEM : DEPS_ERROR;
    }
    size_t sorted;
    deps_error_t err = kahn(d, waiting, NULL, &sorted);
    if (err == DEPS_OK && sorted == d->count) {
        err = DEPS_NO_MATCH;
    }
    if (err == DEPS_OK) {
        deps_node_t *node = NULL;
        for (size_t i = 0; node == NULL; i++) {
            if (waiting[i] > 0) {
                node = d->nodes[i];
            }
        }
        // A target that wasn't ordered is waiting on a dependency that
        // wasn't either, so the walk goes on until it comes round again.
        size_t steps = 0;
        while (err == DEPS_OK && sof_set_find(&on_path, node) == NULL) {
            if (sof_set_add(&on_path, node, NULL) == NULL) {
                err = DEPS_NO_MEM;
                break;
            }
            cycle[steps++] = node;
            deps_node_t *next = NULL;
            for (size_t i = 0; next == NULL; i++) {
                if (waiting[node->deps[i]->index] > 0) {
                    next = node->deps[i];
                }
            }
            node = next;
        }
        if (err == DEPS_OK) {
            // node came round again: the cycle starts where it was first.
            size_t first = 0;
            while (cycle[first] != node) {
                first++;
            }
            *length = steps - first;
            memmove(cycle, cycle + first, *length * sizeof(deps_node_t *));
        }
    }
    sof_set_free(&on_path);
    sof_alloc_free(&d->alloc, waiting, d->count * sizeof(size_t));
    return err;
}

deps_error_t deps_depend(deps_t *d, const char *target, const char *dependency) {
    if (!ready(d) || target == NULL || target[0] == '\0' || dependency == NULL || dependency[0] == '\0') {
        return DEPS_ERROR;
    }
    deps_node_t *t = deps_add(d, target, NULL);
    if (t == NULL) {
        return DEPS_NO_MEM;
    }
    deps_node_t *dep = deps_add(d, dependency, NULL);
    if (dep == NULL) {
        return DEPS_NO_MEM;
    }
    for (size_t i = 0; i < t->dep_count; i++) {
        if (t->deps[i] == dep) {
            return DEPS_OK;
        }
    }
    if (!grow(d, &t->deps, &t->dep_capacity, t->dep_count) || !grow(d, &dep->dependents, &dep->dependent_capacity, dep->dependent_count)) {
        return DEPS_NO_MEM;
    }
    t->deps[t->dep_count++] = dep;
    // Keep the dependents in order of index; they are mostly added in that
    // order, so this seldom moves any.
    size_t i = dep->dependent_count;
    while (i > 0 && dep->dependents[i - 1]->index > t->index) {
        dep->dependents[i] = dep->dependents[i - 1];
        i--;
    }
    dep->dependents[i] = t;
    dep->dependent_count++;
    d->edges++;
    return DEPS_OK;
}

deps_node_t *deps_find(const deps_t *d, const char *name) {
    if (!ready(d) || name == NULL) {
        return NULL;
    }
    const sof_hashmap_element_t *element = sof_hashmap_find(&d->map, name);
    return element == NULL ? NULL : element->data;
}

deps_error_t deps_free(deps_t *d) {
    if (d == NULL) {
        return DEPS_ERROR;
    }
    if (ready(d)) {
        free_nodes(d);
        sof_alloc_free(&d->alloc, d->nodes, d->capacity * sizeof(deps_node_t *));
        sof_hashmap_free(&d->map);
    }
    memset(d, 0, sizeof(*d));
    return DEPS_OK;
}

deps_error_t deps_init(deps_t *d, const sof_alloc_t *alloc) {
    if (d == NULL) {
        return DEPS_ERROR;
    }
    memset(d, 0, sizeof(*d));
    if (!sof_alloc_valid(alloc)) {
        return DEPS_ERROR;
    }
    if (alloc != NULL) {
        d->alloc = *alloc;
    }
    sof_hashmap_error_t err = sof_hashmap_init(&d->map, &sof_hashmap_string_keys, NULL, NULL, 0, &d->alloc);
    if (err != SOF_HASHMAP_OK) {
        memset(d, 0, sizeof(*d));
        return err == SOF_HASHMAP_NO_MEM ? DEPS_NO_MEM : DEPS_ERROR;
    }
    return DEPS_OK;
}

deps_error_t deps_sort(const deps_t *d, deps_node_t **order, size_t *count) {
    if (count != NULL) {
        *count = 0;
    }
    if (!ready(d) || order == NULL || count == NULL) {
        return DEPS_ERROR;
    }
    if (d->count == 0) {
        return DEPS_OK;
    }
    size_t *waiting = sof_alloc_calloc(&d->alloc, d->count, sizeof(size_t));
    if (waiting == NULL) {
        return DEPS_NO_MEM;
    }
    deps_error_t err = kahn(d, waiting, order, count);
    sof_alloc_free(&d->alloc, waiting, d->count * sizeof(size_t));
    if (err == DEPS_OK && *count < d->count) {
        err = DEPS_CYCLE;
    }
    return err;
}
