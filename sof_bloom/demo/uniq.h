/**
 * @file uniq.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Duplicate line finding in C, for the uniq program.
 * @details A uniq_t keeps one copy of each different line it is given, in
 * the order each was first seen, with the number of times it was given. A
 * line is any run of bytes, NULs included, so it works on binary data as
 * well as text; two lines are the same only if their bytes are. The lines
 * don't have to be sorted, or next to each other, to be found the same.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef UNIQ_H
#define UNIQ_H

#include <stddef.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"
#include "sof_string.h"
#include "sof_vector.h"

/**
 * @brief Error codes for the uniq functions.
 */
typedef enum uniq_error_t {
    UNIQ_OK = 0,
    UNIQ_NO_MEM,
    UNIQ_NO_MATCH,
    UNIQ_ERROR,
} uniq_error_t;

/**
 * @brief One different line, as the uniq_t keeps it.
 * @details line holds a copy of the line's bytes, which can include NULs,
 * and count the number of times the line was given to uniq_add(). index is
 * its place in the order the different lines were first seen, from 0. The
 * fields can be read, but must not be changed by the application. A
 * uniq_line_t doesn't move: a pointer to one stays good until uniq_clear() or
 * uniq_free().
 */
typedef struct uniq_line_t {
    sof_string_t line;
    size_t count;
    size_t index;
} uniq_line_t;

/**
 * @brief Called for each line visited by uniq_traverse().
 * @details Return 0 to continue, or a non-zero value to stop the traversal
 * early. It must not call uniq_add(), uniq_clear() or uniq_free(). ctx is the
 * ctx given to uniq_traverse().
 */
typedef int (*uniq_traverse_func_t)(const uniq_line_t *line, void *ctx);

/**
 * @brief A set of lines, in the order they were first seen.
 * @details map is a hashmap whose keys are the uniq_line_t of each different
 * line, hashed by their bytes, and lines a vector of pointers to the same
 * uniq_line_t, in the order they were first seen, so lines.count is the number
 * of different lines. total is the number of lines given to uniq_add(),
 * counting each time, and alloc a copy of the allocator (zeroed, meaning the
 * standard functions, if it was given none). These fields can be read, but
 * should not be changed by the application.
 *
 * uniq_init() must be called before using a uniq_t, and uniq_free() should
 * be called when it is no longer needed. uniq_add() adds a line, uniq_find()
 * looks one up, and uniq_next() and uniq_traverse() go through them in the
 * order they were first seen. uniq_clear() forgets them all.
 *
 * Every function accepts a NULL uniq_t, and one that was freed or whose
 * uniq_init() failed (map.elements is NULL), as described for each.
 *
 * The uniq_t does no locking. uniq_find(), uniq_next() and uniq_traverse()
 * only read it; every other function writes it.
 */
typedef struct uniq_t {
    sof_hashmap_t map;
    sof_vector_t lines;
    size_t total;
    sof_alloc_t alloc;
} uniq_t;

/**
 * @brief Adds a line, or counts it again if it was added before.
 * @details A new line is copied, so bytes can be in a buffer the caller
 * reuses, and goes after every line seen so far, with a count of 1. A line
 * seen before has its count raised by one and keeps its place.
 * @param u The set of lines.
 * @param bytes The line's bytes, which need not be NUL-terminated and can
 * hold NULs. NULL is taken as the empty line.
 * @param length The number of bytes.
 * @param created If not NULL, set to 1 if the line is new, or 0 if it was
 * seen before or the call failed.
 * @return The line, or NULL if memory ran out, in which case nothing was
 * added or counted, or u is NULL or not initialized.
 */
extern const uniq_line_t *uniq_add(uniq_t *u, const char *bytes, size_t length, int *created);

/**
 * @brief Forgets every line, keeping the memory of the hashmap and vector
 * for reuse.
 * @param u The set of lines.
 * @return UNIQ_OK, or UNIQ_ERROR if u is NULL or not initialized.
 */
extern uniq_error_t uniq_clear(uniq_t *u);

/**
 * @brief Finds a line.
 * @param u The set of lines.
 * @param bytes The line's bytes, which need not be NUL-terminated and can
 * hold NULs. NULL is taken as the empty line.
 * @param length The number of bytes.
 * @return The line, or NULL if it wasn't added, or u is NULL or not
 * initialized.
 */
extern const uniq_line_t *uniq_find(const uniq_t *u, const char *bytes, size_t length);

/**
 * @brief Frees everything the set allocated.
 * @details The uniq_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like one whose uniq_init() failed until it is
 * initialized again.
 * @param u The set of lines.
 * @return UNIQ_OK, or UNIQ_ERROR if u is NULL.
 */
extern uniq_error_t uniq_free(uniq_t *u);

/**
 * @brief Initializes a set of lines in a uniq_t the caller provides.
 * @param u The set to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap.
 * @param expected How many different lines to make room for; more can be
 * added. 0 makes room for a few.
 * @param alloc The memory functions the set uses, or NULL for the standard
 * functions. The set keeps a copy, so it can be on the stack.
 * @return UNIQ_OK, UNIQ_NO_MEM, or UNIQ_ERROR if u is NULL or alloc has a
 * malloc but no free; on an error, u is left empty.
 */
extern uniq_error_t uniq_init(uniq_t *u, size_t expected, const sof_alloc_t *alloc);

/**
 * @brief Returns the next line, in the order the lines were first seen, for
 * looping over them without a callback.
 * @details Set *position to 0 to start, then call this until it returns
 * NULL. Lines added during the loop come after the others, so the loop sees
 * them too.
 *
 *     size_t position = 0;
 *     const uniq_line_t *line;
 *     while ((line = uniq_next(&u, &position)) != NULL) {
 *         ...
 *     }
 *
 * @param u The set of lines.
 * @param position Where the loop is; updated on each call.
 * @return The next line, or NULL when there are no more, or u or position is
 * NULL, or u isn't initialized.
 */
extern const uniq_line_t *uniq_next(const uniq_t *u, size_t *position);

/**
 * @brief Calls cb for each line, in the order the lines were first seen.
 * @details If cb returns a non-zero value, the traversal stops and returns
 * that value.
 * @param u The set of lines.
 * @param cb The function called for each line. NULL visits nothing.
 * @param ctx Passed to every call of cb.
 * @return 0 if every line was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if u is NULL or not initialized.
 */
extern int uniq_traverse(const uniq_t *u, uniq_traverse_func_t cb, void *ctx);

#endif // UNIQ_H
