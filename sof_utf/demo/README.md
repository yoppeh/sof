# utfconv

A Unicode encoding converter in c: text from UTF-8, UTF-16 or UTF-32 to any of them, in either byte order, or a check of where text isn't valid.

`utfconv` reads files, or standard input, in the encoding given with `-f`, UTF-8 by default, and writes them to standard output in the encoding given with `-t`, UTF-8 by default, with each error in the input written as U+FFFD, the replacement character. UTF-16 and UTF-32 can be big-endian or little-endian, or, as _utf-16_ and _utf-32_, take their order from a byte order mark and write one. With `-d`, a byte order mark at the start of the input decides its encoding, and with `-b` one is written at the start of the output. With `-c` it converts nothing, but prints the line and byte of each error in the input, and how many code points each file has.

_main.c_ is the whole program. Each file is read into a [sof_string](../../sof_string/), and UTF-16 and UTF-32 are put together from its bytes, in the order of their encoding, into arrays of `uint16_t` or `uint32_t` in the machine's order, which is what sof_utf takes. A file is converted by sof_utf's conversion for the pair of encodings, such as `sof_utf8_to_utf16()` or `sof_utf32_to_utf8()`, called first without a buffer to measure the output, then with one; text converted to its own encoding goes through UTF-32, with `sof_utf8_to_utf32()` or `sof_utf16_to_utf32()` and back, which replaces its errors. The units are then put in the output's byte order. Errors are found with `sof_utf8_validate()`, `sof_utf16_validate()` and `sof_utf32_validate()`, which give the position of the next one, and measured with `sof_utf8_decode()` and `sof_utf16_decode()`, which give the bytes or units it covers, the same that the conversions replace with one U+FFFD; bytes after the last whole unit of UTF-16 or UTF-32 are one error more. Code points are counted with `sof_utf8_count()` and `sof_utf16_count()`, which count each error as one, as it is after conversion, and the one U+FFFD after the last unit and the byte order mark are written with `sof_utf8_encode()` and `sof_utf16_encode()`.

Encoding, decoding and conversion are done by the [sof_utf](../../sof_utf/) library in _../../sof_utf/_ (_sof_utf.h_, which is all of it), and the input is read into a string from the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), which also matches the names of encodings, ignoring case. Memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    size_t n = sof_utf8_to_utf16(bytes, size, NULL, 0);     // the units the whole text needs
    uint16_t *units = sof_alloc_calloc(NULL, n, sizeof(uint16_t));
    sof_utf8_to_utf16(bytes, size, units, n);                // each error written as U+FFFD
    size_t bad = sof_utf8_validate(bytes, size);             // size if the text is valid
    uint32_t code;
    size_t used = sof_utf8_decode(bytes + bad, size - bad, &code);   // the bytes the error covers

## Building

    make            # builds ./utfconv
    make debug=1    # builds with debug info and without optimization
    ./utfconv -t utf-16le file.txt > file16.txt   # UTF-8 to UTF-16, little-endian
    ./utfconv -c -f utf-16le file16.txt           # checks it, and counts its code points

`make` in the library's directory, _../_, builds it with the library, as do `make sof_utf` and `make` from the root of the project.

To build it in another project, copy _main.c_, with _sof_utf.h_ from _../../sof_utf/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it.

## Usage

    utfconv [-h] [-c] [-d] [-b] [-f encoding] [-t encoding] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-c`: check the input instead of converting it: print each error, with its line and byte (see below), then for each file, its encoding and how many code points it has, and how many errors.
- `-d`: if the input starts with a byte order mark, U+FEFF, let it decide the encoding, over `-f`, and skip it. The marks are tried longest first, so UTF-16LE text whose first character after the mark is U+0000 reads as UTF-32LE, as with other programs that go by the mark.
- `-b`: write a byte order mark at the start of the output, in its encoding: the bytes _ef bb bf_ for UTF-8.
- `-f` _encoding_: the input's encoding (default _utf-8_).
- `-t` _encoding_: the output's encoding (default _utf-8_).
- _file_: the text to read. With no _file_, or `-`, it reads standard input. With several, each is converted in turn, to the one output, or checked in turn.

The encodings are _utf-8_, _utf-16be_, _utf-16le_, _utf-32be_ and _utf-32le_, in either case, and _utf-16_ and _utf-32_, which, as the Unicode standard defines them, are big-endian unless the input starts with a byte order mark of their width in the other order, which is skipped, and are written big-endian with a mark. A byte order mark that doesn't decide the encoding is text, U+FEFF, and is converted like any other character.

The input is read whole into memory, so a file can be as large as memory allows. Each error becomes one U+FFFD, as sof_utf's conversions decide, which follows the Unicode standard's practice for how many bytes an error covers. With `-c`, an error's line counts from 1, by the newlines, U+000A, before it, and its byte from 0, from the start of the file, byte order mark included. Errors are printed on standard output, the line for each file after them; when converting, the number of errors in a file is printed on standard error.

`utfconv` exits with **0** if the input was valid, **1** if it had errors (which, when converting, are written as U+FFFD), and **2** if the options are wrong, a file can't be read, the output can't be written, or memory runs out.

For example, text converted to UTF-16, which is written big-endian, after a byte order mark:

    $ printf 'na\xc3\xafve \xe2\x82\xac \xf0\x9f\x98\x80\n' | ./utfconv -t utf-16 | xxd
    00000000: feff 006e 0061 00ef 0076 0065 0020 20ac  ...n.a...v.e.  .
    00000010: 0020 d83d de00 000a                      . .=....

and on to UTF-32LE, the mark deciding the order and skipped:

    $ printf 'na\xc3\xafve \xe2\x82\xac \xf0\x9f\x98\x80\n' | ./utfconv -t utf-16 | ./utfconv -f utf-16 -t utf-32le | xxd
    00000000: 6e00 0000 6100 0000 ef00 0000 7600 0000  n...a.......v...
    00000010: 6500 0000 2000 0000 ac20 0000 2000 0000  e... .... .. ...
    00000020: 00f6 0100 0a00 0000                      ........

text with a mark checked, its encoding found from the mark:

    $ printf 'na\xc3\xafve \xe2\x82\xac \xf0\x9f\x98\x80\n' | ./utfconv -t utf-32le -b | ./utfconv -d -c
    standard input: valid UTF-32LE after a byte order mark, 10 code points

UTF-8 with a bad continuation byte, an encoded surrogate, which is three errors, and a sequence cut off by the end:

    $ printf 'one\ntwo \xc3\x28\nthree \xed\xa0\x80 four \xe2\x82' | ./utfconv -c
    standard input: line 2, byte 8: invalid UTF-8: c3
    standard input: line 3, byte 17: invalid UTF-8: ed
    standard input: line 3, byte 18: invalid UTF-8: a0
    standard input: line 3, byte 19: invalid UTF-8: 80
    standard input: line 3, byte 26: UTF-8 sequence cut off by the end: e2 82
    standard input: UTF-8, 27 code points, 5 errors

and the same text converted, each error written as U+FFFD (_ef bf bd_):

    $ printf 'one\ntwo \xc3\x28\nthree \xed\xa0\x80 four \xe2\x82' | ./utfconv | xxd
    standard input: 5 errors in the UTF-8 written as U+FFFD
    00000000: 6f6e 650a 7477 6f20 efbf bd28 0a74 6872  one.two ...(.thr
    00000010: 6565 20ef bfbd efbf bdef bfbd 2066 6f75  ee ......... fou
    00000020: 7220 efbf bd                             r ...

The complete works of Shakespeare (see _../../test-data/README.md_) come back unchanged from UTF-16 and UTF-32:

    $ ./utfconv -c ../../test-data/shakespeare.txt
    ../../test-data/shakespeare.txt: valid UTF-8, 5575100 code points
    $ ./utfconv -t utf-16 ../../test-data/shakespeare.txt | ./utfconv -f utf-16 -t utf-32le | ./utfconv -f utf-32le | cmp - ../../test-data/shakespeare.txt && echo same
    same
