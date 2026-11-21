#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (markov).
#   clean
#       Removes all object files and executables.
#   markov
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
override CFLAGS += -I../../sof_hashmap/demo -I../../sof_alloc -I../../sof_copt -I../../sof_hash -I../../sof_hashmap -I../../sof_random -I../../sof_sort -I../../sof_string -I../../sof_utf

.PHONY: all clean

all: markov

clean:
	- rm -f markov
	- rm -f *.o

markov: main.o markov.o ngram.o sof_alloc.o sof_copt.o sof_hash.o sof_hashmap.o sof_random.o sof_sort.o sof_string.o
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

sof_random.o: ../../sof_random/sof_random.c ../../sof_random/sof_random.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_sort.o: ../../sof_sort/sof_sort.c ../../sof_sort/sof_sort.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

ngram.o: ../../sof_hashmap/demo/ngram.c ../../sof_hashmap/demo/ngram.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_sort/sof_sort.h ../../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c markov.h ../../sof_hashmap/demo/ngram.h ../../sof_alloc/sof_alloc.h ../../sof_copt/sof_copt.h ../../sof_hashmap/sof_hashmap.h ../../sof_random/sof_random.h ../../sof_string/sof_string.h ../../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -c $< -o $@

markov.o: markov.c markov.h ../../sof_hashmap/demo/ngram.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_random/sof_random.h ../../sof_sort/sof_sort.h ../../sof_string/sof_string.h ../../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c markov.h ../../sof_hashmap/demo/ngram.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_random/sof_random.h ../../sof_string/sof_string.h ../../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -c $< -o $@
