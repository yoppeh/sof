/**
 * @file sof_csv_reader.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief The CSV reader of sof_csv.
 * @details A state machine takes the input apart, appending each field's
 * bytes to a sof_string_t in a sof_vector of them. Runs of ordinary bytes,
 * those that change nothing but the field, are found 8 at a time and
 * appended at once; quotes, line breaks and delimiters go through the state
 * machine a byte at a time. A byte is taken only once everything it does
 * has succeeded, so a call that runs out of memory can be made again and
 * goes on from the same byte.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_csv.h"

/// @brief The reader's states: at the start of a field, in a field without
/// quotes, in a quoted field, and just after a quote in a quoted field,
/// which either ends it or is the first of "".
#define STATE_START 0
#define STATE_UNQUOTED 1
#define STATE_QUOTED 2
#define STATE_QUOTE 3

/// @brief What take() returns for a byte that ends the record, which it
/// leaves for next() to take once the record is dealt with. It is not a
/// sof_csv_error_t.
#define TAKE_END (-1)

/// @brief The bytes a field can hold before it first grows.
#define FIELD_CAPACITY 16

/// @brief A 1 in each byte of a word, and the top bit of each byte.
#define ONES 0x0101010101010101u
#define HIGHS 0x8080808080808080u

static sof_csv_error_t append(sof_csv_reader_t *r, char c);
static int compare_name(const void *key, const void *element, void *ctx);
static sof_string_t *field_at(const sof_csv_reader_t *r, size_t index);
static void field_end(sof_csv_reader_t *r);
static sof_csv_error_t field_start(sof_csv_reader_t *r, size_t start);
static void free_string(void *element, void *ctx);
static size_t here(const sof_csv_reader_t *r);
static sof_csv_error_t record_end(sof_csv_reader_t *r, int *header);
static size_t run_length(const sof_csv_reader_t *r);
static sof_csv_error_t run_take(sof_csv_reader_t *r, size_t length);
static sof_csv_span_t *span_at(const sof_csv_reader_t *r, size_t index);
static int take(sof_csv_reader_t *r, char c);
static int usable(const sof_csv_reader_t *r);

/// @brief Appends a byte to the field being read.
static sof_csv_error_t append(sof_csv_reader_t *r, char c) {
    return sof_string_append_char(field_at(r, r->count - 1), c) == SOF_STRING_OK ? SOF_CSV_OK : SOF_CSV_NO_MEM;
}

/// @brief Compares a name, a sof_string_t key, with a header string, for
/// sof_vector_find().
static int compare_name(const void *key, const void *element, void *ctx) {
    (void)ctx;
    return sof_string_cmp(key, element);
}

/// @brief Gives a field of the record being read, as sof_vector_at() would
/// but without the call, which is made for every field; index is known to be
/// in the vector.
static sof_string_t *field_at(const sof_csv_reader_t *r, size_t index) {
    return (sof_string_t *)r->fields.elements + index;
}

/// @brief Ends the span of the field being read at the byte being taken.
static void field_end(sof_csv_reader_t *r) {
    sof_csv_span_t *span = span_at(r, r->count - 1);
    span->length = here(r) - span->offset;
}

/// @brief Starts a field of the record being read, at start in the input,
/// reusing a string and span left from an earlier record if there are.
/// @return SOF_CSV_OK, or SOF_CSV_NO_MEM, leaving the reader as it was but
/// for a span it may have added past count.
static sof_csv_error_t field_start(sof_csv_reader_t *r, size_t start) {
    sof_csv_span_t span = {start, 0};
    if (r->count == r->spans.count) {
        if (sof_vector_push(&r->spans, &span) != SOF_VECTOR_OK) {
            return SOF_CSV_NO_MEM;
        }
    } else {
        *span_at(r, r->count) = span;
    }
    if (r->count == r->fields.count) {
        sof_string_t field;
        if (sof_string_init(&field, FIELD_CAPACITY, &r->alloc) != SOF_STRING_OK) {
            return SOF_CSV_NO_MEM;
        }
        if (sof_vector_push(&r->fields, &field) != SOF_VECTOR_OK) {
            sof_string_free(&field);
            return SOF_CSV_NO_MEM;
        }
    } else {
        sof_string_clear(field_at(r, r->count));
    }
    r->count++;
    r->state = STATE_START;
    return SOF_CSV_OK;
}

/// @brief The delete function of the fields and header vectors: frees a
/// string.
static void free_string(void *element, void *ctx) {
    (void)ctx;
    sof_string_free(element);
}

/// @brief Gives the offset in the input of the byte being taken.
static size_t here(const sof_csv_reader_t *r) {
    return r->consumed + r->position;
}

/// @brief Deals with a whole record: with SOF_CSV_HEADER, the input's first
/// record is copied to the header.
/// @param header Set to 1 if the record was the header, or 0.
/// @return SOF_CSV_OK, or SOF_CSV_NO_MEM, leaving no header.
static sof_csv_error_t record_end(sof_csv_reader_t *r, int *header) {
    *header = (r->flags & SOF_CSV_HEADER) != 0 && r->header.count == 0;
    if (!*header) {
        return SOF_CSV_OK;
    }
    for (size_t i = 0; i < r->count; i++) {
        sof_string_t name;
        if (sof_string_dup(&name, sof_vector_at(&r->fields, i), &r->alloc) != SOF_STRING_OK) {
            sof_vector_clear(&r->header);
            return SOF_CSV_NO_MEM;
        }
        if (sof_vector_push(&r->header, &name) != SOF_VECTOR_OK) {
            sof_string_free(&name);
            sof_vector_clear(&r->header);
            return SOF_CSV_NO_MEM;
        }
    }
    return SOF_CSV_OK;
}

/// @brief Counts the ordinary bytes in the buffer from the position: those
/// that are not a quote, a CR or an LF, nor the delimiter outside quotes.
/// @details The first 8 bytes a byte at a time, which is quicker for short
/// fields; then 8 bytes at a time while none of them is special, then a
/// byte at a time up to the first that is. A word has a byte equal to b if
/// x ^ (b * ONES) has a zero byte, which (v - ONES) & ~v & HIGHS shows
/// without false alarms.
static size_t run_length(const sof_csv_reader_t *r) {
    const char *start = r->buffer + r->position, *end = r->buffer + r->length, *s = start;
    // In a quoted field the delimiter is ordinary, so the quote stands in
    // for it.
    char delimiter = r->state == STATE_QUOTED ? '"' : r->delimiter;
    uint64_t quotes = ONES * (unsigned char)'"', crs = ONES * (unsigned char)'\r', lfs = ONES * (unsigned char)'\n';
    uint64_t delimiters = ONES * (unsigned char)delimiter;
    for (const char *first = end - s > 8 ? s + 8 : end; s < first; s++) {
        if (*s == '"' || *s == '\r' || *s == '\n' || *s == delimiter) {
            return (size_t)(s - start);
        }
    }
    while (end - s >= 8) {
        uint64_t x, a, b, c, d;
        memcpy(&x, s, 8);
        a = x ^ quotes;
        b = x ^ crs;
        c = x ^ lfs;
        d = x ^ delimiters;
        if ((((a - ONES) & ~a) | ((b - ONES) & ~b) | ((c - ONES) & ~c) | ((d - ONES) & ~d)) & HIGHS) {
            break;
        }
        s += 8;
    }
    while (s < end && *s != '"' && *s != '\r' && *s != '\n' && *s != delimiter) {
        s++;
    }
    return (size_t)(s - start);
}

/// @brief Takes a run of length ordinary bytes from the position, which
/// run_length() found, in a record and not just after a quote: they are
/// appended to the field at once, as take() would one by one.
/// @return SOF_CSV_OK, or SOF_CSV_NO_MEM, having taken nothing.
static sof_csv_error_t run_take(sof_csv_reader_t *r, size_t length) {
    if (sof_string_append_bytes(field_at(r, r->count - 1), r->buffer + r->position, length) != SOF_STRING_OK) {
        return SOF_CSV_NO_MEM;
    }
    if (r->state == STATE_START) {
        r->state = STATE_UNQUOTED;
    }
    r->after_cr = 0;
    r->position += length;
    return SOF_CSV_OK;
}

/// @brief Gives a span of the record being read, as field_at() gives a
/// field.
static sof_csv_span_t *span_at(const sof_csv_reader_t *r, size_t index) {
    return (sof_csv_span_t *)r->spans.elements + index;
}

/// @brief Takes a byte of the input.
/// @details A byte that ends a record isn't taken: TAKE_END is returned,
/// and next() takes it once the record is dealt with. Otherwise the byte is
/// taken only if everything it does succeeds.
/// @return SOF_CSV_OK, SOF_CSV_NO_MEM, SOF_CSV_SYNTAX, or TAKE_END.
static int take(sof_csv_reader_t *r, char c) {
    int newline = c == '\n' || c == '\r';
    sof_csv_error_t rc = SOF_CSV_OK;
    if (c == '\n' && r->after_cr && r->state != STATE_QUOTED) {
        // The LF of a CRLF whose CR ended a record or a blank line.
        r->after_cr = 0;
        return SOF_CSV_OK;
    }
    if (!r->in_record) {
        if (newline && (r->flags & SOF_CSV_STRICT) == 0) {
            // A blank line, which is skipped.
            r->line++;
            r->after_cr = c == '\r';
            return SOF_CSV_OK;
        }
        r->count = 0;
        if (field_start(r, here(r)) != SOF_CSV_OK) {
            return SOF_CSV_NO_MEM;
        }
        r->in_record = 1;
        r->record_line = r->line;
        r->record.offset = here(r);
        if (newline) {
            // A blank line, which RFC 4180's grammar makes a record of one
            // empty field.
            return TAKE_END;
        }
    }
    switch (r->state) {
    case STATE_START:
        if (c == '"') {
            r->state = STATE_QUOTED;
        } else if (c == r->delimiter) {
            field_end(r);
            rc = field_start(r, here(r) + 1);
        } else if (newline) {
            return TAKE_END;
        } else if ((rc = append(r, c)) == SOF_CSV_OK) {
            r->state = STATE_UNQUOTED;
        }
        break;
    case STATE_UNQUOTED:
        if (c == r->delimiter) {
            field_end(r);
            rc = field_start(r, here(r) + 1);
        } else if (newline) {
            return TAKE_END;
        } else if (c == '"' && (r->flags & SOF_CSV_STRICT) != 0) {
            return SOF_CSV_SYNTAX;
        } else {
            rc = append(r, c);
        }
        break;
    case STATE_QUOTED:
        if (c == '"') {
            r->state = STATE_QUOTE;
        } else if ((rc = append(r, c)) == SOF_CSV_OK && (c == '\r' || (c == '\n' && !r->after_cr))) {
            r->line++;
        }
        break;
    default:
        // Just after a quote in a quoted field.
        if (c == '"') {
            if ((rc = append(r, c)) == SOF_CSV_OK) {
                r->state = STATE_QUOTED;
            }
        } else if (c == r->delimiter) {
            field_end(r);
            rc = field_start(r, here(r) + 1);
        } else if (newline) {
            return TAKE_END;
        } else if ((r->flags & SOF_CSV_STRICT) != 0) {
            return SOF_CSV_SYNTAX;
        } else if ((rc = append(r, c)) == SOF_CSV_OK) {
            r->state = STATE_UNQUOTED;
        }
        break;
    }
    if (rc == SOF_CSV_OK) {
        r->after_cr = c == '\r';
    }
    return rc;
}

/// @brief Says whether r is a reader that can be used: not NULL, and
/// initialized.
static int usable(const sof_csv_reader_t *r) {
    return r != NULL && r->ready;
}

int sof_csv_read_file(char *buffer, size_t size, size_t *length, void *ctx) {
    FILE *f = ctx;
    *length = 0;
    if (f == NULL) {
        return 1;
    }
    *length = fread(buffer, 1, size, f);
    // Bytes read before an error are passed on; the error comes with the
    // next call, which reads none.
    return *length == 0 && ferror(f) ? 1 : 0;
}

sof_csv_error_t sof_csv_reader_clear(sof_csv_reader_t *r) {
    if (!usable(r)) {
        return SOF_CSV_ERROR;
    }
    sof_vector_clear(&r->header);
    r->count = 0;
    r->record.offset = 0;
    r->record.length = 0;
    r->consumed = 0;
    r->returned = 0;
    r->length = 0;
    r->position = 0;
    r->state = STATE_START;
    r->in_record = 0;
    r->after_cr = 0;
    r->ended = 0;
    r->unterminated = 0;
    r->line = 1;
    r->record_line = 0;
    r->records = 0;
    return SOF_CSV_OK;
}

sof_csv_error_t sof_csv_reader_column(const sof_csv_reader_t *r, const char *name, size_t *index) {
    if (!usable(r) || name == NULL) {
        return SOF_CSV_ERROR;
    }
    // A header has a few names, looked up once each, so a linear search
    // does, without a sof_hashmap.
    sof_string_t key = sof_string_view(name, strlen(name));
    return sof_vector_find(&r->header, &key, compare_name, NULL, index) != NULL ? SOF_CSV_OK : SOF_CSV_NO_MATCH;
}

sof_csv_error_t sof_csv_reader_free(sof_csv_reader_t *r) {
    if (r == NULL) {
        return SOF_CSV_ERROR;
    }
    if (r->ready) {
        sof_vector_free(&r->fields);
        sof_vector_free(&r->spans);
        sof_vector_free(&r->header);
        sof_alloc_free(&r->alloc, r->buffer, SOF_CSV_READ_SIZE);
    }
    memset(r, 0, sizeof(*r));
    return SOF_CSV_OK;
}

const sof_string_t *sof_csv_reader_header(const sof_csv_reader_t *r, size_t *count) {
    const sof_string_t *names = usable(r) && r->header.count > 0 ? (const sof_string_t *)r->header.elements : NULL;
    if (count != NULL) {
        *count = names != NULL ? r->header.count : 0;
    }
    return names;
}

sof_csv_error_t sof_csv_reader_init(sof_csv_reader_t *r, char delimiter, unsigned flags, sof_csv_read_func_t read, void *ctx, const sof_alloc_t *alloc) {
    if (r == NULL) {
        return SOF_CSV_ERROR;
    }
    memset(r, 0, sizeof(*r));
    if (read == NULL || delimiter == '"' || delimiter == '\r' || delimiter == '\n' || !sof_alloc_valid(alloc)) {
        return SOF_CSV_ERROR;
    }
    if (alloc != NULL) {
        r->alloc = *alloc;
    }
    // The vectors allocate nothing until their first push, so only the
    // buffer can fail.
    sof_vector_init(&r->fields, sizeof(sof_string_t), 0, free_string, NULL, alloc);
    sof_vector_init(&r->spans, sizeof(sof_csv_span_t), 0, NULL, NULL, alloc);
    sof_vector_init(&r->header, sizeof(sof_string_t), 0, free_string, NULL, alloc);
    r->buffer = sof_alloc_malloc(alloc, SOF_CSV_READ_SIZE);
    if (r->buffer == NULL) {
        sof_vector_free(&r->fields);
        sof_vector_free(&r->spans);
        sof_vector_free(&r->header);
        memset(r, 0, sizeof(*r));
        return SOF_CSV_NO_MEM;
    }
    r->delimiter = delimiter;
    r->flags = flags;
    r->read = read;
    r->ctx = ctx;
    r->line = 1;
    r->ready = 1;
    return SOF_CSV_OK;
}

sof_csv_error_t sof_csv_reader_next(sof_csv_reader_t *r, sof_string_t **fields, size_t *count) {
    if (fields != NULL) {
        *fields = NULL;
    }
    if (count != NULL) {
        *count = 0;
    }
    if (!usable(r)) {
        return SOF_CSV_ERROR;
    }
    r->returned = 0;
    for (;;) {
        int header;
        if (r->position == r->length && !r->ended) {
            size_t n = 0;
            if (r->read(r->buffer, SOF_CSV_READ_SIZE, &n, r->ctx) != 0 || n > SOF_CSV_READ_SIZE) {
                return SOF_CSV_READ;
            }
            r->consumed += r->length;
            r->position = 0;
            r->length = n;
            r->ended = n == 0;
            continue;
        }
        if (r->position == r->length) {
            // The end of the input, which ends a record in progress.
            if (r->state == STATE_QUOTED) {
                if ((r->flags & SOF_CSV_STRICT) != 0) {
                    return SOF_CSV_SYNTAX;
                }
                r->unterminated = 1;
            }
            if (!r->in_record) {
                return SOF_CSV_NO_MATCH;
            }
            field_end(r);
            r->record.length = here(r) - r->record.offset;
            if (record_end(r, &header) != SOF_CSV_OK) {
                return SOF_CSV_NO_MEM;
            }
        } else {
            // Just after a quote, an ordinary byte is an error in a strict
            // reader, so take() deals with it.
            if (r->in_record && r->state != STATE_QUOTE) {
                size_t run = run_length(r);
                if (run > 0) {
                    if (run_take(r, run) != SOF_CSV_OK) {
                        return SOF_CSV_NO_MEM;
                    }
                    continue;
                }
            }
            char c = r->buffer[r->position];
            int rc = take(r, c);
            if (rc != TAKE_END) {
                if (rc != SOF_CSV_OK) {
                    return (sof_csv_error_t)rc;
                }
                r->position++;
                continue;
            }
            field_end(r);
            r->record.length = here(r) - r->record.offset;
            if (record_end(r, &header) != SOF_CSV_OK) {
                return SOF_CSV_NO_MEM;
            }
            // Now the line break that ended the record is taken.
            r->line++;
            r->after_cr = c == '\r';
            r->position++;
        }
        r->in_record = 0;
        r->state = STATE_START;
        if (!header) {
            break;
        }
    }
    r->records++;
    r->returned = 1;
    if (fields != NULL) {
        *fields = (sof_string_t *)r->fields.elements;
    }
    if (count != NULL) {
        *count = r->count;
    }
    return SOF_CSV_OK;
}

const sof_csv_span_t *sof_csv_reader_spans(const sof_csv_reader_t *r, size_t *count, sof_csv_span_t *record) {
    int ok = usable(r) && r->returned;
    if (count != NULL) {
        *count = ok ? r->count : 0;
    }
    if (record != NULL) {
        record->offset = ok ? r->record.offset : 0;
        record->length = ok ? r->record.length : 0;
    }
    return ok ? (const sof_csv_span_t *)r->spans.elements : NULL;
}
