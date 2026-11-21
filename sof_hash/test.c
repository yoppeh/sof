/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_hash.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_hash.h"
#include "test.h"

/// @brief Threads, the hashes each computes in a pass, the passes, and the
/// bytes of data shared by the threads, in the threaded test.
#define THREADS 8
#define CASES 4096
#define PASSES 8
#define DATA_SIZE (1 << 16)

/// @brief Fills buf with the pattern the known values were computed from.
static void fill_pattern(unsigned char *buf, size_t length) {
    for (size_t i = 0; i < length; i++) {
        buf[i] = (unsigned char)(i * 7 + 3);
    }
}

static int compare_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

/// @brief Returns 1 if the n values in v are all different. Sorts v.
static int all_distinct(uint64_t *v, size_t n) {
    qsort(v, n, sizeof(uint64_t), compare_u64);
    for (size_t i = 1; i < n; i++) {
        if (v[i] == v[i - 1]) {
            return 0;
        }
    }
    return 1;
}

static int popcount64(uint64_t x) {
    int n = 0;
    while (x != 0) {
        x &= x - 1;
        n++;
    }
    return n;
}

static void test_known_values(void) {
    // These pin the results, which must not change between releases or
    // platforms; they were checked on arm64 and x86-64. Lengths 3, 8, 16, 17,
    // 48, 100, 112 and 1000 cover each of rapidhash's code paths.
    static const struct {
        size_t length;
        uint64_t hash;
    } pattern[] = {
        {3, 0x5655e9f764e8fb47ULL},
        {8, 0xf5980b646f822e89ULL},
        {16, 0xd0e68844cb24a480ULL},
        {17, 0x7998c836066e171bULL},
        {48, 0x78e892266d502576ULL},
        {100, 0xb91fffc22dd03722ULL},
        {112, 0xbde27e659c073097ULL},
        {1000, 0x871e919aec95c844ULL},
    };
    unsigned char buf[1000];
    fill_pattern(buf, sizeof(buf));
    CHECK(sof_hash_bytes("", 0, 0) == 0x0338dc4be2cecdaeULL);
    CHECK(sof_hash_string("a", 0) == 0x599f47df33a2e1ebULL);
    CHECK(sof_hash_string("abc", 0) == 0xcb475beafa9c0da2ULL);
    CHECK(sof_hash_string("hello, world", 42) == 0x7df5bd51422cf31bULL);
    for (size_t i = 0; i < sizeof(pattern) / sizeof(pattern[0]); i++) {
        CHECK(sof_hash_bytes(buf, pattern[i].length, 0) == pattern[i].hash);
    }
    CHECK(sof_hash_mix(0) == 0);
    CHECK(sof_hash_mix(1) == 0xb456bcfc34c2cb2cULL);
    CHECK(sof_hash_mix(UINT64_MAX) == 0x64b5720b4b825f21ULL);
}

static void test_string_matches_bytes(void) {
    static const char long_string[] = "a string that is long enough to take rapidhash's longer paths, which start at 17, 49 and 113 bytes; this one is longer than all";
    static const char *strings[] = {
        "", "a", "ab", "abc", "word", "hashmap", "sixteen bytes!!!", long_string,
    };
    for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++) {
        const char *s = strings[i];
        CHECK(sof_hash_string(s, 0) == sof_hash_bytes(s, strlen(s), 0));
        CHECK(sof_hash_string(s, 99) == sof_hash_bytes(s, strlen(s), 99));
    }
}

static void test_alignment(void) {
    // The same bytes at every alignment hash alike.
    unsigned char source[300], buf[300 + 8];
    fill_pattern(source, sizeof(source));
    for (size_t length = 0; length <= sizeof(source); length += 13) {
        uint64_t expected = sof_hash_bytes(source, length, 0);
        for (size_t offset = 0; offset < 8; offset++) {
            memset(buf, 0xa5, sizeof(buf));
            memcpy(buf + offset, source, length);
            CHECK(sof_hash_bytes(buf + offset, length, 0) == expected);
        }
    }
}

static void test_length_matters(void) {
    // Runs of zeros of every length from 0 to 1024 bytes all hash
    // differently, so trailing zeros aren't lost.
    enum { MAX = 1024 };
    static unsigned char zeros[MAX];
    uint64_t hashes[MAX + 1];
    for (size_t length = 0; length <= MAX; length++) {
        hashes[length] = sof_hash_bytes(zeros, length, 0);
    }
    CHECK(all_distinct(hashes, MAX + 1));
}

static void test_every_bit_matters(void) {
    // Flipping any single bit of the input changes the hash, at every length
    // up to 160 bytes (past each of rapidhash's code paths).
    unsigned char buf[160];
    for (size_t length = 1; length <= sizeof(buf); length++) {
        fill_pattern(buf, length);
        uint64_t original = sof_hash_bytes(buf, length, 0);
        for (size_t bit = 0; bit < length * 8; bit++) {
            buf[bit / 8] ^= (unsigned char)(1u << (bit % 8));
            uint64_t flipped = sof_hash_bytes(buf, length, 0);
            buf[bit / 8] ^= (unsigned char)(1u << (bit % 8));
            CHECK(flipped != original);
        }
    }
}

static void test_avalanche(void) {
    // Flipping one input bit flips each output bit with probability close to
    // one half, so 32 of the 64 on average.
    uint64_t state = 1;
    unsigned char buf[64];
    size_t lengths[] = {4, 8, 13, 16, 32, 64};
    for (size_t l = 0; l < sizeof(lengths) / sizeof(lengths[0]); l++) {
        size_t length = lengths[l];
        long flips = 0, trials = 0;
        for (int round = 0; round < 2000; round++) {
            for (size_t i = 0; i < length; i++) {
                buf[i] = (unsigned char)next_random(&state);
            }
            uint64_t original = sof_hash_bytes(buf, length, 0);
            size_t bit = next_random(&state) % (length * 8);
            buf[bit / 8] ^= (unsigned char)(1u << (bit % 8));
            flips += popcount64(original ^ sof_hash_bytes(buf, length, 0));
            trials++;
        }
        double mean = (double)flips / trials;
        CHECK(mean > 31.0 && mean < 33.0);
    }
}

static void test_seed(void) {
    // Different seeds give different hashes of the same data.
    enum { SEEDS = 1000 };
    uint64_t hashes[SEEDS];
    for (uint64_t seed = 0; seed < SEEDS; seed++) {
        hashes[seed] = sof_hash_string("seeded", seed);
    }
    CHECK(all_distinct(hashes, SEEDS));
    CHECK(sof_hash_string("seeded", 0) == sof_hash_bytes("seeded", 6, 0));
}

static void test_mix(void) {
    // Sequential values and aligned addresses give distinct results whose low
    // bits are spread evenly, which is what a hash table indexes by.
    enum { COUNT = 1 << 16, BUCKETS = 256 };
    static uint64_t mixed[COUNT];
    size_t buckets[BUCKETS];
    for (uint64_t stride = 1; stride <= 64; stride *= 4) {
        memset(buckets, 0, sizeof(buckets));
        for (uint64_t i = 0; i < COUNT; i++) {
            mixed[i] = sof_hash_mix(0x100000000ULL + i * stride);
            buckets[mixed[i] % BUCKETS]++;
        }
        for (size_t b = 0; b < BUCKETS; b++) {
            // 256 expected per bucket.
            CHECK(buckets[b] > 160 && buckets[b] < 352);
        }
        CHECK(all_distinct(mixed, COUNT));
    }
}

/// @brief For stream_hash(): pieces of random sizes up to 24 bytes, so that
/// most fit in the stream's buffer and some end it exactly.
#define SMALL_PIECES ((size_t)-1)

/// @brief Returns the stream hash of length bytes at data, with seed, given
/// in pieces of piece bytes, or of random sizes up to 300 if piece is 0, or
/// up to 24 if it is SMALL_PIECES.
static uint64_t stream_hash(const unsigned char *data, size_t length, uint64_t seed, size_t piece, uint64_t *random) {
    sof_hash_stream_t stream;
    sof_hash_stream_init(&stream, seed);
    size_t done = 0;
    while (done < length) {
        size_t n = piece == 0 ? next_random(random) % 301 : piece == SMALL_PIECES ? next_random(random) % 25 : piece;
        if (n > length - done) {
            n = length - done;
        }
        sof_hash_stream_update(&stream, data + done, n);
        done += n;
    }
    return stream.length == length ? sof_hash_stream_final(&stream) : 0;
}

static void test_stream(void) {
    // Every length up to 1000, past several blocks, split every way, gives
    // sof_hash_bytes()'s hash.
    static unsigned char buf[1000];
    uint64_t random = 0x2545f4914f6cdd1dULL;
    for (size_t i = 0; i < sizeof(buf); i++) {
        buf[i] = (unsigned char)next_random(&random);
    }
    static const size_t pieces[] = {1, 2, 7, 8, 15, 16, 17, 56, 111, 112, 113, 224, 1000, 0, SMALL_PIECES, SMALL_PIECES};
    for (size_t length = 0; length <= sizeof(buf); length++) {
        uint64_t seed = length % 3 == 0 ? 0 : length * 0x9e3779b97f4a7c15ULL;
        uint64_t expected = sof_hash_bytes(buf, length, seed);
        for (size_t p = 0; p < sizeof(pieces) / sizeof(pieces[0]); p++) {
            CHECK(stream_hash(buf, length, seed, pieces[p], &random) == expected);
        }
    }
    // A large input in random pieces, and in one.
    enum { LARGE = 1 << 20 };
    static unsigned char large[LARGE];
    for (size_t i = 0; i < LARGE; i++) {
        large[i] = (unsigned char)next_random(&random);
    }
    uint64_t expected = sof_hash_bytes(large, LARGE, 5);
    CHECK(stream_hash(large, LARGE, 5, 0, &random) == expected);
    CHECK(stream_hash(large, LARGE, 5, LARGE, &random) == expected);
    CHECK(stream_hash(large, LARGE, 5, 4096, &random) == expected);
    CHECK(stream_hash(large, LARGE, 5, SMALL_PIECES, &random) == expected);

    // The hash so far can be taken at any point, and the stream goes on.
    sof_hash_stream_t stream;
    sof_hash_stream_init(&stream, 0);
    CHECK(sof_hash_stream_final(&stream) == sof_hash_bytes("", 0, 0));
    for (size_t length = 1; length <= 500; length++) {
        sof_hash_stream_update(&stream, buf + length - 1, 1);
        CHECK(sof_hash_stream_final(&stream) == sof_hash_bytes(buf, length, 0));
    }
    // Empty and NULL pieces add nothing, and the known values hold.
    sof_hash_stream_init(&stream, 42);
    sof_hash_stream_update(&stream, "hello, ", 7);
    sof_hash_stream_update(&stream, "", 0);
    sof_hash_stream_update(&stream, NULL, 100);
    sof_hash_stream_update(&stream, "world", 5);
    CHECK(stream.length == 12);
    CHECK(sof_hash_stream_final(&stream) == 0x7df5bd51422cf31bULL);
    // A NULL stream does nothing, and hashes as no data.
    sof_hash_stream_init(NULL, 1);
    sof_hash_stream_update(NULL, buf, 10);
    CHECK(sof_hash_stream_final(NULL) == sof_hash_bytes(NULL, 0, 0));
}

static void test_null(void) {
    CHECK(sof_hash_bytes(NULL, 0, 0) == sof_hash_bytes("", 0, 0));
    CHECK(sof_hash_bytes(NULL, 100, 7) == sof_hash_bytes("", 0, 7));
    CHECK(sof_hash_string(NULL, 0) == sof_hash_string("", 0));
    CHECK(sof_hash_string(NULL, 7) == sof_hash_string("", 7));
}

/// @brief One hash to compute: of length bytes at offset in the shared data
/// with seed, of the string there, and the mix of seed.
typedef struct hash_case_t {
    size_t offset;
    size_t length;
    uint64_t seed;
    /// @brief The results, computed serially before the threads start.
    uint64_t bytes;
    uint64_t string;
    uint64_t mix;
} hash_case_t;

/// @brief The data and cases every thread reads.
typedef struct shared_t {
    unsigned char data[DATA_SIZE];
    hash_case_t cases[CASES];
} shared_t;

/// @brief One thread's results, checked by the main thread after joining,
/// since CHECK isn't safe on other threads.
typedef struct worker_t {
    const shared_t *shared;
    size_t first;
    size_t hashes;
    size_t mismatches;
} worker_t;

/// @brief Computes every case PASSES times, starting at a different case
/// from the other threads, and counts results that differ from the serial
/// ones.
static void *hash_cases(void *arg) {
    worker_t *w = arg;
    const shared_t *s = w->shared;
    for (size_t pass = 0; pass < PASSES; pass++) {
        for (size_t n = 0; n < CASES; n++) {
            const hash_case_t *c = &s->cases[(w->first + n) % CASES];
            const unsigned char *p = s->data + c->offset;
            if (sof_hash_bytes(p, c->length, c->seed) != c->bytes || sof_hash_string((const char *)p, c->seed) != c->string || sof_hash_mix(c->seed) != c->mix) {
                w->mismatches++;
            }
            w->hashes++;
        }
    }
    return NULL;
}

static void test_threads(void) {
    // Threads hash the same data at once and must get exactly the results
    // computed serially. The data has a NUL about every 64 bytes, so
    // sof_hash_string() sees strings of many lengths, and ends with one.
    static shared_t s;
    uint64_t random = 0x9e3779b97f4a7c15ULL;
    for (size_t i = 0; i < DATA_SIZE; i++) {
        uint64_t r = next_random(&random);
        s.data[i] = r % 64 == 0 ? 0 : (unsigned char)(r >> 8 | 1);
    }
    s.data[DATA_SIZE - 1] = 0;
    for (size_t i = 0; i < CASES; i++) {
        hash_case_t *c = &s.cases[i];
        c->offset = next_random(&random) % DATA_SIZE;
        size_t room = DATA_SIZE - c->offset;
        // Mostly short keys, with some up to 4 KiB.
        size_t most = i % 8 == 0 ? 4096 : 128;
        c->length = next_random(&random) % (most + 1);
        if (c->length > room) {
            c->length = room;
        }
        c->seed = i % 4 == 0 ? 0 : next_random(&random);
        c->bytes = sof_hash_bytes(s.data + c->offset, c->length, c->seed);
        c->string = sof_hash_string((const char *)s.data + c->offset, c->seed);
        c->mix = sof_hash_mix(c->seed);
    }
    worker_t workers[THREADS] = {{0}};
    pthread_t threads[THREADS];
    for (size_t i = 0; i < THREADS; i++) {
        workers[i].shared = &s;
        workers[i].first = i * (CASES / THREADS);
        CHECK(pthread_create(&threads[i], NULL, hash_cases, &workers[i]) == 0);
    }
    for (size_t i = 0; i < THREADS; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    for (size_t i = 0; i < THREADS; i++) {
        CHECK(workers[i].hashes == (size_t)CASES * PASSES);
        CHECK(workers[i].mismatches == 0);
    }
}

int main(void) {
    printf("sof_hash:\n");
    run("known values", test_known_values);
    run("sof_hash_string matches sof_hash_bytes", test_string_matches_bytes);
    run("alignment doesn't matter", test_alignment);
    run("length matters", test_length_matters);
    run("every bit matters", test_every_bit_matters);
    run("avalanche", test_avalanche);
    run("seeds", test_seed);
    run("sof_hash_mix spreads its input", test_mix);
    run("streams give sof_hash_bytes' hash, however split", test_stream);
    run("NULL arguments", test_null);
    run("threads hashing the same data", test_threads);
    return test_report();
}
