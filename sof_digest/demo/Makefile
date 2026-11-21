#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (digestsum).
#   clean
#       Removes all object files and executables.
#   digestsum
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_digest -I../../sof_string

.PHONY: all clean

all: digestsum

clean:
	- rm -f digestsum
	- rm -f *.o

digestsum: main.o sof_alloc.o sof_copt.o sof_digest.o sof_string.o
	$(CC) $(LDFLAGS) $^ -o $@
ifndef debug
	strip $@
endif

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_digest.o: ../../sof_digest/sof_digest.c ../../sof_digest/sof_digest.h ../../sof_digest/sof_digest_internal.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c ../../sof_alloc/sof_alloc.h ../../sof_copt/sof_copt.h ../../sof_digest/sof_digest.h ../../sof_string/sof_string.h
	$(CC) $(CFLAGS) -c $< -o $@
