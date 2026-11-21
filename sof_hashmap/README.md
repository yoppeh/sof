# sof_hashmap

A hashmap implementation in c, with keys of any type.

The library has a `sof_hashmap_t` type that describes the hashmap as a whole. The application provides the `sof_hashmap_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_hashmap_init()`, which takes a set of key callbacks saying how to hash, compare, copy and free the keys, in the manner of Core Foundation's `CFDictionary`, an optional delete function that is called with every element the map removes, and an optional allocator for the map's memory. `sof_hashmap_string_keys` is a ready-made set for NUL-terminated strings that the map copies and frees itself. Keys can be added with `sof_hashmap_insert()`, found with `sof_hashmap_find()`, both of which have a form that takes a hash the caller already has, from `sof_hashmap_hash()` or another element, `sof_hashmap_insert_hashed()` and `sof_hashmap_find_hashed()`, and removed with `sof_hashmap_remove()` or `sof_hashmap_remove_element()`, and `sof_hashmap_reserve()` makes room for more keys in advance. `sof_hashmap_traverse()` calls a callback for every element, and `sof_hashmap_next()` loops over them without one; `sof_hashmap_next_hashed()` loops over those with a given hash, for a caller that compares keys itself. `sof_hashmap_clear()` empties the map and `sof_hashmap_free()` releases its memory once the application is finished with it.

The map uses open addressing with linear probing. It starts with enough slots for the number of keys given to `sof_hashmap_init()` (at least 16) and doubles before more than three quarters of them are in use: an insert of a new key grows the map, moving every element, only when `count` is already `capacity / 4 * 3`. A remove never shrinks it, so an insert that follows a remove never grows it, and after `sof_hashmap_reserve(map, n)` inserting until `count` reaches _n_ allocates nothing but key copies and moves nothing. An allocator that counts or limits memory can rely on that.

The library's demo is the [ngram](demo/) program in _demo/_, which counts the n-grams of characters or words in a text in a hashmap and prints the most frequent. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/), including threaded ones that check what the Threading section below says. The tests use POSIX threads (`-pthread`); the library itself doesn't. All hashmap declarations are in _sof_hashmap.h_. Keys are hashed by the [sof_hash](../sof_hash/) library in _../sof_hash/_ (_sof_hash.c_ and _sof_hash.h_, which use [rapidhash](https://github.com/Nicoshev/rapidhash) in _rapidhash.h_, under its own MIT license), and memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond those, the library needs nothing but the c standard library.

    sof_hashmap_t map;
    sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, NULL);   // string keys; no delete function, ctx,
                                                                             // size hint or allocator
    sof_hashmap_element_t *e = sof_hashmap_insert(&map, word, NULL);         // stores a copy of word
    e->data = (void *)((uintptr_t)e->data + 1);                              // count it
    ...
    sof_hashmap_free(&map);                                                  // frees the copies too

## Building

    make             # compiles the library and builds ./test and the demo, demo/ngram
    make check       # builds and runs the tests, some of them on several threads
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

The tests cover every function, with **NULL** arguments and freed maps, and the failure of each allocation in turn, through ordinary inserts and inserts with a stored hash. They move keys between maps by their stored hashes and count the _hash_ callback's calls to show those never hash, check that a key's own pointer is found without calling _equal_, and loop over the elements of a hash that every key shares. The threaded tests share one map between a writer and several readers under a `pthread_rwlock_t`, with the writer inserting at one end and removing at the other so the map grows and elements move, and check that the readers' `sof_hashmap_find()`, `sof_hashmap_traverse()` and `sof_hashmap_next()` always see exactly the keys the writer has left in it. They also run separate maps on separate threads with no lock, each with its own allocator and callback _ctx_, and check each map's contents and that nothing leaked.

To use the library in another project, copy _sof_hashmap.c_ and _sof_hashmap.h_, _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, and _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../sof_hash/_, into it. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_, _sof_hashmap.c_, _sof_alloc.c_ and _sof_hash.c_ with _-pthread_.

## Keys

    typedef struct sof_hashmap_key_callbacks_t {
        size_t (*hash)(const void *key, void *ctx);
        int (*equal)(const void *a, const void *b, void *ctx);
        void *(*copy)(const void *key, const sof_alloc_t *alloc, void *ctx);
        void (*free)(void *key, const sof_alloc_t *alloc, void *ctx);
    } sof_hashmap_key_callbacks_t;

Each callback gets the _ctx_ given to `sof_hashmap_init()`, and _copy_ and _free_ also get the map's allocator, so they can allocate and free keys with `sof_alloc_malloc()` and `sof_alloc_free()` from _sof_alloc.h_. Any of them can be **NULL**:

- _hash_ must give equal keys equal hashes, and the same key the same hash whenever it is called with the same _ctx_, in any map: the map keeps each key's hash instead of calling _hash_ again, and `sof_hashmap_hash()` and the functions that take its result rely on it. **NULL** hashes the key's address. The map mixes the bits of whatever it returns, so a simple hash is fine.
- _equal_ returns non-zero if _a_ and _b_ are the same key. **NULL** compares the pointers. A key is always equal to itself: the map doesn't call _equal_ when the key it is given is the very pointer it stores, as when a key is looked up by an element's own `key`.
- _copy_ is called when a new key is inserted, and the map stores what it returns (**NULL** means it failed, and the insert returns **NULL**). With **NULL**, the map stores the caller's pointer, which must stay valid while it is in the map.
- _free_ is called for every key the map gives up, after the delete function. With **NULL**, the map never frees keys.

A _copy_ and _free_ pair makes the map own its keys, so callers can look up and insert keys held in buffers they reuse. Without them the caller owns the keys and frees them, for example in the delete function. Passing **NULL** for the whole set gives a map keyed by pointer identity. Keys can't be **NULL**.

`sof_hashmap_string_keys` copies and frees NUL-terminated strings, with the map's allocator. Its `sof_hashmap_string_hash()` and `sof_hashmap_string_equal()` can be used on their own for a map of strings it doesn't copy.

## Functions

`sof_hashmap_error_t sof_hashmap_init(sof_hashmap_t *map, const sof_hashmap_key_callbacks_t *keys, sof_hashmap_element_delete_func_t del_func, void *ctx, size_t expected_keys, const sof_alloc_t *alloc)`

Initialize the _map_ the caller provides, with the key callbacks _keys_ (copied into the map, so they can be on the stack), the delete function _del_func_ (which can be **NULL**), and the _ctx_ passed to all of them. The map starts with the smallest power-of-two number of slots, at least 16, that holds _expected_keys_ keys without growing; it still grows if more are inserted, and 0 gives 16 slots. _alloc_ is the allocator for the map's array, passed on to the _copy_ and _free_ key callbacks; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). It is copied into the map, so it can be on the stack. Returns **SOF_HASHMAP_OK**, **SOF_HASHMAP_NO_MEM** if memory couldn't be allocated, or **SOF_HASHMAP_ERROR** if _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves the _map_ empty.

`sof_hashmap_element_t *sof_hashmap_insert(sof_hashmap_t *map, const void *key, int *created)`

Insert _key_ into the _map_ and return its element. If the _key_ is already present, no element is created and the existing one is returned; the _map_ keeps that element's key. Otherwise the new element, whose `data` is **NULL**, holds the copy of _key_ (or _key_ itself, without a _copy_ callback). The map grows first, moving every element, if `count` is `capacity / 4 * 3`, and only then. If _created_ is not **NULL**, it is set to **1** if a new element was created, or **0** otherwise. Returns **NULL** if memory couldn't be allocated or the _copy_ callback failed.

`sof_hashmap_element_t *sof_hashmap_insert_hashed(sof_hashmap_t *map, const void *key, size_t hash, int *created)`

The same as `sof_hashmap_insert()`, but instead of calling the _hash_ callback it uses _hash_, which must be _key_'s full hash: what `sof_hashmap_hash()` returns for it, or the `hash` of an element whose key is equal to _key_, from this _map_ or another with the same _hash_ callback and _ctx_, as when the keys of one map are inserted into another. The _hash_ isn't checked: any other value stores the key where `sof_hashmap_find()` may not find it. Returns the new or existing element, or **NULL** as `sof_hashmap_insert()` does.

`sof_hashmap_element_t *sof_hashmap_find(const sof_hashmap_t *map, const void *key)`

Return the element holding _key_, or **NULL** if there isn't one. The _map_ is only read, but the element returned isn't `const`, so the caller can change its `data`.

`size_t sof_hashmap_hash(const sof_hashmap_t *map, const void *key)`

Return the full hash the _map_ gives _key_, as an element's `hash` holds it: the _hash_ callback's result, or _key_'s address without one, with its bits mixed. It is for the functions below that take a hash, so a key can be hashed once for several calls. Returns **0** if _map_ or _key_ is **NULL** or the _map_ isn't initialized.

`sof_hashmap_element_t *sof_hashmap_find_hashed(const sof_hashmap_t *map, const void *key, size_t hash)`

The same as `sof_hashmap_find()`, but instead of calling the _hash_ callback it uses _hash_, which must be _key_'s full hash: what `sof_hashmap_hash()` returns for it, or the `hash` of an element whose key is equal to _key_, from this _map_ or another with the same _hash_ callback and _ctx_. It saves hashing each key again when the keys of one map are looked up in another, as [sof_set](../sof_set/) does. The _hash_ isn't checked: any other value may miss a key that is there. The _equal_ callback is called as usual. Returns the element, or **NULL**.

`sof_hashmap_error_t sof_hashmap_remove(sof_hashmap_t *map, const void *key)`

Remove the element holding _key_. The element goes to the delete function, then its key to the _free_ callback, if there are any. Returns **SOF_HASHMAP_OK**, or **SOF_HASHMAP_NO_MATCH** if the _key_ isn't in the _map_.

`sof_hashmap_error_t sof_hashmap_remove_element(sof_hashmap_t *map, sof_hashmap_element_t *element)`

Remove an _element_ the caller already has, from `sof_hashmap_find()` or `sof_hashmap_next()` for example, without looking its key up again. Otherwise the same as `sof_hashmap_remove()`. Returns **SOF_HASHMAP_OK**, or **SOF_HASHMAP_ERROR** if _element_ isn't one of the _map_'s elements.

`sof_hashmap_error_t sof_hashmap_reserve(sof_hashmap_t *map, size_t keys)`

Make room for _keys_ keys in all, so that inserting until `count` reaches _keys_ doesn't grow the map. If its capacity is too small, the elements move to an array of the smallest power-of-two capacity that holds _keys_ without growing, as `sof_hashmap_init()` sizes it, so element pointers aren't good afterwards; otherwise nothing changes, and the map never shrinks. Returns **SOF_HASHMAP_OK**, **SOF_HASHMAP_NO_MEM** if the larger array couldn't be allocated, which leaves the map as it was, or **SOF_HASHMAP_ERROR** if _map_ is **NULL** or not initialized.

`int sof_hashmap_traverse(const sof_hashmap_t *map, sof_hashmap_traverse_func_t cb, void *ctx)`

Call _cb_ with each element and _ctx_, in no particular order. If _cb_ returns a non-zero value, the traversal stops and returns that value. Otherwise returns **0** once every element has been visited, or **-1** if _map_ is **NULL**.

`sof_hashmap_element_t *sof_hashmap_next(const sof_hashmap_t *map, size_t *position)`

Return the next element, in the same order as `sof_hashmap_traverse()`, or **NULL** when there are no more. Set _position_ to 0 to start; each call updates it. Don't insert or remove until the loop ends, or elements may be skipped or seen twice; changing their `data` is fine.

    size_t position = 0;
    sof_hashmap_element_t *e;
    while ((e = sof_hashmap_next(&map, &position)) != NULL) {
        ...
    }

`sof_hashmap_element_t *sof_hashmap_next_hashed(const sof_hashmap_t *map, size_t hash, size_t *position)`

Return the next element whose `hash` is _hash_, a full hash from `sof_hashmap_hash()` or an element, or **NULL** when there are no more. Set _position_ to 0 to start; each call updates it. The elements come in the order a lookup tries them, and include every element whose key is equal to a key with that hash, but may include others with the same hash, so the caller compares each key with its own. It lets a caller whose keys are its own kind of structure compare them directly instead of through the _equal_ callback, as [sof_intern](../sof_intern/) does, which can be faster. Don't insert or remove until the loop ends. Returns **NULL** if _map_ or _position_ is **NULL** or the _map_ isn't initialized.

    size_t position = 0;
    sof_hashmap_element_t *e;
    while ((e = sof_hashmap_next_hashed(&map, hash, &position)) != NULL) {
        if (... e->key is the key ...) {
            ...
        }
    }

`sof_hashmap_error_t sof_hashmap_clear(sof_hashmap_t *map)`

Remove every element, giving each to the delete function and its key to the _free_ callback, if there are any. The _map_ keeps its array at its current size and can still be used.

`sof_hashmap_error_t sof_hashmap_free(sof_hashmap_t *map)`

Remove every element as `sof_hashmap_clear()` does, then free the _map_'s array. The `sof_hashmap_t` itself belongs to the caller and isn't freed; it is left empty.

Every function accepts a **NULL** _map_ or _key_: `sof_hashmap_find()`, `sof_hashmap_find_hashed()`, `sof_hashmap_insert()`, `sof_hashmap_insert_hashed()`, `sof_hashmap_next()` and `sof_hashmap_next_hashed()` return **NULL**, `sof_hashmap_hash()` returns **0**, `sof_hashmap_traverse()` returns **-1**, and the others return **SOF_HASHMAP_ERROR**. A map that was freed, or whose `sof_hashmap_init()` failed, is empty and stays that way: lookups find nothing and inserts return **NULL** until it is initialized again.

## Types

    typedef enum sof_hashmap_error_t {
        SOF_HASHMAP_OK = 0,
        SOF_HASHMAP_NO_MEM,
        SOF_HASHMAP_NO_MATCH,
        SOF_HASHMAP_ERROR,
    } sof_hashmap_error_t;

    typedef struct sof_hashmap_element_t {
        size_t hash;        // full hash of the key
        void *key;          // the map's copy, or the caller's pointer
        void *data;         // the application's; never touched by the map
    } sof_hashmap_element_t;

    typedef struct sof_hashmap_t {
        size_t capacity;    // number of slots, a power of two
        size_t mask;        // capacity - 1
        size_t count;       // number of elements
        sof_hashmap_element_t *elements;
        sof_hashmap_key_callbacks_t keys;
        sof_hashmap_element_delete_func_t del_func;
        void *ctx;          // passed to del_func and the key callbacks
        sof_alloc_t alloc;       // copy of the allocator; zeroed for the standard functions
    } sof_hashmap_t;

The fields of `sof_hashmap_t` can be read but should not be changed by the application.

Elements live in the map's own array, so an element pointer is only good until the next `sof_hashmap_insert()` of a new key, which may move every element to a larger array, or the next remove, which may move other elements into the freed slot. Look the key up again rather than keeping the pointer.

    typedef void (*sof_hashmap_element_delete_func_t)(sof_hashmap_element_t *element, void *ctx)

Receives each element just before the map removes it, in `sof_hashmap_remove()`, `sof_hashmap_remove_element()`, `sof_hashmap_clear()` and `sof_hashmap_free()`, so the application can free the element's `data`, and its key if the map has no _free_ key callback. It must not call into the map. _ctx_ is the context given to `sof_hashmap_init()`.

    typedef int (*sof_hashmap_traverse_func_t)(sof_hashmap_element_t *element, void *ctx)

Called for each element visited by `sof_hashmap_traverse()`, with the _ctx_ given to it. Return **0** to continue, or a non-zero value to stop the traversal; that value is returned by `sof_hashmap_traverse()`. It must not insert into or remove from the map.

## Threading

The library does no locking. `sof_hashmap_find()`, `sof_hashmap_find_hashed()`, `sof_hashmap_hash()`, `sof_hashmap_traverse()`, `sof_hashmap_next()` and `sof_hashmap_next_hashed()` only read the map (as long as the _hash_ and _equal_ callbacks only read), and take a `const sof_hashmap_t *`, so any number of threads can call them at once as long as nothing writes to it; every other function writes. A caller sharing a map between threads should guard it with a reader-writer lock, keeping in mind that:

- an element pointer from `sof_hashmap_find()` or `sof_hashmap_insert()` is only safe to use while the lock is held, since another thread's insert or remove may move the element;
- writing an element's `data` is a write, even though no hashmap function is called;
- callbacks run while the caller holds its lock, so they must not take it again.

Separate maps need no lock at all, even with the same key callbacks, as long as their _ctx_ and allocator aren't shared, or are safe to share: a program can count on separate maps this way, one per thread, and merge them once the threads are joined.
