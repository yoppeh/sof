/**
 * @file deps.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A dependency graph in C, for the deps program.
 * @details A deps_t holds targets, each named by a string, and what each
 * depends on. deps_sort() puts them in an order to build them in, every
 * target after everything it depends on, and deps_cycle() finds a cycle
 * when there is no such order. The order is the same every time for the
 * same graph: it depends only on the order the targets first appeared in,
 * and the order dependencies were added in, as deps_sort() describes.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef DEPS_H
#define DEPS_H

#include <stddef.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"

/**
 * @brief Error codes for the deps functions.
 * @details DEPS_CYCLE, from deps_sort(), means the targets can't all be
 * ordered, since some depend on each other.
 */
typedef enum deps_error_t {
    DEPS_OK = 0,
    DEPS_NO_MEM,
    DEPS_NO_MATCH,
    DEPS_ERROR,
    DEPS_CYCLE,
} deps_error_t;

/**
 * @brief A target in the graph.
 * @details key is its name, NUL-terminated, a copy the graph owns. index is
 * its place in the order targets first appeared in, from 0, so it is also
 * its place in the graph's nodes. deps holds the dep_count targets it
 * depends on, in the order they were added, each once; dependents holds the
 * dependent_count targets that depend on it, in order of index. data is the
 * application's: the graph sets it to NULL when the node is made, and never
 * reads or writes it again. The other fields can be read but should not be
 * changed by the application. A node never moves, so a pointer to it is good
 * until deps_clear() or deps_free().
 */
typedef struct deps_node_t {
    const char *key;
    void *data;
    size_t index;
    struct deps_node_t **deps;
    size_t dep_count;
    size_t dep_capacity;
    struct deps_node_t **dependents;
    size_t dependent_count;
    size_t dependent_capacity;
} deps_node_t;

/**
 * @brief A dependency graph.
 * @details map finds a node by name: its keys are the names, and the data of
 * each element the node. nodes holds the count nodes in the order they first
 * appeared, in an array of capacity, and edges is the number of dependencies
 * between them. alloc is a copy of the allocator (zeroed, meaning the
 * standard functions, if it was given none). These fields can be read, but
 * should not be changed by the application.
 *
 * deps_init() must be called before using a graph, and deps_free() should be
 * called when it is no longer needed. deps_add(), deps_depend() and
 * deps_add_list() add targets and dependencies, deps_find() finds a target,
 * and deps_sort() and deps_cycle() read the graph.
 *
 * Every function accepts a NULL graph, and a graph that was freed or whose
 * deps_init() failed (map.elements is NULL), as described for each.
 *
 * The graph does no locking. deps_cycle(), deps_find() and deps_sort() only
 * read it; every other function writes it.
 */
typedef struct deps_t {
    sof_hashmap_t map;
    deps_node_t **nodes;
    size_t count;
    size_t capacity;
    size_t edges;
    sof_alloc_t alloc;
} deps_t;

/**
 * @brief Finds a target, adding it if it isn't in the graph.
 * @param d The graph.
 * @param name The target's name, NUL-terminated. The graph keeps a copy.
 * @param created If not NULL, set to 1 if the target was added, or 0 if it
 * was already there or the call failed.
 * @return The node, or NULL if memory ran out, name is NULL or empty, or d
 * is NULL or not initialized.
 */
extern deps_node_t *deps_add(deps_t *d, const char *name, int *created);

/**
 * @brief Adds a target and a list of what it depends on.
 * @details The target is added, then each name in list, in order, as by
 * deps_depend(). Names in list are separated by spaces, tabs, carriage
 * returns, newlines, form feeds and vertical tabs, and can hold any other
 * byte. A list with no names adds just the target. If memory runs out, the
 * target and the names before the one that failed are added.
 * @param d The graph.
 * @param target The target's name, NUL-terminated.
 * @param list The names of what it depends on, NUL-terminated, or NULL for
 * none.
 * @return DEPS_OK, DEPS_NO_MEM, or DEPS_ERROR if target is NULL or empty, or
 * d is NULL or not initialized.
 */
extern deps_error_t deps_add_list(deps_t *d, const char *target, const char *list);

/**
 * @brief Removes every target, leaving an empty graph that can still be used.
 * @param d The graph.
 * @return DEPS_OK, or DEPS_ERROR if d is NULL or not initialized.
 */
extern deps_error_t deps_clear(deps_t *d);

/**
 * @brief Finds a cycle: targets that depend on each other.
 * @details Starts from the first target, in order of index, that deps_sort()
 * can't order, and follows the first of its dependencies that can't be
 * ordered either, and so on until a target comes round again. Those from its
 * first visit on are the cycle, so the result is the same every time.
 * @param d The graph.
 * @param cycle Where to put the cycle, with room for d->count nodes. Each
 * node depends on the next, and the last on the first; a target that
 * depends on itself is a cycle of one.
 * @param length Set to the number of nodes in the cycle, or 0 if there is
 * none or on an error.
 * @return DEPS_OK, DEPS_NO_MATCH if there is no cycle (the graph is empty
 * or deps_sort() orders it all), DEPS_NO_MEM, or DEPS_ERROR if cycle or
 * length is NULL, or d is NULL or not initialized.
 */
extern deps_error_t deps_cycle(const deps_t *d, deps_node_t **cycle, size_t *length);

/**
 * @brief Adds a dependency: target depends on dependency.
 * @details Either target is added if it isn't there. A dependency already
 * added is not added again. A target can depend on itself, which makes a
 * cycle.
 * @param d The graph.
 * @param target The name of the target that depends on the other.
 * @param dependency The name of the target it depends on.
 * @return DEPS_OK, DEPS_NO_MEM, leaving the graph as it was except perhaps
 * for the targets added, or DEPS_ERROR if either name is NULL or empty, or d
 * is NULL or not initialized.
 */
extern deps_error_t deps_depend(deps_t *d, const char *target, const char *dependency);

/**
 * @brief Finds a target by name.
 * @param d The graph.
 * @param name The name, NUL-terminated.
 * @return The node, or NULL if there is none, name is NULL, or d is NULL or
 * not initialized.
 */
extern deps_node_t *deps_find(const deps_t *d, const char *name);

/**
 * @brief Frees everything the graph allocated.
 * @details The deps_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like a graph whose deps_init() failed until it is
 * initialized again.
 * @param d The graph.
 * @return DEPS_OK, or DEPS_ERROR if d is NULL.
 */
extern deps_error_t deps_free(deps_t *d);

/**
 * @brief Initializes a graph in a deps_t the caller provides.
 * @param d The graph to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap.
 * @param alloc The memory functions the graph uses, or NULL for the standard
 * functions. The graph keeps a copy, so it can be on the stack.
 * @return DEPS_OK, DEPS_NO_MEM, or DEPS_ERROR if d is NULL or alloc has a
 * malloc but no free; on an error, d is left empty.
 */
extern deps_error_t deps_init(deps_t *d, const sof_alloc_t *alloc);

/**
 * @brief Puts the targets in an order to build them in.
 * @details Every target comes after everything it depends on. The targets
 * are ordered as by Kahn's algorithm, with a first-in, first-out queue of
 * the targets that are ready: those that depend on nothing go in first, in
 * order of index; then each target taken from the queue is put in the order,
 * and the targets waiting only on it go in at the back of the queue, in
 * order of index. So the order is the same every time, targets are built as
 * early as they can be, and targets that don't depend on each other keep the
 * order they first appeared in, round by round.
 * @param d The graph.
 * @param order Where to put the order, with room for d->count nodes.
 * @param count Set to the number of nodes put in order: d->count, or fewer
 * if there is a cycle, or 0 on an error.
 * @return DEPS_OK if every target was ordered; DEPS_CYCLE if some couldn't
 * be, since they are in a cycle or depend on one, when order holds those
 * that could; DEPS_NO_MEM; or DEPS_ERROR if order or count is NULL, or d is
 * NULL or not initialized.
 */
extern deps_error_t deps_sort(const deps_t *d, deps_node_t **order, size_t *count);

#endif // DEPS_H
