/**
 * @file range.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief The lines program's range of lines, and its options, -f and -c.
 * @details -f first starts at line first of the input, and -c count reads
 * at most count lines from there. Both take a required argument, and their
 * missing callbacks set the defaults: from the first line, to the end.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>

#include "sof_copt.h"
#include "lines.h"

static int count_missing(sof_copt_t *option, void *ctx);
static int count_validate(sof_copt_t *option, void *ctx);
static int first_missing(sof_copt_t *option, void *ctx);
static int first_validate(sof_copt_t *option, void *ctx);

/// @brief The first line to print, from 1.
static size_t first;

/// @brief The most lines to read from first on.
static size_t count;

static sof_copt_t opt_heading = {
    .description = "Range:",
};

static sof_copt_t opt_first = {
    .option = "-f",
    .description = "start at this line (default 1)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = first_validate,
    .missing = first_missing,
};

static sof_copt_t opt_count = {
    .option = "-c",
    .description = "read at most this many lines from there (default all)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = count_validate,
    .missing = count_missing,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_heading,
    &opt_first,
    &opt_count,
};

/// @brief Without -c, reads to the end.
static int count_missing(sof_copt_t *option, void *ctx) {
    (void)option;
    (void)ctx;
    count = SIZE_MAX;
    return 0;
}

/// @brief Reads the argument of -c.
static int count_validate(sof_copt_t *option, void *ctx) {
    (void)ctx;
    return lines_number(option, 0, SIZE_MAX, &count);
}

/// @brief Without -f, starts at the first line.
static int first_missing(sof_copt_t *option, void *ctx) {
    (void)option;
    (void)ctx;
    first = 1;
    return 0;
}

/// @brief Reads the argument of -f.
static int first_validate(sof_copt_t *option, void *ctx) {
    (void)ctx;
    return lines_number(option, 1, SIZE_MAX, &first);
}

range_t range_select(size_t number) {
    if (number < first) {
        return RANGE_SKIP;
    }
    // number - first lines of the range have been read before this one.
    return number - first < count ? RANGE_PRINT : RANGE_STOP;
}
