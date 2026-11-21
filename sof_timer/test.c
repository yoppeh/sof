/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_timer.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <stdio.h>

#include "sof_timer.h"
#include "test.h"

/// @brief Two milliseconds, in nanoseconds: the interval the tests measure.
#define INTERVAL 2000000u

/// @brief The most any interval measured here may take, in nanoseconds, so
/// that a clock reading garbage fails the tests: two seconds.
#define LIMIT 2000000000u

/// @brief Busy-waits until the running timer t has run for ns nanoseconds.
static void spin(const sof_timer_t *t, uint64_t ns) {
    while (sof_timer_elapsed_ns(t) < ns) {
    }
}

static void test_init(void) {
    sof_timer_t t;
    CHECK(sof_timer_init(&t) == SOF_TIMER_OK);
    CHECK(t.ready == 1);
    CHECK(t.running == 0);
    CHECK(t.started == 0);
    CHECK(t.start == 0);
    CHECK(t.stop == 0);
    CHECK(t.overhead == 0);
    CHECK(sof_timer_free(&t) == SOF_TIMER_OK);
}

static void test_not_started(void) {
    sof_timer_t t;
    CHECK(sof_timer_init(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_elapsed_ns(&t) == 0);
    CHECK(sof_timer_elapsed_us(&t) == 0);
    CHECK(sof_timer_elapsed_ms(&t) == 0);
    CHECK(sof_timer_elapsed_seconds(&t) == 0);
    CHECK(sof_timer_stop(&t) == SOF_TIMER_NO_MATCH);
    CHECK(t.started == 0);
    CHECK(sof_timer_free(&t) == SOF_TIMER_OK);
}

static void test_start_stop(void) {
    sof_timer_t t;
    CHECK(sof_timer_init(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    CHECK(t.running == 1);
    CHECK(t.started == 1);
    spin(&t, INTERVAL);
    CHECK(sof_timer_stop(&t) == SOF_TIMER_OK);
    CHECK(t.running == 0);
    CHECK(t.started == 1);
    CHECK(t.stop >= t.start);
    uint64_t ns = sof_timer_elapsed_ns(&t);
    CHECK(ns >= INTERVAL);
    CHECK(ns < LIMIT);
    CHECK(ns == t.stop - t.start);

    // A stopped timer's measurement doesn't change.
    for (int i = 0; i < 1000; i++) {
        CHECK(sof_timer_elapsed_ns(&t) == ns);
    }

    // Stopping it again changes nothing.
    sof_timer_t before = t;
    CHECK(sof_timer_stop(&t) == SOF_TIMER_NO_MATCH);
    CHECK(t.stop == before.stop);
    CHECK(sof_timer_elapsed_ns(&t) == ns);
    CHECK(sof_timer_free(&t) == SOF_TIMER_OK);
}

static void test_units(void) {
    sof_timer_t t;
    CHECK(sof_timer_init(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    spin(&t, INTERVAL);
    CHECK(sof_timer_stop(&t) == SOF_TIMER_OK);
    uint64_t ns = sof_timer_elapsed_ns(&t);
    CHECK(sof_timer_elapsed_us(&t) == (double)ns / 1e3);
    CHECK(sof_timer_elapsed_ms(&t) == (double)ns / 1e6);
    CHECK(sof_timer_elapsed_seconds(&t) == (double)ns / 1e9);
    CHECK(sof_timer_elapsed_ms(&t) >= 2.0);
    CHECK(sof_timer_elapsed_seconds(&t) < 2.0);
    CHECK(sof_timer_free(&t) == SOF_TIMER_OK);
}

static void test_running(void) {
    // While the timer runs, the elapsed time is the time so far, and never
    // goes backwards.
    sof_timer_t t;
    CHECK(sof_timer_init(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    uint64_t previous = 0;
    for (int i = 0; i < 10000; i++) {
        uint64_t ns = sof_timer_elapsed_ns(&t);
        CHECK(ns >= previous);
        previous = ns;
    }
    spin(&t, INTERVAL);
    CHECK(sof_timer_elapsed_ns(&t) >= INTERVAL);
    CHECK(t.running == 1);
    CHECK(sof_timer_stop(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_elapsed_ns(&t) >= INTERVAL);
    CHECK(sof_timer_free(&t) == SOF_TIMER_OK);
}

static void test_restart(void) {
    // Starting again measures from the new start, whether the timer was
    // stopped or still running.
    sof_timer_t t;
    CHECK(sof_timer_init(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    spin(&t, 10 * INTERVAL);
    CHECK(sof_timer_stop(&t) == SOF_TIMER_OK);
    uint64_t first = t.start;
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    CHECK(t.start >= first + 10 * INTERVAL);
    CHECK(sof_timer_elapsed_ns(&t) < 10 * INTERVAL);
    spin(&t, 10 * INTERVAL);
    uint64_t second = t.start;
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    CHECK(t.running == 1);
    CHECK(t.start >= second + 10 * INTERVAL);
    CHECK(sof_timer_stop(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_elapsed_ns(&t) < 10 * INTERVAL);
    CHECK(sof_timer_free(&t) == SOF_TIMER_OK);
}

static void test_calibrate(void) {
    sof_timer_t t;
    CHECK(sof_timer_init(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_calibrate(&t) == SOF_TIMER_OK);
    // Reading the clock costs something under a millisecond; it may round
    // to 0 on a coarse clock.
    CHECK(t.overhead < 1000000);

    // The overhead is subtracted from each measurement.
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    spin(&t, INTERVAL);
    CHECK(sof_timer_stop(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_elapsed_ns(&t) == t.stop - t.start - t.overhead);

    // An immediate start and stop measures about nothing.
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_stop(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_elapsed_ns(&t) < 1000000);

    // A measurement shorter than the overhead reports 0.
    t.overhead = LIMIT;
    CHECK(sof_timer_elapsed_ns(&t) == 0);
    CHECK(sof_timer_elapsed_seconds(&t) == 0);
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_elapsed_ns(&t) == 0);
    CHECK(sof_timer_free(&t) == SOF_TIMER_OK);
}

static void test_calibrate_cost(void) {
    // The overhead is what one reading costs: timed in batches and divided,
    // so not rounded to 0 by a clock whose tick is longer than a reading.
    // A start and stop pair reads the clock twice, so it takes at least the
    // overhead.
    sof_timer_t t, pairs;
    CHECK(sof_timer_init(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_init(&pairs) == SOF_TIMER_OK);
    CHECK(sof_timer_calibrate(&t) == SOF_TIMER_OK);
    uint64_t fastest = UINT64_MAX;
    for (int round = 0; round < 5; round++) {
        CHECK(sof_timer_start(&pairs) == SOF_TIMER_OK);
        for (int i = 0; i < 1000; i++) {
            sof_timer_start(&t);
            sof_timer_stop(&t);
        }
        CHECK(sof_timer_stop(&pairs) == SOF_TIMER_OK);
        if (sof_timer_elapsed_ns(&pairs) < fastest) {
            fastest = sof_timer_elapsed_ns(&pairs);
        }
    }
    CHECK(t.overhead <= fastest / 1000 + 1);
#ifdef __APPLE__
    // On macOS the clock ticks in tens of nanoseconds, not microseconds, so
    // an empty start and stop sometimes measures more than 0 but well under
    // a microsecond.
    t.overhead = 0;
    uint64_t shortest = UINT64_MAX;
    for (int i = 0; i < 100000; i++) {
        sof_timer_start(&t);
        sof_timer_stop(&t);
        uint64_t ns = sof_timer_elapsed_ns(&t);
        if (ns != 0 && ns < shortest) {
            shortest = ns;
        }
    }
    CHECK(shortest < 1000);
#endif
    CHECK(sof_timer_free(&t) == SOF_TIMER_OK);
}

static void test_calibrate_running(void) {
    // Calibrating a running timer leaves the measurement alone.
    sof_timer_t t;
    CHECK(sof_timer_init(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    uint64_t start = t.start;
    CHECK(sof_timer_calibrate(&t) == SOF_TIMER_OK);
    CHECK(t.running == 1);
    CHECK(t.start == start);
    spin(&t, INTERVAL);
    CHECK(sof_timer_stop(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_elapsed_ns(&t) >= INTERVAL - t.overhead);
    uint64_t stop = t.stop;
    CHECK(sof_timer_calibrate(&t) == SOF_TIMER_OK);
    CHECK(t.running == 0);
    CHECK(t.start == start);
    CHECK(t.stop == stop);
    CHECK(sof_timer_free(&t) == SOF_TIMER_OK);
}

static void test_clear(void) {
    sof_timer_t t;
    CHECK(sof_timer_init(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_calibrate(&t) == SOF_TIMER_OK);
    uint64_t overhead = t.overhead;
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    spin(&t, INTERVAL);
    CHECK(sof_timer_clear(&t) == SOF_TIMER_OK);
    CHECK(t.ready == 1);
    CHECK(t.running == 0);
    CHECK(t.started == 0);
    CHECK(t.overhead == overhead);
    CHECK(sof_timer_elapsed_ns(&t) == 0);
    CHECK(sof_timer_stop(&t) == SOF_TIMER_NO_MATCH);

    // It can be used again.
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    spin(&t, INTERVAL);
    CHECK(sof_timer_stop(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_elapsed_ns(&t) >= INTERVAL - overhead);
    CHECK(sof_timer_free(&t) == SOF_TIMER_OK);
}

static void test_separate(void) {
    // Timers don't share anything: each has its own interval and overhead,
    // and a copy is a timer of its own.
    sof_timer_t a, b;
    CHECK(sof_timer_init(&a) == SOF_TIMER_OK);
    CHECK(sof_timer_init(&b) == SOF_TIMER_OK);
    CHECK(sof_timer_calibrate(&a) == SOF_TIMER_OK);
    CHECK(b.overhead == 0);
    CHECK(sof_timer_start(&a) == SOF_TIMER_OK);
    spin(&a, 10 * INTERVAL);
    CHECK(sof_timer_start(&b) == SOF_TIMER_OK);
    CHECK(sof_timer_stop(&b) == SOF_TIMER_OK);
    CHECK(sof_timer_stop(&a) == SOF_TIMER_OK);
    CHECK(sof_timer_elapsed_ns(&a) >= 10 * INTERVAL - a.overhead);
    CHECK(sof_timer_elapsed_ns(&b) < 10 * INTERVAL);

    sof_timer_t c = a;
    CHECK(sof_timer_elapsed_ns(&c) == sof_timer_elapsed_ns(&a));
    CHECK(sof_timer_start(&c) == SOF_TIMER_OK);
    CHECK(a.running == 0);
    CHECK(a.start != c.start);
    CHECK(sof_timer_free(&c) == SOF_TIMER_OK);
    CHECK(a.ready == 1);
    CHECK(sof_timer_free(&a) == SOF_TIMER_OK);
    CHECK(sof_timer_free(&b) == SOF_TIMER_OK);
}

static void test_null(void) {
    CHECK(sof_timer_init(NULL) == SOF_TIMER_ERROR);
    CHECK(sof_timer_calibrate(NULL) == SOF_TIMER_ERROR);
    CHECK(sof_timer_clear(NULL) == SOF_TIMER_ERROR);
    CHECK(sof_timer_elapsed_ns(NULL) == 0);
    CHECK(sof_timer_elapsed_us(NULL) == 0);
    CHECK(sof_timer_elapsed_ms(NULL) == 0);
    CHECK(sof_timer_elapsed_seconds(NULL) == 0);
    CHECK(sof_timer_free(NULL) == SOF_TIMER_ERROR);
    CHECK(sof_timer_start(NULL) == SOF_TIMER_ERROR);
    CHECK(sof_timer_stop(NULL) == SOF_TIMER_ERROR);
}

/// @brief Checks that every function treats t as dead.
static void check_dead(sof_timer_t *t) {
    CHECK(t->ready == 0);
    CHECK(sof_timer_start(t) == SOF_TIMER_ERROR);
    CHECK(t->running == 0);
    CHECK(t->started == 0);
    CHECK(sof_timer_stop(t) == SOF_TIMER_ERROR);
    CHECK(sof_timer_calibrate(t) == SOF_TIMER_ERROR);
    CHECK(t->overhead == 0);
    CHECK(sof_timer_elapsed_ns(t) == 0);
    CHECK(sof_timer_elapsed_us(t) == 0);
    CHECK(sof_timer_elapsed_ms(t) == 0);
    CHECK(sof_timer_elapsed_seconds(t) == 0);
    CHECK(sof_timer_clear(t) == SOF_TIMER_ERROR);
    CHECK(sof_timer_free(t) == SOF_TIMER_ERROR);
}

static void test_freed(void) {
    sof_timer_t t;
    CHECK(sof_timer_init(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_calibrate(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    spin(&t, INTERVAL);
    CHECK(sof_timer_free(&t) == SOF_TIMER_OK);
    CHECK(t.start == 0);
    CHECK(t.stop == 0);
    check_dead(&t);

    // A zeroed sof_timer_t, never initialized, is dead too.
    sof_timer_t z = {0};
    check_dead(&z);

    // A freed timer can be initialized again.
    CHECK(sof_timer_init(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_start(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_stop(&t) == SOF_TIMER_OK);
    CHECK(sof_timer_free(&t) == SOF_TIMER_OK);
}

int main(void) {
    printf("sof_timer:\n");
    run("init", test_init);
    run("timer never started", test_not_started);
    run("start and stop", test_start_stop);
    run("units", test_units);
    run("elapsed time while running", test_running);
    run("starting again", test_restart);
    run("calibrate", test_calibrate);
    run("calibrated cost of a reading", test_calibrate_cost);
    run("calibrating a running timer", test_calibrate_running);
    run("clear", test_clear);
    run("separate timers and copies", test_separate);
    run("NULL arguments", test_null);
    run("freed timer", test_freed);
    return test_report();
}
