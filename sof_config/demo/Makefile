#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (ini).
#   clean
#       Removes all object files and executables.
#   ini
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
override CFLAGS += -I../../sof_alloc -I../../sof_config -I../../sof_copt -I../../sof_hash -I../../sof_hashmap -I../../sof_utf

.PHONY: all clean

all: ini

clean:
	- rm -f ini
	- rm -f *.o

ini: main.o sof_alloc.o sof_config.o sof_copt.o sof_hash.o sof_hashmap.o
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

%.o: %.c ../../sof_alloc/sof_alloc.h ../../sof_config/sof_config.h ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@
