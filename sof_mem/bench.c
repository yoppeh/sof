/**
 * @file bench.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Measures the sof_mem allocators against the standard malloc() and
 * free().
 * @details Each measure is run with five allocators: the standard
 * functions, an arena, a pool, a slab allocator, and a tracker over the
 * standard functions, each through its sof_alloc_t. The arena, pool and slab
 * allocator are cleared after each run, inside the timing, since that is
 * where they free, and kept between runs, as an application reusing one
 * would. The three measures are:
 * - blocks: the -n count of blocks of random sizes from 8 to 128 bytes,
 *   allocated and then freed one at a time, in nanoseconds per block. The
 *   pool's objects are 128 bytes.
 * - sof_rbtree: the -n count of random keys inserted into an sof_rbtree_t, which is
 *   then freed, in nanoseconds per key. The pool's objects are the size of
 *   a node.
 * - hashmap: every word in a word list inserted into a sof_hashmap_t of string
 *   keys, which copies each word with the allocator, then freed, in
 *   nanoseconds per word. The pool's objects are 32 bytes, so longer words
 *   and the element arrays come from the standard functions, as the arrays
 *   do for the slab allocator.
 *
 * Every timing is the best of several runs.
 *
 * Usage: bench [-h] [-n count] [words]
 * where count is the number of blocks and keys (default 1000000) and words
 * is a file with a word on each line (default ../test-data/words.txt).
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
#include "sof_mem.h"
#include "sof_copt.h"
#include "sof_hashmap.h"
#include "sof_random.h"
#include "sof_rbtree.h"
#include "sof_timer.h"

/// @brief Each timing is the best of this many runs.
#define RUNS 5

/// @brief A code the validate callback returns, which sof_copt keeps in the
/// option's stop_value: its argument isn't a number in range. The callback
/// has printed why.
#define BAD_NUMBER 1

/// @brief The allocators compared, in the order of the table's columns.
typedef enum kind_t {
    KIND_MALLOC,
    KIND_ARENA,
    KIND_POOL,
    KIND_SLAB,
    KIND_TRACK,
    KINDS,
} kind_t;

/// @brief An allocator being measured: the sof_alloc_t to give a library, and
/// the allocator behind it.
typedef struct bench_alloc_t {
    kind_t kind;
    sof_alloc_t alloc;
    sof_mem_arena_t arena;
    sof_mem_pool_t pool;
    sof_mem_slab_t slab;
    sof_mem_track_t track;
} bench_alloc_t;

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The number of blocks and keys.
    size_t count;
} settings_t;

/// @brief One measure: what it is called, and a function timing one run of
/// it with an allocator.
typedef struct measure_t {
    const char *name;
    /// @brief The size of the pool's objects.
    size_t object_size;
    void (*run)(const sof_alloc_t *alloc, void *ctx);
} measure_t;

/// @brief The input of the blocks measure.
typedef struct blocks_input_t {
    size_t count;
    size_t *sizes;
    unsigned char **blocks;
} blocks_input_t;

/// @brief The input of the sof_rbtree measure.
typedef struct keys_input_t {
    size_t count;
    uint64_t *keys;
} keys_input_t;

/// @brief The input of the hashmap measure: count words, NUL-terminated, in
/// one buffer of size bytes.
typedef struct words_input_t {
    size_t count;
    char **words;
    char *buffer;
    size_t size;
} words_input_t;

static void bench_alloc_done(bench_alloc_t *b);
static void bench_alloc_init(bench_alloc_t *b, kind_t kind, size_t object_size);
static void bench_alloc_reset(bench_alloc_t *b);
static int compare_key(const void *a, const void *b, void *ctx);
static int count_missing(sof_copt_t *option, void *ctx);
static int count_validate(sof_copt_t *option, void *ctx);
static void fail(const char *what);
static void free_words(words_input_t *input);
static int load_words(const char *path, words_input_t *input);
static void *must_alloc(size_t size);
static void print_row(const measure_t *measure, void *ctx, size_t items);
static void run_blocks(const sof_alloc_t *alloc, void *ctx);
static void run_hashmap(const sof_alloc_t *alloc, void *ctx);
static void run_sof_rbtree(const sof_alloc_t *alloc, void *ctx);
static void usage(FILE *stream);

/// @brief Every result is added in here, so the compiler can't skip work.
static volatile uint64_t sink;

/// @brief The column headings, by kind_t.
static const char *kind_names[KINDS] = {"malloc", "arena", "pool", "slab", "track"};

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_count = {
    .option = "-n",
    .description = "allocate this many blocks and insert this many keys (default 1000000)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = count_validate,
    .missing = count_missing,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_count,
};

/// @brief Frees the allocator behind b.
static void bench_alloc_done(bench_alloc_t *b) {
    switch (b->kind) {
    case KIND_ARENA:
        sof_mem_arena_free(&b->arena);
        break;
    case KIND_POOL:
        sof_mem_pool_free(&b->pool);
        break;
    case KIND_SLAB:
        sof_mem_slab_free(&b->slab);
        break;
    case KIND_TRACK:
        if (b->track.blocks != 0 || b->track.errors != 0) {
            fail("the tracker found leaks or bad frees");
        }
        sof_mem_track_free(&b->track);
        break;
    default:
        break;
    }
}

/// @brief Sets up an allocator of the given kind, with pool objects of
/// object_size bytes.
static void bench_alloc_init(bench_alloc_t *b, kind_t kind, size_t object_size) {
    memset(b, 0, sizeof(*b));
    b->kind = kind;
    sof_mem_error_t rc = SOF_MEM_OK;
    switch (kind) {
    case KIND_ARENA:
        rc = sof_mem_arena_init(&b->arena, 0, NULL);
        b->alloc = sof_mem_arena_allocator(&b->arena);
        break;
    case KIND_POOL:
        rc = sof_mem_pool_init(&b->pool, object_size, 0, NULL);
        b->alloc = sof_mem_pool_allocator(&b->pool);
        break;
    case KIND_SLAB:
        rc = sof_mem_slab_init(&b->slab, 0, 0, NULL);
        b->alloc = sof_mem_slab_allocator(&b->slab);
        break;
    case KIND_TRACK:
        rc = sof_mem_track_init(&b->track, NULL, NULL, NULL);
        b->alloc = sof_mem_track_allocator(&b->track);
        break;
    default:
        break;
    }
    if (rc != SOF_MEM_OK) {
        fail("can't set up an allocator");
    }
}

/// @brief Takes back everything the allocator behind b handed out, for
/// those that take everything back at once.
static void bench_alloc_reset(bench_alloc_t *b) {
    switch (b->kind) {
    case KIND_ARENA:
        sof_mem_arena_clear(&b->arena);
        break;
    case KIND_POOL:
        sof_mem_pool_clear(&b->pool);
        break;
    case KIND_SLAB:
        sof_mem_slab_clear(&b->slab);
        break;
    default:
        break;
    }
}

/// @brief Compares two keys of the sof_rbtree measure, pointers to uint64_t.
static int compare_key(const void *a, const void *b, void *ctx) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

/// @brief Without -n, allocates a million blocks.
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
    if (value < 1 || value > 100000000 || errno != 0 || *end != '\0') {
        fprintf(stderr, "bench: %s wants a count from 1 to 100000000, not \"%s\"\n", option->option, s);
        return BAD_NUMBER;
    }
    settings->count = (size_t)value;
    return 0;
}

/// @brief Prints what went wrong and exits.
static void fail(const char *what) {
    fprintf(stderr, "bench: %s\n", what);
    exit(1);
}

/// @brief Frees what load_words() allocated.
static void free_words(words_input_t *input) {
    sof_alloc_free(NULL, input->words, input->count * sizeof(char *));
    sof_alloc_free(NULL, input->buffer, input->size);
    memset(input, 0, sizeof(*input));
}

/// @brief Reads a word list into one buffer, a word on each line, and an
/// array of pointers to the words. Returns 0 if the file can't be read.
static int load_words(const char *path, words_input_t *input) {
    memset(input, 0, sizeof(*input));
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    size_t capacity = 1 << 20;
    char *buffer = must_alloc(capacity);
    size_t length = 0, n;
    while ((n = fread(buffer + length, 1, capacity - length, f)) > 0) {
        length += n;
        if (length == capacity) {
            buffer = sof_alloc_realloc(NULL, buffer, capacity, capacity * 2);
            if (buffer == NULL) {
                fail("out of memory");
            }
            capacity *= 2;
        }
    }
    fclose(f);
    // There is always room for one more byte, to end the last line.
    buffer[length] = '\n';
    for (size_t i = 0; i <= length; i++) {
        if (buffer[i] == '\n' || buffer[i] == '\r') {
            buffer[i] = '\0';
        }
    }
    size_t count = 0;
    for (size_t i = 0; i <= length; i++) {
        if (buffer[i] != '\0' && (i == 0 || buffer[i - 1] == '\0')) {
            count++;
        }
    }
    char **words = count > 0 ? must_alloc(count * sizeof(char *)) : NULL;
    count = 0;
    for (size_t i = 0; i <= length; i++) {
        if (buffer[i] != '\0' && (i == 0 || buffer[i - 1] == '\0')) {
            words[count++] = buffer + i;
        }
    }
    input->count = count;
    input->words = words;
    input->buffer = buffer;
    input->size = capacity;
    return 1;
}

/// @brief Allocates with the standard functions, or exits.
static void *must_alloc(size_t size) {
    void *p = sof_alloc_malloc(NULL, size);
    if (p == NULL) {
        fail("out of memory");
    }
    return p;
}

/// @brief Prints a row of nanoseconds per item, timing the measure with each
/// allocator.
static void print_row(const measure_t *measure, void *ctx, size_t items) {
    printf("%-10s", measure->name);
    sof_timer_t timer;
    sof_timer_init(&timer);
    sof_timer_calibrate(&timer);
    for (kind_t kind = 0; kind < KINDS; kind++) {
        bench_alloc_t b;
        bench_alloc_init(&b, kind, measure->object_size);
        double best = 1e30;
        for (int run = 0; run < RUNS; run++) {
            sof_timer_start(&timer);
            measure->run(&b.alloc, ctx);
            bench_alloc_reset(&b);
            sof_timer_stop(&timer);
            double elapsed = sof_timer_elapsed_seconds(&timer);
            if (elapsed < best) {
                best = elapsed;
            }
        }
        bench_alloc_done(&b);
        printf(" %9.2f", best / (double)items * 1e9);
        fflush(stdout);
    }
    printf("\n");
}

/// @brief Allocates every block, touching each, then frees them in order.
static void run_blocks(const sof_alloc_t *alloc, void *ctx) {
    blocks_input_t *input = ctx;
    for (size_t i = 0; i < input->count; i++) {
        input->blocks[i] = sof_alloc_malloc(alloc, input->sizes[i]);
        if (input->blocks[i] == NULL) {
            fail("out of memory");
        }
        input->blocks[i][0] = (unsigned char)i;
    }
    uint64_t sum = 0;
    for (size_t i = 0; i < input->count; i++) {
        sum += input->blocks[i][0];
        sof_alloc_free(alloc, input->blocks[i], input->sizes[i]);
    }
    sink += sum;
}

/// @brief Inserts every word into a hashmap that copies them, then frees it.
static void run_hashmap(const sof_alloc_t *alloc, void *ctx) {
    words_input_t *input = ctx;
    sof_hashmap_t map;
    if (sof_hashmap_init(&map, &sof_hashmap_string_keys, NULL, NULL, 0, alloc) != SOF_HASHMAP_OK) {
        fail("can't set up a hashmap");
    }
    for (size_t i = 0; i < input->count; i++) {
        if (sof_hashmap_insert(&map, input->words[i], NULL) == NULL) {
            fail("out of memory");
        }
    }
    sink += map.count;
    sof_hashmap_free(&map);
}

/// @brief Inserts every key into an sof_rbtree, then frees it.
static void run_sof_rbtree(const sof_alloc_t *alloc, void *ctx) {
    keys_input_t *input = ctx;
    sof_rbtree_t tree;
    if (sof_rbtree_init(&tree, compare_key, NULL, NULL, alloc) != SOF_RBTREE_OK) {
        fail("can't set up an sof_rbtree");
    }
    for (size_t i = 0; i < input->count; i++) {
        if (sof_rbtree_insert(&tree, &input->keys[i], NULL) == NULL) {
            fail("out of memory");
        }
    }
    sink += tree.count;
    sof_rbtree_free(&tree);
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
    const char *path = index < argc ? argv[index] : "../test-data/words.txt";
    size_t n = settings.count;
    // A fixed seed, so every run measures the same sizes and keys.
    sof_random_t rng;
    sof_random_init(&rng, 1);

    printf("ns per item, best of %d runs\n", RUNS);
    printf("%-10s", "");
    for (kind_t kind = 0; kind < KINDS; kind++) {
        printf(" %9s", kind_names[kind]);
    }
    printf("\n");

    blocks_input_t blocks = {n, must_alloc(n * sizeof(size_t)), must_alloc(n * sizeof(unsigned char *))};
    for (size_t i = 0; i < n; i++) {
        blocks.sizes[i] = (size_t)sof_random_range(&rng, 8, 128);
    }
    measure_t blocks_measure = {"blocks", 128, run_blocks};
    print_row(&blocks_measure, &blocks, n);
    sof_alloc_free(NULL, blocks.sizes, n * sizeof(size_t));
    sof_alloc_free(NULL, blocks.blocks, n * sizeof(unsigned char *));

    keys_input_t keys = {n, must_alloc(n * sizeof(uint64_t))};
    for (size_t i = 0; i < n; i++) {
        keys.keys[i] = sof_random_next(&rng);
    }
    measure_t sof_rbtree_measure = {"sof_rbtree", sizeof(sof_rbtree_node_t), run_sof_rbtree};
    print_row(&sof_rbtree_measure, &keys, n);
    sof_alloc_free(NULL, keys.keys, n * sizeof(uint64_t));

    words_input_t words;
    if (!load_words(path, &words) || words.count == 0) {
        printf("no words read from %s; skipping hashmap\n", path);
    } else {
        measure_t hashmap_measure = {"hashmap", 32, run_hashmap};
        print_row(&hashmap_measure, &words, words.count);
    }
    free_words(&words);
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: bench [-h] [-n count] [words]\n");
    sof_copt_print(stream);
}
