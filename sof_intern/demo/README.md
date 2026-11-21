# index

A concordance maker in c: each word of a text, in order, with the numbers of the lines it is on.

`index` reads files or standard input and prints each word once, in byte order, with the number of times it occurs and the lines it is on, each line once. A word is a run of letters, with an apostrophe (_'_ or _’_) allowed between two of them, so _don't_ and _o’er_ are words and _'tis_ is _tis_. Options set the shortest word indexed and whether case is ignored.

_main.c_ is the program. The indexing is done by _index.c_, declared in _index.h_, which keeps an `index_t` that the program provides and sets up with `index_init()`. `index_add()` indexes a piece of text, so the input is read in pieces and a line can span two of them; `index_flush()` ends the last line, so a text whose last line has no newline loses nothing. `index_first()` and `index_next()` loop over the words in order, `index_traverse()` does the same with a callback, `index_find()` finds one word, and `index_entry_line()` gives the lines a word is on. `index_clear()` forgets every word, and `index_free()` releases the index's memory.

Text is gathered into a line until a newline ends it, and the words of the line are found by scanning it for letters. Each word is first interned in a string pool, which keeps one copy of every distinct word, packed into large blocks, and gives the same pointer each time the word is found. A word new to the pool gets an entry in a red-black tree, holding a view of the pool's copy, the number of times it was found, and a vector of the lines it is on, which grows as the word is found on new lines. A word already in the pool is looked up in the tree, whose compare function takes two keys with the same pointer as equal without comparing their bytes, so a word already in the index costs no allocation, and a word found again on the same line adds to its count but not to its lines. An index that folds case keeps its words in lowercase and compares them with `sof_string_casecmp()`, which puts lowercase words in the same order as `sof_string_cmp()` and lets `index_find()` look up a word in any case without making a folded copy.

The words are stored once each by the [sof_intern](../../sof_intern/) library in _../../sof_intern/_ (_sof_intern.c_ and _sof_intern.h_), which uses the [sof_hashmap](../../sof_hashmap/) library in _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_) and the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_, _sof_hash.h_ and _rapidhash.h_, under its own MIT license). Their entries are kept in the [sof_rbtree](../../sof_rbtree/) library in _../../sof_rbtree/_ (_sof_rbtree.c_ and _sof_rbtree.h_), and each word's lines in a vector from the [sof_vector](../../sof_vector/) library in _../../sof_vector/_ (_sof_vector.c_ and _sof_vector.h_), which uses the [sof_sort](../../sof_sort/) library in _../../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_). Lines and words are held in strings from the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_). Word lengths are counted in characters by the [sof_utf](../../sof_utf/) library in _../../sof_utf/_ (_sof_utf.h_, which is all of it). Memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    index_options_t options = {3, 1};                 // words of 3 characters or more, ignoring case
    index_t ix;
    index_init(&ix, &options, NULL);                  // no allocator: the standard functions
    index_add(&ix, buffer, length);                   // as many times as there is text
    index_flush(&ix);                                 // ends the last line
    const index_entry_t *e = index_find(&ix, "The");  // the word "the", as -i folds it
    size_t first = index_entry_line(e, 0);            // the first line it is on
    index_free(&ix);

## Building

    make            # builds ./index
    make debug=1    # builds with debug info and without optimization
    ./index -i -l 5 ../../test-data/shakespeare.txt     # every word of 5 letters or more

`make` in the library's directory, _../_, builds it with the library, as do `make sof_intern` and `make` from the root of the project.

To build it in another project, copy _main.c_, _index.c_ and _index.h_, with _sof_intern.c_ and _sof_intern.h_ from _../../sof_intern/_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_rbtree.c_ and _sof_rbtree.h_ from _../../sof_rbtree/_, _sof_vector.c_ and _sof_vector.h_ from _../../sof_vector/_, _sof_sort.c_ and _sof_sort.h_ from _../../sof_sort/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_utf.h_ from _../../sof_utf/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it. To use only the index, _index.c_ and _index.h_ need all of those but the _sof_copt_ files.

## Usage

    index [-h] [-i] [-l length] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-i`: ignore case: ASCII letters are folded to lowercase, so _The_ and _the_ are the same word, printed as _the_. Letters outside ASCII aren't folded, so _Été_ and _été_ are two words.
- `-l` _length_: index only words of at least _length_ characters, an apostrophe included (default 1).
- _file_: the text to index. With no _file_, or `-`, it reads standard input. With several, each is indexed on its own and printed under a line such as `==> name <==`, with its lines numbered from 1.

Each word is printed on a line of its own, with the number of times it occurs in parentheses and then the lines it is on:

    copyright (3): 3, 12, 18

It exits with **0**, **1** if a file couldn't be read or memory ran out, or **2** if the options are wrong. A file that can't be read is reported and skipped, and the others are still indexed.

Text is read as UTF-8. A letter is an ASCII letter or one of the letters of Latin-1 Supplement and Latin Extended-A and -B, from U+00C0 to U+024F, but for the multiplication and division signs, × and ÷. Anything else, digits included, separates words, and lines end at each newline, so text with CRLF line endings needs nothing special. Words are sorted by their bytes, which puts uppercase before lowercase and, for UTF-8, sorts by code point.

For example, the words of eight letters or more in this program's license, ignoring case:

    $ ./index -i -l 8 LICENSE
    associated (1): 6
    conditions (1): 10
    connection (1): 20
    contract (1): 19
    copyright (3): 3, 12, 18
    dealings (1): 20
    distribute (1): 8
    documentation (1): 6
    following (1): 10
    furnished (1): 10
    included (1): 12
    including (2): 7, 16
    liability (1): 19
    limitation (1): 7
    merchantability (1): 16
    noninfringement (1): 17
    obtaining (1): 5
    otherwise (1): 19
    particular (1): 17
    permission (2): 5, 12
    portions (1): 13
    provided (1): 15
    restriction (1): 7
    software (9): 6, 7, 9, 13, 15, 20, 21
    sublicense (1): 8
    substantial (1): 13
    warranties (1): 16
    warranty (1): 15

and some of the longest words in the complete works of Shakespeare, from Project Gutenberg (see _../../test-data/README.md_), in which the apostrophe is ’, U+2019:

    $ ./index -i -l 15 ../../test-data/shakespeare.txt
    anthropophaginian (1): 113242
    caernarfonshire (1): 72993
    ...
    gloucestershire (15): 41986, 45965, 45975, 45977, 48376, 49585, 49635, 50215, 50562, 112150, 113941, 132585, 134267, 134276, 136812
    honorificabilitudinitatibus (1): 94029
    ...
    notwithstanding (23): 29310, 49699, 55845, 63719, 67114, 74112, 82014, 105601, 107539, 109103, 110234, 110245, 110247, 110309, 116275, 123843, 133966, 137783, 154782, 158867, 163465, 172177, 179643
    ...
    th’uncomprehensive (1): 169273
    th’understanding (1): 36250
    ...

All of it, 27,122 words with `-i`, is indexed in about 0.4 seconds on an arm64 Mac.

## Functions

`index_error_t index_init(index_t *ix, const index_options_t *options, const sof_alloc_t *alloc)`

Initialize the index _ix_ the caller provides, to index what _options_ say (see Types), or with **NULL** _options_, every word, keeping its case. _alloc_ is the allocator for all of the index's memory, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into the index, so it can be on the stack. The index's tree points into _ix_, so _ix_ must not be moved or copied while it holds words. Returns **INDEX_OK**, **INDEX_NO_MEM** if memory couldn't be allocated, or **INDEX_ERROR** if _ix_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves _ix_ empty.

`index_error_t index_add(index_t *ix, const char *text, size_t length)`

Index the _length_ bytes of _text_, which is UTF-8 and needn't be NUL-terminated; **NULL** _text_ indexes nothing. The text is cut into lines at each newline, and the words of a line are indexed when it ends, so the bytes after the last newline are kept until the next call, or `index_flush()`, ends their line. Lines are numbered from 1, and `ix->lines` is the number ended so far. Returns **INDEX_OK**, **INDEX_NO_MEM** if memory ran out, in which case the words before the one that couldn't be stored are indexed and the rest of _text_ is not read, or **INDEX_ERROR** if _ix_ is **NULL** or not initialized.

`index_error_t index_flush(index_t *ix)`

End the line in progress, if any bytes of one were given to `index_add()`, and index its words, so the next text starts a new line. Call it at the end of the text. Returns **INDEX_OK**, **INDEX_NO_MEM** or **INDEX_ERROR**, as `index_add()` does.

`const index_entry_t *index_find(const index_t *ix, const char *word)`

Return the entry of the NUL-terminated _word_, or **NULL** if it isn't in the index. If `fold_case` is set, the ASCII letters of _word_ match in either case, as `sof_string_casecmp()` compares them, so _The_ finds _the_. Nothing is allocated. The entry is good until the index is cleared or freed.

`const index_entry_t *index_first(const index_t *ix)`

Return the entry of the first word in byte order, or **NULL** if the index is empty.

`const index_entry_t *index_next(const index_t *ix, const index_entry_t *entry)`

Return the entry of the word after _entry_ in byte order, or **NULL** if _entry_ is the last or **NULL**. With `index_first()`, it loops over the words without a callback; the loop must not call `index_add()`, `index_flush()` or `index_clear()`.

    for (const index_entry_t *e = index_first(&ix); e != NULL; e = index_next(&ix, e)) {
        ...
    }

`size_t index_entry_line(const index_entry_t *entry, size_t i)`

Return the number of the _i_th line, from **0**, that _entry_'s word is on, or **0** if _i_ isn't less than `entry->lines.count` or _entry_ is **NULL**. The lines are in ascending order, each once.

`int index_traverse(const index_t *ix, index_traverse_func_t cb, void *ctx)`

Call _cb_ with each word's entry and _ctx_, in byte order. If _cb_ returns a non-zero value, the traversal stops and returns that value. Otherwise returns **0** once every word has been visited (at once, if _cb_ is **NULL**).

`index_error_t index_clear(index_t *ix)`

Forget every word and line, and the line in progress, keeping the options, so the next text is numbered from line 1. Returns **INDEX_OK**.

`index_error_t index_free(index_t *ix)`

Free everything the index allocated. The `index_t` itself belongs to the caller and isn't freed; it is left empty. Returns **INDEX_OK**.

Every function accepts a **NULL** _ix_: `index_find()`, `index_first()` and `index_next()` return **NULL**, `index_traverse()` returns **-1**, and the others return **INDEX_ERROR**. `index_find()` returns **NULL** for a **NULL** _word_. An index that was freed, or whose `index_init()` failed, is empty and stays that way: it finds nothing, and `index_add()`, `index_flush()` and `index_clear()` return **INDEX_ERROR**, until it is initialized again.

## Types

    typedef enum index_error_t {
        INDEX_OK = 0,
        INDEX_NO_MEM,
        INDEX_NO_MATCH,
        INDEX_ERROR,
    } index_error_t;

    typedef struct index_options_t {
        size_t min_length;  // fewest characters a word indexed has; 0 means 1
        int fold_case;      // non-zero folds ASCII letters to lowercase
    } index_options_t;

What `index_init()` is to index. A zeroed structure indexes every word, keeping its case. The index keeps what it needs, so the options can go once `index_init()` returns.

    typedef struct index_t {
        sof_intern_t pool;          // one copy of each word
        sof_rbtree_t words;         // the words, from &entry->word to the entry
        size_t min_length;
        int fold_case;
        size_t lines;               // lines ended so far
        size_t total;               // words indexed, counting each time
        sof_string_t line;          // the line in progress
        sof_alloc_t alloc;          // copy of the allocator; zeroed for the standard functions
    } index_t;

The fields can be read but should not be changed by the application. `words.count` is the number of different words.

    typedef struct index_entry_t {
        sof_string_t word;          // a view of the word's copy in the pool, NUL-terminated
        sof_vector_t lines;         // the lines it is on, as size_t, ascending, each once
        size_t count;               // the times it was found
        sof_rbtree_node_t *node;    // its node in the tree
    } index_entry_t;

A word in the index, from `index_find()`, `index_first()`, `index_next()` or `index_traverse()`. `sof_string_cstr(&entry->word)` gives the word as a c string, and `index_entry_line()` its lines. The fields can be read but must not be changed. An entry doesn't move until the index is cleared or freed.

    typedef int (*index_traverse_func_t)(const index_entry_t *entry, void *ctx)

Called for each word visited by `index_traverse()`, with the _ctx_ given to it. Return **0** to continue, or a non-zero value to stop the traversal; that value is returned by `index_traverse()`. It must not call `index_add()`, `index_flush()`, `index_clear()` or `index_free()`.

## Threading

_index.c_ has no globals and does no locking, so separate indexes can be used on separate threads freely. `index_entry_line()`, `index_find()`, `index_first()`, `index_next()` and `index_traverse()` only read the index, so any number of threads can call them at once as long as nothing writes to it; every other function writes, and needs the index to itself. None of the functions that read allocates. With a readers-writer lock, take the write lock for `index_add()`, `index_flush()`, `index_clear()` and `index_free()`, and the read lock for the rest. The program uses one index on one thread.
