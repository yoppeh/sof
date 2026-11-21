/**
 * @file sof_log.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Leveled logging to a stream in c.
 * @details Compile with SOF_LOG_THREADED defined to write lines from a thread
 * of each log's own; see sof_log.h.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

// flockfile(), getpid() and threads are POSIX; ask for them in
// case the compiler is in strict c mode.
#if (defined(__unix__) || defined(__APPLE__)) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(__unix__) || defined(__APPLE__)
#define SOF_LOG_POSIX 1
#include <unistd.h>
#endif

#ifdef SOF_LOG_THREADED
#ifndef SOF_LOG_POSIX
#error "SOF_LOG_THREADED needs POSIX threads"
#endif
#include <pthread.h>
#endif

#include "sof_alloc.h"
#include "sof_log.h"

/// @brief The names of the levels, as written in lines.
static const char *const LEVEL_NAMES[] = {"ERROR", "WARN", "INFO", "DEBUG", "TRACE"};

/// @brief The size of the buffer a line is formatted into on the stack. A
/// longer line is written in parts (without SOF_LOG_THREADED) or formatted a
/// second time into memory allocated for it (with SOF_LOG_THREADED).
#define LINE_BUFFER 512

/// @brief What goes at the start of a line, gathered once so that a line
/// formatted twice (to measure it, then to fill it) comes out the same.
typedef struct line_head_t {
    /// @brief The time, in UTC: the year, month (1-12), day of the month
    /// (1-31), hour, minute and second.
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    long pid;
    const char *name;
    const char *file;
    int line;
    const char *level;
} line_head_t;

/// @brief Where a line is formatted: into stream if it isn't NULL, or else
/// into buffer, of size bytes.
typedef struct sink_t {
    FILE *stream;
    char *buffer;
    size_t size;
    /// @brief The characters formatted so far, whether or not they fit.
    size_t length;
    /// @brief Set if something didn't fit in buffer; what did is kept, and
    /// length goes on counting.
    int overflow;
    /// @brief Set if a write or a format failed.
    int failed;
} sink_t;

#ifdef SOF_LOG_THREADED

/// @brief A formatted line waiting for the writer thread.
typedef struct sof_log_message_t {
    struct sof_log_message_t *next;
    /// @brief The length of text, without its NUL.
    size_t length;
    char text[];
} sof_log_message_t;

/// @brief The writer thread and its queue. Everything but thread, stream and
/// alloc is guarded by mutex.
struct sof_log_writer_t {
    pthread_t thread;
    pthread_mutex_t mutex;
    /// @brief Signaled when a line is queued or stop is set.
    pthread_cond_t work;
    /// @brief Broadcast when the queue is empty and nothing is being written.
    pthread_cond_t idle;
    sof_log_message_t *head;
    sof_log_message_t *tail;
    /// @brief Set while the thread writes a line it has taken off the queue.
    int writing;
    /// @brief Set by sof_log_free() to end the thread once the queue is empty.
    int stop;
    /// @brief Set when a write fails; cleared by sof_log_flush().
    int failed;
    FILE *stream;
    sof_alloc_t alloc;
};

static size_t message_size(size_t length);
static sof_log_error_t writer_start(sof_log_t *log, FILE *stream);
static void writer_stop(sof_log_t *log);
static void *writer_main(void *arg);
static sof_log_error_t write_queued(const sof_log_t *log, const line_head_t *head, const char *format, va_list args);

#else

static void lock_stream(FILE *stream);
static void unlock_stream(FILE *stream);
static sof_log_error_t write_direct(const sof_log_t *log, const line_head_t *head, const char *format, va_list args);

#endif

static void format_line(sink_t *sink, const line_head_t *head, const char *format, va_list args);
static void head_time(line_head_t *head, time_t now);
static int level_valid(sof_log_level_t level);
static size_t put_decimal(char *to, long value);
static void put_two(char *to, int value);
static void sink_bytes(sink_t *sink, const char *bytes, size_t length);
static void sink_printf(sink_t *sink, const char *format, ...) SOF_LOG_PRINTF_FORMAT(2, 3);
static void sink_vprintf(sink_t *sink, const char *format, va_list args);

sof_log_error_t sof_log_flush(const sof_log_t *log) {
    if (log == NULL || log->stream == NULL) {
        return SOF_LOG_ERROR;
    }
#ifdef SOF_LOG_THREADED
    struct sof_log_writer_t *writer = log->writer;
    pthread_mutex_lock(&writer->mutex);
    while (writer->head != NULL || writer->writing) {
        pthread_cond_wait(&writer->idle, &writer->mutex);
    }
    int failed = writer->failed;
    writer->failed = 0;
    pthread_mutex_unlock(&writer->mutex);
    if (failed) {
        return SOF_LOG_ERROR;
    }
#endif
    return fflush(log->stream) == 0 ? SOF_LOG_OK : SOF_LOG_ERROR;
}

sof_log_error_t sof_log_free(sof_log_t *log) {
    if (log == NULL) {
        return SOF_LOG_ERROR;
    }
    if (log->stream != NULL) {
#ifdef SOF_LOG_THREADED
        writer_stop(log);
#endif
        fflush(log->stream);
    }
    if (log->name != NULL) {
        sof_alloc_free(&log->alloc, log->name, strlen(log->name) + 1);
    }
    memset(log, 0, sizeof(*log));
    return SOF_LOG_OK;
}

sof_log_error_t sof_log_init(sof_log_t *log, FILE *stream, sof_log_level_t level, const char *name, const sof_alloc_t *alloc) {
    if (log == NULL) {
        return SOF_LOG_ERROR;
    }
    memset(log, 0, sizeof(*log));
    if (stream == NULL || !level_valid(level) || !sof_alloc_valid(alloc)) {
        return SOF_LOG_ERROR;
    }
    if (alloc != NULL) {
        log->alloc = *alloc;
    }
    if (name != NULL && name[0] != '\0') {
        log->name = sof_alloc_strndup(&log->alloc, name, strlen(name));
        if (log->name == NULL) {
            memset(log, 0, sizeof(*log));
            return SOF_LOG_NO_MEM;
        }
    }
    log->level = level;
#ifdef SOF_LOG_THREADED
    sof_log_error_t rc = writer_start(log, stream);
    if (rc != SOF_LOG_OK) {
        sof_log_free(log);
        return rc;
    }
#endif
    log->stream = stream;
    return SOF_LOG_OK;
}

const char *sof_log_level_name(sof_log_level_t level) {
    return level_valid(level) ? LEVEL_NAMES[level] : NULL;
}

sof_log_error_t sof_log_level_parse(const char *name, sof_log_level_t *level) {
    if (name == NULL || level == NULL) {
        return SOF_LOG_ERROR;
    }
    for (int i = SOF_LOG_LEVEL_ERROR; i <= SOF_LOG_LEVEL_TRACE + 1; i++) {
        const char *candidate = i <= SOF_LOG_LEVEL_TRACE ? LEVEL_NAMES[i] : "ALL";
        size_t j = 0;
        while (name[j] != '\0' && toupper((unsigned char)name[j]) == candidate[j]) {
            j++;
        }
        if (name[j] == '\0' && candidate[j] == '\0') {
            *level = i <= SOF_LOG_LEVEL_TRACE ? (sof_log_level_t)i : SOF_LOG_LEVEL_ALL;
            return SOF_LOG_OK;
        }
    }
    return SOF_LOG_NO_MATCH;
}

sof_log_error_t sof_log_set_level(sof_log_t *log, sof_log_level_t level) {
    if (log == NULL || log->stream == NULL || !level_valid(level)) {
        return SOF_LOG_ERROR;
    }
    log->level = level;
    return SOF_LOG_OK;
}

sof_log_error_t sof_log_write(const sof_log_t *log, sof_log_level_t level, const char *file, int line, const char *format, ...) {
    if (log == NULL || log->stream == NULL || format == NULL || !level_valid(level)) {
        return SOF_LOG_ERROR;
    }
    if (level > log->level) {
        return SOF_LOG_OK;
    }
    line_head_t head = {0};
    head_time(&head, time(NULL));
#ifdef SOF_LOG_POSIX
    head.pid = (long)getpid();
#endif
    head.name = log->name;
    head.file = file;
    head.line = line;
    head.level = LEVEL_NAMES[level];
    va_list args;
    va_start(args, format);
#ifdef SOF_LOG_THREADED
    sof_log_error_t rc = write_queued(log, &head, format, args);
#else
    sof_log_error_t rc = write_direct(log, &head, format, args);
#endif
    va_end(args);
    return rc;
}

/// @brief Formats a whole line, ending in a newline, into sink. Everything
/// but the message is written by hand, as
/// "%04d-%02d-%02d %02d:%02d:%02d  %s  %ld  %s:%d  %-5s  " would give it.
static void format_line(sink_t *sink, const line_head_t *head, const char *format, va_list args) {
    // The time, the pid, the line and the level, with their spaces, take at
    // most 19 + 2 + 20 + 2 + 1 + 11 + 2 + 5 + 2 bytes.
    char text[80];
    size_t n;
    if (head->year >= 0 && head->year <= 9999) {
        put_two(text, head->year / 100);
        put_two(text + 2, head->year % 100);
        text[4] = '-';
        put_two(text + 5, head->month);
        text[7] = '-';
        put_two(text + 8, head->day);
        text[10] = ' ';
        put_two(text + 11, head->hour);
        text[13] = ':';
        put_two(text + 14, head->minute);
        text[16] = ':';
        put_two(text + 17, head->second);
        text[19] = ' ';
        text[20] = ' ';
        sink_bytes(sink, text, 21);
    } else {
        sink_printf(sink, "%04d-%02d-%02d %02d:%02d:%02d  ", head->year, head->month, head->day, head->hour, head->minute, head->second);
    }
    if (head->name != NULL) {
        sink_bytes(sink, head->name, strlen(head->name));
        sink_bytes(sink, "  ", 2);
    }
    n = 0;
    if (head->pid != 0) {
        n = put_decimal(text, head->pid);
        text[n++] = ' ';
        text[n++] = ' ';
    }
    if (head->file != NULL) {
        sink_bytes(sink, text, n);
        sink_bytes(sink, head->file, strlen(head->file));
        text[0] = ':';
        n = 1 + put_decimal(text + 1, head->line);
        text[n++] = ' ';
        text[n++] = ' ';
    }
    size_t level = strlen(head->level);
    memcpy(text + n, head->level, level);
    n += level;
    while (level++ < 5) {
        text[n++] = ' ';
    }
    text[n++] = ' ';
    text[n++] = ' ';
    sink_bytes(sink, text, n);
    sink_vprintf(sink, format, args);
    sink_bytes(sink, "\n", 1);
}

/// @brief Sets head's time to now, in UTC.
static void head_time(line_head_t *head, time_t now) {
#ifdef SOF_LOG_POSIX
    // POSIX makes a time_t the seconds since 1970-01-01 00:00:00 UTC, without
    // leap seconds, so the date is arithmetic, as gmtime_r() would give it,
    // but without its call. The days are turned into a date with Howard
    // Hinnant's civil_from_days(), in years that start on March 1st so that
    // the leap day comes last.
    long long seconds = (long long)now;
    long long days = seconds / 86400;
    long long rest = seconds % 86400;
    if (rest < 0) {
        rest += 86400;
        days--;
    }
    head->hour = (int)(rest / 3600);
    head->minute = (int)(rest / 60 % 60);
    head->second = (int)(rest % 60);
    long long z = days + 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long long day_of_era = z - era * 146097;
    long long year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    long long day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    long long shifted_month = (5 * day_of_year + 2) / 153;
    head->day = (int)(day_of_year - (153 * shifted_month + 2) / 5 + 1);
    head->month = (int)(shifted_month < 10 ? shifted_month + 3 : shifted_month - 9);
    head->year = (int)(year_of_era + era * 400 + (head->month <= 2));
#else
    // Elsewhere a time_t need not count seconds, so gmtime() reads it.
    // It isn't thread-safe; copying its result at once keeps the window
    // small where there's no gmtime_r().
    struct tm copy = {0};
    struct tm *tm = gmtime(&now);
    if (tm != NULL) {
        copy = *tm;
    }
    head->year = copy.tm_year + 1900;
    head->month = copy.tm_mon + 1;
    head->day = copy.tm_mday;
    head->hour = copy.tm_hour;
    head->minute = copy.tm_min;
    head->second = copy.tm_sec;
#endif
}

/// @brief Returns 1 if level is one of the sof_log_level_t values.
static int level_valid(sof_log_level_t level) {
    return (int)level >= SOF_LOG_LEVEL_ERROR && (int)level <= SOF_LOG_LEVEL_TRACE;
}

/// @brief Writes value in decimal, with a '-' if it is negative, to to,
/// which has room for 20 characters, and returns how many it wrote.
static size_t put_decimal(char *to, long value) {
    char digits[24];
    size_t n = 0;
    // Negated digit by digit, so that the most negative long works too.
    int negative = value < 0;
    do {
        long digit = value % 10;
        digits[n++] = (char)('0' + (digit < 0 ? -digit : digit));
        value /= 10;
    } while (value != 0);
    size_t length = 0;
    if (negative) {
        to[length++] = '-';
    }
    while (n > 0) {
        to[length++] = digits[--n];
    }
    return length;
}

/// @brief Writes value, from 0 to 99, as two digits to to.
static void put_two(char *to, int value) {
    to[0] = (char)('0' + value / 10);
    to[1] = (char)('0' + value % 10);
}

/// @brief Adds length bytes to sink.
static void sink_bytes(sink_t *sink, const char *bytes, size_t length) {
    if (sink->stream != NULL) {
        if (fwrite(bytes, 1, length, sink->stream) != length) {
            sink->failed = 1;
        }
    } else if (!sink->overflow && length <= sink->size - sink->length) {
        memcpy(sink->buffer + sink->length, bytes, length);
    } else {
        sink->overflow = 1;
    }
    sink->length += length;
}

/// @brief Formats into sink, as printf() would.
static void sink_printf(sink_t *sink, const char *format, ...) {
    va_list args;
    va_start(args, format);
    sink_vprintf(sink, format, args);
    va_end(args);
}

/// @brief Formats into sink, as vprintf() would.
static void sink_vprintf(sink_t *sink, const char *format, va_list args) {
    int n;
    if (sink->stream != NULL) {
        n = vfprintf(sink->stream, format, args);
    } else {
        // Once something hasn't fit, nothing after it is kept, only counted.
        char *at = NULL;
        size_t room = 0;
        if (!sink->overflow) {
            at = sink->buffer + sink->length;
            room = sink->size - sink->length;
        }
        n = vsnprintf(at, room, format, args);
        // vsnprintf() needs room for its NUL as well.
        if (n >= 0 && (size_t)n >= room) {
            sink->overflow = 1;
        }
    }
    if (n < 0) {
        sink->failed = 1;
    } else {
        sink->length += (size_t)n;
    }
}

#ifdef SOF_LOG_THREADED

/// @brief The size a message holding a line of length characters is
/// allocated with.
static size_t message_size(size_t length) {
    return sizeof(sof_log_message_t) + length + 1;
}

/// @brief Allocates log's writer, to write to stream, and starts its thread.
static sof_log_error_t writer_start(sof_log_t *log, FILE *stream) {
    struct sof_log_writer_t *writer = sof_alloc_malloc(&log->alloc, sizeof(*writer));
    if (writer == NULL) {
        return SOF_LOG_NO_MEM;
    }
    memset(writer, 0, sizeof(*writer));
    writer->stream = stream;
    writer->alloc = log->alloc;
    if (pthread_mutex_init(&writer->mutex, NULL) != 0) {
        sof_alloc_free(&log->alloc, writer, sizeof(*writer));
        return SOF_LOG_ERROR;
    }
    if (pthread_cond_init(&writer->work, NULL) != 0) {
        pthread_mutex_destroy(&writer->mutex);
        sof_alloc_free(&log->alloc, writer, sizeof(*writer));
        return SOF_LOG_ERROR;
    }
    if (pthread_cond_init(&writer->idle, NULL) != 0) {
        pthread_cond_destroy(&writer->work);
        pthread_mutex_destroy(&writer->mutex);
        sof_alloc_free(&log->alloc, writer, sizeof(*writer));
        return SOF_LOG_ERROR;
    }
    if (pthread_create(&writer->thread, NULL, writer_main, writer) != 0) {
        pthread_cond_destroy(&writer->idle);
        pthread_cond_destroy(&writer->work);
        pthread_mutex_destroy(&writer->mutex);
        sof_alloc_free(&log->alloc, writer, sizeof(*writer));
        return SOF_LOG_ERROR;
    }
    log->writer = writer;
    return SOF_LOG_OK;
}

/// @brief Has log's writer thread write every queued line and end, then
/// frees the writer.
static void writer_stop(sof_log_t *log) {
    struct sof_log_writer_t *writer = log->writer;
    if (writer == NULL) {
        return;
    }
    pthread_mutex_lock(&writer->mutex);
    writer->stop = 1;
    pthread_cond_signal(&writer->work);
    pthread_mutex_unlock(&writer->mutex);
    pthread_join(writer->thread, NULL);
    pthread_cond_destroy(&writer->idle);
    pthread_cond_destroy(&writer->work);
    pthread_mutex_destroy(&writer->mutex);
    sof_alloc_free(&log->alloc, writer, sizeof(*writer));
    log->writer = NULL;
}

/// @brief The writer thread: writes queued lines in order, flushing the
/// stream whenever the queue is empty, until it is stopped and the queue is
/// empty.
static void *writer_main(void *arg) {
    struct sof_log_writer_t *writer = arg;
    pthread_mutex_lock(&writer->mutex);
    for (;;) {
        while (writer->head == NULL && !writer->stop) {
            pthread_cond_wait(&writer->work, &writer->mutex);
        }
        if (writer->head == NULL) {
            break;
        }
        sof_log_message_t *message = writer->head;
        writer->head = message->next;
        if (writer->head == NULL) {
            writer->tail = NULL;
        }
        writer->writing = 1;
        int last = writer->head == NULL;
        pthread_mutex_unlock(&writer->mutex);

        int failed = fwrite(message->text, 1, message->length, writer->stream) != message->length;
        if (last && fflush(writer->stream) != 0) {
            failed = 1;
        }
        sof_alloc_free(&writer->alloc, message, message_size(message->length));

        pthread_mutex_lock(&writer->mutex);
        writer->writing = 0;
        if (failed) {
            writer->failed = 1;
        }
        if (writer->head == NULL) {
            pthread_cond_broadcast(&writer->idle);
        }
    }
    pthread_mutex_unlock(&writer->mutex);
    return NULL;
}

/// @brief Formats a line into a new message and queues it for the writer.
static sof_log_error_t write_queued(const sof_log_t *log, const line_head_t *head, const char *format, va_list args) {
    // The line is formatted once on the stack and copied into its message;
    // one too long for the buffer is formatted again into its message, which
    // its length, counted the first time, is known for.
    char buffer[LINE_BUFFER];
    sink_t sink = {0};
    sink.buffer = buffer;
    sink.size = sizeof(buffer);
    va_list copy;
    va_copy(copy, args);
    format_line(&sink, head, format, copy);
    va_end(copy);
    if (sink.failed) {
        return SOF_LOG_ERROR;
    }
    sof_log_message_t *message = sof_alloc_malloc(&log->alloc, message_size(sink.length));
    if (message == NULL) {
        return SOF_LOG_NO_MEM;
    }
    if (!sink.overflow) {
        memcpy(message->text, buffer, sink.length);
    } else {
        sink_t fill = {0};
        fill.buffer = message->text;
        fill.size = sink.length + 1;
        format_line(&fill, head, format, args);
    }
    // The second pass gives the same text, since the head is the same; the
    // length is the one it was allocated with either way.
    message->length = sink.length;
    message->text[sink.length] = '\0';
    message->next = NULL;

    struct sof_log_writer_t *writer = log->writer;
    pthread_mutex_lock(&writer->mutex);
    // The writer waits only when the queue is empty, so only a line added to
    // an empty queue needs to wake it.
    int was_empty = writer->tail == NULL;
    if (writer->tail != NULL) {
        writer->tail->next = message;
    } else {
        writer->head = message;
    }
    writer->tail = message;
    if (was_empty) {
        pthread_cond_signal(&writer->work);
    }
    pthread_mutex_unlock(&writer->mutex);
    return SOF_LOG_OK;
}

#else

/// @brief Locks stream for a whole line, where there is flockfile().
static void lock_stream(FILE *stream) {
#ifdef SOF_LOG_POSIX
    flockfile(stream);
#else
    (void)stream;
#endif
}

/// @brief Unlocks a stream locked by lock_stream().
static void unlock_stream(FILE *stream) {
#ifdef SOF_LOG_POSIX
    funlockfile(stream);
#else
    (void)stream;
#endif
}

/// @brief Writes a line to the stream and flushes it, so a crash right after
/// loses nothing: formatted on the stack and written with one fwrite() and
/// fflush() under the stream's lock, or, if it is too long for the buffer,
/// written in parts under the same lock.
static sof_log_error_t write_direct(const sof_log_t *log, const line_head_t *head, const char *format, va_list args) {
    char buffer[LINE_BUFFER];
    sink_t sink = {0};
    sink.buffer = buffer;
    sink.size = sizeof(buffer);
    va_list copy;
    va_copy(copy, args);
    format_line(&sink, head, format, copy);
    va_end(copy);
    if (sink.failed) {
        return SOF_LOG_ERROR;
    }
    int failed;
    lock_stream(log->stream);
    if (!sink.overflow) {
        failed = fwrite(buffer, 1, sink.length, log->stream) != sink.length;
    } else {
        sink_t direct = {0};
        direct.stream = log->stream;
        format_line(&direct, head, format, args);
        failed = direct.failed;
    }
    if (fflush(log->stream) != 0) {
        failed = 1;
    }
    unlock_stream(log->stream);
    return failed ? SOF_LOG_ERROR : SOF_LOG_OK;
}

#endif
