#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles the library
#       and links the tests (test and test-portable), the benchmarks (bench
#       and bench-portable) and the demo.
#   clean
#       Removes all object files and executables, the demo's too.
#   bench
#       Builds the benchmark without running it, with sof_digest.c compiled
#       as usual. Where zlib is installed, it measures its crc32() too, and
#       is linked with -lz.
#   bench-portable
#       Builds the same benchmark with sof_digest.c compiled with
#       SOF_DIGEST_PORTABLE, to measure the portable code.
#   check
#       Builds the tests and runs them, test and then test-portable; fails
#       if any test fails.
#   demo
#       Builds the library's demo, digestsum, in demo/ (all does too).
#   memcheck
#       Runs the tests under valgrind (Linux); see ../test/test.mk.
#   sanitize
#       Runs the tests built with AddressSanitizer and
#       UndefinedBehaviorSanitizer; see ../test/test.mk.
#   test
#       Builds the tests without running them: test, with sof_digest.c
#       compiled as usual, and test-portable, the same tests with
#       sof_digest.c compiled with SOF_DIGEST_PORTABLE, so the portable
#       code is tested where the usual build uses the processor's
#       instructions.
# Variables:
#   CC
#       The C compiler to use. Defaults to gcc.
#   CFLAGS
#       Extra flags to pass to the C compiler. -Wall is always added.
#   debug=1
#       Build with debug info
#   LDFLAGS
#       Extra flags to pass to the linker.
#   SANITIZE, sanitizers=1, VALGRIND
#       For the test targets; see ../test/test.mk.
#

ifeq ($(origin CC),default)
CC = gcc
endif
override CFLAGS += -Wall
ifdef debug
override CFLAGS += -ggdb -D DEBUG
else
override CFLAGS += -O3
endif
override CFLAGS += -I../sof_alloc -I../sof_copt -I../sof_random -I../sof_timer

# zlib, for the benchmarks alone, if a program can be linked with it.
ZLIB := $(shell echo 'int main(void) { return 0; }' | $(CC) -include zlib.h -x c - -lz -o /dev/null 2>/dev/null && echo yes)
ifeq ($(ZLIB),yes)
BENCH_CFLAGS = -D HAVE_ZLIB
BENCH_LIBS = -lz
endif

TESTS = test test-portable

.PHONY: all clean demo

all: sof_digest.o $(TESTS) bench bench-portable demo

clean:
	- rm -f $(TESTS) bench bench-portable
	- rm -f *.o
	$(MAKE) -C demo clean

demo:
	$(MAKE) -C demo

bench: bench.o sof_digest.o sof_alloc.o sof_copt.o sof_random.o sof_timer.o
	$(CC) $(LDFLAGS) $^ -o $@ $(BENCH_LIBS)
ifndef debug
	strip $@
endif

bench-portable: bench-portable.o sof_digest-portable.o sof_alloc.o sof_copt.o sof_random.o sof_timer.o
	$(CC) $(LDFLAGS) $^ -o $@ $(BENCH_LIBS)
ifndef debug
	strip $@
endif

test: test.o sof_digest.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

test-portable: test-portable.o sof_digest-portable.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

bench.o: bench.c sof_digest.h ../sof_alloc/sof_alloc.h ../sof_copt/sof_copt.h ../sof_random/sof_random.h ../sof_timer/sof_timer.h
	$(CC) $(CFLAGS) $(BENCH_CFLAGS) -c $< -o $@

bench-portable.o: bench.c sof_digest.h ../sof_alloc/sof_alloc.h ../sof_copt/sof_copt.h ../sof_random/sof_random.h ../sof_timer/sof_timer.h
	$(CC) $(CFLAGS) $(BENCH_CFLAGS) -D SOF_DIGEST_PORTABLE -c $< -o $@

sof_digest.o: sof_digest.c sof_digest.h sof_digest_internal.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_digest-portable.o: sof_digest.c sof_digest.h sof_digest_internal.h
	$(CC) $(CFLAGS) -D SOF_DIGEST_PORTABLE -c $< -o $@

sof_alloc.o: ../sof_alloc/sof_alloc.c ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../sof_copt/sof_copt.c ../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_random.o: ../sof_random/sof_random.c ../sof_random/sof_random.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_timer.o: ../sof_timer/sof_timer.c ../sof_timer/sof_timer.h
	$(CC) $(CFLAGS) -c $< -o $@

test.o: test.c sof_digest.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

test-portable.o: test.c sof_digest.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -D SOF_DIGEST_PORTABLE -c $< -o $@

%.o: %.c sof_digest.h
	$(CC) $(CFLAGS) -c $< -o $@

include ../test/test.mk
