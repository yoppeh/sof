/**
 * @file sof_json_reader.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief The JSON reader of sof_json, and the conversion of numbers.
 * @details The reader keeps the input in one buffer, and each token is read
 * from a run of it that starts at mark: when it needs more, the bytes from
 * mark on are moved to the front of the buffer, which doubles if they fill
 * it, and the read function fills the rest. A token is always whole in the
 * buffer, then, and the scanning functions find its bytes by their offset
 * from mark, which moving them doesn't change. Nothing about the reader
 * changes until a token is whole, so a call that runs out of memory, or
 * whose read fails, puts position back to mark and can be made again.
 *
 * What the reader expects next is its state; the object or array it is in
 * is the top of the open stack, a byte that is 1 for an object.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sof_json.h"
#include "sof_real.h"
#include "sof_utf.h"

/// @brief The states of a reader: a value, at the top level or after a
/// colon or a comma in an array.
#define EXPECT_VALUE 0
/// @brief The first element of an array, or its end.
#define EXPECT_FIRST_VALUE 1
/// @brief A key, after a comma in an object.
#define EXPECT_KEY 2
/// @brief The first key of an object, or its end.
#define EXPECT_FIRST_KEY 3
/// @brief The colon after a key.
#define EXPECT_COLON 4
/// @brief A comma or the end of the object or array, after a value in it.
#define EXPECT_AFTER 5
/// @brief The end of the input, after a top-level value.
#define EXPECT_END 6

/// @brief The bytes the decoded text can hold when it is first allocated.
#define TEXT_CAPACITY 64

/// @brief Where a number's exponent, and the counts of its digits added to
/// it, saturate: far beyond any number that fits in memory, so the sums are
/// exact for any number there is.
#define EXPONENT_LIMIT 1000000000000000LL

/// @brief A byte repeated through a 64-bit word.
#define BYTES(b) (UINT64_C(0x0101010101010101) * (b))

/// @brief A number's text taken apart.
typedef struct number_t {
    /// @brief 1 if it begins with '-'.
    int negative;
    /// @brief Its digits, before and after the decimal point, with the
    /// leading zeros skipped, and how many there are.
    const char *digits;
    size_t count;
    /// @brief Where the digits after the decimal point begin, and how many
    /// there are.
    const char *fraction;
    size_t fraction_count;
    /// @brief The digits before the decimal point, after the leading zeros.
    size_t whole_count;
    /// @brief Its exponent, saturated at EXPONENT_LIMIT either way.
    long long exponent;
} number_t;

static int at(sof_json_reader_t *r, size_t k, sof_json_error_t *rc);
static sof_json_error_t close_container(sof_json_reader_t *r, sof_json_token_t *token);
static int delimiter(int c);
static void emit(sof_json_reader_t *r, sof_json_token_t *token, sof_json_type_t type);
static sof_json_error_t fill(sof_json_reader_t *r, int *got);
static int hex4(const char *s, uint32_t *unit);
static int number_digit(const number_t *n, size_t i);
static int number_parse(const char *text, size_t length, number_t *n);
static sof_json_error_t number_unsigned(const char *text, size_t length, uint64_t *magnitude, int *negative);
static size_t plain_run(const char *s, size_t n);
static sof_json_error_t read_token(sof_json_reader_t *r, sof_json_token_t *token, sof_json_type_t *peek);
static sof_json_error_t scan_literal(sof_json_reader_t *r, sof_json_token_t *token, int c);
static sof_json_error_t scan_number(sof_json_reader_t *r, sof_json_token_t *token);
static sof_json_error_t scan_string(sof_json_reader_t *r, sof_json_token_t *token, sof_json_type_t type);
static sof_json_error_t skip_space(sof_json_reader_t *r);
static long long saturate(size_t count);
static sof_json_error_t start(sof_json_reader_t *r, unsigned flags, const sof_alloc_t *alloc);
static sof_json_error_t syntax(sof_json_reader_t *r, size_t k, sof_json_error_t code);
static int top_is_object(const sof_json_reader_t *r);
static int usable(const sof_json_reader_t *r);
static sof_json_error_t value(sof_json_reader_t *r, sof_json_token_t *token, int c);
static void value_done(sof_json_reader_t *r);
static sof_json_error_t value_peek(sof_json_reader_t *r, int c, sof_json_type_t *type);

/// @brief Returns the byte k bytes after mark, reading more input if it
/// isn't there yet.
/// @return The byte, -1 at the end of the input, or -2 if reading failed,
/// with the error in *rc.
static int at(sof_json_reader_t *r, size_t k, sof_json_error_t *rc) {
    while (r->mark + k >= r->length) {
        int got;
        *rc = fill(r, &got);
        if (*rc != SOF_JSON_OK) {
            return -2;
        }
        if (!got) {
            return -1;
        }
    }
    return (unsigned char)r->input[r->mark + k];
}

/// @brief Ends the object or array open last, whose closing byte is at
/// position.
static sof_json_error_t close_container(sof_json_reader_t *r, sof_json_token_t *token) {
    sof_json_type_t type = top_is_object(r) ? SOF_JSON_OBJECT_END : SOF_JSON_ARRAY_END;
    sof_stack_pop(&r->open, NULL);
    r->position++;
    value_done(r);
    emit(r, token, type);
    return SOF_JSON_OK;
}

/// @brief Says whether c, a byte or -1 for the end, can follow a number or
/// a literal: whitespace, a comma, the end of an object or array, or the end
/// of the input.
static int delimiter(int c) {
    return c == -1 || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' || c == ']' || c == '}';
}

/// @brief Fills in the token, of type, which begins at mark and ends at
/// position, and notes where it is.
static void emit(sof_json_reader_t *r, sof_json_token_t *token, sof_json_type_t type) {
    r->last = type;
    r->offset = r->consumed + r->mark;
    r->line = r->next_line;
    r->column = r->offset - r->line_start + 1;
    if (token != NULL) {
        token->type = type;
        token->depth = r->open.count;
        token->span.offset = r->offset;
        token->span.length = r->consumed + r->position - r->offset;
        if (type == SOF_JSON_STRING || type == SOF_JSON_KEY || type == SOF_JSON_NUMBER) {
            token->text = r->text.data;
            token->length = r->text.length;
        }
    }
}

/// @brief Reads more input into the buffer, keeping the bytes from mark on.
/// @details Moves them to the front, doubles the buffer if they fill it, and
/// calls the read function for the rest.
/// @return SOF_JSON_OK, with *got 1 if there are more bytes or 0 at the
/// end of the input; SOF_JSON_NO_MEM if the buffer couldn't grow; or
/// SOF_JSON_READ if the read function failed.
static sof_json_error_t fill(sof_json_reader_t *r, int *got) {
    *got = 0;
    if (r->read == NULL || r->ended) {
        return SOF_JSON_OK;
    }
    if (r->mark > 0) {
        memmove(r->buffer, r->buffer + r->mark, r->length - r->mark);
        r->length -= r->mark;
        r->position -= r->mark;
        r->consumed += r->mark;
        r->mark = 0;
    }
    if (r->length == r->size) {
        if (r->size > SIZE_MAX / 2) {
            return SOF_JSON_NO_MEM;
        }
        char *grown = sof_alloc_realloc(&r->alloc, r->buffer, r->size, r->size * 2);
        if (grown == NULL) {
            return SOF_JSON_NO_MEM;
        }
        r->buffer = grown;
        r->input = grown;
        r->size *= 2;
    }
    size_t n = 0;
    if (r->read(r->buffer + r->length, r->size - r->length, &n, r->ctx) != 0) {
        return SOF_JSON_READ;
    }
    if (n == 0) {
        r->ended = 1;
        return SOF_JSON_OK;
    }
    if (n > r->size - r->length) {
        n = r->size - r->length;
    }
    r->length += n;
    *got = 1;
    return SOF_JSON_OK;
}

/// @brief Reads four hex digits.
/// @return 1, with their value in *unit, or 0 if they aren't hex digits.
static int hex4(const char *s, uint32_t *unit) {
    uint32_t u = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        u <<= 4;
        if (c >= '0' && c <= '9') {
            u |= (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            u |= (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            u |= (uint32_t)(c - 'A' + 10);
        } else {
            return 0;
        }
    }
    *unit = u;
    return 1;
}

/// @brief Returns the ith significant digit of a number, counting its
/// digits before and after the decimal point as one run, as a value 0 to 9.
static int number_digit(const number_t *n, size_t i) {
    if (i < n->whole_count) {
        return n->digits[i] - '0';
    }
    return n->fraction[n->fraction_count - (n->count - i)] - '0';
}

/// @brief Takes a number's text apart, checking it against RFC 8259's
/// grammar: number = [ minus ] int [ frac ] [ exp ].
/// @return 1, or 0 if the text isn't a number.
static int number_parse(const char *text, size_t length, number_t *n) {
    size_t i = 0;
    memset(n, 0, sizeof(*n));
    if (i < length && text[i] == '-') {
        n->negative = 1;
        i++;
    }
    if (i == length || text[i] < '0' || text[i] > '9') {
        return 0;
    }
    if (text[i] == '0') {
        i++;
    } else {
        while (i < length && text[i] >= '0' && text[i] <= '9') {
            i++;
        }
    }
    size_t whole_end = i;
    if (i < length && text[i] == '.') {
        i++;
        n->fraction = text + i;
        while (i < length && text[i] >= '0' && text[i] <= '9') {
            i++;
        }
        n->fraction_count = (size_t)(text + i - n->fraction);
        if (n->fraction_count == 0) {
            return 0;
        }
    }
    if (i < length && (text[i] == 'e' || text[i] == 'E')) {
        int exponent_negative = 0;
        i++;
        if (i < length && (text[i] == '+' || text[i] == '-')) {
            exponent_negative = text[i] == '-';
            i++;
        }
        if (i == length || text[i] < '0' || text[i] > '9') {
            return 0;
        }
        long long e = 0;
        while (i < length && text[i] >= '0' && text[i] <= '9') {
            if (e < EXPONENT_LIMIT) {
                e = e * 10 + (text[i] - '0');
            }
            i++;
        }
        if (e > EXPONENT_LIMIT) {
            e = EXPONENT_LIMIT;
        }
        n->exponent = exponent_negative ? -e : e;
    }
    if (i != length) {
        return 0;
    }
    // The significant digits: those before the point without their leading
    // zeros (there can be only one, a lone 0), then those after it. Leading
    // zeros after the point are skipped when there are none before it.
    const char *whole = text + n->negative;
    size_t whole_count = (size_t)(text + whole_end - whole);
    if (whole_count == 1 && whole[0] == '0') {
        whole_count = 0;
    }
    if (n->fraction == NULL) {
        n->fraction = text + whole_end;
    }
    n->whole_count = whole_count;
    if (whole_count > 0) {
        n->digits = whole;
    } else {
        size_t skip = 0;
        while (skip < n->fraction_count && n->fraction[skip] == '0') {
            skip++;
        }
        n->digits = n->fraction + skip;
        n->fraction += skip;
        n->fraction_count -= skip;
        // Each zero skipped after the point is a power of ten off the value;
        // counted below by taking the digits as fraction digits.
        n->exponent -= skip < (size_t)EXPONENT_LIMIT ? (long long)skip : EXPONENT_LIMIT;
    }
    n->count = n->whole_count + n->fraction_count;
    return 1;
}

/// @brief Converts a number's text to the magnitude of an integer.
/// @return SOF_JSON_OK, SOF_JSON_RANGE if it isn't an integer or is over
/// UINT64_MAX, or SOF_JSON_SYNTAX if the text isn't a number.
static sof_json_error_t number_unsigned(const char *text, size_t length, uint64_t *magnitude, int *negative) {
    number_t n;
    if (!number_parse(text, length, &n)) {
        return SOF_JSON_SYNTAX;
    }
    *negative = n.negative;
    // The value is the digits times ten to the power of the exponent less
    // the digits after the point. Trailing zeros come off the digits and add
    // to the power.
    size_t count = n.count;
    while (count > 0 && number_digit(&n, count - 1) == 0) {
        count--;
    }
    if (count == 0) {
        *magnitude = 0;
        return SOF_JSON_OK;
    }
    long long power = n.exponent - saturate(n.fraction_count) + saturate(n.count - count);
    if (power < 0 || count > 20 || (long long)count + power > 20) {
        return SOF_JSON_RANGE;
    }
    uint64_t m = 0;
    for (size_t i = 0; i < count; i++) {
        uint64_t digit = (uint64_t)number_digit(&n, i);
        if (m > (UINT64_MAX - digit) / 10) {
            return SOF_JSON_RANGE;
        }
        m = m * 10 + digit;
    }
    for (long long p = 0; p < power; p++) {
        if (m > UINT64_MAX / 10) {
            return SOF_JSON_RANGE;
        }
        m *= 10;
    }
    *magnitude = m;
    return SOF_JSON_OK;
}

/// @brief Counts the bytes at the start of the n at s that a string can
/// hold as they are, 8 at a time and then one at a time: printable ASCII
/// but '"' and '\'.
/// @details Each word of 8 is tested at once: a byte below 0x20 makes
/// (v - 0x20...) & ~v have its top bit set, as one equal to '"' or '\'
/// does when v is XORed with it, and one of 0x80 or more has it set already.
/// Those tests can flag a byte after a real one wrongly, by a borrow, but
/// never a word with none.
/// @return The bytes; the byte after them, if there is one, is for the
/// caller to look at.
static size_t plain_run(const char *s, size_t n) {
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
    // The rest of the run a byte at a time, so a short string isn't checked
    // a word at a time again after each of its bytes.
    while (k < n) {
        unsigned char c = (unsigned char)s[k];
        if (c < 0x20 || c >= 0x80 || c == '"' || c == '\\') {
            break;
        }
        k++;
    }
    return k;
}

/// @brief Reads the next token, after the checks sof_json_reader_next()
/// makes, or if peek isn't NULL, stops at its first byte and puts its type
/// there.
static sof_json_error_t read_token(sof_json_reader_t *r, sof_json_token_t *token, sof_json_type_t *peek) {
    for (;;) {
        sof_json_error_t rc = skip_space(r);
        if (rc != SOF_JSON_OK) {
            return rc;
        }
        int c = r->position < r->length ? (unsigned char)r->input[r->position] : -1;
        switch (r->state) {
        case EXPECT_COLON:
            if (c != ':') {
                return syntax(r, 0, SOF_JSON_SYNTAX);
            }
            r->position++;
            r->state = EXPECT_VALUE;
            continue;
        case EXPECT_AFTER:
            if (c == ',') {
                r->position++;
                r->state = top_is_object(r) ? EXPECT_KEY : EXPECT_VALUE;
                continue;
            }
            if (c == (top_is_object(r) ? '}' : ']')) {
                if (peek != NULL) {
                    *peek = top_is_object(r) ? SOF_JSON_OBJECT_END : SOF_JSON_ARRAY_END;
                    return SOF_JSON_OK;
                }
                return close_container(r, token);
            }
            return syntax(r, 0, SOF_JSON_SYNTAX);
        case EXPECT_END:
            if (c == -1) {
                return SOF_JSON_NO_MATCH;
            }
            if ((r->flags & SOF_JSON_LINES) == 0) {
                return syntax(r, 0, SOF_JSON_SYNTAX);
            }
            r->state = EXPECT_VALUE;
            continue;
        case EXPECT_FIRST_KEY:
            if (c == '}') {
                if (peek != NULL) {
                    *peek = SOF_JSON_OBJECT_END;
                    return SOF_JSON_OK;
                }
                return close_container(r, token);
            }
            // fall through
        case EXPECT_KEY:
            if (c != '"') {
                return syntax(r, 0, SOF_JSON_SYNTAX);
            }
            if (peek != NULL) {
                *peek = SOF_JSON_KEY;
                return SOF_JSON_OK;
            }
            return scan_string(r, token, SOF_JSON_KEY);
        case EXPECT_FIRST_VALUE:
            if (c == ']') {
                if (peek != NULL) {
                    *peek = SOF_JSON_ARRAY_END;
                    return SOF_JSON_OK;
                }
                return close_container(r, token);
            }
            return peek != NULL ? value_peek(r, c, peek) : value(r, token, c);
        default:
            if (c == -1 && r->open.count == 0 && (r->flags & SOF_JSON_LINES) != 0) {
                return SOF_JSON_NO_MATCH;
            }
            return peek != NULL ? value_peek(r, c, peek) : value(r, token, c);
        }
    }
}

/// @brief Reads true, false or null, whose first byte, c, is at mark.
static sof_json_error_t scan_literal(sof_json_reader_t *r, sof_json_token_t *token, int c) {
    const char *word = c == 't' ? "true" : c == 'f' ? "false" : "null";
    size_t length = strlen(word);
    sof_json_error_t rc = SOF_JSON_OK;
    for (size_t k = 0; k <= length; k++) {
        int b = at(r, k, &rc);
        if (b == -2) {
            r->position = r->mark;
            return rc;
        }
        if (k < length ? b != (unsigned char)word[k] : !delimiter(b)) {
            return syntax(r, k, SOF_JSON_SYNTAX);
        }
    }
    r->position = r->mark + length;
    value_done(r);
    emit(r, token, c == 'n' ? SOF_JSON_NULL : SOF_JSON_BOOL);
    if (token != NULL) {
        token->boolean = c == 't';
    }
    return SOF_JSON_OK;
}

/// @brief Reads a number, which begins at mark, into text.
static sof_json_error_t scan_number(sof_json_reader_t *r, sof_json_token_t *token) {
    sof_json_error_t rc = SOF_JSON_OK;
    size_t k = 0;
    int c = at(r, k, &rc);
    if (c == '-') {
        c = at(r, ++k, &rc);
    }
    if (c == '0') {
        c = at(r, ++k, &rc);
    } else if (c >= '1' && c <= '9') {
        while (c >= '0' && c <= '9') {
            c = at(r, ++k, &rc);
        }
    } else if (c != -2) {
        return syntax(r, k, SOF_JSON_SYNTAX);
    }
    if (c == '.') {
        c = at(r, ++k, &rc);
        if (c != -2 && (c < '0' || c > '9')) {
            return syntax(r, k, SOF_JSON_SYNTAX);
        }
        while (c >= '0' && c <= '9') {
            c = at(r, ++k, &rc);
        }
    }
    if (c == 'e' || c == 'E') {
        c = at(r, ++k, &rc);
        if (c == '+' || c == '-') {
            c = at(r, ++k, &rc);
        }
        if (c != -2 && (c < '0' || c > '9')) {
            return syntax(r, k, SOF_JSON_SYNTAX);
        }
        while (c >= '0' && c <= '9') {
            c = at(r, ++k, &rc);
        }
    }
    if (c == -2) {
        r->position = r->mark;
        return rc;
    }
    if (!delimiter(c)) {
        return syntax(r, k, SOF_JSON_SYNTAX);
    }
    sof_string_clear(&r->text);
    if (sof_string_append_bytes(&r->text, r->input + r->mark, k) != SOF_STRING_OK) {
        r->position = r->mark;
        return SOF_JSON_NO_MEM;
    }
    r->position = r->mark + k;
    value_done(r);
    emit(r, token, SOF_JSON_NUMBER);
    return SOF_JSON_OK;
}

/// @brief Reads a string, whose opening quote is at mark, decoding it into
/// text, and returns it as a token of type, SOF_JSON_STRING or
/// SOF_JSON_KEY.
/// @details Runs of bytes that need no decoding are appended whole. Bytes
/// are found by their offset from mark, k, since reading more input can
/// move them. After a plain byte, those in the buffer are skipped 8 at a
/// time while they are all plain, which changes nothing but k, so a call
/// that fails can still be made again.
static sof_json_error_t scan_string(sof_json_reader_t *r, sof_json_token_t *token, sof_json_type_t type) {
    static const unsigned char replacement[] = {0xef, 0xbf, 0xbd};
    sof_json_error_t rc = SOF_JSON_OK;
    size_t k = 1, run = 1;
    sof_string_clear(&r->text);
    for (;;) {
        int c = at(r, k, &rc);
        if (c == -2) {
            break;
        }
        if (c == -1) {
            return syntax(r, k, SOF_JSON_SYNTAX);
        }
        if (c == '"') {
            if (sof_string_append_bytes(&r->text, r->input + r->mark + run, k - run) != SOF_STRING_OK) {
                rc = SOF_JSON_NO_MEM;
                break;
            }
            r->position = r->mark + k + 1;
            if (type == SOF_JSON_KEY) {
                r->state = EXPECT_COLON;
            } else {
                value_done(r);
            }
            emit(r, token, type);
            return SOF_JSON_OK;
        }
        if (c < 0x20) {
            return syntax(r, k, SOF_JSON_SYNTAX);
        }
        if (c < 0x80 && c != '\\') {
            k++;
            k += plain_run(r->input + r->mark + k, r->length - r->mark - k);
            continue;
        }
        if (c >= 0x80) {
            // A sequence cut off by the end of the buffer is decoded again
            // once there is more.
            uint32_t code;
            size_t available = r->length - r->mark - k;
            size_t used = sof_utf8_decode(r->input + r->mark + k, available, &code);
            if (code == SOF_UTF_INCOMPLETE) {
                int more = at(r, k + available, &rc);
                if (more == -2) {
                    break;
                }
                if (more >= 0) {
                    continue;
                }
            }
            if (!sof_utf_valid(code)) {
                return syntax(r, k, SOF_JSON_SYNTAX);
            }
            k += used;
            continue;
        }
        // An escape: the run before it is appended, then what it stands for.
        if (sof_string_append_bytes(&r->text, r->input + r->mark + run, k - run) != SOF_STRING_OK) {
            rc = SOF_JSON_NO_MEM;
            break;
        }
        int e = at(r, k + 1, &rc);
        if (e == -2) {
            break;
        }
        char decoded = 0;
        switch (e) {
        case '"':
        case '\\':
        case '/':
            decoded = (char)e;
            break;
        case 'b':
            decoded = '\b';
            break;
        case 'f':
            decoded = '\f';
            break;
        case 'n':
            decoded = '\n';
            break;
        case 'r':
            decoded = '\r';
            break;
        case 't':
            decoded = '\t';
            break;
        case 'u':
            break;
        default:
            return syntax(r, k, SOF_JSON_SYNTAX);
        }
        if (decoded != 0) {
            if (sof_string_append_bytes(&r->text, &decoded, 1) != SOF_STRING_OK) {
                rc = SOF_JSON_NO_MEM;
                break;
            }
            k += 2;
            run = k;
            continue;
        }
        int last = at(r, k + 5, &rc);
        if (last == -2) {
            break;
        }
        uint32_t code;
        if (last == -1 || !hex4(r->input + r->mark + k + 2, &code)) {
            return syntax(r, k, SOF_JSON_SYNTAX);
        }
        k += 6;
        if (code >= 0xd800 && code <= 0xdbff) {
            // A high surrogate makes a code point with a low one escaped
            // right after it; without one, it is U+FFFD. Each byte of the
            // escape after it is looked at only if those before it fit, so
            // that a read isn't waited for that the string doesn't need.
            uint32_t low = 0;
            int b = at(r, k, &rc);
            if (b == '\\') {
                b = at(r, k + 1, &rc);
            }
            if (b == 'u') {
                b = at(r, k + 5, &rc);
            }
            if (b == -2) {
                break;
            }
            if (b >= 0 && r->input[r->mark + k] == '\\' && r->input[r->mark + k + 1] == 'u' && hex4(r->input + r->mark + k + 2, &low) && low >= 0xdc00 && low <= 0xdfff) {
                code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                k += 6;
            } else {
                code = SOF_UTF_REPLACEMENT;
            }
        } else if (code >= 0xdc00 && code <= 0xdfff) {
            code = SOF_UTF_REPLACEMENT;
        }
        unsigned char utf8[SOF_UTF8_SIZE_MAX];
        size_t n = code == SOF_UTF_REPLACEMENT ? sizeof(replacement) : sof_utf8_encode(code, utf8, sizeof(utf8));
        if (sof_string_append_bytes(&r->text, code == SOF_UTF_REPLACEMENT ? replacement : utf8, n) != SOF_STRING_OK) {
            rc = SOF_JSON_NO_MEM;
            break;
        }
        run = k;
    }
    r->position = r->mark;
    return rc;
}

/// @brief Skips whitespace, counting lines, and leaves mark and position at
/// the byte after it.
static sof_json_error_t skip_space(sof_json_reader_t *r) {
    for (;;) {
        while (r->position < r->length) {
            char c = r->input[r->position];
            if (c == '\n') {
                r->next_line++;
                r->line_start = r->consumed + r->position + 1;
            } else if (c != ' ' && c != '\t' && c != '\r') {
                r->mark = r->position;
                return SOF_JSON_OK;
            }
            r->position++;
        }
        r->mark = r->position;
        int got;
        sof_json_error_t rc = fill(r, &got);
        if (rc != SOF_JSON_OK || !got) {
            return rc;
        }
    }
}

/// @brief Returns a count of digits as a power of ten, saturated at
/// EXPONENT_LIMIT.
static long long saturate(size_t count) {
    return count < (size_t)EXPONENT_LIMIT ? (long long)count : EXPONENT_LIMIT;
}

/// @brief Sets up the parts the two init functions share.
static sof_json_error_t start(sof_json_reader_t *r, unsigned flags, const sof_alloc_t *alloc) {
    // The stack allocates nothing until the first push.
    if (sof_stack_init(&r->open, 1, 0, NULL, NULL, alloc) != SOF_STACK_OK) {
        return SOF_JSON_NO_MEM;
    }
    if (sof_string_init(&r->text, TEXT_CAPACITY, alloc) != SOF_STRING_OK) {
        sof_stack_free(&r->open);
        return SOF_JSON_NO_MEM;
    }
    if (alloc != NULL) {
        r->alloc = *alloc;
    }
    r->flags = flags;
    r->next_line = 1;
    r->ready = 1;
    return SOF_JSON_OK;
}

/// @brief Notes an error k bytes after mark, which the reader then goes on
/// returning.
static sof_json_error_t syntax(sof_json_reader_t *r, size_t k, sof_json_error_t code) {
    r->error = code;
    r->position = r->mark;
    r->offset = r->consumed + r->mark + k;
    r->line = r->next_line;
    r->column = r->offset - r->line_start + 1;
    return code;
}

/// @brief Says whether the object or array open last is an object.
static int top_is_object(const sof_json_reader_t *r) {
    const unsigned char *top = sof_stack_peek(&r->open);
    return top != NULL && *top != 0;
}

/// @brief Says whether r is a reader that can be used: not NULL, and
/// initialized.
static int usable(const sof_json_reader_t *r) {
    return r != NULL && r->ready;
}

/// @brief Reads a value, whose first byte, c, is at mark.
static sof_json_error_t value(sof_json_reader_t *r, sof_json_token_t *token, int c) {
    switch (c) {
    case '{':
    case '[': {
        if (r->open.count >= SOF_JSON_DEPTH_MAX) {
            return syntax(r, 0, SOF_JSON_TOO_DEEP);
        }
        unsigned char kind = c == '{';
        if (sof_stack_push(&r->open, &kind) != SOF_STACK_OK) {
            return SOF_JSON_NO_MEM;
        }
        r->position++;
        r->state = kind ? EXPECT_FIRST_KEY : EXPECT_FIRST_VALUE;
        emit(r, token, kind ? SOF_JSON_OBJECT : SOF_JSON_ARRAY);
        return SOF_JSON_OK;
    }
    case '"':
        return scan_string(r, token, SOF_JSON_STRING);
    case 't':
    case 'f':
    case 'n':
        return scan_literal(r, token, c);
    default:
        if (c == '-' || (c >= '0' && c <= '9')) {
            return scan_number(r, token);
        }
        return syntax(r, 0, SOF_JSON_SYNTAX);
    }
}

/// @brief Moves on after a value: to a comma or the end in an object or
/// array, or to the end of the input after a top-level value.
static void value_done(sof_json_reader_t *r) {
    if (r->open.count > 0) {
        r->state = EXPECT_AFTER;
    } else {
        r->state = EXPECT_END;
        r->values++;
    }
}

/// @brief Gives the type of the value whose first byte, c, is at mark,
/// without reading it.
static sof_json_error_t value_peek(sof_json_reader_t *r, int c, sof_json_type_t *type) {
    switch (c) {
    case '{':
        *type = SOF_JSON_OBJECT;
        break;
    case '[':
        *type = SOF_JSON_ARRAY;
        break;
    case '"':
        *type = SOF_JSON_STRING;
        break;
    case 't':
    case 'f':
        *type = SOF_JSON_BOOL;
        break;
    case 'n':
        *type = SOF_JSON_NULL;
        break;
    default:
        if (c != '-' && (c < '0' || c > '9')) {
            return syntax(r, 0, SOF_JSON_SYNTAX);
        }
        *type = SOF_JSON_NUMBER;
        break;
    }
    return SOF_JSON_OK;
}

sof_json_error_t sof_json_number_double(const char *text, size_t length, double *value) {
    number_t n;
    if (text == NULL || value == NULL) {
        return SOF_JSON_ERROR;
    }
    // sof_real reads more than JSON's grammar allows, such as ".5" and
    // "007", so the grammar is checked first.
    if (!number_parse(text, length, &n)) {
        return SOF_JSON_SYNTAX;
    }
    // Digits that fit in 64 bits, without the zeros after them, are given
    // to sof_real as an integer and a power of ten, rather than have it
    // read the text again. Beyond 10^±100000 any such number is infinite
    // or 0, so the power is capped there, to fit an int.
    size_t count = n.count;
    while (count > 0 && number_digit(&n, count - 1) == 0) {
        count--;
    }
    sof_real_error_t rc;
    if (count <= 19) {
        uint64_t digits = 0;
        for (size_t i = 0; i < count; i++) {
            digits = digits * 10 + (uint64_t)number_digit(&n, i);
        }
        long long power = n.exponent - saturate(n.fraction_count) + (long long)(n.count - count);
        power = power > 100000 ? 100000 : power < -100000 ? -100000 : power;
        rc = sof_real_from_decimal(digits, (int)power, n.negative, value);
    } else {
        rc = sof_real_parse(text, length, value, NULL);
    }
    return rc == SOF_REAL_RANGE ? SOF_JSON_RANGE : SOF_JSON_OK;
}

sof_json_error_t sof_json_number_int(const char *text, size_t length, int64_t *value) {
    uint64_t magnitude;
    int negative;
    if (text == NULL || value == NULL) {
        return SOF_JSON_ERROR;
    }
    sof_json_error_t rc = number_unsigned(text, length, &magnitude, &negative);
    if (rc != SOF_JSON_OK) {
        return rc;
    }
    if (negative) {
        if (magnitude > (uint64_t)INT64_MAX + 1) {
            return SOF_JSON_RANGE;
        }
        *value = magnitude == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)magnitude;
    } else {
        if (magnitude > (uint64_t)INT64_MAX) {
            return SOF_JSON_RANGE;
        }
        *value = (int64_t)magnitude;
    }
    return SOF_JSON_OK;
}

sof_json_error_t sof_json_number_uint(const char *text, size_t length, uint64_t *value) {
    uint64_t magnitude;
    int negative;
    if (text == NULL || value == NULL) {
        return SOF_JSON_ERROR;
    }
    sof_json_error_t rc = number_unsigned(text, length, &magnitude, &negative);
    if (rc != SOF_JSON_OK) {
        return rc;
    }
    if (negative && magnitude != 0) {
        return SOF_JSON_RANGE;
    }
    *value = magnitude;
    return SOF_JSON_OK;
}

int sof_json_read_file(char *buffer, size_t size, size_t *length, void *ctx) {
    FILE *f = ctx;
    *length = 0;
    if (f == NULL) {
        return 1;
    }
    *length = fread(buffer, 1, size, f);
    return *length == 0 && ferror(f) ? 1 : 0;
}

sof_json_error_t sof_json_reader_clear(sof_json_reader_t *r) {
    if (!usable(r)) {
        return SOF_JSON_ERROR;
    }
    sof_string_clear(&r->text);
    sof_stack_clear(&r->open);
    if (r->read != NULL) {
        r->length = 0;
    }
    r->position = 0;
    r->mark = 0;
    r->consumed = 0;
    r->state = EXPECT_VALUE;
    r->last = SOF_JSON_NONE;
    r->skip = 0;
    r->ended = 0;
    r->error = SOF_JSON_OK;
    r->line = 0;
    r->column = 0;
    r->offset = 0;
    r->next_line = 1;
    r->line_start = 0;
    r->values = 0;
    return SOF_JSON_OK;
}

size_t sof_json_reader_depth(const sof_json_reader_t *r) {
    return usable(r) ? r->open.count : 0;
}

sof_json_error_t sof_json_reader_free(sof_json_reader_t *r) {
    if (r == NULL) {
        return SOF_JSON_ERROR;
    }
    if (r->ready) {
        if (r->buffer != NULL) {
            sof_alloc_free(&r->alloc, r->buffer, r->size);
        }
        sof_string_free(&r->text);
        sof_stack_free(&r->open);
    }
    memset(r, 0, sizeof(*r));
    return SOF_JSON_OK;
}

sof_json_error_t sof_json_reader_init(sof_json_reader_t *r, unsigned flags, sof_json_read_func_t read, void *ctx, const sof_alloc_t *alloc) {
    if (r == NULL) {
        return SOF_JSON_ERROR;
    }
    memset(r, 0, sizeof(*r));
    if (read == NULL || !sof_alloc_valid(alloc)) {
        return SOF_JSON_ERROR;
    }
    r->buffer = sof_alloc_malloc(alloc, SOF_JSON_READ_SIZE);
    if (r->buffer == NULL) {
        return SOF_JSON_NO_MEM;
    }
    sof_json_error_t rc = start(r, flags, alloc);
    if (rc != SOF_JSON_OK) {
        sof_alloc_free(alloc, r->buffer, SOF_JSON_READ_SIZE);
        memset(r, 0, sizeof(*r));
        return rc;
    }
    r->size = SOF_JSON_READ_SIZE;
    r->input = r->buffer;
    r->read = read;
    r->ctx = ctx;
    return SOF_JSON_OK;
}

sof_json_error_t sof_json_reader_init_bytes(sof_json_reader_t *r, unsigned flags, const void *data, size_t length, const sof_alloc_t *alloc) {
    if (r == NULL) {
        return SOF_JSON_ERROR;
    }
    memset(r, 0, sizeof(*r));
    if ((data == NULL && length != 0) || !sof_alloc_valid(alloc)) {
        return SOF_JSON_ERROR;
    }
    sof_json_error_t rc = start(r, flags, alloc);
    if (rc != SOF_JSON_OK) {
        memset(r, 0, sizeof(*r));
        return rc;
    }
    r->input = data != NULL ? data : "";
    r->length = length;
    return SOF_JSON_OK;
}

sof_json_error_t sof_json_reader_next(sof_json_reader_t *r, sof_json_token_t *token) {
    if (token != NULL) {
        token->type = SOF_JSON_NONE;
        token->text = "";
        token->length = 0;
        token->boolean = 0;
        token->depth = usable(r) ? r->open.count : 0;
        token->span.offset = 0;
        token->span.length = 0;
    }
    if (!usable(r)) {
        return SOF_JSON_ERROR;
    }
    r->skip = 0;
    if (r->error != SOF_JSON_OK) {
        return r->error;
    }
    return read_token(r, token, NULL);
}

sof_json_error_t sof_json_reader_peek(sof_json_reader_t *r, sof_json_type_t *type) {
    sof_json_type_t peeked = SOF_JSON_NONE;
    if (type != NULL) {
        *type = SOF_JSON_NONE;
    }
    if (!usable(r)) {
        return SOF_JSON_ERROR;
    }
    if (r->error != SOF_JSON_OK) {
        return r->error;
    }
    sof_json_error_t rc = read_token(r, NULL, &peeked);
    if (rc == SOF_JSON_OK && type != NULL) {
        *type = peeked;
    }
    return rc;
}

sof_json_error_t sof_json_reader_skip(sof_json_reader_t *r) {
    if (!usable(r)) {
        return SOF_JSON_ERROR;
    }
    if (r->error != SOF_JSON_OK) {
        return r->error;
    }
    if (r->skip == 0) {
        if (r->last == SOF_JSON_KEY) {
            r->skip = r->open.count + 1;
        } else if ((r->last == SOF_JSON_ARRAY || r->last == SOF_JSON_OBJECT) && r->open.count > 0) {
            r->skip = r->open.count;
        } else {
            return SOF_JSON_OK;
        }
    }
    // Tokens are read until the reader is back at the depth it started
    // from, and, after a key, past the member's value.
    size_t depth = r->skip - 1;
    while (r->open.count > depth || r->state == EXPECT_COLON || (r->state == EXPECT_VALUE && r->open.count > 0)) {
        sof_json_error_t rc = read_token(r, NULL, NULL);
        if (rc != SOF_JSON_OK) {
            if (rc != SOF_JSON_NO_MEM && rc != SOF_JSON_READ) {
                r->skip = 0;
            }
            return rc;
        }
    }
    r->skip = 0;
    return SOF_JSON_OK;
}
