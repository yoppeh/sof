/**
 * @file bench.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Compares picking the most frequent keys with a heap, as
 * top_select() does, with sorting every key.
 * @details The text is counted three ways, ignoring case: single words, runs
 * of three words, and lines. For each, and for several sizes N of the top,
 * it times top_select(), which keeps the best N in a sof_heap_t, against
 * copying every key and count into an array, sorting it with
 * sof_sort_quick() or sof_sort_merge() in the same order, and taking the
 * first N. The three answers are checked to be the same. Every timing is
 * the best of several runs; counting the text isn't timed.
 *
 * Usage: bench [-h] [file]
 * where file is the text to count (default ../../test-data/shakespeare.txt).
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_copt.h"
#include "sof_sort.h"
#include "sof_timer.h"
#include "top.h"

/// @brief Each timing is the best of this many runs.
#define RUNS 5

/// @brief The text: the file's bytes, each newline (and a CR before it)
/// replaced by a NUL, a pointer to each line, and the sizes the two were
/// allocated with.
typedef struct text_t {
    char *bytes;
    size_t size;
    char **line;
    size_t lines;
    size_t count;
} text_t;

/// @brief A way of picking the top N: fills entries with the best n of t's
/// keys, using all, room for every key, if it needs to.
typedef void (*pick_func_t)(const top_t *t, size_t n, top_entry_t *entries, top_entry_t *all);

static void count_text(top_t *t, const text_t *text, const top_options_t *options);
static void fail(const char *what);
static void free_text(text_t *text);
static size_t gather(const top_t *t, top_entry_t *all);
static int load_text(const char *path, text_t *text);
static void pick_heap(const top_t *t, size_t n, top_entry_t *entries, top_entry_t *all);
static void pick_merge(const top_t *t, size_t n, top_entry_t *entries, top_entry_t *all);
static void pick_quick(const top_t *t, size_t n, top_entry_t *entries, top_entry_t *all);
static double time_pick(pick_func_t pick, const top_t *t, size_t n, top_entry_t *entries, top_entry_t *all);
static void usage(FILE *stream);

/// @brief Every pick's result is added in here, so the compiler can't skip
/// one.
static volatile size_t sink;

/// @brief Times each pick; calibrated once.
static sof_timer_t timer;

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
};

/// @brief Initializes t with options and counts every line of the text.
static void count_text(top_t *t, const text_t *text, const top_options_t *options) {
    if (top_init(t, options, NULL) != TOP_OK) {
        fail("out of memory");
    }
    for (size_t i = 0; i < text->count; i++) {
        if (top_add_line(t, text->line[i]) != TOP_OK) {
            fail("out of memory");
        }
    }
    top_flush(t);
}

/// @brief Prints what went wrong and exits.
static void fail(const char *what) {
    fprintf(stderr, "bench: %s\n", what);
    exit(1);
}

/// @brief Frees what load_text() allocated.
static void free_text(text_t *text) {
    sof_alloc_free(NULL, text->line, text->lines * sizeof(char *));
    sof_alloc_free(NULL, text->bytes, text->size);
}

/// @brief Copies every key and count of t into all.
/// @return The number of keys.
static size_t gather(const top_t *t, top_entry_t *all) {
    size_t count = 0, position = 0;
    const char *key;
    while ((key = top_next(t, &position, &all[count].count)) != NULL) {
        all[count++].key = key;
    }
    return count;
}

/// @brief Reads a whole file and cuts it into lines.
/// @return 0, or -1 if the file can't be read.
static int load_text(const char *path, text_t *text) {
    memset(text, 0, sizeof(*text));
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    size_t capacity = 1 << 20, used = 0;
    char *bytes = sof_alloc_malloc(NULL, capacity);
    while (bytes != NULL) {
        if (used == capacity) {
            char *bigger = sof_alloc_realloc(NULL, bytes, capacity, capacity * 2);
            if (bigger == NULL) {
                sof_alloc_free(NULL, bytes, capacity);
                bytes = NULL;
                break;
            }
            bytes = bigger;
            capacity *= 2;
        }
        size_t n = fread(bytes + used, 1, capacity - used, f);
        if (n == 0) {
            break;
        }
        used += n;
    }
    int error = ferror(f);
    fclose(f);
    if (bytes == NULL) {
        fail("out of memory");
    }
    if (error) {
        sof_alloc_free(NULL, bytes, capacity);
        return -1;
    }
    // One more byte, for a NUL after a last line with no newline.
    char *sized = sof_alloc_realloc(NULL, bytes, capacity, used + 1);
    if (sized == NULL) {
        fail("out of memory");
    }
    bytes = sized;
    bytes[used] = '\0';
    size_t lines = 1;
    for (size_t i = 0; i < used; i++) {
        lines += bytes[i] == '\n';
    }
    char **line = sof_alloc_malloc(NULL, lines * sizeof(char *));
    if (line == NULL) {
        fail("out of memory");
    }
    size_t count = 0, start = 0;
    for (size_t i = 0; i <= used; i++) {
        if (i == used || bytes[i] == '\n') {
            if (i == used && i == start) {
                break;
            }
            size_t end = i;
            if (end > start && bytes[end - 1] == '\r') {
                end--;
            }
            bytes[end] = '\0';
            line[count++] = bytes + start;
            start = i + 1;
        }
    }
    text->bytes = bytes;
    text->size = used + 1;
    text->line = line;
    text->lines = lines;
    text->count = count;
    return 0;
}

/// @brief Picks with top_select(): a heap of n entries.
static void pick_heap(const top_t *t, size_t n, top_entry_t *entries, top_entry_t *all) {
    (void)all;
    if (top_select(t, n, entries, NULL) != TOP_OK) {
        fail("out of memory");
    }
}

/// @brief Picks by sorting every key with merge sort.
static void pick_merge(const top_t *t, size_t n, top_entry_t *entries, top_entry_t *all) {
    size_t count = gather(t, all);
    if (sof_sort_merge(all, count, sizeof(top_entry_t), top_entry_compare, NULL, NULL) != SOF_SORT_OK) {
        fail("out of memory");
    }
    memcpy(entries, all, (n < count ? n : count) * sizeof(top_entry_t));
}

/// @brief Picks by sorting every key with quicksort.
static void pick_quick(const top_t *t, size_t n, top_entry_t *entries, top_entry_t *all) {
    size_t count = gather(t, all);
    sof_sort_quick(all, count, sizeof(top_entry_t), top_entry_compare, NULL);
    memcpy(entries, all, (n < count ? n : count) * sizeof(top_entry_t));
}

/// @brief Returns the best time, in milliseconds, of RUNS picks.
static double time_pick(pick_func_t pick, const top_t *t, size_t n, top_entry_t *entries, top_entry_t *all) {
    double best = 0;
    for (int run = 0; run < RUNS; run++) {
        sof_timer_start(&timer);
        pick(t, n, entries, all);
        sof_timer_stop(&timer);
        double ms = sof_timer_elapsed_ms(&timer);
        best = run == 0 || ms < best ? ms : best;
        sink += entries[0].count + entries[n - 1].count;
    }
    return best;
}

int main(int argc, char **argv) {
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(argc, argv, NULL, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "bench: unknown option %s\n", argv[index]);
        usage(stderr);
        return 2;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "bench: %s takes no argument\n", option->option);
        usage(stderr);
        return 2;
    default:
        fprintf(stderr, "bench: can't read the options (error %d)\n", (int)err);
        return 2;
    }
    if (opt_help.present) {
        usage(stdout);
        return 0;
    }
    if (argc - index > 1) {
        fprintf(stderr, "bench: only one file, please\n");
        usage(stderr);
        return 2;
    }
    const char *path = index < argc ? argv[index] : "../../test-data/shakespeare.txt";
    sof_timer_init(&timer);
    sof_timer_calibrate(&timer);
    text_t text;
    if (load_text(path, &text) != 0) {
        fprintf(stderr, "bench: can't read %s\n", path);
        return 1;
    }
    static const struct {
        const char *label;
        top_options_t options;
    } kinds[] = {
        {"words", {TOP_WORDS, 1, NULL, 1}},
        {"3-grams", {TOP_WORDS, 3, NULL, 1}},
        {"lines", {TOP_LINES, 1, NULL, 1}},
    };
    static const size_t sizes[] = {10, 100, 1000, 10000};
    printf("%zu lines from %s, ignoring case; best of %d runs\n\n", text.count, path, RUNS);
    printf("%-8s %9s %6s %9s %9s %9s %8s\n", "keys", "distinct", "N", "heap ms", "quick ms", "merge ms", "speedup");
    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        top_t t;
        count_text(&t, &text, &kinds[k].options);
        size_t distinct = t.map.count;
        top_entry_t *all = sof_alloc_calloc(NULL, distinct, sizeof(top_entry_t));
        top_entry_t *by_heap = sof_alloc_calloc(NULL, distinct, sizeof(top_entry_t));
        top_entry_t *by_sort = sof_alloc_calloc(NULL, distinct, sizeof(top_entry_t));
        if (all == NULL || by_heap == NULL || by_sort == NULL) {
            fail("out of memory");
        }
        for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]) && sizes[s] <= distinct; s++) {
            size_t n = sizes[s];
            double heap = time_pick(pick_heap, &t, n, by_heap, all);
            double quick = time_pick(pick_quick, &t, n, by_sort, all);
            if (memcmp(by_heap, by_sort, n * sizeof(top_entry_t)) != 0) {
                fail("the heap and quicksort disagree");
            }
            double merge = time_pick(pick_merge, &t, n, by_sort, all);
            if (memcmp(by_heap, by_sort, n * sizeof(top_entry_t)) != 0) {
                fail("the heap and merge sort disagree");
            }
            printf("%-8s %9zu %6zu %9.3f %9.3f %9.3f %7.1fx\n", kinds[k].label, distinct, n, heap, quick, merge, quick / heap);
            fflush(stdout);
        }
        sof_alloc_free(NULL, by_sort, distinct * sizeof(top_entry_t));
        sof_alloc_free(NULL, by_heap, distinct * sizeof(top_entry_t));
        sof_alloc_free(NULL, all, distinct * sizeof(top_entry_t));
        top_free(&t);
    }
    free_text(&text);
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: bench [-h] [file]\n");
    sof_copt_print(stream);
}
