/**
 * @file top.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Counting words, lines or n-grams of words, and picking the most
 * frequent, in C, for the top program.
 * @details A top_t counts keys in a hashmap: the words of the lines it is
 * given, the runs of n words in a row (n-grams, which go on from one line to
 * the next), or the lines themselves. top_select() then picks the most
 * frequent keys with a min-heap of the size asked for, so picking the top N
 * of D distinct keys takes O(D log N) time and memory for N entries, not a
 * sort of all D.
 *
 * Keys found equally often are ordered by their bytes, as strcmp() orders
 * them: of two keys with the same count, the one that is less comes first,
 * and is the one kept when only one fits. The result is the same whatever
 * order the keys were counted in.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef TOP_H
#define TOP_H

#include <stddef.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"
#include "sof_regex.h"
#include "sof_string.h"

/**
 * @brief The regular expression a word matches unless the options give
 * another: letters and digits, with an apostrophe (' or ’, U+2019) between
 * two of them, so "don't" and "o’er" are words and quotes around a word
 * aren't part of it.
 */
#define TOP_DEFAULT_WORD "[A-Za-z0-9]+(?:(?:'|\\xe2\\x80\\x99)[A-Za-z0-9]+)*"

/**
 * @brief Error codes for the top functions.
 */
typedef enum top_error_t {
    TOP_OK = 0,
    TOP_NO_MEM,
    TOP_NO_MATCH,
    TOP_ERROR,
    /// @brief top_init() was given a word pattern that doesn't compile.
    TOP_BAD_REGEX,
} top_error_t;

/**
 * @brief What a key is.
 */
typedef enum top_unit_t {
    /// @brief n words in a row, joined by single spaces.
    TOP_WORDS = 0,
    /// @brief A whole line.
    TOP_LINES,
} top_unit_t;

/**
 * @brief What top_init() is to count.
 * @details A zeroed structure counts single words, as TOP_DEFAULT_WORD
 * matches them, keeping their case.
 */
typedef struct top_options_t {
    /// @brief Whether keys are words or lines.
    top_unit_t unit;
    /// @brief Words only: the words in each key; 0 means 1. Must be 0 or 1
    /// for lines.
    size_t n;
    /// @brief Words only: the PCRE2 regular expression a word matches, or
    /// NULL for TOP_DEFAULT_WORD. It matches bytes, not UTF-8 characters.
    /// Empty matches are skipped.
    const char *word;
    /// @brief Non-zero to fold ASCII letters to lowercase before anything
    /// else, so "The" and "the" are one key.
    int fold_case;
} top_options_t;

/**
 * @brief A key and how many times it was counted, as top_select() gives
 * them.
 * @details key points into the counter, and is good until it is next
 * written.
 */
typedef struct top_entry_t {
    const char *key;
    size_t count;
} top_entry_t;

/**
 * @brief A counter.
 * @details map holds the keys counted, NUL-terminated copies the map owns,
 * with each key's count in the element's data pointer itself; total is the
 * number of keys counted, each time it was counted. regex, line, gram,
 * lengths, words and oldest are for splitting lines into words: the line
 * being split, the last n words (or fewer) joined by spaces, and the length
 * of each, as a ring starting at oldest. alloc is a copy of the allocator
 * (zeroed, meaning the standard functions, if it was given none). The fields
 * can be read, but should not be changed by the application.
 *
 * top_init() must be called before using a counter, and top_free() when it
 * is no longer needed. top_add_line() counts a line's words or the line,
 * top_flush() ends a text so no n-gram spans two, top_add() counts any key,
 * top_find() and top_next() read the counts, and top_select() picks the most
 * frequent.
 *
 * Every function accepts a NULL counter, and one that was freed or whose
 * top_init() failed (map.elements is NULL), as described for each.
 *
 * The counter does no locking. top_find(), top_next() and top_select() only
 * read it; every other function writes it.
 */
typedef struct top_t {
    top_unit_t unit;
    size_t n;
    int fold_case;
    sof_hashmap_t map;
    size_t total;
    sof_regex_t regex;
    sof_string_t line;
    sof_string_t gram;
    size_t *lengths;
    size_t words;
    size_t oldest;
    sof_alloc_t alloc;
} top_t;

/**
 * @brief Counts a key.
 * @details The key is counted as given, whatever the counter's unit, and
 * isn't folded to lowercase.
 * @param t The counter.
 * @param key The key, NUL-terminated. The counter keeps a copy.
 * @param count How many times to count it. 0 does nothing.
 * @return TOP_OK, TOP_NO_MEM if the key is new and couldn't be stored (the
 * counts are unchanged), or TOP_ERROR if t or key is NULL or t isn't
 * initialized.
 */
extern top_error_t top_add(top_t *t, const char *key, size_t count);

/**
 * @brief Counts the words, the n-grams of words, or the line itself, in a
 * line of text.
 * @details For lines, the line is one key, folded to lowercase if
 * fold_case is set. For words, the words are found with the counter's
 * regular expression, after folding, and each run of n words in a row is
 * counted, joined by single spaces. A run can span lines: the last n - 1
 * words of a line start the runs of the next, until top_flush() is called.
 * @param t The counter.
 * @param line The line, NUL-terminated, without its newline. A NUL ends it,
 * so a line holding one is counted up to there.
 * @return TOP_OK, TOP_NO_MEM if memory ran out, in which case the keys before
 * the one that couldn't be stored are counted and the words in progress are
 * forgotten, as by top_flush(), or TOP_ERROR if t or line is NULL, t isn't
 * initialized, or PCRE2 stopped a match.
 */
extern top_error_t top_add_line(top_t *t, const char *line);

/**
 * @brief Forgets every key counted, and the words in progress, keeping the
 * options.
 * @param t The counter.
 * @return TOP_OK, or TOP_ERROR if t is NULL or not initialized.
 */
extern top_error_t top_clear(top_t *t);

/**
 * @brief Orders entries as top_select() gives them.
 * @details A compare function for sof_sort and the like: the higher count
 * comes first, and of equal counts, the key that strcmp() puts first.
 * @param a A top_entry_t.
 * @param b Another.
 * @param ctx Not used.
 * @return Less than 0 if a comes before b, 0 if they are the same key and
 * count, or greater than 0 if a comes after b.
 */
extern int top_entry_compare(const void *a, const void *b, void *ctx);

/**
 * @brief Returns how many times a key was counted.
 * @details Only reads the counter.
 * @param t The counter.
 * @param key The key, NUL-terminated, as it was counted (folded, and with
 * single spaces between words).
 * @return The count, or 0 if the key wasn't counted, or t or key is NULL or
 * t isn't initialized.
 */
extern size_t top_find(const top_t *t, const char *key);

/**
 * @brief Ends a text: the next line's words start new n-grams.
 * @details A program counting several files calls it after each one, so no
 * n-gram spans two. Lines and single words need no flush.
 * @param t The counter.
 * @return TOP_OK, or TOP_ERROR if t is NULL or not initialized.
 */
extern top_error_t top_flush(top_t *t);

/**
 * @brief Frees everything the counter allocated.
 * @details The top_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like a counter whose top_init() failed until it is
 * initialized again.
 * @param t The counter.
 * @return TOP_OK, or TOP_ERROR if t is NULL.
 */
extern top_error_t top_free(top_t *t);

/**
 * @brief Initializes a counter in a top_t the caller provides.
 * @param t The counter to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap.
 * @param options What to count, or NULL for the same as a zeroed
 * top_options_t. The counter keeps what it needs, so they can be on the
 * stack.
 * @param alloc The memory functions the counter uses, PCRE2 included, or
 * NULL for the standard functions. The counter keeps a copy, so it can be
 * on the stack.
 * @return TOP_OK, TOP_NO_MEM, TOP_BAD_REGEX if the word pattern doesn't
 * compile, or TOP_ERROR if t is NULL, alloc has a malloc but no free, the
 * unit isn't a top_unit_t, or n is more than 1 for lines; on an error, t is
 * left empty.
 */
extern top_error_t top_init(top_t *t, const top_options_t *options, const sof_alloc_t *alloc);

/**
 * @brief Returns the next key, for looping over them without a callback.
 * @details Set *position to 0 to start, then call this until it returns
 * NULL. The keys come in no particular order. Only reads the counter; don't
 * count anything until the loop ends.
 *
 *     size_t position = 0, count;
 *     const char *key;
 *     while ((key = top_next(&t, &position, &count)) != NULL) {
 *         ...
 *     }
 *
 * @param t The counter.
 * @param position Where the loop is; updated on each call.
 * @param count If not NULL, set to the key's count.
 * @return The next key, good until the counter is next written, or NULL when
 * there are no more, or t or position is NULL.
 */
extern const char *top_next(const top_t *t, size_t *position, size_t *count);

/**
 * @brief Picks the most frequent keys.
 * @details Keeps the n best keys seen in a min-heap of n entries, whose
 * smallest is the worst of them, as it walks the map, replacing that one
 * whenever a better key comes along; then empties the heap into entries,
 * from the back. Better means as top_entry_compare() orders them: a higher
 * count, or the same count and a key that strcmp() puts first. Only reads
 * the counter; the heap is allocated with the counter's allocator, and
 * freed before it returns.
 * @param t The counter.
 * @param n The most entries wanted.
 * @param entries Room for n entries, filled with the best, best first.
 * Their keys point into the counter, and are good until it is next written.
 * @param found If not NULL, set to the number of entries filled: n, or the
 * number of distinct keys if there are fewer. Set to 0 on an error.
 * @return TOP_OK, TOP_NO_MEM if the heap couldn't be allocated, or
 * TOP_ERROR if t is NULL or not initialized, or entries is NULL and n isn't
 * 0.
 */
extern top_error_t top_select(const top_t *t, size_t n, top_entry_t *entries, size_t *found);

#endif // TOP_H
