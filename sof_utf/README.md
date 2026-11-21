# sof_utf

UTF-8, UTF-16 and UTF-32 encoding, decoding, validation and iteration in c.

The library works on Unicode code points, held in `uint32_t`, and on strings in the three encodings, passed with their length, so there is nothing to set up or free. `sof_utf_valid()` says whether a number is a code point that can be encoded. For UTF-8, `sof_utf8_size()` gives the bytes a code point takes, `sof_utf8_encode()` writes them and `sof_utf8_decode()` reads one code point back. `sof_utf8_next()` steps through a string one code point at a time, `sof_utf8_count()` counts its code points, `sof_utf8_validate()` finds its first error and `sof_utf8_ascii()` gives the length of the ASCII it starts with. The `sof_utf16_` functions do the same for UTF-16. UTF-32 holds one code point in each unit, so it only has `sof_utf32_validate()`. For each encoding there are two functions that convert a whole string to the other two, such as `sof_utf8_to_utf16()` and `sof_utf8_to_utf32()`.

UTF-8 strings are bytes, passed as `const void *` with their size in bytes, so `char` and `unsigned char` buffers both work and NUL is an ordinary character. UTF-16 and UTF-32 strings are arrays of `uint16_t` and `uint32_t` in the machine's byte order, with their length in units. Data read from a file in a fixed byte order, such as UTF-16LE, has to be put in the machine's order first. Nothing is NUL-terminated, and nothing is allocated: output goes to a buffer the caller provides, and a conversion returns the size the whole result needs, so it can be called first with a **NULL** buffer to measure.

Decoding is strict, as the Unicode standard requires, and reports two kinds of error. **SOF_UTF_INVALID** is for data that can't be valid: overlong forms, encoded surrogates, values past U+10FFFF, stray continuation bytes, unpaired UTF-16 surrogates and UTF-32 units that aren't code points. **SOF_UTF_INCOMPLETE** is for a sequence that is valid so far but cut off by the end of the data, which matters when data arrives in pieces. Each error covers the bytes the standard's "maximal subpart" practice says it should, so replacing each one with U+FFFD, as the conversions do, gives the same text as other conforming decoders.

ASCII is passed over 8 bytes at a time: a word read from the string with no high bit set is eight code points. `sof_utf8_validate()`, `sof_utf8_count()`, `sof_utf8_to_utf16()` and `sof_utf8_to_utf32()` do this for every run of ASCII (for `sof_utf8_validate()`, after any ASCII byte; for the others, after two in a row, so a single space between words in another script costs nothing extra), and check or decode other sequences in place, so mostly-ASCII text such as English goes 2 to 10 times as fast as a code point at a time. The words are read with `memcpy()`, so the data needs no alignment, and never past _size_. The conversions still stop writing at the first code point that doesn't fit.

Everything is in _sof_utf.h_, as `static inline` functions, so there is no _sof_utf.c_. The library's demo is the [utfconv](demo/) program in _demo/_, which converts text between UTF-8, UTF-16 and UTF-32 in either byte order with the library's conversions, or finds and reports each error in it. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). They use the [sof_bit](../sof_bit/) library in _../sof_bit/_ (_sof_bit.h_) for a bitmap; the library itself doesn't. The library needs nothing beyond the c standard library.

    const char *s = "na\xc3\xafve \xe2\x82\xac";       // "naïve €" in UTF-8
    size_t size = strlen(s);
    if (sof_utf8_validate(s, size) != size) { ... }    // not valid UTF-8
    size_t n = sof_utf8_count(s, size);                // 7 code points
    size_t position = 0;
    uint32_t code;
    while (sof_utf8_next(s, size, &position, &code)) {
        ...                                            // 'n', 'a', 0xef, ...
    }
    uint16_t units[16];
    size_t count = sof_utf8_to_utf16(s, size, units, 16);   // 7 units
    unsigned char bytes[SOF_UTF8_SIZE_MAX];
    size_t length = sof_utf8_encode(0x1f600, bytes, sizeof(bytes));   // 4

## Building

    make             # checks that sof_utf.h compiles on its own and builds ./test and the demo, demo/utfconv
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

The tests encode and decode every code point in UTF-8 and UTF-16, and decode every sequence of one to three bytes, checking each against the set of valid UTF-8 prefixes built from a reference encoder in _test.c_, and a million longer ones at random. Random strings are converted between all three encodings, into buffers of every size too small as well as large enough. `sof_utf8_ascii()`, `sof_utf8_validate()`, `sof_utf8_count()`, `sof_utf8_to_utf16()` and `sof_utf8_to_utf32()` are checked against versions in _test.c_ that decode one sequence at a time with a reference decoder, on random text made of runs of ASCII, random and edge code points and every kind of error (overlong forms, surrogates, values past U+10FFFF, bytes that can't start a sequence, stray continuation bytes and sequences cut short), placed after 0 to 17 bytes of ASCII so they fall on both sides of each 8-byte word, at every alignment, cut off at every length, and converted with no buffer, a buffer large enough, one unit too small, empty and of random size, checking that nothing past the room is written.

To use the library in another project, copy _sof_utf.h_ into it and include it. To build its tests there too, copy _test.c_, _test.h_ from _../test/_ and _sof_bit.h_ from _../sof_bit/_, and compile _test.c_ with _-pthread_.

## Errors

Decoding gives **SOF_UTF_INVALID** or **SOF_UTF_INCOMPLETE** in place of a code point. Both are above **SOF_UTF_MAX**, so `sof_utf_valid()` rejects them, and a loop can test for either with `!sof_utf_valid(code)`.

For UTF-8, an invalid sequence covers its lead byte and the continuation bytes after it that were still valid, so `"\xe2\x82" "a"` is one error, then `'a'`, but `"\xe0\x80"` is two errors, since no code point starts with those two bytes. A byte that can't start a sequence (0x80 to 0xc1 and 0xf5 to 0xff) is an error of its own. For UTF-16, a low surrogate on its own or a high one not followed by a low one is an error of one unit. An incomplete sequence takes all the bytes or units that are left.

`sof_utf8_validate()` and `sof_utf16_validate()` count an incomplete sequence at the end as an error. Data read in pieces should be checked once it is joined, or decoded with `sof_utf8_decode()`, keeping the bytes of an incomplete sequence for the next piece.

The conversions write U+FFFD, **SOF_UTF_REPLACEMENT**, for each error, and the count functions count each error as one code point, so `sof_utf8_count()` gives the length the string has after conversion.

## Functions

`size_t sof_utf16_count(const uint16_t *data, size_t count)`

Return the number of code points in the _count_ units of UTF-16 at _data_, counting each error as one. Returns **0** if _data_ is **NULL**.

`size_t sof_utf16_decode(const uint16_t *data, size_t count, uint32_t *code)`

Decode the code point at the start of the _count_ units at _data_ and store it in _code_, unless _code_ is **NULL**. Stores **SOF_UTF_INVALID** for a lone low surrogate or a high surrogate not followed by a low one, and **SOF_UTF_INCOMPLETE** for a high surrogate that is the last unit, or when there are no units. Returns the units read: **1** or **2**, **1** for an error, or **0** if _data_ is **NULL** or _count_ is **0**.

`size_t sof_utf16_encode(uint32_t code, uint16_t *buffer, size_t count)`

Write _code_ in UTF-16 to _buffer_, which has room for _count_ units. Returns the units written, **1** or **2**, or **0**, writing nothing, if _code_ isn't a valid code point, _buffer_ is **NULL** or _count_ is too small.

`int sof_utf16_next(const uint16_t *data, size_t count, size_t *position, uint32_t *code)`

Decode the code point at unit _position_ of the _count_ units at _data_, as `sof_utf16_decode()` does, store it in _code_ unless that is **NULL**, and move _position_ past it. Returns **1** if a code point or an error was read, or **0**, changing nothing, if _position_ is at or past the end, or _data_ or _position_ is **NULL**.

`size_t sof_utf16_size(uint32_t code)`

Return the units _code_ takes in UTF-16: **1** below U+10000, **2** above, or **0** if _code_ isn't a valid code point.

`size_t sof_utf16_to_utf32(const uint16_t *data, size_t count, uint32_t *buffer, size_t size)`

Convert the _count_ units of UTF-16 at _data_ to UTF-32 in _buffer_, which has room for _size_ units, writing U+FFFD for each error. Writes as many code points as fit, and returns the number the whole string has, so a result above _size_ means it was cut short. With a **NULL** _buffer_, writes nothing. Returns **0** if _data_ is **NULL**.

`size_t sof_utf16_to_utf8(const uint16_t *data, size_t count, void *buffer, size_t size)`

Convert the _count_ units of UTF-16 at _data_ to UTF-8 in _buffer_, which has room for _size_ bytes, writing U+FFFD for each error. Writes the code points that fit whole, stopping at the first that doesn't, and returns the size of the whole string in UTF-8, so a result above _size_ means it was cut short. With a **NULL** _buffer_, writes nothing. Returns **0** if _data_ is **NULL**.

`size_t sof_utf16_validate(const uint16_t *data, size_t count)`

Return the position of the first error in the _count_ units at _data_, or _count_ if there is none. Returns **0** if _data_ is **NULL**.

`size_t sof_utf32_to_utf16(const uint32_t *data, size_t count, uint16_t *buffer, size_t size)`

Convert the _count_ units of UTF-32 at _data_ to UTF-16 in _buffer_, which has room for _size_ units, writing U+FFFD for each unit that isn't a code point. Writes the code points that fit whole and returns the length of the whole string in UTF-16, so a result above _size_ means it was cut short. With a **NULL** _buffer_, writes nothing. Returns **0** if _data_ is **NULL**.

`size_t sof_utf32_to_utf8(const uint32_t *data, size_t count, void *buffer, size_t size)`

Convert the _count_ units of UTF-32 at _data_ to UTF-8 in _buffer_, which has room for _size_ bytes, writing U+FFFD for each unit that isn't a code point. Writes the code points that fit whole and returns the size of the whole string in UTF-8, so a result above _size_ means it was cut short. With a **NULL** _buffer_, writes nothing. Returns **0** if _data_ is **NULL**.

`size_t sof_utf32_validate(const uint32_t *data, size_t count)`

Return the position of the first unit of the _count_ at _data_ that isn't a code point (a surrogate, or a value past U+10FFFF), or _count_ if there is none. Returns **0** if _data_ is **NULL**.

`size_t sof_utf8_ascii(const void *data, size_t size)`

Return the number of ASCII bytes (below 0x80) at the start of the _size_ bytes at _data_, or _size_ if they all are. Each is a code point of its own. The bytes are tested 8 at a time, so a long run is passed over quickly. Returns **0** if _data_ is **NULL**.

`size_t sof_utf8_count(const void *data, size_t size)`

Return the number of code points in the _size_ bytes of UTF-8 at _data_, counting each error as one. Returns **0** if _data_ is **NULL**.

`size_t sof_utf8_decode(const void *data, size_t size, uint32_t *code)`

Decode the code point at the start of the _size_ bytes at _data_ and store it in _code_, unless _code_ is **NULL**. Stores **SOF_UTF_INVALID** if the bytes aren't valid UTF-8, and **SOF_UTF_INCOMPLETE** if they start a valid sequence that the end of the data cuts off, or there are none. Returns the bytes read: **1** to **4** for a code point; for **SOF_UTF_INVALID**, **1** to **3**, the bytes to skip (see Errors); for **SOF_UTF_INCOMPLETE**, _size_; and **0** if _data_ is **NULL** or _size_ is **0**.

`size_t sof_utf8_encode(uint32_t code, void *buffer, size_t size)`

Write _code_ in UTF-8 to _buffer_, which has room for _size_ bytes. Returns the bytes written, **1** to **4**, or **0**, writing nothing, if _code_ isn't a valid code point, _buffer_ is **NULL** or _size_ is too small. **SOF_UTF8_SIZE_MAX** bytes are always enough.

`int sof_utf8_next(const void *data, size_t size, size_t *position, uint32_t *code)`

Decode the code point at byte _position_ of the _size_ bytes at _data_, as `sof_utf8_decode()` does, store it in _code_ unless that is **NULL**, and move _position_ past it. Returns **1** if a code point or an error was read, or **0**, changing nothing, if _position_ is at or past the end, or _data_ or _position_ is **NULL**.

`size_t sof_utf8_size(uint32_t code)`

Return the bytes _code_ takes in UTF-8: **1** below U+80, **2** below U+800, **3** below U+10000 and **4** above, or **0** if _code_ isn't a valid code point.

`size_t sof_utf8_to_utf16(const void *data, size_t size, uint16_t *buffer, size_t count)`

Convert the _size_ bytes of UTF-8 at _data_ to UTF-16 in _buffer_, which has room for _count_ units, writing U+FFFD for each error. Writes the code points that fit whole, stopping at the first that doesn't, so a surrogate pair is never split, and returns the length of the whole string in UTF-16, so a result above _count_ means it was cut short. With a **NULL** _buffer_, writes nothing. Returns **0** if _data_ is **NULL**.

`size_t sof_utf8_to_utf32(const void *data, size_t size, uint32_t *buffer, size_t count)`

Convert the _size_ bytes of UTF-8 at _data_ to UTF-32 in _buffer_, which has room for _count_ units, writing U+FFFD for each error. Writes as many code points as fit, and returns the number the whole string has, so a result above _count_ means it was cut short. With a **NULL** _buffer_, writes nothing. Returns **0** if _data_ is **NULL**.

`size_t sof_utf8_validate(const void *data, size_t size)`

Return the position of the first invalid or incomplete sequence in the _size_ bytes at _data_, or _size_ if there is none. Returns **0** if _data_ is **NULL**.

`int sof_utf_valid(uint32_t code)`

Return **1** if _code_ is a Unicode scalar value, a code point that can be encoded: at most U+10FFFF and not a surrogate (U+D800 to U+DFFF). Otherwise, including for **SOF_UTF_INVALID** and **SOF_UTF_INCOMPLETE**, returns **0**.

Every function treats **NULL** _data_ as an empty string, so counts, validations and conversions return **0**, decoding returns **0** and gives **SOF_UTF_INCOMPLETE**, and the `next` functions return **0**. A **NULL** _code_ is allowed wherever one is taken, and a **NULL** output _buffer_ makes a conversion measure without writing; encoding into a **NULL** _buffer_ returns **0**. Because a **NULL** string and an empty one both validate to **0**, a check of `sof_utf8_validate(data, size) == size` fails for **NULL** _data_ with a non-zero _size_.

## Types

    #define SOF_UTF_MAX ((uint32_t)0x10ffff)                  // the last code point
    #define SOF_UTF_REPLACEMENT ((uint32_t)0xfffd)            // U+FFFD
    #define SOF_UTF_INVALID ((uint32_t)0xffffffff)            // decoded from invalid data
    #define SOF_UTF_INCOMPLETE ((uint32_t)0xfffffffe)         // decoded from a cut-off sequence
    #define SOF_UTF8_SIZE_MAX 4                               // bytes in a code point, at most
    #define SOF_UTF16_SIZE_MAX 2                              // units in a code point, at most

Code points are plain `uint32_t` values, and the library has no types of its own. **SOF_UTF8_SIZE_MAX** and **SOF_UTF16_SIZE_MAX** can size buffers for one code point; a whole string needs at most 4 bytes or 2 units per code point, or what a conversion with a **NULL** buffer returns.

## Threading

The library has no state, and every function reads only its arguments and writes only the buffer, _code_ and _position_ it is given, so any number of threads can call any of them at once, as long as no thread writes a string another is reading.
