/**
 * @file sieve.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A sieve of Eratosthenes over a bit array in C, for the sieve
 * program.
 * @details A sieve_t finds every prime up to a limit when it is set up with
 * sieve_init(), and keeps them as an array of sof_bit_t words with one bit
 * for each odd number, so it takes a sixteenth of the memory one byte per
 * number would. sieve_is_prime() looks a number up, sieve_next() finds the
 * next prime from a number on, sieve_count() counts the primes in a range,
 * and sieve_free() releases the array.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#ifndef SIEVE_H
#define SIEVE_H

#include <stddef.h>
#include <stdint.h>

#include "sof_alloc.h"
#include "sof_bit.h"

/**
 * @brief Error codes for the sieve functions.
 */
typedef enum sieve_error_t {
    SIEVE_OK = 0,
    SIEVE_NO_MEM,
    SIEVE_NO_MATCH,
    SIEVE_ERROR,
} sieve_error_t;

/**
 * @brief The primes up to a limit.
 * @details Bit i of the array, counting from bit 0 of words[0], is set if
 * the odd number 2i + 1 is prime; 2, the one even prime, has no bit. bits is
 * the number of odd numbers up to limit, which have bits, and the bits after
 * them are 0. word_count is the number of words in words, which is the
 * memory the sieve takes, and count is the number of primes up to limit.
 * alloc is a copy of the allocator (zeroed, meaning the standard functions,
 * if it was given none). These fields can be read, but should not be
 * changed by the application.
 *
 * sieve_init() must be called before using a sieve, and sieve_free() should
 * be called when it is no longer needed.
 *
 * Every function accepts a NULL sieve, and a sieve that was freed or whose
 * sieve_init() failed (words is NULL), as described for each.
 *
 * The sieve does no locking. Only sieve_init() and sieve_free() write it.
 */
typedef struct sieve_t {
    sof_bit_t *words;
    size_t word_count;
    uint64_t bits;
    uint64_t limit;
    uint64_t count;
    sof_alloc_t alloc;
} sieve_t;

/**
 * @brief Counts the primes in a range.
 * @param s The sieve.
 * @param low The smallest number to count.
 * @param high The largest number to count. Numbers above the sieve's limit
 * aren't counted.
 * @return The number of primes from low to high, both included; 0 if there
 * are none, low is above high, or s is NULL or not initialized.
 */
extern uint64_t sieve_count(const sieve_t *s, uint64_t low, uint64_t high);

/**
 * @brief Frees the bit array.
 * @details The sieve_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like a sieve whose sieve_init() failed until it is
 * initialized again.
 * @param s The sieve.
 * @return SIEVE_OK, or SIEVE_ERROR if s is NULL.
 */
extern sieve_error_t sieve_free(sieve_t *s);

/**
 * @brief Finds the primes up to a limit, in a sieve_t the caller provides.
 * @details Allocates a bit for each odd number up to limit, sets them all,
 * clears the bit of 1, and then, for each prime p whose square is at most
 * limit, found from the bits still set, clears the bits of the odd multiples
 * of p from p squared on. What is left set are the primes.
 * @param s The sieve to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap.
 * @param limit The largest number to sieve; any value, though the sieve
 * takes limit / 16 bytes.
 * @param alloc The memory functions the sieve uses, or NULL for the standard
 * functions. The sieve keeps a copy, so it can be on the stack.
 * @return SIEVE_OK, SIEVE_NO_MEM if the array couldn't be allocated, or
 * SIEVE_ERROR if s is NULL or alloc has a malloc but no free; on an error,
 * s is left empty.
 */
extern sieve_error_t sieve_init(sieve_t *s, uint64_t limit, const sof_alloc_t *alloc);

/**
 * @brief Says whether a number is prime.
 * @param s The sieve.
 * @param n The number.
 * @return 1 if n is prime, 0 if it isn't, or -1 if n is above the sieve's
 * limit, or s is NULL or not initialized.
 */
extern int sieve_is_prime(const sieve_t *s, uint64_t n);

/**
 * @brief Finds the smallest prime at or above a number.
 * @details For a loop over the primes:
 *
 *     for (uint64_t p = sieve_next(&s, 0); p != 0; p = sieve_next(&s, p + 1))
 * @param s The sieve.
 * @param from The number to start at.
 * @return The smallest prime from from to the sieve's limit, or 0 if there
 * is none, or s is NULL or not initialized. No prime is UINT64_MAX, so
 * p + 1 never wraps round in the loop above.
 */
extern uint64_t sieve_next(const sieve_t *s, uint64_t from);

#endif // SIEVE_H
