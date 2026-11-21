/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_utf.h.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 *
 * Every code point is encoded and decoded in UTF-8 and UTF-16, and every
 * sequence of one to three bytes is decoded and checked against the set of
 * prefixes of valid UTF-8, built here from a reference encoder that doesn't
 * use the library. Strings of random code points are converted between all
 * three encodings and checked against the reference encoders. UTF-8 with
 * runs of ASCII and every kind of error, at every alignment and cut off at
 * every length, is validated, counted and converted, and checked against
 * versions of those functions that decode a sequence at a time with a
 * reference decoder.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sof_bit.h"
#include "sof_utf.h"
#include "test.h"

/// @brief Random strings tried by each string test.
#define ROUNDS 500

/// @brief The most code points in a random string.
#define LENGTH_MAX 64

/// @brief Random four-byte sequences decoded.
#define SEQUENCES 1000000

/// @brief Random strings of ASCII, code points and errors checked against
/// the reference functions, and the most bytes in one.
#define MIXED_ROUNDS 3000
#define MIXED_MAX 160

/// @brief The value test buffers are filled with, to catch writes past what
/// a function should write.
#define CANARY 0xa5

/// @brief Words in a bitmap of n bits.
#define WORDS(n) (((n) + SOF_BIT_BITS - 1) / SOF_BIT_BITS)

/// @brief The sequences of one, two and three bytes that start a valid
/// UTF-8 encoding, each a bit indexed by its bytes read as a big-endian
/// number. Built by test_prefixes().
static sof_bit_t prefixes_1[WORDS((size_t)1 << 8)];
static sof_bit_t prefixes_2[WORDS((size_t)1 << 16)];
static sof_bit_t prefixes_3[WORDS((size_t)1 << 24)];
static sof_bit_t *const prefixes[4] = {NULL, prefixes_1, prefixes_2, prefixes_3};

/// @brief Sets bit i of map.
static void map_set(sof_bit_t *map, size_t i) {
    map[i / SOF_BIT_BITS] = sof_bit_set(map[i / SOF_BIT_BITS], (unsigned)(i % SOF_BIT_BITS));
}

/// @brief Reads bit i of map.
static int map_test(const sof_bit_t *map, size_t i) {
    return sof_bit_test(map[i / SOF_BIT_BITS], (unsigned)(i % SOF_BIT_BITS));
}

/// @brief The first n bytes of s as a big-endian number.
static size_t prefix_index(const unsigned char *s, size_t n) {
    size_t index = 0;
    for (size_t i = 0; i < n; i++) {
        index = index << 8 | s[i];
    }
    return index;
}

/// @brief Returns 1 if code is a Unicode scalar value, without the library.
static int ref_valid(uint32_t code) {
    return code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff);
}

/// @brief Encodes a valid code point in UTF-8 without the library, and
/// returns the bytes written.
static size_t ref_utf8(uint32_t code, unsigned char *out) {
    if (code < 0x80) {
        out[0] = (unsigned char)code;
        return 1;
    }
    size_t n = code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
    for (size_t i = n - 1; i > 0; i--) {
        out[i] = (unsigned char)(0x80 + code % 64);
        code /= 64;
    }
    // The lead is n one bits, a zero and the rest of the value.
    out[0] = (unsigned char)(((0xff00 >> n) & 0xff) + code);
    return n;
}

/// @brief Encodes a valid code point in UTF-16 without the library, and
/// returns the units written.
static size_t ref_utf16(uint32_t code, uint16_t *out) {
    if (code < 0x10000) {
        out[0] = (uint16_t)code;
        return 1;
    }
    out[0] = (uint16_t)(0xd800 + (code - 0x10000) / 1024);
    out[1] = (uint16_t)(0xdc00 + (code - 0x10000) % 1024);
    return 2;
}

/// @brief The length of the UTF-8 sequence lead starts, from its high bits
/// alone, or 0 for a continuation byte or 0xf8 and above.
static size_t ref_lead_length(unsigned char lead) {
    if (lead < 0x80) {
        return 1;
    } else if (lead < 0xc0) {
        return 0;
    } else if (lead < 0xe0) {
        return 2;
    } else if (lead < 0xf0) {
        return 3;
    }
    return lead < 0xf8 ? 4 : 0;
}

/// @brief The value of n bytes of UTF-8 from their bits alone, without
/// checking them.
static uint32_t ref_value(const unsigned char *s, size_t n) {
    uint32_t value = n == 1 ? s[0] : (uint32_t)(s[0] & (0x7f >> n));
    for (size_t i = 1; i < n; i++) {
        value = value * 64 + (s[i] & 0x3f);
    }
    return value;
}

/// @brief Returns 1 if the first n bytes of s start some valid UTF-8
/// encoding.
static int ref_prefix(const unsigned char *s, size_t n) {
    if (n == 0) {
        return 1;
    }
    if (n < 4) {
        return map_test(prefixes[n], prefix_index(s, n));
    }
    if (ref_lead_length(s[0]) != 4) {
        return 0;
    }
    for (size_t i = 1; i < 4; i++) {
        if ((s[i] & 0xc0) != 0x80) {
            return 0;
        }
    }
    uint32_t value = ref_value(s, 4);
    return value >= 0x10000 && value <= 0x10ffff;
}

/// @brief Decodes the first of the size bytes at s, size > 0, as the
/// standard says, without the library: the code point of a whole valid
/// sequence, or an error covering the longest start of a valid sequence
/// (its "maximal subpart"), or one byte if there is none. Returns the bytes
/// used.
static size_t ref_decode(const unsigned char *s, size_t size, uint32_t *code) {
    size_t k = size < 4 ? size : 4;
    while (k > 0 && !ref_prefix(s, k)) {
        k--;
    }
    if (k == 0) {
        *code = SOF_UTF_INVALID;
        return 1;
    }
    if (k == ref_lead_length(s[0])) {
        *code = ref_value(s, k);
    } else if (k == size) {
        *code = SOF_UTF_INCOMPLETE;
    } else {
        *code = SOF_UTF_INVALID;
    }
    return k;
}

/// @brief Returns 1 if sof_utf8_decode() gives what ref_decode() does for
/// the size bytes of s.
static int decode_matches(const unsigned char *s, size_t size) {
    uint32_t want;
    size_t want_used = ref_decode(s, size, &want);
    uint32_t code = 0;
    size_t used = sof_utf8_decode(s, size, &code);
    return code == want && used == want_used;
}

/// @brief sof_utf8_ascii(), a byte at a time.
static size_t ref_ascii(const unsigned char *s, size_t size) {
    size_t i = 0;
    while (i < size && s[i] < 0x80) {
        i++;
    }
    return i;
}

/// @brief sof_utf8_validate(), a sequence at a time with ref_decode().
static size_t ref_validate(const unsigned char *s, size_t size) {
    size_t position = 0;
    while (position < size) {
        uint32_t code;
        size_t used = ref_decode(s + position, size - position, &code);
        if (!ref_valid(code)) {
            break;
        }
        position += used;
    }
    return position;
}

/// @brief sof_utf8_count(), a sequence at a time with ref_decode().
static size_t ref_count(const unsigned char *s, size_t size) {
    size_t n = 0;
    uint32_t code;
    for (size_t position = 0; position < size; n++) {
        position += ref_decode(s + position, size - position, &code);
    }
    return n;
}

/// @brief sof_utf8_to_utf16(), a sequence at a time with ref_decode() and
/// ref_utf16(): once a code point doesn't fit, nothing more is written.
static size_t ref_to_utf16(const unsigned char *s, size_t size, uint16_t *buffer, size_t count) {
    int full = buffer == NULL;
    size_t total = 0;
    uint16_t units[2];
    for (size_t position = 0; position < size;) {
        uint32_t code;
        position += ref_decode(s + position, size - position, &code);
        size_t n = ref_utf16(ref_valid(code) ? code : 0xfffd, units);
        if (!full && count - total >= n) {
            memcpy(buffer + total, units, n * sizeof(units[0]));
        } else {
            full = 1;
        }
        total += n;
    }
    return total;
}

/// @brief sof_utf8_to_utf32(), a sequence at a time with ref_decode().
static size_t ref_to_utf32(const unsigned char *s, size_t size, uint32_t *buffer, size_t count) {
    size_t total = 0;
    for (size_t position = 0; position < size; total++) {
        uint32_t code;
        position += ref_decode(s + position, size - position, &code);
        if (buffer != NULL && total < count) {
            buffer[total] = ref_valid(code) ? code : 0xfffd;
        }
    }
    return total;
}

/// @brief Returns 1 if sof_utf8_ascii(), sof_utf8_validate(),
/// sof_utf8_count(), sof_utf8_to_utf16() and sof_utf8_to_utf32() give what
/// the reference functions do for the size bytes at s, the conversions with
/// no buffer, room for all, one unit less, none, and a random amount.
static int matches_reference(const unsigned char *s, size_t size, uint64_t *state) {
    enum { UNITS = 2 * (MIXED_MAX + 16) };
    uint16_t want16[UNITS], got16[UNITS];
    uint32_t want32[UNITS], got32[UNITS];
    if (sof_utf8_ascii(s, size) != ref_ascii(s, size) || sof_utf8_validate(s, size) != ref_validate(s, size) || sof_utf8_count(s, size) != ref_count(s, size)) {
        return 0;
    }
    size_t total16 = ref_to_utf16(s, size, NULL, 0);
    size_t total32 = ref_to_utf32(s, size, NULL, 0);
    if (sof_utf8_to_utf16(s, size, NULL, 0) != total16 || sof_utf8_to_utf16(s, size, NULL, 5) != total16 || sof_utf8_to_utf32(s, size, NULL, 0) != total32 || sof_utf8_to_utf32(s, size, NULL, 5) != total32) {
        return 0;
    }
    const size_t rooms[] = {total16, total16 > 0 ? total16 - 1 : 0, 0,
                            (size_t)(next_random(state) % (total16 + 1))};
    for (size_t r = 0; r < sizeof(rooms) / sizeof(rooms[0]); r++) {
        // Units past the room must be left alone.
        memset(want16, CANARY, sizeof(want16));
        memset(got16, CANARY, sizeof(got16));
        memset(want32, CANARY, sizeof(want32));
        memset(got32, CANARY, sizeof(got32));
        size_t room32 = rooms[r] < total32 ? rooms[r] : total32;
        ref_to_utf16(s, size, want16, rooms[r]);
        ref_to_utf32(s, size, want32, room32);
        if (sof_utf8_to_utf16(s, size, got16, rooms[r]) != total16 || sof_utf8_to_utf32(s, size, got32, room32) != total32 || memcmp(want16, got16, sizeof(want16)) != 0 || memcmp(want32, got32, sizeof(want32)) != 0) {
            return 0;
        }
    }
    return 1;
}

/// @brief Sequences that are errors: overlong forms, surrogates, values past
/// U+10FFFF, bytes that can't start a sequence, stray continuation bytes,
/// and sequences cut short, which are errors when something follows them
/// and incomplete at the end.
static const char *const bad_pieces[] = {
    "\xc0\x80", "\xc1\xbf", "\xe0\x80\x80", "\xe0\x9f\xbf", "\xf0\x80\x80\x80", "\xf0\x8f\xbf\xbf",
    "\xed\xa0\x80", "\xed\xbf\xbf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xf8\x88\x80\x80\x80", "\xfe",
    "\xff", "\x80", "\xbf", "\xc3", "\xe2\x82", "\xf0\x9f\x98", "\xe0\xa0", "\xf4\x8f\xbf", "\xc3\xc3",
    "\xe2\x28\xa1",
};

/// @brief Code points at the edges of each encoded size and of the
/// surrogates.
static const uint32_t edge_codes[] = {
    0x00, 0x7f, 0x80, 0x7ff, 0x800, 0xfff, 0x1000, 0xd7ff, 0xe000, 0xfffd, 0xffff, 0x10000, 0x3ffff, 0x40000, 0x10ffff,
};


/// @brief A random code point, from each range of encoded sizes about as
/// often.
static uint32_t random_code(uint64_t *state) {
    uint64_t r = next_random(state);
    switch (r % 4) {
    case 0:
        return (uint32_t)(r >> 8) % 0x80;
    case 1:
        return 0x80 + (uint32_t)(r >> 8) % 0x780;
    case 2: {
        uint32_t code = 0x800 + (uint32_t)(r >> 8) % 0xf000;
        return ref_valid(code) ? code : code - 0x800;
    }
    default:
        return 0x10000 + (uint32_t)(r >> 8) % 0x100000;
    }
}

/// @brief A random string in all three encodings, made with the reference
/// encoders, with the position in UTF-8 and UTF-16 after each code point.
typedef struct sample_t {
    uint32_t utf32[LENGTH_MAX];
    size_t length;
    unsigned char utf8[LENGTH_MAX * 4];
    size_t utf8_size;
    size_t utf8_ends[LENGTH_MAX];
    uint16_t utf16[LENGTH_MAX * 2];
    size_t utf16_count;
    size_t utf16_ends[LENGTH_MAX];
} sample_t;

/// @brief Fills sample with a random string of 0 to LENGTH_MAX code points.
static void random_sample(sample_t *sample, uint64_t *state) {
    sample->length = (size_t)(next_random(state) % (LENGTH_MAX + 1));
    sample->utf8_size = 0;
    sample->utf16_count = 0;
    for (size_t i = 0; i < sample->length; i++) {
        uint32_t code = random_code(state);
        sample->utf32[i] = code;
        sample->utf8_size += ref_utf8(code, sample->utf8 + sample->utf8_size);
        sample->utf8_ends[i] = sample->utf8_size;
        sample->utf16_count += ref_utf16(code, sample->utf16 + sample->utf16_count);
        sample->utf16_ends[i] = sample->utf16_count;
    }
}

/// @brief Appends a random piece to the size bytes at s: a run of 0 to 20
/// ASCII bytes, a random code point, an edge code point or an error.
/// Returns the new size.
static size_t append_piece(unsigned char *s, size_t size, uint64_t *state) {
    uint64_t r = next_random(state);
    switch (r % 4) {
    case 0: {
        size_t n = (size_t)(r >> 8) % 21;
        for (size_t i = 0; i < n; i++) {
            s[size++] = (unsigned char)(next_random(state) % 0x80);
        }
        return size;
    }
    case 1:
        return size + ref_utf8(random_code(state), s + size);
    case 2:
        return size + ref_utf8(edge_codes[(r >> 8) % (sizeof(edge_codes) / sizeof(edge_codes[0]))], s + size);
    default: {
        const char *piece = bad_pieces[(r >> 8) % (sizeof(bad_pieces) / sizeof(bad_pieces[0]))];
        memcpy(s + size, piece, strlen(piece));
        return size + strlen(piece);
    }
    }
}

/// @brief The most of a string's encoding that fits in room: the end of the
/// last whole code point that does, given where each one ends.
static size_t fitting(const size_t *ends, size_t length, size_t room) {
    size_t fit = 0;
    for (size_t i = 0; i < length && ends[i] <= room; i++) {
        fit = ends[i];
    }
    return fit;
}

static void test_constants(void) {
    CHECK(SOF_UTF_MAX == 0x10ffff);
    CHECK(SOF_UTF_REPLACEMENT == 0xfffd);
    CHECK(SOF_UTF_INVALID > SOF_UTF_MAX);
    CHECK(SOF_UTF_INCOMPLETE > SOF_UTF_MAX);
    CHECK(SOF_UTF_INVALID != SOF_UTF_INCOMPLETE);
    CHECK(SOF_UTF8_SIZE_MAX == 4);
    CHECK(SOF_UTF16_SIZE_MAX == 2);
}

static void test_valid(void) {
    CHECK(sof_utf_valid(0));
    CHECK(sof_utf_valid(0xd7ff));
    CHECK(!sof_utf_valid(0xd800));
    CHECK(!sof_utf_valid(0xdbff));
    CHECK(!sof_utf_valid(0xdc00));
    CHECK(!sof_utf_valid(0xdfff));
    CHECK(sof_utf_valid(0xe000));
    CHECK(sof_utf_valid(0xfffd));
    CHECK(sof_utf_valid(0xffff));
    CHECK(sof_utf_valid(0x10ffff));
    CHECK(!sof_utf_valid(0x110000));
    CHECK(!sof_utf_valid(SOF_UTF_INVALID));
    CHECK(!sof_utf_valid(SOF_UTF_INCOMPLETE));
    for (uint32_t code = 0; code <= 0x110000; code++) {
        CHECK(sof_utf_valid(code) == ref_valid(code));
    }
}

static void test_prefixes(void) {
    unsigned char bytes[4];
    for (uint32_t code = 0; code <= SOF_UTF_MAX; code++) {
        if (!ref_valid(code)) {
            continue;
        }
        size_t n = ref_utf8(code, bytes);
        for (size_t k = 1; k <= n && k < 4; k++) {
            map_set(prefixes[k], prefix_index(bytes, k));
        }
    }
    // Leads 00-7f, c2-df, e0-ef and f0-f4.
    size_t counts[4] = {0, 0, 0, 0};
    size_t sizes[4] = {0, 1 << 8, 1 << 16, 1 << 24};
    for (size_t k = 1; k < 4; k++) {
        for (size_t i = 0; i < sizes[k]; i++) {
            counts[k] += (size_t)map_test(prefixes[k], i);
        }
    }
    CHECK(counts[1] == 128 + 30 + 16 + 5);
    // Whole two-byte sequences, and the starts of three- and four-byte ones.
    CHECK(counts[2] == 1920 + 960 + 256);
    // Whole three-byte sequences, U+0800 to U+FFFF less the 2048
    // surrogates, and the starts of four-byte ones.
    CHECK(counts[3] == 61440 + 16384);
}

static void test_utf8_size(void) {
    unsigned char bytes[4];
    for (uint32_t code = 0; code <= 0x110000; code++) {
        size_t want = ref_valid(code) ? ref_utf8(code, bytes) : 0;
        CHECK(sof_utf8_size(code) == want);
    }
    CHECK(sof_utf8_size(0x7f) == 1);
    CHECK(sof_utf8_size(0x80) == 2);
    CHECK(sof_utf8_size(0x7ff) == 2);
    CHECK(sof_utf8_size(0x800) == 3);
    CHECK(sof_utf8_size(0xffff) == 3);
    CHECK(sof_utf8_size(0x10000) == 4);
    CHECK(sof_utf8_size(SOF_UTF_INVALID) == 0);
}

static void test_utf8_encode(void) {
    unsigned char want[4];
    unsigned char got[SOF_UTF8_SIZE_MAX + 1];
    for (uint32_t code = 0; code <= 0x110000; code++) {
        memset(got, CANARY, sizeof(got));
        if (!ref_valid(code)) {
            CHECK(sof_utf8_encode(code, got, sizeof(got)) == 0);
            CHECK(got[0] == CANARY);
            continue;
        }
        size_t n = ref_utf8(code, want);
        CHECK(sof_utf8_encode(code, got, sizeof(got)) == n);
        CHECK(memcmp(got, want, n) == 0);
        CHECK(got[n] == CANARY);
        // Too little room writes nothing.
        memset(got, CANARY, sizeof(got));
        CHECK(sof_utf8_encode(code, got, n - 1) == 0);
        CHECK(got[0] == CANARY);
    }
    CHECK(sof_utf8_encode('a', NULL, 4) == 0);
    unsigned char euro[] = {0xe2, 0x82, 0xac};
    CHECK(sof_utf8_encode(0x20ac, got, sizeof(got)) == 3);
    CHECK(memcmp(got, euro, 3) == 0);
}

static void test_utf8_decode_round_trip(void) {
    unsigned char bytes[4];
    for (uint32_t code = 0; code <= SOF_UTF_MAX; code++) {
        if (!ref_valid(code)) {
            continue;
        }
        size_t n = ref_utf8(code, bytes);
        uint32_t got = 0;
        CHECK(sof_utf8_decode(bytes, n, &got) == n);
        CHECK(got == code);
        CHECK(sof_utf8_decode(bytes, n, NULL) == n);
        // Cut short, it is incomplete, and takes all there is.
        for (size_t k = 1; k < n; k++) {
            CHECK(sof_utf8_decode(bytes, k, &got) == k);
            CHECK(got == SOF_UTF_INCOMPLETE);
        }
    }
}

static void test_utf8_decode_short(void) {
    unsigned char s[3];
    for (unsigned a = 0; a < 256; a++) {
        s[0] = (unsigned char)a;
        CHECK(decode_matches(s, 1));
        for (unsigned b = 0; b < 256; b++) {
            s[1] = (unsigned char)b;
            CHECK(decode_matches(s, 2));
            for (unsigned c = 0; c < 256; c++) {
                s[2] = (unsigned char)c;
                CHECK(decode_matches(s, 3));
            }
        }
    }
}

static void test_utf8_decode_long(void) {
    uint64_t state = 0x75746638;
    unsigned char s[6];
    for (size_t i = 0; i < SEQUENCES; i++) {
        uint64_t r = next_random(&state);
        // Mostly four-byte leads and continuation bytes, so most sequences
        // get far.
        s[0] = (unsigned char)(r % 4 == 0 ? r >> 8 : 0xf0 + (r >> 8) % 6);
        for (size_t j = 1; j < sizeof(s); j++) {
            r = next_random(&state);
            s[j] = (unsigned char)(r % 8 == 0 ? r >> 8 : 0x80 + (r >> 8) % 64);
        }
        CHECK(decode_matches(s, 4));
        // What follows a sequence doesn't change it.
        CHECK(decode_matches(s, sizeof(s)));
    }
}

static void test_utf8_decode_cases(void) {
    // Table 3-8 of the Unicode standard: 61 F1 80 80 E1 80 C2 62 80 63 80 BF
    // 64 is a, three errors, b, one error, c, two errors, d.
    const unsigned char s[] = {0x61, 0xf1, 0x80, 0x80, 0xe1, 0x80, 0xc2, 0x62, 0x80, 0x63, 0x80, 0xbf, 0x64};
    const uint32_t I = SOF_UTF_INVALID;
    const uint32_t want[] = {0x61, I, I, I, 0x62, I, 0x63, I, I, 0x64};
    const size_t used[] = {1, 3, 2, 1, 1, 1, 1, 1, 1, 1};
    size_t position = 0;
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        uint32_t code;
        size_t n = sof_utf8_decode(s + position, sizeof(s) - position, &code);
        CHECK(code == want[i]);
        CHECK(n == used[i]);
        position += n;
    }
    CHECK(position == sizeof(s));

    uint32_t code = 0;
    // Overlong forms of '/' and NUL.
    CHECK(sof_utf8_decode("\xc0\xaf", 2, &code) == 1 && code == I);
    CHECK(sof_utf8_decode("\xe0\x80\xaf", 3, &code) == 1 && code == I);
    CHECK(sof_utf8_decode("\xf0\x80\x80\xaf", 4, &code) == 1 && code == I);
    CHECK(sof_utf8_decode("\xc1\xbf", 2, &code) == 1 && code == I);
    // A surrogate, and the first value past the last code point.
    CHECK(sof_utf8_decode("\xed\xa0\x80", 3, &code) == 1 && code == I);
    CHECK(sof_utf8_decode("\xf4\x90\x80\x80", 4, &code) == 1 && code == I);
    // The edges that are valid.
    CHECK(sof_utf8_decode("\xed\x9f\xbf", 3, &code) == 3 && code == 0xd7ff);
    CHECK(sof_utf8_decode("\xee\x80\x80", 3, &code) == 3 && code == 0xe000);
    CHECK(sof_utf8_decode("\xf4\x8f\xbf\xbf", 4, &code) == 4 && code == 0x10ffff);
    CHECK(sof_utf8_decode("\xf0\x90\x80\x80", 4, &code) == 4 && code == 0x10000);
    // NUL is a character.
    CHECK(sof_utf8_decode("\0a", 2, &code) == 1 && code == 0);
    // Bytes that are never valid.
    CHECK(sof_utf8_decode("\xff", 1, &code) == 1 && code == I);
    CHECK(sof_utf8_decode("\xf5\x80\x80\x80", 4, &code) == 1 && code == I);
    CHECK(sof_utf8_decode("\x80", 1, &code) == 1 && code == I);
    // Cut short.
    CHECK(sof_utf8_decode("\xf0\x9f\x98", 3, &code) == 3 && code == SOF_UTF_INCOMPLETE);
    CHECK(sof_utf8_decode("\xe2", 1, &code) == 1 && code == SOF_UTF_INCOMPLETE);
    // But not if the bytes so far can't start a code point.
    CHECK(sof_utf8_decode("\xe0\x80", 2, &code) == 1 && code == I);
}

static void test_utf8_strings(void) {
    uint64_t state = 0x737472;
    sample_t sample;
    for (size_t round = 0; round < ROUNDS; round++) {
        random_sample(&sample, &state);
        CHECK(sof_utf8_count(sample.utf8, sample.utf8_size) == sample.length);
        CHECK(sof_utf8_validate(sample.utf8, sample.utf8_size) == sample.utf8_size);
        size_t position = 0;
        size_t i = 0;
        uint32_t code;
        while (sof_utf8_next(sample.utf8, sample.utf8_size, &position, &code)) {
            CHECK(i < sample.length);
            CHECK(code == sample.utf32[i]);
            CHECK(position == sample.utf8_ends[i]);
            i++;
        }
        CHECK(i == sample.length);
        CHECK(position == sample.utf8_size);
        // At the end, nothing changes.
        code = 7;
        CHECK(!sof_utf8_next(sample.utf8, sample.utf8_size, &position, &code));
        CHECK(code == 7 && position == sample.utf8_size);
        // Cut inside the last code point, the string is invalid there.
        if (sample.length > 0) {
            size_t last = sample.length == 1 ? 0 : sample.utf8_ends[sample.length - 2];
            if (sample.utf8_size - last > 1) {
                CHECK(sof_utf8_validate(sample.utf8, sample.utf8_size - 1) == last);
                CHECK(sof_utf8_count(sample.utf8, sample.utf8_size - 1) == sample.length);
            }
        }
    }
    // A stray byte in the middle.
    CHECK(sof_utf8_validate("ab\x80" "cd", 5) == 2);
    CHECK(sof_utf8_count("ab\x80" "cd", 5) == 5);
    CHECK(sof_utf8_validate("", 0) == 0);
    CHECK(sof_utf8_count("", 0) == 0);
}

static void test_utf8_reference_edges(void) {
    uint64_t state = 0xed6e;
    unsigned char base[64];
    unsigned char buffer[64 + 8];
    // Each error and edge code point after 0 to 17 bytes of ASCII and
    // before 0 to 9, so they fall on every side of the 8-byte words, at
    // every alignment, and cut off at every length, so a sequence is cut
    // short at the end, straddling a word or not.
    size_t pieces = sizeof(bad_pieces) / sizeof(bad_pieces[0]) + sizeof(edge_codes) / sizeof(edge_codes[0]);
    for (size_t p = 0; p < pieces; p++) {
        for (size_t before = 0; before < 18; before++) {
            for (size_t after = 0; after < 10; after++) {
                size_t size = 0;
                while (size < before) {
                    base[size] = (unsigned char)('a' + size % 26);
                    size++;
                }
                if (p < sizeof(bad_pieces) / sizeof(bad_pieces[0])) {
                    memcpy(base + size, bad_pieces[p], strlen(bad_pieces[p]));
                    size += strlen(bad_pieces[p]);
                } else {
                    size += ref_utf8(edge_codes[p - sizeof(bad_pieces) / sizeof(bad_pieces[0])], base + size);
                }
                for (size_t i = 0; i < after; i++) {
                    base[size++] = (unsigned char)('0' + i);
                }
                for (size_t offset = 0; offset < 8; offset++) {
                    memcpy(buffer + offset, base, size);
                    for (size_t length = before; length <= size; length++) {
                        CHECK(matches_reference(buffer + offset, length, &state));
                    }
                }
            }
        }
    }
}

static void test_utf8_reference_random(void) {
    uint64_t state = 0x6d697865;
    unsigned char base[MIXED_MAX + 16];
    unsigned char buffer[MIXED_MAX + 16 + 8];
    for (size_t round = 0; round < MIXED_ROUNDS; round++) {
        // Random pieces up to MIXED_MAX bytes, at a random alignment, and
        // cut off at every length.
        size_t size = 0;
        size_t target = (size_t)(next_random(&state) % MIXED_MAX);
        while (size < target) {
            size = append_piece(base, size, &state);
        }
        size_t offset = (size_t)(next_random(&state) % 8);
        memcpy(buffer + offset, base, size);
        for (size_t length = 0; length <= size; length++) {
            CHECK(matches_reference(buffer + offset, length, &state));
        }
    }
    // ASCII alone, of every length to 40 at every alignment, and with one
    // byte that isn't ASCII in every place.
    for (size_t size = 0; size <= 40; size++) {
        for (size_t offset = 0; offset < 8; offset++) {
            for (size_t i = 0; i < size; i++) {
                buffer[offset + i] = (unsigned char)(next_random(&state) % 0x80);
            }
            CHECK(sof_utf8_ascii(buffer + offset, size) == size);
            CHECK(sof_utf8_validate(buffer + offset, size) == size);
            CHECK(matches_reference(buffer + offset, size, &state));
            for (size_t at = 0; at < size; at++) {
                unsigned char was = buffer[offset + at];
                buffer[offset + at] = (unsigned char)(0x80 + next_random(&state) % 0x80);
                CHECK(sof_utf8_ascii(buffer + offset, size) == at);
                CHECK(matches_reference(buffer + offset, size, &state));
                buffer[offset + at] = was;
            }
        }
    }
    CHECK(sof_utf8_ascii(NULL, 10) == 0);
    CHECK(sof_utf8_ascii("abc", 0) == 0);
}

static void test_utf16_size(void) {
    for (uint32_t code = 0; code <= 0x110000; code++) {
        size_t want = !ref_valid(code) ? 0 : code < 0x10000 ? 1 : 2;
        CHECK(sof_utf16_size(code) == want);
    }
    CHECK(sof_utf16_size(SOF_UTF_INVALID) == 0);
}

static void test_utf16_encode_decode(void) {
    uint16_t want[2];
    uint16_t got[SOF_UTF16_SIZE_MAX + 1];
    for (uint32_t code = 0; code <= 0x110000; code++) {
        got[0] = got[1] = got[2] = 0xa5a5;
        if (!ref_valid(code)) {
            CHECK(sof_utf16_encode(code, got, 3) == 0);
            CHECK(got[0] == 0xa5a5);
            continue;
        }
        size_t n = ref_utf16(code, want);
        CHECK(sof_utf16_encode(code, got, 3) == n);
        CHECK(memcmp(got, want, n * sizeof(uint16_t)) == 0);
        CHECK(got[n] == 0xa5a5);
        CHECK(sof_utf16_encode(code, got, n - 1) == 0);
        uint32_t back = 0;
        CHECK(sof_utf16_decode(got, n, &back) == n);
        CHECK(back == code);
        CHECK(sof_utf16_decode(got, n, NULL) == n);
    }
    CHECK(sof_utf16_encode('a', NULL, 2) == 0);
    uint16_t smile[] = {0xd83d, 0xde00};
    CHECK(sof_utf16_encode(0x1f600, got, 2) == 2);
    CHECK(got[0] == smile[0] && got[1] == smile[1]);
}

static void test_utf16_decode_units(void) {
    uint64_t state = 0x7531;
    uint16_t s[2];
    // Every unit alone.
    for (uint32_t unit = 0; unit < 0x10000; unit++) {
        s[0] = (uint16_t)unit;
        uint32_t code = 0;
        CHECK(sof_utf16_decode(s, 1, &code) == 1);
        if (unit >= 0xd800 && unit <= 0xdbff) {
            CHECK(code == SOF_UTF_INCOMPLETE);
        } else if (unit >= 0xdc00 && unit <= 0xdfff) {
            CHECK(code == SOF_UTF_INVALID);
        } else {
            CHECK(code == unit);
        }
    }
    // Every high surrogate before a few units, and a few before every unit.
    const uint32_t seconds[] = {0, 0x61, 0xd7ff, 0xd800, 0xdbff, 0xdc00, 0xdfff, 0xe000, 0xffff};
    for (uint32_t high = 0xd800; high <= 0xdbff; high++) {
        for (size_t i = 0; i < sizeof(seconds) / sizeof(seconds[0]); i++) {
            s[0] = (uint16_t)high;
            s[1] = (uint16_t)seconds[i];
            uint32_t code = 0;
            size_t n = sof_utf16_decode(s, 2, &code);
            if (seconds[i] >= 0xdc00 && seconds[i] <= 0xdfff) {
                CHECK(n == 2 && code == 0x10000 + (high - 0xd800) * 1024 + (seconds[i] - 0xdc00));
            } else {
                CHECK(n == 1 && code == SOF_UTF_INVALID);
            }
        }
    }
    for (size_t round = 0; round < 8; round++) {
        uint32_t first = round == 0 ? 0xd800 : round == 1 ? 0xdbff : (uint32_t)(next_random(&state) % 0x10000);
        for (uint32_t second = 0; second < 0x10000; second++) {
            s[0] = (uint16_t)first;
            s[1] = (uint16_t)second;
            uint32_t code = 0;
            size_t n = sof_utf16_decode(s, 2, &code);
            int high = first >= 0xd800 && first <= 0xdbff;
            int low = second >= 0xdc00 && second <= 0xdfff;
            if (high && low) {
                CHECK(n == 2 && code == 0x10000 + (first - 0xd800) * 1024 + (second - 0xdc00));
            } else if (first >= 0xd800 && first <= 0xdfff) {
                CHECK(n == 1 && code == SOF_UTF_INVALID);
            } else {
                CHECK(n == 1 && code == first);
            }
        }
    }
}

static void test_utf16_strings(void) {
    uint64_t state = 0x3136;
    sample_t sample;
    for (size_t round = 0; round < ROUNDS; round++) {
        random_sample(&sample, &state);
        CHECK(sof_utf16_count(sample.utf16, sample.utf16_count) == sample.length);
        CHECK(sof_utf16_validate(sample.utf16, sample.utf16_count) == sample.utf16_count);
        size_t position = 0;
        size_t i = 0;
        uint32_t code;
        while (sof_utf16_next(sample.utf16, sample.utf16_count, &position, &code)) {
            CHECK(i < sample.length);
            CHECK(code == sample.utf32[i]);
            CHECK(position == sample.utf16_ends[i]);
            i++;
        }
        CHECK(i == sample.length);
        code = 7;
        CHECK(!sof_utf16_next(sample.utf16, sample.utf16_count, &position, &code));
        CHECK(code == 7 && position == sample.utf16_count);
    }
    // a, a lone low surrogate, a high one before b, then a high one at the
    // end.
    const uint16_t s[] = {0x61, 0xdc00, 0xd800, 0x62, 0xd800};
    CHECK(sof_utf16_validate(s, 5) == 1);
    CHECK(sof_utf16_validate(s, 1) == 1);
    CHECK(sof_utf16_count(s, 5) == 5);
    uint32_t code;
    CHECK(sof_utf16_decode(s + 4, 1, &code) == 1 && code == SOF_UTF_INCOMPLETE);
    CHECK(sof_utf16_validate(s + 3, 2) == 1);
}

static void test_utf32_validate(void) {
    const uint32_t s[] = {0x61, 0x10ffff, 0xd7ff, 0xe000, 0xd800, 0x62};
    CHECK(sof_utf32_validate(s, 4) == 4);
    CHECK(sof_utf32_validate(s, 6) == 4);
    CHECK(sof_utf32_validate(s + 5, 1) == 1);
    const uint32_t big[] = {0x110000};
    CHECK(sof_utf32_validate(big, 1) == 0);
    CHECK(sof_utf32_validate(s, 0) == 0);
}

static void test_conversions(void) {
    uint64_t state = 0x636f6e76;
    sample_t sample;
    unsigned char utf8[LENGTH_MAX * 4 + 1];
    uint16_t utf16[LENGTH_MAX * 2 + 1];
    uint32_t utf32[LENGTH_MAX + 1];
    for (size_t round = 0; round < ROUNDS; round++) {
        random_sample(&sample, &state);
        // Whole strings, with room to spare.
        memset(utf16, CANARY, sizeof(utf16));
        CHECK(sof_utf8_to_utf16(sample.utf8, sample.utf8_size, utf16, LENGTH_MAX * 2 + 1) == sample.utf16_count);
        CHECK(memcmp(utf16, sample.utf16, sample.utf16_count * sizeof(uint16_t)) == 0);
        memset(utf32, CANARY, sizeof(utf32));
        CHECK(sof_utf8_to_utf32(sample.utf8, sample.utf8_size, utf32, LENGTH_MAX + 1) == sample.length);
        CHECK(memcmp(utf32, sample.utf32, sample.length * sizeof(uint32_t)) == 0);
        memset(utf8, CANARY, sizeof(utf8));
        CHECK(sof_utf16_to_utf8(sample.utf16, sample.utf16_count, utf8, sizeof(utf8)) == sample.utf8_size);
        CHECK(memcmp(utf8, sample.utf8, sample.utf8_size) == 0);
        CHECK(sof_utf16_to_utf32(sample.utf16, sample.utf16_count, utf32, LENGTH_MAX + 1) == sample.length);
        CHECK(memcmp(utf32, sample.utf32, sample.length * sizeof(uint32_t)) == 0);
        memset(utf8, CANARY, sizeof(utf8));
        CHECK(sof_utf32_to_utf8(sample.utf32, sample.length, utf8, sizeof(utf8)) == sample.utf8_size);
        CHECK(memcmp(utf8, sample.utf8, sample.utf8_size) == 0);
        memset(utf16, CANARY, sizeof(utf16));
        CHECK(sof_utf32_to_utf16(sample.utf32, sample.length, utf16, LENGTH_MAX * 2 + 1) == sample.utf16_count);
        CHECK(memcmp(utf16, sample.utf16, sample.utf16_count * sizeof(uint16_t)) == 0);

        // Measured without a buffer.
        CHECK(sof_utf8_to_utf16(sample.utf8, sample.utf8_size, NULL, 0) == sample.utf16_count);
        CHECK(sof_utf8_to_utf32(sample.utf8, sample.utf8_size, NULL, 0) == sample.length);
        CHECK(sof_utf16_to_utf8(sample.utf16, sample.utf16_count, NULL, 0) == sample.utf8_size);
        CHECK(sof_utf16_to_utf32(sample.utf16, sample.utf16_count, NULL, 0) == sample.length);
        CHECK(sof_utf32_to_utf8(sample.utf32, sample.length, NULL, 0) == sample.utf8_size);
        CHECK(sof_utf32_to_utf16(sample.utf32, sample.length, NULL, 0) == sample.utf16_count);

        // Every buffer too small: only whole code points that fit are
        // written, and nothing past them.
        for (size_t room = 0; room < sample.utf8_size; room++) {
            size_t fit = fitting(sample.utf8_ends, sample.length, room);
            memset(utf8, CANARY, sizeof(utf8));
            CHECK(sof_utf16_to_utf8(sample.utf16, sample.utf16_count, utf8, room) == sample.utf8_size);
            CHECK(memcmp(utf8, sample.utf8, fit) == 0);
            CHECK(utf8[fit] == CANARY);
            memset(utf8, CANARY, sizeof(utf8));
            CHECK(sof_utf32_to_utf8(sample.utf32, sample.length, utf8, room) == sample.utf8_size);
            CHECK(memcmp(utf8, sample.utf8, fit) == 0);
            CHECK(utf8[fit] == CANARY);
        }
        for (size_t room = 0; room < sample.utf16_count; room++) {
            size_t fit = fitting(sample.utf16_ends, sample.length, room);
            memset(utf16, CANARY, sizeof(utf16));
            CHECK(sof_utf8_to_utf16(sample.utf8, sample.utf8_size, utf16, room) == sample.utf16_count);
            CHECK(memcmp(utf16, sample.utf16, fit * sizeof(uint16_t)) == 0);
            CHECK(utf16[fit] == (uint16_t)(CANARY * 0x101));
            memset(utf16, CANARY, sizeof(utf16));
            CHECK(sof_utf32_to_utf16(sample.utf32, sample.length, utf16, room) == sample.utf16_count);
            CHECK(memcmp(utf16, sample.utf16, fit * sizeof(uint16_t)) == 0);
            CHECK(utf16[fit] == (uint16_t)(CANARY * 0x101));
        }
        for (size_t room = 0; room < sample.length; room++) {
            memset(utf32, CANARY, sizeof(utf32));
            CHECK(sof_utf8_to_utf32(sample.utf8, sample.utf8_size, utf32, room) == sample.length);
            CHECK(memcmp(utf32, sample.utf32, room * sizeof(uint32_t)) == 0);
            CHECK(utf32[room] == (uint32_t)CANARY * 0x01010101);
            memset(utf32, CANARY, sizeof(utf32));
            CHECK(sof_utf16_to_utf32(sample.utf16, sample.utf16_count, utf32, room) == sample.length);
            CHECK(memcmp(utf32, sample.utf32, room * sizeof(uint32_t)) == 0);
            CHECK(utf32[room] == (uint32_t)CANARY * 0x01010101);
        }
    }
}

static void test_conversions_replace(void) {
    const uint32_t R = SOF_UTF_REPLACEMENT;
    // Table 3-8 again, with each error replaced.
    const unsigned char s8[] = {0x61, 0xf1, 0x80, 0x80, 0xe1, 0x80, 0xc2, 0x62, 0x80, 0x63, 0x80, 0xbf, 0x64};
    const uint32_t want32[] = {0x61, R, R, R, 0x62, R, 0x63, R, R, 0x64};
    uint32_t utf32[16];
    uint16_t utf16[16];
    unsigned char utf8[64];
    CHECK(sof_utf8_count(s8, sizeof(s8)) == 10);
    CHECK(sof_utf8_validate(s8, sizeof(s8)) == 1);
    CHECK(sof_utf8_to_utf32(s8, sizeof(s8), utf32, 16) == 10);
    CHECK(memcmp(utf32, want32, sizeof(want32)) == 0);
    CHECK(sof_utf8_to_utf16(s8, sizeof(s8), utf16, 16) == 10);
    for (size_t i = 0; i < 10; i++) {
        CHECK(utf16[i] == want32[i]);
    }
    // An incomplete sequence at the end is one replacement.
    CHECK(sof_utf8_to_utf32("a\xf0\x9f\x98", 4, utf32, 16) == 2);
    CHECK(utf32[0] == 'a' && utf32[1] == R);

    // UTF-16: a, a lone low surrogate, a high one before b, a high one at
    // the end.
    const uint16_t s16[] = {0x61, 0xdc00, 0xd800, 0x62, 0xd800};
    const char *want8 = "a\xef\xbf\xbd\xef\xbf\xbd" "b\xef\xbf\xbd";
    CHECK(sof_utf16_to_utf8(s16, 5, utf8, sizeof(utf8)) == strlen(want8));
    CHECK(memcmp(utf8, want8, strlen(want8)) == 0);
    CHECK(sof_utf16_to_utf32(s16, 5, utf32, 16) == 5);
    CHECK(utf32[0] == 0x61 && utf32[1] == R && utf32[2] == R && utf32[3] == 0x62 && utf32[4] == R);

    // UTF-32: a surrogate and a value too big.
    const uint32_t s32[] = {0x61, 0xdfff, 0x110000, 0x1f600};
    CHECK(sof_utf32_to_utf16(s32, 4, utf16, 16) == 5);
    CHECK(utf16[0] == 0x61 && utf16[1] == R && utf16[2] == R && utf16[3] == 0xd83d && utf16[4] == 0xde00);
    const char *want8b = "a\xef\xbf\xbd\xef\xbf\xbd\xf0\x9f\x98\x80";
    CHECK(sof_utf32_to_utf8(s32, 4, utf8, sizeof(utf8)) == strlen(want8b));
    CHECK(memcmp(utf8, want8b, strlen(want8b)) == 0);

    // Random bytes become valid UTF-16 with a unit or two per character.
    uint64_t state = 0x6a756e6b;
    unsigned char junk[64];
    uint16_t out[128];
    unsigned char back[64 * 4];
    for (size_t round = 0; round < ROUNDS; round++) {
        for (size_t i = 0; i < sizeof(junk); i++) {
            junk[i] = (unsigned char)next_random(&state);
        }
        size_t n = sof_utf8_to_utf16(junk, sizeof(junk), out, 128);
        CHECK(n <= 128);
        CHECK(sof_utf16_validate(out, n) == n);
        CHECK(sof_utf16_count(out, n) == sof_utf8_count(junk, sizeof(junk)));
        size_t m = sof_utf16_to_utf8(out, n, back, sizeof(back));
        CHECK(m <= sizeof(back));
        CHECK(sof_utf8_validate(back, m) == m);
    }
}

static void test_null(void) {
    uint32_t code = 7;
    size_t position = 0;
    unsigned char bytes[4];
    uint16_t units[2];
    uint32_t points[2];
    CHECK(sof_utf8_decode(NULL, 4, &code) == 0);
    CHECK(code == SOF_UTF_INCOMPLETE);
    code = 7;
    CHECK(sof_utf8_decode("a", 0, &code) == 0);
    CHECK(code == SOF_UTF_INCOMPLETE);
    CHECK(sof_utf8_decode(NULL, 4, NULL) == 0);
    CHECK(sof_utf8_count(NULL, 4) == 0);
    CHECK(sof_utf8_validate(NULL, 4) == 0);
    code = 7;
    CHECK(!sof_utf8_next(NULL, 4, &position, &code));
    CHECK(code == 7 && position == 0);
    CHECK(!sof_utf8_next("a", 1, NULL, &code));
    CHECK(sof_utf8_next("a", 1, &position, NULL) && position == 1);
    CHECK(sof_utf8_to_utf16(NULL, 4, units, 2) == 0);
    CHECK(sof_utf8_to_utf32(NULL, 4, points, 2) == 0);

    position = 0;
    CHECK(sof_utf16_decode(NULL, 4, &code) == 0);
    CHECK(code == SOF_UTF_INCOMPLETE);
    CHECK(sof_utf16_count(NULL, 4) == 0);
    CHECK(sof_utf16_validate(NULL, 4) == 0);
    code = 7;
    CHECK(!sof_utf16_next(NULL, 4, &position, &code));
    CHECK(code == 7 && position == 0);
    units[0] = 'a';
    CHECK(!sof_utf16_next(units, 1, NULL, &code));
    CHECK(sof_utf16_next(units, 1, &position, NULL) && position == 1);
    CHECK(sof_utf16_to_utf8(NULL, 4, bytes, 4) == 0);
    CHECK(sof_utf16_to_utf32(NULL, 4, points, 2) == 0);

    CHECK(sof_utf32_validate(NULL, 4) == 0);
    CHECK(sof_utf32_to_utf8(NULL, 4, bytes, 4) == 0);
    CHECK(sof_utf32_to_utf16(NULL, 4, units, 2) == 0);
}

int main(void) {
    printf("sof_utf:\n");
    run("constants", test_constants);
    run("sof_utf_valid on every value up to 0x110000", test_valid);
    run("prefixes of valid UTF-8, from the reference encoder", test_prefixes);
    run("sof_utf8_size", test_utf8_size);
    run("sof_utf8_encode every code point", test_utf8_encode);
    run("sof_utf8_decode every code point, whole and cut short", test_utf8_decode_round_trip);
    run("sof_utf8_decode every sequence of 1 to 3 bytes", test_utf8_decode_short);
    run("sof_utf8_decode random sequences of 4 bytes and more", test_utf8_decode_long);
    run("sof_utf8_decode errors and edges", test_utf8_decode_cases);
    run("sof_utf8_next, sof_utf8_count and sof_utf8_validate", test_utf8_strings);
    run("UTF-8 functions against the reference, around errors and edges", test_utf8_reference_edges);
    run("UTF-8 functions against the reference, on random text", test_utf8_reference_random);
    run("sof_utf16_size", test_utf16_size);
    run("sof_utf16_encode and sof_utf16_decode every code point", test_utf16_encode_decode);
    run("sof_utf16_decode every unit and surrogate pairs", test_utf16_decode_units);
    run("sof_utf16_next, sof_utf16_count and sof_utf16_validate", test_utf16_strings);
    run("sof_utf32_validate", test_utf32_validate);
    run("conversions between all three, whole and cut short", test_conversions);
    run("conversions replace errors with U+FFFD", test_conversions_replace);
    run("NULL data, position and code", test_null);
    return test_report();
}
