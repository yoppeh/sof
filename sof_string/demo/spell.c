/**
 * @file spell.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A spelling dictionary with suggestions by edit distance in C, for
 * the spell program.
 * @details The words are the keys of a hashmap. spell_suggest() makes every
 * string one edit from the word in a buffer, looks it up, and makes the
 * edits of that string in the next buffer, down to the distance asked for,
 * recording each word found, with the fewest edits it was found at, in a
 * second hashmap. Nothing but the words found is kept, so the memory it
 * takes doesn't grow with the number of strings tried.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_sort.h"
#include "spell.h"

/// @brief Words up to this long are copied on the stack by spell_load();
/// longer ones are allocated.
#define WORD_BUFFER 256

/// @brief What spell_suggest() keeps while it tries edits.
typedef struct search_t {
    const spell_t *sp;
    /// @brief The words found, keyed by the dictionary's own pointer to
    /// each, so they are told apart by identity. The data is the fewest
    /// edits the word was found at, plus 1.
    sof_hashmap_t seen;
    /// @brief max_distance buffers of width bytes: the strings at one edit
    /// are made in the first, those at two in the second.
    char *buffer;
    size_t width;
    size_t max_distance;
} search_t;

static spell_error_t add_word(spell_t *sp, const char *word, size_t length);
static int by_rank(const void *a, const void *b, void *ctx);
static spell_error_t edits(search_t *s, const char *from, size_t length, size_t distance);
static int is_dead(const spell_t *sp);
static int is_space(char c);
static spell_error_t visit(search_t *s, const char *candidate, size_t length, size_t distance);

/// @brief Adds a word of length bytes, NUL-terminated, that isn't empty.
static spell_error_t add_word(spell_t *sp, const char *word, size_t length) {
    int created;
    sof_hashmap_element_t *e = sof_hashmap_insert(&sp->words, word, &created);
    if (e == NULL) {
        return SPELL_NO_MEM;
    }
    e->data = (void *)((uintptr_t)e->data + 1);
    if (created) {
        for (size_t i = 0; i < length; i++) {
            unsigned char b = (unsigned char)word[i];
            if (!sp->letters[b]) {
                sp->letters[b] = 1;
                sp->alphabet[sp->alphabet_length++] = b;
            }
        }
        if (length > sp->longest) {
            sp->longest = length;
        }
    }
    return SPELL_OK;
}

/// @brief Orders suggestions by distance, then count, most first, then
/// word.
static int by_rank(const void *a, const void *b, void *ctx) {
    const spell_suggestion_t *sa = a, *sb = b;
    (void)ctx;
    if (sa->distance != sb->distance) {
        return sa->distance < sb->distance ? -1 : 1;
    }
    if (sa->count != sb->count) {
        return sa->count > sb->count ? -1 : 1;
    }
    return strcmp(sa->word, sb->word);
}

/// @brief Visits every string one edit from the length bytes of from, made
/// in the buffer for distance edits.
static spell_error_t edits(search_t *s, const char *from, size_t length, size_t distance) {
    char *to = s->buffer + (distance - 1) * s->width;
    const unsigned char *alphabet = s->sp->alphabet;
    size_t letters = s->sp->alphabet_length;
    spell_error_t err = SPELL_OK;
    // Deletes.
    for (size_t i = 0; i < length && err == SPELL_OK; i++) {
        memcpy(to, from, i);
        memcpy(to + i, from + i + 1, length - i - 1);
        to[length - 1] = '\0';
        err = visit(s, to, length - 1, distance);
    }
    // Swaps of neighbours that differ.
    for (size_t i = 0; i + 1 < length && err == SPELL_OK; i++) {
        if (from[i] == from[i + 1]) {
            continue;
        }
        memcpy(to, from, length + 1);
        to[i] = from[i + 1];
        to[i + 1] = from[i];
        err = visit(s, to, length, distance);
    }
    // Replaces.
    for (size_t i = 0; i < length && err == SPELL_OK; i++) {
        memcpy(to, from, length + 1);
        for (size_t c = 0; c < letters && err == SPELL_OK; c++) {
            if ((char)alphabet[c] == from[i]) {
                continue;
            }
            to[i] = (char)alphabet[c];
            err = visit(s, to, length, distance);
        }
    }
    // Inserts.
    for (size_t i = 0; i <= length && err == SPELL_OK; i++) {
        memcpy(to, from, i);
        memcpy(to + i + 1, from + i, length - i + 1);
        for (size_t c = 0; c < letters && err == SPELL_OK; c++) {
            to[i] = (char)alphabet[c];
            err = visit(s, to, length + 1, distance);
        }
    }
    return err;
}

/// @brief Returns 1 if sp was freed or its spell_init() failed.
static int is_dead(const spell_t *sp) {
    return sp->words.elements == NULL;
}

/// @brief Returns 1 if c is ASCII white space, which separates the words
/// spell_load() reads.
static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/// @brief Looks up a string distance edits from the word, records it if it
/// is a word, and goes on to its own edits if more are allowed.
static spell_error_t visit(search_t *s, const char *candidate, size_t length, size_t distance) {
    if (length <= s->sp->longest) {
        const sof_hashmap_element_t *e = sof_hashmap_find(&s->sp->words, candidate);
        if (e != NULL) {
            sof_hashmap_element_t *seen = sof_hashmap_insert(&s->seen, e->key, NULL);
            if (seen == NULL) {
                return SPELL_NO_MEM;
            }
            // A new element's data is NULL, which is more than any distance.
            if (seen->data == NULL || (uintptr_t)seen->data > distance + 1) {
                seen->data = (void *)(uintptr_t)(distance + 1);
            }
        }
    }
    if (distance < s->max_distance) {
        return edits(s, candidate, length, distance + 1);
    }
    return SPELL_OK;
}

spell_error_t spell_add(spell_t *sp, const char *word) {
    if (sp == NULL || word == NULL || word[0] == '\0' || is_dead(sp)) {
        return SPELL_ERROR;
    }
    return add_word(sp, word, strlen(word));
}

spell_error_t spell_clear(spell_t *sp) {
    if (sp == NULL || is_dead(sp)) {
        return SPELL_ERROR;
    }
    sof_hashmap_clear(&sp->words);
    memset(sp->letters, 0, sizeof(sp->letters));
    memset(sp->alphabet, 0, sizeof(sp->alphabet));
    sp->alphabet_length = 0;
    sp->longest = 0;
    return SPELL_OK;
}

const char *spell_find(const spell_t *sp, const char *word, size_t *count) {
    if (count != NULL) {
        *count = 0;
    }
    if (sp == NULL || word == NULL || is_dead(sp)) {
        return NULL;
    }
    const sof_hashmap_element_t *e = sof_hashmap_find(&sp->words, word);
    if (e == NULL) {
        return NULL;
    }
    if (count != NULL) {
        *count = (size_t)(uintptr_t)e->data;
    }
    return e->key;
}

spell_error_t spell_free(spell_t *sp) {
    if (sp == NULL) {
        return SPELL_ERROR;
    }
    sof_hashmap_free(&sp->words);
    memset(sp, 0, sizeof(*sp));
    return SPELL_OK;
}

spell_error_t spell_init(spell_t *sp, const sof_alloc_t *alloc) {
    if (sp == NULL) {
        return SPELL_ERROR;
    }
    memset(sp, 0, sizeof(*sp));
    if (!sof_alloc_valid(alloc)) {
        return SPELL_ERROR;
    }
    if (alloc != NULL) {
        sp->alloc = *alloc;
    }
    sof_hashmap_error_t err = sof_hashmap_init(&sp->words, &sof_hashmap_string_keys, NULL, NULL, 0, &sp->alloc);
    if (err != SOF_HASHMAP_OK) {
        memset(sp, 0, sizeof(*sp));
        return err == SOF_HASHMAP_NO_MEM ? SPELL_NO_MEM : SPELL_ERROR;
    }
    return SPELL_OK;
}

spell_error_t spell_load(spell_t *sp, const char *text, size_t length) {
    if (sp == NULL || is_dead(sp)) {
        return SPELL_ERROR;
    }
    if (text == NULL) {
        return SPELL_OK;
    }
    char buffer[WORD_BUFFER];
    size_t i = 0;
    while (i < length) {
        while (i < length && is_space(text[i])) {
            i++;
        }
        size_t start = i;
        while (i < length && !is_space(text[i])) {
            i++;
        }
        size_t n = i - start;
        if (n == 0) {
            break;
        }
        spell_error_t err;
        if (n < WORD_BUFFER) {
            memcpy(buffer, text + start, n);
            buffer[n] = '\0';
            err = add_word(sp, buffer, n);
        } else {
            char *word = sof_alloc_strndup(&sp->alloc, text + start, n);
            if (word == NULL) {
                return SPELL_NO_MEM;
            }
            err = add_word(sp, word, n);
            sof_alloc_free(&sp->alloc, word, n + 1);
        }
        if (err != SPELL_OK) {
            return err;
        }
    }
    return SPELL_OK;
}

spell_error_t spell_suggest(const spell_t *sp, const char *word, size_t max_distance, spell_suggestion_t *suggestions, size_t max, size_t *found) {
    if (found != NULL) {
        *found = 0;
    }
    if (sp == NULL || word == NULL || is_dead(sp) || max_distance > SPELL_MAX_DISTANCE || (suggestions == NULL && max != 0)) {
        return SPELL_ERROR;
    }
    search_t s = {sp, {0}, NULL, 0, max_distance};
    size_t length = strlen(word);
    // Inserts make the strings at most max_distance bytes longer, and each
    // ends with a NUL.
    s.width = length + max_distance + 1;
    if (max_distance > 0) {
        s.buffer = sof_alloc_malloc(&sp->alloc, max_distance * s.width);
        if (s.buffer == NULL) {
            return SPELL_NO_MEM;
        }
    }
    spell_error_t err = SPELL_OK;
    if (sof_hashmap_init(&s.seen, NULL, NULL, NULL, 0, &sp->alloc) != SOF_HASHMAP_OK) {
        err = SPELL_NO_MEM;
    }
    if (err == SPELL_OK) {
        err = visit(&s, word, length, 0);
    }
    size_t count = s.seen.count;
    spell_suggestion_t *all = NULL;
    if (err == SPELL_OK && count > 0) {
        all = sof_alloc_calloc(&sp->alloc, count, sizeof(spell_suggestion_t));
        if (all == NULL) {
            err = SPELL_NO_MEM;
        }
    }
    if (err == SPELL_OK && count > 0) {
        size_t position = 0, n = 0;
        const sof_hashmap_element_t *e;
        while ((e = sof_hashmap_next(&s.seen, &position)) != NULL) {
            all[n].word = e->key;
            all[n].distance = (size_t)(uintptr_t)e->data - 1;
            spell_find(sp, e->key, &all[n].count);
            n++;
        }
        sof_sort_quick(all, count, sizeof(spell_suggestion_t), by_rank, NULL);
        size_t keep = count < max ? count : max;
        if (keep > 0) {
            memcpy(suggestions, all, keep * sizeof(spell_suggestion_t));
        }
        if (found != NULL) {
            *found = count;
        }
    }
    sof_alloc_free(&sp->alloc, all, count * sizeof(spell_suggestion_t));
    sof_hashmap_free(&s.seen);
    sof_alloc_free(&sp->alloc, s.buffer, max_distance * s.width);
    return err;
}
