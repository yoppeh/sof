# sof_rbtree

A red-black tree in c.

The library has an `sof_rbtree_t` type that describes the red-black tree as a whole. The application provides the `sof_rbtree_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_rbtree_init()`, which takes a key comparison function, an optional delete function that is called with every node the tree removes, a _ctx_ passed to both, and an optional allocator for the nodes. Nodes can be added with `sof_rbtree_insert()`, found with `sof_rbtree_find()` and removed with `sof_rbtree_remove()` or `sof_rbtree_remove_node()`. The lowest and highest keys can be found with `sof_rbtree_minimum()` and `sof_rbtree_maximum()`, and `sof_rbtree_next()` and `sof_rbtree_prev()` step from a node to the one with the next higher or lower key. `sof_rbtree_traverse_ascending()` and `sof_rbtree_traverse_descending()` call a callback for each node in ascending or descending key order. `sof_rbtree_get_keys()` returns all keys in ascending order. `sof_rbtree_clear()` removes a subtree, or every node in the tree, and `sof_rbtree_free()` releases the tree's memory once the application is finished with it.

Each node is allocated separately and never moves, so a node pointer stays good until that node is removed, whatever else is inserted or removed. Every node points to the `nil_node` inside the `sof_rbtree_t`, so the `sof_rbtree_t` itself must not be moved or copied while it holds nodes.

The library's demo is the [basic](demo/) program in _demo/_, a BASIC interpreter that keeps its program lines in a tree ordered by line number. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All sof_rbtree declarations are in _sof_rbtree.h_. Memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond that, the library needs nothing but the c standard library.

    sof_rbtree_t tree;
    sof_rbtree_init(&tree, compare, NULL, NULL, NULL);       // compare function; no delete function, ctx or allocator
    sof_rbtree_node_t *n = sof_rbtree_insert(&tree, key, NULL);   // or the node already holding key
    n->data = value;
    ...
    for (n = sof_rbtree_minimum(&tree, NULL); n != NULL; n = sof_rbtree_next(&tree, n)) {
        ...                                             // in key order
    }
    sof_rbtree_free(&tree);                                  // removes every node

## Building

    make             # builds sof_rbtree.o, ./test and the demo, demo/basic
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

Most of the tests use generated integer keys. One reads the 263,533 words in _../test-data/words.txt_ (from [Peter Norvig's n-gram data](https://norvig.com/ngrams/)), inserts them in file order and shuffled, and removes a random half, so the tests run only inside this repository.

Besides testing each function, the tests run trees on threads as the Threading section below describes: readers checking a shared tree under a readers-writer lock with every read-only function (finds, minimum and maximum, `sof_rbtree_next()` and `sof_rbtree_prev()` walks, both traversals and `sof_rbtree_get_keys()`) while a writer inserts, removes and sets each new node's data, and separate trees on separate threads with no lock. The tests are linked with _-pthread_; the library itself doesn't use threads.

To use the library in another project, copy _sof_rbtree.c_ and _sof_rbtree.h_, and _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into it and compile the _.c_ files with the project's own sources. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_, _sof_rbtree.c_ and _sof_alloc.c_ with _-pthread_; the word-list test needs _../test-data/words.txt_ as well.

## Functions

`sof_rbtree_error_t sof_rbtree_init(sof_rbtree_t *tree, sof_rbtree_key_compare_func_t cmp_func, sof_rbtree_node_delete_func_t del_func, void *ctx, const sof_alloc_t *alloc)`

Initialize the _tree_ the caller provides. The _cmp_func_ is a required function that compares two key values and returns a value less than **0** if _a_ is less than _b_, **0** if the two compared keys are equal, and a value greater than **0** if _a_ is greater than _b_. The _del_func_ is an optional function that is called with a node just before it is removed. This gives the caller an opportunity to free memory allocated for the key and/or data belonging to the node. _ctx_ is passed to every call of _cmp_func_ and _del_func_, so they can use application state without globals; it can be **NULL**. _alloc_ is the allocator for nodes and the arrays `sof_rbtree_clear()` and `sof_rbtree_get_keys()` allocate; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). It is copied into the tree, so it can be on the stack. Nodes point to the _tree_'s `nil_node`, so the `sof_rbtree_t` must not be moved or copied while it holds nodes. Returns **SOF_RBTREE_OK**, or **SOF_RBTREE_ERROR** if _tree_ or _cmp_func_ is **NULL**, or _alloc_ has a _malloc_ but no _free_ or _free_sized_.

`sof_rbtree_node_t *sof_rbtree_insert(sof_rbtree_t *tree, void *key, int *created)`

Insert a new node with the given _key_ into the _tree_. The _cmp_func_ will be called to compare the given _key_ with the keys of other nodes in order to determine where the node should be inserted. If a node with this _key_ is already present in the _tree_, no new node is created and the pointer to **that** node is returned; the _tree_ keeps that node's original key, so the caller still owns the _key_ it passed (and should free it if necessary). If _created_ is not **NULL**, it is set to **1** if a new node was created, or **0** otherwise. A new node's `data` is set to **NULL**. Returns **NULL** if _tree_ is **NULL** or memory for the node couldn't be allocated.

`sof_rbtree_node_t *sof_rbtree_find(const sof_rbtree_t *tree, const void *key)`

Find the node with the given _key_. If the node with a matching _key_ is found, a pointer to it is returned to the caller. The _cmp_func_ will be used to find the _key_ in the _tree_. Returns **NULL** if the _key_ is not in the _tree_.

`sof_rbtree_error_t sof_rbtree_remove(sof_rbtree_t *tree, const void *key)`

Remove the node with the given _key_. The node goes to the _del_func_, if there is one, before it is removed. Returns **SOF_RBTREE_OK**, or **SOF_RBTREE_NO_MATCH** if the _key_ isn't in the _tree_.

`sof_rbtree_error_t sof_rbtree_remove_node(sof_rbtree_t *tree, sof_rbtree_node_t *node)`

Remove a _node_ the caller already has, from `sof_rbtree_find()` or `sof_rbtree_next()` for example, without looking its key up again. Otherwise the same as `sof_rbtree_remove()`. Other nodes don't move, so pointers to them stay good. Returns **SOF_RBTREE_OK**, or **SOF_RBTREE_ERROR** if _node_ is **NULL**, the _tree_'s `nil_node`, or the _tree_ is empty.

`sof_rbtree_node_t *sof_rbtree_maximum(const sof_rbtree_t *tree, const sof_rbtree_node_t *subtree)`

Return the node with the highest key in the _subtree_, or in the whole _tree_ if _subtree_ is **NULL**. Returns **NULL** if the _tree_ or _subtree_ is empty.

`sof_rbtree_node_t *sof_rbtree_minimum(const sof_rbtree_t *tree, const sof_rbtree_node_t *subtree)`

Return the node with the lowest key in the _subtree_, or in the whole _tree_ if _subtree_ is **NULL**. Returns **NULL** if the _tree_ or _subtree_ is empty.

`sof_rbtree_node_t *sof_rbtree_next(const sof_rbtree_t *tree, const sof_rbtree_node_t *node)`

Return the node with the next higher key after _node_, or **NULL** if _node_ has the highest key (or is **NULL**). Together with `sof_rbtree_minimum()` this walks the _tree_ in ascending order without a callback:

    for (n = sof_rbtree_minimum(tree, NULL); n != NULL; n = sof_rbtree_next(tree, n)) { ... }

Removing the current node while walking invalidates it, so get the next node first.

`sof_rbtree_node_t *sof_rbtree_prev(const sof_rbtree_t *tree, const sof_rbtree_node_t *node)`

Return the node with the next lower key before _node_, or **NULL** if _node_ has the lowest key (or is **NULL**). Together with `sof_rbtree_maximum()` this walks the _tree_ in descending order.

`int sof_rbtree_traverse_ascending(const sof_rbtree_t *tree, const sof_rbtree_node_t *subtree, sof_rbtree_traverse_func_t cb, void *ctx)`

Visit the nodes of the _subtree_, or of the whole _tree_ if _subtree_ is **NULL**, from lowest key to highest, calling _cb_ with each node and _ctx_. A **NULL** _cb_ visits nothing. If the callback returns a non-zero value, the traversal stops and returns that value. Otherwise **0** is returned once every node has been visited, or **-1** if _tree_ is **NULL**. An empty _tree_ or _subtree_ (a node's missing child) visits nothing and returns **0**.

`int sof_rbtree_traverse_descending(const sof_rbtree_t *tree, const sof_rbtree_node_t *subtree, sof_rbtree_traverse_func_t cb, void *ctx)`

Visit the nodes of the _subtree_, or of the whole _tree_ if _subtree_ is **NULL**, from highest key to lowest, calling _cb_ with each node and _ctx_. Returns the same values as `sof_rbtree_traverse_ascending()`.

`void **sof_rbtree_get_keys(const sof_rbtree_t *tree, size_t *count)`

Returns an array of the keys in the _tree_, in ascending order. The keys themselves are not copied. The array comes from the _tree_'s allocator, and the caller frees it with `sof_alloc_free(&tree->alloc, keys, (count + 1) * sizeof(void *))`. If _count_ is not **NULL**, it is set to the number of keys. The array has a **NULL** after the last key, but since a key can itself be **NULL** (or **0** stored as a pointer), use _count_ to find its end. An empty _tree_ gives an array holding only the terminating **NULL**. The array is the only allocation: the in-order walk keeps its path on the c stack, in room for 128 nodes on a 64-bit system, since a red-black tree of _n_ nodes is at most 2·log2(_n_ + 1) high. Returns **NULL** if the array couldn't be allocated.

`sof_rbtree_error_t sof_rbtree_clear(sof_rbtree_t *tree, sof_rbtree_node_t *subtree)`

Remove every node in the subtree rooted at _subtree_. If _subtree_ is **NULL** (or the root), every node in the _tree_ is removed, leaving an empty tree that can still be used. An empty _subtree_ (a node's missing child, which points to the tree's `nil_node`) removes nothing. Otherwise the subtree's nodes are removed one at a time so the rest of the tree stays balanced, which needs a temporary array of node pointers. The _del_func_, if not **NULL**, is called for each node before it is removed. Returns **SOF_RBTREE_OK**, or **SOF_RBTREE_NO_MEM** if that array couldn't be allocated, in which case the _tree_ is unchanged.

`sof_rbtree_error_t sof_rbtree_free(sof_rbtree_t *tree)`

Remove every node, as `sof_rbtree_clear()` does for the whole tree, and release the tree's memory. The `sof_rbtree_t` itself belongs to the caller and isn't freed; it is left empty.

Every function accepts a **NULL** _tree_: the functions returning a node or keys return **NULL**, the traversals return **-1**, and the others return **SOF_RBTREE_ERROR**. A tree that was freed, or whose `sof_rbtree_init()` failed, is empty and stays that way: lookups find nothing and inserts return **NULL** until it is initialized again. A key can be **NULL** (or **0** stored as a pointer), as long as _cmp_func_ handles it.

## Types

    typedef enum sof_rbtree_error_t {
        SOF_RBTREE_OK = 0,
        SOF_RBTREE_NO_MEM,
        SOF_RBTREE_NO_MATCH,
        SOF_RBTREE_ERROR,
    } sof_rbtree_error_t;

    typedef struct sof_rbtree_node_t {
        struct sof_rbtree_node_t *parent;
        struct sof_rbtree_node_t *left;
        struct sof_rbtree_node_t *right;
        uint32_t flags;
        void *key;
        void *data;
    } sof_rbtree_node_t;

A node of the tree. The application may read and write `data`, which the library never touches, and read the other fields, but must not change `parent`, `left`, `right` or `key`. The tree keeps the `key` given to `sof_rbtree_insert()` but doesn't copy or free it; if it needs freeing, do that in the _del_func_. The top bit of `flags` is the node's color. The other 31 bits are zeroed when the node is inserted and never touched again by the library, so the application can use them, through these macros:

    SOF_RBTREE_GET_USER(n)           // the node's 31 user bits
    SOF_RBTREE_SET_USER(n, d)        // sets them to d, keeping the color
    SOF_RBTREE_COLOR_IS_BLACK(n)     // the node's color, for checking the tree
    SOF_RBTREE_COLOR_IS_RED(n)

A node pointer is good until that node is removed.

    typedef struct sof_rbtree_t {
        sof_rbtree_node_t *root;
        sof_rbtree_key_compare_func_t cmp_func;
        sof_rbtree_node_delete_func_t del_func;
        void *ctx;              // passed to cmp_func and del_func
        sof_rbtree_node_t nil_node;  // used internally; nodes point to it
        size_t count;           // number of nodes
        sof_alloc_t alloc;           // copy of the allocator; zeroed for the standard functions
    } sof_rbtree_t;

The fields of `sof_rbtree_t` can be read but should not be changed by the application.

    typedef int (*sof_rbtree_key_compare_func_t)(const void *a, const void *b, void *ctx)

Compares two keys, and orders the tree. It should return a value less than **0** if _a_ is less than _b_, **0** if _a_ == _b_ or greater than **0** if _a_ > _b_. _ctx_ is the context pointer given to `sof_rbtree_init()`.

    typedef void (*sof_rbtree_node_delete_func_t)(sof_rbtree_node_t *node, void *ctx)

Receives each node just before it is removed, by `sof_rbtree_remove()`, `sof_rbtree_remove_node()`, `sof_rbtree_clear()` and `sof_rbtree_free()`, so the application can free the node's `key` and `data`. It is optional: pass **NULL** to `sof_rbtree_init()` if there is nothing to free. It must not call into the tree. _ctx_ is the context pointer given to `sof_rbtree_init()`.

    typedef int (*sof_rbtree_traverse_func_t)(sof_rbtree_node_t *node, void *ctx)

Called for each _node_ visited during a traversal, with the _ctx_ given to the traversal function. Return **0** to continue, or a non-zero value to stop the traversal early; that value is returned by the traversal function. It must not insert into or remove from the tree.

## Threading

The library does no locking of its own. It has no global state, so different trees can be used on different threads freely, but a tree shared between threads needs a lock supplied by the application. A readers-writer lock (such as `pthread_rwlock_t`) fits well, because the read-only functions can safely run at the same time as each other.

**Read-only** (any number of threads at once, as long as no thread is writing): `sof_rbtree_find()`, `sof_rbtree_minimum()`, `sof_rbtree_maximum()`, `sof_rbtree_next()`, `sof_rbtree_prev()`, `sof_rbtree_traverse_ascending()`, `sof_rbtree_traverse_descending()`, `sof_rbtree_get_keys()`, reading `count`, and the `SOF_RBTREE_COLOR*` and `SOF_RBTREE_GET_USER` macros. These functions take a `const sof_rbtree_t *` (and `const` nodes), but the nodes they return, or pass to the callback, aren't `const`, since the application may change a node's `data` and user flags.

**Writes** (one thread, with no readers): `sof_rbtree_init()`, `sof_rbtree_insert()`, `sof_rbtree_remove()`, `sof_rbtree_remove_node()`, `sof_rbtree_clear()`, `sof_rbtree_free()` and `SOF_RBTREE_SET_USER`. `sof_rbtree_insert()` counts as a write even when the key is already in the tree.

Some things to watch for:

- **Node pointers are only valid while the lock is held.** Once it is released, another thread can remove the node. Find a node and use it under the same lock; don't keep the pointer for later.
- **`SOF_RBTREE_SET_USER` is a write.** The user bits share the `flags` word with the node's color, which inserts and removes change while rebalancing. Setting the user bits rewrites the whole word, so doing it under a read lock can lose a color change and corrupt the tree.
- **`data` belongs to the application.** The library never reads or writes `node->data`, so threads holding a read lock can read it, but if they also change it they need their own synchronization. Set `data` on a newly inserted node before releasing the write lock, or readers may see it as **NULL**.
- **Callbacks run inside the call that invokes them, while the caller holds the lock.** They must not take the lock again. A traversal callback can make read-only calls on the same tree, but must not insert or remove. A `del_func` must not call into the tree at all, since the tree is in the middle of a remove.
- **The compare function can run on several threads at once** (one per concurrent reader), so it must be safe to call concurrently, and so must anything it does with the tree's `ctx`.
- **`sof_rbtree_get_keys()` allocates, though it only reads the tree.** Readers calling it at once allocate and free through the tree's allocator at the same time, so the allocator must be safe to call from several threads. The standard functions are; an allocator of the application's own may need a lock of its own.
- **Walking with `sof_rbtree_next()` or `sof_rbtree_prev()` needs the lock for the whole walk**, not just for each step.

For example, with a `pthread_rwlock_t lock` kept alongside the tree:

    // Reader: find a node and use it under one read lock.
    pthread_rwlock_rdlock(&lock);
    sof_rbtree_node_t *n = sof_rbtree_find(&tree, key);
    if (n != NULL) {
        use(n->data);
    }
    pthread_rwlock_unlock(&lock);

    // Writer: insert a node and set its data under one write lock.
    int created;
    pthread_rwlock_wrlock(&lock);
    sof_rbtree_node_t *n = sof_rbtree_insert(&tree, key, &created);
    if (n != NULL && created) {
        n->data = value;
    }
    pthread_rwlock_unlock(&lock);
