#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (tomljson).
#   clean
#       Removes all object files and executables.
#   tomljson
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_hash -I../../sof_hashmap -I../../sof_json -I../../sof_mem -I../../sof_real -I../../sof_bit -I../../sof_stack -I../../sof_string -I../../sof_toml -I../../sof_utf

# The headers sof_json's and sof_toml's objects depend on, through
# sof_json.h and sof_toml.h.
COMMON_HEADERS = ../../sof_alloc/sof_alloc.h ../../sof_hashmap/sof_hashmap.h ../../sof_mem/sof_mem.h ../../sof_stack/sof_stack.h ../../sof_string/sof_string.h
JSON_HEADERS = ../../sof_json/sof_json.h $(COMMON_HEADERS)
TOML_HEADERS = ../../sof_toml/sof_toml.h $(COMMON_HEADERS)

.PHONY: all clean

all: tomljson

clean:
	- rm -f tomljson
	- rm -f *.o

tomljson: main.o sof_alloc.o sof_copt.o sof_hash.o sof_hashmap.o sof_json_doc.o sof_json_reader.o sof_json_writer.o sof_mem_arena.o sof_real.o sof_stack.o sof_string.o sof_toml_doc.o sof_toml_writer.o
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

sof_stack.o: ../../sof_stack/sof_stack.c ../../sof_stack/sof_stack.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_toml_doc.o: ../../sof_toml/sof_toml_doc.c $(TOML_HEADERS) ../../sof_hash/sof_hash.h ../../sof_real/sof_real.h ../../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_toml_writer.o: ../../sof_toml/sof_toml_writer.c $(TOML_HEADERS) ../../sof_real/sof_real.h ../../sof_utf/sof_utf.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c ../../sof_copt/sof_copt.h $(JSON_HEADERS) $(TOML_HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@
