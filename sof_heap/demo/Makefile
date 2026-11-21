#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (top) and the benchmark (bench).
#   clean
#       Removes all object files and executables.
#   bench
#       Builds the benchmark without running it.
#   top
#       Builds the program.
# Variables:
#   CC
#       The C compiler to use. Defaults to gcc.
#   CFLAGS
#       Extra flags to pass to the C compiler. -Wall and PCRE2's include
#       flags, which sof_regex needs, are always added.
#   debug=1
#       Build with debug info
#   LDFLAGS
#       Extra flags to pass to the linker. PCRE2's library (libpcre2-8) is
#       always linked, for sof_regex.
#
# PCRE2's flags come from pcre2-config if it is installed, else from
# pkg-config, else the library is linked as -lpcre2-8 from the compiler's
# own paths.
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_hash -I../../sof_hashmap -I../../sof_heap -I../../sof_regex -I../../sof_sort -I../../sof_string -I../../sof_timer

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

LIBS = $(PCRE2_LIBS)

.PHONY: all clean

all: top bench

clean:
	- rm -f top bench
	- rm -f *.o

top: main.o top.o sof_alloc.o sof_copt.o sof_hash.o sof_hashmap.o sof_heap.o sof_regex.o sof_string.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)
ifndef debug
	strip $@
endif

bench: bench.o top.o sof_alloc.o sof_copt.o sof_hash.o sof_hashmap.o sof_heap.o sof_regex.o sof_sort.o sof_string.o sof_timer.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)
ifndef debug
	strip $@
endif

bench.o: bench.c top.h ../../sof_alloc/sof_alloc.h ../../sof_copt/sof_copt.h ../../sof_hashmap/sof_hashmap.h ../../sof_regex/sof_regex.h ../../sof_sort/sof_sort.h ../../sof_string/sof_string.h ../../sof_timer/sof_timer.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hash.o: ../../sof_hash/sof_hash.c ../../sof_hash/sof_hash.h ../../sof_hash/rapidhash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hashmap.o: ../../sof_hashmap/sof_hashmap.c ../../sof_hashmap/sof_hashmap.h ../../sof_alloc/sof_alloc.h ../../sof_hash/sof_hash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_heap.o: ../../sof_heap/sof_heap.c ../../sof_heap/sof_heap.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_sort.o: ../../sof_sort/sof_sort.c ../../sof_sort/sof_sort.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_regex.o: ../../sof_regex/sof_regex.c ../../sof_regex/sof_regex.h ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_timer.o: ../../sof_timer/sof_timer.c ../../sof_timer/sof_timer.h
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c top.h ../../sof_alloc/sof_alloc.h ../../sof_copt/sof_copt.h ../../sof_hashmap/sof_hashmap.h ../../sof_regex/sof_regex.h ../../sof_string/sof_string.h
	$(CC) $(CFLAGS) -c $< -o $@

top.o: top.c top.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_heap/sof_heap.h ../../sof_regex/sof_regex.h ../../sof_string/sof_string.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c top.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_regex/sof_regex.h ../../sof_string/sof_string.h
	$(CC) $(CFLAGS) -c $< -o $@
