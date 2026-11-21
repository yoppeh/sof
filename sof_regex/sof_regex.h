/**
 * @file sof_regex.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Regular expression search and split of sof_string strings in C.
 * @details A sof_regex_t is a compiled PCRE2 regular expression, which
 * sof_regex_find() and sof_regex_split() use to walk through a
 * sof_string_t with a position the caller keeps.
 * @version 0.0.1
 * @date 2026-11-05
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_REGEX_H
#define SOF_REGEX_H

#include <stddef.h>

#include "sof_alloc.h"
#include "sof_string.h"

/**
 * @brief Error codes for the sof_regex functions.
 */
typedef enum sof_regex_error_t {
    SOF_REGEX_OK = 0,
    SOF_REGEX_NO_MEM,
    SOF_REGEX_NO_MATCH,
    SOF_REGEX_ERROR,
    /// @brief sof_regex_init() was given a pattern that doesn't compile.
    SOF_REGEX_BAD_PATTERN,
    /// @brief PCRE2 stopped a match, as when it hits its match limit.
    SOF_REGEX_MATCH_ERROR,
} sof_regex_error_t;

/**
 * @brief A compiled regular expression.
 * @details The application provides the sof_regex_t and sets it up with
 * sof_regex_init(). The fields belong to the library: they are PCRE2's
 * compiled pattern, match data and match context, and the allocator PCRE2
 * uses, and are all NULL for a regex that was freed or whose initialization
 * failed. The match data is written by every search, so one regex can't be
 * used by two searches at once on different threads, but it can be used by
 * several searches in turn, and on any number of strings.
 */
typedef struct sof_regex_t {
    /// @brief The compiled pattern (a pcre2_code *), or NULL.
    void *code;
    /// @brief Where PCRE2 puts a match (a pcre2_match_data *).
    void *match_data;
    /// @brief The match context (a pcre2_match_context *).
    void *match_context;
    /// @brief A heap copy of the allocator, which PCRE2 is given.
    void *memory;
} sof_regex_t;

/**
 * @brief Finds the next match of a regular expression.
 * @details The search starts at *position, which the caller sets to 0 to
 * search from the start, and moves past the match, so a loop of calls finds
 * every match in turn:
 * @code
 * size_t position = 0, offset, length;
 * while (sof_regex_find(&re, &s, &position, &offset, &length)
 *        == SOF_REGEX_OK)
 * @endcode
 * After an empty match, *position moves one byte past it, so a regex that
 * can match the empty string doesn't find the same match forever ("x*" in
 * "ab" matches at 0, 1 and 2). The subject is only read, and ^ matches only
 * at its start, whatever *position is. A dead subject is empty.
 * @param regex The regular expression.
 * @param subject The string to search.
 * @param position Where to start, set past the match on SOF_REGEX_OK and
 * left alone otherwise. Past the subject's length, nothing is found.
 * @param match_offset If not NULL, set to where the match starts.
 * @param match_length If not NULL, set to its length, which can be 0.
 * @return SOF_REGEX_OK, SOF_REGEX_NO_MATCH if there are no more matches,
 * SOF_REGEX_NO_MEM if PCRE2 couldn't get memory for the match,
 * SOF_REGEX_MATCH_ERROR if PCRE2 stopped it otherwise, or SOF_REGEX_ERROR
 * if regex, subject or position is NULL, or regex is dead.
 */
extern sof_regex_error_t sof_regex_find(sof_regex_t *regex, const sof_string_t *subject, size_t *position, size_t *match_offset, size_t *match_length);

/**
 * @brief Frees a regular expression's memory.
 * @details The sof_regex_t itself isn't freed; it is left dead until it is
 * initialized again.
 * @param regex The regular expression to free.
 * @return SOF_REGEX_OK, or SOF_REGEX_ERROR if regex is NULL.
 */
extern sof_regex_error_t sof_regex_free(sof_regex_t *regex);

/**
 * @brief Compiles a regular expression.
 * @details pattern is in PCRE2's syntax, which is Perl's, and matches bytes,
 * not UTF-8 characters. Options can be set inside it, as "(?i)" does for
 * case-insensitive matching. PCRE2 gets all of its memory, for compiling and
 * for matching, from alloc, but for the machine code sof_regex_jit() makes.
 * regex is overwritten without being freed.
 * @param regex The regular expression to initialize.
 * @param pattern The NUL-terminated pattern.
 * @param alloc The memory functions for PCRE2, or NULL for the standard
 * functions. The regex keeps a copy, so it can be on the stack.
 * @return SOF_REGEX_OK, SOF_REGEX_BAD_PATTERN if pattern doesn't compile,
 * SOF_REGEX_NO_MEM, or SOF_REGEX_ERROR if regex or pattern is NULL or
 * alloc has a malloc but no free. On failure regex is left dead.
 */
extern sof_regex_error_t sof_regex_init(sof_regex_t *regex, const char *pattern, const sof_alloc_t *alloc);

/**
 * @brief Compiles a regular expression to machine code, with PCRE2's JIT,
 * for faster searches.
 * @details Optional, and off unless this is called: call it once after
 * sof_regex_init(), and sof_regex_find() and sof_regex_split() then run the
 * machine code, finding the same matches and fields. Compiling takes one
 * to three microseconds more per pattern, so it pays for a regex that searches more
 * than a few kilobytes. It is the one exception to the regex getting all its
 * memory from its allocator: the machine code lives in executable memory
 * PCRE2 maps for itself, freed by sof_regex_free(). The machine code runs on
 * 32 KiB of the thread's own stack; a search that needs more is run again
 * without it, so it gets the same result. PCRE2's limits are counted
 * differently by the machine code: the heap limit doesn't apply, and the
 * match limit counts other steps, so a pattern that hits a limit
 * (SOF_REGEX_MATCH_ERROR) without it may not with it, and the other way
 * round. Where PCRE2 was built without its JIT, or the code can't be made,
 * the regex is left as it was and goes on working without it.
 * @param regex The regular expression.
 * @return SOF_REGEX_OK if the regex now runs as machine code (also if it
 * already did), SOF_REGEX_NO_MATCH if it couldn't be compiled and is left
 * as it was, or SOF_REGEX_ERROR if regex is NULL or dead.
 */
extern sof_regex_error_t sof_regex_jit(sof_regex_t *regex);

/**
 * @brief Gives the size of a regular expression's machine code.
 * @details That is the memory sof_regex_jit() took outside the regex's
 * allocator, for an application that keeps count of its memory. PCRE2 maps
 * executable memory in larger pieces and shares them among patterns, so
 * the memory the program has mapped can be somewhat more than the sum.
 * @param regex The regular expression.
 * @return The size in bytes, or 0 if regex has no machine code: it wasn't
 * given to sof_regex_jit(), that couldn't compile it, or regex is NULL or
 * dead.
 */
extern size_t sof_regex_jit_size(const sof_regex_t *regex);

/**
 * @brief Finds the next field of a string split by a regular expression.
 * @details A field is the text up to the next match of the regex, which is
 * the delimiter and isn't part of any field. A string with n delimiters has
 * n + 1 fields, so delimiters at the start or end, or next to each other,
 * give empty fields, and a string with no delimiter, the empty string
 * included, is one field. The caller sets *position to 0 to start, and each
 * call moves it to the next field, so a loop of calls returns every field:
 * @code
 * size_t position = 0, offset, length;
 * while (sof_regex_split(&re, &s, &position, &offset, &length)
 *        == SOF_REGEX_OK)
 * @endcode
 * After the last field, *position is past the subject's length. A delimiter
 * is never an empty match at the start of a field, so a regex that can
 * match the empty string splits between bytes ("x*" splits "ab" into "a",
 * "b" and ""). A dead subject is empty.
 * @param regex The regular expression that matches a delimiter.
 * @param subject The string to split.
 * @param position Where the next field starts, set to where the one after it
 * starts on SOF_REGEX_OK and left alone otherwise.
 * @param field_offset If not NULL, set to where the field starts.
 * @param field_length If not NULL, set to its length, which can be 0.
 * @return SOF_REGEX_OK, SOF_REGEX_NO_MATCH after the last field,
 * SOF_REGEX_NO_MEM if PCRE2 couldn't get memory for the match,
 * SOF_REGEX_MATCH_ERROR if PCRE2 stopped it otherwise, or SOF_REGEX_ERROR
 * if regex, subject or position is NULL, or regex is dead.
 */
extern sof_regex_error_t sof_regex_split(sof_regex_t *regex, const sof_string_t *subject, size_t *position, size_t *field_offset, size_t *field_length);

#endif // SOF_REGEX_H
