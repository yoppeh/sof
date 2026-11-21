#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (basenc).
#   clean
#       Removes all object files and executables.
#   basenc
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
override CFLAGS += -I../../sof_base -I../../sof_copt

.PHONY: all clean

all: basenc

clean:
	- rm -f basenc
	- rm -f *.o

basenc: main.o sof_base.o sof_copt.o
	$(CC) $(LDFLAGS) $^ -o $@
ifndef debug
	strip $@
endif

sof_base.o: ../../sof_base/sof_base.c ../../sof_base/sof_base.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c ../../sof_base/sof_base.h ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@
