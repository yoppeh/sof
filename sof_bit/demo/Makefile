#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (sieve).
#   clean
#       Removes all object files and executables.
#   sieve
#       Builds the program.
# Variables:
#   CC
#       The C compiler to use. Defaults to gcc.
#   CFLAGS
#       Extra flags to pass to the C compiler. -Wall is always added.
#   debug=1
#       Build with debug info
#   LDFLAGS
#       Extra flags to pass to the linker.
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
override CFLAGS += -I../../sof_alloc -I../../sof_bit -I../../sof_copt

.PHONY: all clean

all: sieve

clean:
	- rm -f sieve
	- rm -f *.o

sieve: main.o sieve.o sof_alloc.o sof_copt.o
	$(CC) $(LDFLAGS) $^ -o $@
ifndef debug
	strip $@
endif

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c sieve.h ../../sof_alloc/sof_alloc.h ../../sof_bit/sof_bit.h ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c sieve.h ../../sof_alloc/sof_alloc.h ../../sof_bit/sof_bit.h
	$(CC) $(CFLAGS) -c $< -o $@
