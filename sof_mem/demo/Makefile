#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (concord).
#   clean
#       Removes all object files and executables.
#   concord
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_mem -I../../sof_rbtree -I../../sof_string

.PHONY: all clean

all: concord

clean:
	- rm -f concord
	- rm -f *.o

concord: main.o concord.o sof_alloc.o sof_copt.o sof_mem_arena.o sof_mem_pool.o sof_mem_track.o sof_rbtree.o sof_string.o
	$(CC) $(LDFLAGS) $^ -o $@
ifndef debug
	strip $@
endif

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_mem_arena.o: ../../sof_mem/sof_mem_arena.c ../../sof_mem/sof_mem.h ../../sof_mem/sof_mem_internal.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_mem_pool.o: ../../sof_mem/sof_mem_pool.c ../../sof_mem/sof_mem.h ../../sof_mem/sof_mem_internal.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_mem_track.o: ../../sof_mem/sof_mem_track.c ../../sof_mem/sof_mem.h ../../sof_mem/sof_mem_internal.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_rbtree.o: ../../sof_rbtree/sof_rbtree.c ../../sof_rbtree/sof_rbtree.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c concord.h ../../sof_alloc/sof_alloc.h ../../sof_copt/sof_copt.h ../../sof_mem/sof_mem.h ../../sof_rbtree/sof_rbtree.h ../../sof_string/sof_string.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c concord.h ../../sof_alloc/sof_alloc.h ../../sof_mem/sof_mem.h ../../sof_rbtree/sof_rbtree.h
	$(CC) $(CFLAGS) -c $< -o $@
