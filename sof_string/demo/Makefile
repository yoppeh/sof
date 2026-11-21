#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (spell).
#   clean
#       Removes all object files and executables.
#   spell
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_hash -I../../sof_hashmap -I../../sof_log -I../../sof_sort -I../../sof_string -I../../sof_timer

.PHONY: all clean

all: spell

clean:
	- rm -f spell
	- rm -f *.o

spell: main.o spell.o sof_alloc.o sof_copt.o sof_hash.o sof_hashmap.o sof_log.o sof_sort.o sof_string.o sof_timer.o
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

sof_log.o: ../../sof_log/sof_log.c ../../sof_log/sof_log.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_sort.o: ../../sof_sort/sof_sort.c ../../sof_sort/sof_sort.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_timer.o: ../../sof_timer/sof_timer.c ../../sof_timer/sof_timer.h
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c spell.h ../../sof_alloc/sof_alloc.h ../../sof_copt/sof_copt.h ../../sof_hashmap/sof_hashmap.h ../../sof_log/sof_log.h ../../sof_string/sof_string.h ../../sof_timer/sof_timer.h
	$(CC) $(CFLAGS) -c $< -o $@

spell.o: spell.c spell.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_sort/sof_sort.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c spell.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h
	$(CC) $(CFLAGS) -c $< -o $@
