# sof_vector

A growable array of fixed-size values in c.

The library has a `sof_vector_t` type that describes the vector as a whole. The application provides the `sof_vector_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_vector_init()`, which takes the size of each element, how many to make room for at first, an optional delete function that is called with every element the vector drops, a _ctx_ passed to it, and an optional allocator. `sof_vector_push()` copies an element onto the end, and `sof_vector_insert()` copies one in anywhere, moving the rest up. `sof_vector_at()` returns the element at an index, which the application can read and change in place, and `sof_vector_next()` steps from one element to the next, as `sof_vector_traverse()` does with a callback. `sof_vector_pop()` takes the last element off, `sof_vector_remove()` takes out any element and keeps the rest in order, and `sof_vector_swap_remove()` does it faster by moving the last element into its place. `sof_vector_find()` looks for an element by comparing each in turn, `sof_vector_sort()` and `sof_vector_sort_stable()` sort the elements through [sof_sort](../sof_sort/), and `sof_vector_search()` finds an element in a sorted vector by binary search, or where one belongs. `sof_vector_reserve()` makes room ahead of time, `sof_vector_resize()` sets the number of elements, and `sof_vector_shrink()` gives back the room that isn't used. `sof_vector_clear()` removes every element, and `sof_vector_free()` releases the vector's memory once the application is finished with it.

The elements are kept first to last in one array. Adding to a full array doubles its size, starting from the capacity given to `sof_vector_init()`, or from **16** elements, so a run of pushes copies each element only a few times on average. The array never shrinks except through `sof_vector_shrink()` and `sof_vector_free()`. Pushing and popping take the same time however long the vector is, and so does `sof_vector_swap_remove()`; inserting and removing elsewhere move every element after the place, so they take time in proportion to that. Because the array is reallocated as it grows and elements move when others are inserted or removed, a pointer to an element, from `sof_vector_at()`, `sof_vector_find()`, `sof_vector_search()`, `sof_vector_next()` or a callback, is good only until the next call that adds or removes elements or changes the capacity. An index stays good until elements before it are added or removed. Nothing points into the `sof_vector_t` itself, so it can be moved or copied, as long as only one copy is used afterwards.

The library's demo is the [numstat](demo/) program in _demo/_, which reads numbers into a vector, sorts it, and prints their count, mean, median, percentiles and more, and finds values in it by binary search. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All sof_vector declarations are in _sof_vector.h_. Sorting is done by the [sof_sort](../sof_sort/) library in _../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_), and memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond that, the library needs nothing but the c standard library.

    typedef struct point_t { int x, y; } point_t;

    static int by_x(const void *a, const void *b, void *ctx) {
        const point_t *p = a, *q = b;
        return (p->x > q->x) - (p->x < q->x);
    }

    sof_vector_t v;
    sof_vector_init(&v, sizeof(point_t), 0, NULL, NULL, NULL);   // no initial room, delete function, ctx or allocator
    sof_vector_push(&v, &(point_t){3, 1});                        // copies the point in
    sof_vector_push(&v, &(point_t){1, 2});
    sof_vector_insert(&v, 1, &(point_t){2, 5});                   // 3,1  2,5  1,2
    sof_vector_sort(&v, by_x, NULL);                              // 1,2  2,5  3,1
    point_t *p = sof_vector_at(&v, 0);                            // 1,2
    p->y = 9;                                                     // elements can be changed in place
    size_t i;
    sof_vector_search(&v, &(point_t){3, 0}, by_x, NULL, &i);      // finds 3,1 at index 2
    sof_vector_free(&v);                                          // releases the array

## Building

    make             # compiles the library and builds ./test and the demo, demo/numstat
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

Besides testing each function, including that pushing gives the same array as inserting at the end for element sizes from 1 to 40 bytes, the tests run a vector on threads as the Threading section below describes: readers walking and searching a vector under a readers-writer lock while a writer inserts into it and removes from it. The tests are linked with _-pthread_; the library itself doesn't use threads.

To use the library in another project, copy _sof_vector.c_ and _sof_vector.h_, _sof_sort.c_ and _sof_sort.h_ from _../sof_sort/_, and _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into it and compile the _.c_ files with the project's own sources. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_, _sof_vector.c_, _sof_sort.c_ and _sof_alloc.c_ with _-pthread_.

## Functions

`sof_vector_error_t sof_vector_init(sof_vector_t *v, size_t element_size, size_t capacity, sof_vector_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc)`

Initialize the vector _v_ the caller provides, for elements of _element_size_ bytes. _capacity_ is the number of elements to make room for now, or **0** to allocate when the first is added; the array doubles as it fills, so this only saves reallocations. The _del_func_ is an optional function that is called with each element removed without being copied out to the caller: by `sof_vector_clear()`, `sof_vector_free()` and `sof_vector_resize()`, and by `sof_vector_pop()`, `sof_vector_remove()` and `sof_vector_swap_remove()` given no place to copy it. This gives the caller an opportunity to free what the element owns. _ctx_ is passed to every call of _del_func_, so it can use application state without globals; it can be **NULL**. _alloc_ is the allocator for the array and for the buffer `sof_vector_sort_stable()` uses; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). It is copied into the vector, so it can be on the stack. Returns **SOF_VECTOR_OK**, **SOF_VECTOR_NO_MEM** if room for _capacity_ elements couldn't be allocated, or **SOF_VECTOR_ERROR** if _v_ is **NULL**, _element_size_ is **0**, or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the vector is left empty and not initialized.

`sof_vector_error_t sof_vector_push(sof_vector_t *v, const void *element)`

Copy the _element_size_ bytes at _element_ onto the end of the vector. When there is room the bytes are copied straight into the next slot, without the checks and moves of `sof_vector_insert()` (about 2.9 ns per push of a `uint32_t` on an Apple M4 Max, against 4.5 through insert, and 1.8 against 3.3 on an Intel i9). If the array is full it is reallocated at twice the size, which moves every element. _element_ may point to an element of the vector itself, as from `sof_vector_at()`, to append a copy of it. Returns **SOF_VECTOR_OK**, **SOF_VECTOR_NO_MEM** if the array couldn't grow, in which case the vector is unchanged, or **SOF_VECTOR_ERROR** if _element_ is **NULL**, or _v_ is **NULL** or not initialized.

`sof_vector_error_t sof_vector_insert(sof_vector_t *v, size_t index, const void *element)`

Copy the _element_size_ bytes at _element_ into the vector at _index_, moving the elements from _index_ on up one place. _index_ can be anything from **0**, which puts the element first, to `count`, which appends it as `sof_vector_push()` does. If the array is full it is reallocated at twice the size. _element_ may point to an element of the vector itself, before or after _index_. Returns **SOF_VECTOR_OK**, **SOF_VECTOR_NO_MEM** if the array couldn't grow, in which case the vector is unchanged, or **SOF_VECTOR_ERROR** if _index_ is greater than `count`, _element_ is **NULL**, or _v_ is **NULL** or not initialized.

`void *sof_vector_at(const sof_vector_t *v, size_t index)`

Return a pointer to the element at _index_, counting from **0**. The application may read and change the element's bytes through it. The pointer is good until elements are next added or removed or the capacity changes. Returns **NULL** if _index_ isn't less than `count`.

`void *sof_vector_next(const sof_vector_t *v, const void *element)`

Return the element after _element_, or **NULL** if _element_ is the last one. Together with `sof_vector_at()` this visits every element in order without a callback:

    for (e = sof_vector_at(&v, 0); e != NULL; e = sof_vector_next(&v, e)) { ... }

Also returns **NULL** if _element_ is **NULL** or isn't the start of an element of _v_, as when the vector has shrunk past it.

`int sof_vector_traverse(const sof_vector_t *v, sof_vector_traverse_func_t cb, void *ctx)`

Visit every element from first to last, calling _cb_ with a pointer to each element and _ctx_. The callback may change the element, but not add elements to or remove them from the vector. A **NULL** _cb_ visits nothing. If the callback returns a non-zero value, the traversal stops and returns that value. Otherwise **0** is returned once every element has been visited, or **-1** if _v_ is **NULL** or not initialized. An empty vector visits nothing and returns **0**.

`sof_vector_error_t sof_vector_pop(sof_vector_t *v, void *element)`

Remove the last element. If _element_ is not **NULL**, the last element's _element_size_ bytes are copied there and the caller takes them over; the _del_func_ isn't called. _element_ must not point into the vector. If _element_ is **NULL**, the element goes to the _del_func_, if there is one, and is dropped. The array doesn't shrink. Returns **SOF_VECTOR_OK**, **SOF_VECTOR_NO_MATCH** if the vector is empty, or **SOF_VECTOR_ERROR** if _v_ is **NULL** or not initialized; in both of those cases _element_ is left unchanged.

`sof_vector_error_t sof_vector_remove(sof_vector_t *v, size_t index, void *removed)`

Remove the element at _index_, moving the elements after it down one place, so the rest stay in order. The element is copied to _removed_, which must not point into the vector, or if _removed_ is **NULL**, given to the _del_func_, if there is one. Returns **SOF_VECTOR_OK**, **SOF_VECTOR_NO_MATCH** if _index_ isn't less than `count`, or **SOF_VECTOR_ERROR** if _v_ is **NULL** or not initialized; in each of those cases _removed_ is left unchanged.

`sof_vector_error_t sof_vector_swap_remove(sof_vector_t *v, size_t index, void *removed)`

Remove the element at _index_ by moving the last element into its place, which takes the same time however long the vector is but changes the order. _removed_ is handled as by `sof_vector_remove()`. Returns **SOF_VECTOR_OK**, **SOF_VECTOR_NO_MATCH** if _index_ isn't less than `count`, or **SOF_VECTOR_ERROR** if _v_ is **NULL** or not initialized; in each of those cases _removed_ is left unchanged.

`void *sof_vector_find(const sof_vector_t *v, const void *key, sof_vector_compare_func_t cmp_func, void *ctx, size_t *index)`

Find the first element equal to _key_, calling _cmp_func_ with _key_, an element and _ctx_ for each element in turn until it returns **0**. _key_ can be an element, or anything _cmp_func_ knows how to compare with one, such as a name to find a record by. The vector needn't be sorted. If _index_ is not **NULL**, it is set to the index of the element found. Returns the element, or **NULL**, leaving _index_ unchanged, if none is equal, _key_ or _cmp_func_ is **NULL**, or _v_ is **NULL** or not initialized.

`sof_vector_error_t sof_vector_sort(sof_vector_t *v, sof_vector_compare_func_t cmp_func, void *ctx)`

Sort the elements in place with `sof_sort_quick()`, in the order _cmp_func_ gives, passing it _ctx_. It isn't stable: elements that compare equal may change places. It allocates nothing. Returns **SOF_VECTOR_OK**, or **SOF_VECTOR_ERROR** if _cmp_func_ is **NULL**, or _v_ is **NULL** or not initialized.

`sof_vector_error_t sof_vector_sort_stable(sof_vector_t *v, sof_vector_compare_func_t cmp_func, void *ctx)`

Sort the elements as `sof_vector_sort()` does, but with `sof_sort_merge()`, so elements that compare equal keep their order. It allocates a buffer as large as the elements, through the vector's allocator, for the time of the call. Returns **SOF_VECTOR_OK**, **SOF_VECTOR_NO_MEM** if the buffer couldn't be allocated, in which case the vector is unchanged, or **SOF_VECTOR_ERROR** if _cmp_func_ is **NULL**, or _v_ is **NULL** or not initialized.

`void *sof_vector_search(const sof_vector_t *v, const void *key, sof_vector_compare_func_t cmp_func, void *ctx, size_t *index)`

Find _key_ in a vector sorted in the order _cmp_func_ gives, by binary search, calling _cmp_func_ with _key_, an element and _ctx_ O(log n) times. If _index_ is not **NULL**, it is set to the index of the first element not less than _key_, whether or not that element is equal: where _key_ would be inserted to keep the vector sorted, which is `count` if every element is less. So a sorted insert is a `sof_vector_search()` and a `sof_vector_insert()` at _index_. Returns the first element equal to _key_, or **NULL** if there is none. Also returns **NULL**, leaving _index_ unchanged, if _key_ or _cmp_func_ is **NULL**, or _v_ is **NULL** or not initialized. A vector that isn't sorted gives an unspecified result, but no memory outside it is touched.

`sof_vector_error_t sof_vector_reserve(sof_vector_t *v, size_t capacity)`

Make room for at least _capacity_ elements. If the array is smaller it is reallocated to exactly _capacity_ elements, which moves every element, and adding elements up to that number then allocates nothing; otherwise nothing happens. Returns **SOF_VECTOR_OK**, **SOF_VECTOR_NO_MEM** if the array couldn't grow, in which case the vector is unchanged, or **SOF_VECTOR_ERROR** if _v_ is **NULL** or not initialized.

`sof_vector_error_t sof_vector_resize(sof_vector_t *v, size_t count)`

Set the number of elements to _count_. Growing adds elements whose bytes are all **0**, reallocating the array, to at least twice its size, if it hasn't room for _count_. Shrinking removes the elements from _count_ on, each going to the _del_func_, if there is one, from first to last, and keeps the array. Returns **SOF_VECTOR_OK**, **SOF_VECTOR_NO_MEM** if the array couldn't grow, in which case the vector is unchanged, or **SOF_VECTOR_ERROR** if _v_ is **NULL** or not initialized.

`sof_vector_error_t sof_vector_shrink(sof_vector_t *v)`

Reallocate the array to exactly `count` elements, which moves every element, or free it if the vector is empty, giving back the room the vector isn't using. The vector can still be used. Returns **SOF_VECTOR_OK**, **SOF_VECTOR_NO_MEM** if the array couldn't be reallocated, in which case the vector is unchanged, or **SOF_VECTOR_ERROR** if _v_ is **NULL** or not initialized.

`sof_vector_error_t sof_vector_clear(sof_vector_t *v)`

Remove every element, leaving an empty vector that can still be used. The _del_func_, if not **NULL**, is called for each element before it is removed, from first to last. The array is kept, so adding up to `capacity` elements again allocates nothing. Returns **SOF_VECTOR_OK**, or **SOF_VECTOR_ERROR** if _v_ is **NULL** or not initialized.

`sof_vector_error_t sof_vector_free(sof_vector_t *v)`

Remove every element, as `sof_vector_clear()` does, and release the array. The `sof_vector_t` itself belongs to the caller and isn't freed; it is left empty. Returns **SOF_VECTOR_OK**, or **SOF_VECTOR_ERROR** if _v_ is **NULL** or not initialized.

Every function accepts a **NULL** _v_: `sof_vector_at()`, `sof_vector_next()`, `sof_vector_find()` and `sof_vector_search()` return **NULL**, `sof_vector_traverse()` returns **-1**, and the others return **SOF_VECTOR_ERROR**. A vector that was freed, or whose `sof_vector_init()` failed, is treated the same way: it is empty, and adding to it fails, until it is initialized again.

## Types

    typedef enum sof_vector_error_t {
        SOF_VECTOR_OK = 0,
        SOF_VECTOR_NO_MEM,
        SOF_VECTOR_NO_MATCH,
        SOF_VECTOR_ERROR,
    } sof_vector_error_t;

    typedef struct sof_vector_t {
        unsigned char *elements;    // capacity * element_size bytes, first to last; NULL while capacity is 0
        size_t element_size;        // size of each element in bytes
        size_t count;               // number of elements
        size_t capacity;            // number of elements the array has room for
        sof_vector_element_delete_func_t del_func;
        void *ctx;                  // passed to del_func
        sof_alloc_t alloc;          // copy of the allocator; zeroed for the standard functions
        int ready;                  // 1 once initialized, 0 when freed or init failed
    } sof_vector_t;

The fields of `sof_vector_t` can be read but should not be changed by the application. The elements, `count` of them from `elements`, can be read and changed freely; the room after them belongs to the library.

    typedef int (*sof_vector_compare_func_t)(const void *a, const void *b, void *ctx)

Compares two elements, for `sof_vector_sort()` and `sof_vector_sort_stable()`, or a key and an element, for `sof_vector_find()` and `sof_vector_search()`, which pass their _key_ as _a_. Returns a value less than **0** if _a_ sorts before _b_, **0** if they are equal, or a value greater than **0** if _a_ sorts after _b_. The order must be consistent, or the vector is left in an unspecified order (but no memory outside it is touched). It must not change the elements or call into the vector. _ctx_ is the context pointer given to the function that calls it. It has the same type as `sof_sort_compare_func_t`, so one function serves both libraries.

    typedef void (*sof_vector_element_delete_func_t)(void *element, void *ctx)

Receives each element just before it is removed without being copied out: by `sof_vector_clear()`, `sof_vector_free()` and `sof_vector_resize()`, from first to last, and by `sof_vector_pop()`, `sof_vector_remove()` and `sof_vector_swap_remove()` with a **NULL** place to copy it to. _element_ points to the element's bytes in the vector, and the application can free what the element owns here. It is optional: pass **NULL** to `sof_vector_init()` if there is nothing to free. It must not call into the vector. _ctx_ is the context pointer given to `sof_vector_init()`.

    typedef int (*sof_vector_traverse_func_t)(void *element, void *ctx)

Called for each _element_ visited by `sof_vector_traverse()`, with the _ctx_ given to it. It may change the element's bytes, but must not add elements to or remove them from the vector. Return **0** to continue, or a non-zero value to stop the traversal early; that value is returned by `sof_vector_traverse()`.

## Threading

The library does no locking of its own. It has no global state, so different vectors can be used on different threads freely, but a vector shared between threads needs a lock supplied by the application.

**Read-only** (any number of threads at once, as long as no thread is writing): `sof_vector_at()`, `sof_vector_next()`, `sof_vector_find()`, `sof_vector_search()`, `sof_vector_traverse()` with a callback that doesn't change the elements, and reading `count`. These functions take a `const sof_vector_t *`, but the elements they return, or pass to the callback, aren't `const`, since the application may change them. The compare functions given to `sof_vector_find()` and `sof_vector_search()` run on the reading thread, so they must be safe to run on several at once.

**Writes** (one thread, with no readers): `sof_vector_init()`, `sof_vector_push()`, `sof_vector_insert()`, `sof_vector_pop()`, `sof_vector_remove()`, `sof_vector_swap_remove()`, `sof_vector_reserve()`, `sof_vector_resize()`, `sof_vector_shrink()`, `sof_vector_sort()`, `sof_vector_sort_stable()`, `sof_vector_clear()`, `sof_vector_free()`, `sof_vector_traverse()` with a callback that changes the elements, and changing an element through a pointer to it.

A vector that is mostly read, such as a sorted table searched by many threads and changed now and then, suits a readers-writer lock:

    // Reader
    pthread_rwlock_rdlock(&lock);
    record_t *r = sof_vector_search(&v, &key, by_name, NULL, NULL);
    if (r != NULL) {
        use(r);                                 // only while the lock is held
    }
    pthread_rwlock_unlock(&lock);

    // Writer
    pthread_rwlock_wrlock(&lock);
    size_t i;
    if (sof_vector_search(&v, &rec, by_name, NULL, &i) == NULL) {
        sof_vector_insert(&v, i, &rec);         // keeps the vector sorted
    }
    pthread_rwlock_unlock(&lock);

Some things to watch for:

- **Element pointers are only valid while the lock is held.** Once it is released, another thread can add or remove elements, which moves them, or grow the array, which moves all of them. Copy what is needed out first, or keep an index and look it up again next time.
- **Callbacks run inside the call that invokes them, while the caller holds the lock.** They must not take the lock again. Neither a compare function nor a `del_func` may call into the vector at all.
- **Anything an element points to belongs to the application.** The library copies an element's bytes but never follows pointers in them, so what they point to needs its own synchronization if threads share it.
