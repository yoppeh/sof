# csvcut

A CSV column cutter in c.

`csvcut` reads comma-separated values from files or standard input and writes the columns it is asked for, in the order it is asked for them, named by their header or numbered from 1. Without `-f` it writes every column, which makes it a converter between delimiters, line endings and quoting styles, and a cleaner of lenient CSV into strict. It reads and writes a record at a time, so a file of any size is cut in the memory one record takes.

_main.c_ is the whole program. It reads each file with a [sof_csv](../) reader, set up with `sof_csv_reader_init()`, **SOF_CSV_HEADER** unless `-H` is given, **SOF_CSV_STRICT** with `-s`, and `sof_csv_read_file()` to read the file, and takes its records one at a time with `sof_csv_reader_next()`. The first call reads the header too; the names `-f` gives are then looked up with `sof_csv_reader_column()`, once for each file, and the header of the columns cut is written from `sof_csv_reader_header()`. For each record, the program makes an array of `sof_string_view()` strings of the fields it wants, in a [sof_vector](../../sof_vector/), without copying their bytes, with an empty field for a column past the end of a short record, and writes it with `sof_csv_writer_record()`. The [sof_csv](../) writer, set up with `sof_csv_writer_init()`, the output delimiter, **SOF_CSV_CRLF** for `-c`, **SOF_CSV_QUOTE_ALL** for `-q`, and `sof_csv_write_file()` to write to standard output, quotes the fields that need it and passes its output on as its buffer fills; `sof_csv_writer_flush()` writes the rest at the end.

The reader and writer are the [sof_csv](../) library in _../_ (_sof_csv.h_, _sof_csv_reader.c_ and _sof_csv_writer.c_), which keeps fields as strings from the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), in vectors from the [sof_vector](../../sof_vector/) library in _../../sof_vector/_ (_sof_vector.c_ and _sof_vector.h_), which uses [sof_sort](../../sof_sort/) in _../../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_). The program keeps its columns and the record it writes in sof_vectors too. They all allocate through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    sof_csv_reader_init(&r, ',', SOF_CSV_HEADER, sof_csv_read_file, f, NULL);
    sof_csv_writer_init(&w, '\t', 0, sof_csv_write_file, stdout, NULL);
    while (sof_csv_reader_next(&r, &fields, &count) == SOF_CSV_OK) {
        sof_csv_reader_column(&r, "name", &name);                   // its index in the header
        row[0] = sof_string_view(fields[name].data, fields[name].length);
        sof_csv_writer_record(&w, row, 1);                          // quoted if it must be
    }
    sof_csv_writer_flush(&w);

## Building

    make            # builds ./csvcut
    make debug=1    # builds with debug info and without optimization
    ./csvcut -H -f 1,1 ../../test-data/words.txt    # each word twice, separated by a comma

`make` in the library's directory, _../_, builds it with the library, as do `make sof_csv` and `make` from the root of the project.

To build it in another project, copy _main.c_, with _sof_csv.h_, _sof_csv_reader.c_ and _sof_csv_writer.c_ from _../_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_vector.c_ and _sof_vector.h_ from _../../sof_vector/_, _sof_sort.c_ and _sof_sort.h_ from _../../sof_sort/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it.

## Usage

    csvcut [-h] [-H] [-c] [-q] [-s] [-d delimiter] [-o delimiter] [-f columns] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-H`: the input has no header, so its first line is a record like the others, columns can be named only by number, and the output has no header.
- `-c`: end output records with CRLF, as RFC 4180 has it, rather than LF.
- `-q`: quote every output field, not just those that need it.
- `-s`: be strict, reading RFC 4180's grammar exactly but for LF line breaks and fields of any bytes: stop reading a file at the first line that isn't valid CSV, such as a quote in the middle of an unquoted field, and say which line it is, and write a blank line as a record of one empty field, `""`, as the grammar makes it. Without it, such input is read as most readers read it, and blank lines are skipped.
- `-d` _delimiter_: the input's delimiter, one character other than a quote or line break, or `tab`; **,** if it isn't given.
- `-o` _delimiter_: the output's delimiter, as `-d` takes it; the input's if it isn't given.
- `-f` _columns_: the columns to write, in order, separated by commas: names from the header, numbers counting from **1**, and ranges such as `2-4`. A column can be written more than once. A column past the end of a short record is written empty. A name made only of digits is taken as a number. Without `-f`, every column of each record is written.
- _file_: the input. With no _file_, or `-`, it reads standard input. Each file's names are looked up in its own header, and the output's header is the first file's.

It exits with **0**, with **1** if a file couldn't be opened or read, isn't valid CSV with `-s`, or hasn't a column `-f` names, after printing why and going on to the next file, or if memory ran out or the output couldn't be written, and with **2** if the options are wrong.

For example, with _people.csv_ as made in [csvjson](../../sof_json/demo/)'s README:

    $ ./csvcut -f note,name people.csv
    note,name
    "says ""hi"", then
    leaves",Ada
    café,Bob
    x,Cy

    $ ./csvcut -f 2-3,1 -o tab people.csv
    age	note	name
    36	"says ""hi"", then
    leaves"	Ada
    	café	Bob
    4.5e2	x	Cy

    $ ./csvcut -H -f 1,4 people.csv
    name,
    Ada,
    Bob,
    Cy,extra

    $ ./csvcut -f name,age,nope people.csv
    csvcut: people.csv: there is no column "nope"
