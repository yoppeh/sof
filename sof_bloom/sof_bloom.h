/**
 * @file sof_bloom.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A Bloom filter in c.
 * @details A sof_bloom_t answers whether a key has been added, in much less
 * memory than a set of the keys would take, at the price of sometimes
 * answering yes for a key that wasn't. It never answers no for one that was.
 * sof_bloom_init() sizes the filter for an expected number of keys and a
 * chance of a wrong yes (its false positive rate), sof_bloom_add() adds a
 * key and sof_bloom_contains() tests for one. Keys are any bytes; the
 * _string() forms take NUL-terminated strings. The filter keeps no keys, so
 * they can't be listed or removed.
 *
 * Every key is hashed once with sof_hash_bytes(), and the hash chooses the
 * bits the key sets and tests. A filter is one of two kinds, chosen when it
 * is initialized:
 * - Classic (the default): the filter is an array of bits, kept as sof_bit_t
 *   words and changed with sof_bit, and a key sets k of them, anywhere in
 *   the array, derived from the hash. It takes the fewest bits for a rate,
 *   but each of the k bits is a memory access of its own, which is a cache
 *   miss once the filter is larger than the cache.
 * - Blocked (SOF_BLOOM_BLOCKED): a split-block Bloom filter, as in Apache
 *   Parquet and Impala. The filter is an array of 256-bit blocks, each eight
 *   32-bit words, aligned to 64 bytes. The hash chooses one block, and a key
 *   sets one bit in each of its eight words, so a key's bits are all in one
 *   cache line, and adds and lookups touch one line instead of k. It needs
 *   more bits than a classic filter for the same rate: about 10% more at 1%,
 *   35% more at 0.01%.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_BLOOM_H
#define SOF_BLOOM_H

#include <stddef.h>

#include "sof_alloc.h"
#include "sof_bit.h"

/**
 * @brief Error codes for the Bloom filter functions.
 * @details SOF_BLOOM_NO_MATCH is never returned, since nothing is looked up
 * by a function that returns an error code; it is there so the codes match
 * those of the other libraries.
 */
typedef enum sof_bloom_error_t {
    SOF_BLOOM_OK = 0,
    SOF_BLOOM_NO_MEM,
    SOF_BLOOM_NO_MATCH,
    SOF_BLOOM_ERROR,
} sof_bloom_error_t;

/**
 * @brief Flags for sof_bloom_init(), which can be combined with |. 0 gives
 * a classic filter.
 */
typedef enum sof_bloom_flag_t {
    /// @brief A blocked filter: each key's bits are in one 256-bit block,
    /// so adds and lookups touch one cache line. Faster, above all for
    /// filters larger than the cache, but it takes more bits for the same
    /// false positive rate.
    SOF_BLOOM_BLOCKED = 1,
} sof_bloom_flag_t;

/**
 * @brief A Bloom filter.
 * @details The application provides the sof_bloom_t (on the stack, in
 * another structure or on the heap) and sets it up with sof_bloom_init().
 * Its fields can be read, but must not be changed by the application:
 * - bits: the filter's bits, words words of sof_bit_t; NULL once freed or if
 *   sof_bloom_init() failed. In a blocked filter they are 32-bit words,
 *   eight to a block, starting on a 64-byte boundary, and must be read as
 *   uint32_t.
 * - words: the number of sof_bit_t words in bits.
 * - size: the number of bits in the filter, words * SOF_BIT_BITS.
 * - set: how many of those bits are 1.
 * - count: how many keys sof_bloom_add() found new, which is the number of
 *   different keys added less any that were false positives when added.
 * - blocks: the number of 256-bit blocks in a blocked filter, size / 256; 0
 *   in a classic one.
 * - hashes: how many bits each key sets (k); always 8 in a blocked filter.
 * - flags: the sof_bloom_flag_t flags given to sof_bloom_init().
 * - memory: the memory allocated for bits, which a blocked filter's bits
 *   start up to 63 bytes into.
 * - alloc: a copy of the allocator given to sof_bloom_init().
 *
 * Every function accepts a NULL filter, and one that was freed or whose
 * sof_bloom_init() failed, as described for each. Such a filter is empty:
 * it contains nothing, and adds fail until it is initialized again.
 *
 * The filter does no locking. sof_bloom_contains(),
 * sof_bloom_contains_string(), sof_bloom_count() and
 * sof_bloom_false_positive_rate() only read it; every other function
 * writes.
 */
typedef struct sof_bloom_t {
    sof_bit_t *bits;
    size_t words;
    size_t size;
    size_t set;
    size_t count;
    size_t blocks;
    unsigned hashes;
    unsigned flags;
    void *memory;
    sof_alloc_t alloc;
} sof_bloom_t;

/**
 * @brief Adds a key to the filter.
 * @details Sets the key's bits. If they were all set already, the filter
 * would have said it contains the key, so nothing changes and count isn't
 * increased.
 * @param bloom The filter to add to.
 * @param data The key's bytes. NULL adds the empty key, as if length were 0.
 * @param length The number of bytes in the key.
 * @param created If not NULL, set to 1 if the key was new (at least one of
 * its bits was 0), or 0 if the filter already said it contained the key or
 * the add failed.
 * @return SOF_BLOOM_OK, or SOF_BLOOM_ERROR if bloom is NULL, was freed or
 * was never initialized.
 */
extern sof_bloom_error_t sof_bloom_add(sof_bloom_t *bloom, const void *data, size_t length, int *created);

/**
 * @brief Adds a NUL-terminated string to the filter.
 * @details The same as sof_bloom_add(bloom, s, strlen(s), created): the
 * terminating NUL isn't part of the key, so a string and its bytes are the
 * same key.
 * @param bloom The filter to add to.
 * @param s The string. NULL adds the empty string.
 * @param created As for sof_bloom_add().
 * @return As for sof_bloom_add().
 */
extern sof_bloom_error_t sof_bloom_add_string(sof_bloom_t *bloom, const char *s, int *created);

/**
 * @brief Empties the filter, keeping its memory and size.
 * @param bloom The filter to empty.
 * @return SOF_BLOOM_OK, or SOF_BLOOM_ERROR if bloom is NULL.
 */
extern sof_bloom_error_t sof_bloom_clear(sof_bloom_t *bloom);

/**
 * @brief Tells whether a key may have been added.
 * @param bloom The filter to test.
 * @param data The key's bytes. NULL tests for the empty key, as if length
 * were 0.
 * @param length The number of bytes in the key.
 * @return 1 if the key was probably added (all its bits are set), or 0 if it
 * certainly wasn't, or bloom is NULL, was freed or was never initialized.
 */
extern int sof_bloom_contains(const sof_bloom_t *bloom, const void *data, size_t length);

/**
 * @brief Tells whether a NUL-terminated string may have been added.
 * @details The same as sof_bloom_contains(bloom, s, strlen(s)).
 * @param bloom The filter to test.
 * @param s The string. NULL tests for the empty string.
 * @return As for sof_bloom_contains().
 */
extern int sof_bloom_contains_string(const sof_bloom_t *bloom, const char *s);

/**
 * @brief Returns how many keys sof_bloom_add() found new.
 * @details Adding a key that is already there doesn't count, and nor does
 * one the filter wrongly said it contained, so this can be a little below
 * the number of different keys added.
 * @param bloom The filter.
 * @return The count, or 0 if bloom is NULL, was freed or was never
 * initialized.
 */
extern size_t sof_bloom_count(const sof_bloom_t *bloom);

/**
 * @brief Estimates the chance that sof_bloom_contains() says yes for a key
 * that wasn't added.
 * @details In a classic filter, the fraction of bits that are set, raised
 * to the power of the number of hashes. In a blocked filter, where some
 * blocks hold more keys than others, it is the rate of a block holding j
 * keys, (1 - (31/32)^j)^8, averaged over a Poisson distribution of j whose
 * mean L is found from the fraction of bits set, 1 - e^(-L/32). It starts
 * at 0, is close to the rate given to sof_bloom_init() once the expected
 * number of keys have been added, and rises towards 1 as more are.
 * @param bloom The filter.
 * @return The estimate, from 0 to 1, or 0 if bloom is NULL, was freed or was
 * never initialized.
 */
extern double sof_bloom_false_positive_rate(const sof_bloom_t *bloom);

/**
 * @brief Frees the filter's memory.
 * @details The sof_bloom_t itself isn't freed (it belongs to the caller); it
 * is left empty, and acts like a filter whose sof_bloom_init() failed until
 * it is initialized again.
 * @param bloom The filter to free.
 * @return SOF_BLOOM_OK, or SOF_BLOOM_ERROR if bloom is NULL.
 */
extern sof_bloom_error_t sof_bloom_free(sof_bloom_t *bloom);

/**
 * @brief Initializes a filter in a sof_bloom_t the caller provides.
 * @details A classic filter gets m = -n ln(p) / (ln 2)^2 bits for n
 * expected keys and a false positive rate p, rounded up to a whole number
 * of sof_bit_t words, and each key sets k = -log2(p) bits, rounded to the
 * nearest whole number and at least 1. A rate of 0.01 takes about 9.6 bits
 * and 7 hashes per key, and each tenth as large a rate adds about 4.8 bits
 * and 3 hashes.
 *
 * A blocked filter (SOF_BLOOM_BLOCKED) always sets 8 bits per key. Its rate
 * with an average of L keys per block is the sum, over j, of the Poisson
 * chance of a block holding j keys, e^-L L^j / j!, times that block's rate,
 * (1 - (31/32)^j)^8; sof_bloom_init() finds the L that gives p and takes
 * n / L blocks, rounded up. That is about 6.0 bits per key for 0.1, 10.5
 * for 0.01, 16.9 for 0.001 and 26.3 for 0.0001. It allocates 63 bytes more
 * than the blocks, to start them on a 64-byte boundary, and can have at
 * most 2^32 blocks (128 GiB).
 *
 * More keys than expected can be added to either kind; the false positive
 * rate then rises above p.
 * @param bloom The filter to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap.
 * @param expected_keys How many keys the filter is sized for; 0 is taken as
 * 1.
 * @param false_positive_rate The chance wanted, once expected_keys keys have
 * been added, that sof_bloom_contains() says yes for a key that wasn't:
 * more than 0 and less than 1.
 * @param flags 0 for a classic filter, or SOF_BLOOM_BLOCKED for a blocked
 * one.
 * @param alloc The memory functions the filter uses, or NULL for the
 * standard functions. The filter keeps a copy, so it can be on the stack.
 * @return SOF_BLOOM_OK, SOF_BLOOM_NO_MEM if the bits couldn't be allocated
 * (or would take more than a size_t can count, or more than 2^32 blocks),
 * or SOF_BLOOM_ERROR if bloom is NULL, false_positive_rate is out of range,
 * flags has a bit that isn't a sof_bloom_flag_t or alloc has a malloc but
 * no free; on an error, the filter is left empty, as if it had been freed.
 */
extern sof_bloom_error_t sof_bloom_init(sof_bloom_t *bloom, size_t expected_keys, double false_positive_rate, unsigned flags, const sof_alloc_t *alloc);

#endif // SOF_BLOOM_H
