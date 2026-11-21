#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (uniq).
#   clean
#       Removes all object files and executables.
#   uniq
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
#       with -lm, since sof_bloom uses log(), ceil(), floor() and pow().
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
override CFLAGS += -I../../sof_alloc -I../../sof_bit -I../../sof_bloom -I../../sof_copt -I../../sof_hash -I../../sof_hashmap -I../../sof_sort -I../../sof_string -I../../sof_vector

.PHONY: all clean

all: uniq

clean:
	- rm -f uniq
	- rm -f *.o

uniq: main.o uniq.o sof_alloc.o sof_bloom.o sof_copt.o sof_hash.o sof_hashmap.o sof_sort.o sof_string.o sof_vector.o
	$(CC) $(LDFLAGS) $^ -lm -o $@
ifndef debug
	strip $@
endif

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_bloom.o: ../../sof_bloom/sof_bloom.c ../../sof_bloom/sof_bloom.h ../../sof_alloc/sof_alloc.h ../../sof_bit/sof_bit.h ../../sof_hash/sof_hash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hash.o: ../../sof_hash/sof_hash.c ../../sof_hash/sof_hash.h ../../sof_hash/rapidhash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hashmap.o: ../../sof_hashmap/sof_hashmap.c ../../sof_hashmap/sof_hashmap.h ../../sof_alloc/sof_alloc.h ../../sof_hash/sof_hash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_sort.o: ../../sof_sort/sof_sort.c ../../sof_sort/sof_sort.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_vector.o: ../../sof_vector/sof_vector.c ../../sof_vector/sof_vector.h ../../sof_alloc/sof_alloc.h ../../sof_sort/sof_sort.h
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c uniq.h ../../sof_alloc/sof_alloc.h ../../sof_bit/sof_bit.h ../../sof_bloom/sof_bloom.h ../../sof_copt/sof_copt.h ../../sof_hashmap/sof_hashmap.h ../../sof_string/sof_string.h ../../sof_vector/sof_vector.h
	$(CC) $(CFLAGS) -c $< -o $@

uniq.o: uniq.c uniq.h ../../sof_alloc/sof_alloc.h ../../sof_hash/sof_hash.h ../../sof_hashmap/sof_hashmap.h ../../sof_string/sof_string.h ../../sof_vector/sof_vector.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c uniq.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_string/sof_string.h ../../sof_vector/sof_vector.h
	$(CC) $(CFLAGS) -c $< -o $@
