/**
 * @file bench.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Compares Bloom filters with a hashmap holding the same keys.
 * @details The words of a word list are shuffled and split in two: the first
 * half are added to classic and blocked Bloom filters of several false
 * positive rates and to a sof_hashmap_t, and then both halves are looked
 * up. For each, it reports the memory per key in bits, the nanoseconds per
 * add, per lookup of a key that was added (hit) and per lookup of one that
 * wasn't (miss), and the share of misses the filter wrongly said yes to.
 * The hashmap stores pointers to the words, not copies, and its memory is
 * its array of elements only, not the strings, which a real set of words
 * would have to keep too. Then the same is measured for filters much larger
 * than the cache: 100,000,000 generated 8-byte keys are added and looked up,
 * and as many others. Every timing is the best of several runs.
 *
 * Usage: bench [-h] [-n keys] [words]
 * where words is a file with a word on each line (default
 * ../test-data/words.txt), and keys is the number of generated keys for the
 * large filters (0 leaves them out).
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_bloom.h"
#include "sof_copt.h"
#include "sof_hashmap.h"
#include "sof_random.h"
#include "sof_timer.h"

/// @brief Each timing is the best of this many runs.
#define RUNS 5

/// @brief Each timing of a large filter is the best of this many runs.
#define LARGE_RUNS 3

/// @brief The number of generated keys for the large filters, unless -n is
/// given.
#define LARGE_KEYS 100000000

/// @brief Generated key i is i times this odd number, so every key is
/// different and their bits are mixed.
#define KEY_MULTIPLIER 0x9e3779b97f4a7c15ULL

/// @brief A word list: the file's text, with each line's newline replaced
/// by a NUL, and a pointer to each non-empty line. text_size and lines are
/// the sizes the two were allocated with.
typedef struct words_t {
    char *text;
    size_t text_size;
    char **word;
    size_t lines;
    size_t count;
} words_t;

/// @brief The best time, in nanoseconds per key, of each step.
typedef struct result_t {
    double add;
    double hit;
    double miss;
    size_t false_positives;
    size_t bytes;
} result_t;

static result_t bench_bloom(const words_t *words, size_t members, double rate, unsigned flags);
static result_t bench_hashmap(const words_t *words, size_t members);
static result_t bench_large(size_t keys, double rate, unsigned flags);
static void fail(const char *what);
static void free_words(words_t *words);
static void keep_best(result_t *best, const result_t *r, int run);
static int load_words(const char *path, words_t *words);
static void print_header(void);
static void print_row(const char *label, const result_t *r, size_t members, size_t misses);
static void usage(FILE *stream);

/// @brief Every lookup's result is added in here, so the compiler can't skip
/// one.
static volatile size_t sink;

/// @brief The word order comes from here, with a fixed seed, so every run
/// measures the same input.
static sof_random_t rng;

/// @brief Times each step; calibrated once.
static sof_timer_t timer;

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_keys = {
    .option = "-n",
    .description = "the generated keys for the large filters, 0 for none (default 100000000)",
    .type = SOF_COPT_ARG_REQUIRED,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_keys,
};

/// @brief Hash and equal for strings, without copy or free: the map stores
/// pointers into the word list's text.
static const sof_hashmap_key_callbacks_t borrowed_string_keys = {
    sof_hashmap_string_hash,
    sof_hashmap_string_equal,
    NULL,
    NULL,
};

/// @brief Adds the first members words to a filter of the given rate and
/// kind, then looks them all up.
static result_t bench_bloom(const words_t *words, size_t members, double rate, unsigned flags) {
    result_t best = {0};
    for (int run = 0; run < RUNS; run++) {
        sof_bloom_t bloom;
        result_t r = {0};
        if (sof_bloom_init(&bloom, members, rate, flags, NULL) != SOF_BLOOM_OK) {
            fail("out of memory");
        }
        r.bytes = bloom.words * sizeof(sof_bit_t);
        sof_timer_start(&timer);
        for (size_t i = 0; i < members; i++) {
            sof_bloom_add_string(&bloom, words->word[i], NULL);
        }
        sof_timer_stop(&timer);
        r.add = (double)sof_timer_elapsed_ns(&timer);
        size_t found = 0;
        sof_timer_start(&timer);
        for (size_t i = 0; i < members; i++) {
            found += (size_t)sof_bloom_contains_string(&bloom, words->word[i]);
        }
        sof_timer_stop(&timer);
        r.hit = (double)sof_timer_elapsed_ns(&timer);
        if (found != members) {
            fail("a Bloom filter lost a key");
        }
        sof_timer_start(&timer);
        for (size_t i = members; i < words->count; i++) {
            r.false_positives += (size_t)sof_bloom_contains_string(&bloom, words->word[i]);
        }
        sof_timer_stop(&timer);
        r.miss = (double)sof_timer_elapsed_ns(&timer);
        sink += found + r.false_positives;
        sof_bloom_free(&bloom);
        keep_best(&best, &r, run);
    }
    return best;
}

/// @brief Inserts the first members words into a hashmap, then looks them
/// all up.
static result_t bench_hashmap(const words_t *words, size_t members) {
    result_t best = {0};
    for (int run = 0; run < RUNS; run++) {
        sof_hashmap_t map;
        result_t r = {0};
        if (sof_hashmap_init(&map, &borrowed_string_keys, NULL, NULL, members, NULL) != SOF_HASHMAP_OK) {
            fail("out of memory");
        }
        sof_timer_start(&timer);
        for (size_t i = 0; i < members; i++) {
            if (sof_hashmap_insert(&map, words->word[i], NULL) == NULL) {
                fail("out of memory");
            }
        }
        sof_timer_stop(&timer);
        r.add = (double)sof_timer_elapsed_ns(&timer);
        r.bytes = map.capacity * sizeof(sof_hashmap_element_t);
        size_t found = 0;
        sof_timer_start(&timer);
        for (size_t i = 0; i < members; i++) {
            found += sof_hashmap_find(&map, words->word[i]) != NULL;
        }
        sof_timer_stop(&timer);
        r.hit = (double)sof_timer_elapsed_ns(&timer);
        sof_timer_start(&timer);
        for (size_t i = members; i < words->count; i++) {
            r.false_positives += sof_hashmap_find(&map, words->word[i]) != NULL;
        }
        sof_timer_stop(&timer);
        r.miss = (double)sof_timer_elapsed_ns(&timer);
        sink += found + r.false_positives;
        sof_hashmap_free(&map);
        keep_best(&best, &r, run);
    }
    return best;
}

/// @brief Adds keys generated keys to a filter of the given rate and kind,
/// looks them all up, then looks up as many others.
static result_t bench_large(size_t keys, double rate, unsigned flags) {
    result_t best = {0};
    for (int run = 0; run < LARGE_RUNS; run++) {
        sof_bloom_t bloom;
        result_t r = {0};
        if (sof_bloom_init(&bloom, keys, rate, flags, NULL) != SOF_BLOOM_OK) {
            fail("out of memory");
        }
        r.bytes = bloom.words * sizeof(sof_bit_t);
        sof_timer_start(&timer);
        for (size_t i = 0; i < keys; i++) {
            uint64_t key = (uint64_t)i * KEY_MULTIPLIER;
            sof_bloom_add(&bloom, &key, sizeof(key), NULL);
        }
        sof_timer_stop(&timer);
        r.add = (double)sof_timer_elapsed_ns(&timer);
        size_t found = 0;
        sof_timer_start(&timer);
        for (size_t i = 0; i < keys; i++) {
            uint64_t key = (uint64_t)i * KEY_MULTIPLIER;
            found += (size_t)sof_bloom_contains(&bloom, &key, sizeof(key));
        }
        sof_timer_stop(&timer);
        r.hit = (double)sof_timer_elapsed_ns(&timer);
        if (found != keys) {
            fail("a Bloom filter lost a key");
        }
        sof_timer_start(&timer);
        for (size_t i = keys; i < 2 * keys; i++) {
            uint64_t key = (uint64_t)i * KEY_MULTIPLIER;
            r.false_positives += (size_t)sof_bloom_contains(&bloom, &key, sizeof(key));
        }
        sof_timer_stop(&timer);
        r.miss = (double)sof_timer_elapsed_ns(&timer);
        sink += found + r.false_positives;
        sof_bloom_free(&bloom);
        keep_best(&best, &r, run);
    }
    return best;
}

/// @brief Prints what went wrong and exits.
static void fail(const char *what) {
    fprintf(stderr, "bench: %s\n", what);
    exit(1);
}

/// @brief Frees what load_words() allocated.
static void free_words(words_t *words) {
    sof_alloc_free(NULL, words->word, words->lines * sizeof(char *));
    sof_alloc_free(NULL, words->text, words->text_size);
}

/// @brief Keeps in best the shorter of each time in best and r, the result
/// of the given run; the first run's result is copied whole.
static void keep_best(result_t *best, const result_t *r, int run) {
    if (run == 0) {
        *best = *r;
        return;
    }
    best->add = r->add < best->add ? r->add : best->add;
    best->hit = r->hit < best->hit ? r->hit : best->hit;
    best->miss = r->miss < best->miss ? r->miss : best->miss;
}

/// @brief Reads a word list, one word to a line, and shuffles it.
/// @return 0, or -1 if the file can't be read or holds no words.
static int load_words(const char *path, words_t *words) {
    memset(words, 0, sizeof(*words));
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    size_t capacity = 1 << 20, used = 0;
    char *text = sof_alloc_malloc(NULL, capacity);
    while (text != NULL) {
        if (used == capacity) {
            char *bigger = sof_alloc_realloc(NULL, text, capacity, capacity * 2);
            if (bigger == NULL) {
                sof_alloc_free(NULL, text, capacity);
                text = NULL;
                break;
            }
            text = bigger;
            capacity *= 2;
        }
        size_t n = fread(text + used, 1, capacity - used, f);
        if (n == 0) {
            break;
        }
        used += n;
    }
    fclose(f);
    if (text == NULL) {
        fail("out of memory");
    }
    // One more byte, for a NUL after a last line with no newline.
    char *sized = sof_alloc_realloc(NULL, text, capacity, used + 1);
    if (sized == NULL) {
        fail("out of memory");
    }
    text = sized;
    text[used] = '\0';
    size_t lines = 1;
    for (size_t i = 0; i < used; i++) {
        lines += text[i] == '\n';
    }
    char **word = sof_alloc_malloc(NULL, lines * sizeof(char *));
    if (word == NULL) {
        fail("out of memory");
    }
    size_t count = 0;
    char *line = text;
    for (size_t i = 0; i <= used; i++) {
        if (i == used || text[i] == '\n') {
            text[i] = '\0';
            line[strcspn(line, "\r")] = '\0';
            if (line[0] != '\0') {
                word[count++] = line;
            }
            line = text + i + 1;
        }
    }
    words->text = text;
    words->text_size = used + 1;
    words->word = word;
    words->lines = lines;
    words->count = count;
    if (count < 2) {
        free_words(words);
        return -1;
    }
    sof_random_shuffle(&rng, word, count, sizeof(char *));
    return 0;
}

/// @brief Prints the table's column headings.
static void print_header(void) {
    printf("%-14s %10s %8s %8s %8s %10s\n", "", "bits/key", "add ns", "hit ns", "miss ns", "false pos");
}

/// @brief Prints one row of the table.
static void print_row(const char *label, const result_t *r, size_t members, size_t misses) {
    printf("%-14s %10.1f %8.1f %8.1f %8.1f %9.3f%%\n", label, 8.0 * (double)r->bytes / (double)members, r->add / (double)members, r->hit / (double)members, r->miss / (double)misses, 100.0 * (double)r->false_positives / (double)misses);
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
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "bench: %s needs an argument\n", option->option);
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
        fprintf(stderr, "bench: only one word list, please\n");
        usage(stderr);
        return 2;
    }
    size_t large_keys = LARGE_KEYS;
    if (opt_keys.present) {
        const char *s = opt_keys.value;
        char *end;
        errno = 0;
        unsigned long long value = strtoull(s, &end, 10);
        // Twice as many keys are generated, and must all differ.
        if (*s < '0' || *s > '9' || *end != '\0' || errno != 0 || value > SIZE_MAX / 2) {
            fprintf(stderr, "bench: -n needs a number of keys, not \"%s\"\n", s);
            usage(stderr);
            return 2;
        }
        large_keys = (size_t)value;
    }
    const char *path = index < argc ? argv[index] : "../test-data/words.txt";
    sof_random_init(&rng, 1);
    sof_timer_init(&timer);
    sof_timer_calibrate(&timer);
    words_t words;
    if (load_words(path, &words) != 0) {
        fprintf(stderr, "bench: can't read two or more words from %s\n", path);
        return 1;
    }
    size_t members = words.count / 2, misses = words.count - members;
    printf("%zu words from %s: %zu added, %zu not\n\n", words.count, path, members, misses);
    print_header();
    static const struct {
        const char *label;
        double rate;
        unsigned flags;
    } rates[] = {
        {"classic 10%", 0.1, 0},
        {"classic 1%", 0.01, 0},
        {"classic 0.1%", 0.001, 0},
        {"classic 0.01%", 0.0001, 0},
        {"blocked 10%", 0.1, SOF_BLOOM_BLOCKED},
        {"blocked 1%", 0.01, SOF_BLOOM_BLOCKED},
        {"blocked 0.1%", 0.001, SOF_BLOOM_BLOCKED},
        {"blocked 0.01%", 0.0001, SOF_BLOOM_BLOCKED},
    };
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        result_t r = bench_bloom(&words, members, rates[i].rate, rates[i].flags);
        print_row(rates[i].label, &r, members, misses);
        fflush(stdout);
    }
    result_t r = bench_hashmap(&words, members);
    print_row("hashmap", &r, members, misses);
    free_words(&words);
    if (large_keys == 0) {
        return 0;
    }
    printf("\n%zu generated keys added, %zu not\n\n", large_keys, large_keys);
    print_header();
    static const struct {
        const char *label;
        double rate;
        unsigned flags;
    } large[] = {
        {"classic 1%", 0.01, 0},
        {"classic 0.1%", 0.001, 0},
        {"blocked 1%", 0.01, SOF_BLOOM_BLOCKED},
        {"blocked 0.1%", 0.001, SOF_BLOOM_BLOCKED},
    };
    for (size_t i = 0; i < sizeof(large) / sizeof(large[0]); i++) {
        r = bench_large(large_keys, large[i].rate, large[i].flags);
        print_row(large[i].label, &r, large_keys, large_keys);
        fflush(stdout);
    }
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: bench [-h] [-n keys] [words]\n");
    sof_copt_print(stream);
}
