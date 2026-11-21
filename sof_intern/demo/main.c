/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Prints a concordance of text: each word, in order, with the
 * numbers of the lines it is on.
 * @details Reads files, or standard input, and prints each word once, in
 * byte order, with the number of times it was found and the lines it is on.
 *
 *     index [-h] [-i] [-l length] [file]...
 *
 * With no file, or a file named "-", the text is read from standard input.
 * With several files, each is indexed on its own, under its name. Its
 * options are read with sof_copt.
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
#include "index.h"

/// @brief The size of each piece of input read.
#define READ_SIZE 65536

static int index_file(index_t *ix, const char *path, char *buffer);
static int length_validate(sof_copt_t *option, void *ctx);
static const char *name_of(const char *path);
static void print_index(const index_t *ix);
static void usage(FILE *stream);

/// @brief A code the validate callbacks return, which sof_copt keeps in the
/// option's stop_value: its argument is wrong. The callback has printed why.
#define BAD_ARGUMENT 1

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_fold = {
    .option = "-i",
    .description = "ignore case: fold letters to lowercase",
};

static sof_copt_t opt_length = {
    .option = "-l",
    .description = "index only words of at least this many characters (default 1)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = length_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_fold,
    &opt_length,
};

/// @brief Reads one file, or standard input for "-", into ix, in pieces of
/// READ_SIZE bytes read into buffer.
/// @return 0, or 1 if the file couldn't be read or indexed, after printing
/// why.
static int index_file(index_t *ix, const char *path, char *buffer) {
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        return 1;
    }
    index_error_t err = INDEX_OK;
    size_t got;
    while (err == INDEX_OK && (got = fread(buffer, 1, READ_SIZE, f)) > 0) {
        err = index_add(ix, buffer, got);
    }
    if (err == INDEX_OK) {
        err = index_flush(ix);
    }
    int rc = 0;
    if (err != INDEX_OK) {
        fprintf(stderr, err == INDEX_NO_MEM ? "out of memory indexing %s\n" : "error searching %s for words\n", name_of(path));
        rc = 1;
    } else if (ferror(f)) {
        fprintf(stderr, "error reading %s\n", name_of(path));
        rc = 1;
    }
    if (!is_stdin) {
        fclose(f);
    }
    return rc;
}

/// @brief Reads the argument of -l.
static int length_validate(sof_copt_t *option, void *ctx) {
    index_options_t *settings = ctx;
    const char *s = option->value;
    char *end = NULL;
    unsigned long long value = 0;
    errno = 0;
    if (*s >= '0' && *s <= '9') {
        value = strtoull(s, &end, 10);
    }
    if (end == NULL || *end != '\0' || errno != 0 || value == 0 || value > SIZE_MAX) {
        fprintf(stderr, "%s needs a number of characters, 1 or more, not \"%s\"\n", option->option, s);
        return BAD_ARGUMENT;
    }
    settings->min_length = (size_t)value;
    return 0;
}

/// @brief Returns a file's name for messages: "standard input" for "-".
static const char *name_of(const char *path) {
    return strcmp(path, "-") == 0 ? "standard input" : path;
}

/// @brief Prints each word with its count and its lines.
static void print_index(const index_t *ix) {
    for (const index_entry_t *e = index_first(ix); e != NULL; e = index_next(ix, e)) {
        printf("%s (%zu):", sof_string_cstr(&e->word), e->count);
        for (size_t i = 0; i < e->lines.count; i++) {
            printf("%s %zu", i == 0 ? "" : ",", index_entry_line(e, i));
        }
        printf("\n");
    }
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: index [-h] [-i] [-l length] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Prints each word of the files, or of standard input if there are none or a\n"
        "file is \"-\", in order, with the number of times it occurs and the lines it\n"
        "is on. Text is UTF-8. A word is a run of letters, with an apostrophe allowed\n"
        "between two of them. With several files, each is indexed on its own.\n");
}

int main(int ac, char **av) {
    index_options_t settings = {1, 0};
    int first, rc = 2;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &first, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "unknown option %s\n", av[first]);
        usage(stderr);
        return rc;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "%s needs an argument\n", option->option);
        usage(stderr);
        return rc;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "%s takes no argument\n", option->option);
        usage(stderr);
        return rc;
    case SOF_COPT_STOPPED:
        return rc;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        return rc;
    }
    if (opt_help.present) {
        usage(stdout);
        return 0;
    }
    settings.fold_case = opt_fold.present;

    char *buffer = sof_alloc_malloc(NULL, READ_SIZE);
    index_t ix;
    if (buffer == NULL || index_init(&ix, &settings, NULL) != INDEX_OK) {
        fprintf(stderr, "out of memory\n");
        sof_alloc_free(NULL, buffer, READ_SIZE);
        return 1;
    }
    rc = 0;
    if (first == ac) {
        rc = index_file(&ix, "-", buffer);
        if (rc == 0) {
            print_index(&ix);
        }
    }
    for (int i = first; i < ac; i++) {
        index_clear(&ix);
        if (index_file(&ix, av[i], buffer) != 0) {
            rc = 1;
            continue;
        }
        if (ac - first > 1) {
            printf("%s==> %s <==\n", i == first ? "" : "\n", name_of(av[i]));
        }
        print_index(&ix);
    }
    index_free(&ix);
    sof_alloc_free(NULL, buffer, READ_SIZE);
    return rc;
}
