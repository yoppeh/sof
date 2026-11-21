/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_json: the writer, the reader and the document.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <dirent.h>
#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_json.h"
#include "sof_utf.h"
#include "test.h"
#include "test_json.h"

/// @brief The number of elements in an array.
#define LENGTH(a) (sizeof(a) / sizeof((a)[0]))

/// @brief The deepest the random documents nest.
#define RANDOM_DEPTH 6

/// @brief The most bytes in a random string.
#define RANDOM_STRING 24

/// @brief The number of random documents written.
#define RANDOM_DOCUMENTS 300

/// @brief The number of random doubles written.
#define RANDOM_DOUBLES 20000

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

/// @brief One call of the fixed document the allocation tests write: op is
/// '{', '}', '[', ']', 'k' (a key), 's' (a string), 'i' (an integer), 'd' (a
/// double) or 'n' (null), and text is the key, string or number, or NULL for
/// the long string.
typedef struct step_t {
    char op;
    const char *text;
} step_t;

/// @brief Input given to a reader a piece at a time, by chunks_read(),
/// through its ctx.
typedef struct chunks_t {
    /// @brief The input, and its size.
    const char *data;
    size_t length;
    /// @brief The bytes given so far.
    size_t position;
    /// @brief The most bytes given at a time.
    size_t chunk;
    /// @brief The calls made, failed ones included.
    size_t calls;
    /// @brief The call to fail, counting from 1, or 0 for none.
    size_t fail_at;
} chunks_t;

/// @brief A token a test expects, and where.
typedef struct token_row_t {
    sof_json_type_t type;
    const char *text;
    int boolean;
    size_t depth;
    size_t line;
    size_t column;
} token_row_t;

/// @brief A document the reader's tests read, two lines long; the second
/// begins at offset 38.
static const char sample_text[] = "{\"a\": [1, -2.5e3, true, false, null],\n \"b\": {\"c\": \"d\"}, \"\": []}";

static int chunks_read(char *buffer, size_t size, size_t *length, void *ctx);
static int error_at(const char *json, unsigned flags, sof_json_error_t code, size_t line, size_t column);
static int number_is(double value, const char *expected);
static int output_is(const sof_json_writer_t *w, const char *expected);
static int output_is_bytes(const sof_json_writer_t *w, const char *expected, size_t length);
static size_t random_string(char *bytes, uint64_t *state);
static int random_value(sof_json_writer_t *ws, size_t n, uint64_t *state, int depth);
static sof_json_error_t reader_canon(sof_json_reader_t *r, char *out, size_t size, size_t *length);
static int reader_open(sof_json_reader_t *r, const char *json, size_t length, unsigned flags, size_t chunk, chunks_t *chunks, const sof_alloc_t *alloc);
static int significant_digits(const char *text);
static int sink_write(const char *data, size_t length, void *ctx);
static int span_is(const char *text, sof_json_span_t span, const char *expected);
static sof_json_error_t step_run(sof_json_writer_t *w, const step_t *step, const char *long_string);
static int string_is(const char *json, const char *expected, size_t expected_length);
static size_t strip_space(const char *text, size_t length, char *out);
static int token_canon(const sof_json_token_t *t, char *out, size_t size, size_t *n);

/// @brief The read function of the tests: gives the next chunk bytes of the
/// input in ctx, a chunks_t, or fails if this is its fail_at call.
static int chunks_read(char *buffer, size_t size, size_t *length, void *ctx) {
    chunks_t *c = ctx;
    c->calls++;
    *length = 0;
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

/// @brief Says whether reading json, whole and a byte at a time, fails
/// with code at line and column, and fails the same way when tried again.
static int error_at(const char *json, unsigned flags, sof_json_error_t code, size_t line, size_t column) {
    for (size_t chunk = 0; chunk < 2; chunk++) {
        sof_json_reader_t r;
        chunks_t chunks;
        sof_json_token_t t;
        sof_json_error_t rc = SOF_JSON_ERROR;
        int ok = reader_open(&r, json, strlen(json), flags, chunk, &chunks, NULL);
        while (ok && (rc = sof_json_reader_next(&r, &t)) == SOF_JSON_OK) {
        }
        ok = ok && rc == code && r.line == line && r.column == column && t.type == SOF_JSON_NONE;
        ok = ok && sof_json_reader_next(&r, &t) == code && r.line == line && r.column == column;
        if (!ok) {
            printf("(%s: %d at %zu:%zu) ", json, (int)rc, r.line, r.column);
        }
        sof_json_reader_free(&r);
        if (!ok) {
            return 0;
        }
    }
    return 1;
}

/// @brief Says whether a double is written as expected, at the top level.
static int number_is(double value, const char *expected) {
    sof_json_writer_t w;
    char line[64];
    snprintf(line, sizeof(line), "%s\n", expected);
    int ok = sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK && sof_json_writer_double(&w, value) == SOF_JSON_OK && output_is(&w, line);
    if (!ok) {
        printf("(%.17g gave %s) ", value, w.out.data != NULL ? w.out.data : "nothing");
    }
    sof_json_writer_free(&w);
    return ok;
}

/// @brief Says whether a writer's buffer holds the c string expected.
static int output_is(const sof_json_writer_t *w, const char *expected) {
    return output_is_bytes(w, expected, strlen(expected));
}

/// @brief Says whether a writer's buffer holds the length bytes expected.
static int output_is_bytes(const sof_json_writer_t *w, const char *expected, size_t length) {
    size_t n;
    const char *out = sof_json_writer_output(w, &n);
    return out != NULL && n == length && memcmp(out, expected, n) == 0 && out[n] == '\0';
}

/// @brief Makes a random string: printable ASCII, control characters,
/// valid UTF-8 of any length, and bytes that aren't UTF-8.
/// @return Its length, at most RANDOM_STRING.
static size_t random_string(char *bytes, uint64_t *state) {
    size_t length = 0;
    size_t want = next_random(state) % (RANDOM_STRING - 3);
    while (length < want) {
        uint64_t r = next_random(state);
        switch (r % 6) {
        case 0:
        case 1:
            bytes[length++] = (char)(0x20 + (r >> 8) % 0x60);
            break;
        case 2:
            bytes[length++] = (char)((r >> 8) % 0x20);
            break;
        case 3:
            bytes[length++] = "\"\\/"[(r >> 8) % 3];
            break;
        case 4: {
            uint32_t code = (uint32_t)((r >> 8) % (SOF_UTF_MAX + 1));
            length += sof_utf8_encode(sof_utf_valid(code) ? code : 0xe9, bytes + length, 4);
            break;
        }
        default:
            bytes[length++] = (char)(0x80 | (r >> 8));
            break;
        }
    }
    return length;
}

/// @brief Writes the same random value to each of n writers: a scalar, or
/// below RANDOM_DEPTH sometimes an array or object of random values.
/// @return 1, or 0 if a call didn't return SOF_JSON_OK.
static int random_value(sof_json_writer_t *ws, size_t n, uint64_t *state, int depth) {
    uint64_t r = next_random(state);
    int kind = depth >= RANDOM_DEPTH ? (int)(r % 6) : (int)(r % 8);
    char bytes[RANDOM_STRING];
    size_t length = 0;
    double d = 0;
    uint64_t u = next_random(state);
    if (kind == 3) {
        length = random_string(bytes, state);
    }
    if (kind == 5) {
        do {
            uint64_t bits = next_random(state);
            memcpy(&d, &bits, sizeof(d));
        } while (!isfinite(d));
    }
    for (size_t i = 0; i < n; i++) {
        sof_json_error_t rc;
        switch (kind) {
        case 0:
            rc = sof_json_writer_null(&ws[i]);
            break;
        case 1:
            rc = sof_json_writer_bool(&ws[i], (int)(u & 1));
            break;
        case 2:
            rc = (u & 1) ? sof_json_writer_int(&ws[i], (int64_t)u) : sof_json_writer_uint(&ws[i], u);
            break;
        case 3:
            rc = sof_json_writer_string_bytes(&ws[i], bytes, length);
            break;
        case 4:
            rc = sof_json_writer_raw(&ws[i], "[1,{\"x\":2}]", 11);
            break;
        case 5:
            rc = sof_json_writer_double(&ws[i], d);
            break;
        case 6:
            rc = sof_json_writer_array_begin(&ws[i]);
            break;
        default:
            rc = sof_json_writer_object_begin(&ws[i]);
            break;
        }
        if (rc != SOF_JSON_OK) {
            return 0;
        }
    }
    if (kind < 6) {
        return 1;
    }
    size_t count = (size_t)(u % 5);
    for (size_t k = 0; k < count; k++) {
        if (kind == 7) {
            length = random_string(bytes, state);
            for (size_t i = 0; i < n; i++) {
                if (sof_json_writer_key_bytes(&ws[i], bytes, length) != SOF_JSON_OK) {
                    return 0;
                }
            }
        }
        if (!random_value(ws, n, state, depth + 1)) {
            return 0;
        }
    }
    for (size_t i = 0; i < n; i++) {
        sof_json_error_t rc = kind == 6 ? sof_json_writer_array_end(&ws[i]) : sof_json_writer_object_end(&ws[i]);
        if (rc != SOF_JSON_OK) {
            return 0;
        }
    }
    return 1;
}

/// @brief Reads every token, putting them in out in test_json_text()'s
/// canonical form, with its length in *length.
/// @return What sof_json_reader_next() returned last, which is
/// SOF_JSON_NO_MATCH if all the input was read; or SOF_JSON_ERROR if a
/// token didn't fit, or its text wasn't valid UTF-8.
static sof_json_error_t reader_canon(sof_json_reader_t *r, char *out, size_t size, size_t *length) {
    sof_json_token_t t;
    sof_json_error_t rc;
    *length = 0;
    while ((rc = sof_json_reader_next(r, &t)) == SOF_JSON_OK) {
        if (!token_canon(&t, out, size, length)) {
            return SOF_JSON_ERROR;
        }
    }
    return rc;
}

/// @brief Sets up a reader of the length bytes of json: of the bytes
/// themselves if chunk is 0, or through chunks_read(), chunk bytes at a
/// time, with chunks as its state.
/// @return 1, or 0 if the init failed.
static int reader_open(sof_json_reader_t *r, const char *json, size_t length, unsigned flags, size_t chunk, chunks_t *chunks, const sof_alloc_t *alloc) {
    memset(chunks, 0, sizeof(*chunks));
    if (chunk == 0) {
        return sof_json_reader_init_bytes(r, flags, json, length, alloc) == SOF_JSON_OK;
    }
    chunks->data = json;
    chunks->length = length;
    chunks->chunk = chunk;
    return sof_json_reader_init(r, flags, chunks_read, chunks, alloc) == SOF_JSON_OK;
}

/// @brief Counts the significant digits of a number as the writer writes
/// it: the digits before any exponent, less leading and trailing zeros.
static int significant_digits(const char *text) {
    char digits[64];
    int n = 0;
    for (const char *p = text; *p != '\0' && *p != 'e' && *p != '\n'; p++) {
        if (*p >= '0' && *p <= '9' && (n > 0 || *p != '0')) {
            digits[n++] = *p;
        }
    }
    while (n > 0 && digits[n - 1] == '0') {
        n--;
    }
    return n;
}

/// @brief The write function of the tests: appends to the sink in ctx,
/// unless its fail is set.
static int sink_write(const char *data, size_t length, void *ctx) {
    sink_t *sink = ctx;
    sink->calls++;
    if (sink->fail || length > SINK_SIZE - sink->length) {
        return 1;
    }
    memcpy(sink->data + sink->length, data, length);
    sink->length += length;
    return 0;
}

/// @brief Says whether span covers exactly the text expected in text.
static int span_is(const char *text, sof_json_span_t span, const char *expected) {
    size_t length = strlen(expected);
    if (span.offset > strlen(text) || span.length > strlen(text) - span.offset || span.length != length || memcmp(text + span.offset, expected, length) != 0) {
        printf("(span %zu+%zu, not \"%s\") ", span.offset, span.length, expected);
        return 0;
    }
    return 1;
}

/// @brief Makes the call a step names.
static sof_json_error_t step_run(sof_json_writer_t *w, const step_t *step, const char *long_string) {
    const char *text = step->text != NULL ? step->text : long_string;
    switch (step->op) {
    case '{':
        return sof_json_writer_object_begin(w);
    case '}':
        return sof_json_writer_object_end(w);
    case '[':
        return sof_json_writer_array_begin(w);
    case ']':
        return sof_json_writer_array_end(w);
    case 'k':
        return sof_json_writer_key(w, text);
    case 's':
        return sof_json_writer_string(w, text);
    case 'i':
        return sof_json_writer_int(w, strtoll(text, NULL, 10));
    case 'd':
        return sof_json_writer_double(w, strtod(text, NULL));
    default:
        return sof_json_writer_null(w);
    }
}

/// @brief Says whether json, read whole and a byte at a time, is one string
/// that decodes to the expected_length bytes at expected.
static int string_is(const char *json, const char *expected, size_t expected_length) {
    for (size_t chunk = 0; chunk < 2; chunk++) {
        sof_json_reader_t r;
        chunks_t chunks;
        sof_json_token_t t;
        int ok = reader_open(&r, json, strlen(json), 0, chunk, &chunks, NULL) && sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_STRING && t.length == expected_length && memcmp(t.text, expected, expected_length) == 0 && t.text[t.length] == '\0' && sof_json_reader_next(&r, &t) == SOF_JSON_NO_MATCH;
        sof_json_reader_free(&r);
        if (!ok) {
            printf("(%s) ", json);
            return 0;
        }
    }
    return 1;
}

/// @brief Copies JSON text to out without the whitespace outside strings.
/// @return The length copied.
static size_t strip_space(const char *text, size_t length, char *out) {
    size_t n = 0;
    int in_string = 0;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        if (in_string) {
            out[n++] = c;
            if (c == '\\') {
                out[n++] = text[++i];
            } else if (c == '"') {
                in_string = 0;
            }
        } else if (c == '"') {
            out[n++] = c;
            in_string = 1;
        } else if (c != ' ' && c != '\n') {
            out[n++] = c;
        }
    }
    return n;
}

/// @brief Appends a token, in test_json_text()'s canonical form, to out,
/// which holds *n of its size bytes.
/// @return 1, or 0 if it doesn't fit, or its text isn't NUL-terminated or,
/// for a string or key, valid UTF-8.
static int token_canon(const sof_json_token_t *t, char *out, size_t size, size_t *n) {
    char head[16];
    size_t head_length = 1;
    const char *tail = "";
    switch (t->type) {
    case SOF_JSON_OBJECT:
        head[0] = '{';
        break;
    case SOF_JSON_OBJECT_END:
        head[0] = '}';
        break;
    case SOF_JSON_ARRAY:
        head[0] = '[';
        break;
    case SOF_JSON_ARRAY_END:
        head[0] = ']';
        break;
    case SOF_JSON_NULL:
        head[0] = 'z';
        break;
    case SOF_JSON_BOOL:
        head[0] = t->boolean ? 't' : 'f';
        break;
    case SOF_JSON_NUMBER:
        head[0] = 'n';
        tail = ";";
        break;
    case SOF_JSON_STRING:
    case SOF_JSON_KEY:
        if (sof_utf8_validate(t->text, t->length) != t->length) {
            return 0;
        }
        head_length = (size_t)snprintf(head, sizeof(head), "s%08zx", t->length);
        break;
    default:
        return 0;
    }
    size_t tail_length = strlen(tail);
    if (t->text[t->length] != '\0' || size - *n < head_length + t->length + tail_length) {
        return 0;
    }
    memcpy(out + *n, head, head_length);
    *n += head_length;
    memcpy(out + *n, t->text, t->length);
    *n += t->length;
    memcpy(out + *n, tail, tail_length);
    *n += tail_length;
    return 1;
}

static void test_init(void) {
    sof_json_writer_t w;
    sink_t *sink = NULL;
    CHECK(sof_json_writer_init(&w, 3, SOF_JSON_ASCII, sink_write, &sink, NULL) == SOF_JSON_OK);
    CHECK(w.ready == 1 && w.indent == 3 && w.flags == SOF_JSON_ASCII);
    CHECK(w.write == sink_write && w.ctx == &sink);
    CHECK(w.values == 0 && w.written == 0);
    CHECK(sof_json_writer_depth(&w) == 0 && !sof_json_writer_complete(&w));
    CHECK(output_is(&w, ""));
    CHECK(sof_json_writer_free(&w) == SOF_JSON_OK);
    CHECK(w.ready == 0);

    CHECK(sof_json_writer_init(NULL, 0, 0, NULL, NULL, NULL) == SOF_JSON_ERROR);

    // An allocator that can't free is refused, and nothing is allocated.
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, &alloc) == SOF_JSON_ERROR);
    CHECK(w.ready == 0 && state.allocations == 0);
    CHECK(sof_json_writer_null(&w) == SOF_JSON_ERROR);
}

static void test_compact(void) {
    sof_json_writer_t w;
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_depth(&w) == 1);
    CHECK(sof_json_writer_key(&w, "a") == SOF_JSON_OK);
    CHECK(sof_json_writer_int(&w, 1) == SOF_JSON_OK);
    CHECK(sof_json_writer_key(&w, "b") == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_depth(&w) == 2);
    CHECK(sof_json_writer_bool(&w, 1) == SOF_JSON_OK);
    CHECK(sof_json_writer_bool(&w, 0) == SOF_JSON_OK);
    CHECK(sof_json_writer_null(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_string(&w, "x") == SOF_JSON_OK);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_key(&w, "c") == SOF_JSON_OK);
    CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_object_end(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_key(&w, "d") == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    CHECK(!sof_json_writer_complete(&w));
    CHECK(output_is(&w, "{\"a\":1,\"b\":[true,false,null,\"x\"],\"c\":{},\"d\":[]"));
    CHECK(sof_json_writer_object_end(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_depth(&w) == 0 && sof_json_writer_complete(&w) && w.values == 1);
    CHECK(output_is(&w, "{\"a\":1,\"b\":[true,false,null,\"x\"],\"c\":{},\"d\":[]}\n"));
    CHECK(test_json_lines(w.out.data, w.out.length) == 1);
    sof_json_writer_free(&w);
}

static void test_indent(void) {
    sof_json_writer_t w;
    CHECK(sof_json_writer_init(&w, 2, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_key(&w, "a") == SOF_JSON_OK);
    CHECK(sof_json_writer_int(&w, 1) == SOF_JSON_OK);
    CHECK(sof_json_writer_key(&w, "b") == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_bool(&w, 1) == SOF_JSON_OK);
    CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_key(&w, "x") == SOF_JSON_OK);
    CHECK(sof_json_writer_null(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_object_end(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_raw(&w, "[1, 2]", 6) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_key(&w, "c") == SOF_JSON_OK);
    CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_object_end(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_key(&w, "d") == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_object_end(&w) == SOF_JSON_OK);
    // Top-level scalars, and empty containers, stay on a line of their own.
    CHECK(sof_json_writer_string(&w, "next") == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    CHECK(output_is(&w, "{\n"
                        "  \"a\": 1,\n"
                        "  \"b\": [\n"
                        "    true,\n"
                        "    {\n"
                        "      \"x\": null\n"
                        "    },\n"
                        "    [1, 2]\n"
                        "  ],\n"
                        "  \"c\": {},\n"
                        "  \"d\": []\n"
                        "}\n"
                        "\"next\"\n"
                        "[]\n"));
    CHECK(test_json_lines(w.out.data, w.out.length) == 3);
    CHECK(w.values == 3);
    sof_json_writer_free(&w);

    // Indentation past the writer's block of spaces.
    CHECK(sof_json_writer_init(&w, 100, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_int(&w, 7) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    char expected[512];
    int n = snprintf(expected, sizeof(expected), "[\n%100s[\n%200s7\n%100s]\n]\n", "", "", "");
    CHECK(n > 0 && output_is(&w, expected));
    sof_json_writer_free(&w);
}

static void test_strings(void) {
    sof_json_writer_t w, a;
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_init(&a, 0, SOF_JSON_ASCII, NULL, NULL, NULL) == SOF_JSON_OK);

    // Quotes, backslashes and control characters are escaped; / and DEL
    // aren't.
    static const char special[] = "a\"b\\c/\b\f\n\r\t\x01\x1f\x7f";
    CHECK(sof_json_writer_string_bytes(&w, special, sizeof(special)) == SOF_JSON_OK);
    CHECK(output_is(&w, "\"a\\\"b\\\\c/\\b\\f\\n\\r\\t\\u0001\\u001f\x7f\\u0000\"\n"));
    sof_json_writer_clear(&w);

    // UTF-8 is written as it is, or with SOF_JSON_ASCII as \u escapes,
    // with a surrogate pair past U+FFFF.
    static const char utf8[] = "\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80";
    CHECK(sof_json_writer_string(&w, utf8) == SOF_JSON_OK);
    CHECK(output_is(&w, "\"\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\"\n"));
    CHECK(sof_json_writer_string(&a, utf8) == SOF_JSON_OK);
    CHECK(output_is(&a, "\"\\u00e9\\u20ac\\ud83d\\ude00\"\n"));
    sof_json_writer_clear(&w);
    sof_json_writer_clear(&a);

    // Each error is one U+FFFD: a byte that can't start a sequence, an
    // overlong form, a surrogate, and a sequence cut off at the end.
    static const char bad[] = "a\xff" "b\xc0\xaf" "c\xed\xa0\x80" "d\xe2\x82";
    CHECK(sof_json_writer_string(&w, bad) == SOF_JSON_OK);
    CHECK(output_is(&w, "\"a\xef\xbf\xbd" "b\xef\xbf\xbd\xef\xbf\xbd" "c\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd" "d\xef\xbf\xbd\"\n"));
    CHECK(sof_utf8_validate(w.out.data, w.out.length) == w.out.length);
    CHECK(sof_json_writer_string(&a, bad) == SOF_JSON_OK);
    CHECK(output_is(&a, "\"a\\ufffdb\\ufffd\\ufffdc\\ufffd\\ufffd\\ufffdd\\ufffd\"\n"));
    sof_json_writer_clear(&w);
    sof_json_writer_clear(&a);

    // Empty strings, from a NULL with no length too, and NULs.
    CHECK(sof_json_writer_string(&w, "") == SOF_JSON_OK);
    CHECK(sof_json_writer_string_bytes(&w, NULL, 0) == SOF_JSON_OK);
    CHECK(sof_json_writer_string_bytes(&w, "\0\0", 2) == SOF_JSON_OK);
    CHECK(output_is(&w, "\"\"\n\"\"\n\"\\u0000\\u0000\"\n"));
    CHECK(sof_json_writer_string(&w, NULL) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_string_bytes(&w, NULL, 1) == SOF_JSON_ERROR);
    CHECK(w.values == 3);
    sof_json_writer_clear(&w);

    // Keys are escaped the same way.
    CHECK(sof_json_writer_object_begin(&a) == SOF_JSON_OK);
    CHECK(sof_json_writer_key(&a, "k\"\n\xc3\xa9") == SOF_JSON_OK);
    CHECK(sof_json_writer_int(&a, 1) == SOF_JSON_OK);
    CHECK(sof_json_writer_key_bytes(&a, "x\0y", 3) == SOF_JSON_OK);
    CHECK(sof_json_writer_int(&a, 2) == SOF_JSON_OK);
    CHECK(sof_json_writer_key_bytes(&a, NULL, 0) == SOF_JSON_OK);
    CHECK(sof_json_writer_int(&a, 3) == SOF_JSON_OK);
    CHECK(sof_json_writer_key(&a, NULL) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_key_bytes(&a, NULL, 1) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_object_end(&a) == SOF_JSON_OK);
    CHECK(output_is(&a, "{\"k\\\"\\n\\u00e9\":1,\"x\\u0000y\":2,\"\":3}\n"));

    // A long string, past the buffer's first size.
    char long_string[3000];
    for (size_t i = 0; i < sizeof(long_string) - 1; i++) {
        long_string[i] = (char)('a' + i % 26);
    }
    long_string[sizeof(long_string) - 1] = '\0';
    CHECK(sof_json_writer_string(&w, long_string) == SOF_JSON_OK);
    CHECK(w.out.length == sizeof(long_string) - 1 + 3);
    CHECK(memcmp(w.out.data + 1, long_string, sizeof(long_string) - 1) == 0);
    sof_json_writer_free(&w);
    sof_json_writer_free(&a);
}

static void test_integers(void) {
    sof_json_writer_t w;
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_int(&w, 0) == SOF_JSON_OK);
    CHECK(sof_json_writer_int(&w, -1) == SOF_JSON_OK);
    CHECK(sof_json_writer_int(&w, 1234567890) == SOF_JSON_OK);
    CHECK(sof_json_writer_int(&w, INT64_MAX) == SOF_JSON_OK);
    CHECK(sof_json_writer_int(&w, INT64_MIN) == SOF_JSON_OK);
    CHECK(sof_json_writer_uint(&w, 0) == SOF_JSON_OK);
    CHECK(sof_json_writer_uint(&w, 10) == SOF_JSON_OK);
    CHECK(sof_json_writer_uint(&w, UINT64_MAX) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    CHECK(output_is(&w, "[0,-1,1234567890,9223372036854775807,-9223372036854775808,0,10,18446744073709551615]\n"));
    sof_json_writer_free(&w);
}

static void test_doubles(void) {
    CHECK(number_is(0.0, "0"));
    CHECK(number_is(-0.0, "-0"));
    CHECK(number_is(0.1, "0.1"));
    CHECK(number_is(1.5, "1.5"));
    CHECK(number_is(-2.5e-8, "-2.5e-08"));
    CHECK(number_is(1.0 / 3, "0.3333333333333333"));
    CHECK(number_is(123456.789, "123456.789"));
    CHECK(number_is(100, "100"));
    CHECK(number_is(0.0001, "0.0001"));
    CHECK(number_is(0.00001, "1e-05"));
    CHECK(number_is(9007199254740992.0, "9007199254740992"));
    CHECK(number_is(1e16, "10000000000000000"));
    CHECK(number_is(-49120135613354312.0, "-49120135613354310"));
    CHECK(number_is(1e17, "1e+17"));
    CHECK(number_is(1e100, "1e+100"));
    CHECK(number_is(5e-324, "5e-324"));
    CHECK(number_is(1.7976931348623157e308, "1.7976931348623157e+308"));

    sof_json_writer_t w;
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_double(&w, INFINITY) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_double(&w, -INFINITY) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_double(&w, NAN) == SOF_JSON_ERROR);
    CHECK(output_is(&w, "") && w.values == 0);

    // Random doubles read back as themselves, are valid JSON numbers, and
    // have no more digits than they need.
    uint64_t state = 0x6a09e667f3bcc909ULL;
    for (int i = 0; i < RANDOM_DOUBLES; i++) {
        double value;
        uint64_t bits = next_random(&state);
        if (i % 2 == 1) {
            // Short decimals too, which have shorter forms to find.
            value = (double)(int64_t)(bits % 2000001 - 1000000) / 1000.0;
        } else {
            memcpy(&value, &bits, sizeof(value));
        }
        if (!isfinite(value)) {
            continue;
        }
        sof_json_writer_clear(&w);
        CHECK(sof_json_writer_double(&w, value) == SOF_JSON_OK);
        CHECK(test_json_lines(w.out.data, w.out.length) == 1);
        double back = strtod(w.out.data, NULL);
        CHECK(back == value && signbit(back) == signbit(value));
        int digits = significant_digits(w.out.data);
        CHECK(digits <= 17);
        if (digits > 1) {
            char shorter[64];
            snprintf(shorter, sizeof(shorter), "%.*g", digits - 1, value);
            CHECK(strtod(shorter, NULL) != value);
        }
    }
    sof_json_writer_free(&w);
}

static void test_locale(void) {
    // In a locale whose decimal point is a comma, numbers still have a '.'.
    static const char *names[] = {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "fr_FR.utf8", "fr_FR"};
    const char *found = NULL;
    for (size_t i = 0; i < LENGTH(names) && found == NULL; i++) {
        found = setlocale(LC_NUMERIC, names[i]);
    }
    if (found == NULL) {
        printf("(no locale with a decimal comma) ");
        return;
    }
    int ok = number_is(0.5, "0.5") && number_is(-1234.25, "-1234.25") && number_is(1.5e-7, "1.5e-07");
    setlocale(LC_NUMERIC, "C");
    CHECK(ok);
}

static void test_raw(void) {
    sof_json_writer_t w;
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_raw(&w, "{\"x\":[1,2]}", 11) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_raw(&w, "1e3", 3) == SOF_JSON_OK);
    CHECK(sof_json_writer_raw(&w, "\"s\"", 3) == SOF_JSON_OK);
    CHECK(sof_json_writer_raw(&w, NULL, 3) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_raw(&w, "1", 0) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    CHECK(output_is(&w, "{\"x\":[1,2]}\n[1e3,\"s\"]\n"));
    CHECK(w.values == 2);
    sof_json_writer_free(&w);
}

static void test_misplaced(void) {
    // Each misplaced call is refused and writes nothing, and the writer goes
    // on as if it hadn't been made.
    sof_json_writer_t w;
    CHECK(sof_json_writer_init(&w, 2, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_key(&w, "top") == SOF_JSON_ERROR);
    CHECK(sof_json_writer_object_end(&w) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_ERROR);
    CHECK(output_is(&w, ""));

    CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
    size_t length = w.out.length;
    // A value, or an object or array, needs a key first.
    CHECK(sof_json_writer_null(&w) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_string(&w, "s") == SOF_JSON_ERROR);
    CHECK(sof_json_writer_int(&w, 1) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_double(&w, 1.5) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_raw(&w, "1", 1) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_ERROR);
    CHECK(w.out.length == length && sof_json_writer_depth(&w) == 1);

    CHECK(sof_json_writer_key(&w, "k") == SOF_JSON_OK);
    length = w.out.length;
    // A key needs its value before another key or the end.
    CHECK(sof_json_writer_key(&w, "again") == SOF_JSON_ERROR);
    CHECK(sof_json_writer_object_end(&w) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_ERROR);
    CHECK(w.out.length == length);

    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    length = w.out.length;
    // An array has no keys, and ends with array_end.
    CHECK(sof_json_writer_key(&w, "k") == SOF_JSON_ERROR);
    CHECK(sof_json_writer_object_end(&w) == SOF_JSON_ERROR);
    CHECK(w.out.length == length && sof_json_writer_depth(&w) == 2);
    CHECK(sof_json_writer_int(&w, 1) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_object_end(&w) == SOF_JSON_OK);
    CHECK(output_is(&w, "{\n  \"k\": [\n    1\n  ]\n}\n"));
    CHECK(sof_json_writer_object_end(&w) == SOF_JSON_ERROR);
    sof_json_writer_free(&w);
}

static void test_lines(void) {
    // Top-level values follow one another a line each, as JSON Lines.
    sof_json_writer_t w;
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(!sof_json_writer_complete(&w));
    CHECK(sof_json_writer_null(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_complete(&w) && w.values == 1);
    CHECK(sof_json_writer_int(&w, 1) == SOF_JSON_OK);
    CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
    CHECK(!sof_json_writer_complete(&w) && w.values == 2);
    CHECK(sof_json_writer_key(&w, "a") == SOF_JSON_OK);
    CHECK(sof_json_writer_bool(&w, 1) == SOF_JSON_OK);
    CHECK(sof_json_writer_object_end(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_complete(&w) && w.values == 3);
    CHECK(output_is(&w, "null\n1\n{\"a\":true}\n"));
    sof_json_writer_free(&w);
}

static void test_write(void) {
    static sink_t sink;
    sof_json_writer_t w;
    memset(&sink, 0, sizeof(sink));
    CHECK(sof_json_writer_init(&w, 0, 0, sink_write, &sink, NULL) == SOF_JSON_OK);

    // Each top-level value is passed on when it ends, and not before.
    CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_key(&w, "a") == SOF_JSON_OK);
    CHECK(sof_json_writer_int(&w, 1) == SOF_JSON_OK);
    CHECK(sink.calls == 0 && output_is(&w, "{\"a\":1"));
    CHECK(sof_json_writer_object_end(&w) == SOF_JSON_OK);
    CHECK(sink.calls == 1 && sink.length == 8 && memcmp(sink.data, "{\"a\":1}\n", 8) == 0);
    CHECK(output_is(&w, "") && w.written == 8);
    CHECK(sof_json_writer_null(&w) == SOF_JSON_OK);
    CHECK(sink.calls == 2 && w.written == 13);

    // Flushing in the middle of a value, and with nothing to flush.
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_flush(&w) == SOF_JSON_OK);
    CHECK(sink.calls == 3 && output_is(&w, ""));
    CHECK(sof_json_writer_flush(&w) == SOF_JSON_OK);
    CHECK(sink.calls == 3);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    CHECK(sink.length == 16 && memcmp(sink.data, "{\"a\":1}\nnull\n[]\n", 16) == 0);
    CHECK(w.written == 16);

    // A failed write is reported at the end of a top-level value, and what
    // failed stays in the buffer for the next flush.
    sink.fail = 1;
    CHECK(sof_json_writer_bool(&w, 1) == SOF_JSON_WRITE);
    CHECK(output_is(&w, "true\n") && w.values == 4);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_WRITE);
    CHECK(sof_json_writer_flush(&w) == SOF_JSON_WRITE);
    CHECK(output_is(&w, "true\n[]\n") && w.written == 16);
    sink.fail = 0;
    CHECK(sof_json_writer_flush(&w) == SOF_JSON_OK);
    CHECK(output_is(&w, "") && w.written == 24);
    CHECK(sink.length == 24 && memcmp(sink.data + 16, "true\n[]\n", 8) == 0);
    sof_json_writer_free(&w);

    // Without a write function, flushing does nothing.
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_null(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_flush(&w) == SOF_JSON_OK);
    CHECK(output_is(&w, "null\n") && w.written == 0);
    sof_json_writer_free(&w);
}

static void test_write_large(void) {
    // A value larger than SOF_JSON_FLUSH_SIZE is passed on in pieces, and
    // the pieces join up to what a writer without a write function holds.
    static sink_t sink;
    sof_json_writer_t w, whole;
    memset(&sink, 0, sizeof(sink));
    CHECK(sof_json_writer_init(&w, 1, 0, sink_write, &sink, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_init(&whole, 1, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&whole) == SOF_JSON_OK);
    for (int i = 0; i < 2000; i++) {
        CHECK(sof_json_writer_string(&w, "element") == SOF_JSON_OK);
        CHECK(sof_json_writer_string(&whole, "element") == SOF_JSON_OK);
        CHECK(w.out.length < SOF_JSON_FLUSH_SIZE);
    }
    CHECK(sink.calls > 2);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_end(&whole) == SOF_JSON_OK);
    CHECK(output_is(&w, "") && w.written == sink.length);
    CHECK(sink.length == whole.out.length && memcmp(sink.data, whole.out.data, sink.length) == 0);

    // A failed write in the middle of a value isn't reported; the output is
    // kept, and reported at the end of the value.
    memset(&sink, 0, sizeof(sink));
    sink.fail = 1;
    sof_json_writer_clear(&w);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    for (int i = 0; i < 2000; i++) {
        CHECK(sof_json_writer_string(&w, "element") == SOF_JSON_OK);
    }
    CHECK(sink.calls > 2 && w.out.length > SOF_JSON_FLUSH_SIZE);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_WRITE);
    sink.fail = 0;
    CHECK(sof_json_writer_flush(&w) == SOF_JSON_OK);
    CHECK(sink.length == whole.out.length && memcmp(sink.data, whole.out.data, sink.length) == 0);
    sof_json_writer_free(&w);
    sof_json_writer_free(&whole);
}

static void test_clear(void) {
    static sink_t sink;
    sof_json_writer_t w;
    memset(&sink, 0, sizeof(sink));
    CHECK(sof_json_writer_init(&w, 2, SOF_JSON_ASCII, sink_write, &sink, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_null(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_key(&w, "a") == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    size_t calls = sink.calls;
    CHECK(sof_json_writer_clear(&w) == SOF_JSON_OK);
    // Nothing is passed on, everything is closed, and the settings stay.
    CHECK(sink.calls == calls);
    CHECK(output_is(&w, "") && sof_json_writer_depth(&w) == 0);
    CHECK(w.values == 0 && w.written == 0 && !sof_json_writer_complete(&w));
    CHECK(w.indent == 2 && w.flags == SOF_JSON_ASCII && w.write == sink_write && w.ctx == &sink);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_string(&w, "\xc3\xa9") == SOF_JSON_OK);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    CHECK(sink.length == 20 && memcmp(sink.data, "null\n[\n  \"\\u00e9\"\n]\n", 20) == 0);
    sof_json_writer_free(&w);
}

static void test_free(void) {
    sof_json_writer_t w;
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_free(&w) == SOF_JSON_OK);
    CHECK(w.ready == 0 && w.out.data == NULL && w.open.elements == NULL);
    // Freeing again does nothing.
    CHECK(sof_json_writer_free(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_free(NULL) == SOF_JSON_ERROR);
    // It can be initialized again.
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_null(&w) == SOF_JSON_OK);
    CHECK(output_is(&w, "null\n"));
    sof_json_writer_free(&w);
}

static void test_null_and_dead(void) {
    sof_json_writer_t dead;
    CHECK(sof_json_writer_init(&dead, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    sof_json_writer_free(&dead);
    sof_json_writer_t *writers[] = {NULL, &dead};
    for (size_t i = 0; i < LENGTH(writers); i++) {
        sof_json_writer_t *w = writers[i];
        size_t length = 99;
        CHECK(sof_json_writer_array_begin(w) == SOF_JSON_ERROR);
        CHECK(sof_json_writer_array_end(w) == SOF_JSON_ERROR);
        CHECK(sof_json_writer_bool(w, 1) == SOF_JSON_ERROR);
        CHECK(sof_json_writer_clear(w) == SOF_JSON_ERROR);
        CHECK(sof_json_writer_complete(w) == 0);
        CHECK(sof_json_writer_depth(w) == 0);
        CHECK(sof_json_writer_double(w, 1.0) == SOF_JSON_ERROR);
        CHECK(sof_json_writer_flush(w) == SOF_JSON_ERROR);
        CHECK(sof_json_writer_int(w, 1) == SOF_JSON_ERROR);
        CHECK(sof_json_writer_key(w, "k") == SOF_JSON_ERROR);
        CHECK(sof_json_writer_key_bytes(w, "k", 1) == SOF_JSON_ERROR);
        CHECK(sof_json_writer_null(w) == SOF_JSON_ERROR);
        CHECK(sof_json_writer_object_begin(w) == SOF_JSON_ERROR);
        CHECK(sof_json_writer_object_end(w) == SOF_JSON_ERROR);
        CHECK(sof_json_writer_output(w, &length) == NULL && length == 0);
        CHECK(sof_json_writer_output(w, NULL) == NULL);
        CHECK(sof_json_writer_raw(w, "1", 1) == SOF_JSON_ERROR);
        CHECK(sof_json_writer_string(w, "s") == SOF_JSON_ERROR);
        CHECK(sof_json_writer_string_bytes(w, "s", 1) == SOF_JSON_ERROR);
        CHECK(sof_json_writer_uint(w, 1) == SOF_JSON_ERROR);
    }
    CHECK(sof_json_writer_free(&dead) == SOF_JSON_OK);
    CHECK(dead.ready == 0);
}

static void test_random_documents(void) {
    // The same random documents, written compact, indented and as ASCII, are
    // valid JSON Lines and valid UTF-8; the indented output, less its
    // whitespace, is the compact output; and the ASCII output is ASCII.
    sof_json_writer_t ws[3];
    uint64_t state = 0xbb67ae8584caa73bULL;
    CHECK(sof_json_writer_init(&ws[0], 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_init(&ws[1], 4, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_init(&ws[2], 0, SOF_JSON_ASCII, NULL, NULL, NULL) == SOF_JSON_OK);
    for (int i = 0; i < RANDOM_DOCUMENTS; i++) {
        CHECK(random_value(ws, LENGTH(ws), &state, 0));
        for (size_t k = 0; k < LENGTH(ws); k++) {
            CHECK(sof_json_writer_complete(&ws[k]) && sof_json_writer_depth(&ws[k]) == 0);
        }
    }
    for (size_t k = 0; k < LENGTH(ws); k++) {
        CHECK(ws[k].values == RANDOM_DOCUMENTS);
        CHECK(test_json_lines(ws[k].out.data, ws[k].out.length) == RANDOM_DOCUMENTS);
        CHECK(sof_utf8_validate(ws[k].out.data, ws[k].out.length) == ws[k].out.length);
    }
    for (size_t i = 0; i < ws[2].out.length; i++) {
        CHECK((unsigned char)ws[2].out.data[i] < 0x80);
    }
    char *stripped = malloc(ws[1].out.length + 1);
    CHECK(stripped != NULL);
    // The compact output's top-level newlines go too.
    size_t n = strip_space(ws[1].out.data, ws[1].out.length, stripped);
    char *compact = malloc(ws[0].out.length + 1);
    size_t m = compact != NULL ? strip_space(ws[0].out.data, ws[0].out.length, compact) : 0;
    int same = compact != NULL && n == m && memcmp(stripped, compact, n) == 0;
    free(stripped);
    free(compact);
    CHECK(same);
    for (size_t k = 0; k < LENGTH(ws); k++) {
        sof_json_writer_free(&ws[k]);
    }
}

static void test_no_memory(void) {
    // Fail each allocation in turn until a run makes none fail. A call that
    // fails must return SOF_JSON_NO_MEM and change nothing, so making it
    // again goes on as if it hadn't failed, and the document comes out
    // whole. Nothing may leak.
    static const step_t steps[] = {
        {'{', ""},   {'k', "name"}, {'s', NULL},   {'k', "list"}, {'[', ""},   {'[', ""},   {'[', ""},  {'[', ""},
        {'[', ""},   {'[', ""},     {'[', ""},     {'[', ""},     {'[', ""},   {'[', ""},   {'[', ""},  {'[', ""},
        {'[', ""},   {'[', ""},     {'[', ""},     {'[', ""},     {'[', ""},   {'[', ""},   {'i', "-7"}, {'d', "0.25"},
        {'s', NULL}, {']', ""},     {']', ""},     {']', ""},     {']', ""},   {']', ""},   {']', ""},  {']', ""},
        {']', ""},   {']', ""},     {']', ""},     {']', ""},     {']', ""},   {']', ""},   {']', ""},  {']', ""},
        {']', ""},   {']', ""},     {'{', ""},     {'k', NULL},   {'n', ""},   {'}', ""},   {']', ""},  {'}', ""},
        {'s', NULL}, {'n', ""},
    };
    char long_string[600];
    for (size_t i = 0; i < sizeof(long_string) - 1; i++) {
        long_string[i] = "abc\n\xc3\xa9"[i % 6];
    }
    long_string[sizeof(long_string) - 1] = '\0';

    sof_json_writer_t expected;
    CHECK(sof_json_writer_init(&expected, 2, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    for (size_t i = 0; i < LENGTH(steps); i++) {
        CHECK(step_run(&expected, &steps[i], long_string) == SOF_JSON_OK);
    }
    CHECK(test_json_lines(expected.out.data, expected.out.length) == 3);

    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_json_writer_t w;
        int failed = 0;
        sof_json_error_t rc = sof_json_writer_init(&w, 2, 0, NULL, NULL, &alloc);
        CHECK(rc == SOF_JSON_OK || (rc == SOF_JSON_NO_MEM && w.ready == 0));
        if (rc != SOF_JSON_OK) {
            failed = 1;
            rc = sof_json_writer_init(&w, 2, 0, NULL, NULL, &alloc);
            CHECK(rc == SOF_JSON_OK);
        }
        for (size_t i = 0; i < LENGTH(steps); i++) {
            size_t length = w.out.length, depth = sof_json_writer_depth(&w), values = w.values;
            rc = step_run(&w, &steps[i], long_string);
            if (rc == SOF_JSON_NO_MEM) {
                failed = 1;
                CHECK(w.out.length == length && sof_json_writer_depth(&w) == depth && w.values == values);
                CHECK(memcmp(w.out.data, expected.out.data, length) == 0);
                rc = step_run(&w, &steps[i], long_string);
            }
            CHECK(rc == SOF_JSON_OK);
        }
        CHECK(output_is_bytes(&w, expected.out.data, expected.out.length));
        // Only the chosen allocation fails.
        CHECK(failed == (state.allocations >= n));
        sof_json_writer_free(&w);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            break;
        }
    }
    sof_json_writer_free(&expected);
}

/// @brief Where JSONTestSuite's parsing tests are, from the library's
/// directory.
#define SUITE_DIR "../test-data/JSONTestSuite/test_parsing"

/// @brief The code points written at a time by the every-code-point test.
#define CODE_POINT_CHUNK 4096

/// @brief Writes the value a canonical form from test_json_text() holds.
/// @details Numbers are written with sof_json_writer_raw(), so that their
/// text is kept, and strings with sof_json_writer_string_bytes() from their
/// decoded bytes, so that the writer escapes them itself.
/// @return 1, or 0 if a call failed.
static int canonical_write(sof_json_writer_t *w, const char *canon, size_t length) {
    int is_object[TEST_JSON_DEPTH + 1] = {0};
    int expect_key[TEST_JSON_DEPTH + 1] = {0};
    int depth = 0;
    size_t i = 0;
    while (i < length) {
        char c = canon[i++];
        sof_json_error_t rc;
        int value_done = 1;
        if (c == 's') {
            char hex[9];
            memcpy(hex, canon + i, 8);
            hex[8] = '\0';
            size_t n = (size_t)strtoul(hex, NULL, 16);
            i += 8;
            if (depth > 0 && is_object[depth] && expect_key[depth]) {
                rc = sof_json_writer_key_bytes(w, canon + i, n);
                expect_key[depth] = 0;
                value_done = 0;
            } else {
                rc = sof_json_writer_string_bytes(w, canon + i, n);
            }
            i += n;
        } else if (c == '{' || c == '[') {
            rc = c == '{' ? sof_json_writer_object_begin(w) : sof_json_writer_array_begin(w);
            depth++;
            is_object[depth] = c == '{';
            expect_key[depth] = 1;
            value_done = 0;
        } else if (c == '}' || c == ']') {
            rc = c == '}' ? sof_json_writer_object_end(w) : sof_json_writer_array_end(w);
            depth--;
        } else if (c == 'n') {
            const char *end = memchr(canon + i, ';', length - i);
            if (end == NULL) {
                return 0;
            }
            rc = sof_json_writer_raw(w, canon + i, (size_t)(end - (canon + i)));
            i = (size_t)(end - canon) + 1;
        } else {
            rc = c == 'z' ? sof_json_writer_null(w) : sof_json_writer_bool(w, c == 't');
        }
        if (rc != SOF_JSON_OK) {
            return 0;
        }
        if (value_done && depth > 0 && is_object[depth]) {
            expect_key[depth] = 1;
        }
    }
    return 1;
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
            char *grown = realloc(data, size);
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
    *length = n;
    return data;
}

static void test_suite_checker(void) {
    // The tests' checker, which the rest of these tests rely on, accepts
    // every text JSONTestSuite says a parser must accept (y_), and refuses
    // every one it must refuse (n_). Those it leaves to the parser (i_) are
    // checked without a verdict, so that none crashes it.
    DIR *dir = opendir(SUITE_DIR);
    struct dirent *entry;
    int counts[3] = {0}, wrong = 0;
    CHECK(dir != NULL);
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        size_t name_length = strlen(name), length;
        char path[512];
        if (name_length < 7 || strcmp(name + name_length - 5, ".json") != 0 || name[1] != '_') {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", SUITE_DIR, name);
        char *data = file_read(path, &length);
        if (data == NULL) {
            printf("(can't read %s) ", path);
            wrong++;
            continue;
        }
        int ok = test_json_text(data, length, NULL, 0, NULL);
        free(data);
        if (name[0] == 'y') {
            counts[0]++;
        } else if (name[0] == 'n') {
            counts[1]++;
        } else {
            counts[2]++;
            continue;
        }
        if (ok != (name[0] == 'y')) {
            printf("(%s %s) ", name, ok ? "accepted" : "refused");
            wrong++;
        }
    }
    closedir(dir);
    CHECK(counts[0] == 95 && counts[1] == 188 && counts[2] == 35);
    CHECK(wrong == 0);
}

static void test_suite_rewritten(void) {
    // Every text a parser must accept (y_), decoded and written again by a
    // writer, compact, indented and as ASCII, is valid JSON that decodes to
    // the same values, strings and names.
    DIR *dir = opendir(SUITE_DIR);
    struct dirent *entry;
    int files = 0, wrong = 0;
    CHECK(dir != NULL);
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        char path[512];
        size_t length, canon_length, again_length;
        if (name[0] != 'y' || name[1] != '_') {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", SUITE_DIR, name);
        char *data = file_read(path, &length);
        size_t size = 16 * length + 64;
        char *canon = malloc(size), *again = malloc(size);
        int ok = data != NULL && canon != NULL && again != NULL && test_json_text(data, length, canon, size, &canon_length);
        static const struct {
            size_t indent;
            unsigned flags;
        } settings[] = {{0, 0}, {2, 0}, {0, SOF_JSON_ASCII}};
        for (size_t k = 0; ok && k < LENGTH(settings); k++) {
            sof_json_writer_t w;
            ok = sof_json_writer_init(&w, settings[k].indent, settings[k].flags, NULL, NULL, NULL) == SOF_JSON_OK && canonical_write(&w, canon, canon_length) && sof_json_writer_complete(&w) && test_json_lines(w.out.data, w.out.length) == 1 && test_json_text(w.out.data, w.out.length, again, size, &again_length) && again_length == canon_length && memcmp(again, canon, canon_length) == 0;
            if (ok && (settings[k].flags & SOF_JSON_ASCII) != 0) {
                for (size_t i = 0; i < w.out.length; i++) {
                    ok = ok && (unsigned char)w.out.data[i] < 0x80;
                }
            }
            sof_json_writer_free(&w);
        }
        if (!ok) {
            printf("(%s) ", name);
            wrong++;
        }
        files++;
        free(data);
        free(canon);
        free(again);
    }
    closedir(dir);
    CHECK(files == 95);
    CHECK(wrong == 0);
}

static void test_every_code_point(void) {
    // RFC 8259, section 7: any character may be in a string, but a quote, a
    // reverse solidus and U+0000 to U+001F must be escaped. Every code point
    // is written, a few thousand to a string, compact and as ASCII, and the
    // output is a JSON string that decodes to the same code points.
    static char text[CODE_POINT_CHUNK * 4];
    static char canon[CODE_POINT_CHUNK * 4 + 32], expected[CODE_POINT_CHUNK * 4 + 32];
    sof_json_writer_t ws[2];
    CHECK(sof_json_writer_init(&ws[0], 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_init(&ws[1], 0, SOF_JSON_ASCII, NULL, NULL, NULL) == SOF_JSON_OK);
    for (uint32_t start = 0; start <= SOF_UTF_MAX; start += CODE_POINT_CHUNK) {
        size_t n = 0, canon_length;
        for (uint32_t code = start; code < start + CODE_POINT_CHUNK && code <= SOF_UTF_MAX; code++) {
            n += sof_utf8_encode(code, text + n, 4);
        }
        int length = snprintf(expected, sizeof(expected), "s%08zx", n);
        memcpy(expected + length, text, n);
        for (size_t k = 0; k < LENGTH(ws); k++) {
            sof_json_writer_clear(&ws[k]);
            CHECK(sof_json_writer_string_bytes(&ws[k], text, n) == SOF_JSON_OK);
            CHECK(test_json_text(ws[k].out.data, ws[k].out.length, canon, sizeof(canon), &canon_length));
            CHECK(canon_length == (size_t)length + n && memcmp(canon, expected, canon_length) == 0);
        }
        for (size_t i = 0; i < ws[1].out.length; i++) {
            CHECK((unsigned char)ws[1].out.data[i] < 0x80);
        }
    }
    sof_json_writer_free(&ws[0]);
    sof_json_writer_free(&ws[1]);
}

static void test_reader_tokens(void) {
    // The sample's tokens, read whole and in pieces of every size, with
    // their depth and place.
    static const token_row_t rows[] = {
        {SOF_JSON_OBJECT, "", 0, 1, 1, 1},       {SOF_JSON_KEY, "a", 0, 1, 1, 2},        {SOF_JSON_ARRAY, "", 0, 2, 1, 7},
        {SOF_JSON_NUMBER, "1", 0, 2, 1, 8},      {SOF_JSON_NUMBER, "-2.5e3", 0, 2, 1, 11}, {SOF_JSON_BOOL, "", 1, 2, 1, 19},
        {SOF_JSON_BOOL, "", 0, 2, 1, 25},        {SOF_JSON_NULL, "", 0, 2, 1, 32},       {SOF_JSON_ARRAY_END, "", 0, 1, 1, 36},
        {SOF_JSON_KEY, "b", 0, 1, 2, 2},         {SOF_JSON_OBJECT, "", 0, 2, 2, 7},      {SOF_JSON_KEY, "c", 0, 2, 2, 8},
        {SOF_JSON_STRING, "d", 0, 2, 2, 13},     {SOF_JSON_OBJECT_END, "", 0, 1, 2, 16}, {SOF_JSON_KEY, "", 0, 1, 2, 19},
        {SOF_JSON_ARRAY, "", 0, 2, 2, 23},       {SOF_JSON_ARRAY_END, "", 0, 1, 2, 24},  {SOF_JSON_OBJECT_END, "", 0, 0, 2, 25},
    };
    static const size_t sizes[] = {0, 1, 2, 3, 5, 8, 13, 4096};
    for (size_t c = 0; c < LENGTH(sizes); c++) {
        sof_json_reader_t r;
        chunks_t chunks;
        sof_json_token_t t;
        CHECK(reader_open(&r, sample_text, sizeof(sample_text) - 1, 0, sizes[c], &chunks, NULL));
        CHECK(r.values == 0 && sof_json_reader_depth(&r) == 0);
        for (size_t i = 0; i < LENGTH(rows); i++) {
            CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK);
            CHECK(t.type == rows[i].type && t.depth == rows[i].depth && sof_json_reader_depth(&r) == rows[i].depth);
            CHECK(t.length == strlen(rows[i].text) && strcmp(t.text, rows[i].text) == 0 && t.boolean == rows[i].boolean);
            CHECK(r.line == rows[i].line && r.column == rows[i].column);
            CHECK(r.offset == (rows[i].line == 1 ? 0 : 38) + rows[i].column - 1);
        }
        CHECK(r.values == 1);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_NO_MATCH && t.type == SOF_JSON_NONE);
        // After the end, the read function isn't called again.
        size_t calls = chunks.calls;
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_NO_MATCH);
        CHECK(chunks.calls == calls);
        // A NULL token is fine.
        CHECK(sof_json_reader_clear(&r) == SOF_JSON_OK || sizes[c] != 0);
        if (sizes[c] == 0) {
            CHECK(sof_json_reader_next(&r, NULL) == SOF_JSON_OK && r.last == SOF_JSON_OBJECT);
        }
        CHECK(sof_json_reader_free(&r) == SOF_JSON_OK);
    }
}

static void test_reader_peek(void) {
    // Peeking before each token gives the type it has, twice over, and
    // changes nothing that reading it sees.
    static const size_t sizes[] = {0, 1, 3};
    for (size_t c = 0; c < LENGTH(sizes); c++) {
        sof_json_reader_t r;
        chunks_t chunks;
        sof_json_token_t t;
        sof_json_type_t type, again;
        sof_json_error_t rc;
        size_t tokens = 0;
        CHECK(reader_open(&r, sample_text, sizeof(sample_text) - 1, 0, sizes[c], &chunks, NULL));
        while ((rc = sof_json_reader_peek(&r, &type)) == SOF_JSON_OK) {
            CHECK(sof_json_reader_peek(&r, &again) == SOF_JSON_OK && again == type);
            CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == type);
            tokens++;
        }
        CHECK(rc == SOF_JSON_NO_MATCH && type == SOF_JSON_NONE && tokens == 18);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_NO_MATCH);
        sof_json_reader_free(&r);
    }
    // A byte that can't begin a token is found by peeking; a token that
    // begins well but goes on wrong, by reading it.
    sof_json_reader_t r;
    sof_json_token_t t;
    sof_json_type_t type;
    CHECK(sof_json_reader_init_bytes(&r, 0, "[x]", 3, NULL) == SOF_JSON_OK);
    CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK);
    CHECK(sof_json_reader_peek(&r, &type) == SOF_JSON_SYNTAX && type == SOF_JSON_NONE && r.column == 2);
    CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_SYNTAX);
    sof_json_reader_free(&r);
    CHECK(sof_json_reader_init_bytes(&r, 0, "[tru]", 5, NULL) == SOF_JSON_OK);
    CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK);
    CHECK(sof_json_reader_peek(&r, &type) == SOF_JSON_OK && type == SOF_JSON_BOOL);
    CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_SYNTAX && r.column == 5);
    CHECK(sof_json_reader_peek(&r, &type) == SOF_JSON_SYNTAX && type == SOF_JSON_NONE);
    sof_json_reader_free(&r);
    // Values one after another, and a NULL type.
    CHECK(sof_json_reader_init_bytes(&r, SOF_JSON_LINES, "-1 \"a\" null", 11, NULL) == SOF_JSON_OK);
    CHECK(sof_json_reader_peek(&r, NULL) == SOF_JSON_OK && sof_json_reader_next(&r, &t) == SOF_JSON_OK);
    CHECK(sof_json_reader_peek(&r, &type) == SOF_JSON_OK && type == SOF_JSON_STRING && sof_json_reader_next(&r, &t) == SOF_JSON_OK);
    CHECK(sof_json_reader_peek(&r, &type) == SOF_JSON_OK && type == SOF_JSON_NULL && sof_json_reader_next(&r, &t) == SOF_JSON_OK);
    CHECK(sof_json_reader_peek(&r, &type) == SOF_JSON_NO_MATCH && type == SOF_JSON_NONE);
    sof_json_reader_free(&r);
    CHECK(sof_json_reader_peek(NULL, &type) == SOF_JSON_ERROR && type == SOF_JSON_NONE);
    CHECK(sof_json_reader_peek(&r, &type) == SOF_JSON_ERROR);
}

static void test_reader_strings(void) {
    CHECK(string_is("\"\"", "", 0));
    CHECK(string_is("\"abc\"", "abc", 3));
    CHECK(string_is("\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"", "\"\\/\b\f\n\r\t", 8));
    CHECK(string_is("\"\\u0041\\u00e9\\u20AC\"", "A\xc3\xa9\xe2\x82\xac", 6));
    CHECK(string_is("\"\\ud83d\\ude00\\uD83D\\uDE00\"", "\xf0\x9f\x98\x80\xf0\x9f\x98\x80", 8));
    CHECK(string_is("\"\\u0000x\"", "\0x", 2));
    CHECK(string_is("\"caf\xc3\xa9 \xf0\x9f\x98\x80\"", "caf\xc3\xa9 \xf0\x9f\x98\x80", 10));
    // Half a surrogate pair is U+FFFD, and what follows it is read as usual.
    CHECK(string_is("\"\\ud800\"", "\xef\xbf\xbd", 3));
    CHECK(string_is("\"\\udc00\\ud800x\"", "\xef\xbf\xbd\xef\xbf\xbdx", 7));
    CHECK(string_is("\"\\ud800\\u0041\"", "\xef\xbf\xbd" "A", 4));
    CHECK(string_is("\"\\ud800\\n\"", "\xef\xbf\xbd\n", 4));
    CHECK(string_is("\"\\ud800u\"", "\xef\xbf\xbdu", 4));
    CHECK(string_is("\"\\ud800\\ud800\\udc00\"", "\xef\xbf\xbd\xf0\x90\x80\x80", 7));

    // Strings that aren't strings, and where the error is.
    CHECK(error_at("\"abc", 0, SOF_JSON_SYNTAX, 1, 5));
    CHECK(error_at("\"a\x01\"", 0, SOF_JSON_SYNTAX, 1, 3));
    CHECK(error_at("\"a\nb\"", 0, SOF_JSON_SYNTAX, 1, 3));
    CHECK(error_at("\"a\\x\"", 0, SOF_JSON_SYNTAX, 1, 3));
    CHECK(error_at("\"\\u12G4\"", 0, SOF_JSON_SYNTAX, 1, 2));
    CHECK(error_at("\"\\u123\"", 0, SOF_JSON_SYNTAX, 1, 2));
    CHECK(error_at("\"\\u12", 0, SOF_JSON_SYNTAX, 1, 2));
    CHECK(error_at("\"\\", 0, SOF_JSON_SYNTAX, 1, 2));
    CHECK(error_at("\"\xc3\"", 0, SOF_JSON_SYNTAX, 1, 2));
    CHECK(error_at("\"\xed\xa0\x80\"", 0, SOF_JSON_SYNTAX, 1, 2));
    CHECK(error_at("\"\xff\"", 0, SOF_JSON_SYNTAX, 1, 2));
    CHECK(error_at("\"\xc3", 0, SOF_JSON_SYNTAX, 1, 2));
    CHECK(error_at("\"\xf0\x9f\x98", 0, SOF_JSON_SYNTAX, 1, 2));
    CHECK(error_at("[\"a\", \"\xc0\xaf\"]", 0, SOF_JSON_SYNTAX, 1, 8));
}

static void test_reader_numbers(void) {
    static const char *valid[] = {"0",    "-0",      "1",       "-1",      "123", "1.5", "-0.25", "1e5", "1E+5",
                                  "1e-5", "1.25e10", "0e0", "-0.0e-0", "123456789012345678901234567890"};
    for (size_t i = 0; i < LENGTH(valid); i++) {
        for (size_t chunk = 0; chunk < 2; chunk++) {
            sof_json_reader_t r;
            chunks_t chunks;
            sof_json_token_t t;
            CHECK(reader_open(&r, valid[i], strlen(valid[i]), 0, chunk, &chunks, NULL));
            CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_NUMBER && strcmp(t.text, valid[i]) == 0);
            CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_NO_MATCH);
            sof_json_reader_free(&r);
        }
    }
    static const struct {
        const char *text;
        size_t column;
    } invalid[] = {
        {"01", 2},   {"-", 2},    {"1.", 3},       {".5", 1},     {"+1", 1},   {"1e", 3},  {"1e+", 4},   {"-a", 2},    {"1x", 2},
        {"0x10", 2}, {"1.5.2", 4}, {"--1", 2},     {"1e5e5", 4},  {"[1,01]", 5}, {"-01", 3}, {"Infinity", 1}, {"NaN", 1}, {"tru", 4},
        {"trux", 4}, {"truex", 5}, {"nul", 4},     {"True", 1},   {"[true1]", 6}, {"[nullx]", 6}, {"1true", 2},
    };
    for (size_t i = 0; i < LENGTH(invalid); i++) {
        CHECK(error_at(invalid[i].text, 0, SOF_JSON_SYNTAX, 1, invalid[i].column));
    }
}

static void test_number_conversion(void) {
    static const struct {
        const char *text;
        sof_json_error_t rc;
        double value;
    } doubles[] = {
        {"0", SOF_JSON_OK, 0},
        {"0.1", SOF_JSON_OK, 0.1},
        {"-2.5", SOF_JSON_OK, -2.5},
        {"1E2", SOF_JSON_OK, 100},
        {"0.000001", SOF_JSON_OK, 1e-6},
        {"1e400", SOF_JSON_RANGE, HUGE_VAL},
        {"-1e400", SOF_JSON_RANGE, -HUGE_VAL},
        {"1e-400", SOF_JSON_OK, 0},
        {"4.9e-324", SOF_JSON_OK, 4.9e-324},
        {"2.2250738585072014e-308", SOF_JSON_OK, 2.2250738585072014e-308},
        {"1.7976931348623157e308", SOF_JSON_OK, 1.7976931348623157e308},
        {"1.7976931348623159e308", SOF_JSON_RANGE, HUGE_VAL},
        {"123456789012345678901234567890", SOF_JSON_OK, 1.2345678901234568e29},
        {"0.0000000000000000000000000000001e31", SOF_JSON_OK, 1},
        {"100e-2", SOF_JSON_OK, 1},
        {"9007199254740993", SOF_JSON_OK, 9007199254740992.0},
        {"123456789e999999999999999999999", SOF_JSON_RANGE, HUGE_VAL},
        {"0e999999999999", SOF_JSON_OK, 0},
        {"1e-99999999999999999999", SOF_JSON_OK, 0},
    };
    for (size_t i = 0; i < LENGTH(doubles); i++) {
        double d = 12345;
        CHECK(sof_json_number_double(doubles[i].text, strlen(doubles[i].text), &d) == doubles[i].rc);
        CHECK(d == doubles[i].value);
    }
    double d = 0;
    CHECK(sof_json_number_double("-0", 2, &d) == SOF_JSON_OK && d == 0 && signbit(d));
    CHECK(sof_json_number_double("-0.0e5", 6, &d) == SOF_JSON_OK && d == 0 && signbit(d));
    CHECK(sof_json_number_double("0", 1, &d) == SOF_JSON_OK && d == 0 && !signbit(d));
    CHECK(sof_json_number_double("1.5x", 3, &d) == SOF_JSON_OK && d == 1.5);

    // More digits than a double needs: what is dropped still rounds. 2^53 + 1
    // is halfway between two doubles, and goes to the even one; a 1 far past
    // it makes it more than halfway.
    static char text[2048];
    int n = snprintf(text, sizeof(text), "9007199254740993.");
    memset(text + n, '0', 900);
    text[n + 900] = '\0';
    CHECK(sof_json_number_double(text, strlen(text), &d) == SOF_JSON_OK && d == 9007199254740992.0);
    text[n + 900] = '1';
    text[n + 901] = '\0';
    CHECK(sof_json_number_double(text, strlen(text), &d) == SOF_JSON_OK && d == 9007199254740994.0);
    n = snprintf(text, sizeof(text), "0.");
    memset(text + n, '0', 1000);
    snprintf(text + n + 1000, sizeof(text) - (size_t)n - 1000, "1e1001");
    CHECK(sof_json_number_double(text, strlen(text), &d) == SOF_JSON_OK && d == 1);

    static const struct {
        const char *text;
        sof_json_error_t rc;
        int64_t value;
    } ints[] = {
        {"0", SOF_JSON_OK, 0},
        {"-0", SOF_JSON_OK, 0},
        {"42", SOF_JSON_OK, 42},
        {"-42", SOF_JSON_OK, -42},
        {"9223372036854775807", SOF_JSON_OK, INT64_MAX},
        {"-9223372036854775808", SOF_JSON_OK, INT64_MIN},
        {"9223372036854775808", SOF_JSON_RANGE, 77},
        {"-9223372036854775809", SOF_JSON_RANGE, 77},
        {"1e2", SOF_JSON_OK, 100},
        {"1.0e2", SOF_JSON_OK, 100},
        {"100.00", SOF_JSON_OK, 100},
        {"100e-2", SOF_JSON_OK, 1},
        {"0.5e1", SOF_JSON_OK, 5},
        {"-1.25e2", SOF_JSON_OK, -125},
        {"1.5", SOF_JSON_RANGE, 77},
        {"1e-2", SOF_JSON_RANGE, 77},
        {"1e19", SOF_JSON_RANGE, 77},
        {"9.223372036854775807e18", SOF_JSON_OK, INT64_MAX},
        {"0.000e999999999999999999", SOF_JSON_OK, 0},
        {"12345678901234567890123", SOF_JSON_RANGE, 77},
        {"1e999999999999999999999", SOF_JSON_RANGE, 77},
        {"10000000000000000000000e-4", SOF_JSON_OK, 1000000000000000000},
    };
    for (size_t i = 0; i < LENGTH(ints); i++) {
        int64_t v = 77;
        CHECK(sof_json_number_int(ints[i].text, strlen(ints[i].text), &v) == ints[i].rc);
        CHECK(v == ints[i].value);
    }
    static const struct {
        const char *text;
        sof_json_error_t rc;
        uint64_t value;
    } uints[] = {
        {"0", SOF_JSON_OK, 0},
        {"-0", SOF_JSON_OK, 0},
        {"18446744073709551615", SOF_JSON_OK, UINT64_MAX},
        {"18446744073709551616", SOF_JSON_RANGE, 77},
        {"-1", SOF_JSON_RANGE, 77},
        {"1.8446744073709551615e19", SOF_JSON_OK, UINT64_MAX},
        {"1.8446744073709551616e19", SOF_JSON_RANGE, 77},
    };
    for (size_t i = 0; i < LENGTH(uints); i++) {
        uint64_t v = 77;
        CHECK(sof_json_number_uint(uints[i].text, strlen(uints[i].text), &v) == uints[i].rc);
        CHECK(v == uints[i].value);
    }
    int64_t i64 = 0;
    uint64_t u64 = 0;
    CHECK(sof_json_number_int("123456", 3, &i64) == SOF_JSON_OK && i64 == 123);

    // Text that isn't a number, and NULL.
    static const char *bad[] = {"", "abc", "01", "1 ", " 1", "+1", "1.", "-", ".5", "1e", "0x1", "1e+", "--1"};
    for (size_t i = 0; i < LENGTH(bad); i++) {
        size_t length = strlen(bad[i]);
        CHECK(sof_json_number_double(bad[i], length, &d) == SOF_JSON_SYNTAX);
        CHECK(sof_json_number_int(bad[i], length, &i64) == SOF_JSON_SYNTAX);
        CHECK(sof_json_number_uint(bad[i], length, &u64) == SOF_JSON_SYNTAX);
    }
    CHECK(sof_json_number_double(NULL, 0, &d) == SOF_JSON_ERROR);
    CHECK(sof_json_number_double("1", 1, NULL) == SOF_JSON_ERROR);
    CHECK(sof_json_number_int(NULL, 0, &i64) == SOF_JSON_ERROR);
    CHECK(sof_json_number_int("1", 1, NULL) == SOF_JSON_ERROR);
    CHECK(sof_json_number_uint(NULL, 0, &u64) == SOF_JSON_ERROR);
    CHECK(sof_json_number_uint("1", 1, NULL) == SOF_JSON_ERROR);
}

static void test_number_doubles(void) {
    // Random doubles, written by the writer and read by the reader, convert
    // back to the same doubles.
    sof_json_writer_t w;
    uint64_t state = 0x3c6ef372fe94f82bULL;
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    for (int i = 0; i < RANDOM_DOUBLES; i++) {
        double d, back = 0;
        do {
            uint64_t bits = next_random(&state);
            memcpy(&d, &bits, sizeof(d));
        } while (!isfinite(d));
        sof_json_writer_clear(&w);
        CHECK(sof_json_writer_double(&w, d) == SOF_JSON_OK);
        sof_json_reader_t r;
        sof_json_token_t t;
        CHECK(sof_json_reader_init_bytes(&r, 0, w.out.data, w.out.length, NULL) == SOF_JSON_OK);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_NUMBER && t.length == w.out.length - 1);
        CHECK(sof_json_number_double(t.text, t.length, &back) == SOF_JSON_OK);
        sof_json_reader_free(&r);
        CHECK(memcmp(&back, &d, sizeof(d)) == 0);
    }
    sof_json_writer_free(&w);
}

static void test_number_locale(void) {
    // In a locale whose decimal point is a comma, numbers with a '.' are
    // still read.
    static const char *names[] = {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "fr_FR.utf8", "fr_FR"};
    const char *found = NULL;
    for (size_t i = 0; i < LENGTH(names) && found == NULL; i++) {
        found = setlocale(LC_NUMERIC, names[i]);
    }
    if (found == NULL) {
        printf("(no locale with a decimal comma) ");
        return;
    }
    double a = 0, b = 0, c = 0, e = 0;
    sof_json_doc_t d;
    int ok = sof_json_number_double("0.5", 3, &a) == SOF_JSON_OK && sof_json_number_double("-1234.25", 8, &b) == SOF_JSON_OK && sof_json_number_double("1.5e-7", 6, &c) == SOF_JSON_OK && sof_json_doc_init(&d, NULL) == SOF_JSON_OK && sof_json_doc_parse(&d, "[0.25]", 6) == SOF_JSON_OK && sof_json_value_double(sof_json_value_at(d.root, 0), &e) == SOF_JSON_OK;
    sof_json_doc_free(&d);
    setlocale(LC_NUMERIC, "C");
    CHECK(ok);
    CHECK(a == 0.5 && b == -1234.25 && c == 1.5e-7 && e == 0.25);
}

static void test_reader_errors(void) {
    // What isn't JSON is refused where the error is, and then again.
    CHECK(error_at("{\n  \"a\": tru\n}", 0, SOF_JSON_SYNTAX, 2, 11));
    CHECK(error_at("[1,]", 0, SOF_JSON_SYNTAX, 1, 4));
    CHECK(error_at("[1 2]", 0, SOF_JSON_SYNTAX, 1, 4));
    CHECK(error_at("{\"a\" 1}", 0, SOF_JSON_SYNTAX, 1, 6));
    CHECK(error_at("{\"a\":1,}", 0, SOF_JSON_SYNTAX, 1, 8));
    CHECK(error_at("{1:2}", 0, SOF_JSON_SYNTAX, 1, 2));
    CHECK(error_at("{\"a\":}", 0, SOF_JSON_SYNTAX, 1, 6));
    CHECK(error_at("[1}", 0, SOF_JSON_SYNTAX, 1, 3));
    CHECK(error_at("{\"a\":1]", 0, SOF_JSON_SYNTAX, 1, 7));
    CHECK(error_at("]", 0, SOF_JSON_SYNTAX, 1, 1));
    CHECK(error_at("", 0, SOF_JSON_SYNTAX, 1, 1));
    CHECK(error_at("   ", 0, SOF_JSON_SYNTAX, 1, 4));
    CHECK(error_at("\n\n", 0, SOF_JSON_SYNTAX, 3, 1));
    CHECK(error_at("[", 0, SOF_JSON_SYNTAX, 1, 2));
    CHECK(error_at("{\"a\"", 0, SOF_JSON_SYNTAX, 1, 5));
    CHECK(error_at("1 2", 0, SOF_JSON_SYNTAX, 1, 3));
    CHECK(error_at("\xef\xbb\xbf{}", 0, SOF_JSON_SYNTAX, 1, 1));
    CHECK(error_at("{\"a\":1}x", 0, SOF_JSON_SYNTAX, 1, 8));
    CHECK(error_at("[1,\n\n  x]", 0, SOF_JSON_SYNTAX, 3, 3));
    CHECK(error_at("[1,\r\n  x]", 0, SOF_JSON_SYNTAX, 2, 3));
    CHECK(error_at("\x0b[]", 0, SOF_JSON_SYNTAX, 1, 1));
    CHECK(error_at("[1]\xc2\xa0", 0, SOF_JSON_SYNTAX, 1, 4));
}

static void test_reader_depth(void) {
    // Arrays and objects nest SOF_JSON_DEPTH_MAX deep, and no deeper.
    size_t n = SOF_JSON_DEPTH_MAX;
    char *text = malloc(10 * n + 16);
    CHECK(text != NULL);
    memset(text, '[', n);
    memset(text + n, ']', n);
    text[2 * n] = '\0';
    sof_json_reader_t r;
    sof_json_token_t t;
    sof_json_error_t rc;
    size_t deepest = 0;
    CHECK(sof_json_reader_init_bytes(&r, 0, text, 2 * n, NULL) == SOF_JSON_OK);
    while ((rc = sof_json_reader_next(&r, &t)) == SOF_JSON_OK) {
        deepest = t.depth > deepest ? t.depth : deepest;
    }
    sof_json_reader_free(&r);
    CHECK(rc == SOF_JSON_NO_MATCH && deepest == n);
    sof_json_doc_t d;
    sof_json_writer_t w;
    CHECK(sof_json_doc_init(&d, NULL) == SOF_JSON_OK);
    CHECK(sof_json_doc_parse(&d, text, 2 * n) == SOF_JSON_OK);
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_value(&w, d.root) == SOF_JSON_OK);
    CHECK(w.out.length == 2 * n + 1 && memcmp(w.out.data, text, 2 * n) == 0);
    sof_json_writer_free(&w);

    memset(text, '[', n + 1);
    memset(text + n + 1, ']', n + 1);
    text[2 * n + 2] = '\0';
    CHECK(error_at(text, 0, SOF_JSON_TOO_DEEP, 1, n + 1));
    CHECK(sof_json_doc_parse(&d, text, 2 * n + 2) == SOF_JSON_TOO_DEEP && d.root == NULL && d.column == n + 1);

    size_t length = 0;
    for (size_t i = 0; i <= n; i++) {
        memcpy(text + length, "{\"a\":", 5);
        length += 5;
    }
    text[length++] = '1';
    memset(text + length, '}', n + 1);
    text[length + n + 1] = '\0';
    CHECK(error_at(text, 0, SOF_JSON_TOO_DEEP, 1, 5 * n + 1));
    sof_json_doc_free(&d);
    free(text);
}

static void test_reader_lines(void) {
    // With SOF_JSON_LINES, any number of values, one after another.
    static const char text[] = "1 2\n{\"a\":[]}\n\"x\"[]{}";
    static const char canon[] = "n1;n2;{s00000001a[]}s00000001x[]{}";
    for (size_t chunk = 0; chunk < 2; chunk++) {
        sof_json_reader_t r;
        chunks_t chunks;
        char out[64];
        size_t length;
        CHECK(reader_open(&r, text, sizeof(text) - 1, SOF_JSON_LINES, chunk, &chunks, NULL));
        CHECK(reader_canon(&r, out, sizeof(out), &length) == SOF_JSON_NO_MATCH);
        CHECK(length == sizeof(canon) - 1 && memcmp(out, canon, length) == 0);
        CHECK(r.values == 6);
        sof_json_reader_free(&r);
        // None at all.
        CHECK(reader_open(&r, "  \n ", 4, SOF_JSON_LINES, chunk, &chunks, NULL));
        CHECK(reader_canon(&r, out, sizeof(out), &length) == SOF_JSON_NO_MATCH && length == 0 && r.values == 0);
        sof_json_reader_free(&r);
        CHECK(reader_open(&r, "12", 2, SOF_JSON_LINES, chunk, &chunks, NULL));
        CHECK(reader_canon(&r, out, sizeof(out), &length) == SOF_JSON_NO_MATCH && length == 4 && memcmp(out, "n12;", 4) == 0);
        sof_json_reader_free(&r);
    }
    sof_json_reader_t r;
    sof_json_token_t t;
    CHECK(sof_json_reader_init_bytes(&r, SOF_JSON_LINES, NULL, 0, NULL) == SOF_JSON_OK);
    CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_NO_MATCH);
    sof_json_reader_free(&r);
    CHECK(error_at("1 [", SOF_JSON_LINES, SOF_JSON_SYNTAX, 1, 4));
    CHECK(error_at("1-2", SOF_JSON_LINES, SOF_JSON_SYNTAX, 1, 2));
    CHECK(error_at("[1]]", SOF_JSON_LINES, SOF_JSON_SYNTAX, 1, 4));
    CHECK(error_at("1,2", SOF_JSON_LINES, SOF_JSON_SYNTAX, 1, 2));
}

static void test_reader_pieces(void) {
    // A document with a string and a number longer than the buffer, read in
    // pieces of many sizes, gives the same tokens as read whole, and as the
    // tests' checker reads.
    static const char unit[] = "ab\\n\xc3\xa9\\u00e9\\ud83d\\ude00\"";
    size_t size = 20000;
    char *text = malloc(size), *canon = malloc(4 * size), *out = malloc(4 * size);
    CHECK(text != NULL && canon != NULL && out != NULL);
    size_t length = 0;
    memcpy(text, "[\"", 2);
    length = 2;
    while (length < 10000) {
        // The unit's last byte is a quote, escaped here.
        memcpy(text + length, unit, sizeof(unit) - 2);
        length += sizeof(unit) - 2;
        memcpy(text + length, "\\\"", 2);
        length += 2;
    }
    memcpy(text + length, "\", 1", 4);
    length += 4;
    memset(text + length, '2', 5000);
    length += 5000;
    length += (size_t)snprintf(text + length, size - length, ".5e-10, {\"k\": \"v\"}]");
    size_t canon_length, out_length;
    CHECK(test_json_text(text, length, canon, 4 * size, &canon_length));
    static const size_t sizes[] = {0, 1, 2, 3, 7, 100, 4095, 4096, 4097, 100000};
    for (size_t c = 0; c < LENGTH(sizes); c++) {
        sof_json_reader_t r;
        chunks_t chunks;
        CHECK(reader_open(&r, text, length, 0, sizes[c], &chunks, NULL));
        CHECK(reader_canon(&r, out, 4 * size, &out_length) == SOF_JSON_NO_MATCH);
        CHECK(out_length == canon_length && memcmp(out, canon, canon_length) == 0);
        sof_json_reader_free(&r);
    }
    free(text);
    free(canon);
    free(out);
}

static void test_reader_spans(void) {
    // Each token's text as written, read whole and in pieces, so that the
    // buffer is refilled in the middle of tokens.
    static const char text[] = "{\n  \"name\": \"vika\",\n  \"deps\": [ 1, -2.5e3 , true, null,\n"
                               "    {\"a\\u0041\" : \"x\\\"y\"} ],\n  \"e\": {}\n}\n";
    static const char *const tokens[] = {
        "{",     "\"name\"", "\"vika\"", "\"deps\"", "[", "1", "-2.5e3", "true", "null",
        "{",     "\"a\\u0041\"", "\"x\\\"y\"", "}", "]", "\"e\"", "{", "}", "}",
    };
    static const size_t sizes[] = {0, 1, 2, 3, 7, 4096};
    for (size_t c = 0; c < LENGTH(sizes); c++) {
        sof_json_reader_t r;
        sof_json_token_t t;
        chunks_t chunks;
        CHECK(reader_open(&r, text, strlen(text), 0, sizes[c], &chunks, NULL));
        for (size_t i = 0; i < LENGTH(tokens); i++) {
            CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK);
            CHECK(span_is(text, t.span, tokens[i]));
            CHECK(t.span.offset == r.offset);
        }
        // No token, no span.
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_NO_MATCH && t.span.offset == 0 && t.span.length == 0);
        sof_json_reader_free(&r);
    }
    // JSON Lines: offsets count from the start of the input, not of the
    // value.
    static const char lines[] = "1\n [2]\n\"s\"\n";
    static const char *const line_tokens[] = {"1", "[", "2", "]", "\"s\""};
    sof_json_reader_t r;
    sof_json_token_t t;
    chunks_t chunks;
    CHECK(reader_open(&r, lines, strlen(lines), SOF_JSON_LINES, 1, &chunks, NULL));
    for (size_t i = 0; i < LENGTH(line_tokens); i++) {
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && span_is(lines, t.span, line_tokens[i]));
    }
    CHECK(t.span.offset == 7);
    sof_json_reader_free(&r);
}

static void test_reader_read_fail(void) {
    // A read that fails is reported, changes nothing, and reading again goes
    // on, whichever read it is.
    char expected[256], out[256];
    size_t expected_length;
    sof_json_reader_t r;
    chunks_t chunks;
    CHECK(reader_open(&r, sample_text, sizeof(sample_text) - 1, 0, 0, &chunks, NULL));
    CHECK(reader_canon(&r, expected, sizeof(expected), &expected_length) == SOF_JSON_NO_MATCH);
    sof_json_reader_free(&r);
    for (size_t fail_at = 1;; fail_at++) {
        sof_json_token_t t;
        sof_json_error_t rc;
        size_t length = 0;
        int failed = 0;
        CHECK(reader_open(&r, sample_text, sizeof(sample_text) - 1, 0, 3, &chunks, NULL));
        chunks.fail_at = fail_at;
        while ((rc = sof_json_reader_next(&r, &t)) != SOF_JSON_NO_MATCH) {
            if (rc == SOF_JSON_READ) {
                CHECK(t.type == SOF_JSON_NONE);
                failed++;
                continue;
            }
            CHECK(rc == SOF_JSON_OK && token_canon(&t, out, sizeof(out), &length));
        }
        CHECK(length == expected_length && memcmp(out, expected, length) == 0);
        CHECK(failed == (chunks.calls >= fail_at));
        sof_json_reader_free(&r);
        if (!failed) {
            break;
        }
    }
}

static void test_reader_skip(void) {
    static const char text[] = "{\"a\": {\"x\": [1, {\"y\": 2}]}, \"b\": [3, 4], \"c\": 5, \"d\": \"e\"}";
    for (size_t chunk = 0; chunk < 3; chunk++) {
        sof_json_reader_t r;
        chunks_t chunks;
        sof_json_token_t t;
        CHECK(reader_open(&r, text, sizeof(text) - 1, 0, chunk, &chunks, NULL));
        // Before any token, there is nothing to skip.
        CHECK(sof_json_reader_skip(&r) == SOF_JSON_OK);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_OBJECT);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_KEY && strcmp(t.text, "a") == 0);
        CHECK(sof_json_reader_skip(&r) == SOF_JSON_OK && sof_json_reader_depth(&r) == 1);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_KEY && strcmp(t.text, "b") == 0);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_ARRAY);
        CHECK(sof_json_reader_skip(&r) == SOF_JSON_OK && sof_json_reader_depth(&r) == 1);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_KEY && strcmp(t.text, "c") == 0);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_NUMBER);
        // After a scalar, nothing.
        CHECK(sof_json_reader_skip(&r) == SOF_JSON_OK);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_KEY && strcmp(t.text, "d") == 0);
        CHECK(sof_json_reader_skip(&r) == SOF_JSON_OK);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_OBJECT_END);
        CHECK(sof_json_reader_skip(&r) == SOF_JSON_OK);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_NO_MATCH);
        sof_json_reader_free(&r);
    }
    // A whole top-level value.
    sof_json_reader_t r;
    sof_json_token_t t;
    CHECK(sof_json_reader_init_bytes(&r, 0, "[1,[2]] ", 8, NULL) == SOF_JSON_OK);
    CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_ARRAY);
    CHECK(sof_json_reader_skip(&r) == SOF_JSON_OK && r.values == 1);
    CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_NO_MATCH);
    sof_json_reader_free(&r);
    // An error while skipping is returned.
    CHECK(sof_json_reader_init_bytes(&r, 0, "[1, x]", 6, NULL) == SOF_JSON_OK);
    CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK);
    CHECK(sof_json_reader_skip(&r) == SOF_JSON_SYNTAX && r.column == 5);
    CHECK(sof_json_reader_skip(&r) == SOF_JSON_SYNTAX);
    sof_json_reader_free(&r);
    // A read failing while skipping is returned, and skipping again goes on.
    for (size_t fail_at = 1;; fail_at++) {
        chunks_t chunks;
        sof_json_error_t rc;
        int failed = 0;
        CHECK(reader_open(&r, text, sizeof(text) - 1, 0, 2, &chunks, NULL));
        chunks.fail_at = fail_at;
        for (int i = 0; i < 2; i++) {
            while ((rc = sof_json_reader_next(&r, &t)) == SOF_JSON_READ) {
                failed++;
            }
            CHECK(rc == SOF_JSON_OK);
        }
        CHECK(t.type == SOF_JSON_KEY);
        while ((rc = sof_json_reader_skip(&r)) == SOF_JSON_READ) {
            failed++;
        }
        CHECK(rc == SOF_JSON_OK);
        while ((rc = sof_json_reader_next(&r, &t)) == SOF_JSON_READ) {
            failed++;
        }
        CHECK(rc == SOF_JSON_OK && t.type == SOF_JSON_KEY && strcmp(t.text, "b") == 0);
        sof_json_reader_free(&r);
        if (!failed) {
            break;
        }
        CHECK(failed == 1);
    }
}

static void test_reader_clear(void) {
    char expected[256], out[256];
    size_t expected_length, length;
    sof_json_reader_t r;
    sof_json_token_t t;
    chunks_t chunks;
    // Reading bytes, a reader that is cleared starts again at the first.
    CHECK(reader_open(&r, sample_text, sizeof(sample_text) - 1, 0, 0, &chunks, NULL));
    CHECK(reader_canon(&r, expected, sizeof(expected), &expected_length) == SOF_JSON_NO_MATCH);
    CHECK(sof_json_reader_clear(&r) == SOF_JSON_OK && r.values == 0 && r.line == 0);
    for (int i = 0; i < 3; i++) {
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK);
    }
    CHECK(sof_json_reader_clear(&r) == SOF_JSON_OK && sof_json_reader_depth(&r) == 0);
    CHECK(reader_canon(&r, out, sizeof(out), &length) == SOF_JSON_NO_MATCH);
    CHECK(length == expected_length && memcmp(out, expected, length) == 0);
    sof_json_reader_free(&r);
    // It goes on after an error.
    CHECK(sof_json_reader_init_bytes(&r, 0, "[1,x]", 5, NULL) == SOF_JSON_OK);
    CHECK(reader_canon(&r, out, sizeof(out), &length) == SOF_JSON_SYNTAX);
    CHECK(sof_json_reader_clear(&r) == SOF_JSON_OK && r.error == SOF_JSON_OK);
    CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_ARRAY);
    sof_json_reader_free(&r);
    // With a read function, it goes on with what the function gives next,
    // dropping what was read but not taken apart.
    CHECK(reader_open(&r, "[1, 2] [3]", 10, 0, 7, &chunks, NULL));
    for (int i = 0; i < 4; i++) {
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK);
    }
    CHECK(t.type == SOF_JSON_ARRAY_END && r.values == 1);
    CHECK(sof_json_reader_clear(&r) == SOF_JSON_OK && r.values == 0);
    CHECK(reader_canon(&r, out, sizeof(out), &length) == SOF_JSON_NO_MATCH);
    CHECK(length == 5 && memcmp(out, "[n3;]", 5) == 0 && r.values == 1);
    sof_json_reader_free(&r);
}

static void test_reader_null(void) {
    sof_json_reader_t r;
    sof_json_token_t t;
    chunks_t chunks = {0};
    CHECK(sof_json_reader_init(NULL, 0, chunks_read, &chunks, NULL) == SOF_JSON_ERROR);
    CHECK(sof_json_reader_init_bytes(NULL, 0, "1", 1, NULL) == SOF_JSON_ERROR);
    CHECK(sof_json_reader_init(&r, 0, NULL, NULL, NULL) == SOF_JSON_ERROR && r.ready == 0);
    CHECK(sof_json_reader_init_bytes(&r, 0, NULL, 1, NULL) == SOF_JSON_ERROR && r.ready == 0);
    // An allocator that can't free is refused, and nothing is allocated.
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    CHECK(sof_json_reader_init(&r, 0, chunks_read, &chunks, &alloc) == SOF_JSON_ERROR && r.ready == 0);
    CHECK(sof_json_reader_init_bytes(&r, 0, "1", 1, &alloc) == SOF_JSON_ERROR && r.ready == 0);
    CHECK(state.allocations == 0);

    // NULL, and freed.
    for (int k = 0; k < 2; k++) {
        sof_json_reader_t *p = k == 0 ? NULL : &r;
        if (k == 1) {
            CHECK(sof_json_reader_init_bytes(&r, 0, "[1]", 3, NULL) == SOF_JSON_OK);
            CHECK(sof_json_reader_free(&r) == SOF_JSON_OK && r.ready == 0);
        }
        t.type = SOF_JSON_NULL;
        CHECK(sof_json_reader_next(p, &t) == SOF_JSON_ERROR && t.type == SOF_JSON_NONE && strcmp(t.text, "") == 0);
        CHECK(sof_json_reader_skip(p) == SOF_JSON_ERROR);
        CHECK(sof_json_reader_clear(p) == SOF_JSON_ERROR);
        CHECK(sof_json_reader_depth(p) == 0);
        CHECK(sof_json_reader_free(p) == (k == 0 ? SOF_JSON_ERROR : SOF_JSON_OK));
    }

    // No bytes at all is no JSON.
    CHECK(sof_json_reader_init_bytes(&r, 0, NULL, 0, NULL) == SOF_JSON_OK);
    CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_SYNTAX && r.line == 1 && r.column == 1);
    sof_json_reader_free(&r);

    // sof_json_read_file().
    char buffer[8];
    size_t length = 5;
    CHECK(sof_json_read_file(buffer, sizeof(buffer), &length, NULL) == 1 && length == 0);
    FILE *f = tmpfile();
    if (f == NULL) {
        printf("(no temporary file) ");
        return;
    }
    fputs("{\"a\": [1, 2]}\n", f);
    rewind(f);
    char out[64];
    CHECK(sof_json_reader_init(&r, 0, sof_json_read_file, f, NULL) == SOF_JSON_OK);
    CHECK(reader_canon(&r, out, sizeof(out), &length) == SOF_JSON_NO_MATCH);
    CHECK(length == 20 && memcmp(out, "{s00000001a[n1;n2;]}", 20) == 0);
    sof_json_reader_free(&r);
    fclose(f);
}

static void test_reader_no_memory(void) {
    // Fail each allocation in turn, reading whole and in pieces, until a run
    // makes none fail. A call that fails must return SOF_JSON_NO_MEM and
    // change nothing, so making it again goes on as if it hadn't failed.
    // Nothing may leak.
    size_t size = 16384;
    char *text = malloc(size), *expected = malloc(4 * size), *out = malloc(4 * size);
    CHECK(text != NULL && expected != NULL && out != NULL);
    size_t length = (size_t)snprintf(text, size, "{\"s\": \"");
    while (length < 6000) {
        memcpy(text + length, "x\\ty\xc3\xa9\\u20ac", 12);
        length += 12;
    }
    length += (size_t)snprintf(text + length, size - length, "\", \"n\": -1");
    memset(text + length, '7', 300);
    length += 300;
    length += (size_t)snprintf(text + length, size - length, "e-5, \"d\": ");
    memset(text + length, '[', 40);
    length += 40;
    length += (size_t)snprintf(text + length, size - length, "true");
    memset(text + length, ']', 40);
    length += 40;
    text[length++] = '}';
    size_t expected_length, out_length;
    sof_json_reader_t r;
    chunks_t chunks;
    CHECK(reader_open(&r, text, length, 0, 0, &chunks, NULL));
    CHECK(reader_canon(&r, expected, 4 * size, &expected_length) == SOF_JSON_NO_MATCH);
    sof_json_reader_free(&r);

    static const size_t sizes[] = {0, 5};
    for (size_t c = 0; c < LENGTH(sizes); c++) {
        for (size_t n = 1;; n++) {
            test_alloc_t state = {0};
            state.fail_at = n;
            sof_alloc_t alloc = test_allocator(&state);
            sof_json_token_t t;
            sof_json_error_t rc;
            int failed = 0;
            if (!reader_open(&r, text, length, 0, sizes[c], &chunks, &alloc)) {
                CHECK(r.ready == 0);
                failed = 1;
                CHECK(reader_open(&r, text, length, 0, sizes[c], &chunks, &alloc));
            }
            out_length = 0;
            while ((rc = sof_json_reader_next(&r, &t)) != SOF_JSON_NO_MATCH) {
                if (rc == SOF_JSON_NO_MEM) {
                    CHECK(t.type == SOF_JSON_NONE);
                    failed = 1;
                    continue;
                }
                CHECK(rc == SOF_JSON_OK && token_canon(&t, out, 4 * size, &out_length));
            }
            CHECK(out_length == expected_length && memcmp(out, expected, out_length) == 0);
            // Only the chosen allocation fails.
            CHECK(failed == (state.allocations >= n));
            sof_json_reader_free(&r);
            CHECK(state.live == 0);
            CHECK(state.bad == 0);
            if (state.allocations < n) {
                break;
            }
        }
    }
    free(text);
    free(expected);
    free(out);
}

static void test_doc_values(void) {
    static const char text[] =
        "  {\"name\": \"caf\\u00e9\", \"n\": 42, \"pi\": 3.25, \"big\": 1e400, \"neg\": -7, \"t\": true, \"f\": false,\n"
        "   \"z\": null, \"list\": [1, \"two\", [3], {}], \"nested\": {\"k\": \"v\"}, \"nul\": \"a\\u0000b\", \"empty\": \"\"}\n";
    static const char *keys[] = {"name", "n", "pi", "big", "neg", "t", "f", "z", "list", "nested", "nul", "empty"};
    sof_json_doc_t d;
    CHECK(sof_json_doc_init(&d, NULL) == SOF_JSON_OK && d.root == NULL);
    CHECK(sof_json_doc_parse(&d, text, sizeof(text) - 1) == SOF_JSON_OK);
    const sof_json_value_t *root = d.root, *v;
    CHECK(sof_json_value_type(root) == SOF_JSON_OBJECT && sof_json_value_count(root) == LENGTH(keys));
    CHECK(root->as.object.index != NULL && root->as.object.index == d.index);
    for (size_t i = 0; i < LENGTH(keys); i++) {
        const sof_json_member_t *m = sof_json_value_member(root, i);
        CHECK(m != NULL && strcmp(m->key, keys[i]) == 0 && m->key_length == strlen(keys[i]) && m->object == root);
        CHECK(m->value == sof_json_value_get(root, keys[i]));
    }
    CHECK(sof_json_value_member(root, LENGTH(keys)) == NULL);

    size_t length;
    int64_t i64 = 0;
    uint64_t u64 = 0;
    double dbl = 0;
    int b = -1;
    const char *s = sof_json_value_string(sof_json_value_get(root, "name"), &length);
    CHECK(s != NULL && length == 5 && memcmp(s, "caf\xc3\xa9", 6) == 0);
    CHECK(sof_json_value_int(sof_json_value_get(root, "n"), &i64) == SOF_JSON_OK && i64 == 42);
    CHECK(sof_json_value_uint(sof_json_value_get(root, "n"), &u64) == SOF_JSON_OK && u64 == 42);
    v = sof_json_value_get(root, "pi");
    CHECK(sof_json_value_double(v, &dbl) == SOF_JSON_OK && dbl == 3.25);
    CHECK(v->length == 4 && strcmp(v->as.text, "3.25") == 0);
    CHECK(sof_json_value_int(v, &i64) == SOF_JSON_RANGE && i64 == 42);
    CHECK(sof_json_value_double(sof_json_value_get(root, "big"), &dbl) == SOF_JSON_RANGE && isinf(dbl) && dbl > 0);
    CHECK(sof_json_value_int(sof_json_value_get(root, "neg"), &i64) == SOF_JSON_OK && i64 == -7);
    CHECK(sof_json_value_uint(sof_json_value_get(root, "neg"), &u64) == SOF_JSON_RANGE && u64 == 42);
    CHECK(sof_json_value_bool(sof_json_value_get(root, "t"), &b) == SOF_JSON_OK && b == 1);
    CHECK(sof_json_value_bool(sof_json_value_get(root, "f"), &b) == SOF_JSON_OK && b == 0);
    CHECK(sof_json_value_type(sof_json_value_get(root, "z")) == SOF_JSON_NULL);
    CHECK(sof_json_value_bool(sof_json_value_get(root, "z"), &b) == SOF_JSON_NO_MATCH && b == 0);

    v = sof_json_value_get(root, "list");
    CHECK(sof_json_value_type(v) == SOF_JSON_ARRAY && sof_json_value_count(v) == 4);
    CHECK(sof_json_value_int(sof_json_value_at(v, 0), &i64) == SOF_JSON_OK && i64 == 1);
    s = sof_json_value_string(sof_json_value_at(v, 1), &length);
    CHECK(s != NULL && length == 3 && strcmp(s, "two") == 0);
    CHECK(sof_json_value_count(sof_json_value_at(v, 2)) == 1);
    CHECK(sof_json_value_int(sof_json_value_at(sof_json_value_at(v, 2), 0), &i64) == SOF_JSON_OK && i64 == 3);
    const sof_json_value_t *empty = sof_json_value_at(v, 3);
    CHECK(sof_json_value_type(empty) == SOF_JSON_OBJECT && sof_json_value_count(empty) == 0);
    CHECK(sof_json_value_member(empty, 0) == NULL && sof_json_value_get(empty, "x") == NULL);
    CHECK(sof_json_value_at(v, 4) == NULL);
    v = sof_json_value_get(root, "nested");
    CHECK(v->as.object.index == NULL && strcmp(sof_json_value_string(sof_json_value_get(v, "k"), NULL), "v") == 0);
    s = sof_json_value_string(sof_json_value_get(root, "nul"), &length);
    CHECK(s != NULL && length == 3 && memcmp(s, "a\0b", 4) == 0);
    s = sof_json_value_string(sof_json_value_get(root, "empty"), &length);
    CHECK(s != NULL && length == 0 && s[0] == '\0');
    CHECK(sof_json_value_get(root, "missing") == NULL);
    CHECK(sof_json_value_get_bytes(root, "nulx", 3) == sof_json_value_get(root, "nul"));

    // The wrong type.
    CHECK(sof_json_value_string(sof_json_value_get(root, "n"), &length) == NULL && length == 0);
    CHECK(sof_json_value_double(sof_json_value_get(root, "name"), &dbl) == SOF_JSON_NO_MATCH);
    CHECK(sof_json_value_int(sof_json_value_get(root, "name"), &i64) == SOF_JSON_NO_MATCH);
    CHECK(sof_json_value_uint(sof_json_value_get(root, "name"), &u64) == SOF_JSON_NO_MATCH);
    CHECK(sof_json_value_count(sof_json_value_get(root, "name")) == 0);
    CHECK(sof_json_value_at(root, 0) == NULL);
    CHECK(sof_json_value_member(sof_json_value_get(root, "list"), 0) == NULL);
    CHECK(sof_json_value_get(sof_json_value_get(root, "list"), "a") == NULL);

    // Top-level values of every kind.
    CHECK(sof_json_doc_parse(&d, "42", 2) == SOF_JSON_OK && sof_json_value_type(d.root) == SOF_JSON_NUMBER);
    CHECK(sof_json_doc_parse(&d, " \"x\" ", 5) == SOF_JSON_OK && strcmp(sof_json_value_string(d.root, NULL), "x") == 0);
    CHECK(sof_json_doc_parse(&d, "null", 4) == SOF_JSON_OK && sof_json_value_type(d.root) == SOF_JSON_NULL);
    CHECK(sof_json_doc_parse(&d, "[]", 2) == SOF_JSON_OK && sof_json_value_count(d.root) == 0 && d.root->as.elements == NULL);
    CHECK(d.index == NULL);
    CHECK(sof_json_doc_clear(&d) == SOF_JSON_OK && d.root == NULL);
    CHECK(sof_json_doc_free(&d) == SOF_JSON_OK && d.ready == 0);
}

static void test_doc_index(void) {
    sof_json_doc_t d;
    sof_json_writer_t w;
    char key[16];
    int64_t i64;
    CHECK(sof_json_doc_init(&d, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);

    // A large object is indexed, and every key is found.
    CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
    for (int i = 0; i < 20; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        CHECK(sof_json_writer_key(&w, key) == SOF_JSON_OK && sof_json_writer_int(&w, i) == SOF_JSON_OK);
    }
    CHECK(sof_json_writer_object_end(&w) == SOF_JSON_OK);
    CHECK(sof_json_doc_parse(&d, w.out.data, w.out.length) == SOF_JSON_OK);
    CHECK(d.root->as.object.index != NULL && d.index->count == 20);
    for (int i = 0; i < 20; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        CHECK(sof_json_value_int(sof_json_value_get(d.root, key), &i64) == SOF_JSON_OK && i64 == i);
    }
    CHECK(sof_json_value_get(d.root, "k20") == NULL && sof_json_value_get(d.root, "k") == NULL);

    // Many objects with the same keys share the index, and each finds its
    // own.
    sof_json_writer_clear(&w);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    for (int j = 0; j < 200; j++) {
        CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
        for (int i = 0; i < 10; i++) {
            snprintf(key, sizeof(key), "k%d", i);
            CHECK(sof_json_writer_key(&w, key) == SOF_JSON_OK && sof_json_writer_int(&w, j * 100 + i) == SOF_JSON_OK);
        }
        CHECK(sof_json_writer_object_end(&w) == SOF_JSON_OK);
    }
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    CHECK(sof_json_doc_parse(&d, w.out.data, w.out.length) == SOF_JSON_OK);
    CHECK(d.index != NULL && d.index->count == 2000);
    for (int j = 0; j < 200; j++) {
        for (int i = 0; i < 10; i++) {
            snprintf(key, sizeof(key), "k%d", i);
            CHECK(sof_json_value_int(sof_json_value_get(sof_json_value_at(d.root, (size_t)j), key), &i64) == SOF_JSON_OK);
            CHECK(i64 == j * 100 + i);
        }
    }
    sof_json_writer_free(&w);

    // A key there more than once finds the last, indexed or not.
    static const char twice[] = "{\"a\":1,\"b\":2,\"a\":3}";
    CHECK(sof_json_doc_parse(&d, twice, sizeof(twice) - 1) == SOF_JSON_OK && d.index == NULL);
    CHECK(sof_json_value_count(d.root) == 3 && sof_json_value_int(sof_json_value_get(d.root, "a"), &i64) == SOF_JSON_OK && i64 == 3);
    CHECK(sof_json_value_int(sof_json_value_member(d.root, 0)->value, &i64) == SOF_JSON_OK && i64 == 1);
    static const char twice_large[] = "{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8,\"i\":9,\"a\":10}";
    CHECK(sof_json_doc_parse(&d, twice_large, sizeof(twice_large) - 1) == SOF_JSON_OK && d.index != NULL);
    CHECK(sof_json_value_int(sof_json_value_get(d.root, "a"), &i64) == SOF_JSON_OK && i64 == 10);
    CHECK(sof_json_value_int(sof_json_value_get(d.root, "i"), &i64) == SOF_JSON_OK && i64 == 9);

    // Keys that hold a NUL, indexed and not.
    static const char *nuls[] = {
        "{\"a\\u0000b\":1,\"a\":2,\"a\\u0000c\":3}",
        "{\"a\\u0000b\":1,\"a\":2,\"a\\u0000c\":3,\"x1\":0,\"x2\":0,\"x3\":0,\"x4\":0,\"x5\":0,\"x6\":0}",
    };
    for (size_t k = 0; k < LENGTH(nuls); k++) {
        CHECK(sof_json_doc_parse(&d, nuls[k], strlen(nuls[k])) == SOF_JSON_OK);
        CHECK((d.index != NULL) == (k == 1));
        CHECK(sof_json_value_int(sof_json_value_get_bytes(d.root, "a\0b", 3), &i64) == SOF_JSON_OK && i64 == 1);
        CHECK(sof_json_value_int(sof_json_value_get(d.root, "a"), &i64) == SOF_JSON_OK && i64 == 2);
        CHECK(sof_json_value_int(sof_json_value_get_bytes(d.root, "a\0c", 3), &i64) == SOF_JSON_OK && i64 == 3);
        CHECK(sof_json_value_get_bytes(d.root, "a\0d", 3) == NULL);
    }

    // SOF_JSON_INDEX_MIN members aren't indexed; one more are.
    CHECK(sof_json_doc_parse(&d, "{\"1\":1,\"2\":2,\"3\":3,\"4\":4,\"5\":5,\"6\":6,\"7\":7,\"8\":8}", 49) == SOF_JSON_OK);
    CHECK(sof_json_value_count(d.root) == SOF_JSON_INDEX_MIN && d.index == NULL);
    CHECK(sof_json_doc_parse(&d, "{\"1\":1,\"2\":2,\"3\":3,\"4\":4,\"5\":5,\"6\":6,\"7\":7,\"8\":8,\"9\":9}", 55) == SOF_JSON_OK);
    CHECK(sof_json_value_count(d.root) == SOF_JSON_INDEX_MIN + 1 && d.index != NULL);
    sof_json_doc_free(&d);
}

static void test_doc_pointer(void) {
    // The examples of RFC 6901, section 5.
    static const char text[] = "{\"foo\": [\"bar\", \"baz\"], \"\": 0, \"a/b\": 1, \"c%d\": 2, \"e^f\": 3, \"g|h\": 4, \"i\\\\j\": 5, \"k\\\"l\": 6, \" \": 7, \"m~n\": 8}";
    sof_json_doc_t d;
    int64_t i64;
    CHECK(sof_json_doc_init(&d, NULL) == SOF_JSON_OK);
    CHECK(sof_json_doc_parse(&d, text, sizeof(text) - 1) == SOF_JSON_OK);
    const sof_json_value_t *root = d.root;
    CHECK(sof_json_value_pointer(root, "") == root);
    CHECK(sof_json_value_pointer(root, "/foo") == sof_json_value_get(root, "foo"));
    CHECK(strcmp(sof_json_value_string(sof_json_value_pointer(root, "/foo/0"), NULL), "bar") == 0);
    CHECK(strcmp(sof_json_value_string(sof_json_value_pointer(root, "/foo/1"), NULL), "baz") == 0);
    static const struct {
        const char *pointer;
        int64_t value;
    } numbers[] = {{"/", 0},      {"/a~1b", 1},  {"/c%d", 2}, {"/e^f", 3}, {"/g|h", 4},
                   {"/i\\j", 5}, {"/k\"l", 6}, {"/ ", 7},   {"/m~0n", 8}};
    for (size_t i = 0; i < LENGTH(numbers); i++) {
        CHECK(sof_json_value_int(sof_json_value_pointer(root, numbers[i].pointer), &i64) == SOF_JSON_OK && i64 == numbers[i].value);
    }
    static const char *none[] = {"/foo/2", "/foo/01", "/foo/-", "/foo/", "/foo/x", "foo", "/m~2n", "/m~",
                                 "/missing", "/foo/0/x", "/~01", "/foo/99999999999999999999999"};
    for (size_t i = 0; i < LENGTH(none); i++) {
        CHECK(sof_json_value_pointer(root, none[i]) == NULL);
    }
    CHECK(sof_json_value_pointer(NULL, "") == NULL && sof_json_value_pointer(root, NULL) == NULL);
    // Escapes in a small object, and deeper.
    static const char small[] = "{\"x/y\": {\"~\": [10, {\"\": 20}]}}";
    CHECK(sof_json_doc_parse(&d, small, sizeof(small) - 1) == SOF_JSON_OK && d.index == NULL);
    CHECK(sof_json_value_int(sof_json_value_pointer(d.root, "/x~1y/~0/0"), &i64) == SOF_JSON_OK && i64 == 10);
    CHECK(sof_json_value_int(sof_json_value_pointer(d.root, "/x~1y/~0/1/"), &i64) == SOF_JSON_OK && i64 == 20);
    CHECK(sof_json_value_pointer(d.root, "/x~1y/~1") == NULL);
    sof_json_doc_free(&d);
}

static void test_doc_read(void) {
    sof_json_doc_t d;
    sof_json_reader_t r;
    sof_json_token_t t;
    chunks_t chunks;
    int64_t i64;
    CHECK(sof_json_doc_init(&d, NULL) == SOF_JSON_OK);
    for (size_t chunk = 0; chunk < 2; chunk++) {
        // JSON Lines, a value at a time.
        static const char lines[] = "{\"a\":1}\n[2]\n\"s\"\n";
        CHECK(reader_open(&r, lines, sizeof(lines) - 1, SOF_JSON_LINES, chunk, &chunks, NULL));
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_OK);
        CHECK(sof_json_value_int(sof_json_value_get(d.root, "a"), &i64) == SOF_JSON_OK && i64 == 1);
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_OK && sof_json_value_count(d.root) == 1);
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_OK && strcmp(sof_json_value_string(d.root, NULL), "s") == 0);
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_NO_MATCH && d.root == NULL);
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_NO_MATCH);
        sof_json_reader_free(&r);

        // An array's elements, one at a time.
        static const char array[] = "[{\"id\":1},{\"id\":2},3]";
        CHECK(reader_open(&r, array, sizeof(array) - 1, 0, chunk, &chunks, NULL));
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_ARRAY);
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_OK);
        CHECK(sof_json_value_int(sof_json_value_get(d.root, "id"), &i64) == SOF_JSON_OK && i64 == 1);
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_OK);
        CHECK(sof_json_value_int(sof_json_value_get(d.root, "id"), &i64) == SOF_JSON_OK && i64 == 2);
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_OK && sof_json_value_int(d.root, &i64) == SOF_JSON_OK && i64 == 3);
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_NO_MATCH && sof_json_reader_depth(&r) == 0);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_NO_MATCH);
        sof_json_reader_free(&r);

        // A member's value, after its key; a key where a value should be is
        // an error, and read.
        static const char object[] = "{\"a\":[1],\"b\":2}";
        CHECK(reader_open(&r, object, sizeof(object) - 1, 0, chunk, &chunks, NULL));
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_OBJECT);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == SOF_JSON_KEY);
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_OK && sof_json_value_count(d.root) == 1);
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_ERROR && d.root == NULL);
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_OK && sof_json_value_int(d.root, &i64) == SOF_JSON_OK && i64 == 2);
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_NO_MATCH);
        sof_json_reader_free(&r);

        // An error in the middle of a value.
        CHECK(reader_open(&r, "[1,2,x]", 7, 0, chunk, &chunks, NULL));
        CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_SYNTAX && d.root == NULL);
        CHECK(d.line == 1 && d.column == 6 && d.offset == 5);
        sof_json_reader_free(&r);
    }
    sof_json_doc_free(&d);
}

static void test_doc_spans(void) {
    static const char text[] = "{\n  \"name\": \"vika\",\n  \"deps\": [ 1, -2.5e3 , true, null,\n"
                               "    {\"a\\u0041\" : \"x\\\"y\"} ],\n  \"e\": {}\n}\n";
    sof_json_doc_t d;
    CHECK(sof_json_doc_init(&d, NULL) == SOF_JSON_OK);
    CHECK(sof_json_doc_parse(&d, text, strlen(text)) == SOF_JSON_OK);
    // Containers run from their beginning to their end, and the rest are
    // their text, strings and keys with their quotes and escapes.
    CHECK(d.root->span.offset == 0 && d.root->span.length == strlen(text) - 1);
    const sof_json_member_t *m = sof_json_value_member(d.root, 0);
    CHECK(span_is(text, m->key_span, "\"name\"") && span_is(text, m->value->span, "\"vika\""));
    m = sof_json_value_member(d.root, 1);
    CHECK(span_is(text, m->key_span, "\"deps\""));
    CHECK(span_is(text, m->value->span, "[ 1, -2.5e3 , true, null,\n    {\"a\\u0041\" : \"x\\\"y\"} ]"));
    CHECK(span_is(text, sof_json_value_at(m->value, 1)->span, "-2.5e3"));
    CHECK(span_is(text, sof_json_value_at(m->value, 2)->span, "true"));
    CHECK(span_is(text, sof_json_value_at(m->value, 3)->span, "null"));
    const sof_json_value_t *inner = sof_json_value_at(m->value, 4);
    CHECK(span_is(text, inner->span, "{\"a\\u0041\" : \"x\\\"y\"}"));
    CHECK(span_is(text, sof_json_value_member(inner, 0)->key_span, "\"a\\u0041\""));
    CHECK(span_is(text, sof_json_value_get(inner, "aA")->span, "\"x\\\"y\""));
    CHECK(span_is(text, sof_json_value_get(d.root, "e")->span, "{}"));
    // A line number from an offset.
    size_t line = 0, column = 0;
    sof_string_t whole = sof_string_view(text, strlen(text));
    CHECK(sof_string_position(&whole, inner->span.offset, &line, &column) == SOF_STRING_OK && line == 4 && column == 5);

    // Changing a value by splicing at its span keeps the layout.
    sof_string_t edited;
    CHECK(sof_string_from_cstr(&edited, text, NULL) == SOF_STRING_OK);
    const sof_json_value_t *name = sof_json_value_get(d.root, "name");
    CHECK(sof_string_delete(&edited, name->span.offset, name->span.length) == SOF_STRING_OK);
    CHECK(sof_string_insert_cstr(&edited, name->span.offset, "\"sof\"") == SOF_STRING_OK);
    CHECK(strncmp(edited.data, "{\n  \"name\": \"sof\",\n  \"deps\": [ 1,", 31) == 0);
    CHECK(sof_json_doc_parse(&d, edited.data, edited.length) == SOF_JSON_OK);
    CHECK(strcmp(sof_json_value_string(sof_json_value_get(d.root, "name"), NULL), "sof") == 0);
    sof_string_free(&edited);

    // Values read one at a time from JSON Lines have offsets from the start
    // of the input.
    static const char lines[] = "{\"a\":1}\n [2]\n";
    sof_json_reader_t r;
    chunks_t chunks;
    CHECK(reader_open(&r, lines, strlen(lines), SOF_JSON_LINES, 1, &chunks, NULL));
    CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_OK && span_is(lines, d.root->span, "{\"a\":1}"));
    CHECK(span_is(lines, sof_json_value_member(d.root, 0)->key_span, "\"a\""));
    CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_OK && span_is(lines, d.root->span, "[2]") && d.root->span.offset == 9);
    CHECK(span_is(lines, sof_json_value_at(d.root, 0)->span, "2"));
    sof_json_reader_free(&r);
    sof_json_doc_free(&d);
}

static void test_doc_errors(void) {
    sof_json_doc_t d;
    CHECK(sof_json_doc_init(&d, NULL) == SOF_JSON_OK);
    CHECK(sof_json_doc_parse(&d, "{\"a\":1,}", 8) == SOF_JSON_SYNTAX && d.root == NULL);
    CHECK(d.line == 1 && d.column == 8 && d.offset == 7);
    CHECK(sof_json_doc_parse(&d, "[1] 2", 5) == SOF_JSON_SYNTAX && d.root == NULL && d.column == 5);
    CHECK(sof_json_doc_parse(&d, "[1]\n x", 6) == SOF_JSON_SYNTAX && d.line == 2 && d.column == 2);
    CHECK(sof_json_doc_parse(&d, "", 0) == SOF_JSON_SYNTAX && d.line == 1 && d.column == 1);
    CHECK(sof_json_doc_parse(&d, NULL, 0) == SOF_JSON_SYNTAX);
    CHECK(sof_json_doc_parse(&d, NULL, 5) == SOF_JSON_ERROR);
    // A good document after a bad one.
    CHECK(sof_json_doc_parse(&d, "\"\\ud800\"", 8) == SOF_JSON_OK && d.line == 0 && d.column == 0);
    CHECK(strcmp(sof_json_value_string(d.root, NULL), "\xef\xbf\xbd") == 0);
    sof_json_doc_free(&d);
}

static void test_doc_null(void) {
    sof_json_doc_t d;
    sof_json_reader_t r;
    CHECK(sof_json_doc_init(NULL, NULL) == SOF_JSON_ERROR);
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    CHECK(sof_json_doc_init(&d, &alloc) == SOF_JSON_ERROR && d.ready == 0 && state.allocations == 0);

    CHECK(sof_json_reader_init_bytes(&r, 0, "[1]", 3, NULL) == SOF_JSON_OK);
    for (int k = 0; k < 2; k++) {
        sof_json_doc_t *p = k == 0 ? NULL : &d;
        if (k == 1) {
            CHECK(sof_json_doc_init(&d, NULL) == SOF_JSON_OK);
            CHECK(sof_json_doc_parse(&d, "[1]", 3) == SOF_JSON_OK);
            CHECK(sof_json_doc_free(&d) == SOF_JSON_OK && d.ready == 0 && d.root == NULL);
        }
        CHECK(sof_json_doc_clear(p) == SOF_JSON_ERROR);
        CHECK(sof_json_doc_parse(p, "1", 1) == SOF_JSON_ERROR);
        CHECK(sof_json_doc_read(p, &r) == SOF_JSON_ERROR);
        CHECK(sof_json_doc_free(p) == (k == 0 ? SOF_JSON_ERROR : SOF_JSON_OK));
    }
    // A reader that is NULL or not initialized.
    CHECK(sof_json_doc_init(&d, NULL) == SOF_JSON_OK);
    CHECK(sof_json_doc_read(&d, NULL) == SOF_JSON_ERROR);
    sof_json_reader_free(&r);
    CHECK(sof_json_doc_read(&d, &r) == SOF_JSON_ERROR);

    // The value functions with NULL.
    int b;
    int64_t i64;
    uint64_t u64;
    double dbl;
    size_t length = 5;
    CHECK(sof_json_doc_parse(&d, "{\"\": true}", 10) == SOF_JSON_OK);
    const sof_json_value_t *v = d.root;
    CHECK(sof_json_value_at(NULL, 0) == NULL);
    CHECK(sof_json_value_bool(NULL, &b) == SOF_JSON_NO_MATCH);
    CHECK(sof_json_value_bool(sof_json_value_get(v, ""), NULL) == SOF_JSON_ERROR);
    CHECK(sof_json_value_count(NULL) == 0);
    CHECK(sof_json_value_double(NULL, &dbl) == SOF_JSON_NO_MATCH && sof_json_value_double(v, NULL) == SOF_JSON_ERROR);
    CHECK(sof_json_value_int(NULL, &i64) == SOF_JSON_NO_MATCH && sof_json_value_int(v, NULL) == SOF_JSON_ERROR);
    CHECK(sof_json_value_uint(NULL, &u64) == SOF_JSON_NO_MATCH && sof_json_value_uint(v, NULL) == SOF_JSON_ERROR);
    CHECK(sof_json_value_get(NULL, "a") == NULL && sof_json_value_get(v, NULL) == NULL);
    CHECK(sof_json_value_get_bytes(v, NULL, 1) == NULL);
    CHECK(sof_json_value_bool(sof_json_value_get_bytes(v, NULL, 0), &b) == SOF_JSON_OK && b == 1);
    CHECK(sof_json_value_member(NULL, 0) == NULL);
    CHECK(sof_json_value_pointer(NULL, "/a") == NULL);
    CHECK(sof_json_value_string(NULL, &length) == NULL && length == 0);
    CHECK(sof_json_value_string(NULL, NULL) == NULL);
    CHECK(sof_json_value_type(NULL) == SOF_JSON_NONE);
    sof_json_doc_free(&d);
}

static void test_doc_no_memory(void) {
    // Fail each allocation in turn until a run makes none fail. A parse that
    // fails must return SOF_JSON_NO_MEM and leave the document empty, and
    // parsing again must give the whole document. Nothing may leak.
    sof_json_writer_t w;
    char key[16];
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
    for (int i = 0; i < 12; i++) {
        snprintf(key, sizeof(key), "m%d", i);
        CHECK(sof_json_writer_key(&w, key) == SOF_JSON_OK && sof_json_writer_array_begin(&w) == SOF_JSON_OK);
        CHECK(sof_json_writer_int(&w, i) == SOF_JSON_OK && sof_json_writer_string(&w, "str") == SOF_JSON_OK);
        CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK && sof_json_writer_key(&w, "x") == SOF_JSON_OK);
        CHECK(sof_json_writer_int(&w, i) == SOF_JSON_OK && sof_json_writer_object_end(&w) == SOF_JSON_OK);
        CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK);
    }
    char *long_string = malloc(5001);
    CHECK(long_string != NULL);
    for (int i = 0; i < 5000; i++) {
        long_string[i] = "ab\nc"[i % 4];
    }
    long_string[5000] = '\0';
    CHECK(sof_json_writer_key(&w, "long") == SOF_JSON_OK && sof_json_writer_string(&w, long_string) == SOF_JSON_OK);
    free(long_string);
    CHECK(sof_json_writer_key(&w, "objs") == SOF_JSON_OK && sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    for (int j = 0; j < 3; j++) {
        CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
        for (int i = 0; i < 10; i++) {
            snprintf(key, sizeof(key), "k%d", i);
            CHECK(sof_json_writer_key(&w, key) == SOF_JSON_OK && sof_json_writer_int(&w, j * 10 + i) == SOF_JSON_OK);
        }
        CHECK(sof_json_writer_object_end(&w) == SOF_JSON_OK);
    }
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK && sof_json_writer_object_end(&w) == SOF_JSON_OK);

    sof_json_writer_t again;
    CHECK(sof_json_writer_init(&again, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_json_doc_t d;
        int failed = 0;
        CHECK(sof_json_doc_init(&d, &alloc) == SOF_JSON_OK);
        sof_json_error_t rc = sof_json_doc_parse(&d, w.out.data, w.out.length);
        if (rc == SOF_JSON_NO_MEM) {
            failed = 1;
            CHECK(d.root == NULL && d.index == NULL);
            rc = sof_json_doc_parse(&d, w.out.data, w.out.length);
        }
        CHECK(rc == SOF_JSON_OK);
        sof_json_writer_clear(&again);
        CHECK(sof_json_writer_value(&again, d.root) == SOF_JSON_OK);
        CHECK(output_is_bytes(&again, w.out.data, w.out.length));
        CHECK(failed == (state.allocations >= n));
        sof_json_doc_free(&d);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            break;
        }
    }
    sof_json_writer_free(&again);
    sof_json_writer_free(&w);
}

static void test_writer_value(void) {
    static const char text[] = "{\"a\":[1,-2.5e3,true,false,null],\"b\":{\"c\":\"d\"},\"\":[],\"e\":\"\\u0000\\n\"}\n";
    sof_json_doc_t d;
    sof_json_writer_t w;
    CHECK(sof_json_doc_init(&d, NULL) == SOF_JSON_OK);
    CHECK(sof_json_doc_parse(&d, text, sizeof(text) - 1) == SOF_JSON_OK);
    const sof_json_value_t *root = d.root;

    // Written as it was read, and as part of another value.
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_value(&w, root) == SOF_JSON_OK && output_is(&w, text));
    CHECK(sof_json_writer_complete(&w) && w.values == 1);
    sof_json_writer_clear(&w);
    CHECK(sof_json_writer_array_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_value(&w, sof_json_value_get(root, "b")) == SOF_JSON_OK);
    CHECK(sof_json_writer_value(&w, sof_json_value_at(sof_json_value_get(root, "a"), 1)) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_end(&w) == SOF_JSON_OK && output_is(&w, "[{\"c\":\"d\"},-2.5e3]\n"));
    // Where a value can't go, or with nothing to write, nothing is written.
    sof_json_writer_clear(&w);
    CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
    CHECK(sof_json_writer_value(&w, root) == SOF_JSON_ERROR && output_is(&w, "{") && sof_json_writer_depth(&w) == 1);
    CHECK(sof_json_writer_value(&w, NULL) == SOF_JSON_ERROR);
    CHECK(sof_json_writer_value(NULL, root) == SOF_JSON_ERROR);
    sof_json_writer_free(&w);
    CHECK(sof_json_writer_value(&w, root) == SOF_JSON_ERROR);
    // Indented.
    CHECK(sof_json_writer_init(&w, 2, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_value(&w, sof_json_value_get(root, "b")) == SOF_JSON_OK && output_is(&w, "{\n  \"c\": \"d\"\n}\n"));
    sof_json_writer_free(&w);

    // A large value goes to the write function whole, in one call.
    sof_json_writer_t big;
    CHECK(sof_json_writer_init(&big, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_array_begin(&big) == SOF_JSON_OK);
    for (int i = 0; i < 1000; i++) {
        CHECK(sof_json_writer_string(&big, "0123456789") == SOF_JSON_OK);
    }
    CHECK(sof_json_writer_array_end(&big) == SOF_JSON_OK && big.out.length > 2 * SOF_JSON_FLUSH_SIZE);
    CHECK(sof_json_doc_parse(&d, big.out.data, big.out.length) == SOF_JSON_OK);
    sink_t *sink = calloc(1, sizeof(*sink));
    CHECK(sink != NULL);
    CHECK(sof_json_writer_init(&w, 0, 0, sink_write, sink, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_value(&w, d.root) == SOF_JSON_OK);
    CHECK(sink->calls == 1 && sink->length == big.out.length && memcmp(sink->data, big.out.data, sink->length) == 0);
    // Its write function failing is reported, and the output kept.
    sink->fail = 1;
    CHECK(sof_json_writer_value(&w, d.root) == SOF_JSON_WRITE && w.out.length == big.out.length);
    sof_json_writer_free(&w);
    free(sink);

    // Each allocation failing writes nothing, and writing again goes on,
    // with the commas where they belong.
    char *expected = malloc(2 * big.out.length + 16);
    CHECK(expected != NULL);
    size_t length = 0;
    expected[length++] = '[';
    memcpy(expected + length, big.out.data, big.out.length - 1);
    length += big.out.length - 1;
    expected[length++] = ',';
    memcpy(expected + length, big.out.data, big.out.length - 1);
    length += big.out.length - 1;
    memcpy(expected + length, "]\n", 2);
    length += 2;
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        int failed = 0;
        sof_json_error_t rc = sof_json_writer_init(&w, 0, 0, NULL, NULL, &alloc);
        if (rc != SOF_JSON_OK) {
            failed = 1;
            CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, &alloc) == SOF_JSON_OK);
        }
        for (int step = 0; step < 4; step++) {
            size_t before = w.out.length, depth = sof_json_writer_depth(&w), values = w.values;
            rc = step == 0 ? sof_json_writer_array_begin(&w) : step == 3 ? sof_json_writer_array_end(&w) : sof_json_writer_value(&w, d.root);
            if (rc == SOF_JSON_NO_MEM) {
                failed = 1;
                CHECK(w.out.length == before && sof_json_writer_depth(&w) == depth && w.values == values);
                rc = step == 0 ? sof_json_writer_array_begin(&w) : step == 3 ? sof_json_writer_array_end(&w) : sof_json_writer_value(&w, d.root);
            }
            CHECK(rc == SOF_JSON_OK);
        }
        CHECK(output_is_bytes(&w, expected, length));
        CHECK(failed == (state.allocations >= n));
        sof_json_writer_free(&w);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            break;
        }
    }
    free(expected);
    sof_json_writer_free(&big);
    sof_json_doc_free(&d);
}

static void test_random_round_trip(void) {
    // Random documents, written compact and indented, are read by the
    // document and written again as the compact output; and the reader
    // reads the same tokens from both, in pieces, as the tests' checker.
    sof_json_writer_t ws[2], again;
    sof_json_doc_t d;
    uint64_t state = 0x510e527fade682d1ULL;
    size_t size = 1 << 20;
    char *canon = malloc(size), *out = malloc(size);
    CHECK(canon != NULL && out != NULL);
    CHECK(sof_json_writer_init(&ws[0], 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_init(&ws[1], 3, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_writer_init(&again, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
    CHECK(sof_json_doc_init(&d, NULL) == SOF_JSON_OK);
    for (int i = 0; i < RANDOM_DOCUMENTS; i++) {
        sof_json_writer_clear(&ws[0]);
        sof_json_writer_clear(&ws[1]);
        CHECK(random_value(ws, LENGTH(ws), &state, 0));
        size_t canon_length;
        CHECK(test_json_text(ws[0].out.data, ws[0].out.length, canon, size, &canon_length));
        for (size_t k = 0; k < LENGTH(ws); k++) {
            CHECK(sof_json_doc_parse(&d, ws[k].out.data, ws[k].out.length) == SOF_JSON_OK);
            sof_json_writer_clear(&again);
            CHECK(sof_json_writer_value(&again, d.root) == SOF_JSON_OK);
            CHECK(output_is_bytes(&again, ws[0].out.data, ws[0].out.length));
            sof_json_reader_t r;
            chunks_t chunks;
            size_t out_length;
            CHECK(reader_open(&r, ws[k].out.data, ws[k].out.length, 0, 3, &chunks, NULL));
            CHECK(reader_canon(&r, out, size, &out_length) == SOF_JSON_NO_MATCH);
            CHECK(out_length == canon_length && memcmp(out, canon, canon_length) == 0);
            sof_json_reader_free(&r);
        }
    }
    sof_json_doc_free(&d);
    sof_json_writer_free(&again);
    sof_json_writer_free(&ws[0]);
    sof_json_writer_free(&ws[1]);
    free(canon);
    free(out);
}

static void test_suite_reader(void) {
    // The reader accepts every text JSONTestSuite says a parser must accept
    // (y_), with the tokens the tests' checker reads, whole and a byte at a
    // time, and the document reads it and writes it again as the same
    // values. Both refuse every text a parser must refuse (n_). Those left
    // to the parser (i_) are taken or refused alike whole and a byte at a
    // time, and by the document. Strings are always valid UTF-8.
    DIR *dir = opendir(SUITE_DIR);
    struct dirent *entry;
    int counts[3] = {0}, wrong = 0;
    sof_json_doc_t d;
    CHECK(dir != NULL);
    CHECK(sof_json_doc_init(&d, NULL) == SOF_JSON_OK);
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        size_t name_length = strlen(name), length;
        char path[512];
        if (name_length < 7 || strcmp(name + name_length - 5, ".json") != 0 || name[1] != '_') {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", SUITE_DIR, name);
        char *data = file_read(path, &length);
        size_t size = 16 * length + 64;
        char *canon = malloc(size), *outs[2] = {malloc(size), malloc(size)};
        int ok = data != NULL && canon != NULL && outs[0] != NULL && outs[1] != NULL;
        size_t canon_length = 0, lengths[2] = {0, 0};
        sof_json_error_t rcs[2] = {SOF_JSON_ERROR, SOF_JSON_ERROR};
        int valid = ok && test_json_text(data, length, canon, size, &canon_length);
        for (size_t k = 0; ok && k < 2; k++) {
            sof_json_reader_t r;
            chunks_t chunks;
            ok = reader_open(&r, data, length, 0, k, &chunks, NULL);
            rcs[k] = reader_canon(&r, outs[k], size, &lengths[k]);
            sof_json_reader_free(&r);
        }
        int accepted = rcs[0] == SOF_JSON_NO_MATCH;
        ok = ok && rcs[0] == rcs[1] && lengths[0] == lengths[1] && memcmp(outs[0], outs[1], lengths[0]) == 0;
        ok = ok && (accepted || rcs[0] == SOF_JSON_SYNTAX || rcs[0] == SOF_JSON_TOO_DEEP);
        ok = ok && (sof_json_doc_parse(&d, data, length) == SOF_JSON_OK) == accepted;
        if (name[0] == 'y') {
            counts[0]++;
            ok = ok && accepted && valid && lengths[0] == canon_length && memcmp(outs[0], canon, canon_length) == 0;
            sof_json_writer_t w;
            size_t again_length;
            ok = ok && sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK && sof_json_writer_value(&w, d.root) == SOF_JSON_OK && test_json_text(w.out.data, w.out.length, outs[1], size, &again_length) && again_length == canon_length && memcmp(outs[1], canon, canon_length) == 0;
            sof_json_writer_free(&w);
        } else if (name[0] == 'n') {
            counts[1]++;
            ok = ok && !accepted;
        } else {
            counts[2]++;
        }
        if (!ok) {
            printf("(%s) ", name);
            wrong++;
        }
        free(data);
        free(canon);
        free(outs[0]);
        free(outs[1]);
    }
    closedir(dir);
    sof_json_doc_free(&d);
    CHECK(counts[0] == 95 && counts[1] == 188 && counts[2] == 35);
    CHECK(wrong == 0);
}

static void test_doubles_edges(void) {
    // The values at the edges of the ways doubles are written: 15, 16 and 17
    // digits, the integers below 1e17, and the subnormals.
    CHECK(number_is(1e15, "1000000000000000"));
    CHECK(number_is(123456789012345.0, "123456789012345"));
    CHECK(number_is(1e21, "1e+21"));
    CHECK(number_is(1e22, "1e+22"));
    CHECK(number_is(1e23, "1e+23"));
    CHECK(number_is(0.1 + 0.2, "0.30000000000000004"));
    CHECK(number_is(2.0 / 3, "0.6666666666666666"));
    CHECK(number_is(9007199254740993.0, "9007199254740992"));
    CHECK(number_is(12345678901234567.0, "12345678901234568"));
    CHECK(number_is(-1e-7, "-1e-07"));
    CHECK(number_is(2.2250738585072014e-308, "2.2250738585072014e-308"));
    CHECK(number_is(2.2250738585072009e-308, "2.225073858507201e-308"));
    CHECK(number_is(-5e-324, "-5e-324"));
    CHECK(number_is(1e-320, "1e-320"));
    CHECK(number_is(1.2345e-315, "1.2345e-315"));
    CHECK(number_is(1.7976931348623157e308, "1.7976931348623157e+308"));
    CHECK(number_is(-1.7976931348623157e308, "-1.7976931348623157e+308"));

    // Values near these, and random subnormals and powers of ten, read back
    // as themselves with no more digits than they need, and integers below
    // 1e17 are written without an exponent.
    static const double edges[] = {0.0, 5e-324, 1e-320, 2.2250738585072014e-308, 1e-5, 1e-4, 0.1, 1, 100, 1e15, 1e16, 1e17,
                                   9007199254740992.0, 1e21, 1e22, 1e23, 1e300, 1.7976931348623157e308};
    sof_json_writer_t w;
    uint64_t state = 0x510e527fade682d1ULL;
    CHECK(sof_json_writer_init(&w, 0, 0, NULL, NULL, NULL) == SOF_JSON_OK);
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
        if (!isfinite(value)) {
            continue;
        }
        sof_json_writer_clear(&w);
        CHECK(sof_json_writer_double(&w, value) == SOF_JSON_OK);
        double back = strtod(w.out.data, NULL);
        CHECK(back == value && signbit(back) == signbit(value));
        int digits = significant_digits(w.out.data);
        int shortest = 1;
        for (; shortest < 17; shortest++) {
            char text[64];
            snprintf(text, sizeof(text), "%.*g", shortest, value);
            if (strtod(text, NULL) == value) {
                break;
            }
        }
        CHECK(digits == (value == 0 ? 0 : shortest));
        double magnitude = value < 0 ? -value : value;
        CHECK((strchr(w.out.data, 'e') == NULL) == (magnitude < 1e17 && (magnitude >= 1e-4 || value == 0)));
    }
    sof_json_writer_free(&w);
}

static void test_strings_runs(void) {
    // Strings of plain bytes, which are found 8 at a time, with one byte or
    // sequence that isn't plain at each place, at each alignment, are
    // escaped as they are a byte at a time.
    static const char *const specials[] = {"\"", "\\", "\n", "\x01", "\x1f", "\x7f", "\xc3\xa9", "\xe2\x82\xac", "\xff", "\xc3"};
    static const char *const escaped[] = {"\\\"", "\\\\", "\\n", "\\u0001", "\\u001f", "\x7f", "\xc3\xa9", "\xe2\x82\xac", "\xef\xbf\xbd", "\xef\xbf\xbd"};
    static const char *const ascii[] = {"\\\"", "\\\\", "\\n", "\\u0001", "\\u001f", "\x7f", "\\u00e9", "\\u20ac", "\\ufffd", "\\ufffd"};
    char input[64], expected[128];
    sof_json_writer_t w;
    for (unsigned flags = 0; flags <= SOF_JSON_ASCII; flags += SOF_JSON_ASCII) {
        CHECK(sof_json_writer_init(&w, 0, flags, NULL, NULL, NULL) == SOF_JSON_OK);
        for (size_t length = 0; length <= 40; length++) {
            for (size_t at = 0; at <= length; at++) {
                for (size_t k = 0; k < LENGTH(specials); k++) {
                    for (size_t align = 0; align < 8; align++) {
                        // length plain bytes, with the special one at at, or
                        // none when at is length.
                        char *s = input + align;
                        size_t n = 0, e = 0;
                        expected[e++] = '"';
                        for (size_t i = 0; i <= length; i++) {
                            if (i == at && at < length) {
                                size_t sl = strlen(specials[k]);
                                const char *x = flags != 0 ? ascii[k] : escaped[k];
                                memcpy(s + n, specials[k], sl);
                                n += sl;
                                memcpy(expected + e, x, strlen(x));
                                e += strlen(x);
                            } else if (i < length) {
                                s[n++] = (char)('a' + (i * 7 + k) % 26);
                                expected[e++] = s[n - 1];
                            }
                        }
                        memcpy(expected + e, "\"\n", 2);
                        e += 2;
                        sof_json_writer_clear(&w);
                        CHECK(sof_json_writer_string_bytes(&w, s, n) == SOF_JSON_OK);
                        CHECK(output_is_bytes(&w, expected, e));
                        // As a key, too.
                        sof_json_writer_clear(&w);
                        CHECK(sof_json_writer_object_begin(&w) == SOF_JSON_OK);
                        CHECK(sof_json_writer_key_bytes(&w, s, n) == SOF_JSON_OK);
                        CHECK(w.out.length == e + 1 && memcmp(w.out.data + 1, expected, e - 2) == 0);
                        CHECK(sof_json_writer_null(&w) == SOF_JSON_OK && sof_json_writer_object_end(&w) == SOF_JSON_OK);
                    }
                }
            }
        }
        sof_json_writer_free(&w);
    }
}

static void test_reader_string_runs(void) {
    // Strings of plain bytes, which are skipped 8 at a time, with one byte
    // or sequence that isn't plain at each place, at each alignment, read
    // whole and in pieces, decode as they do a byte at a time, give the same
    // spans, and fail at the same place.
    static const char *const specials[] = {"\\\"", "\\\\", "\\n", "\\u00e9", "\xc3\xa9", "\xe2\x82\xac", "\x7f", "\x01", "\x1f", "\xff", "\xc3"};
    static const char *const decoded[] = {"\"", "\\", "\n", "\xc3\xa9", "\xc3\xa9", "\xe2\x82\xac", "\x7f", NULL, NULL, NULL, NULL};
    static const size_t chunk_sizes[] = {0, 1, 3, 8, 9};
    char text[128], expected[128];
    for (size_t length = 0; length <= 40; length++) {
        for (size_t at = 0; at <= length; at++) {
            for (size_t k = 0; k < LENGTH(specials); k++) {
                for (size_t pad = 0; pad < 8; pad++) {
                    size_t n = 0, e = 0, bad = 0;
                    memset(text, ' ', pad);
                    n = pad;
                    text[n++] = '"';
                    for (size_t i = 0; i < length; i++) {
                        if (i == at) {
                            bad = n;
                            memcpy(text + n, specials[k], strlen(specials[k]));
                            n += strlen(specials[k]);
                            if (decoded[k] != NULL) {
                                memcpy(expected + e, decoded[k], strlen(decoded[k]));
                                e += strlen(decoded[k]);
                            }
                        } else {
                            text[n++] = (char)('a' + (i * 5 + k) % 26);
                            expected[e++] = text[n - 1];
                        }
                    }
                    text[n++] = '"';
                    int fails = at < length && decoded[k] == NULL;
                    for (size_t c = 0; c < LENGTH(chunk_sizes); c++) {
                        sof_json_reader_t r;
                        sof_json_token_t t;
                        chunks_t chunks;
                        CHECK(reader_open(&r, text, n, 0, chunk_sizes[c], &chunks, NULL));
                        sof_json_error_t rc = sof_json_reader_next(&r, &t);
                        if (fails) {
                            CHECK(rc == SOF_JSON_SYNTAX && r.offset == bad);
                        } else {
                            CHECK(rc == SOF_JSON_OK && t.type == SOF_JSON_STRING && t.length == e && memcmp(t.text, expected, e) == 0);
                            CHECK(t.text[e] == '\0' && t.span.offset == pad && t.span.length == n - pad);
                            CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_NO_MATCH);
                        }
                        sof_json_reader_free(&r);
                    }
                }
            }
        }
    }

    // A long string of plain runs between escapes, read whole and in
    // pieces with each allocation failing in turn: each call that fails
    // can be made again.
    size_t size = 8192, length = 0;
    char *long_text = malloc(size);
    CHECK(long_text != NULL);
    long_text[length++] = '[';
    long_text[length++] = '"';
    while (length < size - 64) {
        length += (size_t)snprintf(long_text + length, size - length, "plain bytes run on for a while%s", length % 3 == 0 ? "\\t" : "\xc3\xa9");
    }
    memcpy(long_text + length, "\", \"short\"]", 11);
    length += 11;
    for (size_t c = 0; c < 3; c++) {
        size_t chunk = c == 0 ? 0 : c == 1 ? 7 : 1000;
        for (size_t fail = 1;; fail++) {
            test_alloc_t state = {0};
            state.fail_at = fail;
            sof_alloc_t alloc = test_allocator(&state);
            sof_json_reader_t r;
            sof_json_token_t t;
            chunks_t chunks;
            sof_json_error_t rc;
            size_t strings = 0;
            while (!reader_open(&r, long_text, length, 0, chunk, &chunks, &alloc)) {
            }
            while ((rc = sof_json_reader_next(&r, &t)) != SOF_JSON_NO_MATCH) {
                CHECK(rc == SOF_JSON_OK || rc == SOF_JSON_NO_MEM);
                if (rc == SOF_JSON_OK && t.type == SOF_JSON_STRING) {
                    strings++;
                    CHECK(strings == 2 ? t.length == 5 : t.length > 7000);
                    CHECK(strings == 2 ? t.span.offset == length - 8 : t.span.offset == 1 && t.span.length == length - 11);
                }
            }
            CHECK(strings == 2);
            sof_json_reader_free(&r);
            CHECK(state.live == 0 && state.bad == 0);
            if (state.allocations < fail) {
                break;
            }
        }
    }
    free(long_text);
}

static void test_number_fast(void) {
    // Numbers that fit Clinger's fast path, those just beyond, and random
    // ones of every shape, convert to what strtod() gives.
    static const char *const numbers[] = {"9007199254740991", "9007199254740992", "9007199254740993", "-9007199254740993", "1e22", "1e23",
                                          "-1e22", "1e-22", "1e-23", "123456789012345e-22", "1234567890123456e-22", "0.1", "-0.0",
                                          "0.000000000000000000001", "1000000000000000000000", "10000000000000000000000",
                                          "100000000000000000000000", "3.14159", "4.9e-324", "1.7976931348623157e308", "8.98846567431158e307",
                                          "0.30000000000000004", "123.456e-3", "1.5E+3", "2.5000000000000000000000000000", "0.000123450000",
                                          "17976931348623157e292", "4503599627370497.5", "1E0", "-0e-0"};
    char text[96];
    uint64_t state = 0x1f83d9abfb41bd6bULL;
    for (size_t i = 0; i < LENGTH(numbers) + 100000; i++) {
        size_t n = 0;
        if (i < LENGTH(numbers)) {
            n = strlen(numbers[i]);
            memcpy(text, numbers[i], n);
        } else {
            // A sign, up to 20 digits before the point, up to 20 after it,
            // trailing zeros, and an exponent.
            uint64_t r = next_random(&state);
            if (r % 2 == 0) {
                text[n++] = '-';
            }
            size_t whole = (r >> 1) % 21, fraction = (r >> 6) % 21, zeros = (r >> 11) % 4;
            if (whole == 0) {
                text[n++] = '0';
            }
            for (size_t d = 0; d < whole; d++) {
                text[n++] = (char)('0' + (d == 0 ? 1 + next_random(&state) % 9 : next_random(&state) % 10));
            }
            if (fraction > 0 || zeros > 0) {
                text[n++] = '.';
                for (size_t d = 0; d < fraction; d++) {
                    text[n++] = (char)('0' + next_random(&state) % 10);
                }
                memset(text + n, '0', zeros + 1);
                n += zeros + 1;
            }
            if ((r >> 13) % 3 != 0) {
                n += (size_t)snprintf(text + n, sizeof(text) - n, "e%d", (int)((r >> 16) % 61) - 30);
            }
        }
        text[n] = '\0';
        double value = 0, expected = strtod(text, NULL);
        CHECK(sof_json_number_double(text, n, &value) == SOF_JSON_OK);
        if (memcmp(&value, &expected, sizeof(value)) != 0) {
            printf("(%s: %.17g, not %.17g) ", text, value, expected);
        }
        CHECK(memcmp(&value, &expected, sizeof(value)) == 0);
    }
}

static void test_reader_nesting(void) {
    // Objects and arrays inside each other in turn end as what they are,
    // and a closing byte of the other kind is an error, whichever is open,
    // after each is closed.
    static const char text[] = "[{\"a\": [{\"b\": []}, {}, [[{}]]], \"c\": {\"d\": [1]}}, [], {\"e\": [{}]}]";
    static const sof_json_type_t types[] = {
        SOF_JSON_ARRAY,      SOF_JSON_OBJECT,     SOF_JSON_KEY,       SOF_JSON_ARRAY,      SOF_JSON_OBJECT,     SOF_JSON_KEY,
        SOF_JSON_ARRAY,      SOF_JSON_ARRAY_END,  SOF_JSON_OBJECT_END, SOF_JSON_OBJECT,    SOF_JSON_OBJECT_END, SOF_JSON_ARRAY,
        SOF_JSON_ARRAY,      SOF_JSON_OBJECT,     SOF_JSON_OBJECT_END, SOF_JSON_ARRAY_END, SOF_JSON_ARRAY_END,  SOF_JSON_ARRAY_END,
        SOF_JSON_KEY,        SOF_JSON_OBJECT,     SOF_JSON_KEY,       SOF_JSON_ARRAY,      SOF_JSON_NUMBER,     SOF_JSON_ARRAY_END,
        SOF_JSON_OBJECT_END, SOF_JSON_OBJECT_END, SOF_JSON_ARRAY,     SOF_JSON_ARRAY_END,  SOF_JSON_OBJECT,     SOF_JSON_KEY,
        SOF_JSON_ARRAY,      SOF_JSON_OBJECT,     SOF_JSON_OBJECT_END, SOF_JSON_ARRAY_END, SOF_JSON_OBJECT_END, SOF_JSON_ARRAY_END,
    };
    sof_json_reader_t r;
    sof_json_token_t t;
    sof_json_type_t peeked;
    CHECK(sof_json_reader_init_bytes(&r, 0, text, strlen(text), NULL) == SOF_JSON_OK);
    for (size_t i = 0; i < LENGTH(types); i++) {
        CHECK(sof_json_reader_peek(&r, &peeked) == SOF_JSON_OK && peeked == types[i]);
        CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_OK && t.type == types[i]);
    }
    CHECK(sof_json_reader_next(&r, &t) == SOF_JSON_NO_MATCH);
    sof_json_reader_free(&r);
    CHECK(error_at("[{\"a\": []]", 0, SOF_JSON_SYNTAX, 1, 10));
    CHECK(error_at("{\"a\": [{}}", 0, SOF_JSON_SYNTAX, 1, 10));
    CHECK(error_at("[[{}], {}}", 0, SOF_JSON_SYNTAX, 1, 10));
    CHECK(error_at("{\"a\": {\"b\": [1]}]", 0, SOF_JSON_SYNTAX, 1, 17));
}

int main(void) {
    printf("sof_json:\n");
    run("init", test_init);
    run("compact output", test_compact);
    run("indented output", test_indent);
    run("strings and keys", test_strings);
    run("integers", test_integers);
    run("doubles", test_doubles);
    run("doubles in a locale with a decimal comma", test_locale);
    run("raw", test_raw);
    run("misplaced calls", test_misplaced);
    run("json lines", test_lines);
    run("write function", test_write);
    run("write function with a large value", test_write_large);
    run("clear", test_clear);
    run("free", test_free);
    run("NULL and freed writers", test_null_and_dead);
    run("random documents", test_random_documents);
    run("RFC 8259: every code point in a string", test_every_code_point);
    run("JSONTestSuite: the tests' checker", test_suite_checker);
    run("JSONTestSuite: valid texts written again", test_suite_rewritten);
    run("each allocation failing", test_no_memory);
    run("reader: tokens", test_reader_tokens);
    run("reader: peek", test_reader_peek);
    run("reader: strings", test_reader_strings);
    run("reader: numbers", test_reader_numbers);
    run("reader: errors and where they are", test_reader_errors);
    run("reader: depth", test_reader_depth);
    run("reader: JSON Lines", test_reader_lines);
    run("reader: input in pieces", test_reader_pieces);
    run("reader: spans", test_reader_spans);
    run("reader: read function failing", test_reader_read_fail);
    run("reader: skip", test_reader_skip);
    run("reader: clear", test_reader_clear);
    run("reader: NULL and freed readers", test_reader_null);
    run("reader: each allocation failing", test_reader_no_memory);
    run("number conversion", test_number_conversion);
    run("number conversion: random doubles read back", test_number_doubles);
    run("number conversion in a locale with a decimal comma", test_number_locale);
    run("document: parse and values", test_doc_values);
    run("document: index and repeated keys", test_doc_index);
    run("document: JSON Pointer", test_doc_pointer);
    run("document: reading values one at a time", test_doc_read);
    run("document: spans", test_doc_spans);
    run("document: errors", test_doc_errors);
    run("document: NULL and freed documents and values", test_doc_null);
    run("document: each allocation failing", test_doc_no_memory);
    run("writer: document values", test_writer_value);
    run("random documents read and written again", test_random_round_trip);
    run("JSONTestSuite: the reader and the document", test_suite_reader);
    run("doubles at the edges of their forms", test_doubles_edges);
    run("strings of plain bytes with one that isn't", test_strings_runs);
    run("reader: strings of plain bytes with one that isn't", test_reader_string_runs);
    run("reader: objects and arrays in turn", test_reader_nesting);
    run("number conversion against strtod()", test_number_fast);
    return test_report();
}
