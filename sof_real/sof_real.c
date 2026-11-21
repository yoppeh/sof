/**
 * @file sof_real.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Shortest round-trip formatting and exact parsing of floating-point
 * numbers in c.
 * @details Formatting is Schubfach, parsing Clinger's fast path, then
 * Eisel-Lemire, then an exact comparison in big integers for what that
 * leaves undecided; see sof_real.h. Both use the one table of powers of ten
 * in sof_real_internal.h, written by gen_table.py.
 *
 * A double or float is handled as its bits, in a uint64_t, with the
 * format_t of its type giving the sizes of its fields, so the parsing code
 * is shared by the two. The 64 by 64 bit multiplications use unsigned
 * __int128 where the compiler has it, and 32-bit halves otherwise or with
 * SOF_REAL_NO_BUILTINS defined, which also makes sof_bit count leading
 * zeros without the compiler's builtins; the results are the same.
 * @version 0.0.1
 * @date 2026-11-20
 * @copyright Copyright (c) 2026
 */

#include <float.h>
#include <string.h>

#include "sof_real.h"
#include "sof_real_internal.h"

#if defined(SOF_REAL_NO_BUILTINS) && !defined(SOF_BIT_NO_BUILTINS)
#define SOF_BIT_NO_BUILTINS
#endif
// sof_bit's word is size_t unless this says otherwise, which on a 32-bit
// machine is too small for a double's bits.
#define SOF_BIT_TYPE uint64_t
#include "sof_bit.h"

/// @brief The most significant digits the slow path keeps. A halfway point
/// between two doubles has at most 768 significant digits, so a number with
/// more is taken as its first 799 and, if any of the rest isn't 0, a 1
/// after them, which is on the same side of every halfway point.
#define SLOW_DIGITS 800

/// @brief The 32-bit limbs of a big_t. The slow path's numbers are below
/// 2^2800 (see compare_halfway()); this leaves room to spare.
#define BIG_LIMBS 128

/// @brief The largest power of ten Clinger's fast path multiplies or
/// divides by: 10^22 is the largest that is a double exactly.
#define CLINGER_POWER 22

/// @brief The same for floats: 10^10 is the largest that is a float.
#define CLINGER_POWER_FLOAT 10

/// @brief Where an exponent read from text saturates: far beyond any power
/// of ten whose number isn't 0 or an infinity, and far from overflowing an
/// int64_t when the digits are counted into it.
#define EXPONENT_LIMIT 1000000000000LL

/// @brief Where a count of digits saturates, for the same reason.
#define COUNT_LIMIT ((size_t)1 << 50)

/// @brief The sizes of a floating-point type's fields, and the powers of
/// ten beyond which every number of up to 19 digits is 0 or infinite.
typedef struct format_t {
    /// @brief The bits of the significand, the hidden bit included.
    int mantissa_bits;
    /// @brief The exponent of the smallest normal number: 2^exponent_min.
    int exponent_min;
    /// @brief What is added to an exponent to make its field.
    int bias;
    /// @brief The bits of positive infinity.
    uint64_t infinity;
    /// @brief The sign bit.
    uint64_t sign;
    /// @brief Any number below 10^19 times a power of ten below this is
    /// below half the smallest subnormal, and rounds to 0.
    int power_zero;
    /// @brief Any number of at least 1 times a power of ten above this is
    /// above the largest finite value, and rounds to infinity.
    int power_infinity;
} format_t;

/// @brief A double: 53 bits of significand, exponents from -1022.
static const format_t double_format = {53, -1022, 1023, 0x7ff0000000000000ULL, 0x8000000000000000ULL, -342, 308};

/// @brief A float: 24 bits of significand, exponents from -126.
static const format_t float_format = {24, -126, 127, 0x7f800000ULL, 0x80000000ULL, -64, 38};

/// @brief A non-negative integer of up to BIG_LIMBS 32-bit limbs, least
/// significant first.
typedef struct big_t {
    uint32_t limb[BIG_LIMBS];
    /// @brief The limbs in use; the highest isn't 0, and 0 has none.
    size_t count;
} big_t;

/// @brief A number's text taken apart: its digits, before and after the
/// point, and its exponent.
typedef struct number_t {
    const char *whole;
    size_t whole_count;
    const char *fraction;
    size_t fraction_count;
    /// @brief The exponent after the 'e', 0 without one, saturated at
    /// EXPONENT_LIMIT.
    int64_t exponent;
    int negative;
} number_t;

static void big_add_small(big_t *b, uint32_t add);
static int big_compare(const big_t *a, const big_t *b);
static void big_from_number(big_t *b, const number_t *n, int64_t *power);
static void big_mul_pow5(big_t *b, uint64_t k);
static void big_mul_small(big_t *b, uint32_t m);
static void big_set(big_t *b, uint64_t value);
static void big_shift_left(big_t *b, uint64_t bits);
static int clinger(uint64_t w, int64_t q, const format_t *f, uint64_t *bits);
static int compare_halfway(const big_t *d, int64_t power, uint64_t bits, const format_t *f);
static uint64_t convert(const number_t *n, uint64_t w, int64_t q, int truncated, const format_t *f);
static unsigned count_digits(uint64_t x);
static int64_t floor_log10_pow2(int64_t e);
static int64_t floor_log10_three_quarters_pow2(int64_t e);
static int64_t floor_log2_pow10(int64_t e);
static int64_t floor_shift(int64_t x, unsigned shift);
static uint64_t from_bits(uint64_t bits, int negative, const format_t *f, sof_real_error_t *rc);
static unsigned leading_zeros(uint64_t x);
static int lemire(uint64_t w, int64_t q, int inexact, const format_t *f, uint64_t *bits);
static uint64_t load_eight(const char *p);
static uint64_t multiply_high(uint64_t a, uint64_t b, uint64_t *low);
static sof_real_error_t parse(const char *text, size_t length, const format_t *f, uint64_t *bits, size_t *used);
static uint32_t parse_eight(uint64_t v);
static uint64_t rop(uint64_t g1, uint64_t g0, uint64_t cp);
static uint32_t rop_float(uint64_t g, uint64_t cp);
static const char *scan_digits(const char *p, const char *end, uint64_t *w, unsigned *significant, size_t *taken, int *truncated);
static void schubfach(uint64_t bits, uint64_t *digits, int *exponent);
static void schubfach_float(uint32_t bits, uint64_t *digits, int *exponent);
static uint64_t slow(const number_t *n, uint64_t w, int64_t q, uint64_t bits, const format_t *f);
static void strip_zeros(uint64_t *digits, int *exponent);
static size_t write_decimal(int negative, uint64_t digits, int exponent, char *buffer, size_t size);
static void write_digits(uint64_t x, unsigned n, char *d);
static size_t write_text(const char *text, size_t length, char *buffer, size_t size);

/// @brief The decimal digits of 0 to 99, two by two.
static const char digit_pairs[201] = "00010203040506070809"
                                     "10111213141516171819"
                                     "20212223242526272829"
                                     "30313233343536373839"
                                     "40414243444546474849"
                                     "50515253545556575859"
                                     "60616263646566676869"
                                     "70717273747576777879"
                                     "80818283848586878889"
                                     "90919293949596979899";

/// @brief Adds a small number to b.
static void big_add_small(big_t *b, uint32_t add) {
    uint64_t carry = add;
    for (size_t i = 0; carry != 0 && i < b->count; i++) {
        carry += b->limb[i];
        b->limb[i] = (uint32_t)carry;
        carry >>= 32;
    }
    if (carry != 0 && b->count < BIG_LIMBS) {
        b->limb[b->count++] = (uint32_t)carry;
    }
}

/// @brief Returns -1, 0 or 1 as a is below, equal to or above b.
static int big_compare(const big_t *a, const big_t *b) {
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

/// @brief Sets b to a number's significant digits, the first SLOW_DIGITS -
/// 1 of them and a 1 if any after those isn't 0, and power to the power of
/// ten that b is multiplied by to give the number's magnitude.
static void big_from_number(big_t *b, const number_t *n, int64_t *power) {
    size_t total = n->whole_count + n->fraction_count, kept = 0, i = 0;
    uint32_t chunk = 0;
    unsigned chunk_digits = 0;
    int sticky = 0;
    b->count = 0;
    // Leading zeros, before and after the point, count for nothing.
    for (; i < total; i++) {
        char c = i < n->whole_count ? n->whole[i] : n->fraction[i - n->whole_count];
        if (c != '0') {
            break;
        }
    }
    size_t significant = total - i;
    for (; i < total && kept < SLOW_DIGITS - 1; i++, kept++) {
        char c = i < n->whole_count ? n->whole[i] : n->fraction[i - n->whole_count];
        chunk = chunk * 10 + (uint32_t)(c - '0');
        if (++chunk_digits == 9) {
            big_mul_small(b, 1000000000);
            big_add_small(b, chunk);
            chunk = 0;
            chunk_digits = 0;
        }
    }
    for (; i < total; i++) {
        char c = i < n->whole_count ? n->whole[i] : n->fraction[i - n->whole_count];
        if (c != '0') {
            sticky = 1;
            chunk = chunk * 10 + 1;
            chunk_digits++;
            break;
        }
    }
    if (chunk_digits > 0) {
        static const uint32_t powers[10] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};
        big_mul_small(b, powers[chunk_digits]);
        big_add_small(b, chunk);
    }
    // The number is the digits times ten to the exponent less the digits
    // after the point; each digit left out adds one, and the 1 that stands
    // for them takes one away.
    size_t fraction = n->fraction_count < COUNT_LIMIT ? n->fraction_count : COUNT_LIMIT;
    size_t left = significant - kept < COUNT_LIMIT ? significant - kept : COUNT_LIMIT;
    *power = n->exponent - (int64_t)fraction + (int64_t)left - sticky;
}

/// @brief Multiplies b by 5^k, 5^13 at a time, the largest power that fits
/// in 32 bits.
static void big_mul_pow5(big_t *b, uint64_t k) {
    static const uint32_t powers[14] = {1, 5, 25, 125, 625, 3125, 15625, 78125, 390625, 1953125, 9765625, 48828125, 244140625, 1220703125};
    while (k >= 13) {
        big_mul_small(b, powers[13]);
        k -= 13;
    }
    if (k > 0) {
        big_mul_small(b, powers[k]);
    }
}

/// @brief Multiplies b by a small number.
static void big_mul_small(big_t *b, uint32_t m) {
    uint64_t carry = 0;
    for (size_t i = 0; i < b->count; i++) {
        carry += (uint64_t)b->limb[i] * m;
        b->limb[i] = (uint32_t)carry;
        carry >>= 32;
    }
    if (carry != 0 && b->count < BIG_LIMBS) {
        b->limb[b->count++] = (uint32_t)carry;
    }
}

/// @brief Sets b to a 64-bit value.
static void big_set(big_t *b, uint64_t value) {
    b->count = 0;
    while (value != 0) {
        b->limb[b->count++] = (uint32_t)value;
        value >>= 32;
    }
}

/// @brief Multiplies b by 2^bits.
static void big_shift_left(big_t *b, uint64_t bits) {
    if (b->count == 0 || bits == 0) {
        return;
    }
    size_t limbs = (size_t)(bits / 32);
    unsigned rest = (unsigned)(bits % 32);
    if (limbs + b->count + 1 > BIG_LIMBS) {
        // Never happens for the numbers the slow path compares; saturate
        // rather than write past the end.
        limbs = BIG_LIMBS - b->count - 1;
    }
    if (rest != 0) {
        uint32_t top = b->limb[b->count - 1] >> (32 - rest);
        for (size_t i = b->count - 1; i > 0; i--) {
            b->limb[i] = (b->limb[i] << rest) | (b->limb[i - 1] >> (32 - rest));
        }
        b->limb[0] <<= rest;
        if (top != 0) {
            b->limb[b->count++] = top;
        }
    }
    if (limbs > 0) {
        memmove(b->limb + limbs, b->limb, b->count * sizeof(b->limb[0]));
        memset(b->limb, 0, limbs * sizeof(b->limb[0]));
        b->count += limbs;
    }
}

/// @brief Converts w times 10^q exactly by Clinger's fast path, if w and
/// 10^q are both exactly of the type, so that the value is one correctly
/// rounded multiplication or division.
/// @details It needs the type's arithmetic done in the type itself: with
/// x87 extended precision (FLT_EVAL_METHOD 2) the result would be rounded
/// twice, so there it is never taken.
/// @return 1, with the value's bits in bits, or 0 if it doesn't apply.
static int clinger(uint64_t w, int64_t q, const format_t *f, uint64_t *bits) {
#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD == 0
    if (f == &double_format) {
        static const double powers[CLINGER_POWER + 1] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                                         1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                                         1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
        if (w > (UINT64_C(1) << 53) || q < -CLINGER_POWER || q > CLINGER_POWER) {
            return 0;
        }
        double d = (double)w;
        d = q < 0 ? d / powers[-q] : d * powers[q];
        memcpy(bits, &d, sizeof(d));
        return 1;
    }
    static const float powers_float[CLINGER_POWER_FLOAT + 1] = {1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f,
                                                                1e6f, 1e7f, 1e8f, 1e9f, 1e10f};
    if (w > (UINT64_C(1) << 24) || q < -CLINGER_POWER_FLOAT || q > CLINGER_POWER_FLOAT) {
        return 0;
    }
    float x = (float)w;
    x = q < 0 ? x / powers_float[-q] : x * powers_float[q];
    uint32_t b;
    memcpy(&b, &x, sizeof(b));
    *bits = b;
    return 1;
#else
    (void)w;
    (void)q;
    (void)f;
    (void)bits;
    return 0;
#endif
}

/// @brief Compares d times 10^power with the point halfway between the
/// positive finite value whose bits are bits and the next one up.
/// @details The halfway point is (2m + 1) 2^(e - 1), for the value's
/// significand m and the exponent e of its last bit; this holds at the top
/// of a binade, and for the largest finite value, whose halfway point is
/// where rounding goes to infinity. Both sides are made integers: the
/// power of five goes to whichever side it is positive on, as does the
/// power of two. The two sides are within a few bits of each other, since
/// the halfway point is near the number, so each ends below about 2^2720:
/// d below 10^800 and 5^1143 times 2^55 on the other side, at most.
/// @return -1, 0 or 1 as the number is below, at or above the halfway
/// point.
static int compare_halfway(const big_t *d, int64_t power, uint64_t bits, const format_t *f) {
    uint64_t field = bits >> (f->mantissa_bits - 1);
    uint64_t m = bits & ((UINT64_C(1) << (f->mantissa_bits - 1)) - 1);
    int64_t e;
    if (field == 0) {
        e = f->exponent_min - (f->mantissa_bits - 1);
    } else {
        m |= UINT64_C(1) << (f->mantissa_bits - 1);
        e = (int64_t)field - f->bias - (f->mantissa_bits - 1);
    }
    big_t x = *d, y;
    big_set(&y, 2 * m + 1);
    if (power >= 0) {
        big_mul_pow5(&x, (uint64_t)power);
    } else {
        big_mul_pow5(&y, (uint64_t)-power);
    }
    int64_t x_two = power, y_two = e - 1;
    if (x_two > y_two) {
        big_shift_left(&x, (uint64_t)(x_two - y_two));
    } else {
        big_shift_left(&y, (uint64_t)(y_two - x_two));
    }
    return big_compare(&x, &y);
}

/// @brief Converts a number of at least one significant digit, given as
/// its first 19 significant digits w times 10^q, to the bits of the nearest
/// value of the type.
/// @details truncated says that digits that aren't all 0 were left out of
/// w, so the number is above w 10^q and below (w + 1) 10^q. n is the whole
/// number, for the slow path, or NULL when w is all of it.
/// @return The bits of the magnitude.
static uint64_t convert(const number_t *n, uint64_t w, int64_t q, int truncated, const format_t *f) {
    uint64_t bits, above;
    if (q < f->power_zero) {
        return 0;
    }
    if (q > f->power_infinity) {
        return f->infinity;
    }
    if (!truncated && clinger(w, q, f, &bits)) {
        return bits;
    }
    if (lemire(w, q, truncated, f, &bits)) {
        // Every number between two that round to the same value rounds to
        // it too.
        if (!truncated || (lemire(w + 1, q, 1, f, &above) && above == bits)) {
            return bits;
        }
    }
    return slow(n, w, q, bits, f);
}

/// @brief Returns the number of decimal digits of x, which isn't 0: about
/// log10(2) times its bits, 1233 / 4096, corrected by one comparison.
static unsigned count_digits(uint64_t x) {
    static const uint64_t powers[20] = {1ULL,
                                        10ULL,
                                        100ULL,
                                        1000ULL,
                                        10000ULL,
                                        100000ULL,
                                        1000000ULL,
                                        10000000ULL,
                                        100000000ULL,
                                        1000000000ULL,
                                        10000000000ULL,
                                        100000000000ULL,
                                        1000000000000ULL,
                                        10000000000000ULL,
                                        100000000000000ULL,
                                        1000000000000000ULL,
                                        10000000000000000ULL,
                                        100000000000000000ULL,
                                        1000000000000000000ULL,
                                        10000000000000000000ULL};
    unsigned t = ((64 - leading_zeros(x)) * 1233) >> 12;
    return t + (x >= powers[t]);
}

/// @brief Returns floor(e log10(2)), for e from -5000 to 5000 at least.
static int64_t floor_log10_pow2(int64_t e) {
    return floor_shift(e * 661971961083LL, 41);
}

/// @brief Returns floor(log10(3/4 2^e)), for e from -5000 to 5000 at least.
static int64_t floor_log10_three_quarters_pow2(int64_t e) {
    return floor_shift(e * 661971961083LL - 274743187321LL, 41);
}

/// @brief Returns floor(e log2(10)), for e from -1500 to 1500 at least.
static int64_t floor_log2_pow10(int64_t e) {
    return floor_shift(e * 913124641741LL, 38);
}

/// @brief Returns x / 2^shift rounded down, as an arithmetic shift does,
/// which c leaves to the compiler for a negative x: x is moved up by 2^58,
/// a multiple of 2^shift, so that it is positive, shifted, and moved down
/// again. |x| must be below 2^58, and shift at most 58.
static int64_t floor_shift(int64_t x, unsigned shift) {
    const uint64_t offset = UINT64_C(1) << 58;
    return (int64_t)(((uint64_t)x + offset) >> shift) - (int64_t)(offset >> shift);
}

/// @brief Returns the bits of a magnitude with its sign, and sets rc to
/// SOF_REAL_RANGE if it is an infinity, SOF_REAL_OK otherwise.
static uint64_t from_bits(uint64_t bits, int negative, const format_t *f, sof_real_error_t *rc) {
    *rc = bits == f->infinity ? SOF_REAL_RANGE : SOF_REAL_OK;
    return negative ? bits | f->sign : bits;
}

/// @brief Returns the number of 0 bits above the highest 1 bit of x, which
/// isn't 0.
static unsigned leading_zeros(uint64_t x) {
    return 63 - sof_bit_highest(x);
}

/// @brief Converts w times 10^q to the bits of the nearest value of the
/// type, if a 128-bit approximation of 10^q settles it: the Eisel-Lemire
/// algorithm.
/// @details w is normalized to w' in [2^63, 2^64) and multiplied by the
/// table's 128 bits of 10^q, T, in full: P = w' T, of 192 bits. The table's
/// T is exact for q from 0 to 55, and otherwise below the true value by
/// less than 1, so the true product is in [P, P + 2^64), and above P unless
/// T is exact and w is the whole number. Rounding is decided by where the
/// product falls between halfway points, which are 2^137 or more apart;
/// only if one may lie between P and P + 2^64 is it undecided, which
/// happens for numbers within about 2^-73 of a halfway point. With inexact
/// non-zero, the number is taken to be just above w 10^q.
/// @return 1 with the bits in bits, or 0 if it is undecided, with bits set
/// to a value within one place of the right one.
static int lemire(uint64_t w, int64_t q, int inexact, const format_t *f, uint64_t *bits) {
    unsigned shift = leading_zeros(w);
    uint64_t wn = w << shift;
    const uint64_t *t = sof_real_powers[q - SOF_REAL_POWER_MIN];
    uint64_t a0, b0;
    uint64_t a1 = multiply_high(wn, t[0], &a0);
    uint64_t b1 = multiply_high(wn, t[1], &b0);
    uint64_t p0 = b0, p1 = a0 + b1, p2 = a1 + (p1 < a0);
    int exact = !inexact && q >= 0 && q <= 55;
    // P = (p2 p1 p0) is in [2^190, 2^192); its lowest bit is worth
    // 2^low, and its highest 2^high.
    int top = (int)(p2 >> 63) + 190;
    int64_t low = floor_log2_pow10(q) - 127 - shift;
    int64_t high = top + low;
    // The bits below the result's last: enough to leave its significand,
    // or for a subnormal, its last bit worth the smallest subnormal.
    int64_t drop = high >= f->exponent_min ? top - f->mantissa_bits + 1 : f->exponent_min - f->mantissa_bits + 1 - low;
    if (drop > 192) {
        // Below half the smallest subnormal, which P + 2^64 never reaches.
        *bits = 0;
        return 1;
    }
    unsigned s = (unsigned)(drop - 128);
    uint64_t m = s == 64 ? 0 : p2 >> s;
    uint64_t half = (p2 >> (s - 1)) & 1;
    uint64_t mask = (UINT64_C(1) << (s - 1)) - 1;
    int decided = 1;
    uint64_t up;
    if (exact) {
        up = half && ((p2 & mask) != 0 || p1 != 0 || p0 != 0 || (m & 1) != 0);
    } else {
        // The true product is in (P, P + 2^64). Unless the bits from 2^64
        // to just below the halfway bit are all ones, and some below them
        // aren't 0, no halfway point or multiple of one lies in that range,
        // and the true product rounds as anything just above P does.
        decided = !((p2 & mask) == mask && p1 == UINT64_MAX && p0 != 0);
        up = half;
    }
    m += up;
    if (high >= f->exponent_min) {
        // A carry out of the significand carries into the exponent.
        m += (uint64_t)(high + f->bias - 1) << (f->mantissa_bits - 1);
    }
    *bits = m >= f->infinity ? f->infinity : m;
    return decided;
}

/// @brief Loads 8 bytes, the first in the lowest bits, whatever the
/// machine's byte order; compilers make this one load where they can.
static uint64_t load_eight(const char *p) {
    const unsigned char *u = (const unsigned char *)p;
    return (uint64_t)u[0] | (uint64_t)u[1] << 8 | (uint64_t)u[2] << 16 | (uint64_t)u[3] << 24 | (uint64_t)u[4] << 32 | (uint64_t)u[5] << 40 | (uint64_t)u[6] << 48 | (uint64_t)u[7] << 56;
}

/// @brief Returns the high 64 bits of the 128-bit product a * b, and stores
/// the low 64 in low.
/// @details Without unsigned __int128, or with SOF_REAL_NO_BUILTINS
/// defined, it multiplies 32-bit halves; the result is the same.
static uint64_t multiply_high(uint64_t a, uint64_t b, uint64_t *low) {
#if defined(__SIZEOF_INT128__) && !defined(SOF_REAL_NO_BUILTINS)
    unsigned __int128 m = (unsigned __int128)a * b;
    *low = (uint64_t)m;
    return (uint64_t)(m >> 64);
#else
    uint64_t a_low = a & 0xffffffffULL, a_high = a >> 32;
    uint64_t b_low = b & 0xffffffffULL, b_high = b >> 32;
    uint64_t ll = a_low * b_low, lh = a_low * b_high;
    uint64_t hl = a_high * b_low, hh = a_high * b_high;
    uint64_t middle = (ll >> 32) + (lh & 0xffffffffULL) + (hl & 0xffffffffULL);
    *low = (middle << 32) | (ll & 0xffffffffULL);
    return hh + (lh >> 32) + (hl >> 32) + (middle >> 32);
#endif
}

/// @brief Reads a number by the grammar in sof_real.h and converts it to
/// the bits of the nearest value of the type, its sign included.
static sof_real_error_t parse(const char *text, size_t length, const format_t *f, uint64_t *bits, size_t *used) {
    number_t n = {0};
    if (length == 0) {
        // text may be NULL.
        *bits = 0;
        if (used != NULL) {
            *used = 0;
        }
        return SOF_REAL_NO_MATCH;
    }
    const char *p = text, *end = text + length;
    n.negative = *p == '-';
    p += *p == '-' || *p == '+';
    // The first 19 significant digits, which always fit in 64 bits, go in
    // w as they are read, with the digits of the whole part that don't
    // fit, and those of the fraction that do, counted for the exponent.
    uint64_t w = 0;
    unsigned significant = 0;
    size_t whole_taken = 0, fraction_taken = 0;
    int truncated = 0;
    n.whole = p;
    p = scan_digits(p, end, &w, &significant, &whole_taken, &truncated);
    n.whole_count = (size_t)(p - n.whole);
    if (p < end && *p == '.') {
        const char *after = scan_digits(p + 1, end, &w, &significant, &fraction_taken, &truncated);
        n.fraction = p + 1;
        n.fraction_count = (size_t)(after - n.fraction);
        // A point with digits on neither side isn't part of a number.
        if (n.whole_count > 0 || n.fraction_count > 0) {
            p = after;
        }
    }
    if (n.whole_count == 0 && n.fraction_count == 0) {
        *bits = 0;
        if (used != NULL) {
            *used = 0;
        }
        return SOF_REAL_NO_MATCH;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        const char *e = p + 1;
        int negative = 0;
        if (e < end && (*e == '+' || *e == '-')) {
            negative = *e == '-';
            e++;
        }
        if (e < end && *e >= '0' && *e <= '9') {
            int64_t x = 0;
            for (; e < end && *e >= '0' && *e <= '9'; e++) {
                if (x < EXPONENT_LIMIT) {
                    x = x * 10 + (*e - '0');
                }
            }
            n.exponent = negative ? -x : x;
            p = e;
        }
    }
    if (used != NULL) {
        *used = (size_t)(p - text);
    }
    if (w == 0) {
        *bits = n.negative ? f->sign : 0;
        return SOF_REAL_OK;
    }
    // The number is w times ten to the exponent, plus the digits of the
    // whole part left out of w, less those of the fraction in it.
    size_t left = n.whole_count - whole_taken;
    left = left < COUNT_LIMIT ? left : COUNT_LIMIT;
    fraction_taken = fraction_taken < COUNT_LIMIT ? fraction_taken : COUNT_LIMIT;
    int64_t q = n.exponent + (int64_t)left - (int64_t)fraction_taken;
    sof_real_error_t rc;
    *bits = from_bits(convert(&n, w, q, truncated, f), n.negative, f, &rc);
    return rc;
}

/// @brief Returns the value of the 8 digits in v, as load_eight() loads
/// them, the first the most significant, in three multiplications.
static uint32_t parse_eight(uint64_t v) {
    v -= 0x3030303030303030ULL;
    v = v * 10 + (v >> 8);
    v = (((v & 0x000000ff000000ffULL) * 0x000f424000000064ULL) + (((v >> 16) & 0x000000ff000000ffULL) * 0x0000271000000001ULL)) >> 32;
    return (uint32_t)v;
}

/// @brief Computes Schubfach's rop(cp g 2^-127), where g = g1 2^63 + g0:
/// the product, rounded to odd.
static uint64_t rop(uint64_t g1, uint64_t g0, uint64_t cp) {
    uint64_t x0, y0;
    uint64_t x1 = multiply_high(g0, cp, &x0);
    uint64_t y1 = multiply_high(g1, cp, &y0);
    uint64_t z = (y0 >> 1) + x1;
    uint64_t vbp = y1 + (z >> 63);
    uint64_t mask = (UINT64_C(1) << 63) - 1;
    return vbp | (((z & mask) + mask) >> 63);
}

/// @brief Computes Schubfach's rop(cp g 2^-95) for floats, with g of 63
/// bits: the product, rounded to odd.
static uint32_t rop_float(uint64_t g, uint64_t cp) {
    uint64_t x0;
    uint64_t x1 = multiply_high(g, cp, &x0);
    uint64_t vbp = x1 >> 31;
    uint64_t mask = (UINT64_C(1) << 32) - 1;
    return (uint32_t)(vbp | (((x1 & mask) + mask) >> 32));
}

/// @brief Reads a run of digits from p, adding them to w while it has fewer
/// than 19 significant digits.
/// @details significant counts the digits in w from its first that isn't
/// 0, taken counts the digits added to w, zeros before the first included,
/// and truncated is set if a digit left out isn't 0. Once w isn't 0, and
/// while 8 bytes are left, the digits are taken up to 8 at a time: those
/// at the start of the 8 bytes are found at once and converted together,
/// padded with zeros in front.
/// @return Where the digits end.
static const char *scan_digits(const char *p, const char *end, uint64_t *w, unsigned *significant, size_t *taken, int *truncated) {
    static const uint64_t powers[9] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};
    uint64_t x = *w;
    unsigned s = *significant;
    size_t t = 0;
    for (;;) {
        if (x != 0 && end - p >= 8) {
            uint64_t v = load_eight(p);
            // A byte is a digit if it is below 10 after '0' is taken away
            // with an exclusive or; the top bit of each byte that isn't is
            // set, without carries between bytes.
            uint64_t z = v ^ 0x3030303030303030ULL;
            uint64_t over = (((z & 0x7f7f7f7f7f7f7f7fULL) + 0x7676767676767676ULL) | z) & 0x8080808080808080ULL;
            unsigned k = over == 0 ? 8 : sof_bit_lowest(over) / 8;
            if (k > 0 && s + k <= 19) {
                uint64_t padded = k == 8 ? v : (v << (8 * (8 - k))) | (0x3030303030303030ULL >> (8 * k));
                x = x * powers[k] + parse_eight(padded);
                s += k;
                t += k;
                p += k;
                if (k < 8) {
                    break;
                }
                continue;
            }
            if (k == 8 && s >= 19) {
                *truncated |= v != 0x3030303030303030ULL;
                p += 8;
                continue;
            }
        }
        if (p == end || *p < '0' || *p > '9') {
            break;
        }
        if (s < 19) {
            x = x * 10 + (uint64_t)(*p - '0');
            s += x != 0;
            t++;
        } else if (*p != '0') {
            *truncated = 1;
        }
        p++;
    }
    *w = x;
    *significant = s;
    *taken = t;
    return p;
}

/// @brief Finds the shortest decimal of a positive finite double, by
/// Giulietti's Schubfach.
/// @details As his Java DoubleToDecimal: c 2^q is the value, and the
/// decimals considered are those in its rounding interval, from halfway to
/// the next value down to halfway to the next up, with the ends included
/// when c is even. The interval is four times the value's width on the
/// scale of cb = 4c, and it is scaled by 10^-k, chosen so that the interval
/// holds at least one integer and at most one multiple of ten, by g, the
/// table's 10^-k rounded up to 126 bits. If a multiple of ten lies in the
/// interval it is the shortest, unless both do (s' and t' 10); otherwise
/// the closest of s and s + 1 that does.
static void schubfach(uint64_t bits, uint64_t *digits, int *exponent) {
    const uint64_t c_min = UINT64_C(1) << 52;
    const int q_min = -1074;
    uint64_t t = bits & (c_min - 1);
    int bq = (int)((bits >> 52) & 0x7ff);
    uint64_t c;
    int q;
    if (bq != 0) {
        int mq = 1075 - bq;
        c = c_min | t;
        // A small integer is its own shortest decimal.
        if (mq > 0 && mq < 53 && ((c >> mq) << mq) == c) {
            *digits = c >> mq;
            *exponent = 0;
            strip_zeros(digits, exponent);
            return;
        }
        q = -mq;
    } else {
        q = q_min;
        c = t;
    }
    uint64_t out = c & 1, cb = c << 2, cbr = cb + 2, cbl;
    int64_t k;
    if (c != c_min || q == q_min) {
        cbl = cb - 2;
        k = floor_log10_pow2(q);
    } else {
        // The next value down is half as far as the next up.
        cbl = cb - 1;
        k = floor_log10_three_quarters_pow2(q);
    }
    int h = (int)(q + floor_log2_pow10(-k) + 2);
    // g = floor(10^-k 2^-r) + 1 for the r that puts it in [2^125, 2^126),
    // which is the table's T / 4 + 1, split into 63-bit halves.
    const uint64_t *p = sof_real_powers[-k - SOF_REAL_POWER_MIN];
    uint64_t g_high = p[0] >> 2, g_low = ((p[1] >> 2) | (p[0] << 62)) + 1;
    g_high += g_low == 0;
    uint64_t g1 = (g_high << 1) | (g_low >> 63), g0 = g_low & ((UINT64_C(1) << 63) - 1);
    uint64_t vb = rop(g1, g0, cb << h), vbl = rop(g1, g0, cbl << h), vbr = rop(g1, g0, cbr << h);
    uint64_t s = vb >> 2;
    if (s >= 10) {
        // floor(s / 10), by a multiplication.
        uint64_t unused;
        uint64_t sp10 = 10 * multiply_high(s, UINT64_C(1844674407370955168), &unused);
        uint64_t tp10 = sp10 + 10;
        int upin = vbl + out <= sp10 << 2, wpin = (tp10 << 2) + out <= vbr;
        if (upin != wpin) {
            *digits = upin ? sp10 : tp10;
            *exponent = (int)k;
            strip_zeros(digits, exponent);
            return;
        }
    }
    uint64_t s1 = s + 1;
    int uin = vbl + out <= s << 2, win = (s1 << 2) + out <= vbr;
    if (uin != win) {
        *digits = uin ? s : s1;
    } else {
        // Both are in the interval: the closer, or the even one of two as
        // close.
        int64_t cmp = (int64_t)vb - (int64_t)((s + s1) << 1);
        *digits = cmp < 0 || (cmp == 0 && (s & 1) == 0) ? s : s1;
    }
    *exponent = (int)k;
    strip_zeros(digits, exponent);
}

/// @brief Finds the shortest decimal of a positive finite float, by
/// Schubfach, as schubfach() does for a double, with 32-bit arithmetic and
/// the top 63 bits of the double's g, rounded up.
static void schubfach_float(uint32_t bits, uint64_t *digits, int *exponent) {
    const uint32_t c_min = UINT32_C(1) << 23;
    const int q_min = -149;
    uint32_t t = bits & (c_min - 1);
    int bq = (int)((bits >> 23) & 0xff);
    uint32_t c;
    int q;
    if (bq != 0) {
        int mq = 150 - bq;
        c = c_min | t;
        if (mq > 0 && mq < 24 && ((c >> mq) << mq) == c) {
            *digits = c >> mq;
            *exponent = 0;
            strip_zeros(digits, exponent);
            return;
        }
        q = -mq;
    } else {
        q = q_min;
        c = t;
    }
    uint32_t out = c & 1;
    uint64_t cb = (uint64_t)c << 2, cbr = cb + 2, cbl;
    int64_t k;
    if (c != c_min || q == q_min) {
        cbl = cb - 2;
        k = floor_log10_pow2(q);
    } else {
        cbl = cb - 1;
        k = floor_log10_three_quarters_pow2(q);
    }
    int h = (int)(q + floor_log2_pow10(-k) + 33);
    // The double's g1, the top 63 bits of its 126-bit g, plus 1.
    const uint64_t *p = sof_real_powers[-k - SOF_REAL_POWER_MIN];
    uint64_t g_high = p[0] >> 2, g_low = ((p[1] >> 2) | (p[0] << 62)) + 1;
    g_high += g_low == 0;
    uint64_t g = ((g_high << 1) | (g_low >> 63)) + 1;
    uint32_t vb = rop_float(g, cb << h), vbl = rop_float(g, cbl << h), vbr = rop_float(g, cbr << h);
    uint32_t s = vb >> 2;
    if (s >= 10) {
        uint32_t sp10 = 10 * (uint32_t)(((uint64_t)s * 1717986919) >> 34);
        uint32_t tp10 = sp10 + 10;
        int upin = vbl + out <= sp10 << 2, wpin = (tp10 << 2) + out <= vbr;
        if (upin != wpin) {
            *digits = upin ? sp10 : tp10;
            *exponent = (int)k;
            strip_zeros(digits, exponent);
            return;
        }
    }
    uint32_t s1 = s + 1;
    int uin = vbl + out <= s << 2, win = (s1 << 2) + out <= vbr;
    if (uin != win) {
        *digits = uin ? s : s1;
    } else {
        int32_t cmp = (int32_t)vb - (int32_t)((s + s1) << 1);
        *digits = cmp < 0 || (cmp == 0 && (s & 1) == 0) ? s : s1;
    }
    *exponent = (int)k;
    strip_zeros(digits, exponent);
}

/// @brief Finds the nearest value to a number that Eisel-Lemire left
/// undecided, by comparing the number, in big integers, with the halfway
/// points either side of a candidate, moving the candidate until it is
/// between them.
/// @details The number is n's digits, or w times 10^q if n is NULL, and
/// bits is the candidate, within a place or so of the answer. A number
/// exactly halfway rounds to the candidate whose last bit is 0.
/// @return The bits of the magnitude.
static uint64_t slow(const number_t *n, uint64_t w, int64_t q, uint64_t bits, const format_t *f) {
    big_t d;
    int64_t power = q;
    if (n != NULL) {
        big_from_number(&d, n, &power);
    } else {
        big_set(&d, w);
    }
    for (;;) {
        if (bits != f->infinity) {
            int c = compare_halfway(&d, power, bits, f);
            if (c > 0 || (c == 0 && (bits & 1) != 0)) {
                bits++;
                continue;
            }
        }
        if (bits != 0) {
            int c = compare_halfway(&d, power, bits - 1, f);
            if (c < 0 || (c == 0 && (bits & 1) != 0)) {
                bits--;
                continue;
            }
        }
        return bits;
    }
}

/// @brief Removes the trailing zeros of digits, which isn't 0, adding one
/// to exponent for each.
static void strip_zeros(uint64_t *digits, int *exponent) {
    uint64_t d = *digits;
    int e = *exponent;
    while (d % 100000000 == 0) {
        d /= 100000000;
        e += 8;
    }
    if (d % 10000 == 0) {
        d /= 10000;
        e += 4;
    }
    if (d % 100 == 0) {
        d /= 100;
        e += 2;
    }
    if (d % 10 == 0) {
        d /= 10;
        e += 1;
    }
    *digits = d;
    *exponent = e;
}

/// @brief Writes digits times 10^exponent, with a '-' if negative, in
/// sof_real_format()'s notation.
/// @details digits has no trailing zeros, or is 0. The digits are written
/// once, where most of them belong, straight into buffer when it has room
/// for any number, or into a block of SOF_REAL_FORMAT_SIZE bytes first.
/// @return The length, or 0 if it doesn't fit in size bytes with a NUL.
static size_t write_decimal(int negative, uint64_t digits, int exponent, char *buffer, size_t size) {
    char block[SOF_REAL_FORMAT_SIZE + 8];
    char *text = buffer != NULL && size >= SOF_REAL_FORMAT_SIZE ? buffer : block;
    size_t to = 0;
    if (negative) {
        text[to++] = '-';
    }
    if (digits == 0) {
        text[to++] = '0';
    } else {
        unsigned n = count_digits(digits);
        // The exponent of the first digit, as in d.ddd e+X.
        int first = exponent + (int)n - 1;
        if (first >= 17 || first < -4) {
            // d.ddde+XX: the digits after a gap, then the first moved
            // into it and the point after it.
            write_digits(digits, n, text + to + 1);
            text[to] = text[to + 1];
            if (n > 1) {
                text[to + 1] = '.';
                to += n + 1;
            } else {
                to++;
            }
            text[to++] = 'e';
            text[to++] = first < 0 ? '-' : '+';
            unsigned e = (unsigned)(first < 0 ? -first : first);
            if (e >= 100) {
                text[to++] = (char)('0' + e / 100);
                e %= 100;
            }
            memcpy(text + to, digit_pairs + 2 * e, 2);
            to += 2;
        } else if (first < 0) {
            // 0.000ddd
            text[to++] = '0';
            text[to++] = '.';
            for (int z = first; z < -1; z++) {
                text[to++] = '0';
            }
            write_digits(digits, n, text + to);
            to += n;
        } else if ((unsigned)first + 1 >= n) {
            // An integer: the digits, then zeros to the units.
            write_digits(digits, n, text + to);
            to += n;
            for (unsigned z = n; z <= (unsigned)first; z++) {
                text[to++] = '0';
            }
        } else {
            // ddd.ddd: the digits after a gap, then those before the point
            // moved into it.
            write_digits(digits, n, text + to + 1);
            for (int i = 0; i <= first; i++) {
                text[to + (size_t)i] = text[to + (size_t)i + 1];
            }
            text[to + (size_t)first + 1] = '.';
            to += n + 1;
        }
    }
    if (text == buffer) {
        buffer[to] = '\0';
        return to;
    }
    return write_text(text, to, buffer, size);
}

/// @brief Writes the n decimal digits of x, which has n digits, to d: 8 at
/// a time in 32-bit halves, two at a time within them.
static void write_digits(uint64_t x, unsigned n, char *d) {
    char *p = d + n;
    while (x >= 100000000) {
        uint32_t low = (uint32_t)(x % 100000000);
        x /= 100000000;
        uint32_t high4 = low / 10000, low4 = low % 10000;
        p -= 8;
        memcpy(p, digit_pairs + 2 * (high4 / 100), 2);
        memcpy(p + 2, digit_pairs + 2 * (high4 % 100), 2);
        memcpy(p + 4, digit_pairs + 2 * (low4 / 100), 2);
        memcpy(p + 6, digit_pairs + 2 * (low4 % 100), 2);
    }
    uint32_t y = (uint32_t)x;
    while (y >= 100) {
        p -= 2;
        memcpy(p, digit_pairs + 2 * (y % 100), 2);
        y /= 100;
    }
    if (y >= 10) {
        memcpy(p - 2, digit_pairs + 2 * y, 2);
    } else {
        p[-1] = (char)('0' + y);
    }
}

/// @brief Copies length bytes of text and a NUL to buffer, if they fit in
/// size bytes.
/// @return length, or 0 if they don't fit, when buffer holds an empty
/// string if size isn't 0.
static size_t write_text(const char *text, size_t length, char *buffer, size_t size) {
    if (buffer == NULL) {
        return 0;
    }
    if (length >= size) {
        if (size > 0) {
            buffer[0] = '\0';
        }
        return 0;
    }
    memcpy(buffer, text, length);
    buffer[length] = '\0';
    return length;
}

sof_real_error_t sof_real_decimal(double value, uint64_t *digits, int *exponent) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    bits &= ~double_format.sign;
    if (digits == NULL || exponent == NULL || bits >= double_format.infinity) {
        return SOF_REAL_ERROR;
    }
    if (bits == 0) {
        *digits = 0;
        *exponent = 0;
        return SOF_REAL_OK;
    }
    schubfach(bits, digits, exponent);
    return SOF_REAL_OK;
}

sof_real_error_t sof_real_decimal_float(float value, uint64_t *digits, int *exponent) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    bits &= ~(uint32_t)float_format.sign;
    if (digits == NULL || exponent == NULL || bits >= float_format.infinity) {
        return SOF_REAL_ERROR;
    }
    if (bits == 0) {
        *digits = 0;
        *exponent = 0;
        return SOF_REAL_OK;
    }
    schubfach_float(bits, digits, exponent);
    return SOF_REAL_OK;
}

size_t sof_real_format(double value, char *buffer, size_t size) {
    uint64_t bits, digits = 0;
    int exponent = 0;
    memcpy(&bits, &value, sizeof(bits));
    int negative = (bits & double_format.sign) != 0;
    bits &= ~double_format.sign;
    if (bits > double_format.infinity) {
        return write_text("nan", 3, buffer, size);
    }
    if (bits == double_format.infinity) {
        return negative ? write_text("-inf", 4, buffer, size) : write_text("inf", 3, buffer, size);
    }
    if (bits != 0) {
        schubfach(bits, &digits, &exponent);
    }
    return write_decimal(negative, digits, exponent, buffer, size);
}

size_t sof_real_format_float(float value, char *buffer, size_t size) {
    uint32_t bits;
    uint64_t digits = 0;
    int exponent = 0;
    memcpy(&bits, &value, sizeof(bits));
    int negative = (bits & float_format.sign) != 0;
    bits &= ~(uint32_t)float_format.sign;
    if (bits > float_format.infinity) {
        return write_text("nan", 3, buffer, size);
    }
    if (bits == float_format.infinity) {
        return negative ? write_text("-inf", 4, buffer, size) : write_text("inf", 3, buffer, size);
    }
    if (bits != 0) {
        schubfach_float(bits, &digits, &exponent);
    }
    return write_decimal(negative, digits, exponent, buffer, size);
}

sof_real_error_t sof_real_from_decimal(uint64_t digits, int exponent, int negative, double *value) {
    sof_real_error_t rc = SOF_REAL_OK;
    if (value == NULL) {
        return SOF_REAL_ERROR;
    }
    uint64_t bits = digits == 0 ? 0 : convert(NULL, digits, exponent, 0, &double_format);
    bits = from_bits(bits, negative, &double_format, &rc);
    memcpy(value, &bits, sizeof(bits));
    return rc;
}

sof_real_error_t sof_real_from_decimal_float(uint64_t digits, int exponent, int negative, float *value) {
    sof_real_error_t rc = SOF_REAL_OK;
    if (value == NULL) {
        return SOF_REAL_ERROR;
    }
    uint64_t bits = digits == 0 ? 0 : convert(NULL, digits, exponent, 0, &float_format);
    uint32_t b = (uint32_t)from_bits(bits, negative, &float_format, &rc);
    memcpy(value, &b, sizeof(b));
    return rc;
}

sof_real_error_t sof_real_parse(const char *text, size_t length, double *value, size_t *used) {
    uint64_t bits;
    if (value == NULL || (text == NULL && length != 0)) {
        return SOF_REAL_ERROR;
    }
    sof_real_error_t rc = parse(text, length, &double_format, &bits, used);
    memcpy(value, &bits, sizeof(bits));
    return rc;
}

sof_real_error_t sof_real_parse_float(const char *text, size_t length, float *value, size_t *used) {
    uint64_t bits;
    if (value == NULL || (text == NULL && length != 0)) {
        return SOF_REAL_ERROR;
    }
    sof_real_error_t rc = parse(text, length, &float_format, &bits, used);
    uint32_t b = (uint32_t)bits;
    memcpy(value, &b, sizeof(b));
    return rc;
}
