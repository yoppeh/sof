# test

The test harness that every library's tests share, in c.

Each library's tests are a program of their own, in the library's directory: _test.c_, which its Makefile builds into _test_ and runs with `make check`. This directory holds what they share. _test.h_ is the harness: `CHECK()`, `run()`, `test_report()`, `next_random()`, and, for libraries that allocate, a test allocator that can fail any allocation and checks every free. Everything in it is `static`, so a test program includes it and needs nothing else. _test_json.h_, also all `static`, is a JSON checker for the tests that write or read JSON: `test_json_text()` says whether some bytes are a JSON text as RFC 8259 defines one, following its grammar production by production, and decodes it into a canonical form, so that two texts can be compared for what they hold; `test_json_lines()` checks output written a value to a line. _sof_json_'s tests check it against JSONTestSuite. _test.mk_ is the part of the Makefile every library shares: the `check`, `memcheck` and `sanitize` targets, which each library's Makefile includes at its end.

    static void test_insert(void) {
        CHECK(1 + 1 == 2);              // on failure: prints the check, counts it, returns
    }

    int main(void) {
        printf("sof_name:\n");
        run("insert", test_insert);     // prints "insert... ok"
        return test_report();           // "all tests passed" and 0, or "N test(s) failed" and 1
    }

## Building

There is nothing to build here. In a library's directory:

    make             # builds the library and its tests, ./test, among the rest
    make check       # builds and runs the tests
    make sanitize    # runs them with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs them under valgrind (Linux)

and from the root, `make check` and the rest run the same in every library; see the root _README.md_.

## A library's Makefile

A library's Makefile sets `TESTS` to its test programs, builds them in `all` and removes them in `clean`, and includes _test.mk_ last:

    TESTS = test
    ...
    test: test.o sof_name.o sof_alloc.o
    	$(CC) $(LDFLAGS) -pthread $^ -o $@

    test.o: test.c sof_name.h ../test/test.h
    	$(CC) $(CFLAGS) -pthread -c $< -o $@

    include ../test/test.mk

_test.mk_ adds `-I../test` to `CFLAGS`, and gives:

- `check`: builds the programs in `TESTS` and runs each in turn; fails if any test fails.
- `memcheck`: builds them as usual and runs each under valgrind, failing on any memory error or leak. Needs valgrind, which runs on Linux but not on current macOS. On Arch Linux it sets `DEBUGINFOD_URLS` to Arch's debuginfod server, if it isn't set, since valgrind needs glibc's symbols, which Arch strips.
- `sanitize`: removes the programs and the library's objects, builds the tests with `debug=1` and `sanitizers=1`, which adds `SANITIZE` (AddressSanitizer and UndefinedBehaviorSanitizer) to `CFLAGS` and `LDFLAGS`, runs them, and removes them again, so no sanitized objects are left to mix with an ordinary build.

A library tested in more than one build lists a program per build in `TESTS`: _sof_log_ builds _test-threaded_ as well, with _sof_log.c_ and _test.c_ compiled with `SOF_LOG_THREADED`, and _sof_bit_ builds _test-uint8_ and _test-uint32_, with _test.c_ compiled for other word types.

The tests open their data as _../test-data/name_, which works since they run from their library's directory.
