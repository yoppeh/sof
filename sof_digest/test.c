/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_digest.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it, and
 * test-portable, the same tests with sof_digest.c and this file compiled
 * with SOF_DIGEST_PORTABLE, so the portable code is tested on machines
 * whose instructions the usual build uses.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_digest.h"
#include "test.h"

/// @brief The characters in the longest digest written in hex, with its
/// NUL.
#define HEX_SIZE (2 * SOF_DIGEST_MAX_SIZE + 1)

/// @brief Threads, the digests each computes in a pass, the passes, and the
/// bytes of data shared by the threads, in the threaded test.
#define THREADS 8
#define CASES 256
#define PASSES 4
#define DATA_SIZE (1 << 14)

/// @brief CRC-32's and CRC-32C's polynomials, reflected, for the reference.
#define CRC32_POLY 0xedb88320u
#define CRC32C_POLY 0x82f63b78u

/// @brief The digests the tests run through each of their checks.
typedef enum algo_t {
    MD5,
    SHA1,
    SHA224,
    SHA256,
    SHA384,
    SHA512,
    SHA512_256,
    ALGOS,
} algo_t;

/// @brief The stream of any of the digests. A pointer to it is a pointer to
/// each of its members, and NULL stays NULL.
typedef union stream_t {
    sof_digest_md5_t md5;
    sof_digest_sha1_t sha1;
    sof_digest_sha256_t sha256;
    sof_digest_sha512_t sha512;
} stream_t;

/// @brief A digest known from another implementation: of count repeats of
/// text.
typedef struct known_t {
    algo_t algo;
    const char *text;
    size_t count;
    const char *digest;
} known_t;

/// @brief A CRC-32 and CRC-32C known from other implementations.
typedef struct known_crc_t {
    const char *text;
    uint32_t crc32;
    uint32_t crc32c;
} known_crc_t;

/// @brief The data one thread digests, and what it found.
typedef struct worker_t {
    const unsigned char *data;
    const unsigned char (*expected)[SOF_DIGEST_MAX_SIZE];
    int mismatches;
} worker_t;

/// @brief Each digest's name and size.
static const char *const algo_names[ALGOS] = {"MD5", "SHA-1", "SHA-224", "SHA-256", "SHA-384", "SHA-512", "SHA-512/256"};
static const size_t algo_sizes[ALGOS] = {
    SOF_DIGEST_MD5_SIZE,    SOF_DIGEST_SHA1_SIZE,   SOF_DIGEST_SHA224_SIZE,    SOF_DIGEST_SHA256_SIZE,
    SOF_DIGEST_SHA384_SIZE, SOF_DIGEST_SHA512_SIZE, SOF_DIGEST_SHA512_256_SIZE,
};

/// @brief Gives the digest of length bytes at data with the library's
/// one-shot function.
static void digest(algo_t algo, const void *data, size_t length, unsigned char *out) {
    switch (algo) {
    case MD5:
        sof_digest_md5(data, length, out);
        break;
    case SHA1:
        sof_digest_sha1(data, length, out);
        break;
    case SHA224:
        sof_digest_sha224(data, length, out);
        break;
    case SHA256:
        sof_digest_sha256(data, length, out);
        break;
    case SHA384:
        sof_digest_sha384(data, length, out);
        break;
    case SHA512:
        sof_digest_sha512(data, length, out);
        break;
    default:
        sof_digest_sha512_256(data, length, out);
        break;
    }
}

/// @brief Sets up a stream for algo.
static void stream_init(algo_t algo, stream_t *s) {
    switch (algo) {
    case MD5:
        sof_digest_md5_init((sof_digest_md5_t *)s);
        break;
    case SHA1:
        sof_digest_sha1_init((sof_digest_sha1_t *)s);
        break;
    case SHA224:
        sof_digest_sha224_init((sof_digest_sha256_t *)s);
        break;
    case SHA256:
        sof_digest_sha256_init((sof_digest_sha256_t *)s);
        break;
    case SHA384:
        sof_digest_sha384_init((sof_digest_sha512_t *)s);
        break;
    case SHA512:
        sof_digest_sha512_init((sof_digest_sha512_t *)s);
        break;
    default:
        sof_digest_sha512_256_init((sof_digest_sha512_t *)s);
        break;
    }
}

/// @brief Gives a stream for algo the next piece of its data.
static void stream_update(algo_t algo, stream_t *s, const void *data, size_t length) {
    switch (algo) {
    case MD5:
        sof_digest_md5_update((sof_digest_md5_t *)s, data, length);
        break;
    case SHA1:
        sof_digest_sha1_update((sof_digest_sha1_t *)s, data, length);
        break;
    case SHA224:
        sof_digest_sha224_update((sof_digest_sha256_t *)s, data, length);
        break;
    case SHA256:
        sof_digest_sha256_update((sof_digest_sha256_t *)s, data, length);
        break;
    case SHA384:
        sof_digest_sha384_update((sof_digest_sha512_t *)s, data, length);
        break;
    case SHA512:
        sof_digest_sha512_update((sof_digest_sha512_t *)s, data, length);
        break;
    default:
        sof_digest_sha512_256_update((sof_digest_sha512_t *)s, data, length);
        break;
    }
}

/// @brief Gives the digest of a stream for algo, or of NULL.
static void stream_final(algo_t algo, const stream_t *s, unsigned char *out) {
    switch (algo) {
    case MD5:
        sof_digest_md5_final((const sof_digest_md5_t *)s, out);
        break;
    case SHA1:
        sof_digest_sha1_final((const sof_digest_sha1_t *)s, out);
        break;
    case SHA224:
        sof_digest_sha224_final((const sof_digest_sha256_t *)s, out);
        break;
    case SHA256:
        sof_digest_sha256_final((const sof_digest_sha256_t *)s, out);
        break;
    case SHA384:
        sof_digest_sha384_final((const sof_digest_sha512_t *)s, out);
        break;
    case SHA512:
        sof_digest_sha512_final((const sof_digest_sha512_t *)s, out);
        break;
    default:
        sof_digest_sha512_256_final((const sof_digest_sha512_t *)s, out);
        break;
    }
}

/// @brief The bytes given to a stream for algo so far.
static uint64_t stream_length(algo_t algo, const stream_t *s) {
    switch (algo) {
    case MD5:
        return s->md5.length;
    case SHA1:
        return s->sha1.length;
    case SHA224:
    case SHA256:
        return s->sha256.length;
    default:
        return s->sha512.length;
    }
}

/// @brief Rotates x right by n bits, 0 < n < 32, for the references.
static uint32_t ref_rotr(uint32_t x, unsigned n) {
    return x >> n | x << (32 - n);
}

/// @brief Rotates x right by n bits, 0 < n < 64, for the references.
static uint64_t ref_rotr64(uint64_t x, unsigned n) {
    return x >> n | x << (64 - n);
}

/// @brief Reads a big-endian 32-bit word, for the references.
static uint32_t ref_load32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

/// @brief Pads a message as MD5, SHA-1 and SHA-2 do, for the references: a
/// 1 bit, 0 bits, and the length in bits, in field bytes, to a whole number
/// of blocks of block bytes. The caller frees the result.
static unsigned char *ref_pad(const unsigned char *data, size_t length, size_t block, size_t field, int little_endian, size_t *padded) {
    *padded = (length + 1 + field + block - 1) / block * block;
    unsigned char *m = calloc(*padded, 1);
    if (m == NULL) {
        return NULL;
    }
    if (length > 0) {
        memcpy(m, data, length);
    }
    m[length] = 0x80;
    uint64_t bits = (uint64_t)length * 8;
    for (int i = 0; i < 8; i++) {
        size_t at = little_endian ? *padded - 8 + i : *padded - 1 - i;
        m[at] = (unsigned char)(bits >> 8 * i);
    }
    return m;
}

/// @brief The MD5 digest of length bytes at data, computed independently
/// of the library, as RFC 1321 describes it: a step at a time, with each
/// step's word, shift and constant from tables.
static void ref_md5(const unsigned char *data, size_t length, unsigned char *out) {
    static const uint32_t t[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static const unsigned s[4][4] = {{7, 12, 17, 22}, {5, 9, 14, 20}, {4, 11, 16, 23}, {6, 10, 15, 21}};
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    size_t padded;
    unsigned char *m = ref_pad(data, length, 64, 8, 1, &padded);
    if (m == NULL) {
        memset(out, 0, 16);
        return;
    }
    for (size_t at = 0; at < padded; at += 64) {
        uint32_t x[16];
        for (int i = 0; i < 16; i++) {
            const unsigned char *p = m + at + 4 * i;
            x[i] = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = 7 * i % 16;
            }
            uint32_t sum = a + f + t[i] + x[g];
            unsigned shift = s[i / 16][i % 4];
            a = d;
            d = c;
            c = b;
            b = b + (sum << shift | sum >> (32 - shift));
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
    }
    free(m);
    for (int i = 0; i < 16; i++) {
        out[i] = (unsigned char)(h[i / 4] >> 8 * (i % 4));
    }
}

/// @brief The SHA-1 digest of length bytes at data, computed independently
/// of the library: an 80-word schedule and a round at a time.
static void ref_sha1(const unsigned char *data, size_t length, unsigned char *out) {
    uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    size_t padded;
    unsigned char *m = ref_pad(data, length, 64, 8, 0, &padded);
    if (m == NULL) {
        memset(out, 0, 20);
        return;
    }
    for (size_t at = 0; at < padded; at += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = ref_load32(m + at + 4 * i);
        }
        for (int i = 16; i < 80; i++) {
            uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = x << 1 | x >> 31;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5a827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdc;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6;
            }
            uint32_t temp = (a << 5 | a >> 27) + f + e + k + w[i];
            e = d;
            d = c;
            c = b << 30 | b >> 2;
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    free(m);
    for (int i = 0; i < 20; i++) {
        out[i] = (unsigned char)(h[i / 4] >> (24 - 8 * (i % 4)));
    }
}

/// @brief The SHA-256 digest, or SHA-224's with sha224 set, of length bytes
/// at data, computed independently of the library: FIPS 180-4's algorithm
/// written as plainly as it can be, a 64-word schedule and a round at a
/// time.
static void ref_sha256(const unsigned char *data, size_t length, unsigned char *out, int sha224) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    static const uint32_t initial256[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    static const uint32_t initial224[8] = {
        0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939, 0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4,
    };
    uint32_t state[8];
    memcpy(state, sha224 ? initial224 : initial256, sizeof(state));
    size_t size = sha224 ? 28 : 32, padded;
    unsigned char *m = ref_pad(data, length, 64, 8, 0, &padded);
    if (m == NULL) {
        memset(out, 0, size);
        return;
    }
    for (size_t at = 0; at < padded; at += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = ref_load32(m + at + 4 * i);
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ref_rotr(w[i - 15], 7) ^ ref_rotr(w[i - 15], 18) ^ w[i - 15] >> 3;
            uint32_t s1 = ref_rotr(w[i - 2], 17) ^ ref_rotr(w[i - 2], 19) ^ w[i - 2] >> 10;
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t v[8];
        memcpy(v, state, sizeof(v));
        for (int i = 0; i < 64; i++) {
            uint32_t s1 = ref_rotr(v[4], 6) ^ ref_rotr(v[4], 11) ^ ref_rotr(v[4], 25);
            uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            uint32_t t1 = v[7] + s1 + ch + k[i] + w[i];
            uint32_t s0 = ref_rotr(v[0], 2) ^ ref_rotr(v[0], 13) ^ ref_rotr(v[0], 22);
            uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            memmove(v + 1, v, 7 * sizeof(v[0]));
            v[4] += t1;
            v[0] = t1 + s0 + maj;
        }
        for (int i = 0; i < 8; i++) {
            state[i] += v[i];
        }
    }
    free(m);
    for (size_t i = 0; i < size; i++) {
        out[i] = (unsigned char)(state[i / 4] >> (24 - 8 * (i % 4)));
    }
}

/// @brief The SHA-512 digest of length bytes at data, or SHA-384's or
/// SHA-512/256's, as algo says, computed independently of the library: an
/// 80-word schedule and a round at a time.
static void ref_sha512(const unsigned char *data, size_t length, unsigned char *out, algo_t algo) {
    static const uint64_t k[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
        0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
    };
    static const uint64_t initial[3][8] = {
        // SHA-384, SHA-512 and SHA-512/256.
        {0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
         0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL},
        {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
         0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL},
        {0x22312194fc2bf72cULL, 0x9f555fa3c84c64c2ULL, 0x2393b86b6f53b151ULL, 0x963877195940eabdULL,
         0x96283ee2a88effe3ULL, 0xbe5e1e2553863992ULL, 0x2b0199fc2c85b8aaULL, 0x0eb72ddc81c52ca2ULL},
    };
    uint64_t state[8];
    memcpy(state, initial[algo - SHA384], sizeof(state));
    size_t size = algo_sizes[algo], padded;
    unsigned char *m = ref_pad(data, length, 128, 16, 0, &padded);
    if (m == NULL) {
        memset(out, 0, size);
        return;
    }
    for (size_t at = 0; at < padded; at += 128) {
        uint64_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint64_t)ref_load32(m + at + 8 * i) << 32 | ref_load32(m + at + 8 * i + 4);
        }
        for (int i = 16; i < 80; i++) {
            uint64_t s0 = ref_rotr64(w[i - 15], 1) ^ ref_rotr64(w[i - 15], 8) ^ w[i - 15] >> 7;
            uint64_t s1 = ref_rotr64(w[i - 2], 19) ^ ref_rotr64(w[i - 2], 61) ^ w[i - 2] >> 6;
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t v[8];
        memcpy(v, state, sizeof(v));
        for (int i = 0; i < 80; i++) {
            uint64_t s1 = ref_rotr64(v[4], 14) ^ ref_rotr64(v[4], 18) ^ ref_rotr64(v[4], 41);
            uint64_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            uint64_t t1 = v[7] + s1 + ch + k[i] + w[i];
            uint64_t s0 = ref_rotr64(v[0], 28) ^ ref_rotr64(v[0], 34) ^ ref_rotr64(v[0], 39);
            uint64_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            memmove(v + 1, v, 7 * sizeof(v[0]));
            v[4] += t1;
            v[0] = t1 + s0 + maj;
        }
        for (int i = 0; i < 8; i++) {
            state[i] += v[i];
        }
    }
    free(m);
    for (size_t i = 0; i < size; i++) {
        out[i] = (unsigned char)(state[i / 8] >> (56 - 8 * (i % 8)));
    }
}

/// @brief The digest of length bytes at data with algo, from the
/// references.
static void ref_digest(algo_t algo, const unsigned char *data, size_t length, unsigned char *out) {
    switch (algo) {
    case MD5:
        ref_md5(data, length, out);
        break;
    case SHA1:
        ref_sha1(data, length, out);
        break;
    case SHA224:
    case SHA256:
        ref_sha256(data, length, out, algo == SHA224);
        break;
    default:
        ref_sha512(data, length, out, algo);
        break;
    }
}

/// @brief A CRC of length bytes at data, computed a bit at a time with the
/// reflected polynomial poly, independently of the library.
static uint32_t ref_crc(uint32_t poly, const unsigned char *data, size_t length) {
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = crc & 1 ? crc >> 1 ^ poly : crc >> 1;
        }
    }
    return ~crc;
}

/// @brief Writes the hex digest of length bytes at data into hex.
static const char *hex_digest(algo_t algo, const void *data, size_t length, char *hex) {
    unsigned char out[SOF_DIGEST_MAX_SIZE];
    digest(algo, data, length, out);
    return sof_digest_to_hex(out, algo_sizes[algo], hex);
}

/// @brief Writes the hex digest of a stream into hex.
static const char *hex_final(algo_t algo, const stream_t *s, char *hex) {
    unsigned char out[SOF_DIGEST_MAX_SIZE];
    stream_final(algo, s, out);
    return sof_digest_to_hex(out, algo_sizes[algo], hex);
}

/// @brief Fills length bytes at buf with random bytes from state.
static void fill_random(unsigned char *buf, size_t length, uint64_t *state) {
    for (size_t i = 0; i < length; i++) {
        buf[i] = (unsigned char)(next_random(state) >> 56);
    }
}

static void test_known_values(void) {
    // From FIPS 180-4's examples, NIST's test vectors and RFC 1321's test
    // suite, and for the lengths around the block and padding boundaries,
    // from Python's hashlib (OpenSSL) and sha256sum.
    static const known_t known[] = {
        {SHA256, "", 1, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {SHA256, "abc", 1, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {SHA256, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1,
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {SHA256, "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 1,
         "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
        {SHA256, "The quick brown fox jumps over the lazy dog", 1,
         "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"},
        {SHA256, "a", 1, "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"},
        {SHA256, "a", 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {SHA256, "a", 56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {SHA256, "a", 57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6"},
        {SHA256, "a", 63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {SHA256, "a", 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {SHA256, "a", 65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {SHA256, "a", 111, "6374f73208854473827f6f6a3f43b1f53eaa3b82c21c1a6d69a2110b2a79baad"},
        {SHA256, "a", 112, "f54353008a2553262ecdc4a34749563ba0950e8b0fc8652780b0a614b99683c1"},
        {SHA256, "a", 119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
        {SHA256, "a", 120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
        {SHA256, "a", 127, "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a"},
        {SHA256, "a", 128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
        {SHA256, "a", 129, "c12cb024a2e5551cca0e08fce8f1c5e314555cc3fef6329ee994a3db752166ae"},
        {SHA256, "a", 1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"},
        {SHA256, "a", 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
        {MD5, "", 1, "d41d8cd98f00b204e9800998ecf8427e"},
        {MD5, "abc", 1, "900150983cd24fb0d6963f7d28e17f72"},
        {MD5, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1, "8215ef0796a20bcaaae116d3876c664a"},
        {MD5, "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 1, "03dd8807a93175fb062dfb55dc7d359c"},
        {MD5, "The quick brown fox jumps over the lazy dog", 1, "9e107d9d372bb6826bd81d3542a419d6"},
        {MD5, "a", 1000000, "7707d6ae4e027c70eea2a935c2296f21"},
        {MD5, "a", 1, "0cc175b9c0f1b6a831c399e269772661"},
        {MD5, "message digest", 1, "f96b697d7cb7938d525a2f31aaf161d0"},
        {MD5, "abcdefghijklmnopqrstuvwxyz", 1, "c3fcd3d76192e4007dfb496cca67e13b"},
        {MD5, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 1, "d174ab98d277d9f5a5611c2c9f419d9f"},
        {MD5, "1234567890", 8, "57edf4a22be3c955ac49da2e2107b67a"},
        {MD5, "a", 55, "ef1772b6dff9a122358552954ad0df65"},
        {MD5, "a", 56, "3b0c8ac703f828b04c6c197006d17218"},
        {MD5, "a", 63, "b06521f39153d618550606be297466d5"},
        {MD5, "a", 64, "014842d480b571495a4a0363793f7367"},
        {MD5, "a", 65, "c743a45e0d2e6a95cb859adae0248435"},
        {MD5, "a", 119, "8a7bd0732ed6a28ce75f6dabc90e1613"},
        {MD5, "a", 120, "5f61c0ccad4cac44c75ff505e1f1e537"},
        {SHA1, "", 1, "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
        {SHA1, "abc", 1, "a9993e364706816aba3e25717850c26c9cd0d89d"},
        {SHA1, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"},
        {SHA1, "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 1, "a49b2446a02c645bf419f995b67091253a04a259"},
        {SHA1, "The quick brown fox jumps over the lazy dog", 1, "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12"},
        {SHA1, "a", 1000000, "34aa973cd4c4daa4f61eeb2bdbad27316534016f"},
        {SHA1, "a", 55, "c1c8bbdc22796e28c0e15163d20899b65621d65a"},
        {SHA1, "a", 56, "c2db330f6083854c99d4b5bfb6e8f29f201be699"},
        {SHA1, "a", 63, "03f09f5b158a7a8cdad920bddc29b81c18a551f5"},
        {SHA1, "a", 64, "0098ba824b5c16427bd7a1122a5a442a25ec644d"},
        {SHA1, "a", 65, "11655326c708d70319be2610e8a57d9a5b959d3b"},
        {SHA1, "a", 119, "ee971065aaa017e0632a8ca6c77bb3bf8b1dfc56"},
        {SHA1, "a", 120, "f34c1488385346a55709ba056ddd08280dd4c6d6"},
        {SHA224, "", 1, "d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f"},
        {SHA224, "abc", 1, "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7"},
        {SHA224, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1, "75388b16512776cc5dba5da1fd890150b0c6455cb4f58b1952522525"},
        {SHA224, "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 1, "c97ca9a559850ce97a04a96def6d99a9e0e0e2ab14e6b8df265fc0b3"},
        {SHA224, "The quick brown fox jumps over the lazy dog", 1, "730e109bd7a8a32b1cb9d9a09aa2325d2430587ddbc0c38bad911525"},
        {SHA224, "a", 1000000, "20794655980c91d8bbb4c1ea97618a4bf03f42581948b2ee4ee7ad67"},
        {SHA384, "", 1, "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b"},
        {SHA384, "abc", 1, "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"},
        {SHA384, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1, "3391fdddfc8dc7393707a65b1b4709397cf8b1d162af05abfe8f450de5f36bc6b0455a8520bc4e6f5fe95b1fe3c8452b"},
        {SHA384, "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 1, "09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712fcc7c71a557e2db966c3e9fa91746039"},
        {SHA384, "The quick brown fox jumps over the lazy dog", 1, "ca737f1014a48f4c0b6dd43cb177b0afd9e5169367544c494011e3317dbf9a509cb1e5dc1e85a941bbee3d7f2afbc9b1"},
        {SHA384, "a", 1000000, "9d0e1809716474cb086e834e310a4a1ced149e9c00f248527972cec5704c2a5b07b8b3dc38ecc4ebae97ddd87f3d8985"},
        {SHA384, "a", 111, "3c37955051cb5c3026f94d551d5b5e2ac38d572ae4e07172085fed81f8466b8f90dc23a8ffcdea0b8d8e58e8fdacc80a"},
        {SHA384, "a", 112, "187d4e07cb306103c69967bf544d0dfbe9042577599c73c330abc0cb64c61236d5ed565ee19119d8c31779a38f791fcd"},
        {SHA384, "a", 113, "1d6bed01626682961b50da078a6b1da707c1da0c8a0a3226f159235bd45ed724a0622fa6f39fd70007a6c72a5cda43ae"},
        {SHA384, "a", 127, "9bd06b1763c2cf7aef40e795dc65bc96d59c41b537f3ad72ebdefd485476b5717c1aeb37c327fe9c1831b12b9efd08ae"},
        {SHA384, "a", 128, "edb12730a366098b3b2beac75a3bef1b0969b15c48e2163c23d96994f8d1bef760c7e27f3c464d3829f56c0d53808b0b"},
        {SHA384, "a", 129, "39b6f5a7b0e781dbc419f72e49b30eaac10f2c98c4403bc610da31067fd1b48f324138c8615d2b496d08d73d5e865326"},
        {SHA384, "a", 239, "e247c35f4bc1aa38026f8880c8c97305545d00d3f859e00c57d1c1f0a176b3c6b749c4eb081f08bd0fba500969cd056a"},
        {SHA384, "a", 240, "4d86957beab348a29180f02d02564ac1d32f5b4c217ece2b038f7c184f0cafc8c8e438eb82aa03796170e0a7ce8c0675"},
        {SHA384, "a", 255, "c4f7cca9698ddde4fb9947aebd9da1bda11e9413958d1a5d26f32fbec24bf34cb95f7c7eb84445cbaf84a63d3c705aa6"},
        {SHA384, "a", 256, "ee89d91a5f594f72052c561e5c2458280439eaa77cc1352e27893931c6d9ce5d869fb8a024358c460adc1af9f4fe5b4a"},
        {SHA384, "a", 257, "2b18aee790b03d49f71d4c036bdc8dc3216660f29f92978768f7b7844a36986a400b98186a294af656dc54985dd77e87"},
        {SHA512, "", 1, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"},
        {SHA512, "abc", 1, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"},
        {SHA512, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1, "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c33596fd15c13b1b07f9aa1d3bea57789ca031ad85c7a71dd70354ec631238ca3445"},
        {SHA512, "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 1, "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909"},
        {SHA512, "The quick brown fox jumps over the lazy dog", 1, "07e547d9586f6a73f73fbac0435ed76951218fb7d0c8d788a309d785436bbb642e93a252a954f23912547d1e8a3b5ed6e1bfd7097821233fa0538f3db854fee6"},
        {SHA512, "a", 1000000, "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b"},
        {SHA512, "a", 111, "fa9121c7b32b9e01733d034cfc78cbf67f926c7ed83e82200ef86818196921760b4beff48404df811b953828274461673c68d04e297b0eb7b2b4d60fc6b566a2"},
        {SHA512, "a", 112, "c01d080efd492776a1c43bd23dd99d0a2e626d481e16782e75d54c2503b5dc32bd05f0f1ba33e568b88fd2d970929b719ecbb152f58f130a407c8830604b70ca"},
        {SHA512, "a", 113, "55ddd8ac210a6e18ba1ee055af84c966e0dbff091c43580ae1be703bdb85da31acf6948cf5bd90c55a20e5450f22fb89bd8d0085e39f85a86cc46abbca75e24d"},
        {SHA512, "a", 127, "828613968b501dc00a97e08c73b118aa8876c26b8aac93df128502ab360f91bab50a51e088769a5c1eff4782ace147dce3642554199876374291f5d921629502"},
        {SHA512, "a", 128, "b73d1929aa615934e61a871596b3f3b33359f42b8175602e89f7e06e5f658a243667807ed300314b95cacdd579f3e33abdfbe351909519a846d465c59582f321"},
        {SHA512, "a", 129, "4f681e0bd53cda4b5a2041cc8a06f2eabde44fb16c951fbd5b87702f07aeab611565b19c47fde30587177ebb852e3971bbd8d3fd30da18d71037dfbd98420429"},
        {SHA512, "a", 239, "52c853cb8d907f3d4d6b889beb027985d7c273486d75f8baf26f80d24e90c74c6c3de3e22131582380a7d14d43f2941a31385439cd6ddc469f628015e50bf286"},
        {SHA512, "a", 240, "4c296d90c61052a62ffb1dd196f1b7b09373b1f93e71836baebf89690546b7595684dbe9467a8e484fa0d1094272b4344a7c24f5fee8daedeb0bf549c985ab5f"},
        {SHA512, "a", 255, "d8b5a659e365f704ab114ae7079a8da24fb9997b3052a4a63b37d654652bad6fbdd2b52d737e20a9d5ac3c5831d6afdd32ff737a3dd95269d2793bc2aa850aab"},
        {SHA512, "a", 256, "6a9169eb662f136d87374070e8828b3e615a7eca32a89446e9225b02832709be095e635c824a2bb70213ba2ea0ababac0809827843992c851903b7ac0c136699"},
        {SHA512, "a", 257, "17fa1d01865805f9e657c5f5088754d19913eb418577b03cd040b99e5e1354fd31d0d7f24b5474c62b49e3271860859510909685c5811eba23b06e1e3369899d"},
        {SHA512_256, "", 1, "c672b8d1ef56ed28ab87c3622c5114069bdd3ad7b8f9737498d0c01ecef0967a"},
        {SHA512_256, "abc", 1, "53048e2681941ef99b2e29b76b4c7dabe4c2d0c634fc6d46e0e2f13107e7af23"},
        {SHA512_256, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1, "bde8e1f9f19bb9fd3406c90ec6bc47bd36d8ada9f11880dbc8a22a7078b6a461"},
        {SHA512_256, "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 1, "3928e184fb8690f840da3988121d31be65cb9d3ef83ee6146feac861e19b563a"},
        {SHA512_256, "The quick brown fox jumps over the lazy dog", 1, "dd9d67b371519c339ed8dbd25af90e976a1eeefd4ad3d889005e532fc5bef04d"},
        {SHA512_256, "a", 1000000, "9a59a052930187a97038cae692f30708aa6491923ef5194394dc68d56c74fb21"},
        {SHA512_256, "a", 111, "0239e429f98d0ed61ee8e2a7c30afe98c1c3a80ce5dff62a107e9c538f7632ce"},
        {SHA512_256, "a", 112, "9216b5303edb66504570bee90e48ea5beaa5e9fe9f760bbd3e0460559fc005f6"},
        {SHA512_256, "a", 113, "d0ac79bea5bc97ac80c77c7cbbe92811ba9997eb0ce3c9888f9cef8362d187cd"},
        {SHA512_256, "a", 127, "2fe3b2a6ee7e12f6fe4ba82166541ad9b4ed882c493581cbe300d68f3757b778"},
        {SHA512_256, "a", 128, "b88f97e274f9c1d49f181c8cbd01a9c74930ad055a46ac4499a1d601f1c80bf2"},
        {SHA512_256, "a", 129, "fb9035c9009ed4a60e37510339ebdb1c771339f30aa581d5dea3690a524c23f1"},
        {SHA512_256, "a", 239, "78d0a1b37aaad84c89fff13cbe3cd3d1025bcdb648268f9102b7e7032bea7d2a"},
        {SHA512_256, "a", 240, "d48a4d53397b38ab4e771d781c98ac6b86712dff2a664cfd1f27c7ca40f8ce37"},
        {SHA512_256, "a", 255, "3c07343720a65d00043af3a6514586dde8602a68b7cebde6bd8ffa48ba37afef"},
        {SHA512_256, "a", 256, "d43f85191c3058fd3b2383077f8e1aa800aae7fdf6eb829440fa45f189562c07"},
        {SHA512_256, "a", 257, "88fe9d7dcf7e09156c967a23affc3c70e11130f8d73bcd5dac8f03b72f0c9974"},
    };
    // Every byte value, from Python's hashlib.
    static const char *const all_bytes[ALGOS] = {
        "e2c865db4162bed963bfaa9ef6ac18f0",
        "4916d6bdb7f78e6803698cab32d1586ea457dfc8",
        "88702e63237824c4eb0d0fcfe41469a462493e8beb2a75bbe5981734",
        "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880",
        "ffdaebff65ed05cf400f0221c4ccfb4b2104fb6a51f87e40be6c4309386bfdec2892e9179b34632331a59592737db5c5",
        "1e7b80bc8edc552c8feeb2780e111477e5bc70465fac1a77b29b35980c3f0ce4a036a6c9462036824bd56801e62af7e9feba5c22ed8a5af877bf7de117dcac6d",
        "0837a11d994d5aa860d06917a8a0f63e3111b95633deeb15eed9949376f37d36",
    };
    char hex[HEX_SIZE];
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        const known_t *k = &known[i];
        size_t piece = strlen(k->text);
        // The repeats are given to a stream one at a time, and the whole
        // text to the one-shot function too, when it isn't too long.
        stream_t s;
        stream_init(k->algo, &s);
        for (size_t j = 0; j < k->count; j++) {
            stream_update(k->algo, &s, k->text, piece);
        }
        CHECK(stream_length(k->algo, &s) == piece * k->count);
        CHECK(strcmp(hex_final(k->algo, &s, hex), k->digest) == 0);
        if (k->count == 1) {
            CHECK(strcmp(hex_digest(k->algo, k->text, piece, hex), k->digest) == 0);
        }
    }
    unsigned char all[256];
    for (int i = 0; i < 256; i++) {
        all[i] = (unsigned char)i;
    }
    for (int a = 0; a < ALGOS; a++) {
        CHECK(strcmp(hex_digest((algo_t)a, all, sizeof(all), hex), all_bytes[a]) == 0);
    }
}

static void test_reference(void) {
    enum { SIZE = 1000, BIG = 1 << 20, ROUNDS = 2 };
    uint64_t state = 0x7ef;
    unsigned char *data = malloc(BIG + 64);
    CHECK(data != NULL);
    fill_random(data, BIG + 64, &state);
    unsigned char want[SOF_DIGEST_MAX_SIZE], got[SOF_DIGEST_MAX_SIZE];
    int ok = 1;
    // Every length up to SIZE, at random offsets, whole and in random
    // pieces up to 300 bytes, so several blocks go to the compression at
    // once as well as one, and from the stream's buffer.
    for (int a = 0; ok && a < ALGOS; a++) {
        algo_t algo = (algo_t)a;
        size_t size = algo_sizes[algo];
        for (size_t length = 0; ok && length <= SIZE; length++) {
            for (int round = 0; ok && round < ROUNDS; round++) {
                const unsigned char *at = data + next_random(&state) % 64;
                ref_digest(algo, at, length, want);
                digest(algo, at, length, got);
                ok = memcmp(want, got, size) == 0;
                stream_t s;
                stream_init(algo, &s);
                for (size_t done = 0; done < length;) {
                    size_t n = (size_t)(next_random(&state) % 301);
                    if (n > length - done) {
                        n = length - done;
                    }
                    stream_update(algo, &s, at + done, n);
                    done += n;
                }
                stream_final(algo, &s, got);
                ok = ok && memcmp(want, got, size) == 0;
            }
        }
        if (!ok) {
            printf("(%s) ", algo_names[algo]);
        }
        // A megabyte at once, many blocks to one call.
        ref_digest(algo, data + 3, BIG, want);
        digest(algo, data + 3, BIG, got);
        ok = ok && memcmp(want, got, size) == 0;
    }
    free(data);
    CHECK(ok);
}

static void test_stream(void) {
    enum { SIZE = 600, BIG = 1 << 20 };
    static const size_t pieces[] = {1, 7, 55, 56, 63, 64, 65, 111, 112, 127, 128, 129};
    const size_t npieces = sizeof(pieces) / sizeof(pieces[0]);
    uint64_t state = 0x5eed;
    unsigned char *data = malloc(BIG);
    CHECK(data != NULL);
    fill_random(data, BIG, &state);
    unsigned char want[SOF_DIGEST_MAX_SIZE], got[SOF_DIGEST_MAX_SIZE];
    int ok = 1;
    for (int a = 0; ok && a < ALGOS; a++) {
        algo_t algo = (algo_t)a;
        size_t size = algo_sizes[algo];
        stream_t s;
        // Every length up to SIZE, in pieces of each size and of random
        // sizes.
        for (size_t length = 0; ok && length <= SIZE; length++) {
            digest(algo, data, length, want);
            for (size_t p = 0; ok && p <= npieces; p++) {
                stream_init(algo, &s);
                for (size_t at = 0; at < length;) {
                    size_t n = p < npieces ? pieces[p] : (size_t)(next_random(&state) % 300);
                    if (n > length - at) {
                        n = length - at;
                    }
                    stream_update(algo, &s, data + at, n);
                    at += n;
                }
                stream_final(algo, &s, got);
                ok = memcmp(want, got, size) == 0 && stream_length(algo, &s) == length;
            }
        }
        // A megabyte in random pieces, up to 64 KiB.
        if (ok) {
            digest(algo, data, BIG, want);
            stream_init(algo, &s);
            for (size_t at = 0; at < BIG;) {
                size_t n = (size_t)(next_random(&state) % 65536);
                if (n > BIG - at) {
                    n = BIG - at;
                }
                stream_update(algo, &s, data + at, n);
                at += n;
            }
            stream_final(algo, &s, got);
            ok = memcmp(want, got, size) == 0;
        }
        if (!ok) {
            printf("(%s) ", algo_names[algo]);
        }
    }
    free(data);
    CHECK(ok);
}

static void test_final_and_copies(void) {
    char hex[HEX_SIZE], want_ab[HEX_SIZE], want_abc[HEX_SIZE], want_empty[HEX_SIZE];
    for (int a = 0; a < ALGOS; a++) {
        algo_t algo = (algo_t)a;
        hex_digest(algo, "", 0, want_empty);
        hex_digest(algo, "ab", 2, want_ab);
        hex_digest(algo, "abc", 3, want_abc);
        // Zeroed first, so the bytes init doesn't set, the rest of the union
        // and padding, compare equal below.
        stream_t s;
        memset(&s, 0, sizeof(s));
        stream_init(algo, &s);
        CHECK(strcmp(hex_final(algo, &s, hex), want_empty) == 0);
        stream_update(algo, &s, "ab", 2);
        // final leaves the stream alone, so it goes on from "ab".
        stream_t before = s;
        CHECK(strcmp(hex_final(algo, &s, hex), want_ab) == 0);
        CHECK(memcmp(&before, &s, sizeof(s)) == 0);
        // A copy forks the digest.
        stream_t fork = s;
        stream_update(algo, &s, "c", 1);
        CHECK(strcmp(hex_final(algo, &s, hex), want_abc) == 0);
        stream_update(algo, &fork, "cdefghijklmnopqrstuvwxyz", 0);
        stream_update(algo, &fork, "c", 1);
        CHECK(strcmp(hex_final(algo, &fork, hex), want_abc) == 0);
        // init starts again.
        stream_init(algo, &s);
        CHECK(stream_length(algo, &s) == 0);
        CHECK(strcmp(hex_final(algo, &s, hex), want_empty) == 0);
    }
}

static void test_alignment(void) {
    unsigned char buf[300 + 16];
    uint64_t state = 0xa119;
    fill_random(buf, sizeof(buf), &state);
    unsigned char want[SOF_DIGEST_MAX_SIZE], got[SOF_DIGEST_MAX_SIZE];
    unsigned char copy[300 + 16];
    for (int a = 0; a < ALGOS; a++) {
        digest((algo_t)a, buf, 300, want);
        for (size_t offset = 1; offset < 16; offset++) {
            memcpy(copy + offset, buf, 300);
            digest((algo_t)a, copy + offset, 300, got);
            CHECK(memcmp(want, got, algo_sizes[a]) == 0);
        }
    }
}

static void test_every_bit_matters(void) {
    unsigned char buf[100];
    uint64_t state = 0xb175;
    fill_random(buf, sizeof(buf), &state);
    unsigned char want[SOF_DIGEST_MAX_SIZE], got[SOF_DIGEST_MAX_SIZE];
    for (int a = 0; a < ALGOS; a++) {
        size_t bits = 8 * algo_sizes[a];
        digest((algo_t)a, buf, sizeof(buf), want);
        for (size_t bit = 0; bit < 8 * sizeof(buf); bit++) {
            buf[bit / 8] ^= (unsigned char)(1u << bit % 8);
            digest((algo_t)a, buf, sizeof(buf), got);
            buf[bit / 8] ^= (unsigned char)(1u << bit % 8);
            // About half the bits of the digest change: well within a
            // quarter and three quarters, for 128 bits or more.
            size_t changed = 0;
            for (size_t i = 0; i < algo_sizes[a]; i++) {
                for (unsigned char x = want[i] ^ got[i]; x != 0; x &= (unsigned char)(x - 1)) {
                    changed++;
                }
            }
            CHECK(changed > bits / 4 && changed < 3 * bits / 4);
        }
    }
}

static void test_crc_known(void) {
    // From Python's zlib.crc32 for CRC-32, and for CRC-32C from a bitwise
    // implementation in Python, whose check value, of "123456789", is the
    // one the CRC catalogue gives.
    static const known_crc_t known[] = {
        {"", 0x00000000, 0x00000000},
        {"a", 0xe8b7be43, 0xc1d04330},
        {"abc", 0x352441c2, 0x364b3fb7},
        {"123456789", 0xcbf43926, 0xe3069283},
        {"message digest", 0x20159d7f, 0x02bd79d0},
        {"The quick brown fox jumps over the lazy dog", 0x414fa339, 0x22620404},
    };
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        size_t length = strlen(known[i].text);
        CHECK(sof_digest_crc32(known[i].text, length) == known[i].crc32);
        CHECK(sof_digest_crc32c(known[i].text, length) == known[i].crc32c);
    }
    unsigned char all[256];
    for (int i = 0; i < 256; i++) {
        all[i] = (unsigned char)i;
    }
    CHECK(sof_digest_crc32(all, sizeof(all)) == 0x29058c73);
    CHECK(sof_digest_crc32c(all, sizeof(all)) == 0x9c44184b);
    // A megabyte of zeros.
    unsigned char *zeros = calloc(1 << 20, 1);
    CHECK(zeros != NULL);
    uint32_t crc32 = sof_digest_crc32(zeros, 1 << 20), crc32c = sof_digest_crc32c(zeros, 1 << 20);
    free(zeros);
    CHECK(crc32 == 0xa738ea1c);
    CHECK(crc32c == 0x14298c12);
}

static void test_crc_reference(void) {
    enum { SIZE = 1000, BIG = 1 << 20 };
    uint64_t state = 0xc8c;
    unsigned char *data = malloc(BIG + 16);
    unsigned char *copy = malloc(SIZE + 16);
    CHECK(data != NULL && copy != NULL);
    fill_random(data, BIG + 16, &state);
    int ok = 1;
    // Every length up to SIZE, at every alignment to 16 bytes, whole and as
    // a stream in random pieces of up to 300 bytes.
    for (size_t length = 0; ok && length <= SIZE; length++) {
        uint32_t want32 = ref_crc(CRC32_POLY, data, length), want32c = ref_crc(CRC32C_POLY, data, length);
        for (size_t offset = 0; ok && offset < 16; offset++) {
            memcpy(copy + offset, data, length);
            ok = sof_digest_crc32(copy + offset, length) == want32 && sof_digest_crc32c(copy + offset, length) == want32c;
        }
        sof_digest_crc32_t crc;
        sof_digest_crc32c_t crcc;
        sof_digest_crc32_init(&crc);
        sof_digest_crc32c_init(&crcc);
        for (size_t done = 0; done < length;) {
            size_t n = (size_t)(next_random(&state) % 301);
            if (n > length - done) {
                n = length - done;
            }
            sof_digest_crc32_update(&crc, data + done, n);
            sof_digest_crc32c_update(&crcc, data + done, n);
            done += n;
        }
        ok = ok && sof_digest_crc32_final(&crc) == want32 && sof_digest_crc32c_final(&crcc) == want32c;
        ok = ok && crc.length == length && crcc.length == length;
    }
    free(copy);
    CHECK(ok);
    // A megabyte, at an odd address, whole and in random pieces up to 64
    // KiB.
    uint32_t want32 = ref_crc(CRC32_POLY, data + 3, BIG), want32c = ref_crc(CRC32C_POLY, data + 3, BIG);
    CHECK(sof_digest_crc32(data + 3, BIG) == want32);
    CHECK(sof_digest_crc32c(data + 3, BIG) == want32c);
    uint32_t crc32 = 0, crc32c = 0;
    for (size_t at = 0; at < BIG;) {
        size_t n = (size_t)(next_random(&state) % 65536);
        if (n > BIG - at) {
            n = BIG - at;
        }
        crc32 = sof_digest_crc32_extend(crc32, data + 3 + at, n);
        crc32c = sof_digest_crc32c_extend(crc32c, data + 3 + at, n);
        at += n;
    }
    free(data);
    CHECK(crc32 == want32);
    CHECK(crc32c == want32c);
}

static void test_crc_extend_and_combine(void) {
    enum { SIZE = 5000 };
    uint64_t state = 0xc0b1;
    unsigned char data[SIZE];
    fill_random(data, sizeof(data), &state);
    CHECK(sof_digest_crc32_extend(0, "123456789", 9) == 0xcbf43926);
    CHECK(sof_digest_crc32c_extend(0, "123456789", 9) == 0xe3069283);
    CHECK(sof_digest_crc32_extend(sof_digest_crc32("1234", 4), "56789", 5) == 0xcbf43926);
    CHECK(sof_digest_crc32c_extend(sof_digest_crc32c("1234", 4), "56789", 5) == 0xe3069283);
    // Split anywhere, the CRCs of the two pieces combine into that of the
    // whole, and the first piece extended by the second is too.
    for (int round = 0; round < 200; round++) {
        size_t length = (size_t)(next_random(&state) % (SIZE + 1));
        size_t split = (size_t)(next_random(&state) % (length + 1));
        uint32_t whole = sof_digest_crc32(data, length), first = sof_digest_crc32(data, split);
        uint32_t second = sof_digest_crc32(data + split, length - split);
        CHECK(sof_digest_crc32_combine(first, second, length - split) == whole);
        CHECK(sof_digest_crc32_extend(first, data + split, length - split) == whole);
        whole = sof_digest_crc32c(data, length);
        first = sof_digest_crc32c(data, split);
        second = sof_digest_crc32c(data + split, length - split);
        CHECK(sof_digest_crc32c_combine(first, second, length - split) == whole);
        CHECK(sof_digest_crc32c_extend(first, data + split, length - split) == whole);
    }
    // Combining with nothing changes nothing.
    CHECK(sof_digest_crc32_combine(0x12345678, 0, 0) == 0x12345678);
    CHECK(sof_digest_crc32c_combine(0x12345678, 0, 0) == 0x12345678);
    // "123456789" followed by a megabyte of zeros, without the zeros: the
    // CRC of the zeros is known, and the combination is checked against
    // extending with them.
    unsigned char *zeros = calloc(1 << 20, 1);
    CHECK(zeros != NULL);
    uint32_t want32 = sof_digest_crc32_extend(0xcbf43926, zeros, 1 << 20);
    uint32_t want32c = sof_digest_crc32c_extend(0xe3069283, zeros, 1 << 20);
    free(zeros);
    CHECK(sof_digest_crc32_combine(0xcbf43926, 0xa738ea1c, 1 << 20) == want32);
    CHECK(sof_digest_crc32c_combine(0xe3069283, 0x14298c12, 1 << 20) == want32c);
    // Lengths too long to have data for: shifting a CRC past 2^39 bytes
    // twice is shifting it past 2^40 bytes once.
    uint64_t half = (uint64_t)1 << 39;
    uint32_t a_then = sof_digest_crc32_combine(sof_digest_crc32_combine(0xe8b7be43, 0, half), 0, half);
    CHECK(a_then == sof_digest_crc32_combine(0xe8b7be43, 0, 2 * half));
}

static void test_crc_streams(void) {
    sof_digest_crc32_t crc;
    sof_digest_crc32c_t crcc;
    sof_digest_crc32_init(&crc);
    sof_digest_crc32c_init(&crcc);
    CHECK(sof_digest_crc32_final(&crc) == 0 && crc.length == 0);
    CHECK(sof_digest_crc32c_final(&crcc) == 0 && crcc.length == 0);
    sof_digest_crc32_update(&crc, "1234", 4);
    sof_digest_crc32c_update(&crcc, "1234", 4);
    // final leaves the stream alone, and a copy forks it.
    sof_digest_crc32_t fork = crc;
    sof_digest_crc32c_t forkc = crcc;
    CHECK(sof_digest_crc32_final(&crc) == sof_digest_crc32("1234", 4));
    CHECK(sof_digest_crc32c_final(&crcc) == sof_digest_crc32c("1234", 4));
    sof_digest_crc32_update(&crc, "56789", 5);
    sof_digest_crc32c_update(&crcc, "56789", 5);
    CHECK(sof_digest_crc32_final(&crc) == 0xcbf43926 && crc.length == 9 && crc.crc == 0xcbf43926);
    CHECK(sof_digest_crc32c_final(&crcc) == 0xe3069283 && crcc.length == 9);
    sof_digest_crc32_update(&fork, "5678", 4);
    sof_digest_crc32c_update(&forkc, "5678", 4);
    sof_digest_crc32_update(&fork, "9", 1);
    sof_digest_crc32c_update(&forkc, "9", 1);
    CHECK(sof_digest_crc32_final(&fork) == 0xcbf43926);
    CHECK(sof_digest_crc32c_final(&forkc) == 0xe3069283);
    // Every bit matters: flipping any one bit of the data changes the CRC.
    unsigned char buf[100];
    uint64_t state = 0xf11b;
    fill_random(buf, sizeof(buf), &state);
    uint32_t want32 = sof_digest_crc32(buf, sizeof(buf)), want32c = sof_digest_crc32c(buf, sizeof(buf));
    for (size_t bit = 0; bit < 8 * sizeof(buf); bit++) {
        buf[bit / 8] ^= (unsigned char)(1u << bit % 8);
        CHECK(sof_digest_crc32(buf, sizeof(buf)) != want32);
        CHECK(sof_digest_crc32c(buf, sizeof(buf)) != want32c);
        buf[bit / 8] ^= (unsigned char)(1u << bit % 8);
    }
}

static void test_hex(void) {
    unsigned char bytes[] = {0x00, 0x01, 0x7f, 0x80, 0xab, 0xff};
    char hex[2 * sizeof(bytes) + 1];
    memset(hex, 'x', sizeof(hex));
    CHECK(sof_digest_to_hex(bytes, sizeof(bytes), hex) == hex);
    CHECK(strcmp(hex, "00017f80abff") == 0);
    CHECK(sof_digest_to_hex(bytes, 0, hex) == hex && hex[0] == '\0');

    unsigned char back[sizeof(bytes)];
    CHECK(sof_digest_from_hex("00017f80abff", 12, back) == 0);
    CHECK(memcmp(back, bytes, sizeof(bytes)) == 0);
    memset(back, 0, sizeof(back));
    CHECK(sof_digest_from_hex("00017F80ABFF", 12, back) == 0);
    CHECK(memcmp(back, bytes, sizeof(bytes)) == 0);
    // Only length digits are read, so they needn't end with a NUL.
    CHECK(sof_digest_from_hex("abcdzz", 4, back) == 0 && back[0] == 0xab && back[1] == 0xcd);
    CHECK(sof_digest_from_hex("", 0, back) == 0);

    // Bad digits, or an odd count, write nothing.
    memset(back, 0x55, sizeof(back));
    CHECK(sof_digest_from_hex("abc", 3, back) == -1);
    CHECK(sof_digest_from_hex("abcg", 4, back) == -1);
    CHECK(sof_digest_from_hex("ab c", 4, back) == -1);
    CHECK(sof_digest_from_hex("ab\0c", 4, back) == -1);
    CHECK(sof_digest_from_hex("0x12", 4, back) == -1);
    for (size_t i = 0; i < sizeof(back); i++) {
        CHECK(back[i] == 0x55);
    }

    // A digest goes there and back.
    unsigned char out[SOF_DIGEST_SHA256_SIZE], again[SOF_DIGEST_SHA256_SIZE];
    char digest_hex[HEX_SIZE];
    sof_digest_sha256("abc", 3, out);
    sof_digest_to_hex(out, sizeof(out), digest_hex);
    CHECK(strlen(digest_hex) == 2 * SOF_DIGEST_SHA256_SIZE);
    CHECK(sof_digest_from_hex(digest_hex, strlen(digest_hex), again) == 0);
    CHECK(memcmp(out, again, sizeof(out)) == 0);
}

static void test_null(void) {
    char hex[HEX_SIZE], empty[HEX_SIZE], abc[HEX_SIZE];
    unsigned char out[SOF_DIGEST_MAX_SIZE];
    for (int a = 0; a < ALGOS; a++) {
        algo_t algo = (algo_t)a;
        hex_digest(algo, "", 0, empty);
        hex_digest(algo, "abc", 3, abc);
        // NULL data is no data, whatever the length.
        CHECK(strcmp(hex_digest(algo, NULL, 0, hex), empty) == 0);
        CHECK(strcmp(hex_digest(algo, NULL, 100, hex), empty) == 0);
        digest(algo, "abc", 3, NULL);
        // A NULL stream does nothing, and its digest is that of no data.
        stream_init(algo, NULL);
        stream_update(algo, NULL, "abc", 3);
        CHECK(strcmp(hex_final(algo, NULL, hex), empty) == 0);
        stream_t s;
        stream_init(algo, &s);
        stream_update(algo, &s, "ab", 2);
        stream_update(algo, &s, NULL, 10);
        CHECK(stream_length(algo, &s) == 2);
        stream_final(algo, &s, NULL);
        stream_update(algo, &s, "c", 1);
        CHECK(strcmp(hex_final(algo, &s, hex), abc) == 0);
    }

    // The CRCs: NULL data is no data, and a NULL stream's CRC is 0.
    CHECK(sof_digest_crc32(NULL, 100) == 0);
    CHECK(sof_digest_crc32c(NULL, 100) == 0);
    CHECK(sof_digest_crc32_extend(0x1234, NULL, 100) == 0x1234);
    CHECK(sof_digest_crc32c_extend(0x1234, NULL, 100) == 0x1234);
    sof_digest_crc32_init(NULL);
    sof_digest_crc32c_init(NULL);
    sof_digest_crc32_update(NULL, "abc", 3);
    sof_digest_crc32c_update(NULL, "abc", 3);
    CHECK(sof_digest_crc32_final(NULL) == 0);
    CHECK(sof_digest_crc32c_final(NULL) == 0);
    sof_digest_crc32_t crc;
    sof_digest_crc32c_t crcc;
    sof_digest_crc32_init(&crc);
    sof_digest_crc32c_init(&crcc);
    sof_digest_crc32_update(&crc, NULL, 10);
    sof_digest_crc32c_update(&crcc, NULL, 10);
    CHECK(crc.length == 0 && crc.crc == 0 && crcc.length == 0 && crcc.crc == 0);

    // Hex.
    CHECK(sof_digest_to_hex(out, sizeof(out), NULL) == NULL);
    CHECK(sof_digest_to_hex(NULL, 10, hex) == hex && hex[0] == '\0');
    CHECK(sof_digest_from_hex(NULL, 2, out) == -1);
    CHECK(sof_digest_from_hex("ab", 2, NULL) == -1);
    CHECK(sof_digest_from_hex(NULL, 0, NULL) == 0);
}

/// @brief The digest of case i in the threaded test: its data and its
/// algorithm, which goes round them all, with the CRCs as two more.
static void thread_case(const unsigned char *data, size_t i, unsigned char *out) {
    const unsigned char *p = data + i;
    size_t length = DATA_SIZE - 64 * i;
    int a = (int)(i % (ALGOS + 2));
    memset(out, 0, SOF_DIGEST_MAX_SIZE);
    if (a < ALGOS) {
        digest((algo_t)a, p, length, out);
    } else {
        uint32_t crc = a == ALGOS ? sof_digest_crc32(p, length) : sof_digest_crc32c(p, length);
        memcpy(out, &crc, sizeof(crc));
    }
}

/// @brief A thread's work: computes each case PASSES times and counts those
/// that differ from the serial results.
static void *digest_cases(void *arg) {
    worker_t *worker = arg;
    unsigned char got[SOF_DIGEST_MAX_SIZE];
    for (int pass = 0; pass < PASSES; pass++) {
        for (size_t i = 0; i < CASES; i++) {
            thread_case(worker->data, i, got);
            if (memcmp(got, worker->expected[i], sizeof(got)) != 0) {
                worker->mismatches++;
            }
        }
    }
    return NULL;
}

static void test_threads(void) {
    static unsigned char data[DATA_SIZE];
    static unsigned char expected[CASES][SOF_DIGEST_MAX_SIZE];
    uint64_t state = 0x7417ead5;
    fill_random(data, sizeof(data), &state);
    for (size_t i = 0; i < CASES; i++) {
        thread_case(data, i, expected[i]);
    }
    worker_t workers[THREADS];
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; i++) {
        workers[i] = (worker_t){data, (const unsigned char (*)[SOF_DIGEST_MAX_SIZE])expected, 0};
        CHECK(pthread_create(&threads[i], NULL, digest_cases, &workers[i]) == 0);
    }
    for (int i = 0; i < THREADS; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    for (int i = 0; i < THREADS; i++) {
        CHECK(workers[i].mismatches == 0);
    }
}

int main(void) {
#if defined(SOF_DIGEST_PORTABLE)
    printf("sof_digest, portable:\n");
#else
    printf("sof_digest:\n");
#endif
    run("known digests", test_known_values);
    run("every length to 1,000, whole and in pieces, against references", test_reference);
    run("streams give the one-shot digests, however split", test_stream);
    run("final leaves a stream alone, and copies fork it", test_final_and_copies);
    run("alignment doesn't matter", test_alignment);
    run("every bit matters", test_every_bit_matters);
    run("known CRCs", test_crc_known);
    run("CRCs of every length to 1,000 at every alignment, against a bitwise reference", test_crc_reference);
    run("CRCs extend and combine", test_crc_extend_and_combine);
    run("CRC streams", test_crc_streams);
    run("hex both ways", test_hex);
    run("NULL arguments", test_null);
    run("threads digesting the same data", test_threads);
    return test_report();
}
