# sof_regex

Regular expression search and split of fat strings in c.

The library has a `sof_regex_t` type that holds a compiled [PCRE2](https://github.com/PCRE2Project/pcre2) regular expression. The application provides the `sof_regex_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_regex_init()`, which compiles a pattern with an optional allocator. `sof_regex_find()` finds the regex's matches in a `sof_string_t` one at a time, and `sof_regex_split()` returns the fields between them. `sof_regex_jit()`, if the application asks for it, compiles the regex further, to machine code with PCRE2's JIT, so that searches run faster, and `sof_regex_jit_size()` says how much memory that machine code takes outside the allocator. `sof_regex_free()` releases the memory once the application is finished.

Searches keep their place in a `size_t` the caller holds, not in the string or the regex, so a search only reads its string, and one regex can search several strings in turn. Each search writes the regex's match data, though, so one regex can be used by only one search at a time.

The library's demo is the [match](demo/) program in _demo/_, a small grep. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All sof_regex declarations are in _sof_regex.h_. Strings are [sof_string](../sof_string/)'s, from _../sof_string/_ (_sof_string.c_ and _sof_string.h_), and memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). PCRE2 isn't part of this collection and must be installed; it is used only by _sof_regex.c_, so _sof_regex.h_ doesn't need its headers. PCRE2 gets all of its memory from the regex's allocator, except for the machine code of a regex given to `sof_regex_jit()` (see there). Beyond these, the library needs nothing but the c standard library.

    sof_string_t s;
    sof_regex_t re;
    size_t position = 0, offset, length;
    sof_string_from_cstr(&s, "one, two,three", NULL);     // a copy; no allocator
    sof_regex_init(&re, "\\s*,\\s*", NULL);                 // a comma and any spaces around it
    while (sof_regex_split(&re, &s, &position, &offset, &length) == SOF_REGEX_OK) {
        printf("%.*s\n", (int)length, s.data + offset);  // one, two, three
    }
    sof_regex_free(&re);
    sof_string_free(&s);

## Building

The library needs PCRE2: on macOS, `brew install pcre2`; on Arch Linux, `pacman -S pcre2`. The Makefile takes PCRE2's compiler flags from `pcre2-config` if it is installed, else from `pkg-config libpcre2-8`, and links the tests with the linker flags it finds the same way, or, if neither gives any, with plain _-lpcre2-8_ from the compiler's own paths.

    make             # builds sof_regex.o, ./test and the demo, demo/match
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

The tests use generated data. Besides testing each function, with **NULL** and dead regexes and strings, they search and split random strings, checking each match and field by hand; fail every allocation in turn, PCRE2's included, checking that each failure is reported and leaves nothing leaked; run into PCRE2's match and heap limits; search and split a string of 100,000 words; and search one string from several threads at once. Every test runs twice, the second time with each regex given to `sof_regex_jit()`, and a test of its own checks that regexes with and without it find the same matches and fields in random text, for the patterns of the benchmark below and others, and that a match too deep for the JIT's stack is still found. `make memcheck` runs valgrind with _valgrind.supp_, which suppresses its reports on the JIT's machine code, which reads a subject a word at a time, past its end, and discards the bytes that aren't the subject's. The tests are linked with _-pthread_; the library itself doesn't use threads.

To use the library in another project, copy _sof_regex.c_ and _sof_regex.h_, _sof_string.c_ and _sof_string.h_ from _../sof_string/_, and _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into it, compile the _.c_ files with the project's own sources, _sof_regex.c_ with the flags from `pcre2-config --cflags` (or `pkg-config --cflags libpcre2-8`), and link with those from `pcre2-config --libs8` (or `pkg-config --libs libpcre2-8`, or just _-lpcre2-8_). To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_ and the three _.c_ files with _-pthread_, linking with PCRE2's library as above.

## JIT

`sof_regex_jit()` is off unless the application calls it. Measured by timing `sof_regex_find()` over each line of Shakespeare (_../test-data/shakespeare.txt_), best of 5 runs, and the time `sof_regex_init()` plus `sof_regex_jit()` takes per pattern, best of 200:

| Pattern | Mac, without | Mac, with | morbius, without | morbius, with | davros, without | davros, with |
|---|---|---|---|---|---|---|
| `love` | 757 MB/s | 1765 MB/s | 828 MB/s | 1466 MB/s | 667 MB/s | 1054 MB/s |
| `[A-Z][a-z]+ing\b` | 696 | 1193 | 714 | 1048 | 586 | 794 |
| `\b(\w+)\s+\1\b` | 65 | 255 | 66 | 233 | 48 | 206 |
| `(?i)king\|queen\|prince` | 629 | 863 | 612 | 820 | 494 | 615 |
| `\d+` | 1143 | 1327 | 1021 | 1207 | 820 | 895 |
| compile time | 0.2–0.4 µs | 1.0–2.2 µs | 0.1–0.4 µs | 1.5–3.4 µs | 0.2–0.6 µs | 2.0–4.3 µs |

The Mac is an Apple M4 Max with Apple clang 21; morbius an Intel i9 with gcc 16 on Arch Linux; davros an AMD Ryzen 9 5950X with gcc 16 on Arch Linux; all with PCRE2 10.49.

The machine code is small: `sof_regex_jit_size()` gives 412 bytes for `\d+` and 668 to 912 for the other patterns above, on the Mac.

## Patterns

Patterns are in PCRE2's syntax, which is Perl's. They match bytes, not UTF-8 characters, so `.` matches one byte of a multi-byte character, and NULs and bytes above 0x7f can be matched like any other (`\x00`, `[\x80-\xff]`). Options are set inside the pattern: `(?i)` for case-insensitive matching, `(?m)` for `^` and `$` at line breaks, and so on.

A search or split starts at the position the caller gives it, but sees the whole string, so `^` matches only at the very start, and `\b` and lookbehinds see the bytes before the position.

A regex that can match the empty string needs a rule to keep it from matching in the same place forever. `sof_regex_find()` moves one byte past an empty match, as JavaScript does, so `x*` in "ab" matches at **0**, **1** and **2**; one consequence is that after an empty match, a longer match at the same place is passed over (`|a` in "a" matches the empty string at **0** and **1**, never "a"). `sof_regex_split()` never takes an empty match at the start of a field as a delimiter, so `x*` splits "ab" into "a", "b" and "".

## Functions

`sof_regex_error_t sof_regex_init(sof_regex_t *regex, const char *pattern, const sof_alloc_t *alloc)`

Compile the NUL-terminated _pattern_ into _regex_ (see Patterns), overwriting it without freeing what it held. _alloc_ is the allocator PCRE2 gets all of its memory from, for compiling and matching; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). The regex keeps a copy, so it can be on the stack. Returns **SOF_REGEX_OK**, **SOF_REGEX_BAD_PATTERN** if _pattern_ doesn't compile, **SOF_REGEX_NO_MEM**, or **SOF_REGEX_ERROR** if _regex_ or _pattern_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the _regex_ is left dead.

`sof_regex_error_t sof_regex_jit(sof_regex_t *regex)`

Compile _regex_ further, to machine code with PCRE2's JIT, so that `sof_regex_find()` and `sof_regex_split()` run it instead of PCRE2's interpreter. They find the same matches and fields either way. It is optional, and nothing uses it unless this is called, once, after `sof_regex_init()`. It adds one to three microseconds to compiling each pattern (more the first time a program uses it), and speeds searching up by 1.2 to 4 times (see JIT), so it pays for a regex that searches more than a few kilobytes. It is the one exception to PCRE2 getting all of its memory from the regex's allocator: compiling allocates through it, but the machine code lives in executable memory PCRE2 maps for itself, which `sof_regex_free()` releases. The machine code runs on 32 KiB of the thread's own stack, and a search that needs more is run again without it, so it gives the same result. PCRE2's limits are counted differently by the machine code: its heap limit doesn't apply, and its match limit counts other steps, so a pattern that hits a limit (**SOF_REGEX_MATCH_ERROR**) without it may not with it, and the other way round. If PCRE2 was built without its JIT, or the code can't be made (memory for it couldn't be had, for one), _regex_ is left as it was and goes on working without it. Returns **SOF_REGEX_OK** if _regex_ now runs as machine code, also if it already did; **SOF_REGEX_NO_MATCH** if it couldn't be compiled and is left as it was; or **SOF_REGEX_ERROR** if _regex_ is **NULL** or dead.

`size_t sof_regex_jit_size(const sof_regex_t *regex)`

The size in bytes of _regex_'s machine code, which is the memory `sof_regex_jit()` took outside its allocator, for an application that keeps count of its memory. PCRE2 maps executable memory in larger pieces and shares them among patterns, so what the program has mapped can be somewhat more than the sum. Returns **0** if _regex_ has no machine code: it wasn't given to `sof_regex_jit()`, that couldn't compile it, or _regex_ is **NULL** or dead.

`sof_regex_error_t sof_regex_find(sof_regex_t *regex, const sof_string_t *subject, size_t *position, size_t *match_offset, size_t *match_length)`

Find the next match of _regex_ in _subject_, starting at _*position_, and set _*match_offset_ and _*match_length_ to it; either can be **NULL** if it isn't wanted. _*position_ is set past the match, or a byte past an empty one, so a loop of calls finds every match in turn:

    size_t position = 0, offset, length;
    while (sof_regex_find(&re, &s, &position, &offset, &length) == SOF_REGEX_OK) { ... }

To start again, set _position_ back to **0**, or to any other place to search from there. A dead _subject_ is empty. Returns **SOF_REGEX_OK**, **SOF_REGEX_NO_MATCH** if there are no more matches, as there never are once _*position_ is past the subject's length, **SOF_REGEX_NO_MEM** if PCRE2 couldn't get memory for the match, **SOF_REGEX_MATCH_ERROR** if PCRE2 stopped the match otherwise, as when it hits its match limit, or **SOF_REGEX_ERROR** if _regex_, _subject_ or _position_ is **NULL**, or _regex_ is dead. _*position_ changes only on **SOF_REGEX_OK**, so a search that failed can be tried again.

`sof_regex_error_t sof_regex_split(sof_regex_t *regex, const sof_string_t *subject, size_t *position, size_t *field_offset, size_t *field_length)`

Set _*field_offset_ and _*field_length_ to the field of _subject_ that starts at _*position_: the bytes up to the next match of _regex_, which is the delimiter and isn't part of any field. _*position_ is set past the delimiter, where the next field starts, so a loop of calls returns every field, as `sof_regex_find()` does matches. A string with _n_ delimiters has _n_ + 1 fields, so delimiters at the start or end, or next to each other, give empty fields, and a string with no delimiter, the empty string included, is one field. After the last field, _*position_ is past the subject's length. A dead _subject_ is one empty field. Returns as `sof_regex_find()` does, with **SOF_REGEX_NO_MATCH** once there are no more fields.

`sof_regex_error_t sof_regex_free(sof_regex_t *regex)`

Free the memory of _regex_ and leave it dead. The `sof_regex_t` itself isn't freed. Returns **SOF_REGEX_OK**, or **SOF_REGEX_ERROR** if _regex_ is **NULL**.

Every function accepts **NULL**, and regexes that were freed or whose initialization failed. A dead regex makes `sof_regex_find()`, `sof_regex_jit()` and `sof_regex_split()` return **SOF_REGEX_ERROR**, `sof_regex_jit_size()` returns **0**, and freeing it again does nothing. A dead subject string is empty, so a search finds in it only what matches the empty string.

## Types

    typedef enum sof_regex_error_t {
        SOF_REGEX_OK = 0,
        SOF_REGEX_NO_MEM,
        SOF_REGEX_NO_MATCH,
        SOF_REGEX_ERROR,
        SOF_REGEX_BAD_PATTERN,          // the pattern doesn't compile
        SOF_REGEX_MATCH_ERROR,          // PCRE2 stopped a match, as at its match limit
    } sof_regex_error_t;

    typedef struct sof_regex_t {
        void *code;                     // PCRE2's compiled pattern; NULL when dead
        void *match_data;               // where PCRE2 puts a match
        void *match_context;
        void *memory;                   // heap copy of the allocator, which PCRE2 is given
    } sof_regex_t;

A compiled regular expression. Its fields belong to the library, and are **NULL** when it is dead. PCRE2 points to the allocator's heap copy, not to the `sof_regex_t`, so the regex can be moved or copied by value, though only one copy may be freed.

## Threading

The library has no global state and does no locking. Different regexes can be used on different threads freely.

`sof_regex_find()` and `sof_regex_split()` only read the subject string, so any number of threads can search one string at once, with a readers-writer lock if another thread changes it. But every search writes the regex's match data, so a regex can be used by only one search at a time, even though the functions take it as a pointer that isn't `const`: give each thread its own `sof_regex_t`, compiled from the same pattern, as the tests do. `sof_regex_jit()` changes the compiled pattern, so it must not run while the regex is searching; `sof_regex_jit_size()` only reads it. PCRE2's JIT keeps executable memory shared by the whole program, which PCRE2 locks itself. A regex whose allocator is shared between threads needs an allocator that is safe to call from several threads at once.
