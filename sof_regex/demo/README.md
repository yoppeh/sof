# match

A small grep in c: the lines of text that match a regular expression.

`match` searches files or standard input for a PCRE2 regular expression, which is Perl's syntax, and prints the lines that match, or with `-o` only the matches, or with `-c` how many lines match. `-v` selects the lines that don't match instead, `-n` numbers the lines printed, `-i` ignores case, and `-A` and `-B` print lines of context after and before each selected line, as they do in grep. It exits with **0** if a line was selected, **1** if none was, and **2** on an error, as grep does.

_main.c_ is the program. It keeps the last lines that weren't printed, as many as `-B` asks for, in a [sof_ring](../../sof_ring/) ring, which holds a fixed number of them and drops the oldest as each new line comes; when a line is selected, the lines in the ring are printed before it. The searching is done by _match.c_, declared in _match.h_, which keeps a `match_t` that the program provides and sets up with `match_init()`, giving it the pattern, the options and a function to call with each line or match selected. `match_add()` searches a piece of text, so the input is read in pieces and a line can span two of them; With the `all_lines` option, the function is also called with each line that isn't selected, which is how the program sees the lines of context. `match_flush()` searches a last line that has no newline, at the end of each file. `match_clear()` forgets the line in progress and the counts, so the next file's lines are numbered from 1, and `match_free()` releases the searcher's memory. The searcher's `lines`, `selected` and `matches` fields count what it has read and found.

A line ends at a newline, which isn't part of it, or at the end of the text, and can hold any bytes, NULs included. Each line is gathered into a `sof_string_t` and searched on its own, so ^ and $ match at the start and end of each line, and a match never spans two lines. A carriage return before the newline is part of the line, as it is to grep, so on text with CRLF line endings, `$` matches only after it (`\r?$` matches either way). The pattern matches bytes, not UTF-8 characters, and `-i` folds only ASCII letters.

Lines are kept by the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), and searched by the [sof_regex](../../sof_regex/) library in _../../sof_regex/_ (_sof_regex.c_ and _sof_regex.h_), which compiles and runs the expression with [PCRE2](https://github.com/PCRE2Project/pcre2)'s 8-bit library, _libpcre2-8_, and has PCRE2 get all of its memory from the searcher's allocator, through a PCRE2 general context. Memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The lines of context are held by the [sof_ring](../../sof_ring/) library in _../../sof_ring/_ (_sof_ring.c_ and _sof_ring.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_), and writes its errors and messages with the [sof_log](../../sof_log/) library in _../../sof_log/_ (_sof_log.c_ and _sof_log.h_).

PCRE2 isn't part of this collection and must be installed, with its headers: it is the _pcre2_ package in Homebrew and Arch Linux, and _libpcre2-dev_ in Debian and Ubuntu. The Makefile finds it with _pcre2-config_ if that is installed, else with _pkg-config_, else links _-lpcre2-8_ from the compiler's own paths. Beyond PCRE2, it needs nothing but the c standard library.

    match_options_t options = {1, 0, 0, 0};          // ignore case; whole lines; selected lines only
    match_t m;
    match_init(&m, "^to be", &options, print, &ctx, NULL); // print(text, length, line, selected, ctx) for each line
    match_add(&m, buffer, length);                   // as many times as there is text
    match_flush(&m);                                 // searches a last line with no newline
    size_t found = m.selected;                       // lines selected
    match_free(&m);

## Building

    make            # builds ./match
    make debug=1    # builds with debug info and without optimization
    ./match -n -i 'to be,? or not to be' ../../test-data/shakespeare.txt

`make` in the library's directory, _../_, builds it with the library, as do `make sof_regex` and `make` from the root of the project.

To build it in another project, copy _main.c_, _match.c_ and _match.h_, with _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_regex.c_ and _sof_regex.h_ from _../../sof_regex/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, and _sof_log.c_ and _sof_log.h_ from _../../sof_log/_, and _sof_ring.c_ and _sof_ring.h_ from _../../sof_ring/_, into it, and link with _-lpcre2-8_. To use only the searcher, _match.c_ and _match.h_ need just the _sof_regex_, _sof_string_ and _sof_alloc_ files, and PCRE2.

## Usage

    match [-h] [-i] [-v] [-o] [-c] [-n] [-A n] [-B n] [-L level] pattern [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-i`: ignore case. The pattern is compiled with `(?i)` before it, which folds ASCII letters only. A pattern that starts with one of PCRE2's start-of-pattern items, such as `(*UTF)`, can't be used with `-i`; put `(?i)` after the item in the pattern instead.
- `-v`: select the lines that don't match.
- `-o`: print only the matches in each selected line, each on a line of its own. Empty matches aren't printed, and matches don't overlap. With `-v`, the lines selected have no matches, so nothing is printed, though `-c` still counts them.
- `-c`: print only how many lines are selected in each file, rather than the lines.
- `-n`: print each line's number, counting from 1 in each file, and a colon, before the line, or before each match with `-o`.
- `-A` _n_, or `--after-context` _n_: print the _n_ lines after each selected line as well. Lines of context are printed with a `-` after the file's name and line number, where selected lines have a `:`, and a line `--` parts groups of lines that don't follow on from each other, as in grep. A line of context that is itself selected is printed as selected, and starts the count again. Ignored with `-o` and `-c`.
- `-B` _n_, or `--before-context` _n_: print the _n_ lines before each selected line as well, unless they were printed already. Ignored with `-o` and `-c`.
- `-L` _level_: write log messages up to _level_: `error`, `warn` (the default), `info`, `debug`, `trace` or `all`, in any case. Errors, such as a file that can't be opened, are always written; at `info`, a line is written for each file with how many lines it has and how many were selected. Messages go to standard error, in `sof_log`'s format.
- _pattern_: the regular expression, in PCRE2's syntax, which is Perl's. Put `--` before a pattern that starts with `-`.
- _file_: the text to search. With no _file_, or `-`, it reads standard input. With more than one, each line printed, or count, starts with its file's name and a colon, and standard input is named _(standard input)_.

Short options can be combined, as in `-in` or `-nLinfo`, and can come before or after the pattern and the files; `--` ends the options, so everything after it is the pattern and files even if it starts with `-`. It exits with **0** if a line was selected in any file, **1** if none was, or **2** if the options are wrong, the pattern doesn't compile, a file couldn't be read or searched (memory ran out, or PCRE2 gave up on a line, as at its match limit), or the output couldn't be written; the other files are still searched.

For example, a famous line in the complete works of Shakespeare, from Project Gutenberg (see _../../test-data/README.md_), with its line number:

    $ ./match -n -i 'to be,? or not to be' ../../test-data/shakespeare.txt
    37284:To be, or not to be, that is the question:

the same, with two lines before it and one after:

    $ ./match -n -B 2 -A 1 -i 'to be,? or not to be' ../../test-data/shakespeare.txt
    37282-
    37283-HAMLET.
    37284:To be, or not to be, that is the question:
    37285-Whether ’tis nobler in the mind to suffer

the first few insults of a certain shape:

    $ ./match -o -i '\bthou art an? \w+ \w+' ../../test-data/shakespeare.txt | head -4
    thou art a witty fool
    thou art a knave and
    Thou art a soldier only
    thou art a gallant youth

how many words in the list of words have a q that isn't followed by a u, and the first of them:

    $ ./match -c 'q[^u]' ../../test-data/words.txt
    64
    $ ./match -n 'q[^u]' ../../test-data/words.txt | head -2
    28236:buqsha
    28237:buqshas

and an error, written by `sof_log`:

    $ ./match 'a(' ../../README.md; echo $?
    2026-11-05 04:09:19  match  79405  main.c:232  ERROR  the pattern "a(" isn't a valid regular expression
    2

## Functions

`match_error_t match_init(match_t *m, const char *pattern, const match_options_t *options, match_found_func_t found, void *ctx, const sof_alloc_t *alloc)`

Initialize the searcher _m_ the caller provides, to search for the NUL-terminated _pattern_, in PCRE2's syntax, as _options_ say (see Types), or with **NULL** _options_, selecting the lines that match, case and all. _found_ is called with each line, or match, selected, and _ctx_ is passed to every call of it; with **NULL** _found_ the searcher only counts. _alloc_ is the allocator for all of the searcher's memory, PCRE2's included, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into the searcher, so it can be on the stack. Returns **MATCH_OK**, **MATCH_BAD_REGEX** if _pattern_ doesn't compile, **MATCH_NO_MEM** if memory couldn't be allocated, or **MATCH_ERROR** if _m_ or _pattern_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves _m_ empty.

`match_error_t match_add(match_t *m, const char *text, size_t length)`

Search the _length_ bytes of _text_, which needn't be NUL-terminated; **NULL** _text_ searches nothing. Each line that a newline in _text_ ends is searched, and if it is selected, `lines`, `selected` and `matches` are counted and _found_ is called. The bytes after the last newline are kept as the line in progress, which the next call goes on with. Returns **MATCH_OK**, or **MATCH_NO_MEM** if memory ran out or **MATCH_MATCH_ERROR** if PCRE2 gave up on a line, as when it reaches its match limit; then the lines before were searched, the line that failed is lost, and the rest of _text_ isn't read, so the searcher should be cleared before it is used again.

`match_error_t match_flush(match_t *m)`

Search the line in progress, if the text so far didn't end with a newline, as `match_add()` searches a line; call it at the end of each file. The counts are kept. Returns **MATCH_OK**, or **MATCH_NO_MEM** or **MATCH_MATCH_ERROR** as `match_add()` does.

`match_error_t match_clear(match_t *m)`

Forget the line in progress and set `lines`, `selected` and `matches` to **0**, so the next line read is line 1. Returns **MATCH_OK**.

`match_error_t match_free(match_t *m)`

Free everything the searcher allocated. The `match_t` itself belongs to the caller and isn't freed; it is left empty. Returns **MATCH_OK**.

Every function accepts a **NULL** _m_ and returns **MATCH_ERROR**. A searcher that was freed, or whose `match_init()` failed, is empty and stays that way: `match_add()`, `match_flush()` and `match_clear()` return **MATCH_ERROR**, and `match_free()` returns **MATCH_OK**, until it is initialized again.

## Types

    typedef enum match_error_t {
        MATCH_OK = 0,
        MATCH_NO_MEM,
        MATCH_NO_MATCH,     // not returned: a line that doesn't match isn't an error
        MATCH_ERROR,
        MATCH_BAD_REGEX,    // match_init() was given a pattern that doesn't compile
        MATCH_MATCH_ERROR,  // PCRE2 gave up on a line, as at its match limit
    } match_error_t;

    typedef struct match_options_t {
        int ignore_case;    // non-zero puts (?i) before the pattern
        int invert;         // non-zero selects the lines that don't match
        int only_matching;  // non-zero calls found with each non-empty match
        int all_lines;      // non-zero also calls found with each line not selected
    } match_options_t;

What `match_init()` is to search for. A zeroed structure selects the lines that match, case and all. The searcher keeps a copy, so the options can go once `match_init()` returns. With _only_matching_ and _invert_ both set, the lines selected have no matches, so _found_ is never called, though the lines are counted. With _all_lines_, _found_ is also called with each line that isn't selected, whole and with _selected_ **0**; these lines aren't counted in `selected`.

    typedef struct match_t {
        sof_regex_t regex;          // the compiled pattern
        sof_string_t line;          // the line in progress
        match_options_t options;
        match_found_func_t found;
        void *ctx;
        size_t lines;               // lines read
        size_t selected;            // lines selected
        size_t matches;             // matches found with only_matching
        sof_alloc_t alloc;          // copy of the allocator; zeroed for the standard functions
    } match_t;

The fields can be read but should not be changed by the application. The counts are of what was read since `match_init()` or `match_clear()`. `line.data` is **NULL** for a searcher that was freed or whose `match_init()` failed.

    typedef void (*match_found_func_t)(const char *text, size_t length, size_t line, int selected, void *ctx);

Called with each line selected, or with _only_matching_, each non-empty match in a selected line, and with _all_lines_, each line that isn't selected, in order, with _selected_ **1** for a line or match selected and **0** for a line that isn't, with the _ctx_ given to `match_init()`. _text_ holds _length_ bytes, which aren't NUL-terminated and can hold NULs, and is good only until the function returns; a whole line comes without its newline. _line_ is the line's number, counting from 1. It must not call `match_add()`, `match_flush()`, `match_clear()` or `match_free()`.

## Threading

_match.c_ has no globals and does no locking, so separate searchers can be used on separate threads freely. Every function writes the searcher, since each search writes PCRE2's match data in it, so a searcher shared between threads needs a lock held for every call, and with a readers-writer lock, always the write lock. Two threads searching the same pattern need a searcher each.

The program uses one thread.
