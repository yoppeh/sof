# sof_random

Pseudo-random numbers in c.

The library has a `sof_random_t` type, a xoshiro256** generator. The application provides the `sof_random_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_random_init()`, which seeds it from one 64-bit value. `sof_random_next()` returns 64 random bits. `sof_random_below()` returns an integer from **0** up to a limit, and `sof_random_range()` one between two signed limits, both included, every value equally likely. `sof_random_double()` returns a double from **0** up to **1**. `sof_random_exponential()` and `sof_random_exponential_double()` return a time from an exponential distribution with a given mean, as a whole number or a double, the time between events that happen at random at a steady rate. `sof_random_bytes()` fills memory with random bytes, and `sof_random_shuffle()` puts an array of any type in random order. `sof_random_jump()` moves a generator 2^128 numbers ahead, to give separate threads streams that don't overlap. `sof_random_free()` leaves the generator empty once the application is finished with it. `sof_random_splitmix64()` is the smaller splitmix64 generator on its own, for where a single `uint64_t` of state is all there is room for.

[xoshiro256**](https://prng.di.unimi.it/) and splitmix64 are by David Blackman and Sebastiano Vigna, who put their reference code in the public domain. xoshiro256** is fast, passes the standard statistical tests, and repeats only after 2^256 - 1 numbers. `sof_random_init()` fills its four words of state with the first four results of splitmix64 started at the seed, so any seed, **0** included, gives a good state. The same seed gives the same numbers on every platform and compiler, and `sof_random_bytes()` writes each number's bytes lowest first, so its output doesn't depend on byte order either. `sof_random_below()` and `sof_random_range()` use Lemire's multiply method, taking the high half of a 128-bit product rather than a remainder, and throw away the few numbers that would make some results more likely than others; they divide only in the rare case that might need a redraw, which matters on x86-64, where 64-bit division is slow (1.3 ns a call against 3.5 with a remainder on an Intel i9; the same 2.2 ns either way on an Apple M4 Max). The exponential draws use Ahrens and Dieter's algorithm SA, which builds an exact exponential distribution from the leading zeros and remaining bits of uniform numbers, with integers only in 32.32 fixed point, rather than the c library's `log()`, whose last bits differ between platforms, so they too are the same everywhere; a draw takes about 5 ns, where working out a logarithm bit by bit took 40. `sof_random_shuffle()` swaps elements of 1, 2, 4, 8 and 16 bytes as single values, and others under 64 bytes 8 bytes at a time, about 2.5 ns an element on the M4 Max and 1.5 on the i9 for the common sizes. None of this is meant for cryptographic use.

The library's demo is the [markov](demo/) program in _demo/_, which learns n-grams from text and generates new text from them, drawing each choice from a generator. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All sof_random declarations are in _sof_random.h_. The library uses no other library in this collection, allocates nothing, and needs nothing beyond the c standard library.

    sof_random_t r;
    sof_random_init(&r, 42);                            // the same seed, the same numbers
    uint64_t bits = sof_random_next(&r);                // 64 random bits
    uint64_t die = 1 + sof_random_below(&r, 6);         // 1 to 6
    int64_t t = sof_random_range(&r, -10, 10);          // -10 to 10
    double p = sof_random_double(&r);                   // [0, 1)
    uint64_t wait = sof_random_exponential(&r, 300);    // averages 300
    sof_random_shuffle(&r, cards, 52, sizeof(card_t));  // any array, in place
    sof_random_free(&r);

## Building

    make             # compiles the library and builds ./test, ./test-portable and the demo, demo/markov
    make check       # builds and runs the tests, test and test-portable
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

The tests check splitmix64 and xoshiro256** against the results of the reference implementations, and the jump against the reference `jump()`. They check that every result of `sof_random_below()` and `sof_random_range()` is in range, from 2 values to the full 64 bits, that `sof_random_below()` matches pinned values and a multiply-and-redraw worked out separately in 32-bit halves, and that results are spread evenly where a biased method would not be; that each of the six orders of three elements is equally likely from `sof_random_shuffle()`, and that elements larger than its swap buffer, and of odd sizes, move whole; that the exponential draws match pinned values for one seed, so every platform is checked to give the same ones, are within 4 * 10^-9 of the same algorithm worked out in doubles from the same numbers, including numbers with 30 to 64 leading zeros, and have the right mean, variance and tails over a million draws and fit the distribution in a chi-square test over four million; and that generators drawn from on separate threads with no lock give exactly what they give drawn from in turn. The statistical checks use fixed seeds, so they give the same result every run. _test-portable_ is the same tests with _sof_random.c_ compiled with **SOF_RANDOM_NO_BUILTINS**, so it uses neither `__builtin_clzll()` nor `unsigned __int128`, as on a compiler without them, and must give the same pinned values. The tests are linked with _-pthread_; the library itself doesn't use threads.

To use the library in another project, copy _sof_random.c_ and _sof_random.h_ into it and compile _sof_random.c_ with the project's own sources. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_ and _sof_random.c_ with _-pthread_.

## Functions

`uint64_t sof_random_below(sof_random_t *r, uint64_t n)`

Return a random integer from **0** to _n_ - 1, every value equally likely. The result is the high 64 bits of _x_ * _n_, for a number _x_ from `sof_random_next()` (Lemire's method). An _x_ whose product has low 64 bits below 2^64 mod _n_, which would make some results more likely, is thrown away and drawn again; that happens with a probability below _n_ / 2^64, so almost never for small _n_, and never when _n_ is a power of 2, which gives the top bits of one number. The division that finds 2^64 mod _n_ is done only when the low bits come out below _n_. Returns **0** if _n_ is **0** or **1**, without using the generator, or if _r_ isn't initialized.

`sof_random_error_t sof_random_bytes(sof_random_t *r, void *data, size_t length)`

Fill the _length_ bytes at _data_ with random bytes. Each 8 bytes are one number from `sof_random_next()`, lowest byte first, and a last part shorter than 8 bytes is the low bytes of one more, so the bytes are the same on every platform. Returns **SOF_RANDOM_OK**, or **SOF_RANDOM_ERROR** if _data_ is **NULL** and _length_ isn't **0**, or _r_ isn't initialized, in which case nothing is written.

`double sof_random_double(sof_random_t *r)`

Return a random double from **0** up to, but not including, **1**: the top 53 bits of a number from `sof_random_next()`, divided by 2^53, so every multiple of 2^-53 in that range is equally likely. Returns **0** if _r_ isn't initialized.

`uint64_t sof_random_exponential(sof_random_t *r, uint64_t mean)`

Return a random whole number from an exponential distribution with mean _mean_: _mean_ * E, rounded to the nearest integer, where E is a standard exponential draw. This is the time between events that happen at random at a steady rate, such as arrivals at a queue, in whatever unit _mean_ counts. E is drawn in 32.32 fixed point with integers only, by Ahrens and Dieter's algorithm SA (Knuth, _The Art of Computer Programming_, 3.4.1, algorithm S): the leading zeros of one number from `sof_random_next()` count whole multiples of ln 2, and the bits after its highest 1 are a uniform U; if U is below ln 2, about 69% of the time, E is the multiples plus U, and otherwise a few more numbers are drawn and the least of them, times ln 2, takes U's place. That averages 1.69 numbers a draw, and needs no logarithm, so the results are the same on every platform. E is below 65 ln 2, about 45.06, which cuts off a tail with a probability of 2^-65. How many numbers are drawn doesn't depend on _mean_, **0** included, so later draws don't depend on it. Returns the draw; **UINT64_MAX** if it doesn't fit, which can only happen for a _mean_ above 2^58; or **0** if _r_ isn't initialized, leaving _r_ unchanged.

`double sof_random_exponential_double(sof_random_t *r, double mean)`

Return a random double from an exponential distribution with mean _mean_: the same fixed-point E as `sof_random_exponential()`, a multiple of 2^-32 from **0** to about 45.06, converted to a double without loss and multiplied by _mean_, which is the only rounding, so the results are the same on every platform with IEEE 754 doubles. A _mean_ of **1** gives the standard exponential distribution, and a negative _mean_ results of **0** or below. How many numbers are drawn doesn't depend on _mean_. Returns **0** if _r_ isn't initialized, leaving _r_ unchanged.

`sof_random_error_t sof_random_free(sof_random_t *r)`

Leave the generator empty. The library allocates nothing, so nothing is released, but the state is cleared and the generator acts as one that was never initialized until `sof_random_init()` is called again. The `sof_random_t` itself belongs to the caller and isn't freed. Returns **SOF_RANDOM_OK**, or **SOF_RANDOM_ERROR** if _r_ isn't initialized.

`sof_random_error_t sof_random_init(sof_random_t *r, uint64_t seed)`

Initialize the generator _r_ the caller provides from _seed_. Any _seed_ is fine, **0** included, and different seeds give unrelated sequences. Calling it on a generator that is already initialized starts it over from the new seed. Returns **SOF_RANDOM_OK**, or **SOF_RANDOM_ERROR** if _r_ is **NULL**.

`sof_random_error_t sof_random_jump(sof_random_t *r)`

Move the generator 2^128 numbers ahead, as if `sof_random_next()` had been called that many times, in about the time of 256 calls. To give several threads streams of their own, initialize one generator, copy it for the first thread, jump it, copy it for the second, and so on; the streams won't overlap for 2^128 numbers each. Returns **SOF_RANDOM_OK**, or **SOF_RANDOM_ERROR** if _r_ isn't initialized.

`uint64_t sof_random_next(sof_random_t *r)`

Return the next 64 random bits from the generator, from xoshiro256**. Returns **0** if _r_ isn't initialized.

`int64_t sof_random_range(sof_random_t *r, int64_t lo, int64_t hi)`

Return a random integer from _lo_ to _hi_, both included, every value equally likely, as with `sof_random_below()`. If _hi_ is less than _lo_, the two are swapped. The full range, **INT64_MIN** to **INT64_MAX**, works, and is one number from `sof_random_next()`. Returns the smaller of _lo_ and _hi_ if _r_ isn't initialized.

`sof_random_error_t sof_random_shuffle(sof_random_t *r, void *base, size_t count, size_t size)`

Put the _count_ elements of _size_ bytes at _base_ in random order, in place, every order equally likely. It is a Fisher-Yates shuffle, making _count_ - 1 calls to `sof_random_below()`, and swaps elements of 1, 2, 4, 8 and 16 bytes as single values, others under 64 bytes 8 bytes at a time, and larger ones through a small buffer on the stack, so it allocates nothing whatever their size. An array of **0** or **1** elements is left as it is without using the generator. Returns **SOF_RANDOM_OK**, or **SOF_RANDOM_ERROR** if _size_ is **0**, _base_ is **NULL** and _count_ isn't **0**, or _r_ isn't initialized, in which case the array is unchanged.

`uint64_t sof_random_splitmix64(uint64_t *state)`

Return the next number of the splitmix64 generator whose state is at _state_, advancing it. Any state is fine, **0** included. splitmix64 is fast and good, but weaker than xoshiro256** and repeats after 2^64 numbers; use it where a single `uint64_t` of state is all there is room for, or to make seeds. Returns **0** if _state_ is **NULL**.

Every function that takes a generator accepts a **NULL** _r_, and a generator that was freed or never initialized (`ready` is **0**, as in a zeroed `sof_random_t`): `sof_random_next()`, `sof_random_below()`, `sof_random_double()`, `sof_random_exponential()` and `sof_random_exponential_double()` return **0**, `sof_random_range()` returns the smaller of its limits, and the others return **SOF_RANDOM_ERROR**, without touching the memory they were given. Such a generator stays that way until `sof_random_init()` is called on it.

## Types

    typedef enum sof_random_error_t {
        SOF_RANDOM_OK = 0,
        SOF_RANDOM_NO_MEM,
        SOF_RANDOM_NO_MATCH,
        SOF_RANDOM_ERROR,
    } sof_random_error_t;

What the functions that don't return a number return. **SOF_RANDOM_NO_MEM** and **SOF_RANDOM_NO_MATCH** are never returned, since the library allocates and searches for nothing; they are there so the codes match those of the other libraries.

    typedef struct sof_random_t {
        uint64_t s[4];  // the xoshiro256** state, never all 0 while ready
        int ready;      // 1 once sof_random_init() has succeeded, 0 when freed
    } sof_random_t;

The fields can be read but should not be changed by the application. Nothing points into a `sof_random_t`, so it can be moved or copied; a copy is a separate generator that goes on to give the same numbers as the original, which `sof_random_jump()` can then move apart.

## Threading

The library has no global state and does no locking. Separate generators can be used on separate threads freely; `sof_random_jump()` gives each a stream of its own from one seed, as described above. Every function that takes a generator changes its state, apart from reading the fields, so a generator shared between threads needs a lock supplied by the application around every call, and a readers-writer lock gains nothing over a plain mutex. A generator per thread is faster, and keeps each thread's numbers repeatable. `sof_random_splitmix64()` changes only the state it is given, so it is safe on separate states at once.
