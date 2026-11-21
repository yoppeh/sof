# top

A finder of the most frequent words, lines or runs of words in a text of any size, in c.

`top` counts the words of files or standard input, or the runs of _n_ words in a row (n-grams of words), or the whole lines, and prints how many there were, how many were different, and the most frequent. Memory goes with the number of different keys and the number printed, not with the size of the input: the text is read a line at a time, the counts kept in a hashmap, and the top _N_ picked with a min-heap of _N_ entries rather than by sorting every key.

_main.c_ is the program. The counting and picking are done by _top.c_, declared in _top.h_, which keeps a `top_t` that the program provides and sets up with `top_init()`. `top_add_line()` counts the words, the n-grams or the line itself in a line of text, and `top_flush()` ends a text, so no n-gram spans two files. `top_add()` counts any key directly. `top_find()` gives the count of one key and `top_next()` loops over them all. `top_select()` picks the most frequent, and `top_entry_compare()` orders entries the way it does, for sorting. `top_clear()` forgets the counts and `top_free()` releases the counter's memory.

`top_select()` walks the map once, keeping the best _N_ keys so far in a heap whose smallest entry is the worst of them; a key better than that one replaces it, in O(log _N_). Then it empties the heap into the caller's array from the back, so the best comes first. Picking the top _N_ of _D_ different keys takes O(_D_ log _N_) time and room for _N_ entries, where sorting takes O(_D_ log _D_) time and room for _D_. Ties are broken by the keys' bytes: of two keys counted equally often, the one `strcmp()` puts first comes first, and is kept when only one fits. So the result doesn't depend on the order of the hashmap, or of the text.

_bench.c_ is the benchmark. Keys are counted in a hashmap from the [sof_hashmap](../../sof_hashmap/) library in _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), which hashes them with the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_ and _sof_hash.h_, which use [rapidhash](https://github.com/Nicoshev/rapidhash) in _rapidhash.h_, under its own MIT license). The top is picked with a heap from the [sof_heap](../../sof_heap/) library in _../../sof_heap/_ (_sof_heap.c_ and _sof_heap.h_). Lines are split into words by the [sof_regex](../../sof_regex/) library in _../../sof_regex/_ (_sof_regex.c_ and _sof_regex.h_), whose regular expressions are [PCRE2](https://github.com/PCRE2Project/pcre2)'s, so the program and the benchmark link with PCRE2's 8-bit library, found with _pcre2-config_. Lines and n-grams are held in strings from the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_). Memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program and the benchmark read their options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_), and the benchmark sorts with the [sof_sort](../../sof_sort/) library in _../../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_); the benchmark times with the [sof_timer](../../sof_timer/) library in _../../sof_timer/_ (_sof_timer.c_ and _sof_timer.h_). Beyond those and PCRE2, it needs nothing but the c standard library.

    top_options_t options = {TOP_WORDS, 2, NULL, 1}; // pairs of words, ignoring case
    top_t t;
    top_init(&t, &options, NULL);                     // no allocator: the standard functions
    top_add_line(&t, line);                           // for each line, without its newline
    top_flush(&t);                                    // at the end of each file
    top_entry_t best[10];
    size_t found;
    top_select(&t, 10, best, &found);                 // the 10 most frequent pairs, best first
    top_free(&t);

## Building

    make            # builds ./top and ./bench
    make debug=1    # builds with debug info and without optimization
    ./top -i ../../test-data/shakespeare.txt          # the commonest words
    ./bench         # times picking the top against sorting everything

`make` in the library's directory, _../_, builds it with the library, as do `make sof_heap` and `make` from the root of the project. PCRE2 must be installed; the Makefile finds it with _pcre2-config_ if that is on the path, else with _pkg-config_, else links _-lpcre2-8_ from the compiler's own paths.

To build it in another project, copy _main.c_, _top.c_ and _top.h_, with _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_heap.c_ and _sof_heap.h_ from _../../sof_heap/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_regex.c_ and _sof_regex.h_ from _../../sof_regex/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it, and compile and link with PCRE2 (the flags from `pcre2-config --cflags` and `pcre2-config --libs8`, or from `pkg-config libpcre2-8`). To use only the counter, leave out _main.c_ and the _sof_copt_ files.

## Usage

    top [-h] [-l] [-i] [-n words] [-r regex] [-t top] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-l`: count whole lines instead of words. A CR just before a newline isn't part of the line, and an empty line is a key like any other.
- `-i`: ignore case: ASCII letters are folded to lowercase before anything else, so _The_ and _the_ are the same key. Letters outside ASCII aren't folded.
- `-n` _words_: count runs of _words_ words in a row, printed with a single space between them (default 1). Runs go on from one line to the next, but not from one file to the next. Not with `-l`.
- `-r` _regex_: what a word is, as a [PCRE2](https://www.pcre.org/current/doc/html/pcre2pattern.html) regular expression, which matches bytes, not UTF-8 characters (default `[A-Za-z0-9]+(?:(?:'|\xe2\x80\x99)[A-Za-z0-9]+)*`: letters and digits, with an apostrophe, `'` or `’`, between two of them, so _don't_ and _o’er_ are words and quotes around a word aren't part of it). Empty matches are skipped. Not with `-l`.
- `-t` _top_: print the _top_ most frequent (default 10; 0 prints all of them). Keys counted equally often are printed in byte order, which for UTF-8 is code point order, and when they don't all fit, those that come first in that order are printed.
- _file_: the text to count. With no _file_, or `-`, it reads standard input. With several, each is counted in turn.

It exits with **0**, **1** if a file couldn't be read or memory ran out, or **2** if the options are wrong. A line is counted up to its first NUL byte, if it holds one.

For example, the commonest words in the complete works of Shakespeare, from Project Gutenberg (see _../../test-data/README.md_), ignoring case:

    $ ./top -i ../../test-data/shakespeare.txt
    971907 words, 27515 distinct
         30584  the
         28568  and
         22210  i
         21079  to
         18875  of
         16548  a
         14477  you
         13204  my
         12499  in
         11846  that

its commonest three-word phrases:

    $ ./top -i -n 3 -t 5 ../../test-data/shakespeare.txt
    971905 3-grams of words, 802495 distinct
           312  a room in
           252  i pray you
           229  i will not
           210  room in the
           171  i know not

and its commonest lines, the first of which is empty, followed by the speakers' names:

    $ ./top -l -t 5 ../../test-data/shakespeare.txt
    196398 lines, 119210 distinct
         42458  
           472  FALSTAFF.
           455  KING.
           369  DUKE.
           358  HAMLET.

## Benchmark

    bench [-h] [file]

- `-h`: print the usage message and exit.
- _file_: the text to count (default _../../test-data/shakespeare.txt_).

The benchmark counts the text three ways, ignoring case: single words, runs of three words, and lines. For each, and for _N_ of 10, 100, 1,000 and 10,000, it times picking the top _N_ with `top_select()` (heap) against copying every key and count into an array with `top_next()`, sorting it with `sof_sort_quick()` (quick) or `sof_sort_merge()` (merge) in `top_entry_compare()`'s order, and taking the first _N_. It checks that the three give the same entries, in the same order, and reports the best of 5 runs of each, in milliseconds, with how many times faster the heap is than quicksort. Counting the text isn't timed.

    $ ./bench
    196398 lines from ../../test-data/shakespeare.txt, ignoring case; best of 5 runs

    keys      distinct      N   heap ms  quick ms  merge ms  speedup
    words        27515     10     0.148     2.717     3.466    18.4x
    words        27515    100     0.209     2.683     3.448    12.8x
    words        27515   1000     0.499     2.679     3.359     5.4x
    words        27515  10000     3.054     2.638     3.428     0.9x
    3-grams     802495     10     5.937   137.124   179.413    23.1x
    3-grams     802495    100     6.358   136.254   177.765    21.4x
    3-grams     802495   1000     7.295   136.104   177.373    18.7x
    3-grams     802495  10000    17.578   135.215   174.232     7.7x
    lines       118793     10     0.732    15.723    19.350    21.5x
    lines       118793    100     0.776    15.864    19.368    20.4x
    lines       118793   1000     1.582    15.418    19.137     9.7x
    lines       118793  10000     7.550    15.485    18.820     2.1x

That was on an Apple M4 Max, macOS, clang. On an Intel Core i9-13900K, Arch Linux, gcc:

    keys      distinct      N   heap ms  quick ms  merge ms  speedup
    words        27515     10     0.222     3.100     3.398    14.0x
    words        27515    100     0.245     3.097     3.405    12.7x
    words        27515   1000     0.569     3.101     3.408     5.5x
    words        27515  10000     2.938     3.104     3.411     1.1x
    3-grams     802495     10     7.718   168.932   191.153    21.9x
    3-grams     802495    100     7.792   160.831   153.706    20.6x
    3-grams     802495   1000     8.001   152.747   153.421    19.1x
    3-grams     802495  10000    17.302   153.186   152.874     8.9x
    lines       118793     10     0.856    17.204    18.366    20.1x
    lines       118793    100     0.931    17.236    18.371    18.5x
    lines       118793   1000     1.709    17.272    18.362    10.1x
    lines       118793  10000     7.404    17.242    18.374     2.3x

For a top of 10 or 100, much smaller than the number of keys, the heap is 13 to 23 times faster, since most keys are compared once with the heap's worst and passed over; it spends most of its time walking the hashmap. As _N_ nears the number of keys it loses its edge, and at 10,000 of 27,515 words it is no faster than sorting. Either way it needs room for only _N_ entries, where the sorts need room for every key, and merge sort as much again.

## Functions

`top_error_t top_init(top_t *t, const top_options_t *options, const sof_alloc_t *alloc)`

Initialize the counter _t_ the caller provides, to count what _options_ say (see Types), or with **NULL** _options_, single words as `TOP_DEFAULT_WORD` matches them, keeping their case. _alloc_ is the allocator for all of the counter's memory, PCRE2's included, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into the counter, so it can be on the stack. Returns **TOP_OK**, **TOP_NO_MEM** if memory couldn't be allocated, **TOP_BAD_REGEX** if the word pattern doesn't compile, or **TOP_ERROR** if _t_ is **NULL**, _alloc_ has a _malloc_ but no _free_ or _free_sized_, _unit_ isn't a `top_unit_t`, or _n_ is more than 1 for lines; an error leaves _t_ empty.

`top_error_t top_add_line(top_t *t, const char *line)`

Count what is in _line_, a NUL-terminated line without its newline (a NUL in it ends it). For lines, the line is one key; for words, the words are found with the counter's regular expression, empty matches skipped, and each run of _n_ words in a row is a key, joined by single spaces. Runs go on from one line to the next: the last _n_ - 1 words of a line start the runs of the next, until `top_flush()`. With `fold_case`, ASCII letters are folded to lowercase first. Returns **TOP_OK**, **TOP_NO_MEM** if memory ran out, in which case the keys before the one that couldn't be stored are counted and the words in progress are forgotten, as by `top_flush()`, or **TOP_ERROR** if _line_ is **NULL** or PCRE2 stopped a match.

`top_error_t top_flush(top_t *t)`

End a text: forget the words in progress, so the next line's words start new runs. Call it at the end of each file. Lines and single words need no flush. Returns **TOP_OK**.

`top_error_t top_add(top_t *t, const char *key, size_t count)`

Count the NUL-terminated _key_ _count_ times, as it is, whatever the counter's unit, and without folding it. The counter keeps a copy of a new key. A _count_ of **0** does nothing. Returns **TOP_OK**, **TOP_NO_MEM** if the key is new and couldn't be stored, leaving the counts as they were, or **TOP_ERROR** if _key_ is **NULL**.

`size_t top_find(const top_t *t, const char *key)`

Return how many times the NUL-terminated _key_ was counted, as it was counted: folded, and with single spaces between words. Returns **0** if it wasn't, or _key_ is **NULL**.

`const char *top_next(const top_t *t, size_t *position, size_t *count)`

Return the next key, setting _*count_ to its count if _count_ isn't **NULL**, or return **NULL** when there are no more. Set _*position_ to **0** to start; each call updates it. The keys come in no particular order. The key returned is good until the counter is next written, and the loop must not count anything.

    size_t position = 0, count;
    const char *key;
    while ((key = top_next(&t, &position, &count)) != NULL) {
        ...
    }

`top_error_t top_select(const top_t *t, size_t n, top_entry_t *entries, size_t *found)`

Fill _entries_, which has room for _n_, with the _n_ most frequent keys and their counts, best first, as `top_entry_compare()` orders them: the higher count first, and of equal counts, the key that `strcmp()` puts first. If there are fewer than _n_ different keys, all of them are given. Sets _*found_, if _found_ isn't **NULL**, to the number of entries filled, or **0** on an error. The keys point into the counter and are good until it is next written. The heap is allocated with the counter's allocator, with room for _n_ entries (or the number of keys, if that is less), and freed before it returns. Returns **TOP_OK**, **TOP_NO_MEM** if the heap couldn't be allocated, or **TOP_ERROR** if _entries_ is **NULL** and _n_ isn't **0**.

`int top_entry_compare(const void *a, const void *b, void *ctx)`

Compare two `top_entry_t`s in the order `top_select()` gives them, for `sof_sort` and the like: less than **0** if _a_ comes first, that is, has the higher count, or the same count and a key `strcmp()` puts first; greater than **0** if _b_ comes first; **0** if they have the same key and count. _ctx_ isn't used.

`top_error_t top_clear(top_t *t)`

Forget every key counted, and the words in progress, keeping the options. Returns **TOP_OK**.

`top_error_t top_free(top_t *t)`

Free everything the counter allocated. The `top_t` itself belongs to the caller and isn't freed; it is left empty. Returns **TOP_OK**.

Every function accepts a **NULL** _t_: `top_next()` returns **NULL**, `top_find()` returns **0**, and the others return **TOP_ERROR**. `top_next()` also returns **NULL** for a **NULL** _position_. A counter that was freed, or whose `top_init()` failed, is empty and stays that way: it finds nothing, `top_free()` returns **TOP_OK**, and `top_add()`, `top_add_line()`, `top_flush()`, `top_clear()` and `top_select()` return **TOP_ERROR**, until it is initialized again.

## Types

    typedef enum top_error_t {
        TOP_OK = 0,
        TOP_NO_MEM,
        TOP_NO_MATCH,
        TOP_ERROR,
        TOP_BAD_REGEX,      // top_init() was given a word pattern that doesn't compile
    } top_error_t;

**TOP_NO_MATCH** isn't returned by any function; it is there so the codes match those of the other libraries.

    typedef enum top_unit_t {
        TOP_WORDS = 0,      // a key is n words in a row, joined by single spaces
        TOP_LINES,          // a key is a whole line
    } top_unit_t;

    typedef struct top_options_t {
        top_unit_t unit;
        size_t n;           // words in a key; 0 means 1; 0 or 1 for lines
        const char *word;   // the PCRE2 pattern a word matches; NULL means TOP_DEFAULT_WORD
        int fold_case;      // non-zero folds ASCII letters to lowercase
    } top_options_t;

What `top_init()` is to count. A zeroed structure counts single words, keeping their case. The counter keeps what it needs, so the options, and the pattern, can go once `top_init()` returns.

    typedef struct top_entry_t {
        const char *key;
        size_t count;
    } top_entry_t;

A key and its count, as `top_select()` gives them. The key points into the counter, and is good until it is next written.

    typedef struct top_t {
        top_unit_t unit;
        size_t n;
        int fold_case;
        sof_hashmap_t map;          // the keys counted, each count in its element's data
        size_t total;               // keys counted, each time it was counted
        sof_regex_t regex;          // what a word is
        sof_string_t line;          // the line being split
        sof_string_t gram;          // the last n words, or fewer, joined by spaces
        size_t *lengths;            // the length of each, as a ring from oldest
        size_t words;
        size_t oldest;
        sof_alloc_t alloc;          // copy of the allocator; zeroed for the standard functions
    } top_t;

The fields can be read but should not be changed by the application. `map.count` is the number of different keys. The map's keys are copies the counter owns, and the data of each element is the key's count, held in the pointer itself.

## Threading

_top.c_ has no globals and does no locking, so separate counters can be used on separate threads freely. `top_find()`, `top_next()` and `top_select()` only read the counter, and take it as `const`, so any number of threads can call them at once as long as nothing writes to it. `top_select()` allocates its heap through the counter's allocator, so an allocator shared that way must be safe to call from several threads; the standard one is. Every other function writes, and needs the counter to itself: `top_add_line()` splits each line in the counter's own copy of it, with its own regular expression, whose match data PCRE2 writes. With a readers-writer lock, take the write lock for `top_add()`, `top_add_line()`, `top_flush()`, `top_clear()` and `top_free()`, and the read lock for the rest. The program uses a single thread.
