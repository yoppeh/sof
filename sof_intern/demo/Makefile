#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (index).
#   clean
#       Removes all object files and executables.
#   index
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_hash -I../../sof_hashmap -I../../sof_intern -I../../sof_rbtree -I../../sof_sort -I../../sof_string -I../../sof_utf -I../../sof_vector

.PHONY: all clean

all: index

clean:
	- rm -f index
	- rm -f *.o

index: main.o index.o sof_alloc.o sof_copt.o sof_hash.o sof_hashmap.o sof_intern.o sof_rbtree.o sof_sort.o sof_string.o sof_vector.o
	$(CC) $(LDFLAGS) $^ -o $@
ifndef debug
	strip $@
endif

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hash.o: ../../sof_hash/sof_hash.c ../../sof_hash/sof_hash.h ../../sof_hash/rapidhash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hashmap.o: ../../sof_hashmap/sof_hashmap.c ../../sof_hashmap/sof_hashmap.h ../../sof_alloc/sof_alloc.h ../../sof_hash/sof_hash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_intern.o: ../../sof_intern/sof_intern.c ../../sof_intern/sof_intern.h ../../sof_alloc/sof_alloc.h ../../sof_hash/sof_hash.h ../../sof_hashmap/sof_hashmap.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_rbtree.o: ../../sof_rbtree/sof_rbtree.c ../../sof_rbtree/sof_rbtree.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_sort.o: ../../sof_sort/sof_sort.c ../../sof_sort/sof_sort.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_vector.o: ../../sof_vector/sof_vector.c ../../sof_vector/sof_vector.h ../../sof_alloc/sof_alloc.h ../../sof_sort/sof_sort.h
	$(CC) $(CFLAGS) -c $< -o $@

index.o: index.c index.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_intern/sof_intern.h ../../sof_rbtree/sof_rbtree.h ../../sof_string/sof_string.h ../../sof_utf/sof_utf.h ../../sof_vector/sof_vector.h
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c index.h ../../sof_alloc/sof_alloc.h ../../sof_copt/sof_copt.h ../../sof_hashmap/sof_hashmap.h ../../sof_intern/sof_intern.h ../../sof_rbtree/sof_rbtree.h ../../sof_string/sof_string.h ../../sof_vector/sof_vector.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c index.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_intern/sof_intern.h ../../sof_rbtree/sof_rbtree.h ../../sof_string/sof_string.h ../../sof_vector/sof_vector.h
	$(CC) $(CFLAGS) -c $< -o $@
