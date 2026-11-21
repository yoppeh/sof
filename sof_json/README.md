# sof_json

JSON in c: a streaming writer, a streaming reader and a document tree, for JSON and JSON Lines (JSONL).

The library's types and functions share the `sof_json_` prefix and one error enum, `sof_json_error_t`. It has three parts, each a type the application provides (on the stack, inside another structure or on the heap) and sets up with an init function.

The writer, `sof_json_writer_t` in _sof_json_writer.c_, writes JSON one piece at a time. `sof_json_writer_init()` sets it up with the indentation, flags, an optional write function and an optional allocator. `sof_json_writer_object_begin()` and `sof_json_writer_object_end()` write an object, and `sof_json_writer_key()` or `sof_json_writer_key_bytes()` each member's key; `sof_json_writer_array_begin()` and `sof_json_writer_array_end()` write an array. `sof_json_writer_string()`, `sof_json_writer_string_bytes()`, `sof_json_writer_int()`, `sof_json_writer_uint()`, `sof_json_writer_double()`, `sof_json_writer_bool()` and `sof_json_writer_null()` write values, `sof_json_writer_raw()` writes a value that is already JSON, and `sof_json_writer_value()` writes a value from a document. `sof_json_writer_output()` returns what is in the buffer, `sof_json_writer_flush()` passes it to the write function, `sof_json_writer_depth()` says how many objects and arrays are open and `sof_json_writer_complete()` whether the output is whole. `sof_json_writer_clear()` starts again, empty, and `sof_json_writer_free()` releases the memory once the application is finished.

The reader, `sof_json_reader_t` in _sof_json_reader.c_, takes JSON apart a token at a time. `sof_json_reader_init()` sets it up to pull input through a read function, such as `sof_json_read_file()` for a `FILE *`, and `sof_json_reader_init_bytes()` to read bytes in memory. `sof_json_reader_next()` returns the next token: where an object or array begins or ends, a member's key, or a value, with strings decoded, and its `span`, where it is in the input. `sof_json_reader_peek()` says what the next token is without reading it, `sof_json_reader_skip()` skips the value just begun, and `sof_json_reader_depth()` says how many objects and arrays are open. `sof_json_number_double()`, `sof_json_number_int()` and `sof_json_number_uint()` convert a number's text. `sof_json_reader_clear()` starts again, and `sof_json_reader_free()` releases the memory.

The document, `sof_json_doc_t` in _sof_json_doc.c_, holds a value read whole, as a tree of `sof_json_value_t`. `sof_json_doc_init()` sets it up, `sof_json_doc_parse()` parses a JSON text into it, and `sof_json_doc_read()` reads the next value from a reader into it, so a series of values, or the elements of a large array, can be read one at a time. Its value is `root`. `sof_json_value_type()` gives a value's type; `sof_json_value_string()`, `sof_json_value_bool()`, `sof_json_value_double()`, `sof_json_value_int()` and `sof_json_value_uint()` its contents; `sof_json_value_count()`, `sof_json_value_at()` and `sof_json_value_member()` an array's elements and an object's members; `sof_json_value_get()` and `sof_json_value_get_bytes()` a member's value by its key; and `sof_json_value_pointer()` a value by a JSON Pointer. Each value's `span`, and each member's `key_span`, say where it is in the input, for an application that keeps the text: `sof_string_position()`, from [sof_string](../sof_string/), turns an offset into a line and column, and the text can be changed in place at the spans, keeping its layout (see Spans). `sof_json_doc_clear()` empties the document, and `sof_json_doc_free()` releases its memory.

The writer adds the commas, colons, quotes and escapes, and, if the indentation isn't **0**, the line breaks and spaces. It keeps a stack of the objects and arrays that are open, so a piece in the wrong place, such as a value in an object without its key, or an `sof_json_writer_array_end()` where an object is open, is refused with **SOF_JSON_ERROR** and writes nothing; what it has written is always valid JSON so far. A call that runs out of memory writes nothing either, so it can be made again. Each top-level value is followed by a newline, so a writer can write one document, or a series of them in the JSON Lines format. Output goes into a `sof_string_t` buffer. Without a write function, it all stays there, for the application to read with `sof_json_writer_output()`. With one, the buffer is passed to it at the end of each top-level value, and whenever it passes **SOF_JSON_FLUSH_SIZE** (4096) bytes in the middle of one, so a large document is written in pieces in a buffer of that size.

The reader keeps its input in a buffer of **SOF_JSON_READ_SIZE** (4096) bytes, and reads each token from one run of it: when a token goes past the end, the reader moves it to the front and fills the rest, and doubles the buffer if the token fills it. So a document of any size is read in the memory its longest string or number takes, and a token is never cut in two. In a string, runs of plain bytes, printable ASCII but quotes and backslashes, are skipped 8 at a time, as the writer finds them when it escapes a string. Nothing changes until a token is whole, so a call that runs out of memory, or whose read function fails, can be made again and goes on where it stopped. It checks the input against RFC 8259's grammar as it goes; at the first error it returns **SOF_JSON_SYNTAX**, with the line, column and offset of the error, and goes on returning it.

A document keeps its values, keys and strings in an arena from [sof_mem](../sof_mem/), a string or number's text in the same block as its value, so reading a value allocates in a few large blocks, and clearing or freeing the document gives them back at once. An object's members are kept in their order. Objects with more than **SOF_JSON_INDEX_MIN** (8) members are also put in an index, one [sof_hashmap](../sof_hashmap/) for the whole document, so `sof_json_value_get()` finds a key in such an object without looking at each member; smaller objects are searched, which for so few is as fast. The values belong to the document, and are good until it reads another value, is cleared or is freed.

The library's demo is the [csvjson](demo/) program in _demo/_, which converts CSV, read with [sof_csv](../sof_csv/), to JSON with the writer, and with `-j`, JSON to CSV with the reader and a document. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All sof_json declarations are in _sof_json.h_. The writer's buffer and the reader's text are strings from the [sof_string](../sof_string/) library in _../sof_string/_ (_sof_string.c_ and _sof_string.h_), the record of what is open, and the document's work while it reads, are stacks from [sof_stack](../sof_stack/) in _../sof_stack/_ (_sof_stack.c_ and _sof_stack.h_), and strings are decoded and checked with the [sof_utf](../sof_utf/) library in _../sof_utf/_ (_sof_utf.h_, which is all of it). The document's arena is from the [sof_mem](../sof_mem/) library in _../sof_mem/_ (_sof_mem_arena.c_, _sof_mem.h_ and _sof_mem_internal.h_; its other allocators aren't needed), and its index is a [sof_hashmap](../sof_hashmap/) in _../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), hashing keys with [sof_hash](../sof_hash/) in _../sof_hash/_ (_sof_hash.c_, _sof_hash.h_ and the vendored _rapidhash.h_). Doubles are written and numbers converted to them with the [sof_real](../sof_real/) library in _../sof_real/_ (_sof_real.c_, _sof_real.h_ and _sof_real_internal.h_, with _sof_bit.h_ from [sof_bit](../sof_bit/) in _../sof_bit/_). Memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond those, the library needs nothing but the c standard library.

Writing:

    sof_json_writer_t w;
    sof_json_writer_init(&w, 2, 0, write_out, NULL, NULL);    // indent by 2; write_out() gets the output
    sof_json_writer_object_begin(&w);
    sof_json_writer_key(&w, "name");
    sof_json_writer_string(&w, "caf\xc3\xa9");                // UTF-8, written as it is
    sof_json_writer_key(&w, "sizes");
    sof_json_writer_array_begin(&w);
    sof_json_writer_int(&w, 8);
    sof_json_writer_double(&w, 0.5);
    sof_json_writer_array_end(&w);
    sof_json_writer_object_end(&w);                           // passes the document to write_out()
    sof_json_writer_free(&w);

which writes:

    {
      "name": "café",
      "sizes": [
        8,
        0.5
      ]
    }

Reading it back, whole:

    sof_json_doc_t d;
    sof_json_doc_init(&d, NULL);
    if (sof_json_doc_parse(&d, text, length) != SOF_JSON_OK) {
        printf("not JSON at line %zu, column %zu\n", d.line, d.column);
    }
    name = sof_json_value_string(sof_json_value_get(d.root, "name"), NULL);       // "café"
    sof_json_value_double(sof_json_value_pointer(d.root, "/sizes/1"), &size);     // 0.5
    sof_json_doc_free(&d);

or a token at a time, from a file of JSON Lines:

    sof_json_reader_t r;
    sof_json_token_t t;
    sof_json_reader_init(&r, SOF_JSON_LINES, sof_json_read_file, f, NULL);
    while (sof_json_reader_next(&r, &t) == SOF_JSON_OK) {
        if (t.type == SOF_JSON_KEY && strcmp(t.text, "sizes") == 0) {
            sof_json_reader_skip(&r);                          // passes over the member's value
        }
    }
    sof_json_reader_free(&r);

## Building

    make             # builds the library's objects, ./test and the demo, demo/csvjson
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

The tests use generated data. For the writer, besides testing each function, with **NULL** and freed writers, they check the exact output, compact and indented, of nested objects and arrays, empty ones and top-level values in a row; every escape, UTF-8 written as it is and as \u escapes, and each kind of invalid UTF-8; integers at their limits; doubles that need few digits and many, and 20,000 random doubles, each read back with `strtod()` and checked to have no more digits than it needs; doubles at the edges of the ways they are written, those needing 15, 16 and 17 digits, 1e21 to 1e23, integers below 1e17, **DBL_MAX**, 5e-324 and other subnormals, with their neighbours and 4,000 random subnormals and powers of ten, checked to have exactly as many digits as the shortest form that reads back; strings of up to 40 plain bytes with a quote, backslash, control character, DEL, UTF-8 or invalid UTF-8 at each place and each of 8 alignments, as values and keys, compact and as ASCII, against the escapes made a byte at a time; that doubles have a '.' in a locale whose decimal point is a comma, where one is installed; that every misplaced call is refused and writes nothing; the write function, with output passed on in pieces, failing writes and flushes; and 300 random documents, written compact, indented and as ASCII, checked as JSON and UTF-8, and against each other.

For the reader, they check each token of a document, with its depth, line, column and offset, read whole and in pieces of many sizes, from one byte up; each token's span, read whole and in pieces, and in JSON Lines; peeking before each token; every escape, surrogate pairs and their halves alone, and each kind of invalid UTF-8; strings of up to 40 plain bytes with an escape, UTF-8, DEL, a control character or invalid UTF-8 at each place, after each of 8 alignments, read whole and in pieces of 1, 3, 8 and 9 bytes, for their text, span and where they fail, and a long string of plain runs read with each allocation failing in turn; objects and arrays inside each other in turn, peeked and read, and closed by the wrong byte; the grammar of numbers and literals; errors of every kind and where they are reported; nesting to the limit and one past it; JSON Lines; a string and a number longer than the buffer; a read function that fails at each call in turn; skipping values; clearing; and **NULL** and freed readers. The number conversions are checked at the limits of each type, for numbers too large and too small, integers written with fractions and exponents, rounding with more digits than a double holds, 20,000 random doubles written by the writer and read back exactly, numbers at the edges of the conversion without `strtod()` (2^53, 10^22, and their neighbours) and 100,000 random numbers of up to 20 digits before and after the point, with trailing zeros and exponents, each converted to exactly what `strtod()` gives, and in a locale whose decimal point is a comma. For the document, they check every value function, the index with many objects and repeated keys and keys holding NULs, the examples of RFC 6901, reading values one at a time from JSON Lines, from an array's elements and after a key, the spans of every kind of value and key, with a line number from one and a value changed by splicing the text, and of values read from JSON Lines, errors, and **NULL** and freed documents and values. `sof_json_writer_value()` is checked inside other values, indented, with a write function, and failing. 300 random documents, written compact and indented, are read by the document and written again exactly as the compact output, and read by the reader in pieces as the same tokens. The reader, the document and the writer each fail every allocation in turn, checking that each failure changes nothing, that making the call again finishes, and that nothing is leaked.

Everything is checked against [../test/test_json.h](../test/), a checker written production by production from RFC 8259's grammar, which also requires UTF-8 and refuses a byte order mark, and decodes a text so that two can be compared for what they hold. It is itself checked against [JSONTestSuite](../test-data/JSONTestSuite/), kept unchanged in _../test-data/_: it accepts each of the 95 texts a parser must accept and refuses each of the 188 a parser must refuse. Each of the 95 is then decoded and written again with the writer, compact, indented and as ASCII, and must decode to the same values, strings and names. The reader must read each of the 95, whole and a byte at a time, as the same tokens the checker decodes, and the document must read it and write it again as the same values; both must refuse each of the 188; and the 35 left to the parser must be taken or refused alike whole, a byte at a time and by the document, with every string valid UTF-8. And every Unicode code point is written in a string, compact and as ASCII, and must decode to itself, which checks the escapes section 7 requires and the UTF-8 section 8.1 does.

To use the library in another project, copy _sof_json_writer.c_, _sof_json_reader.c_, _sof_json_doc.c_ and _sof_json.h_, with _sof_string.c_ and _sof_string.h_ from _../sof_string/_, _sof_stack.c_ and _sof_stack.h_ from _../sof_stack/_, _sof_utf.h_ from _../sof_utf/_, _sof_mem_arena.c_, _sof_mem.h_ and _sof_mem_internal.h_ from _../sof_mem/_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../sof_hash/_, _sof_real.c_, _sof_real.h_ and _sof_real_internal.h_ from _../sof_real/_, _sof_bit.h_ from _../sof_bit/_, and _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into it, and compile the _.c_ files with the project's own sources. A project that only writes JSON needs only _sof_json_writer.c_ of the library's own files, but still the headers _sof_json.h_ includes. To build its tests there too, copy _test.c_, and _test.h_ and _test_json.h_ from _../test/_, and compile _test.c_ and the library's _.c_ files with _-pthread_; they read JSONTestSuite's files from _../test-data/JSONTestSuite/_.

## Output

Values go where JSON allows them: at the top level, as an element of an array, or as the value of an object's member, after its key. Keys go only in an object, and only when the last member has its value.

With an indentation of **0**, each top-level value is written on one line, with no spaces: `{"a":[1,2],"b":{}}`. Otherwise each member and element is on a line of its own, indented by that many spaces for each level, with a space after each colon; an empty object or array is written `{}` or `[]`. Either way, each top-level value ends with a newline, so with an indentation of **0**, a series of values is JSON Lines.

Strings and keys are taken as UTF-8. `"` and `\` are escaped, and the control characters below U+0020 are written as `\b`, `\f`, `\n`, `\r`, `\t` or `\u00XX`; nothing else is escaped, not even `/`. Characters outside ASCII are written as they are, or with **SOF_JSON_ASCII**, as `\u` escapes in lower case hex, with a surrogate pair for those past U+FFFF, so the output is ASCII. Bytes that aren't valid UTF-8 are written as U+FFFD, one for each error as `sof_utf8_decode()` finds them, which is the Unicode standard's practice, so the output is always valid UTF-8.

Integers are written in full. A double is written by `sof_real_format()`, with the fewest significant digits, up to 17, that read back as the same double, and of those the closest to it: 0.1, 1.5, 0.3333333333333333, 1e+100, 5e-324 and -0. Numbers from 1e-4 up to 1e17 are written without an exponent, as 0.001, 100 or 10000000000000000, not as 1e-03 or 1e+02. The decimal point is always '.', whatever the locale. JSON has no infinities or NaNs, so they are refused. A document's numbers are written by `sof_json_writer_value()` with their text as it was read.

What the writer writes is JSON as RFC 8259 (and ECMA-404, which has the same grammar) defines it: each top-level value is a JSON text, in UTF-8, without a byte order mark, with only the whitespace the grammar allows between tokens. Values written with `sof_json_writer_raw()` are the exception, being written as they are. RFC 8259 leaves some things to the writer, and the writer doesn't check them: names in an object should be unique, and integers beyond 2^53, such as large `sof_json_writer_uint()` values, are valid JSON but may not be read exactly by a reader that keeps numbers as doubles.

## Input

The reader reads JSON as RFC 8259 defines it, and nothing else: one value, with whitespace around it if any, or with **SOF_JSON_LINES**, any number of values, one after another. Whitespace is space, tab, CR and LF. Literals are `true`, `false` and `null` in lower case, and numbers are an optional minus, an integer part without leading zeros, and an optional fraction and exponent, followed by whitespace, a comma, a closing bracket or brace, or the end. There are no comments, trailing commas, single quotes, unquoted keys, infinities or NaNs. Without **SOF_JSON_LINES**, input that is empty or only whitespace isn't JSON, and neither is anything after the value; with it, either is fine, and values can be separated by any whitespace, or by none where they can be told apart, as in `{}{}`, so JSON Lines, NDJSON and concatenated JSON are all read.

Input must be UTF-8, as section 8.1 requires, without a byte order mark: bytes that aren't valid UTF-8, in a string or anywhere else, are an error. Control characters below U+0020 must be escaped in a string. Strings and keys are returned decoded, with each escape replaced by what it stands for and a surrogate pair by its code point. A `\u` escape of half a surrogate pair without its other half, which the grammar allows but stands for no character, is returned as U+FFFD, so what the reader returns is always valid UTF-8. A `\u0000` is returned as a NUL, so a string's length, not its NUL, says where it ends.

Objects and arrays can nest **SOF_JSON_DEPTH_MAX** (1024) deep, as section 9 lets a parser limit them; deeper input is refused with **SOF_JSON_TOO_DEEP**. The reader doesn't recurse, so the limit is there to keep documents, and the writer writing them, within bounds.

A number token is its text as written, which can be any length. `sof_json_number_double()` converts it to the nearest double, correctly rounded however many digits it has, with [sof_real](../sof_real/), which doesn't depend on the locale; numbers too large for a double are **SOF_JSON_RANGE**, and those too small are **0** or subnormal. `sof_json_number_int()` and `sof_json_number_uint()` convert it to an integer exactly, if it is one, however it is written: 100, 1e2, 1.0e2 and 10000e-2 are all 100; a number with a fraction, or out of the type's range, is **SOF_JSON_RANGE**.

Line and column numbers count from **1**, lines at each LF, and columns in bytes. The offset counts bytes from **0**, from the start of the input, or since the reader was last cleared.

## Documents

An object's members are kept in their order, as `sof_json_value_member()` returns them. RFC 8259 says names should be unique but doesn't require it; if a key is there more than once, `sof_json_value_get()` finds the last member with it, as most readers do, and `sof_json_value_member()` still returns them all.

`sof_json_value_pointer()` follows RFC 6901. The pointer `""` is the value itself, and each `/` and the token after it go a level down: into an object by key, with `~1` in the token standing for `/` and `~0` for `~`, or into an array by index, in decimal without leading zeros. `-`, which the RFC uses for the place past an array's last element, finds nothing.

A document doesn't keep a text's layout, or how its strings were escaped: written again, it is written as the writer writes. The spans say where each value and key is in the input, so an application that must keep the layout changes the text itself (see Spans).

`sof_json_doc_read()` reads the next whole value from a reader, so a document needn't hold all the input. With **SOF_JSON_LINES**, each call reads the next value, until it returns **SOF_JSON_NO_MATCH** at the end. After reading the token that begins an array, each call reads the next element, until it returns **SOF_JSON_NO_MATCH** at the array's end, so an array of records too large for memory is read a record at a time. After a key, it reads the member's value.

## Spans

A span is an `offset`, in bytes from the start of the input, or from where the reader was last cleared, and a `length`. Each token the reader returns has one, in `span`, as does each value in a document, and each member of an object for its key, in `key_span`:

- A string or key is its text as written, quotes and escapes included; a key's doesn't include its colon.
- A number or literal is its text as written.
- The token that begins or ends an object or array is its `{`, `[`, `}` or `]`, and a document's object or array runs from the first to the second.

The offsets go on counting from one value to the next, so values read one at a time from JSON Lines have offsets in the whole input. The document doesn't keep the text, so an application that uses the spans reads a text it holds, with `sof_json_doc_parse()`. To find the line a value is on, give its offset to `sof_string_position()`. To change a value, replace the text of its span with the new value written as JSON. `sof_string_delete()` and `sof_string_insert()` make these changes to a text kept in a `sof_string_t`. Splice from the end of the text back, or parse again after each change, since an edit moves the offsets after it.

    size_t line;
    sof_string_t text = sof_string_view(json, length);
    const sof_json_value_t *v = sof_json_value_pointer(d.root, "/dependencies/sof");
    sof_string_position(&text, v->span.offset, &line, NULL);         // the line it is on

## JSON Lines

JSON Lines (JSONL, also called NDJSON) is a series of JSON values, one to a line, each followed by a newline: a log, or records too many to hold as one array. The library reads and writes it as it does JSON.

To write it, set the writer up with an indentation of **0** and write one top-level value after another. Each is written on one line, with any newline in a string escaped, and followed by a newline, and with a write function, each line is passed on as soon as its value ends. An indentation other than **0** would spread a value over several lines, which JSON Lines doesn't allow, and so would a `sof_json_writer_raw()` value with a newline in it.

    sof_json_writer_init(&w, 0, 0, write_out, NULL, NULL);     // one value to a line
    for (i = 0; i < count; i++) {
        sof_json_writer_object_begin(&w);
        sof_json_writer_key(&w, "name");
        sof_json_writer_string(&w, names[i]);
        sof_json_writer_object_end(&w);                         // writes the line and passes it to write_out()
    }

To read it, set the reader up with **SOF_JSON_LINES**, and read each value whole into a document with `sof_json_doc_read()`, or a token at a time with `sof_json_reader_next()`; either returns **SOF_JSON_NO_MATCH** after the last value. A file of any size is read in the memory one line takes. The reader is lenient: values can be separated by any whitespace, so blank lines, CRLF line ends, several values on a line and values over several lines are all read, as is concatenated JSON such as `{}{}`, and an empty input is no values rather than an error. `values` counts the values read, and an error's `line` is the line it is on.

    sof_json_reader_init(&r, SOF_JSON_LINES, sof_json_read_file, f, NULL);
    while ((rc = sof_json_doc_read(&d, &r)) == SOF_JSON_OK) {
        printf("%s\n", sof_json_value_string(sof_json_value_get(d.root, "name"), NULL));
    }
    if (rc != SOF_JSON_NO_MATCH) {
        printf("line %zu: not JSON\n", r.line);
    }

Reading a value into a document and writing it with `sof_json_writer_value()` copies JSON Lines a record at a time, and the demo's `csvjson -l` and `csvjson -j` convert CSV to JSON Lines and back.

## Functions

`sof_json_error_t sof_json_writer_init(sof_json_writer_t *w, size_t indent, unsigned flags, sof_json_write_func_t write, void *ctx, const sof_alloc_t *alloc)`

Initialize the writer _w_. _indent_ is the spaces to indent each level of objects and arrays by, or **0** to write each top-level value on one line. _flags_ is **0**, or **SOF_JSON_ASCII** to write every character outside ASCII as a \u escape. _write_ is the function to pass output to, or **NULL** to keep it all in the buffer, and _ctx_ is passed to it. _alloc_ is the allocator for the buffer and the stack of what is open; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). It is copied into the writer, so it can be on the stack. Returns **SOF_JSON_OK**, **SOF_JSON_NO_MEM**, or **SOF_JSON_ERROR** if _w_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the writer is left empty and not initialized.

`sof_json_error_t sof_json_writer_object_begin(sof_json_writer_t *w)`

Begin an object, where a value can go. Until `sof_json_writer_object_end()`, its members are written as a key and then a value. Returns **SOF_JSON_OK**, **SOF_JSON_NO_MEM**, or **SOF_JSON_ERROR** if _w_ is **NULL** or not initialized, or a value can't go here. On failure nothing is written.

`sof_json_error_t sof_json_writer_key(sof_json_writer_t *w, const char *key)`

Write the key of the next member of the object open last, from the NUL-terminated UTF-8 _key_, escaped as a string is. The value written next is the member's value. The writer doesn't check that keys are unique. Returns **SOF_JSON_OK**, **SOF_JSON_NO_MEM**, or **SOF_JSON_ERROR** if _w_ or _key_ is **NULL**, _w_ is not initialized, or what is open last isn't an object waiting for a key. On failure nothing is written.

`sof_json_error_t sof_json_writer_key_bytes(sof_json_writer_t *w, const void *key, size_t length)`

Write a key as `sof_json_writer_key()` does, from the _length_ bytes at _key_, which can hold NULs, written as \u0000. _key_ can be **NULL** if _length_ is **0**, for an empty key. Returns as `sof_json_writer_key()` does, and **SOF_JSON_ERROR** if _key_ is **NULL** and _length_ isn't **0**.

`sof_json_error_t sof_json_writer_object_end(sof_json_writer_t *w)`

End the object begun last. If it ends a top-level value, a newline follows, and if there is a write function, the buffer is passed to it. Returns **SOF_JSON_OK**, **SOF_JSON_NO_MEM**, **SOF_JSON_WRITE** if this ends a top-level value and the write function failed, or **SOF_JSON_ERROR** if _w_ is **NULL** or not initialized, what is open last isn't an object, or it has a key waiting for its value. On failure other than **SOF_JSON_WRITE**, nothing is written and the object stays open.

`sof_json_error_t sof_json_writer_array_begin(sof_json_writer_t *w)`

Begin an array, where a value can go. Values written until `sof_json_writer_array_end()` are its elements. Returns as `sof_json_writer_object_begin()` does.

`sof_json_error_t sof_json_writer_array_end(sof_json_writer_t *w)`

End the array begun last, as `sof_json_writer_object_end()` ends an object. Returns as `sof_json_writer_object_end()` does, with **SOF_JSON_ERROR** if what is open last isn't an array.

`sof_json_error_t sof_json_writer_string(sof_json_writer_t *w, const char *s)`

Write the NUL-terminated UTF-8 string _s_, in quotes and escaped as Output describes. Returns as `sof_json_writer_null()` does, and **SOF_JSON_ERROR** if _s_ is **NULL**.

`sof_json_error_t sof_json_writer_string_bytes(sof_json_writer_t *w, const void *s, size_t length)`

Write a string as `sof_json_writer_string()` does, from the _length_ bytes at _s_, which can hold NULs, written as \u0000. _s_ can be **NULL** if _length_ is **0**, for an empty string. Returns as `sof_json_writer_null()` does, and **SOF_JSON_ERROR** if _s_ is **NULL** and _length_ isn't **0**.

`sof_json_error_t sof_json_writer_int(sof_json_writer_t *w, int64_t value)`

`sof_json_error_t sof_json_writer_uint(sof_json_writer_t *w, uint64_t value)`

Write a signed or unsigned integer, _value_, in full. Returns as `sof_json_writer_null()` does.

`sof_json_error_t sof_json_writer_double(sof_json_writer_t *w, double value)`

Write the number _value_ with the fewest significant digits that read back as it, as Output describes. Returns as `sof_json_writer_null()` does, and **SOF_JSON_ERROR** if _value_ is infinite or a NaN.

`sof_json_error_t sof_json_writer_bool(sof_json_writer_t *w, int value)`

Write `true` if _value_ isn't **0**, or `false`. Returns as `sof_json_writer_null()` does.

`sof_json_error_t sof_json_writer_null(sof_json_writer_t *w)`

Write `null`. Like every value, it goes at the top level, in an array, or in an object after its key; a top-level value is followed by a newline, and if there is a write function, the buffer is passed to it. Returns **SOF_JSON_OK**, **SOF_JSON_NO_MEM**, **SOF_JSON_WRITE** if the value was written but the write function failed, or **SOF_JSON_ERROR** if _w_ is **NULL** or not initialized, or a value can't go here. On failure other than **SOF_JSON_WRITE**, nothing is written.

`sof_json_error_t sof_json_writer_raw(sof_json_writer_t *w, const void *json, size_t length)`

Write the _length_ bytes at _json_ as they are, as one value, where a value can go. They must be one whole JSON value, such as the output of another writer; the writer doesn't check them, and doesn't indent them further. Returns as `sof_json_writer_null()` does, and **SOF_JSON_ERROR** if _json_ is **NULL** or _length_ is **0**.

`sof_json_error_t sof_json_writer_value(sof_json_writer_t *w, const sof_json_value_t *v)`

Write the value _v_, from a document, with all it holds, where a value can go, as the calls for each of its parts would write it, numbers with their text as it was read. The whole value is built in the buffer before any of it is passed to the write function, so if a call fails, nothing is written, and a large value goes to the write function in one piece. Returns as `sof_json_writer_null()` does, and **SOF_JSON_ERROR** if _v_ is **NULL**.

`const char *sof_json_writer_output(const sof_json_writer_t *w, size_t *length)`

Return the output in the buffer, followed by a NUL, and if _length_ isn't **NULL**, store its length in bytes there. With a write function, this is what hasn't been passed to it yet. The output belongs to the writer, and is good until the next call that writes, flushes, clears or frees. Returns **NULL**, and a _length_ of **0**, if _w_ is **NULL** or not initialized.

`sof_json_error_t sof_json_writer_flush(sof_json_writer_t *w)`

Pass the buffer to the write function and empty it. Output can be flushed at any point, in the middle of a value too. Does nothing if there is no write function or the buffer is empty. Returns **SOF_JSON_OK**, **SOF_JSON_WRITE** if the write function failed, which leaves the buffer as it was, or **SOF_JSON_ERROR** if _w_ is **NULL** or not initialized.

`size_t sof_json_writer_depth(const sof_json_writer_t *w)`

Return the number of objects and arrays that are open: **0** at the top level, and also if _w_ is **NULL** or not initialized.

`int sof_json_writer_complete(const sof_json_writer_t *w)`

Return **1** if at least one top-level value has been written and nothing is open, so the output is complete JSON (or JSON Lines), or **0**, also if _w_ is **NULL** or not initialized.

`sof_json_error_t sof_json_writer_clear(sof_json_writer_t *w)`

Start again, empty: discard the buffer, without passing it to the write function, and every object and array that is open, so the next value is a new top-level value. `values` and `written` go back to **0**. The memory is kept, and so are the indentation, flags and write function. Returns **SOF_JSON_OK**, or **SOF_JSON_ERROR** if _w_ is **NULL** or not initialized.

`sof_json_error_t sof_json_writer_free(sof_json_writer_t *w)`

Release the writer's memory. The buffer is discarded, not flushed: call `sof_json_writer_flush()` first to keep it. The `sof_json_writer_t` itself isn't freed; it is left empty, and acts like a writer whose `sof_json_writer_init()` failed until it is initialized again. Returns **SOF_JSON_OK**, also for a writer not initialized, or **SOF_JSON_ERROR** if _w_ is **NULL**.

`sof_json_error_t sof_json_reader_init(sof_json_reader_t *r, unsigned flags, sof_json_read_func_t read, void *ctx, const sof_alloc_t *alloc)`

Initialize the reader _r_ to read input from the function _read_, such as `sof_json_read_file()`, which is passed _ctx_, such as a `FILE *`. _flags_ is **0**, or **SOF_JSON_LINES** to read a series of values. _alloc_ is the allocator for the buffer, the decoded text and the stack of what is open, or **NULL** for the standard functions; it is copied. Returns **SOF_JSON_OK**, **SOF_JSON_NO_MEM**, or **SOF_JSON_ERROR** if _r_ or _read_ is **NULL**, or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the reader is left empty and not initialized.

`sof_json_error_t sof_json_reader_init_bytes(sof_json_reader_t *r, unsigned flags, const void *data, size_t length, const sof_alloc_t *alloc)`

Initialize the reader _r_ to read the _length_ bytes at _data_, which aren't copied, so they must stay as they are while it is used. _data_ can be **NULL** if _length_ is **0**. _flags_ and _alloc_ are as for `sof_json_reader_init()`. Returns **SOF_JSON_OK**, **SOF_JSON_NO_MEM**, or **SOF_JSON_ERROR** if _r_ is **NULL**, _data_ is **NULL** and _length_ isn't **0**, or _alloc_ can't free. On failure the reader is left empty and not initialized.

`int sof_json_read_file(char *buffer, size_t size, size_t *length, void *ctx)`

A read function for a `FILE *`, given as _ctx_. It reads up to _size_ bytes into _buffer_ with `fread()`, and stores how many in _length_, **0** at the end of the file. It never closes the file. Returns **0**, or **1** if the file couldn't be read or _ctx_ is **NULL**.

`sof_json_error_t sof_json_reader_next(sof_json_reader_t *r, sof_json_token_t *token)`

Read the next token into _token_, if it isn't **NULL**. An object is read as **SOF_JSON_OBJECT**, then for each member a **SOF_JSON_KEY** and its value, then **SOF_JSON_OBJECT_END**; an array as **SOF_JSON_ARRAY**, its elements and **SOF_JSON_ARRAY_END**; and other values as **SOF_JSON_STRING**, **SOF_JSON_NUMBER**, **SOF_JSON_BOOL** or **SOF_JSON_NULL**. The token's _text_, _length_, _boolean_ and _depth_ are as Types describes, and its place is in the reader's `line`, `column` and `offset`. Returns **SOF_JSON_OK**; **SOF_JSON_NO_MATCH** at the end of the input, after the value or values; **SOF_JSON_NO_MEM** or **SOF_JSON_READ** if memory ran out or the read function failed, which changes nothing, so the call can be made again; **SOF_JSON_SYNTAX** if the input isn't JSON, or **SOF_JSON_TOO_DEEP** if it nests too deep, with the error's place in `line`, `column` and `offset`, which it then goes on returning until the reader is cleared; or **SOF_JSON_ERROR** if _r_ is **NULL** or not initialized. After the end, it goes on returning **SOF_JSON_NO_MATCH** without calling the read function. Unless a token is returned, the token's type is **SOF_JSON_NONE**.

`sof_json_error_t sof_json_reader_peek(sof_json_reader_t *r, sof_json_type_t *type)`

Store the type the next token has in _type_, if it isn't **NULL**, without reading the token. The whitespace before it, and a comma or colon, are read; the token itself is checked when `sof_json_reader_next()` reads it, so a token that begins as a value does but goes on wrong, such as `tru`, is found then. Use it to choose between `sof_json_doc_read()` and `sof_json_reader_next()`, as the demo does to read the elements of a top-level array one at a time. Returns **SOF_JSON_OK**; **SOF_JSON_NO_MATCH** at the end of the input; **SOF_JSON_SYNTAX** if no token can begin with the next byte, or none can go there; or otherwise as `sof_json_reader_next()` does. Unless **SOF_JSON_OK** is returned, _type_ is **SOF_JSON_NONE**.

`sof_json_error_t sof_json_reader_skip(sof_json_reader_t *r)`

Skip a value: after a **SOF_JSON_ARRAY** or **SOF_JSON_OBJECT** token, read up to and including its end; after a **SOF_JSON_KEY**, read the member's value, whole; after any other token, do nothing. Returns **SOF_JSON_OK**, or as `sof_json_reader_next()` does; after **SOF_JSON_NO_MEM** or **SOF_JSON_READ**, calling it again goes on skipping.

`size_t sof_json_reader_depth(const sof_json_reader_t *r)`

Return the number of objects and arrays that are open: **0** at the top level, and also if _r_ is **NULL** or not initialized.

`sof_json_error_t sof_json_reader_clear(sof_json_reader_t *r)`

Start again: drop the open objects and arrays, the token and any error, and start on line **1**, with `values` back to **0**. A reader with a read function drops the input read but not yet taken apart, and goes on with what the function gives next; one reading bytes starts again at their first. The memory is kept. Returns **SOF_JSON_OK**, or **SOF_JSON_ERROR** if _r_ is **NULL** or not initialized.

`sof_json_error_t sof_json_reader_free(sof_json_reader_t *r)`

Release the reader's memory. The `sof_json_reader_t` itself isn't freed; it is left empty, and acts like a reader whose init failed until it is initialized again. The input isn't closed. Returns **SOF_JSON_OK**, also for a reader not initialized, or **SOF_JSON_ERROR** if _r_ is **NULL**.

`sof_json_error_t sof_json_number_double(const char *text, size_t length, double *value)`

Convert the number in the _length_ bytes at _text_, which needn't be NUL-terminated, such as a number token's text, to the nearest double, and store it in _value_, as Input describes. Returns **SOF_JSON_OK**; **SOF_JSON_RANGE** if the number is too large for a double, with an infinity of its sign in _value_; **SOF_JSON_SYNTAX** if the text isn't a JSON number; or **SOF_JSON_ERROR** if _text_ or _value_ is **NULL**.

`sof_json_error_t sof_json_number_int(const char *text, size_t length, int64_t *value)`

`sof_json_error_t sof_json_number_uint(const char *text, size_t length, uint64_t *value)`

Convert the number in the _length_ bytes at _text_ to a signed or unsigned integer, exactly, and store it in _value_, as Input describes; -0 is **0**. Returns **SOF_JSON_OK**; **SOF_JSON_RANGE** if the number isn't an integer or is out of the type's range; **SOF_JSON_SYNTAX** if the text isn't a JSON number; or **SOF_JSON_ERROR** if _text_ or _value_ is **NULL**. On failure _value_ is left alone.

`sof_json_error_t sof_json_doc_init(sof_json_doc_t *d, const sof_alloc_t *alloc)`

Initialize the empty document _d_. _alloc_ is the allocator for its arena, index and the rest, or **NULL** for the standard functions; it is copied. Nothing is allocated until a value is read. Returns **SOF_JSON_OK**, or **SOF_JSON_ERROR** if _d_ is **NULL** or _alloc_ can't free. On failure the document is left empty and not initialized.

`sof_json_error_t sof_json_doc_parse(sof_json_doc_t *d, const void *text, size_t length)`

Parse the _length_ bytes at _text_, which must be one JSON value with whitespace around it if any, into _d_, replacing what it held, with the value in `root`. _text_ can be **NULL** if _length_ is **0**, which isn't JSON. Returns **SOF_JSON_OK**; **SOF_JSON_NO_MEM**; **SOF_JSON_SYNTAX** if the text isn't JSON, or **SOF_JSON_TOO_DEEP** if it nests too deep, with the error's place in `line`, `column` and `offset`; or **SOF_JSON_ERROR** if _d_ is **NULL** or not initialized, or _text_ is **NULL** and _length_ isn't **0**. On failure the document is empty.

`sof_json_error_t sof_json_doc_read(sof_json_doc_t *d, sof_json_reader_t *r)`

Read the next whole value from the reader _r_ into _d_, replacing what it held, with the value in `root`: a top-level value, the next element of an array the reader is in, or the value of a member whose key the application has read, as Documents describes. Returns **SOF_JSON_OK**; **SOF_JSON_NO_MATCH**, with the document empty, at the end of the input or, having read it, at the end of the array or object the reader is in; **SOF_JSON_ERROR** if the next token is a key, which the reader has then read, or _d_ or _r_ is **NULL** or not initialized; or as `sof_json_reader_next()` returns, with an error's place in `line`, `column` and `offset`. On failure the document is empty, and the reader has read part of the value.

`sof_json_type_t sof_json_value_type(const sof_json_value_t *v)`

Return the type of _v_: **SOF_JSON_NULL**, **SOF_JSON_BOOL**, **SOF_JSON_NUMBER**, **SOF_JSON_STRING**, **SOF_JSON_ARRAY** or **SOF_JSON_OBJECT**, or **SOF_JSON_NONE** if _v_ is **NULL**.

`const char *sof_json_value_string(const sof_json_value_t *v, size_t *length)`

Return the bytes of the string _v_, decoded UTF-8 that can hold NULs, followed by a NUL, and if _length_ isn't **NULL**, store their number there, not counting the NUL. Returns **NULL**, with a _length_ of **0**, if _v_ is **NULL** or not a string.

`sof_json_error_t sof_json_value_bool(const sof_json_value_t *v, int *value)`

Store **1** in _value_ if _v_ is true, or **0** if it is false. Returns **SOF_JSON_OK**, **SOF_JSON_NO_MATCH** if _v_ is **NULL** or not a boolean, or **SOF_JSON_ERROR** if _value_ is **NULL**.

`sof_json_error_t sof_json_value_double(const sof_json_value_t *v, double *value)`

`sof_json_error_t sof_json_value_int(const sof_json_value_t *v, int64_t *value)`

`sof_json_error_t sof_json_value_uint(const sof_json_value_t *v, uint64_t *value)`

Convert the number _v_ as `sof_json_number_double()`, `sof_json_number_int()` or `sof_json_number_uint()` does, and store it in _value_. Returns as they do, and **SOF_JSON_NO_MATCH** if _v_ is **NULL** or not a number.

`size_t sof_json_value_count(const sof_json_value_t *v)`

Return the number of elements in the array _v_ or members in the object _v_, or **0** if _v_ is **NULL** or neither.

`const sof_json_value_t *sof_json_value_at(const sof_json_value_t *array, size_t index)`

Return the element of _array_ at _index_, counting from **0**, or **NULL** if _array_ is **NULL** or not an array, or _index_ is past its end.

`const sof_json_member_t *sof_json_value_member(const sof_json_value_t *object, size_t index)`

Return the member of _object_ at _index_, in the order of its members, counting from **0**, or **NULL** if _object_ is **NULL** or not an object, or _index_ is past its end.

`const sof_json_value_t *sof_json_value_get(const sof_json_value_t *object, const char *key)`

Return the value of the member of _object_ whose key is the NUL-terminated UTF-8 _key_; the last such member, if there is more than one. Returns **NULL** if no member has the key, _object_ is **NULL** or not an object, or _key_ is **NULL**.

`const sof_json_value_t *sof_json_value_get_bytes(const sof_json_value_t *object, const void *key, size_t length)`

Find a member as `sof_json_value_get()` does, by the _length_ bytes at _key_, which can hold NULs. _key_ can be **NULL** if _length_ is **0**, for the empty key. Returns as `sof_json_value_get()` does.

`const sof_json_value_t *sof_json_value_pointer(const sof_json_value_t *v, const char *pointer)`

Return the value the NUL-terminated JSON Pointer _pointer_, such as `"/items/0/name"`, finds from _v_, as Documents describes. Returns **NULL** if there is none, _pointer_ isn't a JSON Pointer, or _v_ or _pointer_ is **NULL**.

`sof_json_error_t sof_json_doc_clear(sof_json_doc_t *d)`

Empty the document, so `root` is **NULL**, keeping its memory for the next value. Returns **SOF_JSON_OK**, or **SOF_JSON_ERROR** if _d_ is **NULL** or not initialized.

`sof_json_error_t sof_json_doc_free(sof_json_doc_t *d)`

Release the document's memory. The `sof_json_doc_t` itself isn't freed; it is left empty, and acts like a document whose `sof_json_doc_init()` failed until it is initialized again. Returns **SOF_JSON_OK**, also for a document not initialized, or **SOF_JSON_ERROR** if _d_ is **NULL**.

Every function accepts a **NULL** writer, reader or document, and one that was freed or whose init failed (`ready` is **0**), which writes, reads or holds nothing: functions that change it return **SOF_JSON_ERROR**, `sof_json_writer_output()` returns **NULL**, `sof_json_writer_depth()`, `sof_json_writer_complete()` and `sof_json_reader_depth()` return **0**, and the free functions return **SOF_JSON_OK**, or **SOF_JSON_ERROR** for **NULL**. The value functions accept a **NULL** value, so lookups can be chained, as in `sof_json_value_get(sof_json_value_at(list, 2), "name")`: those that return a value, member or string return **NULL**, `sof_json_value_count()` returns **0**, `sof_json_value_type()` returns **SOF_JSON_NONE**, and those that convert return **SOF_JSON_NO_MATCH**, or **SOF_JSON_ERROR** for a **NULL** place to store the result.

## Types

    typedef enum sof_json_error_t {
        SOF_JSON_OK = 0,
        SOF_JSON_NO_MEM,
        SOF_JSON_NO_MATCH,              // the end of the input, or a value that isn't there or isn't the type asked for
        SOF_JSON_ERROR,
        SOF_JSON_WRITE,                 // the write function failed; the output is kept
        SOF_JSON_READ,                  // the read function failed
        SOF_JSON_SYNTAX,                // the input isn't JSON
        SOF_JSON_TOO_DEEP,              // the input nests deeper than SOF_JSON_DEPTH_MAX
        SOF_JSON_RANGE,                 // a number doesn't fit the type asked for
    } sof_json_error_t;

    typedef enum sof_json_flag_t {
        SOF_JSON_ASCII = 1,             // writer: write characters outside ASCII as \u escapes
        SOF_JSON_LINES = 2,             // reader: read a series of values, as in JSON Lines
    } sof_json_flag_t;

    typedef enum sof_json_type_t {
        SOF_JSON_NONE = 0,              // no value or token
        SOF_JSON_NULL,
        SOF_JSON_BOOL,
        SOF_JSON_NUMBER,
        SOF_JSON_STRING,
        SOF_JSON_ARRAY,                 // a value, or the reader's token where one begins
        SOF_JSON_OBJECT,                // a value, or the reader's token where one begins
        SOF_JSON_KEY,                   // the reader's token for a member's key
        SOF_JSON_ARRAY_END,             // the reader's token where an array ends
        SOF_JSON_OBJECT_END,            // the reader's token where an object ends
    } sof_json_type_t;

The types of the reader's tokens and the document's values. A document's values are **SOF_JSON_NULL** to **SOF_JSON_OBJECT**.

    typedef int (*sof_json_write_func_t)(const char *data, size_t length, void *ctx);

The write function. It is called with the _length_ bytes of output at _data_, which aren't NUL-terminated and belong to the writer, and the _ctx_ given to `sof_json_writer_init()`. It returns **0** if every byte was written, or non-zero if they couldn't be; the writer then keeps them all in its buffer and passes them again, with whatever follows, at the next flush. A failure is reported by the call that ended a top-level value, or by `sof_json_writer_flush()`; one in the middle of a value, when the buffer passed **SOF_JSON_FLUSH_SIZE**, isn't reported, since the next flush will try again. It must not call into the writer.

    typedef int (*sof_json_read_func_t)(char *buffer, size_t size, size_t *length, void *ctx);

The read function. It is called with a _buffer_ of _size_ bytes, the reader's own, and the _ctx_ given to `sof_json_reader_init()`, and puts up to _size_ bytes in _buffer_ and their number in _length_, which is **0** at the end of the input. It may return fewer bytes than _size_, as a pipe or socket does. It returns **0**, or non-zero if the input couldn't be read, which `sof_json_reader_next()` returns as **SOF_JSON_READ**. It must not call into the reader.

    typedef struct sof_json_writer_t {
        sof_string_t out;               // the output not yet passed to write: all of it, if there is none
        sof_stack_t open;               // a byte for each object or array that is open, outermost first
        size_t indent;                  // spaces to indent each level by, or 0
        unsigned flags;                 // the sof_json_flag_t flags
        sof_json_write_func_t write;    // where output goes, or NULL
        void *ctx;                      // passed to write
        size_t values;                  // top-level values finished
        size_t written;                 // bytes passed to write
        int ready;                      // 1 once initialized; 0 when freed or failed
    } sof_json_writer_t;

A writer. The application may read the fields but must not change them. `out.data` moves as the buffer grows, so a pointer into it is good only until the next call that writes.

    typedef struct sof_json_token_t {
        sof_json_type_t type;           // what the token is, or SOF_JSON_NONE
        const char *text;               // a string or key decoded, or a number's text; "" otherwise
        size_t length;                  // the bytes in text, not counting its NUL
        int boolean;                    // for SOF_JSON_BOOL, 1 for true or 0 for false
        size_t depth;                   // the objects and arrays open after the token
        sof_json_span_t span;           // where it is in the input, as written
    } sof_json_token_t;

A token, which the application provides and `sof_json_reader_next()` fills in. _text_ is NUL-terminated, and for a string or key is UTF-8 that can hold NULs, so _length_ says where it ends. It belongs to the reader, and is good until the reader's next call. _depth_ is **1** for an array that begins at the top level, and **0** for its end. _span_ is where it is in the input, as Spans describes, or **0** and **0** if no token was read.

    typedef struct sof_json_span_t {
        size_t offset;                  // the first byte's, from the start of the input
        size_t length;                  // bytes
    } sof_json_span_t;

Where a token or value is in the input; see Spans.

    typedef struct sof_json_reader_t {
        char *buffer;                   // the buffer, size bytes, with a read function; NULL with bytes
        size_t size;
        const char *input;              // buffer, or the application's bytes
        size_t length;                  // the bytes in input
        size_t position;                // where the next byte to take apart is
        size_t mark;                    // where the token being read begins
        size_t consumed;                // bytes dropped from the front of buffer
        sof_string_t text;              // the decoded text of the token last read
        sof_stack_t open;               // a byte for each object (1) or array (0) open, outermost first
        unsigned flags;                 // the sof_json_flag_t flags
        sof_json_read_func_t read;      // where input comes from, or NULL
        void *ctx;                      // passed to read
        sof_alloc_t alloc;              // the memory functions
        int state;                      // what the reader expects next
        sof_json_type_t last;           // the type of the token last returned
        size_t skip;                    // while skipping, 1 more than the depth skipped to
        int ended;                      // 1 once read has reported the end
        sof_json_error_t error;         // SOF_JSON_SYNTAX or SOF_JSON_TOO_DEEP once found, or SOF_JSON_OK
        size_t line;                    // the line of the token last returned, or of the error
        size_t column;                  // its column, in bytes
        size_t offset;                  // its offset from the start of the input
        size_t next_line;               // the line the next byte is on
        size_t line_start;              // the offset where that line begins
        size_t values;                  // top-level values read whole
        int ready;                      // 1 once initialized; 0 when freed or failed
    } sof_json_reader_t;

A reader. The application may read the fields but must not change them; `line`, `column`, `offset` and `values` are those it will want.

    typedef struct sof_json_value_t {
        sof_json_type_t type;           // SOF_JSON_NULL to SOF_JSON_OBJECT
        size_t length;                  // bytes of a string or number, elements of an array, members of an object
        sof_json_span_t span;           // where it is in the input
        union {
            int boolean;                                    // SOF_JSON_BOOL
            const char *text;                               // SOF_JSON_STRING, decoded, or SOF_JSON_NUMBER, as written
            const struct sof_json_value_t *const *elements;   // SOF_JSON_ARRAY, or NULL if it has none
            struct {
                const struct sof_json_member_t *members;     // SOF_JSON_OBJECT, in order, or NULL if it has none
                const sof_hashmap_t *index;                  // the document's index, if it has more than SOF_JSON_INDEX_MIN members
            } object;
        } as;
    } sof_json_value_t;

    typedef struct sof_json_member_t {
        const char *key;                // decoded, UTF-8 that can hold NULs, NUL-terminated
        size_t key_length;
        sof_json_span_t key_span;       // where the key is in the input, as written
        const sof_json_value_t *value;
        const sof_json_value_t *object;  // the object it is in
    } sof_json_member_t;

A value and an object's member in a document. They belong to the document, and are good until it reads another value, is cleared or is freed. The application may read the fields, or use the `sof_json_value_` functions, which also take **NULL**, but must not change them.

    typedef struct sof_json_doc_t {
        const sof_json_value_t *root;   // the value read, or NULL
        sof_mem_arena_t arena;          // where the values, members, keys and strings are
        sof_hashmap_t *index;           // the index of large objects' members, in the arena, or NULL
        sof_stack_t frames;             // while a value is read, the objects and arrays open in it
        sof_stack_t elements;           // ... their elements
        sof_stack_t members;            // ... and their members
        sof_alloc_t alloc;              // the memory functions
        size_t line;                    // after SOF_JSON_SYNTAX or SOF_JSON_TOO_DEEP, where the error is; otherwise 0
        size_t column;
        size_t offset;
        int ready;                      // 1 once initialized; 0 when freed or failed
    } sof_json_doc_t;

A document. The application may read the fields but must not change them.

## Threading

The library has no global state and does no locking. Different writers, readers and documents can be used on different threads freely. Numbers are written and converted without the locale, so changing it on another thread doesn't matter.

`sof_json_writer_output()`, `sof_json_writer_depth()` and `sof_json_writer_complete()` only read the writer, so any number of threads can call them at once on one writer, with a readers-writer lock if another thread writes to it. Every other writer function changes the writer and needs the write lock; since writing can move the buffer, a reader must not keep the pointer `sof_json_writer_output()` returned once it lets go of the lock. The write function is called with the lock held by whoever is writing. `sof_json_writer_value()` only reads the document's value it writes.

`sof_json_reader_depth()` is the only reader function that only reads the reader; the others all change it, `sof_json_reader_peek()` too, since it reads ahead.

A document's values never change once read, so the `sof_json_value_` functions, which only read, can be called on one document from any number of threads at once, as can `sof_json_writer_value()` with a writer each. `sof_json_doc_parse()`, `sof_json_doc_read()`, `sof_json_doc_clear()` and `sof_json_doc_free()` change the document and invalidate its values, so they need the write lock, and no thread may keep a value once it lets go of the read lock.
