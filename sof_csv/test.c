/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_csv_reader.c and sof_csv_writer.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_csv.h"
#include "sof_string.h"
#include "test.h"
#include "test_json.h"

/// @brief The number of elements in an array.
#define LENGTH(a) (sizeof(a) / sizeof((a)[0]))

/// @brief The size of the buffers records are described in.
#define TEXT_SIZE 65536

/// @brief The number of random records written and read back.
#define RANDOM_RECORDS 3000

/// @brief The most fields in a random record.
#define RANDOM_FIELDS 6

/// @brief The most bytes in a random field.
#define RANDOM_FIELD 12

/// @brief The records made for each delimiter by test_reader_runs().
#define RUN_RECORDS 200

/// @brief The most bytes in a field made by test_reader_runs() and
/// test_writer_runs(), enough for runs of ordinary bytes to end before, on
/// and after two 8-byte boundaries.
#define RUN_FIELD 25

/// @brief Input for a reader, from memory, through source_read().
typedef struct source_t {
    const char *data;
    size_t length;
    size_t position;
    /// @brief The most bytes a call gives, or 0 for as many as asked.
    size_t chunk;
    /// @brief The calls made.
    size_t calls;
    /// @brief The call to fail, counting from 1, or 0 for none.
    size_t fail_at;
    /// @brief Non-zero to give more bytes than asked for.
    int overflow;
} source_t;

/// @brief What a write function in the tests collects, through its ctx.
typedef struct sink_t {
    char data[TEXT_SIZE];
    size_t length;
    size_t calls;
    /// @brief Non-zero to fail every call.
    int fail;
} sink_t;

static int describe(sof_csv_reader_t *r, char *text, size_t size);
static int describe_record(const sof_string_t *fields, size_t count, char *text, size_t size, size_t *n);
static int describe_spans(const sof_csv_reader_t *r, char *text, size_t size, size_t *n);
static int reads_as(const char *input, char delimiter, unsigned flags, const char *expected);
static int sink_write(const char *data, size_t length, void *ctx);
static int source_read(char *buffer, size_t size, size_t *length, void *ctx);
static source_t source_of(const char *data, size_t chunk);
static int writes_as(sof_csv_writer_t *w, const char *expected);

/// @brief Reads every record and describes them in text, each field in
/// <>, a record to a line, the header first as "header" and its fields.
/// @return 1, or 0 if a call failed or the text didn't fit.
static int describe(sof_csv_reader_t *r, char *text, size_t size) {
    size_t n = 0;
    sof_string_t *fields;
    size_t count;
    sof_csv_error_t rc;
    while ((rc = sof_csv_reader_next(r, &fields, &count)) == SOF_CSV_OK) {
        if (n == 0 && (r->flags & SOF_CSV_HEADER) != 0) {
            size_t names;
            const sof_string_t *header = sof_csv_reader_header(r, &names);
            n += (size_t)snprintf(text + n, size - n, "header");
            if (!describe_record(header, names, text, size, &n)) {
                return 0;
            }
        }
        if (!describe_record(fields, count, text, size, &n)) {
            return 0;
        }
    }
    text[n] = '\0';
    return rc == SOF_CSV_NO_MATCH;
}

/// @brief Appends a record's description to text at *n: each field in <>,
/// then a newline.
static int describe_record(const sof_string_t *fields, size_t count, char *text, size_t size, size_t *n) {
    for (size_t i = 0; i < count; i++) {
        if (size - *n < fields[i].length + 4) {
            return 0;
        }
        text[(*n)++] = '<';
        memcpy(text + *n, fields[i].data, fields[i].length);
        *n += fields[i].length;
        text[(*n)++] = '>';
    }
    text[(*n)++] = '\n';
    text[*n] = '\0';
    return 1;
}

/// @brief Appends the spans of the record last read to text at *n, as
/// offset+length for each field, then the record's in [], then a newline.
static int describe_spans(const sof_csv_reader_t *r, char *text, size_t size, size_t *n) {
    size_t count;
    sof_csv_span_t record;
    const sof_csv_span_t *spans = sof_csv_reader_spans(r, &count, &record);
    if (spans == NULL) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        *n += (size_t)snprintf(text + *n, size - *n, "%zu+%zu ", spans[i].offset, spans[i].length);
    }
    *n += (size_t)snprintf(text + *n, size - *n, "[%zu+%zu]\n", record.offset, record.length);
    return *n < size;
}

/// @brief Says whether input reads as the records expected describes, as
/// describe() describes them, given whole and a byte, two bytes and seven
/// bytes at a time.
static int reads_as(const char *input, char delimiter, unsigned flags, const char *expected) {
    static const size_t chunks[] = {0, 1, 2, 7};
    static char text[TEXT_SIZE];
    for (size_t i = 0; i < LENGTH(chunks); i++) {
        source_t source = source_of(input, chunks[i]);
        sof_csv_reader_t r;
        int ok = sof_csv_reader_init(&r, delimiter, flags, source_read, &source, NULL) == SOF_CSV_OK && describe(&r, text, sizeof(text)) && strcmp(text, expected) == 0;
        sof_csv_reader_free(&r);
        if (!ok) {
            printf("(chunk %zu read \"%s\") ", chunks[i], text);
            return 0;
        }
    }
    return 1;
}

/// @brief The write function of the tests: appends to the sink in ctx,
/// unless its fail is set.
static int sink_write(const char *data, size_t length, void *ctx) {
    sink_t *sink = ctx;
    sink->calls++;
    if (sink->fail || length > TEXT_SIZE - sink->length) {
        return 1;
    }
    memcpy(sink->data + sink->length, data, length);
    sink->length += length;
    return 0;
}

/// @brief The read function of the tests: gives the source in ctx, chunk
/// bytes at a time, failing the call fail_at.
static int source_read(char *buffer, size_t size, size_t *length, void *ctx) {
    source_t *source = ctx;
    source->calls++;
    *length = 0;
    if (source->calls == source->fail_at) {
        return 1;
    }
    if (source->overflow) {
        *length = size + 1;
        return 0;
    }
    size_t n = source->length - source->position;
    if (n > size) {
        n = size;
    }
    if (source->chunk != 0 && n > source->chunk) {
        n = source->chunk;
    }
    memcpy(buffer, source->data + source->position, n);
    source->position += n;
    *length = n;
    return 0;
}

/// @brief Makes a source of a c string, given chunk bytes at a time.
static source_t source_of(const char *data, size_t chunk) {
    source_t source = {data, strlen(data), 0, chunk, 0, 0, 0};
    return source;
}

/// @brief Says whether a writer's buffer holds the c string expected.
static int writes_as(sof_csv_writer_t *w, const char *expected) {
    size_t n;
    const char *out = sof_csv_writer_output(w, &n);
    int ok = out != NULL && n == strlen(expected) && memcmp(out, expected, n) == 0;
    if (!ok) {
        printf("(wrote \"%s\") ", out != NULL ? out : "nothing");
    }
    return ok;
}

static void test_reader_init(void) {
    source_t source = source_of("", 0);
    sof_csv_reader_t r;
    CHECK(sof_csv_reader_init(&r, ',', SOF_CSV_STRICT, source_read, &source, NULL) == SOF_CSV_OK);
    CHECK(r.ready == 1 && r.delimiter == ',' && r.flags == SOF_CSV_STRICT && r.line == 1);
    CHECK(r.read == source_read && r.ctx == &source && r.buffer != NULL);
    // Nothing is read until the first record is asked for.
    CHECK(source.calls == 0);
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_NO_MATCH);
    CHECK(source.calls == 1);
    // At the end, it stays at the end without reading again.
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_NO_MATCH);
    CHECK(source.calls == 1);
    CHECK(sof_csv_reader_free(&r) == SOF_CSV_OK && r.ready == 0);

    CHECK(sof_csv_reader_init(NULL, ',', 0, source_read, &source, NULL) == SOF_CSV_ERROR);
    CHECK(sof_csv_reader_init(&r, ',', 0, NULL, &source, NULL) == SOF_CSV_ERROR && r.ready == 0);
    CHECK(sof_csv_reader_init(&r, '"', 0, source_read, &source, NULL) == SOF_CSV_ERROR);
    CHECK(sof_csv_reader_init(&r, '\n', 0, source_read, &source, NULL) == SOF_CSV_ERROR);
    CHECK(sof_csv_reader_init(&r, '\r', 0, source_read, &source, NULL) == SOF_CSV_ERROR);
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    CHECK(sof_csv_reader_init(&r, ',', 0, source_read, &source, &alloc) == SOF_CSV_ERROR);
    CHECK(r.ready == 0 && state.allocations == 0);
}

static void test_reader_fields(void) {
    CHECK(reads_as("a,b,c\n1,2,3\n", ',', 0, "<a><b><c>\n<1><2><3>\n"));
    // Without a line break at the end.
    CHECK(reads_as("a,b\nc", ',', 0, "<a><b>\n<c>\n"));
    // Empty fields.
    CHECK(reads_as(",\n,,a\na,\n", ',', 0, "<><>\n<><><a>\n<a><>\n"));
    // Spaces are part of fields.
    CHECK(reads_as(" a , b \n", ',', 0, "< a >< b >\n"));
    // Other delimiters; a comma is then an ordinary byte.
    CHECK(reads_as("a;b,c\n", ';', 0, "<a><b,c>\n"));
    CHECK(reads_as("a\tb c\n", '\t', 0, "<a><b c>\n"));
    // Bytes of every kind, but line breaks, pass through.
    CHECK(reads_as("\xc3\xa9\x01\x7f\xff,x\n", ',', 0, "<\xc3\xa9\x01\x7f\xff><x>\n"));
}

static void test_reader_line_breaks(void) {
    // LF, CRLF and a lone CR end records; blank lines are skipped.
    CHECK(reads_as("a\r\nb\rc\nd", ',', 0, "<a>\n<b>\n<c>\n<d>\n"));
    CHECK(reads_as("\n\r\n\r\na\n\n\r\rb\n\n", ',', 0, "<a>\n<b>\n"));
    CHECK(reads_as("\n\n", ',', 0, ""));
    CHECK(reads_as("", ',', 0, ""));
}

static void test_reader_quotes(void) {
    CHECK(reads_as("\"a,b\",\"x\"\"y\",\"\"\n", ',', 0, "<a,b><x\"y><>\n"));
    // Line breaks in quotes are kept as they are.
    CHECK(reads_as("\"line\nbreak\",\"cr\r\nlf\",\"cr\ronly\"\n", ',', 0, "<line\nbreak><cr\r\nlf><cr\ronly>\n"));
    // A record of one quoted empty field isn't a blank line.
    CHECK(reads_as("\"\"\na\n", ',', 0, "<>\n<a>\n"));
    // A quoted field at the end of the input, and one ending a record with
    // CRLF.
    CHECK(reads_as("a,\"b\"", ',', 0, "<a><b>\n"));
    CHECK(reads_as("\"a\"\r\n\"b\"\r\n", ',', 0, "<a>\n<b>\n"));
    // A quote doubled at the very end of a field.
    CHECK(reads_as("\"\"\"\"\n", ',', 0, "<\">\n"));
}

static void test_reader_lenient(void) {
    // A quote inside an unquoted field is kept, text after a closing quote
    // is added, and an open quote ends at the end of the input.
    CHECK(reads_as("a\"b,\"x\"y z,c\n", ',', 0, "<a\"b><xy z><c>\n"));
    CHECK(reads_as("a,\"open\nstill", ',', 0, "<a><open\nstill>\n"));
    source_t source = source_of("a,\"open", 0);
    sof_csv_reader_t r;
    CHECK(sof_csv_reader_init(&r, ',', 0, source_read, &source, NULL) == SOF_CSV_OK);
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_OK && r.unterminated == 1);
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_NO_MATCH);
    sof_csv_reader_free(&r);
}

static void test_reader_strict(void) {
    static const struct {
        const char *input;
        size_t line;
    } cases[] = {
        {"ok\nab\"c\n", 2},
        {"ok\n\"x\"y\n", 2},
        {"ok\r\n\"multi\nline\" ,x\n", 3},
        {"ok\na,\"open\n\n", 4},
    };
    // Valid input reads as it does when lenient.
    CHECK(reads_as("\"a,b\",\"x\"\"y\"\r\nc,\n", ',', SOF_CSV_STRICT, "<a,b><x\"y>\n<c><>\n"));
    for (size_t i = 0; i < LENGTH(cases); i++) {
        source_t source = source_of(cases[i].input, 0);
        sof_csv_reader_t r;
        sof_string_t *fields;
        size_t count;
        CHECK(sof_csv_reader_init(&r, ',', SOF_CSV_STRICT, source_read, &source, NULL) == SOF_CSV_OK);
        CHECK(sof_csv_reader_next(&r, &fields, &count) == SOF_CSV_OK && count == 1);
        CHECK(sof_csv_reader_next(&r, &fields, &count) == SOF_CSV_SYNTAX);
        CHECK(fields == NULL && count == 0 && r.line == cases[i].line);
        // It fails the same way again, until cleared.
        CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_SYNTAX);
        CHECK(sof_csv_reader_clear(&r) == SOF_CSV_OK && r.line == 1 && r.records == 0);
        CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_NO_MATCH);
        sof_csv_reader_free(&r);
    }
}

static void test_reader_lines(void) {
    // Each record's line, past quoted line breaks, CRLFs and blank lines,
    // with the input given a byte at a time so CRLFs are split.
    static const char input[] = "a\r\n\"b\r\nc\"\r\n\r\nd\n\re\rf";
    static const size_t lines[] = {1, 2, 5, 7, 8};
    source_t source = source_of(input, 1);
    sof_csv_reader_t r;
    CHECK(sof_csv_reader_init(&r, ',', 0, source_read, &source, NULL) == SOF_CSV_OK);
    for (size_t i = 0; i < LENGTH(lines); i++) {
        CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_OK);
        CHECK(r.record_line == lines[i] && r.records == i + 1);
    }
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_NO_MATCH);
    sof_csv_reader_free(&r);
}

static void test_reader_header(void) {
    CHECK(reads_as("name,age\nx,1\n\ny,2", ',', SOF_CSV_HEADER, "header<name><age>\n<x><1>\n<y><2>\n"));
    source_t source = source_of("\nname,age,name\r\nx,1,z\n", 0);
    sof_csv_reader_t r;
    size_t index = 99, count;
    sof_string_t *fields;
    CHECK(sof_csv_reader_init(&r, ',', SOF_CSV_HEADER, source_read, &source, NULL) == SOF_CSV_OK);
    // Before the first record, there is no header.
    CHECK(sof_csv_reader_header(&r, &count) == NULL && count == 0);
    CHECK(sof_csv_reader_column(&r, "age", &index) == SOF_CSV_NO_MATCH && index == 99);
    CHECK(sof_csv_reader_next(&r, &fields, &count) == SOF_CSV_OK && count == 3);
    CHECK(r.records == 1 && r.record_line == 3);
    const sof_string_t *names = sof_csv_reader_header(&r, &count);
    CHECK(names != NULL && count == 3 && strcmp(names[1].data, "age") == 0);
    CHECK(sof_csv_reader_column(&r, "age", &index) == SOF_CSV_OK && index == 1);
    // The first of a repeated name is found.
    CHECK(sof_csv_reader_column(&r, "name", &index) == SOF_CSV_OK && index == 0);
    CHECK(sof_csv_reader_column(&r, "nam", NULL) == SOF_CSV_NO_MATCH);
    CHECK(sof_csv_reader_column(&r, "", NULL) == SOF_CSV_NO_MATCH);
    CHECK(sof_csv_reader_column(&r, NULL, NULL) == SOF_CSV_ERROR);
    CHECK(strcmp(fields[2].data, "z") == 0);
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_NO_MATCH);
    // The header stays until the reader is cleared.
    CHECK(sof_csv_reader_header(&r, NULL) != NULL);
    CHECK(sof_csv_reader_clear(&r) == SOF_CSV_OK);
    CHECK(sof_csv_reader_header(&r, &count) == NULL && count == 0);
    sof_csv_reader_free(&r);

    // A header alone: no records, but the header is read.
    source = source_of("a,b\n", 0);
    CHECK(sof_csv_reader_init(&r, ',', SOF_CSV_HEADER, source_read, &source, NULL) == SOF_CSV_OK);
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_NO_MATCH);
    CHECK(sof_csv_reader_header(&r, &count) != NULL && count == 2);
    CHECK(r.records == 0);
    sof_csv_reader_free(&r);

    // Without SOF_CSV_HEADER, the first record is a record.
    source = source_of("a,b\n", 0);
    CHECK(sof_csv_reader_init(&r, ',', 0, source_read, &source, NULL) == SOF_CSV_OK);
    CHECK(sof_csv_reader_next(&r, NULL, &count) == SOF_CSV_OK && count == 2);
    CHECK(sof_csv_reader_header(&r, NULL) == NULL);
    CHECK(sof_csv_reader_column(&r, "a", NULL) == SOF_CSV_NO_MATCH);
    sof_csv_reader_free(&r);
}

static void test_reader_fields_reused(void) {
    // The fields belong to the reader; the application may change their
    // bytes. A shorter record after a longer one has its own count.
    source_t source = source_of("abc,def,ghi\nx\n", 0);
    sof_csv_reader_t r;
    sof_string_t *fields;
    size_t count;
    CHECK(sof_csv_reader_init(&r, ',', 0, source_read, &source, NULL) == SOF_CSV_OK);
    CHECK(sof_csv_reader_next(&r, &fields, &count) == SOF_CSV_OK && count == 3);
    sof_string_toupper(&fields[1]);
    CHECK(strcmp(fields[1].data, "DEF") == 0);
    CHECK(sof_csv_reader_next(&r, &fields, &count) == SOF_CSV_OK && count == 1);
    CHECK(fields[0].length == 1 && strcmp(fields[0].data, "x") == 0);
    CHECK(r.fields.count == 3);
    sof_csv_reader_free(&r);
}

static void test_reader_spans(void) {
    // Each field as written, quotes included, and each record without its
    // line break, given whole and in pieces, so that they cross the reads.
    static const char input[] = "name,age\r\n\"Smith, J\",42\n\nx,\"a\"\"b\"\n,\nlast,\"open";
    static const char expected[] = "0+4 5+3 [0+8]\n"
                                   "10+10 21+2 [10+13]\n"
                                   "25+1 27+6 [25+8]\n"
                                   "34+0 35+0 [34+1]\n"
                                   "36+4 41+5 [36+10]\n";
    static const size_t chunks[] = {0, 1, 2, 7};
    char text[512];
    for (size_t c = 0; c < LENGTH(chunks); c++) {
        source_t source = source_of(input, chunks[c]);
        sof_csv_reader_t r;
        size_t n = 0, count = 99;
        sof_csv_span_t record = {9, 9};
        CHECK(sof_csv_reader_init(&r, ',', 0, source_read, &source, NULL) == SOF_CSV_OK);
        // Nothing has been read yet.
        CHECK(sof_csv_reader_spans(&r, &count, &record) == NULL && count == 0 && record.offset == 0 && record.length == 0);
        while (sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_OK) {
            CHECK(describe_spans(&r, text, sizeof(text), &n));
        }
        CHECK(strcmp(text, expected) == 0);
        // After the end there is no record.
        CHECK(sof_csv_reader_spans(&r, &count, NULL) == NULL && count == 0);
        sof_csv_reader_free(&r);
    }

    // The spans as text, and a line number from one.
    source_t source = source_of(input, 0);
    sof_csv_reader_t r;
    size_t count, line, column;
    sof_csv_span_t record;
    CHECK(sof_csv_reader_init(&r, ',', SOF_CSV_HEADER, source_read, &source, NULL) == SOF_CSV_OK);
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_OK);
    const sof_csv_span_t *spans = sof_csv_reader_spans(&r, &count, &record);
    CHECK(spans != NULL && count == 2);
    CHECK(memcmp(input + spans[0].offset, "\"Smith, J\"", spans[0].length) == 0 && spans[0].length == 10);
    CHECK(memcmp(input + record.offset, "\"Smith, J\",42", record.length) == 0 && record.length == 13);
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_OK);
    spans = sof_csv_reader_spans(&r, NULL, NULL);
    sof_string_t whole = sof_string_view(input, strlen(input));
    CHECK(sof_string_position(&whole, spans[1].offset, &line, &column) == SOF_STRING_OK && line == 4 && column == 3);
    // Clearing starts the offsets again, on what the read function gives
    // next, whose first line is the header again.
    CHECK(sof_csv_reader_clear(&r) == SOF_CSV_OK && sof_csv_reader_spans(&r, NULL, NULL) == NULL);
    source = source_of("a,b\nc\n", 0);
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_OK);
    spans = sof_csv_reader_spans(&r, &count, &record);
    CHECK(count == 1 && spans[0].offset == 4 && spans[0].length == 1 && record.offset == 4 && record.length == 1);
    sof_csv_reader_free(&r);

    // A strict reader's blank line is a record of one empty field, where
    // the line is.
    source = source_of("a\r\n\r\nb", 1);
    CHECK(sof_csv_reader_init(&r, ',', SOF_CSV_STRICT, source_read, &source, NULL) == SOF_CSV_OK);
    size_t n = 0;
    while (sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_OK) {
        CHECK(describe_spans(&r, text, sizeof(text), &n));
    }
    CHECK(strcmp(text, "0+1 [0+1]\n3+0 [3+0]\n5+1 [5+1]\n") == 0);
    sof_csv_reader_free(&r);
}

static void test_reader_runs(void) {
    // Runs of ordinary bytes are taken 8 at a time. Records made of fields
    // of every length up to RUN_FIELD - 1, unquoted and quoted, at every
    // alignment, with up to two quotes, line breaks or delimiters at random
    // places in the quoted ones, ended by LF or CRLF, with four delimiters,
    // NUL among them, read whole and in chunks that split runs anywhere and
    // end buffers in the middle of them, read as they were made, with their
    // spans and line count, lenient and strict.
    static const char delimiters[] = {',', ';', '\t', '\0'};
    static const char ordinary[] = {'a', 'b', ',', ';', '\t', ' ', '|', '\'', (char)0xc3, (char)0xa9};
    static const char specials[] = {'"', '\n', '\r', 'd'};
    static const size_t chunks[] = {0, 1, 2, 3, 7, 8, 9, 13, 15, 16, 17, 23};
    static char input[TEXT_SIZE], expected[TEXT_SIZE], expected_spans[TEXT_SIZE], text[TEXT_SIZE], spans[TEXT_SIZE];
    uint64_t state = 0x6a09e667f3bcc908ULL;
    for (size_t d = 0; d < LENGTH(delimiters); d++) {
        char delimiter = delimiters[d];
        size_t length = 0, described = 0, spanned = 0, lines = 1;
        for (int i = 0; i < RUN_RECORDS; i++) {
            size_t count = 1 + next_random(&state) % 4, record = length;
            for (size_t k = 0; k < count; k++) {
                char field[RUN_FIELD];
                size_t n = next_random(&state) % RUN_FIELD, start;
                // A lone empty field without quotes would be a blank line.
                int quoted = (count == 1 && n == 0) || next_random(&state) % 2 == 0;
                for (size_t j = 0; j < n; j++) {
                    char c = ordinary[next_random(&state) % sizeof(ordinary)];
                    field[j] = c == delimiter ? 'x' : c;
                }
                for (size_t s = quoted && n > 0 ? next_random(&state) % 3 : 0; s > 0; s--) {
                    char c = specials[next_random(&state) % sizeof(specials)];
                    field[next_random(&state) % n] = c == 'd' ? delimiter : c;
                }
                for (size_t j = 0; quoted && j < n; j++) {
                    if (field[j] == '\r' || (field[j] == '\n' && (j == 0 || field[j - 1] != '\r'))) {
                        lines++;
                    }
                }
                if (k > 0) {
                    input[length++] = delimiter;
                }
                start = length;
                if (quoted) {
                    input[length++] = '"';
                }
                for (size_t j = 0; j < n; j++) {
                    if (field[j] == '"') {
                        input[length++] = '"';
                    }
                    input[length++] = field[j];
                }
                if (quoted) {
                    input[length++] = '"';
                }
                spanned += (size_t)snprintf(expected_spans + spanned, sizeof(expected_spans) - spanned, "%zu+%zu ", start, length - start);
                expected[described++] = '<';
                memcpy(expected + described, field, n);
                described += n;
                expected[described++] = '>';
            }
            expected[described++] = '\n';
            spanned += (size_t)snprintf(expected_spans + spanned, sizeof(expected_spans) - spanned, "[%zu+%zu]\n", record, length - record);
            // The last record may end without a line break.
            size_t end = next_random(&state) % 2;
            if (i < RUN_RECORDS - 1 || end == 0) {
                if (end == 0) {
                    input[length++] = '\r';
                }
                input[length++] = '\n';
                lines++;
            }
        }
        CHECK(length < sizeof(input) && described < sizeof(expected) && spanned < sizeof(expected_spans));
        for (size_t c = 0; c < LENGTH(chunks); c++) {
            for (unsigned flags = 0; flags <= SOF_CSV_STRICT; flags += SOF_CSV_STRICT) {
                source_t source = {input, length, 0, chunks[c], 0, 0, 0};
                sof_csv_reader_t r;
                sof_string_t *fields;
                size_t count, n = 0, m = 0;
                CHECK(sof_csv_reader_init(&r, delimiter, flags, source_read, &source, NULL) == SOF_CSV_OK);
                while (sof_csv_reader_next(&r, &fields, &count) == SOF_CSV_OK) {
                    CHECK(describe_record(fields, count, text, sizeof(text), &n));
                    CHECK(describe_spans(&r, spans, sizeof(spans), &m));
                }
                CHECK(n == described && memcmp(text, expected, n) == 0);
                CHECK(m == spanned && memcmp(spans, expected_spans, m) == 0);
                CHECK(r.records == RUN_RECORDS && r.line == lines && r.unterminated == 0);
                sof_csv_reader_free(&r);
            }
        }
    }

    // A quote in a field without quotes, at each place in a long one, is
    // an error on its line in a strict reader, and a byte of the field in a
    // lenient one; as are the bytes after a quoted field's closing quote.
    for (size_t p = 1; p < 20; p++) {
        char line[32] = "ok\n", want[40] = "<ok>\n<";
        memset(line + 3, 'a', 20);
        line[3 + p] = '"';
        memcpy(line + 23, ",x\n", 4);
        memcpy(want + 6, line + 3, 20);
        memcpy(want + 26, "><x>\n", 6);
        CHECK(reads_as(line, ',', 0, want));
        source_t source = source_of(line, 0);
        sof_csv_reader_t r;
        CHECK(sof_csv_reader_init(&r, ',', SOF_CSV_STRICT, source_read, &source, NULL) == SOF_CSV_OK);
        CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_OK);
        CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_SYNTAX && r.line == 2);
        sof_csv_reader_free(&r);
    }
    CHECK(reads_as("\"ab\"cdefghijklmnopq,x\n", ',', 0, "<abcdefghijklmnopq><x>\n"));
}

static void test_reader_read_errors(void) {
    // A failed read is reported, and asking again goes on where it stopped:
    // the 12 bytes are read in four calls of three, and a fifth finds the
    // end.
    for (size_t fail = 1; fail <= 5; fail++) {
        static char text[256];
        source_t source = source_of("a,b\n\"c\nd\",e\n", 3);
        source.fail_at = fail;
        sof_csv_reader_t r;
        sof_string_t *fields;
        size_t count, n = 0;
        int failures = 0;
        sof_csv_error_t rc;
        CHECK(sof_csv_reader_init(&r, ',', 0, source_read, &source, NULL) == SOF_CSV_OK);
        while ((rc = sof_csv_reader_next(&r, &fields, &count)) != SOF_CSV_NO_MATCH) {
            if (rc == SOF_CSV_READ) {
                CHECK(fields == NULL && count == 0);
                failures++;
                continue;
            }
            CHECK(rc == SOF_CSV_OK && describe_record(fields, count, text, sizeof(text), &n));
        }
        CHECK(failures == 1);
        CHECK(strcmp(text, "<a><b>\n<c\nd><e>\n") == 0);
        sof_csv_reader_free(&r);
    }

    // A read function that gives more than it was asked for is an error.
    source_t source = source_of("a", 0);
    source.overflow = 1;
    sof_csv_reader_t r;
    CHECK(sof_csv_reader_init(&r, ',', 0, source_read, &source, NULL) == SOF_CSV_OK);
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_READ);
    sof_csv_reader_free(&r);
}

static void test_read_file(void) {
    FILE *f = tmpfile();
    CHECK(f != NULL);
    // Enough to need several reads.
    for (int i = 0; i < 2000; i++) {
        fprintf(f, "%d,\"row %d\"\r\n", i, i);
    }
    rewind(f);
    sof_csv_reader_t r;
    sof_string_t *fields;
    size_t count;
    int i = 0;
    CHECK(sof_csv_reader_init(&r, ',', SOF_CSV_STRICT, sof_csv_read_file, f, NULL) == SOF_CSV_OK);
    while (sof_csv_reader_next(&r, &fields, &count) == SOF_CSV_OK) {
        char expected[32];
        snprintf(expected, sizeof(expected), "row %d", i);
        CHECK(count == 2 && atoi(fields[0].data) == i && strcmp(fields[1].data, expected) == 0);
        i++;
    }
    CHECK(i == 2000 && r.line == 2001);
    sof_csv_reader_free(&r);
    fclose(f);

    // No file is an error.
    size_t length = 99;
    char buffer[4];
    CHECK(sof_csv_read_file(buffer, sizeof(buffer), &length, NULL) == 1 && length == 0);
}

static void test_writer(void) {
    sof_csv_writer_t w;
    CHECK(sof_csv_writer_init(&w, ',', 0, NULL, NULL, NULL) == SOF_CSV_OK);
    CHECK(w.ready == 1 && w.delimiter == ',' && w.flags == 0);
    // Fields are quoted only when they need it, with quotes doubled.
    CHECK(sof_csv_writer_field(&w, "plain") == SOF_CSV_OK);
    CHECK(sof_csv_writer_field(&w, "a,b") == SOF_CSV_OK);
    CHECK(sof_csv_writer_field(&w, "say \"hi\"") == SOF_CSV_OK);
    CHECK(sof_csv_writer_field(&w, "two\nlines") == SOF_CSV_OK);
    CHECK(sof_csv_writer_field(&w, "cr\r") == SOF_CSV_OK);
    CHECK(sof_csv_writer_field(&w, "") == SOF_CSV_OK);
    CHECK(sof_csv_writer_field(&w, " spaced ") == SOF_CSV_OK);
    CHECK(w.fields == 7 && w.records == 0);
    CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
    CHECK(w.fields == 0 && w.records == 1);
    CHECK(writes_as(&w, "plain,\"a,b\",\"say \"\"hi\"\"\",\"two\nlines\",\"cr\r\",, spaced \n"));
    sof_csv_writer_clear(&w);

    // A record of one empty field is "", and of two empty fields a comma.
    CHECK(sof_csv_writer_field(&w, "") == SOF_CSV_OK);
    CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
    CHECK(sof_csv_writer_field_bytes(&w, NULL, 0) == SOF_CSV_OK);
    CHECK(sof_csv_writer_field_bytes(&w, "", 0) == SOF_CSV_OK);
    CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
    // NULs are written as they are.
    CHECK(sof_csv_writer_field_bytes(&w, "a\0b", 3) == SOF_CSV_OK);
    CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
    size_t n;
    const char *out = sof_csv_writer_output(&w, &n);
    CHECK(n == 9 && memcmp(out, "\"\"\n,\na\0b\n", 9) == 0);
    sof_csv_writer_clear(&w);

    // A record needs a field.
    CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_ERROR);
    CHECK(sof_csv_writer_record(&w, NULL, 0) == SOF_CSV_ERROR);
    CHECK(sof_csv_writer_field(&w, NULL) == SOF_CSV_ERROR);
    CHECK(sof_csv_writer_field_bytes(&w, NULL, 1) == SOF_CSV_ERROR);
    CHECK(sof_csv_writer_record(&w, NULL, 1) == SOF_CSV_ERROR);
    CHECK(writes_as(&w, "") && w.records == 0);

    // Whole records, after any fields already written; a dead string is an
    // empty field.
    sof_string_t fields[3] = {sof_string_const("x"), {0}, sof_string_const("y;z")};
    CHECK(sof_csv_writer_record(&w, fields, 3) == SOF_CSV_OK);
    CHECK(sof_csv_writer_field(&w, "first") == SOF_CSV_OK);
    CHECK(sof_csv_writer_record(&w, fields, 1) == SOF_CSV_OK);
    CHECK(writes_as(&w, "x,,y;z\nfirst,x\n") && w.records == 2);
    sof_csv_writer_free(&w);

    // Another delimiter, CRLF, and quoting everything.
    CHECK(sof_csv_writer_init(&w, ';', SOF_CSV_CRLF | SOF_CSV_QUOTE_ALL, NULL, NULL, NULL) == SOF_CSV_OK);
    CHECK(sof_csv_writer_record(&w, fields, 3) == SOF_CSV_OK);
    CHECK(sof_csv_writer_field(&w, "") == SOF_CSV_OK);
    CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
    CHECK(writes_as(&w, "\"x\";\"\";\"y;z\"\r\n\"\"\r\n"));
    sof_csv_writer_free(&w);
    CHECK(sof_csv_writer_init(&w, ';', 0, NULL, NULL, NULL) == SOF_CSV_OK);
    CHECK(sof_csv_writer_record(&w, fields, 3) == SOF_CSV_OK);
    CHECK(writes_as(&w, "x;;\"y;z\"\n"));
    sof_csv_writer_free(&w);

    CHECK(sof_csv_writer_init(NULL, ',', 0, NULL, NULL, NULL) == SOF_CSV_ERROR);
    CHECK(sof_csv_writer_init(&w, '"', 0, NULL, NULL, NULL) == SOF_CSV_ERROR && w.ready == 0);
    CHECK(sof_csv_writer_init(&w, '\n', 0, NULL, NULL, NULL) == SOF_CSV_ERROR);
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    CHECK(sof_csv_writer_init(&w, ',', 0, NULL, NULL, &alloc) == SOF_CSV_ERROR && state.allocations == 0);
}

static void test_writer_output(void) {
    static sink_t sink;
    sof_csv_writer_t w;
    memset(&sink, 0, sizeof(sink));
    CHECK(sof_csv_writer_init(&w, ',', 0, sink_write, &sink, NULL) == SOF_CSV_OK);
    // Records are kept until the buffer fills.
    CHECK(sof_csv_writer_field(&w, "a") == SOF_CSV_OK);
    CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
    CHECK(sink.calls == 0 && writes_as(&w, "a\n"));
    // Flushing in the middle of a record.
    CHECK(sof_csv_writer_field(&w, "b") == SOF_CSV_OK);
    CHECK(sof_csv_writer_flush(&w) == SOF_CSV_OK);
    CHECK(sink.calls == 1 && w.written == 3 && writes_as(&w, ""));
    CHECK(sof_csv_writer_field(&w, "c") == SOF_CSV_OK);
    CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
    CHECK(sof_csv_writer_flush(&w) == SOF_CSV_OK);
    CHECK(sof_csv_writer_flush(&w) == SOF_CSV_OK && sink.calls == 2);
    CHECK(sink.length == 6 && memcmp(sink.data, "a\nb,c\n", 6) == 0);

    // The record that takes the buffer to SOF_CSV_FLUSH_SIZE passes it on.
    size_t calls = sink.calls;
    int records = 0;
    while (sink.calls == calls) {
        CHECK(sof_csv_writer_field(&w, "0123456789") == SOF_CSV_OK);
        CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
        records++;
    }
    CHECK((size_t)records * 11 >= SOF_CSV_FLUSH_SIZE && (size_t)(records - 1) * 11 < SOF_CSV_FLUSH_SIZE);
    CHECK(writes_as(&w, "") && w.written == sink.length);

    // A failed write keeps the output, and is reported.
    sink.fail = 1;
    for (int i = 0; i < records - 1; i++) {
        CHECK(sof_csv_writer_field(&w, "0123456789") == SOF_CSV_OK);
        CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
    }
    CHECK(sof_csv_writer_field(&w, "0123456789") == SOF_CSV_OK);
    CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_WRITE);
    CHECK(w.out.length == (size_t)records * 11 && w.fields == 0);
    CHECK(sof_csv_writer_flush(&w) == SOF_CSV_WRITE);
    sink.fail = 0;
    size_t before = sink.length;
    CHECK(sof_csv_writer_flush(&w) == SOF_CSV_OK);
    CHECK(sink.length == before + (size_t)records * 11 && writes_as(&w, ""));

    // Clearing drops the output and the record being written.
    CHECK(sof_csv_writer_field(&w, "dropped") == SOF_CSV_OK);
    calls = sink.calls;
    CHECK(sof_csv_writer_clear(&w) == SOF_CSV_OK);
    CHECK(sink.calls == calls && writes_as(&w, "") && w.fields == 0 && w.records == 0 && w.written == 0);
    CHECK(w.write == sink_write && w.ctx == &sink);
    CHECK(sof_csv_writer_field(&w, "") == SOF_CSV_OK);
    CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
    CHECK(writes_as(&w, "\"\"\n"));
    sof_csv_writer_free(&w);

    // sof_csv_write_file() writes to a FILE *.
    FILE *f = tmpfile();
    char back[16] = {0};
    CHECK(f != NULL);
    CHECK(sof_csv_writer_init(&w, ',', 0, sof_csv_write_file, f, NULL) == SOF_CSV_OK);
    CHECK(sof_csv_writer_field(&w, "x,y") == SOF_CSV_OK);
    CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
    CHECK(sof_csv_writer_flush(&w) == SOF_CSV_OK);
    rewind(f);
    CHECK(fread(back, 1, sizeof(back), f) == 6 && strcmp(back, "\"x,y\"\n") == 0);
    sof_csv_writer_free(&w);
    fclose(f);
    CHECK(sof_csv_write_file("x", 1, NULL) == 1);
}

static void test_writer_runs(void) {
    // Fields are looked at 8 bytes at a time for what makes them need
    // quotes. A delimiter, quote, CR or LF at each place in fields of every
    // length up to RUN_FIELD - 1, at every alignment, with four delimiters,
    // NUL among them, is found, and a field without one isn't quoted.
    static const char delimiters[] = {',', ';', '\t', '\0'};
    static const char ordinary[] = {'a', ',', ';', '\t', ' ', (char)0xc3, (char)0xa9};
    static const char specials[] = {'"', '\n', '\r', 'd', 0};
    static char expected[4 * RUN_FIELD];
    for (size_t d = 0; d < LENGTH(delimiters); d++) {
        char delimiter = delimiters[d];
        sof_csv_writer_t w;
        CHECK(sof_csv_writer_init(&w, delimiter, 0, NULL, NULL, NULL) == SOF_CSV_OK);
        for (size_t s = 0; s < LENGTH(specials); s++) {
            char special = specials[s] == 'd' ? delimiter : specials[s];
            for (size_t n = 0; n < RUN_FIELD; n++) {
                // Without a special byte the field is written once, at each
                // alignment; with one, once for each place it can be.
                for (size_t at = 0; at < (specials[s] == 0 ? 1 : n); at++) {
                    for (size_t align = 0; align < 8; align++) {
                        char buffer[RUN_FIELD + 8], *field = buffer + align;
                        size_t length = 0;
                        for (size_t j = 0; j < n; j++) {
                            char c = ordinary[(j + align) % sizeof(ordinary)];
                            field[j] = c == delimiter ? 'x' : c;
                        }
                        if (specials[s] != 0) {
                            field[at] = special;
                        }
                        int quoted = specials[s] != 0 || n == 0;
                        if (quoted) {
                            expected[length++] = '"';
                        }
                        for (size_t j = 0; j < n; j++) {
                            if (field[j] == '"') {
                                expected[length++] = '"';
                            }
                            expected[length++] = field[j];
                        }
                        if (quoted) {
                            expected[length++] = '"';
                        }
                        expected[length++] = '\n';
                        size_t written;
                        CHECK(sof_csv_writer_field_bytes(&w, field, n) == SOF_CSV_OK);
                        CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
                        const char *out = sof_csv_writer_output(&w, &written);
                        CHECK(written == length && memcmp(out, expected, length) == 0);
                        CHECK(sof_csv_writer_clear(&w) == SOF_CSV_OK);
                    }
                }
            }
        }
        sof_csv_writer_free(&w);
    }
}

static void test_round_trip(void) {
    // Random records, written with random settings and read back a random
    // number of bytes at a time, come back as they were.
    static const char alphabet[] = {'a', 'b', ' ', ',', ';', '"', '\r', '\n', '\t', '\0', (char)0xc3, (char)0xa9};
    static const char delimiters[] = {',', ';', '\t', '|'};
    static char written[TEXT_SIZE * 4];
    static sof_string_t model[RANDOM_FIELDS];
    uint64_t state = 0x3c6ef372fe94f82bULL;
    for (int round = 0; round < 4; round++) {
        char delimiter = delimiters[round];
        unsigned flags = (round & 1 ? SOF_CSV_CRLF : 0) | (round & 2 ? SOF_CSV_QUOTE_ALL : 0);
        sof_csv_writer_t w;
        sof_csv_reader_t r;
        CHECK(sof_csv_writer_init(&w, delimiter, flags, NULL, NULL, NULL) == SOF_CSV_OK);
        uint64_t replay = state;
        for (int i = 0; i < RANDOM_RECORDS / 4; i++) {
            size_t count = 1 + next_random(&state) % RANDOM_FIELDS;
            for (size_t k = 0; k < count; k++) {
                char field[RANDOM_FIELD];
                size_t length = next_random(&state) % RANDOM_FIELD;
                for (size_t j = 0; j < length; j++) {
                    field[j] = alphabet[next_random(&state) % sizeof(alphabet)];
                }
                CHECK(sof_csv_writer_field_bytes(&w, field, length) == SOF_CSV_OK);
            }
            CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
        }
        CHECK(w.out.length <= sizeof(written));
        memcpy(written, w.out.data, w.out.length);
        source_t source = {written, w.out.length, 0, 1 + next_random(&state) % 50, 0, 0, 0};
        sof_csv_writer_free(&w);

        // Make the same records again, and compare each with what is read.
        CHECK(sof_csv_reader_init(&r, delimiter, SOF_CSV_STRICT, source_read, &source, NULL) == SOF_CSV_OK);
        for (int i = 0; i < RANDOM_RECORDS / 4; i++) {
            sof_string_t *fields;
            size_t count = 1 + next_random(&replay) % RANDOM_FIELDS, got;
            for (size_t k = 0; k < count; k++) {
                char field[RANDOM_FIELD];
                size_t length = next_random(&replay) % RANDOM_FIELD;
                for (size_t j = 0; j < length; j++) {
                    field[j] = alphabet[next_random(&replay) % sizeof(alphabet)];
                }
                CHECK(sof_string_from_bytes(&model[k], field, length, NULL) == SOF_STRING_OK);
            }
            sof_csv_error_t rc = sof_csv_reader_next(&r, &fields, &got);
            int same = rc == SOF_CSV_OK && got == count;
            for (size_t k = 0; k < count; k++) {
                same = same && sof_string_cmp(&fields[k], &model[k]) == 0;
                sof_string_free(&model[k]);
            }
            CHECK(same);
        }
        CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_NO_MATCH);
        CHECK(r.records == RANDOM_RECORDS / 4 && r.unterminated == 0);
        sof_csv_reader_free(&r);
    }
}

static void test_null_and_dead(void) {
    source_t source = source_of("a\n", 0);
    sof_csv_reader_t dead_reader;
    sof_csv_writer_t dead_writer;
    CHECK(sof_csv_reader_init(&dead_reader, ',', 0, source_read, &source, NULL) == SOF_CSV_OK);
    CHECK(sof_csv_reader_free(&dead_reader) == SOF_CSV_OK);
    CHECK(sof_csv_writer_init(&dead_writer, ',', 0, NULL, NULL, NULL) == SOF_CSV_OK);
    CHECK(sof_csv_writer_free(&dead_writer) == SOF_CSV_OK);
    sof_csv_reader_t *readers[] = {NULL, &dead_reader};
    sof_csv_writer_t *writers[] = {NULL, &dead_writer};
    sof_string_t field = sof_string_const("x");
    for (size_t i = 0; i < 2; i++) {
        sof_csv_reader_t *r = readers[i];
        sof_csv_writer_t *w = writers[i];
        sof_string_t *fields = &field;
        size_t count = 99, index = 99;
        CHECK(sof_csv_reader_next(r, &fields, &count) == SOF_CSV_ERROR && fields == NULL && count == 0);
        CHECK(sof_csv_reader_clear(r) == SOF_CSV_ERROR);
        CHECK(sof_csv_reader_column(r, "a", &index) == SOF_CSV_ERROR && index == 99);
        count = 99;
        CHECK(sof_csv_reader_header(r, &count) == NULL && count == 0);
        count = 99;
        sof_csv_span_t record = {9, 9};
        CHECK(sof_csv_reader_spans(r, &count, &record) == NULL && count == 0 && record.offset == 0 && record.length == 0);
        CHECK(sof_csv_reader_spans(r, NULL, NULL) == NULL);
        CHECK(sof_csv_writer_field(w, "a") == SOF_CSV_ERROR);
        CHECK(sof_csv_writer_field_bytes(w, "a", 1) == SOF_CSV_ERROR);
        CHECK(sof_csv_writer_record(w, &field, 1) == SOF_CSV_ERROR);
        CHECK(sof_csv_writer_record_end(w) == SOF_CSV_ERROR);
        CHECK(sof_csv_writer_flush(w) == SOF_CSV_ERROR);
        CHECK(sof_csv_writer_clear(w) == SOF_CSV_ERROR);
        CHECK(sof_csv_writer_output(w, &count) == NULL && count == 0);
        CHECK(sof_csv_writer_output(w, NULL) == NULL);
    }
    CHECK(source.calls == 0);
    CHECK(sof_csv_reader_free(NULL) == SOF_CSV_ERROR);
    CHECK(sof_csv_writer_free(NULL) == SOF_CSV_ERROR);
    CHECK(sof_csv_reader_free(&dead_reader) == SOF_CSV_OK);
    CHECK(sof_csv_writer_free(&dead_writer) == SOF_CSV_OK);
}

static void test_reader_no_memory(void) {
    // Fail each allocation in turn until a run makes none fail. A call that
    // fails returns SOF_CSV_NO_MEM, and asking again goes on as if it hadn't
    // failed, so every record comes out whole. Nothing may leak.
    static const char input[] = "name,note,n\r\nalpha,\"a long field, with a comma, \"\"quotes\"\"\nand a line\",1\n"
                                "b,,2\nc,x,y,z,w,v,u\n\"\"\nan unquoted field long enough to grow,x\nlast,\"open";
    static char expected[1024], text[1024], expected_spans[1024], spans[1024];
    source_t source = source_of(input, 5);
    sof_csv_reader_t r;
    CHECK(sof_csv_reader_init(&r, ',', SOF_CSV_HEADER, source_read, &source, NULL) == SOF_CSV_OK);
    CHECK(describe(&r, expected, sizeof(expected)));
    sof_csv_reader_free(&r);
    // And where each record and field is.
    size_t spans_length = 0;
    source = source_of(input, 5);
    CHECK(sof_csv_reader_init(&r, ',', SOF_CSV_HEADER, source_read, &source, NULL) == SOF_CSV_OK);
    while (sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_OK) {
        CHECK(describe_spans(&r, expected_spans, sizeof(expected_spans), &spans_length));
    }
    sof_csv_reader_free(&r);

    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        int failed = 0;
        size_t length = 0;
        spans_length = 0;
        spans[0] = '\0';
        sof_string_t *fields;
        size_t count;
        sof_csv_error_t rc;
        source = source_of(input, 5);
        rc = sof_csv_reader_init(&r, ',', SOF_CSV_HEADER, source_read, &source, &alloc);
        CHECK(rc == SOF_CSV_OK || (rc == SOF_CSV_NO_MEM && r.ready == 0));
        if (rc != SOF_CSV_OK) {
            failed = 1;
            CHECK(sof_csv_reader_init(&r, ',', SOF_CSV_HEADER, source_read, &source, &alloc) == SOF_CSV_OK);
        }
        while ((rc = sof_csv_reader_next(&r, &fields, &count)) != SOF_CSV_NO_MATCH) {
            if (rc == SOF_CSV_NO_MEM) {
                failed = 1;
                continue;
            }
            CHECK(rc == SOF_CSV_OK);
            if (length == 0) {
                size_t names;
                const sof_string_t *header = sof_csv_reader_header(&r, &names);
                length += (size_t)snprintf(text, sizeof(text), "header");
                CHECK(describe_record(header, names, text, sizeof(text), &length));
            }
            CHECK(describe_record(fields, count, text, sizeof(text), &length));
            CHECK(describe_spans(&r, spans, sizeof(spans), &spans_length));
        }
        CHECK(strcmp(text, expected) == 0);
        CHECK(strcmp(spans, expected_spans) == 0);
        // Only the chosen allocation fails.
        CHECK(failed == (state.allocations >= n));
        sof_csv_reader_free(&r);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            break;
        }
    }
}

static void test_writer_no_memory(void) {
    // The same for the writer: a field or record that fails writes nothing,
    // and making the call again finishes the output.
    static char long_field[700];
    memset(long_field, 'q', sizeof(long_field) - 1);
    long_field[100] = '"';
    sof_string_t record[3] = {sof_string_const("one"), sof_string_const("t,wo"), sof_string_const("")};
    sof_csv_writer_t expected;
    CHECK(sof_csv_writer_init(&expected, ',', 0, NULL, NULL, NULL) == SOF_CSV_OK);
    for (int i = 0; i < 4; i++) {
        CHECK(sof_csv_writer_field(&expected, long_field) == SOF_CSV_OK);
        CHECK(sof_csv_writer_field(&expected, "x\"y") == SOF_CSV_OK);
        CHECK(sof_csv_writer_record_end(&expected) == SOF_CSV_OK);
        CHECK(sof_csv_writer_record(&expected, record, 3) == SOF_CSV_OK);
    }

    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_csv_writer_t w;
        int failed = 0;
        sof_csv_error_t rc = sof_csv_writer_init(&w, ',', 0, NULL, NULL, &alloc);
        CHECK(rc == SOF_CSV_OK || (rc == SOF_CSV_NO_MEM && w.ready == 0));
        if (rc != SOF_CSV_OK) {
            failed = 1;
            CHECK(sof_csv_writer_init(&w, ',', 0, NULL, NULL, &alloc) == SOF_CSV_OK);
        }
        for (int i = 0; i < 4; i++) {
            for (int step = 0; step < 4; step++) {
                size_t length = w.out.length, fields = w.fields;
                switch (step) {
                case 0:
                    rc = sof_csv_writer_field(&w, long_field);
                    break;
                case 1:
                    rc = sof_csv_writer_field(&w, "x\"y");
                    break;
                case 2:
                    rc = sof_csv_writer_record_end(&w);
                    break;
                default:
                    rc = sof_csv_writer_record(&w, record, 3);
                    break;
                }
                if (rc == SOF_CSV_NO_MEM) {
                    failed = 1;
                    CHECK(w.out.length == length && w.fields == fields);
                    step--;
                    continue;
                }
                CHECK(rc == SOF_CSV_OK);
            }
        }
        CHECK(w.out.length == expected.out.length && memcmp(w.out.data, expected.out.data, w.out.length) == 0);
        CHECK(failed == (state.allocations >= n));
        sof_csv_writer_free(&w);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            break;
        }
    }
    sof_csv_writer_free(&expected);
}

/// @brief Where csv-spectrum's files are, from the library's directory.
#define SPECTRUM_DIR "../test-data/csv-spectrum"

/// @brief The number of files generated from RFC 4180's grammar.
#define GRAMMAR_FILES 2000

/// @brief Appends a string to a canonical form as test_json_text() makes
/// it: 's', its length in 8 hex digits, and its bytes.
static void canonical_string(char *canon, size_t *n, const char *bytes, size_t length) {
    // 's', 8 digits and the NUL; the tests' strings are far shorter than 4 GB.
    *n += (size_t)snprintf(canon + *n, 10, "s%08zx", length);
    memcpy(canon + *n, bytes, length);
    *n += length;
    canon[*n] = '\0';
}

/// @brief Says whether a byte is TEXTDATA in RFC 4180's grammar:
/// %x20-21 / %x23-2B / %x2D-7E, printable ASCII but the quote and comma.
static int rfc4180_textdata(unsigned char c) {
    return (c >= 0x20 && c <= 0x21) || (c >= 0x23 && c <= 0x2b) || (c >= 0x2d && c <= 0x7e);
}

/// @brief Says whether text is a file in RFC 4180's grammar, exactly:
///
///     file = [header CRLF] record *(CRLF record) [CRLF]
///     record = field *(COMMA field)
///     field = (escaped / non-escaped)
///     escaped = DQUOTE *(TEXTDATA / COMMA / CR / LF / 2DQUOTE) DQUOTE
///     non-escaped = *TEXTDATA
///
/// A header is a record, so [header CRLF] needs nothing of its own.
static int rfc4180_file(const char *s, size_t n) {
    size_t i = 0;
    for (;;) {
        for (;;) {
            if (i < n && s[i] == '"') {
                i++;
                for (;;) {
                    if (i >= n) {
                        return 0;
                    }
                    if (s[i] == '"') {
                        if (i + 1 < n && s[i + 1] == '"') {
                            i += 2;
                            continue;
                        }
                        i++;
                        break;
                    }
                    if (!rfc4180_textdata((unsigned char)s[i]) && s[i] != ',' && s[i] != '\r' && s[i] != '\n') {
                        return 0;
                    }
                    i++;
                }
            } else {
                while (i < n && rfc4180_textdata((unsigned char)s[i])) {
                    i++;
                }
            }
            if (i < n && s[i] == ',') {
                i++;
                continue;
            }
            break;
        }
        if (i == n) {
            return 1;
        }
        if (n - i < 2 || s[i] != '\r' || s[i + 1] != '\n') {
            return 0;
        }
        i += 2;
        if (i == n) {
            return 1;
        }
    }
}

static void test_rfc4180_rules(void) {
    // The rules of RFC 4180, section 2, each with the RFC's own example,
    // read strictly and leniently alike.
    static const unsigned modes[] = {0, SOF_CSV_STRICT};
    for (size_t m = 0; m < LENGTH(modes); m++) {
        unsigned strict = modes[m];
        // 1. Each record is on a separate line, delimited by a line break
        // (CRLF).
        CHECK(reads_as("aaa,bbb,ccc\r\nzzz,yyy,xxx\r\n", ',', strict, "<aaa><bbb><ccc>\n<zzz><yyy><xxx>\n"));
        // 2. The last record in the file may or may not have an ending line
        // break.
        CHECK(reads_as("aaa,bbb,ccc\r\nzzz,yyy,xxx", ',', strict, "<aaa><bbb><ccc>\n<zzz><yyy><xxx>\n"));
        // 3. There maybe an optional header line appearing as the first line
        // of the file with the same format as normal record lines.
        CHECK(reads_as("field_name,field_name,field_name\r\naaa,bbb,ccc\r\nzzz,yyy,xxx\r\n", ',', strict | SOF_CSV_HEADER,
                       "header<field_name><field_name><field_name>\n<aaa><bbb><ccc>\n<zzz><yyy><xxx>\n"));
        // 4. Within the header and each record, there may be one or more
        // fields, separated by commas. Spaces are considered part of a field
        // and should not be ignored.
        CHECK(reads_as("aaa,bbb,ccc", ',', strict, "<aaa><bbb><ccc>\n"));
        CHECK(reads_as(" aaa , bbb\r\nccc", ',', strict, "< aaa >< bbb>\n<ccc>\n"));
        // 5. Each field may or may not be enclosed in double quotes.
        CHECK(reads_as("\"aaa\",\"bbb\",\"ccc\"\r\nzzz,yyy,xxx", ',', strict, "<aaa><bbb><ccc>\n<zzz><yyy><xxx>\n"));
        // 6. Fields containing line breaks (CRLF), double quotes, and commas
        // should be enclosed in double-quotes.
        CHECK(reads_as("\"aaa\",\"b\r\nbb\",\"ccc\"\r\nzzz,yyy,xxx", ',', strict,
                       "<aaa><b\r\nbb><ccc>\n<zzz><yyy><xxx>\n"));
        CHECK(reads_as("\"a,b\",\"c\"\"d\"\r\n", ',', strict, "<a,b><c\"d>\n"));
        // 7. A double-quote appearing inside a field must be escaped by
        // preceding it with another double quote.
        CHECK(reads_as("\"aaa\",\"b\"\"bb\",\"ccc\"", ',', strict, "<aaa><b\"bb><ccc>\n"));
    }
}

static void test_rfc4180_blank_lines(void) {
    // In the grammar, a record can be one empty field, so a blank line is a
    // record: a strict reader returns it, and only the line break at the end
    // of the file is optional. A lenient one skips blank lines.
    CHECK(reads_as("a\r\n\r\nb\r\n", ',', SOF_CSV_STRICT, "<a>\n<>\n<b>\n"));
    CHECK(reads_as("a\r\n\r\n", ',', SOF_CSV_STRICT, "<a>\n<>\n"));
    CHECK(reads_as("\r\n", ',', SOF_CSV_STRICT, "<>\n"));
    CHECK(reads_as("\r\n\r\na", ',', SOF_CSV_STRICT, "<>\n<>\n<a>\n"));
    CHECK(reads_as("a\n\nb\r\r", ',', SOF_CSV_STRICT, "<a>\n<>\n<b>\n<>\n"));
    CHECK(reads_as("a\r\n\r\nb\r\n", ',', 0, "<a>\n<b>\n"));
    CHECK(reads_as("\r\n", ',', 0, ""));
    // A header and a blank line: the blank line is a record of one field.
    CHECK(reads_as("x,y\r\n\r\n1,2", ',', SOF_CSV_STRICT | SOF_CSV_HEADER, "header<x><y>\n<>\n<1><2>\n"));
    // An empty file has no records, where the grammar would give it one
    // empty one.
    CHECK(reads_as("", ',', SOF_CSV_STRICT, ""));
    // Each line's number counts the blank ones.
    source_t source = source_of("a\r\n\r\nb", 1);
    sof_csv_reader_t r;
    CHECK(sof_csv_reader_init(&r, ',', SOF_CSV_STRICT, source_read, &source, NULL) == SOF_CSV_OK);
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_OK && r.record_line == 1);
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_OK && r.record_line == 2);
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_OK && r.record_line == 3);
    CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_NO_MATCH);
    sof_csv_reader_free(&r);
}

static void test_rfc4180_grammar(void) {
    // Files made from RFC 4180's grammar, with fields escaped or not at
    // random, of every byte the grammar allows in each, with and without a
    // header and the last line break, read strictly as the records they were
    // made from.
    static char text[4096], expected[4096];
    static const char escaped_extra[] = {',', '\r', '\n', '"'};
    uint64_t state = 0xa54ff53a5f1d36f1ULL;
    for (int file = 0; file < GRAMMAR_FILES; file++) {
        size_t n = 0, e = 0;
        size_t records = 1 + next_random(&state) % 6;
        size_t fields = 1 + next_random(&state) % 4;
        int header = records > 1 && next_random(&state) % 2;
        int last_empty = 0;
        if (header) {
            e += (size_t)snprintf(expected, sizeof(expected), "header");
        }
        for (size_t r = 0; r < records; r++) {
            if (r > 0) {
                text[n++] = '\r';
                text[n++] = '\n';
            }
            last_empty = 1;
            for (size_t f = 0; f < fields; f++) {
                int escaped = next_random(&state) % 2;
                size_t length = next_random(&state) % 5;
                if (f > 0) {
                    text[n++] = ',';
                }
                if (escaped) {
                    text[n++] = '"';
                }
                expected[e++] = '<';
                for (size_t i = 0; i < length; i++) {
                    char c;
                    uint64_t pick = next_random(&state) % 100;
                    if (escaped && pick < 20) {
                        c = escaped_extra[pick % sizeof(escaped_extra)];
                    } else {
                        // TEXTDATA, but < and >, which describe() uses.
                        do {
                            c = (char)(0x20 + next_random(&state) % 0x5f);
                        } while (!rfc4180_textdata((unsigned char)c) || c == '<' || c == '>');
                    }
                    text[n++] = c;
                    if (c == '"') {
                        // 2DQUOTE
                        text[n++] = '"';
                    }
                    expected[e++] = c;
                }
                if (escaped) {
                    text[n++] = '"';
                }
                expected[e++] = '>';
                last_empty = last_empty && fields == 1 && length == 0 && !escaped;
            }
            expected[e++] = '\n';
        }
        // The final CRLF is optional, but a last record of one empty field,
        // with nothing to show it, needs it: without, the file would read as
        // the records before it with a line break at the end.
        if (last_empty || next_random(&state) % 2) {
            text[n++] = '\r';
            text[n++] = '\n';
        }
        text[n] = '\0';
        expected[e] = '\0';
        CHECK(rfc4180_file(text, n));
        // reads_as() takes a c string, and these hold no NULs.
        CHECK(reads_as(text, ',', SOF_CSV_STRICT | (header ? SOF_CSV_HEADER : 0), expected));
    }
    // The checker refuses what the grammar does.
    CHECK(!rfc4180_file("a\nb", 3) && !rfc4180_file("a\rb", 3) && !rfc4180_file("a\"b", 3));
    CHECK(!rfc4180_file("\"a\"b", 4) && !rfc4180_file("\"a", 2) && !rfc4180_file("a\r\n\r", 4));
    CHECK(!rfc4180_file("\xc3\xa9", 2) && !rfc4180_file("a\tb", 3));
    CHECK(rfc4180_file("\"a\r\n\"\"b\"\r\n", 10) && rfc4180_file("a,,\r\n\r\nb", 8));
}

static void test_rfc4180_writer(void) {
    // Records of every byte RFC 4180 allows in a field, written with CRLF,
    // with and without quoting everything, make a file in its grammar
    // exactly, which reads back strictly as the same records.
    static char expected[TEXT_SIZE], text[TEXT_SIZE];
    uint64_t state = 0x510e527fade682d1ULL;
    for (int round = 0; round < 2; round++) {
        sof_csv_writer_t w;
        size_t e = 0;
        CHECK(sof_csv_writer_init(&w, ',', SOF_CSV_CRLF | (round ? SOF_CSV_QUOTE_ALL : 0), NULL, NULL, NULL) == SOF_CSV_OK);
        for (int r = 0; r < 300; r++) {
            size_t fields = 1 + next_random(&state) % 4;
            for (size_t f = 0; f < fields; f++) {
                char field[8];
                size_t length = next_random(&state) % sizeof(field);
                expected[e++] = '<';
                for (size_t i = 0; i < length; i++) {
                    char c;
                    do {
                        c = (char)(next_random(&state) % 0x7f);
                    } while (!(rfc4180_textdata((unsigned char)c) || c == ',' || c == '\r' || c == '\n' || c == '"') || c == '<' || c == '>');
                    field[i] = c;
                    expected[e++] = c;
                }
                expected[e++] = '>';
                CHECK(sof_csv_writer_field_bytes(&w, field, length) == SOF_CSV_OK);
            }
            expected[e++] = '\n';
            CHECK(sof_csv_writer_record_end(&w) == SOF_CSV_OK);
        }
        expected[e] = '\0';
        CHECK(rfc4180_file(w.out.data, w.out.length));
        CHECK(w.out.length < sizeof(text));
        memcpy(text, w.out.data, w.out.length + 1);
        sof_csv_writer_free(&w);
        CHECK(reads_as(text, ',', SOF_CSV_STRICT, expected));
    }
}

static void test_csv_spectrum(void) {
    // Each of csv-spectrum's files, read with its header, is the records its
    // JSON file holds: an array of objects of the fields keyed by the
    // header's names, or for one record, the object alone. Those in RFC
    // 4180's grammar are read strictly.
    //
    // location_coordinates isn't: its second field has quotes in it without
    // being quoted, so a strict reader refuses it on line 2, and it is read
    // leniently. And its JSON file has the phone number 1234567890 where its
    // CSV file has 2095257564, which no reader could match, so that one
    // field is replaced before comparing; the files are kept as they came.
    static const struct {
        const char *name;
        size_t bad_line;
        const char *csv_value;
        const char *json_value;
    } cases[] = {
        {"comma_in_quotes", 0, NULL, NULL},
        {"empty", 0, NULL, NULL},
        {"empty_crlf", 0, NULL, NULL},
        {"escaped_quotes", 0, NULL, NULL},
        {"json", 0, NULL, NULL},
        {"location_coordinates", 2, "2095257564", "1234567890"},
        {"newlines", 0, NULL, NULL},
        {"newlines_crlf", 0, NULL, NULL},
        {"quotes_and_newlines", 0, NULL, NULL},
        {"simple", 0, NULL, NULL},
        {"simple_crlf", 0, NULL, NULL},
        {"utf8", 0, NULL, NULL},
    };
    static char json[TEXT_SIZE], expected[TEXT_SIZE], canon[TEXT_SIZE];
    for (size_t i = 0; i < LENGTH(cases); i++) {
        char path[256];
        size_t n = 0, expected_length, json_length;
        sof_csv_reader_t r;
        sof_string_t *fields;
        size_t count;
        sof_csv_error_t rc;
        int ok = 1;
        unsigned flags = SOF_CSV_HEADER | SOF_CSV_STRICT;
        if (cases[i].bad_line != 0) {
            snprintf(path, sizeof(path), "%s/csvs/%s.csv", SPECTRUM_DIR, cases[i].name);
            FILE *f = fopen(path, "rb");
            CHECK(f != NULL);
            CHECK(sof_csv_reader_init(&r, ',', flags, sof_csv_read_file, f, NULL) == SOF_CSV_OK);
            CHECK(sof_csv_reader_next(&r, NULL, NULL) == SOF_CSV_SYNTAX && r.line == cases[i].bad_line);
            sof_csv_reader_free(&r);
            fclose(f);
            flags = SOF_CSV_HEADER;
        }
        snprintf(path, sizeof(path), "%s/json/%s.json", SPECTRUM_DIR, cases[i].name);
        FILE *f = fopen(path, "rb");
        CHECK(f != NULL);
        json_length = fread(json, 1, sizeof(json), f);
        fclose(f);
        CHECK(test_json_text(json, json_length, expected, sizeof(expected), &expected_length));

        snprintf(path, sizeof(path), "%s/csvs/%s.csv", SPECTRUM_DIR, cases[i].name);
        f = fopen(path, "rb");
        CHECK(f != NULL);
        CHECK(sof_csv_reader_init(&r, ',', flags, sof_csv_read_file, f, NULL) == SOF_CSV_OK);
        canon[n++] = '[';
        while ((rc = sof_csv_reader_next(&r, &fields, &count)) == SOF_CSV_OK) {
            size_t names_count;
            const sof_string_t *header = sof_csv_reader_header(&r, &names_count);
            ok = ok && count == names_count && n + 1024 < sizeof(canon);
            canon[n++] = '{';
            for (size_t k = 0; ok && k < count; k++) {
                const char *value = fields[k].data;
                size_t length = fields[k].length;
                if (cases[i].csv_value != NULL && strcmp(value, cases[i].csv_value) == 0) {
                    value = cases[i].json_value;
                    length = strlen(value);
                }
                canonical_string(canon, &n, header[k].data, header[k].length);
                canonical_string(canon, &n, value, length);
            }
            canon[n++] = '}';
        }
        canon[n++] = ']';
        canon[n] = '\0';
        sof_csv_reader_free(&r);
        fclose(f);
        if (expected[0] == '{') {
            // One record, given as the object alone.
            ok = ok && n >= 2 && expected_length == n - 2 && memcmp(canon + 1, expected, n - 2) == 0;
        } else {
            ok = ok && expected_length == n && memcmp(canon, expected, n) == 0;
        }
        if (!ok || rc != SOF_CSV_NO_MATCH) {
            printf("(%s) ", cases[i].name);
        }
        CHECK(rc == SOF_CSV_NO_MATCH && ok);
    }
}

int main(void) {
    printf("sof_csv:\n");
    run("reader init", test_reader_init);
    run("reader fields", test_reader_fields);
    run("reader line breaks", test_reader_line_breaks);
    run("reader quotes", test_reader_quotes);
    run("reader leniency", test_reader_lenient);
    run("reader strictness", test_reader_strict);
    run("reader line numbers", test_reader_lines);
    run("reader header and columns", test_reader_header);
    run("reader fields reused", test_reader_fields_reused);
    run("reader spans", test_reader_spans);
    run("reader runs of ordinary bytes", test_reader_runs);
    run("reader read errors", test_reader_read_errors);
    run("read file", test_read_file);
    run("writer", test_writer);
    run("writer output and flushing", test_writer_output);
    run("writer looking for bytes to quote", test_writer_runs);
    run("random records written and read back", test_round_trip);
    run("RFC 4180: the rules of section 2", test_rfc4180_rules);
    run("RFC 4180: blank lines", test_rfc4180_blank_lines);
    run("RFC 4180: files made from the grammar", test_rfc4180_grammar);
    run("RFC 4180: the writer's output", test_rfc4180_writer);
    run("csv-spectrum", test_csv_spectrum);
    run("NULL and freed readers and writers", test_null_and_dead);
    run("reader with each allocation failing", test_reader_no_memory);
    run("writer with each allocation failing", test_writer_no_memory);
    return test_report();
}
