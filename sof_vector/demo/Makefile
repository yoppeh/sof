#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (numstat).
#   clean
#       Removes all object files and executables.
#   numstat
#       Builds the program.
# Variables:
#   CC
#       The C compiler to use. Defaults to gcc.
#   CFLAGS
#       Extra flags to pass to the C compiler. -Wall is always added.
#   debug=1
#       Build with debug info
#   LDFLAGS
#       Extra flags to pass to the linker. The program is always linked
#       with -lm, since it uses sqrt() and nextafter().
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_sort -I../../sof_vector

.PHONY: all clean

all: numstat

clean:
	- rm -f numstat
	- rm -f *.o

numstat: main.o sof_alloc.o sof_copt.o sof_sort.o sof_vector.o
	$(CC) $(LDFLAGS) $^ -lm -o $@
ifndef debug
	strip $@
endif

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_sort.o: ../../sof_sort/sof_sort.c ../../sof_sort/sof_sort.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_vector.o: ../../sof_vector/sof_vector.c ../../sof_vector/sof_vector.h ../../sof_alloc/sof_alloc.h ../../sof_sort/sof_sort.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c ../../sof_alloc/sof_alloc.h ../../sof_copt/sof_copt.h ../../sof_vector/sof_vector.h
	$(CC) $(CFLAGS) -c $< -o $@
