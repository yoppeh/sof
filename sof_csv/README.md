# sof_csv

A streaming reader and writer of comma-separated values in c.

The library has two types. A `sof_csv_reader_t` returns the records of its input one at a time, and a `sof_csv_writer_t` writes records a field or a record at a time, so a program can read a record, convert it and write it out, in the memory one record takes, however large the input. The application provides each (on the stack, inside another structure or on the heap). It sets up a reader with `sof_csv_reader_init()`, giving the delimiter, flags, a read function that gives it input, such as `sof_csv_read_file()` for a `FILE *`, and an optional allocator, and then calls `sof_csv_reader_next()` for each record, which returns its fields as an array of `sof_string_t`, until it returns **SOF_CSV_NO_MATCH** at the end of the input. With **SOF_CSV_HEADER**, the input's first record is its header: `sof_csv_reader_header()` returns it, and `sof_csv_reader_column()` finds a column by name. `sof_csv_reader_spans()` says where the last record and each of its fields are in the input, as byte offsets and lengths, for an application that keeps the text: `sof_string_position()`, from [sof_string](../sof_string/), turns an offset into a line and column, and the text can be changed in place at the spans. `sof_csv_reader_clear()` starts again on new input, and `sof_csv_reader_free()` releases the memory. It sets up a writer with `sof_csv_writer_init()`, giving the delimiter, flags, an optional write function, such as `sof_csv_write_file()`, and an optional allocator; `sof_csv_writer_field()` and `sof_csv_writer_field_bytes()` write a field, `sof_csv_writer_record_end()` ends a record, and `sof_csv_writer_record()` writes a whole one, such as one the reader returned. `sof_csv_writer_output()` returns what is in the buffer, `sof_csv_writer_flush()` passes it to the write function, `sof_csv_writer_clear()` starts again, and `sof_csv_writer_free()` releases the memory.

The reader asks its read function for **SOF_CSV_READ_SIZE** (4096) bytes at a time, into a buffer of its own, and takes them apart, so the input can come from a file, a pipe, a socket or memory, in pieces of any size. Quotes, line breaks and delimiters are taken a byte at a time; the runs of other bytes between them are found 8 bytes at a time and added to the field at once. The writer looks for the bytes that make a field need quotes 8 at a time too. Each field is a `sof_string_t` that the reader keeps and reuses for the next record, so reading allocates only while records grow longer or fields larger than any before. The writer quotes a field only when it needs it, and keeps its output in a buffer until a record ends with **SOF_CSV_FLUSH_SIZE** (4096) bytes in it, then passes it to the write function, so it writes in large pieces. A call that runs out of memory, or whose read fails, changes nothing, and can be made again.

The library's demo is the [csvcut](demo/) program in _demo/_, which cuts columns out of CSV files by name or number; [csvjson](../sof_json/demo/), the demo of [sof_json](../sof_json/), uses the reader to convert CSV to JSON. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All sof_csv declarations are in _sof_csv.h_; the reader is in _sof_csv_reader.c_ and the writer in _sof_csv_writer.c_. Fields and the writer's buffer are strings from the [sof_string](../sof_string/) library in _../sof_string/_ (_sof_string.c_ and _sof_string.h_), and the reader keeps its fields and header in vectors from [sof_vector](../sof_vector/) in _../sof_vector/_ (_sof_vector.c_ and _sof_vector.h_), which uses [sof_sort](../sof_sort/) in _../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_). Memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond those, the library needs nothing but the c standard library.

    sof_csv_reader_t r;
    sof_csv_writer_t w;
    sof_string_t *fields;
    size_t count, price;
    sof_csv_reader_init(&r, ',', SOF_CSV_HEADER, sof_csv_read_file, stdin, NULL);
    sof_csv_writer_init(&w, ';', 0, sof_csv_write_file, stdout, NULL);
    while (sof_csv_reader_next(&r, &fields, &count) == SOF_CSV_OK) {    // a record at a time
        if (sof_csv_reader_column(&r, "price", &price) == SOF_CSV_OK && price < count) {
            sof_csv_writer_field_bytes(&w, fields[0].data, fields[0].length);
            sof_csv_writer_field(&w, fields[price].data);
            sof_csv_writer_record_end(&w);                               // quoted where needed
        }
    }
    sof_csv_writer_flush(&w);                                            // what is left in the buffer
    sof_csv_reader_free(&r);
    sof_csv_writer_free(&w);

## Building

    make             # builds sof_csv_reader.o, sof_csv_writer.o, ./test and the demo, demo/csvcut
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

The tests use generated data. Each reading test reads its input whole, and a byte, two bytes and seven bytes at a time, so that fields, quotes and CRLFs are split between reads. They check fields, empty fields, other delimiters and bytes of every kind; LF, CRLF and lone CR line breaks and blank lines; quoted fields with delimiters, doubled quotes and line breaks in them; each kind of malformed input, read leniently and refused with its line by a strict reader; the line of each record; where each field and record is, quoted, empty and at the end of the input, with a header, after clearing and from a strict reader's blank lines; the header and finding columns, repeated names included; that fields are reused; runs of ordinary bytes, in records of fields of every length to 24, unquoted and quoted, with quotes, line breaks and delimiters at random places in them, LF and CRLF, and four delimiters, NUL among them, read whole and in pieces of up to 23 bytes that end in the middle of runs, each field, span and line count checked, leniently and strictly; a quote at each place in a long field without quotes; read errors and going on after them; and reading a file through `sof_csv_read_file()`. They check the writer's quoting, empty fields, NULs, other delimiters, CRLF and quoting everything, a delimiter, quote, CR or LF at each place in fields of every length to 24 at every alignment, with four delimiters, and none, whole records, the write function, flushing and failing writes, and `sof_csv_write_file()`; write 3,000 random records of awkward bytes with each setting and read them back, strictly, a random number of bytes at a time, checking each against what was written; test **NULL** and freed readers and writers; and fail every allocation of a reader and a writer in turn, checking that each failure changes nothing, that making the call again finishes the work, and that nothing is leaked.

They test the library against RFC 4180 too (see Format). Each rule of section 2 is checked with the RFC's own example, strictly and leniently, and blank lines as the grammar has them. 2,000 files are made from the RFC's ABNF, with fields escaped or not at random and every byte the grammar allows in each, with and without a header and the final line break, and read strictly as the records they were made from. The writer's output, with **SOF_CSV_CRLF**, is checked against a recognizer of the ABNF, exactly as the RFC gives it. And the 12 files of [csv-spectrum](../test-data/csv-spectrum/), a suite of CSV files and the JSON they should read as, kept unchanged in _../test-data/_, are each read and compared with their JSON, decoded by the RFC 8259 checker in [../test/test_json.h](../test/). One of them, _location_coordinates_, isn't RFC 4180, having quotes in a field that isn't quoted, so the test checks that a strict reader refuses it and reads it leniently; and its JSON has a different phone number from its CSV, which the test allows for.

To use the library in another project, copy _sof_csv.h_, _sof_csv_reader.c_ and _sof_csv_writer.c_ (or just the one of the two _.c_ files it needs), with _sof_string.c_ and _sof_string.h_ from _../sof_string/_, _sof_vector.c_ and _sof_vector.h_ from _../sof_vector/_, _sof_sort.c_ and _sof_sort.h_ from _../sof_sort/_, and _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into it, and compile the _.c_ files with the project's own sources. To build its tests there too, copy _test.c_, and _test.h_ and _test_json.h_ from _../test/_, and compile _test.c_ and the library's _.c_ files with _-pthread_; they read csv-spectrum's files from _../test-data/csv-spectrum/_.

## Format

The reader and writer follow RFC 4180. Fields are separated by a delimiter, **,** unless another is given, and records by a line break. A field in double quotes can hold the delimiter, line breaks, and `""` for a quote. Spaces are part of a field. Fields are bytes: the library doesn't check or change their encoding, and a field can hold NULs.

The reader takes LF, CRLF and a lone CR as line breaks, and every record it returns has at least one field; a line of `""` is a record of one empty field. Line breaks inside quotes are kept as they are. By default it is lenient, as most readers are: it skips blank lines, a quote in the middle of a field without quotes is part of the field, text after a closing quote is added to the field (`"ab"c` is `abc`), and a quoted field still open at the end of the input ends there, setting `unterminated`. With **SOF_CSV_STRICT**, it reads RFC 4180's grammar exactly, with the exceptions below: a blank line is a record of one empty field, as the grammar makes it, the line break at the end of the input being the only one that is optional; and each of the others is an error, **SOF_CSV_SYNTAX**, with `line` saying where.

The writer puts a field in quotes, with each quote doubled, if it holds the delimiter, a quote, a CR or an LF, or with **SOF_CSV_QUOTE_ALL**, always. It ends records with LF, or with **SOF_CSV_CRLF**, CRLF. A record of one empty field is written `""`, so that a reader doesn't take it for a blank line, and a record of no fields can't be written. So whatever records the writer writes, the reader reads back the same.

RFC 4180 is an informational RFC, describing CSV as most programs wrote it, rather than a standard. Its grammar is narrower than what CSV holds in practice, and the library departs from it where it is:

- The RFC ends lines with CRLF only. The reader, strict too, takes LF and a lone CR as well, and the writer writes LF unless **SOF_CSV_CRLF** is given.
- The RFC's fields hold only printable ASCII (its TEXTDATA), though its MIME type takes a charset. The library's fields hold any bytes, UTF-8 or otherwise, and NULs.
- The RFC's delimiter is a comma; the library takes any byte but a quote, CR or LF.
- In the grammar, an empty file is one record of one empty field. The reader gives it no records.
- The RFC says each line should have the same number of fields. The reader returns each record as it is, and leaves that check to the application.
- Whether there is a header, which the RFC leaves to a parameter of its MIME type, is for the application to say, with **SOF_CSV_HEADER**.

## Functions

### The reader

`sof_csv_error_t sof_csv_reader_init(sof_csv_reader_t *r, char delimiter, unsigned flags, sof_csv_read_func_t read, void *ctx, const sof_alloc_t *alloc)`

Initialize the reader _r_. _delimiter_ is the byte between fields, such as **,**, **;** or **\t**; it can't be a quote, CR or LF. _flags_ is **0**, or **SOF_CSV_HEADER**, **SOF_CSV_STRICT** or both. _read_ is the function input comes from, such as `sof_csv_read_file()`, and _ctx_ is passed to it, such as a `FILE *`. _alloc_ is the allocator for the buffer, fields and header; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). It is copied into the reader, so it can be on the stack. Nothing is read until the first `sof_csv_reader_next()`. Returns **SOF_CSV_OK**, **SOF_CSV_NO_MEM**, or **SOF_CSV_ERROR** if _r_ or _read_ is **NULL**, the delimiter can't be used, or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the reader is left empty and not initialized.

`int sof_csv_read_file(char *buffer, size_t size, size_t *length, void *ctx)`

A read function for a `FILE *`, which is its _ctx_. It reads up to _size_ bytes into _buffer_ with `fread()` and stores how many in _length_, **0** at the end of the file. It never closes the file. Returns **0**, or **1** if the file couldn't be read or _ctx_ is **NULL**.

`sof_csv_error_t sof_csv_reader_next(sof_csv_reader_t *r, sof_string_t **fields, size_t *count)`

Read the next record and store a pointer to its fields, an array of strings, in _fields_, and their number in _count_; either can be **NULL**. They belong to the reader and are reused by the next call, so copy what is to be kept; the application may change their bytes, but not free or grow them. A record has at least one field. Blank lines are skipped, or with **SOF_CSV_STRICT** are records of one empty field. With **SOF_CSV_HEADER**, the first call reads the header first, and returns the record after it. The record's line is in `record_line`. Returns **SOF_CSV_OK**; **SOF_CSV_NO_MATCH** at the end of the input, and again for each call after, without calling the read function, until the reader is cleared; **SOF_CSV_NO_MEM**; **SOF_CSV_READ** if the read function failed or gave more bytes than asked for; **SOF_CSV_SYNTAX** if a strict reader found an error on `line`; or **SOF_CSV_ERROR** if _r_ is **NULL** or not initialized. Unless a record is returned, _fields_ is set to **NULL** and _count_ to **0**. After **SOF_CSV_NO_MEM** or **SOF_CSV_READ**, calling again goes on where it stopped; after **SOF_CSV_SYNTAX**, it fails the same way again.

`const sof_csv_span_t *sof_csv_reader_spans(const sof_csv_reader_t *r, size_t *count, sof_csv_span_t *record)`

Return where the fields of the record `sof_csv_reader_next()` returned last are in the input, an array of spans, one for each field, and if _count_ isn't **NULL**, store their number there, and if _record_ isn't **NULL**, the record's span. A field's span is its text as written, quotes and the `""` in them included, but not the delimiter after it; an empty field's has length **0**, where it would be. A record's runs from its first byte to the line break that ends it, which isn't included, or to the end of the input; a strict reader's blank line is a record of length **0**. Offsets count bytes from the start of the input, or from where the reader was last cleared, however the read function gave it. The spans belong to the reader and are good until its next call. Returns **NULL**, with a _count_ of **0** and a _record_ of **0** and **0**, if the last `sof_csv_reader_next()` didn't return a record, or _r_ is **NULL** or not initialized.

    sof_csv_span_t record;
    const sof_csv_span_t *spans = sof_csv_reader_spans(&r, NULL, &record);
    sof_string_t text = sof_string_view(csv, length);
    sof_string_position(&text, spans[2].offset, &line, &column);      // where the third field is

`const sof_string_t *sof_csv_reader_header(const sof_csv_reader_t *r, size_t *count)`

Return the header, the input's first record, as an array of strings, and if _count_ isn't **NULL**, store their number there. It is read by the first `sof_csv_reader_next()`, and kept until the reader is cleared or freed. Returns **NULL**, with a _count_ of **0**, if the header hasn't been read, the reader wasn't made with **SOF_CSV_HEADER**, or _r_ is **NULL** or not initialized.

`sof_csv_error_t sof_csv_reader_column(const sof_csv_reader_t *r, const char *name, size_t *index)`

Find the column named _name_ in the header, and if _index_ isn't **NULL**, store its index there, counting from **0**. If the name is there more than once, the first is found. A header has few names, looked up once each, so it is searched in turn rather than kept in a [sof_hashmap](../sof_hashmap/). Returns **SOF_CSV_OK**, **SOF_CSV_NO_MATCH**, leaving _index_ alone, if no column has the name or the header hasn't been read, or **SOF_CSV_ERROR** if _r_ or _name_ is **NULL**, or _r_ is not initialized.

`sof_csv_error_t sof_csv_reader_clear(sof_csv_reader_t *r)`

Start again on whatever the read function gives next: drop the input read but not yet taken apart, the record being read and the header, and start on line **1**, with `records` and `unterminated` back to **0**. The memory is kept. Use it to go on after an error, or when the read function has moved on to new input. Returns **SOF_CSV_OK**, or **SOF_CSV_ERROR** if _r_ is **NULL** or not initialized.

`sof_csv_error_t sof_csv_reader_free(sof_csv_reader_t *r)`

Release the reader's memory. The input isn't closed. The `sof_csv_reader_t` itself isn't freed; it is left empty, and acts like a reader whose `sof_csv_reader_init()` failed until it is initialized again. Returns **SOF_CSV_OK**, also for a reader not initialized, or **SOF_CSV_ERROR** if _r_ is **NULL**.

### The writer

`sof_csv_error_t sof_csv_writer_init(sof_csv_writer_t *w, char delimiter, unsigned flags, sof_csv_write_func_t write, void *ctx, const sof_alloc_t *alloc)`

Initialize the writer _w_. _delimiter_ is the byte to write between fields; it can't be a quote, CR or LF. _flags_ is **0**, or **SOF_CSV_CRLF**, **SOF_CSV_QUOTE_ALL** or both. _write_ is the function to pass output to, such as `sof_csv_write_file()`, or **NULL** to keep it all in the buffer, and _ctx_ is passed to it. _alloc_ is the allocator for the buffer, or **NULL** for the standard functions. Returns **SOF_CSV_OK**, **SOF_CSV_NO_MEM**, or **SOF_CSV_ERROR** if _w_ is **NULL**, the delimiter can't be used, or _alloc_ has a _malloc_ but no _free_ or _free_sized_. On failure the writer is left empty and not initialized.

`int sof_csv_write_file(const char *data, size_t length, void *ctx)`

A write function for a `FILE *`, which is its _ctx_. It writes the _length_ bytes at _data_ with `fwrite()`, and never flushes or closes the file. Returns **0**, or **1** if the bytes couldn't all be written or _ctx_ is **NULL**.

`sof_csv_error_t sof_csv_writer_field(sof_csv_writer_t *w, const char *s)`

Write the NUL-terminated _s_ as the next field of the record being written, as `sof_csv_writer_field_bytes()` does. Returns as it does, and **SOF_CSV_ERROR** if _s_ is **NULL**.

`sof_csv_error_t sof_csv_writer_field_bytes(sof_csv_writer_t *w, const void *s, size_t length)`

Write the _length_ bytes at _s_ as the next field of the record being written, after the delimiter unless it is the record's first, and quoted as Format describes. The bytes are written as they are, NULs included. _s_ can be **NULL** if _length_ is **0**, for an empty field. Returns **SOF_CSV_OK**, **SOF_CSV_NO_MEM**, or **SOF_CSV_ERROR** if _w_ is **NULL** or not initialized, or _s_ is **NULL** and _length_ isn't **0**. On failure nothing is written.

`sof_csv_error_t sof_csv_writer_record_end(sof_csv_writer_t *w)`

End the record being written, with the line break. A record of one empty field is written `""`. If there is a write function and the buffer has reached **SOF_CSV_FLUSH_SIZE** bytes, it is passed on. Returns **SOF_CSV_OK**, **SOF_CSV_NO_MEM**, **SOF_CSV_WRITE** if the record was ended but the write function failed, or **SOF_CSV_ERROR** if _w_ is **NULL** or not initialized, or the record has no fields. On failure other than **SOF_CSV_WRITE**, nothing is written and the record stays open.

`sof_csv_error_t sof_csv_writer_record(sof_csv_writer_t *w, const sof_string_t *fields, size_t count)`

Write the _count_ strings in _fields_ as fields, after any written already, and end the record, so a record `sof_csv_reader_next()` returned can be written as it is. A dead string is an empty field. Returns as `sof_csv_writer_record_end()` does, and **SOF_CSV_ERROR** if _fields_ is **NULL** and _count_ isn't **0**. On failure other than **SOF_CSV_WRITE**, nothing is written.

`const char *sof_csv_writer_output(const sof_csv_writer_t *w, size_t *length)`

Return the output in the buffer, followed by a NUL, and if _length_ isn't **NULL**, store its length in bytes there. With a write function, this is what hasn't been passed to it yet. The output belongs to the writer, and is good until the next call that writes, flushes, clears or frees. Returns **NULL**, and a _length_ of **0**, if _w_ is **NULL** or not initialized.

`sof_csv_error_t sof_csv_writer_flush(sof_csv_writer_t *w)`

Pass the buffer to the write function and empty it; in the middle of a record too. Call it when the output is finished, since the writer otherwise waits for its buffer to fill. Does nothing if there is no write function or the buffer is empty. Returns **SOF_CSV_OK**, **SOF_CSV_WRITE** if the write function failed, which leaves the buffer as it was, or **SOF_CSV_ERROR** if _w_ is **NULL** or not initialized.

`sof_csv_error_t sof_csv_writer_clear(sof_csv_writer_t *w)`

Discard the buffer, without passing it to the write function, and the record being written; `records` and `written` go back to **0**. The memory is kept, and so are the delimiter, flags and write function. Returns **SOF_CSV_OK**, or **SOF_CSV_ERROR** if _w_ is **NULL** or not initialized.

`sof_csv_error_t sof_csv_writer_free(sof_csv_writer_t *w)`

Release the writer's memory. The buffer is discarded, not flushed: call `sof_csv_writer_flush()` first to keep it. The `sof_csv_writer_t` itself isn't freed; it is left empty, and acts like a writer whose `sof_csv_writer_init()` failed until it is initialized again. Returns **SOF_CSV_OK**, also for a writer not initialized, or **SOF_CSV_ERROR** if _w_ is **NULL**.

Every function accepts a **NULL** reader or writer, and one that was freed or whose initialization failed (`ready` is **0**), which does nothing: functions that read, write, flush or clear return **SOF_CSV_ERROR**, `sof_csv_reader_header()`, `sof_csv_reader_spans()` and `sof_csv_writer_output()` return **NULL** with a count or length of **0**, and the free functions return **SOF_CSV_OK**, or **SOF_CSV_ERROR** for **NULL**.

## Types

    typedef enum sof_csv_error_t {
        SOF_CSV_OK = 0,
        SOF_CSV_NO_MEM,
        SOF_CSV_NO_MATCH,               // no more records, or no such column
        SOF_CSV_ERROR,
        SOF_CSV_SYNTAX,                 // a strict reader's input isn't valid CSV
        SOF_CSV_READ,                   // the read function failed
        SOF_CSV_WRITE,                  // the write function failed; the output is kept
    } sof_csv_error_t;

    typedef enum sof_csv_flag_t {
        SOF_CSV_HEADER = 1,             // reader: the first record is the header
        SOF_CSV_STRICT = 2,             // reader: malformed input is an error
        SOF_CSV_CRLF = 4,               // writer: end records with CRLF
        SOF_CSV_QUOTE_ALL = 8,          // writer: quote every field
    } sof_csv_flag_t;

    typedef int (*sof_csv_read_func_t)(char *buffer, size_t size, size_t *length, void *ctx);

The read function. It is called with a buffer of _size_ bytes, the reader's own, and the _ctx_ given to `sof_csv_reader_init()`. It puts up to _size_ bytes in _buffer_ and their number in _length_, **0** at the end of the input, and returns **0**, or non-zero if the input couldn't be read. It may give fewer bytes than asked for, as a pipe or socket does. It must not call into the reader.

    typedef int (*sof_csv_write_func_t)(const char *data, size_t length, void *ctx);

The write function. It is called with the _length_ bytes of output at _data_, which belong to the writer, and the _ctx_ given to `sof_csv_writer_init()`. It returns **0** if every byte was written, or non-zero if they couldn't be, in which case the writer keeps them all in its buffer for the next flush. It must not call into the writer.

    typedef struct sof_csv_span_t {
        size_t offset;                  // the first byte's, from the start of the input
        size_t length;                  // bytes
    } sof_csv_span_t;

Where a field or record is in the input, as `sof_csv_reader_spans()` gives it.

    typedef struct sof_csv_reader_t {
        sof_vector_t fields;            // sof_string_t, reused from record to record
        size_t count;                   // fields in the record being read, or last returned
        sof_vector_t spans;             // sof_csv_span_t, the fields', reused from record to record
        sof_csv_span_t record;          // the record's span
        size_t consumed;                // bytes of input before buffer's
        int returned;                   // 1 if the last sof_csv_reader_next() returned a record
        sof_vector_t header;            // sof_string_t, the first record, with SOF_CSV_HEADER
        char *buffer;                   // input read but not yet taken apart
        size_t length;                  // bytes in buffer
        size_t position;                // the next byte in buffer
        char delimiter;
        unsigned flags;                 // the sof_csv_flag_t flags
        sof_csv_read_func_t read;       // where input comes from
        void *ctx;                      // passed to read
        sof_alloc_t alloc;              // copy of the allocator, for the buffer
        int state;                      // where the reader is in a field
        int in_record;                  // 1 once the record being read has a byte
        int after_cr;                   // 1 after a CR, which an LF may belong to
        int ended;                      // 1 once the input has ended
        int unterminated;               // 1 if the input ended in a quoted field
        size_t line;                    // the line being read, from 1; after SOF_CSV_SYNTAX, the error's
        size_t record_line;             // the line the record last returned starts on
        size_t records;                 // records returned, the header not counted
        int ready;                      // 1 once initialized; 0 when freed or failed
    } sof_csv_reader_t;

    typedef struct sof_csv_writer_t {
        sof_string_t out;               // the output not yet passed to write: all of it, if there is none
        char delimiter;
        unsigned flags;                 // the sof_csv_flag_t flags
        sof_csv_write_func_t write;     // where output goes, or NULL
        void *ctx;                      // passed to write
        size_t fields;                  // fields in the record being written
        size_t record_length;           // bytes written for it
        size_t records;                 // records finished
        size_t written;                 // bytes passed to write
        int ready;                      // 1 once initialized; 0 when freed or failed
    } sof_csv_writer_t;

A reader and a writer. The application may read the fields but must not change them. The writer's `out.data` moves as the buffer grows, so a pointer into it is good only until the next call that writes.

## Threading

The library has no global state and does no locking. Different readers and writers can be used on different threads freely, so one thread can read while another writes.

`sof_csv_reader_header()`, `sof_csv_reader_column()`, `sof_csv_reader_spans()` and `sof_csv_writer_output()` only read, so any number of threads can call them at once on one reader or writer, with a readers-writer lock if another thread uses it otherwise. Every other function changes the reader or writer and needs the write lock. The fields `sof_csv_reader_next()` returns, and the spans `sof_csv_reader_spans()` returns, are good only until the next call to it, and the pointer `sof_csv_writer_output()` returns only until the next call that writes, so a thread must not keep them once it lets go of the lock.
