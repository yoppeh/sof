/**
 * @file sof_bit.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Bit operations on a machine word in c.
 * @details Every function takes a sof_bit_t by value and returns a result,
 * so there is nothing to set up or free and no state. sof_bit_set(),
 * sof_bit_clear() and sof_bit_flip() return the word with one bit
 * changed, sof_bit_test() reads one bit, sof_bit_count() counts the bits
 * that are set, sof_bit_lowest() finds the lowest set bit,
 * sof_bit_next() the lowest set bit at or above a given one,
 * sof_bit_highest() finds the highest set bit, which is the
 * base 2 logarithm rounded down, and sof_bit_ceil_log2() gives the
 * logarithm rounded up. Bits are numbered from 0, the least significant, to
 * SOF_BIT_BITS - 1.
 *
 * sof_bit_t is size_t, the machine's word, unless SOF_BIT_TYPE is defined
 * as another unsigned integer type, such as uint8_t or uint32_t, before this
 * header is included. With gcc and clang, sof_bit_count(),
 * sof_bit_lowest(), sof_bit_next(), sof_bit_highest() and
 * sof_bit_ceil_log2() use the compiler's builtins; defining
 * SOF_BIT_NO_BUILTINS uses plain c instead, which gives the same results:
 * a de Bruijn multiply and table lookup for the lowest bit, and a binary
 * search for the highest. Only compilers without the GNU builtins, such as MSVC, need
 * those.
 *
 * Everything is static inline, so there is no sof_bit.c.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_BIT_H
#define SOF_BIT_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#ifndef SOF_BIT_TYPE
/// @brief The type of sof_bit_t: size_t unless defined before this header.
#define SOF_BIT_TYPE size_t
#endif

/**
 * @brief The word the functions work on.
 * @details An unsigned integer type: size_t, or SOF_BIT_TYPE if that is
 * defined. Its size, and so the layout of an array of them, differs between
 * platforms.
 */
typedef SOF_BIT_TYPE sof_bit_t;

_Static_assert((sof_bit_t)-1 > 0, "SOF_BIT_TYPE must be an unsigned integer type");
_Static_assert(sizeof(sof_bit_t) <= sizeof(unsigned long long), "SOF_BIT_TYPE must fit in unsigned long long");

/**
 * @brief The number of bits in a sof_bit_t, as an unsigned constant.
 * @details 64 for size_t on a 64-bit machine, 32 on a 32-bit one.
 */
#define SOF_BIT_BITS ((unsigned)(sizeof(sof_bit_t) * CHAR_BIT))

static inline unsigned sof_bit_highest(sof_bit_t x);
static inline unsigned sof_bit_lowest(sof_bit_t x);

/**
 * @brief Returns the base 2 logarithm of a word, rounded up.
 * @details The smallest k for which 2^k is x or more: the number of bits
 * needed for x different values, or the exponent of the smallest power of 2
 * that holds x. sof_bit_ceil_log2(1000) is 10.
 * @param x The word.
 * @return 0 if x is 0 or 1; otherwise sof_bit_highest(x - 1) + 1, from 1
 * to SOF_BIT_BITS. It is SOF_BIT_BITS when x is above 2^(SOF_BIT_BITS -
 * 1), even though 2^SOF_BIT_BITS doesn't fit in a sof_bit_t.
 */
static inline unsigned sof_bit_ceil_log2(sof_bit_t x) {
    if (x <= 1) {
        return 0;
    }
    return sof_bit_highest((sof_bit_t)(x - 1)) + 1;
}

/**
 * @brief Clears one bit.
 * @param x The word.
 * @param bit The bit to clear, below SOF_BIT_BITS; any other value is
 * undefined behaviour, as a shift that large is in c.
 * @return x with bit cleared.
 */
static inline sof_bit_t sof_bit_clear(sof_bit_t x, unsigned bit) {
    return (sof_bit_t)(x & ~((sof_bit_t)1 << bit));
}

/**
 * @brief Counts the bits that are set.
 * @param x The word.
 * @return The number of set bits, from 0 to SOF_BIT_BITS.
 */
static inline unsigned sof_bit_count(sof_bit_t x) {
#if defined(__GNUC__) && !defined(SOF_BIT_NO_BUILTINS)
    return (unsigned)__builtin_popcountll((unsigned long long)x);
#else
    unsigned n = 0;
    while (x != 0) {
        x = (sof_bit_t)(x & (x - 1));
        n++;
    }
    return n;
#endif
}

/**
 * @brief Flips one bit.
 * @param x The word.
 * @param bit The bit to flip, below SOF_BIT_BITS; any other value is
 * undefined behaviour.
 * @return x with bit flipped.
 */
static inline sof_bit_t sof_bit_flip(sof_bit_t x, unsigned bit) {
    return (sof_bit_t)(x ^ ((sof_bit_t)1 << bit));
}

/**
 * @brief Finds the highest set bit.
 * @details For x above 0 this is the base 2 logarithm of x rounded down, and
 * one less than the number of bits needed to write x. The lowest set bit is
 * sof_bit_lowest(x).
 * @param x The word.
 * @return The number of the highest set bit of x, from 0 to SOF_BIT_BITS -
 * 1, or SOF_BIT_BITS if x is 0, the same "none" that sof_bit_next()
 * returns.
 */
static inline unsigned sof_bit_highest(sof_bit_t x) {
    if (x == 0) {
        return SOF_BIT_BITS;
    }
#if defined(__GNUC__) && !defined(SOF_BIT_NO_BUILTINS)
    return (unsigned)(sizeof(unsigned long long) * CHAR_BIT) - 1 - (unsigned)__builtin_clzll((unsigned long long)x);
#else
    // A binary search, six steps for 64 bits. A loop a bit at a time was 14
    // times slower on arm64; gcc and clang on x86-64 turn that loop into one
    // instruction, but they use the builtin above, and the compilers that
    // come here, such as MSVC, don't recognize it.
    unsigned n = 0;
    for (unsigned shift = SOF_BIT_BITS / 2; shift > 0; shift /= 2) {
        if ((x >> shift) != 0) {
            x = (sof_bit_t)(x >> shift);
            n += shift;
        }
    }
    return n;
#endif
}

/**
 * @brief Finds the lowest set bit.
 * @details The same as sof_bit_next(x, 0). With x &= x - 1, which clears the
 * lowest set bit, it gives the fastest loop over the set bits, lowest first:
 *
 *     for (; x != 0; x &= x - 1) {
 *         unsigned i = sof_bit_lowest(x);
 *         ...
 *     }
 *
 * which needs a copy of the word, as it clears x as it goes. sof_bit_next()
 * loops without changing the word, for a little more each time.
 * @param x The word.
 * @return The number of the lowest set bit of x, from 0 to SOF_BIT_BITS -
 * 1, or SOF_BIT_BITS if x is 0.
 */
static inline unsigned sof_bit_lowest(sof_bit_t x) {
    if (x == 0) {
        return SOF_BIT_BITS;
    }
#if defined(__GNUC__) && !defined(SOF_BIT_NO_BUILTINS)
    return (unsigned)__builtin_ctzll((unsigned long long)x);
#else
    // x & -x is the lowest set bit alone, 2^i. Times a de Bruijn sequence,
    // whose every window of log2(bits) bits is different, it puts a window
    // unique to i at the top, which the table turns back into i.
    if (sizeof(sof_bit_t) > 4) {
        static const unsigned char table64[64] = {
            0, 1, 48, 2, 57, 49, 28, 3, 61, 58, 50, 42, 38, 29, 17, 4,
            62, 55, 59, 36, 53, 51, 43, 22, 45, 39, 33, 30, 24, 18, 12, 5,
            63, 47, 56, 27, 60, 41, 37, 16, 54, 35, 52, 21, 44, 32, 23, 11,
            46, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19, 9, 13, 8, 7, 6,
        };
        unsigned long long bit = (unsigned long long)x & (0 - (unsigned long long)x);
        return table64[(uint64_t)(bit * 0x03f79d71b4cb0a89ULL) >> 58];
    }
    static const unsigned char table32[32] = {
        0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
        31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9,
    };
    uint32_t bit = (uint32_t)x & (0 - (uint32_t)x);
    return table32[(uint32_t)(bit * 0x077cb531U) >> 27];
#endif
}

/**
 * @brief Finds the lowest set bit at or above a given bit.
 * @details Every from is allowed, so a loop over the set bits can pass the
 * last one found plus 1, without changing x:
 *
 *     for (unsigned i = sof_bit_next(x, 0); i < SOF_BIT_BITS; i = sof_bit_next(x, i + 1))
 *
 * Clearing each bit from a copy, with sof_bit_lowest(), is faster; see
 * there.
 * @param x The word.
 * @param from The bit to start at.
 * @return The number of the lowest set bit at or above from, or
 * SOF_BIT_BITS if there is none, including when from is SOF_BIT_BITS or
 * more.
 */
static inline unsigned sof_bit_next(sof_bit_t x, unsigned from) {
    if (from >= SOF_BIT_BITS) {
        return SOF_BIT_BITS;
    }
    x = (sof_bit_t)(x >> from);
    if (x == 0) {
        return SOF_BIT_BITS;
    }
    return from + sof_bit_lowest(x);
}

/**
 * @brief Sets one bit.
 * @param x The word.
 * @param bit The bit to set, below SOF_BIT_BITS; any other value is
 * undefined behaviour.
 * @return x with bit set.
 */
static inline sof_bit_t sof_bit_set(sof_bit_t x, unsigned bit) {
    return (sof_bit_t)(x | ((sof_bit_t)1 << bit));
}

/**
 * @brief Reads one bit.
 * @param x The word.
 * @param bit The bit to read, below SOF_BIT_BITS; any other value is
 * undefined behaviour.
 * @return 1 if bit is set in x, or 0.
 */
static inline int sof_bit_test(sof_bit_t x, unsigned bit) {
    return (int)((x >> bit) & 1);
}

#endif // SOF_BIT_H
