#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles the library,
#       builds its archive (libsof_stack.a) and links the tests (test).
#   clean
#       Removes all object files, the library and the tests.
#   check
#       Builds the tests and runs them; fails if any test fails.
#   demo
#       Builds the library's demo, calc, in demo/ (all does too).
#   lib
#       Builds the library, libsof_stack.a. A program linked with the
#       archive gets only the objects it uses.
#   memcheck
#       Runs the tests under valgrind (Linux); see ../test/test.mk.
#   sanitize
#       Runs the tests built with AddressSanitizer and
#       UndefinedBehaviorSanitizer; see ../test/test.mk.
#   test
#       Builds the tests without running them.
# Variables:
#   AR
#       The archiver. Defaults to ar.
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
override CFLAGS += -I../sof_alloc

LIB = libsof_stack.a
LIB_OBJECTS = sof_stack.o
TESTS = test

.PHONY: all clean demo lib

all: $(LIB) test demo

clean:
	- rm -f $(LIB) $(TESTS)
	- rm -f *.o
	$(MAKE) -C demo clean

demo:
	$(MAKE) -C demo

lib: $(LIB)

$(LIB): $(LIB_OBJECTS)
	- rm -f $@
	$(AR) rcs $@ $^

test: test.o $(LIB) sof_alloc.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@

sof_alloc.o: ../sof_alloc/sof_alloc.c ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

test.o: test.c sof_stack.h ../sof_alloc/sof_alloc.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

%.o: %.c sof_stack.h ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

include ../test/test.mk
