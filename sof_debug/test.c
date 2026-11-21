/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for the sof_debug library.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 *
 * The macros are tested first with DEBUG defined, writing to a
 * temporary file the tests read back, then with DEBUG undefined, after
 * including sof_debug.h again. Threads then trace into one file at once, and
 * every line is checked for whole text and its own thread's indentation.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/// @brief Where the macros write: a temporary file opened by start().
static FILE *out = NULL;

#ifndef DEBUG
#define DEBUG
#endif
#define SOF_DEBUG_STREAM out
#include "sof_debug.h"
#include "test.h"

/// @brief How the macros name this file, before the line number.
#define LOCATION "test.c:"

/// @brief The length of LOCATION.
#define LOCATION_LENGTH (sizeof(LOCATION) - 1)

/// @brief What the last trace wrote, read back by captured().
static char text[4096];

/// @brief Starts capturing the macros' output in a new temporary file.
static int start(void) {
    out = tmpfile();
    return out != NULL;
}

/// @brief Ends capturing, and returns what was written, in text.
static const char *captured(void) {
    size_t n = 0;
    if (out != NULL) {
        rewind(out);
        n = fread(text, 1, sizeof(text) - 1, out);
        fclose(out);
        out = NULL;
    }
    text[n] = '\0';
    return text;
}

/// @brief Ends capturing, and returns what was written with every
/// LOCATION "<line>: " taken out, leaving the indentation and messages.
static const char *captured_without_locations(void) {
    char *s = (char *)captured();
    char *to = s;
    const char *from = s;
    while (*from != '\0') {
        if (strncmp(from, LOCATION, LOCATION_LENGTH) == 0) {
            const char *p = from + LOCATION_LENGTH;
            while (*p >= '0' && *p <= '9') {
                p++;
            }
            if (p[0] == ':' && p[1] == ' ') {
                from = p + 2;
                continue;
            }
        }
        *to++ = *from++;
    }
    *to = '\0';
    return s;
}

static int inner(int x) {
    sof_debug_enter();
    sof_debug("x is %d\n", x);
    sof_debug_return(x * 2);
}

static int outer(int x) {
    sof_debug_enter();
    int y = inner(x);
    sof_debug("y is %d\n", y);
    sof_debug_return(y + 1);
}

/// @brief Uses sof_debug_return() in an if without braces, which the old
/// sof_debug_return broke.
static int pick(int x) {
    sof_debug_enter();
    if (x)
        sof_debug_return(1);
    sof_debug_return(2);
}

static int void_calls = 0;

static void returns_void(int early) {
    sof_debug_enter();
    void_calls++;
    if (early) {
        sof_debug_return();
    }
    sof_debug("late\n");
    sof_debug_leave();
}

static void test_debug(void) {
    char expected[256];
    CHECK(start());
    sof_debug("hello %s %d\n", "world", 42); int line = __LINE__;
    snprintf(expected, sizeof(expected), LOCATION "%d: hello world 42\n", line);
    CHECK(strcmp(captured(), expected) == 0);
    CHECK(sof_debug_depth() == 0);
}

static void test_enter_return(void) {
    CHECK(start());
    CHECK(outer(5) == 11);
    CHECK(sof_debug_depth() == 0);
    CHECK(strcmp(captured_without_locations(),
                 "outer()\n"
                 "    inner()\n"
                 "        x is 5\n"
                 "    y is 10\n") == 0);
}

static void test_return_unbraced(void) {
    CHECK(start());
    CHECK(pick(1) == 1);
    CHECK(sof_debug_depth() == 0);
    CHECK(pick(0) == 2);
    CHECK(sof_debug_depth() == 0);
    CHECK(strcmp(captured_without_locations(), "pick()\npick()\n") == 0);
}

static void test_void(void) {
    CHECK(start());
    void_calls = 0;
    returns_void(1);
    CHECK(sof_debug_depth() == 0);
    returns_void(0);
    CHECK(sof_debug_depth() == 0);
    CHECK(void_calls == 2);
    CHECK(strcmp(captured_without_locations(),
                 "returns_void()\nreturns_void()\n    late\n") == 0);
}

static void test_indent(void) {
    CHECK(start());
    sof_debug_indent_inc();
    CHECK(sof_debug_depth() == 1);
    sof_debug_indent_inc();
    CHECK(sof_debug_depth() == 2);
    sof_debug("two\n");
    sof_debug_indent_dec();
    sof_debug("one\n");
    sof_debug_indent_dec();
    CHECK(sof_debug_depth() == 0);
    sof_debug_indent_dec();
    CHECK(sof_debug_depth() == 0);
    sof_debug("zero\n");
    CHECK(strcmp(captured_without_locations(), "        two\n    one\nzero\n") == 0);
    sof_debug_indent(7);
    CHECK(sof_debug_depth() == 7);
    sof_debug_indent(-3);
    CHECK(sof_debug_depth() == 4);
    sof_debug_indent(-100);
    CHECK(sof_debug_depth() == 0);
    sof_debug_indent(0);
    CHECK(sof_debug_depth() == 0);
}

static void test_indent_max(void) {
    char expected[SOF_DEBUG_INDENT_MAX + 8];
    memset(expected, ' ', SOF_DEBUG_INDENT_MAX);
    strcpy(expected + SOF_DEBUG_INDENT_MAX, "deep\n");
    CHECK(start());
    sof_debug_indent(SOF_DEBUG_INDENT_MAX / SOF_DEBUG_INDENT_WIDTH + 10);
    sof_debug_print(out, NULL, 0, "deep\n");
    sof_debug_indent(-(SOF_DEBUG_INDENT_MAX / SOF_DEBUG_INDENT_WIDTH + 10));
    CHECK(sof_debug_depth() == 0);
    CHECK(strcmp(captured(), expected) == 0);
}

static void test_print_null(void) {
    sof_debug_print(NULL, "file.c", 1, "nothing %d\n", 1);
    CHECK(start());
    sof_debug_print(out, NULL, 0, "no location\n");
    sof_debug_print(out, "file.c", 12, NULL);
    sof_debug_indent(1);
    sof_debug_print(out, NULL, 0, NULL);
    sof_debug_indent(-1);
    CHECK(strcmp(captured(), "no location\nfile.c:12:     ") == 0);
}

static void test_print_lengths(void) {
    // A line is formatted into a 512-byte buffer and written at once, or, if
    // it is longer, written in parts. Either way it is what fprintf() gives,
    // for lines ending on either side of the buffer's end, a file longer
    // than the buffer, and any line number.
    static char message[700], file[600], expected[2048];
    for (size_t i = 0; i < sizeof(message) - 1; i++) {
        message[i] = (char)('a' + i % 26);
    }
    memset(file, 'f', sizeof(file) - 1);
    static const int lines[] = {0, 7, -7, 2147483647, -2147483647 - 1};
    for (int depth = 0; depth < 3; depth++) {
        sof_debug_indent(depth);
        for (size_t length = 400; length < 600; length++) {
            for (size_t l = 0; l < sizeof(lines) / sizeof(lines[0]); l++) {
                const char *name = length == 599 ? file : "src/file.c";
                CHECK(start());
                sof_debug_print(out, name, lines[l], "%.*s|\n", (int)length, message);
                snprintf(expected, sizeof(expected), "%*s%s:%d: %.*s|\n", depth * SOF_DEBUG_INDENT_WIDTH, "", name, lines[l], (int)length, message);
                int same = strcmp(captured(), expected) == 0;
                if (!same) {
                    sof_debug_indent(-depth);
                }
                CHECK(same);
            }
        }
        sof_debug_indent(-depth);
    }
    CHECK(sof_debug_depth() == 0);
}

/// @brief Threads, and steps each takes, in the threaded test.
#define THREADS 8
#define STEPS 4000

/// @brief The deepest a tracing thread goes, kept below the depth at which
/// SOF_DEBUG_INDENT_MAX limits the indentation.
#define MAX_DEPTH 10

/// @brief One tracing thread's state and results, checked by the main thread
/// after joining, since CHECK isn't safe on other threads.
typedef struct tracer_t {
    size_t id;
    uint64_t random;
    /// @brief The depth the thread expects sof_debug_depth() to return.
    size_t depth;
    /// @brief Tagged lines written, which numbers the next one.
    size_t seq;
    /// @brief Calls of step_into(), each writing one "step_into()" line.
    size_t entered;
    /// @brief Times sof_debug_depth() disagreed with depth.
    size_t errors;
} tracer_t;

/// @brief Fills payload with the text a tagged line carries for seq: a run of
/// one letter, of a length that varies with seq.
static void make_payload(size_t seq, char payload[64]) {
    size_t length = 10 + seq % 40;
    memset(payload, 'a' + (int)(seq % 26), length);
    payload[length] = '\0';
}

static void check_depth(tracer_t *t) {
    if (sof_debug_depth() != t->depth) {
        t->errors++;
    }
}

/// @brief Writes a line naming the thread, the line's number and the depth
/// the thread expects, so the reader can check the indentation.
static void write_tagged(tracer_t *t) {
    char payload[64];
    make_payload(t->seq, payload);
    sof_debug("thread %zu seq %zu depth %zu %s\n", t->id, t->seq, t->depth, payload);
    t->seq++;
}

/// @brief Traces through sof_debug_enter() and sof_debug_return(), one level deeper.
static void step_into(tracer_t *t) {
    sof_debug_enter();
    t->depth++;
    t->entered++;
    check_depth(t);
    write_tagged(t);
    t->depth--;
    sof_debug_return();
}

/// @brief Goes deeper and comes back at random, writing a tagged line at each
/// step and checking sof_debug_depth() against the depth it expects.
static void *trace(void *arg) {
    tracer_t *t = arg;
    check_depth(t);
    for (size_t i = 0; i < STEPS; i++) {
        uint64_t r = next_random(&t->random) % 3;
        if (r == 0 && t->depth < MAX_DEPTH) {
            sof_debug_indent_inc();
            t->depth++;
        } else if (r == 1 && t->depth > 0) {
            sof_debug_indent_dec();
            t->depth--;
        } else if (t->depth < MAX_DEPTH) {
            step_into(t);
        }
        check_depth(t);
        write_tagged(t);
    }
    sof_debug_indent(-(int)t->depth);
    t->depth = 0;
    check_depth(t);
    return NULL;
}

static void test_threads(void) {
    // Threads trace into one file at once, each at its own depth, while the
    // main thread sits at a depth of its own. Every line read back must be
    // whole, indented for the depth of the thread that wrote it, and each
    // thread's lines must come in the order it wrote them.
    tracer_t tracers[THREADS] = {{0}};
    pthread_t threads[THREADS];
    CHECK(start());
    sof_debug_indent(3);
    for (size_t i = 0; i < THREADS; i++) {
        tracers[i].id = i;
        tracers[i].random = 0x9e3779b97f4a7c15ULL * (i + 1);
        CHECK(pthread_create(&threads[i], NULL, trace, &tracers[i]) == 0);
    }
    for (size_t i = 0; i < THREADS; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    CHECK(sof_debug_depth() == 3);
    sof_debug_indent(-3);

    size_t next_seq[THREADS] = {0};
    size_t entered_lines = 0, bad = 0;
    char line[512];
    rewind(out);
    while (fgets(line, sizeof(line), out) != NULL) {
        size_t indent = strspn(line, " ");
        const char *p = line + indent;
        if (strncmp(p, LOCATION, LOCATION_LENGTH) != 0) {
            bad++;
            continue;
        }
        p += LOCATION_LENGTH;
        while (*p >= '0' && *p <= '9') {
            p++;
        }
        if (p[0] != ':' || p[1] != ' ') {
            bad++;
            continue;
        }
        p += 2;
        if (strcmp(p, "step_into()\n") == 0) {
            // Written at the depth step_into() was called at.
            if (indent % SOF_DEBUG_INDENT_WIDTH != 0 || indent / SOF_DEBUG_INDENT_WIDTH >= MAX_DEPTH) {
                bad++;
            }
            entered_lines++;
            continue;
        }
        size_t id, seq, depth;
        int n = 0;
        char expected[72];
        if (sscanf(p, "thread %zu seq %zu depth %zu %n", &id, &seq, &depth, &n) != 3 || n == 0 || id >= THREADS || seq != next_seq[id] || indent != depth * SOF_DEBUG_INDENT_WIDTH) {
            bad++;
            continue;
        }
        make_payload(seq, expected);
        strcat(expected, "\n");
        if (strcmp(p + n, expected) != 0) {
            bad++;
            continue;
        }
        next_seq[id]++;
    }
    fclose(out);
    out = NULL;

    size_t entered = 0;
    for (size_t i = 0; i < THREADS; i++) {
        CHECK(tracers[i].errors == 0);
        CHECK(tracers[i].seq >= STEPS);
        CHECK(next_seq[i] == tracers[i].seq);
        entered += tracers[i].entered;
    }
    CHECK(entered > 0);
    CHECK(entered_lines == entered);
    CHECK(bad == 0);
}

// From here on, the macros are compiled without DEBUG.
#undef DEBUG
#include "sof_debug.h"

/// @brief Counts evaluations of the disabled macros' arguments.
static int evaluated = 0;

static int count(void) {
    return ++evaluated;
}

static int off_pick(int x) {
    sof_debug_enter();
    if (x)
        sof_debug_return(10);
    else
        sof_debug("not written %d\n", count());
    sof_debug_indent_inc();
    sof_debug_indent_dec();
    sof_debug_return(20);
}

static void off_void(void) {
    sof_debug_enter();
    sof_debug_leave();
    sof_debug_return();
}

static void test_disabled(void) {
    CHECK(start());
    evaluated = 0;
    sof_debug("not written %d\n", count());
    CHECK(off_pick(1) == 10);
    CHECK(off_pick(0) == 20);
    off_void();
    CHECK(evaluated == 0);
    CHECK(sof_debug_depth() == 0);
    CHECK(strcmp(captured(), "") == 0);
    CHECK(count() == 1);
}

int main(void) {
    printf("sof_debug:\n");
    run("sof_debug writes the location and message", test_debug);
    run("sof_debug_enter and sof_debug_return indent and come back", test_enter_return);
    run("sof_debug_return in an if without braces", test_return_unbraced);
    run("sof_debug_return and sof_debug_leave in void functions", test_void);
    run("sof_debug_indent_inc, sof_debug_indent_dec and sof_debug_indent", test_indent);
    run("indentation is limited to SOF_DEBUG_INDENT_MAX", test_indent_max);
    run("sof_debug_print with NULL stream, file and format", test_print_null);
    run("lines of every length are written whole", test_print_lengths);
    run("threads trace at their own depths, in whole lines", test_threads);
    run("macros without DEBUG do nothing", test_disabled);
    return test_report();
}
