/**
 * @file sof_mem.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Memory allocators tuned to different purposes, each usable as an
 * sof_alloc_t, in c.
 * @details Four allocators, each in a file of its own and each taking a
 * parent sof_alloc_t, so they can be used side by side or stacked one on
 * another:
 * - sof_mem_arena_t: a bump allocator for memory freed all at once, with
 *   marks to free back to a point, and a form that works in a buffer the
 *   caller supplies and never calls a parent at all.
 * - sof_mem_pool_t: objects of one size, freed one at a time.
 * - sof_mem_slab_t: small blocks of any size, in size classes, each a pool,
 *   freed one at a time.
 * - sof_mem_track_t: passes everything to its parent, counting what is live
 *   and catching frees of the wrong size or of blocks it never allocated.
 *
 * Each has a function that returns a sof_alloc_t of it, to pass to the init
 * function of any library in this collection that allocates.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_MEM_H
#define SOF_MEM_H

#include <stddef.h>

#include "sof_alloc.h"

/**
 * @brief The usable size of an arena's blocks when sof_mem_arena_init() is
 * given 0.
 */
#define SOF_MEM_ARENA_BLOCK_SIZE 65536

/**
 * @brief The size of a pool's slabs when sof_mem_pool_init() is given 0
 * objects per slab: as many objects as fit in this many bytes, and at least
 * one. Slab allocators' pools use it too.
 */
#define SOF_MEM_POOL_SLAB_SIZE 65536

/**
 * @brief The largest size a slab allocator serves from its size classes
 * when sof_mem_slab_init() is given 0.
 */
#define SOF_MEM_SLAB_MAX_SIZE 1024

/**
 * @brief The largest max_size sof_mem_slab_init() accepts.
 */
#define SOF_MEM_SLAB_LIMIT 8192

/**
 * @brief Error codes for the sof_mem functions.
 */
typedef enum sof_mem_error_t {
    SOF_MEM_OK = 0,
    SOF_MEM_NO_MEM,
    SOF_MEM_NO_MATCH,
    SOF_MEM_ERROR,
} sof_mem_error_t;

/**
 * @brief A block an arena or pool got from its parent allocator, or the
 * buffer a fixed arena works in.
 * @details The application can follow the lists but must not change them.
 * The usable bytes follow the header, which is padded to the strictest
 * alignment.
 */
typedef struct sof_mem_block_t {
    /// @brief The next block in the list, or NULL.
    struct sof_mem_block_t *next;
    /// @brief The previous block in the list, or NULL. Kept only for an
    /// arena's large blocks.
    struct sof_mem_block_t *prev;
    /// @brief The number of usable bytes after the header.
    size_t size;
    /// @brief For an arena's large blocks, the order they were allocated in,
    /// which sof_mem_arena_reset() uses; otherwise 0.
    size_t serial;
} sof_mem_block_t;

/**
 * @brief A bump allocator.
 * @details The application provides the sof_mem_arena_t (on the stack, in
 * another structure or on the heap) and sets it up with
 * sof_mem_arena_init(), or sof_mem_arena_init_buffer() to work in a
 * buffer of its own. Its fields can be read, but must not be changed by the
 * application.
 *
 * Allocations come from blocks of block_size usable bytes, one after
 * another, each aligned for any type. A block is never given back until
 * sof_mem_arena_free(); sof_mem_arena_clear() and sof_mem_arena_reset()
 * go back to an earlier point and use the same blocks again, so an arena
 * used the same way after each asks its parent for nothing. An allocation
 * larger than a quarter of block_size gets a block of its own, which
 * sof_mem_arena_release() gives back to the parent at once.
 * sof_mem_arena_release() of any other allocation gives back nothing,
 * except that the most recent one is taken back, so the next allocation
 * reuses its space.
 *
 * A fixed arena, from sof_mem_arena_init_buffer(), has one block, in the
 * caller's buffer, and no parent: every allocation comes from the buffer,
 * whatever its size, and fails once the buffer is full.
 *
 * Blocks never move, so a pointer the arena returned is good until it is
 * released, reallocated, or the arena is cleared, reset past it or freed.
 * Nothing points into the sof_mem_arena_t, so it can be moved, except while
 * a sof_alloc_t from sof_mem_arena_allocator() is in use, since that holds
 * its address.
 *
 * Every function accepts a NULL arena, and one that was freed or whose init
 * failed (ready is 0), as described for each.
 */
typedef struct sof_mem_arena_t {
    /// @brief The standard blocks, in the order they are used, or NULL. A
    /// fixed arena's one block is in its buffer.
    sof_mem_block_t *blocks;
    /// @brief The block allocations are coming from, or NULL before the
    /// first allocation and after sof_mem_arena_clear().
    sof_mem_block_t *current;
    /// @brief The large blocks, one per large allocation, newest first, or
    /// NULL.
    sof_mem_block_t *large;
    /// @brief The most recent allocation in current, which
    /// sof_mem_arena_release() and sof_mem_arena_realloc() can take back
    /// or grow, or NULL.
    unsigned char *last;
    /// @brief The number of bytes of current already handed out.
    size_t offset;
    /// @brief The usable size of each standard block, or of a fixed arena's
    /// buffer.
    size_t block_size;
    /// @brief The bytes held from the parent allocator, headers included; 0
    /// for a fixed arena.
    size_t allocated;
    /// @brief The serial the next large block gets.
    size_t serial;
    /// @brief The parent allocator the blocks come from.
    sof_alloc_t alloc;
    /// @brief 1 for an arena in a buffer of the caller's, with no parent.
    int fixed;
    /// @brief 1 once init has succeeded, 0 when freed or failed.
    int ready;
} sof_mem_arena_t;

/**
 * @brief A point in an arena to go back to, from sof_mem_arena_mark().
 * @details The application keeps it, by value, and passes it to
 * sof_mem_arena_reset(); it must not change it.
 */
typedef struct sof_mem_arena_mark_t {
    /// @brief The arena's current block when the mark was made.
    sof_mem_block_t *block;
    /// @brief The arena's offset in that block.
    size_t offset;
    /// @brief The serial the next large block would have got.
    size_t serial;
} sof_mem_arena_mark_t;

/**
 * @brief A pool of objects of one size.
 * @details The application provides the sof_mem_pool_t and sets it up with
 * sof_mem_pool_init(). Its fields can be read, but must not be changed by
 * the application.
 *
 * Objects come from slabs of per_slab objects, each aligned for any type. A
 * released object goes onto a free list and is the next to be handed out.
 * Slabs are never given back until sof_mem_pool_free();
 * sof_mem_pool_clear() takes back every object and starts again from the
 * first slab.
 *
 * Slabs never move, so an object is good until it is released or the pool
 * is cleared or freed. The sof_mem_pool_t can be moved, except while an
 * sof_alloc_t from sof_mem_pool_allocator() is in use.
 *
 * Every function accepts a NULL pool, and one that was freed or whose
 * sof_mem_pool_init() failed (ready is 0), as described for each.
 */
typedef struct sof_mem_pool_t {
    /// @brief The slabs, in the order they are used, or NULL.
    sof_mem_block_t *slabs;
    /// @brief The slab new objects are coming from, or NULL before the first
    /// and after sof_mem_pool_clear().
    sof_mem_block_t *current;
    /// @brief The released objects, each holding a pointer to the next, or
    /// NULL.
    void *free_list;
    /// @brief The number of objects of current already handed out.
    size_t used;
    /// @brief The size of each object, as given to sof_mem_pool_init().
    size_t object_size;
    /// @brief The space each object takes in a slab: object_size, at least a
    /// pointer, rounded up to the strictest alignment.
    size_t slot_size;
    /// @brief The number of objects in each slab.
    size_t per_slab;
    /// @brief The number of objects handed out and not released.
    size_t count;
    /// @brief The bytes held from the parent allocator, headers included.
    size_t allocated;
    /// @brief The parent allocator the slabs come from.
    sof_alloc_t alloc;
    /// @brief 1 once sof_mem_pool_init() has succeeded, 0 when freed or
    /// failed.
    int ready;
} sof_mem_pool_t;

/**
 * @brief Small blocks of any size, from a pool per size class.
 * @details The application provides the sof_mem_slab_t and sets it up with
 * sof_mem_slab_init(). Its fields can be read, but must not be changed by
 * the application.
 *
 * Sizes up to max_size are rounded up to a size class and served by that
 * class's pool: every multiple of the strictest alignment up to 128 bytes,
 * then four classes between each power of two and the next (160, 192, 224,
 * 256, 320, ...), so no block wastes more than a fifth of its size, past
 * alignment. Larger sizes are passed to the parent. Freeing is told the
 * size, as every sof_alloc_free() in this collection is, so it finds the class
 * without a header on the block. A pool allocates its first slab only when
 * its class is first used.
 *
 * Every function accepts a NULL slab allocator, and one that was freed or
 * whose sof_mem_slab_init() failed (ready is 0), as described for each.
 */
typedef struct sof_mem_slab_t {
    /// @brief The pools, one per size class, smallest first.
    sof_mem_pool_t *pools;
    /// @brief The number of size classes.
    size_t classes;
    /// @brief The largest size served from a pool: the size of the largest
    /// class.
    size_t max_size;
    /// @brief The parent allocator the pools' slabs, the array of pools, and
    /// blocks larger than max_size come from.
    sof_alloc_t alloc;
    /// @brief 1 once sof_mem_slab_init() has succeeded, 0 when freed or
    /// failed.
    int ready;
} sof_mem_slab_t;

/**
 * @brief What a tracking allocator found wrong with a free or realloc.
 */
typedef enum sof_mem_track_problem_t {
    /// @brief The block was freed or reallocated with a size other than the
    /// one it was allocated with. The tracker uses the right size.
    SOF_MEM_TRACK_WRONG_SIZE,
    /// @brief The pointer isn't a live block of this tracker: never
    /// allocated by it, or already freed. Nothing is done with it.
    SOF_MEM_TRACK_UNKNOWN_BLOCK,
} sof_mem_track_problem_t;

/**
 * @brief Told of each problem a tracking allocator finds.
 * @details p is the pointer the application gave, size the size it gave,
 * and problem what is wrong. It must not call into the tracker. ctx is the
 * ctx given to sof_mem_track_init().
 */
typedef void (*sof_mem_track_error_func_t)(const void *p, size_t size, sof_mem_track_problem_t problem, void *ctx);

/**
 * @brief Called for each live block visited by sof_mem_track_traverse().
 * @details p is the block and size the size it was allocated with. Return 0
 * to continue, or a non-zero value to stop the traversal early. It must not
 * call into the tracker. ctx is the ctx given to sof_mem_track_traverse().
 */
typedef int (*sof_mem_track_traverse_func_t)(void *p, size_t size, void *ctx);

/// @brief The hidden header of a tracked block; internal.
struct sof_mem_track_header_t;

/**
 * @brief An allocator that tracks what passes through it to its parent.
 * @details The application provides the sof_mem_track_t and sets it up with
 * sof_mem_track_init(). Its fields can be read, but must not be changed by
 * the application.
 *
 * Each block gets a hidden header, before the bytes the application sees,
 * holding its size and links to the other live blocks, so the tracker can
 * check every free and list every block still live. Checks for blocks it
 * never allocated, or that were already freed, read the memory before the
 * pointer, so they catch most such mistakes but, for a block already given
 * back to the parent, not all: a memory checker such as AddressSanitizer
 * catches the rest.
 *
 * Every function accepts a NULL tracker, and one that was freed or whose
 * sof_mem_track_init() failed (ready is 0), as described for each.
 */
typedef struct sof_mem_track_t {
    /// @brief The live blocks, newest first, or NULL.
    struct sof_mem_track_header_t *live;
    /// @brief The number of live blocks.
    size_t blocks;
    /// @brief The bytes in live blocks, as the application asked for them,
    /// headers not included.
    size_t bytes;
    /// @brief The most bytes live at once.
    size_t peak_bytes;
    /// @brief The most blocks live at once.
    size_t peak_blocks;
    /// @brief Successful allocations, not counting reallocations.
    size_t allocations;
    /// @brief Successful reallocations.
    size_t reallocations;
    /// @brief Successful frees.
    size_t frees;
    /// @brief Allocations and reallocations the parent couldn't satisfy.
    size_t failures;
    /// @brief Problems found, each also given to error_func.
    size_t errors;
    /// @brief Told of each problem, or NULL.
    sof_mem_track_error_func_t error_func;
    /// @brief Application context passed to error_func.
    void *ctx;
    /// @brief The parent allocator.
    sof_alloc_t alloc;
    /// @brief 1 once sof_mem_track_init() has succeeded, 0 when freed or
    /// failed.
    int ready;
} sof_mem_track_t;

/**
 * @brief Allocate a block from an arena.
 * @param a The arena.
 * @param size The size of the block in bytes.
 * @return The block, aligned for any type and not zeroed, or NULL if size
 * is 0, the parent couldn't give a new block, a fixed arena's buffer is
 * full, or a is NULL or not initialized.
 */
extern void *sof_mem_arena_alloc(sof_mem_arena_t *a, size_t size);

/**
 * @brief Return a sof_alloc_t that allocates from an arena.
 * @details Its malloc is sof_mem_arena_alloc(), its realloc
 * sof_mem_arena_realloc() and its free_sized sof_mem_arena_release(). It
 * holds a's address, so a must not move while it is in use. Everything it
 * allocated is taken back by sof_mem_arena_clear() and
 * sof_mem_arena_free(), and what it allocated after a mark by
 * sof_mem_arena_reset() to that mark, so a library's structure must not be
 * used after either; free the structure first, then clear the arena.
 * @param a The arena. If it is NULL or not initialized, the sof_alloc_t is
 * still valid, but every allocation through it fails.
 * @return The sof_alloc_t, by value, to pass to an init function.
 */
extern sof_alloc_t sof_mem_arena_allocator(sof_mem_arena_t *a);

/**
 * @brief Take back every allocation, keeping the standard blocks.
 * @details Large blocks are given back to the parent. Every pointer the
 * arena returned, and every mark, is invalid afterwards.
 * @param a The arena.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if a is NULL or not initialized.
 */
extern sof_mem_error_t sof_mem_arena_clear(sof_mem_arena_t *a);

/**
 * @brief Give every block back to the parent.
 * @details A fixed arena's buffer belongs to the caller and isn't touched.
 * The sof_mem_arena_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like an arena whose init failed until it is
 * initialized again.
 * @param a The arena.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if a is NULL or not initialized.
 */
extern sof_mem_error_t sof_mem_arena_free(sof_mem_arena_t *a);

/**
 * @brief Initialize an arena in a sof_mem_arena_t the caller provides.
 * @details Nothing is allocated until the first sof_mem_arena_alloc().
 * @param a The arena to initialize.
 * @param block_size The usable size of each standard block, rounded up to
 * the strictest alignment, or 0 for SOF_MEM_ARENA_BLOCK_SIZE. Allocations
 * larger than a quarter of it get blocks of their own.
 * @param alloc The parent allocator the blocks come from, or NULL for the
 * standard functions. The arena keeps a copy, so it can be on the stack.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if a is NULL, block_size is too
 * large to round up, or alloc has a malloc but no free; on failure the arena
 * is left empty and not initialized.
 */
extern sof_mem_error_t sof_mem_arena_init(sof_mem_arena_t *a, size_t block_size, const sof_alloc_t *alloc);

/**
 * @brief Initialize an arena that works in a buffer the caller provides.
 * @details The arena keeps its block header at the start of the buffer,
 * aligned, and allocates from the rest; it never calls a parent allocator.
 * The buffer must stay valid, and must not be used otherwise, until the
 * arena is freed.
 * @param a The arena to initialize.
 * @param buffer The buffer.
 * @param size The size of the buffer in bytes.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if a or buffer is NULL, or the
 * buffer is too small to hold the header and one allocation; on failure the
 * arena is left empty and not initialized.
 */
extern sof_mem_error_t sof_mem_arena_init_buffer(sof_mem_arena_t *a, void *buffer, size_t size);

/**
 * @brief Return a mark of the arena's present point, to go back to.
 * @details Allocations made before the mark stay as they are, and can no
 * longer grow in place or be taken back by sof_mem_arena_release(), so
 * sof_mem_arena_reset() never cuts one short. That changes the arena, so a
 * isn't const.
 * @param a The arena.
 * @return The mark, by value; a zeroed mark if a is NULL or not
 * initialized.
 */
extern sof_mem_arena_mark_t sof_mem_arena_mark(sof_mem_arena_t *a);

/**
 * @brief Resize a block from an arena, which may move.
 * @details The most recent allocation grows or shrinks in place while it
 * fits in its block; any other shrinks in place, keeping its space. A large
 * block is resized by the parent. Otherwise a new block is allocated, the
 * contents copied, and p released.
 * @param a The arena.
 * @param p The block, or NULL to allocate a new one.
 * @param old_size The size p was allocated with, or 0 if p is NULL.
 * @param new_size The size wanted.
 * @return The resized block, or NULL, leaving p as it was, if new_size is 0,
 * the memory couldn't be allocated, or a is NULL or not initialized.
 */
extern void *sof_mem_arena_realloc(sof_mem_arena_t *a, void *p, size_t old_size, size_t new_size);

/**
 * @brief Give a block back to an arena.
 * @details A large block goes back to the parent at once. The most recent
 * allocation is taken back, so the next allocation reuses its space. Any
 * other block is kept until the arena is cleared, reset past it or freed. p
 * must have come from this arena with this size, and not been released
 * since.
 * @param a The arena.
 * @param p The block, or NULL, which does nothing.
 * @param size The size p was allocated with.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if a is NULL or not initialized.
 */
extern sof_mem_error_t sof_mem_arena_release(sof_mem_arena_t *a, void *p, size_t size);

/**
 * @brief Go back to a mark, taking back everything allocated since.
 * @details Large blocks allocated since the mark go back to the parent; the
 * standard blocks are kept, and used again from the mark. Every pointer
 * allocated since the mark, and every later mark, is invalid afterwards;
 * the mark itself can be used again.
 * @param a The arena.
 * @param mark A mark of a, from sof_mem_arena_mark(), made since a was last
 * cleared and not past a mark already reset to.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if the mark can't be a's, or a is
 * NULL or not initialized.
 */
extern sof_mem_error_t sof_mem_arena_reset(sof_mem_arena_t *a, sof_mem_arena_mark_t mark);

/**
 * @brief Allocate an object from a pool.
 * @param pool The pool.
 * @return The object, object_size bytes aligned for any type and not
 * zeroed, or NULL if the parent couldn't give a new slab, or pool is NULL or
 * not initialized.
 */
extern void *sof_mem_pool_alloc(sof_mem_pool_t *pool);

/**
 * @brief Return a sof_alloc_t that allocates from a pool.
 * @details Its malloc gives an object from the pool for any size up to
 * object_size, and passes larger sizes on to the parent; its free_sized
 * tells the two apart by the size it is given, and its realloc moves a block
 * between them when its size crosses object_size. It holds pool's address,
 * so pool must not move while it is in use. Blocks from the parent must be
 * freed before sof_mem_pool_free(), which forgets the parent.
 * @param pool The pool. If it is NULL or not initialized, the sof_alloc_t is
 * still valid, but every allocation through it fails.
 * @return The sof_alloc_t, by value, to pass to an init function.
 */
extern sof_alloc_t sof_mem_pool_allocator(sof_mem_pool_t *pool);

/**
 * @brief Take back every object, keeping the slabs.
 * @param pool The pool.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if pool is NULL or not initialized.
 */
extern sof_mem_error_t sof_mem_pool_clear(sof_mem_pool_t *pool);

/**
 * @brief Give every slab back to the parent.
 * @details The sof_mem_pool_t itself isn't freed; it is left empty, and
 * acts like a pool whose sof_mem_pool_init() failed until it is
 * initialized again.
 * @param pool The pool.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if pool is NULL or not initialized.
 */
extern sof_mem_error_t sof_mem_pool_free(sof_mem_pool_t *pool);

/**
 * @brief Initialize a pool in a sof_mem_pool_t the caller provides.
 * @details Nothing is allocated until the first sof_mem_pool_alloc().
 * @param pool The pool to initialize.
 * @param object_size The size of each object in bytes.
 * @param per_slab The number of objects in each slab, or 0 for as many as
 * fit in SOF_MEM_POOL_SLAB_SIZE bytes.
 * @param alloc The parent allocator the slabs come from, or NULL for the
 * standard functions. The pool keeps a copy, so it can be on the stack.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if pool is NULL, object_size is 0,
 * a slab would be too large to allocate, or alloc has a malloc but no free;
 * on failure the pool is left empty and not initialized.
 */
extern sof_mem_error_t sof_mem_pool_init(sof_mem_pool_t *pool, size_t object_size, size_t per_slab, const sof_alloc_t *alloc);

/**
 * @brief Give an object back to its pool.
 * @details The object goes onto the free list and is the next one
 * sof_mem_pool_alloc() returns. It must have come from this pool and not
 * been released since.
 * @param pool The pool.
 * @param p The object, or NULL, which does nothing.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if pool is NULL or not initialized.
 */
extern sof_mem_error_t sof_mem_pool_release(sof_mem_pool_t *pool, void *p);

/**
 * @brief Allocate a block from a slab allocator.
 * @param slab The slab allocator.
 * @param size The size of the block in bytes.
 * @return The block, aligned for any type and not zeroed: from the pool of
 * size's class if size is up to max_size, otherwise from the parent. NULL if
 * size is 0, the memory couldn't be allocated, or slab is NULL or not
 * initialized.
 */
extern void *sof_mem_slab_alloc(sof_mem_slab_t *slab, size_t size);

/**
 * @brief Return a sof_alloc_t that allocates from a slab allocator.
 * @details Its malloc is sof_mem_slab_alloc(), its realloc
 * sof_mem_slab_realloc() and its free_sized sof_mem_slab_release(). It
 * holds slab's address, so slab must not move while it is in use. Blocks
 * larger than max_size come from the parent and must be freed before
 * sof_mem_slab_free(), which forgets the parent.
 * @param slab The slab allocator. If it is NULL or not initialized, the
 * sof_alloc_t is still valid, but every allocation through it fails.
 * @return The sof_alloc_t, by value, to pass to an init function.
 */
extern sof_alloc_t sof_mem_slab_allocator(sof_mem_slab_t *slab);

/**
 * @brief Take back every block in the size classes, keeping the slabs.
 * @details Blocks larger than max_size belong to the parent and aren't
 * affected.
 * @param slab The slab allocator.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if slab is NULL or not initialized.
 */
extern sof_mem_error_t sof_mem_slab_clear(sof_mem_slab_t *slab);

/**
 * @brief Give every slab, and the array of pools, back to the parent.
 * @details The sof_mem_slab_t itself isn't freed; it is left empty, and
 * acts like one whose sof_mem_slab_init() failed until it is initialized
 * again.
 * @param slab The slab allocator.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if slab is NULL or not initialized.
 */
extern sof_mem_error_t sof_mem_slab_free(sof_mem_slab_t *slab);

/**
 * @brief Initialize a slab allocator in a sof_mem_slab_t the caller
 * provides.
 * @details Allocates the array of pools, one per size class; each pool
 * allocates its first slab when its class is first used.
 * @param slab The slab allocator to initialize.
 * @param max_size The largest size to serve from the size classes, rounded
 * up to a class, or 0 for SOF_MEM_SLAB_MAX_SIZE. At most
 * SOF_MEM_SLAB_LIMIT.
 * @param slab_size The size of each pool's slabs in bytes, or 0 for
 * SOF_MEM_POOL_SLAB_SIZE. Each slab holds as many blocks of its class as
 * fit, and at least one. Smaller slabs suit a small parent, such as a fixed
 * arena; larger ones ask the parent less often.
 * @param alloc The parent allocator, or NULL for the standard functions. The
 * slab allocator keeps a copy, so it can be on the stack.
 * @return SOF_MEM_OK, SOF_MEM_NO_MEM if the array of pools couldn't be
 * allocated, or SOF_MEM_ERROR if slab is NULL, max_size is larger than
 * SOF_MEM_SLAB_LIMIT, a slab would be too large to allocate, or alloc has a
 * malloc but no free; on failure the slab allocator is left empty and not
 * initialized.
 */
extern sof_mem_error_t sof_mem_slab_init(sof_mem_slab_t *slab, size_t max_size, size_t slab_size, const sof_alloc_t *alloc);

/**
 * @brief Resize a block from a slab allocator, which may move.
 * @details A block that stays in its size class keeps its place, and one
 * that stays larger than max_size is resized by the parent. Otherwise a new
 * block is allocated, the contents copied, and p released.
 * @param slab The slab allocator.
 * @param p The block, or NULL to allocate a new one.
 * @param old_size The size p was allocated with, or 0 if p is NULL.
 * @param new_size The size wanted.
 * @return The resized block, or NULL, leaving p as it was, if new_size is 0,
 * the memory couldn't be allocated, or slab is NULL or not initialized.
 */
extern void *sof_mem_slab_realloc(sof_mem_slab_t *slab, void *p, size_t old_size, size_t new_size);

/**
 * @brief Give a block back to a slab allocator.
 * @details A block of up to max_size goes back to its class's pool, and is
 * the next one of that class handed out; a larger one goes back to the
 * parent. p must have come from this slab allocator with this size, and not
 * been released since.
 * @param slab The slab allocator.
 * @param p The block, or NULL, which does nothing.
 * @param size The size p was allocated with.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if slab is NULL or not initialized.
 */
extern sof_mem_error_t sof_mem_slab_release(sof_mem_slab_t *slab, void *p, size_t size);

/**
 * @brief Allocate a tracked block from the parent.
 * @param t The tracker.
 * @param size The size of the block in bytes.
 * @return The block, aligned for any type and not zeroed, or NULL if size
 * is 0, the parent couldn't allocate it (counted in failures), or t is NULL
 * or not initialized.
 */
extern void *sof_mem_track_alloc(sof_mem_track_t *t, size_t size);

/**
 * @brief Return a sof_alloc_t that allocates through a tracker.
 * @details Its malloc is sof_mem_track_alloc(), its realloc
 * sof_mem_track_realloc() and its free_sized sof_mem_track_release(). It
 * holds t's address, so t must not move while it is in use.
 * @param t The tracker. If it is NULL or not initialized, the sof_alloc_t is
 * still valid, but every allocation through it fails.
 * @return The sof_alloc_t, by value, to pass to an init function.
 */
extern sof_alloc_t sof_mem_track_allocator(sof_mem_track_t *t);

/**
 * @brief Free every live block, and set the counts back to 0.
 * @details error_func and ctx are kept.
 * @param t The tracker.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if t is NULL or not initialized.
 */
extern sof_mem_error_t sof_mem_track_clear(sof_mem_track_t *t);

/**
 * @brief Free every live block and leave the tracker empty.
 * @details The sof_mem_track_t itself isn't freed; it is left empty, and
 * acts like one whose sof_mem_track_init() failed until it is initialized
 * again. Read the counts, or call sof_mem_track_traverse() to list leaks,
 * before this.
 * @param t The tracker.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if t is NULL or not initialized.
 */
extern sof_mem_error_t sof_mem_track_free(sof_mem_track_t *t);

/**
 * @brief Initialize a tracker in a sof_mem_track_t the caller provides.
 * @param t The tracker to initialize.
 * @param error_func Told of each problem found, or NULL.
 * @param ctx The context pointer passed to error_func.
 * @param alloc The parent allocator, or NULL for the standard functions. The
 * tracker keeps a copy, so it can be on the stack.
 * @return SOF_MEM_OK, or SOF_MEM_ERROR if t is NULL or alloc has a malloc
 * but no free; on failure the tracker is left empty and not initialized.
 */
extern sof_mem_error_t sof_mem_track_init(sof_mem_track_t *t, sof_mem_track_error_func_t error_func, void *ctx, const sof_alloc_t *alloc);

/**
 * @brief Resize a tracked block through the parent, which may move it.
 * @details If p isn't a live block of t, the problem is reported and
 * nothing is done. If old_size is wrong, the problem is reported and the
 * block is resized from its real size.
 * @param t The tracker.
 * @param p The block, or NULL to allocate a new one.
 * @param old_size The size p was allocated with, or 0 if p is NULL.
 * @param new_size The size wanted.
 * @return The resized block, or NULL, leaving p as it was, if new_size is 0,
 * p isn't a live block of t, the parent couldn't resize it (counted in
 * failures), or t is NULL or not initialized.
 */
extern void *sof_mem_track_realloc(sof_mem_track_t *t, void *p, size_t old_size, size_t new_size);

/**
 * @brief Free a tracked block.
 * @details If size is wrong, the problem is reported and the block is freed
 * with its real size.
 * @param t The tracker.
 * @param p The block, or NULL, which does nothing.
 * @param size The size p was allocated with.
 * @return SOF_MEM_OK, SOF_MEM_NO_MATCH if p isn't a live block of t (the
 * problem is reported and nothing is freed), or SOF_MEM_ERROR if t is NULL
 * or not initialized.
 */
extern sof_mem_error_t sof_mem_track_release(sof_mem_track_t *t, void *p, size_t size);

/**
 * @brief Visit every live block, newest first.
 * @details cb is called with each block, its size and ctx, unless it is
 * NULL, in which case nothing is visited. If cb returns a non-zero value,
 * the traversal stops and returns that value. Listing what is live at the
 * end of a program finds its leaks.
 * @param t The tracker.
 * @param cb The callback function to be called for each block.
 * @param ctx The context pointer passed to every call of cb.
 * @return 0 if every block was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if t is NULL or not initialized.
 */
extern int sof_mem_track_traverse(const sof_mem_track_t *t, sof_mem_track_traverse_func_t cb, void *ctx);

#endif // SOF_MEM_H
