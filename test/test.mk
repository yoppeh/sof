#
# test.mk
#
# The test targets every library's Makefile shares. A library's Makefile sets
# TESTS to its test programs (usually just test), gives the rules that build
# them, and includes this file at its end:
#
#   TESTS = test
#   test: test.o sof_name.o sof_alloc.o
#   	$(CC) $(LDFLAGS) -pthread $^ -o $@ $(TEST_LIBS)
#   ...
#   include ../test/test.mk
#
# Targets:
#   check
#       Builds the test programs and runs each in turn; fails if any test
#       fails.
#   memcheck
#       Builds the test programs as usual and runs each under valgrind. Fails
#       on any memory error or leak. Needs valgrind, which runs on Linux but
#       not on current macOS. On Arch Linux, DEBUGINFOD_URLS is set for it if
#       it isn't already, since valgrind needs glibc's symbols.
#   sanitize
#       Removes the test programs and objects, builds the tests with debug
#       info and AddressSanitizer and UndefinedBehaviorSanitizer, runs them,
#       and removes them again, so no sanitized objects are left to mix with
#       an ordinary build. Fails on any error the sanitizers find, and on
#       Linux, on any leak.
# Variables:
#   SANITIZE
#       The sanitizer flags sanitize compiles and links with.
#   sanitizers=1
#       Adds SANITIZE to CFLAGS and LDFLAGS; sanitize sets it, with debug=1.
#   VALGRIND
#       The valgrind command memcheck runs the tests with.
#   VALGRIND_FLAGS
#       Extra valgrind options a library's Makefile sets before including
#       this file, such as a suppressions file.
#
# The tests include test.h by name, from this directory, and open their data
# as ../test-data/name, so they run from their library's directory.
#

SANITIZE = -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
VALGRIND = valgrind -q --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=all $(VALGRIND_FLAGS)

override CFLAGS += -I../test
ifdef sanitizers
override CFLAGS += $(SANITIZE)
override LDFLAGS += $(SANITIZE)
endif

# Arch strips glibc's dynamic linker, and valgrind can't start without its
# symbols, which Arch serves through debuginfod.
ifeq ($(wildcard /etc/arch-release),/etc/arch-release)
DEBUGINFOD_URLS ?= https://debuginfod.archlinux.org
export DEBUGINFOD_URLS
endif

.PHONY: check memcheck sanitize

check: $(TESTS)
	@for t in $(TESTS); do echo "./$$t"; ./$$t || exit 1; done

memcheck: $(TESTS)
	@command -v valgrind >/dev/null || { echo "memcheck needs valgrind, which isn't installed"; exit 1; }
	@for t in $(TESTS); do echo "$(VALGRIND) ./$$t"; $(VALGRIND) ./$$t || exit 1; done

sanitize:
	- rm -f $(TESTS) *.o
	$(MAKE) debug=1 sanitizers=1 check; \
	status=$$?; rm -f $(TESTS) *.o; exit $$status
