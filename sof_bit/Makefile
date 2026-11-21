#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. The library is a
#       header only, so this checks that sof_bit.h compiles on its own,
#       without warning about the functions this check doesn't call, and
#       links the tests (test, test-uint8, test-uint32,
#       test-uint32-portable and test-uint64-portable) and the demo.
#   clean
#       Removes all object files and executables, the demo's too.
#   check
#       Builds the tests and runs them; fails if any test fails.
#   demo
#       Builds the library's demo, sieve, in demo/ (all does too).
#   memcheck
#       Runs the tests under valgrind (Linux); see ../test/test.mk.
#   sanitize
#       Runs the tests built with AddressSanitizer and
#       UndefinedBehaviorSanitizer; see ../test/test.mk.
#   test
#       Builds the tests without running them: test, with the default
#       sof_bit_t; test-uint32, with uint32_t; and, without the compiler's
#       builtins, test-uint8, test-uint32-portable and test-uint64-portable,
#       with uint8_t, uint32_t and uint64_t. All are built from test.c.
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

TESTS = test test-uint8 test-uint32 test-uint32-portable test-uint64-portable

.PHONY: all clean demo

all: $(TESTS) demo
	$(CC) $(CFLAGS) -Wno-unused-function -fsyntax-only -x c sof_bit.h

clean:
	- rm -f $(TESTS)
	- rm -f *.o
	$(MAKE) -C demo clean

demo:
	$(MAKE) -C demo

test: test.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

test-uint32: test-uint32.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

test-uint32-portable: test-uint32-portable.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

test-uint64-portable: test-uint64-portable.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

test-uint8: test-uint8.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

test-uint32.o: test.c sof_bit.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -D SOF_BIT_TYPE=uint32_t -c $< -o $@

test-uint32-portable.o: test.c sof_bit.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -D SOF_BIT_TYPE=uint32_t -D SOF_BIT_NO_BUILTINS -c $< -o $@

test-uint64-portable.o: test.c sof_bit.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -D SOF_BIT_TYPE=uint64_t -D SOF_BIT_NO_BUILTINS -c $< -o $@

test-uint8.o: test.c sof_bit.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -D SOF_BIT_TYPE=uint8_t -D SOF_BIT_NO_BUILTINS -c $< -o $@

test.o: test.c sof_bit.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

include ../test/test.mk
