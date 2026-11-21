/**
 * @file test_json.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A JSON checker for the tests, written from RFC 8259.
 * @details test_json_text() says whether some bytes are a JSON text, as RFC
 * 8259 defines one, and can decode it into a canonical form, so that two
 * texts can be compared for what they hold rather than how they are
 * written. Each function follows the production of the RFC's grammar it is
 * named for:
 *
 *     JSON-text = ws value ws                                      (section 2)
 *     value = false / null / true / object / array / number / string  (3)
 *     object = begin-object [ member *( value-separator member ) ] end-object  (4)
 *     member = string name-separator value
 *     array = begin-array [ value *( value-separator value ) ] end-array  (5)
 *     number = [ minus ] int [ frac ] [ exp ]                      (6)
 *     string = quotation-mark *char quotation-mark                 (7)
 *     char = unescaped / escape ( %x22 / %x5C / %x2F / %x62 / %x66 /
 *            %x6E / %x72 / %x74 / %x75 4HEXDIG )
 *     unescaped = %x20-21 / %x23-5B / %x5D-10FFFF
 *     ws = *( %x20 / %x09 / %x0A / %x0D )
 *
 * The text must be UTF-8 (section 8.1), so bytes that aren't, in strings or
 * anywhere else, are refused, as is a byte order mark, which a JSON text
 * MUST NOT begin with and which a parser MAY, but needn't, ignore. A \u
 * escape of a surrogate that isn't half of a pair is allowed by the grammar,
 * and decodes as the three bytes UTF-8 would give it if it were a code
 * point. Nesting deeper than TEST_JSON_DEPTH is refused, as section 9 allows.
 *
 * The canonical form is a string of tokens: '{', '}', '[' and ']'; 't',
 * 'f' and 'z' for true, false and null; 'n', a number's text and ';'; and
 * for a string or a member's name, 's', its decoded length in 8 hex digits
 * and its decoded bytes.
 *
 * Like test.h, everything is static, so the header is all there is.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#ifndef TEST_JSON_H
#define TEST_JSON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/// @brief The deepest objects and arrays may nest.
#define TEST_JSON_DEPTH 512

/// @brief A checker's place in the text, and where the canonical form goes.
typedef struct test_json_t {
    const unsigned char *s;
    size_t length;
    size_t position;
    /// @brief The canonical form, or NULL to check without decoding.
    char *out;
    size_t size;
    size_t out_length;
    /// @brief How deep in objects and arrays the checker is.
    int depth;
} test_json_t;

static int test_json_value(test_json_t *p);

/// @brief Appends n bytes to the canonical form, if there is one.
/// @return 1, or 0 if they don't fit.
static inline int test_json_emit(test_json_t *p, const void *bytes, size_t n) {
    if (p->out == NULL) {
        return 1;
    }
    if (p->size - p->out_length < n + 1) {
        return 0;
    }
    memcpy(p->out + p->out_length, bytes, n);
    p->out_length += n;
    p->out[p->out_length] = '\0';
    return 1;
}

/// @brief Appends the UTF-8 encoding of a number up to U+10FFFF,
/// surrogates included, to the canonical form.
static inline int test_json_emit_code(test_json_t *p, uint32_t code) {
    unsigned char bytes[4];
    size_t n;
    if (code < 0x80) {
        bytes[0] = (unsigned char)code;
        n = 1;
    } else if (code < 0x800) {
        bytes[0] = (unsigned char)(0xc0 | (code >> 6));
        bytes[1] = (unsigned char)(0x80 | (code & 0x3f));
        n = 2;
    } else if (code < 0x10000) {
        bytes[0] = (unsigned char)(0xe0 | (code >> 12));
        bytes[1] = (unsigned char)(0x80 | ((code >> 6) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | (code & 0x3f));
        n = 3;
    } else {
        bytes[0] = (unsigned char)(0xf0 | (code >> 18));
        bytes[1] = (unsigned char)(0x80 | ((code >> 12) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | ((code >> 6) & 0x3f));
        bytes[3] = (unsigned char)(0x80 | (code & 0x3f));
        n = 4;
    }
    return test_json_emit(p, bytes, n);
}

/// @brief ws = *( %x20 / %x09 / %x0A / %x0D )
static inline void test_json_ws(test_json_t *p) {
    while (p->position < p->length) {
        unsigned char c = p->s[p->position];
        if (c != 0x20 && c != 0x09 && c != 0x0a && c != 0x0d) {
            break;
        }
        p->position++;
    }
}

/// @brief Reads one UTF-8 encoded scalar value, strictly: no overlong
/// forms, surrogates or values past U+10FFFF.
/// @return Its length in bytes, or 0 if the bytes aren't one.
static inline size_t test_json_utf8(const test_json_t *p, uint32_t *code) {
    const unsigned char *s = p->s + p->position;
    size_t left = p->length - p->position;
    size_t n;
    uint32_t value, minimum;
    if (left == 0) {
        return 0;
    }
    if (s[0] < 0x80) {
        *code = s[0];
        return 1;
    } else if ((s[0] & 0xe0) == 0xc0) {
        n = 2;
        value = s[0] & 0x1f;
        minimum = 0x80;
    } else if ((s[0] & 0xf0) == 0xe0) {
        n = 3;
        value = s[0] & 0x0f;
        minimum = 0x800;
    } else if ((s[0] & 0xf8) == 0xf0) {
        n = 4;
        value = s[0] & 0x07;
        minimum = 0x10000;
    } else {
        return 0;
    }
    if (left < n) {
        return 0;
    }
    for (size_t i = 1; i < n; i++) {
        if ((s[i] & 0xc0) != 0x80) {
            return 0;
        }
        value = (value << 6) | (s[i] & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        return 0;
    }
    *code = value;
    return n;
}

/// @brief Reads 4HEXDIG, as in a \u escape.
static inline int test_json_hex4(test_json_t *p, uint32_t *unit) {
    uint32_t value = 0;
    if (p->length - p->position < 4) {
        return 0;
    }
    for (int i = 0; i < 4; i++) {
        unsigned char c = p->s[p->position++];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= (uint32_t)(c - 'A' + 10);
        } else {
            return 0;
        }
    }
    *unit = value;
    return 1;
}

/// @brief string = quotation-mark *char quotation-mark
static inline int test_json_string(test_json_t *p) {
    size_t length_at = p->out_length;
    if (p->position >= p->length || p->s[p->position] != '"' || !test_json_emit(p, "s00000000", 9)) {
        return 0;
    }
    size_t start = p->out_length;
    p->position++;
    for (;;) {
        if (p->position >= p->length) {
            return 0;
        }
        unsigned char c = p->s[p->position];
        if (c == '"') {
            p->position++;
            break;
        }
        if (c < 0x20) {
            // Not unescaped, so a control character must be escaped.
            return 0;
        }
        if (c != '\\') {
            uint32_t code;
            size_t n = test_json_utf8(p, &code);
            if (n == 0 || !test_json_emit(p, p->s + p->position, n)) {
                return 0;
            }
            p->position += n;
            continue;
        }
        p->position++;
        if (p->position >= p->length) {
            return 0;
        }
        c = p->s[p->position++];
        static const char escapes[] = "\"\\/bfnrt";
        static const char decoded[] = "\"\\/\b\f\n\r\t";
        const char *e = c != 0 ? strchr(escapes, c) : NULL;
        if (e != NULL) {
            if (!test_json_emit(p, &decoded[e - escapes], 1)) {
                return 0;
            }
            continue;
        }
        uint32_t unit, low;
        if (c != 'u' || !test_json_hex4(p, &unit)) {
            return 0;
        }
        if (unit >= 0xd800 && unit <= 0xdbff && p->length - p->position >= 6 && p->s[p->position] == '\\' && p->s[p->position + 1] == 'u') {
            // A pair of surrogates is one code point.
            size_t at = p->position;
            p->position += 2;
            if (test_json_hex4(p, &low) && low >= 0xdc00 && low <= 0xdfff) {
                unit = 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00);
            } else {
                p->position = at;
            }
        }
        if (!test_json_emit_code(p, unit)) {
            return 0;
        }
    }
    if (p->out != NULL) {
        char hex[10];
        snprintf(hex, sizeof(hex), "%08zx", p->out_length - start);
        memcpy(p->out + length_at + 1, hex, 8);
    }
    return 1;
}

/// @brief number = [ minus ] int [ frac ] [ exp ]
static inline int test_json_number(test_json_t *p) {
    const unsigned char *s = p->s;
    size_t i = p->position, n = p->length, start = p->position;
    if (i < n && s[i] == '-') {
        i++;
    }
    // int = zero / ( digit1-9 *DIGIT )
    if (i < n && s[i] == '0') {
        i++;
    } else if (i < n && s[i] >= '1' && s[i] <= '9') {
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            i++;
        }
    } else {
        return 0;
    }
    // frac = decimal-point 1*DIGIT
    if (i < n && s[i] == '.') {
        i++;
        if (i >= n || s[i] < '0' || s[i] > '9') {
            return 0;
        }
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            i++;
        }
    }
    // exp = e [ minus / plus ] 1*DIGIT
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) {
            i++;
        }
        if (i >= n || s[i] < '0' || s[i] > '9') {
            return 0;
        }
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            i++;
        }
    }
    p->position = i;
    return test_json_emit(p, "n", 1) && test_json_emit(p, s + start, i - start) && test_json_emit(p, ";", 1);
}

/// @brief Reads a literal name: false, null or true.
static inline int test_json_literal(test_json_t *p, const char *word, const char *token) {
    size_t n = strlen(word);
    if (p->length - p->position < n || memcmp(p->s + p->position, word, n) != 0) {
        return 0;
    }
    p->position += n;
    return test_json_emit(p, token, 1);
}

/// @brief object and array, with the ws the structural characters allow
/// around them, as begin-object = ws %x7B ws and so on.
static inline int test_json_container(test_json_t *p, unsigned char close) {
    int object = close == '}';
    if (++p->depth > TEST_JSON_DEPTH || !test_json_emit(p, object ? "{" : "[", 1)) {
        return 0;
    }
    p->position++;
    test_json_ws(p);
    if (p->position < p->length && p->s[p->position] == close) {
        p->position++;
        p->depth--;
        return test_json_emit(p, object ? "}" : "]", 1);
    }
    for (;;) {
        if (object) {
            // member = string name-separator value
            if (!test_json_string(p)) {
                return 0;
            }
            test_json_ws(p);
            if (p->position >= p->length || p->s[p->position] != ':') {
                return 0;
            }
            p->position++;
            test_json_ws(p);
        }
        if (!test_json_value(p)) {
            return 0;
        }
        test_json_ws(p);
        if (p->position >= p->length) {
            return 0;
        }
        unsigned char c = p->s[p->position++];
        if (c == close) {
            p->depth--;
            return test_json_emit(p, object ? "}" : "]", 1);
        }
        if (c != ',') {
            return 0;
        }
        test_json_ws(p);
    }
}

/// @brief value = false / null / true / object / array / number / string
static int test_json_value(test_json_t *p) {
    if (p->position >= p->length) {
        return 0;
    }
    switch (p->s[p->position]) {
    case '{':
        return test_json_container(p, '}');
    case '[':
        return test_json_container(p, ']');
    case '"':
        return test_json_string(p);
    case 't':
        return test_json_literal(p, "true", "t");
    case 'f':
        return test_json_literal(p, "false", "f");
    case 'n':
        return test_json_literal(p, "null", "z");
    default:
        return test_json_number(p);
    }
}

/**
 * @brief Checks a JSON text, JSON-text = ws value ws, and decodes it.
 * @param text The bytes.
 * @param length How many.
 * @param out Where the canonical form goes, NUL-terminated, or NULL.
 * @param size The bytes out has room for.
 * @param out_length Where the canonical form's length goes, or NULL.
 * @return 1 if the bytes are a JSON text, and its canonical form fits, or 0.
 */
static inline int test_json_text(const void *text, size_t length, char *out, size_t size, size_t *out_length) {
    test_json_t p = {text, length, 0, out, size, 0, 0};
    if (out != NULL && size > 0) {
        out[0] = '\0';
    }
    test_json_ws(&p);
    int ok = test_json_value(&p);
    test_json_ws(&p);
    if (out_length != NULL) {
        *out_length = p.out_length;
    }
    return ok && p.position == length;
}

/**
 * @brief Checks output as sof_json's writer writes it: JSON values, each
 * followed by a newline, with no other whitespace between them.
 * @return The number of values, or -1 if the output isn't that.
 */
static inline long test_json_lines(const void *text, size_t length) {
    test_json_t p = {text, length, 0, NULL, 0, 0, 0};
    long count = 0;
    while (p.position < p.length) {
        if (!test_json_value(&p) || p.position >= p.length || p.s[p.position] != '\n') {
            return -1;
        }
        p.position++;
        count++;
    }
    return count;
}

#endif // TEST_JSON_H
