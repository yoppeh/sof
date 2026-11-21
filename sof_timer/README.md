# sof_timer

A timer for measuring elapsed time in c.

The library has a `sof_timer_t` type, a stopwatch on the system's monotonic clock. The application provides the `sof_timer_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_timer_init()`. `sof_timer_calibrate()` optionally measures the cost of reading the clock, which is then subtracted from every measurement. `sof_timer_start()` and `sof_timer_stop()` mark the interval to measure, and `sof_timer_elapsed_ns()`, `sof_timer_elapsed_us()`, `sof_timer_elapsed_ms()` and `sof_timer_elapsed_seconds()` report it; called while the timer runs, they report the time so far. `sof_timer_clear()` forgets the measurement, and `sof_timer_free()` leaves the timer empty once the application is finished with it.

The clock is `clock_gettime()` with `CLOCK_MONOTONIC`, which counts nanoseconds and isn't affected by changes to the time of day. On macOS, where `CLOCK_MONOTONIC` only ticks in whole microseconds, it is `CLOCK_MONOTONIC_RAW` instead, which ticks every 41 ns or so on Apple silicon and is cheaper to read; like Linux's `CLOCK_MONOTONIC`, it doesn't count time the machine spends asleep. (Linux keeps `CLOCK_MONOTONIC`, since before Linux 5.3 reading `CLOCK_MONOTONIC_RAW` was a system call.) The calibration lives in each `sof_timer_t`, not in a global, so timers calibrated separately don't affect each other.

The library's demo is the [timeit](demo/) program in _demo/_, which times several ways of reading, counting and hashing a file and prints the best, median and worst of each as a table. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). All sof_timer declarations are in _sof_timer.h_. The library uses no other library in this collection, and allocates nothing. Beyond the c standard library it needs the POSIX `clock_gettime()` and `CLOCK_MONOTONIC` (and on macOS `CLOCK_MONOTONIC_RAW`), which Linux, macOS and the BSDs have; the c standard has no monotonic clock.

    sof_timer_t t;
    sof_timer_init(&t);
    sof_timer_calibrate(&t);                        // optional
    sof_timer_start(&t);
    ...                                         // the work to time
    sof_timer_stop(&t);
    printf("%.3f ms\n", sof_timer_elapsed_ms(&t));
    sof_timer_free(&t);

## Building

    make             # compiles the library and builds ./test and the demo, demo/timeit
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

The tests measure intervals of a few milliseconds by busy-waiting on the timer itself, and check each one against the clock readings the timer recorded and against generous bounds, so they pass on a loaded machine and under valgrind; they also check that the calibrated overhead is no more than a reading costs, and, on macOS, that the clock ticks in well under a microsecond.

To use the library in another project, copy _sof_timer.c_ and _sof_timer.h_ into it and compile _sof_timer.c_ with the project's own sources. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_ and _sof_timer.c_ with _-pthread_.

## Functions

`sof_timer_error_t sof_timer_calibrate(sof_timer_t *t)`

Measure the cost of reading the clock. Batches of a hundred readings in a row are timed, and the fastest batch, divided by the readings in it, is kept in the timer's `overhead`, so a reading that costs less than the clock's tick still counts. The overhead is subtracted from every measurement after that; a measurement shorter than `overhead` reports **0**. The interval being measured isn't changed, so a running timer can be calibrated. It takes well under a millisecond. Returns **SOF_TIMER_OK**, or **SOF_TIMER_ERROR** if _t_ isn't initialized or the clock couldn't be read, in which case `overhead` is unchanged.

`sof_timer_error_t sof_timer_clear(sof_timer_t *t)`

Forget the measurement, as if the timer had never been started, stopping it if it runs. The `overhead` from `sof_timer_calibrate()` is kept. Returns **SOF_TIMER_OK**, or **SOF_TIMER_ERROR** if _t_ isn't initialized.

`double sof_timer_elapsed_ms(const sof_timer_t *t)`

Return the elapsed time in milliseconds: `sof_timer_elapsed_ns()` divided by one million, so **0** wherever that returns **0**.

`uint64_t sof_timer_elapsed_ns(const sof_timer_t *t)`

Return the elapsed time in nanoseconds, less `overhead`. For a stopped timer it is the time from `sof_timer_start()` to `sof_timer_stop()`, the same on every call; for a running timer, the time from `sof_timer_start()` until now. Returns **0** if the time is shorter than `overhead`, the timer hasn't been started since `sof_timer_init()` or `sof_timer_clear()`, _t_ isn't initialized, or, for a running timer, the clock couldn't be read.

`double sof_timer_elapsed_seconds(const sof_timer_t *t)`

Return the elapsed time in seconds: `sof_timer_elapsed_ns()` divided by one billion, so **0** wherever that returns **0**.

`double sof_timer_elapsed_us(const sof_timer_t *t)`

Return the elapsed time in microseconds: `sof_timer_elapsed_ns()` divided by one thousand, so **0** wherever that returns **0**.

`sof_timer_error_t sof_timer_free(sof_timer_t *t)`

Leave the timer empty. The library allocates nothing, so nothing is released, but the timer acts as one that was never initialized until `sof_timer_init()` is called again. The `sof_timer_t` itself belongs to the caller and isn't freed. Returns **SOF_TIMER_OK**, or **SOF_TIMER_ERROR** if _t_ isn't initialized.

`sof_timer_error_t sof_timer_init(sof_timer_t *t)`

Initialize the timer _t_ the caller provides. It starts stopped, with no measurement and an `overhead` of **0**. Returns **SOF_TIMER_OK**, or **SOF_TIMER_ERROR** if _t_ is **NULL**.

`sof_timer_error_t sof_timer_start(sof_timer_t *t)`

Start the timer: record the clock's reading in `start` and set `running`. Starting a running timer starts it again from now. Returns **SOF_TIMER_OK**, or **SOF_TIMER_ERROR** if _t_ isn't initialized or the clock couldn't be read, in which case the timer is unchanged.

`sof_timer_error_t sof_timer_stop(sof_timer_t *t)`

Stop the timer: record the clock's reading in `stop` and clear `running`, so the elapsed time stays the interval from `sof_timer_start()` to this call until the timer is started again. Returns **SOF_TIMER_OK**, **SOF_TIMER_NO_MATCH** if the timer isn't running, in which case it is unchanged, or **SOF_TIMER_ERROR** if _t_ isn't initialized or the clock couldn't be read, in which case the timer is unchanged and still running.

Every function accepts a **NULL** _t_, and a timer that was freed or never initialized (`ready` is **0**, as in a zeroed `sof_timer_t`): the `sof_timer_elapsed_*()` functions return **0**, and the others return **SOF_TIMER_ERROR**. Such a timer stays that way until `sof_timer_init()` is called on it.

## Types

    typedef enum sof_timer_error_t {
        SOF_TIMER_OK = 0,
        SOF_TIMER_NO_MEM,
        SOF_TIMER_NO_MATCH,
        SOF_TIMER_ERROR,
    } sof_timer_error_t;

**SOF_TIMER_NO_MEM** is never returned, since the library allocates nothing; it is there so the codes match those of the other libraries. **SOF_TIMER_NO_MATCH** is returned by `sof_timer_stop()` when the timer isn't running.

    typedef struct sof_timer_t {
        uint64_t start;     // the clock's reading at sof_timer_start(), in nanoseconds
        uint64_t stop;      // the clock's reading at sof_timer_stop(), in nanoseconds
        uint64_t overhead;  // subtracted from each measurement; set by sof_timer_calibrate()
        int running;        // 1 between sof_timer_start() and sof_timer_stop()
        int started;        // 1 once started since sof_timer_init() or sof_timer_clear()
        int ready;          // 1 once sof_timer_init() has succeeded, 0 when freed
    } sof_timer_t;

The fields can be read but should not be changed by the application. `start` and `stop` are readings of the monotonic clock, whose zero is arbitrary, so only their difference means anything. Nothing points into a `sof_timer_t`, so it can be moved or copied; a copy is a separate timer that starts with the same state.

## Threading

The library does no locking of its own. It has no global state, so different timers can be used on different threads freely, each with its own calibration. A timer shared between threads needs a lock supplied by the application; a readers-writer lock (such as `pthread_rwlock_t`) fits, since the read-only functions can run at the same time as each other.

**Read-only** (any number of threads at once, as long as no thread is writing): `sof_timer_elapsed_ns()`, `sof_timer_elapsed_us()`, `sof_timer_elapsed_ms()`, `sof_timer_elapsed_seconds()`, and reading the fields. On a running timer these read the clock, but don't change the timer.

**Writes** (one thread, with no readers): `sof_timer_init()`, `sof_timer_calibrate()`, `sof_timer_start()`, `sof_timer_stop()`, `sof_timer_clear()` and `sof_timer_free()`.

The usual pattern needs no lock at all: one thread starts and stops the timer, and reports the result itself. If other threads watch the time so far while one thread runs the timer, as a progress display might, they need the read lock for each `sof_timer_elapsed_*()` call, and the timing thread the write lock for `sof_timer_start()` and `sof_timer_stop()`.
