/**
 * @file sof_base.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Base16, base32 and base64 encoding and decoding in C.
 * @details Each encoding is described by a spec_t: the bits a character
 * holds, the characters and bytes in a group, whether it pads, and its
 * alphabets and decoding table. Whole groups are encoded and decoded a
 * group at a time in a loop for each group size; a decoding table maps
 * every byte to its value or to SPACE, PAD or BAD, all of which have the
 * high bits set, so a group of data characters is checked with one test.
 * Anything else (whitespace, padding, a bad character, a group split
 * between pieces) is handled a character at a time by the same state a
 * sof_base_decoder_t keeps, which the one-shot sof_base_decode() uses too.
 * Bytes are read and written one at a time, so alignment doesn't matter,
 * and nothing is read past the end of the input.
 * @version 0.0.1
 * @date 2026-11-20
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_base.h"

/// @brief A decoding table's value for ASCII whitespace.
#define SPACE 0xfd

/// @brief A decoding table's value for '=', in encodings that pad.
#define PAD 0xfe

/// @brief A decoding table's value for any other character that isn't in
/// the alphabet.
#define BAD 0xff

/// @brief Every flag there is.
#define ALL_FLAGS (SOF_BASE_LOWER | SOF_BASE_NO_PAD | SOF_BASE_LENIENT | SOF_BASE_SKIP_SPACE)

/// @brief What sets an encoding apart.
typedef struct spec_t {
    /// @brief The bits a character holds: 4, 5 or 6.
    unsigned bits;
    /// @brief The characters in a group.
    unsigned chars;
    /// @brief The bytes in a group.
    unsigned bytes;
    /// @brief Whether a short last group is padded with '='.
    int padded;
    /// @brief The alphabet, and the one SOF_BASE_LOWER asks for.
    const char *alphabet;
    const char *lower;
    /// @brief The decoding table.
    const unsigned char *decode;
} spec_t;

static const unsigned char *decode_groups(const spec_t *spec, const unsigned char *p, const unsigned char *end, unsigned char *out, size_t size, size_t *n);
static sof_base_error_t decode_end(sof_base_decoder_t *decoder, unsigned char *out, size_t size, size_t *n, size_t *offset);
static int decode_group_end(sof_base_decoder_t *decoder, unsigned char *out, size_t size, size_t *n, size_t at, size_t *offset);
static sof_base_error_t decode_run(sof_base_decoder_t *decoder, const unsigned char *p, size_t length, unsigned char *out, size_t size, size_t *n, size_t *offset);
static size_t emit(unsigned char *out, size_t size, size_t n, uint64_t bits, unsigned bytes);
static char *encode_groups(const spec_t *spec, const char *alphabet, const unsigned char *p, size_t groups, char *out);
static size_t encode_tail(const spec_t *spec, unsigned flags, const unsigned char *p, size_t length, char *out);
static size_t encode_tail_size(const spec_t *spec, unsigned flags, size_t length);
static const spec_t *find_spec(sof_base_encoding_t encoding, unsigned flags);
static size_t group_bytes(const spec_t *spec, size_t chars);
static void set(size_t *p, size_t value);

/// @brief The value of each character in base16, or SPACE, PAD or BAD.
static const unsigned char decode_base16[256] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xfd, 0xfd, 0xfd, 0xfd, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/// @brief The value of each character in base32, or SPACE, PAD or BAD.
static const unsigned char decode_base32[256] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xfd, 0xfd, 0xfd, 0xfd, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff,
    0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
    0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
    0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/// @brief The value of each character in base32hex, or SPACE, PAD or BAD.
static const unsigned char decode_base32hex[256] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xfd, 0xfd, 0xfd, 0xfd, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff,
    0xff, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/// @brief The value of each character in base64, or SPACE, PAD or BAD.
static const unsigned char decode_base64[256] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xfd, 0xfd, 0xfd, 0xfd, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3e, 0xff, 0xff, 0xff, 0x3f,
    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff,
    0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
    0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/// @brief The value of each character in base64url, or SPACE, PAD or BAD.
static const unsigned char decode_base64url[256] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xfd, 0xfd, 0xfd, 0xfd, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3e, 0xff, 0xff,
    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff,
    0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
    0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xff, 0xff, 0xff, 0xff, 0x3f,
    0xff, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/// @brief Each encoding's spec, by its sof_base_encoding_t.
static const spec_t specs[] = {
    [SOF_BASE_16] = {4, 2, 1, 0, "0123456789ABCDEF", "0123456789abcdef", decode_base16},
    [SOF_BASE_32] = {5, 8, 5, 1, "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", "abcdefghijklmnopqrstuvwxyz234567",
        decode_base32},
    [SOF_BASE_32HEX] = {5, 8, 5, 1, "0123456789ABCDEFGHIJKLMNOPQRSTUV", "0123456789abcdefghijklmnopqrstuv",
        decode_base32hex},
    [SOF_BASE_64] = {6, 4, 3, 1, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", decode_base64},
    [SOF_BASE_64URL] = {6, 4, 3, 1, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_", decode_base64url},
};

/// @brief Decodes whole groups of data characters from p, writing their
/// bytes at out + *n while a whole group fits in size, until a group has a
/// character that isn't data or fewer than a group are left.
/// @return Where it stopped.
static const unsigned char *decode_groups(const spec_t *spec, const unsigned char *p, const unsigned char *end, unsigned char *out, size_t size, size_t *n) {
    const unsigned char *t = spec->decode;
    size_t i = *n;
    if (spec->bits == 6) {
        while (end - p >= 8 && i + 6 <= size) {
            uint32_t a = t[p[0]], b = t[p[1]], c = t[p[2]], d = t[p[3]];
            uint32_t e = t[p[4]], f = t[p[5]], g = t[p[6]], h = t[p[7]];
            if ((a | b | c | d | e | f | g | h) & 0xc0) {
                break;
            }
            uint32_t v = a << 18 | b << 12 | c << 6 | d;
            uint32_t w = e << 18 | f << 12 | g << 6 | h;
            out[i] = (unsigned char)(v >> 16);
            out[i + 1] = (unsigned char)(v >> 8);
            out[i + 2] = (unsigned char)v;
            out[i + 3] = (unsigned char)(w >> 16);
            out[i + 4] = (unsigned char)(w >> 8);
            out[i + 5] = (unsigned char)w;
            p += 8;
            i += 6;
        }
        while (end - p >= 4 && i + 3 <= size) {
            uint32_t a = t[p[0]], b = t[p[1]], c = t[p[2]], d = t[p[3]];
            if ((a | b | c | d) & 0xc0) {
                break;
            }
            uint32_t v = a << 18 | b << 12 | c << 6 | d;
            out[i] = (unsigned char)(v >> 16);
            out[i + 1] = (unsigned char)(v >> 8);
            out[i + 2] = (unsigned char)v;
            p += 4;
            i += 3;
        }
    } else if (spec->bits == 5) {
        while (end - p >= 8 && i + 5 <= size) {
            uint64_t a = t[p[0]], b = t[p[1]], c = t[p[2]], d = t[p[3]];
            uint64_t e = t[p[4]], f = t[p[5]], g = t[p[6]], h = t[p[7]];
            if ((a | b | c | d | e | f | g | h) & 0xe0) {
                break;
            }
            uint64_t v = a << 35 | b << 30 | c << 25 | d << 20 | e << 15 | f << 10 | g << 5 | h;
            out[i] = (unsigned char)(v >> 32);
            out[i + 1] = (unsigned char)(v >> 24);
            out[i + 2] = (unsigned char)(v >> 16);
            out[i + 3] = (unsigned char)(v >> 8);
            out[i + 4] = (unsigned char)v;
            p += 8;
            i += 5;
        }
    } else {
        while (end - p >= 4 && i + 2 <= size) {
            unsigned a = t[p[0]], b = t[p[1]], c = t[p[2]], d = t[p[3]];
            if ((a | b | c | d) & 0xf0) {
                break;
            }
            out[i] = (unsigned char)(a << 4 | b);
            out[i + 1] = (unsigned char)(c << 4 | d);
            p += 4;
            i += 2;
        }
        while (end - p >= 2 && i + 1 <= size) {
            unsigned a = t[p[0]], b = t[p[1]];
            if ((a | b) & 0xf0) {
                break;
            }
            out[i] = (unsigned char)(a << 4 | b);
            p += 2;
            i += 1;
        }
    }
    *n = i;
    return p;
}

/// @brief Ends a decoding: checks that the text ended where it may, and
/// decodes a last group that wasn't padded. Bytes are written at out + *n
/// while they fit in size, and counted in *n either way.
/// @return SOF_BASE_OK, or SOF_BASE_BAD_INPUT with *offset set.
static sof_base_error_t decode_end(sof_base_decoder_t *decoder, unsigned char *out, size_t size, size_t *n, size_t *offset) {
    const spec_t *spec = &specs[decoder->encoding];
    if (decoder->padding != 0) {
        if (decoder->padding < decoder->padding_needed && !(decoder->flags & SOF_BASE_LENIENT)) {
            *offset = decoder->offset;
            return SOF_BASE_BAD_INPUT;
        }
        return SOF_BASE_OK;
    }
    if (decoder->pending == 0) {
        return SOF_BASE_OK;
    }
    if (spec->padded && !(decoder->flags & (SOF_BASE_NO_PAD | SOF_BASE_LENIENT)) && group_bytes(spec, decoder->pending) != 0) {
        // A group that could end the text, but without the padding it needs.
        *offset = decoder->offset;
        return SOF_BASE_BAD_INPUT;
    }
    if (decode_group_end(decoder, out, size, n, decoder->offset, offset) != 0) {
        return SOF_BASE_BAD_INPUT;
    }
    return SOF_BASE_OK;
}

/// @brief Decodes the short group that ends the data, before padding or the
/// end of the text at offset at. Bytes are written at out + *n while they
/// fit in size, and counted in *n either way.
/// @return 0, or -1 with *offset set to at if the group is of a length no
/// bytes encode to, or to its last character if bits that don't make a
/// whole byte aren't 0 in strict decoding.
static int decode_group_end(sof_base_decoder_t *decoder, unsigned char *out, size_t size, size_t *n, size_t at, size_t *offset) {
    const spec_t *spec = &specs[decoder->encoding];
    size_t bytes = group_bytes(spec, decoder->pending);
    if (bytes == 0) {
        *offset = at;
        return -1;
    }
    unsigned extra = (unsigned)(decoder->pending * spec->bits - bytes * 8);
    uint64_t bits = decoder->bits;
    if ((bits & (((uint64_t)1 << extra) - 1)) != 0 && !(decoder->flags & SOF_BASE_LENIENT)) {
        *offset = decoder->last;
        return -1;
    }
    *n = emit(out, size, *n, bits >> extra, (unsigned)bytes);
    decoder->pending = 0;
    decoder->bits = 0;
    return 0;
}

/// @brief Decodes the length characters at p with a decoder, writing bytes
/// at out + *n while they fit in size and counting them in *n either way.
/// Whole groups go through decode_groups(); everything else a character
/// at a time.
/// @return SOF_BASE_OK, or SOF_BASE_BAD_INPUT with *offset set.
static sof_base_error_t decode_run(sof_base_decoder_t *decoder, const unsigned char *p, size_t length, unsigned char *out, size_t size, size_t *n, size_t *offset) {
    const spec_t *spec = &specs[decoder->encoding];
    const unsigned char *start = p, *end = p + length;
    unsigned flags = decoder->flags;
    while (p < end) {
        if (decoder->pending == 0 && decoder->padding == 0) {
            p = decode_groups(spec, p, end, out, size, n);
            if (p == end) {
                break;
            }
        }
        size_t at = decoder->offset + (size_t)(p - start);
        unsigned v = spec->decode[*p];
        if (v < SPACE) {
            if (decoder->padding != 0) {
                *offset = at;
                return SOF_BASE_BAD_INPUT;
            }
            decoder->bits = decoder->bits << spec->bits | v;
            decoder->last = at;
            if (++decoder->pending == spec->chars) {
                *n = emit(out, size, *n, decoder->bits, spec->bytes);
                decoder->pending = 0;
                decoder->bits = 0;
            }
        } else if (v == SPACE && (flags & SOF_BASE_SKIP_SPACE)) {
            // Skipped.
        } else if (v == PAD) {
            if (decoder->padding == 0) {
                if ((flags & SOF_BASE_NO_PAD) && !(flags & SOF_BASE_LENIENT)) {
                    *offset = at;
                    return SOF_BASE_BAD_INPUT;
                }
                size_t needed = spec->chars - decoder->pending;
                if (decode_group_end(decoder, out, size, n, at, offset) != 0) {
                    return SOF_BASE_BAD_INPUT;
                }
                decoder->padding_needed = needed;
            } else if (decoder->padding == decoder->padding_needed) {
                *offset = at;
                return SOF_BASE_BAD_INPUT;
            }
            decoder->padding++;
        } else {
            *offset = at;
            return SOF_BASE_BAD_INPUT;
        }
        p++;
    }
    return SOF_BASE_OK;
}

/// @brief Writes the low bytes * 8 bits of bits, the highest byte first,
/// at out + n while they fit in size.
/// @return n + bytes.
static size_t emit(unsigned char *out, size_t size, size_t n, uint64_t bits, unsigned bytes) {
    for (unsigned i = bytes; i-- > 0; n++) {
        if (n < size) {
            out[n] = (unsigned char)(bits >> (8 * i));
        }
    }
    return n;
}

/// @brief Encodes groups whole groups from p into out with alphabet.
/// @return Where the characters end.
static char *encode_groups(const spec_t *spec, const char *alphabet, const unsigned char *p, size_t groups, char *out) {
    if (spec->bits == 6) {
        for (size_t g = 0; g < groups; g++, p += 3, out += 4) {
            uint32_t v = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
            out[0] = alphabet[v >> 18];
            out[1] = alphabet[v >> 12 & 63];
            out[2] = alphabet[v >> 6 & 63];
            out[3] = alphabet[v & 63];
        }
    } else if (spec->bits == 5) {
        for (size_t g = 0; g < groups; g++, p += 5, out += 8) {
            uint64_t v = (uint64_t)p[0] << 32 | (uint64_t)p[1] << 24 | (uint64_t)p[2] << 16 | (uint64_t)p[3] << 8 | p[4];
            out[0] = alphabet[v >> 35];
            out[1] = alphabet[v >> 30 & 31];
            out[2] = alphabet[v >> 25 & 31];
            out[3] = alphabet[v >> 20 & 31];
            out[4] = alphabet[v >> 15 & 31];
            out[5] = alphabet[v >> 10 & 31];
            out[6] = alphabet[v >> 5 & 31];
            out[7] = alphabet[v & 31];
        }
    } else {
        for (size_t g = 0; g < groups; g++, p++, out += 2) {
            out[0] = alphabet[*p >> 4];
            out[1] = alphabet[*p & 15];
        }
    }
    return out;
}

/// @brief Encodes the length bytes at p, fewer than a group, into out,
/// with padding unless the flags or the encoding have none.
/// @return The characters written.
static size_t encode_tail(const spec_t *spec, unsigned flags, const unsigned char *p, size_t length, char *out) {
    if (length == 0) {
        return 0;
    }
    const char *alphabet = (flags & SOF_BASE_LOWER) ? spec->lower : spec->alphabet;
    uint64_t bits = 0;
    for (size_t i = 0; i < length; i++) {
        bits = bits << 8 | p[i];
    }
    size_t chars = (length * 8 + spec->bits - 1) / spec->bits;
    bits <<= chars * spec->bits - length * 8;
    for (size_t i = chars; i-- > 0;) {
        out[chars - 1 - i] = alphabet[bits >> (i * spec->bits) & ((1u << spec->bits) - 1)];
    }
    size_t total = encode_tail_size(spec, flags, length);
    for (size_t i = chars; i < total; i++) {
        out[i] = '=';
    }
    return total;
}

/// @brief Gives the characters length bytes, fewer than a group, encode to,
/// with padding unless the flags or the encoding have none.
static size_t encode_tail_size(const spec_t *spec, unsigned flags, size_t length) {
    if (length == 0) {
        return 0;
    }
    if (spec->padded && !(flags & SOF_BASE_NO_PAD)) {
        return spec->chars;
    }
    return (length * 8 + spec->bits - 1) / spec->bits;
}

/// @brief Returns the spec of an encoding, or NULL if the encoding or the
/// flags are bad.
static const spec_t *find_spec(sof_base_encoding_t encoding, unsigned flags) {
    if (encoding < SOF_BASE_16 || encoding > SOF_BASE_64URL || (flags & ~(unsigned)ALL_FLAGS) != 0) {
        return NULL;
    }
    return &specs[encoding];
}

/// @brief Gives the bytes a short last group of chars characters decodes
/// to, or 0 if no number of bytes encodes to that many characters.
static size_t group_bytes(const spec_t *spec, size_t chars) {
    size_t bytes = chars * spec->bits / 8;
    if (bytes == 0 || (bytes * 8 + spec->bits - 1) / spec->bits != chars) {
        return 0;
    }
    return bytes;
}

/// @brief Sets *p to value, if p isn't NULL.
static void set(size_t *p, size_t value) {
    if (p != NULL) {
        *p = value;
    }
}

sof_base_error_t sof_base_decode(sof_base_encoding_t encoding, unsigned flags, const char *text, size_t length, void *data, size_t size, size_t *written, size_t *offset) {
    set(written, 0);
    sof_base_decoder_t decoder;
    if (sof_base_decoder_init(&decoder, encoding, flags) != SOF_BASE_OK) {
        return SOF_BASE_ERROR;
    }
    if (text == NULL) {
        text = "";
        length = 0;
    }
    if (data == NULL) {
        size = 0;
    }
    size_t n = 0, bad = 0;
    sof_base_error_t err = decode_run(&decoder, (const unsigned char *)text, length, data, size, &n, &bad);
    if (err == SOF_BASE_OK) {
        decoder.offset = length;
        err = decode_end(&decoder, data, size, &n, &bad);
    }
    if (err != SOF_BASE_OK) {
        set(offset, bad);
        return err;
    }
    set(written, n);
    return n > size ? SOF_BASE_TOO_SMALL : SOF_BASE_OK;
}

size_t sof_base_decoded_size(sof_base_encoding_t encoding, size_t length) {
    const spec_t *spec = find_spec(encoding, 0);
    if (spec == NULL) {
        return 0;
    }
    return length / spec->chars * spec->bytes + length % spec->chars * spec->bits / 8;
}

sof_base_error_t sof_base_decoder_final(sof_base_decoder_t *decoder, void *data, size_t size, size_t *written, size_t *offset) {
    set(written, 0);
    if (decoder == NULL || find_spec(decoder->encoding, decoder->flags) == NULL) {
        return SOF_BASE_ERROR;
    }
    if (data == NULL) {
        size = 0;
    }
    // A trial on a copy, writing nothing, finds the error or the size.
    sof_base_decoder_t trial = *decoder;
    size_t n = 0, bad = 0;
    if (decode_end(&trial, NULL, 0, &n, &bad) != SOF_BASE_OK) {
        decoder->encoding = 0;
        set(offset, bad);
        return SOF_BASE_BAD_INPUT;
    }
    if (n > size) {
        set(written, n);
        return SOF_BASE_TOO_SMALL;
    }
    n = 0;
    decode_end(decoder, data, size, &n, &bad);
    set(written, n);
    return sof_base_decoder_init(decoder, decoder->encoding, decoder->flags);
}

sof_base_error_t sof_base_decoder_init(sof_base_decoder_t *decoder, sof_base_encoding_t encoding, unsigned flags) {
    if (decoder == NULL) {
        return SOF_BASE_ERROR;
    }
    memset(decoder, 0, sizeof(*decoder));
    if (find_spec(encoding, flags) == NULL) {
        return SOF_BASE_ERROR;
    }
    decoder->encoding = encoding;
    decoder->flags = flags;
    return SOF_BASE_OK;
}

sof_base_error_t sof_base_decoder_update(sof_base_decoder_t *decoder, const char *text, size_t length, void *data, size_t size, size_t *written, size_t *offset) {
    set(written, 0);
    if (decoder == NULL) {
        return SOF_BASE_ERROR;
    }
    const spec_t *spec = find_spec(decoder->encoding, decoder->flags);
    if (spec == NULL) {
        return SOF_BASE_ERROR;
    }
    if (text == NULL) {
        text = "";
        length = 0;
    }
    if (data == NULL) {
        size = 0;
    }
    // The bytes of pending + length characters, without overflow.
    size_t room = length / spec->chars * spec->bytes + (length % spec->chars + decoder->pending) * spec->bits / 8;
    if (size < room) {
        set(written, room);
        return SOF_BASE_TOO_SMALL;
    }
    size_t n = 0, bad = 0;
    if (decode_run(decoder, (const unsigned char *)text, length, data, size, &n, &bad) != SOF_BASE_OK) {
        decoder->encoding = 0;
        set(offset, bad);
        return SOF_BASE_BAD_INPUT;
    }
    decoder->offset += length;
    set(written, n);
    return SOF_BASE_OK;
}

sof_base_error_t sof_base_encode(sof_base_encoding_t encoding, unsigned flags, const void *data, size_t length, char *text, size_t size, size_t *written) {
    set(written, 0);
    const spec_t *spec = find_spec(encoding, flags);
    if (spec == NULL) {
        return SOF_BASE_ERROR;
    }
    if (data == NULL) {
        data = "";
        length = 0;
    }
    if (text == NULL) {
        size = 0;
    }
    size_t need = sof_base_encoded_size(encoding, flags, length);
    set(written, need);
    if (size < need) {
        return SOF_BASE_TOO_SMALL;
    }
    const unsigned char *p = data;
    size_t groups = length / spec->bytes;
    char *out = encode_groups(spec, (flags & SOF_BASE_LOWER) ? spec->lower : spec->alphabet, p, groups, text);
    encode_tail(spec, flags, p + groups * spec->bytes, length % spec->bytes, out);
    return SOF_BASE_OK;
}

size_t sof_base_encoded_size(sof_base_encoding_t encoding, unsigned flags, size_t length) {
    const spec_t *spec = find_spec(encoding, flags & SOF_BASE_NO_PAD);
    if (spec == NULL) {
        return 0;
    }
    size_t groups = length / spec->bytes;
    size_t tail = encode_tail_size(spec, flags, length % spec->bytes);
    if (groups > (SIZE_MAX - tail) / spec->chars) {
        return SIZE_MAX;
    }
    return groups * spec->chars + tail;
}

sof_base_error_t sof_base_encoder_final(sof_base_encoder_t *encoder, char *text, size_t size, size_t *written) {
    set(written, 0);
    if (encoder == NULL) {
        return SOF_BASE_ERROR;
    }
    const spec_t *spec = find_spec(encoder->encoding, encoder->flags);
    if (spec == NULL) {
        return SOF_BASE_ERROR;
    }
    if (text == NULL) {
        size = 0;
    }
    size_t need = encode_tail_size(spec, encoder->flags, encoder->pending);
    set(written, need);
    if (size < need) {
        return SOF_BASE_TOO_SMALL;
    }
    encode_tail(spec, encoder->flags, encoder->bytes, encoder->pending, text);
    encoder->pending = 0;
    return SOF_BASE_OK;
}

sof_base_error_t sof_base_encoder_init(sof_base_encoder_t *encoder, sof_base_encoding_t encoding, unsigned flags) {
    if (encoder == NULL) {
        return SOF_BASE_ERROR;
    }
    memset(encoder, 0, sizeof(*encoder));
    if (find_spec(encoding, flags) == NULL) {
        return SOF_BASE_ERROR;
    }
    encoder->encoding = encoding;
    encoder->flags = flags;
    return SOF_BASE_OK;
}

sof_base_error_t sof_base_encoder_update(sof_base_encoder_t *encoder, const void *data, size_t length, char *text, size_t size, size_t *written) {
    set(written, 0);
    if (encoder == NULL) {
        return SOF_BASE_ERROR;
    }
    const spec_t *spec = find_spec(encoder->encoding, encoder->flags);
    if (spec == NULL) {
        return SOF_BASE_ERROR;
    }
    if (data == NULL) {
        data = "";
        length = 0;
    }
    if (text == NULL) {
        size = 0;
    }
    // The whole groups of pending + length bytes, without overflow.
    size_t groups = length / spec->bytes + (length % spec->bytes + encoder->pending) / spec->bytes;
    size_t need = groups > SIZE_MAX / spec->chars ? SIZE_MAX : groups * spec->chars;
    set(written, need);
    if (size < need) {
        return SOF_BASE_TOO_SMALL;
    }
    const char *alphabet = (encoder->flags & SOF_BASE_LOWER) ? spec->lower : spec->alphabet;
    const unsigned char *p = data;
    char *out = text;
    if (encoder->pending != 0) {
        size_t take = spec->bytes - encoder->pending;
        if (take > length) {
            take = length;
        }
        if (take != 0) {
            memcpy(encoder->bytes + encoder->pending, p, take);
        }
        encoder->pending += take;
        p += take;
        length -= take;
        if (encoder->pending < spec->bytes) {
            return SOF_BASE_OK;
        }
        out = encode_groups(spec, alphabet, encoder->bytes, 1, out);
        encoder->pending = 0;
    }
    size_t whole = length / spec->bytes;
    encode_groups(spec, alphabet, p, whole, out);
    encoder->pending = length % spec->bytes;
    if (encoder->pending != 0) {
        memcpy(encoder->bytes, p + whole * spec->bytes, encoder->pending);
    }
    return SOF_BASE_OK;
}
