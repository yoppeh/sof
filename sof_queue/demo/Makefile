#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (bank).
#   clean
#       Removes all object files and executables.
#   bank
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
override CFLAGS += -I../../sof_alloc -I../../sof_config -I../../sof_copt -I../../sof_hash -I../../sof_hashmap -I../../sof_mem -I../../sof_queue -I../../sof_random -I../../sof_rbtree -I../../sof_utf

.PHONY: all clean

all: bank

clean:
	- rm -f bank
	- rm -f *.o

bank: main.o bank.o sof_alloc.o sof_config.o sof_copt.o sof_hash.o sof_hashmap.o sof_mem_pool.o sof_queue.o sof_random.o sof_rbtree.o
	$(CC) $(LDFLAGS) $^ -o $@
ifndef debug
	strip $@
endif

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_config.o: ../../sof_config/sof_config.c ../../sof_config/sof_config.h ../../sof_alloc/sof_alloc.h ../../sof_hash/sof_hash.h ../../sof_hashmap/sof_hashmap.h ../../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hash.o: ../../sof_hash/sof_hash.c ../../sof_hash/sof_hash.h ../../sof_hash/rapidhash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hashmap.o: ../../sof_hashmap/sof_hashmap.c ../../sof_hashmap/sof_hashmap.h ../../sof_alloc/sof_alloc.h ../../sof_hash/sof_hash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_mem_pool.o: ../../sof_mem/sof_mem_pool.c ../../sof_mem/sof_mem.h ../../sof_mem/sof_mem_internal.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_queue.o: ../../sof_queue/sof_queue.c ../../sof_queue/sof_queue.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_random.o: ../../sof_random/sof_random.c ../../sof_random/sof_random.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_rbtree.o: ../../sof_rbtree/sof_rbtree.c ../../sof_rbtree/sof_rbtree.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c bank.h ../../sof_alloc/sof_alloc.h ../../sof_config/sof_config.h ../../sof_copt/sof_copt.h ../../sof_mem/sof_mem.h ../../sof_queue/sof_queue.h ../../sof_random/sof_random.h ../../sof_rbtree/sof_rbtree.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c bank.h ../../sof_alloc/sof_alloc.h ../../sof_mem/sof_mem.h ../../sof_queue/sof_queue.h ../../sof_random/sof_random.h ../../sof_rbtree/sof_rbtree.h
	$(CC) $(CFLAGS) -c $< -o $@
