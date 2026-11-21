/**
 * @file sof_random.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Pseudo-random numbers in c.
 * @details A sof_random_t is a xoshiro256** generator, seeded from one 64-bit
 * value through splitmix64. sof_random_next() returns 64 random bits,
 * sof_random_below() and sof_random_range() unbiased integers in a range,
 * sof_random_double() a double in [0, 1), sof_random_exponential() and
 * sof_random_exponential_double() exponentially distributed times with a
 * given mean, sof_random_bytes() fills memory and sof_random_shuffle()
 * puts an array in random order.
 * sof_random_jump() moves a generator 2^128 steps ahead, to make streams
 * that don't overlap for separate threads. sof_random_splitmix64() is the
 * splitmix64 generator on its own, for a single uint64_t of state.
 *
 * The same seed gives the same numbers on every platform and compiler. None
 * of these are meant for cryptographic use.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_RANDOM_H
#define SOF_RANDOM_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Error codes for the sof_random functions.
 * @details SOF_RANDOM_NO_MEM and SOF_RANDOM_NO_MATCH are never returned,
 * since the library allocates and searches for nothing; they are there so the
 * codes match those of the other libraries.
 */
typedef enum sof_random_error_t {
    SOF_RANDOM_OK = 0,
    SOF_RANDOM_NO_MEM,
    SOF_RANDOM_NO_MATCH,
    SOF_RANDOM_ERROR,
} sof_random_error_t;

/**
 * @brief A generator.
 * @details The application provides the sof_random_t (on the stack, in
 * another structure or on the heap) and sets it up with sof_random_init().
 * Its fields can be read, but must not be changed by the application. Nothing
 * points into the sof_random_t, so it can be moved or copied; a copy is a
 * separate generator that goes on to give the same numbers as the original.
 *
 * Every function accepts a NULL generator, and one that was freed or never
 * initialized (ready is 0), as described for each.
 */
typedef struct sof_random_t {
    /// @brief The xoshiro256** state, never all 0 while ready.
    uint64_t s[4];
    /// @brief 1 once sof_random_init() has succeeded, 0 when freed.
    int ready;
} sof_random_t;

/**
 * @brief Return a random integer from 0 to n - 1.
 * @details Lemire's multiply method: the result is the high 64 bits of x * n,
 * x a number from sof_random_next(). Every value is equally likely: the x
 * that would make some results more likely than others, those whose product
 * has low 64 bits below 2^64 mod n, are thrown away, which happens with a
 * probability below n / 2^64, so less than once in four billion calls for n
 * under 2^32. A power of 2 gives the top bits of one number. No division is
 * done unless the low 64 bits come out below n.
 * @param r The generator.
 * @param n The number of possible values.
 * @return The integer, or 0 if n is 0 or 1, or r is NULL or not initialized
 * (r is then unchanged).
 */
extern uint64_t sof_random_below(sof_random_t *r, uint64_t n);

/**
 * @brief Fill memory with random bytes.
 * @details Each 8 bytes come from one sof_random_next(), lowest byte first,
 * so the bytes are the same on every platform whatever its byte order; a
 * last part shorter than 8 bytes uses the low bytes of one more.
 * @param r The generator.
 * @param data Where to put the bytes.
 * @param length The number of bytes.
 * @return SOF_RANDOM_OK, or SOF_RANDOM_ERROR if data is NULL and length
 * isn't 0, or r is NULL or not initialized; nothing is written then.
 */
extern sof_random_error_t sof_random_bytes(sof_random_t *r, void *data, size_t length);

/**
 * @brief Return a random double from 0 up to, but not including, 1.
 * @details The top 53 bits of sof_random_next(), divided by 2^53: every
 * multiple of 2^-53 in the range is equally likely.
 * @param r The generator.
 * @return The double, or 0 if r is NULL or not initialized.
 */
extern double sof_random_double(sof_random_t *r);

/**
 * @brief Return a random whole number from an exponential distribution with
 * a given mean.
 * @details The time between events that happen at random at a steady rate,
 * such as arrivals at a queue, in whole ticks: mean * E, rounded to the
 * nearest integer, where E is a standard exponential draw in 32.32 fixed
 * point. E comes from Ahrens and Dieter's algorithm SA, which needs no
 * logarithm: the leading zeros of one sof_random_next() give E's multiple of
 * ln 2, and the bits after them the rest, about 69% of the time; otherwise a
 * few more numbers are drawn (1.69 numbers a draw on average). Integers only,
 * so the results are the same on every platform and compiler, which the c
 * library's log() doesn't promise. E is below 65 ln 2, about 45.06, which
 * cuts off a tail with a probability of 2^-65. How many numbers are drawn
 * doesn't depend on mean, so neither do the numbers drawn after it.
 * @param r The generator.
 * @param mean The mean of the distribution, in whatever unit the caller
 * counts; 0 gives 0.
 * @return The draw, or UINT64_MAX if it doesn't fit, which can only happen
 * for a mean above 2^58. 0 if r is NULL or not initialized (r is then
 * unchanged).
 */
extern uint64_t sof_random_exponential(sof_random_t *r, uint64_t mean);

/**
 * @brief Return a random double from an exponential distribution with a
 * given mean.
 * @details mean * E, with E drawn exactly as for sof_random_exponential():
 * a multiple of 2^-32 from 0 to about 45.06, converted to a double without
 * loss and multiplied by mean, the only rounding. The results are the same on
 * every platform that has IEEE 754 doubles. With a mean of 1 it is the
 * standard exponential distribution. How many numbers are drawn doesn't
 * depend on mean.
 * @param r The generator.
 * @param mean The mean of the distribution. A negative mean gives results
 * of 0 or below, and a NaN or infinite one whatever IEEE 754 multiplication
 * gives.
 * @return The draw, or 0 if r is NULL or not initialized (r is then
 * unchanged).
 */
extern double sof_random_exponential_double(sof_random_t *r, double mean);

/**
 * @brief Leave the generator empty.
 * @details The library allocates nothing, so there is nothing to release, but
 * the state is cleared and the generator acts like one that was never
 * initialized until sof_random_init() is called again. The sof_random_t
 * itself isn't freed; it belongs to the caller.
 * @param r The generator.
 * @return SOF_RANDOM_OK, or SOF_RANDOM_ERROR if r is NULL or not
 * initialized.
 */
extern sof_random_error_t sof_random_free(sof_random_t *r);

/**
 * @brief Initialize a generator in a sof_random_t the caller provides.
 * @details The four words of state are the first four results of
 * sof_random_splitmix64() started at seed, which are never all 0. Any seed is
 * fine, 0 included; different seeds give unrelated sequences. Calling it again
 * on an initialized generator starts it over from the new seed.
 * @param r The generator to initialize.
 * @param seed The seed.
 * @return SOF_RANDOM_OK, or SOF_RANDOM_ERROR if r is NULL.
 */
extern sof_random_error_t sof_random_init(sof_random_t *r, uint64_t seed);

/**
 * @brief Move the generator 2^128 steps ahead.
 * @details The same as calling sof_random_next() 2^128 times, but takes about
 * as long as 256 calls. To give each of several threads a stream of its own,
 * initialize one generator, copy it for the first thread, jump it, copy it for
 * the second, and so on: the streams won't overlap for 2^128 numbers each.
 * @param r The generator.
 * @return SOF_RANDOM_OK, or SOF_RANDOM_ERROR if r is NULL or not
 * initialized.
 */
extern sof_random_error_t sof_random_jump(sof_random_t *r);

/**
 * @brief Return 64 random bits.
 * @details This is xoshiro256**, by David Blackman and Sebastiano Vigna:
 * every bit is random, and the sequence repeats only after 2^256 - 1 numbers.
 * @param r The generator.
 * @return The next number, or 0 if r is NULL or not initialized.
 */
extern uint64_t sof_random_next(sof_random_t *r);

/**
 * @brief Return a random integer from lo to hi, both included.
 * @details Every value is equally likely, as with sof_random_below(). If hi
 * is less than lo, the two are swapped. The full range, INT64_MIN to
 * INT64_MAX, works.
 * @param r The generator.
 * @param lo One end of the range.
 * @param hi The other end of the range.
 * @return The integer, or the smaller of lo and hi if r is NULL or not
 * initialized (r is then unchanged).
 */
extern int64_t sof_random_range(sof_random_t *r, int64_t lo, int64_t hi);

/**
 * @brief Put the elements of an array in random order.
 * @details A Fisher-Yates shuffle, so every order is equally likely. It takes
 * count - 1 calls to sof_random_below() and swaps elements of any size in
 * place, using no memory beyond a small buffer on the stack. Elements of 1,
 * 2, 4, 8 and 16 bytes are swapped as single values, and others under 64
 * bytes 8 bytes at a time.
 * @param r The generator.
 * @param base The array.
 * @param count The number of elements.
 * @param size The size of each element in bytes.
 * @return SOF_RANDOM_OK, or SOF_RANDOM_ERROR if size is 0, base is NULL and
 * count isn't 0, or r is NULL or not initialized; the array is unchanged then.
 */
extern sof_random_error_t sof_random_shuffle(sof_random_t *r, void *base, size_t count, size_t size);

/**
 * @brief Return the next number of a splitmix64 generator.
 * @details splitmix64, by Sebastiano Vigna, adds a constant to its 64-bit
 * state and scrambles the result. It is small and fast, and any state,
 * 0 included, is fine, but it is weaker than xoshiro256** and repeats after
 * 2^64 numbers. sof_random_init() seeds a sof_random_t with it; use it
 * directly where a single uint64_t of state is all there is room for.
 * @param state The generator's state, which is advanced.
 * @return The next number, or 0 if state is NULL.
 */
extern uint64_t sof_random_splitmix64(uint64_t *state);

#endif // SOF_RANDOM_H
