#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles the library
#       and links the tests (test) and the demo.
#   clean
#       Removes all object files and executables, the demo's too.
#   check
#       Builds the tests and runs them; fails if any test fails.
#   demo
#       Builds the library's demo, ngram, in demo/ (all does too).
#   memcheck
#       Runs the tests under valgrind (Linux); see ../test/test.mk.
#   sanitize
#       Runs the tests built with AddressSanitizer and
#       UndefinedBehaviorSanitizer; see ../test/test.mk.
#   test
#       Builds the tests without running them.
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
override CFLAGS += -I../sof_alloc -I../sof_hash

TESTS = test

.PHONY: all clean demo

all: sof_hashmap.o test demo

clean:
	- rm -f $(TESTS)
	- rm -f *.o
	$(MAKE) -C demo clean

demo:
	$(MAKE) -C demo

test: test.o sof_hashmap.o sof_alloc.o sof_hash.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

sof_alloc.o: ../sof_alloc/sof_alloc.c ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hash.o: ../sof_hash/sof_hash.c ../sof_hash/sof_hash.h ../sof_hash/rapidhash.h
	$(CC) $(CFLAGS) -c $< -o $@

test.o: test.c sof_hashmap.h ../sof_alloc/sof_alloc.h ../sof_hash/sof_hash.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

%.o: %.c sof_hashmap.h ../sof_alloc/sof_alloc.h ../sof_hash/sof_hash.h
	$(CC) $(CFLAGS) -c $< -o $@

include ../test/test.mk
