# tail

A tail in c: the last lines, or bytes, of files or standard input.

`tail` reads each file to its end and prints its last 10 lines, or as many as `-n` asks for, or with `-c` its last bytes. It reads a file once, front to back, and holds only the lines it may print, so it works the same on a pipe as on a file, and in as little memory as the lines it prints take. With more than one file, each one's lines are headed `==> name <==`, as the standard tail heads them.

_main.c_ is the whole program. The lines are kept in a [sof_ring](../) of `sof_string_t`, whose capacity is the number of lines to print. Each line is gathered in a `sof_string_t`, newline included, and pushed with `sof_ring_push_overwrite()`: the ring takes the string over, and a new one is started for the next line. Once the ring is full, each push replaces the oldest line, which goes to the ring's delete function, where its string is freed with `sof_string_free()`. At the end of a file the ring holds the last lines, oldest first, and `sof_ring_traverse()` prints them; `sof_ring_clear()` then frees them through the delete function, ready for the next file, and `sof_ring_free()` releases the ring at the end. With `-c`, the ring holds one byte to an element and has no delete function; the bytes are printed with `sof_ring_peek()` and `sof_ring_next()`.

The ring is the [sof_ring](../) library in _../_ (_sof_ring.c_ and _sof_ring.h_), and the lines are strings from the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_). Both allocate through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    sof_ring_t ring;
    sof_ring_init(&ring, sizeof(sof_string_t), 10, free_line, NULL, NULL);   // free_line() calls sof_string_free()
    while (read_line(f, &line)) {                   // a sof_string_t the ring takes over
        sof_ring_push_overwrite(&ring, &line);      // frees the oldest line once there are 10
    }
    sof_ring_traverse(&ring, print_line, NULL);     // the last 10 lines, oldest first
    sof_ring_free(&ring);                           // frees the lines it holds

## Building

    make            # builds ./tail
    make debug=1    # builds with debug info and without optimization
    ./tail ../../test-data/words.txt          # the last 10 words
    ./tail -c 100 ../../test-data/words.txt   # the last 100 bytes

`make` in the library's directory, _../_, builds it with the library, as do `make sof_ring` and `make` from the root of the project.

To build it in another project, copy _main.c_, with _sof_ring.c_ and _sof_ring.h_ from _../_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it.

## Usage

    tail [-h] [-q] [-n lines | -c bytes] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-q`: never print the `==> name <==` headers, so the output of several files runs together.
- `-n` _lines_: print the last _lines_ lines of each file; **10** if neither `-n` nor `-c` is given. A line is everything up to and including a newline, and the end of a file that doesn't end with one is a last line, printed without one. **0** prints nothing, and doesn't read the files.
- `-c` _bytes_: print the last _bytes_ bytes of each file instead of lines. It can't be given with `-n`.
- _file_: the input. With no _file_, or `-`, it reads standard input. With several, each one's output is headed `==> name <==`, with a blank line before each header but the first, and standard input is named `standard input`.

Bytes are printed as they were read, so carriage returns, NULs and invalid UTF-8 come through unchanged. It exits with **0**, with **1** if a file couldn't be opened or read, or memory ran out, after printing why and going on to the next file, and with **2** if the options are wrong.

For example:

    $ ./tail -n 4 ../../test-data/words.txt
    zythums
    zyzzyva
    zyzzyvas
    zzz

    $ printf 'one\ntwo\nthree\nfour\n' | ./tail -n 2 - ../../test-data/words.txt
    ==> standard input <==
    three
    four

    ==> ../../test-data/words.txt <==
    zyzzyvas
    zzz

    $ ./tail -c 9 ../../test-data/words.txt
    yvas
    zzz
