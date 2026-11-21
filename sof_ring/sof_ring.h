/**
 * @file sof_ring.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A fixed-capacity circular buffer of fixed-size values in c.
 * @details Each element is element_size bytes, copied in at the back by
 * sof_ring_push() and out from the front by sof_ring_pop(), first in, first
 * out. The elements are kept in one array of capacity elements, allocated by
 * sof_ring_init() and never again, so pushing and popping never allocate.
 * When the ring is full, sof_ring_push() fails and sof_ring_push_overwrite()
 * replaces the oldest element.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_RING_H
#define SOF_RING_H

#include <stddef.h>

#include "sof_alloc.h"

/**
 * @brief Error codes for the sof_ring functions.
 */
typedef enum sof_ring_error_t {
    SOF_RING_OK = 0,
    SOF_RING_NO_MEM,
    SOF_RING_NO_MATCH,
    SOF_RING_ERROR,
    /// @brief sof_ring_push() found the ring full.
    SOF_RING_FULL,
} sof_ring_error_t;

/**
 * @brief Receives an element just before it is removed, when it isn't copied
 * out to the caller.
 * @details Called by sof_ring_clear() and sof_ring_free() for each element,
 * from oldest to newest, by sof_ring_pop() when it is given no place to copy
 * the element to, and by sof_ring_push_overwrite() for the oldest element it
 * replaces, so the application can free what the element owns. element
 * points to the element's element_size bytes in the ring. It must not call
 * into the ring. ctx is the ctx given to sof_ring_init().
 */
typedef void (*sof_ring_element_delete_func_t)(void *element, void *ctx);

/**
 * @brief Called for each element visited by sof_ring_traverse().
 * @details element points to the element's element_size bytes in the ring;
 * the callback may change them. Return 0 to continue, or a non-zero value to
 * stop the traversal early. It must not push onto or pop from the ring. ctx
 * is the ctx given to sof_ring_traverse().
 */
typedef int (*sof_ring_traverse_func_t)(void *element, void *ctx);

/**
 * @brief A ring.
 * @details The application provides the sof_ring_t (on the stack, in another
 * structure or on the heap) and sets it up with sof_ring_init(). Its fields
 * can be read, but must not be changed by the application. Nothing points
 * into the sof_ring_t, so it can be moved or copied, as long as only one
 * copy is used afterwards.
 *
 * The array never moves, so a pointer to an element, from sof_ring_peek(),
 * sof_ring_at(), sof_ring_next() or a callback, stays good until that
 * element is popped or overwritten. The slot it points to is then reused by
 * a later push.
 *
 * Every function accepts a NULL ring, and a ring that was freed or whose
 * sof_ring_init() failed (ready is 0), as described for each.
 */
typedef struct sof_ring_t {
    /// @brief The array, capacity * element_size bytes, or NULL when not
    /// initialized.
    unsigned char *elements;
    /// @brief The size of each element in bytes.
    size_t element_size;
    /// @brief The number of elements the ring holds when full.
    size_t capacity;
    /// @brief The number of elements.
    size_t count;
    /// @brief The index in the array of the oldest element, the front. The
    /// others follow it, wrapping from the end of the array to the start.
    size_t head;
    /// @brief Callback letting the application free what an element owns, or
    /// NULL.
    sof_ring_element_delete_func_t del_func;
    /// @brief Application context passed to del_func.
    void *ctx;
    /// @brief The memory functions the array is allocated and freed with.
    sof_alloc_t alloc;
    /// @brief 1 once sof_ring_init() has succeeded, 0 when freed or failed.
    int ready;
} sof_ring_t;

/**
 * @brief Return the element at a position, counting from the oldest.
 * @details sof_ring_at(r, 0) is the oldest element, the next to be popped,
 * and sof_ring_at(r, r->count - 1) the newest, the last pushed.
 * @param r The ring.
 * @param index The position, from 0 to count - 1.
 * @return A pointer to the element's element_size bytes in the ring, or NULL
 * if index is count or more, or r is NULL or not initialized.
 */
extern void *sof_ring_at(const sof_ring_t *r, size_t index);

/**
 * @brief Remove every element, leaving an empty ring that can still be used.
 * @details Each element goes to the del_func, if there is one, before it is
 * removed, from oldest to newest. The array is kept.
 * @param r The ring.
 * @return SOF_RING_OK, or SOF_RING_ERROR if r is NULL or not initialized.
 */
extern sof_ring_error_t sof_ring_clear(sof_ring_t *r);

/**
 * @brief Remove every element and release the ring's memory.
 * @details Each element goes to the del_func, if there is one, before it is
 * removed, from oldest to newest. The sof_ring_t itself isn't freed (it
 * belongs to the caller); it is left empty, and acts like a ring whose
 * sof_ring_init() failed until it is initialized again.
 * @param r The ring.
 * @return SOF_RING_OK, or SOF_RING_ERROR if r is NULL or not initialized.
 */
extern sof_ring_error_t sof_ring_free(sof_ring_t *r);

/**
 * @brief Initialize a ring in a sof_ring_t the caller provides.
 * @details The array of capacity elements is allocated here, once; nothing
 * else in the library allocates. The del_func is optional and is called with
 * each element that is removed without being copied out; see
 * sof_ring_element_delete_func_t. ctx is passed to every call of del_func;
 * it can be NULL.
 * @param r The ring to initialize.
 * @param element_size The size of each element in bytes.
 * @param capacity The number of elements the ring holds when full.
 * @param del_func The element delete function, or NULL.
 * @param ctx The context pointer passed to del_func.
 * @param alloc The memory functions for the array, or NULL for the standard
 * functions. The ring keeps a copy, so it can be on the stack.
 * @return SOF_RING_OK, SOF_RING_NO_MEM if the array couldn't be allocated,
 * or SOF_RING_ERROR if r is NULL, element_size or capacity is 0, or alloc
 * has a malloc but no free; on failure the ring is left empty and not
 * initialized.
 */
extern sof_ring_error_t sof_ring_init(sof_ring_t *r, size_t element_size, size_t capacity, sof_ring_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc);

/**
 * @brief Return the element after element, toward the newest.
 * @details Walks the ring from oldest to newest without a callback:
 * @code
 * for (e = sof_ring_peek(r); e != NULL; e = sof_ring_next(r, e))
 * @endcode
 * @param r The ring containing element.
 * @param element An element of r, as sof_ring_peek(), sof_ring_at() or
 * sof_ring_next() returned it.
 * @return The element after it, or NULL if element is the newest one,
 * element is NULL or isn't an element of r, or r is NULL or not initialized.
 */
extern void *sof_ring_next(const sof_ring_t *r, const void *element);

/**
 * @brief Return the oldest element, at the front, without removing it.
 * @param r The ring.
 * @return A pointer to the oldest element's element_size bytes in the ring,
 * or NULL if the ring is empty, or r is NULL or not initialized.
 */
extern void *sof_ring_peek(const sof_ring_t *r);

/**
 * @brief Remove the oldest element, at the front.
 * @details If element is not NULL, the oldest element's element_size bytes
 * are copied there and the caller takes them over; the del_func isn't
 * called. If element is NULL, the element goes to the del_func, if there is
 * one, and is dropped.
 * @param r The ring.
 * @param element Where to copy the removed element, element_size bytes, or
 * NULL to drop it. Left unchanged if nothing is removed.
 * @return SOF_RING_OK, SOF_RING_NO_MATCH if the ring is empty, or
 * SOF_RING_ERROR if r is NULL or not initialized.
 */
extern sof_ring_error_t sof_ring_pop(sof_ring_t *r, void *element);

/**
 * @brief Push a copy of an element at the back, the newest, if there is room.
 * @details element may point to an element of the ring itself, as from
 * sof_ring_peek(), to push a copy of it.
 * @param r The ring.
 * @param element The element_size bytes to copy into the ring.
 * @return SOF_RING_OK, SOF_RING_FULL if the ring holds capacity elements
 * already (it is unchanged), or SOF_RING_ERROR if element is NULL, or r is
 * NULL or not initialized.
 */
extern sof_ring_error_t sof_ring_push(sof_ring_t *r, const void *element);

/**
 * @brief Push a copy of an element at the back, replacing the oldest if the
 * ring is full.
 * @details If the ring is full, the oldest element goes to the del_func, if
 * there is one, and its slot takes the new element, which becomes the
 * newest; otherwise this is sof_ring_push(). element may point to an
 * element of the ring itself. If it is the oldest one, of a full ring, that
 * element simply becomes the newest, without going to the del_func.
 * @param r The ring.
 * @param element The element_size bytes to copy into the ring.
 * @return SOF_RING_OK, or SOF_RING_ERROR if element is NULL, or r is NULL
 * or not initialized.
 */
extern sof_ring_error_t sof_ring_push_overwrite(sof_ring_t *r, const void *element);

/**
 * @brief Visit the elements from oldest to newest.
 * @details cb is called with each element and ctx, unless it is NULL, in
 * which case nothing is visited. If cb returns a non-zero value, the
 * traversal stops and returns that value.
 * @param r The ring.
 * @param cb The callback function to be called for each element.
 * @param ctx The context pointer passed to every call of cb.
 * @return 0 if every element was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if r is NULL or not initialized.
 */
extern int sof_ring_traverse(const sof_ring_t *r, sof_ring_traverse_func_t cb, void *ctx);

#endif // SOF_RING_H
