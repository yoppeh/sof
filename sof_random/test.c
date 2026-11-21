/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_random.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 *
 * Checks splitmix64 and xoshiro256** against the reference
 * implementations' published results, then the ranges, bytes, doubles,
 * exponential draws (against pinned values that every platform must give),
 * shuffle and jump built on them, the NULL and freed cases, and separate
 * generators on separate threads. The statistical checks use fixed seeds, so
 * they give the same result on every run, and their bounds are far wider than
 * a correct generator comes near.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "sof_random.h"
#include "test.h"

/// @brief The threaded test: threads, and the numbers each draws.
#define THREADS 4
#define THREAD_DRAWS 100000

/// @brief What each thread of the threaded test draws, from its own
/// generator.
typedef struct thread_t {
    sof_random_t r;
    uint64_t sum;
    uint64_t last;
} thread_t;

static int is_permutation(const uint32_t *a, size_t n);
static void *draw(void *arg);
static double ref_exponential(sof_random_t *r);
static double ref_ln(double y);
static sof_random_t state_giving(uint64_t want, uint64_t s0);

/// @brief ln 2, as a double.
#define LN2 0.69314718055994530942

/// @brief ln(y) for y > 0, in doubles, without libm: y = m * 2^e, m in
/// [1, 2), and ln(m) is 2 atanh((m - 1) / (m + 1)) by its series.
static double ref_ln(double y) {
    int e = 0;
    while (y >= 2) {
        y /= 2;
        e++;
    }
    while (y < 1) {
        y *= 2;
        e--;
    }
    double t = (y - 1) / (y + 1), t2 = t * t, power = t, sum = 0;
    for (int k = 0; k < 40; k++) {
        sum += power / (2 * k + 1);
        power *= t2;
    }
    return e * LN2 + 2 * sum;
}

/// @brief An exponential draw with a mean of 1, made in doubles from the
/// numbers of r as the header describes algorithm SA, to compare with the
/// library's fixed point one: j from the leading zeros of one number (64 for
/// a number of 0), U from the bits after its highest 1 (or the next number),
/// filled from another number if fewer than 32 are left; j ln 2 + U if U <
/// ln 2, or else (j + V) ln 2, V the least of k more numbers, for the least
/// k from 2 up with U below the sum of (ln 2)^i / i! for i from 1 to k.
static double ref_exponential(sof_random_t *r) {
    uint64_t x = sof_random_next(r), bits;
    int j = 0;
    if (x == 0) {
        j = 64;
        bits = sof_random_next(r);
    } else {
        while (!(x & ((uint64_t)1 << 63))) {
            x <<= 1;
            j++;
        }
        bits = x << 1;
        if (j >= 32) {
            bits |= sof_random_next(r) >> (63 - j);
        }
    }
    double u = (double)bits * 0x1.0p-64;
    if (u < LN2) {
        return j * LN2 + u;
    }
    double term = LN2 * LN2 / 2, q = LN2 + term;
    int k = 2;
    while (k < 18 && u >= q) {
        k++;
        term *= LN2 / k;
        q += term;
    }
    double least = 1;
    for (int i = 0; i < k; i++) {
        double v = (double)sof_random_next(r) * 0x1.0p-64;
        least = v < least ? v : least;
    }
    return (j + least) * LN2;
}

/// @brief Returns the xoshiro256** state whose next number is want, with
/// s[0] (which doesn't affect it) as given: the inverse of rotl(s[1] * 5, 7)
/// * 9.
static sof_random_t state_giving(uint64_t want, uint64_t s0) {
    sof_random_t r = {{s0, 0, 0x1234567890abcdefULL, 0xfedcba0987654321ULL}, 1};
    uint64_t inverse9 = 0x8e38e38e38e38e39ULL, inverse5 = 0xcccccccccccccccdULL;
    uint64_t x = want * inverse9;
    x = (x >> 7) | (x << 57);
    r.s[1] = x * inverse5;
    return r;
}

/// @brief Draws THREAD_DRAWS numbers from the thread's generator, summing
/// them.
static void *draw(void *arg) {
    thread_t *t = arg;
    for (int i = 0; i < THREAD_DRAWS; i++) {
        t->last = sof_random_next(&t->r);
        t->sum += t->last;
    }
    return NULL;
}

/// @brief Returns whether a holds each of 0 to n - 1 once.
static int is_permutation(const uint32_t *a, size_t n) {
    static unsigned char seen[1000];
    if (n > sizeof(seen)) {
        return 0;
    }
    memset(seen, 0, n);
    for (size_t i = 0; i < n; i++) {
        if (a[i] >= n || seen[a[i]]) {
            return 0;
        }
        seen[a[i]] = 1;
    }
    return 1;
}

static void test_splitmix64(void) {
    // The reference implementation's first results from 1234567.
    static const uint64_t expected[] = {
        6457827717110365317ULL, 3203168211198807973ULL, 9817491932198370423ULL,
        4593380528125082431ULL, 16408922859458223821ULL,
    };
    uint64_t state = 1234567;
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        CHECK(sof_random_splitmix64(&state) == expected[i]);
    }
    CHECK(state == 1234567 + 5 * 0x9e3779b97f4a7c15ULL);
    // A state of 0 is fine.
    state = 0;
    CHECK(sof_random_splitmix64(&state) == 0xe220a8397b1dcdafULL);
    CHECK(sof_random_splitmix64(NULL) == 0);
}

static void test_next(void) {
    // The reference implementation's first results from the state 1, 2, 3, 4.
    static const uint64_t expected[] = {
        11520ULL, 0ULL, 1509978240ULL, 1215971899390074240ULL, 1216172134540287360ULL, 607988272756665600ULL,
    };
    sof_random_t r = {{1, 2, 3, 4}, 1};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        CHECK(sof_random_next(&r) == expected[i]);
    }
}

static void test_init(void) {
    sof_random_t r;
    CHECK(sof_random_init(&r, 0) == SOF_RANDOM_OK);
    CHECK(r.ready == 1);
    // The state is splitmix64's first four results from the seed.
    CHECK(r.s[0] == 0xe220a8397b1dcdafULL);
    CHECK(r.s[1] == 0x6e789e6aa1b965f4ULL);
    CHECK(r.s[2] == 0x06c45d188009454fULL);
    CHECK(r.s[3] == 0xf88bb8a8724c81ecULL);
    CHECK(sof_random_next(&r) == 11091344671253066420ULL);
    CHECK(sof_random_next(&r) == 13793997310169335082ULL);
    CHECK(sof_random_next(&r) == 1900383378846508768ULL);

    // Initializing again starts over.
    CHECK(sof_random_init(&r, 0) == SOF_RANDOM_OK);
    CHECK(sof_random_next(&r) == 11091344671253066420ULL);

    // Many seeds, none giving a state of all 0s.
    for (uint64_t seed = 0; seed < 10000; seed++) {
        CHECK(sof_random_init(&r, seed) == SOF_RANDOM_OK);
        CHECK((r.s[0] | r.s[1] | r.s[2] | r.s[3]) != 0);
    }
    CHECK(sof_random_free(&r) == SOF_RANDOM_OK);
}

static void test_seeds(void) {
    // The same seed gives the same numbers, different seeds different ones,
    // and a copy goes on like the original.
    sof_random_t a, b, c;
    CHECK(sof_random_init(&a, 42) == SOF_RANDOM_OK);
    CHECK(sof_random_init(&b, 42) == SOF_RANDOM_OK);
    CHECK(sof_random_init(&c, 43) == SOF_RANDOM_OK);
    int same = 0;
    for (int i = 0; i < 1000; i++) {
        uint64_t x = sof_random_next(&a);
        CHECK(sof_random_next(&b) == x);
        same += sof_random_next(&c) == x;
    }
    CHECK(same == 0);
    sof_random_t d = a;
    for (int i = 0; i < 1000; i++) {
        CHECK(sof_random_next(&d) == sof_random_next(&a));
    }
    CHECK(sof_random_free(&a) == SOF_RANDOM_OK);
    CHECK(sof_random_free(&b) == SOF_RANDOM_OK);
    CHECK(sof_random_free(&c) == SOF_RANDOM_OK);
    CHECK(sof_random_free(&d) == SOF_RANDOM_OK);
}

static void test_bits(void) {
    // Each of the 64 bits is set about half the time.
    sof_random_t r;
    CHECK(sof_random_init(&r, 7) == SOF_RANDOM_OK);
    int counts[64] = {0};
    const int draws = 100000;
    for (int i = 0; i < draws; i++) {
        uint64_t x = sof_random_next(&r);
        for (int b = 0; b < 64; b++) {
            counts[b] += (x >> b) & 1;
        }
    }
    for (int b = 0; b < 64; b++) {
        CHECK(counts[b] > draws / 2 - 2000 && counts[b] < draws / 2 + 2000);
    }
    CHECK(sof_random_free(&r) == SOF_RANDOM_OK);
}

static void test_below(void) {
    sof_random_t r;
    CHECK(sof_random_init(&r, 1) == SOF_RANDOM_OK);

    // 0 and 1 have one answer, and don't use the generator.
    sof_random_t before = r;
    CHECK(sof_random_below(&r, 0) == 0);
    CHECK(sof_random_below(&r, 1) == 0);
    CHECK(memcmp(r.s, before.s, sizeof(r.s)) == 0);

    // A power of 2 is never redrawn: it is the high bits of one number.
    sof_random_t copy = r;
    for (int i = 0; i < 1000; i++) {
        CHECK(sof_random_below(&r, 1024) == sof_random_next(&copy) >> 54);
    }

    // Golden values from seed 1, which every build must give exactly.
    static const uint64_t golden[] = {
        4800180567299276685ULL, 574107, 5088625326638166914ULL, 932575, 8828779273611125371ULL, 932775,
        5533409047677527001ULL, 890544, 742075105987019300ULL, 491361, 422616464214307605ULL, 63774,
    };
    sof_random_t g;
    CHECK(sof_random_init(&g, 1) == SOF_RANDOM_OK);
    for (size_t i = 0; i < sizeof(golden) / sizeof(golden[0]); i++) {
        CHECK(sof_random_below(&g, i % 2 ? 1000003 : ((uint64_t)1 << 63) + 12345) == golden[i]);
    }

    // Otherwise the result is the high 64 bits of x * n for the first x
    // whose low 64 bits aren't below 2^64 mod n, computed here in 32-bit
    // halves. n = 3 * 2^62 throws away a quarter of the numbers.
    static const uint64_t lemire_ns[] = {3, 10, 1000003, (uint64_t)3 << 62, UINT64_MAX - 1};
    for (size_t k = 0; k < sizeof(lemire_ns) / sizeof(lemire_ns[0]); k++) {
        uint64_t n = lemire_ns[k], threshold = (0 - n) % n;
        int redrawn = 0;
        for (int i = 0; i < 10000; i++) {
            uint64_t got = sof_random_below(&r, n), x, high, low;
            do {
                x = sof_random_next(&copy);
                uint64_t x0 = x & 0xffffffffULL, x1 = x >> 32, n0 = n & 0xffffffffULL, n1 = n >> 32;
                uint64_t p00 = x0 * n0, p01 = x0 * n1, p10 = x1 * n0, p11 = x1 * n1;
                uint64_t middle = (p00 >> 32) + (p01 & 0xffffffffULL) + (p10 & 0xffffffffULL);
                low = (middle << 32) | (p00 & 0xffffffffULL);
                high = p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32);
                redrawn += low < threshold;
            } while (low < threshold);
            CHECK(got == high);
        }
        CHECK(memcmp(r.s, copy.s, sizeof(r.s)) == 0);
        if (n == ((uint64_t)3 << 62)) {
            CHECK(redrawn > 2500 && redrawn < 4200);
        }
    }

    // Every result is in range, for small and large n.
    static const uint64_t ns[] = {2, 3, 7, 10, 1000, 1000003, (uint64_t)1 << 32, ((uint64_t)1 << 63) + 1, UINT64_MAX};
    for (size_t k = 0; k < sizeof(ns) / sizeof(ns[0]); k++) {
        for (int i = 0; i < 10000; i++) {
            CHECK(sof_random_below(&r, ns[k]) < ns[k]);
        }
    }

    // With n just over 2^63, nearly half of the numbers are redrawn, and the
    // results are still spread evenly: about half are below n / 2. Taking
    // x % n without redrawing would put two thirds there.
    uint64_t n = ((uint64_t)1 << 63) + 1;
    int low = 0;
    for (int i = 0; i < 10000; i++) {
        low += sof_random_below(&r, n) < n / 2;
    }
    CHECK(low > 4700 && low < 5300);

    // Ten values, each drawn about equally often: a chi-square test with 9
    // degrees of freedom, whose 99.99th percentile is about 33.7.
    int counts[10] = {0};
    const int draws = 100000;
    for (int i = 0; i < draws; i++) {
        counts[sof_random_below(&r, 10)]++;
    }
    double chi = 0;
    for (int v = 0; v < 10; v++) {
        double d = counts[v] - draws / 10.0;
        chi += d * d / (draws / 10.0);
    }
    CHECK(chi < 33.7);
    CHECK(sof_random_free(&r) == SOF_RANDOM_OK);
}

static void test_range(void) {
    sof_random_t r;
    CHECK(sof_random_init(&r, 2) == SOF_RANDOM_OK);
    CHECK(sof_random_range(&r, 5, 5) == 5);
    CHECK(sof_random_range(&r, -9, -9) == -9);

    // Every value of a small range turns up, and nothing outside it, with the
    // ends given either way round.
    int seen[11] = {0};
    for (int i = 0; i < 10000; i++) {
        int64_t x = sof_random_range(&r, -5, 5);
        CHECK(x >= -5 && x <= 5);
        seen[x + 5]++;
        x = sof_random_range(&r, 5, -5);
        CHECK(x >= -5 && x <= 5);
        seen[x + 5]++;
    }
    for (int v = 0; v < 11; v++) {
        CHECK(seen[v] > 1500 && seen[v] < 2100);
    }

    // The full range is one number from the generator, as a signed value.
    sof_random_t copy = r;
    int negative = 0;
    for (int i = 0; i < 1000; i++) {
        int64_t x = sof_random_range(&r, INT64_MIN, INT64_MAX);
        uint64_t u = sof_random_next(&copy);
        CHECK((uint64_t)x == u + ((uint64_t)1 << 63));
        negative += x < 0;
    }
    CHECK(negative > 400 && negative < 600);

    // Ranges near the ends of int64_t.
    for (int i = 0; i < 1000; i++) {
        int64_t x = sof_random_range(&r, INT64_MAX - 3, INT64_MAX);
        CHECK(x >= INT64_MAX - 3);
        x = sof_random_range(&r, INT64_MIN, INT64_MIN + 3);
        CHECK(x <= INT64_MIN + 3);
        x = sof_random_range(&r, INT64_MIN, 0);
        CHECK(x <= 0);
    }
    CHECK(sof_random_free(&r) == SOF_RANDOM_OK);
}

static void test_double(void) {
    sof_random_t r;
    CHECK(sof_random_init(&r, 3) == SOF_RANDOM_OK);
    sof_random_t copy = r;
    double sum = 0;
    const int draws = 100000;
    for (int i = 0; i < draws; i++) {
        double x = sof_random_double(&r);
        CHECK(x >= 0 && x < 1);
        CHECK(x == (double)(sof_random_next(&copy) >> 11) / 9007199254740992.0);
        sum += x;
    }
    CHECK(sum / draws > 0.49 && sum / draws < 0.51);

    // The largest number gives the largest double below 1.
    sof_random_t top = {{0, 0, 0, 0}, 1};
    // xoshiro256** returns rotl(s[1] * 5, 7) * 9; this s[1] makes that all 1s.
    uint64_t want = UINT64_MAX;
    uint64_t inverse9 = 0x8e38e38e38e38e39ULL, inverse5 = 0xcccccccccccccccdULL;
    uint64_t x = want * inverse9;
    x = (x >> 7) | (x << 57);
    top.s[1] = x * inverse5;
    top.s[0] = 1;
    CHECK(sof_random_double(&top) == 1.0 - 1.0 / 9007199254740992.0);
    CHECK(sof_random_free(&r) == SOF_RANDOM_OK);
}

static void test_exponential(void) {
    // Golden values from seed 12345, which every platform must give exactly:
    // whole numbers with a mean of 1000, then doubles with a mean of 1.
    static const uint64_t golden[] = {
        488, 1427, 34, 4526, 1665, 876, 1237, 372, 679, 94, 1068, 757,
    };
    static const double golden_double[] = {
        0x1.6fa6591ap+0, 0x1.c3797d4ep-1, 0x1.e49abe69p+1, 0x1.c0bb4d72p+1, 0x1.b37d7f64p-2, 0x1.59305964p-2,
    };
    sof_random_t r;
    CHECK(sof_random_init(&r, 12345) == SOF_RANDOM_OK);
    for (size_t i = 0; i < sizeof(golden) / sizeof(golden[0]); i++) {
        CHECK(sof_random_exponential(&r, 1000) == golden[i]);
    }
    for (size_t i = 0; i < sizeof(golden_double) / sizeof(golden_double[0]); i++) {
        CHECK(sof_random_exponential_double(&r, 1.0) == golden_double[i]);
    }

    // Each draw is the one algorithm SA makes in doubles from the same
    // numbers, to within a few units of the fixed point's 2^-32, and uses
    // exactly those numbers. Some take one number, some several.
    sof_random_t copy = r;
    double largest = 0;
    for (int i = 0; i < 100000; i++) {
        double x = sof_random_exponential_double(&r, 1.0);
        double want = ref_exponential(&copy);
        CHECK(x - want < 4e-9 && want - x < 4e-9);
        CHECK(memcmp(r.s, copy.s, sizeof(r.s)) == 0);
        largest = x > largest ? x : largest;
    }
    CHECK(largest > 8 && largest < 65 * LN2);

    // Numbers with many leading zeros: 0 (j is 64, and U is the next
    // number), and from 31 to 63 zeros, where the bits after the highest 1
    // are too few and are filled from another number.
    for (int zeros = 30; zeros <= 64; zeros++) {
        uint64_t one = zeros == 64 ? 0 : (uint64_t)1 << (63 - zeros);
        uint64_t first = one == 0 ? 0 : one | (5 & (one - 1));
        for (uint64_t s0 = 1; s0 <= 20; s0++) {
            sof_random_t a = state_giving(first, s0), b = a;
            double x = sof_random_exponential_double(&a, 1.0);
            double want = ref_exponential(&b);
            CHECK(x - want < 4e-9 && want - x < 4e-9);
            CHECK(memcmp(a.s, b.s, sizeof(a.s)) == 0);
            CHECK(x >= zeros * LN2 - 1e-9 && x < 65 * LN2);
        }
    }

    // The whole number is the fixed point value times the mean, rounded to
    // the nearest, and uses the same numbers, even with a mean of 0.
    static const uint64_t means[] = {0, 1, 2, 3, 10, 1000, 123456789, (uint64_t)1 << 26, ((uint64_t)1 << 58) - 1};
    for (size_t k = 0; k < sizeof(means) / sizeof(means[0]); k++) {
        for (int i = 0; i < 10000; i++) {
            uint64_t neg_ln = (uint64_t)(sof_random_exponential_double(&copy, 1.0) * 4294967296.0);
            uint64_t whole = neg_ln >> 32, fraction = neg_ln & 0xffffffffULL, m = means[k];
            uint64_t want = m * whole + (m >> 32) * fraction + (((m & 0xffffffffULL) * fraction + ((uint64_t)1 << 31)) >> 32);
            CHECK(sof_random_exponential(&r, m) == want);
            CHECK(memcmp(r.s, copy.s, sizeof(r.s)) == 0);
        }
    }

    // The double scales with its mean: one multiply of the mean-1 draw.
    static const double double_means[] = {0.0, 0.5, 3.0, 1e-300, 1e300, -2.0};
    for (size_t k = 0; k < sizeof(double_means) / sizeof(double_means[0]); k++) {
        for (int i = 0; i < 1000; i++) {
            double unit = sof_random_exponential_double(&copy, 1.0);
            CHECK(sof_random_exponential_double(&r, double_means[k]) == unit * double_means[k]);
        }
    }

    // Huge means saturate rather than wrap: a draw of 1 or more times
    // UINT64_MAX doesn't fit.
    for (int i = 0; i < 10000; i++) {
        double unit = sof_random_exponential_double(&copy, 1.0);
        uint64_t x = sof_random_exponential(&r, UINT64_MAX);
        if (unit >= 1.0) {
            CHECK(x == UINT64_MAX);
        } else {
            double want = unit * 18446744073709551616.0;
            CHECK((double)x > want * 0.999999 && (double)x < want * 1.000001 + 1);
        }
    }
    // Means just over 2^58 take the checked path, and match the fast one.
    for (int i = 0; i < 10000; i++) {
        sof_random_t before = r;
        uint64_t big = sof_random_exponential(&r, ((uint64_t)1 << 58) + 1);
        uint64_t small = sof_random_exponential(&before, (uint64_t)1 << 58);
        CHECK(big < UINT64_MAX);
        CHECK(big >= small && big - small <= 64);
    }
    // 2^58 never overflows: every draw is below 65 ln 2, and a number of 0,
    // which gives the largest, from 64 ln 2 up, still fits.
    sof_random_t top = state_giving(0, 1), top2 = top;
    double most = sof_random_exponential_double(&top2, 1.0);
    CHECK(most > 64 * LN2 && most < 65 * LN2);
    CHECK(sof_random_exponential(&top, (uint64_t)1 << 58) < UINT64_MAX);
    CHECK(sof_random_free(&r) == SOF_RANDOM_OK);
}

static void test_exponential_distribution(void) {
    // Over a million draws with a mean of 1000, the sample mean is within
    // 1% (the standard error is 0.1%), and the share above t means is within
    // half a percentage point of e^-t.
    sof_random_t r;
    CHECK(sof_random_init(&r, 10) == SOF_RANDOM_OK);
    const int draws = 1000000;
    static const struct {
        uint64_t above;
        double share;
    } tails[] = {
        {100, 0.904837}, {500, 0.606531}, {1000, 0.367879}, {2000, 0.135335}, {4000, 0.018316}, {8000, 0.000335},
    };
    int counts[sizeof(tails) / sizeof(tails[0])] = {0};
    uint64_t sum = 0;
    double sum_double = 0, sum_squares = 0;
    for (int i = 0; i < draws; i++) {
        uint64_t x = sof_random_exponential(&r, 1000);
        sum += x;
        sum_squares += (double)x * (double)x;
        for (size_t t = 0; t < sizeof(tails) / sizeof(tails[0]); t++) {
            counts[t] += x > tails[t].above;
        }
        sum_double += sof_random_exponential_double(&r, 2.5);
    }
    CHECK(sum / (double)draws > 990 && sum / (double)draws < 1010);
    CHECK(sum_double / draws > 2.475 && sum_double / draws < 2.525);
    // The variance is the mean squared, 1,000,000; the standard error of
    // the sample variance is about 0.3%.
    double mean = sum / (double)draws;
    CHECK(sum_squares / draws - mean * mean > 980000 && sum_squares / draws - mean * mean < 1020000);
    for (size_t t = 0; t < sizeof(tails) / sizeof(tails[0]); t++) {
        double share = counts[t] / (double)draws;
        CHECK(share > tails[t].share - 0.005 && share < tails[t].share + 0.005);
    }

    // With a mean of 1, whole numbers k come up with the probability that
    // the draw rounds to k: e^-(k - 1/2) - e^-(k + 1/2), and 1 - e^-1/2 for 0.
    static const double rounded[] = {0.393469, 0.383400, 0.141047, 0.051888, 0.019088};
    int seen[5] = {0};
    for (int i = 0; i < draws; i++) {
        uint64_t x = sof_random_exponential(&r, 1);
        if (x < 5) {
            seen[x]++;
        }
    }
    for (int k = 0; k < 5; k++) {
        double share = seen[k] / (double)draws;
        CHECK(share > rounded[k] - 0.003 && share < rounded[k] + 0.003);
    }

    // Goodness of fit: 64 bins that each hold 1/64 of the distribution,
    // bin i from -ln(1 - i/64) to -ln(1 - (i + 1)/64), over four million
    // draws. A chi-square test with 63 degrees of freedom, whose 99.99th
    // percentile is about 113.5. The bins cross the multiples of ln 2 where
    // the draw's parts meet.
    enum { BINS = 64 };
    double edges[BINS];
    for (int i = 0; i < BINS; i++) {
        edges[i] = -ref_ln(1 - (double)i / BINS);
    }
    static int bins[BINS];
    memset(bins, 0, sizeof(bins));
    const int fit_draws = 4000000;
    for (int i = 0; i < fit_draws; i++) {
        double x = sof_random_exponential_double(&r, 1.0);
        int lo = 0, hi = BINS - 1;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (x >= edges[mid]) {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        bins[lo]++;
    }
    double chi = 0, expected = fit_draws / (double)BINS;
    for (int i = 0; i < BINS; i++) {
        double d = bins[i] - expected;
        chi += d * d / expected;
    }
    CHECK(chi < 113.5);
    CHECK(sof_random_free(&r) == SOF_RANDOM_OK);
}

static void test_bytes(void) {
    sof_random_t r;
    CHECK(sof_random_init(&r, 4) == SOF_RANDOM_OK);
    for (size_t length = 0; length <= 40; length++) {
        unsigned char buffer[48];
        memset(buffer, 0xa5, sizeof(buffer));
        sof_random_t copy = r;
        CHECK(sof_random_bytes(&r, buffer, length) == SOF_RANDOM_OK);
        // Each 8 bytes are one number, lowest byte first.
        for (size_t i = 0; i < length; i += 8) {
            uint64_t x = sof_random_next(&copy);
            for (size_t j = i; j < length && j < i + 8; j++) {
                CHECK(buffer[j] == (unsigned char)(x >> (8 * (j - i))));
            }
        }
        for (size_t j = length; j < sizeof(buffer); j++) {
            CHECK(buffer[j] == 0xa5);
        }
        CHECK(memcmp(r.s, copy.s, sizeof(r.s)) == 0);
    }
    // Nothing to write is fine, even with nowhere to write it.
    sof_random_t before = r;
    CHECK(sof_random_bytes(&r, NULL, 0) == SOF_RANDOM_OK);
    CHECK(memcmp(r.s, before.s, sizeof(r.s)) == 0);
    CHECK(sof_random_bytes(&r, NULL, 1) == SOF_RANDOM_ERROR);
    CHECK(sof_random_free(&r) == SOF_RANDOM_OK);
}

static void test_shuffle(void) {
    sof_random_t r;
    CHECK(sof_random_init(&r, 5) == SOF_RANDOM_OK);

    // The elements are all still there, for many sizes.
    static uint32_t a[1000];
    for (size_t n = 0; n <= 1000; n += n < 20 ? 1 : 97) {
        for (size_t i = 0; i < n; i++) {
            a[i] = (uint32_t)i;
        }
        CHECK(sof_random_shuffle(&r, a, n, sizeof(a[0])) == SOF_RANDOM_OK);
        CHECK(is_permutation(a, n));
    }

    // 0 and 1 elements stay as they are and don't use the generator.
    sof_random_t before = r;
    a[0] = 7;
    CHECK(sof_random_shuffle(&r, a, 1, sizeof(a[0])) == SOF_RANDOM_OK);
    CHECK(sof_random_shuffle(&r, a, 0, sizeof(a[0])) == SOF_RANDOM_OK);
    CHECK(sof_random_shuffle(&r, NULL, 0, sizeof(a[0])) == SOF_RANDOM_OK);
    CHECK(a[0] == 7);
    CHECK(memcmp(r.s, before.s, sizeof(r.s)) == 0);

    // It is the Fisher-Yates shuffle, drawing with sof_random_below().
    sof_random_t copy = r;
    uint32_t b[50], c[50];
    for (uint32_t i = 0; i < 50; i++) {
        b[i] = c[i] = i;
    }
    CHECK(sof_random_shuffle(&r, b, 50, sizeof(b[0])) == SOF_RANDOM_OK);
    for (size_t i = 50; i > 1; i--) {
        size_t j = (size_t)sof_random_below(&copy, i);
        uint32_t t = c[j];
        c[j] = c[i - 1];
        c[i - 1] = t;
    }
    CHECK(memcmp(b, c, sizeof(b)) == 0);

    // Each of the 6 orders of 3 elements comes up about equally often.
    int orders[6] = {0};
    const int shuffles = 60000;
    for (int k = 0; k < shuffles; k++) {
        unsigned char e[3] = {0, 1, 2};
        CHECK(sof_random_shuffle(&r, e, 3, 1) == SOF_RANDOM_OK);
        // Number the order by its first two elements.
        orders[e[0] * 2 + (e[1] > e[2])]++;
    }
    for (int o = 0; o < 6; o++) {
        CHECK(orders[o] > 9400 && orders[o] < 10600);
    }

    // Elements larger than the swap buffer, and of odd sizes, move whole.
    typedef struct {
        unsigned char bytes[150];
    } big_t;
    static big_t big[40];
    for (int i = 0; i < 40; i++) {
        memset(big[i].bytes, i, sizeof(big[i].bytes));
    }
    CHECK(sof_random_shuffle(&r, big, 40, sizeof(big_t)) == SOF_RANDOM_OK);
    uint32_t firsts[40];
    for (int i = 0; i < 40; i++) {
        for (size_t j = 1; j < sizeof(big[i].bytes); j++) {
            CHECK(big[i].bytes[j] == big[i].bytes[0]);
        }
        firsts[i] = big[i].bytes[0];
    }
    CHECK(is_permutation(firsts, 40));

    CHECK(sof_random_shuffle(&r, a, 10, 0) == SOF_RANDOM_ERROR);
    CHECK(sof_random_shuffle(&r, NULL, 10, 4) == SOF_RANDOM_ERROR);
    CHECK(sof_random_free(&r) == SOF_RANDOM_OK);
}

static void test_jump(void) {
    // The jump from the state 1, 2, 3, 4, as the reference jump() gives it.
    sof_random_t r = {{1, 2, 3, 4}, 1};
    CHECK(sof_random_jump(&r) == SOF_RANDOM_OK);
    CHECK(r.s[0] == 0x8c7a153956b5f3d1ULL);
    CHECK(r.s[1] == 0x701f1a713401d85eULL);
    CHECK(r.s[2] == 0x6527f66a65469085ULL);
    CHECK(r.s[3] == 0x8386b786c4408050ULL);
    CHECK(r.ready == 1);

    // A jumped copy gives numbers unlike the original's.
    sof_random_t a, b;
    CHECK(sof_random_init(&a, 6) == SOF_RANDOM_OK);
    b = a;
    CHECK(sof_random_jump(&b) == SOF_RANDOM_OK);
    int same = 0;
    for (int i = 0; i < 1000; i++) {
        same += sof_random_next(&a) == sof_random_next(&b);
    }
    CHECK(same == 0);
    CHECK(sof_random_free(&a) == SOF_RANDOM_OK);
    CHECK(sof_random_free(&b) == SOF_RANDOM_OK);
}

static void test_null(void) {
    CHECK(sof_random_init(NULL, 1) == SOF_RANDOM_ERROR);
    CHECK(sof_random_below(NULL, 10) == 0);
    unsigned char buffer[4] = {1, 2, 3, 4};
    CHECK(sof_random_bytes(NULL, buffer, sizeof(buffer)) == SOF_RANDOM_ERROR);
    CHECK(buffer[0] == 1 && buffer[3] == 4);
    CHECK(sof_random_double(NULL) == 0);
    CHECK(sof_random_exponential(NULL, 1000) == 0);
    CHECK(sof_random_exponential_double(NULL, 1000.0) == 0);
    CHECK(sof_random_free(NULL) == SOF_RANDOM_ERROR);
    CHECK(sof_random_jump(NULL) == SOF_RANDOM_ERROR);
    CHECK(sof_random_next(NULL) == 0);
    CHECK(sof_random_range(NULL, 3, 9) == 3);
    CHECK(sof_random_range(NULL, 9, 3) == 3);
    CHECK(sof_random_shuffle(NULL, buffer, sizeof(buffer), 1) == SOF_RANDOM_ERROR);
    CHECK(buffer[0] == 1 && buffer[3] == 4);
    CHECK(sof_random_splitmix64(NULL) == 0);
}

/// @brief Checks that every function treats r as dead.
static void check_dead(sof_random_t *r) {
    CHECK(r->ready == 0);
    CHECK(sof_random_next(r) == 0);
    CHECK(sof_random_below(r, 10) == 0);
    CHECK(sof_random_range(r, -4, 4) == -4);
    CHECK(sof_random_double(r) == 0);
    CHECK(sof_random_exponential(r, 1000) == 0);
    CHECK(sof_random_exponential_double(r, 1000.0) == 0);
    unsigned char buffer[4] = {1, 2, 3, 4};
    CHECK(sof_random_bytes(r, buffer, sizeof(buffer)) == SOF_RANDOM_ERROR);
    CHECK(sof_random_shuffle(r, buffer, sizeof(buffer), 1) == SOF_RANDOM_ERROR);
    CHECK(buffer[0] == 1 && buffer[1] == 2 && buffer[2] == 3 && buffer[3] == 4);
    CHECK(sof_random_jump(r) == SOF_RANDOM_ERROR);
    CHECK(sof_random_free(r) == SOF_RANDOM_ERROR);
    CHECK((r->s[0] | r->s[1] | r->s[2] | r->s[3]) == 0);
}

static void test_freed(void) {
    sof_random_t r;
    CHECK(sof_random_init(&r, 8) == SOF_RANDOM_OK);
    sof_random_next(&r);
    CHECK(sof_random_free(&r) == SOF_RANDOM_OK);
    check_dead(&r);

    // A zeroed sof_random_t, never initialized, is dead too.
    sof_random_t z = {{0, 0, 0, 0}, 0};
    check_dead(&z);

    // A freed generator can be initialized again, and starts over.
    sof_random_t fresh;
    CHECK(sof_random_init(&fresh, 8) == SOF_RANDOM_OK);
    CHECK(sof_random_init(&r, 8) == SOF_RANDOM_OK);
    CHECK(sof_random_next(&r) == sof_random_next(&fresh));
    CHECK(sof_random_free(&r) == SOF_RANDOM_OK);
    CHECK(sof_random_free(&fresh) == SOF_RANDOM_OK);
}

static void test_threads(void) {
    // Each thread draws from its own jumped copy of one generator, with no
    // lock, and gets exactly what drawing from the copies in turn gives.
    thread_t threads[THREADS], serial[THREADS];
    sof_random_t r;
    CHECK(sof_random_init(&r, 9) == SOF_RANDOM_OK);
    for (int i = 0; i < THREADS; i++) {
        threads[i].r = r;
        threads[i].sum = threads[i].last = 0;
        serial[i] = threads[i];
        CHECK(sof_random_jump(&r) == SOF_RANDOM_OK);
    }
    pthread_t ids[THREADS];
    for (int i = 0; i < THREADS; i++) {
        CHECK(pthread_create(&ids[i], NULL, draw, &threads[i]) == 0);
    }
    for (int i = 0; i < THREADS; i++) {
        CHECK(pthread_join(ids[i], NULL) == 0);
    }
    for (int i = 0; i < THREADS; i++) {
        draw(&serial[i]);
        CHECK(threads[i].sum == serial[i].sum);
        CHECK(threads[i].last == serial[i].last);
        CHECK(memcmp(threads[i].r.s, serial[i].r.s, sizeof(r.s)) == 0);
        for (int j = 0; j < i; j++) {
            CHECK(threads[i].sum != threads[j].sum);
        }
    }
    CHECK(sof_random_free(&r) == SOF_RANDOM_OK);
}

int main(void) {
    printf("sof_random:\n");
    run("splitmix64", test_splitmix64);
    run("xoshiro256**", test_next);
    run("init", test_init);
    run("seeds and copies", test_seeds);
    run("every bit random", test_bits);
    run("below", test_below);
    run("range", test_range);
    run("double", test_double);
    run("exponential", test_exponential);
    run("exponential distribution", test_exponential_distribution);
    run("bytes", test_bytes);
    run("shuffle", test_shuffle);
    run("jump", test_jump);
    run("NULL arguments", test_null);
    run("freed generator", test_freed);
    run("separate generators on threads", test_threads);
    return test_report();
}
