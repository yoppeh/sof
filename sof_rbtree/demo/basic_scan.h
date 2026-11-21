/**
 * @file basic_scan.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief The scanner of the BASIC interpreter: splits lines into tokens, and
 * reads and writes numbers as BASIC does.
 * @details Internal to the interpreter: basic.c is its only user. A line is
 * scanned once, when it is entered, into a basic_line_t holding the line as
 * typed and its tokens. Names and keywords are folded to upper case in the
 * tokens; the text keeps the case it was typed in, for LIST and for the
 * contents of strings, REM and DATA.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef BASIC_SCAN_H
#define BASIC_SCAN_H

#include <stddef.h>
#include <stdint.h>

#include "basic.h"

/// @brief The size of a buffer basic_format_number() writes, NUL included.
#define BASIC_NUMBER_SIZE 32

/// @brief The significant digits numbers are printed with.
#define BASIC_DIGITS 9

/// @brief The longest line, in bytes, the scanner takes.
#define BASIC_TEXT_MAX 65535

/**
 * @brief What a token is.
 */
typedef enum basic_token_type_t {
    /// @brief A numeric constant; number holds its value.
    BASIC_TOKEN_NUMBER = 1,
    /// @brief A string constant; offset and length give its contents in the
    /// line's text, without the quotes.
    BASIC_TOKEN_STRING,
    /// @brief The name of a variable or array, in name.
    BASIC_TOKEN_NAME,
    /// @brief The name of a DEF function, starting with FN, in name.
    BASIC_TOKEN_FN,
    /// @brief A keyword; id is its basic_keyword_t.
    BASIC_TOKEN_KEYWORD,
    /// @brief An operator or punctuation; id is its character, or one of
    /// BASIC_OP_LE, BASIC_OP_GE and BASIC_OP_NE.
    BASIC_TOKEN_OP,
    /// @brief A DATA item, or an item of an INPUT reply; offset and length
    /// give its text, without quotes or the spaces around it, and id is 1
    /// if it was quoted.
    BASIC_TOKEN_DATUM,
    /// @brief The text of a REM, to the end of the line.
    BASIC_TOKEN_REMARK,
} basic_token_type_t;

/// @brief The operator <=.
#define BASIC_OP_LE 'l'
/// @brief The operator >=.
#define BASIC_OP_GE 'g'
/// @brief The operator <>.
#define BASIC_OP_NE 'n'

/**
 * @brief The keywords, in alphabetical order.
 */
typedef enum basic_keyword_t {
    BASIC_KW_ABS = 1,
    BASIC_KW_AND,
    BASIC_KW_ASC,
    BASIC_KW_ATN,
    BASIC_KW_BASE,
    BASIC_KW_CHR,
    BASIC_KW_CONT,
    BASIC_KW_COS,
    BASIC_KW_DATA,
    BASIC_KW_DEF,
    BASIC_KW_DIM,
    BASIC_KW_END,
    BASIC_KW_EXP,
    BASIC_KW_FOR,
    BASIC_KW_GOSUB,
    BASIC_KW_GOTO,
    BASIC_KW_IF,
    BASIC_KW_INPUT,
    BASIC_KW_INSTR,
    BASIC_KW_INT,
    BASIC_KW_LEFT,
    BASIC_KW_LEN,
    BASIC_KW_LET,
    BASIC_KW_LIST,
    BASIC_KW_LOAD,
    BASIC_KW_LOG,
    BASIC_KW_MID,
    BASIC_KW_MOD,
    BASIC_KW_NEW,
    BASIC_KW_NEXT,
    BASIC_KW_NOT,
    BASIC_KW_ON,
    BASIC_KW_OPTION,
    BASIC_KW_OR,
    BASIC_KW_PRINT,
    BASIC_KW_RANDOMIZE,
    BASIC_KW_READ,
    BASIC_KW_REM,
    BASIC_KW_RESTORE,
    BASIC_KW_RETURN,
    BASIC_KW_RIGHT,
    BASIC_KW_RND,
    BASIC_KW_RUN,
    BASIC_KW_SAVE,
    BASIC_KW_SGN,
    BASIC_KW_SIN,
    BASIC_KW_SQR,
    BASIC_KW_STEP,
    BASIC_KW_STOP,
    BASIC_KW_STR,
    BASIC_KW_TAB,
    BASIC_KW_TAN,
    BASIC_KW_THEN,
    BASIC_KW_TO,
    BASIC_KW_VAL,
} basic_keyword_t;

/**
 * @brief A token of a line.
 */
typedef struct basic_token_t {
    /// @brief A basic_token_type_t.
    uint8_t type;
    /// @brief The keyword or operator, or for a datum, 1 if it was quoted.
    uint8_t id;
    /// @brief Where the token starts in the line's text, and its length.
    uint32_t offset;
    uint32_t length;
    union {
        /// @brief A numeric constant's value.
        double number;
        /// @brief A name, in upper case, NUL-terminated.
        char name[BASIC_NAME_MAX + 1];
    } u;
} basic_token_t;

/**
 * @brief A line of BASIC and its tokens.
 */
typedef struct basic_line_t {
    /// @brief The line's number, or 0 for a line run at once.
    unsigned long number;
    /// @brief The line as it was entered, after its number, without the
    /// spaces before and after it.
    sof_string_t text;
    /// @brief Its tokens, of basic_token_t.
    sof_vector_t tokens;
} basic_line_t;

/**
 * @brief Writes a number as PRINT does, without the spaces around it.
 * @details In the ECMA-55 forms, with BASIC_DIGITS significant digits: an
 * integer ("42", "-7"), a number with a point and no leading zero ("3.25",
 * ".001"), or with an exponent ("1.5E+10", "-2E-12") when it can't be
 * written in BASIC_DIGITS digits without one. -0 is written as 0.
 * @param x The number. An infinity is written as the largest double.
 * @param buffer Where to write it, BASIC_NUMBER_SIZE bytes.
 * @return The length written, not counting the NUL.
 */
extern size_t basic_format_number(double x, char *buffer);

/**
 * @brief Returns the name of a keyword, in upper case.
 * @param id A basic_keyword_t.
 * @return The name, or "?" for anything else.
 */
extern const char *basic_keyword_name(int id);

/**
 * @brief Reads a whole string as a numeric constant, as INPUT and READ
 * read them.
 * @details Spaces are allowed before and after it, and a sign before it.
 * The number is digits with an optional point, or a point and digits,
 * then optionally E, an optional sign, and digits.
 * @param text The text, which need not be NUL-terminated.
 * @param length Its length in bytes.
 * @param value Set to the number on success.
 * @return 0, 1 if the text isn't a numeric constant, or 2 if it is one too
 * large for a double.
 */
extern int basic_parse_number(const char *text, size_t length, double *value);

/**
 * @brief Reads the longest numeric constant at the start of a string, as
 * VAL does.
 * @details Spaces and a sign may come before it. Whatever follows is
 * ignored.
 * @param text The text, which need not be NUL-terminated.
 * @param length Its length in bytes.
 * @param value Set to the number, or 0 if the text doesn't start with one.
 * @return 0, or 2 if the number is too large for a double.
 */
extern int basic_parse_prefix(const char *text, size_t length, double *value);

/**
 * @brief Splits text into comma-separated items, as INPUT reads its reply.
 * @details Each item is a quoted string, or anything up to the next comma,
 * without the spaces around it. A blank text is one empty item. The items
 * are pushed onto items as BASIC_TOKEN_DATUM tokens.
 * @param text The text.
 * @param length Its length in bytes.
 * @param items A vector of basic_token_t, which the items are added to.
 * @param message Where to write why the text couldn't be split.
 * @param size The size of message.
 * @return BASIC_OK, BASIC_SYNTAX if a quoted item isn't closed or isn't
 * followed by a comma, or BASIC_NO_MEM.
 */
extern basic_error_t basic_scan_items(const char *text, size_t length, sof_vector_t *items, char *message, size_t size);

/**
 * @brief Splits a line into tokens.
 * @details line->text is set to a copy of text without the spaces around
 * it, and line->tokens to its tokens.
 * @param line The line to fill, whose fields are overwritten.
 * @param text The text of the line, after its number.
 * @param length Its length in bytes.
 * @param number The line's number, or 0 for a line run at once.
 * @param alloc The memory functions for the copy and the tokens.
 * @param message Where to write why the line couldn't be split, such as
 * "syntax error: unterminated string".
 * @param size The size of message.
 * @return BASIC_OK, BASIC_SYNTAX, or BASIC_NO_MEM; on an error, line holds
 * nothing to free.
 */
extern basic_error_t basic_scan_line(basic_line_t *line, const char *text, size_t length, unsigned long number, const sof_alloc_t *alloc, char *message, size_t size);

/**
 * @brief Frees the text and tokens of a line, leaving it empty.
 * @param line The line. The basic_line_t itself isn't freed.
 */
extern void basic_scan_free(basic_line_t *line);

#endif // BASIC_SCAN_H
