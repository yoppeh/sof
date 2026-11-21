# sof_bit

Bit operations on a machine word in c.

The library works on a `sof_bit_t`, an unsigned integer the size of the machine's word, passed and returned by value, so there is nothing to set up or free. `sof_bit_set()`, `sof_bit_clear()` and `sof_bit_flip()` return the word with one bit set, cleared or flipped. `sof_bit_test()` reads one bit. `sof_bit_count()` counts the bits that are set, `sof_bit_lowest()` finds the lowest set bit, and `sof_bit_next()` the lowest set bit at or above a given one; either makes a loop over the set bits. `sof_bit_highest()` finds the highest set bit, which is the base 2 logarithm rounded down, and `sof_bit_ceil_log2()` gives the logarithm rounded up, the number of bits needed for a number of values. Bits are numbered from 0, the least significant, to `SOF_BIT_BITS` - 1. With gcc and clang, counting and finding use the compiler's builtins, which compile to single instructions on most machines. Without them the lowest bit is found with a de Bruijn multiply and a table lookup.

Everything is in _sof_bit.h_, as `static inline` functions, so there is no _sof_bit.c_. The library's demo is the [sieve](demo/) program in _demo/_, which finds primes with a sieve of Eratosthenes over an array of `sof_bit_t` words, a bit for each odd number. The tests are in _test.c_, five programs of their own that the Makefile builds from it and runs, using the harness the collection's tests share in [../test/](../test/). The library needs nothing beyond the c standard library.

    sof_bit_t flags = 0;
    flags = sof_bit_set(flags, 3);                    // bit 3 on
    flags = sof_bit_flip(flags, 5);                   // bit 5 on
    if (sof_bit_test(flags, 3)) { ... }               // 1: bit 3 is set
    unsigned n = sof_bit_count(flags);                // 2
    unsigned top = sof_bit_highest(flags);            // 5
    unsigned bits = sof_bit_ceil_log2(1000);          // 10: 2^10 >= 1000
    unsigned low = sof_bit_lowest(flags);             // 3
    for (sof_bit_t x = flags; x != 0; x &= x - 1) {   // clears each bit of a copy
        unsigned i = sof_bit_lowest(x);               // i is 3, then 5
        ...
    }
    for (unsigned i = sof_bit_next(flags, 0); i < SOF_BIT_BITS; i = sof_bit_next(flags, i + 1)) {
        ...                                           // the same, leaving flags as it is
    }

## Building

    make             # checks that sof_bit.h compiles on its own and builds the five test programs and the demo, demo/sieve
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

The tests are compiled five times from _test.c_: as _test_, with the default `sof_bit_t`; as _test-uint32_, with `uint32_t`; and with **SOF_BIT_NO_BUILTINS**, so the plain c versions of `sof_bit_count()`, `sof_bit_lowest()`, `sof_bit_next()`, `sof_bit_highest()` and `sof_bit_ceil_log2()` are tested, as _test-uint8_, _test-uint32-portable_ and _test-uint64-portable_, with `uint8_t`, `uint32_t` and `uint64_t`, which use the 32-bit and 64-bit de Bruijn tables. Each checks the functions against bit-by-bit versions that don't use the library, at every single bit and on random words, and checks that both loops over the set bits visit each once, in order.

To use the library in another project, copy _sof_bit.h_ into it and include it. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_ with _-pthread_, adding `-D SOF_BIT_TYPE=uint32_t`, and `-D SOF_BIT_NO_BUILTINS` with `-D SOF_BIT_TYPE=uint8_t`, `uint32_t` or `uint64_t`, for the other four programs.

## Word size

`sof_bit_t` is `size_t` unless **SOF_BIT_TYPE** is defined as another unsigned integer type before _sof_bit.h_ is included, as in `-D SOF_BIT_TYPE=uint32_t`. `size_t` is the machine's register width on every common platform: 64 bits on 64-bit machines, Windows included, and 32 on 32-bit ones. A narrower type, such as `uint8_t`, suits a small microcontroller. The type must be unsigned and no wider than `unsigned long long`; the header refuses to compile otherwise.

Because the size differs between platforms, a `sof_bit_t`, or an array of them, shouldn't be written to a file or sent to another machine as it is. Where that's needed, define **SOF_BIT_TYPE** as a fixed-width type such as `uint64_t` and choose a byte order.

Defining **SOF_BIT_NO_BUILTINS** makes `sof_bit_count()`, `sof_bit_lowest()`, `sof_bit_next()`, `sof_bit_highest()` and `sof_bit_ceil_log2()` use plain c instead of the gcc and clang builtins. The results are the same; compilers without the builtins, such as MSVC, use the plain c versions anyway. `sof_bit_lowest()`, and so `sof_bit_next()`, isolates the lowest set bit with `x & -x` and multiplies it by a de Bruijn sequence, whose top bits then pick the answer from a table of 64 entries (32 for words of 32 bits or fewer). On an Apple M4 Max, with 64-bit words, that takes a loop over the set bits of random words from 37 ns, a bit at a time, to 11.7 with `sof_bit_next()` and 5.1 with `sof_bit_lowest()`; on an Intel i9 with gcc, from 34 ns to 13.8 and 6.5. `sof_bit_highest()`, and so `sof_bit_ceil_log2()`, does a binary search over the bit positions, six steps for 64-bit words: on the M4 Max, 0.87 ns against 12.4 for a loop a bit at a time.

## Functions

`unsigned sof_bit_ceil_log2(sof_bit_t x)`

Return the base 2 logarithm of _x_ rounded up: the smallest _k_ for which 2^_k_ is _x_ or more, so the number of bits needed to tell _x_ values apart. It returns **0** for _x_ **0** and **1**, and `SOF_BIT_BITS` when _x_ is above 2^(`SOF_BIT_BITS` - 1), even though 2^`SOF_BIT_BITS` doesn't fit in a `sof_bit_t`. For any other _x_ it is `sof_bit_highest(x - 1) + 1`.

`sof_bit_t sof_bit_clear(sof_bit_t x, unsigned bit)`

Return _x_ with bit number _bit_ cleared. The other bits are unchanged. _bit_ must be below `SOF_BIT_BITS`; a larger value is undefined behaviour, as a shift that large is in c.

`unsigned sof_bit_count(sof_bit_t x)`

Return the number of bits set in _x_, from **0** to `SOF_BIT_BITS`.

`sof_bit_t sof_bit_flip(sof_bit_t x, unsigned bit)`

Return _x_ with bit number _bit_ flipped. The other bits are unchanged. _bit_ must be below `SOF_BIT_BITS`.

`unsigned sof_bit_highest(sof_bit_t x)`

Return the number of the highest set bit of _x_, from **0** to `SOF_BIT_BITS` - 1, or `SOF_BIT_BITS` if _x_ is **0**, the same value `sof_bit_next()` returns when there is no bit to find. For _x_ above **0** this is the base 2 logarithm of _x_ rounded down. The lowest set bit is `sof_bit_lowest(x)`.

`unsigned sof_bit_lowest(sof_bit_t x)`

Return the number of the lowest set bit of _x_, from **0** to `SOF_BIT_BITS` - 1, or `SOF_BIT_BITS` if _x_ is **0**; the same as `sof_bit_next(x, 0)`. With `x &= x - 1`, which clears the lowest set bit, it makes the fastest loop over the set bits, lowest first, as in the example above, on a copy of the word, since the loop clears it: about half the time of the loop with `sof_bit_next()` with the builtins (5.4 ns against 9.6 for a random 64-bit word on an Apple M4 Max), and less than half without.

`unsigned sof_bit_next(sof_bit_t x, unsigned from)`

Return the number of the lowest set bit of _x_ at or above bit _from_, or `SOF_BIT_BITS` if there is none. Any _from_ is allowed: at or past `SOF_BIT_BITS` it returns `SOF_BIT_BITS`, so a loop can always ask again from the last bit found plus 1, as in the example above, without changing _x_. `sof_bit_next(x, 0)` is `sof_bit_lowest(x)`.

`sof_bit_t sof_bit_set(sof_bit_t x, unsigned bit)`

Return _x_ with bit number _bit_ set. The other bits are unchanged. _bit_ must be below `SOF_BIT_BITS`.

`int sof_bit_test(sof_bit_t x, unsigned bit)`

Return **1** if bit number _bit_ is set in _x_, or **0**. _bit_ must be below `SOF_BIT_BITS`.

There are no pointers, so there is no **NULL** to handle. Only `sof_bit_next()` checks its bit number; the others leave that to the caller, so they stay a single operation. Every word, **0** included, is a valid argument to `sof_bit_count()`, `sof_bit_lowest()`, `sof_bit_highest()` and `sof_bit_ceil_log2()`.

## Types

    typedef SOF_BIT_TYPE sof_bit_t;     // size_t by default
    #define SOF_BIT_BITS ((unsigned)(sizeof(sof_bit_t) * CHAR_BIT))

`sof_bit_t` is an ordinary unsigned integer, and the application can use it with c's own operators as well as these functions: `a | b`, `a & b` and `a & ~b` combine words. `SOF_BIT_BITS` is a constant expression, so it can size an array, but it uses `sizeof`, so it can't be used in `#if`.

## Threading

The library has no state, and every function works only on its arguments, so any number of threads can call any of them at once.
