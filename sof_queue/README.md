# sof_queue

A first-in, first-out queue in c.

The library has a `sof_queue_t` type that describes the queue as a whole. The application provides the `sof_queue_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_queue_init()`, which takes an optional delete function that is called with every element the queue drops, a _ctx_ passed to it, and an optional allocator for the elements. Elements are added at the tail with `sof_queue_enqueue()` and taken from the head with `sof_queue_dequeue()`, which hands the element's data back or, if asked to, drops it. `sof_queue_peek()` returns the head element without removing it, and `sof_queue_next()` steps from an element to the one after it, toward the tail. `sof_queue_traverse()` calls a callback for each element from head to tail. `sof_queue_shrink()` gives back the memory of spare elements after a burst, `sof_queue_clear()` removes every element, and `sof_queue_free()` releases the queue's memory once the application is finished with it.

The queue is a singly linked list. Each element is allocated separately and never moves, so an element pointer stays good until that element is removed. A dequeued element isn't freed: the queue keeps it on a list of spares, and the next enqueue takes a spare before it allocates. A queue that stays about the same size therefore stops allocating once it has grown, without the application supplying a pool; a steady queue of 1,000 elements runs at about 3.5 ns per enqueue and dequeue, against 8.4 when each element was allocated and freed, on an Apple M4 Max, and at 2.4 against 4.5 on an Intel i9 with glibc. The cost is that a queue holds memory for the most elements it has held at once until `sof_queue_shrink()`, `sof_queue_clear()` or `sof_queue_free()` frees the spares. Nothing points into the `sof_queue_t` itself, so it can be moved or copied, as long as only one copy is used afterwards.

The library's demo is the [bank](demo/) program in _demo/_, which simulates customers queuing for bank tellers. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All queue declarations are in _sof_queue.h_. Memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond that, the library needs nothing but the c standard library.

    sof_queue_t q;
    sof_queue_init(&q, NULL, NULL, NULL);        // no delete function, ctx or allocator
    sof_queue_enqueue(&q, job);                  // add at the tail
    ...
    void *next;
    while (sof_queue_dequeue(&q, &next) == SOF_QUEUE_OK) {
        run(next);                          // oldest first
    }
    sof_queue_free(&q);                          // removes any elements left

## Building

    make             # compiles the library and builds ./test and the demo, demo/bank
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

Besides testing each function, including that enqueues reuse spare elements without allocating and that `sof_queue_shrink()` frees only the spares, the tests fail each allocation in turn with dequeues mixed in, and run the queue on threads as the Threading section below describes: producers and consumers sharing a queue under a mutex and condition variable, and readers walking a queue under a readers-writer lock while a writer changes it. The tests are linked with _-pthread_; the library itself doesn't use threads.

To use the library in another project, copy _sof_queue.c_ and _sof_queue.h_, and _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into it and compile the _.c_ files with the project's own sources. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_, _sof_queue.c_ and _sof_alloc.c_ with _-pthread_.

## Functions

`sof_queue_error_t sof_queue_init(sof_queue_t *q, sof_queue_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc)`

Initialize the queue _q_ the caller provides. The _del_func_ is an optional function that is called with each element removed without its data being handed back to the caller: by `sof_queue_clear()`, `sof_queue_free()`, and `sof_queue_dequeue()` given no place to put the data. This gives the caller an opportunity to free the element's data. _ctx_ is passed to every call of _del_func_, so it can use application state without globals; it can be **NULL**. _alloc_ is the allocator for elements; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). It is copied into the queue, so it can be on the stack. Returns **SOF_QUEUE_OK**, or **SOF_QUEUE_ERROR** if _q_ is **NULL**, or _alloc_ has a _malloc_ but no _free_ or _free_sized_, in which case the queue is left empty and not initialized.

`sof_queue_error_t sof_queue_enqueue(sof_queue_t *q, void *data)`

Add an element holding _data_ at the tail of the queue. _data_ can be **NULL**; the library never reads it. The element is a spare left by `sof_queue_dequeue()` if there is one, and is allocated otherwise. Returns **SOF_QUEUE_OK**, **SOF_QUEUE_NO_MEM** if the element couldn't be allocated, in which case the queue is unchanged, or **SOF_QUEUE_ERROR** if _q_ is **NULL** or not initialized.

`sof_queue_error_t sof_queue_dequeue(sof_queue_t *q, void **data)`

Remove the element at the head of the queue, the oldest one. If _data_ is not **NULL**, the element's data is stored there and the caller takes it over; the _del_func_ isn't called. If _data_ is **NULL**, the element goes to the _del_func_, if there is one, and is dropped. Either way the element is kept as a spare for a later `sof_queue_enqueue()`, not freed. Returns **SOF_QUEUE_OK**, **SOF_QUEUE_NO_MATCH** if the queue is empty, or **SOF_QUEUE_ERROR** if _q_ is **NULL** or not initialized; in both of those cases _data_ is left unchanged. Since an empty queue is told apart by the return value, **NULL** data can be queued like any other.

`sof_queue_element_t *sof_queue_peek(const sof_queue_t *q)`

Return the element at the head of the queue, the next to be dequeued, without removing it. Returns **NULL** if the queue is empty.

`sof_queue_element_t *sof_queue_next(const sof_queue_t *q, const sof_queue_element_t *element)`

Return the element after _element_, toward the tail, or **NULL** if _element_ is the tail (or is **NULL**). Together with `sof_queue_peek()` this walks the queue from oldest to newest without a callback:

    for (e = sof_queue_peek(&q); e != NULL; e = sof_queue_next(&q, e)) { ... }

Dequeuing the current element while walking invalidates it, so get the next element first.

`int sof_queue_traverse(const sof_queue_t *q, sof_queue_traverse_func_t cb, void *ctx)`

Visit the elements from head to tail, calling _cb_ with each element and _ctx_. A **NULL** _cb_ visits nothing. If the callback returns a non-zero value, the traversal stops and returns that value. Otherwise **0** is returned once every element has been visited, or **-1** if _q_ is **NULL** or not initialized. An empty queue visits nothing and returns **0**.

`sof_queue_error_t sof_queue_shrink(sof_queue_t *q)`

Free the spare elements that `sof_queue_dequeue()` kept for reuse, giving back the memory a queue held at its largest. The elements in the queue are untouched and the _del_func_ isn't called. Returns **SOF_QUEUE_OK**, or **SOF_QUEUE_ERROR** if _q_ is **NULL** or not initialized.

`sof_queue_error_t sof_queue_clear(sof_queue_t *q)`

Remove every element, from head to tail, leaving an empty queue that can still be used. The _del_func_, if not **NULL**, is called for each element before it is removed. The elements and any spares are freed. Returns **SOF_QUEUE_OK**, or **SOF_QUEUE_ERROR** if _q_ is **NULL** or not initialized.

`sof_queue_error_t sof_queue_free(sof_queue_t *q)`

Remove every element, as `sof_queue_clear()` does, and release the queue's memory. The `sof_queue_t` itself belongs to the caller and isn't freed; it is left empty. Returns **SOF_QUEUE_OK**, or **SOF_QUEUE_ERROR** if _q_ is **NULL** or not initialized.

Every function accepts a **NULL** _q_: `sof_queue_peek()` and `sof_queue_next()` return **NULL**, `sof_queue_traverse()` returns **-1**, and the others return **SOF_QUEUE_ERROR**. A queue that was freed, or whose `sof_queue_init()` failed, is treated the same way: it is empty, and `sof_queue_enqueue()` fails, until it is initialized again.

## Types

    typedef enum sof_queue_error_t {
        SOF_QUEUE_OK = 0,
        SOF_QUEUE_NO_MEM,
        SOF_QUEUE_NO_MATCH,
        SOF_QUEUE_ERROR,
    } sof_queue_error_t;

    typedef struct sof_queue_element_t {
        struct sof_queue_element_t *next;
        void *data;
    } sof_queue_element_t;

An element of the queue. The application may read and write `data`, which the library never touches, and read `next`, but must not change it. An element pointer is good until that element is removed; after that the queue may reuse the element for a later enqueue.

    typedef struct sof_queue_t {
        sof_queue_element_t *head;       // oldest element, next to be dequeued
        sof_queue_element_t *tail;       // newest element
        size_t count;               // number of elements
        sof_queue_element_t *spare;      // dequeued elements kept for reuse
        size_t spare_count;         // number of spares
        sof_queue_element_delete_func_t del_func;
        void *ctx;                  // passed to del_func
        sof_alloc_t alloc;          // copy of the allocator; zeroed for the standard functions
        int ready;                  // 1 once initialized, 0 when freed or init failed
    } sof_queue_t;

The fields of `sof_queue_t` can be read but should not be changed by the application.

    typedef void (*sof_queue_element_delete_func_t)(sof_queue_element_t *element, void *ctx)

Receives each element just before it is removed without its data being handed back: by `sof_queue_clear()`, `sof_queue_free()`, and `sof_queue_dequeue()` with a **NULL** _data_. The application can free the element's `data` here. It is optional: pass **NULL** to `sof_queue_init()` if there is nothing to free. It must not call into the queue. _ctx_ is the context pointer given to `sof_queue_init()`.

    typedef int (*sof_queue_traverse_func_t)(sof_queue_element_t *element, void *ctx)

Called for each _element_ visited by `sof_queue_traverse()`, with the _ctx_ given to it. Return **0** to continue, or a non-zero value to stop the traversal early; that value is returned by `sof_queue_traverse()`. It must not add to or remove from the queue.

## Threading

The library does no locking of its own. It has no global state, so different queues can be used on different threads freely, but a queue shared between threads needs a lock supplied by the application.

**Read-only** (any number of threads at once, as long as no thread is writing): `sof_queue_peek()`, `sof_queue_next()`, `sof_queue_traverse()`, and reading `count`. These functions take a `const sof_queue_t *`, but the elements they return, or pass to the callback, aren't `const`, since the application may change their `data`.

**Writes** (one thread, with no readers): `sof_queue_init()`, `sof_queue_enqueue()`, `sof_queue_dequeue()`, `sof_queue_shrink()`, `sof_queue_clear()` and `sof_queue_free()`.

A queue passing work between threads is nearly always written by both sides, so a plain mutex fits better than a readers-writer lock. To let a consumer wait for work rather than poll, pair the mutex with a condition variable:

    // Producer
    pthread_mutex_lock(&lock);
    sof_queue_enqueue(&q, job);
    pthread_cond_signal(&ready);
    pthread_mutex_unlock(&lock);

    // Consumer
    void *job;
    pthread_mutex_lock(&lock);
    while (sof_queue_dequeue(&q, &job) == SOF_QUEUE_NO_MATCH && !stopping) {
        pthread_cond_wait(&ready, &lock);
    }
    pthread_mutex_unlock(&lock);

Some things to watch for:

- **Element pointers are only valid while the lock is held.** Once it is released, another thread can dequeue the element. `sof_queue_dequeue()` hands back the data itself, which is the safe way to take work off a shared queue.
- **Callbacks run inside the call that invokes them, while the caller holds the lock.** They must not take the lock again. A `del_func` must not call into the queue at all.
- **`data` belongs to the application.** The library never reads or writes it, so whatever it points to needs its own synchronization if threads share it after it leaves the queue.
