#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links the program: logrun, or logrun-threaded with
#       threaded=1.
#   clean
#       Removes all object files and executables, of both builds.
#   logrun
#       Builds the program.
#   logrun-threaded
#       Builds the program with sof_log.c compiled with SOF_LOG_THREADED.
# Variables:
#   CC
#       The C compiler to use. Defaults to gcc.
#   CFLAGS
#       Extra flags to pass to the C compiler. -Wall is always added.
#   debug=1
#       Build with debug info
#   LDFLAGS
#       Extra flags to pass to the linker.
#   threaded=1
#       Build logrun-threaded instead of logrun: the same program, with
#       sof_log.c compiled with SOF_LOG_THREADED, writing from a thread of
#       its own, and linked with -pthread. Its object is named apart, so both
#       builds can live side by side.
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
override CFLAGS += -I../../sof_alloc -I../../sof_copt -I../../sof_log -I../../sof_string -I../../sof_timer

OBJECTS = main.o sof_alloc.o sof_copt.o sof_string.o sof_timer.o
ifdef threaded
PROGRAM = logrun-threaded
else
PROGRAM = logrun
endif

.PHONY: all clean

all: $(PROGRAM)

clean:
	- rm -f logrun logrun-threaded
	- rm -f *.o

logrun: $(OBJECTS) sof_log.o
	$(CC) $(LDFLAGS) $^ -o $@
ifndef debug
	strip $@
endif

logrun-threaded: $(OBJECTS) sof_log-threaded.o
	$(CC) $(LDFLAGS) -pthread $^ -o $@
ifndef debug
	strip $@
endif

sof_alloc.o: ../../sof_alloc/sof_alloc.c ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_copt.o: ../../sof_copt/sof_copt.c ../../sof_copt/sof_copt.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_log.o: ../../sof_log/sof_log.c ../../sof_log/sof_log.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_log-threaded.o: ../../sof_log/sof_log.c ../../sof_log/sof_log.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -pthread -D SOF_LOG_THREADED -c $< -o $@

sof_string.o: ../../sof_string/sof_string.c ../../sof_string/sof_string.h ../../sof_alloc/sof_alloc.h
	$(CC) $(CFLAGS) -c $< -o $@

sof_timer.o: ../../sof_timer/sof_timer.c ../../sof_timer/sof_timer.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c ../../sof_alloc/sof_alloc.h ../../sof_copt/sof_copt.h ../../sof_log/sof_log.h ../../sof_string/sof_string.h ../../sof_timer/sof_timer.h
	$(CC) $(CFLAGS) -c $< -o $@
