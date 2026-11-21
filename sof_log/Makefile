#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles the library
#       and links the tests (test and test-threaded) and the demo.
#   clean
#       Removes all object files and executables, the demo's too.
#   check
#       Builds the tests and runs them, test and then test-threaded; fails
#       if any test fails.
#   demo
#       Builds the library's demo, logrun, in demo/ (all does too), or
#       logrun-threaded with threaded=1.
#   memcheck
#       Runs the tests under valgrind (Linux); see ../test/test.mk.
#   sanitize
#       Runs the tests built with AddressSanitizer and
#       UndefinedBehaviorSanitizer; see ../test/test.mk.
#   test
#       Builds the tests without running them: test, with sof_log.c
#       compiled as usual, and test-threaded, the same tests with sof_log.c
#       compiled with SOF_LOG_THREADED, writing from a thread of its own.
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
#   threaded=1
#       Build the demo as logrun-threaded, with sof_log.c compiled with
#       SOF_LOG_THREADED. The library's own object is compiled as usual,
#       and the tests are built both ways whether it is given or not.
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
override CFLAGS += -I../sof_alloc

TESTS = test test-threaded

.PHONY: all clean demo

all: sof_log.o test test-threaded demo

clean:
	- rm -f $(TESTS)
	- rm -f *.o
	$(MAKE) -C demo clean

demo:
	$(MAKE) -C demo

test: test.o sof_log.o sof_alloc.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

test-threaded: test-threaded.o sof_log-threaded.o sof_alloc.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

sof_alloc.o: ../sof_alloc/sof_alloc.c ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_log-threaded.o: sof_log.c sof_log.h ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -pthread -D SOF_LOG_THREADED -c $< -o $@

test-threaded.o: test.c sof_log.h ../sof_alloc/sof_alloc.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -D SOF_LOG_THREADED -c $< -o $@

test.o: test.c sof_log.h ../sof_alloc/sof_alloc.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

%.o: %.c sof_log.h ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

include ../test/test.mk
