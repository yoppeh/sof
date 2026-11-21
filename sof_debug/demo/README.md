# hanoi

A Towers of Hanoi solver in c: every move for a tower of up to 64 disks, with its recursion traced, call by call, in a debug build.

`hanoi` moves a tower of disks, 3 unless another number is given, from peg A to peg C, one disk at a time and never onto a smaller disk, and prints each move. With `-p` it prints the pegs at the start and after each move, and with `-c` only how many moves there are, 2^_n_ - 1 for _n_ disks, without making them. Built with `make debug=1`, it traces its recursion to standard error with [sof_debug](../../sof_debug/): each call, indented by how deep it is, with the disks it is to move, between the moves it prints.

_main.c_ is the whole program. `move_tower()` moves a tower in three steps: all but its bottom disk to the spare peg, by calling itself, the bottom disk to the peg it is going to, and the rest from the spare peg onto it, by calling itself again; a tower of one disk is a single move. Each call starts with `sof_debug_enter()`, which writes its name and goes a level deeper, writes the disks it is to move with `sof_debug()`, and returns through `sof_debug_return()`, which comes back a level. In an ordinary build those compile to nothing, and `sof_debug_return()` to `return`. The pegs are [sof_stack](../../sof_stack/) stacks of disks: each move pops a disk from one and pushes it on another, after `sof_stack_peek()` has checked that it isn't going onto a smaller disk.

Tracing is done by the [sof_debug](../../sof_debug/) library in _../../sof_debug/_ (_sof_debug.c_ and _sof_debug.h_), and the pegs are stacks from the [sof_stack](../../sof_stack/) library in _../../sof_stack/_ (_sof_stack.c_ and _sof_stack.h_), whose memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    static int move_tower(towers_t *t, unsigned disks, int from, int to, int via) {
        sof_debug_enter();                               // "main.c:110: move_tower()", then a level deeper
        sof_debug("%u disks from %c to %c, by %c\n", ...);
        if (disks == 1) {
            sof_debug_return(move_disk(t, from, to));    // back a level, and return
        }
        int rc = move_tower(t, disks - 1, from, via, to); // traced a level deeper
        ...
        sof_debug_return(rc);
    }

## Building

    make            # builds ./hanoi
    make debug=1    # builds with debug info, without optimization, tracing to stderr
    ./hanoi 4       # the 15 moves for 4 disks
    ./hanoi -p 3    # the 7 moves for 3 disks, with the pegs after each

`make` in the library's directory, _../_, builds it with the library, as do `make sof_debug` and `make` from the root of the project. A program built with `make debug=1` traces whenever it runs; run `make clean` before building again without it.

To build it in another project, copy _main.c_, with _sof_debug.c_ and _sof_debug.h_ from _../../sof_debug/_, _sof_stack.c_ and _sof_stack.h_ from _../../sof_stack/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it, and compile with `-D DEBUG` to trace.

## Usage

    hanoi [-h] [-c] [-p] [disks]

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-c`: print only the number of moves, 2^_disks_ - 1, without making them.
- `-p`: print the pegs at the start and after each move, each with its disks from the bottom up, numbered by size from 1, the smallest.
- _disks_: the number of disks, from 0 to 64 (default 3). A tower of _n_ disks takes 2^_n_ - 1 moves, so 20 disks take a million, and 64 more than the program could print in a lifetime.

Each move is printed as its number, the disk and the pegs it moves from and to. It exits with **0**, or **2** if the options are wrong or memory runs out.

For example:

    $ ./hanoi
    1: disk 1 from A to C
    2: disk 2 from A to B
    3: disk 1 from C to B
    4: disk 3 from A to C
    5: disk 1 from B to A
    6: disk 2 from B to C
    7: disk 1 from A to C

    $ ./hanoi -p 2
        A: 2 1    B:    C:
    1: disk 1 from A to B
        A: 2    B: 1    C:
    2: disk 2 from A to C
        A:    B: 1    C: 2
    3: disk 1 from B to C
        A:    B:    C: 2 1

    $ ./hanoi -c 64
    18446744073709551615

Built with `make debug=1`, each call of `move_tower()` is traced, indented by its depth, between the moves printed on standard output. In a debug build standard output is line-buffered, so the two keep their order when they go to one place:

    $ ./hanoi 2>&1
    main.c:110: move_tower()
        main.c:111: 3 disks from A to C, by B
        main.c:110: move_tower()
            main.c:111: 2 disks from A to B, by C
            main.c:110: move_tower()
                main.c:111: 1 disk from A to C, by B
    1: disk 1 from A to C
    2: disk 2 from A to B
            main.c:110: move_tower()
                main.c:111: 1 disk from C to B, by A
    3: disk 1 from C to B
    4: disk 3 from A to C
        main.c:110: move_tower()
            main.c:111: 2 disks from B to C, by A
            main.c:110: move_tower()
                main.c:111: 1 disk from B to A, by C
    5: disk 1 from B to A
    6: disk 2 from B to C
            main.c:110: move_tower()
                main.c:111: 1 disk from A to C, by B
    7: disk 1 from A to C

and the trace alone:

    $ ./hanoi 2 > /dev/null
    main.c:110: move_tower()
        main.c:111: 2 disks from A to C, by B
        main.c:110: move_tower()
            main.c:111: 1 disk from A to B, by C
        main.c:110: move_tower()
            main.c:111: 1 disk from B to C, by A
