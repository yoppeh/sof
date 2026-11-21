# sof_string

Fat strings in c.

The library has a `sof_string_t` type that holds a string's data together with its length, so a string can hold any bytes, NULs included, and its length is known without scanning. The application provides the `sof_string_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_string_init()`, which makes an empty string with room for a given number of bytes, or with `sof_string_from_bytes()`, `sof_string_from_cstr()`, `sof_string_dup()` or `sof_string_substr()`, which copy some bytes, a c string, another string or part of one; each takes an optional allocator. `sof_string_const()` makes a read-only string of a literal without copying it, and `sof_string_view()` one of any bytes. `sof_string_append()`, `sof_string_append_bytes()`, `sof_string_append_cstr()` and `sof_string_append_char()` add to the end of a string, `sof_string_reserve()` and `sof_string_commit()` let the caller write there directly, `sof_string_insert()`, `sof_string_insert_bytes()` and `sof_string_insert_cstr()` add anywhere in it, `sof_string_delete()` removes bytes from it, `sof_string_clear()` empties it, and `sof_string_tolower()` and `sof_string_toupper()` change the case of its ASCII letters. `sof_string_cmp()` compares two strings, `sof_string_casecmp()` compares them ignoring the case of ASCII letters, and `sof_string_cstr()` returns the data as a c string. `sof_string_position()` gives the line and column of an offset, such as a place a parser reports. `sof_string_free()` releases the memory once the application is finished. Regular expression search and split of these strings are in [sof_regex](../sof_regex/), which needs PCRE2; this library doesn't.

The data is always followed by a NUL, except in a `sof_string_view()`, which isn't counted in the length, so `sof_string_cstr()` can hand it to functions that expect a c string. A string grows to at least twice its size when it must grow, so appending a byte at a time takes linear time, and its data can move whenever it grows.

The library's demo is the [spell](demo/) program in _demo/_, which checks text against a dictionary and suggests the nearest known words. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). _bench.c_ is a benchmark against the c library's NUL-terminated strings (see Benchmark). All sof_string declarations are in _sof_string.h_. Memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The benchmark, only, uses [sof_copt](../sof_copt/) in _../sof_copt/_ for its options, [sof_random](../sof_random/) in _../sof_random/_ for its strings and [sof_timer](../sof_timer/) in _../sof_timer/_ for timing. Beyond that, the library needs nothing but the c standard library.

    sof_string_t s;
    sof_string_from_cstr(&s, "one", NULL);               // a copy; no allocator
    sof_string_append_cstr(&s, ", two");
    sof_string_append_bytes(&s, "\0three", 6);           // any bytes, NULs included
    sof_string_toupper(&s);
    fwrite(s.data, 1, s.length, stdout);                // ONE, TWO\0THREE
    sof_string_free(&s);

## Building

    make             # builds sof_string.o, ./test, ./bench and the demo, demo/spell
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization
    ./bench          # compares sof_string with the c library's strings

The tests use generated data. Besides testing each function, with NULL, dead and read-only strings, they insert every part of a string into itself at every place, with room to spare and growing, and make 20,000 random edits, inserts of its own bytes among them, to a string and to a plain array alongside it, and compare the two after every edit; compare random strings with `memcmp()`, strings of up to 140 bytes differing at each place, and random strings of up to 140 bytes of mixed case with `sof_string_casecmp()` against lower case copies, and every pair of bytes without case, alone and at each of 16 places in longer strings; change the case of every byte value in strings of up to 130 bytes, against a byte at a time; and fail every allocation in turn, checking that each failure is reported and leaves nothing leaked.

To use the library in another project, copy _sof_string.c_ and _sof_string.h_, and _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into it, and compile the _.c_ files with the project's own sources. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_, _sof_string.c_ and _sof_alloc.c_ with _-pthread_.

## Benchmark

    bench [-h] [-n bytes]

Times six measures on strings of random letters of both cases, from **16** bytes, then **8** times as many each time, up to _bytes_, once with sof_string and once with the c library's NUL-terminated strings, in nanoseconds per call. _c / sof_ is how many times as long the c library takes. Every timing is the best of **5** runs, and every run goes over at least 16 MB of strings, so that short strings are timed many at a time. Both use the same allocator, the standard one through [sof_alloc](../sof_alloc/).

- _length_: the `length` field, against `strlen()`.
- _compare_: `sof_string_cmp()`, against `strcmp()`, of two strings that differ in their last byte.
- _copy_: `sof_string_dup()` and `sof_string_free()`, against `strlen()`, an allocation, `memcpy()` and a free.
- _append_: a string built **8** bytes at a time, with `sof_string_append_bytes()` from an empty string that grows as it needs, against `strcat()` into a buffer allocated at the string's final size; per append.
- _insert_: **8** bytes put in the middle of a string and taken out again, with `sof_string_insert_bytes()` and `sof_string_delete()`, against `strlen()`, `memmove()` and `memcpy()`; per insert and delete.
- _tolower_: `sof_string_tolower()`, against a loop of `tolower()`.

Options:

- _-h_ prints the usage message and exits.
- _-n bytes_ sets the longest string, from **16** to **16777216** (default **65536**). `strcat()` takes time in the square of a string's length, so the append measure builds fewer strings the longer they are, but past a megabyte it still takes a while.

For example, on an Apple M4 Max, macOS, Apple clang 21:

    $ ./bench
    ns per call, best of 5 runs; c / sof is how many times as long c strings take

    length: the length field, against strlen()
         bytes  sof_string           c  c / sof
            16        0.44        1.15      2.6
           128        0.42        3.32      7.9
          1024        0.10       15.96    159.6
          8192        0.10      124.55   1245.5
         65536        0.24      919.72   3832.2

    compare: sof_string_cmp(), against strcmp(), of strings differing in the last byte
         bytes  sof_string           c  c / sof
            16        1.80        4.17      2.3
           128        4.64        6.69      1.4
          1024       26.42       21.45      0.8
          8192      178.77      175.00      1.0
         65536     1252.73     1197.54      1.0

    copy: sof_string_dup() and sof_string_free(), against strlen(), allocation, memcpy() and free
         bytes  sof_string           c  c / sof
            16       10.44        9.35      0.9
           128       11.67       10.40      0.9
          1024       22.37       36.80      1.6
          8192      116.55      238.99      2.1
         65536      794.23     1546.02      1.9

    append: 8 bytes to an empty string, against strcat() into a buffer of the final size; per append
         bytes  sof_string           c  c / sof
            16       23.22        9.07      0.4
           128        9.92        6.84      0.7
          1024        4.74       11.53      2.4
          8192        3.07       67.49     22.0
         65536        2.78      468.58    168.6

    insert: 8 bytes into the middle and out again, against strlen(), memmove() and memcpy(); per pair
         bytes  sof_string           c  c / sof
            16        7.02        7.82      1.1
           128        7.26       16.05      2.2
          1024       15.81       35.79      2.3
          8192       76.92      290.64      3.8
         65536      576.62     1966.92      3.4

    tolower: sof_string_tolower(), against a loop of tolower()
         bytes  sof_string           c  c / sof
            16        1.67       18.71     11.2
           128        2.52      148.39     58.9
          1024       16.63     1157.26     69.6
          8192      132.95     9280.84     69.8
         65536      995.40    73192.33     73.5

and on an Intel i9-13900K, Arch Linux, gcc 16 and glibc, _c / sof_ only:

         bytes   length  compare     copy   append   insert  tolower
            16      1.2      0.5      0.7      0.8      1.5      3.4
           128      5.4      0.8      1.0      1.0      1.8      7.6
          1024     27.6      1.0      1.3      2.1      2.6      7.6
          8192    204.9      1.0      1.3     10.0      2.2      8.1
         65536   1645.0      1.0      1.4     77.0      3.2      8.3

and on an AMD Ryzen 9 5950X, Arch Linux, gcc 16 and glibc, _c / sof_ only:

         bytes   length  compare     copy   append   insert  tolower
            16      4.1      1.1      0.7      1.0      1.6      3.1
           128      4.4      0.7      0.8      1.1      1.4      9.5
          1024     23.8      0.9      1.2      2.0      2.2     12.4
          8192    317.8      1.0      1.3      4.9      1.7     12.7
         65536   3368.1      1.0      1.5     46.3      2.4     13.0

What a sof_string gains is knowing its length. Reading it is one load, where `strlen()` reads every byte, at about 70 bytes a nanosecond with macOS's and 150 with glibc's; so anything that needs the length, as a copy and an insert do, saves that pass, about half the time at a kilobyte and more. `strcat()` finds the end of what it appends to every time, so building a string with it takes time in the square of its length, 169 times as long as sof_string at 64 KB on macOS. A c program that kept the length itself, and called `memcpy()`, would do what sof_string does. For the shortest strings the c library can be as fast or faster: `strcat()` here writes into a buffer allocated at its final size, where the sof_string starts empty and grows. `sof_string_cmp()` compares strings under 32 bytes itself, 8 bytes at a time, and longer ones with `memcmp()`; that beats macOS's `strcmp()` on short strings, but not glibc's, and in this measure the i9 also spends time fetching the two 64-byte `sof_string_t` of each of 131,072 pairs, 16 MB, more than its cache holds, where `strcmp()` reads only the bytes; the Ryzen's 32 MB cache holds them, and there `sof_string_cmp()` is a little faster at 16 bytes. Comparing longer strings costs the same either way, since both read every byte. `sof_string_tolower()` writes every byte, changing only ASCII letters, with no branch, so the compiler vectorizes it, at 40 to 65 bytes a nanosecond; a loop of `tolower()`, which depends on the locale, takes 8 to 13 times as long with glibc's table lookup and 70 with macOS's.

## Functions

Functions that change a string refuse a read-only string, made by `sof_string_const()` or `sof_string_view()`, and a dead one (see Types), with **SOF_STRING_ERROR**. Those that initialize a string overwrite it without freeing what it held.

`sof_string_error_t sof_string_init(sof_string_t *string, size_t capacity, const sof_alloc_t *alloc)`

Initialize an empty _string_ with room for _capacity_ bytes before it must grow, and a byte more for the NUL. _alloc_ is the allocator for the string's data; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). It is copied into the string, so it can be on the stack. Returns **SOF_STRING_OK**, **SOF_STRING_NO_MEM**, or **SOF_STRING_ERROR** if _string_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the _string_ is left dead.

`sof_string_error_t sof_string_from_bytes(sof_string_t *string, const void *bytes, size_t length, const sof_alloc_t *alloc)`

Initialize a _string_ with a copy of the _length_ bytes at _bytes_, using _alloc_ as `sof_string_init()` does. The bytes can be anything, NULs included, and the string's memory is just large enough for them. _bytes_ can be **NULL** if _length_ is **0**, which makes an empty string. Returns **SOF_STRING_OK**, **SOF_STRING_NO_MEM**, or **SOF_STRING_ERROR** if _string_ is **NULL**, _bytes_ is **NULL** and _length_ isn't **0**, or _alloc_ is refused. On failure the _string_ is left dead.

`sof_string_error_t sof_string_from_cstr(sof_string_t *string, const char *cstring, const sof_alloc_t *alloc)`

Initialize a _string_ with a copy of the NUL-terminated _cstring_, using _alloc_ as `sof_string_init()` does. Returns **SOF_STRING_OK**, **SOF_STRING_NO_MEM**, or **SOF_STRING_ERROR** if _string_ or _cstring_ is **NULL** or _alloc_ is refused. On failure the _string_ is left dead.

`sof_string_const(s)`

A `sof_string_t` initializer for a string literal _s_, without copying it: `sof_string_t of = sof_string_const("of");`. The literal can hold NULs, which are part of the string: `sof_string_const("a\0b")` has length **3**. Such a string is read-only: it can be read, copied and appended to other strings, but functions that would change it return **SOF_STRING_ERROR**, and `sof_string_free()` empties it without freeing anything.

`sof_string_t sof_string_view(const void *bytes, size_t length)`

Return a read-only string of the _length_ bytes at _bytes_, without copying them, as `sof_string_const()` makes one of a literal: `sof_string_t word = sof_string_view(line + start, length);`. The bytes can be anything, NULs included, and must stay as they are while the string is used. It needs no freeing; `sof_string_free()` empties it without freeing the bytes. Unlike every other string, its data needn't be followed by a NUL, so `sof_string_cstr()` of it is a c string only if the bytes are followed by one; every other function uses only its _length_ bytes, and a copy made with `sof_string_dup()` or `sof_string_substr()` has its NUL. A **NULL** _bytes_ gives an empty read-only string, whatever _length_ is.

`sof_string_error_t sof_string_dup(sof_string_t *target, const sof_string_t *source, const sof_alloc_t *alloc)`

Initialize _target_ with a copy of _source_, using _alloc_ as `sof_string_init()` does. The two don't share memory, and the copy is just large enough for its data. Returns **SOF_STRING_OK**, **SOF_STRING_NO_MEM**, or **SOF_STRING_ERROR** if either string is **NULL**, _source_ is dead, _target_ is _source_, or _alloc_ is refused. On failure _target_ is left dead, unless it is **NULL** or _source_, which is left alone.

`sof_string_error_t sof_string_substr(sof_string_t *target, const sof_string_t *source, size_t offset, size_t length, const sof_alloc_t *alloc)`

Initialize _target_ with a copy of the _length_ bytes of _source_ starting at _offset_, using _alloc_ as `sof_string_init()` does. Returns **SOF_STRING_OK**, **SOF_STRING_NO_MEM**, or **SOF_STRING_ERROR** if either string is **NULL**, _source_ is dead, _target_ is _source_, the bytes don't lie within _source_, or _alloc_ is refused. On failure _target_ is left dead, unless it is **NULL** or _source_.

`sof_string_error_t sof_string_append(sof_string_t *target, const sof_string_t *suffix)`

Append _suffix_ to _target_, growing it as needed. _suffix_ can be _target_ itself, or a copy of its struct that shares its data, which doubles _target_; a dead _suffix_ is empty. Returns **SOF_STRING_OK**, **SOF_STRING_NO_MEM** if _target_ couldn't grow, in which case it is unchanged, or **SOF_STRING_ERROR** if either is **NULL**, or _target_ is dead or read-only.

`sof_string_error_t sof_string_append_bytes(sof_string_t *target, const void *bytes, size_t length)`

Append the _length_ bytes at _bytes_ to _target_, as `sof_string_append()` does. The bytes can be anything, NULs included, and can lie in _target_'s own data. _bytes_ can be **NULL** if _length_ is **0**, which appends nothing. Returns **SOF_STRING_OK**, **SOF_STRING_NO_MEM** if _target_ couldn't grow, in which case it is unchanged, or **SOF_STRING_ERROR** if _target_ is **NULL**, dead or read-only, or _bytes_ is **NULL** and _length_ isn't **0**.

`sof_string_error_t sof_string_append_cstr(sof_string_t *target, const char *suffix)`

Append the bytes of the NUL-terminated _suffix_ to _target_, as `sof_string_append()` does. _suffix_ can point into _target_'s own data. Returns as `sof_string_append()` does.

`sof_string_error_t sof_string_append_char(sof_string_t *target, char c)`

Append the one byte _c_, which can be any byte, NUL included, to _target_, as `sof_string_append_bytes()` does. It is `static inline` in _sof_string.h_, so when _target_ has room it is a store and no call, for writers that build their output a byte or two at a time. Returns as `sof_string_append_bytes()` does.

`char *sof_string_reserve(sof_string_t *target, size_t length)`

Make room at the end of _target_ for _length_ more bytes and the NUL, growing it as `sof_string_append()` does, for the caller to write into and then add with `sof_string_commit()`. _target_'s length and contents don't change. Returns where to write, which is _target_'s data plus its length and is good only until the next function that changes _target_, or **NULL** if _target_ is **NULL**, dead or read-only, or couldn't grow, in which case it is unchanged.

`sof_string_error_t sof_string_commit(sof_string_t *target, size_t length)`

Add to _target_ the _length_ bytes written after its end since `sof_string_reserve()`, and write the NUL after them. It is `static inline`, as `sof_string_append_char()` is. Returns **SOF_STRING_OK**, or **SOF_STRING_ERROR**, changing nothing, if _target_ is **NULL**, dead or read-only, or hasn't room for _length_ bytes and the NUL.

`sof_string_error_t sof_string_insert(sof_string_t *target, size_t offset, const sof_string_t *s)`

Insert _s_ into _target_ at _offset_, which can be anything from **0** to _target_'s length, where it appends. The bytes from _offset_ on move up to make room, and _target_ grows as `sof_string_append()` grows it. _s_ can be _target_ itself, or a view of any part of it, even a part the insertion moves; a dead _s_ is empty. With `sof_string_delete()`, it changes a text in place, such as at a span from [sof_toml](../sof_toml/), [sof_json](../sof_json/) or [sof_csv](../sof_csv/). Returns **SOF_STRING_OK**, **SOF_STRING_NO_MEM** if _target_ couldn't grow, in which case it is unchanged, or **SOF_STRING_ERROR** if either is **NULL**, _target_ is dead or read-only, or _offset_ is past its length.

`sof_string_error_t sof_string_insert_bytes(sof_string_t *target, size_t offset, const void *bytes, size_t length)`

Insert the _length_ bytes at _bytes_ into _target_ at _offset_, as `sof_string_insert()` does. The bytes can be anything, NULs included, and can lie in _target_'s own data. _bytes_ can be **NULL** if _length_ is **0**, which inserts nothing. Returns as `sof_string_insert()` does, and **SOF_STRING_ERROR** if _bytes_ is **NULL** and _length_ isn't **0**.

`sof_string_error_t sof_string_insert_cstr(sof_string_t *target, size_t offset, const char *s)`

Insert the bytes of the NUL-terminated _s_ into _target_ at _offset_, as `sof_string_insert()` does. _s_ can point into _target_'s own data. Returns as `sof_string_insert()` does.

    sof_string_delete(&text, line_start, line_end - line_start);        // remove a line
    sof_string_insert_cstr(&text, table_end, "\nsof = \"0.1\"");        // add one

`sof_string_error_t sof_string_delete(sof_string_t *string, size_t offset, size_t length)`

Remove the _length_ bytes of _string_ starting at _offset_; the bytes after them move down, and the memory is kept. Returns **SOF_STRING_OK**, or **SOF_STRING_ERROR** if _string_ is **NULL**, dead or read-only, or the bytes don't lie within it.

`sof_string_error_t sof_string_clear(sof_string_t *string)`

Empty _string_, keeping its memory for what is appended next. Returns **SOF_STRING_OK**, or **SOF_STRING_ERROR** if _string_ is **NULL**, dead or read-only.

`sof_string_error_t sof_string_tolower(sof_string_t *string)`

`sof_string_error_t sof_string_toupper(sof_string_t *string)`

Convert the ASCII letters of _string_ to lower or upper case in place. Other bytes, UTF-8 sequences included, are left alone, whatever the locale. Returns **SOF_STRING_OK**, or **SOF_STRING_ERROR** if _string_ is **NULL**, dead or read-only.

`int sof_string_cmp(const sof_string_t *a, const sof_string_t *b)`

Compare two strings byte by byte, as unsigned bytes. A string that is a prefix of another is less than it. **NULL** and dead strings are empty. Returns **-1**, **0** or **1** as _a_ is less than, equal to or greater than _b_.

`int sof_string_casecmp(const sof_string_t *a, const sof_string_t *b)`

Compare two strings as `sof_string_cmp()` does, but with each ASCII upper case letter folded to lower case first, as `sof_string_tolower()` folds it, so the result is what `sof_string_cmp()` would give for lower case copies of _a_ and _b_, without making them or changing either string. Other bytes, UTF-8 sequences included, compare as they are, whatever the locale, so "Caf\xc3\xa9" equals "CAF\xc3\xa9" but not "CAF\xc3\x89". **NULL** and dead strings are empty. Returns **-1**, **0** or **1** as _a_ is less than, equal to or greater than _b_.

`const char *sof_string_cstr(const sof_string_t *string)`

Return the _string_'s data, which is followed by a NUL, so it can be passed as a c string. It belongs to the _string_, so don't change or free it. A function that takes a c string sees only the bytes up to the first NUL, which comes before the end if the string holds one. Returns **NULL** if _string_ is **NULL** or dead.

`sof_string_error_t sof_string_position(const sof_string_t *string, size_t offset, size_t *line, size_t *column)`

Give the line and column of byte _offset_ in _string_, for a place a parser reports as an offset, such as a span from [sof_toml](../sof_toml/), [sof_json](../sof_json/) or [sof_csv](../sof_csv/); `sof_string_view()` makes a string of a text without copying it. _offset_ can be anything from **0** to the string's length, which is its end. Lines end at each line feed, so a CRLF line's carriage return is the last column of its line. _line_ gets the line and _column_ the column, in bytes, both counting from **1**; either can be **NULL**. Returns **SOF_STRING_OK**, or **SOF_STRING_ERROR**, leaving _line_ and _column_ alone, if _string_ is **NULL** or _offset_ is past its length. A dead string is empty.

`sof_string_error_t sof_string_free(sof_string_t *string)`

Free the _string_'s data and leave it dead; a read-only string is emptied without freeing anything. The `sof_string_t` itself isn't freed. Returns **SOF_STRING_OK**, or **SOF_STRING_ERROR** if _string_ is **NULL**.

Every function accepts **NULL**, and strings that were freed or whose initialization failed. A dead string is empty: it compares equal to "", `sof_string_cstr()` returns **NULL** for it, and functions that change it return **SOF_STRING_ERROR** until it is initialized again. Freeing it again does nothing.

## Types

    typedef enum sof_string_error_t {
        SOF_STRING_OK = 0,
        SOF_STRING_NO_MEM,
        SOF_STRING_NO_MATCH,
        SOF_STRING_ERROR,
    } sof_string_error_t;

    typedef struct sof_string_t {
        char *data;                     // the bytes, followed by a NUL but in a view; NULL when dead
        size_t length;                  // bytes in use, not counting the NUL
        size_t size;                    // size of the block data points to; 0 if it isn't the string's own
        sof_alloc_t alloc;                   // copy of the allocator; zeroed for the standard functions
    } sof_string_t;

A string. The application may read the fields but must not change them. A string whose `data` is **NULL** is dead: freed, or one whose initialization failed. A `size` of **0** with `data` set marks a read-only string from `sof_string_const()` or `sof_string_view()`. `data` can move whenever the string grows, so pointers into it are good only until the next append or insert; deleting and clearing don't move it.

## Threading

The library has no global state and does no locking. Different strings can be used on different threads freely.

`sof_string_cmp()`, `sof_string_casecmp()`, `sof_string_cstr()`, `sof_string_position()`, `sof_string_dup()` and `sof_string_substr()` only read their source, and `sof_string_from_bytes()` and `sof_string_view()` only read their bytes, so any number of threads can do these at once on one string, with a readers-writer lock if another thread changes it. `sof_string_append()`, `sof_string_append_bytes()`, `sof_string_append_cstr()`, `sof_string_append_char()`, `sof_string_reserve()`, `sof_string_commit()`, `sof_string_insert()`, `sof_string_insert_bytes()`, `sof_string_insert_cstr()`, `sof_string_delete()`, `sof_string_clear()`, `sof_string_tolower()`, `sof_string_toupper()` and `sof_string_free()` change the string and need the write lock; since an append or insert can move the data, a reader must not keep a pointer into it once it lets go of the lock.
