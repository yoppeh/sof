/**
 * @file bench.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Compares sof_base's encoding and decoding with simple versions.
 * @details Random data is encoded and decoded in every encoding by
 * sof_base_encode() and sof_base_decode(), strictly, and by a simple
 * version of each that works a byte or a character at a time: the encoder
 * shifts each byte into an accumulator and takes characters out of it, and
 * the decoder finds each character's value by comparing it with the
 * alphabet's ranges, as code written for one use usually does. Both report
 * megabytes of data a second (MB/s, 10^6 bytes), for encoding and for
 * decoding; decoding is measured on the bytes it gives, not the
 * characters. Every timing is the best of several runs.
 *
 * Usage: bench [-h] [-s megabytes]
 * where megabytes is the size of the data (default 4).
 * @version 0.0.1
 * @date 2026-11-20
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_base.h"
#include "sof_copt.h"
#include "sof_random.h"
#include "sof_timer.h"

/// @brief Each timing is the best of this many runs.
#define RUNS 7

/// @brief The megabytes of data, unless -s is given.
#define MEGABYTES 4

/// @brief How an encoding is named, and what the simple versions need of
/// it.
typedef struct encoding_info_t {
    const char *name;
    sof_base_encoding_t encoding;
    const char *alphabet;
    unsigned bits;
    unsigned chars;
} encoding_info_t;

/// @brief The best rates, in MB/s, of one encoding.
typedef struct result_t {
    double encode;
    double decode;
    double simple_encode;
    double simple_decode;
} result_t;

static result_t bench_encoding(const encoding_info_t *info, const unsigned char *data, size_t length, char *text, unsigned char *back);
static void fail(const char *what);
static double rate(size_t bytes);
static int simple_decode(const encoding_info_t *info, const char *text, size_t length, unsigned char *data, size_t *written);
static size_t simple_encode(const encoding_info_t *info, const unsigned char *data, size_t length, char *text);
static int simple_value(sof_base_encoding_t encoding, unsigned char c);
static void usage(FILE *stream);

/// @brief Every result is added in here, so the compiler can't skip one.
static volatile size_t sink;

/// @brief Times each step; calibrated once.
static sof_timer_t timer;

/// @brief The encodings, in the order they are reported.
static const encoding_info_t encodings[] = {
    {"base16", SOF_BASE_16, "0123456789ABCDEF", 4, 2},
    {"base32", SOF_BASE_32, "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", 5, 8},
    {"base32hex", SOF_BASE_32HEX, "0123456789ABCDEFGHIJKLMNOPQRSTUV", 5, 8},
    {"base64", SOF_BASE_64, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", 6, 4},
    {"base64url", SOF_BASE_64URL, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_", 6, 4},
};

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_size = {
    .option = "-s",
    .description = "the megabytes of data to encode and decode (default 4)",
    .type = SOF_COPT_ARG_REQUIRED,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_size,
};

/// @brief Encodes and decodes length bytes of data in one encoding, with
/// sof_base and with the simple versions, checking that each gives the
/// other's result, and returns the best rate of each.
static result_t bench_encoding(const encoding_info_t *info, const unsigned char *data, size_t length, char *text, unsigned char *back) {
    result_t best = {0};
    size_t size = sof_base_encoded_size(info->encoding, 0, length);
    for (int run = 0; run < RUNS; run++) {
        size_t written;
        sof_timer_start(&timer);
        if (sof_base_encode(info->encoding, 0, data, length, text, size, &written) != SOF_BASE_OK) {
            fail("sof_base_encode() failed");
        }
        sof_timer_stop(&timer);
        double r = rate(length);
        best.encode = r > best.encode ? r : best.encode;
        sink += (size_t)text[written - 1];

        sof_timer_start(&timer);
        if (sof_base_decode(info->encoding, 0, text, size, back, length, &written, NULL) != SOF_BASE_OK || written != length) {
            fail("sof_base_decode() failed");
        }
        sof_timer_stop(&timer);
        r = rate(length);
        best.decode = r > best.decode ? r : best.decode;
        sink += back[length - 1];
        if (memcmp(back, data, length) != 0) {
            fail("sof_base_decode() didn't give the data back");
        }

        sof_timer_start(&timer);
        written = simple_encode(info, data, length, text);
        sof_timer_stop(&timer);
        r = rate(length);
        best.simple_encode = r > best.simple_encode ? r : best.simple_encode;
        sink += (size_t)text[written - 1];
        if (written != size) {
            fail("the simple encoder gave the wrong length");
        }

        memset(back, 0, length);
        sof_timer_start(&timer);
        if (simple_decode(info, text, size, back, &written) != 0) {
            fail("the simple decoder failed");
        }
        sof_timer_stop(&timer);
        r = rate(length);
        best.simple_decode = r > best.simple_decode ? r : best.simple_decode;
        sink += back[length - 1];
        if (written != length || memcmp(back, data, length) != 0) {
            fail("the simple decoder didn't give the data back");
        }
    }
    return best;
}

/// @brief Prints what failed and exits.
static void fail(const char *what) {
    fprintf(stderr, "bench: %s\n", what);
    exit(1);
}

/// @brief The rate of the last timing, in MB/s, for bytes bytes.
static double rate(size_t bytes) {
    uint64_t ns = sof_timer_elapsed_ns(&timer);
    return ns == 0 ? 0 : (double)bytes * 1000.0 / (double)ns;
}

/// @brief Decodes padded text a character at a time, finding each one's
/// value by its range in the alphabet and shifting it into an accumulator,
/// a byte out whenever it holds eight bits. Padding ends the data.
/// @return 0, or -1 if a character isn't in the alphabet.
static int simple_decode(const encoding_info_t *info, const char *text, size_t length, unsigned char *data, size_t *written) {
    uint32_t acc = 0;
    unsigned held = 0;
    size_t n = 0;
    for (size_t i = 0; i < length && text[i] != '='; i++) {
        int v = simple_value(info->encoding, (unsigned char)text[i]);
        if (v < 0) {
            return -1;
        }
        acc = acc << info->bits | (uint32_t)v;
        held += info->bits;
        if (held >= 8) {
            held -= 8;
            data[n++] = (unsigned char)(acc >> held);
        }
    }
    *written = n;
    return 0;
}

/// @brief Encodes data a byte at a time, shifting each into an accumulator
/// and taking a character out whenever it holds enough bits, then pads.
/// @return The characters written.
static size_t simple_encode(const encoding_info_t *info, const unsigned char *data, size_t length, char *text) {
    uint32_t acc = 0, mask = (1u << info->bits) - 1;
    unsigned held = 0;
    size_t n = 0;
    for (size_t i = 0; i < length; i++) {
        acc = acc << 8 | data[i];
        held += 8;
        while (held >= info->bits) {
            held -= info->bits;
            text[n++] = info->alphabet[acc >> held & mask];
        }
    }
    if (held != 0) {
        text[n++] = info->alphabet[acc << (info->bits - held) & mask];
    }
    while (info->encoding != SOF_BASE_16 && n % info->chars != 0) {
        text[n++] = '=';
    }
    return n;
}

/// @brief The value of a character in an encoding, by comparing it with the
/// alphabet's ranges, or -1 if it isn't in it.
static int simple_value(sof_base_encoding_t encoding, unsigned char c) {
    switch (encoding) {
    case SOF_BASE_16:
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return -1;
    case SOF_BASE_32:
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }
        if (c >= '2' && c <= '7') {
            return c - '2' + 26;
        }
        return -1;
    case SOF_BASE_32HEX:
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'A' && c <= 'V') {
            return c - 'A' + 10;
        }
        return -1;
    default:
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9') {
            return c - '0' + 52;
        }
        if (encoding == SOF_BASE_64) {
            return c == '+' ? 62 : c == '/' ? 63 : -1;
        }
        return c == '-' ? 62 : c == '_' ? 63 : -1;
    }
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: bench [-h] [-s megabytes]\n");
    sof_copt_print(stream);
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
    if (index < argc) {
        fprintf(stderr, "bench: no arguments, please\n");
        usage(stderr);
        return 2;
    }
    size_t megabytes = MEGABYTES;
    if (opt_size.present) {
        const char *s = opt_size.value;
        char *end;
        errno = 0;
        unsigned long long value = strtoull(s, &end, 10);
        if (*s < '0' || *s > '9' || *end != '\0' || errno != 0 || value == 0 || value > 4096) {
            fprintf(stderr, "bench: -s needs a number of megabytes from 1 to 4096, not \"%s\"\n", s);
            usage(stderr);
            return 2;
        }
        megabytes = (size_t)value;
    }
    size_t length = megabytes * 1000000;
    size_t text_size = sof_base_encoded_size(SOF_BASE_16, 0, length);
    unsigned char *data = sof_alloc_malloc(NULL, length);
    unsigned char *back = sof_alloc_malloc(NULL, length);
    char *text = sof_alloc_malloc(NULL, text_size);
    if (data == NULL || back == NULL || text == NULL) {
        fail("out of memory");
    }
    sof_random_t rng;
    sof_random_init(&rng, 1);
    sof_random_bytes(&rng, data, length);
    sof_timer_init(&timer);
    sof_timer_calibrate(&timer);

    printf("%zu MB of random data, MB/s of data, best of %d runs\n\n", megabytes, RUNS);
    printf("| | encode | simple encode | decode | simple decode |\n");
    printf("|---|---|---|---|---|\n");
    for (size_t i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++) {
        result_t r = bench_encoding(&encodings[i], data, length, text, back);
        printf("| %s | %.0f | %.0f | %.0f | %.0f |\n", encodings[i].name, r.encode, r.simple_encode, r.decode, r.simple_decode);
        fflush(stdout);
    }
    sof_alloc_free(NULL, data, length);
    sof_alloc_free(NULL, back, length);
    sof_alloc_free(NULL, text, text_size);
    sof_timer_free(&timer);
    sof_random_free(&rng);
    return 0;
}
