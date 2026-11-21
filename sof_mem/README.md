# sof_mem

Memory allocators tuned to different purposes, which can be used side by side or stacked, each usable as an allocator for the other libraries, in c.

The library has four allocators, each set up by the application in a structure it provides (on the stack, inside another structure or on the heap) and each taking an optional parent allocator, a `sof_alloc_t` from [sof_alloc](../sof_alloc/), to get its memory from:

- **`sof_mem_arena_t`**, a bump allocator, for memory that is freed all at once. `sof_mem_arena_init()` sets it up, and `sof_mem_arena_init_buffer()` sets it up in a buffer of the application's, with no parent at all. `sof_mem_arena_alloc()` allocates, `sof_mem_arena_realloc()` resizes, and `sof_mem_arena_release()` gives a block back, which reclaims it only in the cases described below. `sof_mem_arena_mark()` notes a point that `sof_mem_arena_reset()` goes back to, freeing everything allocated since, and `sof_mem_arena_clear()` goes back to the start.
- **`sof_mem_pool_t`**, for objects of one size, freed one at a time. `sof_mem_pool_init()` sets it up for a size, `sof_mem_pool_alloc()` hands out an object and `sof_mem_pool_release()` takes one back.
- **`sof_mem_slab_t`**, for small blocks of any size, freed one at a time: a pool for each of a range of size classes. `sof_mem_slab_init()` sets it up, and `sof_mem_slab_alloc()`, `sof_mem_slab_realloc()` and `sof_mem_slab_release()` work like the arena's.
- **`sof_mem_track_t`**, which passes everything on to its parent, counting what is live and its peak, and catching frees of the wrong size or of blocks it never allocated. `sof_mem_track_init()` sets it up, with an optional function to tell of each problem; `sof_mem_track_alloc()`, `sof_mem_track_realloc()` and `sof_mem_track_release()` work as the others' do, and `sof_mem_track_traverse()` visits every block still live, which at the end of a program are its leaks.

Each has a function returning a `sof_alloc_t` of it (`sof_mem_arena_allocator()`, `sof_mem_pool_allocator()`, `sof_mem_slab_allocator()`, `sof_mem_track_allocator()`) to pass to the `init` function of any library here that allocates, such as _sof_rbtree_ or _sof_hashmap_, without changing it, or to another of these allocators as its parent. Each has a `clear` function that takes back everything at once, keeping the memory for reuse (the tracker's frees it), and a `free` function that gives everything back to the parent when the application is finished.

**Which to use.** A structure that is built, used and thrown away, such as everything a server allocates for one request, belongs in an arena: allocation is a pointer bump and freeing is nothing, until the arena is cleared. Marks do the same for nested lifetimes, like a stack. A structure whose blocks come and go one at a time needs a pool, if they are all one size (_sof_rbtree_'s nodes), or a slab allocator, if they aren't (_sof_hashmap_'s copied keys). A tracker goes around any allocator, the standard ones included, to see what a structure really uses, or in tests, to check that it frees everything with the right size.

**Stacking.** Since each takes a parent `sof_alloc_t` and gives one, they combine. A slab allocator over an arena frees blocks one at a time and gives everything back at once when the arena is cleared. Over a fixed arena, nothing calls `malloc()` at all. A tracker over either counts what the structure above it does. Each allocator holds the address of its parent's structure, through the parent's `sof_alloc_t`, so the parent must outlive it and not move.

## How they work

**Arena.** Standard blocks are _block_size_ usable bytes (**65536** by default). Allocations come from them one after another, each rounded up to the strictest alignment (**16** bytes on x86-64 Linux, **8** on arm64 macOS). When one doesn't fit in what is left of a block, the next block is used and the rest is wasted. An allocation larger than a quarter of _block_size_ gets a block of its own from the parent instead, so the waste is never more than a quarter of a block, and `sof_mem_arena_release()` gives such a block back to the parent at once. Releasing the most recent allocation takes its space back, so the next allocation reuses it, and `sof_mem_arena_realloc()` grows the most recent allocation in place while it fits; any other release keeps the space until the arena is cleared or reset. Standard blocks are never given back until `sof_mem_arena_free()`: clearing and resetting use them again from where they go back to, so an arena used the same way each time, such as once per request, asks its parent for nothing after the first. A mark records the arena's place; `sof_mem_arena_reset()` returns to it and gives back the large blocks allocated since. Making a mark stops the allocations before it from growing in place or being taken back, so a reset never cuts one short.

**Fixed arena.** `sof_mem_arena_init_buffer()` puts the arena's one block header at the start of the application's buffer, aligned, and allocates every size from the rest, with no large blocks and no parent. Once the buffer is full, allocations fail. Since an arena reclaims only the most recent block, anything stacked on a fixed arena should free little, or everything at once: a slab allocator over one should serve every size the program uses, so its own pools do the reusing.

**Pool.** Objects come from slabs of _per_slab_ objects (as many as fit in **65536** bytes by default), each object's space rounded up to the alignment and to at least a pointer. A released object goes onto a free list, threaded through the objects themselves, and is the next handed out, so a pool never wastes space on objects of its own size however they come and go. Its `sof_alloc_t` serves any size up to the object size from the pool and passes larger sizes to the parent, telling them apart on free by the size every `sof_alloc_free()` in this collection is given.

**Slab.** Sizes up to _max_size_ (**1024** by default, at most **8192**) are rounded up to a size class: every multiple of the alignment up to **128** bytes, then four classes between each power of two and the next (**160**, **192**, **224**, **256**, **320**, ...), so a block wastes less than the alignment or a fifth of its size. Each class is a pool, whose first slab is allocated only when the class is first used; _slab_size_ sets the slabs' size (**65536** by default), smaller for a small parent such as a fixed arena. Larger sizes go to the parent. Like the pool, it finds a block's class on free from its size, so blocks carry no header.

**Track.** Each block gets a hidden header before it, from the parent, holding its size, a mark that it is live, and links to the other live blocks. A free or realloc checks the header before doing anything: a wrong size is reported and the right one used, so the parent sees nothing wrong, and a pointer without a live header is reported and left alone. The check reads the memory before the pointer, so it catches most stray and double frees, but not every one of a block already given back to the parent; AddressSanitizer catches the rest.

None of the allocators moves a block once handed out. Their structures can be moved, except while a `sof_alloc_t` made from them is in use, since that holds their address.

_bench.c_ is a benchmark against the standard functions. The library's demo is the [concord](demo/) program in _demo/_, which makes a concordance of a text, keeping the words' text in an arena, each line in a second arena reset to a mark, and the tree's nodes, the words' records and their lists of lines in pools, and with an option reports them all through a tracker. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). The [bank](../sof_queue/demo/) program, sof_queue's demo in _../sof_queue/demo/_, keeps its events and customers in pools. The declarations are all in _sof_mem.h_; each allocator is in a file of its own (_sof_mem_arena.c_, _sof_mem_pool.c_, _sof_mem_slab.c_, _sof_mem_track.c_), and _sof_mem_internal.h_ holds what they share, all `static inline`, for the library's files only. A program can compile only the files of the allocators it uses, and in _build/libsof.a_ at the root each allocator is an object of its own, so a program linked with it gets only the allocators it uses. The parent allocator, and the `sof_alloc_t` each allocator makes, come from the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The benchmark, only, uses [sof_copt](../sof_copt/) in _../sof_copt/_ for its options, [sof_timer](../sof_timer/) in _../sof_timer/_ for timing, [sof_random](../sof_random/) in _../sof_random/_ for its random sizes and keys, and [sof_rbtree](../sof_rbtree/), [sof_hashmap](../sof_hashmap/) and [sof_hash](../sof_hash/) as the libraries it measures. Beyond that, the library needs nothing but the c standard library.

    sof_mem_arena_t scratch;
    sof_mem_arena_init(&scratch, 0, NULL);                  // default block size, standard parent
    sof_alloc_t scratch_alloc = sof_mem_arena_allocator(&scratch);

    for (each request) {
        sof_hashmap_t map;
        sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, &scratch_alloc);
        handle(request, &map);
        sof_hashmap_free(&map);                             // its frees cost almost nothing
        sof_mem_arena_clear(&scratch);                      // takes everything back, keeps the blocks
    }
    sof_mem_arena_free(&scratch);                           // gives the blocks back

    sof_mem_slab_t small;                                   // individual frees, for a long-lived map
    sof_mem_slab_init(&small, 0, 0, NULL);
    sof_mem_track_t counted;                                // to see what it uses
    sof_alloc_t small_alloc = sof_mem_slab_allocator(&small);
    sof_mem_track_init(&counted, NULL, NULL, &small_alloc);
    sof_alloc_t alloc = sof_mem_track_allocator(&counted);
    sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, &alloc);
    ...
    printf("peak %zu bytes in %zu blocks\n", counted.peak_bytes, counted.peak_blocks);
    sof_hashmap_free(&map);
    sof_mem_track_free(&counted);
    sof_mem_slab_free(&small);

## Building

    make             # builds the library, ./test, ./bench and the demo, demo/concord
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization
    ./bench          # compares the allocators with malloc()

To use the library in another project, copy _sof_mem.h_, _sof_mem_internal.h_ and the _.c_ files of the allocators it uses, with _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into the project and compile them with its own sources. To build its tests there too, copy all four allocators' _.c_ files, _test.c_, and _test.h_ and _test.mk_ from _../test/_, and compile _test.c_ with _-pthread_.

## Benchmark

    bench [-h] [-n count] [words]

Times three measures, each with five allocators through their `sof_alloc_t`: the standard functions (_malloc_), an arena (_arena_), a pool (_pool_), a slab allocator with its defaults (_slab_), and a tracker over the standard functions (_track_). The arena, pool and slab allocator are cleared at the end of each run, inside the timing, and kept from one run to the next, as an application reusing one would. Every timing is the best of **5** runs, in nanoseconds per item.

- _blocks_: _count_ blocks of random sizes from **8** to **128** bytes, allocated and touched, then freed one at a time. The pool's objects are **128** bytes.
- _sof_rbtree_: _count_ random 64-bit keys inserted into an `sof_rbtree_t`, which is then freed. The pool's objects are the size of an `sof_rbtree_node_t`.
- _sof_hashmap_: every word of _words_ inserted into a `sof_hashmap_t` with `sof_hashmap_string_keys`, which copies each word through the allocator, then freed. The pool's objects are **32** bytes, so longer words go to the standard functions, and the element arrays do for both the pool and the slab allocator.

Options:

- _-h_ prints the usage message and exits.
- _-n count_ sets the number of blocks and keys, from **1** to **100000000** (default **1000000**).
- _words_ is a file with a word on each line (default _../test-data/words.txt_).

For example, on an Apple M4 Max, macOS, Apple clang 21:

    $ ./bench
    ns per item, best of 5 runs
                  malloc     arena      pool      slab     track
    blocks         15.68      5.13      8.28      3.93     20.16
    sof_rbtree    272.47    254.58    252.67    255.62    342.20
    hashmap        60.73     58.01     53.19     54.55     67.61

and on an Intel i9-13900K, Arch Linux, gcc 16 and glibc:

    $ ./bench
    ns per item, best of 5 runs
                  malloc     arena      pool      slab     track
    blocks         15.05      8.19     13.83      6.14     24.93
    sof_rbtree    419.97    301.21    298.85    302.25    466.83
    hashmap        67.19     48.46     49.83     49.72     80.30

and on an AMD Ryzen 9 5950X, Arch Linux, gcc 16 and glibc:

    $ ./bench
    ns per item, best of 5 runs
                  malloc     arena      pool      slab     track
    blocks         16.20      6.44     13.54      9.58     30.34
    sof_rbtree    428.88    337.01    343.00    343.54    499.08
    hashmap        81.76     55.85     62.27     60.47     95.53

The slab allocator is the fastest at small blocks of mixed sizes on the M4 Max and the i9, faster even than the arena, since a block it frees is the next one handed out, so the memory is still in the cache; on the Ryzen the arena is faster. The pool does worse there than in real use, since it gives every block **128** bytes. What the others save depends on the `malloc()` they replace: macOS's is slow for small blocks, and glibc's per-thread cache is about as fast as a pool. In real structures allocation is a smaller part of the time. An sof_rbtree of a million random keys spends most of it on cache misses following the tree, and a hashmap of words on hashing and probing. With glibc, an arena, pool or slab allocator still saves a fifth to a third there, from blocks laid out next to each other and frees that cost nothing; with macOS's allocator, from a twentieth to an eighth. The tracker costs a header and a list update per block, which adds from about a tenth to two thirds to what its parent takes on these measures (nine tenths for the blocks on the Ryzen); that is what it costs to know what is live.

## Functions

### Arena

`sof_mem_error_t sof_mem_arena_init(sof_mem_arena_t *a, size_t block_size, const sof_alloc_t *alloc)`

Initialize the arena _a_ the caller provides. _block_size_ is the usable size of each standard block, rounded up to the strictest alignment, or **0** for **SOF_MEM_ARENA_BLOCK_SIZE** (**65536**); allocations larger than a quarter of it get blocks of their own. _alloc_ is the parent allocator the blocks come from; **NULL** means the standard functions. It is copied into the arena, so it can be on the stack. Nothing is allocated until the first `sof_mem_arena_alloc()`. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _a_ is **NULL**, _block_size_ is too large to round up and add a header to, or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the arena is left empty and not initialized.

`sof_mem_error_t sof_mem_arena_init_buffer(sof_mem_arena_t *a, void *buffer, size_t size)`

Initialize the arena _a_ to work in _buffer_, of _size_ bytes, which the caller provides. The arena's block header goes at the first aligned address in the buffer, and every allocation, whatever its size, comes from the rest; no parent is ever called, and allocations fail once the buffer is full. The buffer must stay valid, and not be used otherwise, until the arena is freed. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _a_ or _buffer_ is **NULL**, or the buffer can't hold the header and one aligned unit after it. On failure the arena is left empty and not initialized.

`void *sof_mem_arena_alloc(sof_mem_arena_t *a, size_t size)`

Allocate a block of _size_ bytes, aligned for any type and not zeroed. It comes from the current standard block, or the next one, which is allocated from the parent if the arena has none left, or, if _size_ is more than a quarter of `block_size`, from a block of its own; a fixed arena's all come from its buffer. Returns the block, or **NULL** if _size_ is **0**, the parent couldn't give a new block, a fixed arena's buffer is full, or _a_ is **NULL** or not initialized.

`void *sof_mem_arena_realloc(sof_mem_arena_t *a, void *p, size_t old_size, size_t new_size)`

Resize block _p_, allocated from _a_ with _old_size_ bytes, to _new_size_ bytes, keeping its contents up to the smaller size. The most recent allocation grows or shrinks in place while it fits in its block, and any other block shrinks in place, keeping its space. A large block that stays large is resized by the parent. Otherwise a new block is allocated, the contents copied and _p_ released, which is also how a block moves between standard and large when its size crosses a quarter of `block_size`. A **NULL** _p_ allocates a new block. Returns the resized block, or **NULL**, leaving _p_ as it was, if _new_size_ is **0**, the memory couldn't be allocated, or _a_ is **NULL** or not initialized.

`sof_mem_error_t sof_mem_arena_release(sof_mem_arena_t *a, void *p, size_t size)`

Give block _p_, allocated from _a_ with _size_ bytes, back to the arena. A large block goes back to the parent at once. The most recent allocation, unless a mark was made since, is taken back, so the next allocation reuses its space; this goes back one allocation only. Any other block is kept until the arena is cleared, reset past it or freed. _p_ must have come from this arena with this size and not been released since, as with `free()`; the arena can't check. A **NULL** _p_ does nothing. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _a_ is **NULL** or not initialized.

`sof_mem_arena_mark_t sof_mem_arena_mark(sof_mem_arena_t *a)`

Return a mark, by value, of the arena's present place, to pass to `sof_mem_arena_reset()`. Allocations made before it stay as they are, but can no longer grow in place or be taken back by `sof_mem_arena_release()`. Marks nest: a reset to an earlier mark goes past the later ones. Returns a zeroed mark if _a_ is **NULL** or not initialized.

`sof_mem_error_t sof_mem_arena_reset(sof_mem_arena_t *a, sof_mem_arena_mark_t mark)`

Go back to _mark_, taking back everything allocated since it was made: large blocks allocated since go back to the parent, and the standard blocks are kept and used again from the mark. A large block allocated before the mark and resized since is kept. Every pointer allocated since the mark, and every later mark, is invalid afterwards; the mark itself can be used again. _mark_ must be one of _a_'s, made since _a_ was last cleared, and not past a mark already reset to; the checks the arena can make cheaply are made. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if the mark can't be _a_'s, or _a_ is **NULL** or not initialized.

`sof_alloc_t sof_mem_arena_allocator(sof_mem_arena_t *a)`

Return a `sof_alloc_t`, by value, whose _malloc_ is `sof_mem_arena_alloc()`, _realloc_ `sof_mem_arena_realloc()` and _free_sized_ `sof_mem_arena_release()`, with _a_ as its _ctx_. It holds _a_'s address, so _a_ must not move while it is in use. Everything allocated through it is taken back by `sof_mem_arena_clear()` and `sof_mem_arena_free()`, and by `sof_mem_arena_reset()` to a mark made before it was allocated, so a library's structure must not be used after those; free the structure first, then clear the arena. If _a_ is **NULL** or not initialized, the `sof_alloc_t` is still valid, but every allocation through it fails.

`sof_mem_error_t sof_mem_arena_clear(sof_mem_arena_t *a)`

Take back every allocation. Large blocks go back to the parent; standard blocks are kept, and used again from the first. Every pointer the arena returned, and every mark, is invalid afterwards. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _a_ is **NULL** or not initialized.

`sof_mem_error_t sof_mem_arena_free(sof_mem_arena_t *a)`

Give every block back to the parent; a fixed arena's buffer belongs to the caller and isn't touched. The `sof_mem_arena_t` itself belongs to the caller and isn't freed; it is left empty. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _a_ is **NULL** or not initialized.

### Pool

`sof_mem_error_t sof_mem_pool_init(sof_mem_pool_t *pool, size_t object_size, size_t per_slab, const sof_alloc_t *alloc)`

Initialize the pool _pool_ the caller provides, for objects of _object_size_ bytes. Each takes `slot_size` bytes in a slab: _object_size_, at least the size of a pointer, rounded up to the strictest alignment. _per_slab_ is the number of objects in each slab, or **0** for as many as fit in **SOF_MEM_POOL_SLAB_SIZE** (**65536**) bytes, and at least one. _alloc_ is the parent allocator the slabs come from, or **NULL** for the standard functions; it is copied into the pool. Nothing is allocated until the first `sof_mem_pool_alloc()`. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _pool_ is **NULL**, _object_size_ is **0**, a slab would be too large to allocate, or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the pool is left empty and not initialized.

`void *sof_mem_pool_alloc(sof_mem_pool_t *pool)`

Allocate an object of `object_size` bytes, aligned for any type and not zeroed: the most recently released one if there is one, otherwise the next unused one in a slab, allocating a slab from the parent if the pool has none left. Returns the object, or **NULL** if the parent couldn't give a new slab, or _pool_ is **NULL** or not initialized.

`sof_mem_error_t sof_mem_pool_release(sof_mem_pool_t *pool, void *p)`

Give object _p_ back to the pool, onto the free list, so it is the next one `sof_mem_pool_alloc()` returns. _p_ must have come from this pool and not been released since; the pool can't check. A **NULL** _p_ does nothing. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _pool_ is **NULL** or not initialized.

`sof_alloc_t sof_mem_pool_allocator(sof_mem_pool_t *pool)`

Return a `sof_alloc_t`, by value, that allocates from _pool_, with _pool_ as its _ctx_. Its _malloc_ gives an object from the pool for any size up to `object_size` and passes larger sizes to the parent; its _free_sized_ tells the two apart by the size it is given; and its _realloc_ keeps an object that stays within `object_size`, resizes a parent's block that stays larger through the parent, and moves a block between them when its size crosses `object_size`. It holds _pool_'s address, so _pool_ must not move while it is in use. Blocks from the parent must be freed before `sof_mem_pool_free()`, which forgets the parent. If _pool_ is **NULL** or not initialized, the `sof_alloc_t` is still valid, but every allocation through it fails and every free does nothing.

`sof_mem_error_t sof_mem_pool_clear(sof_mem_pool_t *pool)`

Take back every object, keeping the slabs, which are used again from the first. Every object the pool returned is invalid afterwards; blocks its `sof_alloc_t` passed on to the parent aren't affected. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _pool_ is **NULL** or not initialized.

`sof_mem_error_t sof_mem_pool_free(sof_mem_pool_t *pool)`

Give every slab back to the parent. The `sof_mem_pool_t` itself belongs to the caller and isn't freed; it is left empty. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _pool_ is **NULL** or not initialized.

### Slab

`sof_mem_error_t sof_mem_slab_init(sof_mem_slab_t *slab, size_t max_size, size_t slab_size, const sof_alloc_t *alloc)`

Initialize the slab allocator _slab_ the caller provides. _max_size_ is the largest size served from the size classes, rounded up to a class, or **0** for **SOF_MEM_SLAB_MAX_SIZE** (**1024**); it can be at most **SOF_MEM_SLAB_LIMIT** (**8192**). _slab_size_ is the size of each class's slabs in bytes, or **0** for **SOF_MEM_POOL_SLAB_SIZE** (**65536**); each holds as many blocks of its class as fit, and at least one. _alloc_ is the parent allocator, or **NULL** for the standard functions; it is copied into the slab allocator and its pools. The array of pools, one per class, is allocated now; each pool allocates its first slab when its class is first used. Returns **SOF_MEM_OK**, **SOF_MEM_NO_MEM** if the array of pools couldn't be allocated, or **SOF_MEM_ERROR** if _slab_ is **NULL**, _max_size_ is larger than **SOF_MEM_SLAB_LIMIT**, a slab would be too large to allocate, or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the slab allocator is left empty and not initialized.

`void *sof_mem_slab_alloc(sof_mem_slab_t *slab, size_t size)`

Allocate a block of _size_ bytes, aligned for any type and not zeroed: from the pool of _size_'s class if _size_ is up to `max_size`, otherwise from the parent. Returns the block, or **NULL** if _size_ is **0**, the memory couldn't be allocated, or _slab_ is **NULL** or not initialized.

`void *sof_mem_slab_realloc(sof_mem_slab_t *slab, void *p, size_t old_size, size_t new_size)`

Resize block _p_, allocated from _slab_ with _old_size_ bytes, to _new_size_ bytes, keeping its contents up to the smaller size. A block that stays in its class keeps its place, and one that stays larger than `max_size` is resized by the parent. Otherwise a new block is allocated, the contents copied and _p_ released. A **NULL** _p_ allocates a new block. Returns the resized block, or **NULL**, leaving _p_ as it was, if _new_size_ is **0**, the memory couldn't be allocated, or _slab_ is **NULL** or not initialized.

`sof_mem_error_t sof_mem_slab_release(sof_mem_slab_t *slab, void *p, size_t size)`

Give block _p_, allocated from _slab_ with _size_ bytes, back: to its class's pool, where it is the next of that class handed out, or to the parent if _size_ is larger than `max_size`. _p_ must have come from this slab allocator with this size and not been released since; it can't check. A **NULL** _p_ does nothing. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _slab_ is **NULL** or not initialized.

`sof_alloc_t sof_mem_slab_allocator(sof_mem_slab_t *slab)`

Return a `sof_alloc_t`, by value, whose _malloc_ is `sof_mem_slab_alloc()`, _realloc_ `sof_mem_slab_realloc()` and _free_sized_ `sof_mem_slab_release()`, with _slab_ as its _ctx_. It holds _slab_'s address, so _slab_ must not move while it is in use. Blocks larger than `max_size` come from the parent and must be freed before `sof_mem_slab_free()`, which forgets the parent. If _slab_ is **NULL** or not initialized, the `sof_alloc_t` is still valid, but every allocation through it fails.

`sof_mem_error_t sof_mem_slab_clear(sof_mem_slab_t *slab)`

Take back every block in the size classes, keeping the slabs. Blocks larger than `max_size` belong to the parent and aren't affected. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _slab_ is **NULL** or not initialized.

`sof_mem_error_t sof_mem_slab_free(sof_mem_slab_t *slab)`

Give every slab, and the array of pools, back to the parent. The `sof_mem_slab_t` itself belongs to the caller and isn't freed; it is left empty. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _slab_ is **NULL** or not initialized.

### Track

`sof_mem_error_t sof_mem_track_init(sof_mem_track_t *t, sof_mem_track_error_func_t error_func, void *ctx, const sof_alloc_t *alloc)`

Initialize the tracker _t_ the caller provides. _error_func_ is told of each problem the tracker finds, with _ctx_; it can be **NULL**, and the problems are counted in `errors` either way. _alloc_ is the parent allocator every block comes from, or **NULL** for the standard functions; it is copied into the tracker. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _t_ is **NULL**, or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the tracker is left empty and not initialized.

`void *sof_mem_track_alloc(sof_mem_track_t *t, size_t size)`

Allocate a block of _size_ bytes from the parent, with a hidden header before it, aligned for any type and not zeroed, and count it. Returns the block, or **NULL** if _size_ is **0**, the parent couldn't allocate it (counted in `failures`), or _t_ is **NULL** or not initialized.

`void *sof_mem_track_realloc(sof_mem_track_t *t, void *p, size_t old_size, size_t new_size)`

Resize block _p_ through the parent to _new_size_ bytes, keeping its contents up to the smaller size; it may move. If _p_ isn't a live block of _t_, the problem is reported as **SOF_MEM_TRACK_UNKNOWN_BLOCK** and nothing is done. If _old_size_ isn't its size, the problem is reported as **SOF_MEM_TRACK_WRONG_SIZE** and the block is resized from its real size. A **NULL** _p_ allocates a new block. Returns the resized block, or **NULL**, leaving _p_ as it was, if _new_size_ is **0**, _p_ isn't a live block of _t_, the parent couldn't resize it (counted in `failures`), or _t_ is **NULL** or not initialized.

`sof_mem_error_t sof_mem_track_release(sof_mem_track_t *t, void *p, size_t size)`

Free block _p_, giving it back to the parent. If _size_ isn't its size, the problem is reported as **SOF_MEM_TRACK_WRONG_SIZE** and the block is freed with its real size. A **NULL** _p_ does nothing. Returns **SOF_MEM_OK**, **SOF_MEM_NO_MATCH** if _p_ isn't a live block of _t_ (reported as **SOF_MEM_TRACK_UNKNOWN_BLOCK**; nothing is freed), or **SOF_MEM_ERROR** if _t_ is **NULL** or not initialized.

`int sof_mem_track_traverse(const sof_mem_track_t *t, sof_mem_track_traverse_func_t cb, void *ctx)`

Visit every live block, newest first, calling _cb_ with the block, its size and _ctx_. A **NULL** _cb_ visits nothing. If the callback returns a non-zero value, the traversal stops and returns that value. Otherwise **0** is returned once every block has been visited, or **-1** if _t_ is **NULL** or not initialized. Called at the end of a program, it lists the leaks.

`sof_alloc_t sof_mem_track_allocator(sof_mem_track_t *t)`

Return a `sof_alloc_t`, by value, whose _malloc_ is `sof_mem_track_alloc()`, _realloc_ `sof_mem_track_realloc()` and _free_sized_ `sof_mem_track_release()`, with _t_ as its _ctx_. It holds _t_'s address, so _t_ must not move while it is in use. If _t_ is **NULL** or not initialized, the `sof_alloc_t` is still valid, but every allocation through it fails.

`sof_mem_error_t sof_mem_track_clear(sof_mem_track_t *t)`

Free every live block and set every count back to **0**, keeping _error_func_ and _ctx_. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _t_ is **NULL** or not initialized.

`sof_mem_error_t sof_mem_track_free(sof_mem_track_t *t)`

Free every live block and leave the tracker empty. Read the counts, or list the leaks with `sof_mem_track_traverse()`, before this. The `sof_mem_track_t` itself belongs to the caller and isn't freed. Returns **SOF_MEM_OK**, or **SOF_MEM_ERROR** if _t_ is **NULL** or not initialized.

Every function accepts a **NULL** allocator: those that allocate return **NULL**, the `_allocator()` functions return a `sof_alloc_t` that allocates nothing, `sof_mem_arena_mark()` returns a zeroed mark, `sof_mem_track_traverse()` returns **-1**, and the others return **SOF_MEM_ERROR**. An allocator that was freed, or whose `init` failed, is treated the same way until it is initialized again. None holds elements of the application's, so there is no delete callback: what is in the memory is the application's to deal with before it is cleared or freed.

## Types

    typedef enum sof_mem_error_t {
        SOF_MEM_OK = 0,
        SOF_MEM_NO_MEM,
        SOF_MEM_NO_MATCH,
        SOF_MEM_ERROR,
    } sof_mem_error_t;

The functions that allocate a block return **NULL** when they can't; **SOF_MEM_NO_MEM** is returned only by `sof_mem_slab_init()`, and **SOF_MEM_NO_MATCH** only by `sof_mem_track_release()`.

    typedef struct sof_mem_block_t {
        struct sof_mem_block_t *next;   // next block in the list
        struct sof_mem_block_t *prev;   // previous block, for an arena's large blocks only
        size_t size;                    // usable bytes after the header
        size_t serial;                  // order an arena's large blocks were allocated in
    } sof_mem_block_t;

The header of each block an arena or pool gets from its parent, and of a fixed arena's buffer. The application may follow the lists, but not change them.

    typedef struct sof_mem_arena_t {
        sof_mem_block_t *blocks;        // standard blocks, in the order they are used
        sof_mem_block_t *current;       // the block allocations come from, or NULL
        sof_mem_block_t *large;         // large blocks, newest first
        unsigned char *last;            // the most recent allocation in current, or NULL
        size_t offset;                  // bytes of current handed out
        size_t block_size;              // usable bytes of each standard block, or of the buffer
        size_t allocated;               // bytes held from the parent, headers included
        size_t serial;                  // serial of the next large block
        sof_alloc_t alloc;                   // copy of the parent allocator
        int fixed;                      // 1 for an arena in a buffer of the caller's
        int ready;                      // 1 once initialized, 0 when freed or init failed
    } sof_mem_arena_t;

    typedef struct sof_mem_arena_mark_t {
        sof_mem_block_t *block;         // the arena's current block
        size_t offset;                  // its offset in that block
        size_t serial;                  // the serial its next large block would get
    } sof_mem_arena_mark_t;

The application keeps a mark by value and must not change it.

    typedef struct sof_mem_pool_t {
        sof_mem_block_t *slabs;         // slabs, in the order they are used
        sof_mem_block_t *current;       // the slab new objects come from, or NULL
        void *free_list;                // released objects, each pointing to the next
        size_t used;                    // objects of current handed out
        size_t object_size;             // as given to sof_mem_pool_init()
        size_t slot_size;               // space each object takes in a slab
        size_t per_slab;                // objects in each slab
        size_t count;                   // objects handed out and not released
        size_t allocated;               // bytes of slabs held from the parent, headers included
        sof_alloc_t alloc;                   // copy of the parent allocator
        int ready;                      // 1 once initialized, 0 when freed or init failed
    } sof_mem_pool_t;

    typedef struct sof_mem_slab_t {
        sof_mem_pool_t *pools;          // a pool per size class, smallest first
        size_t classes;                 // number of size classes
        size_t max_size;                // the largest class's size
        sof_alloc_t alloc;                   // copy of the parent allocator
        int ready;                      // 1 once initialized, 0 when freed or init failed
    } sof_mem_slab_t;

    typedef struct sof_mem_track_t {
        struct sof_mem_track_header_t *live;   // live blocks, newest first (internal)
        size_t blocks;                  // live blocks
        size_t bytes;                   // bytes in them, as asked for
        size_t peak_bytes;              // the most bytes live at once
        size_t peak_blocks;             // the most blocks live at once
        size_t allocations;             // successful allocations
        size_t reallocations;           // successful reallocations
        size_t frees;                   // successful frees
        size_t failures;                // allocations and reallocations the parent refused
        size_t errors;                  // problems found
        sof_mem_track_error_func_t error_func;
        void *ctx;                      // passed to error_func
        sof_alloc_t alloc;                   // copy of the parent allocator
        int ready;                      // 1 once initialized, 0 when freed or init failed
    } sof_mem_track_t;

The fields of every allocator can be read but must not be changed by the application. An `allocated` count is what the allocator holds itself; blocks a pool or slab allocator passed on to the parent aren't counted. A slab allocator's totals are the sums of its pools' `count` and `allocated`.

    typedef enum sof_mem_track_problem_t {
        SOF_MEM_TRACK_WRONG_SIZE,       // freed or reallocated with the wrong size; the right one is used
        SOF_MEM_TRACK_UNKNOWN_BLOCK,    // not a live block of the tracker; nothing is done
    } sof_mem_track_problem_t;

    typedef void (*sof_mem_track_error_func_t)(const void *p, size_t size, sof_mem_track_problem_t problem, void *ctx)

Told of each problem a tracker finds: _p_ and _size_ as the application gave them, and what is wrong. It must not call into the tracker. _ctx_ is the context pointer given to `sof_mem_track_init()`.

    typedef int (*sof_mem_track_traverse_func_t)(void *p, size_t size, void *ctx)

Called for each live block visited by `sof_mem_track_traverse()`, with its size and the _ctx_ given to the traversal. It must not call into the tracker. Return **0** to continue, or a non-zero value to stop the traversal early; that value is returned by `sof_mem_track_traverse()`.

## Threading

The library does no locking of its own. It has no global state, so different allocators can be used on different threads freely; one per thread is the usual way to use them, and needs no lock at all.

An allocator shared between threads needs a lock supplied by the application around every call, including the calls a library makes through its `sof_alloc_t`. None of the functions that allocate or free only reads: even releasing a block changes the allocator. The only read-only function is `sof_mem_track_traverse()`, which takes a `const sof_mem_track_t *`, and reading the fields; `sof_mem_arena_mark()` writes, since it stops earlier blocks growing in place, and the `_allocator()` functions take a non-`const` pointer because the `sof_alloc_t` they return allocates through it. A structure using a shared allocator's `sof_alloc_t` needs the allocator's lock held for every call into it that may allocate or free, which is usually simplest done by giving the structure and its allocator the same lock.

Some things to watch for:

- **Stacked allocators share their parent's state.** A slab allocator over an arena calls into the arena; if either is shared, both need the same lock.
- **A structure's read-only functions stay read-only.** A _sof_hashmap_ or _sof_rbtree_ using one of these allocators allocates only when it is changed, so its readers don't touch the allocator, but `sof_rbtree_get_keys()` and `sof_rbtree_clear()`, which allocate, do.
- **Clearing invalidates everything.** No thread may use a block from an allocator across its `clear`, `reset` or `free`.
