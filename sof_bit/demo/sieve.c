/**
 * @file sieve.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A sieve of Eratosthenes over a bit array in C, for the sieve
 * program.
 * @details Bits are read and changed with sof_bit: sof_bit_clear() strikes
 * out each multiple, sof_bit_next() finds the next prime to sieve with and
 * the next prime asked for within a word, sof_bit_lowest() the first in
 * each word after it, and sof_bit_count() counts the primes in each word.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <string.h>

#include "sieve.h"

static uint64_t count_bits(const sieve_t *s, uint64_t from, uint64_t to);
static uint64_t next_bit(const sieve_t *s, uint64_t from);

/// @brief Counts the bits that are set from bit from up to, but not
/// including, bit to, which is at most s->bits.
static uint64_t count_bits(const sieve_t *s, uint64_t from, uint64_t to) {
    if (from >= to) {
        return 0;
    }
    size_t first = (size_t)(from / SOF_BIT_BITS);
    size_t last = (size_t)((to - 1) / SOF_BIT_BITS);
    unsigned low = (unsigned)(from % SOF_BIT_BITS);
    unsigned high = (unsigned)((to - 1) % SOF_BIT_BITS);
    uint64_t n = 0;
    for (size_t i = first; i <= last; i++) {
        sof_bit_t word = s->words[i];
        if (i == first) {
            // Only the bits from low up.
            word = (sof_bit_t)(word & ((sof_bit_t)~(sof_bit_t)0 << low));
        }
        if (i == last && high + 1 < SOF_BIT_BITS) {
            // Only the bits up to high.
            word = (sof_bit_t)(word & (((sof_bit_t)1 << (high + 1)) - 1));
        }
        n += sof_bit_count(word);
    }
    return n;
}

/// @brief Finds the lowest set bit at or above bit from.
/// @return Its number, or s->bits if there is none.
static uint64_t next_bit(const sieve_t *s, uint64_t from) {
    if (from >= s->bits) {
        return s->bits;
    }
    size_t i = (size_t)(from / SOF_BIT_BITS);
    unsigned bit = sof_bit_next(s->words[i], (unsigned)(from % SOF_BIT_BITS));
    while (bit == SOF_BIT_BITS) {
        if (++i == s->word_count) {
            return s->bits;
        }
        bit = sof_bit_lowest(s->words[i]);
    }
    // The bits past s->bits are 0, so this is below it.
    return (uint64_t)i * SOF_BIT_BITS + bit;
}

uint64_t sieve_count(const sieve_t *s, uint64_t low, uint64_t high) {
    if (s == NULL || s->words == NULL) {
        return 0;
    }
    if (high > s->limit) {
        high = s->limit;
    }
    if (low > high) {
        return 0;
    }
    uint64_t n = low <= 2 && high >= 2 ? 1 : 0;
    // The odd numbers from low to high have the bits from low / 2 up to,
    // but not including, high / 2 rounded up.
    return n + count_bits(s, low / 2, high / 2 + (high & 1));
}

sieve_error_t sieve_free(sieve_t *s) {
    if (s == NULL) {
        return SIEVE_ERROR;
    }
    sof_alloc_free(&s->alloc, s->words, s->word_count * sizeof(sof_bit_t));
    memset(s, 0, sizeof(*s));
    return SIEVE_OK;
}

sieve_error_t sieve_init(sieve_t *s, uint64_t limit, const sof_alloc_t *alloc) {
    if (s == NULL) {
        return SIEVE_ERROR;
    }
    memset(s, 0, sizeof(*s));
    if (!sof_alloc_valid(alloc)) {
        return SIEVE_ERROR;
    }
    if (alloc != NULL) {
        s->alloc = *alloc;
    }
    // One bit for each odd number from 1 to limit, in bits / SOF_BIT_BITS
    // + 1 words, so there is always a word to allocate and a word for the
    // bits left over after the full words.
    uint64_t bits = limit / 2 + (limit & 1);
    uint64_t words = bits / SOF_BIT_BITS + 1;
    if (words > SIZE_MAX / sizeof(sof_bit_t)) {
        return SIEVE_NO_MEM;
    }
    s->words = sof_alloc_malloc(&s->alloc, (size_t)words * sizeof(sof_bit_t));
    if (s->words == NULL) {
        return SIEVE_NO_MEM;
    }
    s->word_count = (size_t)words;
    s->bits = bits;
    s->limit = limit;

    // Every odd number is a prime until it is struck out, but 1.
    size_t full = (size_t)(bits / SOF_BIT_BITS);
    unsigned rest = (unsigned)(bits % SOF_BIT_BITS);
    for (size_t i = 0; i < full; i++) {
        s->words[i] = (sof_bit_t)~(sof_bit_t)0;
    }
    s->words[full] = rest == 0 ? 0 : (sof_bit_t)(((sof_bit_t)1 << rest) - 1);
    if (bits > 0) {
        s->words[0] = sof_bit_clear(s->words[0], 0);
    }

    // Each prime p strikes out its odd multiples from p * p, those below
    // having a smaller prime factor; p * p is odd, and so is every second
    // multiple after it, p bits apart.
    for (uint64_t i = next_bit(s, 1); i < bits; i = next_bit(s, i + 1)) {
        uint64_t p = 2 * i + 1;
        if (p > limit / p) {
            break;
        }
        for (uint64_t j = p * p / 2; j < bits; j += p) {
            size_t w = (size_t)(j / SOF_BIT_BITS);
            s->words[w] = sof_bit_clear(s->words[w], (unsigned)(j % SOF_BIT_BITS));
        }
    }
    s->count = (limit >= 2 ? 1 : 0) + count_bits(s, 0, bits);
    return SIEVE_OK;
}

int sieve_is_prime(const sieve_t *s, uint64_t n) {
    if (s == NULL || s->words == NULL || n > s->limit) {
        return -1;
    }
    if (n == 2) {
        return 1;
    }
    if ((n & 1) == 0) {
        return 0;
    }
    uint64_t i = n / 2;
    return sof_bit_test(s->words[i / SOF_BIT_BITS], (unsigned)(i % SOF_BIT_BITS));
}

uint64_t sieve_next(const sieve_t *s, uint64_t from) {
    if (s == NULL || s->words == NULL) {
        return 0;
    }
    if (from <= 2) {
        return s->limit >= 2 ? 2 : 0;
    }
    // The first odd number from from on has bit from / 2.
    uint64_t i = next_bit(s, from / 2);
    return i < s->bits ? 2 * i + 1 : 0;
}
