#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (ngram).
#   clean
#       Removes all object files and executables.
#   ngram
#       Builds the program. It can count on several threads, so it is
#       compiled and linked with -pthread.
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_hash -I../../sof_hashmap -I../../sof_queue -I../../sof_sort -I../../sof_utf

.PHONY: all clean

all: ngram

clean:
	- rm -f ngram
	- rm -f *.o

ngram: main.o ngram.o sof_alloc.o sof_copt.o sof_hash.o sof_hashmap.o sof_queue.o sof_sort.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@
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

main.o: main.c ngram.h ../../sof_alloc/sof_alloc.h ../../sof_copt/sof_copt.h ../../sof_hashmap/sof_hashmap.h ../../sof_queue/sof_queue.h ../../sof_sort/sof_sort.h ../../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

sof_queue.o: ../../sof_queue/sof_queue.c ../../sof_queue/sof_queue.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_sort.o: ../../sof_sort/sof_sort.c ../../sof_sort/sof_sort.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

ngram.o: ngram.c ngram.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_sort/sof_sort.h ../../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c ngram.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -c $< -o $@
