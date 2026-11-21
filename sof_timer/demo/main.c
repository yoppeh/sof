/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Times several ways of reading, counting and hashing a file, and
 * prints them as a table.
 * @details Each job does one thing to the whole file: reads it in calls of
 * a given size, counts its lines, or hashes it with sof_hash. Each is run
 * once untimed, to bring the file into memory and the cache, then the
 * number of times -r asks for, each run timed with a sof_timer that has
 * been calibrated, and the table gives the best, median and worst of the
 * runs, the rate of the best, and the job's result. Before the table it
 * prints what reading the clock costs, which the timer subtracts from every
 * run, and the smallest step in which the clock advances.
 *
 *     timeit [-h] [-r runs] file
 *
 * Its options are read with sof_copt. It exits with 0, 1 if the file
 * couldn't be read or memory ran out, and 2 for a usage error.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_copt.h"
#include "sof_hash.h"
#include "sof_sort.h"
#include "sof_timer.h"

/// @brief The exit codes.
#define EXIT_DONE 0
#define EXIT_TROUBLE 1
#define EXIT_USAGE 2

/// @brief The code runs_validate() returns for an argument that isn't a
/// positive number, after printing why. sof_copt keeps it in the option's
/// stop_value.
#define BAD_NUMBER 1

/// @brief The number of timed runs of each job when -r isn't given.
#define DEFAULT_RUNS 9

/// @brief The most timed runs -r allows.
#define MAX_RUNS 1000

/// @brief How many steps of the clock measure_resolution() watches.
#define RESOLUTION_STEPS 1000

/// @brief What every job works on: the file, by name and read into memory.
typedef struct input_t {
    /// @brief The file's path, for the jobs that read it.
    const char *path;
    /// @brief The file's bytes.
    unsigned char *data;
    /// @brief The number of bytes in data.
    size_t size;
    /// @brief The size data was allocated with, to free it with.
    size_t capacity;
} input_t;

/// @brief A job: does its work on the input and sets *result to what it
/// found. Returns 0, or -1 if it couldn't, such as when the file can't be
/// opened.
typedef int (*job_func_t)(const input_t *in, size_t param, uint64_t *result);

/// @brief A job in the table.
typedef struct job_t {
    /// @brief The job's name, its row's first column.
    const char *name;
    /// @brief The job.
    job_func_t func;
    /// @brief Passed to func: the size of each read, for the read jobs.
    size_t param;
    /// @brief 1 to print the result in hexadecimal, as for a hash.
    int hex;
} job_t;

static int compare_ns(const void *a, const void *b, void *ctx);
static void format_time(char *buffer, size_t size, double ns);
static int job_getc_lines(const input_t *in, size_t param, uint64_t *result);
static int job_hash(const input_t *in, size_t param, uint64_t *result);
static int job_loop_lines(const input_t *in, size_t param, uint64_t *result);
static int job_memchr_lines(const input_t *in, size_t param, uint64_t *result);
static int job_read(const input_t *in, size_t param, uint64_t *result);
static uint64_t measure_resolution(void);
static int read_input(input_t *in, const char *path);
static int run_job(const job_t *job, const input_t *in, sof_timer_t *t, uint64_t *times, size_t runs);
static int runs_validate(sof_copt_t *option, void *ctx);
static void usage(FILE *stream);

/// @brief Where the jobs' results go, so the compiler can't drop the work.
static volatile uint64_t sink;

/// @brief The jobs, in the order of the table.
static const job_t jobs[] = {
    {"read, 512 B calls", job_read, 512, 0},
    {"read, 8 KiB calls", job_read, 8192, 0},
    {"read, 64 KiB calls", job_read, 65536, 0},
    {"read, 1 MiB calls", job_read, 1048576, 0},
    {"lines, getc", job_getc_lines, 0, 0},
    {"lines, byte loop", job_loop_lines, 0, 0},
    {"lines, memchr", job_memchr_lines, 0, 0},
    {"hash, sof_hash_bytes", job_hash, 0, 1},
};

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_runs = {
    .option = "-r",
    .description = "time each job this many times (default 9)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = runs_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_runs,
};

/// @brief Compares two times, in nanoseconds, for sof_sort.
static int compare_ns(const void *a, const void *b, void *ctx) {
    (void)ctx;
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/// @brief Writes a time given in nanoseconds into buffer, in the unit that
/// suits it: ns, us, ms or s.
static void format_time(char *buffer, size_t size, double ns) {
    if (ns < 1e3) {
        snprintf(buffer, size, "%.0f ns", ns);
    } else if (ns < 1e6) {
        snprintf(buffer, size, "%.2f us", ns / 1e3);
    } else if (ns < 1e9) {
        snprintf(buffer, size, "%.2f ms", ns / 1e6);
    } else {
        snprintf(buffer, size, "%.3f s", ns / 1e9);
    }
}

/// @brief Counts the newlines in the file, reading it a byte at a time with
/// getc() through stdio's buffer.
static int job_getc_lines(const input_t *in, size_t param, uint64_t *result) {
    (void)param;
    FILE *f = fopen(in->path, "rb");
    if (f == NULL) {
        return -1;
    }
    uint64_t lines = 0;
    int ch;
    while ((ch = getc(f)) != EOF) {
        lines += ch == '\n';
    }
    int failed = ferror(f);
    fclose(f);
    *result = lines;
    return failed ? -1 : 0;
}

/// @brief Hashes the file's bytes, in memory, with sof_hash_bytes().
static int job_hash(const input_t *in, size_t param, uint64_t *result) {
    (void)param;
    *result = sof_hash_bytes(in->data, in->size, 0);
    return 0;
}

/// @brief Counts the newlines in the file's bytes, in memory, looking at
/// each byte in turn.
static int job_loop_lines(const input_t *in, size_t param, uint64_t *result) {
    (void)param;
    uint64_t lines = 0;
    for (size_t i = 0; i < in->size; i++) {
        lines += in->data[i] == '\n';
    }
    *result = lines;
    return 0;
}

/// @brief Counts the newlines in the file's bytes, in memory, with
/// memchr().
static int job_memchr_lines(const input_t *in, size_t param, uint64_t *result) {
    (void)param;
    uint64_t lines = 0;
    const unsigned char *p = in->data, *end = in->data + in->size;
    while (p < end && (p = memchr(p, '\n', (size_t)(end - p))) != NULL) {
        lines++;
        p++;
    }
    *result = lines;
    return 0;
}

/// @brief Reads the file to its end in calls of param bytes, with stdio's
/// buffering turned off, so each call is a read from the system, and sets
/// *result to the bytes read.
static int job_read(const input_t *in, size_t param, uint64_t *result) {
    FILE *f = fopen(in->path, "rb");
    if (f == NULL) {
        return -1;
    }
    unsigned char *buffer = sof_alloc_malloc(NULL, param);
    if (buffer == NULL) {
        fclose(f);
        return -1;
    }
    setvbuf(f, NULL, _IONBF, 0);
    uint64_t bytes = 0;
    size_t n;
    while ((n = fread(buffer, 1, param, f)) > 0) {
        bytes += n;
    }
    int failed = ferror(f);
    sof_alloc_free(NULL, buffer, param);
    fclose(f);
    *result = bytes;
    return failed ? -1 : 0;
}

/// @brief Finds the smallest step in which the clock advances, as a running
/// sof_timer sees it.
/// @details The timer isn't calibrated, so each reading is the raw time
/// since it started. Readings are taken until they have changed
/// RESOLUTION_STEPS times, and the smallest change is the resolution.
/// @return The resolution in nanoseconds, or 0 if the clock couldn't be
/// read.
static uint64_t measure_resolution(void) {
    sof_timer_t t;
    if (sof_timer_init(&t) != SOF_TIMER_OK || sof_timer_start(&t) != SOF_TIMER_OK) {
        return 0;
    }
    uint64_t smallest = UINT64_MAX, last = sof_timer_elapsed_ns(&t);
    for (int steps = 0; steps < RESOLUTION_STEPS;) {
        uint64_t now = sof_timer_elapsed_ns(&t);
        if (now != last) {
            if (now - last < smallest) {
                smallest = now - last;
            }
            last = now;
            steps++;
        }
    }
    sof_timer_free(&t);
    return smallest;
}

/// @brief Reads the file at path into in.
/// @return 0, or -1 if it couldn't be read or memory ran out, after printing
/// why.
static int read_input(input_t *in, const char *path) {
    *in = (input_t){path, NULL, 0, 0};
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "timeit: %s: %s\n", path, strerror(errno));
        return -1;
    }
    int rc = 0;
    for (;;) {
        if (in->size == in->capacity) {
            size_t capacity = in->capacity == 0 ? 65536 : in->capacity * 2;
            unsigned char *grown = sof_alloc_realloc(NULL, in->data, in->capacity, capacity);
            if (grown == NULL) {
                fprintf(stderr, "timeit: out of memory reading %s\n", path);
                rc = -1;
                break;
            }
            in->data = grown;
            in->capacity = capacity;
        }
        size_t n = fread(in->data + in->size, 1, in->capacity - in->size, f);
        in->size += n;
        if (n == 0) {
            break;
        }
    }
    if (rc == 0 && ferror(f)) {
        fprintf(stderr, "timeit: %s: read error\n", path);
        rc = -1;
    }
    fclose(f);
    if (rc != 0) {
        sof_alloc_free(NULL, in->data, in->capacity);
        *in = (input_t){path, NULL, 0, 0};
    }
    return rc;
}

/// @brief Runs a job once untimed, then runs times, timing each with t, and
/// prints its row of the table.
/// @return 0, or -1 if the job failed, after printing why.
static int run_job(const job_t *job, const input_t *in, sof_timer_t *t, uint64_t *times, size_t runs) {
    uint64_t result;
    if (job->func(in, job->param, &result) != 0) {
        fprintf(stderr, "timeit: %s: %s failed\n", in->path, job->name);
        return -1;
    }
    sink = result;
    for (size_t i = 0; i < runs; i++) {
        sof_timer_start(t);
        int rc = job->func(in, job->param, &result);
        sof_timer_stop(t);
        if (rc != 0) {
            fprintf(stderr, "timeit: %s: %s failed\n", in->path, job->name);
            return -1;
        }
        sink = result;
        times[i] = sof_timer_elapsed_ns(t);
    }
    sof_sort_insertion(times, runs, sizeof(uint64_t), compare_ns, NULL);
    double median = runs % 2 == 1 ? (double)times[runs / 2] : ((double)times[runs / 2 - 1] + (double)times[runs / 2]) / 2;
    char best[32], middle[32], worst[32], rate[32];
    format_time(best, sizeof(best), (double)times[0]);
    format_time(middle, sizeof(middle), median);
    format_time(worst, sizeof(worst), (double)times[runs - 1]);
    if (times[0] > 0) {
        snprintf(rate, sizeof(rate), "%.0f", (double)in->size * 1e3 / (double)times[0]);
    } else {
        snprintf(rate, sizeof(rate), "-");
    }
    printf("%-22s %10s %10s %10s %8s  ", job->name, best, middle, worst, rate);
    printf(job->hex ? "%016llx\n" : "%llu\n", (unsigned long long)result);
    return 0;
}

/// @brief Reads the argument of -r into the number of runs in ctx, printing
/// why if it isn't a number from 1 to MAX_RUNS.
static int runs_validate(sof_copt_t *option, void *ctx) {
    const char *s = option->value;
    char *end;
    errno = 0;
    unsigned long value = strtoul(s, &end, 10);
    if (*s < '0' || *s > '9' || *end != '\0' || errno != 0 || value < 1 || value > MAX_RUNS) {
        fprintf(stderr, "timeit: -r needs a number from 1 to %d, not \"%s\"\n", MAX_RUNS, s);
        return BAD_NUMBER;
    }
    *(size_t *)ctx = (size_t)value;
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: timeit [-h] [-r runs] file\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Times ways of reading the file, counting its lines and hashing it, and\n"
        "prints the best, median and worst run of each, the rate of the best in\n"
        "MB/s, and what each found. Exits with 0, 1 if the file can't be read, 2\n"
        "for bad options.\n");
}

int main(int ac, char **av) {
    size_t runs = DEFAULT_RUNS;
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &runs, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "timeit: unknown option %s\n", av[index]);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "timeit: %s needs an argument\n", option->option);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "timeit: %s takes no argument\n", option->option);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_STOPPED:
        return EXIT_USAGE;
    default:
        fprintf(stderr, "timeit: can't read the options (error %d)\n", (int)err);
        return EXIT_USAGE;
    }
    if (opt_help.present) {
        usage(stdout);
        return EXIT_DONE;
    }
    if (ac - index != 1 || strcmp(av[index], "-") == 0) {
        fprintf(stderr, "timeit: give one file, which is read many times\n");
        usage(stderr);
        return EXIT_USAGE;
    }

    sof_timer_t t;
    if (sof_timer_init(&t) != SOF_TIMER_OK || sof_timer_calibrate(&t) != SOF_TIMER_OK) {
        fprintf(stderr, "timeit: can't read the clock\n");
        return EXIT_TROUBLE;
    }
    input_t in;
    if (read_input(&in, av[index]) != 0) {
        return EXIT_TROUBLE;
    }
    uint64_t *times = sof_alloc_calloc(NULL, runs, sizeof(uint64_t));
    if (times == NULL) {
        fprintf(stderr, "timeit: out of memory\n");
        sof_alloc_free(NULL, in.data, in.capacity);
        return EXIT_TROUBLE;
    }
    char overhead[32], resolution[32];
    format_time(overhead, sizeof(overhead), (double)t.overhead);
    format_time(resolution, sizeof(resolution), (double)measure_resolution());
    printf("file:  %s, %zu bytes\n", in.path, in.size);
    printf("clock: %s to read (subtracted from each run), advances in steps of %s\n", overhead, resolution);
    printf("runs:  %zu of each job, after one untimed\n\n", runs);
    printf("%-22s %10s %10s %10s %8s  %s\n", "job", "best", "median", "worst", "MB/s", "result");
    int status = EXIT_DONE;
    for (size_t i = 0; i < sizeof(jobs) / sizeof(jobs[0]); i++) {
        if (run_job(&jobs[i], &in, &t, times, runs) != 0) {
            status = EXIT_TROUBLE;
        }
    }
    sof_alloc_free(NULL, times, runs * sizeof(uint64_t));
    sof_alloc_free(NULL, in.data, in.capacity);
    sof_timer_free(&t);
    return status;
}
