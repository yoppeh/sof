# sof_digest

Message digests and checksums in c.

The library computes SHA-256, as FIPS 180-4 defines it: a 32-byte digest that changes unpredictably with any change to the data, and for which no one knows how to find two inputs with the same digest, so it identifies data that may have been tampered with, not just changed by accident. `sof_digest_sha256()` gives the digest of memory. Data that comes in pieces, such as a file read a buffer at a time, is digested with a `sof_digest_sha256_t`, which the application provides and sets up with `sof_digest_sha256_init()`, gives each piece with `sof_digest_sha256_update()`, and gets the digest from with `sof_digest_sha256_final()`; the digest is the one `sof_digest_sha256()` gives for all the data at once, however it is split. SHA-512 works the same way, with `sof_digest_sha512()` and a `sof_digest_sha512_t`, and gives 64 bytes; it is faster than SHA-256 where neither has the processor's help, on 64-bit machines. SHA-224, SHA-384 and SHA-512/256 are SHA-256 and SHA-512 from other initial values, cut short, and have functions of their own, `sof_digest_sha224()` and the rest, whose streams are SHA-256's and SHA-512's types.

SHA-1 (`sof_digest_sha1()`, 20 bytes) and MD5 (`sof_digest_md5()`, RFC 1321, 16 bytes) work the same way too, for the formats and protocols that call for them: git's object names, ETags, the checksums in old file lists, legacy certificates. **Both are broken.** Anyone can make two inputs with the same MD5 digest in seconds, and two PDFs with the same SHA-1 digest were published in 2017 (SHAttered); such pairs now cost little to make. Use them to match what something else computes, never to show that data hasn't been tampered with; for that, use SHA-256.

`sof_digest_crc32()` and `sof_digest_crc32c()` give 32-bit cyclic redundancy checks: CRC-32, the one zip, gzip, PNG and Ethernet use, and CRC-32C, Castagnoli's, the one iSCSI, ext4, Btrfs and SSE4.2's crc32 instruction use. A CRC catches accidental damage, such as any burst of errors up to 32 bits long, many times faster than a digest, but anyone can change data without changing its CRC. They have streams too, `sof_digest_crc32_t` and `sof_digest_crc32c_t`, but don't need them: `sof_digest_crc32_extend()` continues a CRC with more data, as zlib's `crc32()` does, so a stream can be checked with nothing but the CRC so far, and `sof_digest_crc32_combine()` gives the CRC of two pieces from the CRC of each and the length of the second, so the pieces of a file can be checked on separate threads and their CRCs joined. `sof_digest_to_hex()` writes a digest as the hex digits it is usually shown in, and `sof_digest_from_hex()` reads them back. For hash tables and other uses that need none of these, use [sof_hash](../sof_hash/), which is faster still.

Blocks are hashed with the processor's instructions where they can be. On arm64, ARMv8's SHA-1 and SHA-256 instructions are used when the compiler targets them (it defines `__ARM_FEATURE_SHA2`, as Apple's clang does for every arm64 Mac; a generic aarch64 Linux build needs `-march=armv8-a+crypto`), and ARMv8.2's SHA-512 instructions likewise (`__ARM_FEATURE_SHA512`, which Apple's clang defines; elsewhere `-march=armv8.2-a+sha3`). On x86-64 with gcc or clang, x86's SHA extensions are used for SHA-1 and SHA-256, compiled for them alone and used when the processor reports them at run time, so the usual build needs no `-msha`; x86 has no SHA-512 instructions but on the newest Intel processors, so SHA-512 is portable c there. MD5 is portable c everywhere: no processor has instructions for it. Everywhere else, and wherever _sof_digest.c_ is compiled with `-D SOF_DIGEST_PORTABLE`, it uses portable c, which keeps the message schedule as a ring of 16 words and the state in registers, with the rounds written out and the variables renamed rather than moved. Every path gives the same digests.

The CRCs, on arm64, use ARMv8's CRC32 instructions where the compiler targets them (`__ARM_FEATURE_CRC32`; elsewhere `-march=armv8-a+crc`), 8 bytes to an instruction, for pieces under 64 bytes and the last few bytes of longer ones. On x86-64, SSE4.2's crc32 instruction does the same for CRC-32C up to 128 bytes, when the processor has it; it has no instruction for CRC-32. Longer pieces are folded with carry-less multiplication, ARMv8's PMULL (`__ARM_FEATURE_AES`) or x86's PCLMULQDQ, found at run time, by Intel's method ("Fast CRC Computation for Generic Polynomials Using PCLMULQDQ Instruction", 2009), which works for both polynomials with their own constants: eight 16-byte lanes are each multiplied forward past 128 bytes and the next 128 xored in, the lanes' multiplications overlapping, then folded into one and reduced to the 32 bits of the CRC. That takes in 45 to 60 GB/s on an Apple M4 Max and an Intel i9, and 20 GB/s on an AMD Ryzen 9 5950X, whose multiplier is half as fast. The portable code takes 16 bytes at a step through 16 tables of 256 entries, one lookup per byte, the lookups independent of each other, at 3.6 to 5.7 GB/s on the same machines, faster than zlib's portable code. The tables, and the constants of the multiplication, are in _sof_digest_internal.h_, which _gen_table.py_ writes with exact integer arithmetic. Combining shifts the first CRC past the second piece's length in zero bytes, multiplying it, modulo the polynomial, by x to the power of 8 times the length, from a table of x^(8 · 2^k): a multiplication for each bit set in the length.

The library's demo is the [digestsum](demo/) program in _demo/_, which prints and checks the digests of files in GNU `sha256sum`'s format, or with `--tag` in BSD's, so its lists check with those tools and theirs with it, and with `-a` any of the library's digests or CRCs instead of SHA-256. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/), twice: as _test_, with the library as usual, and as _test-portable_, with _sof_digest.c_ and _test.c_ compiled with `SOF_DIGEST_PORTABLE`, so both the instructions and the portable code are tested on a machine that has the instructions. _bench.c_ holds the benchmark, which the Makefile builds the same two ways, as _bench_ and _bench-portable_. All declarations are in _sof_digest.h_. _sof_digest_internal.h_ holds the CRC tables, which _gen_table.py_ writes; only _sof_digest.c_ includes it, and it isn't installed. The library needs nothing beyond the c standard library. The benchmark measures with [sof_timer](../sof_timer/) in _../sof_timer/_ (_sof_timer.c_ and _sof_timer.h_), reads its options with [sof_copt](../sof_copt/) in _../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_), makes its data with [sof_random](../sof_random/) in _../sof_random/_ (_sof_random.c_ and _sof_random.h_) and allocates through [sof_alloc](../sof_alloc/) in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_), and measures zlib's `crc32()` too where zlib is installed; the library itself uses none of them.

    unsigned char digest[SOF_DIGEST_SHA256_SIZE];
    char hex[2 * SOF_DIGEST_SHA256_SIZE + 1];
    sof_digest_sha256(data, length, digest);                  // any memory
    printf("%s\n", sof_digest_to_hex(digest, sizeof(digest), hex));

    sof_digest_sha256_t sha;
    sof_digest_sha256_init(&sha);
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        sof_digest_sha256_update(&sha, buf, n);               // any number of pieces, of any size
    }
    sof_digest_sha256_final(&sha, digest);                    // as sof_digest_sha256() of the whole file

    uint32_t crc = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        crc = sof_digest_crc32_extend(crc, buf, n);           // as sof_digest_crc32() of the whole file
    }
    uint32_t both = sof_digest_crc32_combine(crc_a, crc_b, length_b);   // the CRC of a then b

## Building

    make                # compiles the library and builds the tests, the benchmarks and the demo, demo/digestsum
    make check          # builds and runs the tests, test and test-portable
    make sanitize       # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck       # runs the tests under valgrind (Linux)
    make debug=1        # builds with debug info and without optimization
    ./bench             # measures the digests and CRCs
    ./bench-portable    # measures the portable code
    python3 gen_table.py > sof_digest_internal.h   # writes the CRC tables again

The tests check each digest against known values: for SHA-256, the examples in FIPS 180-4 and NIST's test vectors, runs of _a_ of each length around the 64-byte block and the padding's boundaries (55, 56, 63, 64, 119, 120 bytes and others), and a million _a_s; for MD5, RFC 1321's test suite; for SHA-1, SHA-224, SHA-384, SHA-512 and SHA-512/256, NIST's vectors, a million _a_s, and, for the last three, the lengths around SHA-512's 128-byte block and its padding (111, 112, 239, 240 bytes and others); and for each, every byte value, all from other implementations. They check every digest of every length up to 1,000 bytes, twice each at random alignments, whole and in random pieces of up to 300 bytes, and of a megabyte at once, against plain implementations in _test.c_ that compute a round at a time from a whole schedule. They check that a stream gives the one-shot digest for every length up to 600 bytes, given in pieces of 1, 7, 55, 56, 63, 64, 65, 111, 112, 127, 128 and 129 bytes and of random sizes, and for a megabyte in random pieces; that the final functions leave a stream unchanged and a copy forks it; that the data's alignment doesn't matter; and that flipping any bit of the input changes about half the digest. For the CRCs, they check the CRC catalogue's check values, of _123456789_, and others from zlib and an independent implementation, and of a megabyte of zeros; every length up to 1,000 bytes at each of 16 alignments, and as a stream in random pieces, and a megabyte whole and continued in random pieces, against a CRC in _test.c_ computed a bit at a time; that `sof_digest_crc32_extend()` and `sof_digest_crc32_combine()` give the CRC of the whole wherever the data is split; and that flipping any bit changes the CRC. They check both hex functions, including bad digits, odd lengths and that nothing is written on failure, every **NULL** argument, and eight threads computing every digest and CRC of the same data at once, which must get exactly the results computed serially. The tests are linked with _-pthread_; the library itself doesn't use threads.

To use the library in another project, copy _sof_digest.c_, _sof_digest.h_ and _sof_digest_internal.h_ into it and compile _sof_digest.c_ with the project's own sources. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_ and _sof_digest.c_ with _-pthread_; compiling both with `-D SOF_DIGEST_PORTABLE` as well tests the portable code.

## Benchmark

    bench [-h] [-n megabytes]

- `-h`: print the usage message and exit.
- `-n` _megabytes_: take in this many megabytes in each run of each measure (default 64).

For each of CRC-32, CRC-32C, MD5, SHA-1, SHA-256 and SHA-512, and zlib's `crc32()` where zlib is installed, it gives the gigabytes per second the one-shot function takes in, on random data in pieces of 64 and 256 bytes, 4 KiB and 1 MiB, the best of five runs. SHA-224 runs as fast as SHA-256, and SHA-384 and SHA-512/256 as SHA-512, so they aren't measured apart. _bench-portable_ is the same, with the library's portable code; zlib is the same in both. With gcc 16.2 on Linux, on an Intel Core i9-13900K (SHA extensions, PCLMULQDQ):

    sof_digest: GB/s, best of 5 runs of 64 MB
                      64 B     256 B     4 KiB     1 MiB
    CRC-32           15.19     38.22     44.79     45.07
    zlib crc32        1.09      2.25      4.08      4.31
    CRC-32C          20.06     38.61     45.18     45.48
    MD5               0.63      1.02      1.26      1.28
    SHA-1             1.26      2.27      3.04      3.09
    SHA-256           1.06      1.99      2.75      2.82
    SHA-512           0.41      0.58      0.87      0.89
    sof_digest, portable code: GB/s, best of 5 runs of 64 MB
                      64 B     256 B     4 KiB     1 MiB
    CRC-32            6.98      6.39      5.63      5.63
    CRC-32C           7.26      6.61      5.73      5.70
    SHA-1             0.52      0.88      1.12      1.15
    SHA-256           0.25      0.41      0.52      0.52

and on an AMD Ryzen 9 5950X (the same, and SSE4.2's crc32 instruction for short CRC-32Cs):

    sof_digest: GB/s, best of 5 runs of 64 MB
                      64 B     256 B     4 KiB     1 MiB
    CRC-32            8.57     15.82     19.55     19.80
    zlib crc32        0.81      1.89      3.41      3.59
    CRC-32C          12.48     15.44     19.44     19.82
    MD5               0.51      0.86      1.09      1.11
    SHA-1             1.44      2.20      2.60      2.64
    SHA-256           0.93      1.75      2.42      2.49
    SHA-512           0.31      0.43      0.65      0.67
    sof_digest, portable code: GB/s, best of 5 runs of 64 MB
                      64 B     256 B     4 KiB     1 MiB
    CRC-32            4.81      4.78      4.56      4.56
    CRC-32C           4.80      4.76      4.56      4.56
    SHA-1             0.35      0.60      0.77      0.79
    SHA-256           0.20      0.33      0.42      0.43

and with Apple clang on an Apple M4 Max (ARMv8's CRC32, PMULL, SHA-1, SHA-256 and SHA-512 instructions), where zlib is Apple's, which uses the instructions too:

    sof_digest: GB/s, best of 5 runs of 64 MB
                      64 B     256 B     4 KiB     1 MiB
    CRC-32           19.10     44.18     60.67     60.87
    zlib crc32        5.41     12.13     36.01     41.44
    CRC-32C          19.03     44.08     61.63     60.87
    MD5               0.44      0.66      0.82      0.81
    SHA-1             1.42      2.41      2.76      2.70
    SHA-256           1.76      2.48      2.43      2.52
    SHA-512           0.65      1.10      1.61      1.67
    sof_digest, portable code: GB/s, best of 5 runs of 64 MB
                      64 B     256 B     4 KiB     1 MiB
    CRC-32            6.69      4.70      3.66      3.62
    CRC-32C           6.96      4.65      3.65      3.62
    SHA-1             0.59      0.96      1.24      1.17
    SHA-256           0.25      0.41      0.50      0.49
    SHA-512           0.36      0.51      0.82      0.82

MD5, and SHA-512 on x86, are portable c in both builds, so the portable tables leave them out. For comparison, OpenSSL's hand-written assembly (`openssl speed`, 1 MiB pieces) hashes MD5 at 1.28, 1.11 and 0.80 GB/s on the three machines, SHA-1 at 3.16, 2.67 and 2.80, SHA-256 at 2.82, 2.49 and 2.80, and SHA-512, with AVX2 on x86, at 1.25, 1.17 and 1.58. A short piece costs the padding's extra block, or two, and the copy a stream makes of its state, so 64 bytes go at a third to half the rate of long data.

## Functions

`uint32_t sof_digest_crc32(const void *data, size_t length)`

Return the CRC-32 of the _length_ bytes at _data_: the polynomial 0x04C11DB7, reflected, with an initial value and final xor of 0xFFFFFFFF, as zip, gzip, PNG and Ethernet compute it. The CRC of _123456789_ is **0xCBF43926**, and of no bytes, **0**. **NULL** _data_ is taken as no bytes, whatever _length_ is.

`uint32_t sof_digest_crc32_combine(uint32_t crc1, uint32_t crc2, uint64_t length2)`

Return the CRC-32 of a first piece of data followed by a second, from _crc1_, the CRC-32 of the first, _crc2_, that of the second, and _length2_, the bytes in the second. The data needn't be at hand. It takes a multiplication modulo the polynomial, about 32 steps, for each bit set in _length2_; a _length2_ of **0** returns _crc1_ ^ _crc2_.

`uint32_t sof_digest_crc32_extend(uint32_t crc, const void *data, size_t length)`

Return the CRC-32 of the data whose CRC-32 is _crc_ (**0** for none) followed by the _length_ bytes at _data_, as zlib's `crc32()` does: `sof_digest_crc32_extend(sof_digest_crc32(a, n), b, m)` is the CRC of the _n_ bytes at _a_ and then the _m_ at _b_. **NULL** _data_ is taken as no bytes, returning _crc_.

`void sof_digest_crc32_init(sof_digest_crc32_t *crc)`

Set up _crc_ to compute a CRC-32 of data given to it in pieces. A **NULL** _crc_ does nothing.

`void sof_digest_crc32_update(sof_digest_crc32_t *crc, const void *data, size_t length)`

Give _crc_ the next _length_ bytes at _data_, extending its CRC and adding _length_ to its length. **NULL** _data_ is taken as no bytes, whatever _length_ is, and a **NULL** _crc_ does nothing.

`uint32_t sof_digest_crc32_final(const sof_digest_crc32_t *crc)`

Return the CRC-32 of the data given to _crc_ so far, which is also its `crc` field. The stream isn't changed. A **NULL** _crc_ gives **0**, the CRC of no data.

`uint32_t sof_digest_crc32c(const void *data, size_t length)`

Return the CRC-32C of the _length_ bytes at _data_: Castagnoli's polynomial 0x1EDC6F41, reflected, with an initial value and final xor of 0xFFFFFFFF, as iSCSI, ext4, Btrfs and SSE4.2's crc32 instruction compute it. The CRC of _123456789_ is **0xE3069283**, and of no bytes, **0**. **NULL** _data_ is taken as no bytes, whatever _length_ is.

`uint32_t sof_digest_crc32c_combine(uint32_t crc1, uint32_t crc2, uint64_t length2)`

As `sof_digest_crc32_combine()`, for CRC-32C.

`uint32_t sof_digest_crc32c_extend(uint32_t crc, const void *data, size_t length)`

As `sof_digest_crc32_extend()`, for CRC-32C.

`void sof_digest_crc32c_init(sof_digest_crc32c_t *crc)`

As `sof_digest_crc32_init()`, for CRC-32C.

`void sof_digest_crc32c_update(sof_digest_crc32c_t *crc, const void *data, size_t length)`

As `sof_digest_crc32_update()`, for CRC-32C.

`uint32_t sof_digest_crc32c_final(const sof_digest_crc32c_t *crc)`

As `sof_digest_crc32_final()`, for CRC-32C.

`void sof_digest_md5(const void *data, size_t length, unsigned char *digest)`

Write the MD5 digest of the _length_ bytes at _data_, as RFC 1321 defines it, to _digest_, which takes `SOF_DIGEST_MD5_SIZE` (16) bytes. MD5 is broken: use it only where a format calls for it. **NULL** _data_ is taken as no bytes, whatever _length_ is, and a **NULL** _digest_ does nothing.

`void sof_digest_md5_init(sof_digest_md5_t *md5)`

Set up _md5_ to digest data given to it in pieces. A **NULL** _md5_ does nothing.

`void sof_digest_md5_update(sof_digest_md5_t *md5, const void *data, size_t length)`

Give _md5_ the next _length_ bytes at _data_. Fewer than `SOF_DIGEST_MD5_BLOCK` (64) bytes are copied into the stream, until it has a whole block; whole blocks are hashed where they are. **NULL** _data_ is taken as no bytes, whatever _length_ is, and a **NULL** _md5_ does nothing.

`void sof_digest_md5_final(const sof_digest_md5_t *md5, unsigned char *digest)`

Write the MD5 digest of the data given to _md5_ so far to _digest_, which takes `SOF_DIGEST_MD5_SIZE` bytes: the same as `sof_digest_md5()` of all of it at once. The stream isn't changed, so more data can be given afterwards. A **NULL** _md5_ gives the digest of no data, and a **NULL** _digest_ does nothing.

`void sof_digest_sha1(const void *data, size_t length, unsigned char *digest)`

Write the SHA-1 digest of the _length_ bytes at _data_ to _digest_, which takes `SOF_DIGEST_SHA1_SIZE` (20) bytes. SHA-1 is broken for collisions: use it only where a format calls for it. **NULL** _data_ is taken as no bytes, whatever _length_ is, and a **NULL** _digest_ does nothing.

`void sof_digest_sha1_init(sof_digest_sha1_t *sha)`

`void sof_digest_sha1_update(sof_digest_sha1_t *sha, const void *data, size_t length)`

`void sof_digest_sha1_final(const sof_digest_sha1_t *sha, unsigned char *digest)`

As the MD5 stream's functions, for SHA-1, whose blocks are `SOF_DIGEST_SHA1_BLOCK` (64) bytes and whose digest takes `SOF_DIGEST_SHA1_SIZE` bytes.

`void sof_digest_sha224(const void *data, size_t length, unsigned char *digest)`

Write the SHA-224 digest of the _length_ bytes at _data_ to _digest_, which takes `SOF_DIGEST_SHA224_SIZE` (28) bytes. **NULL** _data_ is taken as no bytes, whatever _length_ is, and a **NULL** _digest_ does nothing.

`void sof_digest_sha224_init(sof_digest_sha224_t *sha)`

`void sof_digest_sha224_update(sof_digest_sha224_t *sha, const void *data, size_t length)`

`void sof_digest_sha224_final(const sof_digest_sha224_t *sha, unsigned char *digest)`

As SHA-256's stream functions, for SHA-224: `sof_digest_sha224_init()` sets the stream up with SHA-224's initial value, `sof_digest_sha224_update()` is `sof_digest_sha256_update()`, and `sof_digest_sha224_final()` writes the first `SOF_DIGEST_SHA224_SIZE` bytes. A stream set up with one init must be finished with the same digest's final.

`void sof_digest_sha256(const void *data, size_t length, unsigned char *digest)`

Write the SHA-256 digest of the _length_ bytes at _data_ to _digest_, which takes `SOF_DIGEST_SHA256_SIZE` (32) bytes. **NULL** _data_ is taken as no bytes, whatever _length_ is, and a **NULL** _digest_ does nothing.

`void sof_digest_sha256_init(sof_digest_sha256_t *sha)`

Set up _sha_ to digest data given to it in pieces. A **NULL** _sha_ does nothing. A stream allocates nothing, so there is no function to free one.

`void sof_digest_sha256_update(sof_digest_sha256_t *sha, const void *data, size_t length)`

Give _sha_ the next _length_ bytes at _data_. Pieces can be of any length, **0** included, and the digest doesn't depend on how the data is split. Fewer than `SOF_DIGEST_SHA256_BLOCK` (64) bytes are copied into the stream, until it has a whole block; whole blocks are hashed where they are. **NULL** _data_ is taken as no bytes, whatever _length_ is, and a **NULL** _sha_ does nothing.

`void sof_digest_sha256_final(const sof_digest_sha256_t *sha, unsigned char *digest)`

Write the SHA-256 digest of the data given to _sha_ so far to _digest_, which takes `SOF_DIGEST_SHA256_SIZE` bytes: the same as `sof_digest_sha256()` of all of it at once. The stream isn't changed, so more data can be given afterwards, for the digest of a longer input. A **NULL** _sha_ gives the digest of no data, and a **NULL** _digest_ does nothing.

`void sof_digest_sha384(const void *data, size_t length, unsigned char *digest)`

Write the SHA-384 digest of the _length_ bytes at _data_ to _digest_, which takes `SOF_DIGEST_SHA384_SIZE` (48) bytes. **NULL** _data_ is taken as no bytes, whatever _length_ is, and a **NULL** _digest_ does nothing.

`void sof_digest_sha384_init(sof_digest_sha384_t *sha)`

`void sof_digest_sha384_update(sof_digest_sha384_t *sha, const void *data, size_t length)`

`void sof_digest_sha384_final(const sof_digest_sha384_t *sha, unsigned char *digest)`

As SHA-512's stream functions, for SHA-384: `sof_digest_sha384_init()` sets the stream up with SHA-384's initial value, `sof_digest_sha384_update()` is `sof_digest_sha512_update()`, and `sof_digest_sha384_final()` writes the first `SOF_DIGEST_SHA384_SIZE` bytes.

`void sof_digest_sha512(const void *data, size_t length, unsigned char *digest)`

Write the SHA-512 digest of the _length_ bytes at _data_ to _digest_, which takes `SOF_DIGEST_SHA512_SIZE` (64) bytes. **NULL** _data_ is taken as no bytes, whatever _length_ is, and a **NULL** _digest_ does nothing.

`void sof_digest_sha512_init(sof_digest_sha512_t *sha)`

`void sof_digest_sha512_update(sof_digest_sha512_t *sha, const void *data, size_t length)`

`void sof_digest_sha512_final(const sof_digest_sha512_t *sha, unsigned char *digest)`

As SHA-256's stream functions, for SHA-512, whose blocks are `SOF_DIGEST_SHA512_BLOCK` (128) bytes and whose digest takes `SOF_DIGEST_SHA512_SIZE` bytes.

`void sof_digest_sha512_256(const void *data, size_t length, unsigned char *digest)`

Write the SHA-512/256 digest of the _length_ bytes at _data_ to _digest_, which takes `SOF_DIGEST_SHA512_256_SIZE` (32) bytes: SHA-512 from another initial value, cut to 32 bytes, which is faster than SHA-256 on 64-bit machines without SHA-256 instructions. **NULL** _data_ is taken as no bytes, whatever _length_ is, and a **NULL** _digest_ does nothing.

`void sof_digest_sha512_256_init(sof_digest_sha512_256_t *sha)`

`void sof_digest_sha512_256_update(sof_digest_sha512_256_t *sha, const void *data, size_t length)`

`void sof_digest_sha512_256_final(const sof_digest_sha512_256_t *sha, unsigned char *digest)`

As SHA-384's stream functions, for SHA-512/256, whose digest takes `SOF_DIGEST_SHA512_256_SIZE` bytes.

`char *sof_digest_to_hex(const void *bytes, size_t length, char *hex)`

Write the _length_ bytes at _bytes_, a digest or anything else, to _hex_ as two lowercase hex digits each, the high four bits first, as `sha256sum` prints them, followed by a NUL. _hex_ needs room for 2 × _length_ + 1 characters: 65 for a SHA-256 digest, and 2 × `SOF_DIGEST_MAX_SIZE` + 1 for any. Return _hex_, or **NULL** if _hex_ is **NULL**. **NULL** _bytes_ is taken as no bytes, giving the empty string. A CRC is a number rather than bytes; print it with `printf("%08x", crc)`.

`int sof_digest_from_hex(const char *hex, size_t length, void *bytes)`

Read the _length_ hex digits at _hex_, in either case, into _length_ / 2 bytes at _bytes_, each two digits making one byte, the first the high four bits. Nothing else, not even a space, may come between them, and _hex_ needn't end with a NUL. Return **0**, or **-1**, writing nothing, if _length_ is odd, any character isn't a hex digit, or _hex_ or _bytes_ is **NULL** and _length_ isn't **0**.

None of the functions fail: every **NULL** argument is handled as above, and a stream that has been set up is always good. There is no freed state, since a stream holds nothing to free.

## Types

    typedef struct sof_digest_sha256_t {
        uint32_t state[8];              // the hash so far
        uint64_t length;                // bytes given so far
        size_t buffered;                // bytes in buffer, not yet hashed
        unsigned char buffer[64];       // the start of a block, waiting for the rest
    } sof_digest_sha256_t;

    typedef sof_digest_sha256_t sof_digest_sha224_t;

The state of a SHA-256 digest, or with SHA-224's functions a SHA-224 digest, of data that comes in pieces. The application provides it, anywhere, and sets it up with `sof_digest_sha256_init()`; it may read the fields, `length` in particular, but must not change them. It holds no pointers, so it can be copied, which forks the digest: each copy goes on from the same data, which saves digesting a shared prefix again. It holds part of the data, so treat it as the data would be treated, and clear it when that matters.

    typedef struct sof_digest_sha512_t {
        uint64_t state[8];              // the hash so far
        uint64_t length;                // bytes given so far
        size_t buffered;                // bytes in buffer, not yet hashed
        unsigned char buffer[128];      // the start of a block, waiting for the rest
    } sof_digest_sha512_t;

    typedef sof_digest_sha512_t sof_digest_sha384_t;
    typedef sof_digest_sha512_t sof_digest_sha512_256_t;

    typedef struct sof_digest_sha1_t {
        uint32_t state[5];
        uint64_t length;
        size_t buffered;
        unsigned char buffer[64];
    } sof_digest_sha1_t;

    typedef struct sof_digest_md5_t {
        uint32_t state[4];
        uint64_t length;
        size_t buffered;
        unsigned char buffer[64];
    } sof_digest_md5_t;

The states of SHA-512, SHA-384 and SHA-512/256 digests, of SHA-1 and of MD5, with the same fields and rules as `sof_digest_sha256_t`.

    typedef struct sof_digest_crc32_t {
        uint32_t crc;                   // the CRC of the data so far
        uint64_t length;                // bytes given so far
    } sof_digest_crc32_t;

    typedef struct sof_digest_crc32c_t {
        uint32_t crc;
        uint64_t length;
    } sof_digest_crc32c_t;

The state of a CRC-32 or CRC-32C of data that comes in pieces, set up with `sof_digest_crc32_init()` or `sof_digest_crc32c_init()`. The application may read the fields: `crc` is the CRC of the data so far, and `length` its length, as `sof_digest_crc32_combine()` wants it. It must not change them. It can be copied, which forks the CRC.

## Threading

The library has no global state. The one-shot functions, `sof_digest_crc32_extend()`, `sof_digest_crc32_combine()` and their CRC-32C forms, `sof_digest_to_hex()` and `sof_digest_from_hex()` can be called by any number of threads at once. A stream belongs to whoever is giving it data: the final functions only read it, so several threads can call them at once, but the init and update functions change it, and need it to themselves. Separate streams can be used on separate threads freely, and a long file's pieces can be checked on separate threads with the one-shot CRC functions and joined with `sof_digest_crc32_combine()`.
