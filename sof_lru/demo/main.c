/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Simulates least-recently-used caches of several sizes on a trace of
 * keys, and reports the hit rate of each.
 * @details Replays a trace, one key per line, from files or standard input,
 * or one it generates, through an LRU cache of each size given, and prints
 * each cache's hits, misses and hit rate. Generated traces draw keys from a
 * seeded generator, uniformly or with a Zipf-like skew, and can be printed
 * instead, to be saved and replayed.
 *
 *     cachesim [-h] [-s sizes] [-g keys] [-n count] [-z skew] [-r seed] [-p] [file]...
 *
 * With no file, or a file named "-", the trace is read from standard input.
 * Its options are read with sof_copt.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cachesim.h"
#include "sof_copt.h"

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The cache sizes given with -s, allocated, and how many.
    size_t *sizes;
    size_t num_sizes;
    /// @brief The number of different keys to generate, or 0 to read a
    /// trace.
    uint64_t keys;
    /// @brief The number of accesses to generate.
    uint64_t count;
    /// @brief The skew of a generated trace.
    double skew;
    /// @brief The seed of a generated trace.
    uint64_t seed;
} settings_t;

static int count_validate(sof_copt_t *option, void *ctx);
static int generate(cachesim_t *sim, const settings_t *settings, int print);
static int keys_validate(sof_copt_t *option, void *ctx);
static const char *name_of(const char *path);
static const char *parse_number(const char *s, uint64_t *n);
static int read_line(FILE *f, char **line, size_t *size);
static int replay_file(cachesim_t *sim, const char *path, char **line, size_t *size);
static void report(const cachesim_t *sim);
static int seed_validate(sof_copt_t *option, void *ctx);
static int set_sizes(settings_t *settings, const char *list);
static int sizes_validate(sof_copt_t *option, void *ctx);
static int skew_validate(sof_copt_t *option, void *ctx);
static void usage(FILE *stream);

/// @brief A code the validate callbacks return, which sof_copt keeps in the
/// option's stop_value: its argument is wrong. The callback has printed why.
#define BAD_ARGUMENT 1

/// @brief A code the validate callbacks return, which sof_copt keeps in the
/// option's stop_value: memory ran out reading the option.
#define OUT_OF_MEMORY 2

/// @brief The cache sizes used when -s isn't given.
#define DEFAULT_SIZES "10,100,1000"

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_sizes = {
    .option = "-s",
    .description = "cache sizes, separated by commas (default " DEFAULT_SIZES ")",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = sizes_validate,
};

static sof_copt_t opt_keys = {
    .option = "-g",
    .description = "generate a trace of this many different keys",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = keys_validate,
};

static sof_copt_t opt_count = {
    .option = "-n",
    .description = "accesses to generate (default 1000000)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = count_validate,
};

static sof_copt_t opt_skew = {
    .option = "-z",
    .description = "skew of the generated keys: 0 uniform, 1 Zipf (default 0)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = skew_validate,
};

static sof_copt_t opt_seed = {
    .option = "-r",
    .description = "seed of the generated trace (default 1)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = seed_validate,
};

static sof_copt_t opt_print = {
    .option = "-p",
    .description = "print the generated trace instead of simulating it",
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_sizes,
    &opt_keys,
    &opt_count,
    &opt_skew,
    &opt_seed,
    &opt_print,
};

/// @brief Reads the argument of -n.
static int count_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *end = parse_number(option->value, &settings->count);
    if (end == NULL || *end != '\0') {
        fprintf(stderr, "%s needs a number of accesses, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Generates the trace settings asks for, and replays it through sim,
/// or prints it if print is set.
/// @return 0, or 1 if memory ran out, after printing why.
static int generate(cachesim_t *sim, const settings_t *settings, int print) {
    cachesim_trace_t trace;
    if (cachesim_trace_init(&trace, settings->keys, settings->skew, settings->seed, NULL) != CACHESIM_OK) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    int rc = 0;
    char key[32];
    for (uint64_t i = 0; i < settings->count && rc == 0; i++) {
        uint64_t k;
        cachesim_trace_next(&trace, &k);
        snprintf(key, sizeof(key), "%" PRIu64, k);
        if (print) {
            puts(key);
        } else if (cachesim_access(sim, key) != CACHESIM_OK) {
            fprintf(stderr, "out of memory\n");
            rc = 1;
        }
    }
    cachesim_trace_free(&trace);
    return rc;
}

/// @brief Reads the argument of -g.
static int keys_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *end = parse_number(option->value, &settings->keys);
    if (end == NULL || *end != '\0' || settings->keys == 0) {
        fprintf(stderr, "%s needs a number of keys, 1 or more, not \"%s\"\n", option->option, option->value);
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
static const char *parse_number(const char *s, uint64_t *n) {
    if (*s < '0' || *s > '9') {
        return NULL;
    }
    char *end;
    errno = 0;
    unsigned long long value = strtoull(s, &end, 10);
    if (errno != 0) {
        return NULL;
    }
    *n = (uint64_t)value;
    return end;
}

/// @brief Reads a line of f into *line, a buffer of *size bytes, growing it
/// as needed, without its line ending (a newline, and a carriage return
/// before it).
/// @return 1 if a line was read, 0 at the end of f, or -1 if memory ran out.
static int read_line(FILE *f, char **line, size_t *size) {
    size_t used = 0;
    while (1) {
        if (*size - used < 2) {
            size_t new_size = *size == 0 ? 256 : 2 * *size;
            char *grown = sof_alloc_realloc(NULL, *line, *size, new_size);
            if (grown == NULL) {
                return -1;
            }
            *line = grown;
            *size = new_size;
        }
        size_t room = *size - used;
        if (room > INT32_MAX) {
            room = INT32_MAX;
        }
        if (fgets(*line + used, (int)room, f) == NULL) {
            break;
        }
        used += strlen(*line + used);
        if (used > 0 && (*line)[used - 1] == '\n') {
            break;
        }
    }
    if (used == 0) {
        return 0;
    }
    if ((*line)[used - 1] == '\n') {
        used--;
    }
    if (used > 0 && (*line)[used - 1] == '\r') {
        used--;
    }
    (*line)[used] = '\0';
    return 1;
}

/// @brief Replays the keys in one file, or standard input for "-", one per
/// line, through sim. Empty lines are skipped.
/// @return 0, or 1 if the file couldn't be read or memory ran out, after
/// printing why.
static int replay_file(cachesim_t *sim, const char *path, char **line, size_t *size) {
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        return 1;
    }
    int rc = 0, got;
    while (rc == 0 && (got = read_line(f, line, size)) != 0) {
        if (got < 0 || ((*line)[0] != '\0' && cachesim_access(sim, *line) != CACHESIM_OK)) {
            fprintf(stderr, "out of memory\n");
            rc = 1;
        }
    }
    if (ferror(f)) {
        fprintf(stderr, "error reading %s\n", name_of(path));
        rc = 1;
    }
    if (!is_stdin) {
        fclose(f);
    }
    return rc;
}

/// @brief Prints a line for each cache: its size, hits, misses and hit rate.
static void report(const cachesim_t *sim) {
    printf("%12s  %12s  %12s  %8s\n", "size", "hits", "misses", "hit rate");
    for (size_t i = 0; i < sim->count; i++) {
        size_t hits = cachesim_hits(sim, i);
        double rate = sim->accesses == 0 ? 0.0 : 100.0 * (double)hits / (double)sim->accesses;
        printf("%12zu  %12zu  %12zu  %7.2f%%\n", sim->caches[i].size, hits, sim->accesses - hits, rate);
    }
}

/// @brief Reads the argument of -r.
static int seed_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *end = parse_number(option->value, &settings->seed);
    if (end == NULL || *end != '\0') {
        fprintf(stderr, "%s needs a non-negative number, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Replaces the sizes in settings with those in list, numbers of 1 or
/// more separated by commas.
/// @return 0, 1 if list isn't such a list, or -1 if memory ran out.
static int set_sizes(settings_t *settings, const char *list) {
    size_t num = 1;
    for (const char *p = list; *p != '\0'; p++) {
        num += *p == ',';
    }
    size_t *sizes = sof_alloc_calloc(NULL, num, sizeof(size_t));
    if (sizes == NULL) {
        return -1;
    }
    const char *p = list;
    for (size_t i = 0; i < num; i++) {
        uint64_t n;
        p = parse_number(p, &n);
        if (p == NULL || n == 0 || (uint64_t)(size_t)n != n || (*p != ',' && *p != '\0')) {
            sof_alloc_free(NULL, sizes, num * sizeof(size_t));
            return 1;
        }
        sizes[i] = (size_t)n;
        p += *p == ',';
    }
    sof_alloc_free(NULL, settings->sizes, settings->num_sizes * sizeof(size_t));
    settings->sizes = sizes;
    settings->num_sizes = num;
    return 0;
}

/// @brief Reads the argument of -s.
static int sizes_validate(sof_copt_t *option, void *ctx) {
    int rc = set_sizes(ctx, option->value);
    if (rc < 0) {
        fprintf(stderr, "out of memory\n");
        return OUT_OF_MEMORY;
    }
    if (rc > 0) {
        fprintf(stderr, "%s needs sizes of 1 or more separated by commas, such as 10,100, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Reads the argument of -z.
static int skew_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    char *end;
    errno = 0;
    settings->skew = strtod(option->value, &end);
    if (end == option->value || *end != '\0' || errno != 0 || !(settings->skew >= 0 && settings->skew <= CACHESIM_SKEW_MAX)) {
        fprintf(stderr, "%s needs a skew from 0 to %d, not \"%s\"\n", option->option, CACHESIM_SKEW_MAX, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: cachesim [-h] [-s sizes] [-g keys] [-n count] [-z skew] [-r seed] [-p] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Replays a trace of keys, one per line, from the files, or from standard input\n"
        "if there are none or a file is \"-\", through an LRU cache of each size, and\n"
        "prints the hits, misses and hit rate of each. With -g, it generates the trace\n"
        "instead: keys 0 to keys - 1, key k drawn in proportion to 1 / (k + 1)^skew.\n");
}

int main(int ac, char **av) {
    settings_t settings = {NULL, 0, 0, 1000000, 0.0, 1};
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
        if (option->stop_value == OUT_OF_MEMORY) {
            rc = 1;
        }
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
    if (!opt_keys.present && (opt_count.present || opt_skew.present || opt_seed.present || opt_print.present)) {
        fprintf(stderr, "-n, -z, -r and -p need -g\n");
        usage(stderr);
        goto done;
    }
    if (opt_keys.present && index < ac) {
        fprintf(stderr, "-g generates the trace, so it takes no files\n");
        usage(stderr);
        goto done;
    }
    if (settings.sizes == NULL) {
        int got = set_sizes(&settings, DEFAULT_SIZES);
        if (got != 0) {
            fprintf(stderr, "out of memory\n");
            rc = 1;
            goto done;
        }
    }
    if (opt_print.present) {
        rc = generate(NULL, &settings, 1);
        goto done;
    }

    cachesim_t sim;
    if (cachesim_init(&sim, settings.sizes, settings.num_sizes, NULL) != CACHESIM_OK) {
        fprintf(stderr, "out of memory\n");
        rc = 1;
        goto done;
    }
    rc = 0;
    if (opt_keys.present) {
        rc = generate(&sim, &settings, 0);
    } else {
        char *line = NULL;
        size_t size = 0;
        if (index == ac) {
            rc = replay_file(&sim, "-", &line, &size);
        }
        for (int i = index; i < ac && rc == 0; i++) {
            rc = replay_file(&sim, av[i], &line, &size);
        }
        sof_alloc_free(NULL, line, size);
    }
    if (rc == 0) {
        if (opt_keys.present) {
            printf("%zu accesses to %" PRIu64 " keys, skew %g, seed %" PRIu64 "\n", sim.accesses, settings.keys, settings.skew, settings.seed);
        } else {
            printf("%zu accesses\n", sim.accesses);
        }
        report(&sim);
    }
    cachesim_free(&sim);
done:
    sof_alloc_free(NULL, settings.sizes, settings.num_sizes * sizeof(size_t));
    return rc;
}
