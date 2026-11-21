# sof_sort

Sorting algorithms for arrays of any type in c.

The library sorts an array the application already has, in place, like `qsort()`: each function takes the array, the number of elements, the size of each element in bytes, a compare function and a _ctx_ that is passed to every compare, so the compare function can use application state without globals. There is no structure to set up. `sof_sort_quick()` is the one to use in most programs: it is the fastest or close to it on every input in the benchmark, needs no memory, and is never worse than O(n log n). `sof_sort_merge()` is stable (equal elements keep their order), also O(n log n) in every case, a little faster on random arrays of up to 10,000 elements and fastest on input made of runs already in order, such as organ pipe, but needs a buffer as large as the array, which it allocates through an optional `sof_alloc_t`. `sof_sort_heap()` is O(n log n) and needs no memory, but isn't stable and is slower. `sof_sort_insertion()` is stable and fast on very small or nearly sorted arrays, and `sof_sort_bubble()` is there for comparison; both are O(n^2) in general. Once an array is sorted, `sof_sort_search()` finds an element equal to a key by binary search, `sof_sort_lower_bound()` gives the index of the first element not less than a key, which is where the key would be inserted, and `sof_sort_upper_bound()` the first element greater than it, so the two give the range of elements equal to a key, and the second picks from running totals.

`sof_sort_quick()` is pattern-defeating quicksort (pdqsort, after Orson Peters). It partitions around the median of the first, middle and last elements, sorts partitions of 16 elements or fewer with insertion sort, and recurses into the smaller partition only, so the stack never holds more than log2(n) calls. A partition that needed no swaps suggests the input is already in order, so it tries insertion sort on both sides, giving up after moving elements 8 places; sorted and reversed input take 2 or 3 compares per element. When a pivot equals the pivot before it, the elements equal to it are set aside in one pass and never looked at again, so arrays of all-equal or a few distinct values take about linear time. A badly unbalanced partition (a side smaller than an eighth) swaps a few elements to break up the pattern, and after log2(n) of them on one path it finishes that partition with heap sort, so it is O(n log n) in every case, even against an adversary. `sof_sort_merge()` sorts runs of 16 elements or fewer with insertion sort too, and skips merging two runs that are already in order. Elements of 4 and 8 bytes are swapped and copied as one word, through `memcpy()`, so the array needn't be aligned.

_bench.c_ is the benchmark. The library's demo is the [anagram](demo/) program in _demo/_, which finds the groups of words in a word list that are anagrams of each other. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All sof_sort declarations are in _sof_sort.h_. `sof_sort_merge()` allocates through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_), which the other sorts don't use. The benchmark reads its options with [sof_copt](../sof_copt/) (_../sof_copt/sof_copt.c_ and _sof_copt.h_) and makes its random input with [sof_random](../sof_random/) (_../sof_random/sof_random.c_ and _sof_random.h_), neither of which the library itself uses. Beyond that, the library needs nothing but the c standard library.

    static int compare(const void *a, const void *b, void *ctx) {
        const point_t *p = a, *q = b;
        return (p->x > q->x) - (p->x < q->x);                       // by x
    }
    ...
    sof_sort_quick(points, count, sizeof(point_t), compare, NULL);  // any type, in place
    if (sof_sort_merge(points, count, sizeof(point_t), compare, NULL, NULL) != SOF_SORT_OK) {
        ...                                                         // no memory for the buffer
    }
    point_t key = {.x = 10};
    point_t *p = sof_sort_search(points, count, sizeof(point_t), &key, compare, NULL);  // NULL if no x is 10
    size_t at = sof_sort_lower_bound(points, count, sizeof(point_t), &key, compare, NULL);  // where x = 10 goes

## Building

    make             # builds sof_sort.o, ./test, ./bench (the benchmark) and the demo, demo/anagram
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization
    ./bench          # runs the benchmark

The tests sort shuffled permutations of every size from 0 to 100 and larger ones, arrays with many duplicates, sorted, reversed, organ pipe and other patterned input, and records of 1 to 40 bytes, with every sort. Every sort is also given random, sorted, reversed, all-equal, organ pipe, few-unique and sawtooth input at every size from 0 to 300 and three larger ones (the quadratic sorts up to 100), with elements of 1, 2, 3, 4, 8, 12, 16 and 24 bytes at aligned and unaligned addresses, checking that the keys come out sorted and complete, each element's other bytes travel with its key, and the O(n log n) sorts stay within 4 n (log2(n) + 2) compares. They check that the stable sorts are stable, with elements of 3 to 24 bytes too, count the compares the O(n log n) sorts take on 200,000-element patterned arrays and against McIlroy's adversary for quicksort, check that `sof_sort_quick()` takes no more than 4 compares per element on sorted, reversed and all-equal input, sort with a compare function that answers at random (every element must still be there, and the sanitizers check nothing outside the array is touched), and run `sof_sort_merge()` with the test allocator, failing its allocation. The searches are checked against a linear scan on random sorted arrays of every size up to 300, with every key in range and either side of it, with their compares counted, and on their **NULL** and invalid cases.

To use the library in another project, copy _sof_sort.c_ and _sof_sort.h_, and _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into it and compile the _.c_ files with the project's own sources. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_, _sof_sort.c_ and _sof_alloc.c_ with _-pthread_.

## Benchmark

    bench [-h] [-n count] [words]

Times every sort and the c library's `qsort()` on three kinds of input, reporting the best of 5 runs in nanoseconds per element:

- random: arrays of random `uint32_t` of 10, 100, 1000 ... elements up to _count_. Arrays smaller than 16,384 elements are timed many at a time.
- patterns: arrays of _count_ `uint32_t` that are sorted, reversed, all equal, organ pipe (rising, then falling) or random.
- words: every word in _words_, shuffled, as an array of `char *` compared with `strcmp()`.

Bubble and insertion sort are only given arrays of up to 10,000 elements. The options are:

- _-h_ prints the usage message and exits.
- _-n count_ sorts arrays of up to _count_ elements, from 10 to 100,000,000 (default 1,000,000).
- _words_ is a file with a word on each line (default _../test-data/words.txt_). If it can't be read, the words measure is skipped.

On an Apple M-series Mac, with the default options:

| random | bubble | insertion | heap | merge | quick | qsort |
|---|---|---|---|---|---|---|
| 10 | 8.8 | 6.7 | 10.4 | 8.1 | 6.5 | 8.1 |
| 100 | 92.8 | 33.3 | 17.6 | 13.6 | 14.7 | 20.0 |
| 1000 | 637 | 235 | 27.3 | 23.8 | 24.6 | 28.9 |
| 10000 | 6076 | 3011 | 37.9 | 32.2 | 32.5 | 37.9 |
| 100000 | | | 50.2 | 49.6 | 48.2 | 53.2 |
| 1000000 | | | 76.0 | 61.8 | 57.4 | 64.0 |

| 1,000,000 | heap | merge | quick | qsort |
|---|---|---|---|---|
| sorted | 55.9 | 1.8 | 1.6 | 1.8 |
| reversed | 55.2 | 14.0 | 2.5 | 9.4 |
| equal | 3.7 | 1.6 | 1.3 | 1.1 |
| organ pipe | 58.1 | 8.9 | 30.6 | 26.7 |
| random | 76.0 | 61.8 | 58.1 | 64.3 |
| 263,533 words | 234 | 125 | 119 | 126 |

and on an AMD Ryzen 9 5950X, Arch Linux, gcc 16 and glibc:

| random | bubble | insertion | heap | merge | quick | qsort |
|---|---|---|---|---|---|---|
| 10 | 13.0 | 9.6 | 12.8 | 10.6 | 9.8 | 13.9 |
| 100 | 129 | 41.9 | 25.3 | 21.6 | 23.3 | 29.6 |
| 1000 | 922 | 367 | 37.5 | 32.7 | 36.5 | 45.6 |
| 10000 | 9020 | 3625 | 49.0 | 46.3 | 49.3 | 60.7 |
| 100000 | | | 66.4 | 56.1 | 62.6 | 77.7 |
| 1000000 | | | 107 | 66.9 | 73.8 | 93.4 |

| 1,000,000 | heap | merge | quick | qsort |
|---|---|---|---|---|
| sorted | 73.6 | 2.7 | 2.8 | 22.2 |
| reversed | 71.2 | 22.6 | 4.6 | 24.8 |
| equal | 7.2 | 2.7 | 2.1 | 22.1 |
| organ pipe | 74.7 | 13.4 | 52.6 | 24.4 |
| random | 107 | 66.9 | 75.4 | 93.8 |
| 263,533 words | 312 | 153 | 157 | 163 |

`sof_sort_quick()` is a little faster than macOS's `qsort()` on random numbers and on strings, and as fast on sorted and all-equal input, which it recognizes; reversed input is a partition and two linear passes. Organ pipe input is its slowest case: a few long runs in order, such as sorted batches appended to each other, aren't recognized, and are partitioned like any other input. For data like that, use `sof_sort_merge()`, which merges the runs as they are. `sof_sort_merge()` is a little faster on random arrays of 100 to 10,000 numbers and close behind on larger ones, fastest on organ pipe input, and skips merging runs already in order. On the Ryzen with glibc, whose `qsort()` doesn't recognize sorted or all-equal input, `sof_sort_quick()` is a fifth faster on random numbers and 8 to 10 times as fast on those, and `sof_sort_merge()` is faster than it on random arrays of 100 elements and more.

## Functions

`sof_sort_error_t sof_sort_bubble(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx)`

Sort the _count_ elements of _size_ bytes at _base_ in place with bubble sort, in the order _cmp_func_ gives; _ctx_ is passed to every call of _cmp_func_ and may be **NULL**. Stable. Takes O(n^2) compares and swaps, but only _count_ - 1 compares on an array that is already sorted. Returns **SOF_SORT_OK**, or **SOF_SORT_ERROR** if _cmp_func_ is **NULL**, _size_ is **0**, or _base_ is **NULL** and _count_ isn't **0**.

`sof_sort_error_t sof_sort_heap(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx)`

Sort the array with heap sort, with the same arguments as `sof_sort_bubble()`. Not stable. Takes O(n log n) compares in every case and no memory beyond the array. Returns the same values as `sof_sort_bubble()`.

`sof_sort_error_t sof_sort_insertion(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx)`

Sort the array with insertion sort, with the same arguments as `sof_sort_bubble()`. Stable. Takes O(n^2) compares and swaps, but only _count_ - 1 compares on an array that is already sorted, and is fast on small arrays. Returns the same values as `sof_sort_bubble()`.

`size_t sof_sort_lower_bound(const void *base, size_t count, size_t size, const void *key, sof_sort_compare_func_t cmp_func, void *ctx)`

Find where _key_ belongs in the sorted array of _count_ elements of _size_ bytes at _base_, by binary search: return the index of the first element for which `cmp_func(element, key, ctx)` returns **0** or more, from **0** to _count_. That is where _key_ would be inserted to keep the array sorted, before any elements equal to it. It returns _count_ if every element is less than _key_, as it does for an empty array. The array must be sorted in the order _cmp_func_ gives, or at least have every element less than _key_ before every other. _key_ is always passed as _cmp_func_'s second argument and an element as its first, so _key_ may point to something other than an element, such as just the field the array is sorted by, if _cmp_func_ knows that. It takes at most ceil(log2(_count_ + 1)) compares. Returns **0**, without calling _cmp_func_, if _cmp_func_ or _key_ is **NULL**, _size_ is **0**, or _base_ is **NULL** and _count_ isn't **0**.

`sof_sort_error_t sof_sort_merge(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx, const sof_alloc_t *alloc)`

Sort the array with merge sort, with the same arguments as `sof_sort_bubble()`. Stable. Takes O(n log n) compares in every case, and at most _count_ on an array that is already sorted. Allocates a buffer of _count_ * _size_ bytes from _alloc_ (**NULL** for the standard functions; see [sof_alloc](../sof_alloc/)) and frees it before returning; with a _count_ of **0** or **1** it allocates nothing. Returns **SOF_SORT_OK**; **SOF_SORT_NO_MEM** if the buffer couldn't be allocated or _count_ * _size_ overflows, leaving the array unchanged; or **SOF_SORT_ERROR** for the same arguments as `sof_sort_bubble()`, or if _alloc_ has a _malloc_ but no _free_ or _free_sized_.

`sof_sort_error_t sof_sort_quick(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx)`

Sort the array with quicksort, falling back to heap sort as described above, with the same arguments as `sof_sort_bubble()`. Not stable. Takes O(n log n) compares in every case, about 2 per element on sorted or all-equal input and 3 on reversed input, and O(log n) stack, and no memory beyond that. Input made of a few long runs in order, such as organ pipe, isn't recognized; `sof_sort_merge()` sorts it three to four times as fast (see Benchmark). Returns the same values as `sof_sort_bubble()`.

`void *sof_sort_search(const void *base, size_t count, size_t size, const void *key, sof_sort_compare_func_t cmp_func, void *ctx)`

Find an element equal to _key_ in the sorted array, with the same arguments as `sof_sort_lower_bound()` and one more compare. If several elements are equal to _key_, it returns the first of them. Returns a pointer to the element, or **NULL** if no element is equal to _key_, the array is empty, or the arguments are those `sof_sort_lower_bound()` rejects. As with `bsearch()`, the pointer isn't `const`, since the array belongs to the caller.

`size_t sof_sort_upper_bound(const void *base, size_t count, size_t size, const void *key, sof_sort_compare_func_t cmp_func, void *ctx)`

As `sof_sort_lower_bound()`, but return the index of the first element for which _cmp_func_ returns more than **0**, so past any elements equal to _key_: where _key_ would be inserted after its equals. The elements equal to _key_ are those from `sof_sort_lower_bound()` up to, but not including, `sof_sort_upper_bound()`. In an array of running totals of weights, the first total above a number drawn below the last total is chosen in proportion to its weight. Returns _count_ if no element is greater than _key_, and **0** for the same invalid arguments as `sof_sort_lower_bound()`.

No function compares or moves anything when it returns **SOF_SORT_ERROR**. A **NULL** _base_ with a _count_ of **0** is an empty array, and sorts with **SOF_SORT_OK**, as does any array of **0** or **1** elements, without calling _cmp_func_. If _cmp_func_ isn't consistent (if it says _a_ < _b_ and _b_ < _a_, say), the array is left in some unspecified order, but its elements are all still there and nothing outside it is touched. The searches never change the array; a **NULL** _base_ with a _count_ of **0** is empty for them too, and they find nothing in it without calling _cmp_func_. On an array that isn't sorted in _cmp_func_'s order, they return some index from **0** to _count_, or some element or **NULL**, but read nothing outside the array.

## Types

    typedef enum sof_sort_error_t {
        SOF_SORT_OK = 0,
        SOF_SORT_NO_MEM,
        SOF_SORT_NO_MATCH,
        SOF_SORT_ERROR,
    } sof_sort_error_t;

What the functions return. **SOF_SORT_NO_MATCH** is never returned; it is there so the codes are the same as the other libraries'.

    typedef int (*sof_sort_compare_func_t)(const void *a, const void *b, void *ctx)

Compares two elements, and orders the array. It should return a value less than **0** if _a_ sorts before _b_, **0** if they are equal, or greater than **0** if _a_ sorts after _b_. _a_ and _b_ point to elements in the array or, during `sof_sort_merge()`, in its buffer, so the function must not keep the pointers or change what they point to. The searches pass an element as _a_ and the key as _b_. _ctx_ is the context pointer given to the sort or search function.

## Threading

The library has no global state and does no locking. Any number of threads can sort different arrays at once. An array must not be read or changed by another thread while it is being sorted, since every sort moves its elements around until it returns. The searches only read, so any number of threads can search one array at once, as long as none is sorting or changing it; with a reader-writer lock, they need only the read lock. The compare function is only called on the sorting thread, but if several threads share a _ctx_, the compare function must be safe to call from all of them at once. `sof_sort_merge()` allocates, so threads using it at once with one allocator need an allocator that is safe to call from several threads; the standard functions are.
