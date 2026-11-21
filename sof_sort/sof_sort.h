/**
 * @file sof_sort.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Sorting algorithms for arrays of any type in c.
 * @details Each function sorts an array of count elements of size bytes in
 * place, in the order given by a compare function, as qsort() does, but with
 * a ctx pointer passed to every compare. Bubble, insertion and merge sort are
 * stable (equal elements keep their order); heap and quick sort aren't. Only
 * merge sort allocates, through a sof_alloc_t. sof_sort_search(),
 * sof_sort_lower_bound() and sof_sort_upper_bound() find elements in an
 * array sorted with the same kind of compare function, by binary search.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_SORT_H
#define SOF_SORT_H

#include <stddef.h>

#include "sof_alloc.h"

/**
 * @brief The error codes the sort functions return.
 */
typedef enum sof_sort_error_t {
    /// @brief The array is sorted.
    SOF_SORT_OK = 0,
    /// @brief Memory couldn't be allocated; the array is unchanged.
    SOF_SORT_NO_MEM,
    /// @brief Not returned by any function; kept so the codes match the
    /// other libraries'.
    SOF_SORT_NO_MATCH,
    /// @brief An argument is invalid; the array is unchanged.
    SOF_SORT_ERROR,
} sof_sort_error_t;

/**
 * @brief Compares two elements of the array being sorted or searched.
 * @details Called with pointers to two elements, in the array or in merge
 * sort's buffer. The search functions always pass an element of the array as
 * a and the key as b, so the key may be of another type if the function
 * knows that. It must not change them or the array.
 * @param a The first element.
 * @param b The second element, or the key being searched for.
 * @param ctx The ctx given to the sort or search function.
 * @return Less than 0 if a sorts before b, 0 if they are equal, greater than
 * 0 if a sorts after b. The order must be consistent, or the array is left
 * in some unspecified order (but no memory outside it is touched).
 */
typedef int (*sof_sort_compare_func_t)(const void *a, const void *b, void *ctx);

/**
 * @brief Sorts an array with bubble sort.
 * @details Stable. O(n^2) compares and swaps, O(n) on an array that is
 * already sorted. For comparison and for tiny arrays only.
 * @param base The array. May be NULL if count is 0.
 * @param count The number of elements.
 * @param size The size of each element in bytes.
 * @param cmp_func Compares two elements.
 * @param ctx Passed to every call of cmp_func; may be NULL.
 * @return SOF_SORT_OK, or SOF_SORT_ERROR if cmp_func is NULL, size is 0, or base is
 * NULL and count isn't 0.
 */
extern sof_sort_error_t sof_sort_bubble(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx);

/**
 * @brief Sorts an array with heap sort.
 * @details Not stable. O(n log n) compares in every case, and no memory
 * beyond the array.
 * @param base The array. May be NULL if count is 0.
 * @param count The number of elements.
 * @param size The size of each element in bytes.
 * @param cmp_func Compares two elements.
 * @param ctx Passed to every call of cmp_func; may be NULL.
 * @return SOF_SORT_OK, or SOF_SORT_ERROR if cmp_func is NULL, size is 0, or base is
 * NULL and count isn't 0.
 */
extern sof_sort_error_t sof_sort_heap(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx);

/**
 * @brief Sorts an array with insertion sort.
 * @details Stable. O(n^2) compares and swaps, O(n) on an array that is
 * already sorted, and fast for small arrays.
 * @param base The array. May be NULL if count is 0.
 * @param count The number of elements.
 * @param size The size of each element in bytes.
 * @param cmp_func Compares two elements.
 * @param ctx Passed to every call of cmp_func; may be NULL.
 * @return SOF_SORT_OK, or SOF_SORT_ERROR if cmp_func is NULL, size is 0, or base is
 * NULL and count isn't 0.
 */
extern sof_sort_error_t sof_sort_insertion(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx);

/**
 * @brief Finds where a key belongs in a sorted array: the first element not
 * less than it.
 * @details A binary search, taking at most ceil(log2(count + 1)) compares.
 * The array must be sorted in the order cmp_func gives, or at least
 * partitioned: every element less than key before every other. Each compare
 * is cmp_func(element, key, ctx). The result is where key would be inserted
 * to keep the array sorted, before any elements equal to it; with
 * sof_sort_upper_bound(), [lower, upper) is the range of elements equal to
 * key.
 * @param base The sorted array. May be NULL if count is 0.
 * @param count The number of elements.
 * @param size The size of each element in bytes.
 * @param key What to look for, passed as cmp_func's b.
 * @param cmp_func Compares an element with key.
 * @param ctx Passed to every call of cmp_func; may be NULL.
 * @return The index of the first element for which cmp_func returns 0 or
 * more, from 0 to count; count if every element is less than key, including
 * when count is 0. 0, without calling cmp_func, if cmp_func or key is NULL,
 * size is 0, or base is NULL and count isn't 0.
 */
extern size_t sof_sort_lower_bound(const void *base, size_t count, size_t size, const void *key, sof_sort_compare_func_t cmp_func, void *ctx);

/**
 * @brief Sorts an array with merge sort.
 * @details Stable. O(n log n) compares in every case. Allocates a buffer as
 * large as the array (count * size bytes) for the time of the call, unless
 * count is 0 or 1.
 * @param base The array. May be NULL if count is 0.
 * @param count The number of elements.
 * @param size The size of each element in bytes.
 * @param cmp_func Compares two elements.
 * @param ctx Passed to every call of cmp_func; may be NULL.
 * @param alloc The allocator for the buffer, or NULL for the standard
 * functions.
 * @return SOF_SORT_OK; SOF_SORT_NO_MEM if the buffer couldn't be allocated or
 * count * size overflows, leaving the array unchanged; or SOF_SORT_ERROR if
 * cmp_func is NULL, size is 0, base is NULL and count isn't 0, or
 * sof_alloc_valid() rejects alloc.
 */
extern sof_sort_error_t sof_sort_merge(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx, const sof_alloc_t *alloc);

/**
 * @brief Sorts an array with quicksort.
 * @details Not stable. Pattern-defeating quicksort: partitions around the
 * median of three elements, sorts small partitions with insertion sort,
 * finishes partitions that needed no swaps with insertion sort if that is
 * quick, sets aside runs equal to the previous pivot in one pass, and
 * switches to heap sort for any partition after too many badly unbalanced
 * ones, so it takes O(n log n) compares in every case, about 2 or 3 per
 * element on sorted, reversed or all-equal input, and O(log n) stack.
 * The fastest here or close to it on most input; input made of a few long
 * runs in order, such as organ pipe, isn't recognized, and
 * sof_sort_merge() sorts it three to four times as fast.
 * @param base The array. May be NULL if count is 0.
 * @param count The number of elements.
 * @param size The size of each element in bytes.
 * @param cmp_func Compares two elements.
 * @param ctx Passed to every call of cmp_func; may be NULL.
 * @return SOF_SORT_OK, or SOF_SORT_ERROR if cmp_func is NULL, size is 0, or base is
 * NULL and count isn't 0.
 */
extern sof_sort_error_t sof_sort_quick(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx);

/**
 * @brief Finds an element equal to a key in a sorted array.
 * @details A binary search, as sof_sort_lower_bound(), plus one compare. If
 * several elements are equal to key, it finds the first of them, so the
 * result doesn't depend on how the search goes.
 * @param base The sorted array. May be NULL if count is 0.
 * @param count The number of elements.
 * @param size The size of each element in bytes.
 * @param key What to look for, passed as cmp_func's b.
 * @param cmp_func Compares an element with key.
 * @param ctx Passed to every call of cmp_func; may be NULL.
 * @return A pointer to the first element for which cmp_func returns 0, or
 * NULL if there is none, count is 0, or the arguments are those
 * sof_sort_lower_bound() rejects. As with bsearch(), the pointer isn't
 * const, since the array is the caller's.
 */
extern void *sof_sort_search(const void *base, size_t count, size_t size, const void *key, sof_sort_compare_func_t cmp_func, void *ctx);

/**
 * @brief Finds the first element greater than a key in a sorted array.
 * @details As sof_sort_lower_bound(), but past any elements equal to key:
 * where key would be inserted after its equals, or, in an array of running
 * totals, the first total above a value.
 * @param base The sorted array. May be NULL if count is 0.
 * @param count The number of elements.
 * @param size The size of each element in bytes.
 * @param key What to look for, passed as cmp_func's b.
 * @param cmp_func Compares an element with key.
 * @param ctx Passed to every call of cmp_func; may be NULL.
 * @return The index of the first element for which cmp_func returns more
 * than 0, from 0 to count; count if there is none. 0, without calling
 * cmp_func, for the arguments sof_sort_lower_bound() rejects.
 */
extern size_t sof_sort_upper_bound(const void *base, size_t count, size_t size, const void *key, sof_sort_compare_func_t cmp_func, void *ctx);

#endif // SOF_SORT_H
