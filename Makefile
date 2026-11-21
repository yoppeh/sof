#
# Makefile
#
# Builds, tests and cleans every library in MODULES by running make in its
# directory, whose Makefile builds its tests, benchmark and demo too and runs
# its tests; and builds the libraries together as one static library,
# build/libsof.a, and installs it. Each library also builds and is tested on
# its own: see its README.md. The test harness all the libraries' tests share
# is in test/: test.h, and test.mk, the test targets their Makefiles include.
#
# Targets:
#   all
#       The default target, if no target is specified. Runs make in each
#       library's directory, building its tests and any benchmark or demo,
#       and builds build/libsof.a.
#   check
#       Runs make check in each library's directory, which builds and runs
#       its tests (sof_log's in two builds, sof_bit's in three). Goes on
#       through every library, then fails if any test failed.
#   clean
#       Removes every library's and demo's object files and executables,
#       its tests' included, and build/.
#   <library>
#       Builds that library alone, with its tests, benchmark and demo, for
#       example make sof_hashmap.
#   check-<library>, memcheck-<library>, sanitize-<library>
#       Runs check, memcheck or sanitize in that library's directory alone,
#       for example make check-sof_hashmap.
#   clean-<library>
#       Runs clean in that library's directory alone, which cleans its demo
#       too, for example make clean-sof_hashmap.
#   install
#       Builds build/libsof.a and build/sof.pc and copies them to
#       $(PREFIX)/lib and $(PREFIX)/lib/pkgconfig, and the headers of the
#       libraries in it to $(PREFIX)/include/sof, under $(DESTDIR) if it is
#       set.
#   lib
#       Builds build/libsof.a, one static library of every library in
#       MODULES but those named in without, and build/sof.pc, its
#       pkg-config file. Its objects are compiled into build/, so the
#       libraries' directories aren't written to.
#   memcheck
#       Runs make memcheck in each library's directory: its tests, built as
#       usual, under valgrind. Fails on any memory error or leak. Needs
#       valgrind, which runs on Linux but not on current macOS; see
#       test/test.mk.
#   sanitize
#       Runs make sanitize in each library's directory: its tests built with
#       debug info and AddressSanitizer and UndefinedBehaviorSanitizer,
#       failing on any error they find, and on Linux, on any leak. Each
#       library removes its tests and objects before and after, so no
#       sanitized objects are left to mix with an ordinary build.
#   test
#       The same as check, for tools that run make test.
#   uninstall
#       Removes what install copied.
# Variables:
#   AR
#       The archiver lib uses. Defaults to ar.
#   CC, CFLAGS, LDFLAGS, debug=1
#       Given on the command line, these are passed on to every library's
#       Makefile; see any of them for what they do.
#       lib compiles with CC and CFLAGS too, adding -Wall, an -I for every
#       library and -O3, or with debug=1, -ggdb -D DEBUG.
#   DESTDIR
#       A directory install and uninstall put before PREFIX, for staging.
#   PREFIX
#       Where install puts the library and headers, and where sof.pc says
#       they are. Defaults to /usr/local.
#   run
#       The libraries whose tests check, memcheck and sanitize run,
#       separated by spaces; all of them if it isn't set.
#   SANITIZE, VALGRIND
#       Given on the command line, passed on to every library's Makefile;
#       see test/test.mk.
#   threaded=1
#       lib compiles sof_log with SOF_LOG_THREADED, so it logs from a
#       thread of its own. Programs that use the library must then be
#       compiled with -D SOF_LOG_THREADED, since sof_log.h depends on it,
#       and linked with -pthread.
#   without
#       Libraries to leave out of build/libsof.a, separated by spaces, such
#       as without=sof_regex where PCRE2 isn't installed. A library that
#       another one in the archive uses can't be left out.
#

# Every library, each in a directory of its own, with its tests. Add a new
# library here, in alphabetical order.
MODULES = sof_alloc sof_base sof_bit sof_bloom sof_config sof_copt sof_csv sof_debug sof_digest sof_hash sof_hashmap sof_heap sof_intern sof_json sof_log sof_lru sof_mem sof_queue sof_random sof_rbtree sof_real sof_regex sof_ring sof_set sof_sort sof_stack sof_string sof_timer sof_toml sof_utf sof_vector

# Each library's Makefile builds and cleans its demo, so the demos aren't
# listed here: running both would build one twice at once under make -j.
DIRS = $(MODULES)

# The libraries whose tests check, memcheck and sanitize run.
TESTED = $(if $(run),$(run),$(MODULES))

# The single static library. Its flags are kept apart from CFLAGS, which
# this Makefile passes on to the others as it was given.
ifeq ($(origin CC),default)
CC = gcc
endif
PREFIX = /usr/local
VERSION = 0.0.1
BUILD = build
LIB = $(BUILD)/libsof.a
PC = $(BUILD)/sof.pc
LIB_MODULES = $(filter-out $(without),$(MODULES))
# Every library's .c files but its benchmark and its tests.
LIB_SOURCES = $(foreach f,$(wildcard $(LIB_MODULES:%=%/*.c)),$(if $(filter bench.c test.c test_%.c,$(notdir $(f))),,$(f)))
LIB_OBJECTS = $(addprefix $(BUILD)/,$(notdir $(LIB_SOURCES:.c=.o)))
LIB_HEADERS = $(filter-out %_internal.h,$(wildcard $(LIB_MODULES:%=%/sof_*.h)))
LIB_CFLAGS = -Wall $(if $(debug),-ggdb -D DEBUG,-O3) $(MODULES:%=-I%) $(CFLAGS)
ifdef threaded
LIB_CFLAGS += -pthread -D SOF_LOG_THREADED
endif

# What sof.pc gives a program that uses the library: the headers'
# directory, and the system libraries the libraries in it need. These are
# in Libs and Requires rather than their .private forms, since there is
# only the static library, so pkg-config --libs sof is enough without
# --static.
PC_CFLAGS = -I$${includedir}/sof$(if $(threaded), -D SOF_LOG_THREADED -pthread)
PC_LIBS = -L$${libdir} -lsof$(if $(filter sof_bloom,$(LIB_MODULES)), -lm)$(if $(threaded), -pthread)
PC_REQUIRES = $(if $(filter sof_regex,$(LIB_MODULES)),libpcre2-8)
vpath %.c $(LIB_MODULES)

# PCRE2's include flags, for sof_regex, from pcre2-config or pkg-config.
ifneq ($(filter sof_regex,$(LIB_MODULES)),)
ifneq ($(shell command -v pcre2-config 2>/dev/null),)
LIB_CFLAGS += $(shell pcre2-config --cflags)
else
LIB_CFLAGS += $(shell pkg-config --cflags libpcre2-8 2>/dev/null)
endif
endif

.PHONY: all check clean install lib memcheck sanitize test uninstall $(PC) $(DIRS) $(DIRS:%=clean-%) \
	$(MODULES:%=check-%) $(MODULES:%=memcheck-%) $(MODULES:%=sanitize-%)

all: $(DIRS) lib

# Each goes through every library, then fails if any did, naming them.
check memcheck sanitize:
	@failed=; for m in $(TESTED); do $(MAKE) -C $$m $@ || failed="$$failed $$m"; done; \
	if [ -n "$$failed" ]; then echo "make $@ failed in:$$failed"; exit 1; fi; \
	echo "make $@ passed in $(words $(TESTED)) libraries"

clean: $(DIRS:%=clean-%)
	- rm -rf $(BUILD)

install: $(LIB) $(PC)
	mkdir -p $(DESTDIR)$(PREFIX)/lib/pkgconfig $(DESTDIR)$(PREFIX)/include/sof
	cp $(LIB) $(DESTDIR)$(PREFIX)/lib/
	cp $(PC) $(DESTDIR)$(PREFIX)/lib/pkgconfig/
	cp $(LIB_HEADERS) $(DESTDIR)$(PREFIX)/include/sof/

lib: $(LIB) $(PC)

test: check

uninstall:
	- rm -f $(DESTDIR)$(PREFIX)/lib/libsof.a
	- rm -f $(DESTDIR)$(PREFIX)/lib/pkgconfig/sof.pc
	- rm -f $(addprefix $(DESTDIR)$(PREFIX)/include/sof/,$(notdir $(LIB_HEADERS)))
	- rmdir $(DESTDIR)$(PREFIX)/include/sof

$(DIRS):
	$(MAKE) -C $@

$(DIRS:%=clean-%):
	$(MAKE) -C $(@:clean-%=%) clean

$(MODULES:%=check-%):
	$(MAKE) -C $(@:check-%=%) check

$(MODULES:%=memcheck-%):
	$(MAKE) -C $(@:memcheck-%=%) memcheck

$(MODULES:%=sanitize-%):
	$(MAKE) -C $(@:sanitize-%=%) sanitize

$(LIB): $(LIB_OBJECTS)
	- rm -f $@
	$(AR) rcs $@ $^

# Written every time, since PREFIX, threaded and without change what it
# says without changing any file it could depend on.
$(PC): | $(BUILD)
	@echo 'writing $@'
	@{ echo 'prefix=$(PREFIX)'; \
	echo 'exec_prefix=$${prefix}'; \
	echo 'libdir=$${exec_prefix}/lib'; \
	echo 'includedir=$${prefix}/include'; \
	echo; \
	echo 'Name: sof'; \
	echo 'Description: A collection of small c libraries: data structures, hashing, parsing and other everyday tools.'; \
	echo 'Version: $(VERSION)'; \
	$(if $(PC_REQUIRES),echo 'Requires: $(PC_REQUIRES)';) \
	echo 'Cflags: $(PC_CFLAGS)'; \
	echo 'Libs: $(PC_LIBS)'; } > $@

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: %.c $(wildcard sof_*/*.h) | $(BUILD)
	$(CC) $(LIB_CFLAGS) -c $< -o $@
