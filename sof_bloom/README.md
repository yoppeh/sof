# sof_bloom

A Bloom filter, which tells whether a key has been added in a few bits per key, in c.

The library has a `sof_bloom_t` type that describes a filter. The application provides the `sof_bloom_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_bloom_init()`, giving the number of keys it expects, the false positive rate it can accept, whether it is a classic or a blocked filter, and an optional allocator. Keys, which are any bytes, are added with `sof_bloom_add()` and tested for with `sof_bloom_contains()`; `sof_bloom_add_string()` and `sof_bloom_contains_string()` do the same for NUL-terminated strings. `sof_bloom_count()` says how many keys were added and `sof_bloom_false_positive_rate()` estimates the rate the filter has reached. `sof_bloom_clear()` empties the filter and `sof_bloom_free()` releases its memory once the application is finished with it.

A Bloom filter never says no for a key that was added, but sometimes says yes for one that wasn't: a false positive. In exchange it takes far less memory than a set of the keys, and none for the keys themselves, which it doesn't keep, so they can't be listed or removed. The filter is an array of _m_ bits. Adding a key sets _k_ of them, chosen from one hash of the key; testing a key checks the same _k_, and says no if any is 0. For _n_ expected keys and a false positive rate _p_, `sof_bloom_init()` takes _m_ = -_n_ ln _p_ / (ln 2)² bits and _k_ = -log₂ _p_ hashes: about 9.6 bits and 7 hashes per key for 1%, and 4.8 bits and 3 hashes more for each tenth as large a rate. Adding more keys than expected works, but the false positive rate then rises above _p_, towards 1.

That is a classic filter, the default. Each of its _k_ bits can be anywhere in the array, so each is a memory access of its own, and once the filter is larger than the cache, each is a cache miss. Given **SOF_BLOOM_BLOCKED**, `sof_bloom_init()` makes a blocked filter instead: a split-block Bloom filter, as in Apache Parquet and Impala. Its array is 256-bit blocks of eight 32-bit words, starting on a 64-byte boundary. The key's hash chooses one block, by multiplying its high 32 bits by the number of blocks and keeping the top half of the product (no division), and the key sets one bit in each of the block's eight words, chosen by the top 5 bits of the hash's low 32 bits times a fixed odd constant for that word. So every add or lookup touches one cache line, and costs the same whatever the rate, but the bits are spread less evenly, and a blocked filter takes more of them for the same rate: some blocks get more keys than others, and every key sets 8 bits, more than a high rate needs and fewer than a low one would use. The rate with an average of _L_ keys per block is the sum over _j_ of the Poisson chance of a block holding _j_ keys, e^-_L_ _L_^_j_ / _j_!, times (1 - (31/32)^_j_)^8, and `sof_bloom_init()` solves that for the _L_ that gives _p_ and takes _n_ / _L_ blocks: 6.0 bits per key for 10%, 10.5 for 1%, 16.9 for 0.1% and 26.3 for 0.01%, against 4.8, 9.6, 14.4 and 19.2 for a classic filter. The benchmark below measures the rates it reaches; they match.

Choose a classic filter when memory is what matters, or the filter fits in the cache and keys are added and looked up rarely enough for the speed not to matter. Choose a blocked filter when speed matters, above all for a filter larger than the cache: there it is 1.6 to 3.7 times as fast in the benchmark below, for 10 to 17% more memory at rates of 1% and 0.1%. When the filter fits in the cache, its lookups are still faster, misses up to twice as fast, but adds gain less, and on one machine of three lost to a classic filter's at 10%. Very low rates cost it more memory (37% more at 0.01%), and a blocked filter overfilled far beyond the keys it was sized for fills up sooner than a classic one of a high rate, which sets fewer bits per key.

_bench.c_ is a benchmark that compares classic and blocked filters of several rates with each other and with a [sof_hashmap](../sof_hashmap/) holding the same keys, and classic and blocked filters much larger than the cache with each other. It reads its options with the [sof_copt](../sof_copt/) library, shuffles the words with [sof_random](../sof_random/) and times with [sof_timer](../sof_timer/), none of which the library itself uses. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). The library's demo, the [uniq](demo/) program in _demo/_, uses a filter for its `-a` option, to drop repeated lines in fixed memory, and a blocked one with `-b` as well. All declarations are in _sof_bloom.h_. The bits are `sof_bit_t` words from the [sof_bit](../sof_bit/) library in _../sof_bit/_ (_sof_bit.h_, which is all of it), keys are hashed with the [sof_hash](../sof_hash/) library in _../sof_hash/_ (_sof_hash.c_ and _sof_hash.h_, which use [rapidhash](https://github.com/Nicoshev/rapidhash) in _rapidhash.h_, under its own MIT license), and memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond those, the library needs nothing but the c standard library, including its maths functions, so programs that use it link with `-lm`.

    sof_bloom_t seen;
    sof_bloom_init(&seen, 100000, 0.01, 0, NULL); // 100,000 keys at 1%; or an allocator
                                                  // SOF_BLOOM_BLOCKED for a blocked filter
    sof_bloom_add_string(&seen, "red", NULL);
    if (!sof_bloom_contains_string(&seen, "green")) {
        ...                                       // certainly never added
    }
    int created;
    sof_bloom_add_string(&seen, url, &created);   // add and test in one call
    if (created) {
        ...                                       // url is new
    }
    sof_bloom_free(&seen);

## Building

    make             # compiles the library and builds ./test, ./bench (the benchmark) and the demo, demo/uniq
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization
    ./bench          # runs the benchmark on ../test-data/words.txt and 100,000,000 generated keys
    ./bench -n 0     # runs it on the word list only

The tests run for both kinds of filter. They check the classic sizes for known rates, and the blocked filter's bits per key for rates from 10% to 0.01%, that it takes more bits than a classic filter, that its blocks are aligned to 64 bytes and that a key sets one bit in each word of one block. For each kind, they add random keys and check that every one is found afterwards and that _created_ and the count agree with what the filter said before each add, and measure the false positive rate over 1,000,000 keys that weren't added, at 10%, 1%, 0.1% and 0.01%, which must be within 5% to 40% of the rate asked for (wider for the rarer events), with the estimate from `sof_bloom_false_positive_rate()` within 15%. They check that the same keys in another order set the same bits, and that those bits are the same as they have always been (a fingerprint of the classic filter's bits from before blocked filters were added). They also cover strings, **NULL** and empty keys, clearing, overfilling, bad rates, flags and allocators, the sizes given to the allocator and too large a filter, failing allocation, and every function on a **NULL**, freed or failed filter.

To use the library in another project, copy _sof_bloom.c_ and _sof_bloom.h_, _sof_bit.h_ from _../sof_bit/_, _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, and _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../sof_hash/_, into it, and link with `-lm`. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_, _sof_bloom.c_, _sof_alloc.c_ and _sof_hash.c_ with _-pthread_, linking with `-lm`.

## Benchmark

    bench [-h] [-n keys] [words]

The benchmark shuffles the words of a word list and splits them in two. The first half go into classic and blocked Bloom filters of 10%, 1%, 0.1% and 0.01% and into a `sof_hashmap_t`, and then every word of both halves is looked up. For each, it prints the memory per key in bits, the nanoseconds per add, per lookup of a word that was added (a hit) and per lookup of one that wasn't (a miss), and the share of misses that came back yes. The hashmap stores pointers into the word list rather than copies, and its memory counts its array of elements only; a real set of words would need the strings as well, about 80 bits more per word for this list. Each time is the best of five runs.

Then it does the same for classic and blocked filters of 1% and 0.1% far larger than any cache: it adds _keys_ generated 8-byte keys (100,000,000 unless `-n` says otherwise), looks them all up, and looks up as many others. At 100,000,000 keys the filters take 120 to 211 MB. Each time is the best of three runs.

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-n` _keys_: the number of generated keys for the large filters; **0** leaves them out.
- _words_: a file with a word on each line; by default _../test-data/words.txt_.

On an Apple M4 Max with Apple clang 21, for the 263,533 words of _../test-data/words.txt_, 131,766 of them added (measured while other work kept the machine busy, so the times are about 10% higher than on an idle machine):

| | bits/key | add ns | hit ns | miss ns | false pos |
|---|---|---|---|---|---|
| classic 10% | 4.8 | 16.6 | 13.6 | 24.3 | 10.103% |
| classic 1% | 9.6 | 20.6 | 16.3 | 28.5 | 1.012% |
| classic 0.1% | 14.4 | 24.2 | 19.0 | 28.8 | 0.090% |
| classic 0.01% | 19.2 | 28.2 | 21.4 | 29.0 | 0.009% |
| blocked 10% | 6.0 | 14.4 | 12.0 | 11.8 | 10.042% |
| blocked 1% | 10.5 | 14.3 | 12.1 | 11.8 | 1.000% |
| blocked 0.1% | 16.9 | 14.3 | 12.1 | 11.8 | 0.104% |
| blocked 0.01% | 26.3 | 14.5 | 12.2 | 11.9 | 0.009% |
| hashmap | 382.0 | 22.7 | 21.8 | 34.6 | 0.000% |

and for 100,000,000 generated keys:

| | bits/key | add ns | hit ns | miss ns | false pos |
|---|---|---|---|---|---|
| classic 1% | 9.6 | 56.4 | 43.2 | 46.7 | 1.004% |
| classic 0.1% | 14.4 | 78.6 | 58.3 | 50.2 | 0.100% |
| blocked 1% | 10.5 | 19.9 | 14.7 | 14.3 | 0.999% |
| blocked 0.1% | 16.9 | 21.1 | 15.6 | 15.5 | 0.100% |

On an Intel i9-13900K with gcc 16, Arch Linux:

| | bits/key | add ns | hit ns | miss ns | false pos |
|---|---|---|---|---|---|
| classic 10% | 4.8 | 14.0 | 11.8 | 18.0 | 10.103% |
| classic 1% | 9.6 | 17.4 | 14.5 | 20.6 | 1.012% |
| classic 0.1% | 14.4 | 22.2 | 18.7 | 20.4 | 0.090% |
| classic 0.01% | 19.2 | 27.7 | 23.7 | 20.0 | 0.009% |
| blocked 10% | 6.0 | 11.6 | 9.5 | 9.5 | 10.042% |
| blocked 1% | 10.5 | 11.8 | 9.5 | 9.5 | 1.000% |
| blocked 0.1% | 16.9 | 12.0 | 9.6 | 9.6 | 0.104% |
| blocked 0.01% | 26.3 | 12.2 | 9.7 | 9.7 | 0.009% |
| hashmap | 382.0 | 15.0 | 14.2 | 23.0 | 0.000% |

and for 100,000,000 generated keys:

| | bits/key | add ns | hit ns | miss ns | false pos |
|---|---|---|---|---|---|
| classic 1% | 9.6 | 83.8 | 71.5 | 52.2 | 1.004% |
| classic 0.1% | 14.4 | 114.5 | 94.4 | 56.3 | 0.100% |
| blocked 1% | 10.5 | 37.0 | 28.0 | 28.0 | 0.999% |
| blocked 0.1% | 16.9 | 39.3 | 29.3 | 29.3 | 0.100% |

On an AMD Ryzen 9 5950X with gcc 16, Arch Linux:

| | bits/key | add ns | hit ns | miss ns | false pos |
|---|---|---|---|---|---|
| classic 10% | 4.8 | 18.5 | 14.9 | 21.2 | 10.103% |
| classic 1% | 9.6 | 30.6 | 21.7 | 23.8 | 1.012% |
| classic 0.1% | 14.4 | 35.3 | 25.9 | 23.9 | 0.090% |
| classic 0.01% | 19.2 | 41.0 | 29.3 | 24.4 | 0.009% |
| blocked 10% | 6.0 | 25.9 | 15.3 | 15.1 | 10.042% |
| blocked 1% | 10.5 | 26.1 | 15.6 | 15.4 | 1.000% |
| blocked 0.1% | 16.9 | 26.3 | 15.9 | 15.7 | 0.104% |
| blocked 0.01% | 26.3 | 26.3 | 16.2 | 15.9 | 0.009% |
| hashmap | 382.0 | 20.9 | 19.2 | 28.3 | 0.000% |

and for 100,000,000 generated keys:

| | bits/key | add ns | hit ns | miss ns | false pos |
|---|---|---|---|---|---|
| classic 1% | 9.6 | 92.3 | 67.3 | 58.4 | 1.004% |
| classic 0.1% | 14.4 | 119.6 | 90.0 | 62.0 | 0.100% |
| blocked 1% | 10.5 | 41.5 | 37.1 | 37.1 | 0.999% |
| blocked 0.1% | 16.9 | 44.0 | 39.3 | 39.3 | 0.100% |

Every filter reaches the false positive rate asked for, blocked ones as well as classic, in 15 to 80 times less memory than the hashmap's array alone. A classic filter's add sets _k_ bits scattered over the filter, so it costs more as the rate falls, while a blocked filter's add or lookup touches one block, and costs the same at every rate. On the word list, whose filters fit in the cache, a blocked filter's lookups take little more than hashing the word, and its misses, which in a classic filter test bits until one is 0, take half as long (two-thirds on the AMD Ryzen). Its adds are faster too, with one exception: on the AMD Ryzen they take longer than a classic filter's at 10%. There the cost is less the add than the word's text, which the shuffled list keeps out of the cache, and a blocked add, though it touches less memory, takes more instructions, so fewer adds fit in the processor's window to wait for their words at once. When the filter is far larger than the cache, a classic filter waits for a cache miss for each of its bits, and a blocked filter for one: it is 2.2 to 3.7 times as fast to add to and 1.6 to 3.7 times as fast to look up in, for 10 to 17% more memory. Hits are measured straight after the adds, while those words' text is still in cache, which flatters them next to misses.

## Functions

`sof_bloom_error_t sof_bloom_init(sof_bloom_t *bloom, size_t expected_keys, double false_positive_rate, unsigned flags, const sof_alloc_t *alloc)`

Initialize the _bloom_ the caller provides, sized for _expected_keys_ keys (**0** is taken as **1**) with a chance of _false_positive_rate_, once that many have been added, that `sof_bloom_contains()` says yes for a key that wasn't. _flags_ is **0** for a classic filter or **SOF_BLOOM_BLOCKED** for a blocked one. In a classic filter, the number of bits is rounded up to a whole number of `sof_bit_t` words, and the number of hashes to the nearest whole number, at least **1**. A blocked filter has 8 hashes and a whole number of 256-bit blocks, at least **1** and at most 2^32 (128 GiB), sized as described above; it allocates 63 bytes more than the blocks, to align them, and finding the size takes from 2 to 40 microseconds. _alloc_ is the allocator for the filter's bits; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). Returns **SOF_BLOOM_OK**, **SOF_BLOOM_NO_MEM** if the bits couldn't be allocated, would be too many to count in a `size_t` or would be more than 2^32 blocks, or **SOF_BLOOM_ERROR** if _bloom_ is **NULL**, _false_positive_rate_ isn't more than **0** and less than **1**, _flags_ has a bit other than **SOF_BLOOM_BLOCKED**, or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves the _bloom_ empty.

`sof_bloom_error_t sof_bloom_add(sof_bloom_t *bloom, const void *data, size_t length, int *created)`

Add the key of _length_ bytes at _data_ to the _bloom_, setting its bits. **NULL** _data_ is the empty key, whatever _length_ is. If _created_ is not **NULL**, it is set to **1** if the key was new (at least one of its bits was 0, so the filter would have said no), or **0** if the filter already said yes or the add failed; that makes this a test and add in one call. A new key increases the count. Returns **SOF_BLOOM_OK**, or **SOF_BLOOM_ERROR** if _bloom_ is **NULL**, was freed or was never initialized.

`sof_bloom_error_t sof_bloom_add_string(sof_bloom_t *bloom, const char *s, int *created)`

The same as `sof_bloom_add(bloom, s, strlen(s), created)`: the terminating NUL isn't part of the key, so a string and its bytes are the same key. **NULL** _s_ is the empty string.

`int sof_bloom_contains(const sof_bloom_t *bloom, const void *data, size_t length)`

Return **1** if the key of _length_ bytes at _data_ was probably added to the _bloom_ (all its bits are set), or **0** if it certainly wasn't. **NULL** _data_ is the empty key. Returns **0** if _bloom_ is **NULL**, was freed or was never initialized.

`int sof_bloom_contains_string(const sof_bloom_t *bloom, const char *s)`

The same as `sof_bloom_contains(bloom, s, strlen(s))`. **NULL** _s_ is the empty string.

`size_t sof_bloom_count(const sof_bloom_t *bloom)`

Return how many keys `sof_bloom_add()` found new. Adding a key again doesn't count, and nor does a key that was a false positive when it was added, so this can be a little below the number of different keys added. Returns **0** if _bloom_ is **NULL**, was freed or was never initialized.

`double sof_bloom_false_positive_rate(const sof_bloom_t *bloom)`

Return an estimate of the chance that `sof_bloom_contains()` says yes for a key that wasn't added. For a classic filter it is the fraction of bits that are set, to the power of the number of hashes. That would be too low for a blocked filter, whose blocks hold different numbers of keys, so there it is the formula `sof_bloom_init()` sizes it by, with the average keys per block _L_ found from the fraction of bits set, which is 1 - e^(-_L_/32). It starts at **0**, is close to the rate given to `sof_bloom_init()` once the expected number of keys have been added, and rises towards **1** as more are. Returns **0** if _bloom_ is **NULL**, was freed or was never initialized.

`sof_bloom_error_t sof_bloom_clear(sof_bloom_t *bloom)`

Empty the _bloom_, keeping its memory and size, so it can be filled again. Returns **SOF_BLOOM_OK**.

`sof_bloom_error_t sof_bloom_free(sof_bloom_t *bloom)`

Free the _bloom_'s memory. The `sof_bloom_t` itself belongs to the caller and isn't freed; it is left empty. Returns **SOF_BLOOM_OK**.

Every function accepts a **NULL** _bloom_: `sof_bloom_contains()`, `sof_bloom_contains_string()`, `sof_bloom_count()` and `sof_bloom_false_positive_rate()` return **0**, and the others return **SOF_BLOOM_ERROR**. A filter that was freed, or whose `sof_bloom_init()` failed, is empty and stays that way: it contains nothing, adds return **SOF_BLOOM_ERROR**, and clearing or freeing it again returns **SOF_BLOOM_OK**, until it is initialized again. **NULL** key data is the empty key, as in [sof_hash](../sof_hash/).

## Types

    typedef enum sof_bloom_error_t {
        SOF_BLOOM_OK = 0,
        SOF_BLOOM_NO_MEM,
        SOF_BLOOM_NO_MATCH,       // never returned; kept so the codes match
        SOF_BLOOM_ERROR,
    } sof_bloom_error_t;

    typedef enum sof_bloom_flag_t {
        SOF_BLOOM_BLOCKED = 1,    // a blocked filter
    } sof_bloom_flag_t;

The flags `sof_bloom_init()` takes; **0** is a classic filter.

    typedef struct sof_bloom_t {
        sof_bit_t *bits;          // the filter's bits; NULL when freed
        size_t words;             // the number of sof_bit_t words in bits
        size_t size;              // the number of bits, words * SOF_BIT_BITS
        size_t set;               // how many bits are 1
        size_t count;             // keys sof_bloom_add() found new
        size_t blocks;            // 256-bit blocks in a blocked filter; 0 in a classic one
        unsigned hashes;          // how many bits each key sets; 8 in a blocked filter
        unsigned flags;           // the flags given to sof_bloom_init()
        void *memory;             // what was allocated for bits
        sof_alloc_t alloc;        // a copy of the allocator
    } sof_bloom_t;

The application can read the fields, but must not change them. In a classic filter, _memory_ is _bits_. `sof_bit_t` is `size_t` unless _sof_bit.h_ is told otherwise, so _words_ differs between 64-bit and 32-bit machines, but _size_ is within a word of the same, and a key's bit positions depend only on its bytes and _size_. In a blocked filter, _bits_ is the first 64-byte boundary in _memory_, and holds _blocks_ blocks of eight `uint32_t` words, which must be read as `uint32_t`; a key's bits depend only on its bytes and _blocks_, the same on any machine. In both, _set_ and _count_ mean the same: a key whose bits were all set already counts as a false positive and isn't counted.

## Threading

The library does no locking. `sof_bloom_contains()`, `sof_bloom_contains_string()`, `sof_bloom_count()` and `sof_bloom_false_positive_rate()` only read the filter, so any number of threads can call them at once as long as nothing writes to it; every other function writes. A caller sharing a filter between threads should guard it with a reader-writer lock, taking the write lock for `sof_bloom_add()`, `sof_bloom_add_string()`, `sof_bloom_clear()` and `sof_bloom_free()`.

Separate filters need no lock at all, as long as their allocators aren't shared, or are safe to share.
