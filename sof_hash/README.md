# sof_hash

General-purpose hashing in c.

The library hashes memory with `sof_hash_bytes()` and NUL-terminated strings with `sof_hash_string()`, both using [rapidhash](https://github.com/Nicoshev/rapidhash), a very fast hash with good distribution whose results are the same on every platform. `sof_hash_mix()` scrambles a 64-bit value, such as an integer key, a pointer or the result of a weak hash, so that every bit of the result depends on every bit of the input. Data that comes in pieces, such as a file read a buffer at a time, is hashed with a `sof_hash_stream_t`, which the application provides and sets up with `sof_hash_stream_init()`, gives each piece with `sof_hash_stream_update()`, and gets the hash from with `sof_hash_stream_final()`; the hash is the one `sof_hash_bytes()` gives for all the data at once, however it is split. Each hash takes a _seed_, which gives unrelated hashes of the same data; pass 0 if there's no reason to do otherwise.

_bench.c_ holds the benchmark. The library's demo is the [hashsum](demo/) program in _demo/_, which prints the hash of files or of each line, as `md5sum` does, and checks lists of sums it printed. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All hash declarations are in _sof_hash.h_. rapidhash is in _rapidhash.h_, unchanged from its repository, under its own MIT license; only _sof_hash.c_ includes it. The library needs nothing beyond the c standard library; the benchmark allocates through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_), and makes its random data with [sof_random](../sof_random/) in _../sof_random/_ (_sof_random.c_ and _sof_random.h_), neither of which the library itself uses.

    uint64_t h = sof_hash_string(word, 0);                   // a NUL-terminated string
    uint64_t k = sof_hash_bytes(&point, sizeof(point), 0);   // any memory
    uint64_t p = sof_hash_mix((uintptr_t)pointer);           // an integer or pointer

    sof_hash_stream_t stream;
    sof_hash_stream_init(&stream, 0);
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        sof_hash_stream_update(&stream, buf, n);             // any number of pieces, of any size
    }
    uint64_t file = sof_hash_stream_final(&stream);          // as sof_hash_bytes() of the whole file

None of these are meant to resist hash-flooding attacks or for cryptographic use.

## Building

    make             # compiles the library and builds ./test, ./bench (the benchmark) and the demo, demo/hashsum
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization
    ./bench          # runs the benchmark

Besides testing each function, the tests check that a stream gives `sof_hash_bytes()`'s hash for every length up to 1,000 bytes, given in pieces of 1, 2, 7, 8, 15, 16, 17, 56, 111, 112, 113 and 224 bytes, whole, and in random sizes up to 300 bytes and up to 24, which mostly take the path for pieces that fit in the stream's buffer and sometimes fill it exactly, and for a megabyte in random pieces of both kinds. They also run the functions on threads, as the Threading section below describes: eight threads hashing the same data at once, with `sof_hash_bytes()`, `sof_hash_string()` and `sof_hash_mix()`, must get exactly the results computed serially. The tests are linked with _-pthread_; the library itself doesn't use threads.

To use the library in another project, copy _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ into it and compile _sof_hash.c_ with the project's own sources. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_ and _sof_hash.c_ with _-pthread_.

## Benchmark

    bench [words]

Measures `sof_hash_bytes()` on random data from 4 bytes to 64 KiB, and `sof_hash_string()` on every word in _words_, a file with a word on each line (default _/usr/share/dict/words_). Both are compared with two hashes that work a byte at a time: x31 (`h = h * 31 + c`, as in Java's `String.hashCode()`) and FNV-1a. On an Apple M-series Mac:

| bytes | hash | x31 | FNV-1a |
|---|---|---|---|
| 8 | 5.3 ns | 6.1 ns | 9.6 ns |
| 64 | 8.6 ns | 32.1 ns | 59.2 ns |
| 1024 | 19.2 ns (53 GB/s) | 482 ns (2.1 GB/s) | 911 ns (1.1 GB/s) |
| 65536 | 850 ns (77 GB/s) | 30772 ns (2.1 GB/s) | 58175 ns (1.1 GB/s) |

Dictionary words, 9.6 bytes on average, take 6.3 ns each with `sof_hash_string()`, including its `strlen()`, against 10.4 ns with x31 or with FNV-1a.

On an AMD Ryzen 9 5950X with gcc 16, Arch Linux:

| bytes | hash | x31 | FNV-1a |
|---|---|---|---|
| 8 | 4.5 ns | 6.5 ns | 8.2 ns |
| 64 | 8.0 ns | 40.4 ns | 53.0 ns |
| 1024 | 30.9 ns (33 GB/s) | 621 ns (1.6 GB/s) | 819 ns (1.2 GB/s) |
| 65536 | 1828 ns (36 GB/s) | 39644 ns (1.7 GB/s) | 52322 ns (1.3 GB/s) |

The words of _../test-data/words.txt_, 9.3 bytes on average, take 7.9 ns each with `sof_hash_string()`, against 14.0 ns with x31 and 12.5 ns with FNV-1a.

## Functions

`uint64_t sof_hash_bytes(const void *data, size_t length, uint64_t seed)`

Return the hash of the _length_ bytes at _data_, with _seed_. The result depends only on the bytes, their length and the _seed_: not on the data's address or alignment, the platform or the compiler. **NULL** _data_ hashes as if _length_ were 0.

`uint64_t sof_hash_string(const char *s, uint64_t seed)`

Return the hash of the NUL-terminated string _s_, with _seed_. The same as `sof_hash_bytes(s, strlen(s), seed)`: the terminating NUL isn't hashed. **NULL** hashes as the empty string.

`uint64_t sof_hash_mix(uint64_t x)`

Return _x_ with its bits scrambled, so that values differing only in their high bits, such as aligned pointers or small integers, give results that differ in their low bits too, which is what a hash table indexes by. It is a bijection: distinct values always give distinct results, and 0 gives 0. Use it to hash integer and pointer keys or to improve a weak hash; results from `sof_hash_bytes()` and `sof_hash_string()` don't need it. This is the MurmurHash3 finalizer.

`void sof_hash_stream_init(sof_hash_stream_t *stream, uint64_t seed)`

Set up _stream_ to hash data given to it in pieces, with _seed_, as `sof_hash_bytes()` takes one. A **NULL** _stream_ does nothing. A stream allocates nothing, so there is no function to free one.

`void sof_hash_stream_update(sof_hash_stream_t *stream, const void *data, size_t length)`

Give _stream_ the next _length_ bytes at _data_. Pieces can be of any length, **0** included, and the hash doesn't depend on how the data is split. Up to `SOF_HASH_STREAM_BLOCK` (112) bytes are copied into the stream, which holds back the last block until it knows more data follows; the rest is hashed where it is. **NULL** _data_ is taken as no bytes, whatever _length_ is, and a **NULL** _stream_ does nothing.

`uint64_t sof_hash_stream_final(const sof_hash_stream_t *stream)`

Return the hash of the data given to _stream_ so far: the same as `sof_hash_bytes()` of all of it at once, with the stream's seed. The stream isn't changed, so more data can be given afterwards, for the hash of a longer input. A **NULL** _stream_ gives the hash of no data with a seed of **0**.

## Types

    typedef struct sof_hash_stream_t {
        uint64_t seed;                  // the seed given to sof_hash_stream_init()
        uint64_t lanes[7];              // rapidhash's running state
        uint64_t length;                // bytes given so far
        size_t buffered;                // bytes held back, not yet hashed
        int blocks;                     // 1 once a 112-byte block has been hashed
        unsigned char buffer[16 + 112]; // the end of the last block hashed, then the bytes held back
    } sof_hash_stream_t;

The state of a hash of data that comes in pieces. The application provides it, anywhere, and sets it up with `sof_hash_stream_init()`; it may read the fields, `length` in particular, but must not change them. It holds no pointers, so it can be copied, which forks the hash: each copy goes on from the same data.

## Threading

The library has no global state. `sof_hash_bytes()`, `sof_hash_string()` and `sof_hash_mix()` can be called by any number of threads at once. A `sof_hash_stream_t` belongs to whoever is giving it data: `sof_hash_stream_final()` only reads it, so several threads can call that at once, but `sof_hash_stream_init()` and `sof_hash_stream_update()` change it, and need it to themselves. Separate streams can be used on separate threads freely.
