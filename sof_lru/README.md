# sof_lru

A least-recently-used cache in c, with keys of any type.

The library has a `sof_lru_t` type that describes the cache as a whole. The application provides the `sof_lru_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_lru_init()`, which takes the cache's capacity, a set of key callbacks as [sof_hashmap](../sof_hashmap/) takes them, an optional delete function that is called with every entry the cache removes or evicts, and an optional allocator. `sof_lru_insert()` adds a key, or finds the one already there, and makes it the most recently used; inserting a new key into a full cache evicts the least recently used entry. `sof_lru_find()` finds a key and makes it the most recently used, and `sof_lru_peek()` finds it without changing the order. `sof_lru_remove()` and `sof_lru_remove_node()` remove an entry. `sof_lru_newest()`, `sof_lru_oldest()`, `sof_lru_next()` and `sof_lru_prev()` walk the entries in order of use, and `sof_lru_traverse()` calls a callback for each, newest first. `sof_lru_clear()` empties the cache and `sof_lru_free()` releases its memory once the application is finished with it.

Keys are found through a `sof_hashmap_t`, whose elements point to the cache's nodes, and the nodes are linked in a list from the most to the least recently used. `sof_lru_init()` allocates every node, and a map with room for one more key than the capacity, so inserting allocates nothing but key copies, and every operation but `sof_lru_clear()` and `sof_lru_free()` takes constant time on average. Each node keeps the hash its key has in the map, so evicting or removing an entry finds its map element by that hash and the key's own pointer, with `sof_hashmap_find_hashed()`, without hashing or comparing the key again; that makes an insert into a full cache about 15% faster. Nodes don't move, so a node pointer stays good until its own entry goes.

The library's demo is the [cachesim](demo/) program in _demo/_, which replays or generates a trace of keys through caches of one or more sizes and reports the hit rates. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All cache declarations are in _sof_lru.h_. Keys are kept in the [sof_hashmap](../sof_hashmap/) library in _../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), which hashes them with the [sof_hash](../sof_hash/) library in _../sof_hash/_ (_sof_hash.c_ and _sof_hash.h_, which use [rapidhash](https://github.com/Nicoshev/rapidhash) in _rapidhash.h_, under its own MIT license), and memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond those, the library needs nothing but the c standard library.

    sof_lru_t lru;
    sof_lru_init(&lru, &sof_hashmap_string_keys, drop, NULL, 1000, NULL);   // string keys, at most 1000;
                                                                            // drop() frees each entry's data
    sof_lru_node_t *n = sof_lru_find(&lru, path);                           // a hit makes path the newest
    if (n == NULL) {
        n = sof_lru_insert(&lru, path, NULL);                               // may evict the oldest
        n->data = load(path);
    }
    ...
    sof_lru_free(&lru);                                                     // drops every entry

## Building

    make             # compiles the library and builds ./test and the demo, demo/cachesim
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

The tests check every function against a list kept in order of use, on random operations with good hashes and with colliding ones, and count the _hash_ callback's calls to show that evicting and removing entries never hash a key again; they also cover **NULL** and freed caches, and the failure of each allocation in turn.

To use the library in another project, copy _sof_lru.c_ and _sof_lru.h_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../sof_hashmap/_, _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, and _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../sof_hash/_, into it. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_ and the four libraries' _.c_ files with _-pthread_.

## Keys

The cache keeps its keys in a `sof_hashmap_t`, and `sof_lru_init()` takes the same `sof_hashmap_key_callbacks_t` as `sof_hashmap_init()`, described under [Keys](../sof_hashmap/README.md#keys) in its README: _hash_, _equal_, _copy_ and _free_, each of which can be **NULL**. They get the _ctx_ given to `sof_lru_init()`, and _copy_ and _free_ get the cache's allocator. `sof_hashmap_string_keys` makes the cache copy and free NUL-terminated strings; **NULL** for the whole set gives a cache keyed by pointer identity. Keys can't be **NULL**.

A key's _free_ callback is called after the delete function, so the delete function still sees the key.

## Functions

`sof_lru_error_t sof_lru_init(sof_lru_t *lru, const sof_hashmap_key_callbacks_t *keys, sof_lru_node_delete_func_t del_func, void *ctx, size_t capacity, const sof_alloc_t *alloc)`

Initialize the _lru_ the caller provides, to hold at most _capacity_ entries, with the key callbacks _keys_ (copied, so they can be on the stack), the delete function _del_func_ (which can be **NULL**), and the _ctx_ passed to all of them. _alloc_ is the allocator for the cache's nodes and map, passed on to the _copy_ and _free_ key callbacks; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). It is copied into the cache, so it can be on the stack. Returns **SOF_LRU_OK**, **SOF_LRU_NO_MEM** if memory couldn't be allocated, or **SOF_LRU_ERROR** if _capacity_ is **0** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves the _lru_ empty.

`sof_lru_node_t *sof_lru_insert(sof_lru_t *lru, const void *key, int *created)`

Insert _key_ into the _lru_, make its entry the most recently used, and return it. If the _key_ is already present, no entry is created and the existing one is returned; the cache keeps that entry's key. Otherwise, if the cache is full, the least recently used entry is evicted, going to the delete function and its key to the _free_ callback, and the new entry, whose `data` is **NULL**, holds the copy of _key_ (or _key_ itself, without a _copy_ callback). If _created_ is not **NULL**, it is set to **1** if a new entry was created, or **0** otherwise. Returns **NULL** if the _copy_ callback failed, in which case nothing was evicted and the cache is as it was.

`sof_lru_node_t *sof_lru_find(sof_lru_t *lru, const void *key)`

Return the entry holding _key_, after making it the most recently used, or **NULL** if there isn't one. Moving the entry writes the cache, so _lru_ isn't `const`; `sof_lru_peek()` looks up a key in a `const` cache.

`sof_lru_node_t *sof_lru_peek(const sof_lru_t *lru, const void *key)`

Return the entry holding _key_, or **NULL** if there isn't one, without changing the order of the entries.

`sof_lru_error_t sof_lru_remove(sof_lru_t *lru, const void *key)`

Remove the entry holding _key_. The entry goes to the delete function, then its key to the _free_ callback, if there are any. _key_ may be the entry's own key. Returns **SOF_LRU_OK**, or **SOF_LRU_NO_MATCH** if the _key_ isn't in the _lru_.

`sof_lru_error_t sof_lru_remove_node(sof_lru_t *lru, sof_lru_node_t *node)`

Remove an entry the caller already has, from `sof_lru_peek()` or `sof_lru_oldest()` for example, without looking its key up again. Otherwise the same as `sof_lru_remove()`. Returns **SOF_LRU_OK**, or **SOF_LRU_ERROR** if _node_ isn't one of the _lru_'s entries.

`sof_lru_node_t *sof_lru_newest(const sof_lru_t *lru)`

Return the most recently used entry, or **NULL** if the cache is empty.

`sof_lru_node_t *sof_lru_oldest(const sof_lru_t *lru)`

Return the least recently used entry, the one the next insert of a new key into a full cache evicts, or **NULL** if the cache is empty.

`sof_lru_node_t *sof_lru_next(const sof_lru_t *lru, const sof_lru_node_t *node)`

Return the entry used next less recently than _node_, or **NULL** if _node_ is the oldest or is **NULL**. Finding, inserting or removing changes the order, so get the next entry before removing the current one.

    for (sof_lru_node_t *n = sof_lru_newest(&lru); n != NULL; n = sof_lru_next(&lru, n)) {
        ...
    }

`sof_lru_node_t *sof_lru_prev(const sof_lru_t *lru, const sof_lru_node_t *node)`

Return the entry used next more recently than _node_, or **NULL** if _node_ is the newest or is **NULL**. Starting from `sof_lru_oldest()`, it walks the entries oldest first.

`int sof_lru_traverse(const sof_lru_t *lru, sof_lru_traverse_func_t cb, void *ctx)`

Call _cb_ with each entry and _ctx_, from the most to the least recently used, without changing the order. If _cb_ returns a non-zero value, the traversal stops and returns that value. Otherwise returns **0** once every entry has been visited (or at once, if _cb_ is **NULL**), or **-1** if _lru_ is **NULL**.

`sof_lru_error_t sof_lru_clear(sof_lru_t *lru)`

Remove every entry, newest first, giving each to the delete function and its key to the _free_ callback, if there are any. The _lru_ keeps its memory and can still be used.

`sof_lru_error_t sof_lru_free(sof_lru_t *lru)`

Remove every entry as `sof_lru_clear()` does, then free the cache's memory. The `sof_lru_t` itself belongs to the caller and isn't freed; it is left empty.

Every function accepts a **NULL** _lru_ or _key_: `sof_lru_insert()`, `sof_lru_find()`, `sof_lru_peek()`, `sof_lru_newest()`, `sof_lru_oldest()`, `sof_lru_next()` and `sof_lru_prev()` return **NULL**, `sof_lru_traverse()` returns **-1**, and the others return **SOF_LRU_ERROR**. A cache that was freed, or whose `sof_lru_init()` failed, is empty and stays that way: lookups find nothing, `sof_lru_remove()` returns **SOF_LRU_NO_MATCH** and inserts return **NULL** until it is initialized again.

## Types

    typedef enum sof_lru_error_t {
        SOF_LRU_OK = 0,
        SOF_LRU_NO_MEM,
        SOF_LRU_NO_MATCH,
        SOF_LRU_ERROR,
    } sof_lru_error_t;

    typedef struct sof_lru_node_t {
        void *key;                      // the map's copy, or the caller's pointer
        void *data;                     // the application's; never touched by the cache
        struct sof_lru_node_t *newer;   // the entry used next more recently, or NULL
        struct sof_lru_node_t *older;   // the entry used next less recently, or NULL
        size_t hash;                    // the key's hash in the map
    } sof_lru_node_t;

The application may change a node's `data`, but not its other fields. Nodes live in an array allocated by `sof_lru_init()`, so a node pointer stays good until its entry is removed or evicted, or the cache is cleared or freed.

    typedef struct sof_lru_t {
        size_t capacity;                // the most entries the cache holds
        size_t count;                   // the entries it holds now
        sof_lru_node_t *nodes;          // capacity nodes
        sof_lru_node_t *newest;         // the most recently used entry
        sof_lru_node_t *oldest;         // the least recently used entry
        sof_lru_node_t *spare;          // unused nodes, linked through older
        sof_hashmap_t map;              // each element's data is its key's node
        sof_lru_node_delete_func_t del_func;
        void *ctx;                      // passed to del_func and the key callbacks
        sof_alloc_t alloc;              // copy of the allocator; zeroed for the standard functions
    } sof_lru_t;

The fields of `sof_lru_t` can be read but should not be changed by the application.

    typedef void (*sof_lru_node_delete_func_t)(sof_lru_node_t *node, void *ctx)

Receives each entry just before the cache removes it, in `sof_lru_insert()` when it evicts, `sof_lru_remove()`, `sof_lru_remove_node()`, `sof_lru_clear()` and `sof_lru_free()`, so the application can free the entry's `data`, and its key if the cache has no _free_ key callback. It must not call into the cache. _ctx_ is the context given to `sof_lru_init()`.

    typedef int (*sof_lru_traverse_func_t)(sof_lru_node_t *node, void *ctx)

Called for each entry visited by `sof_lru_traverse()`, with the _ctx_ given to it. Return **0** to continue, or a non-zero value to stop the traversal; that value is returned by `sof_lru_traverse()`. It must not insert into, remove from or find in the cache.

## Threading

The library does no locking. `sof_lru_peek()`, `sof_lru_newest()`, `sof_lru_oldest()`, `sof_lru_next()`, `sof_lru_prev()` and `sof_lru_traverse()` only read the cache (as long as the _hash_ and _equal_ callbacks only read), and take a `const sof_lru_t *`, though the entries they return aren't `const`, so any number of threads can call them at once as long as nothing writes to it. Every other function writes, `sof_lru_find()` included: a hit moves the entry to the front of the list. A cache shared between threads, where lookups are mostly `sof_lru_find()`, is best guarded by a plain mutex, since a reader-writer lock would need the write lock for nearly every call. With a reader-writer lock, keep in mind that:

- a node pointer is only safe to use while the lock is held, since another thread's insert may evict its entry and reuse the node for another key;
- writing a node's `data` is a write, even though no cache function is called;
- callbacks run while the caller holds its lock, so they must not take it again.

Separate caches need no lock at all, even with the same key callbacks, as long as their _ctx_ and allocator aren't shared, or are safe to share.
