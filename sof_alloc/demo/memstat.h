/**
 * @file memstat.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A counting allocator: a sof_alloc_t that passes every call on to a
 * parent and keeps statistics of what passes through.
 * @details The application provides a memstat_t, sets it up with
 * memstat_init(), and passes the sof_alloc_t from memstat_allocator() to
 * the init function of any library in this collection. Every malloc,
 * realloc and free is counted, with the bytes live, the peak, and a
 * histogram of block sizes by power of two, and every free and realloc is
 * checked against the size the block was allocated with.
 * memstat_report() prints what was counted.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#ifndef MEMSTAT_H
#define MEMSTAT_H

#include <stddef.h>
#include <stdio.h>

#include "sof_alloc.h"
#include "sof_bit.h"
#include "sof_hashmap.h"

/**
 * @brief The number of buckets in a memstat_t's histogram.
 * @details Bucket k counts the blocks of more than 2^(k-1) and at most 2^k
 * bytes, bucket 0 those of 1 byte, so a size_t of any size has a bucket.
 */
#define MEMSTAT_BUCKETS (SOF_BIT_BITS + 1)

/**
 * @brief Error codes for the memstat functions.
 */
typedef enum memstat_error_t {
    MEMSTAT_OK = 0,
    MEMSTAT_NO_MEM,
    MEMSTAT_NO_MATCH,
    MEMSTAT_ERROR,
} memstat_error_t;

/**
 * @brief A counting allocator.
 * @details The application provides the memstat_t (on the stack, in another
 * structure or on the heap) and sets it up with memstat_init(). The counts
 * can be read at any time, but no field may be changed by the application.
 *
 * Each live block is kept in a sof_hashmap, keyed by its address, with the
 * size it was allocated with as its data, so every free can be checked. The
 * map's memory comes from the parent and isn't counted.
 *
 * Every function accepts a NULL memstat_t, and one that was freed or whose
 * init failed (ready is 0), as described for each.
 */
typedef struct memstat_t {
    /// @brief Successful mallocs.
    size_t mallocs;
    /// @brief Successful reallocs.
    size_t reallocs;
    /// @brief Successful frees.
    size_t frees;
    /// @brief Mallocs and reallocs that failed: the parent's, or the map's.
    size_t failures;
    /// @brief Frees and reallocs given a size other than the block's. The
    /// block's own size is used.
    size_t wrong_sizes;
    /// @brief Frees and reallocs of pointers that aren't live blocks. Nothing
    /// is done with them.
    size_t unknown;
    /// @brief The number of live blocks.
    size_t blocks;
    /// @brief The bytes in live blocks.
    size_t bytes;
    /// @brief The most blocks live at once.
    size_t peak_blocks;
    /// @brief The most bytes live at once.
    size_t peak_bytes;
    /// @brief The bytes asked for by every malloc and realloc.
    size_t total_bytes;
    /// @brief The sizes asked for by every successful malloc and realloc;
    /// see MEMSTAT_BUCKETS.
    size_t histogram[MEMSTAT_BUCKETS];
    /// @brief The live blocks: each element's key is a block, and its data
    /// the block's size.
    sof_hashmap_t live;
    /// @brief The parent allocator.
    sof_alloc_t alloc;
    /// @brief 1 once memstat_init() has succeeded, 0 when freed or failed.
    int ready;
} memstat_t;

/**
 * @brief Return a sof_alloc_t that allocates through a counting allocator.
 * @details It holds s's address, so s must not move while it is in use.
 * @param s The counting allocator. If it is NULL or not initialized, the
 * sof_alloc_t is still valid, but every allocation through it fails.
 * @return The sof_alloc_t, by value, to pass to an init function.
 */
extern sof_alloc_t memstat_allocator(memstat_t *s);

/**
 * @brief Free every block still live, through the parent, and the map of
 * blocks.
 * @details Read the counts, or call memstat_report(), first: blocks still
 * live at the end of a program are its leaks. The memstat_t itself isn't
 * freed; it is left empty, and acts like one whose memstat_init() failed
 * until it is initialized again.
 * @param s The counting allocator.
 * @return MEMSTAT_OK, or MEMSTAT_ERROR if s is NULL or not initialized.
 */
extern memstat_error_t memstat_free(memstat_t *s);

/**
 * @brief Initialize a counting allocator in a memstat_t the caller provides.
 * @param s The counting allocator to initialize, with every count 0.
 * @param alloc The parent allocator, or NULL for the standard functions. The
 * memstat_t keeps a copy, so it can be on the stack.
 * @return MEMSTAT_OK, MEMSTAT_NO_MEM if the map of blocks couldn't be
 * allocated, or MEMSTAT_ERROR if s is NULL or alloc has a malloc but no free;
 * on failure s is left empty and not initialized.
 */
extern memstat_error_t memstat_init(memstat_t *s, const sof_alloc_t *alloc);

/**
 * @brief Print the counts, the bytes and blocks live and at the peak, the
 * problems found, and the histogram of block sizes.
 * @param s The counting allocator. One that is NULL, freed or not initialized
 * prints its counts, which are 0 unless it was freed.
 * @param stream Where to print, such as stdout.
 * @return MEMSTAT_OK, or MEMSTAT_ERROR if s or stream is NULL.
 */
extern memstat_error_t memstat_report(const memstat_t *s, FILE *stream);

#endif // MEMSTAT_H
