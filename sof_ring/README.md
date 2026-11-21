# sof_ring

A fixed-capacity circular buffer of fixed-size values in c.

The library has a `sof_ring_t` type that describes the ring as a whole. The application provides the `sof_ring_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_ring_init()`, which takes the size of each element, how many the ring holds, an optional delete function that is called with every element the ring drops, a _ctx_ passed to it, and an optional allocator. `sof_ring_push()` copies an element in at the back if there is room, and `sof_ring_push_overwrite()` does the same but, when the ring is full, replaces the oldest element. `sof_ring_pop()` takes the oldest element off the front, copying it out or, if asked to, dropping it. `sof_ring_peek()` returns the oldest element without removing it, `sof_ring_at()` returns the element at any position counting from the oldest, and `sof_ring_next()` steps from an element to the one after it. `sof_ring_traverse()` calls a callback for each element from oldest to newest. `sof_ring_clear()` removes every element, and `sof_ring_free()` releases the ring's memory once the application is finished with it.

The elements are kept in one array of _capacity_ elements, allocated by `sof_ring_init()`, which is the only allocation the library makes. The oldest element is at index `head`, and the others follow it, wrapping from the end of the array to the start. Pushing and popping only copy an element and move `head` or `count`, so they never allocate and never fail for lack of memory. The array never moves, so a pointer to an element, from `sof_ring_peek()`, `sof_ring_at()`, `sof_ring_next()` or a callback, stays good until that element is popped or overwritten; after that its slot is reused by a later push. Nothing points into the `sof_ring_t` itself, so it can be moved or copied, as long as only one copy is used afterwards.

The library's demo is the [tail](demo/) program in _demo/_, which prints the last lines of files, holding them in a ring that overwrites its oldest line and frees it through the delete function. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). The [match](../sof_regex/demo/) program, sof_regex's demo in _../sof_regex/demo/_, uses a ring to hold the lines before each selected line, for its `-B` option. All declarations are in _sof_ring.h_, and the implementation is in _sof_ring.c_. Memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond that, the library needs nothing but the c standard library.

    sof_ring_t r;
    sof_ring_init(&r, sizeof(double), 100, NULL, NULL, NULL);   // room for 100, no delete function, ctx or allocator
    while (read_sample(&sample)) {
        sof_ring_push_overwrite(&r, &sample);                   // keeps the latest 100 samples
    }
    for (size_t i = 0; i < r.count; i++) {
        use(*(double *)sof_ring_at(&r, i));                     // oldest first
    }
    sof_ring_free(&r);                                          // releases the array

## Building

    make             # compiles the library and builds ./test and the demo, demo/tail
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

Besides testing each function, the tests run the ring on threads as the Threading section below describes: producers and consumers sharing a small ring under a mutex and two condition variables, and readers walking a ring under a readers-writer lock while a writer overwrites and pops. The tests are linked with _-pthread_; the library itself doesn't use threads.

To use the library in another project, copy _sof_ring.c_ and _sof_ring.h_, and _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into the project and compile the _.c_ files with its own sources. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_, _sof_ring.c_ and _sof_alloc.c_ with _-pthread_.

## Functions

`sof_ring_error_t sof_ring_init(sof_ring_t *r, size_t element_size, size_t capacity, sof_ring_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc)`

Initialize the ring _r_ the caller provides, for up to _capacity_ elements of _element_size_ bytes. The array is allocated here, once, and nothing else in the library allocates. The _del_func_ is an optional function that is called with each element removed without being copied out to the caller: by `sof_ring_clear()`, `sof_ring_free()`, `sof_ring_pop()` given no place to copy it, and `sof_ring_push_overwrite()` replacing the oldest element. This gives the caller an opportunity to free what the element owns. _ctx_ is passed to every call of _del_func_, so it can use application state without globals; it can be **NULL**. _alloc_ is the allocator for the array; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). It is copied into the ring, so it can be on the stack. Returns **SOF_RING_OK**, **SOF_RING_NO_MEM** if the array couldn't be allocated, or **SOF_RING_ERROR** if _r_ is **NULL**, _element_size_ or _capacity_ is **0**, or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the ring is left empty and not initialized.

`sof_ring_error_t sof_ring_push(sof_ring_t *r, const void *element)`

Copy the _element_size_ bytes at _element_ into the ring at the back, as the newest element, if there is room. _element_ may point to an element of the ring itself, as from `sof_ring_peek()`, to push a copy of it. Returns **SOF_RING_OK**, **SOF_RING_FULL** if the ring already holds _capacity_ elements, in which case it is unchanged, or **SOF_RING_ERROR** if _element_ is **NULL**, or _r_ is **NULL** or not initialized.

`sof_ring_error_t sof_ring_push_overwrite(sof_ring_t *r, const void *element)`

Copy the _element_size_ bytes at _element_ into the ring at the back, as `sof_ring_push()` does, but if the ring is full, replace the oldest element: it goes to the _del_func_, if there is one, and its slot takes the new element, which becomes the newest. The ring then always holds the latest _capacity_ elements pushed. _element_ may point to an element of the ring itself; if it is the oldest element of a full ring, that element simply becomes the newest, without going to the _del_func_. A caller that needs to know whether an element was replaced can compare `count` with `capacity` first. Returns **SOF_RING_OK**, or **SOF_RING_ERROR** if _element_ is **NULL**, or _r_ is **NULL** or not initialized.

`sof_ring_error_t sof_ring_pop(sof_ring_t *r, void *element)`

Remove the oldest element, at the front of the ring. If _element_ is not **NULL**, the oldest element's _element_size_ bytes are copied there and the caller takes them over; the _del_func_ isn't called. If _element_ is **NULL**, the element goes to the _del_func_, if there is one, and is dropped. Returns **SOF_RING_OK**, **SOF_RING_NO_MATCH** if the ring is empty, or **SOF_RING_ERROR** if _r_ is **NULL** or not initialized; in both of those cases _element_ is left unchanged.

`void *sof_ring_peek(const sof_ring_t *r)`

Return a pointer to the oldest element, the next to be popped, without removing it. The application may read and change the element through it. The pointer is good until the element is popped or overwritten. Returns **NULL** if the ring is empty.

`void *sof_ring_at(const sof_ring_t *r, size_t index)`

Return a pointer to the element at position _index_, counting from the oldest: **0** is the oldest, the same as `sof_ring_peek()`, and `count - 1` is the newest, the last pushed. The application may read and change the element through it, and the pointer is good until the element is popped or overwritten. Returns **NULL** if _index_ is `count` or more.

`void *sof_ring_next(const sof_ring_t *r, const void *element)`

Return the element after _element_, toward the newest, or **NULL** if _element_ is the newest one. Together with `sof_ring_peek()` this walks the ring from oldest to newest without a callback:

    for (e = sof_ring_peek(&r); e != NULL; e = sof_ring_next(&r, e)) { ... }

Also returns **NULL** if _element_ is **NULL** or isn't the start of an element of _r_, as when it was popped.

`int sof_ring_traverse(const sof_ring_t *r, sof_ring_traverse_func_t cb, void *ctx)`

Visit the elements from oldest to newest, calling _cb_ with a pointer to each element and _ctx_. A **NULL** _cb_ visits nothing. If the callback returns a non-zero value, the traversal stops and returns that value. Otherwise **0** is returned once every element has been visited, or **-1** if _r_ is **NULL** or not initialized. An empty ring visits nothing and returns **0**.

`sof_ring_error_t sof_ring_clear(sof_ring_t *r)`

Remove every element, from oldest to newest, leaving an empty ring that can still be used. The _del_func_, if not **NULL**, is called for each element before it is removed. The array is kept. Returns **SOF_RING_OK**, or **SOF_RING_ERROR** if _r_ is **NULL** or not initialized.

`sof_ring_error_t sof_ring_free(sof_ring_t *r)`

Remove every element, as `sof_ring_clear()` does, and release the array. The `sof_ring_t` itself belongs to the caller and isn't freed; it is left empty. Returns **SOF_RING_OK**, or **SOF_RING_ERROR** if _r_ is **NULL** or not initialized.

Every function accepts a **NULL** _r_: `sof_ring_peek()`, `sof_ring_at()` and `sof_ring_next()` return **NULL**, `sof_ring_traverse()` returns **-1**, and the others return **SOF_RING_ERROR**. A ring that was freed, or whose `sof_ring_init()` failed, is treated the same way: it is empty, and `sof_ring_push()` and `sof_ring_push_overwrite()` fail, until it is initialized again.

## Types

    typedef enum sof_ring_error_t {
        SOF_RING_OK = 0,
        SOF_RING_NO_MEM,
        SOF_RING_NO_MATCH,
        SOF_RING_ERROR,
        SOF_RING_FULL,              // sof_ring_push() found the ring full
    } sof_ring_error_t;

    typedef struct sof_ring_t {
        unsigned char *elements;    // capacity * element_size bytes
        size_t element_size;        // size of each element in bytes
        size_t capacity;            // number of elements the ring holds when full
        size_t count;               // number of elements
        size_t head;                // index in elements of the oldest element
        sof_ring_element_delete_func_t del_func;
        void *ctx;                  // passed to del_func
        sof_alloc_t alloc;          // copy of the allocator; zeroed for the standard functions
        int ready;                  // 1 once initialized, 0 when freed or init failed
    } sof_ring_t;

The fields of `sof_ring_t` can be read but should not be changed by the application. The elements themselves can be read and changed in place, through the pointers the functions return.

    typedef void (*sof_ring_element_delete_func_t)(void *element, void *ctx)

Receives each element just before it is removed without being copied out: by `sof_ring_clear()` and `sof_ring_free()`, from oldest to newest, by `sof_ring_pop()` with a **NULL** _element_, and by `sof_ring_push_overwrite()` for the oldest element it replaces. _element_ points to the element's bytes in the ring, and the application can free what the element owns here. It is optional: pass **NULL** to `sof_ring_init()` if there is nothing to free. It must not call into the ring. _ctx_ is the context pointer given to `sof_ring_init()`.

    typedef int (*sof_ring_traverse_func_t)(void *element, void *ctx)

Called for each _element_ visited by `sof_ring_traverse()`, with the _ctx_ given to it. It may change the element's bytes. Return **0** to continue, or a non-zero value to stop the traversal early; that value is returned by `sof_ring_traverse()`. It must not push onto or pop from the ring.

## Threading

The library does no locking of its own. It has no global state, so different rings can be used on different threads freely, but a ring shared between threads needs a lock supplied by the application.

**Read-only** (any number of threads at once, as long as no thread is writing): `sof_ring_peek()`, `sof_ring_at()`, `sof_ring_next()`, `sof_ring_traverse()` with a callback that doesn't change the elements, and reading `count`. These functions take a `const sof_ring_t *`, but the elements they return, or pass to the callback, aren't `const`, since the application may change them.

**Writes** (one thread, with no readers): `sof_ring_init()`, `sof_ring_push()`, `sof_ring_push_overwrite()`, `sof_ring_pop()`, `sof_ring_clear()` and `sof_ring_free()`, and changing an element in place.

A ring shared between a producer and a consumer is written by both sides, so a plain mutex fits better than a readers-writer lock. Because the ring is bounded, there are two things to wait for: consumers wait for an element, and producers, using `sof_ring_push()`, wait for room. Pair the mutex with a condition variable for each:

    // Producer
    pthread_mutex_lock(&lock);
    while (sof_ring_push(&r, &job) == SOF_RING_FULL) {
        pthread_cond_wait(&not_full, &lock);
    }
    pthread_cond_signal(&not_empty);
    pthread_mutex_unlock(&lock);

    // Consumer
    job_t job;
    pthread_mutex_lock(&lock);
    while (sof_ring_pop(&r, &job) == SOF_RING_NO_MATCH && !stopping) {
        pthread_cond_wait(&not_empty, &lock);
    }
    pthread_cond_signal(&not_full);
    pthread_mutex_unlock(&lock);

A producer that must never wait, such as one logging from a time-critical thread, can use `sof_ring_push_overwrite()` instead, giving up the oldest elements when the consumer falls behind.

Some things to watch for:

- **Element pointers are only valid while the lock is held.** Once it is released, another thread can pop the element, or overwrite it. `sof_ring_pop()` copies the element out, which is the safe way to take work off a shared ring.
- **Callbacks run inside the call that invokes them, while the caller holds the lock.** They must not take the lock again. A `del_func` must not call into the ring at all.
- **Anything an element points to belongs to the application.** The library copies an element's bytes but never follows pointers in them, so what they point to needs its own synchronization if threads share it.
