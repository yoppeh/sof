/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Finds the groups of words in a word list that are anagrams of each
 * other, or the anagrams of given words.
 * @details Reads the words of the files, or of standard input, gathers them
 * into groups of anagrams and prints the largest groups first, or, with -w,
 * the anagrams of each word asked for.
 *
 *     anagram [-h] [-i] [-m min] [-t top] [-w word]... [file]...
 *
 * With no file, or a file named "-", the words are read from standard input.
 * A word is a run of bytes that aren't ASCII white space or NULs. Its options
 * are read with sof_copt.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anagram.h"
#include "sof_copt.h"
#include "sof_string.h"

/// @brief The size of each piece of input read.
#define CHUNK_SIZE 65536

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The fewest words a group printed has.
    size_t min;
    /// @brief The number of groups to print, or 0 for all of them.
    size_t top;
    /// @brief The words given with -w, pointing into argv.
    const char **lookups;
    size_t num_lookups;
} settings_t;

static int add_input(anagram_t *an, const char *path);
static int is_space(char c);
static int lookup_validate(sof_copt_t *option, void *ctx);
static int min_validate(sof_copt_t *option, void *ctx);
static const char *name_of(const char *path);
static const char *parse_size(const char *s, size_t *n);
static void print_groups(const anagram_t *an, size_t min, size_t top);
static void print_lookup(const anagram_t *an, const char *word);
static int read_input(sof_string_t *text, const char *path);
static int same_word(const anagram_t *an, const char *a, const char *b);
static int top_validate(sof_copt_t *option, void *ctx);
static void usage(FILE *stream);

/// @brief The code the validate callbacks return when an option's argument
/// is wrong, after printing why. sof_copt keeps it in the option's
/// stop_value.
#define BAD_ARGUMENT 1

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_fold = {
    .option = "-i",
    .description = "ignore case: fold ASCII letters to lowercase in keys",
};

static sof_copt_t opt_min = {
    .option = "-m",
    .description = "print only groups of at least this many words (default 2)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = min_validate,
};

static sof_copt_t opt_top = {
    .option = "-t",
    .description = "print this many of the largest groups (default 10; 0 for all)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = top_validate,
};

static sof_copt_t opt_lookup = {
    .option = "-w",
    .description = "print the anagrams of this word in the list; can be repeated",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = lookup_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_fold,
    &opt_min,
    &opt_top,
    &opt_lookup,
};

/// @brief Reads one file, or standard input for "-", and adds every word in
/// it to an.
/// @return 0, or 1 if the file couldn't be read or memory ran out, after
/// printing why.
static int add_input(anagram_t *an, const char *path) {
    sof_string_t text;
    if (sof_string_init(&text, CHUNK_SIZE, NULL) != SOF_STRING_OK) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    int rc = read_input(&text, path);
    size_t position = 0;
    while (rc == 0 && position < text.length) {
        while (position < text.length && is_space(text.data[position])) {
            position++;
        }
        size_t start = position;
        while (position < text.length && !is_space(text.data[position])) {
            position++;
        }
        if (position == start) {
            break;
        }
        // The word ends in the space after it, or in the NUL after the text,
        // so it can be passed as a c string without copying it.
        text.data[position] = '\0';
        // ANAGRAM_NO_MATCH, a word given twice, is fine.
        if (anagram_add(an, text.data + start) == ANAGRAM_NO_MEM) {
            fprintf(stderr, "out of memory\n");
            rc = 1;
        }
    }
    sof_string_free(&text);
    return rc;
}

/// @brief Returns 1 if c separates words: ASCII white space (a space, tab,
/// newline, vertical tab, form feed or carriage return, whatever the locale)
/// or a NUL.
static int is_space(char c) {
    return c == ' ' || c == '\0' || (c >= '\t' && c <= '\r');
}

/// @brief Adds the argument of -w, each time it is given, to the words to
/// look up.
static int lookup_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    settings->lookups[settings->num_lookups++] = option->value;
    return 0;
}

/// @brief Reads the argument of -m.
static int min_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *end = parse_size(option->value, &settings->min);
    if (end == NULL || *end != '\0' || settings->min == 0) {
        fprintf(stderr, "%s needs a number of words, 1 or more, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Returns a file's name for messages: "standard input" for "-".
static const char *name_of(const char *path) {
    return strcmp(path, "-") == 0 ? "standard input" : path;
}

/// @brief Reads a non-negative decimal number from the start of s.
/// @return Where the number ends, or NULL if s doesn't start with one or it
/// is too big.
static const char *parse_size(const char *s, size_t *n) {
    if (*s < '0' || *s > '9') {
        return NULL;
    }
    char *end;
    errno = 0;
    unsigned long long value = strtoull(s, &end, 10);
    if (errno != 0 || value > SIZE_MAX) {
        return NULL;
    }
    *n = (size_t)value;
    return end;
}

/// @brief Prints how many words and groups there are, then the top largest
/// groups of at least min words (all of them if top is 0). an is sorted.
static void print_groups(const anagram_t *an, size_t min, size_t top) {
    size_t big = 0, position = 0;
    const anagram_group_t *group;
    while ((group = anagram_next(an, &position)) != NULL && group->count >= min) {
        big++;
    }
    printf("words: %zu, groups: %zu, of %zu or more words: %zu\n", an->words, an->count, min, big);
    if (top == 0 || top > big) {
        top = big;
    }
    int width = 1;
    for (size_t largest = top > 0 ? an->groups[0]->count : 0; largest >= 10; largest /= 10) {
        width++;
    }
    position = 0;
    for (size_t i = 0; i < top && (group = anagram_next(an, &position)) != NULL; i++) {
        printf("%*zu ", width, group->count);
        for (size_t w = 0; w < group->count; w++) {
            printf(" %s", group->words[w]);
        }
        printf("\n");
    }
}

/// @brief Prints a word and its anagrams in the list, leaving out the word
/// itself (in any case, if an folds case), or "none".
static void print_lookup(const anagram_t *an, const char *word) {
    const anagram_group_t *group = anagram_find(an, word);
    size_t printed = 0;
    printf("%s:", word);
    for (size_t w = 0; group != NULL && w < group->count; w++) {
        if (!same_word(an, group->words[w], word)) {
            printf(" %s", group->words[w]);
            printed++;
        }
    }
    printf("%s\n", printed == 0 ? " none" : "");
}

/// @brief Reads all of one file, or of standard input for "-", into text.
/// NUL bytes are kept; is_space() counts them as separating words.
/// @return 0, or 1 if the file couldn't be read or memory ran out, after
/// printing why.
static int read_input(sof_string_t *text, const char *path) {
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        return 1;
    }
    char chunk[CHUNK_SIZE];
    int rc = 0;
    size_t got;
    while (rc == 0 && (got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (sof_string_append_bytes(text, chunk, got) != SOF_STRING_OK) {
            fprintf(stderr, "out of memory\n");
            rc = 1;
        }
    }
    if (rc == 0 && ferror(f)) {
        fprintf(stderr, "error reading %s\n", name_of(path));
        rc = 1;
    }
    if (!is_stdin) {
        fclose(f);
    }
    return rc;
}

/// @brief Returns 1 if a and b are the same word: the same bytes, or if an
/// folds case, the same once their ASCII letters are folded, as
/// sof_string_casecmp() compares them.
static int same_word(const anagram_t *an, const char *a, const char *b) {
    if (strcmp(a, b) == 0) {
        return 1;
    }
    if (!an->fold_case) {
        return 0;
    }
    sof_string_t sa = sof_string_view(a, strlen(a));
    sof_string_t sb = sof_string_view(b, strlen(b));
    return sof_string_casecmp(&sa, &sb) == 0;
}

/// @brief Reads the argument of -t.
static int top_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *end = parse_size(option->value, &settings->top);
    if (end == NULL || *end != '\0') {
        fprintf(stderr, "%s needs a non-negative number, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: anagram [-h] [-i] [-m min] [-t top] [-w word]... [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Reads the words of the files, or of standard input if there are none or a\n"
        "file is \"-\", and prints the groups of words that are anagrams of each\n"
        "other, the largest first, or with -w, the anagrams of each word given.\n"
        "Words are separated by white space, and are UTF-8.\n");
}

int main(int ac, char **av) {
    // Every argument could be a -w, so there is room for that many words.
    settings_t settings = {2, 10, sof_alloc_calloc(NULL, (size_t)ac, sizeof(char *)), 0};
    if (settings.lookups == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    int index, rc = 2;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "unknown option %s\n", av[index]);
        usage(stderr);
        goto done;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "%s needs an argument\n", option->option);
        usage(stderr);
        goto done;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "%s takes no argument\n", option->option);
        usage(stderr);
        goto done;
    case SOF_COPT_STOPPED:
        goto done;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        goto done;
    }
    if (opt_help.present) {
        usage(stdout);
        rc = 0;
        goto done;
    }

    rc = 1;
    anagram_t an;
    if (anagram_init(&an, opt_fold.present, NULL) != ANAGRAM_OK) {
        fprintf(stderr, "out of memory\n");
        goto done;
    }
    rc = index == ac ? add_input(&an, "-") : 0;
    for (int i = index; i < ac && rc == 0; i++) {
        rc = add_input(&an, av[i]);
    }
    if (rc == 0) {
        if (settings.num_lookups > 0) {
            for (size_t i = 0; i < settings.num_lookups; i++) {
                print_lookup(&an, settings.lookups[i]);
            }
        } else {
            anagram_sort(&an);
            print_groups(&an, settings.min, settings.top);
        }
    }
    anagram_free(&an);
done:
    sof_alloc_free(NULL, settings.lookups, (size_t)ac * sizeof(char *));
    return rc;
}
