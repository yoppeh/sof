/**
 * @file sof_hash.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief General-purpose hashing in C.
 * @details sof_hash_bytes() and sof_hash_string() hash memory with rapidhash, a very
 * fast hash with good distribution whose results are the same on every
 * platform. sof_hash_mix() scrambles a 64-bit value, such as an integer key, a
 * pointer or a weak hash, so that every bit of the result depends on every
 * bit of the input. A sof_hash_stream_t hashes data that comes in pieces,
 * such as a file read a buffer at a time, giving the same hash as
 * sof_hash_bytes() of all of it at once.
 *
 * None of these are meant to resist hash-flooding attacks or for
 * cryptographic use.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_HASH_H
#define SOF_HASH_H

#include <stddef.h>
#include <stdint.h>

/// @brief The bytes rapidhash takes in each step of a long input, which a
/// sof_hash_stream_t holds back until it knows more input follows.
#define SOF_HASH_STREAM_BLOCK 112

/**
 * @brief The state of a hash of data that comes in pieces.
 * @details The application provides the sof_hash_stream_t (on the stack or
 * anywhere else) and sets it up with sof_hash_stream_init(), gives it the
 * data with sof_hash_stream_update(), as many times as there are pieces, and
 * gets the hash with sof_hash_stream_final(). It allocates nothing, so there
 * is nothing to free. The hash is the one sof_hash_bytes() gives for all the
 * data at once, with the same seed, however the data is split. The fields
 * can be read but must not be changed; length is the number of bytes given
 * so far.
 */
typedef struct sof_hash_stream_t {
    /// @brief The seed given to sof_hash_stream_init().
    uint64_t seed;
    /// @brief The running state of rapidhash's seven lanes.
    uint64_t lanes[7];
    /// @brief The bytes given so far.
    uint64_t length;
    /// @brief The bytes in buffer, after its first 16, not yet hashed.
    size_t buffered;
    /// @brief 1 once a block has been hashed, after which buffer's first 16
    /// bytes are the end of the last one, which the final step can read.
    int blocks;
    /// @brief The end of the last block hashed, then the bytes not yet
    /// hashed.
    unsigned char buffer[16 + SOF_HASH_STREAM_BLOCK];
} sof_hash_stream_t;

/**
 * @brief Hashes length bytes of memory.
 * @details The result depends only on the bytes, their length and the seed:
 * not on the data's address or alignment, the platform or the compiler. Any
 * seed is fine; different seeds give unrelated hashes of the same data.
 * @param data The bytes to hash. NULL hashes as if length were 0.
 * @param length The number of bytes to hash.
 * @param seed Alters the hash. Pass 0 if there is no reason to do otherwise.
 * @return The 64-bit hash.
 */
extern uint64_t sof_hash_bytes(const void *data, size_t length, uint64_t seed);

/**
 * @brief Hashes a NUL-terminated string.
 * @details The same as sof_hash_bytes(s, strlen(s), seed): the terminating NUL
 * isn't hashed, so a string and its bytes without the NUL hash alike.
 * @param s The string to hash. NULL hashes as the empty string.
 * @param seed Alters the hash. Pass 0 if there is no reason to do otherwise.
 * @return The 64-bit hash.
 */
extern uint64_t sof_hash_string(const char *s, uint64_t seed);

/**
 * @brief Scrambles the bits of a 64-bit value.
 * @details Every bit of the result depends on every bit of x, so values that
 * differ only in their high bits (such as aligned pointers or small integers)
 * give results that differ in their low bits too. It is a bijection: distinct
 * values always give distinct results. Use it to hash integer and pointer
 * keys, or to improve a weak hash; sof_hash_bytes() and sof_hash_string() results
 * don't need it. This is the MurmurHash3 finalizer.
 * @param x The value to scramble.
 * @return The scrambled value.
 */
extern uint64_t sof_hash_mix(uint64_t x);

/**
 * @brief Returns the hash of the data given to a stream so far.
 * @details The same as sof_hash_bytes() of all the data at once, with the
 * stream's seed. The stream isn't changed, so more data can be given after
 * this, for the hash of a longer input.
 * @param stream The stream.
 * @return The 64-bit hash. A NULL stream gives the hash of no data with a
 * seed of 0.
 */
extern uint64_t sof_hash_stream_final(const sof_hash_stream_t *stream);

/**
 * @brief Starts a hash of data that comes in pieces.
 * @param stream The stream to set up. A NULL stream does nothing.
 * @param seed Alters the hash, as sof_hash_bytes()'s seed does.
 */
extern void sof_hash_stream_init(sof_hash_stream_t *stream, uint64_t seed);

/**
 * @brief Gives a stream the next piece of its data.
 * @details Pieces can be of any length, 0 included; the hash doesn't depend
 * on how the data is split. At most SOF_HASH_STREAM_BLOCK bytes are copied
 * into the stream; the rest is hashed where it is.
 * @param stream The stream. A NULL stream does nothing.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 */
extern void sof_hash_stream_update(sof_hash_stream_t *stream, const void *data, size_t length);

#endif // SOF_HASH_H
