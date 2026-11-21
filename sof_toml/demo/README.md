# tomljson

A TOML to JSON, and JSON to TOML, converter in c.

`tomljson` reads a TOML file, or standard input, and writes it as JSON: tables as objects, arrays as arrays, strings, integers, floats and booleans as themselves, and dates and times as strings, in their TOML form. With `-j` it goes the other way, reading a JSON object and writing it as TOML. With `-t`, the JSON either way is the tagged JSON of [toml-test](../../test-data/toml-test/), the TOML project's test suite, in which each value is an object of its type and its text, so nothing is lost: `tomljson -t` is a TOML decoder toml-test's runner can test, and `tomljson -j -t` an encoder.

_main.c_ is the whole program. To write JSON, it reads the file whole into a [sof_toml](../) document with `sof_toml_doc_read()` and `sof_toml_read_file()`; an error is reported with the document's `line`, `column` and `message`. It walks the tree from `root`, a table's members in their order with `sof_toml_value_member()` and an array's elements with `sof_toml_value_at()`, and writes each with a [sof_json](../../sof_json/) writer, set up with `sof_json_writer_init()`, the indentation `-i` gives, **SOF_JSON_ASCII** for `-a`, and a write function that writes to standard output. Dates and times are formatted with `sof_toml_datetime_format()`. JSON has no infinities or NaNs, so plain JSON has `inf` and `nan` as strings; tagged JSON has every value as a string anyway, floats with all 17 digits.

With `-j`, it reads one JSON value into a [sof_json](../../sof_json/) document with `sof_json_doc_read()` from a reader, checks that nothing follows it with `sof_json_reader_next()`, and writes it with a [sof_toml](../) writer, set up with `sof_toml_writer_init()`, **SOF_TOML_ASCII** for `-a`, and the same write function. A TOML document is a table, so the value must be an object. For each object, it gives each member's key with `sof_toml_writer_key_bytes()` and writes its value: those that aren't objects, or arrays of objects, first, and then those that are, since a table written as a section can have no key/value pairs after its sub-tables. An object is begun with `sof_toml_writer_table_begin()`, which makes it a `[table]` section after a key in a section, and an inline table in an array; an array of objects with `sof_toml_writer_table_array_begin()`, which makes it `[[array]]` sections there; and other arrays with `sof_toml_writer_array_begin()`. Strings, booleans and numbers are written with `sof_toml_writer_string_bytes()`, `sof_toml_writer_bool()`, and `sof_toml_writer_int()` for a number written as an integer that fits an `int64_t`, or `sof_toml_writer_double()`. With `-t`, a tagged value's text becomes its type: an integer with `sof_json_number_int()`, a float with `sof_json_number_double()`, or `inf` and `nan`, and a date or time with `sof_toml_datetime_parse()` and `sof_toml_writer_datetime()`. TOML has no null, so a null is an error.

The TOML document and writer are the [sof_toml](../) library in _../_ (_sof_toml_doc.c_, _sof_toml_writer.c_ and _sof_toml.h_), and the JSON reader, document and writer the [sof_json](../../sof_json/) library in _../../sof_json/_ (_sof_json_reader.c_, _sof_json_doc.c_, _sof_json_writer.c_ and _sof_json.h_). Both use the arena of [sof_mem](../../sof_mem/) in _../../sof_mem/_ (_sof_mem_arena.c_, _sof_mem.h_ and _sof_mem_internal.h_), [sof_hashmap](../../sof_hashmap/) in _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_) with [sof_hash](../../sof_hash/) in _../../sof_hash/_ (_sof_hash.c_, _sof_hash.h_ and _rapidhash.h_), [sof_string](../../sof_string/) in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), [sof_stack](../../sof_stack/) in _../../sof_stack/_ (_sof_stack.c_ and _sof_stack.h_), [sof_utf](../../sof_utf/) in _../../sof_utf/_ (_sof_utf.h_) and, for their numbers, [sof_real](../../sof_real/) in _../../sof_real/_ (_sof_real.c_, _sof_real.h_ and _sof_real_internal.h_, with _sof_bit.h_ from _../../sof_bit/_), and allocate through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    sof_toml_doc_init(&d, NULL);
    sof_toml_doc_read(&d, sof_toml_read_file, f);                     // the whole file
    sof_json_writer_init(&w, 2, 0, write_stdout, NULL, NULL);         // write_stdout() calls fwrite()
    json_value(&w, d.root, tagged);                                    // each member and element in turn
    ...
    sof_json_doc_read(&j, &r);                                         // with -j, the JSON object
    sof_toml_writer_init(&t, 0, write_stdout, NULL, NULL);
    toml_members(&to, j.root);                                         // key/value pairs, then tables
    sof_toml_writer_flush(&t);

## Building

    make            # builds ./tomljson
    make debug=1    # builds with debug info and without optimization
    ./tomljson ../../test-data/toml-test/tests/valid/example.toml       # as JSON
    ./tomljson -i 0 ../../test-data/toml-test/tests/valid/example.toml | ./tomljson -j    # and back again

`make` in the library's directory, _../_, builds it with the library, as do `make sof_toml` and `make` from the root of the project.

To build it in another project, copy _main.c_, with _sof_toml_doc.c_, _sof_toml_writer.c_ and _sof_toml.h_ from _../_, _sof_json_reader.c_, _sof_json_doc.c_, _sof_json_writer.c_ and _sof_json.h_ from _../../sof_json/_, _sof_mem_arena.c_, _sof_mem.h_ and _sof_mem_internal.h_ from _../../sof_mem/_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_stack.c_ and _sof_stack.h_ from _../../sof_stack/_, _sof_utf.h_ from _../../sof_utf/_, _sof_real.c_, _sof_real.h_ and _sof_real_internal.h_ from _../../sof_real/_, _sof_bit.h_ from _../../sof_bit/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it.

To test it with toml-test's own runner, which is written in Go, build the runner from [toml-test](https://github.com/toml-lang/toml-test) and give it the program as a decoder, and as an encoder; the encoder's tests need the decoder too, to read back what the encoder writes:

    toml-test test -toml 1.1.0 -decoder "./tomljson -t"                             # the valid and invalid tests
    toml-test test -toml 1.1.0 -decoder "./tomljson -t" -encoder "./tomljson -j -t" # and the encoder's tests

With toml-test v2.2.0, it passes all 214 valid tests, all 214 encoder tests and all 467 invalid tests for TOML 1.1.0. With `-toml 1.0.0`, 9 of the invalid tests fail, since they use what TOML 1.1 allows and TOML 1.0 doesn't: times without seconds, inline tables over several lines or ending with a comma, and `\x` escapes.

## Usage

    tomljson [-h] [-a] [-j] [-t] [-i spaces] [file]

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-a`: write every character outside ASCII as an escape, so the output is ASCII.
- `-j`: read a JSON object and write TOML. It can't be given with `-i`.
- `-t`: the JSON, read or written, is toml-test's tagged JSON: each value an object such as `{"type": "integer", "value": "42"}`, whose type is `string`, `integer`, `float`, `bool`, `datetime`, `datetime-local`, `date-local` or `time-local`, and whose value is its text.
- `-i` _spaces_: indent each level of the JSON by _spaces_, from **0**, for it all on one line, to **16**; **2** if it isn't given.
- _file_: the input. With no _file_, or `-`, it reads standard input.

The TOML it reads is TOML 1.1.0, checked as the [sof_toml](../) document checks it; an error is reported with its line, column and what it is, and nothing is written. The JSON it reads with `-j` is one value, an object, as RFC 8259 defines it; numbers written as integers that fit an `int64_t` become integers, and the rest floats.

It exits with **0**, with **1** if the file couldn't be opened or read, isn't TOML, or with `-j`, isn't a JSON object or holds a null, or memory ran out or the output couldn't be written, after printing why, and with **2** if the options are wrong.

For example:

    $ cat server.toml
    # A server
    title = "café"
    port = 8080
    ratio = 0.75

    [owner]
    name = "Tom"
    born = 1979-05-27T07:32:00-08:00

    [[fruit]]
    name = "apple"
    colors = ["red", "green"]

    [[fruit]]
    name = "banana"

    $ ./tomljson server.toml
    {
      "title": "café",
      "port": 8080,
      "ratio": 0.75,
      "owner": {
        "name": "Tom",
        "born": "1979-05-27T07:32:00-08:00"
      },
      "fruit": [
        {
          "name": "apple",
          "colors": [
            "red",
            "green"
          ]
        },
        {
          "name": "banana"
        }
      ]
    }

    $ ./tomljson -t -i 0 server.toml | ./tomljson -j -t
    title = "café"
    port = 8080
    ratio = 0.75

    [owner]
    name = "Tom"
    born = 1979-05-27T07:32:00-08:00

    [[fruit]]
    name = "apple"
    colors = ["red", "green"]

    [[fruit]]
    name = "banana"

    $ printf 'a = 1\n[a]\n' | ./tomljson
    tomljson: standard input:2:2: a table is defined twice
