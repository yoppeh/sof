#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (basic).
#   clean
#       Removes all object files and executables.
#   basic
#       Builds the program.
# Variables:
#   CC
#       The C compiler to use. Defaults to gcc.
#   CFLAGS
#       Extra flags to pass to the C compiler. -Wall is always added.
#   debug=1
#       Build with debug info
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_hash -I../../sof_hashmap -I../../sof_random -I../../sof_rbtree -I../../sof_sort -I../../sof_stack -I../../sof_string -I../../sof_vector

LIBS = -lm

# The interpreter and the libraries it uses.
OBJECTS = basic.o basic_scan.o sof_alloc.o sof_hash.o sof_hashmap.o sof_random.o sof_rbtree.o sof_sort.o sof_stack.o sof_string.o sof_vector.o

# The headers basic.h includes.
HEADERS = basic.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_random/sof_random.h ../../sof_rbtree/sof_rbtree.h ../../sof_stack/sof_stack.h ../../sof_string/sof_string.h ../../sof_vector/sof_vector.h

.PHONY: all clean

all: basic

clean:
	- rm -f basic
	- rm -f *.o

basic: main.o sof_copt.o $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)
ifndef debug
	strip $@
endif

basic.o: basic.c basic_scan.h $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

basic_scan.o: basic_scan.c basic_scan.h $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hash.o: ../../sof_hash/sof_hash.c ../../sof_hash/sof_hash.h ../../sof_hash/rapidhash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hashmap.o: ../../sof_hashmap/sof_hashmap.c ../../sof_hashmap/sof_hashmap.h ../../sof_alloc/sof_alloc.h ../../sof_hash/sof_hash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_random.o: ../../sof_random/sof_random.c ../../sof_random/sof_random.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_rbtree.o: ../../sof_rbtree/sof_rbtree.c ../../sof_rbtree/sof_rbtree.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_sort.o: ../../sof_sort/sof_sort.c ../../sof_sort/sof_sort.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_stack.o: ../../sof_stack/sof_stack.c ../../sof_stack/sof_stack.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_vector.o: ../../sof_vector/sof_vector.c ../../sof_vector/sof_vector.h ../../sof_alloc/sof_alloc.h ../../sof_sort/sof_sort.h
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c $(HEADERS) ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@
