/**
 * @file sof_queue.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A first-in, first-out queue in c.
 * @details A singly linked list of elements, each holding a void * the
 * library never reads. Elements are added at the tail and taken from the
 * head. A dequeued element isn't freed but kept as a spare, and the next
 * enqueue reuses it, so a queue that stays about the same size allocates
 * nothing once it has grown. Spares are freed by sof_queue_shrink(),
 * sof_queue_clear() and sof_queue_free().
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_QUEUE_H
#define SOF_QUEUE_H

#include <stddef.h>

#include "sof_alloc.h"

/**
 * @brief Error codes for the queue functions.
 */
typedef enum sof_queue_error_t {
    SOF_QUEUE_OK = 0,
    SOF_QUEUE_NO_MEM,
    SOF_QUEUE_NO_MATCH,
    SOF_QUEUE_ERROR,
} sof_queue_error_t;

/**
 * @brief One element of a queue.
 * @details data is the application's, and is never read or written by the
 * library. next must not be changed by the application. Each element is
 * allocated separately and never moves, so a pointer to one stays good until
 * it is removed. After that the queue may reuse it for a later enqueue.
 */
typedef struct sof_queue_element_t {
    /// @brief The element after this one, toward the tail, or NULL.
    struct sof_queue_element_t *next;
    /// @brief Application data.
    void *data;
} sof_queue_element_t;

/**
 * @brief Receives an element just before it is removed, when its data isn't
 * handed back to the caller.
 * @details Called by sof_queue_clear() and sof_queue_free() for each element, and by
 * sof_queue_dequeue() when it is given no place to put the data, so the
 * application can free the element's data. It must not call into the queue.
 * ctx is the ctx given to sof_queue_init().
 */
typedef void (*sof_queue_element_delete_func_t)(sof_queue_element_t *element, void *ctx);

/**
 * @brief Called for each element visited by sof_queue_traverse().
 * @details Return 0 to continue, or a non-zero value to stop the traversal
 * early. It must not add to or remove from the queue. ctx is the ctx given
 * to sof_queue_traverse().
 */
typedef int (*sof_queue_traverse_func_t)(sof_queue_element_t *element, void *ctx);

/**
 * @brief A queue.
 * @details The application provides the sof_queue_t (on the stack, in another
 * structure or on the heap) and sets it up with sof_queue_init(). Its fields can
 * be read, but must not be changed by the application. Nothing points into
 * the sof_queue_t, so it can be moved or copied, as long as only one copy is used
 * afterwards.
 *
 * Every function accepts a NULL queue, and a queue that was freed or whose
 * sof_queue_init() failed (ready is 0), as described for each.
 */
typedef struct sof_queue_t {
    /// @brief The oldest element, the next to be dequeued, or NULL.
    sof_queue_element_t *head;
    /// @brief The newest element, or NULL.
    sof_queue_element_t *tail;
    /// @brief The number of elements.
    size_t count;
    /// @brief Dequeued elements kept for reuse by sof_queue_enqueue(), linked
    /// through next, or NULL.
    sof_queue_element_t *spare;
    /// @brief The number of elements in spare.
    size_t spare_count;
    /// @brief Callback letting the application free an element's data, or
    /// NULL.
    sof_queue_element_delete_func_t del_func;
    /// @brief Application context passed to del_func.
    void *ctx;
    /// @brief The memory functions elements are allocated and freed with.
    sof_alloc_t alloc;
    /// @brief 1 once sof_queue_init() has succeeded, 0 when freed or failed.
    int ready;
} sof_queue_t;

/**
 * @brief Remove every element, leaving an empty queue that can still be used.
 * @details Each element goes to the del_func, if there is one, before it is
 * removed, from head to tail. The elements and any spares are freed.
 * @param q The queue.
 * @return SOF_QUEUE_OK, or SOF_QUEUE_ERROR if q is NULL or not initialized.
 */
extern sof_queue_error_t sof_queue_clear(sof_queue_t *q);

/**
 * @brief Remove the element at the head of the queue.
 * @details If data is not NULL, the element's data is stored there and the
 * caller takes it over; the del_func isn't called. If data is NULL, the
 * element goes to the del_func, if there is one, and is dropped. Either way
 * the element is kept as a spare for the next sof_queue_enqueue() rather than
 * freed, so dequeuing never frees memory; see sof_queue_shrink().
 * @param q The queue.
 * @param data Where to put the removed element's data, or NULL to drop it.
 * Left unchanged if nothing is removed.
 * @return SOF_QUEUE_OK, SOF_QUEUE_NO_MATCH if the queue is empty, or SOF_QUEUE_ERROR if q
 * is NULL or not initialized.
 */
extern sof_queue_error_t sof_queue_dequeue(sof_queue_t *q, void **data);

/**
 * @brief Add an element at the tail of the queue.
 * @details The element is a spare left by sof_queue_dequeue() if there is
 * one, and is allocated otherwise.
 * @param q The queue.
 * @param data The new element's data. It can be NULL.
 * @return SOF_QUEUE_OK, SOF_QUEUE_NO_MEM if the element couldn't be allocated (the
 * queue is unchanged), or SOF_QUEUE_ERROR if q is NULL or not initialized.
 */
extern sof_queue_error_t sof_queue_enqueue(sof_queue_t *q, void *data);

/**
 * @brief Remove every element and release the queue's memory.
 * @details Each element goes to the del_func, if there is one, before it is
 * removed. The sof_queue_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like a queue whose sof_queue_init() failed until it is
 * initialized again.
 * @param q The queue.
 * @return SOF_QUEUE_OK, or SOF_QUEUE_ERROR if q is NULL or not initialized.
 */
extern sof_queue_error_t sof_queue_free(sof_queue_t *q);

/**
 * @brief Initialize a queue in a sof_queue_t the caller provides.
 * @details The del_func is optional and is called with each element that is
 * removed without its data being handed back; see
 * sof_queue_element_delete_func_t. ctx is passed to every call of del_func; it
 * can be NULL.
 * @param q The queue to initialize.
 * @param del_func The element delete function, or NULL.
 * @param ctx The context pointer passed to del_func.
 * @param alloc The memory functions for elements, or NULL for the standard
 * functions. The queue keeps a copy, so it can be on the stack.
 * @return SOF_QUEUE_OK, or SOF_QUEUE_ERROR if q is NULL, or alloc has a malloc but no
 * free, in which case the queue is left empty and not initialized.
 */
extern sof_queue_error_t sof_queue_init(sof_queue_t *q, sof_queue_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc);

/**
 * @brief Return the element after element, toward the tail.
 * @details Walks the queue from oldest to newest without a callback:
 * @code
 * for (e = sof_queue_peek(q); e != NULL; e = sof_queue_next(q, e))
 * @endcode
 * Dequeuing the current element while walking invalidates it, so get the
 * next one first.
 * @param q The queue containing element.
 * @param element The element to start from.
 * @return The next element, or NULL if element is the tail, element is NULL,
 * or q is NULL or not initialized.
 */
extern sof_queue_element_t *sof_queue_next(const sof_queue_t *q, const sof_queue_element_t *element);

/**
 * @brief Return the element at the head of the queue without removing it.
 * @param q The queue.
 * @return The head element, the next to be dequeued, or NULL if the queue is
 * empty, or q is NULL or not initialized.
 */
extern sof_queue_element_t *sof_queue_peek(const sof_queue_t *q);

/**
 * @brief Free the spare elements kept for reuse.
 * @details sof_queue_dequeue() keeps each element it removes, so a queue
 * holds memory for the most elements it has held at once until it is
 * cleared, freed or shrunk. Call this after a burst to give that memory back.
 * The elements in the queue are untouched, and the del_func isn't called.
 * @param q The queue.
 * @return SOF_QUEUE_OK, or SOF_QUEUE_ERROR if q is NULL or not initialized.
 */
extern sof_queue_error_t sof_queue_shrink(sof_queue_t *q);

/**
 * @brief Visit the elements from head to tail.
 * @details cb is called with each element and ctx, unless it is NULL, in
 * which case nothing is visited. If cb returns a non-zero value, the
 * traversal stops and returns that value.
 * @param q The queue.
 * @param cb The callback function to be called for each element.
 * @param ctx The context pointer passed to every call of cb.
 * @return 0 if every element was visited, the callback's non-zero value if
 * it stopped the traversal, or -1 if q is NULL or not initialized.
 */
extern int sof_queue_traverse(const sof_queue_t *q, sof_queue_traverse_func_t cb, void *ctx);

#endif // SOF_QUEUE_H
