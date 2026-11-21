/**
 * @file bench.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Measures the digests and CRCs of sof_digest, and zlib's crc32().
 * @details For each of CRC-32, CRC-32C, MD5, SHA-1, SHA-256 and SHA-512,
 * gives the gigabytes per second the one-shot function takes in, on random
 * data in pieces of 64 and 256 bytes, 4 KiB and 1 MiB, each the best of
 * several runs. SHA-224 runs as SHA-256 does, and SHA-384 and SHA-512/256
 * as SHA-512 does, so they aren't measured apart. Where zlib is installed,
 * the Makefile builds this with HAVE_ZLIB and links it with -lz, and zlib's
 * crc32() is measured too.
 *
 * The Makefile builds it twice: bench, with sof_digest.c as usual, which
 * uses the processor's instructions where it has them, and bench-portable,
 * with sof_digest.c and this file compiled with SOF_DIGEST_PORTABLE, which
 * uses the portable code everywhere.
 *
 * Usage: bench [-h] [-n megabytes]
 * @version 0.0.1
 * @date 2026-11-20
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_ZLIB)
#include <zlib.h>
#endif

#include "sof_alloc.h"
#include "sof_copt.h"
#include "sof_digest.h"
#include "sof_random.h"
#include "sof_timer.h"

/// @brief Each timing is the best of this many runs.
#define RUNS 5

/// @brief The bytes of random data the pieces are taken from, in turn.
#define AREA (8u << 20)

/// @brief A function measured: takes length bytes at data and returns
/// something of the result, for the sink.
typedef uint64_t (*measured_func_t)(const void *data, size_t length);

/// @brief A row of the table.
typedef struct row_t {
    const char *name;
    measured_func_t func;
} row_t;

static uint64_t run_crc32(const void *data, size_t length);
static uint64_t run_crc32c(const void *data, size_t length);
static uint64_t run_md5(const void *data, size_t length);
static uint64_t run_sha1(const void *data, size_t length);
static uint64_t run_sha256(const void *data, size_t length);
static uint64_t run_sha512(const void *data, size_t length);
#if defined(HAVE_ZLIB)
static uint64_t run_zlib(const void *data, size_t length);
#endif
static double measure(measured_func_t func, const unsigned char *area, size_t length, size_t total, sof_timer_t *timer);
static int megabytes_validate(sof_copt_t *option, void *ctx);
static uint64_t prefix(const unsigned char *digest);
static void usage(FILE *stream);

/// @brief Every result is added in here, so the compiler can't skip work.
static volatile uint64_t sink;

/// @brief The megabytes each run takes in, from -n.
static size_t megabytes = 64;

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_megabytes = {
    .option = "-n",
    .description = "take in this many megabytes in each run (default 64)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = megabytes_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_megabytes,
};

/// @brief The rows of the table, in order.
static const row_t rows[] = {
    {"CRC-32", run_crc32},
#if defined(HAVE_ZLIB)
    {"zlib crc32", run_zlib},
#endif
    {"CRC-32C", run_crc32c},
    {"MD5", run_md5},
    {"SHA-1", run_sha1},
    {"SHA-256", run_sha256},
    {"SHA-512", run_sha512},
};

/// @brief The pieces' sizes, the table's columns.
static const size_t lengths[] = {64, 256, 4096, 1 << 20};

/// @brief Times func over total bytes, in pieces of length taken in turn
/// from the AREA bytes at area, RUNS times.
/// @return The best run's gigabytes per second.
static double measure(measured_func_t func, const unsigned char *area, size_t length, size_t total, sof_timer_t *timer) {
    size_t count = total / length;
    double best = 0;
    for (int run = 0; run < RUNS; run++) {
        uint64_t result = 0;
        size_t offset = 0;
        sof_timer_start(timer);
        for (size_t i = 0; i < count; i++) {
            result += func(area + offset, length);
            offset += length;
            if (offset + length > AREA) {
                offset = 0;
            }
        }
        sof_timer_stop(timer);
        sink += result;
        double seconds = sof_timer_elapsed_seconds(timer);
        double rate = seconds > 0 ? (double)count * (double)length / seconds / 1e9 : 0;
        if (rate > best) {
            best = rate;
        }
    }
    return best;
}

/// @brief Reads -n's argument, a number of megabytes from 1 to 65536.
static int megabytes_validate(sof_copt_t *option, void *ctx) {
    (void)ctx;
    char *end;
    unsigned long n = strtoul(option->value, &end, 10);
    if (*end != '\0' || n < 1 || n > 65536) {
        fprintf(stderr, "-n takes a number of megabytes from 1 to 65536, not %s\n", option->value);
        return 1;
    }
    megabytes = (size_t)n;
    return 0;
}

/// @brief The first 8 bytes of a digest, for the sink.
static uint64_t prefix(const unsigned char *digest) {
    uint64_t x;
    memcpy(&x, digest, sizeof(x));
    return x;
}

static uint64_t run_crc32(const void *data, size_t length) {
    return sof_digest_crc32(data, length);
}

static uint64_t run_crc32c(const void *data, size_t length) {
    return sof_digest_crc32c(data, length);
}

static uint64_t run_md5(const void *data, size_t length) {
    unsigned char digest[SOF_DIGEST_MD5_SIZE];
    sof_digest_md5(data, length, digest);
    return prefix(digest);
}

static uint64_t run_sha1(const void *data, size_t length) {
    unsigned char digest[SOF_DIGEST_SHA1_SIZE];
    sof_digest_sha1(data, length, digest);
    return prefix(digest);
}

static uint64_t run_sha256(const void *data, size_t length) {
    unsigned char digest[SOF_DIGEST_SHA256_SIZE];
    sof_digest_sha256(data, length, digest);
    return prefix(digest);
}

static uint64_t run_sha512(const void *data, size_t length) {
    unsigned char digest[SOF_DIGEST_SHA512_SIZE];
    sof_digest_sha512(data, length, digest);
    return prefix(digest);
}

#if defined(HAVE_ZLIB)

static uint64_t run_zlib(const void *data, size_t length) {
    return crc32(0, data, (uInt)length);
}

#endif

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: bench [-h] [-n megabytes]\n");
    sof_copt_print(stream);
}

int main(int ac, char **av) {
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, NULL, &index, &option);
    if (err != SOF_COPT_OK || index != ac) {
        if (err == SOF_COPT_NO_MATCH || index != ac) {
            fprintf(stderr, "unknown argument %s\n", av[index]);
        } else if (err == SOF_COPT_MISSING_ARGUMENT) {
            fprintf(stderr, "%s needs an argument\n", option->option);
        }
        usage(stderr);
        return 2;
    }
    if (opt_help.present) {
        usage(stdout);
        return 0;
    }
    unsigned char *area = sof_alloc_malloc(NULL, AREA);
    if (area == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    sof_random_t rng;
    sof_random_init(&rng, 1);
    sof_random_bytes(&rng, area, AREA);
    sof_random_free(&rng);
    sof_timer_t timer;
    sof_timer_init(&timer);
    sof_timer_calibrate(&timer);

#if defined(SOF_DIGEST_PORTABLE)
    printf("sof_digest, portable code: GB/s, best of %d runs of %zu MB\n", RUNS, megabytes);
#else
    printf("sof_digest: GB/s, best of %d runs of %zu MB\n", RUNS, megabytes);
#endif
    printf("%-12s %9s %9s %9s %9s\n", "", "64 B", "256 B", "4 KiB", "1 MiB");
    for (size_t r = 0; r < sizeof(rows) / sizeof(rows[0]); r++) {
        printf("%-12s", rows[r].name);
        fflush(stdout);
        for (size_t l = 0; l < sizeof(lengths) / sizeof(lengths[0]); l++) {
            printf(" %9.2f", measure(rows[r].func, area, lengths[l], megabytes << 20, &timer));
            fflush(stdout);
        }
        printf("\n");
    }
    sof_timer_free(&timer);
    sof_alloc_free(NULL, area, AREA);
    return 0;
}
