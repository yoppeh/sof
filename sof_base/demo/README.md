# basenc

A base16, base32 and base64 encoder and decoder in c: files encoded as text and back, as GNU's `basenc` does.

`basenc` encodes a file, or standard input, in base64 or another encoding of RFC 4648, writing the text in lines of 76 characters, as MIME does, or with `-d` decodes it back. Its encoded output is byte for byte that of GNU's `basenc` with the same encoding, and it decodes GNU's. Decoding skips whitespace, such as those line breaks, unless `--strict` is given, and otherwise accepts only what encoding with the same options writes: padded or, with `--no-pad`, not, with nothing after the padding and 0 in the bits left over at the end. `--lenient` lets padding be missing and leftover bits be anything.

_main.c_ is the whole program. The encoding comes from the [sof_base](../../sof_base/) library. Input is read in chunks of 64 KiB, each given to a `sof_base_encoder_t` or a `sof_base_decoder_t`, so input of any size, or a pipe, takes the same memory, and the output is what `sof_base_encode()` or `sof_base_decode()` would give for the whole input at once. A stream holds the few bytes or characters of a group that a chunk splits until the next one completes it. Encoded text is broken into lines as it is written, and a decoding error reports the offset of the bad character in the whole input, which the decoder counts across chunks.

The encoding comes from the [sof_base](../../sof_base/) library in _../../sof_base/_ (_sof_base.c_ and _sof_base.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    sof_base_encoder_t encoder;
    sof_base_encoder_init(&encoder, SOF_BASE_64, 0);
    while ((got = fread(chunk, 1, sizeof(chunk), in)) > 0) {
        sof_base_encoder_update(&encoder, chunk, got, out, sizeof(out), &written);   // whole groups
        write_text(out, written, wrap, &column);
    }
    sof_base_encoder_final(&encoder, out, sizeof(out), &written);                      // the last group, padded

## Building

    make            # builds ./basenc
    make debug=1    # builds with debug info and without optimization
    ./basenc ../../test-data/README.md | ./basenc -d    # a round trip

`make` in the library's directory, _../_, builds it with the library, as do `make sof_base` and `make` from the root of the project.

To build it in another project, copy _main.c_, with _sof_base.c_ and _sof_base.h_ from _../../sof_base/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it.

## Usage

    basenc [-h] [-d] [-w cols] [--base16 | --base32 | --base32hex | --base64 | --base64url]
           [--lower] [--no-pad] [--lenient] [--strict] [file]

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-d`: decode instead of encoding.
- `-w` _cols_: when encoding, break the text into lines of _cols_ characters, ending the last with a newline; **0** writes it all on one line, with no newline. 76 if not given.
- `--base16`, `--base32`, `--base32hex`, `--base64`, `--base64url`: the encoding; base64 if none is given. Only one may be.
- `--lower`: encode base16, base32 and base32hex in lower case. Decoding takes either case anyway.
- `--no-pad`: encode without the `=` that pad the last group, and when decoding, refuse them (unless `--lenient` is given too).
- `--lenient`: decode text whose padding is missing or short, or whose last character's leftover bits aren't 0.
- `--strict`: decode refusing whitespace, line breaks included, so the text must be on one line with nothing after it, as `-w 0` writes it.
- _file_: the file to encode or decode. With none, or `-`, it reads standard input.

`-w` and `--lower` are for encoding, and `--lenient` and `--strict` for decoding. It exits with **0**; **1** if the input couldn't be read or the output written, or the text to decode isn't valid, after saying where on standard error; or **2** if the options are wrong. Bytes decoded before a bad character have been written by then.

For example:

    $ printf foobar | ./basenc
    Zm9vYmFy
    $ printf foobar | ./basenc --base32 --lower --no-pad
    mzxw6ytboi
    $ printf foobar | ./basenc --base16
    666F6F626172
    $ printf 'Zm9v\nYmFy\n' | ./basenc -d
    foobar
    $ printf 'Zm9vYg' | ./basenc -d
    basenc: invalid input at character 6
    foo
    $ printf 'Zm9vYg' | ./basenc -d --lenient
    foob

and checked against GNU's tool, on Linux:

    $ ./basenc --base32hex ../../test-data/shakespeare.txt | cmp - <(basenc --base32hex ../../test-data/shakespeare.txt)
    $ basenc --base64url ../../test-data/shakespeare.txt | ./basenc -d --base64url | cmp - ../../test-data/shakespeare.txt
