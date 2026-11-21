/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_base.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails.
 * @version 0.0.1
 * @date 2026-11-20
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_base.h"
#include "test.h"

/// @brief The longest data the round trips encode.
#define MAX_LENGTH 300

/// @brief The alignments the round trips try, 0 to ALIGNMENTS - 1.
#define ALIGNMENTS 8

/// @brief Room for anything the small tests encode or decode.
#define BUFFER_SIZE 1024

/// @brief A value no function sets an out-parameter to.
#define UNSET ((size_t)0x5eed)

/// @brief Every encoding.
static const sof_base_encoding_t encodings[] = {
    SOF_BASE_16, SOF_BASE_32, SOF_BASE_32HEX, SOF_BASE_64, SOF_BASE_64URL,
};
#define ENCODINGS (sizeof(encodings) / sizeof(encodings[0]))

/// @brief The flags of encoding, each combination of the two that matter.
static const unsigned encode_flags[] = {0, SOF_BASE_LOWER, SOF_BASE_NO_PAD, SOF_BASE_LOWER | SOF_BASE_NO_PAD};
#define ENCODE_FLAGS (sizeof(encode_flags) / sizeof(encode_flags[0]))

/// @brief An RFC 4648 section 10 test vector: the encoding of data.
typedef struct vector_t {
    sof_base_encoding_t encoding;
    const char *data;
    const char *text;
} vector_t;

/// @brief A text that must fail to decode, and where.
typedef struct bad_t {
    sof_base_encoding_t encoding;
    unsigned flags;
    const char *text;
    size_t offset;
} bad_t;

/// @brief A text that must decode, and to what.
typedef struct good_t {
    sof_base_encoding_t encoding;
    unsigned flags;
    const char *text;
    const char *data;
} good_t;

static sof_base_error_t decode_streamed(sof_base_encoding_t encoding, unsigned flags, const char *text, size_t length, size_t piece, unsigned char *data, size_t *written, size_t *offset);
static size_t encode_streamed(sof_base_encoding_t encoding, unsigned flags, const unsigned char *data, size_t length, uint64_t *rng, char *text);
static int fails_at(const bad_t *bad);
static int gives(const good_t *good);
static unsigned group_bits(sof_base_encoding_t encoding);
static unsigned group_chars(sof_base_encoding_t encoding);
static int padded(sof_base_encoding_t encoding);
static const char *ref_alphabet(sof_base_encoding_t encoding, unsigned flags);
static size_t ref_encode(sof_base_encoding_t encoding, unsigned flags, const unsigned char *data, size_t length, char *text);

/// @brief Decodes length characters with a sof_base_decoder_t, in pieces of
/// piece characters, or of random sizes if piece is 0, into data.
/// @return The error of the update or final that failed, or SOF_BASE_OK,
/// with *written the bytes written and *offset where it failed.
static sof_base_error_t decode_streamed(sof_base_encoding_t encoding, unsigned flags, const char *text, size_t length, size_t piece, unsigned char *data, size_t *written, size_t *offset) {
    sof_base_decoder_t decoder;
    uint64_t rng = 0x5eed + length;
    *written = 0;
    if (sof_base_decoder_init(&decoder, encoding, flags) != SOF_BASE_OK) {
        return SOF_BASE_ERROR;
    }
    size_t at = 0, n;
    while (at < length) {
        size_t take = piece != 0 ? piece : (size_t)(next_random(&rng) % 20);
        if (take > length - at) {
            take = length - at;
        }
        size_t room = sof_base_decoded_size(encoding, take + SOF_BASE_PENDING_MAX);
        sof_base_error_t err = sof_base_decoder_update(&decoder, text + at, take, data + *written, room, &n, offset);
        if (err != SOF_BASE_OK) {
            return err;
        }
        *written += n;
        at += take;
    }
    sof_base_error_t err = sof_base_decoder_final(&decoder, data + *written, 4, &n, offset);
    *written += n;
    return err;
}

/// @brief Encodes length bytes with a sof_base_encoder_t, in pieces of
/// random sizes, into text.
/// @return The characters written, or SIZE_MAX if a call failed.
static size_t encode_streamed(sof_base_encoding_t encoding, unsigned flags, const unsigned char *data, size_t length, uint64_t *rng, char *text) {
    sof_base_encoder_t encoder;
    if (sof_base_encoder_init(&encoder, encoding, flags) != SOF_BASE_OK) {
        return SIZE_MAX;
    }
    size_t at = 0, total = 0, n;
    while (at < length) {
        size_t take = (size_t)(next_random(rng) % 13);
        if (take > length - at) {
            take = length - at;
        }
        size_t room = sof_base_encoded_size(encoding, flags, take + SOF_BASE_PENDING_MAX);
        if (sof_base_encoder_update(&encoder, data + at, take, text + total, room, &n) != SOF_BASE_OK) {
            return SIZE_MAX;
        }
        total += n;
        at += take;
    }
    if (sof_base_encoder_final(&encoder, text + total, 8, &n) != SOF_BASE_OK) {
        return SIZE_MAX;
    }
    return total + n;
}

/// @brief Returns 1 if bad's text fails to decode with SOF_BASE_BAD_INPUT
/// at its offset, at once and streamed a character at a time and in random
/// pieces, writing nothing on the way.
static int fails_at(const bad_t *bad) {
    unsigned char data[BUFFER_SIZE];
    size_t length = strlen(bad->text), written = UNSET, offset = UNSET;
    sof_base_error_t err = sof_base_decode(bad->encoding, bad->flags, bad->text, length, data, sizeof(data), &written, &offset);
    if (err != SOF_BASE_BAD_INPUT || offset != bad->offset || written != 0) {
        printf("\n    decode \"%s\": error %d at %zu, expected %zu ", bad->text, (int)err, offset, bad->offset);
        return 0;
    }
    for (size_t piece = 0; piece < 2; piece++) {
        offset = UNSET;
        err = decode_streamed(bad->encoding, bad->flags, bad->text, length, piece, data, &written, &offset);
        if (err != SOF_BASE_BAD_INPUT || offset != bad->offset) {
            printf("\n    stream \"%s\": error %d at %zu, expected %zu ", bad->text, (int)err, offset, bad->offset);
            return 0;
        }
    }
    return 1;
}

/// @brief Returns 1 if good's text decodes to its data, at once and
/// streamed a character at a time and in random pieces.
static int gives(const good_t *good) {
    unsigned char data[BUFFER_SIZE];
    size_t length = strlen(good->text), expected = strlen(good->data), written = UNSET, offset = UNSET;
    sof_base_error_t err = sof_base_decode(good->encoding, good->flags, good->text, length, data, sizeof(data), &written, &offset);
    if (err != SOF_BASE_OK || written != expected || memcmp(data, good->data, expected) != 0 || offset != UNSET) {
        printf("\n    decode \"%s\": error %d at %zu ", good->text, (int)err, offset);
        return 0;
    }
    for (size_t piece = 0; piece < 2; piece++) {
        err = decode_streamed(good->encoding, good->flags, good->text, length, piece, data, &written, &offset);
        if (err != SOF_BASE_OK || written != expected || memcmp(data, good->data, expected) != 0) {
            printf("\n    stream \"%s\": error %d at %zu ", good->text, (int)err, offset);
            return 0;
        }
    }
    return 1;
}

/// @brief The bits a character holds in an encoding.
static unsigned group_bits(sof_base_encoding_t encoding) {
    return encoding == SOF_BASE_16 ? 4 : encoding == SOF_BASE_64 || encoding == SOF_BASE_64URL ? 6 : 5;
}

/// @brief The characters in a group of an encoding.
static unsigned group_chars(sof_base_encoding_t encoding) {
    return encoding == SOF_BASE_16 ? 2 : encoding == SOF_BASE_64 || encoding == SOF_BASE_64URL ? 4 : 8;
}

/// @brief Whether an encoding pads.
static int padded(sof_base_encoding_t encoding) {
    return encoding != SOF_BASE_16;
}

/// @brief The alphabet of an encoding, written out as RFC 4648 gives it,
/// in lower case if the flags say and the encoding has case.
static const char *ref_alphabet(sof_base_encoding_t encoding, unsigned flags) {
    int lower = (flags & SOF_BASE_LOWER) != 0;
    switch (encoding) {
    case SOF_BASE_16:
        return lower ? "0123456789abcdef" : "0123456789ABCDEF";
    case SOF_BASE_32:
        return lower ? "abcdefghijklmnopqrstuvwxyz234567" : "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    case SOF_BASE_32HEX:
        return lower ? "0123456789abcdefghijklmnopqrstuv" : "0123456789ABCDEFGHIJKLMNOPQRSTUV";
    case SOF_BASE_64:
        return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    default:
        return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    }
}

/// @brief Encodes data as plainly as can be: a bit at a time into an
/// accumulator, a character out whenever it holds enough bits.
/// @return The characters written.
static size_t ref_encode(sof_base_encoding_t encoding, unsigned flags, const unsigned char *data, size_t length, char *text) {
    const char *alphabet = ref_alphabet(encoding, flags);
    unsigned bits = group_bits(encoding), held = 0, value = 0, mask = (1u << bits) - 1;
    size_t n = 0;
    for (size_t i = 0; i < length; i++) {
        for (int b = 7; b >= 0; b--) {
            value = (value << 1 | (data[i] >> b & 1)) & mask;
            if (++held == bits) {
                text[n++] = alphabet[value];
                held = 0;
                value = 0;
            }
        }
    }
    if (held != 0) {
        text[n++] = alphabet[(value << (bits - held)) & mask];
    }
    if (padded(encoding) && !(flags & SOF_BASE_NO_PAD)) {
        while (n % group_chars(encoding) != 0) {
            text[n++] = '=';
        }
    }
    return n;
}

/// @brief Every byte value, in every position of a group, decodes only if
/// it is in the alphabet, in either case where the encoding has case, to
/// its place there; and whitespace only with SOF_BASE_SKIP_SPACE.
static void test_alphabets(void) {
    for (size_t e = 0; e < ENCODINGS; e++) {
        sof_base_encoding_t encoding = encodings[e];
        const char *upper = ref_alphabet(encoding, 0), *lower = ref_alphabet(encoding, SOF_BASE_LOWER);
        unsigned chars = group_chars(encoding), bits = group_bits(encoding);
        for (unsigned position = 0; position < chars; position++) {
            for (int c = 0; c < 256; c++) {
                if (c == '=') {
                    // Padding, tested on its own.
                    continue;
                }
                char text[8];
                memset(text, upper[0], chars);
                text[position] = (char)c;
                const char *in_upper = c != 0 ? memchr(upper, c, strlen(upper)) : NULL;
                const char *in_lower = c != 0 ? memchr(lower, c, strlen(lower)) : NULL;
                unsigned char data[8];
                size_t written = UNSET, offset = UNSET;
                sof_base_error_t err = sof_base_decode(encoding, 0, text, chars, data, sizeof(data), &written, &offset);
                if (in_upper == NULL && in_lower == NULL) {
                    CHECK(err == SOF_BASE_BAD_INPUT && offset == position);
                    continue;
                }
                CHECK(err == SOF_BASE_OK && written == chars * bits / 8);
                unsigned value = (unsigned)(in_upper != NULL ? in_upper - upper : in_lower - lower);
                // The value's bits are at bits * position from the top.
                unsigned shift = (chars - 1 - position) * bits;
                uint64_t whole = 0;
                for (size_t i = 0; i < written; i++) {
                    whole = whole << 8 | data[i];
                }
                CHECK((whole >> shift & ((1u << bits) - 1)) == value);
            }
        }
        // Whitespace is skipped only when asked, inside a group or between
        // two.
        const char *spaces = " \t\n\v\f\r";
        for (const char *s = spaces; *s != '\0'; s++) {
            char text[24];
            memset(text, upper[0], 2 * chars + 2);
            text[1] = *s;
            text[chars + 1] = *s;
            unsigned char data[16];
            size_t written, offset = UNSET;
            CHECK(sof_base_decode(encoding, 0, text, 2 * chars + 2, data, sizeof(data), &written, &offset) == SOF_BASE_BAD_INPUT);
            CHECK(offset == 1);
            CHECK(sof_base_decode(encoding, 0, text + 2, 2 * chars, data, sizeof(data), &written, &offset) == SOF_BASE_BAD_INPUT);
            CHECK(offset == chars - 1);
            CHECK(sof_base_decode(encoding, SOF_BASE_SKIP_SPACE, text, 2 * chars + 2, data, sizeof(data), &written, NULL) == SOF_BASE_OK);
            CHECK(written == 2 * chars * bits / 8);
        }
    }
}

/// @brief Text with bad characters, padding in the wrong place or of the
/// wrong length, too few characters, bits left over that aren't 0, and
/// whitespace fails, with the offset of the first character at fault.
static void test_bad_input(void) {
    static const bad_t bad[] = {
        // Characters outside the alphabet.
        {SOF_BASE_64, 0, "Zm9*", 3},
        {SOF_BASE_64, 0, "-_8=", 0},
        {SOF_BASE_64URL, 0, "+/8=", 0},
        {SOF_BASE_64, 0, "Zm9v\x80mFy", 4},
        {SOF_BASE_64, 0, "Zm9v\xffmFy", 4},
        {SOF_BASE_64, SOF_BASE_LENIENT | SOF_BASE_SKIP_SPACE, "Zm9v.", 4},
        {SOF_BASE_32, 0, "MZXW1===", 4},
        {SOF_BASE_32, 0, "MZXW8===", 4},
        {SOF_BASE_32HEX, 0, "CPNMW===", 4},
        {SOF_BASE_16, 0, "666G", 3},
        {SOF_BASE_16, 0, "6g", 1},
        // Too few characters to make a byte, or a number no bytes make.
        {SOF_BASE_64, SOF_BASE_LENIENT, "Z", 1},
        {SOF_BASE_64, SOF_BASE_NO_PAD, "Zm9vY", 5},
        {SOF_BASE_64, 0, "Z===", 1},
        {SOF_BASE_32, SOF_BASE_LENIENT, "M", 1},
        {SOF_BASE_32, SOF_BASE_LENIENT, "MZX", 3},
        {SOF_BASE_32, SOF_BASE_LENIENT, "MZXW6Y", 6},
        {SOF_BASE_32, 0, "MZX=====", 3},
        {SOF_BASE_32, 0, "MZXW6Y==", 6},
        {SOF_BASE_16, 0, "666", 3},
        {SOF_BASE_16, SOF_BASE_LENIENT, "6", 1},
        // Padding missing, short, long, misplaced, or followed by data.
        {SOF_BASE_64, 0, "Zg", 2},
        {SOF_BASE_64, 0, "Zm8", 3},
        {SOF_BASE_64, 0, "Zg=", 3},
        {SOF_BASE_64, 0, "Zg===", 4},
        {SOF_BASE_64, SOF_BASE_LENIENT, "Zg===", 4},
        {SOF_BASE_64, 0, "Zm9v=", 4},
        {SOF_BASE_64, SOF_BASE_LENIENT, "Zm9v=", 4},
        {SOF_BASE_64, 0, "====", 0},
        {SOF_BASE_64, 0, "=", 0},
        {SOF_BASE_64, 0, "Zg==Zg==", 4},
        {SOF_BASE_64, SOF_BASE_LENIENT, "Zg=Zg==", 3},
        {SOF_BASE_64, 0, "Zg=a", 3},
        {SOF_BASE_64, SOF_BASE_NO_PAD, "Zg==", 2},
        {SOF_BASE_64URL, SOF_BASE_NO_PAD, "Zm8=", 3},
        {SOF_BASE_32, 0, "MY", 2},
        {SOF_BASE_32, 0, "MY=====", 7},
        {SOF_BASE_32, 0, "MY=======", 8},
        {SOF_BASE_32, SOF_BASE_NO_PAD, "MY======", 2},
        {SOF_BASE_32HEX, 0, "CO=====", 7},
        {SOF_BASE_16, 0, "66=", 2},
        {SOF_BASE_16, SOF_BASE_LENIENT, "6=", 1},
        // Bits left over that aren't 0, in strict decoding.
        {SOF_BASE_64, 0, "Zh==", 1},
        {SOF_BASE_64, 0, "Zm9=", 2},
        {SOF_BASE_64, SOF_BASE_NO_PAD, "Zh", 1},
        {SOF_BASE_64, SOF_BASE_SKIP_SPACE, "Z h \n= =", 2},
        {SOF_BASE_32, 0, "MZ======", 1},
        {SOF_BASE_32, 0, "MZXR====", 3},
        {SOF_BASE_32, 0, "MZXW7===", 4},
        {SOF_BASE_32, 0, "MZXW6YR=", 6},
        {SOF_BASE_32HEX, SOF_BASE_NO_PAD, "CP", 1},
        // Whitespace, unless skipped.
        {SOF_BASE_64, 0, "Zm9v Zg==", 4},
        {SOF_BASE_64, 0, "Zm9v\n", 4},
        {SOF_BASE_64, 0, " Zm9v", 0},
        {SOF_BASE_64, SOF_BASE_LENIENT, "Zm\r\n9v", 2},
        {SOF_BASE_64, SOF_BASE_SKIP_SPACE, "Zg= =x", 5},
        {SOF_BASE_32, 0, "MZXW6===\n", 8},
        {SOF_BASE_16, 0, "66 6F", 2},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(fails_at(&bad[i]));
    }
    // A NUL in the text, which strlen() can't show.
    unsigned char data[8];
    size_t written, offset = UNSET;
    CHECK(sof_base_decode(SOF_BASE_64, 0, "Zm\0v", 4, data, sizeof(data), &written, &offset) == SOF_BASE_BAD_INPUT);
    CHECK(offset == 2 && written == 0);
    // Without an offset to set, the error is the same.
    CHECK(sof_base_decode(SOF_BASE_64, 0, "Zg", 2, data, sizeof(data), &written, NULL) == SOF_BASE_BAD_INPUT);
    CHECK(sof_base_decode(SOF_BASE_64, 0, "Zg", 2, data, sizeof(data), NULL, NULL) == SOF_BASE_BAD_INPUT);
    // A bad character comes before a buffer too small, even one past it.
    offset = UNSET;
    CHECK(sof_base_decode(SOF_BASE_64, 0, "Zm9vYmFy*", 9, data, 1, &written, &offset) == SOF_BASE_BAD_INPUT);
    CHECK(offset == 8 && written == 0);
}

/// @brief Every strictly decoded two- or three-character last group of
/// base64 and base32 encodes back to itself: there is one text for each
/// byte string. Leniently, every one decodes, to the same bytes as the
/// group with its unused bits cleared.
static void test_canonical(void) {
    for (size_t e = 0; e < ENCODINGS; e++) {
        sof_base_encoding_t encoding = encodings[e];
        if (encoding == SOF_BASE_16) {
            continue;
        }
        const char *alphabet = ref_alphabet(encoding, 0);
        unsigned bits = group_bits(encoding), chars = group_chars(encoding);
        size_t size = (size_t)1 << bits;
        size_t accepted = 0;
        for (size_t a = 0; a < size; a++) {
            for (size_t b = 0; b < size; b++) {
                char text[8], again[8];
                memset(text, '=', chars);
                text[0] = alphabet[a];
                text[1] = alphabet[b];
                unsigned char data[4], lenient[4];
                size_t written, lenient_written;
                sof_base_error_t err = sof_base_decode(encoding, 0, text, chars, data, sizeof(data), &written, NULL);
                CHECK(sof_base_decode(encoding, SOF_BASE_LENIENT, text, chars, lenient, sizeof(lenient), &lenient_written, NULL) == SOF_BASE_OK);
                CHECK(lenient_written == 1);
                CHECK(sof_base_decode(encoding, SOF_BASE_LENIENT, text, 2, lenient + 1, 1, &lenient_written, NULL) == SOF_BASE_OK);
                CHECK(lenient[0] == lenient[1]);
                if (err != SOF_BASE_OK) {
                    CHECK(err == SOF_BASE_BAD_INPUT);
                    continue;
                }
                accepted++;
                CHECK(written == 1 && data[0] == lenient[0]);
                CHECK(ref_encode(encoding, 0, data, 1, again) == chars);
                CHECK(memcmp(text, again, chars) == 0);
            }
        }
        // Exactly one group for each of the 256 bytes.
        CHECK(accepted == 256);
    }
}

/// @brief NULL streams, data and text, bad encodings and flags, and
/// streams whose init failed or that failed with bad input.
static void test_null(void) {
    char text[16];
    unsigned char data[16];
    size_t written = UNSET, offset = UNSET;
    // Bad encodings and flags.
    CHECK(sof_base_encode(0, 0, "f", 1, text, sizeof(text), &written) == SOF_BASE_ERROR && written == 0);
    CHECK(sof_base_encode(SOF_BASE_64URL + 1, 0, "f", 1, text, sizeof(text), &written) == SOF_BASE_ERROR);
    CHECK(sof_base_encode((sof_base_encoding_t)-1, 0, "f", 1, text, sizeof(text), NULL) == SOF_BASE_ERROR);
    CHECK(sof_base_encode(SOF_BASE_64, 0x10, "f", 1, text, sizeof(text), &written) == SOF_BASE_ERROR);
    written = UNSET;
    CHECK(sof_base_decode(0, 0, "Zg==", 4, data, sizeof(data), &written, &offset) == SOF_BASE_ERROR);
    CHECK(written == 0 && offset == UNSET);
    CHECK(sof_base_decode(SOF_BASE_64, 0x80000000u, "Zg==", 4, data, sizeof(data), &written, &offset) == SOF_BASE_ERROR);
    CHECK(sof_base_encoded_size(0, 0, 10) == 0);
    CHECK(sof_base_encoded_size(SOF_BASE_64URL + 1, 0, 10) == 0);
    CHECK(sof_base_decoded_size(0, 10) == 0);
    CHECK(sof_base_decoded_size(SOF_BASE_64URL + 1, 10) == 0);
    // Flags that don't concern a size don't matter to it.
    CHECK(sof_base_encoded_size(SOF_BASE_64, 0x10 | SOF_BASE_LOWER, 1) == 4);

    // NULL data is no data, and NULL text no text, whatever the length.
    CHECK(sof_base_encode(SOF_BASE_64, 0, NULL, 99, text, sizeof(text), &written) == SOF_BASE_OK && written == 0);
    CHECK(sof_base_decode(SOF_BASE_64, 0, NULL, 99, data, sizeof(data), &written, &offset) == SOF_BASE_OK);
    CHECK(written == 0);
    // A NULL buffer has no room, whatever its size, so it measures.
    CHECK(sof_base_encode(SOF_BASE_64, 0, "fo", 2, NULL, 99, &written) == SOF_BASE_TOO_SMALL && written == 4);
    CHECK(sof_base_encode(SOF_BASE_64, 0, NULL, 0, NULL, 99, &written) == SOF_BASE_OK && written == 0);
    CHECK(sof_base_decode(SOF_BASE_64, 0, "Zm8=", 4, NULL, 99, &written, &offset) == SOF_BASE_TOO_SMALL);
    CHECK(written == 2);
    CHECK(sof_base_decode(SOF_BASE_64, 0, "", 0, NULL, 0, &written, &offset) == SOF_BASE_OK && written == 0);
    CHECK(sof_base_decode(SOF_BASE_64, 0, "Zm8", 3, NULL, 0, NULL, &offset) == SOF_BASE_BAD_INPUT && offset == 3);

    // NULL streams.
    CHECK(sof_base_encoder_init(NULL, SOF_BASE_64, 0) == SOF_BASE_ERROR);
    CHECK(sof_base_encoder_update(NULL, "f", 1, text, sizeof(text), &written) == SOF_BASE_ERROR && written == 0);
    CHECK(sof_base_encoder_final(NULL, text, sizeof(text), &written) == SOF_BASE_ERROR && written == 0);
    CHECK(sof_base_decoder_init(NULL, SOF_BASE_64, 0) == SOF_BASE_ERROR);
    written = UNSET;
    CHECK(sof_base_decoder_update(NULL, "Zg==", 4, data, sizeof(data), &written, &offset) == SOF_BASE_ERROR);
    CHECK(written == 0);
    CHECK(sof_base_decoder_final(NULL, data, sizeof(data), &written, &offset) == SOF_BASE_ERROR);

    // A stream whose init failed is dead until set up again.
    sof_base_encoder_t encoder;
    CHECK(sof_base_encoder_init(&encoder, SOF_BASE_64, 0x10) == SOF_BASE_ERROR && encoder.encoding == 0);
    CHECK(sof_base_encoder_update(&encoder, "f", 1, text, sizeof(text), &written) == SOF_BASE_ERROR);
    CHECK(sof_base_encoder_final(&encoder, text, sizeof(text), &written) == SOF_BASE_ERROR);
    CHECK(sof_base_encoder_init(&encoder, 0, 0) == SOF_BASE_ERROR);
    CHECK(sof_base_encoder_init(&encoder, SOF_BASE_64, 0) == SOF_BASE_OK);
    CHECK(sof_base_encoder_update(&encoder, NULL, 5, text, sizeof(text), &written) == SOF_BASE_OK && written == 0);
    CHECK(sof_base_encoder_update(&encoder, "f", 1, NULL, 0, &written) == SOF_BASE_OK && written == 0);
    CHECK(sof_base_encoder_final(&encoder, NULL, 9, &written) == SOF_BASE_TOO_SMALL && written == 4);
    CHECK(sof_base_encoder_final(&encoder, text, 4, NULL) == SOF_BASE_OK && memcmp(text, "Zg==", 4) == 0);

    sof_base_decoder_t decoder;
    CHECK(sof_base_decoder_init(&decoder, SOF_BASE_64URL + 1, 0) == SOF_BASE_ERROR && decoder.encoding == 0);
    CHECK(sof_base_decoder_update(&decoder, "Zg==", 4, data, sizeof(data), &written, &offset) == SOF_BASE_ERROR);
    CHECK(sof_base_decoder_final(&decoder, data, sizeof(data), &written, &offset) == SOF_BASE_ERROR);
    CHECK(sof_base_decoder_init(&decoder, SOF_BASE_64, 0) == SOF_BASE_OK);
    CHECK(sof_base_decoder_update(&decoder, NULL, 7, data, sizeof(data), &written, &offset) == SOF_BASE_OK);
    CHECK(written == 0);
    // Room for every character as data is needed, the 1 byte of "Zm" here,
    // though they make no whole group.
    CHECK(sof_base_decoder_update(&decoder, "Zm", 2, NULL, 0, &written, &offset) == SOF_BASE_TOO_SMALL);
    CHECK(written == 1);
    CHECK(sof_base_decoder_update(&decoder, "Zm", 2, data, 1, &written, &offset) == SOF_BASE_OK && written == 0);
    CHECK(sof_base_decoder_update(&decoder, "8=", 2, NULL, 0, &written, &offset) == SOF_BASE_TOO_SMALL);
    CHECK(written == 3);
    CHECK(sof_base_decoder_update(&decoder, "8=", 2, data, 3, NULL, NULL) == SOF_BASE_OK);
    CHECK(sof_base_decoder_final(&decoder, NULL, 0, &written, &offset) == SOF_BASE_OK && written == 0);
    CHECK(memcmp(data, "fo", 2) == 0);

    // A stream that met bad input is dead until set up again.
    CHECK(sof_base_decoder_init(&decoder, SOF_BASE_64, 0) == SOF_BASE_OK);
    CHECK(sof_base_decoder_update(&decoder, "Zm9v", 4, data, sizeof(data), &written, &offset) == SOF_BASE_OK);
    CHECK(sof_base_decoder_update(&decoder, "Z*", 2, data, sizeof(data), &written, &offset) == SOF_BASE_BAD_INPUT);
    CHECK(offset == 5 && written == 0 && decoder.encoding == 0);
    CHECK(sof_base_decoder_update(&decoder, "Zg==", 4, data, sizeof(data), &written, &offset) == SOF_BASE_ERROR);
    CHECK(sof_base_decoder_final(&decoder, data, sizeof(data), &written, &offset) == SOF_BASE_ERROR);
    CHECK(sof_base_decoder_init(&decoder, SOF_BASE_64, 0) == SOF_BASE_OK);
    CHECK(sof_base_decoder_update(&decoder, "Zg", 2, data, sizeof(data), &written, &offset) == SOF_BASE_OK);
    CHECK(sof_base_decoder_final(&decoder, data, sizeof(data), &written, &offset) == SOF_BASE_BAD_INPUT);
    CHECK(offset == 2 && decoder.encoding == 0);
    CHECK(sof_base_decoder_final(&decoder, data, sizeof(data), &written, &offset) == SOF_BASE_ERROR);
}

/// @brief Random data of every length up to MAX_LENGTH, at every alignment
/// of the data and the text, in every encoding with every combination of
/// flags, encodes as the plain encoder does and decodes back: strictly,
/// leniently, with whitespace added, and with padding taken off.
static void test_round_trip(void) {
    uint64_t rng = 0x0ddba11;
    unsigned char source[MAX_LENGTH];
    for (size_t i = 0; i < sizeof(source); i++) {
        source[i] = (unsigned char)next_random(&rng);
    }
    for (size_t e = 0; e < ENCODINGS; e++) {
        sof_base_encoding_t encoding = encodings[e];
        for (size_t f = 0; f < ENCODE_FLAGS; f++) {
            unsigned flags = encode_flags[f];
            for (size_t length = 0; length <= MAX_LENGTH; length++) {
                char expected[2 * MAX_LENGTH + 8];
                size_t size = ref_encode(encoding, flags, source, length, expected);
                CHECK(sof_base_encoded_size(encoding, flags, length) == size);
                CHECK(sof_base_decoded_size(encoding, size) >= length);
                if (flags & SOF_BASE_NO_PAD || !padded(encoding)) {
                    CHECK(sof_base_decoded_size(encoding, size) == length);
                }
                for (size_t align = 0; align < ALIGNMENTS; align++) {
                    // Each buffer ends where its contents do, so a read or a
                    // write past them is caught by the sanitizers.
                    // A byte more only when both are 0, since malloc(0) may
                    // give NULL.
                    unsigned char *data = malloc(align + length + (align + length == 0));
                    char *text = malloc(align + size + (align + size == 0));
                    unsigned char *back = malloc(align + length + (align + length == 0));
                    CHECK(data != NULL && text != NULL && back != NULL);
                    unsigned char *d = data + align;
                    memcpy(d, source, length);
                    size_t written = UNSET, offset = UNSET;
                    CHECK(sof_base_encode(encoding, flags, d, length, text + align, size, &written) == SOF_BASE_OK);
                    CHECK(written == size && memcmp(text + align, expected, size) == 0);
                    unsigned decode_flags[] = {flags, flags | SOF_BASE_LENIENT, flags | SOF_BASE_SKIP_SPACE};
                    for (size_t g = 0; g < sizeof(decode_flags) / sizeof(decode_flags[0]); g++) {
                        written = UNSET;
                        CHECK(sof_base_decode(encoding, decode_flags[g], text + align, size, back + align, length, &written, &offset) == SOF_BASE_OK);
                        CHECK(written == length && memcmp(back + align, source, length) == 0 && offset == UNSET);
                    }
                    // Line breaks every 7 characters, and spaces around.
                    char spaced[3 * MAX_LENGTH + 16];
                    size_t spaced_size = 0;
                    spaced[spaced_size++] = ' ';
                    for (size_t i = 0; i < size; i++) {
                        spaced[spaced_size++] = text[align + i];
                        if (i % 7 == 6) {
                            spaced[spaced_size++] = '\r';
                            spaced[spaced_size++] = '\n';
                        }
                    }
                    spaced[spaced_size++] = '\t';
                    CHECK(sof_base_decode(encoding, flags | SOF_BASE_SKIP_SPACE, spaced, spaced_size, back + align, length, &written, &offset) == SOF_BASE_OK);
                    CHECK(written == length && memcmp(back + align, source, length) == 0 && offset == UNSET);
                    CHECK(sof_base_decode(encoding, flags, spaced, spaced_size, back + align, length, &written, &offset) == SOF_BASE_BAD_INPUT);
                    CHECK(offset == 0);
                    free(data);
                    free(text);
                    free(back);
                }
            }
        }
    }
}

/// @brief The RFC's examples with padding taken off, whitespace put in, and
/// in lower case, decode in the modes that allow them.
static void test_lenient(void) {
    static const good_t good[] = {
        {SOF_BASE_64, SOF_BASE_LENIENT, "Zg", "f"},
        {SOF_BASE_64, SOF_BASE_LENIENT, "Zg=", "f"},
        {SOF_BASE_64, SOF_BASE_LENIENT, "Zg==", "f"},
        {SOF_BASE_64, SOF_BASE_LENIENT, "Zh", "f"},
        {SOF_BASE_64, SOF_BASE_LENIENT, "Zh=", "f"},
        {SOF_BASE_64, SOF_BASE_LENIENT, "Zm9", "fo"},
        {SOF_BASE_64, SOF_BASE_LENIENT, "Zm9=", "fo"},
        {SOF_BASE_64, SOF_BASE_LENIENT | SOF_BASE_NO_PAD, "Zm8=", "fo"},
        {SOF_BASE_64, SOF_BASE_NO_PAD, "Zm9vYmE", "fooba"},
        {SOF_BASE_64URL, SOF_BASE_NO_PAD, "-_8", "\xfb\xff"},
        {SOF_BASE_64, SOF_BASE_SKIP_SPACE, " Zm9v\r\nYmFy\n", "foobar"},
        {SOF_BASE_64, SOF_BASE_SKIP_SPACE, "Z m 9 v Y g = = ", "foob"},
        {SOF_BASE_64, SOF_BASE_SKIP_SPACE, "\t\v\f ", ""},
        {SOF_BASE_64, SOF_BASE_LENIENT | SOF_BASE_SKIP_SPACE, "Zm9vYg\n=\n", "foob"},
        {SOF_BASE_32, 0, "mzxw6ytboi======", "foobar"},
        {SOF_BASE_32, 0, "MzXw6YtBoI======", "foobar"},
        {SOF_BASE_32, SOF_BASE_LENIENT, "MZXW6YTBOI", "foobar"},
        {SOF_BASE_32, SOF_BASE_LENIENT, "MZXW6YTBOI==", "foobar"},
        {SOF_BASE_32, SOF_BASE_LENIENT, "MZXW6YTBOJ", "foobar"},
        {SOF_BASE_32, SOF_BASE_NO_PAD, "MZXW6YQ", "foob"},
        {SOF_BASE_32, SOF_BASE_SKIP_SPACE, "MZXW6\n===", "foo"},
        {SOF_BASE_32HEX, 0, "cpnmuoj1e8======", "foobar"},
        {SOF_BASE_32HEX, SOF_BASE_LENIENT, "CPNMUOG", "foob"},
        {SOF_BASE_16, 0, "666f6F626172", "foobar"},
        {SOF_BASE_16, SOF_BASE_SKIP_SPACE, "66 6f\n6f", "foo"},
        {SOF_BASE_16, SOF_BASE_NO_PAD | SOF_BASE_LENIENT, "66", "f"},
    };
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        CHECK(gives(&good[i]));
    }
}

/// @brief The sizes the size functions give, for every length up to a few
/// groups, and lengths too large for a size_t.
static void test_sizes(void) {
    for (size_t e = 0; e < ENCODINGS; e++) {
        sof_base_encoding_t encoding = encodings[e];
        unsigned bits = group_bits(encoding), chars = group_chars(encoding);
        for (size_t length = 0; length < 64; length++) {
            size_t unpadded = (length * 8 + bits - 1) / bits;
            size_t padded_size = padded(encoding) ? (unpadded + chars - 1) / chars * chars : unpadded;
            CHECK(sof_base_encoded_size(encoding, 0, length) == padded_size);
            CHECK(sof_base_encoded_size(encoding, SOF_BASE_NO_PAD, length) == unpadded);
            CHECK(sof_base_decoded_size(encoding, length) == length * bits / 8);
        }
        CHECK(sof_base_encoded_size(encoding, 0, SIZE_MAX) == SIZE_MAX);
        CHECK(sof_base_encoded_size(encoding, 0, SIZE_MAX / 4 * 3 + 4) == SIZE_MAX);
        CHECK(sof_base_decoded_size(encoding, SIZE_MAX) <= SIZE_MAX / 8 * bits + bits);
        CHECK(sof_base_decoded_size(encoding, SIZE_MAX) >= SIZE_MAX / 8 * bits);
    }
    CHECK(sof_base_encoded_size(SOF_BASE_64, 0, SIZE_MAX / 4 * 3) == SIZE_MAX / 4 * 4);
}

/// @brief Encoding and decoding in pieces, of random sizes and of single
/// characters, gives what the one-shot functions give, for every length up
/// to MAX_LENGTH, every encoding and every flag.
static void test_streams(void) {
    uint64_t rng = 0xfeed;
    unsigned char source[MAX_LENGTH];
    for (size_t i = 0; i < sizeof(source); i++) {
        source[i] = (unsigned char)next_random(&rng);
    }
    for (size_t e = 0; e < ENCODINGS; e++) {
        sof_base_encoding_t encoding = encodings[e];
        for (size_t f = 0; f < ENCODE_FLAGS; f++) {
            unsigned flags = encode_flags[f];
            for (size_t length = 0; length <= MAX_LENGTH; length++) {
                char expected[2 * MAX_LENGTH + 8], text[2 * MAX_LENGTH + 8];
                size_t size = ref_encode(encoding, flags, source, length, expected);
                CHECK(encode_streamed(encoding, flags, source, length, &rng, text) == size);
                CHECK(memcmp(text, expected, size) == 0);
                unsigned char back[MAX_LENGTH + 8];
                size_t written, offset;
                for (size_t piece = 0; piece < 2; piece++) {
                    CHECK(decode_streamed(encoding, flags, text, size, piece, back, &written, &offset) == SOF_BASE_OK);
                    CHECK(written == length && memcmp(back, source, length) == 0);
                }
            }
        }
    }
    // A stream is ready for new data after final, as if set up again.
    sof_base_encoder_t encoder;
    char text[16];
    size_t written;
    CHECK(sof_base_encoder_init(&encoder, SOF_BASE_32, SOF_BASE_LOWER) == SOF_BASE_OK);
    for (int i = 0; i < 2; i++) {
        CHECK(sof_base_encoder_update(&encoder, "fo", 2, text, 0, &written) == SOF_BASE_OK && written == 0);
        CHECK(encoder.pending == 2);
        CHECK(sof_base_encoder_update(&encoder, "ob", 2, text, 0, &written) == SOF_BASE_OK && written == 0);
        CHECK(sof_base_encoder_final(&encoder, text, 8, &written) == SOF_BASE_OK && written == 8);
        CHECK(memcmp(text, "mzxw6yq=", 8) == 0 && encoder.pending == 0);
    }
    sof_base_decoder_t decoder;
    unsigned char data[8];
    CHECK(sof_base_decoder_init(&decoder, SOF_BASE_64, SOF_BASE_SKIP_SPACE) == SOF_BASE_OK);
    for (int i = 0; i < 2; i++) {
        CHECK(sof_base_decoder_update(&decoder, "Zm9v\nY", 6, data, 6, &written, NULL) == SOF_BASE_OK);
        CHECK(written == 3 && decoder.pending == 1 && decoder.offset == 6);
        CHECK(sof_base_decoder_update(&decoder, "g=", 2, data + 3, 6, &written, NULL) == SOF_BASE_OK);
        CHECK(written == 1 && decoder.padding == 1 && decoder.padding_needed == 2);
        size_t offset = UNSET;
        CHECK(sof_base_decoder_update(&decoder, "=\n", 2, data + 4, 4, &written, &offset) == SOF_BASE_OK);
        CHECK(written == 0 && offset == UNSET);
        CHECK(sof_base_decoder_final(&decoder, data + 4, 0, &written, &offset) == SOF_BASE_OK && written == 0);
        CHECK(memcmp(data, "foob", 4) == 0 && decoder.offset == 0);
    }
    // Offsets count every piece given.
    size_t offset = UNSET;
    CHECK(sof_base_decoder_init(&decoder, SOF_BASE_16, 0) == SOF_BASE_OK);
    CHECK(sof_base_decoder_update(&decoder, "666", 3, data, 8, &written, &offset) == SOF_BASE_OK && written == 1);
    CHECK(sof_base_decoder_update(&decoder, "F6", 2, data, 8, &written, &offset) == SOF_BASE_OK && written == 1);
    CHECK(sof_base_decoder_update(&decoder, "F6x", 3, data, 8, &written, &offset) == SOF_BASE_BAD_INPUT);
    CHECK(offset == 7);
}

/// @brief A buffer one too small fails with SOF_BASE_TOO_SMALL and the size
/// needed, writing nothing when encoding; one of the exact size works.
static void test_too_small(void) {
    uint64_t rng = 0xbadbeef;
    unsigned char source[64];
    for (size_t i = 0; i < sizeof(source); i++) {
        source[i] = (unsigned char)next_random(&rng);
    }
    for (size_t e = 0; e < ENCODINGS; e++) {
        sof_base_encoding_t encoding = encodings[e];
        for (size_t f = 0; f < ENCODE_FLAGS; f++) {
            unsigned flags = encode_flags[f];
            for (size_t length = 1; length < sizeof(source); length++) {
                char text[256];
                size_t size = sof_base_encoded_size(encoding, flags, length), written = UNSET;
                memset(text, '#', sizeof(text));
                CHECK(sof_base_encode(encoding, flags, source, length, text, size - 1, &written) == SOF_BASE_TOO_SMALL);
                CHECK(written == size && text[0] == '#');
                CHECK(sof_base_encode(encoding, flags, source, length, text, size, &written) == SOF_BASE_OK);
                CHECK(written == size && text[size] == '#');

                unsigned char data[64];
                memset(data, 0xa5, sizeof(data));
                CHECK(sof_base_decode(encoding, flags, text, size, data, length - 1, &written, NULL) == SOF_BASE_TOO_SMALL);
                CHECK(written == length && data[length - 1] == 0xa5);
                CHECK(sof_base_decode(encoding, flags, text, size, data, length, &written, NULL) == SOF_BASE_OK);
                CHECK(written == length && memcmp(data, source, length) == 0 && data[length] == 0xa5);

                // Streams: an update or final one short changes nothing.
                sof_base_encoder_t encoder;
                char streamed[256];
                size_t at = 0, n;
                CHECK(sof_base_encoder_init(&encoder, encoding, flags) == SOF_BASE_OK);
                size_t first = length / 2;
                for (size_t pass = 0; pass < 2; pass++) {
                    const unsigned char *p = pass == 0 ? source : source + first;
                    size_t take = pass == 0 ? first : length - first;
                    sof_base_error_t err = sof_base_encoder_update(&encoder, p, take, NULL, 0, &n);
                    CHECK(err == (n == 0 ? SOF_BASE_OK : SOF_BASE_TOO_SMALL));
                    if (n != 0) {
                        sof_base_encoder_t before = encoder;
                        CHECK(sof_base_encoder_update(&encoder, p, take, streamed + at, n - 1, &n) == SOF_BASE_TOO_SMALL);
                        CHECK(memcmp(&before, &encoder, sizeof(encoder)) == 0);
                        CHECK(sof_base_encoder_update(&encoder, p, take, streamed + at, n, &n) == SOF_BASE_OK);
                    }
                    at += n;
                }
                sof_base_error_t err = sof_base_encoder_final(&encoder, NULL, 0, &n);
                CHECK(err == (n == 0 ? SOF_BASE_OK : SOF_BASE_TOO_SMALL));
                if (n != 0) {
                    CHECK(sof_base_encoder_final(&encoder, streamed + at, n - 1, &n) == SOF_BASE_TOO_SMALL);
                    CHECK(sof_base_encoder_final(&encoder, streamed + at, n, &n) == SOF_BASE_OK);
                }
                at += n;
                CHECK(at == size && memcmp(streamed, text, size) == 0);

                sof_base_decoder_t decoder;
                CHECK(sof_base_decoder_init(&decoder, encoding, flags | SOF_BASE_LENIENT) == SOF_BASE_OK);
                // Unpadded, the last group is decoded by final.
                size_t chars = sof_base_encoded_size(encoding, SOF_BASE_NO_PAD, length);
                size_t room = sof_base_decoded_size(encoding, chars);
                sof_base_decoder_t before = decoder;
                CHECK(sof_base_decoder_update(&decoder, text, chars, data, room - 1, &n, NULL) == SOF_BASE_TOO_SMALL);
                CHECK(n == room && memcmp(&before, &decoder, sizeof(decoder)) == 0);
                CHECK(sof_base_decoder_update(&decoder, text, chars, data, room, &n, NULL) == SOF_BASE_OK);
                at = n;
                size_t last = length - at;
                if (last != 0) {
                    before = decoder;
                    CHECK(sof_base_decoder_final(&decoder, data + at, last - 1, &n, NULL) == SOF_BASE_TOO_SMALL);
                    CHECK(n == last && memcmp(&before, &decoder, sizeof(decoder)) == 0);
                }
                CHECK(sof_base_decoder_final(&decoder, data + at, last, &n, NULL) == SOF_BASE_OK && n == last);
                CHECK(memcmp(data, source, length) == 0);
            }
        }
    }
}

/// @brief The test vectors of RFC 4648, section 10, encode and decode both
/// ways, as do their lower-case and unpadded forms, and bytes that differ
/// between base64 and base64url.
static void test_vectors(void) {
    static const vector_t vectors[] = {
        {SOF_BASE_64, "", ""},
        {SOF_BASE_64, "f", "Zg=="},
        {SOF_BASE_64, "fo", "Zm8="},
        {SOF_BASE_64, "foo", "Zm9v"},
        {SOF_BASE_64, "foob", "Zm9vYg=="},
        {SOF_BASE_64, "fooba", "Zm9vYmE="},
        {SOF_BASE_64, "foobar", "Zm9vYmFy"},
        {SOF_BASE_32, "", ""},
        {SOF_BASE_32, "f", "MY======"},
        {SOF_BASE_32, "fo", "MZXQ===="},
        {SOF_BASE_32, "foo", "MZXW6==="},
        {SOF_BASE_32, "foob", "MZXW6YQ="},
        {SOF_BASE_32, "fooba", "MZXW6YTB"},
        {SOF_BASE_32, "foobar", "MZXW6YTBOI======"},
        {SOF_BASE_32HEX, "", ""},
        {SOF_BASE_32HEX, "f", "CO======"},
        {SOF_BASE_32HEX, "fo", "CPNG===="},
        {SOF_BASE_32HEX, "foo", "CPNMU==="},
        {SOF_BASE_32HEX, "foob", "CPNMUOG="},
        {SOF_BASE_32HEX, "fooba", "CPNMUOJ1"},
        {SOF_BASE_32HEX, "foobar", "CPNMUOJ1E8======"},
        {SOF_BASE_16, "", ""},
        {SOF_BASE_16, "f", "66"},
        {SOF_BASE_16, "fo", "666F"},
        {SOF_BASE_16, "foo", "666F6F"},
        {SOF_BASE_16, "foob", "666F6F62"},
        {SOF_BASE_16, "fooba", "666F6F6261"},
        {SOF_BASE_16, "foobar", "666F6F626172"},
        // The two alphabets differ in their last two characters.
        {SOF_BASE_64, "\xfb\xff", "+/8="},
        {SOF_BASE_64URL, "\xfb\xff", "-_8="},
        {SOF_BASE_64URL, "\xfb\xef\xbe", "----"},
        {SOF_BASE_64URL, "\xff\xff\xff", "____"},
        {SOF_BASE_64URL, "foob", "Zm9vYg=="},
        {SOF_BASE_16, "\x01\x23\x45\x67\x89\xab\xcd\xef", "0123456789ABCDEF"},
    };
    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        const vector_t *v = &vectors[i];
        size_t length = strlen(v->data), size = strlen(v->text);
        for (size_t f = 0; f < ENCODE_FLAGS; f++) {
            unsigned flags = encode_flags[f];
            // The expected text in this case and padding.
            char expected[32];
            size_t expected_size = 0;
            for (size_t j = 0; j < size; j++) {
                char c = v->text[j];
                if (c == '=' && (flags & SOF_BASE_NO_PAD)) {
                    continue;
                }
                if ((flags & SOF_BASE_LOWER) && v->encoding != SOF_BASE_64 && v->encoding != SOF_BASE_64URL && c >= 'A' && c <= 'Z') {
                    c = (char)(c - 'A' + 'a');
                }
                expected[expected_size++] = c;
            }
            char text[32];
            size_t written = UNSET;
            CHECK(sof_base_encode(v->encoding, flags, v->data, length, text, sizeof(text), &written) == SOF_BASE_OK);
            CHECK(written == expected_size && memcmp(text, expected, written) == 0);
            unsigned char data[32];
            CHECK(sof_base_decode(v->encoding, flags, expected, expected_size, data, sizeof(data), &written, NULL) == SOF_BASE_OK);
            CHECK(written == length && memcmp(data, v->data, length) == 0);
            good_t good = {v->encoding, flags, v->text, v->data};
            if (!(flags & SOF_BASE_NO_PAD)) {
                CHECK(gives(&good));
            }
        }
    }
}

int main(void) {
    printf("sof_base:\n");
    run("RFC 4648 test vectors", test_vectors);
    run("sizes", test_sizes);
    run("every character in every position", test_alphabets);
    run("one encoding for each byte string", test_canonical);
    run("round trips at every length and alignment", test_round_trip);
    run("streams in pieces", test_streams);
    run("lenient decoding and whitespace", test_lenient);
    run("bad input and its offset", test_bad_input);
    run("buffers too small by one", test_too_small);
    run("NULL, bad arguments and dead streams", test_null);
    return test_report();
}
