/**
 * @file sof_vector.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A growable array of fixed-size values in c.
 * @details Each element is element_size bytes, copied in by sof_vector_push()
 * and sof_vector_insert() and reached by index with sof_vector_at(). The
 * elements are kept in order in one array, which doubles in size when it
 * fills. Vectors can be searched in order or, once sorted through sof_sort,
 * by binary search.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_VECTOR_H
#define SOF_VECTOR_H

#include <stddef.h>

#include "sof_alloc.h"

/**
 * @brief Error codes for the vector functions.
 */
typedef enum sof_vector_error_t {
    SOF_VECTOR_OK = 0,
    SOF_VECTOR_NO_MEM,
    SOF_VECTOR_NO_MATCH,
    SOF_VECTOR_ERROR,
} sof_vector_error_t;

/**
 * @brief Compares two elements, or a key with an element.
 * @details Used by sof_vector_sort() and sof_vector_sort_stable(), which
 * pass two elements, and by sof_vector_find() and sof_vector_search(), which
 * pass the key they were given as a and an element as b. It should return a
 * value less than 0 if a sorts before b, 0 if they are equal, or a value
 * greater than 0 if a sorts after b. It must not change the elements or call
 * into the vector. ctx is the ctx given to the function that calls it. It
 * has the same type as sof_sort_compare_func_t, so one function serves both.
 */
typedef int (*sof_vector_compare_func_t)(const void *a, const void *b, void *ctx);

/**
 * @brief Receives an element just before it is removed, when it isn't copied
 * out to the caller.
 * @details Called by sof_vector_clear(), sof_vector_free() and
 * sof_vector_resize() for each element they remove, in order from first to
 * last, and by sof_vector_pop(), sof_vector_remove() and
 * sof_vector_swap_remove() when they are given no place to copy the element
 * to, so the application can free what the element owns. element points to
 * the element's element_size bytes in the vector. It must not call into the
 * vector. ctx is the ctx given to sof_vector_init().
 */
typedef void (*sof_vector_element_delete_func_t)(void *element, void *ctx);

/**
 * @brief Called for each element visited by sof_vector_traverse().
 * @details element points to the element's element_size bytes in the vector;
 * the callback may change them. Return 0 to continue, or a non-zero value to
 * stop the traversal early. It must not add elements to or remove them from
 * the vector. ctx is the ctx given to sof_vector_traverse().
 */
typedef int (*sof_vector_traverse_func_t)(void *element, void *ctx);

/**
 * @brief A vector.
 * @details The application provides the sof_vector_t (on the stack, in
 * another structure or on the heap) and sets it up with sof_vector_init().
 * Its fields can be read, but must not be changed by the application; the
 * elements, count of them from elements, can be read and changed freely.
 * Nothing points into the sof_vector_t, so it can be moved or copied, as long
 * as only one copy is used afterwards.
 *
 * The elements are in one array that is reallocated when it grows or
 * shrinks, and inserting or removing moves the elements after the place it
 * happens. So a pointer to an element, from sof_vector_at(),
 * sof_vector_find() or a callback, is good only until the next function that
 * adds or removes elements or changes the capacity; an index stays good until
 * elements before it are added or removed.
 *
 * Every function accepts a NULL vector, and a vector that was freed or whose
 * sof_vector_init() failed (ready is 0), as described for each.
 *
 * sof_vector_at(), sof_vector_find(), sof_vector_next(),
 * sof_vector_search() and sof_vector_traverse() only read the vector, and
 * take a const vector. The elements they return, or pass to the callback,
 * are not const, since the application may change them freely.
 */
typedef struct sof_vector_t {
    /// @brief The elements, capacity * element_size bytes, first to last, or
    /// NULL while the capacity is 0.
    unsigned char *elements;
    /// @brief The size of each element in bytes.
    size_t element_size;
    /// @brief The number of elements.
    size_t count;
    /// @brief The number of elements the array has room for.
    size_t capacity;
    /// @brief Callback letting the application free what an element owns, or
    /// NULL.
    sof_vector_element_delete_func_t del_func;
    /// @brief Application context passed to del_func.
    void *ctx;
    /// @brief The memory functions the array is allocated and freed with.
    sof_alloc_t alloc;
    /// @brief 1 once sof_vector_init() has succeeded, 0 when freed or failed.
    int ready;
} sof_vector_t;

/**
 * @brief Return the element at an index.
 * @param v The vector.
 * @param index The element's index, from 0.
 * @return A pointer to the element's element_size bytes in the vector, which
 * the caller may change, good until elements are next added or removed; or
 * NULL if index isn't less than count, or v is NULL or not initialized.
 */
extern void *sof_vector_at(const sof_vector_t *v, size_t index);

/**
 * @brief Remove every element, leaving an empty vector that can still be
 * used.
 * @details Each element goes to the del_func, if there is one, before it is
 * removed, from first to last. The array is kept, so adding up to capacity
 * elements again allocates nothing; sof_vector_shrink() releases it.
 * @param v The vector.
 * @return SOF_VECTOR_OK, or SOF_VECTOR_ERROR if v is NULL or not
 * initialized.
 */
extern sof_vector_error_t sof_vector_clear(sof_vector_t *v);

/**
 * @brief Find the first element equal to a key, looking at each in turn.
 * @details cmp_func is called with key and each element, from the first,
 * until it returns 0. The vector needn't be sorted; for a sorted vector,
 * sof_vector_search() is faster.
 * @param v The vector.
 * @param key The key, passed to cmp_func as its first argument. It can be an
 * element, or anything cmp_func knows how to compare with one.
 * @param cmp_func Compares key with an element.
 * @param ctx The context pointer passed to every call of cmp_func.
 * @param index If not NULL, set to the index of the element found. Left
 * unchanged if none is.
 * @return The first element equal to key, or NULL if there is none, key or
 * cmp_func is NULL, or v is NULL or not initialized.
 */
extern void *sof_vector_find(const sof_vector_t *v, const void *key, sof_vector_compare_func_t cmp_func, void *ctx, size_t *index);

/**
 * @brief Remove every element and release the vector's memory.
 * @details Each element goes to the del_func, if there is one, before it is
 * removed, from first to last. The sof_vector_t itself isn't freed (it
 * belongs to the caller); it is left empty, and acts like a vector whose
 * sof_vector_init() failed until it is initialized again.
 * @param v The vector.
 * @return SOF_VECTOR_OK, or SOF_VECTOR_ERROR if v is NULL or not
 * initialized.
 */
extern sof_vector_error_t sof_vector_free(sof_vector_t *v);

/**
 * @brief Initialize a vector in a sof_vector_t the caller provides.
 * @details The del_func is optional and is called with each element that is
 * removed without being copied out; see sof_vector_element_delete_func_t.
 * ctx is passed to every call of del_func; it can be NULL.
 * @param v The vector to initialize.
 * @param element_size The size of each element in bytes.
 * @param capacity The number of elements to allocate room for now, or 0 to
 * allocate when the first is added. The array doubles when it fills, so this
 * only saves reallocations.
 * @param del_func The element delete function, or NULL.
 * @param ctx The context pointer passed to del_func.
 * @param alloc The memory functions for the array and for
 * sof_vector_sort_stable()'s buffer, or NULL for the standard functions. The
 * vector keeps a copy, so it can be on the stack.
 * @return SOF_VECTOR_OK, SOF_VECTOR_NO_MEM if room for capacity elements
 * couldn't be allocated, or SOF_VECTOR_ERROR if v is NULL, element_size is 0,
 * or alloc has a malloc but no free; on failure the vector is left empty and
 * not initialized.
 */
extern sof_vector_error_t sof_vector_init(sof_vector_t *v, size_t element_size, size_t capacity, sof_vector_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc);

/**
 * @brief Insert a copy of an element before the element at an index.
 * @details The elements from index on move up one place. The array is
 * reallocated, doubling in size, if it is full, which moves every element.
 * element may point to an element of the vector itself, as from
 * sof_vector_at(), to insert a copy of it.
 * @param v The vector.
 * @param index Where the new element goes, from 0 to count; count appends it.
 * @param element The element_size bytes to copy into the vector.
 * @return SOF_VECTOR_OK, SOF_VECTOR_NO_MEM if the array couldn't grow (the
 * vector is unchanged), or SOF_VECTOR_ERROR if index is greater than count,
 * element is NULL, or v is NULL or not initialized.
 */
extern sof_vector_error_t sof_vector_insert(sof_vector_t *v, size_t index, const void *element);

/**
 * @brief Return the element after element.
 * @details Walks the vector from first to last without a callback:
 * @code
 * for (e = sof_vector_at(v, 0); e != NULL; e = sof_vector_next(v, e))
 * @endcode
 * @param v The vector containing element.
 * @param element An element of v, as sof_vector_at() or sof_vector_next()
 * returned it.
 * @return The next element, or NULL if element is the last one, element is
 * NULL or isn't the start of an element of v, or v is NULL or not
 * initialized.
 */
extern void *sof_vector_next(const sof_vector_t *v, const void *element);

/**
 * @brief Remove the last element.
 * @details If element is not NULL, the last element's element_size bytes are
 * copied there and the caller takes them over; the del_func isn't called. If
 * element is NULL, the element goes to the del_func, if there is one, and is
 * dropped. The array doesn't shrink; sof_vector_shrink() does that.
 * @param v The vector.
 * @param element Where to copy the removed element, element_size bytes
 * outside the vector, or NULL to drop it. Left unchanged if nothing is
 * removed.
 * @return SOF_VECTOR_OK, SOF_VECTOR_NO_MATCH if the vector is empty, or
 * SOF_VECTOR_ERROR if v is NULL or not initialized.
 */
extern sof_vector_error_t sof_vector_pop(sof_vector_t *v, void *element);

/**
 * @brief Append a copy of an element after the last.
 * @details When there is room the element is copied straight into the next
 * slot. The array is reallocated, doubling in size, if it is full, which
 * moves every element. element may point to an element of the vector itself,
 * as from sof_vector_at(), to append a copy of it.
 * @param v The vector.
 * @param element The element_size bytes to copy into the vector.
 * @return SOF_VECTOR_OK, SOF_VECTOR_NO_MEM if the array couldn't grow (the
 * vector is unchanged), or SOF_VECTOR_ERROR if element is NULL, or v is NULL
 * or not initialized.
 */
extern sof_vector_error_t sof_vector_push(sof_vector_t *v, const void *element);

/**
 * @brief Remove the element at an index, keeping the others in order.
 * @details The elements after index move down one place. If removed is not
 * NULL, the element's element_size bytes are copied there; if it is NULL,
 * the element goes to the del_func, if there is one. sof_vector_swap_remove()
 * is faster when the order doesn't matter.
 * @param v The vector.
 * @param index The index of the element to remove.
 * @param removed Where to copy the removed element, element_size bytes
 * outside the vector, or NULL to drop it. Left unchanged if nothing is
 * removed.
 * @return SOF_VECTOR_OK, SOF_VECTOR_NO_MATCH if index isn't less than count,
 * or SOF_VECTOR_ERROR if v is NULL or not initialized.
 */
extern sof_vector_error_t sof_vector_remove(sof_vector_t *v, size_t index, void *removed);

/**
 * @brief Make room for at least capacity elements.
 * @details If the array is smaller, it is reallocated to exactly capacity
 * elements, which moves every element; adding elements up to that number
 * then allocates nothing. A capacity no greater than the current one does
 * nothing.
 * @param v The vector.
 * @param capacity The number of elements to make room for.
 * @return SOF_VECTOR_OK, SOF_VECTOR_NO_MEM if the array couldn't grow (the
 * vector is unchanged), or SOF_VECTOR_ERROR if v is NULL or not initialized.
 */
extern sof_vector_error_t sof_vector_reserve(sof_vector_t *v, size_t capacity);

/**
 * @brief Set the number of elements, adding zeroed ones or removing from the
 * end.
 * @details Growing adds elements whose bytes are all 0, reallocating the
 * array, to at least double its size, if count is more than it has room for.
 * Shrinking removes the elements from count on, each going to the del_func,
 * if there is one, from first to last; the array is kept.
 * @param v The vector.
 * @param count The number of elements the vector should have.
 * @return SOF_VECTOR_OK, SOF_VECTOR_NO_MEM if the array couldn't grow (the
 * vector is unchanged), or SOF_VECTOR_ERROR if v is NULL or not initialized.
 */
extern sof_vector_error_t sof_vector_resize(sof_vector_t *v, size_t count);

/**
 * @brief Find a key in a sorted vector by binary search.
 * @details The vector must be sorted in the order cmp_func gives, as
 * sof_vector_sort() with the same cmp_func leaves it. cmp_func is called with
 * key and an element, O(log n) times.
 * @param v The vector.
 * @param key The key, passed to cmp_func as its first argument.
 * @param cmp_func Compares key with an element.
 * @param ctx The context pointer passed to every call of cmp_func.
 * @param index If not NULL, set to the index of the first element not less
 * than key, whether or not it is equal: where key would be inserted to keep
 * the vector sorted, which is count if every element is less. Left unchanged
 * if v, key or cmp_func is NULL or v is not initialized.
 * @return The first element equal to key, or NULL if there is none, key or
 * cmp_func is NULL, or v is NULL or not initialized.
 */
extern void *sof_vector_search(const sof_vector_t *v, const void *key, sof_vector_compare_func_t cmp_func, void *ctx, size_t *index);

/**
 * @brief Release the room the array has beyond count elements.
 * @details Reallocates the array to exactly count elements, which moves every
 * element, or frees it if the vector is empty.
 * @param v The vector.
 * @return SOF_VECTOR_OK, SOF_VECTOR_NO_MEM if the array couldn't be
 * reallocated (the vector is unchanged), or SOF_VECTOR_ERROR if v is NULL or
 * not initialized.
 */
extern sof_vector_error_t sof_vector_shrink(sof_vector_t *v);

/**
 * @brief Sort the elements with sof_sort's quicksort.
 * @details Not stable: equal elements may change places. Allocates nothing.
 * @param v The vector.
 * @param cmp_func Compares two elements.
 * @param ctx The context pointer passed to every call of cmp_func.
 * @return SOF_VECTOR_OK, or SOF_VECTOR_ERROR if cmp_func is NULL, or v is
 * NULL or not initialized.
 */
extern sof_vector_error_t sof_vector_sort(sof_vector_t *v, sof_vector_compare_func_t cmp_func, void *ctx);

/**
 * @brief Sort the elements with sof_sort's merge sort, keeping equal
 * elements in order.
 * @details Allocates a buffer as large as the array, through the vector's
 * allocator, for the time of the call.
 * @param v The vector.
 * @param cmp_func Compares two elements.
 * @param ctx The context pointer passed to every call of cmp_func.
 * @return SOF_VECTOR_OK, SOF_VECTOR_NO_MEM if the buffer couldn't be
 * allocated (the vector is unchanged), or SOF_VECTOR_ERROR if cmp_func is
 * NULL, or v is NULL or not initialized.
 */
extern sof_vector_error_t sof_vector_sort_stable(sof_vector_t *v, sof_vector_compare_func_t cmp_func, void *ctx);

/**
 * @brief Remove the element at an index by moving the last element into its
 * place.
 * @details Takes the same time however long the vector is, but changes the
 * order. If removed is not NULL, the element's element_size bytes are copied
 * there; if it is NULL, the element goes to the del_func, if there is one.
 * @param v The vector.
 * @param index The index of the element to remove.
 * @param removed Where to copy the removed element, element_size bytes
 * outside the vector, or NULL to drop it. Left unchanged if nothing is
 * removed.
 * @return SOF_VECTOR_OK, SOF_VECTOR_NO_MATCH if index isn't less than count,
 * or SOF_VECTOR_ERROR if v is NULL or not initialized.
 */
extern sof_vector_error_t sof_vector_swap_remove(sof_vector_t *v, size_t index, void *removed);

/**
 * @brief Visit the elements from first to last.
 * @details cb is called with each element and ctx, unless it is NULL, in
 * which case nothing is visited. If cb returns a non-zero value, the
 * traversal stops and returns that value.
 * @param v The vector.
 * @param cb The callback function to be called for each element.
 * @param ctx The context pointer passed to every call of cb.
 * @return 0 if every element was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if v is NULL or not initialized.
 */
extern int sof_vector_traverse(const sof_vector_t *v, sof_vector_traverse_func_t cb, void *ctx);

#endif // SOF_VECTOR_H
