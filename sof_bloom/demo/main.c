/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Removes duplicate lines without sorting, keeping the first of each.
 * @details Prints each different line of files or standard input once, in
 * the order each was first seen, however far apart its copies are. With -c,
 * each line is printed with the number of times it occurs; with -d, only the
 * lines that occur more than once are printed. With -a, the lines seen are
 * kept in a fixed-size Bloom filter instead of in full, so memory doesn't
 * grow with the input, at the price of now and then taking a new line for
 * one seen before and leaving it out; -b makes it a blocked filter, faster
 * but a little larger.
 *
 *     uniq [-h] [-a [-b] [-n lines] [-p rate] | -c | -d] [file]...
 *
 * With no file, or a file named "-", the lines are read from standard input.
 * Its options are read with sof_copt.
 *
 * A line is the bytes up to a newline, which can include NULs, so binary
 * data works too. Lines are kept in a uniq_t, keyed by their bytes. Without
 * -c or -d, each line is printed as soon as it is first seen; with either,
 * nothing is printed until all the input is read, since the counts aren't
 * known before. With -a, the lines are added to a sof_bloom_t instead, and
 * each is printed if the filter didn't already contain it.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_bloom.h"
#include "sof_copt.h"
#include "sof_string.h"
#include "uniq.h"

/// @brief The size of each piece of input read.
#define CHUNK_SIZE 65536

/// @brief An application code the validate callbacks return when an
/// option's argument is wrong, once they have printed why; sof_copt keeps
/// it in the option's stop_value.
#define BAD_ARGUMENT 1

/// @brief What the options ask for.
typedef struct settings_t {
    /// @brief Print each line's count before it.
    int count;
    /// @brief Print only the lines that occur more than once.
    int duplicates;
    /// @brief The lines -a sizes the filter for.
    size_t expected;
    /// @brief The false positive rate -a sizes the filter for.
    double rate;
    /// @brief With -a, the filter the lines are added to; otherwise NULL.
    sof_bloom_t *bloom;
} settings_t;

static int expected_validate(sof_copt_t *option, void *ctx);
static int add_line(uniq_t *u, const settings_t *settings, const char *bytes, size_t length);
static const char *name_of(const char *path);
static int print_lines(const uniq_t *u, const settings_t *settings);
static int rate_validate(sof_copt_t *option, void *ctx);
static int read_file(uniq_t *u, const settings_t *settings, sof_string_t *line, const char *path);
static void usage(FILE *stream);
static int write_line(const char *bytes, size_t length);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_approximate = {
    .option = "-a",
    .description = "approximate: keep the lines seen in a fixed-size Bloom filter",
};

static sof_copt_t opt_approximate_long = {
    .option = "--approximate",
    .description = "the same as -a",
};

static sof_copt_t opt_blocked = {
    .option = "-b",
    .description = "with -a, use a blocked filter: faster, but larger for the same rate",
};

static sof_copt_t opt_expected = {
    .option = "-n",
    .description = "with -a, the different lines to size the filter for (default 1000000)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = expected_validate,
};

static sof_copt_t opt_rate = {
    .option = "-p",
    .description = "with -a, the chance of dropping a new line once -n lines are seen (default 0.001)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = rate_validate,
};

static sof_copt_t opt_count = {
    .option = "-c",
    .description = "print the number of times each line occurs before it",
};

static sof_copt_t opt_duplicates = {
    .option = "-d",
    .description = "print only the lines that occur more than once",
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_approximate,
    &opt_approximate_long,
    &opt_blocked,
    &opt_expected,
    &opt_rate,
    &opt_count,
    &opt_duplicates,
};

/// @brief Adds one line, and prints it if it is new and the lines are
/// printed as they come.
/// @return 0, or 1 if memory ran out or the line couldn't be written, after
/// printing why.
static int add_line(uniq_t *u, const settings_t *settings, const char *bytes, size_t length) {
    int created;
    if (settings->bloom != NULL) {
        // The filter's memory is all allocated by sof_bloom_init(), so an
        // add can't fail here.
        sof_bloom_add(settings->bloom, bytes, length, &created);
        return created ? write_line(bytes, length) : 0;
    }
    if (uniq_add(u, bytes, length, &created) == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    if (created && !settings->count && !settings->duplicates) {
        return write_line(bytes, length);
    }
    return 0;
}

/// @brief Reads the argument of -n.
static int expected_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *s = option->value;
    char *end;
    errno = 0;
    unsigned long long value = strtoull(s, &end, 10);
    if (*s < '0' || *s > '9' || *end != '\0' || errno != 0 || value == 0 || value > SIZE_MAX) {
        fprintf(stderr, "%s needs a number of lines, 1 or more, not \"%s\"\n", option->option, s);
        return BAD_ARGUMENT;
    }
    settings->expected = (size_t)value;
    return 0;
}

/// @brief Returns a file's name for messages: "standard input" for "-".
static const char *name_of(const char *path) {
    return strcmp(path, "-") == 0 ? "standard input" : path;
}

/// @brief Prints the lines, once all are read, as -c and -d ask: in the
/// order they were first seen, those found once left out with -d, and each
/// after its count with -c.
/// @return 0, or 1 if a line couldn't be written, after printing why.
static int print_lines(const uniq_t *u, const settings_t *settings) {
    size_t position = 0;
    const uniq_line_t *line;
    while ((line = uniq_next(u, &position)) != NULL) {
        if (settings->duplicates && line->count < 2) {
            continue;
        }
        if (settings->count && printf("%7zu ", line->count) < 0) {
            fprintf(stderr, "error writing output\n");
            return 1;
        }
        if (write_line(line->line.data, line->line.length) != 0) {
            return 1;
        }
    }
    return 0;
}

/// @brief Reads the argument of -p.
static int rate_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    char *end;
    double value = strtod(option->value, &end);
    if (end == option->value || *end != '\0' || !(value > 0.0 && value < 1.0)) {
        fprintf(stderr, "%s needs a rate between 0 and 1, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    settings->rate = value;
    return 0;
}

/// @brief Reads the lines of one file, or of standard input for "-", into
/// u. A last line with no newline after it is a line too.
/// @param line Holds the start of a line that runs past the end of one
/// piece of input, until its end is read.
/// @return 0, 1 if the file couldn't be opened or read, or -1 if memory ran
/// out or a line couldn't be written, so the program can't go on, after
/// printing why.
static int read_file(uniq_t *u, const settings_t *settings, sof_string_t *line, const char *path) {
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        return 1;
    }
    char chunk[CHUNK_SIZE];
    int rc = 0;
    size_t got;
    sof_string_clear(line);
    while (rc == 0 && (got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        size_t start = 0;
        while (rc == 0 && start < got) {
            char *end = memchr(chunk + start, '\n', got - start);
            size_t length = (end == NULL ? got : (size_t)(end - chunk)) - start;
            if (end != NULL && line->length == 0) {
                // The whole line is in the chunk: add it from there.
                rc = add_line(u, settings, chunk + start, length);
            } else {
                if (sof_string_append_bytes(line, chunk + start, length) != SOF_STRING_OK) {
                    fprintf(stderr, "out of memory\n");
                    rc = 1;
                } else if (end != NULL) {
                    rc = add_line(u, settings, line->data, line->length);
                    sof_string_clear(line);
                }
            }
            start += length + 1;
        }
    }
    // The lines read before an error are kept, the last one included.
    int read_error = ferror(f);
    if (rc == 0 && line->length > 0) {
        rc = add_line(u, settings, line->data, line->length);
    }
    if (!is_stdin) {
        fclose(f);
    }
    if (rc != 0) {
        return -1;
    }
    if (read_error) {
        fprintf(stderr, "error reading %s\n", name_of(path));
        return 1;
    }
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: uniq [-h] [-a [-b] [-n lines] [-p rate] | -c | -d] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Prints each different line of the files, or of standard input if there\n"
        "are none or a file is \"-\", once, in the order each was first seen. Lines\n"
        "needn't be sorted or next to each other to be found the same, and can hold\n"
        "any bytes, NULs included. With -a, memory stays the same however much is\n"
        "read, but a few different lines may be taken for ones seen before and left\n"
        "out.\n");
}

/// @brief Writes length bytes of a line, then a newline.
/// @return 0, or 1 if writing failed, after printing why.
static int write_line(const char *bytes, size_t length) {
    if (fwrite(bytes, 1, length, stdout) != length || putchar('\n') == EOF) {
        fprintf(stderr, "error writing output\n");
        return 1;
    }
    return 0;
}

int main(int ac, char **av) {
    settings_t settings = {0, 0, 1000000, 0.001, NULL};
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
        // A callback returned BAD_ARGUMENT, the only code, and said why.
        return 2;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        return 2;
    }
    if (opt_help.present) {
        usage(stdout);
        return 0;
    }
    settings.count = opt_count.present;
    settings.duplicates = opt_duplicates.present;
    int approximate = opt_approximate.present || opt_approximate_long.present;
    if (approximate && (settings.count || settings.duplicates)) {
        fprintf(stderr, "-a can't count lines, so it can't be used with -c or -d\n");
        usage(stderr);
        return 2;
    }
    if (!approximate && (opt_blocked.present || opt_expected.present || opt_rate.present)) {
        fprintf(stderr, "-b, -n and -p are only for -a\n");
        usage(stderr);
        return 2;
    }

    uniq_t u;
    sof_string_t line;
    // Left empty without -a, which sof_bloom_free() accepts.
    sof_bloom_t bloom = {0};
    if (uniq_init(&u, 0, NULL) != UNIQ_OK || sof_string_init(&line, 0, NULL) != SOF_STRING_OK) {
        fprintf(stderr, "out of memory\n");
        uniq_free(&u);
        return 1;
    }
    if (approximate) {
        if (sof_bloom_init(&bloom, settings.expected, settings.rate, opt_blocked.present ? SOF_BLOOM_BLOCKED : 0, NULL) != SOF_BLOOM_OK) {
            fprintf(stderr, "out of memory for a filter of %zu lines\n", settings.expected);
            sof_string_free(&line);
            uniq_free(&u);
            return 1;
        }
        settings.bloom = &bloom;
    }
    int rc = 0, fatal = 0;
    if (index == ac) {
        rc = read_file(&u, &settings, &line, "-");
        fatal = rc < 0;
    }
    // A file that can't be read is reported, and the others are still read.
    for (int i = index; i < ac && !fatal; i++) {
        int file_rc = read_file(&u, &settings, &line, av[i]);
        if (file_rc != 0) {
            rc = file_rc;
            fatal = file_rc < 0;
        }
    }
    rc = rc != 0;
    if (!fatal && (settings.count || settings.duplicates)) {
        if (print_lines(&u, &settings) != 0) {
            rc = 1;
        }
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "error writing output\n");
        rc = 1;
    }
    sof_string_free(&line);
    uniq_free(&u);
    sof_bloom_free(&bloom);
    return rc;
}
