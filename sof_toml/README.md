# sof_toml

TOML 1.1.0 in c: a document tree that parses and checks a whole text, and a streaming writer.

The library's types and functions share the `sof_toml_` prefix and one error enum, `sof_toml_error_t`. It reads and writes [TOML 1.1.0](https://toml.io/en/v1.1.0), and has two parts, each a type the application provides (on the stack, inside another structure or on the heap) and sets up with an init function. It is made to be used as [sof_json](../sof_json/) is: the document and its `sof_toml_value_` functions, and the writer, work as sof_json's do, with TOML's types.

The document, `sof_toml_doc_t` in _sof_toml_doc.c_, holds a TOML text parsed whole, as a tree of `sof_toml_value_t`. `sof_toml_doc_init()` sets it up, `sof_toml_doc_parse()` parses a text into it, and `sof_toml_doc_read()` reads one through a read function, such as `sof_toml_read_file()` for a `FILE *`, and parses it. Its root table is `root`. `sof_toml_value_type()` gives a value's type; `sof_toml_value_string()`, `sof_toml_value_int()`, `sof_toml_value_double()`, `sof_toml_value_bool()` and `sof_toml_value_datetime()` its contents; `sof_toml_value_count()`, `sof_toml_value_at()` and `sof_toml_value_member()` an array's elements and a table's members; `sof_toml_value_get()` and `sof_toml_value_get_bytes()` a member's value by its key; and `sof_toml_value_path()` a value by a dotted key with array indexes, such as `"servers.alpha.ports[0]"`. Each value's `span`, and each member's `key_span`, say where it is in the text, as a byte offset and length, for an application that keeps the text: `sof_string_position()`, from [sof_string](../sof_string/), turns an offset into a line and column, and the text can be changed in place at the spans, keeping its comments and layout (see Spans). `sof_toml_datetime_parse()` and `sof_toml_datetime_format()` read and write a date or time on its own. `sof_toml_doc_clear()` empties the document, and `sof_toml_doc_free()` releases its memory.

The writer, `sof_toml_writer_t` in _sof_toml_writer.c_, writes TOML one piece at a time. `sof_toml_writer_init()` sets it up with flags, an optional write function and an optional allocator; the document's root table is open from the start. `sof_toml_writer_key()` or `sof_toml_writer_key_bytes()` gives each member's key, and `sof_toml_writer_string()`, `sof_toml_writer_string_bytes()`, `sof_toml_writer_int()`, `sof_toml_writer_double()`, `sof_toml_writer_bool()` and `sof_toml_writer_datetime()` write values. `sof_toml_writer_table_begin()` and `sof_toml_writer_table_end()` write a table: a section of its own, under a `[header]`, after a key in a table that is one, or an inline table, `{...}`, anywhere else. `sof_toml_writer_table_array_begin()` and `sof_toml_writer_array_end()` write an array of tables as `[[header]]` sections, and `sof_toml_writer_array_begin()` and `sof_toml_writer_inline_begin()` an inline array and an inline table wherever a value goes. `sof_toml_writer_value()` writes a value from a document. `sof_toml_writer_output()` returns what is in the buffer, `sof_toml_writer_flush()` passes it to the write function, `sof_toml_writer_depth()` says how many tables and arrays are open and `sof_toml_writer_complete()` whether the output is whole. `sof_toml_writer_clear()` starts again, empty, and `sof_toml_writer_free()` releases the memory once the application is finished.

There is no streaming reader, as sof_json has. TOML lets a table be defined in pieces, anywhere in a text, by headers and dotted keys, and whether a key or a table is defined twice, which makes a text invalid, can't be known until the whole text is read; so a TOML text is read whole, into a document. Configuration files, which TOML is for, are small enough that this is no loss.

The parser reads the text once, from start to end, skipping runs of plain bytes in strings and comments 8 at a time, and building the tree as it goes, in an arena from [sof_mem](../sof_mem/), so a document allocates in a few large blocks, and clearing or freeing it gives them back at once. A table's members are kept in the order they were defined, in an array that grows as headers anywhere in the text add to it. Tables with more than **SOF_TOML_INDEX_MIN** (8) members are also put in an index, one [sof_hashmap](../sof_hashmap/) for the whole document, which the parser uses to find a key already defined, and `sof_toml_value_get()` to find a key without looking at each member. Arrays and inline tables are read by recursion, to **SOF_TOML_DEPTH_MAX** (256) deep; dotted keys and table headers, without it, to any depth. The values belong to the document, and are good until it parses again, is cleared or is freed.

The writer keeps what is open as a stack of frames, the root table at the bottom, and a key until its value is written, so that a key/value pair, or a section's header, is written in one piece. A table's header is written before its first key/value pair, and a table that holds only tables gets none, since theirs define it. A key/value pair after a table's sub-tables would fall in the last sub-table's section, and TOML has no way to write it, so it is refused, with **SOF_TOML_ERROR**, and writes nothing, as does every misplaced piece, and every call that runs out of memory, which can then be made again: what the writer has written is always valid TOML so far. `sof_toml_writer_value()` writes a table's key/value pairs before its tables, so it never meets that. Output goes into a `sof_string_t` buffer. Without a write function, it all stays there, for the application to read with `sof_toml_writer_output()`. With one, the buffer is passed to it whenever it passes **SOF_TOML_FLUSH_SIZE** (4096) bytes, and by `sof_toml_writer_flush()`, which the application calls at the end.

The library's demo is the [tomljson](demo/) program in _demo/_, which converts TOML to JSON with a document and a [sof_json](../sof_json/) writer, and JSON to TOML with a sof_json document and the writer, plain or as toml-test's tagged JSON. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/); they read the tests of [toml-test](../test-data/toml-test/), the TOML project's own suite, from _../test-data/_, and its JSON with [sof_json](../sof_json/), which only the tests use. All sof_toml declarations are in _sof_toml.h_. The document's arena is from the [sof_mem](../sof_mem/) library in _../sof_mem/_ (_sof_mem_arena.c_, _sof_mem.h_ and _sof_mem_internal.h_; its other allocators aren't needed), and its index is a [sof_hashmap](../sof_hashmap/) in _../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), hashing keys with [sof_hash](../sof_hash/) in _../sof_hash/_ (_sof_hash.c_, _sof_hash.h_ and the vendored _rapidhash.h_). The writer's buffer, and the document's decoded strings, are strings from the [sof_string](../sof_string/) library in _../sof_string/_ (_sof_string.c_ and _sof_string.h_), the record of what is open is a stack from [sof_stack](../sof_stack/) in _../sof_stack/_ (_sof_stack.c_ and _sof_stack.h_), and UTF-8 is checked, decoded and encoded with the [sof_utf](../sof_utf/) library in _../sof_utf/_ (_sof_utf.h_, which is all of it). Floats are converted and written with the [sof_real](../sof_real/) library in _../sof_real/_ (_sof_real.c_, _sof_real.h_ and _sof_real_internal.h_, with _sof_bit.h_ from [sof_bit](../sof_bit/) in _../sof_bit/_). Memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond those, the library needs nothing but the c standard library.

Reading:

    sof_toml_doc_t d;
    sof_toml_doc_init(&d, NULL);
    if (sof_toml_doc_read(&d, sof_toml_read_file, f) != SOF_TOML_OK) {
        printf("line %zu, column %zu: %s\n", d.line, d.column, d.message);   // "a key is defined twice"
    }
    title = sof_toml_value_string(sof_toml_value_get(d.root, "title"), NULL);
    sof_toml_value_int(sof_toml_value_path(d.root, "servers.alpha.port"), &port);
    sof_toml_value_datetime(sof_toml_value_path(d.root, "owner.born"), &born);    // born.year is 1979
    sof_toml_doc_free(&d);

Writing:

    sof_toml_writer_t w;
    sof_toml_writer_init(&w, 0, write_out, NULL, NULL);       // write_out() gets the output
    sof_toml_writer_key(&w, "title");
    sof_toml_writer_string(&w, "caf\xc3\xa9");                   // UTF-8, written as it is
    sof_toml_writer_key(&w, "owner");
    sof_toml_writer_table_begin(&w);                           // [owner], at its first key
    sof_toml_writer_key(&w, "sizes");
    sof_toml_writer_array_begin(&w);
    sof_toml_writer_int(&w, 8);
    sof_toml_writer_double(&w, 0.5);
    sof_toml_writer_array_end(&w);
    sof_toml_writer_table_end(&w);
    sof_toml_writer_key(&w, "fruit");
    sof_toml_writer_table_array_begin(&w);
    sof_toml_writer_table_begin(&w);                           // [[fruit]]
    sof_toml_writer_key(&w, "point");
    sof_toml_writer_inline_begin(&w);
    sof_toml_writer_key(&w, "x");
    sof_toml_writer_int(&w, 1);
    sof_toml_writer_table_end(&w);                             // ends the inline table
    sof_toml_writer_table_end(&w);                             // ends the [[fruit]] table
    sof_toml_writer_array_end(&w);
    sof_toml_writer_flush(&w);                                 // passes the document to write_out()
    sof_toml_writer_free(&w);

which writes:

    title = "café"

    [owner]
    sizes = [8, 0.5]

    [[fruit]]
    point = {x = 1}

## Building

    make             # builds the library's objects, ./test and the demo, demo/tomljson
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

The tests check the document's every value function on a sample of everything; tables defined by headers, dotted keys and both, in pieces and in any order, arrays of tables with tables in them, and fourteen ways of defining a key or table twice, or adding to a table that is closed; every kind of string, every escape TOML 1.1 has, line-ending backslashes, CRLF in multi-line strings, NULs in keys and strings, and strings that aren't valid; strings of each of the four kinds, and comments, of up to 40 plain bytes with a quote, escape, tab, newline, CR, UTF-8, DEL or control character at each place and each of 8 alignments, for their text, span and where they fail; integers at their limits, in each base, with underscores, and one past them; floats, inf and nan, a float of 300 digits, and twenty-one numbers that aren't valid; floats at the edges of the conversion without `strtod()` and 50,000 random ones with signs, underscores, trailing zeros and exponents, each converted to exactly what `strtod()` gives; dates and times of each kind, without seconds, with a space for the T, leap days, and sixteen that aren't valid, and `sof_toml_datetime_parse()` and `sof_toml_datetime_format()`; errors of each kind, with their line, column and message; nesting to **SOF_TOML_DEPTH_MAX** and one past it, and a header 5,000 keys deep; the index, in twenty tables of up to 57 members, with a key defined twice; paths, with every kind of key and index; the spans of every kind of value and key, tables with headers, with none and named on the way to others, arrays of tables, a byte order mark and CRLF lines, with line numbers from them, and a dependency added and another removed by splicing the text at them, keeping its comments, and the result parsed again; reading through a read function, a byte at a time and failing at each call, and from a file; a text of 100 KB read in pieces from a byte to all of it, and with each allocation failing in turn; **NULL** and freed documents and values; and parsing with each allocation failing in turn. For the writer, they check the exact output of sections, nested and empty, arrays of tables, inline arrays and tables, and an empty array of tables; every escape, keys bare and quoted, invalid UTF-8, and **SOF_TOML_ASCII**; strings and quoted keys of up to 40 plain bytes with a byte that needs escaping, UTF-8 or invalid UTF-8 at each place and each of 8 alignments, against the escapes made a byte at a time; integers at their limits, doubles that need few digits and many, inf and nan, and 2,000 random doubles read back exactly; doubles at the edges of the ways they are written and 4,000 random subnormals and powers of ten, with exactly as many digits as they need; that every misplaced call is refused and writes nothing; the write function, with output passed on in pieces and a failing flush; `sof_toml_writer_value()` with documents written much as they were read, and in each place a value goes; **NULL** and freed writers; and each allocation failing in turn, for `sof_toml_writer_value()` and the calls one at a time, each made again. 200 random documents, written by the writer, are read by the document and written again exactly as they were. Nothing leaks, and every block is freed with its size.

Then they run [toml-test](../test-data/toml-test/) v2.2.0, the TOML project's own suite, kept unchanged in _../test-data/_: for TOML 1.1.0, the document must read each of its 214 valid documents as the tagged JSON beside it says, compared as toml-test compares them, floats by value and dates by the moment they stand for, and must refuse each of its 467 invalid ones, with a line, column and message. Each valid document, written again by the writer, as it is and as ASCII, must read as its JSON says, and so must its JSON, written as TOML by the writer as an encoder would, which tests the writer against the suite's tests for encoders. The demo, `tomljson -t` and `tomljson -j -t`, is a decoder and an encoder toml-test's own runner can test.

To use the library in another project, copy _sof_toml_doc.c_, _sof_toml_writer.c_ and _sof_toml.h_, with _sof_mem_arena.c_, _sof_mem.h_ and _sof_mem_internal.h_ from _../sof_mem/_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../sof_hash/_, _sof_string.c_ and _sof_string.h_ from _../sof_string/_, _sof_stack.c_ and _sof_stack.h_ from _../sof_stack/_, _sof_utf.h_ from _../sof_utf/_, _sof_real.c_, _sof_real.h_ and _sof_real_internal.h_ from _../sof_real/_, _sof_bit.h_ from _../sof_bit/_, and _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into it, and compile the _.c_ files with the project's own sources. A project that only writes TOML still needs _sof_toml_doc.c_, for `sof_toml_datetime_format()`, but not the arena or hashmap's _.c_ files. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, with sof_json's files (_sof_json_doc.c_, _sof_json_reader.c_ and _sof_json.h_ from _../sof_json/_), and compile _test.c_ and the libraries' _.c_ files with _-pthread_; they read toml-test's files from _../test-data/toml-test/_.

## Input

The document reads TOML 1.1.0, as its specification and ABNF grammar define it, and nothing else. A text is UTF-8: bytes that aren't, anywhere, are an error, though a byte order mark at its start is skipped. Lines end with LF or CRLF; a CR alone is an error. Comments run from `#` to the end of the line, and can hold any character but the control characters other than tab, as can strings, where they must be escaped. An empty text, or one of only whitespace and comments, is an empty table.

Keys are bare, of letters, digits, `-` and `_`, or quoted as basic or literal strings, which can be empty. A dotted key, `a.b.c = 1`, with whitespace allowed around the dots, puts the value in table `c` of table `b` of table `a`, making them if they aren't there; `1.2 = 3` is the key `2` in the table `1`. Strings are basic, `"..."`, with the escapes `\b`, `\t`, `\n`, `\f`, `\r`, `\e`, `\"`, `\\`, `\x` with two hex digits, `\u` with four and `\U` with eight, each for a Unicode scalar value; literal, `'...'`, with none; and multi-line, `"""..."""` and `'''...'''`, where a newline straight after the opening quotes is dropped, each CRLF is read as LF, one or two quotes can come before the closing three, and in the basic kind, a backslash at the end of a line drops it and the whitespace and newlines after it. Strings are returned decoded, as UTF-8; a `\x00` or `\u0000` is a NUL, so a string's length, not its NUL, says where it ends.

Integers are decimal, with an optional sign and no leading zeros, or hexadecimal, octal or binary after `0x`, `0o` or `0b`, without a sign, and digits can have single underscores between them; an integer outside the range of `int64_t` is **SOF_TOML_RANGE**, since TOML requires one that can't be kept exactly to be an error. Floats are an integer part in decimal and a fraction, an exponent or both, `inf` or `nan`, with an optional sign; they are converted to the nearest double, whatever the locale's decimal point, those whose digits without the zeros around them are an integer below 2^53, with a power of ten of at most 22 either way, by one exact multiplication or division without `strtod()`, and one too large for a double is **SOF_TOML_RANGE**. Dates and times are an offset date-time, `1979-05-27T07:32:00Z` or `1979-05-27 00:32:00.999999-07:00`, a local date-time, a local date or a local time; the T can be a t or a space, Z a z, the seconds can be left out, and a fraction of a second can have any number of digits, of which the first nine are kept and the rest dropped. Each field must be in its range, February 29 only in a leap year; a second can be 60, for a leap second. Booleans are `true` and `false`.

A key can be defined once in a table. A table can be defined once, by a `[header]` of its own; a header with a dotted key makes the tables before its last key if they aren't there, and a header for one of them may come later, once. Tables made by dotted keys can be added to by more dotted keys in the same table, and headers can define tables inside them, but not them. An inline table, `{...}`, is whole when it ends, and can't be added to by headers or dotted keys; TOML 1.1 lets it span lines, with comments, and end with a comma. An inline array is whole too, so `[[header]]` can't add to it; arrays can span lines, hold values of different types, and end with a comma. `[[header]]` adds a table to an array of tables, and `[header]` and dotted keys after it go in the table last added. Each of these that is broken is **SOF_TOML_DUPLICATE**; the toml-test suite's 467 invalid documents include them all.

At the first error, the document is left empty, and `message`, `line`, `column` and `offset` say what it is and where. Line and column numbers count from **1**, lines at each LF, and columns in bytes. The offset counts bytes from **0**, from the start of the text.

## Output

The writer writes the document's root table first, its key/value pairs one to a line as `key = value`, then each table that is a section of its own under its header, after a blank line: `[a.b]` for a table, the keys of the tables it is in before its own, and `[[a]]` for each table of an array of tables. A table is written as a section when it is begun after a key in a table that is one, which the root is; anywhere else, in an inline array or table, it is written inline, as is anything begun with `sof_toml_writer_inline_begin()`. Inline arrays and tables are written on one line, as `[1, 2]` and `{a = 1, b = 2}`, or `[]` and `{}`. An array of tables with no tables is written as `key = []` in the table it is in.

Keys are written bare if they can be, and otherwise quoted as strings are. Strings are written in double quotes, with `"` and `\` escaped, and the control characters, DEL included, written as `\b`, `\t`, `\n`, `\f`, `\r`, or a `\u` escape, in upper case hex; nothing else is escaped. Characters outside ASCII are written as they are, or with **SOF_TOML_ASCII**, as `\u` escapes, or `\U` past U+FFFF. Bytes that aren't valid UTF-8 are written as U+FFFD, one for each error as `sof_utf8_decode()` finds them, so the output is always valid UTF-8. The writer writes only escapes TOML 1.0 has, so what it writes can be read by a TOML 1.0 parser too, as long as inline tables are on one line, which they are.

Integers are written in full, in decimal. A double is written by `sof_real_format()`, with the fewest significant digits, up to 17, that read back as the same double, and of those the closest to it, and always as a float: 1.0, 0.1, 1e+100, 5e-324, -0.0, and 100.0 rather than 1e+02. The decimal point is always `.`, whatever the locale. Infinities are `inf` and `-inf`, and NaNs `nan`. Dates and times are written as `sof_toml_datetime_format()` formats them: `1979-05-27T07:32:00Z`, `1979-05-27T07:32:00.5-08:00`, `1979-05-27`, `07:32:00`, with the seconds always, and a fraction without trailing zeros.

The writer doesn't check that keys are unique, or that a table isn't written twice; a document it writes with `sof_toml_writer_value()` has neither.

## Documents

A table's members are kept in the order they were defined, as `sof_toml_value_member()` returns them: a table that a header defines after its sub-tables were defined is where the first of them made it. A member's key is one key: a dotted key or header puts each of its parts in a table of its own, so `a.b = 1` gives the root a member `a`, a table with a member `b`. `sof_toml_value_get()` finds a key in one table; `sof_toml_value_path()` follows a dotted key, as TOML writes one, with array indexes in brackets after any part, so `"products[1].name"`, `"site.\"google.com\""` and `"list[0][2]"` all go where they look as if they would. An index is decimal digits; `sof_toml_value_path(v, "")` is `v`.

Each table and array remembers, in `is_inline`, whether the text wrote it inline. `sof_toml_writer_value()` writes those inline again, and the rest as sections, a table's key/value pairs before its tables, so a document read and written again is the same document, written much as it was. Comments, blank lines, and whether a key was quoted or a string literal, aren't kept, but the spans say where each value and key is in the text, so an application that must keep them changes the text itself.

`sof_toml_value_double()` gives an integer's value as well as a float's, so a setting such as `ratio = 1` can be read either way; `sof_toml_value_int()` gives only integers.

## Spans

A span is an `offset`, in bytes from the start of the text the document was parsed from, a byte order mark included, and a `length`. Each value has one, in `span`, and each member of a table has one for its key, in `key_span`:

- A string, number, boolean, or date or time is its text as written, a string's quotes included.
- An inline array or table runs from its `[` or `{` to its `]` or `}`.
- A table with a header runs from the header's `[` to the end of the last value of a key/value pair under it, or is the header alone if there are none. Comments and blank lines after that value aren't in it, so the end of the span is where a new key/value line goes, as `\n` and the line. Tables defined by later headers, such as `[a.b]` after `[a]`, have spans of their own and don't stretch it.
- The root table runs the same way from the start of the text, to its last key/value pair before the first header.
- An array of tables runs from its first `[[header]]` to the end of its last table, and each table in it as a table with a header.
- A table that has no text of its own, made by a dotted key (`a` in `a.b = 1`) or named on the way to another in a header (`a` in `[a.b]`), has length **0**, at the key that first named it, until a header of its own defines it.
- A key is one part as written, quotes included: in `"a".b = 1`, member `a`'s is `"a"` and member `b`'s `b`. A table's key is where it was first named, even when a header later defines it.

The document doesn't keep the text, so an application that uses the spans parses a text it holds, with `sof_toml_doc_parse()`. To find the line a value is on, give its offset to `sof_string_position()`. To change one value, replace the text of its span. To remove a key/value pair, remove from the start of the line its key is on through the line feed that ends its value's last line, which takes a comment at the end of that line with it and leaves those on other lines. To add one to a table, insert a line feed and the line at the end of the table's span. `sof_string_delete()` and `sof_string_insert()` make these changes to a text kept in a `sof_string_t`. Splice from the end of the text back, or parse again after each change, since an edit moves the offsets after it.

    size_t line;
    sof_string_t text = sof_string_view(manifest, length);
    const sof_toml_value_t *deps = sof_toml_value_get(d.root, "dependencies");
    const sof_toml_member_t *m = sof_toml_value_member(deps, 0);
    sof_string_position(&text, m->key_span.offset, &line, NULL);     // the line it is declared on
    size_t end = deps->span.offset + deps->span.length;            // insert "\nname = ..." here

## Functions

`sof_toml_error_t sof_toml_doc_init(sof_toml_doc_t *d, const sof_alloc_t *alloc)`

Initialize the empty document _d_. _alloc_ is the allocator for its arena, index and the rest, or **NULL** for the standard functions (see [sof_alloc](../sof_alloc/)); it is copied, so it can be on the stack. Nothing is allocated until a text is parsed. Returns **SOF_TOML_OK**, or **SOF_TOML_ERROR** if _d_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the document is left empty and not initialized.

`sof_toml_error_t sof_toml_doc_parse(sof_toml_doc_t *d, const void *text, size_t length)`

Parse the _length_ bytes at _text_, a TOML 1.1.0 document in UTF-8, into _d_, replacing what it held, with the root table in `root`. _text_ can be **NULL** if _length_ is **0**, which is an empty table. Returns **SOF_TOML_OK**; **SOF_TOML_NO_MEM**; **SOF_TOML_SYNTAX** if the text isn't TOML, **SOF_TOML_DUPLICATE** if it defines a key or table twice, **SOF_TOML_TOO_DEEP** if arrays and inline tables nest more than **SOF_TOML_DEPTH_MAX** deep, or **SOF_TOML_RANGE** if an integer doesn't fit an `int64_t` or a float a double, each with what is wrong in `message` and where in `line`, `column` and `offset`; or **SOF_TOML_ERROR** if _d_ is **NULL** or not initialized, or _text_ is **NULL** and _length_ isn't **0**. On failure the document is empty.

`sof_toml_error_t sof_toml_doc_read(sof_toml_doc_t *d, sof_toml_read_func_t read, void *ctx)`

Read a TOML text to its end through the function _read_, such as `sof_toml_read_file()`, which is passed _ctx_, such as a `FILE *`, at least **SOF_TOML_READ_SIZE** bytes at a time, straight into the string that holds the text, and parse it into _d_ as `sof_toml_doc_parse()` does. The text is kept only while it is parsed, so an application that will use the values' spans reads the text itself and calls `sof_toml_doc_parse()`. Returns as `sof_toml_doc_parse()` does, and **SOF_TOML_READ** if the read function failed, or **SOF_TOML_ERROR** if _read_ is **NULL**.

`int sof_toml_read_file(char *buffer, size_t size, size_t *length, void *ctx)`

A read function for a `FILE *`, given as _ctx_. It reads up to _size_ bytes into _buffer_ with `fread()`, and stores how many in _length_, **0** at the end of the file. It never closes the file. Returns **0**, or **1** if the file couldn't be read or _ctx_ is **NULL**.

`sof_toml_type_t sof_toml_value_type(const sof_toml_value_t *v)`

Return the type of _v_, **SOF_TOML_STRING** to **SOF_TOML_TABLE**, or **SOF_TOML_NONE** if _v_ is **NULL**.

`const char *sof_toml_value_string(const sof_toml_value_t *v, size_t *length)`

Return the bytes of the string _v_, decoded UTF-8 that can hold NULs, followed by a NUL, and if _length_ isn't **NULL**, store their number there, not counting the NUL. Returns **NULL**, with a _length_ of **0**, if _v_ is **NULL** or not a string.

`sof_toml_error_t sof_toml_value_int(const sof_toml_value_t *v, int64_t *value)`

Store the integer _v_ in _value_. Returns **SOF_TOML_OK**, **SOF_TOML_NO_MATCH** if _v_ is **NULL** or not an integer, or **SOF_TOML_ERROR** if _value_ is **NULL**. On failure _value_ is left alone.

`sof_toml_error_t sof_toml_value_double(const sof_toml_value_t *v, double *value)`

Store the float _v_ in _value_, or the integer _v_ converted, to the nearest double if it has more than 53 bits. Returns **SOF_TOML_OK**, **SOF_TOML_NO_MATCH** if _v_ is **NULL** or neither a float nor an integer, or **SOF_TOML_ERROR** if _value_ is **NULL**.

`sof_toml_error_t sof_toml_value_bool(const sof_toml_value_t *v, int *value)`

Store **1** in _value_ if _v_ is true, or **0** if it is false. Returns **SOF_TOML_OK**, **SOF_TOML_NO_MATCH** if _v_ is **NULL** or not a boolean, or **SOF_TOML_ERROR** if _value_ is **NULL**.

`sof_toml_error_t sof_toml_value_datetime(const sof_toml_value_t *v, sof_toml_datetime_t *value)`

Store the date and time _v_, of any of the four types, in _value_; `sof_toml_value_type()` says which of its fields mean something. Returns **SOF_TOML_OK**, **SOF_TOML_NO_MATCH** if _v_ is **NULL** or not a date or time, or **SOF_TOML_ERROR** if _value_ is **NULL**.

`size_t sof_toml_value_count(const sof_toml_value_t *v)`

Return the number of elements in the array _v_ or members in the table _v_, or **0** if _v_ is **NULL** or neither.

`const sof_toml_value_t *sof_toml_value_at(const sof_toml_value_t *array, size_t index)`

Return the element of _array_ at _index_, counting from **0**, or **NULL** if _array_ is **NULL** or not an array, or _index_ is past its end.

`const sof_toml_member_t *sof_toml_value_member(const sof_toml_value_t *table, size_t index)`

Return the member of _table_ at _index_, in the order the members were defined, counting from **0**, or **NULL** if _table_ is **NULL** or not a table, or _index_ is past its end.

`const sof_toml_value_t *sof_toml_value_get(const sof_toml_value_t *table, const char *key)`

Return the value of the member of _table_ whose key is the NUL-terminated UTF-8 _key_: one key, so `"a.b"` is a key with a full stop in it. Returns **NULL** if no member has the key, _table_ is **NULL** or not a table, or _key_ is **NULL**.

`const sof_toml_value_t *sof_toml_value_get_bytes(const sof_toml_value_t *table, const void *key, size_t length)`

Find a member as `sof_toml_value_get()` does, by the _length_ bytes at _key_, which can hold NULs. _key_ can be **NULL** if _length_ is **0**, for the empty key. Returns as `sof_toml_value_get()` does.

`const sof_toml_value_t *sof_toml_value_path(const sof_toml_value_t *v, const char *path)`

Return the value the NUL-terminated _path_, a TOML key with array indexes in brackets after any part, such as `"servers.alpha.ports[0]"`, finds from _v_, as Documents describes. Returns **NULL** if there is none, _path_ isn't a path, or _v_ or _path_ is **NULL**.

`sof_toml_error_t sof_toml_datetime_parse(const char *text, size_t length, sof_toml_type_t *type, sof_toml_datetime_t *dt)`

Parse the _length_ bytes at _text_, which needn't be NUL-terminated, as a date and time as TOML writes them, all of it, as Input describes, storing its type in _type_, **SOF_TOML_DATETIME**, **SOF_TOML_DATETIME_LOCAL**, **SOF_TOML_DATE_LOCAL** or **SOF_TOML_TIME_LOCAL**, and its fields in _dt_, those the type doesn't use **0**. Returns **SOF_TOML_OK**; **SOF_TOML_SYNTAX** if the text isn't a date or time, or a field is out of its range; or **SOF_TOML_ERROR** if _text_, _type_ or _dt_ is **NULL**. On failure _type_ and _dt_ are left alone.

`size_t sof_toml_datetime_format(sof_toml_type_t type, const sof_toml_datetime_t *dt, char *buffer, size_t size)`

Write the fields of _dt_ that _type_ uses, as Output describes, in _buffer_, which has room for _size_ bytes, followed by a NUL. **SOF_TOML_DATETIME_SIZE** (40) is always room enough. Returns the length of the text, not counting the NUL, or **0**, writing nothing, if _type_ isn't one of the four, a field it uses is out of its range, _dt_ or _buffer_ is **NULL**, or the text doesn't fit.

`sof_toml_error_t sof_toml_doc_clear(sof_toml_doc_t *d)`

Empty the document, so `root` is **NULL**, keeping its memory for the next text. Returns **SOF_TOML_OK**, or **SOF_TOML_ERROR** if _d_ is **NULL** or not initialized.

`sof_toml_error_t sof_toml_doc_free(sof_toml_doc_t *d)`

Release the document's memory. The `sof_toml_doc_t` itself isn't freed; it is left empty, and acts like a document whose `sof_toml_doc_init()` failed until it is initialized again. Returns **SOF_TOML_OK**, also for a document not initialized, or **SOF_TOML_ERROR** if _d_ is **NULL**.

`sof_toml_error_t sof_toml_writer_init(sof_toml_writer_t *w, unsigned flags, sof_toml_write_func_t write, void *ctx, const sof_alloc_t *alloc)`

Initialize the writer _w_, with the document's root table open. _flags_ is **0**, or **SOF_TOML_ASCII** to write every character outside ASCII as an escape. _write_ is the function to pass output to, or **NULL** to keep it all in the buffer, and _ctx_ is passed to it. _alloc_ is the allocator for the buffer and the record of what is open, or **NULL** for the standard functions; it is copied. Returns **SOF_TOML_OK**, **SOF_TOML_NO_MEM**, or **SOF_TOML_ERROR** if _w_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the writer is left empty and not initialized.

`sof_toml_error_t sof_toml_writer_key(sof_toml_writer_t *w, const char *key)`

Give the NUL-terminated UTF-8 _key_ of the next member of the table or inline table open last. Nothing is written until its value is; the key is then written bare if it is letters, digits, `-` and `_`, and otherwise quoted and escaped as a string is. The writer doesn't check that keys are unique. Returns **SOF_TOML_OK**, **SOF_TOML_NO_MEM**, or **SOF_TOML_ERROR** if _w_ or _key_ is **NULL**, _w_ is not initialized, what is open last isn't a table or inline table, or a key is already waiting. On failure no key is kept.

`sof_toml_error_t sof_toml_writer_key_bytes(sof_toml_writer_t *w, const void *key, size_t length)`

Give a key as `sof_toml_writer_key()` does, from the _length_ bytes at _key_, which can hold NULs, written as an escape. _key_ can be **NULL** if _length_ is **0**, for the empty key, `""`. Returns as `sof_toml_writer_key()` does, and **SOF_TOML_ERROR** if _key_ is **NULL** and _length_ isn't **0**.

`sof_toml_error_t sof_toml_writer_string(sof_toml_writer_t *w, const char *s)`

Write the NUL-terminated UTF-8 string _s_, in double quotes and escaped as Output describes. Like every value, it goes after a key, in a table or inline table, or as an element of an inline array; in a table written as a section, it is written as `key = value` on a line of its own, after the table's header if this is its first key/value pair. Returns **SOF_TOML_OK**, **SOF_TOML_NO_MEM**, or **SOF_TOML_ERROR** if _w_ or _s_ is **NULL**, _w_ is not initialized, or a value can't go here: in a table with no key waiting, in a table after a section of its own was begun, or in an array of tables. On failure nothing is written.

`sof_toml_error_t sof_toml_writer_string_bytes(sof_toml_writer_t *w, const void *s, size_t length)`

Write a string as `sof_toml_writer_string()` does, from the _length_ bytes at _s_, which can hold NULs, written as escapes. _s_ can be **NULL** if _length_ is **0**, for an empty string. Returns as `sof_toml_writer_string()` does, and **SOF_TOML_ERROR** if _s_ is **NULL** and _length_ isn't **0**.

`sof_toml_error_t sof_toml_writer_int(sof_toml_writer_t *w, int64_t value)`

Write the integer _value_, in full. Returns as `sof_toml_writer_string()` does.

`sof_toml_error_t sof_toml_writer_double(sof_toml_writer_t *w, double value)`

Write _value_ as a float, with the fewest significant digits that read back as it, or as `inf`, `-inf` or `nan`, as Output describes. Returns as `sof_toml_writer_string()` does.

`sof_toml_error_t sof_toml_writer_bool(sof_toml_writer_t *w, int value)`

Write `true` if _value_ isn't **0**, or `false`. Returns as `sof_toml_writer_string()` does.

`sof_toml_error_t sof_toml_writer_datetime(sof_toml_writer_t *w, sof_toml_type_t type, const sof_toml_datetime_t *dt)`

Write the fields of _dt_ that _type_ uses, **SOF_TOML_DATETIME**, **SOF_TOML_DATETIME_LOCAL**, **SOF_TOML_DATE_LOCAL** or **SOF_TOML_TIME_LOCAL**, as `sof_toml_datetime_format()` formats them. Returns as `sof_toml_writer_string()` does, and **SOF_TOML_ERROR** if _dt_ is **NULL**, _type_ isn't one of the four, or a field it uses is out of its range.

`sof_toml_error_t sof_toml_writer_table_begin(sof_toml_writer_t *w)`

Begin a table. After a key, in a table written as a section, it is a section of its own, under a header of the keys of the tables it is in and its own, `[a.b]`, written before its first key/value pair, or at its end if it has none and no sections in it; the table it is in can then have no more key/value pairs. In an array of tables, it is the array's next table, under a `[[a]]` header written now. Anywhere else a value goes, it is an inline table, as `sof_toml_writer_inline_begin()` begins. Members written until `sof_toml_writer_table_end()` are its own. Returns **SOF_TOML_OK**, **SOF_TOML_NO_MEM**, or **SOF_TOML_ERROR** if _w_ is **NULL** or not initialized, or a table can't go here. On failure nothing is written.

`sof_toml_error_t sof_toml_writer_table_end(sof_toml_writer_t *w)`

End the table, or inline table, begun last. Returns **SOF_TOML_OK**, **SOF_TOML_NO_MEM**, or **SOF_TOML_ERROR** if _w_ is **NULL** or not initialized, what is open last isn't a table the application began, or a key is waiting for its value. On failure nothing is written and the table stays open.

`sof_toml_error_t sof_toml_writer_table_array_begin(sof_toml_writer_t *w)`

Begin an array of tables. After a key, in a table written as a section, its tables, each begun with `sof_toml_writer_table_begin()`, are written as `[[key]]` sections, and nothing else can go in it, until `sof_toml_writer_array_end()`. Anywhere else a value goes, it begins an inline array, as `sof_toml_writer_array_begin()` does. Returns as `sof_toml_writer_table_begin()` does.

`sof_toml_error_t sof_toml_writer_array_begin(sof_toml_writer_t *w)`

Begin an inline array, where a value can go. Values written until `sof_toml_writer_array_end()` are its elements, and tables begun in it are inline tables. Returns as `sof_toml_writer_table_begin()` does.

`sof_toml_error_t sof_toml_writer_inline_begin(sof_toml_writer_t *w)`

Begin an inline table, where a value can go, whatever is open. Its members, a key and then a value each, are written on one line, `{a = 1, b = 2}`, until `sof_toml_writer_table_end()`. Returns as `sof_toml_writer_table_begin()` does.

`sof_toml_error_t sof_toml_writer_array_end(sof_toml_writer_t *w)`

End the inline array, or array of tables, begun last. An array of tables with no tables is written as `key = []` in the table it is in, if that table has had no section of its own begun. Returns **SOF_TOML_OK**, **SOF_TOML_NO_MEM**, or **SOF_TOML_ERROR** if _w_ is **NULL** or not initialized, what is open last isn't an array, a key is waiting, or it is an empty array of tables that can't be written. On failure nothing is written and the array stays open.

`sof_toml_error_t sof_toml_writer_value(sof_toml_writer_t *w, const sof_toml_value_t *v)`

Write the value _v_, from a document, with all it holds, where a value can go, as the calls for each of its parts would write it. After a key in a table written as a section, a table is written as a section, with its key/value pairs first and then its tables and arrays of tables, and an array of tables as `[[key]]` sections, unless the text wrote them inline (`is_inline`). A table given with no key waiting, where a table written as a section is open, such as the root, has its members written in that table, so `sof_toml_writer_value(w, d.root)` writes a whole document. The whole value is built in the buffer before any of it is passed to the write function, so if a call fails, nothing is written, and the key waiting, if there was one, still is. Returns as `sof_toml_writer_string()` does, and **SOF_TOML_ERROR** if _v_ is **NULL**.

`const char *sof_toml_writer_output(const sof_toml_writer_t *w, size_t *length)`

Return the output in the buffer, followed by a NUL, and if _length_ isn't **NULL**, store its length in bytes there. With a write function, this is what hasn't been passed to it yet. The output belongs to the writer, and is good until the next call that writes, flushes, clears or frees. Returns **NULL**, and a _length_ of **0**, if _w_ is **NULL** or not initialized.

`sof_toml_error_t sof_toml_writer_flush(sof_toml_writer_t *w)`

Pass the buffer to the write function and empty it. Output can be flushed at any point; the application calls it once the document is written. Does nothing if there is no write function or the buffer is empty. Returns **SOF_TOML_OK**, **SOF_TOML_WRITE** if the write function failed, which leaves the buffer as it was, or **SOF_TOML_ERROR** if _w_ is **NULL** or not initialized.

`size_t sof_toml_writer_depth(const sof_toml_writer_t *w)`

Return the number of tables and arrays that are open, not counting the root table: **0** at the root, and also if _w_ is **NULL** or not initialized.

`int sof_toml_writer_complete(const sof_toml_writer_t *w)`

Return **1** if nothing is open but the root table and no key is waiting, so the output is a whole document, or **0**, also if _w_ is **NULL** or not initialized.

`sof_toml_error_t sof_toml_writer_clear(sof_toml_writer_t *w)`

Start again, empty: discard the buffer, without passing it to the write function, the key waiting and everything open, so the next key is the root table's. `written` goes back to **0**. The memory is kept, and so are the flags and write function. Returns **SOF_TOML_OK**, or **SOF_TOML_ERROR** if _w_ is **NULL** or not initialized.

`sof_toml_error_t sof_toml_writer_free(sof_toml_writer_t *w)`

Release the writer's memory. The buffer is discarded, not flushed: call `sof_toml_writer_flush()` first to keep it. The `sof_toml_writer_t` itself isn't freed; it is left empty, and acts like a writer whose `sof_toml_writer_init()` failed until it is initialized again. Returns **SOF_TOML_OK**, also for a writer not initialized, or **SOF_TOML_ERROR** if _w_ is **NULL**.

Every function accepts a **NULL** document or writer, and one that was freed or whose init failed (`ready` is **0**), which holds or writes nothing: functions that change it return **SOF_TOML_ERROR**, `sof_toml_writer_output()` returns **NULL**, `sof_toml_writer_depth()` and `sof_toml_writer_complete()` return **0**, and the free functions return **SOF_TOML_OK**, or **SOF_TOML_ERROR** for **NULL**. The value functions accept a **NULL** value, so lookups can be chained, as in `sof_toml_value_get(sof_toml_value_at(list, 2), "name")`: those that return a value, member or string return **NULL**, `sof_toml_value_count()` returns **0**, `sof_toml_value_type()` returns **SOF_TOML_NONE**, and those that give a value's contents return **SOF_TOML_NO_MATCH**, or **SOF_TOML_ERROR** for a **NULL** place to store it.

## Types

    typedef enum sof_toml_error_t {
        SOF_TOML_OK = 0,
        SOF_TOML_NO_MEM,
        SOF_TOML_NO_MATCH,              // a value that isn't there or isn't the type asked for
        SOF_TOML_ERROR,
        SOF_TOML_WRITE,                 // the write function failed; the output is kept
        SOF_TOML_READ,                  // the read function failed
        SOF_TOML_SYNTAX,                // the input isn't TOML
        SOF_TOML_DUPLICATE,             // the input defines a key or table twice
        SOF_TOML_TOO_DEEP,              // the input nests deeper than SOF_TOML_DEPTH_MAX
        SOF_TOML_RANGE,                 // an integer doesn't fit an int64_t, or a float a double
    } sof_toml_error_t;

    typedef enum sof_toml_flag_t {
        SOF_TOML_ASCII = 1,             // writer: write characters outside ASCII as escapes
    } sof_toml_flag_t;

    typedef enum sof_toml_type_t {
        SOF_TOML_NONE = 0,              // no value
        SOF_TOML_STRING,
        SOF_TOML_INTEGER,               // an int64_t
        SOF_TOML_FLOAT,                 // a double
        SOF_TOML_BOOL,
        SOF_TOML_DATETIME,              // 1979-05-27T07:32:00Z, with an offset
        SOF_TOML_DATETIME_LOCAL,        // 1979-05-27T07:32:00
        SOF_TOML_DATE_LOCAL,            // 1979-05-27
        SOF_TOML_TIME_LOCAL,            // 07:32:00
        SOF_TOML_ARRAY,
        SOF_TOML_TABLE,
    } sof_toml_type_t;

The types of a document's values.

    typedef struct sof_toml_datetime_t {
        int year;                       // 0 to 9999
        int month;                      // 1 to 12
        int day;                        // 1 to 28, 29, 30 or 31
        int hour;                       // 0 to 23
        int minute;                     // 0 to 59
        int second;                     // 0 to 60, 60 for a leap second; 0 if the text left it out
        uint32_t nanosecond;            // 0 to 999999999
        int offset;                     // minutes east of UTC, -1439 to 1439; 0 for Z
    } sof_toml_datetime_t;

A date, a time or both. Which fields mean something depends on the type: a local date has only the date, a local time only the time, and only an offset date-time an offset; the others are **0**. The application fills one in to write it.

    typedef int (*sof_toml_write_func_t)(const char *data, size_t length, void *ctx);

The write function. It is called with the _length_ bytes of output at _data_, which aren't NUL-terminated and belong to the writer, and the _ctx_ given to `sof_toml_writer_init()`. It returns **0** if every byte was written, or non-zero if they couldn't be; the writer then keeps them all in its buffer and passes them again, with whatever follows, at the next flush. A failure is reported by `sof_toml_writer_flush()`; one when the buffer passed **SOF_TOML_FLUSH_SIZE**, in the middle of writing, isn't, since the next flush will try again. It must not call into the writer.

    typedef int (*sof_toml_read_func_t)(char *buffer, size_t size, size_t *length, void *ctx);

The read function. It is called with a _buffer_ of _size_ bytes and the _ctx_ given to `sof_toml_doc_read()`, and puts up to _size_ bytes in _buffer_ and their number in _length_, which is **0** at the end of the input. It may return fewer bytes than _size_, as a pipe or socket does. It returns **0**, or non-zero if the input couldn't be read, which `sof_toml_doc_read()` returns as **SOF_TOML_READ**. It must not call into the document.

    typedef struct sof_toml_span_t {
        size_t offset;                  // the first byte's, from the start of the text
        size_t length;                  // bytes; 0 for a table with no text of its own
    } sof_toml_span_t;

Where something is in the text a document was parsed from; see Spans.

    typedef struct sof_toml_value_t {
        sof_toml_type_t type;           // SOF_TOML_STRING to SOF_TOML_TABLE
        size_t length;                  // bytes of a string, elements of an array, members of a table
        int is_inline;                  // a table or array the text wrote inline
        sof_toml_span_t span;           // where it is in the text
        union {
            int boolean;                                    // SOF_TOML_BOOL
            int64_t integer;                                // SOF_TOML_INTEGER
            double number;                                  // SOF_TOML_FLOAT
            const char *text;                               // SOF_TOML_STRING, decoded
            sof_toml_datetime_t datetime;                   // the four date and time types
            const struct sof_toml_value_t *const *elements;  // SOF_TOML_ARRAY, or NULL if it has none
            struct {
                const struct sof_toml_member_t *members;     // SOF_TOML_TABLE, in order, or NULL if it has none
                const sof_hashmap_t *index;                  // the document's index, if it has more than SOF_TOML_INDEX_MIN members
            } table;
        } as;
    } sof_toml_value_t;

    typedef struct sof_toml_member_t {
        const char *key;                // one key, decoded, UTF-8 that can hold NULs, NUL-terminated
        size_t key_length;
        sof_toml_span_t key_span;       // where the key is in the text, as written
        const sof_toml_value_t *value;
        const sof_toml_value_t *table;  // the table it is in
    } sof_toml_member_t;

A value and a table's member in a document. They belong to the document, and are good until it parses again, is cleared or is freed. The application may read the fields, or use the `sof_toml_value_` functions, which also take **NULL**, but must not change them.

    typedef struct sof_toml_doc_t {
        const sof_toml_value_t *root;   // the root table, or NULL
        sof_mem_arena_t arena;          // where the values, members, keys and strings are
        sof_hashmap_t *index;           // the index of large tables' members, in the arena, or NULL
        sof_string_t text;              // while parsing, the string or key being decoded
        sof_alloc_t alloc;              // the memory functions
        const char *message;            // after an error in the text, what it is; otherwise NULL
        size_t line;                    // after an error in the text, where it is; otherwise 0
        size_t column;
        size_t offset;
        int ready;                      // 1 once initialized; 0 when freed or failed
    } sof_toml_doc_t;

A document. The application may read the fields but must not change them; `root`, `message`, `line`, `column` and `offset` are those it will want. `message` is a string constant, a sentence without a full stop, such as `"a key is defined twice"`.

    typedef struct sof_toml_writer_t {
        sof_string_t out;               // the output not yet passed to write: all of it, if there is none
        sof_stack_t open;               // what is open, the root table first
        sof_string_t path;              // the keys of the sections open, for their headers
        sof_string_t key;               // the key waiting for its value, as it is written
        int has_key;                    // 1 while a key is waiting
        int holding;                    // 1 while sof_toml_writer_value() writes
        unsigned flags;                 // the sof_toml_flag_t flags
        sof_toml_write_func_t write;    // where output goes, or NULL
        void *ctx;                      // passed to write
        size_t written;                 // bytes passed to write
        int ready;                      // 1 once initialized; 0 when freed or failed
    } sof_toml_writer_t;

A writer. The application may read the fields but must not change them. `out.data` moves as the buffer grows, so a pointer into it is good only until the next call that writes.

## Threading

The library has no global state and does no locking. Different documents and writers can be used on different threads freely. Floats are parsed and written without the locale, so changing it on another thread doesn't matter.

A document's values never change once parsed, so the `sof_toml_value_` functions, which only read, can be called on one document from any number of threads at once, as can `sof_toml_writer_value()` with a writer each. `sof_toml_doc_parse()`, `sof_toml_doc_read()`, `sof_toml_doc_clear()` and `sof_toml_doc_free()` change the document and invalidate its values, so with a readers-writer lock they need the write lock, and no thread may keep a value once it lets go of the read lock. `sof_toml_datetime_parse()` and `sof_toml_datetime_format()` use nothing but their arguments.

`sof_toml_writer_output()`, `sof_toml_writer_depth()` and `sof_toml_writer_complete()` only read the writer, so any number of threads can call them at once on one writer, with a readers-writer lock if another thread writes to it. Every other writer function changes the writer and needs the write lock; since writing can move the buffer, a reader must not keep the pointer `sof_toml_writer_output()` returned once it lets go of the lock. The write function is called with the lock held by whoever is writing.
