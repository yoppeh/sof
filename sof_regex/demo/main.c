/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A small grep: prints the lines of text that match a regular
 * expression.
 * @details Searches files, or standard input, for a PCRE2 regular
 * expression, and prints the lines that match, or only the matches, or how
 * many lines match, with lines of context before and after each selected
 * line if asked; the lines before are held in a sof_ring.
 *
 *     match [-h] [-i] [-v] [-o] [-c] [-n] [-A n] [-B n] [-L level] pattern [file]...
 *
 * With no file, or a file named "-", the text is read from standard input.
 * Its options are read with sof_copt, and its errors are written with
 * sof_log. It exits with 0 if a line was selected, 1 if none was, and 2 on
 * an error, as grep does.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_copt.h"
#include "sof_log.h"
#include "sof_ring.h"
#include "sof_string.h"
#include "match.h"

/// @brief The size of each piece of input read.
#define BUFFER_SIZE 65536

/// @brief A line kept in the ring of lines before, in case a selected line
/// follows.
typedef struct context_line_t {
    /// @brief The line, without its newline.
    sof_string_t text;
    /// @brief Its number.
    size_t line;
} context_line_t;

/// @brief What the found function needs to print a line or match.
typedef struct output_t {
    /// @brief The name of the file, printed before each line, or NULL when
    /// only one file is searched.
    const char *name;
    /// @brief Non-zero to print each line's number.
    int numbers;
    /// @brief Non-zero if lines of context are printed (-A or -B), so
    /// groups of lines that don't follow on are parted by "--".
    int context;
    /// @brief The lines to print after each selected line (-A).
    size_t after;
    /// @brief The lines still to print after the last selected line.
    size_t after_left;
    /// @brief The last lines not printed, up to -B of them, oldest first;
    /// not initialized without -B.
    sof_ring_t before;
    /// @brief The number of the last line printed in this file, or 0.
    size_t last;
    /// @brief Non-zero once anything has been printed.
    int printed;
    /// @brief Set if a line of context couldn't be kept.
    int no_mem;
} output_t;

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    sof_log_level_t level;
    size_t after;
    size_t before;
} settings_t;

static int after_validate(sof_copt_t *option, void *ctx);
static int before_validate(sof_copt_t *option, void *ctx);
static void forget(void *element, void *ctx);
static void found(const char *text, size_t length, size_t line, int selected, void *ctx);
static int level_validate(sof_copt_t *option, void *ctx);
static const char *name_of(const char *path);
static int parse_lines(const sof_copt_t *option, size_t *lines);
static void print_line(output_t *out, const char *text, size_t length, size_t line, char separator);
static void remember(output_t *out, const char *text, size_t length, size_t line);
static int search_file(match_t *m, sof_log_t *log, const char *path);
static void usage(FILE *stream);

/// @brief The code the validate callbacks return when an option's argument
/// is wrong, after printing why. sof_copt keeps it in the option's
/// stop_value.
#define BAD_ARGUMENT 1

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_ignore_case = {
    .option = "-i",
    .description = "ignore case (of ASCII letters)",
};

static sof_copt_t opt_invert = {
    .option = "-v",
    .description = "select the lines that don't match",
};

static sof_copt_t opt_only = {
    .option = "-o",
    .description = "print only the matches, each on a line of its own",
};

static sof_copt_t opt_count = {
    .option = "-c",
    .description = "print only how many lines are selected in each file",
};

static sof_copt_t opt_numbers = {
    .option = "-n",
    .description = "print each line's number before it",
};

static sof_copt_t opt_after = {
    .option = "-A",
    .description = "print n lines of context after each selected line",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = after_validate,
};

static sof_copt_t opt_after_long = {
    .option = "--after-context",
    .description = "the same as -A",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = after_validate,
};

static sof_copt_t opt_before = {
    .option = "-B",
    .description = "print n lines of context before each selected line",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = before_validate,
};

static sof_copt_t opt_before_long = {
    .option = "--before-context",
    .description = "the same as -B",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = before_validate,
};

static sof_copt_t opt_level = {
    .option = "-L",
    .description = "log messages up to this level: error, warn, info, debug or trace (default warn)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = level_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_ignore_case,
    &opt_invert,
    &opt_only,
    &opt_count,
    &opt_numbers,
    &opt_after,
    &opt_after_long,
    &opt_before,
    &opt_before_long,
    &opt_level,
};

/// @brief Reads the argument of -A or --after-context.
static int after_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    return parse_lines(option, &settings->after) == 0 ? 0 : BAD_ARGUMENT;
}

/// @brief Reads the argument of -B or --before-context.
static int before_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    return parse_lines(option, &settings->before) == 0 ? 0 : BAD_ARGUMENT;
}

/// @brief Frees a line of context as it leaves the ring.
static void forget(void *element, void *ctx) {
    (void)ctx;
    context_line_t *c = element;
    sof_string_free(&c->text);
}

/// @brief Prints a selected line or match, with the lines of context kept
/// before it, and counts down the lines after it; a line that isn't selected
/// is printed if it comes soon enough after one that was, else kept in case
/// one follows.
static void found(const char *text, size_t length, size_t line, int selected, void *ctx) {
    output_t *out = ctx;
    if (selected) {
        const context_line_t *c;
        for (c = sof_ring_peek(&out->before); c != NULL; c = sof_ring_next(&out->before, c)) {
            print_line(out, c->text.data, c->text.length, c->line, '-');
        }
        sof_ring_clear(&out->before);
        print_line(out, text, length, line, ':');
        out->after_left = out->after;
    } else if (out->after_left > 0) {
        out->after_left--;
        print_line(out, text, length, line, '-');
    } else if (out->before.ready) {
        remember(out, text, length, line);
    }
}

/// @brief Reads the argument of -L.
static int level_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    if (sof_log_level_parse(option->value, &settings->level) != SOF_LOG_OK) {
        fprintf(stderr, "%s needs error, warn, info, debug, trace or all, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Returns a file's name for messages and output: "(standard input)"
/// for "-", as grep names it.
static const char *name_of(const char *path) {
    return strcmp(path, "-") == 0 ? "(standard input)" : path;
}

/// @brief Reads a number of lines, 0 or more, from an option's argument.
/// @return 0, or 1 after printing why the argument is wrong.
static int parse_lines(const sof_copt_t *option, size_t *lines) {
    const char *s = option->value;
    char *end = NULL;
    unsigned long long value = 0;
    errno = 0;
    if (*s >= '0' && *s <= '9') {
        value = strtoull(s, &end, 10);
    }
    if (end == NULL || *end != '\0' || errno != 0 || value > SIZE_MAX) {
        fprintf(stderr, "%s needs a number of lines, not \"%s\"\n", option->option, s);
        return 1;
    }
    *lines = (size_t)value;
    return 0;
}

/// @brief Prints a line, or match, after the file's name and the line's
/// number if they are asked for, each followed by separator: ':' for a line
/// selected and '-' for context, as grep does. Before a line that doesn't
/// follow the last one printed, with context, prints "--".
static void print_line(output_t *out, const char *text, size_t length, size_t line, char separator) {
    if (out->context && out->printed && (out->last == 0 || line > out->last + 1)) {
        fputs("--\n", stdout);
    }
    out->printed = 1;
    out->last = line;
    if (out->name != NULL) {
        printf("%s%c", out->name, separator);
    }
    if (out->numbers) {
        printf("%zu%c", line, separator);
    }
    fwrite(text, 1, length, stdout);
    putchar('\n');
}

/// @brief Keeps a line that wasn't printed in the ring of lines before,
/// reusing the oldest line's string once the ring is full.
static void remember(output_t *out, const char *text, size_t length, size_t line) {
    context_line_t c;
    if (out->before.count == out->before.capacity) {
        sof_ring_pop(&out->before, &c);
        sof_string_clear(&c.text);
    } else if (sof_string_init(&c.text, length, NULL) != SOF_STRING_OK) {
        out->no_mem = 1;
        return;
    }
    if (sof_string_append_bytes(&c.text, text, length) != SOF_STRING_OK) {
        sof_string_free(&c.text);
        out->no_mem = 1;
        return;
    }
    c.line = line;
    sof_ring_push(&out->before, &c);
}

/// @brief Searches one file, or standard input for "-", from its first line.
/// @return 0, or 1 if the file couldn't be read or searched, after logging
/// why.
static int search_file(match_t *m, sof_log_t *log, const char *path) {
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        sof_log_error(log, "can't open %s: %s", path, strerror(errno));
        return 1;
    }
    match_clear(m);
    char buffer[BUFFER_SIZE];
    match_error_t err = MATCH_OK;
    size_t got;
    while (err == MATCH_OK && (got = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        err = match_add(m, buffer, got);
    }
    if (err == MATCH_OK) {
        err = match_flush(m);
    }
    int rc = 0;
    if (err != MATCH_OK) {
        sof_log_error(log, "%s searching line %zu of %s", err == MATCH_NO_MEM ? "out of memory" : "PCRE2 gave up", m->lines, name_of(path));
        rc = 1;
    } else if (ferror(f)) {
        sof_log_error(log, "error reading %s", name_of(path));
        rc = 1;
    } else {
        sof_log_info(log, "%s: %zu lines, %zu selected", name_of(path), m->lines, m->selected);
    }
    if (!is_stdin) {
        fclose(f);
    }
    return rc;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: match [-h] [-i] [-v] [-o] [-c] [-n] [-A n] [-B n] [-L level] pattern [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Prints the lines of the files, or of standard input if there are none or a\n"
        "file is \"-\", that match pattern, a PCRE2 (Perl) regular expression.\n"
        "Options can be combined (-in) and can come after the pattern and files;\n"
        "-- ends them, so put it before a pattern that starts with '-'. Exits with\n"
        "0 if a line was selected, 1 if none was, and 2 on an error. -A and -B\n"
        "are ignored with -o and -c.\n");
}

int main(int ac, char **av) {
    settings_t settings = {SOF_LOG_LEVEL_WARN, 0, 0};
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "unknown option %s\n", av[index]);
        usage(stderr);
        return 2;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "%s needs an argument\n", option->option);
        usage(stderr);
        return 2;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "%s takes no argument\n", option->option);
        usage(stderr);
        return 2;
    case SOF_COPT_STOPPED:
        return 2;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        return 2;
    }
    if (opt_help.present) {
        usage(stdout);
        return 0;
    }
    if (index >= ac) {
        fprintf(stderr, "no pattern given\n");
        usage(stderr);
        return 2;
    }
    const char *pattern = av[index++];

    sof_log_t log;
    if (sof_log_init(&log, stderr, settings.level, "match", NULL) != SOF_LOG_OK) {
        fprintf(stderr, "can't start the log\n");
        return 2;
    }
    output_t out = {0};
    out.numbers = opt_numbers.present;
    // Context goes with whole lines: with -o or -c it is ignored.
    if (!opt_only.present && !opt_count.present) {
        out.after = settings.after;
        out.context = settings.after > 0 || settings.before > 0;
    }
    if (out.context && settings.before > 0 && sof_ring_init(&out.before, sizeof(context_line_t), settings.before, forget, NULL, NULL) != SOF_RING_OK) {
        sof_log_error(&log, "out of memory");
        sof_log_free(&log);
        return 2;
    }
    match_options_t match_options = {opt_ignore_case.present, opt_invert.present, opt_only.present, out.context};
    match_t m;
    // With -c nothing is printed for each line, so there is no found
    // function: the searcher only counts.
    match_error_t m_err = match_init(&m, pattern, &match_options, opt_count.present ? NULL : found, &out, NULL);
    if (m_err != MATCH_OK) {
        if (m_err == MATCH_BAD_REGEX) {
            sof_log_error(&log, "the pattern \"%s\" isn't a valid regular expression", pattern);
        } else {
            sof_log_error(&log, "out of memory");
        }
        sof_ring_free(&out.before);
        sof_log_free(&log);
        return 2;
    }
    sof_log_debug(&log, "searching for \"%s\"", pattern);

    char *standard_input[] = {"-"};
    char **paths = index < ac ? av + index : standard_input;
    int num_paths = index < ac ? ac - index : 1;
    int errors = 0;
    size_t selected = 0;
    for (int i = 0; i < num_paths; i++) {
        out.name = num_paths > 1 ? name_of(paths[i]) : NULL;
        out.last = 0;
        out.after_left = 0;
        out.no_mem = 0;
        if (out.before.ready) {
            sof_ring_clear(&out.before);
        }
        if (search_file(&m, &log, paths[i]) != 0) {
            errors = 1;
            continue;
        }
        if (out.no_mem) {
            sof_log_error(&log, "out of memory keeping lines of context in %s", name_of(paths[i]));
            errors = 1;
        }
        selected += m.selected;
        if (opt_count.present) {
            if (out.name != NULL) {
                printf("%s:", out.name);
            }
            printf("%zu\n", m.selected);
        }
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        sof_log_error(&log, "error writing the output");
        errors = 1;
    }
    match_free(&m);
    sof_ring_free(&out.before);
    sof_log_free(&log);
    if (errors) {
        return 2;
    }
    return selected > 0 ? 0 : 1;
}
