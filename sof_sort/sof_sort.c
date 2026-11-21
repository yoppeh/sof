/**
 * @file sof_sort.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Sorting algorithms for arrays of any type in c.
 * @details Elements are handled as size bytes each, moved with memcpy() and
 * swap(), and compared only through the application's compare function.
 * Elements of 4 and 8 bytes are moved as one word, through memcpy(), so the
 * array needn't be aligned. Quicksort is pattern-defeating quicksort
 * (Orson Peters' pdqsort): a median-of-three pivot, a check for partitions
 * that were already in order, runs equal to the previous pivot set aside,
 * and heap sort once too many partitions have gone badly. (pdqsort's
 * ninther pivot was tried, and was no faster on any input measured.)
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_sort.h"

/// @brief Partitions and merge sort runs this small are finished with
/// insertion sort, which is faster on them.
#define INSERTION_CUTOFF 16

/// @brief After a badly unbalanced partition, sides larger than this have
/// six elements swapped to break up patterns, and smaller ones two.
#define BREAK_MORE_THRESHOLD 128

/// @brief After a partition that swapped nothing, insertion sort tries to
/// finish each side, giving up once it has moved elements this many places.
#define PARTIAL_INSERTION_LIMIT 8

/// @brief What every internal function needs to know about the sort.
typedef struct sorter_t {
    /// @brief The size of each element in bytes.
    size_t size;
    /// @brief The application's compare function.
    sof_sort_compare_func_t cmp_func;
    /// @brief Passed to every call of cmp_func.
    void *ctx;
} sorter_t;

static int args_valid(const void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func);
static size_t bound(const void *base, size_t count, size_t size, const void *key, sof_sort_compare_func_t cmp_func, void *ctx, int upper);
static void break_patterns(unsigned char *a, size_t n, size_t size);
static void bubble_swap(unsigned char *a, unsigned char *b, size_t size);
static void heap_sort(const sorter_t *s, unsigned char *a, size_t n);
static void insertion_sort(const sorter_t *s, unsigned char *a, size_t n);
static inline void merge_runs(const sorter_t *s, const unsigned char *left, const unsigned char *left_end, const unsigned char *right, const unsigned char *right_end, unsigned char *out, size_t size);
static void merge_sort(const sorter_t *s, unsigned char *src, unsigned char *dst, size_t n);
static int partial_insertion_sort(const sorter_t *s, unsigned char *a, size_t n);
static size_t partition_left(const sorter_t *s, unsigned char *a, size_t n);
static size_t partition_right(const sorter_t *s, unsigned char *a, size_t n, int *already);
static void quick_sort(const sorter_t *s, unsigned char *a, size_t n, size_t bad_allowed, int leftmost);
static void sift_down(const sorter_t *s, unsigned char *a, size_t root, size_t n);
static inline void sort3(const sorter_t *s, unsigned char *x, unsigned char *y, unsigned char *z);
static inline void swap(unsigned char *a, unsigned char *b, size_t size);

sof_sort_error_t sof_sort_bubble(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx) {
    if (!args_valid(base, count, size, cmp_func)) {
        return SOF_SORT_ERROR;
    }
    unsigned char *a = base;
    // Everything from the last swap of a pass onwards is in place, so the
    // next pass stops there.
    size_t end = count;
    while (end > 1) {
        size_t last = 0;
        for (size_t i = 1; i < end; i++) {
            unsigned char *p = a + (i - 1) * size;
            if (cmp_func(p, p + size, ctx) > 0) {
                bubble_swap(p, p + size, size);
                last = i;
            }
        }
        end = last;
    }
    return SOF_SORT_OK;
}

sof_sort_error_t sof_sort_heap(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx) {
    if (!args_valid(base, count, size, cmp_func)) {
        return SOF_SORT_ERROR;
    }
    sorter_t s = {size, cmp_func, ctx};
    heap_sort(&s, base, count);
    return SOF_SORT_OK;
}

sof_sort_error_t sof_sort_insertion(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx) {
    if (!args_valid(base, count, size, cmp_func)) {
        return SOF_SORT_ERROR;
    }
    sorter_t s = {size, cmp_func, ctx};
    insertion_sort(&s, base, count);
    return SOF_SORT_OK;
}

size_t sof_sort_lower_bound(const void *base, size_t count, size_t size, const void *key, sof_sort_compare_func_t cmp_func, void *ctx) {
    return bound(base, count, size, key, cmp_func, ctx, 0);
}

sof_sort_error_t sof_sort_merge(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx, const sof_alloc_t *alloc) {
    if (!args_valid(base, count, size, cmp_func) || !sof_alloc_valid(alloc)) {
        return SOF_SORT_ERROR;
    }
    if (count < 2) {
        return SOF_SORT_OK;
    }
    if (count > SIZE_MAX / size) {
        return SOF_SORT_NO_MEM;
    }
    size_t bytes = count * size;
    unsigned char *buffer = sof_alloc_malloc(alloc, bytes);
    if (buffer == NULL) {
        return SOF_SORT_NO_MEM;
    }
    memcpy(buffer, base, bytes);
    sorter_t s = {size, cmp_func, ctx};
    merge_sort(&s, buffer, base, count);
    sof_alloc_free(alloc, buffer, bytes);
    return SOF_SORT_OK;
}

sof_sort_error_t sof_sort_quick(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx) {
    if (!args_valid(base, count, size, cmp_func)) {
        return SOF_SORT_ERROR;
    }
    // Allow log2(count) badly unbalanced partitions on any path before
    // falling back to heap sort; well-chosen pivots never need that many.
    size_t bad_allowed = 0;
    for (size_t m = count; m > 1; m >>= 1) {
        bad_allowed++;
    }
    sorter_t s = {size, cmp_func, ctx};
    quick_sort(&s, base, count, bad_allowed, 1);
    return SOF_SORT_OK;
}

void *sof_sort_search(const void *base, size_t count, size_t size, const void *key, sof_sort_compare_func_t cmp_func, void *ctx) {
    if (!args_valid(base, count, size, cmp_func) || key == NULL) {
        return NULL;
    }
    size_t i = bound(base, count, size, key, cmp_func, ctx, 0);
    if (i == count) {
        return NULL;
    }
    // Cast away const, as bsearch() does: the array is the caller's, and
    // may well be writable.
    unsigned char *element = (unsigned char *)(uintptr_t)base + i * size;
    return cmp_func(element, key, ctx) == 0 ? element : NULL;
}

size_t sof_sort_upper_bound(const void *base, size_t count, size_t size, const void *key, sof_sort_compare_func_t cmp_func, void *ctx) {
    return bound(base, count, size, key, cmp_func, ctx, 1);
}

/// @brief Returns 1 if the arguments every sort takes are usable, or 0.
static int args_valid(const void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func) {
    return cmp_func != NULL && size != 0 && (base != NULL || count == 0);
}

/// @brief Returns the index of the first of the count elements at base for
/// which cmp_func(element, key) is not below 0 (upper 0), or above 0 (upper
/// 1), or count if there is none; 0 if the arguments are unusable.
/// @details Halves the range [low, high) that holds the answer until it is
/// empty, so it takes at most ceil(log2(count + 1)) compares.
static size_t bound(const void *base, size_t count, size_t size, const void *key, sof_sort_compare_func_t cmp_func, void *ctx, int upper) {
    if (!args_valid(base, count, size, cmp_func) || key == NULL) {
        return 0;
    }
    const unsigned char *a = base;
    size_t low = 0, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int c = cmp_func(a + middle * size, key, ctx);
        if (upper ? c <= 0 : c < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

/// @brief Swaps a few elements of the n at a at fixed places a quarter of
/// the way in from each end, after a badly unbalanced partition, to break up
/// patterns that fool the pivot choice. Ranges smaller than
/// INSERTION_CUTOFF are left alone.
static void break_patterns(unsigned char *a, size_t n, size_t size) {
    if (n < INSERTION_CUTOFF) {
        return;
    }
    size_t q = n / 4;
    swap(a, a + q * size, size);
    swap(a + (n - 1) * size, a + (n - q) * size, size);
    if (n > BREAK_MORE_THRESHOLD) {
        swap(a + size, a + (q + 1) * size, size);
        swap(a + 2 * size, a + (q + 2) * size, size);
        swap(a + (n - 2) * size, a + (n - q - 1) * size, size);
        swap(a + (n - 3) * size, a + (n - q - 2) * size, size);
    }
}

/// @brief swap() for sof_sort_bubble(), kept out of line under clang.
/// @details With swap() inline, clang makes bubble sort's compare and swap
/// free of branches, so each step waits on the stores of the one before:
/// on random input of 10,000 elements that took twice as long on arm64 and
/// 1.2 times as long on x86-64, and up to twice as long on other inputs.
/// gcc is faster with it inline, by 8 to 17%.
#if defined(__clang__)
__attribute__((noinline))
#endif
static void bubble_swap(unsigned char *a, unsigned char *b, size_t size) {
    swap(a, b, size);
}

/// @brief Sorts the n elements at a with heap sort.
static void heap_sort(const sorter_t *s, unsigned char *a, size_t n) {
    if (n < 2) {
        return;
    }
    for (size_t i = n / 2; i-- > 0;) {
        sift_down(s, a, i, n);
    }
    for (size_t end = n - 1; end > 0; end--) {
        swap(a, a + end * s->size, s->size);
        sift_down(s, a, 0, end);
    }
}

/// @brief Sorts the n elements at a with insertion sort.
static void insertion_sort(const sorter_t *s, unsigned char *a, size_t n) {
    size_t size = s->size;
    for (size_t i = 1; i < n; i++) {
        unsigned char *p = a + i * size;
        while (p > a && s->cmp_func(p - size, p, s->ctx) > 0) {
            swap(p - size, p, size);
            p -= size;
        }
    }
}

/// @brief Merges the sorted runs [left, left_end) and [right, right_end)
/// into out, taking from the left run when elements are equal.
/// @details Inlined with a constant size, the copies become single loads
/// and stores; memcpy() keeps them safe on unaligned arrays.
static inline void merge_runs(const sorter_t *s, const unsigned char *left, const unsigned char *left_end, const unsigned char *right, const unsigned char *right_end, unsigned char *out, size_t size) {
    while (left < left_end && right < right_end) {
        // Take from the left unless the right is strictly less, so equal
        // elements keep their order.
        if (s->cmp_func(right, left, s->ctx) < 0) {
            memcpy(out, right, size);
            right += size;
        } else {
            memcpy(out, left, size);
            left += size;
        }
        out += size;
    }
    memcpy(out, left, (size_t)(left_end - left));
    out += left_end - left;
    memcpy(out, right, (size_t)(right_end - right));
}

/// @brief Sorts n elements into dst, using src as scratch space.
/// @details src and dst must hold the same elements on entry. Each half is
/// sorted into src, using dst as scratch, and the halves are merged into dst.
static void merge_sort(const sorter_t *s, unsigned char *src, unsigned char *dst, size_t n) {
    if (n <= INSERTION_CUTOFF) {
        insertion_sort(s, dst, n);
        return;
    }
    size_t size = s->size;
    size_t half = n / 2;
    merge_sort(s, dst, src, half);
    merge_sort(s, dst + half * size, src + half * size, n - half);
    unsigned char *left = src, *left_end = src + half * size;
    unsigned char *right = left_end, *right_end = src + n * size;
    // Already in order, as when the array was sorted to begin with.
    if (s->cmp_func(left_end - size, right, s->ctx) <= 0) {
        memcpy(dst, src, n * size);
        return;
    }
    // Constant sizes for the common element sizes, so the copies are inlined.
    if (size == sizeof(uint32_t)) {
        merge_runs(s, left, left_end, right, right_end, dst, sizeof(uint32_t));
    } else if (size == sizeof(uint64_t)) {
        merge_runs(s, left, left_end, right, right_end, dst, sizeof(uint64_t));
    } else {
        merge_runs(s, left, left_end, right, right_end, dst, size);
    }
}

/// @brief Insertion sorts the n elements at a unless that would move
/// elements more than PARTIAL_INSERTION_LIMIT places in all; returns 1 if
/// they are sorted, or 0 if it gave up, leaving them in some order.
static int partial_insertion_sort(const sorter_t *s, unsigned char *a, size_t n) {
    size_t size = s->size, moves = 0;
    for (size_t i = 1; i < n; i++) {
        if (moves > PARTIAL_INSERTION_LIMIT) {
            return 0;
        }
        unsigned char *p = a + i * size;
        while (p > a && s->cmp_func(p - size, p, s->ctx) > 0) {
            swap(p - size, p, size);
            p -= size;
            moves++;
        }
    }
    return 1;
}

/// @brief Partitions the n elements at a around the pivot a[0], which is
/// known to be no greater than any element: the elements equal to it go to
/// the front, those greater after them. Returns the index of the last
/// element equal to the pivot.
/// @details Used when the pivot equals the element before the partition,
/// which no element is less than, so the equal run is set aside in one pass
/// and never partitioned again. The bounds checks only matter if cmp_func
/// is inconsistent.
static size_t partition_left(const sorter_t *s, unsigned char *a, size_t n) {
    size_t size = s->size, i = 0, j = n;
    // The last element not greater than the pivot; a[0] stops the scan.
    while (j > 0 && s->cmp_func(a, a + (--j) * size, s->ctx) < 0) {
    }
    if (j + 1 == n) {
        while (i < j && s->cmp_func(a, a + (++i) * size, s->ctx) >= 0) {
        }
    } else {
        while (i + 1 < n && s->cmp_func(a, a + (++i) * size, s->ctx) >= 0) {
        }
    }
    while (i < j) {
        swap(a + i * size, a + j * size, size);
        while (j > 0 && s->cmp_func(a, a + (--j) * size, s->ctx) < 0) {
        }
        while (i + 1 < n && s->cmp_func(a, a + (++i) * size, s->ctx) >= 0) {
        }
    }
    swap(a, a + j * size, size);
    return j;
}

/// @brief Partitions the n elements at a around the pivot a[0]: those less
/// than it before it, the rest after. Returns the pivot's new index, and
/// sets *already to 1 if no elements had to be swapped, or 0.
/// @details Elements equal to the pivot go to the right. The bounds checks
/// only matter if cmp_func is inconsistent.
static size_t partition_right(const sorter_t *s, unsigned char *a, size_t n, int *already) {
    size_t size = s->size, i = 0, j = n;
    // The first element not less than the pivot.
    while (++i < n && s->cmp_func(a + i * size, a, s->ctx) < 0) {
    }
    // The last element less than it. If there was none before i, there
    // may be none at all, so stop at i.
    if (i == 1) {
        while (i < j && s->cmp_func(a + (--j) * size, a, s->ctx) >= 0) {
        }
    } else {
        while (j > 0 && s->cmp_func(a + (--j) * size, a, s->ctx) >= 0) {
        }
    }
    *already = i >= j;
    while (i < j) {
        swap(a + i * size, a + j * size, size);
        while (++i < n && s->cmp_func(a + i * size, a, s->ctx) < 0) {
        }
        while (j > 0 && s->cmp_func(a + (--j) * size, a, s->ctx) >= 0) {
        }
    }
    swap(a, a + (i - 1) * size, size);
    return i - 1;
}

/// @brief Sorts the n elements at a with pattern-defeating quicksort,
/// falling back to heap sort once bad_allowed badly unbalanced partitions
/// have been made on the way here.
/// @details leftmost is 0 if the element before a is part of the array: a
/// pivot from an earlier partition, no greater than any element here. If
/// the new pivot equals it, the elements equal to it are set aside with
/// partition_left(), so runs of equal elements cost one pass. A partition
/// that swapped nothing suggests sorted input, and partial_insertion_sort()
/// tries to finish both sides. Recurses into the smaller side and loops on
/// the larger, so the stack holds at most log2(n) calls.
static void quick_sort(const sorter_t *s, unsigned char *a, size_t n, size_t bad_allowed, int leftmost) {
    size_t size = s->size;
    while (n > INSERTION_CUTOFF) {
        // Move the median of the first, middle and last elements to the
        // front as the pivot, leaving the largest of them at the end.
        sort3(s, a + (n / 2) * size, a, a + (n - 1) * size);
        if (!leftmost && s->cmp_func(a - size, a, s->ctx) >= 0) {
            size_t p = partition_left(s, a, n);
            a += (p + 1) * size;
            n -= p + 1;
            continue;
        }
        int already;
        size_t p = partition_right(s, a, n, &already);
        size_t left = p, right = n - p - 1;
        unsigned char *right_start = a + (p + 1) * size;
        if (left < n / 8 || right < n / 8) {
            if (--bad_allowed == 0) {
                heap_sort(s, a, n);
                return;
            }
            break_patterns(a, left, size);
            break_patterns(right_start, right, size);
        } else if (already && partial_insertion_sort(s, a, left) && partial_insertion_sort(s, right_start, right)) {
            return;
        }
        if (left < right) {
            quick_sort(s, a, left, bad_allowed, leftmost);
            a = right_start;
            n = right;
            leftmost = 0;
        } else {
            quick_sort(s, right_start, right, bad_allowed, 0);
            n = left;
        }
    }
    insertion_sort(s, a, n);
}

/// @brief Moves the element at root down the max-heap of n elements at a
/// until neither child is greater.
static void sift_down(const sorter_t *s, unsigned char *a, size_t root, size_t n) {
    size_t size = s->size;
    // root has a child while 2 * root + 1 < n, which this tests without
    // overflowing.
    while (n >= 2 && root <= (n - 2) / 2) {
        size_t child = 2 * root + 1;
        unsigned char *c = a + child * size;
        if (child + 1 < n && s->cmp_func(c, c + size, s->ctx) < 0) {
            child++;
            c += size;
        }
        unsigned char *r = a + root * size;
        if (s->cmp_func(r, c, s->ctx) >= 0) {
            return;
        }
        swap(r, c, size);
        root = child;
    }
}

/// @brief Orders the elements at x, y and z so that x <= y <= z.
static inline void sort3(const sorter_t *s, unsigned char *x, unsigned char *y, unsigned char *z) {
    size_t size = s->size;
    if (s->cmp_func(y, x, s->ctx) < 0) {
        swap(x, y, size);
    }
    if (s->cmp_func(z, y, s->ctx) < 0) {
        swap(y, z, size);
        if (s->cmp_func(y, x, s->ctx) < 0) {
            swap(x, y, size);
        }
    }
}

/// @brief Swaps the size bytes at a and b: 4 and 8 bytes as one word,
/// others eight at a time and then byte by byte.
/// @details memcpy() through locals keeps it safe on unaligned elements and
/// when a and b are the same.
static inline void swap(unsigned char *a, unsigned char *b, size_t size) {
    if (size == sizeof(uint32_t)) {
        uint32_t x, y;
        memcpy(&x, a, sizeof(x));
        memcpy(&y, b, sizeof(y));
        memcpy(a, &y, sizeof(y));
        memcpy(b, &x, sizeof(x));
        return;
    }
    if (size == sizeof(uint64_t)) {
        uint64_t x, y;
        memcpy(&x, a, sizeof(x));
        memcpy(&y, b, sizeof(y));
        memcpy(a, &y, sizeof(y));
        memcpy(b, &x, sizeof(x));
        return;
    }
    if (a == b) {
        return;
    }
    while (size >= sizeof(uint64_t)) {
        uint64_t t;
        memcpy(&t, a, sizeof(t));
        memcpy(a, b, sizeof(t));
        memcpy(b, &t, sizeof(t));
        a += sizeof(t);
        b += sizeof(t);
        size -= sizeof(t);
    }
    while (size > 0) {
        unsigned char t = *a;
        *a++ = *b;
        *b++ = t;
        size--;
    }
}
