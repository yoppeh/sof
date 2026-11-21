# sieve

A prime finder in c: the primes up to a number, how many there are, or whether given numbers are prime, by a sieve of Eratosthenes over a bit array.

`sieve` prints the primes up to a limit, one to a line, or with `-f` only those from a given number on, or with `-c` only how many there are. With `-t`, it says for each number given whether it is prime, and if it isn't, gives its smallest prime factor; numbers up to 2^64 - 1 take a sieve only up to the square root of the largest. With `-m` it reports the memory the sieve took, a bit for each odd number, against an array of one byte per number.

_main.c_ is the program. The sieve is kept by _sieve.c_, declared in _sieve.h_, in a `sieve_t` that the program provides and sets up with `sieve_init()`, giving the limit, which finds every prime up to it. `sieve_is_prime()` looks a number up, `sieve_next()` finds the smallest prime at or above a number, which makes a loop over the primes, and `sieve_count()` counts the primes in a range. `sieve_free()` releases the sieve's memory.

The sieve is an array of `sof_bit_t` words, from the [sof_bit](../../sof_bit/) library, with bit _i_ standing for the odd number 2_i_ + 1; 2, the only even prime, is handled on its own, so the sieve takes a bit for every two numbers, a sixteenth of what one byte per number takes. `sieve_init()` sets the bits of every odd number but 1, then takes each prime _p_ whose square is at most the limit, found with `sof_bit_next()` from the bits still set, and strikes out its odd multiples from _p_ * _p_ on, which are _p_ bits apart, with `sof_bit_clear()`. `sieve_is_prime()` reads a bit with `sof_bit_test()`, `sieve_next()` finds the next bit set a word at a time, with `sof_bit_next()` in the word it starts in and `sof_bit_lowest()` in each word after it, and `sieve_count()` counts the bits set a word at a time with `sof_bit_count()`, masking the words at the ends of the range. With `-t`, a number up to the limit is looked up, and a larger one is divided by the primes up to its square root, from `sieve_next()`.

Bits are handled with the [sof_bit](../../sof_bit/) library in _../../sof_bit/_ (_sof_bit.h_, which is all of it), and memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    sieve_t s;
    sieve_init(&s, 1000000, NULL);                 // no allocator: the standard functions
    int prime = sieve_is_prime(&s, 999983);        // 1
    uint64_t n = sieve_count(&s, 0, 1000000);      // 78498, also in s.count
    for (uint64_t p = sieve_next(&s, 0); p != 0; p = sieve_next(&s, p + 1)) {
        ...                                        // 2, 3, 5, 7, ...
    }
    sieve_free(&s);

## Building

    make            # builds ./sieve
    make debug=1    # builds with debug info and without optimization
    ./sieve 100                 # the primes up to 100
    ./sieve -c -m 100000000     # how many there are up to 10^8, and the memory it took

`make` in the library's directory, _../_, builds it with the library, as do `make sof_bit` and `make` from the root of the project.

To build it in another project, copy _main.c_, _sieve.c_ and _sieve.h_, with _sof_bit.h_ from _../../sof_bit/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it. To use only the sieve, _sieve.c_ and _sieve.h_ need just the _sof_bit_ and _sof_alloc_ files.

## Usage

    sieve [-h] [-c] [-m] [-f from] limit
    sieve [-h] [-m] -t number...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-c`: print only the number of primes up to _limit_, or from _from_ to _limit_ with `-f`.
- `-f` _from_: print, or count, only the primes from _from_ to _limit_.
- `-m`: after the primes, report on standard error how many primes the sieve found, the memory its bits took, and the memory an array of one byte for each number from 0 to the limit would take.
- `-t`: instead of listing primes, say for each _number_ whether it is prime, and if it isn't, give its smallest prime factor and what is left. Numbers can be up to 2^64 - 1, for which the sieve goes up to 2^32 and takes 256 MB.
- _limit_: the largest number to sieve, from 0 up. The sieve takes _limit_ / 16 bytes, rounded up to a whole word.

It exits with **0**, or with `-t`, **0** if every number is prime and **1** if any isn't. It exits with **2** if the options are wrong, or memory runs out.

For example:

    $ ./sieve 30
    2
    3
    5
    7
    11
    13
    17
    19
    23
    29

    $ ./sieve -f 1000 1100 | paste -sd' ' -
    1009 1013 1019 1021 1031 1033 1039 1049 1051 1061 1063 1069 1087 1091 1093 1097

the number of primes below 10^8, and the memory the sieve took:

    $ ./sieve -c -m 100000000
    5761455
    5761455 primes up to 100000000, sieved in 6250008 bytes (781251 words of 64 bits, a bit for each odd number);
    one byte per number would take 100000001 bytes, 16.0 times as much

and some numbers tested, the last the largest prime below 2^64:

    $ ./sieve -t 97 1001 2147483647 18446744073709551557
    97 is prime
    1001 is not prime: 7 x 143
    2147483647 is prime
    18446744073709551557 is prime

## Functions

`sieve_error_t sieve_init(sieve_t *s, uint64_t limit, const sof_alloc_t *alloc)`

Initialize the sieve _s_ the caller provides with the primes from 0 to _limit_, which can be any value, though the sieve takes _limit_ / 16 bytes. _alloc_ is the allocator for the sieve's memory, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into the sieve, so it can be on the stack. Returns **SIEVE_OK**, **SIEVE_NO_MEM** if the bits couldn't be allocated, or **SIEVE_ERROR** if _s_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves _s_ empty.

`int sieve_is_prime(const sieve_t *s, uint64_t n)`

Return **1** if _n_ is prime, **0** if it isn't, or **-1** if _n_ is above the sieve's limit.

`uint64_t sieve_next(const sieve_t *s, uint64_t from)`

Return the smallest prime from _from_ to the sieve's limit, or **0** if there is none. `sieve_next(s, 0)` is **2**, if the limit is at least **2**, and a loop passing the last prime found plus **1** goes through them all, as in the example above; no prime is 2^64 - 1, so that never wraps round.

`uint64_t sieve_count(const sieve_t *s, uint64_t low, uint64_t high)`

Return the number of primes from _low_ to _high_, both included. Numbers above the sieve's limit aren't counted, and a _low_ above _high_ gives **0**. The number up to the limit is in `count` too.

`sieve_error_t sieve_free(sieve_t *s)`

Free the sieve's bits. The `sieve_t` itself belongs to the caller and isn't freed; it is left empty. Returns **SIEVE_OK**.

Every function accepts a **NULL** _s_: `sieve_is_prime()` returns **-1**, `sieve_next()` and `sieve_count()` return **0**, and the others return **SIEVE_ERROR**. A sieve that was freed, or whose `sieve_init()` failed, is empty and stays that way: it has no primes, so the same values come back, but `sieve_free()` returns **SIEVE_OK**, until it is initialized again.

## Types

    typedef enum sieve_error_t {
        SIEVE_OK = 0,
        SIEVE_NO_MEM,
        SIEVE_NO_MATCH,     // never returned
        SIEVE_ERROR,
    } sieve_error_t;

    typedef struct sieve_t {
        sof_bit_t *words;       // bit i, from bit 0 of words[0], is set if 2i + 1 is prime
        size_t word_count;      // the words in words
        uint64_t bits;          // the odd numbers up to limit, which have bits
        uint64_t limit;         // the largest number sieved
        uint64_t count;         // the primes up to limit
        sof_alloc_t alloc;      // copy of the allocator; zeroed for the standard functions
    } sieve_t;

The fields can be read but should not be changed by the application. The bits past _bits_ are **0**. _word_count_ is _bits_ / `SOF_BIT_BITS` + 1, so there is always a word, even for a limit below 3, which has no odd primes. The memory the sieve takes is _word_count_ * `sizeof(sof_bit_t)`.

## Threading

_sieve.c_ has no globals and does no locking, so separate sieves can be used on separate threads freely. `sieve_is_prime()`, `sieve_next()` and `sieve_count()` only read the sieve, so once `sieve_init()` has returned, any number of threads can call them at once; only `sieve_init()` and `sieve_free()` write it. With a readers-writer lock, take the write lock for those two and the read lock for the rest. The program uses one thread.
