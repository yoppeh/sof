# sof_set

A hash set, with union, intersection, difference and subset tests, in c.

The library has a `sof_set_t` type that describes a set of keys of any type. The application provides the `sof_set_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_set_init()`, which takes the same key callbacks as [sof_hashmap](../sof_hashmap/), saying how to hash, compare, copy and free the keys, and an optional allocator. Keys are added with `sof_set_add()`, looked up with `sof_set_find()` and removed with `sof_set_remove()`, and `sof_set_count()` says how many there are. `sof_set_union()`, `sof_set_intersection()`, `sof_set_difference()` and `sof_set_symmetric_difference()` change one set by another, in place, and `sof_set_is_subset()`, `sof_set_equal()` and `sof_set_is_disjoint()` compare two sets. `sof_set_traverse()` calls a callback for every key, and `sof_set_next()` loops over them without one. `sof_set_clear()` empties the set and `sof_set_free()` releases its memory once the application is finished with it.

A set is a `sof_hashmap_t` whose element data is unused, so adding, finding and removing a key take constant time on average. Each set operation takes time in proportion to one of its two sets: union and symmetric difference to the set being applied, intersection to the set being changed, and difference and the comparisons to the smaller of the two. A red-black tree ([sof_rbtree](../sof_rbtree/)) would give sorted order, but at a logarithmic cost for every lookup, and every set operation is made of lookups; the hash set is faster wherever order doesn't matter. Keys come out in no particular order. When both sets have the same _hash_ callback and _ctx_, the operations and comparisons look each key up by the hash the other set stored for it, with `sof_hashmap_find_hashed()` and `sof_hashmap_insert_hashed()`, rather than hashing it again, which makes them 1.5 to 2 times as fast for string keys; sets with different ones hash every key. The operations work in place, as `|=`, `&=`, `-=` and `^=` do; to make a new set from two others, take the union of an empty set with the first, then apply the second.

The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). The library's demo, the [deps](demo/) program in _demo/_, uses sets of targets, compared by pointer, to walk its dependency graph. All set declarations are in _sof_set.h_. The set is built on the [sof_hashmap](../sof_hashmap/) library in _../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), which hashes keys with the [sof_hash](../sof_hash/) library in _../sof_hash/_ (_sof_hash.c_ and _sof_hash.h_, which use [rapidhash](https://github.com/Nicoshev/rapidhash) in _rapidhash.h_, under its own MIT license), and memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond those, the library needs nothing but the c standard library.

    sof_set_t a, b;
    sof_set_init(&a, &sof_hashmap_string_keys, NULL, 0, NULL);   // string keys the set copies; no ctx, size hint
    sof_set_init(&b, &sof_hashmap_string_keys, NULL, 0, NULL);   // or allocator
    sof_set_add(&a, "red", NULL);                                // stores a copy of "red"
    sof_set_add(&a, "green", NULL);
    sof_set_add(&b, "green", NULL);
    sof_set_intersection(&a, &b);                                // a is now {"green"}
    if (sof_set_find(&a, "green") != NULL) {
        ...
    }
    sof_set_free(&a);                                            // frees the copies too
    sof_set_free(&b);

## Building

    make             # compiles the library and builds ./test and the demo, demo/deps
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

The tests check every operation and comparison against arrays on random sets, with good hashes and with colliding ones whose runs wrap around the end of the array, both between sets with the same _ctx_, which use the stored hashes, and with different ones, which hash again; they count the hash callback's calls to show the first never hashes. They also cover a set applied to itself, borrowed keys, **NULL** and freed sets, and the failure of each allocation in turn.

To use the library in another project, copy _sof_set.c_ and _sof_set.h_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../sof_hashmap/_, _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, and _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../sof_hash/_, into it and compile the _.c_ files with the project's own sources. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_ and those four _.c_ files with _-pthread_.

## Keys

A set's keys are handled by a `sof_hashmap_key_callbacks_t`, as described under Keys in [sof_hashmap](../sof_hashmap/): _hash_ and _equal_ say which keys are the same, and a _copy_ and _free_ pair makes the set own its keys, so `sof_hashmap_string_keys` gives a set of strings it copies. Without _copy_, the set stores the caller's pointers, which must stay valid while they are in the set. The _free_ callback is called for every key the set gives up, so it also serves as the set's delete callback; a set has no data of its own to free. Keys can't be **NULL**.

The functions that take two sets expect both to use the same kind of keys, with _hash_ and _equal_ callbacks that agree. When `sof_set_union()` or `sof_set_symmetric_difference()` adds keys from one set to another, it adds them as `sof_set_add()` does: copied, if the receiving set has a _copy_ callback, or else as the other set's own pointers, which then must outlive it. A set with a _free_ callback but no _copy_ must not take keys from another set, or both would free them.

## Functions

`sof_set_error_t sof_set_init(sof_set_t *set, const sof_hashmap_key_callbacks_t *keys, void *ctx, size_t expected_keys, const sof_alloc_t *alloc)`

Initialize the _set_ the caller provides, with the key callbacks _keys_ (copied into the set, so they can be on the stack; **NULL** tells keys apart by their addresses) and the _ctx_ passed to them. _expected_keys_ is how many keys to make room for before the set has to grow; **0** gives the smallest size. _alloc_ is the allocator for the set's memory, passed on to the _copy_ and _free_ key callbacks; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). Returns **SOF_SET_OK**, **SOF_SET_NO_MEM** if memory couldn't be allocated, or **SOF_SET_ERROR** if _set_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves the _set_ empty.

`void *sof_set_add(sof_set_t *set, const void *key, int *created)`

Add _key_ to the _set_ and return the key the set stores: its copy of _key_, or _key_ itself without a _copy_ callback. If the key is already there, nothing is added and the set's own key is returned, so the caller still owns _key_. If _created_ is not **NULL**, it is set to **1** if the key was added, or **0** otherwise. Returns **NULL** if memory couldn't be allocated or the _copy_ callback failed. The stored key doesn't move, so the pointer is good until the key is removed.

`void *sof_set_find(const sof_set_t *set, const void *key)`

Return the key the _set_ stores that equals _key_, or **NULL** if there isn't one.

`sof_set_error_t sof_set_remove(sof_set_t *set, const void *key)`

Remove _key_ from the _set_, giving the set's key to the _free_ callback, if there is one. _key_ may be the set's own key. Returns **SOF_SET_OK**, or **SOF_SET_NO_MATCH** if the _key_ isn't in the _set_.

`size_t sof_set_count(const sof_set_t *set)`

Return the number of keys in the _set_, or **0** if _set_ is **NULL**.

`sof_set_error_t sof_set_union(sof_set_t *dst, const sof_set_t *src)`

Add every key of _src_ to _dst_, leaving _src_ as it was. Takes time in proportion to _src_. Into an empty _dst_, it makes a copy of _src_. Returns **SOF_SET_OK**, or **SOF_SET_NO_MEM** if a key couldn't be added, leaving _dst_ with the keys added so far.

`sof_set_error_t sof_set_intersection(sof_set_t *dst, const sof_set_t *src)`

Remove from _dst_ every key that isn't in _src_. Takes time in proportion to _dst_. Returns **SOF_SET_OK**.

`sof_set_error_t sof_set_difference(sof_set_t *dst, const sof_set_t *src)`

Remove from _dst_ every key that is in _src_. Takes time in proportion to the smaller of the two: it looks up the keys of _src_ in _dst_, or those of _dst_ in _src_. Returns **SOF_SET_OK**.

`sof_set_error_t sof_set_symmetric_difference(sof_set_t *dst, const sof_set_t *src)`

Leave in _dst_ the keys that are in _dst_ or _src_ but not both: each key of _src_ is removed from _dst_ if _dst_ has it, and added otherwise. Takes time in proportion to _src_. Returns **SOF_SET_OK**, or **SOF_SET_NO_MEM** if a key couldn't be added, leaving _dst_ part way, with some of _src_'s keys added or removed.

The four set operations leave _src_ unchanged, remove keys from _dst_ through its _free_ callback, and return **SOF_SET_ERROR** if _dst_ or _src_ is **NULL**, or _dst_ was freed or never initialized. If _dst_ and _src_ are the same set, union and intersection leave it as it is, and difference and symmetric difference empty it.

`int sof_set_is_subset(const sof_set_t *a, const sof_set_t *b)`

Return **1** if every key of _a_ is in _b_ (as it is when _a_ is empty, or is _b_), or **0** if not. Takes time in proportion to _a_, and none if _a_ has more keys than _b_.

`int sof_set_equal(const sof_set_t *a, const sof_set_t *b)`

Return **1** if _a_ and _b_ hold the same keys, or **0** if not.

`int sof_set_is_disjoint(const sof_set_t *a, const sof_set_t *b)`

Return **1** if no key is in both _a_ and _b_, or **0** if one is. Takes time in proportion to the smaller set.

The three comparisons return **-1** if _a_ or _b_ is **NULL**.

`int sof_set_traverse(const sof_set_t *set, sof_set_traverse_func_t cb, void *ctx)`

Call _cb_ with each key and _ctx_, in no particular order. If _cb_ returns a non-zero value, the traversal stops and returns that value. Otherwise returns **0** once every key has been visited (also when _cb_ is **NULL**), or **-1** if _set_ is **NULL**.

`void *sof_set_next(const sof_set_t *set, size_t *position)`

Return the next key, in the same order as `sof_set_traverse()`, or **NULL** when there are no more. Set _position_ to 0 to start; each call updates it. Don't add or remove keys until the loop ends, or keys may be skipped or seen twice.

    size_t position = 0;
    void *key;
    while ((key = sof_set_next(&set, &position)) != NULL) {
        ...
    }

`sof_set_error_t sof_set_clear(sof_set_t *set)`

Remove every key, giving each to the _free_ callback, if there is one. The _set_ keeps its memory and can still be used. Returns **SOF_SET_OK**.

`sof_set_error_t sof_set_free(sof_set_t *set)`

Remove every key as `sof_set_clear()` does, then free the _set_'s memory. The `sof_set_t` itself belongs to the caller and isn't freed; it is left empty. Returns **SOF_SET_OK**.

Every function accepts a **NULL** _set_ or _key_: `sof_set_add()`, `sof_set_find()` and `sof_set_next()` return **NULL**, `sof_set_count()` returns **0**, `sof_set_traverse()` and the comparisons return **-1**, and the others return **SOF_SET_ERROR**. A set that was freed, or whose `sof_set_init()` failed, is empty and stays that way: lookups find nothing, adds return **NULL** and set operations into it return **SOF_SET_ERROR** until it is initialized again. As the _src_ of a set operation, or in a comparison, it is an empty set.

## Types

    typedef enum sof_set_error_t {
        SOF_SET_OK = 0,
        SOF_SET_NO_MEM,
        SOF_SET_NO_MATCH,
        SOF_SET_ERROR,
    } sof_set_error_t;

    typedef struct sof_set_t {
        sof_hashmap_t map;      // the keys; map.count is how many
    } sof_set_t;

The application can read `map`, but should not change it, or call `sof_hashmap` functions on it. The keys the set stores never move; the hashmap's elements do, but a set doesn't hand them out.

    typedef int (*sof_set_traverse_func_t)(void *key, void *ctx)

Called for each key visited by `sof_set_traverse()`, with the _ctx_ given to it. Return **0** to continue, or a non-zero value to stop the traversal; that value is returned by `sof_set_traverse()`. It must not add to or remove from the set.

## Threading

The library does no locking. `sof_set_count()`, `sof_set_find()`, `sof_set_next()`, `sof_set_traverse()`, `sof_set_is_subset()`, `sof_set_equal()` and `sof_set_is_disjoint()` only read their sets, and the set operations only read _src_ (as long as the _hash_ and _equal_ callbacks only read), so any number of threads can call them at once as long as nothing writes to those sets; every other function writes. A caller sharing sets between threads should guard them with a reader-writer lock, keeping in mind that:

- a set operation needs the write lock on _dst_ and the read lock on _src_; take the locks of two sets in a fixed order, to avoid deadlock;
- a key pointer from `sof_set_find()` or `sof_set_add()` is only safe to use while the lock is held, since another thread may remove the key;
- callbacks run while the caller holds its lock, so they must not take it again.

Separate sets need no lock at all, even with the same key callbacks, as long as their _ctx_ and allocator aren't shared, or are safe to share.
