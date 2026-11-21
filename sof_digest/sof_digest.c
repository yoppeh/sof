/**
 * @file sof_digest.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Message digests and checksums in C: SHA-2, SHA-1, MD5, CRC-32 and
 * CRC-32C.
 * @details SHA-1, SHA-256 and SHA-512 as FIPS 180-4 defines them, with
 * SHA-224, SHA-384 and SHA-512/256 from the same functions, MD5 as RFC 1321
 * defines it, and the CRC-32 of zip and the CRC-32C of iSCSI.
 *
 * The blocks are hashed with ARMv8's SHA-1 and SHA-256 instructions when the
 * compiler targets them (__ARM_FEATURE_SHA2, as Apple's clang does for
 * arm64), and its SHA-512 instructions likewise (__ARM_FEATURE_SHA512); with
 * x86's SHA extensions, for SHA-1 and SHA-256, when the processor has them
 * (on x86-64 with gcc or clang, checked at run time); and otherwise in
 * portable c, which reads and writes words a byte at a time, so it needs no
 * knowledge of the platform's byte order or alignment. MD5 is portable c
 * everywhere.
 *
 * The CRCs fold pieces of 64 bytes or more with carry-less multiplication,
 * ARMv8's PMULL when the compiler targets it (__ARM_FEATURE_AES), or x86's
 * PCLMULQDQ when the processor has it, 128 bytes at a time in eight lanes;
 * shorter pieces, and the last few bytes, go through ARMv8's CRC32
 * instructions when the compiler targets them (__ARM_FEATURE_CRC32), or
 * for CRC-32C on x86-64, SSE4.2's crc32 instruction when the processor has
 * it; and otherwise through portable c that takes 16 bytes at a step,
 * through 16 tables of 256 entries that gen_table.py writes into
 * sof_digest_internal.h, with the folding constants.
 *
 * Defining SOF_DIGEST_PORTABLE when compiling this file uses the portable
 * code everywhere.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <string.h>

#include "sof_digest.h"

// The instructions: ARMv8's where the compiler targets them, and x86's
// where the processor has them, found at run time, since the usual x86-64
// build doesn't assume them. SOF_DIGEST_PORTABLE leaves them all out. The
// CRC instructions are given 8 bytes at a time, read as a little-endian
// word, so they are used only where that is the byte order.
#if !defined(SOF_DIGEST_PORTABLE) && defined(__ARM_FEATURE_SHA2)
#define SHA2_ARM 1
#endif
#if !defined(SOF_DIGEST_PORTABLE) && defined(__ARM_FEATURE_SHA512)
#define SHA512_ARM 1
#endif
#if defined(SHA2_ARM) || defined(SHA512_ARM)
#include <arm_neon.h>
#endif
#if !defined(SOF_DIGEST_PORTABLE) && defined(__ARM_FEATURE_CRC32) && defined(__BYTE_ORDER__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define CRC_ARM 1
#include <arm_acle.h>
#if defined(__ARM_FEATURE_AES)
#define CRC_FOLD_ARM 1
#include <arm_neon.h>
#endif
#endif
#if !defined(SOF_DIGEST_PORTABLE) && defined(__x86_64__) && defined(__GNUC__)
#define DIGEST_X86 1
#include <immintrin.h>
#endif

// The tables of sof_digest_internal.h that this build uses: the portable
// CRC code's, unless the CRC instructions do all of it, and the folding
// constants, where there is carry-less multiplication.
#if !defined(CRC_ARM)
#define SOF_DIGEST_CRC_TABLES 1
#endif
#if defined(DIGEST_X86) || defined(CRC_FOLD_ARM)
#define SOF_DIGEST_CRC_FOLD 1
#endif
#include "sof_digest_internal.h"

/// @brief The fewest bytes the CRCs fold with carry-less multiplication,
/// rather than use the CRC instructions or the tables alone, and on x86,
/// where SSE4.2's crc32 instruction is faster up to about 128 bytes, the
/// fewest for CRC-32C.
#define CRC_FOLD_MIN 64
#define CRC32C_FOLD_MIN_X86 128

/// @brief Hashes blocks blocks of a stream's size at p into state.
typedef void (*blocks_func_t)(void *state, const unsigned char *p, size_t blocks);

#if defined(CRC_ARM)
static uint32_t crc_arm(int castagnoli, uint32_t crc, const unsigned char *p, size_t length);
#endif
#if defined(CRC_FOLD_ARM)
static uint32_t crc_fold_arm(const uint64_t *k, uint32_t crc, const unsigned char *p, size_t length);
#endif
#if !defined(CRC_ARM)
static uint32_t crc_portable(const uint32_t (*table)[256], uint32_t crc, const unsigned char *p, size_t length);
#endif
static uint32_t crc_shift(uint32_t poly, const uint32_t *x8n, uint32_t crc, uint64_t length);
static uint32_t crc32_register(uint32_t crc, const unsigned char *p, size_t length);
static uint32_t crc32c_register(uint32_t crc, const unsigned char *p, size_t length);
#if defined(DIGEST_X86)
static uint32_t crc_fold_x86(const uint64_t *k, uint32_t crc, const unsigned char *p, size_t length);
static uint32_t crc32c_x86(uint32_t crc, const unsigned char *p, size_t length);
#endif
#if !defined(SHA2_ARM)
static inline uint32_t load32(const unsigned char *p);
#endif
static inline uint32_t load32le(const unsigned char *p);
#if !defined(SHA512_ARM)
static inline uint64_t load64(const unsigned char *p);
#endif
static void md5_blocks(void *state, const unsigned char *p, size_t blocks);
static uint32_t multmodp(uint32_t poly, uint32_t a, uint32_t b);
static inline uint32_t rotl32(uint32_t x, unsigned n);
#if !defined(SHA2_ARM)
static inline uint32_t rotr32(uint32_t x, unsigned n);
#endif
#if !defined(SHA512_ARM)
static inline uint64_t rotr64(uint64_t x, unsigned n);
#endif
static void sha1_blocks(void *state, const unsigned char *p, size_t blocks);
#if defined(SHA2_ARM)
static void sha1_blocks_arm(uint32_t *state, const unsigned char *p, size_t blocks);
#else
static void sha1_blocks_portable(uint32_t *state, const unsigned char *p, size_t blocks);
#endif
#if defined(DIGEST_X86)
static void sha1_blocks_x86(uint32_t *state, const unsigned char *p, size_t blocks);
#endif
static void sha256_blocks(void *state, const unsigned char *p, size_t blocks);
#if defined(SHA2_ARM)
static void sha256_blocks_arm(uint32_t *state, const unsigned char *p, size_t blocks);
#else
static void sha256_blocks_portable(uint32_t *state, const unsigned char *p, size_t blocks);
#endif
#if defined(DIGEST_X86)
static void sha256_blocks_x86(uint32_t *state, const unsigned char *p, size_t blocks);
#endif
static void sha512_blocks(void *state, const unsigned char *p, size_t blocks);
static void sha512_output(const sof_digest_sha512_t *sha, unsigned char *digest, size_t size);
static inline void store32(unsigned char *p, uint32_t x);
static inline void store32le(unsigned char *p, uint32_t x);
static inline void store64(unsigned char *p, uint64_t x);
static void stream_pad(void *state, const unsigned char *buffer, size_t buffered, size_t block, blocks_func_t blocks_func, uint64_t length, int little_endian);
static void stream_update(void *state, unsigned char *buffer, size_t *buffered, size_t block, blocks_func_t blocks_func, const unsigned char *p, size_t length);
static int unhex(char c);

/// @brief MD5's initial hash value.
static const uint32_t md5_initial[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};

/// @brief SHA-1's initial hash value.
static const uint32_t sha1_initial[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};

/// @brief SHA-224's initial hash value: the second 32 bits of the
/// fractional parts of the square roots of the ninth to sixteenth primes.
static const uint32_t sha224_initial[8] = {
    0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939, 0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4,
};

/// @brief SHA-256's initial hash value: the first 32 bits of the fractional
/// parts of the square roots of the first eight primes.
static const uint32_t sha256_initial[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

/// @brief SHA-256's round constants: the first 32 bits of the fractional
/// parts of the cube roots of the first 64 primes.
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

/// @brief SHA-384's initial hash value: the first 64 bits of the fractional
/// parts of the square roots of the ninth to sixteenth primes.
static const uint64_t sha384_initial[8] = {
    0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
    0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL,
};

/// @brief SHA-512's initial hash value: the first 64 bits of the fractional
/// parts of the square roots of the first eight primes.
static const uint64_t sha512_initial[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

/// @brief SHA-512/256's initial hash value, which FIPS 180-4 makes by
/// hashing "SHA-512/256" with SHA-512 from a changed initial value.
static const uint64_t sha512_256_initial[8] = {
    0x22312194fc2bf72cULL, 0x9f555fa3c84c64c2ULL, 0x2393b86b6f53b151ULL, 0x963877195940eabdULL,
    0x96283ee2a88effe3ULL, 0xbe5e1e2553863992ULL, 0x2b0199fc2c85b8aaULL, 0x0eb72ddc81c52ca2ULL,
};

/// @brief SHA-512's round constants: the first 64 bits of the fractional
/// parts of the cube roots of the first 80 primes.
static const uint64_t sha512_k[80] = {
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

#if defined(CRC_ARM)

/// @brief Runs length bytes at p through the CRC register crc with ARMv8's
/// CRC32 instructions, 8 bytes at a time: CRC-32's, or CRC-32C's if
/// castagnoli is set.
static uint32_t crc_arm(int castagnoli, uint32_t crc, const unsigned char *p, size_t length) {
    for (; length >= 8; length -= 8, p += 8) {
        uint64_t word;
        memcpy(&word, p, sizeof(word));
        crc = castagnoli ? __crc32cd(crc, word) : __crc32d(crc, word);
    }
    for (; length > 0; length--, p++) {
        crc = castagnoli ? __crc32cb(crc, *p) : __crc32b(crc, *p);
    }
    return crc;
}

#endif

#if defined(CRC_FOLD_ARM)

/// @brief Runs length bytes at p, at least 64 and a multiple of 16, through
/// the CRC register crc with ARMv8's carry-less multiplication (PMULL),
/// given the polynomial's folding constants k: the method of
/// crc_fold_x86(), step for step.
static uint32_t crc_fold_arm(const uint64_t *k, uint32_t crc, const unsigned char *p, size_t length) {
// The products of the low halves of a and b, of a's low and b's high, and
// of the high halves, as PCLMULQDQ's selectors 0x00, 0x10 and 0x11 give.
#define MUL00(a, b) vreinterpretq_u64_p128(vmull_p64(vgetq_lane_u64(a, 0), vgetq_lane_u64(b, 0)))
#define MUL10(a, b) vreinterpretq_u64_p128(vmull_p64(vgetq_lane_u64(a, 0), vgetq_lane_u64(b, 1)))
#define MUL11(a, b) vreinterpretq_u64_p128(vmull_p64(vgetq_lane_u64(a, 1), vgetq_lane_u64(b, 1)))
#define LOAD(at) vreinterpretq_u64_u8(vld1q_u8(at))
// A lane x multiplied forward by the pair of constants kk, with next xored
// in.
#define FOLD(x, kk, next) veorq_u64(veorq_u64(MUL11(x, kk), MUL00(x, kk)), (next))
    uint64x2_t x1 = LOAD(p), x2 = LOAD(p + 16), x3 = LOAD(p + 32), x4 = LOAD(p + 48);
    x1 = veorq_u64(x1, vsetq_lane_u64(crc, vdupq_n_u64(0), 0));
    uint64x2_t k12 = vld1q_u64(k);
    p += 64;
    length -= 64;
    // Eight lanes, 128 bytes at a time, while there are, folded into four.
    if (length >= 192) {
        uint64x2_t k8 = vld1q_u64(k + 8);
        uint64x2_t x5 = LOAD(p), x6 = LOAD(p + 16), x7 = LOAD(p + 32), x8 = LOAD(p + 48);
        p += 64;
        length -= 64;
        for (; length >= 128; length -= 128, p += 128) {
            x1 = FOLD(x1, k8, LOAD(p));
            x2 = FOLD(x2, k8, LOAD(p + 16));
            x3 = FOLD(x3, k8, LOAD(p + 32));
            x4 = FOLD(x4, k8, LOAD(p + 48));
            x5 = FOLD(x5, k8, LOAD(p + 64));
            x6 = FOLD(x6, k8, LOAD(p + 80));
            x7 = FOLD(x7, k8, LOAD(p + 96));
            x8 = FOLD(x8, k8, LOAD(p + 112));
        }
        x1 = FOLD(x1, k12, x5);
        x2 = FOLD(x2, k12, x6);
        x3 = FOLD(x3, k12, x7);
        x4 = FOLD(x4, k12, x8);
    }
    for (; length >= 64; length -= 64, p += 64) {
        x1 = FOLD(x1, k12, LOAD(p));
        x2 = FOLD(x2, k12, LOAD(p + 16));
        x3 = FOLD(x3, k12, LOAD(p + 32));
        x4 = FOLD(x4, k12, LOAD(p + 48));
    }
    // Fold the four lanes into one, then each 16 bytes left into it.
    uint64x2_t k34 = vld1q_u64(k + 2);
    x1 = FOLD(x1, k34, x2);
    x1 = FOLD(x1, k34, x3);
    x1 = FOLD(x1, k34, x4);
    for (; length >= 16; length -= 16, p += 16) {
        x1 = FOLD(x1, k34, LOAD(p));
    }
    // 128 bits to 64, then Barrett's reduction to 32.
    const uint64x2_t zero = vdupq_n_u64(0), mask = vdupq_n_u64(0xffffffff);
    x1 = veorq_u64(vextq_u64(x1, zero, 1), MUL10(x1, k34));
    uint64x2_t k5 = vld1q_u64(k + 4);
    x2 = vreinterpretq_u64_u8(vextq_u8(vreinterpretq_u8_u64(x1), vreinterpretq_u8_u64(zero), 4));
    x1 = veorq_u64(MUL00(vandq_u64(x1, mask), k5), x2);
    uint64x2_t poly = vld1q_u64(k + 6);
    x2 = MUL10(vandq_u64(x1, mask), poly);
    x2 = MUL00(vandq_u64(x2, mask), poly);
    x1 = veorq_u64(x1, x2);
    return vgetq_lane_u32(vreinterpretq_u32_u64(x1), 1);
#undef MUL00
#undef MUL10
#undef MUL11
#undef LOAD
#undef FOLD
}

#endif

#if !defined(CRC_ARM)

/// @brief Runs length bytes at p through the CRC register crc with the
/// slicing tables table, 16 bytes to each step.
/// @details Each step xors the register into the first of the next 16
/// bytes, read as four little-endian words, and replaces it with the CRC of
/// those bytes alone, one table lookup per byte: the table for the number
/// of bytes that follow it in the step. The lookups don't depend on each
/// other, so the processor makes them at once.
static uint32_t crc_portable(const uint32_t (*table)[256], uint32_t crc, const unsigned char *p, size_t length) {
    for (; length >= 16; length -= 16, p += 16) {
        uint32_t a = crc ^ load32le(p), b = load32le(p + 4), c = load32le(p + 8), d = load32le(p + 12);
        crc = table[15][a & 0xff] ^ table[14][a >> 8 & 0xff] ^ table[13][a >> 16 & 0xff] ^ table[12][a >> 24] ^ table[11][b & 0xff] ^ table[10][b >> 8 & 0xff] ^ table[9][b >> 16 & 0xff] ^ table[8][b >> 24] ^ table[7][c & 0xff] ^ table[6][c >> 8 & 0xff] ^ table[5][c >> 16 & 0xff] ^ table[4][c >> 24] ^ table[3][d & 0xff] ^ table[2][d >> 8 & 0xff] ^ table[1][d >> 16 & 0xff] ^ table[0][d >> 24];
    }
    for (; length > 0; length--, p++) {
        crc = table[0][(crc ^ *p) & 0xff] ^ crc >> 8;
    }
    return crc;
}

#endif

/// @brief Shifts a CRC register, or a finished CRC, past length zero bytes:
/// multiplies it by x^(8 * length) modulo the polynomial, from the powers
/// x^(8 * 2^k) in x8n.
static uint32_t crc_shift(uint32_t poly, const uint32_t *x8n, uint32_t crc, uint64_t length) {
    for (int k = 0; length != 0; k++, length >>= 1) {
        if (length & 1) {
            crc = multmodp(poly, x8n[k], crc);
        }
    }
    return crc;
}

/// @brief Runs length bytes at p through the CRC-32 register crc with the
/// fastest code the compiler and processor allow.
static uint32_t crc32_register(uint32_t crc, const unsigned char *p, size_t length) {
#if defined(CRC_ARM)
#if defined(CRC_FOLD_ARM)
    if (length >= CRC_FOLD_MIN) {
        size_t folded = length & ~(size_t)15;
        crc = crc_fold_arm(sof_digest_crc32_fold, crc, p, folded);
        p += folded;
        length -= folded;
    }
#endif
    return crc_arm(0, crc, p, length);
#else
#if defined(DIGEST_X86)
    if (length >= CRC_FOLD_MIN && __builtin_cpu_supports("pclmul") && __builtin_cpu_supports("sse4.1")) {
        size_t folded = length & ~(size_t)15;
        crc = crc_fold_x86(sof_digest_crc32_fold, crc, p, folded);
        p += folded;
        length -= folded;
    }
#endif
    return crc_portable(sof_digest_crc32_table, crc, p, length);
#endif
}

/// @brief Runs length bytes at p through the CRC-32C register crc with the
/// fastest code the compiler and processor allow.
static uint32_t crc32c_register(uint32_t crc, const unsigned char *p, size_t length) {
#if defined(CRC_ARM)
#if defined(CRC_FOLD_ARM)
    if (length >= CRC_FOLD_MIN) {
        size_t folded = length & ~(size_t)15;
        crc = crc_fold_arm(sof_digest_crc32c_fold, crc, p, folded);
        p += folded;
        length -= folded;
    }
#endif
    return crc_arm(1, crc, p, length);
#else
#if defined(DIGEST_X86)
    if (__builtin_cpu_supports("sse4.2")) {
        if (length >= CRC32C_FOLD_MIN_X86 && __builtin_cpu_supports("pclmul")) {
            size_t folded = length & ~(size_t)15;
            crc = crc_fold_x86(sof_digest_crc32c_fold, crc, p, folded);
            p += folded;
            length -= folded;
        }
        return crc32c_x86(crc, p, length);
    }
#endif
    return crc_portable(sof_digest_crc32c_table, crc, p, length);
#endif
}

#if defined(DIGEST_X86)

/// @brief Runs length bytes at p, at least 64 and a multiple of 16, through
/// the CRC register crc with carry-less multiplication, given the
/// polynomial's folding constants k, compiled for PCLMULQDQ alone, so it is
/// called only on a processor that has it.
/// @details Intel's method ("Fast CRC Computation for Generic Polynomials
/// Using PCLMULQDQ Instruction", 2009), as zlib in Chromium has it, with
/// eight lanes for long data: eight 128-bit lanes are each multiplied
/// forward past 128 bytes and the next 128 xored in, as long as there are,
/// then folded into four, which do the same 64 bytes at a time; those are
/// folded into one, which takes the remaining 16-byte pieces, and is
/// reduced to 64 bits and then, by Barrett's method, to the 32 of the CRC.
/// The lanes' multiplications don't depend on each other, so the processor
/// overlaps them.
__attribute__((target("pclmul,sse4.1"))) static uint32_t crc_fold_x86(const uint64_t *k, uint32_t crc, const unsigned char *p, size_t length) {
// A lane x multiplied forward by the pair of constants kk, with next xored
// in.
#define FOLD(x, kk, next) \
    _mm_xor_si128(_mm_xor_si128(_mm_clmulepi64_si128(x, kk, 0x11), _mm_clmulepi64_si128(x, kk, 0x00)), (next))
#define LOAD(at) _mm_loadu_si128((const __m128i *)(at))
    __m128i x1 = LOAD(p), x2 = LOAD(p + 16), x3 = LOAD(p + 32), x4 = LOAD(p + 48);
    x1 = _mm_xor_si128(x1, _mm_cvtsi32_si128((int)crc));
    __m128i k12 = LOAD(k);
    p += 64;
    length -= 64;
    // Eight lanes, 128 bytes at a time, while there are, folded into four.
    if (length >= 192) {
        __m128i k8 = LOAD(k + 8);
        __m128i x5 = LOAD(p), x6 = LOAD(p + 16), x7 = LOAD(p + 32), x8 = LOAD(p + 48);
        p += 64;
        length -= 64;
        for (; length >= 128; length -= 128, p += 128) {
            x1 = FOLD(x1, k8, LOAD(p));
            x2 = FOLD(x2, k8, LOAD(p + 16));
            x3 = FOLD(x3, k8, LOAD(p + 32));
            x4 = FOLD(x4, k8, LOAD(p + 48));
            x5 = FOLD(x5, k8, LOAD(p + 64));
            x6 = FOLD(x6, k8, LOAD(p + 80));
            x7 = FOLD(x7, k8, LOAD(p + 96));
            x8 = FOLD(x8, k8, LOAD(p + 112));
        }
        x1 = FOLD(x1, k12, x5);
        x2 = FOLD(x2, k12, x6);
        x3 = FOLD(x3, k12, x7);
        x4 = FOLD(x4, k12, x8);
    }
    for (; length >= 64; length -= 64, p += 64) {
        x1 = FOLD(x1, k12, LOAD(p));
        x2 = FOLD(x2, k12, LOAD(p + 16));
        x3 = FOLD(x3, k12, LOAD(p + 32));
        x4 = FOLD(x4, k12, LOAD(p + 48));
    }
    // Fold the four lanes into one, then each 16 bytes left into it.
    __m128i k34 = LOAD(k + 2);
    x1 = FOLD(x1, k34, x2);
    x1 = FOLD(x1, k34, x3);
    x1 = FOLD(x1, k34, x4);
    for (; length >= 16; length -= 16, p += 16) {
        x1 = FOLD(x1, k34, LOAD(p));
    }
#undef FOLD
    // 128 bits to 64, then Barrett's reduction to 32.
    const __m128i mask = _mm_setr_epi32(-1, 0, -1, 0);
    x2 = _mm_clmulepi64_si128(x1, k34, 0x10);
    x1 = _mm_xor_si128(_mm_srli_si128(x1, 8), x2);
    __m128i k5 = _mm_loadl_epi64((const __m128i *)(k + 4));
    x2 = _mm_srli_si128(x1, 4);
    x1 = _mm_clmulepi64_si128(_mm_and_si128(x1, mask), k5, 0x00);
    x1 = _mm_xor_si128(x1, x2);
    __m128i poly = LOAD(k + 6);
#undef LOAD
    x2 = _mm_clmulepi64_si128(_mm_and_si128(x1, mask), poly, 0x10);
    x2 = _mm_clmulepi64_si128(_mm_and_si128(x2, mask), poly, 0x00);
    x1 = _mm_xor_si128(x1, x2);
    return (uint32_t)_mm_extract_epi32(x1, 1);
}

/// @brief Runs length bytes at p through the CRC-32C register crc with
/// SSE4.2's crc32 instruction, 8 bytes at a time, compiled for SSE4.2
/// alone, so it is called only on a processor that has it.
__attribute__((target("sse4.2"))) static uint32_t crc32c_x86(uint32_t crc, const unsigned char *p, size_t length) {
    uint64_t c = crc;
    for (; length >= 8; length -= 8, p += 8) {
        uint64_t word;
        memcpy(&word, p, sizeof(word));
        c = _mm_crc32_u64(c, word);
    }
    crc = (uint32_t)c;
    for (; length > 0; length--, p++) {
        crc = _mm_crc32_u8(crc, *p);
    }
    return crc;
}

#endif

#if !defined(SHA2_ARM)

/// @brief Reads a big-endian 32-bit word.
static inline uint32_t load32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

#endif

/// @brief Reads a little-endian 32-bit word. Compilers make it one load
/// where that is the byte order.
static inline uint32_t load32le(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

#if !defined(SHA512_ARM)

/// @brief Reads a big-endian 64-bit word.
static inline uint64_t load64(const unsigned char *p) {
    return (uint64_t)p[0] << 56 | (uint64_t)p[1] << 48 | (uint64_t)p[2] << 40 | (uint64_t)p[3] << 32 | (uint64_t)p[4] << 24 | (uint64_t)p[5] << 16 | (uint64_t)p[6] << 8 | (uint64_t)p[7];
}

#endif

/// @brief Hashes blocks 64-byte blocks at p into an MD5 state, in portable
/// c.
/// @details The 64 steps are written out, with the four variables renamed
/// rather than moved, so they stay in registers, and each block's sixteen
/// words are read once, at the start.
static void md5_blocks(void *state_words, const unsigned char *p, size_t blocks) {
    uint32_t *state = state_words;
// Three of the four functions of RFC 1321, F in a shorter equivalent form.
// The second, G, is written into its step as the sum of its two terms,
// which share no bits, so the one that doesn't need b is added while b is
// computed.
#define F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))
#define STEP(f, a, b, c, d, i, s, t) a = b + rotl32(a + f(b, c, d) + w[i] + (t), s)
#define STEPG(a, b, c, d, i, s, t) a = b + rotl32(a + w[i] + (t) + (~(d) & (c)) + ((d) & (b)), s)
    uint32_t w[16];
    for (; blocks > 0; blocks--, p += SOF_DIGEST_MD5_BLOCK) {
        for (int i = 0; i < 16; i++) {
            w[i] = load32le(p + 4 * i);
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        STEP(F, a, b, c, d, 0, 7, 0xd76aa478);
        STEP(F, d, a, b, c, 1, 12, 0xe8c7b756);
        STEP(F, c, d, a, b, 2, 17, 0x242070db);
        STEP(F, b, c, d, a, 3, 22, 0xc1bdceee);
        STEP(F, a, b, c, d, 4, 7, 0xf57c0faf);
        STEP(F, d, a, b, c, 5, 12, 0x4787c62a);
        STEP(F, c, d, a, b, 6, 17, 0xa8304613);
        STEP(F, b, c, d, a, 7, 22, 0xfd469501);
        STEP(F, a, b, c, d, 8, 7, 0x698098d8);
        STEP(F, d, a, b, c, 9, 12, 0x8b44f7af);
        STEP(F, c, d, a, b, 10, 17, 0xffff5bb1);
        STEP(F, b, c, d, a, 11, 22, 0x895cd7be);
        STEP(F, a, b, c, d, 12, 7, 0x6b901122);
        STEP(F, d, a, b, c, 13, 12, 0xfd987193);
        STEP(F, c, d, a, b, 14, 17, 0xa679438e);
        STEP(F, b, c, d, a, 15, 22, 0x49b40821);
        STEPG(a, b, c, d, 1, 5, 0xf61e2562);
        STEPG(d, a, b, c, 6, 9, 0xc040b340);
        STEPG(c, d, a, b, 11, 14, 0x265e5a51);
        STEPG(b, c, d, a, 0, 20, 0xe9b6c7aa);
        STEPG(a, b, c, d, 5, 5, 0xd62f105d);
        STEPG(d, a, b, c, 10, 9, 0x02441453);
        STEPG(c, d, a, b, 15, 14, 0xd8a1e681);
        STEPG(b, c, d, a, 4, 20, 0xe7d3fbc8);
        STEPG(a, b, c, d, 9, 5, 0x21e1cde6);
        STEPG(d, a, b, c, 14, 9, 0xc33707d6);
        STEPG(c, d, a, b, 3, 14, 0xf4d50d87);
        STEPG(b, c, d, a, 8, 20, 0x455a14ed);
        STEPG(a, b, c, d, 13, 5, 0xa9e3e905);
        STEPG(d, a, b, c, 2, 9, 0xfcefa3f8);
        STEPG(c, d, a, b, 7, 14, 0x676f02d9);
        STEPG(b, c, d, a, 12, 20, 0x8d2a4c8a);
        STEP(H, a, b, c, d, 5, 4, 0xfffa3942);
        STEP(H, d, a, b, c, 8, 11, 0x8771f681);
        STEP(H, c, d, a, b, 11, 16, 0x6d9d6122);
        STEP(H, b, c, d, a, 14, 23, 0xfde5380c);
        STEP(H, a, b, c, d, 1, 4, 0xa4beea44);
        STEP(H, d, a, b, c, 4, 11, 0x4bdecfa9);
        STEP(H, c, d, a, b, 7, 16, 0xf6bb4b60);
        STEP(H, b, c, d, a, 10, 23, 0xbebfbc70);
        STEP(H, a, b, c, d, 13, 4, 0x289b7ec6);
        STEP(H, d, a, b, c, 0, 11, 0xeaa127fa);
        STEP(H, c, d, a, b, 3, 16, 0xd4ef3085);
        STEP(H, b, c, d, a, 6, 23, 0x04881d05);
        STEP(H, a, b, c, d, 9, 4, 0xd9d4d039);
        STEP(H, d, a, b, c, 12, 11, 0xe6db99e5);
        STEP(H, c, d, a, b, 15, 16, 0x1fa27cf8);
        STEP(H, b, c, d, a, 2, 23, 0xc4ac5665);
        STEP(I, a, b, c, d, 0, 6, 0xf4292244);
        STEP(I, d, a, b, c, 7, 10, 0x432aff97);
        STEP(I, c, d, a, b, 14, 15, 0xab9423a7);
        STEP(I, b, c, d, a, 5, 21, 0xfc93a039);
        STEP(I, a, b, c, d, 12, 6, 0x655b59c3);
        STEP(I, d, a, b, c, 3, 10, 0x8f0ccc92);
        STEP(I, c, d, a, b, 10, 15, 0xffeff47d);
        STEP(I, b, c, d, a, 1, 21, 0x85845dd1);
        STEP(I, a, b, c, d, 8, 6, 0x6fa87e4f);
        STEP(I, d, a, b, c, 15, 10, 0xfe2ce6e0);
        STEP(I, c, d, a, b, 6, 15, 0xa3014314);
        STEP(I, b, c, d, a, 13, 21, 0x4e0811a1);
        STEP(I, a, b, c, d, 4, 6, 0xf7537e82);
        STEP(I, d, a, b, c, 11, 10, 0xbd3af235);
        STEP(I, c, d, a, b, 2, 15, 0x2ad7d2bb);
        STEP(I, b, c, d, a, 9, 21, 0xeb86d391);
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
    }
#undef F
#undef H
#undef I
#undef STEP
#undef STEPG
}

/// @brief Multiplies a and b, polynomials in the CRCs' reflected form,
/// modulo the reflected polynomial poly, a bit of a at a time.
static uint32_t multmodp(uint32_t poly, uint32_t a, uint32_t b) {
    uint32_t product = 0;
    for (uint32_t m = 1u << 31; m != 0; m >>= 1) {
        if (a & m) {
            product ^= b;
        }
        b = b & 1 ? b >> 1 ^ poly : b >> 1;
    }
    return product;
}

/// @brief Rotates x left by n bits, 0 < n < 32.
static inline uint32_t rotl32(uint32_t x, unsigned n) {
    return x << n | x >> (32 - n);
}

#if !defined(SHA2_ARM)

/// @brief Rotates x right by n bits, 0 < n < 32.
static inline uint32_t rotr32(uint32_t x, unsigned n) {
    return x >> n | x << (32 - n);
}

#endif

#if !defined(SHA512_ARM)

/// @brief Rotates x right by n bits, 0 < n < 64.
static inline uint64_t rotr64(uint64_t x, unsigned n) {
    return x >> n | x << (64 - n);
}

#endif

/// @brief Hashes blocks 64-byte blocks at p into a SHA-1 state, with the
/// fastest code the compiler and processor allow.
static void sha1_blocks(void *state, const unsigned char *p, size_t blocks) {
#if defined(SHA2_ARM)
    sha1_blocks_arm(state, p, blocks);
#else
#if defined(DIGEST_X86)
    if (__builtin_cpu_supports("sha") && __builtin_cpu_supports("sse4.1")) {
        sha1_blocks_x86(state, p, blocks);
        return;
    }
#endif
    sha1_blocks_portable(state, p, blocks);
#endif
}

#if defined(SHA2_ARM)

/// @brief Hashes blocks 64-byte blocks at p into state with ARMv8's SHA-1
/// instructions, four rounds to each.
static void sha1_blocks_arm(uint32_t *state, const unsigned char *p, size_t blocks) {
    const uint32x4_t k0 = vdupq_n_u32(0x5a827999), k1 = vdupq_n_u32(0x6ed9eba1);
    const uint32x4_t k2 = vdupq_n_u32(0x8f1bbcdc), k3 = vdupq_n_u32(0xca62c1d6);
    uint32x4_t abcd = vld1q_u32(state);
    uint32_t e = state[4];
// Four rounds, from round 4 * n, with schedule words m0, the choose,
// parity or majority function op and constant k. The e of the next four is
// a, rotated, from before these. Rounds 0 to 63 also make the schedule's
// words sixteen rounds on, replacing m0, from the four latest groups.
#define ROUNDS4(n, op, k, m0, m1, m2, m3)                             \
    do {                                                              \
        uint32x4_t wk = vaddq_u32(m0, k);                             \
        uint32_t e_next = vsha1h_u32(vgetq_lane_u32(abcd, 0));        \
        abcd = op(abcd, e, wk);                                       \
        e = e_next;                                                   \
        if ((n) < 16) {                                               \
            m0 = vsha1su1q_u32(vsha1su0q_u32(m0, m1, m2), m3);        \
        }                                                             \
    } while (0)
    for (; blocks > 0; blocks--, p += SOF_DIGEST_SHA1_BLOCK) {
        uint32x4_t abcd_before = abcd;
        uint32_t e_before = e;
        uint32x4_t m0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p)));
        uint32x4_t m1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 16)));
        uint32x4_t m2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 32)));
        uint32x4_t m3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 48)));
        ROUNDS4(0, vsha1cq_u32, k0, m0, m1, m2, m3);
        ROUNDS4(1, vsha1cq_u32, k0, m1, m2, m3, m0);
        ROUNDS4(2, vsha1cq_u32, k0, m2, m3, m0, m1);
        ROUNDS4(3, vsha1cq_u32, k0, m3, m0, m1, m2);
        ROUNDS4(4, vsha1cq_u32, k0, m0, m1, m2, m3);
        ROUNDS4(5, vsha1pq_u32, k1, m1, m2, m3, m0);
        ROUNDS4(6, vsha1pq_u32, k1, m2, m3, m0, m1);
        ROUNDS4(7, vsha1pq_u32, k1, m3, m0, m1, m2);
        ROUNDS4(8, vsha1pq_u32, k1, m0, m1, m2, m3);
        ROUNDS4(9, vsha1pq_u32, k1, m1, m2, m3, m0);
        ROUNDS4(10, vsha1mq_u32, k2, m2, m3, m0, m1);
        ROUNDS4(11, vsha1mq_u32, k2, m3, m0, m1, m2);
        ROUNDS4(12, vsha1mq_u32, k2, m0, m1, m2, m3);
        ROUNDS4(13, vsha1mq_u32, k2, m1, m2, m3, m0);
        ROUNDS4(14, vsha1mq_u32, k2, m2, m3, m0, m1);
        ROUNDS4(15, vsha1pq_u32, k3, m3, m0, m1, m2);
        ROUNDS4(16, vsha1pq_u32, k3, m0, m1, m2, m3);
        ROUNDS4(17, vsha1pq_u32, k3, m1, m2, m3, m0);
        ROUNDS4(18, vsha1pq_u32, k3, m2, m3, m0, m1);
        ROUNDS4(19, vsha1pq_u32, k3, m3, m0, m1, m2);
        abcd = vaddq_u32(abcd, abcd_before);
        e += e_before;
    }
#undef ROUNDS4
    vst1q_u32(state, abcd);
    state[4] = e;
}

#else

/// @brief Hashes blocks 64-byte blocks at p into state in portable c.
/// @details The schedule is a ring of 16 words, and the rounds are written
/// out 5 at a time with the variables renamed rather than moved, so the
/// state stays in registers.
static void sha1_blocks_portable(uint32_t *state, const unsigned char *p, size_t blocks) {
// The functions of FIPS 180-4: choose, parity and majority, the first and
// last in shorter equivalent forms.
#define CH(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define PARITY(x, y, z) ((x) ^ (y) ^ (z))
#define MAJ(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
// Schedule word i: read from the block for the first 16 rounds, then made
// from the ring.
#define LOAD(i) (w[(i) & 15] = load32(p + 4 * (i)))
#define UPDATE(i) \
    (w[(i) & 15] = rotl32(w[((i) + 13) & 15] ^ w[((i) + 8) & 15] ^ w[((i) + 2) & 15] ^ w[(i) & 15], 1))
// Round i, with the five variables named in their order for this round.
#define ROUND(a, b, c, d, e, f, k, word)                     \
    do {                                                     \
        e += rotl32(a, 5) + f(b, c, d) + (k) + (word);       \
        b = rotl32(b, 30);                                   \
    } while (0)
#define ROUNDS5(i, f, k, W)                              \
    do {                                                 \
        ROUND(a, b, c, d, e, f, k, W(i));                \
        ROUND(e, a, b, c, d, f, k, W((i) + 1));          \
        ROUND(d, e, a, b, c, f, k, W((i) + 2));          \
        ROUND(c, d, e, a, b, f, k, W((i) + 3));          \
        ROUND(b, c, d, e, a, f, k, W((i) + 4));          \
    } while (0)
    uint32_t w[16];
    for (; blocks > 0; blocks--, p += SOF_DIGEST_SHA1_BLOCK) {
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
        ROUNDS5(0, CH, 0x5a827999, LOAD);
        ROUNDS5(5, CH, 0x5a827999, LOAD);
        ROUNDS5(10, CH, 0x5a827999, LOAD);
        ROUND(a, b, c, d, e, CH, 0x5a827999, LOAD(15));
        ROUND(e, a, b, c, d, CH, 0x5a827999, UPDATE(16));
        ROUND(d, e, a, b, c, CH, 0x5a827999, UPDATE(17));
        ROUND(c, d, e, a, b, CH, 0x5a827999, UPDATE(18));
        ROUND(b, c, d, e, a, CH, 0x5a827999, UPDATE(19));
        for (int i = 20; i < 40; i += 5) {
            ROUNDS5(i, PARITY, 0x6ed9eba1, UPDATE);
        }
        for (int i = 40; i < 60; i += 5) {
            ROUNDS5(i, MAJ, 0x8f1bbcdc, UPDATE);
        }
        for (int i = 60; i < 80; i += 5) {
            ROUNDS5(i, PARITY, 0xca62c1d6, UPDATE);
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }
#undef CH
#undef PARITY
#undef MAJ
#undef LOAD
#undef UPDATE
#undef ROUND
#undef ROUNDS5
}

#endif

#if defined(DIGEST_X86)

/// @brief Hashes blocks 64-byte blocks at p into state with x86's SHA
/// extensions, compiled for them alone, so sha1_blocks() calls it only on a
/// processor that has them.
__attribute__((target("sha,sse4.1"))) static void sha1_blocks_x86(uint32_t *state, const unsigned char *p, size_t blocks) {
    // The instructions keep A to D in one register, A highest, and E in the
    // top of another.
    const __m128i swap = _mm_set_epi64x(0x0001020304050607ULL, 0x08090a0b0c0d0e0fULL);
    __m128i abcd = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i *)state), 0x1b);
    __m128i e0 = _mm_set_epi32((int)state[4], 0, 0, 0);
// Four rounds, from round 4 * n, with schedule words m, function f (0 to
// 3). The E of these comes from A before the last four, through
// sha1nexte, which also adds the words; e_from keeps A for the next four.
// Along the way they finish the words four rounds on (m1, made from m and
// the three groups before), go on with those eight on (m2), and start
// those twelve on (m3).
#define ROUNDS4(n, f, m, m1, m2, m3)                                  \
    do {                                                              \
        e = (n) == 0 ? _mm_add_epi32(e0, m) : _mm_sha1nexte_epu32(e_from, m); \
        e_from = abcd;                                                \
        if ((n) >= 3 && (n) <= 18) {                                  \
            m1 = _mm_sha1msg2_epu32(m1, m);                           \
        }                                                             \
        abcd = _mm_sha1rnds4_epu32(abcd, e, f);                       \
        if ((n) >= 1 && (n) <= 16) {                                  \
            m3 = _mm_sha1msg1_epu32(m3, m);                           \
        }                                                             \
        if ((n) >= 2 && (n) <= 17) {                                  \
            m2 = _mm_xor_si128(m2, m);                                \
        }                                                             \
    } while (0)
    for (; blocks > 0; blocks--, p += SOF_DIGEST_SHA1_BLOCK) {
        __m128i abcd_before = abcd, e0_before = e0, e, e_from = abcd;
        __m128i m0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)p), swap);
        __m128i m1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 16)), swap);
        __m128i m2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 32)), swap);
        __m128i m3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 48)), swap);
        ROUNDS4(0, 0, m0, m1, m2, m3);
        ROUNDS4(1, 0, m1, m2, m3, m0);
        ROUNDS4(2, 0, m2, m3, m0, m1);
        ROUNDS4(3, 0, m3, m0, m1, m2);
        ROUNDS4(4, 0, m0, m1, m2, m3);
        ROUNDS4(5, 1, m1, m2, m3, m0);
        ROUNDS4(6, 1, m2, m3, m0, m1);
        ROUNDS4(7, 1, m3, m0, m1, m2);
        ROUNDS4(8, 1, m0, m1, m2, m3);
        ROUNDS4(9, 1, m1, m2, m3, m0);
        ROUNDS4(10, 2, m2, m3, m0, m1);
        ROUNDS4(11, 2, m3, m0, m1, m2);
        ROUNDS4(12, 2, m0, m1, m2, m3);
        ROUNDS4(13, 2, m1, m2, m3, m0);
        ROUNDS4(14, 2, m2, m3, m0, m1);
        ROUNDS4(15, 3, m3, m0, m1, m2);
        ROUNDS4(16, 3, m0, m1, m2, m3);
        ROUNDS4(17, 3, m1, m2, m3, m0);
        ROUNDS4(18, 3, m2, m3, m0, m1);
        ROUNDS4(19, 3, m3, m0, m1, m2);
        e0 = _mm_sha1nexte_epu32(e_from, e0_before);
        abcd = _mm_add_epi32(abcd, abcd_before);
    }
#undef ROUNDS4
    _mm_storeu_si128((__m128i *)state, _mm_shuffle_epi32(abcd, 0x1b));
    state[4] = (uint32_t)_mm_extract_epi32(e0, 3);
}

#endif

/// @brief Hashes blocks 64-byte blocks at p into a SHA-256 state, with the
/// fastest code the compiler and processor allow.
static void sha256_blocks(void *state, const unsigned char *p, size_t blocks) {
#if defined(SHA2_ARM)
    sha256_blocks_arm(state, p, blocks);
#else
#if defined(DIGEST_X86)
    if (__builtin_cpu_supports("sha") && __builtin_cpu_supports("sse4.1")) {
        sha256_blocks_x86(state, p, blocks);
        return;
    }
#endif
    sha256_blocks_portable(state, p, blocks);
#endif
}

#if defined(SHA2_ARM)

/// @brief Hashes blocks 64-byte blocks at p into state with ARMv8's SHA-256
/// instructions, four rounds to each pair.
static void sha256_blocks_arm(uint32_t *state, const unsigned char *p, size_t blocks) {
    uint32x4_t abcd = vld1q_u32(state);
    uint32x4_t efgh = vld1q_u32(state + 4);
// Four rounds with schedule words m, from round i.
#define ROUNDS4(m, i)                                          \
    do {                                                       \
        uint32x4_t wk = vaddq_u32(m, vld1q_u32(sha256_k + i)); \
        uint32x4_t old = abcd;                                 \
        abcd = vsha256hq_u32(abcd, efgh, wk);                  \
        efgh = vsha256h2q_u32(efgh, old, wk);                  \
    } while (0)
// The schedule's next four words, replacing the oldest four, w0.
#define SCHEDULE4(w0, w4, w8, w12) w0 = vsha256su1q_u32(vsha256su0q_u32(w0, w4), w8, w12)
    for (; blocks > 0; blocks--, p += SOF_DIGEST_SHA256_BLOCK) {
        uint32x4_t abcd_before = abcd, efgh_before = efgh;
        uint32x4_t m0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p)));
        uint32x4_t m1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 16)));
        uint32x4_t m2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 32)));
        uint32x4_t m3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 48)));
        for (int i = 0; i < 48; i += 16) {
            ROUNDS4(m0, i);
            SCHEDULE4(m0, m1, m2, m3);
            ROUNDS4(m1, i + 4);
            SCHEDULE4(m1, m2, m3, m0);
            ROUNDS4(m2, i + 8);
            SCHEDULE4(m2, m3, m0, m1);
            ROUNDS4(m3, i + 12);
            SCHEDULE4(m3, m0, m1, m2);
        }
        ROUNDS4(m0, 48);
        ROUNDS4(m1, 52);
        ROUNDS4(m2, 56);
        ROUNDS4(m3, 60);
        abcd = vaddq_u32(abcd, abcd_before);
        efgh = vaddq_u32(efgh, efgh_before);
    }
#undef ROUNDS4
#undef SCHEDULE4
    vst1q_u32(state, abcd);
    vst1q_u32(state + 4, efgh);
}

#else

/// @brief Hashes blocks 64-byte blocks at p into state in portable c.
/// @details The schedule is a ring of 16 words, and the rounds are written
/// out 8 at a time with the variables renamed rather than moved, so the
/// state stays in registers.
static void sha256_blocks_portable(uint32_t *state, const unsigned char *p, size_t blocks) {
// The functions of FIPS 180-4, Ch and Maj in their shorter equivalent forms.
#define CH(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define MAJ(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define SIGMA0(x) (rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22))
#define SIGMA1(x) (rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25))
#define SMALL0(x) (rotr32(x, 7) ^ rotr32(x, 18) ^ (x) >> 3)
#define SMALL1(x) (rotr32(x, 17) ^ rotr32(x, 19) ^ (x) >> 10)
// Schedule word i: read from the block for the first 16 rounds, then made
// from the ring.
#define LOAD(i) (w[(i) & 15] = load32(p + 4 * (i)))
#define UPDATE(i) (w[(i) & 15] += SMALL0(w[((i) + 1) & 15]) + w[((i) + 9) & 15] + SMALL1(w[((i) + 14) & 15]))
// Round i, with the eight variables named in their order for this round.
#define ROUND(a, b, c, d, e, f, g, h, i, word)                            \
    do {                                                                  \
        uint32_t t1 = h + SIGMA1(e) + CH(e, f, g) + sha256_k[i] + (word); \
        d += t1;                                                          \
        h = t1 + SIGMA0(a) + MAJ(a, b, c);                                \
    } while (0)
#define ROUNDS8(i, W)                                   \
    do {                                                \
        ROUND(a, b, c, d, e, f, g, h, i, W(i));         \
        ROUND(h, a, b, c, d, e, f, g, i + 1, W(i + 1)); \
        ROUND(g, h, a, b, c, d, e, f, i + 2, W(i + 2)); \
        ROUND(f, g, h, a, b, c, d, e, i + 3, W(i + 3)); \
        ROUND(e, f, g, h, a, b, c, d, i + 4, W(i + 4)); \
        ROUND(d, e, f, g, h, a, b, c, i + 5, W(i + 5)); \
        ROUND(c, d, e, f, g, h, a, b, i + 6, W(i + 6)); \
        ROUND(b, c, d, e, f, g, h, a, i + 7, W(i + 7)); \
    } while (0)
    uint32_t w[16];
    for (; blocks > 0; blocks--, p += SOF_DIGEST_SHA256_BLOCK) {
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 16; i += 8) {
            ROUNDS8(i, LOAD);
        }
        for (int i = 16; i < 64; i += 8) {
            ROUNDS8(i, UPDATE);
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
#undef CH
#undef MAJ
#undef SIGMA0
#undef SIGMA1
#undef SMALL0
#undef SMALL1
#undef LOAD
#undef UPDATE
#undef ROUND
#undef ROUNDS8
}

#endif

#if defined(DIGEST_X86)

/// @brief Hashes blocks 64-byte blocks at p into state with x86's SHA
/// extensions, compiled for them alone, so sha256_blocks() calls it only on
/// a processor that has them.
__attribute__((target("sha,sse4.1"))) static void sha256_blocks_x86(uint32_t *state, const unsigned char *p, size_t blocks) {
    // The instructions keep the state as ABEF and CDGH, with A highest.
    const __m128i swap = _mm_set_epi64x(0x0c0d0e0f08090a0bULL, 0x0405060700010203ULL);
    __m128i dcba = _mm_loadu_si128((const __m128i *)state);
    __m128i hgfe = _mm_loadu_si128((const __m128i *)(state + 4));
    __m128i cdab = _mm_shuffle_epi32(dcba, 0xb1);
    __m128i efgh = _mm_shuffle_epi32(hgfe, 0x1b);
    __m128i abef = _mm_alignr_epi8(cdab, efgh, 8);
    __m128i cdgh = _mm_blend_epi16(efgh, cdab, 0xf0);
// Four rounds, from round 4 * n, with schedule words m. Along the way they
// finish the words four rounds on (next) with the two latest groups (prev
// and m), and start those twelve on (prev).
#define ROUNDS4(n, m, prev, next)                                                              \
    do {                                                                                       \
        __m128i wk = _mm_add_epi32(m, _mm_loadu_si128((const __m128i *)(sha256_k + 4 * (n)))); \
        cdgh = _mm_sha256rnds2_epu32(cdgh, abef, wk);                                          \
        if ((n) >= 3 && (n) <= 14) {                                                           \
            next = _mm_add_epi32(next, _mm_alignr_epi8(m, prev, 4));                           \
            next = _mm_sha256msg2_epu32(next, m);                                              \
        }                                                                                      \
        abef = _mm_sha256rnds2_epu32(abef, cdgh, _mm_shuffle_epi32(wk, 0x0e));                 \
        if ((n) >= 1 && (n) <= 12) {                                                           \
            prev = _mm_sha256msg1_epu32(prev, m);                                              \
        }                                                                                      \
    } while (0)
    for (; blocks > 0; blocks--, p += SOF_DIGEST_SHA256_BLOCK) {
        __m128i abef_before = abef, cdgh_before = cdgh;
        __m128i m0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)p), swap);
        __m128i m1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 16)), swap);
        __m128i m2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 32)), swap);
        __m128i m3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 48)), swap);
        ROUNDS4(0, m0, m3, m1);
        ROUNDS4(1, m1, m0, m2);
        ROUNDS4(2, m2, m1, m3);
        ROUNDS4(3, m3, m2, m0);
        ROUNDS4(4, m0, m3, m1);
        ROUNDS4(5, m1, m0, m2);
        ROUNDS4(6, m2, m1, m3);
        ROUNDS4(7, m3, m2, m0);
        ROUNDS4(8, m0, m3, m1);
        ROUNDS4(9, m1, m0, m2);
        ROUNDS4(10, m2, m1, m3);
        ROUNDS4(11, m3, m2, m0);
        ROUNDS4(12, m0, m3, m1);
        ROUNDS4(13, m1, m0, m2);
        ROUNDS4(14, m2, m1, m3);
        ROUNDS4(15, m3, m2, m0);
        abef = _mm_add_epi32(abef, abef_before);
        cdgh = _mm_add_epi32(cdgh, cdgh_before);
    }
#undef ROUNDS4
    __m128i feba = _mm_shuffle_epi32(abef, 0x1b);
    __m128i dchg = _mm_shuffle_epi32(cdgh, 0xb1);
    _mm_storeu_si128((__m128i *)state, _mm_blend_epi16(feba, dchg, 0xf0));
    _mm_storeu_si128((__m128i *)(state + 4), _mm_alignr_epi8(dchg, feba, 8));
}

#endif

#if defined(SHA512_ARM)

/// @brief Hashes blocks 128-byte blocks at p into a SHA-512 state with
/// ARMv8.2's SHA-512 instructions, two rounds to each sha512h and sha512h2.
/// @details The state is four pairs of words, ab, cd, ef and gh, in s[0] to
/// s[3], and a fifth pair, s[4], takes each two rounds' new pair, so the
/// pairs move round the five as they would through registers, coming back
/// to their places every five steps, eight times in a block. The schedule
/// is eight pairs, m[0] to m[7], each replaced by the pair sixteen rounds
/// on once it is used, until round 64.
static void sha512_blocks(void *state_words, const unsigned char *p, size_t blocks) {
    uint64_t *state = state_words;
    uint64x2_t s[5];
    s[0] = vld1q_u64(state);
    s[1] = vld1q_u64(state + 2);
    s[2] = vld1q_u64(state + 4);
    s[3] = vld1q_u64(state + 6);
    s[4] = s[0];
// Rounds 2 * j and 2 * j + 1, with the pairs in s[i0] to s[i4].
#define ROUNDS2(j, i0, i1, i2, i3, i4)                                                              \
    do {                                                                                            \
        uint64x2_t wk = vaddq_u64(m[(j) & 7], vld1q_u64(sha512_k + 2 * (j)));                       \
        uint64x2_t fg = vextq_u64(s[i2], s[i3], 1);                                                 \
        uint64x2_t de = vextq_u64(s[i1], s[i2], 1);                                                 \
        s[i3] = vaddq_u64(s[i3], vextq_u64(wk, wk, 1));                                             \
        if ((j) < 32) {                                                                             \
            uint64x2_t w9 = vextq_u64(m[((j) + 4) & 7], m[((j) + 5) & 7], 1);                       \
            m[(j) & 7] = vsha512su0q_u64(m[(j) & 7], m[((j) + 1) & 7]);                             \
            s[i3] = vsha512hq_u64(s[i3], fg, de);                                                   \
            m[(j) & 7] = vsha512su1q_u64(m[(j) & 7], m[((j) + 7) & 7], w9);                         \
        } else {                                                                                    \
            s[i3] = vsha512hq_u64(s[i3], fg, de);                                                   \
        }                                                                                           \
        s[i4] = vaddq_u64(s[i1], s[i3]);                                                            \
        s[i3] = vsha512h2q_u64(s[i3], s[i1], s[i0]);                                                \
    } while (0)
#define ROUNDS10(j)                       \
    do {                                  \
        ROUNDS2((j), 0, 1, 2, 3, 4);      \
        ROUNDS2((j) + 1, 3, 0, 4, 2, 1);  \
        ROUNDS2((j) + 2, 2, 3, 1, 4, 0);  \
        ROUNDS2((j) + 3, 4, 2, 0, 1, 3);  \
        ROUNDS2((j) + 4, 1, 4, 3, 0, 2);  \
    } while (0)
    for (; blocks > 0; blocks--, p += SOF_DIGEST_SHA512_BLOCK) {
        uint64x2_t before[4] = {s[0], s[1], s[2], s[3]};
        uint64x2_t m[8];
        for (int i = 0; i < 8; i++) {
            m[i] = vreinterpretq_u64_u8(vrev64q_u8(vld1q_u8(p + 16 * i)));
        }
        ROUNDS10(0);
        ROUNDS10(5);
        ROUNDS10(10);
        ROUNDS10(15);
        ROUNDS10(20);
        ROUNDS10(25);
        ROUNDS10(30);
        ROUNDS10(35);
        for (int i = 0; i < 4; i++) {
            s[i] = vaddq_u64(s[i], before[i]);
        }
    }
#undef ROUNDS2
#undef ROUNDS10
    vst1q_u64(state, s[0]);
    vst1q_u64(state + 2, s[1]);
    vst1q_u64(state + 4, s[2]);
    vst1q_u64(state + 6, s[3]);
}

#else

/// @brief Hashes blocks 128-byte blocks at p into a SHA-512 state in
/// portable c, as sha256_blocks_portable() does SHA-256's, with 64-bit
/// words and 80 rounds.
static void sha512_blocks(void *state_words, const unsigned char *p, size_t blocks) {
    uint64_t *state = state_words;
#define CH(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define MAJ(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define SIGMA0(x) (rotr64(x, 28) ^ rotr64(x, 34) ^ rotr64(x, 39))
#define SIGMA1(x) (rotr64(x, 14) ^ rotr64(x, 18) ^ rotr64(x, 41))
#define SMALL0(x) (rotr64(x, 1) ^ rotr64(x, 8) ^ (x) >> 7)
#define SMALL1(x) (rotr64(x, 19) ^ rotr64(x, 61) ^ (x) >> 6)
#define LOAD(i) (w[(i) & 15] = load64(p + 8 * (i)))
#define UPDATE(i) (w[(i) & 15] += SMALL0(w[((i) + 1) & 15]) + w[((i) + 9) & 15] + SMALL1(w[((i) + 14) & 15]))
#define ROUND(a, b, c, d, e, f, g, h, i, word)                            \
    do {                                                                  \
        uint64_t t1 = h + SIGMA1(e) + CH(e, f, g) + sha512_k[i] + (word); \
        d += t1;                                                          \
        h = t1 + SIGMA0(a) + MAJ(a, b, c);                                \
    } while (0)
#define ROUNDS8(i, W)                                   \
    do {                                                \
        ROUND(a, b, c, d, e, f, g, h, i, W(i));         \
        ROUND(h, a, b, c, d, e, f, g, i + 1, W(i + 1)); \
        ROUND(g, h, a, b, c, d, e, f, i + 2, W(i + 2)); \
        ROUND(f, g, h, a, b, c, d, e, i + 3, W(i + 3)); \
        ROUND(e, f, g, h, a, b, c, d, i + 4, W(i + 4)); \
        ROUND(d, e, f, g, h, a, b, c, i + 5, W(i + 5)); \
        ROUND(c, d, e, f, g, h, a, b, i + 6, W(i + 6)); \
        ROUND(b, c, d, e, f, g, h, a, i + 7, W(i + 7)); \
    } while (0)
    uint64_t w[16];
    for (; blocks > 0; blocks--, p += SOF_DIGEST_SHA512_BLOCK) {
        uint64_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint64_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 16; i += 8) {
            ROUNDS8(i, LOAD);
        }
        for (int i = 16; i < 80; i += 8) {
            ROUNDS8(i, UPDATE);
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
#undef CH
#undef MAJ
#undef SIGMA0
#undef SIGMA1
#undef SMALL0
#undef SMALL1
#undef LOAD
#undef UPDATE
#undef ROUND
#undef ROUNDS8
}

#endif

/// @brief Pads a copy of a SHA-512-family stream and writes the first size
/// bytes of its digest, or that of no data if sha is NULL.
static void sha512_output(const sof_digest_sha512_t *sha, unsigned char *digest, size_t size) {
    uint64_t state[8];
    memcpy(state, sha->state, sizeof(state));
    stream_pad(state, sha->buffer, sha->buffered, SOF_DIGEST_SHA512_BLOCK, sha512_blocks, sha->length, 0);
    unsigned char out[SOF_DIGEST_SHA512_SIZE];
    for (int i = 0; i < 8; i++) {
        store64(out + 8 * i, state[i]);
    }
    memcpy(digest, out, size);
}

/// @brief Writes a big-endian 32-bit word.
static inline void store32(unsigned char *p, uint32_t x) {
    p[0] = (unsigned char)(x >> 24);
    p[1] = (unsigned char)(x >> 16);
    p[2] = (unsigned char)(x >> 8);
    p[3] = (unsigned char)x;
}

/// @brief Writes a little-endian 32-bit word.
static inline void store32le(unsigned char *p, uint32_t x) {
    p[0] = (unsigned char)x;
    p[1] = (unsigned char)(x >> 8);
    p[2] = (unsigned char)(x >> 16);
    p[3] = (unsigned char)(x >> 24);
}

/// @brief Writes a big-endian 64-bit word.
static inline void store64(unsigned char *p, uint64_t x) {
    store32(p, (uint32_t)(x >> 32));
    store32(p + 4, (uint32_t)x);
}

/// @brief Hashes the end of a stream into state, a copy of its state: the
/// buffered bytes, a 1 bit, 0 bits, and the length in bits, filling one
/// block, or two if the length doesn't fit after the data.
/// @details The length takes 8 bytes, little-endian for MD5 and big-endian
/// for SHA-1 and SHA-256, or 16 bytes, big-endian, with a block of 128
/// bytes, for SHA-512.
static void stream_pad(void *state, const unsigned char *buffer, size_t buffered, size_t block, blocks_func_t blocks_func, uint64_t length, int little_endian) {
    unsigned char last[2 * SOF_DIGEST_SHA512_BLOCK];
    size_t field = block == SOF_DIGEST_SHA512_BLOCK ? 16 : 8;
    size_t n = buffered;
    memcpy(last, buffer, n);
    last[n++] = 0x80;
    size_t total = n + field <= block ? block : 2 * block;
    memset(last + n, 0, total - n);
    uint64_t bits = length << 3;
    if (little_endian) {
        store32le(last + total - 8, (uint32_t)bits);
        store32le(last + total - 4, (uint32_t)(bits >> 32));
    } else {
        if (field == 16) {
            store64(last + total - 16, length >> 61);
        }
        store64(last + total - 8, bits);
    }
    blocks_func(state, last, total / block);
}

/// @brief Gives a stream with a buffer of block bytes, buffered of them
/// full, the next length bytes at p: fills the block already started, and
/// hashes it once it is whole, hashes whole blocks where they are, and
/// keeps the rest.
static void stream_update(void *state, unsigned char *buffer, size_t *buffered, size_t block, blocks_func_t blocks_func, const unsigned char *p, size_t length) {
    if (*buffered > 0) {
        size_t take = block - *buffered;
        if (take > length) {
            take = length;
        }
        memcpy(buffer + *buffered, p, take);
        *buffered += take;
        p += take;
        length -= take;
        if (*buffered < block) {
            return;
        }
        blocks_func(state, buffer, 1);
        *buffered = 0;
    }
    size_t blocks = length / block;
    if (blocks > 0) {
        blocks_func(state, p, blocks);
        p += blocks * block;
        length -= blocks * block;
    }
    memcpy(buffer, p, length);
    *buffered = length;
}

/// @brief Returns the value of hex digit c, in either case, or -1.
static int unhex(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

uint32_t sof_digest_crc32(const void *data, size_t length) {
    return sof_digest_crc32_extend(0, data, length);
}

uint32_t sof_digest_crc32_combine(uint32_t crc1, uint32_t crc2, uint64_t length2) {
    return crc_shift(SOF_DIGEST_CRC32_POLY, sof_digest_crc32_x8n, crc1, length2) ^ crc2;
}

uint32_t sof_digest_crc32_extend(uint32_t crc, const void *data, size_t length) {
    if (data == NULL) {
        return crc;
    }
    return ~crc32_register(~crc, data, length);
}

uint32_t sof_digest_crc32_final(const sof_digest_crc32_t *crc) {
    return crc == NULL ? 0 : crc->crc;
}

void sof_digest_crc32_init(sof_digest_crc32_t *crc) {
    if (crc == NULL) {
        return;
    }
    crc->crc = 0;
    crc->length = 0;
}

void sof_digest_crc32_update(sof_digest_crc32_t *crc, const void *data, size_t length) {
    if (crc == NULL || data == NULL) {
        return;
    }
    crc->crc = sof_digest_crc32_extend(crc->crc, data, length);
    crc->length += length;
}

uint32_t sof_digest_crc32c(const void *data, size_t length) {
    return sof_digest_crc32c_extend(0, data, length);
}

uint32_t sof_digest_crc32c_combine(uint32_t crc1, uint32_t crc2, uint64_t length2) {
    return crc_shift(SOF_DIGEST_CRC32C_POLY, sof_digest_crc32c_x8n, crc1, length2) ^ crc2;
}

uint32_t sof_digest_crc32c_extend(uint32_t crc, const void *data, size_t length) {
    if (data == NULL) {
        return crc;
    }
    return ~crc32c_register(~crc, data, length);
}

uint32_t sof_digest_crc32c_final(const sof_digest_crc32c_t *crc) {
    return crc == NULL ? 0 : crc->crc;
}

void sof_digest_crc32c_init(sof_digest_crc32c_t *crc) {
    if (crc == NULL) {
        return;
    }
    crc->crc = 0;
    crc->length = 0;
}

void sof_digest_crc32c_update(sof_digest_crc32c_t *crc, const void *data, size_t length) {
    if (crc == NULL || data == NULL) {
        return;
    }
    crc->crc = sof_digest_crc32c_extend(crc->crc, data, length);
    crc->length += length;
}

int sof_digest_from_hex(const char *hex, size_t length, void *bytes) {
    if (length == 0) {
        return 0;
    }
    if (hex == NULL || bytes == NULL || length % 2 != 0) {
        return -1;
    }
    // Check every digit before writing any byte.
    for (size_t i = 0; i < length; i++) {
        if (unhex(hex[i]) < 0) {
            return -1;
        }
    }
    unsigned char *out = bytes;
    for (size_t i = 0; i < length; i += 2) {
        out[i / 2] = (unsigned char)(unhex(hex[i]) << 4 | unhex(hex[i + 1]));
    }
    return 0;
}

void sof_digest_md5(const void *data, size_t length, unsigned char *digest) {
    sof_digest_md5_t md5;
    sof_digest_md5_init(&md5);
    sof_digest_md5_update(&md5, data, length);
    sof_digest_md5_final(&md5, digest);
}

void sof_digest_md5_final(const sof_digest_md5_t *md5, unsigned char *digest) {
    if (digest == NULL) {
        return;
    }
    if (md5 == NULL) {
        sof_digest_md5(NULL, 0, digest);
        return;
    }
    uint32_t state[4];
    memcpy(state, md5->state, sizeof(state));
    stream_pad(state, md5->buffer, md5->buffered, SOF_DIGEST_MD5_BLOCK, md5_blocks, md5->length, 1);
    for (int i = 0; i < 4; i++) {
        store32le(digest + 4 * i, state[i]);
    }
}

void sof_digest_md5_init(sof_digest_md5_t *md5) {
    if (md5 == NULL) {
        return;
    }
    memcpy(md5->state, md5_initial, sizeof(md5->state));
    md5->length = 0;
    md5->buffered = 0;
    memset(md5->buffer, 0, sizeof(md5->buffer));
}

void sof_digest_md5_update(sof_digest_md5_t *md5, const void *data, size_t length) {
    if (md5 == NULL || data == NULL || length == 0) {
        return;
    }
    md5->length += length;
    stream_update(md5->state, md5->buffer, &md5->buffered, SOF_DIGEST_MD5_BLOCK, md5_blocks, data, length);
}

void sof_digest_sha1(const void *data, size_t length, unsigned char *digest) {
    sof_digest_sha1_t sha;
    sof_digest_sha1_init(&sha);
    sof_digest_sha1_update(&sha, data, length);
    sof_digest_sha1_final(&sha, digest);
}

void sof_digest_sha1_final(const sof_digest_sha1_t *sha, unsigned char *digest) {
    if (digest == NULL) {
        return;
    }
    if (sha == NULL) {
        sof_digest_sha1(NULL, 0, digest);
        return;
    }
    uint32_t state[5];
    memcpy(state, sha->state, sizeof(state));
    stream_pad(state, sha->buffer, sha->buffered, SOF_DIGEST_SHA1_BLOCK, sha1_blocks, sha->length, 0);
    for (int i = 0; i < 5; i++) {
        store32(digest + 4 * i, state[i]);
    }
}

void sof_digest_sha1_init(sof_digest_sha1_t *sha) {
    if (sha == NULL) {
        return;
    }
    memcpy(sha->state, sha1_initial, sizeof(sha->state));
    sha->length = 0;
    sha->buffered = 0;
    memset(sha->buffer, 0, sizeof(sha->buffer));
}

void sof_digest_sha1_update(sof_digest_sha1_t *sha, const void *data, size_t length) {
    if (sha == NULL || data == NULL || length == 0) {
        return;
    }
    sha->length += length;
    stream_update(sha->state, sha->buffer, &sha->buffered, SOF_DIGEST_SHA1_BLOCK, sha1_blocks, data, length);
}

void sof_digest_sha224(const void *data, size_t length, unsigned char *digest) {
    sof_digest_sha224_t sha;
    sof_digest_sha224_init(&sha);
    sof_digest_sha224_update(&sha, data, length);
    sof_digest_sha224_final(&sha, digest);
}

void sof_digest_sha224_final(const sof_digest_sha224_t *sha, unsigned char *digest) {
    if (digest == NULL) {
        return;
    }
    if (sha == NULL) {
        sof_digest_sha224(NULL, 0, digest);
        return;
    }
    unsigned char full[SOF_DIGEST_SHA256_SIZE];
    sof_digest_sha256_final(sha, full);
    memcpy(digest, full, SOF_DIGEST_SHA224_SIZE);
}

void sof_digest_sha224_init(sof_digest_sha224_t *sha) {
    if (sha == NULL) {
        return;
    }
    sof_digest_sha256_init(sha);
    memcpy(sha->state, sha224_initial, sizeof(sha->state));
}

void sof_digest_sha224_update(sof_digest_sha224_t *sha, const void *data, size_t length) {
    sof_digest_sha256_update(sha, data, length);
}

void sof_digest_sha256(const void *data, size_t length, unsigned char *digest) {
    sof_digest_sha256_t sha;
    sof_digest_sha256_init(&sha);
    sof_digest_sha256_update(&sha, data, length);
    sof_digest_sha256_final(&sha, digest);
}

void sof_digest_sha256_final(const sof_digest_sha256_t *sha, unsigned char *digest) {
    if (digest == NULL) {
        return;
    }
    if (sha == NULL) {
        sof_digest_sha256(NULL, 0, digest);
        return;
    }
    // Pad a copy, so the stream can go on.
    uint32_t state[8];
    memcpy(state, sha->state, sizeof(state));
    stream_pad(state, sha->buffer, sha->buffered, SOF_DIGEST_SHA256_BLOCK, sha256_blocks, sha->length, 0);
    for (int i = 0; i < 8; i++) {
        store32(digest + 4 * i, state[i]);
    }
}

void sof_digest_sha256_init(sof_digest_sha256_t *sha) {
    if (sha == NULL) {
        return;
    }
    memcpy(sha->state, sha256_initial, sizeof(sha->state));
    sha->length = 0;
    sha->buffered = 0;
    memset(sha->buffer, 0, sizeof(sha->buffer));
}

void sof_digest_sha256_update(sof_digest_sha256_t *sha, const void *data, size_t length) {
    if (sha == NULL || data == NULL || length == 0) {
        return;
    }
    sha->length += length;
    stream_update(sha->state, sha->buffer, &sha->buffered, SOF_DIGEST_SHA256_BLOCK, sha256_blocks, data, length);
}

void sof_digest_sha384(const void *data, size_t length, unsigned char *digest) {
    sof_digest_sha384_t sha;
    sof_digest_sha384_init(&sha);
    sof_digest_sha384_update(&sha, data, length);
    sof_digest_sha384_final(&sha, digest);
}

void sof_digest_sha384_final(const sof_digest_sha384_t *sha, unsigned char *digest) {
    if (digest == NULL) {
        return;
    }
    if (sha == NULL) {
        sof_digest_sha384(NULL, 0, digest);
        return;
    }
    sha512_output(sha, digest, SOF_DIGEST_SHA384_SIZE);
}

void sof_digest_sha384_init(sof_digest_sha384_t *sha) {
    if (sha == NULL) {
        return;
    }
    sof_digest_sha512_init(sha);
    memcpy(sha->state, sha384_initial, sizeof(sha->state));
}

void sof_digest_sha384_update(sof_digest_sha384_t *sha, const void *data, size_t length) {
    sof_digest_sha512_update(sha, data, length);
}

void sof_digest_sha512(const void *data, size_t length, unsigned char *digest) {
    sof_digest_sha512_t sha;
    sof_digest_sha512_init(&sha);
    sof_digest_sha512_update(&sha, data, length);
    sof_digest_sha512_final(&sha, digest);
}

void sof_digest_sha512_256(const void *data, size_t length, unsigned char *digest) {
    sof_digest_sha512_256_t sha;
    sof_digest_sha512_256_init(&sha);
    sof_digest_sha512_256_update(&sha, data, length);
    sof_digest_sha512_256_final(&sha, digest);
}

void sof_digest_sha512_256_final(const sof_digest_sha512_256_t *sha, unsigned char *digest) {
    if (digest == NULL) {
        return;
    }
    if (sha == NULL) {
        sof_digest_sha512_256(NULL, 0, digest);
        return;
    }
    sha512_output(sha, digest, SOF_DIGEST_SHA512_256_SIZE);
}

void sof_digest_sha512_256_init(sof_digest_sha512_256_t *sha) {
    if (sha == NULL) {
        return;
    }
    sof_digest_sha512_init(sha);
    memcpy(sha->state, sha512_256_initial, sizeof(sha->state));
}

void sof_digest_sha512_256_update(sof_digest_sha512_256_t *sha, const void *data, size_t length) {
    sof_digest_sha512_update(sha, data, length);
}

void sof_digest_sha512_final(const sof_digest_sha512_t *sha, unsigned char *digest) {
    if (digest == NULL) {
        return;
    }
    if (sha == NULL) {
        sof_digest_sha512(NULL, 0, digest);
        return;
    }
    sha512_output(sha, digest, SOF_DIGEST_SHA512_SIZE);
}

void sof_digest_sha512_init(sof_digest_sha512_t *sha) {
    if (sha == NULL) {
        return;
    }
    memcpy(sha->state, sha512_initial, sizeof(sha->state));
    sha->length = 0;
    sha->buffered = 0;
    memset(sha->buffer, 0, sizeof(sha->buffer));
}

void sof_digest_sha512_update(sof_digest_sha512_t *sha, const void *data, size_t length) {
    if (sha == NULL || data == NULL || length == 0) {
        return;
    }
    sha->length += length;
    stream_update(sha->state, sha->buffer, &sha->buffered, SOF_DIGEST_SHA512_BLOCK, sha512_blocks, data, length);
}

char *sof_digest_to_hex(const void *bytes, size_t length, char *hex) {
    static const char digits[] = "0123456789abcdef";
    if (hex == NULL) {
        return NULL;
    }
    if (bytes == NULL) {
        length = 0;
    }
    const unsigned char *in = bytes;
    for (size_t i = 0; i < length; i++) {
        hex[2 * i] = digits[in[i] >> 4];
        hex[2 * i + 1] = digits[in[i] & 0x0f];
    }
    hex[2 * length] = '\0';
    return hex;
}
