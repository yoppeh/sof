/**
 * @file sof_random.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Pseudo-random numbers in c.
 * @details xoshiro256** and splitmix64 are by David Blackman and Sebastiano
 * Vigna, who put their reference code in the public domain; see
 * https://prng.di.unimi.it/.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <string.h>

#include "sof_random.h"

/// @brief The size of the buffer sof_random_shuffle() swaps elements through.
#define SWAP_CHUNK 64

/// @brief ln 2 in 0.64 fixed point, rounded down.
#define LN2_Q64 0xb17217f7d1cf79abULL

/// @brief The exponential draw's table: entry k is the sum of (ln 2)^i / i!
/// for i from 1 to k, in 0.64 fixed point, rounded down, from k = 0 to
/// EXP_TABLE_LAST. The sums approach e^(ln 2) - 1 = 1, and the last is the
/// largest 0.64 value below 1.
#define EXP_TABLE_LAST 18
static const uint64_t exp_table[EXP_TABLE_LAST + 1] = {
    0,
    0xb17217f7d1cf79abULL,
    0xeef193f6d75a96fcULL,
    0xfd27186259aaf6c2ULL,
    0xff9d6dd050f4c5a7ULL,
    0xfff4cfcfef1e626bULL,
    0xfffee81886e1c62fULL,
    0xffffe7fe850e0bb5ULL,
    0xfffffe2a8731b17dULL,
    0xffffffdfac6eeadcULL,
    0xfffffffdf9640067ULL,
    0xffffffffe22ec79cULL,
    0xfffffffffe6a9dedULL,
    0xffffffffffebff81ULL,
    0xffffffffffff1417ULL,
    0xfffffffffffff5ceULL,
    0xffffffffffffff95ULL,
    0xfffffffffffffffbULL,
    0xffffffffffffffffULL,
};

static unsigned leading_zeros(uint64_t x);
static uint64_t multiply_high(uint64_t a, uint64_t b, uint64_t *low);
static uint64_t neg_ln_q32(sof_random_t *r);
static uint64_t rotl(uint64_t x, int k);
static void swap(unsigned char *a, unsigned char *b, size_t size);
static int64_t to_signed(uint64_t x);

/// @brief Returns the number of 0 bits above the highest 1 bit of x, which
/// isn't 0.
/// @details sof_bit has this, but its word type is set at build time and
/// every program that builds sof_random.c would need its header, so the
/// library keeps its own. Without the GNU builtins, or with
/// SOF_RANDOM_NO_BUILTINS defined, it is a binary search.
static unsigned leading_zeros(uint64_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(SOF_RANDOM_NO_BUILTINS)
    return (unsigned)__builtin_clzll(x);
#else
    unsigned n = 0;
    for (unsigned shift = 32; shift > 0; shift >>= 1) {
        if ((x >> (64 - shift)) == 0) {
            n += shift;
            x <<= shift;
        }
    }
    return n;
#endif
}

/// @brief Returns the high 64 bits of the 128-bit product a * b, and stores
/// the low 64 in low.
/// @details Without unsigned __int128, or with SOF_RANDOM_NO_BUILTINS
/// defined, it multiplies 32-bit halves; the result is the same.
static uint64_t multiply_high(uint64_t a, uint64_t b, uint64_t *low) {
#if defined(__SIZEOF_INT128__) && !defined(SOF_RANDOM_NO_BUILTINS)
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

/// @brief Draws from the standard exponential distribution, in 32.32 fixed
/// point.
/// @details Ahrens and Dieter's algorithm SA (Knuth, TAOCP 3.4.1, algorithm
/// S), which needs no logarithm. An exponential draw is j ln 2 + Y, where j
/// is k with probability 2^-(k+1), and Y in [0, ln 2) has a density
/// proportional to e^-y. The leading zeros of one number give j, and the
/// bits after its highest 1 give a uniform U in [0, 1). If U < ln 2, which is
/// about 69% of the time, Y is U. Otherwise, with k the least number from 2
/// up for which U < exp_table[k], Y is ln 2 times the least of k more
/// uniforms. A number of 0, one time in 2^64, gives j = 64 and U from the next
/// number, cutting off a tail of 2^-65. Integer arithmetic only, so every
/// platform gets the same result. The result is below 65 ln 2, about 45.06,
/// so below 2^38. r must be ready.
static uint64_t neg_ln_q32(sof_random_t *r) {
    uint64_t x = sof_random_next(r);
    unsigned j;
    uint64_t u;
    if (x == 0) {
        j = 64;
        u = sof_random_next(r);
    } else {
        j = leading_zeros(x);
        // The 63 - j bits below the highest 1, at the top.
        u = (x << j) << 1;
        if (j >= 32) {
            // Fewer than 32 bits are left, so fill the rest from another
            // number. This happens one time in 2^32.
            u |= sof_random_next(r) >> (63 - j);
        }
    }
    // j ln 2, in 32.32 fixed point, rounded: j * ln 2 in 8.56 fits.
    uint64_t whole = ((uint64_t)j * (LN2_Q64 >> 8) + (1ULL << 23)) >> 24;
    if (u < LN2_Q64) {
        return whole + ((u >> 32) + ((u >> 31) & 1));
    }
    unsigned k = 2;
    while (k < EXP_TABLE_LAST && u >= exp_table[k]) {
        k++;
    }
    uint64_t least = UINT64_MAX;
    for (unsigned i = 0; i < k; i++) {
        uint64_t v = sof_random_next(r);
        if (v < least) {
            least = v;
        }
    }
    // least ln 2, both in 0.32, gives 0.64; rounded to 0.32.
    return whole + (((least >> 32) * (LN2_Q64 >> 32) + (1ULL << 31)) >> 32);
}

/// @brief Rotates x left by k bits, 0 < k < 64.
static uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

/// @brief Swaps the size bytes at a and b, which don't overlap.
/// @details The common sizes get a memcpy() of a constant size, which the
/// compiler turns into a load and a store; others go through a buffer in
/// SWAP_CHUNK pieces.
static void swap(unsigned char *a, unsigned char *b, size_t size) {
    unsigned char t[SWAP_CHUNK];
    switch (size) {
    case 1:
        t[0] = *a;
        *a = *b;
        *b = t[0];
        return;
    case 2:
        memcpy(t, a, 2);
        memcpy(a, b, 2);
        memcpy(b, t, 2);
        return;
    case 4:
        memcpy(t, a, 4);
        memcpy(a, b, 4);
        memcpy(b, t, 4);
        return;
    case 8:
        memcpy(t, a, 8);
        memcpy(a, b, 8);
        memcpy(b, t, 8);
        return;
    case 16:
        memcpy(t, a, 16);
        memcpy(a, b, 16);
        memcpy(b, t, 16);
        return;
    default:
        break;
    }
    if (size < SWAP_CHUNK) {
        // Small elements of other sizes go 8 bytes at a time, then a byte at
        // a time, rather than through three calls of memcpy().
        uint64_t x, y;
        for (; size >= 8; size -= 8, a += 8, b += 8) {
            memcpy(&x, a, 8);
            memcpy(&y, b, 8);
            memcpy(a, &y, 8);
            memcpy(b, &x, 8);
        }
        for (; size > 0; size--, a++, b++) {
            unsigned char c = *a;
            *a = *b;
            *b = c;
        }
        return;
    }
    while (size > 0) {
        size_t n = size < SWAP_CHUNK ? size : SWAP_CHUNK;
        memcpy(t, a, n);
        memcpy(a, b, n);
        memcpy(b, t, n);
        a += n;
        b += n;
        size -= n;
    }
}

/// @brief Returns the int64_t with the same two's complement bits as x,
/// without the implementation-defined conversion of a value above INT64_MAX.
static int64_t to_signed(uint64_t x) {
    if (x <= (uint64_t)INT64_MAX) {
        return (int64_t)x;
    }
    return -(int64_t)(UINT64_MAX - x) - 1;
}

uint64_t sof_random_below(sof_random_t *r, uint64_t n) {
    if (r == NULL || !r->ready || n < 2) {
        return 0;
    }
    // Lemire's method: the high 64 bits of x * n are from 0 to n - 1. Some
    // results come from one more x than others; those x are the ones whose
    // low 64 bits fall below 2^64 mod n, so they are drawn again. The
    // division that finds 2^64 mod n is needed only when the low bits are
    // below n, which is rare for small n.
    uint64_t low;
    uint64_t high = multiply_high(sof_random_next(r), n, &low);
    if (n > (UINT64_MAX >> 1)) {
        // Above 2^63, 2^64 mod n is 2^64 - n, without dividing, and nearly
        // half the numbers may be thrown away, so test against it at once.
        while (low < 0 - n) {
            high = multiply_high(sof_random_next(r), n, &low);
        }
    } else if (low < n) {
        uint64_t threshold = (0 - n) % n;
        while (low < threshold) {
            high = multiply_high(sof_random_next(r), n, &low);
        }
    }
    return high;
}

sof_random_error_t sof_random_bytes(sof_random_t *r, void *data, size_t length) {
    if (r == NULL || !r->ready || (data == NULL && length != 0)) {
        return SOF_RANDOM_ERROR;
    }
    unsigned char *p = data;
    while (length > 0) {
        uint64_t x = sof_random_next(r);
        size_t n = length < 8 ? length : 8;
        for (size_t i = 0; i < n; i++) {
            p[i] = (unsigned char)(x >> (8 * i));
        }
        p += n;
        length -= n;
    }
    return SOF_RANDOM_OK;
}

double sof_random_double(sof_random_t *r) {
    if (r == NULL || !r->ready) {
        return 0;
    }
    return (double)(sof_random_next(r) >> 11) * 0x1.0p-53;
}

uint64_t sof_random_exponential(sof_random_t *r, uint64_t mean) {
    if (r == NULL || !r->ready) {
        return 0;
    }
    uint64_t neg_ln = neg_ln_q32(r);
    // mean * neg_ln / 2^32, rounded, in parts so only the whole part of
    // neg_ln (at most 45) can overflow, and then the result saturates.
    uint64_t whole = neg_ln >> 32, fraction = neg_ln & 0xffffffffULL;
    uint64_t mean_high = mean >> 32, mean_low = mean & 0xffffffffULL;
    if (mean < (1ULL << 58)) {
        // neg_ln is below 46, so nothing can overflow.
        return mean * whole + mean_high * fraction + ((mean_low * fraction + (1ULL << 31)) >> 32);
    }
    if (whole != 0 && mean > UINT64_MAX / whole) {
        return UINT64_MAX;
    }
    uint64_t result = mean * whole;
    uint64_t parts[2] = {mean_high * fraction, (mean_low * fraction + (1ULL << 31)) >> 32};
    for (int i = 0; i < 2; i++) {
        if (result > UINT64_MAX - parts[i]) {
            return UINT64_MAX;
        }
        result += parts[i];
    }
    return result;
}

double sof_random_exponential_double(sof_random_t *r, double mean) {
    if (r == NULL || !r->ready) {
        return 0;
    }
    // The fixed point value is below 2^38, so it converts to a double
    // exactly, and the one multiply by mean is the only rounding.
    return (double)neg_ln_q32(r) * 0x1.0p-32 * mean;
}

sof_random_error_t sof_random_free(sof_random_t *r) {
    if (r == NULL || !r->ready) {
        return SOF_RANDOM_ERROR;
    }
    memset(r, 0, sizeof(*r));
    return SOF_RANDOM_OK;
}

sof_random_error_t sof_random_init(sof_random_t *r, uint64_t seed) {
    if (r == NULL) {
        return SOF_RANDOM_ERROR;
    }
    // splitmix64's results are a bijection of its states, and these four
    // states are distinct, so at most one of the words is 0.
    for (int i = 0; i < 4; i++) {
        r->s[i] = sof_random_splitmix64(&seed);
    }
    r->ready = 1;
    return SOF_RANDOM_OK;
}

sof_random_error_t sof_random_jump(sof_random_t *r) {
    static const uint64_t jump[4] = {
        0x180ec6d33cfd0abaULL,
        0xd5a61266f0c9392cULL,
        0xa9582618e03fc9aaULL,
        0x39abdc4529b1661cULL,
    };
    if (r == NULL || !r->ready) {
        return SOF_RANDOM_ERROR;
    }
    uint64_t s[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++) {
        for (int b = 0; b < 64; b++) {
            if (jump[i] & ((uint64_t)1 << b)) {
                s[0] ^= r->s[0];
                s[1] ^= r->s[1];
                s[2] ^= r->s[2];
                s[3] ^= r->s[3];
            }
            sof_random_next(r);
        }
    }
    memcpy(r->s, s, sizeof(s));
    return SOF_RANDOM_OK;
}

uint64_t sof_random_next(sof_random_t *r) {
    if (r == NULL || !r->ready) {
        return 0;
    }
    uint64_t *s = r->s;
    uint64_t result = rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

int64_t sof_random_range(sof_random_t *r, int64_t lo, int64_t hi) {
    if (hi < lo) {
        int64_t t = lo;
        lo = hi;
        hi = t;
    }
    if (r == NULL || !r->ready) {
        return lo;
    }
    uint64_t span = (uint64_t)hi - (uint64_t)lo;
    uint64_t offset = span == UINT64_MAX ? sof_random_next(r) : sof_random_below(r, span + 1);
    return to_signed((uint64_t)lo + offset);
}

sof_random_error_t sof_random_shuffle(sof_random_t *r, void *base, size_t count, size_t size) {
    if (r == NULL || !r->ready || size == 0 || (base == NULL && count != 0)) {
        return SOF_RANDOM_ERROR;
    }
    unsigned char *a = base;
    for (size_t i = count; i > 1; i--) {
        size_t j = (size_t)sof_random_below(r, i);
        if (j != i - 1) {
            swap(a + j * size, a + (i - 1) * size, size);
        }
    }
    return SOF_RANDOM_OK;
}

uint64_t sof_random_splitmix64(uint64_t *state) {
    if (state == NULL) {
        return 0;
    }
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
