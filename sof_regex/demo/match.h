/**
 * @file match.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Line-by-line regular expression search in c, for the match program.
 * @details A match_t searches text, given in pieces of any size, for a PCRE2
 * regular expression, one line at a time, and calls the application's
 * function with each line it selects: those that match, or with invert set,
 * those that don't. With only_matching set, the function is called with
 * each match in a selected line instead, and with all_lines set, it is
 * called with every other line too, for a program that prints lines around
 * those selected. A line ends at a newline, which
 * isn't part of it, or at the end of the text. Lines can hold any bytes,
 * NULs included. Lines are held by sof_string and searched by sof_regex,
 * which gets PCRE2's memory from the match_t's allocator.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef MATCH_H
#define MATCH_H

#include <stddef.h>

#include "sof_alloc.h"
#include "sof_regex.h"
#include "sof_string.h"

/**
 * @brief Error codes for the match functions.
 */
typedef enum match_error_t {
    MATCH_OK = 0,
    MATCH_NO_MEM,
    /// @brief Not returned: a line that doesn't match isn't an error.
    MATCH_NO_MATCH,
    MATCH_ERROR,
    /// @brief match_init() was given a pattern that doesn't compile.
    MATCH_BAD_REGEX,
    /// @brief PCRE2 stopped a search, as when it hits its match limit.
    MATCH_MATCH_ERROR,
} match_error_t;

/**
 * @brief Called with each line, or with only_matching each match, selected,
 * and with all_lines, each line that isn't.
 * @details text holds length bytes, which aren't NUL-terminated and can
 * hold NULs; without only_matching, or for a line that isn't selected, it
 * is the whole line, without its newline. line is the line's number,
 * counting from 1 at match_init() or match_clear(). selected is 1 for a
 * line or match selected and 0 for a line that isn't. ctx is the ctx given to match_init(). The text is good only
 * until the function returns. It must not call match_add(), match_flush(),
 * match_clear() or match_free().
 */
typedef void (*match_found_func_t)(const char *text, size_t length, size_t line, int selected, void *ctx);

/**
 * @brief What match_init() is to search for.
 * @details A zeroed structure selects the lines that match, case and all.
 */
typedef struct match_options_t {
    /// @brief Non-zero to ignore case, by putting "(?i)" before the
    /// pattern. Only ASCII letters are folded, since lines are bytes.
    int ignore_case;
    /// @brief Non-zero to select the lines that don't match.
    int invert;
    /// @brief Non-zero to call the found function with each non-empty
    /// match in a selected line, rather than with the line. With invert,
    /// the lines selected have no matches, so it is never called.
    int only_matching;
    /// @brief Non-zero to call the found function with each line that
    /// isn't selected as well, whole, with selected 0. Such lines aren't
    /// counted in selected or matches.
    int all_lines;
} match_options_t;

/**
 * @brief A line searcher.
 * @details regex is the compiled expression, line the line in progress,
 * options a copy of those given to match_init(), found and ctx the function
 * and context given to it, and alloc a copy of the allocator (zeroed,
 * meaning the standard functions, if it was given none). lines is the
 * number of lines read, selected the number of lines selected, and matches
 * the number of times found was called with a match (only_matching only),
 * all since match_init() or match_clear(). These fields can be read, but
 * should not be changed by the application.
 *
 * match_init() must be called before using a searcher, and match_free()
 * should be called when it is no longer needed. match_add() searches text
 * as it comes, match_flush() ends it, and match_clear() starts again.
 *
 * Every function accepts a NULL searcher, and a searcher that was freed or
 * whose match_init() failed (line.data is NULL), as described for each.
 *
 * The searcher does no locking, and every function writes it.
 */
typedef struct match_t {
    sof_regex_t regex;
    sof_string_t line;
    match_options_t options;
    match_found_func_t found;
    void *ctx;
    size_t lines;
    size_t selected;
    size_t matches;
    sof_alloc_t alloc;
} match_t;

/**
 * @brief Searches a piece of text.
 * @details Each line the piece ends is searched, and found called for it if
 * it is selected. A line the piece doesn't end is kept, and goes on in the
 * next piece, until a newline or match_flush() ends it.
 * @param m The searcher.
 * @param text The text, which need not be NUL-terminated. NULL is taken as
 * empty.
 * @param length The number of bytes of text.
 * @return MATCH_OK; MATCH_NO_MEM or MATCH_MATCH_ERROR, after the lines
 * before the one that failed were searched and with that line lost, so the
 * searcher should be cleared or freed; or MATCH_ERROR if m is NULL or not
 * initialized.
 */
extern match_error_t match_add(match_t *m, const char *text, size_t length);

/**
 * @brief Forgets the line in progress and sets the counts to 0.
 * @details The next line read is line 1. A program searching several files
 * calls it after each one.
 * @param m The searcher.
 * @return MATCH_OK, or MATCH_ERROR if m is NULL or not initialized.
 */
extern match_error_t match_clear(match_t *m);

/**
 * @brief Searches the line in progress, if the text didn't end with a
 * newline.
 * @details A program calls it at the end of each file, so that a last line
 * without a newline is searched. The counts are kept.
 * @param m The searcher.
 * @return MATCH_OK, MATCH_NO_MEM or MATCH_MATCH_ERROR if searching the
 * line failed (it is lost), or MATCH_ERROR if m is NULL or not initialized.
 */
extern match_error_t match_flush(match_t *m);

/**
 * @brief Frees everything the searcher allocated.
 * @details The match_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like a searcher whose match_init() failed until it is
 * initialized again.
 * @param m The searcher.
 * @return MATCH_OK, or MATCH_ERROR if m is NULL.
 */
extern match_error_t match_free(match_t *m);

/**
 * @brief Initializes a searcher in a match_t the caller provides.
 * @param m The searcher to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap.
 * @param pattern The regular expression, NUL-terminated, in PCRE2's syntax,
 * which is Perl's. It matches bytes, not UTF-8 characters, and is searched
 * for in each line separately, so ^ and $ match at the start and end of a
 * line.
 * @param options How to search, or NULL for the same as a zeroed
 * match_options_t. The searcher keeps a copy.
 * @param found The function called with each line or match selected, or
 * NULL to only count them.
 * @param ctx Passed to every call of found.
 * @param alloc The memory functions the searcher, and PCRE2, use, or NULL
 * for the standard functions. The searcher keeps a copy, so it can be on the
 * stack.
 * @return MATCH_OK, MATCH_BAD_REGEX if pattern doesn't compile, MATCH_NO_MEM,
 * or MATCH_ERROR if m or pattern is NULL or alloc has a malloc but no free;
 * on an error, m is left empty.
 */
extern match_error_t match_init(match_t *m, const char *pattern, const match_options_t *options, match_found_func_t found, void *ctx, const sof_alloc_t *alloc);

#endif // MATCH_H
