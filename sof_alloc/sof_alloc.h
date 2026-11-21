/**
 * @file sof_alloc.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Replaceable memory allocation in C.
 * @details A sof_alloc_t names the functions a library allocates and frees
 * with, and a context for them. The libraries in this collection that
 * allocate take one in their init function, and call these helpers, which
 * use the standard functions for a NULL sof_alloc_t or one without a malloc.
 * sof_alloc_malloc() and sof_alloc_free(), which every allocation and free
 * goes through, are static inline here, saving a call each time.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_ALLOC_H
#define SOF_ALLOC_H

#include <stddef.h>
#include <stdlib.h>

/**
 * @brief The memory functions an application gives a library.
 * @details Every function gets ctx. malloc is required, and so is free or
 * free_sized; realloc is optional.
 * - malloc returns a block of at least size bytes, aligned for any type, or
 *   NULL. It isn't called with a size of 0.
 * - realloc moves or resizes block p, of old_size bytes, to new_size bytes,
 *   keeping its contents up to the smaller size, and returns it, or returns
 *   NULL and leaves p as it was. p is never NULL, and neither size is 0.
 *   Without it, sof_alloc_realloc() uses malloc, a copy and a free.
 * - free_sized frees block p, which was allocated with size bytes (the size
 *   last asked of malloc or realloc). Allocators that keep blocks of a few
 *   sizes can use the size to find where a block goes without storing it.
 * - free frees block p. It is used when there is no free_sized.
 *
 * Neither free function is called with a NULL p. A sof_alloc_t whose malloc
 * is NULL, like a NULL sof_alloc_t, means the standard malloc(), realloc()
 * and free(). Libraries keep a copy, so the sof_alloc_t can be on the stack.
 */
typedef struct sof_alloc_t {
    void *(*malloc)(size_t size, void *ctx);
    void *(*realloc)(void *p, size_t old_size, size_t new_size, void *ctx);
    void (*free)(void *p, void *ctx);
    void (*free_sized)(void *p, size_t size, void *ctx);
    void *ctx;
} sof_alloc_t;

/**
 * @brief Allocates a zeroed array of count elements of size bytes.
 * @param alloc The allocator, or NULL for the standard functions.
 * @param count The number of elements.
 * @param size The size of each element.
 * @return The array, of count * size bytes, or NULL if it couldn't be
 * allocated, count * size overflows, or it is 0.
 */
extern void *sof_alloc_calloc(const sof_alloc_t *alloc, size_t count, size_t size);

/**
 * @brief Resizes a block, which may move.
 * @param alloc The allocator the block came from, or NULL for the standard
 * functions.
 * @param p The block, or NULL to allocate a new one.
 * @param old_size The size p was allocated with, or 0 if p is NULL.
 * @param new_size The size wanted.
 * @return The resized block, or NULL, leaving p as it was, if it couldn't be
 * allocated or new_size is 0.
 */
extern void *sof_alloc_realloc(const sof_alloc_t *alloc, void *p, size_t old_size, size_t new_size);

/**
 * @brief Copies length bytes of s into a new NUL-terminated string.
 * @details The copy is length + 1 bytes long, which is the size to free it
 * with.
 * @param alloc The allocator, or NULL for the standard functions.
 * @param s The bytes to copy.
 * @param length How many to copy.
 * @return The copy, or NULL if it couldn't be allocated or s is NULL.
 */
extern char *sof_alloc_strndup(const sof_alloc_t *alloc, const char *s, size_t length);

/**
 * @brief Checks that an allocator can be used.
 * @param alloc The allocator.
 * @return 1 if alloc is NULL, has no malloc (both meaning the standard
 * functions), or has a malloc and a free or free_sized; 0 if it has a malloc
 * but no way to free.
 */
extern int sof_alloc_valid(const sof_alloc_t *alloc);

/**
 * @brief Frees a block.
 * @param alloc The allocator the block came from, or NULL for the standard
 * functions.
 * @param p The block, or NULL, which does nothing.
 * @param size The size the block was allocated with, passed to free_sized.
 */
static inline void sof_alloc_free(const sof_alloc_t *alloc, void *p, size_t size) {
    if (p == NULL) {
        return;
    }
    if (alloc == NULL || alloc->malloc == NULL) {
        free(p);
    } else if (alloc->free_sized != NULL) {
        alloc->free_sized(p, size, alloc->ctx);
    } else {
        alloc->free(p, alloc->ctx);
    }
}

/**
 * @brief Allocates a block.
 * @param alloc The allocator, or NULL for the standard functions.
 * @param size The size of the block.
 * @return The block, or NULL if it couldn't be allocated or size is 0.
 */
static inline void *sof_alloc_malloc(const sof_alloc_t *alloc, size_t size) {
    if (size == 0) {
        return NULL;
    }
    if (alloc == NULL || alloc->malloc == NULL) {
        return malloc(size);
    }
    return alloc->malloc(size, alloc->ctx);
}

#endif // SOF_ALLOC_H
