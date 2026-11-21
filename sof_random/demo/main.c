/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Generates text from the n-grams of words or characters in other
 * text, by a Markov chain.
 * @details Learns every n-gram of words or characters in files or standard
 * input, then prints new text in which each word or character follows the
 * n - 1 before it as often as it did in the input. The text depends only on
 * the input and the seed, so the same seed gives the same text.
 *
 *     markov [-h] [-w] [-i] [-n order] [-c chars] [-l length] [-s seed] [file]...
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
#include "sof_random.h"
#include "sof_string.h"
#include "markov.h"

/// @brief The size of each piece of input read.
#define CHUNK_SIZE 65536

/// @brief The column words are wrapped at.
#define WRAP 72

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief Passed to markov_init().
    markov_options_t options;
    /// @brief The number of words or characters to generate, or 0 for the
    /// default.
    size_t length;
    /// @brief The seed of the random numbers.
    uint64_t seed;
} settings_t;

static int chars_validate(sof_copt_t *option, void *ctx);
static int learn_file(markov_t *m, const char *path, char *buffer);
static int length_validate(sof_copt_t *option, void *ctx);
static const char *name_of(const char *path);
static int order_validate(sof_copt_t *option, void *ctx);
static int parse_u64(const char *s, uint64_t *n);
static void print_text(const sof_string_t *text, int wrap);
static int seed_validate(sof_copt_t *option, void *ctx);
static void usage(FILE *stream);

/// @brief The code the validate callbacks return when an option's argument
/// is wrong, after printing why. sof_copt keeps it in the option's
/// stop_value.
#define BAD_ARGUMENT 1

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_words = {
    .option = "-w",
    .description = "learn and generate words, not characters",
};

static sof_copt_t opt_fold = {
    .option = "-i",
    .description = "ignore case: fold letters to lowercase",
};

static sof_copt_t opt_order = {
    .option = "-n",
    .description = "the gram length: each unit follows the n - 1 before it (default 3 for words, 6 for characters)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = order_validate,
};

static sof_copt_t opt_chars = {
    .option = "-c",
    .description = "the characters words or grams are made of",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = chars_validate,
};

static sof_copt_t opt_length = {
    .option = "-l",
    .description = "how many words or characters to generate (default 100 words, 500 characters)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = length_validate,
};

static sof_copt_t opt_seed = {
    .option = "-s",
    .description = "the seed of the random numbers (default 1)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = seed_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_words,
    &opt_fold,
    &opt_order,
    &opt_chars,
    &opt_length,
    &opt_seed,
};

/// @brief Reads the argument of -c.
static int chars_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    if (option->value[0] == '\0') {
        fprintf(stderr, "%s needs at least one character\n", option->option);
        return BAD_ARGUMENT;
    }
    settings->options.chars = option->value;
    return 0;
}

/// @brief Learns one file, or standard input for "-", reading it into buffer
/// a piece at a time.
/// @return 0, or 1 if the file couldn't be read or memory ran out, after
/// printing why.
static int learn_file(markov_t *m, const char *path, char *buffer) {
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        return 1;
    }
    int rc = 0;
    size_t got;
    while ((got = fread(buffer, 1, CHUNK_SIZE, f)) > 0) {
        if (markov_add(m, buffer, got) != MARKOV_OK) {
            fprintf(stderr, "out of memory reading %s\n", name_of(path));
            rc = 1;
            break;
        }
    }
    if (rc == 0 && ferror(f)) {
        fprintf(stderr, "error reading %s\n", name_of(path));
        rc = 1;
    }
    // No gram spans two files.
    if (rc == 0 && markov_flush(m) != MARKOV_OK) {
        fprintf(stderr, "out of memory reading %s\n", name_of(path));
        rc = 1;
    }
    if (!is_stdin) {
        fclose(f);
    }
    return rc;
}

/// @brief Reads the argument of -l.
static int length_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    uint64_t n;
    if (!parse_u64(option->value, &n) || n == 0 || n > SIZE_MAX) {
        fprintf(stderr, "%s needs a positive number, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    settings->length = (size_t)n;
    return 0;
}

/// @brief Returns a file's name for messages: "standard input" for "-".
static const char *name_of(const char *path) {
    return strcmp(path, "-") == 0 ? "standard input" : path;
}

/// @brief Reads the argument of -n.
static int order_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    uint64_t n;
    if (!parse_u64(option->value, &n) || n < 2 || n > 100) {
        fprintf(stderr, "%s needs a length from 2 to 100, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    settings->options.order = (size_t)n;
    return 0;
}

/// @brief Reads a whole non-negative decimal number.
/// @return 1, or 0 if s isn't one or it is too big.
static int parse_u64(const char *s, uint64_t *n) {
    if (*s < '0' || *s > '9') {
        return 0;
    }
    char *end;
    errno = 0;
    unsigned long long value = strtoull(s, &end, 10);
    if (errno != 0 || *end != '\0') {
        return 0;
    }
    *n = (uint64_t)value;
    return 1;
}

/// @brief Prints the text and a newline. With wrap set, lines are broken at
/// spaces so they fit in WRAP columns where they can; a newline in the text
/// starts a new line too.
static void print_text(const sof_string_t *text, int wrap) {
    const char *s = sof_string_cstr(text);
    if (!wrap) {
        printf("%s\n", s);
        return;
    }
    size_t column = 0;
    while (*s != '\0') {
        size_t word = strcspn(s, " \n");
        if (column > 0 && column + 1 + word > WRAP) {
            putchar('\n');
            column = 0;
        } else if (column > 0) {
            putchar(' ');
            column++;
        }
        fwrite(s, 1, word, stdout);
        column += word;
        s += word;
        if (*s == '\n') {
            putchar('\n');
            column = 0;
        }
        if (*s != '\0') {
            s++;
        }
    }
    putchar('\n');
}

/// @brief Reads the argument of -s.
static int seed_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    if (!parse_u64(option->value, &settings->seed)) {
        fprintf(stderr, "%s needs a non-negative number, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: markov [-h] [-w] [-i] [-n order] [-c chars] [-l length] [-s seed] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Learns the n-grams of characters, or of words, in the files, or in standard\n"
        "input if there are none or a file is \"-\", and prints new text made from\n"
        "them. Text is UTF-8. chars is a set such as a-z0-9' or a-zà-ÿ; by default\n"
        "it is every printable ASCII character, the Latin-1 letters, the em dash and\n"
        "the curly quotes, and for characters the space. Any other character breaks\n"
        "a run of characters, or separates words. The same seed gives the same text.\n");
}

int main(int ac, char **av) {
    settings_t settings = {{NGRAM_CHARS, 0, NULL, 0}, 0, 1};
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
    int words = opt_words.present;
    settings.options.unit = words ? NGRAM_WORDS : NGRAM_CHARS;
    settings.options.fold_case = opt_fold.present;
    if (settings.options.order == 0) {
        settings.options.order = words ? 3 : 6;
    }
    if (settings.length == 0) {
        settings.length = words ? 100 : 500;
    }

    markov_t m;
    markov_error_t m_err = markov_init(&m, &settings.options, NULL);
    if (m_err != MARKOV_OK) {
        if (m_err == MARKOV_NO_MEM) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        fprintf(stderr, words ? "the set given with -c is empty, isn't UTF-8 or holds the space\n" : "the set given with -c is empty or isn't UTF-8\n");
        return 2;
    }
    int rc = 0;
    char *buffer = sof_alloc_malloc(NULL, CHUNK_SIZE);
    if (buffer == NULL) {
        fprintf(stderr, "out of memory\n");
        rc = 1;
    }
    if (rc == 0 && index == ac) {
        rc = learn_file(&m, "-", buffer);
    }
    for (int i = index; i < ac && rc == 0; i++) {
        rc = learn_file(&m, av[i], buffer);
    }
    sof_alloc_free(NULL, buffer, CHUNK_SIZE);

    sof_random_t random;
    sof_random_init(&random, settings.seed);
    sof_string_t text;
    if (rc == 0 && sof_string_init(&text, 0, NULL) != SOF_STRING_OK) {
        fprintf(stderr, "out of memory\n");
        rc = 1;
    }
    if (rc == 0) {
        m_err = markov_generate(&m, &random, settings.length, &text);
        if (m_err == MARKOV_OK) {
            print_text(&text, words);
        } else if (m_err == MARKOV_NO_MATCH) {
            fprintf(stderr, "the input has no %zu %s in a row\n", settings.options.order, words ? "words" : "characters in the set");
            rc = 1;
        } else {
            fprintf(stderr, "out of memory\n");
            rc = 1;
        }
        sof_string_free(&text);
    }
    sof_random_free(&random);
    markov_free(&m);
    return rc;
}
