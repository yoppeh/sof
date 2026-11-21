# sof_base

Base16, base32 and base64 encoding and decoding in c.

The library implements the encodings of RFC 4648: base16 (hex), base32 and base32hex, and base64 and base64url, each named by a `sof_base_encoding_t`. It is a utility library with no main struct: `sof_base_encoded_size()` gives the characters some bytes encode to, and `sof_base_encode()` writes them into the caller's buffer; `sof_base_decoded_size()` gives the most bytes some characters decode to, and `sof_base_decode()` decodes them into the caller's buffer, reporting bad input with the offset of the first bad character, apart from a buffer that is too small. Data that comes in pieces, such as a file read a buffer at a time, is encoded with a `sof_base_encoder_t`, which the application provides and sets up with `sof_base_encoder_init()`, gives each piece with `sof_base_encoder_update()` and ends with `sof_base_encoder_final()`, and decoded the same way with a `sof_base_decoder_t`, `sof_base_decoder_init()`, `sof_base_decoder_update()` and `sof_base_decoder_final()`; the result is the same however the input is split. Flags choose upper or lower case and padding or none, and whether decoding is strict, accepting only the text encoding gives, or lenient, and whether it skips whitespace (see Flags). The same flags work in both directions, so text encoded with some flags always decodes with them.

Whole groups (3 bytes and 4 characters in base64, 5 and 8 in base32, 1 and 2 in base16) are encoded and decoded a group at a time, in a loop for each group size, with a 256-entry table for decoding that marks whitespace, padding and every character outside the alphabet with high bits, so one test checks a whole group. Anything else, from whitespace to padding to a group split between pieces, is handled a character at a time. Bytes are read and written one at a time, so the data's alignment doesn't matter, and nothing is read past the end of the input or written past the size given. Nothing is allocated.

The library's demo is the [basenc](demo/) program in _demo/_, which encodes and decodes files in any of the encodings, like GNU's `basenc`, with whose output it agrees. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). The benchmark is _bench.c_, which compares the library with simple versions that work a byte or a character at a time. All declarations are in _sof_base.h_. The library needs nothing beyond the c standard library; the benchmark also uses the [sof_alloc](../sof_alloc/), [sof_copt](../sof_copt/), [sof_random](../sof_random/) and [sof_timer](../sof_timer/) libraries. It doesn't append to a [sof_string](../sof_string/) itself, which would make every program that uses it build sof_string and sof_alloc for two lines it can write itself, as below.

    char text[64];
    size_t n;
    sof_base_encode(SOF_BASE_64, 0, "foobar", 6, text, sizeof(text), &n);     // "Zm9vYmFy", n is 8, no NUL

    unsigned char data[64];
    size_t offset;
    if (sof_base_decode(SOF_BASE_64, SOF_BASE_SKIP_SPACE, pem, length, data, sizeof(data), &n, &offset)
        == SOF_BASE_BAD_INPUT) {
        printf("bad character at %zu\n", offset);
    }

    size_t size = sof_base_encoded_size(SOF_BASE_32, SOF_BASE_LOWER, length);
    char *end = sof_string_reserve(&s, size);                                  // append to a sof_string_t
    sof_base_encode(SOF_BASE_32, SOF_BASE_LOWER, bytes, length, end, size, &n);
    sof_string_commit(&s, n);

    sof_base_decoder_t decoder;
    sof_base_decoder_init(&decoder, SOF_BASE_64URL, SOF_BASE_NO_PAD);
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (sof_base_decoder_update(&decoder, chunk, got, out, sizeof(out), &n, &offset) != SOF_BASE_OK) {
            break;                                                             // offset counts every piece
        }
        fwrite(out, 1, n, stdout);
    }
    sof_base_decoder_final(&decoder, out, sizeof(out), &n, &offset);          // the last group, and checks the end

## Building

    make             # compiles the library and builds ./test, ./bench and the demo, demo/basenc
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization
    ./bench          # runs the benchmark

The tests check the test vectors of RFC 4648, section 10, in every encoding, with and without padding and in lower case, and bytes whose base64 and base64url differ. They check every byte value in every position of a group of every encoding, which must decode, in either case where the encoding has case, to its place in the alphabet, or fail at its offset; that every two-character last group of base32 and base64 that decodes strictly encodes back to itself, so exactly 256 do, and that leniently all of them decode. They encode random data of every length from 0 to 300 bytes, at 8 alignments, in every encoding with every combination of `SOF_BASE_LOWER` and `SOF_BASE_NO_PAD`, comparing the text with a plain encoder in _test.c_ that works a bit at a time, and decode it back strictly, leniently, and with spaces and line breaks added, from buffers allocated to the exact size so the sanitizers catch any read or write past them. The same data is encoded and decoded by the streams in random pieces and a character at a time. Some 60 bad texts fail at the offset expected, at once and in pieces: characters outside the alphabet, NUL and bytes over 127 among them, too few characters to make a byte, padding missing, short, too long, misplaced or followed by data, bits left over that aren't 0, and whitespace when it isn't skipped. Buffers one too small fail with the size needed, writing nothing when encoding, streams one too small change nothing, and the exact size works. Every **NULL** argument, bad encodings and flags, and streams that are dead because their init failed or they met bad input are checked too. The tests are linked with _-pthread_, like every library's; the library itself doesn't use threads.

To use the library in another project, copy _sof_base.c_ and _sof_base.h_ into it and compile _sof_base.c_ with the project's own sources. To build its tests there too, copy _test.c_, and _test.h_ and _test.mk_ from _../test/_.

## Benchmark

    bench [-h] [-s megabytes]

The benchmark encodes and decodes random data in every encoding with `sof_base_encode()` and `sof_base_decode()`, strictly, and with simple versions that work a byte or a character at a time: the encoder shifts each byte into an accumulator and takes characters out of it, and the decoder finds each character's value by comparing it with the alphabet's ranges, as code written for one use usually does. It checks that each gives the data back, and reports megabytes (10^6 bytes) of data a second, for decoding too, so a decoding rate is of the bytes it gives, not the characters it reads. Each rate is the best of seven runs.

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-s` _megabytes_: the size of the data, from 1 to 4096; 4 if not given.

On an Apple M4 Max with Apple clang 21, for 4 MB of data:

| | encode | simple encode | decode | simple decode |
|---|---|---|---|---|
| base16 | 2563 | 1062 | 2808 | 173 |
| base32 | 3477 | 1238 | 3182 | 301 |
| base32hex | 3472 | 1237 | 3178 | 223 |
| base64 | 4149 | 1387 | 3747 | 180 |
| base64url | 4147 | 1387 | 3769 | 178 |

On an Intel i9-13900K with gcc 16, Arch Linux:

| | encode | simple encode | decode | simple decode |
|---|---|---|---|---|
| base16 | 2887 | 1156 | 2652 | 198 |
| base32 | 3308 | 1313 | 2913 | 342 |
| base32hex | 3250 | 1313 | 2964 | 225 |
| base64 | 3997 | 1444 | 3648 | 185 |
| base64url | 3979 | 1445 | 3648 | 187 |

On an AMD Ryzen 9 5950X with gcc 16, Arch Linux:

| | encode | simple encode | decode | simple decode |
|---|---|---|---|---|
| base16 | 2184 | 936 | 1625 | 187 |
| base32 | 2378 | 1077 | 1623 | 380 |
| base32hex | 2373 | 1076 | 1625 | 253 |
| base64 | 2847 | 1210 | 2254 | 178 |
| base64url | 2842 | 1204 | 2258 | 177 |

The library encodes two to three times as fast as the simple encoder, which takes one character at a time out of its accumulator, in a loop that branches on how many bits it holds. It decodes four to twenty times as fast as the simple decoder, whose comparisons branch on each character and, on random data, guess wrong about half the time, while a table gives every character's value without a branch, and a group needs one test. Base64 is fastest per byte of data because each character carries the most bits; base16, with a character for every four bits, is the slowest or close to it.

## Flags

The flags, or'ed together in an `unsigned`, choose how text is written and read. Each direction uses those that concern it and ignores the rest, so one set can be given to both.

- **SOF_BASE_LOWER**: encode base16, base32 and base32hex in lower case. Without it they are written in upper case, the RFC's alphabets. Base64 ignores it, and decoding always takes letters of base16, base32 and base32hex in either case, even mixed.
- **SOF_BASE_NO_PAD**: encode without the `=` that pad a short last group of base32, base32hex, base64 and base64url to a whole group, as JSON Web Tokens and many URLs do; and when decoding strictly, refuse padding. Base16 has none.
- **SOF_BASE_LENIENT**: decode leniently. Padding may be missing, or only part of it given, and the bits of the last character that don't make a whole byte are ignored, rather than having to be 0. Padding, if any, must still come at the end, after a group that can end the data, and no more of it than the group needs.
- **SOF_BASE_SKIP_SPACE**: decode skipping ASCII whitespace (space, tab, newline, vertical tab, form feed and carriage return) wherever it comes, as in the line breaks of PEM and MIME bodies, and between `=`s.

With no flags, encoding writes the RFC's alphabet in upper case, padded, and decoding is strict: the text must be exactly what encoding with the same flags writes, apart from the case of letters. That is, it must be padded (or, with `SOF_BASE_NO_PAD`, not), have no whitespace and nothing after the padding, and have 0 in the leftover bits of its last character, so each byte string has a single encoding. Every encoding refuses a last group with too few characters to make a byte, such as one base64 character, or a number no bytes encode to, such as three base32 characters.

## Functions

`size_t sof_base_encoded_size(sof_base_encoding_t encoding, unsigned flags, size_t length)`

Return the characters _length_ bytes encode to in _encoding_, padded unless _flags_ has `SOF_BASE_NO_PAD`, the only flag it looks at, and not counting a NUL. Return **0** if _encoding_ isn't a `sof_base_encoding_t`, or **SIZE_MAX** if the number is too large for a `size_t`.

`sof_base_error_t sof_base_encode(sof_base_encoding_t encoding, unsigned flags, const void *data, size_t length, char *text, size_t size, size_t *written)`

Encode the _length_ bytes at _data_ in _encoding_, as _flags_ say, into _text_, which has room for _size_ characters. Exactly `sof_base_encoded_size()` characters are written, and no NUL. Set *_written_, if _written_ isn't **NULL**, to the characters written. Return **SOF_BASE_OK**; **SOF_BASE_TOO_SMALL**, writing nothing and setting *_written_ to the characters needed, if _size_ is smaller; or **SOF_BASE_ERROR**, setting *_written_ to **0**, if _encoding_ isn't a `sof_base_encoding_t` or _flags_ has a bit that isn't a flag. **NULL** _data_ is taken as no bytes, whatever _length_ is, and **NULL** _text_ as a buffer of no room, whatever _size_ is, so a call with **NULL** _text_ measures the text.

`size_t sof_base_decoded_size(sof_base_encoding_t encoding, size_t length)`

Return the most bytes _length_ characters can decode to in _encoding_: as many as there would be if all were data. That is exact for text without padding or whitespace, and otherwise a little more, up to two bytes for base64's padding and four for base32's. Return **0** if _encoding_ isn't a `sof_base_encoding_t`. A buffer of this size never fails for lack of room.

`sof_base_error_t sof_base_decode(sof_base_encoding_t encoding, unsigned flags, const char *text, size_t length, void *data, size_t size, size_t *written, size_t *offset)`

Decode the _length_ characters at _text_, which need not end with a NUL, from _encoding_, checking them as _flags_ say, into _data_, which has room for _size_ bytes. Set *_written_, if _written_ isn't **NULL**, to the bytes written. Return **SOF_BASE_OK**; **SOF_BASE_BAD_INPUT**, setting *_written_ to **0** and *_offset_, if _offset_ isn't **NULL**, to the offset of the first character at fault, or to _length_ if the text ends too soon, part way through a group or before padding it needs; **SOF_BASE_TOO_SMALL**, setting *_written_ to the bytes needed, if _size_ is smaller and the text is valid; or **SOF_BASE_ERROR**, setting *_written_ to **0**, if _encoding_ or _flags_ is bad. A bad character is found even past where the buffer ran out, so **SOF_BASE_BAD_INPUT** comes before **SOF_BASE_TOO_SMALL**. *_offset_ is changed only on **SOF_BASE_BAD_INPUT**. On an error, _data_ may hold part of the result. When a bit left over in the last character isn't 0, in strict decoding, the offset is that character's. **NULL** _text_ is taken as no characters, whatever _length_ is, and **NULL** _data_ as a buffer of no room, whatever _size_ is, so a call with **NULL** _data_ checks the text and measures the result.

`sof_base_error_t sof_base_encoder_init(sof_base_encoder_t *encoder, sof_base_encoding_t encoding, unsigned flags)`

Set up _encoder_ to encode data given to it in pieces, in _encoding_, as _flags_ say. Return **SOF_BASE_OK**, or **SOF_BASE_ERROR** if _encoder_ is **NULL**, or _encoding_ or _flags_ is bad, which leaves the stream dead. A stream allocates nothing, so there is no function to free one.

`sof_base_error_t sof_base_encoder_update(sof_base_encoder_t *encoder, const void *data, size_t length, char *text, size_t size, size_t *written)`

Encode the next _length_ bytes at _data_ into _text_, which has room for _size_ characters. Every group the bytes held and these complete is encoded, and the bytes of one they don't, fewer than a group, are held for the next piece. Pieces can be of any length, **0** included, and the text doesn't depend on how the data is split. At most `sof_base_encoded_size(encoding, flags, length + SOF_BASE_PENDING_MAX)` characters are written. Set *_written_, if _written_ isn't **NULL**, to the characters written. Return **SOF_BASE_OK**; **SOF_BASE_TOO_SMALL**, changing nothing and setting *_written_ to the characters needed, if _size_ is smaller; or **SOF_BASE_ERROR**, setting *_written_ to **0**, if _encoder_ is **NULL** or dead. **NULL** _data_ is no bytes and **NULL** _text_ no room, as for `sof_base_encode()`.

`sof_base_error_t sof_base_encoder_final(sof_base_encoder_t *encoder, char *text, size_t size, size_t *written)`

Encode the bytes held, and any padding, into _text_, which has room for _size_ characters: at most 8 are written, a whole group. The stream is then ready for new data, with the same encoding and flags. Set *_written_ and return as `sof_base_encoder_update()` does.

`sof_base_error_t sof_base_decoder_init(sof_base_decoder_t *decoder, sof_base_encoding_t encoding, unsigned flags)`

Set up _decoder_ to decode text given to it in pieces, from _encoding_, checking it as _flags_ say. Return **SOF_BASE_OK**, or **SOF_BASE_ERROR** if _decoder_ is **NULL**, or _encoding_ or _flags_ is bad, which leaves the stream dead.

`sof_base_error_t sof_base_decoder_update(sof_base_decoder_t *decoder, const char *text, size_t length, void *data, size_t size, size_t *written, size_t *offset)`

Decode the next _length_ characters at _text_ into _data_, which has room for _size_ bytes. Every group the characters held and these complete is decoded, as is a short group that padding ends, and the characters of one they don't are held for the next piece. Pieces can be of any length, **0** included, and split the text anywhere, between `=`s too; the bytes don't depend on how. Bad characters are reported as soon as they are given. _data_ must have room for `sof_base_decoded_size(encoding, decoder->pending + length)` bytes, as if every character held and given were data, even where fewer are written; a buffer of `sof_base_decoded_size(encoding, length + SOF_BASE_PENDING_MAX)` always has. Set *_written_, if _written_ isn't **NULL**, to the bytes written. Return **SOF_BASE_OK**; **SOF_BASE_BAD_INPUT**, setting *_written_ to **0** and *_offset_, if _offset_ isn't **NULL**, to the offset of the bad character in the whole text, counting every piece, which leaves the stream dead; **SOF_BASE_TOO_SMALL**, changing nothing and setting *_written_ to the room needed, if _size_ is smaller; or **SOF_BASE_ERROR**, setting *_written_ to **0**, if _decoder_ is **NULL** or dead. **NULL** _text_ is no characters and **NULL** _data_ no room, as for `sof_base_decode()`.

`sof_base_error_t sof_base_decoder_final(sof_base_decoder_t *decoder, void *data, size_t size, size_t *written, size_t *offset)`

End the text: check that it ended where it may, after a whole group, after the padding a group needs, or, without padding where the flags allow, part way through a group with enough characters to make a byte, and decode that group into _data_, which has room for _size_ bytes: at most 4 are written. The stream is then ready for new text, with the same encoding and flags. Set *_written_ and *_offset_ and return as `sof_base_decoder_update()` does; on **SOF_BASE_BAD_INPUT**, *_offset_ is the length of the whole text if it ended too soon, or the last character's offset if its leftover bits aren't 0, and the stream is left dead.

Every function takes **NULL** for any pointer, as above, and for the out-parameters _written_ and _offset_. A stream whose init failed, or that met bad input, is dead: its `encoding` is **0**, and every call but init returns **SOF_BASE_ERROR** until it is set up again. **SOF_BASE_NO_MEM** and **SOF_BASE_NO_MATCH** are never returned; they are in the enum to keep the collection's order of error codes.

## Types

    typedef enum sof_base_encoding_t {
        SOF_BASE_16 = 1,                // 0-9 A-F, a byte to 2 characters
        SOF_BASE_32,                    // A-Z 2-7, 5 bytes to 8 characters
        SOF_BASE_32HEX,                 // 0-9 A-V, which sorts as the bytes do
        SOF_BASE_64,                    // A-Z a-z 0-9 + /, 3 bytes to 4 characters
        SOF_BASE_64URL,                 // A-Z a-z 0-9 - _, safe in URLs and file names
    } sof_base_encoding_t;

The encodings. **0** is none, so a zeroed stream is dead.

    typedef enum sof_base_flag_t {
        SOF_BASE_LOWER = 0x1,
        SOF_BASE_NO_PAD = 0x2,
        SOF_BASE_LENIENT = 0x4,
        SOF_BASE_SKIP_SPACE = 0x8,
    } sof_base_flag_t;

The flags, described under Flags. Functions take them as an `unsigned`, or'ed together.

    typedef enum sof_base_error_t {
        SOF_BASE_OK = 0,
        SOF_BASE_NO_MEM,                // never returned
        SOF_BASE_NO_MATCH,              // never returned
        SOF_BASE_ERROR,                 // a NULL or dead stream, or a bad encoding or flag
        SOF_BASE_BAD_INPUT,             // the text isn't valid
        SOF_BASE_TOO_SMALL,             // the buffer is too small
    } sof_base_error_t;

The error codes.

    typedef struct sof_base_encoder_t {
        sof_base_encoding_t encoding;   // 0 if not set up
        unsigned flags;
        size_t pending;                 // bytes held, fewer than a group
        unsigned char bytes[5];         // the start of a group
    } sof_base_encoder_t;

    typedef struct sof_base_decoder_t {
        sof_base_encoding_t encoding;   // 0 if not set up or failed
        unsigned flags;
        size_t offset;                  // characters given so far
        size_t pending;                 // data characters held, fewer than a group
        uint64_t bits;                  // their values
        size_t last;                    // the offset of the last data character
        size_t padding;                 // '=' given so far
        size_t padding_needed;          // '=' the last group needs
    } sof_base_decoder_t;

The state of an encoding or a decoding that comes in pieces. The application provides it, anywhere, and sets it up with its init; it may read the fields, `pending` and `offset` in particular, but must not change them. It holds no pointers and allocates nothing, so it can be copied, which forks the stream, and needs no freeing. It holds part of the data, so treat it as the data would be treated, and clear it when that matters.

## Threading

The library has no global state, and its tables are constant. `sof_base_encode()`, `sof_base_decode()` and the size functions can be called by any number of threads at once. A stream belongs to whoever is giving it data: every stream function changes it, so it needs to be used by one thread at a time, but separate streams can be used on separate threads freely. No function only reads a stream.
