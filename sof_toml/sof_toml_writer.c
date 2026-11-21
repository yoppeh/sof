/**
 * @file sof_toml_writer.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief The TOML writer of sof_toml.
 * @details What is open is a stack of frames, the root table at the bottom.
 * A frame is a table written as a section (the root, a [table] or an element
 * of an array of tables), an array of tables, an inline array or an inline
 * table, with bits saying whether a section's header has been written,
 * whether a section has begun a section of its own, after which it can have
 * no more key/value pairs, and whether an array or inline table has an item,
 * so the next needs a comma. The keys of the sections open are kept as one
 * dotted key, for their headers, and each frame notes how long it was
 * before the frame's own key was added.
 *
 * A key is kept until its value is written, so that a key/value pair, or a
 * section's header, is written in one piece. A section's header is written
 * before its first key/value pair, and at its end if it had none and no
 * sections of its own; a table holding only tables needs no header, since
 * theirs define it.
 *
 * Every function that writes notes the buffer's length and the frames it
 * may change before it begins, and puts them back if anything fails, so a
 * failed call writes nothing.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sof_real.h"
#include "sof_toml.h"
#include "sof_utf.h"

/// @brief A table written as a section: the root, a [table], or an element
/// of an array of tables.
#define KIND_SECTION 0
/// @brief An array of tables, whose elements are [[array]] sections.
#define KIND_AOT 1
/// @brief An inline array.
#define KIND_ARRAY 2
/// @brief An inline table.
#define KIND_INLINE 3

/// @brief A section's header is written, or it needs none.
#define STATE_HEADER 1
/// @brief A section has begun a section of its own, so it can have no more
/// key/value pairs.
#define STATE_SECTIONS 2
/// @brief An array, inline table or array of tables has an item.
#define STATE_ITEMS 4

/// @brief The bytes the buffer can hold when it is first allocated.
#define OUT_CAPACITY 256

/// @brief The longest run of bytes put() appends one at a time.
#define PUT_SHORT 4

/// @brief A byte repeated through a 64-bit word.
#define BYTES(b) (UINT64_C(0x0101010101010101) * (b))

/// @brief Something open.
typedef struct frame_t {
    /// @brief A KIND_ value.
    unsigned char kind;
    /// @brief The STATE_ bits.
    unsigned char state;
    /// @brief The length of the path before this frame's key was added to
    /// it, which it goes back to when the frame ends.
    size_t path;
} frame_t;

/// @brief What a call may change, noted before it begins, to put back if it
/// fails: the buffer's length, the frames open, the two at the top, the
/// path's length and whether a key was waiting.
typedef struct undo_t {
    size_t out;
    size_t count;
    frame_t frames[2];
    size_t path;
    int has_key;
} undo_t;

static int aot_shaped(const sof_toml_value_t *v);
static sof_toml_error_t container_begin(sof_toml_writer_t *w, unsigned char kind);
static sof_toml_error_t container_end(sof_toml_writer_t *w, unsigned char kind, char close);
static sof_toml_error_t finish(sof_toml_writer_t *w);
static frame_t *frame_at(const sof_toml_writer_t *w, size_t from_top);
static sof_toml_error_t header_write(sof_toml_writer_t *w, int aot, size_t path_length);
static int is_section(const sof_toml_value_t *v);
static sof_toml_error_t key_set(sof_toml_writer_t *w, const void *key, size_t length);
static sof_toml_error_t members_write(sof_toml_writer_t *w, const sof_toml_value_t *table);
static size_t plain_run(const unsigned char *s, size_t n);
static sof_toml_error_t put(sof_string_t *to, const void *bytes, size_t length);
static sof_toml_error_t put_escaped(const sof_toml_writer_t *w, sof_string_t *to, const unsigned char *s, size_t length);
static sof_toml_error_t put_quoted(const sof_toml_writer_t *w, sof_string_t *to, const void *s, size_t length);
static sof_toml_error_t put_u(sof_string_t *to, uint32_t code, int big);
static sof_toml_error_t scalar(sof_toml_writer_t *w, const void *text, size_t length, int quoted);
static void undo_restore(sof_toml_writer_t *w, const undo_t *u);
static void undo_save(const sof_toml_writer_t *w, undo_t *u);
static int usable(const sof_toml_writer_t *w);
static sof_toml_error_t value_begin(sof_toml_writer_t *w);
static sof_toml_error_t value_write(sof_toml_writer_t *w, const sof_toml_value_t *v);

/// @brief Says whether an array can be written as an array of tables: it
/// has elements, and they are all tables.
static int aot_shaped(const sof_toml_value_t *v) {
    if (v->type != SOF_TOML_ARRAY || v->length == 0) {
        return 0;
    }
    for (size_t i = 0; i < v->length; i++) {
        if (v->as.elements[i]->type != SOF_TOML_TABLE) {
            return 0;
        }
    }
    return 1;
}

/// @brief Begins an inline array or inline table, where a value can go.
static sof_toml_error_t container_begin(sof_toml_writer_t *w, unsigned char kind) {
    undo_t u;
    if (!usable(w)) {
        return SOF_TOML_ERROR;
    }
    undo_save(w, &u);
    sof_toml_error_t rc = value_begin(w);
    if (rc == SOF_TOML_OK) {
        rc = put(&w->out, kind == KIND_ARRAY ? "[" : "{", 1);
    }
    if (rc == SOF_TOML_OK) {
        frame_t frame = {kind, 0, w->path.length};
        if (sof_stack_push(&w->open, &frame) != SOF_STACK_OK) {
            rc = SOF_TOML_NO_MEM;
        }
    }
    if (rc != SOF_TOML_OK) {
        undo_restore(w, &u);
        return rc;
    }
    return finish(w);
}

/// @brief Ends the inline array or inline table open last, of kind, with
/// close: ] or }.
static sof_toml_error_t container_end(sof_toml_writer_t *w, unsigned char kind, char close) {
    frame_t *f = frame_at(w, 0);
    if (!usable(w) || f == NULL || f->kind != kind || w->has_key) {
        return SOF_TOML_ERROR;
    }
    size_t mark = w->out.length;
    sof_toml_error_t rc = put(&w->out, &close, 1);
    // In a section, the value ends its line.
    if (rc == SOF_TOML_OK && frame_at(w, 1)->kind == KIND_SECTION) {
        rc = put(&w->out, "\n", 1);
    }
    if (rc != SOF_TOML_OK) {
        sof_string_delete(&w->out, mark, w->out.length - mark);
        return rc;
    }
    sof_stack_pop(&w->open, NULL);
    return finish(w);
}

/// @brief Passes output to the write function if the buffer has grown large
/// enough, unless a value is being written whole. A failure isn't reported:
/// the output stays in the buffer for the next flush.
static sof_toml_error_t finish(sof_toml_writer_t *w) {
    if (!w->holding && w->write != NULL && w->out.length >= SOF_TOML_FLUSH_SIZE) {
        sof_toml_writer_flush(w);
    }
    return SOF_TOML_OK;
}

/// @brief Returns the frame from_top below the top (0 for the top), or NULL
/// if there isn't one.
static frame_t *frame_at(const sof_toml_writer_t *w, size_t from_top) {
    if (w == NULL || w->open.count <= from_top) {
        return NULL;
    }
    return (frame_t *)(void *)(w->open.elements + (w->open.count - 1 - from_top) * sizeof(frame_t));
}

/// @brief Writes a section's header, [path] or [[path]], from the first
/// path_length bytes of the path, after a blank line unless it is the first
/// thing written.
static sof_toml_error_t header_write(sof_toml_writer_t *w, int aot, size_t path_length) {
    sof_toml_error_t rc = SOF_TOML_OK;
    if (w->written + w->out.length > 0) {
        rc = put(&w->out, "\n", 1);
    }
    if (rc == SOF_TOML_OK) {
        rc = put(&w->out, "[[", aot ? 2 : 1);
    }
    if (rc == SOF_TOML_OK) {
        rc = put(&w->out, w->path.data, path_length);
    }
    if (rc == SOF_TOML_OK) {
        rc = aot ? put(&w->out, "]]\n", 3) : put(&w->out, "]\n", 2);
    }
    return rc;
}

/// @brief Says whether a table's member is written as a section of its own
/// when the table is: a table, or an array of tables, that the text didn't
/// write inline.
static int is_section(const sof_toml_value_t *v) {
    return !v->is_inline && (v->type == SOF_TOML_TABLE || aot_shaped(v));
}

/// @brief Keeps a key, as TOML writes it, for its value: bare if it can be,
/// otherwise quoted.
static sof_toml_error_t key_set(sof_toml_writer_t *w, const void *key, size_t length) {
    const unsigned char *k = key;
    frame_t *f = frame_at(w, 0);
    if (!usable(w) || (key == NULL && length != 0) || w->has_key || (f->kind != KIND_SECTION && f->kind != KIND_INLINE)) {
        return SOF_TOML_ERROR;
    }
    int is_bare = length > 0;
    for (size_t i = 0; is_bare && i < length; i++) {
        unsigned char c = k[i];
        is_bare = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    }
    sof_string_clear(&w->key);
    sof_toml_error_t rc = is_bare ? put(&w->key, key, length) : put_quoted(w, &w->key, key != NULL ? key : "", length);
    if (rc != SOF_TOML_OK) {
        sof_string_clear(&w->key);
        return rc;
    }
    w->has_key = 1;
    return SOF_TOML_OK;
}

/// @brief Writes a table's members in the section open last: its key/value
/// pairs, then its tables and arrays of tables, each as sections.
static sof_toml_error_t members_write(sof_toml_writer_t *w, const sof_toml_value_t *table) {
    sof_toml_error_t rc = SOF_TOML_OK;
    for (int sections = 0; sections < 2; sections++) {
        for (size_t i = 0; rc == SOF_TOML_OK && i < table->length; i++) {
            const sof_toml_member_t *m = &table->as.table.members[i];
            if (is_section(m->value) == sections) {
                rc = sof_toml_writer_key_bytes(w, m->key, m->key_length);
                if (rc == SOF_TOML_OK) {
                    rc = value_write(w, m->value);
                }
            }
        }
    }
    return rc;
}

/// @brief Counts the bytes at the start of the n at s that a string can
/// hold as they are, 8 at a time and then one at a time, so a short run
/// isn't checked a word at a time again after each byte: printable ASCII
/// but '"' and '\'.
/// @details Each word of 8 is tested at once: a byte below 0x20 makes
/// (v - 0x20...) & ~v have its top bit set, as one equal to 0x7f, '"' or
/// '\' does when v is XORed with it, and one of 0x80 or more has it set
/// already. A borrow can flag a byte after a real one wrongly, but never a
/// word with none.
/// @return The bytes; the byte after them is for the caller to look at.
static size_t plain_run(const unsigned char *s, size_t n) {
    size_t k = 0;
    while (n - k >= 8) {
        uint64_t v;
        memcpy(&v, s + k, sizeof(v));
        uint64_t del = v ^ BYTES(0x7f), quote = v ^ BYTES('"'), backslash = v ^ BYTES('\\');
        uint64_t special = ((v - BYTES(0x20)) & ~v) | ((del - BYTES(1)) & ~del) | ((quote - BYTES(1)) & ~quote) | ((backslash - BYTES(1)) & ~backslash) | v;
        if ((special & BYTES(0x80)) != 0) {
            break;
        }
        k += 8;
    }
    while (k < n) {
        unsigned char c = s[k];
        if (c < 0x20 || c >= 0x7f || c == '"' || c == '\\') {
            break;
        }
        k++;
    }
    return k;
}

/// @brief Appends bytes to a string, which they must not lie in.
/// @details Up to PUT_SHORT bytes are appended one at a time, which is
/// inline while there is room. On failure, part of them may have been
/// appended; the caller rolls back.
static sof_toml_error_t put(sof_string_t *to, const void *bytes, size_t length) {
    if (length <= PUT_SHORT) {
        const char *b = bytes;
        for (size_t i = 0; i < length; i++) {
            if (sof_string_append_char(to, b[i]) != SOF_STRING_OK) {
                return SOF_TOML_NO_MEM;
            }
        }
        return SOF_TOML_OK;
    }
    return sof_string_append_bytes(to, bytes, length) == SOF_STRING_OK ? SOF_TOML_OK : SOF_TOML_NO_MEM;
}

/// @brief Appends a string's contents, escaped, without the quotes.
/// @details Runs of bytes that need no escape are appended whole, and are
/// found 8 bytes at a time while they are printable ASCII. Each invalid or
/// incomplete UTF-8 sequence becomes U+FFFD, and with SOF_TOML_ASCII each
/// character outside ASCII becomes a \u or \U escape.
/// On failure, part of the string may have been appended; the caller rolls
/// back.
static sof_toml_error_t put_escaped(const sof_toml_writer_t *w, sof_string_t *to, const unsigned char *s, size_t length) {
    static const unsigned char replacement[] = {0xef, 0xbf, 0xbd};
    int ascii = (w->flags & SOF_TOML_ASCII) != 0;
    size_t run = 0;
    size_t position = 0;
    sof_toml_error_t rc = SOF_TOML_OK;
    while (rc == SOF_TOML_OK && position < length) {
        unsigned char c = s[position];
        if (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') {
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
        rc = put(to, s + run, position - run);
        if (rc != SOF_TOML_OK) {
            break;
        }
        if (c < 0x80) {
            static const char simple[] = "\"\"\\\\\bb\tt\nn\ff\rr";
            const char *found = NULL;
            for (size_t k = 0; k + 1 < sizeof(simple); k += 2) {
                if ((char)c == simple[k]) {
                    found = simple + k + 1;
                }
            }
            if (found != NULL) {
                char escape[2] = {'\\', *found};
                rc = put(to, escape, 2);
            } else {
                rc = put_u(to, c, 0);
            }
        } else if (!sof_utf_valid(code)) {
            rc = ascii ? put_u(to, SOF_UTF_REPLACEMENT, 0) : put(to, replacement, sizeof(replacement));
        } else {
            rc = put_u(to, code, code > 0xffff);
        }
        position += used;
        run = position;
    }
    if (rc == SOF_TOML_OK) {
        rc = put(to, s + run, position - run);
    }
    return rc;
}

/// @brief Appends a string in double quotes, escaped.
static sof_toml_error_t put_quoted(const sof_toml_writer_t *w, sof_string_t *to, const void *s, size_t length) {
    sof_toml_error_t rc = put(to, "\"", 1);
    if (rc == SOF_TOML_OK) {
        rc = put_escaped(w, to, s, length);
    }
    if (rc == SOF_TOML_OK) {
        rc = put(to, "\"", 1);
    }
    return rc;
}

/// @brief Appends a \u escape of a code point, in upper case hex, or if big,
/// a \U escape.
static sof_toml_error_t put_u(sof_string_t *to, uint32_t code, int big) {
    char escape[12];
    int n = snprintf(escape, sizeof(escape), big ? "\\U%08" PRIX32 : "\\u%04" PRIX32, code);
    return put(to, escape, (size_t)n);
}

/// @brief Writes a value that isn't an array or table: the length bytes at
/// text, as they are, or if quoted, as an escaped string in quotes.
static sof_toml_error_t scalar(sof_toml_writer_t *w, const void *text, size_t length, int quoted) {
    undo_t u;
    if (!usable(w)) {
        return SOF_TOML_ERROR;
    }
    undo_save(w, &u);
    sof_toml_error_t rc = value_begin(w);
    if (rc == SOF_TOML_OK) {
        rc = quoted ? put_quoted(w, &w->out, text, length) : put(&w->out, text, length);
    }
    if (rc == SOF_TOML_OK && frame_at(w, 0)->kind == KIND_SECTION) {
        rc = put(&w->out, "\n", 1);
    }
    if (rc != SOF_TOML_OK) {
        undo_restore(w, &u);
        return rc;
    }
    return finish(w);
}

/// @brief Puts back what undo_save() noted, after a call fails.
static void undo_restore(sof_toml_writer_t *w, const undo_t *u) {
    sof_string_delete(&w->out, u->out, w->out.length - u->out);
    sof_stack_truncate(&w->open, u->count);
    for (size_t k = 0; k < 2 && k < u->count; k++) {
        *frame_at(w, k) = u->frames[k];
    }
    sof_string_delete(&w->path, u->path, w->path.length - u->path);
    w->has_key = u->has_key;
}

/// @brief Notes what a call may change, before it begins.
static void undo_save(const sof_toml_writer_t *w, undo_t *u) {
    memset(u, 0, sizeof(*u));
    u->out = w->out.length;
    u->count = w->open.count;
    for (size_t k = 0; k < 2 && k < u->count; k++) {
        u->frames[k] = *frame_at(w, k);
    }
    u->path = w->path.length;
    u->has_key = w->has_key;
}

/// @brief Says whether w is a writer that can be used: not NULL, and
/// initialized.
static int usable(const sof_toml_writer_t *w) {
    return w != NULL && w->ready;
}

/// @brief Checks that a value can go here and writes what comes before it:
/// in a section, its header if it isn't written yet, and the key and an
/// equals sign; in an inline table, a comma, the key and an equals sign; in
/// an inline array, a comma.
/// @return SOF_TOML_OK, SOF_TOML_NO_MEM, or SOF_TOML_ERROR if a value
/// can't go here. The caller rolls back on failure.
static sof_toml_error_t value_begin(sof_toml_writer_t *w) {
    frame_t *f = frame_at(w, 0);
    sof_toml_error_t rc = SOF_TOML_OK;
    switch (f->kind) {
    case KIND_SECTION:
        if (!w->has_key || (f->state & STATE_SECTIONS) != 0) {
            return SOF_TOML_ERROR;
        }
        if ((f->state & STATE_HEADER) == 0) {
            rc = header_write(w, 0, w->path.length);
            f->state |= STATE_HEADER;
        }
        break;
    case KIND_INLINE:
        if (!w->has_key) {
            return SOF_TOML_ERROR;
        }
        if ((f->state & STATE_ITEMS) != 0) {
            rc = put(&w->out, ", ", 2);
        }
        break;
    case KIND_ARRAY:
        if ((f->state & STATE_ITEMS) != 0) {
            rc = put(&w->out, ", ", 2);
        }
        break;
    default:
        return SOF_TOML_ERROR;
    }
    f->state |= STATE_ITEMS;
    if (rc == SOF_TOML_OK && w->has_key) {
        rc = put(&w->out, w->key.data, w->key.length);
        if (rc == SOF_TOML_OK) {
            rc = put(&w->out, " = ", 3);
        }
        w->has_key = 0;
    }
    return rc;
}

/// @brief Writes a document's value and what it holds, with the public
/// calls for each part.
static sof_toml_error_t value_write(sof_toml_writer_t *w, const sof_toml_value_t *v) {
    sof_toml_error_t rc;
    frame_t *f = frame_at(w, 0);
    switch (v->type) {
    case SOF_TOML_STRING:
        return sof_toml_writer_string_bytes(w, v->as.text, v->length);
    case SOF_TOML_INTEGER:
        return sof_toml_writer_int(w, v->as.integer);
    case SOF_TOML_FLOAT:
        return sof_toml_writer_double(w, v->as.number);
    case SOF_TOML_BOOL:
        return sof_toml_writer_bool(w, v->as.boolean);
    case SOF_TOML_DATETIME:
    case SOF_TOML_DATETIME_LOCAL:
    case SOF_TOML_DATE_LOCAL:
    case SOF_TOML_TIME_LOCAL:
        return sof_toml_writer_datetime(w, v->type, &v->as.datetime);
    case SOF_TOML_ARRAY:
        if (f->kind == KIND_SECTION && w->has_key && is_section(v)) {
            rc = sof_toml_writer_table_array_begin(w);
            for (size_t i = 0; rc == SOF_TOML_OK && i < v->length; i++) {
                rc = sof_toml_writer_table_begin(w);
                if (rc == SOF_TOML_OK) {
                    rc = members_write(w, v->as.elements[i]);
                }
                if (rc == SOF_TOML_OK) {
                    rc = sof_toml_writer_table_end(w);
                }
            }
        } else {
            rc = sof_toml_writer_array_begin(w);
            for (size_t i = 0; rc == SOF_TOML_OK && i < v->length; i++) {
                rc = value_write(w, v->as.elements[i]);
            }
        }
        return rc == SOF_TOML_OK ? sof_toml_writer_array_end(w) : rc;
    case SOF_TOML_TABLE:
        if (f->kind == KIND_SECTION && !w->has_key) {
            return members_write(w, v);
        }
        if ((f->kind == KIND_SECTION && w->has_key && !v->is_inline) || f->kind == KIND_AOT) {
            rc = sof_toml_writer_table_begin(w);
            if (rc == SOF_TOML_OK) {
                rc = members_write(w, v);
            }
        } else {
            rc = sof_toml_writer_inline_begin(w);
            for (size_t i = 0; rc == SOF_TOML_OK && i < v->length; i++) {
                const sof_toml_member_t *m = &v->as.table.members[i];
                rc = sof_toml_writer_key_bytes(w, m->key, m->key_length);
                if (rc == SOF_TOML_OK) {
                    rc = value_write(w, m->value);
                }
            }
        }
        return rc == SOF_TOML_OK ? sof_toml_writer_table_end(w) : rc;
    default:
        return SOF_TOML_ERROR;
    }
}

sof_toml_error_t sof_toml_writer_array_begin(sof_toml_writer_t *w) {
    return container_begin(w, KIND_ARRAY);
}

sof_toml_error_t sof_toml_writer_array_end(sof_toml_writer_t *w) {
    frame_t *f = frame_at(w, 0);
    if (!usable(w) || f == NULL || w->has_key) {
        return SOF_TOML_ERROR;
    }
    if (f->kind == KIND_ARRAY) {
        return container_end(w, KIND_ARRAY, ']');
    }
    if (f->kind != KIND_AOT) {
        return SOF_TOML_ERROR;
    }
    if ((f->state & STATE_ITEMS) == 0) {
        // No tables: the array is written as key = [] in the section it is
        // in, with that section's header if it isn't written yet, which
        // can't be done once the section has sections of its own.
        frame_t *parent = frame_at(w, 1);
        if ((parent->state & STATE_SECTIONS) != 0) {
            return SOF_TOML_ERROR;
        }
        size_t mark = w->out.length;
        size_t key_at = f->path == 0 ? 0 : f->path + 1;
        sof_toml_error_t rc = SOF_TOML_OK;
        if ((parent->state & STATE_HEADER) == 0) {
            rc = header_write(w, 0, f->path);
        }
        if (rc == SOF_TOML_OK) {
            rc = put(&w->out, w->path.data + key_at, w->path.length - key_at);
        }
        if (rc == SOF_TOML_OK) {
            rc = put(&w->out, " = []\n", 6);
        }
        if (rc != SOF_TOML_OK) {
            sof_string_delete(&w->out, mark, w->out.length - mark);
            return rc;
        }
        parent->state |= STATE_HEADER | STATE_ITEMS;
    }
    sof_string_delete(&w->path, f->path, w->path.length - f->path);
    sof_stack_pop(&w->open, NULL);
    return finish(w);
}

sof_toml_error_t sof_toml_writer_bool(sof_toml_writer_t *w, int value) {
    return value ? scalar(w, "true", 4, 0) : scalar(w, "false", 5, 0);
}

sof_toml_error_t sof_toml_writer_clear(sof_toml_writer_t *w) {
    if (!usable(w)) {
        return SOF_TOML_ERROR;
    }
    sof_string_clear(&w->out);
    sof_string_clear(&w->path);
    sof_string_clear(&w->key);
    sof_stack_truncate(&w->open, 1);
    frame_at(w, 0)->state = STATE_HEADER;
    w->has_key = 0;
    w->holding = 0;
    w->written = 0;
    return SOF_TOML_OK;
}

int sof_toml_writer_complete(const sof_toml_writer_t *w) {
    return usable(w) && w->open.count == 1 && !w->has_key;
}

sof_toml_error_t sof_toml_writer_datetime(sof_toml_writer_t *w, sof_toml_type_t type, const sof_toml_datetime_t *dt) {
    char text[SOF_TOML_DATETIME_SIZE];
    size_t length = sof_toml_datetime_format(type, dt, text, sizeof(text));
    if (length == 0) {
        return SOF_TOML_ERROR;
    }
    return scalar(w, text, length, 0);
}

size_t sof_toml_writer_depth(const sof_toml_writer_t *w) {
    return usable(w) ? w->open.count - 1 : 0;
}

sof_toml_error_t sof_toml_writer_double(sof_toml_writer_t *w, double value) {
    // Room for ".0" after the longest number.
    char text[SOF_REAL_FORMAT_SIZE + 2];
    size_t length;
    if (isnan(value)) {
        return scalar(w, "nan", 3, 0);
    }
    if (isinf(value)) {
        return value < 0 ? scalar(w, "-inf", 4, 0) : scalar(w, "inf", 3, 0);
    }
    length = sof_real_format(value, text, sizeof(text) - 2);
    if (length == 0) {
        return SOF_TOML_ERROR;
    }
    // TOML takes a number without a decimal point or exponent as an integer.
    if (strpbrk(text, ".e") == NULL) {
        memcpy(text + length, ".0", 3);
        length += 2;
    }
    return scalar(w, text, length, 0);
}

sof_toml_error_t sof_toml_writer_flush(sof_toml_writer_t *w) {
    if (!usable(w)) {
        return SOF_TOML_ERROR;
    }
    if (w->write == NULL || w->out.length == 0) {
        return SOF_TOML_OK;
    }
    if (w->write(w->out.data, w->out.length, w->ctx) != 0) {
        return SOF_TOML_WRITE;
    }
    w->written += w->out.length;
    sof_string_clear(&w->out);
    return SOF_TOML_OK;
}

sof_toml_error_t sof_toml_writer_free(sof_toml_writer_t *w) {
    if (w == NULL) {
        return SOF_TOML_ERROR;
    }
    if (w->ready) {
        sof_string_free(&w->out);
        sof_string_free(&w->path);
        sof_string_free(&w->key);
        sof_stack_free(&w->open);
    }
    memset(w, 0, sizeof(*w));
    return SOF_TOML_OK;
}

sof_toml_error_t sof_toml_writer_init(sof_toml_writer_t *w, unsigned flags, sof_toml_write_func_t write, void *ctx, const sof_alloc_t *alloc) {
    if (w == NULL) {
        return SOF_TOML_ERROR;
    }
    memset(w, 0, sizeof(*w));
    if (!sof_alloc_valid(alloc)) {
        return SOF_TOML_ERROR;
    }
    frame_t root = {KIND_SECTION, STATE_HEADER, 0};
    if (sof_string_init(&w->out, OUT_CAPACITY, alloc) != SOF_STRING_OK || sof_string_init(&w->path, 0, alloc) != SOF_STRING_OK || sof_string_init(&w->key, 0, alloc) != SOF_STRING_OK || sof_stack_init(&w->open, sizeof(frame_t), 8, NULL, NULL, alloc) != SOF_STACK_OK || sof_stack_push(&w->open, &root) != SOF_STACK_OK) {
        sof_string_free(&w->out);
        sof_string_free(&w->path);
        sof_string_free(&w->key);
        sof_stack_free(&w->open);
        memset(w, 0, sizeof(*w));
        return SOF_TOML_NO_MEM;
    }
    w->flags = flags;
    w->write = write;
    w->ctx = ctx;
    w->ready = 1;
    return SOF_TOML_OK;
}

sof_toml_error_t sof_toml_writer_inline_begin(sof_toml_writer_t *w) {
    return container_begin(w, KIND_INLINE);
}

sof_toml_error_t sof_toml_writer_int(sof_toml_writer_t *w, int64_t value) {
    char text[24];
    int n = snprintf(text, sizeof(text), "%" PRId64, value);
    return scalar(w, text, (size_t)n, 0);
}

sof_toml_error_t sof_toml_writer_key(sof_toml_writer_t *w, const char *key) {
    if (key == NULL) {
        return SOF_TOML_ERROR;
    }
    return key_set(w, key, strlen(key));
}

sof_toml_error_t sof_toml_writer_key_bytes(sof_toml_writer_t *w, const void *key, size_t length) {
    return key_set(w, key, length);
}

const char *sof_toml_writer_output(const sof_toml_writer_t *w, size_t *length) {
    if (length != NULL) {
        *length = usable(w) ? w->out.length : 0;
    }
    return usable(w) ? w->out.data : NULL;
}

sof_toml_error_t sof_toml_writer_string(sof_toml_writer_t *w, const char *s) {
    if (s == NULL) {
        return SOF_TOML_ERROR;
    }
    return scalar(w, s, strlen(s), 1);
}

sof_toml_error_t sof_toml_writer_string_bytes(sof_toml_writer_t *w, const void *s, size_t length) {
    if (s == NULL && length != 0) {
        return SOF_TOML_ERROR;
    }
    return scalar(w, s != NULL ? s : "", length, 1);
}

sof_toml_error_t sof_toml_writer_table_array_begin(sof_toml_writer_t *w) {
    frame_t *f = frame_at(w, 0);
    if (!usable(w)) {
        return SOF_TOML_ERROR;
    }
    if (f->kind != KIND_SECTION || !w->has_key) {
        return container_begin(w, KIND_ARRAY);
    }
    // Nothing is written until the first table: the array's key goes in the
    // path, for its elements' headers.
    undo_t u;
    undo_save(w, &u);
    frame_t frame = {KIND_AOT, 0, w->path.length};
    sof_toml_error_t rc = SOF_TOML_OK;
    if (w->path.length > 0) {
        rc = put(&w->path, ".", 1);
    }
    if (rc == SOF_TOML_OK) {
        rc = put(&w->path, w->key.data, w->key.length);
    }
    if (rc == SOF_TOML_OK && sof_stack_push(&w->open, &frame) != SOF_STACK_OK) {
        rc = SOF_TOML_NO_MEM;
    }
    if (rc != SOF_TOML_OK) {
        undo_restore(w, &u);
        return rc;
    }
    w->has_key = 0;
    return SOF_TOML_OK;
}

sof_toml_error_t sof_toml_writer_table_begin(sof_toml_writer_t *w) {
    frame_t *f = frame_at(w, 0);
    undo_t u;
    sof_toml_error_t rc = SOF_TOML_OK;
    if (!usable(w)) {
        return SOF_TOML_ERROR;
    }
    if (f->kind == KIND_SECTION && w->has_key) {
        // A section: its key goes in the path, and its header waits for its
        // first key/value pair.
        undo_save(w, &u);
        frame_t frame = {KIND_SECTION, 0, w->path.length};
        if (w->path.length > 0) {
            rc = put(&w->path, ".", 1);
        }
        if (rc == SOF_TOML_OK) {
            rc = put(&w->path, w->key.data, w->key.length);
        }
        f->state |= STATE_SECTIONS;
        if (rc == SOF_TOML_OK && sof_stack_push(&w->open, &frame) != SOF_STACK_OK) {
            rc = SOF_TOML_NO_MEM;
        }
        if (rc != SOF_TOML_OK) {
            undo_restore(w, &u);
            return rc;
        }
        w->has_key = 0;
        return SOF_TOML_OK;
    }
    if (f->kind == KIND_AOT) {
        // The next element of an array of tables, whose header is written
        // now, so that an element with nothing in it is still one.
        undo_save(w, &u);
        frame_t frame = {KIND_SECTION, STATE_HEADER, w->path.length};
        frame_at(w, 1)->state |= STATE_SECTIONS;
        f->state |= STATE_ITEMS;
        rc = header_write(w, 1, w->path.length);
        if (rc == SOF_TOML_OK && sof_stack_push(&w->open, &frame) != SOF_STACK_OK) {
            rc = SOF_TOML_NO_MEM;
        }
        if (rc != SOF_TOML_OK) {
            undo_restore(w, &u);
            return rc;
        }
        return finish(w);
    }
    return container_begin(w, KIND_INLINE);
}

sof_toml_error_t sof_toml_writer_table_end(sof_toml_writer_t *w) {
    frame_t *f = frame_at(w, 0);
    if (!usable(w) || w->has_key) {
        return SOF_TOML_ERROR;
    }
    if (f->kind == KIND_INLINE) {
        return container_end(w, KIND_INLINE, '}');
    }
    if (f->kind != KIND_SECTION || w->open.count == 1) {
        return SOF_TOML_ERROR;
    }
    // A table with nothing in it is written as its header alone.
    if ((f->state & (STATE_HEADER | STATE_SECTIONS)) == 0) {
        size_t mark = w->out.length;
        sof_toml_error_t rc = header_write(w, 0, w->path.length);
        if (rc != SOF_TOML_OK) {
            sof_string_delete(&w->out, mark, w->out.length - mark);
            return rc;
        }
    }
    sof_string_delete(&w->path, f->path, w->path.length - f->path);
    sof_stack_pop(&w->open, NULL);
    return finish(w);
}

sof_toml_error_t sof_toml_writer_value(sof_toml_writer_t *w, const sof_toml_value_t *v) {
    undo_t u;
    sof_string_t key;
    if (!usable(w) || v == NULL) {
        return SOF_TOML_ERROR;
    }
    // The keys of the value's members are kept in the writer's key as they
    // are written, so the one waiting, if there is one, is copied, to put
    // back if the value can't be written.
    if (sof_string_dup(&key, &w->key, &w->out.alloc) != SOF_STRING_OK) {
        return SOF_TOML_NO_MEM;
    }
    undo_save(w, &u);
    w->holding = 1;
    sof_toml_error_t rc = value_write(w, v);
    w->holding = 0;
    if (rc != SOF_TOML_OK) {
        undo_restore(w, &u);
        sof_string_clear(&w->key);
        put(&w->key, key.data, key.length);
    }
    sof_string_free(&key);
    return rc == SOF_TOML_OK ? finish(w) : rc;
}
