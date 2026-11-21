#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (csvcut).
#   clean
#       Removes all object files and executables.
#   csvcut
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_csv -I../../sof_sort -I../../sof_string -I../../sof_vector

.PHONY: all clean

all: csvcut

clean:
	- rm -f csvcut
	- rm -f *.o

csvcut: main.o sof_alloc.o sof_copt.o sof_csv_reader.o sof_csv_writer.o sof_sort.o sof_string.o sof_vector.o
	$(CC) $(LDFLAGS) $^ -o $@
ifndef debug
	strip $@
endif

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_csv_reader.o: ../../sof_csv/sof_csv_reader.c ../../sof_csv/sof_csv.h ../../sof_alloc/sof_alloc.h ../../sof_string/sof_string.h ../../sof_vector/sof_vector.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_csv_writer.o: ../../sof_csv/sof_csv_writer.c ../../sof_csv/sof_csv.h ../../sof_alloc/sof_alloc.h ../../sof_string/sof_string.h ../../sof_vector/sof_vector.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_sort.o: ../../sof_sort/sof_sort.c ../../sof_sort/sof_sort.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_vector.o: ../../sof_vector/sof_vector.c ../../sof_vector/sof_vector.h ../../sof_alloc/sof_alloc.h ../../sof_sort/sof_sort.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c ../../sof_copt/sof_copt.h ../../sof_csv/sof_csv.h ../../sof_string/sof_string.h ../../sof_vector/sof_vector.h
	$(CC) $(CFLAGS) -c $< -o $@
