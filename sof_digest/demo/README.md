# digestsum

A file checksum tool in c: the SHA-256 digests of files, or their MD5, SHA-1, other SHA-2 digests or CRCs, and a check of digests printed earlier, in the formats of GNU's and BSD's tools.

`digestsum` works like GNU's `sha256sum`. It prints the SHA-256 digest of each file, or of standard input, as 64 hex digits, two spaces and the file's name, or with `--tag` as `SHA256 (name) = digest`, which is BSD's format and GNU's with `--tag`. With `-a` it prints another of the library's digests instead, as `md5sum`, `sha1sum`, `sha512sum` and the others do, or a CRC-32 or CRC-32C as 8 hex digits. With `-c` it reads lists of digests in either format and checks each file named against its digest, printing `name: OK` or `name: FAILED`; a line in the tag format says which digest it holds, so one list can mix them. The lists it prints check with GNU's `sha256sum -c`, `md5sum -c` and the rest, and `shasum -c`, and theirs with it. MD5 and SHA-1 are broken, and fit only for matching lists other tools made; the CRCs only catch accidents. Unlike [hashsum](../../sof_hash/demo/), which uses the much faster sof_hash, its digests are cryptographic: no one knows how to make a file with the same digest as another, so a digest published apart from a file shows it hasn't been tampered with.

_main.c_ is the whole program. The digests come from the [sof_digest](../../sof_digest/) library. A file is read in chunks of 64 KiB, each given to a stream, such as a `sof_digest_sha256_t`, kept in a union of every digest's stream type, so a file of any size, or a pipe, takes the same memory, and the digest is the one `sof_digest_sha256()` would give for the whole file at once. A CRC is written as its 32-bit value, high byte first. `sof_digest_to_hex()` writes it as it is printed, and `sof_digest_from_hex()` reads it back from a list. For `-c`, each list is read whole into a [sof_string](../../sof_string/) and split into lines there, and each name is copied into another, which `fopen()` needs ending with a NUL; both strings are emptied and reused from one file to the next, so they grow only to the largest.

The digests come from the [sof_digest](../../sof_digest/) library in _../../sof_digest/_ (_sof_digest.c_, _sof_digest.h_ and _sof_digest_internal.h_). Lists are read into strings from the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), whose memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    sof_digest_sha256_t sha;
    sof_digest_sha256_init(&sha);
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        sof_digest_sha256_update(&sha, chunk, got);        // a piece at a time
    }
    sof_digest_sha256_final(&sha, digest);                 // as sof_digest_sha256() of the whole file
    printf("%s  %s\n", sof_digest_to_hex(digest, sizeof(digest), hex), path);

## Building

    make            # builds ./digestsum
    make debug=1    # builds with debug info and without optimization
    ./digestsum ../../test-data/*.txt                # the digests of the test data

`make` in the library's directory, _../_, builds it with the library, as do `make sof_digest` and `make` from the root of the project.

To build it in another project, copy _main.c_, with _sof_digest.c_, _sof_digest.h_ and _sof_digest_internal.h_ from _../../sof_digest/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it.

## Usage

    digestsum [-h] [-a algorithm] [-c] [--quiet] [--status] [--tag] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-a` _algorithm_: the digest to print, or to check lines without a tag with: `md5`, `sha1`, `sha224`, `sha256` (the default), `sha384`, `sha512`, `sha512-256`, `crc32` or `crc32c`. Their tags are `MD5`, `SHA1`, `SHA224`, `SHA256`, `SHA384`, `SHA512`, `SHA512/256`, `CRC32` and `CRC32C`.
- `-c`: check digests instead of printing them. Each _file_ is a list of digests, one to a line: the digest's hex digits (64 for SHA-256), in either case, two spaces, or a space and a `*` as GNU's tool writes in binary mode, and the name of a file, which is the rest of the line; or a tag, ` (`, the name, `) = ` and the digits. Lines of both formats can be mixed, and a line with a tag is checked with the digest the tag names, whatever `-a` says. Each file named is read and digested, and `name: OK` printed if its digest matches, `name: FAILED` if it doesn't, or `name: FAILED open or read` if it can't be read. A line that is neither format is skipped and counted. Names are opened as they are written, so a list made with relative names is checked from the same directory. Can't be used with `--tag`.
- `--quiet`: with `-c`, don't print `OK` lines, only failures.
- `--status`: with `-c`, print nothing, not even the counts of failures; the exit status says whether every file checked.
- `--tag`: print each digest as `SHA256 (name) = digest`, with the digest's tag, instead of `digest  name`.
- _file_: the files to digest, or with `-c` the lists to check. With no _file_, or `-`, it reads standard input, whose name is printed as `-`.

A name holding a backslash, a newline or a carriage return is written with them as `\\`, `\n` and `\r`, after a backslash at the start of the line, as GNU's tool writes it, and `-c` reads such lines back. A list's lines may end with a carriage return and a newline. It exits with **0**; **1** if a file couldn't be read, memory ran out, or with `-c` any file failed or couldn't be read, or any line wasn't a digest and a name, after a count of each on standard error; or **2** if the options are wrong.

For example, with the test data (see _../../test-data/README.md_):

    $ ./digestsum ../../test-data/words.txt > data.sums
    $ cat data.sums
    f76ea46bec1132114abbd1ec62112f5f0450a52dec57a0de6c02fbfbd7952833  ../../test-data/words.txt
    $ ./digestsum -c data.sums
    ../../test-data/words.txt: OK
    $ shasum -a 256 -c data.sums
    ../../test-data/words.txt: OK

in BSD's format:

    $ printf abc | ./digestsum --tag
    SHA256 (-) = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad

another digest, and a list that mixes them:

    $ printf abc | ./digestsum -a md5
    900150983cd24fb0d6963f7d28e17f72  -
    $ ./digestsum -a crc32 --tag ../../test-data/words.txt > mixed.sums
    $ ./digestsum -a sha512-256 --tag ../../test-data/words.txt >> mixed.sums
    $ ./digestsum -c mixed.sums
    ../../test-data/words.txt: OK
    ../../test-data/words.txt: OK

and a check that finds a changed file and a missing one:

    $ printf 'one\ntwo\n' > a.txt; printf 'three\n' > b.txt
    $ ./digestsum a.txt b.txt > sums
    $ printf 'four\n' >> b.txt; echo "SHA256 (gone.txt) = $(printf %064d 0)" >> sums
    $ ./digestsum -c sums
    a.txt: OK
    b.txt: FAILED
    error opening gone.txt: No such file or directory
    gone.txt: FAILED open or read
    digestsum: 1 file couldn't be read
    digestsum: 1 digest didn't match
