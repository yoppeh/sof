#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles the library
#       (sof_regex.o) and links the tests (test) and the demo.
#   clean
#       Removes all object files and executables, the demo's too.
#   check
#       Builds the tests and runs them; fails if any test fails.
#   demo
#       Builds the library's demo, match, in demo/ (all does too).
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
#       Extra flags to pass to the C compiler. -Wall and PCRE2's include
#       flags are always added.
#   debug=1
#       Build with debug info
#   LDFLAGS
#       Extra flags to pass to the linker. The tests are always linked with
#       PCRE2's library, libpcre2-8.
#   SANITIZE, sanitizers=1, VALGRIND
#       For the test targets; see ../test/test.mk. VALGRIND_FLAGS adds
#       valgrind.supp's suppressions, for PCRE2's JIT code.
#
# PCRE2's flags come from pcre2-config if it is installed, else from
# pkg-config, else no include flags are added and the library is linked as
# -lpcre2-8 from the compiler's own paths. A program that uses the library
# links with PCRE2's library, libpcre2-8.
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
override CFLAGS += -I../sof_alloc -I../sof_string

ifneq ($(shell command -v pcre2-config 2>/dev/null),)
PCRE2_CFLAGS := $(shell pcre2-config --cflags)
PCRE2_LIBS := $(shell pcre2-config --libs8)
else
PCRE2_CFLAGS := $(shell pkg-config --cflags libpcre2-8 2>/dev/null)
PCRE2_LIBS := $(shell pkg-config --libs libpcre2-8 2>/dev/null)
endif
ifeq ($(strip $(PCRE2_LIBS)),)
PCRE2_LIBS := -lpcre2-8
endif
override CFLAGS += $(PCRE2_CFLAGS)

TESTS = test

# The machine code from sof_regex_jit() reads past the end of a subject,
# which valgrind would report.
VALGRIND_FLAGS = --suppressions=valgrind.supp

.PHONY: all clean demo

all: sof_regex.o test demo

clean:
	- rm -f $(TESTS)
	- rm -f *.o
	$(MAKE) -C demo clean

demo:
	$(MAKE) -C demo

test: test.o sof_regex.o sof_string.o sof_alloc.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@ $(PCRE2_LIBS)

sof_alloc.o: ../sof_alloc/sof_alloc.c ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../sof_string/sof_string.c ../sof_string/sof_string.h ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

test.o: test.c sof_regex.h ../sof_string/sof_string.h ../sof_alloc/sof_alloc.h ../test/test.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

%.o: %.c sof_regex.h ../sof_string/sof_string.h ../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

include ../test/test.mk
