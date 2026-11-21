# uniq

A duplicate line remover in c: each different line once, in the order it was first seen, without sorting.

`uniq` prints each different line of files or standard input once, keeping the first of each. Unlike the standard tool, the copies of a line don't have to be next to each other, so the input needn't be sorted, and the lines come out in the order they were first seen. With `-c` each line is printed after the number of times it occurs, and with `-d` only the lines that occur more than once are printed. With `-a` the lines seen are kept in a Bloom filter of fixed size rather than in full, so memory doesn't grow with the input, but now and then a new line is taken for one seen before and left out; `-b` makes that a blocked filter, faster but a little larger. A line is the bytes up to a newline, NULs included, so it works on binary data too.

_main.c_ is the program. The lines are kept by _uniq.c_, declared in _uniq.h_, in a `uniq_t` that the program provides and sets up with `uniq_init()`. `uniq_add()` adds a line, or counts it again if it was added before, and says which; `uniq_find()` looks a line up. `uniq_next()` and `uniq_traverse()` go through the different lines in the order they were first seen, each with its count. `uniq_clear()` forgets every line, and `uniq_free()` releases the memory.

Inside, each different line is a `uniq_line_t` holding a copy of its bytes, its count and its place in the order. A hashmap, keyed by the line's bytes, finds it, and a vector of pointers to the same records keeps the order. Records are allocated one by one, so a pointer to one stays good until the set is cleared or freed. Without `-c` or `-d`, the program prints each line as soon as it is first seen, so it works on a stream that never ends; with either, it prints nothing until all the input is read. With `-a`, the program doesn't use the set at all: it adds each line to a `sof_bloom_t`, classic or, with `-b`, blocked, and prints the line if the filter didn't already contain it.

The lines are kept in a [sof_hashmap](../../sof_hashmap/) from _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), whose keys are hashed by the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_ and _sof_hash.h_, which use [rapidhash](https://github.com/Nicoshev/rapidhash) in _rapidhash.h_, under its own MIT license), and in a [sof_vector](../../sof_vector/) from _../../sof_vector/_ (_sof_vector.c_ and _sof_vector.h_), which uses the [sof_sort](../../sof_sort/) library in _../../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_). Each line's bytes are a [sof_string](../../sof_string/) from _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), which the program also gathers lines in. With `-a`, the lines are added to a [sof_bloom](../../sof_bloom/) filter from _../../sof_bloom/_ (_sof_bloom.c_ and _sof_bloom.h_), whose bits are words from the [sof_bit](../../sof_bit/) library in _../../sof_bit/_ (_sof_bit.h_) and which uses the c library's maths functions, so the program is linked with `-lm`. Memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    uniq_t u;
    uniq_init(&u, 0, NULL);                              // no allocator: the standard functions
    int created;
    uniq_add(&u, "b", 1, &created);                      // created is 1: a new line
    uniq_add(&u, "a\0z", 3, &created);                   // lines can hold NULs
    const uniq_line_t *b = uniq_add(&u, "b", 1, &created); // created is 0, b->count is 2
    size_t position = 0;
    const uniq_line_t *line;
    while ((line = uniq_next(&u, &position)) != NULL) {  // "b", then "a\0z"
        fwrite(line->line.data, 1, line->line.length, stdout);
    }
    uniq_free(&u);

## Building

    make            # builds ./uniq
    make debug=1    # builds with debug info and without optimization
    ./uniq -c -d ../../test-data/shakespeare.txt   # the lines Shakespeare repeats, with their counts
    ./uniq -a ../../test-data/shakespeare.txt      # the different lines, in a fixed 1.8 MB filter

`make` in the library's directory, _../_, builds it with the library, as do `make sof_bloom` and `make` from the root of the project.

To build it in another project, copy _main.c_, _uniq.c_ and _uniq.h_, with _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_vector.c_ and _sof_vector.h_ from _../../sof_vector/_, _sof_bloom.c_ and _sof_bloom.h_ from _../../sof_bloom/_, _sof_bit.h_ from _../../sof_bit/_, _sof_sort.c_ and _sof_sort.h_ from _../../sof_sort/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it, and link with `-lm`. To use only the set of lines, _uniq.c_ and _uniq.h_ need all of these but _sof_copt_, _sof_bloom_ and _sof_bit_.

## Usage

    uniq [-h] [-a [-b] [-n lines] [-p rate] | -c | -d] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-a`, or `--approximate`: keep the lines seen in a Bloom filter instead of in full. Its memory is set when the program starts, by `-n` and `-p`, and doesn't grow however much is read. The price is that a new line whose bits the filter already has set is taken for one seen before and left out; a line seen before is never printed twice. It can't count lines, so it can't be used with `-c` or `-d`.
- `-b`: with `-a`, use a blocked filter (see [sof_bloom](../../sof_bloom/)), which keeps each line's bits in one cache line. It is faster, above all for filters larger than the cache, but takes more memory for the same `-p`: about 16.9 bits a line rather than 14.4 at the default **0.001**.
- `-n` _lines_: with `-a`, the number of different lines to size the filter for (default **1000000**). More can be read, but the chance of leaving one out then rises above `-p`.
- `-p` _rate_: with `-a`, the chance, once `-n` different lines have been read, that a new line is left out, more than **0** and less than **1** (default **0.001**). Each tenth as large adds about 4.8 bits a line to the filter.
- `-c`: print each line after the number of times it occurs, right-aligned in seven columns and followed by a space.
- `-d`: print only the lines that occur more than once, each once. With `-c` too, their counts are printed.
- _file_: the lines to read. With no _file_, or `-`, it reads standard input. With several, they are read in turn as one input, so a line in two files is a duplicate.

It exits with **0**, **1** if a file couldn't be read, memory ran out or the output couldn't be written, or **2** if the options are wrong (including `-a` with `-c` or `-d`, or `-b`, `-n` or `-p` without `-a`). A file that can't be opened or read is reported and the others are still read; running out of memory, or failing to write, stops the program.

A line is every byte up to a newline, NULs included, and two lines are the same only if their bytes are: a carriage return before the newline is part of the line, and case and spaces count. The last line of a file needn't end with a newline; it is printed with one, like every line, and is the same as the line with one.

For example:

    $ printf 'apple\nbanana\napple\ncherry\nbanana\napple\n' > fruit
    $ ./uniq fruit
    apple
    banana
    cherry
    $ ./uniq -c fruit
          3 apple
          2 banana
          1 cherry
    $ ./uniq -d fruit
    apple
    banana

The complete works of Shakespeare, from Project Gutenberg (see _../../test-data/README.md_), have 196,398 lines, of which 119,210 are different:

    $ ./uniq ../../test-data/shakespeare.txt | wc -l
      119210

With `-a`, the default filter (1,000,000 lines at 0.001, about 1.8 MB) misses none of them, while one sized for fewer lines than there are leaves some out:

    $ ./uniq -a ../../test-data/shakespeare.txt | wc -l
      119210
    $ ./uniq -a -n 200000 -p 0.01 ../../test-data/shakespeare.txt | wc -l
      119199
    $ ./uniq -a -n 1000 -p 0.1 ../../test-data/shakespeare.txt | wc -l
        2940

A blocked filter does the same in a little more memory (about 2.1 MB by default), and fills up sooner when given far more lines than it was sized for, since each line sets 8 bits where a classic filter at 0.1 sets 3:

    $ ./uniq -a -b ../../test-data/shakespeare.txt | wc -l
      119210
    $ ./uniq -a -b -n 200000 -p 0.01 ../../test-data/shakespeare.txt | wc -l
      119195
    $ ./uniq -a -b -n 1000 -p 0.1 ../../test-data/shakespeare.txt | wc -l
        2061

and the first lines it repeats are blank lines, one with spaces and the empty line (each ends with a carriage return, shown here as ^M), then lines of the sonnets that _The Passionate Pilgrim_ prints again:

    $ ./uniq -c -d ../../test-data/shakespeare.txt | head -5 | cat -v
          7     ^M
      42458 ^M
          2   But do not so, I love thee in such sort,^M
          2   As thou being mine, mine is thy good report.^M
          2 When my love swears that she is made of truth,^M

## Functions

`uniq_error_t uniq_init(uniq_t *u, size_t expected, const sof_alloc_t *alloc)`

Initialize the set of lines _u_ the caller provides, with room for _expected_ different lines before its hashmap and vector grow (**0** for a few; more can always be added). _alloc_ is the allocator for all of the set's memory, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into the set, so it can be on the stack. Returns **UNIQ_OK**, **UNIQ_NO_MEM** if memory couldn't be allocated, or **UNIQ_ERROR** if _u_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves _u_ empty.

`const uniq_line_t *uniq_add(uniq_t *u, const char *bytes, size_t length, int *created)`

Add the line made of the _length_ bytes of _bytes_, which can hold NULs and needn't be NUL-terminated; **NULL** _bytes_ is the empty line, whatever _length_ is. A new line is copied, so _bytes_ can be in a buffer the caller reuses, and goes after every line seen so far with a count of 1; a line seen before has its count raised by one and keeps its place. If _created_ isn't **NULL**, it is set to **1** for a new line, or **0** for one seen before or if the call fails. Returns the line, or **NULL** if memory ran out, in which case nothing was added or counted.

`const uniq_line_t *uniq_find(const uniq_t *u, const char *bytes, size_t length)`

Return the line made of the _length_ bytes of _bytes_ (**NULL** _bytes_ being the empty line), or **NULL** if it wasn't added. Nothing is counted.

`const uniq_line_t *uniq_next(const uniq_t *u, size_t *position)`

Return the next line in the order the lines were first seen, or **NULL** when there are no more. Set _*position_ to **0** to start; each call updates it. Lines added during the loop come after the others, so the loop sees them too.

    size_t position = 0;
    const uniq_line_t *line;
    while ((line = uniq_next(&u, &position)) != NULL) {
        ...
    }

`int uniq_traverse(const uniq_t *u, uniq_traverse_func_t cb, void *ctx)`

Call _cb_ with each line, in the order the lines were first seen, and _ctx_. If _cb_ returns a non-zero value, the traversal stops and returns that value. Otherwise returns **0** once every line has been visited (at once, if _cb_ is **NULL**).

`uniq_error_t uniq_clear(uniq_t *u)`

Forget every line, keeping the memory of the hashmap and vector for reuse; the next line added is the first again. Returns **UNIQ_OK**.

`uniq_error_t uniq_free(uniq_t *u)`

Free everything the set allocated. The `uniq_t` itself belongs to the caller and isn't freed; it is left empty. Returns **UNIQ_OK**.

Every function accepts a **NULL** _u_: `uniq_add()`, `uniq_find()` and `uniq_next()` return **NULL**, `uniq_traverse()` returns **-1**, and the others return **UNIQ_ERROR**. `uniq_next()` returns **NULL** for a **NULL** _position_. A set that was freed, or whose `uniq_init()` failed, is empty and stays that way: it finds nothing, `uniq_add()` returns **NULL**, `uniq_traverse()` returns **-1** and `uniq_clear()` returns **UNIQ_ERROR**, until it is initialized again.

## Types

    typedef enum uniq_error_t {
        UNIQ_OK = 0,
        UNIQ_NO_MEM,
        UNIQ_NO_MATCH,
        UNIQ_ERROR,
    } uniq_error_t;

    typedef struct uniq_line_t {
        sof_string_t line;      // a copy of the line's bytes, NULs included
        size_t count;           // the times it was added
        size_t index;           // its place in the order lines were first seen, from 0
    } uniq_line_t;

One different line. The fields can be read but must not be changed. _line.data_ holds _line.length_ bytes followed by a NUL, so a line without NULs can be used as a c string. A `uniq_line_t` doesn't move, so a pointer to one is good until `uniq_clear()` or `uniq_free()`.

    typedef struct uniq_t {
        sof_hashmap_t map;      // the lines, keyed by their bytes
        sof_vector_t lines;     // pointers to the lines, in the order first seen
        size_t total;           // lines added, counting each time
        sof_alloc_t alloc;      // copy of the allocator; zeroed for the standard functions
    } uniq_t;

The fields can be read but should not be changed by the application. _lines.count_ is the number of different lines.

    typedef int (*uniq_traverse_func_t)(const uniq_line_t *line, void *ctx)

Called for each line visited by `uniq_traverse()`, with the _ctx_ given to it. Return **0** to continue, or a non-zero value to stop the traversal; that value is returned by `uniq_traverse()`. It must not call `uniq_add()`, `uniq_clear()` or `uniq_free()`.

## Threading

_uniq.c_ has no globals and does no locking, so separate sets can be used on separate threads freely. `uniq_find()`, `uniq_next()` and `uniq_traverse()` only read the set, and allocate nothing, so any number of threads can call them at once as long as nothing writes to it; `uniq_add()`, `uniq_clear()`, `uniq_free()` and `uniq_init()` write, and need the set to themselves. With a readers-writer lock, take the write lock for those and the read lock for the rest. The program is single-threaded.
