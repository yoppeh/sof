/**
 * @file sof_csv_writer.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief The CSV writer of sof_csv.
 * @details Every function that writes notes where the buffer ended before
 * it began, and cuts the buffer back to there if anything fails, so a
 * failed call writes nothing.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_csv.h"

/// @brief The bytes the buffer can hold when it is first allocated.
#define OUT_CAPACITY 256

/// @brief A 1 in each byte of a word, and the top bit of each byte.
#define ONES 0x0101010101010101u
#define HIGHS 0x8080808080808080u

static sof_csv_error_t append(sof_csv_writer_t *w, const void *bytes, size_t length);
static sof_csv_error_t append_char(sof_csv_writer_t *w, char c);
static sof_csv_error_t field_write(sof_csv_writer_t *w, const char *s, size_t length);
static int needs_quotes(const sof_csv_writer_t *w, const char *s, size_t length);
static void rollback(sof_csv_writer_t *w, size_t mark);
static int usable(const sof_csv_writer_t *w);

/// @brief Appends bytes to the buffer.
/// @return SOF_CSV_OK, or SOF_CSV_NO_MEM, leaving the buffer as it was.
static sof_csv_error_t append(sof_csv_writer_t *w, const void *bytes, size_t length) {
    return sof_string_append_bytes(&w->out, bytes, length) == SOF_STRING_OK ? SOF_CSV_OK : SOF_CSV_NO_MEM;
}

/// @brief Appends a byte to the buffer, as append() does.
static sof_csv_error_t append_char(sof_csv_writer_t *w, char c) {
    return sof_string_append_char(&w->out, c) == SOF_STRING_OK ? SOF_CSV_OK : SOF_CSV_NO_MEM;
}

/// @brief Writes a field, after the checks of the functions that do, with
/// the delimiter before it unless it is the record's first.
/// @return SOF_CSV_OK, or SOF_CSV_NO_MEM, having written nothing.
static sof_csv_error_t field_write(sof_csv_writer_t *w, const char *s, size_t length) {
    size_t mark = w->out.length;
    sof_csv_error_t rc = SOF_CSV_OK;
    if (w->fields > 0) {
        rc = append_char(w, w->delimiter);
    }
    if (rc == SOF_CSV_OK && needs_quotes(w, s, length)) {
        rc = append_char(w, '"');
        // Each quote is written twice: the run up to and including it, then
        // the quote again.
        const char *run = s, *end = s + length;
        while (rc == SOF_CSV_OK) {
            const char *quote = run < end ? memchr(run, '"', (size_t)(end - run)) : NULL;
            if (quote == NULL) {
                break;
            }
            rc = append(w, run, (size_t)(quote + 1 - run));
            if (rc == SOF_CSV_OK) {
                rc = append_char(w, '"');
            }
            run = quote + 1;
        }
        if (rc == SOF_CSV_OK) {
            rc = append(w, run, (size_t)(end - run));
        }
        if (rc == SOF_CSV_OK) {
            rc = append_char(w, '"');
        }
    } else if (rc == SOF_CSV_OK) {
        rc = append(w, s, length);
    }
    if (rc != SOF_CSV_OK) {
        rollback(w, mark);
        return rc;
    }
    w->fields++;
    w->record_length += w->out.length - mark;
    return SOF_CSV_OK;
}

/// @brief Says whether a field must be quoted: with SOF_CSV_QUOTE_ALL, or
/// if it holds the delimiter, a quote, a CR or an LF.
/// @details Eight bytes at a time, then a byte at a time, as the reader's
/// run_length() looks (a few lines, kept in each file rather than shared
/// through an internal header).
static int needs_quotes(const sof_csv_writer_t *w, const char *s, size_t length) {
    if ((w->flags & SOF_CSV_QUOTE_ALL) != 0) {
        return 1;
    }
    const char *end = s + length;
    char delimiter = w->delimiter;
    uint64_t quotes = ONES * (unsigned char)'"', crs = ONES * (unsigned char)'\r', lfs = ONES * (unsigned char)'\n';
    uint64_t delimiters = ONES * (unsigned char)delimiter;
    while (end - s >= 8) {
        uint64_t x, a, b, c, d;
        memcpy(&x, s, 8);
        a = x ^ quotes;
        b = x ^ crs;
        c = x ^ lfs;
        d = x ^ delimiters;
        if ((((a - ONES) & ~a) | ((b - ONES) & ~b) | ((c - ONES) & ~c) | ((d - ONES) & ~d)) & HIGHS) {
            return 1;
        }
        s += 8;
    }
    for (; s < end; s++) {
        if (*s == delimiter || *s == '"' || *s == '\r' || *s == '\n') {
            return 1;
        }
    }
    return 0;
}

/// @brief Cuts the buffer back to mark bytes, undoing a failed call.
static void rollback(sof_csv_writer_t *w, size_t mark) {
    sof_string_delete(&w->out, mark, w->out.length - mark);
}

/// @brief Says whether w is a writer that can be used: not NULL, and
/// initialized.
static int usable(const sof_csv_writer_t *w) {
    return w != NULL && w->ready;
}

int sof_csv_write_file(const char *data, size_t length, void *ctx) {
    FILE *f = ctx;
    if (f == NULL) {
        return 1;
    }
    return fwrite(data, 1, length, f) == length ? 0 : 1;
}

sof_csv_error_t sof_csv_writer_clear(sof_csv_writer_t *w) {
    if (!usable(w)) {
        return SOF_CSV_ERROR;
    }
    sof_string_clear(&w->out);
    w->fields = 0;
    w->record_length = 0;
    w->records = 0;
    w->written = 0;
    return SOF_CSV_OK;
}

sof_csv_error_t sof_csv_writer_field(sof_csv_writer_t *w, const char *s) {
    if (!usable(w) || s == NULL) {
        return SOF_CSV_ERROR;
    }
    return field_write(w, s, strlen(s));
}

sof_csv_error_t sof_csv_writer_field_bytes(sof_csv_writer_t *w, const void *s, size_t length) {
    if (!usable(w) || (s == NULL && length != 0)) {
        return SOF_CSV_ERROR;
    }
    return field_write(w, s, length);
}

sof_csv_error_t sof_csv_writer_flush(sof_csv_writer_t *w) {
    if (!usable(w)) {
        return SOF_CSV_ERROR;
    }
    if (w->write == NULL || w->out.length == 0) {
        return SOF_CSV_OK;
    }
    if (w->write(w->out.data, w->out.length, w->ctx) != 0) {
        return SOF_CSV_WRITE;
    }
    w->written += w->out.length;
    sof_string_clear(&w->out);
    return SOF_CSV_OK;
}

sof_csv_error_t sof_csv_writer_free(sof_csv_writer_t *w) {
    if (w == NULL) {
        return SOF_CSV_ERROR;
    }
    if (w->ready) {
        sof_string_free(&w->out);
    }
    memset(w, 0, sizeof(*w));
    return SOF_CSV_OK;
}

sof_csv_error_t sof_csv_writer_init(sof_csv_writer_t *w, char delimiter, unsigned flags, sof_csv_write_func_t write, void *ctx, const sof_alloc_t *alloc) {
    if (w == NULL) {
        return SOF_CSV_ERROR;
    }
    memset(w, 0, sizeof(*w));
    if (delimiter == '"' || delimiter == '\r' || delimiter == '\n' || !sof_alloc_valid(alloc)) {
        return SOF_CSV_ERROR;
    }
    if (sof_string_init(&w->out, OUT_CAPACITY, alloc) != SOF_STRING_OK) {
        memset(w, 0, sizeof(*w));
        return SOF_CSV_NO_MEM;
    }
    w->delimiter = delimiter;
    w->flags = flags;
    w->write = write;
    w->ctx = ctx;
    w->ready = 1;
    return SOF_CSV_OK;
}

const char *sof_csv_writer_output(const sof_csv_writer_t *w, size_t *length) {
    if (length != NULL) {
        *length = usable(w) ? w->out.length : 0;
    }
    return usable(w) ? w->out.data : NULL;
}

sof_csv_error_t sof_csv_writer_record(sof_csv_writer_t *w, const sof_string_t *fields, size_t count) {
    if (!usable(w) || (fields == NULL && count != 0)) {
        return SOF_CSV_ERROR;
    }
    size_t mark = w->out.length;
    size_t saved_fields = w->fields;
    size_t saved_length = w->record_length;
    sof_csv_error_t rc = SOF_CSV_OK;
    for (size_t i = 0; rc == SOF_CSV_OK && i < count; i++) {
        // A dead string has a NULL data and a length of 0: an empty field.
        rc = field_write(w, fields[i].data, fields[i].length);
    }
    if (rc == SOF_CSV_OK) {
        rc = sof_csv_writer_record_end(w);
    }
    if (rc != SOF_CSV_OK && rc != SOF_CSV_WRITE) {
        rollback(w, mark);
        w->fields = saved_fields;
        w->record_length = saved_length;
    }
    return rc;
}

sof_csv_error_t sof_csv_writer_record_end(sof_csv_writer_t *w) {
    if (!usable(w) || w->fields == 0) {
        return SOF_CSV_ERROR;
    }
    size_t mark = w->out.length;
    sof_csv_error_t rc = SOF_CSV_OK;
    if (w->fields == 1 && w->record_length == 0) {
        // One empty field, which written as nothing would be a blank line.
        rc = append(w, "\"\"", 2);
    }
    if (rc == SOF_CSV_OK) {
        rc = (w->flags & SOF_CSV_CRLF) != 0 ? append(w, "\r\n", 2) : append_char(w, '\n');
    }
    if (rc != SOF_CSV_OK) {
        rollback(w, mark);
        return rc;
    }
    w->fields = 0;
    w->record_length = 0;
    w->records++;
    if (w->write != NULL && w->out.length >= SOF_CSV_FLUSH_SIZE) {
        return sof_csv_writer_flush(w);
    }
    return SOF_CSV_OK;
}
