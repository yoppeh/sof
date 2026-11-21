/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Counts the words of a text through an allocator of its own, and
 * reports every allocation the libraries made.
 * @details Reads the files, or standard input, counts each word in a
 * sof_hashmap, building each word in a sof_string, and sorts the most
 * frequent in a sof_vector, all through a counting sof_alloc_t from
 * memstat.c. It prints the most frequent words, then what the allocator saw:
 * the calls, the bytes live and at the peak, every free of the wrong size or
 * of an unknown block, and a histogram of block sizes.
 *
 *     memstat [-h] [-e count] [-t count] [file]...
 *
 * With no file, or a file named "-", it reads standard input. Its options
 * are read with sof_copt. It exits with 0, 1 if a file couldn't be read,
 * memory ran out, or the allocator found a leak or a bad free, and 2 if the
 * options are wrong.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_copt.h"
#include "sof_hashmap.h"
#include "sof_string.h"
#include "sof_vector.h"
#include "memstat.h"

/// @brief An application code the validate callbacks return when an
/// option's argument is wrong, once they have printed why; sof_copt keeps
/// it in the option's stop_value.
#define BAD_ARGUMENT 1

/// @brief The size of the chunks files are read in.
#define CHUNK_SIZE 65536

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The distinct words to size the map for, from -e.
    size_t expected;
    /// @brief The number of most frequent words to print, from -t.
    size_t top;
} settings_t;

/// @brief The words being counted, and the word being read.
typedef struct counter_t {
    /// @brief Each word, copied by the map, with its count as its data.
    sof_hashmap_t words;
    /// @brief The word being read, which can span chunks.
    sof_string_t word;
    /// @brief The number of words read.
    size_t total;
} counter_t;

/// @brief A word and its count, for sorting.
typedef struct entry_t {
    const char *word;
    size_t count;
} entry_t;

static int add_word(counter_t *counter);
static int by_count(const void *a, const void *b, void *ctx);
static int expected_validate(sof_copt_t *option, void *ctx);
static int is_word_byte(unsigned char c);
static int parse_size(const char *s, size_t *n);
static int print_top(const counter_t *counter, size_t top, const sof_alloc_t *alloc);
static int read_file(counter_t *counter, const char *path);
static int top_validate(sof_copt_t *option, void *ctx);
static void usage(FILE *stream);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_expected = {
    .option = "-e",
    .description = "size the map for this many distinct words (default 0)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = expected_validate,
};

static sof_copt_t opt_top = {
    .option = "-t",
    .description = "print this many of the most frequent words (default 10)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = top_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_expected,
    &opt_top,
};

/// @brief Counts the word in counter->word, lower case, and empties it.
/// @return 0, or 1 if memory ran out.
static int add_word(counter_t *counter) {
    sof_string_tolower(&counter->word);
    sof_hashmap_element_t *e = sof_hashmap_insert(&counter->words, sof_string_cstr(&counter->word), NULL);
    if (e == NULL) {
        return 1;
    }
    e->data = (void *)((uintptr_t)e->data + 1);
    counter->total++;
    sof_string_clear(&counter->word);
    return 0;
}

/// @brief Orders entries by count, highest first, then by word.
static int by_count(const void *a, const void *b, void *ctx) {
    const entry_t *x = a, *y = b;
    (void)ctx;
    if (x->count != y->count) {
        return x->count > y->count ? -1 : 1;
    }
    return strcmp(x->word, y->word);
}

/// @brief Reads the argument of -e.
static int expected_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    if (parse_size(option->value, &settings->expected) != 0) {
        fprintf(stderr, "%s needs a number of words, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Returns 1 if c can be part of a word: an ASCII letter or digit, or
/// any byte of a UTF-8 sequence.
static int is_word_byte(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0x80;
}

/// @brief Reads a non-negative decimal number, the whole of s, into *n.
/// @return 0, or 1 if s isn't such a number or it doesn't fit.
static int parse_size(const char *s, size_t *n) {
    if (*s < '0' || *s > '9') {
        return 1;
    }
    char *end;
    errno = 0;
    unsigned long long value = strtoull(s, &end, 10);
    if (errno != 0 || *end != '\0' || (unsigned long long)(size_t)value != value) {
        return 1;
    }
    *n = (size_t)value;
    return 0;
}

/// @brief Prints the top most frequent words with their counts, sorted in a
/// sof_vector that allocates through alloc.
/// @return 0, or 1 if memory ran out.
static int print_top(const counter_t *counter, size_t top, const sof_alloc_t *alloc) {
    if (top == 0) {
        return 0;
    }
    sof_vector_t entries;
    if (sof_vector_init(&entries, sizeof(entry_t), 0, NULL, NULL, alloc) != SOF_VECTOR_OK) {
        return 1;
    }
    size_t position = 0;
    sof_hashmap_element_t *e;
    while ((e = sof_hashmap_next(&counter->words, &position)) != NULL) {
        entry_t entry = {e->key, (size_t)(uintptr_t)e->data};
        if (sof_vector_push(&entries, &entry) != SOF_VECTOR_OK) {
            sof_vector_free(&entries);
            return 1;
        }
    }
    // The stable sort's buffer comes through alloc too.
    if (sof_vector_sort_stable(&entries, by_count, NULL) != SOF_VECTOR_OK) {
        sof_vector_free(&entries);
        return 1;
    }
    for (size_t i = 0; i < top && i < entries.count; i++) {
        const entry_t *entry = sof_vector_at(&entries, i);
        printf("%10zu  %s\n", entry->count, entry->word);
    }
    sof_vector_free(&entries);
    return 0;
}

/// @brief Counts the words of one file, or standard input for "-", reading
/// it in chunks. A word is a run of the bytes is_word_byte() accepts, so it
/// can span chunks.
/// @return 0, or 1 if the file couldn't be read or memory ran out, after
/// printing why.
static int read_file(counter_t *counter, const char *path) {
    int is_stdin = strcmp(path, "-") == 0;
    const char *name = is_stdin ? "standard input" : path;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        return 1;
    }
    static unsigned char chunk[CHUNK_SIZE];
    int rc = 0;
    size_t got;
    while (rc == 0 && (got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        size_t i = 0;
        while (i < got && rc == 0) {
            size_t start = i;
            while (i < got && is_word_byte(chunk[i])) {
                i++;
            }
            if (i > start && sof_string_append_bytes(&counter->word, chunk + start, i - start) != SOF_STRING_OK) {
                rc = 1;
            } else if (i < got) {
                // A separator ends the word, if there is one.
                if (counter->word.length > 0) {
                    rc = add_word(counter);
                }
                i++;
            }
        }
    }
    if (rc == 0 && counter->word.length > 0) {
        rc = add_word(counter);
    }
    if (rc != 0) {
        fprintf(stderr, "out of memory\n");
    } else if (ferror(f)) {
        fprintf(stderr, "error reading %s\n", name);
        rc = 1;
    }
    if (!is_stdin) {
        fclose(f);
    }
    return rc;
}

/// @brief Reads the argument of -t.
static int top_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    if (parse_size(option->value, &settings->top) != 0) {
        fprintf(stderr, "%s needs a number of words, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: memstat [-h] [-e count] [-t count] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Counts the words of the files, or of standard input if there are none or a\n"
        "file is \"-\", through a counting allocator, prints the most frequent, then\n"
        "every allocation the libraries made: calls, bytes live and at the peak, bad\n"
        "frees and a histogram of block sizes. Exits with 1 on a leak or a bad free.\n");
}

int main(int ac, char **av) {
    settings_t settings = {0, 10};
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

    // Everything below allocates through stat, which gets its own memory
    // from the standard functions.
    memstat_t stat;
    if (memstat_init(&stat, NULL) != MEMSTAT_OK) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    sof_alloc_t alloc = memstat_allocator(&stat);
    counter_t counter = {.total = 0};
    int rc = 1;
    if (sof_hashmap_init(&counter.words, &sof_hashmap_string_keys, NULL, NULL, settings.expected, &alloc) != SOF_HASHMAP_OK) {
        fprintf(stderr, "out of memory\n");
        goto free_stat;
    }
    if (sof_string_init(&counter.word, 0, &alloc) != SOF_STRING_OK) {
        fprintf(stderr, "out of memory\n");
        goto free_words;
    }
    rc = 0;
    if (index == ac) {
        rc = read_file(&counter, "-");
    }
    for (int i = index; i < ac && rc == 0; i++) {
        rc = read_file(&counter, av[i]);
    }
    if (rc == 0) {
        if (print_top(&counter, settings.top, &alloc) != 0) {
            fprintf(stderr, "out of memory\n");
            rc = 1;
        } else {
            printf("%zu words, %zu distinct\n", counter.total, counter.words.count);
        }
    }
    sof_string_free(&counter.word);
free_words:
    sof_hashmap_free(&counter.words);
free_stat:
    // Everything the libraries allocated has been freed, so whatever is still
    // live is a leak.
    memstat_report(&stat, stdout);
    if (stat.blocks > 0 || stat.wrong_sizes > 0 || stat.unknown > 0) {
        fprintf(stderr, "memstat: %zu blocks leaked, %zu bad frees\n", stat.blocks, stat.wrong_sizes + stat.unknown);
        rc = 1;
    }
    memstat_free(&stat);
    return rc;
}
