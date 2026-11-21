#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles the library
#       (sof_json_doc.o, sof_json_reader.o and sof_json_writer.o), links
#       the tests (test) and builds the demo.
#   clean
#       Removes all object files and the tests.
#   check
#       Builds the tests and runs them; fails if any test fails.
#   demo
#       Builds the library's demo, csvjson, in demo/ (all does too).
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
override CFLAGS += -I../sof_alloc -I../sof_bit -I../sof_hash -I../sof_hashmap -I../sof_mem -I../sof_real -I../sof_stack -I../sof_string -I../sof_utf

TESTS = test

# The library's own objects, and those of the libraries it uses.
OBJECTS = sof_json_doc.o sof_json_reader.o sof_json_writer.o
USED = sof_alloc.o sof_hash.o sof_hashmap.o sof_mem_arena.o sof_real.o sof_stack.o sof_string.o

# The headers every object here depends on, through sof_json.h.
HEADERS = sof_json.h ../sof_alloc/sof_alloc.h ../sof_hashmap/sof_hashmap.h ../sof_mem/sof_mem.h ../sof_stack/sof_stack.h ../sof_string/sof_string.h

.PHONY: all clean demo

all: $(OBJECTS) test demo

clean:
	- rm -f $(TESTS)
	- rm -f *.o
	$(MAKE) -C demo clean

demo:
	$(MAKE) -C demo

test: test.o $(OBJECTS) $(USED)
	$(CC) $(LDFLAGS) -pthread $^ -o $@

sof_json_doc.o: sof_json_doc.c $(HEADERS) ../sof_hash/sof_hash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_json_reader.o: sof_json_reader.c $(HEADERS) ../sof_real/sof_real.h ../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_json_writer.o: sof_json_writer.c $(HEADERS) ../sof_real/sof_real.h ../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_alloc.o: ../sof_alloc/sof_alloc.c ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hash.o: ../sof_hash/sof_hash.c ../sof_hash/sof_hash.h ../sof_hash/rapidhash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hashmap.o: ../sof_hashmap/sof_hashmap.c ../sof_hashmap/sof_hashmap.h ../sof_alloc/sof_alloc.h ../sof_hash/sof_hash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_mem_arena.o: ../sof_mem/sof_mem_arena.c ../sof_mem/sof_mem.h ../sof_mem/sof_mem_internal.h ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_real.o: ../sof_real/sof_real.c ../sof_real/sof_real.h ../sof_real/sof_real_internal.h ../sof_bit/sof_bit.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_stack.o: ../sof_stack/sof_stack.c ../sof_stack/sof_stack.h ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../sof_string/sof_string.c ../sof_string/sof_string.h ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

test.o: test.c $(HEADERS) ../sof_utf/sof_utf.h ../test/test.h ../test/test_json.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

include ../test/test.mk
