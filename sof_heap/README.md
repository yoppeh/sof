# sof_heap

A binary-heap priority queue of fixed-size values in c.

The library has a `sof_heap_t` type that describes the heap as a whole. The application provides the `sof_heap_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_heap_init()`, which takes the size of each element, how many to make room for at first, a compare function that orders the elements, an optional delete function that is called with every element the heap drops, a _ctx_ passed to both, and an optional allocator. `sof_heap_push()` copies an element in, and `sof_heap_pop()` takes the smallest out, copying it out or, if asked to, dropping it. `sof_heap_peek()` returns the smallest without removing it. `sof_heap_replace()` pops the smallest and pushes another in one step, which is how a heap keeps the n largest of a stream, and `sof_heap_remove()` takes out any element, wherever it is. `sof_heap_next()` steps from one element to the next in the array, and `sof_heap_traverse()` calls a callback for each. `sof_heap_clear()` removes every element, and `sof_heap_free()` releases the heap's memory once the application is finished with it.

The elements are kept in one array as a binary heap: each is no greater than the two at twice its index plus 1 and plus 2, so the first is the smallest, but the rest are in no useful order. Pushing and popping take O(log n) compares, and peeking none. Of elements that compare equal, any can come out first; to make the order stable, include a sequence number in the element and compare by it last. A reversed compare function gives the largest first. The array doubles in size when it fills, starting from the capacity given to `sof_heap_init()`, or from **16** elements, and never shrinks until `sof_heap_free()`. It holds one more element than its capacity, where the library keeps the element it is moving, so nothing is allocated except to grow. Every change moves elements, so a pointer to an element, from `sof_heap_peek()`, `sof_heap_next()` or a callback, is good only until the heap next changes. Nothing points into the `sof_heap_t` itself, so it can be moved or copied, as long as only one copy is used afterwards.

The library's demo is the [top](demo/) program in _demo/_, which prints the most frequent words, lines or n-grams of a large input, keeping the leaders in a heap. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All sof_heap declarations are in _sof_heap.h_. Memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The tests, only, use [sof_sort](../sof_sort/) in _../sof_sort/_ to check the heap's order. Beyond that, the library needs nothing but the c standard library.

    typedef struct job_t { int priority; const char *name; } job_t;

    static int by_priority(const void *a, const void *b, void *ctx) {
        const job_t *x = a, *y = b;
        return (x->priority > y->priority) - (x->priority < y->priority);
    }

    sof_heap_t h;
    sof_heap_init(&h, sizeof(job_t), 0, by_priority, NULL, NULL, NULL);   // no initial room, delete function, ctx or allocator
    sof_heap_push(&h, &(job_t){3, "backup"});                             // copies the job in
    sof_heap_push(&h, &(job_t){1, "page"});
    job_t job;
    while (sof_heap_pop(&h, &job) == SOF_HEAP_OK) {                       // copies out the lowest priority: page, then backup
        run(&h, job);                                                     // may push more jobs
    }
    sof_heap_free(&h);                                                    // releases the array

## Building

    make             # compiles the library and builds ./test and the demo, demo/top
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

Besides testing each function, the tests run the heap on threads as the Threading section below describes: producers and consumers sharing a heap under a mutex and condition variable, and readers walking a heap under a readers-writer lock while a writer changes it. The tests are linked with _-pthread_; the library itself doesn't use threads.

To use the library in another project, copy _sof_heap.c_ and _sof_heap.h_, and _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into it and compile the _.c_ files with the project's own sources. To build its tests there too, copy _test.c_, _test.h_ from _../test/_, and _sof_sort.c_ and _sof_sort.h_ from _../sof_sort/_, and compile _test.c_, _sof_heap.c_, _sof_sort.c_ and _sof_alloc.c_ with _-pthread_.

## Functions

`sof_heap_error_t sof_heap_init(sof_heap_t *h, size_t element_size, size_t capacity, sof_heap_compare_func_t cmp_func, sof_heap_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc)`

Initialize the heap _h_ the caller provides, for elements of _element_size_ bytes. _capacity_ is the number of elements to make room for now, or **0** to allocate when the first is pushed; the array doubles as it fills, so this only saves reallocations. The _cmp_func_ is required and orders the elements: see `sof_heap_compare_func_t` below. The _del_func_ is an optional function that is called with each element removed without being copied out to the caller: by `sof_heap_clear()`, `sof_heap_free()`, and `sof_heap_pop()`, `sof_heap_remove()` and `sof_heap_replace()` given no place to copy it. This gives the caller an opportunity to free what the element owns. _ctx_ is passed to every call of _cmp_func_ and _del_func_, so they can use application state without globals; it can be **NULL**. _alloc_ is the allocator for the array; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). It is copied into the heap, so it can be on the stack. Returns **SOF_HEAP_OK**, **SOF_HEAP_NO_MEM** if room for _capacity_ elements couldn't be allocated, or **SOF_HEAP_ERROR** if _h_ or _cmp_func_ is **NULL**, _element_size_ is **0**, or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the heap is left empty and not initialized.

`sof_heap_error_t sof_heap_push(sof_heap_t *h, const void *element)`

Copy the _element_size_ bytes at _element_ into the heap, where its order puts them. If the array is full it is reallocated at twice the size, which moves every element. _element_ may point to an element of the heap itself, as from `sof_heap_peek()`, to push a copy of it. Returns **SOF_HEAP_OK**, **SOF_HEAP_NO_MEM** if the array couldn't grow, in which case the heap is unchanged, or **SOF_HEAP_ERROR** if _element_ is **NULL**, or _h_ is **NULL** or not initialized.

`sof_heap_error_t sof_heap_pop(sof_heap_t *h, void *element)`

Remove the smallest element. If _element_ is not **NULL**, the smallest element's _element_size_ bytes are copied there and the caller takes them over; the _del_func_ isn't called. _element_ must not point into the heap. If _element_ is **NULL**, the element goes to the _del_func_, if there is one, and is dropped. The array doesn't shrink. Returns **SOF_HEAP_OK**, **SOF_HEAP_NO_MATCH** if the heap is empty, or **SOF_HEAP_ERROR** if _h_ is **NULL** or not initialized; in both of those cases _element_ is left unchanged.

`const void *sof_heap_peek(const sof_heap_t *h)`

Return a pointer to the smallest element, the next to be popped, without removing it. The element must not be changed through it, since that could break the heap's order; `sof_heap_replace()` changes it safely. The pointer is good until the heap next changes. Returns **NULL** if the heap is empty.

`sof_heap_error_t sof_heap_replace(sof_heap_t *h, const void *element, void *removed)`

Remove the smallest element and push a copy of the _element_size_ bytes at _element_, as `sof_heap_pop()` and then `sof_heap_push()` would, but moving elements once and never allocating. The removed element is copied to _removed_, which must not point into the heap, or if _removed_ is **NULL**, given to the _del_func_, if there is one. _element_ may point to an element of the heap itself. To keep the n largest of a stream, push until the heap has n elements, then `sof_heap_replace()` each one larger than `sof_heap_peek()`. Returns **SOF_HEAP_OK**, **SOF_HEAP_NO_MATCH** if the heap is empty, in which case nothing is pushed, or **SOF_HEAP_ERROR** if _element_ is **NULL**, or _h_ is **NULL** or not initialized; in each of those cases _removed_ is left unchanged.

`sof_heap_error_t sof_heap_remove(sof_heap_t *h, const void *element, void *removed)`

Remove the element that _element_ points to, wherever it is in the heap. It is found by its address, as `sof_heap_peek()`, `sof_heap_next()` or `sof_heap_traverse()` gave it, not by comparing. The element is copied to _removed_, which must not point into the heap, or if _removed_ is **NULL**, given to the _del_func_, if there is one. Changing an element's priority is a `sof_heap_remove()` and a `sof_heap_push()`. Returns **SOF_HEAP_OK**, **SOF_HEAP_NO_MATCH** if _element_ is **NULL** or isn't the start of an element of _h_, or **SOF_HEAP_ERROR** if _h_ is **NULL** or not initialized; in each of those cases _removed_ is left unchanged.

`const void *sof_heap_next(const sof_heap_t *h, const void *element)`

Return the element after _element_ in the array, or **NULL** if _element_ is the last one. Together with `sof_heap_peek()` this visits every element without a callback, starting with the smallest, but otherwise in no useful order:

    for (e = sof_heap_peek(&h); e != NULL; e = sof_heap_next(&h, e)) { ... }

Also returns **NULL** if _element_ is **NULL** or isn't the start of an element of _h_, as when the heap has shrunk past it.

`int sof_heap_traverse(const sof_heap_t *h, sof_heap_traverse_func_t cb, void *ctx)`

Visit every element in the array's order, as `sof_heap_next()` does, calling _cb_ with a pointer to each element and _ctx_. A **NULL** _cb_ visits nothing. If the callback returns a non-zero value, the traversal stops and returns that value. Otherwise **0** is returned once every element has been visited, or **-1** if _h_ is **NULL** or not initialized. An empty heap visits nothing and returns **0**.

`sof_heap_error_t sof_heap_clear(sof_heap_t *h)`

Remove every element, leaving an empty heap that can still be used. The _del_func_, if not **NULL**, is called for each element before it is removed, in no particular order. The array is kept, so pushing up to `capacity` elements again allocates nothing. Returns **SOF_HEAP_OK**, or **SOF_HEAP_ERROR** if _h_ is **NULL** or not initialized.

`sof_heap_error_t sof_heap_free(sof_heap_t *h)`

Remove every element, as `sof_heap_clear()` does, and release the array. The `sof_heap_t` itself belongs to the caller and isn't freed; it is left empty. Returns **SOF_HEAP_OK**, or **SOF_HEAP_ERROR** if _h_ is **NULL** or not initialized.

Every function accepts a **NULL** _h_: `sof_heap_peek()` and `sof_heap_next()` return **NULL**, `sof_heap_traverse()` returns **-1**, and the others return **SOF_HEAP_ERROR**. A heap that was freed, or whose `sof_heap_init()` failed, is treated the same way: it is empty, and `sof_heap_push()` fails, until it is initialized again.

## Types

    typedef enum sof_heap_error_t {
        SOF_HEAP_OK = 0,
        SOF_HEAP_NO_MEM,
        SOF_HEAP_NO_MATCH,
        SOF_HEAP_ERROR,
    } sof_heap_error_t;

    typedef struct sof_heap_t {
        unsigned char *elements;    // (capacity + 1) * element_size bytes, in heap order
        size_t element_size;        // size of each element in bytes
        size_t count;               // number of elements
        size_t capacity;            // number of elements the array has room for
        sof_heap_compare_func_t cmp_func;
        sof_heap_element_delete_func_t del_func;
        void *ctx;                  // passed to cmp_func and del_func
        sof_alloc_t alloc;          // copy of the allocator; zeroed for the standard functions
        int ready;                  // 1 once initialized, 0 when freed or init failed
    } sof_heap_t;

The fields of `sof_heap_t` can be read but should not be changed by the application, and neither can the elements, `count` of them from `elements`, except through `sof_heap_replace()` and `sof_heap_remove()`. The element after the last of the capacity belongs to the library.

    typedef int (*sof_heap_compare_func_t)(const void *a, const void *b, void *ctx)

Orders the elements: returns a value less than **0** if _a_ should come out before _b_, **0** if either can come out first, or a value greater than **0** if _a_ should come out after _b_. _a_ and _b_ point to _element_size_ bytes, in the heap or in the element being pushed. The order must be consistent, or elements come out in an unspecified order (but no memory outside the heap is touched). It must not change the elements or call into the heap. _ctx_ is the context pointer given to `sof_heap_init()`.

    typedef void (*sof_heap_element_delete_func_t)(void *element, void *ctx)

Receives each element just before it is removed without being copied out: by `sof_heap_clear()` and `sof_heap_free()`, and by `sof_heap_pop()`, `sof_heap_remove()` and `sof_heap_replace()` with a **NULL** place to copy it to. _element_ points to the element's bytes in the heap, and the application can free what the element owns here. It is optional: pass **NULL** to `sof_heap_init()` if there is nothing to free. It must not call into the heap. _ctx_ is the context pointer given to `sof_heap_init()`.

    typedef int (*sof_heap_traverse_func_t)(const void *element, void *ctx)

Called for each _element_ visited by `sof_heap_traverse()`, with the _ctx_ given to it. It must not change the element or the heap. Return **0** to continue, or a non-zero value to stop the traversal early; that value is returned by `sof_heap_traverse()`.

## Threading

The library does no locking of its own. It has no global state, so different heaps can be used on different threads freely, but a heap shared between threads needs a lock supplied by the application.

**Read-only** (any number of threads at once, as long as no thread is writing): `sof_heap_peek()`, `sof_heap_next()`, `sof_heap_traverse()`, and reading `count`; they take a `const sof_heap_t *`. None of them calls the compare function.

**Writes** (one thread, with no readers): `sof_heap_init()`, `sof_heap_push()`, `sof_heap_pop()`, `sof_heap_replace()`, `sof_heap_remove()`, `sof_heap_clear()` and `sof_heap_free()`.

A priority queue shared between threads is nearly always written by both sides, as a work queue, so a plain mutex fits better than a readers-writer lock. To let a consumer wait for work rather than poll, pair the mutex with a condition variable:

    // Producer
    pthread_mutex_lock(&lock);
    sof_heap_push(&h, &job);
    pthread_cond_signal(&ready);
    pthread_mutex_unlock(&lock);

    // Consumer
    job_t job;
    pthread_mutex_lock(&lock);
    while (sof_heap_pop(&h, &job) == SOF_HEAP_NO_MATCH && !stopping) {
        pthread_cond_wait(&ready, &lock);
    }
    pthread_mutex_unlock(&lock);

Some things to watch for:

- **Element pointers are only valid while the lock is held.** Once it is released, another thread can change the heap, which moves elements. `sof_heap_pop()` copies the element out, which is the safe way to take work off a shared heap.
- **Callbacks run inside the call that invokes them, while the caller holds the lock.** They must not take the lock again. Neither a `cmp_func` nor a `del_func` may call into the heap at all.
- **Anything an element points to belongs to the application.** The library copies an element's bytes but never follows pointers in them, so what they point to needs its own synchronization if threads share it, and the compare function must not read anything another thread may be changing.
