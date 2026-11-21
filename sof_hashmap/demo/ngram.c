/**
 * @file ngram.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief N-gram counting in C, for the ngram program.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_sort.h"
#include "sof_utf.h"
#include "ngram.h"

/// @brief The set of characters grams are made of when the options give
/// none.
#define DEFAULT_CHARS "A-Za-z0-9"

/// @brief Grams that take up to this many bytes as they are stored,
/// counting the NUL, are looked up by ngram_find() without allocating.
#define FIND_BUFFER 256

static ngram_error_t add_char(ngram_t *ng, uint32_t c);
static ngram_error_t add_code(ngram_t *ng, uint32_t code);
static ngram_error_t add_word_char(ngram_t *ng, uint32_t c);
static int compare_ranges(const void *a, const void *b, void *ctx);
static ngram_error_t count_gram(ngram_t *ng, size_t n, const char *gram);
static ngram_error_t end_word(ngram_t *ng);
static ngram_error_t grow(const sof_alloc_t *alloc, char **p, size_t *size, size_t needed);
static sof_hashmap_t *map_of(const ngram_t *ng, size_t n);
static uint32_t member(const ngram_t *ng, uint32_t code);
static size_t normalize(const ngram_t *ng, const char *gram, size_t length, char *key, size_t *stored);
static ngram_error_t parse_set(ngram_t *ng, const char *chars);
static void set_ascii(ngram_t *ng, uint32_t first, uint32_t last);
static unsigned char to_lower(unsigned char c);
static unsigned char to_upper(unsigned char c);

/// @brief Counts one character that is in the set, in character mode: every
/// gram ending with it.
static ngram_error_t add_char(ngram_t *ng, uint32_t c) {
    if (ng->run == ng->max) {
        // Drop the oldest character, which no gram ending here includes.
        size_t width = ng->widths[0];
        memmove(ng->gram, ng->gram + width, ng->gram_length - width);
        ng->gram_length -= width;
        memmove(ng->widths, ng->widths + 1, ng->max - 1);
        ng->run--;
    }
    size_t width = sof_utf8_encode(c, ng->gram + ng->gram_length, SOF_UTF8_SIZE_MAX);
    ng->widths[ng->run++] = (unsigned char)width;
    ng->gram_length += width;
    ng->gram[ng->gram_length] = '\0';
    if (ng->quiet) {
        return NGRAM_OK;
    }
    ng->units++;
    // The gram of the last k characters starts where their widths add up to,
    // counting back from the end.
    size_t start = ng->gram_length;
    for (size_t k = 1; k <= ng->run; k++) {
        start -= ng->widths[ng->run - k];
        if (k >= ng->min) {
            ngram_error_t err = count_gram(ng, k, ng->gram + start);
            if (err != NGRAM_OK) {
                return err;
            }
        }
    }
    return NGRAM_OK;
}

/// @brief Reads one character of text: a code point, or SOF_UTF_INVALID.
static ngram_error_t add_code(ngram_t *ng, uint32_t code) {
    uint32_t c = member(ng, code);
    if (ng->unit == NGRAM_CHARS) {
        if (c == 0) {
            ng->run = 0;
            ng->gram_length = 0;
            return NGRAM_OK;
        }
        return add_char(ng, c);
    }
    if (c != 0) {
        return add_word_char(ng, c);
    }
    if (ng->recent_length[ng->next] > 0) {
        return end_word(ng);
    }
    return NGRAM_OK;
}

/// @brief Adds one character that is in the set to the word in progress.
static ngram_error_t add_word_char(ngram_t *ng, uint32_t c) {
    size_t i = ng->next;
    size_t width = sof_utf8_size(c);
    // Room for the character and the NUL end_word() adds.
    if (grow(&ng->alloc, &ng->recent[i], &ng->recent_size[i], ng->recent_length[i] + width + 1) != NGRAM_OK) {
        return NGRAM_NO_MEM;
    }
    ng->recent_length[i] += sof_utf8_encode(c, ng->recent[i] + ng->recent_length[i], width);
    return NGRAM_OK;
}

/// @brief Orders ranges by their first code point, for sof_sort.
static int compare_ranges(const void *a, const void *b, void *ctx) {
    const ngram_range_t *ra = a, *rb = b;
    return (ra->first > rb->first) - (ra->first < rb->first);
}

/// @brief Adds one to the count of a gram of length n.
static ngram_error_t count_gram(ngram_t *ng, size_t n, const char *gram) {
    sof_hashmap_element_t *e = sof_hashmap_insert(map_of(ng, n), gram, NULL);
    if (e == NULL) {
        return NGRAM_NO_MEM;
    }
    // The data is the gram's count, stored in the pointer itself.
    e->data = (void *)((uintptr_t)e->data + 1);
    ng->totals[n - ng->min]++;
    return NGRAM_OK;
}

/// @brief Ends the word in progress, which isn't empty, and counts every gram
/// ending with it.
static ngram_error_t end_word(ngram_t *ng) {
    ng->recent[ng->next][ng->recent_length[ng->next]] = '\0';
    ng->next = (ng->next + 1) % ng->max;
    if (ng->run < ng->max) {
        ng->run++;
    }
    if (ng->quiet) {
        // Context: the word is kept for the grams after it, but not counted.
        ng->recent_length[ng->next] = 0;
        return NGRAM_OK;
    }
    ng->units++;
    ngram_error_t err = NGRAM_OK;
    for (size_t n = ng->min; n <= ng->run && err == NGRAM_OK; n++) {
        // The last n words, oldest first, joined with spaces.
        size_t first = (ng->next + ng->max - n) % ng->max, length = n;
        for (size_t k = 0; k < n; k++) {
            length += ng->recent_length[(first + k) % ng->max];
        }
        err = grow(&ng->alloc, &ng->gram, &ng->gram_size, length);
        if (err != NGRAM_OK) {
            break;
        }
        char *p = ng->gram;
        for (size_t k = 0; k < n; k++) {
            size_t w = (first + k) % ng->max;
            if (k > 0) {
                *p++ = ' ';
            }
            memcpy(p, ng->recent[w], ng->recent_length[w]);
            p += ng->recent_length[w];
        }
        *p = '\0';
        err = count_gram(ng, n, ng->gram);
    }
    // The oldest word, whose slot the next word takes, is no longer needed.
    ng->recent_length[ng->next] = 0;
    return err;
}

/// @brief Makes *p, of *size bytes, at least needed bytes, doubling it.
static ngram_error_t grow(const sof_alloc_t *alloc, char **p, size_t *size, size_t needed) {
    if (needed <= *size) {
        return NGRAM_OK;
    }
    size_t new_size = *size == 0 ? 16 : *size;
    while (new_size < needed) {
        if (new_size > SIZE_MAX / 2) {
            return NGRAM_NO_MEM;
        }
        new_size *= 2;
    }
    char *grown = sof_alloc_realloc(alloc, *p, *size, new_size);
    if (grown == NULL) {
        return NGRAM_NO_MEM;
    }
    *p = grown;
    *size = new_size;
    return NGRAM_OK;
}

/// @brief Returns the map of grams of length n, or NULL if n is outside the
/// range or the counter isn't initialized.
static sof_hashmap_t *map_of(const ngram_t *ng, size_t n) {
    if (ng == NULL || ng->maps == NULL || n < ng->min || n > ng->max) {
        return NULL;
    }
    return &ng->maps[n - ng->min];
}

/// @brief ngram_member() for a counter known to be initialized.
static uint32_t member(const ngram_t *ng, uint32_t code) {
    if (code < 0x80) {
        return ng->map[code];
    }
    if (!sof_utf_valid(code)) {
        code = SOF_UTF_REPLACEMENT;
    }
    // The ranges are sorted and don't touch, so a binary search finds the
    // only one that can hold code.
    size_t lo = 0, hi = ng->range_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (code < ng->ranges[mid].first) {
            hi = mid;
        } else if (code > ng->ranges[mid].last) {
            lo = mid + 1;
        } else {
            return code;
        }
    }
    return 0;
}

/// @brief Reads length bytes of gram as ngram_add() would store it.
/// @param key Where the gram goes as stored, NUL-terminated, or NULL to only
/// measure it.
/// @param stored Set to the number of bytes the gram takes as stored, not
/// counting the NUL.
/// @return The gram's length in characters or words, or 0 if it isn't one
/// gram.
static size_t normalize(const ngram_t *ng, const char *gram, size_t length, char *key, size_t *stored) {
    size_t position = 0, n = 0, bytes = 0;
    int in_word = 0;
    uint32_t code;
    *stored = 0;
    while (sof_utf8_next(gram, length, &position, &code)) {
        uint32_t c = member(ng, code);
        if (ng->unit == NGRAM_CHARS) {
            if (c == 0) {
                return 0;
            }
            n++;
        } else if (c == 0) {
            in_word = 0;
            continue;
        } else if (!in_word) {
            if (n > 0) {
                if (key != NULL) {
                    key[bytes] = ' ';
                }
                bytes++;
            }
            n++;
            in_word = 1;
        }
        if (key != NULL) {
            sof_utf8_encode(c, key + bytes, SOF_UTF8_SIZE_MAX);
        }
        bytes += sof_utf8_size(c);
    }
    if (key != NULL) {
        key[bytes] = '\0';
    }
    *stored = bytes;
    return n;
}

/// @brief Fills ng->map and ng->ranges from a set such as "a-z0-9à-ÿ".
/// @details ng->alloc must already be set. The ranges above ASCII are sorted
/// and those that overlap or touch are joined, so two counters with the same
/// set have the same ranges however the set was written.
/// @return NGRAM_OK, NGRAM_NO_MEM, or NGRAM_ERROR if chars isn't valid UTF-8
/// or the set is empty. On an error, ng->ranges may still need freeing.
static ngram_error_t parse_set(ngram_t *ng, const char *chars) {
    size_t length = strlen(chars);
    if (sof_utf8_validate(chars, length) != length) {
        return NGRAM_ERROR;
    }
    // The first pass fills map and counts the ranges above ASCII; the second
    // stores them.
    size_t count = 0;
    for (int pass = 0; pass < 2; pass++) {
        size_t position = 0;
        uint32_t first;
        count = 0;
        while (sof_utf8_next(chars, length, &position, &first)) {
            uint32_t last = first, dash, end;
            size_t after = position;
            if (sof_utf8_next(chars, length, &after, &dash) && dash == '-' && sof_utf8_next(chars, length, &after, &end)) {
                last = end;
                position = after;
            }
            if (first > last) {
                continue;
            }
            if (pass == 0) {
                set_ascii(ng, first, last);
            }
            if (last >= 0x80) {
                if (pass == 1) {
                    ng->ranges[count].first = first < 0x80 ? 0x80 : first;
                    ng->ranges[count].last = last;
                }
                count++;
            }
        }
        if (pass == 0) {
            if (count == 0) {
                break;
            }
            ng->ranges = sof_alloc_malloc(&ng->alloc, count * sizeof(ngram_range_t));
            if (ng->ranges == NULL) {
                return NGRAM_NO_MEM;
            }
            ng->range_count = count;
        }
    }
    if (count > 0) {
        sof_sort_insertion(ng->ranges, count, sizeof(ngram_range_t), compare_ranges, NULL);
        size_t joined = 0;
        for (size_t i = 1; i < count; i++) {
            ngram_range_t *last = &ng->ranges[joined];
            if (ng->ranges[i].first <= last->last + 1) {
                if (ng->ranges[i].last > last->last) {
                    last->last = ng->ranges[i].last;
                }
            } else {
                ng->ranges[++joined] = ng->ranges[i];
            }
        }
        joined++;
        if (joined < count) {
            // On failure the array is unchanged and still count long, so
            // ngram_free() frees it with the right size.
            ngram_range_t *fit = sof_alloc_realloc(&ng->alloc, ng->ranges, count * sizeof(ngram_range_t), joined * sizeof(ngram_range_t));
            if (fit == NULL) {
                return NGRAM_NO_MEM;
            }
            ng->ranges = fit;
            ng->range_count = joined;
        }
    }
    if (ng->range_count > 0) {
        return NGRAM_OK;
    }
    for (size_t c = 1; c < sizeof(ng->map); c++) {
        if (ng->map[c] != 0) {
            return NGRAM_OK;
        }
    }
    return NGRAM_ERROR;
}

/// @brief Puts the ASCII characters from first to last in ng->map.
static void set_ascii(ngram_t *ng, uint32_t first, uint32_t last) {
    for (uint32_t c = first; c <= last && c < 0x80; c++) {
        unsigned char lower = to_lower((unsigned char)c);
        if (ng->fold_case) {
            ng->map[lower] = lower;
            ng->map[to_upper(lower)] = lower;
        } else {
            ng->map[c] = (unsigned char)c;
        }
    }
}

/// @brief Folds an ASCII letter to lowercase; locale-independent, unlike
/// tolower().
static unsigned char to_lower(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c - 'A' + 'a') : c;
}

/// @brief Folds an ASCII letter to uppercase.
static unsigned char to_upper(unsigned char c) {
    return c >= 'a' && c <= 'z' ? (unsigned char)(c - 'a' + 'A') : c;
}

ngram_error_t ngram_add(ngram_t *ng, const char *text, size_t length) {
    if (ng == NULL || ng->maps == NULL) {
        return NGRAM_ERROR;
    }
    if (text == NULL) {
        length = 0;
    }
    const unsigned char *s = (const unsigned char *)text;
    size_t i = 0;
    while (1) {
        // A character cut off by the end of the last piece is decoded from
        // its bytes so far followed by this piece's.
        unsigned char joined[2 * SOF_UTF8_SIZE_MAX];
        const unsigned char *p;
        size_t available;
        size_t pending = ng->pending_length;
        if (pending > 0) {
            size_t take = length - i < SOF_UTF8_SIZE_MAX ? length - i : SOF_UTF8_SIZE_MAX;
            memcpy(joined, ng->pending, pending);
            if (take > 0) {
                // s may be NULL, when take is 0, and memcpy() mustn't see it.
                memcpy(joined + pending, s + i, take);
            }
            p = joined;
            available = pending + take;
        } else if (i < length) {
            p = s + i;
            available = length - i;
        } else {
            break;
        }
        uint32_t code;
        size_t used = sof_utf8_decode(p, available, &code);
        if (code == SOF_UTF_INCOMPLETE) {
            // The rest of the text, fewer bytes than a character takes, is
            // the start of one: keep it for the next piece.
            memcpy(ng->pending, p, available);
            ng->pending_length = available;
            break;
        }
        if (used <= pending) {
            // An error made only of pending bytes: the rest of them start
            // the next character.
            memmove(ng->pending, ng->pending + used, pending - used);
            ng->pending_length = pending - used;
        } else {
            i += used - pending;
            ng->pending_length = 0;
        }
        ngram_error_t err = add_code(ng, code);
        if (err != NGRAM_OK) {
            return err;
        }
    }
    return NGRAM_OK;
}

ngram_error_t ngram_clear(ngram_t *ng) {
    if (ng == NULL || ng->maps == NULL) {
        return NGRAM_ERROR;
    }
    for (size_t n = ng->min; n <= ng->max; n++) {
        sof_hashmap_clear(map_of(ng, n));
        ng->totals[n - ng->min] = 0;
    }
    if (ng->recent_length != NULL) {
        memset(ng->recent_length, 0, ng->max * sizeof(size_t));
    }
    ng->units = 0;
    ng->run = 0;
    ng->next = 0;
    ng->gram_length = 0;
    ng->pending_length = 0;
    return NGRAM_OK;
}

ngram_error_t ngram_context(ngram_t *ng, const char *text, size_t length) {
    if (ng == NULL || ng->maps == NULL) {
        return NGRAM_ERROR;
    }
    ng->quiet = 1;
    ngram_error_t err = ngram_add(ng, text, length);
    ng->quiet = 0;
    return err;
}

size_t ngram_distinct(const ngram_t *ng, size_t n) {
    sof_hashmap_t *map = map_of(ng, n);
    return map == NULL ? 0 : map->count;
}

size_t ngram_find(const ngram_t *ng, const char *gram) {
    if (ng == NULL || ng->maps == NULL || gram == NULL) {
        return 0;
    }
    size_t length = strlen(gram), stored;
    sof_hashmap_t *map = map_of(ng, normalize(ng, gram, length, NULL, &stored));
    if (map == NULL) {
        return 0;
    }
    char buffer[FIND_BUFFER], *key = buffer;
    size_t size = stored + 1;
    if (size > sizeof(buffer)) {
        key = sof_alloc_malloc(&ng->alloc, size);
        if (key == NULL) {
            return 0;
        }
    }
    normalize(ng, gram, length, key, &stored);
    size_t count = 0;
    sof_hashmap_element_t *e = sof_hashmap_find(map, key);
    if (e != NULL) {
        count = (size_t)(uintptr_t)e->data;
    }
    if (key != buffer) {
        sof_alloc_free(&ng->alloc, key, size);
    }
    return count;
}

ngram_error_t ngram_flush(ngram_t *ng) {
    if (ng == NULL || ng->maps == NULL) {
        return NGRAM_ERROR;
    }
    ngram_error_t err = NGRAM_OK;
    if (ng->pending_length > 0) {
        // A character the end of the text cut off is one error.
        ng->pending_length = 0;
        err = add_code(ng, SOF_UTF_INVALID);
    }
    if (err == NGRAM_OK && ng->unit == NGRAM_WORDS && ng->recent_length[ng->next] > 0) {
        err = end_word(ng);
    }
    ng->run = 0;
    ng->gram_length = 0;
    return err;
}

ngram_error_t ngram_free(ngram_t *ng) {
    if (ng == NULL) {
        return NGRAM_ERROR;
    }
    size_t lengths = ng->max - ng->min + 1;
    if (ng->maps != NULL) {
        for (size_t i = 0; i < lengths; i++) {
            sof_hashmap_free(&ng->maps[i]);
        }
        sof_alloc_free(&ng->alloc, ng->maps, lengths * sizeof(sof_hashmap_t));
    }
    sof_alloc_free(&ng->alloc, ng->totals, lengths * sizeof(size_t));
    // Either can be NULL if ngram_init() failed, in which case the words
    // are too.
    if (ng->recent != NULL && ng->recent_size != NULL) {
        for (size_t i = 0; i < ng->max; i++) {
            sof_alloc_free(&ng->alloc, ng->recent[i], ng->recent_size[i]);
        }
    }
    sof_alloc_free(&ng->alloc, ng->recent, ng->max * sizeof(char *));
    sof_alloc_free(&ng->alloc, ng->recent_length, ng->max * sizeof(size_t));
    sof_alloc_free(&ng->alloc, ng->recent_size, ng->max * sizeof(size_t));
    sof_alloc_free(&ng->alloc, ng->gram, ng->gram_size);
    sof_alloc_free(&ng->alloc, ng->widths, ng->max);
    sof_alloc_free(&ng->alloc, ng->ranges, ng->range_count * sizeof(ngram_range_t));
    memset(ng, 0, sizeof(*ng));
    return NGRAM_OK;
}

ngram_error_t ngram_init(ngram_t *ng, const ngram_options_t *options, const sof_alloc_t *alloc) {
    if (ng == NULL) {
        return NGRAM_ERROR;
    }
    memset(ng, 0, sizeof(*ng));
    ngram_options_t defaults = {0};
    if (options == NULL) {
        options = &defaults;
    }
    size_t min = options->min == 0 ? 1 : options->min;
    size_t max = options->max == 0 ? min : options->max;
    if (!sof_alloc_valid(alloc) || max < min || (options->unit != NGRAM_CHARS && options->unit != NGRAM_WORDS)) {
        return NGRAM_ERROR;
    }
    if (alloc != NULL) {
        ng->alloc = *alloc;
    }
    ng->unit = options->unit;
    ng->fold_case = options->fold_case != 0;
    ngram_error_t err = parse_set(ng, options->chars == NULL ? DEFAULT_CHARS : options->chars);
    if (err != NGRAM_OK) {
        ngram_free(ng);
        return err;
    }
    // Set before anything else is allocated, so ngram_free() can undo a
    // failure.
    ng->min = min;
    ng->max = max;
    size_t lengths = max - min + 1;
    ng->totals = sof_alloc_calloc(&ng->alloc, lengths, sizeof(size_t));
    if (ng->totals == NULL) {
        goto no_mem;
    }
    if (ng->unit == NGRAM_CHARS) {
        // max characters of up to SOF_UTF8_SIZE_MAX bytes each, and a NUL.
        if (max > (SIZE_MAX - 1) / SOF_UTF8_SIZE_MAX) {
            goto no_mem;
        }
        ng->gram_size = max * SOF_UTF8_SIZE_MAX + 1;
        ng->gram = sof_alloc_malloc(&ng->alloc, ng->gram_size);
        ng->widths = sof_alloc_malloc(&ng->alloc, max);
        if (ng->gram == NULL || ng->widths == NULL) {
            if (ng->gram == NULL) {
                ng->gram_size = 0;
            }
            goto no_mem;
        }
    } else {
        ng->recent = sof_alloc_calloc(&ng->alloc, max, sizeof(char *));
        ng->recent_length = sof_alloc_calloc(&ng->alloc, max, sizeof(size_t));
        ng->recent_size = sof_alloc_calloc(&ng->alloc, max, sizeof(size_t));
        if (ng->recent == NULL || ng->recent_length == NULL || ng->recent_size == NULL) {
            goto no_mem;
        }
    }
    sof_hashmap_t *maps = sof_alloc_calloc(&ng->alloc, lengths, sizeof(sof_hashmap_t));
    if (maps == NULL) {
        goto no_mem;
    }
    // Every map is freed on a failure, those not yet initialized being
    // zeroed, which sof_hashmap_free() takes as empty.
    ng->maps = maps;
    for (size_t i = 0; i < lengths; i++) {
        if (sof_hashmap_init(&maps[i], &sof_hashmap_string_keys, NULL, NULL, 0, &ng->alloc) != SOF_HASHMAP_OK) {
            goto no_mem;
        }
    }
    return NGRAM_OK;
no_mem:
    ngram_free(ng);
    return NGRAM_NO_MEM;
}

uint32_t ngram_member(const ngram_t *ng, uint32_t code) {
    if (ng == NULL || ng->maps == NULL) {
        return 0;
    }
    return member(ng, code);
}

ngram_error_t ngram_merge(ngram_t *ng, const ngram_t *from) {
    if (ng == NULL || ng->maps == NULL || from == NULL || from->maps == NULL || ng == from) {
        return NGRAM_ERROR;
    }
    if (ng->unit != from->unit || ng->min != from->min || ng->max != from->max || ng->fold_case != from->fold_case || memcmp(ng->map, from->map, sizeof(ng->map)) != 0 || ng->range_count != from->range_count || (ng->range_count > 0 && memcmp(ng->ranges, from->ranges, ng->range_count * sizeof(ngram_range_t)) != 0)) {
        return NGRAM_ERROR;
    }
    for (size_t n = ng->min; n <= ng->max; n++) {
        sof_hashmap_t *map = map_of(ng, n);
        size_t position = 0;
        sof_hashmap_element_t *e;
        while ((e = sof_hashmap_next(map_of(from, n), &position)) != NULL) {
            sof_hashmap_element_t *to = sof_hashmap_insert(map, e->key, NULL);
            if (to == NULL) {
                return NGRAM_NO_MEM;
            }
            to->data = (void *)((uintptr_t)to->data + (uintptr_t)e->data);
        }
        ng->totals[n - ng->min] += from->totals[n - from->min];
    }
    ng->units += from->units;
    return NGRAM_OK;
}

const char *ngram_next(const ngram_t *ng, size_t n, size_t *position, size_t *count) {
    sof_hashmap_element_t *e = sof_hashmap_next(map_of(ng, n), position);
    if (e == NULL) {
        return NULL;
    }
    if (count != NULL) {
        *count = (size_t)(uintptr_t)e->data;
    }
    return e->key;
}

size_t ngram_total(const ngram_t *ng, size_t n) {
    sof_hashmap_t *map = map_of(ng, n);
    return map == NULL ? 0 : ng->totals[n - ng->min];
}

int ngram_traverse(const ngram_t *ng, size_t n, ngram_traverse_func_t cb, void *ctx) {
    sof_hashmap_t *map = map_of(ng, n);
    if (map == NULL) {
        return -1;
    }
    if (cb == NULL) {
        return 0;
    }
    size_t position = 0;
    sof_hashmap_element_t *e;
    while ((e = sof_hashmap_next(map, &position)) != NULL) {
        int rc = cb(e->key, (size_t)(uintptr_t)e->data, ctx);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}
