/**
 * @file sof_hash.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief General-purpose hashing in C.
 * @details The stream functions follow rapidhash_internal() in rapidhash.h
 * step by step, so they give the same hash. For more than 112 bytes it hashes
 * 112-byte blocks, one lane of 16 bytes per secret, while more than 112 bytes
 * remain (its default build takes two blocks at a time, which comes to the
 * same thing); then it mixes the last 1 to 112 bytes, and reads the 16 bytes
 * before the end, which can reach back into the last block. So a stream hashes
 * a block only once it has a byte past it, and keeps the last 16 bytes of the
 * block it hashed in front of the bytes it holds back. Up to 112 bytes in
 * all, it is the short-input path, which sof_hash_stream_final() leaves to
 * rapidhash itself.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <string.h>

#include "sof_hash.h"
#include "rapidhash.h"

static void stream_block(sof_hash_stream_t *stream, const uint8_t *p);

/// @brief Hashes one SOF_HASH_STREAM_BLOCK-byte block at p into the lanes,
/// as each pass of rapidhash_internal()'s loop does.
static void stream_block(sof_hash_stream_t *stream, const uint8_t *p) {
    for (int lane = 0; lane < 7; lane++) {
        const uint8_t *q = p + 16 * lane;
        stream->lanes[lane] = rapid_mix(rapid_read64(q) ^ rapid_secret[lane], rapid_read64(q + 8) ^ stream->lanes[lane]);
    }
}

uint64_t sof_hash_bytes(const void *data, size_t length, uint64_t seed) {
    if (data == NULL) {
        data = "";
        length = 0;
    }
    return rapidhash_withSeed(data, length, seed);
}

uint64_t sof_hash_mix(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

uint64_t sof_hash_stream_final(const sof_hash_stream_t *stream) {
    if (stream == NULL) {
        return sof_hash_bytes(NULL, 0, 0);
    }
    const uint8_t *p = stream->buffer + 16;
    size_t i = stream->buffered;
    if (!stream->blocks) {
        return rapidhash_withSeed(p, i, stream->seed);
    }
    // The lanes are folded together, then the 1 to 112 bytes held back are
    // mixed in 16 at a time, with the secrets rapidhash_internal() uses.
    const uint64_t *lanes = stream->lanes;
    uint64_t seed = lanes[0] ^ lanes[1] ^ lanes[2] ^ lanes[3] ^ lanes[4] ^ lanes[5] ^ lanes[6];
    static const int secrets[6] = {2, 2, 1, 1, 2, 1};
    for (size_t step = 0; step < 6 && i > 16 * (step + 1); step++) {
        const uint8_t *q = p + 16 * step;
        seed = rapid_mix(rapid_read64(q) ^ rapid_secret[secrets[step]], rapid_read64(q + 8) ^ seed);
    }
    // These can read back into the end of the last block, before p.
    uint64_t a = rapid_read64(p + i - 16) ^ i;
    uint64_t b = rapid_read64(p + i - 8);
    a ^= rapid_secret[1];
    b ^= seed;
    rapid_mum(&a, &b);
    return rapid_mix(a ^ rapid_secret[7], b ^ rapid_secret[1] ^ i);
}

void sof_hash_stream_init(sof_hash_stream_t *stream, uint64_t seed) {
    if (stream == NULL) {
        return;
    }
    memset(stream, 0, sizeof(sof_hash_stream_t));
    stream->seed = seed;
    uint64_t mixed = seed ^ rapid_mix(seed ^ rapid_secret[2], rapid_secret[1]);
    for (int lane = 0; lane < 7; lane++) {
        stream->lanes[lane] = mixed;
    }
}

void sof_hash_stream_update(sof_hash_stream_t *stream, const void *data, size_t length) {
    if (stream == NULL || data == NULL) {
        return;
    }
    const uint8_t *p = data;
    stream->length += length;
    if (length <= SOF_HASH_STREAM_BLOCK - stream->buffered) {
        // The bytes fit after those held back, as small updates do, so
        // nothing can be hashed yet: the block, if this fills it, waits for
        // a byte after it. Up to 16 bytes are copied here, which is much
        // faster than calling memcpy() for so few.
        uint8_t *to = stream->buffer + 16 + stream->buffered;
        stream->buffered += length;
        if (length < 8) {
            for (size_t i = 0; i < length; i++) {
                to[i] = p[i];
            }
        } else if (length <= 16) {
            // Two 8-byte copies, which overlap below 16, cover 8 to 16 bytes.
            memcpy(to, p, 8);
            memcpy(to + length - 8, p + length - 8, 8);
        } else {
            memcpy(to, p, length);
        }
        return;
    }
    while (length > 0) {
        if (stream->buffered == SOF_HASH_STREAM_BLOCK) {
            // A whole block is held back and more follows, so it can go.
            stream_block(stream, stream->buffer + 16);
            memcpy(stream->buffer, stream->buffer + SOF_HASH_STREAM_BLOCK, 16);
            stream->buffered = 0;
            stream->blocks = 1;
        }
        if (stream->buffered == 0 && length > SOF_HASH_STREAM_BLOCK) {
            // Blocks with more after them are hashed where they are.
            do {
                stream_block(stream, p);
                p += SOF_HASH_STREAM_BLOCK;
                length -= SOF_HASH_STREAM_BLOCK;
            } while (length > SOF_HASH_STREAM_BLOCK);
            memcpy(stream->buffer, p - 16, 16);
            stream->blocks = 1;
        }
        size_t n = SOF_HASH_STREAM_BLOCK - stream->buffered;
        if (n > length) {
            n = length;
        }
        memcpy(stream->buffer + 16 + stream->buffered, p, n);
        stream->buffered += n;
        p += n;
        length -= n;
    }
}

uint64_t sof_hash_string(const char *s, uint64_t seed) {
    if (s == NULL) {
        s = "";
    }
    return rapidhash_withSeed(s, strlen(s), seed);
}
