#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (deps).
#   clean
#       Removes all object files and executables.
#   deps
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
override CFLAGS += -I../../sof_alloc -I../../sof_config -I../../sof_copt -I../../sof_hash -I../../sof_hashmap -I../../sof_log -I../../sof_queue -I../../sof_set -I../../sof_utf

.PHONY: all clean

all: deps

clean:
	- rm -f deps
	- rm -f *.o

deps: main.o deps.o sof_alloc.o sof_config.o sof_copt.o sof_hash.o sof_hashmap.o sof_log.o sof_queue.o sof_set.o
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

sof_log.o: ../../sof_log/sof_log.c ../../sof_log/sof_log.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_queue.o: ../../sof_queue/sof_queue.c ../../sof_queue/sof_queue.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_set.o: ../../sof_set/sof_set.c ../../sof_set/sof_set.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c deps.h ../../sof_alloc/sof_alloc.h ../../sof_config/sof_config.h ../../sof_copt/sof_copt.h ../../sof_hashmap/sof_hashmap.h ../../sof_log/sof_log.h ../../sof_queue/sof_queue.h ../../sof_set/sof_set.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c deps.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_queue/sof_queue.h ../../sof_set/sof_set.h
	$(CC) $(CFLAGS) -c $< -o $@
