/**
 * @file anagram.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Anagram groups in C, for the anagram program.
 * @details An anagram_t gathers words into groups of anagrams: words made of
 * the same characters, in any order, such as "listen" and "silent". Each
 * group has a key, the word's characters sorted, which a hashmap maps to the
 * group. Words are UTF-8, and a character is a lead byte and the
 * continuation bytes after it, so "été" and "tée" are anagrams. ASCII letters
 * can be folded to lowercase first, so "Listen" and "silent" are too.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef ANAGRAM_H
#define ANAGRAM_H

#include <stddef.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"
#include "sof_string.h"

/**
 * @brief Error codes for the anagram functions.
 */
typedef enum anagram_error_t {
    ANAGRAM_OK = 0,
    ANAGRAM_NO_MEM,
    ANAGRAM_NO_MATCH,
    ANAGRAM_ERROR,
} anagram_error_t;

/**
 * @brief A group of words that are anagrams of each other.
 * @details key is the words' characters, sorted (and folded, if the
 * collection folds case), NUL-terminated; it belongs to the collection's
 * hashmap. words holds count copies of the words, each NUL-terminated, in the
 * order they were added until anagram_sort() sorts them, in an array of
 * capacity pointers. No two words in a group are the same. These fields can
 * be read, but should not be changed by the application. A group stays at the
 * same address until anagram_clear() or anagram_free(), but its words array
 * moves when a word is added to it.
 */
typedef struct anagram_group_t {
    const char *key;
    char **words;
    size_t count;
    size_t capacity;
} anagram_group_t;

/**
 * @brief Called for each group visited by anagram_traverse().
 * @details Return 0 to continue, or a non-zero value to stop the traversal
 * early. It must not call anagram_add(), anagram_sort(), anagram_clear() or
 * anagram_free(). ctx is the ctx given to anagram_traverse().
 */
typedef int (*anagram_traverse_func_t)(const anagram_group_t *group, void *ctx);

/**
 * @brief A collection of words, in groups of anagrams.
 * @details map maps each key to its group, held in the element's data.
 * groups holds count pointers to the groups, in an array of capacity, in the
 * order they were made until anagram_sort() sorts them. words is the number
 * of words in all the groups. key is where anagram_add() makes each word's
 * key, and alloc a copy of the allocator (zeroed, meaning the standard
 * functions, if it was given none). These fields can be read, but should not
 * be changed by the application.
 *
 * anagram_init() must be called before using a collection, and
 * anagram_free() should be called when it is no longer needed.
 * anagram_add() adds a word, anagram_find() finds the group a word belongs
 * in, anagram_sort() puts the groups in order, largest first, and
 * anagram_next() and anagram_traverse() visit them in that order.
 *
 * Every function accepts a NULL collection, and a collection that was freed
 * or whose anagram_init() failed (map.elements is NULL), as described for
 * each.
 *
 * The collection does no locking. anagram_find(), anagram_key(),
 * anagram_next() and anagram_traverse() only read it; every other function
 * writes it.
 */
typedef struct anagram_t {
    int fold_case;
    sof_hashmap_t map;
    anagram_group_t **groups;
    size_t count;
    size_t capacity;
    size_t words;
    sof_string_t key;
    sof_alloc_t alloc;
} anagram_t;

/**
 * @brief Adds a word to the group of its anagrams.
 * @details The collection keeps a copy of the word, in a new group if no
 * word added before has the same key. The group's words, and the groups,
 * stay in the order they were added until anagram_sort() is called again.
 * @param an The collection.
 * @param word The word, NUL-terminated, in UTF-8.
 * @return ANAGRAM_OK; ANAGRAM_NO_MATCH if the word is already in its group,
 * which is left as it was; ANAGRAM_NO_MEM, leaving the collection as it was;
 * or ANAGRAM_ERROR if an or word is NULL, an isn't initialized, or word is
 * empty.
 */
extern anagram_error_t anagram_add(anagram_t *an, const char *word);

/**
 * @brief Removes every word and group, keeping the collection's options.
 * @param an The collection.
 * @return ANAGRAM_OK, or ANAGRAM_ERROR if an is NULL or not initialized.
 */
extern anagram_error_t anagram_clear(anagram_t *an);

/**
 * @brief Finds the group of a word's anagrams.
 * @details The word need not be in the collection: the group is the one it
 * would be added to. The group may hold the word itself.
 * @param an The collection.
 * @param word The word, NUL-terminated, in UTF-8.
 * @return The group, or NULL if no word added has the same key, memory
 * couldn't be allocated, through the collection's allocator, to make the
 * word's key, or an or word is NULL or an isn't initialized.
 */
extern const anagram_group_t *anagram_find(const anagram_t *an, const char *word);

/**
 * @brief Frees everything the collection allocated.
 * @details The anagram_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like a collection whose anagram_init() failed until it
 * is initialized again. Group pointers aren't good afterwards.
 * @param an The collection.
 * @return ANAGRAM_OK, or ANAGRAM_ERROR if an is NULL.
 */
extern anagram_error_t anagram_free(anagram_t *an);

/**
 * @brief Initializes a collection in an anagram_t the caller provides.
 * @param an The collection to initialize. It can be anywhere: on the stack,
 * in another structure or on the heap.
 * @param fold_case Non-zero to fold ASCII letters to lowercase in keys, so
 * "Listen" and "silent" are anagrams. Words are kept as they were given.
 * @param alloc The memory functions the collection uses, or NULL for the
 * standard functions. The collection keeps a copy, so it can be on the
 * stack.
 * @return ANAGRAM_OK, ANAGRAM_NO_MEM, or ANAGRAM_ERROR if an is NULL or alloc
 * has a malloc but no free; on an error, an is left empty.
 */
extern anagram_error_t anagram_init(anagram_t *an, int fold_case, const sof_alloc_t *alloc);

/**
 * @brief Makes the key of a word: its characters, sorted.
 * @details The word is folded to lowercase first if the collection folds
 * case. Characters are sorted by their bytes, which for UTF-8 is code point
 * order, so "listen" gives "eilnst". This is how anagram_add() and
 * anagram_find() make keys.
 * @param an The collection, whose fold_case and allocator are used.
 * @param word The word, NUL-terminated, in UTF-8.
 * @param key A string the caller initialized (see sof_string_init()), which
 * is emptied and given the key. It grows with its own allocator.
 * @return ANAGRAM_OK; ANAGRAM_NO_MEM if memory couldn't be allocated, through
 * the collection's allocator for a word longer than 64 characters or the
 * key's for the key, leaving the key with some other content; or
 * ANAGRAM_ERROR if an, word or key is NULL, an isn't initialized, or key is
 * dead or a sof_string_const().
 */
extern anagram_error_t anagram_key(const anagram_t *an, const char *word, sof_string_t *key);

/**
 * @brief Returns the next group, for looping over the groups without a
 * callback.
 * @details Set *position to 0 to start, then call this until it returns
 * NULL. The groups come in the order anagram_sort() put them in, with any
 * made since after them. Don't call anagram_add(), anagram_sort() or
 * anagram_clear() until the loop ends.
 *
 *     size_t position = 0;
 *     const anagram_group_t *group;
 *     while ((group = anagram_next(&an, &position)) != NULL) {
 *         ...
 *     }
 *
 * @param an The collection.
 * @param position Where the loop is; updated on each call.
 * @return The next group, or NULL when there are no more, or an or position
 * is NULL or an isn't initialized.
 */
extern const anagram_group_t *anagram_next(const anagram_t *an, size_t *position);

/**
 * @brief Puts the groups in order, and the words in each group.
 * @details The groups with the most words come first, and groups of the same
 * size are ordered by their first word. The words in each group are put in
 * byte order, which for UTF-8 is code point order. Nothing is allocated.
 * @param an The collection.
 * @return ANAGRAM_OK, or ANAGRAM_ERROR if an is NULL or not initialized.
 */
extern anagram_error_t anagram_sort(anagram_t *an);

/**
 * @brief Calls cb for each group.
 * @details The groups are visited in the order anagram_next() gives them. If
 * cb returns a non-zero value, the traversal stops and returns that value.
 * @param an The collection.
 * @param cb The function called for each group. NULL visits nothing.
 * @param ctx Passed to every call of cb.
 * @return 0 if every group was visited, the callback's non-zero value if it
 * stopped the traversal, or -1 if an is NULL or not initialized.
 */
extern int anagram_traverse(const anagram_t *an, anagram_traverse_func_t cb, void *ctx);

#endif // ANAGRAM_H
