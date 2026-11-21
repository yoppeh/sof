/**
 * @file sof_heap.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A binary-heap priority queue of fixed-size values in c.
 * @details Each element is element_size bytes, copied in by sof_heap_push() and
 * out by sof_heap_pop(). The elements are kept in one array, as a binary heap
 * ordered by a compare callback, so the smallest is always first. The array
 * doubles in size when it fills.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_HEAP_H
#define SOF_HEAP_H

#include <stddef.h>

#include "sof_alloc.h"

/**
 * @brief Error codes for the heap functions.
 */
typedef enum sof_heap_error_t {
    SOF_HEAP_OK = 0,
    SOF_HEAP_NO_MEM,
    SOF_HEAP_NO_MATCH,
    SOF_HEAP_ERROR,
} sof_heap_error_t;

/**
 * @brief Compares two elements.
 * @details Required, to order the heap. It should return a value less than 0
 * if a comes out before b, 0 if either can come out first, or a value greater
 * than 0 if a comes out after b; so the heap gives the smallest first, and a
 * reversed compare gives the largest first. a and b point to element_size
 * bytes, in the heap or in the element being pushed. It must not change them
 * or call into the heap. ctx is the ctx given to sof_heap_init().
 */
typedef int (*sof_heap_compare_func_t)(const void *a, const void *b, void *ctx);

/**
 * @brief Receives an element just before it is removed, when it isn't copied
 * out to the caller.
 * @details Called by sof_heap_clear() and sof_heap_free() for each element, in no
 * particular order, and by sof_heap_pop(), sof_heap_remove() and sof_heap_replace() when
 * they are given no place to copy the element to, so the application can free
 * what the element owns. element points to the element's element_size bytes
 * in the heap. It must not call into the heap. ctx is the ctx given to
 * sof_heap_init().
 */
typedef void (*sof_heap_element_delete_func_t)(void *element, void *ctx);

/**
 * @brief Called for each element visited by sof_heap_traverse().
 * @details element points to the element's element_size bytes in the heap,
 * which must not be changed, since that could break the heap's order. Return
 * 0 to continue, or a non-zero value to stop the traversal early. It must not
 * change the heap. ctx is the ctx given to sof_heap_traverse().
 */
typedef int (*sof_heap_traverse_func_t)(const void *element, void *ctx);

/**
 * @brief A priority queue.
 * @details The application provides the sof_heap_t (on the stack, in
 * another structure or on the heap) and sets it up with sof_heap_init(). Its
 * fields can be read, but must not be changed by the application, and neither
 * can the elements, except through sof_heap_replace() and
 * sof_heap_remove(). Nothing points into the sof_heap_t, so it can be moved
 * or copied, as long as only one copy is used afterwards.
 *
 * The elements are in one array, in heap order: each is no greater than the
 * two at twice its index plus 1 and plus 2, so the first is the smallest, but
 * the rest aren't sorted. Every change moves elements, and growing
 * reallocates the array, so a pointer to an element, from sof_heap_peek(),
 * sof_heap_next() or a callback, is good only until the heap next changes.
 *
 * Every function accepts a NULL heap, and a heap that was freed or whose
 * sof_heap_init() failed (ready is 0), as described for each.
 */
typedef struct sof_heap_t {
    /// @brief The elements, in heap order, or NULL before the first is
    /// pushed. The block has room for capacity + 1 elements: the last is
    /// where the library keeps the element it is moving.
    unsigned char *elements;
    /// @brief The size of each element in bytes.
    size_t element_size;
    /// @brief The number of elements.
    size_t count;
    /// @brief The number of elements the array has room for.
    size_t capacity;
    /// @brief Callback ordering the elements.
    sof_heap_compare_func_t cmp_func;
    /// @brief Callback letting the application free what an element owns, or
    /// NULL.
    sof_heap_element_delete_func_t del_func;
    /// @brief Application context passed to cmp_func and del_func.
    void *ctx;
    /// @brief The memory functions the array is allocated and freed with.
    sof_alloc_t alloc;
    /// @brief 1 once sof_heap_init() has succeeded, 0 when freed or failed.
    int ready;
} sof_heap_t;

/**
 * @brief Remove every element, leaving an empty heap that can still be used.
 * @details Each element goes to the del_func, if there is one, before it is
 * removed, in no particular order. The array is kept, so pushing up to
 * capacity elements again allocates nothing.
 * @param h The heap.
 * @return SOF_HEAP_OK, or SOF_HEAP_ERROR if h is NULL or not initialized.
 */
extern sof_heap_error_t sof_heap_clear(sof_heap_t *h);

/**
 * @brief Remove every element and release the heap's memory.
 * @details Each element goes to the del_func, if there is one, before it is
 * removed, in no particular order. The sof_heap_t itself isn't freed (it belongs
 * to the caller); it is left empty, and acts like a heap whose sof_heap_init()
 * failed until it is initialized again.
 * @param h The heap.
 * @return SOF_HEAP_OK, or SOF_HEAP_ERROR if h is NULL or not initialized.
 */
extern sof_heap_error_t sof_heap_free(sof_heap_t *h);

/**
 * @brief Initialize a heap in a sof_heap_t the caller provides.
 * @details The cmp_func is required; see sof_heap_compare_func_t. The del_func is
 * optional and is called with each element that is removed without being
 * copied out; see sof_heap_element_delete_func_t. ctx is passed to every call of
 * cmp_func and del_func; it can be NULL.
 * @param h The heap to initialize.
 * @param element_size The size of each element in bytes.
 * @param capacity The number of elements to allocate room for now, or 0 to
 * allocate when the first is pushed. The array doubles when it fills, so this
 * only saves reallocations.
 * @param cmp_func The compare function.
 * @param del_func The element delete function, or NULL.
 * @param ctx The context pointer passed to cmp_func and del_func.
 * @param alloc The memory functions for the array, or NULL for the standard
 * functions. The heap keeps a copy, so it can be on the stack.
 * @return SOF_HEAP_OK, SOF_HEAP_NO_MEM if room for capacity elements couldn't be
 * allocated, or SOF_HEAP_ERROR if h or cmp_func is NULL, element_size is 0, or
 * alloc has a malloc but no free; on failure the heap is left empty and not
 * initialized.
 */
extern sof_heap_error_t sof_heap_init(sof_heap_t *h, size_t element_size, size_t capacity, sof_heap_compare_func_t cmp_func, sof_heap_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc);

/**
 * @brief Return the element after element in the array.
 * @details Walks every element without a callback, in the array's order,
 * which starts with the smallest but is otherwise not sorted:
 * @code
 * for (e = sof_heap_peek(h); e != NULL; e = sof_heap_next(h, e))
 * @endcode
 * @param h The heap containing element.
 * @param element An element of h, as sof_heap_peek() or sof_heap_next() returned it.
 * @return The next element, or NULL if element is the last one, element is
 * NULL or isn't an element of h, or h is NULL or not initialized.
 */
extern const void *sof_heap_next(const sof_heap_t *h, const void *element);

/**
 * @brief Return the smallest element without removing it.
 * @param h The heap.
 * @return A pointer to the smallest element's element_size bytes in the heap,
 * good until the heap next changes, or NULL if the heap is empty, or h is
 * NULL or not initialized.
 */
extern const void *sof_heap_peek(const sof_heap_t *h);

/**
 * @brief Remove the smallest element.
 * @details If element is not NULL, the smallest element's element_size bytes
 * are copied there and the caller takes them over; the del_func isn't called.
 * If element is NULL, the element goes to the del_func, if there is one, and
 * is dropped. Of elements that compare equal, any can come out first. The
 * array never shrinks; sof_heap_free() releases it.
 * @param h The heap.
 * @param element Where to copy the removed element, element_size bytes
 * outside the heap, or NULL to drop it. Left unchanged if nothing is removed.
 * @return SOF_HEAP_OK, SOF_HEAP_NO_MATCH if the heap is empty, or SOF_HEAP_ERROR if h is
 * NULL or not initialized.
 */
extern sof_heap_error_t sof_heap_pop(sof_heap_t *h, void *element);

/**
 * @brief Push a copy of an element onto the heap.
 * @details The array is reallocated, doubling in size, if it is full, which
 * moves every element. element may point to an element of the heap itself,
 * as from sof_heap_peek(), to push a copy of it.
 * @param h The heap.
 * @param element The element_size bytes to copy into the heap.
 * @return SOF_HEAP_OK, SOF_HEAP_NO_MEM if the array couldn't grow (the heap is
 * unchanged), or SOF_HEAP_ERROR if element is NULL, or h is NULL or not
 * initialized.
 */
extern sof_heap_error_t sof_heap_push(sof_heap_t *h, const void *element);

/**
 * @brief Remove a given element, wherever it is in the heap.
 * @details element is found by its address, as sof_heap_peek(), sof_heap_next() or a
 * traversal gave it, not by comparing. If removed is not NULL, the element's
 * element_size bytes are copied there; if it is NULL, the element goes to the
 * del_func, if there is one. Changing an element's priority is a remove and
 * a push.
 * @param h The heap.
 * @param element The element to remove: a pointer to an element of h.
 * @param removed Where to copy the removed element, element_size bytes
 * outside the heap, or NULL to drop it. Left unchanged if nothing is removed.
 * @return SOF_HEAP_OK, SOF_HEAP_NO_MATCH if element is NULL or isn't an element of h,
 * or SOF_HEAP_ERROR if h is NULL or not initialized.
 */
extern sof_heap_error_t sof_heap_remove(sof_heap_t *h, const void *element, void *removed);

/**
 * @brief Remove the smallest element and push another, in one step.
 * @details Does what sof_heap_pop() and then sof_heap_push() would, but moves the
 * elements only once and never allocates. Keeping the n largest of a stream
 * is a heap of n elements and, for each one larger than sof_heap_peek(), a
 * sof_heap_replace(). element may point to an element of the heap itself.
 * @param h The heap.
 * @param element The element_size bytes to copy into the heap.
 * @param removed Where to copy the smallest element, element_size bytes
 * outside the heap, or NULL to give it to the del_func, if there is one.
 * Left unchanged if nothing is removed.
 * @return SOF_HEAP_OK, SOF_HEAP_NO_MATCH if the heap is empty (nothing is pushed), or
 * SOF_HEAP_ERROR if element is NULL, or h is NULL or not initialized.
 */
extern sof_heap_error_t sof_heap_replace(sof_heap_t *h, const void *element, void *removed);

/**
 * @brief Visit every element, in the array's order.
 * @details The smallest comes first, but the rest aren't sorted. cb is called
 * with each element and ctx, unless it is NULL, in which case nothing is
 * visited. If cb returns a non-zero value, the traversal stops and returns
 * that value.
 * @param h The heap.
 * @param cb The callback function to be called for each element.
 * @param ctx The context pointer passed to every call of cb.
 * @return 0 if every element was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if h is NULL or not initialized.
 */
extern int sof_heap_traverse(const sof_heap_t *h, sof_heap_traverse_func_t cb, void *ctx);

#endif // SOF_HEAP_H
