# Optimization survey

**Status, 2026-11-20:** done. Every change below was made and measured
before and after on the Mac, morbius and davros, except these, which were
measured and dropped: Tukey's ninther (sof_sort), the conditional
subtraction in sof_bloom's probe, the 8-byte skip in sof_json's
`skip_space()`, and keeping the top of the stack in sof_json's reader. Not
done: vendoring Ryu or Schubfach (sof_json and sof_toml use the cheaper
`%.15g` change), the blocked Bloom filter, and skipping sof_hashmap's mix.
PCRE2's JIT is opt-in through `sof_regex_jit()`. sof_log's direct build
still flushes every line, so a line survives a crash; the flush is a system
call, about 140 ns on Linux and 1.4 us on macOS. The survey below is kept as
it was written.

A survey of every library in sof for places where the code is slower than it
needs to be, made on 2026-11-20 after the work on sof_string. Nothing in the
libraries was changed. Each finding was prototyped or measured on copies of the
sources outside the project, and the main claims were checked against the
code.

How to read the numbers:

- **Measured** numbers come from Apple clang 21 at -O3 on the Apple M4 Max
  (macOS, arm64), best of several runs. Other surveys were running on the
  machine at the same time, so differences under about 10% are treated as
  noise.
- **Estimated** numbers are reasoning, not measurement, and are marked so.
- Nothing here was measured on morbius (Linux, x86-64, gcc). Several findings
  depend on the CPU (64-bit division, `memcmp()`, the C library), so the larger
  changes should be measured there before they are adopted.
- Where a prototype was tested, the library's own _test.c_ passed with it; the
  parser prototypes also passed under AddressSanitizer and
  UndefinedBehaviorSanitizer, with the allocation-failure tests and the
  conformance suites.

## The short list

Ranked by benefit for the effort. Each is described under its library below.

| # | Library | Change | Gain | Effort | Risk |
|---|---|---|---|---|---|
| 1 | sof_csv | Append runs of ordinary bytes at once, not one byte per call | 2.3–4x reading (measured) | ~40 lines | low |
| 2 | sof_utf | ASCII fast path, 8 bytes at a time, in validate, count and convert | 3–10x on ASCII text (measured) | small | low |
| 3 | sof_digest | Restructured SHA-256; ARMv8 SHA instructions where the compiler has them | 1.35x portable, 6x on arm64 (measured) | ~100 lines | low |
| 4 | sof_sort | Fast 4- and 8-byte swap, typed merge copies, ninther pivot | 1.1–2.2x depending on input (measured) | small | low |
| 5 | sof_json | Exact fast path for ordinary numbers in `sof_json_number_double()` | 5.6x per number (measured) | ~20 lines | low |
| 6 | sof_bloom | Locals and a branch-free count in `sof_bloom_add()` | 1.6–2.3x per add (measured) | trivial | none |
| 7 | sof_json, sof_toml | Skip runs of plain string bytes 8 at a time | up to 1.5x on string-heavy text (measured) | small | low |
| 8 | sof_set | Reuse each element's stored hash in union, intersection and difference | 1.3–1.7x (measured) | small | low |
| 9 | sof_queue | Recycle elements instead of one allocation per enqueue | 2.2x (measured with a pool) | small | low |
| 10 | sof_log | Optional flush per line; format a threaded line once | 3–8x and 2.4x (measured) | small | behaviour change |
| 11 | sof_vector | `sof_vector_push()` writes the slot directly when there is room | 1.65x (measured) | 5 lines | none |
| 12 | sof_random | Fixed-size swap in `sof_random_shuffle()` | 2.6x (measured) | trivial | none |

Four things need a decision before they can be done (see Decisions): PCRE2's
JIT in sof_regex, flushing in sof_log, a fast append in sof_string, and changes
to sof_random that alter seeded sequences.

## Parsers and writers

### sof_csv

**Reader: one byte per call.** _sof_csv_reader.c_, the loop in
`sof_csv_reader_next()` and `take()`/`append()` (around line 46). Every byte goes
through the state machine, and every byte of a field costs a `sof_vector_at()`
and a one-byte `sof_string_append_bytes()`.

- Change: while in a record and in the START, UNQUOTED or QUOTED state, find
  the run of ordinary bytes (not a quote, CR or LF, and not the delimiter
  outside quotes), 8 bytes at a time and then byte by byte, append it in one
  call, and set the state and position after it. Quotes, CR and LF still go
  through `take()`, so line counting, CRLF handling, `""`, strict mode's
  errors and the spans are unchanged, and if the append fails nothing has been
  taken, so the call can be made again after **SOF_CSV_NO_MEM**.
- Measured: 232 → 532 MB/s on a 22.7 MB plain file, 242 → 961 MB/s on prose
  in quoted fields.

**Writer.** A one-byte `append()` per delimiter and quote, and `needs_quotes`
looks at a byte at a time (_sof_csv_writer.c_:28, :75). With an 8-byte
`needs_quotes` and an inline append (see sof_string under Decisions): about
742 → 1060 MB/s (measured).

### sof_json

**`sof_json_number_double()` always formats the number again for `strtod()`**
(_sof_json_reader.c_:819, the `snprintf()` at :870).

- Change: Clinger's fast path. With 15 significant digits or fewer and a
  decimal exponent within ±22, the value is the digits as a double times or
  divided by an exact power of ten, which is one correctly rounded operation.
  Anything else takes the existing path. It must be guarded with
  `#if FLT_EVAL_METHOD == 0`, since x87 extended precision would round twice.
- Measured: 60.6 → 10.8 ns per number; 191 → 1028 MB/s over a numeric array.
  The tests pass.
- The same fits sof_toml's `float_convert()` (_sof_toml_doc.c_:553), which also
  calls `localeconv()` and copies the text for each float (not measured).

**Strings scanned one byte at a time**, in the reader's `scan_string()`
(_sof_json_reader.c_:528) and the writer's `append_escaped()`
(_sof_json_writer.c_:71).

- Change: skip 8 bytes at a time while they are printable ASCII and not `"` or
  `\`; any other byte drops to the existing code, so escapes, UTF-8 checks and
  errors are unchanged. In the reader the scan changes no state, so it stays
  resumable after **SOF_JSON_NO_MEM** and read errors.
- Measured: reader on string-heavy text 879 → about 1190 MB/s; writer
  strings about 1090 → 1520 MB/s. Records with short strings gain about 8%,
  since the cost per token dominates there.
- Not measured, same idea: `skip_space()` (:677) for indented input.

**Shortest double formatting**, `format_double()` (_sof_json_writer.c_:249,
and an identical copy at _sof_toml_writer.c_:185). It searches for the
shortest form with about four `snprintf("%.*g")` and `strtod()` pairs, and
macOS's `snprintf()` costs about 225 ns.

- Cheap change (prototyped): try `%.15g` first, which is the shortest form
  whenever it reads back, then `%.16g`, then 17; subnormals keep the old search
  (a test caught 5e-324). Output was byte for byte the same on about 3 million
  values. Measured: 573 → 278 ns for short values, 1215 → 952 ns for random
  ones.
- Real change: a shortest-round-trip algorithm such as Ryu or Schubfach,
  vendored under its license: about 30 ns (estimated 20–40x), and it would
  remove the locale workaround.
- Either way the two copies should become one.

**Document: two arena allocations per string or number**
(_sof_json_doc.c_:114 and `copy_text()`). Allocating the value and its text
together: +3–5% (measured).

**Smaller, not prototyped:** `top_is_object()` calls `sof_stack_peek()` in
another file up to three times per token (about 3% of the profile); keeping the
top byte in the reader would remove it. Handing back unescaped strings from the
input without copying would save about 15%, but `token.text` is promised to be
NUL-terminated, so it would change the API; not recommended.

Already fine: the reader's buffer refill (streaming is as fast as parsing
bytes), integer scanning and formatting, and the writer's indentation, which
writes spaces in blocks.

Overall with all the prototypes (measured): JSON reader tokens 530 → 601 MB/s on
records and 879 → 1333 on strings; document parse 361 → 395 and 708 → 1012;
writer compact 527 → 715 and indented 596 → 856.

### sof_toml

- `sof_utf8_validate()` over the whole text (_sof_toml_doc.c_:1510) was 17% of
  a parse; see sof_utf.
- `basic_string()` (:192) scans a byte at a time; the same 8-byte skip as
  sof_json, stopping at 0x7f too. Also candidates, not measured:
  `literal_string()` (:841), `comment()` (:232), the multi-line strings and the
  writer's `put_escaped()`.
- `sof_toml_doc_read()` reads 4 KB chunks onto the stack and appends them, an
  extra copy of about 4%; reading straight into the string's spare room needs
  a reserve function in sof_string.
- Floats: as sof_json's number fast path.
- Measured with these: parse 344 → 490 MB/s on 12.3 MB, writer 210 → 296 MB/s.

### sof_config, sof_copt

Nothing worth changing. sof_config works a byte at a time with four allocations
per entry, but configuration files are a few kilobytes; sof_copt searches its
options once per argument at startup.

## Text, memory and digests

### sof_utf

**No ASCII fast path.** `sof_utf8_validate()`, `sof_utf8_count()`,
`sof_utf8_to_utf16()` and `sof_utf8_to_utf32()` (all `static inline` in
_sof_utf.h_; validate at :643) decode every byte through the general decoder.

- Change: while 8 bytes remain, load them and test
  `(w & 0x8080808080808080) == 0`; if so, skip, count or widen all 8. Validate
  can also check 2-, 3- and 4-byte sequences inline. The conversions must keep
  their "once full, write nothing more" rule, so they take the 8-byte path
  only with room for 8 more.
- Measured on shakespeare.txt (5.6 MB, nearly all ASCII):

  | Function | Now | ASCII fast path |
  |---|---|---|
  | validate | 1.7–2.1 GB/s | 8.6–21 GB/s |
  | count | 3.1 GB/s | 9.7 GB/s |
  | to_utf16 | 1.6 GB/s | 6.75 GB/s |

- On 6.9 MB of mixed Cyrillic, CJK and emoji: validate +30% from the inline
  checks, count no gain, to_utf16 about 10% worse from the extra test. Trying
  the 8-byte test only after an ASCII byte should avoid that loss (not
  measured).
- Users: sof_toml validates every document, and the ngram demo its input.

### sof_digest

**SHA-256 compression is a plain loop over a 64-word schedule**
(_sof_digest.c_:55-90), with a call per 64-byte block. 436 MB/s, against
`shasum -a 256` at about 510 and `openssl dgst -sha256` at about 2.4 GB/s.

- Portable change: keep the schedule as a ring of 16 words, unroll the rounds
  8 at a time with the variables renamed rather than shifted, use the shorter
  forms of Ch and Maj, and compress several blocks per call so the state stays
  in registers. Measured: 429 → 581 MB/s (+35%), also at -O2. Output matched
  over 256 MB.
- ARMv8: the SHA-256 instructions through `<arm_neon.h>`, under
  `#if defined(__ARM_FEATURE_SHA2)`, which Apple clang defines for arm64.
  Measured: 2628 MB/s, 6.1x, level with OpenSSL. Compile-time only, so a
  generic Linux aarch64 build without `-march=armv8-a+crypto` gets the
  portable code.
- x86 SHA-NI could be added the same way under `#if defined(__SHA__)`; it takes
  effect only with `-msha` or `-march=native` (not measured, and not known
  whether morbius's i9 has it).

### sof_regex

**PCRE2's JIT is not used** (_sof_regex.c_, after `pcre2_compile()` in
`sof_regex_init()`). The pattern, match data and match context are already
made once and reused, so there is no allocation per call.

- Change: `pcre2_jit_compile(code, PCRE2_JIT_COMPLETE)`, ignoring failure,
  which leaves the interpreter as now.
- Measured, `find` over each line of Shakespeare:

  | Pattern | Interpreter | JIT |
  |---|---|---|
  | `love` | 798 MB/s | 1982 MB/s |
  | `[A-Z][a-z]+ing\b` | 756 | 1273 |
  | `\b(\w+)\s+\1\b` | 65 | 261 |
  | `(?i)king\|queen\|prince` | 634 | 886 |
  | `\d+` | 1157 | 1416 |

- This needs a decision; see Decisions.

### sof_mem, sof_alloc

Already fast; nothing worth changing. Measured costs per call through
`sof_alloc_t`, without touching the memory: arena 2.3 ns, slab 1.8, pool 2.5.
In the rbtree and hashmap measures of sof_mem's benchmark the allocator hardly
matters. Small items, each well under a nanosecond (estimated): store the
arena's large-block limit rather than computing it each call
(_sof_mem_arena.c_:236), a table or bit-length for the slab's size class
(_sof_mem_slab.c_:166), the pool's fast path `static inline` in
_sof_mem_internal.h_ for the slab, and the _sof_alloc.h_ helpers `static
inline` (about 0.4 ns per allocation). The tracker's 4–5 ns is its live list,
which it needs.

## Hashing and sets

### sof_bloom

**`sof_bloom_add()` reloads its struct and branches per bit**
(_sof_bloom.c_:64-74). `sof_bit_t` is `size_t`, the same type as `set`, `size`
and the struct's other counts, so a store through a word pointer may change
them, and they are read again each time; and the test-and-set branch
mispredicts for new keys.

- Change: copy `bits`, `size` and `hashes` into locals; count with
  `added += (w & mask) == 0; *word = w | mask;`; add `added` to `set` once.
  The same bits are set and the same counts kept.
- Measured, ns per add: 24.3 → 15.4 at a false positive rate of 10%,
  40.7 → 17.6 at 1%, 48.9 → 22.1 at 0.1%, 60.2 → 26.7 at 0.01%. Lookups
  unchanged.
- Tried and rejected: replacing `% size` in `probe_next()` with a conditional
  subtraction was 10–25% slower on arm64, where division is cheap; it might
  win on x86 (not measured).
- Larger option (estimated): a blocked Bloom filter, with a key's bits in one
  cache line, for filters larger than the cache: about k misses become one,
  at a slightly higher false positive rate for the same memory. A design
  change.

### sof_set

**Set operations hash every key again** (_sof_set.c_:39 `keep_where`, and
:104, :165, :185, :217, :248). Each key is looked up in the other set, which
computes its hash again and reads the key, though the element holds its hash.

- Change: `sof_hashmap_find_hashed()` and `sof_hashmap_insert_hashed()` taking
  the stored hash, used when both sets have the same hash callback and
  context; the same for subset and disjoint tests. The header should then say
  that a hash callback must give the same result for the same key and context,
  which is already assumed.
- Measured, ns per key on sets of 131k half overlapping: union 53.5 → 35.9,
  intersection 42.2 → 24.7, difference 40.0 → 22.8, symmetric difference
  62.9 → 41.8.

### sof_lru

**Eviction hashes and compares the evicted key again** (_sof_lru.c_:42,
`release()`). Keeping the hash in the node and removing from the map by
pointer from that slot: insert into a full cache 99.9 → 85.8 ns (measured).
Adds a field to a public struct and one hashmap function.

### sof_intern

Finding an existing string with the hashmap's probe written inline in
sof_intern: 42.4 → 30.2 ns (measured, stable). The cause isn't pinned down:
reusing the hash, or other ways of calling the equality check, gave nothing.
It walks the hashmap's elements itself, a layering shortcut, so measure it on
morbius before adopting.

### sof_hash

**Small stream updates take the general path** (_sof_hash.c_:92-125). An early
return when the bytes fit in the buffer: 1-byte updates 0.15 → 0.42 GB/s, 8-byte
1.86 → 3.20, 16-byte 3.55 → 6.03 (measured); 64 bytes and up unchanged, and the
hash is the same. The one-shot hash is already fast: about 7 ns per word, 72 GB/s
for large inputs.

### sof_hashmap

Sound as it is: linear probing, stored hashes that avoid almost every needless
compare, deletion by backward shift, growth without calling the hash callback.
The hash is mixed again on top of the callback's (_sof_hashmap.c_:65); skipping
that saves about 2 ns per operation (measured, 5–8%), but a weak user hash, such
as the identity on integers, would then cluster under linear probing. Not
recommended without a change to the documented contract.

## Containers and sorting

### sof_sort

All in _sof_sort.c_; measured with sof_sort's benchmark, ns per element.

1. **`swap()` copies a 4-byte element a byte at a time** (:314-333): its loop
   moves 8 bytes at a time and then single bytes. Fast paths for 4 and 8 bytes:
   quick 66 → 61 on 1M random, 30 → 24 at 1,000 elements, 23.5 → 15.3 on equal
   input; heap 88 → 80; merge 98 → 92.
2. **Merge copies each element with a `memcpy()` of a runtime size**
   (:213-224). Typed loops for 4 and 8 bytes, keeping the comparison that makes
   it stable (checked): merge 92 → 69 on 1M random, 17.5 → 9.4 on organ pipe,
   about 154 → 140 on the words.
3. **Median-of-3 pivot is slow on organ-pipe input** (:245-255): 80 ns against
   qsort's 27. Tukey's ninther for more than 128 elements: 80 → 37–41, random
   input unchanged.
4. **No fast path for sorted or equal input**: sorted 13.5 against qsort's 1.8,
   equal 15 against 1.2. pdqsort's methods (partial insertion sort after a
   partition with no swaps; putting runs equal to the previous pivot aside)
   would bring both to about 1–3 (estimated). Medium effort, subtle.

The benchmark tables in _sof_sort/README.md_ would need updating.

### sof_queue

**One allocation and free per element** (_sof_queue.c_:42, :50). With a
sof_mem pool as the queue's allocator: steady depth of 1,000, 9.0 → 4.0 ns per
operation; fill 1M then drain, 11.9 → 4.9 (measured). A list of recycled
elements inside the queue would give about the same without the caller doing
anything (estimated); at least, the README could suggest a pool.

### sof_vector

**`sof_vector_push()` goes through `sof_vector_insert()`** (_sof_vector.c_:144):
a position check, a `memmove()` of nothing, and a `memmove()` of the element.
With room, copying straight into the next slot: 4.8 → 2.9 ns per push of 10M
`uint32_t` (measured).

### sof_rbtree, sof_heap, sof_stack, sof_ring

Nothing measurable to gain. The heap's hole-based sift is good; bottom-up sift
and a 4-ary heap helped some cases and hurt others. The tree is bound by cache
misses and its compare callback; pooling its nodes was slower on macOS, whose
`malloc()` already serves 48-byte blocks from slabs (glibc not measured). One
small item, not measured: `sof_rbtree_get_keys()` (:139) allocates a stack of
count + 1 pointers, where 128 on the C stack always suffice, since the height is
at most 2·log2(n + 1); that also removes a way to fail. The stack and ring cost a
few nanoseconds per call, mostly the call itself, and the ring already wraps
without division.

## Utilities

### sof_log

**Direct build: a flush per line** (_sof_log.c_:466). To /dev/null:

| Version | ns per line |
|---|---|
| now | 1148 |
| one formatted buffer, fwrite and fflush | 1070 |
| the same without the fflush | 352 |
| also with the timestamp text kept for the same second | 194 |

To a real file: 1840 now, 383 without the flush. The flush is 70–80% of the
cost; see Decisions.

**Threaded build: every line is formatted twice** (_sof_log.c_:404-438), once to
measure it and once to fill it, and the writer thread is signalled for every
line. Measured cost to the calling thread:

| Version | ns per line |
|---|---|
| now | 760–790 |
| format once into a 512-byte buffer, falling back for longer lines | 455 |
| also write the timestamp digits by hand instead of with `snprintf()` | 372–380 |
| also signal only when the queue was empty | 327–331 |

Output unchanged. The hand-written timestamp also helps the direct build. Already
right: a message below the level costs nothing, since the level is checked before
the arguments are evaluated.

### sof_timer

**Microsecond resolution on macOS** (_sof_timer.c_:23). `CLOCK_MONOTONIC` there
ticks in whole microseconds (measured: every reading a multiple of 1,000 ns), so
an empty start and stop measures 0 ns 98% of the time, and calibration, which
takes the least of 1,000 back-to-back differences, is always 0.

- `CLOCK_MONOTONIC_RAW` (or `CLOCK_UPTIME_RAW`) on macOS ticks every 41 ns and
  costs 11.4 ns to read against 15.1 (measured). Guard it with
  `#if defined(__APPLE__) && defined(CLOCK_MONOTONIC_RAW)`, since Linux before
  5.3 made `CLOCK_MONOTONIC_RAW` a system call.
- Calibrate by timing many reads together and dividing.

The benchmarks here already time long runs, so this matters mostly to short
measurements.

### sof_random

- **Shuffle's swap calls `memcpy()` three times** (_sof_random.c_:64-75): the
  size is known only at run time. Fixed-size paths for 1, 2, 4, 8 and 16 bytes:
  6.1–7.0 → 2.4–2.6 ns per element (measured). The results are the same.
- **`sof_random_below()` divides twice** (:86-98). Lemire's multiply method
  made no difference here, since division on the M4 is cheap; on x86 64-bit
  division is slow, so it may win on morbius (not measured). It changes the
  results for a seed; see Decisions.
- **Exponential draws cost about 40 ns** (:35-56), a chain of 32 dependent
  squarings. A fixed-point ziggurat would be 5–10x (estimated), but changes
  every value the tests pin. Worth it only if they are hot.
- Already good: `sof_random_next()` (xoshiro256\*\*, 2.3 ns), `double` (2.2 ns),
  `bytes`.

### sof_bit

- **Without builtins, `highest` and `next` loop a bit at a time**
  (_sof_bit.h_:133, :165). Measured with `SOF_BIT_NO_BUILTINS`: `highest`
  15.7–19.1 → 0.84 ns with a six-step binary search; `next` up to 9.3 → 0.40 ns
  with a de Bruijn table. Only compilers without the GNU builtins, such as
  MSVC, use these. The fallbacks are tested only as `uint8_t`; a 64-bit build
  without builtins would be worth testing.
- **The documented loop over set bits** (_sof_bit.h_:147, and the sieve demo)
  calls `next(x, i + 1)` each time: 45 ns for a word with 32 bits set, against
  21 for clearing the lowest bit each time (measured). A `sof_bit_lowest()`, or
  documenting that loop, would fix it.
- With builtins, every function is one instruction or close.

### sof_debug

Three unbuffered writes per line to stderr (_sof_debug.c_:55-63). One formatted
buffer and one `fwrite()`: 2327 → 826 ns per line to /dev/null (measured), and a
line stays whole between processes. Low priority, since it only runs in DEBUG
builds; without DEBUG the macros are already nothing.

## Decisions

These need a choice before the change can be made.

1. **PCRE2's JIT and sof_alloc.** PCRE2 puts JIT code in executable memory of
   its own, not through the regex's `sof_alloc_t`, which breaks the rule that
   all memory goes through sof_alloc. Options: a flag to `sof_regex_init()`
   that turns it on, or on by default with the exception documented. It also
   adds tens of microseconds to compiling each pattern.
2. **sof_log flushing.** The header promises each line is written and flushed
   before the call returns. Options: keep that as the default and add a flag
   or field to turn it off, relying on `sof_log_flush()`; or leave flushing to
   the stream's buffering.
3. **A fast append in sof_string.** The writers of sof_json, sof_toml and
   sof_csv spend up to 45% of their time in one-byte to few-byte appends, each
   a call through `sof_string_append_bytes()`, `grow()` and `memmove()`. A
   `static inline` fast path in _sof_string.h_, or a reserve-and-write pair,
   would help every caller; the prototype wrote the string's fields directly,
   which other libraries may not. Measured with it: JSON writer 527 → 644 MB/s.
4. **sof_random's sequences.** Lemire's method for `below()` and a faster
   exponential both change the values for a seed, which the tests pin and the
   markov, cachesim and bank demos print. Worth it only if they measure faster
   on the machines that matter.

## Not measured

- Everything on Linux and x86-64. The findings most likely to differ there:
  `memcmp()`/`strcmp()` against inline code, 64-bit division (sof_bloom,
  sof_random), the pool for sof_rbtree's nodes, and the sof_intern result.
- The 8-byte scans in TOML literal strings, comments and multi-line strings, and
  in JSON whitespace.
- x86 SHA-NI.
