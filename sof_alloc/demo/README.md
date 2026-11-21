# memstat

An allocation counter in c: a word count run through an allocator of the program's own, reporting every allocation the libraries made.

`memstat` counts the words of a text, and prints the most frequent, the way any small program would: each word is built in a [sof_string](../../sof_string/), counted in a [sof_hashmap](../../sof_hashmap/) that keeps a copy of it, and the counts are sorted in a [sof_vector](../../sof_vector/). What makes it different is the `sof_alloc_t` it gives all three: a counting allocator of its own, which passes every call on to the standard functions and notes what goes through. At the end, once everything is freed, it prints the calls, the bytes asked for, the bytes and blocks live at the peak and now, every free given the wrong size or a block it never handed out, and a histogram of the sizes asked for, by power of two. Blocks still live then are leaks. It shows how an application plugs an allocator into the libraries without changing them, and what they really ask for.

_main.c_ is the program. The allocator is in _memstat.c_, declared in _memstat.h_: a `memstat_t` that the program provides and sets up with `memstat_init()`, giving the allocator it gets its memory from. `memstat_allocator()` returns the `sof_alloc_t` to pass to the libraries' `init` functions, `memstat_report()` prints what it counted, and `memstat_free()` frees whatever is still live and the allocator's own memory.

The allocator keeps each live block in a [sof_hashmap](../../sof_hashmap/) of its own, keyed by the block's address, with the size it was allocated with as the element's data, so every free and realloc is checked against it, as [sof_alloc](../../sof_alloc/) promises each one is given the size. That map gets its memory from the parent allocator, so it isn't counted. A free of the wrong size is counted and done with the right one, as a real allocator relying on the size would need, and a free of a block that isn't live is counted and ignored. A realloc takes the block out of the map before calling the parent, and puts it back after, at its new address or its old one; since [sof_hashmap](../../sof_hashmap/) promises that an insert after a remove never grows the map, putting back a block it held a moment before never allocates, so the count can't be lost. The histogram's buckets come from `sof_bit_ceil_log2()` in [sof_bit](../../sof_bit/).

The counting allocator's map is a [sof_hashmap](../../sof_hashmap/) from _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), as is the program's map of words; it hashes with the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_, _sof_hash.h_ and _rapidhash.h_, which is [rapidhash](https://github.com/Nicoshev/rapidhash), under its own MIT license). The histogram's buckets come from the [sof_bit](../../sof_bit/) library in _../../sof_bit/_ (_sof_bit.h_). Words are built with the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_) and sorted with the [sof_vector](../../sof_vector/) library in _../../sof_vector/_ (_sof_vector.c_ and _sof_vector.h_), which sorts with [sof_sort](../../sof_sort/) in _../../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_). Memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    memstat_t stat;
    memstat_init(&stat, NULL);                      // its memory from the standard functions
    sof_alloc_t alloc = memstat_allocator(&stat);   // to give any library
    sof_hashmap_t map;
    sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, &alloc);
    ...
    sof_hashmap_free(&map);
    memstat_report(&stat, stdout);                  // what the map asked for; live is its leaks
    memstat_free(&stat);

## Building

    make            # builds ./memstat
    make debug=1    # builds with debug info and without optimization
    ./memstat ../../test-data/shakespeare.txt   # the ten most common words, and what they cost

`make` in the library's directory, _../_, builds it with the library, as do `make sof_alloc` and `make` from the root of the project.

To build it in another project, copy _main.c_, _memstat.c_ and _memstat.h_, with _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_bit.h_ from _../../sof_bit/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_vector.c_ and _sof_vector.h_ from _../../sof_vector/_, _sof_sort.c_ and _sof_sort.h_ from _../../sof_sort/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it. To use only the counting allocator, _memstat.c_ and _memstat.h_ need just the _sof_hashmap_, _sof_hash_, _sof_bit_ and _sof_alloc_ files.

## Usage

    memstat [-h] [-e count] [-t count] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-e` _count_: initialize the map of words for _count_ distinct words (default **0**), so that it needn't grow while it fills, as `sof_hashmap_init()`'s _expected_keys_ does.
- `-t` _count_: print the _count_ most frequent words, with their counts, highest first and then in byte order (default **10**). **0** prints none, and doesn't sort.
- _file_: the text. With no _file_, or `-`, it reads standard input. With several, they are counted together.

A word is a run of ASCII letters and digits and bytes of UTF-8 sequences, so punctuation and spaces separate words, and ASCII letters are counted in lower case. Files are read in chunks of 64 KiB, and a word can span two.

It prints the most frequent words, the number of words and of distinct words, then the report: the calls the libraries made and how many failed, the bytes asked for by every malloc and realloc, the most bytes and blocks live at once, the bytes and blocks still live after everything was freed, the problems found, and the histogram. Each line of the histogram is a range of sizes, from one more than a power of two to the next, the number of mallocs and reallocs that asked for a size in it, and a bar. The report is printed even if a file couldn't be read, since the memory is freed and checked either way. It exits with **0**; **1** if a file couldn't be read, memory ran out, or a block was leaked or freed wrongly, after saying so on standard error; or **2** if the options are wrong.

For example, with the complete works of Shakespeare (see _../../test-data/README.md_):

    $ ./memstat -t 3 ../../test-data/shakespeare.txt
         30497  the
         28497  and
         21694  i
    972577 words, 30054 distinct
    calls     30070 malloc, 16 realloc, 30070 free, 0 failed
    asked     4934855 bytes in all
    peak      2838282 bytes in 30058 blocks
    live      0 bytes in 0 blocks
    problems  0 wrong sizes, 0 unknown blocks
               block size       count
                        1           1  #
                        2          27  #
                      3-4         995  ##
                      5-8       13902  ####################################
                     9-16       15034  ########################################
                    17-32          99  #
                    33-64           2  #
                  129-256           1  #
                  257-512           2  #
                 513-1024           2  #
                1025-2048           2  #
                2049-4096           2  #
                4097-8192           2  #
               8193-16384           2  #
              16385-32768           2  #
             32769-65536           2  #
            65537-131072           2  #
           131073-262144           2  #
           262145-524288           3  #
          524289-1048576           1  #
         1048577-2097152           1  #

The copies of the words are nearly all the blocks, most of them 16 bytes or less. The map's array doubles 12 times, from 16 slots to 65,536, a block in each range from 257-512 up; with `-e 30054` it is allocated once, at its full size, and the 12 smaller arrays are never asked for. The vector of words to sort is the block of 129-256 bytes, room for 16 words, and the other block in each range above it up to 524288 bytes, as its 11 reallocs double it; the third block of 262145-524288 bytes is the buffer of its stable sort. The other 5 reallocs are the word being read growing to the longest word.

## Functions

`memstat_error_t memstat_init(memstat_t *s, const sof_alloc_t *alloc)`

Initialize the counting allocator _s_ the caller provides, with every count **0**. _alloc_ is the parent, which every call is passed on to and which the map of live blocks gets its memory from, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied, so it can be on the stack. Returns **MEMSTAT_OK**, **MEMSTAT_NO_MEM** if the map couldn't be allocated, or **MEMSTAT_ERROR** if _s_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves _s_ empty.

`sof_alloc_t memstat_allocator(memstat_t *s)`

Return a `sof_alloc_t` whose _malloc_, _realloc_ and _free_sized_ go through _s_, to pass to the `init` function of any library here. It holds _s_'s address, so _s_ must not move while it is in use. Its _malloc_ allocates from the parent and records the block, counting a failure if the parent, or the map, can't. Its _free_sized_ looks the block up, counts a wrong size and uses the right one, or counts an unknown block and does nothing, and frees through the parent. Its _realloc_ checks the block as a free does and resizes it through the parent from its real size. If _s_ is **NULL** or not initialized, the `sof_alloc_t` is still valid, but every allocation through it fails.

`memstat_error_t memstat_report(const memstat_t *s, FILE *stream)`

Print the counts to _stream_, as in the example above: the calls, the bytes asked for, the peak, what is live, the problems, and the histogram, with a line for each range of sizes that was asked for. Returns **MEMSTAT_OK**, or **MEMSTAT_ERROR** if _s_ or _stream_ is **NULL**.

`memstat_error_t memstat_free(memstat_t *s)`

Free every block still live, through the parent with its size, then the map. The `memstat_t` itself belongs to the caller and isn't freed; it is left empty, with every count **0**, so read them first. Returns **MEMSTAT_OK**.

Every function accepts a **NULL** _s_: `memstat_allocator()` returns an allocator that fails, and the others return **MEMSTAT_ERROR**. A counting allocator that was freed, or whose `memstat_init()` failed, is empty and stays that way: allocations through it fail, without being counted, `memstat_report()` prints zeros and `memstat_free()` returns **MEMSTAT_ERROR**, until it is initialized again.

## Types

    typedef enum memstat_error_t {
        MEMSTAT_OK = 0,
        MEMSTAT_NO_MEM,
        MEMSTAT_NO_MATCH,       // never returned
        MEMSTAT_ERROR,
    } memstat_error_t;

    typedef struct memstat_t {
        size_t mallocs;         // successful mallocs
        size_t reallocs;        // successful reallocs
        size_t frees;           // successful frees
        size_t failures;        // mallocs and reallocs that failed
        size_t wrong_sizes;     // frees and reallocs given the wrong size
        size_t unknown;         // frees and reallocs of blocks that aren't live
        size_t blocks;          // live blocks
        size_t bytes;           // bytes in live blocks
        size_t peak_blocks;     // the most blocks live at once
        size_t peak_bytes;      // the most bytes live at once
        size_t total_bytes;     // bytes asked for by every malloc and realloc
        size_t histogram[MEMSTAT_BUCKETS]; // sizes asked for, by power of two
        sof_hashmap_t live;     // block to size: each element's data is the size
        sof_alloc_t alloc;      // copy of the parent; zeroed for the standard functions
        int ready;              // 1 once memstat_init() has succeeded
    } memstat_t;

A counting allocator. The application can read the fields at any time, but should not change them. _histogram_[_k_] counts the sizes of more than 2^(_k_ - 1) and at most 2^_k_ bytes, and _histogram_[0] those of 1 byte; **MEMSTAT_BUCKETS** is one more than the bits in a `size_t`, so every size has a bucket.

## Threading

_memstat.c_ has no globals and does no locking, so separate counting allocators can be used on separate threads freely. Every call through the `sof_alloc_t` changes the counts and the map, so one counting allocator shared by structures on several threads needs a lock around each call, which means wrapping its functions in an allocator of the application's that takes the lock. `memstat_report()` only reads, and needs only that no allocation is under way. The program uses one thread.
