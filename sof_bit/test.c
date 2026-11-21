/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_bit.h.
 * @details Usage: test, test-uint8, test-uint32, test-uint32-portable or
 * test-uint64-portable. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs them.
 *
 * The Makefile compiles this file into five programs: test, with the
 * default sof_bit_t; test-uint32, with uint32_t; and, without the
 * compiler's builtins, so with the plain c fallbacks, test-uint8,
 * test-uint32-portable and test-uint64-portable, with uint8_t, uint32_t and
 * uint64_t. The results are checked against bit-by-bit versions here that
 * don't use the library.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

/// @brief Makes a string of a macro's value.
#define STRING(x) #x
#define VALUE_STRING(x) STRING(x)

/// @brief The heading this build prints, naming the sof_bit_t and builtins
/// the Makefile chose with SOF_BIT_TYPE and SOF_BIT_NO_BUILTINS. It is
/// decided before sof_bit.h defines SOF_BIT_TYPE itself.
#if defined(SOF_BIT_TYPE) && defined(SOF_BIT_NO_BUILTINS)
#define HEADING "sof_bit with " VALUE_STRING(SOF_BIT_TYPE) " and no builtins:\n"
#elif defined(SOF_BIT_TYPE)
#define HEADING "sof_bit with " VALUE_STRING(SOF_BIT_TYPE) ":\n"
#elif defined(SOF_BIT_NO_BUILTINS)
#define HEADING "sof_bit with no builtins:\n"
#else
#define HEADING "sof_bit:\n"
#endif

#include "sof_bit.h"
#include "test.h"

/// @brief Random words tried by each test.
#define ROUNDS 2000

/// @brief Every bit of a sof_bit_t set.
#define ALL ((sof_bit_t) ~(sof_bit_t)0)

/// @brief Bit b of x, read without the library.
static int ref_test(sof_bit_t x, unsigned b) {
    return (int)(((unsigned long long)x >> b) & 1);
}

/// @brief The set bits of x, counted one at a time.
static unsigned ref_count(sof_bit_t x) {
    unsigned n = 0;
    for (unsigned b = 0; b < SOF_BIT_BITS; b++) {
        n += (unsigned)ref_test(x, b);
    }
    return n;
}

/// @brief The lowest set bit of x at or above from, found one bit at a time.
static unsigned ref_next(sof_bit_t x, unsigned from) {
    for (unsigned b = from; b < SOF_BIT_BITS; b++) {
        if (ref_test(x, b)) {
            return b;
        }
    }
    return SOF_BIT_BITS;
}

/// @brief The highest set bit of x, found one bit at a time, or SOF_BIT_BITS.
static unsigned ref_highest(sof_bit_t x) {
    for (unsigned b = SOF_BIT_BITS; b-- > 0;) {
        if (ref_test(x, b)) {
            return b;
        }
    }
    return SOF_BIT_BITS;
}

/// @brief The smallest k with 2^k at least x, found by trying each k.
static unsigned ref_ceil_log2(sof_bit_t x) {
    unsigned k = 0;
    while (k < SOF_BIT_BITS && ((unsigned long long)1 << k) < (unsigned long long)x) {
        k++;
    }
    return k;
}

/// @brief Returns 1 if x and y differ in bit b and nowhere else.
static int differ_only_in(sof_bit_t x, sof_bit_t y, unsigned b) {
    for (unsigned i = 0; i < SOF_BIT_BITS; i++) {
        if ((ref_test(x, i) != ref_test(y, i)) != (i == b)) {
            return 0;
        }
    }
    return 1;
}

/// @brief A random word: dense, sparse or very sparse, so that counts and
/// gaps between set bits of every size come up.
static sof_bit_t random_word(uint64_t *state) {
    uint64_t r = next_random(state);
    switch (next_random(state) % 3) {
        case 1:
            r &= next_random(state);
            break;
        case 2:
            r &= next_random(state) & next_random(state) & next_random(state);
            break;
    }
    return (sof_bit_t)r;
}

static void test_bits(void) {
    CHECK(SOF_BIT_BITS == sizeof(sof_bit_t) * CHAR_BIT);
    CHECK((sof_bit_t)(ALL + 1) == 0);
    CHECK(ref_count(ALL) == SOF_BIT_BITS);
}

static void test_known_values(void) {
    unsigned top = SOF_BIT_BITS - 1;
    CHECK(sof_bit_set(0, 0) == 1);
    CHECK(sof_bit_set(0, 3) == 8);
    CHECK(sof_bit_set(8, 3) == 8);
    CHECK(sof_bit_set(0, top) == (sof_bit_t)(ALL ^ (ALL >> 1)));
    CHECK(sof_bit_clear(ALL, 0) == (sof_bit_t)(ALL - 1));
    CHECK(sof_bit_clear(8, 3) == 0);
    CHECK(sof_bit_clear(0, 3) == 0);
    CHECK(sof_bit_clear(ALL, top) == (ALL >> 1));
    CHECK(sof_bit_flip(0, 5) == 32);
    CHECK(sof_bit_flip(32, 5) == 0);
    CHECK(sof_bit_flip(ALL, top) == (ALL >> 1));
    CHECK(sof_bit_test(8, 3) == 1);
    CHECK(sof_bit_test(8, 2) == 0);
    CHECK(sof_bit_test(ALL, top) == 1);
    CHECK(sof_bit_test(ALL >> 1, top) == 0);
    CHECK(sof_bit_count(0) == 0);
    CHECK(sof_bit_count(1) == 1);
    CHECK(sof_bit_count(0xa5) == 4);
    CHECK(sof_bit_count(ALL) == SOF_BIT_BITS);
    CHECK(sof_bit_count(ALL ^ (ALL >> 1)) == 1);
}

static void test_set_clear_flip(void) {
    // Each changes exactly the one bit, in the right direction, at every
    // position of random words.
    uint64_t state = 1;
    for (int round = 0; round < ROUNDS; round++) {
        sof_bit_t x = random_word(&state);
        for (unsigned b = 0; b < SOF_BIT_BITS; b++) {
            sof_bit_t s = sof_bit_set(x, b), c = sof_bit_clear(x, b), f = sof_bit_flip(x, b);
            CHECK(ref_test(s, b) == 1);
            CHECK(ref_test(c, b) == 0);
            CHECK(ref_test(f, b) != ref_test(x, b));
            CHECK(s == x || differ_only_in(s, x, b));
            CHECK(c == x || differ_only_in(c, x, b));
            CHECK(differ_only_in(f, x, b));
            CHECK(sof_bit_flip(f, b) == x);
        }
    }
}

static void test_test(void) {
    uint64_t state = 2;
    for (int round = 0; round < ROUNDS; round++) {
        sof_bit_t x = random_word(&state);
        for (unsigned b = 0; b < SOF_BIT_BITS; b++) {
            CHECK(sof_bit_test(x, b) == ref_test(x, b));
        }
    }
}

static void test_count(void) {
    uint64_t state = 3;
    for (int round = 0; round < ROUNDS; round++) {
        sof_bit_t x = random_word(&state);
        CHECK(sof_bit_count(x) == ref_count(x));
    }
    // Every count from 0 to SOF_BIT_BITS, filling from the bottom and from
    // the top.
    sof_bit_t low = 0, high = 0;
    for (unsigned b = 0; b < SOF_BIT_BITS; b++) {
        CHECK(sof_bit_count(low) == b);
        CHECK(sof_bit_count(high) == b);
        low = sof_bit_set(low, b);
        high = sof_bit_set(high, SOF_BIT_BITS - 1 - b);
    }
    CHECK(low == ALL && high == ALL);
    CHECK(sof_bit_count(low) == SOF_BIT_BITS);
}

static void test_next(void) {
    uint64_t state = 4;
    for (int round = 0; round < ROUNDS; round++) {
        sof_bit_t x = random_word(&state);
        for (unsigned from = 0; from <= SOF_BIT_BITS; from++) {
            CHECK(sof_bit_next(x, from) == ref_next(x, from));
        }
    }
    unsigned top = SOF_BIT_BITS - 1;
    CHECK(sof_bit_next(0, 0) == SOF_BIT_BITS);
    CHECK(sof_bit_next(ALL, 0) == 0);
    CHECK(sof_bit_next(ALL, top) == top);
    CHECK(sof_bit_next(sof_bit_set(0, top), 0) == top);
    CHECK(sof_bit_next(sof_bit_set(0, top), top) == top);
    CHECK(sof_bit_next(1, 1) == SOF_BIT_BITS);
}

static void test_lowest(void) {
    uint64_t state = 8;
    for (int round = 0; round < ROUNDS; round++) {
        sof_bit_t x = random_word(&state);
        CHECK(sof_bit_lowest(x) == ref_next(x, 0));
        CHECK(sof_bit_lowest(x) == sof_bit_next(x, 0));
    }
    // Zero has none; every single bit, every bit with all those above it
    // set, and every bit with the top bit set too, is found.
    CHECK(sof_bit_lowest(0) == SOF_BIT_BITS);
    CHECK(sof_bit_lowest(ALL) == 0);
    CHECK(sof_bit_lowest(100) == 2);
    unsigned top = SOF_BIT_BITS - 1;
    for (unsigned b = 0; b < SOF_BIT_BITS; b++) {
        sof_bit_t bit = sof_bit_set(0, b);
        CHECK(sof_bit_lowest(bit) == b);
        CHECK(sof_bit_lowest((sof_bit_t)(ALL << b)) == b);
        CHECK(sof_bit_lowest(sof_bit_set(bit, top)) == b);
        // From each bit up, next agrees with lowest of the shifted word.
        for (unsigned from = 0; from <= b; from++) {
            CHECK(sof_bit_next(sof_bit_set(bit, top), from) == b);
        }
    }
}

static void test_next_past_the_end(void) {
    // Any from at or past the end finds nothing, so a loop can always ask
    // from the last bit found plus 1.
    CHECK(sof_bit_next(ALL, SOF_BIT_BITS) == SOF_BIT_BITS);
    CHECK(sof_bit_next(ALL, SOF_BIT_BITS + 1) == SOF_BIT_BITS);
    CHECK(sof_bit_next(ALL, 1000) == SOF_BIT_BITS);
    CHECK(sof_bit_next(ALL, UINT_MAX) == SOF_BIT_BITS);
}

static void test_highest(void) {
    uint64_t state = 6;
    for (int round = 0; round < ROUNDS; round++) {
        sof_bit_t x = random_word(&state);
        CHECK(sof_bit_highest(x) == ref_highest(x));
    }
    // Zero has none; every single bit and every all-ones run below it is
    // found.
    CHECK(sof_bit_highest(0) == SOF_BIT_BITS);
    CHECK(sof_bit_highest(1) == 0);
    CHECK(sof_bit_highest(100) == 6);
    CHECK(sof_bit_ceil_log2(100) == 7);
    CHECK(sof_bit_highest(ALL) == SOF_BIT_BITS - 1);
    for (unsigned b = 0; b < SOF_BIT_BITS; b++) {
        sof_bit_t bit = sof_bit_set(0, b);
        CHECK(sof_bit_highest(bit) == b);
        CHECK(sof_bit_highest((sof_bit_t)(bit | (bit - 1))) == b);
        CHECK(sof_bit_highest(ALL >> (SOF_BIT_BITS - 1 - b)) == b);
    }
}

static void test_ceil_log2(void) {
    uint64_t state = 7;
    for (int round = 0; round < ROUNDS; round++) {
        sof_bit_t x = random_word(&state);
        CHECK(sof_bit_ceil_log2(x) == ref_ceil_log2(x));
    }
    CHECK(sof_bit_ceil_log2(0) == 0);
    CHECK(sof_bit_ceil_log2(1) == 0);
    CHECK(sof_bit_ceil_log2(2) == 1);
    CHECK(sof_bit_ceil_log2(3) == 2);
    CHECK(sof_bit_ceil_log2(4) == 2);
    CHECK(sof_bit_ceil_log2(5) == 3);
    CHECK(sof_bit_ceil_log2(ALL) == SOF_BIT_BITS);
    // At each power of 2 and on either side of it.
    for (unsigned b = 0; b < SOF_BIT_BITS; b++) {
        sof_bit_t power = sof_bit_set(0, b);
        CHECK(sof_bit_ceil_log2(power) == b);
        CHECK(sof_bit_ceil_log2((sof_bit_t)(power - 1)) == (b > 1 ? b : 0));
        CHECK(sof_bit_ceil_log2((sof_bit_t)(power + 1)) == b + 1);
    }
}

static void test_loop(void) {
    // The loop in the header visits each set bit once, in increasing order,
    // and nothing else.
    uint64_t state = 5;
    for (int round = 0; round < ROUNDS; round++) {
        sof_bit_t x = random_word(&state), seen = 0;
        unsigned visits = 0, last = 0;
        for (unsigned i = sof_bit_next(x, 0); i < SOF_BIT_BITS; i = sof_bit_next(x, i + 1)) {
            CHECK(visits == 0 || i > last);
            CHECK(ref_test(x, i) == 1);
            seen = sof_bit_set(seen, i);
            last = i;
            visits++;
        }
        CHECK(seen == x);
        CHECK(visits == ref_count(x));

        // The faster loop in the header, clearing the lowest bit of a copy
        // each time, visits the same bits in the same order.
        sof_bit_t copy = x, seen2 = 0;
        unsigned visits2 = 0;
        last = 0;
        for (; copy != 0; copy = (sof_bit_t)(copy & (copy - 1))) {
            unsigned i = sof_bit_lowest(copy);
            CHECK(visits2 == 0 || i > last);
            CHECK(ref_test(x, i) == 1);
            seen2 = sof_bit_set(seen2, i);
            last = i;
            visits2++;
        }
        CHECK(seen2 == x);
        CHECK(visits2 == visits);
    }
}

int main(void) {
    printf(HEADING);
    printf("sof_bit_t has %u bits, builtins %s\n", SOF_BIT_BITS,
#if defined(__GNUC__) && !defined(SOF_BIT_NO_BUILTINS)
           "on"
#else
           "off"
#endif
    );
    run("SOF_BIT_BITS", test_bits);
    run("known values", test_known_values);
    run("sof_bit_set, sof_bit_clear and sof_bit_flip", test_set_clear_flip);
    run("sof_bit_test", test_test);
    run("sof_bit_count", test_count);
    run("sof_bit_lowest", test_lowest);
    run("sof_bit_next", test_next);
    run("sof_bit_next past the end", test_next_past_the_end);
    run("looping over the set bits", test_loop);
    run("sof_bit_highest", test_highest);
    run("sof_bit_ceil_log2", test_ceil_log2);
    return test_report();
}
