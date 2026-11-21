/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Checks the spelling of text against a dictionary, and suggests the
 * nearest known words for each word it doesn't know.
 * @details Loads a word list into a dictionary (see spell.h), then reads
 * files or standard input, finds the words in them (runs of ASCII letters),
 * and prints each word that isn't in the dictionary, with where
 * it first occurs and the nearest words by edit distance.
 *
 *     spell [-h] [-v] [-d dictionary] [-f corpus] [-n suggestions] [-e distance] [-m length] [file]...
 *
 * With no file, or a file named "-", the text is read from standard input.
 * Its options are read with sof_copt, and -v logs timings, measured with
 * sof_timer, through sof_log.
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
#include "sof_hashmap.h"
#include "sof_log.h"
#include "sof_string.h"
#include "sof_timer.h"
#include "spell.h"

/// @brief The dictionary read when -d isn't given.
#define DEFAULT_DICTIONARY "../../test-data/words.txt"

/// @brief The size of each piece of input read.
#define CHUNK_SIZE 65536

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    const char *dictionary;
    /// @brief The text whose words rank suggestions, or NULL.
    const char *corpus;
    /// @brief The most suggestions printed for a word.
    size_t suggestions;
    /// @brief The most edits a suggestion is from the word.
    size_t distance;
    /// @brief Words shorter than this aren't checked.
    size_t min_length;
} settings_t;

/// @brief Everything the check of the text needs and counts.
typedef struct checker_t {
    const settings_t *settings;
    spell_t *sp;
    const sof_log_t *log;
    /// @brief The words already reported, folded to lowercase, which the map
    /// copies.
    sof_hashmap_t reported;
    /// @brief The word being checked, as it is and then in lowercase.
    sof_string_t word;
    /// @brief Room for settings->suggestions suggestions.
    spell_suggestion_t *found;
    /// @brief Time spent finding suggestions.
    sof_timer_t suggest_timer;
    uint64_t suggest_ns;
    size_t words;
    size_t unknown;
    size_t distinct_unknown;
} checker_t;

static int check_file(checker_t *c, const char *path);
static int check_text(checker_t *c, const char *name, const sof_string_t *text);
static int corpus_validate(sof_copt_t *option, void *ctx);
static int count_corpus(checker_t *c, const char *path);
static int dictionary_validate(sof_copt_t *option, void *ctx);
static int distance_validate(sof_copt_t *option, void *ctx);
static int is_letter(char c);
static int length_validate(sof_copt_t *option, void *ctx);
static const char *name_of(const char *path);
static int next_word(const sof_string_t *text, size_t *position, size_t *offset, size_t *length);
static const char *parse_size(const char *s, size_t *n);
static int read_file(const char *path, sof_string_t *text, const sof_log_t *log);
static int report(checker_t *c, const char *name, size_t line, const char *word, size_t length);
static int suggestions_validate(sof_copt_t *option, void *ctx);
static void usage(FILE *stream);

/// @brief The code the validate callbacks return when an option's argument
/// is wrong, after printing why. sof_copt keeps it in the option's
/// stop_value.
#define BAD_ARGUMENT 1

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_verbose = {
    .option = "-v",
    .description = "log what was loaded and checked, and how long it took",
};

static sof_copt_t opt_dictionary = {
    .option = "-d",
    .description = "the word list, words separated by white space (default " DEFAULT_DICTIONARY ")",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = dictionary_validate,
};

static sof_copt_t opt_corpus = {
    .option = "-f",
    .description = "rank suggestions by how often they occur in this text",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = corpus_validate,
};

static sof_copt_t opt_suggestions = {
    .option = "-n",
    .description = "print at most this many suggestions for a word (default 5)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = suggestions_validate,
};

static sof_copt_t opt_distance = {
    .option = "-e",
    .description = "suggest words at most this many edits away, 0 to 2 (default 2)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = distance_validate,
};

static sof_copt_t opt_length = {
    .option = "-m",
    .description = "don't check words shorter than this (default 2)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = length_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_verbose,
    &opt_dictionary,
    &opt_corpus,
    &opt_suggestions,
    &opt_distance,
    &opt_length,
};

/// @brief Reads one file, or standard input for "-", and checks it.
/// @return 0, or 1 if it couldn't be read or memory ran out, after logging
/// why.
static int check_file(checker_t *c, const char *path) {
    sof_string_t text;
    int rc = read_file(path, &text, c->log);
    if (rc == 0) {
        rc = check_text(c, name_of(path), &text);
    }
    sof_string_free(&text);
    return rc;
}

/// @brief Checks every word in text, reporting each unknown word the first
/// time it is found.
/// @return 0, or 1 if memory ran out, after logging why.
static int check_text(checker_t *c, const char *name, const sof_string_t *text) {
    size_t position = 0, offset, length, line = 1, counted = 0;
    while (next_word(text, &position, &offset, &length)) {
        // Count the lines up to the word.
        const char *nl;
        while ((nl = memchr(text->data + counted, '\n', offset - counted)) != NULL) {
            line++;
            counted = (size_t)(nl - text->data) + 1;
        }
        counted = offset;
        if (length < c->settings->min_length) {
            continue;
        }
        c->words++;
        sof_string_clear(&c->word);
        if (sof_string_append_bytes(&c->word, text->data + offset, length) != SOF_STRING_OK) {
            sof_log_error(c->log, "out of memory");
            return 1;
        }
        // A word is known as it is, or in lowercase, so "The" is "the" but
        // "paris" isn't "Paris".
        if (spell_find(c->sp, c->word.data, NULL) != NULL) {
            continue;
        }
        sof_string_tolower(&c->word);
        if (spell_find(c->sp, c->word.data, NULL) != NULL) {
            continue;
        }
        c->unknown++;
        if (sof_hashmap_find(&c->reported, c->word.data) != NULL) {
            continue;
        }
        if (report(c, name, line, text->data + offset, length) != 0) {
            return 1;
        }
    }
    return 0;
}

/// @brief Reads the argument of -f.
static int corpus_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    settings->corpus = option->value;
    return 0;
}

/// @brief Reads a text and adds each of its words that is in the
/// dictionary, in lowercase, to the dictionary again, so the words that
/// occur most often in it rank first among suggestions at the same distance.
/// @return 0, or 1 if it couldn't be read or memory ran out, after logging
/// why.
static int count_corpus(checker_t *c, const char *path) {
    sof_string_t text;
    int rc = read_file(path, &text, c->log);
    size_t position = 0, offset, length, counted = 0;
    while (rc == 0 && next_word(&text, &position, &offset, &length)) {
        sof_string_clear(&c->word);
        if (sof_string_append_bytes(&c->word, text.data + offset, length) != SOF_STRING_OK) {
            sof_log_error(c->log, "out of memory");
            rc = 1;
            break;
        }
        sof_string_tolower(&c->word);
        if (spell_find(c->sp, c->word.data, NULL) == NULL) {
            continue;
        }
        if (spell_add(c->sp, c->word.data) != SPELL_OK) {
            sof_log_error(c->log, "out of memory");
            rc = 1;
            break;
        }
        counted++;
    }
    if (rc == 0) {
        sof_log_info(c->log, "%zu words of %s are in the dictionary", counted, name_of(path));
    }
    sof_string_free(&text);
    return rc;
}

/// @brief Reads the argument of -d.
static int dictionary_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    settings->dictionary = option->value;
    return 0;
}

/// @brief Reads the argument of -e.
static int distance_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *end = parse_size(option->value, &settings->distance);
    if (end == NULL || *end != '\0' || settings->distance > SPELL_MAX_DISTANCE) {
        fprintf(stderr, "%s needs a distance from 0 to %d, not \"%s\"\n", option->option, SPELL_MAX_DISTANCE, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Returns 1 if c is an ASCII letter, whatever the locale.
static int is_letter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/// @brief Reads the argument of -m.
static int length_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *end = parse_size(option->value, &settings->min_length);
    if (end == NULL || *end != '\0') {
        fprintf(stderr, "%s needs a non-negative number, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Returns a file's name for messages: "standard input" for "-".
static const char *name_of(const char *path) {
    return strcmp(path, "-") == 0 ? "standard input" : path;
}

/// @brief Finds the next word, a run of ASCII letters, in text at or after
/// *position, and moves *position past it.
/// @return 1, with *offset and *length set to the word, or 0 if there are no
/// more words.
static int next_word(const sof_string_t *text, size_t *position, size_t *offset, size_t *length) {
    size_t i = *position;
    while (i < text->length && !is_letter(text->data[i])) {
        i++;
    }
    if (i == text->length) {
        *position = i;
        return 0;
    }
    *offset = i;
    while (i < text->length && is_letter(text->data[i])) {
        i++;
    }
    *length = i - *offset;
    *position = i;
    return 1;
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

/// @brief Reads a whole file, or standard input for "-", into text, which
/// the caller frees whatever this returns.
/// @return 0, or 1 if the file couldn't be read or memory ran out, after
/// logging why.
static int read_file(const char *path, sof_string_t *text, const sof_log_t *log) {
    if (sof_string_init(text, CHUNK_SIZE, NULL) != SOF_STRING_OK) {
        sof_log_error(log, "out of memory");
        return 1;
    }
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        sof_log_error(log, "can't open %s: %s", path, strerror(errno));
        return 1;
    }
    char buffer[CHUNK_SIZE];
    size_t got;
    int rc = 0;
    while (rc == 0 && (got = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        if (sof_string_append_bytes(text, buffer, got) != SOF_STRING_OK) {
            sof_log_error(log, "out of memory reading %s", name_of(path));
            rc = 1;
        }
    }
    if (rc == 0 && ferror(f)) {
        sof_log_error(log, "error reading %s", name_of(path));
        rc = 1;
    }
    if (!is_stdin) {
        fclose(f);
    }
    return rc;
}

/// @brief Prints an unknown word, where it is, and the nearest words, and
/// remembers it so it is reported once. c->word holds it in lowercase.
/// @return 0, or 1 if memory ran out, after logging why.
static int report(checker_t *c, const char *name, size_t line, const char *word, size_t length) {
    size_t found = 0;
    spell_error_t err = SPELL_OK;
    sof_timer_start(&c->suggest_timer);
    // Two edits take thousands of times as long as one, so they are tried
    // only when one finds nothing.
    for (size_t d = c->settings->distance < 1 ? c->settings->distance : 1; d <= c->settings->distance; d++) {
        err = spell_suggest(c->sp, c->word.data, d, c->found, c->settings->suggestions, &found);
        if (err != SPELL_OK || found > 0) {
            break;
        }
    }
    sof_timer_stop(&c->suggest_timer);
    c->suggest_ns += sof_timer_elapsed_ns(&c->suggest_timer);
    if (err != SPELL_OK) {
        sof_log_error(c->log, "out of memory");
        return 1;
    }
    printf("%s:%zu: %.*s:", name, line, (int)length, word);
    size_t shown = found < c->settings->suggestions ? found : c->settings->suggestions;
    for (size_t i = 0; i < shown; i++) {
        printf("%s %s", i == 0 ? "" : ",", c->found[i].word);
    }
    printf("\n");
    c->distinct_unknown++;
    if (sof_hashmap_insert(&c->reported, c->word.data, NULL) == NULL) {
        sof_log_error(c->log, "out of memory");
        return 1;
    }
    return 0;
}

/// @brief Reads the argument of -n.
static int suggestions_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *end = parse_size(option->value, &settings->suggestions);
    if (end == NULL || *end != '\0' || settings->suggestions > 1000) {
        fprintf(stderr, "%s needs a number from 0 to 1000, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: spell [-h] [-v] [-d dictionary] [-f corpus] [-n suggestions] [-e distance] [-m length] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Checks the words in the files, or in standard input if there are none or a\n"
        "file is \"-\", against the dictionary. A word is a run of ASCII letters, and is\n"
        "known if it is in the dictionary as it is or in lowercase. Each unknown word\n"
        "is printed once, where it first occurs, with the nearest known words: those\n"
        "one edit away (a letter deleted, inserted or replaced, or two swapped), or if\n"
        "there are none, two. They are ranked by distance, then by how often they occur\n"
        "in the corpus given with -f, then in byte order.\n");
}

int main(int ac, char **av) {
    settings_t settings = {DEFAULT_DICTIONARY, NULL, 5, 2, 2};
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

    sof_log_t log;
    if (sof_log_init(&log, stderr, opt_verbose.present ? SOF_LOG_LEVEL_INFO : SOF_LOG_LEVEL_WARN, "spell", NULL) != SOF_LOG_OK) {
        fprintf(stderr, "can't start the log\n");
        return 1;
    }
    checker_t c = {.settings = &settings, .log = &log};
    spell_t sp;
    sof_string_t text = {0};
    sof_timer_t timer;
    sof_timer_init(&timer);
    sof_timer_init(&c.suggest_timer);
    sof_timer_calibrate(&c.suggest_timer);
    int rc = 1;

    if (spell_init(&sp, NULL) != SPELL_OK) {
        sof_log_error(&log, "out of memory");
        goto done;
    }
    c.sp = &sp;
    sof_timer_start(&timer);
    if (read_file(settings.dictionary, &text, &log) != 0) {
        goto done;
    }
    if (spell_load(&sp, text.data, text.length) != SPELL_OK) {
        sof_log_error(&log, "out of memory loading %s", settings.dictionary);
        goto done;
    }
    sof_timer_stop(&timer);
    sof_log_info(&log, "loaded %zu words from %s in %.1f ms", sp.words.count, name_of(settings.dictionary), sof_timer_elapsed_ms(&timer));
    sof_string_free(&text);

    if (sof_hashmap_init(&c.reported, &sof_hashmap_string_keys, NULL, NULL, 0, NULL) != SOF_HASHMAP_OK || sof_string_init(&c.word, 64, NULL) != SOF_STRING_OK) {
        sof_log_error(&log, "out of memory");
        goto done;
    }
    if (settings.corpus != NULL) {
        sof_timer_start(&timer);
        if (count_corpus(&c, settings.corpus) != 0) {
            goto done;
        }
        sof_timer_stop(&timer);
        sof_log_info(&log, "counted the words of %s in %.1f ms", name_of(settings.corpus), sof_timer_elapsed_ms(&timer));
    }
    if (settings.suggestions > 0) {
        c.found = sof_alloc_calloc(NULL, settings.suggestions, sizeof(spell_suggestion_t));
        if (c.found == NULL) {
            sof_log_error(&log, "out of memory");
            goto done;
        }
    }
    sof_timer_start(&timer);
    rc = 0;
    if (index == ac) {
        rc = check_file(&c, "-");
    }
    for (int i = index; i < ac && rc == 0; i++) {
        rc = check_file(&c, av[i]);
    }
    sof_timer_stop(&timer);
    if (rc == 0) {
        sof_log_info(&log, "checked %zu words in %.1f ms: %zu unknown, %zu of them different", c.words, sof_timer_elapsed_ms(&timer), c.unknown, c.distinct_unknown);
        sof_log_info(&log, "found suggestions in %.1f ms, %.3f ms a word", (double)c.suggest_ns / 1e6, c.distinct_unknown == 0 ? 0.0 : (double)c.suggest_ns / 1e6 / (double)c.distinct_unknown);
    }

done:
    sof_alloc_free(NULL, c.found, settings.suggestions * sizeof(spell_suggestion_t));
    sof_string_free(&c.word);
    sof_hashmap_free(&c.reported);
    sof_string_free(&text);
    spell_free(&sp);
    sof_log_free(&log);
    return rc;
}
