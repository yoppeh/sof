# hashsum

A file checksum tool in c: the 64-bit sof_hash of files or of each line, and a check of sums printed earlier.

`hashsum` works like `md5sum`. It prints the hash of each file, or of standard input, as 16 hex digits, two spaces and the file's name. With `-l` it prints the hash of each line instead, followed by the line, which finds duplicate lines or keys a table by line. With `-c` it reads lists of sums it printed earlier and checks each file named against its sum, printing `name: OK` or `name: FAILED`. `-s` hashes with a seed other than **0**, which gives unrelated sums of the same data, so a list made with a seed is checked with the same one.

_main.c_ is the whole program. The hashes come from the [sof_hash](../../sof_hash/) library, which is [rapidhash](https://github.com/Nicoshev/rapidhash): fast, and the same on every platform and compiler, so a list of sums made on one machine checks on any other. A file is read in chunks of 64 KiB, each given to a `sof_hash_stream_t`, so a file of any size, or a pipe, takes the same memory, and the hash is the one `sof_hash_bytes()` would give for the whole file at once. With `-l`, and for a list of sums, the file is read whole into a [sof_string](../../sof_string/) instead and split into lines there, each hashed with `sof_hash_bytes()`; the string is emptied and reused from one file to the next, so it grows only to the largest. A sum is not meant to resist someone making two files with the same one, as a cryptographic hash is; it catches files that have changed by accident.

The hashes come from the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_, _sof_hash.h_ and _rapidhash.h_, which is rapidhash, under its own MIT license). Lines are read into strings from the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), whose memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    sof_hash_stream_t stream;
    sof_hash_stream_init(&stream, 0);                  // seed 0
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        sof_hash_stream_update(&stream, chunk, got);   // a piece at a time
    }
    uint64_t sum = sof_hash_stream_final(&stream);     // as sof_hash_bytes() of the whole file
    printf("%016" PRIx64 "  %s\n", sum, path);         // as hashsum prints it

## Building

    make            # builds ./hashsum
    make debug=1    # builds with debug info and without optimization
    ./hashsum ../../test-data/*.txt                # the sums of the test data

`make` in the library's directory, _../_, builds it with the library, as do `make sof_hash` and `make` from the root of the project.

To build it in another project, copy _main.c_, with _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it.

## Usage

    hashsum [-h] [-c] [-l] [-s seed] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-c`: check sums instead of printing them. Each _file_ is a list of sums, as `hashsum` printed them: a line of 16 hex digits, in either case, two spaces and the name of a file, which is the rest of the line. Each file named is read and hashed, and `name: OK` printed if its hash is the sum, `name: FAILED` if it isn't, or `name: FAILED open or read` if it can't be read. A line that isn't a sum and a name is skipped and counted. Names are opened as they are written, so a list made with relative names is checked from the same directory. Can't be used with `-l`.
- `-l`: print the hash of each line, then the line, instead of the hash of each file. A line ends with a newline, which isn't hashed, and a carriage return before it isn't either, so a file with CRLF line endings hashes as the same file with LF would. The last line needn't end with a newline; an empty line has a hash of its own.
- `-s` _seed_: hash with _seed_, a decimal number or hex after `0x`, from **0** to 2^64 - 1 (default **0**). The sums are unrelated to those of another seed.
- _file_: the files to hash, or with `-c` the lists to check. With no _file_, or `-`, it reads standard input, whose name is printed as `-`.

A file is hashed as it is read, so its size doesn't matter; with `-l` each file is read whole into memory first, as is each list for `-c`. It exits with **0**; **1** if a file couldn't be read, memory ran out, or with `-c` any file failed or couldn't be read, or any line wasn't a sum, after a count of each on standard error; or **2** if the options are wrong.

For example, with the test data (see _../../test-data/README.md_):

    $ ./hashsum ../../test-data/words.txt ../../test-data/shakespeare.txt > data.sums
    $ cat data.sums
    58fd4a8d887948f5  ../../test-data/words.txt
    a52547362b7984d5  ../../test-data/shakespeare.txt
    $ ./hashsum -c data.sums
    ../../test-data/words.txt: OK
    ../../test-data/shakespeare.txt: OK

with a seed:

    $ ./hashsum -s 0x2a ../../test-data/words.txt
    7b6ac7285b5bc4e0  ../../test-data/words.txt

each line, the hash of _aa_ being that of the two bytes alone:

    $ head -4 ../../test-data/words.txt | ./hashsum -l
    7e0dbae22baf2ddb  aa
    3fa4c5352f2e3453  aah
    d939325d6b1ad71c  aahed
    82aaa17834567c5d  aahing
    $ printf aa | ./hashsum
    7e0dbae22baf2ddb  -

and a check that finds a changed file and a missing one:

    $ printf 'one\ntwo\n' > a.txt; printf 'three\n' > b.txt
    $ ./hashsum a.txt b.txt > sums
    $ printf 'four\n' >> b.txt; echo '0000000000000000  gone.txt' >> sums
    $ ./hashsum -c sums
    a.txt: OK
    b.txt: FAILED
    error opening gone.txt: No such file or directory
    gone.txt: FAILED open or read
    hashsum: 1 file couldn't be read
    hashsum: 1 sum didn't match
