# concord

A concordance maker in c: every word of a text, in order, with how often it appears and the lines it is on, in memory from arenas and pools.

`concord` reads a text and prints each word in it, in lower case and in byte order, with the number of times it appears and the numbers of the lines it is on: `the 3: 1 2` is a word on lines 1 and 2, three times in all. With `-w` it prints only the words asked for, which looks them up in a long text, and with `-c` only their counts. With `-t` it allocates everything through a tracker and reports, on standard error, what each allocator holds, what the tracker counted, and that nothing was leaked.

_main.c_ is the program. The concordance is kept by _concord.c_, declared in _concord.h_, in a `concord_t` that the program provides and sets up with `concord_init()`, giving the allocator it gets its memory from. `concord_read()` adds every line of a file, and `concord_add_line()` adds one line. `concord_find()` looks up a word, and the words can be walked in order through the `concord_t`'s tree. `concord_free()` gives back all its memory.

The point of the program is where that memory comes from, the allocators of [sof_mem](../../sof_mem/), each doing what it is good at:

- **The words' text** lasts as long as the concordance and is never freed a word at a time, so it comes from an arena, `strings`: each word is a pointer bump, with no header and no size to keep, and the whole arena goes at once in `concord_free()`.
- **Each line**, and a lower-case copy of its words while they are looked up, is needed only until the next line, so it comes from a second arena, `scratch`, which goes back to a mark when the line is done. `concord_read()` marks the arena, and for each line goes back to the mark and reads the line there, growing it with `sof_mem_arena_realloc()`, which grows the most recent allocation in place. `concord_add_line()` makes a mark of its own, takes room for the words after the line, and goes back to its mark when it is done, so marks nest like a stack, and each function gives back exactly what it took and leaves its caller's allocations alone. The arena asks its parent for one block, of 64 KiB, and uses it for every line.
- **The tree's nodes, the words' records and the lists of lines** are many small objects of three sizes, so each size has a pool. The words are kept in a [sof_rbtree](../../sof_rbtree/), which takes a `sof_alloc_t` for its nodes: it is given `sof_mem_pool_allocator()` of a pool of nodes, so the tree's library, unchanged, allocates from the pool. The records, a `concord_word_t` for each word, and the places, a `concord_place_t` for each line a word is on, are taken from their pools directly with `sof_mem_pool_alloc()`.
- **The tracker**, with `-t`, is the parent of every one of these, so it sees the blocks the arenas and pools take from the standard functions: a few hundred blocks for a million words. It is read before `concord_free()`, to show what the concordance holds, and after, to check that every block came back with the right size.

The concordance is built on the [sof_mem](../../sof_mem/) library in _../../sof_mem/_ (_sof_mem.h_, _sof_mem_internal.h_, and _sof_mem_arena.c_, _sof_mem_pool.c_ and _sof_mem_track.c_, the three allocators it uses), and the [sof_rbtree](../../sof_rbtree/) library in _../../sof_rbtree/_ (_sof_rbtree.c_ and _sof_rbtree.h_). Memory comes through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program lower-cases the words given with `-w` with the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), and reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    concord_t c;
    concord_init(&c, NULL);                         // the allocators' parent: the standard functions
    concord_read(&c, stdin);                        // every line
    const concord_word_t *w = concord_find(&c, "love");
    for (const concord_place_t *p = w ? w->first : NULL; p != NULL; p = p->next) {
        printf("%zu\n", p->line);                   // each line love is on
    }
    concord_free(&c);

## Building

    make            # builds ./concord
    make debug=1    # builds with debug info and without optimization
    ./concord -t -c -w love ../../test-data/shakespeare.txt   # a count, and the memory report

`make` in the library's directory, _../_, builds it with the library, as do `make sof_mem` and `make` from the root of the project. The program's Makefile compiles the three allocators' files it uses into its own directory, as every program here does with the libraries it uses.

To build it in another project, copy _main.c_, _concord.c_ and _concord.h_, with _sof_mem.h_, _sof_mem_internal.h_, _sof_mem_arena.c_, _sof_mem_pool.c_ and _sof_mem_track.c_ from _../../sof_mem/_, _sof_rbtree.c_ and _sof_rbtree.h_ from _../../sof_rbtree/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it. To use only the concordance, _concord.c_ and _concord.h_ need just the _sof_mem_ files, less _sof_mem_track.c_, and the _sof_rbtree_ and _sof_alloc_ files.

## Usage

    concord [-h] [-c] [-t] [-w word]... [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-c`: print each word with its count only, not the lines it is on.
- `-t`: allocate everything through a `sof_mem_track_t`, and report on standard error, after the words, how many words and lines were read, what each arena and pool holds from it, and its counts; then, once the concordance is freed, that no blocks are live, or each one that is. A free of the wrong size, or of a block it never gave, is reported as it happens.
- `-w` _word_: print only _word_, in lower case, in the order given; a word that isn't in the text is printed with a count of **0**. Can be given more than once. An apostrophe in _word_ is typed as `'`.
- _file_: the text. With no _file_, or `-`, it reads standard input. With several, they are read as one text, the lines of each numbered on from the last line of the one before.

A word is a run of ASCII letters, with an apostrophe between two letters, `'` or the UTF-8 right single quotation mark, `’`, kept as `'`: so _yorick’s_ and _o'er_ are words, and the apostrophe of _'tis_ isn't part of it. Every other byte separates words. Lines are numbered from **1**, and end with a newline, or a carriage return and a newline. Each word is printed as `word count: line line...`, with each line it is on once, in order, or `word count` with `-c`. It exits with **0**; **1** if a file couldn't be read, memory ran out, or the tracker found a leak or a bad free, after saying so on standard error; or **2** if the options are wrong.

For example, a short text, with a curly apostrophe in _dog’s_:

    $ printf 'The cat sat.\nThe dog\342\200\231s bone, the cat'"'"'s toy.\n' | ./concord
    bone 1: 2
    cat 1: 1
    cat's 1: 2
    dog's 1: 2
    sat 1: 1
    the 3: 1 2
    toy 1: 2

some words in the complete works of Shakespeare (see _../../test-data/README.md_):

    $ ./concord -w yorick -w "yorick's" ../../test-data/shakespeare.txt
    yorick 1: 40166
    yorick's 1: 40157

and some counts, with the memory report:

    $ ./concord -t -c -w love -w hate -w "all's" -w zounds ../../test-data/shakespeare.txt
    love 2353
    hate 186
    all's 44
    zounds 23
    971290 words, 27089 distinct, on 196398 lines
    strings arena      327840 bytes for the words' text
    scratch arena       65568 bytes for a line and its words
    node pool         1310080 bytes for 27089 tree nodes of 48 bytes
    word pool          917504 bytes for 27089 records of 32 bytes
    place pool       15007744 bytes for 937464 places of 16 bytes
    tracker          17628736 bytes live in 269 blocks, peak 17628736 bytes in 269 blocks
                          269 allocations, 0 reallocations, 0 frees, 0 failures, 0 problems
    after concord_free(): no blocks live, no leaks

The text of 27,089 words takes 5 blocks of the strings arena, and nearly 200,000 lines, read one after another in the same place, one block of the scratch arena. The tree's 27,089 nodes, records and 937,464 places take 263 slabs of 64 KiB from their pools, so a million words and nearly a million places come from 269 calls to the standard `malloc()`, and the tracker's counts show the arenas and pools never gave a block back, or grew one, until `concord_free()` gave back all of them.

## Functions

`concord_error_t concord_init(concord_t *c, const sof_alloc_t *alloc)`

Initialize the concordance _c_ the caller provides, empty. _alloc_ is the parent allocator every arena and pool gets its memory from, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into _c_, so it can be on the stack. Nothing is allocated until the first word. _c_ must not move afterwards, since the tree's allocator holds the address of its pool of nodes. Returns **CONCORD_OK**, or **CONCORD_ERROR** if _c_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves _c_ empty.

`concord_error_t concord_read(concord_t *c, FILE *f)`

Add every line of _f_, read to its end, as `concord_add_line()` does. A line ends with a newline, and a carriage return before it, neither of which is part of it; the last line needn't end with one, and a line can be of any length. Each line is read into the scratch arena, which is reset to a mark after it. Returns **CONCORD_OK**, **CONCORD_NO_MEM** if memory ran out, or **CONCORD_ERROR** if _f_ is **NULL** or couldn't be read; on an error, the lines before it are added.

`concord_error_t concord_add_line(concord_t *c, const char *line, size_t length)`

Add the words of one line, the _length_ bytes at _line_, which needn't end with a NUL, numbered one more than the last line added. Words are as Usage describes, and each is kept in lower case with any apostrophe as `'`: its count goes up by one, and the line is added to its list unless it is already the last there. The words are copied into the scratch arena while they are looked up, and taken back with a mark when the line is done. Returns **CONCORD_OK**; **CONCORD_NO_MEM** if memory ran out, when the line is counted and the words before the one that failed are added; or **CONCORD_ERROR** if _line_ is **NULL** and _length_ isn't **0**.

`const concord_word_t *concord_find(const concord_t *c, const char *word)`

Return the record of _word_, a NUL-terminated string in lower case and with any apostrophe as `'`, as words are kept, or **NULL** if it isn't in the concordance or _word_ is **NULL**. The record is good until `concord_free()`.

`concord_error_t concord_free(concord_t *c)`

Give back everything the concordance allocated: the tree's nodes go back to their pool, then every pool and arena gives its blocks back to the parent. The `concord_t` itself belongs to the caller and isn't freed; it is left empty. Returns **CONCORD_OK**.

Every function accepts a **NULL** _c_: `concord_find()` returns **NULL**, and the others return **CONCORD_ERROR**. A concordance that was freed, or whose `concord_init()` failed, is empty and stays that way: `concord_find()` finds nothing and the others return **CONCORD_ERROR**, until it is initialized again.

## Types

    typedef enum concord_error_t {
        CONCORD_OK = 0,
        CONCORD_NO_MEM,
        CONCORD_NO_MATCH,       // never returned
        CONCORD_ERROR,
    } concord_error_t;

    typedef struct concord_place_t {
        size_t line;                    // the line's number, from 1
        struct concord_place_t *next;   // the next line the word is on, or NULL
    } concord_place_t;

    typedef struct concord_word_t {
        size_t count;                   // the times the word appears
        size_t lines;                   // the lines it is on: the length of the list
        concord_place_t *first;         // the list, in order
        concord_place_t *last;
    } concord_word_t;

A word's record, and a place in its list of lines. The application can read them, and follow the list from _first_ by _next_, but should not change them.

    typedef struct concord_t {
        sof_rbtree_t tree;              // word to record: each key the word, each data its concord_word_t
        sof_mem_arena_t strings;        // the words' text
        sof_mem_arena_t scratch;        // each line and its words
        sof_mem_pool_t nodes;           // the tree's nodes
        sof_mem_pool_t words;           // the records
        sof_mem_pool_t places;          // the places
        size_t total;                   // words read, counting each time
        size_t lines;                   // lines read
        sof_alloc_t alloc;              // copy of the parent; zeroed for the standard functions
        int ready;                      // 1 once concord_init() has succeeded
    } concord_t;

A concordance. The fields can be read but should not be changed by the application. The tree orders the words by their bytes, so `sof_rbtree_minimum()` and `sof_rbtree_next()` on _tree_, which don't change it, walk them in order; `tree.count` is the number of distinct words. The `allocated` field of each arena and pool is what it holds from the parent.

## Threading

_concord.c_ has no globals and does no locking, so separate concordances can be used on separate threads freely. `concord_find()`, and walking the tree with `sof_rbtree_minimum()` and `sof_rbtree_next()`, only read, so any number of threads can do them at once as long as nothing writes. `concord_read()` and `concord_add_line()` change the tree, the pools and the scratch arena, so they need the concordance to themselves, as `concord_free()` does. With a readers-writer lock, take the read lock for `concord_find()` and walks, and the write lock for the rest. The parent allocator is called only by those that write, so it needn't be safe to call from several threads unless several concordances share it. The program uses one thread.
