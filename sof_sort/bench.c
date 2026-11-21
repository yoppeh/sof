/**
 * @file bench.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Measures the sorts against each other and the c library's qsort().
 * @details The three measures are:
 * - random: arrays of random uint32_t, from 10 elements up to the -n count,
 *   in nanoseconds per element.
 * - patterns: arrays of the -n count that are sorted, reversed, all equal,
 *   organ pipe (up, then down) or random.
 * - words: every word in a word list, as an array of char pointers compared
 *   with strcmp(), shuffled.
 *
 * Bubble and insertion sort are only run on arrays of up to 10,000
 * elements. Small arrays are timed many at a time. Every timing is the
 * best of several runs.
 *
 * Usage: bench [-h] [-n count] [words]
 * where count is the largest array (default 1000000) and words is a file
 * with a word on each line (default ../test-data/words.txt).
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sof_alloc.h"
#include "sof_copt.h"
#include "sof_random.h"
#include "sof_sort.h"

/// @brief Each timing is the best of this many runs.
#define RUNS 5

/// @brief The largest array bubble and insertion sort are given.
#define QUADRATIC_MAX 10000

/// @brief Arrays smaller than this are timed as many arrays, together
/// holding at least this many elements.
#define BATCH 16384

/// @brief The code count_validate() returns when an option's argument isn't
/// a number in range, after printing why. sof_copt keeps it in the
/// option's stop_value.
#define BAD_NUMBER 1

/// @brief A sort function with the arguments all of them share.
typedef sof_sort_error_t (*sort_func_t)(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx);

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The largest array to sort.
    size_t count;
} settings_t;

static void bench_patterns(size_t n);
static void bench_random(size_t max);
static void bench_words(const char *path);
static int compare_string(const void *a, const void *b, void *ctx);
static int compare_string_qsort(const void *a, const void *b);
static int compare_u32(const void *a, const void *b, void *ctx);
static int compare_u32_qsort(const void *a, const void *b);
static void free_words(char **words, size_t count, size_t capacity);
static char **load_words(const char *path, size_t *count, size_t *capacity);
static sof_sort_error_t merge_standard(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx);
static double now(void);
static int count_missing(sof_copt_t *option, void *ctx);
static int count_validate(sof_copt_t *option, void *ctx);
static void print_header(const char *first);
static void print_row(const char *label, const void *input, void *work, size_t n, size_t total, size_t size, sof_sort_compare_func_t cmp_func);
static sof_sort_error_t qsort_wrapper(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx);
static double time_sort(sort_func_t sort, const void *input, void *work, size_t n, size_t total, size_t size, sof_sort_compare_func_t cmp_func);
static void usage(FILE *stream);

/// @brief Every result is added in here, so the compiler can't skip a sort.
static volatile uint64_t sink;

/// @brief The random input and the word order come from here, with a fixed
/// seed, so every run measures the same input.
static sof_random_t rng;

/// @brief The sorts compared, with qsort() last.
static const struct {
    const char *name;
    sort_func_t sort;
    int quadratic;
} sorts[] = {
    {"bubble", sof_sort_bubble, 1},
    {"insertion", sof_sort_insertion, 1},
    {"heap", sof_sort_heap, 0},
    {"merge", merge_standard, 0},
    {"quick", sof_sort_quick, 0},
    {"qsort", qsort_wrapper, 0},
};
#define NUM_SORTS (sizeof(sorts) / sizeof(sorts[0]))

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_count = {
    .option = "-n",
    .description = "sort arrays of up to this many elements (default 1000000)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = count_validate,
    .missing = count_missing,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_count,
};

/// @brief Compares two char pointers' strings.
static int compare_string(const void *a, const void *b, void *ctx) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int compare_string_qsort(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int compare_u32(const void *a, const void *b, void *ctx) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static int compare_u32_qsort(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/// @brief Without -n, sorts up to a million elements.
static int count_missing(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    settings->count = 1000000;
    return 0;
}

/// @brief Reads the argument of -n.
static int count_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *s = option->value;
    char *end;
    errno = 0;
    unsigned long long value = s[0] >= '0' && s[0] <= '9' ? strtoull(s, &end, 10) : 0;
    if (value < 10 || value > 100000000 || errno != 0 || *end != '\0') {
        fprintf(stderr, "bench: %s wants a count from 10 to 100000000, not \"%s\"\n", option->option, s);
        return BAD_NUMBER;
    }
    settings->count = (size_t)value;
    return 0;
}

/// @brief Frees the count strings in words and the array, which has room for
/// capacity strings.
static void free_words(char **words, size_t count, size_t capacity) {
    for (size_t i = 0; i < count; i++) {
        sof_alloc_free(NULL, words[i], strlen(words[i]) + 1);
    }
    sof_alloc_free(NULL, words, capacity * sizeof(char *));
}

/// @brief Reads a word list into an array of strings, in random order.
/// @details The array is allocated with room for *capacity strings, which
/// free_words() frees it with. Returns NULL if the file can't be opened.
static char **load_words(const char *path, size_t *count, size_t *capacity) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return NULL;
    }
    size_t n = 0;
    *capacity = 1 << 16;
    char **words = sof_alloc_malloc(NULL, *capacity * sizeof(char *));
    char line[256];
    while (words != NULL && fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }
        if (n == *capacity) {
            char **bigger = sof_alloc_realloc(NULL, words, *capacity * sizeof(char *), *capacity * 2 * sizeof(char *));
            if (bigger == NULL) {
                free_words(words, n, *capacity);
                words = NULL;
                break;
            }
            words = bigger;
            *capacity *= 2;
        }
        if ((words[n] = sof_alloc_strndup(NULL, line, strlen(line))) != NULL) {
            n++;
        }
    }
    fclose(f);
    if (words == NULL) {
        fprintf(stderr, "bench: out of memory\n");
        exit(1);
    }
    sof_random_shuffle(&rng, words, n, sizeof(char *));
    *count = n;
    return words;
}

/// @brief sof_sort_merge() with the standard allocator, to fit sort_func_t.
static sof_sort_error_t merge_standard(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx) {
    return sof_sort_merge(base, count, size, cmp_func, ctx, NULL);
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/// @brief qsort() to fit sort_func_t. qsort()'s compare takes no ctx, so
/// this picks the one matching cmp_func.
static sof_sort_error_t qsort_wrapper(void *base, size_t count, size_t size, sof_sort_compare_func_t cmp_func, void *ctx) {
    qsort(base, count, size, cmp_func == compare_string ? compare_string_qsort : compare_u32_qsort);
    return SOF_SORT_OK;
}

/// @brief Returns the best time, in seconds, that sort takes to sort the
/// arrays of n elements of size bytes in input, total elements in all.
/// @details Each run copies input to work and sorts it, n at a time; only
/// the sorting is timed.
static double time_sort(sort_func_t sort, const void *input, void *work, size_t n, size_t total, size_t size, sof_sort_compare_func_t cmp_func) {
    double best = 1e30;
    for (int run = 0; run < RUNS; run++) {
        memcpy(work, input, total * size);
        double start = now();
        for (size_t offset = 0; offset < total; offset += n) {
            if (sort((unsigned char *)work + offset * size, n, size, cmp_func, NULL) != SOF_SORT_OK) {
                fprintf(stderr, "bench: out of memory\n");
                exit(1);
            }
        }
        double elapsed = now() - start;
        sink += ((unsigned char *)work)[(n / 2) * size];
        if (elapsed < best) {
            best = elapsed;
        }
    }
    return best;
}

/// @brief Prints the header of a table, with a column for each sort.
static void print_header(const char *first) {
    printf("%-10s", first);
    for (size_t s = 0; s < NUM_SORTS; s++) {
        printf(" %9s", sorts[s].name);
    }
    printf("\n");
}

/// @brief Prints a row of nanoseconds per element, timing each sort on
/// input, which holds total elements in arrays of n.
static void print_row(const char *label, const void *input, void *work, size_t n, size_t total, size_t size, sof_sort_compare_func_t cmp_func) {
    printf("%-10s", label);
    for (size_t s = 0; s < NUM_SORTS; s++) {
        if (sorts[s].quadratic && n > QUADRATIC_MAX) {
            printf(" %9s", "-");
        } else {
            double seconds = time_sort(sorts[s].sort, input, work, n, total, size, cmp_func);
            printf(" %9.2f", seconds / (double)total * 1e9);
        }
        fflush(stdout);
    }
    printf("\n");
}

/// @brief Random uint32_t arrays of 10, 100, ... elements, up to max.
static void bench_random(size_t max) {
    size_t largest = max > BATCH ? max : BATCH;
    uint32_t *input = sof_alloc_malloc(NULL, largest * sizeof(uint32_t));
    uint32_t *work = sof_alloc_malloc(NULL, largest * sizeof(uint32_t));
    if (input == NULL || work == NULL) {
        fprintf(stderr, "bench: out of memory\n");
        exit(1);
    }
    printf("random uint32_t: ns per element\n");
    print_header("count");
    for (size_t n = 10; n <= max; n = n * 10 > max && n < max ? max : n * 10) {
        // Small arrays are sorted many at a time, to be long enough to time.
        size_t total = n < BATCH ? BATCH / n * n : n;
        for (size_t i = 0; i < total; i++) {
            input[i] = (uint32_t)sof_random_next(&rng);
        }
        char label[24];
        snprintf(label, sizeof(label), "%zu", n);
        print_row(label, input, work, n, total, sizeof(uint32_t), compare_u32);
    }
    sof_alloc_free(NULL, input, largest * sizeof(uint32_t));
    sof_alloc_free(NULL, work, largest * sizeof(uint32_t));
}

/// @brief Arrays of n uint32_t in patterns that trouble simple sorts.
static void bench_patterns(size_t n) {
    static const char *names[] = {"sorted", "reversed", "equal", "organ", "random"};
    uint32_t *input = sof_alloc_malloc(NULL, n * sizeof(uint32_t));
    uint32_t *work = sof_alloc_malloc(NULL, n * sizeof(uint32_t));
    if (input == NULL || work == NULL) {
        fprintf(stderr, "bench: out of memory\n");
        exit(1);
    }
    printf("\npatterns of %zu uint32_t: ns per element\n", n);
    print_header("pattern");
    for (size_t p = 0; p < sizeof(names) / sizeof(names[0]); p++) {
        for (size_t i = 0; i < n; i++) {
            switch (p) {
            case 0:
                input[i] = (uint32_t)i;
                break;
            case 1:
                input[i] = (uint32_t)(n - i);
                break;
            case 2:
                input[i] = 7;
                break;
            case 3:
                input[i] = (uint32_t)(i < n / 2 ? i : n - i);
                break;
            default:
                input[i] = (uint32_t)sof_random_next(&rng);
                break;
            }
        }
        print_row(names[p], input, work, n, n, sizeof(uint32_t), compare_u32);
    }
    sof_alloc_free(NULL, input, n * sizeof(uint32_t));
    sof_alloc_free(NULL, work, n * sizeof(uint32_t));
}

/// @brief Every word in the file at path, as char pointers.
static void bench_words(const char *path) {
    size_t count = 0, capacity = 0;
    char **words = load_words(path, &count, &capacity);
    if (words == NULL || count == 0) {
        printf("\nno words read from %s; skipping words\n", path);
        if (words != NULL) {
            free_words(words, count, capacity);
        }
        return;
    }
    char **work = sof_alloc_malloc(NULL, count * sizeof(char *));
    if (work == NULL) {
        fprintf(stderr, "bench: out of memory\n");
        exit(1);
    }
    printf("\n%zu words from %s, shuffled: ns per word\n", count, path);
    print_header("");
    print_row("words", words, work, count, count, sizeof(char *), compare_string);
    sof_alloc_free(NULL, work, count * sizeof(char *));
    free_words(words, count, capacity);
}

int main(int argc, char **argv) {
    settings_t settings = {0};
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(argc, argv, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "bench: unknown option %s\n", argv[index]);
        usage(stderr);
        return 2;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "bench: %s needs an argument\n", option->option);
        usage(stderr);
        return 2;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "bench: %s takes no argument\n", option->option);
        usage(stderr);
        return 2;
    case SOF_COPT_STOPPED:
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
        fprintf(stderr, "bench: only one word list, please\n");
        usage(stderr);
        return 2;
    }
    sof_random_init(&rng, 1);
    bench_random(settings.count);
    bench_patterns(settings.count);
    bench_words(index < argc ? argv[index] : "../test-data/words.txt");
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: bench [-h] [-n count] [words]\n");
    sof_copt_print(stream);
}
