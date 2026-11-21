/**
 * @file markov.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A Markov chain text generator in C, for the markov program.
 * @details The counts are kept by an ngram_t, for lengths n - 1 and n. The
 * chain is made from them by markov_build(): a state per (n - 1)-gram, found
 * through a hashmap, and the n-grams split into the state they start with and
 * the word or character they end with, its successor. Nothing in the chain
 * depends on the order the hashmaps give their grams in: the successors of
 * each state, and the starts, are sorted by bytes before anything is chosen
 * from them.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <string.h>

#include "sof_sort.h"
#include "sof_utf.h"
#include "markov.h"

static int by_context(const void *a, const void *b, void *ctx);
static int by_unit(const void *a, const void *b, void *ctx);
static markov_error_t count_successors(markov_t *m, sof_string_t *scratch, int fill);
static void drop_chain(markov_t *m);
static markov_error_t from_ngram(ngram_error_t err);
static int is_ready(const markov_t *m);
static const markov_state_t *pick_start(const markov_t *m, sof_random_t *r);
static const markov_successor_t *pick_successor(const markov_state_t *state, sof_random_t *r);
static markov_state_t *prefix_state(const markov_t *m, sof_string_t *scratch, const char *gram, const char **unit, markov_error_t *err);
static size_t unit_offset(const markov_t *m, const char *s, size_t k);

/// @brief Hashes and compares the contexts in the index, which are the
/// counter's own strings, so the index neither copies nor frees them.
static const sof_hashmap_key_callbacks_t context_keys = {
    sof_hashmap_string_hash,
    sof_hashmap_string_equal,
    NULL,
    NULL,
};

/// @brief Orders pointers to states by context.
static int by_context(const void *a, const void *b, void *ctx) {
    (void)ctx;
    const markov_state_t *sa = *(const markov_state_t *const *)a;
    const markov_state_t *sb = *(const markov_state_t *const *)b;
    return strcmp(sa->context, sb->context);
}

/// @brief Orders successors by their word or character.
static int by_unit(const void *a, const void *b, void *ctx) {
    (void)ctx;
    return strcmp(((const markov_successor_t *)a)->unit, ((const markov_successor_t *)b)->unit);
}

/// @brief Goes through the n-grams, counting each state's successors in its
/// next_count, or with fill set, storing them in its next array, which
/// next_count indexes from 0.
/// @details An n-gram whose (n - 1)-gram prefix wasn't counted, which can
/// only happen if memory ran out while counting, is skipped.
static markov_error_t count_successors(markov_t *m, sof_string_t *scratch, int fill) {
    size_t position = 0, count;
    const char *gram;
    while ((gram = ngram_next(&m->counts, m->order, &position, &count)) != NULL) {
        markov_error_t err;
        const char *unit;
        markov_state_t *state = prefix_state(m, scratch, gram, &unit, &err);
        if (err != MARKOV_OK) {
            return err;
        }
        if (state == NULL) {
            continue;
        }
        if (fill) {
            state->next[state->next_count].unit = unit;
            state->next[state->next_count].count = count;
            state->total += count;
        }
        state->next_count++;
    }
    return MARKOV_OK;
}

/// @brief Frees the chain, if there is one, leaving none.
static void drop_chain(markov_t *m) {
    sof_hashmap_free(&m->index);
    sof_alloc_free(&m->alloc, m->states, m->state_count * sizeof(markov_state_t));
    sof_alloc_free(&m->alloc, m->successors, m->successor_count * sizeof(markov_successor_t));
    sof_alloc_free(&m->alloc, m->starts, m->start_count * sizeof(markov_state_t *));
    m->states = NULL;
    m->state_count = 0;
    m->successors = NULL;
    m->successor_count = 0;
    m->starts = NULL;
    m->start_count = 0;
    m->start_total = 0;
}

/// @brief Gives the markov error for an ngram error.
static markov_error_t from_ngram(ngram_error_t err) {
    switch (err) {
    case NGRAM_OK:
        return MARKOV_OK;
    case NGRAM_NO_MEM:
        return MARKOV_NO_MEM;
    default:
        return MARKOV_ERROR;
    }
}

/// @brief Returns 1 if m is initialized.
static int is_ready(const markov_t *m) {
    return m != NULL && m->counts.maps != NULL;
}

/// @brief Chooses a start, each as likely as its total. There must be one.
static const markov_state_t *pick_start(const markov_t *m, sof_random_t *r) {
    uint64_t v = sof_random_below(r, m->start_total);
    size_t i = 0;
    while (v >= m->starts[i]->total) {
        v -= m->starts[i]->total;
        i++;
    }
    return m->starts[i];
}

/// @brief Chooses a successor of a state, each as likely as its count. The
/// state must have one.
static const markov_successor_t *pick_successor(const markov_state_t *state, sof_random_t *r) {
    uint64_t v = sof_random_below(r, state->total);
    size_t i = 0;
    while (v >= state->next[i].count) {
        v -= state->next[i].count;
        i++;
    }
    return &state->next[i];
}

/// @brief Splits an n-gram into its first n - 1 words or characters and its
/// last, and finds the state of the first.
/// @param unit Set to where the last word or character starts in gram.
/// @param err Set to MARKOV_OK, or MARKOV_NO_MEM if scratch couldn't hold the
/// prefix.
/// @return The state, or NULL if there is none or memory ran out.
static markov_state_t *prefix_state(const markov_t *m, sof_string_t *scratch, const char *gram, const char **unit, markov_error_t *err) {
    size_t sep = m->counts.unit == NGRAM_WORDS ? 1 : 0;
    size_t offset = unit_offset(m, gram, m->order - 1);
    *unit = gram + offset;
    sof_string_clear(scratch);
    // offset is at most the length of gram plus the space it doesn't have.
    if (sof_string_append_bytes(scratch, gram, offset - sep) != SOF_STRING_OK) {
        *err = MARKOV_NO_MEM;
        return NULL;
    }
    *err = MARKOV_OK;
    const sof_hashmap_element_t *e = sof_hashmap_find(&m->index, sof_string_cstr(scratch));
    return e == NULL ? NULL : e->data;
}

/// @brief Returns where the word or character after the first k of s starts:
/// past k words and the space after each, or past k characters. If s has
/// fewer, it is as if s ended in a space, for words: the length of s plus 1.
static size_t unit_offset(const markov_t *m, const char *s, size_t k) {
    size_t length = strlen(s), position = 0;
    if (m->counts.unit == NGRAM_WORDS) {
        for (size_t i = 0; i < k; i++) {
            const char *space = strchr(s + position, ' ');
            if (space == NULL) {
                return length + 1;
            }
            position = (size_t)(space - s) + 1;
        }
        return position;
    }
    for (size_t i = 0; i < k && sof_utf8_next(s, length, &position, NULL); i++) {
    }
    return position;
}

markov_error_t markov_add(markov_t *m, const char *text, size_t length) {
    if (!is_ready(m)) {
        return MARKOV_ERROR;
    }
    drop_chain(m);
    return from_ngram(ngram_add(&m->counts, text, length));
}

markov_error_t markov_build(markov_t *m) {
    if (!is_ready(m)) {
        return MARKOV_ERROR;
    }
    if (m->index.elements != NULL) {
        return MARKOV_OK;
    }
    size_t contexts = ngram_distinct(&m->counts, m->order - 1);
    size_t grams = ngram_distinct(&m->counts, m->order);
    if (sof_hashmap_init(&m->index, &context_keys, NULL, NULL, contexts, &m->alloc) != SOF_HASHMAP_OK) {
        return MARKOV_NO_MEM;
    }
    sof_string_t scratch;
    if (sof_string_init(&scratch, 64, &m->alloc) != SOF_STRING_OK) {
        drop_chain(m);
        return MARKOV_NO_MEM;
    }
    markov_error_t err = MARKOV_NO_MEM;
    if (contexts > 0) {
        m->states = sof_alloc_calloc(&m->alloc, contexts, sizeof(markov_state_t));
        if (m->states == NULL) {
            goto fail;
        }
        m->state_count = contexts;
    }
    if (grams > 0) {
        m->successors = sof_alloc_calloc(&m->alloc, grams, sizeof(markov_successor_t));
        if (m->successors == NULL) {
            goto fail;
        }
        m->successor_count = grams;
    }
    // A state for each (n - 1)-gram, in the index.
    size_t position = 0, i = 0, count;
    const char *gram;
    while ((gram = ngram_next(&m->counts, m->order - 1, &position, &count)) != NULL) {
        markov_state_t *state = &m->states[i++];
        state->context = gram;
        state->count = count;
        sof_hashmap_element_t *e = sof_hashmap_insert(&m->index, gram, NULL);
        if (e == NULL) {
            goto fail;
        }
        e->data = state;
    }
    // Count each state's successors, give each its part of the array, then
    // fill them in.
    if ((err = count_successors(m, &scratch, 0)) != MARKOV_OK) {
        goto fail;
    }
    size_t offset = 0, starts = 0;
    for (i = 0; i < m->state_count; i++) {
        markov_state_t *state = &m->states[i];
        state->next = state->next_count > 0 ? m->successors + offset : NULL;
        offset += state->next_count;
        starts += state->next_count > 0;
        state->next_count = 0;
    }
    if ((err = count_successors(m, &scratch, 1)) != MARKOV_OK) {
        goto fail;
    }
    err = MARKOV_NO_MEM;
    for (i = 0; i < m->state_count; i++) {
        markov_state_t *state = &m->states[i];
        sof_sort_quick(state->next, state->next_count, sizeof(markov_successor_t), by_unit, NULL);
    }
    if (starts > 0) {
        m->starts = sof_alloc_calloc(&m->alloc, starts, sizeof(markov_state_t *));
        if (m->starts == NULL) {
            goto fail;
        }
        m->start_count = starts;
        size_t j = 0;
        for (i = 0; i < m->state_count; i++) {
            if (m->states[i].next_count > 0) {
                m->starts[j++] = &m->states[i];
                m->start_total += m->states[i].total;
            }
        }
        sof_sort_quick(m->starts, m->start_count, sizeof(markov_state_t *), by_context, NULL);
    }
    sof_string_free(&scratch);
    return MARKOV_OK;
fail:
    sof_string_free(&scratch);
    drop_chain(m);
    return err;
}

markov_error_t markov_clear(markov_t *m) {
    if (!is_ready(m)) {
        return MARKOV_ERROR;
    }
    drop_chain(m);
    return from_ngram(ngram_clear(&m->counts));
}

const markov_state_t *markov_find(const markov_t *m, const char *context) {
    if (!is_ready(m) || context == NULL || m->index.elements == NULL) {
        return NULL;
    }
    const sof_hashmap_element_t *e = sof_hashmap_find(&m->index, context);
    return e == NULL ? NULL : e->data;
}

markov_error_t markov_flush(markov_t *m) {
    if (!is_ready(m)) {
        return MARKOV_ERROR;
    }
    drop_chain(m);
    return from_ngram(ngram_flush(&m->counts));
}

markov_error_t markov_free(markov_t *m) {
    if (m == NULL) {
        return MARKOV_ERROR;
    }
    if (is_ready(m)) {
        drop_chain(m);
        ngram_free(&m->counts);
    }
    memset(m, 0, sizeof(*m));
    return MARKOV_OK;
}

markov_error_t markov_generate(markov_t *m, sof_random_t *r, size_t length, sof_string_t *out) {
    if (!is_ready(m) || r == NULL || !r->ready || out == NULL || out->data == NULL || out->size == 0) {
        return MARKOV_ERROR;
    }
    markov_error_t err = markov_build(m);
    if (err != MARKOV_OK) {
        return err;
    }
    if (length == 0) {
        return MARKOV_OK;
    }
    if (m->start_count == 0) {
        return MARKOV_NO_MATCH;
    }
    int words = m->counts.unit == NGRAM_WORDS;
    size_t sep = words ? 1 : 0;
    // The last n - 1 words or characters generated.
    sof_string_t context;
    if (sof_string_init(&context, 64, &m->alloc) != SOF_STRING_OK) {
        return MARKOV_NO_MEM;
    }
    const markov_state_t *state = NULL;
    size_t done = 0;
    int first = 1;
    err = MARKOV_NO_MEM;
    while (done < length) {
        if (state == NULL) {
            // Start a chain, with as much of a start's context as there is
            // room for.
            state = pick_start(m, r);
            size_t k = m->order - 1;
            if (length - done < k) {
                k = length - done;
            }
            size_t keep = unit_offset(m, state->context, k) - sep;
            if ((!first && sof_string_append_cstr(out, "\n") != SOF_STRING_OK) || sof_string_append_bytes(out, state->context, keep) != SOF_STRING_OK) {
                goto done;
            }
            first = 0;
            done += k;
            sof_string_clear(&context);
            if (sof_string_append_cstr(&context, state->context) != SOF_STRING_OK) {
                goto done;
            }
            continue;
        }
        const markov_successor_t *next = pick_successor(state, r);
        if ((words && sof_string_append_cstr(out, " ") != SOF_STRING_OK) || sof_string_append_cstr(out, next->unit) != SOF_STRING_OK) {
            goto done;
        }
        done++;
        // Move the context on by one: drop its first word or character and
        // add the new one.
        // A context of one word has no space after it.
        size_t drop = unit_offset(m, sof_string_cstr(&context), 1);
        sof_string_delete(&context, 0, drop < context.length ? drop : context.length);
        if ((words && context.length > 0 && sof_string_append_cstr(&context, " ") != SOF_STRING_OK) || sof_string_append_cstr(&context, next->unit) != SOF_STRING_OK) {
            goto done;
        }
        // A context that never had a successor ends the chain.
        state = markov_find(m, sof_string_cstr(&context));
        if (state != NULL && state->next_count == 0) {
            state = NULL;
        }
    }
    err = MARKOV_OK;
done:
    sof_string_free(&context);
    return err;
}

markov_error_t markov_init(markov_t *m, const markov_options_t *options, const sof_alloc_t *alloc) {
    if (m == NULL) {
        return MARKOV_ERROR;
    }
    memset(m, 0, sizeof(*m));
    if (!sof_alloc_valid(alloc)) {
        return MARKOV_ERROR;
    }
    markov_options_t o = {NGRAM_CHARS, 0, NULL, 0};
    if (options != NULL) {
        o = *options;
    }
    size_t order = o.order == 0 ? 2 : o.order;
    if (order < 2) {
        return MARKOV_ERROR;
    }
    const char *chars = o.chars;
    if (chars == NULL) {
        chars = o.unit == NGRAM_WORDS ? MARKOV_WORD_CHARS : MARKOV_CHAR_CHARS;
    }
    ngram_options_t counting = {o.unit, order - 1, order, chars, o.fold_case};
    ngram_error_t err = ngram_init(&m->counts, &counting, alloc);
    if (err != NGRAM_OK) {
        memset(m, 0, sizeof(*m));
        return from_ngram(err);
    }
    if (o.unit == NGRAM_WORDS && ngram_member(&m->counts, ' ') != 0) {
        ngram_free(&m->counts);
        memset(m, 0, sizeof(*m));
        return MARKOV_ERROR;
    }
    m->order = order;
    if (alloc != NULL) {
        m->alloc = *alloc;
    }
    return MARKOV_OK;
}
