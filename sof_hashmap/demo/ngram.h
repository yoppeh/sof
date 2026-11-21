/**
 * @file ngram.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief N-gram counting in C, for the ngram program.
 * @details An ngram_t counts every n-gram, for a range of lengths n, in the
 * text it is given: runs of n characters, or of n words. Text is UTF-8, and
 * a character is a code point. Which characters make up grams is up to the
 * application: any other character breaks a run of characters, or separates
 * words. Grams overlap, so "aaa" holds the 2-gram "aa" twice. Text can be
 * given in pieces of any size; a gram, or a character's bytes, may span two
 * pieces. Bytes that aren't valid UTF-8 are read as U+FFFD, the replacement
 * character, one for each error as sof_utf8_decode() finds them.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#ifndef NGRAM_H
#define NGRAM_H

#include <stddef.h>
#include <stdint.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"
#include "sof_utf.h"

/**
 * @brief Error codes for the ngram functions.
 */
typedef enum ngram_error_t {
    NGRAM_OK = 0,
    NGRAM_NO_MEM,
    NGRAM_NO_MATCH,
    NGRAM_ERROR,
} ngram_error_t;

/**
 * @brief What a gram is made of.
 */
typedef enum ngram_unit_t {
    /// @brief Characters: an n-gram is n characters in a row.
    NGRAM_CHARS = 0,
    /// @brief Words: an n-gram is n words in a row, stored with a single
    /// space between each word and the next.
    NGRAM_WORDS,
} ngram_unit_t;

/**
 * @brief Called for each gram visited by ngram_traverse().
 * @details gram is the gram, NUL-terminated, and count the number of times it
 * was found. Return 0 to continue, or a non-zero value to stop the traversal
 * early. It must not call ngram_add(), ngram_flush(), ngram_clear() or
 * ngram_free(). ctx is the ctx given to ngram_traverse().
 */
typedef int (*ngram_traverse_func_t)(const char *gram, size_t count, void *ctx);

/**
 * @brief A range of code points in a counter's set, first to last.
 */
typedef struct ngram_range_t {
    uint32_t first;
    uint32_t last;
} ngram_range_t;

/**
 * @brief What ngram_init() is to count.
 * @details A zeroed structure counts single characters made of letters and
 * digits.
 */
typedef struct ngram_options_t {
    /// @brief Whether grams are of characters or words.
    ngram_unit_t unit;
    /// @brief The shortest gram to count; 0 means 1.
    size_t min;
    /// @brief The longest gram to count; 0 means min. Must not be less than
    /// min.
    size_t max;
    /// @brief The characters grams are made of, as a set in UTF-8 such as
    /// "a-z0-9'": each character stands for itself, and a '-' between two
    /// characters for every code point from the first to the second, so
    /// "a-zà-ÿ" adds the Latin-1 letters. A '-' first or last
    /// stands for itself. NULL means "A-Za-z0-9". It must be valid UTF-8.
    const char *chars;
    /// @brief Non-zero to fold ASCII letters to lowercase, so "The" and "the"
    /// are one gram. Both cases of each letter in chars are then in the set.
    int fold_case;
} ngram_options_t;

/**
 * @brief An n-gram counter.
 * @details maps holds a hashmap per length, from min to max, whose keys are
 * the grams of that length and whose data is each gram's count, held in the
 * pointer itself. totals holds the number of grams counted for each length,
 * and units the number of characters or words counted. map and ranges hold
 * the set, which ngram_member() reads. The other fields hold the grams in
 * progress, and alloc a copy of the allocator (zeroed, meaning
 * the standard functions, if it was given none). These fields can be read,
 * but should not be changed by the application.
 *
 * ngram_init() must be called before using a counter, and ngram_free()
 * should be called when it is no longer needed. ngram_add() counts text,
 * ngram_flush() ends it, and ngram_find(), ngram_next() and
 * ngram_traverse() read the counts. ngram_context() and ngram_merge() let
 * pieces of one text be counted by separate counters, on separate threads,
 * and the counts put together.
 *
 * Every function accepts a NULL counter, and a counter that was freed or
 * whose ngram_init() failed (maps is NULL), as described for each.
 *
 * The counter does no locking. ngram_distinct(), ngram_find(),
 * ngram_member(), ngram_next(), ngram_total() and ngram_traverse() only
 * read it, as ngram_merge() reads
 * the counter it merges from; every other function writes it.
 */
typedef struct ngram_t {
    ngram_unit_t unit;
    size_t min;
    size_t max;
    int fold_case;
    /// @brief For each ASCII character, the character it is counted as
    /// (folded to lowercase if fold_case is set), or 0 if it isn't in the
    /// set.
    unsigned char map[128];
    /// @brief The code points above ASCII in the set, as range_count ranges.
    ngram_range_t *ranges;
    size_t range_count;
    /// @brief max - min + 1 hashmaps, the first for grams of length min.
    sof_hashmap_t *maps;
    /// @brief max - min + 1 totals, the first for grams of length min.
    size_t *totals;
    size_t units;
    /// @brief Words only: the last max words, as a ring, with the length of
    /// each and the size it was allocated with. The word in progress is put
    /// together in recent[next], in place of the oldest.
    char **recent;
    size_t *recent_length;
    size_t *recent_size;
    size_t next;
    /// @brief How many characters or words in a row have been read, up to
    /// max.
    size_t run;
    /// @brief Where grams are put together, and the size it was allocated
    /// with. For characters, it holds the last run characters read, in
    /// gram_length bytes of UTF-8, with room for max of them and a NUL, so
    /// the grams ending at the last one all end at gram[gram_length].
    char *gram;
    size_t gram_size;
    size_t gram_length;
    /// @brief Characters only: the bytes each of the last run characters
    /// takes in gram, max of them.
    unsigned char *widths;
    /// @brief The bytes of a character cut off by the end of the last
    /// piece of text, finished by the next.
    unsigned char pending[SOF_UTF8_SIZE_MAX];
    size_t pending_length;
    /// @brief Non-zero while ngram_context() reads text without counting it.
    int quiet;
    sof_alloc_t alloc;
} ngram_t;

/**
 * @brief Counts the grams in a piece of text.
 * @details Text is counted as it comes, and grams can span pieces: a word or
 * run of characters at the end of one piece goes on in the next, until
 * ngram_flush() is called, and so can a character whose bytes the end of a
 * piece cuts off. A word in progress isn't counted until the character after
 * it, or ngram_flush(), ends it.
 * @param ng The counter.
 * @param text The text, which need not be NUL-terminated. NULL is taken as
 * empty.
 * @param length The number of bytes of text.
 * @return NGRAM_OK, NGRAM_NO_MEM, leaving the counts up to the gram that
 * couldn't be stored, or NGRAM_ERROR if ng is NULL or not initialized.
 */
extern ngram_error_t ngram_add(ngram_t *ng, const char *text, size_t length);

/**
 * @brief Forgets every gram counted, keeping the options.
 * @param ng The counter.
 * @return NGRAM_OK, or NGRAM_ERROR if ng is NULL or not initialized.
 */
extern ngram_error_t ngram_clear(ngram_t *ng);

/**
 * @brief Reads text that comes before the text to be counted, without
 * counting it.
 * @details The text is read as ngram_add() reads it, but no gram, character
 * or word in it is counted: it only sets up the grams in progress, so that
 * grams beginning in it and ending in the text given next to ngram_add() are
 * counted. This lets a text be cut into pieces that separate counters count
 * (see ngram_merge()): each piece is given the end of the piece before it as
 * context. Cut the text between characters, such as just after an ASCII
 * byte, which is always a whole character. For characters, cutting just
 * after one that isn't in the set needs no context; for words, the context is the last max - 1 words
 * before the cut. A word still in progress at the end of the context is
 * counted, with its grams, when ngram_add() or ngram_flush() ends it.
 * @param ng The counter.
 * @param text The text, which need not be NUL-terminated. NULL is taken as
 * empty.
 * @param length The number of bytes of text.
 * @return NGRAM_OK, NGRAM_NO_MEM, or NGRAM_ERROR if ng is NULL or not
 * initialized.
 */
extern ngram_error_t ngram_context(ngram_t *ng, const char *text, size_t length);

/**
 * @brief Returns the number of different grams of length n found.
 * @param ng The counter.
 * @param n The length.
 * @return The number, or 0 if n is outside the counter's range or ng is NULL
 * or not initialized.
 */
extern size_t ngram_distinct(const ngram_t *ng, size_t n);

/**
 * @brief Returns how many times a gram was found.
 * @details The gram is read as text is by ngram_add(): as UTF-8, its letters
 * folded if fold_case is set, and for words, split into words wherever a
 * character isn't in the set, so "of  the" and "of the" find the same
 * 2-gram. Its length is in characters, or words.
 * @param ng The counter.
 * @param gram The gram, NUL-terminated.
 * @return The count, or 0 if the gram wasn't found, isn't one gram (a
 * character of a character gram isn't in the set, or a word gram has no
 * words), is of a length outside the counter's range, is longer than 255
 * bytes as it is stored and memory couldn't be allocated to look it up, or
 * ng or gram is NULL or ng isn't initialized.
 */
extern size_t ngram_find(const ngram_t *ng, const char *gram);

/**
 * @brief Counts a word still in progress, and starts over.
 * @details No gram spans text given before and after the call, so a program
 * counting several files calls it after each one. The bytes of a character
 * cut off at the end of the text are read as U+FFFD.
 * @param ng The counter.
 * @return NGRAM_OK, NGRAM_NO_MEM, or NGRAM_ERROR if ng is NULL or not
 * initialized.
 */
extern ngram_error_t ngram_flush(ngram_t *ng);

/**
 * @brief Frees everything the counter allocated.
 * @details The ngram_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like a counter whose ngram_init() failed until it is
 * initialized again.
 * @param ng The counter.
 * @return NGRAM_OK, or NGRAM_ERROR if ng is NULL.
 */
extern ngram_error_t ngram_free(ngram_t *ng);

/**
 * @brief Initializes a counter in an ngram_t the caller provides.
 * @param ng The counter to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap.
 * @param options What to count, or NULL for the same as a zeroed
 * ngram_options_t. The counter keeps what it needs, so they can be on the
 * stack.
 * @param alloc The memory functions the counter uses, or NULL for the
 * standard functions. The counter keeps a copy, so it can be on the stack.
 * @return NGRAM_OK, NGRAM_NO_MEM, or NGRAM_ERROR if ng is NULL, alloc has a
 * malloc but no free, max is less than min, the unit isn't an
 * ngram_unit_t, or the set of chars is empty or isn't valid UTF-8; on an
 * error, ng is left empty.
 */
extern ngram_error_t ngram_init(ngram_t *ng, const ngram_options_t *options, const sof_alloc_t *alloc);

/**
 * @brief Adds the counts of another counter to this one.
 * @details Every gram of from is counted in ng as many times as it was found
 * in from, and from's totals and units are added to ng's, as if ng had
 * counted from's text too. The grams in progress in ng are kept, and from
 * is only read. The two counters must have been initialized with the same
 * options, though not the same allocator.
 * @param ng The counter to add to.
 * @param from The counter to add.
 * @return NGRAM_OK, NGRAM_NO_MEM, leaving ng with some of from's counts and
 * not others, so it should be cleared or freed, or NGRAM_ERROR if either is
 * NULL or not initialized, they are the same counter, or their options
 * differ.
 */
extern ngram_error_t ngram_merge(ngram_t *ng, const ngram_t *from);

/**
 * @brief Says whether a character is in the counter's set, and what it is
 * counted as.
 * @details This is how ngram_add() reads each character, so a program
 * cutting text for separate counters can find where words end.
 * @param ng The counter.
 * @param code The code point. Anything that isn't one, such as the
 * SOF_UTF_INVALID sof_utf8_decode() gives for bytes that aren't UTF-8, is
 * taken as U+FFFD, as ngram_add() takes it.
 * @return The code point as counted, folded to lowercase if it is an ASCII
 * letter and fold_case is set, or 0 if it isn't in the set or ng is NULL
 * or not initialized.
 */
extern uint32_t ngram_member(const ngram_t *ng, uint32_t code);

/**
 * @brief Returns the next gram of length n, for looping over them without a
 * callback.
 * @details Set *position to 0 to start, then call this until it returns NULL.
 * The grams come in no particular order. Don't call ngram_add(),
 * ngram_flush() or ngram_clear() until the loop ends.
 *
 *     size_t position = 0, count;
 *     const char *gram;
 *     while ((gram = ngram_next(&ng, 2, &position, &count)) != NULL) {
 *         ...
 *     }
 *
 * @param ng The counter.
 * @param n The length of the grams.
 * @param position Where the loop is; updated on each call.
 * @param count If not NULL, set to the gram's count.
 * @return The next gram, which stays good until the counter is next written,
 * or NULL when there are no more, n is outside the range, or ng or position
 * is NULL.
 */
extern const char *ngram_next(const ngram_t *ng, size_t n, size_t *position, size_t *count);

/**
 * @brief Returns the number of grams of length n found, counting each time
 * a gram was found.
 * @param ng The counter.
 * @param n The length.
 * @return The number, or 0 if n is outside the counter's range or ng is NULL
 * or not initialized.
 */
extern size_t ngram_total(const ngram_t *ng, size_t n);

/**
 * @brief Calls cb for each gram of length n.
 * @details The grams are visited in no particular order. If cb returns a
 * non-zero value, the traversal stops and returns that value.
 * @param ng The counter.
 * @param n The length of the grams.
 * @param cb The function called for each gram. NULL visits nothing.
 * @param ctx Passed to every call of cb.
 * @return 0 if every gram was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if ng is NULL or not initialized, or n is
 * outside the counter's range.
 */
extern int ngram_traverse(const ngram_t *ng, size_t n, ngram_traverse_func_t cb, void *ctx);

#endif // NGRAM_H
