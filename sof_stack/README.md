# sof_stack

A last-in, first-out stack of fixed-size values in c.

The library has a `sof_stack_t` type that describes the stack as a whole. The application provides the `sof_stack_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_stack_init()`, which takes the size of each element, how many to make room for at first, an optional delete function that is called with every element the stack drops, a _ctx_ passed to it, and an optional allocator. `sof_stack_push()` copies an element onto the top, and `sof_stack_pop()` takes the top one off, copying it out or, if asked to, dropping it. `sof_stack_peek()` returns the top element without removing it, and `sof_stack_next()` steps from an element to the one below it. `sof_stack_traverse()` calls a callback for each element from top to bottom. `sof_stack_truncate()` pops elements until a given number are left, `sof_stack_clear()` removes every element, and `sof_stack_free()` releases the stack's memory once the application is finished with it.

The elements are kept in one array, bottom first, which doubles in size when it fills, starting from the capacity given to `sof_stack_init()`, or from **16** elements. It never shrinks until `sof_stack_free()`. Because the array is reallocated when it grows, a pointer to an element, from `sof_stack_peek()`, `sof_stack_next()` or a callback, is good only until the next `sof_stack_push()`, or until that element is popped. Nothing points into the `sof_stack_t` itself, so it can be moved or copied, as long as only one copy is used afterwards.

The library's demo is the [calc](demo/) program in _demo/_, an expression calculator that evaluates on stacks. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All declarations are in _sof_stack.h_, and the Makefile builds _sof_stack.c_ into _libsof_stack.a_. Memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond that, the library needs nothing but the c standard library.

    typedef struct point_t { int x, y; } point_t;

    sof_stack_t s;
    sof_stack_init(&s, sizeof(point_t), 0, NULL, NULL, NULL);   // no initial room, delete function, ctx or allocator
    sof_stack_push(&s, &(point_t){0, 0});                      // copies the point onto the top
    point_t p;
    while (sof_stack_pop(&s, &p) == SOF_STACK_OK) {                 // copies the top point out
        visit(&s, p);                                   // may push more points
    }
    sof_stack_free(&s);                                       // releases the array

## Building

    make             # builds libsof_stack.a, ./test and the demo, demo/calc
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make lib         # builds libsof_stack.a, as make does
    make debug=1     # builds with debug info and without optimization

Besides testing each function, the tests run the stack on threads as the Threading section below describes: producers and consumers sharing a stack under a mutex and condition variable, and readers walking a stack under a readers-writer lock while a writer changes it. The tests are linked with _-pthread_; the library itself doesn't use threads.

A program linked with the archive gets only the objects it uses. To use the library in another project, link with _libsof_stack.a_ and _sof_alloc.c_ from _../sof_alloc/_, with _sof_stack.h_ and _sof_alloc.h_ on the include path, or copy _sof_stack.c_ and _sof_stack.h_, and _sof_alloc.c_ and _sof_alloc.h_, into the project and compile the _.c_ files with its own sources. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_ with _-pthread_, linking it with _libsof_stack.a_ and _sof_alloc.c_ or compiling it with _sof_stack.c_ and _sof_alloc.c_.

## Functions

`sof_stack_error_t sof_stack_init(sof_stack_t *s, size_t element_size, size_t capacity, sof_stack_element_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc)`

Initialize the stack _s_ the caller provides, for elements of _element_size_ bytes. _capacity_ is the number of elements to make room for now, or **0** to allocate when the first is pushed; the array doubles as it fills, so this only saves reallocations. The _del_func_ is an optional function that is called with each element removed without being copied out to the caller: by `sof_stack_clear()`, `sof_stack_free()`, `sof_stack_truncate()`, and `sof_stack_pop()` given no place to copy it. This gives the caller an opportunity to free what the element owns. _ctx_ is passed to every call of _del_func_, so it can use application state without globals; it can be **NULL**. _alloc_ is the allocator for the array; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). It is copied into the stack, so it can be on the stack. Returns **SOF_STACK_OK**, **SOF_STACK_NO_MEM** if room for _capacity_ elements couldn't be allocated, or **SOF_STACK_ERROR** if _s_ is **NULL**, _element_size_ is **0**, or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the stack is left empty and not initialized.

`sof_stack_error_t sof_stack_push(sof_stack_t *s, const void *element)`

Copy the _element_size_ bytes at _element_ onto the top of the stack. If the array is full it is reallocated at twice the size, which moves every element. _element_ may point to an element of the stack itself, as from `sof_stack_peek()`, to push a copy of it. Returns **SOF_STACK_OK**, **SOF_STACK_NO_MEM** if the array couldn't grow, in which case the stack is unchanged, or **SOF_STACK_ERROR** if _element_ is **NULL**, or _s_ is **NULL** or not initialized.

`sof_stack_error_t sof_stack_pop(sof_stack_t *s, void *element)`

Remove the element at the top of the stack, the newest one. If _element_ is not **NULL**, the top element's _element_size_ bytes are copied there and the caller takes them over; the _del_func_ isn't called. If _element_ is **NULL**, the element goes to the _del_func_, if there is one, and is dropped. The array doesn't shrink. Returns **SOF_STACK_OK**, **SOF_STACK_NO_MATCH** if the stack is empty, or **SOF_STACK_ERROR** if _s_ is **NULL** or not initialized; in both of those cases _element_ is left unchanged.

`void *sof_stack_peek(const sof_stack_t *s)`

Return a pointer to the element at the top of the stack, the next to be popped, without removing it. The application may read and change the element through it. The pointer is good until the next `sof_stack_push()`, or until the element is popped. Returns **NULL** if the stack is empty.

`void *sof_stack_next(const sof_stack_t *s, const void *element)`

Return the element below _element_, toward the bottom, or **NULL** if _element_ is the bottom one. Together with `sof_stack_peek()` this walks the stack from newest to oldest without a callback:

    for (e = sof_stack_peek(&s); e != NULL; e = sof_stack_next(&s, e)) { ... }

Also returns **NULL** if _element_ is **NULL** or isn't the start of an element of _s_, as when it was popped.

`int sof_stack_traverse(const sof_stack_t *s, sof_stack_traverse_func_t cb, void *ctx)`

Visit the elements from top to bottom, calling _cb_ with a pointer to each element and _ctx_. A **NULL** _cb_ visits nothing. If the callback returns a non-zero value, the traversal stops and returns that value. Otherwise **0** is returned once every element has been visited, or **-1** if _s_ is **NULL** or not initialized. An empty stack visits nothing and returns **0**.

`sof_stack_error_t sof_stack_truncate(sof_stack_t *s, size_t count)`

Pop elements from the top until _count_ are left, counting from the bottom, as `sof_stack_pop()` with a **NULL** _element_ would: the _del_func_, if not **NULL**, is called for each element before it is removed, from top to bottom. This is for unwinding to a depth recorded earlier, such as the loops a subroutine started when it returns. A _count_ at or above the number of elements removes nothing, and a _count_ of **0** empties the stack, as `sof_stack_clear()` does. The array doesn't shrink. Returns **SOF_STACK_OK**, or **SOF_STACK_ERROR** if _s_ is **NULL** or not initialized.

`sof_stack_error_t sof_stack_clear(sof_stack_t *s)`

Remove every element, from top to bottom, leaving an empty stack that can still be used. The _del_func_, if not **NULL**, is called for each element before it is removed. The array is kept, so pushing up to `capacity` elements again allocates nothing. Returns **SOF_STACK_OK**, or **SOF_STACK_ERROR** if _s_ is **NULL** or not initialized.

`sof_stack_error_t sof_stack_free(sof_stack_t *s)`

Remove every element, as `sof_stack_clear()` does, and release the array. The `sof_stack_t` itself belongs to the caller and isn't freed; it is left empty. Returns **SOF_STACK_OK**, or **SOF_STACK_ERROR** if _s_ is **NULL** or not initialized.

Every function accepts a **NULL** _s_: `sof_stack_peek()` and `sof_stack_next()` return **NULL**, `sof_stack_traverse()` returns **-1**, and the others return **SOF_STACK_ERROR**. A stack that was freed, or whose `sof_stack_init()` failed, is treated the same way: it is empty, and `sof_stack_push()` fails, until it is initialized again.

## Types

    typedef enum sof_stack_error_t {
        SOF_STACK_OK = 0,
        SOF_STACK_NO_MEM,
        SOF_STACK_NO_MATCH,
        SOF_STACK_ERROR,
    } sof_stack_error_t;

    typedef struct sof_stack_t {
        unsigned char *elements;    // capacity * element_size bytes, bottom first
        size_t element_size;        // size of each element in bytes
        size_t count;               // number of elements
        size_t capacity;            // number of elements the array has room for
        sof_stack_element_delete_func_t del_func;
        void *ctx;                  // passed to del_func
        sof_alloc_t alloc;               // copy of the allocator; zeroed for the standard functions
        int ready;                  // 1 once initialized, 0 when freed or init failed
    } sof_stack_t;

The fields of `sof_stack_t` can be read but should not be changed by the application. The elements themselves, `count` of them from `elements`, can be read and changed in place.

    typedef void (*sof_stack_element_delete_func_t)(void *element, void *ctx)

Receives each element just before it is removed without being copied out: by `sof_stack_clear()`, `sof_stack_free()` and `sof_stack_truncate()`, from top to bottom, and `sof_stack_pop()` with a **NULL** _element_. _element_ points to the element's bytes in the stack, and the application can free what the element owns here. It is optional: pass **NULL** to `sof_stack_init()` if there is nothing to free. It must not call into the stack. _ctx_ is the context pointer given to `sof_stack_init()`.

    typedef int (*sof_stack_traverse_func_t)(void *element, void *ctx)

Called for each _element_ visited by `sof_stack_traverse()`, with the _ctx_ given to it. It may change the element's bytes. Return **0** to continue, or a non-zero value to stop the traversal early; that value is returned by `sof_stack_traverse()`. It must not push onto or pop from the stack.

## Threading

The library does no locking of its own. It has no global state, so different stacks can be used on different threads freely, but a stack shared between threads needs a lock supplied by the application.

**Read-only** (any number of threads at once, as long as no thread is writing): `sof_stack_peek()`, `sof_stack_next()`, `sof_stack_traverse()` with a callback that doesn't change the elements, and reading `count`. These functions take a `const sof_stack_t *`, but the elements they return, or pass to the callback, aren't `const`, since the application may change them.

**Writes** (one thread, with no readers): `sof_stack_init()`, `sof_stack_push()`, `sof_stack_pop()`, `sof_stack_truncate()`, `sof_stack_clear()` and `sof_stack_free()`, and changing an element in place.

A stack shared between threads is nearly always written by both sides, so a plain mutex fits better than a readers-writer lock. To let a consumer wait for work rather than poll, pair the mutex with a condition variable:

    // Producer
    pthread_mutex_lock(&lock);
    sof_stack_push(&s, &job);
    pthread_cond_signal(&ready);
    pthread_mutex_unlock(&lock);

    // Consumer
    job_t job;
    pthread_mutex_lock(&lock);
    while (sof_stack_pop(&s, &job) == SOF_STACK_NO_MATCH && !stopping) {
        pthread_cond_wait(&ready, &lock);
    }
    pthread_mutex_unlock(&lock);

Some things to watch for:

- **Element pointers are only valid while the lock is held.** Once it is released, another thread can push, which may move the array, or pop the element. `sof_stack_pop()` copies the element out, which is the safe way to take work off a shared stack.
- **Callbacks run inside the call that invokes them, while the caller holds the lock.** They must not take the lock again. A `del_func` must not call into the stack at all.
- **Anything an element points to belongs to the application.** The library copies an element's bytes but never follows pointers in them, so what they point to needs its own synchronization if threads share it.
