/**
 * @file markov.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A Markov chain text generator in C, for the markov program.
 * @details A markov_t learns the n-grams of a text, of words or of
 * characters, and generates new text from them: each word or character is
 * chosen at random from those that followed the n - 1 before it in the text,
 * as often as they followed them. The counting is done by an ngram_t (see
 * ../../sof_hashmap/demo/ngram.h), which counts the n-grams and the (n - 1)-grams; the
 * (n - 1)-grams are the states of the chain, and the n-grams its
 * transitions. Random numbers come from a sof_random_t the caller seeds, and
 * every choice is made from a list sorted by bytes, with integer weights, so
 * the same seed and text give the same output on every platform.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef MARKOV_H
#define MARKOV_H

#include <stddef.h>
#include <stdint.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"
#include "sof_random.h"
#include "sof_string.h"
#include "ngram.h"

/**
 * @brief The set of characters words are made of when the options give none:
 * every printable ASCII character but the space, the Latin-1 letters and
 * signs, the em dash and the curly quotes, so punctuation stays with its
 * word.
 */
#define MARKOV_WORD_CHARS "!-~¡-ÿ—‘-”"

/**
 * @brief The set of characters character grams are made of when the options
 * give none: as MARKOV_WORD_CHARS, with the space.
 */
#define MARKOV_CHAR_CHARS " -~¡-ÿ—‘-”"

/**
 * @brief Error codes for the markov functions.
 */
typedef enum markov_error_t {
    MARKOV_OK = 0,
    MARKOV_NO_MEM,
    MARKOV_NO_MATCH,
    MARKOV_ERROR,
} markov_error_t;

/**
 * @brief What markov_init() is to learn.
 * @details A zeroed structure learns pairs of characters from
 * MARKOV_CHAR_CHARS.
 */
typedef struct markov_options_t {
    /// @brief Whether the chain is of characters or words.
    ngram_unit_t unit;
    /// @brief n, the length of the grams counted: each character or word is
    /// chosen by the n - 1 before it. 0 means 2; 1 is an error.
    size_t order;
    /// @brief The characters words or character grams are made of, as a set
    /// in UTF-8, as ngram_options_t takes it. NULL means MARKOV_WORD_CHARS
    /// for words and MARKOV_CHAR_CHARS for characters. For words it must not
    /// hold the space, which separates the words of a gram.
    const char *chars;
    /// @brief Non-zero to fold ASCII letters to lowercase.
    int fold_case;
} markov_options_t;

/**
 * @brief A word or character that follows a state, and how often it did.
 */
typedef struct markov_successor_t {
    /// @brief The word or character, NUL-terminated: the end of an n-gram
    /// in the counter.
    const char *unit;
    /// @brief How many times it followed the state's context.
    size_t count;
} markov_successor_t;

/**
 * @brief A state of the chain: n - 1 words or characters, and what followed
 * them.
 */
typedef struct markov_state_t {
    /// @brief The n - 1 words or characters, as the counter stores them:
    /// words joined by single spaces, and letters folded if fold_case is set.
    const char *context;
    /// @brief How many times the context was found in the text.
    size_t count;
    /// @brief How many times something followed it: the sum of the
    /// successors' counts, which is count less the times it ended a run.
    uint64_t total;
    /// @brief The words or characters that followed it, sorted by bytes,
    /// next_count of them.
    markov_successor_t *next;
    size_t next_count;
} markov_state_t;

/**
 * @brief A Markov chain text generator.
 * @details counts counts the n-grams and (n - 1)-grams of the text.
 * markov_build() makes the chain from them: states, sorted by context,
 * holding state_count states; successors, holding every state's successors
 * one after another; index, a hashmap from each context to its state; and
 * starts, the start_count states with a successor, sorted by context, and the
 * sum of their totals. index.elements is NULL while there is no chain, before
 * markov_build() and after anything changes the counts. alloc is a copy of
 * the allocator (zeroed, meaning the standard functions, if it was given
 * none). These fields can be read, but should not be changed by the
 * application. The strings in the chain point into the counter, so they, and
 * the states, are good until the counts next change.
 *
 * markov_init() must be called before using a generator, and markov_free()
 * should be called when it is no longer needed. markov_add() learns text,
 * markov_flush() ends it, and markov_generate() makes new text.
 *
 * Every function accepts a NULL generator, and one that was freed or whose
 * markov_init() failed (counts.maps is NULL), as described for each.
 *
 * The generator does no locking. markov_find() only reads it, and so does
 * markov_generate() once markov_build() has made the chain; every other
 * function writes it.
 */
typedef struct markov_t {
    ngram_t counts;
    size_t order;
    sof_hashmap_t index;
    markov_state_t *states;
    size_t state_count;
    markov_successor_t *successors;
    size_t successor_count;
    markov_state_t **starts;
    size_t start_count;
    uint64_t start_total;
    sof_alloc_t alloc;
} markov_t;

/**
 * @brief Learns a piece of text.
 * @details The text is counted by ngram_add(), so grams can span pieces, until
 * markov_flush() ends the text. The chain, if markov_build() made one, is
 * dropped.
 * @param m The generator.
 * @param text The text, in UTF-8, which need not be NUL-terminated. NULL is
 * taken as empty.
 * @param length The number of bytes of text.
 * @return MARKOV_OK, MARKOV_NO_MEM, leaving the counts up to the gram that
 * couldn't be stored, or MARKOV_ERROR if m is NULL or not initialized.
 */
extern markov_error_t markov_add(markov_t *m, const char *text, size_t length);

/**
 * @brief Makes the chain from the counts.
 * @details Does nothing if the chain is already made and the counts haven't
 * changed since. markov_generate() calls it, so it need only be called to
 * make the chain ahead of time, as before sharing the generator between
 * threads, or to read it with markov_find().
 * @param m The generator.
 * @return MARKOV_OK, MARKOV_NO_MEM, leaving no chain, or MARKOV_ERROR if m is
 * NULL or not initialized.
 */
extern markov_error_t markov_build(markov_t *m);

/**
 * @brief Forgets everything learned, keeping the options.
 * @param m The generator.
 * @return MARKOV_OK, or MARKOV_ERROR if m is NULL or not initialized.
 */
extern markov_error_t markov_clear(markov_t *m);

/**
 * @brief Finds the state for a context.
 * @param m The generator.
 * @param context The n - 1 words or characters, NUL-terminated, as the
 * counter stores them: words joined by single spaces, and letters folded if
 * fold_case is set.
 * @return The state, good until the counts next change, or NULL if the
 * context wasn't found, there is no chain (see markov_build()), or m or
 * context is NULL or m isn't initialized.
 */
extern const markov_state_t *markov_find(const markov_t *m, const char *context);

/**
 * @brief Ends the text: no gram spans the text given before and after the
 * call.
 * @details Call it at the end of each file. The chain, if there is one, is
 * dropped.
 * @param m The generator.
 * @return MARKOV_OK, MARKOV_NO_MEM, or MARKOV_ERROR if m is NULL or not
 * initialized.
 */
extern markov_error_t markov_flush(markov_t *m);

/**
 * @brief Frees everything the generator allocated.
 * @details The markov_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like a generator whose markov_init() failed until it
 * is initialized again.
 * @param m The generator.
 * @return MARKOV_OK, or MARKOV_ERROR if m is NULL.
 */
extern markov_error_t markov_free(markov_t *m);

/**
 * @brief Generates text, appending it to a string.
 * @details Makes the chain first if need be (see markov_build()). The text
 * starts with a context chosen at random from the starts, each as likely as
 * its total, then each word or character is chosen at random from the
 * successors of the n - 1 before it, each as likely as its count. Where those
 * have no successor (they ended the text, or a run of characters), it starts
 * again from a new context, after a newline. Words are joined by single
 * spaces; characters are appended as they are. With the same seed in r and
 * the same text learned, the text is the same on every platform.
 * @param m The generator.
 * @param r The random number generator, initialized by the caller, which is
 * advanced.
 * @param length The number of words or characters to generate, not counting
 * spaces between words or newlines. 0 generates nothing.
 * @param out The string the text is appended to, initialized by the caller,
 * with any allocator.
 * @return MARKOV_OK, MARKOV_NO_MATCH if nothing was learned that can start a
 * chain, so nothing is appended, MARKOV_NO_MEM, leaving part of the text
 * appended, or MARKOV_ERROR if m is NULL or not initialized, r is NULL or not
 * initialized, or out is NULL, dead or a sof_string_const().
 */
extern markov_error_t markov_generate(markov_t *m, sof_random_t *r, size_t length, sof_string_t *out);

/**
 * @brief Initializes a generator in a markov_t the caller provides.
 * @param m The generator to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap.
 * @param options What to learn, or NULL for the same as a zeroed
 * markov_options_t. The generator keeps what it needs, so they can be on the
 * stack.
 * @param alloc The memory functions the generator uses, or NULL for the
 * standard functions. The generator keeps a copy, so it can be on the stack.
 * @return MARKOV_OK, MARKOV_NO_MEM, or MARKOV_ERROR if m is NULL, alloc has a
 * malloc but no free, the order is 1, the unit isn't an ngram_unit_t, the set
 * of chars is empty or isn't valid UTF-8, or the unit is words and the set
 * holds the space; on an error, m is left empty.
 */
extern markov_error_t markov_init(markov_t *m, const markov_options_t *options, const sof_alloc_t *alloc);

#endif // MARKOV_H
