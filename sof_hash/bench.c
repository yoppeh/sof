/**
 * @file bench.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Measures sof_hash_bytes() and sof_hash_string() against two simple hashes.
 * @details The two measures are:
 * - bytes: sof_hash_bytes() on random data of lengths from 4 bytes to 64 KiB,
 *   in nanoseconds per hash and gigabytes per second.
 * - words: sof_hash_string() on every word in a word list, with the
 *   strlen() this takes, in nanoseconds per word.
 *
 * Both compare the result with the x31 hash (h = h * 31 + c, as in Java's
 * String.hashCode()) and FNV-1a, which hash a byte at a time. Every timing
 * is the best of several runs.
 *
 * Usage: bench [words]
 * where words is a file with a word on each line (default
 * /usr/share/dict/words). Without one, only the bytes measure is run.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sof_alloc.h"
#include "sof_hash.h"
#include "sof_random.h"

#define RUNS 7

/// @brief Every hash is added in here, so the compiler can't skip them.
static volatile uint64_t sink;

/// @brief The random data and the word order come from here, with a fixed
/// seed, so every run measures the same input.
static sof_random_t rng;

static void free_words(char **words, size_t count, size_t capacity);

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static uint64_t x31_bytes(const void *data, size_t length) {
    const unsigned char *p = data;
    uint64_t h = 0;
    for (size_t i = 0; i < length; i++) {
        h = (h << 5) - h + p[i];
    }
    return h;
}

static uint64_t fnv1a_bytes(const void *data, size_t length) {
    const unsigned char *p = data;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < length; i++) {
        h = (h ^ p[i]) * 0x100000001b3ULL;
    }
    return h;
}

static uint64_t rapid_bytes(const void *data, size_t length) {
    return sof_hash_bytes(data, length, 0);
}

static uint64_t x31_string(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    uint64_t h = 0;
    while (*p != '\0') {
        h = (h << 5) - h + *p++;
    }
    return h;
}

static uint64_t fnv1a_string(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    uint64_t h = 0xcbf29ce484222325ULL;
    while (*p != '\0') {
        h = (h ^ *p++) * 0x100000001b3ULL;
    }
    return h;
}

static uint64_t rapid_string(const char *s) {
    return sof_hash_string(s, 0);
}

static const struct {
    const char *name;
    uint64_t (*bytes)(const void *data, size_t length);
    uint64_t (*string)(const char *s);
} hashes[] = {
    {"hash", rapid_bytes, rapid_string},
    {"x31", x31_bytes, x31_string},
    {"fnv1a", fnv1a_bytes, fnv1a_string},
};
#define NUM_HASHES (sizeof(hashes) / sizeof(hashes[0]))

static void bench_bytes(void) {
    static const size_t lengths[] = {4, 8, 16, 32, 64, 128, 256, 1024, 4096, 65536};
    const size_t area = 1 << 20;
    unsigned char *buf = sof_alloc_malloc(NULL, area + 65536);
    if (buf == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    sof_random_bytes(&rng, buf, area + 65536);

    printf("sof_hash_bytes(): ns per hash (GB/s)\n%-8s", "bytes");
    for (size_t h = 0; h < NUM_HASHES; h++) {
        printf(" %17s", hashes[h].name);
    }
    printf("\n");
    for (size_t l = 0; l < sizeof(lengths) / sizeof(lengths[0]); l++) {
        size_t length = lengths[l];
        // About 64 MiB of input, or 4 million hashes, a run.
        size_t count = (64u << 20) / length;
        if (count > 4000000) {
            count = 4000000;
        }
        printf("%-8zu", length);
        for (size_t h = 0; h < NUM_HASHES; h++) {
            double best = 1e9;
            for (int run = 0; run < RUNS; run++) {
                uint64_t total = 0;
                size_t offset = 0;
                double start = now();
                for (size_t i = 0; i < count; i++) {
                    total += hashes[h].bytes(buf + offset, length);
                    // Move on, at an offset the CPU can't predict.
                    offset = (offset + 64 + (total & 63)) & (area - 1);
                }
                double elapsed = now() - start;
                sink += total;
                if (elapsed < best) {
                    best = elapsed;
                }
            }
            double ns = best / (double)count * 1e9;
            printf("   %6.2f (%6.2f)", ns, (double)length / ns);
        }
        printf("\n");
    }
    sof_alloc_free(NULL, buf, area + 65536);
}

/// @brief Reads a word list into an array of strings, in random order.
/// @details The array is allocated with room for *capacity strings, which
/// free_words() frees it with.
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
                *count = n;
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
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    // Shuffle, so neighbouring words don't share prefixes.
    sof_random_shuffle(&rng, words, n, sizeof(char *));
    *count = n;
    return words;
}

/// @brief Frees the count strings in words and the array, which has room for
/// capacity strings.
static void free_words(char **words, size_t count, size_t capacity) {
    for (size_t i = 0; i < count; i++) {
        sof_alloc_free(NULL, words[i], strlen(words[i]) + 1);
    }
    sof_alloc_free(NULL, words, capacity * sizeof(char *));
}

static void bench_words(const char *path) {
    size_t count = 0, capacity = 0;
    char **words = load_words(path, &count, &capacity);
    if (words == NULL || count == 0) {
        printf("\nno words read from %s; skipping sof_hash_string()\n", path);
        if (words != NULL) {
            free_words(words, count, capacity);
        }
        return;
    }
    size_t total_length = 0;
    for (size_t i = 0; i < count; i++) {
        total_length += strlen(words[i]);
    }
    printf("\nsof_hash_string(): %zu words from %s, %.1f bytes on average\n", count, path, (double)total_length / (double)count);
    printf("%-8s %10s\n", "", "ns/word");
    for (size_t h = 0; h < NUM_HASHES; h++) {
        double best = 1e9;
        for (int run = 0; run < RUNS; run++) {
            uint64_t total = 0;
            double start = now();
            for (int pass = 0; pass < 10; pass++) {
                for (size_t i = 0; i < count; i++) {
                    total += hashes[h].string(words[i]);
                }
            }
            double elapsed = now() - start;
            sink += total;
            if (elapsed < best) {
                best = elapsed;
            }
        }
        printf("%-8s %10.2f\n", hashes[h].name, best / (10.0 * (double)count) * 1e9);
    }
    free_words(words, count, capacity);
}

int main(int ac, char **av) {
    if (ac > 2) {
        fprintf(stderr, "Usage: %s [words]\n", av[0]);
        return 2;
    }
    sof_random_init(&rng, 1);
    bench_bytes();
    bench_words(ac > 1 ? av[1] : "/usr/share/dict/words");
    return 0;
}
