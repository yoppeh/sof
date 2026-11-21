# fpinfo

A floating-point explainer in c: what a decimal number becomes as a double or a float.

For each number on the command line, or each line of standard input, `fpinfo` prints the double the number reads as, written with the fewest digits that read back as it; its bits, with the sign, exponent and fraction fields; its exact value in decimal, every digit of it, and whether that is the number given or the number was rounded; the doubles either side of it; and the gap to the next one away from 0. With `-f` it does the same for floats. With `-s` it prints only the shortest form of each number, a line each, which tidies a column of numbers written with too many digits, such as 0.30000000000000004 written as 0.3000000000000000444, without changing any value.

_main.c_ is the whole program. Numbers are read with `sof_real_parse()` or `sof_real_parse_float()`, correctly rounded however many digits they have, and written with `sof_real_format()` or `sof_real_format_float()`, from the [sof_real](../) library in _../_ (_sof_real.c_, _sof_real.h_ and _sof_real_internal.h_, with _sof_bit.h_ from [sof_bit](../../sof_bit/) in _../../sof_bit/_). The exact value is worked out in big integers of the program's own: a double is m 2^e, which for a negative e is m 5^-e 10^e, so its digits are those of the integer m 5^-e. Lines are read into a string, and the exact value built in one, from the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), which allocates through [sof_alloc](../../sof_alloc/) in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    double x;
    size_t used;
    char text[SOF_REAL_FORMAT_SIZE];
    sof_real_parse(arg, strlen(arg), &x, &used);   // the nearest double; used is strlen(arg) for a number
    sof_real_format(x, text, sizeof(text));         // the fewest digits that read back as it

## Building

    make            # builds ./fpinfo
    make debug=1    # builds with debug info and without optimization
    ./fpinfo 0.1    # what 0.1 becomes

`make` in the library's directory, _../_, builds it with the library, as do `make sof_real` and `make` from the root of the project.

To build it in another project, copy _main.c_, with _sof_real.c_, _sof_real.h_ and _sof_real_internal.h_ from _../_, _sof_bit.h_ from _../../sof_bit/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it.

## Usage

    fpinfo [-h] [-f] [-s] [number]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-f`: convert to floats rather than doubles.
- `-s`: print only the shortest form of each number, one a line.
- _number_: the numbers, by `sof_real_parse()`'s grammar: an optional sign, digits with an optional point, and an optional exponent, such as 12, -0.5, .5, 1e-7 or 6.02E23. A negative number on the command line comes after `--`, which ends the options, so it isn't taken for one. With no _number_, each line of standard input is one, without the spaces and tabs around it; blank lines are skipped.

A number too large for the type is shown as `inf`, marked as too large; one too small is 0 or a subnormal, marked as such by its exponent. It exits with **0**; **1** if a number couldn't be read, after saying which on standard error; or **2** if the options are wrong.

For example:

    $ ./fpinfo 0.1 1e23
    0.1
        double     0.1
        bits       0x3fb999999999999a: sign 0, exponent -4, fraction 0x999999999999a
        exact      0.1000000000000000055511151231257827021181583404541015625 (rounded)
        neighbours 0.09999999999999999 and 0.10000000000000002
        gap        1.3877787807814457e-17
    1e23
        double     1e+23
        bits       0x44b52d02c7e14af6: sign 0, exponent 76, fraction 0x52d02c7e14af6
        exact      99999999999999991611392 (rounded)
        neighbours 9.999999999999997e+22 and 1.0000000000000001e+23
        gap        16777216

floats, where 2^24 + 1 is the first integer that isn't one:

    $ ./fpinfo -f 16777217
    16777217
        float      16777216
        bits       0x4b800000: sign 0, exponent 24, fraction 0x0
        exact      16777216 (rounded)
        neighbours 16777215 and 16777218
        gap        2

a negative number, after `--`:

    $ ./fpinfo -- -0.5
    -0.5
        double     -0.5
        bits       0xbfe0000000000000: sign 1, exponent -1, fraction 0x0
        exact      -0.5 (the number given)
        neighbours -0.49999999999999994 and -0.5000000000000001
        gap        1.1102230246251565e-16

and a column tidied:

    $ printf '3.14159\n0.30000000000000004\n100.000\n1e-7\n' | ./fpinfo -s
    3.14159
    0.30000000000000004
    100
    1e-07
