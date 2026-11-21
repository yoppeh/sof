#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles the library
#       and links the tests (test and test-portable) and the demo.
#   clean
#       Removes all object files and executables, the demo's too.
#   check
#       Builds the tests and runs them, test and then test-portable; fails
#       if any test fails.
#   demo
#       Builds the library's demo, markov, in demo/ (all does too).
#   memcheck
#       Runs the tests under valgrind (Linux); see ../test/test.mk.
#   sanitize
#       Runs the tests built with AddressSanitizer and
#       UndefinedBehaviorSanitizer; see ../test/test.mk.
#   test
#       Builds the tests without running them: test, with sof_random.c
#       compiled as usual, and test-portable, the same tests with
#       sof_random.c compiled with SOF_RANDOM_NO_BUILTINS, so it uses no
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

TESTS = test test-portable

.PHONY: all clean demo

all: sof_random.o test test-portable demo

clean:
	- rm -f $(TESTS)
	- rm -f *.o
	$(MAKE) -C demo clean

demo:
	$(MAKE) -C demo

test: test.o sof_random.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

test-portable: test.o sof_random-portable.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

sof_random-portable.o: sof_random.c sof_random.h
	$(CC) $(CFLAGS) -D SOF_RANDOM_NO_BUILTINS -c $< -o $@

test.o: test.c sof_random.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

%.o: %.c sof_random.h
	$(CC) $(CFLAGS) -c $< -o $@

include ../test/test.mk
