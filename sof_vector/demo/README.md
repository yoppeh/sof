# numstat

A summary of a list of numbers in c: count, sum, mean, standard deviation, minimum, median, maximum and percentiles.

`numstat` reads numbers separated by whitespace, such as one to a line or a column of them, from files or standard input, and prints their count, sum, mean, standard deviation, minimum, median, maximum and the percentiles asked for. With `-s` it prints the numbers sorted instead, and with `-u` each distinct number and how often it occurs. `-f` says whether a number occurs, where it first does in the input, and at which ranks, or if it doesn't, which numbers it falls between.

_main.c_ is the whole program. The numbers are pushed, as they are read, onto a [sof_vector](../) of `double` with `sof_vector_push()`, which doubles its array when it fills. They are kept in that order for `-f`, and copied into a second vector, sized with `sof_vector_resize()`, which `sof_vector_sort()` sorts. The minimum and maximum are then its first and last elements, and the median and percentiles are read from it with `sof_vector_at()`, interpolating between the two numbers around each rank. The sum and the deviations are added up in a loop of `sof_vector_next()`, and `-s` prints the sorted vector with `sof_vector_traverse()`. Each number given with `-f` is looked for in the input with `sof_vector_find()`, which looks at each element in turn and gives the index of the first equal one, and in the sorted vector with `sof_vector_search()`, a binary search whose index, the first element not less than the number, is where its run of equal numbers starts or where it would go. The percentiles asked for with `-p` are kept in a third vector, sorted and each once: `sof_vector_search()` finds where each goes, and `sof_vector_insert()` puts it there. `sof_vector_free()` releases the vectors.

The vectors are the [sof_vector](../) library in _../_ (_sof_vector.c_ and _sof_vector.h_), which sorts with the [sof_sort](../../sof_sort/) library in _../../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_) and allocates through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library, with its maths library for `sqrt()` and `nextafter()`.

    sof_vector_t v;
    sof_vector_init(&v, sizeof(double), 0, NULL, NULL, NULL);    // no capacity yet, delete function, ctx or allocator
    while (read_number(f, &x)) {
        sof_vector_push(&v, &x);                                 // grows as needed
    }
    sof_vector_sort(&v, compare_doubles, NULL);                  // smallest first
    double min = *(double *)sof_vector_at(&v, 0);
    size_t index;
    if (sof_vector_search(&v, &key, compare_doubles, NULL, &index) != NULL) {
        ...                                                      // key is at index, and maybe after it
    }
    sof_vector_free(&v);

## Building

    make            # builds ./numstat
    make debug=1    # builds with debug info and without optimization
    seq 1 1000 | ./numstat                                       # 1 to 1000
    awk '{print length($0)}' ../../test-data/words.txt | ./numstat -u   # how many words of each length

`make` in the library's directory, _../_, builds it with the library, as do `make sof_vector` and `make` from the root of the project.

To build it in another project, copy _main.c_, with _sof_vector.c_ and _sof_vector.h_ from _../_, _sof_sort.c_ and _sof_sort.h_ from _../../sof_sort/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it, and link it with the maths library (_-lm_).

## Usage

    numstat [-h] [-s | -u] [-p list] [-f value]... [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-s`: print the numbers sorted, smallest first, one to a line, instead of the summary.
- `-u`: print each distinct number, smallest first, a tab and how many times it occurs, instead of the summary.
- `-p` _list_: the percentiles to print, from **0** to **100**, separated by commas, such as `50,90,99.9`; **25,75,90,99** if it isn't given. They are printed in order, each once, whatever order they are given in, and `-p` can be given more than once to add more. Percentile _p_ of _n_ numbers is the number at rank _p_ / 100 × (_n_ − 1), counting from **0** in the sorted numbers, interpolated between the two numbers around it when the rank isn't whole, so **0** is the minimum, **50** the median and **100** the maximum.
- `-f` _value_: after the rest of the output, say whether _value_ occurs: how many times, which number of the input it first is, counting from **1**, and its ranks among the sorted numbers, from **1**; or, if it doesn't occur, the numbers it falls between. Can be given more than once.
- _file_: the input. With no _file_, or `-`, it reads standard input. With several, they are read in turn, as if they were one.

A number is anything `strtod()` reads whole, such as `42`, `-3.5`, `1e6` or `0x1p4`, but not an infinity or a NaN. Anything else between whitespace is reported on standard error, with its file and line, and skipped. Numbers are printed with up to 12 significant digits. The standard deviation is the sample's, dividing by _n_ − 1, and is left out when there is only one number; with none, only the count is printed. It exits with **0**, with **1** if a file couldn't be read, held something that isn't a number, or memory ran out, and with **2** if the options are wrong.

For example, the lengths of the 263,533 words in _../../test-data/words.txt_:

    $ awk '{print length($0)}' ../../test-data/words.txt | ./numstat -f 15 -f 40
    count   263533
    sum     2452231
    mean    9.30521414775
    stddev  2.83510870068
    min     2
    median  9
    max     45
    p25     7
    p75     11
    p90     13
    p99     17
    15: found 5427 times, first as number 268 of the input, ranks 251171 to 256597 of 263533
    40: not found, above 32 and below 45

    $ awk '{print length($0)}' ../../test-data/words.txt | ./numstat -u | tail -n 4
    30	1
    31	1
    32	1
    45	1

and with something that isn't a number:

    $ printf '3 1 4 1 5\n9 2 6 x 5 3\n5\n' | ./numstat -p 10,50,99.9 -f 5
    numstat: standard input:2: not a number: "x"
    count   11
    sum     44
    mean    4
    stddev  2.36643191324
    min     1
    median  4
    max     9
    p10     1
    p50     4
    p99.9   8.97
    5: found 3 times, first as number 5 of the input, ranks 7 to 9 of 11
