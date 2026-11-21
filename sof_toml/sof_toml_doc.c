/**
 * @file sof_toml_doc.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief The TOML document of sof_toml: the parser, the functions that
 * read its values, and those for dates and times.
 * @details The parser reads the text once, from start to end, building the
 * tree as it goes. Each value is a node in the arena: the public
 * sof_toml_value_t, then the room its members or elements have, and flags
 * saying how a table or array came to be, which decide what may later be
 * added to it. A table's members, and an array's elements, are kept in an
 * array in the arena that doubles as it fills, since a table defined by a
 * header can be added to by later headers anywhere in the text.
 *
 * Tables with more than SOF_TOML_INDEX_MIN members are put in the index,
 * one hashmap for the whole document, which the parser uses too, to find a
 * key already defined. Its keys are the table and the key, kept in the
 * arena, and its data the member's place in the table, which stays the same
 * when the members array moves.
 *
 * Arrays and inline tables are read by recursion, to SOF_TOML_DEPTH_MAX
 * deep; dotted keys and table headers are followed a part at a time,
 * without it.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sof_hash.h"
#include "sof_real.h"
#include "sof_toml.h"
#include "sof_utf.h"

/// @brief The usable size of each block of the document's arena.
#define DOC_BLOCK 8192

/// @brief The members or elements a table or array has room for when it
/// first gets one.
#define ITEMS_MIN 4

/// @brief The room a float's text with underscores has on the stack while
/// they are taken out; longer ones are allocated.
#define FLOAT_TEXT 128

/// @brief A byte repeated through a 64-bit word.
#define BYTES(b) (UINT64_C(0x0101010101010101) * (b))

/// @brief A table that a table header's path went through without naming
/// it: [a.b] makes a so. A [a] header may then define it once.
#define NODE_IMPLICIT 1
/// @brief A table defined by a header of its own, [a], or an element of an
/// array of tables.
#define NODE_EXPLICIT 2
/// @brief A table made by a dotted key, a.b = 1, which more dotted keys may
/// add to, and headers may go through, but not define.
#define NODE_DOTTED 4
/// @brief An inline table or an inline array, which is whole once it ends:
/// nothing can be added to it.
#define NODE_SEALED 8
/// @brief An array of tables, made by [[a]] headers, which more of them add
/// to.
#define NODE_AOT 16

/// @brief A value in the arena, with what the parser needs to grow it.
typedef struct node_t {
    /// @brief The value, first, so a value is its node.
    sof_toml_value_t value;
    /// @brief The members or elements there is room for.
    size_t capacity;
    /// @brief The NODE_ flags.
    unsigned flags;
} node_t;

/// @brief A key in the index: a table and one of its keys.
typedef struct index_key_t {
    const sof_toml_value_t *table;
    const char *key;
    size_t length;
} index_key_t;

/// @brief One part of a key, decoded, in the arena.
typedef struct part_t {
    const char *text;
    size_t length;
    /// @brief Where it begins in the text, for an error and its span.
    size_t at;
    /// @brief Where it ends in the text, for its span.
    size_t end;
} part_t;

/// @brief The state of a parse.
typedef struct parser_t {
    sof_toml_doc_t *d;
    /// @brief The text and its size.
    const char *s;
    size_t n;
    /// @brief Where the next byte to read is.
    size_t i;
    /// @brief The root table.
    node_t *root;
    /// @brief The table key/value pairs go in: the root, or the table the
    /// last header named.
    node_t *current;
    /// @brief If current is an element of an array of tables, the array,
    /// whose span grows with it; otherwise NULL.
    node_t *aot;
    /// @brief The arrays and inline tables open.
    size_t depth;
    /// @brief The first error, what it is, and where.
    sof_toml_error_t error;
    const char *message;
    size_t at;
} parser_t;

static sof_toml_error_t array_push(parser_t *p, node_t *array, node_t *element);
static int bare(int c);
static sof_toml_error_t basic_string(parser_t *p);
static sof_toml_error_t code_add(parser_t *p, uint32_t code);
static sof_toml_error_t comment(parser_t *p);
static char *copy_text(parser_t *p, const char *text, size_t length);
static int datetime_scan(const char *s, size_t n, size_t *used, sof_toml_type_t *type, sof_toml_datetime_t *dt);
static int datetime_valid(sof_toml_type_t type, const sof_toml_datetime_t *dt);
static int days_in_month(int year, int month);
static int digit(int c);
static size_t digit_run(const char *t, size_t length, size_t *k, int radix);
static int digits(const char *s, size_t n, size_t at, size_t count, int *value);
static sof_toml_error_t dotted_table(parser_t *p, node_t **table, const part_t *k);
static sof_toml_error_t escape(parser_t *p);
static size_t escape_decode(const char *s, size_t n, uint32_t *code);
static int escaped_equal(const char *s, size_t n, const char *key, size_t length);
static sof_toml_error_t fail(parser_t *p, sof_toml_error_t code, size_t at, const char *message);
static const sof_toml_member_t *find(const sof_toml_value_t *table, const void *key, size_t length);
static sof_toml_error_t float_convert(parser_t *p, const char *t, size_t length, double *value);
static sof_toml_error_t header(parser_t *p);
static sof_toml_error_t header_table(parser_t *p, node_t **table, const part_t *k);
static int hex_value(int c);
static sof_toml_error_t index_add(parser_t *p, const sof_toml_value_t *table, size_t position);
static int index_equal(const void *a, const void *b, void *ctx);
static size_t index_hash(const void *key, void *ctx);
static sof_toml_error_t inline_table(parser_t *p, node_t **out);
static sof_toml_error_t keyval(parser_t *p, node_t *table);
static sof_toml_error_t line_end(parser_t *p);
static sof_toml_error_t literal_string(parser_t *p);
static sof_toml_error_t member_add(parser_t *p, node_t *table, const part_t *k, node_t *value);
static sof_toml_error_t ml_basic_string(parser_t *p);
static sof_toml_error_t ml_literal_string(parser_t *p);
static int newline_at(const parser_t *p, size_t at);
static node_t *node_new(parser_t *p, sof_toml_type_t type, unsigned flags);
static sof_toml_error_t number(parser_t *p, node_t **out);
static sof_toml_error_t parse_array(parser_t *p, node_t **out);
static sof_toml_error_t parse_text(parser_t *p);
static int peek(const parser_t *p, size_t k);
static size_t plain_run(const char *s, size_t n, unsigned char stop, unsigned char also);
static void reset(sof_toml_doc_t *d);
static sof_toml_error_t simple_key(parser_t *p, part_t *k);
static sof_toml_error_t skip_wcn(parser_t *p);
static void skip_ws(parser_t *p);
static sof_toml_error_t string_value(parser_t *p, node_t **out);
static sof_toml_error_t text_add(parser_t *p, const char *bytes, size_t length);
static int usable(const sof_toml_doc_t *d);
static sof_toml_error_t value(parser_t *p, node_t **out);
static sof_toml_error_t value_read(parser_t *p, node_t **out);

/// @brief How the index hashes and compares its keys.
static const sof_hashmap_key_callbacks_t index_keys = {index_hash, index_equal, NULL, NULL};

/// @brief Adds an element to an array, making room for it.
static sof_toml_error_t array_push(parser_t *p, node_t *array, node_t *element) {
    sof_toml_value_t *a = &array->value;
    const sof_toml_value_t **elements = (const sof_toml_value_t **)a->as.elements;
    if (a->length == array->capacity) {
        size_t capacity = array->capacity == 0 ? ITEMS_MIN : array->capacity * 2;
        elements = sof_mem_arena_realloc(&p->d->arena, elements, array->capacity * sizeof(*elements), capacity * sizeof(*elements));
        if (elements == NULL) {
            return fail(p, SOF_TOML_NO_MEM, p->i, NULL);
        }
        a->as.elements = elements;
        array->capacity = capacity;
    }
    elements[a->length++] = &element->value;
    return SOF_TOML_OK;
}

/// @brief Says whether c can be in a bare key: A-Z, a-z, 0-9, - and _.
static int bare(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || digit(c) || c == '-' || c == '_';
}

/// @brief Reads a basic string, "...", on one line, decoding it into the
/// document's text.
static sof_toml_error_t basic_string(parser_t *p) {
    size_t start = p->i;
    sof_toml_error_t rc;
    sof_string_clear(&p->d->text);
    size_t run = ++p->i;
    for (;;) {
        if (p->i >= p->n) {
            return fail(p, SOF_TOML_SYNTAX, start, "a string has no closing quote");
        }
        unsigned char c = (unsigned char)p->s[p->i];
        if (c == '"' || c == '\\') {
            if ((rc = text_add(p, p->s + run, p->i - run)) != SOF_TOML_OK) {
                return rc;
            }
            if (c == '"') {
                p->i++;
                return SOF_TOML_OK;
            }
            if ((rc = escape(p)) != SOF_TOML_OK) {
                return rc;
            }
            run = p->i;
        } else if (c == '\n' || c == '\r') {
            return fail(p, SOF_TOML_SYNTAX, start, "a string has no closing quote on its line");
        } else if ((c < 0x20 && c != '\t') || c == 0x7f) {
            return fail(p, SOF_TOML_SYNTAX, p->i, "a string holds a control character");
        } else {
            p->i++;
            p->i += plain_run(p->s + p->i, p->n - p->i, '"', '\\');
        }
    }
}

/// @brief Adds a code point to the document's text, in UTF-8.
static sof_toml_error_t code_add(parser_t *p, uint32_t code) {
    char bytes[SOF_UTF8_SIZE_MAX];
    return text_add(p, bytes, sof_utf8_encode(code, bytes, sizeof(bytes)));
}

/// @brief Reads a comment, from its # to the end of its line, which is
/// left to be read.
static sof_toml_error_t comment(parser_t *p) {
    for (p->i++; p->i < p->n; p->i++) {
        unsigned char c = (unsigned char)p->s[p->i];
        if (c == '\n' || (c == '\r' && newline_at(p, p->i))) {
            break;
        }
        if ((c < 0x20 && c != '\t') || c == 0x7f) {
            return fail(p, SOF_TOML_SYNTAX, p->i, "a comment holds a control character");
        }
        p->i += plain_run(p->s + p->i + 1, p->n - p->i - 1, 0, 0);
    }
    return SOF_TOML_OK;
}

/// @brief Copies text, of length bytes, into the arena, with a NUL after
/// it.
/// @return The copy, or NULL, failing the parse, if there isn't the memory.
static char *copy_text(parser_t *p, const char *text, size_t length) {
    char *copy = sof_mem_arena_alloc(&p->d->arena, length + 1);
    if (copy == NULL) {
        fail(p, SOF_TOML_NO_MEM, p->i, NULL);
        return NULL;
    }
    if (length > 0) {
        memcpy(copy, text, length);
    }
    copy[length] = '\0';
    return copy;
}

/// @brief Reads a date and time at the start of the n bytes at s: a local
/// date, a local time, or a date and time with or without an offset.
/// @return 1, with the bytes read in *used and the value in *type and *dt,
/// or 0 if the bytes don't begin with one, or a field is out of its range.
static int datetime_scan(const char *s, size_t n, size_t *used, sof_toml_type_t *type, sof_toml_datetime_t *dt) {
    sof_toml_datetime_t t;
    sof_toml_type_t kind;
    size_t i = 0;
    int has_date = 0;
    memset(&t, 0, sizeof(t));
    if (n >= 5 && digits(s, n, 0, 4, &t.year) && s[4] == '-') {
        if (!digits(s, n, 5, 2, &t.month) || n < 8 || s[7] != '-' || !digits(s, n, 8, 2, &t.day)) {
            return 0;
        }
        has_date = 1;
        i = 10;
        // A time follows a T or t, or a space and then what begins a time:
        // two digits and a colon. After a date alone, a space ends it.
        if (i < n && (s[i] == 'T' || s[i] == 't' || (s[i] == ' ' && i + 3 < n && digit(s[i + 1]) && digit(s[i + 2]) && s[i + 3] == ':'))) {
            i++;
        } else {
            if (!datetime_valid(SOF_TOML_DATE_LOCAL, &t)) {
                return 0;
            }
            *used = i;
            *type = SOF_TOML_DATE_LOCAL;
            *dt = t;
            return 1;
        }
    }
    if (!digits(s, n, i, 2, &t.hour) || i + 2 >= n || s[i + 2] != ':' || !digits(s, n, i + 3, 2, &t.minute)) {
        return 0;
    }
    i += 5;
    if (i < n && s[i] == ':') {
        if (!digits(s, n, i + 1, 2, &t.second)) {
            return 0;
        }
        i += 3;
        if (i < n && s[i] == '.') {
            size_t start = ++i, kept = 0;
            uint32_t ns = 0;
            while (i < n && digit(s[i])) {
                if (kept < 9) {
                    ns = ns * 10 + (uint32_t)(s[i] - '0');
                    kept++;
                }
                i++;
            }
            if (i == start) {
                return 0;
            }
            for (; kept < 9; kept++) {
                ns *= 10;
            }
            t.nanosecond = ns;
        }
    }
    if (!has_date) {
        kind = SOF_TOML_TIME_LOCAL;
    } else if (i < n && (s[i] == 'Z' || s[i] == 'z')) {
        kind = SOF_TOML_DATETIME;
        i++;
    } else if (i < n && (s[i] == '+' || s[i] == '-')) {
        int hours, minutes;
        if (!digits(s, n, i + 1, 2, &hours) || i + 3 >= n || s[i + 3] != ':' || !digits(s, n, i + 4, 2, &minutes) || hours > 23 || minutes > 59) {
            return 0;
        }
        t.offset = (s[i] == '-' ? -1 : 1) * (hours * 60 + minutes);
        kind = SOF_TOML_DATETIME;
        i += 6;
    } else {
        kind = SOF_TOML_DATETIME_LOCAL;
    }
    if (!datetime_valid(kind, &t)) {
        return 0;
    }
    *used = i;
    *type = kind;
    *dt = t;
    return 1;
}

/// @brief Says whether the fields of dt that type uses are in their
/// ranges.
static int datetime_valid(sof_toml_type_t type, const sof_toml_datetime_t *dt) {
    int has_date = type == SOF_TOML_DATETIME || type == SOF_TOML_DATETIME_LOCAL || type == SOF_TOML_DATE_LOCAL;
    int has_time = type == SOF_TOML_DATETIME || type == SOF_TOML_DATETIME_LOCAL || type == SOF_TOML_TIME_LOCAL;
    if (!has_date && !has_time) {
        return 0;
    }
    if (has_date && (dt->year < 0 || dt->year > 9999 || dt->month < 1 || dt->month > 12 || dt->day < 1 || dt->day > days_in_month(dt->year, dt->month))) {
        return 0;
    }
    if (has_time && (dt->hour < 0 || dt->hour > 23 || dt->minute < 0 || dt->minute > 59 || dt->second < 0 || dt->second > 60 || dt->nanosecond > 999999999)) {
        return 0;
    }
    return type != SOF_TOML_DATETIME || (dt->offset > -1440 && dt->offset < 1440);
}

/// @brief Gives the days in a month of a year of the Gregorian calendar.
static int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
        return 29;
    }
    return days[month - 1];
}

/// @brief Says whether c is a decimal digit.
static int digit(int c) {
    return c >= '0' && c <= '9';
}

/// @brief Reads digits of a radix with single underscores between them, as
/// DIGIT *( DIGIT / underscore DIGIT ), from t[*k], moving *k past them.
/// @return The number of digits, or 0 if there are none, or an underscore
/// isn't between two digits.
static size_t digit_run(const char *t, size_t length, size_t *k, int radix) {
    size_t count = 0;
    size_t i = *k;
    for (;;) {
        if (i < length && hex_value((unsigned char)t[i]) >= 0 && hex_value((unsigned char)t[i]) < radix) {
            count++;
            i++;
        } else if (i < length && t[i] == '_' && count > 0 && i + 1 < length && hex_value((unsigned char)t[i + 1]) >= 0 && hex_value((unsigned char)t[i + 1]) < radix) {
            i++;
        } else {
            break;
        }
    }
    if (count == 0 || (i < length && t[i] == '_')) {
        return 0;
    }
    *k = i;
    return count;
}

/// @brief Reads count decimal digits at s[at], all within n bytes.
/// @return 1, with their value in *value, or 0.
static int digits(const char *s, size_t n, size_t at, size_t count, int *value) {
    int v = 0;
    if (at + count > n) {
        return 0;
    }
    for (size_t k = 0; k < count; k++) {
        if (!digit(s[at + k])) {
            return 0;
        }
        v = v * 10 + (s[at + k] - '0');
    }
    *value = v;
    return 1;
}

/// @brief Follows one part of a dotted key, before its last, from *table:
/// to the table the part names, made if there isn't one.
static sof_toml_error_t dotted_table(parser_t *p, node_t **table, const part_t *k) {
    const sof_toml_member_t *m = find(&(*table)->value, k->text, k->length);
    if (m == NULL) {
        node_t *node = node_new(p, SOF_TOML_TABLE, NODE_DOTTED);
        if (node != NULL) {
            node->value.span.offset = k->at;
        }
        sof_toml_error_t rc = node != NULL ? member_add(p, *table, k, node) : SOF_TOML_NO_MEM;
        *table = node;
        return rc;
    }
    node_t *node = (node_t *)(void *)m->value;
    if (node->value.type != SOF_TOML_TABLE) {
        return fail(p, SOF_TOML_DUPLICATE, k->at, "a dotted key goes through a key that isn't a table");
    }
    // Only tables that dotted keys made can be added to by dotted keys; a
    // table that a header defined, or went through, is defined elsewhere.
    if ((node->flags & NODE_DOTTED) == 0 || (node->flags & NODE_SEALED) != 0) {
        return fail(p, SOF_TOML_DUPLICATE, k->at, "a dotted key adds to a table defined elsewhere");
    }
    *table = node;
    return SOF_TOML_OK;
}

/// @brief Reads an escape in a basic string, at its backslash, adding what
/// it stands for to the document's text.
static sof_toml_error_t escape(parser_t *p) {
    size_t at = p->i;
    uint32_t code;
    size_t used = escape_decode(p->s + p->i, p->n - p->i, &code);
    if (used == 0) {
        return fail(p, SOF_TOML_SYNTAX, at, "a string has an escape TOML doesn't have");
    }
    p->i += used;
    return code_add(p, code);
}

/// @brief Decodes the escape at the start of the n bytes at s, at its
/// backslash: \b, \t, \n, \f, \r, \e, \", \\, or \x, \u or \U and
/// hexadecimal digits, 2, 4 or 8 of them, for a Unicode scalar value.
/// @return The bytes the escape takes, with what it stands for in *code,
/// or 0 if it isn't one.
static size_t escape_decode(const char *s, size_t n, uint32_t *code) {
    static const char simple[] = "b\bt\tn\nf\fr\re\x1b\"\"\\\\";
    if (n < 2 || s[0] != '\\') {
        return 0;
    }
    for (size_t k = 0; k + 1 < sizeof(simple); k += 2) {
        if (s[1] == simple[k]) {
            *code = (unsigned char)simple[k + 1];
            return 2;
        }
    }
    size_t hex = s[1] == 'x' ? 2 : s[1] == 'u' ? 4 : s[1] == 'U' ? 8 : 0;
    uint32_t c = 0;
    if (hex == 0 || n < 2 + hex) {
        return 0;
    }
    for (size_t k = 0; k < hex; k++) {
        int v = hex_value((unsigned char)s[2 + k]);
        if (v < 0) {
            return 0;
        }
        c = c << 4 | (uint32_t)v;
    }
    if (!sof_utf_valid(c)) {
        return 0;
    }
    *code = c;
    return 2 + hex;
}

/// @brief Compares the body of a basic string, with its escapes, with a
/// key.
/// @return 1 if they are equal, 0 if not, or -1 if the body has an escape
/// TOML doesn't.
static int escaped_equal(const char *s, size_t n, const char *key, size_t length) {
    size_t i = 0, k = 0;
    while (i < n) {
        char bytes[SOF_UTF8_SIZE_MAX];
        size_t count = 1;
        if (s[i] == '\\') {
            uint32_t code;
            size_t used = escape_decode(s + i, n - i, &code);
            if (used == 0) {
                return -1;
            }
            count = sof_utf8_encode(code, bytes, sizeof(bytes));
            i += used;
        } else {
            bytes[0] = s[i++];
        }
        if (k + count > length || memcmp(key + k, bytes, count) != 0) {
            return 0;
        }
        k += count;
    }
    return k == length;
}

/// @brief Records the parse's first error.
/// @return code.
static sof_toml_error_t fail(parser_t *p, sof_toml_error_t code, size_t at, const char *message) {
    if (p->error == SOF_TOML_OK) {
        p->error = code;
        p->at = at;
        p->message = message;
    }
    return code;
}

/// @brief Finds a table's member by its key, in the index or by searching.
static const sof_toml_member_t *find(const sof_toml_value_t *table, const void *key, size_t length) {
    if (table->as.table.index != NULL) {
        index_key_t probe = {table, key, length};
        sof_hashmap_element_t *e = sof_hashmap_find(table->as.table.index, &probe);
        return e != NULL ? &table->as.table.members[(uintptr_t)e->data] : NULL;
    }
    for (size_t i = 0; i < table->length; i++) {
        const sof_toml_member_t *m = &table->as.table.members[i];
        if (m->key_length == length && (length == 0 || memcmp(m->key, key, length) == 0)) {
            return m;
        }
    }
    return NULL;
}

/// @brief Converts a float's text, checked against the grammar, to the
/// nearest double, with sof_real.
/// @details A float of up to 19 significant digits, which fit in 64 bits,
/// is read here, underscores and all, and given to sof_real as an integer
/// and a power of ten. A longer one is copied without its underscores,
/// which is then sof_real_parse()'s grammar, and read by that.
static sof_toml_error_t float_convert(parser_t *p, const char *t, size_t length, double *value) {
    char stack[FLOAT_TEXT];
    size_t i = 0;
    int negative = 0, fraction = 0, digits = 0;
    uint64_t m = 0;
    long power = 0;
    if (t[0] == '+' || t[0] == '-') {
        negative = t[0] == '-';
        i = 1;
    }
    // The digits before and after the point, without the zeros before the
    // first that isn't one; each after the point takes one from the power.
    for (; i < length && t[i] != 'e' && t[i] != 'E' && digits <= 19; i++) {
        if (t[i] == '.') {
            fraction = 1;
        } else if (t[i] != '_') {
            if (digits == 0 && t[i] == '0') {
                power -= fraction;
                continue;
            }
            if (++digits <= 19) {
                m = m * 10 + (uint64_t)(t[i] - '0');
                power -= fraction;
            }
        }
    }
    if (digits <= 19) {
        if (i < length) {
            // The exponent, which stops growing where any number of 19
            // digits is infinite or 0.
            int exponent_negative = 0;
            long exponent = 0;
            i++;
            if (t[i] == '+' || t[i] == '-') {
                exponent_negative = t[i] == '-';
                i++;
            }
            for (; i < length; i++) {
                if (t[i] != '_' && exponent < 100000) {
                    exponent = exponent * 10 + (t[i] - '0');
                }
            }
            power += exponent_negative ? -exponent : exponent;
        }
        power = power > 100000 ? 100000 : power < -100000 ? -100000 : power;
        return sof_real_from_decimal(m, (int)power, negative, value) == SOF_REAL_RANGE ? SOF_TOML_RANGE : SOF_TOML_OK;
    }
    char *buffer = length <= sizeof(stack) ? stack : sof_alloc_malloc(&p->d->alloc, length);
    if (buffer == NULL) {
        return fail(p, SOF_TOML_NO_MEM, p->i, NULL);
    }
    size_t n = 0;
    for (size_t i = 0; i < length; i++) {
        if (t[i] != '_') {
            buffer[n++] = t[i];
        }
    }
    sof_real_error_t rc = sof_real_parse(buffer, n, value, NULL);
    if (buffer != stack) {
        sof_alloc_free(&p->d->alloc, buffer, length);
    }
    return rc == SOF_REAL_RANGE ? SOF_TOML_RANGE : SOF_TOML_OK;
}

/// @brief Reads a table header, [key] or [[key]], making the table it names
/// the one key/value pairs go in.
static sof_toml_error_t header(parser_t *p) {
    size_t start = p->i;
    int aot = peek(p, 1) == '[';
    node_t *table = p->root;
    part_t k;
    sof_toml_error_t rc;
    p->i += aot ? 2 : 1;
    skip_ws(p);
    if ((rc = simple_key(p, &k)) != SOF_TOML_OK) {
        return rc;
    }
    for (;;) {
        skip_ws(p);
        if (peek(p, 0) != '.') {
            break;
        }
        if ((rc = header_table(p, &table, &k)) != SOF_TOML_OK) {
            return rc;
        }
        p->i++;
        skip_ws(p);
        if ((rc = simple_key(p, &k)) != SOF_TOML_OK) {
            return rc;
        }
    }
    if (aot) {
        if (peek(p, 0) != ']' || peek(p, 1) != ']') {
            return fail(p, SOF_TOML_SYNTAX, p->i, "an array of tables' header doesn't end with ]]");
        }
        p->i += 2;
    } else {
        if (peek(p, 0) != ']') {
            return fail(p, SOF_TOML_SYNTAX, p->i, "a table header doesn't end with ]");
        }
        p->i++;
    }
    const sof_toml_member_t *m = find(&table->value, k.text, k.length);
    node_t *node = m != NULL ? (node_t *)(void *)m->value : NULL;
    if (!aot) {
        if (node == NULL) {
            node = node_new(p, SOF_TOML_TABLE, NODE_EXPLICIT);
            if (node == NULL || (rc = member_add(p, table, &k, node)) != SOF_TOML_OK) {
                return SOF_TOML_NO_MEM;
            }
        } else if (node->value.type == SOF_TOML_TABLE && (node->flags & (NODE_EXPLICIT | NODE_DOTTED | NODE_SEALED)) == 0) {
            node->flags |= NODE_EXPLICIT;
        } else {
            return fail(p, SOF_TOML_DUPLICATE, k.at, "a table is defined twice");
        }
        p->aot = NULL;
    } else {
        node_t *array = node;
        if (array == NULL) {
            array = node_new(p, SOF_TOML_ARRAY, NODE_AOT);
            if (array == NULL || (rc = member_add(p, table, &k, array)) != SOF_TOML_OK) {
                return SOF_TOML_NO_MEM;
            }
            array->value.span.offset = start;
        } else if (array->value.type != SOF_TOML_ARRAY || (array->flags & NODE_AOT) == 0) {
            return fail(p, SOF_TOML_DUPLICATE, k.at, "an array of tables has the key of another value");
        }
        node = node_new(p, SOF_TOML_TABLE, NODE_EXPLICIT);
        if (node == NULL || (rc = array_push(p, array, node)) != SOF_TOML_OK) {
            return fail(p, SOF_TOML_NO_MEM, p->i, NULL);
        }
        array->value.span.length = p->i - array->value.span.offset;
        p->aot = array;
    }
    // The table's span is its header, until key/value pairs under it
    // stretch it.
    node->value.span.offset = start;
    node->value.span.length = p->i - start;
    p->current = node;
    return SOF_TOML_OK;
}

/// @brief Follows one part of a table header's key, before its last, from
/// *table: to the table the part names, made if there isn't one, or the
/// last element of an array of tables.
static sof_toml_error_t header_table(parser_t *p, node_t **table, const part_t *k) {
    const sof_toml_member_t *m = find(&(*table)->value, k->text, k->length);
    if (m == NULL) {
        node_t *node = node_new(p, SOF_TOML_TABLE, NODE_IMPLICIT);
        if (node != NULL) {
            node->value.span.offset = k->at;
        }
        sof_toml_error_t rc = node != NULL ? member_add(p, *table, k, node) : SOF_TOML_NO_MEM;
        *table = node;
        return rc;
    }
    node_t *node = (node_t *)(void *)m->value;
    if (node->value.type == SOF_TOML_TABLE && (node->flags & NODE_SEALED) == 0) {
        *table = node;
    } else if (node->value.type == SOF_TOML_ARRAY && (node->flags & NODE_AOT) != 0) {
        *table = (node_t *)(void *)node->value.as.elements[node->value.length - 1];
    } else if (node->value.type == SOF_TOML_TABLE) {
        return fail(p, SOF_TOML_DUPLICATE, k->at, "a table header adds to an inline table");
    } else {
        return fail(p, SOF_TOML_DUPLICATE, k->at, "a table header goes through a key that isn't a table");
    }
    return SOF_TOML_OK;
}

/// @brief Gives the value of a hexadecimal digit, either case.
/// @return 0 to 15, or -1 if c isn't one.
static int hex_value(int c) {
    if (digit(c)) {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/// @brief Puts a table's member, at position in its members, in the index,
/// which is made the first time.
static sof_toml_error_t index_add(parser_t *p, const sof_toml_value_t *table, size_t position) {
    sof_toml_doc_t *d = p->d;
    if (d->index == NULL) {
        sof_hashmap_t *map = sof_mem_arena_alloc(&d->arena, sizeof(*map));
        if (map == NULL || sof_hashmap_init(map, &index_keys, NULL, NULL, 4 * SOF_TOML_INDEX_MIN, &d->alloc) != SOF_HASHMAP_OK) {
            return fail(p, SOF_TOML_NO_MEM, p->i, NULL);
        }
        d->index = map;
    }
    const sof_toml_member_t *m = &table->as.table.members[position];
    index_key_t *key = sof_mem_arena_alloc(&d->arena, sizeof(*key));
    if (key == NULL) {
        return fail(p, SOF_TOML_NO_MEM, p->i, NULL);
    }
    key->table = table;
    key->key = m->key;
    key->length = m->key_length;
    sof_hashmap_element_t *e = sof_hashmap_insert(d->index, key, NULL);
    if (e == NULL) {
        return fail(p, SOF_TOML_NO_MEM, p->i, NULL);
    }
    e->data = (void *)(uintptr_t)position;
    return SOF_TOML_OK;
}

/// @brief Says whether two index keys are of the same table and key.
static int index_equal(const void *a, const void *b, void *ctx) {
    const index_key_t *ka = a, *kb = b;
    (void)ctx;
    return ka->table == kb->table && ka->length == kb->length && (ka->length == 0 || memcmp(ka->key, kb->key, ka->length) == 0);
}

/// @brief Hashes an index key by its key, seeded with its table's address.
static size_t index_hash(const void *key, void *ctx) {
    const index_key_t *k = key;
    (void)ctx;
    return (size_t)sof_hash_bytes(k->key, k->length, (uint64_t)(uintptr_t)k->table);
}

/// @brief Reads an inline table, {...}, which TOML 1.1 lets span lines and
/// end with a comma.
static sof_toml_error_t inline_table(parser_t *p, node_t **out) {
    size_t start = p->i;
    sof_toml_error_t rc;
    node_t *table = node_new(p, SOF_TOML_TABLE, NODE_EXPLICIT);
    if (table == NULL) {
        return SOF_TOML_NO_MEM;
    }
    p->i++;
    for (;;) {
        if ((rc = skip_wcn(p)) != SOF_TOML_OK) {
            return rc;
        }
        if (peek(p, 0) == '}') {
            break;
        }
        if (p->i >= p->n) {
            return fail(p, SOF_TOML_SYNTAX, start, "an inline table has no closing }");
        }
        if ((rc = keyval(p, table)) != SOF_TOML_OK || (rc = skip_wcn(p)) != SOF_TOML_OK) {
            return rc;
        }
        if (peek(p, 0) == ',') {
            p->i++;
        } else if (peek(p, 0) != '}') {
            return p->i < p->n ? fail(p, SOF_TOML_SYNTAX, p->i, "expected , or } in an inline table") : fail(p, SOF_TOML_SYNTAX, start, "an inline table has no closing }");
        }
    }
    p->i++;
    table->flags |= NODE_SEALED;
    table->value.is_inline = 1;
    *out = table;
    return SOF_TOML_OK;
}

/// @brief Reads a key/value pair, key = value, putting it in table, or in
/// the tables its dotted key names in table.
static sof_toml_error_t keyval(parser_t *p, node_t *table) {
    part_t k;
    node_t *v;
    sof_toml_error_t rc = simple_key(p, &k);
    if (rc != SOF_TOML_OK) {
        return rc;
    }
    for (;;) {
        skip_ws(p);
        if (peek(p, 0) != '.') {
            break;
        }
        if ((rc = dotted_table(p, &table, &k)) != SOF_TOML_OK) {
            return rc;
        }
        p->i++;
        skip_ws(p);
        if ((rc = simple_key(p, &k)) != SOF_TOML_OK) {
            return rc;
        }
    }
    if (peek(p, 0) != '=') {
        return fail(p, SOF_TOML_SYNTAX, p->i, "expected = after a key");
    }
    p->i++;
    skip_ws(p);
    if (find(&table->value, k.text, k.length) != NULL) {
        return fail(p, SOF_TOML_DUPLICATE, k.at, "a key is defined twice");
    }
    if ((rc = value(p, &v)) != SOF_TOML_OK) {
        return rc;
    }
    return member_add(p, table, &k, v);
}

/// @brief Reads the end of a line: whitespace, a comment if there is one,
/// and a newline or the end of the text.
static sof_toml_error_t line_end(parser_t *p) {
    sof_toml_error_t rc;
    skip_ws(p);
    if (peek(p, 0) == '#' && (rc = comment(p)) != SOF_TOML_OK) {
        return rc;
    }
    if (p->i >= p->n) {
        return SOF_TOML_OK;
    }
    if (newline_at(p, p->i)) {
        p->i += p->s[p->i] == '\r' ? 2 : 1;
        return SOF_TOML_OK;
    }
    if (p->s[p->i] == '\r') {
        return fail(p, SOF_TOML_SYNTAX, p->i, "a carriage return isn't followed by a line feed");
    }
    return fail(p, SOF_TOML_SYNTAX, p->i, "expected the end of the line");
}

/// @brief Reads a literal string, '...', on one line, into the document's
/// text.
static sof_toml_error_t literal_string(parser_t *p) {
    size_t start = p->i++;
    sof_string_clear(&p->d->text);
    for (size_t i = p->i; i < p->n; i++) {
        unsigned char c = (unsigned char)p->s[i];
        if (c == '\'') {
            sof_toml_error_t rc = text_add(p, p->s + p->i, i - p->i);
            p->i = i + 1;
            return rc;
        }
        if (c == '\n' || c == '\r') {
            break;
        }
        if ((c < 0x20 && c != '\t') || c == 0x7f) {
            return fail(p, SOF_TOML_SYNTAX, i, "a string holds a control character");
        }
        i += plain_run(p->s + i + 1, p->n - i - 1, '\'', '\'');
    }
    return fail(p, SOF_TOML_SYNTAX, start, "a string has no closing quote on its line");
}

/// @brief Adds a member to a table, making room for it, and puts it in the
/// index once the table has more than SOF_TOML_INDEX_MIN.
static sof_toml_error_t member_add(parser_t *p, node_t *table, const part_t *k, node_t *value) {
    sof_toml_value_t *t = &table->value;
    sof_toml_member_t *members = (sof_toml_member_t *)t->as.table.members;
    if (t->length == table->capacity) {
        size_t capacity = table->capacity == 0 ? ITEMS_MIN : table->capacity * 2;
        members = sof_mem_arena_realloc(&p->d->arena, members, table->capacity * sizeof(*members), capacity * sizeof(*members));
        if (members == NULL) {
            return fail(p, SOF_TOML_NO_MEM, p->i, NULL);
        }
        t->as.table.members = members;
        table->capacity = capacity;
    }
    sof_toml_member_t *m = &members[t->length++];
    m->key = k->text;
    m->key_length = k->length;
    m->value = &value->value;
    m->table = t;
    m->key_span.offset = k->at;
    m->key_span.length = k->end - k->at;
    if (t->length <= SOF_TOML_INDEX_MIN) {
        return SOF_TOML_OK;
    }
    if (t->as.table.index != NULL) {
        return index_add(p, t, t->length - 1);
    }
    for (size_t i = 0; i < t->length; i++) {
        sof_toml_error_t rc = index_add(p, t, i);
        if (rc != SOF_TOML_OK) {
            return rc;
        }
    }
    t->as.table.index = p->d->index;
    return SOF_TOML_OK;
}

/// @brief Reads a multi-line basic string, """...""", decoding it into the
/// document's text.
/// @details A newline straight after the opening quotes is dropped, each
/// CRLF becomes LF, and a backslash at the end of a line drops it, with the
/// whitespace and newlines after it. One or two quotes can come before the
/// closing three, and are part of the string.
static sof_toml_error_t ml_basic_string(parser_t *p) {
    size_t start = p->i;
    sof_toml_error_t rc;
    sof_string_clear(&p->d->text);
    p->i += 3;
    if (newline_at(p, p->i)) {
        p->i += p->s[p->i] == '\r' ? 2 : 1;
    }
    size_t run = p->i;
    for (;;) {
        if (p->i >= p->n) {
            return fail(p, SOF_TOML_SYNTAX, start, "a multi-line string has no closing quotes");
        }
        unsigned char c = (unsigned char)p->s[p->i];
        if (c == '"') {
            size_t q = 1;
            while (q < 5 && peek(p, q) == '"') {
                q++;
            }
            if (q < 3) {
                p->i += q;
                continue;
            }
            rc = text_add(p, p->s + run, p->i + q - 3 - run);
            p->i += q;
            return rc;
        }
        if (c == '\\' || c == '\r') {
            if ((rc = text_add(p, p->s + run, p->i - run)) != SOF_TOML_OK) {
                return rc;
            }
            if (c == '\r') {
                if (!newline_at(p, p->i)) {
                    return fail(p, SOF_TOML_SYNTAX, p->i, "a carriage return isn't followed by a line feed");
                }
                if ((rc = text_add(p, "\n", 1)) != SOF_TOML_OK) {
                    return rc;
                }
                p->i += 2;
                run = p->i;
                continue;
            }
            // A backslash with only whitespace after it on its line ends the
            // line, and the whitespace and newlines that follow go too.
            size_t j = p->i + 1;
            while (j < p->n && (p->s[j] == ' ' || p->s[j] == '\t')) {
                j++;
            }
            if (j < p->n && newline_at(p, j)) {
                p->i = j;
                while (p->i < p->n && (p->s[p->i] == ' ' || p->s[p->i] == '\t' || newline_at(p, p->i))) {
                    p->i += p->s[p->i] == '\r' ? 2 : 1;
                }
            } else if ((rc = escape(p)) != SOF_TOML_OK) {
                return rc;
            }
            run = p->i;
        } else if ((c < 0x20 && c != '\t' && c != '\n') || c == 0x7f) {
            return fail(p, SOF_TOML_SYNTAX, p->i, "a string holds a control character");
        } else {
            p->i++;
            p->i += plain_run(p->s + p->i, p->n - p->i, '"', '\\');
        }
    }
}

/// @brief Reads a multi-line literal string, '''...''', into the
/// document's text: as ml_basic_string(), without escapes.
static sof_toml_error_t ml_literal_string(parser_t *p) {
    size_t start = p->i;
    sof_toml_error_t rc;
    sof_string_clear(&p->d->text);
    p->i += 3;
    if (newline_at(p, p->i)) {
        p->i += p->s[p->i] == '\r' ? 2 : 1;
    }
    size_t run = p->i;
    for (;;) {
        if (p->i >= p->n) {
            return fail(p, SOF_TOML_SYNTAX, start, "a multi-line string has no closing quotes");
        }
        unsigned char c = (unsigned char)p->s[p->i];
        if (c == '\'') {
            size_t q = 1;
            while (q < 5 && peek(p, q) == '\'') {
                q++;
            }
            if (q < 3) {
                p->i += q;
                continue;
            }
            rc = text_add(p, p->s + run, p->i + q - 3 - run);
            p->i += q;
            return rc;
        }
        if (c == '\r') {
            if (!newline_at(p, p->i)) {
                return fail(p, SOF_TOML_SYNTAX, p->i, "a carriage return isn't followed by a line feed");
            }
            if ((rc = text_add(p, p->s + run, p->i - run)) != SOF_TOML_OK || (rc = text_add(p, "\n", 1)) != SOF_TOML_OK) {
                return rc;
            }
            p->i += 2;
            run = p->i;
        } else if ((c < 0x20 && c != '\t' && c != '\n') || c == 0x7f) {
            return fail(p, SOF_TOML_SYNTAX, p->i, "a string holds a control character");
        } else {
            p->i++;
            p->i += plain_run(p->s + p->i, p->n - p->i, '\'', '\'');
        }
    }
}

/// @brief Says whether a newline, LF or CRLF, is at the byte at.
static int newline_at(const parser_t *p, size_t at) {
    return at < p->n && (p->s[at] == '\n' || (p->s[at] == '\r' && at + 1 < p->n && p->s[at + 1] == '\n'));
}

/// @brief Makes a value of a type in the arena, empty.
/// @return The node, or NULL, failing the parse, if there isn't the memory.
static node_t *node_new(parser_t *p, sof_toml_type_t type, unsigned flags) {
    node_t *node = sof_mem_arena_alloc(&p->d->arena, sizeof(*node));
    if (node == NULL) {
        fail(p, SOF_TOML_NO_MEM, p->i, NULL);
        return NULL;
    }
    memset(node, 0, sizeof(*node));
    node->value.type = type;
    node->flags = flags;
    return node;
}

/// @brief Reads an integer or a float.
/// @details The number's text is the run of characters a number can hold,
/// checked against the grammar: an integer in decimal with an optional sign
/// and no leading zeros, or in hexadecimal, octal or binary after 0x, 0o or
/// 0b, without a sign; or a float, an integer part in decimal and a
/// fraction, an exponent or both, or inf or nan with an optional sign. Digits
/// can have single underscores between them.
static sof_toml_error_t number(parser_t *p, node_t **out) {
    size_t start = p->i, end = p->i;
    while (end < p->n && (bare((unsigned char)p->s[end]) || p->s[end] == '.' || p->s[end] == '+')) {
        end++;
    }
    const char *t = p->s + start;
    size_t length = end - start, k = 0;
    int negative = 0;
    if (t[0] == '+' || t[0] == '-') {
        negative = t[0] == '-';
        k = 1;
    }
    node_t *node = node_new(p, SOF_TOML_INTEGER, NODE_SEALED);
    if (node == NULL) {
        return SOF_TOML_NO_MEM;
    }
    *out = node;
    p->i = end;
    if (length - k == 3 && (memcmp(t + k, "inf", 3) == 0 || memcmp(t + k, "nan", 3) == 0)) {
        node->value.type = SOF_TOML_FLOAT;
        node->value.as.number = t[k] == 'i' ? HUGE_VAL : NAN;
        if (negative) {
            node->value.as.number = -node->value.as.number;
        }
        return SOF_TOML_OK;
    }
    int radix = 10;
    if (length - k > 2 && t[k] == '0' && (t[k + 1] == 'x' || t[k + 1] == 'o' || t[k + 1] == 'b')) {
        if (k != 0) {
            return fail(p, SOF_TOML_SYNTAX, start, "a hexadecimal, octal or binary integer has a sign");
        }
        radix = t[1] == 'x' ? 16 : t[1] == 'o' ? 8 : 2;
        k = 2;
    }
    size_t whole = k;
    size_t count = digit_run(t, length, &k, radix);
    if (count == 0) {
        return fail(p, SOF_TOML_SYNTAX, start, "a number isn't valid");
    }
    if (radix == 10 && t[whole] == '0' && count > 1) {
        return fail(p, SOF_TOML_SYNTAX, start, "a number has a leading zero");
    }
    if (k == length) {
        uint64_t magnitude = 0, limit = negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
        for (size_t i = whole; i < length; i++) {
            if (t[i] == '_') {
                continue;
            }
            uint64_t v = (uint64_t)hex_value((unsigned char)t[i]);
            if (magnitude > (limit - v) / (uint64_t)radix) {
                return fail(p, SOF_TOML_RANGE, start, "an integer is too large for 64 bits");
            }
            magnitude = magnitude * (uint64_t)radix + v;
        }
        node->value.as.integer = negative ? (magnitude == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)magnitude) : (int64_t)magnitude;
        return SOF_TOML_OK;
    }
    if (radix != 10) {
        return fail(p, SOF_TOML_SYNTAX, start, "a number isn't valid");
    }
    int is_float = 0;
    if (t[k] == '.') {
        k++;
        if (digit_run(t, length, &k, 10) == 0) {
            return fail(p, SOF_TOML_SYNTAX, start, "a float has no digits after its decimal point");
        }
        is_float = 1;
    }
    if (k < length && (t[k] == 'e' || t[k] == 'E')) {
        k++;
        if (k < length && (t[k] == '+' || t[k] == '-')) {
            k++;
        }
        if (digit_run(t, length, &k, 10) == 0) {
            return fail(p, SOF_TOML_SYNTAX, start, "a float's exponent has no digits");
        }
        is_float = 1;
    }
    if (!is_float || k != length) {
        return fail(p, SOF_TOML_SYNTAX, start, "a number isn't valid");
    }
    node->value.type = SOF_TOML_FLOAT;
    sof_toml_error_t rc = float_convert(p, t, length, &node->value.as.number);
    if (rc == SOF_TOML_RANGE) {
        return fail(p, rc, start, "a float is too large for a double");
    }
    return rc;
}

/// @brief Reads an inline array, [...], which can span lines and end with a
/// comma.
static sof_toml_error_t parse_array(parser_t *p, node_t **out) {
    size_t start = p->i;
    sof_toml_error_t rc;
    node_t *array = node_new(p, SOF_TOML_ARRAY, NODE_SEALED);
    if (array == NULL) {
        return SOF_TOML_NO_MEM;
    }
    array->value.is_inline = 1;
    p->i++;
    for (;;) {
        if ((rc = skip_wcn(p)) != SOF_TOML_OK) {
            return rc;
        }
        if (peek(p, 0) == ']') {
            break;
        }
        if (p->i >= p->n) {
            return fail(p, SOF_TOML_SYNTAX, start, "an array has no closing ]");
        }
        node_t *element;
        if ((rc = value(p, &element)) != SOF_TOML_OK || (rc = array_push(p, array, element)) != SOF_TOML_OK || (rc = skip_wcn(p)) != SOF_TOML_OK) {
            return rc;
        }
        if (peek(p, 0) == ',') {
            p->i++;
        } else if (peek(p, 0) != ']') {
            return p->i < p->n ? fail(p, SOF_TOML_SYNTAX, p->i, "expected , or ] in an array") : fail(p, SOF_TOML_SYNTAX, start, "an array has no closing ]");
        }
    }
    p->i++;
    *out = array;
    return SOF_TOML_OK;
}

/// @brief Parses the whole text, which is valid UTF-8, into the tree.
static sof_toml_error_t parse_text(parser_t *p) {
    sof_toml_error_t rc;
    if (p->n >= 3 && memcmp(p->s, "\xef\xbb\xbf", 3) == 0) {
        p->i = 3;
    }
    p->root = node_new(p, SOF_TOML_TABLE, NODE_EXPLICIT);
    if (p->root == NULL) {
        return SOF_TOML_NO_MEM;
    }
    // The root's span starts at the start of the text, byte order mark
    // included, like a header no one wrote.
    p->root->value.span.offset = 0;
    p->current = p->root;
    for (;;) {
        skip_ws(p);
        if (p->i >= p->n) {
            return SOF_TOML_OK;
        }
        char c = p->s[p->i];
        if (c == '[') {
            rc = header(p);
        } else if (c == '#' || c == '\n' || c == '\r') {
            rc = SOF_TOML_OK;
        } else {
            rc = keyval(p, p->current);
            // A key/value pair stretches the span of the table it is under,
            // and of the array of tables that table is in, to its end.
            if (rc == SOF_TOML_OK) {
                p->current->value.span.length = p->i - p->current->value.span.offset;
                if (p->aot != NULL) {
                    p->aot->value.span.length = p->i - p->aot->value.span.offset;
                }
            }
        }
        if (rc == SOF_TOML_OK) {
            rc = line_end(p);
        }
        if (rc != SOF_TOML_OK) {
            return rc;
        }
    }
}

/// @brief Returns the byte k ahead of the next, or -1 past the end.
static int peek(const parser_t *p, size_t k) {
    return p->i + k < p->n ? (unsigned char)p->s[p->i + k] : -1;
}

/// @brief Counts the bytes at the start of the n at s that a string or
/// comment can hold as they are, 8 at a time and then one at a time, so a
/// short run isn't checked a word at a time again after each byte: any but
/// those below 0x20,
/// 0x7f, stop and also. The text is already known to be UTF-8, so bytes of
/// 0x80 or more are taken as they come.
/// @details Each word of 8 is tested at once: a byte below 0x20 makes
/// (v - 0x20...) & ~v have its top bit set, as one equal to 0x7f, stop or
/// also does when v is XORed with it. A borrow can flag a byte after a real
/// one wrongly, but never a word with none.
/// @return The bytes; the byte after them is for the caller to look at.
static size_t plain_run(const char *s, size_t n, unsigned char stop, unsigned char also) {
    size_t k = 0;
    while (n - k >= 8) {
        uint64_t v;
        memcpy(&v, s + k, sizeof(v));
        uint64_t del = v ^ BYTES(0x7f), a = v ^ BYTES(stop), b = v ^ BYTES(also);
        uint64_t special = ((v - BYTES(0x20)) & ~v) | ((del - BYTES(1)) & ~del) | ((a - BYTES(1)) & ~a) | ((b - BYTES(1)) & ~b);
        if ((special & BYTES(0x80)) != 0) {
            break;
        }
        k += 8;
    }
    while (k < n) {
        unsigned char c = (unsigned char)s[k];
        if (c < 0x20 || c == 0x7f || c == stop || c == also) {
            break;
        }
        k++;
    }
    return k;
}

/// @brief Empties the document, keeping its memory.
static void reset(sof_toml_doc_t *d) {
    if (d->index != NULL) {
        sof_hashmap_free(d->index);
        d->index = NULL;
    }
    sof_mem_arena_clear(&d->arena);
    d->root = NULL;
    d->message = NULL;
    d->line = 0;
    d->column = 0;
    d->offset = 0;
}

/// @brief Reads one part of a key: bare, or a basic or literal string on
/// one line, into the arena.
static sof_toml_error_t simple_key(parser_t *p, part_t *k) {
    sof_toml_error_t rc;
    int c = peek(p, 0);
    k->at = p->i;
    if (bare(c)) {
        size_t start = p->i;
        while (p->i < p->n && bare((unsigned char)p->s[p->i])) {
            p->i++;
        }
        k->length = p->i - start;
        k->end = p->i;
        k->text = copy_text(p, p->s + start, k->length);
        return k->text != NULL ? SOF_TOML_OK : SOF_TOML_NO_MEM;
    }
    if (c == '"' || c == '\'') {
        if (peek(p, 1) == c && peek(p, 2) == c) {
            return fail(p, SOF_TOML_SYNTAX, p->i, "a key is a multi-line string");
        }
        rc = c == '"' ? basic_string(p) : literal_string(p);
        if (rc != SOF_TOML_OK) {
            return rc;
        }
        k->end = p->i;
        k->length = p->d->text.length;
        k->text = copy_text(p, p->d->text.data, k->length);
        return k->text != NULL ? SOF_TOML_OK : SOF_TOML_NO_MEM;
    }
    return fail(p, SOF_TOML_SYNTAX, p->i, "expected a key");
}

/// @brief Skips whitespace, comments and newlines, as between the elements
/// of an array.
static sof_toml_error_t skip_wcn(parser_t *p) {
    for (;;) {
        skip_ws(p);
        if (peek(p, 0) == '#') {
            sof_toml_error_t rc = comment(p);
            if (rc != SOF_TOML_OK) {
                return rc;
            }
        } else if (newline_at(p, p->i)) {
            p->i += p->s[p->i] == '\r' ? 2 : 1;
        } else if (peek(p, 0) == '\r') {
            return fail(p, SOF_TOML_SYNTAX, p->i, "a carriage return isn't followed by a line feed");
        } else {
            return SOF_TOML_OK;
        }
    }
}

/// @brief Skips spaces and tabs.
static void skip_ws(parser_t *p) {
    while (p->i < p->n && (p->s[p->i] == ' ' || p->s[p->i] == '\t')) {
        p->i++;
    }
}

/// @brief Reads a string of any of the four kinds as a value.
static sof_toml_error_t string_value(parser_t *p, node_t **out) {
    int c = peek(p, 0);
    int multi = peek(p, 1) == c && peek(p, 2) == c;
    sof_toml_error_t rc;
    if (c == '"') {
        rc = multi ? ml_basic_string(p) : basic_string(p);
    } else {
        rc = multi ? ml_literal_string(p) : literal_string(p);
    }
    if (rc != SOF_TOML_OK) {
        return rc;
    }
    node_t *node = node_new(p, SOF_TOML_STRING, NODE_SEALED);
    if (node == NULL) {
        return SOF_TOML_NO_MEM;
    }
    node->value.length = p->d->text.length;
    node->value.as.text = copy_text(p, p->d->text.data, p->d->text.length);
    *out = node;
    return node->value.as.text != NULL ? SOF_TOML_OK : SOF_TOML_NO_MEM;
}

/// @brief Adds bytes to the document's text.
static sof_toml_error_t text_add(parser_t *p, const char *bytes, size_t length) {
    if (length == 0 || sof_string_append_bytes(&p->d->text, bytes, length) == SOF_STRING_OK) {
        return SOF_TOML_OK;
    }
    return fail(p, SOF_TOML_NO_MEM, p->i, NULL);
}

/// @brief Says whether d is a document that can be used: not NULL, and
/// initialized.
static int usable(const sof_toml_doc_t *d) {
    return d != NULL && d->ready;
}

/// @brief Reads a value of any type, whose first byte is next, and gives it
/// its span: the text value_read() went over.
static sof_toml_error_t value(parser_t *p, node_t **out) {
    size_t at = p->i;
    sof_toml_error_t rc = value_read(p, out);
    if (rc == SOF_TOML_OK) {
        (*out)->value.span.offset = at;
        (*out)->value.span.length = p->i - at;
    }
    return rc;
}

/// @brief Reads a value of any type, whose first byte is next.
static sof_toml_error_t value_read(parser_t *p, node_t **out) {
    int c = peek(p, 0);
    size_t at = p->i;
    sof_toml_error_t rc;
    switch (c) {
    case '"':
    case '\'':
        return string_value(p, out);
    case 't':
    case 'f': {
        size_t length = c == 't' ? 4 : 5;
        if (p->n - p->i < length || memcmp(p->s + p->i, c == 't' ? "true" : "false", length) != 0) {
            return fail(p, SOF_TOML_SYNTAX, at, "expected a value");
        }
        node_t *node = node_new(p, SOF_TOML_BOOL, NODE_SEALED);
        if (node == NULL) {
            return SOF_TOML_NO_MEM;
        }
        node->value.as.boolean = c == 't';
        p->i += length;
        *out = node;
        return SOF_TOML_OK;
    }
    case '[':
    case '{':
        if (p->depth >= SOF_TOML_DEPTH_MAX) {
            return fail(p, SOF_TOML_TOO_DEEP, at, "arrays and inline tables nest too deep");
        }
        p->depth++;
        rc = c == '[' ? parse_array(p, out) : inline_table(p, out);
        p->depth--;
        return rc;
    case 'i':
    case 'n':
    case '+':
    case '-':
        return number(p, out);
    default:
        break;
    }
    if (!digit(c)) {
        return fail(p, SOF_TOML_SYNTAX, at, "expected a value");
    }
    // Four digits and a hyphen begin a date, and two digits and a colon a
    // time; any other digits, a number.
    if ((peek(p, 4) == '-' && digit(peek(p, 1)) && digit(peek(p, 2)) && digit(peek(p, 3))) || (peek(p, 2) == ':' && digit(peek(p, 1)))) {
        node_t *node = node_new(p, SOF_TOML_NONE, NODE_SEALED);
        size_t used;
        if (node == NULL) {
            return SOF_TOML_NO_MEM;
        }
        if (!datetime_scan(p->s + p->i, p->n - p->i, &used, &node->value.type, &node->value.as.datetime)) {
            return fail(p, SOF_TOML_SYNTAX, at, "a date or time isn't valid");
        }
        p->i += used;
        *out = node;
        return SOF_TOML_OK;
    }
    return number(p, out);
}

size_t sof_toml_datetime_format(sof_toml_type_t type, const sof_toml_datetime_t *dt, char *buffer, size_t size) {
    char text[SOF_TOML_DATETIME_SIZE];
    size_t n = 0;
    if (dt == NULL || buffer == NULL || !datetime_valid(type, dt)) {
        return 0;
    }
    if (type != SOF_TOML_TIME_LOCAL) {
        n += (size_t)snprintf(text, sizeof(text), "%04d-%02d-%02d", dt->year, dt->month, dt->day);
    }
    if (type != SOF_TOML_DATE_LOCAL) {
        if (n > 0) {
            text[n++] = 'T';
        }
        n += (size_t)snprintf(text + n, sizeof(text) - n, "%02d:%02d:%02d", dt->hour, dt->minute, dt->second);
        if (dt->nanosecond != 0) {
            n += (size_t)snprintf(text + n, sizeof(text) - n, ".%09u", (unsigned)dt->nanosecond);
            while (text[n - 1] == '0') {
                n--;
            }
        }
    }
    if (type == SOF_TOML_DATETIME) {
        if (dt->offset == 0) {
            text[n++] = 'Z';
        } else {
            int minutes = dt->offset < 0 ? -dt->offset : dt->offset;
            n += (size_t)snprintf(text + n, sizeof(text) - n, "%c%02d:%02d", dt->offset < 0 ? '-' : '+', minutes / 60, minutes % 60);
        }
    }
    if (n >= size) {
        return 0;
    }
    memcpy(buffer, text, n);
    buffer[n] = '\0';
    return n;
}

sof_toml_error_t sof_toml_datetime_parse(const char *text, size_t length, sof_toml_type_t *type, sof_toml_datetime_t *dt) {
    sof_toml_datetime_t t;
    sof_toml_type_t kind;
    size_t used;
    if (text == NULL || type == NULL || dt == NULL) {
        return SOF_TOML_ERROR;
    }
    if (!datetime_scan(text, length, &used, &kind, &t) || used != length) {
        return SOF_TOML_SYNTAX;
    }
    *type = kind;
    *dt = t;
    return SOF_TOML_OK;
}

sof_toml_error_t sof_toml_doc_clear(sof_toml_doc_t *d) {
    if (!usable(d)) {
        return SOF_TOML_ERROR;
    }
    reset(d);
    return SOF_TOML_OK;
}

sof_toml_error_t sof_toml_doc_free(sof_toml_doc_t *d) {
    if (d == NULL) {
        return SOF_TOML_ERROR;
    }
    if (d->ready) {
        if (d->index != NULL) {
            sof_hashmap_free(d->index);
        }
        sof_mem_arena_free(&d->arena);
        sof_string_free(&d->text);
    }
    memset(d, 0, sizeof(*d));
    return SOF_TOML_OK;
}

sof_toml_error_t sof_toml_doc_init(sof_toml_doc_t *d, const sof_alloc_t *alloc) {
    if (d == NULL) {
        return SOF_TOML_ERROR;
    }
    memset(d, 0, sizeof(*d));
    if (!sof_alloc_valid(alloc) || sof_mem_arena_init(&d->arena, DOC_BLOCK, alloc) != SOF_MEM_OK) {
        memset(d, 0, sizeof(*d));
        return SOF_TOML_ERROR;
    }
    if (alloc != NULL) {
        d->alloc = *alloc;
    }
    d->ready = 1;
    return SOF_TOML_OK;
}

sof_toml_error_t sof_toml_doc_parse(sof_toml_doc_t *d, const void *text, size_t length) {
    parser_t p;
    if (!usable(d) || (text == NULL && length != 0)) {
        return SOF_TOML_ERROR;
    }
    reset(d);
    // The text being decoded is made the first time it is needed, and kept.
    if (d->text.data == NULL && sof_string_init(&d->text, 64, &d->alloc) != SOF_STRING_OK) {
        return SOF_TOML_NO_MEM;
    }
    memset(&p, 0, sizeof(p));
    p.d = d;
    p.s = text != NULL ? text : "";
    p.n = length;
    size_t bad = sof_utf8_validate(p.s, p.n);
    sof_toml_error_t rc = bad != p.n ? fail(&p, SOF_TOML_SYNTAX, bad, "the text isn't UTF-8") : parse_text(&p);
    if (rc == SOF_TOML_OK) {
        d->root = &p.root->value;
        return SOF_TOML_OK;
    }
    reset(d);
    if (p.error != SOF_TOML_NO_MEM) {
        sof_string_t text = sof_string_view(p.s, p.n);
        d->message = p.message;
        sof_string_position(&text, p.at, &d->line, &d->column);
        d->offset = p.at;
    }
    return p.error;
}

sof_toml_error_t sof_toml_doc_read(sof_toml_doc_t *d, sof_toml_read_func_t read, void *ctx) {
    sof_string_t input;
    if (!usable(d) || read == NULL) {
        return SOF_TOML_ERROR;
    }
    reset(d);
    if (sof_string_init(&input, SOF_TOML_READ_SIZE, &d->alloc) != SOF_STRING_OK) {
        return SOF_TOML_NO_MEM;
    }
    // The text is read straight into the string, into all the room it has
    // once there is room for SOF_TOML_READ_SIZE bytes more.
    sof_toml_error_t rc = SOF_TOML_OK;
    for (;;) {
        size_t got = 0;
        char *room = sof_string_reserve(&input, SOF_TOML_READ_SIZE);
        if (room == NULL) {
            rc = SOF_TOML_NO_MEM;
            break;
        }
        size_t size = input.size - 1 - input.length;
        if (read(room, size, &got, ctx) != 0) {
            rc = SOF_TOML_READ;
            break;
        }
        if (got == 0) {
            break;
        }
        sof_string_commit(&input, got < size ? got : size);
    }
    if (rc == SOF_TOML_OK) {
        rc = sof_toml_doc_parse(d, input.data, input.length);
    }
    sof_string_free(&input);
    return rc;
}

int sof_toml_read_file(char *buffer, size_t size, size_t *length, void *ctx) {
    FILE *f = ctx;
    if (f == NULL || length == NULL) {
        return 1;
    }
    *length = fread(buffer, 1, size, f);
    return *length == 0 && ferror(f) ? 1 : 0;
}

const sof_toml_value_t *sof_toml_value_at(const sof_toml_value_t *array, size_t index) {
    if (array == NULL || array->type != SOF_TOML_ARRAY || index >= array->length) {
        return NULL;
    }
    return array->as.elements[index];
}

sof_toml_error_t sof_toml_value_bool(const sof_toml_value_t *v, int *value) {
    if (value == NULL) {
        return SOF_TOML_ERROR;
    }
    if (v == NULL || v->type != SOF_TOML_BOOL) {
        return SOF_TOML_NO_MATCH;
    }
    *value = v->as.boolean;
    return SOF_TOML_OK;
}

size_t sof_toml_value_count(const sof_toml_value_t *v) {
    if (v == NULL || (v->type != SOF_TOML_ARRAY && v->type != SOF_TOML_TABLE)) {
        return 0;
    }
    return v->length;
}

sof_toml_error_t sof_toml_value_datetime(const sof_toml_value_t *v, sof_toml_datetime_t *value) {
    if (value == NULL) {
        return SOF_TOML_ERROR;
    }
    if (v == NULL || v->type < SOF_TOML_DATETIME || v->type > SOF_TOML_TIME_LOCAL) {
        return SOF_TOML_NO_MATCH;
    }
    *value = v->as.datetime;
    return SOF_TOML_OK;
}

sof_toml_error_t sof_toml_value_double(const sof_toml_value_t *v, double *value) {
    if (value == NULL) {
        return SOF_TOML_ERROR;
    }
    if (v == NULL || (v->type != SOF_TOML_FLOAT && v->type != SOF_TOML_INTEGER)) {
        return SOF_TOML_NO_MATCH;
    }
    *value = v->type == SOF_TOML_FLOAT ? v->as.number : (double)v->as.integer;
    return SOF_TOML_OK;
}

const sof_toml_value_t *sof_toml_value_get(const sof_toml_value_t *table, const char *key) {
    if (key == NULL) {
        return NULL;
    }
    return sof_toml_value_get_bytes(table, key, strlen(key));
}

const sof_toml_value_t *sof_toml_value_get_bytes(const sof_toml_value_t *table, const void *key, size_t length) {
    if (table == NULL || table->type != SOF_TOML_TABLE || (key == NULL && length != 0)) {
        return NULL;
    }
    const sof_toml_member_t *m = find(table, key != NULL ? key : "", length);
    return m != NULL ? m->value : NULL;
}

sof_toml_error_t sof_toml_value_int(const sof_toml_value_t *v, int64_t *value) {
    if (value == NULL) {
        return SOF_TOML_ERROR;
    }
    if (v == NULL || v->type != SOF_TOML_INTEGER) {
        return SOF_TOML_NO_MATCH;
    }
    *value = v->as.integer;
    return SOF_TOML_OK;
}

const sof_toml_member_t *sof_toml_value_member(const sof_toml_value_t *table, size_t index) {
    if (table == NULL || table->type != SOF_TOML_TABLE || index >= table->length) {
        return NULL;
    }
    return &table->as.table.members[index];
}

const sof_toml_value_t *sof_toml_value_path(const sof_toml_value_t *v, const char *path) {
    // The parser's own scanning isn't used, since it needs a document to
    // put things in.
    if (v == NULL || path == NULL) {
        return NULL;
    }
    size_t n = strlen(path), i = 0;
    int first = 1;
    while (v != NULL) {
        while (i < n && (path[i] == ' ' || path[i] == '\t')) {
            i++;
        }
        if (i == n) {
            return v;
        }
        if (path[i] == '[') {
            size_t index = 0, start = ++i;
            while (i < n && digit(path[i])) {
                if (index > (SIZE_MAX - 9) / 10) {
                    return NULL;
                }
                index = index * 10 + (size_t)(path[i++] - '0');
            }
            if (i == start || i == n || path[i] != ']') {
                return NULL;
            }
            i++;
            v = sof_toml_value_at(v, index);
            first = 0;
            continue;
        }
        if (!first) {
            if (path[i] != '.') {
                return NULL;
            }
            i++;
            while (i < n && (path[i] == ' ' || path[i] == '\t')) {
                i++;
            }
        }
        first = 0;
        // A part is compared as it is decoded, byte by byte, with the key
        // of each member in turn, unless it is bare or a literal string,
        // whose bytes are the key.
        const char *key = path + i;
        size_t length = 0;
        if (i < n && bare((unsigned char)path[i])) {
            while (i < n && bare((unsigned char)path[i])) {
                i++;
            }
            length = (size_t)(path + i - key);
        } else if (i < n && path[i] == '\'') {
            key = path + ++i;
            while (i < n && path[i] != '\'') {
                i++;
            }
            if (i == n) {
                return NULL;
            }
            length = (size_t)(path + i++ - key);
        } else if (i < n && path[i] == '"') {
            // A basic string's escapes are decoded as it is compared with
            // each key in turn, so nothing need be allocated.
            size_t j = i + 1;
            while (j < n && path[j] != '"') {
                j += path[j] == '\\' && j + 1 < n ? 2 : 1;
            }
            if (j >= n) {
                return NULL;
            }
            const sof_toml_value_t *found = NULL;
            for (size_t m = 0; found == NULL && v->type == SOF_TOML_TABLE && m < v->length; m++) {
                const sof_toml_member_t *member = &v->as.table.members[m];
                int equal = escaped_equal(path + i + 1, j - i - 1, member->key, member->key_length);
                if (equal < 0) {
                    return NULL;
                }
                if (equal) {
                    found = member->value;
                }
            }
            v = found;
            i = j + 1;
            continue;
        } else {
            return NULL;
        }
        v = sof_toml_value_get_bytes(v, key, length);
    }
    return NULL;
}

const char *sof_toml_value_string(const sof_toml_value_t *v, size_t *length) {
    int is_string = v != NULL && v->type == SOF_TOML_STRING;
    if (length != NULL) {
        *length = is_string ? v->length : 0;
    }
    return is_string ? v->as.text : NULL;
}

sof_toml_type_t sof_toml_value_type(const sof_toml_value_t *v) {
    return v != NULL ? v->type : SOF_TOML_NONE;
}
