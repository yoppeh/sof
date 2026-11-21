#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program (match).
#   clean
#       Removes all object files and executables.
#   match
#       Builds the program.
# Variables:
#   CC
#       The C compiler to use. Defaults to gcc.
#   CFLAGS
#       Extra flags to pass to the C compiler. -Wall and PCRE2's include
#       flags are always added.
#   debug=1
#       Build with debug info
#   LDFLAGS
#       Extra flags to pass to the linker. PCRE2's library (libpcre2-8) is
#       always linked.
#
# PCRE2's flags come from pcre2-config if it is installed, else from
# pkg-config, else the library is linked as -lpcre2-8 from the compiler's
# own paths.
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_log -I../../sof_regex -I../../sof_ring -I../../sof_string

ifneq ($(shell command -v pcre2-config 2>/dev/null),)
PCRE2_CFLAGS := $(shell pcre2-config --cflags)
PCRE2_LIBS := $(shell pcre2-config --libs8)
else
PCRE2_CFLAGS := $(shell pkg-config --cflags libpcre2-8 2>/dev/null)
PCRE2_LIBS := $(shell pkg-config --libs libpcre2-8 2>/dev/null)
endif
ifeq ($(strip $(PCRE2_LIBS)),)
PCRE2_LIBS := -lpcre2-8
endif
override CFLAGS += $(PCRE2_CFLAGS)

.PHONY: all clean

all: match

clean:
	- rm -f match
	- rm -f *.o

match: main.o match.o sof_alloc.o sof_copt.o sof_log.o sof_regex.o sof_ring.o sof_string.o
	$(CC) $(LDFLAGS) $^ -o $@ $(PCRE2_LIBS)
ifndef debug
	strip $@
endif

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_log.o: ../../sof_log/sof_log.c ../../sof_log/sof_log.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_regex.o: ../../sof_regex/sof_regex.c ../../sof_regex/sof_regex.h ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_ring.o: ../../sof_ring/sof_ring.c ../../sof_ring/sof_ring.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c match.h ../../sof_alloc/sof_alloc.h ../../sof_copt/sof_copt.h ../../sof_log/sof_log.h ../../sof_regex/sof_regex.h ../../sof_ring/sof_ring.h ../../sof_string/sof_string.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c match.h ../../sof_alloc/sof_alloc.h ../../sof_regex/sof_regex.h ../../sof_string/sof_string.h
	$(CC) $(CFLAGS) -c $< -o $@
