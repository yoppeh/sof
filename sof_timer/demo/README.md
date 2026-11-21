# timeit

A timing table in c: how long several ways of reading, counting and hashing a file take.

`timeit` reads a file in calls of four sizes, counts its lines three ways, and hashes it, timing each job several times with a [sof_timer](../), and prints a table of the best, median and worst run of each, the rate of the best in megabytes a second, and what the job found, so that the jobs that should agree can be seen to. Before the table it prints what reading the clock costs and the smallest step in which the clock advances, which say how far the shortest times can be trusted.

_main.c_ is the whole program. One `sof_timer_t` times every run: `sof_timer_init()` sets it up and `sof_timer_calibrate()` measures the cost of reading the clock, which the timer then subtracts from each measurement. Each job is run once untimed, so that the file is in the system's cache and the code in the processor's, and then as many times as `-r` asks, between `sof_timer_start()` and `sof_timer_stop()`, with `sof_timer_elapsed_ns()` giving each run's time. The times are sorted with [sof_sort](../../sof_sort/) to find the best, median and worst, and printed in nanoseconds, microseconds, milliseconds or seconds, whichever suits each. The clock's resolution is measured with a second timer, left uncalibrated and running: `sof_timer_elapsed_ns()` is read until its value has changed 1,000 times, and the smallest change is the step. `sof_timer_free()` leaves the timers empty at the end.

The jobs:

- `read, N calls`: opens the file, turns off stdio's buffering, so each `fread()` is a read from the system, and reads it to its end in calls of 512 bytes, 8 KiB, 64 KiB and 1 MiB. The result is the bytes read.
- `lines, getc`: opens the file and counts its newlines, reading a byte at a time with `getc()` through stdio's buffer.
- `lines, byte loop` and `lines, memchr`: count the newlines in a copy of the file read into memory at the start, looking at each byte in turn, or with `memchr()`.
- `hash, sof_hash_bytes`: hashes that copy with [sof_hash](../../sof_hash/)'s `sof_hash_bytes()`, with a seed of **0**. The result is the hash, in hexadecimal.

Each result is stored in a `volatile` variable, so the compiler can't leave the work out.

The timer is the [sof_timer](../) library in _../_ (_sof_timer.c_ and _sof_timer.h_). The times are sorted with the [sof_sort](../../sof_sort/) library in _../../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_), the file is hashed with the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_, _sof_hash.h_ and _rapidhash.h_, which is [rapidhash](https://github.com/Nicoshev/rapidhash), under its own MIT license), and memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library and, through sof_timer, the POSIX `clock_gettime()`.

    sof_timer_t t;
    sof_timer_init(&t);
    sof_timer_calibrate(&t);                    // t.overhead: the cost of reading the clock
    for (size_t i = 0; i < runs; i++) {
        sof_timer_start(&t);
        job(&in, &result);                      // the work to time
        sof_timer_stop(&t);
        times[i] = sof_timer_elapsed_ns(&t);    // less t.overhead
    }
    sof_sort_insertion(times, runs, sizeof(uint64_t), compare_ns, NULL);  // best first
    sof_timer_free(&t);

## Building

    make            # builds ./timeit
    make debug=1    # builds with debug info and without optimization
    ./timeit ../../test-data/shakespeare.txt        # the table for 5.6 MB of text
    ./timeit -r 25 ../../test-data/words.txt        # 25 runs of each job

`make` in the library's directory, _../_, builds it with the library, as do `make sof_timer` and `make` from the root of the project.

To build it in another project, copy _main.c_, with _sof_timer.c_ and _sof_timer.h_ from _../_, _sof_sort.c_ and _sof_sort.h_ from _../../sof_sort/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it.

## Usage

    timeit [-h] [-r runs] file

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-r` _runs_: time each job _runs_ times, from **1** to **1000**; **9** if it isn't given. The median of an even number of runs is the mean of the middle two.
- _file_: the file to work on. It is read many times, so it must be a file, not standard input.

The table goes to standard output. `MB/s` is the file's size in millions of bytes divided by the best time, or `-` if that time is **0**. A time of **0** means the job finished within the clock's cost of reading, which is subtracted from each run; the step the clock advances in is the shortest time that can be told from **0**, so times of a few steps are rough. It exits with **0**, with **1** if the file can't be read, a job fails or memory runs out, and with **2** if the options are wrong.

For example, on an Apple M4 Max under macOS, built with Apple clang 21, whose clock advances a microsecond at a time, so fast that two readings in a row usually show the same time:

    $ ./timeit ../../test-data/shakespeare.txt
    file:  ../../test-data/shakespeare.txt, 5638481 bytes
    clock: 0 ns to read (subtracted from each run), advances in steps of 1.00 us
    runs:  9 of each job, after one untimed

    job                          best     median      worst     MB/s  result
    read, 512 B calls         3.54 ms    3.66 ms    4.37 ms     1592  5638481
    read, 8 KiB calls       343.00 us  360.00 us  372.00 us    16439  5638481
    read, 64 KiB calls      151.00 us  163.00 us  177.00 us    37341  5638481
    read, 1 MiB calls       118.00 us  125.00 us  137.00 us    47784  5638481
    lines, getc              62.96 ms   63.80 ms   66.07 ms       90  196398
    lines, byte loop        608.00 us  615.00 us  633.00 us     9274  196398
    lines, memchr             1.73 ms    1.73 ms    1.82 ms     3254  196398
    hash, sof_hash_bytes    78.00 us   83.00 us   98.00 us    72288  a52547362b7984d5

Reading in small calls costs a system call each, and `getc()` locks the stream for each byte on macOS. The lines of Shakespeare are short, so `memchr()` is called every few dozen bytes, and the cost of each call makes it slower than the plain loop.
