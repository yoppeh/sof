#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. The library is a
#       header alone, so this checks that sof_utf.h compiles on its own
#       (without warning that its static inline functions are unused), and
#       links the tests (test).
#   clean
#       Removes all object files and the tests.
#   check
#       Builds the tests and runs them; fails if any test fails.
#   demo
#       Builds the library's demo, utfconv, in demo/ (all does too).
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
override CFLAGS += -I../sof_bit

TESTS = test

.PHONY: all clean demo

all: test demo
	$(CC) $(CFLAGS) -Wno-unused-function -fsyntax-only -x c sof_utf.h

clean:
	- rm -f $(TESTS)
	- rm -f *.o
	$(MAKE) -C demo clean

demo:
	$(MAKE) -C demo

test: test.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

test.o: test.c sof_utf.h ../sof_bit/sof_bit.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

include ../test/test.mk
