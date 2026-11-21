#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (calc).
#   clean
#       Removes all object files and executables.
#   calc
#       Builds the program.
# Variables:
#   CC
#       The C compiler to use. Defaults to gcc.
#   CFLAGS
#       Extra flags to pass to the C compiler. -Wall is always added.
#   debug=1
#       Build with debug info, tracing the evaluator to stderr
#   LDFLAGS
#       Extra flags to pass to the linker. The math library is always
#       linked.
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_debug -I../../sof_hash -I../../sof_hashmap -I../../sof_stack -I../../sof_string

LIBS = -lm

.PHONY: all clean

all: calc

clean:
	- rm -f calc
	- rm -f *.o

calc: main.o calc.o sof_alloc.o sof_copt.o sof_debug.o sof_hash.o sof_hashmap.o sof_stack.o sof_string.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)
ifndef debug
	strip $@
endif

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_debug.o: ../../sof_debug/sof_debug.c ../../sof_debug/sof_debug.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hash.o: ../../sof_hash/sof_hash.c ../../sof_hash/sof_hash.h ../../sof_hash/rapidhash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hashmap.o: ../../sof_hashmap/sof_hashmap.c ../../sof_hashmap/sof_hashmap.h ../../sof_alloc/sof_alloc.h ../../sof_hash/sof_hash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_stack.o: ../../sof_stack/sof_stack.c ../../sof_stack/sof_stack.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c calc.h ../../sof_alloc/sof_alloc.h ../../sof_copt/sof_copt.h ../../sof_hashmap/sof_hashmap.h ../../sof_stack/sof_stack.h ../../sof_string/sof_string.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c calc.h ../../sof_alloc/sof_alloc.h ../../sof_debug/sof_debug.h ../../sof_hashmap/sof_hashmap.h ../../sof_stack/sof_stack.h ../../sof_string/sof_string.h
	$(CC) $(CFLAGS) -c $< -o $@
