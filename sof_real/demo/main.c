/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Shows what a decimal number becomes as a double or a float.
 * @details For each number on the command line, or each line of standard
 * input, prints the nearest double: the shortest decimal that reads back
 * as it, its exact value, whether that is the number given, its bits and
 * fields, the doubles either side and the gap to the next one. With -f the
 * same for floats; with -s just the shortest decimal, one a line, to
 * tidy a column of numbers.
 *
 *     fpinfo [-h] [-f] [-s] [number]...
 *
 * Numbers are read with sof_real_parse() and written with
 * sof_real_format(); the exact value is worked out in big integers of the
 * program's own. Lines are read into a sof_string, and options with
 * sof_copt. It exits with 0, 1 if a number couldn't be read, and 2 if the
 * options are wrong.
 * @version 0.0.1
 * @date 2026-11-20
 * @copyright Copyright (c) 2026
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sof_copt.h"
#include "sof_real.h"
#include "sof_string.h"

/// @brief The limbs of a decimal_t: 2^1024 and 5^1074 times 2^53 both
/// fit in 90 of 9 digits.
#define LIMBS 100

/// @brief A limb of a decimal_t holds this much: 9 decimal digits.
#define LIMB_BASE 1000000000u

/// @brief The size of the pieces lines are read in.
#define CHUNK 256

/// @brief A non-negative integer in base 10^9, least significant limb
/// first, for writing a double's exact value.
typedef struct decimal_t {
    uint32_t limb[LIMBS];
    size_t count;
} decimal_t;

/// @brief A floating-point type: its name, its sizes and how to convert to
/// and from it, so the program handles doubles and floats alike.
typedef struct type_t {
    const char *name;
    /// @brief The bits of the significand, without the hidden bit.
    int fraction_bits;
    /// @brief The bits of the exponent.
    int exponent_bits;
    /// @brief What is added to an exponent to make its field.
    int bias;
    /// @brief Hex digits to print the bits with.
    int hex_digits;
    /// @brief Reads text into the type's bits.
    sof_real_error_t (*parse)(const char *text, size_t length, uint64_t *bits, size_t *used);
    /// @brief Writes the value with the given bits.
    size_t (*format)(uint64_t bits, char *buffer, size_t size);
} type_t;

static void decimal_mul(decimal_t *d, uint32_t m);
static void decimal_set(decimal_t *d, uint64_t value);
static void exact_value(const type_t *t, uint64_t bits, sof_string_t *out);
static size_t format_double(uint64_t bits, char *buffer, size_t size);
static size_t format_float(uint64_t bits, char *buffer, size_t size);
static void normalize(const char *text, size_t length, sof_string_t *digits, long *exponent);
static sof_real_error_t parse_double(const char *text, size_t length, uint64_t *bits, size_t *used);
static sof_real_error_t parse_float(const char *text, size_t length, uint64_t *bits, size_t *used);
static int show(const type_t *t, const char *text, size_t length, sof_string_t *scratch[3]);
static void usage(FILE *stream);

static const type_t double_type = {"double", 52, 11, 1023, 16, parse_double, format_double};
static const type_t float_type = {"float", 23, 8, 127, 8, parse_float, format_float};

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_float = {
    .option = "-f",
    .description = "convert to floats rather than doubles",
};

static sof_copt_t opt_short = {
    .option = "-s",
    .description = "print only the shortest decimal of each number",
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_float,
    &opt_short,
};

/// @brief Multiplies d by m, which is below 2^32.
static void decimal_mul(decimal_t *d, uint32_t m) {
    uint64_t carry = 0;
    for (size_t i = 0; i < d->count; i++) {
        carry += (uint64_t)d->limb[i] * m;
        d->limb[i] = (uint32_t)(carry % LIMB_BASE);
        carry /= LIMB_BASE;
    }
    while (carry != 0 && d->count < LIMBS) {
        d->limb[d->count++] = (uint32_t)(carry % LIMB_BASE);
        carry /= LIMB_BASE;
    }
}

/// @brief Sets d to a 64-bit value.
static void decimal_set(decimal_t *d, uint64_t value) {
    d->count = 0;
    while (value != 0) {
        d->limb[d->count++] = (uint32_t)(value % LIMB_BASE);
        value /= LIMB_BASE;
    }
}

/// @brief Writes the exact decimal value of the positive finite number with
/// the given bits: m 2^e is m 5^-e 10^e when e is negative, so its digits
/// are those of the integer m 5^-e, with the point e places from the end.
static void exact_value(const type_t *t, uint64_t bits, sof_string_t *out) {
    uint64_t field = bits >> t->fraction_bits;
    uint64_t m = bits & ((UINT64_C(1) << t->fraction_bits) - 1);
    int e = 1 - t->bias - t->fraction_bits;
    if (field != 0) {
        m |= UINT64_C(1) << t->fraction_bits;
        e = (int)field - t->bias - t->fraction_bits;
    }
    sof_string_clear(out);
    if (m == 0) {
        sof_string_append_char(out, '0');
        return;
    }
    decimal_t d;
    decimal_set(&d, m);
    for (int i = 0; i < e; i++) {
        decimal_mul(&d, 2);
    }
    for (int i = 0; i < -e; i++) {
        decimal_mul(&d, 5);
    }
    // The integer's digits, most significant limb first.
    char digits[LIMBS * 9 + 1];
    size_t n = 0;
    if (d.count == 0) {
        digits[n++] = '0';
    } else {
        n = (size_t)snprintf(digits, sizeof(digits), "%" PRIu32, d.limb[d.count - 1]);
        for (size_t i = d.count - 1; i > 0; i--) {
            n += (size_t)snprintf(digits + n, sizeof(digits) - n, "%09" PRIu32, d.limb[i - 1]);
        }
    }
    if (e >= 0) {
        sof_string_append_bytes(out, digits, n);
        return;
    }
    size_t places = (size_t)-e;
    if (n > places) {
        sof_string_append_bytes(out, digits, n - places);
    } else {
        sof_string_append_char(out, '0');
    }
    sof_string_append_char(out, '.');
    for (size_t i = n; i < places; i++) {
        sof_string_append_char(out, '0');
    }
    size_t from = n > places ? n - places : 0;
    // Trailing zeros, when m is even, say nothing.
    while (n > from && digits[n - 1] == '0') {
        n--;
    }
    sof_string_append_bytes(out, digits + from, n - from);
}

static size_t format_double(uint64_t bits, char *buffer, size_t size) {
    double x;
    memcpy(&x, &bits, sizeof(x));
    return sof_real_format(x, buffer, size);
}

static size_t format_float(uint64_t bits, char *buffer, size_t size) {
    uint32_t b = (uint32_t)bits;
    float x;
    memcpy(&x, &b, sizeof(x));
    return sof_real_format_float(x, buffer, size);
}

/// @brief Takes the significant digits of a number's text, without the
/// zeros before and after them, and the power of ten of the last, so that
/// two texts of the same value give the same: "0.50" and "5e-1" both give
/// "5" and -1. The text is a number by sof_real_parse()'s grammar, without
/// a sign.
static void normalize(const char *text, size_t length, sof_string_t *digits, long *exponent) {
    long power = 0;
    int point = 0;
    size_t i = 0;
    sof_string_clear(digits);
    for (; i < length && text[i] != 'e' && text[i] != 'E'; i++) {
        if (text[i] == '.') {
            point = 1;
        } else {
            if (digits->length > 0 || text[i] != '0') {
                sof_string_append_char(digits, text[i]);
            }
            power -= point;
        }
    }
    if (i < length) {
        long e = 0;
        int negative = text[i + 1] == '-';
        for (i += 1 + (text[i + 1] == '-' || text[i + 1] == '+'); i < length; i++) {
            if (e < 100000000) {
                e = e * 10 + (text[i] - '0');
            }
        }
        power += negative ? -e : e;
    }
    while (digits->length > 0 && digits->data[digits->length - 1] == '0') {
        sof_string_delete(digits, digits->length - 1, 1);
        power++;
    }
    *exponent = digits->length > 0 ? power : 0;
}

static sof_real_error_t parse_double(const char *text, size_t length, uint64_t *bits, size_t *used) {
    double x;
    sof_real_error_t rc = sof_real_parse(text, length, &x, used);
    memcpy(bits, &x, sizeof(x));
    return rc;
}

static sof_real_error_t parse_float(const char *text, size_t length, uint64_t *bits, size_t *used) {
    float x;
    uint32_t b;
    sof_real_error_t rc = sof_real_parse_float(text, length, &x, used);
    memcpy(&b, &x, sizeof(b));
    *bits = b;
    return rc;
}

/// @brief Prints what the number in text becomes as type t, building text
/// in the three scratch strings.
/// @return 0, or 1 if the text isn't a number.
static int show(const type_t *t, const char *text, size_t length, sof_string_t *scratch[3]) {
    uint64_t bits;
    size_t used;
    char shortest[SOF_REAL_FORMAT_SIZE], other[SOF_REAL_FORMAT_SIZE];
    sof_real_error_t rc = t->parse(text, length, &bits, &used);
    if (rc == SOF_REAL_NO_MATCH || used != length) {
        fprintf(stderr, "fpinfo: \"%.*s\" isn't a number\n", (int)length, text);
        return 1;
    }
    t->format(bits, shortest, sizeof(shortest));
    if (opt_short.present) {
        printf("%s\n", shortest);
        return 0;
    }
    uint64_t sign = UINT64_C(1) << (t->fraction_bits + t->exponent_bits);
    uint64_t magnitude = bits & (sign - 1);
    uint64_t infinity = ((UINT64_C(1) << t->exponent_bits) - 1) << t->fraction_bits;
    uint64_t field = magnitude >> t->fraction_bits;
    uint64_t fraction = magnitude & ((UINT64_C(1) << t->fraction_bits) - 1);
    printf("%.*s\n", (int)length, text);
    printf("    %-10s %s%s\n", t->name, shortest, rc == SOF_REAL_RANGE ? " (too large)" : "");
    if (magnitude == infinity) {
        return 0;
    }
    printf("    %-10s 0x%0*" PRIx64 ": sign %d, exponent %d%s, fraction 0x%" PRIx64 "\n", "bits", t->hex_digits, bits, (bits & sign) != 0, field == 0 ? 1 - t->bias : (int)field - t->bias, field == 0 ? " (subnormal)" : "", fraction);
    // The exact value, and whether it is the number given.
    exact_value(t, magnitude, scratch[0]);
    long given_power, exact_power;
    size_t start = text[0] == '-' || text[0] == '+';
    normalize(text + start, length - start, scratch[1], &given_power);
    normalize(scratch[0]->data, scratch[0]->length, scratch[2], &exact_power);
    int same = given_power == exact_power && sof_string_cmp(scratch[1], scratch[2]) == 0;
    printf("    %-10s %s%s%s\n", "exact", (bits & sign) != 0 ? "-" : "", scratch[0]->data, same ? " (the number given)" : " (rounded)");
    // The neighbours: the next value down and up from the magnitude, with
    // the sign put back.
    if (magnitude == 0) {
        t->format(1, other, sizeof(other));
        printf("    %-10s -%s and %s\n", "neighbours", other, other);
    } else {
        t->format((magnitude - 1) | (bits & sign), other, sizeof(other));
        t->format((magnitude + 1) | (bits & sign), shortest, sizeof(shortest));
        printf("    %-10s %s and %s\n", "neighbours", other, shortest);
    }
    // The gap to the next value away from 0: 2^e of the last bit.
    int e = (field == 0 ? 1 : (int)field) - t->bias - t->fraction_bits;
    uint64_t gap = e >= 1 - t->bias ? (uint64_t)(e + t->bias) << t->fraction_bits : UINT64_C(1) << (e - (1 - t->bias - t->fraction_bits));
    if (magnitude + 1 == infinity) {
        printf("    %-10s none: the next is infinity\n", "gap");
    } else {
        t->format(gap, other, sizeof(other));
        printf("    %-10s %s\n", "gap", other);
    }
    return 0;
}

int main(int argc, char **argv) {
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(argc, argv, NULL, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "fpinfo: unknown option %s\n", argv[index]);
        usage(stderr);
        return 2;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "fpinfo: %s takes no argument\n", option->option);
        usage(stderr);
        return 2;
    default:
        fprintf(stderr, "fpinfo: can't read the options (error %d)\n", (int)err);
        return 2;
    }
    if (opt_help.present) {
        usage(stdout);
        return 0;
    }
    const type_t *t = opt_float.present ? &float_type : &double_type;
    sof_string_t strings[4];
    sof_string_t *scratch[3] = {&strings[0], &strings[1], &strings[2]}, *line = &strings[3];
    for (int i = 0; i < 4; i++) {
        if (sof_string_init(&strings[i], 0, NULL) != SOF_STRING_OK) {
            fprintf(stderr, "fpinfo: out of memory\n");
            return 1;
        }
    }
    int status = 0;
    if (index < argc) {
        for (int i = index; i < argc; i++) {
            status |= show(t, argv[i], strlen(argv[i]), scratch);
        }
    } else {
        // Each line, read a piece at a time, without the spaces around it;
        // blank lines are skipped.
        char piece[CHUNK];
        int more = 1;
        while (more) {
            sof_string_clear(line);
            more = 0;
            while (fgets(piece, sizeof(piece), stdin) != NULL) {
                sof_string_append_cstr(line, piece);
                if (line->length > 0 && line->data[line->length - 1] == '\n') {
                    more = 1;
                    break;
                }
            }
            size_t from = 0, to = line->length;
            while (from < to && (line->data[from] == ' ' || line->data[from] == '\t')) {
                from++;
            }
            while (to > from && (line->data[to - 1] == ' ' || line->data[to - 1] == '\t' || line->data[to - 1] == '\n' || line->data[to - 1] == '\r')) {
                to--;
            }
            if (to > from) {
                status |= show(t, line->data + from, to - from, scratch);
            }
        }
    }
    for (int i = 0; i < 4; i++) {
        sof_string_free(&strings[i]);
    }
    return status;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: fpinfo [-h] [-f] [-s] [number]...\n");
    sof_copt_print(stream);
}
