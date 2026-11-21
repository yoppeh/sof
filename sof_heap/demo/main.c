/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Prints the most frequent words, lines or n-grams of words of its
 * input.
 * @details Counts the words, the runs of n words, or the lines, of files or
 * standard input in a top_t, then prints how many there were and the most
 * frequent, picked with a min-heap the size of the number asked for, so
 * memory goes with the distinct keys and the number printed, not the size of
 * the input.
 *
 *     top [-h] [-l] [-i] [-n words] [-r regex] [-t top] [file]...
 *
 * With no file, or a file named "-", the text is read from standard input.
 * Its options are read with sof_copt.
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
#include "top.h"

/// @brief The size of each piece of input read.
#define CHUNK_SIZE 65536

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief Passed to top_init().
    top_options_t options;
    /// @brief The number of most frequent keys to print, or 0 for all.
    size_t top;
} settings_t;

static int count_file(top_t *t, const char *path);
static const char *name_of(const char *path);
static int number_validate(sof_copt_t *option, void *ctx);
static int print_top(const top_t *t, size_t top);
static void usage(FILE *stream);
static int words_validate(sof_copt_t *option, void *ctx);

/// @brief A code the validate callbacks return, which sof_copt keeps in the
/// option's stop_value: its argument is wrong. The callback has printed why.
#define BAD_ARGUMENT 1

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_lines = {
    .option = "-l",
    .description = "count whole lines, not words",
};

static sof_copt_t opt_fold = {
    .option = "-i",
    .description = "ignore case: fold ASCII letters to lowercase",
};

static sof_copt_t opt_words = {
    .option = "-n",
    .description = "count runs of this many words (default 1)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = words_validate,
};

static sof_copt_t opt_regex = {
    .option = "-r",
    .description = "the PCRE2 regular expression a word matches",
    .type = SOF_COPT_ARG_REQUIRED,
};

static sof_copt_t opt_top = {
    .option = "-t",
    .description = "print this many of the most frequent (default 10; 0 for all)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = number_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_lines,
    &opt_fold,
    &opt_words,
    &opt_regex,
    &opt_top,
};

/// @brief Counts one file, or standard input for "-", a line at a time,
/// then ends its text with top_flush().
/// @details The file is read in pieces into a buffer that holds the line in
/// progress and the text read after it; each newline, and a CR just before
/// it, is replaced by a NUL, and the line before it counted. The buffer
/// grows only for a line longer than it.
/// @return 0, or 1 if the file couldn't be read or memory ran out, after
/// printing why.
static int count_file(top_t *t, const char *path) {
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        return 1;
    }
    // buffer[start, used) is the text not yet counted, and none of
    // buffer[start, scan) is a newline. One byte is always kept free, for
    // the NUL after a last line with no newline.
    char *buffer = NULL;
    size_t size = 0, start = 0, scan = 0, used = 0;
    int rc = 0;
    top_error_t err = TOP_OK;
    while (rc == 0 && err == TOP_OK) {
        if (size - used <= CHUNK_SIZE) {
            if (start > 0) {
                memmove(buffer, buffer + start, used - start);
                used -= start;
                scan -= start;
                start = 0;
            }
            if (size - used <= CHUNK_SIZE) {
                size_t new_size = size == 0 ? 4 * CHUNK_SIZE : 2 * size;
                char *grown = sof_alloc_realloc(NULL, buffer, size, new_size);
                if (grown == NULL) {
                    fprintf(stderr, "out of memory\n");
                    rc = 1;
                    break;
                }
                buffer = grown;
                size = new_size;
            }
        }
        size_t got = fread(buffer + used, 1, CHUNK_SIZE, f);
        used += got;
        char *newline;
        while (err == TOP_OK && (newline = memchr(buffer + scan, '\n', used - scan)) != NULL) {
            size_t end = (size_t)(newline - buffer);
            scan = end + 1;
            if (end > start && buffer[end - 1] == '\r') {
                end--;
            }
            buffer[end] = '\0';
            err = top_add_line(t, buffer + start);
            start = scan;
        }
        scan = used;
        if (got == 0) {
            if (err == TOP_OK && start < used) {
                if (buffer[used - 1] == '\r') {
                    used--;
                }
                buffer[used] = '\0';
                err = top_add_line(t, buffer + start);
            }
            break;
        }
    }
    if (err != TOP_OK) {
        fprintf(stderr, err == TOP_NO_MEM ? "out of memory counting %s\n" : "error matching words in %s\n", name_of(path));
        rc = 1;
    }
    if (ferror(f)) {
        fprintf(stderr, "error reading %s\n", name_of(path));
        rc = 1;
    }
    if (!is_stdin) {
        fclose(f);
    }
    sof_alloc_free(NULL, buffer, size);
    top_flush(t);
    return rc;
}

/// @brief Returns a file's name for messages: "standard input" for "-".
static const char *name_of(const char *path) {
    return strcmp(path, "-") == 0 ? "standard input" : path;
}

/// @brief Reads a non-negative decimal number, the argument of -t or -n.
static int number_validate(sof_copt_t *option, void *ctx) {
    size_t *n = option == &opt_top ? &((settings_t *)ctx)->top : &((settings_t *)ctx)->options.n;
    const char *s = option->value;
    char *end;
    errno = 0;
    unsigned long long value = strtoull(s, &end, 10);
    if (*s < '0' || *s > '9' || *end != '\0' || errno != 0 || value > SIZE_MAX) {
        fprintf(stderr, "%s needs a non-negative number, not \"%s\"\n", option->option, s);
        return BAD_ARGUMENT;
    }
    *n = (size_t)value;
    return 0;
}

/// @brief Prints how many keys were counted, and how many were distinct,
/// then the top most frequent of them (all of them if top is 0).
/// @return 0, or 1 if memory ran out, after printing why.
static int print_top(const top_t *t, size_t top) {
    size_t distinct = t->map.count;
    if (t->unit == TOP_LINES) {
        printf("%zu lines, %zu distinct\n", t->total, distinct);
    } else if (t->n == 1) {
        printf("%zu words, %zu distinct\n", t->total, distinct);
    } else {
        printf("%zu %zu-grams of words, %zu distinct\n", t->total, t->n, distinct);
    }
    if (top == 0 || top > distinct) {
        top = distinct;
    }
    if (top == 0) {
        return 0;
    }
    top_entry_t *entries = sof_alloc_calloc(NULL, top, sizeof(top_entry_t));
    size_t found;
    if (entries == NULL || top_select(t, top, entries, &found) != TOP_OK) {
        fprintf(stderr, "out of memory\n");
        sof_alloc_free(NULL, entries, top * sizeof(top_entry_t));
        return 1;
    }
    for (size_t i = 0; i < found; i++) {
        printf("%10zu  %s\n", entries[i].count, entries[i].key);
    }
    sof_alloc_free(NULL, entries, top * sizeof(top_entry_t));
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: top [-h] [-l] [-i] [-n words] [-r regex] [-t top] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
            "Prints the most frequent words, runs of n words, or lines of the files, or\n"
            "of standard input if there are none or a file is \"-\". Keys found equally\n"
            "often are printed in byte order. The default word is\n"
            "  " TOP_DEFAULT_WORD "\n");
}

/// @brief Reads the argument of -n, which must be at least 1.
static int words_validate(sof_copt_t *option, void *ctx) {
    int err = number_validate(option, ctx);
    if (err == 0 && ((settings_t *)ctx)->options.n == 0) {
        fprintf(stderr, "%s needs at least 1 word\n", option->option);
        return BAD_ARGUMENT;
    }
    return err;
}

int main(int ac, char **av) {
    settings_t settings = {{TOP_WORDS, 1, NULL, 0}, 10};
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
    if (opt_lines.present && (opt_words.present || opt_regex.present)) {
        fprintf(stderr, "-l counts lines, so -n and -r don't apply\n");
        usage(stderr);
        return 2;
    }
    settings.options.unit = opt_lines.present ? TOP_LINES : TOP_WORDS;
    settings.options.word = opt_regex.value;
    settings.options.fold_case = opt_fold.present;

    top_t t;
    top_error_t top_err = top_init(&t, &settings.options, NULL);
    if (top_err == TOP_BAD_REGEX) {
        fprintf(stderr, "-r: \"%s\" isn't a regular expression PCRE2 can compile\n", opt_regex.value);
        return 2;
    }
    if (top_err != TOP_OK) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    int rc = 0;
    if (index == ac) {
        rc = count_file(&t, "-");
    }
    for (int i = index; i < ac && rc == 0; i++) {
        rc = count_file(&t, av[i]);
    }
    if (rc == 0) {
        rc = print_top(&t, settings.top);
    }
    top_free(&t);
    return rc;
}
