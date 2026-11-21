/**
 * @file sof_stack.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A last-in, first-out stack of fixed-size values in c.
 * @details Each element is element_size bytes, copied in by sof_stack_push() and
 * out by sof_stack_pop(). The elements are kept in one array, bottom first, which
 * doubles in size when it fills.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_STACK_H
#define SOF_STACK_H

#include <stddef.h>

#include "sof_alloc.h"

/**
 * @brief Error codes for the sof_stack functions.
 */
typedef enum sof_stack_error_t {
    SOF_STACK_OK = 0,
    SOF_STACK_NO_MEM,
    SOF_STACK_NO_MATCH,
    SOF_STACK_ERROR,
} sof_stack_error_t;

/**
 * @brief Receives an element just before it is removed, when it isn't copied
 * out to the caller.
 * @details Called by sof_stack_clear(), sof_stack_free() and sof_stack_truncate()
 * for each element they remove, from top to bottom, and by sof_stack_pop() when
 * it is given no place to copy the element to, so the application can free what the element owns. element points to
 * the element's element_size bytes in the stack. It must not call into the
 * stack. ctx is the ctx given to sof_stack_init().
 */
typedef void (*sof_stack_element_delete_func_t)(void *element, void *ctx);

/**
 * @brief Called for each element visited by sof_stack_traverse().
 * @details element points to the element's element_size bytes in the stack;
 * the callback may change them. Return 0 to continue, or a non-zero value to
 * stop the traversal early. It must not push onto or pop from the stack. ctx
 * is the ctx given to sof_stack_traverse().
 */
typedef int (*sof_stack_traverse_func_t)(void *element, void *ctx);

/**
 * @brief A stack.
 * @details The application provides the sof_stack_t (on the stack, in another
 * structure or on the heap) and sets it up with sof_stack_init(). Its fields can be
 * read, but must not be changed by the application. Nothing points into the
 * sof_stack_t, so it can be moved or copied, as long as only one copy is used
 * afterwards.
 *
 * The elements are in one array that is reallocated when it grows, so a
 * pointer to an element, from sof_stack_peek(), sof_stack_next() or a callback, is good
 * only until the next sof_stack_push(), or until that element is popped.
 *
 * Every function accepts a NULL stack, and a stack that was freed or whose
 * sof_stack_init() failed (ready is 0), as described for each.
 */
typedef struct sof_stack_t {
    /// @brief The elements, capacity * element_size bytes, bottom first, or
    /// NULL before the first is pushed.
    unsigned char *elements;
    /// @brief The size of each element in bytes.
    size_t element_size;
    /// @brief The number of elements.
    size_t count;
    /// @brief The number of elements the array has room for.
    size_t capacity;
    /// @brief Callback letting the application free what an element owns, or
    /// NULL.
    sof_stack_element_delete_func_t del_func;
    /// @brief Application context passed to del_func.
    void *ctx;
    /// @brief The memory functions the array is allocated and freed with.
    sof_alloc_t alloc;
    /// @brief 1 once sof_stack_init() has succeeded, 0 when freed or failed.
    int ready;
} sof_stack_t;

/**
 * @brief Remove every element, leaving an empty stack that can still be used.
 * @details Each element goes to the del_func, if there is one, before it is
 * removed, from top to bottom. The array is kept, so pushing up to capacity
 * elements again allocates nothing.
 * @param s The stack.
 * @return SOF_STACK_OK, or SOF_STACK_ERROR if s is NULL or not initialized.
 */
extern sof_stack_error_t sof_stack_clear(sof_stack_t *s);

/**
 * @brief Remove every element and release the stack's memory.
 * @details Each element goes to the del_func, if there is one, before it is
 * removed, from top to bottom. The sof_stack_t itself isn't freed (it belongs to
 * the caller); it is left empty, and acts like a stack whose sof_stack_init()
 * failed until it is initialized again.
 * @param s The stack.
 * @return SOF_STACK_OK, or SOF_STACK_ERROR if s is NULL or not initialized.
 */
extern sof_stack_error_t sof_stack_free(sof_stack_t *s);

/**
 * @brief Initialize a stack in a sof_stack_t the caller provides.
 * @details The del_func is optional and is called with each element that is
 * removed without being copied out; see sof_stack_element_delete_func_t. ctx is
 * passed to every call of del_func; it can be NULL.
 * @param s The stack to initialize.
 * @param element_size The size of each element in bytes.
 * @param capacity The number of elements to allocate room for now, or 0 to
 * allocate when the first is pushed. The array doubles when it fills, so this
 * only saves reallocations.
 * @param del_func The element delete function, or NULL.
 * @param ctx The context pointer passed to del_func.
 * @param alloc The memory functions for the array, or NULL for the standard
 * functions. The stack keeps a copy, so it can be on the stack.
 * @return SOF_STACK_OK, SOF_STACK_NO_MEM if room for capacity elements couldn't be
 * allocated, or SOF_STACK_ERROR if s is NULL, element_size is 0, or alloc has a
 * malloc but no free; on failure the stack is left empty and not
 * initialized.
 */
extern sof_stack_error_t sof_stack_init(sof_stack_t *s, size_t element_size, size_t capacity, sof_stack_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc);

/**
 * @brief Return the element below element, toward the bottom.
 * @details Walks the stack from top to bottom without a callback:
 * @code
 * for (e = sof_stack_peek(s); e != NULL; e = sof_stack_next(s, e))
 * @endcode
 * @param s The stack containing element.
 * @param element An element of s, as sof_stack_peek() or sof_stack_next() returned it.
 * @return The element below, or NULL if element is the bottom one, element is
 * NULL or isn't an element of s, or s is NULL or not initialized.
 */
extern void *sof_stack_next(const sof_stack_t *s, const void *element);

/**
 * @brief Return the element at the top of the stack without removing it.
 * @param s The stack.
 * @return A pointer to the top element's element_size bytes in the stack,
 * good until the next sof_stack_push() or until it is popped, or NULL if the stack
 * is empty, or s is NULL or not initialized.
 */
extern void *sof_stack_peek(const sof_stack_t *s);

/**
 * @brief Remove the element at the top of the stack.
 * @details If element is not NULL, the top element's element_size bytes are
 * copied there and the caller takes them over; the del_func isn't called. If
 * element is NULL, the element goes to the del_func, if there is one, and is
 * dropped. The array never shrinks; sof_stack_free() releases it.
 * @param s The stack.
 * @param element Where to copy the removed element, element_size bytes, or
 * NULL to drop it. Left unchanged if nothing is removed.
 * @return SOF_STACK_OK, SOF_STACK_NO_MATCH if the stack is empty, or SOF_STACK_ERROR if s is
 * NULL or not initialized.
 */
extern sof_stack_error_t sof_stack_pop(sof_stack_t *s, void *element);

/**
 * @brief Push a copy of an element onto the top of the stack.
 * @details The array is reallocated, doubling in size, if it is full, which
 * moves every element. element may point to an element of the stack itself,
 * as from sof_stack_peek(), to push a copy of it.
 * @param s The stack.
 * @param element The element_size bytes to copy onto the stack.
 * @return SOF_STACK_OK, SOF_STACK_NO_MEM if the array couldn't grow (the stack is
 * unchanged), or SOF_STACK_ERROR if element is NULL, or s is NULL or not
 * initialized.
 */
extern sof_stack_error_t sof_stack_push(sof_stack_t *s, const void *element);

/**
 * @brief Visit the elements from top to bottom.
 * @details cb is called with each element and ctx, unless it is NULL, in
 * which case nothing is visited. If cb returns a non-zero value, the
 * traversal stops and returns that value.
 * @param s The stack.
 * @param cb The callback function to be called for each element.
 * @param ctx The context pointer passed to every call of cb.
 * @return 0 if every element was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if s is NULL or not initialized.
 */
extern int sof_stack_traverse(const sof_stack_t *s, sof_stack_traverse_func_t cb, void *ctx);

/**
 * @brief Pop elements until the stack holds count of them.
 * @details Each element removed goes to the del_func, if there is one, from
 * top to bottom, as sof_stack_pop() with no place to copy it would. The
 * array never shrinks. A count at or above the number of elements removes
 * nothing; a count of 0 empties the stack, as sof_stack_clear() does.
 * @param s The stack.
 * @param count The number of elements to leave, counted from the bottom.
 * @return SOF_STACK_OK, or SOF_STACK_ERROR if s is NULL or not initialized.
 */
extern sof_stack_error_t sof_stack_truncate(sof_stack_t *s, size_t count);

#endif // SOF_STACK_H
