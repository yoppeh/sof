/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for the log library.
 * @details Usage: test or test-threaded. Runs every test, and exits with 0
 * if they all pass or 1 if any fails. The Makefile builds it twice: into
 * test, with sof_log.c compiled as usual, and into test-threaded, with
 * sof_log.c and this file compiled with SOF_LOG_THREADED; its check target
 * builds and runs both. The logs write to temporary files the tests read
 * back.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

// mkstemp(), fdopen() and getpid() are POSIX; ask for them in case the
// compiler is in strict c mode.
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <pthread.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sof_log.h"
#include "test.h"

/// @brief The threads test_threads() logs from, and the lines each writes.
#define THREADS 8
#define LINES_PER_THREAD 500

/// @brief The length of a line's time, as "2026-11-03 21:54:01".
#define TIME_LENGTH 19

/// @brief Returns everything written to stream, NUL-terminated, in memory the
/// caller frees with release(), or NULL if it can't be read.
static char *contents(FILE *stream) {
    fflush(stream);
    if (fseek(stream, 0, SEEK_END) != 0) {
        return NULL;
    }
    long size = ftell(stream);
    if (size < 0) {
        return NULL;
    }
    rewind(stream);
    char *text = sof_alloc_malloc(NULL, (size_t)size + 1);
    if (text == NULL) {
        return NULL;
    }
    // A short read would leave the text shorter than its block, so that
    // release() couldn't work out the size from it.
    if (fread(text, 1, (size_t)size, stream) != (size_t)size) {
        sof_alloc_free(NULL, text, (size_t)size + 1);
        return NULL;
    }
    text[size] = '\0';
    fseek(stream, 0, SEEK_END);
    return text;
}

/// @brief Frees text from contents(), whose block is its length plus its NUL,
/// since log lines hold no NULs. NULL does nothing.
static void release(char *text) {
    if (text != NULL) {
        sof_alloc_free(NULL, text, strlen(text) + 1);
    }
}

/// @brief Returns the number of lines in text.
static size_t count_lines(const char *text) {
    size_t lines = 0;
    for (const char *p = text; *p != '\0'; p++) {
        lines += *p == '\n';
    }
    return lines;
}

/// @brief Returns line after its time and the two spaces that follow, or NULL
/// if it doesn't start with a time in the form "2026-11-03 21:54:01".
static const char *after_time(const char *line) {
    static const char pattern[] = "dddd-dd-dd dd:dd:dd  ";
    for (size_t i = 0; pattern[i] != '\0'; i++) {
        if (pattern[i] == 'd' ? line[i] < '0' || line[i] > '9' : line[i] != pattern[i]) {
            return NULL;
        }
    }
    int month = (line[5] - '0') * 10 + (line[6] - '0');
    int day = (line[8] - '0') * 10 + (line[9] - '0');
    int hour = (line[11] - '0') * 10 + (line[12] - '0');
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23) {
        return NULL;
    }
    return line + TIME_LENGTH + 2;
}

/// @brief Checks that text is exactly one line, and returns what follows its
/// time, or NULL.
static const char *only_line(const char *text) {
    if (text == NULL || count_lines(text) != 1 || text[strlen(text) - 1] != '\n') {
        return NULL;
    }
    return after_time(text);
}

static void test_level_names(void) {
    CHECK(strcmp(sof_log_level_name(SOF_LOG_LEVEL_ERROR), "ERROR") == 0);
    CHECK(strcmp(sof_log_level_name(SOF_LOG_LEVEL_WARN), "WARN") == 0);
    CHECK(strcmp(sof_log_level_name(SOF_LOG_LEVEL_INFO), "INFO") == 0);
    CHECK(strcmp(sof_log_level_name(SOF_LOG_LEVEL_DEBUG), "DEBUG") == 0);
    CHECK(strcmp(sof_log_level_name(SOF_LOG_LEVEL_TRACE), "TRACE") == 0);
    CHECK(strcmp(sof_log_level_name(SOF_LOG_LEVEL_ALL), "TRACE") == 0);
    CHECK(sof_log_level_name((sof_log_level_t)(SOF_LOG_LEVEL_TRACE + 1)) == NULL);
    CHECK(sof_log_level_name((sof_log_level_t)-1) == NULL);
}

static void test_level_parse(void) {
    sof_log_level_t level = SOF_LOG_LEVEL_INFO;
    CHECK(sof_log_level_parse("error", &level) == SOF_LOG_OK && level == SOF_LOG_LEVEL_ERROR);
    CHECK(sof_log_level_parse("Warn", &level) == SOF_LOG_OK && level == SOF_LOG_LEVEL_WARN);
    CHECK(sof_log_level_parse("INFO", &level) == SOF_LOG_OK && level == SOF_LOG_LEVEL_INFO);
    CHECK(sof_log_level_parse("dEbUg", &level) == SOF_LOG_OK && level == SOF_LOG_LEVEL_DEBUG);
    CHECK(sof_log_level_parse("trace", &level) == SOF_LOG_OK && level == SOF_LOG_LEVEL_TRACE);
    CHECK(sof_log_level_parse("error", &level) == SOF_LOG_OK && level == SOF_LOG_LEVEL_ERROR);
    CHECK(sof_log_level_parse("all", &level) == SOF_LOG_OK && level == SOF_LOG_LEVEL_ALL);

    // Anything else leaves the level as it was.
    level = SOF_LOG_LEVEL_WARN;
    CHECK(sof_log_level_parse("", &level) == SOF_LOG_NO_MATCH);
    CHECK(sof_log_level_parse("warning", &level) == SOF_LOG_NO_MATCH);
    CHECK(sof_log_level_parse("war", &level) == SOF_LOG_NO_MATCH);
    CHECK(sof_log_level_parse("info ", &level) == SOF_LOG_NO_MATCH);
    CHECK(sof_log_level_parse("3", &level) == SOF_LOG_NO_MATCH);
    CHECK(level == SOF_LOG_LEVEL_WARN);
    CHECK(sof_log_level_parse(NULL, &level) == SOF_LOG_ERROR);
    CHECK(sof_log_level_parse("info", NULL) == SOF_LOG_ERROR);
    CHECK(level == SOF_LOG_LEVEL_WARN);
}

static void test_init_free(void) {
    FILE *out = tmpfile();
    CHECK(out != NULL);
    sof_log_t logger;
    char name[] = "server";
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_DEBUG, name, NULL) == SOF_LOG_OK);
    CHECK(logger.stream == out);
    CHECK(logger.level == SOF_LOG_LEVEL_DEBUG);
    // The log keeps its own copy of the name.
    CHECK(logger.name != NULL && logger.name != name && strcmp(logger.name, "server") == 0);
    name[0] = 'X';
    CHECK(strcmp(logger.name, "server") == 0);
#ifdef SOF_LOG_THREADED
    CHECK(logger.writer != NULL);
#else
    CHECK(logger.writer == NULL);
#endif
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    CHECK(logger.stream == NULL && logger.name == NULL && logger.writer == NULL);
    // Freeing again is harmless.
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);

    // No name, or an empty one, is left out.
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, NULL, NULL) == SOF_LOG_OK);
    CHECK(logger.name == NULL);
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, "", NULL) == SOF_LOG_OK);
    CHECK(logger.name == NULL);
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);

    // Bad arguments leave the log empty.
    CHECK(sof_log_init(NULL, out, SOF_LOG_LEVEL_INFO, "x", NULL) == SOF_LOG_ERROR);
    CHECK(sof_log_init(&logger, NULL, SOF_LOG_LEVEL_INFO, "x", NULL) == SOF_LOG_ERROR);
    CHECK(logger.stream == NULL && logger.name == NULL);
    CHECK(sof_log_init(&logger, out, (sof_log_level_t)(SOF_LOG_LEVEL_TRACE + 1), "x", NULL) == SOF_LOG_ERROR);
    CHECK(logger.stream == NULL && logger.name == NULL);
    CHECK(sof_log_init(&logger, out, (sof_log_level_t)-1, "x", NULL) == SOF_LOG_ERROR);
    CHECK(logger.stream == NULL);
    CHECK(sof_log_free(NULL) == SOF_LOG_ERROR);

    // The stream belongs to the caller: sof_log_free() didn't close it.
    CHECK(fputs("still open\n", out) >= 0);
    fclose(out);
}

static void test_line_format(void) {
    FILE *out = tmpfile();
    CHECK(out != NULL);
    sof_log_t logger;
    char expected[256];
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, "server", NULL) == SOF_LOG_OK);

    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_WARN, "main.c", 31, "disk %d%% full", 91) == SOF_LOG_OK);
    CHECK(sof_log_flush(&logger) == SOF_LOG_OK);
    char *text = contents(out);
    const char *rest = only_line(text);
    CHECK(rest != NULL);
    snprintf(expected, sizeof(expected), "server  %ld  main.c:31  WARN   disk 91%% full\n", (long)getpid());
    CHECK(strcmp(rest, expected) == 0);
    release(text);

    // A NULL file leaves out the location.
    CHECK(ftruncate(fileno(out), 0) == 0);
    rewind(out);
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_ERROR, NULL, 99, "no location") == SOF_LOG_OK);
    CHECK(sof_log_flush(&logger) == SOF_LOG_OK);
    text = contents(out);
    rest = only_line(text);
    CHECK(rest != NULL);
    snprintf(expected, sizeof(expected), "server  %ld  ERROR  no location\n", (long)getpid());
    CHECK(strcmp(rest, expected) == 0);
    release(text);
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);

    // A log without a name leaves it out. The macros give this file and line.
    CHECK(ftruncate(fileno(out), 0) == 0);
    rewind(out);
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_TRACE, NULL, NULL) == SOF_LOG_OK);
    int line = __LINE__ + 1;
    sof_log_trace(&logger, "%s and %d", "text", 7);
    CHECK(sof_log_flush(&logger) == SOF_LOG_OK);
    text = contents(out);
    rest = only_line(text);
    CHECK(rest != NULL);
    snprintf(expected, sizeof(expected), "%ld  test.c:%d  TRACE  text and 7\n", (long)getpid(), line);
    CHECK(strcmp(rest, expected) == 0);
    release(text);

    // Logging only reads the log, so the functions and macros take a const
    // one, and leave its fields as they were.
    CHECK(ftruncate(fileno(out), 0) == 0);
    rewind(out);
    const sof_log_t *reader = &logger;
    sof_log_t before = logger;
    CHECK(sof_log_write(reader, SOF_LOG_LEVEL_INFO, "c.c", 2, "const %d", 1) == SOF_LOG_OK);
    line = __LINE__ + 1;
    sof_log_info(reader, "const %d", 2);
    CHECK(sof_log_flush(reader) == SOF_LOG_OK);
    CHECK(before.stream == logger.stream && before.level == logger.level && before.name == logger.name && before.writer == logger.writer);
    text = contents(out);
    CHECK(text != NULL);
    snprintf(expected, sizeof(expected), "c.c:2  INFO   const 1\n");
    CHECK(strstr(text, expected) != NULL);
    snprintf(expected, sizeof(expected), "test.c:%d  INFO   const 2\n", line);
    CHECK(strstr(text, expected) != NULL);
    release(text);
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    fclose(out);
}

static void test_long_message(void) {
    // A message of any length is written whole.
    size_t length = 100000;
    char *message = sof_alloc_malloc(NULL, length + 1);
    CHECK(message != NULL);
    for (size_t i = 0; i < length; i++) {
        message[i] = (char)('a' + i % 26);
    }
    message[length] = '\0';
    FILE *out = tmpfile();
    CHECK(out != NULL);
    sof_log_t logger;
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, "app", NULL) == SOF_LOG_OK);
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_INFO, "f.c", 1, "<%s>", message) == SOF_LOG_OK);
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    char *text = contents(out);
    const char *rest = only_line(text);
    CHECK(rest != NULL);
    const char *open = strchr(rest, '<');
    CHECK(open != NULL);
    CHECK(strncmp(open + 1, message, length) == 0);
    CHECK(strcmp(open + 1 + length, ">\n") == 0);
    release(text);
    sof_alloc_free(NULL, message, length + 1);
    fclose(out);
}

/// @brief Logs a message of length characters with name, file and line, and
/// checks the line is exactly what printf() gives for it.
static void check_line(FILE *out, const char *name, const char *file, int line, size_t length) {
    CHECK(ftruncate(fileno(out), 0) == 0);
    rewind(out);
    char *message = sof_alloc_malloc(NULL, length + 1);
    char *expected = sof_alloc_malloc(NULL, length + 2048);
    CHECK(message != NULL && expected != NULL);
    for (size_t i = 0; i < length; i++) {
        message[i] = (char)('a' + (i * 7 + length) % 26);
    }
    message[length] = '\0';
    sof_log_t logger;
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_ALL, name, NULL) == SOF_LOG_OK);
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_WARN, file, line, "%s", message) == SOF_LOG_OK);
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    size_t n = 0;
    if (name != NULL && name[0] != '\0') {
        n += (size_t)snprintf(expected + n, length + 2048 - n, "%s  ", name);
    }
    n += (size_t)snprintf(expected + n, length + 2048 - n, "%ld  ", (long)getpid());
    if (file != NULL) {
        n += (size_t)snprintf(expected + n, length + 2048 - n, "%s:%d  ", file, line);
    }
    snprintf(expected + n, length + 2048 - n, "%-5s  %s\n", "WARN", message);
    char *text = contents(out);
    const char *rest = only_line(text);
    int same = rest != NULL && strcmp(rest, expected) == 0;
    release(text);
    sof_alloc_free(NULL, message, length + 1);
    sof_alloc_free(NULL, expected, length + 2048);
    CHECK(same);
}

static void test_line_lengths(void) {
    // Lines are formatted into a buffer of 512 bytes; longer ones are
    // written another way. Either way the line is the same, for messages
    // whose lines end on either side of the buffer's end, and for a name or
    // a file that doesn't fit in it at all.
    FILE *out = tmpfile();
    CHECK(out != NULL);
    int failures = test_failure_count;
    for (size_t length = 0; length < 600 && test_failure_count == failures; length++) {
        check_line(out, "server", "src/main.c", 31, length);
    }
    check_line(out, NULL, NULL, 0, 500);
    check_line(out, "app", "f.c", -7, 10);
    check_line(out, "app", "f.c", 2147483647, 10);
    check_line(out, "app", "f.c", -2147483647 - 1, 10);
    char big[700];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    check_line(out, big, "f.c", 1, 10);
    check_line(out, "app", big, 1, 10);
    check_line(out, big, big, 1, 1000);
    fclose(out);
}

static void test_line_time(void) {
    // The time is the time of the call, in UTC, as gmtime_r() gives it.
    FILE *out = tmpfile();
    CHECK(out != NULL);
    sof_log_t logger;
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, NULL, NULL) == SOF_LOG_OK);
    time_t before = time(NULL);
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_INFO, NULL, 0, "now") == SOF_LOG_OK);
    time_t after = time(NULL);
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    char *text = contents(out);
    CHECK(only_line(text) != NULL);
    int found = 0;
    for (time_t t = before; t <= after; t++) {
        struct tm tm;
        // Room for the widest ints, which gcc checks the format against.
        char want[80];
        CHECK(gmtime_r(&t, &tm) != NULL);
        snprintf(want, sizeof(want), "%04d-%02d-%02d %02d:%02d:%02d  ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        found |= strncmp(text, want, TIME_LENGTH + 2) == 0;
    }
    CHECK(found);
    release(text);
    fclose(out);
}

static void test_buffering(void) {
    FILE *out = tmpfile();
    CHECK(out != NULL);
    CHECK(setvbuf(out, NULL, _IOFBF, 4096) == 0);
    sof_log_t logger;
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, "app", NULL) == SOF_LOG_OK);
    struct stat st;
#ifdef SOF_LOG_THREADED
    // The writer thread flushes the stream whenever it has written every
    // queued line, so the line is in the file once sof_log_flush() returns.
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_INFO, "f.c", 1, "buffered") == SOF_LOG_OK);
#else
    // Without SOF_LOG_THREADED, each line is flushed as it is written, so it
    // is in the file when sof_log_write() returns, though the stream is
    // fully buffered.
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_INFO, "f.c", 1, "buffered") == SOF_LOG_OK);
    CHECK(fstat(fileno(out), &st) == 0 && st.st_size > 0);
    // A line longer than the 512-byte buffer is flushed too.
    off_t short_size = st.st_size;
    char long_message[1500];
    memset(long_message, 'z', sizeof(long_message) - 1);
    long_message[sizeof(long_message) - 1] = '\0';
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_INFO, "f.c", 1, "%s", long_message) == SOF_LOG_OK);
    CHECK(fstat(fileno(out), &st) == 0 && st.st_size > short_size + 1500);
#endif
    CHECK(sof_log_flush(&logger) == SOF_LOG_OK);
    CHECK(fstat(fileno(out), &st) == 0 && st.st_size > 0);
    // sof_log_free() flushes too.
    off_t size = st.st_size;
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_INFO, "f.c", 2, "freed") == SOF_LOG_OK);
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    CHECK(fstat(fileno(out), &st) == 0 && st.st_size > size);
    char *text = contents(out);
#ifdef SOF_LOG_THREADED
    CHECK(text != NULL && count_lines(text) == 2);
#else
    CHECK(text != NULL && count_lines(text) == 3);
#endif
    release(text);
    fclose(out);
}

static void test_levels(void) {
    FILE *out = tmpfile();
    CHECK(out != NULL);
    sof_log_t logger;
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, "app", NULL) == SOF_LOG_OK);
    int evaluated = 0;
    sof_log_error(&logger, "e %d", evaluated++);
    sof_log_warn(&logger, "w %d", evaluated++);
    sof_log_info(&logger, "i %d", evaluated++);
    // Messages after the log's level are dropped, without their arguments
    // being evaluated.
    sof_log_debug(&logger, "d %d", evaluated++);
    sof_log_trace(&logger, "t %d", evaluated++);
    CHECK(evaluated == 3);
    // sof_log_write() drops them too, returning SOF_LOG_OK.
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_DEBUG, "f.c", 1, "dropped") == SOF_LOG_OK);
    CHECK(sof_log_flush(&logger) == SOF_LOG_OK);
    char *text = contents(out);
    CHECK(text != NULL);
    CHECK(count_lines(text) == 3);
    CHECK(strstr(text, "ERROR  e 0\n") != NULL);
    CHECK(strstr(text, "WARN   w 1\n") != NULL);
    CHECK(strstr(text, "INFO   i 2\n") != NULL);
    release(text);

    // sof_log_set_level() changes what is written.
    CHECK(sof_log_set_level(&logger, SOF_LOG_LEVEL_ERROR) == SOF_LOG_OK);
    CHECK(logger.level == SOF_LOG_LEVEL_ERROR);
    sof_log_warn(&logger, "dropped");
    CHECK(sof_log_set_level(&logger, SOF_LOG_LEVEL_ALL) == SOF_LOG_OK);
    sof_log_trace(&logger, "kept");
    CHECK(sof_log_set_level(&logger, (sof_log_level_t)(SOF_LOG_LEVEL_TRACE + 1)) == SOF_LOG_ERROR);
    CHECK(sof_log_set_level(&logger, (sof_log_level_t)-1) == SOF_LOG_ERROR);
    CHECK(logger.level == SOF_LOG_LEVEL_ALL);
    CHECK(sof_log_flush(&logger) == SOF_LOG_OK);
    text = contents(out);
    CHECK(text != NULL);
    CHECK(count_lines(text) == 4);
    CHECK(strstr(text, "dropped") == NULL);
    CHECK(strstr(text, "TRACE  kept\n") != NULL);
    release(text);

    // A level that isn't a sof_log_level_t is refused.
    CHECK(sof_log_write(&logger, (sof_log_level_t)(SOF_LOG_LEVEL_TRACE + 1), "f.c", 1, "bad") == SOF_LOG_ERROR);
    CHECK(sof_log_write(&logger, (sof_log_level_t)-1, "f.c", 1, "bad") == SOF_LOG_ERROR);
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    fclose(out);
}

static void test_macros(void) {
    FILE *out = tmpfile();
    CHECK(out != NULL);
    sof_log_t logger;
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, "app", NULL) == SOF_LOG_OK);
    // The log argument is evaluated once, written or not.
    int evaluated = 0;
    sof_log_info((evaluated++, &logger), "once");
    CHECK(evaluated == 1);
    sof_log_debug((evaluated++, &logger), "dropped");
    CHECK(evaluated == 2);
    // Each macro is a single statement, safe in an if without braces.
    int branch = 0;
    if (evaluated == 0)
        sof_log_error(&logger, "not reached");
    else
        branch = 1;
    CHECK(branch == 1);
    // A NULL log writes nothing.
    sof_log_t *none = NULL;
    sof_log_error(none, "nothing %d", evaluated++);
    CHECK(evaluated == 2);
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    char *text = contents(out);
    CHECK(text != NULL);
    CHECK(count_lines(text) == 1 && strstr(text, "INFO   once\n") != NULL);
    release(text);
    fclose(out);
}

static void test_null_and_freed(void) {
    FILE *out = tmpfile();
    CHECK(out != NULL);
    const char *no_format = NULL;
    CHECK(sof_log_write(NULL, SOF_LOG_LEVEL_ERROR, "f.c", 1, "x") == SOF_LOG_ERROR);
    CHECK(sof_log_flush(NULL) == SOF_LOG_ERROR);
    CHECK(sof_log_set_level(NULL, SOF_LOG_LEVEL_INFO) == SOF_LOG_ERROR);

    sof_log_t logger;
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, "app", NULL) == SOF_LOG_OK);
    // A NULL format is what is being tested.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_ERROR, "f.c", 1, no_format) == SOF_LOG_ERROR);
#pragma GCC diagnostic pop
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);

    // A freed log writes nothing until it is initialized again.
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_ERROR, "f.c", 1, "x") == SOF_LOG_ERROR);
    sof_log_error(&logger, "x");
    CHECK(sof_log_flush(&logger) == SOF_LOG_ERROR);
    CHECK(sof_log_set_level(&logger, SOF_LOG_LEVEL_INFO) == SOF_LOG_ERROR);

    // So does one whose sof_log_init() failed.
    CHECK(sof_log_init(&logger, NULL, SOF_LOG_LEVEL_INFO, "app", NULL) == SOF_LOG_ERROR);
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_ERROR, "f.c", 1, "x") == SOF_LOG_ERROR);
    sof_log_error(&logger, "x");
    CHECK(sof_log_flush(&logger) == SOF_LOG_ERROR);
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);

    char *text = contents(out);
    CHECK(text != NULL && text[0] == '\0');
    release(text);

    // It works again once initialized.
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, "app", NULL) == SOF_LOG_OK);
    sof_log_error(&logger, "back");
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    text = contents(out);
    CHECK(text != NULL && count_lines(text) == 1);
    release(text);
    fclose(out);
}

static void test_free_writes_everything(void) {
    // sof_log_free() writes every line written before it, without a sof_log_flush().
    FILE *out = tmpfile();
    CHECK(out != NULL);
    sof_log_t logger;
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, "app", NULL) == SOF_LOG_OK);
    for (int i = 0; i < 2000; i++) {
        sof_log_info(&logger, "line %d", i);
    }
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    char *text = contents(out);
    CHECK(text != NULL);
    CHECK(count_lines(text) == 2000);
    // In the order they were written.
    const char *p = text;
    for (int i = 0; i < 2000; i++) {
        char want[32];
        snprintf(want, sizeof(want), "INFO   line %d\n", i);
        const char *at = strstr(p, want);
        CHECK(at != NULL);
        p = at + strlen(want);
    }
    release(text);
    fclose(out);
}

/// @brief What each thread of test_threads() gets.
typedef struct writer_arg_t {
    sof_log_t *logger;
    int thread;
} writer_arg_t;

static void *write_lines(void *arg) {
    writer_arg_t *w = arg;
    for (int i = 0; i < LINES_PER_THREAD; i++) {
        sof_log_info(w->logger, "thread %d line %d", w->thread, i);
    }
    return NULL;
}

static void test_threads(void) {
    // Lines from threads writing at once come out whole, each thread's in its
    // own order.
    FILE *out = tmpfile();
    CHECK(out != NULL);
    sof_log_t logger;
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, "app", NULL) == SOF_LOG_OK);
    pthread_t threads[THREADS];
    writer_arg_t args[THREADS];
    for (int t = 0; t < THREADS; t++) {
        args[t].logger = &logger;
        args[t].thread = t;
        CHECK(pthread_create(&threads[t], NULL, write_lines, &args[t]) == 0);
    }
    for (int t = 0; t < THREADS; t++) {
        pthread_join(threads[t], NULL);
    }
    CHECK(sof_log_flush(&logger) == SOF_LOG_OK);
    char *text = contents(out);
    CHECK(text != NULL);
    CHECK(count_lines(text) == THREADS * LINES_PER_THREAD);
    int next[THREADS] = {0};
    int whole = 1;
    for (char *line = text; *line != '\0';) {
        char *end = strchr(line, '\n');
        const char *rest = after_time(line);
        const char *message = rest != NULL ? strstr(rest, "INFO   thread ") : NULL;
        int t = -1, i = -1, n = 0;
        if (end == NULL || message == NULL || message > end || sscanf(message, "INFO   thread %d line %d%n", &t, &i, &n) != 2 || message + n != end || t < 0 || t >= THREADS || i != next[t]) {
            whole = 0;
            break;
        }
        next[t]++;
        line = end + 1;
    }
    CHECK(whole);
    for (int t = 0; t < THREADS; t++) {
        CHECK(next[t] == LINES_PER_THREAD);
    }
    release(text);
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    fclose(out);
}

static void test_write_failure(void) {
    // A stream that can't be written to: a file opened for reading only.
    char path[] = "/tmp/log-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    unlink(path);
    FILE *in = fdopen(fd, "r");
    CHECK(in != NULL);
    sof_log_t logger;
    CHECK(sof_log_init(&logger, in, SOF_LOG_LEVEL_INFO, "app", NULL) == SOF_LOG_OK);
#ifdef SOF_LOG_THREADED
    // The line is queued, and sof_log_flush() reports that writing it failed.
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_ERROR, "f.c", 1, "lost") == SOF_LOG_OK);
    CHECK(sof_log_flush(&logger) == SOF_LOG_ERROR);
#else
    CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_ERROR, "f.c", 1, "lost") == SOF_LOG_ERROR);
#endif
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    fclose(in);
}

static void test_custom_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    FILE *out = tmpfile();
    CHECK(out != NULL);
    sof_log_t logger;
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, "app", &alloc) == SOF_LOG_OK);
    // The log keeps a copy of the allocator.
    alloc.malloc = NULL;
    CHECK(logger.alloc.ctx == &state);
#ifdef SOF_LOG_THREADED
    // The name and the writer.
    CHECK(state.live == 2);
#else
    CHECK(state.live == 1);
#endif
    size_t allocations = state.allocations;
    for (int i = 0; i < 100; i++) {
        sof_log_info(&logger, "line %d", i);
        // The writer frees each line on its own thread; waiting for it keeps
        // the test allocator's counts from being changed by two threads at
        // once.
        CHECK(sof_log_flush(&logger) == SOF_LOG_OK);
    }
#ifdef SOF_LOG_THREADED
    // Each line is allocated, and freed once it is written.
    CHECK(state.allocations == allocations + 100);
    CHECK(state.live == 2);
#else
    // Lines are written without allocating.
    CHECK(state.allocations == allocations);
#endif
    CHECK(sof_log_free(&logger) == SOF_LOG_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // An allocator with a malloc but no free is refused, without allocating.
    allocations = state.allocations;
    alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    CHECK(sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, "app", &alloc) == SOF_LOG_ERROR);
    CHECK(logger.stream == NULL);
    CHECK(state.allocations == allocations);
    fclose(out);
}

static void test_no_memory(void) {
    // Fail each allocation in turn, through init and writes, until a run
    // makes none fail.
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        FILE *out = tmpfile();
        CHECK(out != NULL);
        sof_log_t logger;
        sof_log_error_t rc = sof_log_init(&logger, out, SOF_LOG_LEVEL_INFO, "app", &alloc);
        size_t written = 0;
        if (rc != SOF_LOG_OK) {
            CHECK(rc == SOF_LOG_NO_MEM);
            CHECK(logger.stream == NULL && logger.name == NULL && logger.writer == NULL);
            CHECK(sof_log_write(&logger, SOF_LOG_LEVEL_ERROR, "f.c", 1, "x") == SOF_LOG_ERROR);
        } else {
            int failed = 0;
            for (int i = 0; i < 20; i++) {
                rc = sof_log_write(&logger, SOF_LOG_LEVEL_ERROR, "f.c", 1, "line %d", i);
                if (rc == SOF_LOG_OK) {
                    written++;
                } else {
                    CHECK(rc == SOF_LOG_NO_MEM);
                    failed++;
                }
                CHECK(sof_log_flush(&logger) == SOF_LOG_OK);
            }
            // Only the chosen allocation fails.
            CHECK(failed == (state.allocations >= n));
        }
        CHECK(sof_log_free(&logger) == SOF_LOG_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        char *text = contents(out);
        CHECK(text != NULL);
        CHECK(count_lines(text) == written);
        release(text);
        fclose(out);
        if (state.allocations < n) {
            break;
        }
    }
}

int main(void) {
#ifdef SOF_LOG_THREADED
    printf("sof_log with SOF_LOG_THREADED:\n");
#else
    printf("sof_log:\n");
#endif
    run("level names", test_level_names);
    run("level parse", test_level_parse);
    run("init and free", test_init_free);
    run("line format", test_line_format);
    run("long message", test_long_message);
    run("line lengths", test_line_lengths);
    run("line time", test_line_time);
    run("buffering and flushing", test_buffering);
    run("levels", test_levels);
    run("macros", test_macros);
    run("null and freed logs", test_null_and_freed);
    run("free writes everything", test_free_writes_everything);
    run("threads", test_threads);
    run("write failure", test_write_failure);
    run("custom allocator", test_custom_allocator);
    run("no memory", test_no_memory);
    return test_report();
}
