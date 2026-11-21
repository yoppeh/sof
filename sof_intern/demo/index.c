/**
 * @file index.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A concordance in C, for the index program.
 * @details Text is gathered into a line in a sof_string_t until a '\n' ends
 * it; then the words of the line are found by next_word() and each
 * is interned in a sof_intern pool, which keeps one copy of it in large
 * blocks rather than a block per word. The interned copy is then looked up in
 * a sof_rbtree, which holds an index_entry_t for each word; a new word to the
 * pool needs no lookup, and the compare function takes two keys with the same
 * data pointer as equal without comparing their bytes, which is how every
 * search for a word already indexed ends. An index that folds case
 * keeps its words in lowercase and compares keys with sof_string_casecmp(),
 * which orders lowercase words as sof_string_cmp() does, so index_find()
 * can look up a word in any case without folding a copy of it.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <string.h>

#include "sof_utf.h"
#include "index.h"

/// @brief What index_traverse() gives visit().
typedef struct visit_t {
    index_traverse_func_t cb;
    void *ctx;
} visit_t;

static index_error_t add_line(index_t *ix);
static index_error_t add_word(index_t *ix, const char *text, size_t length);
static size_t apostrophe_length(const char *text, size_t length);
static int compare(const void *a, const void *b, void *ctx);
static int compare_folded(const void *a, const void *b, void *ctx);
static void delete_node(sof_rbtree_node_t *node, void *ctx);
static void free_entry(index_t *ix, index_entry_t *entry);
static int is_dead(const index_t *ix);
static size_t letter_length(const char *text, size_t length);
static index_entry_t *new_entry(index_t *ix, const sof_string_t *word);
static int next_word(const sof_string_t *line, size_t *position, size_t *offset, size_t *length);
static int visit(sof_rbtree_node_t *node, void *ctx);

/// @brief Indexes the words of the line in progress as line ix->lines + 1,
/// then starts the next line, whether or not that worked.
static index_error_t add_line(index_t *ix) {
    if (ix->fold_case) {
        sof_string_tolower(&ix->line);
    }
    size_t position = 0, offset, length;
    index_error_t err = INDEX_OK;
    while (err == INDEX_OK && next_word(&ix->line, &position, &offset, &length)) {
        err = add_word(ix, ix->line.data + offset, length);
    }
    sof_string_clear(&ix->line);
    ix->lines++;
    return err;
}

/// @brief Indexes one word on the line in progress, if it is long enough.
static index_error_t add_word(index_t *ix, const char *text, size_t length) {
    if (sof_utf8_count(text, length) < ix->min_length) {
        return INDEX_OK;
    }
    int created;
    const char *word = sof_intern_string(&ix->pool, text, length, &created);
    if (word == NULL) {
        return INDEX_NO_MEM;
    }
    // A word new to the pool has no entry yet. One already there usually has,
    // unless making its entry ran out of memory, so it is looked up.
    sof_string_t key = sof_string_view(word, length);
    sof_rbtree_node_t *node = created ? NULL : sof_rbtree_find(&ix->words, &key);
    index_entry_t *entry;
    size_t line = ix->lines + 1;
    if (node == NULL) {
        entry = new_entry(ix, &key);
        if (entry == NULL) {
            return INDEX_NO_MEM;
        }
        node = sof_rbtree_insert(&ix->words, &entry->word, NULL);
        if (node == NULL) {
            free_entry(ix, entry);
            return INDEX_NO_MEM;
        }
        node->data = entry;
        entry->node = node;
        // new_entry() made room for one line, so this can't fail.
        sof_vector_push(&entry->lines, &line);
    } else {
        entry = node->data;
        const size_t *last = sof_vector_at(&entry->lines, entry->lines.count - 1);
        if (*last != line && sof_vector_push(&entry->lines, &line) != SOF_VECTOR_OK) {
            return INDEX_NO_MEM;
        }
    }
    entry->count++;
    ix->total++;
    return INDEX_OK;
}

/// @brief Returns the length of the apostrophe (' or ’, U+2019) at the start
/// of the length bytes at text, or 0 if they don't start with one.
static size_t apostrophe_length(const char *text, size_t length) {
    if (length >= 1 && text[0] == '\'') {
        return 1;
    }
    if (length >= 3 && memcmp(text, "\xe2\x80\x99", 3) == 0) {
        return 3;
    }
    return 0;
}

/// @brief The tree's key compare function: keys are sof_string_t. Two
/// views of the same interned word are equal without comparing their bytes.
static int compare(const void *a, const void *b, void *ctx) {
    (void)ctx;
    const sof_string_t *x = a, *y = b;
    return x->data == y->data ? 0 : sof_string_cmp(x, y);
}

/// @brief The tree's key compare function when the index folds case. The
/// words stored are all lowercase, so they are in the same order as with
/// compare(), and a key in any case finds the word it folds to.
static int compare_folded(const void *a, const void *b, void *ctx) {
    (void)ctx;
    const sof_string_t *x = a, *y = b;
    return x->data == y->data ? 0 : sof_string_casecmp(x, y);
}

/// @brief The tree's delete function: frees the node's entry.
static void delete_node(sof_rbtree_node_t *node, void *ctx) {
    free_entry(ctx, node->data);
}

/// @brief Frees an entry and its lines. Its word belongs to the pool.
static void free_entry(index_t *ix, index_entry_t *entry) {
    sof_vector_free(&entry->lines);
    sof_alloc_free(&ix->alloc, entry, sizeof(index_entry_t));
}

/// @brief Returns 1 if ix is NULL, freed or not initialized.
static int is_dead(const index_t *ix) {
    return ix == NULL || ix->words.root == NULL;
}

/// @brief Returns the length of the letter at the start of the length bytes
/// at text, or 0 if they don't start with one. A letter is ASCII, or a
/// two-byte UTF-8 letter from U+00C0 to U+024F, leaving out U+00D7 (×) and
/// U+00F7 (÷).
static size_t letter_length(const char *text, size_t length) {
    if (length == 0) {
        return 0;
    }
    unsigned char c = (unsigned char)text[0];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        return 1;
    }
    if (length < 2 || c < 0xc3 || c > 0xc9) {
        return 0;
    }
    unsigned char d = (unsigned char)text[1];
    if (d < 0x80 || d > 0xbf || (c == 0xc3 && (d == 0x97 || d == 0xb7))) {
        return 0;
    }
    return 2;
}

/// @brief Allocates an entry for word, a view of an interned word, with room
/// for one line.
/// @return The entry, or NULL if memory ran out.
static index_entry_t *new_entry(index_t *ix, const sof_string_t *word) {
    index_entry_t *entry = sof_alloc_calloc(&ix->alloc, 1, sizeof(index_entry_t));
    if (entry == NULL) {
        return NULL;
    }
    entry->word = *word;
    if (sof_vector_init(&entry->lines, sizeof(size_t), 1, NULL, NULL, &ix->alloc) != SOF_VECTOR_OK) {
        sof_alloc_free(&ix->alloc, entry, sizeof(index_entry_t));
        return NULL;
    }
    return entry;
}

/// @brief Finds the next word in line at or after *position, and moves
/// *position past it. A word is letters, with an apostrophe between two of
/// them; an apostrophe not followed by a letter ends it.
/// @return 1, with *offset and *length set to the word, or 0 if there are no
/// more words.
static int next_word(const sof_string_t *line, size_t *position, size_t *offset, size_t *length) {
    const char *text = line->data;
    size_t end = line->length, i = *position, n;
    while (i < end && letter_length(text + i, end - i) == 0) {
        i++;
    }
    if (i == end) {
        *position = i;
        return 0;
    }
    *offset = i;
    while (1) {
        while ((n = letter_length(text + i, end - i)) != 0) {
            i += n;
        }
        n = apostrophe_length(text + i, end - i);
        if (n == 0 || letter_length(text + i + n, end - i - n) == 0) {
            break;
        }
        i += n;
    }
    *length = i - *offset;
    *position = i;
    return 1;
}

/// @brief The tree traversal's callback, which calls index_traverse()'s.
static int visit(sof_rbtree_node_t *node, void *ctx) {
    visit_t *v = ctx;
    return v->cb(node->data, v->ctx);
}

index_error_t index_add(index_t *ix, const char *text, size_t length) {
    if (is_dead(ix)) {
        return INDEX_ERROR;
    }
    if (text == NULL) {
        return INDEX_OK;
    }
    while (length > 0) {
        const char *newline = memchr(text, '\n', length);
        size_t n = newline != NULL ? (size_t)(newline - text) : length;
        if (sof_string_append_bytes(&ix->line, text, n) != SOF_STRING_OK) {
            return INDEX_NO_MEM;
        }
        if (newline == NULL) {
            break;
        }
        index_error_t err = add_line(ix);
        if (err != INDEX_OK) {
            return err;
        }
        text += n + 1;
        length -= n + 1;
    }
    return INDEX_OK;
}

index_error_t index_clear(index_t *ix) {
    if (is_dead(ix)) {
        return INDEX_ERROR;
    }
    sof_rbtree_clear(&ix->words, NULL);
    sof_intern_clear(&ix->pool);
    sof_string_clear(&ix->line);
    ix->lines = 0;
    ix->total = 0;
    return INDEX_OK;
}

size_t index_entry_line(const index_entry_t *entry, size_t i) {
    if (entry == NULL || i >= entry->lines.count) {
        return 0;
    }
    const size_t *line = sof_vector_at(&entry->lines, i);
    return *line;
}

const index_entry_t *index_find(const index_t *ix, const char *word) {
    if (is_dead(ix) || word == NULL) {
        return NULL;
    }
    // An index that folds case compares keys with sof_string_casecmp(),
    // so the word needn't be folded first.
    sof_string_t key = sof_string_view(word, strlen(word));
    const sof_rbtree_node_t *node = sof_rbtree_find(&ix->words, &key);
    return node != NULL ? node->data : NULL;
}

const index_entry_t *index_first(const index_t *ix) {
    if (is_dead(ix)) {
        return NULL;
    }
    const sof_rbtree_node_t *node = sof_rbtree_minimum(&ix->words, NULL);
    return node != NULL ? node->data : NULL;
}

index_error_t index_flush(index_t *ix) {
    if (is_dead(ix)) {
        return INDEX_ERROR;
    }
    return ix->line.length > 0 ? add_line(ix) : INDEX_OK;
}

index_error_t index_free(index_t *ix) {
    if (ix == NULL) {
        return INDEX_ERROR;
    }
    if (!is_dead(ix)) {
        sof_rbtree_free(&ix->words);
        sof_intern_free(&ix->pool);
        sof_string_free(&ix->line);
    }
    memset(ix, 0, sizeof(index_t));
    return INDEX_OK;
}

index_error_t index_init(index_t *ix, const index_options_t *options, const sof_alloc_t *alloc) {
    if (ix == NULL) {
        return INDEX_ERROR;
    }
    memset(ix, 0, sizeof(index_t));
    if (!sof_alloc_valid(alloc)) {
        return INDEX_ERROR;
    }
    if (alloc != NULL) {
        ix->alloc = *alloc;
    }
    if (options != NULL) {
        ix->min_length = options->min_length;
        ix->fold_case = options->fold_case != 0;
    }
    if (sof_string_init(&ix->line, 0, &ix->alloc) != SOF_STRING_OK) {
        memset(ix, 0, sizeof(index_t));
        return INDEX_NO_MEM;
    }
    if (sof_intern_init(&ix->pool, 0, &ix->alloc) != SOF_INTERN_OK) {
        sof_string_free(&ix->line);
        memset(ix, 0, sizeof(index_t));
        return INDEX_NO_MEM;
    }
    // Last, as it can't fail: a tree with a root marks the index as ready.
    sof_rbtree_init(&ix->words, ix->fold_case ? compare_folded : compare, delete_node, ix, &ix->alloc);
    return INDEX_OK;
}

const index_entry_t *index_next(const index_t *ix, const index_entry_t *entry) {
    if (is_dead(ix) || entry == NULL) {
        return NULL;
    }
    const sof_rbtree_node_t *node = sof_rbtree_next(&ix->words, entry->node);
    return node != NULL ? node->data : NULL;
}

int index_traverse(const index_t *ix, index_traverse_func_t cb, void *ctx) {
    if (is_dead(ix)) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    visit_t v = {cb, ctx};
    return sof_rbtree_traverse_ascending(&ix->words, NULL, visit, &v);
}
