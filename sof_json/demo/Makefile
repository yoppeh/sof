#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (csvjson).
#   clean
#       Removes all object files and executables.
#   csvjson
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_csv -I../../sof_hash -I../../sof_hashmap -I../../sof_json -I../../sof_mem -I../../sof_real -I../../sof_bit -I../../sof_sort -I../../sof_stack -I../../sof_string -I../../sof_utf -I../../sof_vector

# The headers sof_json's objects depend on, through sof_json.h.
JSON_HEADERS = ../../sof_json/sof_json.h ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_mem/sof_mem.h ../../sof_stack/sof_stack.h ../../sof_string/sof_string.h

.PHONY: all clean

all: csvjson

clean:
	- rm -f csvjson
	- rm -f *.o

csvjson: main.o sof_alloc.o sof_copt.o sof_csv_reader.o sof_csv_writer.o sof_hash.o sof_hashmap.o sof_json_doc.o sof_json_reader.o sof_json_writer.o sof_mem_arena.o sof_real.o sof_sort.o sof_stack.o sof_string.o sof_vector.o
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

sof_hash.o: ../../sof_hash/sof_hash.c ../../sof_hash/sof_hash.h ../../sof_hash/rapidhash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_hashmap.o: ../../sof_hashmap/sof_hashmap.c ../../sof_hashmap/sof_hashmap.h ../../sof_alloc/sof_alloc.h ../../sof_hash/sof_hash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_json_doc.o: ../../sof_json/sof_json_doc.c $(JSON_HEADERS) ../../sof_hash/sof_hash.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_json_reader.o: ../../sof_json/sof_json_reader.c $(JSON_HEADERS) ../../sof_real/sof_real.h ../../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_json_writer.o: ../../sof_json/sof_json_writer.c $(JSON_HEADERS) ../../sof_real/sof_real.h ../../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_mem_arena.o: ../../sof_mem/sof_mem_arena.c ../../sof_mem/sof_mem.h ../../sof_mem/sof_mem_internal.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_real.o: ../../sof_real/sof_real.c ../../sof_real/sof_real.h ../../sof_real/sof_real_internal.h ../../sof_bit/sof_bit.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_sort.o: ../../sof_sort/sof_sort.c ../../sof_sort/sof_sort.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_stack.o: ../../sof_stack/sof_stack.c ../../sof_stack/sof_stack.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_vector.o: ../../sof_vector/sof_vector.c ../../sof_vector/sof_vector.h ../../sof_alloc/sof_alloc.h ../../sof_sort/sof_sort.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c ../../sof_copt/sof_copt.h ../../sof_csv/sof_csv.h $(JSON_HEADERS) ../../sof_vector/sof_vector.h
	$(CC) $(CFLAGS) -c $< -o $@
