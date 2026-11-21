/**
 * @file match.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Line-by-line regular expression search in c, for the match program.
 * @details Text is gathered into a sof_string_t a line at a time, and each
 * line is searched with sof_regex_find(), whose PCRE2 expression is
 * compiled by sof_regex_init() with the searcher's allocator.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stddef.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_regex.h"
#include "sof_string.h"
#include "match.h"

/// @brief The bytes a line can hold before it first grows.
#define LINE_CAPACITY 256

static match_error_t from_regex(sof_regex_error_t err);
static match_error_t from_string(sof_string_error_t err);
static match_error_t search_line(match_t *m);

/// @brief Turns a sof_regex error code into the matching match_error_t.
static match_error_t from_regex(sof_regex_error_t err) {
    switch (err) {
    case SOF_REGEX_OK:
        return MATCH_OK;
    case SOF_REGEX_NO_MEM:
        return MATCH_NO_MEM;
    case SOF_REGEX_NO_MATCH:
        return MATCH_NO_MATCH;
    case SOF_REGEX_BAD_PATTERN:
        return MATCH_BAD_REGEX;
    case SOF_REGEX_MATCH_ERROR:
        return MATCH_MATCH_ERROR;
    default:
        return MATCH_ERROR;
    }
}

/// @brief Turns a sof_string error code into the matching match_error_t.
static match_error_t from_string(sof_string_error_t err) {
    switch (err) {
    case SOF_STRING_OK:
        return MATCH_OK;
    case SOF_STRING_NO_MEM:
        return MATCH_NO_MEM;
    case SOF_STRING_NO_MATCH:
        return MATCH_NO_MATCH;
    default:
        return MATCH_ERROR;
    }
}

/// @brief Searches the line in m->line, calls m->found for it or its matches
/// if it is selected, and empties it.
static match_error_t search_line(match_t *m) {
    m->lines++;
    size_t position = 0, offset = 0, length = 0;
    sof_regex_error_t err = sof_regex_find(&m->regex, &m->line, &position, &offset, &length);
    if (err != SOF_REGEX_OK && err != SOF_REGEX_NO_MATCH) {
        sof_string_clear(&m->line);
        return from_regex(err);
    }
    int matched = err == SOF_REGEX_OK;
    if (matched != !m->options.invert) {
        if (m->options.all_lines && m->found != NULL) {
            m->found(m->line.data, m->line.length, m->lines, 0, m->ctx);
        }
        sof_string_clear(&m->line);
        return MATCH_OK;
    }
    m->selected++;
    if (!m->options.only_matching) {
        if (m->found != NULL) {
            m->found(m->line.data, m->line.length, m->lines, 1, m->ctx);
        }
    } else if (matched) {
        // Empty matches are skipped, as grep -o skips them; sof_regex_find()
        // moves past each one, so the loop ends.
        while (err == SOF_REGEX_OK) {
            if (length != 0) {
                m->matches++;
                if (m->found != NULL) {
                    m->found(m->line.data + offset, length, m->lines, 1, m->ctx);
                }
            }
            err = sof_regex_find(&m->regex, &m->line, &position, &offset, &length);
        }
        if (err != SOF_REGEX_NO_MATCH) {
            sof_string_clear(&m->line);
            return from_regex(err);
        }
    }
    sof_string_clear(&m->line);
    return MATCH_OK;
}

match_error_t match_add(match_t *m, const char *text, size_t length) {
    if (m == NULL || m->line.data == NULL) {
        return MATCH_ERROR;
    }
    if (text == NULL) {
        return MATCH_OK;
    }
    const char *end = text + length;
    while (text < end) {
        const char *newline = memchr(text, '\n', (size_t)(end - text));
        size_t piece = (size_t)((newline != NULL ? newline : end) - text);
        if (piece != 0) {
            sof_string_error_t err = sof_string_append_bytes(&m->line, text, piece);
            if (err != SOF_STRING_OK) {
                sof_string_clear(&m->line);
                return from_string(err);
            }
        }
        if (newline == NULL) {
            break;
        }
        match_error_t err = search_line(m);
        if (err != MATCH_OK) {
            return err;
        }
        text = newline + 1;
    }
    return MATCH_OK;
}

match_error_t match_clear(match_t *m) {
    if (m == NULL || m->line.data == NULL) {
        return MATCH_ERROR;
    }
    sof_string_clear(&m->line);
    m->lines = 0;
    m->selected = 0;
    m->matches = 0;
    return MATCH_OK;
}

match_error_t match_flush(match_t *m) {
    if (m == NULL || m->line.data == NULL) {
        return MATCH_ERROR;
    }
    if (m->line.length == 0) {
        return MATCH_OK;
    }
    return search_line(m);
}

match_error_t match_free(match_t *m) {
    if (m == NULL) {
        return MATCH_ERROR;
    }
    sof_regex_free(&m->regex);
    sof_string_free(&m->line);
    memset(m, 0, sizeof(*m));
    return MATCH_OK;
}

match_error_t match_init(match_t *m, const char *pattern, const match_options_t *options, match_found_func_t found, void *ctx, const sof_alloc_t *alloc) {
    if (m == NULL) {
        return MATCH_ERROR;
    }
    memset(m, 0, sizeof(*m));
    if (pattern == NULL || !sof_alloc_valid(alloc)) {
        return MATCH_ERROR;
    }
    if (options != NULL) {
        m->options = *options;
    }
    if (alloc != NULL) {
        m->alloc = *alloc;
    }
    match_error_t err;
    if (m->options.ignore_case) {
        sof_string_t full;
        sof_string_error_t string_err = sof_string_from_cstr(&full, "(?i)", &m->alloc);
        if (string_err == SOF_STRING_OK) {
            string_err = sof_string_append_cstr(&full, pattern);
        }
        err = from_string(string_err);
        if (err == MATCH_OK) {
            err = from_regex(sof_regex_init(&m->regex, sof_string_cstr(&full), &m->alloc));
        }
        sof_string_free(&full);
    } else {
        err = from_regex(sof_regex_init(&m->regex, pattern, &m->alloc));
    }
    if (err == MATCH_OK) {
        err = from_string(sof_string_init(&m->line, LINE_CAPACITY, &m->alloc));
    }
    if (err != MATCH_OK) {
        sof_regex_free(&m->regex);
        sof_string_free(&m->line);
        memset(m, 0, sizeof(*m));
        return err;
    }
    m->found = found;
    m->ctx = ctx;
    return MATCH_OK;
}
