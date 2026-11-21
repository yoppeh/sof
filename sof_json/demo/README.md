# csvjson

A CSV to JSON, and JSON to CSV, converter in c.

`csvjson` reads comma-separated values from files or standard input and writes them as JSON. By default the output is one JSON array of objects, one for each record, keyed by the names in each file's first record. With `-l` it is JSON Lines (JSONL) instead, an object to a line, and with `-r` each record is an array of its fields. Fields are strings, unless `-n` asks for those that are numbers to be numbers and empty ones to be null. With `-j` it goes the other way, reading JSON, an array of records or JSON Lines, and writing CSV. Records are written as they are read, so a file of any size is converted in the memory one record takes.

_main.c_ is the whole program. It reads each file with a [sof_csv](../../sof_csv/) reader, set up with `sof_csv_reader_init()`, **SOF_CSV_HEADER** unless `-r` is given, and `sof_csv_read_file()` to read the file, and takes its records one at a time with `sof_csv_reader_next()`, which returns each as an array of `sof_string_t` fields; the names come from `sof_csv_reader_header()`. It writes each record, as soon as it has it, with a [sof_json](../) writer. The writer is set up with `sof_json_writer_init()`, with the indentation `-i` gives (or **0** for `-l`), **SOF_JSON_ASCII** for `-a`, and a write function that writes to standard output. The outer array, without `-l`, is begun with `sof_json_writer_array_begin()` before the first file and ended after the last. For each record, it writes `sof_json_writer_object_begin()`, then for each field a `sof_json_writer_key_bytes()` with its name and a `sof_json_writer_string_bytes()` with its value, or, with `-n`, a `sof_json_writer_raw()` for a number and `sof_json_writer_null()` for an empty field, and `sof_json_writer_object_end()`; with `-r`, an array instead. The writer adds the commas, quotes, escapes and indentation, and turns any bytes that aren't UTF-8 into U+FFFD, so the output is valid JSON whatever the input holds. A JSON Lines record is passed to the write function when it ends; inside the outer array, the writer would wait until its buffer filled, so the program calls `sof_json_writer_flush()` after each record.

With `-j`, each file is read with a [sof_json](../) reader, set up with `sof_json_reader_init()`, **SOF_JSON_LINES** so that a series of values is read, and `sof_json_read_file()`. At the top level, `sof_json_reader_peek()` says what comes next: for an array, `sof_json_reader_next()` goes into it, and each element is a record; anything else is a record itself. Each record is read whole into a [sof_json](../) document with `sof_json_doc_read()`, which reads one element, or one top-level value, at a time, so an array of records too large for memory is converted a record at a time. The first record that is an object names the columns: its keys, from `sof_json_value_member()`, are written as the first CSV record, and its document is kept, while the records after it are read into a second one. For each later object, each name is looked up with `sof_json_value_get_bytes()`, which finds a key in a large object through the document's index. An array record's elements are its fields, and any other value is a field of its own. Strings are written as they are, numbers with the text they were written with, `true` and `false` as words, `null` and missing values as empty fields, and objects and arrays as compact JSON, written with `sof_json_writer_value()`. The records are written with a [sof_csv](../../sof_csv/) writer, set up with `sof_csv_writer_init()` and `sof_csv_write_file()`, a field at a time with `sof_csv_writer_field_bytes()` and `sof_csv_writer_record_end()`, which quotes the fields that need it.

The JSON writer, reader and document are the [sof_json](../) library in _../_ (_sof_json_writer.c_, _sof_json_reader.c_, _sof_json_doc.c_ and _sof_json.h_), which uses [sof_stack](../../sof_stack/) in _../../sof_stack/_ (_sof_stack.c_ and _sof_stack.h_), [sof_utf](../../sof_utf/) in _../../sof_utf/_ (_sof_utf.h_), the arena of [sof_mem](../../sof_mem/) in _../../sof_mem/_ (_sof_mem_arena.c_, _sof_mem.h_ and _sof_mem_internal.h_), and [sof_hashmap](../../sof_hashmap/) in _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), with [sof_hash](../../sof_hash/) in _../../sof_hash/_ (_sof_hash.c_, _sof_hash.h_ and _rapidhash.h_), and [sof_real](../../sof_real/) in _../../sof_real/_ (_sof_real.c_, _sof_real.h_ and _sof_real_internal.h_, with _sof_bit.h_ from _../../sof_bit/_) for its numbers. The CSV reader and writer are the [sof_csv](../../sof_csv/) library in _../../sof_csv/_ (_sof_csv_reader.c_, _sof_csv_writer.c_ and _sof_csv.h_), which keeps the fields as strings from the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), in vectors from [sof_vector](../../sof_vector/) in _../../sof_vector/_ (_sof_vector.c_ and _sof_vector.h_), which uses [sof_sort](../../sof_sort/) in _../../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_). They all allocate through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    sof_csv_reader_init(&r, ',', SOF_CSV_HEADER, sof_csv_read_file, f, NULL);
    sof_json_writer_init(&w, 2, 0, write_stdout, NULL, NULL);         // write_stdout() calls fwrite()
    sof_json_writer_array_begin(&w);
    while (sof_csv_reader_next(&r, &fields, &count) == SOF_CSV_OK) {
        names = sof_csv_reader_header(&r, &names_count);
        sof_json_writer_object_begin(&w);
        for (i = 0; i < count; i++) {
            sof_json_writer_key_bytes(&w, names[i].data, names[i].length);
            sof_json_writer_string_bytes(&w, fields[i].data, fields[i].length);
        }
        sof_json_writer_object_end(&w);
        sof_json_writer_flush(&w);                                     // the record goes to standard output
    }
    sof_json_writer_array_end(&w);                                     // the array is whole, and flushed
    sof_csv_reader_free(&r);
    sof_json_writer_free(&w);

## Building

    make            # builds ./csvjson
    make debug=1    # builds with debug info and without optimization
    ./csvjson -l ../../test-data/words.txt    # each word, as {"aa":"word"}
    ./csvjson ../../test-data/words.txt | ./csvjson -j    # and back again, as it was

`make` in the library's directory, _../_, builds it with the library, as do `make sof_json` and `make` from the root of the project.

To build it in another project, copy _main.c_, with _sof_json_writer.c_, _sof_json_reader.c_, _sof_json_doc.c_ and _sof_json.h_ from _../_, _sof_csv_reader.c_, _sof_csv_writer.c_ and _sof_csv.h_ from _../../sof_csv/_, _sof_stack.c_ and _sof_stack.h_ from _../../sof_stack/_, _sof_utf.h_ from _../../sof_utf/_, _sof_mem_arena.c_, _sof_mem.h_ and _sof_mem_internal.h_ from _../../sof_mem/_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_real.c_, _sof_real.h_ and _sof_real_internal.h_ from _../../sof_real/_, _sof_bit.h_ from _../../sof_bit/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_vector.c_ and _sof_vector.h_ from _../../sof_vector/_, _sof_sort.c_ and _sof_sort.h_ from _../../sof_sort/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it.

## Usage

    csvjson [-h] [-a] [-j] [-l] [-n] [-r] [-d delimiter] [-i spaces] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-a`: write every character outside ASCII as a \u escape, so the output is ASCII.
- `-j`: read JSON and write CSV. Of the other options, it takes only `-d`, for the output's delimiter.
- `-l`: write JSON Lines, one record to a line, rather than one array. It can't be given with `-i`.
- `-n`: write each field that is a number as JSON writes numbers, such as `-12`, `0.5` or `4.5e2`, as a number, and each empty field as `null`. Other fields, `007` and `1.` among them, stay strings.
- `-r`: write each record as an array of its fields, without taking names from the first record, which is written like the others.
- `-d` _delimiter_: the character between fields, which can't be a quote or a newline, or `tab`; **,** if it isn't given.
- `-i` _spaces_: indent each level of the output by _spaces_, from **0**, for the whole array on one line, to **16**; **2** if it isn't given.
- _file_: the input. With no _file_, or `-`, it reads standard input. Each file's first record names the fields of the records after it, and the records of every file go in the one array.

The input follows RFC 4180. A field in double quotes can hold the delimiter, line breaks and `""` for a quote. Records end with LF, CRLF or a lone CR, and blank lines are skipped. A field after the last name is keyed by its column number, counting from **1**, and a name after the last field gets `null`. Names that repeat are written as they are, so the object has the same key twice. Like most readers, it is lenient: a quote in the middle of an unquoted field is kept as a quote, text after a closing quote is part of the field, and a quoted field still open at the end of the input ends there, with a warning. Bytes that aren't UTF-8 are written as U+FFFD.

With `-j`, the input is JSON as RFC 8259 defines it: a top-level array, whose elements are the records, or a series of values, such as JSON Lines, each a record, or arrays of records. The first record that is an object names the columns, and is written first, as the names; each object after it gives a field for each name, empty where it has no such key, and its keys that aren't names are left out. An array record's elements are its fields, and any other value is one field. Strings are written as they are, numbers as they were written, `true` and `false` as words, `null` as an empty field, and an object or array as compact JSON. Input that isn't JSON is reported with its line and column, and the next file is read; what was written before the error stays.

It exits with **0**, with **1** if a file couldn't be opened or read, or with `-j` isn't JSON, after printing why and going on to the next file, or memory ran out or the output couldn't be written, after which it stops, and with **2** if the options are wrong. The outer array is closed even after an error, so the output is whole JSON.

For example:

    $ printf 'name,age,note\nAda,36,"says ""hi"", then\nleaves"\nBob,,café\nCy,4.5e2,x,extra\n' > people.csv
    $ ./csvjson people.csv
    [
      {
        "name": "Ada",
        "age": "36",
        "note": "says \"hi\", then\nleaves"
      },
      {
        "name": "Bob",
        "age": "",
        "note": "café"
      },
      {
        "name": "Cy",
        "age": "4.5e2",
        "note": "x",
        "4": "extra"
      }
    ]

    $ ./csvjson -l -n -a people.csv
    {"name":"Ada","age":36,"note":"says \"hi\", then\nleaves"}
    {"name":"Bob","age":null,"note":"caf\u00e9"}
    {"name":"Cy","age":4.5e2,"note":"x","4":"extra"}

    $ ./csvjson -r -i 0 people.csv
    [["name","age","note"],["Ada","36","says \"hi\", then\nleaves"],["Bob","","café"],["Cy","4.5e2","x","extra"]]

    $ ./csvjson -l -n people.csv | ./csvjson -j -d ';'
    name;age;note
    Ada;36;"says ""hi"", then
    leaves"
    Bob;;café
    Cy;4.5e2;x

    $ printf '{"id":1,"tags":["a","b"],"ok":true}\n{"ok":null,"id":2}\n' | ./csvjson -j
    id,tags,ok
    1,"[""a"",""b""]",true
    2,,
