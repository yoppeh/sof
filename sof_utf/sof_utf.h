/**
 * @file sof_utf.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief UTF-8, UTF-16 and UTF-32 encoding, decoding, validation and
 * iteration in c.
 * @details The library moves between Unicode code points, held in uint32_t,
 * and their UTF-8, UTF-16 and UTF-32 encodings. sof_utf_valid() says
 * whether a number is a code point that can be encoded (a Unicode scalar
 * value: at most U+10FFFF and not a surrogate). For UTF-8 and UTF-16 there
 * is a function to give the size of a code point's encoding, encode one,
 * decode one, step through a string one code point at a time, count the
 * code points in a string and find its first error. UTF-32 holds one code
 * point per unit, so it needs only a function to find its first error. For
 * each encoding there are functions to convert a whole string to the other
 * two.
 *
 * UTF-8 strings are bytes, passed as const void * with their size in bytes,
 * so char and unsigned char buffers both work and NUL is an ordinary
 * character. UTF-16 and UTF-32 strings are arrays of uint16_t and uint32_t
 * in the machine's byte order, with their length counted in units. Nothing
 * is NUL-terminated or allocated; output goes to a buffer the caller
 * provides.
 *
 * Decoding is strict, as the Unicode standard requires: overlong forms,
 * surrogates, values past U+10FFFF, unpaired UTF-16 surrogates and UTF-32
 * units that aren't code points are errors, which decode as SOF_UTF_INVALID, and a sequence cut off by the
 * end of the data decodes as SOF_UTF_INCOMPLETE. Each error covers the
 * bytes the standard's "maximal subpart" practice says it should, so
 * replacing each with U+FFFD, as the conversions do, gives the same result
 * as other conforming decoders.
 *
 * Functions are named sof_utf8_, sof_utf16_ and sof_utf32_ for each
 * encoding, and sof_utf_ for code points. NULL data is treated as empty. Everything is
 * static inline, so there is no sof_utf.c.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_UTF_H
#define SOF_UTF_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/// @brief The largest code point, U+10FFFF.
#define SOF_UTF_MAX ((uint32_t)0x10ffff)

/// @brief U+FFFD, the replacement character, which the conversions write in
/// place of each error.
#define SOF_UTF_REPLACEMENT ((uint32_t)0xfffd)

/// @brief What decoding gives for data that isn't valid in its encoding.
/// It is above SOF_UTF_MAX, so sof_utf_valid() rejects it.
#define SOF_UTF_INVALID ((uint32_t)0xffffffff)

/// @brief What decoding gives for a sequence that is valid so far but cut
/// off by the end of the data, or for no data at all. It is above
/// SOF_UTF_MAX, so sof_utf_valid() rejects it.
#define SOF_UTF_INCOMPLETE ((uint32_t)0xfffffffe)

/// @brief The most bytes a code point takes in UTF-8.
#define SOF_UTF8_SIZE_MAX 4

/// @brief The most units a code point takes in UTF-16.
#define SOF_UTF16_SIZE_MAX 2

/**
 * @brief Counts the code points in a UTF-16 string.
 * @details Each error counts as one, as it would after conversion, where
 * it becomes U+FFFD.
 * @param data The string, or NULL for none.
 * @param count Its length in units.
 * @return The number of code points; 0 if data is NULL.
 */
static inline size_t sof_utf16_count(const uint16_t *data, size_t count);

/**
 * @brief Decodes the code point at the start of a UTF-16 string.
 * @param data The string, or NULL for none.
 * @param count Its length in units.
 * @param code Where the code point goes: SOF_UTF_INVALID for a low
 * surrogate, or a high one not followed by a low one, and
 * SOF_UTF_INCOMPLETE for a high surrogate that is the last unit, or for
 * no data. May be NULL.
 * @return The units read: 1 or 2, or 0 if data is NULL or count is 0. For
 * an error it is 1, the unit to skip.
 */
static inline size_t sof_utf16_decode(const uint16_t *data, size_t count, uint32_t *code);

/**
 * @brief Encodes a code point in UTF-16.
 * @param code The code point.
 * @param buffer Where the units go.
 * @param count The units buffer has room for.
 * @return The units written, 1 or 2, or 0, writing nothing, if code isn't
 * valid, buffer is NULL or count is too small.
 */
static inline size_t sof_utf16_encode(uint32_t code, uint16_t *buffer, size_t count);

/**
 * @brief Decodes the code point at a position in a UTF-16 string, and moves
 * the position past it.
 * @details For a loop over the code points:
 *
 *     size_t position = 0;
 *     uint32_t code;
 *     while (sof_utf16_next(data, count, &position, &code)) { ... }
 * @param data The string, or NULL for none.
 * @param count Its length in units.
 * @param position The position, in units, to decode at.
 * @param code Where the code point goes, as sof_utf16_decode() gives it.
 * May be NULL.
 * @return 1 if a code point or an error was read, or 0, leaving position
 * and code alone, at the end of the string, or if data or position is
 * NULL.
 */
static inline int sof_utf16_next(const uint16_t *data, size_t count, size_t *position, uint32_t *code);

/**
 * @brief Gives the size of a code point in UTF-16.
 * @param code The code point.
 * @return 1 or 2 units, or 0 if code isn't valid.
 */
static inline size_t sof_utf16_size(uint32_t code);

/**
 * @brief Converts a UTF-16 string to UTF-32.
 * @details Each error is written as U+FFFD. Output stops when buffer is
 * full, and the return value is the length the whole string needs, so a
 * return value above size means the output was cut short. With a NULL
 * buffer, nothing is written and the length is still returned. No NUL is
 * added.
 * @param data The string, or NULL for none.
 * @param count Its length in units.
 * @param buffer Where the code points go, or NULL.
 * @param size The units buffer has room for.
 * @return The length of the whole string in UTF-32, in units, which is its
 * number of code points.
 */
static inline size_t sof_utf16_to_utf32(const uint16_t *data, size_t count, uint32_t *buffer, size_t size);

/**
 * @brief Converts a UTF-16 string to UTF-8.
 * @details Each error is written as U+FFFD. Output stops at the last code
 * point that fits whole, and the return value is the size the whole string
 * needs, so a return value above size means the output was cut short. With
 * a NULL buffer, nothing is written and the size is still returned. No NUL
 * is added.
 * @param data The string, or NULL for none.
 * @param count Its length in units.
 * @param buffer Where the bytes go, or NULL.
 * @param size The bytes buffer has room for.
 * @return The size of the whole string in UTF-8, in bytes.
 */
static inline size_t sof_utf16_to_utf8(const uint16_t *data, size_t count, void *buffer, size_t size);

/**
 * @brief Finds the first error in a UTF-16 string.
 * @param data The string, or NULL for none.
 * @param count Its length in units.
 * @return The position, in units, of the first unpaired surrogate, or count
 * if there is none, so the string is valid if the result is count. 0 if
 * data is NULL.
 */
static inline size_t sof_utf16_validate(const uint16_t *data, size_t count);

/**
 * @brief Converts a UTF-32 string to UTF-16.
 * @details Each unit that isn't a code point is written as U+FFFD. Output
 * stops at the last code point that fits whole, and the return value is
 * the length the whole string needs, so a return value above size means
 * the output was cut short. With a NULL buffer, nothing is written and the
 * length is still returned. No NUL is added.
 * @param data The string, or NULL for none.
 * @param count Its length in units.
 * @param buffer Where the UTF-16 units go, or NULL.
 * @param size The units buffer has room for.
 * @return The length of the whole string in UTF-16, in units.
 */
static inline size_t sof_utf32_to_utf16(const uint32_t *data, size_t count, uint16_t *buffer, size_t size);

/**
 * @brief Converts a UTF-32 string to UTF-8.
 * @details Each unit that isn't a code point is written as U+FFFD. Output
 * stops at the last code point that fits whole, and the return value is
 * the size the whole string needs, so a return value above size means the
 * output was cut short. With a NULL buffer, nothing is written and the
 * size is still returned. No NUL is added.
 * @param data The string, or NULL for none.
 * @param count Its length in units.
 * @param buffer Where the bytes go, or NULL.
 * @param size The bytes buffer has room for.
 * @return The size of the whole string in UTF-8, in bytes.
 */
static inline size_t sof_utf32_to_utf8(const uint32_t *data, size_t count, void *buffer, size_t size);

/**
 * @brief Finds the first error in a UTF-32 string.
 * @param data The string, or NULL for none.
 * @param count Its length in units.
 * @return The position of the first unit that isn't a code point (a
 * surrogate or a value past U+10FFFF), or count if there is none, so the
 * string is valid if the result is count. 0 if data is NULL.
 */
static inline size_t sof_utf32_validate(const uint32_t *data, size_t count);

/**
 * @brief Gives the length of the ASCII at the start of a UTF-8 string.
 * @details ASCII is the bytes below 0x80, each a code point of its own.
 * The bytes are tested 8 at a time, so this finds the end of a long run
 * quickly; the other UTF-8 functions use it to pass over ASCII.
 * @param data The string, or NULL for none.
 * @param size Its size in bytes.
 * @return The number of bytes before the first that isn't ASCII, or size
 * if they all are; 0 if data is NULL.
 */
static inline size_t sof_utf8_ascii(const void *data, size_t size);

/**
 * @brief Counts the code points in a UTF-8 string.
 * @details Each error counts as one, as it would after conversion, where
 * it becomes U+FFFD.
 * @param data The string, or NULL for none.
 * @param size Its size in bytes.
 * @return The number of code points; 0 if data is NULL.
 */
static inline size_t sof_utf8_count(const void *data, size_t size);

/**
 * @brief Decodes the code point at the start of a UTF-8 string.
 * @param data The string, or NULL for none.
 * @param size Its size in bytes.
 * @param code Where the code point goes: SOF_UTF_INVALID if the bytes
 * aren't valid UTF-8, and SOF_UTF_INCOMPLETE if they are the start of a
 * valid sequence cut off by the end of the data, or there are none. May be
 * NULL.
 * @return The bytes read: 1 to 4, or 0 if data is NULL or size is 0. For
 * SOF_UTF_INVALID, it is the bytes to skip, from 1 to 3: the lead byte and
 * any continuation bytes that were valid after it. For SOF_UTF_INCOMPLETE,
 * it is size.
 */
static inline size_t sof_utf8_decode(const void *data, size_t size, uint32_t *code);

/**
 * @brief Encodes a code point in UTF-8.
 * @param code The code point.
 * @param buffer Where the bytes go.
 * @param size The bytes buffer has room for.
 * @return The bytes written, 1 to 4, or 0, writing nothing, if code isn't
 * valid, buffer is NULL or size is too small.
 */
static inline size_t sof_utf8_encode(uint32_t code, void *buffer, size_t size);

/**
 * @brief Decodes the code point at a position in a UTF-8 string, and moves
 * the position past it.
 * @details For a loop over the code points:
 *
 *     size_t position = 0;
 *     uint32_t code;
 *     while (sof_utf8_next(data, size, &position, &code)) { ... }
 * @param data The string, or NULL for none.
 * @param size Its size in bytes.
 * @param position The position, in bytes, to decode at.
 * @param code Where the code point goes, as sof_utf8_decode() gives it.
 * May be NULL.
 * @return 1 if a code point or an error was read, or 0, leaving position
 * and code alone, at the end of the string, or if data or position is
 * NULL.
 */
static inline int sof_utf8_next(const void *data, size_t size, size_t *position, uint32_t *code);

/**
 * @brief Gives the size of a code point in UTF-8.
 * @param code The code point.
 * @return 1 to 4 bytes, or 0 if code isn't valid.
 */
static inline size_t sof_utf8_size(uint32_t code);

/**
 * @brief Converts a UTF-8 string to UTF-16.
 * @details Each error is written as U+FFFD. Output stops at the last code
 * point that fits whole, and the return value is the length the whole
 * string needs, so a return value above count means the output was cut
 * short. With a NULL buffer, nothing is written and the length is still
 * returned. No NUL is added.
 * @param data The string, or NULL for none.
 * @param size Its size in bytes.
 * @param buffer Where the units go, or NULL.
 * @param count The units buffer has room for.
 * @return The length of the whole string in UTF-16, in units.
 */
static inline size_t sof_utf8_to_utf16(const void *data, size_t size, uint16_t *buffer, size_t count);

/**
 * @brief Converts a UTF-8 string to UTF-32.
 * @details Each error is written as U+FFFD. Output stops when buffer is
 * full, and the return value is the length the whole string needs, so a
 * return value above count means the output was cut short. With a NULL
 * buffer, nothing is written and the length is still returned. No NUL is
 * added.
 * @param data The string, or NULL for none.
 * @param size Its size in bytes.
 * @param buffer Where the code points go, or NULL.
 * @param count The units buffer has room for.
 * @return The length of the whole string in UTF-32, in units, which is its
 * number of code points.
 */
static inline size_t sof_utf8_to_utf32(const void *data, size_t size, uint32_t *buffer, size_t count);

/**
 * @brief Finds the first error in a UTF-8 string.
 * @details A sequence cut off at the end of the string is an error, so
 * data read in pieces should be validated once the pieces are joined, or
 * with sof_utf8_decode(), which tells an incomplete sequence from an
 * invalid one.
 * @param data The string, or NULL for none.
 * @param size Its size in bytes.
 * @return The position, in bytes, of the first invalid or incomplete
 * sequence, or size if there is none, so the string is valid if the result
 * is size. 0 if data is NULL.
 */
static inline size_t sof_utf8_validate(const void *data, size_t size);

/**
 * @brief Says whether a number is a code point that can be encoded.
 * @param code The number.
 * @return 1 if code is at most U+10FFFF and not a surrogate (U+D800 to
 * U+DFFF), or 0. SOF_UTF_INVALID and SOF_UTF_INCOMPLETE give 0.
 */
static inline int sof_utf_valid(uint32_t code);

static inline size_t sof_utf16_count(const uint16_t *data, size_t count) {
    size_t n = 0;
    size_t position = 0;
    while (sof_utf16_next(data, count, &position, NULL)) {
        n++;
    }
    return n;
}

static inline size_t sof_utf16_decode(const uint16_t *data, size_t count, uint32_t *code) {
    uint32_t result = SOF_UTF_INCOMPLETE;
    size_t used = 0;
    if (data != NULL && count > 0) {
        uint32_t unit = data[0];
        used = 1;
        if (unit < 0xd800 || unit > 0xdfff) {
            result = unit;
        } else if (unit >= 0xdc00) {
            result = SOF_UTF_INVALID;
        } else if (count < 2) {
            result = SOF_UTF_INCOMPLETE;
        } else if (data[1] < 0xdc00 || data[1] > 0xdfff) {
            result = SOF_UTF_INVALID;
        } else {
            result = 0x10000 + ((unit - 0xd800) << 10) + ((uint32_t)data[1] - 0xdc00);
            used = 2;
        }
    }
    if (code != NULL) {
        *code = result;
    }
    return used;
}

static inline size_t sof_utf16_encode(uint32_t code, uint16_t *buffer, size_t count) {
    size_t n = sof_utf16_size(code);
    if (n == 0 || buffer == NULL || count < n) {
        return 0;
    }
    if (n == 1) {
        buffer[0] = (uint16_t)code;
    } else {
        code -= 0x10000;
        buffer[0] = (uint16_t)(0xd800 | (code >> 10));
        buffer[1] = (uint16_t)(0xdc00 | (code & 0x3ff));
    }
    return n;
}

static inline int sof_utf16_next(const uint16_t *data, size_t count, size_t *position, uint32_t *code) {
    if (data == NULL || position == NULL || *position >= count) {
        return 0;
    }
    *position += sof_utf16_decode(data + *position, count - *position, code);
    return 1;
}

static inline size_t sof_utf16_size(uint32_t code) {
    if (!sof_utf_valid(code)) {
        return 0;
    }
    return code < 0x10000 ? 1 : 2;
}

static inline size_t sof_utf16_to_utf32(const uint16_t *data, size_t count, uint32_t *buffer, size_t size) {
    size_t total = 0;
    size_t position = 0;
    uint32_t code;
    while (sof_utf16_next(data, count, &position, &code)) {
        if (buffer != NULL && total < size) {
            buffer[total] = sof_utf_valid(code) ? code : SOF_UTF_REPLACEMENT;
        }
        total++;
    }
    return total;
}

static inline size_t sof_utf16_to_utf8(const uint16_t *data, size_t count, void *buffer, size_t size) {
    unsigned char *out = buffer;
    // Once a code point doesn't fit, nothing after it is written either.
    int full = out == NULL;
    size_t total = 0;
    size_t position = 0;
    uint32_t code;
    while (sof_utf16_next(data, count, &position, &code)) {
        if (!sof_utf_valid(code)) {
            code = SOF_UTF_REPLACEMENT;
        }
        size_t n = sof_utf8_size(code);
        if (!full && size - total >= n) {
            sof_utf8_encode(code, out + total, n);
        } else {
            full = 1;
        }
        total += n;
    }
    return total;
}

static inline size_t sof_utf16_validate(const uint16_t *data, size_t count) {
    size_t position = 0;
    uint32_t code;
    if (data == NULL) {
        return 0;
    }
    while (position < count) {
        size_t used = sof_utf16_decode(data + position, count - position, &code);
        if (!sof_utf_valid(code)) {
            break;
        }
        position += used;
    }
    return position;
}

static inline size_t sof_utf32_to_utf16(const uint32_t *data, size_t count, uint16_t *buffer, size_t size) {
    // Once a code point doesn't fit, nothing after it is written either.
    int full = buffer == NULL;
    size_t total = 0;
    if (data == NULL) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        uint32_t code = sof_utf_valid(data[i]) ? data[i] : SOF_UTF_REPLACEMENT;
        size_t n = sof_utf16_size(code);
        if (!full && size - total >= n) {
            sof_utf16_encode(code, buffer + total, n);
        } else {
            full = 1;
        }
        total += n;
    }
    return total;
}

static inline size_t sof_utf32_to_utf8(const uint32_t *data, size_t count, void *buffer, size_t size) {
    unsigned char *out = buffer;
    // Once a code point doesn't fit, nothing after it is written either.
    int full = out == NULL;
    size_t total = 0;
    if (data == NULL) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        uint32_t code = sof_utf_valid(data[i]) ? data[i] : SOF_UTF_REPLACEMENT;
        size_t n = sof_utf8_size(code);
        if (!full && size - total >= n) {
            sof_utf8_encode(code, out + total, n);
        } else {
            full = 1;
        }
        total += n;
    }
    return total;
}

static inline size_t sof_utf32_validate(const uint32_t *data, size_t count) {
    size_t position = 0;
    if (data == NULL) {
        return 0;
    }
    while (position < count && sof_utf_valid(data[position])) {
        position++;
    }
    return position;
}

static inline size_t sof_utf8_ascii(const void *data, size_t size) {
    const unsigned char *s = data;
    size_t i = 0;
    if (s == NULL) {
        return 0;
    }
    // A word with no high bit set is 8 bytes of ASCII. memcpy() loads it
    // whatever the alignment, and compilers make it a single load. Words
    // start before words_end, which lets gcc see that a short constant
    // string has none.
    size_t words_end = size >= 8 ? size - 7 : 0;
    while (i < words_end) {
        uint64_t word;
        memcpy(&word, s + i, sizeof(word));
        if ((word & UINT64_C(0x8080808080808080)) != 0) {
            break;
        }
        i += 8;
    }
    while (i < size && s[i] < 0x80) {
        i++;
    }
    return i;
}

static inline size_t sof_utf8_count(const void *data, size_t size) {
    const unsigned char *s = data;
    size_t n = 0;
    size_t i = 0;
    if (s == NULL) {
        return 0;
    }
    while (i < size) {
        // ASCII is a code point to each byte. A run of two or more is passed
        // over 8 bytes at a time, and a single ASCII byte, such as a space
        // between words in another script, costs no more than before.
        if (s[i] < 0x80) {
            size_t ascii = 1;
            if (size - i > 1 && s[i + 1] < 0x80) {
                ascii = 2 + sof_utf8_ascii(s + i + 2, size - i - 2);
            }
            i += ascii;
            n += ascii;
        } else {
            // Sequences that aren't ASCII, until one is.
            do {
                i += sof_utf8_decode(s + i, size - i, NULL);
                n++;
            } while (i < size && s[i] >= 0x80);
        }
    }
    return n;
}

static inline size_t sof_utf8_decode(const void *data, size_t size, uint32_t *code) {
    const unsigned char *s = data;
    uint32_t result = SOF_UTF_INCOMPLETE;
    size_t used = 0;
    if (s != NULL && size > 0) {
        unsigned char lead = s[0];
        // Whole, valid sequences are decoded at once, with the checks of the
        // general path below, which takes errors and sequences cut short.
        if (lead < 0x80) {
            if (code != NULL) {
                *code = lead;
            }
            return 1;
        }
        if (lead >= 0xc2 && lead <= 0xdf && size >= 2 && (s[1] & 0xc0) == 0x80) {
            if (code != NULL) {
                *code = (uint32_t)(lead & 0x1f) << 6 | (s[1] & 0x3f);
            }
            return 2;
        }
        if (lead >= 0xe0 && lead <= 0xef && size >= 3 && s[1] >= (lead == 0xe0 ? 0xa0 : 0x80) && s[1] <= (lead == 0xed ? 0x9f : 0xbf) && (s[2] & 0xc0) == 0x80) {
            if (code != NULL) {
                *code = (uint32_t)(lead & 0x0f) << 12 | (uint32_t)(s[1] & 0x3f) << 6 | (s[2] & 0x3f);
            }
            return 3;
        }
        if (lead >= 0xf0 && lead <= 0xf4 && size >= 4 && s[1] >= (lead == 0xf0 ? 0x90 : 0x80) && s[1] <= (lead == 0xf4 ? 0x8f : 0xbf) && (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80) {
            if (code != NULL) {
                *code = (uint32_t)(lead & 0x07) << 18 | (uint32_t)(s[1] & 0x3f) << 12 | (uint32_t)(s[2] & 0x3f) << 6 | (s[3] & 0x3f);
            }
            return 4;
        }
        size_t need = 0;
        // The range of the byte after the lead, which rules out overlong
        // forms, surrogates and values past U+10FFFF; later continuation
        // bytes are always 0x80 to 0xbf.
        unsigned char low = 0x80;
        unsigned char high = 0xbf;
        if (lead < 0x80) {
            need = 1;
            result = lead;
        } else if (lead >= 0xc2 && lead <= 0xdf) {
            need = 2;
            result = lead & 0x1f;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            need = 3;
            result = lead & 0x0f;
            if (lead == 0xe0) {
                low = 0xa0;
            } else if (lead == 0xed) {
                high = 0x9f;
            }
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            need = 4;
            result = lead & 0x07;
            if (lead == 0xf0) {
                low = 0x90;
            } else if (lead == 0xf4) {
                high = 0x8f;
            }
        }
        used = 1;
        if (need == 0) {
            result = SOF_UTF_INVALID;
        }
        while (used < need) {
            if (used == size) {
                result = SOF_UTF_INCOMPLETE;
                break;
            }
            unsigned char next = s[used];
            if (next < low || next > high) {
                result = SOF_UTF_INVALID;
                break;
            }
            result = (result << 6) | (next & 0x3f);
            low = 0x80;
            high = 0xbf;
            used++;
        }
    }
    if (code != NULL) {
        *code = result;
    }
    return used;
}

static inline size_t sof_utf8_encode(uint32_t code, void *buffer, size_t size) {
    unsigned char *out = buffer;
    size_t n = sof_utf8_size(code);
    if (n == 0 || out == NULL || size < n) {
        return 0;
    }
    switch (n) {
    case 1:
        out[0] = (unsigned char)code;
        break;
    case 2:
        out[0] = (unsigned char)(0xc0 | (code >> 6));
        out[1] = (unsigned char)(0x80 | (code & 0x3f));
        break;
    case 3:
        out[0] = (unsigned char)(0xe0 | (code >> 12));
        out[1] = (unsigned char)(0x80 | ((code >> 6) & 0x3f));
        out[2] = (unsigned char)(0x80 | (code & 0x3f));
        break;
    default:
        out[0] = (unsigned char)(0xf0 | (code >> 18));
        out[1] = (unsigned char)(0x80 | ((code >> 12) & 0x3f));
        out[2] = (unsigned char)(0x80 | ((code >> 6) & 0x3f));
        out[3] = (unsigned char)(0x80 | (code & 0x3f));
        break;
    }
    return n;
}

static inline int sof_utf8_next(const void *data, size_t size, size_t *position, uint32_t *code) {
    if (data == NULL || position == NULL || *position >= size) {
        return 0;
    }
    *position += sof_utf8_decode((const unsigned char *)data + *position, size - *position, code);
    return 1;
}

static inline size_t sof_utf8_size(uint32_t code) {
    if (!sof_utf_valid(code)) {
        return 0;
    }
    if (code < 0x80) {
        return 1;
    }
    if (code < 0x800) {
        return 2;
    }
    return code < 0x10000 ? 3 : 4;
}

static inline size_t sof_utf8_to_utf16(const void *data, size_t size, uint16_t *buffer, size_t count) {
    const unsigned char *s = data;
    // Once a code point doesn't fit, nothing after it is written either.
    int full = buffer == NULL;
    size_t total = 0;
    size_t position = 0;
    uint32_t code;
    if (s == NULL) {
        return 0;
    }
    while (position < size) {
        // ASCII is a unit to each byte, and runs of it are found as
        // sof_utf8_count() finds them.
        if (s[position] < 0x80) {
            size_t ascii = 1;
            if (size - position > 1 && s[position + 1] < 0x80) {
                ascii = 2 + sof_utf8_ascii(s + position + 2, size - position - 2);
            }
            if (!full) {
                size_t fits = count - total < ascii ? count - total : ascii;
                for (size_t i = 0; i < fits; i++) {
                    buffer[total + i] = s[position + i];
                }
                full = fits < ascii;
            }
            position += ascii;
            total += ascii;
            continue;
        }
        // Sequences that aren't ASCII, until one is.
        do {
            position += sof_utf8_decode(s + position, size - position, &code);
            if (!sof_utf_valid(code)) {
                code = SOF_UTF_REPLACEMENT;
            }
            // A code point, so one unit, or a surrogate pair past U+FFFF.
            size_t n = code < 0x10000 ? 1 : 2;
            if (full || count - total < n) {
                full = 1;
            } else if (n == 1) {
                buffer[total] = (uint16_t)code;
            } else {
                buffer[total] = (uint16_t)(0xd800 | (code - 0x10000) >> 10);
                buffer[total + 1] = (uint16_t)(0xdc00 | (code & 0x3ff));
            }
            total += n;
        } while (position < size && s[position] >= 0x80);
    }
    return total;
}

static inline size_t sof_utf8_to_utf32(const void *data, size_t size, uint32_t *buffer, size_t count) {
    const unsigned char *s = data;
    // The units that can still be written.
    size_t room = buffer == NULL ? 0 : count;
    size_t total = 0;
    size_t position = 0;
    uint32_t code;
    if (s == NULL) {
        return 0;
    }
    while (position < size) {
        // ASCII is a unit to each byte, and runs of it are found as
        // sof_utf8_count() finds them.
        if (s[position] < 0x80) {
            size_t ascii = 1;
            if (size - position > 1 && s[position + 1] < 0x80) {
                ascii = 2 + sof_utf8_ascii(s + position + 2, size - position - 2);
            }
            size_t fits = room < ascii ? room : ascii;
            for (size_t i = 0; i < fits; i++) {
                buffer[total + i] = s[position + i];
            }
            room -= fits;
            position += ascii;
            total += ascii;
            continue;
        }
        // Sequences that aren't ASCII, until one is.
        do {
            position += sof_utf8_decode(s + position, size - position, &code);
            if (room > 0) {
                buffer[total] = sof_utf_valid(code) ? code : SOF_UTF_REPLACEMENT;
                room--;
            }
            total++;
        } while (position < size && s[position] >= 0x80);
    }
    return total;
}

static inline size_t sof_utf8_validate(const void *data, size_t size) {
    const unsigned char *s = data;
    size_t position = 0;
    if (s == NULL) {
        return 0;
    }
    // The checks of sof_utf8_decode(), made in place: each sequence must be
    // whole, and the range of the byte after the lead rules out overlong
    // forms, surrogates and values past U+10FFFF.
    while (position < size) {
        unsigned char lead = s[position];
        size_t left = size - position;
        if (lead < 0x80) {
            // After an ASCII byte, look for more 8 at a time.
            position += 1 + sof_utf8_ascii(s + position + 1, left - 1);
        } else if (lead >= 0xc2 && lead <= 0xdf) {
            if (left < 2 || (s[position + 1] & 0xc0) != 0x80) {
                break;
            }
            position += 2;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            unsigned char low = lead == 0xe0 ? 0xa0 : 0x80;
            unsigned char high = lead == 0xed ? 0x9f : 0xbf;
            if (left < 3 || s[position + 1] < low || s[position + 1] > high || (s[position + 2] & 0xc0) != 0x80) {
                break;
            }
            position += 3;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            unsigned char low = lead == 0xf0 ? 0x90 : 0x80;
            unsigned char high = lead == 0xf4 ? 0x8f : 0xbf;
            if (left < 4 || s[position + 1] < low || s[position + 1] > high || (s[position + 2] & 0xc0) != 0x80 || (s[position + 3] & 0xc0) != 0x80) {
                break;
            }
            position += 4;
        } else {
            break;
        }
    }
    return position;
}

static inline int sof_utf_valid(uint32_t code) {
    return code <= SOF_UTF_MAX && (code < 0xd800 || code > 0xdfff);
}

#endif // SOF_UTF_H
