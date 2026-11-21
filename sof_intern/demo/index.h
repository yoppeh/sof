/**
 * @file index.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A concordance in C, for the index program: each word of a text, in
 * order, with the numbers of the lines it is on.
 * @details An index_t reads text a piece at a time, cuts it into lines at
 * each '\n', and finds the words of each line. A word is a run of letters,
 * which are the ASCII letters and the letters of Latin-1 Supplement and
 * Latin Extended-A and -B (U+00C0 to U+024F, but for U+00D7 and U+00F7, the
 * multiplication and division signs), in UTF-8, with an apostrophe (' or ’,
 * U+2019) allowed between two letters, as in "don't" and "o’er". Anything
 * else separates words. Each word is interned in a sof_intern pool, so it
 * is stored once however often it occurs, and its entries are kept in a
 * sof_rbtree, in byte order, which for UTF-8 is code point order; each
 * holds a sof_vector of the lines it is on, each once, in ascending order.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef INDEX_H
#define INDEX_H

#include <stddef.h>

#include "sof_alloc.h"
#include "sof_intern.h"
#include "sof_rbtree.h"
#include "sof_string.h"
#include "sof_vector.h"

/**
 * @brief Error codes for the index functions.
 */
typedef enum index_error_t {
    INDEX_OK = 0,
    INDEX_NO_MEM,
    INDEX_NO_MATCH,
    INDEX_ERROR,
} index_error_t;

/**
 * @brief A word in the index.
 * @details word is the word, as a sof_string_view() of its interned copy
 * in the index's pool, so its data is NUL-terminated and the same pointer
 * for every occurrence of the word;
 * lines holds the numbers of the lines it is on, as size_t, each once and in
 * ascending order, counting from 1; count is the number of times it was
 * found, which is more than the number of lines if it is on a line more than
 * once. node is the word's node in the index's tree, whose key is &word and
 * whose data is the entry. The fields can be read, but must not be changed
 * by the application. An entry stays where it is until the index is cleared
 * or freed.
 */
typedef struct index_entry_t {
    sof_string_t word;
    sof_vector_t lines;
    size_t count;
    sof_rbtree_node_t *node;
} index_entry_t;

/**
 * @brief Called for each word visited by index_traverse().
 * @details Return 0 to continue, or a non-zero value to stop the traversal
 * early. It must not call index_add(), index_flush(), index_clear() or
 * index_free(). ctx is the ctx given to index_traverse().
 */
typedef int (*index_traverse_func_t)(const index_entry_t *entry, void *ctx);

/**
 * @brief What index_init() is to index.
 * @details A zeroed structure indexes every word, keeping its case.
 */
typedef struct index_options_t {
    /// @brief The fewest characters (code points, an apostrophe included) a
    /// word must have to be indexed; 0 is the same as 1.
    size_t min_length;
    /// @brief Non-zero to fold ASCII letters to lowercase, so "The" and "the"
    /// are one word. Letters outside ASCII aren't folded.
    int fold_case;
} index_options_t;

/**
 * @brief A concordance.
 * @details pool holds one copy of each word, and words is a tree of the
 * words, from index_entry_t.word to the entry; its count is the number of
 * different words. lines is the number of
 * lines read, so the line in progress is lines + 1, and total the number of
 * words indexed, counting each time a word was found. line holds the line in
 * progress, and alloc a copy of the
 * allocator (zeroed, meaning the standard functions, if it was given none).
 * These fields can be read, but should not be changed by the application.
 *
 * index_init() must be called before using an index, and index_free()
 * should be called when it is no longer needed. index_add() indexes text,
 * index_flush() ends it, and index_find(), index_first(), index_next() and
 * index_traverse() read the words. The tree points into the index_t, so it
 * must not be moved or copied while it holds words.
 *
 * Every function accepts a NULL index, and an index that was freed or whose
 * index_init() failed (words.root is NULL), as described for each.
 *
 * The index does no locking. index_entry_line(), index_find(),
 * index_first(), index_next() and index_traverse() only read it; every other
 * function writes it.
 */
typedef struct index_t {
    sof_intern_t pool;
    sof_rbtree_t words;
    size_t min_length;
    int fold_case;
    size_t lines;
    size_t total;
    sof_string_t line;
    sof_alloc_t alloc;
} index_t;

/**
 * @brief Indexes a piece of text.
 * @details Text is cut into lines at each '\n', and a line can span pieces:
 * the bytes after the last '\n' of a piece are kept until the next piece, or
 * index_flush(), ends their line. A '\r' before the '\n' separates words like
 * any other character that isn't a letter, so CRLF text needs nothing
 * special. Words are found in a line once it is ended.
 * @param ix The index.
 * @param text The text, which need not be NUL-terminated. NULL is taken as
 * empty.
 * @param length The number of bytes of text.
 * @return INDEX_OK, INDEX_NO_MEM if memory ran out, leaving the words before
 * the one that couldn't be stored indexed and the rest of the text unread,
 * or INDEX_ERROR if ix is NULL or not initialized.
 */
extern index_error_t index_add(index_t *ix, const char *text, size_t length);

/**
 * @brief Forgets every word and line, keeping the options.
 * @param ix The index.
 * @return INDEX_OK, or INDEX_ERROR if ix is NULL or not initialized.
 */
extern index_error_t index_clear(index_t *ix);

/**
 * @brief Returns a line number of a word.
 * @param entry The word.
 * @param i Which of its lines, from 0 to entry->lines.count - 1.
 * @return The number of the line, counting from 1, or 0 if i is out of range
 * or entry is NULL.
 */
extern size_t index_entry_line(const index_entry_t *entry, size_t i);

/**
 * @brief Finds a word.
 * @details If fold_case is set, its ASCII letters are matched in either
 * case, as sof_string_casecmp() compares them, so "The" finds "the".
 * Nothing is allocated.
 * @param ix The index.
 * @param word The word, NUL-terminated.
 * @return The word's entry, good until the index is cleared or freed, or
 * NULL if it isn't in the index, word is NULL, or ix is NULL or not
 * initialized.
 */
extern const index_entry_t *index_find(const index_t *ix, const char *word);

/**
 * @brief Returns the first word, in byte order, for looping over the words
 * without a callback.
 * @details With index_next():
 *
 *     for (e = index_first(&ix); e != NULL; e = index_next(&ix, e))
 *
 * Don't call index_add(), index_flush() or index_clear() until the loop
 * ends.
 * @param ix The index.
 * @return The first word's entry, or NULL if the index is empty, or ix is
 * NULL or not initialized.
 */
extern const index_entry_t *index_first(const index_t *ix);

/**
 * @brief Ends the line in progress.
 * @details The bytes given to index_add() after its last '\n', if there are
 * any, are indexed as a line, so a text whose last line has no '\n' loses
 * nothing. The next text starts a new line. A program reading several texts
 * into one index calls it after each.
 * @param ix The index.
 * @return INDEX_OK, INDEX_NO_MEM, or INDEX_ERROR if ix is NULL or not
 * initialized.
 */
extern index_error_t index_flush(index_t *ix);

/**
 * @brief Frees everything the index allocated.
 * @details The index_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like an index whose index_init() failed until it is
 * initialized again.
 * @param ix The index.
 * @return INDEX_OK, or INDEX_ERROR if ix is NULL.
 */
extern index_error_t index_free(index_t *ix);

/**
 * @brief Initializes an index in an index_t the caller provides.
 * @param ix The index to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap, but must not move while it holds words.
 * @param options What to index, or NULL for the same as a zeroed
 * index_options_t. The index keeps what it needs, so they can be on the
 * stack.
 * @param alloc The memory functions the index uses, for its words and its
 * lines, or NULL for the standard functions. The index keeps a copy, so
 * it can be on the stack.
 * @return INDEX_OK, INDEX_NO_MEM, or INDEX_ERROR if ix is NULL or alloc has a
 * malloc but no free; on an error, ix is left empty.
 */
extern index_error_t index_init(index_t *ix, const index_options_t *options, const sof_alloc_t *alloc);

/**
 * @brief Returns the word after entry, in byte order.
 * @param ix The index.
 * @param entry A word of ix, from index_first(), index_next() or
 * index_find().
 * @return The next word's entry, or NULL if entry is the last word or NULL,
 * or ix is NULL or not initialized.
 */
extern const index_entry_t *index_next(const index_t *ix, const index_entry_t *entry);

/**
 * @brief Calls cb for each word, in byte order.
 * @details If cb returns a non-zero value, the traversal stops and returns
 * that value.
 * @param ix The index.
 * @param cb The function called for each word. NULL visits nothing.
 * @param ctx Passed to every call of cb.
 * @return 0 if every word was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if ix is NULL or not initialized.
 */
extern int index_traverse(const index_t *ix, index_traverse_func_t cb, void *ctx);

#endif // INDEX_H
