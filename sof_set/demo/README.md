# deps

A dependency sorter in c: an order to build targets in, from a file of what each depends on, or the cycle that stops there being one.

`deps` reads _.ini_-style files in which each assignment, such as `app = libnet libui`, names a target and the targets it depends on, and prints every target, one to a line, after everything it depends on. If some targets depend on each other, so there is no such order, it prints one cycle among them instead. The order is the same every run: targets ready together keep the order they first appeared in (see Order below). With `-t`, it orders only the targets named and what they need.

_main.c_ is the program. The graph is kept by _deps.c_, declared in _deps.h_, in a `deps_t` that the program provides and sets up with `deps_init()`. `deps_add_list()` adds a target and a list of names it depends on, as an assignment gives them, and `deps_depend()` and `deps_add()` add one dependency or one target. `deps_find()` finds a target by name. `deps_sort()` puts the targets in order, and if there is a cycle, `deps_cycle()` finds one. `deps_clear()` removes every target, and `deps_free()` releases the graph's memory.

Each target is a `deps_node_t`, found by name in a [sof_hashmap](../../sof_hashmap/), which holds its name. Each node keeps the targets it depends on, in the order they were given, and the targets that depend on it, in the order they first appeared. `deps_sort()` is Kahn's algorithm: it counts, for each target, the dependencies not yet in the order, and keeps the targets whose count is **0** in a first-in, first-out [sof_queue](../../sof_queue/), taking each in turn, putting it in the order and counting it off its dependents. Targets left with a count above **0** are in a cycle or depend on one; `deps_cycle()` follows such targets from one to a dependency, keeping those it has passed in a [sof_set](../../sof_set/), until one comes round again. With `-t`, the program finds the targets needed by a walk of what they depend on, keeping those it has reached in a sof_set too.

_example.ini_ is an example input: the libraries this program is built from. Targets are found through the [sof_hashmap](../../sof_hashmap/) library in _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), whose keys are hashed by the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_, _sof_hash.h_ and _rapidhash.h_, which is [rapidhash](https://github.com/Nicoshev/rapidhash), under its own MIT license), ordered through the [sof_queue](../../sof_queue/) library in _../../sof_queue/_ (_sof_queue.c_ and _sof_queue.h_), and kept track of on walks through the graph with the [sof_set](../../sof_set/) library in _../../sof_set/_ (_sof_set.c_ and _sof_set.h_). Memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program parses its input with the [sof_config](../../sof_config/) library in _../../sof_config/_ (_sof_config.c_ and _sof_config.h_, which also use _sof_utf.h_ from [sof_utf](../../sof_utf/) in _../../sof_utf/_), reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_), and logs with the [sof_log](../../sof_log/) library in _../../sof_log/_ (_sof_log.c_ and _sof_log.h_). Beyond those, it needs nothing but the c standard library.

    deps_t d;
    deps_init(&d, NULL);                           // no allocator: the standard functions
    deps_add_list(&d, "app", "libnet libui");      // app depends on libnet and libui
    deps_depend(&d, "libnet", "libcore");          // libnet depends on libcore
    deps_node_t *order[4];                         // room for d.count nodes
    size_t count;
    if (deps_sort(&d, order, &count) == DEPS_OK) { // libui libcore libnet app
        ...
    }
    deps_free(&d);

## Building

    make            # builds ./deps
    make debug=1    # builds with debug info and without optimization
    ./deps example.ini                  # the order to build this program's libraries in
    ./deps -t sof_config example.ini    # just sof_config and what it needs

`make` in the library's directory, _../_, builds it with the library, as do `make sof_set` and `make` from the root of the project.

To build it in another project, copy _main.c_, _deps.c_ and _deps.h_, with _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, _sof_queue.c_ and _sof_queue.h_ from _../../sof_queue/_, _sof_set.c_ and _sof_set.h_ from _../../sof_set/_, _sof_config.c_ and _sof_config.h_ from _../../sof_config/_, _sof_utf.h_ from _../../sof_utf/_, _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, and _sof_log.c_ and _sof_log.h_ from _../../sof_log/_, into it. To use only the graph, _deps.c_ and _deps.h_ need just the _sof_hashmap_, _sof_hash_, _sof_queue_, _sof_set_ and _sof_alloc_ files.

## Usage

    deps [-h] [-v] [-r] [-t target]... [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-v`: log, through sof_log to standard error, how many targets and dependencies each file added, how many there are in all, how many `-t` chose, and how many couldn't be ordered.
- `-r`: print the order reversed, each target before everything it depends on, such as for taking things down in the order they can go.
- `-t` _target_: order only _target_ and everything it depends on, directly or through others. Can be given more than once. The targets chosen are ordered as they would be among all the others, and a cycle is reported only if they are in one or depend on one. A _target_ that isn't in the input is an error.
- _file_: the input. With no _file_, or `-`, it reads standard input. With several, they are read in turn, as if they were one.

It prints the targets in order on standard output, one to a line, and exits with **0**. If they can't be ordered, it prints a cycle on standard error, as `cycle: a -> b -> c -> a`, each target depending on the next, prints nothing on standard output, and exits with **1**. It exits with **2** if the options are wrong, a file can't be read, its syntax is wrong (with the file, line and column), a target given with `-t` isn't in the input, or memory runs out.

For example, with _example.ini_:

    $ ./deps example.ini
    sof_copt
    sof_alloc
    sof_hash
    sof_utf
    sof_log
    sof_queue
    sof_hashmap
    sof_config
    sof_set
    deps

    $ ./deps -r -t sof_config example.ini
    sof_config
    sof_hashmap
    sof_utf
    sof_hash
    sof_alloc

and with a cycle:

    $ printf 'a = b\nb = c\nc = a d\nx = a\n' | ./deps
    cycle: a -> b -> c -> a

## Input

The input is parsed by [sof_config](../../sof_config/), so its syntax is that library's (see its README): comments start with `;` or `#`, `[name]` starts a section, and each assignment is a key, `=` and a value.

    ; what each target needs
    app = libnet libui      ; a comment can follow
    [libraries]
    libnet = libcore
    libui = libcore libgfx
    libcore =               ; depends on nothing

- Each assignment's key is a target, and its value a list of the targets it depends on, separated by whitespace. A value that is empty, or only whitespace, adds the target alone.
- A target named in a list is added if it hasn't been, so targets that depend on nothing needn't have a line of their own. Keys are ASCII letters, digits, `_`, `-` and `.`; names in a list can hold any character but whitespace, though a name that couldn't be a key can only be depended on.
- Every assignment adds to what its target depends on, so a target can be given on several lines, in any section, or in several files. A dependency given twice counts once. A target that depends on itself is a cycle.
- Sections are only for grouping, and don't change what is read.
- A quoted value is split like any other, after sof_config has replaced its escapes, so `"a\tb"` is two names.

## Order

The order is that of Kahn's algorithm with a first-in, first-out queue, and depends only on the order targets first appeared in, as key or as a name in a list. First the targets that depend on nothing go in the queue, in the order they first appeared. Then, until the queue is empty, the target at its head is taken and printed, and the targets that were waiting only on it go in at the back of the queue, in the order they first appeared. So each target is printed as soon as it can be, round by round, and targets that don't depend on each other keep the order of the input. In the example above, _deps_ appeared first but comes last, and _sof_copt_ comes before _sof_alloc_ since _deps_ named it first.

Of the targets in cycles, the one reported is found by starting from the first target, in the order they appeared, that couldn't be ordered, and following its first dependency, in the order they were given, that couldn't be ordered either, until a target comes round again; the cycle starts there.

## Functions

`deps_error_t deps_init(deps_t *d, const sof_alloc_t *alloc)`

Initialize the graph _d_ the caller provides, empty. _alloc_ is the allocator for all of the graph's memory, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into the graph, so it can be on the stack. Returns **DEPS_OK**, **DEPS_NO_MEM** if memory couldn't be allocated, or **DEPS_ERROR** if _d_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves _d_ empty.

`deps_error_t deps_add_list(deps_t *d, const char *target, const char *list)`

Add the target named _target_, if it isn't there, and each name in _list_ as something it depends on, in order, as `deps_depend()` does. Names in _list_ are separated by spaces, tabs, carriage returns, newlines, form feeds and vertical tabs, and can hold any other byte; a **NULL** or empty _list_ adds just the target. Returns **DEPS_OK**, **DEPS_NO_MEM** if memory ran out, when the target and the names before the one that failed are added, or **DEPS_ERROR** if _target_ is **NULL** or empty.

`deps_error_t deps_depend(deps_t *d, const char *target, const char *dependency)`

Make the target named _target_ depend on the one named _dependency_, adding either if it isn't there. A dependency already there isn't added again, and a target can depend on itself, which is a cycle. Returns **DEPS_OK**, **DEPS_NO_MEM** if memory ran out, which leaves the graph as it was but for the targets added, or **DEPS_ERROR** if either name is **NULL** or empty.

`deps_node_t *deps_add(deps_t *d, const char *name, int *created)`

Find the target named _name_, adding it, with nothing to depend on, if it isn't there. The graph keeps a copy of _name_. If _created_ isn't **NULL**, it is set to **1** if the target was added, or **0** if it was there or the call failed. Returns the node, or **NULL** if memory ran out or _name_ is **NULL** or empty.

`deps_node_t *deps_find(const deps_t *d, const char *name)`

Return the node of the target named _name_, or **NULL** if there is none or _name_ is **NULL**.

`deps_error_t deps_sort(const deps_t *d, deps_node_t **order, size_t *count)`

Put the targets in _order_, which must have room for _d_->count nodes, each after everything it depends on, as Order above describes, and set _*count_ to how many were put there. Returns **DEPS_OK** if every target was ordered; **DEPS_CYCLE** if some couldn't be, since they are in a cycle or depend on one, when _order_ holds the _*count_ targets that could; **DEPS_NO_MEM** if memory ran out, with _*count_ **0**; or **DEPS_ERROR**, with _*count_ **0**, if _order_ or _count_ is **NULL**. An empty graph gives **DEPS_OK** and a _*count_ of **0**.

`deps_error_t deps_cycle(const deps_t *d, deps_node_t **cycle, size_t *length)`

Find a cycle, as Order above describes, and put it in _cycle_, which must have room for _d_->count nodes: each node depends on the next, and the last on the first. _*length_ is set to the number of nodes in it, **1** for a target that depends on itself, or **0** if there is none or on an error. Returns **DEPS_OK**, **DEPS_NO_MATCH** if there is no cycle, **DEPS_NO_MEM** if memory ran out, or **DEPS_ERROR** if _cycle_ or _length_ is **NULL**.

`deps_error_t deps_clear(deps_t *d)`

Remove every target, leaving an empty graph that can still be used. Nodes from it are no longer good. Returns **DEPS_OK**.

`deps_error_t deps_free(deps_t *d)`

Free everything the graph allocated. The `deps_t` itself belongs to the caller and isn't freed; it is left empty. Returns **DEPS_OK**.

Every function accepts a **NULL** _d_: `deps_add()` and `deps_find()` return **NULL**, and the others return **DEPS_ERROR**. A graph that was freed, or whose `deps_init()` failed, is empty and stays that way: `deps_find()` finds nothing, `deps_add()` returns **NULL**, `deps_free()` returns **DEPS_OK**, and the others return **DEPS_ERROR**, until it is initialized again.

## Types

    typedef enum deps_error_t {
        DEPS_OK = 0,
        DEPS_NO_MEM,
        DEPS_NO_MATCH,
        DEPS_ERROR,
        DEPS_CYCLE,     // deps_sort(): some targets can't be ordered
    } deps_error_t;

    typedef struct deps_node_t {
        const char *key;                 // the target's name, which the graph owns
        void *data;                      // the application's; NULL when the node is made
        size_t index;                    // its place in the order targets first appeared in
        struct deps_node_t **deps;       // what it depends on, in the order given
        size_t dep_count;
        size_t dep_capacity;
        struct deps_node_t **dependents; // what depends on it, in order of index
        size_t dependent_count;
        size_t dependent_capacity;
    } deps_node_t;

A target. The graph never reads or writes _data_ after making the node, so the application can use it. The other fields can be read but should not be changed. A node never moves, so a pointer to it is good until `deps_clear()` or `deps_free()`.

    typedef struct deps_t {
        sof_hashmap_t map;      // name to node: each element's data is the node
        deps_node_t **nodes;    // the nodes, in the order they first appeared
        size_t count;           // the number of nodes
        size_t capacity;        // the size of nodes
        size_t edges;           // the number of dependencies
        sof_alloc_t alloc;      // copy of the allocator; zeroed for the standard functions
    } deps_t;

The fields can be read but should not be changed by the application. _nodes_[_i_]->index is _i_.

## Threading

_deps.c_ has no globals and does no locking, so separate graphs can be used on separate threads freely. `deps_find()`, `deps_sort()` and `deps_cycle()` only read the graph, so any number of threads can call them at once as long as nothing writes to it; `deps_sort()` and `deps_cycle()` allocate their working memory, including `deps_cycle()`'s sof_set, through the graph's allocator, which must then be safe to call from several threads, as the standard one is. Every other function writes, and needs the graph to itself. With a readers-writer lock, take the write lock for `deps_add()`, `deps_add_list()`, `deps_depend()`, `deps_clear()` and `deps_free()`, and the read lock for the rest. The program uses one thread.
