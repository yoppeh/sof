/**
 * @file basic_scan.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief The scanner of the BASIC interpreter: splits lines into tokens, and
 * reads and writes numbers as BASIC does.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basic_scan.h"

/// @brief The longest numeric constant read, in bytes.
#define NUMBER_TEXT_MAX 63

/// @brief The keywords, in the order of basic_keyword_t.
static const char *const keywords[] = {
    "ABS", "AND", "ASC", "ATN", "BASE", "CHR$", "CONT", "COS", "DATA", "DEF", "DIM", "END", "EXP", "FOR",
    "GOSUB", "GOTO", "IF", "INPUT", "INSTR", "INT", "LEFT$", "LEN", "LET", "LIST", "LOAD", "LOG", "MID$", "MOD",
    "NEW", "NEXT", "NOT", "ON", "OPTION", "OR", "PRINT", "RANDOMIZE", "READ", "REM", "RESTORE", "RETURN", "RIGHT$",
    "RND", "RUN", "SAVE", "SGN", "SIN", "SQR", "STEP", "STOP", "STR$", "TAB", "TAN", "THEN", "TO", "VAL",
};

/// @brief The number of keywords.
#define KEYWORD_COUNT (sizeof(keywords) / sizeof(keywords[0]))

static basic_error_t bad(char *message, size_t size, const char *what);
static int convert(const char *text, size_t length, double *value);
static int is_digit(char c);
static int is_letter(char c);
static int is_space(char c);
static int keyword_find(const char *word);
static size_t number_end(const char *text, size_t length, size_t i);
static basic_error_t push(sof_vector_t *tokens, const basic_token_t *token, char *message, size_t size);
static basic_error_t scan(const char *text, size_t length, sof_vector_t *tokens, char *message, size_t size);
static basic_error_t scan_items(const char *text, size_t length, size_t *position, sof_vector_t *items, int in_line, char *message, size_t size);
static size_t word_end(const char *text, size_t length, size_t i);

/// @brief Writes "syntax error: what" to message and returns BASIC_SYNTAX.
static basic_error_t bad(char *message, size_t size, const char *what) {
    snprintf(message, size, "syntax error: %s", what);
    return BASIC_SYNTAX;
}

/// @brief Converts a numeric constant, already checked, to a double.
/// @return 0, 1 if it is too long to read, or 2 if it is too large.
static int convert(const char *text, size_t length, double *value) {
    char buffer[NUMBER_TEXT_MAX + 1];
    if (length > NUMBER_TEXT_MAX) {
        return 1;
    }
    memcpy(buffer, text, length);
    buffer[length] = '\0';
    double x = strtod(buffer, NULL);
    if (isinf(x)) {
        return 2;
    }
    *value = x;
    return 0;
}

/// @brief Returns 1 if c is an ASCII digit.
static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

/// @brief Returns 1 if c is an ASCII letter.
static int is_letter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/// @brief Returns 1 if c is a space or a tab.
static int is_space(char c) {
    return c == ' ' || c == '\t';
}

/// @brief Returns the basic_keyword_t of an upper case word, or 0.
static int keyword_find(const char *word) {
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (strcmp(keywords[i], word) == 0) {
            return (int)i + 1;
        }
    }
    return 0;
}

/// @brief Returns where the numeric constant starting at text[i], which is
/// a digit or a point, ends: digits, a point and digits, then an exponent
/// if an E is followed by digits, with or without a sign.
static size_t number_end(const char *text, size_t length, size_t i) {
    while (i < length && is_digit(text[i])) {
        i++;
    }
    if (i < length && text[i] == '.') {
        i++;
        while (i < length && is_digit(text[i])) {
            i++;
        }
    }
    if (i < length && (text[i] == 'E' || text[i] == 'e')) {
        size_t j = i + 1;
        if (j < length && (text[j] == '+' || text[j] == '-')) {
            j++;
        }
        if (j < length && is_digit(text[j])) {
            while (j < length && is_digit(text[j])) {
                j++;
            }
            i = j;
        }
    }
    return i;
}

/// @brief Appends a token, or says memory ran out.
static basic_error_t push(sof_vector_t *tokens, const basic_token_t *token, char *message, size_t size) {
    if (sof_vector_push(tokens, token) != SOF_VECTOR_OK) {
        snprintf(message, size, "out of memory");
        return BASIC_NO_MEM;
    }
    return BASIC_OK;
}

/// @brief Splits text into tokens, appending them to tokens.
static basic_error_t scan(const char *text, size_t length, sof_vector_t *tokens, char *message, size_t size) {
    size_t i = 0;
    basic_error_t rc;
    while (i < length) {
        char c = text[i];
        if (is_space(c)) {
            i++;
            continue;
        }
        basic_token_t t;
        memset(&t, 0, sizeof(t));
        t.offset = (uint32_t)i;
        if (is_digit(c) || (c == '.' && i + 1 < length && is_digit(text[i + 1]))) {
            size_t end = number_end(text, length, i);
            int bad_number = convert(text + i, end - i, &t.u.number);
            if (bad_number == 1) {
                return bad(message, size, "number too long");
            }
            if (bad_number == 2) {
                return bad(message, size, "number out of range");
            }
            t.type = BASIC_TOKEN_NUMBER;
            t.length = (uint32_t)(end - i);
            i = end;
        } else if (is_letter(c)) {
            size_t end = word_end(text, length, i);
            if (end - i > BASIC_NAME_MAX) {
                return bad(message, size, "name too long");
            }
            char word[BASIC_NAME_MAX + 1];
            for (size_t j = i; j < end; j++) {
                char w = text[j];
                word[j - i] = w >= 'a' && w <= 'z' ? (char)(w - 'a' + 'A') : w;
            }
            word[end - i] = '\0';
            int kw = keyword_find(word);
            if (kw == 0 && strcmp(word, "GO") == 0) {
                // GO TO and GO SUB, as ECMA-55 spells them.
                size_t j = end;
                while (j < length && is_space(text[j])) {
                    j++;
                }
                size_t next = word_end(text, length, j);
                if (next - j == 2 && (text[j] == 'T' || text[j] == 't') && (text[j + 1] == 'O' || text[j + 1] == 'o')) {
                    kw = BASIC_KW_GOTO;
                    end = next;
                } else if (next - j == 3 && (text[j] == 'S' || text[j] == 's') && (text[j + 1] == 'U' || text[j + 1] == 'u') && (text[j + 2] == 'B' || text[j + 2] == 'b')) {
                    kw = BASIC_KW_GOSUB;
                    end = next;
                }
            }
            t.length = (uint32_t)(end - i);
            i = end;
            if (kw != 0) {
                t.type = BASIC_TOKEN_KEYWORD;
                t.id = (uint8_t)kw;
                if ((rc = push(tokens, &t, message, size)) != BASIC_OK) {
                    return rc;
                }
                if (kw == BASIC_KW_REM) {
                    memset(&t, 0, sizeof(t));
                    t.type = BASIC_TOKEN_REMARK;
                    t.offset = (uint32_t)i;
                    t.length = (uint32_t)(length - i);
                    i = length;
                } else if (kw == BASIC_KW_DATA) {
                    if ((rc = scan_items(text, length, &i, tokens, 1, message, size)) != BASIC_OK) {
                        return rc;
                    }
                    continue;
                } else {
                    continue;
                }
            } else {
                t.type = end - t.offset > 2 && word[0] == 'F' && word[1] == 'N' ? BASIC_TOKEN_FN : BASIC_TOKEN_NAME;
                memcpy(t.u.name, word, sizeof(word));
            }
        } else if (c == '"') {
            size_t j = i + 1;
            while (j < length && text[j] != '"') {
                j++;
            }
            if (j >= length) {
                return bad(message, size, "unterminated string");
            }
            t.type = BASIC_TOKEN_STRING;
            t.offset = (uint32_t)(i + 1);
            t.length = (uint32_t)(j - i - 1);
            i = j + 1;
        } else {
            t.type = BASIC_TOKEN_OP;
            t.length = 1;
            char next = i + 1 < length ? text[i + 1] : '\0';
            switch (c) {
            case '+':
            case '-':
            case '*':
            case '/':
            case '^':
            case '(':
            case ')':
            case ',':
            case ';':
            case ':':
            case '=':
                t.id = (uint8_t)c;
                break;
            case '<':
                t.id = next == '=' ? BASIC_OP_LE : next == '>' ? BASIC_OP_NE : '<';
                break;
            case '>':
                t.id = next == '=' ? BASIC_OP_GE : '>';
                break;
            default: {
                char what[48];
                if (c > ' ' && c < 127) {
                    snprintf(what, sizeof(what), "unexpected character '%c'", c);
                } else {
                    snprintf(what, sizeof(what), "unexpected byte 0x%02x", (unsigned)(unsigned char)c);
                }
                return bad(message, size, what);
            }
            }
            if (t.id != (uint8_t)c) {
                t.length = 2;
            }
            i += t.length;
        }
        if ((rc = push(tokens, &t, message, size)) != BASIC_OK) {
            return rc;
        }
    }
    return BASIC_OK;
}

/// @brief Reads comma-separated items from text[*position], as DATA and
/// INPUT have them, appending them to items as BASIC_TOKEN_DATUM tokens.
/// @param in_line 1 for DATA, whose items end at a colon and which may have
/// none, or 0 for an INPUT reply, which is all items and, if blank, one
/// empty one.
static basic_error_t scan_items(const char *text, size_t length, size_t *position, sof_vector_t *items, int in_line, char *message, size_t size) {
    size_t i = *position;
    basic_error_t rc;
    while (i < length && is_space(text[i])) {
        i++;
    }
    if (in_line && (i >= length || text[i] == ':')) {
        *position = i;
        return BASIC_OK;
    }
    for (;;) {
        while (i < length && is_space(text[i])) {
            i++;
        }
        basic_token_t t;
        memset(&t, 0, sizeof(t));
        t.type = BASIC_TOKEN_DATUM;
        if (i < length && text[i] == '"') {
            size_t j = i + 1;
            while (j < length && text[j] != '"') {
                j++;
            }
            if (j >= length) {
                return bad(message, size, "unterminated string");
            }
            t.id = 1;
            t.offset = (uint32_t)(i + 1);
            t.length = (uint32_t)(j - i - 1);
            i = j + 1;
            while (i < length && is_space(text[i])) {
                i++;
            }
        } else {
            size_t start = i;
            while (i < length && text[i] != ',' && !(in_line && text[i] == ':')) {
                i++;
            }
            size_t end = i;
            while (end > start && is_space(text[end - 1])) {
                end--;
            }
            t.offset = (uint32_t)start;
            t.length = (uint32_t)(end - start);
        }
        if ((rc = push(items, &t, message, size)) != BASIC_OK) {
            return rc;
        }
        if (i < length && text[i] == ',') {
            i++;
            continue;
        }
        if (i >= length || (in_line && text[i] == ':')) {
            break;
        }
        return bad(message, size, "expected ',' after a quoted item");
    }
    *position = i;
    return BASIC_OK;
}

/// @brief Returns where the word starting at text[i] ends: letters and
/// digits, then an optional '$'.
static size_t word_end(const char *text, size_t length, size_t i) {
    while (i < length && (is_letter(text[i]) || is_digit(text[i]))) {
        i++;
    }
    if (i < length && text[i] == '$') {
        i++;
    }
    return i;
}

size_t basic_format_number(double x, char *buffer) {
    if (isinf(x)) {
        x = x < 0 ? -DBL_MAX : DBL_MAX;
    }
    if (x == 0 || isnan(x)) {
        memcpy(buffer, "0", 2);
        return 1;
    }
    // The digits, rounded to BASIC_DIGITS, and the exponent of the first.
    char e_form[BASIC_NUMBER_SIZE];
    snprintf(e_form, sizeof(e_form), "%.*e", BASIC_DIGITS - 1, x);
    const char *p = e_form;
    char *out = buffer;
    if (*p == '-') {
        *out++ = '-';
        p++;
    }
    char digits[BASIC_DIGITS + 1];
    size_t count = 0;
    while (*p != 'e' && *p != '\0') {
        if (is_digit(*p) && count < BASIC_DIGITS) {
            digits[count++] = *p;
        }
        p++;
    }
    int exponent = *p == 'e' ? atoi(p + 1) : 0;
    while (count > 1 && digits[count - 1] == '0') {
        count--;
    }
    if (exponent >= 0 && exponent < BASIC_DIGITS) {
        // Integer or decimal form: the digits up to the point, padded with
        // zeros, then any after it.
        for (int i = 0; i <= exponent; i++) {
            *out++ = (size_t)i < count ? digits[i] : '0';
        }
        if ((size_t)exponent + 1 < count) {
            *out++ = '.';
            for (size_t i = (size_t)exponent + 1; i < count; i++) {
                *out++ = digits[i];
            }
        }
    } else if (exponent < 0 && (size_t)(-exponent - 1) + count <= BASIC_DIGITS) {
        // A point, zeros and the digits.
        *out++ = '.';
        for (int i = 0; i < -exponent - 1; i++) {
            *out++ = '0';
        }
        for (size_t i = 0; i < count; i++) {
            *out++ = digits[i];
        }
    } else {
        *out++ = digits[0];
        if (count > 1) {
            *out++ = '.';
            for (size_t i = 1; i < count; i++) {
                *out++ = digits[i];
            }
        }
        out += snprintf(out, BASIC_NUMBER_SIZE - (size_t)(out - buffer), "E%c%02d", exponent < 0 ? '-' : '+', exponent < 0 ? -exponent : exponent);
    }
    *out = '\0';
    return (size_t)(out - buffer);
}

const char *basic_keyword_name(int id) {
    if (id < 1 || (size_t)id > KEYWORD_COUNT) {
        return "?";
    }
    return keywords[id - 1];
}

int basic_parse_number(const char *text, size_t length, double *value) {
    size_t i = 0;
    while (i < length && is_space(text[i])) {
        i++;
    }
    while (length > i && is_space(text[length - 1])) {
        length--;
    }
    size_t start = i;
    if (i < length && (text[i] == '+' || text[i] == '-')) {
        i++;
    }
    if (!(i < length && (is_digit(text[i]) || (text[i] == '.' && i + 1 < length && is_digit(text[i + 1]))))) {
        return 1;
    }
    if (number_end(text, length, i) != length) {
        return 1;
    }
    return convert(text + start, length - start, value);
}

int basic_parse_prefix(const char *text, size_t length, double *value) {
    size_t i = 0;
    *value = 0;
    while (i < length && is_space(text[i])) {
        i++;
    }
    size_t start = i;
    if (i < length && (text[i] == '+' || text[i] == '-')) {
        i++;
    }
    if (!(i < length && (is_digit(text[i]) || (text[i] == '.' && i + 1 < length && is_digit(text[i + 1]))))) {
        return 0;
    }
    int rc = convert(text + start, number_end(text, length, i) - start, value);
    if (rc == 1) {
        // Too many digits to read; their value is as good as unknown.
        *value = 0;
        return 0;
    }
    return rc;
}

basic_error_t basic_scan_items(const char *text, size_t length, sof_vector_t *items, char *message, size_t size) {
    size_t position = 0;
    message[0] = '\0';
    return scan_items(text, length, &position, items, 0, message, size);
}

void basic_scan_free(basic_line_t *line) {
    sof_string_free(&line->text);
    sof_vector_free(&line->tokens);
    memset(line, 0, sizeof(*line));
}

basic_error_t basic_scan_line(basic_line_t *line, const char *text, size_t length, unsigned long number, const sof_alloc_t *alloc, char *message, size_t size) {
    memset(line, 0, sizeof(*line));
    message[0] = '\0';
    while (length > 0 && (is_space(text[0]) || text[0] == '\r' || text[0] == '\n')) {
        text++;
        length--;
    }
    while (length > 0 && (is_space(text[length - 1]) || text[length - 1] == '\r' || text[length - 1] == '\n')) {
        length--;
    }
    if (length > BASIC_TEXT_MAX) {
        return bad(message, size, "line too long");
    }
    line->number = number;
    if (sof_string_from_bytes(&line->text, text, length, alloc) != SOF_STRING_OK) {
        memset(line, 0, sizeof(*line));
        snprintf(message, size, "out of memory");
        return BASIC_NO_MEM;
    }
    if (sof_vector_init(&line->tokens, sizeof(basic_token_t), 0, NULL, NULL, alloc) != SOF_VECTOR_OK) {
        basic_scan_free(line);
        snprintf(message, size, "out of memory");
        return BASIC_NO_MEM;
    }
    basic_error_t rc = scan(line->text.data, line->text.length, &line->tokens, message, size);
    if (rc != BASIC_OK) {
        basic_scan_free(line);
    }
    return rc;
}
