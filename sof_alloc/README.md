# sof_alloc

Replaceable memory allocation for the libraries in this collection, in c.

The library has a `sof_alloc_t` type that names the functions to allocate and free memory with, and a _ctx_ for them. The application fills in a `sof_alloc_t` (on the stack is fine) and passes it to the `init` function of each library it wants to use it, such as `sof_hashmap_init()`, `sof_rbtree_init()` or `sof_config_init()`, which keep a copy. **NULL**, or a `sof_alloc_t` whose _malloc_ is **NULL**, means the standard `malloc()`, `realloc()` and `free()`. The libraries allocate through `sof_alloc_malloc()`, `sof_alloc_calloc()`, `sof_alloc_realloc()` and `sof_alloc_strndup()`, and free through `sof_alloc_free()`, always giving the size the block was allocated with. `sof_alloc_valid()` checks that a `sof_alloc_t` can be used. An application can call these helpers too, to allocate with the same allocator. `sof_alloc_malloc()` and `sof_alloc_free()`, through which every allocation and free passes, are `static inline` in _sof_alloc.h_, which saves a call each time: through a slab allocator, an allocation and its free went from 2.53 to 2.33 ns on an Apple M4 Max and from 2.75 to 2.40 on an Intel i9.

Since every free comes with its size, an allocator that keeps blocks of a few sizes, such as a pool or a slab allocator, knows where a block goes without storing a header in front of it. The _ctx_ lets one allocator serve several arenas or pools without globals.

The library's demo is the [memstat](demo/) program in _demo/_, which counts the words of a text through an allocator of its own that counts and checks every call the libraries make, and reports them. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All alloc declarations are in _sof_alloc.h_. The library needs nothing but the c standard library. The test harness in _../test/test.h_ has a test allocator built on it, which other libraries' tests use to fail chosen allocations, count blocks and check every size.

    static void *pool_malloc(size_t size, void *ctx) { ... }          // ctx is the pool
    static void pool_free_sized(void *p, size_t size, void *ctx) { ... }

    sof_alloc_t pool = {pool_malloc, NULL, NULL, pool_free_sized, &my_pool};  // no realloc: emulated
    sof_hashmap_t map;
    sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, &pool);   // the map and its keys use the pool

## Building

    make             # compiles the library and builds ./test and the demo, demo/memstat
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

Besides testing each function, the tests run the helpers on threads, as the Threading section below describes: threads allocating, resizing and freeing blocks at once through the standard functions, and through an application allocator they share, guarded by its own mutex, which must see every allocation, every size right and no leaks. The tests are linked with _-pthread_; the library itself doesn't use threads.

To use the library in another project, copy _sof_alloc.c_ and _sof_alloc.h_ into it. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_ and _sof_alloc.c_ with _-pthread_. The libraries that use it say so in their README and list it among the files to copy.

## Allocators

    typedef struct sof_alloc_t {
        void *(*malloc)(size_t size, void *ctx);
        void *(*realloc)(void *p, size_t old_size, size_t new_size, void *ctx);
        void (*free)(void *p, void *ctx);
        void (*free_sized)(void *p, size_t size, void *ctx);
        void *ctx;
    } sof_alloc_t;

Every function gets _ctx_. _malloc_ is required, and so is _free_ or _free_sized_; _realloc_ is optional.

- _malloc_ returns a block of at least _size_ bytes, aligned for any type, or **NULL**. It isn't called with a _size_ of **0**.
- _realloc_ resizes block _p_, of _old_size_ bytes, to _new_size_ bytes, moving it if need be and keeping its contents up to the smaller size, and returns it; or it returns **NULL** and leaves _p_ as it was. _p_ is never **NULL** and neither size is **0**. With **NULL**, `sof_alloc_realloc()` allocates a new block, copies, and frees the old one.
- _free_sized_ frees block _p_, which was allocated with _size_ bytes: the size last asked of _malloc_ or _realloc_ for it.
- _free_ frees block _p_, and is used only when there is no _free_sized_. An allocator that doesn't need sizes, such as one wrapping the standard `free()`, can give just this.

Neither free function is called with a **NULL** _p_. The libraries keep a copy of the `sof_alloc_t` they are given, so it can go out of scope, but whatever _ctx_ points to must last as long as they use it.

## Functions

`void *sof_alloc_malloc(const sof_alloc_t *alloc, size_t size)`

Allocate a block of _size_ bytes with _alloc_. Returns the block, or **NULL** if it couldn't be allocated or _size_ is **0**.

`void *sof_alloc_calloc(const sof_alloc_t *alloc, size_t count, size_t size)`

Allocate a zeroed array of _count_ elements of _size_ bytes, which is freed with the size _count_ * _size_. Returns the array, or **NULL** if it couldn't be allocated, or _count_ * _size_ is **0** or overflows. The standard functions use `calloc()`; an allocator's _malloc_ is followed by `memset()`.

`void *sof_alloc_realloc(const sof_alloc_t *alloc, void *p, size_t old_size, size_t new_size)`

Resize block _p_, allocated with _old_size_ bytes, to _new_size_ bytes; it may move. A **NULL** _p_ allocates a new block. Returns the block, or **NULL**, leaving _p_ as it was, if it couldn't be allocated or _new_size_ is **0**.

`char *sof_alloc_strndup(const sof_alloc_t *alloc, const char *s, size_t length)`

Copy _length_ bytes of _s_ into a new NUL-terminated string of _length_ + 1 bytes, which is the size to free it with. Returns the copy, or **NULL** if it couldn't be allocated or _s_ is **NULL**.

`void sof_alloc_free(const sof_alloc_t *alloc, void *p, size_t size)`

Free block _p_, which was allocated from _alloc_ with _size_ bytes, through _free_sized_ if _alloc_ has one, or _free_. A **NULL** _p_ does nothing.

`int sof_alloc_valid(const sof_alloc_t *alloc)`

Returns **1** if _alloc_ can be used: it is **NULL**, has no _malloc_, or has a _malloc_ and a _free_ or _free_sized_. Returns **0** if it has a _malloc_ but no way to free. The libraries' `init` functions return their `ERROR` code for a `sof_alloc_t` that isn't valid.

Every function accepts a **NULL** _alloc_, meaning the standard functions.

## Types

`sof_alloc_t` is described under Allocators. The application owns it and may change it at any time; libraries that were given one use the copy they made in `init`.

## Threading

The library has no global state and does no locking. The standard functions are safe to call from any thread. An allocator of the application's own must be safe to call from every thread that uses a structure allocating with it, which, for a structure used only under the application's lock, means no more than that lock gives.
