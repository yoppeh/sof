/**
 * @file bench.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Measures sof_real against the c library, and checks every float.
 * @details The measures, each on -n values, give the nanoseconds a
 * conversion takes:
 * - format: doubles of random bits, and short decimals such as 12.5 and
 *   0.003, with sof_real_format(); snprintf() with "%.17g", which always
 *   reads back but is rarely the shortest; and the shortest "%g" found by
 *   trying "%.15g", "%.16g" and "%.17g" and reading each back with
 *   strtod(), as sof_json and sof_toml did before they used sof_real.
 * - format float: floats of random bits, with sof_real_format_float() and
 *   snprintf() with "%.9g".
 * - parse: the texts sof_real_format() wrote for the doubles above, and
 *   numbers of 25 random digits, with sof_real_parse() and strtod().
 * - parse float: the texts of the floats, with sof_real_parse_float() and
 *   strtof().
 *
 * Every timing is the best of several runs. With -x it measures nothing,
 * but checks every one of the 2^32 floats instead, on -j threads: that its
 * text reads back as it, by sof_real_parse_float() and by strtof(); that
 * no decimal of one digit fewer reads back; that if another of the same
 * length reads back, the one printf() rounds to is the one written or
 * doesn't; and, for every positive finite float but the largest, that
 * the decimal exactly halfway to the next float up, and the doubles either
 * side of it, read as strtof() and strtod() read them.
 *
 * Usage: bench [-h] [-n count] [-x] [-j threads]
 * @version 0.0.1
 * @date 2026-11-20
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <float.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_copt.h"
#include "sof_random.h"
#include "sof_real.h"
#include "sof_timer.h"

/// @brief Each timing is the best of this many runs.
#define RUNS 5

/// @brief The bytes each value's text has in the measures' text arrays.
#define TEXT 32

/// @brief The most significant digits a double needs to read back.
#define DOUBLE_DIGITS 17

/// @brief The code a validate callback returns when an argument isn't a
/// number in range, after printing why.
#define BAD_NUMBER 1

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The values each measure converts.
    size_t count;
    /// @brief The threads -x checks on.
    size_t threads;
} settings_t;

/// @brief The values the measures convert, and their texts, TEXT bytes
/// apart.
typedef struct data_t {
    size_t count;
    double *random;
    double *short_values;
    float *floats;
    char *random_text;
    char *short_text;
    char *long_text;
    char *float_text;
    char *out;
} data_t;

/// @brief What the threads of -x share.
typedef struct check_t {
    pthread_mutex_t lock;
    /// @brief The next block of floats to check.
    uint64_t next;
    uint64_t failures;
} check_t;

static int check_all(size_t threads);
static void check_float(uint32_t bits, check_t *c);
static void check_halfway(uint32_t bits, check_t *c);
static void *check_thread(void *arg);
static int count_missing(sof_copt_t *option, void *ctx);
static int count_validate(sof_copt_t *option, void *ctx);
static void data_free(data_t *d);
static void data_init(data_t *d, size_t count);
static void failed(check_t *c, const char *what, uint32_t bits, const char *text);
static size_t format_g(double value, char *text, size_t size);
static size_t format_g_search(double value, char *text, size_t size);
static void *grab(size_t size);
static void measure(const char *name, void (*sof)(data_t *d), void (*c)(data_t *d), data_t *d, sof_timer_t *timer);
static void format_float_printf(data_t *d);
static void format_float_sof(data_t *d);
static void format_random_g(data_t *d);
static void format_random_printf(data_t *d);
static void format_random_sof(data_t *d);
static void format_short_g(data_t *d);
static void format_short_printf(data_t *d);
static void format_short_sof(data_t *d);
static void parse_float_sof(data_t *d);
static void parse_float_strtof(data_t *d);
static void parse_long_sof(data_t *d);
static void parse_long_strtod(data_t *d);
static void parse_random_sof(data_t *d);
static void parse_random_strtod(data_t *d);
static void parse_short_sof(data_t *d);
static void parse_short_strtod(data_t *d);
static void parse_sof(const char *texts, size_t count);
static void parse_strtod(const char *texts, size_t count);
static int reads_back(uint64_t digits, int exponent, uint32_t bits);
static double time_run(void (*run)(data_t *d), data_t *d, sof_timer_t *timer);
static int threads_missing(sof_copt_t *option, void *ctx);
static int threads_validate(sof_copt_t *option, void *ctx);
static void usage(FILE *stream);

/// @brief Every result is added in here, so the compiler can't skip work.
static volatile uint64_t sink;

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_count = {
    .option = "-n",
    .description = "convert this many values in each measure (default 1000000)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = count_validate,
    .missing = count_missing,
};

static sof_copt_t opt_check = {
    .option = "-x",
    .description = "check every float instead of measuring",
};

static sof_copt_t opt_threads = {
    .option = "-j",
    .description = "check on this many threads (default 8)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = threads_validate,
    .missing = threads_missing,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_count,
    &opt_check,
    &opt_threads,
};

/// @brief Checks every float on threads threads, printing the first
/// failures and a summary.
/// @return 0 if every float passed, 1 if not.
static int check_all(size_t threads) {
    check_t c;
    pthread_t ids[256];
    pthread_mutex_init(&c.lock, NULL);
    c.next = 0;
    c.failures = 0;
    for (size_t i = 0; i < threads; i++) {
        if (pthread_create(&ids[i], NULL, check_thread, &c) != 0) {
            fprintf(stderr, "bench: can't start a thread\n");
            exit(1);
        }
    }
    for (size_t i = 0; i < threads; i++) {
        pthread_join(ids[i], NULL);
    }
    pthread_mutex_destroy(&c.lock);
    printf("checked all 4294967296 floats: %llu failures\n", (unsigned long long)c.failures);
    return c.failures != 0;
}

/// @brief Checks one float's text, as the description at the top says.
static void check_float(uint32_t bits, check_t *c) {
    float v, back;
    char text[SOF_REAL_FORMAT_SIZE], other[64];
    size_t used;
    memcpy(&v, &bits, sizeof(v));
    size_t length = sof_real_format_float(v, text, sizeof(text));
    uint32_t magnitude = bits & 0x7fffffff;
    if (magnitude > 0x7f800000) {
        if (strcmp(text, "nan") != 0) {
            failed(c, "nan", bits, text);
        }
        return;
    }
    if (magnitude == 0x7f800000) {
        if (strcmp(text, bits == magnitude ? "inf" : "-inf") != 0) {
            failed(c, "infinity", bits, text);
        }
        return;
    }
    uint32_t b;
    if (sof_real_parse_float(text, length, &back, &used) != SOF_REAL_OK || used != length) {
        failed(c, "parse", bits, text);
    }
    memcpy(&b, &back, sizeof(b));
    if (b != bits) {
        failed(c, "read back", bits, text);
    }
    back = strtof(text, NULL);
    memcpy(&b, &back, sizeof(b));
    if (b != bits) {
        failed(c, "strtof", bits, text);
    }
    uint64_t digits;
    int exponent;
    if (magnitude == 0 || sof_real_decimal_float(v, &digits, &exponent) != SOF_REAL_OK) {
        return;
    }
    if (digits % 10 == 0) {
        failed(c, "trailing zero", bits, text);
    }
    if (digits >= 10 && (reads_back(digits / 10, exponent + 1, magnitude) || reads_back(digits / 10 + 1, exponent + 1, magnitude))) {
        failed(c, "not shortest", bits, text);
    }
    if (reads_back(digits - 1, exponent, magnitude) || reads_back(digits + 1, exponent, magnitude)) {
        int n = 0;
        for (uint64_t x = digits; x != 0; x /= 10) {
            n++;
        }
        float m;
        memcpy(&m, &magnitude, sizeof(m));
        snprintf(other, sizeof(other), "%.*e", n - 1, (double)m);
        uint64_t rounded = 0;
        const char *p = other;
        for (; *p != 'e'; p++) {
            if (*p >= '0' && *p <= '9') {
                rounded = rounded * 10 + (uint64_t)(*p - '0');
            }
        }
        int power = atoi(p + 1) - (n - 1);
        if (reads_back(rounded, power, magnitude) && (rounded != digits || power != exponent)) {
            failed(c, "not closest", bits, text);
        }
    }
}

/// @brief Checks that the point halfway between a positive finite float
/// and the next one up, and the doubles either side of it, read as strtof()
/// and strtod() read them.
static void check_halfway(uint32_t bits, check_t *c) {
    float v, next, a, b;
    uint32_t up = bits + 1;
    memcpy(&v, &bits, sizeof(v));
    memcpy(&next, &up, sizeof(next));
    // Exact: a float's halfway point has 25 significant bits.
    double half = ((double)v + (double)next) / 2;
    uint64_t half_bits;
    memcpy(&half_bits, &half, sizeof(half_bits));
    char text[200];
    for (int k = 0; k < 3; k++) {
        double x;
        uint64_t x_bits = half_bits + (uint64_t)k - (k == 2 ? 3 : 0);
        memcpy(&x, &x_bits, sizeof(x));
        // A float's halfway point has at most 105 significant digits.
        snprintf(text, sizeof(text), k == 0 ? "%.110e" : "%.25e", x);
        size_t length = strlen(text);
        sof_real_parse_float(text, length, &a, NULL);
        b = strtof(text, NULL);
        if (memcmp(&a, &b, sizeof(a)) != 0) {
            failed(c, "halfway", bits, text);
        }
        double d, e;
        sof_real_parse(text, length, &d, NULL);
        e = strtod(text, NULL);
        if (memcmp(&d, &e, sizeof(d)) != 0) {
            failed(c, "halfway double", bits, text);
        }
    }
}

/// @brief A thread of -x: takes blocks of 2^20 floats until there are none
/// left.
static void *check_thread(void *arg) {
    check_t *c = arg;
    for (;;) {
        pthread_mutex_lock(&c->lock);
        uint64_t start = c->next;
        c->next += UINT64_C(1) << 20;
        pthread_mutex_unlock(&c->lock);
        if (start > UINT32_MAX) {
            return NULL;
        }
        for (uint64_t bits = start; bits < start + (UINT64_C(1) << 20); bits++) {
            check_float((uint32_t)bits, c);
            if (bits < 0x7f7fffff) {
                check_halfway((uint32_t)bits, c);
            }
        }
    }
}

/// @brief Without -n, each measure converts a million values.
static int count_missing(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    (void)option;
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

static void data_free(data_t *d) {
    sof_alloc_free(NULL, d->random, d->count * sizeof(double));
    sof_alloc_free(NULL, d->short_values, d->count * sizeof(double));
    sof_alloc_free(NULL, d->floats, d->count * sizeof(float));
    sof_alloc_free(NULL, d->random_text, d->count * TEXT);
    sof_alloc_free(NULL, d->short_text, d->count * TEXT);
    sof_alloc_free(NULL, d->long_text, d->count * TEXT);
    sof_alloc_free(NULL, d->float_text, d->count * TEXT);
    sof_alloc_free(NULL, d->out, d->count * TEXT);
}

/// @brief Makes count values of each kind, and their texts.
static void data_init(data_t *d, size_t count) {
    sof_random_t r;
    sof_random_init(&r, 42);
    d->count = count;
    d->random = grab(count * sizeof(double));
    d->short_values = grab(count * sizeof(double));
    d->floats = grab(count * sizeof(float));
    d->random_text = grab(count * TEXT);
    d->short_text = grab(count * TEXT);
    d->long_text = grab(count * TEXT);
    d->float_text = grab(count * TEXT);
    d->out = grab(count * TEXT);
    static const double scales[] = {1, 10, 100, 1000, 10000, 100000};
    for (size_t i = 0; i < count; i++) {
        uint64_t bits;
        do {
            bits = sof_random_next(&r);
        } while ((bits & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL);
        memcpy(&d->random[i], &bits, sizeof(double));
        sof_real_format(d->random[i], d->random_text + i * TEXT, TEXT);
        // Up to 6 digits, with up to 5 after the point.
        d->short_values[i] = (double)sof_random_below(&r, 1000000) / scales[sof_random_below(&r, 6)];
        sof_real_format(d->short_values[i], d->short_text + i * TEXT, TEXT);
        char *p = d->long_text + i * TEXT;
        for (int k = 0; k < 25; k++) {
            p[k] = (char)('0' + sof_random_below(&r, 10));
        }
        p[1] = '.';
        p[0] = (char)('1' + sof_random_below(&r, 9));
        // The exponent, -300 to 300, fits the TEXT - 25 bytes left, but gcc
        // checks the format against any int, so it is formatted apart.
        char exponent[16];
        snprintf(exponent, sizeof(exponent), "e%d", (int)sof_random_below(&r, 601) - 300);
        memcpy(p + 25, exponent, strlen(exponent) + 1);
        uint32_t fbits;
        do {
            fbits = (uint32_t)sof_random_next(&r);
        } while ((fbits & 0x7f800000) == 0x7f800000);
        memcpy(&d->floats[i], &fbits, sizeof(float));
        sof_real_format_float(d->floats[i], d->float_text + i * TEXT, TEXT);
    }
    sof_random_free(&r);
}

/// @brief Counts a failure of -x and prints the first few.
static void failed(check_t *c, const char *what, uint32_t bits, const char *text) {
    pthread_mutex_lock(&c->lock);
    if (c->failures++ < 20) {
        printf("%s: float %08lx, \"%s\"\n", what, (unsigned long)bits, text);
    }
    pthread_mutex_unlock(&c->lock);
}

/// @brief Formats a finite double with the fewest significant digits that
/// read back as it, as sof_json and sof_toml did before sof_real: %.15g,
/// %.16g and %.17g, each read back with strtod(), and a binary search over
/// the precisions for subnormals. Numbers below 1e17 that need fewer
/// digits than their integer part has, such as 100, are written as those
/// digits and zeros rather than as 1e+02. The locale's decimal point is
/// replaced by '.'.
/// @return The length of the text, or 0 if it didn't fit.
static size_t format_g(double value, char *text, size_t size) {
    char g[64];
    int found = 0, n = 0;
    if (value == 0 || value >= DBL_MIN || value <= -DBL_MIN) {
        for (int p = DOUBLE_DIGITS - 2; p <= DOUBLE_DIGITS && !found; p++) {
            n = snprintf(g, sizeof(g), "%.*g", p, value);
            found = n > 0 && (size_t)n < sizeof(g) && strtod(g, NULL) == value;
        }
    }
    if (!found) {
        return format_g_search(value, text, size);
    }
    char digits[DOUBLE_DIGITS + 1];
    int count = 0, whole = 0, point = 0, zeros = 0;
    long exponent = 0;
    const char *c = g;
    if (*c == '-') {
        c++;
    }
    for (; *c != '\0' && *c != 'e'; c++) {
        if (*c < '0' || *c > '9') {
            point = 1;
        } else if (count > 0 || *c != '0') {
            if (count < DOUBLE_DIGITS) {
                digits[count++] = *c;
            }
        } else if (point) {
            zeros++;
        }
        if (!point && *c >= '0' && *c <= '9') {
            whole++;
        }
    }
    if (*c == 'e') {
        exponent = strtol(c + 1, NULL, 10);
    } else if (count > 0) {
        exponent = g[g[0] == '-'] != '0' ? whole - 1 : -(long)zeros - 1;
    }
    while (count > 0 && digits[count - 1] == '0') {
        count--;
    }
    int low = count > 0 ? count : 1;
    size_t to = 0;
    if (exponent >= low && exponent < DOUBLE_DIGITS) {
        if ((size_t)exponent + 3 > size) {
            return 0;
        }
        if (value < 0) {
            text[to++] = '-';
        }
        memcpy(text + to, digits, (size_t)count);
        to += (size_t)count;
        for (long i = low; i <= exponent; i++) {
            text[to++] = '0';
        }
        text[to] = '\0';
        return to;
    }
    if ((size_t)n >= size) {
        return 0;
    }
    for (const char *from = g; *from != '\0'; from++) {
        if ((*from >= '0' && *from <= '9') || *from == '-' || *from == '+' || *from == 'e') {
            text[to++] = *from;
        } else if (to == 0 || text[to - 1] != '.') {
            text[to++] = '.';
        }
    }
    text[to] = '\0';
    return to;
}

/// @brief format_g() for subnormals: the shortest precision by a binary
/// search.
static size_t format_g_search(double value, char *text, size_t size) {
    int low = 1, high = DOUBLE_DIGITS;
    int n;
    while (low < high) {
        int middle = (low + high) / 2;
        n = snprintf(text, size, "%.*g", middle, value);
        if (n > 0 && (size_t)n < size && strtod(text, NULL) == value) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    n = snprintf(text, size, "%.*e", low - 1, value);
    if (n <= 0 || (size_t)n >= size) {
        return 0;
    }
    const char *e = strchr(text, 'e');
    long exponent = e != NULL ? strtol(e + 1, NULL, 10) : 0;
    size_t from = 0, to = 0;
    if (exponent >= low && exponent < DOUBLE_DIGITS) {
        while (text[from] != 'e') {
            char c = text[from++];
            if ((c >= '0' && c <= '9') || c == '-') {
                text[to++] = c;
            }
        }
        for (long i = low; i <= exponent; i++) {
            text[to++] = '0';
        }
        text[to] = '\0';
        return to;
    }
    n = snprintf(text, size, "%.*g", low, value);
    if (n <= 0 || (size_t)n >= size) {
        return 0;
    }
    while (text[from] != '\0') {
        char c = text[from++];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == 'e') {
            text[to++] = c;
        } else if (to == 0 || text[to - 1] != '.') {
            text[to++] = '.';
        }
    }
    text[to] = '\0';
    return to;
}

static void format_float_printf(data_t *d) {
    size_t total = 0;
    for (size_t i = 0; i < d->count; i++) {
        total += (size_t)snprintf(d->out + i * TEXT, TEXT, "%.9g", (double)d->floats[i]);
    }
    sink += total;
}

static void format_float_sof(data_t *d) {
    size_t total = 0;
    for (size_t i = 0; i < d->count; i++) {
        total += sof_real_format_float(d->floats[i], d->out + i * TEXT, TEXT);
    }
    sink += total;
}

static void format_random_g(data_t *d) {
    size_t total = 0;
    for (size_t i = 0; i < d->count; i++) {
        total += format_g(d->random[i], d->out + i * TEXT, TEXT);
    }
    sink += total;
}

static void format_random_printf(data_t *d) {
    size_t total = 0;
    for (size_t i = 0; i < d->count; i++) {
        total += (size_t)snprintf(d->out + i * TEXT, TEXT, "%.17g", d->random[i]);
    }
    sink += total;
}

static void format_random_sof(data_t *d) {
    size_t total = 0;
    for (size_t i = 0; i < d->count; i++) {
        total += sof_real_format(d->random[i], d->out + i * TEXT, TEXT);
    }
    sink += total;
}

static void format_short_g(data_t *d) {
    size_t total = 0;
    for (size_t i = 0; i < d->count; i++) {
        total += format_g(d->short_values[i], d->out + i * TEXT, TEXT);
    }
    sink += total;
}

static void format_short_printf(data_t *d) {
    size_t total = 0;
    for (size_t i = 0; i < d->count; i++) {
        total += (size_t)snprintf(d->out + i * TEXT, TEXT, "%.17g", d->short_values[i]);
    }
    sink += total;
}

static void format_short_sof(data_t *d) {
    size_t total = 0;
    for (size_t i = 0; i < d->count; i++) {
        total += sof_real_format(d->short_values[i], d->out + i * TEXT, TEXT);
    }
    sink += total;
}

/// @brief Allocates memory for the data, or exits.
static void *grab(size_t size) {
    void *p = sof_alloc_malloc(NULL, size);
    if (p == NULL) {
        fprintf(stderr, "bench: out of memory\n");
        exit(1);
    }
    return p;
}

/// @brief Times one measure both ways and prints a line of the table.
static void measure(const char *name, void (*sof)(data_t *d), void (*c)(data_t *d), data_t *d, sof_timer_t *timer) {
    double ns_sof = time_run(sof, d, timer) / (double)d->count * 1e9;
    double ns_c = time_run(c, d, timer) / (double)d->count * 1e9;
    printf("%-32s %10.1f %10.1f %8.1f\n", name, ns_sof, ns_c, ns_c / ns_sof);
}

static void parse_float_sof(data_t *d) {
    uint32_t total = 0;
    for (size_t i = 0; i < d->count; i++) {
        const char *t = d->float_text + i * TEXT;
        float v;
        uint32_t b;
        sof_real_parse_float(t, strlen(t), &v, NULL);
        memcpy(&b, &v, sizeof(b));
        total += b;
    }
    sink += total;
}

static void parse_float_strtof(data_t *d) {
    uint32_t total = 0;
    for (size_t i = 0; i < d->count; i++) {
        float v = strtof(d->float_text + i * TEXT, NULL);
        uint32_t b;
        memcpy(&b, &v, sizeof(b));
        total += b;
    }
    sink += total;
}

/// @brief Parses each text of an array with sof_real_parse().
static void parse_sof(const char *texts, size_t count) {
    uint64_t total = 0;
    for (size_t i = 0; i < count; i++) {
        const char *t = texts + i * TEXT;
        double v;
        uint64_t b;
        sof_real_parse(t, strlen(t), &v, NULL);
        memcpy(&b, &v, sizeof(b));
        total += b;
    }
    sink += total;
}

/// @brief Parses each text of an array with strtod().
static void parse_strtod(const char *texts, size_t count) {
    uint64_t total = 0;
    for (size_t i = 0; i < count; i++) {
        double v = strtod(texts + i * TEXT, NULL);
        uint64_t b;
        memcpy(&b, &v, sizeof(b));
        total += b;
    }
    sink += total;
}

static void parse_long_sof(data_t *d) {
    parse_sof(d->long_text, d->count);
}

static void parse_long_strtod(data_t *d) {
    parse_strtod(d->long_text, d->count);
}

static void parse_random_sof(data_t *d) {
    parse_sof(d->random_text, d->count);
}

static void parse_random_strtod(data_t *d) {
    parse_strtod(d->random_text, d->count);
}

static void parse_short_sof(data_t *d) {
    parse_sof(d->short_text, d->count);
}

static void parse_short_strtod(data_t *d) {
    parse_strtod(d->short_text, d->count);
}

/// @brief Says whether digits times 10^exponent reads back, by strtof(),
/// as the float with the given bits.
static int reads_back(uint64_t digits, int exponent, uint32_t bits) {
    char text[48];
    snprintf(text, sizeof(text), "%llue%d", (unsigned long long)digits, exponent);
    float f = strtof(text, NULL);
    uint32_t b;
    memcpy(&b, &f, sizeof(b));
    return b == bits;
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

/// @brief Without -j, -x checks on 8 threads.
static int threads_missing(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    (void)option;
    settings->threads = 8;
    return 0;
}

/// @brief Reads the argument of -j.
static int threads_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *s = option->value;
    char *end;
    errno = 0;
    unsigned long long value = s[0] >= '0' && s[0] <= '9' ? strtoull(s, &end, 10) : 0;
    if (value < 1 || value > 256 || errno != 0 || *end != '\0') {
        fprintf(stderr, "bench: %s wants a number of threads from 1 to 256, not \"%s\"\n", option->option, s);
        return BAD_NUMBER;
    }
    settings->threads = (size_t)value;
    return 0;
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
    if (opt_check.present) {
        return check_all(settings.threads);
    }
    data_t d;
    data_init(&d, settings.count);
    sof_timer_t timer;
    sof_timer_init(&timer);
    sof_timer_calibrate(&timer);
    printf("ns per value, best of %d runs, %zu values\n\n", RUNS, settings.count);
    printf("%-32s %10s %10s %8s\n", "", "sof_real", "c", "c / sof");
    measure("format random, %.17g", format_random_sof, format_random_printf, &d, &timer);
    measure("format random, shortest %g", format_random_sof, format_random_g, &d, &timer);
    measure("format short, %.17g", format_short_sof, format_short_printf, &d, &timer);
    measure("format short, shortest %g", format_short_sof, format_short_g, &d, &timer);
    measure("format float, %.9g", format_float_sof, format_float_printf, &d, &timer);
    measure("parse random, strtod", parse_random_sof, parse_random_strtod, &d, &timer);
    measure("parse short, strtod", parse_short_sof, parse_short_strtod, &d, &timer);
    measure("parse 25 digits, strtod", parse_long_sof, parse_long_strtod, &d, &timer);
    measure("parse float, strtof", parse_float_sof, parse_float_strtof, &d, &timer);
    sof_timer_free(&timer);
    data_free(&d);
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: bench [-h] [-n count] [-x] [-j threads]\n");
    sof_copt_print(stream);
}
