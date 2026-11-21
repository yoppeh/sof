/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Prints a concordance of a text: each word, in order, with how often
 * it appears and the numbers of the lines it is on.
 * @details Reads the files, or standard input, into a concord_t from
 * concord.c, whose memory comes from sof_mem arenas and pools, and prints
 * every word, or those given with -w. With -t, everything is allocated
 * through a sof_mem tracker, and what the allocators held, the tracker's
 * counts, and any leak are reported on standard error.
 *
 *     concord [-h] [-c] [-t] [-w word]... [file]...
 *
 * With no file, or a file named "-", it reads standard input. Several files
 * are read as one text, their lines numbered on from one to the next. Its
 * options are read with sof_copt. It exits with 0, 1 if a file couldn't be
 * read, memory ran out or the tracker found a problem, and 2 if the options
 * are wrong.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_copt.h"
#include "sof_mem.h"
#include "sof_rbtree.h"
#include "sof_string.h"
#include "concord.h"

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The words given with -w, pointing into argv.
    const char **words;
    size_t num_words;
} settings_t;

static int list_leak(void *p, size_t size, void *ctx);
static void print_word(const char *word, const concord_word_t *record, int counts_only);
static int print_words(const concord_t *c, const settings_t *settings, int counts_only);
static int read_input(concord_t *c, char **paths, int num_paths);
static void report_memory(const concord_t *c, const sof_mem_track_t *track);
static void track_problem(const void *p, size_t size, sof_mem_track_problem_t problem, void *ctx);
static void usage(FILE *stream);
static int word_validate(sof_copt_t *option, void *ctx);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_counts = {
    .option = "-c",
    .description = "print only each word's count, not its lines",
};

static sof_copt_t opt_track = {
    .option = "-t",
    .description = "track every allocation, and report memory on standard error",
};

static sof_copt_t opt_word = {
    .option = "-w",
    .description = "print only this word; can be repeated",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = word_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_counts,
    &opt_track,
    &opt_word,
};

/// @brief The sof_mem_track_traverse() callback: prints a block still live
/// after everything was freed.
static int list_leak(void *p, size_t size, void *ctx) {
    (void)ctx;
    fprintf(stderr, "concord: leaked %zu bytes at %p\n", size, p);
    return 0;
}

/// @brief Prints one word, its count and, unless counts_only, the lines it
/// is on. A NULL record is a word that isn't in the text.
static void print_word(const char *word, const concord_word_t *record, int counts_only) {
    printf("%s %zu", word, record == NULL ? 0 : record->count);
    if (!counts_only) {
        putchar(':');
        for (const concord_place_t *p = record == NULL ? NULL : record->first; p != NULL; p = p->next) {
            printf(" %zu", p->line);
        }
    }
    putchar('\n');
}

/// @brief Prints every word in order, or the words given with -w in the
/// order given, each lower case as the concordance keeps them.
/// @return 0, or 1 if memory ran out, after printing why.
static int print_words(const concord_t *c, const settings_t *settings, int counts_only) {
    if (settings->num_words == 0) {
        for (sof_rbtree_node_t *n = sof_rbtree_minimum(&c->tree, NULL); n != NULL; n = sof_rbtree_next(&c->tree, n)) {
            print_word(n->key, n->data, counts_only);
        }
        return 0;
    }
    for (size_t i = 0; i < settings->num_words; i++) {
        sof_string_t word;
        if (sof_string_from_cstr(&word, settings->words[i], NULL) != SOF_STRING_OK) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        sof_string_tolower(&word);
        print_word(word.data, concord_find(c, word.data), counts_only);
        sof_string_free(&word);
    }
    return 0;
}

/// @brief Reads the files, or standard input if there are none, into c.
/// @return 0, or 1 if a file couldn't be read or memory ran out, after
/// printing why.
static int read_input(concord_t *c, char **paths, int num_paths) {
    static char *standard_input[] = {"-"};
    if (num_paths == 0) {
        paths = standard_input;
        num_paths = 1;
    }
    for (int i = 0; i < num_paths; i++) {
        int is_stdin = strcmp(paths[i], "-") == 0;
        FILE *f = is_stdin ? stdin : fopen(paths[i], "rb");
        if (f == NULL) {
            fprintf(stderr, "error opening %s: %s\n", paths[i], strerror(errno));
            return 1;
        }
        concord_error_t err = concord_read(c, f);
        if (!is_stdin) {
            fclose(f);
        }
        if (err == CONCORD_NO_MEM) {
            fprintf(stderr, "out of memory reading %s\n", paths[i]);
            return 1;
        }
        if (err != CONCORD_OK) {
            fprintf(stderr, "error reading %s\n", is_stdin ? "standard input" : paths[i]);
            return 1;
        }
    }
    return 0;
}

/// @brief Prints, on standard error, the words and lines read, what each
/// allocator holds from the tracker, and the tracker's counts.
static void report_memory(const concord_t *c, const sof_mem_track_t *track) {
    fprintf(stderr, "%zu words, %zu distinct, on %zu lines\n", c->total, c->tree.count, c->lines);
    fprintf(stderr, "strings arena  %10zu bytes for the words' text\n", c->strings.allocated);
    fprintf(stderr, "scratch arena  %10zu bytes for a line and its words\n", c->scratch.allocated);
    fprintf(stderr, "node pool      %10zu bytes for %zu tree nodes of %zu bytes\n", c->nodes.allocated, c->nodes.count, c->nodes.object_size);
    fprintf(stderr, "word pool      %10zu bytes for %zu records of %zu bytes\n", c->words.allocated, c->words.count, c->words.object_size);
    fprintf(stderr, "place pool     %10zu bytes for %zu places of %zu bytes\n", c->places.allocated, c->places.count, c->places.object_size);
    fprintf(stderr, "tracker        %10zu bytes live in %zu blocks, peak %zu bytes in %zu blocks\n", track->bytes, track->blocks, track->peak_bytes, track->peak_blocks);
    fprintf(stderr, "               %10zu allocations, %zu reallocations, %zu frees, %zu failures, %zu problems\n", track->allocations, track->reallocations, track->frees, track->failures, track->errors);
}

/// @brief The tracker's error callback: prints each problem it finds.
static void track_problem(const void *p, size_t size, sof_mem_track_problem_t problem, void *ctx) {
    (void)ctx;
    if (problem == SOF_MEM_TRACK_WRONG_SIZE) {
        fprintf(stderr, "concord: block %p freed with the wrong size, %zu\n", p, size);
    } else {
        fprintf(stderr, "concord: block %p freed, of %zu bytes, isn't live\n", p, size);
    }
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: concord [-h] [-c] [-t] [-w word]... [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Reads the files, or standard input if there are none or a file is \"-\", as one\n"
        "text, and prints each word in order, lower case, with its count and the numbers\n"
        "of the lines it is on. A word is a run of ASCII letters, with an apostrophe\n"
        "between two letters.\n");
}

/// @brief Adds the argument of -w, each time it is given, to the words to
/// print.
static int word_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    settings->words[settings->num_words++] = option->value;
    return 0;
}

int main(int ac, char **av) {
    // Every argument could be a -w, so there is room for that many words.
    settings_t settings = {sof_alloc_calloc(NULL, (size_t)ac, sizeof(char *)), 0};
    if (settings.words == NULL) {
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
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        goto done;
    }
    if (opt_help.present) {
        usage(stdout);
        rc = 0;
        goto done;
    }

    // With -t, every allocator gets its memory through the tracker, which
    // gets it from the standard functions.
    sof_mem_track_t track = {.live = NULL};
    sof_alloc_t tracked = {.malloc = NULL};
    const sof_alloc_t *parent = NULL;
    if (opt_track.present) {
        sof_mem_track_init(&track, track_problem, NULL, NULL);
        tracked = sof_mem_track_allocator(&track);
        parent = &tracked;
    }
    concord_t c;
    concord_init(&c, parent);
    rc = read_input(&c, av + index, ac - index);
    if (rc == 0) {
        rc = print_words(&c, &settings, opt_counts.present);
    }
    if (opt_track.present) {
        report_memory(&c, &track);
    }
    concord_free(&c);
    if (opt_track.present) {
        if (track.blocks == 0) {
            fprintf(stderr, "after concord_free(): no blocks live, no leaks\n");
        } else {
            sof_mem_track_traverse(&track, list_leak, NULL);
            rc = 1;
        }
        if (track.errors > 0) {
            rc = 1;
        }
        sof_mem_track_free(&track);
    }
done:
    sof_alloc_free(NULL, settings.words, (size_t)ac * sizeof(char *));
    return rc;
}
