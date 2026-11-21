/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_real.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 *
 * Checks every entry of the table of powers of ten against big integers of
 * the test's own; formatting of chosen values, then of random doubles and
 * floats, each of which must read back, have no shorter decimal that reads
 * back, and be the closest of its length; parsing against strtod() and
 * strtof() in the "C" locale, on chosen and random text, long numbers and
 * numbers exactly at, just above and just below the halfway points between
 * doubles; the grammar, ranges, NULL arguments, and the functions on
 * threads. Random data comes from next_random() with fixed seeds.
 * @version 0.0.1
 * @date 2026-11-20
 * @copyright Copyright (c) 2026
 */

#include <float.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sof_real.h"
#include "sof_real_internal.h"
#include "test.h"

/// @brief The limbs of a ref_big_t: enough for 10^800 times 2^1100.
#define REF_LIMBS 200

/// @brief The threaded test: threads, and the numbers each converts.
#define THREADS 4
#define THREAD_COUNT 20000

/// @brief A big integer of the test's own, to check the library's with,
/// 32-bit limbs least significant first.
typedef struct ref_big_t {
    uint32_t limb[REF_LIMBS];
    size_t count;
} ref_big_t;

/// @brief What each thread of the threaded test computes.
typedef struct thread_t {
    uint64_t seed;
    uint64_t sum;
} thread_t;

static uint64_t bits_of(double x);
static uint32_t bits_of_float(float x);
static int check_format(double v);
static int check_format_float(float v);
static void *convert_many(void *arg);
static double double_of(uint64_t bits);
static float float_of(uint32_t bits);
static size_t halfway_text(uint64_t bits, char *text, size_t size);
static int parses_as_strtod(const char *text);
static int parses_as_strtof(const char *text);
static int ref_bit(const ref_big_t *b, size_t i);
static size_t ref_bits(const ref_big_t *b);
static int ref_compare(const ref_big_t *a, const ref_big_t *b);
static size_t ref_decimal(ref_big_t *b, char *text, size_t size);
static void ref_mul(ref_big_t *b, uint32_t m);
static void ref_mul_big(ref_big_t *out, const ref_big_t *a, const ref_big_t *b);
static void ref_set(ref_big_t *b, uint64_t value);
static void ref_set_pow2(ref_big_t *b, size_t power);
static void ref_add(ref_big_t *a, const ref_big_t *b);
static void ref_shift(ref_big_t *b, size_t bits);

/// @brief Returns the bits of a double.
static uint64_t bits_of(double x) {
    uint64_t b;
    memcpy(&b, &x, sizeof(b));
    return b;
}

/// @brief Returns the bits of a float.
static uint32_t bits_of_float(float x) {
    uint32_t b;
    memcpy(&b, &x, sizeof(b));
    return b;
}

/// @brief Formats v and checks that the text reads back as v, by
/// sof_real_parse() and strtod(); that no decimal with one digit fewer
/// does; and that if another of the same length does, printf()'s correctly
/// rounded one is the one chosen or doesn't read back.
/// @return 1 if all is well, 0 if not.
static int check_format(double v) {
    char text[SOF_REAL_FORMAT_SIZE], other[64];
    double back;
    size_t used, length = sof_real_format(v, text, sizeof(text));
    if (length == 0 || length != strlen(text)) {
        return 0;
    }
    if (sof_real_parse(text, length, &back, &used) != SOF_REAL_OK || used != length || bits_of(back) != bits_of(v) || bits_of(strtod(text, NULL)) != bits_of(v)) {
        return 0;
    }
    uint64_t digits, d2;
    int exponent, e2;
    if (sof_real_decimal(v, &digits, &exponent) != SOF_REAL_OK) {
        return 0;
    }
    double magnitude = v < 0 ? -v : v;
    if (digits == 0) {
        return magnitude == 0;
    }
    if (digits % 10 == 0) {
        return 0;
    }
    // The text is the decimal.
    if (sof_real_from_decimal(digits, exponent, v < 0, &back) != SOF_REAL_OK || bits_of(back) != bits_of(v)) {
        return 0;
    }
    // Nothing shorter reads back.
    if (digits >= 10) {
        for (uint64_t d = digits / 10; d <= digits / 10 + 1; d++) {
            if (sof_real_from_decimal(d, exponent + 1, 0, &back) == SOF_REAL_OK && back == magnitude) {
                return 0;
            }
        }
    }
    // The closest of its length that reads back: printf()'s is either it
    // or doesn't read back.
    int n = 0;
    for (uint64_t d = digits; d != 0; d /= 10) {
        n++;
    }
    snprintf(other, sizeof(other), "%.*e", n - 1, magnitude);
    d2 = 0;
    const char *c = other;
    for (; *c != 'e'; c++) {
        if (*c >= '0' && *c <= '9') {
            d2 = d2 * 10 + (uint64_t)(*c - '0');
        }
    }
    e2 = atoi(c + 1) - (n - 1);
    if (sof_real_from_decimal(d2, e2, 0, &back) == SOF_REAL_OK && back == magnitude) {
        while (d2 % 10 == 0) {
            d2 /= 10;
            e2++;
        }
        return d2 == digits && e2 == exponent;
    }
    return 1;
}

/// @brief As check_format(), for a float.
static int check_format_float(float v) {
    char text[SOF_REAL_FORMAT_SIZE], other[64];
    float back;
    size_t used, length = sof_real_format_float(v, text, sizeof(text));
    if (length == 0 || length != strlen(text)) {
        return 0;
    }
    if (sof_real_parse_float(text, length, &back, &used) != SOF_REAL_OK || used != length || bits_of_float(back) != bits_of_float(v) || bits_of_float(strtof(text, NULL)) != bits_of_float(v)) {
        return 0;
    }
    uint64_t digits, d2;
    int exponent, e2;
    if (sof_real_decimal_float(v, &digits, &exponent) != SOF_REAL_OK || digits > 999999999) {
        return 0;
    }
    float magnitude = v < 0 ? -v : v;
    if (digits == 0) {
        return magnitude == 0;
    }
    if (digits % 10 == 0) {
        return 0;
    }
    if (digits >= 10) {
        for (uint64_t d = digits / 10; d <= digits / 10 + 1; d++) {
            if (sof_real_from_decimal_float(d, exponent + 1, 0, &back) == SOF_REAL_OK && back == magnitude) {
                return 0;
            }
        }
    }
    int n = 0;
    for (uint64_t d = digits; d != 0; d /= 10) {
        n++;
    }
    snprintf(other, sizeof(other), "%.*e", n - 1, (double)magnitude);
    d2 = 0;
    const char *c = other;
    for (; *c != 'e'; c++) {
        if (*c >= '0' && *c <= '9') {
            d2 = d2 * 10 + (uint64_t)(*c - '0');
        }
    }
    e2 = atoi(c + 1) - (n - 1);
    if (sof_real_from_decimal_float(d2, e2, 0, &back) == SOF_REAL_OK && back == magnitude) {
        while (d2 % 10 == 0) {
            d2 /= 10;
            e2++;
        }
        return d2 == digits && e2 == exponent;
    }
    return 1;
}

/// @brief A thread of the threaded test: formats and parses random doubles,
/// adding up the bits it reads back.
static void *convert_many(void *arg) {
    thread_t *t = arg;
    char text[SOF_REAL_FORMAT_SIZE];
    for (int i = 0; i < THREAD_COUNT; i++) {
        double v = double_of(next_random(&t->seed) & 0x7fefffffffffffffULL), back;
        size_t length = sof_real_format(v, text, sizeof(text));
        sof_real_parse(text, length, &back, NULL);
        t->sum += bits_of(back) + length;
    }
    return NULL;
}

/// @brief Returns the double with the given bits.
static double double_of(uint64_t bits) {
    double x;
    memcpy(&x, &bits, sizeof(x));
    return x;
}

/// @brief Returns the float with the given bits.
static float float_of(uint32_t bits) {
    float x;
    memcpy(&x, &bits, sizeof(x));
    return x;
}

/// @brief Writes the exact decimal of the point halfway between the
/// positive finite double with the given bits and the next one up, as
/// digits, an 'e' and a power of ten.
/// @return Its length.
static size_t halfway_text(uint64_t bits, char *text, size_t size) {
    uint64_t field = bits >> 52, m = bits & ((UINT64_C(1) << 52) - 1);
    int e = -1074;
    if (field != 0) {
        m |= UINT64_C(1) << 52;
        e = (int)field - 1075;
    }
    // (2m + 1) 2^(e - 1): times 5^(1 - e) and 10^(e - 1) when e < 1.
    ref_big_t b;
    ref_set(&b, 2 * m + 1);
    int power = 0;
    if (e - 1 >= 0) {
        ref_shift(&b, (size_t)(e - 1));
    } else {
        for (int i = 0; i < 1 - e; i++) {
            ref_mul(&b, 5);
        }
        power = e - 1;
    }
    size_t length = ref_decimal(&b, text, size);
    return length + (size_t)snprintf(text + length, size - length, "e%d", power);
}

/// @brief Says whether sof_real_parse() reads text as strtod() does: the
/// same bits, the same length, and SOF_REAL_RANGE just when it overflows.
static int parses_as_strtod(const char *text) {
    double a;
    size_t used;
    char *end;
    sof_real_error_t rc = sof_real_parse(text, strlen(text), &a, &used);
    double b = strtod(text, &end);
    int infinite = b > DBL_MAX || b < -DBL_MAX;
    return bits_of(a) == bits_of(b) && used == (size_t)(end - text) && (rc == SOF_REAL_RANGE) == infinite;
}

/// @brief Says whether sof_real_parse_float() reads text as strtof() does.
static int parses_as_strtof(const char *text) {
    float a;
    size_t used;
    char *end;
    sof_real_error_t rc = sof_real_parse_float(text, strlen(text), &a, &used);
    float b = strtof(text, &end);
    int infinite = b > FLT_MAX || b < -FLT_MAX;
    return bits_of_float(a) == bits_of_float(b) && used == (size_t)(end - text) && (rc == SOF_REAL_RANGE) == infinite;
}

/// @brief Adds b to a.
static void ref_add(ref_big_t *a, const ref_big_t *b) {
    uint64_t carry = 0;
    size_t n = a->count > b->count ? a->count : b->count;
    for (size_t i = 0; i < n; i++) {
        carry += (uint64_t)(i < a->count ? a->limb[i] : 0) + (i < b->count ? b->limb[i] : 0);
        a->limb[i] = (uint32_t)carry;
        carry >>= 32;
    }
    a->count = n;
    if (carry != 0) {
        a->limb[a->count++] = (uint32_t)carry;
    }
}

/// @brief Returns bit i of b.
static int ref_bit(const ref_big_t *b, size_t i) {
    return i / 32 < b->count ? (int)((b->limb[i / 32] >> (i % 32)) & 1) : 0;
}

/// @brief Returns the number of bits b takes.
static size_t ref_bits(const ref_big_t *b) {
    if (b->count == 0) {
        return 0;
    }
    size_t n = b->count * 32;
    while (!ref_bit(b, n - 1)) {
        n--;
    }
    return n;
}

/// @brief Returns -1, 0 or 1 as a is below, equal to or above b.
static int ref_compare(const ref_big_t *a, const ref_big_t *b) {
    if (a->count != b->count) {
        return a->count < b->count ? -1 : 1;
    }
    for (size_t i = a->count; i > 0; i--) {
        if (a->limb[i - 1] != b->limb[i - 1]) {
            return a->limb[i - 1] < b->limb[i - 1] ? -1 : 1;
        }
    }
    return 0;
}

/// @brief Writes b in decimal, emptying it.
/// @return The number of digits.
static size_t ref_decimal(ref_big_t *b, char *text, size_t size) {
    char reversed[8000];
    size_t n = 0;
    do {
        uint64_t rest = 0;
        for (size_t i = b->count; i > 0; i--) {
            rest = (rest << 32) | b->limb[i - 1];
            b->limb[i - 1] = (uint32_t)(rest / 10);
            rest %= 10;
        }
        while (b->count > 0 && b->limb[b->count - 1] == 0) {
            b->count--;
        }
        reversed[n++] = (char)('0' + rest);
    } while (b->count > 0 && n < sizeof(reversed));
    for (size_t i = 0; i < n && i + 1 < size; i++) {
        text[i] = reversed[n - 1 - i];
    }
    text[n < size ? n : size - 1] = '\0';
    return n < size ? n : size - 1;
}

/// @brief Multiplies b by a small number.
static void ref_mul(ref_big_t *b, uint32_t m) {
    uint64_t carry = 0;
    for (size_t i = 0; i < b->count; i++) {
        carry += (uint64_t)b->limb[i] * m;
        b->limb[i] = (uint32_t)carry;
        carry >>= 32;
    }
    if (carry != 0) {
        b->limb[b->count++] = (uint32_t)carry;
    }
}

/// @brief Sets out to a times b, by long multiplication.
static void ref_mul_big(ref_big_t *out, const ref_big_t *a, const ref_big_t *b) {
    memset(out->limb, 0, sizeof(out->limb));
    for (size_t i = 0; i < a->count; i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < b->count; j++) {
            carry += (uint64_t)a->limb[i] * b->limb[j] + out->limb[i + j];
            out->limb[i + j] = (uint32_t)carry;
            carry >>= 32;
        }
        out->limb[i + b->count] = (uint32_t)carry;
    }
    out->count = a->count + b->count;
    while (out->count > 0 && out->limb[out->count - 1] == 0) {
        out->count--;
    }
}

/// @brief Sets b to a 64-bit value.
static void ref_set(ref_big_t *b, uint64_t value) {
    b->count = 0;
    while (value != 0) {
        b->limb[b->count++] = (uint32_t)value;
        value >>= 32;
    }
}

/// @brief Sets b to 2^power.
static void ref_set_pow2(ref_big_t *b, size_t power) {
    memset(b->limb, 0, sizeof(b->limb));
    b->limb[power / 32] = UINT32_C(1) << (power % 32);
    b->count = power / 32 + 1;
}

/// @brief Multiplies b by 2^bits, a bit at a time.
static void ref_shift(ref_big_t *b, size_t bits) {
    for (size_t i = 0; i < bits; i++) {
        ref_mul(b, 2);
    }
}

/// @brief Every entry of the table is the 128 leading bits of its power of
/// ten, rounded down: for q >= 0 those of 10^q itself, and for q < 0 the T
/// with T 10^-q <= 2^s < (T + 1) 10^-q, s putting T in [2^127, 2^128).
static void test_table(void) {
    for (int q = SOF_REAL_POWER_MIN; q <= SOF_REAL_POWER_MAX; q++) {
        const uint64_t *t = sof_real_powers[q - SOF_REAL_POWER_MIN];
        CHECK((t[0] >> 63) == 1);
        ref_big_t p;
        ref_set(&p, 1);
        for (int i = 0; i < (q < 0 ? -q : q); i++) {
            ref_mul(&p, 10);
        }
        size_t length = ref_bits(&p);
        if (q >= 0) {
            uint64_t high = 0, low = 0;
            for (size_t i = 0; i < 128; i++) {
                int bit = length >= i + 1 ? ref_bit(&p, length - 1 - i) : 0;
                if (i < 64) {
                    high = (high << 1) | (uint64_t)bit;
                } else {
                    low = (low << 1) | (uint64_t)bit;
                }
            }
            CHECK(t[0] == high && t[1] == low);
        } else {
            ref_big_t entry, product, power;
            ref_set(&entry, t[1]);
            entry.limb[2] = (uint32_t)t[0];
            entry.limb[3] = (uint32_t)(t[0] >> 32);
            entry.count = 4;
            ref_mul_big(&product, &entry, &p);
            ref_set_pow2(&power, 127 + length);
            CHECK(ref_compare(&product, &power) <= 0);
            ref_add(&product, &p);
            CHECK(ref_compare(&product, &power) > 0);
        }
    }
}

/// @brief Chosen doubles are written as expected.
static void test_format(void) {
    static const struct {
        double value;
        const char *text;
    } cases[] = {
        {0.0, "0"},
        {1.0, "1"},
        {-1.0, "-1"},
        {0.1, "0.1"},
        {0.3, "0.3"},
        {2.0 / 3.0, "0.6666666666666666"},
        {1.5, "1.5"},
        {100.0, "100"},
        {123.456, "123.456"},
        {-0.5, "-0.5"},
        {0.0001, "0.0001"},
        {0.00012345, "0.00012345"},
        {0.00001, "1e-05"},
        {1.5e-7, "1.5e-07"},
        {1e16, "10000000000000000"},
        {12345678901234567e0, "12345678901234568"},
        {1e17, "1e+17"},
        {1.5e17, "1.5e+17"},
        {1e21, "1e+21"},
        {1e22, "1e+22"},
        {1e23, "1e+23"},
        {9007199254740993.0, "9007199254740992"},
        {5.684341886080802e-14, "5.684341886080802e-14"},
        {1e100, "1e+100"},
        {1.7976931348623157e308, "1.7976931348623157e+308"},
        {-1.7976931348623157e308, "-1.7976931348623157e+308"},
        {2.2250738585072014e-308, "2.2250738585072014e-308"},
        {2.225073858507201e-308, "2.225073858507201e-308"},
        {4.9406564584124654e-324, "5e-324"},
        {9.8813129168249309e-324, "1e-323"},
        {-4.9406564584124654e-324, "-5e-324"},
        {1.2345678901234568e-300, "1.2345678901234568e-300"},
        {-0.00012345678901234567, "-0.00012345678901234567"},
    };
    char text[SOF_REAL_FORMAT_SIZE];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t length = sof_real_format(cases[i].value, text, sizeof(text));
        CHECK(length == strlen(cases[i].text));
        CHECK(strcmp(text, cases[i].text) == 0);
    }
    CHECK(sof_real_format(-0.0, text, sizeof(text)) == 2 && strcmp(text, "-0") == 0);
    CHECK(sof_real_format(double_of(0x7ff0000000000000ULL), text, sizeof(text)) == 3 && strcmp(text, "inf") == 0);
    CHECK(sof_real_format(double_of(0xfff0000000000000ULL), text, sizeof(text)) == 4 && strcmp(text, "-inf") == 0);
    CHECK(sof_real_format(double_of(0x7ff8000000000000ULL), text, sizeof(text)) == 3 && strcmp(text, "nan") == 0);
    CHECK(sof_real_format(double_of(0xfff8000000000001ULL), text, sizeof(text)) == 3 && strcmp(text, "nan") == 0);
    // The longest texts fit SOF_REAL_FORMAT_SIZE exactly.
    CHECK(sof_real_format(-1.2345678901234567e-308, text, sizeof(text)) == SOF_REAL_FORMAT_SIZE - 1);
}

/// @brief Chosen floats are written as expected.
static void test_format_float(void) {
    static const struct {
        float value;
        const char *text;
    } cases[] = {
        {0.0f, "0"},
        {1.0f, "1"},
        {0.1f, "0.1"},
        {-0.3f, "-0.3"},
        {1.5e-7f, "1.5e-07"},
        {16777216.0f, "16777216"},
        {1e10f, "10000000000"},
        {1e17f, "1e+17"},
        {3.4028235e38f, "3.4028235e+38"},
        {1.1754944e-38f, "1.1754944e-38"},
        {1.4e-45f, "1e-45"},
        {2.8e-45f, "3e-45"},
        {123456.79f, "123456.79"},
        {2.0f / 3.0f, "0.6666667"},
    };
    char text[SOF_REAL_FORMAT_SIZE];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t length = sof_real_format_float(cases[i].value, text, sizeof(text));
        CHECK(length == strlen(cases[i].text));
        CHECK(strcmp(text, cases[i].text) == 0);
    }
    CHECK(sof_real_format_float(-0.0f, text, sizeof(text)) == 2 && strcmp(text, "-0") == 0);
    CHECK(sof_real_format_float(float_of(0x7f800000), text, sizeof(text)) == 3 && strcmp(text, "inf") == 0);
    CHECK(sof_real_format_float(float_of(0xff800000), text, sizeof(text)) == 4 && strcmp(text, "-inf") == 0);
    CHECK(sof_real_format_float(float_of(0x7fc00000), text, sizeof(text)) == 3 && strcmp(text, "nan") == 0);
}

/// @brief A buffer too small gets an empty string and 0; one just large
/// enough gets the text.
static void test_format_size(void) {
    char text[SOF_REAL_FORMAT_SIZE];
    memset(text, 'x', sizeof(text));
    CHECK(sof_real_format(123.5, text, 5) == 0 && text[0] == '\0');
    CHECK(sof_real_format(123.5, text, 6) == 5 && strcmp(text, "123.5") == 0);
    text[0] = 'x';
    CHECK(sof_real_format(123.5, text, 0) == 0 && text[0] == 'x');
    CHECK(sof_real_format(123.5, NULL, 10) == 0);
    CHECK(sof_real_format(1.0, text, 1) == 0 && text[0] == '\0');
    CHECK(sof_real_format(1.0, text, 2) == 1 && strcmp(text, "1") == 0);
    CHECK(sof_real_format(double_of(0xfff0000000000000ULL), text, 4) == 0 && text[0] == '\0');
    CHECK(sof_real_format_float(0.25f, text, 4) == 0 && text[0] == '\0');
    CHECK(sof_real_format_float(0.25f, text, 5) == 4 && strcmp(text, "0.25") == 0);
    CHECK(sof_real_format_float(0.25f, NULL, 5) == 0);
}

/// @brief Every power of two, with its neighbours, and the numbers at the
/// ends of each range, format to text that reads back and is shortest.
static void test_format_edges(void) {
    for (uint64_t field = 0; field < 2047; field++) {
        for (int k = -2; k <= 2; k++) {
            uint64_t bits = (field << 52) + (uint64_t)k;
            if ((field == 0 && k < 0) || bits >= 0x7ff0000000000000ULL) {
                continue;
            }
            CHECK(check_format(double_of(bits)));
            CHECK(check_format(-double_of(bits)));
        }
    }
    // The smallest subnormals, and the largest values below the smallest
    // normal and below infinity.
    for (uint64_t i = 0; i < 2000; i++) {
        CHECK(check_format(double_of(i)));
        CHECK(check_format(double_of(0x0010000000000000ULL - 1 - i)));
        CHECK(check_format(double_of(0x7fefffffffffffffULL - i)));
    }
    // Powers of ten, as near as doubles get, and their neighbours.
    char text[32];
    for (int e = -325; e <= 309; e++) {
        snprintf(text, sizeof(text), "1e%d", e);
        double v = strtod(text, NULL);
        if (v > DBL_MAX) {
            continue;
        }
        CHECK(check_format(v));
        CHECK(check_format(double_of(bits_of(v) + 1)));
        if (v > 0) {
            CHECK(check_format(double_of(bits_of(v) - 1)));
        }
    }
    // Integers.
    for (uint64_t i = 0; i < 20000; i++) {
        CHECK(check_format((double)i));
        CHECK(check_format((double)(UINT64_C(9007199254740992) - i)));
    }
    for (uint32_t field = 0; field < 255; field++) {
        for (int k = -2; k <= 2; k++) {
            uint32_t bits = (field << 23) + (uint32_t)k;
            if ((field == 0 && k < 0) || bits >= 0x7f800000) {
                continue;
            }
            CHECK(check_format_float(float_of(bits)));
            CHECK(check_format_float(-float_of(bits)));
        }
    }
}

/// @brief Random doubles, of random bits and of short decimals, format to
/// text that reads back and is the shortest and closest.
static void test_format_random(void) {
    uint64_t seed = 0x5eed0001;
    for (int i = 0; i < 100000; i++) {
        uint64_t bits = next_random(&seed);
        if ((bits & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL) {
            continue;
        }
        CHECK(check_format(double_of(bits)));
    }
    char text[64];
    for (int i = 0; i < 50000; i++) {
        uint64_t r = next_random(&seed);
        int digits = 1 + (int)(r % 17);
        uint64_t d = next_random(&seed) % 100000000000000000ULL;
        snprintf(text, sizeof(text), "%.*e", digits - 1, (double)d * 1e-10);
        double v = strtod(text, NULL) * (r & 1 ? 1e-200 : 1);
        CHECK(check_format(v));
    }
}

/// @brief Random floats, and a sample of every bit pattern, format to text
/// that reads back and is the shortest and closest.
static void test_format_random_float(void) {
    uint64_t seed = 0x5eed0002;
    for (int i = 0; i < 100000; i++) {
        uint32_t bits = (uint32_t)next_random(&seed);
        if ((bits & 0x7f800000) == 0x7f800000) {
            continue;
        }
        CHECK(check_format_float(float_of(bits)));
    }
    for (uint64_t bits = 0; bits < 0x7f800000; bits += 40009) {
        CHECK(check_format_float(float_of((uint32_t)bits)));
    }
}

/// @brief sof_real_decimal() gives the shortest digits and their power of
/// ten, and refuses what has none.
static void test_decimal(void) {
    uint64_t digits;
    int exponent;
    CHECK(sof_real_decimal(0.1, &digits, &exponent) == SOF_REAL_OK && digits == 1 && exponent == -1);
    CHECK(sof_real_decimal(-1234.5, &digits, &exponent) == SOF_REAL_OK && digits == 12345 && exponent == -1);
    CHECK(sof_real_decimal(1e300, &digits, &exponent) == SOF_REAL_OK && digits == 1 && exponent == 300);
    CHECK(sof_real_decimal(1500, &digits, &exponent) == SOF_REAL_OK && digits == 15 && exponent == 2);
    CHECK(sof_real_decimal(-0.0, &digits, &exponent) == SOF_REAL_OK && digits == 0 && exponent == 0);
    CHECK(sof_real_decimal(DBL_MAX, &digits, &exponent) == SOF_REAL_OK && digits == 17976931348623157ULL && exponent == 292);
    CHECK(sof_real_decimal(double_of(1), &digits, &exponent) == SOF_REAL_OK && digits == 5 && exponent == -324);
    CHECK(sof_real_decimal_float(0.1f, &digits, &exponent) == SOF_REAL_OK && digits == 1 && exponent == -1);
    CHECK(sof_real_decimal_float(FLT_MAX, &digits, &exponent) == SOF_REAL_OK && digits == 34028235 && exponent == 31);
    CHECK(sof_real_decimal_float(float_of(1), &digits, &exponent) == SOF_REAL_OK && digits == 1 && exponent == -45);
    digits = 7;
    exponent = 7;
    CHECK(sof_real_decimal(double_of(0x7ff0000000000000ULL), &digits, &exponent) == SOF_REAL_ERROR);
    CHECK(sof_real_decimal(double_of(0x7ff8000000000000ULL), &digits, &exponent) == SOF_REAL_ERROR);
    CHECK(sof_real_decimal_float(float_of(0xff800000), &digits, &exponent) == SOF_REAL_ERROR);
    CHECK(sof_real_decimal_float(float_of(0x7fc00000), &digits, &exponent) == SOF_REAL_ERROR);
    CHECK(digits == 7 && exponent == 7);
    CHECK(sof_real_decimal(1.0, NULL, &exponent) == SOF_REAL_ERROR);
    CHECK(sof_real_decimal(1.0, &digits, NULL) == SOF_REAL_ERROR);
    CHECK(sof_real_decimal_float(1.0f, NULL, &exponent) == SOF_REAL_ERROR);
    CHECK(sof_real_decimal_float(1.0f, &digits, NULL) == SOF_REAL_ERROR);
}

/// @brief sof_real_from_decimal() rounds digits times a power of ten
/// correctly, and is the inverse of sof_real_decimal().
static void test_from_decimal(void) {
    double v;
    float f;
    CHECK(sof_real_from_decimal(1, -1, 0, &v) == SOF_REAL_OK && v == 0.1);
    CHECK(sof_real_from_decimal(15, 2, 1, &v) == SOF_REAL_OK && v == -1500);
    CHECK(sof_real_from_decimal(0, 400, 1, &v) == SOF_REAL_OK && bits_of(v) == 0x8000000000000000ULL);
    CHECK(sof_real_from_decimal(1, 309, 0, &v) == SOF_REAL_RANGE && v > DBL_MAX);
    CHECK(sof_real_from_decimal(1, INT_MAX, 1, &v) == SOF_REAL_RANGE && v < -DBL_MAX);
    CHECK(sof_real_from_decimal(UINT64_MAX, INT_MIN, 0, &v) == SOF_REAL_OK && bits_of(v) == 0);
    CHECK(sof_real_from_decimal(UINT64_MAX, 0, 0, &v) == SOF_REAL_OK && v == 18446744073709551616.0);
    CHECK(sof_real_from_decimal(17976931348623157ULL, 292, 0, &v) == SOF_REAL_OK && v == DBL_MAX);
    CHECK(sof_real_from_decimal(17976931348623159ULL, 292, 0, &v) == SOF_REAL_RANGE);
    CHECK(sof_real_from_decimal(5, -324, 0, &v) == SOF_REAL_OK && bits_of(v) == 1);
    CHECK(sof_real_from_decimal(2, -324, 0, &v) == SOF_REAL_OK && bits_of(v) == 0);
    CHECK(sof_real_from_decimal(3, -324, 0, &v) == SOF_REAL_OK && bits_of(v) == 1);
    CHECK(sof_real_from_decimal(1, -1, 0, NULL) == SOF_REAL_ERROR);
    CHECK(sof_real_from_decimal_float(1, -1, 0, &f) == SOF_REAL_OK && f == 0.1f);
    CHECK(sof_real_from_decimal_float(34028235, 31, 1, &f) == SOF_REAL_OK && f == -FLT_MAX);
    CHECK(sof_real_from_decimal_float(1, 39, 0, &f) == SOF_REAL_RANGE && f > FLT_MAX);
    CHECK(sof_real_from_decimal_float(1, -46, 0, &f) == SOF_REAL_OK && f == 0);
    CHECK(sof_real_from_decimal_float(1, -1, 0, NULL) == SOF_REAL_ERROR);
    // Just above the halfway point between 1 and the next float, 1 + 2^-24,
    // and so rounding up; rounded to a double first, it would be 1 + 2^-24
    // exactly, and then 1.
    CHECK(sof_real_from_decimal_float(10000000596046448ULL, -16, 0, &f) == SOF_REAL_OK && bits_of_float(f) == 0x3f800001);
    uint64_t seed = 0x5eed0003, digits;
    int exponent;
    for (int i = 0; i < 20000; i++) {
        double x = double_of(next_random(&seed) & 0x7fefffffffffffffULL);
        CHECK(sof_real_decimal(x, &digits, &exponent) == SOF_REAL_OK);
        CHECK(sof_real_from_decimal(digits, exponent, 0, &v) == SOF_REAL_OK && bits_of(v) == bits_of(x));
        float y = float_of((uint32_t)next_random(&seed) & 0x7f7fffff);
        CHECK(sof_real_decimal_float(y, &digits, &exponent) == SOF_REAL_OK);
        CHECK(sof_real_from_decimal_float(digits, exponent, 1, &f) == SOF_REAL_OK && bits_of_float(-f) == bits_of_float(y));
    }
}

/// @brief The grammar: what is a number, and where reading stops.
static void test_parse_grammar(void) {
    static const struct {
        const char *text;
        sof_real_error_t rc;
        size_t used;
        double value;
    } cases[] = {
        {"", SOF_REAL_NO_MATCH, 0, 0},
        {"+", SOF_REAL_NO_MATCH, 0, 0},
        {"-", SOF_REAL_NO_MATCH, 0, 0},
        {".", SOF_REAL_NO_MATCH, 0, 0},
        {"-.", SOF_REAL_NO_MATCH, 0, 0},
        {".e5", SOF_REAL_NO_MATCH, 0, 0},
        {"e5", SOF_REAL_NO_MATCH, 0, 0},
        {" 1", SOF_REAL_NO_MATCH, 0, 0},
        {"inf", SOF_REAL_NO_MATCH, 0, 0},
        {"nan", SOF_REAL_NO_MATCH, 0, 0},
        {"1", SOF_REAL_OK, 1, 1},
        {"-1", SOF_REAL_OK, 2, -1},
        {"+1", SOF_REAL_OK, 2, 1},
        {".5", SOF_REAL_OK, 2, 0.5},
        {"-.5", SOF_REAL_OK, 3, -0.5},
        {"5.", SOF_REAL_OK, 2, 5},
        {"5.e1", SOF_REAL_OK, 4, 50},
        {"1.25", SOF_REAL_OK, 4, 1.25},
        {"1e3", SOF_REAL_OK, 3, 1000},
        {"1E+3", SOF_REAL_OK, 4, 1000},
        {"1e-3", SOF_REAL_OK, 4, 0.001},
        {"1e", SOF_REAL_OK, 1, 1},
        {"1e+", SOF_REAL_OK, 1, 1},
        {"1e-x", SOF_REAL_OK, 1, 1},
        {"2em", SOF_REAL_OK, 1, 2},
        {"1.5E-3x", SOF_REAL_OK, 6, 0.0015},
        {"0x10", SOF_REAL_OK, 1, 0},
        {"12,5", SOF_REAL_OK, 2, 12},
        {"1..2", SOF_REAL_OK, 2, 1},
        {"007", SOF_REAL_OK, 3, 7},
        {"-0", SOF_REAL_OK, 2, -0.0},
        {"0e99999999999999999999", SOF_REAL_OK, 22, 0},
        {"1e99999999999999999999", SOF_REAL_RANGE, 22, 1.0 / 0.0},
        {"-1e99999999999999999999", SOF_REAL_RANGE, 23, -1.0 / 0.0},
        {"1e-99999999999999999999", SOF_REAL_OK, 23, 0},
        {"1e309", SOF_REAL_RANGE, 5, 1.0 / 0.0},
        {"1e-400", SOF_REAL_OK, 6, 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        double v = 99;
        size_t used = 99;
        sof_real_error_t rc = sof_real_parse(cases[i].text, strlen(cases[i].text), &v, &used);
        CHECK(rc == cases[i].rc);
        CHECK(used == cases[i].used);
        CHECK(bits_of(v) == bits_of(cases[i].value));
        float f = 99;
        used = 99;
        rc = sof_real_parse_float(cases[i].text, strlen(cases[i].text), &f, &used);
        CHECK(rc == cases[i].rc);
        CHECK(used == cases[i].used);
        CHECK(bits_of_float(f) == bits_of_float((float)cases[i].value));
    }
    // Only length bytes are read, and the text needn't end in a NUL.
    double v;
    size_t used;
    CHECK(sof_real_parse("12345", 3, &v, &used) == SOF_REAL_OK && v == 123 && used == 3);
    CHECK(sof_real_parse("1e45", 2, &v, &used) == SOF_REAL_OK && v == 1 && used == 1);
    CHECK(sof_real_parse("1.5", 2, &v, &used) == SOF_REAL_OK && v == 1 && used == 2);
    CHECK(sof_real_parse("1.5", 0, &v, &used) == SOF_REAL_NO_MATCH && v == 0 && used == 0);
    CHECK(sof_real_parse("1.5", 3, &v, NULL) == SOF_REAL_OK && v == 1.5);
    CHECK(sof_real_parse("-1e-400", 7, &v, NULL) == SOF_REAL_OK && bits_of(v) == 0x8000000000000000ULL);
}

/// @brief Numbers around the ends of the range, and long ones, read as
/// strtod() reads them.
static void test_parse_edges(void) {
    static const char *cases[] = {
        "1.7976931348623157e308",
        "1.7976931348623158e308",
        "1.7976931348623159e308",
        "179769313486231580793728971405303415079934132710037826936173778980444968292764750946649017977587207096330286416692887910946555547851940402630657488671505820681908902000708383676273854845817711531764475730270069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497791",
        "179769313486231580793728971405303415079934132710037826936173778980444968292764750946649017977587207096330286416692887910946555547851940402630657488671505820681908902000708383676273854845817711531764475730270069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497792",
        "2.2250738585072011e-308",
        "2.2250738585072012e-308",
        "2.2250738585072014e-308",
        "4.9406564584124654e-324",
        "2.4703282292062327e-324",
        "2.4703282292062328e-324",
        "2.47032822920623272088e-324",
        "7.4109846876186981626e-324",
        "1e-324",
        "3e-324",
        "9007199254740993",
        "9007199254740993.0000000000000000000000000001",
        "9007199254740992.9999999999999999999999999999",
        "9007199254740995",
        "4503599627370496.5",
        "4503599627370497.5",
        "1e23",
        "8.98846567431158e307",
        "0.000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001e93",
        "100000000000000000000000000000000000000000000000000000000000000000000000000000000e-80",
        "1.00000000000000011102230246251565404236316680908203125",
        "1.00000000000000011102230246251565404236316680908203124",
        "1.00000000000000011102230246251565404236316680908203126",
        "0.1000000000000000055511151231257827021181583404541015625",
        "3.4028235677973366e38",
        "3.4028235677973362e38",
        "1.17549435e-38",
        "7.0064923216240854e-46",
        "7.0064923216240862e-46",
        "1.00000005960464477539062500",
        "1.00000005960464477539062501",
        "1.00000017881393432617187500",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK(parses_as_strtod(cases[i]));
        CHECK(parses_as_strtof(cases[i]));
    }
    // A number of 10,000 digits, near the halfway point between 1 and the
    // next double, either side, and many zeros before its digits.
    static char text[12000];
    strcpy(text, "1.00000000000000011102230246251565404236316680908203125");
    size_t n = strlen(text);
    memset(text + n, '0', 10000);
    text[n + 10000] = '\0';
    CHECK(parses_as_strtod(text));
    text[n + 9999] = '1';
    CHECK(parses_as_strtod(text));
    text[n - 1] = '4';
    memset(text + n, '9', 10000);
    CHECK(parses_as_strtod(text));
    text[0] = '0';
    text[1] = '.';
    memset(text + 2, '0', 9000);
    strcpy(text + 9002, "123456789e9000");
    CHECK(parses_as_strtod(text));
}

/// @brief Random text, of every kind the grammar allows, reads as strtod()
/// and strtof() read it.
static void test_parse_random(void) {
    static char text[1200];
    uint64_t seed = 0x5eed0004;
    for (int i = 0; i < 60000; i++) {
        uint64_t r = next_random(&seed);
        char *p = text;
        if (r & 1) {
            *p++ = '-';
        } else if (r & 2) {
            *p++ = '+';
        }
        int digits;
        switch ((r >> 2) & 7) {
        case 0:
            digits = 1 + (int)((r >> 8) % 4);
            break;
        case 1:
        case 2:
            digits = 1 + (int)((r >> 8) % 19);
            break;
        case 3:
            digits = 15 + (int)((r >> 8) % 6);
            break;
        case 4:
            digits = 19 + (int)((r >> 8) % 40);
            break;
        case 5:
            digits = 1 + (int)((r >> 8) % 1000);
            break;
        default:
            digits = 16 + (int)((r >> 8) % 3);
            break;
        }
        int point = (int)((r >> 20) % (uint64_t)(digits + 2)) - 1;
        int zeros = ((r >> 30) & 7) == 0 ? (int)((r >> 33) % 30) : 0;
        int runs = ((r >> 40) & 3) == 0;
        for (int k = 0; k < zeros; k++) {
            *p++ = '0';
        }
        for (int k = 0; k < digits; k++) {
            if (k == point) {
                *p++ = '.';
            }
            *p++ = runs && k > 3 ? ((r >> 42) & 1 ? '0' : '9') : (char)('0' + next_random(&seed) % 10);
        }
        if (((r >> 44) & 3) != 0) {
            uint64_t y = next_random(&seed);
            int e = (y & 1) ? (int)((y >> 8) % 40) - 20 : (int)((y >> 8) % 1400) - 1100;
            p += snprintf(p, 16, "%c%d", (y >> 4) & 1 ? 'E' : 'e', e);
        }
        *p = '\0';
        CHECK(parses_as_strtod(text));
        CHECK(parses_as_strtof(text));
    }
}

/// @brief Numbers exactly halfway between two doubles round to the one
/// whose last bit is 0, and those a little above or below to the nearer,
/// for random doubles and the ends of the ranges.
static void test_parse_halfway(void) {
    static char text[1200], other[1300];
    uint64_t seed = 0x5eed0005;
    for (int i = 0; i < 3000; i++) {
        uint64_t bits = next_random(&seed) & 0x7fefffffffffffffULL;
        if (i % 4 == 0) {
            bits &= 0x003fffffffffffffULL;
        } else if (i < 40) {
            bits = (uint64_t)i;
        }
        size_t length = halfway_text(bits, text, sizeof(text));
        double v;
        size_t used;
        CHECK(sof_real_parse(text, length, &v, &used) == SOF_REAL_OK && used == length);
        CHECK(bits_of(v) == ((bits & 1) ? bits + 1 : bits));
        CHECK(parses_as_strtod(text));
        // A little above: a 1 far after the last digit.
        char *e = strchr(text, 'e');
        size_t digits = (size_t)(e - text);
        memcpy(other, text, digits);
        memcpy(other + digits, "00000000000000000001", 20);
        snprintf(other + digits + 20, sizeof(other) - digits - 20, "e%d", atoi(e + 1) - 20);
        CHECK(sof_real_parse(other, strlen(other), &v, NULL) == SOF_REAL_OK && bits_of(v) == bits + 1);
        CHECK(parses_as_strtod(other));
        // A little below: one less in the last digit, and nines after it.
        memcpy(other, text, digits);
        for (size_t k = digits; k > 0; k--) {
            if (other[k - 1] != '0') {
                other[k - 1]--;
                break;
            }
            other[k - 1] = '9';
        }
        memset(other + digits, '9', 20);
        snprintf(other + digits + 20, sizeof(other) - digits - 20, "e%d", atoi(e + 1) - 20);
        CHECK(sof_real_parse(other, strlen(other), &v, NULL) == SOF_REAL_OK && bits_of(v) == bits);
        CHECK(parses_as_strtod(other));
    }
    // The halfway point above the largest double rounds to infinity.
    size_t length = halfway_text(0x7fefffffffffffffULL, text, sizeof(text));
    double v;
    CHECK(sof_real_parse(text, length, &v, NULL) == SOF_REAL_RANGE && v > DBL_MAX);
    CHECK(parses_as_strtod(text));
}

/// @brief Floats from random digits, and the halfway points between
/// floats, read as strtof() reads them.
static void test_parse_float(void) {
    char text[200];
    uint64_t seed = 0x5eed0006;
    for (int i = 0; i < 30000; i++) {
        uint32_t bits = (uint32_t)next_random(&seed) & 0x7f7fffff;
        if (bits == 0x7f7fffff) {
            continue;
        }
        // Every float's halfway point is a double, exactly.
        double half = ((double)float_of(bits) + (double)float_of(bits + 1)) / 2;
        snprintf(text, sizeof(text), "%.120e", half);
        float f;
        CHECK(sof_real_parse_float(text, strlen(text), &f, NULL) == SOF_REAL_OK);
        CHECK(bits_of_float(f) == ((bits & 1) ? bits + 1 : bits));
        CHECK(parses_as_strtof(text));
        snprintf(text, sizeof(text), "%.25e", double_of(bits_of(half) + 1));
        CHECK(parses_as_strtof(text));
        snprintf(text, sizeof(text), "%.25e", double_of(bits_of(half) - 1));
        CHECK(parses_as_strtof(text));
        snprintf(text, sizeof(text), "%.*e", (int)(next_random(&seed) % 12), (double)float_of(bits));
        CHECK(parses_as_strtof(text));
    }
}

/// @brief NULL arguments are refused, changing nothing.
static void test_null(void) {
    double v = 7;
    float f = 7;
    size_t used = 7;
    CHECK(sof_real_parse(NULL, 3, &v, &used) == SOF_REAL_ERROR && v == 7 && used == 7);
    CHECK(sof_real_parse("1", 1, NULL, &used) == SOF_REAL_ERROR && used == 7);
    CHECK(sof_real_parse(NULL, 0, &v, &used) == SOF_REAL_NO_MATCH && v == 0 && used == 0);
    CHECK(sof_real_parse_float(NULL, 3, &f, &used) == SOF_REAL_ERROR && f == 7);
    CHECK(sof_real_parse_float("1", 1, NULL, &used) == SOF_REAL_ERROR);
    CHECK(sof_real_parse_float(NULL, 0, &f, NULL) == SOF_REAL_NO_MATCH && f == 0);
    CHECK(sof_real_format(1.0, NULL, 0) == 0);
    CHECK(sof_real_format_float(1.0f, NULL, 0) == 0);
}

/// @brief Threads converting at once get what one thread gets.
static void test_threads(void) {
    thread_t threads[THREADS], serial;
    pthread_t ids[THREADS];
    for (int i = 0; i < THREADS; i++) {
        threads[i].seed = 0x5eed0100 + (uint64_t)i;
        threads[i].sum = 0;
        CHECK(pthread_create(&ids[i], NULL, convert_many, &threads[i]) == 0);
    }
    for (int i = 0; i < THREADS; i++) {
        CHECK(pthread_join(ids[i], NULL) == 0);
    }
    for (int i = 0; i < THREADS; i++) {
        serial.seed = 0x5eed0100 + (uint64_t)i;
        serial.sum = 0;
        convert_many(&serial);
        CHECK(serial.sum == threads[i].sum);
    }
}

int main(void) {
    printf("sof_real:\n");
    run("table of powers of ten", test_table);
    run("format", test_format);
    run("format float", test_format_float);
    run("format into a small buffer", test_format_size);
    run("format powers of two and range ends", test_format_edges);
    run("format random doubles", test_format_random);
    run("format random floats", test_format_random_float);
    run("decimal", test_decimal);
    run("from decimal", test_from_decimal);
    run("parse grammar", test_parse_grammar);
    run("parse range ends and long numbers", test_parse_edges);
    run("parse random text", test_parse_random);
    run("parse halfway points", test_parse_halfway);
    run("parse float", test_parse_float);
    run("NULL arguments", test_null);
    run("threads", test_threads);
    return test_report();
}
