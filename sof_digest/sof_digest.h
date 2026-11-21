/**
 * @file sof_digest.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Message digests and checksums in C: SHA-2, SHA-1, MD5, CRC-32 and
 * CRC-32C.
 * @details sof_digest_sha256() gives the SHA-256 digest of memory, as FIPS
 * 180-4 defines it: 32 bytes that change unpredictably with any change to
 * the data, and for which no one knows how to find two inputs with the same
 * digest. sof_digest_sha512() gives SHA-512's 64 bytes, and
 * sof_digest_sha224(), sof_digest_sha384() and sof_digest_sha512_256() the
 * shorter digests FIPS 180-4 defines from the same two functions. A
 * sof_digest_sha256_t, or the type of each of the others, digests data that
 * comes in pieces, such as a file read a buffer at a time, giving the same
 * digest as the one-shot function of all of it at once.
 *
 * sof_digest_sha1() and sof_digest_md5() give SHA-1's 20 bytes and MD5's
 * 16, in the same way, for formats and protocols that use them: git's
 * object names, ETags, old file lists and certificates. Both are broken:
 * anyone can make two inputs with the same MD5 digest in seconds, and two
 * with the same SHA-1 digest has been done (SHAttered, 2017). Don't use them
 * where someone might want to forge data; use SHA-256.
 *
 * sof_digest_crc32() and sof_digest_crc32c() give 32-bit cyclic redundancy
 * checks, which catch accidental damage, such as a flipped bit or a burst
 * of errors, very quickly, but are no protection against changes made on
 * purpose. CRC-32 is the one zip, gzip and PNG use; CRC-32C, Castagnoli's,
 * the one iSCSI, ext4 and SSE4.2's crc32 instruction use. They can also be
 * continued from an earlier CRC, and combined.
 *
 * sof_digest_to_hex() and sof_digest_from_hex() convert a digest to and
 * from the hex digits it is usually written in.
 *
 * Use sof_hash instead for hash tables and other uses that don't need any of
 * these: it is many times faster.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_DIGEST_H
#define SOF_DIGEST_H

#include <stddef.h>
#include <stdint.h>

/// @brief The bytes in an MD5 digest.
#define SOF_DIGEST_MD5_SIZE 16

/// @brief The bytes in a SHA-1 digest.
#define SOF_DIGEST_SHA1_SIZE 20

/// @brief The bytes in a SHA-224 digest.
#define SOF_DIGEST_SHA224_SIZE 28

/// @brief The bytes in a SHA-256 digest.
#define SOF_DIGEST_SHA256_SIZE 32

/// @brief The bytes in a SHA-384 digest.
#define SOF_DIGEST_SHA384_SIZE 48

/// @brief The bytes in a SHA-512 digest.
#define SOF_DIGEST_SHA512_SIZE 64

/// @brief The bytes in a SHA-512/256 digest.
#define SOF_DIGEST_SHA512_256_SIZE 32

/// @brief The bytes in the largest digest, SHA-512's.
#define SOF_DIGEST_MAX_SIZE 64

/// @brief The bytes MD5 takes in each step, which a sof_digest_md5_t holds
/// until it has a whole block.
#define SOF_DIGEST_MD5_BLOCK 64

/// @brief The bytes SHA-1 takes in each step.
#define SOF_DIGEST_SHA1_BLOCK 64

/// @brief The bytes SHA-256, and SHA-224, take in each step.
#define SOF_DIGEST_SHA256_BLOCK 64

/// @brief The bytes SHA-512, and SHA-384 and SHA-512/256, take in each step.
#define SOF_DIGEST_SHA512_BLOCK 128

/**
 * @brief The state of a CRC-32 of data that comes in pieces.
 * @details The application provides the sof_digest_crc32_t (on the stack or
 * anywhere else) and sets it up with sof_digest_crc32_init(), gives it the
 * data with sof_digest_crc32_update(), as many times as there are pieces,
 * and gets the CRC with sof_digest_crc32_final(). It allocates nothing, so
 * there is nothing to free. The fields can be read but must not be changed:
 * crc is the CRC-32 of the data so far, and length the bytes given so far.
 * It can be copied, which forks the CRC.
 */
typedef struct sof_digest_crc32_t {
    /// @brief The CRC of the data so far.
    uint32_t crc;
    /// @brief The bytes given so far.
    uint64_t length;
} sof_digest_crc32_t;

/**
 * @brief The state of a CRC-32C of data that comes in pieces.
 * @details As sof_digest_crc32_t, for CRC-32C, with the sof_digest_crc32c_
 * functions.
 */
typedef struct sof_digest_crc32c_t {
    /// @brief The CRC of the data so far.
    uint32_t crc;
    /// @brief The bytes given so far.
    uint64_t length;
} sof_digest_crc32c_t;

/**
 * @brief The state of an MD5 digest of data that comes in pieces.
 * @details As sof_digest_sha256_t, for MD5, with the sof_digest_md5_
 * functions.
 */
typedef struct sof_digest_md5_t {
    /// @brief The four 32-bit words of the hash so far.
    uint32_t state[4];
    /// @brief The bytes given so far.
    uint64_t length;
    /// @brief The bytes in buffer, not yet hashed.
    size_t buffered;
    /// @brief The start of a block, waiting for the rest of it.
    unsigned char buffer[SOF_DIGEST_MD5_BLOCK];
} sof_digest_md5_t;

/**
 * @brief The state of a SHA-1 digest of data that comes in pieces.
 * @details As sof_digest_sha256_t, for SHA-1, with the sof_digest_sha1_
 * functions.
 */
typedef struct sof_digest_sha1_t {
    /// @brief The five 32-bit words of the hash so far.
    uint32_t state[5];
    /// @brief The bytes given so far.
    uint64_t length;
    /// @brief The bytes in buffer, not yet hashed.
    size_t buffered;
    /// @brief The start of a block, waiting for the rest of it.
    unsigned char buffer[SOF_DIGEST_SHA1_BLOCK];
} sof_digest_sha1_t;

/**
 * @brief The state of a SHA-256 digest of data that comes in pieces.
 * @details The application provides the sof_digest_sha256_t (on the stack
 * or anywhere else) and sets it up with sof_digest_sha256_init(), gives it
 * the data with sof_digest_sha256_update(), as many times as there are
 * pieces, and gets the digest with sof_digest_sha256_final(). It allocates
 * nothing, so there is nothing to free. The digest is the one
 * sof_digest_sha256() gives for all the data at once, however it is split.
 * The fields can be read but must not be changed; length is the number of
 * bytes given so far. It holds no pointers, so it can be copied, which forks
 * the digest: each copy goes on from the same data.
 */
typedef struct sof_digest_sha256_t {
    /// @brief The eight 32-bit words of the hash so far.
    uint32_t state[8];
    /// @brief The bytes given so far.
    uint64_t length;
    /// @brief The bytes in buffer, not yet hashed.
    size_t buffered;
    /// @brief The start of a block, waiting for the rest of it.
    unsigned char buffer[SOF_DIGEST_SHA256_BLOCK];
} sof_digest_sha256_t;

/**
 * @brief The state of a SHA-224 digest: SHA-256's, set up by
 * sof_digest_sha224_init() with SHA-224's initial value, and given data with
 * sof_digest_sha224_update().
 */
typedef sof_digest_sha256_t sof_digest_sha224_t;

/**
 * @brief The state of a SHA-512 digest of data that comes in pieces.
 * @details As sof_digest_sha256_t, for SHA-512, with the sof_digest_sha512_
 * functions.
 */
typedef struct sof_digest_sha512_t {
    /// @brief The eight 64-bit words of the hash so far.
    uint64_t state[8];
    /// @brief The bytes given so far.
    uint64_t length;
    /// @brief The bytes in buffer, not yet hashed.
    size_t buffered;
    /// @brief The start of a block, waiting for the rest of it.
    unsigned char buffer[SOF_DIGEST_SHA512_BLOCK];
} sof_digest_sha512_t;

/**
 * @brief The state of a SHA-384 digest: SHA-512's, set up by
 * sof_digest_sha384_init() with SHA-384's initial value.
 */
typedef sof_digest_sha512_t sof_digest_sha384_t;

/**
 * @brief The state of a SHA-512/256 digest: SHA-512's, set up by
 * sof_digest_sha512_256_init() with SHA-512/256's initial value.
 */
typedef sof_digest_sha512_t sof_digest_sha512_256_t;

/**
 * @brief Gives the CRC-32 of length bytes of memory.
 * @details CRC-32 as zip, gzip, PNG and Ethernet compute it: the polynomial
 * 0x04C11DB7, reflected, with an initial value and final xor of 0xFFFFFFFF.
 * The CRC of "123456789" is 0xCBF43926, and of no bytes, 0.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @return The CRC.
 */
extern uint32_t sof_digest_crc32(const void *data, size_t length);

/**
 * @brief Gives the CRC-32 of two pieces of data, one after the other, from
 * the CRC of each and the length of the second.
 * @details The pieces needn't be at hand, so CRCs computed apart, such as
 * of the parts of a file on separate threads, can be joined. It takes about
 * as long as one multiplication modulo the polynomial for each bit set in
 * length2.
 * @param crc1 The CRC-32 of the first piece.
 * @param crc2 The CRC-32 of the second.
 * @param length2 The bytes in the second.
 * @return The CRC-32 of the first piece followed by the second.
 */
extern uint32_t sof_digest_crc32_combine(uint32_t crc1, uint32_t crc2, uint64_t length2);

/**
 * @brief Continues a CRC-32 with more data.
 * @details sof_digest_crc32_extend(sof_digest_crc32(a, n), b, m) is the
 * CRC-32 of the n bytes at a followed by the m at b, and
 * sof_digest_crc32_extend(0, data, length) is sof_digest_crc32(data,
 * length), as zlib's crc32() works, so a stream can be checked a piece at a
 * time with nothing but the CRC so far.
 * @param crc The CRC-32 of the data before, 0 for none.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @return The CRC-32 of the data before followed by these bytes.
 */
extern uint32_t sof_digest_crc32_extend(uint32_t crc, const void *data, size_t length);

/**
 * @brief Gives the CRC-32 of the data given to a stream so far.
 * @param crc The stream. NULL gives the CRC of no data, 0.
 * @return The CRC-32.
 */
extern uint32_t sof_digest_crc32_final(const sof_digest_crc32_t *crc);

/**
 * @brief Starts a CRC-32 of data that comes in pieces.
 * @param crc The stream to set up. NULL does nothing.
 */
extern void sof_digest_crc32_init(sof_digest_crc32_t *crc);

/**
 * @brief Gives a CRC-32 stream the next piece of its data.
 * @param crc The stream. NULL does nothing.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 */
extern void sof_digest_crc32_update(sof_digest_crc32_t *crc, const void *data, size_t length);

/**
 * @brief Gives the CRC-32C of length bytes of memory.
 * @details CRC-32C, Castagnoli's, as iSCSI, ext4 and SSE4.2's crc32
 * instruction compute it: the polynomial 0x1EDC6F41, reflected, with an
 * initial value and final xor of 0xFFFFFFFF. The CRC of "123456789" is
 * 0xE3069283, and of no bytes, 0.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @return The CRC.
 */
extern uint32_t sof_digest_crc32c(const void *data, size_t length);

/**
 * @brief Gives the CRC-32C of two pieces of data, one after the other, from
 * the CRC of each and the length of the second.
 * @param crc1 The CRC-32C of the first piece.
 * @param crc2 The CRC-32C of the second.
 * @param length2 The bytes in the second.
 * @return The CRC-32C of the first piece followed by the second.
 */
extern uint32_t sof_digest_crc32c_combine(uint32_t crc1, uint32_t crc2, uint64_t length2);

/**
 * @brief Continues a CRC-32C with more data, as sof_digest_crc32_extend()
 * does a CRC-32.
 * @param crc The CRC-32C of the data before, 0 for none.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @return The CRC-32C of the data before followed by these bytes.
 */
extern uint32_t sof_digest_crc32c_extend(uint32_t crc, const void *data, size_t length);

/**
 * @brief Gives the CRC-32C of the data given to a stream so far.
 * @param crc The stream. NULL gives the CRC of no data, 0.
 * @return The CRC-32C.
 */
extern uint32_t sof_digest_crc32c_final(const sof_digest_crc32c_t *crc);

/**
 * @brief Starts a CRC-32C of data that comes in pieces.
 * @param crc The stream to set up. NULL does nothing.
 */
extern void sof_digest_crc32c_init(sof_digest_crc32c_t *crc);

/**
 * @brief Gives a CRC-32C stream the next piece of its data.
 * @param crc The stream. NULL does nothing.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 */
extern void sof_digest_crc32c_update(sof_digest_crc32c_t *crc, const void *data, size_t length);

/**
 * @brief Reads a digest, or any bytes, from hex digits.
 * @details Each two digits, in either case, make one byte, the first the
 * high four bits. Nothing else, not even a space, may come between them.
 * @param hex The digits. Need not end with a NUL.
 * @param length The number of digits, which must be even.
 * @param bytes Where the length / 2 bytes go. Nothing is written unless
 * every digit is good.
 * @return 0, or -1 if length is odd, a character isn't a hex digit, or hex
 * or bytes is NULL and length isn't 0.
 */
extern int sof_digest_from_hex(const char *hex, size_t length, void *bytes);

/**
 * @brief Gives the MD5 digest of length bytes of memory.
 * @details MD5 as RFC 1321 defines it. It is broken: collisions, two inputs
 * with the same digest, take seconds to make. Use it only where a format or
 * protocol calls for it, as a checksum or an ETag, and SHA-256 wherever
 * someone might want to forge data.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @param digest Where the SOF_DIGEST_MD5_SIZE bytes of the digest go. NULL
 * does nothing.
 */
extern void sof_digest_md5(const void *data, size_t length, unsigned char *digest);

/**
 * @brief Gives the MD5 digest of the data given to a stream so far, leaving
 * the stream unchanged.
 * @param md5 The stream. NULL gives the digest of no data.
 * @param digest Where the SOF_DIGEST_MD5_SIZE bytes of the digest go. NULL
 * does nothing.
 */
extern void sof_digest_md5_final(const sof_digest_md5_t *md5, unsigned char *digest);

/**
 * @brief Starts an MD5 digest of data that comes in pieces.
 * @param md5 The stream to set up. NULL does nothing.
 */
extern void sof_digest_md5_init(sof_digest_md5_t *md5);

/**
 * @brief Gives an MD5 stream the next piece of its data.
 * @param md5 The stream. NULL does nothing.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 */
extern void sof_digest_md5_update(sof_digest_md5_t *md5, const void *data, size_t length);

/**
 * @brief Gives the SHA-1 digest of length bytes of memory.
 * @details SHA-1 as FIPS 180-4 defines it. It is broken for collisions: two
 * PDFs with the same digest were published in 2017 (SHAttered), and such
 * pairs now cost little to make. Use it only where a format or protocol
 * calls for it, such as git's object names, and SHA-256 wherever someone
 * might want to forge data.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @param digest Where the SOF_DIGEST_SHA1_SIZE bytes of the digest go. NULL
 * does nothing.
 */
extern void sof_digest_sha1(const void *data, size_t length, unsigned char *digest);

/**
 * @brief Gives the SHA-1 digest of the data given to a stream so far,
 * leaving the stream unchanged.
 * @param sha The stream. NULL gives the digest of no data.
 * @param digest Where the SOF_DIGEST_SHA1_SIZE bytes of the digest go. NULL
 * does nothing.
 */
extern void sof_digest_sha1_final(const sof_digest_sha1_t *sha, unsigned char *digest);

/**
 * @brief Starts a SHA-1 digest of data that comes in pieces.
 * @param sha The stream to set up. NULL does nothing.
 */
extern void sof_digest_sha1_init(sof_digest_sha1_t *sha);

/**
 * @brief Gives a SHA-1 stream the next piece of its data.
 * @param sha The stream. NULL does nothing.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 */
extern void sof_digest_sha1_update(sof_digest_sha1_t *sha, const void *data, size_t length);

/**
 * @brief Gives the SHA-224 digest of length bytes of memory: SHA-256 from
 * another initial value, cut to 28 bytes.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @param digest Where the SOF_DIGEST_SHA224_SIZE bytes of the digest go.
 * NULL does nothing.
 */
extern void sof_digest_sha224(const void *data, size_t length, unsigned char *digest);

/**
 * @brief Gives the SHA-224 digest of the data given to a stream so far,
 * leaving the stream unchanged.
 * @param sha The stream, set up with sof_digest_sha224_init(). NULL gives
 * the digest of no data.
 * @param digest Where the SOF_DIGEST_SHA224_SIZE bytes of the digest go.
 * NULL does nothing.
 */
extern void sof_digest_sha224_final(const sof_digest_sha224_t *sha, unsigned char *digest);

/**
 * @brief Starts a SHA-224 digest of data that comes in pieces.
 * @param sha The stream to set up. NULL does nothing.
 */
extern void sof_digest_sha224_init(sof_digest_sha224_t *sha);

/**
 * @brief Gives a SHA-224 stream the next piece of its data; the same as
 * sof_digest_sha256_update().
 * @param sha The stream. NULL does nothing.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 */
extern void sof_digest_sha224_update(sof_digest_sha224_t *sha, const void *data, size_t length);

/**
 * @brief Gives the SHA-256 digest of length bytes of memory.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @param digest Where the SOF_DIGEST_SHA256_SIZE bytes of the digest go.
 * NULL does nothing.
 */
extern void sof_digest_sha256(const void *data, size_t length, unsigned char *digest);

/**
 * @brief Gives the SHA-256 digest of the data given to a stream so far.
 * @details The same as sof_digest_sha256() of all the data at once. The
 * stream isn't changed, so more data can be given after this, for the
 * digest of a longer input.
 * @param sha The stream. NULL gives the digest of no data.
 * @param digest Where the SOF_DIGEST_SHA256_SIZE bytes of the digest go.
 * NULL does nothing.
 */
extern void sof_digest_sha256_final(const sof_digest_sha256_t *sha, unsigned char *digest);

/**
 * @brief Starts a SHA-256 digest of data that comes in pieces.
 * @param sha The stream to set up. NULL does nothing.
 */
extern void sof_digest_sha256_init(sof_digest_sha256_t *sha);

/**
 * @brief Gives a stream the next piece of its data.
 * @details Pieces can be of any length, 0 included; the digest doesn't
 * depend on how the data is split. Fewer than SOF_DIGEST_SHA256_BLOCK
 * bytes are copied into the stream; whole blocks are hashed where they are.
 * @param sha The stream. NULL does nothing.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 */
extern void sof_digest_sha256_update(sof_digest_sha256_t *sha, const void *data, size_t length);

/**
 * @brief Gives the SHA-384 digest of length bytes of memory: SHA-512 from
 * another initial value, cut to 48 bytes.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @param digest Where the SOF_DIGEST_SHA384_SIZE bytes of the digest go.
 * NULL does nothing.
 */
extern void sof_digest_sha384(const void *data, size_t length, unsigned char *digest);

/**
 * @brief Gives the SHA-384 digest of the data given to a stream so far,
 * leaving the stream unchanged.
 * @param sha The stream, set up with sof_digest_sha384_init(). NULL gives
 * the digest of no data.
 * @param digest Where the SOF_DIGEST_SHA384_SIZE bytes of the digest go.
 * NULL does nothing.
 */
extern void sof_digest_sha384_final(const sof_digest_sha384_t *sha, unsigned char *digest);

/**
 * @brief Starts a SHA-384 digest of data that comes in pieces.
 * @param sha The stream to set up. NULL does nothing.
 */
extern void sof_digest_sha384_init(sof_digest_sha384_t *sha);

/**
 * @brief Gives a SHA-384 stream the next piece of its data; the same as
 * sof_digest_sha512_update().
 * @param sha The stream. NULL does nothing.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 */
extern void sof_digest_sha384_update(sof_digest_sha384_t *sha, const void *data, size_t length);

/**
 * @brief Gives the SHA-512 digest of length bytes of memory.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @param digest Where the SOF_DIGEST_SHA512_SIZE bytes of the digest go.
 * NULL does nothing.
 */
extern void sof_digest_sha512(const void *data, size_t length, unsigned char *digest);

/**
 * @brief Gives the SHA-512 digest of the data given to a stream so far,
 * leaving the stream unchanged.
 * @param sha The stream. NULL gives the digest of no data.
 * @param digest Where the SOF_DIGEST_SHA512_SIZE bytes of the digest go.
 * NULL does nothing.
 */
extern void sof_digest_sha512_final(const sof_digest_sha512_t *sha, unsigned char *digest);

/**
 * @brief Starts a SHA-512 digest of data that comes in pieces.
 * @param sha The stream to set up. NULL does nothing.
 */
extern void sof_digest_sha512_init(sof_digest_sha512_t *sha);

/**
 * @brief Gives a SHA-512 stream the next piece of its data.
 * @details Fewer than SOF_DIGEST_SHA512_BLOCK bytes are copied into the
 * stream; whole blocks are hashed where they are.
 * @param sha The stream. NULL does nothing.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 */
extern void sof_digest_sha512_update(sof_digest_sha512_t *sha, const void *data, size_t length);

/**
 * @brief Gives the SHA-512/256 digest of length bytes of memory: SHA-512
 * from another initial value, cut to 32 bytes.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @param digest Where the SOF_DIGEST_SHA512_256_SIZE bytes of the digest
 * go. NULL does nothing.
 */
extern void sof_digest_sha512_256(const void *data, size_t length, unsigned char *digest);

/**
 * @brief Gives the SHA-512/256 digest of the data given to a stream so far,
 * leaving the stream unchanged.
 * @param sha The stream, set up with sof_digest_sha512_256_init(). NULL
 * gives the digest of no data.
 * @param digest Where the SOF_DIGEST_SHA512_256_SIZE bytes of the digest
 * go. NULL does nothing.
 */
extern void sof_digest_sha512_256_final(const sof_digest_sha512_256_t *sha, unsigned char *digest);

/**
 * @brief Starts a SHA-512/256 digest of data that comes in pieces.
 * @param sha The stream to set up. NULL does nothing.
 */
extern void sof_digest_sha512_256_init(sof_digest_sha512_256_t *sha);

/**
 * @brief Gives a SHA-512/256 stream the next piece of its data; the same as
 * sof_digest_sha512_update().
 * @param sha The stream. NULL does nothing.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 */
extern void sof_digest_sha512_256_update(sof_digest_sha512_256_t *sha, const void *data, size_t length);

/**
 * @brief Writes a digest, or any bytes, as hex digits.
 * @details Each byte becomes two lowercase hex digits, the high four bits
 * first, as sha256sum prints them, and a NUL follows the last.
 * @param bytes The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @param hex Where the 2 * length digits and the NUL go, so it needs room for
 * 2 * length + 1 characters: 2 * SOF_DIGEST_SHA256_SIZE + 1 for a SHA-256
 * digest.
 * @return hex, or NULL if hex is NULL.
 */
extern char *sof_digest_to_hex(const void *bytes, size_t length, char *hex);

#endif // SOF_DIGEST_H
