/**
 * @file concord.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A concordance: every word of a text, in order, with how often it
 * appears and the lines it is on.
 * @details See concord.h.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <limits.h>
#include <string.h>

#include "concord.h"

/// @brief The room concord_read() first makes for a line; it doubles as
/// longer lines need.
#define LINE_SIZE 256

static concord_error_t add_word(concord_t *c, const char *word, size_t line);
static int compare_words(const void *a, const void *b, void *ctx);
static int is_letter(unsigned char ch);
static size_t quote_length(const char *p, size_t left);
static size_t split(const char *line, size_t length, char *words);

/// @brief Counts one appearance of word, on line, adding the word if it is
/// new and the line to its list if it isn't there yet.
/// @return CONCORD_OK, or CONCORD_NO_MEM if memory ran out, which leaves the
/// concordance as it was.
static concord_error_t add_word(concord_t *c, const char *word, size_t line) {
    sof_rbtree_node_t *node = sof_rbtree_find(&c->tree, word);
    concord_word_t *record;
    if (node != NULL) {
        record = node->data;
    } else {
        // The text lasts as long as the concordance, so it goes in the
        // arena, which needs no size to free it and wastes nothing.
        size_t size = strlen(word) + 1;
        char *key = sof_mem_arena_alloc(&c->strings, size);
        record = sof_mem_pool_alloc(&c->words);
        if (key != NULL && record != NULL) {
            memcpy(key, word, size);
            node = sof_rbtree_insert(&c->tree, key, NULL);
        }
        if (node == NULL) {
            sof_mem_pool_release(&c->words, record);
            // key is the arena's most recent allocation, so it is taken back.
            sof_mem_arena_release(&c->strings, key, size);
            return CONCORD_NO_MEM;
        }
        memset(record, 0, sizeof(*record));
        node->data = record;
    }
    if (record->last == NULL || record->last->line != line) {
        concord_place_t *place = sof_mem_pool_alloc(&c->places);
        if (place == NULL) {
            return CONCORD_NO_MEM;
        }
        place->line = line;
        place->next = NULL;
        if (record->last == NULL) {
            record->first = place;
        } else {
            record->last->next = place;
        }
        record->last = place;
        record->lines++;
    }
    record->count++;
    c->total++;
    return CONCORD_OK;
}

/// @brief Orders words by their bytes, for the tree.
static int compare_words(const void *a, const void *b, void *ctx) {
    (void)ctx;
    return strcmp(a, b);
}

/// @brief Returns 1 if ch is an ASCII letter.
static int is_letter(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

/// @brief Returns the length of the apostrophe at p, with left bytes from p
/// to the end of the line, if it is one followed by a letter: 1 for ', 3 for
/// U+2019 in UTF-8. Otherwise 0.
static size_t quote_length(const char *p, size_t left) {
    const unsigned char *u = (const unsigned char *)p;
    if (left >= 2 && u[0] == '\'' && is_letter(u[1])) {
        return 1;
    }
    if (left >= 4 && u[0] == 0xe2 && u[1] == 0x80 && u[2] == 0x99 && is_letter(u[3])) {
        return 3;
    }
    return 0;
}

/// @brief Copies the words of line into words, each lower case and followed
/// by a NUL, with any apostrophe as an ASCII '.
/// @details Each word takes no more room than it had in the line, and the NUL
/// after it the place of the byte that ended it, or one more for the last;
/// so words needs room for length + 1 bytes.
/// @return The number of bytes put in words.
static size_t split(const char *line, size_t length, char *words) {
    size_t used = 0, i = 0;
    while (i < length) {
        if (!is_letter((unsigned char)line[i])) {
            i++;
            continue;
        }
        while (i < length) {
            unsigned char ch = (unsigned char)line[i];
            size_t quote;
            if (is_letter(ch)) {
                words[used++] = (char)(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
                i++;
            } else if ((quote = quote_length(line + i, length - i)) > 0) {
                words[used++] = '\'';
                i += quote;
            } else {
                break;
            }
        }
        words[used++] = '\0';
    }
    return used;
}

concord_error_t concord_add_line(concord_t *c, const char *line, size_t length) {
    if (c == NULL || !c->ready || (line == NULL && length != 0)) {
        return CONCORD_ERROR;
    }
    c->lines++;
    // The words are needed only while they are looked up; everything this
    // line puts in the scratch arena goes back to the mark at the end, and
    // whatever the caller had there before stays.
    sof_mem_arena_mark_t mark = sof_mem_arena_mark(&c->scratch);
    char *words = sof_mem_arena_alloc(&c->scratch, length + 1);
    if (words == NULL) {
        return CONCORD_NO_MEM;
    }
    size_t used = split(line, length, words);
    concord_error_t rc = CONCORD_OK;
    for (size_t i = 0; i < used && rc == CONCORD_OK; i += strlen(words + i) + 1) {
        rc = add_word(c, words + i, c->lines);
    }
    sof_mem_arena_reset(&c->scratch, mark);
    return rc;
}

const concord_word_t *concord_find(const concord_t *c, const char *word) {
    if (c == NULL || !c->ready || word == NULL) {
        return NULL;
    }
    sof_rbtree_node_t *node = sof_rbtree_find(&c->tree, word);
    return node == NULL ? NULL : node->data;
}

concord_error_t concord_free(concord_t *c) {
    if (c == NULL || !c->ready) {
        return CONCORD_ERROR;
    }
    // The tree gives its nodes back to their pool before the pool goes.
    sof_rbtree_free(&c->tree);
    sof_mem_pool_free(&c->nodes);
    sof_mem_pool_free(&c->words);
    sof_mem_pool_free(&c->places);
    sof_mem_arena_free(&c->strings);
    sof_mem_arena_free(&c->scratch);
    memset(c, 0, sizeof(*c));
    return CONCORD_OK;
}

concord_error_t concord_init(concord_t *c, const sof_alloc_t *alloc) {
    if (c == NULL) {
        return CONCORD_ERROR;
    }
    memset(c, 0, sizeof(*c));
    if (!sof_alloc_valid(alloc)) {
        return CONCORD_ERROR;
    }
    if (alloc != NULL) {
        c->alloc = *alloc;
    }
    // None of these allocates, and with a valid parent none can fail.
    sof_mem_arena_init(&c->strings, 0, &c->alloc);
    sof_mem_arena_init(&c->scratch, 0, &c->alloc);
    sof_mem_pool_init(&c->nodes, sizeof(sof_rbtree_node_t), 0, &c->alloc);
    sof_mem_pool_init(&c->words, sizeof(concord_word_t), 0, &c->alloc);
    sof_mem_pool_init(&c->places, sizeof(concord_place_t), 0, &c->alloc);
    // The tree keeps a copy of this, which holds the pool's address.
    sof_alloc_t node_alloc = sof_mem_pool_allocator(&c->nodes);
    sof_rbtree_init(&c->tree, compare_words, NULL, NULL, &node_alloc);
    c->ready = 1;
    return CONCORD_OK;
}

concord_error_t concord_read(concord_t *c, FILE *f) {
    if (c == NULL || !c->ready || f == NULL) {
        return CONCORD_ERROR;
    }
    // Each line goes back to this mark when it is done, so the line read
    // next starts in the same place, and the arena asks its parent for
    // nothing more once it has held the longest line.
    sof_mem_arena_mark_t mark = sof_mem_arena_mark(&c->scratch);
    size_t size = LINE_SIZE;
    concord_error_t rc = CONCORD_OK;
    while (rc == CONCORD_OK) {
        sof_mem_arena_reset(&c->scratch, mark);
        char *line = sof_mem_arena_alloc(&c->scratch, size);
        if (line == NULL) {
            rc = CONCORD_NO_MEM;
            break;
        }
        size_t used = 0;
        while (1) {
            if (size - used < 2) {
                // The line is the arena's most recent allocation, so it
                // grows in place while its block has room.
                char *grown = sof_mem_arena_realloc(&c->scratch, line, size, 2 * size);
                if (grown == NULL) {
                    rc = CONCORD_NO_MEM;
                    break;
                }
                line = grown;
                size *= 2;
            }
            size_t room = size - used;
            if (room > INT_MAX) {
                room = INT_MAX;
            }
            if (fgets(line + used, (int)room, f) == NULL) {
                break;
            }
            used += strlen(line + used);
            if (used > 0 && line[used - 1] == '\n') {
                break;
            }
        }
        if (rc != CONCORD_OK || used == 0) {
            break;
        }
        if (line[used - 1] == '\n') {
            used--;
        }
        if (used > 0 && line[used - 1] == '\r') {
            used--;
        }
        rc = concord_add_line(c, line, used);
    }
    sof_mem_arena_reset(&c->scratch, mark);
    if (rc == CONCORD_OK && ferror(f)) {
        rc = CONCORD_ERROR;
    }
    return rc;
}
