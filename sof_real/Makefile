#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles the library
#       and links the tests (test and test-portable), the benchmark (bench)
#       and the demo.
#   clean
#       Removes all object files and executables, the demo's too.
#   bench
#       Builds the benchmark without running it.
#   check
#       Builds the tests and runs them, test and then test-portable; fails
#       if any test fails.
#   demo
#       Builds the library's demo, fpinfo, in demo/ (all does too).
#   memcheck
#       Runs the tests under valgrind (Linux); see ../test/test.mk.
#   sanitize
#       Runs the tests built with AddressSanitizer and
#       UndefinedBehaviorSanitizer; see ../test/test.mk.
#   test
#       Builds the tests without running them: test, with sof_real.c
#       compiled as usual, and test-portable, the same tests with
#       sof_real.c compiled with SOF_REAL_NO_BUILTINS, so it uses no
#       compiler builtins or unsigned __int128, as on compilers without them.
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
override CFLAGS += -I../sof_bit -I../sof_alloc -I../sof_copt -I../sof_random -I../sof_timer

TESTS = test test-portable

.PHONY: all clean demo

all: sof_real.o $(TESTS) bench demo

clean:
	- rm -f $(TESTS) bench
	- rm -f *.o
	$(MAKE) -C demo clean

demo:
	$(MAKE) -C demo

bench: bench.o sof_real.o sof_alloc.o sof_copt.o sof_random.o sof_timer.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@
ifndef debug
	strip $@
endif

test: test.o sof_real.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

test-portable: test.o sof_real-portable.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

bench.o: bench.c sof_real.h ../sof_alloc/sof_alloc.h ../sof_copt/sof_copt.h ../sof_random/sof_random.h ../sof_timer/sof_timer.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

sof_real.o: sof_real.c sof_real.h sof_real_internal.h ../sof_bit/sof_bit.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_real-portable.o: sof_real.c sof_real.h sof_real_internal.h ../sof_bit/sof_bit.h
	$(CC) $(CFLAGS) -D SOF_REAL_NO_BUILTINS -c $< -o $@

sof_alloc.o: ../sof_alloc/sof_alloc.c ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../sof_copt/sof_copt.c ../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_random.o: ../sof_random/sof_random.c ../sof_random/sof_random.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_timer.o: ../sof_timer/sof_timer.c ../sof_timer/sof_timer.h
	$(CC) $(CFLAGS) -c $< -o $@

test.o: test.c sof_real.h sof_real_internal.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

%.o: %.c sof_real.h
	$(CC) $(CFLAGS) -c $< -o $@

include ../test/test.mk
