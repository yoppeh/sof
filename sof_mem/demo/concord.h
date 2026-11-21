/**
 * @file concord.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A concordance: every word of a text, in order, with how often it
 * appears and the lines it is on.
 * @details The application provides a concord_t, sets it up with
 * concord_init(), and gives it text with concord_read(), a file at a time,
 * or concord_add_line(), a line at a time. concord_find() looks up a word,
 * and the words can be walked in order through the concord_t's tree.
 * concord_free() gives back everything.
 *
 * Its memory comes from sof_mem allocators, each used for what it is good
 * at: the words' text from an arena, which is never freed until the end;
 * each line, and its words while they are looked up, from a second arena,
 * reset to a mark when the line is done; and the tree's nodes, the words'
 * records and the lists of lines from pools, one per size.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#ifndef CONCORD_H
#define CONCORD_H

#include <stddef.h>
#include <stdio.h>

#include "sof_alloc.h"
#include "sof_mem.h"
#include "sof_rbtree.h"

/**
 * @brief Error codes for the concord functions.
 */
typedef enum concord_error_t {
    CONCORD_OK = 0,
    CONCORD_NO_MEM,
    CONCORD_NO_MATCH,
    CONCORD_ERROR,
} concord_error_t;

/**
 * @brief A line a word is on, in a list of them in the order they were read.
 */
typedef struct concord_place_t {
    /// @brief The line's number, counting from 1.
    size_t line;
    /// @brief The next line the word is on, or NULL.
    struct concord_place_t *next;
} concord_place_t;

/**
 * @brief What is known of a word: the data of its node in the tree.
 * @details The application can read it but must not change it.
 */
typedef struct concord_word_t {
    /// @brief The number of times the word appears.
    size_t count;
    /// @brief The number of lines it is on: the length of the list.
    size_t lines;
    /// @brief The first line it is on.
    concord_place_t *first;
    /// @brief The last line it is on, where the next is added.
    concord_place_t *last;
} concord_word_t;

/**
 * @brief A concordance.
 * @details The application provides the concord_t (on the stack, in another
 * structure or on the heap) and sets it up with concord_init(). It must not
 * move while it is in use, since the tree's allocator holds the address of
 * its pool, and the tree's nodes that of the tree. The fields can be read,
 * but must not be changed by the application.
 *
 * Each node of tree has a word, lower case and NUL-terminated, as its key,
 * and its concord_word_t as its data, and the tree orders them by their
 * bytes, so sof_rbtree_minimum() and sof_rbtree_next() walk the words in
 * order.
 *
 * Every function accepts a NULL concord_t, and one that was freed or whose
 * init failed (ready is 0), as described for each.
 */
typedef struct concord_t {
    /// @brief The words, in order.
    sof_rbtree_t tree;
    /// @brief The words' text, the keys of the tree.
    sof_mem_arena_t strings;
    /// @brief Each line being read, and its words.
    sof_mem_arena_t scratch;
    /// @brief The tree's nodes, through sof_mem_pool_allocator().
    sof_mem_pool_t nodes;
    /// @brief The concord_word_t of each word.
    sof_mem_pool_t words;
    /// @brief The concord_place_t of each line of each word.
    sof_mem_pool_t places;
    /// @brief The number of words read, counting each time.
    size_t total;
    /// @brief The number of lines read.
    size_t lines;
    /// @brief The parent allocator every allocator gets its memory from.
    sof_alloc_t alloc;
    /// @brief 1 once concord_init() has succeeded, 0 when freed or failed.
    int ready;
} concord_t;

/**
 * @brief Add one line of text, numbered one more than the last.
 * @details A word is a run of ASCII letters, with an apostrophe between two
 * letters (', or the UTF-8 right single quotation mark, U+2019) kept as an
 * ASCII '. Every other byte separates words. Words are kept in lower case.
 * The line's words are copied into the scratch arena while they are looked
 * up, and taken back with a mark when the line is done.
 * @param c The concordance.
 * @param line The line's bytes, which needn't end with a NUL.
 * @param length How many there are.
 * @return CONCORD_OK, CONCORD_NO_MEM if memory ran out, when the words
 * before the one that failed are added, or CONCORD_ERROR if line is NULL
 * and length isn't 0.
 */
extern concord_error_t concord_add_line(concord_t *c, const char *line, size_t length);

/**
 * @brief Find a word.
 * @param c The concordance.
 * @param word The word, NUL-terminated, in lower case and with any
 * apostrophe an ASCII ', as concord_add_line() keeps words.
 * @return The word's record, or NULL if it isn't there or word is NULL.
 */
extern const concord_word_t *concord_find(const concord_t *c, const char *word);

/**
 * @brief Give back all the concordance's memory.
 * @details The concord_t itself isn't freed; it is left empty, and acts like
 * one whose concord_init() failed until it is initialized again.
 * @param c The concordance.
 * @return CONCORD_OK, or CONCORD_ERROR if c is NULL or not initialized.
 */
extern concord_error_t concord_free(concord_t *c);

/**
 * @brief Initialize an empty concordance in a concord_t the caller provides.
 * @details Nothing is allocated until the first word.
 * @param c The concordance to initialize.
 * @param alloc The parent allocator the arenas and pools get their memory
 * from, or NULL for the standard functions. It is copied, so it can be on
 * the stack.
 * @return CONCORD_OK, or CONCORD_ERROR if c is NULL or alloc has a malloc
 * but no free; on failure c is left empty and not initialized.
 */
extern concord_error_t concord_init(concord_t *c, const sof_alloc_t *alloc);

/**
 * @brief Add every line of a file, read to its end.
 * @details Lines end with a newline, and a carriage return before it, which
 * aren't part of them; the last needn't end with one. Each line is read into
 * the scratch arena, which is reset to a mark after it, so the arena holds
 * only the longest line and its words.
 * @param c The concordance.
 * @param f The file, open for reading.
 * @return CONCORD_OK, CONCORD_NO_MEM if memory ran out, or CONCORD_ERROR if
 * f is NULL or couldn't be read; on an error, the lines before it are added.
 */
extern concord_error_t concord_read(concord_t *c, FILE *f);

#endif // CONCORD_H
