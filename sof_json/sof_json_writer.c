/**
 * @file sof_json_writer.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief The JSON writer of sof_json.
 * @details Each open object or array is one byte on the open stack: whether
 * it is an object, whether it has a member or element yet (so the next needs
 * a comma), and, for an object, whether a key is waiting for its value. Every
 * function that writes notes where the buffer ended before it began, and
 * cuts the buffer back to there if anything fails, so a failed call writes
 * nothing.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "sof_json.h"
#include "sof_real.h"
#include "sof_utf.h"

/// @brief The bits of an open stack byte: the container is an object, not
/// an array.
#define OPEN_OBJECT 1
/// @brief It has a member or element, so the next is preceded by a comma.
#define OPEN_ITEMS 2
/// @brief It is an object with a key waiting for its value.
#define OPEN_KEY 4

/// @brief The bytes the buffer can hold when it is first allocated.
#define OUT_CAPACITY 256

/// @brief A byte repeated through a 64-bit word.
#define BYTES(b) (UINT64_C(0x0101010101010101) * (b))

static sof_json_error_t append(sof_json_writer_t *w, const void *bytes, size_t length);
static sof_json_error_t append_char(sof_json_writer_t *w, char c);
static sof_json_error_t append_escaped(sof_json_writer_t *w, const unsigned char *s, size_t length);
static sof_json_error_t append_line(sof_json_writer_t *w, size_t level);
static sof_json_error_t append_u(sof_json_writer_t *w, uint32_t unit);
static sof_json_error_t container_begin(sof_json_writer_t *w, unsigned char kind);
static sof_json_error_t container_end(sof_json_writer_t *w, unsigned char kind);
static sof_json_error_t finish(sof_json_writer_t *w, int top_level);
static size_t format_uint(uint64_t value, char *text);
static sof_json_error_t key_write(sof_json_writer_t *w, const void *key, size_t length);
static size_t plain_run(const unsigned char *s, size_t n);
static void rollback(sof_json_writer_t *w, size_t mark);
static sof_json_error_t scalar(sof_json_writer_t *w, const void *text, size_t length, int quoted);
static unsigned char *top(const sof_json_writer_t *w);
static int usable(const sof_json_writer_t *w);
static sof_json_error_t value_prefix(sof_json_writer_t *w);
static void value_placed(sof_json_writer_t *w);
static sof_json_error_t value_write(sof_json_writer_t *w, const sof_json_value_t *v);

/// @brief Appends bytes to the buffer.
/// @return SOF_JSON_OK, or SOF_JSON_NO_MEM, leaving the buffer as it
/// was.
static sof_json_error_t append(sof_json_writer_t *w, const void *bytes, size_t length) {
    return sof_string_append_bytes(&w->out, bytes, length) == SOF_STRING_OK ? SOF_JSON_OK : SOF_JSON_NO_MEM;
}

/// @brief Appends one byte to the buffer, inline while there is room.
/// @return SOF_JSON_OK, or SOF_JSON_NO_MEM, leaving the buffer as it
/// was.
static sof_json_error_t append_char(sof_json_writer_t *w, char c) {
    return sof_string_append_char(&w->out, c) == SOF_STRING_OK ? SOF_JSON_OK : SOF_JSON_NO_MEM;
}

/// @brief Appends a string's contents, escaped, without the quotes.
/// @details Runs of bytes that need no escape are appended whole, and are
/// found 8 bytes at a time while they are printable ASCII. Each invalid or
/// incomplete UTF-8 sequence becomes U+FFFD, and with SOF_JSON_ASCII each
/// character outside ASCII becomes a \u escape. On failure, part of the
/// string may have been appended; the caller rolls back.
static sof_json_error_t append_escaped(sof_json_writer_t *w, const unsigned char *s, size_t length) {
    static const unsigned char replacement[] = {0xef, 0xbf, 0xbd};
    int ascii = (w->flags & SOF_JSON_ASCII) != 0;
    size_t run = 0;
    size_t position = 0;
    sof_json_error_t rc = SOF_JSON_OK;
    while (rc == SOF_JSON_OK && position < length) {
        unsigned char c = s[position];
        if (c >= 0x20 && c < 0x80 && c != '"' && c != '\\') {
            position++;
            position += plain_run(s + position, length - position);
            continue;
        }
        uint32_t code = c;
        size_t used = 1;
        if (c >= 0x80) {
            used = sof_utf8_decode(s + position, length - position, &code);
            if (sof_utf_valid(code) && !ascii) {
                // Valid UTF-8 is written as it is, as part of the run.
                position += used;
                continue;
            }
        }
        rc = append(w, s + run, position - run);
        if (rc != SOF_JSON_OK) {
            break;
        }
        if (c < 0x80) {
            char escape[2] = {'\\', 0};
            switch (c) {
            case '"':
            case '\\':
                escape[1] = (char)c;
                break;
            case '\b':
                escape[1] = 'b';
                break;
            case '\f':
                escape[1] = 'f';
                break;
            case '\n':
                escape[1] = 'n';
                break;
            case '\r':
                escape[1] = 'r';
                break;
            case '\t':
                escape[1] = 't';
                break;
            default:
                break;
            }
            rc = escape[1] != 0 ? append(w, escape, 2) : append_u(w, c);
        } else if (!sof_utf_valid(code)) {
            rc = ascii ? append_u(w, SOF_UTF_REPLACEMENT) : append(w, replacement, sizeof(replacement));
        } else if (code < 0x10000) {
            rc = append_u(w, code);
        } else {
            code -= 0x10000;
            rc = append_u(w, 0xd800 | (code >> 10));
            if (rc == SOF_JSON_OK) {
                rc = append_u(w, 0xdc00 | (code & 0x3ff));
            }
        }
        position += used;
        run = position;
    }
    if (rc == SOF_JSON_OK) {
        rc = append(w, s + run, position - run);
    }
    return rc;
}

/// @brief Starts a line at an indentation level, when the writer indents.
/// @details Appends a newline and level * indent spaces, or nothing if
/// indent is 0.
static sof_json_error_t append_line(sof_json_writer_t *w, size_t level) {
    static const char spaces[] = "\n                                                               ";
    if (w->indent == 0) {
        return SOF_JSON_OK;
    }
    sof_json_error_t rc = append_char(w, '\n');
    size_t remaining = level * w->indent;
    while (rc == SOF_JSON_OK && remaining > 0) {
        size_t n = remaining < sizeof(spaces) - 2 ? remaining : sizeof(spaces) - 2;
        rc = append(w, spaces + 1, n);
        remaining -= n;
    }
    return rc;
}

/// @brief Appends a \u escape of a UTF-16 unit, in lower case hex.
static sof_json_error_t append_u(sof_json_writer_t *w, uint32_t unit) {
    static const char hex[] = "0123456789abcdef";
    char escape[6] = {'\\', 'u', hex[(unit >> 12) & 0xf], hex[(unit >> 8) & 0xf], hex[(unit >> 4) & 0xf], hex[unit & 0xf]};
    return append(w, escape, sizeof(escape));
}

/// @brief Begins an object or array, kind being OPEN_OBJECT or 0.
static sof_json_error_t container_begin(sof_json_writer_t *w, unsigned char kind) {
    if (!usable(w)) {
        return SOF_JSON_ERROR;
    }
    size_t mark = w->out.length;
    sof_json_error_t rc = value_prefix(w);
    if (rc == SOF_JSON_OK) {
        rc = append_char(w, kind == OPEN_OBJECT ? '{' : '[');
    }
    if (rc != SOF_JSON_OK) {
        rollback(w, mark);
        return rc;
    }
    // The container is its parent's value as soon as it begins. If it can't
    // be pushed, the parent's state goes back as it was; a failed push leaves
    // the stack's array where it was, so the pointer stays good.
    unsigned char *parent = top(w);
    unsigned char saved = parent != NULL ? *parent : 0;
    value_placed(w);
    if (sof_stack_push(&w->open, &kind) != SOF_STACK_OK) {
        if (parent != NULL) {
            *parent = saved;
        }
        rollback(w, mark);
        return SOF_JSON_NO_MEM;
    }
    return finish(w, 0);
}

/// @brief Ends the object or array begun last, which must be of kind
/// OPEN_OBJECT or 0.
static sof_json_error_t container_end(sof_json_writer_t *w, unsigned char kind) {
    unsigned char *open = top(w);
    if (!usable(w) || open == NULL || (*open & OPEN_OBJECT) != kind || (*open & OPEN_KEY) != 0) {
        return SOF_JSON_ERROR;
    }
    size_t depth = w->open.count;
    size_t mark = w->out.length;
    sof_json_error_t rc = SOF_JSON_OK;
    if ((*open & OPEN_ITEMS) != 0) {
        rc = append_line(w, depth - 1);
    }
    if (rc == SOF_JSON_OK) {
        rc = append_char(w, kind == OPEN_OBJECT ? '}' : ']');
    }
    if (rc == SOF_JSON_OK && depth == 1) {
        rc = append_char(w, '\n');
    }
    if (rc != SOF_JSON_OK) {
        rollback(w, mark);
        return rc;
    }
    sof_stack_pop(&w->open, NULL);
    return finish(w, depth == 1);
}

/// @brief Counts a finished top-level value, and passes output to the write
/// function if it ends one or the buffer has grown large enough.
/// @return SOF_JSON_OK, or SOF_JSON_WRITE if top_level and the write
/// failed; a failed write in the middle of a value isn't reported.
static sof_json_error_t finish(sof_json_writer_t *w, int top_level) {
    if (top_level) {
        w->values++;
        return sof_json_writer_flush(w);
    }
    if (w->write != NULL && w->out.length >= SOF_JSON_FLUSH_SIZE) {
        sof_json_writer_flush(w);
    }
    return SOF_JSON_OK;
}

/// @brief Formats an unsigned integer in decimal at the end of text, which
/// has room for 20 bytes.
/// @return The number of digits, which end at text + 20.
static size_t format_uint(uint64_t value, char *text) {
    size_t n = 0;
    do {
        text[19 - n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    return n;
}

/// @brief Writes a key, after the checks sof_json_writer_key() and
/// sof_json_writer_key_bytes() share.
static sof_json_error_t key_write(sof_json_writer_t *w, const void *key, size_t length) {
    unsigned char *open = top(w);
    if (!usable(w) || open == NULL || (*open & OPEN_OBJECT) == 0 || (*open & OPEN_KEY) != 0) {
        return SOF_JSON_ERROR;
    }
    size_t mark = w->out.length;
    sof_json_error_t rc = SOF_JSON_OK;
    if ((*open & OPEN_ITEMS) != 0) {
        rc = append_char(w, ',');
    }
    if (rc == SOF_JSON_OK) {
        rc = append_line(w, w->open.count);
    }
    if (rc == SOF_JSON_OK) {
        rc = append_char(w, '"');
    }
    if (rc == SOF_JSON_OK) {
        rc = append_escaped(w, key, length);
    }
    if (rc == SOF_JSON_OK) {
        rc = append_char(w, '"');
    }
    if (rc == SOF_JSON_OK) {
        rc = append_char(w, ':');
    }
    if (rc == SOF_JSON_OK && w->indent != 0) {
        rc = append_char(w, ' ');
    }
    if (rc != SOF_JSON_OK) {
        rollback(w, mark);
        return rc;
    }
    *open |= OPEN_ITEMS | OPEN_KEY;
    return finish(w, 0);
}

/// @brief Counts the bytes at the start of the n at s that a string can
/// hold as they are, 8 at a time: printable ASCII but '"' and '\'.
/// @details As sof_json_reader.c's: each word of 8 is tested at once, a byte
/// below 0x20 making (v - 0x20...) & ~v have its top bit set, as one equal
/// to '"' or '\' does when v is XORed with it, and one of 0x80 or more
/// having it set already. A borrow can flag a byte after a real one
/// wrongly, but never a word with none.
/// @return The bytes, a multiple of 8; the byte after them is for the
/// caller to look at.
static size_t plain_run(const unsigned char *s, size_t n) {
    size_t k = 0;
    while (n - k >= 8) {
        uint64_t v;
        memcpy(&v, s + k, sizeof(v));
        uint64_t quote = v ^ BYTES('"'), backslash = v ^ BYTES('\\');
        uint64_t special = ((v - BYTES(0x20)) & ~v) | ((quote - BYTES(1)) & ~quote) | ((backslash - BYTES(1)) & ~backslash) | v;
        if ((special & BYTES(0x80)) != 0) {
            break;
        }
        k += 8;
    }
    return k;
}

/// @brief Cuts the buffer back to mark bytes, undoing a failed call.
static void rollback(sof_json_writer_t *w, size_t mark) {
    sof_string_delete(&w->out, mark, w->out.length - mark);
}

/// @brief Writes a value that isn't an object or array: the length bytes at
/// text, as they are, or if quoted, as an escaped string in quotes.
static sof_json_error_t scalar(sof_json_writer_t *w, const void *text, size_t length, int quoted) {
    if (!usable(w)) {
        return SOF_JSON_ERROR;
    }
    size_t mark = w->out.length;
    sof_json_error_t rc = value_prefix(w);
    if (rc == SOF_JSON_OK) {
        if (quoted) {
            rc = append_char(w, '"');
            if (rc == SOF_JSON_OK) {
                rc = append_escaped(w, text, length);
            }
            if (rc == SOF_JSON_OK) {
                rc = append_char(w, '"');
            }
        } else {
            rc = append(w, text, length);
        }
    }
    int top_level = w->open.count == 0;
    if (rc == SOF_JSON_OK && top_level) {
        rc = append_char(w, '\n');
    }
    if (rc != SOF_JSON_OK) {
        rollback(w, mark);
        return rc;
    }
    value_placed(w);
    return finish(w, top_level);
}

/// @brief Returns the byte of the object or array open last, or NULL at the
/// top level.
static unsigned char *top(const sof_json_writer_t *w) {
    return w != NULL ? sof_stack_peek(&w->open) : NULL;
}

/// @brief Says whether w is a writer that can be used: not NULL, and
/// initialized.
static int usable(const sof_json_writer_t *w) {
    return w != NULL && w->ready;
}

/// @brief Checks that a value can go here and writes what comes before it:
/// a comma and a new line in an array. At the top level and after a key,
/// nothing comes before it.
/// @return SOF_JSON_OK, SOF_JSON_NO_MEM, or SOF_JSON_ERROR, writing
/// nothing, if a value can't go here.
static sof_json_error_t value_prefix(sof_json_writer_t *w) {
    unsigned char *open = top(w);
    if (open == NULL) {
        return SOF_JSON_OK;
    }
    if ((*open & OPEN_OBJECT) != 0) {
        return (*open & OPEN_KEY) != 0 ? SOF_JSON_OK : SOF_JSON_ERROR;
    }
    sof_json_error_t rc = SOF_JSON_OK;
    if ((*open & OPEN_ITEMS) != 0) {
        rc = append_char(w, ',');
    }
    if (rc == SOF_JSON_OK) {
        rc = append_line(w, w->open.count);
    }
    return rc;
}

/// @brief Records that a value was written in the object or array open
/// last: an array has an element, and an object's key has its value.
static void value_placed(sof_json_writer_t *w) {
    unsigned char *open = top(w);
    if (open != NULL) {
        *open = (unsigned char)((*open | OPEN_ITEMS) & ~OPEN_KEY);
    }
}

/// @brief Writes a document's value and what it holds, with the public
/// calls for each part.
static sof_json_error_t value_write(sof_json_writer_t *w, const sof_json_value_t *v) {
    sof_json_error_t rc;
    switch (v->type) {
    case SOF_JSON_NULL:
        return sof_json_writer_null(w);
    case SOF_JSON_BOOL:
        return sof_json_writer_bool(w, v->as.boolean);
    case SOF_JSON_NUMBER:
        return sof_json_writer_raw(w, v->as.text, v->length);
    case SOF_JSON_STRING:
        return sof_json_writer_string_bytes(w, v->as.text, v->length);
    case SOF_JSON_ARRAY:
        rc = sof_json_writer_array_begin(w);
        for (size_t i = 0; rc == SOF_JSON_OK && i < v->length; i++) {
            rc = value_write(w, v->as.elements[i]);
        }
        return rc == SOF_JSON_OK ? sof_json_writer_array_end(w) : rc;
    case SOF_JSON_OBJECT:
        rc = sof_json_writer_object_begin(w);
        for (size_t i = 0; rc == SOF_JSON_OK && i < v->length; i++) {
            const sof_json_member_t *m = &v->as.object.members[i];
            rc = sof_json_writer_key_bytes(w, m->key, m->key_length);
            if (rc == SOF_JSON_OK) {
                rc = value_write(w, m->value);
            }
        }
        return rc == SOF_JSON_OK ? sof_json_writer_object_end(w) : rc;
    default:
        return SOF_JSON_ERROR;
    }
}

sof_json_error_t sof_json_writer_array_begin(sof_json_writer_t *w) {
    return container_begin(w, 0);
}

sof_json_error_t sof_json_writer_array_end(sof_json_writer_t *w) {
    return container_end(w, 0);
}

sof_json_error_t sof_json_writer_bool(sof_json_writer_t *w, int value) {
    return value ? scalar(w, "true", 4, 0) : scalar(w, "false", 5, 0);
}

sof_json_error_t sof_json_writer_clear(sof_json_writer_t *w) {
    if (!usable(w)) {
        return SOF_JSON_ERROR;
    }
    sof_string_clear(&w->out);
    sof_stack_clear(&w->open);
    w->values = 0;
    w->written = 0;
    return SOF_JSON_OK;
}

int sof_json_writer_complete(const sof_json_writer_t *w) {
    return usable(w) && w->values > 0 && w->open.count == 0;
}

size_t sof_json_writer_depth(const sof_json_writer_t *w) {
    return usable(w) ? w->open.count : 0;
}

sof_json_error_t sof_json_writer_double(sof_json_writer_t *w, double value) {
    char text[SOF_REAL_FORMAT_SIZE];
    if (!usable(w) || !isfinite(value)) {
        return SOF_JSON_ERROR;
    }
    size_t length = sof_real_format(value, text, sizeof(text));
    if (length == 0) {
        return SOF_JSON_ERROR;
    }
    return scalar(w, text, length, 0);
}

sof_json_error_t sof_json_writer_flush(sof_json_writer_t *w) {
    if (!usable(w)) {
        return SOF_JSON_ERROR;
    }
    if (w->write == NULL || w->out.length == 0) {
        return SOF_JSON_OK;
    }
    if (w->write(w->out.data, w->out.length, w->ctx) != 0) {
        return SOF_JSON_WRITE;
    }
    w->written += w->out.length;
    sof_string_clear(&w->out);
    return SOF_JSON_OK;
}

sof_json_error_t sof_json_writer_free(sof_json_writer_t *w) {
    if (w == NULL) {
        return SOF_JSON_ERROR;
    }
    if (w->ready) {
        sof_string_free(&w->out);
        sof_stack_free(&w->open);
    }
    memset(w, 0, sizeof(*w));
    return SOF_JSON_OK;
}

sof_json_error_t sof_json_writer_init(sof_json_writer_t *w, size_t indent, unsigned flags, sof_json_write_func_t write, void *ctx, const sof_alloc_t *alloc) {
    if (w == NULL) {
        return SOF_JSON_ERROR;
    }
    memset(w, 0, sizeof(*w));
    if (!sof_alloc_valid(alloc)) {
        return SOF_JSON_ERROR;
    }
    // The stack allocates nothing until the first push, so only the buffer
    // can fail here.
    if (sof_stack_init(&w->open, 1, 0, NULL, NULL, alloc) != SOF_STACK_OK) {
        memset(w, 0, sizeof(*w));
        return SOF_JSON_NO_MEM;
    }
    if (sof_string_init(&w->out, OUT_CAPACITY, alloc) != SOF_STRING_OK) {
        sof_stack_free(&w->open);
        memset(w, 0, sizeof(*w));
        return SOF_JSON_NO_MEM;
    }
    w->indent = indent;
    w->flags = flags;
    w->write = write;
    w->ctx = ctx;
    w->ready = 1;
    return SOF_JSON_OK;
}

sof_json_error_t sof_json_writer_int(sof_json_writer_t *w, int64_t value) {
    char text[21];
    // The magnitude, as unsigned, so INT64_MIN's doesn't overflow.
    uint64_t magnitude = value < 0 ? 0 - (uint64_t)value : (uint64_t)value;
    size_t n = format_uint(magnitude, text + 1);
    if (value < 0) {
        text[20 - n++] = '-';
    }
    return scalar(w, text + 21 - n, n, 0);
}

sof_json_error_t sof_json_writer_key(sof_json_writer_t *w, const char *key) {
    if (key == NULL) {
        return SOF_JSON_ERROR;
    }
    return key_write(w, key, strlen(key));
}

sof_json_error_t sof_json_writer_key_bytes(sof_json_writer_t *w, const void *key, size_t length) {
    if (key == NULL && length != 0) {
        return SOF_JSON_ERROR;
    }
    return key_write(w, key, length);
}

sof_json_error_t sof_json_writer_null(sof_json_writer_t *w) {
    return scalar(w, "null", 4, 0);
}

sof_json_error_t sof_json_writer_object_begin(sof_json_writer_t *w) {
    return container_begin(w, OPEN_OBJECT);
}

sof_json_error_t sof_json_writer_object_end(sof_json_writer_t *w) {
    return container_end(w, OPEN_OBJECT);
}

const char *sof_json_writer_output(const sof_json_writer_t *w, size_t *length) {
    if (length != NULL) {
        *length = usable(w) ? w->out.length : 0;
    }
    return usable(w) ? w->out.data : NULL;
}

sof_json_error_t sof_json_writer_raw(sof_json_writer_t *w, const void *json, size_t length) {
    if (json == NULL || length == 0) {
        return SOF_JSON_ERROR;
    }
    return scalar(w, json, length, 0);
}

sof_json_error_t sof_json_writer_string(sof_json_writer_t *w, const char *s) {
    if (s == NULL) {
        return SOF_JSON_ERROR;
    }
    return scalar(w, s, strlen(s), 1);
}

sof_json_error_t sof_json_writer_string_bytes(sof_json_writer_t *w, const void *s, size_t length) {
    if (s == NULL && length != 0) {
        return SOF_JSON_ERROR;
    }
    return scalar(w, s, length, 1);
}

sof_json_error_t sof_json_writer_value(sof_json_writer_t *w, const sof_json_value_t *v) {
    if (!usable(w) || v == NULL) {
        return SOF_JSON_ERROR;
    }
    // The value is written with the write function put aside, so none of it
    // is passed on before it is whole; if a part fails, the buffer, the open
    // stack and the state of the object or array it went in go back to how
    // they were.
    size_t mark = w->out.length;
    size_t depth = w->open.count;
    size_t values = w->values;
    unsigned char *parent = top(w);
    unsigned char saved = parent != NULL ? *parent : 0;
    sof_json_write_func_t write = w->write;
    w->write = NULL;
    sof_json_error_t rc = value_write(w, v);
    w->write = write;
    if (rc != SOF_JSON_OK) {
        rollback(w, mark);
        sof_stack_truncate(&w->open, depth);
        parent = top(w);
        if (parent != NULL) {
            *parent = saved;
        }
        w->values = values;
        return rc;
    }
    return depth == 0 ? sof_json_writer_flush(w) : finish(w, 0);
}

sof_json_error_t sof_json_writer_uint(sof_json_writer_t *w, uint64_t value) {
    char text[20];
    size_t n = format_uint(value, text);
    return scalar(w, text + 20 - n, n, 0);
}
