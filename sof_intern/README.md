# sof_intern

A string pool that keeps one copy of each distinct string, so strings can be compared by pointer, in c.

The library has a `sof_intern_t` type, the pool. The application provides the `sof_intern_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_intern_init()`, which takes a hint of how many strings to expect and an optional allocator. `sof_intern_string()` interns a string given as bytes and a length, and `sof_intern_cstr()` one given as a NUL-terminated c string: each returns the pool's copy, making one the first time, so two strings with the same bytes always give the same pointer. `sof_intern_find()` looks a string up without interning it. `sof_intern_length()` and `sof_intern_hash()` give an interned string's length and hash, which are kept with it. `sof_intern_count()` gives the number of strings, `sof_intern_traverse()` calls a callback for each one, and `sof_intern_next()` loops over them without one. `sof_intern_clear()` forgets every string and `sof_intern_free()` releases the pool's memory once the application is finished with it.

Strings can hold any bytes, NULs included, and each copy is followed by a NUL, so it can be passed as a c string as well. A `sof_string_t` from [sof_string](../sof_string/) is interned as `sof_intern_string(&pool, s.data, s.length, NULL)`; the pool doesn't use sof_string, which would bring in PCRE2. The copies are kept in blocks of 64 KiB, with a block of its own for any string too long for one, each copy just after a small header holding its length and hash. A hashmap finds them by their bytes: a string is hashed once, and the pool walks the map's elements with that hash, with `sof_hashmap_next_hashed()`, comparing lengths and bytes itself rather than through the map's _equal_ callback, which on an Apple M4 makes finding a string already in the pool about 40% faster (and changes little on x86-64). Copies never move, so a pointer to one is good until the pool is cleared or freed; there is no way to remove a single string.

The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). The library's demo, the [index](demo/) program in _demo/_, interns every word it indexes, so each is stored once. All declarations are in _sof_intern.h_. The pool's index is a hashmap from the [sof_hashmap](../sof_hashmap/) library in _../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), strings are hashed by the [sof_hash](../sof_hash/) library in _../sof_hash/_ (_sof_hash.c_ and _sof_hash.h_, which use [rapidhash](https://github.com/Nicoshev/rapidhash) in _rapidhash.h_, under its own MIT license), and memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond those, the library needs nothing but the c standard library.

    sof_intern_t pool;
    sof_intern_init(&pool, 0, NULL);                         // no size hint or allocator
    const char *a = sof_intern_cstr(&pool, "the", NULL);     // copies "the"
    const char *b = sof_intern_string(&pool, buf, 3, NULL);  // buf holds "the..."
    if (a == b) { ... }                                      // same bytes, same pointer
    size_t n = sof_intern_length(a);                         // 3, without strlen()
    sof_intern_free(&pool);                                  // a and b are gone

## Building

    make             # compiles the library and builds ./test and the demo, demo/index
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

To use the library in another project, copy _sof_intern.c_ and _sof_intern.h_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../sof_hash/_, and _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into it. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_ and the four libraries' _.c_ files with _-pthread_.

## Functions

`sof_intern_error_t sof_intern_init(sof_intern_t *pool, size_t expected_strings, const sof_alloc_t *alloc)`

Initialize the _pool_ the caller provides. The pool's hashmap starts with room for _expected_strings_ strings and grows as needed, so **0** is fine. _alloc_ is the allocator for all of the pool's memory, **NULL** meaning the standard functions (see [sof_alloc](../sof_alloc/)); it is copied into the pool, so it can be on the stack. The pool holds no pointers into itself, so it can be moved once initialized. Returns **SOF_INTERN_OK**, **SOF_INTERN_NO_MEM** if memory couldn't be allocated, or **SOF_INTERN_ERROR** if _pool_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves the _pool_ empty.

`const char *sof_intern_string(sof_intern_t *pool, const void *data, size_t length, int *created)`

Intern the _length_ bytes at _data_, which need not be NUL-terminated and can hold NULs, and return the pool's copy. If the pool already has a string with these bytes, that one is returned; otherwise a copy is made, followed by a NUL. The pool doesn't keep _data_. A _length_ of **0** interns the empty string. If _created_ is not **NULL**, it is set to **1** if the string was new to the pool, or **0** otherwise. Returns **NULL** if memory couldn't be allocated, which leaves the pool as it was.

`const char *sof_intern_cstr(sof_intern_t *pool, const char *s, int *created)`

The same as `sof_intern_string(pool, s, strlen(s), created)`.

`const char *sof_intern_find(const sof_intern_t *pool, const void *data, size_t length)`

Return the interned string with the _length_ bytes at _data_, or **NULL** if the pool doesn't have it. Nothing is interned.

`size_t sof_intern_length(const char *s)`

Return the length of the interned string _s_, in bytes and not counting the NUL after it, which is kept with the string, so this takes constant time and counts any NULs in it. _s_ must have come from this library, from a pool that hasn't since been cleared or freed. Returns **0** for **NULL**.

`uint64_t sof_intern_hash(const char *s)`

Return the hash of the interned string _s_, which is kept with it: `sof_hash_bytes(s, length, 0)` from [sof_hash](../sof_hash/). It depends only on the bytes, so it is the same in every pool and every run, which the string's address isn't. Use it to hash something made of interned strings, such as a sequence of words, so that a hashmap of them comes out in the same order every time. _s_ must have come from this library, as for `sof_intern_length()`. Returns the hash of the empty string for **NULL**.

`size_t sof_intern_count(const sof_intern_t *pool)`

Return the number of distinct strings in the _pool_.

`int sof_intern_traverse(const sof_intern_t *pool, sof_intern_traverse_func_t cb, void *ctx)`

Call _cb_ with each string, its length and _ctx_, in no particular order. If _cb_ returns a non-zero value, the traversal stops and returns that value. Otherwise returns **0** once every string has been visited (at once, if _cb_ is **NULL**), or **-1** if _pool_ is **NULL**.

`const char *sof_intern_next(const sof_intern_t *pool, size_t *position)`

Return the next string, in the same order as `sof_intern_traverse()`, or **NULL** when there are no more. Set _*position_ to **0** to start; each call updates it. Don't intern new strings until the loop ends.

    size_t position = 0;
    const char *s;
    while ((s = sof_intern_next(&pool, &position)) != NULL) {
        ...
    }

`sof_intern_error_t sof_intern_clear(sof_intern_t *pool)`

Forget every string and free the blocks they are kept in, so pointers to them are no longer good. The pool's hashmap keeps its size, and the pool can still be used. Returns **SOF_INTERN_OK**.

`sof_intern_error_t sof_intern_free(sof_intern_t *pool)`

Free everything the pool allocated, so pointers to its strings are no longer good. The `sof_intern_t` itself belongs to the caller and isn't freed; it is left empty. Returns **SOF_INTERN_OK**.

Every function accepts a **NULL** _pool_: `sof_intern_string()`, `sof_intern_cstr()`, `sof_intern_find()` and `sof_intern_next()` return **NULL**, `sof_intern_count()` returns **0**, `sof_intern_traverse()` returns **-1**, and the others return **SOF_INTERN_ERROR**. **NULL** _data_ or _s_ makes `sof_intern_string()`, `sof_intern_cstr()` and `sof_intern_find()` return **NULL**, whatever the _length_, and a **NULL** _position_ makes `sof_intern_next()` return **NULL**. A pool that was freed, or whose `sof_intern_init()` failed, is empty and stays that way: lookups find nothing and interning returns **NULL** until it is initialized again; `sof_intern_clear()` and `sof_intern_free()` return **SOF_INTERN_OK** for it.

## Types

    typedef enum sof_intern_error_t {
        SOF_INTERN_OK = 0,
        SOF_INTERN_NO_MEM,
        SOF_INTERN_NO_MATCH,
        SOF_INTERN_ERROR,
    } sof_intern_error_t;

    typedef struct sof_intern_t {
        sof_hashmap_t map;           // keys are the entries of the strings
        sof_intern_block_t *blocks;  // the blocks the strings are kept in, newest first
        size_t used;                 // bytes taken in the first block
        sof_alloc_t alloc;           // copy of the allocator; zeroed for the standard functions
    } sof_intern_t;

The fields can be read but should not be changed by the application. `map.elements` is **NULL** for a pool that was freed or whose `sof_intern_init()` failed.

    typedef struct sof_intern_entry_t {
        const char *data;   // the string, just after this header
        size_t length;      // its length, not counting the NUL
        uint64_t hash;      // sof_hash_bytes(data, length, 0)
    } sof_intern_entry_t;

    typedef struct sof_intern_block_t {
        struct sof_intern_block_t *next;
        size_t size;        // the size it was allocated with
    } sof_intern_block_t;

The header kept before each string, and the header of each block. They are public because the pool holds them, but the application needs neither: `sof_intern_length()` and `sof_intern_hash()` read the entry.

    typedef int (*sof_intern_traverse_func_t)(const char *s, size_t length, void *ctx)

Called for each string visited by `sof_intern_traverse()`, with its length and the _ctx_ given to it. Return **0** to continue, or a non-zero value to stop the traversal; that value is returned by `sof_intern_traverse()`. It must not intern into, clear or free the pool.

## Threading

The library has no globals and does no locking, so separate pools can be used on separate threads freely. `sof_intern_count()`, `sof_intern_find()`, `sof_intern_hash()`, `sof_intern_length()`, `sof_intern_next()` and `sof_intern_traverse()` only read, so any number of threads can call them at once as long as nothing writes to the pool; `sof_intern_string()`, `sof_intern_cstr()`, `sof_intern_clear()` and `sof_intern_free()` write. With a reader-writer lock, take the write lock for those four and the read lock for the rest. An interned string itself never changes or moves, so once a thread has its pointer it can read the string, its length and its hash without the lock, until the pool is cleared or freed.
