# sof

A collection of small c libraries: data structures, hashing, parsing and other everyday tools.

The collection can be used in two ways. As a reference, each library is in its own directory, with its documentation, its tests, which its Makefile builds and runs, and a demo program in its _demo/_ directory: to use one in another project, copy its source files, and those of any library it uses, into that project and compile them with the project's own sources; the library's README says which files to copy. As a whole, `make lib` builds every library into one static library, _build/libsof.a_, and `make install` installs it with the headers (see Building).

Libraries build on each other where that helps (_sof_hashmap_ uses _sof_hash_), and those that allocate memory take an optional allocator from _sof_alloc_ in their `init` function, so an application can give them its own memory functions. Beyond that and the c standard library they need nothing, unless their README says otherwise. Every library comes with tests, and some with a benchmark. The tests share a small harness in [test](test/), and large test data is in _test-data/_.

## Libraries

| library | |
|---|---|
| [sof_alloc](sof_alloc/) | Replaceable memory functions, with sized frees, for the libraries that allocate. |
| [sof_base](sof_base/) | Base16, base32 and base64 encoding and decoding, RFC 4648's, strict or lenient, at once or in pieces. |
| [sof_bit](sof_bit/) | Bit operations on a machine word: set, clear, flip, test, count and find the next set bit. |
| [sof_bloom](sof_bloom/) | A Bloom filter: set membership in a few bits per key, with false positives at a rate chosen up front, classic or blocked (each key's bits in one 32-byte block, faster for filters larger than the cache), with a benchmark. |
| [sof_config](sof_config/) | A parser for .ini style configuration files, with lookup and typed values by section and key. |
| [sof_copt](sof_copt/) | Command line options defined in any module and merged into one table at link time. |
| [sof_csv](sof_csv/) | A streaming CSV reader and writer, a record at a time, following RFC 4180, with headers, columns by name and a strict mode. |
| [sof_debug](sof_debug/) | Debug tracing to stderr, indented by call depth, that compiles away unless DEBUG is defined. |
| [sof_digest](sof_digest/) | Message digests and checksums: SHA-256, SHA-512 and the rest of SHA-2, SHA-1 and MD5 for compatibility, and the CRC-32 and CRC-32C, of memory or of data in pieces, and hex conversion. |
| [sof_hash](sof_hash/) | General-purpose hashing: bytes, strings and 64-bit values, using rapidhash, with streams that hash data in pieces, with a benchmark. |
| [sof_hashmap](sof_hashmap/) | A hashmap with keys of any type, using open addressing. |
| [sof_heap](sof_heap/) | A binary-heap priority queue of fixed-size values, with replace-top and removal from anywhere. |
| [sof_intern](sof_intern/) | A string pool that keeps one copy of each distinct string, so strings can be compared by pointer. |
| [sof_json](sof_json/) | JSON and JSON Lines: a streaming writer that adds the punctuation, escapes and indentation and refuses misplaced pieces; a streaming reader that checks RFC 8259's grammar and returns a token at a time; and a document tree with indexed objects and JSON Pointer lookup. |
| [sof_log](sof_log/) | Leveled logging to a stream, written directly or, built with SOF_LOG_THREADED, by a thread of its own. |
| [sof_lru](sof_lru/) | A least-recently-used cache of a fixed number of entries, with keys of any type, that calls a delete function for each entry it evicts. |
| [sof_mem](sof_mem/) | Memory allocators for different purposes, which stack: an arena with marks and a fixed-buffer form, a pool, size classes, and a tracker, with a benchmark. |
| [sof_stack](sof_stack/) | A last-in, first-out stack of fixed-size values, in one growing array, built as libsof_stack.a. |
| [sof_string](sof_string/) | Strings that know their length and hold any bytes, with append, insert, delete, case conversion, comparison and line and column of an offset, with a benchmark. |
| [sof_timer](sof_timer/) | A timer on the monotonic clock, with calibration, reporting in nanoseconds to seconds. |
| [sof_queue](sof_queue/) | A first-in, first-out queue of pointers, as a linked list. |
| [sof_random](sof_random/) | Pseudo-random numbers with xoshiro256** and splitmix64: unbiased ranges, doubles, bytes, shuffles and jumps for separate streams. |
| [sof_real](sof_real/) | Shortest round-trip formatting and exact parsing of doubles and floats, without the locale: Schubfach and Eisel-Lemire, with a benchmark. |
| [sof_regex](sof_regex/) | Regular expression search and split of sof_string strings through PCRE2, which it needs installed. |
| [sof_rbtree](sof_rbtree/) | A red-black tree, with ordered traversal and stepping between nodes. |
| [sof_ring](sof_ring/) | A fixed-capacity circular buffer of fixed-size values, allocated once, that can overwrite its oldest element when full. |
| [sof_set](sof_set/) | A hash set of keys of any type, with union, intersection, difference, symmetric difference and subset tests. |
| [sof_sort](sof_sort/) | Bubble, heap, insertion, merge and quick sort for arrays of any type, with a benchmark. |
| [sof_toml](sof_toml/) | TOML 1.1.0: a document tree that parses and checks a whole text, with indexed tables and lookup by dotted key; and a streaming writer that adds the headers, quotes and escapes and refuses misplaced pieces. Tested against toml-test, the TOML project's own suite. |
| [sof_utf](sof_utf/) | UTF-8, UTF-16 and UTF-32 encoding, decoding, validation and iteration, with conversions between them. |
| [sof_vector](sof_vector/) | A growable array of fixed-size values, with insert, remove, find, sort and binary search. |

## Demos

A library's demo is a real program, in its _demo/_ directory with its own Makefile and README, that shows the library at work alongside others from the collection. The library's Makefile builds it, so `make` in a library's directory, or `make sof_hashmap` from the root, builds the library and its demo, and `make` builds everything. Every library has one. The table lists the libraries each demo uses directly, by including its header; every demo that uses _sof_hashmap_ also uses _sof_hash_ through it, and every one that allocates uses _sof_alloc_.

| library | demo | | libraries used |
|---|---|---|---|
| sof_alloc | [memstat](sof_alloc/demo/) | Counts a word-counting run's every malloc, realloc and free through an allocator of its own, and reports the peak, bad frees and a histogram of block sizes. | sof_alloc, sof_bit, sof_copt, sof_hashmap, sof_string, sof_vector |
| sof_base | [basenc](sof_base/demo/) | Encodes and decodes files in base16, base32, base32hex, base64 or base64url, as GNU basenc does, strictly or leniently, a chunk at a time. | sof_base, sof_copt |
| sof_bit | [sieve](sof_bit/demo/) | Prints the primes up to a number, counts them, or says whether given numbers are prime, by a sieve of Eratosthenes over a bit array. | sof_alloc, sof_bit, sof_copt |
| sof_bloom | [uniq](sof_bloom/demo/) | Removes duplicate lines, adjacent or not, exactly or in fixed memory. | sof_alloc, sof_bloom, sof_copt, sof_hash, sof_hashmap, sof_string, sof_vector |
| sof_config | [ini](sof_config/demo/) | Prints and queries .ini files: every setting, one section, or one key's value as a string, integer, double or boolean. | sof_config, sof_copt |
| sof_copt | [lines](sof_copt/demo/) | Prints the lines of files, numbered, squeezed or cut to a range, with each feature's options defined in its own source file. | sof_copt, sof_string |
| sof_csv | [csvcut](sof_csv/demo/) | Cuts columns out of CSV files by name or number, and converts between delimiters, line endings and quoting. | sof_copt, sof_csv, sof_string, sof_vector |
| sof_debug | [hanoi](sof_debug/demo/) | Solves the Towers of Hanoi, printing each move, and traces its recursion in debug builds. | sof_copt, sof_debug, sof_stack |
| sof_digest | [digestsum](sof_digest/demo/) | Prints or checks the SHA-256 digests of files, or with -a any other digest or CRC of sof_digest's, in GNU sha256sum's format or BSD's tag format, reading lists from either. | sof_alloc, sof_copt, sof_digest, sof_string |
| sof_hash | [hashsum](sof_hash/demo/) | Prints or checks the 64-bit hashes of files, or of each line, like md5sum. | sof_copt, sof_hash, sof_string |
| sof_hashmap | [ngram](sof_hashmap/demo/) | Finds the n-grams of characters or words in text: the most frequent, and how often any given one occurs. | sof_alloc, sof_copt, sof_hashmap, sof_queue, sof_sort, sof_utf |
| sof_heap | [top](sof_heap/demo/) | Prints the most frequent words, lines or n-grams of a large input, with a benchmark. | sof_alloc, sof_copt, sof_hashmap, sof_heap, sof_regex, sof_sort, sof_string, sof_timer |
| sof_intern | [index](sof_intern/demo/) | Builds a concordance of a text: each word, in order, with the lines it appears on. | sof_alloc, sof_copt, sof_intern, sof_rbtree, sof_string, sof_utf, sof_vector |
| sof_json | [csvjson](sof_json/demo/) | Converts CSV to JSON: an array of objects keyed by the first record's names, JSON Lines, or arrays of fields; and with -j, JSON or JSON Lines back to CSV, a record at a time. | sof_copt, sof_csv, sof_json, sof_string |
| sof_log | [logrun](sof_log/demo/) | Logs the lines of files at the levels their first words name, and shows what logging from a thread of its own changes. | sof_alloc, sof_copt, sof_log, sof_string, sof_timer |
| sof_lru | [cachesim](sof_lru/demo/) | Replays or generates a trace of keys through an LRU cache of one or more sizes and reports the hit rates. | sof_alloc, sof_bit, sof_copt, sof_lru, sof_random, sof_sort |
| sof_mem | [concord](sof_mem/demo/) | Builds a concordance in arenas and pools, with a tracker's report of the memory used. | sof_alloc, sof_copt, sof_mem, sof_rbtree, sof_string |
| sof_queue | [bank](sof_queue/demo/) | Simulates customers queuing for bank tellers and reports wait times and queue lengths. | sof_alloc, sof_config, sof_copt, sof_mem, sof_queue, sof_random, sof_rbtree |
| sof_random | [markov](sof_random/demo/) | Learns n-grams from text and generates new text from them. Uses _ngram.c_ from [ngram](sof_hashmap/demo/). | sof_alloc, sof_copt, sof_hashmap, sof_random, sof_sort, sof_string, sof_utf |
| sof_rbtree | [basic](sof_rbtree/demo/) | A BASIC interpreter: ECMA-55 Minimal BASIC with string functions, run from a file or interactively. | sof_alloc, sof_copt, sof_hashmap, sof_random, sof_rbtree, sof_stack, sof_string, sof_vector |
| sof_real | [fpinfo](sof_real/demo/) | Shows what decimal numbers become as doubles or floats: the shortest form that reads back, the exact value, the bits and the neighbours; or just the shortest forms, to tidy a column of numbers. | sof_alloc, sof_copt, sof_real, sof_string |
| sof_regex | [match](sof_regex/demo/) | A small grep, for PCRE2 regular expressions, with context lines. | sof_alloc, sof_copt, sof_log, sof_regex, sof_ring, sof_string |
| sof_ring | [tail](sof_ring/demo/) | Prints the last lines, or bytes, of files, holding them in a ring that overwrites its oldest. | sof_copt, sof_ring, sof_string |
| sof_set | [deps](sof_set/demo/) | Prints an order to build targets in from a file of their dependencies, or reports a cycle. | sof_alloc, sof_config, sof_copt, sof_hashmap, sof_log, sof_queue, sof_set |
| sof_sort | [anagram](sof_sort/demo/) | Finds the groups of words in a word list that are anagrams of each other, or the anagrams of given words. | sof_alloc, sof_copt, sof_hashmap, sof_sort, sof_string |
| sof_stack | [calc](sof_stack/demo/) | An expression calculator with variables, traced in debug builds. | sof_alloc, sof_copt, sof_debug, sof_hashmap, sof_stack, sof_string |
| sof_string | [spell](sof_string/demo/) | Checks text against a dictionary and suggests the nearest known words for unknown ones. | sof_alloc, sof_copt, sof_hashmap, sof_log, sof_sort, sof_string, sof_timer |
| sof_timer | [timeit](sof_timer/demo/) | Times several ways of reading, counting and hashing a file, with the best, median and worst runs. | sof_alloc, sof_copt, sof_hash, sof_sort, sof_timer |
| sof_toml | [tomljson](sof_toml/demo/) | Converts TOML to JSON and JSON to TOML, plain or as toml-test's tagged JSON, so it can be tested by toml-test as a decoder and an encoder. | sof_copt, sof_json, sof_toml |
| sof_utf | [utfconv](sof_utf/demo/) | Converts text between UTF-8, UTF-16 and UTF-32, or checks it and reports where each error is. | sof_alloc, sof_copt, sof_string, sof_utf |
| sof_vector | [numstat](sof_vector/demo/) | Prints the count, mean, median and percentiles of a list of numbers, and finds values by binary search. | sof_copt, sof_vector |

## Building

    make                    # builds every library, benchmark and demo, the tests and build/libsof.a
    make check              # builds and runs every library's tests; make test does the same
    make debug=1            # builds with debug info and without optimization
    make clean              # removes what make built
    make sanitize           # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck           # runs the tests under valgrind (Linux)
    make sof_hashmap        # builds one library, with its tests, benchmark and demo; also clean-sof_hashmap
    make check-sof_hashmap  # runs one library's tests; also sanitize- and memcheck-sof_hashmap
    make lib                # builds build/libsof.a, every library in one static library, and build/sof.pc
    make install            # installs them in /usr/local/lib and /usr/local/lib/pkgconfig, and the headers in /usr/local/include/sof

`CC`, `CFLAGS` and `LDFLAGS` given on the command line, as in `make CC=gcc check`, are passed on to every library. `make -j` builds the libraries in parallel. Running `make` in a library's own directory builds that library alone, and `make check`, `make sanitize` and `make memcheck` there test it alone.

`make lib` compiles every library's _.c_ files, but benchmarks, tests and demos, into _build/_ and archives them as _build/libsof.a_. A program links with it like any static library, and gets only the objects it uses:

    cc -I/usr/local/include/sof prog.c -L/usr/local/lib -lsof -lpcre2-8 -lm

or with `#include <sof/sof_hashmap.h>` and no _-I_. `make lib` also writes _build/sof.pc_, which `make install` puts in _$(PREFIX)/lib/pkgconfig_, so pkg-config gives the same flags, with whatever system libraries the build needs:

    cc prog.c $(pkg-config --cflags --libs sof)

It is written for the `PREFIX`, `threaded` and `without` given, so it names PCRE2 only when _sof_regex_ is in the library, and adds `-D SOF_LOG_THREADED` and _-pthread_ for a threaded build. The headers include each other by name, so they stay together in one directory. _sof_regex_ needs PCRE2, so a program that uses it links with _-lpcre2-8_; `make lib without=sof_regex` leaves it out where PCRE2 isn't installed, and `without` takes any libraries no other one in the archive needs. _sof_bloom_ uses the maths library, _-lm_. `make lib threaded=1` compiles _sof_log_ with `SOF_LOG_THREADED`, and a program using that build must be compiled with `-D SOF_LOG_THREADED` and linked with _-pthread_. `make install` copies the library to _$(PREFIX)/lib_, _sof.pc_ to _$(PREFIX)/lib/pkgconfig_ and the headers to _$(PREFIX)/include/sof_, with `PREFIX` _/usr/local_ unless it is given, and `DESTDIR` before both for staging; `make uninstall` removes them. Change `debug`, `threaded` or `without` only after `make clean`, since objects already built aren't rebuilt for them.

Each library's tests are a program of their own, _test_, built from _test.c_ in the library's directory by its Makefile and run from there; _sof_log_'s are built twice, the second time as _test-threaded_ with `SOF_LOG_THREADED`, and _sof_bit_'s three times, for three word types. `make check` at the root runs `make check` in every library, going on past one that fails and naming those that did at the end; `make check run="sof_hash sof_rbtree"` tests only the libraries named. The harness the tests share, _test.h_, and the test targets every library's Makefile includes, _test.mk_, are in [test](test/).

`make sanitize` builds each library's tests with debug info and the sanitizers and runs them, removing the tests and the library's objects before and after, so sanitized objects never mix with an ordinary build. It fails on any memory error or undefined behaviour, and on Linux on any leak. `make memcheck` runs the ordinary tests under valgrind's memcheck, which also finds reads of memory that was never set, and fails on any error or leak. Valgrind doesn't run on current macOS. On Arch Linux, whose glibc valgrind can't start with unless it fetches its symbols, _test.mk_ sets `DEBUGINFOD_URLS` to Arch's debuginfod server if it isn't already set.

On GitHub, _.github/workflows/ci.yml_ runs the same targets on every push: `make`, `make check`, `make sanitize` and `make lib` on Linux with gcc and clang and on macOS with clang, and `make memcheck` on Linux.

## License

MIT; see _LICENSE_. Third-party code included with a library, such as rapidhash, is under its own license, which the library's README names.
