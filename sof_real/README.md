# sof_real

Shortest round-trip formatting and exact parsing of floating-point numbers in c.

The library has no type of its own to set up: each function takes a value or a text and gives the other. `sof_real_format()` writes a double with the fewest significant digits that read back as the same double, and of those the closest to it, into the caller's buffer, and `sof_real_format_float()` does the same for a float, with at most 9 digits rather than 17. `sof_real_decimal()` and `sof_real_decimal_float()` give those digits as an integer and their power of ten, for a caller that writes numbers its own way. `sof_real_parse()` reads a decimal number from a text and rounds it correctly to the nearest double, however many digits it has, and `sof_real_parse_float()` to the nearest float, rounding once, straight to the float. `sof_real_from_decimal()` and `sof_real_from_decimal_float()` do the same for digits and a power of ten the caller has already taken apart, and are the inverse of `sof_real_decimal()` and `sof_real_decimal_float()`. None of them depends on the locale: the decimal point is always '.'. None allocates, keeps any state, or uses the maths library.

Formatting is Raffaello Giulietti's Schubfach ("The Schubfach way to render doubles", 2020), which gives the same digits as Ryu and as Java's `Double.toString()`: of the decimals in the value's rounding interval, those with the fewest digits, and of those the closest, with ties to the even one. Parsing takes the first 19 significant digits, which fit in 64 bits, and tries Clinger's fast path, a single multiplication or division of two numbers that are exact doubles; then the Eisel-Lemire algorithm, in Daniel Lemire's form (["Number Parsing at a Gigabyte per Second"](https://arxiv.org/abs/2101.11408), 2021), which multiplies the digits by a 128-bit approximation of the power of ten and decides the rounding unless the product is too close to a halfway point between two doubles to tell; and for those few, an exact comparison of the whole number with the halfway point, in big integers of about two kilobytes on the stack. A number of more than 19 digits is decided by Eisel-Lemire if its first 19, and the same plus one in the last place, round to the same double. Both use one table of 667 powers of ten, 10^-342 to 10^324, each its leading 128 bits, which _gen_table.py_ writes with exact integer arithmetic.

The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). _bench.c_ holds the benchmark, which also checks every one of the 2^32 floats with `-x`. The library's demo is the [fpinfo](demo/) program in _demo/_, which shows what decimal numbers become as doubles or floats: the shortest form, the exact value, the bits and the neighbours. [sof_json](../sof_json/) and [sof_toml](../sof_toml/) write and read their numbers with it, so their demos, [csvjson](../sof_json/demo/) and [tomljson](../sof_toml/demo/), use it too. All sof_real declarations are in _sof_real.h_. The table of powers of ten is in _sof_real_internal.h_, which _gen_table.py_ writes and only _sof_real.c_ and the tests include. The library counts leading zeros with [sof_bit](../sof_bit/) (_sof_bit.h_, in _../sof_bit/_, which is all of it), and needs nothing else but the c standard library. The benchmark measures with [sof_timer](../sof_timer/) in _../sof_timer/_ (_sof_timer.c_ and _sof_timer.h_), reads its options with [sof_copt](../sof_copt/) in _../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_), makes its data with [sof_random](../sof_random/) in _../sof_random/_ (_sof_random.c_ and _sof_random.h_) and allocates through [sof_alloc](../sof_alloc/) in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_); the library itself uses none of them.

    char text[SOF_REAL_FORMAT_SIZE];                         // room for any double, NUL included
    size_t n = sof_real_format(0.1 + 0.2, text, sizeof(text)); // "0.30000000000000004", n is 19
    sof_real_format_float(0.1f, text, sizeof(text));          // "0.1"

    double x;
    size_t used;
    sof_real_parse("6.02e23 mol", 11, &x, &used);              // 6.02e23, used is 7
    if (sof_real_parse(s, length, &x, &used) != SOF_REAL_OK || used != length) {
        ...                                                    // not a number, all of it, or too large
    }

    uint64_t digits;
    int exponent;
    sof_real_decimal(1500.0, &digits, &exponent);             // 15 and 2: 15e2
    sof_real_from_decimal(15, 2, 0, &x);                       // 1500.0

    char *room = sof_string_reserve(&s, SOF_REAL_FORMAT_SIZE); // straight into a sof_string_t
    sof_string_commit(&s, sof_real_format(x, room, SOF_REAL_FORMAT_SIZE));

## Building

    make             # compiles the library and builds ./test, ./test-portable, ./bench and the demo, demo/fpinfo
    make check       # builds and runs the tests, test and test-portable
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization
    ./bench          # runs the benchmark
    ./bench -x -j 8  # checks every float, on 8 threads
    python3 gen_table.py > sof_real_internal.h   # writes the table of powers of ten again

The tests check every entry of the table against powers of ten worked out by the test's own big integers. They format chosen doubles and floats to the exact text expected, with the infinities, NaNs, -0, the subnormals and the largest values, into buffers too small and just large enough; then every power of two and its neighbours, the smallest subnormals, the largest values below the smallest normal and below infinity, the doubles nearest every power of ten, 40,000 integers, 150,000 random doubles of random bits and of short decimals, 100,000 random floats and every 40,009th float. Each must read back, by the library and by `strtod()` or `strtof()`; no decimal with a digit fewer may read back; and if another of the same length does, the one `printf()` rounds to must be the one written or not read back, so the digits are the closest. They check the grammar, where reading stops and what isn't a number; the ends of the range, numbers of 10,000 digits and numbers with 9,000 zeros before their digits; 60,000 random texts of every shape, up to 1,000 digits, against `strtod()` and `strtof()`; 3,000 random doubles' exact halfway points, of up to 767 digits, which must round to the even neighbour, and the same a little above and below, which must round up and down; the halfway points of 30,000 random floats; the inverse functions both ways; **NULL** arguments; and four threads converting at once. The tests are built twice: _test_, with sof_real.c as usual, and _test-portable_, with `SOF_REAL_NO_BUILTINS`, so that the multiplications are done in 32-bit halves rather than with `unsigned __int128`, and sof_bit counts leading zeros without the compiler's builtins, as on compilers without them. The tests are linked with _-pthread_; the library itself doesn't use threads.

`./bench -x` checks every float, as its Benchmark section says. On the i9 below it took six minutes on 30 threads and found no failures; on the Mac, where the c library's conversions contend for a lock between threads and run many times slower, the same checks of every float's text, without the halfway points, took 38 minutes and found none. Nor did longer runs outside the tests find any: 300 million random doubles and 300 million from short decimals formatted as `printf()` and `strtod()` find shortest, 300 million random texts and 50 million halfway points parsed as glibc's `strtod()` parses them.

To use the library in another project, copy _sof_real.c_, _sof_real.h_ and _sof_real_internal.h_, with _sof_bit.h_ from _../sof_bit/_, into it and compile _sof_real.c_ with the project's own sources. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_ and _sof_real.c_ with _-pthread_.

## Benchmark

    bench [-h] [-n count] [-x] [-j threads]

- `-h`: print the usage message and exit.
- `-n` _count_: convert this many values in each measure (default 1,000,000).
- `-x`: measure nothing, but check every one of the 2^32 floats: that its text reads back as it, by `sof_real_parse_float()` and by `strtof()`; that no decimal of one digit fewer reads back; that if another of the same length does, the one `printf()` rounds to is the one written or doesn't read back; and for every positive finite float but the largest, that the exact decimal halfway to the next float up, and the doubles either side of it, read as `strtof()` and `strtod()` read them.
- `-j` _threads_: check on this many threads (default 8).

Each measure gives the nanoseconds one conversion takes, the best of 5 runs over a million values, with sof_real and with the c library, and how many times as long the c library takes. _random_ is doubles or floats of random bits, so mostly 16 or 17 digits with large exponents; _short_ is decimals of up to 6 digits, with up to 5 after the point, such as 12.5 or 0.0375; _25 digits_ is numbers of 25 random digits. Formatting is measured against `snprintf()` with `%.17g`, which always reads back but is rarely the shortest, and against the shortest `%g`, found by trying `%.15g`, `%.16g` and `%.17g` and reading each back with `strtod()`, as sof_json and sof_toml did before they used sof_real. Parsing is measured against `strtod()` and `strtof()`.

On an Apple M4 Max, with Apple clang 21, macOS:

| measure | sof_real | c library | c / sof_real |
|---|---|---|---|
| format random, against `%.17g` | 30.3 ns | 542.1 ns | 17.9 |
| format random, against the shortest `%g` | 30.1 ns | 1374.0 ns | 45.6 |
| format short, against `%.17g` | 30.3 ns | 205.0 ns | 6.8 |
| format short, against the shortest `%g` | 30.3 ns | 215.3 ns | 7.1 |
| format float, against `%.9g` | 26.5 ns | 119.6 ns | 4.5 |
| parse random, against `strtod()` | 27.7 ns | 27.8 ns | 1.0 |
| parse short, against `strtod()` | 19.5 ns | 17.5 ns | 0.9 |
| parse 25 digits, against `strtod()` | 33.3 ns | 37.0 ns | 1.1 |
| parse float, against `strtof()` | 26.0 ns | 22.4 ns | 0.9 |

On an Intel Core i9-13900K, with gcc 16.2, Arch Linux:

| measure | sof_real | c library | c / sof_real |
|---|---|---|---|
| format random, against `%.17g` | 25.7 ns | 265.7 ns | 10.3 |
| format random, against the shortest `%g` | 25.5 ns | 1043.0 ns | 40.8 |
| format short, against `%.17g` | 21.3 ns | 148.3 ns | 7.0 |
| format short, against the shortest `%g` | 21.2 ns | 190.0 ns | 8.9 |
| format float, against `%.9g` | 24.8 ns | 138.4 ns | 5.6 |
| parse random, against `strtod()` | 23.4 ns | 112.8 ns | 4.8 |
| parse short, against `strtod()` | 16.8 ns | 39.0 ns | 2.3 |
| parse 25 digits, against `strtod()` | 30.5 ns | 123.0 ns | 4.0 |
| parse float, against `strtof()` | 22.7 ns | 60.4 ns | 2.7 |

On an AMD Ryzen 9 5950X, with gcc 16.2, Arch Linux:

| measure | sof_real | c library | c / sof_real |
|---|---|---|---|
| format random, against `%.17g` | 32.5 ns | 356.5 ns | 11.0 |
| format random, against the shortest `%g` | 32.5 ns | 1267.4 ns | 39.0 |
| format short, against `%.17g` | 25.7 ns | 182.5 ns | 7.1 |
| format short, against the shortest `%g` | 25.8 ns | 255.4 ns | 9.9 |
| format float, against `%.9g` | 29.5 ns | 172.9 ns | 5.9 |
| parse random, against `strtod()` | 29.4 ns | 140.2 ns | 4.8 |
| parse short, against `strtod()` | 20.9 ns | 45.1 ns | 2.2 |
| parse 25 digits, against `strtod()` | 40.4 ns | 153.2 ns | 3.8 |
| parse float, against `strtof()` | 28.3 ns | 73.3 ns | 2.6 |

Formatting is 4 to 45 times as fast as the c library on all three, and parsing 2 to 5 times as fast as glibc's; macOS's `strtod()` and `strtof()` are about as fast as sof_real, but read the locale, and `strtod()` aborts on some long numbers (see Notation).

## Notation

`sof_real_format()` writes the digits of `sof_real_decimal()` as `printf()`'s `%g` would with the precision of their number, except that a number from 1e-4 up to, but not including, 1e17 is never written with an exponent: 0.0001, 0.1, 1.5, 100, 10000000000000000, 1e+17, 1.5e-07, 1e+100, 5e-324. An exponent has a sign and at least two digits, as `%g`'s has. A negative number, -0 included, starts with '-'. Infinities are written as `inf` and `-inf`, and every NaN, whatever its sign or payload, as `nan`; JSON, for one, has neither, so a caller writing JSON refuses them first. The longest text, such as -1.2345678901234567e-308, is 24 characters, so `SOF_REAL_FORMAT_SIZE`, 25, is always room enough. A float is written the same way, with its own shortest digits: 0.1f is 0.1, not the 0.10000000149011612 of the double it converts to.

This is the notation sof_json and sof_toml wrote before they used sof_real, by trying `%.15g`, `%.16g` and `%.17g` in turn, and the text is the same for every double but 46 powers of two, such as 2^-24, 2^-44 and 2^89. There the next double down is half as far away as the next up, so the rounding interval is lopsided, and the 16-digit decimal `printf()` rounds to falls outside it on the short side, while another 16-digit decimal, one further up, is inside: sof_real writes 5.960464477539063e-08 for 2^-24 where the old way wrote all 17 digits, 5.9604644775390625e-08. Both read back as 2^-24. The 46 are 2^n for n = -1017, -1007, -957, -921, -808, -791, -788, -778, -705, -695, -662, -652, -549, -509, -496, -489, -383, -366, -296, -140, -97, -77, -44, -24, 89, 122, 132, 172, 182, 275, 305, 345, 378, 398, 405, 481, 534, 544, 554, 574, 594, 710, 803, 863, 896 and 976, and their negatives.

`sof_real_parse()` reads this grammar from the start of the text:

    number   = [ sign ] mantissa [ exponent ]
    sign     = "+" / "-"
    mantissa = digits [ "." [ digits ] ] / "." digits
    exponent = ( "e" / "E" ) [ sign ] digits
    digits   = 1*( "0" - "9" )

so 1, -0.5, .5, 5., +1e10 and 1E-7 are numbers, and JSON's numbers, and TOML's without their underscores, are among them. There is no white space before the number, no hexadecimal form and no infinity or NaN, which a caller that wants them reads itself. Reading stops at the first byte that can't continue the number, and the text needn't end with a NUL: in 2em, the e that no digit follows is left unread, and the number is 2. `strtod()` and `strtof()` in the "C" locale give the same value for any text they both read in full. They depend on the locale's decimal point, and on macOS, `strtod()` fails an assertion and aborts on some long numbers, such as 98381695332488789278060386466380503979170576.22650378360388, which sof_real reads as glibc's `strtod()` does.

To write into a `sof_string_t`, `sof_string_reserve()` makes room for `SOF_REAL_FORMAT_SIZE` bytes and `sof_string_commit()` adds what `sof_real_format()` wrote, as the example above does, so sof_real needs no function of its own for that, and no dependency on sof_string.

## Functions

`sof_real_error_t sof_real_decimal(double value, uint64_t *digits, int *exponent)`

Find the shortest decimal that reads back as _value_: set _digits_ and _exponent_ so that _digits_ times 10^_exponent_ is, of the decimals that round to _value_, one with the fewest significant digits, at most 17, and of those the closest to _value_, and of two as close the even one. _digits_ has no trailing zeros, and _exponent_ is from -324 to 308. The sign isn't included: _digits_ is of the magnitude, and `signbit()` gives the sign. **0** and -0 give **0** and **0**. Returns **SOF_REAL_OK**, or **SOF_REAL_ERROR**, changing nothing, if _value_ is an infinity or a NaN, or _digits_ or _exponent_ is **NULL**.

`sof_real_error_t sof_real_decimal_float(float value, uint64_t *digits, int *exponent)`

As `sof_real_decimal()`, for a float: _digits_ has at most 9 significant digits, and _exponent_ is from -45 to 38.

`size_t sof_real_format(double value, char *buffer, size_t size)`

Write _value_ with the fewest digits that read back as it, `sof_real_decimal()`'s, in the notation above, as a NUL-terminated string in _buffer_, which has room for _size_ bytes. Returns the length of the text, not counting the NUL. If the text and its NUL don't fit in _size_ bytes, it returns **0** and _buffer_ holds an empty string, unless _size_ is **0**, when nothing is written; `SOF_REAL_FORMAT_SIZE` bytes always fit. A **NULL** _buffer_ returns **0**. Infinities are `inf` and `-inf`, and NaNs `nan`.

`size_t sof_real_format_float(float value, char *buffer, size_t size)`

As `sof_real_format()`, for a float, with `sof_real_decimal_float()`'s digits, at most 9.

`sof_real_error_t sof_real_from_decimal(uint64_t digits, int exponent, int negative, double *value)`

Set _value_ to the double nearest _digits_ times 10^_exponent_, negated if _negative_ isn't **0**, with ties to the double whose last bit is **0**. _digits_ is any 64-bit integer, and _exponent_ any int; _digits_ of **0** gives **0** or -0. It is the inverse of `sof_real_decimal()`: the digits and exponent that gives for a finite double convert back to it. Returns **SOF_REAL_OK**, including for a value too small for a double, which is **0** or a subnormal; **SOF_REAL_RANGE** if it is too large, with an infinity of its sign in _value_; or **SOF_REAL_ERROR** if _value_ is **NULL**.

`sof_real_error_t sof_real_from_decimal_float(uint64_t digits, int exponent, int negative, float *value)`

As `sof_real_from_decimal()`, rounding to the nearest float, once: never to a double first, which could round twice.

`sof_real_error_t sof_real_parse(const char *text, size_t length, double *value, size_t *used)`

Read a number by the grammar above from the start of _text_, reading no more than _length_ bytes, and set _value_ to the nearest double, with ties to the double whose last bit is **0**, however many digits the number has. If _used_ isn't **NULL**, it is set to the number of bytes read; a caller that wants all of the text to be the number checks that it is _length_. Returns **SOF_REAL_OK**, including for a number too small for a double, which is **0** or a subnormal of its sign; **SOF_REAL_RANGE** if it is too large, with an infinity of its sign in _value_; **SOF_REAL_NO_MATCH** if _text_ doesn't start with a number, with **0** in _value_ and _used_; or **SOF_REAL_ERROR**, changing nothing, if _value_ is **NULL**, or _text_ is **NULL** and _length_ isn't **0**.

`sof_real_error_t sof_real_parse_float(const char *text, size_t length, float *value, size_t *used)`

As `sof_real_parse()`, rounding to the nearest float, once, as `strtof()` does: never to a double first.

Every function that takes a pointer accepts **NULL**, as described for each: the formatting functions return **0**, and the others **SOF_REAL_ERROR**, changing nothing. There is no state to set up or free.

## Types

    typedef enum sof_real_error_t {
        SOF_REAL_OK = 0,     // no error
        SOF_REAL_NO_MEM,     // never returned: the library allocates nothing
        SOF_REAL_NO_MATCH,   // the text doesn't start with a number
        SOF_REAL_ERROR,      // a NULL argument, or a value with no digits: an infinity or a NaN
        SOF_REAL_RANGE,      // too large for the type: the value is an infinity of its sign
    } sof_real_error_t;

The error codes, in the order every library in the collection has, with **SOF_REAL_RANGE** after them. **SOF_REAL_NO_MEM** is there only so the codes match those of the other libraries.

## Threading

The library has no global state and does no locking, and doesn't read the locale, so any number of threads can call any of its functions at once, on separate buffers. Each function only reads its arguments and writes its results; two threads must not write to the same buffer or value at once.
