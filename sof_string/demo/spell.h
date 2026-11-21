/**
 * @file spell.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A spelling dictionary with suggestions by edit distance in C, for
 * the spell program.
 * @details A spell_t holds a dictionary of words in a hashmap, and says
 * whether a word is in it. For a word that isn't, spell_suggest() finds the
 * dictionary's words nearest to it by trying every string one or two edits
 * away: an edit deletes a byte, inserts one, replaces one, or swaps two
 * that are next to each other. The number of edits is the Damerau-Levenshtein
 * distance. Words are compared byte for byte: case isn't folded, and a
 * character of UTF-8 is as many bytes as it takes.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SPELL_H
#define SPELL_H

#include <stddef.h>
#include <stdint.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"

/**
 * @brief Error codes for the spell functions.
 */
typedef enum spell_error_t {
    SPELL_OK = 0,
    SPELL_NO_MEM,
    SPELL_NO_MATCH,
    SPELL_ERROR,
} spell_error_t;

/**
 * @brief The most edits spell_suggest() tries.
 * @details The strings tried grow with the power of the distance: about 54n
 * at one edit for a word of n letters from a-z, and the square of that at
 * two, so three would take too long to be of use.
 */
#define SPELL_MAX_DISTANCE 2

/**
 * @brief A word spell_suggest() found.
 */
typedef struct spell_suggestion_t {
    /// @brief The word, as the dictionary holds it. It is good until the
    /// dictionary is next written.
    const char *word;
    /// @brief The fewest edits that turn the word looked up into this one.
    size_t distance;
    /// @brief How many times the word was added to the dictionary.
    size_t count;
} spell_suggestion_t;

/**
 * @brief A dictionary.
 * @details words is a hashmap whose keys are the words, copies the
 * dictionary owns, and whose data is the number of times each was added,
 * held in the pointer itself. letters says which bytes are in some word, and
 * alphabet lists them, alphabet_length of them, in the order they were
 * first added: these are the bytes spell_suggest() inserts and replaces.
 * longest is the length of the longest word. alloc is a copy of the
 * allocator (zeroed, meaning the standard functions, if it was given none).
 * These fields can be read, but should not be changed by the application.
 *
 * spell_init() must be called before using a dictionary, and spell_free()
 * should be called when it is no longer needed. spell_add() and spell_load()
 * add words, spell_find() looks one up, spell_suggest() finds the nearest
 * words to one, and spell_clear() empties the dictionary.
 *
 * Every function accepts a NULL dictionary, and a dictionary that was freed
 * or whose spell_init() failed (words.elements is NULL), as described for
 * each.
 *
 * The dictionary does no locking. spell_find() and spell_suggest() only
 * read it; spell_add(), spell_load(), spell_clear(), spell_init() and
 * spell_free() write it.
 */
typedef struct spell_t {
    sof_hashmap_t words;
    unsigned char letters[256];
    unsigned char alphabet[256];
    size_t alphabet_length;
    size_t longest;
    sof_alloc_t alloc;
} spell_t;

/**
 * @brief Adds a word to the dictionary.
 * @details A word added again is counted again; spell_suggest() puts the
 * words added most often first among those at the same distance.
 * @param sp The dictionary.
 * @param word The word, NUL-terminated. The dictionary keeps a copy.
 * @return SPELL_OK, SPELL_NO_MEM, leaving the dictionary as it was, or
 * SPELL_ERROR if sp or word is NULL, word is empty, or sp isn't initialized.
 */
extern spell_error_t spell_add(spell_t *sp, const char *word);

/**
 * @brief Removes every word, keeping the dictionary ready for more.
 * @param sp The dictionary.
 * @return SPELL_OK, or SPELL_ERROR if sp is NULL or not initialized.
 */
extern spell_error_t spell_clear(spell_t *sp);

/**
 * @brief Looks a word up.
 * @param sp The dictionary.
 * @param word The word, NUL-terminated, compared byte for byte.
 * @param count If not NULL, set to the number of times the word was added,
 * or 0 if it isn't found.
 * @return The dictionary's copy of the word, good until the dictionary is
 * next written, or NULL if it isn't there, or sp or word is NULL or sp isn't
 * initialized.
 */
extern const char *spell_find(const spell_t *sp, const char *word, size_t *count);

/**
 * @brief Frees everything the dictionary allocated.
 * @details The spell_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like a dictionary whose spell_init() failed until it
 * is initialized again.
 * @param sp The dictionary.
 * @return SPELL_OK, or SPELL_ERROR if sp is NULL.
 */
extern spell_error_t spell_free(spell_t *sp);

/**
 * @brief Initializes an empty dictionary in a spell_t the caller provides.
 * @param sp The dictionary to initialize. It can be anywhere: on the stack,
 * in another structure or on the heap.
 * @param alloc The memory functions the dictionary uses, or NULL for the
 * standard functions. The dictionary keeps a copy, so it can be on the
 * stack.
 * @return SPELL_OK, SPELL_NO_MEM, or SPELL_ERROR if sp is NULL or alloc has
 * a malloc but no free; on an error, sp is left empty.
 */
extern spell_error_t spell_init(spell_t *sp, const sof_alloc_t *alloc);

/**
 * @brief Adds every word in a piece of text, such as a word list.
 * @details Words are separated by ASCII white space (space, tab, newline,
 * carriage return, vertical tab and form feed), so a list with one word on
 * each line, with either line ending, loads as it should. Each word is
 * added as spell_add() adds it.
 * @param sp The dictionary.
 * @param text The text, which need not be NUL-terminated. NULL is taken as
 * empty.
 * @param length The number of bytes of text.
 * @return SPELL_OK, SPELL_NO_MEM, leaving the words before the one that
 * couldn't be added in the dictionary, or SPELL_ERROR if sp is NULL or not
 * initialized.
 */
extern spell_error_t spell_load(spell_t *sp, const char *text, size_t length);

/**
 * @brief Finds the dictionary's words nearest to a word.
 * @details Every string up to max_distance edits from word is looked up,
 * where an edit deletes a byte, inserts a byte of the dictionary's alphabet,
 * replaces a byte with one, or swaps two neighbouring bytes, so every word
 * within that Damerau-Levenshtein distance is found. word itself, if it is
 * in the dictionary, is found at distance 0. The words found are sorted by
 * distance, then by how many times they were added, most first, then in
 * byte order, and the first max of them are put in suggestions.
 *
 * At distance 1 about 54n strings are looked up for a word of n letters
 * from a-z; at distance 2, the square of that, which takes some
 * milliseconds. A program can look at distance 1 first and go on to 2 only
 * if nothing was found.
 * @param sp The dictionary.
 * @param word The word, NUL-terminated.
 * @param max_distance The most edits, from 0 to SPELL_MAX_DISTANCE.
 * @param suggestions Where to put the words found, room for max of them. The
 * words point into the dictionary, and are good until it is next written.
 * @param max The most words to put in suggestions. 0 only counts them.
 * @param found If not NULL, set to the number of words found, which can be
 * more than max; 0 on an error.
 * @return SPELL_OK, SPELL_NO_MEM, or SPELL_ERROR if sp or word is NULL, sp
 * isn't initialized, max_distance is more than SPELL_MAX_DISTANCE, or
 * suggestions is NULL and max isn't 0.
 */
extern spell_error_t spell_suggest(const spell_t *sp, const char *word, size_t max_distance, spell_suggestion_t *suggestions, size_t max, size_t *found);

#endif // SPELL_H
