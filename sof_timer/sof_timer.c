/**
 * @file sof_timer.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Measuring elapsed time in c.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <time.h>

#include "sof_timer.h"

/// @brief The batches of clock readings sof_timer_calibrate() times.
#define CALIBRATE_ROUNDS 20

/// @brief The clock readings in each batch.
#define CALIBRATE_READS 100

/// @brief The clock now() reads. On macOS, CLOCK_MONOTONIC ticks in whole
/// microseconds, while CLOCK_MONOTONIC_RAW ticks in tens of nanoseconds and
/// is cheaper to read. Elsewhere CLOCK_MONOTONIC is kept: Linux before 5.3
/// made CLOCK_MONOTONIC_RAW a system call.
#if defined(__APPLE__) && defined(CLOCK_MONOTONIC_RAW)
#define SOF_TIMER_CLOCK CLOCK_MONOTONIC_RAW
#else
#define SOF_TIMER_CLOCK CLOCK_MONOTONIC
#endif

static int now(uint64_t *ns);

/// @brief Reads the monotonic clock, in nanoseconds, into ns.
/// @return 1, or 0 if the clock couldn't be read.
static int now(uint64_t *ns) {
    struct timespec ts;
    if (clock_gettime(SOF_TIMER_CLOCK, &ts) != 0) {
        return 0;
    }
    *ns = (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
    return 1;
}

sof_timer_error_t sof_timer_calibrate(sof_timer_t *t) {
    if (t == NULL || !t->ready) {
        return SOF_TIMER_ERROR;
    }
    // One reading costs less than the clock's tick on some systems, so two
    // readings in a row often differ by 0. A batch of readings is timed
    // instead, and divided by the readings in it; the fastest batch is the
    // one least disturbed.
    uint64_t shortest = UINT64_MAX;
    for (int i = 0; i < CALIBRATE_ROUNDS; i++) {
        uint64_t first, last;
        if (!now(&first)) {
            return SOF_TIMER_ERROR;
        }
        for (int j = 0; j < CALIBRATE_READS; j++) {
            if (!now(&last)) {
                return SOF_TIMER_ERROR;
            }
        }
        if (last - first < shortest) {
            shortest = last - first;
        }
    }
    t->overhead = (shortest + CALIBRATE_READS / 2) / CALIBRATE_READS;
    return SOF_TIMER_OK;
}

sof_timer_error_t sof_timer_clear(sof_timer_t *t) {
    if (t == NULL || !t->ready) {
        return SOF_TIMER_ERROR;
    }
    t->start = 0;
    t->stop = 0;
    t->running = 0;
    t->started = 0;
    return SOF_TIMER_OK;
}

double sof_timer_elapsed_ms(const sof_timer_t *t) {
    return (double)sof_timer_elapsed_ns(t) / 1e6;
}

uint64_t sof_timer_elapsed_ns(const sof_timer_t *t) {
    if (t == NULL || !t->ready || !t->started) {
        return 0;
    }
    uint64_t end = t->stop;
    if (t->running && !now(&end)) {
        return 0;
    }
    uint64_t total = end - t->start;
    return total > t->overhead ? total - t->overhead : 0;
}

double sof_timer_elapsed_seconds(const sof_timer_t *t) {
    return (double)sof_timer_elapsed_ns(t) / 1e9;
}

double sof_timer_elapsed_us(const sof_timer_t *t) {
    return (double)sof_timer_elapsed_ns(t) / 1e3;
}

sof_timer_error_t sof_timer_free(sof_timer_t *t) {
    if (t == NULL || !t->ready) {
        return SOF_TIMER_ERROR;
    }
    *t = (sof_timer_t){0};
    return SOF_TIMER_OK;
}

sof_timer_error_t sof_timer_init(sof_timer_t *t) {
    if (t == NULL) {
        return SOF_TIMER_ERROR;
    }
    *t = (sof_timer_t){0};
    t->ready = 1;
    return SOF_TIMER_OK;
}

sof_timer_error_t sof_timer_start(sof_timer_t *t) {
    if (t == NULL || !t->ready) {
        return SOF_TIMER_ERROR;
    }
    uint64_t ns;
    if (!now(&ns)) {
        return SOF_TIMER_ERROR;
    }
    t->start = ns;
    t->stop = ns;
    t->running = 1;
    t->started = 1;
    return SOF_TIMER_OK;
}

sof_timer_error_t sof_timer_stop(sof_timer_t *t) {
    if (t == NULL || !t->ready) {
        return SOF_TIMER_ERROR;
    }
    if (!t->running) {
        return SOF_TIMER_NO_MATCH;
    }
    uint64_t ns;
    if (!now(&ns)) {
        return SOF_TIMER_ERROR;
    }
    t->stop = ns;
    t->running = 0;
    return SOF_TIMER_OK;
}
