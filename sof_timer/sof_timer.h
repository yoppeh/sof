/**
 * @file sof_timer.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Measuring elapsed time in c.
 * @details A sof_timer_t is a stopwatch on the system's monotonic clock:
 * sof_timer_start() and sof_timer_stop() mark the interval, and the
 * sof_timer_elapsed_*() functions report it in nanoseconds, microseconds,
 * milliseconds or seconds, or report the time so far while the timer runs.
 * sof_timer_calibrate() measures what reading the clock costs, which is then
 * subtracted from every measurement.
 *
 * The clock is the POSIX clock_gettime() with CLOCK_MONOTONIC, which isn't
 * affected by changes to the time of day. On macOS, where CLOCK_MONOTONIC
 * ticks in whole microseconds, it is CLOCK_MONOTONIC_RAW, which ticks in
 * tens of nanoseconds and, like Linux's CLOCK_MONOTONIC, doesn't count time
 * asleep.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_TIMER_H
#define SOF_TIMER_H

#include <stdint.h>

/**
 * @brief Error codes for the sof_timer functions.
 * @details SOF_TIMER_NO_MEM is never returned, since the library allocates
 * nothing; it is there so the codes match those of the other libraries.
 */
typedef enum sof_timer_error_t {
    SOF_TIMER_OK = 0,
    SOF_TIMER_NO_MEM,
    SOF_TIMER_NO_MATCH,
    SOF_TIMER_ERROR,
} sof_timer_error_t;

/**
 * @brief A timer.
 * @details The application provides the sof_timer_t (on the stack, in
 * another structure or on the heap) and sets it up with sof_timer_init(). Its
 * fields can be read, but must not be changed by the application. Nothing
 * points into the sof_timer_t, so it can be moved or copied; a copy is a
 * separate timer with the same state.
 *
 * Every function accepts a NULL timer, and a timer that was freed (ready is
 * 0), as described for each.
 */
typedef struct sof_timer_t {
    /// @brief The clock's reading, in nanoseconds, when sof_timer_start() was
    /// last called.
    uint64_t start;
    /// @brief The clock's reading, in nanoseconds, when sof_timer_stop() was
    /// last called.
    uint64_t stop;
    /// @brief Nanoseconds subtracted from every measurement: the cost of
    /// reading the clock, as sof_timer_calibrate() measured it, or 0.
    uint64_t overhead;
    /// @brief 1 between sof_timer_start() and sof_timer_stop(), otherwise 0.
    int running;
    /// @brief 1 once sof_timer_start() has been called since
    /// sof_timer_init() or sof_timer_clear(), otherwise 0.
    int started;
    /// @brief 1 once sof_timer_init() has succeeded, 0 when freed.
    int ready;
} sof_timer_t;

/**
 * @brief Measure the cost of reading the clock, to subtract it from every
 * measurement.
 * @details Times batches of readings of the clock in a row, and keeps the
 * fastest batch's time, divided by its readings, in overhead, so a reading
 * cheaper than the clock's tick isn't measured as 0. A measurement shorter than overhead reports 0.
 * The interval being measured, if any, isn't changed: the timer can be
 * calibrated while it runs. Calibration takes well under a millisecond.
 * @param t The timer.
 * @return SOF_TIMER_OK, or SOF_TIMER_ERROR if t is NULL or not initialized,
 * or the clock couldn't be read (overhead is then unchanged).
 */
extern sof_timer_error_t sof_timer_calibrate(sof_timer_t *t);

/**
 * @brief Forget the measurement, as if the timer had never been started.
 * @details Stops the timer if it is running. The overhead from
 * sof_timer_calibrate() is kept.
 * @param t The timer.
 * @return SOF_TIMER_OK, or SOF_TIMER_ERROR if t is NULL or not initialized.
 */
extern sof_timer_error_t sof_timer_clear(sof_timer_t *t);

/**
 * @brief Return the elapsed time in milliseconds.
 * @details The same as sof_timer_elapsed_ns() divided by one million.
 * @param t The timer.
 * @return The elapsed time in milliseconds, or 0 in the cases where
 * sof_timer_elapsed_ns() returns 0.
 */
extern double sof_timer_elapsed_ms(const sof_timer_t *t);

/**
 * @brief Return the elapsed time in nanoseconds.
 * @details For a stopped timer, the time from sof_timer_start() to
 * sof_timer_stop(); for a running one, the time from sof_timer_start() until
 * now. The overhead is subtracted in either case.
 * @param t The timer.
 * @return The elapsed time in nanoseconds; 0 if it is shorter than overhead,
 * if the timer hasn't been started, or t is NULL or not initialized, or, for
 * a running timer, if the clock couldn't be read.
 */
extern uint64_t sof_timer_elapsed_ns(const sof_timer_t *t);

/**
 * @brief Return the elapsed time in seconds.
 * @details The same as sof_timer_elapsed_ns() divided by one billion.
 * @param t The timer.
 * @return The elapsed time in seconds, or 0 in the cases where
 * sof_timer_elapsed_ns() returns 0.
 */
extern double sof_timer_elapsed_seconds(const sof_timer_t *t);

/**
 * @brief Return the elapsed time in microseconds.
 * @details The same as sof_timer_elapsed_ns() divided by one thousand.
 * @param t The timer.
 * @return The elapsed time in microseconds, or 0 in the cases where
 * sof_timer_elapsed_ns() returns 0.
 */
extern double sof_timer_elapsed_us(const sof_timer_t *t);

/**
 * @brief Leave the timer empty.
 * @details The library allocates nothing, so there is nothing to release, but
 * the timer acts like one that was never initialized until sof_timer_init()
 * is called again. The sof_timer_t itself isn't freed; it belongs to the
 * caller.
 * @param t The timer.
 * @return SOF_TIMER_OK, or SOF_TIMER_ERROR if t is NULL or not initialized.
 */
extern sof_timer_error_t sof_timer_free(sof_timer_t *t);

/**
 * @brief Initialize a timer in a sof_timer_t the caller provides.
 * @details The timer starts stopped, with no measurement and an overhead of
 * 0; call sof_timer_calibrate() to set the overhead.
 * @param t The timer to initialize.
 * @return SOF_TIMER_OK, or SOF_TIMER_ERROR if t is NULL.
 */
extern sof_timer_error_t sof_timer_init(sof_timer_t *t);

/**
 * @brief Start the timer.
 * @details Records the clock's reading in start and sets running. Starting a
 * running timer starts it again from now.
 * @param t The timer.
 * @return SOF_TIMER_OK, or SOF_TIMER_ERROR if t is NULL or not initialized,
 * or the clock couldn't be read (the timer is then unchanged).
 */
extern sof_timer_error_t sof_timer_start(sof_timer_t *t);

/**
 * @brief Stop the timer.
 * @details Records the clock's reading in stop and clears running, so the
 * sof_timer_elapsed_*() functions report the interval from
 * sof_timer_start() to this call until the timer is started again.
 * @param t The timer.
 * @return SOF_TIMER_OK, SOF_TIMER_NO_MATCH if the timer isn't running (it
 * is unchanged), or SOF_TIMER_ERROR if t is NULL or not initialized, or the
 * clock couldn't be read (the timer is then unchanged, and still running).
 */
extern sof_timer_error_t sof_timer_stop(sof_timer_t *t);

#endif // SOF_TIMER_H
