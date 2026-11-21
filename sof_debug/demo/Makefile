#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (hanoi).
#   clean
#       Removes all object files and executables.
#   hanoi
#       Builds the program.
# Variables:
#   CC
#       The C compiler to use. Defaults to gcc.
#   CFLAGS
#       Extra flags to pass to the C compiler. -Wall is always added.
#   debug=1
#       Build with debug info, tracing the recursion to stderr
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_debug -I../../sof_stack

.PHONY: all clean

all: hanoi

clean:
	- rm -f hanoi
	- rm -f *.o

hanoi: main.o sof_alloc.o sof_copt.o sof_debug.o sof_stack.o
	$(CC) $(LDFLAGS) $^ -o $@
ifndef debug
	strip $@
endif

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_debug.o: ../../sof_debug/sof_debug.c ../../sof_debug/sof_debug.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_stack.o: ../../sof_stack/sof_stack.c ../../sof_stack/sof_stack.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c ../../sof_alloc/sof_alloc.h ../../sof_copt/sof_copt.h ../../sof_debug/sof_debug.h ../../sof_stack/sof_stack.h
	$(CC) $(CFLAGS) -c $< -o $@
