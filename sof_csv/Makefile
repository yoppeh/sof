#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles the library
#       (sof_csv_reader.o and sof_csv_writer.o) and links the tests (test).
#   clean
#       Removes all object files and the tests.
#   check
#       Builds the tests and runs them; fails if any test fails.
#   demo
#       Builds the library's demo, csvcut, in demo/ (all does too).
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
override CFLAGS += -I../sof_alloc -I../sof_sort -I../sof_string -I../sof_vector

TESTS = test

.PHONY: all clean demo

all: sof_csv_reader.o sof_csv_writer.o test demo

clean:
	- rm -f $(TESTS)
	- rm -f *.o
	$(MAKE) -C demo clean

demo:
	$(MAKE) -C demo

test: test.o sof_csv_reader.o sof_csv_writer.o sof_alloc.o sof_sort.o sof_string.o sof_vector.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

sof_alloc.o: ../sof_alloc/sof_alloc.c ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_sort.o: ../sof_sort/sof_sort.c ../sof_sort/sof_sort.h ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../sof_string/sof_string.c ../sof_string/sof_string.h ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_vector.o: ../sof_vector/sof_vector.c ../sof_vector/sof_vector.h ../sof_alloc/sof_alloc.h ../sof_sort/sof_sort.h
	$(CC) $(CFLAGS) -c $< -o $@

test.o: test.c sof_csv.h ../sof_alloc/sof_alloc.h ../sof_string/sof_string.h ../sof_vector/sof_vector.h ../test/test.h ../test/test_json.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

%.o: %.c sof_csv.h ../sof_alloc/sof_alloc.h ../sof_string/sof_string.h ../sof_vector/sof_vector.h
	$(CC) $(CFLAGS) -c $< -o $@

include ../test/test.mk
