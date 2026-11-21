/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Logs the lines of files at the levels they name.
 * @details Each line of the input is logged with sof_log as a message. If
 * its first word names a level, such as "warn disk almost full" or
 * "ERROR: no reply", the rest of the line is logged at that level; otherwise
 * the whole line is logged at the default level. Lines after the log's level
 * are dropped. Each line in the log names the input and line the message
 * came from.
 *
 *     logrun [-h] [-v] [-l level] [-d level] [-n name] [-o file] [file]...
 *
 * With no file, or a file named "-", it reads standard input. Its options
 * are read with sof_copt. With -v, it logs what it did, and how long
 * logging and flushing took, to standard error, through a second log, which
 * shows what changes when sof_log.c is compiled with SOF_LOG_THREADED. It
 * exits with 0, 1 if a file couldn't be read or the log couldn't be written,
 * or 2 if the options are wrong.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "sof_copt.h"
#include "sof_log.h"
#include "sof_string.h"
#include "sof_timer.h"

/// @brief The exit codes.
#define EXIT_DONE 0
#define EXIT_TROUBLE 1
#define EXIT_BAD_OPTIONS 2

/// @brief A code the validate callbacks return, which sof_copt keeps in the
/// option's stop_value: -l or -d named no level. The callback has printed
/// why.
#define BAD_LEVEL 1

/// @brief Room for the longest level name, "ERROR" or "TRACE", a ':' and a
/// NUL, and one more byte, to tell a longer word from a name.
#define WORD_SIZE 8

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The least important level written, from -l.
    sof_log_level_t level;
    /// @brief The level of a line that names none, from -d.
    sof_log_level_t fallback;
} settings_t;

/// @brief What has been logged, for -v.
typedef struct counts_t {
    size_t read;
    size_t written;
    size_t dropped;
} counts_t;

static int fallback_validate(sof_copt_t *option, void *ctx);
static int level_validate(sof_copt_t *option, void *ctx);
static int log_file(const sof_log_t *log, const sof_log_t *self, FILE *f, const char *name, sof_log_level_t fallback, sof_string_t *line, counts_t *counts);
static int parse_level(const sof_copt_t *option, sof_log_level_t *level);
static int read_line(FILE *f, sof_string_t *line);
static const char *split_level(const char *line, sof_log_level_t *level);
static void usage(FILE *stream);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_verbose = {
    .option = "-v",
    .description = "say what was logged, and how long it took, on standard error",
};

static sof_copt_t opt_level = {
    .option = "-l",
    .description = "write this level and before: error, warn, info (default), debug, trace or all",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = level_validate,
};

static sof_copt_t opt_fallback = {
    .option = "-d",
    .description = "log lines that name no level at this one (default info)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = fallback_validate,
};

static sof_copt_t opt_name = {
    .option = "-n",
    .description = "the name on each line (default logrun; \"\" for none)",
    .type = SOF_COPT_ARG_REQUIRED,
};

static sof_copt_t opt_output = {
    .option = "-o",
    .description = "append the log to this file instead of writing it to standard error",
    .type = SOF_COPT_ARG_REQUIRED,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_verbose,
    &opt_level,
    &opt_fallback,
    &opt_name,
    &opt_output,
};

/// @brief Reads the argument of -d.
static int fallback_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    return parse_level(option, &settings->fallback);
}

/// @brief Reads the argument of -l.
static int level_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    return parse_level(option, &settings->level);
}

/// @brief Logs each line of f, at the level it names or at fallback.
/// @details The input's name and line number take the place of the source
/// file and line that the macros would give, which would only name this
/// loop. Problems reading are logged to self.
/// @param name The input's name, for the log and messages.
/// @param line A string to read each line into.
/// @return 0, or EXIT_TROUBLE if f couldn't be read or memory ran out.
static int log_file(const sof_log_t *log, const sof_log_t *self, FILE *f, const char *name, sof_log_level_t fallback, sof_string_t *line, counts_t *counts) {
    size_t number = 0;
    int got;
    while ((got = read_line(f, line)) > 0) {
        number++;
        sof_log_level_t at = fallback;
        const char *message = split_level(line->data, &at);
        if (at > log->level) {
            counts->dropped++;
        } else if (sof_log_write(log, at, name, (int)number, "%s", message) == SOF_LOG_OK) {
            counts->written++;
        } else {
            sof_log_error(self, "%s:%zu: couldn't log the line", name, number);
        }
    }
    counts->read += number;
    if (got < 0) {
        sof_log_error(self, "%s: out of memory", name);
        return EXIT_TROUBLE;
    }
    if (ferror(f)) {
        sof_log_error(self, "%s: %s", name, strerror(errno));
        return EXIT_TROUBLE;
    }
    sof_log_info(self, "%s: %zu lines", name, number);
    return 0;
}

/// @brief Reads a level's name, the argument of -l or -d.
/// @return 0, or BAD_LEVEL after printing why.
static int parse_level(const sof_copt_t *option, sof_log_level_t *level) {
    if (sof_log_level_parse(option->value, level) != SOF_LOG_OK) {
        fprintf(stderr, "%s wants error, warn, info, debug, trace or all, not \"%s\"\n", option->option, option->value);
        return BAD_LEVEL;
    }
    return 0;
}

/// @brief Reads a line of any length into line, without its newline, or
/// its carriage return and newline.
/// @return 1 if a line was read, 0 at the end of the file or on an error,
/// or -1 if memory ran out.
static int read_line(FILE *f, sof_string_t *line) {
    char buffer[256];
    int got = 0;
    sof_string_clear(line);
    while (fgets(buffer, sizeof(buffer), f) != NULL) {
        got = 1;
        if (sof_string_append_cstr(line, buffer) != SOF_STRING_OK) {
            return -1;
        }
        if (line->data[line->length - 1] == '\n') {
            break;
        }
    }
    if (line->length > 0 && line->data[line->length - 1] == '\n') {
        sof_string_delete(line, line->length - 1, 1);
    }
    if (line->length > 0 && line->data[line->length - 1] == '\r') {
        sof_string_delete(line, line->length - 1, 1);
    }
    return got;
}

/// @brief Finds the level a line names with its first word, in any case
/// and perhaps followed by a ':', as in "warn disk full" or "ERROR: no
/// reply".
/// @details Only a level's own name counts. sof_log_level_parse() also
/// takes "all", which names a setting for a whole log rather than a
/// message's level, so a line starting "All" is an ordinary line.
/// @param level Set to the level named; left as it was if there is none.
/// @return The message: the rest of the line, after the spaces and tabs
/// following the word, or the whole line if it names no level.
static const char *split_level(const char *line, sof_log_level_t *level) {
    size_t length = strcspn(line, " \t");
    if (length == 0 || length >= WORD_SIZE) {
        return line;
    }
    char word[WORD_SIZE];
    memcpy(word, line, length);
    word[length] = '\0';
    if (word[length - 1] == ':') {
        word[length - 1] = '\0';
    }
    static const sof_string_t all = sof_string_const("all");
    sof_string_t name = sof_string_view(word, strlen(word));
    sof_log_level_t named;
    if (sof_string_casecmp(&name, &all) == 0 || sof_log_level_parse(word, &named) != SOF_LOG_OK) {
        return line;
    }
    *level = named;
    return line + length + strspn(line + length, " \t");
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: logrun [-h] [-v] [-l level] [-d level] [-n name] [-o file] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Logs each line of the files, or of standard input if there are none or a\n"
        "file is \"-\", at the level its first word names, as in \"warn: disk full\",\n"
        "or else at the default level. Exits with 0, 1 if a file couldn't be read or\n"
        "the log couldn't be written, or 2 if the options are wrong.\n");
}

int main(int ac, char **av) {
    settings_t settings = {SOF_LOG_LEVEL_INFO, SOF_LOG_LEVEL_INFO};
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "unknown option %s\n", av[index]);
        usage(stderr);
        return EXIT_BAD_OPTIONS;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "%s needs an argument\n", option->option);
        usage(stderr);
        return EXIT_BAD_OPTIONS;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "%s takes no argument\n", option->option);
        usage(stderr);
        return EXIT_BAD_OPTIONS;
    case SOF_COPT_STOPPED:
        return EXIT_BAD_OPTIONS;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        return EXIT_BAD_OPTIONS;
    }
    if (opt_help.present) {
        usage(stdout);
        return EXIT_DONE;
    }

    // The program's own messages go to a log of their own, on standard error.
    sof_log_t self;
    if (sof_log_init(&self, stderr, opt_verbose.present ? SOF_LOG_LEVEL_INFO : SOF_LOG_LEVEL_WARN, "logrun", NULL) != SOF_LOG_OK) {
        fprintf(stderr, "couldn't start the log\n");
        return EXIT_TROUBLE;
    }
    int rc = EXIT_TROUBLE;
    FILE *out = stderr;
    if (opt_output.present && (out = fopen(opt_output.value, "a")) == NULL) {
        sof_log_error(&self, "%s: %s", opt_output.value, strerror(errno));
        goto free_self;
    }
    sof_log_t log;
    if (sof_log_init(&log, out, settings.level, opt_name.present ? opt_name.value : "logrun", NULL) != SOF_LOG_OK) {
        sof_log_error(&self, "couldn't start the log");
        goto close_out;
    }
    sof_string_t line;
    if (sof_string_init(&line, 0, NULL) != SOF_STRING_OK) {
        sof_log_error(&self, "out of memory");
        goto free_log;
    }
    sof_log_info(&self, "logging at %s and before, lines with no level at %s; %s", sof_log_level_name(settings.level), sof_log_level_name(settings.fallback), log.writer != NULL ? "SOF_LOG_THREADED: lines are queued for a writer thread" : "lines are written as they are logged");

    static char *standard_input[] = {"-"};
    char **paths = av + index;
    int num_paths = ac - index;
    if (num_paths == 0) {
        paths = standard_input;
        num_paths = 1;
    }
    sof_timer_t logging, flushing;
    sof_timer_init(&logging);
    sof_timer_init(&flushing);
    counts_t counts = {0, 0, 0};
    rc = EXIT_DONE;
    sof_timer_start(&logging);
    for (int i = 0; i < num_paths; i++) {
        int is_stdin = strcmp(paths[i], "-") == 0;
        const char *name = is_stdin ? "stdin" : paths[i];
        FILE *f = is_stdin ? stdin : fopen(paths[i], "r");
        if (f == NULL) {
            sof_log_error(&self, "%s: %s", name, strerror(errno));
            rc = EXIT_TROUBLE;
            continue;
        }
        if (log_file(&log, &self, f, name, settings.fallback, &line, &counts) != 0) {
            rc = EXIT_TROUBLE;
        }
        if (!is_stdin) {
            fclose(f);
        }
    }
    sof_timer_stop(&logging);
    // With SOF_LOG_THREADED, this is where the program waits for the writer.
    sof_timer_start(&flushing);
    if (sof_log_flush(&log) != SOF_LOG_OK) {
        sof_log_error(&self, "writing the log failed");
        rc = EXIT_TROUBLE;
    }
    sof_timer_stop(&flushing);
    sof_log_info(&self, "%zu lines: %zu written, %zu dropped for their level", counts.read, counts.written, counts.dropped);
    sof_log_info(&self, "logging took %.3f ms, flushing %.3f ms", sof_timer_elapsed_ms(&logging), sof_timer_elapsed_ms(&flushing));
    sof_string_free(&line);
free_log:
    sof_log_free(&log);
close_out:
    if (out != stderr && fclose(out) != 0) {
        sof_log_error(&self, "%s: %s", opt_output.value, strerror(errno));
        rc = EXIT_TROUBLE;
    }
free_self:
    sof_log_free(&self);
    return rc;
}
