/**
 * @file bench.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Measures sof_string against the c library's NUL-terminated strings.
 * @details Each measure does the same work on strings of random letters,
 * from 16 bytes up to the -n size, once with sof_string and once with the
 * c library, and gives the nanoseconds each call takes:
 * - length: the length field, against strlen().
 * - compare: sof_string_cmp(), against strcmp(), on strings that differ in
 *   their last byte.
 * - copy: sof_string_dup() and sof_string_free(), against strlen(), an
 *   allocation, memcpy() and a free.
 * - append: a string built 8 bytes at a time, with
 *   sof_string_append_bytes() from an empty string, against strcat() into a
 *   buffer allocated at its final size; per append.
 * - insert: 8 bytes put in the middle of a string and taken out again, with
 *   sof_string_insert_bytes() and sof_string_delete(), against strlen(),
 *   memmove() and memcpy(); per insert and delete.
 * - tolower: sof_string_tolower(), against a loop of tolower().
 *
 * Every timing is the best of several runs, and every run does at least
 * WORK bytes of strings, so that short strings are timed many at a time.
 *
 * Usage: bench [-h] [-n bytes]
 * where bytes is the longest string (default 65536).
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_copt.h"
#include "sof_random.h"
#include "sof_string.h"
#include "sof_timer.h"

/// @brief Each timing is the best of this many runs.
#define RUNS 5

/// @brief The bytes of strings each measure holds: as many strings of a
/// size as make this many bytes, or one.
#define DATA 2097152

/// @brief The bytes of strings each run goes over at least, going over the
/// strings as many times as that takes.
#define WORK 16777216

/// @brief The bytes of strings the append measure builds in a run, at
/// least one string. strcat() takes time in the square of a string's
/// length, so it builds fewer than the others go over.
#define APPEND_WORK 1048576

/// @brief The calls a run of the length field makes at least. It takes no
/// time for the bytes, so runs sized by them would be too short to time.
#define LENGTH_CALLS 4194304

/// @brief The bytes each append and insert adds.
#define PIECE 8

/// @brief The shortest string measured; each size is 8 times the last.
#define SIZE_MIN 16

/// @brief The code bytes_validate() returns when -n's argument isn't a
/// number in range, after printing why.
#define BAD_NUMBER 1

/// @brief The strings of one size that the measures work on.
typedef struct data_t {
    /// @brief Their length, in bytes.
    size_t n;
    /// @brief How many there are.
    size_t count;
    /// @brief How many times a run goes over them.
    size_t reps;
    /// @brief The c strings, each n bytes and a NUL, n + 1 apart.
    char *text;
    /// @brief Copies of text, each with its last byte changed.
    char *other;
    /// @brief Copies of text, for tolower() to change.
    char *lower;
    /// @brief Views of text and other.
    sof_string_t *views;
    sof_string_t *other_views;
    /// @brief Copies of text as strings of their own, for
    /// sof_string_tolower() to change.
    sof_string_t *owned;
    /// @brief A string of n bytes with room for a piece more, for insert,
    /// and the same as a c string.
    sof_string_t roomy;
    char *buffer;
} data_t;

/// @brief A measure: what it does, and its two ways of doing it, each
/// making one run over the data.
typedef struct measure_t {
    const char *name;
    const char *what;
    void (*sof)(data_t *d);
    void (*c)(data_t *d);
    /// @brief The calls a run makes, to divide its time by: the c way's,
    /// and sof_string's if it makes a different number, or NULL.
    size_t (*calls)(data_t *d);
    size_t (*sof_calls)(data_t *d);
} measure_t;

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The longest string.
    size_t bytes;
} settings_t;

static void append_c(data_t *d);
static size_t append_calls(data_t *d);
static void append_sof(data_t *d);
static size_t builds(data_t *d);
static int bytes_missing(sof_copt_t *option, void *ctx);
static int bytes_validate(sof_copt_t *option, void *ctx);
static void compare_c(data_t *d);
static void compare_sof(data_t *d);
static void copy_c(data_t *d);
static void copy_sof(data_t *d);
static void data_free(data_t *d);
static void data_init(data_t *d, size_t n);
static void *grab(size_t size);
static void insert_c(data_t *d);
static void insert_sof(data_t *d);
static void length_c(data_t *d);
static size_t length_calls(data_t *d);
static size_t length_reps(data_t *d);
static void length_sof(data_t *d);
static void *opaque(void *p);
static size_t per_rep(data_t *d);
static size_t per_string(data_t *d);
static double time_run(void (*run)(data_t *d), data_t *d, sof_timer_t *timer);
static void tolower_c(data_t *d);
static void tolower_sof(data_t *d);
static void usage(FILE *stream);

/// @brief Every result is added in here, so the compiler can't skip work.
static volatile uint64_t sink;

/// @brief What append and insert add.
static const char piece[PIECE + 1] = "abcdefgh";

/// @brief The measures, in the order they are printed.
static const measure_t measures[] = {
    {"length", "the length field, against strlen()", length_sof, length_c, per_string, length_calls},
    {"compare", "sof_string_cmp(), against strcmp(), of strings differing in the last byte", compare_sof, compare_c, per_string,
     NULL},
    {"copy", "sof_string_dup() and sof_string_free(), against strlen(), allocation, memcpy() and free", copy_sof, copy_c,
     per_string, NULL},
    {"append", "8 bytes to an empty string, against strcat() into a buffer of the final size; per append", append_sof,
     append_c, append_calls, NULL},
    {"insert", "8 bytes into the middle and out again, against strlen(), memmove() and memcpy(); per pair", insert_sof,
     insert_c, per_rep, NULL},
    {"tolower", "sof_string_tolower(), against a loop of tolower()", tolower_sof, tolower_c, per_string, NULL},
};
#define NUM_MEASURES (sizeof(measures) / sizeof(measures[0]))

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_bytes = {
    .option = "-n",
    .description = "measure strings of up to this many bytes (default 65536)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = bytes_validate,
    .missing = bytes_missing,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_bytes,
};

/// @brief Builds strings of n bytes with strcat(), in a buffer allocated at
/// that size, as a c program that knows the final size would.
static void append_c(data_t *d) {
    for (size_t b = 0; b < builds(d); b++) {
        char *s = grab(d->n + 1);
        s[0] = '\0';
        for (size_t k = 0; k < d->n / PIECE; k++) {
            strcat(s, piece);
        }
        sink += (unsigned char)s[d->n - 1];
        sof_alloc_free(NULL, s, d->n + 1);
    }
}

/// @brief The appends a run of append makes.
static size_t append_calls(data_t *d) {
    return builds(d) * (d->n / PIECE);
}

/// @brief Builds strings of n bytes from empty strings, which grow as they
/// need.
static void append_sof(data_t *d) {
    for (size_t b = 0; b < builds(d); b++) {
        sof_string_t s;
        if (sof_string_init(&s, 0, NULL) != SOF_STRING_OK) {
            fprintf(stderr, "bench: out of memory\n");
            exit(1);
        }
        for (size_t k = 0; k < d->n / PIECE; k++) {
            sof_string_append_bytes(&s, piece, PIECE);
        }
        sink += (unsigned char)s.data[d->n - 1];
        sof_string_free(&s);
    }
}

/// @brief The strings a run of append builds.
static size_t builds(data_t *d) {
    return d->n < APPEND_WORK ? APPEND_WORK / d->n : 1;
}

/// @brief Without -n, measures strings of up to 65536 bytes.
static int bytes_missing(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    (void)option;
    settings->bytes = 65536;
    return 0;
}

/// @brief Reads the argument of -n.
static int bytes_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *s = option->value;
    char *end;
    errno = 0;
    unsigned long long value = s[0] >= '0' && s[0] <= '9' ? strtoull(s, &end, 10) : 0;
    if (value < SIZE_MIN || value > 16777216 || errno != 0 || *end != '\0') {
        fprintf(stderr, "bench: %s wants a size from %d to 16777216, not \"%s\"\n", option->option, SIZE_MIN, s);
        return BAD_NUMBER;
    }
    settings->bytes = (size_t)value;
    return 0;
}

static void compare_c(data_t *d) {
    int64_t total = 0;
    for (size_t r = 0; r < d->reps; r++) {
        const char *text = opaque(d->text), *other = opaque(d->other);
        for (size_t i = 0; i < d->count; i++) {
            total += strcmp(text + i * (d->n + 1), other + i * (d->n + 1)) < 0;
        }
    }
    sink += (uint64_t)total;
}

static void compare_sof(data_t *d) {
    int64_t total = 0;
    for (size_t r = 0; r < d->reps; r++) {
        const sof_string_t *views = opaque(d->views), *other_views = opaque(d->other_views);
        for (size_t i = 0; i < d->count; i++) {
            total += sof_string_cmp(&views[i], &other_views[i]) < 0;
        }
    }
    sink += (uint64_t)total;
}

static void copy_c(data_t *d) {
    for (size_t r = 0; r < d->reps; r++) {
        const char *text = opaque(d->text);
        for (size_t i = 0; i < d->count; i++) {
            const char *s = text + i * (d->n + 1);
            size_t length = strlen(s);
            char *copy = grab(length + 1);
            memcpy(copy, s, length + 1);
            sink += (unsigned char)copy[length - 1];
            sof_alloc_free(NULL, copy, length + 1);
        }
    }
}

static void copy_sof(data_t *d) {
    for (size_t r = 0; r < d->reps; r++) {
        const sof_string_t *views = opaque(d->views);
        for (size_t i = 0; i < d->count; i++) {
            sof_string_t copy;
            if (sof_string_dup(&copy, &views[i], NULL) != SOF_STRING_OK) {
                fprintf(stderr, "bench: out of memory\n");
                exit(1);
            }
            sink += (unsigned char)copy.data[copy.length - 1];
            sof_string_free(&copy);
        }
    }
}

/// @brief Frees what data_init() allocated.
static void data_free(data_t *d) {
    size_t bytes = d->count * (d->n + 1);
    sof_alloc_free(NULL, d->text, bytes);
    sof_alloc_free(NULL, d->other, bytes);
    sof_alloc_free(NULL, d->lower, bytes);
    sof_alloc_free(NULL, d->views, d->count * sizeof(sof_string_t));
    sof_alloc_free(NULL, d->other_views, d->count * sizeof(sof_string_t));
    for (size_t i = 0; i < d->count; i++) {
        sof_string_free(&d->owned[i]);
    }
    sof_alloc_free(NULL, d->owned, d->count * sizeof(sof_string_t));
    sof_string_free(&d->roomy);
    sof_alloc_free(NULL, d->buffer, d->n + PIECE + 1);
}

/// @brief Makes strings of n random letters, of both cases, as many as make
/// DATA bytes, in every form the measures use.
static void data_init(data_t *d, size_t n) {
    static sof_random_t rng;
    sof_random_init(&rng, n);
    d->n = n;
    d->count = n < DATA ? DATA / n : 1;
    d->reps = d->count * n < WORK ? WORK / (d->count * n) : 1;
    size_t bytes = d->count * (n + 1);
    d->text = grab(bytes);
    d->other = grab(bytes);
    d->lower = grab(bytes);
    d->views = grab(d->count * sizeof(sof_string_t));
    d->other_views = grab(d->count * sizeof(sof_string_t));
    d->owned = grab(d->count * sizeof(sof_string_t));
    for (size_t i = 0; i < d->count; i++) {
        char *s = d->text + i * (n + 1);
        for (size_t k = 0; k < n; k++) {
            uint64_t r = sof_random_next(&rng) % 52;
            s[k] = (char)(r < 26 ? 'a' + r : 'A' + (r - 26));
        }
        s[n] = '\0';
        char *o = d->other + i * (n + 1);
        memcpy(o, s, n + 1);
        o[n - 1] = s[n - 1] == 'z' ? 'y' : 'z';
        memcpy(d->lower + i * (n + 1), s, n + 1);
        d->views[i] = sof_string_view(s, n);
        d->other_views[i] = sof_string_view(o, n);
        if (sof_string_from_bytes(&d->owned[i], s, n, NULL) != SOF_STRING_OK) {
            fprintf(stderr, "bench: out of memory\n");
            exit(1);
        }
    }
    if (sof_string_init(&d->roomy, n + PIECE, NULL) != SOF_STRING_OK || sof_string_append_bytes(&d->roomy, d->text, n) != SOF_STRING_OK) {
        fprintf(stderr, "bench: out of memory\n");
        exit(1);
    }
    d->buffer = grab(n + PIECE + 1);
    memcpy(d->buffer, d->text, n + 1);
}

/// @brief Allocates size bytes, or exits.
static void *grab(size_t size) {
    void *p = sof_alloc_malloc(NULL, size);
    if (p == NULL) {
        fprintf(stderr, "bench: out of memory\n");
        exit(1);
    }
    return p;
}

/// @brief Puts a piece in the middle of the c string and takes it out
/// again, finding its length as c strings must.
static void insert_c(data_t *d) {
    char *s = d->buffer;
    for (size_t r = 0; r < per_rep(d); r++) {
        size_t length = strlen(s), middle = length / 2;
        memmove(s + middle + PIECE, s + middle, length - middle + 1);
        memcpy(s + middle, piece, PIECE);
        memmove(s + middle, s + middle + PIECE, strlen(s + middle + PIECE) + 1);
    }
    sink += (unsigned char)s[0];
}

/// @brief Puts a piece in the middle of the string and takes it out again.
static void insert_sof(data_t *d) {
    sof_string_t *s = &d->roomy;
    for (size_t r = 0; r < per_rep(d); r++) {
        size_t middle = s->length / 2;
        sof_string_insert_bytes(s, middle, piece, PIECE);
        sof_string_delete(s, middle, PIECE);
    }
    sink += (unsigned char)s->data[0];
}

static void length_c(data_t *d) {
    size_t total = 0;
    for (size_t r = 0; r < d->reps; r++) {
        const char *text = opaque(d->text);
        for (size_t i = 0; i < d->count; i++) {
            total += strlen(text + i * (d->n + 1));
        }
    }
    sink += total;
}

/// @brief The calls a run of the length field makes.
static size_t length_calls(data_t *d) {
    return d->count * length_reps(d);
}

/// @brief The times a run of the length field goes over the strings.
static size_t length_reps(data_t *d) {
    size_t reps = LENGTH_CALLS / d->count;
    return reps > d->reps ? reps : d->reps;
}

static void length_sof(data_t *d) {
    size_t total = 0, reps = length_reps(d);
    for (size_t r = 0; r < reps; r++) {
        const sof_string_t *views = opaque(d->views);
        for (size_t i = 0; i < d->count; i++) {
            total += views[i].length;
        }
    }
    sink += total;
}

/// @brief Returns p, through a volatile.
/// @details Each pass over the strings gets its pointers through this, so
/// that the compiler can't tell they are the same as the last pass's, and
/// work out the results of the passes after the first, such as lengths
/// that haven't changed, without doing them.
static void *opaque(void *p) {
    void *volatile v = p;
    return v;
}

/// @brief The inserts a run makes: enough to move WORK bytes.
static size_t per_rep(data_t *d) {
    return d->n < WORK ? WORK / d->n : 1;
}

/// @brief The calls a run makes that goes over every string reps times.
static size_t per_string(data_t *d) {
    return d->count * d->reps;
}

/// @brief Returns the best time, in seconds, of RUNS runs.
static double time_run(void (*run)(data_t *d), data_t *d, sof_timer_t *timer) {
    double best = 1e30;
    for (int i = 0; i < RUNS; i++) {
        sof_timer_start(timer);
        run(d);
        sof_timer_stop(timer);
        double elapsed = sof_timer_elapsed_seconds(timer);
        if (elapsed < best) {
            best = elapsed;
        }
    }
    return best;
}

static void tolower_c(data_t *d) {
    for (size_t r = 0; r < d->reps; r++) {
        char *lower = opaque(d->lower);
        for (size_t i = 0; i < d->count; i++) {
            for (char *p = lower + i * (d->n + 1); *p != '\0'; p++) {
                *p = (char)tolower((unsigned char)*p);
            }
        }
    }
    sink += (unsigned char)d->lower[0];
}

static void tolower_sof(data_t *d) {
    for (size_t r = 0; r < d->reps; r++) {
        sof_string_t *owned = opaque(d->owned);
        for (size_t i = 0; i < d->count; i++) {
            sof_string_tolower(&owned[i]);
        }
    }
    sink += (unsigned char)d->owned[0].data[0];
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
    if (index < argc) {
        fprintf(stderr, "bench: no arguments, please\n");
        usage(stderr);
        return 2;
    }
    // The sizes: 16 bytes, then 8 times as many each time, to the longest.
    size_t sizes[16], num_sizes = 0;
    for (size_t n = SIZE_MIN; n <= settings.bytes && num_sizes < 16; n *= 8) {
        sizes[num_sizes++] = n;
    }
    // The timings, measure by measure, size by size: sof_string, then c.
    static double times[NUM_MEASURES][16][2];
    static size_t calls[NUM_MEASURES][16][2];
    sof_timer_t timer;
    sof_timer_init(&timer);
    sof_timer_calibrate(&timer);
    for (size_t s = 0; s < num_sizes; s++) {
        data_t d;
        data_init(&d, sizes[s]);
        for (size_t m = 0; m < NUM_MEASURES; m++) {
            times[m][s][0] = time_run(measures[m].sof, &d, &timer);
            times[m][s][1] = time_run(measures[m].c, &d, &timer);
            calls[m][s][1] = measures[m].calls(&d);
            calls[m][s][0] = measures[m].sof_calls != NULL ? measures[m].sof_calls(&d) : calls[m][s][1];
        }
        data_free(&d);
    }
    printf("ns per call, best of %d runs; c / sof is how many times as long c strings take\n", RUNS);
    for (size_t m = 0; m < NUM_MEASURES; m++) {
        printf("\n%s: %s\n", measures[m].name, measures[m].what);
        printf("%10s %11s %11s %8s\n", "bytes", "sof_string", "c", "c / sof");
        for (size_t s = 0; s < num_sizes; s++) {
            double sof = times[m][s][0] / (double)calls[m][s][0] * 1e9;
            double c = times[m][s][1] / (double)calls[m][s][1] * 1e9;
            printf("%10zu %11.2f %11.2f %8.1f\n", sizes[s], sof, c, sof > 0 ? c / sof : 0.0);
        }
    }
    sof_timer_free(&timer);
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: bench [-h] [-n bytes]\n");
    sof_copt_print(stream);
}
