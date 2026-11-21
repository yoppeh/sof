/**
 * @file top.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Counting words, lines or n-grams of words, and picking the most
 * frequent, in C, for the top program.
 * @details See top.h. Counts live in a sof_hashmap_t with string keys, the
 * count held in each element's data pointer. Lines are split into words by a
 * sof_regex_t, and the last n words are kept, joined, in a
 * sof_string_t. top_select() keeps the best n keys in a sof_heap_t.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_heap.h"
#include "top.h"

static top_error_t count_key(top_t *t, const char *key, size_t count);
static top_error_t push_word(top_t *t, size_t offset, size_t length);
static int worst_first(const void *a, const void *b, void *ctx);

/// @brief Adds count to a key's count, inserting the key if it is new.
static top_error_t count_key(top_t *t, const char *key, size_t count) {
    sof_hashmap_element_t *e = sof_hashmap_insert(&t->map, key, NULL);
    if (e == NULL) {
        return TOP_NO_MEM;
    }
    e->data = (void *)((uintptr_t)e->data + count);
    t->total += count;
    return TOP_OK;
}

/// @brief Adds the word at offset in t->line, length bytes, to the words in
/// progress, dropping the oldest if there are n already, and counts the
/// n-gram they make once there are n.
/// @return TOP_OK, or TOP_NO_MEM, after forgetting the words in progress.
static top_error_t push_word(top_t *t, size_t offset, size_t length) {
    if (t->words == t->n) {
        // The oldest word, and the space after it if another follows.
        size_t drop = t->lengths[t->oldest] + (t->words > 1 ? 1 : 0);
        sof_string_delete(&t->gram, 0, drop);
        t->oldest = (t->oldest + 1) % t->n;
        t->words--;
    }
    sof_string_error_t err = SOF_STRING_OK;
    if (t->words > 0) {
        err = sof_string_append_cstr(&t->gram, " ");
    }
    if (err == SOF_STRING_OK) {
        err = sof_string_append_bytes(&t->gram, t->line.data + offset, length);
    }
    if (err != SOF_STRING_OK) {
        top_flush(t);
        return TOP_NO_MEM;
    }
    t->lengths[(t->oldest + t->words) % t->n] = length;
    t->words++;
    if (t->words < t->n) {
        return TOP_OK;
    }
    top_error_t rc = count_key(t, t->gram.data, 1);
    if (rc != TOP_OK) {
        top_flush(t);
    }
    return rc;
}

/// @brief Orders entries worst first, the reverse of top_entry_compare(),
/// so the heap's smallest is the worst of the best kept so far.
static int worst_first(const void *a, const void *b, void *ctx) {
    return top_entry_compare(b, a, ctx);
}

top_error_t top_add(top_t *t, const char *key, size_t count) {
    if (t == NULL || t->map.elements == NULL || key == NULL) {
        return TOP_ERROR;
    }
    if (count == 0) {
        return TOP_OK;
    }
    return count_key(t, key, count);
}

top_error_t top_add_line(top_t *t, const char *line) {
    if (t == NULL || t->map.elements == NULL || line == NULL) {
        return TOP_ERROR;
    }
    sof_string_clear(&t->line);
    if (sof_string_append_cstr(&t->line, line) != SOF_STRING_OK) {
        top_flush(t);
        return TOP_NO_MEM;
    }
    if (t->fold_case) {
        sof_string_tolower(&t->line);
    }
    if (t->unit == TOP_LINES) {
        return count_key(t, t->line.data, 1);
    }
    size_t position = 0, offset, length;
    sof_regex_error_t err;
    while ((err = sof_regex_find(&t->regex, &t->line, &position, &offset, &length)) == SOF_REGEX_OK) {
        if (length == 0) {
            continue;
        }
        top_error_t rc = push_word(t, offset, length);
        if (rc != TOP_OK) {
            return rc;
        }
    }
    if (err == SOF_REGEX_NO_MATCH) {
        return TOP_OK;
    }
    top_flush(t);
    return err == SOF_REGEX_NO_MEM ? TOP_NO_MEM : TOP_ERROR;
}

top_error_t top_clear(top_t *t) {
    if (t == NULL || t->map.elements == NULL) {
        return TOP_ERROR;
    }
    sof_hashmap_clear(&t->map);
    t->total = 0;
    return top_flush(t);
}

int top_entry_compare(const void *a, const void *b, void *ctx) {
    (void)ctx;
    const top_entry_t *ea = a, *eb = b;
    if (ea->count != eb->count) {
        return ea->count > eb->count ? -1 : 1;
    }
    return strcmp(ea->key, eb->key);
}

size_t top_find(const top_t *t, const char *key) {
    if (t == NULL || key == NULL) {
        return 0;
    }
    const sof_hashmap_element_t *e = sof_hashmap_find(&t->map, key);
    return e == NULL ? 0 : (size_t)(uintptr_t)e->data;
}

top_error_t top_flush(top_t *t) {
    if (t == NULL || t->map.elements == NULL) {
        return TOP_ERROR;
    }
    if (t->unit == TOP_WORDS) {
        sof_string_clear(&t->gram);
    }
    t->words = 0;
    t->oldest = 0;
    return TOP_OK;
}

top_error_t top_free(top_t *t) {
    if (t == NULL) {
        return TOP_ERROR;
    }
    sof_hashmap_free(&t->map);
    sof_regex_free(&t->regex);
    sof_string_free(&t->line);
    sof_string_free(&t->gram);
    if (t->lengths != NULL) {
        sof_alloc_free(&t->alloc, t->lengths, t->n * sizeof(size_t));
    }
    memset(t, 0, sizeof(*t));
    return TOP_OK;
}

top_error_t top_init(top_t *t, const top_options_t *options, const sof_alloc_t *alloc) {
    if (t == NULL) {
        return TOP_ERROR;
    }
    memset(t, 0, sizeof(*t));
    top_options_t defaults = {TOP_WORDS, 1, NULL, 0};
    if (options == NULL) {
        options = &defaults;
    }
    if (!sof_alloc_valid(alloc) || (options->unit != TOP_WORDS && options->unit != TOP_LINES)) {
        return TOP_ERROR;
    }
    size_t n = options->n == 0 ? 1 : options->n;
    if (options->unit == TOP_LINES && n != 1) {
        return TOP_ERROR;
    }
    t->unit = options->unit;
    t->n = n;
    t->fold_case = options->fold_case;
    if (alloc != NULL) {
        t->alloc = *alloc;
    }
    top_error_t rc = TOP_NO_MEM;
    if (sof_hashmap_init(&t->map, &sof_hashmap_string_keys, NULL, NULL, 0, &t->alloc) != SOF_HASHMAP_OK) {
        goto fail;
    }
    if (sof_string_init(&t->line, 0, &t->alloc) != SOF_STRING_OK) {
        goto fail;
    }
    if (t->unit == TOP_WORDS) {
        const char *pattern = options->word == NULL ? TOP_DEFAULT_WORD : options->word;
        sof_regex_error_t err = sof_regex_init(&t->regex, pattern, &t->alloc);
        if (err != SOF_REGEX_OK) {
            rc = err == SOF_REGEX_BAD_PATTERN ? TOP_BAD_REGEX : err == SOF_REGEX_NO_MEM ? TOP_NO_MEM : TOP_ERROR;
            goto fail;
        }
        if (sof_string_init(&t->gram, 0, &t->alloc) != SOF_STRING_OK) {
            goto fail;
        }
        t->lengths = sof_alloc_calloc(&t->alloc, n, sizeof(size_t));
        if (t->lengths == NULL) {
            goto fail;
        }
    }
    return TOP_OK;
fail:
    top_free(t);
    return rc;
}

const char *top_next(const top_t *t, size_t *position, size_t *count) {
    if (t == NULL || position == NULL) {
        return NULL;
    }
    const sof_hashmap_element_t *e = sof_hashmap_next(&t->map, position);
    if (e == NULL) {
        return NULL;
    }
    if (count != NULL) {
        *count = (size_t)(uintptr_t)e->data;
    }
    return e->key;
}

top_error_t top_select(const top_t *t, size_t n, top_entry_t *entries, size_t *found) {
    if (found != NULL) {
        *found = 0;
    }
    if (t == NULL || t->map.elements == NULL || (entries == NULL && n != 0)) {
        return TOP_ERROR;
    }
    size_t limit = n < t->map.count ? n : t->map.count;
    if (limit == 0) {
        return TOP_OK;
    }
    sof_heap_t heap;
    // Room for limit entries now, so no push allocates.
    if (sof_heap_init(&heap, sizeof(top_entry_t), limit, worst_first, NULL, NULL, &t->alloc) != SOF_HEAP_OK) {
        return TOP_NO_MEM;
    }
    size_t position = 0;
    const sof_hashmap_element_t *e;
    while ((e = sof_hashmap_next(&t->map, &position)) != NULL) {
        top_entry_t entry = {e->key, (size_t)(uintptr_t)e->data};
        if (heap.count < limit) {
            sof_heap_push(&heap, &entry);
        } else if (worst_first(&entry, sof_heap_peek(&heap), NULL) > 0) {
            sof_heap_replace(&heap, &entry, NULL);
        }
    }
    // The worst comes out first, so the entries are filled from the back.
    for (size_t i = limit; i > 0; i--) {
        sof_heap_pop(&heap, &entries[i - 1]);
    }
    sof_heap_free(&heap);
    if (found != NULL) {
        *found = limit;
    }
    return TOP_OK;
}
