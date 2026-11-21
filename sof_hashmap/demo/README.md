# ngram

An n-gram finder in c: the most frequent runs of characters or words in text, and how often any given one occurs.

`ngram` counts every n-gram, for a range of lengths n, in files or standard input. A gram is n characters in a row, or with `-w`, n words in a row. Which characters make up grams is a set given with `-c`: any other character breaks a run of characters, or separates words. Grams overlap, so _aaa_ holds the 2-gram _aa_ twice. For each length it prints how many grams there are, how many of them are different, and the most frequent, then the counts of any grams asked for with `-f`.

_main.c_ is the program. The counting is done by _ngram.c_, declared in _ngram.h_, which keeps an `ngram_t` that the program provides and sets up with `ngram_init()`. `ngram_add()` counts a piece of text, so the input is read in pieces and a gram can span two of them; `ngram_flush()` ends the text, so no gram spans two files. `ngram_distinct()` and `ngram_total()` give the number of grams of a length, `ngram_next()` and `ngram_traverse()` loop over them, and `ngram_find()` gives the count of one. `ngram_free()` releases the counter's memory and `ngram_clear()` forgets the counts. `ngram_context()` reads the text before a piece without counting it, and `ngram_merge()` adds one counter's counts to another's, so the pieces of a text can be counted by separate counters on separate threads and the counts put together.

With `-j`, the program counts on several threads. The main thread reads the text in pieces of 256 KiB, cuts each just after an ASCII character that isn't in the set (an ASCII byte is always a whole character in UTF-8), and hands the chunks to worker threads through a [sof_queue](../../sof_queue/) under a mutex, with condition variables so workers wait for work and the reader waits when the workers are behind. Each worker counts its chunks in a counter of its own. For word grams, each chunk carries the last _max_ - 1 words before it as context, given to `ngram_context()`, so the grams that span a cut are counted once, by the worker counting the chunk they end in; character grams don't span a cut, since it comes after a character not in the set. Once the text is read, the workers' counters are merged into one. The counts are sums, so the output is the same whatever the number of threads.

Grams are counted in a hashmap per length, from the [sof_hashmap](../../sof_hashmap/) library in _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), whose keys are hashed by the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_ and _sof_hash.h_, which use [rapidhash](https://github.com/Nicoshev/rapidhash) in _rapidhash.h_, under its own MIT license). Text is decoded from UTF-8 by the [sof_utf](../../sof_utf/) library in _../../sof_utf/_ (_sof_utf.h_, which is all of it), and the ranges of the set above ASCII, and the grams the program prints, are sorted by the [sof_sort](../../sof_sort/) library in _../../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_). Memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_), and hands chunks of text to its threads through the [sof_queue](../../sof_queue/) library in _../../sof_queue/_ (_sof_queue.c_ and _sof_queue.h_). The program uses POSIX threads, and is compiled and linked with _-pthread_; _ngram.c_ itself doesn't use threads. Beyond those, it needs nothing but the c standard library.

    ngram_options_t options = {NGRAM_WORDS, 2, 3, "a-z'", 1}; // 2- and 3-grams of words, ignoring case
    ngram_t ng;
    ngram_init(&ng, &options, NULL);                           // no allocator: the standard functions
    ngram_add(&ng, buffer, length);                            // as many times as there is text
    ngram_flush(&ng);                                          // counts the last word
    size_t count = ngram_find(&ng, "of the");                  // how often "of the" occurs
    ngram_free(&ng);

## Building

    make            # builds ./ngram
    make debug=1    # builds with debug info and without optimization
    ./ngram -w -i ../../test-data/shakespeare.txt         # the commonest words and phrases
    ./ngram -w -i -j 4 ../../test-data/shakespeare.txt    # the same, counted on 4 threads

`make` in the library's directory, _../_, builds it with the library, as do `make sof_hashmap` and `make` from the root of the project.

To build it in another project, copy _main.c_, _ngram.c_ and _ngram.h_, with _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, _sof_utf.h_ from _../../sof_utf/_, _sof_sort.c_ and _sof_sort.h_ from _../../sof_sort/_, _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, and _sof_queue.c_ and _sof_queue.h_ from _../../sof_queue/_, into it, and compile and link with _-pthread_. To use only the counter, _ngram.c_ and _ngram.h_ need just the _sof_hashmap_, _sof_hash_, _sof_alloc_, _sof_utf_ and _sof_sort_ files, and no threads.

## Usage

    ngram [-h] [-w] [-i] [-n length] [-c chars] [-t top] [-j threads] [-f gram]... [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-w`: count grams of words instead of characters. A word is a run of characters in the set, and the words of a gram are printed, and looked up, with a single space between them.
- `-i`: ignore case: ASCII letters are folded to lowercase, so _The_ and _the_ are the same gram. Both cases of every ASCII letter in the set are then in it. Letters outside ASCII aren't folded: put both cases in the set to count them, as separate characters.
- `-n` _length_: the length of the grams counted, in characters or words: a single length, such as `3`, or a range, such as `1-4` (default `1-3`).
- `-c` _chars_: the set of characters grams are made of (default `A-Za-z0-9`), in UTF-8. Each character stands for itself, and a `-` between two characters for every code point from the first to the second, so `a-z0-9'` is the lowercase letters, the digits and the apostrophe, and `a-zà-ÿ` adds the accented Latin-1 letters. A `-` first or last stands for itself.
- `-t` _top_: print the _top_ most frequent grams of each length (default 10; 0 prints all of them). Grams found equally often are printed in byte order, which for UTF-8 is code point order.
- `-j` _threads_: count on _threads_ worker threads, from 1 to 256 (default 1), as described above. The output is the same for any number.
- `-f` _gram_: print how often _gram_ occurs. Its length must be in the range given with `-n`. It is read as the text is, so with `-i` its case doesn't matter, and with `-w` it is split into words wherever a character isn't in the set. Can be given more than once.
- _file_: the text to count. With no _file_, or `-`, it reads standard input. With several, each is counted in turn, and no gram spans two of them.

It exits with **0**, **1** if a file couldn't be read or memory ran out, or **2** if the options are wrong.

Text is read as UTF-8, and a character is a code point, however many bytes it takes. Bytes that aren't valid UTF-8 are read as U+FFFD, the replacement character, one for each error as the Unicode standard counts them, so they break runs and separate words unless U+FFFD (`�`) is in the set. A character cut off by the end of a file is one error.

For example, the most frequent pairs of words in some text, ignoring case:

    $ ./ngram -w -i -n 2 -t 3 -c "a-z'" -f "THE  tests" ../../README.md
    words: 926

    2-grams: 805 distinct, 925 in all
             5  sof hashmap
             5  its own
             5  of any

    lookups:
             3  THE  tests

the commonest three-word phrases in the complete works of Shakespeare, from Project Gutenberg (see _../../test-data/README.md_), in which the apostrophe is ’, U+2019:

    $ ./ngram -w -i -n 3 -t 5 -c "a-z'’" ../../test-data/shakespeare.txt
    words: 971514

    3-grams: 802297 distinct, 971512 in all
           312  a room in
           252  i pray you
           229  i will not
           210  room in the
           171  i know not

and its commonest runs of four letters:

    $ ./ngram -n 4 -t 5 -i -c a-z ../../test-data/shakespeare.txt
    characters: 4072366

    4-grams: 17402 distinct, 1384042 in all
         12292  that
          9871  ther
          9848  with
          8311  thou
          8059  here

## Functions

`ngram_error_t ngram_init(ngram_t *ng, const ngram_options_t *options, const sof_alloc_t *alloc)`

Initialize the counter _ng_ the caller provides, to count what _options_ say (see Types), or with **NULL** _options_, single characters made of letters and digits. _alloc_ is the allocator for all of the counter's memory, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into the counter, so it can be on the stack. Returns **NGRAM_OK**, **NGRAM_NO_MEM** if memory couldn't be allocated, or **NGRAM_ERROR** if _ng_ is **NULL**, _alloc_ has a _malloc_ but no _free_ or _free_sized_, _max_ is less than _min_, _unit_ isn't an `ngram_unit_t`, or the set of characters is empty or isn't valid UTF-8; an error leaves _ng_ empty.

`ngram_error_t ngram_add(ngram_t *ng, const char *text, size_t length)`

Count the grams in the _length_ bytes of _text_, which is UTF-8 and needn't be NUL-terminated; **NULL** _text_ counts nothing. Grams go on from one call to the next, so a run of characters or a word at the end of _text_ can be continued by the next call, and so can a character whose bytes the end of _text_ cuts off. A word isn't counted until the character after it, or `ngram_flush()`, ends it. Returns **NGRAM_OK**, or **NGRAM_NO_MEM** if memory ran out, in which case the grams before the one that couldn't be stored are counted.

`ngram_error_t ngram_flush(ngram_t *ng)`

Count a word still in progress and start over, so that no gram spans the text given before the call and after it. The bytes of a character cut off at the end of the text are read as one U+FFFD. Call it at the end of each file. Returns **NGRAM_OK** or **NGRAM_NO_MEM**.

`ngram_error_t ngram_context(ngram_t *ng, const char *text, size_t length)`

Read the _length_ bytes of _text_ as `ngram_add()` does, but count nothing in them: no gram, character or word. It only sets up the grams in progress, so that grams that begin in _text_ and end in the text given next to `ngram_add()` are counted. This is for counting a text in pieces with separate counters: each piece is given the end of the text before it as context, then counted and flushed. Cut the text between characters, such as just after an ASCII byte, which is always a whole character, and best just after a character that isn't in the set; for character grams no context is then needed, and for word grams the last _max_ - 1 words before the cut are enough. A word still in progress at the end of _text_ is counted, with its grams, when `ngram_add()` or `ngram_flush()` ends it. **NULL** _text_ reads nothing. Returns **NGRAM_OK**, or **NGRAM_NO_MEM** if memory ran out.

`size_t ngram_find(const ngram_t *ng, const char *gram)`

Return how many times the NUL-terminated _gram_ was found. The _gram_ is read as `ngram_add()` reads text: as UTF-8, its letters folded if `fold_case` is set, and for words split into words wherever a character isn't in the set, so `"of  the"` finds the 2-gram _of the_. Its length is in characters or words. Returns **0** if it wasn't found, isn't a single gram (a character not in the set in a character gram, or no words in a word gram), is of a length outside the counter's range, or is longer than 255 bytes as it is stored and memory couldn't be allocated to look it up.

`size_t ngram_distinct(const ngram_t *ng, size_t n)`

Return the number of different grams of length _n_ found, or **0** if _n_ is outside the counter's range.

`size_t ngram_total(const ngram_t *ng, size_t n)`

Return the number of grams of length _n_ found, counting each occurrence, or **0** if _n_ is outside the counter's range.

`int ngram_traverse(const ngram_t *ng, size_t n, ngram_traverse_func_t cb, void *ctx)`

Call _cb_ with each gram of length _n_, its count and _ctx_, in no particular order. If _cb_ returns a non-zero value, the traversal stops and returns that value. Otherwise returns **0** once every gram has been visited (at once, if _cb_ is **NULL**), or **-1** if _n_ is outside the counter's range.

`const char *ngram_next(const ngram_t *ng, size_t n, size_t *position, size_t *count)`

Return the next gram of length _n_, setting _*count_ to its count if _count_ isn't **NULL**, or return **NULL** when there are no more or _n_ is outside the counter's range. Set _*position_ to **0** to start; each call updates it. The grams come in no particular order. The gram returned is good until the counter is next written, and the loop must not call `ngram_add()`, `ngram_flush()` or `ngram_clear()`.

    size_t position = 0, count;
    const char *gram;
    while ((gram = ngram_next(&ng, 2, &position, &count)) != NULL) {
        ...
    }

`ngram_error_t ngram_merge(ngram_t *ng, const ngram_t *from)`

Add the counts of _from_ to those of _ng_, as if _ng_ had counted _from_'s text too: each gram of _from_ is counted in _ng_ as many times as it was found in _from_, and the totals and the number of characters or words are added. The grams in progress in _ng_ are kept, and _from_ is only read. The two counters must have been initialized with the same options, though their allocators can differ. Returns **NGRAM_OK**, **NGRAM_NO_MEM** if memory ran out, which leaves _ng_ with some of _from_'s counts and not others, so it should be cleared or freed, or **NGRAM_ERROR** if _from_ is **NULL** or not initialized, is _ng_ itself, or has different options.

`uint32_t ngram_member(const ngram_t *ng, uint32_t code)`

Return the code point _code_ as the counter counts it, folded to lowercase if it is an ASCII letter and `fold_case` is set, or **0** if it isn't in the set. Anything that isn't a code point, such as the **SOF_UTF_INVALID** `sof_utf8_decode()` gives for bytes that aren't UTF-8, is taken as U+FFFD, as `ngram_add()` takes it. This is how `ngram_add()` reads each character, so a program cutting text for separate counters can find where words end.

`ngram_error_t ngram_clear(ngram_t *ng)`

Forget every gram counted, and any word in progress, keeping the options. Returns **NGRAM_OK**.

`ngram_error_t ngram_free(ngram_t *ng)`

Free everything the counter allocated. The `ngram_t` itself belongs to the caller and isn't freed; it is left empty. Returns **NGRAM_OK**.

Every function accepts a **NULL** _ng_: `ngram_next()` returns **NULL**, `ngram_traverse()` returns **-1**, `ngram_find()`, `ngram_distinct()`, `ngram_member()` and `ngram_total()` return **0**, and the others return **NGRAM_ERROR**. `ngram_find()` returns **0** for a **NULL** _gram_, and `ngram_next()` returns **NULL** for a **NULL** _position_. A counter that was freed, or whose `ngram_init()` failed, is empty and stays that way: it finds nothing, and `ngram_add()`, `ngram_context()`, `ngram_flush()`, `ngram_clear()` and `ngram_merge()`, as either counter, return **NGRAM_ERROR**, until it is initialized again.

## Types

    typedef enum ngram_error_t {
        NGRAM_OK = 0,
        NGRAM_NO_MEM,
        NGRAM_NO_MATCH,
        NGRAM_ERROR,
    } ngram_error_t;

    typedef enum ngram_unit_t {
        NGRAM_CHARS = 0,    // a gram is n characters in a row
        NGRAM_WORDS,        // a gram is n words in a row, joined by single spaces
    } ngram_unit_t;

    typedef struct ngram_options_t {
        ngram_unit_t unit;
        size_t min;         // shortest gram; 0 means 1
        size_t max;         // longest gram; 0 means min
        const char *chars;  // the set, as for -c; NULL means "A-Za-z0-9"
        int fold_case;      // non-zero folds ASCII letters to lowercase
    } ngram_options_t;

What `ngram_init()` is to count. A zeroed structure counts single characters made of letters and digits. The counter keeps what it needs, so the options, and the string _chars_, can go once `ngram_init()` returns.

    typedef struct ngram_t {
        ngram_unit_t unit;
        size_t min, max;
        int fold_case;
        unsigned char map[128]; // each ASCII character as counted, or 0 if not in the set
        ngram_range_t *ranges;  // the code points above ASCII in the set, sorted
        size_t range_count;
        sof_hashmap_t *maps;    // a map per length, from min to max
        size_t *totals;         // grams counted per length, from min to max
        size_t units;           // characters or words counted
        ...                     // the grams in progress
        sof_alloc_t alloc;           // copy of the allocator; zeroed for the standard functions
    } ngram_t;

The fields can be read but should not be changed by the application. The keys of each map are the grams, in UTF-8, and the data of each element is the gram's count, held in the pointer itself. The ranges don't overlap or touch, so two counters with the same set have the same ranges however the set was written.

    typedef struct ngram_range_t {
        uint32_t first;     // the first code point of the range
        uint32_t last;      // and the last
    } ngram_range_t;

A range of code points in a counter's set.

    typedef int (*ngram_traverse_func_t)(const char *gram, size_t count, void *ctx)

Called for each gram visited by `ngram_traverse()`, with the _ctx_ given to it. Return **0** to continue, or a non-zero value to stop the traversal; that value is returned by `ngram_traverse()`. It must not call `ngram_add()`, `ngram_flush()`, `ngram_clear()` or `ngram_free()`.

## Threading

_ngram.c_ has no globals and does no locking, so separate counters can be used on separate threads freely. `ngram_distinct()`, `ngram_find()`, `ngram_member()`, `ngram_next()`, `ngram_total()` and `ngram_traverse()` only read the counter, as `ngram_merge()` only reads _from_, so any number of threads can call them at once as long as nothing writes to it; every other function writes, and needs the counter to itself. `ngram_find()` allocates, through the counter's allocator, to look up a gram longer than 255 bytes as it is stored, so an allocator shared that way must be safe to call from several threads. With a readers-writer lock, take the write lock for `ngram_add()`, `ngram_context()`, `ngram_flush()`, `ngram_clear()`, `ngram_free()` and merging into the counter, and the read lock for the rest.

The program, with `-j`, gives each worker thread a counter of its own, so no counter is shared while it is written: the workers share only the queue of chunks, under a mutex, and the main thread merges their counters after joining them. The counters use the standard allocator, which is safe on any thread.
