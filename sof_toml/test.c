/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_toml: the document and the writer.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it. The
 * tests of toml-test, the TOML project's own, are read from
 * ../test-data/toml-test/, and their JSON with sof_json.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_json.h"
#include "sof_toml.h"
#include "test.h"

/// @brief The number of elements in an array.
#define LENGTH(a) (sizeof(a) / sizeof((a)[0]))

/// @brief Where toml-test's tests are, and the list of those for TOML 1.1.0.
#define SUITE_DIR "../test-data/toml-test/tests"
#define SUITE_LIST SUITE_DIR "/files-toml-1.1.0"

/// @brief The numbers of toml-test's valid and invalid tests for TOML 1.1.0.
#define SUITE_VALID 214
#define SUITE_INVALID 467

/// @brief The number of random documents written and read.
#define RANDOM_DOCUMENTS 200

/// @brief The deepest the random documents' tables nest.
#define RANDOM_DEPTH 4

/// @brief The size of a sink's buffer.
#define SINK_SIZE 65536

/// @brief What a write function in the tests collects, through its ctx.
typedef struct sink_t {
    /// @brief The bytes written.
    char data[SINK_SIZE];
    size_t length;
    /// @brief The calls made, failed ones included.
    size_t calls;
    /// @brief Non-zero to fail every call.
    int fail;
} sink_t;

/// @brief Input given to a document a piece at a time, by chunks_read(),
/// through its ctx.
typedef struct chunks_t {
    const char *data;
    size_t length;
    size_t position;
    /// @brief The most bytes given at a time.
    size_t chunk;
    /// @brief The call to fail, counting from 1, or 0 for none.
    size_t fail_at;
    size_t calls;
} chunks_t;

/// @brief A text that isn't TOML, the error it gives, and where.
typedef struct error_row_t {
    const char *text;
    sof_toml_error_t code;
    size_t line;
    size_t column;
} error_row_t;

/// @brief What a test of toml-test's runs on: the test's name, without
/// valid/ or invalid/ or .toml, and its TOML text, and for a valid test, its
/// JSON, as a document.
typedef int (*suite_func_t)(const char *name, const char *toml, size_t length, const sof_json_value_t *json, void *ctx);

static int chunks_read(char *buffer, size_t size, size_t *length, void *ctx);
static int64_t datetime_instant(sof_toml_type_t type, const sof_toml_datetime_t *dt);
static int encode(sof_toml_writer_t *w, const sof_json_value_t *j);
static int encode_members(sof_toml_writer_t *w, const sof_json_value_t *object);
static int encode_is_section(const sof_json_value_t *j);
static int error_is(const char *text, sof_toml_error_t code, size_t line, size_t column);
static char *file_read(const char *path, size_t *length);
static int matches(const sof_toml_value_t *t, const sof_json_value_t *j);
static int output_is(const sof_toml_writer_t *w, const char *expected);
static int parses_as(const char *toml, const char *expected);
static void random_key(char *key, size_t size, uint64_t *state, int n);
static int random_table(sof_toml_writer_t *w, uint64_t *state, int depth, int section);
static int random_value(sof_toml_writer_t *w, uint64_t *state, int depth);
static int rewrite_is(const char *toml, const char *expected);
static int sink_write(const char *data, size_t length, void *ctx);
static int span_is(const char *text, sof_toml_span_t span, const char *expected);
static int suite_run(int valid, suite_func_t func, void *ctx, int *count);
static int tagged(const sof_json_value_t *j, const char **type, const char **value, size_t *length);

/// @brief A document with something of everything, the tests' sample.
static const char sample_text[] =
    "# A sample\n"
    "title = \"TOML Example\"\n"
    "count = 42\n"
    "ratio = 0.5\n"
    "enabled = true\n"
    "when = 1979-05-27T07:32:00-08:00\n"
    "\n"
    "[owner]\n"
    "name = \"Tom\"\n"
    "\n"
    "[database]\n"
    "ports = [ 8000, 8001, 8002 ]\n"
    "data = [ [\"delta\", \"phi\"], [3.14] ]\n"
    "temp = { cpu = 79.5, case = 72.0 }\n"
    "\n"
    "[servers.alpha]\n"
    "ip = \"10.0.0.1\"\n"
    "\n"
    "[[products]]\n"
    "name = \"Hammer\"\n"
    "sku = 738594937\n"
    "\n"
    "[[products]]\n"
    "\n"
    "[[products]]\n"
    "name = \"Nail\"\n"
    "color = \"gray\"\n";

/// @brief The sample as the writer writes it again.
static const char sample_written[] =
    "title = \"TOML Example\"\n"
    "count = 42\n"
    "ratio = 0.5\n"
    "enabled = true\n"
    "when = 1979-05-27T07:32:00-08:00\n"
    "\n"
    "[owner]\n"
    "name = \"Tom\"\n"
    "\n"
    "[database]\n"
    "ports = [8000, 8001, 8002]\n"
    "data = [[\"delta\", \"phi\"], [3.14]]\n"
    "temp = {cpu = 79.5, case = 72.0}\n"
    "\n"
    "[servers.alpha]\n"
    "ip = \"10.0.0.1\"\n"
    "\n"
    "[[products]]\n"
    "name = \"Hammer\"\n"
    "sku = 738594937\n"
    "\n"
    "[[products]]\n"
    "\n"
    "[[products]]\n"
    "name = \"Nail\"\n"
    "color = \"gray\"\n";

/// @brief A read function giving chunks_t's data a chunk at a time, and
/// failing the call fail_at.
static int chunks_read(char *buffer, size_t size, size_t *length, void *ctx) {
    chunks_t *c = ctx;
    c->calls++;
    if (c->fail_at != 0 && c->calls == c->fail_at) {
        return 1;
    }
    size_t n = c->length - c->position;
    if (n > c->chunk) {
        n = c->chunk;
    }
    if (n > size) {
        n = size;
    }
    memcpy(buffer, c->data + c->position, n);
    c->position += n;
    *length = n;
    return 0;
}

/// @brief Gives the second a date and time stands for, from
/// 0000-01-01T00:00:00Z, counting an offset date-time's offset, so two can
/// be compared as toml-test compares them, with their nanoseconds; for a
/// local time, from midnight.
static int64_t datetime_instant(sof_toml_type_t type, const sof_toml_datetime_t *dt) {
    int64_t days = 0;
    if (type != SOF_TOML_TIME_LOCAL) {
        // Days from the civil date, by Howard Hinnant's algorithm.
        int64_t y = dt->year - (dt->month <= 2);
        int64_t era = (y >= 0 ? y : y - 399) / 400;
        int64_t yoe = y - era * 400;
        int64_t doy = (153 * (dt->month + (dt->month > 2 ? -3 : 9)) + 2) / 5 + dt->day - 1;
        days = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy;
    }
    int64_t seconds = days * 86400 + dt->hour * 3600 + dt->minute * 60 + dt->second;
    if (type == SOF_TOML_DATETIME) {
        seconds -= dt->offset * 60;
    }
    return seconds;
}

/// @brief Writes a value of toml-test's tagged JSON with the writer: a
/// tagged value as its type, an object as a table, an array as an array.
/// @return 1, or 0 if a call failed or the JSON isn't tagged JSON.
static int encode(sof_toml_writer_t *w, const sof_json_value_t *j) {
    const char *type, *value;
    size_t length;
    if (tagged(j, &type, &value, &length)) {
        if (strcmp(type, "string") == 0) {
            return sof_toml_writer_string_bytes(w, value, length) == SOF_TOML_OK;
        }
        if (strcmp(type, "integer") == 0) {
            int64_t i;
            return sof_json_number_int(value, length, &i) == SOF_JSON_OK && sof_toml_writer_int(w, i) == SOF_TOML_OK;
        }
        if (strcmp(type, "float") == 0) {
            const char *v = value + (value[0] == '+' || value[0] == '-');
            double d = strcmp(v, "nan") == 0 ? NAN : strcmp(v, "inf") == 0 ? HUGE_VAL : strtod(value, NULL);
            if (value[0] == '-' && (isnan(d) || isinf(d))) {
                d = -d;
            }
            return sof_toml_writer_double(w, d) == SOF_TOML_OK;
        }
        if (strcmp(type, "bool") == 0) {
            return sof_toml_writer_bool(w, strcmp(value, "true") == 0) == SOF_TOML_OK;
        }
        sof_toml_type_t t;
        sof_toml_datetime_t dt;
        return sof_toml_datetime_parse(value, length, &t, &dt) == SOF_TOML_OK && sof_toml_writer_datetime(w, t, &dt) == SOF_TOML_OK;
    }
    size_t count = sof_json_value_count(j);
    if (sof_json_value_type(j) == SOF_JSON_ARRAY) {
        if (encode_is_section(j)) {
            // An array of tables, after a key in a section, is written as
            // [[key]] sections.
            if (sof_toml_writer_table_array_begin(w) != SOF_TOML_OK) {
                return 0;
            }
        } else if (sof_toml_writer_array_begin(w) != SOF_TOML_OK) {
            return 0;
        }
        for (size_t i = 0; i < count; i++) {
            if (!encode(w, sof_json_value_at(j, i))) {
                return 0;
            }
        }
        return sof_toml_writer_array_end(w) == SOF_TOML_OK;
    }
    if (sof_json_value_type(j) != SOF_JSON_OBJECT || sof_toml_writer_table_begin(w) != SOF_TOML_OK) {
        return 0;
    }
    return encode_members(w, j) && sof_toml_writer_table_end(w) == SOF_TOML_OK;
}

/// @brief Writes an object's members as a table's, those that are written
/// as sections last.
static int encode_members(sof_toml_writer_t *w, const sof_json_value_t *object) {
    for (int sections = 0; sections < 2; sections++) {
        for (size_t i = 0; i < sof_json_value_count(object); i++) {
            const sof_json_member_t *m = sof_json_value_member(object, i);
            if (encode_is_section(m->value) != sections) {
                continue;
            }
            if (sof_toml_writer_key_bytes(w, m->key, m->key_length) != SOF_TOML_OK || !encode(w, m->value)) {
                return 0;
            }
        }
    }
    return 1;
}

/// @brief Says whether a tagged JSON value is a table, or an array of
/// tables, which a section writes after its key/value pairs.
static int encode_is_section(const sof_json_value_t *j) {
    if (sof_json_value_type(j) == SOF_JSON_OBJECT) {
        return !tagged(j, NULL, NULL, NULL);
    }
    if (sof_json_value_type(j) != SOF_JSON_ARRAY || sof_json_value_count(j) == 0) {
        return 0;
    }
    for (size_t i = 0; i < sof_json_value_count(j); i++) {
        const sof_json_value_t *e = sof_json_value_at(j, i);
        if (sof_json_value_type(e) != SOF_JSON_OBJECT || tagged(e, NULL, NULL, NULL)) {
            return 0;
        }
    }
    return 1;
}

/// @brief Says whether parsing text fails with code at line and column,
/// with a message, leaving the document empty.
static int error_is(const char *text, sof_toml_error_t code, size_t line, size_t column) {
    sof_toml_doc_t d;
    sof_toml_doc_init(&d, NULL);
    sof_toml_error_t rc = sof_toml_doc_parse(&d, text, strlen(text));
    int ok = rc == code && d.line == line && d.column == column && d.message != NULL && d.root == NULL;
    if (!ok) {
        printf("(\"%s\" gave %d at %zu:%zu: %s) ", text, (int)rc, d.line, d.column, d.message != NULL ? d.message : "no message");
    }
    sof_toml_doc_free(&d);
    return ok;
}

/// @brief Reads a whole file.
/// @return The bytes, from malloc(), with their number in *length, or NULL.
static char *file_read(const char *path, size_t *length) {
    FILE *f = fopen(path, "rb");
    char *data = NULL;
    size_t size = 0, n = 0;
    if (f == NULL) {
        return NULL;
    }
    for (;;) {
        if (n == size) {
            size = size == 0 ? 4096 : size * 2;
            char *grown = realloc(data, size + 1);
            if (grown == NULL) {
                free(data);
                fclose(f);
                return NULL;
            }
            data = grown;
        }
        size_t got = fread(data + n, 1, size - n, f);
        if (got == 0) {
            break;
        }
        n += got;
    }
    fclose(f);
    data[n] = '\0';
    *length = n;
    return data;
}

/// @brief Says whether a document's value holds what toml-test's tagged
/// JSON says, compared as toml-test compares them: floats by value, NaNs
/// alike whatever their sign, and dates and times by the moment they stand
/// for.
static int matches(const sof_toml_value_t *t, const sof_json_value_t *j) {
    const char *type, *value;
    size_t length;
    if (t == NULL) {
        return 0;
    }
    if (tagged(j, &type, &value, &length)) {
        if (strcmp(type, "string") == 0) {
            return t->type == SOF_TOML_STRING && t->length == length && memcmp(t->as.text, value, length) == 0;
        }
        if (strcmp(type, "integer") == 0) {
            int64_t i;
            return t->type == SOF_TOML_INTEGER && sof_json_number_int(value, length, &i) == SOF_JSON_OK && i == t->as.integer;
        }
        if (strcmp(type, "float") == 0) {
            const char *v = value + (value[0] == '+' || value[0] == '-');
            if (t->type != SOF_TOML_FLOAT) {
                return 0;
            }
            if (strcmp(v, "nan") == 0) {
                return isnan(t->as.number);
            }
            if (strcmp(v, "inf") == 0) {
                return isinf(t->as.number) && (t->as.number < 0) == (value[0] == '-');
            }
            return strtod(value, NULL) == t->as.number;
        }
        if (strcmp(type, "bool") == 0) {
            return t->type == SOF_TOML_BOOL && t->as.boolean == (strcmp(value, "true") == 0);
        }
        sof_toml_type_t want_type;
        sof_toml_datetime_t want;
        if (sof_toml_datetime_parse(value, length, &want_type, &want) != SOF_TOML_OK || t->type != want_type) {
            return 0;
        }
        const char *names[] = {"datetime", "datetime-local", "date-local", "time-local"};
        return strcmp(type, names[want_type - SOF_TOML_DATETIME]) == 0 && datetime_instant(want_type, &want) == datetime_instant(t->type, &t->as.datetime) && want.nanosecond == t->as.datetime.nanosecond;
    }
    size_t count = sof_json_value_count(j);
    if (sof_json_value_type(j) == SOF_JSON_ARRAY) {
        if (t->type != SOF_TOML_ARRAY || t->length != count) {
            return 0;
        }
        for (size_t i = 0; i < count; i++) {
            if (!matches(sof_toml_value_at(t, i), sof_json_value_at(j, i))) {
                return 0;
            }
        }
        return 1;
    }
    if (sof_json_value_type(j) != SOF_JSON_OBJECT || t->type != SOF_TOML_TABLE || t->length != count) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        const sof_json_member_t *m = sof_json_value_member(j, i);
        if (!matches(sof_toml_value_get_bytes(t, m->key, m->key_length), m->value)) {
            return 0;
        }
    }
    return 1;
}

/// @brief Says whether the writer's output is exactly expected.
static int output_is(const sof_toml_writer_t *w, const char *expected) {
    size_t length;
    const char *out = sof_toml_writer_output(w, &length);
    if (out == NULL || length != strlen(expected) || memcmp(out, expected, length) != 0) {
        printf("(wrote \"%s\") ", out != NULL ? out : "nothing");
        return 0;
    }
    return 1;
}

/// @brief Says whether a TOML text parses as the tagged JSON expected.
static int parses_as(const char *toml, const char *expected) {
    sof_toml_doc_t d;
    sof_json_doc_t j;
    sof_toml_doc_init(&d, NULL);
    sof_json_doc_init(&j, NULL);
    sof_toml_error_t rc = sof_toml_doc_parse(&d, toml, strlen(toml));
    int ok = rc == SOF_TOML_OK && sof_json_doc_parse(&j, expected, strlen(expected)) == SOF_JSON_OK && matches(d.root, j.root);
    if (!ok) {
        printf("(\"%s\" gave %d: %s at %zu:%zu) ", toml, (int)rc, d.message != NULL ? d.message : "", d.line, d.column);
    }
    sof_toml_doc_free(&d);
    sof_json_doc_free(&j);
    return ok;
}

/// @brief Makes a key for the nth member of a random table: bare, or with
/// characters that need it quoted.
static void random_key(char *key, size_t size, uint64_t *state, int n) {
    static const char *forms[] = {"k%d", "key-%d", "a b %d", "caf\xc3\xa9%d", "q\"%d", "%d", "dot.%d", "tab\t%d"};
    snprintf(key, size, forms[next_random(state) % LENGTH(forms)], n);
}

/// @brief Writes a random table's members: key/value pairs, then, if it is
/// a section, tables and arrays of tables.
/// @return 1, or 0 if a call failed.
static int random_table(sof_toml_writer_t *w, uint64_t *state, int depth, int section) {
    char key[32];
    int n = 0;
    int values = (int)(next_random(state) % 5);
    for (int i = 0; i < values; i++) {
        random_key(key, sizeof(key), state, n++);
        if (sof_toml_writer_key(w, key) != SOF_TOML_OK || !random_value(w, state, depth)) {
            return 0;
        }
    }
    if (!section || depth >= RANDOM_DEPTH) {
        return 1;
    }
    int tables = (int)(next_random(state) % 3);
    for (int i = 0; i < tables; i++) {
        random_key(key, sizeof(key), state, n++);
        int aot = next_random(state) % 3 == 0;
        if (sof_toml_writer_key(w, key) != SOF_TOML_OK) {
            return 0;
        }
        if (aot) {
            int elements = 1 + (int)(next_random(state) % 3);
            if (sof_toml_writer_table_array_begin(w) != SOF_TOML_OK) {
                return 0;
            }
            for (int e = 0; e < elements; e++) {
                if (sof_toml_writer_table_begin(w) != SOF_TOML_OK || !random_table(w, state, depth + 1, 1) || sof_toml_writer_table_end(w) != SOF_TOML_OK) {
                    return 0;
                }
            }
            if (sof_toml_writer_array_end(w) != SOF_TOML_OK) {
                return 0;
            }
        } else if (sof_toml_writer_table_begin(w) != SOF_TOML_OK || !random_table(w, state, depth + 1, 1) || sof_toml_writer_table_end(w) != SOF_TOML_OK) {
            return 0;
        }
    }
    return 1;
}

/// @brief Writes a random value: a string, number, boolean, date or time,
/// or an inline array or table.
/// @return 1, or 0 if a call failed.
static int random_value(sof_toml_writer_t *w, uint64_t *state, int depth) {
    static const char *strings[] = {"", "plain", "quote \" and \\ backslash", "line\nbreak\ttab", "caf\xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80",
                                    "nul \0 inside", "del \x7f and esc \x1b"};
    uint64_t r = next_random(state);
    int kind = (int)(r % (depth >= RANDOM_DEPTH ? 6 : 8));
    sof_toml_datetime_t dt = {1900 + (int)(r % 200), 1 + (int)(r % 12), 1 + (int)(r % 28), (int)(r % 24), (int)(r % 60),
                               (int)(r % 60), (uint32_t)(r % 3 == 0 ? r % 1000000000 : 0), (int)(r % 1439) - 719};
    switch (kind) {
    case 0: {
        size_t i = (size_t)(r >> 8) % LENGTH(strings);
        size_t length = i == 5 ? 13 : strlen(strings[i]);
        return sof_toml_writer_string_bytes(w, strings[i], length) == SOF_TOML_OK;
    }
    case 1:
        return sof_toml_writer_int(w, (int64_t)next_random(state)) == SOF_TOML_OK;
    case 2: {
        double d;
        uint64_t bits = next_random(state);
        memcpy(&d, &bits, sizeof(d));
        return sof_toml_writer_double(w, isnan(d) ? 0.25 : d) == SOF_TOML_OK;
    }
    case 3:
        return sof_toml_writer_bool(w, (int)(r & 1)) == SOF_TOML_OK;
    case 4:
    case 5:
        return sof_toml_writer_datetime(w, (sof_toml_type_t)(SOF_TOML_DATETIME + (r >> 4) % 4), &dt) == SOF_TOML_OK;
    case 6: {
        int count = (int)(next_random(state) % 4);
        if (sof_toml_writer_array_begin(w) != SOF_TOML_OK) {
            return 0;
        }
        for (int i = 0; i < count; i++) {
            if (!random_value(w, state, depth + 1)) {
                return 0;
            }
        }
        return sof_toml_writer_array_end(w) == SOF_TOML_OK;
    }
    default:
        return sof_toml_writer_inline_begin(w) == SOF_TOML_OK && random_table(w, state, depth + 1, 0) && sof_toml_writer_table_end(w) == SOF_TOML_OK;
    }
}

/// @brief Says whether a TOML text, parsed and written again, is exactly
/// expected.
static int rewrite_is(const char *toml, const char *expected) {
    sof_toml_doc_t d;
    sof_toml_writer_t w;
    sof_toml_doc_init(&d, NULL);
    sof_toml_writer_init(&w, 0, NULL, NULL, NULL);
    int ok = sof_toml_doc_parse(&d, toml, strlen(toml)) == SOF_TOML_OK && sof_toml_writer_value(&w, d.root) == SOF_TOML_OK && output_is(&w, expected);
    sof_toml_writer_free(&w);
    sof_toml_doc_free(&d);
    return ok;
}

/// @brief A write function collecting output in the sink_t in ctx, or
/// failing if it is set to.
static int sink_write(const char *data, size_t length, void *ctx) {
    sink_t *sink = ctx;
    sink->calls++;
    if (sink->fail || sink->length + length > sizeof(sink->data)) {
        return 1;
    }
    memcpy(sink->data + sink->length, data, length);
    sink->length += length;
    return 0;
}

/// @brief Says whether span covers exactly the text expected in text.
static int span_is(const char *text, sof_toml_span_t span, const char *expected) {
    size_t length = strlen(expected);
    if (span.offset > strlen(text) || span.length > strlen(text) - span.offset || span.length != length || memcmp(text + span.offset, expected, length) != 0) {
        printf("(span %zu+%zu is \"%.*s\", not \"%s\") ", span.offset, span.length, (int)span.length, text + span.offset, expected);
        return 0;
    }
    return 1;
}

/// @brief Runs func on each of toml-test's valid tests, or its invalid ones,
/// for TOML 1.1.0, counting them in *count.
/// @return 1 if func returned 1 for each, or 0, having named those it
/// didn't, or if the files couldn't be read.
static int suite_run(int valid, suite_func_t func, void *ctx, int *count) {
    size_t list_length;
    char *list = file_read(SUITE_LIST, &list_length);
    const char *prefix = valid ? "valid/" : "invalid/";
    int ok = list != NULL;
    *count = 0;
    for (char *line = list; ok && line != NULL && *line != '\0';) {
        char *end = strchr(line, '\n');
        if (end != NULL) {
            *end = '\0';
        }
        size_t n = strlen(line);
        if (strncmp(line, prefix, strlen(prefix)) == 0 && n > 5 && strcmp(line + n - 5, ".toml") == 0) {
            char path[512];
            size_t toml_length, json_length;
            snprintf(path, sizeof(path), "%s/%s", SUITE_DIR, line);
            char *toml = file_read(path, &toml_length);
            char *json = NULL;
            sof_json_doc_t j;
            sof_json_doc_init(&j, NULL);
            if (valid) {
                snprintf(path + strlen(path) - 5, 6, ".json");
                json = file_read(path, &json_length);
            }
            if (toml == NULL || (valid && (json == NULL || sof_json_doc_parse(&j, json, json_length) != SOF_JSON_OK))) {
                printf("(can't read %s) ", line);
                ok = 0;
            } else {
                line[n - 5] = '\0';
                if (!func(line + strlen(prefix), toml, toml_length, j.root, ctx)) {
                    ok = 0;
                }
                (*count)++;
            }
            sof_json_doc_free(&j);
            free(toml);
            free(json);
        }
        line = end != NULL ? end + 1 : NULL;
    }
    free(list);
    return ok;
}

/// @brief Says whether a JSON value is a toml-test tagged value, an object
/// of two strings, type and value, and if so, gives them.
static int tagged(const sof_json_value_t *j, const char **type, const char **value, size_t *length) {
    if (sof_json_value_type(j) != SOF_JSON_OBJECT || sof_json_value_count(j) != 2) {
        return 0;
    }
    const char *t = sof_json_value_string(sof_json_value_get(j, "type"), NULL);
    size_t n;
    const char *v = sof_json_value_string(sof_json_value_get(j, "value"), &n);
    if (t == NULL || v == NULL) {
        return 0;
    }
    if (type != NULL) {
        *type = t;
        *value = v;
        *length = n;
    }
    return 1;
}

static void test_doc_values(void) {
    sof_toml_doc_t d;
    int64_t i;
    double x;
    int b;
    size_t length;
    sof_toml_datetime_t dt;
    CHECK(sof_toml_doc_init(&d, NULL) == SOF_TOML_OK);
    CHECK(sof_toml_doc_parse(&d, sample_text, strlen(sample_text)) == SOF_TOML_OK);
    CHECK(d.message == NULL && d.line == 0);
    const sof_toml_value_t *root = d.root;
    CHECK(sof_toml_value_type(root) == SOF_TOML_TABLE && sof_toml_value_count(root) == 9);
    CHECK(strcmp(sof_toml_value_string(sof_toml_value_get(root, "title"), &length), "TOML Example") == 0 && length == 12);
    CHECK(sof_toml_value_int(sof_toml_value_get(root, "count"), &i) == SOF_TOML_OK && i == 42);
    CHECK(sof_toml_value_double(sof_toml_value_get(root, "count"), &x) == SOF_TOML_OK && x == 42.0);
    CHECK(sof_toml_value_double(sof_toml_value_get(root, "ratio"), &x) == SOF_TOML_OK && x == 0.5);
    CHECK(sof_toml_value_int(sof_toml_value_get(root, "ratio"), &i) == SOF_TOML_NO_MATCH);
    CHECK(sof_toml_value_bool(sof_toml_value_get(root, "enabled"), &b) == SOF_TOML_OK && b == 1);
    const sof_toml_value_t *when = sof_toml_value_get(root, "when");
    CHECK(sof_toml_value_type(when) == SOF_TOML_DATETIME);
    CHECK(sof_toml_value_datetime(when, &dt) == SOF_TOML_OK);
    CHECK(dt.year == 1979 && dt.month == 5 && dt.day == 27 && dt.hour == 7 && dt.minute == 32 && dt.second == 0 && dt.offset == -480);
    // Members are in the order they were defined.
    const char *keys[] = {"title", "count", "ratio", "enabled", "when", "owner", "database", "servers", "products"};
    for (size_t k = 0; k < LENGTH(keys); k++) {
        const sof_toml_member_t *m = sof_toml_value_member(root, k);
        CHECK(m != NULL && strcmp(m->key, keys[k]) == 0 && m->key_length == strlen(keys[k]) && m->table == root);
    }
    CHECK(sof_toml_value_member(root, 9) == NULL);
    const sof_toml_value_t *database = sof_toml_value_get(root, "database");
    const sof_toml_value_t *ports = sof_toml_value_get(database, "ports");
    CHECK(sof_toml_value_type(ports) == SOF_TOML_ARRAY && sof_toml_value_count(ports) == 3);
    CHECK(sof_toml_value_int(sof_toml_value_at(ports, 2), &i) == SOF_TOML_OK && i == 8002);
    CHECK(sof_toml_value_at(ports, 3) == NULL);
    CHECK(strcmp(sof_toml_value_string(sof_toml_value_at(sof_toml_value_at(sof_toml_value_get(database, "data"), 0), 1), NULL), "phi") == 0);
    CHECK(sof_toml_value_double(sof_toml_value_get(sof_toml_value_get(database, "temp"), "cpu"), &x) == SOF_TOML_OK && x == 79.5);
    const sof_toml_value_t *products = sof_toml_value_get(root, "products");
    CHECK(sof_toml_value_count(products) == 3 && sof_toml_value_count(sof_toml_value_at(products, 1)) == 0);
    CHECK(strcmp(sof_toml_value_string(sof_toml_value_get(sof_toml_value_at(products, 2), "color"), NULL), "gray") == 0);
    CHECK(sof_toml_value_get(sof_toml_value_get(sof_toml_value_get(root, "servers"), "alpha"), "ip") != NULL);
    // Wrong types find nothing.
    CHECK(sof_toml_value_string(sof_toml_value_get(root, "count"), &length) == NULL && length == 0);
    CHECK(sof_toml_value_bool(sof_toml_value_get(root, "title"), &b) == SOF_TOML_NO_MATCH);
    CHECK(sof_toml_value_datetime(sof_toml_value_get(root, "title"), &dt) == SOF_TOML_NO_MATCH);
    CHECK(sof_toml_value_get(ports, "x") == NULL && sof_toml_value_at(root, 0) == NULL && sof_toml_value_member(ports, 0) == NULL);
    CHECK(sof_toml_value_count(sof_toml_value_get(root, "title")) == 0);
    CHECK(sof_toml_value_get(root, "missing") == NULL);
    // Parsing again replaces the document; clearing empties it.
    CHECK(sof_toml_doc_parse(&d, "a = 1", 5) == SOF_TOML_OK && sof_toml_value_count(d.root) == 1);
    CHECK(sof_toml_doc_parse(&d, NULL, 0) == SOF_TOML_OK && sof_toml_value_count(d.root) == 0);
    CHECK(sof_toml_doc_clear(&d) == SOF_TOML_OK && d.root == NULL);
    CHECK(sof_toml_doc_free(&d) == SOF_TOML_OK && d.ready == 0);
}

static void test_doc_tables(void) {
    // Tables defined by headers, dotted keys and both, in pieces and in any
    // order, as the specification's examples have them.
    CHECK(parses_as("a.b.c = 1\na.d = 2\n[x]\ny.z = 3\n", "{\"a\":{\"b\":{\"c\":{\"type\":\"integer\",\"value\":\"1\"}},\"d\":{\"type\":\"integer\",\"value\":\"2\"}},\"x\":{\"y\":{\"z\":{\"type\":\"integer\",\"value\":\"3\"}}}}"));
    CHECK(parses_as("[x.y.z.w]\na = 1\n[x]\nb = 2\n", "{\"x\":{\"y\":{\"z\":{\"w\":{\"a\":{\"type\":\"integer\",\"value\":\"1\"}}}},\"b\":{\"type\":\"integer\",\"value\":\"2\"}}}"));
    CHECK(parses_as("[fruit]\napple.color = \"red\"\n[fruit.apple.texture]\nsmooth = true\n", "{\"fruit\":{\"apple\":{\"color\":{\"type\":\"string\",\"value\":\"red\"},\"texture\":{\"smooth\":{\"type\":\"bool\",\"value\":\"true\"}}}}}"));
    CHECK(parses_as("[[a]]\nx = 1\n[a.b]\ny = 2\n[[a]]\nx = 3\n[[a.c]]\n", "{\"a\":[{\"x\":{\"type\":\"integer\",\"value\":\"1\"},\"b\":{\"y\":{\"type\":\"integer\",\"value\":\"2\"}}},{\"x\":{\"type\":\"integer\",\"value\":\"3\"},\"c\":[{}]}]}"));
    CHECK(parses_as("[ a . \"b.c\" . 'd' ]\n", "{\"a\":{\"b.c\":{\"d\":{}}}}"));
    CHECK(parses_as("t = { a.b = 1, a.c = 2, }\n", "{\"t\":{\"a\":{\"b\":{\"type\":\"integer\",\"value\":\"1\"},\"c\":{\"type\":\"integer\",\"value\":\"2\"}}}}"));
    CHECK(parses_as("t = {\n  a = 1, # a comment\n  b = [\n    2,\n  ],\n}\n", "{\"t\":{\"a\":{\"type\":\"integer\",\"value\":\"1\"},\"b\":[{\"type\":\"integer\",\"value\":\"2\"}]}}"));
    CHECK(parses_as("\"\" = 1\n'a' . b = 2\n", "{\"\":{\"type\":\"integer\",\"value\":\"1\"},\"a\":{\"b\":{\"type\":\"integer\",\"value\":\"2\"}}}"));
    CHECK(parses_as("1.2 = 3\n", "{\"1\":{\"2\":{\"type\":\"integer\",\"value\":\"3\"}}}"));
    // Each way of defining something twice, or adding to what is closed.
    const char *duplicates[] = {
        "a = 1\na = 2\n",          "[a]\n[a]\n",           "[a]\nb = 1\n[a.b]\n",      "a.b = 1\n[a.b]\n",
        "a = {}\n[a]\n",           "a = {}\na.b = 1\n",    "a = []\n[[a]]\n",          "[[a]]\n[a]\n",
        "[a.b.c]\n[a]\nb.c.d = 1\n", "a = {b = {}, b.c = 1}\n", "a = 1\na.b = 2\n",     "[a]\nb = 1\n[a.b.c]\n",
        "a = [{}]\n[a.b]\n",       "[[a.b]]\n[a]\nb.c = 1\n",
    };
    for (size_t k = 0; k < LENGTH(duplicates); k++) {
        sof_toml_doc_t d;
        sof_toml_doc_init(&d, NULL);
        sof_toml_error_t rc = sof_toml_doc_parse(&d, duplicates[k], strlen(duplicates[k]));
        sof_toml_doc_free(&d);
        if (rc != SOF_TOML_DUPLICATE) {
            printf("(\"%s\" gave %d) ", duplicates[k], (int)rc);
        }
        CHECK(rc == SOF_TOML_DUPLICATE);
    }
}

static void test_doc_strings(void) {
    CHECK(parses_as("s = \"tab\\tnl\\nq\\\"bs\\\\b\\bf\\fr\\re\\e\"\n", "{\"s\":{\"type\":\"string\",\"value\":\"tab\\tnl\\nq\\\"bs\\\\b\\bf\\fr\\re\\u001b\"}}"));
    CHECK(parses_as("s = \"\\x41\\xe9\\u00e9\\U0001F600\"\n", "{\"s\":{\"type\":\"string\",\"value\":\"A\\u00e9\\u00e9\\ud83d\\ude00\"}}"));
    CHECK(parses_as("s = 'C:\\path\\\"none\"'\n", "{\"s\":{\"type\":\"string\",\"value\":\"C:\\\\path\\\\\\\"none\\\"\"}}"));
    CHECK(parses_as("s = \"\"\"\nline 1\r\nline 2\"\"\"\n", "{\"s\":{\"type\":\"string\",\"value\":\"line 1\\nline 2\"}}"));
    CHECK(parses_as("s = \"\"\"a \\\n   \n   b\"\"\"\n", "{\"s\":{\"type\":\"string\",\"value\":\"a b\"}}"));
    CHECK(parses_as("s = \"\"\"\"\"quotes\"\"\"\"\"\n", "{\"s\":{\"type\":\"string\",\"value\":\"\\\"\\\"quotes\\\"\\\"\"}}"));
    CHECK(parses_as("s = '''\n  raw \\n ''x'' '''\n", "{\"s\":{\"type\":\"string\",\"value\":\"  raw \\\\n ''x'' \"}}"));
    CHECK(parses_as("s = ''''''\n", "{\"s\":{\"type\":\"string\",\"value\":\"\"}}"));
    // A NUL decoded from an escape is part of the string.
    sof_toml_doc_t d;
    size_t length;
    sof_toml_doc_init(&d, NULL);
    CHECK(sof_toml_doc_parse(&d, "\"k\\u0000\" = \"a\\u0000b\"", 22) == SOF_TOML_OK);
    const sof_toml_member_t *m = sof_toml_value_member(d.root, 0);
    CHECK(m != NULL && m->key_length == 2 && memcmp(m->key, "k\0", 3) == 0);
    CHECK(sof_toml_value_get_bytes(d.root, "k\0", 2) == m->value && sof_toml_value_get(d.root, "k") == NULL);
    CHECK(memcmp(sof_toml_value_string(m->value, &length), "a\0b", 4) == 0 && length == 3);
    sof_toml_doc_free(&d);
    // Strings that aren't valid.
    CHECK(error_is("s = \"a\\qb\"", SOF_TOML_SYNTAX, 1, 7));
    CHECK(error_is("s = \"\\uD800\"", SOF_TOML_SYNTAX, 1, 6));
    CHECK(error_is("s = \"\\x4\"", SOF_TOML_SYNTAX, 1, 6));
    CHECK(error_is("s = \"open\nt = 1", SOF_TOML_SYNTAX, 1, 5));
    CHECK(error_is("s = 'a\x01'", SOF_TOML_SYNTAX, 1, 7));
    CHECK(error_is("s = \"a\x7f\"", SOF_TOML_SYNTAX, 1, 7));
    CHECK(error_is("s = \"\"\"a\rb\"\"\"", SOF_TOML_SYNTAX, 1, 9));
    CHECK(error_is("s = \"\"\"abc", SOF_TOML_SYNTAX, 1, 5));
    CHECK(error_is("s = \"\"\"a\"\"\"\"\"\"", SOF_TOML_SYNTAX, 1, 14));
    CHECK(error_is("s = \"\xc3\x28\"", SOF_TOML_SYNTAX, 1, 6));
    CHECK(error_is("\"\"\"k\"\"\" = 1", SOF_TOML_SYNTAX, 1, 1));
}

static void test_doc_numbers(void) {
    CHECK(parses_as("a = +99\nb = -17\nc = 0\nd = +0\ne = -0\nf = 1_000_000\n", "{\"a\":{\"type\":\"integer\",\"value\":\"99\"},\"b\":{\"type\":\"integer\",\"value\":\"-17\"},\"c\":{\"type\":\"integer\",\"value\":\"0\"},\"d\":{\"type\":\"integer\",\"value\":\"0\"},\"e\":{\"type\":\"integer\",\"value\":\"0\"},\"f\":{\"type\":\"integer\",\"value\":\"1000000\"}}"));
    CHECK(parses_as("a = 0xDEAD_beef\nb = 0o755\nc = 0b1101_0110\nd = 0x7FFFFFFFFFFFFFFF\n", "{\"a\":{\"type\":\"integer\",\"value\":\"3735928559\"},\"b\":{\"type\":\"integer\",\"value\":\"493\"},\"c\":{\"type\":\"integer\",\"value\":\"214\"},\"d\":{\"type\":\"integer\",\"value\":\"9223372036854775807\"}}"));
    CHECK(parses_as("a = 9223372036854775807\nb = -9223372036854775808\n", "{\"a\":{\"type\":\"integer\",\"value\":\"9223372036854775807\"},\"b\":{\"type\":\"integer\",\"value\":\"-9223372036854775808\"}}"));
    CHECK(parses_as("a = +1.0\nb = 3.1415\nc = -0.01\nd = 5e+22\ne = 1e06\nf = -2E-2\ng = 6.626e-34\nh = 224_617.445_991\ni = 1e1_0\n", "{\"a\":{\"type\":\"float\",\"value\":\"1\"},\"b\":{\"type\":\"float\",\"value\":\"3.1415\"},\"c\":{\"type\":\"float\",\"value\":\"-0.01\"},\"d\":{\"type\":\"float\",\"value\":\"5e22\"},\"e\":{\"type\":\"float\",\"value\":\"1e6\"},\"f\":{\"type\":\"float\",\"value\":\"-0.02\"},\"g\":{\"type\":\"float\",\"value\":\"6.626e-34\"},\"h\":{\"type\":\"float\",\"value\":\"224617.445991\"},\"i\":{\"type\":\"float\",\"value\":\"1e10\"}}"));
    CHECK(parses_as("a = inf\nb = +inf\nc = -inf\nd = nan\ne = +nan\nf = -nan\ng = -0.0\n", "{\"a\":{\"type\":\"float\",\"value\":\"inf\"},\"b\":{\"type\":\"float\",\"value\":\"inf\"},\"c\":{\"type\":\"float\",\"value\":\"-inf\"},\"d\":{\"type\":\"float\",\"value\":\"nan\"},\"e\":{\"type\":\"float\",\"value\":\"nan\"},\"f\":{\"type\":\"float\",\"value\":\"nan\"},\"g\":{\"type\":\"float\",\"value\":\"-0.0\"}}"));
    // A float with more digits than fit on the stack.
    char text[400];
    size_t n = (size_t)snprintf(text, sizeof(text), "x = 0.");
    while (n < 300) {
        text[n++] = '3';
    }
    text[n] = '\0';
    CHECK(parses_as(text, "{\"x\":{\"type\":\"float\",\"value\":\"0.3333333333333333\"}}"));
    CHECK(error_is("a = 9223372036854775808", SOF_TOML_RANGE, 1, 5));
    CHECK(error_is("a = -9223372036854775809", SOF_TOML_RANGE, 1, 5));
    CHECK(error_is("a = 0x8000000000000000", SOF_TOML_RANGE, 1, 5));
    CHECK(error_is("a = 1e400", SOF_TOML_RANGE, 1, 5));
    const char *bad[] = {"a = 01",   "a = 1__0", "a = _1",  "a = 1_",  "a = 0X10", "a = +0x10", "a = 0x",  "a = 0b2",  "a = .5",
                         "a = 5.",   "a = 1e",   "a = 1.e5", "a = 1_.5", "a = 00.5", "a = Inf", "a = in",  "a = ++1", "a = 1e_5",
                         "a = 0o8",  "a = 1.2.3", "a = 0_1"};
    for (size_t k = 0; k < LENGTH(bad); k++) {
        CHECK(error_is(bad[k], SOF_TOML_SYNTAX, 1, 5));
    }
}

static void test_doc_datetimes(void) {
    CHECK(parses_as("a = 1979-05-27T07:32:00Z\nb = 1979-05-27 00:32:00.999999-07:00\nc = 1979-05-27t07:32:00z\nd = 1979-05-27T07:32:00\ne = 1979-05-27\nf = 07:32:00\ng = 00:32:00.5\nh = 2010-02-03 14:15\ni = 14:15\n", "{\"a\":{\"type\":\"datetime\",\"value\":\"1979-05-27T07:32:00Z\"},\"b\":{\"type\":\"datetime\",\"value\":\"1979-05-27T07:32:00.999999Z\"},\"c\":{\"type\":\"datetime\",\"value\":\"1979-05-27T07:32:00Z\"},\"d\":{\"type\":\"datetime-local\",\"value\":\"1979-05-27T07:32:00\"},\"e\":{\"type\":\"date-local\",\"value\":\"1979-05-27\"},\"f\":{\"type\":\"time-local\",\"value\":\"07:32:00\"},\"g\":{\"type\":\"time-local\",\"value\":\"00:32:00.5\"},\"h\":{\"type\":\"datetime-local\",\"value\":\"2010-02-03T14:15:00\"},\"i\":{\"type\":\"time-local\",\"value\":\"14:15:00\"}}"));
    // A date alone, then a comment; and a leap day.
    CHECK(parses_as("a = 2000-02-29 # leap\nb = [1979-05-27, 07:32:00]\n", "{\"a\":{\"type\":\"date-local\",\"value\":\"2000-02-29\"},\"b\":[{\"type\":\"date-local\",\"value\":\"1979-05-27\"},{\"type\":\"time-local\",\"value\":\"07:32:00\"}]}"));
    const char *bad[] = {"a = 1900-02-29", "a = 2021-04-31", "a = 2021-13-01", "a = 2021-00-01", "a = 2021-01-00", "a = 24:00:00",
                         "a = 23:60:00", "a = 23:59:61", "a = 1979-05-27T07:32:00+24:00", "a = 1979-05-27T07:32:00+05:60",
                         "a = 1979-05-27T07:32:00.", "a = 1979-5-27", "a = 1979-05-27T7:32:00", "a = 1979-05-27T"};
    for (size_t k = 0; k < LENGTH(bad); k++) {
        CHECK(error_is(bad[k], SOF_TOML_SYNTAX, 1, 5));
    }
    // A local time has no offset, and a local date no time after a space.
    CHECK(error_is("a = 07:32:00Z", SOF_TOML_SYNTAX, 1, 13));
    CHECK(error_is("a = 1979-05-27 7:32:00", SOF_TOML_SYNTAX, 1, 16));

    // The public parse and format.
    sof_toml_type_t type = SOF_TOML_NONE;
    sof_toml_datetime_t dt;
    char text[SOF_TOML_DATETIME_SIZE];
    CHECK(sof_toml_datetime_parse("1979-05-27 07:32:00.1234567891-05:30", 36, &type, &dt) == SOF_TOML_OK);
    CHECK(type == SOF_TOML_DATETIME && dt.nanosecond == 123456789 && dt.offset == -330);
    CHECK(sof_toml_datetime_format(type, &dt, text, sizeof(text)) == 35 && strcmp(text, "1979-05-27T07:32:00.123456789-05:30") == 0);
    CHECK(sof_toml_datetime_format(type, &dt, text, 35) == 0);
    CHECK(sof_toml_datetime_parse("07:32", 5, &type, &dt) == SOF_TOML_OK && type == SOF_TOML_TIME_LOCAL);
    CHECK(sof_toml_datetime_format(type, &dt, text, sizeof(text)) == 8 && strcmp(text, "07:32:00") == 0);
    CHECK(sof_toml_datetime_parse("1979-05-27 ", 11, &type, &dt) == SOF_TOML_SYNTAX);
    CHECK(sof_toml_datetime_parse("1979-05-27x", 11, &type, &dt) == SOF_TOML_SYNTAX);
    CHECK(sof_toml_datetime_parse(NULL, 0, &type, &dt) == SOF_TOML_ERROR);
    CHECK(sof_toml_datetime_parse("07:32", 5, NULL, &dt) == SOF_TOML_ERROR);
    CHECK(sof_toml_datetime_parse("07:32", 5, &type, NULL) == SOF_TOML_ERROR);
    sof_toml_datetime_t leap = {2024, 2, 29, 23, 59, 60, 500000000, 0};
    CHECK(sof_toml_datetime_format(SOF_TOML_DATETIME, &leap, text, sizeof(text)) > 0 && strcmp(text, "2024-02-29T23:59:60.5Z") == 0);
    CHECK(sof_toml_datetime_format(SOF_TOML_DATE_LOCAL, &leap, text, sizeof(text)) == 10 && strcmp(text, "2024-02-29") == 0);
    leap.day = 30;
    CHECK(sof_toml_datetime_format(SOF_TOML_DATE_LOCAL, &leap, text, sizeof(text)) == 0);
    CHECK(sof_toml_datetime_format(SOF_TOML_TIME_LOCAL, &leap, text, sizeof(text)) == 10 && strcmp(text, "23:59:60.5") == 0);
    CHECK(sof_toml_datetime_format(SOF_TOML_STRING, &leap, text, sizeof(text)) == 0);
    CHECK(sof_toml_datetime_format(SOF_TOML_DATE_LOCAL, NULL, text, sizeof(text)) == 0);
    CHECK(sof_toml_datetime_format(SOF_TOML_DATE_LOCAL, &dt, NULL, 0) == 0);
}

static void test_doc_errors(void) {
    static const error_row_t rows[] = {
        {"a = 1\nb = \n", SOF_TOML_SYNTAX, 2, 5},
        {"a = 1 b = 2", SOF_TOML_SYNTAX, 1, 7},
        {"a", SOF_TOML_SYNTAX, 1, 2},
        {"= 1", SOF_TOML_SYNTAX, 1, 1},
        {"a = 1\n\n[t\n", SOF_TOML_SYNTAX, 3, 3},
        {"[[t]\n", SOF_TOML_SYNTAX, 1, 4},
        {"[ [t]]\n", SOF_TOML_SYNTAX, 1, 3},
        {"[]\n", SOF_TOML_SYNTAX, 1, 2},
        {"a = [1 2]", SOF_TOML_SYNTAX, 1, 8},
        {"a = [1,,2]", SOF_TOML_SYNTAX, 1, 8},
        {"a = [1", SOF_TOML_SYNTAX, 1, 5},
        {"a = {b = 1 c = 2}", SOF_TOML_SYNTAX, 1, 12},
        {"a = {,}", SOF_TOML_SYNTAX, 1, 6},
        {"a = tru", SOF_TOML_SYNTAX, 1, 5},
        {"a = truex", SOF_TOML_SYNTAX, 1, 9},
        {"a = 1\r", SOF_TOML_SYNTAX, 1, 6},
        {"# bell \x07\n", SOF_TOML_SYNTAX, 1, 8},
        {"a = 1 # del \x7f", SOF_TOML_SYNTAX, 1, 13},
        {"a = \xff", SOF_TOML_SYNTAX, 1, 5},
        {"a.b = 1\na.b = 2\n", SOF_TOML_DUPLICATE, 2, 3},
        {"[t]\n[t]\n", SOF_TOML_DUPLICATE, 2, 2},
        {"a = 1\n\"a\" = 2\n", SOF_TOML_DUPLICATE, 2, 1},
        {"\xef\xbb\xbf a = 1\nb = ?", SOF_TOML_SYNTAX, 2, 5},
    };
    for (size_t k = 0; k < LENGTH(rows); k++) {
        CHECK(error_is(rows[k].text, rows[k].code, rows[k].line, rows[k].column));
    }
    // A byte order mark is skipped; whitespace and comments alone are an
    // empty table.
    CHECK(parses_as("\xef\xbb\xbf" "a = 1", "{\"a\":{\"type\":\"integer\",\"value\":\"1\"}}"));
    CHECK(parses_as(" \t\n# only a comment\r\n\n", "{}"));
    // A document that failed keeps nothing, and can parse again.
    sof_toml_doc_t d;
    sof_toml_doc_init(&d, NULL);
    CHECK(sof_toml_doc_parse(&d, "a = 1", 5) == SOF_TOML_OK);
    CHECK(sof_toml_doc_parse(&d, "a = ", 4) == SOF_TOML_SYNTAX && d.root == NULL && d.index == NULL && d.offset == 4);
    CHECK(sof_toml_doc_parse(&d, "a = 1", 5) == SOF_TOML_OK && d.message == NULL && d.line == 0 && d.column == 0 && d.offset == 0);
    sof_toml_doc_free(&d);
}

static void test_doc_depth(void) {
    // Arrays and inline tables nest SOF_TOML_DEPTH_MAX deep, and no
    // deeper.
    char text[4 * SOF_TOML_DEPTH_MAX + 32];
    for (int deeper = 0; deeper < 2; deeper++) {
        size_t n = 0, levels = SOF_TOML_DEPTH_MAX + (size_t)deeper;
        n += (size_t)snprintf(text, sizeof(text), "a = ");
        for (size_t k = 0; k < levels; k++) {
            text[n++] = k % 2 == 0 ? '[' : '{';
            if (k % 2 == 1) {
                text[n++] = 'b';
                text[n++] = '=';
            }
        }
        // The innermost inline table needs its member's value.
        if (levels % 2 == 0) {
            text[n++] = '1';
        }
        for (size_t k = levels; k > 0; k--) {
            text[n++] = (k - 1) % 2 == 0 ? ']' : '}';
        }
        text[n] = '\0';
        sof_toml_doc_t d;
        sof_toml_doc_init(&d, NULL);
        sof_toml_error_t rc = sof_toml_doc_parse(&d, text, n);
        sof_toml_doc_free(&d);
        CHECK(rc == (deeper ? SOF_TOML_TOO_DEEP : SOF_TOML_OK));
    }
    // Dotted keys and headers go as deep as they like.
    sof_toml_doc_t d;
    sof_string_t deep;
    sof_string_init(&deep, 0, NULL);
    sof_string_append_cstr(&deep, "[a");
    for (int k = 0; k < 5000; k++) {
        sof_string_append_cstr(&deep, ".a");
    }
    sof_string_append_cstr(&deep, "]\nx = 1\n");
    sof_toml_doc_init(&d, NULL);
    CHECK(sof_toml_doc_parse(&d, deep.data, deep.length) == SOF_TOML_OK);
    sof_toml_doc_free(&d);
    sof_string_free(&deep);
}

static void test_doc_index(void) {
    // Tables with more than SOF_TOML_INDEX_MIN members are indexed, many
    // of them in one document, and a key defined twice is found in the
    // index too.
    sof_string_t text;
    sof_toml_doc_t d;
    char line[64];
    sof_string_init(&text, 0, NULL);
    for (int t = 0; t < 20; t++) {
        snprintf(line, sizeof(line), "[t%d]\n", t);
        sof_string_append_cstr(&text, line);
        for (int k = 0; k < 3 * t; k++) {
            snprintf(line, sizeof(line), "\"key %d\" = %d\n", k, t * 1000 + k);
            sof_string_append_cstr(&text, line);
        }
    }
    sof_toml_doc_init(&d, NULL);
    CHECK(sof_toml_doc_parse(&d, text.data, text.length) == SOF_TOML_OK);
    CHECK(d.index != NULL && sof_toml_value_count(d.root) == 20);
    for (int t = 0; t < 20; t++) {
        snprintf(line, sizeof(line), "t%d", t);
        const sof_toml_value_t *table = sof_toml_value_get(d.root, line);
        CHECK(sof_toml_value_count(table) == (size_t)(3 * t));
        CHECK((table->as.table.index != NULL) == (3 * t > SOF_TOML_INDEX_MIN));
        for (int k = 0; k < 3 * t; k++) {
            int64_t i;
            snprintf(line, sizeof(line), "key %d", k);
            CHECK(sof_toml_value_int(sof_toml_value_get(table, line), &i) == SOF_TOML_OK && i == t * 1000 + k);
        }
        CHECK(sof_toml_value_get(table, "key 999") == NULL && sof_toml_value_get(table, "") == NULL);
    }
    sof_string_append_cstr(&text, "\"key 30\" = 1\n");
    CHECK(sof_toml_doc_parse(&d, text.data, text.length) == SOF_TOML_DUPLICATE && d.root == NULL && d.index == NULL);
    sof_toml_doc_free(&d);
    sof_string_free(&text);
}

static void test_doc_path(void) {
    static const char text[] = "a.b = 1\n"
                               "\"x.y\" = 2\n"
                               "'q\"' = 3\n"
                               "\"\xc3\xa9\" = 4\n"
                               "list = [[10, 11], [12]]\n"
                               "[[things]]\n"
                               "name = \"first\"\n"
                               "[[things]]\n"
                               "name = \"second\"\n"
                               "tags = { \"\" = 5 }\n";
    sof_toml_doc_t d;
    int64_t i;
    sof_toml_doc_init(&d, NULL);
    CHECK(sof_toml_doc_parse(&d, text, strlen(text)) == SOF_TOML_OK);
    const sof_toml_value_t *root = d.root;
    CHECK(sof_toml_value_path(root, "") == root && sof_toml_value_path(root, "  ") == root);
    CHECK(sof_toml_value_int(sof_toml_value_path(root, "a.b"), &i) == SOF_TOML_OK && i == 1);
    CHECK(sof_toml_value_int(sof_toml_value_path(root, " a . b "), &i) == SOF_TOML_OK && i == 1);
    CHECK(sof_toml_value_int(sof_toml_value_path(root, "\"x.y\""), &i) == SOF_TOML_OK && i == 2);
    CHECK(sof_toml_value_int(sof_toml_value_path(root, "'x.y'"), &i) == SOF_TOML_OK && i == 2);
    CHECK(sof_toml_value_int(sof_toml_value_path(root, "'q\"'"), &i) == SOF_TOML_OK && i == 3);
    CHECK(sof_toml_value_int(sof_toml_value_path(root, "\"q\\\"\""), &i) == SOF_TOML_OK && i == 3);
    CHECK(sof_toml_value_int(sof_toml_value_path(root, "\"\\u00E9\""), &i) == SOF_TOML_OK && i == 4);
    CHECK(sof_toml_value_int(sof_toml_value_path(root, "\"\\xe9\""), &i) == SOF_TOML_OK && i == 4);
    CHECK(sof_toml_value_int(sof_toml_value_path(root, "list[0][1]"), &i) == SOF_TOML_OK && i == 11);
    CHECK(sof_toml_value_int(sof_toml_value_path(root, "list [1] [0]"), &i) == SOF_TOML_OK && i == 12);
    CHECK(strcmp(sof_toml_value_string(sof_toml_value_path(root, "things[1].name"), NULL), "second") == 0);
    CHECK(sof_toml_value_int(sof_toml_value_path(root, "things[1].tags.\"\""), &i) == SOF_TOML_OK && i == 5);
    CHECK(sof_toml_value_path(sof_toml_value_path(root, "things"), "[0].name") != NULL);
    const char *none[] = {"x.y", "a.c", "a.b.c", "list[2]", "list[]", "list[x]", "list[0", "a..b", "a.", ".a", "a b", "\"x.y",
                          "'x.y", "\"\\q\"", "things.name", "a[0]", "list.x"};
    for (size_t k = 0; k < LENGTH(none); k++) {
        if (sof_toml_value_path(root, none[k]) != NULL) {
            printf("(%s found something) ", none[k]);
        }
        CHECK(sof_toml_value_path(root, none[k]) == NULL);
    }
    CHECK(sof_toml_value_path(NULL, "a") == NULL && sof_toml_value_path(root, NULL) == NULL);
    sof_toml_doc_free(&d);
}

static void test_doc_spans(void) {
    static const char text[] = "name = \"vika\"  # the name\n"
                               "version = '1.0'\n"
                               "\n"
                               "[dependencies]\n"
                               "sof = { path = \"../sof\", version = \"0.1\" }\n"
                               "\"quoted key\" = [ 1, 2,\n  3 ]\n"
                               "dotted.part = 1979-05-27\n"
                               "# trailing comment\n"
                               "\n"
                               "[empty]\n"
                               "[[bin]]\n"
                               "name = \"a\"\n"
                               "[bin.sub]\n"
                               "x = 1\n"
                               "[[bin]]\n"
                               "name = \"b\" # last\n"
                               "[a.b]\n"
                               "c = true\n";
    sof_toml_doc_t d;
    CHECK(sof_toml_doc_init(&d, NULL) == SOF_TOML_OK);
    CHECK(sof_toml_doc_parse(&d, text, strlen(text)) == SOF_TOML_OK);
    const sof_toml_value_t *root = d.root;
    // The root runs from the start to its last value; scalars and their keys
    // are their text, quotes included.
    CHECK(span_is(text, root->span, "name = \"vika\"  # the name\nversion = '1.0'"));
    CHECK(span_is(text, sof_toml_value_member(root, 0)->key_span, "name"));
    CHECK(span_is(text, sof_toml_value_get(root, "name")->span, "\"vika\""));
    CHECK(span_is(text, sof_toml_value_get(root, "version")->span, "'1.0'"));
    // A table with a header runs from it to its last value, leaving out the
    // comment and blank line after.
    const sof_toml_value_t *deps = sof_toml_value_get(root, "dependencies");
    CHECK(span_is(text, deps->span,
                  "[dependencies]\nsof = { path = \"../sof\", version = \"0.1\" }\n\"quoted key\" = [ 1, 2,\n  3 ]\n"
                  "dotted.part = 1979-05-27"));
    CHECK(span_is(text, sof_toml_value_member(root, 2)->key_span, "dependencies"));
    const sof_toml_member_t *m = sof_toml_value_member(deps, 0);
    CHECK(span_is(text, m->key_span, "sof"));
    CHECK(span_is(text, m->value->span, "{ path = \"../sof\", version = \"0.1\" }"));
    CHECK(span_is(text, sof_toml_value_member(m->value, 0)->key_span, "path"));
    CHECK(span_is(text, sof_toml_value_get(m->value, "path")->span, "\"../sof\""));
    m = sof_toml_value_member(deps, 1);
    CHECK(span_is(text, m->key_span, "\"quoted key\""));
    CHECK(span_is(text, m->value->span, "[ 1, 2,\n  3 ]"));
    CHECK(span_is(text, sof_toml_value_at(m->value, 2)->span, "3"));
    // A dotted key's table has no text of its own: length 0, at its key.
    m = sof_toml_value_member(deps, 2);
    CHECK(span_is(text, m->key_span, "dotted"));
    CHECK(m->value->span.length == 0 && m->value->span.offset == m->key_span.offset);
    CHECK(span_is(text, sof_toml_value_member(m->value, 0)->key_span, "part"));
    CHECK(span_is(text, sof_toml_value_get(m->value, "part")->span, "1979-05-27"));
    // A line number from the offset.
    size_t line = 0, column = 0;
    sof_string_t whole = sof_string_view(text, strlen(text));
    CHECK(sof_string_position(&whole, sof_toml_value_member(deps, 0)->key_span.offset, &line, &column) == SOF_STRING_OK);
    CHECK(line == 5 && column == 1);
    CHECK(sof_string_position(&whole, sof_toml_value_path(root, "dependencies.sof.version")->span.offset, &line, &column) == SOF_STRING_OK);
    CHECK(line == 5 && column == 36);
    // A header with nothing under it is the header.
    CHECK(span_is(text, sof_toml_value_get(root, "empty")->span, "[empty]"));
    // An array of tables runs from its first header to the end of its last
    // element, each element from its header to its last value, and a
    // sub-table from its own header.
    const sof_toml_value_t *bin = sof_toml_value_get(root, "bin");
    CHECK(span_is(text, bin->span, "[[bin]]\nname = \"a\"\n[bin.sub]\nx = 1\n[[bin]]\nname = \"b\""));
    CHECK(span_is(text, sof_toml_value_at(bin, 0)->span, "[[bin]]\nname = \"a\""));
    CHECK(span_is(text, sof_toml_value_at(bin, 1)->span, "[[bin]]\nname = \"b\""));
    CHECK(span_is(text, sof_toml_value_path(root, "bin[0].sub")->span, "[bin.sub]\nx = 1"));
    CHECK(span_is(text, sof_toml_value_member(sof_toml_value_at(bin, 0), 1)->key_span, "sub"));
    // A table a header goes through has length 0 at that part of the header.
    m = sof_toml_value_member(root, 5);
    CHECK(span_is(text, m->key_span, "a"));
    CHECK(m->value->span.length == 0 && m->value->span.offset == m->key_span.offset);
    CHECK(span_is(text, sof_toml_value_get(m->value, "b")->span, "[a.b]\nc = true"));
    CHECK(span_is(text, sof_toml_value_member(m->value, 0)->key_span, "b"));

    // Splicing at the spans: add a dependency after the last, and remove
    // the first one's line, inserting first, since it is further on.
    sof_string_t copy;
    CHECK(sof_string_from_cstr(&copy, text, NULL) == SOF_STRING_OK);
    m = sof_toml_value_member(deps, 0);
    size_t from = m->key_span.offset, to = m->value->span.offset + m->value->span.length + 1;
    CHECK(sof_string_insert_cstr(&copy, deps->span.offset + deps->span.length, "\nnew = \"1\"") == SOF_STRING_OK);
    CHECK(sof_string_delete(&copy, from, to - from) == SOF_STRING_OK);
    const char *edited = copy.data;
    CHECK(strstr(edited, "[dependencies]\n\"quoted key\"") != NULL);
    CHECK(strstr(edited, "dotted.part = 1979-05-27\nnew = \"1\"\n# trailing comment\n") != NULL);
    CHECK(strstr(edited, "# the name") != NULL && strstr(edited, "sof =") == NULL);
    CHECK(sof_toml_doc_parse(&d, copy.data, copy.length) == SOF_TOML_OK);
    deps = sof_toml_value_get(d.root, "dependencies");
    CHECK(sof_toml_value_count(deps) == 3 && sof_toml_value_get(deps, "sof") == NULL);
    CHECK(span_is(edited, sof_toml_value_member(deps, 2)->key_span, "new"));
    CHECK(span_is(edited, deps->span, "[dependencies]\n\"quoted key\" = [ 1, 2,\n  3 ]\ndotted.part = 1979-05-27\nnew = \"1\""));
    sof_string_free(&copy);

    // A table named on the way to another takes its own header's span once
    // it has one, keeping its key's where it was first named.
    static const char later[] = "[x.y]\n[x]\nk = 1\n";
    CHECK(sof_toml_doc_parse(&d, later, strlen(later)) == SOF_TOML_OK);
    m = sof_toml_value_member(d.root, 0);
    CHECK(span_is(later, m->value->span, "[x]\nk = 1") && m->key_span.offset == 1 && m->key_span.length == 1);

    // Offsets count a byte order mark, and a CRLF line's carriage return
    // isn't part of what is on it.
    static const char crlf[] = "\xef\xbb\xbf" "a = 1\r\n[t]\r\nb = 2\r\n";
    CHECK(sof_toml_doc_parse(&d, crlf, strlen(crlf)) == SOF_TOML_OK);
    CHECK(span_is(crlf, d.root->span, "\xef\xbb\xbf" "a = 1"));
    CHECK(span_is(crlf, sof_toml_value_member(d.root, 0)->key_span, "a") && sof_toml_value_member(d.root, 0)->key_span.offset == 3);
    CHECK(span_is(crlf, sof_toml_value_get(d.root, "t")->span, "[t]\r\nb = 2"));
    whole = sof_string_view(crlf, strlen(crlf));
    CHECK(sof_string_position(&whole, sof_toml_value_path(d.root, "t.b")->span.offset, &line, &column) == SOF_STRING_OK);
    CHECK(line == 3 && column == 5);

    // An empty text's root is empty, at the start.
    CHECK(sof_toml_doc_parse(&d, "", 0) == SOF_TOML_OK);
    CHECK(d.root->span.offset == 0 && d.root->span.length == 0);
    sof_toml_doc_free(&d);
}

static void test_doc_read(void) {
    // Reading through a read function, a few bytes at a time, gives what
    // parsing does; a read function that fails gives SOF_TOML_READ.
    size_t sizes[] = {1, 3, 100, 100000};
    for (size_t k = 0; k < LENGTH(sizes); k++) {
        chunks_t chunks = {sample_text, strlen(sample_text), 0, sizes[k], 0, 0};
        sof_toml_doc_t d;
        sof_toml_writer_t w;
        sof_toml_doc_init(&d, NULL);
        sof_toml_writer_init(&w, 0, NULL, NULL, NULL);
        CHECK(sof_toml_doc_read(&d, chunks_read, &chunks) == SOF_TOML_OK);
        CHECK(sof_toml_writer_value(&w, d.root) == SOF_TOML_OK && output_is(&w, sample_written));
        sof_toml_writer_free(&w);
        sof_toml_doc_free(&d);
    }
    for (size_t fail_at = 1; fail_at < 5; fail_at++) {
        chunks_t chunks = {sample_text, strlen(sample_text), 0, 64, fail_at, 0};
        sof_toml_doc_t d;
        sof_toml_doc_init(&d, NULL);
        CHECK(sof_toml_doc_read(&d, chunks_read, &chunks) == SOF_TOML_READ && d.root == NULL);
        sof_toml_doc_free(&d);
    }
    // Errors are found as when parsing.
    chunks_t chunks = {"a = 1\nb = ", 10, 0, 2, 0, 0};
    sof_toml_doc_t d;
    sof_toml_doc_init(&d, NULL);
    CHECK(sof_toml_doc_read(&d, chunks_read, &chunks) == SOF_TOML_SYNTAX && d.line == 2 && d.column == 5);
    CHECK(sof_toml_doc_read(&d, NULL, NULL) == SOF_TOML_ERROR);
    sof_toml_doc_free(&d);
    // And from a file.
    FILE *f = tmpfile();
    CHECK(f != NULL);
    fputs(sample_text, f);
    rewind(f);
    sof_toml_doc_init(&d, NULL);
    CHECK(sof_toml_doc_read(&d, sof_toml_read_file, f) == SOF_TOML_OK && sof_toml_value_count(d.root) == 9);
    sof_toml_doc_free(&d);
    fclose(f);
    char buffer[4];
    size_t length;
    CHECK(sof_toml_read_file(buffer, sizeof(buffer), &length, NULL) == 1);
}

static void test_doc_null(void) {
    sof_toml_doc_t d;
    int b;
    int64_t i;
    double x;
    size_t length = 99;
    sof_toml_datetime_t dt;
    sof_alloc_t bad = {test_alloc_malloc, NULL, NULL, NULL, NULL};
    CHECK(sof_toml_doc_init(NULL, NULL) == SOF_TOML_ERROR);
    CHECK(sof_toml_doc_init(&d, &bad) == SOF_TOML_ERROR && d.ready == 0);
    CHECK(sof_toml_doc_parse(&d, "a = 1", 5) == SOF_TOML_ERROR);
    CHECK(sof_toml_doc_parse(NULL, "a = 1", 5) == SOF_TOML_ERROR);
    CHECK(sof_toml_doc_read(NULL, chunks_read, NULL) == SOF_TOML_ERROR);
    CHECK(sof_toml_doc_clear(NULL) == SOF_TOML_ERROR && sof_toml_doc_clear(&d) == SOF_TOML_ERROR);
    CHECK(sof_toml_doc_free(NULL) == SOF_TOML_ERROR && sof_toml_doc_free(&d) == SOF_TOML_OK);
    CHECK(sof_toml_doc_init(&d, NULL) == SOF_TOML_OK);
    CHECK(sof_toml_doc_parse(&d, NULL, 1) == SOF_TOML_ERROR);
    CHECK(sof_toml_doc_free(&d) == SOF_TOML_OK && sof_toml_doc_free(&d) == SOF_TOML_OK);
    CHECK(sof_toml_doc_parse(&d, "a = 1", 5) == SOF_TOML_ERROR && d.root == NULL);
    // The value functions take NULL, so lookups can be chained.
    CHECK(sof_toml_value_type(NULL) == SOF_TOML_NONE && sof_toml_value_count(NULL) == 0);
    CHECK(sof_toml_value_at(NULL, 0) == NULL && sof_toml_value_member(NULL, 0) == NULL);
    CHECK(sof_toml_value_get(NULL, "a") == NULL && sof_toml_value_get_bytes(NULL, "a", 1) == NULL);
    CHECK(sof_toml_value_string(NULL, &length) == NULL && length == 0 && sof_toml_value_string(NULL, NULL) == NULL);
    CHECK(sof_toml_value_bool(NULL, &b) == SOF_TOML_NO_MATCH && sof_toml_value_int(NULL, &i) == SOF_TOML_NO_MATCH);
    CHECK(sof_toml_value_double(NULL, &x) == SOF_TOML_NO_MATCH && sof_toml_value_datetime(NULL, &dt) == SOF_TOML_NO_MATCH);
    CHECK(sof_toml_value_bool(NULL, NULL) == SOF_TOML_ERROR && sof_toml_value_int(NULL, NULL) == SOF_TOML_ERROR);
    CHECK(sof_toml_value_double(NULL, NULL) == SOF_TOML_ERROR && sof_toml_value_datetime(NULL, NULL) == SOF_TOML_ERROR);
    CHECK(sof_toml_doc_init(&d, NULL) == SOF_TOML_OK && sof_toml_doc_parse(&d, "a = 1", 5) == SOF_TOML_OK);
    CHECK(sof_toml_value_get(d.root, NULL) == NULL && sof_toml_value_get_bytes(d.root, NULL, 1) == NULL);
    CHECK(sof_toml_value_get_bytes(d.root, NULL, 0) == NULL);
    sof_toml_doc_free(&d);
}

static void test_doc_no_memory(void) {
    // Fail each allocation in turn until a parse makes none fail. A parse
    // that fails must return SOF_TOML_NO_MEM and leave the document empty,
    // and parsing again must give the whole document. Nothing may leak, and
    // every size must be right.
    sof_string_t text;
    char line[64];
    sof_string_init(&text, 0, NULL);
    sof_string_append_cstr(&text, sample_text);
    sof_string_append_cstr(&text, "[big]\n");
    for (int k = 0; k < 40; k++) {
        snprintf(line, sizeof(line), "k%d = \"value %d\"\n", k, k);
        sof_string_append_cstr(&text, line);
    }
    sof_string_append_cstr(&text, "long = \"");
    for (int k = 0; k < 3000; k++) {
        sof_string_append_cstr(&text, "ab\\n");
    }
    sof_string_append_cstr(&text, "\"\nfloat = 0.");
    for (int k = 0; k < 200; k++) {
        sof_string_append_cstr(&text, "1");
    }
    sof_string_append_cstr(&text, "\n");
    sof_toml_writer_t expected, again;
    sof_toml_doc_t d;
    sof_toml_doc_init(&d, NULL);
    sof_toml_writer_init(&expected, 0, NULL, NULL, NULL);
    sof_toml_writer_init(&again, 0, NULL, NULL, NULL);
    CHECK(sof_toml_doc_parse(&d, text.data, text.length) == SOF_TOML_OK);
    CHECK(sof_toml_writer_value(&expected, d.root) == SOF_TOML_OK);
    sof_toml_doc_free(&d);
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        int failed = 0;
        CHECK(sof_toml_doc_init(&d, &alloc) == SOF_TOML_OK);
        sof_toml_error_t rc = sof_toml_doc_parse(&d, text.data, text.length);
        if (rc == SOF_TOML_NO_MEM) {
            failed = 1;
            CHECK(d.root == NULL && d.index == NULL && d.message == NULL);
            rc = sof_toml_doc_parse(&d, text.data, text.length);
        }
        CHECK(rc == SOF_TOML_OK);
        sof_toml_writer_clear(&again);
        CHECK(sof_toml_writer_value(&again, d.root) == SOF_TOML_OK);
        CHECK(again.out.length == expected.out.length && memcmp(again.out.data, expected.out.data, again.out.length) == 0);
        CHECK(failed == (state.allocations >= n));
        sof_toml_doc_free(&d);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            break;
        }
    }
    sof_toml_writer_free(&expected);
    sof_toml_writer_free(&again);
    sof_string_free(&text);
}

static void test_writer_sections(void) {
    sof_toml_writer_t w;
    sof_toml_datetime_t dob = {1979, 5, 27, 7, 32, 0, 0, -480};
    CHECK(sof_toml_writer_init(&w, 0, NULL, NULL, NULL) == SOF_TOML_OK);
    CHECK(sof_toml_writer_complete(&w) && sof_toml_writer_depth(&w) == 0);
    CHECK(sof_toml_writer_key(&w, "title") == SOF_TOML_OK && !sof_toml_writer_complete(&w));
    CHECK(sof_toml_writer_string(&w, "TOML") == SOF_TOML_OK && sof_toml_writer_complete(&w));
    CHECK(sof_toml_writer_key(&w, "owner") == SOF_TOML_OK && sof_toml_writer_table_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_depth(&w) == 1 && !sof_toml_writer_complete(&w));
    CHECK(sof_toml_writer_key(&w, "name") == SOF_TOML_OK && sof_toml_writer_string(&w, "Tom") == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "dob") == SOF_TOML_OK && sof_toml_writer_datetime(&w, SOF_TOML_DATETIME, &dob) == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "database") == SOF_TOML_OK && sof_toml_writer_table_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "ports") == SOF_TOML_OK && sof_toml_writer_array_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_int(&w, 8000) == SOF_TOML_OK && sof_toml_writer_int(&w, 8001) == SOF_TOML_OK);
    CHECK(sof_toml_writer_array_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "data") == SOF_TOML_OK && sof_toml_writer_array_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_array_begin(&w) == SOF_TOML_OK && sof_toml_writer_string(&w, "delta") == SOF_TOML_OK);
    CHECK(sof_toml_writer_array_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_begin(&w) == SOF_TOML_OK && sof_toml_writer_key(&w, "x") == SOF_TOML_OK);
    CHECK(sof_toml_writer_double(&w, 3.14) == SOF_TOML_OK && sof_toml_writer_table_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_array_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "temp") == SOF_TOML_OK && sof_toml_writer_inline_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "cpu") == SOF_TOML_OK && sof_toml_writer_int(&w, 79) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "on") == SOF_TOML_OK && sof_toml_writer_bool(&w, 1) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "e") == SOF_TOML_OK && sof_toml_writer_table_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_end(&w) == SOF_TOML_OK && sof_toml_writer_table_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_end(&w) == SOF_TOML_OK);
    // Tables in tables, the outer with nothing but the inner, and a table
    // with nothing in it.
    CHECK(sof_toml_writer_key(&w, "a") == SOF_TOML_OK && sof_toml_writer_table_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "b c") == SOF_TOML_OK && sof_toml_writer_table_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "x") == SOF_TOML_OK && sof_toml_writer_int(&w, 1) == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "empty") == SOF_TOML_OK && sof_toml_writer_table_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_depth(&w) == 2 && sof_toml_writer_table_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_end(&w) == SOF_TOML_OK);
    // An array of tables, one of its tables empty, one with a table in it.
    CHECK(sof_toml_writer_key(&w, "products") == SOF_TOML_OK && sof_toml_writer_table_array_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_begin(&w) == SOF_TOML_OK && sof_toml_writer_key(&w, "name") == SOF_TOML_OK);
    CHECK(sof_toml_writer_string(&w, "Hammer") == SOF_TOML_OK && sof_toml_writer_table_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_begin(&w) == SOF_TOML_OK && sof_toml_writer_table_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_begin(&w) == SOF_TOML_OK && sof_toml_writer_key(&w, "size") == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_begin(&w) == SOF_TOML_OK && sof_toml_writer_key(&w, "mm") == SOF_TOML_OK);
    CHECK(sof_toml_writer_int(&w, 30) == SOF_TOML_OK && sof_toml_writer_table_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_end(&w) == SOF_TOML_OK && sof_toml_writer_array_end(&w) == SOF_TOML_OK);
    // An array of tables with none is written as an empty array, if it can
    // be.
    CHECK(sof_toml_writer_key(&w, "none") == SOF_TOML_OK && sof_toml_writer_table_array_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_array_end(&w) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_table_begin(&w) == SOF_TOML_OK && sof_toml_writer_key(&w, "z") == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_array_begin(&w) == SOF_TOML_OK && sof_toml_writer_array_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_end(&w) == SOF_TOML_OK && sof_toml_writer_array_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_complete(&w));
    CHECK(output_is(&w, "title = \"TOML\"\n"
                        "\n[owner]\n"
                        "name = \"Tom\"\n"
                        "dob = 1979-05-27T07:32:00-08:00\n"
                        "\n[database]\n"
                        "ports = [8000, 8001]\n"
                        "data = [[\"delta\"], {x = 3.14}]\n"
                        "temp = {cpu = 79, on = true, e = {}}\n"
                        "\n[a.\"b c\"]\n"
                        "x = 1\n"
                        "\n[a.empty]\n"
                        "\n[[products]]\n"
                        "name = \"Hammer\"\n"
                        "\n[[products]]\n"
                        "\n[[products]]\n"
                        "\n[products.size]\n"
                        "mm = 30\n"
                        "\n[[none]]\n"
                        "z = []\n"));
    // What it wrote reads back.
    sof_toml_doc_t d;
    sof_toml_doc_init(&d, NULL);
    CHECK(sof_toml_doc_parse(&d, w.out.data, w.out.length) == SOF_TOML_OK);
    CHECK(sof_toml_value_path(d.root, "products[2].size.mm") != NULL && sof_toml_value_path(d.root, "a.empty") != NULL);
    sof_toml_doc_free(&d);
    sof_toml_writer_free(&w);
}

static void test_writer_strings(void) {
    sof_toml_writer_t w;
    CHECK(sof_toml_writer_init(&w, 0, NULL, NULL, NULL) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "s") == SOF_TOML_OK);
    CHECK(sof_toml_writer_string(&w, "q\" b\\ \b\t\n\f\r \x01\x1b\x7f caf\xc3\xa9 \xf0\x9f\x98\x80") == SOF_TOML_OK);
    CHECK(sof_toml_writer_key_bytes(&w, "n\0l", 3) == SOF_TOML_OK && sof_toml_writer_string_bytes(&w, "a\0b", 3) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "bad") == SOF_TOML_OK && sof_toml_writer_string(&w, "\xc3\x28 \xff \xe2\x82") == SOF_TOML_OK);
    CHECK(sof_toml_writer_key_bytes(&w, NULL, 0) == SOF_TOML_OK && sof_toml_writer_string_bytes(&w, NULL, 0) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "A-z_09") == SOF_TOML_OK && sof_toml_writer_string(&w, "") == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "a.b") == SOF_TOML_OK && sof_toml_writer_int(&w, 1) == SOF_TOML_OK);
    CHECK(output_is(&w, "s = \"q\\\" b\\\\ \\b\\t\\n\\f\\r \\u0001\\u001B\\u007F caf\xc3\xa9 \xf0\x9f\x98\x80\"\n"
                        "\"n\\u0000l\" = \"a\\u0000b\"\n"
                        "bad = \"\xef\xbf\xbd( \xef\xbf\xbd \xef\xbf\xbd\"\n"
                        "\"\" = \"\"\n"
                        "A-z_09 = \"\"\n"
                        "\"a.b\" = 1\n"));
    sof_toml_writer_free(&w);
    // With SOF_TOML_ASCII, everything outside ASCII is escaped, in keys
    // too, with \U past U+FFFF.
    CHECK(sof_toml_writer_init(&w, SOF_TOML_ASCII, NULL, NULL, NULL) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "caf\xc3\xa9") == SOF_TOML_OK);
    CHECK(sof_toml_writer_string(&w, "\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\xff") == SOF_TOML_OK);
    CHECK(output_is(&w, "\"caf\\u00E9\" = \"\\u00E9\\u20AC\\U0001F600\\uFFFD\"\n"));
    sof_toml_writer_free(&w);
}

static void test_writer_numbers(void) {
    static const struct {
        double value;
        const char *text;
    } doubles[] = {
        {1.0, "1.0"},       {0.1, "0.1"},         {-0.0, "-0.0"},   {100.0, "100.0"},     {1e+100, "1e+100"},
        {5e-324, "5e-324"}, {1e16, "10000000000000000.0"},         {1e17, "1e+17"},      {0.3333333333333333, "0.3333333333333333"},
        {-2.5, "-2.5"},
    };
    sof_toml_writer_t w;
    CHECK(sof_toml_writer_init(&w, 0, NULL, NULL, NULL) == SOF_TOML_OK);
    for (size_t k = 0; k < LENGTH(doubles); k++) {
        char expected[64];
        sof_toml_writer_clear(&w);
        snprintf(expected, sizeof(expected), "x = %s\n", doubles[k].text);
        CHECK(sof_toml_writer_key(&w, "x") == SOF_TOML_OK && sof_toml_writer_double(&w, doubles[k].value) == SOF_TOML_OK);
        CHECK(output_is(&w, expected));
    }
    sof_toml_writer_clear(&w);
    CHECK(sof_toml_writer_key(&w, "a") == SOF_TOML_OK && sof_toml_writer_array_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_double(&w, HUGE_VAL) == SOF_TOML_OK && sof_toml_writer_double(&w, -HUGE_VAL) == SOF_TOML_OK);
    CHECK(sof_toml_writer_double(&w, NAN) == SOF_TOML_OK && sof_toml_writer_int(&w, INT64_MIN) == SOF_TOML_OK);
    CHECK(sof_toml_writer_int(&w, INT64_MAX) == SOF_TOML_OK && sof_toml_writer_int(&w, 0) == SOF_TOML_OK);
    CHECK(sof_toml_writer_bool(&w, 0) == SOF_TOML_OK && sof_toml_writer_array_end(&w) == SOF_TOML_OK);
    CHECK(output_is(&w, "a = [inf, -inf, nan, -9223372036854775808, 9223372036854775807, 0, false]\n"));
    // 2,000 random doubles read back exactly, as floats.
    uint64_t state = 0x1234567;
    sof_toml_doc_t d;
    sof_toml_doc_init(&d, NULL);
    for (int k = 0; k < 2000; k++) {
        double value, back;
        uint64_t bits = next_random(&state);
        memcpy(&value, &bits, sizeof(value));
        if (isnan(value)) {
            continue;
        }
        sof_toml_writer_clear(&w);
        CHECK(sof_toml_writer_key(&w, "x") == SOF_TOML_OK && sof_toml_writer_double(&w, value) == SOF_TOML_OK);
        CHECK(sof_toml_doc_parse(&d, w.out.data, w.out.length) == SOF_TOML_OK);
        const sof_toml_value_t *x = sof_toml_value_get(d.root, "x");
        CHECK(sof_toml_value_type(x) == SOF_TOML_FLOAT && sof_toml_value_double(x, &back) == SOF_TOML_OK);
        CHECK(back == value && signbit(back) == signbit(value));
    }
    sof_toml_doc_free(&d);
    // Dates and times out of range are refused.
    sof_toml_datetime_t bad = {2021, 2, 29, 0, 0, 0, 0, 0};
    sof_toml_writer_clear(&w);
    CHECK(sof_toml_writer_key(&w, "d") == SOF_TOML_OK && sof_toml_writer_datetime(&w, SOF_TOML_DATE_LOCAL, &bad) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_datetime(&w, SOF_TOML_DATE_LOCAL, NULL) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_datetime(&w, SOF_TOML_TIME_LOCAL, &bad) == SOF_TOML_OK && output_is(&w, "d = 00:00:00\n"));
    sof_toml_writer_free(&w);
}

static void test_writer_misplaced(void) {
    // Each call in the wrong place is refused, and writes nothing.
    sof_toml_writer_t w;
    size_t length;
    CHECK(sof_toml_writer_init(&w, 0, NULL, NULL, NULL) == SOF_TOML_OK);
    CHECK(sof_toml_writer_int(&w, 1) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_array_begin(&w) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_table_begin(&w) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_table_array_begin(&w) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_table_end(&w) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_array_end(&w) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_key(&w, NULL) == SOF_TOML_ERROR && sof_toml_writer_key_bytes(&w, NULL, 1) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_key(&w, "a") == SOF_TOML_OK && sof_toml_writer_key(&w, "b") == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_string(&w, NULL) == SOF_TOML_ERROR && sof_toml_writer_string_bytes(&w, NULL, 1) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_table_end(&w) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_array_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "k") == SOF_TOML_ERROR && sof_toml_writer_table_end(&w) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_array_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "t") == SOF_TOML_OK && sof_toml_writer_table_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_array_end(&w) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_key(&w, "x") == SOF_TOML_OK && sof_toml_writer_table_end(&w) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_int(&w, 1) == SOF_TOML_OK && sof_toml_writer_table_end(&w) == SOF_TOML_OK);
    sof_toml_writer_output(&w, &length);
    // Once the root has a section, it takes no more key/value pairs, but
    // takes more sections.
    CHECK(sof_toml_writer_key(&w, "late") == SOF_TOML_OK && sof_toml_writer_int(&w, 2) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_array_begin(&w) == SOF_TOML_ERROR && sof_toml_writer_inline_begin(&w) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_string(&w, "x") == SOF_TOML_ERROR && w.out.length == length);
    CHECK(sof_toml_writer_table_array_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_int(&w, 1) == SOF_TOML_ERROR && sof_toml_writer_key(&w, "k") == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_array_begin(&w) == SOF_TOML_ERROR && sof_toml_writer_table_end(&w) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_table_begin(&w) == SOF_TOML_OK && sof_toml_writer_table_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_array_end(&w) == SOF_TOML_OK && sof_toml_writer_table_end(&w) == SOF_TOML_ERROR);
    CHECK(output_is(&w, "a = []\n\n[t]\nx = 1\n\n[[late]]\n"));
    sof_toml_writer_free(&w);
}

static void test_writer_write(void) {
    // Output goes to the write function once the buffer passes
    // SOF_TOML_FLUSH_SIZE, and at a flush; a failed flush keeps it.
    static sink_t sink;
    sof_toml_writer_t w;
    char key[16];
    memset(&sink, 0, sizeof(sink));
    CHECK(sof_toml_writer_init(&w, 0, sink_write, &sink, NULL) == SOF_TOML_OK);
    for (int k = 0; k < 1000; k++) {
        snprintf(key, sizeof(key), "k%d", k);
        CHECK(sof_toml_writer_key(&w, key) == SOF_TOML_OK && sof_toml_writer_int(&w, k) == SOF_TOML_OK);
        CHECK(w.out.length < SOF_TOML_FLUSH_SIZE);
    }
    CHECK(sink.calls > 1 && w.written == sink.length);
    sink.fail = 1;
    size_t kept = w.out.length;
    CHECK(sof_toml_writer_flush(&w) == SOF_TOML_WRITE && w.out.length == kept);
    sink.fail = 0;
    CHECK(sof_toml_writer_flush(&w) == SOF_TOML_OK && w.out.length == 0 && w.written == sink.length);
    CHECK(sof_toml_writer_flush(&w) == SOF_TOML_OK);
    sof_toml_doc_t d;
    sof_toml_doc_init(&d, NULL);
    CHECK(sof_toml_doc_parse(&d, sink.data, sink.length) == SOF_TOML_OK && sof_toml_value_count(d.root) == 1000);
    // A section begun after output has gone still has a blank line before
    // it.
    CHECK(sof_toml_writer_key(&w, "t") == SOF_TOML_OK && sof_toml_writer_table_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_table_end(&w) == SOF_TOML_OK && output_is(&w, "\n[t]\n"));
    // A value is written whole before any of it goes to the write function,
    // in one piece, however large.
    sink.length = 0;
    sink.calls = 0;
    sof_toml_writer_clear(&w);
    CHECK(w.written == 0);
    CHECK(sof_toml_writer_key(&w, "doc") == SOF_TOML_OK && sof_toml_writer_table_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_value(&w, d.root) == SOF_TOML_OK && sink.calls == 1 && w.out.length == 0);
    CHECK(sink.length > SOF_TOML_FLUSH_SIZE && w.written == sink.length);
    sof_toml_doc_free(&d);
    sof_toml_writer_free(&w);
}

static void test_writer_value(void) {
    // A document is written again as the writer writes it: key/value pairs
    // first, then tables and arrays of tables.
    CHECK(rewrite_is(sample_text, sample_written));
    CHECK(rewrite_is("[b]\nx = 1\n[a.b]\n", "[b]\nx = 1\n\n[a.b]\n"));
    CHECK(rewrite_is("[t]\nz = [{a = 1}]\nempty = []\n[t.u]\ny = 2\n[[t.v]]\nw = {}\n",
                     "[t]\nz = [{a = 1}]\nempty = []\n\n[t.u]\ny = 2\n\n[[t.v]]\nw = {}\n"));
    CHECK(rewrite_is("a.b = 1\n[c]\nd.e.f = 2\n", "[a]\nb = 1\n\n[c.d.e]\nf = 2\n"));
    CHECK(rewrite_is("x = [{a = 1}, 2]\ny = {b = [{c = {}}]}\n", "x = [{a = 1}, 2]\ny = {b = [{c = {}}]}\n"));
    CHECK(rewrite_is("[a.b.c]\n[a]\nd = 1\n", "[a]\nd = 1\n\n[a.b.c]\n"));
    // In other places: after a key, in an array, in an inline table, and in
    // an array of tables.
    sof_toml_doc_t d;
    sof_toml_writer_t w;
    sof_toml_doc_init(&d, NULL);
    sof_toml_writer_init(&w, 0, NULL, NULL, NULL);
    CHECK(sof_toml_doc_parse(&d, "x = 1\n[t]\ny = 2\n", 16) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "v") == SOF_TOML_OK && sof_toml_writer_value(&w, sof_toml_value_get(d.root, "x")) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "arr") == SOF_TOML_OK && sof_toml_writer_array_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_value(&w, d.root) == SOF_TOML_OK && sof_toml_writer_array_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "in") == SOF_TOML_OK && sof_toml_writer_value(&w, d.root) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "list") == SOF_TOML_OK && sof_toml_writer_table_array_begin(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_value(&w, d.root) == SOF_TOML_OK && sof_toml_writer_array_end(&w) == SOF_TOML_OK);
    CHECK(output_is(&w, "v = 1\narr = [{x = 1, t = {y = 2}}]\n\n[in]\nx = 1\n\n[in.t]\ny = 2\n\n[[list]]\nx = 1\n\n[list.t]\ny = 2\n"));
    // A value that can't go where it is put writes nothing, and leaves the
    // key waiting.
    size_t length = w.out.length;
    CHECK(sof_toml_writer_key(&w, "late") == SOF_TOML_OK);
    CHECK(sof_toml_writer_value(&w, sof_toml_value_get(d.root, "x")) == SOF_TOML_ERROR);
    CHECK(w.out.length == length && w.has_key && sof_toml_writer_depth(&w) == 0);
    CHECK(sof_toml_writer_table_begin(&w) == SOF_TOML_OK && sof_toml_writer_table_end(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_value(&w, NULL) == SOF_TOML_ERROR && sof_toml_writer_value(NULL, d.root) == SOF_TOML_ERROR);
    sof_toml_writer_free(&w);
    sof_toml_doc_free(&d);
}

static void test_writer_null(void) {
    sof_toml_writer_t w;
    size_t length = 99;
    sof_toml_datetime_t dt = {2000, 1, 1, 0, 0, 0, 0, 0};
    sof_alloc_t bad = {test_alloc_malloc, NULL, NULL, NULL, NULL};
    CHECK(sof_toml_writer_init(NULL, 0, NULL, NULL, NULL) == SOF_TOML_ERROR);
    CHECK(sof_toml_writer_init(&w, 0, NULL, NULL, &bad) == SOF_TOML_ERROR && w.ready == 0);
    sof_toml_writer_t *each[] = {NULL, &w};
    for (size_t k = 0; k < LENGTH(each); k++) {
        sof_toml_writer_t *p = each[k];
        CHECK(sof_toml_writer_key(p, "a") == SOF_TOML_ERROR && sof_toml_writer_key_bytes(p, "a", 1) == SOF_TOML_ERROR);
        CHECK(sof_toml_writer_string(p, "a") == SOF_TOML_ERROR && sof_toml_writer_string_bytes(p, "a", 1) == SOF_TOML_ERROR);
        CHECK(sof_toml_writer_int(p, 1) == SOF_TOML_ERROR && sof_toml_writer_double(p, 1) == SOF_TOML_ERROR);
        CHECK(sof_toml_writer_bool(p, 1) == SOF_TOML_ERROR && sof_toml_writer_datetime(p, SOF_TOML_DATE_LOCAL, &dt) == SOF_TOML_ERROR);
        CHECK(sof_toml_writer_array_begin(p) == SOF_TOML_ERROR && sof_toml_writer_array_end(p) == SOF_TOML_ERROR);
        CHECK(sof_toml_writer_table_begin(p) == SOF_TOML_ERROR && sof_toml_writer_table_end(p) == SOF_TOML_ERROR);
        CHECK(sof_toml_writer_inline_begin(p) == SOF_TOML_ERROR && sof_toml_writer_table_array_begin(p) == SOF_TOML_ERROR);
        CHECK(sof_toml_writer_flush(p) == SOF_TOML_ERROR && sof_toml_writer_clear(p) == SOF_TOML_ERROR);
        CHECK(sof_toml_writer_output(p, &length) == NULL && length == 0 && sof_toml_writer_output(p, NULL) == NULL);
        CHECK(sof_toml_writer_depth(p) == 0 && sof_toml_writer_complete(p) == 0);
    }
    CHECK(sof_toml_writer_free(NULL) == SOF_TOML_ERROR && sof_toml_writer_free(&w) == SOF_TOML_OK);
    CHECK(sof_toml_writer_init(&w, 0, NULL, NULL, NULL) == SOF_TOML_OK);
    CHECK(sof_toml_writer_key(&w, "a") == SOF_TOML_OK && sof_toml_writer_free(&w) == SOF_TOML_OK);
    CHECK(w.ready == 0 && sof_toml_writer_free(&w) == SOF_TOML_OK && sof_toml_writer_int(&w, 1) == SOF_TOML_ERROR);
}

static void test_writer_no_memory(void) {
    // Fail each allocation in turn, writing the sample a call at a time with
    // sof_toml_writer_value() and on its own: a call that fails with
    // SOF_TOML_NO_MEM writes nothing, so made again, it finishes, and the
    // output is the same. Nothing may leak.
    sof_toml_doc_t d;
    sof_toml_doc_init(&d, NULL);
    CHECK(sof_toml_doc_parse(&d, sample_text, strlen(sample_text)) == SOF_TOML_OK);
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_toml_writer_t w;
        int failed = 0;
        sof_toml_error_t rc = sof_toml_writer_init(&w, 0, NULL, NULL, &alloc);
        if (rc == SOF_TOML_NO_MEM) {
            failed = 1;
            CHECK(w.ready == 0 && state.live == 0);
            rc = sof_toml_writer_init(&w, 0, NULL, NULL, &alloc);
        }
        CHECK(rc == SOF_TOML_OK);
        for (int whole = 0; whole < 2; whole++) {
            const sof_toml_value_t *root = d.root;
            for (size_t i = 0; i < root->length; i++) {
                const sof_toml_member_t *m = &root->as.table.members[i];
                size_t before = w.out.length;
                if ((rc = sof_toml_writer_key(&w, m->key)) == SOF_TOML_NO_MEM) {
                    failed = 1;
                    CHECK(!w.has_key);
                    rc = sof_toml_writer_key(&w, m->key);
                }
                CHECK(rc == SOF_TOML_OK);
                if ((rc = sof_toml_writer_value(&w, m->value)) == SOF_TOML_NO_MEM) {
                    failed = 1;
                    CHECK(w.out.length == before && w.has_key && sof_toml_writer_depth(&w) == 0);
                    rc = sof_toml_writer_value(&w, m->value);
                }
                CHECK(rc == SOF_TOML_OK);
            }
            CHECK(output_is(&w, sample_written));
            sof_toml_writer_clear(&w);
            if (whole) {
                break;
            }
            // The streaming calls, each made again when it fails.
            static const char *ops = "ksk{ksk[iI]}ka{k{kikx}}";
            for (const char *op = ops; *op != '\0'; op++) {
                for (int attempt = 0; attempt < 2; attempt++) {
                    size_t before = w.out.length;
                    switch (*op) {
                    case 'k':
                        rc = sof_toml_writer_key(&w, op == ops ? "title" : "k");
                        break;
                    case 's':
                        rc = sof_toml_writer_string(&w, "a fairly long string to make the buffer grow, and grow again");
                        break;
                    case 'i':
                        rc = sof_toml_writer_int(&w, 7);
                        break;
                    case 'I':
                        rc = sof_toml_writer_double(&w, 7.5);
                        break;
                    case 'x':
                        rc = sof_toml_writer_bool(&w, 1);
                        break;
                    case '{':
                        rc = sof_toml_writer_table_begin(&w);
                        break;
                    case '}':
                        rc = sof_toml_writer_table_end(&w);
                        break;
                    case '[':
                        rc = sof_toml_writer_array_begin(&w);
                        break;
                    case ']':
                        rc = sof_toml_writer_array_end(&w);
                        break;
                    case 'a':
                        rc = sof_toml_writer_table_array_begin(&w);
                        break;
                    default:
                        break;
                    }
                    if (rc != SOF_TOML_NO_MEM) {
                        break;
                    }
                    failed = 1;
                    CHECK(w.out.length == before);
                }
                if (rc != SOF_TOML_OK) {
                    printf("(call %c failing allocation %zu gave %d) ", *op, n, (int)rc);
                }
                CHECK(rc == SOF_TOML_OK);
            }
            CHECK(sof_toml_writer_array_end(&w) == SOF_TOML_OK && sof_toml_writer_complete(&w));
            CHECK(output_is(&w, "title = \"a fairly long string to make the buffer grow, and grow again\"\n"
                                "\n[k]\nk = \"a fairly long string to make the buffer grow, and grow again\"\nk = [7, 7.5]\n"
                                "\n[[k]]\n\n[k.k]\nk = 7\nk = true\n"));
            sof_toml_writer_clear(&w);
        }
        CHECK(failed == (state.allocations >= n));
        sof_toml_writer_free(&w);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            break;
        }
    }
    sof_toml_doc_free(&d);
}

static void test_random_documents(void) {
    // Random documents, written by the writer, read back and written again
    // from the document, give the same text.
    uint64_t state = 0x9e3779b97f4a7c15ULL;
    sof_toml_writer_t w, again;
    sof_toml_doc_t d;
    sof_toml_writer_init(&w, 0, NULL, NULL, NULL);
    sof_toml_writer_init(&again, 0, NULL, NULL, NULL);
    sof_toml_doc_init(&d, NULL);
    for (int k = 0; k < RANDOM_DOCUMENTS; k++) {
        sof_toml_writer_clear(&w);
        sof_toml_writer_clear(&again);
        CHECK(random_table(&w, &state, 0, 1) && sof_toml_writer_complete(&w));
        sof_toml_error_t rc = sof_toml_doc_parse(&d, w.out.data, w.out.length);
        if (rc != SOF_TOML_OK) {
            printf("(%s at %zu:%zu in\n%s) ", d.message, d.line, d.column, w.out.data);
        }
        CHECK(rc == SOF_TOML_OK);
        CHECK(sof_toml_writer_value(&again, d.root) == SOF_TOML_OK);
        CHECK(again.out.length == w.out.length && memcmp(again.out.data, w.out.data, w.out.length) == 0);
    }
    sof_toml_doc_free(&d);
    sof_toml_writer_free(&w);
    sof_toml_writer_free(&again);
}

/// @brief toml-test: a valid test must parse as its JSON says.
static int suite_valid(const char *name, const char *toml, size_t length, const sof_json_value_t *json, void *ctx) {
    sof_toml_doc_t *d = ctx;
    sof_toml_error_t rc = sof_toml_doc_parse(d, toml, length);
    if (rc != SOF_TOML_OK) {
        printf("\n    %s: refused, %s at %zu:%zu", name, d->message != NULL ? d->message : "no memory", d->line, d->column);
        return 0;
    }
    if (!matches(d->root, json)) {
        printf("\n    %s: not as its JSON says", name);
        return 0;
    }
    return 1;
}

/// @brief toml-test: an invalid test must be refused, with an error
/// saying why.
static int suite_invalid(const char *name, const char *toml, size_t length, const sof_json_value_t *json, void *ctx) {
    sof_toml_doc_t *d = ctx;
    (void)json;
    sof_toml_error_t rc = sof_toml_doc_parse(d, toml, length);
    if (rc == SOF_TOML_OK || d->message == NULL || d->line == 0) {
        printf("\n    %s: %s", name, rc == SOF_TOML_OK ? "accepted" : "refused without saying where");
        return 0;
    }
    return 1;
}

/// @brief toml-test: a valid test, parsed and written again by the writer,
/// as is and as ASCII, must parse as its JSON says.
static int suite_rewritten(const char *name, const char *toml, size_t length, const sof_json_value_t *json, void *ctx) {
    sof_toml_doc_t *d = ctx, again;
    sof_toml_writer_t w;
    int ok = 1;
    sof_toml_doc_init(&again, NULL);
    for (unsigned flags = 0; ok && flags <= SOF_TOML_ASCII; flags += SOF_TOML_ASCII) {
        sof_toml_writer_init(&w, flags, NULL, NULL, NULL);
        ok = sof_toml_doc_parse(d, toml, length) == SOF_TOML_OK && sof_toml_writer_value(&w, d->root) == SOF_TOML_OK && sof_toml_doc_parse(&again, w.out.data, w.out.length) == SOF_TOML_OK && matches(again.root, json);
        if (!ok) {
            printf("\n    %s: written as\n%s", name, w.out.data);
        }
        sof_toml_writer_free(&w);
    }
    sof_toml_doc_free(&again);
    return ok;
}

/// @brief toml-test, as for an encoder: a valid test's JSON, written as
/// TOML by the writer, must parse as the JSON says.
static int suite_encoded(const char *name, const char *toml, size_t length, const sof_json_value_t *json, void *ctx) {
    sof_toml_doc_t *d = ctx;
    sof_toml_writer_t w;
    (void)toml;
    (void)length;
    sof_toml_writer_init(&w, 0, NULL, NULL, NULL);
    int ok = encode_members(&w, json) && sof_toml_writer_complete(&w) && sof_toml_doc_parse(d, w.out.data, w.out.length) == SOF_TOML_OK && matches(d->root, json);
    if (!ok) {
        printf("\n    %s: encoded as\n%s", name, w.out.data != NULL ? w.out.data : "");
    }
    sof_toml_writer_free(&w);
    return ok;
}

static void test_suite_valid(void) {
    sof_toml_doc_t d;
    int count;
    sof_toml_doc_init(&d, NULL);
    int ok = suite_run(1, suite_valid, &d, &count);
    sof_toml_doc_free(&d);
    CHECK(count == SUITE_VALID);
    CHECK(ok);
}

static void test_suite_invalid(void) {
    sof_toml_doc_t d;
    int count;
    sof_toml_doc_init(&d, NULL);
    int ok = suite_run(0, suite_invalid, &d, &count);
    sof_toml_doc_free(&d);
    CHECK(count == SUITE_INVALID);
    CHECK(ok);
}

static void test_suite_rewritten(void) {
    sof_toml_doc_t d;
    int count;
    sof_toml_doc_init(&d, NULL);
    int ok = suite_run(1, suite_rewritten, &d, &count);
    sof_toml_doc_free(&d);
    CHECK(count == SUITE_VALID);
    CHECK(ok);
}

static void test_suite_encoded(void) {
    sof_toml_doc_t d;
    int count;
    sof_toml_doc_init(&d, NULL);
    int ok = suite_run(1, suite_encoded, &d, &count);
    sof_toml_doc_free(&d);
    CHECK(count == SUITE_VALID);
    CHECK(ok);
}

static void test_doc_string_runs(void) {
    // Strings and comments of plain bytes, which are skipped 8 at a time,
    // with one byte or sequence that isn't plain at each place and each
    // alignment, read as they do a byte at a time, with the same spans, and
    // fail at the same place. Each row is a kind, the special bytes, what
    // they read as, or NULL if they are an error, and 1 if that error is at
    // the string's start rather than at them.
    static const struct {
        const char *open, *close;
        const char *special, *decoded;
        int at_start;
    } rows[] = {
        {"\"", "\"", "\\\"", "\"", 0},          {"\"", "\"", "\\\\", "\\", 0},           {"\"", "\"", "\\t", "\t", 0},
        {"\"", "\"", "\t", "\t", 0},            {"\"", "\"", "\xc3\xa9", "\xc3\xa9", 0}, {"\"", "\"", "'", "'", 0},
        {"\"", "\"", "\x01", NULL, 0},          {"\"", "\"", "\x7f", NULL, 0},           {"\"", "\"", "\n", NULL, 1},
        {"'", "'", "\\", "\\", 0},              {"'", "'", "\"", "\"", 0},               {"'", "'", "\t", "\t", 0},
        {"'", "'", "\xe2\x82\xac", "\xe2\x82\xac", 0}, {"'", "'", "\x1f", NULL, 0},      {"'", "'", "\x7f", NULL, 0},
        {"'", "'", "\r", NULL, 1},              {"\"\"\"", "\"\"\"", "\"", "\"", 0},     {"\"\"\"", "\"\"\"", "\\u00e9", "\xc3\xa9", 0},
        {"\"\"\"", "\"\"\"", "\n", "\n", 0},    {"\"\"\"", "\"\"\"", "\r\n", "\n", 0},   {"\"\"\"", "\"\"\"", "\t", "\t", 0},
        {"\"\"\"", "\"\"\"", "\x08", NULL, 0},  {"\"\"\"", "\"\"\"", "\x7f", NULL, 0},   {"\"\"\"", "\"\"\"", "\r", NULL, 0},
        {"'''", "'''", "'", "'", 0},            {"'''", "'''", "\\", "\\", 0},           {"'''", "'''", "\r\n", "\n", 0},
        {"'''", "'''", "\xc3\xa9", "\xc3\xa9", 0}, {"'''", "'''", "\x01", NULL, 0},      {"'''", "'''", "\x7f", NULL, 0},
        {"'''", "'''", "\r", NULL, 0},          {"1 #", "\n", "\t", "", 0},              {"1 #", "\n", "\xc3\xa9#'\"", "", 0},
        {"1 #", "\n", "\x01", NULL, 0},         {"1 #", "\n", "\x7f", NULL, 0},
    };
    char text[160], expected[128];
    sof_toml_doc_t d;
    CHECK(sof_toml_doc_init(&d, NULL) == SOF_TOML_OK);
    for (size_t k = 0; k < LENGTH(rows); k++) {
        int comment = rows[k].open[0] == '1';
        for (size_t length = 0; length <= 40; length++) {
            for (size_t at = 0; at <= length; at++) {
                for (size_t pad = 0; pad < 8; pad++) {
                    size_t n = (size_t)snprintf(text, sizeof(text), "k =%*s", (int)pad + 1, "");
                    size_t start = n, bad = 0, e = 0;
                    memcpy(text + n, rows[k].open, strlen(rows[k].open));
                    n += strlen(rows[k].open);
                    for (size_t i = 0; i < length; i++) {
                        if (i == at) {
                            bad = n;
                            memcpy(text + n, rows[k].special, strlen(rows[k].special));
                            n += strlen(rows[k].special);
                            // A newline just after a multi-line string's
                            // opening quotes is dropped.
                            int dropped = i == 0 && strlen(rows[k].open) == 3 && (rows[k].special[0] == '\n' || rows[k].special[0] == '\r');
                            if (rows[k].decoded != NULL && !dropped) {
                                memcpy(expected + e, rows[k].decoded, strlen(rows[k].decoded));
                                e += strlen(rows[k].decoded);
                            }
                        } else {
                            text[n++] = (char)('a' + (i * 3 + k) % 26);
                            expected[e++] = text[n - 1];
                        }
                    }
                    memcpy(text + n, rows[k].close, strlen(rows[k].close));
                    n += strlen(rows[k].close);
                    size_t end = n;
                    n += (size_t)snprintf(text + n, sizeof(text) - n, "\nz = 2\n");
                    sof_toml_error_t rc = sof_toml_doc_parse(&d, text, n);
                    if (at < length && rows[k].decoded == NULL) {
                        CHECK(rc == SOF_TOML_SYNTAX && d.offset == (rows[k].at_start ? start : bad));
                        continue;
                    }
                    CHECK(rc == SOF_TOML_OK && sof_toml_value_count(d.root) == 2);
                    const sof_toml_value_t *v = sof_toml_value_get(d.root, "k");
                    if (comment) {
                        CHECK(sof_toml_value_type(v) == SOF_TOML_INTEGER && v->span.offset == start && v->span.length == 1);
                    } else {
                        size_t got;
                        const char *s = sof_toml_value_string(v, &got);
                        CHECK(s != NULL && got == e && memcmp(s, expected, e) == 0 && s[e] == '\0');
                        CHECK(v->span.offset == start && v->span.length == end - start);
                    }
                }
            }
        }
    }
    sof_toml_doc_free(&d);
}

static void test_doc_float_fast(void) {
    // Floats that fit Clinger's fast path, those just beyond, and random
    // ones of every shape, with underscores and signs, convert to what
    // strtod() gives for them without their underscores.
    static const char *const floats[] = {"9007199254740991.0", "9007199254740992.0", "9_007_199_254_740_993.0", "-9007199254740993.0", "1e22",
                                         "1e23", "-1E22", "1e-22", "1e-23", "123456789012345e-22", "1234567890123456e-22", "0.1", "-0.0",
                                         "+0.0", "0.000000000000000000001", "1000000000000000000000.0", "1e2_2", "3.141_59",
                                         "4.9e-324", "1.7976931348623157e308", "0.30000000000000004", "123.456e-3", "1.5E+3",
                                         "2.5000000000000000000000000000", "0.000123450000", "17976931348623157e292", "4503599627370497.5",
                                         "1e0", "-0e-0", "0e999999999999999999", "1e-400", "12345678901234567890.0", "1234567890123456789.0"};
    char text[128], clean[128];
    uint64_t state = 0x9b05688c2b3e6c1fULL;
    sof_toml_doc_t d;
    CHECK(sof_toml_doc_init(&d, NULL) == SOF_TOML_OK);
    for (size_t i = 0; i < LENGTH(floats) + 50000; i++) {
        size_t n = (size_t)snprintf(text, sizeof(text), "x = "), from = n;
        if (i < LENGTH(floats)) {
            n += (size_t)snprintf(text + n, sizeof(text) - n, "%s", floats[i]);
        } else {
            // A sign, up to 20 digits before the point with underscores, up
            // to 20 after it and trailing zeros, an exponent, or both.
            uint64_t r = next_random(&state);
            if (r % 3 != 2) {
                text[n++] = r % 3 == 0 ? '-' : '+';
            }
            size_t whole = (r >> 2) % 21, fraction = (r >> 7) % 21, zeros = (r >> 12) % 4;
            if (whole == 0) {
                text[n++] = '0';
            }
            for (size_t k = 0; k < whole; k++) {
                if (k > 0 && (r >> (20 + k)) % 4 == 0) {
                    text[n++] = '_';
                }
                text[n++] = (char)('0' + (k == 0 ? 1 + next_random(&state) % 9 : next_random(&state) % 10));
            }
            if ((r >> 15) % 3 != 2) {
                text[n++] = '.';
                for (size_t k = 0; k < fraction; k++) {
                    text[n++] = (char)('0' + next_random(&state) % 10);
                }
                memset(text + n, '0', zeros + 1);
                n += zeros + 1;
            }
            if ((r >> 15) % 3 != 1) {
                n += (size_t)snprintf(text + n, sizeof(text) - n, "e%d", (int)((r >> 17) % 61) - 30);
            }
        }
        size_t c = 0;
        for (size_t k = from; k < n; k++) {
            if (text[k] != '_') {
                clean[c++] = text[k];
            }
        }
        clean[c] = '\0';
        text[n++] = '\n';
        double expected = strtod(clean, NULL), value = 0;
        if (isinf(expected)) {
            CHECK(sof_toml_doc_parse(&d, text, n) == SOF_TOML_RANGE);
            continue;
        }
        CHECK(sof_toml_doc_parse(&d, text, n) == SOF_TOML_OK);
        CHECK(sof_toml_value_double(sof_toml_value_get(d.root, "x"), &value) == SOF_TOML_OK);
        if (memcmp(&value, &expected, sizeof(value)) != 0) {
            printf("(%s: %.17g, not %.17g) ", clean, value, expected);
        }
        CHECK(memcmp(&value, &expected, sizeof(value)) == 0);
    }
    sof_toml_doc_free(&d);
}

static void test_doc_read_large(void) {
    // A text many times SOF_TOML_READ_SIZE, read in pieces of every size
    // around it, with each allocation failing in turn for some, gives what
    // parsing it does.
    sof_string_t text;
    char line[96];
    CHECK(sof_string_init(&text, 0, NULL) == SOF_STRING_OK);
    for (int k = 0; k < 1500; k++) {
        snprintf(line, sizeof(line), "key%d = \"a string long enough to fill the buffers %d\" # and a comment\n", k, k);
        sof_string_append_cstr(&text, line);
    }
    sof_toml_doc_t d;
    sof_toml_writer_t expected, again;
    CHECK(sof_toml_doc_init(&d, NULL) == SOF_TOML_OK);
    sof_toml_writer_init(&expected, 0, NULL, NULL, NULL);
    sof_toml_writer_init(&again, 0, NULL, NULL, NULL);
    CHECK(sof_toml_doc_parse(&d, text.data, text.length) == SOF_TOML_OK);
    CHECK(sof_toml_writer_value(&expected, d.root) == SOF_TOML_OK);
    static const size_t sizes[] = {1, 4095, SOF_TOML_READ_SIZE, 4097, 10000, (size_t)-1};
    for (size_t k = 0; k < LENGTH(sizes); k++) {
        chunks_t chunks = {text.data, text.length, 0, sizes[k], 0, 0};
        CHECK(sof_toml_doc_read(&d, chunks_read, &chunks) == SOF_TOML_OK);
        sof_toml_writer_clear(&again);
        CHECK(sof_toml_writer_value(&again, d.root) == SOF_TOML_OK);
        CHECK(again.out.length == expected.out.length && memcmp(again.out.data, expected.out.data, again.out.length) == 0);
    }
    sof_toml_doc_free(&d);
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        chunks_t chunks = {text.data, text.length, 0, 5000, 0, 0};
        CHECK(sof_toml_doc_init(&d, &alloc) == SOF_TOML_OK);
        sof_toml_error_t rc = sof_toml_doc_read(&d, chunks_read, &chunks);
        if (rc == SOF_TOML_NO_MEM) {
            CHECK(d.root == NULL);
            chunks.position = 0;
            rc = sof_toml_doc_read(&d, chunks_read, &chunks);
        }
        CHECK(rc == SOF_TOML_OK && sof_toml_value_count(d.root) == 1500);
        sof_toml_doc_free(&d);
        CHECK(state.live == 0 && state.bad == 0);
        if (state.allocations < n) {
            break;
        }
    }
    sof_toml_writer_free(&expected);
    sof_toml_writer_free(&again);
    sof_string_free(&text);
}

static void test_writer_string_runs(void) {
    // Strings of plain bytes, which are found 8 at a time, with one byte or
    // sequence that isn't plain at each place, at each alignment, are
    // escaped as they are a byte at a time.
    static const char *const specials[] = {"\"", "\\", "\n", "\x01", "\x1b", "\x7f", "\xc3\xa9", "\xf0\x9f\x98\x80", "\xff", "\xc3"};
    static const char *const escaped[] = {"\\\"", "\\\\", "\\n", "\\u0001", "\\u001B", "\\u007F", "\xc3\xa9", "\xf0\x9f\x98\x80", "\xef\xbf\xbd", "\xef\xbf\xbd"};
    static const char *const ascii[] = {"\\\"", "\\\\", "\\n", "\\u0001", "\\u001B", "\\u007F", "\\u00E9", "\\U0001F600", "\\uFFFD", "\\uFFFD"};
    char input[64], expected[160];
    sof_toml_writer_t w;
    for (unsigned flags = 0; flags <= SOF_TOML_ASCII; flags += SOF_TOML_ASCII) {
        CHECK(sof_toml_writer_init(&w, flags, NULL, NULL, NULL) == SOF_TOML_OK);
        for (size_t length = 0; length <= 40; length++) {
            for (size_t at = 0; at <= length; at++) {
                for (size_t k = 0; k < LENGTH(specials); k++) {
                    for (size_t align = 0; align < 8; align++) {
                        char *s = input + align;
                        size_t n = 0, e = (size_t)snprintf(expected, sizeof(expected), "x = \"");
                        for (size_t i = 0; i < length; i++) {
                            if (i == at) {
                                const char *x = flags != 0 ? ascii[k] : escaped[k];
                                memcpy(s + n, specials[k], strlen(specials[k]));
                                n += strlen(specials[k]);
                                memcpy(expected + e, x, strlen(x));
                                e += strlen(x);
                            } else {
                                s[n++] = (char)('a' + (i * 7 + k) % 26);
                                expected[e++] = s[n - 1];
                            }
                        }
                        memcpy(expected + e, "\"\n", 3);
                        sof_toml_writer_clear(&w);
                        CHECK(sof_toml_writer_key(&w, "x") == SOF_TOML_OK && sof_toml_writer_string_bytes(&w, s, n) == SOF_TOML_OK);
                        CHECK(output_is(&w, expected));
                        // As a key, quoted, too.
                        sof_toml_writer_clear(&w);
                        CHECK(sof_toml_writer_key_bytes(&w, s, n) == SOF_TOML_OK && sof_toml_writer_int(&w, 1) == SOF_TOML_OK);
                        int bare = n > 0 && at == length;
                        CHECK(bare || (w.out.length == e - 4 + 6 && memcmp(w.out.data, expected + 4, e - 4) == 0));
                    }
                }
            }
        }
        sof_toml_writer_free(&w);
    }
}

static void test_writer_double_edges(void) {
    // Doubles at the edges of the ways they are written read back as
    // themselves, with no more digits than they need.
    static const double edges[] = {5e-324, 1e-320, 2.2250738585072014e-308, 1e-5, 1e-4, 0.1, 1, 100, 1e15, 1e16, 1e17, 9007199254740992.0,
                                   1e21, 1e22, 1e23, 1e300, 1.7976931348623157e308};
    sof_toml_writer_t w;
    uint64_t state = 0x5be0cd19137e2179ULL;
    CHECK(sof_toml_writer_init(&w, 0, NULL, NULL, NULL) == SOF_TOML_OK);
    for (size_t i = 0; i < LENGTH(edges) * 6 + 4000; i++) {
        double value;
        if (i < LENGTH(edges) * 6) {
            // The value, or a double just above or below it.
            uint64_t bits;
            value = edges[i / 6];
            memcpy(&bits, &value, sizeof(bits));
            bits = i % 6 < 3 ? bits + i % 3 : bits - (bits >= i % 3 ? i % 3 : 0);
            memcpy(&value, &bits, sizeof(value));
            value = i % 2 == 0 ? value : -value;
        } else if (i % 2 == 0) {
            uint64_t bits = next_random(&state) & ((UINT64_C(1) << 52) - 1);
            memcpy(&value, &bits, sizeof(value));
        } else {
            value = (double)(next_random(&state) % 100000);
            for (int p = (int)(next_random(&state) % 60) - 30; p != 0; p += p < 0 ? 1 : -1) {
                value = p < 0 ? value / 10 : value * 10;
            }
        }
        if (!isfinite(value) || value == 0) {
            continue;
        }
        sof_toml_writer_clear(&w);
        CHECK(sof_toml_writer_key(&w, "x") == SOF_TOML_OK && sof_toml_writer_double(&w, value) == SOF_TOML_OK);
        const char *text = w.out.data + 4;
        CHECK(strtod(text, NULL) == value);
        // The significant digits, which a ".0" after an integer doesn't add
        // to.
        int digits = 0, trailing = 0;
        for (const char *p = text; *p != '\0' && *p != 'e' && *p != '\n'; p++) {
            if (*p >= '1' && *p <= '9') {
                digits += trailing + 1;
                trailing = 0;
            } else if (*p == '0' && digits > 0) {
                trailing++;
            }
        }
        int shortest = 1;
        for (; shortest < 17; shortest++) {
            char t[64];
            snprintf(t, sizeof(t), "%.*g", shortest, value);
            if (strtod(t, NULL) == value) {
                break;
            }
        }
        CHECK(digits == shortest);
    }
    sof_toml_writer_free(&w);
}

int main(void) {
    printf("sof_toml:\n");
    run("document: parse and values", test_doc_values);
    run("document: tables, dotted keys and arrays of tables", test_doc_tables);
    run("document: strings", test_doc_strings);
    run("document: integers and floats", test_doc_numbers);
    run("document: dates and times", test_doc_datetimes);
    run("document: errors and where they are", test_doc_errors);
    run("document: depth", test_doc_depth);
    run("document: index", test_doc_index);
    run("document: paths", test_doc_path);
    run("document: spans", test_doc_spans);
    run("document: reading through a read function", test_doc_read);
    run("document: NULL and freed documents and values", test_doc_null);
    run("document: each allocation failing", test_doc_no_memory);
    run("writer: sections, arrays of tables and inline values", test_writer_sections);
    run("writer: strings and keys", test_writer_strings);
    run("writer: numbers, dates and times", test_writer_numbers);
    run("writer: misplaced calls", test_writer_misplaced);
    run("writer: write function", test_writer_write);
    run("writer: document values", test_writer_value);
    run("writer: NULL and freed writers", test_writer_null);
    run("writer: each allocation failing", test_writer_no_memory);
    run("random documents written, read and written again", test_random_documents);
    run("document: strings and comments of plain bytes with one that isn't", test_doc_string_runs);
    run("document: floats against strtod()", test_doc_float_fast);
    run("document: reading a large text through a read function", test_doc_read_large);
    run("writer: strings of plain bytes with one that isn't", test_writer_string_runs);
    run("writer: doubles at the edges of their forms", test_writer_double_edges);
    run("toml-test: valid documents", test_suite_valid);
    run("toml-test: invalid documents", test_suite_invalid);
    run("toml-test: valid documents written again", test_suite_rewritten);
    run("toml-test: valid documents' JSON encoded", test_suite_encoded);
    return test_report();
}
