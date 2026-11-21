/**
 * @file anagram.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Anagram groups in C, for the anagram program.
 * @details Each word's key is made in a sof_string_t: the word, folded with
 * sof_string_tolower() if asked, then cut into characters, each packed
 * into a uint32_t with its bytes from the top down so that comparing two of
 * them compares their bytes, sorted with sof_sort_quick() and put back. A
 * sof_hashmap with string keys maps each key to its group.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "anagram.h"
#include "sof_sort.h"

/// @brief The most characters a key is made of without allocating.
#define LOCAL_CHARS 64

/// @brief The bytes a key string starts with room for.
#define KEY_CAPACITY 32

static int by_bytes(const void *a, const void *b, void *ctx);
static int by_char(const void *a, const void *b, void *ctx);
static int by_size(const void *a, const void *b, void *ctx);
static void free_group(anagram_t *an, anagram_group_t *group);
static void free_groups(anagram_t *an);
static int live(const anagram_t *an);
static uint32_t next_char(const unsigned char *bytes, size_t length, size_t *i);

/// @brief Orders words, given pointers to their pointers, in byte order.
static int by_bytes(const void *a, const void *b, void *ctx) {
    (void)ctx;
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/// @brief Orders packed characters, and so their bytes.
static int by_char(const void *a, const void *b, void *ctx) {
    (void)ctx;
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/// @brief Orders groups, given pointers to their pointers, largest first,
/// then by their first word, which is the least once their words are sorted.
/// No two groups share a word, so no two groups are equal.
static int by_size(const void *a, const void *b, void *ctx) {
    (void)ctx;
    const anagram_group_t *ga = *(anagram_group_t *const *)a;
    const anagram_group_t *gb = *(anagram_group_t *const *)b;
    if (ga->count != gb->count) {
        return ga->count > gb->count ? -1 : 1;
    }
    return strcmp(ga->words[0], gb->words[0]);
}

/// @brief Frees a group's words, its array and the group, but not its key,
/// which belongs to the map.
static void free_group(anagram_t *an, anagram_group_t *group) {
    for (size_t i = 0; i < group->count; i++) {
        sof_alloc_free(&an->alloc, group->words[i], strlen(group->words[i]) + 1);
    }
    sof_alloc_free(&an->alloc, group->words, group->capacity * sizeof(char *));
    sof_alloc_free(&an->alloc, group, sizeof(anagram_group_t));
}

/// @brief Frees every group, leaving the array of them empty.
static void free_groups(anagram_t *an) {
    for (size_t i = 0; i < an->count; i++) {
        free_group(an, an->groups[i]);
    }
    an->count = 0;
    an->words = 0;
}

/// @brief Returns 1 if an is initialized: not NULL, freed or failed.
static int live(const anagram_t *an) {
    return an != NULL && an->map.elements != NULL;
}

/// @brief Reads the character at bytes[*i]: a byte and up to three
/// continuation bytes after it, packed from the top of the result down, so
/// unused low bytes are 0. Moves *i past it.
static uint32_t next_char(const unsigned char *bytes, size_t length, size_t *i) {
    uint32_t c = bytes[(*i)++];
    size_t n = 1;
    while (*i < length && n < 4 && (bytes[*i] & 0xc0) == 0x80) {
        c = c << 8 | bytes[(*i)++];
        n++;
    }
    return c << (8 * (4 - n));
}

anagram_error_t anagram_add(anagram_t *an, const char *word) {
    if (!live(an) || word == NULL || word[0] == '\0') {
        return ANAGRAM_ERROR;
    }
    anagram_error_t err = anagram_key(an, word, &an->key);
    if (err != ANAGRAM_OK) {
        return err;
    }
    const char *key = sof_string_cstr(&an->key);
    sof_hashmap_element_t *e = sof_hashmap_find(&an->map, key);
    anagram_group_t *group;
    int created = 0;
    if (e != NULL) {
        group = e->data;
        for (size_t i = 0; i < group->count; i++) {
            if (strcmp(group->words[i], word) == 0) {
                return ANAGRAM_NO_MATCH;
            }
        }
    } else {
        if (an->count == an->capacity) {
            size_t capacity = an->capacity == 0 ? 16 : 2 * an->capacity;
            anagram_group_t **grown = sof_alloc_realloc(&an->alloc, an->groups, an->capacity * sizeof(anagram_group_t *), capacity * sizeof(anagram_group_t *));
            if (grown == NULL) {
                return ANAGRAM_NO_MEM;
            }
            an->groups = grown;
            an->capacity = capacity;
        }
        group = sof_alloc_calloc(&an->alloc, 1, sizeof(anagram_group_t));
        if (group == NULL) {
            return ANAGRAM_NO_MEM;
        }
        e = sof_hashmap_insert(&an->map, key, NULL);
        if (e == NULL) {
            sof_alloc_free(&an->alloc, group, sizeof(anagram_group_t));
            return ANAGRAM_NO_MEM;
        }
        e->data = group;
        group->key = e->key;
        created = 1;
    }
    if (group->count == group->capacity) {
        size_t capacity = group->capacity == 0 ? 2 : 2 * group->capacity;
        char **grown = sof_alloc_realloc(&an->alloc, group->words, group->capacity * sizeof(char *), capacity * sizeof(char *));
        if (grown == NULL) {
            goto undo;
        }
        group->words = grown;
        group->capacity = capacity;
    }
    char *copy = sof_alloc_strndup(&an->alloc, word, strlen(word));
    if (copy == NULL) {
        goto undo;
    }
    group->words[group->count++] = copy;
    if (created) {
        an->groups[an->count++] = group;
    }
    an->words++;
    return ANAGRAM_OK;

undo:
    // A new group is taken out of the map again (which frees its key), so
    // no group is ever empty; nothing has moved the element since it was
    // inserted.
    if (created) {
        sof_hashmap_remove_element(&an->map, e);
        free_group(an, group);
    }
    return ANAGRAM_NO_MEM;
}

anagram_error_t anagram_clear(anagram_t *an) {
    if (!live(an)) {
        return ANAGRAM_ERROR;
    }
    free_groups(an);
    sof_hashmap_clear(&an->map);
    return ANAGRAM_OK;
}

const anagram_group_t *anagram_find(const anagram_t *an, const char *word) {
    if (!live(an) || word == NULL) {
        return NULL;
    }
    sof_string_t key;
    if (sof_string_init(&key, KEY_CAPACITY, &an->alloc) != SOF_STRING_OK) {
        return NULL;
    }
    const anagram_group_t *group = NULL;
    if (anagram_key(an, word, &key) == ANAGRAM_OK) {
        const sof_hashmap_element_t *e = sof_hashmap_find(&an->map, sof_string_cstr(&key));
        if (e != NULL) {
            group = e->data;
        }
    }
    sof_string_free(&key);
    return group;
}

anagram_error_t anagram_free(anagram_t *an) {
    if (an == NULL) {
        return ANAGRAM_ERROR;
    }
    free_groups(an);
    sof_alloc_free(&an->alloc, an->groups, an->capacity * sizeof(anagram_group_t *));
    sof_hashmap_free(&an->map);
    sof_string_free(&an->key);
    memset(an, 0, sizeof(anagram_t));
    return ANAGRAM_OK;
}

anagram_error_t anagram_init(anagram_t *an, int fold_case, const sof_alloc_t *alloc) {
    if (an == NULL) {
        return ANAGRAM_ERROR;
    }
    memset(an, 0, sizeof(anagram_t));
    if (!sof_alloc_valid(alloc)) {
        return ANAGRAM_ERROR;
    }
    if (alloc != NULL) {
        an->alloc = *alloc;
    }
    an->fold_case = fold_case != 0;
    sof_hashmap_error_t map_err = sof_hashmap_init(&an->map, &sof_hashmap_string_keys, NULL, NULL, 0, &an->alloc);
    if (map_err != SOF_HASHMAP_OK) {
        memset(an, 0, sizeof(anagram_t));
        return map_err == SOF_HASHMAP_NO_MEM ? ANAGRAM_NO_MEM : ANAGRAM_ERROR;
    }
    if (sof_string_init(&an->key, KEY_CAPACITY, &an->alloc) != SOF_STRING_OK) {
        sof_hashmap_free(&an->map);
        memset(an, 0, sizeof(anagram_t));
        return ANAGRAM_NO_MEM;
    }
    return ANAGRAM_OK;
}

anagram_error_t anagram_key(const anagram_t *an, const char *word, sof_string_t *key) {
    if (!live(an) || word == NULL || key == NULL) {
        return ANAGRAM_ERROR;
    }
    if (sof_string_clear(key) != SOF_STRING_OK) {
        return ANAGRAM_ERROR;
    }
    if (sof_string_append_cstr(key, word) != SOF_STRING_OK) {
        return ANAGRAM_NO_MEM;
    }
    if (an->fold_case) {
        sof_string_tolower(key);
    }
    const unsigned char *bytes = (const unsigned char *)sof_string_cstr(key);
    size_t length = key->length, n = 0;
    for (size_t i = 0; i < length; n++) {
        next_char(bytes, length, &i);
    }
    uint32_t local[LOCAL_CHARS], *chars = local;
    if (n > LOCAL_CHARS) {
        chars = sof_alloc_malloc(&an->alloc, n * sizeof(uint32_t));
        if (chars == NULL) {
            return ANAGRAM_NO_MEM;
        }
    }
    for (size_t i = 0, c = 0; i < length; c++) {
        chars[c] = next_char(bytes, length, &i);
    }
    sof_sort_quick(chars, n, sizeof(uint32_t), by_char, NULL);
    // The key is rebuilt from the sorted characters. It is as long as it
    // was, so it has room and appending can't fail.
    sof_string_clear(key);
    for (size_t c = 0; c < n; c++) {
        char out[5];
        size_t k = 0;
        for (int shift = 24; shift >= 0 && ((chars[c] >> shift) & 0xff) != 0; shift -= 8) {
            out[k++] = (char)((chars[c] >> shift) & 0xff);
        }
        out[k] = '\0';
        sof_string_append_cstr(key, out);
    }
    if (chars != local) {
        sof_alloc_free(&an->alloc, chars, n * sizeof(uint32_t));
    }
    return ANAGRAM_OK;
}

const anagram_group_t *anagram_next(const anagram_t *an, size_t *position) {
    if (!live(an) || position == NULL || *position >= an->count) {
        return NULL;
    }
    return an->groups[(*position)++];
}

anagram_error_t anagram_sort(anagram_t *an) {
    if (!live(an)) {
        return ANAGRAM_ERROR;
    }
    for (size_t i = 0; i < an->count; i++) {
        sof_sort_quick(an->groups[i]->words, an->groups[i]->count, sizeof(char *), by_bytes, NULL);
    }
    sof_sort_quick(an->groups, an->count, sizeof(anagram_group_t *), by_size, NULL);
    return ANAGRAM_OK;
}

int anagram_traverse(const anagram_t *an, anagram_traverse_func_t cb, void *ctx) {
    if (!live(an)) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    for (size_t i = 0; i < an->count; i++) {
        int rc = cb(an->groups[i], ctx);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}
